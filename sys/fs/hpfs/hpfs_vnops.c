/*-
 * Copyright (c) 1998, 1999 Semen Ustimenko (semenu@FreeBSD.org)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/fs/hpfs/hpfs_vnops.c,v 1.2.2.2 2002/01/15 18:35:09 semenu Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/dirent.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#if !defined(__FreeBSD__)
#include <vm/vm_prot.h>
#endif
#include <vm/vm_page.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <vm/vm_zone.h>
#if defined(__FreeBSD__)
#include <vm/vnode_pager.h>
#endif
#include <vm/vm_extern.h>

#if !defined(__FreeBSD__)
#include <miscfs/specfs/specdev.h>
#include <miscfs/genfs/genfs.h>
#endif

#include <sys/unistd.h> /* for pathconf(2) constants */

#include <fs/hpfs/hpfs.h>
#include <fs/hpfs/hpfsmount.h>
#include <fs/hpfs/hpfs_subr.h>
#include <fs/hpfs/hpfs_ioctl.h>

static int	hpfs_de_uiomove __P((struct hpfsmount *, struct hpfsdirent *,
				     struct uio *));
static int	hpfs_ioctl __P((struct vop_ioctl_args *ap));
static int	hpfs_read __P((struct vop_read_args *));
static int	hpfs_write __P((struct vop_write_args *ap));
static int	hpfs_getattr __P((struct vop_getattr_args *ap));
static int	hpfs_setattr __P((struct vop_setattr_args *ap));
static int	hpfs_inactive __P((struct vop_inactive_args *ap));
static int	hpfs_print __P((struct vop_print_args *ap));
static int	hpfs_reclaim __P((struct vop_reclaim_args *ap));
static int	hpfs_strategy __P((struct vop_strategy_args *ap));
static int	hpfs_access __P((struct vop_access_args *ap));
static int	hpfs_open __P((struct vop_open_args *ap));
static int	hpfs_close __P((struct vop_close_args *ap));
static int	hpfs_readdir __P((struct vop_readdir_args *ap));
static int	hpfs_lookup __P((struct vop_lookup_args *ap));
static int	hpfs_create __P((struct vop_create_args *));
static int	hpfs_remove __P((struct vop_remove_args *));
static int	hpfs_bmap __P((struct vop_bmap_args *ap));
#if defined(__FreeBSD__)
static int	hpfs_getpages __P((struct vop_getpages_args *ap));
static int	hpfs_putpages __P((struct vop_putpages_args *));
static int	hpfs_fsync __P((struct vop_fsync_args *ap));
#endif
static int	hpfs_pathconf __P((struct vop_pathconf_args *ap));

#if defined(__FreeBSD__)
int
hpfs_getpages(ap)
	struct vop_getpages_args *ap;
{
	return vnode_pager_generic_getpages(ap->a_vp, ap->a_m, ap->a_count,
		ap->a_reqpage);
}

int
hpfs_putpages(ap)
	struct vop_putpages_args *ap;
{
	return vnode_pager_generic_putpages(ap->a_vp, ap->a_m, ap->a_count,
		ap->a_sync, ap->a_rtvals);
}

static int
hpfs_fsync(ap)
	struct vop_fsync_args /* {
		struct vnode *a_vp;
		struct ucred *a_cred;
		int a_waitfor;
		struct proc *a_p;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	int s;
	struct buf *bp, *nbp;

	/*
	 * Flush all dirty buffers associated with a vnode.
	 */
loop:
	s = splbio();
	for (bp = TAILQ_FIRST(&vp->v_dirtyblkhd); bp; bp = nbp) {
		nbp = TAILQ_NEXT(bp, b_vnbufs);
		if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT))
			continue;
		if ((bp->b_flags & B_DELWRI) == 0)
			panic("hpfs_fsync: not dirty");
		bremfree(bp);
		splx(s);
		(void) bwrite(bp);
		goto loop;
	}
	while (vp->v_numoutput) {
		vp->v_flag |= VBWAIT;
		(void) tsleep((caddr_t)&vp->v_numoutput, PRIBIO + 1, "hpfsn", 0);
	}
#ifdef DIAGNOSTIC
	if (!TAILQ_EMPTY(&vp->v_dirtyblkhd)) {
		vprint("hpfs_fsync: dirty", vp);
		goto loop;
	}
#endif
	splx(s);

	/*
	 * Write out the on-disc version of the vnode.
	 */
	return hpfs_update(VTOHP(vp));
}

#endif

