/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Paul Borman at Krystal Technologies.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)ansi.c	8.1 (Berkeley) 6/27/93";
#endif /* LIBC_SCCS and not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/lib/libc/locale/ansi.c,v 1.3.6.1 2003/04/05 08:28:24 tjr Exp $");

#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <stddef.h>
#include <rune.h>

int
mblen(s, n)
	const char *s;
	size_t n;
{
	char const *e;

	if (s == 0 || *s == 0)
		return (0);	/* No support for state dependent encodings. */

	if (sgetrune(s, n, &e) == _INVALID_RUNE)
		return (s - e);
	return (e - s);
}

int
mbtowc(pwc, s, n)
	wchar_t *pwc;
	const char *s;
	size_t n;
{
	char const *e;
	rune_t r;

	if (s == 0 || *s == 0)
		return (0);	/* No support for state dependent encodings. */

	if ((r = sgetrune(s, n, &e)) == _INVALID_RUNE)
		return (s - e);
	if (pwc)
		*pwc = r;
	return (e - s);
}

int
wctomb(s, wchar)
	char *s;
	wchar_t wchar;
{
	char *e;

	if (s == 0)
		return (0);	/* No support for state dependent encodings. */

	if (wchar == 0) {
		*s = 0;
		return (1);
	}

	sputrune(wchar, s, MB_CUR_MAX, &e);
	return (e ? e - s : -1);
}

size_t
mbstowcs(pwcs, s, n)
	wchar_t *pwcs;
	const char *s;
	size_t n;
{
	const char *e;
	int cnt;
	rune_t r;

	if (s == NULL) {
		errno = EINVAL;
		return (-1);
	}

	if (pwcs == NULL) {
		/* Convert and count only, do not store. */
		cnt = 0;
		while ((r = sgetrune(s, MB_LEN_MAX, &e)) != _INVALID_RUNE &&
		    r != 0) {
			s = e;
			cnt++;
		}
		if (r == _INVALID_RUNE) {
			errno = EILSEQ;
			return (-1);
		}
		return (cnt);
	}

	/* Convert, store and count characters. */
	cnt = 0;
	while (n-- > 0) {
		*pwcs = sgetrune(s, MB_LEN_MAX, &e);
		if (*pwcs == _INVALID_RUNE) {
			errno = EILSEQ;
			return (-1);
		}
		if (*pwcs++ == L'\0')
			break;
		s = e;
		++cnt;
	}

	return (cnt);
}

size_t
wcstombs(s, pwcs, n)
	char *s;
	const wchar_t *pwcs;
	size_t n;
{
	char buf[MB_LEN_MAX];
	char *e;
	int cnt, nb;

	if (pwcs == NULL || n > INT_MAX) {
		errno = EINVAL;
		return (-1);
	}

	cnt = 0;

	if (s == NULL) {
		/* Convert and count only, do not store. */
		while (*pwcs != L'\0') {
			if (sputrune(*pwcs++, buf, MB_LEN_MAX, &e) == 0) {
				errno = EILSEQ;
				return (-1);
			}
			cnt += e - buf;
		}
		return (cnt);
	}

	/* Convert, store and count characters. */
	nb = n;
	while (nb > 0) {
		if (*pwcs == L'\0') {
			*s = '\0';
			break;
		}
		if (sputrune(*pwcs++, s, nb, &e) == 0) {
			errno = EILSEQ;
			return (-1);
		}
		if (e == NULL)		/* too long */
			return (cnt);
		cnt += e - s;
		nb -= e - s;
		s = e;
	}

	return (cnt);
}
