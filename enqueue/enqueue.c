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
void		 handle_signal(int, short, void *);
void		 stomp_flush(int, short, void *);
void		 graphite_server_read(struct bufferevent *, void *);
void		 graphite_server_error(struct bufferevent *, short, void *);
void		 graphite_server_count_rx(struct evbuffer *,
		    const struct evbuffer_cb_info *, void *);
void		 graphite_server_accept(struct evconnlistener *,
		    evutil_socket_t, struct sockaddr *, int, void *);
void		 stats_connect_cb(struct graphite_connection *, void *);
void		 stats_timer_cb(int, short, void *);
void		 stats_disconnect_cb(struct graphite_connection *, void *);
void		 stomp_connect_cb(struct stomp_connection *,
		    struct stomp_frame *, void *);
void		 stomp_disconnect_cb(struct stomp_connection *, void *);

__dead void
usage(void)
{
	extern char	*__progname;

	fprintf(stderr, "usage: %s [-dnv] [-f file]\n", __progname);
	exit(1);
}

void
handle_signal(int sig, short event, void *arg)
{
	//struct enqueue	*env = (struct enqueue *)arg;

	log_info("exiting on signal %d", sig);

	exit(0);
}

void
stomp_flush(int fd, short event, void *arg)
{
	struct enqueue	*env = (struct enqueue *)arg;

	log_debug("timeout fired");

	/* Send what's in the buffer and zero it */
	if (strlen(env->buffer) > 0)
		stomp_send(env->stomp_conn, env->stomp_send, env->buffer, NULL);
	*env->buffer = '\0';
}