static int
hpfs_ioctl (
	struct vop_ioctl_args /* {
		struct vnode *a_vp;
		u_long a_command;
		caddr_t a_data;
		int a_fflag;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap)
{
	register struct vnode *vp = ap->a_vp;
	register struct hpfsnode *hp = VTOHP(vp);
	int error;

	printf("hpfs_ioctl(0x%x, 0x%lx, 0x%p, 0x%x): ",
		hp->h_no, ap->a_command, ap->a_data, ap->a_fflag);

	switch (ap->a_command) {
	case HPFSIOCGEANUM: {
		u_long eanum;
		u_long passed;
		struct ea *eap;

		eanum = 0;

		if (hp->h_fn.fn_ealen > 0) {
			eap = (struct ea *)&(hp->h_fn.fn_int);
			passed = 0;

			while (passed < hp->h_fn.fn_ealen) {

				printf("EAname: %s\n", EA_NAME(eap));

				eanum++;
				passed += sizeof(struct ea) +
					  eap->ea_namelen + 1 + eap->ea_vallen;
				eap = (struct ea *)((caddr_t)hp->h_fn.fn_int +
						passed);
			}
			error = 0;
		} else {
			error = ENOENT;
		}

		printf("%lu eas\n", eanum);

		*(u_long *)ap->a_data = eanum;

		break;
	}
	case HPFSIOCGEASZ: {
		u_long eanum;
		u_long passed;
		struct ea *eap;

		printf("EA%ld\n", *(u_long *)ap->a_data);

		eanum = 0;
		if (hp->h_fn.fn_ealen > 0) {
			eap = (struct ea *)&(hp->h_fn.fn_int);
			passed = 0;

			error = ENOENT;
			while (passed < hp->h_fn.fn_ealen) {
				printf("EAname: %s\n", EA_NAME(eap));

				if (eanum == *(u_long *)ap->a_data) {
					*(u_long *)ap->a_data =
					  	eap->ea_namelen + 1 +
						eap->ea_vallen;

					error = 0;
					break;
				}

				eanum++;
				passed += sizeof(struct ea) +
					  eap->ea_namelen + 1 + eap->ea_vallen;
				eap = (struct ea *)((caddr_t)hp->h_fn.fn_int +
						passed);
			}
		} else {
			error = ENOENT;
		}

		break;
	}
	case HPFSIOCRDEA: {
		u_long eanum;
		u_long passed;
		struct hpfs_rdea *rdeap;
		struct ea *eap;

		rdeap = (struct hpfs_rdea *)ap->a_data;
		printf("EA%ld\n", rdeap->ea_no);

		eanum = 0;
		if (hp->h_fn.fn_ealen > 0) {
			eap = (struct ea *)&(hp->h_fn.fn_int);
			passed = 0;

			error = ENOENT;
			while (passed < hp->h_fn.fn_ealen) {
				printf("EAname: %s\n", EA_NAME(eap));

				if (eanum == rdeap->ea_no) {
					rdeap->ea_sz = eap->ea_namelen + 1 +
							eap->ea_vallen;
					copyout(EA_NAME(eap),rdeap->ea_data,
						rdeap->ea_sz);
					error = 0;
					break;
				}

				eanum++;
				passed += sizeof(struct ea) +
					  eap->ea_namelen + 1 + eap->ea_vallen;
				eap = (struct ea *)((caddr_t)hp->h_fn.fn_int +
						passed);
			}
		} else {
			error = ENOENT;
		}

		break;
	}
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

/*
 * Map file offset to disk offset.
 */
int
hpfs_bmap(ap)
	struct vop_bmap_args /* {
		struct vnode *a_vp;
		daddr_t  a_bn;
		struct vnode **a_vpp;
		daddr_t *a_bnp;
		int *a_runp;
		int *a_runb;
	} */ *ap;
{
	register struct hpfsnode *hp = VTOHP(ap->a_vp);
	int error;

	if (ap->a_vpp != NULL) 
		*ap->a_vpp = hp->h_devvp;
#if defined(__FreeBSD__)
	if (ap->a_runb != NULL)
		*ap->a_runb = 0;
#endif
	if (ap->a_bnp == NULL)
		return (0);

	dprintf(("hpfs_bmap(0x%x, 0x%x): ",hp->h_no, ap->a_bn));

	error = hpfs_hpbmap (hp, ap->a_bn, ap->a_bnp, ap->a_runp);

	return (error);
}

static int
hpfs_read(ap)
	struct vop_read_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct hpfsnode *hp = VTOHP(vp);
	struct uio *uio = ap->a_uio;
	struct buf *bp;
	u_int xfersz, toread;
	u_int off;
	daddr_t lbn, bn;
	int resid;
	int runl;
	int error = 0;

	resid = min (uio->uio_resid, hp->h_fn.fn_size - uio->uio_offset);

	dprintf(("hpfs_read(0x%x, off: %d resid: %d, segflg: %d): [resid: 0x%x]\n",hp->h_no,(u_int32_t)uio->uio_offset,uio->uio_resid,uio->uio_segflg, resid));

	while (resid) {
		lbn = uio->uio_offset >> DEV_BSHIFT;
		off = uio->uio_offset & (DEV_BSIZE - 1);
		dprintf(("hpfs_read: resid: 0x%x lbn: 0x%x off: 0x%x\n",
			uio->uio_resid, lbn, off));
		error = hpfs_hpbmap(hp, lbn, &bn, &runl);
		if (error)
			return (error);

		toread = min(off + resid, min(DFLTPHYS, (runl+1)*DEV_BSIZE));
		xfersz = (toread + DEV_BSIZE - 1) & ~(DEV_BSIZE - 1);
		dprintf(("hpfs_read: bn: 0x%x (0x%x) toread: 0x%x (0x%x)\n",
			bn, runl, toread, xfersz));

		if (toread == 0) 
			break;

		error = bread(hp->h_devvp, bn, xfersz, NOCRED, &bp);
		if (error) {
			brelse(bp);
			break;
		}

		error = uiomove(bp->b_data + off, toread - off, uio);
		if(error) {
			brelse(bp);
			break;
		}
		brelse(bp);
		resid -= toread;
	}
	dprintf(("hpfs_read: successful\n"));
	return (error);
}

