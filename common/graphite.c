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

#include <string.h>

#include "common.h"

/* For use with strspn(3) */
#define	DIGITS		"0123456789"
#define	LOWER		"abcdefghijklmnopqrstuvwxyz"
#define	UPPER		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define	SPACE		" "
#define	UNDERSCORE	"_"
#define	MINUS		"-"
#define	PERIOD		"."

/* Returns < 0 on error, >= 0 on success with > 0 indicating length of key
 * if it wasn't passed separately
 */
int
graphite_parse(char *key, char *line)
{
	char	*ptr = line;
	size_t	 len, klen = 0;

	if (key != NULL) {
		/* If the key came from a message routing key, the whole thing
		 * should be valid
		 */
		if (strspn(key, LOWER UPPER DIGITS UNDERSCORE MINUS PERIOD) !=
		    strlen(key))
			goto bad;
	} else {
		/* Scan the metric key, make a note of how long it is */
		if ((len = strspn(ptr,
		    LOWER UPPER DIGITS UNDERSCORE MINUS PERIOD)) == 0)
			goto bad;
		ptr += klen = len;

		/* Scan the spaces after the metric key */
		if ((len = strspn(ptr, SPACE)) == 0)
			goto bad;
		/* Remove any duplicate spaces */
		if (len > 1)
			memmove(ptr + 1, ptr + len, strlen(ptr + len) + 1);
		ptr++;
	}

	/* At this point, we're just concerned with the value and timestamp */

	/* Scan the metric value */
	if ((len = strspn(ptr, DIGITS MINUS PERIOD)) == 0)
		goto bad;
	ptr += len;

	/* Scan the spaces after the metric value */
	if ((len = strspn(ptr, SPACE)) == 0)
		goto bad;
	/* Remove any duplicate spaces */
	if (len > 1)
		memmove(ptr + 1, ptr + len, strlen(ptr + len) + 1);
	ptr++;

	/* Scan the metric timestamp */
	if ((len = strspn(ptr, DIGITS)) == 0)
		goto bad;
	ptr += len;

	/* If we reached the end and yet don't have a null, there's extra
	 * junk on the line so get rid of it
	 */
	if (*ptr != '\0') {
		log_warnx("junk at EOL: \"%s\"", ptr);
		*ptr = '\0';
	}

	return (klen);
bad:
	log_warnx("Something wrong");
	return (-1);
}
