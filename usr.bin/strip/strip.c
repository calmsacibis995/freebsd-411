/*
 * Copyright (c) 1988, 1993
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
"@(#) Copyright (c) 1988, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)strip.c	8.1 (Berkeley) 6/6/93";
#endif
static const char rcsid[] =
  "$FreeBSD: src/usr.bin/strip/strip.c,v 1.12 1999/08/28 01:05:53 peter Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <a.out.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct exec EXEC;
typedef struct nlist NLIST;

#define	strx	n_un.n_strx

void s_stab __P((const char *, int, EXEC *));
void s_sym __P((const char *, int, EXEC *));
static void usage __P((void));

int xflag = 0;
int err_val = 0;

int
main(argc, argv)
	int argc;
	char *argv[];
{
	register int fd, nb;
	EXEC head;
	void (*sfcn)__P((const char *, int, EXEC *));
	int ch;
	char *fn;

	sfcn = s_sym;
	while ((ch = getopt(argc, argv, "dx")) != -1)
		switch(ch) {
                case 'x':
                        xflag = 1;
                        /*FALLTHROUGH*/
		case 'd':
			sfcn = s_stab;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	while ((fn = *argv++) != NULL) {
		if ((fd = open(fn, O_RDWR)) < 0 ||
		    (nb = read(fd, &head, sizeof(EXEC))) == -1) {
			warn("%s", fn);
			err_val = 1;
			if (fd >= 0 && close(fd)) {
				warn("%s", fn);
				err_val = 1;
			}
			continue;
		}
		if (nb != sizeof(EXEC) || N_BADMAG(head)) {
			warnx("%s: %s", fn, strerror(EFTYPE));
			err_val = 1;
			if (close(fd)) {
				warn("%s", fn);
				err_val = 1;
			}
			continue;
		}
		sfcn(fn, fd, &head);
		if (close(fd)) {
			warn("%s", fn);
			err_val = 1;
		}
	}
	exit(err_val);
}

void
s_sym(fn, fd, ep)
	const char *fn;
	int fd;
	register EXEC *ep;
{
	register off_t fsize;

	/* If no symbols or data/text relocation info, quit. */
	if (!ep->a_syms && !ep->a_trsize && !ep->a_drsize)
		return;

	/*
	 * New file size is the header plus text and data segments.
	 */
	fsize = N_DATOFF(*ep) + ep->a_data;

	/* Set symbol size and relocation info values to 0. */
	ep->a_syms = ep->a_trsize = ep->a_drsize = 0;

	/* Rewrite the header and truncate the file. */
	if (lseek(fd, (off_t)0, SEEK_SET) == -1 ||
	    write(fd, ep, sizeof(EXEC)) != sizeof(EXEC) ||
	    ftruncate(fd, fsize)) {
		warn("%s", fn);
		err_val = 1;
	}
}

void
s_stab(fn, fd, ep)
	const char *fn;
	int fd;
	EXEC *ep;
{
	register int cnt, len;
	register char *nstr, *nstrbase, *p, *strbase;
	register NLIST *sym, *nsym;
	struct stat sb;
	NLIST *symbase;

	/* Quit if no symbols. */
	if (ep->a_syms == 0)
		return;

	/* Stat the file. */
	if (fstat(fd, &sb) < 0) {
		warn("%s", fn);
		err_val = 1;
		return;
	}

	/* Check size. */
	if (sb.st_size > SIZE_T_MAX) {
		warnx("%s: %s", fn, strerror(EFBIG));
		err_val = 1;
		return;
	}

	/* Map the file. */
	if ((ep = (EXEC *)mmap(NULL, (size_t)sb.st_size,
	    PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)0)) == (EXEC *)MAP_FAILED) {
		warn("%s", fn);
		err_val = 1;
		return;
	}

	/*
	 * Initialize old and new symbol pointers.  They both point to the
	 * beginning of the symbol table in memory, since we're deleting
	 * entries.
	 */
	sym = nsym = symbase = (NLIST *)((char *)ep + N_SYMOFF(*ep));

	/*
	 * Allocate space for the new string table, initialize old and
	 * new string pointers.  Handle the extra long at the beginning
	 * of the string table.
	 */
	strbase = (char *)ep + N_STROFF(*ep);
	if ((nstrbase = malloc((size_t)*(u_long *)strbase)) == NULL) {
		warn(NULL);
		err_val = 1;
		munmap((caddr_t)ep, sb.st_size);
		return;
	}
	nstr = nstrbase + sizeof(u_long);

	/*
	 * Read through the symbol table.  For each non-debugging symbol,
	 * copy it and save its string in the new string table.  Keep
	 * track of the number of symbols.
	 */
	for (cnt = ep->a_syms / sizeof(NLIST); cnt--; ++sym)
		if (!(sym->n_type & N_STAB) && sym->strx) {
			*nsym = *sym;
			nsym->strx = nstr - nstrbase;
			p = strbase + sym->strx;
			if (xflag &&
			    (!(sym->n_type & N_EXT) ||
			     (sym->n_type & ~N_EXT) == N_FN ||
			     strcmp(p, "gcc_compiled.") == 0 ||
			     strcmp(p, "gcc2_compiled.") == 0 ||
			     strcmp(p, "___gnu_compiled_c") == 0))
				continue;
			len = strlen(p) + 1;
			bcopy(p, nstr, len);
			nstr += len;
			++nsym;
		}

	/* Fill in new symbol table size. */
	ep->a_syms = (nsym - symbase) * sizeof(NLIST);

	/* Fill in the new size of the string table. */
	*(u_long *)nstrbase = len = nstr - nstrbase;

	/*
	 * Copy the new string table into place.  Nsym should be pointing
	 * at the address past the last symbol entry.
	 */
	bcopy(nstrbase, (void *)nsym, len);

	/* Truncate to the current length. */
	if (ftruncate(fd, (char *)nsym + len - (char *)ep)) {
		warn("%s", fn);
		err_val = 1;
	}
	munmap((caddr_t)ep, (size_t)sb.st_size);
}

static void
usage()
{
	(void)fprintf(stderr, "usage: strip [-dx] file ...\n");
	exit(1);
}
