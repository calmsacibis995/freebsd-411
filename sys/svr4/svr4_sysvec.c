/*
 * Copyright (c) 1998 Mark Newton
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
 *      This product includes software developed by Christos Zoulas.
 * 4. The name of the author may not be used to endorse or promote products
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
 * $FreeBSD: src/sys/svr4/svr4_sysvec.c,v 1.10.2.2 2002/07/09 14:12:43 robert Exp $
 */

/* XXX we use functions that might not exist. */
#include "opt_compat.h"

#ifndef COMPAT_43
#error "Unable to compile SVR4-emulator due to missing COMPAT_43 option!"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/sysent.h>
#include <sys/imgact.h>
#include <sys/imgact_elf.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/module.h>
#include <vm/vm.h>
#include <vm/vm_zone.h>
#include <sys/exec.h>
#include <sys/kernel.h>
#include <machine/cpu.h>
#include <netinet/in.h>

#include <svr4/svr4.h>
#include <svr4/svr4_types.h>
#include <svr4/svr4_syscall.h>
#include <svr4/svr4_signal.h>
#include <svr4/svr4_sockio.h>
#include <svr4/svr4_socket.h>
#include <svr4/svr4_errno.h>
#include <svr4/svr4_proto.h>
#include <svr4/svr4_siginfo.h>
#include <svr4/svr4_util.h>

int bsd_to_svr4_errno[ELAST+1] = {
        0,
        SVR4_EPERM,
        SVR4_ENOENT,
        SVR4_ESRCH,
        SVR4_EINTR,
        SVR4_EIO,
        SVR4_ENXIO,
        SVR4_E2BIG,
        SVR4_ENOEXEC,
        SVR4_EBADF,
        SVR4_ECHILD,
        SVR4_EDEADLK,
        SVR4_ENOMEM,
        SVR4_EACCES,
        SVR4_EFAULT,
        SVR4_ENOTBLK,
        SVR4_EBUSY,
        SVR4_EEXIST,
        SVR4_EXDEV,
        SVR4_ENODEV,
        SVR4_ENOTDIR,
        SVR4_EISDIR,
        SVR4_EINVAL,
        SVR4_ENFILE,
        SVR4_EMFILE,
        SVR4_ENOTTY,
        SVR4_ETXTBSY,
        SVR4_EFBIG,
        SVR4_ENOSPC,
        SVR4_ESPIPE,
        SVR4_EROFS,
        SVR4_EMLINK,
        SVR4_EPIPE,
        SVR4_EDOM,
        SVR4_ERANGE,
        SVR4_EAGAIN,
        SVR4_EINPROGRESS,
        SVR4_EALREADY,
        SVR4_ENOTSOCK,
        SVR4_EDESTADDRREQ,
        SVR4_EMSGSIZE,
        SVR4_EPROTOTYPE,
        SVR4_ENOPROTOOPT,
        SVR4_EPROTONOSUPPORT,
        SVR4_ESOCKTNOSUPPORT,
        SVR4_EOPNOTSUPP,
        SVR4_EPFNOSUPPORT,
        SVR4_EAFNOSUPPORT,
        SVR4_EADDRINUSE,
        SVR4_EADDRNOTAVAIL,
        SVR4_ENETDOWN,
        SVR4_ENETUNREACH,
        SVR4_ENETRESET,
        SVR4_ECONNABORTED,
        SVR4_ECONNRESET,
        SVR4_ENOBUFS,
        SVR4_EISCONN,
        SVR4_ENOTCONN,
        SVR4_ESHUTDOWN,
        SVR4_ETOOMANYREFS,
        SVR4_ETIMEDOUT,
        SVR4_ECONNREFUSED,
        SVR4_ELOOP,
        SVR4_ENAMETOOLONG,
        SVR4_EHOSTDOWN,
        SVR4_EHOSTUNREACH,
        SVR4_ENOTEMPTY,
        SVR4_EPROCLIM,
        SVR4_EUSERS,
        SVR4_EDQUOT,
        SVR4_ESTALE,
        SVR4_EREMOTE,
        SVR4_EBADRPC,
        SVR4_ERPCMISMATCH,
        SVR4_EPROGUNAVAIL,
        SVR4_EPROGMISMATCH,
        SVR4_EPROCUNAVAIL,
        SVR4_ENOLCK,
        SVR4_ENOSYS,
        SVR4_EFTYPE,
        SVR4_EAUTH,
        SVR4_ENEEDAUTH,
        SVR4_EIDRM,
        SVR4_ENOMSG,
};