static int
hpfs_write(ap)
	struct vop_write_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int  a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct hpfsnode *hp = VTOHP(vp);
	struct uio *uio = ap->a_uio;
	struct buf *bp;
	u_int xfersz, towrite;
	u_int off;
	daddr_t lbn, bn;
	int runl;
	int error = 0;

	dprintf(("hpfs_write(0x%x, off: %d resid: %d, segflg: %d):\n",hp->h_no,(u_int32_t)uio->uio_offset,uio->uio_resid,uio->uio_segflg));

	if (ap->a_ioflag & IO_APPEND) {
		dprintf(("hpfs_write: APPEND mode\n"));
		uio->uio_offset = hp->h_fn.fn_size;
	}
	if (uio->uio_offset + uio->uio_resid > hp->h_fn.fn_size) {
		error = hpfs_extend (hp, uio->uio_offset + uio->uio_resid);
		if (error) {
			printf("hpfs_write: hpfs_extend FAILED %d\n", error);
			return (error);
		}
	}

	while (uio->uio_resid) {
		lbn = uio->uio_offset >> DEV_BSHIFT;
		off = uio->uio_offset & (DEV_BSIZE - 1);
		dprintf(("hpfs_write: resid: 0x%x lbn: 0x%x off: 0x%x\n",
			uio->uio_resid, lbn, off));
		error = hpfs_hpbmap(hp, lbn, &bn, &runl);
		if (error)
			return (error);

		towrite = min(off + uio->uio_resid, min(DFLTPHYS, (runl+1)*DEV_BSIZE));
		xfersz = (towrite + DEV_BSIZE - 1) & ~(DEV_BSIZE - 1);
		dprintf(("hpfs_write: bn: 0x%x (0x%x) towrite: 0x%x (0x%x)\n",
			bn, runl, towrite, xfersz));

		if ((off == 0) && (towrite == xfersz)) {
			bp = getblk(hp->h_devvp, bn, xfersz, 0, 0);
			clrbuf(bp);
		} else {
			error = bread(hp->h_devvp, bn, xfersz, NOCRED, &bp);
			if (error) {
				brelse(bp);
				return (error);
			}
		}

		error = uiomove(bp->b_data + off, towrite - off, uio);
		if(error) {
			brelse(bp);
			return (error);
		}

		if (ap->a_ioflag & IO_SYNC)
			bwrite(bp);
		else
			bawrite(bp);
	}

	dprintf(("hpfs_write: successful\n"));
	return (0);
}

/*
 * XXXXX do we need hpfsnode locking inside?
 */
static int
hpfs_getattr(ap)
	struct vop_getattr_args /* {
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct hpfsnode *hp = VTOHP(vp);
	register struct vattr *vap = ap->a_vap;
	int error;

	dprintf(("hpfs_getattr(0x%x):\n", hp->h_no));

#if defined(__FreeBSD__)
	vap->va_fsid = dev2udev(hp->h_dev);
#else /* defined(__NetBSD__) */
	vap->va_fsid = ip->i_dev;
#endif
	vap->va_fileid = hp->h_no;
	vap->va_mode = hp->h_mode;
	vap->va_nlink = 1;
	vap->va_uid = hp->h_uid;
	vap->va_gid = hp->h_gid;
	vap->va_rdev = 0;				/* XXX UNODEV ? */
	vap->va_size = hp->h_fn.fn_size;
	vap->va_bytes = ((hp->h_fn.fn_size + DEV_BSIZE-1) & ~(DEV_BSIZE-1)) +
			DEV_BSIZE;

	if (!(hp->h_flag & H_PARVALID)) {
		error = hpfs_validateparent(hp);
		if (error) 
			return (error);
	}
	vap->va_atime = hpfstimetounix(hp->h_atime);
	vap->va_mtime = hpfstimetounix(hp->h_mtime);
	vap->va_ctime = hpfstimetounix(hp->h_ctime);

	vap->va_flags = 0;
	vap->va_gen = 0;
	vap->va_blocksize = DEV_BSIZE;
	vap->va_type = vp->v_type;
	vap->va_filerev = 0;

	return (0);
}

/*
 * XXXXX do we need hpfsnode locking inside?
 */
static int
hpfs_setattr(ap)
	struct vop_setattr_args /* {
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct hpfsnode *hp = VTOHP(vp);
	struct vattr *vap = ap->a_vap;
	struct ucred *cred = ap->a_cred;
	struct proc *p = ap->a_p;
	int error;

	dprintf(("hpfs_setattr(0x%x):\n", hp->h_no));

	/*
	 * Check for unsettable attributes.
	 */
	if ((vap->va_type != VNON) || (vap->va_nlink != VNOVAL) ||
	    (vap->va_fsid != VNOVAL) || (vap->va_fileid != VNOVAL) ||
	    (vap->va_blocksize != VNOVAL) || (vap->va_rdev != VNOVAL) ||
	    (vap->va_bytes != VNOVAL) || (vap->va_gen != VNOVAL)) {
		dprintf(("hpfs_setattr: changing nonsettable attr\n"));
		return (EINVAL);
	}

	/* Can't change flags XXX Could be implemented */
	if (vap->va_flags != VNOVAL) {
		printf("hpfs_setattr: FLAGS CANNOT BE SET\n");
		return (EINVAL);
	}

	/* Can't change uid/gid XXX Could be implemented */
	if (vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL) {
		printf("hpfs_setattr: UID/GID CANNOT BE SET\n");
		return (EINVAL);
	}

	/* Can't change mode XXX Could be implemented */
	if (vap->va_mode != (mode_t)VNOVAL) {
		printf("hpfs_setattr: MODE CANNOT BE SET\n");
		return (EINVAL);
	}

	/* Update times */
	if (vap->va_atime.tv_sec != VNOVAL || vap->va_mtime.tv_sec != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		if (cred->cr_uid != hp->h_uid &&
		    (error = suser_xxx(cred, p, PRISON_ROOT)) &&
		    ((vap->va_vaflags & VA_UTIMES_NULL) == 0 ||
		    (error = VOP_ACCESS(vp, VWRITE, cred, p))))
			return (error);
		if (vap->va_atime.tv_sec != VNOVAL)
			hp->h_atime = vap->va_atime.tv_sec;
		if (vap->va_mtime.tv_sec != VNOVAL)
			hp->h_mtime = vap->va_mtime.tv_sec;

		hp->h_flag |= H_PARCHANGE;
	}

	if (vap->va_size != VNOVAL) {
		switch (vp->v_type) {
		case VDIR:
			return (EISDIR);
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
			break;
		default:
			printf("hpfs_setattr: WRONG v_type\n");
			return (EINVAL);
		}

		if (vap->va_size < hp->h_fn.fn_size) {
#if defined(__FreeBSD__)
			error = vtruncbuf(vp, cred, p, vap->va_size, DEV_BSIZE);
			if (error)
				return (error);
#else /* defined(__NetBSD__) */
#error Need alternation for vtruncbuf()
#endif
			error = hpfs_truncate(hp, vap->va_size);
			if (error)
				return (error);

		} else if (vap->va_size > hp->h_fn.fn_size) {
#if defined(__FreeBSD__)
			vnode_pager_setsize(vp, vap->va_size);
#endif
			error = hpfs_extend(hp, vap->va_size);
			if (error)
				return (error);
		}
	}

	return (0);
}

