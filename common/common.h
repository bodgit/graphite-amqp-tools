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
#include <sys/time.h>

#include <stdio.h>
#include <netdb.h>

#include "config.h"

#define	GRAPHITE_DEFAULT_HOST		"localhost"

#define	STOMP_DEFAULT_HOST		"localhost"

#define	STOMP_FLAG_SSL			(1 << 0)

#define	STOMP_VERSION_1_0_S		"1.0"
#define	STOMP_VERSION_1_1_S		"1.1"
#define	STOMP_VERSION_1_2_S		"1.2"

#define	STOMP_DEFAULT_SUBSCRIPTION	"/queue/graphite"

#define	STOMP_ACK_AUTO_S		"auto"
#define	STOMP_ACK_CLIENT_S		"client"
#define	STOMP_ACK_CLIENT_INDIVIDUAL_S	"individual"

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
const char	*log_sockaddr(struct sockaddr *);

/* strtonum.c */
long long	 strtonum(const char *, long long, long long, const char **);

#endif
