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

#include <sys/param.h>

#include <stdio.h>
#include <unistd.h>
#include <err.h>
#include <signal.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include "rewrite.h"

__dead void	 usage(void);
void		 handle_signal(int, short, void *);
int		 pcre_replace(char *, char *, int *, int, char *, int);
void		 stats_connect_cb(struct graphite_connection *, void *);
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
	//struct rewrite	*env = (struct rewrite *)arg;

	log_info("exiting on signal %d", sig);

	exit(0);
}

int
pcre_replace(char *subject, char *replacement, int *ovector, int stringcount,
    char *buffer, int buffersize)
{
	int		 total = ovector[0], len;
	char		*ptr = buffer, *p1, *p2, *num;
	long long	 ss;
	size_t		 size;

	/* Allow for the extra storage for the trailing null */
	if (buffersize)
		buffersize--;

	/* Copy any characters before the match */
	if (ptr) {
		len = MIN(buffersize, ovector[0]);
		strncpy(ptr, subject, len);
		ptr += len;
		buffersize -= len;
	}

	/* Loop through the replacement string looking for backslashes */
	p1 = p2 = replacement;
	while ((p1 = strchr(p1, '\\')) != NULL) {
		/* Copy any characters in the replacement before the backslash */
		if (ptr) {
			len = MIN(buffersize, p1 - p2);
			strncpy(ptr, p2, len);
			ptr += len;
			buffersize -= len;
		}
		total += p1 - p2;
		p1++;
		/* Get the substring number */
		if ((size = strspn(p1, "0123456789")) == 0)
			return (-1);
		if ((num = calloc(size + 1, sizeof(char))) == NULL)
			return (-1);
		strncpy(num, p1, size);
		if ((ss = strtonum(num, 1, 10, NULL)) == 0) {
			free(num);
			return (-1);
		}
		free(num);
		/* Substring is out of bounds */
		if (ss >= stringcount)
			return (-1);
		len = ovector[(ss << 1) + 1] - ovector[ss << 1];
		total += len;
		/* pcre_copy_substring won't do a partial copy */
		if ((num = calloc(len + 1, sizeof(char))) == NULL)
			return (-1);
		pcre_copy_substring(subject, ovector, stringcount, ss, num, len + 1);
		if (ptr) {
			len = MIN(buffersize, (int)strlen(num));
			strncpy(ptr, num, len);
			ptr += len;
			buffersize -= len;
		}
		free(num);
		p1 += size;
		p2 = p1;
	}
	total += strlen(p2);

	/* Copy any characters in the replacement after the substring */
	if (ptr) {
		len = MIN(buffersize, (int)strlen(p2));
		strncpy(ptr, p2, len);
		ptr += len;
		buffersize -= len;
	}

	/* Copy any characters after the match */
	if (ovector[1] < (int)strlen(subject)) {
		total += strlen(subject) - ovector[1];
		if (ptr) {
			len = MIN(buffersize, (int)strlen(subject + ovector[1]));
			strncpy(ptr, subject + ovector[1], len);
			buffersize -= len;
		}
	}

	return (total);
}

void
stats_connect_cb(struct graphite_connection *c, void *arg)
{
	struct rewrite	*env = (struct rewrite *)arg;

	log_debug("Connected to %s:%hu", env->stats_host,
	    env->stats_port);

	evtimer_add(env->stats_ev, &env->stats_interval);
}

void
stats_timer_cb(int fd, short event, void *arg)
{
	struct rewrite	*env = (struct rewrite *)arg;
	struct timeval	 tv;

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
	    "rules", tv, "%lld", env->rules);
	graphite_send_metric(env->stats_conn, env->stats_prefix,
	    "metrics", tv, "%lld", env->metrics);
	graphite_send_metric(env->stats_conn, env->stats_prefix,
	    "rewrites", tv, "%lld", env->rewrites);
}

void
stats_disconnect_cb(struct graphite_connection *c, void *arg)
{
	struct rewrite	*env = (struct rewrite *)arg;

	fprintf(stderr, "Stats down\n");

	if (evtimer_pending(env->stats_ev, NULL))
		evtimer_del(env->stats_ev);
}

