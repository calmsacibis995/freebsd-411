/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
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
 *	@(#)nfs_vnops.c	8.16 (Berkeley) 5/27/95
 * $FreeBSD: src/sys/nfs/nfs_vnops.c,v 1.150.2.8 2004/01/10 08:24:33 tjr Exp $
 */


/*
 * vnode op calls for Sun NFS version 2 and 3
 */

#include "opt_inet.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/resourcevar.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/namei.h>
#include <sys/socket.h>
#include <sys/vnode.h>
#include <sys/dirent.h>
#include <sys/fcntl.h>
#include <sys/lockf.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/conf.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_zone.h>

#include <miscfs/fifofs/fifo.h>

#include <nfs/rpcv2.h>
#include <nfs/nfsproto.h>
#include <nfs/nfs.h>
#include <nfs/nfsnode.h>
#include <nfs/nfsmount.h>
#include <nfs/xdr_subs.h>
#include <nfs/nfsm_subs.h>
#include <nfs/nqnfs.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_var.h>

/* Defs */
#define	TRUE	1
#define	FALSE	0

/*
 * Ifdef for FreeBSD-current merged buffer cache. It is unfortunate that these
 * calls are not in getblk() and brelse() so that they would not be necessary
 * here.
 */
#ifndef B_VMIO
#define vfs_busy_pages(bp, f)
#endif

static int	nfsspec_read __P((struct vop_read_args *));
static int	nfsspec_write __P((struct vop_write_args *));
static int	nfsfifo_read __P((struct vop_read_args *));
static int	nfsfifo_write __P((struct vop_write_args *));
static int	nfsspec_close __P((struct vop_close_args *));
static int	nfsfifo_close __P((struct vop_close_args *));
#define nfs_poll vop_nopoll
static int	nfs_flush __P((struct vnode *,struct ucred *,int,struct proc *,int));
static int	nfs_setattrrpc __P((struct vnode *,struct vattr *,struct ucred *,struct proc *));
static	int	nfs_lookup __P((struct vop_lookup_args *));
static	int	nfs_create __P((struct vop_create_args *));
static	int	nfs_mknod __P((struct vop_mknod_args *));
static	int	nfs_open __P((struct vop_open_args *));
static	int	nfs_close __P((struct vop_close_args *));
static	int	nfs_access __P((struct vop_access_args *));
static	int	nfs_getattr __P((struct vop_getattr_args *));
static	int	nfs_setattr __P((struct vop_setattr_args *));
static	int	nfs_read __P((struct vop_read_args *));
static	int	nfs_mmap __P((struct vop_mmap_args *));
static	int	nfs_fsync __P((struct vop_fsync_args *));
static	int	nfs_remove __P((struct vop_remove_args *));
static	int	nfs_link __P((struct vop_link_args *));
static	int	nfs_rename __P((struct vop_rename_args *));
static	int	nfs_mkdir __P((struct vop_mkdir_args *));
static	int	nfs_rmdir __P((struct vop_rmdir_args *));
static	int	nfs_symlink __P((struct vop_symlink_args *));
static	int	nfs_readdir __P((struct vop_readdir_args *));
static	int	nfs_bmap __P((struct vop_bmap_args *));
static	int	nfs_strategy __P((struct vop_strategy_args *));
static	int	nfs_lookitup __P((struct vnode *, const char *, int,
			struct ucred *, struct proc *, struct nfsnode **));
static	int	nfs_sillyrename __P((struct vnode *,struct vnode *,struct componentname *));
static int	nfsspec_access __P((struct vop_access_args *));
static int	nfs_readlink __P((struct vop_readlink_args *));
static int	nfs_print __P((struct vop_print_args *));
static int	nfs_advlock __P((struct vop_advlock_args *));
static int	nfs_bwrite __P((struct vop_bwrite_args *));
/*
 * Global vfs data structures for nfs
 */
vop_t **nfsv2_vnodeop_p;
static struct vnodeopv_entry_desc nfsv2_vnodeop_entries[] = {
	{ &vop_default_desc,		(vop_t *) vop_defaultop },
	{ &vop_access_desc,		(vop_t *) nfs_access },
	{ &vop_advlock_desc,		(vop_t *) nfs_advlock },
	{ &vop_bmap_desc,		(vop_t *) nfs_bmap },
	{ &vop_bwrite_desc,		(vop_t *) nfs_bwrite },
	{ &vop_close_desc,		(vop_t *) nfs_close },
	{ &vop_create_desc,		(vop_t *) nfs_create },
	{ &vop_fsync_desc,		(vop_t *) nfs_fsync },
	{ &vop_getattr_desc,		(vop_t *) nfs_getattr },
	{ &vop_getpages_desc,		(vop_t *) nfs_getpages },
	{ &vop_putpages_desc,		(vop_t *) nfs_putpages },
	{ &vop_inactive_desc,		(vop_t *) nfs_inactive },
	{ &vop_islocked_desc,		(vop_t *) vop_stdislocked },
	{ &vop_lease_desc,		(vop_t *) vop_null },
	{ &vop_link_desc,		(vop_t *) nfs_link },
	{ &vop_lock_desc,		(vop_t *) vop_sharedlock },
	{ &vop_lookup_desc,		(vop_t *) nfs_lookup },
	{ &vop_mkdir_desc,		(vop_t *) nfs_mkdir },
	{ &vop_mknod_desc,		(vop_t *) nfs_mknod },
	{ &vop_mmap_desc,		(vop_t *) nfs_mmap },
	{ &vop_open_desc,		(vop_t *) nfs_open },
	{ &vop_poll_desc,		(vop_t *) nfs_poll },
	{ &vop_print_desc,		(vop_t *) nfs_print },
	{ &vop_read_desc,		(vop_t *) nfs_read },
	{ &vop_readdir_desc,		(vop_t *) nfs_readdir },
	{ &vop_readlink_desc,		(vop_t *) nfs_readlink },
	{ &vop_reclaim_desc,		(vop_t *) nfs_reclaim },
	{ &vop_remove_desc,		(vop_t *) nfs_remove },
	{ &vop_rename_desc,		(vop_t *) nfs_rename },
	{ &vop_rmdir_desc,		(vop_t *) nfs_rmdir },
	{ &vop_setattr_desc,		(vop_t *) nfs_setattr },
	{ &vop_strategy_desc,		(vop_t *) nfs_strategy },
	{ &vop_symlink_desc,		(vop_t *) nfs_symlink },
	{ &vop_unlock_desc,		(vop_t *) vop_stdunlock },
	{ &vop_write_desc,		(vop_t *) nfs_write },
	{ NULL, NULL }
};
static struct vnodeopv_desc nfsv2_vnodeop_opv_desc =
	{ &nfsv2_vnodeop_p, nfsv2_vnodeop_entries };
VNODEOP_SET(nfsv2_vnodeop_opv_desc);

/*
 * Special device vnode ops
 */
vop_t **spec_nfsv2nodeop_p;
static struct vnodeopv_entry_desc nfsv2_specop_entries[] = {
	{ &vop_default_desc,		(vop_t *) spec_vnoperate },
	{ &vop_access_desc,		(vop_t *) nfsspec_access },
	{ &vop_close_desc,		(vop_t *) nfsspec_close },
	{ &vop_fsync_desc,		(vop_t *) nfs_fsync },
	{ &vop_getattr_desc,		(vop_t *) nfs_getattr },
	{ &vop_inactive_desc,		(vop_t *) nfs_inactive },
	{ &vop_islocked_desc,		(vop_t *) vop_stdislocked },
	{ &vop_lock_desc,		(vop_t *) vop_sharedlock },
	{ &vop_print_desc,		(vop_t *) nfs_print },
	{ &vop_read_desc,		(vop_t *) nfsspec_read },
	{ &vop_reclaim_desc,		(vop_t *) nfs_reclaim },
	{ &vop_setattr_desc,		(vop_t *) nfs_setattr },
	{ &vop_unlock_desc,		(vop_t *) vop_stdunlock },
	{ &vop_write_desc,		(vop_t *) nfsspec_write },
	{ NULL, NULL }
};
static struct vnodeopv_desc spec_nfsv2nodeop_opv_desc =
	{ &spec_nfsv2nodeop_p, nfsv2_specop_entries };
VNODEOP_SET(spec_nfsv2nodeop_opv_desc);

vop_t **fifo_nfsv2nodeop_p;
static struct vnodeopv_entry_desc nfsv2_fifoop_entries[] = {
	{ &vop_default_desc,		(vop_t *) fifo_vnoperate },
	{ &vop_access_desc,		(vop_t *) nfsspec_access },
	{ &vop_close_desc,		(vop_t *) nfsfifo_close },
	{ &vop_fsync_desc,		(vop_t *) nfs_fsync },
	{ &vop_getattr_desc,		(vop_t *) nfs_getattr },
	{ &vop_inactive_desc,		(vop_t *) nfs_inactive },
	{ &vop_islocked_desc,		(vop_t *) vop_stdislocked },
	{ &vop_lock_desc,		(vop_t *) vop_sharedlock },
	{ &vop_print_desc,		(vop_t *) nfs_print },
	{ &vop_read_desc,		(vop_t *) nfsfifo_read },
	{ &vop_reclaim_desc,		(vop_t *) nfs_reclaim },
	{ &vop_setattr_desc,		(vop_t *) nfs_setattr },
	{ &vop_unlock_desc,		(vop_t *) vop_stdunlock },
	{ &vop_write_desc,		(vop_t *) nfsfifo_write },
	{ NULL, NULL }
};
static struct vnodeopv_desc fifo_nfsv2nodeop_opv_desc =
	{ &fifo_nfsv2nodeop_p, nfsv2_fifoop_entries };
VNODEOP_SET(fifo_nfsv2nodeop_opv_desc);

static int	nfs_mknodrpc __P((struct vnode *dvp, struct vnode **vpp,
				  struct componentname *cnp,
				  struct vattr *vap));
static int	nfs_removerpc __P((struct vnode *dvp, const char *name,
				   int namelen,
				   struct ucred *cred, struct proc *proc));
static int	nfs_renamerpc __P((struct vnode *fdvp, const char *fnameptr,
				   int fnamelen, struct vnode *tdvp,
				   const char *tnameptr, int tnamelen,
				   struct ucred *cred, struct proc *proc));
static int	nfs_renameit __P((struct vnode *sdvp,
				  struct componentname *scnp,
				  struct sillyrename *sp));

/*
 * Global variables
 */
extern u_int32_t nfs_true, nfs_false;
extern u_int32_t nfs_xdrneg1;
extern struct nfsstats nfsstats;
extern nfstype nfsv3_type[9];
struct proc *nfs_iodwant[NFS_MAXASYNCDAEMON];
struct nfsmount *nfs_iodmount[NFS_MAXASYNCDAEMON];
int nfs_numasync = 0;
#define	DIRHDSIZ	(sizeof (struct dirent) - (MAXNAMLEN + 1))

SYSCTL_DECL(_vfs_nfs);

static int	nfsaccess_cache_timeout = NFS_MAXATTRTIMO;
SYSCTL_INT(_vfs_nfs, OID_AUTO, access_cache_timeout, CTLFLAG_RW, 
	   &nfsaccess_cache_timeout, 0, "NFS ACCESS cache timeout");

static int	nfsv3_commit_on_close = 0;
SYSCTL_INT(_vfs_nfs, OID_AUTO, nfsv3_commit_on_close, CTLFLAG_RW, 
	   &nfsv3_commit_on_close, 0, "write+commit on close, else only write");
#if 0
SYSCTL_INT(_vfs_nfs, OID_AUTO, access_cache_hits, CTLFLAG_RD, 
	   &nfsstats.accesscache_hits, 0, "NFS ACCESS cache hit count");

SYSCTL_INT(_vfs_nfs, OID_AUTO, access_cache_misses, CTLFLAG_RD, 
	   &nfsstats.accesscache_misses, 0, "NFS ACCESS cache miss count");
#endif

#define	NFSV3ACCESS_ALL (NFSV3ACCESS_READ | NFSV3ACCESS_MODIFY		\
			 | NFSV3ACCESS_EXTEND | NFSV3ACCESS_EXECUTE	\
			 | NFSV3ACCESS_DELETE | NFSV3ACCESS_LOOKUP)
static int
nfs3_access_otw(struct vnode *vp,
		int wmode,
		struct proc *p,
		struct ucred *cred)
{
	const int v3 = 1;
	u_int32_t *tl;
	int error = 0, attrflag;
	
	struct mbuf *mreq, *mrep, *md, *mb, *mb2;
	caddr_t bpos, dpos, cp2;
	register int32_t t1, t2;
	register caddr_t cp;
	u_int32_t rmode;
	struct nfsnode *np = VTONFS(vp);

	nfsstats.rpccnt[NFSPROC_ACCESS]++;
	nfsm_reqhead(vp, NFSPROC_ACCESS, NFSX_FH(v3) + NFSX_UNSIGNED);
	nfsm_fhtom(vp, v3);
	nfsm_build(tl, u_int32_t *, NFSX_UNSIGNED);
	*tl = txdr_unsigned(wmode); 
	nfsm_request(vp, NFSPROC_ACCESS, p, cred);
	nfsm_postop_attr(vp, attrflag);
	if (!error) {
		nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED);
		rmode = fxdr_unsigned(u_int32_t, *tl);
		np->n_mode = rmode;
		np->n_modeuid = cred->cr_uid;
		np->n_modestamp = time_second;
	}
	nfsm_reqdone;
	return error;
}

/*
 * nfs access vnode op.
 * For nfs version 2, just return ok. File accesses may fail later.
 * For nfs version 3, use the access rpc to check accessibility. If file modes
 * are changed on the server, accesses might still fail later.
 */
