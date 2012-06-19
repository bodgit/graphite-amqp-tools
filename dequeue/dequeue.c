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

#include "dequeue.h"

__dead void	 usage(void);

__dead void
usage(void)
{
	extern char	*__progname;

	fprintf(stderr, "usage: %s [-dnv] [-f file]\n", __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int		 c;
	int		 debug = 0;
	int		 noaction = 0;
	const char	*conffile = DEQUEUE_CONF_FILE;
	//u_int		 flags = 0;
	struct passwd	*pw;

	size_t		 len;
	struct dequeue	*env;
	int		 tag;
	char		*buf = NULL;
	char		*key = NULL;
	char		*ptr;
	int		 i, lines;
	char		**line;

	struct graphite_addr	*ga;
	int			 fd;

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

	for (ga = TAILQ_FIRST(&env->graphite_addrs); ga; ) {
		switch (ga->sa.ss_family) {
		case AF_INET:
			((struct sockaddr_in *)&ga->sa)->sin_port =
			    htons(ga->port);
			break;
		case AF_INET6:
			((struct sockaddr_in6 *)&ga->sa)->sin6_port =
			    htons(ga->port);
			break;
		default:
			fatalx("");
		}

		log_info("connecting to %s:%d",
		    log_sockaddr((struct sockaddr *)&ga->sa), ga->port);

		if ((fd = socket(ga->sa.ss_family, SOCK_STREAM, 0)) == -1)
			fatal("socket");

		if (connect(fd, (struct sockaddr *)&ga->sa,
		    ((struct sockaddr *)&ga->sa)->sa_len) == -1) {
			log_warn("connect to %s failed, skipping",
			    log_sockaddr((struct sockaddr *)&ga->sa));
			close(fd);
			ga = TAILQ_NEXT(ga, entry);
			continue;
		}

		break;
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

	if (amqp_open(env->amqp) != 0)
		fatalx("amqp_open");
	if (amqp_exchange(env->amqp) != 0)
		fatalx("amqp_exchange");
	if (amqp_queue(env->amqp) != 0)
		fatalx("amqp_queue");

	/* At this point, we are ready to consume messages in some sort of loop
	 */
	while (1) {
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
		//log_info("%d lines", lines);

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

		/* Parse each line, one failure rejects the message */
		for (i = 0; i < lines; i++) {

			/* Ignore completely empty lines */
			if (strlen(line[i]) == 0)
				continue;

			if (env->amqp->flags & AMQP_FLAG_METRIC_IN_MESSAGE) {
				if (graphite_parse(NULL, line[i]) < 0)
					break;
			} else {
				if (graphite_parse(key, line[i]) < 0)
					break;
			}
		}
		if (i < lines) {
			//log_info("rejected");
			amqp_basic_reject(env->amqp->c, AMQP_DEFAULT_CHANNEL,
			    tag, 0); /* <-- Flip this to 1 to re-queue */
		} else {
			//log_info("accepted");
			for (i = 0; i < lines; i++) {
				if (strlen(line[i]) == 0)
					continue;
				if (env->amqp->flags & AMQP_FLAG_METRIC_IN_MESSAGE) {
					send(fd, line[i], strlen(line[i]), 0);
				} else {
					send(fd, key, strlen(key), 0);
					send(fd, " ", 1, 0);
					send(fd, line[i], strlen(line[i]), 0);
				}
				send(fd, "\n", 1, 0);
			}
			amqp_basic_ack(env->amqp->c, AMQP_DEFAULT_CHANNEL,
			    tag, 0);
		}

		free(line);

		free(key);
		free(buf);
	}

	amqp_close(env->amqp);

	return (0);
}
