/*
 * Copyright (c) 2012 Matt Dainty <matt@bodgit-n-scarper.com>
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

#ifndef _COMMON_H
#define _COMMON_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>

#include <stdio.h>
#include <netdb.h>

#include <amqp.h>

#include "config.h"

#define	GRAPHITE_DEFAULT_HOST		"localhost"
#define	GRAPHITE_DEFAULT_PORT		2003

#define	AMQP_DEFAULT_HOST		"localhost"
#define	AMQP_DEFAULT_PORT		5672
#define	AMQP_DEFAULT_VHOST		"/"
#define	AMQP_DEFAULT_USER		"guest"
#define	AMQP_DEFAULT_PASSWORD		"guest"
#define	AMQP_DEFAULT_CHANNEL		1
#define	AMQP_DEFAULT_EXCHANGE		"graphite"
#define	AMQP_DEFAULT_EXCHANGE_TYPE	"topic"
#define	AMQP_DEFAULT_QUEUE		"graphite"
#define	AMQP_DEFAULT_BINDING_TOPIC	"#"
#define	AMQP_DEFAULT_BINDING_DIRECT	"graphite"
#define	AMQP_DEFAULT_TIMEOUT		500

#define	AMQP_FLAG_METRIC_IN_MESSAGE	(1 << 0)
#define	AMQP_FLAG_MIRRORED_QUEUE	(1 << 1)

#define	AMQP_EXCHANGE_TYPE_DIRECT	"direct"
#define	AMQP_EXCHANGE_TYPE_TOPIC	"topic"
#define	AMQP_EXCHANGE_TYPE_FANOUT	"fanout"
#define	AMQP_EXCHANGE_TYPE_HEADERS	"headers"

#ifndef SA_LEN
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
#define	SA_LEN(x)	((x)->sa_len)
#else
#define	SA_LEN(x)	((x)->sa_family == AF_INET  ? sizeof(struct sockaddr_in) : \
			 (x)->sa_family == AF_INET6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr))
#endif
#endif

#ifndef __dead
#define	__dead
#endif

struct binding {
	char			*key;
	LIST_ENTRY(binding)	 entry;
};

struct amqp {
	char				*host;
	int				 port;
	char				*vhost;
	char				*user;
	char				*password;
	char				*exchange;
	char				*type;
	char				*key;
	char				*upstreams;
	char				*queue;
	long long			 ttl;
	long long			 bytes;
	long long			 timeout;

	int				 flags;

	LIST_HEAD(bindings, binding)	 bindings;

	amqp_connection_state_t		 c;
};

/* prototypes */
/* amqp.c */
struct amqp	*amqp_init(void);
int		 amqp_open(struct amqp *);
int		 amqp_exchange(struct amqp *);
int		 amqp_queue(struct amqp *);
int		 amqp_consume(struct amqp *, char **, char **, size_t *);
void		 amqp_acknowledge(struct amqp *, int);
void		 amqp_reject(struct amqp *, int, int);
void		 amqp_close(struct amqp *);

/* graphite.c */
int		 graphite_parse(char *, char *);

/* log.c */
void		 log_init(int);
void		 vlog(int, const char *, va_list);
void		 log_warn(const char *, ...);
void		 log_warnx(const char *, ...);
void		 log_info(const char *, ...);
void		 log_debug(const char *, ...);
void		 fatal(const char *);
void		 fatalx(const char *);
const char	*log_sockaddr(struct sockaddr *);

/* strtonum.c */
long long	 strtonum(const char *, long long, long long, const char **);

#endif
