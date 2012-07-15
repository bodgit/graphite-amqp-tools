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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <pwd.h>
#include <signal.h>

#include "enqueue.h"

#define	MAX_LINE	4096

__dead void	 usage(void);
void		 amqp_flush(int, short, void *);
void		 graphite_read(struct bufferevent *, void *);
void		 graphite_error(struct bufferevent *, short, void *);
void		 graphite_accept(int, short, void *);

__dead void
usage(void)
{
	extern char	*__progname;

	fprintf(stderr, "usage: %s [-dnv] [-f file]\n", __progname);
	exit(1);
}

void
amqp_flush(int fd, short event, void *arg)
{
	struct enqueue	*env = (struct enqueue *)arg;

	log_debug("timeout fired");

	/* Send what's in the buffer and zero it */
	if (strlen(env->buffer) > 0)
		amqp_publish(env->amqp, env->amqp->key, env->buffer);
	*env->buffer = '\0';
}

void
graphite_read(struct bufferevent *bev, void *arg)
{
	struct enqueue		*env = (struct enqueue *)arg;
	struct evbuffer		*input;
	struct evbuffer		*output;
	char			*line, *data;
	size_t			 n;
	int			 len;

	input = bufferevent_get_input(bev);
	output = bufferevent_get_output(bev);

	while ((line = evbuffer_readln(input, &n, EVBUFFER_EOL_CRLF))) {
		//log_debug("Got line: \"%s\"", line);
		if ((len = graphite_parse(NULL, line)) < 0)
			goto loop;

		/* The metric is used as the routing key so using the length
		 * of the key, overwrite the first space with a null and set
		 * the pointer to point to the value and timestamp following
		 * it
		 */
		if (!(env->amqp->flags & AMQP_FLAG_METRIC_IN_MESSAGE)) {
			data = line + len;
			*data = '\0';
			data++;

			/* Send immediately, we can't batch these */
			amqp_publish(env->amqp, line, data);
			goto loop;
		}

		/* The metric is is in the message, no batching */
		if (env->amqp->bytes == 0) {
			amqp_publish(env->amqp, env->amqp->key, line);
			goto loop;
		}

		/* Deactivate the timeout */
		if (evtimer_pending(env->ev, NULL))
			evtimer_del(env->ev);

		/* Existing data in the buffer? */
		if (strlen(env->buffer) > 0) {
			if ((strlen(env->buffer) + 1 + strlen(line)) > env->amqp->bytes) {
				/* Send what we have */
				amqp_publish(env->amqp, env->amqp->key,
				    env->buffer);
				*env->buffer = '\0';
			} else {
				/* Append a newline and the latest metric */
				strcat(env->buffer, "\n");
				strcat(env->buffer, line);

				/* Set the timeout timer */
				evtimer_add(env->ev, &env->t);

				goto loop;
			}
		}

		if (strlen(line) > env->amqp->bytes) {
			/* Numpty has set the batch size too low */
			log_warnx("batch size is set too low");
			amqp_publish(env->amqp, env->amqp->key, line);
		} else {
			/* Set the buffer to this new metric */
			strcpy(env->buffer, line);

			/* Set the timeout timer */
			evtimer_add(env->ev, &env->t);
		}

loop:
		free(line);
	}

	/* Big line of gibberish, just drain it away */
	if (evbuffer_get_length(input) >= MAX_LINE)
		while (evbuffer_get_length(input))
			if (evbuffer_drain(input, MAX_LINE) != 0)
				log_warnx("evbuffer_drain");
}

void
graphite_error(struct bufferevent *bev, short error, void *arg)
{
	if (error & BEV_EVENT_EOF) {
	} else if (error & BEV_EVENT_ERROR) {
	} else if (error & BEV_EVENT_TIMEOUT) {
	}
	bufferevent_free(bev);
}

void
graphite_accept(int fd, short event, void *arg)
{
	struct enqueue		*env = (struct enqueue *)arg;
	int			 nfd;
	struct sockaddr_storage	 ss;
	socklen_t		 slen = sizeof(ss);
	struct bufferevent	*bev;

	if ((nfd = accept(fd, (struct sockaddr *)&ss, &slen)) < 0)
		fatal("accept");
	if (nfd > FD_SETSIZE) {
		close(nfd);
	} else {
		evutil_make_socket_nonblocking(nfd);
		bev = bufferevent_socket_new(env->base, nfd,
		    BEV_OPT_CLOSE_ON_FREE);
		bufferevent_setcb(bev, graphite_read, NULL, graphite_error,
		    (void *)env);
		bufferevent_setwatermark(bev, EV_READ, 0, MAX_LINE);
		bufferevent_enable(bev, EV_READ|EV_WRITE);
	}
}

