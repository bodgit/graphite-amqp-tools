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

#include "dequeue.h"

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

struct dequeue		*conf;

struct opts {
	int		 port;
	char		*vhost;
	char		*user;
	char		*password;
	char		*type;
	char		*upstreams;
	long long	 ttl;
	int		 flags;
} opts;
void		 opts_default(void);

struct binding	*b;
struct binding	*init_binding(void);

/* FIXME */
#define YYSTYPE_IS_DECLARED 1
typedef struct {
	union {
		int64_t			 number;
		char			*string;
		//struct ntp_addr_wrap	*addr;
		struct opts		 opts;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	GRAPHITE
%token	AMQP VHOST USER PASSWORD
%token	EXCHANGE TYPE FEDERATED
%token	QUEUE MIRRORED TTL
%token	BINDING
%token	METRICSOURCE
%token	PORT
%token	ERROR
%token	<v.string>		STRING
%token	<v.number>		NUMBER
%type	<v.addr>		address
%type	<v.opts>		amqp_opts amqp_opts_l amqp_opt
%type	<v.opts>		exchange_opts exchange_opts_l exchange_opt
%type	<v.opts>		graphite_opts graphite_opts_l graphite_opt
%type	<v.opts>		queue_opts queue_opts_l queue_opt
%type	<v.opts>		port
%type	<v.opts>		vhost
%type	<v.opts>		user
%type	<v.opts>		password
%type	<v.opts>		type
%type	<v.opts>		federated
%type	<v.opts>		mirrored
%type	<v.opts>		ttl
%%

grammar		: /* empty */
		| grammar '\n'
		| grammar main '\n'
		| grammar error '\n'		{ file->errors++; }
		;

main		: GRAPHITE address graphite_opts	{
			conf->graphite_port = opts.port;
		}
		| AMQP address amqp_opts		{
			conf->amqp_port = opts.port;

			if (conf->vhost)
				free(conf->vhost);
			conf->vhost = (opts.vhost) ? opts.vhost : NULL;

			if (conf->user)
				free(conf->user);
			conf->user = (opts.user) ? opts.user : NULL;

			if (conf->password)
				free(conf->password);
			conf->password = (opts.password) ? opts.password : NULL;
		}
		| EXCHANGE STRING exchange_opts		{
			if (conf->exchange)
				free(conf->exchange);
			conf->exchange = $2;

			if (conf->type)
				free(conf->type);
			conf->type = (opts.type) ? opts.type : NULL;

			if (conf->upstreams)
				free(conf->upstreams);
			conf->upstreams = (opts.upstreams) ? opts.upstreams : NULL;
		}
		| QUEUE STRING queue_opts		{
			if (conf->queue)
				free(conf->queue);
			conf->queue = $2;
			conf->flags &= ~DEQUEUE_FLAG_MIRRORED_QUEUE;
			conf->flags |= opts.flags;
			conf->ttl = opts.ttl;
		}
		| BINDING STRING			{
			if ((b = init_binding()) == NULL)
				fatal(NULL);
			b->key = $2;
			LIST_INSERT_HEAD(&conf->bindings, b, entry);
		}
		| METRICSOURCE STRING			{
			if (!strcmp($2, "message"))
				conf->flags |= DEQUEUE_FLAG_METRIC_IN_MESSAGE;
			else if (!strcmp($2, "key"))
				conf->flags &= ~DEQUEUE_FLAG_METRIC_IN_MESSAGE;
			else {
				yyerror("unknown metric source");
				free($2);
				YYERROR;
			}
			free($2);
		}
		;
/*
		: LISTEN ON address listen_opts	{
			struct listen_addr	*la;
			struct ntp_addr		*h, *next;

			if ($3->a)
				$3->a->rtable = $4.rtable;
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
				la->rtable = $4.rtable;
				memcpy(&la->sa, &h->ss,
				    sizeof(struct sockaddr_storage));
				TAILQ_INSERT_TAIL(&conf->listen_addrs, la,
				    entry);
				free(h);
			}
			free($3->name);
			free($3);
		}
		| SERVERS address server_opts	{
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
		| SERVER address server_opts {
			struct ntp_peer		*p;
			struct ntp_addr		*h, *next;

			p = new_peer();
			for (h = $2->a; h != NULL; h = next) {
				next = h->next;
				if (h->ss.ss_family != AF_INET &&
				    h->ss.ss_family != AF_INET6) {
					yyerror("IPv4 or IPv6 address "
					    "or hostname expected");
					free(h);
					free(p);
					free($2->name);
					free($2);
					YYERROR;
				}
				h->next = p->addr;
				p->addr = h;
			}

			p->weight = $3.weight;
			p->rtable = $3.rtable;
			p->addr_head.a = p->addr;
			p->addr_head.pool = 0;
			p->addr_head.name = strdup($2->name);
			if (p->addr_head.name == NULL)
				fatal(NULL);
			if (p->addr != NULL)
				p->state = STATE_DNS_DONE;
			if (!(p->rtable > 0 && p->addr &&
			    p->addr->ss.ss_family != AF_INET))
				TAILQ_INSERT_TAIL(&conf->ntp_peers, p, entry);
			free($2->name);
			free($2);
		}
		;
*/

address		: STRING		{
			/*if (($$ = calloc(1, sizeof(struct ntp_addr_wrap))) ==
			    NULL)
				fatal(NULL);
			if (host($1, &$$->a) == -1) {
				yyerror("could not parse address spec \"%s\"",
				    $1);
				free($1);
				free($$);
				YYERROR;
			}
			$$->name = $1;*/
		}
		;

amqp_opts	:	{ opts_default(); }
		  amqp_opts_l
			{ $$ = opts; }
		|	{ opts_default(); $$ = opts; }
		;
amqp_opts_l	: amqp_opts_l amqp_opt
		| amqp_opt
		;
amqp_opt	: port
		| vhost
		| user
		| password
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

graphite_opts	:	{ opts_default(); }
		  graphite_opts_l
			{ $$ = opts; }
		|	{ opts_default(); $$ = opts; }
		;
graphite_opts_l	: graphite_opts_l graphite_opt
		| graphite_opt
		;
graphite_opt	: port
		;

queue_opts	:	{ opts_default(); }
		  queue_opts_l
			{ $$ = opts; }
		|	{ opts_default(); $$ = opts; }
		;
queue_opts_l	: queue_opts_l queue_opt
		| queue_opt
		;
queue_opt	: mirrored
		| ttl
		;

port		: PORT NUMBER {
			if ($2 < 1 || $2 > USHRT_MAX) {
				yyerror("invalid port number");
				YYERROR;
			}
			opts.port = $2;
		}
		;

vhost		: VHOST STRING {
			opts.vhost = $2;
		}
		;

user		: USER STRING {
			opts.user = $2;
		}
		;

password	: PASSWORD STRING {
			opts.password = $2;
		}
		;

type		: TYPE STRING {
			if (strcmp($2, AMQP_EXCHANGE_TYPE_TOPIC) &&
			    strcmp($2, AMQP_EXCHANGE_TYPE_DIRECT) &&
			    strcmp($2, AMQP_EXCHANGE_TYPE_FANOUT)) {
				yyerror("invalid exchange type");
				free($2);
				YYERROR;
			}
			opts.type = $2;
		}
		;

federated	: FEDERATED STRING {
			opts.upstreams = $2;
		}
		;

mirrored	: MIRRORED {
			opts.flags |= DEQUEUE_FLAG_MIRRORED_QUEUE;
		}
		;

ttl		: TTL NUMBER {
			if ($2 < 1 || $2 > LLONG_MAX) {
				yyerror("invalid ttl");
				YYERROR;
			}
			opts.ttl = $2;
		}
		;

%%

void
opts_default(void)
{
	bzero(&opts, sizeof opts);
}

struct binding *
init_binding(void)
{
	if ((b = calloc(1, sizeof(struct binding))) == NULL)
		return (NULL);

	return (b);
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
		{ "binding",		BINDING},
		{ "exchange",		EXCHANGE},
		{ "federated",		FEDERATED},
		{ "graphite",		GRAPHITE},
		{ "metric-source",	METRICSOURCE},
		{ "mirrored",		MIRRORED},
		{ "password",		PASSWORD},
		{ "port",		PORT},
		{ "queue",		QUEUE},
		{ "ttl",		TTL},
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

struct dequeue *
parse_config(const char *filename, int flags)
{
	int		 errors = 0;

	if ((conf = calloc(1, sizeof(*conf))) == NULL) {
		log_warn("cannot allocate memory");
		return (NULL);
	}

	LIST_INIT(&conf->bindings);

	if ((file = pushfile(filename)) == NULL) {
		free(conf);
		return (NULL);
	}
	topfile = file;

	yyparse();
	errors = file->errors;
	popfile();

	if (errors) {
		free(conf);
		return (NULL);
	}

	/* Fill in the gaps with well-defined defaults
	 */

	/* Graphite */
	//if (conf->graphite_host)
	if (conf->graphite_port == 0)
		conf->graphite_port = GRAPHITE_DEFAULT_PORT;

	/* AMQP host/login */
	//if (conf->amqp_host)
	if (conf->amqp_port == 0)
		conf->amqp_port = AMQP_DEFAULT_PORT;
	if (conf->vhost == NULL)
		if ((conf->vhost = strdup(AMQP_DEFAULT_VHOST)) == NULL)
			fatal("strdup");
	if (conf->user == NULL)
		if ((conf->user = strdup(AMQP_DEFAULT_USER)) == NULL)
			fatal("strdup");
	if (conf->password == NULL)
		if ((conf->password = strdup(AMQP_DEFAULT_PASSWORD)) == NULL)
			fatal("strdup");

	/* AMQP exchange */
	if (conf->exchange == NULL)
		if ((conf->exchange = strdup(AMQP_DEFAULT_EXCHANGE)) == NULL)
			fatal("strdup");
	if (conf->type == NULL)
		if ((conf->type = strdup(AMQP_DEFAULT_EXCHANGE_TYPE)) == NULL)
			fatal("strdup");

	/* AMQP queue */
	if (conf->queue == NULL)
		if ((conf->queue = strdup(AMQP_DEFAULT_QUEUE)) == NULL)
			fatal("strdup");

	/* AMQP exchange to queue bindings (ignore fanout exchanges) */
	if (LIST_EMPTY(&conf->bindings) &&
	    strcmp(conf->type, AMQP_EXCHANGE_TYPE_FANOUT)) {
		if ((b = init_binding()) == NULL)
			fatal(NULL);
		if ((b->key = strdup(
		    (strcmp(conf->type, AMQP_EXCHANGE_TYPE_FANOUT) == 0) ?
		    AMQP_DEFAULT_BINDING_FANOUT :
		    AMQP_DEFAULT_BINDING_DIRECT)) == NULL)
			fatal("strdup");
		LIST_INSERT_HEAD(&conf->bindings, b, entry);
	}

	/* Only topic exchanges can handle the metric key as the routing key */
	if (strcmp(conf->type, AMQP_EXCHANGE_TYPE_TOPIC) &&
	    !(conf->flags & DEQUEUE_FLAG_METRIC_IN_MESSAGE))
		fatalx("metric key must be in message with non-topic exchange");

	return (conf);
}
