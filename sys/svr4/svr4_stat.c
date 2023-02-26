/*
 * Copyright (c) 1998 Mark Newton
 * Copyright (c) 1994 Christos Zoulas
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
 * 3. The name of the author may not be used to endorse or promote products
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
 * $FreeBSD: src/sys/svr4/svr4_stat.c,v 1.6 1999/12/08 12:00:48 newton Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/unistd.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/sysproto.h>

#include <vm/vm.h>

#include <netinet/in.h>

#include <svr4/svr4.h>
#include <svr4/svr4_types.h>
#include <svr4/svr4_signal.h>
#include <svr4/svr4_proto.h>
#include <svr4/svr4_util.h>
#include <svr4/svr4_stat.h>
#include <svr4/svr4_ustat.h>
#include <svr4/svr4_utsname.h>
#include <svr4/svr4_systeminfo.h>
#include <svr4/svr4_socket.h>
#include <svr4/svr4_time.h>
#if defined(NOTYET)
#include "svr4_fuser.h"
#endif

#ifdef sparc
/* 
 * Solaris-2.4 on the sparc has the old stat call using the new
 * stat data structure...
 */
# define SVR4_NO_OSTAT
#endif

struct svr4_ustat_args {
	svr4_dev_t		dev;
	struct svr4_ustat * name;
};

static void bsd_to_svr4_xstat __P((struct stat *, struct svr4_xstat *));
static void bsd_to_svr4_stat64 __P((struct stat *, struct svr4_stat64 *));
int svr4_ustat __P((struct proc *, struct svr4_ustat_args *));
static int svr4_to_bsd_pathconf __P((int));

/*
 * SVR4 uses named pipes as named sockets, so we tell programs
 * that sockets are named pipes with mode 0
 */
#define BSD_TO_SVR4_MODE(mode) (S_ISSOCK(mode) ? S_IFIFO : (mode))


#ifndef SVR4_NO_OSTAT
static void bsd_to_svr4_stat __P((struct stat *, struct svr4_stat *));

static void
bsd_to_svr4_stat(st, st4)
	struct stat		*st;
	struct svr4_stat 	*st4;
{
	memset(st4, 0, sizeof(*st4));
	st4->st_dev = bsd_to_svr4_odev_t(st->st_dev);
	st4->st_ino = st->st_ino;
	st4->st_mode = BSD_TO_SVR4_MODE(st->st_mode);
	st4->st_nlink = st->st_nlink;
	st4->st_uid = st->st_uid;
	st4->st_gid = st->st_gid;
	st4->st_rdev = bsd_to_svr4_odev_t(st->st_rdev);
	st4->st_size = st->st_size;
	st4->st_atim = st->st_atimespec.tv_sec;
	st4->st_mtim = st->st_mtimespec.tv_sec;
	st4->st_ctim = st->st_ctimespec.tv_sec;
}
#endif


static void
bsd_to_svr4_xstat(st, st4)
	struct stat		*st;
	struct svr4_xstat	*st4;
{
	memset(st4, 0, sizeof(*st4));
	st4->st_dev = bsd_to_svr4_dev_t(st->st_dev);
	st4->st_ino = st->st_ino;
	st4->st_mode = BSD_TO_SVR4_MODE(st->st_mode);
	st4->st_nlink = st->st_nlink;
	st4->st_uid = st->st_uid;
	st4->st_gid = st->st_gid;
	st4->st_rdev = bsd_to_svr4_dev_t(st->st_rdev);
	st4->st_size = st->st_size;
	st4->st_atim = st->st_atimespec;
	st4->st_mtim = st->st_mtimespec;
	st4->st_ctim = st->st_ctimespec;
	st4->st_blksize = st->st_blksize;
	st4->st_blocks = st->st_blocks;
	strcpy(st4->st_fstype, "unknown");
}


static void
bsd_to_svr4_stat64(st, st4)
	struct stat		*st;
	struct svr4_stat64	*st4;
{
	memset(st4, 0, sizeof(*st4));
	st4->st_dev = bsd_to_svr4_dev_t(st->st_dev);
	st4->st_ino = st->st_ino;
	st4->st_mode = BSD_TO_SVR4_MODE(st->st_mode);
	st4->st_nlink = st->st_nlink;
	st4->st_uid = st->st_uid;
	st4->st_gid = st->st_gid;
	st4->st_rdev = bsd_to_svr4_dev_t(st->st_rdev);
	st4->st_size = st->st_size;
	st4->st_atim = st->st_atimespec;
	st4->st_mtim = st->st_mtimespec;
	st4->st_ctim = st->st_ctimespec;
	st4->st_blksize = st->st_blksize;
	st4->st_blocks = st->st_blocks;
	strcpy(st4->st_fstype, "unknown");
}

