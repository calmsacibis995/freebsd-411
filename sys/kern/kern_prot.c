/*
 * Copyright (c) 1982, 1986, 1989, 1990, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *
 *	@(#)kern_prot.c	8.6 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/kern/kern_prot.c,v 1.53.2.10 2003/09/23 18:22:13 silby Exp $
 */

/*
 * System calls related to processes and protection
 */

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/acct.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/pioctl.h>
#include <sys/resourcevar.h>

static MALLOC_DEFINE(M_CRED, "cred", "credentials");

#ifndef _SYS_SYSPROTO_H_
struct getpid_args {
	int	dummy;
};
#endif

/*
 * NOT MP SAFE due to p_pptr access
 */
/* ARGSUSED */
int
getpid(p, uap)
	struct proc *p;
	struct getpid_args *uap;
{

	p->p_retval[0] = p->p_pid;
#if defined(COMPAT_43) || defined(COMPAT_SUNOS)
	p->p_retval[1] = p->p_pptr->p_pid;
#endif
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct getppid_args {
        int     dummy;
};
#endif
/* ARGSUSED */
int
getppid(p, uap)
	struct proc *p;
	struct getppid_args *uap;
{

	p->p_retval[0] = p->p_pptr->p_pid;
	return (0);
}

/* 
 * Get process group ID; note that POSIX getpgrp takes no parameter 
 *
 * MP SAFE
 */
#ifndef _SYS_SYSPROTO_H_
struct getpgrp_args {
        int     dummy;
};
#endif

int
getpgrp(p, uap)
	struct proc *p;
	struct getpgrp_args *uap;
{

	p->p_retval[0] = p->p_pgrp->pg_id;
	return (0);
}

/* Get an arbitary pid's process group id */
#ifndef _SYS_SYSPROTO_H_
struct getpgid_args {
	pid_t	pid;
};
#endif

int
getpgid(p, uap)
	struct proc *p;
	struct getpgid_args *uap;
{
	struct proc *pt;

	pt = p;
	if (uap->pid == 0)
		goto found;

	if ((pt = pfind(uap->pid)) == 0)
		return ESRCH;
found:
	p->p_retval[0] = pt->p_pgrp->pg_id;
	return 0;
}

/*
 * Get an arbitary pid's session id.
 */
#ifndef _SYS_SYSPROTO_H_
struct getsid_args {
	pid_t	pid;
};
#endif

int
getsid(p, uap)
	struct proc *p;
	struct getsid_args *uap;
{
	struct proc *pt;

	pt = p;
	if (uap->pid == 0)
		goto found;

	if ((pt = pfind(uap->pid)) == 0)
		return ESRCH;
found:
	p->p_retval[0] = pt->p_session->s_sid;
	return 0;
}


/*
 * getuid() - MP SAFE
 */
#ifndef _SYS_SYSPROTO_H_
struct getuid_args {
        int     dummy;
};
#endif

/* ARGSUSED */
int
getuid(p, uap)
	struct proc *p;
	struct getuid_args *uap;
{

	p->p_retval[0] = p->p_cred->p_ruid;
#if defined(COMPAT_43) || defined(COMPAT_SUNOS)
	p->p_retval[1] = p->p_ucred->cr_uid;
#endif
	return (0);
}

/*
 * geteuid() - MP SAFE
 */
#ifndef _SYS_SYSPROTO_H_
struct geteuid_args {
        int     dummy;
};
#endif

/* ARGSUSED */
int
geteuid(p, uap)
	struct proc *p;
	struct geteuid_args *uap;
{

	p->p_retval[0] = p->p_ucred->cr_uid;
	return (0);
}

/*
 * getgid() - MP SAFE
 */
#ifndef _SYS_SYSPROTO_H_
struct getgid_args {
        int     dummy;
};
#endif

/* ARGSUSED */
int
getgid(p, uap)
	struct proc *p;
	struct getgid_args *uap;
{

