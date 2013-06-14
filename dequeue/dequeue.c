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
#include <unistd.h>
#include <err.h>
#include <pwd.h>
#include <signal.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include "dequeue.h"

__dead void	 usage(void);
void		 handle_signal(int, short, void *);
void		 check_state(struct dequeue *);
void		 graphite_connect_cb(struct graphite_connection *, void *);
void		 graphite_disconnect_cb(struct graphite_connection *, void *);
void		 stats_connect_cb(struct graphite_connection *, void *);
void		 stats_send_metric(struct graphite_connection *, char *,
		    char *, struct timeval, char *, ...);
void		 stats_timer_cb(int, short, void *);
void		 stats_disconnect_cb(struct graphite_connection *, void *);
void		 stomp_connect_cb(struct stomp_connection *,
		    struct stomp_frame *, void *);
void		 stomp_message_cb(struct stomp_connection *,
		    struct stomp_subscription *, struct stomp_frame *, void *);
void		 stomp_ack_cb(int, short, void *);
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
	//struct dequeue	*env = (struct dequeue *)arg;

	log_info("exiting on signal %d", sig);

	exit(0);
}

void
check_state(struct dequeue *env)
{
	struct stomp_sub	*sub;

	log_debug("State: Graphite (%s), STOMP (%s)",
	    (env->state & DEQUEUE_GRAPHITE_CONNECTED) ? "Up" : "Down",
	    (env->state & DEQUEUE_STOMP_CONNECTED) ? "Up" : "Down");

	if (!(env->state & DEQUEUE_STOMP_CONNECTED))
		return;

	for (sub = TAILQ_FIRST(&env->stomp_subs); sub;
	    sub = TAILQ_NEXT(sub, entry))
		if (env->state & DEQUEUE_GRAPHITE_CONNECTED) {
			sub->subscription = stomp_subscription_new(env->stomp_conn,
			    sub->path, sub->ack);
			stomp_subscription_setcb(sub->subscription,
			    stomp_message_cb, (void *)sub);
			stomp_subscribe(env->stomp_conn, sub->subscription);
		} else if (sub->subscription) {
			stomp_unsubscribe(env->stomp_conn, sub->subscription);
			stomp_subscription_free(sub->subscription);
			sub->subscription = NULL;
		}
}

void
graphite_connect_cb(struct graphite_connection *c, void *arg)
{
	struct dequeue	*env = (struct dequeue *)arg;

	log_debug("Connected to %s:%hu", env->graphite_host,
	    env->graphite_port);
	env->state |= DEQUEUE_GRAPHITE_CONNECTED;
	check_state(env);
}

void
graphite_disconnect_cb(struct graphite_connection *c, void *arg)
{
	struct dequeue	*env = (struct dequeue *)arg;

	env->state &= ~(DEQUEUE_GRAPHITE_CONNECTED);
	check_state(env);
}

void
stats_connect_cb(struct graphite_connection *c, void *arg)
{
	struct dequeue	*env = (struct dequeue *)arg;

	log_debug("Connected to %s:%hu", env->stats_host,
	    env->stats_port);

	evtimer_add(env->stats_ev, &env->stats_interval);
}

void
stats_send_metric(struct graphite_connection *c, char *prefix, char *metric,
    struct timeval tv, char *format, ...)
{
	char	 buffer[1024];
	char	*m, *v, *t;
	size_t	 size, length;
	va_list	 ap;

	length = 1024;
	va_start(ap, format);

	m = buffer;
	if ((size = snprintf(m, length, "%s.%s", prefix, metric)) >= length)
		goto bad;
	length -= size + 1;

	v = m + size + 1;
	if ((size = vsnprintf(v, length, format, ap)) >= length)
		goto bad;
	length -= size + 1;

	t = v + size + 1;
	if ((size = snprintf(t, length, "%ld", tv.tv_sec)) >= length)
		goto bad;

	va_end(ap);
	graphite_send(c, m, v, t);
	return;
bad:
	va_end(ap);
	log_warnx("Insufficient space to render metric");
}

