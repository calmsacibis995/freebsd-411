/*
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
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
 *	from: @(#)vm_pager.c	8.6 (Berkeley) 1/12/94
 *
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Avadis Tevanian, Jr., Michael Wayne Young
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 *
 * $FreeBSD: src/sys/vm/vm_pager.c,v 1.54.2.2 2001/11/18 07:11:00 dillon Exp $
 */

/*
 *	Paging space routine stubs.  Emulates a matchmaker-like interface
 *	for builtin pagers.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/ucred.h>
#include <sys/malloc.h>
#include <sys/proc.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>

MALLOC_DEFINE(M_VMPGDATA, "VM pgdata", "XXX: VM pager private data");

extern struct pagerops defaultpagerops;
extern struct pagerops swappagerops;
extern struct pagerops vnodepagerops;
extern struct pagerops devicepagerops;
extern struct pagerops physpagerops;

int cluster_pbuf_freecnt = -1;	/* unlimited to begin with */

static int dead_pager_getpages __P((vm_object_t, vm_page_t *, int, int));
static vm_object_t dead_pager_alloc __P((void *, vm_ooffset_t, vm_prot_t,
	vm_ooffset_t));
static void dead_pager_putpages __P((vm_object_t, vm_page_t *, int, int, int *));
static boolean_t dead_pager_haspage __P((vm_object_t, vm_pindex_t, int *, int *));
static void dead_pager_dealloc __P((vm_object_t));

static int
dead_pager_getpages(obj, ma, count, req)
	vm_object_t obj;
	vm_page_t *ma;
	int count;
	int req;
{
	return VM_PAGER_FAIL;
}

static vm_object_t
dead_pager_alloc(handle, size, prot, off)
	void *handle;
	vm_ooffset_t size;
	vm_prot_t prot;
	vm_ooffset_t off;
{
	return NULL;
}

static void
dead_pager_putpages(object, m, count, flags, rtvals)
	vm_object_t object;
	vm_page_t *m;
	int count;
	int flags;
	int *rtvals;
{
	int i;

	for (i = 0; i < count; i++) {
		rtvals[i] = VM_PAGER_AGAIN;
	}
}

static int
dead_pager_haspage(object, pindex, prev, next)
	vm_object_t object;
	vm_pindex_t pindex;
	int *prev;
	int *next;
{
	if (prev)
		*prev = 0;
	if (next)
		*next = 0;
	return FALSE;
}

static void
dead_pager_dealloc(object)
	vm_object_t object;
{
	return;
}

static struct pagerops deadpagerops = {
	NULL,
	dead_pager_alloc,
	dead_pager_dealloc,
	dead_pager_getpages,
	dead_pager_putpages,
	dead_pager_haspage,
	NULL
};

struct pagerops *pagertab[] = {
	&defaultpagerops,	/* OBJT_DEFAULT */
	&swappagerops,		/* OBJT_SWAP */
	&vnodepagerops,		/* OBJT_VNODE */
	&devicepagerops,	/* OBJT_DEVICE */
	&physpagerops,		/* OBJT_PHYS */
	&deadpagerops		/* OBJT_DEAD */
};

int npagers = sizeof(pagertab) / sizeof(pagertab[0]);

/*
 * Kernel address space for mapping pages.
 * Used by pagers where KVAs are needed for IO.
 *
 * XXX needs to be large enough to support the number of pending async
 * cleaning requests (NPENDINGIO == 64) * the maximum swap cluster size
 * (MAXPHYS == 64k) if you want to get the most efficiency.
 */
#define PAGER_MAP_SIZE	(8 * 1024 * 1024)

int pager_map_size = PAGER_MAP_SIZE;
vm_map_t pager_map;
static int bswneeded;
static vm_offset_t swapbkva;		/* swap buffers kva */

