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

#include <sys/uio.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <pwd.h>
#include <signal.h>

#include "dequeue.h"

#define	SPACE	" "
#define	NEWLINE	"\n"

__dead void	 usage(void);
int		 graphite_open(struct graphite *);
void		 graphite_close(struct graphite *);

__dead void
usage(void)
{
	extern char	*__progname;

	fprintf(stderr, "usage: %s [-dnv] [-f file]\n", __progname);
	exit(1);
}

int
graphite_open(struct graphite *env)
{
	struct dequeue_addr	*h = NULL, *next;

	if (host(env->host, &h) == -1)
		log_warnx("could not parse address spec \"%s\"", env->host);

	if (h == NULL &&
	    (host_dns(env->host, &h) == -1 || !h))
		log_warnx("could not resolve \"%s\"", env->host);

	for (; h != NULL; h = next) {
		next = h->next;

		switch (h->ss.ss_family) {
		case AF_INET:
			((struct sockaddr_in *)&h->ss)->sin_port =
			    htons(env->port);
			break;
		case AF_INET6:
			((struct sockaddr_in6 *)&h->ss)->sin6_port =
			    htons(env->port);
			break;
		default:
			fatalx("invalid address family");
		}

		log_info("connecting to %s:%d",
		    log_sockaddr((struct sockaddr *)&h->ss), env->port);

		if ((env->fd = socket(h->ss.ss_family, SOCK_STREAM, 0)) == -1)
			fatal("socket");

		if (connect(env->fd, (struct sockaddr *)&h->ss,
		    SA_LEN((struct sockaddr *)&h->ss)) == -1) {
			log_warn("connect to %s failed, skipping",
			    log_sockaddr((struct sockaddr *)&h->ss));
			close(env->fd);
			env->fd = -1;
			free(h);
			continue;
		}

		break;
	}
	/* Free up any outstanding addresses we didn't need to try */
	for (; h != NULL; h = next) {
		next = h->next;
		free(h);
	}
	return (env->fd);
}

void
graphite_close(struct graphite *env)
{
	close(env->fd);
	env->fd = -1;
}

int
main(int argc, char *argv[])
{
	int		  c;
	int		  debug = 0;
	int		  noaction = 0;
	const char	 *conffile = DEQUEUE_CONF_FILE;
	//u_int		  flags = 0;
	struct passwd	 *pw;

	size_t		  len;
	struct dequeue	 *env;
	int		  tag;
	char		 *buf = NULL;
	char		 *key = NULL;
	char		 *ptr;
	int		  i, j, lines;
	char		**line;
	struct iovec	 *data;
	ssize_t		  bytes;

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

	if (amqp_open(env->amqp) != 0)
		fatalx("amqp_open");
	if (amqp_exchange(env->amqp) != 0)
		fatalx("amqp_exchange");
	if (amqp_queue(env->amqp) != 0)
		fatalx("amqp_queue");

	if (graphite_open(env->graphite) == -1)
		fatalx("graphite_open");

	/* At this point, we are ready to consume messages in some sort of loop
	 */
	while (1) {
		/* Reconnect to graphite */
		if (env->graphite->fd == -1)
			while (graphite_open(env->graphite) == -1)
				sleep(1);

		if ((tag = amqp_consume(env->amqp, &key, &buf, &len)) < 0)
			fatalx("amqp_consume");

		/* Count how many lines (metrics) are in this message */
		lines = 1;
		ptr = buf;
		while (*ptr != '\0') {
			len = strcspn(ptr, "\r\n");
			ptr += len;
			if (*ptr != '\0') {
				lines++;
				while (*ptr == '\r') ptr++;
				while (*ptr == '\n') ptr++;
			}
		}
		log_debug("%d line%s in message", lines,
		    (lines == 1) ? "" : "s");

		/* Allocate array of pointers to reference each line */
		if ((line = calloc(lines, sizeof(char *))) == NULL)
			fatal("calloc");

		/* Go through message again, this time store a pointer to
		 * each line and replace all linebreak characters with nulls
		 */
		i = 0;
		line[i] = ptr = buf;
		while (*ptr != '\0') {
			len = strcspn(ptr, "\r\n");
			ptr += len;
			if (*ptr != '\0') {
				while (*ptr == '\r') *(ptr++) = '\0';
				while (*ptr == '\n') *(ptr++) = '\0';
				line[++i] = ptr;
			}
		}

		/* Allocate array of iovec structures */
		if ((data = calloc(
		    (env->amqp->flags & AMQP_FLAG_METRIC_IN_MESSAGE) ? lines * 2 : lines * 4,
		    sizeof(struct iovec))) == NULL)
			fatal("calloc");

		/* Parse each line, one failure rejects the message. Build up
		 * list of iovec structures as we go
		 */
		for (i = 0, j = 0; i < lines; i++) {

			/* Ignore completely empty lines */
			if (strlen(line[i]) == 0)
				continue;

			if (env->amqp->flags & AMQP_FLAG_METRIC_IN_MESSAGE) {
				if (graphite_parse(NULL, line[i]) < 0)
					break;
			} else {
				if (graphite_parse(key, line[i]) < 0)
					break;

				data[j].iov_base = key;
				data[j].iov_len = strlen(key);
				j++;

				data[j].iov_base = SPACE;
				data[j].iov_len = strlen(SPACE);
				j++;
			}

			data[j].iov_base = line[i];
			data[j].iov_len = strlen(line[i]);
			j++;

			data[j].iov_base = NEWLINE;
			data[j].iov_len = strlen(NEWLINE);
			j++;
		}
		if (i < lines) {
			amqp_reject(env->amqp, tag, 0);
			log_debug("message rejected");
		} else {
			/* Go go gadget writev
			 *
			 * XXX If the remote end has disappeared, the writev
			 *     may still succeed before it starts failing so
			 *     there is a risk a metric can be lost here
			 */
			if ((bytes = writev(env->graphite->fd, data, j)) == -1) {
				log_warn("writev");
				/* Reject and re-queue */
				amqp_reject(env->amqp, tag, 1);
				graphite_close(env->graphite);
			} else {
				amqp_acknowledge(env->amqp, tag);
				log_debug("message accepted, %d bytes written to graphite",
				    bytes);
			}
		}

		/* Free all of the things */
		free(data);
		free(line);
		free(key);
		free(buf);
	}

	amqp_close(env->amqp);

	return (0);
}
