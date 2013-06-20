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

#include <stdlib.h>
#include <string.h>

#include "common.h"

/* Given a string containing sigil-prefixed numbers, calls out for that
 * nth string and merges it with the string into the buffer at most
 * buffersize bytes long including a null terminating byte. Returns the
 * length of the result or -1 on error.
 */
int
merge_nth_string(char *string, char sigil, char *buffer, int buffersize,
    char *(*nth_string)(int, void *), void *arg)
{
	int		 total = 0, len;
	char		*ptr = buffer, *p1, *p2, *num;
	long long	 ss;
	size_t		 size;

	if (!nth_string)
		return (-1);

	/* Allow for the extra storage for the trailing null */
	if (buffersize)
		buffersize--;

	/* Loop through the string looking for the chosen sigil */
	p1 = p2 = string;
	while ((p1 = strchr(p1, sigil)) != NULL) {
		/* Copy any characters in the replacement before the sigil */
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
		/* Call out for the substring */
		if ((num = nth_string(ss, arg)) == NULL)
			return (-1);
		total += strlen(num);
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

	/* Copy any characters in the string after the last substring */
	if (ptr) {
		len = MIN(buffersize, (int)strlen(p2));
		strncpy(ptr, p2, len);
		ptr += len;
		buffersize -= len;
	}

	return (total);
}