/*
 * Last reference to an node.  If necessary, write or delete it.
 */
int
hpfs_inactive(ap)
	struct vop_inactive_args /* {
		struct vnode *a_vp;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct hpfsnode *hp = VTOHP(vp);
	int error;

	dprintf(("hpfs_inactive(0x%x): \n", hp->h_no));

	if (hp->h_flag & H_CHANGE) {
		dprintf(("hpfs_inactive: node changed, update\n"));
		error = hpfs_update (hp);
		if (error)
			return (error);
	}

	if (hp->h_flag & H_PARCHANGE) {
		dprintf(("hpfs_inactive: parent node changed, update\n"));
		error = hpfs_updateparent (hp);
		if (error)
			return (error);
	}

	if (prtactive && vp->v_usecount != 0)
		vprint("hpfs_inactive: pushing active", vp);

	if (hp->h_flag & H_INVAL) {
		VOP__UNLOCK(vp,0,ap->a_p);
#if defined(__FreeBSD__)
		vrecycle(vp, (struct simplelock *)0, ap->a_p);
#else /* defined(__NetBSD__) */
		vgone(vp);
#endif
		return (0);
	}

	VOP__UNLOCK(vp,0,ap->a_p);
	return (0);
}

/*
 * Reclaim an inode so that it can be used for other purposes.
 */
int
hpfs_reclaim(ap)
	struct vop_reclaim_args /* {
		struct vnode *a_vp;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct hpfsnode *hp = VTOHP(vp);

	dprintf(("hpfs_reclaim(0x%x0): \n", hp->h_no));

	hpfs_hphashrem(hp);

	/* Purge old data structures associated with the inode. */
	cache_purge(vp);
	if (hp->h_devvp) {
		vrele(hp->h_devvp);
		hp->h_devvp = NULL;
	}

	vp->v_data = NULL;

	FREE(hp, M_HPFSNO);

	return (0);
}

static int
hpfs_print(ap)
	struct vop_print_args /* {
		struct vnode *a_vp;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct hpfsnode *hp = VTOHP(vp);

	printf("tag VT_HPFS, ino 0x%x",hp->h_no);
	lockmgr_printinfo(&hp->h_lock);
	printf("\n");
	return (0);
}

/*
 * Calculate the logical to physical mapping if not done already,
 * then call the device strategy routine.
 *
 * In order to be able to swap to a file, the VOP_BMAP operation may not
 * deadlock on memory.  See hpfs_bmap() for details. XXXXXXX (not impl)
 */
int
hpfs_strategy(ap)
	struct vop_strategy_args /* {
		struct buf *a_bp;
	} */ *ap;
{
	register struct buf *bp = ap->a_bp;
	register struct vnode *vp = ap->a_vp;
	struct vnode *nvp;
	int error;

	dprintf(("hpfs_strategy(): \n"));

	if (vp->v_type == VBLK || vp->v_type == VCHR)
		panic("hpfs_strategy: spec");
	if (bp->b_blkno == bp->b_lblkno) {
		error = VOP_BMAP(vp, bp->b_lblkno, &nvp, &bp->b_blkno, NULL, NULL);
		if (error) {
			printf("hpfs_strategy: VOP_BMAP FAILED %d\n", error);
			bp->b_error = error;
			bp->b_flags |= B_ERROR;
			biodone(bp);
			return (error);
		}
		if ((long)bp->b_blkno == -1)
			vfs_bio_clrbuf(bp);
	}
	if ((long)bp->b_blkno == -1) {
		biodone(bp);
		return (0);
	}
	bp->b_dev = nvp->v_rdev;
	VOP_STRATEGY(nvp, bp);
	return (0);
}

/*
 * XXXXX do we need hpfsnode locking inside?
 */
int
hpfs_access(ap)
	struct vop_access_args /* {
		struct vnode *a_vp;
		int  a_mode;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct hpfsnode *hp = VTOHP(vp);
	struct ucred *cred = ap->a_cred;
	mode_t mask, mode = ap->a_mode;
	register gid_t *gp;
	int i;

	dprintf(("hpfs_access(0x%x):\n", hp->h_no));

	/*
	 * Disallow write attempts on read-only file systems;
	 * unless the file is a socket, fifo, or a block or
	 * character device resident on the file system.
	 */
	if (mode & VWRITE) {
		switch ((int)vp->v_type) {
		case VDIR:
		case VLNK:
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
			break;
		}
	}

	/* Otherwise, user id 0 always gets access. */
	if (cred->cr_uid == 0)
		return (0);

	mask = 0;

	/* Otherwise, check the owner. */
	if (cred->cr_uid == hp->h_uid) {
		if (mode & VEXEC)
			mask |= S_IXUSR;
		if (mode & VREAD)
			mask |= S_IRUSR;
		if (mode & VWRITE)
			mask |= S_IWUSR;
		return ((hp->h_mode & mask) == mask ? 0 : EACCES);
	}

	/* Otherwise, check the groups. */
	for (i = 0, gp = cred->cr_groups; i < cred->cr_ngroups; i++, gp++)
		if (hp->h_gid == *gp) {
			if (mode & VEXEC)
				mask |= S_IXGRP;
			if (mode & VREAD)
				mask |= S_IRGRP;
			if (mode & VWRITE)
				mask |= S_IWGRP;
			return ((hp->h_mode & mask) == mask ? 0 : EACCES);
		}

	/* Otherwise, check everyone else. */
	if (mode & VEXEC)
		mask |= S_IXOTH;
	if (mode & VREAD)
		mask |= S_IROTH;
	if (mode & VWRITE)
		mask |= S_IWOTH;
	return ((hp->h_mode & mask) == mask ? 0 : EACCES);
}