int
svr4_sys_stat(p, uap)
	struct proc *p;
	struct svr4_sys_stat_args *uap;
{
	struct stat		st;
	struct svr4_stat	svr4_st;
	struct stat_args	cup;
	int			error;
	caddr_t sg = stackgap_init();

	CHECKALTEXIST(p, &sg, SCARG(uap, path));

	SCARG(&cup, path) = SCARG(uap, path);
	SCARG(&cup, ub) = stackgap_alloc(&sg, sizeof(struct stat));


	if ((error = stat(p, &cup)) != 0)
		return error;

	if ((error = copyin(SCARG(&cup, ub), &st, sizeof st)) != 0)
		return error;

	bsd_to_svr4_stat(&st, &svr4_st);

	if (S_ISSOCK(st.st_mode))
		(void) svr4_add_socket(p, SCARG(uap, path), &st);

	if ((error = copyout(&svr4_st, SCARG(uap, ub), sizeof svr4_st)) != 0)
		return error;

	return 0;
}


int
svr4_sys_lstat(p, uap)
	register struct proc *p;
	struct svr4_sys_lstat_args *uap;
{
	struct stat		st;
	struct svr4_stat	svr4_st;
	struct lstat_args	cup;
	int			error;
	caddr_t			sg = stackgap_init();

	CHECKALTEXIST(p, &sg, SCARG(uap, path));

	SCARG(&cup, path) = SCARG(uap, path);
	SCARG(&cup, ub) = stackgap_alloc(&sg, sizeof(struct stat));

	if ((error = lstat(p, &cup)) != 0)
		return error;

	if ((error = copyin(SCARG(&cup, ub), &st, sizeof st)) != 0)
		return error;

	bsd_to_svr4_stat(&st, &svr4_st);

	if (S_ISSOCK(st.st_mode))
		(void) svr4_add_socket(p, SCARG(uap, path), &st);

	if ((error = copyout(&svr4_st, SCARG(uap, ub), sizeof svr4_st)) != 0)
		return error;

	return 0;
}


int
svr4_sys_fstat(p, uap)
	register struct proc *p;
	struct svr4_sys_fstat_args *uap;
{
	struct stat		st;
	struct svr4_stat	svr4_st;
	struct fstat_args	cup;
	int			error;
	caddr_t			sg = stackgap_init();

	SCARG(&cup, fd) = SCARG(uap, fd);
	SCARG(&cup, sb) = stackgap_alloc(&sg, sizeof(struct stat));

	if ((error = fstat(p, &cup)) != 0)
		return error;

	if ((error = copyin(SCARG(&cup, sb), &st, sizeof st)) != 0)
		return error;

	bsd_to_svr4_stat(&st, &svr4_st);

	if ((error = copyout(&svr4_st, SCARG(uap, sb), sizeof svr4_st)) != 0)
		return error;

	return 0;
}


int
svr4_sys_xstat(p, uap)
	register struct proc *p;
	struct svr4_sys_xstat_args *uap;
{
	struct stat		st;
	struct svr4_xstat	svr4_st;
	struct stat_args	cup;
	int			error;

	caddr_t sg = stackgap_init();
	CHECKALTEXIST(p, &sg, SCARG(uap, path));

	SCARG(&cup, path) = SCARG(uap, path);
	SCARG(&cup, ub) = stackgap_alloc(&sg, sizeof(struct stat));

	if ((error = stat(p, &cup)) != 0)
		return error;

	if ((error = copyin(SCARG(&cup, ub), &st, sizeof st)) != 0)
		return error;

	bsd_to_svr4_xstat(&st, &svr4_st);

#if defined(SOCKET_NOTYET)
	if (S_ISSOCK(st.st_mode))
		(void) svr4_add_socket(p, SCARG(uap, path), &st);
#endif

	if ((error = copyout(&svr4_st, SCARG(uap, ub), sizeof svr4_st)) != 0)
		return error;

	return 0;
}

int
svr4_sys_lxstat(p, uap)
	register struct proc *p;
	struct svr4_sys_lxstat_args *uap;
{
	struct stat		st;
	struct svr4_xstat	svr4_st;
	struct lstat_args	cup;
	int			error;
	caddr_t sg = stackgap_init();
	CHECKALTEXIST(p, &sg, SCARG(uap, path));