void
stats_timer_cb(int fd, short event, void *arg)
{
	struct dequeue	*env = (struct dequeue *)arg;
	struct timeval	 tv;

	gettimeofday(&tv, NULL);

	/* FIXME shouldn't really be poking inside structs */
	stats_send_metric(env->stats_conn, env->stats_prefix,
	    "stomp.bytes.rx", tv, "%lld", env->stomp_conn->bytes_rx);
	stats_send_metric(env->stats_conn, env->stats_prefix,
	    "stomp.bytes.tx", tv, "%lld", env->stomp_conn->bytes_tx);
	stats_send_metric(env->stats_conn, env->stats_prefix,
	    "stomp.messages.rx", tv, "%lld", env->stomp_conn->messages_rx);
	stats_send_metric(env->stats_conn, env->stats_prefix,
	    "stomp.messages.tx", tv, "%lld", env->stomp_conn->messages_tx);
	stats_send_metric(env->stats_conn, env->stats_prefix,
	    "stomp.frames.rx", tv, "%lld", env->stomp_conn->frames_rx);
	stats_send_metric(env->stats_conn, env->stats_prefix,
	    "stomp.frames.tx", tv, "%lld", env->stomp_conn->frames_tx);
	stats_send_metric(env->stats_conn, env->stats_prefix,
	    "stomp.buffer.input", tv, "%zd",
	    (env->stomp_conn->bev == NULL) ? 0 :
	    evbuffer_get_length(bufferevent_get_input(env->stomp_conn->bev)));
	stats_send_metric(env->stats_conn, env->stats_prefix,
	    "stomp.buffer.output", tv, "%zd",
	    (env->stomp_conn->bev == NULL) ? 0 :
	    evbuffer_get_length(bufferevent_get_output(env->stomp_conn->bev)));
	stats_send_metric(env->stats_conn, env->stats_prefix,
	    "graphite.bytes.tx", tv, "%lld", env->graphite_conn->bytes_tx);
	stats_send_metric(env->stats_conn, env->stats_prefix,
	    "graphite.metrics.tx", tv, "%lld", env->graphite_conn->metrics_tx);
	stats_send_metric(env->stats_conn, env->stats_prefix,
	    "graphite.buffer.input", tv, "%zd",
	    (env->graphite_conn->bev == NULL) ? 0 :
	    evbuffer_get_length(bufferevent_get_input(env->graphite_conn->bev)));
	stats_send_metric(env->stats_conn, env->stats_prefix,
	    "graphite.buffer.output", tv, "%zd",
	    (env->graphite_conn->bev == NULL) ? 0 :
	    evbuffer_get_length(bufferevent_get_output(env->graphite_conn->bev)));
}

void
stats_disconnect_cb(struct graphite_connection *c, void *arg)
{
	struct dequeue	*env = (struct dequeue *)arg;

	fprintf(stderr, "Stats down\n");

	if (evtimer_pending(env->stats_ev, NULL))
		evtimer_del(env->stats_ev);
}

void
stomp_connect_cb(struct stomp_connection *c, struct stomp_frame *frame,
    void *arg)
{
	struct dequeue	*env = (struct dequeue *)arg;

	env->state |= DEQUEUE_STOMP_CONNECTED;
	check_state(env);
}

void
stomp_message_cb(struct stomp_connection *c,
    struct stomp_subscription *subscription, struct stomp_frame *frame,
    void *arg)
{
	struct stomp_sub	 *sub = (struct stomp_sub *)arg;
	struct stomp_header	 *header;
	char			 *ptr;
	int			  i, j, lines;
	char			**line;
	size_t			  len;
	char			**part;

	/* Count how many lines (metrics) are in this message */
	lines = 1;
	ptr = (char *)frame->body;
	while (*ptr != '\0') {
		len = strcspn(ptr, "\r\n");
		ptr += len;
		if (*ptr != '\0') {
			lines++;
			while (*ptr == '\r') ptr++;
			while (*ptr == '\n') ptr++;
		}
	}
	log_debug("%d line%s in message", lines, (lines == 1) ? "" : "s");

	/* Allocate array of pointers to reference each line */
	if ((line = calloc(lines, sizeof(unsigned char *))) == NULL)
		fatal("calloc");

	/* Go through message again, this time store a pointer to
	 * each line and replace all linebreak characters with nulls
	 */
	i = 0;
	line[i] = ptr = (char *)frame->body;
	while (*ptr != '\0') {
		len = strcspn(ptr, "\r\n");
		ptr += len;
		if (*ptr != '\0') {
			while (*ptr == '\r') *(ptr++) = '\0';
			while (*ptr == '\n') *(ptr++) = '\0';
			line[++i] = ptr;
		}
	}

	/* Allocate array of pointers 3 x # of lines to store the
	 * metric, value and timestamp on each line
	 */
	if ((part = calloc(lines * 3, sizeof(unsigned char *))) == NULL)
		fatal("calloc");

	for (i = j = 0; i < lines; i++) {

		/* Ignore completely empty lines */
		if (strlen(line[i]) == 0)
			continue;

		if (graphite_parse(NULL, line[i], &part[j]) < 0) {
			log_warnx("Can't parse line \"%s\"", line[i]);
			break;
		}
		j += 3;
	}

	/* If this is not auto ack, we will need the "ack" or "message-id"
	 * header value
	 */
	switch (sub->ack) {
	case STOMP_ACK_CLIENT:
		/* FALLTHROUGH */
	case STOMP_ACK_CLIENT_INDIVIDUAL:
		if (((header = stomp_frame_header_find(frame, "ack")) == NULL) &&
		    ((header = stomp_frame_header_find(frame, "message-id")) == NULL))
			goto end;
		break;
	default:
		break;
	}

	/* Parse failures, reject the message */
	if (i < lines)
		switch (sub->ack) {
		case STOMP_ACK_CLIENT:
			/* Cancel any pending ack timer */
			if (evtimer_pending(sub->ack_ev, NULL))
				evtimer_del(sub->ack_ev);
			/* Acknowledge any previous messages immediately */
			if (sub->ack_pending) {
				log_debug("Ack previous");
				stomp_ack(c, sub->subscription,
				    sub->ack_pending, NULL);
				free(sub->ack_pending);
				sub->ack_pending = NULL;
			}
			/* FALLTHROUGH */
		case STOMP_ACK_CLIENT_INDIVIDUAL:
			/* Negatively acknowledge this message immediately */
			log_debug("Nack");
#if 0
			stomp_nack(c, sub->subscription, header->value, NULL);
#else
			/* RabbitMQ at least seems to only redeliver messages
			 * upon receiving a NACK so if a bad message gets into
			 * the queue it will just cause a loop
			 */
			stomp_ack(c, sub->subscription, header->value, NULL);
#endif
			/* FALLTHROUGH */
		default:
			goto end;
			/* NOTREACHED */
			break;
		}

	/* Success, send on to Graphite and acknowledge */
	for (i = j = 0; i < lines; i++) {

		/* Ignore completely empty lines */
		if (strlen(line[i]) == 0)
			continue;

		graphite_send(sub->env->graphite_conn,
		    part[j + GRAPHITE_PART_METRIC],
		    part[j + GRAPHITE_PART_VALUE],
		    part[j + GRAPHITE_PART_TIMESTAMP]);
		j += 3;
	}
	switch (sub->ack) {
	case STOMP_ACK_CLIENT:
		if (sub->ack_pending)
			free(sub->ack_pending);
		sub->ack_pending = strdup(header->value);
		/* If we don't have an active ack timer, start it ticking */
		if (!evtimer_pending(sub->ack_ev, NULL))
			evtimer_add(sub->ack_ev, &sub->ack_tv);
		break;
	case STOMP_ACK_CLIENT_INDIVIDUAL:
		stomp_ack(c, sub->subscription, header->value, NULL);
		break;
	default:
		break;
	}

end:
	free(part);
	free(line);
}