static int 	svr4_fixup(long **stack_base, struct image_params *imgp);

extern struct sysent svr4_sysent[];
#undef szsigcode
#undef sigcode

extern int svr4_szsigcode;
extern char svr4_sigcode[];

struct sysentvec svr4_sysvec = {
  SVR4_SYS_MAXSYSCALL,
  svr4_sysent,
  0xff,
  SVR4_SIGTBLSZ,
  bsd_to_svr4_sig,
  ELAST,  /* ELAST */
  bsd_to_svr4_errno,
  0,
  svr4_fixup,
  svr4_sendsig,
  svr4_sigcode,
  &svr4_szsigcode,
  NULL,
  "SVR4",
  elf_coredump,
  NULL,
  SVR4_MINSIGSTKSZ
};

Elf32_Brandinfo svr4_brand = {
  ELFOSABI_SYSV,
  "SVR4",
  "/compat/svr4",
  "/lib/libc.so.1",
  &svr4_sysvec
};

const char      svr4_emul_path[] = "/compat/svr4";

static int
svr4_fixup(long **stack_base, struct image_params *imgp)
{
	Elf32_Auxargs *args = (Elf32_Auxargs *)imgp->auxargs;
	long *pos;
             
	pos = *stack_base + (imgp->argc + imgp->envc + 2);  
    
	if (args->trace) {
		AUXARGS_ENTRY(pos, AT_DEBUG, 1);
	}
	if (args->execfd != -1) {
		AUXARGS_ENTRY(pos, AT_EXECFD, args->execfd);
	}       
	AUXARGS_ENTRY(pos, AT_PHDR, args->phdr);
	AUXARGS_ENTRY(pos, AT_PHENT, args->phent);
	AUXARGS_ENTRY(pos, AT_PHNUM, args->phnum);
	AUXARGS_ENTRY(pos, AT_PAGESZ, args->pagesz);
	AUXARGS_ENTRY(pos, AT_FLAGS, args->flags);
	AUXARGS_ENTRY(pos, AT_ENTRY, args->entry);
	AUXARGS_ENTRY(pos, AT_BASE, args->base);
	AUXARGS_ENTRY(pos, AT_UID, imgp->proc->p_cred->p_ruid);
	AUXARGS_ENTRY(pos, AT_EUID, imgp->proc->p_cred->p_svuid);
	AUXARGS_ENTRY(pos, AT_GID, imgp->proc->p_cred->p_rgid);
	AUXARGS_ENTRY(pos, AT_EGID, imgp->proc->p_cred->p_svgid);
	AUXARGS_ENTRY(pos, AT_NULL, 0);
	
	free(imgp->auxargs, M_TEMP);      
	imgp->auxargs = NULL;

	(*stack_base)--;
	**stack_base = (int)imgp->argc;
	return 0;
}

/*
 * Search an alternate path before passing pathname arguments on
 * to system calls. Useful for keeping a seperate 'emulation tree'.
 *
 * If cflag is set, we check if an attempt can be made to create
 * the named file, i.e. we check if the directory it should
 * be in exists.
 *
 * Code shamelessly stolen by Mark Newton from IBCS2 emulation code.
 */