	SCARG(&cup, path) = SCARG(uap, path);
	SCARG(&cup, ub) = stackgap_alloc(&sg, sizeof(struct stat));

	if ((error = lstat(p, &cup)) != 0)
		return error;

	if ((error = copyin(SCARG(&cup, ub), &st, sizeof st)) != 0)
		return error;

	bsd_to_svr4_xstat(&st, &svr4_st);

#if defined(SOCKET_NOTYET)
	if (S_ISSOCK(st.st_mode))
		(void) svr4_add_socket(p, SCARG(uap, path), &st);
#endif
	if ((error = copyout(&svr4_st, SCARG(uap, ub), sizeof svr4_st)) != 0)
		return error;

	return 0;
}


int
svr4_sys_fxstat(p, uap)
	register struct proc *p;
	struct svr4_sys_fxstat_args *uap;
{
	struct stat		st;
	struct svr4_xstat	svr4_st;
	struct fstat_args	cup;
	int			error;

	caddr_t sg = stackgap_init();

	SCARG(&cup, fd) = SCARG(uap, fd);
	SCARG(&cup, sb) = stackgap_alloc(&sg, sizeof(struct stat));

	if ((error = fstat(p, &cup)) != 0)
		return error;

	if ((error = copyin(SCARG(&cup, sb), &st, sizeof st)) != 0)
		return error;

	bsd_to_svr4_xstat(&st, &svr4_st);

	if ((error = copyout(&svr4_st, SCARG(uap, sb), sizeof svr4_st)) != 0)
		return error;

	return 0;
}

int
svr4_sys_stat64(p, uap)
	register struct proc *p;
	struct svr4_sys_stat64_args *uap;
{
	struct stat		st;
	struct svr4_stat64	svr4_st;
	struct stat_args	cup;
	int			error;
	caddr_t			sg = stackgap_init();

	CHECKALTEXIST(p, &sg, SCARG(uap, path));

	SCARG(&cup, path) = SCARG(uap, path);
	SCARG(&cup, ub) = stackgap_alloc(&sg, sizeof(struct stat));

	if ((error = stat(p, &cup)) != 0)
		return error;

	if ((error = copyin(SCARG(&cup, ub), &st, sizeof st)) != 0)
		return error;

	bsd_to_svr4_stat64(&st, &svr4_st);

	if (S_ISSOCK(st.st_mode))
		(void) svr4_add_socket(p, SCARG(uap, path), &st);

	if ((error = copyout(&svr4_st, SCARG(uap, sb), sizeof svr4_st)) != 0)
		return error;

	return 0;
}


int
svr4_sys_lstat64(p, uap)
	register struct proc *p;
	struct svr4_sys_lstat64_args *uap;
{
	struct stat		st;
	struct svr4_stat64	svr4_st;
	struct stat_args	cup;
	int			error;
	caddr_t			sg = stackgap_init();

	CHECKALTEXIST(p, &sg, (char *) SCARG(uap, path));

	SCARG(&cup, path) = SCARG(uap, path);
	SCARG(&cup, ub) = stackgap_alloc(&sg, sizeof(struct stat));

	if ((error = lstat(p, (struct lstat_args *)&cup)) != 0)
		return error;

	if ((error = copyin(SCARG(&cup, ub), &st, sizeof st)) != 0)
		return error;

	bsd_to_svr4_stat64(&st, &svr4_st);

	if (S_ISSOCK(st.st_mode))
		(void) svr4_add_socket(p, SCARG(uap, path), &st);

	if ((error = copyout(&svr4_st, SCARG(uap, sb), sizeof svr4_st)) != 0)
		return error;

	return 0;
}


int
svr4_sys_fstat64(p, uap)
	register struct proc *p;
	struct svr4_sys_fstat64_args *uap;
{
	struct stat		st;
	struct svr4_stat64	svr4_st;
	struct fstat_args	cup;
	int			error;
	caddr_t sg = stackgap_init();

	SCARG(&cup, fd) = SCARG(uap, fd);
	SCARG(&cup, sb) = stackgap_alloc(&sg, sizeof(struct stat));

	if ((error = fstat(p, &cup)) != 0)
		return error;

	if ((error = copyin(SCARG(&cup, sb), &st, sizeof st)) != 0)
		return error;

	bsd_to_svr4_stat64(&st, &svr4_st);

	if ((error = copyout(&svr4_st, SCARG(uap, sb), sizeof svr4_st)) != 0)
		return error;

	return 0;
}


int
svr4_ustat(p, uap)
	register struct proc *p;
	struct svr4_ustat_args *uap;
{
	struct svr4_ustat	us;
	int			error;