static int
nfs_access(ap)
	struct vop_access_args /* {
		struct vnode *a_vp;
		int  a_mode;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	int error = 0;
	u_int32_t mode, wmode;
	int v3 = NFS_ISV3(vp);
	struct nfsnode *np = VTONFS(vp);

	/*
	 * Disallow write attempts on filesystems mounted read-only;
	 * unless the file is a socket, fifo, or a block or character
	 * device resident on the filesystem.
	 */
	if ((ap->a_mode & VWRITE) && (vp->v_mount->mnt_flag & MNT_RDONLY)) {
		switch (vp->v_type) {
		case VREG:
		case VDIR:
		case VLNK:
			return (EROFS);
		default:
			break;
		}
	}
	/*
	 * For nfs v3, check to see if we have done this recently, and if
	 * so return our cached result instead of making an ACCESS call.
	 * If not, do an access rpc, otherwise you are stuck emulating
	 * ufs_access() locally using the vattr. This may not be correct,
	 * since the server may apply other access criteria such as
	 * client uid-->server uid mapping that we do not know about.
	 */
	if (v3) {
		if (ap->a_mode & VREAD)
			mode = NFSV3ACCESS_READ;
		else
			mode = 0;
		if (vp->v_type != VDIR) {
			if (ap->a_mode & VWRITE)
				mode |= (NFSV3ACCESS_MODIFY | NFSV3ACCESS_EXTEND);
			if (ap->a_mode & VEXEC)
				mode |= NFSV3ACCESS_EXECUTE;
		} else {
			if (ap->a_mode & VWRITE)
				mode |= (NFSV3ACCESS_MODIFY | NFSV3ACCESS_EXTEND |
					 NFSV3ACCESS_DELETE);
			if (ap->a_mode & VEXEC)
				mode |= NFSV3ACCESS_LOOKUP;
		}
		/* XXX safety belt, only make blanket request if caching */
		if (nfsaccess_cache_timeout > 0) {
			wmode = NFSV3ACCESS_READ | NFSV3ACCESS_MODIFY | 
				NFSV3ACCESS_EXTEND | NFSV3ACCESS_EXECUTE | 
				NFSV3ACCESS_DELETE | NFSV3ACCESS_LOOKUP;
		} else {
			wmode = mode;
		}

		/*
		 * Does our cached result allow us to give a definite yes to
		 * this request?
		 */
		if ((time_second < (np->n_modestamp + nfsaccess_cache_timeout)) &&
		    (ap->a_cred->cr_uid == np->n_modeuid) &&
		    ((np->n_mode & mode) == mode)) {
			nfsstats.accesscache_hits++;
		} else {
			/*
			 * Either a no, or a don't know.  Go to the wire.
			 */
			nfsstats.accesscache_misses++;
		        error = nfs3_access_otw(vp, wmode, ap->a_p,ap->a_cred);
			if (!error) {
				if ((np->n_mode & mode) != mode) {
					error = EACCES;
				}
			}
		}
		return (error);
	} else {
		if ((error = nfsspec_access(ap)) != 0)
			return (error);

		/*
		 * Attempt to prevent a mapped root from accessing a file
		 * which it shouldn't.  We try to read a byte from the file
		 * if the user is root and the file is not zero length.
		 * After calling nfsspec_access, we should have the correct
		 * file size cached.
		 */
		if (ap->a_cred->cr_uid == 0 && (ap->a_mode & VREAD)
		    && VTONFS(vp)->n_size > 0) {
			struct iovec aiov;
			struct uio auio;
			char buf[1];

			aiov.iov_base = buf;
			aiov.iov_len = 1;
			auio.uio_iov = &aiov;
			auio.uio_iovcnt = 1;
			auio.uio_offset = 0;
			auio.uio_resid = 1;
			auio.uio_segflg = UIO_SYSSPACE;
			auio.uio_rw = UIO_READ;
			auio.uio_procp = ap->a_p;

			if (vp->v_type == VREG)
				error = nfs_readrpc(vp, &auio, ap->a_cred);
			else if (vp->v_type == VDIR) {
				char* bp;
				bp = malloc(NFS_DIRBLKSIZ, M_TEMP, M_WAITOK);
				aiov.iov_base = bp;
				aiov.iov_len = auio.uio_resid = NFS_DIRBLKSIZ;
				error = nfs_readdirrpc(vp, &auio, ap->a_cred);
				free(bp, M_TEMP);
			} else if (vp->v_type == VLNK)
				error = nfs_readlinkrpc(vp, &auio, ap->a_cred);
			else
				error = EACCES;
		}
		return (error);
	}
}

/*
 * nfs open vnode op
 * Check to see if the type is ok
 * and that deletion is not in progress.
 * For paged in text files, you will need to flush the page cache
 * if consistency is lost.
 */
/* ARGSUSED */
static int
nfs_open(ap)
	struct vop_open_args /* {
		struct vnode *a_vp;
		int  a_mode;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	struct nfsnode *np = VTONFS(vp);
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	struct vattr vattr;
	int error;

	if (vp->v_type != VREG && vp->v_type != VDIR && vp->v_type != VLNK) {
#ifdef DIAGNOSTIC
		printf("open eacces vtyp=%d\n",vp->v_type);
#endif
		return (EOPNOTSUPP);
	}
	/*
	 * Get a valid lease. If cached data is stale, flush it.
	 */
	if (nmp->nm_flag & NFSMNT_NQNFS) {
		if (NQNFS_CKINVALID(vp, np, ND_READ)) {
		    do {
			error = nqnfs_getlease(vp, ND_READ, ap->a_cred,
			    ap->a_p);
		    } while (error == NQNFS_EXPIRED);
		    if (error)
			return (error);
		    if (np->n_lrev != np->n_brev ||
			(np->n_flag & NQNFSNONCACHE)) {
			if ((error = nfs_vinvalbuf(vp, V_SAVE, ap->a_cred,
				ap->a_p, 1)) == EINTR)
				return (error);
			np->n_brev = np->n_lrev;
		    }
		}
	} else {
		if (np->n_flag & NMODIFIED) {
			if ((error = nfs_vinvalbuf(vp, V_SAVE, ap->a_cred,
				ap->a_p, 1)) == EINTR)
				return (error);
			np->n_attrstamp = 0;
			if (vp->v_type == VDIR)
				np->n_direofoffset = 0;
			error = VOP_GETATTR(vp, &vattr, ap->a_cred, ap->a_p);
			if (error)
				return (error);
			np->n_mtime = vattr.va_mtime.tv_sec;
		} else {
			error = VOP_GETATTR(vp, &vattr, ap->a_cred, ap->a_p);
			if (error)
				return (error);
			if (np->n_mtime != vattr.va_mtime.tv_sec) {
				if (vp->v_type == VDIR)
					np->n_direofoffset = 0;
				if ((error = nfs_vinvalbuf(vp, V_SAVE,
					ap->a_cred, ap->a_p, 1)) == EINTR)
					return (error);
				np->n_mtime = vattr.va_mtime.tv_sec;
			}
		}
	}
	if ((nmp->nm_flag & NFSMNT_NQNFS) == 0)
		np->n_attrstamp = 0; /* For Open/Close consistency */
	return (0);
}

/*
 * nfs close vnode op
 * What an NFS client should do upon close after writing is a debatable issue.
 * Most NFS clients push delayed writes to the server upon close, basically for
 * two reasons:
 * 1 - So that any write errors may be reported back to the client process
 *     doing the close system call. By far the two most likely errors are
 *     NFSERR_NOSPC and NFSERR_DQUOT to indicate space allocation failure.
 * 2 - To put a worst case upper bound on cache inconsistency between
 *     multiple clients for the file.
 * There is also a consistency problem for Version 2 of the protocol w.r.t.
 * not being able to tell if other clients are writing a file concurrently,
 * since there is no way of knowing if the changed modify time in the reply
 * is only due to the write for this client.
 * (NFS Version 3 provides weak cache consistency data in the reply that
 *  should be sufficient to detect and handle this case.)
 *
 * The current code does the following:
 * for NFS Version 2 - play it safe and flush/invalidate all dirty buffers
 * for NFS Version 3 - flush dirty buffers to the server but don't invalidate
 *                     or commit them (this satisfies 1 and 2 except for the
 *                     case where the server crashes after this close but
 *                     before the commit RPC, which is felt to be "good
 *                     enough". Changing the last argument to nfs_flush() to
 *                     a 1 would force a commit operation, if it is felt a
 *                     commit is necessary now.
 * for NQNFS         - do nothing now, since 2 is dealt with via leases and
 *                     1 should be dealt with via an fsync() system call for
 *                     cases where write errors are important.
 */
/* ARGSUSED */
static int
nfs_close(ap)
	struct vop_close_args /* {
		struct vnodeop_desc *a_desc;
		struct vnode *a_vp;
		int  a_fflag;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct nfsnode *np = VTONFS(vp);
	int error = 0;

	if (vp->v_type == VREG) {
	    if ((VFSTONFS(vp->v_mount)->nm_flag & NFSMNT_NQNFS) == 0 &&
		(np->n_flag & NMODIFIED)) {
		if (NFS_ISV3(vp)) {
		    /*
		     * Under NFSv3 we have dirty buffers to dispose of.  We
		     * must flush them to the NFS server.  We have the option
		     * of waiting all the way through the commit rpc or just
		     * waiting for the initial write.  The default is to only
		     * wait through the initial write so the data is in the
		     * server's cache, which is roughly similar to the state
		     * a standard disk subsystem leaves the file in on close().
		     *
		     * We cannot clear the NMODIFIED bit in np->n_flag due to
		     * potential races with other processes, and certainly
		     * cannot clear it if we don't commit.
		     */
		    int cm = nfsv3_commit_on_close ? 1 : 0;
		    error = nfs_flush(vp, ap->a_cred, MNT_WAIT, ap->a_p, cm);
		    /* np->n_flag &= ~NMODIFIED; */
		} else {
		    error = nfs_vinvalbuf(vp, V_SAVE, ap->a_cred, ap->a_p, 1);
		}
		np->n_attrstamp = 0;
	    }
	    if (np->n_flag & NWRITEERR) {
		np->n_flag &= ~NWRITEERR;
		error = np->n_error;
	    }
	}
	return (error);
}

/*
 * nfs getattr call from vfs.
 */
static int
nfs_getattr(ap)
	struct vop_getattr_args /* {
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct nfsnode *np = VTONFS(vp);
	register caddr_t cp;
	register u_int32_t *tl;
	register int32_t t1, t2;
	caddr_t bpos, dpos;
	int error = 0;
	struct mbuf *mreq, *mrep, *md, *mb, *mb2;
	int v3 = NFS_ISV3(vp);
	
	/*
	 * Update local times for special files.
	 */
	if (np->n_flag & (NACC | NUPD))
		np->n_flag |= NCHG;
	/*
	 * First look in the cache.
	 */
	if (nfs_getattrcache(vp, ap->a_vap) == 0)
		return (0);

	if (v3 && nfsaccess_cache_timeout > 0) {
		nfsstats.accesscache_misses++;
		nfs3_access_otw(vp, NFSV3ACCESS_ALL, ap->a_p, ap->a_cred);
		if (nfs_getattrcache(vp, ap->a_vap) == 0)
			return (0);
	}

	nfsstats.rpccnt[NFSPROC_GETATTR]++;
	nfsm_reqhead(vp, NFSPROC_GETATTR, NFSX_FH(v3));
	nfsm_fhtom(vp, v3);
	nfsm_request(vp, NFSPROC_GETATTR, ap->a_p, ap->a_cred);
	if (!error) {
		nfsm_loadattr(vp, ap->a_vap);
	}
	nfsm_reqdone;
	return (error);
}

/*
 * nfs setattr call.
 */
static int
nfs_setattr(ap)
	struct vop_setattr_args /* {
		struct vnodeop_desc *a_desc;
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct nfsnode *np = VTONFS(vp);
	register struct vattr *vap = ap->a_vap;
	int error = 0;
	u_quad_t tsize;

#ifndef nolint
	tsize = (u_quad_t)0;
#endif

	/*
	 * Setting of flags is not supported.
	 */
	if (vap->va_flags != VNOVAL)
		return (EOPNOTSUPP);

	/*
	 * Disallow write attempts if the filesystem is mounted read-only.
	 */
  	if ((vap->va_flags != VNOVAL || vap->va_uid != (uid_t)VNOVAL ||
	    vap->va_gid != (gid_t)VNOVAL || vap->va_atime.tv_sec != VNOVAL ||
	    vap->va_mtime.tv_sec != VNOVAL || vap->va_mode != (mode_t)VNOVAL) &&
	    (vp->v_mount->mnt_flag & MNT_RDONLY))
		return (EROFS);
	if (vap->va_size != VNOVAL) {
 		switch (vp->v_type) {
 		case VDIR:
 			return (EISDIR);
 		case VCHR:
 		case VBLK:
 		case VSOCK:
 		case VFIFO:
			if (vap->va_mtime.tv_sec == VNOVAL &&
			    vap->va_atime.tv_sec == VNOVAL &&
			    vap->va_mode == (mode_t)VNOVAL &&
			    vap->va_uid == (uid_t)VNOVAL &&
			    vap->va_gid == (gid_t)VNOVAL)
				return (0);
 			vap->va_size = VNOVAL;
 			break;
 		default:
			/*
			 * Disallow write attempts if the filesystem is
			 * mounted read-only.
			 */
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);

			/*
			 * We run vnode_pager_setsize() early (why?),
			 * we must set np->n_size now to avoid vinvalbuf
			 * V_SAVE races that might setsize a lower
			 * value.
			 */

			tsize = np->n_size;
			error = nfs_meta_setsize(vp, ap->a_cred, 
						ap->a_p, vap->va_size);

 			if (np->n_flag & NMODIFIED) {
 			    if (vap->va_size == 0)
 				error = nfs_vinvalbuf(vp, 0,
 					ap->a_cred, ap->a_p, 1);
 			    else
 				error = nfs_vinvalbuf(vp, V_SAVE,
 					ap->a_cred, ap->a_p, 1);
 			    if (error) {
				np->n_size = tsize;
				vnode_pager_setsize(vp, np->n_size);
 				return (error);
			    }
 			}
			/*
			 * np->n_size has already been set to vap->va_size
			 * in nfs_meta_setsize(). We must set it again since
			 * nfs_loadattrcache() could be called through
			 * nfs_meta_setsize() and could modify np->n_size.
			 */
 			np->n_vattr.va_size = np->n_size = vap->va_size;
  		};
  	} else if ((vap->va_mtime.tv_sec != VNOVAL ||
		vap->va_atime.tv_sec != VNOVAL) && (np->n_flag & NMODIFIED) &&
		vp->v_type == VREG &&
  		(error = nfs_vinvalbuf(vp, V_SAVE, ap->a_cred,
		 ap->a_p, 1)) == EINTR)
		return (error);
	error = nfs_setattrrpc(vp, vap, ap->a_cred, ap->a_p);
	if (error && vap->va_size != VNOVAL) {
		np->n_size = np->n_vattr.va_size = tsize;
		vnode_pager_setsize(vp, np->n_size);
	}
	return (error);
}

/*
 * Do an nfs setattr rpc.
 */
static int
nfs_setattrrpc(vp, vap, cred, procp)
	register struct vnode *vp;
	register struct vattr *vap;
	struct ucred *cred;
	struct proc *procp;
{
	register struct nfsv2_sattr *sp;
	register caddr_t cp;
	register int32_t t1, t2;
	caddr_t bpos, dpos, cp2;
	u_int32_t *tl;
	int error = 0, wccflag = NFSV3_WCCRATTR;
	struct mbuf *mreq, *mrep, *md, *mb, *mb2;
	int v3 = NFS_ISV3(vp);

