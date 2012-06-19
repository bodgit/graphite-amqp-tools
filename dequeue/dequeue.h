/*
 */

#ifndef _GRAPHITE_DEQUEUE_H
#define _GRAPHITE_DEQUEUE_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>

#include <amqp.h>
#include <amqp_framing.h>

#include <stdio.h>
#include <string.h>
#include <netdb.h>

#define	GRAPHITE_DEFAULT_HOST		"localhost"
#define	GRAPHITE_DEFAULT_PORT		2003

//#define	AMQP_DEFAULT_HOST		"localhost"
#define	AMQP_DEFAULT_HOST		"192.168.23.120"
#define	AMQP_DEFAULT_PORT		5672
#define	AMQP_DEFAULT_VHOST		"/"
#define	AMQP_DEFAULT_USER		"guest"
#define	AMQP_DEFAULT_PASSWORD		"guest"
#define	AMQP_DEFAULT_CHANNEL		1
#define	AMQP_DEFAULT_EXCHANGE		"graphite"
#define	AMQP_DEFAULT_EXCHANGE_TYPE	"topic"
#define	AMQP_DEFAULT_QUEUE		"graphite"
#define	AMQP_DEFAULT_BINDING_FANOUT	"#"
#define	AMQP_DEFAULT_BINDING_DIRECT	"graphite"

#define	AMQP_EXCHANGE_TYPE_DIRECT	"direct"
#define	AMQP_EXCHANGE_TYPE_TOPIC	"topic"
#define	AMQP_EXCHANGE_TYPE_FANOUT	"fanout"
#define	AMQP_EXCHANGE_TYPE_HEADERS	"headers"

struct binding {
	char			*key;
	LIST_ENTRY(binding)	 entry;
};

struct dequeue {
	int				 flags;
#define	DEQUEUE_FLAG_METRIC_IN_MESSAGE	(1 << 0)
#define	DEQUEUE_FLAG_MIRRORED_QUEUE	(1 << 1)

	char				*graphite_host;
	int				 graphite_port;

	char				*amqp_host;
	int				 amqp_port;
	char				*vhost;
	char				*user;
	char				*password;
	char				*exchange;
	char				*type;
	char				*upstreams;
	char				*queue;
	long long			 ttl;

	LIST_HEAD(bindings, binding)	 bindings;

	amqp_connection_state_t		 c;
};

/* prototypes */
/* log.c */
void		 log_init(int);
void		 vlog(int, const char *, va_list);
void		 log_warn(const char *, ...);
void		 log_warnx(const char *, ...);
void		 log_info(const char *, ...);
void		 log_debug(const char *, ...);
void		 fatal(const char *);
void		 fatalx(const char *);
const char *	 log_sockaddr(struct sockaddr *);

/* parse.y */
struct dequeue	*parse_config(const char *, int);

/* strtonum.c */
long long	 strtonum(const char *, long long, long long, const char **);

#endif
