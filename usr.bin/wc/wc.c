/*
 * Copyright (c) 1980, 1987, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1980, 1987, 1991, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#if 0
#ifndef lint
static char sccsid[] = "@(#)wc.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/usr.bin/wc/wc.c,v 1.11.2.1 2002/08/25 02:47:04 tjr Exp $");

#include <sys/param.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

u_quad_t tlinect, twordct, tcharct;
int doline, doword, dochar, domulti;

static int	cnt(const char *);
static void	usage(void);

int
main(argc, argv)
	int argc;
	char *argv[];
{
	int ch, errors, total;

	(void) setlocale(LC_CTYPE, "");

	while ((ch = getopt(argc, argv, "clmw")) != -1)
		switch((char)ch) {
		case 'l':
			doline = 1;
			break;
		case 'w':
			doword = 1;
			break;
		case 'c':
			dochar = 1;
			domulti = 0;
			break;
		case 'm':
			domulti = 1;
			dochar = 0;
			break;
		case '?':
		default:
			usage();
		}
	argv += optind;
	argc -= optind;

	/* Wc's flags are on by default. */
	if (doline + doword + dochar + domulti == 0)
		doline = doword = dochar = 1;

	errors = 0;
	total = 0;
	if (!*argv) {
		if (cnt((char *)NULL) != 0)
			++errors;
		else
			(void)printf("\n");
	}
	else do {
		if (cnt(*argv) != 0)
			++errors;
		else
			(void)printf(" %s\n", *argv);
		++total;
	} while(*++argv);

	if (total > 1) {
		if (doline)
			(void)printf(" %7qu", tlinect);
		if (doword)
			(void)printf(" %7qu", twordct);
		if (dochar || domulti)
			(void)printf(" %7qu", tcharct);
		(void)printf(" total\n");
	}
	exit(errors == 0 ? 0 : 1);
}

static int
cnt(file)
	const char *file;
{
	struct stat sb;
	u_quad_t linect, wordct, charct;
	ssize_t nread;
	int clen, fd, len, warned;
	short gotsp;
	u_char *p;
	u_char buf[MAXBSIZE];
	wchar_t wch;

	linect = wordct = charct = 0;
	if (file == NULL) {
		file = "stdin";
		fd = STDIN_FILENO;
	} else {
		if ((fd = open(file, O_RDONLY, 0)) < 0) {
			warn("%s: open", file);
			return (1);
		}
		if (doword || (domulti && MB_CUR_MAX != 1))
			goto word;
		/*
		 * Line counting is split out because it's a lot faster to get
		 * lines than to get words, since the word count requires some
		 * logic.
		 */
		if (doline) {
			while ((len = read(fd, buf, MAXBSIZE))) {
				if (len == -1) {
					warn("%s: read", file);
					(void)close(fd);
					return (1);
				}
				charct += len;
				for (p = buf; len--; ++p)
					if (*p == '\n')
						++linect;
			}
			tlinect += linect;
			(void)printf(" %7qu", linect);
			if (dochar) {
				tcharct += charct;
				(void)printf(" %7qu", charct);
			}
			(void)close(fd);
			return (0);
		}
		/*
		 * If all we need is the number of characters and it's a
		 * regular file, just stat the puppy.
		 */
		if (dochar || domulti) {
			if (fstat(fd, &sb)) {
				warn("%s: fstat", file);
				(void)close(fd);
				return (1);
			}
			if (S_ISREG(sb.st_mode)) {
				(void)printf(" %7lld", (long long)sb.st_size);
				tcharct += sb.st_size;
				(void)close(fd);
				return (0);
			}
		}
	}

	/* Do it the hard way... */
word:	gotsp = 1;
	len = 0;
	warned = 0;
	while ((nread = read(fd, buf + len, MAXBSIZE - len)) != 0) {
		if (nread == -1) {
			warn("%s: read", file);
			(void)close(fd);
			return (1);
		}
		len += nread;
		p = buf;
		while (len > 0) {
			if (!domulti || MB_CUR_MAX == 1) {
				clen = 1;
				wch = (unsigned char)*p;
			} else if ((clen = mbtowc(&wch, p, len)) <= 0) {
				if (len > MB_CUR_MAX) {
					clen = 1;
					wch = (unsigned char)*p;
					if (!warned) {
						errno = EILSEQ;
						warn("%s", file);
						warned = 1;
					}
				} else {
					memmove(buf, p, len);
					break;
				}
			}
			charct++;
			len -= clen;
			p += clen;
			if (wch == L'\n')
				++linect;
			if (isspace(wch))
				gotsp = 1;
			else if (gotsp) {
				gotsp = 0;
				++wordct;
			}
		}
	}
	if (doline) {
		tlinect += linect;
		(void)printf(" %7qu", linect);
	}
	if (doword) {
		twordct += wordct;
		(void)printf(" %7qu", wordct);
	}
	if (dochar || domulti) {
		tcharct += charct;
		(void)printf(" %7qu", charct);
	}
	(void)close(fd);
	return (0);
}

static void
usage()
{
	(void)fprintf(stderr, "usage: wc [-clmw] [file ...]\n");
	exit(1);
}