void
stomp_ack_cb(int fd, short event, void *arg)
{
	struct stomp_sub	*sub = (struct stomp_sub *)arg;

	log_debug("ACK timer fired for subscription #%s",
	    sub->subscription->id);
	stomp_ack(sub->env->stomp_conn, sub->subscription, sub->ack_pending,
	    NULL);
	free(sub->ack_pending);
	sub->ack_pending = NULL;
}

void
stomp_disconnect_cb(struct stomp_connection *c, void *arg)
{
	struct dequeue		*env = (struct dequeue *)arg;
	struct stomp_sub	*sub;

	env->state &= ~(DEQUEUE_STOMP_CONNECTED);

	/* FIXME Need to clear down all of the subscription state */
	for (sub = TAILQ_FIRST(&env->stomp_subs); sub;
	    sub = TAILQ_NEXT(sub, entry)) {
		if (evtimer_pending(sub->ack_ev, NULL))
			evtimer_del(sub->ack_ev);
	}

	check_state(env);
}

int
main(int argc, char *argv[])
{
	int			  c;
	int			  debug = 0;
	int			  noaction = 0;
	const char		 *conffile = DEQUEUE_CONF_FILE;
	//u_int			  flags = 0;
	//struct passwd		 *pw;

	struct event_config	 *cfg;
	struct dequeue		 *env;
	SSL_CTX			 *ctx;
	struct stomp_sub	 *sub;
	struct event		 *ev_sighup;
	struct event		 *ev_sigint;
	struct event		 *ev_sigterm;

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
			//flags |= DEQUEUE_F_VERBOSE;
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

	if ((pw = getpwnam(DEQUEUE_USER)) == NULL)
		errx(1, "unknown user %s", DEQUEUE_USER);
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

	if (graphite_init(env->base) < 0)
		fatalx("graphite_init");
	if ((env->graphite_conn = graphite_connection_new(env->graphite_host,
	    env->graphite_port, env->graphite_reconnect)) == NULL)
		fatalx("graphite_connection_new");
	graphite_connection_setcb(env->graphite_conn, graphite_connect_cb,
	    graphite_disconnect_cb, (void *)env);
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
	    (env->stomp_flags & STOMP_FLAG_SSL) ? ctx : NULL,
	    env->stomp_reconnect, env->stomp_heartbeat,
	    env->stomp_heartbeat)) == NULL)
		fatalx("stomp_connection_new");
	stomp_connection_setcb(env->stomp_conn, stomp_connect_cb, NULL, NULL,
	    stomp_disconnect_cb, (void *)env);

	for (sub = TAILQ_FIRST(&env->stomp_subs); sub; sub = TAILQ_NEXT(sub,
	    entry))
		sub->ack_ev = evtimer_new(env->base, stomp_ack_cb, (void *)sub);

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

	graphite_connect(env->graphite_conn);
	graphite_connect(env->stats_conn);
	stomp_connect(env->stomp_conn);

	event_base_dispatch(env->base);

	SSL_CTX_free(ctx);

	return (0);
}
