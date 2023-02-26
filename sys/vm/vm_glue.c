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
 *	from: @(#)vm_glue.c	8.6 (Berkeley) 1/5/94
 *
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
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
 * $FreeBSD: src/sys/vm/vm_glue.c,v 1.94.2.4 2003/01/13 22:51:17 dillon Exp $
 */

#include "opt_vm.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/buf.h>
#include <sys/shm.h>
#include <sys/vmmeter.h>
#include <sys/sysctl.h>

#include <sys/kernel.h>
#include <sys/unistd.h>

#include <machine/limits.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>

#include <sys/user.h>

/*
 * System initialization
 *
 * Note: proc0 from proc.h
 */

static void vm_init_limits __P((void *));
SYSINIT(vm_limits, SI_SUB_VM_CONF, SI_ORDER_FIRST, vm_init_limits, &proc0)

/*
 * THIS MUST BE THE LAST INITIALIZATION ITEM!!!
 *
 * Note: run scheduling should be divorced from the vm system.
 */
static void scheduler __P((void *));
SYSINIT(scheduler, SI_SUB_RUN_SCHEDULER, SI_ORDER_FIRST, scheduler, NULL)


static void swapout __P((struct proc *));

int
kernacc(addr, len, rw)
	caddr_t addr;
	int len, rw;
{
	boolean_t rv;
	vm_offset_t saddr, eaddr;
	vm_prot_t prot;

	KASSERT((rw & (~VM_PROT_ALL)) == 0,
	    ("illegal ``rw'' argument to kernacc (%x)\n", rw));
	prot = rw;
	saddr = trunc_page((vm_offset_t)addr);
	eaddr = round_page((vm_offset_t)addr + len);
	vm_map_lock_read(kernel_map);
	rv = vm_map_check_protection(kernel_map, saddr, eaddr, prot);
	vm_map_unlock_read(kernel_map);
	return (rv == TRUE);
}

int
useracc(addr, len, rw)
	caddr_t addr;
	int len, rw;
{
	boolean_t rv;
	vm_prot_t prot;
	vm_map_t map;
	vm_map_entry_t save_hint;

	KASSERT((rw & (~VM_PROT_ALL)) == 0,
	    ("illegal ``rw'' argument to useracc (%x)\n", rw));
	prot = rw;
	/*
	 * XXX - check separately to disallow access to user area and user
	 * page tables - they are in the map.
	 *
	 * XXX - VM_MAXUSER_ADDRESS is an end address, not a max.  It was once
	 * only used (as an end address) in trap.c.  Use it as an end address
	 * here too.  This bogusness has spread.  I just fixed where it was
	 * used as a max in vm_mmap.c.
	 */
	if ((vm_offset_t) addr + len > /* XXX */ VM_MAXUSER_ADDRESS
	    || (vm_offset_t) addr + len < (vm_offset_t) addr) {
		return (FALSE);
	}
	map = &curproc->p_vmspace->vm_map;
	vm_map_lock_read(map);
	/*
	 * We save the map hint, and restore it.  Useracc appears to distort
	 * the map hint unnecessarily.
	 */
	save_hint = map->hint;
	rv = vm_map_check_protection(map,
	    trunc_page((vm_offset_t)addr), round_page((vm_offset_t)addr + len), prot);
	map->hint = save_hint;
	vm_map_unlock_read(map);
	
	return (rv == TRUE);
}

void
vslock(addr, len)
	caddr_t addr;
	u_int len;
{
	vm_map_pageable(&curproc->p_vmspace->vm_map, trunc_page((vm_offset_t)addr),
	    round_page((vm_offset_t)addr + len), FALSE);
}

void
vsunlock(addr, len)
	caddr_t addr;
	u_int len;
{
	vm_map_pageable(&curproc->p_vmspace->vm_map, trunc_page((vm_offset_t)addr),
	    round_page((vm_offset_t)addr + len), TRUE);
}

/*
 * Implement fork's actions on an address space.
 * Here we arrange for the address space to be copied or referenced,
 * allocate a user struct (pcb and kernel stack), then call the
 * machine-dependent layer to fill those in and make the new process
 * ready to run.  The new process is set up so that it returns directly
 * to user mode to avoid stack copying and relocation problems.
 */
void
vm_fork(p1, p2, flags)
	register struct proc *p1, *p2;
	int flags;
{
	register struct user *up;

	if ((flags & RFPROC) == 0) {
		/*
		 * Divorce the memory, if it is shared, essentially
		 * this changes shared memory amongst threads, into
		 * COW locally.
		 */
		if ((flags & RFMEM) == 0) {
			if (p1->p_vmspace->vm_refcnt > 1) {
				vmspace_unshare(p1);
			}
		}
		cpu_fork(p1, p2, flags);
		return;
	}

	if (flags & RFMEM) {
		p2->p_vmspace = p1->p_vmspace;
		p1->p_vmspace->vm_refcnt++;
	}

	while (vm_page_count_severe()) {
		VM_WAIT;
	}

	if ((flags & RFMEM) == 0) {
		p2->p_vmspace = vmspace_fork(p1->p_vmspace);

		pmap_pinit2(vmspace_pmap(p2->p_vmspace));

		if (p1->p_vmspace->vm_shm)
			shmfork(p1, p2);
	}

	pmap_new_proc(p2);

	up = p2->p_addr;

	/*
	 * p_stats currently points at fields in the user struct
	 * but not at &u, instead at p_addr. Copy parts of
	 * p_stats; zero the rest of p_stats (statistics).
	 *
	 * If procsig->ps_refcnt is 1 and p2->p_sigacts is NULL we dont' need
	 * to share sigacts, so we use the up->u_sigacts.
	 */
	p2->p_stats = &up->u_stats;
	if (p2->p_sigacts == NULL) {
		if (p2->p_procsig->ps_refcnt != 1)
			printf ("PID:%d NULL sigacts with refcnt not 1!\n",p2->p_pid);
		p2->p_sigacts = &up->u_sigacts;
		up->u_sigacts = *p1->p_sigacts;
	}

	bzero(&up->u_stats.pstat_startzero,
	    (unsigned) ((caddr_t) &up->u_stats.pstat_endzero -
		(caddr_t) &up->u_stats.pstat_startzero));
	bcopy(&p1->p_stats->pstat_startcopy, &up->u_stats.pstat_startcopy,
	    ((caddr_t) &up->u_stats.pstat_endcopy -
		(caddr_t) &up->u_stats.pstat_startcopy));


	/*
	 * cpu_fork will copy and update the pcb, set up the kernel stack,
	 * and make the child ready to run.
	 */
	cpu_fork(p1, p2, flags);
}

/*
 * Called after process has been wait(2)'ed apon and is being reaped.
 * The idea is to reclaim resources that we could not reclaim while  
 * the process was still executing.
 */
void
vm_waitproc(struct proc *p)
{
	cpu_wait(p);
	vmspace_exitfree(p);	/* and clean-out the vmspace */
}

/*
 * Set default limits for VM system.
 * Called for proc 0, and then inherited by all others.
 *
 * XXX should probably act directly on proc0.
 */
static void
vm_init_limits(udata)
	void *udata;
{
	register struct proc *p = udata;
	int rss_limit;

	/*
	 * Set up the initial limits on process VM. Set the maximum resident
	 * set size to be half of (reasonably) available memory.  Since this
	 * is a soft limit, it comes into effect only when the system is out
	 * of memory - half of main memory helps to favor smaller processes,
	 * and reduces thrashing of the object cache.
	 */
	p->p_rlimit[RLIMIT_STACK].rlim_cur = dflssiz;
	p->p_rlimit[RLIMIT_STACK].rlim_max = maxssiz;
	p->p_rlimit[RLIMIT_DATA].rlim_cur = dfldsiz;
	p->p_rlimit[RLIMIT_DATA].rlim_max = maxdsiz;
	/* limit the limit to no less than 2MB */
	rss_limit = max(cnt.v_free_count, 512);
	p->p_rlimit[RLIMIT_RSS].rlim_cur = ptoa(rss_limit);
	p->p_rlimit[RLIMIT_RSS].rlim_max = RLIM_INFINITY;
}

void
faultin(p)
	struct proc *p;
{
	int s;

	if ((p->p_flag & P_INMEM) == 0) {

		++p->p_lock;

		pmap_swapin_proc(p);

		s = splhigh();

		if (p->p_stat == SRUN)
			setrunqueue(p);

		p->p_flag |= P_INMEM;

		/* undo the effect of setting SLOCK above */
		--p->p_lock;
		splx(s);

	}
}

/*
 * This swapin algorithm attempts to swap-in processes only if there
 * is enough space for them.  Of course, if a process waits for a long
 * time, it will be swapped in anyway.
 */
/* ARGSUSED*/
static void
scheduler(dummy)
	void *dummy;
{
	register struct proc *p;
	register int pri;
	struct proc *pp;
	int ppri;

loop:
	if (vm_page_count_min()) {
		VM_WAIT;
		goto loop;
	}

	pp = NULL;
	ppri = INT_MIN;
	for (p = allproc.lh_first; p != 0; p = p->p_list.le_next) {
		if (p->p_stat == SRUN &&
			(p->p_flag & (P_INMEM | P_SWAPPING)) == 0) {

			pri = p->p_swtime + p->p_slptime;
			if ((p->p_flag & P_SWAPINREQ) == 0) {
				pri -= p->p_nice * 8;
			}

			/*
			 * if this process is higher priority and there is
			 * enough space, then select this process instead of
			 * the previous selection.
			 */
			if (pri > ppri) {
				pp = p;
				ppri = pri;
			}
		}
	}

	/*
	 * Nothing to do, back to sleep.
	 */
	if ((p = pp) == NULL) {
		tsleep(&proc0, PVM, "sched", 0);
		goto loop;
	}
	p->p_flag &= ~P_SWAPINREQ;

	/*
	 * We would like to bring someone in. (only if there is space).
	 */
	faultin(p);
	p->p_swtime = 0;
	goto loop;
}

#ifndef NO_SWAPPING

#define	swappable(p) \
	(((p)->p_lock == 0) && \
		((p)->p_flag & (P_TRACED|P_SYSTEM|P_INMEM|P_WEXIT|P_SWAPPING)) == P_INMEM)


/*
 * Swap_idle_threshold1 is the guaranteed swapped in time for a process
 */
static int swap_idle_threshold1 = 2;
SYSCTL_INT(_vm, OID_AUTO, swap_idle_threshold1,
	CTLFLAG_RW, &swap_idle_threshold1, 0, "");

/*
 * Swap_idle_threshold2 is the time that a process can be idle before
 * it will be swapped out, if idle swapping is enabled.
 */
static int swap_idle_threshold2 = 10;
SYSCTL_INT(_vm, OID_AUTO, swap_idle_threshold2,
	CTLFLAG_RW, &swap_idle_threshold2, 0, "");

/*
 * Swapout is driven by the pageout daemon.  Very simple, we find eligible
 * procs and unwire their u-areas.  We try to always "swap" at least one
 * process in case we need the room for a swapin.
 * If any procs have been sleeping/stopped for at least maxslp seconds,
 * they are swapped.  Else, we swap the longest-sleeping or stopped process,
 * if any, otherwise the longest-resident process.
 */
void
swapout_procs(action)
int action;
{
	register struct proc *p;
	struct proc *outp, *outp2;
	int outpri, outpri2;
	int didswap = 0;

	outp = outp2 = NULL;
	outpri = outpri2 = INT_MIN;
retry:
	for (p = allproc.lh_first; p != 0; p = p->p_list.le_next) {
		struct vmspace *vm;
		if (!swappable(p))
			continue;

		vm = p->p_vmspace;

		switch (p->p_stat) {
		default:
			continue;

		case SSLEEP:
		case SSTOP:
			/*
			 * do not swapout a realtime process
			 */
			if (RTP_PRIO_IS_REALTIME(p->p_rtprio.type))
				continue;

			/*
			 * Do not swapout a process waiting on a critical
			 * event of some kind.  Also guarantee swap_idle_threshold1
			 * time in memory.
			 */
			if (((p->p_priority & 0x7f) < PSOCK) ||
				(p->p_slptime < swap_idle_threshold1))
				continue;

			/*
			 * If the system is under memory stress, or if we are swapping
			 * idle processes >= swap_idle_threshold2, then swap the process
			 * out.
			 */
			if (((action & VM_SWAP_NORMAL) == 0) &&
				(((action & VM_SWAP_IDLE) == 0) ||
				  (p->p_slptime < swap_idle_threshold2)))
				continue;

			++vm->vm_refcnt;
			/*
			 * do not swapout a process that is waiting for VM
			 * data structures there is a possible deadlock.
			 */
			if (lockmgr(&vm->vm_map.lock,
					LK_EXCLUSIVE | LK_NOWAIT,
					(void *)0, curproc)) {
				vmspace_free(vm);
				continue;
			}
			vm_map_unlock(&vm->vm_map);
			/*
			 * If the process has been asleep for awhile and had
			 * most of its pages taken away already, swap it out.
			 */
			if ((action & VM_SWAP_NORMAL) ||
				((action & VM_SWAP_IDLE) &&
				 (p->p_slptime > swap_idle_threshold2))) {
				swapout(p);
				vmspace_free(vm);
				didswap++;
				goto retry;
			}

			/*
			 * cleanup our reference
			 */
			vmspace_free(vm);
		}
	}
	/*
	 * If we swapped something out, and another process needed memory,
	 * then wakeup the sched process.
	 */
	if (didswap)
		wakeup(&proc0);
}

static void
swapout(p)
	register struct proc *p;
{

#if defined(SWAP_DEBUG)
	printf("swapping out %d\n", p->p_pid);
#endif
	++p->p_stats->p_ru.ru_nswap;
	/*
	 * remember the process resident count
	 */
	p->p_vmspace->vm_swrss = vmspace_resident_count(p->p_vmspace);

	(void) splhigh();
	p->p_flag &= ~P_INMEM;
	p->p_flag |= P_SWAPPING;
	if (p->p_stat == SRUN)
		remrunqueue(p);
	(void) spl0();

	pmap_swapout_proc(p);

	p->p_flag &= ~P_SWAPPING;
	p->p_swtime = 0;
}
#endif /* !NO_SWAPPING */