/*
 * Open called.
 *
 * Nothing to do.
 */
/* ARGSUSED */
static int
hpfs_open(ap)
	struct vop_open_args /* {
		struct vnode *a_vp;
		int  a_mode;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
#if HPFS_DEBUG
	register struct vnode *vp = ap->a_vp;
	register struct hpfsnode *hp = VTOHP(vp);

	printf("hpfs_open(0x%x):\n",hp->h_no);
#endif

	/*
	 * Files marked append-only must be opened for appending.
	 */

	return (0);
}

/*
 * Close called.
 *
 * Update the times on the inode.
 */
/* ARGSUSED */
static int
hpfs_close(ap)
	struct vop_close_args /* {
		struct vnode *a_vp;
		int  a_fflag;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
#if HPFS_DEBUG
	register struct vnode *vp = ap->a_vp;
	register struct hpfsnode *hp = VTOHP(vp);

	printf("hpfs_close: %d\n",hp->h_no);
#endif

	return (0);
}

static int
hpfs_de_uiomove (
	struct hpfsmount *hpmp,
	struct hpfsdirent *dep,
	struct uio *uio)
{
	struct dirent cde;
	int i, error;

	dprintf(("[no: 0x%x, size: %d, name: %2d:%.*s, flag: 0x%x] ",
		dep->de_fnode, dep->de_size, dep->de_namelen,
		dep->de_namelen, dep->de_name, dep->de_flag));

	/*strncpy(cde.d_name, dep->de_name, dep->de_namelen);*/
	for (i=0; i<dep->de_namelen; i++) 
		cde.d_name[i] = hpfs_d2u(hpmp, dep->de_name[i]);

	cde.d_name[dep->de_namelen] = '\0';
	cde.d_namlen = dep->de_namelen;
	cde.d_fileno = dep->de_fnode;
	cde.d_type = (dep->de_flag & DE_DIR) ? DT_DIR : DT_REG;
	cde.d_reclen = sizeof(struct dirent);

	error = uiomove((char *)&cde, sizeof(struct dirent), uio);
	if (error)
		return (error);
	
	dprintf(("[0x%x] ", uio->uio_resid));
	return (error);
}


static struct dirent hpfs_de_dot =
	{ 0, sizeof(struct dirent), DT_DIR, 1, "." };
static struct dirent hpfs_de_dotdot =
	{ 0, sizeof(struct dirent), DT_DIR, 2, ".." };
int
hpfs_readdir(ap)
	struct vop_readdir_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		struct ucred *a_cred;
		int *a_ncookies;
		u_int **cookies;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct hpfsnode *hp = VTOHP(vp);
	struct hpfsmount *hpmp = hp->h_hpmp;
	struct uio *uio = ap->a_uio;
	int ncookies = 0, i, num, cnum;
	int error = 0;
	off_t off;
	struct buf *bp;
	struct dirblk *dp;
	struct hpfsdirent *dep;
	lsn_t olsn;
	lsn_t lsn;
	int level;

	dprintf(("hpfs_readdir(0x%x, 0x%x, 0x%x): ",hp->h_no,(u_int32_t)uio->uio_offset,uio->uio_resid));

	off = uio->uio_offset;

	if( uio->uio_offset < sizeof(struct dirent) ) {
		dprintf((". faked, "));
		hpfs_de_dot.d_fileno = hp->h_no;
		error = uiomove((char *)&hpfs_de_dot,sizeof(struct dirent),uio);
		if(error) {
			return (error);
		}

		ncookies ++;
	}

	if( uio->uio_offset < 2 * sizeof(struct dirent) ) {
		dprintf((".. faked, "));
		hpfs_de_dotdot.d_fileno = hp->h_fn.fn_parent;

		error = uiomove((char *)&hpfs_de_dotdot, sizeof(struct dirent),
				uio);
		if(error) {
			return (error);
		}

		ncookies ++;
	}

	num = uio->uio_offset / sizeof(struct dirent) - 2;
	cnum = 0;

	lsn = ((alleaf_t *)hp->h_fn.fn_abd)->al_lsn;

	olsn = 0;
	level = 1;