void
vm_pager_init()
{
	struct pagerops **pgops;

	/*
	 * Initialize known pagers
	 */
	for (pgops = pagertab; pgops < &pagertab[npagers]; pgops++)
		if (pgops && ((*pgops)->pgo_init != NULL))
			(*(*pgops)->pgo_init) ();
}

void
vm_pager_bufferinit()
{
	struct buf *bp;
	int i;

	bp = swbuf;
	/*
	 * Now set up swap and physical I/O buffer headers.
	 */
	for (i = 0; i < nswbuf; i++, bp++) {
		TAILQ_INSERT_HEAD(&bswlist, bp, b_freelist);
		BUF_LOCKINIT(bp);
		LIST_INIT(&bp->b_dep);
		bp->b_rcred = bp->b_wcred = NOCRED;
		bp->b_xflags = 0;
	}

	cluster_pbuf_freecnt = nswbuf / 2;

	swapbkva = kmem_alloc_pageable(pager_map, nswbuf * MAXPHYS);
	if (!swapbkva)
		panic("Not enough pager_map VM space for physical buffers");
}

/*
 * Allocate an instance of a pager of the given type.
 * Size, protection and offset parameters are passed in for pagers that
 * need to perform page-level validation (e.g. the device pager).
 */
vm_object_t
vm_pager_allocate(objtype_t type, void *handle, vm_ooffset_t size, vm_prot_t prot,
		  vm_ooffset_t off)
{
	struct pagerops *ops;

	ops = pagertab[type];
	if (ops)
		return ((*ops->pgo_alloc) (handle, size, prot, off));
	return (NULL);
}

void
vm_pager_deallocate(object)
	vm_object_t object;
{
	(*pagertab[object->type]->pgo_dealloc) (object);
}

/*
 *      vm_pager_strategy:
 *
 *      called with no specific spl
 *      Execute strategy routine directly to pager.
 */

void
vm_pager_strategy(vm_object_t object, struct buf *bp)
{
	if (pagertab[object->type]->pgo_strategy) {
	    (*pagertab[object->type]->pgo_strategy)(object, bp);
	} else {
		bp->b_flags |= B_ERROR;
		bp->b_error = ENXIO;
		biodone(bp);
	}
}

/*
 * vm_pager_get_pages() - inline, see vm/vm_pager.h
 * vm_pager_put_pages() - inline, see vm/vm_pager.h
 * vm_pager_has_page() - inline, see vm/vm_pager.h
 * vm_pager_page_inserted() - inline, see vm/vm_pager.h
 * vm_pager_page_removed() - inline, see vm/vm_pager.h
 */

#if 0
/*
 *	vm_pager_sync:
 *
 *	Called by pageout daemon before going back to sleep.
 *	Gives pagers a chance to clean up any completed async pageing 
 *	operations.
 */
void
vm_pager_sync()
{
	struct pagerops **pgops;

	for (pgops = pagertab; pgops < &pagertab[npagers]; pgops++)
		if (pgops && ((*pgops)->pgo_sync != NULL))
			(*(*pgops)->pgo_sync) ();
}

#endif

vm_offset_t
vm_pager_map_page(m)
	vm_page_t m;
{
	vm_offset_t kva;

	kva = kmem_alloc_wait(pager_map, PAGE_SIZE);
	pmap_kenter(kva, VM_PAGE_TO_PHYS(m));
	return (kva);
}

void
vm_pager_unmap_page(kva)
	vm_offset_t kva;
{
	pmap_kremove(kva);
	kmem_free_wakeup(pager_map, kva, PAGE_SIZE);
}

vm_object_t
vm_pager_object_lookup(pg_list, handle)
	register struct pagerlst *pg_list;
	void *handle;
{
	register vm_object_t object;

	for (object = TAILQ_FIRST(pg_list); object != NULL; object = TAILQ_NEXT(object,pager_object_list))
		if (object->handle == handle)
			return (object);
	return (NULL);
}

/*
 * initialize a physical buffer
 */

