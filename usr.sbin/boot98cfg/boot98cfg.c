/*
 * Copyright (c) KATO Takenori, 2000.
 * 
 * All rights reserved.  Unpublished rights reserved under the copyright
 * laws of Japan.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/usr.sbin/boot98cfg/boot98cfg.c,v 1.1.2.2 2001/07/30 10:22:58 dd Exp $
 */

/*
 * Copyright (c) 1999 Robert Nordier
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	IPLSIZE			512		/* IPL size */
#define	BOOTMENUSIZE		7168		/* Max HDD boot menu size */
#define	BOOTMENUOFF		0x400

static	char *mkrdev(char *);
static	void usage(void);

u_char	boot0buf[0x2000];
char	ipl[IPLSIZE];
char	menu[BOOTMENUSIZE];

/*
 * Produce a device path for a "canonical" name, where appropriate.
 */
static char *
mkrdev(char *fname)
{
    char buf[MAXPATHLEN];
    struct stat sb;
    char *s;

    s = (char *)fname;
    if (!strchr(fname, '/')) {
        snprintf(buf, sizeof(buf), "%sr%s", _PATH_DEV, fname);
        if (stat(buf, &sb))
            snprintf(buf, sizeof(buf), "%s%s", _PATH_DEV, fname);
        if (!(s = strdup(buf)))
            err(1, NULL);
    }
    return s;
}

static void
usage(void)
{
	fprintf(stderr,
	    "boot98cfg [-B][-i boot0][-m boot0.5][-s secsize][-v version]\n"
	    "          [-f ipl.bak][-F menu.bak] disk\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	char	*endptr;
	const	char *iplpath = "/boot/boot0", *menupath = "/boot/boot0.5";
	char	*iplbakpath = NULL, *menubakpath = NULL;
	char	*disk;
	int	B_flag = 0;
	int	c;
	int	fd, fd1;
	int	n;
	int	secsize = 512;
	int	v_flag = 0, version;

	while ((c = getopt(argc, argv, "BF:f:i:m:s:v:")) != -1) {
		switch (c) {
		case 'B':
			B_flag = 1;
			break;
		case 'F':
			menubakpath = optarg;
			break;
		case 'f':
			iplbakpath = optarg;
			break;
		case 'i':
			iplpath = optarg;
			break;
		case 'm':
			menupath = optarg;
			break;
		case 's':
			secsize = strtol(optarg, &endptr, 0);
			if (errno || *optarg == NULL || *endptr)
				errx(1, "%s: Bad argument to -s option",
				    optarg);
			switch (secsize) {
			case 256:
			case 512:
			case 1024:
			case 2048:
				break;
			default:
				errx(1, "%s: unsupported sector size", optarg);
				break;
			}
			break;
		case 'v':
			v_flag = 1;
			version = strtol(optarg, &endptr, 0);
			if (errno || *optarg == NULL || *endptr ||
			    version < 0 || version > 255)
				errx(1, "%s: Bad argument to -s option",
				    optarg);
			break;
		default:
			usage();
			/* NOTREACHED */
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1)
		usage();
	disk = mkrdev(*argv);

	/* Read IPL, partition table and HDD boot menu. */
	fd = open(disk, O_RDWR);
	if (fd < 0)
		err(1, "%s", disk);
	n = read(fd, boot0buf, 0x2000);
	if (n != 0x2000)
		errx(1, "%s: short read", disk);
	if (!B_flag && !v_flag)
		close(fd);

	if (iplbakpath != NULL) {
		fd1 = open(iplbakpath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (fd1 < 0)
			err(1, "%s", iplbakpath);
		n = write(fd1, boot0buf, IPLSIZE);
		if (n == -1)
			err(1, "%s", iplbakpath);
		if (n != IPLSIZE)
			errx(1, "%s: short write", iplbakpath);
		close(fd1);
	}

	if (menubakpath != NULL) {
		fd1 = open(menubakpath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (fd1 < 0)
			err(1, "%s", menubakpath);
		n = write(fd1, boot0buf + BOOTMENUOFF, BOOTMENUSIZE);
		if (n == -1)
			err(1, "%s", menubakpath);
		if (n != BOOTMENUSIZE)
			errx(1, "%s: short write", menubakpath);
		close(fd1);
	}

	if (B_flag) {
		/* Read IPL (boot0). */
		fd1 = open(iplpath, O_RDONLY);
		if (fd1 < 0)
			err(1, "%s", disk);
		n = read(fd1, ipl, IPLSIZE);
		if (n < 0)
			err(1, "%s", iplpath);
		if (n != IPLSIZE)
			errx(1, "%s: invalid file", iplpath);
		close(fd1);

		/* Read HDD boot menu (boot0.5). */
		fd1 = open(menupath, O_RDONLY);
		if (fd1 < 0)
			err(1, "%s", disk);
		n = read(fd1, menu, BOOTMENUSIZE);
		if (n < 0)
			err(1, "%s", menupath);
		if (n != BOOTMENUSIZE)
			errx(1, "%s: invalid file", menupath);
		close(fd1);

		memcpy(boot0buf, ipl, IPLSIZE);
		memcpy(boot0buf + BOOTMENUOFF, menu, BOOTMENUSIZE);
	}

	/* Set version number field. */
	if (v_flag)
		*(boot0buf + secsize - 4) = (u_char)version;

	if (B_flag || v_flag) {
		if (lseek(fd, 0, SEEK_SET) == -1 ||
		    (n = write(fd, boot0buf, 0x2000)) < 0 ||
		    close(fd))
			err(1, "%s", disk);
		if (n != 0x2000)
			errx(1, "%s: short write", disk);
	}
	
	return 0;
}
