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

#ifndef _ENQUEUE_H
#define _ENQUEUE_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>

#include "common.h"
#include "stomp.h"
#include "graphite.h"

#define	ENQUEUE_CONF_FILE	"/etc/graphite-enqueue.conf"
#define	ENQUEUE_USER		"_enqueue"

#define	STOMP_DEFAULT_TIMEOUT	500

#define	ENQUEUE_STOMP_CONNECTED	(1 << 0)

struct listen_addr {
	TAILQ_ENTRY(listen_addr)	 entry;
	struct sockaddr_storage		 sa;
	int				 port;
	struct evconnlistener		*listener;
};

struct enqueue_addr {
	struct enqueue_addr	*next;
	struct sockaddr_storage	 ss;
};

struct enqueue_addr_wrap {
	char			*name;
	struct enqueue_addr	*a;
};

struct enqueue {
	struct event_base			*base;

	int					 state;

	TAILQ_HEAD(listen_addrs, listen_addr)	 listen_addrs;

	char					*stomp_host;
	unsigned short				 stomp_port;
	char					*stomp_vhost;
	char					*stomp_user;
	char					*stomp_password;
	int					 stomp_version;
	long long				 stomp_heartbeat;
	struct timeval				 stomp_reconnect;
	int					 stomp_flags;

	char					*stomp_send;

	struct stomp_connection			*stomp_conn;

	unsigned long long			 stomp_bytes;
	long long				 stomp_timeout;

	char					*buffer;
	char					*destination;
	struct event				*ev;
	struct timeval				 t;

	char					*stats_host;
	unsigned short				 stats_port;
	struct timeval				 stats_reconnect;
	struct timeval				 stats_interval;
	char					*stats_prefix;

	struct graphite_connection		*stats_conn;
	struct event				*stats_ev;

	TAILQ_HEAD(clients, client)		 clients;

	/* Stats */
	unsigned long long			 bytes_rx;
	unsigned long long			 metrics_rx;
	unsigned long long			 connects;
	unsigned long long			 disconnects;
};

struct client {
	TAILQ_ENTRY(client)	 entry;
	struct bufferevent	*bev;
	struct enqueue		*env;

	unsigned long long	 bytes_rx;
	unsigned long long	 metrics_rx;
};

/* prototypes */
/* parse.y */
struct enqueue	*parse_config(const char *, int);
int		 host(const char *, struct enqueue_addr **);
int		 host_dns(const char *, struct enqueue_addr **);

#endif
