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

#define	DEQUEUE_CONF_FILE	"/etc/graphite-dequeue.conf"
#define	DEQUEUE_USER		"_dequeue"

struct dequeue_addr {
	struct dequeue_addr	*next;
	struct sockaddr_storage	 ss;
};

struct dequeue_addr_wrap {
	char			*name;
	struct dequeue_addr	*a;
};

struct graphite {
	char			*host;
	int			 port;
	int			 fd;
	struct sockaddr_storage	 ss;
};

struct dequeue {
	struct graphite	*graphite;
	struct amqp	*amqp;
};

/* prototypes */
/* parse.y */
struct dequeue	*parse_config(const char *, int);
int		 host(const char *, struct dequeue_addr **);
int		 host_dns(const char *, struct dequeue_addr **);

#endif
