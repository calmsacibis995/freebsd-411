/*-
 * Copyright (c) 1991, 1993
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
"@(#) Copyright (c) 1991, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static const char sccsid[] = "@(#)dmesg.c	8.1 (Berkeley) 6/5/93";
#endif
static const char rcsid[] =
  "$FreeBSD: src/sbin/dmesg/dmesg.c,v 1.11.2.4 2004/02/29 20:39:40 iedowse Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/msgbuf.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vis.h>
#include <sys/syslog.h>

struct nlist nl[] = {
#define	X_MSGBUF	0
	{ "_msgbufp" },
	{ NULL },
};

void usage __P((void));

#define	KREAD(addr, var) \
	kvm_read(kd, addr, &var, sizeof(var)) != sizeof(var)

int
main(argc, argv)
	int argc;
	char *argv[];
{
	struct msgbuf *bufp, cur;
	char *bp, *ep, *memf, *nextp, *nlistf, *p, *q, *visbp;
	kvm_t *kd;
	size_t buflen, bufpos;
	long pri;
	int all, ch;

	all = 0;
	(void) setlocale(LC_CTYPE, "");
	memf = nlistf = NULL;
	while ((ch = getopt(argc, argv, "aM:N:")) != -1)
		switch(ch) {
		case 'a':
			all++;
			break;
		case 'M':
			memf = optarg;
			break;
		case 'N':
			nlistf = optarg;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (memf == NULL && nlistf == NULL) {
		/*
		 * Running kernel.  Use sysctl.  This gives an unwrapped
		 * buffer as a side effect.
		 */
		if (sysctlbyname("kern.msgbuf", NULL, &buflen, NULL, 0) == -1)
			err(1, "sysctl kern.msgbuf");
		if ((bp = malloc(buflen + 2)) == NULL)
			errx(1, "malloc failed");
		if (sysctlbyname("kern.msgbuf", bp, &buflen, NULL, 0) == -1)
			err(1, "sysctl kern.msgbuf");
	} else {
		/* Read in kernel message buffer and do sanity checks. */
		kd = kvm_open(nlistf, memf, NULL, O_RDONLY, "dmesg");
		if (kd == NULL)
			exit (1);
		if (kvm_nlist(kd, nl) == -1)
			errx(1, "kvm_nlist: %s", kvm_geterr(kd));
		if (nl[X_MSGBUF].n_type == 0)
			errx(1, "%s: msgbufp not found",
			    nlistf ? nlistf : "namelist");
		if (KREAD(nl[X_MSGBUF].n_value, bufp) || KREAD((long)bufp, cur))
			errx(1, "kvm_read: %s", kvm_geterr(kd));
		if (cur.msg_magic != MSG_MAGIC)
			errx(1, "kernel message buffer has different magic "
			    "number");
		if ((bp = malloc(cur.msg_size + 2)) == NULL)
			errx(1, "malloc failed");

		/* Unwrap the circular buffer to start from the oldest data. */
		bufpos = cur.msg_bufx;
		if (bufpos >= buflen)
			bufpos = 0;
		if (kvm_read(kd, (long)&cur.msg_ptr[bufpos], bp,
		    cur.msg_size - bufpos) != cur.msg_size - bufpos)
			errx(1, "kvm_read: %s", kvm_geterr(kd));
		if (bufpos != 0 && kvm_read(kd, (long)cur.msg_ptr,
		    &bp[cur.msg_size - bufpos], bufpos) != bufpos)
			errx(1, "kvm_read: %s", kvm_geterr(kd));
		kvm_close(kd);
		buflen = cur.msg_size;
	}

	/*
	 * Ensure that the buffer ends with a newline and a \0 to avoid
	 * complications below.  We left space above.
	 */
	if (buflen == 0 || bp[buflen - 1] != '\n')
		bp[buflen++] = '\n';
	bp[buflen] = '\0';

	if ((visbp = malloc(4 * buflen + 1)) == NULL)
		errx(1, "malloc failed");

	/*
	 * The message buffer is circular, but has been unwrapped so that
	 * the oldest data comes first.  The data will be preceded by \0's
	 * if the message buffer was not full.
	 */
	p = bp;
	ep = &bp[buflen];
	if (*p == '\0') {
		/* Strip leading \0's */
		while (*p == '\0')
			p++;
	} else if (!all) {
		/* Skip the first line, since it is probably incomplete. */
		p = memchr(p, '\n', ep - p) + 1;
	}
	for (; p < ep; p = nextp) {
		nextp = memchr(p, '\n', ep - p) + 1;

		/* Skip ^<[0-9]+> syslog sequences. */
		if (*p == '<') {
			errno = 0;
			pri = strtol(p + 1, &q, 10);
			if (*q == '>' && pri >= 0 && pri < INT_MAX &&
			    errno == 0) {
				if (LOG_FAC(pri) != LOG_KERN && !all)
					continue;
				p = q + 1;
			}
		}

		(void)strvisx(visbp, p, nextp - p, 0);
		(void)printf("%s", visbp);
	}
	exit(0);
}

void
usage()
{
	(void)fprintf(stderr, "usage: dmesg [-a] [-M core] [-N system]\n");
	exit(1);
}