	nfsstats.rpccnt[NFSPROC_SETATTR]++;
	nfsm_reqhead(vp, NFSPROC_SETATTR, NFSX_FH(v3) + NFSX_SATTR(v3));
	nfsm_fhtom(vp, v3);
	if (v3) {
		nfsm_v3attrbuild(vap, TRUE);
		nfsm_build(tl, u_int32_t *, NFSX_UNSIGNED);
		*tl = nfs_false;
	} else {
		nfsm_build(sp, struct nfsv2_sattr *, NFSX_V2SATTR);
		if (vap->va_mode == (mode_t)VNOVAL)
			sp->sa_mode = nfs_xdrneg1;
		else
			sp->sa_mode = vtonfsv2_mode(vp->v_type, vap->va_mode);
		if (vap->va_uid == (uid_t)VNOVAL)
			sp->sa_uid = nfs_xdrneg1;
		else
			sp->sa_uid = txdr_unsigned(vap->va_uid);
		if (vap->va_gid == (gid_t)VNOVAL)
			sp->sa_gid = nfs_xdrneg1;
		else
			sp->sa_gid = txdr_unsigned(vap->va_gid);
		sp->sa_size = txdr_unsigned(vap->va_size);
		txdr_nfsv2time(&vap->va_atime, &sp->sa_atime);
		txdr_nfsv2time(&vap->va_mtime, &sp->sa_mtime);
	}
	nfsm_request(vp, NFSPROC_SETATTR, procp, cred);
	if (v3) {
		nfsm_wcc_data(vp, wccflag);
	} else
		nfsm_loadattr(vp, (struct vattr *)0);
	nfsm_reqdone;
	return (error);
}

/*
 * nfs lookup call, one step at a time...
 * First look in cache
 * If not found, unlock the directory nfsnode and do the rpc
 */
static int
nfs_lookup(ap)
	struct vop_lookup_args /* {
		struct vnodeop_desc *a_desc;
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
	} */ *ap;
{
	struct componentname *cnp = ap->a_cnp;
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	int flags = cnp->cn_flags;
	struct vnode *newvp;
	u_int32_t *tl;
	caddr_t cp;
	int32_t t1, t2;
	struct nfsmount *nmp;
	caddr_t bpos, dpos, cp2;
	struct mbuf *mreq, *mrep, *md, *mb, *mb2;
	long len;
	nfsfh_t *fhp;
	struct nfsnode *np;
	int lockparent, wantparent, error = 0, attrflag, fhsize;
	int v3 = NFS_ISV3(dvp);
	struct proc *p = cnp->cn_proc;

	*vpp = NULLVP;
	if ((flags & ISLASTCN) && (dvp->v_mount->mnt_flag & MNT_RDONLY) &&
	    (cnp->cn_nameiop == DELETE || cnp->cn_nameiop == RENAME))
		return (EROFS);
	if (dvp->v_type != VDIR)
		return (ENOTDIR);
	lockparent = flags & LOCKPARENT;
	wantparent = flags & (LOCKPARENT|WANTPARENT);
	nmp = VFSTONFS(dvp->v_mount);
	np = VTONFS(dvp);
	if ((error = cache_lookup(dvp, vpp, cnp)) && error != ENOENT) {
		struct vattr vattr;
		int vpid;

		if ((error = VOP_ACCESS(dvp, VEXEC, cnp->cn_cred, p)) != 0) {
			*vpp = NULLVP;
			return (error);
		}

		newvp = *vpp;
		vpid = newvp->v_id;
		/*
		 * See the comment starting `Step through' in ufs/ufs_lookup.c
		 * for an explanation of the locking protocol
		 */
		if (dvp == newvp) {
			VREF(newvp);
			error = 0;
		} else if (flags & ISDOTDOT) {
			VOP_UNLOCK(dvp, 0, p);
			error = vget(newvp, LK_EXCLUSIVE, p);
			if (!error && lockparent && (flags & ISLASTCN))
				error = vn_lock(dvp, LK_EXCLUSIVE, p);
		} else {
			error = vget(newvp, LK_EXCLUSIVE, p);
			if (!lockparent || error || !(flags & ISLASTCN))
				VOP_UNLOCK(dvp, 0, p);
		}
		if (!error) {
			if (vpid == newvp->v_id) {
			   if (!VOP_GETATTR(newvp, &vattr, cnp->cn_cred, p)
			    && vattr.va_ctime.tv_sec == VTONFS(newvp)->n_ctime) {
				nfsstats.lookupcache_hits++;
				if (cnp->cn_nameiop != LOOKUP &&
				    (flags & ISLASTCN))
					cnp->cn_flags |= SAVENAME;
				return (0);
			   }
			   cache_purge(newvp);
			}
			vput(newvp);
			if (lockparent && dvp != newvp && (flags & ISLASTCN))
				VOP_UNLOCK(dvp, 0, p);
		}
		error = vn_lock(dvp, LK_EXCLUSIVE, p);
		*vpp = NULLVP;
		if (error)
			return (error);
	}
	error = 0;
	newvp = NULLVP;
	nfsstats.lookupcache_misses++;
	nfsstats.rpccnt[NFSPROC_LOOKUP]++;
	len = cnp->cn_namelen;
	nfsm_reqhead(dvp, NFSPROC_LOOKUP,
		NFSX_FH(v3) + NFSX_UNSIGNED + nfsm_rndup(len));
	nfsm_fhtom(dvp, v3);
	nfsm_strtom(cnp->cn_nameptr, len, NFS_MAXNAMLEN);
	nfsm_request(dvp, NFSPROC_LOOKUP, cnp->cn_proc, cnp->cn_cred);
	if (error) {
		nfsm_postop_attr(dvp, attrflag);
		m_freem(mrep);
		goto nfsmout;
	}
	nfsm_getfh(fhp, fhsize, v3);

	/*
	 * Handle RENAME case...
	 */
	if (cnp->cn_nameiop == RENAME && wantparent && (flags & ISLASTCN)) {
		if (NFS_CMPFH(np, fhp, fhsize)) {
			m_freem(mrep);
			return (EISDIR);
		}
		error = nfs_nget(dvp->v_mount, fhp, fhsize, &np);
		if (error) {
			m_freem(mrep);
			return (error);
		}
		newvp = NFSTOV(np);
		if (v3) {
			nfsm_postop_attr(newvp, attrflag);
			nfsm_postop_attr(dvp, attrflag);
		} else
			nfsm_loadattr(newvp, (struct vattr *)0);
		*vpp = newvp;
		m_freem(mrep);
		cnp->cn_flags |= SAVENAME;
		if (!lockparent)
			VOP_UNLOCK(dvp, 0, p);
		return (0);
	}

	if (flags & ISDOTDOT) {
		VOP_UNLOCK(dvp, 0, p);
		error = nfs_nget(dvp->v_mount, fhp, fhsize, &np);
		if (error) {
			vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY, p);
			return (error);
		}
		newvp = NFSTOV(np);
		if (lockparent && (flags & ISLASTCN) &&
		    (error = vn_lock(dvp, LK_EXCLUSIVE, p))) {
		    	vput(newvp);
			return (error);
		}
	} else if (NFS_CMPFH(np, fhp, fhsize)) {
		VREF(dvp);
		newvp = dvp;
	} else {
		error = nfs_nget(dvp->v_mount, fhp, fhsize, &np);
		if (error) {
			m_freem(mrep);
			return (error);
		}
		if (!lockparent || !(flags & ISLASTCN))
			VOP_UNLOCK(dvp, 0, p);
		newvp = NFSTOV(np);
	}
	if (v3) {
		nfsm_postop_attr(newvp, attrflag);
		nfsm_postop_attr(dvp, attrflag);
	} else
		nfsm_loadattr(newvp, (struct vattr *)0);
	if (cnp->cn_nameiop != LOOKUP && (flags & ISLASTCN))
		cnp->cn_flags |= SAVENAME;
	if ((cnp->cn_flags & MAKEENTRY) &&
	    (cnp->cn_nameiop != DELETE || !(flags & ISLASTCN))) {
		np->n_ctime = np->n_vattr.va_ctime.tv_sec;
		cache_enter(dvp, newvp, cnp);
	}
	*vpp = newvp;
	nfsm_reqdone;
	if (error) {
		if (newvp != NULLVP) {
			vrele(newvp);
			*vpp = NULLVP;
		}
		if ((cnp->cn_nameiop == CREATE || cnp->cn_nameiop == RENAME) &&
		    (flags & ISLASTCN) && error == ENOENT) {
			if (!lockparent)
				VOP_UNLOCK(dvp, 0, p);
			if (dvp->v_mount->mnt_flag & MNT_RDONLY)
				error = EROFS;
			else
				error = EJUSTRETURN;
		}
		if (cnp->cn_nameiop != LOOKUP && (flags & ISLASTCN))
			cnp->cn_flags |= SAVENAME;
	}
	return (error);
}

/*
 * nfs read call.
 * Just call nfs_bioread() to do the work.
 */
static int
nfs_read(ap)
	struct vop_read_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int  a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;

	switch (vp->v_type) {
	case VREG:
		return (nfs_bioread(vp, ap->a_uio, ap->a_ioflag, ap->a_cred));
	case VDIR:
		return (EISDIR);
	default:
		return (EOPNOTSUPP);
	}
}

/*
 * nfs readlink call
 */
static int
nfs_readlink(ap)
	struct vop_readlink_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		struct ucred *a_cred;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;

	if (vp->v_type != VLNK)
		return (EINVAL);
	return (nfs_bioread(vp, ap->a_uio, 0, ap->a_cred));
}

/*
 * Do a readlink rpc.
 * Called by nfs_doio() from below the buffer cache.
 */
int
nfs_readlinkrpc(vp, uiop, cred)
	register struct vnode *vp;
	struct uio *uiop;
	struct ucred *cred;
{
	register u_int32_t *tl;
	register caddr_t cp;
	register int32_t t1, t2;
	caddr_t bpos, dpos, cp2;
	int error = 0, len, attrflag;
	struct mbuf *mreq, *mrep, *md, *mb, *mb2;
	int v3 = NFS_ISV3(vp);

	nfsstats.rpccnt[NFSPROC_READLINK]++;
	nfsm_reqhead(vp, NFSPROC_READLINK, NFSX_FH(v3));
	nfsm_fhtom(vp, v3);
	nfsm_request(vp, NFSPROC_READLINK, uiop->uio_procp, cred);
	if (v3)
		nfsm_postop_attr(vp, attrflag);
	if (!error) {
		nfsm_strsiz(len, NFS_MAXPATHLEN);
		if (len == NFS_MAXPATHLEN) {
			struct nfsnode *np = VTONFS(vp);
			if (np->n_size && np->n_size < NFS_MAXPATHLEN)
				len = np->n_size;
		}
		nfsm_mtouio(uiop, len);
	}
	nfsm_reqdone;
	return (error);
}

/*
 * nfs read rpc call
 * Ditto above
 */
