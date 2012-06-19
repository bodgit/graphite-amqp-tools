/*
 * Copyright (c) 2012 Matt Dainty <matt@bodgit-n-scarper.com>
 * Copyright (c) 2002, 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
 * Copyright (c) 2001 Daniel Hartmeier.  All rights reserved.
 * Copyright (c) 2001 Theo de Raadt.  All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

%{
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "graphite-enqueue.h"

TAILQ_HEAD(files, file)		 files = TAILQ_HEAD_INITIALIZER(files);
static struct file {
	TAILQ_ENTRY(file)	 entry;
	FILE			*stream;
	char			*name;
	int			 lineno;
	int			 errors;
} *file, *topfile;
struct file	*pushfile(const char *);
int		 popfile(void);
int		 yyparse(void);
int		 yylex(void);
int		 yyerror(const char *, ...);
int		 kw_cmp(const void *, const void *);
int		 lookup(char *);
int		 lgetc(int);
int		 lungetc(int);
int		 findeol(void);

//struct ntpd_conf		*conf;

struct amqp_opts {
	int	 port;
	char	*vhost;
	char	*user;
	char	*password;
} amqp_opts;
void		amqp_opts_default(void);

typedef struct {
	union {
		int64_t			 number;
		char			*string;
		struct ntp_addr_wrap	*addr;
		struct amqp_opts	 opts;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	LISTEN ON
%token	PORT AMQP VHOST USER PASSWORD EXCHANGE TYPE FEDERATED METRICSOURCE
%token	ERROR
%token	<v.string>		STRING
%token	<v.number>		NUMBER
%type	<v.addr>		address
%type	<v.opts>		listen_opts listen_opts_l listen_opt
%type	<v.opts>		amqp_opts amqp_opts_l amqp_opt
%type	<v.opts>		exchange_opts exchange_opts_l exchange_opt
%type	<v.opts>		port
%type	<v.opts>		vhost
%type	<v.opts>		user
%type	<v.opts>		password
%type	<v.opts>		type
%type	<v.opts>		federated
%%

grammar		: /* empty */
		| grammar '\n'
		| grammar main '\n'
		| grammar error '\n'		{ file->errors++; }
		;

main		: LISTEN ON address listen_opts	{
			struct listen_addr	*la;
			struct ntp_addr		*h, *next;

			if ((h = $3->a) == NULL &&
			    (host_dns($3->name, &h) == -1 || !h)) {
				yyerror("could not resolve \"%s\"", $3->name);
				free($3->name);
				free($3);
				YYERROR;
			}

			for (; h != NULL; h = next) {
				next = h->next;
				la = calloc(1, sizeof(struct listen_addr));
				if (la == NULL)
					fatal("listen on calloc");
				la->fd = -1;
				memcpy(&la->sa, &h->ss,
				    sizeof(struct sockaddr_storage));
				TAILQ_INSERT_TAIL(&conf->listen_addrs, la,
				    entry);
				free(h);
			}
			free($3->name);
			free($3);
		}
		| AMQP address amqp_opts	{
			struct ntp_peer		*p;
			struct ntp_addr		*h, *next;

			h = $2->a;
			do {
				if (h != NULL) {
					next = h->next;
					if (h->ss.ss_family != AF_INET &&
					    h->ss.ss_family != AF_INET6) {
						yyerror("IPv4 or IPv6 address "
						    "or hostname expected");
						free(h);
						free($2->name);
						free($2);
						YYERROR;
					}
					h->next = NULL;
				} else
					next = NULL;

				p = new_peer();
				p->weight = $3.weight;
				p->rtable = $3.rtable;
				p->addr = h;
				p->addr_head.a = h;
				p->addr_head.pool = 1;
				p->addr_head.name = strdup($2->name);
				if (p->addr_head.name == NULL)
					fatal(NULL);
				if (p->addr != NULL)
					p->state = STATE_DNS_DONE;
				if (!(p->rtable > 0 && p->addr &&
				    p->addr->ss.ss_family != AF_INET))
					TAILQ_INSERT_TAIL(&conf->ntp_peers,
					    p, entry);
				h = next;
			} while (h != NULL);

			free($2->name);
			free($2);
		}
		| EXCHANGE STRING exchange_opts	{
		}
		| METRICSOURCE STRING		{
			if (!strcmp($2, "message"))
			else if (!strcmp($2, "key"))
			else {
				yyerror("unknown metric-source");
				free($2);
				YYERROR;
			}
			free($2);
		}
		;