void
stomp_connect_cb(struct stomp_connection *c, struct stomp_frame *frame,
    void *arg)
{
	struct rewrite		*env = (struct rewrite *)arg;
	struct stomp_sub	*sub;

//	env->state |= DEQUEUE_STOMP_CONNECTED;
//	check_state(env);

	for (sub = TAILQ_FIRST(&env->stomp_subs); sub;
	    sub = TAILQ_NEXT(sub, entry)) {
		sub->subscription = stomp_subscription_new(env->stomp_conn,
		    sub->path, sub->ack);
		stomp_subscription_setcb(sub->subscription, stomp_message_cb,
		    (void *)sub);
		stomp_subscribe(env->stomp_conn, sub->subscription);
	}
		//stomp_unsubscribe(env->stomp_conn, sub->subscription);
		//stomp_subscription_free(sub->subscription);
		//sub->subscription = NULL;
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
	struct rewrite_rule	 *rule;
	int			  rc;
	int			  ovector[30];
	int			  size, total;
	char			 *new;

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

	/* Allocate array of pointers 4 x # of lines to store the
	 * metric, value and timestamp on each line along with the
	 * potentially rewritten metric
	 */
	if ((part = calloc(lines * 4, sizeof(unsigned char *))) == NULL)
		fatal("calloc");

	for (i = j = 0; i < lines; i++) {

		/* Ignore completely empty lines */
		if (strlen(line[i]) == 0)
			continue;

		if (graphite_parse(NULL, line[i], &part[j]) < 0) {
			log_warnx("Can't parse line \"%s\"", line[i]);
			break;
		}
		j += 4;
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

	/* Message looks ok, apply any rewrite rules to each line */
	for (i = j = total = 0; i < lines; i++) {

		/* Ignore completely empty lines */
		if (strlen(line[i]) == 0)
			continue;

		/* Add a newline delimiter between lines */
		if (total)
			total++;

		part[j + 3] = strdup(part[j + GRAPHITE_PART_METRIC]);

		sub->env->metrics++;

		for (rule = TAILQ_FIRST(&sub->env->rewrite_rules); rule;
		    rule = TAILQ_NEXT(rule, entry)) {
			if ((rc = pcre_exec(rule->re, rule->sd, part[j + 3],
			    strlen(part[j + 3]), 0, 0, ovector, 30)) < -1) {
				log_warnx("Match failed: %d", rc);
				continue;
			}
			if (rc == 0) {
				log_warnx("ovector too small");
				continue;
			}
			if (rc > 0) {
				size = pcre_replace(part[j + 3], rule->replacement,
				    ovector, rc, NULL, 0);
				new = calloc(size + 1, sizeof(char));
				pcre_replace(part[j + 3], rule->replacement, ovector, rc,
				    new, size + 1);

				sub->env->rewrites++;

				free(part[j + 3]);
				part[j + 3] = new;
			}
		}

		/* Track length of rewritten line */
		total += 2 + strlen(part[j + 3]) +
		    strlen(part[j + GRAPHITE_PART_VALUE]) +
		    strlen(part[j + GRAPHITE_PART_TIMESTAMP]);

		if (strcmp(part[j + GRAPHITE_PART_METRIC], part[j + 3]))
			log_debug("Metric was \"%s\", is now \"%s\"",
			    part[j + GRAPHITE_PART_METRIC], part[j + 3]);

		j += 4;
	}

	/* Create new message and send it */
	if ((new = calloc(total + 1, sizeof(char))) == NULL)
		goto end;

	for (i = j = 0; i < lines; i++) {

		/* Ignore completely empty lines */
		if (strlen(line[i]) == 0)
			continue;

		if (strlen(new))
			strcat(new, "\n");

		strcat(new, part[j + 3]);
		strcat(new, " ");
		strcat(new, part[j + GRAPHITE_PART_VALUE]);
		strcat(new, " ");
		strcat(new, part[j + GRAPHITE_PART_TIMESTAMP]);

		/* Free this now we don't need it */
		free(part[j + 3]);

		j += 4;
	}

	stomp_send(sub->env->stomp_conn, sub->env->stomp_send, new, NULL);
	free(new);

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
	struct rewrite		*env = (struct rewrite *)arg;
	struct stomp_sub	*sub;

//	env->state &= ~(DEQUEUE_STOMP_CONNECTED);

	/* FIXME Need to clear down all of the subscription state */
	for (sub = TAILQ_FIRST(&env->stomp_subs); sub;
	    sub = TAILQ_NEXT(sub, entry)) {
		if (evtimer_pending(sub->ack_ev, NULL))
			evtimer_del(sub->ack_ev);
	}

//	check_state(env);
}

int
main(int argc, char *argv[])
{
	int			 c;
	int			 debug = 0;
	int			 noaction = 0;
	const char		*conffile = REWRITE_CONF_FILE;
	//u_int			 flags = 0;
	//struct passwd		*pw;
	struct event_config	*cfg;
	struct rewrite		*env;
	SSL_CTX			*ctx;
	struct stomp_sub	*sub;
	struct rewrite_rule	*rule, *nrule;
	const char		*pcre_err;
	int			 pcre_err_offset;
	//int			 rc;
	//int			 ovector[30];
	//int			 size;
	//char			*new;
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

	if ((pw = getpwnam(REWRITE_USER)) == NULL)
		errx(1, "unknown user %s", REWRITE_USER);
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
	    (env->stomp_flags & STOMP_FLAG_SSL) ? ctx : NULL,
	    env->stomp_reconnect, env->stomp_heartbeat,
	    env->stomp_heartbeat)) == NULL)
		fatalx("stomp_connection_new");
	stomp_connection_setcb(env->stomp_conn, stomp_connect_cb, NULL, NULL,
	    stomp_disconnect_cb, (void *)env);

	for (sub = TAILQ_FIRST(&env->stomp_subs); sub; sub = TAILQ_NEXT(sub,
	    entry))
		sub->ack_ev = evtimer_new(env->base, stomp_ack_cb, (void *)sub);
	
	/* Compile the rules */
	for (rule = TAILQ_FIRST(&env->rewrite_rules); rule; ) {
		if ((rule->re = pcre_compile(rule->pattern, 0, &pcre_err,
		    &pcre_err_offset, NULL)) == NULL) {
			log_warnx("Compilation failed: %s, at %d\n", pcre_err,
			    pcre_err_offset);
			nrule = TAILQ_NEXT(rule, entry);
			TAILQ_REMOVE(&env->rewrite_rules, rule, entry);
			free(rule->pattern);
			free(rule->replacement);
			free(rule);
			rule = nrule;
			continue;
		}
		rule->sd = pcre_study(rule->re,
#ifdef PCRE_CONFIG_JIT
		    PCRE_STUDY_JIT_COMPILE,
#else
		    0,
#endif
		    &pcre_err);
		if (pcre_err) {
			log_warnx("Study failed: %s\n", pcre_err);
			nrule = TAILQ_NEXT(rule, entry);
			TAILQ_REMOVE(&env->rewrite_rules, rule, entry);
			pcre_free(rule->re);
			free(rule->pattern);
			free(rule->replacement);
			free(rule);
			rule = nrule;
			continue;
		}

		env->rules++;

		rule = TAILQ_NEXT(rule, entry);
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

#if 0
	/* A typical verbose collectd metric converted to graphite */
	subject = strdup("server_example_com.cpu.0.cpu.idle.value");

	for (rule = TAILQ_FIRST(&env->rewrite_rules); rule;
	    rule = TAILQ_NEXT(rule, entry)) {
		if ((rc = pcre_exec(rule->re, rule->sd, subject,
		    strlen(subject), 0, 0, ovector, 30)) < -1) {
			fprintf(stderr, "Match failed: %d\n", rc);
			continue;
		}
		if (rc == 0) {
			fprintf(stderr, "ovector too small\n");
			continue;
		}
		if (rc > 0) {
			fprintf(stderr, "String was \"%s\"\n", subject);

			size = pcre_replace(subject, rule->replacement,
			    ovector, rc, NULL, 0);
			new = calloc(size + 1, sizeof(char));
			pcre_replace(subject, rule->replacement, ovector, rc,
			    new, size + 1);

			fprintf(stderr, "String is now \"%s\"\n", new);
			free(subject);
			subject = new;
		}
	}
#endif

	event_base_dispatch(env->base);

	while (!TAILQ_EMPTY(&env->rewrite_rules)) {
		rule = TAILQ_FIRST(&env->rewrite_rules);
		TAILQ_REMOVE(&env->rewrite_rules, rule, entry);
#ifdef PCRE_CONFIG_JIT
		pcre_free_study(rule->sd);
#else
		pcre_free(rule->sd);
#endif
		pcre_free(rule->re);
		free(rule->pattern);
		free(rule->replacement);
		free(rule);
	}

	SSL_CTX_free(ctx);

	return (0);
}
