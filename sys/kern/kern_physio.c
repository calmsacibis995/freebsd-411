/*
 * Copyright (c) 1994 John S. Dyson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Absolutely no warranty of function or purpose is made by the author
 *    John S. Dyson.
 * 4. Modifications may be freely made to this file if the above conditions
 *    are met.
 *
 * $FreeBSD: src/sys/kern/kern_physio.c,v 1.46.2.4 2003/11/14 09:51:47 simokawa Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/uio.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

static void
physwakeup(struct buf *bp)
{
	wakeup((caddr_t) bp);
}

int
physio(dev_t dev, struct uio *uio, int ioflag)
{
	int i;
	int error;
	int spl;
	int chk_blockno;
	caddr_t sa;
	off_t blockno;
	u_int iolen;
	struct buf *bp;

	/* Keep the process UPAGES from being swapped. XXX: why ? */
	PHOLD(curproc);

	bp = getpbuf(NULL);
	sa = bp->b_data;
	error = 0;

	/* XXX: sanity check */
	if(dev->si_iosize_max < PAGE_SIZE) {
		printf("WARNING: %s si_iosize_max=%d, using DFLTPHYS.\n",
		    devtoname(dev), dev->si_iosize_max);
		dev->si_iosize_max = DFLTPHYS;
	}

	/* Don't check block number overflow for D_MEM */
	if ((devsw(dev)->d_flags & D_TYPEMASK) == D_MEM)
		chk_blockno = 0;
	else
		chk_blockno = 1;

	for (i = 0; i < uio->uio_iovcnt; i++) {
		while (uio->uio_iov[i].iov_len) {
			if (uio->uio_rw == UIO_READ)
				bp->b_flags = B_PHYS | B_CALL | B_READ;
			else 
				bp->b_flags = B_PHYS | B_CALL | B_WRITE;
			bp->b_dev = dev;
			bp->b_iodone = physwakeup;
			bp->b_data = uio->uio_iov[i].iov_base;
			bp->b_bcount = uio->uio_iov[i].iov_len;
			bp->b_offset = uio->uio_offset;
			bp->b_saveaddr = sa;

			/* Don't exceed drivers iosize limit */
			if (bp->b_bcount > dev->si_iosize_max)
				bp->b_bcount = dev->si_iosize_max;

			/* 
			 * Make sure the pbuf can map the request
			 * XXX: The pbuf has kvasize = MAXPHYS so a request
			 * XXX: larger than MAXPHYS - PAGE_SIZE must be
			 * XXX: page aligned or it will be fragmented.
			 */
			iolen = ((vm_offset_t) bp->b_data) & PAGE_MASK;
			if ((bp->b_bcount + iolen) > bp->b_kvasize) {
				bp->b_bcount = bp->b_kvasize;
				if (iolen != 0)
					bp->b_bcount -= PAGE_SIZE;
			}
			bp->b_bufsize = bp->b_bcount;

			blockno = bp->b_offset >> DEV_BSHIFT;
			if (chk_blockno && (daddr_t)blockno != blockno) {
				error = EINVAL; /* blockno overflow */
				goto doerror;
			}
			bp->b_blkno = blockno;

			if (uio->uio_segflg == UIO_USERSPACE)
				if (vmapbuf(bp) < 0) {
					error = EFAULT;
					goto doerror;
				}

			BUF_STRATEGY(bp, 0);
			spl = splbio();
			while ((bp->b_flags & B_DONE) == 0)
				tsleep((caddr_t)bp, PRIBIO, "physstr", 0);
			splx(spl);

			if (uio->uio_segflg == UIO_USERSPACE)
				vunmapbuf(bp);
			iolen = bp->b_bcount - bp->b_resid;
			if (iolen == 0 && !(bp->b_flags & B_ERROR))
				goto doerror;	/* EOF */
			uio->uio_iov[i].iov_len -= iolen;
			uio->uio_iov[i].iov_base += iolen;
			uio->uio_resid -= iolen;
			uio->uio_offset += iolen;
			if( bp->b_flags & B_ERROR) {
				error = bp->b_error;
				goto doerror;
			}
		}
	}
doerror:
	relpbuf(bp, NULL);
	PRELE(curproc);
	return (error);
}
