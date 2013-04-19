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

#include <pcre.h>

#include "rewrite.h"

struct rewrite_rule {
	TAILQ_ENTRY(rewrite_rule)	 entry;
	char				*pattern;
	char				*replacement;
	pcre				*re;
	pcre_extra			*sd;
};

TAILQ_HEAD(rewrite_rules, rewrite_rule)	 rewrite_rules = TAILQ_HEAD_INITIALIZER(rewrite_rules);

__dead void	 usage(void);
int		 pcre_replace(char *, char *, int *, int, char *, int);

__dead void
usage(void)
{
	extern char	*__progname;

	fprintf(stderr, "usage: %s [-dnv] [-f file]\n", __progname);
	exit(1);
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

int
main(int argc, char *argv[])
{
	int			 c;
	int			 debug = 0;
	int			 noaction = 0;
	const char		*conffile = REWRITE_CONF_FILE;
	//u_int			 flags = 0;
	//struct passwd		*pw;
	struct rewrite		*env = calloc(1, sizeof(struct rewrite));
	SSL_CTX			*ctx;
	struct rewrite_rule	*rule, *nrule;
	const char		*pcre_err;
	int			 pcre_err_offset;
	int			 rc;
	int			 ovector[30];
	int			 size;
	char			*new;

	char			*subject;

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

	//if ((env = parse_config(conffile, 0)) == NULL)
	//	exit(1);

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

	env->base = event_base_new();
	if (!env->base)
		fatalx("event_base_new");

	/* FIXME Add code here! */
	rule = calloc(1, sizeof(struct rewrite_rule));
	rule->pattern = strdup("_example_com\\.");
	rule->replacement = strdup(".");
	TAILQ_INSERT_TAIL(&rewrite_rules, rule, entry);

	rule = calloc(1, sizeof(struct rewrite_rule));
	rule->pattern = strdup("\\.cpu\\.idle\\.value$");
	rule->replacement = strdup(".idle");
	TAILQ_INSERT_TAIL(&rewrite_rules, rule, entry);
	
	/* Compile the rules */
	for (rule = TAILQ_FIRST(&rewrite_rules); rule; ) {
		if ((rule->re = pcre_compile(rule->pattern, 0, &pcre_err,
		    &pcre_err_offset, NULL)) == NULL) {
			fprintf(stderr, "Compilation failed: %s, at %d\n",
			    pcre_err, pcre_err_offset);
			nrule = TAILQ_NEXT(rule, entry);
			TAILQ_REMOVE(&rewrite_rules, rule, entry);
			free(rule->pattern);
			free(rule->replacement);
			free(rule);
			rule = nrule;
			continue;
		}
		if ((rule->sd = pcre_study(rule->re,
		    PCRE_STUDY_EXTRA_NEEDED|PCRE_STUDY_JIT_COMPILE,
		    &pcre_err)) == NULL) {
			fprintf(stderr, "Study failed: %s\n", pcre_err);
			nrule = TAILQ_NEXT(rule, entry);
			TAILQ_REMOVE(&rewrite_rules, rule, entry);
			pcre_free(rule->re);
			free(rule->pattern);
			free(rule->replacement);
			free(rule);
			rule = nrule;
			continue;
		}

		rule = TAILQ_NEXT(rule, entry);
	}

	/* A typical verbose collectd metric converted to graphite */
	subject = strdup("server_example_com.cpu.0.cpu.idle.value");

	for (rule = TAILQ_FIRST(&rewrite_rules); rule;
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

	event_base_dispatch(env->base);

	while (!TAILQ_EMPTY(&rewrite_rules)) {
		rule = TAILQ_FIRST(&rewrite_rules);
		TAILQ_REMOVE(&rewrite_rules, rule, entry);
		pcre_free_study(rule->sd);
		pcre_free(rule->re);
		free(rule->pattern);
		free(rule->replacement);
		free(rule);
	}

	SSL_CTX_free(ctx);

	return (0);
}