dive:
	dprintf(("[dive 0x%x] ", lsn));
	error = bread(hp->h_devvp, lsn, D_BSIZE, NOCRED, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	dp = (struct dirblk *) bp->b_data;
	if (dp->d_magic != D_MAGIC) {
		printf("hpfs_readdir: MAGIC DOESN'T MATCH\n");
		brelse(bp);
		return (EINVAL);
	}

	dep = D_DIRENT(dp);

	if (olsn) {
		dprintf(("[restore 0x%x] ", olsn));

		while(!(dep->de_flag & DE_END) ) {
			if((dep->de_flag & DE_DOWN) &&
			   (olsn == DE_DOWNLSN(dep)))
					 break;
			dep = (hpfsdirent_t *)((caddr_t)dep + dep->de_reclen);
		}

		if((dep->de_flag & DE_DOWN) && (olsn == DE_DOWNLSN(dep))) {
			if (dep->de_flag & DE_END)
				goto blockdone;

			if (!(dep->de_flag & DE_SPECIAL)) {
				if (num <= cnum) {
					if (uio->uio_resid < sizeof(struct dirent)) {
						brelse(bp);
						dprintf(("[resid] "));
						goto readdone;
					}

					error = hpfs_de_uiomove(hpmp, dep, uio);
					if (error) {
						brelse (bp);
						return (error);
					}
					ncookies++;

					if (uio->uio_resid < sizeof(struct dirent)) {
						brelse(bp);
						dprintf(("[resid] "));
						goto readdone;
					}
				}
				cnum++;
			}

			dep = (hpfsdirent_t *)((caddr_t)dep + dep->de_reclen);
		} else {
			printf("hpfs_readdir: ERROR! oLSN not found\n");
			brelse(bp);
			return (EINVAL);
		}
	}

	olsn = 0;

	while(!(dep->de_flag & DE_END)) {
		if(dep->de_flag & DE_DOWN) {
			lsn = DE_DOWNLSN(dep);
			brelse(bp);
			level++;
			goto dive;
		}

		if (!(dep->de_flag & DE_SPECIAL)) {
			if (num <= cnum) {
				if (uio->uio_resid < sizeof(struct dirent)) {
					brelse(bp);
					dprintf(("[resid] "));
					goto readdone;
				}

				error = hpfs_de_uiomove(hpmp, dep, uio);
				if (error) {
					brelse (bp);
					return (error);
				}
				ncookies++;
				
				if (uio->uio_resid < sizeof(struct dirent)) {
					brelse(bp);
					dprintf(("[resid] "));
					goto readdone;
				}
			}
			cnum++;
		}

		dep = (hpfsdirent_t *)((caddr_t)dep + dep->de_reclen);
	}

	if(dep->de_flag & DE_DOWN) {
		dprintf(("[enddive] "));
		lsn = DE_DOWNLSN(dep);
		brelse(bp);
		level++;
		goto dive;
	}

blockdone:
	dprintf(("[EOB] "));
	olsn = lsn;
	lsn = dp->d_parent;
	brelse(bp);
	level--;

	dprintf(("[level %d] ", level));

	if (level > 0)
		goto dive;	/* undive really */

	if (ap->a_eofflag) {
	    dprintf(("[EOF] "));
	    *ap->a_eofflag = 1;
	}

readdone:
	dprintf(("[readdone]\n"));
	if (!error && ap->a_ncookies != NULL) {
		struct dirent* dpStart;
		struct dirent* dp;
#if defined(__FreeBSD__)
		u_long *cookies;
		u_long *cookiep;
#else /* defined(__NetBSD__) */
		off_t *cookies;
		off_t *cookiep;
#endif

		dprintf(("%d cookies, ",ncookies));
		if (uio->uio_segflg != UIO_SYSSPACE || uio->uio_iovcnt != 1)
			panic("hpfs_readdir: unexpected uio from NFS server");
		dpStart = (struct dirent *)
		     ((caddr_t)uio->uio_iov->iov_base -
			 (uio->uio_offset - off));
#if defined(__FreeBSD__)
		MALLOC(cookies, u_long *, ncookies * sizeof(u_long),
		       M_TEMP, M_WAITOK);
#else /* defined(__NetBSD__) */
		MALLOC(cookies, off_t *, ncookies * sizeof(off_t),
		       M_TEMP, M_WAITOK);
#endif
		for (dp = dpStart, cookiep = cookies, i=0;
		     i < ncookies;
		     dp = (struct dirent *)((caddr_t) dp + dp->d_reclen), i++) {
			off += dp->d_reclen;
			*cookiep++ = (u_int) off;
		}
		*ap->a_ncookies = ncookies;
		*ap->a_cookies = cookies;
	}

	return (0);
}

