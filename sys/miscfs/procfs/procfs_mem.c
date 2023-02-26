/*
 * Copyright (c) 1993 Jan-Simon Pendry
 * Copyright (c) 1993 Sean Eric Fagan
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry and Sean Eric Fagan.
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
 *	@(#)procfs_mem.c	8.5 (Berkeley) 6/15/94
 *
 * $FreeBSD: src/sys/miscfs/procfs/procfs_mem.c,v 1.46.2.4 2003/08/12 18:36:28 tegge Exp $
 */

/*
 * This is a lightly hacked and merged version
 * of sef's pread/pwrite functions
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <miscfs/procfs/procfs.h>
#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_map.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <sys/user.h>
#include <sys/ptrace.h>

static int	procfs_rwmem __P((struct proc *curp,
				  struct proc *p, struct uio *uio));

static int
procfs_rwmem(curp, p, uio)
	struct proc *curp;
	struct proc *p;
	struct uio *uio;
{
	int error;
	int writing;
	struct vmspace *vm;
	vm_map_t map;
	vm_object_t object = NULL;
	vm_offset_t pageno = 0;		/* page number */
	vm_prot_t reqprot;
	vm_offset_t kva;

	/*
	 * if the vmspace is in the midst of being deallocated or the
	 * process is exiting, don't try to grab anything.  The page table
	 * usage in that process can be messed up.
	 */
	vm = p->p_vmspace;
	if ((p->p_flag & P_WEXIT) || (vm->vm_refcnt < 1))
		return EFAULT;
	++vm->vm_refcnt;
	/*
	 * The map we want...
	 */
	map = &vm->vm_map;

	writing = uio->uio_rw == UIO_WRITE;
	reqprot = writing ? (VM_PROT_WRITE | VM_PROT_OVERRIDE_WRITE) : VM_PROT_READ;

	kva = kmem_alloc_nofault(kernel_map, PAGE_SIZE);

	/*
	 * Only map in one page at a time.  We don't have to, but it
	 * makes things easier.  This way is trivial - right?
	 */
	do {
		vm_map_t tmap;
		vm_offset_t uva;
		int page_offset;		/* offset into page */
		vm_map_entry_t out_entry;
		vm_prot_t out_prot;
		boolean_t wired;
		vm_pindex_t pindex;
		u_int len;
		vm_page_t m;

		object = NULL;

		uva = (vm_offset_t) uio->uio_offset;

		/*
		 * Get the page number of this segment.
		 */
		pageno = trunc_page(uva);
		page_offset = uva - pageno;

		/*
		 * How many bytes to copy
		 */
		len = min(PAGE_SIZE - page_offset, uio->uio_resid);

		/*
		 * Fault the page on behalf of the process
		 */
		error = vm_fault(map, pageno, reqprot, VM_FAULT_NORMAL);
		if (error) {
			error = EFAULT;
			break;
		}

		/*
		 * Now we need to get the page.  out_entry, out_prot, wired,
		 * and single_use aren't used.  One would think the vm code
		 * would be a *bit* nicer...  We use tmap because
		 * vm_map_lookup() can change the map argument.
		 */
		tmap = map;
		error = vm_map_lookup(&tmap, pageno, reqprot,
			      &out_entry, &object, &pindex, &out_prot,
			      &wired);
		if (error) {
			error = EFAULT;
			break;
		}

		m = vm_page_lookup(object, pindex);

		/* Allow fallback to backing objects if we are reading */

		while (m == NULL && !writing && object->backing_object) {

		  pindex += OFF_TO_IDX(object->backing_object_offset);
		  object = object->backing_object;

		  m = vm_page_lookup(object, pindex);
		}

		if (m == NULL) {
			vm_map_lookup_done(tmap, out_entry);
			error = EFAULT;
			break;
		}

		/*
		 * Hold the page in memory.
		 */
		vm_page_hold(m);

		/*
		 * We're done with tmap now.
		 */
		vm_map_lookup_done(tmap, out_entry);

		pmap_kenter(kva, VM_PAGE_TO_PHYS(m));

		/*
		 * Now do the i/o move.
		 */
		error = uiomove((caddr_t)(kva + page_offset), len, uio);

		pmap_kremove(kva);

		/*
		 * Release the page.
		 */
		vm_page_unhold(m);

	} while (error == 0 && uio->uio_resid > 0);

	kmem_free(kernel_map, kva, PAGE_SIZE);
	vmspace_free(vm);
	return (error);
}

/*
 * Copy data in and out of the target process.
 * We do this by mapping the process's page into
 * the kernel and then doing a uiomove direct
 * from the kernel address space.
 */
int
procfs_domem(curp, p, pfs, uio)
	struct proc *curp;
	struct proc *p;
	struct pfsnode *pfs;
	struct uio *uio;
{

	if (uio->uio_resid == 0)
		return (0);

	/* Can't trace a process that's currently exec'ing. */ 
	if ((p->p_flag & P_INEXEC) != 0)
		return EAGAIN;
 	if (!CHECKIO(curp, p) || p_trespass(curp, p))
 		return EPERM;

	return (procfs_rwmem(curp, p, uio));
}

/*
 * Given process (p), find the vnode from which
 * its text segment is being executed.
 *
 * It would be nice to grab this information from
 * the VM system, however, there is no sure-fire
 * way of doing that.  Instead, fork(), exec() and
 * wait() all maintain the p_textvp field in the
 * process proc structure which contains a held
 * reference to the exec'ed vnode.
 *
 * XXX - Currently, this is not not used, as the
 * /proc/pid/file object exposes an information leak
 * that shouldn't happen.  Using a mount option would
 * make it configurable on a per-system (or, at least,
 * per-mount) basis; however, that's not really best.
 * The best way to do it, I think, would be as an
 * ioctl; this would restrict it to the uid running
 * program, or root, which seems a reasonable compromise.
 * However, the number of applications for this is
 * minimal, if it can't be seen in the filesytem space,
 * and doint it as an ioctl makes it somewhat less
 * useful due to the, well, inelegance.
 *
 */
struct vnode *
procfs_findtextvp(p)
	struct proc *p;
{

	return (p->p_textvp);
}