	memset(&us, 0, sizeof us);

	/*
         * XXX: should set f_tfree and f_tinode at least
         * How do we translate dev -> fstat? (and then to svr4_ustat)
         */
	if ((error = copyout(&us, SCARG(uap, name), sizeof us)) != 0)
		return (error);

	return 0;
}

/*extern char ostype[], hostname[], osrelease[], version[], machine[];*/

int
svr4_sys_uname(p, uap)
	register struct proc *p;
	struct svr4_sys_uname_args *uap;
{
	struct svr4_utsname	sut;
	

	memset(&sut, 0, sizeof(sut));

	strncpy(sut.sysname, ostype, sizeof(sut.sysname));
	sut.sysname[sizeof(sut.sysname) - 1] = '\0';

	strncpy(sut.nodename, hostname, sizeof(sut.nodename));
	sut.nodename[sizeof(sut.nodename) - 1] = '\0';

	strncpy(sut.release, osrelease, sizeof(sut.release));
	sut.release[sizeof(sut.release) - 1] = '\0';

	strncpy(sut.version, version, sizeof(sut.version));
	sut.version[sizeof(sut.version) - 1] = '\0';

	strncpy(sut.machine, machine, sizeof(sut.machine));
	sut.machine[sizeof(sut.machine) - 1] = '\0';

	return copyout((caddr_t) &sut, (caddr_t) SCARG(uap, name),
		       sizeof(struct svr4_utsname));
}

int
svr4_sys_systeminfo(p, uap)
	struct proc *p;
	struct svr4_sys_systeminfo_args *uap;
{
	char		*str = NULL;
	int		error = 0;
	register_t	*retval = p->p_retval;
	size_t		len = 0;
	char		buf[1];   /* XXX NetBSD uses 256, but that seems
				     like awfully excessive kstack usage
				     for an empty string... */
	u_int		rlen = SCARG(uap, len);

	switch (SCARG(uap, what)) {
	case SVR4_SI_SYSNAME:
		str = ostype;
		break;

	case SVR4_SI_HOSTNAME:
		str = hostname;
		break;

	case SVR4_SI_RELEASE:
		str = osrelease;
		break;

	case SVR4_SI_VERSION:
		str = version;
		break;

	case SVR4_SI_MACHINE:
		str = machine;
		break;

	case SVR4_SI_ARCHITECTURE:
		str = machine;
		break;

	case SVR4_SI_HW_SERIAL:
		str = "0";
		break;

	case SVR4_SI_HW_PROVIDER:
		str = ostype;
		break;

	case SVR4_SI_SRPC_DOMAIN:
		str = domainname;
		break;

	case SVR4_SI_PLATFORM:
#ifdef __i386__
		str = "i86pc";
#else
		str = "unknown";
#endif
		break;

	case SVR4_SI_KERB_REALM:
		str = "unsupported";
		break;
#if defined(WHY_DOES_AN_EMULATOR_WANT_TO_SET_HOSTNAMES)
	case SVR4_SI_SET_HOSTNAME:
		if ((error = suser(p)) != 0)
			return error;
		name = KERN_HOSTNAME;
		return kern_sysctl(&name, 1, 0, 0, SCARG(uap, buf), rlen, p);

	case SVR4_SI_SET_SRPC_DOMAIN:
		if ((error = suser(p)) != 0)
			return error;
		name = KERN_NISDOMAINNAME;
		return kern_sysctl(&name, 1, 0, 0, SCARG(uap, buf), rlen, p);
#else
	case SVR4_SI_SET_HOSTNAME:
        case SVR4_SI_SET_SRPC_DOMAIN:
		/* FALLTHROUGH */
#endif
	case SVR4_SI_SET_KERB_REALM:
		return 0;

	default:
		DPRINTF(("Bad systeminfo command %d\n", SCARG(uap, what)));
		return ENOSYS;
	}

	if (str) {
		len = strlen(str) + 1;
		if (len > rlen)
			len = rlen;

		if (SCARG(uap, buf)) {
			error = copyout(str, SCARG(uap, buf), len);
			if (error)
				return error;
			/* make sure we are NULL terminated */
			buf[0] = '\0';
			error = copyout(buf, &(SCARG(uap, buf)[len - 1]), 1);
		}
		else
			error = 0;
	}
	/* XXX NetBSD has hostname setting stuff here.  Why would an emulator
	   want to do that? */

	*retval = len;
	return error;
}