int
hpfs_lookup(ap)
	struct vop_lookup_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
	} */ *ap;
{
	register struct vnode *dvp = ap->a_dvp;
	register struct hpfsnode *dhp = VTOHP(dvp);
	struct hpfsmount *hpmp = dhp->h_hpmp;
	struct componentname *cnp = ap->a_cnp;
	struct ucred *cred = cnp->cn_cred;
	int error;
	int nameiop = cnp->cn_nameiop;
	int flags = cnp->cn_flags;
	int lockparent = flags & LOCKPARENT;
#if HPFS_DEBUG
	int wantparent = flags & (LOCKPARENT|WANTPARENT);
#endif
	dprintf(("hpfs_lookup(0x%x, %s, %ld, %d, %d): \n",
		dhp->h_no, cnp->cn_nameptr, cnp->cn_namelen,
		lockparent, wantparent));

	if (nameiop != CREATE && nameiop != DELETE && nameiop != LOOKUP) {
		printf("hpfs_lookup: LOOKUP, DELETE and CREATE are only supported\n");
		return (EOPNOTSUPP);
	}

	error = VOP_ACCESS(dvp, VEXEC, cred, cnp->cn_proc);
	if(error)
		return (error);

	if( (cnp->cn_namelen == 1) &&
	    !strncmp(cnp->cn_nameptr,".",1) ) {
		dprintf(("hpfs_lookup(0x%x,...): . faked\n",dhp->h_no));

		VREF(dvp);
		*ap->a_vpp = dvp;

		return (0);
	} else if( (cnp->cn_namelen == 2) &&
	    !strncmp(cnp->cn_nameptr,"..",2) && (flags & ISDOTDOT) ) {
		dprintf(("hpfs_lookup(0x%x,...): .. faked (0x%x)\n",
			dhp->h_no, dhp->h_fn.fn_parent));

		VOP__UNLOCK(dvp,0,cnp->cn_proc);

		error = VFS_VGET(hpmp->hpm_mp,
				 dhp->h_fn.fn_parent, ap->a_vpp); 
		if(error) {
			VOP__LOCK(dvp, 0, cnp->cn_proc);
			return(error);
		}

		if( lockparent && (flags & ISLASTCN) && 
		    (error = VOP__LOCK(dvp, 0, cnp->cn_proc)) ) {
			vput( *(ap->a_vpp) );
			return (error);
		}
		return (error);
	} else {
		struct buf *bp;
		struct hpfsdirent *dep;
		struct hpfsnode *hp;

		error = hpfs_genlookupbyname(dhp,
				cnp->cn_nameptr, cnp->cn_namelen, &bp, &dep);
		if (error) {
			if ((error == ENOENT) && (flags & ISLASTCN) &&
			    (nameiop == CREATE || nameiop == RENAME)) {
				if(!lockparent)
					VOP__UNLOCK(dvp, 0, cnp->cn_proc);
				cnp->cn_flags |= SAVENAME;
				return (EJUSTRETURN);
			}

			return (error);
		}

		dprintf(("hpfs_lookup: fnode: 0x%x, CPID: 0x%x\n",
			 dep->de_fnode, dep->de_cpid));

		if (nameiop == DELETE && (flags & ISLASTCN)) {
			error = VOP_ACCESS(dvp, VWRITE, cred, cnp->cn_proc);
			if (error) {
				brelse(bp);
				return (error);
			}
		}

		if (dhp->h_no == dep->de_fnode) {
			brelse(bp);
			VREF(dvp);
			*ap->a_vpp = dvp;
			return (0);
		}

		error = VFS_VGET(hpmp->hpm_mp, dep->de_fnode, ap->a_vpp);
		if (error) {
			printf("hpfs_lookup: VFS_VGET FAILED %d\n", error);
			brelse(bp);
			return(error);
		}

		hp = VTOHP(*ap->a_vpp);

		hp->h_mtime = dep->de_mtime;
		hp->h_ctime = dep->de_ctime;
		hp->h_atime = dep->de_atime;
		bcopy(dep->de_name, hp->h_name, dep->de_namelen);
		hp->h_name[dep->de_namelen] = '\0';
		hp->h_namelen = dep->de_namelen;
		hp->h_flag |= H_PARVALID;

		brelse(bp);

		if(!lockparent || !(flags & ISLASTCN))
			VOP__UNLOCK(dvp, 0, cnp->cn_proc);
		if ((flags & MAKEENTRY) &&
		    (!(flags & ISLASTCN) || 
		     (nameiop != DELETE && nameiop != CREATE)))
			cache_enter(dvp, *ap->a_vpp, cnp);
	}
	return (error);
}

int
hpfs_remove(ap)
	struct vop_remove_args /* {
		struct vnode *a_dvp;
		struct vnode *a_vp;
		struct componentname *a_cnp;
	} */ *ap;
{
	int error;

	dprintf(("hpfs_remove(0x%x, %s, %ld): \n", VTOHP(ap->a_vp)->h_no,
		ap->a_cnp->cn_nameptr, ap->a_cnp->cn_namelen));

	if (ap->a_vp->v_type == VDIR)
		return (EPERM);

	error = hpfs_removefnode (ap->a_dvp, ap->a_vp, ap->a_cnp);
	return (error);
}

int
hpfs_create(ap)
	struct vop_create_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vattr *a_vap;
	} */ *ap;
{
	int error;

	dprintf(("hpfs_create(0x%x, %s, %ld): \n", VTOHP(ap->a_dvp)->h_no,
		ap->a_cnp->cn_nameptr, ap->a_cnp->cn_namelen));

	if (!(ap->a_cnp->cn_flags & HASBUF)) 
		panic ("hpfs_create: no name\n");

	error = hpfs_makefnode (ap->a_dvp, ap->a_vpp, ap->a_cnp, ap->a_vap);

	return (error);
}

/*
 * Return POSIX pathconf information applicable to NTFS filesystem
 */
int
hpfs_pathconf(ap)
	struct vop_pathconf_args /* {
		struct vnode *a_vp;
		int a_name;
		register_t *a_retval;
	} */ *ap;
{
	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = 1;
		return (0);
	case _PC_NAME_MAX:
		*ap->a_retval = HPFS_MAXFILENAME;
		return (0);
	case _PC_PATH_MAX:
		*ap->a_retval = PATH_MAX;
		return (0);
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		return (0);
	case _PC_NO_TRUNC:
		*ap->a_retval = 0;
		return (0);
#if defined(__NetBSD__)
	case _PC_SYNC_IO:
		*ap->a_retval = 1;
		return (0);
	case _PC_FILESIZEBITS:
		*ap->a_retval = 32;
		return (0);
#endif
	default:
		return (EINVAL);
	}
	/* NOTREACHED */
}


/*
 * Global vfs data structures
 */
vop_t **hpfs_vnodeop_p;
#if defined(__FreeBSD__)
struct vnodeopv_entry_desc hpfs_vnodeop_entries[] = {
	{ &vop_default_desc, (vop_t *)vop_defaultop },