int
nfs_readrpc(vp, uiop, cred)
	register struct vnode *vp;
	struct uio *uiop;
	struct ucred *cred;
{
	register u_int32_t *tl;
	register caddr_t cp;
	register int32_t t1, t2;
	caddr_t bpos, dpos, cp2;
	struct mbuf *mreq, *mrep, *md, *mb, *mb2;
	struct nfsmount *nmp;
	int error = 0, len, retlen, tsiz, eof, attrflag;
	int v3 = NFS_ISV3(vp);

#ifndef nolint
	eof = 0;
#endif
	nmp = VFSTONFS(vp->v_mount);
	tsiz = uiop->uio_resid;
	if (uiop->uio_offset + tsiz > nmp->nm_maxfilesize)
		return (EFBIG);
	while (tsiz > 0) {
		nfsstats.rpccnt[NFSPROC_READ]++;
		len = (tsiz > nmp->nm_rsize) ? nmp->nm_rsize : tsiz;
		nfsm_reqhead(vp, NFSPROC_READ, NFSX_FH(v3) + NFSX_UNSIGNED * 3);
		nfsm_fhtom(vp, v3);
		nfsm_build(tl, u_int32_t *, NFSX_UNSIGNED * 3);
		if (v3) {
			txdr_hyper(uiop->uio_offset, tl);
			*(tl + 2) = txdr_unsigned(len);
		} else {
			*tl++ = txdr_unsigned(uiop->uio_offset);
			*tl++ = txdr_unsigned(len);
			*tl = 0;
		}
		nfsm_request(vp, NFSPROC_READ, uiop->uio_procp, cred);
		if (v3) {
			nfsm_postop_attr(vp, attrflag);
			if (error) {
				m_freem(mrep);
				goto nfsmout;
			}
			nfsm_dissect(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
			eof = fxdr_unsigned(int, *(tl + 1));
		} else
			nfsm_loadattr(vp, (struct vattr *)0);
		nfsm_strsiz(retlen, nmp->nm_rsize);
		nfsm_mtouio(uiop, retlen);
		m_freem(mrep);
		tsiz -= retlen;
		if (v3) {
			if (eof || retlen == 0) {
				tsiz = 0;
			}
		} else if (retlen < len) {
			tsiz = 0;
		}
	}
nfsmout:
	return (error);
}

/*
 * nfs write call
 */
int
nfs_writerpc(vp, uiop, cred, iomode, must_commit)
	register struct vnode *vp;
	register struct uio *uiop;
	struct ucred *cred;
	int *iomode, *must_commit;
{
	register u_int32_t *tl;
	register caddr_t cp;
	register int32_t t1, t2, backup;
	caddr_t bpos, dpos, cp2;
	struct mbuf *mreq, *mrep, *md, *mb, *mb2;
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	int error = 0, len, tsiz, wccflag = NFSV3_WCCRATTR, rlen, commit;
	int v3 = NFS_ISV3(vp), committed = NFSV3WRITE_FILESYNC;

#ifndef DIAGNOSTIC
	if (uiop->uio_iovcnt != 1)
		panic("nfs: writerpc iovcnt > 1");
#endif
	*must_commit = 0;
	tsiz = uiop->uio_resid;
	if (uiop->uio_offset + tsiz > nmp->nm_maxfilesize)
		return (EFBIG);
	while (tsiz > 0) {
		nfsstats.rpccnt[NFSPROC_WRITE]++;
		len = (tsiz > nmp->nm_wsize) ? nmp->nm_wsize : tsiz;
		nfsm_reqhead(vp, NFSPROC_WRITE,
			NFSX_FH(v3) + 5 * NFSX_UNSIGNED + nfsm_rndup(len));
		nfsm_fhtom(vp, v3);
		if (v3) {
			nfsm_build(tl, u_int32_t *, 5 * NFSX_UNSIGNED);
			txdr_hyper(uiop->uio_offset, tl);
			tl += 2;
			*tl++ = txdr_unsigned(len);
			*tl++ = txdr_unsigned(*iomode);
			*tl = txdr_unsigned(len);
		} else {
			register u_int32_t x;

			nfsm_build(tl, u_int32_t *, 4 * NFSX_UNSIGNED);
			/* Set both "begin" and "current" to non-garbage. */
			x = txdr_unsigned((u_int32_t)uiop->uio_offset);
			*tl++ = x;	/* "begin offset" */
			*tl++ = x;	/* "current offset" */
			x = txdr_unsigned(len);
			*tl++ = x;	/* total to this offset */
			*tl = x;	/* size of this write */
		}
		nfsm_uiotom(uiop, len);
		nfsm_request(vp, NFSPROC_WRITE, uiop->uio_procp, cred);
		if (v3) {
			wccflag = NFSV3_WCCCHK;
			nfsm_wcc_data(vp, wccflag);
			if (!error) {
				nfsm_dissect(tl, u_int32_t *, 2 * NFSX_UNSIGNED
					+ NFSX_V3WRITEVERF);
				rlen = fxdr_unsigned(int, *tl++);
				if (rlen == 0) {
					error = NFSERR_IO;
					m_freem(mrep);
					break;
				} else if (rlen < len) {
					backup = len - rlen;
					uiop->uio_iov->iov_base -= backup;
					uiop->uio_iov->iov_len += backup;
					uiop->uio_offset -= backup;
					uiop->uio_resid += backup;
					len = rlen;
				}
				commit = fxdr_unsigned(int, *tl++);

				/*
				 * Return the lowest committment level
				 * obtained by any of the RPCs.
				 */
				if (committed == NFSV3WRITE_FILESYNC)
					committed = commit;
				else if (committed == NFSV3WRITE_DATASYNC &&
					commit == NFSV3WRITE_UNSTABLE)
					committed = commit;
				if ((nmp->nm_state & NFSSTA_HASWRITEVERF) == 0){
				    bcopy((caddr_t)tl, (caddr_t)nmp->nm_verf,
					NFSX_V3WRITEVERF);
				    nmp->nm_state |= NFSSTA_HASWRITEVERF;
				} else if (bcmp((caddr_t)tl,
				    (caddr_t)nmp->nm_verf, NFSX_V3WRITEVERF)) {
				    *must_commit = 1;
				    bcopy((caddr_t)tl, (caddr_t)nmp->nm_verf,
					NFSX_V3WRITEVERF);
				}
			}
		} else
		    nfsm_loadattr(vp, (struct vattr *)0);
		if (wccflag)
		    VTONFS(vp)->n_mtime = VTONFS(vp)->n_vattr.va_mtime.tv_sec;
		m_freem(mrep);
		if (error)
			break;
		tsiz -= len;
	}
nfsmout:
	if (vp->v_mount->mnt_flag & MNT_ASYNC)
		committed = NFSV3WRITE_FILESYNC;
	*iomode = committed;
	if (error)
		uiop->uio_resid = tsiz;
	return (error);
}

/*
 * nfs mknod rpc
 * For NFS v2 this is a kludge. Use a create rpc but with the IFMT bits of the
 * mode set to specify the file type and the size field for rdev.
 */
static int
nfs_mknodrpc(dvp, vpp, cnp, vap)
	register struct vnode *dvp;
	register struct vnode **vpp;
	register struct componentname *cnp;
	register struct vattr *vap;
{
	register struct nfsv2_sattr *sp;
	register u_int32_t *tl;
	register caddr_t cp;
	register int32_t t1, t2;
	struct vnode *newvp = (struct vnode *)0;
	struct nfsnode *np = (struct nfsnode *)0;
	struct vattr vattr;
	char *cp2;
	caddr_t bpos, dpos;
	int error = 0, wccflag = NFSV3_WCCRATTR, gotvp = 0;
	struct mbuf *mreq, *mrep, *md, *mb, *mb2;
	u_int32_t rdev;
	int v3 = NFS_ISV3(dvp);

	if (vap->va_type == VCHR || vap->va_type == VBLK)
		rdev = txdr_unsigned(vap->va_rdev);
	else if (vap->va_type == VFIFO || vap->va_type == VSOCK)
		rdev = nfs_xdrneg1;
	else {
		return (EOPNOTSUPP);
	}
	if ((error = VOP_GETATTR(dvp, &vattr, cnp->cn_cred, cnp->cn_proc)) != 0) {
		return (error);
	}
	nfsstats.rpccnt[NFSPROC_MKNOD]++;
	nfsm_reqhead(dvp, NFSPROC_MKNOD, NFSX_FH(v3) + 4 * NFSX_UNSIGNED +
		+ nfsm_rndup(cnp->cn_namelen) + NFSX_SATTR(v3));
	nfsm_fhtom(dvp, v3);
	nfsm_strtom(cnp->cn_nameptr, cnp->cn_namelen, NFS_MAXNAMLEN);
	if (v3) {
		nfsm_build(tl, u_int32_t *, NFSX_UNSIGNED);
		*tl++ = vtonfsv3_type(vap->va_type);
		nfsm_v3attrbuild(vap, FALSE);
		if (vap->va_type == VCHR || vap->va_type == VBLK) {
			nfsm_build(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
			*tl++ = txdr_unsigned(umajor(vap->va_rdev));
			*tl = txdr_unsigned(uminor(vap->va_rdev));
		}
	} else {
		nfsm_build(sp, struct nfsv2_sattr *, NFSX_V2SATTR);
		sp->sa_mode = vtonfsv2_mode(vap->va_type, vap->va_mode);
		sp->sa_uid = nfs_xdrneg1;
		sp->sa_gid = nfs_xdrneg1;
		sp->sa_size = rdev;
		txdr_nfsv2time(&vap->va_atime, &sp->sa_atime);
		txdr_nfsv2time(&vap->va_mtime, &sp->sa_mtime);
	}
	nfsm_request(dvp, NFSPROC_MKNOD, cnp->cn_proc, cnp->cn_cred);
	if (!error) {
		nfsm_mtofh(dvp, newvp, v3, gotvp);
		if (!gotvp) {
			if (newvp) {
				vput(newvp);
				newvp = (struct vnode *)0;
			}
			error = nfs_lookitup(dvp, cnp->cn_nameptr,
			    cnp->cn_namelen, cnp->cn_cred, cnp->cn_proc, &np);
			if (!error)
				newvp = NFSTOV(np);
		}
	}
	if (v3)
		nfsm_wcc_data(dvp, wccflag);
	nfsm_reqdone;
	if (error) {
		if (newvp)
			vput(newvp);
	} else {
		if (cnp->cn_flags & MAKEENTRY)
			cache_enter(dvp, newvp, cnp);
		*vpp = newvp;
	}
	VTONFS(dvp)->n_flag |= NMODIFIED;
	if (!wccflag)
		VTONFS(dvp)->n_attrstamp = 0;
	return (error);
}

/*
 * nfs mknod vop
 * just call nfs_mknodrpc() to do the work.
 */
/* ARGSUSED */
static int
nfs_mknod(ap)
	struct vop_mknod_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vattr *a_vap;
	} */ *ap;
{
	return nfs_mknodrpc(ap->a_dvp, ap->a_vpp, ap->a_cnp, ap->a_vap);
}

static u_long create_verf;
/*
 * nfs file create call
 */
static int
nfs_create(ap)
	struct vop_create_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vattr *a_vap;
	} */ *ap;
{
	register struct vnode *dvp = ap->a_dvp;
	register struct vattr *vap = ap->a_vap;
	register struct componentname *cnp = ap->a_cnp;
	register struct nfsv2_sattr *sp;
	register u_int32_t *tl;
	register caddr_t cp;
	register int32_t t1, t2;
	struct nfsnode *np = (struct nfsnode *)0;
	struct vnode *newvp = (struct vnode *)0;
	caddr_t bpos, dpos, cp2;
	int error = 0, wccflag = NFSV3_WCCRATTR, gotvp = 0, fmode = 0;
	struct mbuf *mreq, *mrep, *md, *mb, *mb2;
	struct vattr vattr;
	int v3 = NFS_ISV3(dvp);

	/*
	 * Oops, not for me..
	 */
	if (vap->va_type == VSOCK)
		return (nfs_mknodrpc(dvp, ap->a_vpp, cnp, vap));

	if ((error = VOP_GETATTR(dvp, &vattr, cnp->cn_cred, cnp->cn_proc)) != 0) {
		return (error);
	}
	if (vap->va_vaflags & VA_EXCLUSIVE)
		fmode |= O_EXCL;
again:
	nfsstats.rpccnt[NFSPROC_CREATE]++;
	nfsm_reqhead(dvp, NFSPROC_CREATE, NFSX_FH(v3) + 2 * NFSX_UNSIGNED +
		nfsm_rndup(cnp->cn_namelen) + NFSX_SATTR(v3));
	nfsm_fhtom(dvp, v3);
	nfsm_strtom(cnp->cn_nameptr, cnp->cn_namelen, NFS_MAXNAMLEN);
	if (v3) {
		nfsm_build(tl, u_int32_t *, NFSX_UNSIGNED);
		if (fmode & O_EXCL) {
			*tl = txdr_unsigned(NFSV3CREATE_EXCLUSIVE);
			nfsm_build(tl, u_int32_t *, NFSX_V3CREATEVERF);
#ifdef INET
			if (!TAILQ_EMPTY(&in_ifaddrhead))
				*tl++ = IA_SIN(in_ifaddrhead.tqh_first)->sin_addr.s_addr;
			else
#endif
				*tl++ = create_verf;
			*tl = ++create_verf;
		} else {
			*tl = txdr_unsigned(NFSV3CREATE_UNCHECKED);
			nfsm_v3attrbuild(vap, FALSE);
		}
	} else {
		nfsm_build(sp, struct nfsv2_sattr *, NFSX_V2SATTR);
		sp->sa_mode = vtonfsv2_mode(vap->va_type, vap->va_mode);
		sp->sa_uid = nfs_xdrneg1;
		sp->sa_gid = nfs_xdrneg1;
		sp->sa_size = 0;
		txdr_nfsv2time(&vap->va_atime, &sp->sa_atime);
		txdr_nfsv2time(&vap->va_mtime, &sp->sa_mtime);
	}
	nfsm_request(dvp, NFSPROC_CREATE, cnp->cn_proc, cnp->cn_cred);
	if (!error) {
		nfsm_mtofh(dvp, newvp, v3, gotvp);
		if (!gotvp) {
			if (newvp) {
				vput(newvp);
				newvp = (struct vnode *)0;
			}
			error = nfs_lookitup(dvp, cnp->cn_nameptr,
			    cnp->cn_namelen, cnp->cn_cred, cnp->cn_proc, &np);
			if (!error)
				newvp = NFSTOV(np);
		}
	}
	if (v3)
		nfsm_wcc_data(dvp, wccflag);
	nfsm_reqdone;
	if (error) {
		if (v3 && (fmode & O_EXCL) && error == NFSERR_NOTSUPP) {
			fmode &= ~O_EXCL;
			goto again;
		}
		if (newvp)
			vput(newvp);
	} else if (v3 && (fmode & O_EXCL)) {
		/*
		 * We are normally called with only a partially initialized
		 * VAP.  Since the NFSv3 spec says that server may use the
		 * file attributes to store the verifier, the spec requires
		 * us to do a SETATTR RPC. FreeBSD servers store the verifier
		 * in atime, but we can't really assume that all servers will
		 * so we ensure that our SETATTR sets both atime and mtime.
		 */
		if (vap->va_mtime.tv_sec == VNOVAL)
			vfs_timestamp(&vap->va_mtime);
		if (vap->va_atime.tv_sec == VNOVAL)
			vap->va_atime = vap->va_mtime;
		error = nfs_setattrrpc(newvp, vap, cnp->cn_cred, cnp->cn_proc);
	}
	if (!error) {
		if (cnp->cn_flags & MAKEENTRY)
			cache_enter(dvp, newvp, cnp);
		*ap->a_vpp = newvp;
	}
	VTONFS(dvp)->n_flag |= NMODIFIED;
	if (!wccflag)
		VTONFS(dvp)->n_attrstamp = 0;
	return (error);
}

/*
 * nfs file remove call
 * To try and make nfs semantics closer to ufs semantics, a file that has
 * other processes using the vnode is renamed instead of removed and then
 * removed later on the last close.
 * - If v_usecount > 1
 *	  If a rename is not already in the works
 *	     call nfs_sillyrename() to set it up
 *     else
 *	  do the remove rpc
 */
static int
nfs_remove(ap)
	struct vop_remove_args /* {
		struct vnodeop_desc *a_desc;
		struct vnode * a_dvp;
		struct vnode * a_vp;
		struct componentname * a_cnp;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct vnode *dvp = ap->a_dvp;
	register struct componentname *cnp = ap->a_cnp;
	register struct nfsnode *np = VTONFS(vp);
	int error = 0;
	struct vattr vattr;

#ifndef DIAGNOSTIC
	if ((cnp->cn_flags & HASBUF) == 0)
		panic("nfs_remove: no name");
	if (vp->v_usecount < 1)
		panic("nfs_remove: bad v_usecount");
#endif
	if (vp->v_type == VDIR)
		error = EPERM;
	else if (vp->v_usecount == 1 || (np->n_sillyrename &&
	    VOP_GETATTR(vp, &vattr, cnp->cn_cred, cnp->cn_proc) == 0 &&
	    vattr.va_nlink > 1)) {
		/*
		 * Purge the name cache so that the chance of a lookup for
		 * the name succeeding while the remove is in progress is
		 * minimized. Without node locking it can still happen, such
		 * that an I/O op returns ESTALE, but since you get this if
		 * another host removes the file..
		 */
		cache_purge(vp);
		/*
		 * throw away biocache buffers, mainly to avoid
		 * unnecessary delayed writes later.
		 */
		error = nfs_vinvalbuf(vp, 0, cnp->cn_cred, cnp->cn_proc, 1);
		/* Do the rpc */
		if (error != EINTR)
			error = nfs_removerpc(dvp, cnp->cn_nameptr,
				cnp->cn_namelen, cnp->cn_cred, cnp->cn_proc);
		/*
		 * Kludge City: If the first reply to the remove rpc is lost..
		 *   the reply to the retransmitted request will be ENOENT
		 *   since the file was in fact removed
		 *   Therefore, we cheat and return success.
		 */
		if (error == ENOENT)
			error = 0;
	} else if (!np->n_sillyrename)
		error = nfs_sillyrename(dvp, vp, cnp);
	np->n_attrstamp = 0;
	return (error);
}

/*
 * nfs file remove rpc called from nfs_inactive
 */
int
nfs_removeit(sp)
	register struct sillyrename *sp;
{

	/*
	 * Make sure that the directory vnode is still valid.
	 * XXX we should lock sp->s_dvp here.
	 */
	if (sp->s_dvp->v_type == VBAD)
		return (0);
	return (nfs_removerpc(sp->s_dvp, sp->s_name, sp->s_namlen, sp->s_cred,
		(struct proc *)0));
}

/*
 * Nfs remove rpc, called from nfs_remove() and nfs_removeit().
 */
static int
nfs_removerpc(dvp, name, namelen, cred, proc)
	register struct vnode *dvp;
	const char *name;
	int namelen;
	struct ucred *cred;
	struct proc *proc;
{
	register u_int32_t *tl;
	register caddr_t cp;
	register int32_t t1, t2;
	caddr_t bpos, dpos, cp2;
	int error = 0, wccflag = NFSV3_WCCRATTR;
	struct mbuf *mreq, *mrep, *md, *mb, *mb2;
	int v3 = NFS_ISV3(dvp);

	nfsstats.rpccnt[NFSPROC_REMOVE]++;
	nfsm_reqhead(dvp, NFSPROC_REMOVE,
		NFSX_FH(v3) + NFSX_UNSIGNED + nfsm_rndup(namelen));
	nfsm_fhtom(dvp, v3);
	nfsm_strtom(name, namelen, NFS_MAXNAMLEN);
	nfsm_request(dvp, NFSPROC_REMOVE, proc, cred);
	if (v3)
		nfsm_wcc_data(dvp, wccflag);
	nfsm_reqdone;
	VTONFS(dvp)->n_flag |= NMODIFIED;
	if (!wccflag)
		VTONFS(dvp)->n_attrstamp = 0;
	return (error);
}

