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
	int		 prefetch;
	int		 flags;
} opts;
void		 opts_default(void);

struct binding	*b;
struct binding	*init_binding(void);

/* FIXME */
#define YYSTYPE_IS_DECLARED 1
typedef struct {
	union {
		int64_t		 number;
		char		*string;
		struct opts	 opts;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	GRAPHITE
%token	AMQP VHOST USER PASSWORD
%token	EXCHANGE TYPE FEDERATED
%token	QUEUE MIRRORED TTL PREFETCH
%token	BINDING
%token	METRICLOCATION
%token	PORT
%token	ERROR
%token	<v.string>		STRING
%token	<v.number>		NUMBER
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
%type	<v.opts>		prefetch
%%

grammar		: /* empty */
		| grammar '\n'
		| grammar main '\n'
		| grammar error '\n'		{ file->errors++; }
		;

main		: GRAPHITE STRING graphite_opts	{
			if (conf->graphite->host)
				free(conf->graphite->host);
			conf->graphite->host = $2;
			conf->graphite->port =
			    (opts.port) ? opts.port : GRAPHITE_DEFAULT_PORT;
		}
		| AMQP STRING amqp_opts			{
			if (conf->amqp->host)
				free(conf->amqp->host);
			conf->amqp->host = $2;
			conf->amqp->port = opts.port;

			if (conf->amqp->vhost)
				free(conf->amqp->vhost);
			conf->amqp->vhost = (opts.vhost) ? opts.vhost : NULL;

			if (conf->amqp->user)
				free(conf->amqp->user);
			conf->amqp->user = (opts.user) ? opts.user : NULL;

			if (conf->amqp->password)
				free(conf->amqp->password);
			conf->amqp->password = (opts.password) ? opts.password : NULL;
		}
		| EXCHANGE STRING exchange_opts		{
			if (conf->amqp->exchange)
				free(conf->amqp->exchange);
			conf->amqp->exchange = $2;

			if (conf->amqp->type)
				free(conf->amqp->type);
			conf->amqp->type = (opts.type) ? opts.type : NULL;

			if (conf->amqp->upstreams)
				free(conf->amqp->upstreams);
			conf->amqp->upstreams = (opts.upstreams) ? opts.upstreams : NULL;
		}
		| QUEUE STRING queue_opts		{
			if (conf->amqp->queue)
				free(conf->amqp->queue);
			conf->amqp->queue = $2;
			conf->amqp->flags &= ~AMQP_FLAG_MIRRORED_QUEUE;
			conf->amqp->flags |= opts.flags;
			conf->amqp->ttl = opts.ttl;
			conf->amqp->prefetch = opts.prefetch;
		}
		| BINDING STRING			{
			if ((b = init_binding()) == NULL)
				fatal(NULL);
			b->key = $2;
			LIST_INSERT_HEAD(&conf->amqp->bindings, b, entry);
		}
		| METRICLOCATION STRING			{
			if (!strcmp($2, "message"))
				conf->amqp->flags |=
				    AMQP_FLAG_METRIC_IN_MESSAGE;
			else if (!strcmp($2, "key"))
				conf->amqp->flags &=
				    ~AMQP_FLAG_METRIC_IN_MESSAGE;
			else {
				yyerror("unknown metric location");
				free($2);
				YYERROR;
			}
			free($2);
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
		| prefetch
		| ttl
		;

port		: PORT NUMBER {
			if ($2 < 0 || $2 > USHRT_MAX) {
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
			opts.flags |= AMQP_FLAG_MIRRORED_QUEUE;
		}
		;

prefetch	: PREFETCH NUMBER {
			if ($2 < 0 || $2 > UINT_MAX) {
				yyerror("invalid prefetch");
				YYERROR;
			}
			opts.prefetch = $2;
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
		{ "metric-location",	METRICLOCATION},
		{ "mirrored",		MIRRORED},
		{ "password",		PASSWORD},
		{ "port",		PORT},
		{ "prefetch",		PREFETCH},
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

	if ((conf->amqp = amqp_init()) == NULL) {
		log_warn("cannot allocate memory");
		return (NULL);
	}

	if ((conf->graphite = calloc(1, sizeof(struct graphite))) == NULL) {
		log_warn("cannot allocate memory");
		return (NULL);
	}

	//TAILQ_INIT(&conf->graphite_addrs);

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
	if (conf->graphite->host == NULL)
		if ((conf->graphite->host = strdup(GRAPHITE_DEFAULT_HOST)) == NULL)
			fatal("strdup");
	if (conf->graphite->port == 0)
		conf->graphite->port = GRAPHITE_DEFAULT_PORT;

	/* AMQP host/login */
	if (conf->amqp->host == NULL)
		if ((conf->amqp->host = strdup(AMQP_DEFAULT_HOST)) == NULL)
			fatal("strdup");
	if (conf->amqp->port == 0)
		conf->amqp->port = AMQP_DEFAULT_PORT;
	if (conf->amqp->vhost == NULL)
		if ((conf->amqp->vhost = strdup(AMQP_DEFAULT_VHOST)) == NULL)
			fatal("strdup");
	if (conf->amqp->user == NULL)
		if ((conf->amqp->user = strdup(AMQP_DEFAULT_USER)) == NULL)
			fatal("strdup");
	if (conf->amqp->password == NULL)
		if ((conf->amqp->password = strdup(AMQP_DEFAULT_PASSWORD)) == NULL)
			fatal("strdup");

	/* AMQP exchange */
	if (conf->amqp->exchange == NULL)
		if ((conf->amqp->exchange = strdup(AMQP_DEFAULT_EXCHANGE)) == NULL)
			fatal("strdup");
	if (conf->amqp->type == NULL)
		if ((conf->amqp->type = strdup(AMQP_DEFAULT_EXCHANGE_TYPE)) == NULL)
			fatal("strdup");

	/* AMQP queue */
	if (conf->amqp->queue == NULL)
		if ((conf->amqp->queue = strdup(AMQP_DEFAULT_QUEUE)) == NULL)
			fatal("strdup");

	/* AMQP exchange to queue bindings (ignore fanout exchanges) */
	if (LIST_EMPTY(&conf->amqp->bindings) &&
	    strcmp(conf->amqp->type, AMQP_EXCHANGE_TYPE_FANOUT)) {
		if ((b = init_binding()) == NULL)
			fatal(NULL);
		if ((b->key = strdup(
		    (strcmp(conf->amqp->type, AMQP_EXCHANGE_TYPE_TOPIC) == 0) ?
		    AMQP_DEFAULT_BINDING_TOPIC :
		    AMQP_DEFAULT_BINDING_DIRECT)) == NULL)
			fatal("strdup");
		LIST_INSERT_HEAD(&conf->amqp->bindings, b, entry);
	}

	/* Only topic exchanges can handle the metric key as the routing key */
	if (strcmp(conf->amqp->type, AMQP_EXCHANGE_TYPE_TOPIC) &&
	    !(conf->amqp->flags & AMQP_FLAG_METRIC_IN_MESSAGE))
		fatalx("metric key must be in message with non-topic exchange");

	return (conf);
}

struct dequeue_addr	*host_v4(const char *);
struct dequeue_addr	*host_v6(const char *);

int
host(const char *s, struct dequeue_addr **hn)
{
	struct dequeue_addr *h = NULL;

	if (!strcmp(s, "*"))
		if ((h = calloc(1, sizeof(struct dequeue_addr))) == NULL)
			fatal(NULL);

	/* IPv4 address? */
	if (h == NULL)
		h = host_v4(s);

	/* IPv6 address? */
	if (h == NULL)
		h = host_v6(s);

	if (h == NULL)
		return (0);

	*hn = h;

	return (1);
}

struct dequeue_addr *
host_v4(const char *s)
{
	struct in_addr		 ina;
	struct sockaddr_in	*sa_in;
	struct dequeue_addr	*h;

	bzero(&ina, sizeof(struct in_addr));
	if (inet_pton(AF_INET, s, &ina) != 1)
		return (NULL);

	if ((h = calloc(1, sizeof(struct dequeue_addr))) == NULL)
		fatal(NULL);
	sa_in = (struct sockaddr_in *)&h->ss;
#ifdef HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
	sa_in->sin_len = sizeof(struct sockaddr_in);
#endif
	sa_in->sin_family = AF_INET;
	sa_in->sin_addr.s_addr = ina.s_addr;

	return (h);
}

struct dequeue_addr *
host_v6(const char *s)
{
	struct addrinfo		 hints, *res;
	struct sockaddr_in6	*sa_in6;
	struct dequeue_addr	*h = NULL;

	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;	/* DUMMY */
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(s, "0", &hints, &res) == 0) {
		if ((h = calloc(1, sizeof(struct dequeue_addr))) == NULL)
			fatal(NULL);
		sa_in6 = (struct sockaddr_in6 *)&h->ss;
#ifdef HAVE_STRUCT_SOCKADDR_IN6_SIN6_LEN
		sa_in6->sin6_len = sizeof(struct sockaddr_in6);
#endif
		sa_in6->sin6_family = AF_INET6;
		memcpy(&sa_in6->sin6_addr,
		    &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr,
		    sizeof(sa_in6->sin6_addr));
		sa_in6->sin6_scope_id =
		    ((struct sockaddr_in6 *)res->ai_addr)->sin6_scope_id;

		freeaddrinfo(res);
	}

	return (h);
}

#define	MAX_SERVERS_DNS	8

int
host_dns(const char *s, struct dequeue_addr **hn)
{
	struct addrinfo		 hints, *res0, *res;
	int			 error, cnt = 0;
	struct sockaddr_in	*sa_in;
	struct sockaddr_in6	*sa_in6;
	struct dequeue_addr	*h, *hh = NULL;

	bzero(&hints, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;	/* DUMMY */
	error = getaddrinfo(s, NULL, &hints, &res0);
	if (error == EAI_AGAIN || error == EAI_NODATA || error == EAI_NONAME)
		return (0);
	if (error) {
		log_warnx("could not parse \"%s\": %s", s,
		    gai_strerror(error));
		return (-1);
	}

	for (res = res0; res && cnt < MAX_SERVERS_DNS; res = res->ai_next) {
		if (res->ai_family != AF_INET &&
		    res->ai_family != AF_INET6)
			continue;
		if ((h = calloc(1, sizeof(struct dequeue_addr))) == NULL)
			fatal(NULL);
		h->ss.ss_family = res->ai_family;
		if (res->ai_family == AF_INET) {
			sa_in = (struct sockaddr_in *)&h->ss;
#ifdef HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
			sa_in->sin_len = sizeof(struct sockaddr_in);
#endif
			sa_in->sin_addr.s_addr = ((struct sockaddr_in *)
			    res->ai_addr)->sin_addr.s_addr;
		} else {
			sa_in6 = (struct sockaddr_in6 *)&h->ss;
#ifdef HAVE_STRUCT_SOCKADDR_IN6_SIN6_LEN
			sa_in6->sin6_len = sizeof(struct sockaddr_in6);
#endif
			memcpy(&sa_in6->sin6_addr, &((struct sockaddr_in6 *)
			    res->ai_addr)->sin6_addr, sizeof(struct in6_addr));
		}

		h->next = hh;
		hh = h;
		cnt++;
	}
	freeaddrinfo(res0);

	*hn = hh;
	return (cnt);
}