/*		| SENSOR STRING	sensor_opts {
			struct ntp_conf_sensor	*s;

			s = new_sensor($2);
			s->weight = $3.weight;
			s->correction = $3.correction;
			s->refstr = $3.refstr;
			free($2);
			TAILQ_INSERT_TAIL(&conf->ntp_conf_sensors, s, entry);
		}
		;*/

address		: STRING		{
			if (($$ = calloc(1, sizeof(struct ntp_addr_wrap))) ==
			    NULL)
				fatal(NULL);
			if (host($1, &$$->a) == -1) {
				yyerror("could not parse address spec \"%s\"",
				    $1);
				free($1);
				free($$);
				YYERROR;
			}
			$$->name = $1;
		}
		;

listen_opts	:	{ opts_default(); }
		  listen_opts_l
			{ $$ = opts; }
		|	{ opts_default(); $$ = opts; }
		;
listen_opts_l	: listen_opts_l listen_opt
		| listen_opt
		;
listen_opt	: port
		;

amqp_opts	:	{ opts_default(); }
		  amqp_opts_l
			{ $$ = opts; }
		|	{ opts_default(); $$ = opts; }
		;
amqp_opts_l	: amqp_opts_l amqp_opt
		| amqp_opt
		;
amqp_opt	: amqp_port
		| amqp_vhost
		| amqp_user
		| amqp_password
		;

exchange_opts	:	{ opts_default(); }
		  exchange_opts_l
			{ $$ = opts; }
		|	{ opts_default(); $$ = opts; }
		;
exchange_opts_l	: exchange_opts_l exchange_opt
		| exchange_opt
		;
exchange_opt	: type
		| federated
		;

amqp_port	: PORT NUMBER {
			if ($2 < 1 || $2 > USHRT_MAX) {
				yyerror("port must be between 1 and 65535");
				YYERROR;
			}
			conf->amqp_port = $2;
		}
		;

amqp_vhost	: VHOST STRING {
			conf->amqp_vhost = $2;
		}
		;

amqp_user	: USER STRING {
			conf->amqp_user = $2;
		}
		;

amqp_password	: PASSWORD STRING {
			conf->amqp_password = $2;
		}
		;

type		: TYPE STRING {
			if (strcmp($2, "direct") && strcmp($2, "fanout") &&
			    strcmp($2, "topic")) {
				yyerror("unknown exchange type");
				free($2);
				YYERROR;
			}
			conf->amqp_type = $2;
		}
		;

federated	: FEDERATED STRING {
			conf->flags |= AMQP_FLAGS_FEDERATED;
			conf->federated = $2;
		}
		;

%%

void
amqp_opts_default(void)
{
	bzero(&amqp_opts, sizeof amqp_opts);
	opts.port = AMQP_DEFAULT_PORT;
	opts.vhost = AMQP_DEFAULT_VHOST;
	opts.user = AMQP_DEFAULT_USER;
	opts.password = AMQP_DEFAULT_PASSWORD;
}

struct keywords {
	const char	*k_name;
	int		 k_val;
};

int
yyerror(const char *fmt, ...)
{
	va_list		 ap;
	char		*nfmt;

	file->errors++;
	va_start(ap, fmt);
	if (asprintf(&nfmt, "%s:%d: %s", file->name, yylval.lineno, fmt) == -1)
		fatalx("yyerror asprintf");
	vlog(LOG_CRIT, nfmt, ap);
	va_end(ap);
	free(nfmt);
	return (0);
}