int
main(int argc, char *argv[])
{
	int			 c;
	int			 debug = 0;
	int			 noaction = 0;
	const char		*conffile = ENQUEUE_CONF_FILE;
	//u_int			 flags = 0;
	struct passwd		*pw;
	struct enqueue		*env;
	struct listen_addr	*la, *nla;
	evutil_socket_t		 fd;

	log_init(1);	/* log to stderr until daemonized */

	while ((c = getopt(argc, argv, "df:nv")) != -1) {
		switch (c) {
		case 'd':
			debug = 1;
			break;
		case 'f':
			conffile = optarg;
			break;
		case 'n':
			noaction++;
			break;
		case 'v':
			//flags |= ENQUEUE_F_VERBOSE;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;
	if (argc > 0)
		usage();

	if ((env = parse_config(conffile, 0)) == NULL)
		exit(1);

	if (noaction) {
		fprintf(stderr, "configuration ok\n");
		exit(0);
	}

#if 0
	if (geteuid())
		errx(1, "need root privileges");

	if ((pw = getpwnam(ENQUEUE_USER)) == NULL)
		errx(1, "unknown user %s", ENQUEUE_USER);
#endif

	log_init(debug);

	if (!debug) {
		if (daemon(1, 0) == -1)
			err(1, "failed to daemonize");
	}

	env->base = event_base_new();
	if (!env->base)
		fatalx("event_base_new");

	for (la = TAILQ_FIRST(&env->listen_addrs); la; ) {
		switch (la->sa.ss_family) {
		case AF_INET:
			((struct sockaddr_in *)&la->sa)->sin_port =
			    htons(la->port);
			break;
		case AF_INET6:
			((struct sockaddr_in6 *)&la->sa)->sin6_port =
			    htons(la->port);
			break;
		default:
			fatalx("invalid address family");
		}

		log_info("listening on %s:%d",
		    log_sockaddr((struct sockaddr *)&la->sa), la->port);

		if ((fd = socket(la->sa.ss_family, SOCK_STREAM, 0)) == -1)
			fatal("socket");
		evutil_make_socket_nonblocking(fd);

		{
			int one = 1;
			setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
		}

		if (bind(fd, (struct sockaddr *)&la->sa,
		    SA_LEN((struct sockaddr *)&la->sa)) == -1) {
			log_warn("bind on %s failed, skipping",
			    log_sockaddr((struct sockaddr *)&la->sa));
			close(la->fd);
			nla = TAILQ_NEXT(la, entry);
			TAILQ_REMOVE(&env->listen_addrs, la, entry);
			free(la);
			la = nla;
			continue;
		}

		if (listen(fd, 16) == -1)
			fatal("listen");

		la->ev = event_new(env->base, fd, EV_READ|EV_PERSIST,
		    graphite_accept, (void *)env);
		event_add(la->ev, NULL);

		la = TAILQ_NEXT(la, entry);
	}

	if ((env->amqp->flags & AMQP_FLAG_METRIC_IN_MESSAGE) &&
	    (env->amqp->bytes > 0)) {
		/* Allocate buffer to batch up metrics */
		if ((env->buffer = calloc(1, env->amqp->bytes + 1)) == NULL)
			fatal("calloc");

		/* Create timer event for flushing */
		env->ev = evtimer_new(env->base, amqp_flush, (void *)env);
		env->t.tv_sec = env->amqp->timeout / 1000;
		env->t.tv_usec = (env->amqp->timeout % 1000) * 1000;
	}

	log_info("startup");

#if 0
	if (chroot(pw->pw_dir) == -1)
		fatal("chroot");
	if (chdir("/") == -1)
		fatal("chdir(\"/\")");

	if (setgroups(1, &pw->pw_gid) ||
#if 0
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
#else
	    setgid(pw->pw_gid) || setegid(pw->pw_gid) ||
	    setuid(pw->pw_uid) || seteuid(pw->pw_uid))
#endif
		fatal("cannot drop privileges");
#endif

	/* XXX */

	if (amqp_open(env->amqp) != 0)
		fatalx("amqp_open");
	if (amqp_exchange(env->amqp) != 0)
		fatalx("amqp_exchange");

	signal(SIGPIPE, SIG_IGN);

	event_base_dispatch(env->base);

	amqp_close(env->amqp);

	return (0);
}