/*
 * nfs file rename call
 */
static int
nfs_rename(ap)
	struct vop_rename_args  /* {
		struct vnode *a_fdvp;
		struct vnode *a_fvp;
		struct componentname *a_fcnp;
		struct vnode *a_tdvp;
		struct vnode *a_tvp;
		struct componentname *a_tcnp;
	} */ *ap;
{
	register struct vnode *fvp = ap->a_fvp;
	register struct vnode *tvp = ap->a_tvp;
	register struct vnode *fdvp = ap->a_fdvp;
	register struct vnode *tdvp = ap->a_tdvp;
	register struct componentname *tcnp = ap->a_tcnp;
	register struct componentname *fcnp = ap->a_fcnp;
	int error;

#ifndef DIAGNOSTIC
	if ((tcnp->cn_flags & HASBUF) == 0 ||
	    (fcnp->cn_flags & HASBUF) == 0)
		panic("nfs_rename: no name");
#endif
	/* Check for cross-device rename */
	if ((fvp->v_mount != tdvp->v_mount) ||
	    (tvp && (fvp->v_mount != tvp->v_mount))) {
		error = EXDEV;
		goto out;
	}

	/*
	 * We have to flush B_DELWRI data prior to renaming
	 * the file.  If we don't, the delayed-write buffers
	 * can be flushed out later after the file has gone stale
	 * under NFSV3.  NFSV2 does not have this problem because
	 * ( as far as I can tell ) it flushes dirty buffers more
	 * often.
	 */

	VOP_FSYNC(fvp, fcnp->cn_cred, MNT_WAIT, fcnp->cn_proc);
	if (tvp)
	    VOP_FSYNC(tvp, tcnp->cn_cred, MNT_WAIT, tcnp->cn_proc);

	/*
	 * If the tvp exists and is in use, sillyrename it before doing the
	 * rename of the new file over it.
	 * XXX Can't sillyrename a directory.
	 */
	if (tvp && tvp->v_usecount > 1 && !VTONFS(tvp)->n_sillyrename &&
		tvp->v_type != VDIR && !nfs_sillyrename(tdvp, tvp, tcnp)) {
		vput(tvp);
		tvp = NULL;
	}

	error = nfs_renamerpc(fdvp, fcnp->cn_nameptr, fcnp->cn_namelen,
		tdvp, tcnp->cn_nameptr, tcnp->cn_namelen, tcnp->cn_cred,
		tcnp->cn_proc);

	if (fvp->v_type == VDIR) {
		if (tvp != NULL && tvp->v_type == VDIR)
			cache_purge(tdvp);
		cache_purge(fdvp);
	}

out:
	if (tdvp == tvp)
		vrele(tdvp);
	else
		vput(tdvp);
	if (tvp)
		vput(tvp);
	vrele(fdvp);
	vrele(fvp);
	/*
	 * Kludge: Map ENOENT => 0 assuming that it is a reply to a retry.
	 */
	if (error == ENOENT)
		error = 0;
	return (error);
}

/*
 * nfs file rename rpc called from nfs_remove() above
 */
static int
nfs_renameit(sdvp, scnp, sp)
	struct vnode *sdvp;
	struct componentname *scnp;
	register struct sillyrename *sp;
{
	return (nfs_renamerpc(sdvp, scnp->cn_nameptr, scnp->cn_namelen,
		sdvp, sp->s_name, sp->s_namlen, scnp->cn_cred, scnp->cn_proc));
}

/*
 * Do an nfs rename rpc. Called from nfs_rename() and nfs_renameit().
 */
static int
nfs_renamerpc(fdvp, fnameptr, fnamelen, tdvp, tnameptr, tnamelen, cred, proc)
	register struct vnode *fdvp;
	const char *fnameptr;
	int fnamelen;
	register struct vnode *tdvp;
	const char *tnameptr;
	int tnamelen;
	struct ucred *cred;
	struct proc *proc;
{
	register u_int32_t *tl;
	register caddr_t cp;
	register int32_t t1, t2;
	caddr_t bpos, dpos, cp2;
	int error = 0, fwccflag = NFSV3_WCCRATTR, twccflag = NFSV3_WCCRATTR;
	struct mbuf *mreq, *mrep, *md, *mb, *mb2;
	int v3 = NFS_ISV3(fdvp);

	nfsstats.rpccnt[NFSPROC_RENAME]++;
	nfsm_reqhead(fdvp, NFSPROC_RENAME,
		(NFSX_FH(v3) + NFSX_UNSIGNED)*2 + nfsm_rndup(fnamelen) +
		nfsm_rndup(tnamelen));
	nfsm_fhtom(fdvp, v3);
	nfsm_strtom(fnameptr, fnamelen, NFS_MAXNAMLEN);
	nfsm_fhtom(tdvp, v3);
	nfsm_strtom(tnameptr, tnamelen, NFS_MAXNAMLEN);
	nfsm_request(fdvp, NFSPROC_RENAME, proc, cred);
	if (v3) {
		nfsm_wcc_data(fdvp, fwccflag);
		nfsm_wcc_data(tdvp, twccflag);
	}
	nfsm_reqdone;
	VTONFS(fdvp)->n_flag |= NMODIFIED;
	VTONFS(tdvp)->n_flag |= NMODIFIED;
	if (!fwccflag)
		VTONFS(fdvp)->n_attrstamp = 0;
	if (!twccflag)
		VTONFS(tdvp)->n_attrstamp = 0;
	return (error);
}

/*
 * nfs hard link create call
 */
static int
nfs_link(ap)
	struct vop_link_args /* {
		struct vnode *a_tdvp;
		struct vnode *a_vp;
		struct componentname *a_cnp;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct vnode *tdvp = ap->a_tdvp;
	register struct componentname *cnp = ap->a_cnp;
	register u_int32_t *tl;
	register caddr_t cp;
	register int32_t t1, t2;
	caddr_t bpos, dpos, cp2;
	int error = 0, wccflag = NFSV3_WCCRATTR, attrflag = 0;
	struct mbuf *mreq, *mrep, *md, *mb, *mb2;
	int v3;

	if (vp->v_mount != tdvp->v_mount) {
		return (EXDEV);
	}

	/*
	 * Push all writes to the server, so that the attribute cache
	 * doesn't get "out of sync" with the server.
	 * XXX There should be a better way!
	 */
	VOP_FSYNC(vp, cnp->cn_cred, MNT_WAIT, cnp->cn_proc);

	v3 = NFS_ISV3(vp);
	nfsstats.rpccnt[NFSPROC_LINK]++;
	nfsm_reqhead(vp, NFSPROC_LINK,
		NFSX_FH(v3)*2 + NFSX_UNSIGNED + nfsm_rndup(cnp->cn_namelen));
	nfsm_fhtom(vp, v3);
	nfsm_fhtom(tdvp, v3);
	nfsm_strtom(cnp->cn_nameptr, cnp->cn_namelen, NFS_MAXNAMLEN);
	nfsm_request(vp, NFSPROC_LINK, cnp->cn_proc, cnp->cn_cred);
	if (v3) {
		nfsm_postop_attr(vp, attrflag);
		nfsm_wcc_data(tdvp, wccflag);
	}
	nfsm_reqdone;
	VTONFS(tdvp)->n_flag |= NMODIFIED;
	if (!attrflag)
		VTONFS(vp)->n_attrstamp = 0;
	if (!wccflag)
		VTONFS(tdvp)->n_attrstamp = 0;
	/*
	 * Kludge: Map EEXIST => 0 assuming that it is a reply to a retry.
	 */
	if (error == EEXIST)
		error = 0;
	return (error);
}

/*
 * nfs symbolic link create call
 */
static int
nfs_symlink(ap)
	struct vop_symlink_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vattr *a_vap;
		char *a_target;
	} */ *ap;
{
	register struct vnode *dvp = ap->a_dvp;
	register struct vattr *vap = ap->a_vap;
	register struct componentname *cnp = ap->a_cnp;
	register struct nfsv2_sattr *sp;
	register u_int32_t *tl;
	register caddr_t cp;
	register int32_t t1, t2;
	caddr_t bpos, dpos, cp2;
	int slen, error = 0, wccflag = NFSV3_WCCRATTR, gotvp;
	struct mbuf *mreq, *mrep, *md, *mb, *mb2;
	struct vnode *newvp = (struct vnode *)0;
	int v3 = NFS_ISV3(dvp);

	nfsstats.rpccnt[NFSPROC_SYMLINK]++;
	slen = strlen(ap->a_target);
	nfsm_reqhead(dvp, NFSPROC_SYMLINK, NFSX_FH(v3) + 2*NFSX_UNSIGNED +
	    nfsm_rndup(cnp->cn_namelen) + nfsm_rndup(slen) + NFSX_SATTR(v3));
	nfsm_fhtom(dvp, v3);
	nfsm_strtom(cnp->cn_nameptr, cnp->cn_namelen, NFS_MAXNAMLEN);
	if (v3) {
		nfsm_v3attrbuild(vap, FALSE);
	}
	nfsm_strtom(ap->a_target, slen, NFS_MAXPATHLEN);
	if (!v3) {
		nfsm_build(sp, struct nfsv2_sattr *, NFSX_V2SATTR);
		sp->sa_mode = vtonfsv2_mode(VLNK, vap->va_mode);
		sp->sa_uid = nfs_xdrneg1;
		sp->sa_gid = nfs_xdrneg1;
		sp->sa_size = nfs_xdrneg1;
		txdr_nfsv2time(&vap->va_atime, &sp->sa_atime);
		txdr_nfsv2time(&vap->va_mtime, &sp->sa_mtime);
	}

	/*
	 * Issue the NFS request and get the rpc response.
	 *
	 * Only NFSv3 responses returning an error of 0 actually return
	 * a file handle that can be converted into newvp without having
	 * to do an extra lookup rpc.
	 */
	nfsm_request(dvp, NFSPROC_SYMLINK, cnp->cn_proc, cnp->cn_cred);
	if (v3) {
		if (error == 0)
			nfsm_mtofh(dvp, newvp, v3, gotvp);
		nfsm_wcc_data(dvp, wccflag);
	}

	/*
	 * out code jumps -> here, mrep is also freed.
	 */

	nfsm_reqdone;

	/*
	 * If we get an EEXIST error, silently convert it to no-error
	 * in case of an NFS retry.
	 */
	if (error == EEXIST)
		error = 0;

	/*
	 * If we do not have (or no longer have) an error, and we could
	 * not extract the newvp from the response due to the request being
	 * NFSv2 or the error being EEXIST.  We have to do a lookup in order
	 * to obtain a newvp to return.  
	 */
	if (error == 0 && newvp == NULL) {
		struct nfsnode *np = NULL;

		error = nfs_lookitup(dvp, cnp->cn_nameptr, cnp->cn_namelen,
		    cnp->cn_cred, cnp->cn_proc, &np);
		if (!error)
			newvp = NFSTOV(np);
	}
	if (error) {
		if (newvp)
			vput(newvp);
	} else {
		*ap->a_vpp = newvp;
	}
	VTONFS(dvp)->n_flag |= NMODIFIED;
	if (!wccflag)
		VTONFS(dvp)->n_attrstamp = 0;
	return (error);
}

/*
 * nfs make dir call
 */
static int
nfs_mkdir(ap)
	struct vop_mkdir_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vattr *a_vap;
	} */ *ap;
{
	register struct vnode *dvp = ap->a_dvp;
	register struct vattr *vap = ap->a_vap;
	register struct componentname *cnp = ap->a_cnp;
	register struct nfsv2_sattr *sp;
	register u_int32_t *tl;
	register caddr_t cp;
	register int32_t t1, t2;
	register int len;
	struct nfsnode *np = (struct nfsnode *)0;
	struct vnode *newvp = (struct vnode *)0;
	caddr_t bpos, dpos, cp2;
	int error = 0, wccflag = NFSV3_WCCRATTR;
	int gotvp = 0;
	struct mbuf *mreq, *mrep, *md, *mb, *mb2;
	struct vattr vattr;
	int v3 = NFS_ISV3(dvp);

	if ((error = VOP_GETATTR(dvp, &vattr, cnp->cn_cred, cnp->cn_proc)) != 0) {
		return (error);
	}
	len = cnp->cn_namelen;
	nfsstats.rpccnt[NFSPROC_MKDIR]++;
	nfsm_reqhead(dvp, NFSPROC_MKDIR,
	  NFSX_FH(v3) + NFSX_UNSIGNED + nfsm_rndup(len) + NFSX_SATTR(v3));
	nfsm_fhtom(dvp, v3);
	nfsm_strtom(cnp->cn_nameptr, len, NFS_MAXNAMLEN);
	if (v3) {
		nfsm_v3attrbuild(vap, FALSE);
	} else {
		nfsm_build(sp, struct nfsv2_sattr *, NFSX_V2SATTR);
		sp->sa_mode = vtonfsv2_mode(VDIR, vap->va_mode);
		sp->sa_uid = nfs_xdrneg1;
		sp->sa_gid = nfs_xdrneg1;
		sp->sa_size = nfs_xdrneg1;
		txdr_nfsv2time(&vap->va_atime, &sp->sa_atime);
		txdr_nfsv2time(&vap->va_mtime, &sp->sa_mtime);
	}
	nfsm_request(dvp, NFSPROC_MKDIR, cnp->cn_proc, cnp->cn_cred);
	if (!error)
		nfsm_mtofh(dvp, newvp, v3, gotvp);
	if (v3)
		nfsm_wcc_data(dvp, wccflag);
	nfsm_reqdone;
	VTONFS(dvp)->n_flag |= NMODIFIED;
	if (!wccflag)
		VTONFS(dvp)->n_attrstamp = 0;
	/*
	 * Kludge: Map EEXIST => 0 assuming that you have a reply to a retry
	 * if we can succeed in looking up the directory.
	 */
	if (error == EEXIST || (!error && !gotvp)) {
		if (newvp) {
			vrele(newvp);
			newvp = (struct vnode *)0;
		}
		error = nfs_lookitup(dvp, cnp->cn_nameptr, len, cnp->cn_cred,
			cnp->cn_proc, &np);
		if (!error) {
			newvp = NFSTOV(np);
			if (newvp->v_type != VDIR)
				error = EEXIST;
		}
	}
	if (error) {
		if (newvp)
			vrele(newvp);
	} else
		*ap->a_vpp = newvp;
	return (error);
}

/*
 * nfs remove directory call
 */