int
svr4_sys_utssys(p, uap)
	register struct proc *p;
	struct svr4_sys_utssys_args *uap;
{
	switch (SCARG(uap, sel)) {
	case 0:		/* uname(2)  */
		{
			struct svr4_sys_uname_args ua;
			SCARG(&ua, name) = SCARG(uap, a1);
			return svr4_sys_uname(p, &ua);
		}

	case 2:		/* ustat(2)  */
		{
			struct svr4_ustat_args ua;
			SCARG(&ua, dev) = (svr4_dev_t) SCARG(uap, a2);
			SCARG(&ua, name) = SCARG(uap, a1);
			return svr4_ustat(p, &ua);
		}

	case 3:		/* fusers(2) */
		return ENOSYS;

	default:
		return ENOSYS;
	}
	return ENOSYS;
}


int
svr4_sys_utime(p, uap)
	register struct proc *p;
	struct svr4_sys_utime_args *uap;
{
	struct svr4_utimbuf ub;
	struct timeval tbuf[2];
	struct utimes_args ap;
	int error;
	caddr_t sg = stackgap_init();
	void *ttp;

	CHECKALTEXIST(p, &sg, SCARG(uap, path));
	SCARG(&ap, path) = SCARG(uap, path);
	if (SCARG(uap, ubuf) != NULL) {
		if ((error = copyin(SCARG(uap, ubuf), &ub, sizeof(ub))) != 0)
			return error;
		tbuf[0].tv_sec = ub.actime;
		tbuf[0].tv_usec = 0;
		tbuf[1].tv_sec = ub.modtime;
		tbuf[1].tv_usec = 0;
		ttp = stackgap_alloc(&sg, sizeof(tbuf));
		error = copyout(tbuf, ttp, sizeof(tbuf));
		if (error)
			return error;
		SCARG(&ap, tptr) = ttp;
	}
	else
		SCARG(&ap, tptr) = NULL;
	return utimes(p, &ap);
}


int
svr4_sys_utimes(p, uap)
	register struct proc *p;
	struct svr4_sys_utimes_args *uap;
{
	caddr_t sg = stackgap_init();
	CHECKALTEXIST(p, &sg, SCARG(uap, path));
	return utimes(p, (struct utimes_args *)uap);
}

static int
svr4_to_bsd_pathconf(name)
	int name;
{
	switch (name) {
	case SVR4_PC_LINK_MAX:
	    	return _PC_LINK_MAX;

	case SVR4_PC_MAX_CANON:
		return _PC_MAX_CANON;

	case SVR4_PC_MAX_INPUT:
		return _PC_MAX_INPUT;

	case SVR4_PC_NAME_MAX:
		return _PC_NAME_MAX;

	case SVR4_PC_PATH_MAX:
		return _PC_PATH_MAX;

	case SVR4_PC_PIPE_BUF:
		return _PC_PIPE_BUF;

	case SVR4_PC_NO_TRUNC:
		return _PC_NO_TRUNC;

	case SVR4_PC_VDISABLE:
		return _PC_VDISABLE;

	case SVR4_PC_CHOWN_RESTRICTED:
		return _PC_CHOWN_RESTRICTED;
	case SVR4_PC_SYNC_IO:
#if defined(_PC_SYNC_IO)
		return _PC_SYNC_IO;
#else
		return 0;
#endif
	case SVR4_PC_ASYNC_IO:
	case SVR4_PC_PRIO_IO:
		/* Not supported */
		return 0;

	default:
		/* Invalid */
		return -1;
	}
}


int
svr4_sys_pathconf(p, uap)
	register struct proc *p;
	struct svr4_sys_pathconf_args *uap;
{
	caddr_t		sg = stackgap_init();
	register_t	*retval = p->p_retval;

	CHECKALTEXIST(p, &sg, SCARG(uap, path));

	SCARG(uap, name) = svr4_to_bsd_pathconf(SCARG(uap, name));

	switch (SCARG(uap, name)) {
	case -1:
		*retval = -1;
		return EINVAL;
	case 0:
		*retval = 0;
		return 0;
	default:
		return pathconf(p, (struct pathconf_args *)uap);
	}
}


int
svr4_sys_fpathconf(p, uap)
	register struct proc *p;
	struct svr4_sys_fpathconf_args *uap;
{
        register_t	*retval = p->p_retval;

	SCARG(uap, name) = svr4_to_bsd_pathconf(SCARG(uap, name));

	switch (SCARG(uap, name)) {
	case -1:
		*retval = -1;
		return EINVAL;
	case 0:
		*retval = 0;
		return 0;
	default:
		return fpathconf(p, (struct fpathconf_args *)uap);
	}
}