void
graphite_server_read(struct bufferevent *bev, void *arg)
{
	struct client	*client = (struct client *)arg;
	struct enqueue	*env = client->env;
	struct evbuffer	*input;
	struct evbuffer	*output;
	char		*line;
	size_t		 n;
	int		 len;

	input = bufferevent_get_input(bev);
	output = bufferevent_get_output(bev);

	while ((line = evbuffer_readln(input, &n, EVBUFFER_EOL_CRLF))) {
		//log_debug("Got line: \"%s\"", line);
		if ((len = graphite_parse(NULL, (unsigned char *)line,
		    NULL)) < 0)
			goto loop;

		client->metrics_rx++;
		env->metrics_rx++;

		/* FIXME Not connected to STOMP, drop the message */
		if (!(env->state & ENQUEUE_STOMP_CONNECTED))
			goto loop;

		/* The metric is is in the message, no batching */
		if (env->stomp_bytes == 0) {
			stomp_send(env->stomp_conn, env->stomp_send, line,
			    NULL);
			goto loop;
		}

		/* Deactivate the timeout */
		if (evtimer_pending(env->ev, NULL))
			evtimer_del(env->ev);

		/* Existing data in the buffer? */
		if (strlen(env->buffer) > 0) {
			if ((strlen(env->buffer) + 1 + strlen(line)) > env->stomp_bytes) {
				/* Send what we have */
				stomp_send(env->stomp_conn, env->stomp_send,
				    env->buffer, NULL);
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

		if (strlen(line) > env->stomp_bytes) {
			/* Numpty has set the batch size too low */
			log_warnx("batch size is set too low");
			stomp_send(env->stomp_conn, env->stomp_send, line,
			    NULL);
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
graphite_server_error(struct bufferevent *bev, short error, void *arg)
{
	struct client	*client = (struct client *)arg;
	struct enqueue	*env = client->env;

	if (error & (BEV_EVENT_ERROR|BEV_EVENT_EOF)) {
		env->disconnects++;
		bufferevent_free(bev);
		TAILQ_REMOVE(&env->clients, client, entry);
		free(client);
	}
}

void
graphite_server_count_rx(struct evbuffer *buffer,
    const struct evbuffer_cb_info *info, void *arg)
{
	struct client	*client = (struct client *)arg;

	client->bytes_rx += info->n_added;
	client->env->bytes_rx += info->n_added;
}

void
graphite_server_accept(struct evconnlistener *listener, evutil_socket_t fd,
    struct sockaddr *address, int socklen, void *arg)
{
	struct enqueue		*env = (struct enqueue *)arg;
	struct event_base	*base = evconnlistener_get_base(listener);
	struct client		*client;

	log_debug("Connection from %s", log_sockaddr(address));

	env->connects++;

	if ((client = calloc(1, sizeof(struct client))) != NULL) {
		client->env = env;
		client->bev = bufferevent_socket_new(base, fd,
		    BEV_OPT_CLOSE_ON_FREE);
		bufferevent_setcb(client->bev, graphite_server_read, NULL,
		    graphite_server_error, (void *)client);
		bufferevent_enable(client->bev, EV_READ|EV_WRITE);

		evbuffer_add_cb(bufferevent_get_input(client->bev),
		    graphite_server_count_rx, (void *)client);

		TAILQ_INSERT_TAIL(&env->clients, client, entry);
	}
}

void
stats_connect_cb(struct graphite_connection *c, void *arg)
{
	struct enqueue	*env = (struct enqueue *)arg;

	log_debug("Connected to %s:%hu", env->stats_host,
	    env->stats_port);

	evtimer_add(env->stats_ev, &env->stats_interval);
}

void
stats_timer_cb(int fd, short event, void *arg)
{
	struct enqueue		*env = (struct enqueue *)arg;
	struct timeval		 tv;
	struct client		*client;
	size_t			 input = 0, output = 0;

	gettimeofday(&tv, NULL);

	/* FIXME shouldn't really be poking inside structs */
	graphite_send_metric(env->stats_conn, env->stats_prefix,
	    "stomp.bytes.rx", tv, "%lld", env->stomp_conn->bytes_rx);
	graphite_send_metric(env->stats_conn, env->stats_prefix,
	    "stomp.bytes.tx", tv, "%lld", env->stomp_conn->bytes_tx);
	graphite_send_metric(env->stats_conn, env->stats_prefix,
	    "stomp.messages.rx", tv, "%lld", env->stomp_conn->messages_rx);
	graphite_send_metric(env->stats_conn, env->stats_prefix,
	    "stomp.messages.tx", tv, "%lld", env->stomp_conn->messages_tx);
	graphite_send_metric(env->stats_conn, env->stats_prefix,
	    "stomp.frames.rx", tv, "%lld", env->stomp_conn->frames_rx);
	graphite_send_metric(env->stats_conn, env->stats_prefix,
	    "stomp.frames.tx", tv, "%lld", env->stomp_conn->frames_tx);
	graphite_send_metric(env->stats_conn, env->stats_prefix,
	    "stomp.buffer.input", tv, "%zd",
	    (env->stomp_conn->bev == NULL) ? 0 :
	    evbuffer_get_length(bufferevent_get_input(env->stomp_conn->bev)));
	graphite_send_metric(env->stats_conn, env->stats_prefix,
	    "stomp.buffer.output", tv, "%zd",
	    (env->stomp_conn->bev == NULL) ? 0 :
	    evbuffer_get_length(bufferevent_get_output(env->stomp_conn->bev)));
	graphite_send_metric(env->stats_conn, env->stats_prefix,
	    "graphite.bytes.rx", tv, "%lld", env->bytes_rx);
	graphite_send_metric(env->stats_conn, env->stats_prefix,
	    "graphite.metrics.rx", tv, "%lld", env->metrics_rx);
	graphite_send_metric(env->stats_conn, env->stats_prefix,
	    "graphite.connects", tv, "%lld", env->connects);
	graphite_send_metric(env->stats_conn, env->stats_prefix,
	    "graphite.disconnects", tv, "%lld", env->disconnects);

	for (client = TAILQ_FIRST(&env->clients); client;
	    client = TAILQ_NEXT(client, entry)) {
		input += evbuffer_get_length(bufferevent_get_input(client->bev));
		output += evbuffer_get_length(bufferevent_get_output(client->bev));
	}

	graphite_send_metric(env->stats_conn, env->stats_prefix,
	    "graphite.buffer.input", tv, "%zd", input);
	graphite_send_metric(env->stats_conn, env->stats_prefix,
	    "graphite.buffer.output", tv, "%zd", output);
}

void
stats_disconnect_cb(struct graphite_connection *c, void *arg)
{
	struct enqueue	*env = (struct enqueue *)arg;

	log_debug("Disconnected from %s:%hu", env->stats_host,
	    env->stats_port);

	if (evtimer_pending(env->stats_ev, NULL))
		evtimer_del(env->stats_ev);
}

void
stomp_connect_cb(struct stomp_connection *c, struct stomp_frame *frame,
    void *arg)
{
	struct enqueue	*env = (struct enqueue *)arg;

	log_debug("Connected to %s:%hu", env->stomp_host,
	    env->stomp_port);

	env->state |= ENQUEUE_STOMP_CONNECTED;
}

void
stomp_disconnect_cb(struct stomp_connection *c, void *arg)
{
	struct enqueue	*env = (struct enqueue *)arg;

	log_debug("Disconnected from %s:%hu", env->stomp_host,
	    env->stomp_port);

	env->state &= ~(ENQUEUE_STOMP_CONNECTED);

	/* Deactivate the timeout if pending */
	if (evtimer_pending(env->ev, NULL))
		evtimer_del(env->ev);

	/* FIXME Dump the current buffer */
	*env->buffer = '\0';
}

int
main(int argc, char *argv[])
{
	int			 c;
	int			 debug = 0;
	int			 noaction = 0;
	const char		*conffile = ENQUEUE_CONF_FILE;
	//u_int			 flags = 0;
	//struct passwd		*pw;
	struct event_config	*cfg;
	struct enqueue		*env;
	SSL_CTX			*ctx;
	struct listen_addr	*la, *nla;
	struct event		*ev_sighup;
	struct event		*ev_sigint;
	struct event		*ev_sigterm;

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

	SSL_load_error_strings();
	SSL_library_init();

	ctx = SSL_CTX_new(SSLv23_client_method());

	SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2);
	SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

	if ((cfg = event_config_new()) == NULL)
		fatalx("event_config_new");

#ifdef __APPLE__
	/* Don't use kqueue(2) on OS X */
	event_config_avoid_method(cfg, "kqueue");
#endif

	env->base = event_base_new_with_config(cfg);
	if (!env->base)
		fatalx("event_base_new_with_config");
	event_config_free(cfg);

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
			fatalx("");
		}

		log_info("listening on %s:%d",
		    log_sockaddr((struct sockaddr *)&la->sa), la->port);

		if ((la->listener = evconnlistener_new_bind(env->base,
		    graphite_server_accept, (void *)env,
		    LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1,
		    (struct sockaddr *)&la->sa,
		    SA_LEN((struct sockaddr *)&la->sa))) == NULL) {
			log_warn("bind on %s failed, skipping",
			    log_sockaddr((struct sockaddr *)&la->sa));
			nla = TAILQ_NEXT(la, entry);
			TAILQ_REMOVE(&env->listen_addrs, la, entry);
			free(la);
			la = nla;
			continue;
		}

		la = TAILQ_NEXT(la, entry);
	}

	if (graphite_init(env->base) < 0)
		fatalx("graphite_init");
	if ((env->stats_conn = graphite_connection_new(env->stats_host,
	    env->stats_port, env->stats_reconnect)) == NULL)
		fatalx("graphite_connection_new");
	graphite_connection_setcb(env->stats_conn, stats_connect_cb,
	    stats_disconnect_cb, (void *)env);
	env->stats_ev = event_new(env->base, -1, EV_PERSIST, stats_timer_cb,
	    (void *)env);

	if (stomp_init(env->base) < 0)
		fatalx("stomp_init");
	if ((env->stomp_conn = stomp_connection_new(env->stomp_host,
	    env->stomp_port, env->stomp_version, env->stomp_vhost,
	    env->stomp_user, env->stomp_password,
	    (env->stomp_flags * STOMP_FLAG_SSL) ? ctx : NULL,
	    env->stomp_reconnect, env->stomp_heartbeat,
	    env->stomp_heartbeat)) == NULL)
		fatalx("stomp_connection_new");
	stomp_connection_setcb(env->stomp_conn, stomp_connect_cb, NULL, NULL,
	    stomp_disconnect_cb, (void *)env);

	if (env->stomp_bytes > 0) {
		/* Allocate buffer to batch up metrics */
		if ((env->buffer = calloc(1, env->stomp_bytes + 1)) == NULL)
			fatal("calloc");

		/* Create timer event for flushing */
		env->ev = evtimer_new(env->base, stomp_flush, (void *)env);
		env->t.tv_sec = env->stomp_timeout / 1000;
		env->t.tv_usec = (env->stomp_timeout % 1000) * 1000;
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

	signal(SIGPIPE, SIG_IGN);
	ev_sighup = evsignal_new(env->base, SIGHUP, handle_signal, env);
	ev_sigint = evsignal_new(env->base, SIGINT, handle_signal, env);
	ev_sigterm = evsignal_new(env->base, SIGTERM, handle_signal, env);
	evsignal_add(ev_sighup, NULL);
	evsignal_add(ev_sigint, NULL);
	evsignal_add(ev_sigterm, NULL);

	graphite_connect(env->stats_conn);
	stomp_connect(env->stomp_conn);

	event_base_dispatch(env->base);

	SSL_CTX_free(ctx);

	return (0);
}