int
kw_cmp(const void *k, const void *e)
{
	return (strcmp(k, ((const struct keywords *)e)->k_name));
}

int
lookup(char *s)
{
	/* this has to be sorted always */
	static const struct keywords keywords[] = {
		{ "amqp",		AMQP},
		{ "exchange",		EXCHANGE},
		{ "federated",		FEDERATED},
		{ "in",			IN},
		{ "listen",		LISTEN},
		{ "metric-source",	METRICSOURCE},
		{ "on",			ON},
		{ "password",		PASSWORD},
		{ "port",		PORT},
		{ "type",		TYPE},
		{ "user",		USER},
		{ "vhost",		VHOST}
	};
	const struct keywords	*p;

	p = bsearch(s, keywords, sizeof(keywords)/sizeof(keywords[0]),
	    sizeof(keywords[0]), kw_cmp);

	if (p)
		return (p->k_val);
	else
		return (STRING);
}

#define MAXPUSHBACK	128

char	*parsebuf;
int	 parseindex;
char	 pushback_buffer[MAXPUSHBACK];
int	 pushback_index = 0;

int
lgetc(int quotec)
{
	int		c, next;

	if (parsebuf) {
		/* Read character from the parsebuffer instead of input. */
		if (parseindex >= 0) {
			c = parsebuf[parseindex++];
			if (c != '\0')
				return (c);
			parsebuf = NULL;
		} else
			parseindex++;
	}

	if (pushback_index)
		return (pushback_buffer[--pushback_index]);

	if (quotec) {
		if ((c = getc(file->stream)) == EOF) {
			yyerror("reached end of file while parsing "
			    "quoted string");
			if (file == topfile || popfile() == EOF)
				return (EOF);
			return (quotec);
		}
		return (c);
	}

	while ((c = getc(file->stream)) == '\\') {
		next = getc(file->stream);
		if (next != '\n') {
			c = next;
			break;
		}
		yylval.lineno = file->lineno;
		file->lineno++;
	}

	while (c == EOF) {
		if (file == topfile || popfile() == EOF)
			return (EOF);
		c = getc(file->stream);
	}
	return (c);
}

int
lungetc(int c)
{
	if (c == EOF)
		return (EOF);
	if (parsebuf) {
		parseindex--;
		if (parseindex >= 0)
			return (c);
	}
	if (pushback_index < MAXPUSHBACK-1)
		return (pushback_buffer[pushback_index++] = c);
	else
		return (EOF);
}

int
findeol(void)
{
	int	c;

	parsebuf = NULL;

	/* skip to either EOF or the first real EOL */
	while (1) {
		if (pushback_index)
			c = pushback_buffer[--pushback_index];
		else
			c = lgetc(0);
		if (c == '\n') {
			file->lineno++;
			break;
		}
		if (c == EOF)
			break;
	}
	return (ERROR);
}

int
yylex(void)
{
	char	 buf[8096];
	char	*p;
	int	 quotec, next, c;
	int	 token;

	p = buf;
	while ((c = lgetc(0)) == ' ' || c == '\t')
		; /* nothing */

	yylval.lineno = file->lineno;
	if (c == '#')
		while ((c = lgetc(0)) != '\n' && c != EOF)
			; /* nothing */

	switch (c) {
	case '\'':
	case '"':
		quotec = c;
		while (1) {
			if ((c = lgetc(quotec)) == EOF)
				return (0);
			if (c == '\n') {
				file->lineno++;
				continue;
			} else if (c == '\\') {
				if ((next = lgetc(quotec)) == EOF)
					return (0);
				if (next == quotec || c == ' ' || c == '\t')
					c = next;
				else if (next == '\n') {
					file->lineno++;
					continue;
				} else
					lungetc(next);
			} else if (c == quotec) {
				*p = '\0';
				break;
			}
			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return (findeol());
			}
			*p++ = (char)c;
		}
		yylval.v.string = strdup(buf);
		if (yylval.v.string == NULL)
			fatal("yylex: strdup");
		return (STRING);
	}