static int
nfs_rmdir(ap)
	struct vop_rmdir_args /* {
		struct vnode *a_dvp;
		struct vnode *a_vp;
		struct componentname *a_cnp;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct vnode *dvp = ap->a_dvp;
	register struct componentname *cnp = ap->a_cnp;
	register u_int32_t *tl;
	register caddr_t cp;
	register int32_t t1, t2;
	caddr_t bpos, dpos, cp2;
	int error = 0, wccflag = NFSV3_WCCRATTR;
	struct mbuf *mreq, *mrep, *md, *mb, *mb2;
	int v3 = NFS_ISV3(dvp);

	if (dvp == vp)
		return (EINVAL);
	nfsstats.rpccnt[NFSPROC_RMDIR]++;
	nfsm_reqhead(dvp, NFSPROC_RMDIR,
		NFSX_FH(v3) + NFSX_UNSIGNED + nfsm_rndup(cnp->cn_namelen));
	nfsm_fhtom(dvp, v3);
	nfsm_strtom(cnp->cn_nameptr, cnp->cn_namelen, NFS_MAXNAMLEN);
	nfsm_request(dvp, NFSPROC_RMDIR, cnp->cn_proc, cnp->cn_cred);
	if (v3)
		nfsm_wcc_data(dvp, wccflag);
	nfsm_reqdone;
	VTONFS(dvp)->n_flag |= NMODIFIED;
	if (!wccflag)
		VTONFS(dvp)->n_attrstamp = 0;
	cache_purge(dvp);
	cache_purge(vp);
	/*
	 * Kludge: Map ENOENT => 0 assuming that you have a reply to a retry.
	 */
	if (error == ENOENT)
		error = 0;
	return (error);
}

/*
 * nfs readdir call
 */
static int
nfs_readdir(ap)
	struct vop_readdir_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		struct ucred *a_cred;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct nfsnode *np = VTONFS(vp);
	register struct uio *uio = ap->a_uio;
	int tresid, error;
	struct vattr vattr;

	if (vp->v_type != VDIR)
		return (EPERM);
	/*
	 * First, check for hit on the EOF offset cache
	 */
	if (np->n_direofoffset > 0 && uio->uio_offset >= np->n_direofoffset &&
	    (np->n_flag & NMODIFIED) == 0) {
		if (VFSTONFS(vp->v_mount)->nm_flag & NFSMNT_NQNFS) {
			if (NQNFS_CKCACHABLE(vp, ND_READ)) {
				nfsstats.direofcache_hits++;
				return (0);
			}
		} else if (VOP_GETATTR(vp, &vattr, ap->a_cred, uio->uio_procp) == 0 &&
			np->n_mtime == vattr.va_mtime.tv_sec) {
			nfsstats.direofcache_hits++;
			return (0);
		}
	}

	/*
	 * Call nfs_bioread() to do the real work.
	 */
	tresid = uio->uio_resid;
	error = nfs_bioread(vp, uio, 0, ap->a_cred);

	if (!error && uio->uio_resid == tresid)
		nfsstats.direofcache_misses++;
	return (error);
}

/*
 * Readdir rpc call.
 * Called from below the buffer cache by nfs_doio().
 */
int
nfs_readdirrpc(vp, uiop, cred)
	struct vnode *vp;
	register struct uio *uiop;
	struct ucred *cred;

{
	register int len, left;
	register struct dirent *dp = NULL;
	register u_int32_t *tl;
	register caddr_t cp;
	register int32_t t1, t2;
	register nfsuint64 *cookiep;
	caddr_t bpos, dpos, cp2;
	struct mbuf *mreq, *mrep, *md, *mb, *mb2;
	nfsuint64 cookie;
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	struct nfsnode *dnp = VTONFS(vp);
	u_quad_t fileno;
	int error = 0, tlen, more_dirs = 1, blksiz = 0, bigenough = 1;
	int attrflag;
	int v3 = NFS_ISV3(vp);

#ifndef DIAGNOSTIC
	if (uiop->uio_iovcnt != 1 || (uiop->uio_offset & (DIRBLKSIZ - 1)) ||
		(uiop->uio_resid & (DIRBLKSIZ - 1)))
		panic("nfs readdirrpc bad uio");
#endif

	/*
	 * If there is no cookie, assume directory was stale.
	 */
	cookiep = nfs_getcookie(dnp, uiop->uio_offset, 0);
	if (cookiep)
		cookie = *cookiep;
	else
		return (NFSERR_BAD_COOKIE);
	/*
	 * Loop around doing readdir rpc's of size nm_readdirsize
	 * truncated to a multiple of DIRBLKSIZ.
	 * The stopping criteria is EOF or buffer full.
	 */
	while (more_dirs && bigenough) {
		nfsstats.rpccnt[NFSPROC_READDIR]++;
		nfsm_reqhead(vp, NFSPROC_READDIR, NFSX_FH(v3) +
			NFSX_READDIR(v3));
		nfsm_fhtom(vp, v3);
		if (v3) {
			nfsm_build(tl, u_int32_t *, 5 * NFSX_UNSIGNED);
			*tl++ = cookie.nfsuquad[0];
			*tl++ = cookie.nfsuquad[1];
			*tl++ = dnp->n_cookieverf.nfsuquad[0];
			*tl++ = dnp->n_cookieverf.nfsuquad[1];
		} else {
			nfsm_build(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
			*tl++ = cookie.nfsuquad[0];
		}
		*tl = txdr_unsigned(nmp->nm_readdirsize);
		nfsm_request(vp, NFSPROC_READDIR, uiop->uio_procp, cred);
		if (v3) {
			nfsm_postop_attr(vp, attrflag);
			if (!error) {
				nfsm_dissect(tl, u_int32_t *,
				    2 * NFSX_UNSIGNED);
				dnp->n_cookieverf.nfsuquad[0] = *tl++;
				dnp->n_cookieverf.nfsuquad[1] = *tl;
			} else {
				m_freem(mrep);
				goto nfsmout;
			}
		}
		nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED);
		more_dirs = fxdr_unsigned(int, *tl);
	
		/* loop thru the dir entries, doctoring them to 4bsd form */
		while (more_dirs && bigenough) {
			if (v3) {
				nfsm_dissect(tl, u_int32_t *,
				    3 * NFSX_UNSIGNED);
				fileno = fxdr_hyper(tl);
				len = fxdr_unsigned(int, *(tl + 2));
			} else {
				nfsm_dissect(tl, u_int32_t *,
				    2 * NFSX_UNSIGNED);
				fileno = fxdr_unsigned(u_quad_t, *tl++);
				len = fxdr_unsigned(int, *tl);
			}
			if (len <= 0 || len > NFS_MAXNAMLEN) {
				error = EBADRPC;
				m_freem(mrep);
				goto nfsmout;
			}
			tlen = nfsm_rndup(len);
			if (tlen == len)
				tlen += 4;	/* To ensure null termination */
			left = DIRBLKSIZ - blksiz;
			if ((tlen + DIRHDSIZ) > left) {
				dp->d_reclen += left;
				uiop->uio_iov->iov_base += left;
				uiop->uio_iov->iov_len -= left;
				uiop->uio_offset += left;
				uiop->uio_resid -= left;
				blksiz = 0;
			}
			if ((tlen + DIRHDSIZ) > uiop->uio_resid)
				bigenough = 0;
			if (bigenough) {
				dp = (struct dirent *)uiop->uio_iov->iov_base;
				dp->d_fileno = (int)fileno;
				dp->d_namlen = len;
				dp->d_reclen = tlen + DIRHDSIZ;
				dp->d_type = DT_UNKNOWN;
				blksiz += dp->d_reclen;
				if (blksiz == DIRBLKSIZ)
					blksiz = 0;
				uiop->uio_offset += DIRHDSIZ;
				uiop->uio_resid -= DIRHDSIZ;
				uiop->uio_iov->iov_base += DIRHDSIZ;
				uiop->uio_iov->iov_len -= DIRHDSIZ;
				nfsm_mtouio(uiop, len);
				cp = uiop->uio_iov->iov_base;
				tlen -= len;
				*cp = '\0';	/* null terminate */
				uiop->uio_iov->iov_base += tlen;
				uiop->uio_iov->iov_len -= tlen;
				uiop->uio_offset += tlen;
				uiop->uio_resid -= tlen;
			} else
				nfsm_adv(nfsm_rndup(len));
			if (v3) {
				nfsm_dissect(tl, u_int32_t *,
				    3 * NFSX_UNSIGNED);
			} else {
				nfsm_dissect(tl, u_int32_t *,
				    2 * NFSX_UNSIGNED);
			}
			if (bigenough) {
				cookie.nfsuquad[0] = *tl++;
				if (v3)
					cookie.nfsuquad[1] = *tl++;
			} else if (v3)
				tl += 2;
			else
				tl++;
			more_dirs = fxdr_unsigned(int, *tl);
		}
		/*
		 * If at end of rpc data, get the eof boolean
		 */
		if (!more_dirs) {
			nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED);
			more_dirs = (fxdr_unsigned(int, *tl) == 0);
		}
		m_freem(mrep);
	}
	/*
	 * Fill last record, iff any, out to a multiple of DIRBLKSIZ
	 * by increasing d_reclen for the last record.
	 */
	if (blksiz > 0) {
		left = DIRBLKSIZ - blksiz;
		dp->d_reclen += left;
		uiop->uio_iov->iov_base += left;
		uiop->uio_iov->iov_len -= left;
		uiop->uio_offset += left;
		uiop->uio_resid -= left;
	}

	/*
	 * We are now either at the end of the directory or have filled the
	 * block.
	 */
	if (bigenough)
		dnp->n_direofoffset = uiop->uio_offset;
	else {
		if (uiop->uio_resid > 0)
			printf("EEK! readdirrpc resid > 0\n");
		cookiep = nfs_getcookie(dnp, uiop->uio_offset, 1);
		*cookiep = cookie;
	}
nfsmout:
	return (error);
}

/*
 * NFS V3 readdir plus RPC. Used in place of nfs_readdirrpc().
 */
int
nfs_readdirplusrpc(vp, uiop, cred)
	struct vnode *vp;
	register struct uio *uiop;
	struct ucred *cred;
{
	register int len, left;
	register struct dirent *dp;
	register u_int32_t *tl;
	register caddr_t cp;
	register int32_t t1, t2;
	register struct vnode *newvp;
	register nfsuint64 *cookiep;
	caddr_t bpos, dpos, cp2, dpossav1, dpossav2;
	struct mbuf *mreq, *mrep, *md, *mb, *mb2, *mdsav1, *mdsav2;
	struct nameidata nami, *ndp = &nami;
	struct componentname *cnp = &ndp->ni_cnd;
	nfsuint64 cookie;
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	struct nfsnode *dnp = VTONFS(vp), *np;
	nfsfh_t *fhp;
	u_quad_t fileno;
	int error = 0, tlen, more_dirs = 1, blksiz = 0, doit, bigenough = 1, i;
	int attrflag, fhsize;

#ifndef nolint
	dp = (struct dirent *)0;
#endif
#ifndef DIAGNOSTIC
	if (uiop->uio_iovcnt != 1 || (uiop->uio_offset & (DIRBLKSIZ - 1)) ||
		(uiop->uio_resid & (DIRBLKSIZ - 1)))
		panic("nfs readdirplusrpc bad uio");
#endif
	ndp->ni_dvp = vp;
	newvp = NULLVP;

	/*
	 * If there is no cookie, assume directory was stale.
	 */
	cookiep = nfs_getcookie(dnp, uiop->uio_offset, 0);
	if (cookiep)
		cookie = *cookiep;
	else
		return (NFSERR_BAD_COOKIE);
	/*
	 * Loop around doing readdir rpc's of size nm_readdirsize
	 * truncated to a multiple of DIRBLKSIZ.
	 * The stopping criteria is EOF or buffer full.
	 */
	while (more_dirs && bigenough) {
		nfsstats.rpccnt[NFSPROC_READDIRPLUS]++;
		nfsm_reqhead(vp, NFSPROC_READDIRPLUS,
			NFSX_FH(1) + 6 * NFSX_UNSIGNED);
		nfsm_fhtom(vp, 1);
 		nfsm_build(tl, u_int32_t *, 6 * NFSX_UNSIGNED);
		*tl++ = cookie.nfsuquad[0];
		*tl++ = cookie.nfsuquad[1];
		*tl++ = dnp->n_cookieverf.nfsuquad[0];
		*tl++ = dnp->n_cookieverf.nfsuquad[1];
		*tl++ = txdr_unsigned(nmp->nm_readdirsize);
		*tl = txdr_unsigned(nmp->nm_rsize);
		nfsm_request(vp, NFSPROC_READDIRPLUS, uiop->uio_procp, cred);
		nfsm_postop_attr(vp, attrflag);
		if (error) {
			m_freem(mrep);
			goto nfsmout;
		}
		nfsm_dissect(tl, u_int32_t *, 3 * NFSX_UNSIGNED);
		dnp->n_cookieverf.nfsuquad[0] = *tl++;
		dnp->n_cookieverf.nfsuquad[1] = *tl++;
		more_dirs = fxdr_unsigned(int, *tl);

		/* loop thru the dir entries, doctoring them to 4bsd form */
		while (more_dirs && bigenough) {
			nfsm_dissect(tl, u_int32_t *, 3 * NFSX_UNSIGNED);
			fileno = fxdr_hyper(tl);
			len = fxdr_unsigned(int, *(tl + 2));
			if (len <= 0 || len > NFS_MAXNAMLEN) {
				error = EBADRPC;
				m_freem(mrep);
				goto nfsmout;
			}
			tlen = nfsm_rndup(len);
			if (tlen == len)
				tlen += 4;	/* To ensure null termination*/
			left = DIRBLKSIZ - blksiz;
			if ((tlen + DIRHDSIZ) > left) {
				dp->d_reclen += left;
				uiop->uio_iov->iov_base += left;
				uiop->uio_iov->iov_len -= left;
				uiop->uio_offset += left;
				uiop->uio_resid -= left;
				blksiz = 0;
			}
			if ((tlen + DIRHDSIZ) > uiop->uio_resid)
				bigenough = 0;
			if (bigenough) {
				dp = (struct dirent *)uiop->uio_iov->iov_base;
				dp->d_fileno = (int)fileno;
				dp->d_namlen = len;
				dp->d_reclen = tlen + DIRHDSIZ;
				dp->d_type = DT_UNKNOWN;
				blksiz += dp->d_reclen;
				if (blksiz == DIRBLKSIZ)
					blksiz = 0;
				uiop->uio_offset += DIRHDSIZ;
				uiop->uio_resid -= DIRHDSIZ;
				uiop->uio_iov->iov_base += DIRHDSIZ;
				uiop->uio_iov->iov_len -= DIRHDSIZ;
				cnp->cn_nameptr = uiop->uio_iov->iov_base;
				cnp->cn_namelen = len;
				nfsm_mtouio(uiop, len);
				cp = uiop->uio_iov->iov_base;
				tlen -= len;
				*cp = '\0';
				uiop->uio_iov->iov_base += tlen;
				uiop->uio_iov->iov_len -= tlen;
				uiop->uio_offset += tlen;
				uiop->uio_resid -= tlen;
			} else
				nfsm_adv(nfsm_rndup(len));
			nfsm_dissect(tl, u_int32_t *, 3 * NFSX_UNSIGNED);
			if (bigenough) {
				cookie.nfsuquad[0] = *tl++;
				cookie.nfsuquad[1] = *tl++;
			} else
				tl += 2;

			/*
			 * Since the attributes are before the file handle
			 * (sigh), we must skip over the attributes and then
			 * come back and get them.
			 */
			attrflag = fxdr_unsigned(int, *tl);
			if (attrflag) {
			    dpossav1 = dpos;
			    mdsav1 = md;
			    nfsm_adv(NFSX_V3FATTR);
			    nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED);
			    doit = fxdr_unsigned(int, *tl);
			    if (doit) {
				nfsm_getfh(fhp, fhsize, 1);
				if (NFS_CMPFH(dnp, fhp, fhsize)) {
				    VREF(vp);
				    newvp = vp;
				    np = dnp;
				} else {
				    error = nfs_nget(vp->v_mount, fhp,
					fhsize, &np);
				    if (error)
					doit = 0;
				    else
					newvp = NFSTOV(np);
				}
			    }
			    if (doit && bigenough) {
				dpossav2 = dpos;
				dpos = dpossav1;
				mdsav2 = md;
				md = mdsav1;
				nfsm_loadattr(newvp, (struct vattr *)0);
				dpos = dpossav2;
				md = mdsav2;
				dp->d_type =
				    IFTODT(VTTOIF(np->n_vattr.va_type));
				ndp->ni_vp = newvp;
			        cache_enter(ndp->ni_dvp, ndp->ni_vp, cnp);
			    }
			} else {
			    /* Just skip over the file handle */
			    nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED);
			    i = fxdr_unsigned(int, *tl);
			    nfsm_adv(nfsm_rndup(i));
			}
			if (newvp != NULLVP) {
			    if (newvp == vp)
				vrele(newvp);
			    else
				vput(newvp);
			    newvp = NULLVP;
			}
			nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED);
			more_dirs = fxdr_unsigned(int, *tl);
		}
		/*
		 * If at end of rpc data, get the eof boolean
		 */
		if (!more_dirs) {
			nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED);
			more_dirs = (fxdr_unsigned(int, *tl) == 0);
		}
		m_freem(mrep);
	}
	/*
	 * Fill last record, iff any, out to a multiple of DIRBLKSIZ
	 * by increasing d_reclen for the last record.
	 */
	if (blksiz > 0) {
		left = DIRBLKSIZ - blksiz;
		dp->d_reclen += left;
		uiop->uio_iov->iov_base += left;
		uiop->uio_iov->iov_len -= left;
		uiop->uio_offset += left;
		uiop->uio_resid -= left;
	}

	/*
	 * We are now either at the end of the directory or have filled the
	 * block.
	 */
	if (bigenough)
		dnp->n_direofoffset = uiop->uio_offset;
	else {
		if (uiop->uio_resid > 0)
			printf("EEK! readdirplusrpc resid > 0\n");
		cookiep = nfs_getcookie(dnp, uiop->uio_offset, 1);
		*cookiep = cookie;
	}
