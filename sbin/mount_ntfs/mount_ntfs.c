/*
 * Copyright (c) 1994 Christopher G. Demetriou
 * Copyright (c) 1999 Semen Ustimenko
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 * $FreeBSD: src/sbin/mount_ntfs/mount_ntfs.c,v 1.3.2.2 2001/10/12 22:08:43 semenu Exp $
 *
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#define NTFS
#include <sys/mount.h>
#include <sys/stat.h>
#include <ntfs/ntfsmount.h>
#include <ctype.h>
#include <err.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <libutil.h>

#include "mntopts.h"

static struct mntopt mopts[] = {
	MOPT_STDOPTS,
	{ NULL }
};

static gid_t	a_gid __P((char *));
static uid_t	a_uid __P((char *));
static mode_t	a_mask __P((char *));
static void	usage __P((void)) __dead2;

static void     load_u2wtable __P((struct ntfs_args *, char *));

int
main(argc, argv)
	int argc;
	char **argv;
{
	struct ntfs_args args;
	struct stat sb;
	int c, mntflags, set_gid, set_uid, set_mask, error;
	char *dev, *dir, mntpath[MAXPATHLEN];
#if __FreeBSD_version >= 300000
	struct vfsconf vfc;
#else
	struct vfsconf *vfc;
#endif

	mntflags = set_gid = set_uid = set_mask = 0;
	(void)memset(&args, '\0', sizeof(args));

	while ((c = getopt(argc, argv, "aiu:g:m:o:W:")) !=  -1) {
		switch (c) {
		case 'u':
			args.uid = a_uid(optarg);
			set_uid = 1;
			break;
		case 'g':
			args.gid = a_gid(optarg);
			set_gid = 1;
			break;
		case 'm':
			args.mode = a_mask(optarg);
			set_mask = 1;
			break;
		case 'i':
			args.flag |= NTFS_MFLAG_CASEINS;
			break;
		case 'a':
			args.flag |= NTFS_MFLAG_ALLNAMES;
			break;
		case 'o':
			getmntopts(optarg, mopts, &mntflags, 0);
			break;
		case 'W':
			load_u2wtable(&args, optarg);
			args.flag |= NTFSMNT_U2WTABLE;
			break;
		case '?':
		default:
			usage();
			break;
		}
	}

	if (optind + 2 != argc)
		usage();

	dev = argv[optind];
	dir = argv[optind + 1];

	/*
	 * Resolve the mountpoint with realpath(3) and remove unnecessary 
	 * slashes from the devicename if there are any.
	 */
	(void)checkpath(dir, mntpath);
	(void)rmslashes(dev, dev);

	args.fspec = dev;
	args.export.ex_root = 65534;	/* unchecked anyway on DOS fs */
	if (mntflags & MNT_RDONLY)
		args.export.ex_flags = MNT_EXRDONLY;
	else
		args.export.ex_flags = 0;
	if (!set_gid || !set_uid || !set_mask) {
		if (stat(mntpath, &sb) == -1)
			err(EX_OSERR, "stat %s", mntpath);

		if (!set_uid)
			args.uid = sb.st_uid;
		if (!set_gid)
			args.gid = sb.st_gid;
		if (!set_mask)
			args.mode = sb.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
	}

#if __FreeBSD_version >= 300000
	error = getvfsbyname("ntfs", &vfc);
	if(error && vfsisloadable("ntfs")) {
		if(vfsload("ntfs"))
#else
	vfc = getvfsbyname("ntfs");
	if(!vfc && vfsisloadable("ntfs")) {
		if(vfsload("ntfs"))
#endif
			err(EX_OSERR, "vfsload(ntfs)");
		endvfsent();	/* clear cache */
#if __FreeBSD_version >= 300000
		error = getvfsbyname("ntfs", &vfc);
#else
		vfc = getvfsbyname("ntfs");
#endif
	}
#if __FreeBSD_version >= 300000
	if (error)
#else
	if (!vfc)
#endif
		errx(EX_OSERR, "ntfs filesystem is not available");

#if __FreeBSD_version >= 300000
	if (mount(vfc.vfc_name, mntpath, mntflags, &args) < 0)
#else
	if (mount(vfc->vfc_index, mntpath, mntflags, &args) < 0)
#endif
		err(EX_OSERR, "%s", dev);

	exit (0);
}