	p->p_retval[0] = p->p_cred->p_rgid;
#if defined(COMPAT_43) || defined(COMPAT_SUNOS)
	p->p_retval[1] = p->p_ucred->cr_groups[0];
#endif
	return (0);
}

/*
 * Get effective group ID.  The "egid" is groups[0], and could be obtained
 * via getgroups.  This syscall exists because it is somewhat painful to do
 * correctly in a library function.
 */
#ifndef _SYS_SYSPROTO_H_
struct getegid_args {
        int     dummy;
};
#endif

/* ARGSUSED */
int
getegid(p, uap)
	struct proc *p;
	struct getegid_args *uap;
{

	p->p_retval[0] = p->p_ucred->cr_groups[0];
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct getgroups_args {
	u_int	gidsetsize;
	gid_t	*gidset;
};
#endif
int
getgroups(p, uap)
	struct proc *p;
	register struct	getgroups_args *uap;
{
	register struct pcred *pc = p->p_cred;
	register u_int ngrp;
	int error;

	if ((ngrp = uap->gidsetsize) == 0) {
		p->p_retval[0] = pc->pc_ucred->cr_ngroups;
		return (0);
	}
	if (ngrp < pc->pc_ucred->cr_ngroups)
		return (EINVAL);
	ngrp = pc->pc_ucred->cr_ngroups;
	if ((error = copyout((caddr_t)pc->pc_ucred->cr_groups,
	    (caddr_t)uap->gidset, ngrp * sizeof(gid_t))))
		return (error);
	p->p_retval[0] = ngrp;
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct setsid_args {
        int     dummy;
};
#endif

/* ARGSUSED */
int
setsid(p, uap)
	register struct proc *p;
	struct setsid_args *uap;
{

	if (p->p_pgid == p->p_pid || pgfind(p->p_pid)) {
		return (EPERM);
	} else {
		(void)enterpgrp(p, p->p_pid, 1);
		p->p_retval[0] = p->p_pid;
		return (0);
	}
}

/*
 * set process group (setpgid/old setpgrp)
 *
 * caller does setpgid(targpid, targpgid)
 *
 * pid must be caller or child of caller (ESRCH)
 * if a child
 *	pid must be in same session (EPERM)
 *	pid can't have done an exec (EACCES)
 * if pgid != pid
 * 	there must exist some pid in same session having pgid (EPERM)
 * pid must not be session leader (EPERM)
 */
#ifndef _SYS_SYSPROTO_H_
struct setpgid_args {
	int	pid;	/* target process id */
	int	pgid;	/* target pgrp id */
};
#endif
/* ARGSUSED */
int
setpgid(curp, uap)
	struct proc *curp;
	register struct setpgid_args *uap;
{
	register struct proc *targp;		/* target process */
	register struct pgrp *pgrp;		/* target pgrp */

	if (uap->pgid < 0)
		return (EINVAL);
	if (uap->pid != 0 && uap->pid != curp->p_pid) {
		if ((targp = pfind(uap->pid)) == 0 || !inferior(targp))
			return (ESRCH);
		if (targp->p_pgrp == NULL ||  targp->p_session != curp->p_session)
			return (EPERM);
		if (targp->p_flag & P_EXEC)
			return (EACCES);
	} else
		targp = curp;
	if (SESS_LEADER(targp))
		return (EPERM);
	if (uap->pgid == 0)
		uap->pgid = targp->p_pid;
	else if (uap->pgid != targp->p_pid)
		if ((pgrp = pgfind(uap->pgid)) == 0 ||
	            pgrp->pg_session != curp->p_session)
			return (EPERM);
	return (enterpgrp(targp, uap->pgid, 0));
}

/*
 * Use the clause in B.4.2.2 that allows setuid/setgid to be 4.2/4.3BSD
 * compatable.  It says that setting the uid/gid to euid/egid is a special
 * case of "appropriate privilege".  Once the rules are expanded out, this
 * basically means that setuid(nnn) sets all three id's, in all permitted
 * cases unless _POSIX_SAVED_IDS is enabled.  In that case, setuid(getuid())
 * does not set the saved id - this is dangerous for traditional BSD
 * programs.  For this reason, we *really* do not want to set
 * _POSIX_SAVED_IDS and do not want to clear POSIX_APPENDIX_B_4_2_2.
 */
#define POSIX_APPENDIX_B_4_2_2

#ifndef _SYS_SYSPROTO_H_
struct setuid_args {
	uid_t	uid;
};
#endif
/* ARGSUSED */
int
setuid(p, uap)
	struct proc *p;
	struct setuid_args *uap;
{
	register struct pcred *pc = p->p_cred;
	register uid_t uid;
	int error;

	/*
	 * See if we have "permission" by POSIX 1003.1 rules.
	 *
	 * Note that setuid(geteuid()) is a special case of 
	 * "appropriate privileges" in appendix B.4.2.2.  We need
	 * to use this clause to be compatable with traditional BSD
	 * semantics.  Basically, it means that "setuid(xx)" sets all
	 * three id's (assuming you have privs).
	 *
	 * Notes on the logic.  We do things in three steps.
	 * 1: We determine if the euid is going to change, and do EPERM
	 *    right away.  We unconditionally change the euid later if this
	 *    test is satisfied, simplifying that part of the logic.
	 * 2: We determine if the real and/or saved uid's are going to
	 *    change.  Determined by compile options.
	 * 3: Change euid last. (after tests in #2 for "appropriate privs")
	 */
	uid = uap->uid;
	if (uid != pc->p_ruid &&		/* allow setuid(getuid()) */
#ifdef _POSIX_SAVED_IDS
	    uid != pc->p_svuid &&		/* allow setuid(saved gid) */
#endif
#ifdef POSIX_APPENDIX_B_4_2_2	/* Use BSD-compat clause from B.4.2.2 */
	    uid != pc->pc_ucred->cr_uid &&	/* allow setuid(geteuid()) */
#endif
	    (error = suser_xxx(0, p, PRISON_ROOT)))
		return (error);

#ifdef _POSIX_SAVED_IDS
	/*
	 * Do we have "appropriate privileges" (are we root or uid == euid)
	 * If so, we are changing the real uid and/or saved uid.
	 */
	if (
#ifdef POSIX_APPENDIX_B_4_2_2	/* Use the clause from B.4.2.2 */
	    uid == pc->pc_ucred->cr_uid ||
#endif
	    suser_xxx(0, p, PRISON_ROOT) == 0) /* we are using privs */
#endif
	{
		/*
		 * Set the real uid and transfer proc count to new user.
		 */
		if (uid != pc->p_ruid) {
			change_ruid(p, uid);
			setsugid(p);
		}
		/*
		 * Set saved uid
		 *
		 * XXX always set saved uid even if not _POSIX_SAVED_IDS, as
		 * the security of seteuid() depends on it.  B.4.2.2 says it
		 * is important that we should do this.
		 */
		if (pc->p_svuid != uid) {
			pc->p_svuid = uid;
			setsugid(p);
		}
	}

	/*
	 * In all permitted cases, we are changing the euid.
	 * Copy credentials so other references do not see our changes.
	 */
	if (pc->pc_ucred->cr_uid != uid) {
		change_euid(p, uid);
		setsugid(p);
	}
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct seteuid_args {
	uid_t	euid;
};
#endif
/* ARGSUSED */
int
seteuid(p, uap)
	struct proc *p;
	struct seteuid_args *uap;
{
	register struct pcred *pc = p->p_cred;
	register uid_t euid;
	int error;

	euid = uap->euid;
	if (euid != pc->p_ruid &&		/* allow seteuid(getuid()) */
	    euid != pc->p_svuid &&		/* allow seteuid(saved uid) */
	    (error = suser_xxx(0, p, PRISON_ROOT)))
		return (error);
	/*
	 * Everything's okay, do it.  Copy credentials so other references do
	 * not see our changes.
	 */
	if (pc->pc_ucred->cr_uid != euid) {
		change_euid(p, euid);
		setsugid(p);
	}
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct setgid_args {
	gid_t	gid;
};
#endif
/* ARGSUSED */
int
setgid(p, uap)
	struct proc *p;
	struct setgid_args *uap;
{
	register struct pcred *pc = p->p_cred;
	register gid_t gid;
	int error;

	/*
	 * See if we have "permission" by POSIX 1003.1 rules.
	 *
	 * Note that setgid(getegid()) is a special case of
	 * "appropriate privileges" in appendix B.4.2.2.  We need
	 * to use this clause to be compatable with traditional BSD
	 * semantics.  Basically, it means that "setgid(xx)" sets all
	 * three id's (assuming you have privs).
	 *
	 * For notes on the logic here, see setuid() above.
	 */
	gid = uap->gid;
	if (gid != pc->p_rgid &&		/* allow setgid(getgid()) */
#ifdef _POSIX_SAVED_IDS
	    gid != pc->p_svgid &&		/* allow setgid(saved gid) */
#endif
#ifdef POSIX_APPENDIX_B_4_2_2	/* Use BSD-compat clause from B.4.2.2 */
	    gid != pc->pc_ucred->cr_groups[0] && /* allow setgid(getegid()) */
#endif
	    (error = suser_xxx(0, p, PRISON_ROOT)))
		return (error);

#ifdef _POSIX_SAVED_IDS
	/*
	 * Do we have "appropriate privileges" (are we root or gid == egid)
	 * If so, we are changing the real uid and saved gid.
	 */
	if (
#ifdef POSIX_APPENDIX_B_4_2_2	/* use the clause from B.4.2.2 */
	    gid == pc->pc_ucred->cr_groups[0] ||
#endif
	    suser_xxx(0, p, PRISON_ROOT) == 0) /* we are using privs */
#endif
	{
		/*
		 * Set real gid
		 */
		if (pc->p_rgid != gid) {
			pc->p_rgid = gid;
			setsugid(p);
		}
		/*
		 * Set saved gid
		 *
		 * XXX always set saved gid even if not _POSIX_SAVED_IDS, as
		 * the security of setegid() depends on it.  B.4.2.2 says it
		 * is important that we should do this.
		 */
		if (pc->p_svgid != gid) {
			pc->p_svgid = gid;
			setsugid(p);
		}
	}
	/*
	 * In all cases permitted cases, we are changing the egid.
	 * Copy credentials so other references do not see our changes.
	 */
	if (pc->pc_ucred->cr_groups[0] != gid) {
		pc->pc_ucred = crcopy(pc->pc_ucred);
		pc->pc_ucred->cr_groups[0] = gid;
		setsugid(p);
	}
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct setegid_args {
	gid_t	egid;
};
#endif
/* ARGSUSED */
int
setegid(p, uap)
	struct proc *p;
	struct setegid_args *uap;
{
	register struct pcred *pc = p->p_cred;
	register gid_t egid;
	int error;

	egid = uap->egid;
	if (egid != pc->p_rgid &&		/* allow setegid(getgid()) */
	    egid != pc->p_svgid &&		/* allow setegid(saved gid) */
	    (error = suser_xxx(0, p, PRISON_ROOT)))
		return (error);
	if (pc->pc_ucred->cr_groups[0] != egid) {
		pc->pc_ucred = crcopy(pc->pc_ucred);
		pc->pc_ucred->cr_groups[0] = egid;
		setsugid(p);
	}
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct setgroups_args {
	u_int	gidsetsize;
	gid_t	*gidset;
};
#endif
/* ARGSUSED */
int
setgroups(p, uap)
	struct proc *p;
	struct setgroups_args *uap;
{
	register struct pcred *pc = p->p_cred;
	register u_int ngrp;
	int error;

	if ((error = suser_xxx(0, p, PRISON_ROOT)))
		return (error);
	ngrp = uap->gidsetsize;
	if (ngrp > NGROUPS)
		return (EINVAL);
	/*
	 * XXX A little bit lazy here.  We could test if anything has
	 * changed before crcopy() and setting P_SUGID.
	 */
	pc->pc_ucred = crcopy(pc->pc_ucred);
	if (ngrp < 1) {
		/*
		 * setgroups(0, NULL) is a legitimate way of clearing the
		 * groups vector on non-BSD systems (which generally do not
		 * have the egid in the groups[0]).  We risk security holes
		 * when running non-BSD software if we do not do the same.
		 */
		pc->pc_ucred->cr_ngroups = 1;
	} else {
		if ((error = copyin((caddr_t)uap->gidset,
		    (caddr_t)pc->pc_ucred->cr_groups, ngrp * sizeof(gid_t))))
			return (error);
		pc->pc_ucred->cr_ngroups = ngrp;
	}
	setsugid(p);
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct setreuid_args {
	uid_t	ruid;
	uid_t	euid;
};
#endif
/* ARGSUSED */
int
setreuid(p, uap)
	register struct proc *p;
	struct setreuid_args *uap;
{
	register struct pcred *pc = p->p_cred;
	register uid_t ruid, euid;
	int error;

	ruid = uap->ruid;
	euid = uap->euid;
	if (((ruid != (uid_t)-1 && ruid != pc->p_ruid && ruid != pc->p_svuid) ||
	     (euid != (uid_t)-1 && euid != pc->pc_ucred->cr_uid &&
	     euid != pc->p_ruid && euid != pc->p_svuid)) &&
	    (error = suser_xxx(0, p, PRISON_ROOT)) != 0)
		return (error);

	if (euid != (uid_t)-1 && pc->pc_ucred->cr_uid != euid) {
		change_euid(p, euid);
		setsugid(p);
	}
	if (ruid != (uid_t)-1 && pc->p_ruid != ruid) {
		change_ruid(p, ruid);
		setsugid(p);
	}
	if ((ruid != (uid_t)-1 || pc->pc_ucred->cr_uid != pc->p_ruid) &&
	    pc->p_svuid != pc->pc_ucred->cr_uid) {
		pc->p_svuid = pc->pc_ucred->cr_uid;
		setsugid(p);
	}
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct setregid_args {
	gid_t	rgid;
	gid_t	egid;
};
#endif
/* ARGSUSED */
int
setregid(p, uap)
	register struct proc *p;
	struct setregid_args *uap;
{
	register struct pcred *pc = p->p_cred;
	register gid_t rgid, egid;
	int error;

	rgid = uap->rgid;
	egid = uap->egid;
	if (((rgid != (gid_t)-1 && rgid != pc->p_rgid && rgid != pc->p_svgid) ||
	     (egid != (gid_t)-1 && egid != pc->pc_ucred->cr_groups[0] &&
	     egid != pc->p_rgid && egid != pc->p_svgid)) &&
	    (error = suser_xxx(0, p, PRISON_ROOT)) != 0)
		return (error);

	if (egid != (gid_t)-1 && pc->pc_ucred->cr_groups[0] != egid) {
		pc->pc_ucred = crcopy(pc->pc_ucred);
		pc->pc_ucred->cr_groups[0] = egid;
		setsugid(p);
	}
	if (rgid != (gid_t)-1 && pc->p_rgid != rgid) {
		pc->p_rgid = rgid;
		setsugid(p);
	}
	if ((rgid != (gid_t)-1 || pc->pc_ucred->cr_groups[0] != pc->p_rgid) &&
	    pc->p_svgid != pc->pc_ucred->cr_groups[0]) {
		pc->p_svgid = pc->pc_ucred->cr_groups[0];
		setsugid(p);
	}
	return (0);
}

/*
 * setresuid(ruid, euid, suid) is like setreuid except control over the
 * saved uid is explicit.
 */

#ifndef _SYS_SYSPROTO_H_
struct setresuid_args {
	uid_t	ruid;
	uid_t	euid;
	uid_t	suid;
};
#endif
/* ARGSUSED */
int
setresuid(p, uap)
	register struct proc *p;
	struct setresuid_args *uap;
{
	register struct pcred *pc = p->p_cred;
	register uid_t ruid, euid, suid;
	int error;

	ruid = uap->ruid;
	euid = uap->euid;
	suid = uap->suid;
	if (((ruid != (uid_t)-1 && ruid != pc->p_ruid && ruid != pc->p_svuid &&
	      ruid != pc->pc_ucred->cr_uid) ||
	     (euid != (uid_t)-1 && euid != pc->p_ruid && euid != pc->p_svuid &&
	      euid != pc->pc_ucred->cr_uid) ||
	     (suid != (uid_t)-1 && suid != pc->p_ruid && suid != pc->p_svuid &&
	      suid != pc->pc_ucred->cr_uid)) &&
	    (error = suser_xxx(0, p, PRISON_ROOT)) != 0)
		return (error);
	if (euid != (uid_t)-1 && pc->pc_ucred->cr_uid != euid) {
		change_euid(p, euid);
		setsugid(p);
	}
	if (ruid != (uid_t)-1 && pc->p_ruid != ruid) {
		change_ruid(p, ruid);
		setsugid(p);
	}
	if (suid != (uid_t)-1 && pc->p_svuid != suid) {
		pc->p_svuid = suid;
		setsugid(p);
	}
	return (0);
}

/*
 * setresgid(rgid, egid, sgid) is like setregid except control over the
 * saved gid is explicit.
 */

#ifndef _SYS_SYSPROTO_H_
struct setresgid_args {
	gid_t	rgid;
	gid_t	egid;
	gid_t	sgid;
};
#endif
/* ARGSUSED */
int
setresgid(p, uap)
	register struct proc *p;
	struct setresgid_args *uap;
{
	register struct pcred *pc = p->p_cred;
	register gid_t rgid, egid, sgid;
	int error;

	rgid = uap->rgid;
	egid = uap->egid;
	sgid = uap->sgid;
	if (((rgid != (gid_t)-1 && rgid != pc->p_rgid && rgid != pc->p_svgid &&
	      rgid != pc->pc_ucred->cr_groups[0]) ||
	     (egid != (gid_t)-1 && egid != pc->p_rgid && egid != pc->p_svgid &&
	      egid != pc->pc_ucred->cr_groups[0]) ||
	     (sgid != (gid_t)-1 && sgid != pc->p_rgid && sgid != pc->p_svgid &&
	      sgid != pc->pc_ucred->cr_groups[0])) &&
	    (error = suser_xxx(0, p, PRISON_ROOT)) != 0)
		return (error);

	if (egid != (gid_t)-1 && pc->pc_ucred->cr_groups[0] != egid) {
		pc->pc_ucred = crcopy(pc->pc_ucred);
		pc->pc_ucred->cr_groups[0] = egid;
		setsugid(p);
	}
	if (rgid != (gid_t)-1 && pc->p_rgid != rgid) {
		pc->p_rgid = rgid;
		setsugid(p);
	}
	if (sgid != (gid_t)-1 && pc->p_svgid != sgid) {
		pc->p_svgid = sgid;
		setsugid(p);
	}
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct getresuid_args {
	uid_t	*ruid;
	uid_t	*euid;
	uid_t	*suid;
};
#endif
/* ARGSUSED */
int
getresuid(p, uap)
	register struct proc *p;
	struct getresuid_args *uap;
{
	struct pcred *pc = p->p_cred;
	int error1 = 0, error2 = 0, error3 = 0;

	if (uap->ruid)
		error1 = copyout((caddr_t)&pc->p_ruid,
		    (caddr_t)uap->ruid, sizeof(pc->p_ruid));
	if (uap->euid)
		error2 = copyout((caddr_t)&pc->pc_ucred->cr_uid,
		    (caddr_t)uap->euid, sizeof(pc->pc_ucred->cr_uid));
	if (uap->suid)
		error3 = copyout((caddr_t)&pc->p_svuid,
		    (caddr_t)uap->suid, sizeof(pc->p_svuid));
	return error1 ? error1 : (error2 ? error2 : error3);
}

#ifndef _SYS_SYSPROTO_H_
struct getresgid_args {
	gid_t	*rgid;
	gid_t	*egid;
	gid_t	*sgid;
};
#endif
/* ARGSUSED */
int
getresgid(p, uap)
	register struct proc *p;
	struct getresgid_args *uap;
{
	struct pcred *pc = p->p_cred;
	int error1 = 0, error2 = 0, error3 = 0;

	if (uap->rgid)
		error1 = copyout((caddr_t)&pc->p_rgid,
		    (caddr_t)uap->rgid, sizeof(pc->p_rgid));
	if (uap->egid)
		error2 = copyout((caddr_t)&pc->pc_ucred->cr_groups[0],
		    (caddr_t)uap->egid, sizeof(pc->pc_ucred->cr_groups[0]));
	if (uap->sgid)
		error3 = copyout((caddr_t)&pc->p_svgid,
		    (caddr_t)uap->sgid, sizeof(pc->p_svgid));
	return error1 ? error1 : (error2 ? error2 : error3);
}


#ifndef _SYS_SYSPROTO_H_
struct issetugid_args {
	int dummy;
};
#endif
/* ARGSUSED */
int
issetugid(p, uap)
	register struct proc *p;
	struct issetugid_args *uap;
{
	/*
	 * Note: OpenBSD sets a P_SUGIDEXEC flag set at execve() time,
	 * we use P_SUGID because we consider changing the owners as
	 * "tainting" as well.
	 * This is significant for procs that start as root and "become"
	 * a user without an exec - programs cannot know *everything*
	 * that libc *might* have put in their data segment.
	 */
	p->p_retval[0] = (p->p_flag & P_SUGID) ? 1 : 0;
	return (0);
}

/*
 * Check if gid is a member of the group set.
 */
int
groupmember(gid, cred)
	gid_t gid;
	register struct ucred *cred;
{
	register gid_t *gp;
	gid_t *egp;

	egp = &(cred->cr_groups[cred->cr_ngroups]);
	for (gp = cred->cr_groups; gp < egp; gp++)
		if (*gp == gid)
			return (1);
	return (0);
}

/*
 * Test whether the specified credentials imply "super-user"
 * privilege; if so, and we have accounting info, set the flag
 * indicating use of super-powers.
 * Returns 0 or error.
 */
int
suser(p)
	struct proc *p;
{
	return suser_xxx(0, p, 0);
}

int
suser_xxx(cred, proc, flag)
	struct ucred *cred;
	struct proc *proc;
	int flag;
{
	if (!cred && !proc) {
		printf("suser_xxx(): THINK!\n");
		return (EPERM);
	}
	if (!cred) 
		cred = proc->p_ucred;
	if (cred->cr_uid != 0) 
		return (EPERM);
	if (proc && proc->p_prison && !(flag & PRISON_ROOT))
		return (EPERM);
	if (proc)
		proc->p_acflag |= ASU;
	return (0);
}

/*
 * Return zero if p1 can fondle p2, return errno (EPERM/ESRCH) otherwise.
 */

int
p_trespass(struct proc *p1, struct proc *p2)
{

	if (p1 == p2)
		return (0);
	if (!PRISON_CHECK(p1, p2))
		return (ESRCH);
	if (p1->p_cred->p_ruid == p2->p_cred->p_ruid)
		return (0);
	if (p1->p_ucred->cr_uid == p2->p_cred->p_ruid)
		return (0);
	if (p1->p_cred->p_ruid == p2->p_ucred->cr_uid)
		return (0);
	if (p1->p_ucred->cr_uid == p2->p_ucred->cr_uid)
		return (0);
	if (!suser_xxx(0, p1, PRISON_ROOT))
		return (0);
	return (EPERM);
}

/*
 * Allocate a zeroed cred structure.
 */
struct ucred *
crget()
{
	register struct ucred *cr;

	MALLOC(cr, struct ucred *, sizeof(*cr), M_CRED, M_WAITOK);
	bzero((caddr_t)cr, sizeof(*cr));
	cr->cr_ref = 1;
	return (cr);
}

/*
 * Claim another reference to a ucred structure
 */
void
crhold(cr) 
        struct ucred *cr;
{
	cr->cr_ref++;
}

/*
 * Free a cred structure.
 * Throws away space when ref count gets to 0.
 */
void
crfree(cr)
	struct ucred *cr;
{
	int s;

	s = splhigh();
	if (cr->cr_ref == 0)
		panic("Freeing already free credential! %p", cr);
	
	if (--cr->cr_ref == 0) {
		/*
		 * Some callers of crget(), such as nfs_statfs(),
		 * allocate a temporary credential, but don't
		 * allocate a uidinfo structure.
		 */
		if (cr->cr_uidinfo != NULL)
			uifree(cr->cr_uidinfo);
		FREE((caddr_t)cr, M_CRED);
	}
	splx(s);
}

/*
 * Copy cred structure to a new one and free the old one.
 */
struct ucred *
crcopy(cr)
	struct ucred *cr;
{
	struct ucred *newcr;

	if (cr->cr_ref == 1)
		return (cr);
	newcr = crget();
	*newcr = *cr;
	uihold(newcr->cr_uidinfo);
	crfree(cr);
	newcr->cr_ref = 1;
	return (newcr);
}

/*
 * Dup cred struct to a new held one.
 */
struct ucred *
crdup(cr)
	struct ucred *cr;
{
	struct ucred *newcr;

	newcr = crget();
	*newcr = *cr;
	uihold(newcr->cr_uidinfo);
	newcr->cr_ref = 1;
	return (newcr);
}

/*
 * Fill in a struct xucred based on a struct ucred.
 */
void
cru2x(cr, xcr)
	struct ucred *cr;
	struct xucred *xcr;
{

	bzero(xcr, sizeof(*xcr));
	xcr->cr_version = XUCRED_VERSION;
	xcr->cr_uid = cr->cr_uid;
	xcr->cr_ngroups = cr->cr_ngroups;
	bcopy(cr->cr_groups, xcr->cr_groups, sizeof(cr->cr_groups));
}

/*
 * Get login name, if available.
 */
#ifndef _SYS_SYSPROTO_H_
struct getlogin_args {
	char	*namebuf;
	u_int	namelen;
};
#endif
/* ARGSUSED */
int
getlogin(p, uap)
	struct proc *p;
	struct getlogin_args *uap;
{

	if (uap->namelen > MAXLOGNAME)
		uap->namelen = MAXLOGNAME;
	return (copyout((caddr_t) p->p_pgrp->pg_session->s_login,
	    (caddr_t) uap->namebuf, uap->namelen));
}

/*
 * Set login name.
 */
#ifndef _SYS_SYSPROTO_H_
struct setlogin_args {
	char	*namebuf;
};
#endif
/* ARGSUSED */
int
setlogin(p, uap)
	struct proc *p;
	struct setlogin_args *uap;
{
	int error;
	char logintmp[MAXLOGNAME];

	if ((error = suser_xxx(0, p, PRISON_ROOT)))
		return (error);
	error = copyinstr((caddr_t) uap->namebuf, (caddr_t) logintmp,
	    sizeof(logintmp), (size_t *)0);
	if (error == ENAMETOOLONG)
		error = EINVAL;
	else if (!error)
		(void) memcpy(p->p_pgrp->pg_session->s_login, logintmp,
		    sizeof(logintmp));
	return (error);
}

void
setsugid(p)
	struct proc *p;
{
	p->p_flag |= P_SUGID;
	if (!(p->p_pfsflags & PF_ISUGID))
		p->p_stops = 0;
}

/*
 * Helper function to change the effective uid of a process
 */
void
change_euid(p, euid)
	struct	proc *p;
	uid_t	euid;
{
	struct	pcred *pc;
	struct	uidinfo *uip;

	pc = p->p_cred;
	/*
	 * crcopy is essentially a NOP if ucred has a reference count
	 * of 1, which is true if it has already been copied.
	 */
	pc->pc_ucred = crcopy(pc->pc_ucred);
	uip = pc->pc_ucred->cr_uidinfo;
	pc->pc_ucred->cr_uid = euid;
	pc->pc_ucred->cr_uidinfo = uifind(euid);
	uifree(uip);
}

/*
 * Helper function to change the real uid of a process
 *
 * The per-uid process count for this process is transfered from
 * the old uid to the new uid.
 */
void
change_ruid(p, ruid)
	struct	proc *p;
	uid_t	ruid;
{
	struct	pcred *pc;
	struct	uidinfo *uip;

	pc = p->p_cred;
	(void)chgproccnt(pc->p_uidinfo, -1, 0);
	uip = pc->p_uidinfo;
	/* It is assumed that pcred is not shared between processes */
	pc->p_ruid = ruid;
	pc->p_uidinfo = uifind(ruid);
	(void)chgproccnt(pc->p_uidinfo, 1, 0);
	uifree(uip);
}