nfsmout:
	if (newvp != NULLVP) {
	        if (newvp == vp)
			vrele(newvp);
		else
			vput(newvp);
		newvp = NULLVP;
	}
	return (error);
}

/*
 * Silly rename. To make the NFS filesystem that is stateless look a little
 * more like the "ufs" a remove of an active vnode is translated to a rename
 * to a funny looking filename that is removed by nfs_inactive on the
 * nfsnode. There is the potential for another process on a different client
 * to create the same funny name between the nfs_lookitup() fails and the
 * nfs_rename() completes, but...
 */
static int
nfs_sillyrename(dvp, vp, cnp)
	struct vnode *dvp, *vp;
	struct componentname *cnp;
{
	register struct sillyrename *sp;
	struct nfsnode *np;
	int error;
	short pid;

	cache_purge(dvp);
	np = VTONFS(vp);
#ifndef DIAGNOSTIC
	if (vp->v_type == VDIR)
		panic("nfs: sillyrename dir");
#endif
	MALLOC(sp, struct sillyrename *, sizeof (struct sillyrename),
		M_NFSREQ, M_WAITOK);
	sp->s_cred = crdup(cnp->cn_cred);
	sp->s_dvp = dvp;
	VREF(dvp);

	/* Fudge together a funny name */
	pid = cnp->cn_proc->p_pid;
	sp->s_namlen = sprintf(sp->s_name, ".nfsA%04x4.4", pid);

	/* Try lookitups until we get one that isn't there */
	while (nfs_lookitup(dvp, sp->s_name, sp->s_namlen, sp->s_cred,
		cnp->cn_proc, (struct nfsnode **)0) == 0) {
		sp->s_name[4]++;
		if (sp->s_name[4] > 'z') {
			error = EINVAL;
			goto bad;
		}
	}
	error = nfs_renameit(dvp, cnp, sp);
	if (error)
		goto bad;
	error = nfs_lookitup(dvp, sp->s_name, sp->s_namlen, sp->s_cred,
		cnp->cn_proc, &np);
	np->n_sillyrename = sp;
	return (0);
bad:
	vrele(sp->s_dvp);
	crfree(sp->s_cred);
	free((caddr_t)sp, M_NFSREQ);
	return (error);
}

/*
 * Look up a file name and optionally either update the file handle or
 * allocate an nfsnode, depending on the value of npp.
 * npp == NULL	--> just do the lookup
 * *npp == NULL --> allocate a new nfsnode and make sure attributes are
 *			handled too
 * *npp != NULL --> update the file handle in the vnode
 */
static int
nfs_lookitup(dvp, name, len, cred, procp, npp)
	register struct vnode *dvp;
	const char *name;
	int len;
	struct ucred *cred;
	struct proc *procp;
	struct nfsnode **npp;
{
	register u_int32_t *tl;
	register caddr_t cp;
	register int32_t t1, t2;
	struct vnode *newvp = (struct vnode *)0;
	struct nfsnode *np, *dnp = VTONFS(dvp);
	caddr_t bpos, dpos, cp2;
	int error = 0, fhlen, attrflag;
	struct mbuf *mreq, *mrep, *md, *mb, *mb2;
	nfsfh_t *nfhp;
	int v3 = NFS_ISV3(dvp);

	nfsstats.rpccnt[NFSPROC_LOOKUP]++;
	nfsm_reqhead(dvp, NFSPROC_LOOKUP,
		NFSX_FH(v3) + NFSX_UNSIGNED + nfsm_rndup(len));
	nfsm_fhtom(dvp, v3);
	nfsm_strtom(name, len, NFS_MAXNAMLEN);
	nfsm_request(dvp, NFSPROC_LOOKUP, procp, cred);
	if (npp && !error) {
		nfsm_getfh(nfhp, fhlen, v3);
		if (*npp) {
		    np = *npp;
		    if (np->n_fhsize > NFS_SMALLFH && fhlen <= NFS_SMALLFH) {
			free((caddr_t)np->n_fhp, M_NFSBIGFH);
			np->n_fhp = &np->n_fh;
		    } else if (np->n_fhsize <= NFS_SMALLFH && fhlen>NFS_SMALLFH)
			np->n_fhp =(nfsfh_t *)malloc(fhlen,M_NFSBIGFH,M_WAITOK);
		    bcopy((caddr_t)nfhp, (caddr_t)np->n_fhp, fhlen);
		    np->n_fhsize = fhlen;
		    newvp = NFSTOV(np);
		} else if (NFS_CMPFH(dnp, nfhp, fhlen)) {
		    VREF(dvp);
		    newvp = dvp;
		} else {
		    error = nfs_nget(dvp->v_mount, nfhp, fhlen, &np);
		    if (error) {
			m_freem(mrep);
			return (error);
		    }
		    newvp = NFSTOV(np);
		}
		if (v3) {
			nfsm_postop_attr(newvp, attrflag);
			if (!attrflag && *npp == NULL) {
				m_freem(mrep);
				if (newvp == dvp)
					vrele(newvp);
				else
					vput(newvp);
				return (ENOENT);
			}
		} else
			nfsm_loadattr(newvp, (struct vattr *)0);
	}
	nfsm_reqdone;
	if (npp && *npp == NULL) {
		if (error) {
			if (newvp) {
				if (newvp == dvp)
					vrele(newvp);
				else
					vput(newvp);
			}
		} else
			*npp = np;
	}
	return (error);
}

/*
 * Nfs Version 3 commit rpc
 */
int
nfs_commit(vp, offset, cnt, cred, procp)
	struct vnode *vp;
	u_quad_t offset;
	int cnt;
	struct ucred *cred;
	struct proc *procp;
{
	register caddr_t cp;
	register u_int32_t *tl;
	register int32_t t1, t2;
	register struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	caddr_t bpos, dpos, cp2;
	int error = 0, wccflag = NFSV3_WCCRATTR;
	struct mbuf *mreq, *mrep, *md, *mb, *mb2;
	
	if ((nmp->nm_state & NFSSTA_HASWRITEVERF) == 0)
		return (0);
	nfsstats.rpccnt[NFSPROC_COMMIT]++;
	nfsm_reqhead(vp, NFSPROC_COMMIT, NFSX_FH(1));
	nfsm_fhtom(vp, 1);
	nfsm_build(tl, u_int32_t *, 3 * NFSX_UNSIGNED);
	txdr_hyper(offset, tl);
	tl += 2;
	*tl = txdr_unsigned(cnt);
	nfsm_request(vp, NFSPROC_COMMIT, procp, cred);
	nfsm_wcc_data(vp, wccflag);
	if (!error) {
		nfsm_dissect(tl, u_int32_t *, NFSX_V3WRITEVERF);
		if (bcmp((caddr_t)nmp->nm_verf, (caddr_t)tl,
			NFSX_V3WRITEVERF)) {
			bcopy((caddr_t)tl, (caddr_t)nmp->nm_verf,
				NFSX_V3WRITEVERF);
			error = NFSERR_STALEWRITEVERF;
		}
	}
	nfsm_reqdone;
	return (error);
}

/*
 * Kludge City..
 * - make nfs_bmap() essentially a no-op that does no translation
 * - do nfs_strategy() by doing I/O with nfs_readrpc/nfs_writerpc
 *   (Maybe I could use the process's page mapping, but I was concerned that
 *    Kernel Write might not be enabled and also figured copyout() would do
 *    a lot more work than bcopy() and also it currently happens in the
 *    context of the swapper process (2).
 */
static int
nfs_bmap(ap)
	struct vop_bmap_args /* {
		struct vnode *a_vp;
		daddr_t  a_bn;
		struct vnode **a_vpp;
		daddr_t *a_bnp;
		int *a_runp;
		int *a_runb;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;

	if (ap->a_vpp != NULL)
		*ap->a_vpp = vp;
	if (ap->a_bnp != NULL)
		*ap->a_bnp = ap->a_bn * btodb(vp->v_mount->mnt_stat.f_iosize);
	if (ap->a_runp != NULL)
		*ap->a_runp = 0;
	if (ap->a_runb != NULL)
		*ap->a_runb = 0;
	return (0);
}

/*
 * Strategy routine.
 * For async requests when nfsiod(s) are running, queue the request by
 * calling nfs_asyncio(), otherwise just all nfs_doio() to do the
 * request.
 */
static int
nfs_strategy(ap)
	struct vop_strategy_args *ap;
{
	register struct buf *bp = ap->a_bp;
	struct ucred *cr;
	struct proc *p;
	int error = 0;

	KASSERT(!(bp->b_flags & B_DONE), ("nfs_strategy: buffer %p unexpectedly marked B_DONE", bp));
	KASSERT(BUF_REFCNT(bp) > 0, ("nfs_strategy: buffer %p not locked", bp));

	if (bp->b_flags & B_PHYS)
		panic("nfs physio");

	if (bp->b_flags & B_ASYNC)
		p = (struct proc *)0;
	else
		p = curproc;	/* XXX */

	if (bp->b_flags & B_READ)
		cr = bp->b_rcred;
	else
		cr = bp->b_wcred;

	/*
	 * If the op is asynchronous and an i/o daemon is waiting
	 * queue the request, wake it up and wait for completion
	 * otherwise just do it ourselves.
	 */
	if ((bp->b_flags & B_ASYNC) == 0 ||
		nfs_asyncio(bp, NOCRED, p))
		error = nfs_doio(bp, cr, p);
	return (error);
}

/*
 * Mmap a file
 *
 * NB Currently unsupported.
 */
/* ARGSUSED */
static int
nfs_mmap(ap)
	struct vop_mmap_args /* {
		struct vnode *a_vp;
		int  a_fflags;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{

	return (EINVAL);
}

/*
 * fsync vnode op. Just call nfs_flush() with commit == 1.
 */
/* ARGSUSED */
static int
nfs_fsync(ap)
	struct vop_fsync_args /* {
		struct vnodeop_desc *a_desc;
		struct vnode * a_vp;
		struct ucred * a_cred;
		int  a_waitfor;
		struct proc * a_p;
	} */ *ap;
{

	return (nfs_flush(ap->a_vp, ap->a_cred, ap->a_waitfor, ap->a_p, 1));
}

/*
 * Flush all the blocks associated with a vnode.
 * 	Walk through the buffer pool and push any dirty pages
 *	associated with the vnode.
 */