static void
initpbuf(struct buf *bp)
{
	bp->b_rcred = NOCRED;
	bp->b_wcred = NOCRED;
	bp->b_qindex = QUEUE_NONE;
	bp->b_data = (caddr_t) (MAXPHYS * (bp - swbuf)) + swapbkva;
	bp->b_kvabase = bp->b_data;
	bp->b_kvasize = MAXPHYS;
	bp->b_xflags = 0;
	bp->b_flags = 0;
	bp->b_error = 0;
	BUF_LOCK(bp, LK_EXCLUSIVE);
}

/*
 * allocate a physical buffer
 *
 *	There are a limited number (nswbuf) of physical buffers.  We need
 *	to make sure that no single subsystem is able to hog all of them,
 *	so each subsystem implements a counter which is typically initialized
 *	to 1/2 nswbuf.  getpbuf() decrements this counter in allocation and
 *	increments it on release, and blocks if the counter hits zero.  A
 *	subsystem may initialize the counter to -1 to disable the feature,
 *	but it must still be sure to match up all uses of getpbuf() with 
 *	relpbuf() using the same variable.
 *
 *	NOTE: pfreecnt can be NULL, but this 'feature' will be removed
 *	relatively soon when the rest of the subsystems get smart about it. XXX
 */
struct buf *
getpbuf(pfreecnt)
	int *pfreecnt;
{
	int s;
	struct buf *bp;

	s = splvm();

	for (;;) {
		if (pfreecnt) {
			while (*pfreecnt == 0) {
				tsleep(pfreecnt, PVM, "wswbuf0", 0);
			}
		}

		/* get a bp from the swap buffer header pool */
		if ((bp = TAILQ_FIRST(&bswlist)) != NULL)
			break;

		bswneeded = 1;
		tsleep(&bswneeded, PVM, "wswbuf1", 0);
		/* loop in case someone else grabbed one */
	}
	TAILQ_REMOVE(&bswlist, bp, b_freelist);
	if (pfreecnt)
		--*pfreecnt;
	splx(s);

	initpbuf(bp);
	return bp;
}

/*
 * allocate a physical buffer, if one is available.
 *
 *	Note that there is no NULL hack here - all subsystems using this
 *	call understand how to use pfreecnt.
 */
struct buf *
trypbuf(pfreecnt)
	int *pfreecnt;
{
	int s;
	struct buf *bp;

	s = splvm();
	if (*pfreecnt == 0 || (bp = TAILQ_FIRST(&bswlist)) == NULL) {
		splx(s);
		return NULL;
	}
	TAILQ_REMOVE(&bswlist, bp, b_freelist);

	--*pfreecnt;

	splx(s);

	initpbuf(bp);

	return bp;
}

/*
 * release a physical buffer
 *
 *	NOTE: pfreecnt can be NULL, but this 'feature' will be removed
 *	relatively soon when the rest of the subsystems get smart about it. XXX
 */
void
relpbuf(bp, pfreecnt)
	struct buf *bp;
	int *pfreecnt;
{
	int s;

	s = splvm();

	if (bp->b_rcred != NOCRED) {
		crfree(bp->b_rcred);
		bp->b_rcred = NOCRED;
	}
	if (bp->b_wcred != NOCRED) {
		crfree(bp->b_wcred);
		bp->b_wcred = NOCRED;
	}

	if (bp->b_vp)
		pbrelvp(bp);

	BUF_UNLOCK(bp);

	TAILQ_INSERT_HEAD(&bswlist, bp, b_freelist);

	if (bswneeded) {
		bswneeded = 0;
		wakeup(&bswneeded);
	}
	if (pfreecnt) {
		if (++*pfreecnt == 1)
			wakeup(pfreecnt);
	}
	splx(s);
}

/********************************************************
 *		CHAINING FUNCTIONS			*
 ********************************************************
 *
 *	These functions support recursion of I/O operations
 *	on bp's, typically by chaining one or more 'child' bp's
 *	to the parent.  Synchronous, asynchronous, and semi-synchronous
 *	chaining is possible.
 */

