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

#include <stdio.h>
#include <unistd.h>
#include <err.h>

#include <pcre.h>

#include "rewrite.h"

#define	PATTERN		"(foo)bar"
#define	REPLACEMENT	"$1baz"
#define	TEST		"somefoobarteststring"

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
	int		  c;
	int		  debug = 0;
	int		  noaction = 0;
	const char	 *conffile = REWRITE_CONF_FILE;
	//u_int		  flags = 0;
	//struct passwd	 *pw;

	struct rewrite	 *env = calloc(1, sizeof(struct rewrite));
	SSL_CTX		 *ctx;

	pcre		 *re;
	const char	 *pcre_err;
	int		  pcre_err_offset;
	pcre_extra	 *sd;
	int		  rc;
	int		  ovector[30];
	const char	 *pcre_ss;

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
	if ((re = pcre_compile(PATTERN, 0, &pcre_err,
	    &pcre_err_offset, NULL)) == NULL)
		fprintf(stderr, "Compilation failed: %s, at %d\n", pcre_err,
		    pcre_err_offset);
	if ((sd = pcre_study(re,
	    PCRE_STUDY_EXTRA_NEEDED|PCRE_STUDY_JIT_COMPILE,
	    &pcre_err)) == NULL)
		fprintf(stderr, "Study failed: %s\n", pcre_err);

	if ((rc = pcre_exec(re, sd, TEST, strlen(TEST), 0, 0, ovector,
	    30)) < -1)
		fprintf(stderr, "Match failed: %d\n", rc);
	if (rc == 0)
		fprintf(stderr, "ovector too small\n");
	if (rc > 0) {
		pcre_get_substring(TEST, ovector, rc, 0, &pcre_ss);
		fprintf(stderr, "Substring #0 = %s\n", pcre_ss);
		pcre_free(pcre_ss);
	}

	event_base_dispatch(env->base);

	pcre_free_study(sd);
	pcre_free(re);

	SSL_CTX_free(ctx);

	return (0);
}