gid_t
a_gid(s)
	char *s;
{
	struct group *gr;
	char *gname;
	gid_t gid;

	if ((gr = getgrnam(s)) != NULL)
		gid = gr->gr_gid;
	else {
		for (gname = s; *s && isdigit(*s); ++s);
		if (!*s)
			gid = atoi(gname);
		else
			errx(EX_NOUSER, "unknown group id: %s", gname);
	}
	return (gid);
}

uid_t
a_uid(s)
	char *s;
{
	struct passwd *pw;
	char *uname;
	uid_t uid;

	if ((pw = getpwnam(s)) != NULL)
		uid = pw->pw_uid;
	else {
		for (uname = s; *s && isdigit(*s); ++s);
		if (!*s)
			uid = atoi(uname);
		else
			errx(EX_NOUSER, "unknown user id: %s", uname);
	}
	return (uid);
}

mode_t
a_mask(s)
	char *s;
{
	int done, rv=0;
	char *ep;

	done = 0;
	if (*s >= '0' && *s <= '7') {
		done = 1;
		rv = strtol(optarg, &ep, 8);
	}
	if (!done || rv < 0 || *ep)
		errx(EX_USAGE, "invalid file mode: %s", s);
	return (rv);
}

void
usage()
{
	fprintf(stderr, "usage: mount_ntfs [-a] [-i] [-u user] [-g group] [-m mask] [-W u2wtable] bdev dir\n");
	exit(EX_USAGE);
}

void
load_u2wtable (pargs, name)
	struct ntfs_args *pargs;
	char *name;
{
	FILE *f;
	int i, j, code[8];
	size_t line = 0;
	char buf[128];
	char *fn, *s, *p;

	if (*name == '/')
		fn = name;
	else {
		snprintf(buf, sizeof(buf), "/usr/libdata/msdosfs/%s", name);
		buf[127] = '\0';
		fn = buf;
	}
	if ((f = fopen(fn, "r")) == NULL)
		err(EX_NOINPUT, "%s", fn);
	p = NULL;
	for (i = 0; i < 16; i++) {
		do {
			if (p != NULL) free(p);
			if ((p = s = fparseln(f, NULL, &line, NULL, 0)) == NULL)
				errx(EX_DATAERR, "can't read u2w table row %d near line %d", i, line);
			while (isspace((unsigned char)*s))
				s++;
		} while (*s == '\0');
		if (sscanf(s, "%i%i%i%i%i%i%i%i",
code, code + 1, code + 2, code + 3, code + 4, code + 5, code + 6, code + 7) != 8)
			errx(EX_DATAERR, "u2w table: missing item(s) in row %d, line %d", i, line);
		for (j = 0; j < 8; j++)
			pargs->u2w[i * 8 + j] = code[j];
	}
	for (i = 0; i < 16; i++) {
		do {
			free(p);
			if ((p = s = fparseln(f, NULL, &line, NULL, 0)) == NULL)
				errx(EX_DATAERR, "can't read d2u table row %d near line %d", i, line);
			while (isspace((unsigned char)*s))
				s++;
		} while (*s == '\0');
		if (sscanf(s, "%i%i%i%i%i%i%i%i",
code, code + 1, code + 2, code + 3, code + 4, code + 5, code + 6, code + 7) != 8)
			errx(EX_DATAERR, "d2u table: missing item(s) in row %d, line %d", i, line);
		for (j = 0; j < 8; j++)
			/* pargs->d2u[i * 8 + j] = code[j] */;
	}
	for (i = 0; i < 16; i++) {
		do {
			free(p);
			if ((p = s = fparseln(f, NULL, &line, NULL, 0)) == NULL)
				errx(EX_DATAERR, "can't read u2d table row %d near line %d", i, line);
			while (isspace((unsigned char)*s))
				s++;
		} while (*s == '\0');
		if (sscanf(s, "%i%i%i%i%i%i%i%i",
code, code + 1, code + 2, code + 3, code + 4, code + 5, code + 6, code + 7) != 8)
			errx(EX_DATAERR, "u2d table: missing item(s) in row %d, line %d", i, line);
		for (j = 0; j < 8; j++)
			/* pargs->u2d[i * 8 + j] = code[j] */;
	}
	free(p);
	fclose(f);
}