/*
 *	vm_pager_chain_iodone:
 *
 *	io completion routine for child bp.  Currently we fudge a bit
 *	on dealing with b_resid.   Since users of these routines may issue
 *	multiple children simultaniously, sequencing of the error can be lost.
 */

static void
vm_pager_chain_iodone(struct buf *nbp)
{
	struct buf *bp;

	if ((bp = nbp->b_chain.parent) != NULL) {
		if (nbp->b_flags & B_ERROR) {
			bp->b_flags |= B_ERROR;
			bp->b_error = nbp->b_error;
		} else if (nbp->b_resid != 0) {
			bp->b_flags |= B_ERROR;
			bp->b_error = EINVAL;
		} else {
			bp->b_resid -= nbp->b_bcount;
		}
		nbp->b_chain.parent = NULL;
		--bp->b_chain.count;
		if (bp->b_flags & B_WANT) {
			bp->b_flags &= ~B_WANT;
			wakeup(bp);
		}
		if (!bp->b_chain.count && (bp->b_xflags & BX_AUTOCHAINDONE)) {
			bp->b_xflags &= ~BX_AUTOCHAINDONE;
			if (bp->b_resid != 0 && !(bp->b_flags & B_ERROR)) {
				bp->b_flags |= B_ERROR;
				bp->b_error = EINVAL;
			}
			biodone(bp);
		}
	}
	nbp->b_flags |= B_DONE;
	nbp->b_flags &= ~B_ASYNC;
	relpbuf(nbp, NULL);
}

/*
 *	getchainbuf:
 *
 *	Obtain a physical buffer and chain it to its parent buffer.  When
 *	I/O completes, the parent buffer will be B_SIGNAL'd.  Errors are
 *	automatically propogated to the parent
 *
 *	Since these are brand new buffers, we do not have to clear B_INVAL
 *	and B_ERROR because they are already clear.
 */

struct buf *
getchainbuf(struct buf *bp, struct vnode *vp, int flags)
{
	struct buf *nbp = getpbuf(NULL);

	nbp->b_chain.parent = bp;
	++bp->b_chain.count;

	if (bp->b_chain.count > 4)
		waitchainbuf(bp, 4, 0);

	nbp->b_flags = B_CALL | (bp->b_flags & B_ORDERED) | flags;
	nbp->b_rcred = nbp->b_wcred = proc0.p_ucred;
	nbp->b_iodone = vm_pager_chain_iodone;

	crhold(nbp->b_rcred);
	crhold(nbp->b_wcred);

	if (vp)
		pbgetvp(vp, nbp);
	return(nbp);
}

void
flushchainbuf(struct buf *nbp)
{
	if (nbp->b_bcount) {
		nbp->b_bufsize = nbp->b_bcount;
		if ((nbp->b_flags & B_READ) == 0)
			nbp->b_dirtyend = nbp->b_bcount;
		BUF_KERNPROC(nbp);
		VOP_STRATEGY(nbp->b_vp, nbp);
	} else {
		biodone(nbp);
	}
}

void
waitchainbuf(struct buf *bp, int count, int done)
{
 	int s;

	s = splbio();
	while (bp->b_chain.count > count) {
		bp->b_flags |= B_WANT;
		tsleep(bp, PRIBIO + 4, "bpchain", 0);
	}
	if (done) {
		if (bp->b_resid != 0 && !(bp->b_flags & B_ERROR)) {
			bp->b_flags |= B_ERROR;
			bp->b_error = EINVAL;
		}
		biodone(bp);
	}
	splx(s);
}

void
autochaindone(struct buf *bp)
{
 	int s;

	s = splbio();
	if (bp->b_chain.count == 0)
		biodone(bp);
	else
		bp->b_xflags |= BX_AUTOCHAINDONE;
	splx(s);
}