#define allowed_to_end_number(x) \
	(isspace(x) || x == ')' || x ==',' || x == '/' || x == '}' || x == '=')

	if (c == '-' || isdigit(c)) {
		do {
			*p++ = c;
			if ((unsigned)(p-buf) >= sizeof(buf)) {
				yyerror("string too long");
				return (findeol());
			}
		} while ((c = lgetc(0)) != EOF && isdigit(c));
		lungetc(c);
		if (p == buf + 1 && buf[0] == '-')
			goto nodigits;
		if (c == EOF || allowed_to_end_number(c)) {
			const char *errstr = NULL;

			*p = '\0';
			yylval.v.number = strtonum(buf, LLONG_MIN,
			    LLONG_MAX, &errstr);
			if (errstr) {
				yyerror("\"%s\" invalid number: %s",
				    buf, errstr);
				return (findeol());
			}
			return (NUMBER);
		} else {
nodigits:
			while (p > buf + 1)
				lungetc(*--p);
			c = *--p;
			if (c == '-')
				return (c);
		}
	}

#define allowed_in_string(x) \
	(isalnum(x) || (ispunct(x) && x != '(' && x != ')' && \
	x != '{' && x != '}' && x != '<' && x != '>' && \
	x != '!' && x != '=' && x != '/' && x != '#' && \
	x != ','))

	if (isalnum(c) || c == ':' || c == '_' || c == '*') {
		do {
			*p++ = c;
			if ((unsigned)(p-buf) >= sizeof(buf)) {
				yyerror("string too long");
				return (findeol());
			}
		} while ((c = lgetc(0)) != EOF && (allowed_in_string(c)));
		lungetc(c);
		*p = '\0';
		if ((token = lookup(buf)) == STRING)
			if ((yylval.v.string = strdup(buf)) == NULL)
				fatal("yylex: strdup");
		return (token);
	}
	if (c == '\n') {
		yylval.lineno = file->lineno;
		file->lineno++;
	}
	if (c == EOF)
		return (0);
	return (c);
}

struct file *
pushfile(const char *name)
{
	struct file	*nfile;

	if ((nfile = calloc(1, sizeof(struct file))) == NULL) {
		log_warn("malloc");
		return (NULL);
	}
	if ((nfile->name = strdup(name)) == NULL) {
		log_warn("malloc");
		free(nfile);
		return (NULL);
	}
	if ((nfile->stream = fopen(nfile->name, "r")) == NULL) {
		log_warn("%s", nfile->name);
		free(nfile->name);
		free(nfile);
		return (NULL);
	}
	nfile->lineno = 1;
	TAILQ_INSERT_TAIL(&files, nfile, entry);
	return (nfile);
}

int
popfile(void)
{
	struct file	*prev;

	if ((prev = TAILQ_PREV(file, files, entry)) != NULL)
		prev->errors += file->errors;

	TAILQ_REMOVE(&files, file, entry);
	fclose(file->stream);
	free(file->name);
	free(file);
	file = prev;
	return (file ? 0 : EOF);
}

int
parse_config(const char *filename, struct ntpd_conf *xconf)
{
	int		 errors = 0;

	conf = xconf;
	TAILQ_INIT(&conf->listen_addrs);
	TAILQ_INIT(&conf->ntp_peers);
	TAILQ_INIT(&conf->ntp_conf_sensors);

	if ((file = pushfile(filename)) == NULL) {
		return (-1);
	}
	topfile = file;

	yyparse();
	errors = file->errors;
	popfile();

	return (errors ? -1 : 0);
}