static int
nfs_flush(vp, cred, waitfor, p, commit)
	register struct vnode *vp;
	struct ucred *cred;
	int waitfor;
	struct proc *p;
	int commit;
{
	register struct nfsnode *np = VTONFS(vp);
	register struct buf *bp;
	register int i;
	struct buf *nbp;
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	int s, error = 0, slptimeo = 0, slpflag = 0, retv, bvecpos;
	int passone = 1;
	u_quad_t off, endoff, toff;
	struct ucred* wcred = NULL;
	struct buf **bvec = NULL;
#ifndef NFS_COMMITBVECSIZ
#define NFS_COMMITBVECSIZ	20
#endif
	struct buf *bvec_on_stack[NFS_COMMITBVECSIZ];
	int bvecsize = 0, bveccount;

	if (nmp->nm_flag & NFSMNT_INT)
		slpflag = PCATCH;
	if (!commit)
		passone = 0;
	/*
	 * A b_flags == (B_DELWRI | B_NEEDCOMMIT) block has been written to the
	 * server, but nas not been committed to stable storage on the server
	 * yet. On the first pass, the byte range is worked out and the commit
	 * rpc is done. On the second pass, nfs_writebp() is called to do the
	 * job.
	 */
again:
	off = (u_quad_t)-1;
	endoff = 0;
	bvecpos = 0;
	if (NFS_ISV3(vp) && commit) {
		s = splbio();
		/*
		 * Count up how many buffers waiting for a commit.
		 */
		bveccount = 0;
		for (bp = TAILQ_FIRST(&vp->v_dirtyblkhd); bp; bp = nbp) {
			nbp = TAILQ_NEXT(bp, b_vnbufs);
			if (BUF_REFCNT(bp) == 0 &&
			    (bp->b_flags & (B_DELWRI | B_NEEDCOMMIT))
				== (B_DELWRI | B_NEEDCOMMIT))
				bveccount++;
		}
		/*
		 * Allocate space to remember the list of bufs to commit.  It is
		 * important to use M_NOWAIT here to avoid a race with nfs_write.
		 * If we can't get memory (for whatever reason), we will end up
		 * committing the buffers one-by-one in the loop below.
		 */
		if (bvec != NULL && bvec != bvec_on_stack)
			free(bvec, M_TEMP);
		if (bveccount > NFS_COMMITBVECSIZ) {
			bvec = (struct buf **)
				malloc(bveccount * sizeof(struct buf *),
				       M_TEMP, M_NOWAIT);
			if (bvec == NULL) {
				bvec = bvec_on_stack;
				bvecsize = NFS_COMMITBVECSIZ;
			} else
				bvecsize = bveccount;
		} else {
			bvec = bvec_on_stack;
			bvecsize = NFS_COMMITBVECSIZ;
		}
		for (bp = TAILQ_FIRST(&vp->v_dirtyblkhd); bp; bp = nbp) {
			nbp = TAILQ_NEXT(bp, b_vnbufs);
			if (bvecpos >= bvecsize)
				break;
			if ((bp->b_flags & (B_DELWRI | B_NEEDCOMMIT)) !=
			    (B_DELWRI | B_NEEDCOMMIT) ||
			    BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT))
				continue;
			bremfree(bp);
			/*
			 * Work out if all buffers are using the same cred
			 * so we can deal with them all with one commit.
			 *
			 * NOTE: we are not clearing B_DONE here, so we have
			 * to do it later on in this routine if we intend to 
			 * initiate I/O on the bp.
			 *
			 * Note: to avoid loopback deadlocks, we do not
			 * assign b_runningbufspace.
			 */
			if (wcred == NULL)
				wcred = bp->b_wcred;
			else if (wcred != bp->b_wcred)
				wcred = NOCRED;
			bp->b_flags |= B_WRITEINPROG;
			vfs_busy_pages(bp, 1);

			/*
			 * bp is protected by being locked, but nbp is not
			 * and vfs_busy_pages() may sleep.  We have to
			 * recalculate nbp.
			 */
			nbp = TAILQ_NEXT(bp, b_vnbufs);

			/*
			 * A list of these buffers is kept so that the
			 * second loop knows which buffers have actually
			 * been committed. This is necessary, since there
			 * may be a race between the commit rpc and new
			 * uncommitted writes on the file.
			 */
			bvec[bvecpos++] = bp;
			toff = ((u_quad_t)bp->b_blkno) * DEV_BSIZE +
				bp->b_dirtyoff;
			if (toff < off)
				off = toff;
			toff += (u_quad_t)(bp->b_dirtyend - bp->b_dirtyoff);
			if (toff > endoff)
				endoff = toff;
		}
		splx(s);
	}
	if (bvecpos > 0) {
		/*
		 * Commit data on the server, as required.
		 * If all bufs are using the same wcred, then use that with
		 * one call for all of them, otherwise commit each one
		 * separately.
		 */
		if (wcred != NOCRED)
			retv = nfs_commit(vp, off, (int)(endoff - off),
					  wcred, p);
		else {
			retv = 0;
			for (i = 0; i < bvecpos; i++) {
				off_t off, size;
				bp = bvec[i];
				off = ((u_quad_t)bp->b_blkno) * DEV_BSIZE +
					bp->b_dirtyoff;
				size = (u_quad_t)(bp->b_dirtyend
						  - bp->b_dirtyoff);
				retv = nfs_commit(vp, off, (int)size,
						  bp->b_wcred, p);
				if (retv) break;
			}
		}

		if (retv == NFSERR_STALEWRITEVERF)
			nfs_clearcommit(vp->v_mount);

		/*
		 * Now, either mark the blocks I/O done or mark the
		 * blocks dirty, depending on whether the commit
		 * succeeded.
		 */
		for (i = 0; i < bvecpos; i++) {
			bp = bvec[i];
			bp->b_flags &= ~(B_NEEDCOMMIT | B_WRITEINPROG | B_CLUSTEROK);
			if (retv) {
				/*
				 * Error, leave B_DELWRI intact
				 */
				vfs_unbusy_pages(bp);
				brelse(bp);
			} else {
				/*
				 * Success, remove B_DELWRI ( bundirty() ).
				 *
				 * b_dirtyoff/b_dirtyend seem to be NFS 
				 * specific.  We should probably move that
				 * into bundirty(). XXX
				 */
				s = splbio();
				vp->v_numoutput++;
				bp->b_flags |= B_ASYNC;
				bundirty(bp);
				bp->b_flags &= ~(B_READ|B_DONE|B_ERROR);
				bp->b_dirtyoff = bp->b_dirtyend = 0;
				splx(s);
				biodone(bp);
			}
		}
	}

	/*
	 * Start/do any write(s) that are required.
	 */
loop:
	s = splbio();
	for (bp = TAILQ_FIRST(&vp->v_dirtyblkhd); bp; bp = nbp) {
		nbp = TAILQ_NEXT(bp, b_vnbufs);
		if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT)) {
			if (waitfor != MNT_WAIT || passone)
				continue;
			error = BUF_TIMELOCK(bp, LK_EXCLUSIVE | LK_SLEEPFAIL,
			    "nfsfsync", slpflag, slptimeo);
			splx(s);
			if (error == 0)
				panic("nfs_fsync: inconsistent lock");
			if (error == ENOLCK)
				goto loop;
			if (nfs_sigintr(nmp, (struct nfsreq *)0, p)) {
				error = EINTR;
				goto done;
			}
			if (slpflag == PCATCH) {
				slpflag = 0;
				slptimeo = 2 * hz;
			}
			goto loop;
		}
		if ((bp->b_flags & B_DELWRI) == 0)
			panic("nfs_fsync: not dirty");
		if ((passone || !commit) && (bp->b_flags & B_NEEDCOMMIT)) {
			BUF_UNLOCK(bp);
			continue;
		}
		bremfree(bp);
		if (passone || !commit)
		    bp->b_flags |= B_ASYNC;
		else
		    bp->b_flags |= B_ASYNC | B_WRITEINPROG;
		splx(s);
		VOP_BWRITE(bp->b_vp, bp);
		goto loop;
	}
	splx(s);
	if (passone) {
		passone = 0;
		goto again;
	}
	if (waitfor == MNT_WAIT) {
		while (vp->v_numoutput) {
			vp->v_flag |= VBWAIT;
			error = tsleep((caddr_t)&vp->v_numoutput,
				slpflag | (PRIBIO + 1), "nfsfsync", slptimeo);
			if (error) {
			    if (nfs_sigintr(nmp, (struct nfsreq *)0, p)) {
				error = EINTR;
				goto done;
			    }
			    if (slpflag == PCATCH) {
				slpflag = 0;
				slptimeo = 2 * hz;
			    }
			}
		}
		if (!TAILQ_EMPTY(&vp->v_dirtyblkhd) && commit) {
			goto loop;
		}
	}
	if (np->n_flag & NWRITEERR) {
		error = np->n_error;
		np->n_flag &= ~NWRITEERR;
	}
done:
	if (bvec != NULL && bvec != bvec_on_stack)
		free(bvec, M_TEMP);
	return (error);
}

/*
 * NFS advisory byte-level locks.
 * Currently unsupported.
 */
static int
nfs_advlock(ap)
	struct vop_advlock_args /* {
		struct vnode *a_vp;
		caddr_t  a_id;
		int  a_op;
		struct flock *a_fl;
		int  a_flags;
	} */ *ap;
{
	register struct nfsnode *np = VTONFS(ap->a_vp);

	/*
	 * The following kludge is to allow diskless support to work
	 * until a real NFS lockd is implemented. Basically, just pretend
	 * that this is a local lock.
	 */
	return (lf_advlock(ap, &(np->n_lockf), np->n_size));
}

/*
 * Print out the contents of an nfsnode.
 */
static int
nfs_print(ap)
	struct vop_print_args /* {
		struct vnode *a_vp;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct nfsnode *np = VTONFS(vp);

	printf("tag VT_NFS, fileid %ld fsid 0x%x",
		np->n_vattr.va_fileid, np->n_vattr.va_fsid);
	if (vp->v_type == VFIFO)
		fifo_printinfo(vp);
	printf("\n");
	return (0);
}

/*
 * Just call nfs_writebp() with the force argument set to 1.
 *
 * NOTE: B_DONE may or may not be set in a_bp on call.
 */
static int
nfs_bwrite(ap)
	struct vop_bwrite_args /* {
		struct vnode *a_bp;
	} */ *ap;
{
	return (nfs_writebp(ap->a_bp, 1, curproc));
}

/*
 * This is a clone of vn_bwrite(), except that B_WRITEINPROG isn't set unless
 * the force flag is one and it also handles the B_NEEDCOMMIT flag.  We set
 * B_CACHE if this is a VMIO buffer.
 */
int
nfs_writebp(bp, force, procp)
	register struct buf *bp;
	int force;
	struct proc *procp;
{
	int s;
	int oldflags = bp->b_flags;
#if 0
	int retv = 1;
	off_t off;
#endif

	if (BUF_REFCNT(bp) == 0)
		panic("bwrite: buffer is not locked???");

	if (bp->b_flags & B_INVAL) {
		brelse(bp);
		return(0);
	}

	bp->b_flags |= B_CACHE;

	/*
	 * Undirty the bp.  We will redirty it later if the I/O fails.
	 */

	s = splbio();
	bundirty(bp);
	bp->b_flags &= ~(B_READ|B_DONE|B_ERROR);

	bp->b_vp->v_numoutput++;
	curproc->p_stats->p_ru.ru_oublock++;
	splx(s);

	/*
	 * Note: to avoid loopback deadlocks, we do not
	 * assign b_runningbufspace.
	 */
	vfs_busy_pages(bp, 1);

	if (force)
		bp->b_flags |= B_WRITEINPROG;
	BUF_KERNPROC(bp);
	VOP_STRATEGY(bp->b_vp, bp);

	if( (oldflags & B_ASYNC) == 0) {
		int rtval = biowait(bp);

		if (oldflags & B_DELWRI) {
			s = splbio();
			reassignbuf(bp, bp->b_vp);
			splx(s);
		}

		brelse(bp);
		return (rtval);
	} 

	return (0);
}

/*
 * nfs special file access vnode op.
 * Essentially just get vattr and then imitate iaccess() since the device is
 * local to the client.
 */
static int
nfsspec_access(ap)
	struct vop_access_args /* {
		struct vnode *a_vp;
		int  a_mode;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
	register struct vattr *vap;
	register gid_t *gp;
	register struct ucred *cred = ap->a_cred;
	struct vnode *vp = ap->a_vp;
	mode_t mode = ap->a_mode;
	struct vattr vattr;
	register int i;
	int error;

	/*
	 * Disallow write attempts on filesystems mounted read-only;
	 * unless the file is a socket, fifo, or a block or character
	 * device resident on the filesystem.
	 */
	if ((mode & VWRITE) && (vp->v_mount->mnt_flag & MNT_RDONLY)) {
		switch (vp->v_type) {
		case VREG:
		case VDIR:
		case VLNK:
			return (EROFS);
		default:
			break;
		}
	}
	/*
	 * If you're the super-user,
	 * you always get access.
	 */
	if (cred->cr_uid == 0)
		return (0);
	vap = &vattr;
	error = VOP_GETATTR(vp, vap, cred, ap->a_p);
	if (error)
		return (error);
	/*
	 * Access check is based on only one of owner, group, public.
	 * If not owner, then check group. If not a member of the
	 * group, then check public access.
	 */
	if (cred->cr_uid != vap->va_uid) {
		mode >>= 3;
		gp = cred->cr_groups;
		for (i = 0; i < cred->cr_ngroups; i++, gp++)
			if (vap->va_gid == *gp)
				goto found;
		mode >>= 3;
found:
		;
	}
	error = (vap->va_mode & mode) == mode ? 0 : EACCES;
	return (error);
}

/*
 * Read wrapper for special devices.
 */
static int
nfsspec_read(ap)
	struct vop_read_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int  a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	register struct nfsnode *np = VTONFS(ap->a_vp);

	/*
	 * Set access flag.
	 */
	np->n_flag |= NACC;
	getnanotime(&np->n_atim);
	return (VOCALL(spec_vnodeop_p, VOFFSET(vop_read), ap));
}

/*
 * Write wrapper for special devices.
 */
static int
nfsspec_write(ap)
	struct vop_write_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int  a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	register struct nfsnode *np = VTONFS(ap->a_vp);

	/*
	 * Set update flag.
	 */
	np->n_flag |= NUPD;
	getnanotime(&np->n_mtim);
	return (VOCALL(spec_vnodeop_p, VOFFSET(vop_write), ap));
}

/*
 * Close wrapper for special devices.
 *
 * Update the times on the nfsnode then do device close.
 */
static int
nfsspec_close(ap)
	struct vop_close_args /* {
		struct vnode *a_vp;
		int  a_fflag;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct nfsnode *np = VTONFS(vp);
	struct vattr vattr;

	if (np->n_flag & (NACC | NUPD)) {
		np->n_flag |= NCHG;
		if (vp->v_usecount == 1 &&
		    (vp->v_mount->mnt_flag & MNT_RDONLY) == 0) {
			VATTR_NULL(&vattr);
			if (np->n_flag & NACC)
				vattr.va_atime = np->n_atim;
			if (np->n_flag & NUPD)
				vattr.va_mtime = np->n_mtim;
			(void)VOP_SETATTR(vp, &vattr, ap->a_cred, ap->a_p);
		}
	}
	return (VOCALL(spec_vnodeop_p, VOFFSET(vop_close), ap));
}

/*
 * Read wrapper for fifos.
 */
static int
nfsfifo_read(ap)
	struct vop_read_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int  a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	register struct nfsnode *np = VTONFS(ap->a_vp);

	/*
	 * Set access flag.
	 */
	np->n_flag |= NACC;
	getnanotime(&np->n_atim);
	return (VOCALL(fifo_vnodeop_p, VOFFSET(vop_read), ap));
}

/*
 * Write wrapper for fifos.
 */
static int
nfsfifo_write(ap)
	struct vop_write_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int  a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	register struct nfsnode *np = VTONFS(ap->a_vp);

	/*
	 * Set update flag.
	 */
	np->n_flag |= NUPD;
	getnanotime(&np->n_mtim);
	return (VOCALL(fifo_vnodeop_p, VOFFSET(vop_write), ap));
}

/*
 * Close wrapper for fifos.
 *
 * Update the times on the nfsnode then do fifo close.
 */
static int
nfsfifo_close(ap)
	struct vop_close_args /* {
		struct vnode *a_vp;
		int  a_fflag;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct nfsnode *np = VTONFS(vp);
	struct vattr vattr;
	struct timespec ts;

	if (np->n_flag & (NACC | NUPD)) {
		getnanotime(&ts);
		if (np->n_flag & NACC)
			np->n_atim = ts;
		if (np->n_flag & NUPD)
			np->n_mtim = ts;
		np->n_flag |= NCHG;
		if (vp->v_usecount == 1 &&
		    (vp->v_mount->mnt_flag & MNT_RDONLY) == 0) {
			VATTR_NULL(&vattr);
			if (np->n_flag & NACC)
				vattr.va_atime = np->n_atim;
			if (np->n_flag & NUPD)
				vattr.va_mtime = np->n_mtim;
			(void)VOP_SETATTR(vp, &vattr, ap->a_cred, ap->a_p);
		}
	}
	return (VOCALL(fifo_vnodeop_p, VOFFSET(vop_close), ap));
}

