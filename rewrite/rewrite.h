/*
 * Copyright (c) 2013 Matt Dainty <matt@bodgit-n-scarper.com>
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

#ifndef _REWRITE_H
#define _REWRITE_H

#include <pcre.h>

#include "common.h"
#include "stomp.h"
#include "graphite.h"

#define	REWRITE_CONF_FILE		"/etc/graphite-rewrite.conf"
#define	REWRITE_USER			"_rewrite"

struct stomp_sub {
	struct rewrite			*env;
	char				*path;
	int				 ack;
	struct event			*ack_ev;
	struct timeval			 ack_tv;
	char				*ack_pending;
	struct stomp_subscription	*subscription;
	
	TAILQ_ENTRY(stomp_sub)		 entry;
};

struct rewrite_rule {
	TAILQ_ENTRY(rewrite_rule)	 entry;
	char				*pattern;
	char				*replacement;
	pcre				*re;
	pcre_extra			*sd;
};

struct rewrite {
	struct event_base			*base;

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

	TAILQ_HEAD(rewrite_rules, rewrite_rule)	 rewrite_rules;

	char					*stomp_send;

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
struct rewrite	*parse_config(const char *, int);

#endif /* _REWRITE_H */