	{ &vop_getattr_desc, (vop_t *)hpfs_getattr },
	{ &vop_setattr_desc, (vop_t *)hpfs_setattr },
	{ &vop_inactive_desc, (vop_t *)hpfs_inactive },
	{ &vop_reclaim_desc, (vop_t *)hpfs_reclaim },
	{ &vop_print_desc, (vop_t *)hpfs_print },
	{ &vop_create_desc, (vop_t *)hpfs_create },
	{ &vop_remove_desc, (vop_t *)hpfs_remove },
	{ &vop_islocked_desc, (vop_t *)vop_stdislocked },
	{ &vop_unlock_desc, (vop_t *)vop_stdunlock },
	{ &vop_lock_desc, (vop_t *)vop_stdlock },
	{ &vop_cachedlookup_desc, (vop_t *)hpfs_lookup },
	{ &vop_lookup_desc, (vop_t *)vfs_cache_lookup },
	{ &vop_access_desc, (vop_t *)hpfs_access },
	{ &vop_close_desc, (vop_t *)hpfs_close },
	{ &vop_open_desc, (vop_t *)hpfs_open },
	{ &vop_readdir_desc, (vop_t *)hpfs_readdir },
	{ &vop_fsync_desc, (vop_t *)hpfs_fsync },
	{ &vop_bmap_desc, (vop_t *)hpfs_bmap },
	{ &vop_getpages_desc, (vop_t *) hpfs_getpages },
	{ &vop_putpages_desc, (vop_t *) hpfs_putpages },
	{ &vop_strategy_desc, (vop_t *)hpfs_strategy },
	{ &vop_bwrite_desc, (vop_t *)vop_stdbwrite },
	{ &vop_read_desc, (vop_t *)hpfs_read },
	{ &vop_write_desc, (vop_t *)hpfs_write },
	{ &vop_ioctl_desc, (vop_t *)hpfs_ioctl },
	{ &vop_pathconf_desc, (vop_t *)hpfs_pathconf },
	{ NULL, NULL }
};

static
struct vnodeopv_desc hpfs_vnodeop_opv_desc =
	{ &hpfs_vnodeop_p, hpfs_vnodeop_entries };

VNODEOP_SET(hpfs_vnodeop_opv_desc);
#else /* defined(__NetBSD__) */
struct vnodeopv_entry_desc ntfs_vnodeop_entries[] = {
	{ &vop_default_desc, (vop_t *) genfs_badop },	/* XXX */
	{ &vop_lookup_desc, (vop_t *) hpfs_lookup },	/* lookup */
	{ &vop_create_desc, genfs_eopnotsupp },		/* create */
	{ &vop_mknod_desc, genfs_eopnotsupp },		/* mknod */
	{ &vop_open_desc, (vop_t *) hpfs_open },	/* open */
	{ &vop_close_desc,(vop_t *) hpfs_close },	/* close */
	{ &vop_access_desc, (vop_t *) hpfs_access },	/* access */
	{ &vop_getattr_desc, (vop_t *) hpfs_getattr },	/* getattr */
	{ &vop_setattr_desc, genfs_eopnotsupp },	/* setattr */
	{ &vop_read_desc, (vop_t *) hpfs_read },	/* read */
	{ &vop_write_desc, (vop_t *) hpfs_write },	/* write */
	{ &vop_lease_desc, genfs_lease_check },		/* lease */
	{ &vop_fcntl_desc, genfs_fcntl },		/* fcntl */
	{ &vop_ioctl_desc, genfs_enoioctl },		/* ioctl */
	{ &vop_poll_desc, genfs_poll },			/* poll */
	{ &vop_revoke_desc, genfs_revoke },		/* revoke */
	{ &vop_mmap_desc, genfs_eopnotsupp },		/* mmap */
	{ &vop_fsync_desc, genfs_fsync },		/* fsync */
	{ &vop_seek_desc, genfs_seek },			/* seek */
	{ &vop_remove_desc, genfs_eopnotsupp },		/* remove */
	{ &vop_link_desc, genfs_eopnotsupp },		/* link */
	{ &vop_rename_desc, genfs_eopnotsupp },		/* rename */
	{ &vop_mkdir_desc, genfs_eopnotsupp },		/* mkdir */
	{ &vop_rmdir_desc, genfs_eopnotsupp },		/* rmdir */
	{ &vop_symlink_desc, genfs_eopnotsupp },	/* symlink */
	{ &vop_readdir_desc, (vop_t *) hpfs_readdir },	/* readdir */
	{ &vop_readlink_desc, genfs_eopnotsupp },	/* readlink */
	{ &vop_abortop_desc, genfs_abortop },		/* abortop */
	{ &vop_inactive_desc, (vop_t *) hpfs_inactive },	/* inactive */
	{ &vop_reclaim_desc, (vop_t *) hpfs_reclaim },	/* reclaim */
	{ &vop_lock_desc, genfs_lock },			/* lock */
	{ &vop_unlock_desc, genfs_unlock },		/* unlock */
	{ &vop_bmap_desc, (vop_t *) hpfs_bmap },	/* bmap */
	{ &vop_strategy_desc, (vop_t *) hpfs_strategy },	/* strategy */
	{ &vop_print_desc, (vop_t *) hpfs_print },	/* print */
	{ &vop_islocked_desc, genfs_islocked },		/* islocked */
	{ &vop_pathconf_desc, hpfs_pathconf },		/* pathconf */
	{ &vop_advlock_desc, genfs_nullop },		/* advlock */
	{ &vop_blkatoff_desc, genfs_eopnotsupp },	/* blkatoff */
	{ &vop_valloc_desc, genfs_eopnotsupp },		/* valloc */
	{ &vop_reallocblks_desc, genfs_eopnotsupp },	/* reallocblks */
	{ &vop_vfree_desc, genfs_eopnotsupp },		/* vfree */
	{ &vop_truncate_desc, genfs_eopnotsupp },	/* truncate */
	{ &vop_update_desc, genfs_eopnotsupp },		/* update */
	{ &vop_bwrite_desc, vn_bwrite },		/* bwrite */
	{ (struct vnodeop_desc *)NULL, (int (*) __P((void *)))NULL }
};
struct vnodeopv_desc ntfs_vnodeop_opv_desc =
	{ &ntfs_vnodeop_p, ntfs_vnodeop_entries };

#endif
