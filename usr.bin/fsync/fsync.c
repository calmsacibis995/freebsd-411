/*-
 * Copyright (c) 2000 Paul Saab <ps@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
static const char rcsid[] =
  "$FreeBSD: src/usr.bin/fsync/fsync.c,v 1.1.2.2 2000/07/25 07:48:25 ps Exp $";
#endif /* not lint */

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>

void	usage(void);

int
main(int argc, char *argv[])
{
	int fd;
	int i;
	
	if (argc < 2)
		usage();
	
	for (i = 1; i < argc; ++i) {
		if ((fd = open(argv[i], O_RDONLY)) < 0)
			err(1, "open %s", argv[i]);

		if (fsync(fd) != 0)
			err(1, "fsync %s", argv[1]);
		close(fd);
	}
	return(0);
}

void
usage()
{
	fprintf(stderr, "usage: fsync file ...\n");
	exit(EX_USAGE);
}
