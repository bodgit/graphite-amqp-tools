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

#ifndef _DEQUEUE_H
#define _DEQUEUE_H

#include <stdio.h>
#include <string.h>

#include "common.h"
#include "stomp.h"
#include "graphite.h"

#define	DEQUEUE_CONF_FILE		"/etc/graphite-dequeue.conf"
#define	DEQUEUE_USER			"_dequeue"

#define	DEQUEUE_GRAPHITE_CONNECTED	(1 << 0)
#define	DEQUEUE_STOMP_CONNECTED		(1 << 1)

struct stomp_sub {
	struct dequeue			*env;
	char				*path;
	int				 ack;
	struct event			*ack_ev;
	struct timeval			 ack_tv;
	char				*ack_pending;
	struct stomp_subscription	*subscription;
	
	TAILQ_ENTRY(stomp_sub)		 entry;
};

struct dequeue {
	struct event_base			*base;

	int					 state;

	char					*graphite_host;
	unsigned short				 graphite_port;
	struct timeval				 graphite_reconnect;

	struct graphite_connection		*graphite_conn;

	char					*stomp_host;
	unsigned short				 stomp_port;
	char					*stomp_vhost;
	char					*stomp_user;
	char					*stomp_password;
	int					 stomp_version;
	long long				 stomp_heartbeat;
	struct timeval				 stomp_reconnect;
	int					 stomp_flags;

	TAILQ_HEAD(stomp_subs, stomp_sub)	 stomp_subs;

	struct stomp_connection			*stomp_conn;

	char					*stats_host;
	unsigned short				 stats_port;
	struct timeval				 stats_reconnect;
	struct timeval				 stats_interval;
	char					*stats_prefix;

	struct graphite_connection		*stats_conn;
	struct event				*stats_ev;
};

/* prototypes */
/* parse.y */
struct dequeue	*parse_config(const char *, int);

#endif