int
svr4_emul_find(p, sgp, prefix, path, pbuf, cflag)
	struct proc	 *p;
	caddr_t		 *sgp;		/* Pointer to stackgap memory */
	const char	 *prefix;
	char		 *path;
	char		**pbuf;
	int		  cflag;
{
	struct nameidata	 nd;
	struct nameidata	 ndroot;
	struct vattr		 vat;
	struct vattr		 vatroot;
	int			 error;
	char			*ptr, *buf, *cp;
	size_t			 sz, len;

	buf = (char *) malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	*pbuf = path;

	for (ptr = buf; (*ptr = *prefix) != '\0'; ptr++, prefix++)
		continue;

	sz = MAXPATHLEN - (ptr - buf);

	/* 
	 * If sgp is not given then the path is already in kernel space
	 */
	if (sgp == NULL)
		error = copystr(path, ptr, sz, &len);
	else
		error = copyinstr(path, ptr, sz, &len);

	if (error) {
		free(buf, M_TEMP);
		return error;
	}

	if (*ptr != '/') {
		free(buf, M_TEMP);
		return EINVAL;
	}

	/*
	 * We know that there is a / somewhere in this pathname.
	 * Search backwards for it, to find the file's parent dir
	 * to see if it exists in the alternate tree. If it does,
	 * and we want to create a file (cflag is set). We don't
	 * need to worry about the root comparison in this case.
	 */

	if (cflag) {
		for (cp = &ptr[len] - 1; *cp != '/'; cp--);
		*cp = '\0';

		NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, buf, p);

		if ((error = namei(&nd)) != 0) {
			free(buf, M_TEMP);
			return error;
		}
		NDFREE(&nd, NDF_ONLY_PNBUF);

		*cp = '/';
	}
	else {
		NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, buf, p);

		if ((error = namei(&nd)) != 0) {
			free(buf, M_TEMP);
			return error;
		}
		NDFREE(&nd, NDF_ONLY_PNBUF);

		/*
		 * We now compare the vnode of the svr4_root to the one
		 * vnode asked. If they resolve to be the same, then we
		 * ignore the match so that the real root gets used.
		 * This avoids the problem of traversing "../.." to find the
		 * root directory and never finding it, because "/" resolves
		 * to the emulation root directory. This is expensive :-(
		 */
		NDINIT(&ndroot, LOOKUP, FOLLOW, UIO_SYSSPACE, svr4_emul_path,
		       p);

		if ((error = namei(&ndroot)) != 0) {
			/* Cannot happen! */
			free(buf, M_TEMP);
			vrele(nd.ni_vp);
			return error;
		}
		NDFREE(&ndroot, NDF_ONLY_PNBUF);

		if ((error = VOP_GETATTR(nd.ni_vp, &vat, p->p_ucred, p)) != 0) {
			goto done;
		}

		if ((error = VOP_GETATTR(ndroot.ni_vp, &vatroot, p->p_ucred, p))
		    != 0) {
			goto done;
		}

		if (vat.va_fsid == vatroot.va_fsid &&
		    vat.va_fileid == vatroot.va_fileid) {
			error = ENOENT;
			goto done;
		}

	}
	if (sgp == NULL)
		*pbuf = buf;
	else {
		sz = &ptr[len] - buf;
		*pbuf = stackgap_alloc(sgp, sz + 1);
		error = copyout(buf, *pbuf, sz);
		free(buf, M_TEMP);
	}


done:
	vrele(nd.ni_vp);
	if (!cflag)
		vrele(ndroot.ni_vp);
	return error;
}

static int
svr4_elf_modevent(module_t mod, int type, void *data)
{
	int error;

	error = 0;

	switch(type) {
	case MOD_LOAD:
		if (elf_insert_brand_entry(&svr4_brand) < 0)
			error = EINVAL;
		if (error)
			printf("cannot insert svr4 elf brand handler\n");
		else if (bootverbose)
			printf("svr4 ELF exec handler installed\n");
		break;
	case MOD_UNLOAD:
		/* Only allow the emulator to be removed if it isn't in use. */
		if (elf_brand_inuse(&svr4_brand) != 0) {
			error = EBUSY;
		} else if (elf_remove_brand_entry(&svr4_brand) < 0) {
			error = EINVAL;
		}

		if (error)
			printf("Could not deinstall ELF interpreter entry (error %d)\n",
			       error);
		else if (bootverbose)
			printf("svr4 ELF exec handler removed\n");
		break;
	default:
		break;
	}
	return error;
}

static moduledata_t svr4_elf_mod = {
	"svr4elf",
	svr4_elf_modevent,
	0
};
DECLARE_MODULE(svr4elf, svr4_elf_mod, SI_SUB_EXEC, SI_ORDER_ANY);
