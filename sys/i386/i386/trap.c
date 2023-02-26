/*-
 * Copyright (C) 1994, David Greenman
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the University of Utah, and William Jolitz.
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
 *	from: @(#)trap.c	7.4 (Berkeley) 5/13/91
 * $FreeBSD: src/sys/i386/i386/trap.c,v 1.147.2.11 2003/02/27 19:09:59 luoqi Exp $
 */

/*
 * 386 Trap and System call handling
 */

#include "opt_cpu.h"
#include "opt_ddb.h"
#include "opt_ktrace.h"
#include "opt_clock.h"
#include "opt_trap.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/pioctl.h>
#include <sys/kernel.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/uio.h>
#include <sys/vmmeter.h>
#ifdef KTRACE
#include <sys/ktrace.h>
#endif

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_extern.h>

#include <machine/cpu.h>
#include <machine/ipl.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#ifdef SMP
#include <machine/smp.h>
#endif
#include <machine/tss.h>

#include <i386/isa/intr_machdep.h>

#ifdef POWERFAIL_NMI
#include <sys/syslog.h>
#include <machine/clock.h>
#endif

#include <machine/vm86.h>

#include <ddb/ddb.h>

#include "isa.h"
#include "npx.h"

int (*pmath_emulate) __P((struct trapframe *));

extern void trap __P((struct trapframe frame));
extern int trapwrite __P((unsigned addr));
extern void syscall2 __P((struct trapframe frame));

static int trap_pfault __P((struct trapframe *, int, vm_offset_t));
static void trap_fatal __P((struct trapframe *, vm_offset_t));
void dblfault_handler __P((void));

extern inthand_t IDTVEC(syscall);

#define MAX_TRAP_MSG		28
static char *trap_msg[] = {
	"",					/*  0 unused */
	"privileged instruction fault",		/*  1 T_PRIVINFLT */
	"",					/*  2 unused */
	"breakpoint instruction fault",		/*  3 T_BPTFLT */
	"",					/*  4 unused */
	"",					/*  5 unused */
	"arithmetic trap",			/*  6 T_ARITHTRAP */
	"system forced exception",		/*  7 T_ASTFLT */
	"",					/*  8 unused */
	"general protection fault",		/*  9 T_PROTFLT */
	"trace trap",				/* 10 T_TRCTRAP */
	"",					/* 11 unused */
	"page fault",				/* 12 T_PAGEFLT */
	"",					/* 13 unused */
	"alignment fault",			/* 14 T_ALIGNFLT */
	"",					/* 15 unused */
	"",					/* 16 unused */
	"",					/* 17 unused */
	"integer divide fault",			/* 18 T_DIVIDE */
	"non-maskable interrupt trap",		/* 19 T_NMI */
	"overflow trap",			/* 20 T_OFLOW */
	"FPU bounds check fault",		/* 21 T_BOUND */
	"FPU device not available",		/* 22 T_DNA */
	"double fault",				/* 23 T_DOUBLEFLT */
	"FPU operand fetch fault",		/* 24 T_FPOPFLT */
	"invalid TSS fault",			/* 25 T_TSSFLT */
	"segment not present fault",		/* 26 T_SEGNPFLT */
	"stack fault",				/* 27 T_STKFLT */
	"machine check trap",			/* 28 T_MCHK */
};

static __inline int userret __P((struct proc *p, struct trapframe *frame,
				  u_quad_t oticks, int have_mplock));

#if defined(I586_CPU) && !defined(NO_F00F_HACK)
extern int has_f00f_bug;
#endif

#ifdef DDB
static int ddb_on_nmi = 1;
SYSCTL_INT(_machdep, OID_AUTO, ddb_on_nmi, CTLFLAG_RW,
	&ddb_on_nmi, 0, "Go to DDB on NMI");
#endif
static int panic_on_nmi = 1;
SYSCTL_INT(_machdep, OID_AUTO, panic_on_nmi, CTLFLAG_RW,
	&panic_on_nmi, 0, "Panic on NMI");

static __inline int
userret(p, frame, oticks, have_mplock)
	struct proc *p;
	struct trapframe *frame;
	u_quad_t oticks;
	int have_mplock;
{
	int sig, s;

	while ((sig = CURSIG(p)) != 0) {
		if (have_mplock == 0) {
			get_mplock();
			have_mplock = 1;
		}
		postsig(sig);
	}

	p->p_priority = p->p_usrpri;
	if (resched_wanted()) {
		/*
		 * Since we are curproc, clock will normally just change
		 * our priority without moving us from one queue to another
		 * (since the running process is not on a queue.)
		 * If that happened after we setrunqueue ourselves but before we
		 * mi_switch()'ed, we might not be on the queue indicated by
		 * our priority.
		 */
		if (have_mplock == 0) {
			get_mplock();
			have_mplock = 1;
		}
		s = splhigh();
		setrunqueue(p);
		p->p_stats->p_ru.ru_nivcsw++;
		mi_switch();
		splx(s);
		while ((sig = CURSIG(p)) != 0)
			postsig(sig);
	}
	/*
	 * Charge system time if profiling.
	 */
	if (p->p_flag & P_PROFIL) {
		if (have_mplock == 0) {
			get_mplock();
			have_mplock = 1;
		}
		addupc_task(p, frame->tf_eip,
			    (u_int)(p->p_sticks - oticks) * psratio);
	}
	curpriority = p->p_priority;
	return(have_mplock);
}

#ifdef DEVICE_POLLING
extern u_int32_t poll_in_trap;
extern int ether_poll __P((int count));
#endif /* DEVICE_POLLING */

/*
 * Exception, fault, and trap interface to the FreeBSD kernel.
 * This common code is called from assembly language IDT gate entry
 * routines that prepare a suitable stack frame, and restore this
 * frame after the exception has been processed.
 */

void
trap(frame)
	struct trapframe frame;
{
	struct proc *p = curproc;
	u_quad_t sticks = 0;
	int i = 0, ucode = 0, type, code;
	vm_offset_t eva;

#ifdef DDB
	if (db_active) {
		eva = (frame.tf_trapno == T_PAGEFLT ? rcr2() : 0);
		trap_fatal(&frame, eva);
		return;
	}
#endif

	if (!(frame.tf_eflags & PSL_I)) {
		/*
		 * Buggy application or kernel code has disabled interrupts
		 * and then trapped.  Enabling interrupts now is wrong, but
		 * it is better than running with interrupts disabled until
		 * they are accidentally enabled later.
		 */
		type = frame.tf_trapno;
		if (ISPL(frame.tf_cs) == SEL_UPL || (frame.tf_eflags & PSL_VM))
			printf(
			    "pid %ld (%s): trap %d with interrupts disabled\n",
			    (long)curproc->p_pid, curproc->p_comm, type);
		else if (type != T_BPTFLT && type != T_TRCTRAP)
			/*
			 * XXX not quite right, since this may be for a
			 * multiple fault in user mode.
			 */
			printf("kernel trap %d with interrupts disabled\n",
			    type);
		enable_intr();
	}

	eva = 0;
	if (frame.tf_trapno == T_PAGEFLT) {
		/*
		 * For some Cyrix CPUs, %cr2 is clobbered by interrupts.
		 * This problem is worked around by using an interrupt
		 * gate for the pagefault handler.  We are finally ready
		 * to read %cr2 and then must reenable interrupts.
		 *
		 * XXX this should be in the switch statement, but the
		 * NO_FOOF_HACK and VM86 goto and ifdefs obfuscate the
		 * flow of control too much for this to be obviously
		 * correct.
		 */
		eva = rcr2();
		enable_intr();
	}

#ifdef DEVICE_POLLING
	if (poll_in_trap)
		ether_poll(poll_in_trap);
#endif /* DEVICE_POLLING */

#if defined(I586_CPU) && !defined(NO_F00F_HACK)
restart:
#endif
	type = frame.tf_trapno;
	code = frame.tf_err;

	if (in_vm86call) {
		if (frame.tf_eflags & PSL_VM &&
		    (type == T_PROTFLT || type == T_STKFLT)) {
			i = vm86_emulate((struct vm86frame *)&frame);
			if (i != 0)
				/*
				 * returns to original process
				 */
				vm86_trap((struct vm86frame *)&frame);
			return;
		}
		switch (type) {
			/*
			 * these traps want either a process context, or
			 * assume a normal userspace trap.
			 */
		case T_PROTFLT:
		case T_SEGNPFLT:
			trap_fatal(&frame, eva);
			return;
		case T_TRCTRAP:
			type = T_BPTFLT;	/* kernel breakpoint */
			/* FALL THROUGH */
		}
		goto kernel_trap;	/* normal kernel trap handling */
	}

        if ((ISPL(frame.tf_cs) == SEL_UPL) || (frame.tf_eflags & PSL_VM)) {
		/* user trap */

		sticks = p->p_sticks;
		p->p_md.md_regs = &frame;

		switch (type) {
		case T_PRIVINFLT:	/* privileged instruction fault */
			ucode = type;
			i = SIGILL;
			break;

		case T_BPTFLT:		/* bpt instruction fault */
		case T_TRCTRAP:		/* trace trap */
			frame.tf_eflags &= ~PSL_T;
			i = SIGTRAP;
			break;

		case T_ARITHTRAP:	/* arithmetic trap */
			ucode = code;
			i = SIGFPE;
			break;

		case T_ASTFLT:		/* Allow process switch */
			astoff();
			cnt.v_soft++;
			if (p->p_flag & P_OWEUPC) {
				p->p_flag &= ~P_OWEUPC;
				addupc_task(p, p->p_stats->p_prof.pr_addr,
					    p->p_stats->p_prof.pr_ticks);
			}
			goto out;

			/*
			 * The following two traps can happen in
			 * vm86 mode, and, if so, we want to handle
			 * them specially.
			 */
		case T_PROTFLT:		/* general protection fault */
		case T_STKFLT:		/* stack fault */
			if (frame.tf_eflags & PSL_VM) {
				i = vm86_emulate((struct vm86frame *)&frame);
				if (i == 0)
					goto out;
				break;
			}
			/* FALL THROUGH */

		case T_SEGNPFLT:	/* segment not present fault */
		case T_TSSFLT:		/* invalid TSS fault */
		case T_DOUBLEFLT:	/* double fault */
		default:
			ucode = code + BUS_SEGM_FAULT ;
			i = SIGBUS;
			break;

		case T_PAGEFLT:		/* page fault */
			i = trap_pfault(&frame, TRUE, eva);
			if (i == -1)
				return;
#if defined(I586_CPU) && !defined(NO_F00F_HACK)
			if (i == -2)
				goto restart;
#endif
			if (i == 0)
				goto out;

			ucode = T_PAGEFLT;
			break;

		case T_DIVIDE:		/* integer divide fault */
			ucode = FPE_INTDIV;
			i = SIGFPE;
			break;

#if NISA > 0
		case T_NMI:
#ifdef POWERFAIL_NMI
			goto handle_powerfail;
#else /* !POWERFAIL_NMI */
			/* machine/parity/power fail/"kitchen sink" faults */
			if (isa_nmi(code) == 0) {
#ifdef DDB
				/*
				 * NMI can be hooked up to a pushbutton
				 * for debugging.
				 */
				if (ddb_on_nmi) {
					printf ("NMI ... going to debugger\n");
					kdb_trap (type, 0, &frame);
				}
#endif /* DDB */
				return;
			} else if (panic_on_nmi)
				panic("NMI indicates hardware failure");
			break;
#endif /* POWERFAIL_NMI */
#endif /* NISA > 0 */

		case T_OFLOW:		/* integer overflow fault */
			ucode = FPE_INTOVF;
			i = SIGFPE;
			break;

		case T_BOUND:		/* bounds check fault */
			ucode = FPE_FLTSUB;
			i = SIGFPE;
			break;

		case T_DNA:
#if NNPX > 0
			/* if a transparent fault (due to context switch "late") */
			if (npxdna())
				return;
#endif
			if (!pmath_emulate) {
				i = SIGFPE;
				ucode = FPE_FPU_NP_TRAP;
				break;
			}
			i = (*pmath_emulate)(&frame);
			if (i == 0) {
				if (!(frame.tf_eflags & PSL_T))
					return;
				frame.tf_eflags &= ~PSL_T;
				i = SIGTRAP;
			}
			/* else ucode = emulator_only_knows() XXX */
			break;

		case T_FPOPFLT:		/* FPU operand fetch fault */
			ucode = T_FPOPFLT;
			i = SIGILL;
			break;

		case T_XMMFLT:		/* SIMD floating-point exception */
			ucode = 0; /* XXX */
			i = SIGFPE;
			break;
		}
	} else {
kernel_trap:
		/* kernel trap */

		switch (type) {
		case T_PAGEFLT:			/* page fault */
			(void) trap_pfault(&frame, FALSE, eva);
			return;

		case T_DNA:
#if NNPX > 0
			/*
			 * The kernel is apparently using npx for copying.
			 * XXX this should be fatal unless the kernel has
			 * registered such use.
			 */
			if (npxdna())
				return;
#endif
			break;

		case T_PROTFLT:		/* general protection fault */
		case T_SEGNPFLT:	/* segment not present fault */
			/*
			 * Invalid segment selectors and out of bounds
			 * %eip's and %esp's can be set up in user mode.
			 * This causes a fault in kernel mode when the
			 * kernel tries to return to user mode.  We want
			 * to get this fault so that we can fix the
			 * problem here and not have to check all the
			 * selectors and pointers when the user changes
			 * them.
			 */
#define	MAYBE_DORETI_FAULT(where, whereto)				\
	do {								\
		if (frame.tf_eip == (int)where) {			\
			frame.tf_eip = (int)whereto;			\
			return;						\
		}							\
	} while (0)

			if (intr_nesting_level == 0) {
				/*
				 * Invalid %fs's and %gs's can be created using
				 * procfs or PT_SETREGS or by invalidating the
				 * underlying LDT entry.  This causes a fault
				 * in kernel mode when the kernel attempts to
				 * switch contexts.  Lose the bad context
				 * (XXX) so that we can continue, and generate
				 * a signal.
				 */
				if (frame.tf_eip == (int)cpu_switch_load_gs) {
					curpcb->pcb_gs = 0;
					psignal(p, SIGBUS);
					return;
				}
				MAYBE_DORETI_FAULT(doreti_iret,
						   doreti_iret_fault);
				MAYBE_DORETI_FAULT(doreti_popl_ds,
						   doreti_popl_ds_fault);
				MAYBE_DORETI_FAULT(doreti_popl_es,
						   doreti_popl_es_fault);
				MAYBE_DORETI_FAULT(doreti_popl_fs,
						   doreti_popl_fs_fault);
				if (curpcb && curpcb->pcb_onfault) {
					frame.tf_eip = (int)curpcb->pcb_onfault;
					return;
				}
			}
			break;

		case T_TSSFLT:
			/*
			 * PSL_NT can be set in user mode and isn't cleared
			 * automatically when the kernel is entered.  This
			 * causes a TSS fault when the kernel attempts to
			 * `iret' because the TSS link is uninitialized.  We
			 * want to get this fault so that we can fix the
			 * problem here and not every time the kernel is
			 * entered.
			 */
			if (frame.tf_eflags & PSL_NT) {
				frame.tf_eflags &= ~PSL_NT;
				return;
			}
			break;

		case T_TRCTRAP:	 /* trace trap */
			if (frame.tf_eip == (int)IDTVEC(syscall)) {
				/*
				 * We've just entered system mode via the
				 * syscall lcall.  Continue single stepping
				 * silently until the syscall handler has
				 * saved the flags.
				 */
				return;
			}
			if (frame.tf_eip == (int)IDTVEC(syscall) + 1) {
				/*
				 * The syscall handler has now saved the
				 * flags.  Stop single stepping it.
				 */
				frame.tf_eflags &= ~PSL_T;
				return;
			}
                        /*
                         * Ignore debug register trace traps due to
                         * accesses in the user's address space, which
                         * can happen under several conditions such as
                         * if a user sets a watchpoint on a buffer and
                         * then passes that buffer to a system call.
                         * We still want to get TRCTRAPS for addresses
                         * in kernel space because that is useful when
                         * debugging the kernel.
                         */
                        if (user_dbreg_trap()) {
                                /*
                                 * Reset breakpoint bits because the
                                 * processor doesn't
                                 */
                                load_dr6(rdr6() & 0xfffffff0);
                                return;
                        }
			/*
			 * Fall through (TRCTRAP kernel mode, kernel address)
			 */
		case T_BPTFLT:
			/*
			 * If DDB is enabled, let it handle the debugger trap.
			 * Otherwise, debugger traps "can't happen".
			 */
#ifdef DDB
			if (kdb_trap (type, 0, &frame))
				return;
#endif
			break;

#if NISA > 0
		case T_NMI:
#ifdef POWERFAIL_NMI
#ifndef TIMER_FREQ
#  define TIMER_FREQ 1193182
#endif
	handle_powerfail:
		{
		  static unsigned lastalert = 0;

		  if(time_second - lastalert > 10)
		    {
		      log(LOG_WARNING, "NMI: power fail\n");
		      sysbeep(TIMER_FREQ/880, hz);
		      lastalert = time_second;
		    }
		  return;
		}
#else /* !POWERFAIL_NMI */
			/* machine/parity/power fail/"kitchen sink" faults */
			if (isa_nmi(code) == 0) {
#ifdef DDB
				/*
				 * NMI can be hooked up to a pushbutton
				 * for debugging.
				 */
				if (ddb_on_nmi) {
					printf ("NMI ... going to debugger\n");
					kdb_trap (type, 0, &frame);
				}
#endif /* DDB */
				return;
			} else if (panic_on_nmi == 0)
				return;
			/* FALL THROUGH */
#endif /* POWERFAIL_NMI */
#endif /* NISA > 0 */
		}

		trap_fatal(&frame, eva);
		return;
	}

	/* Translate fault for emulators (e.g. Linux) */
	if (*p->p_sysent->sv_transtrap)
		i = (*p->p_sysent->sv_transtrap)(i, type);

	trapsignal(p, i, ucode);

#ifdef DEBUG
	if (type <= MAX_TRAP_MSG) {
		uprintf("fatal process exception: %s",
			trap_msg[type]);
		if ((type == T_PAGEFLT) || (type == T_PROTFLT))
			uprintf(", fault VA = 0x%lx", (u_long)eva);
		uprintf("\n");
	}
#endif

out:
	userret(p, &frame, sticks, 1);
}

#ifdef notyet
/*
 * This version doesn't allow a page fault to user space while
 * in the kernel. The rest of the kernel needs to be made "safe"
 * before this can be used. I think the only things remaining
 * to be made safe are the iBCS2 code and the process tracing/
 * debugging code.
 */
static int
trap_pfault(frame, usermode, eva)
	struct trapframe *frame;
	int usermode;
	vm_offset_t eva;
{
	vm_offset_t va;
	struct vmspace *vm = NULL;
	vm_map_t map = 0;
	int rv = 0;
	vm_prot_t ftype;
	struct proc *p = curproc;

	if (frame->tf_err & PGEX_W)
		ftype = VM_PROT_WRITE;
	else
		ftype = VM_PROT_READ;

	va = trunc_page(eva);
	if (va < VM_MIN_KERNEL_ADDRESS) {
		vm_offset_t v;
		vm_page_t mpte;

		if (p == NULL ||
		    (!usermode && va < VM_MAXUSER_ADDRESS &&
		     (intr_nesting_level != 0 || curpcb == NULL ||
		      curpcb->pcb_onfault == NULL))) {
			trap_fatal(frame, eva);
			return (-1);
		}

		/*
		 * This is a fault on non-kernel virtual memory.
		 * vm is initialized above to NULL. If curproc is NULL
		 * or curproc->p_vmspace is NULL the fault is fatal.
		 */
		vm = p->p_vmspace;
		if (vm == NULL)
			goto nogo;

		map = &vm->vm_map;

		/*
		 * Keep swapout from messing with us during this
		 *	critical time.
		 */
		++p->p_lock;

		/*
		 * Grow the stack if necessary
		 */
		/* grow_stack returns false only if va falls into
		 * a growable stack region and the stack growth
		 * fails.  It returns true if va was not within
		 * a growable stack region, or if the stack 
		 * growth succeeded.
		 */
		if (!grow_stack (p, va)) {
			rv = KERN_FAILURE;
			--p->p_lock;
			goto nogo;
		}
		
		/* Fault in the user page: */
		rv = vm_fault(map, va, ftype,
			      (ftype & VM_PROT_WRITE) ? VM_FAULT_DIRTY
						      : VM_FAULT_NORMAL);

		--p->p_lock;
	} else {
		/*
		 * Don't allow user-mode faults in kernel address space.
		 */
		if (usermode)
			goto nogo;

		/*
		 * Since we know that kernel virtual address addresses
		 * always have pte pages mapped, we just have to fault
		 * the page.
		 */
		rv = vm_fault(kernel_map, va, ftype, VM_FAULT_NORMAL);
	}

	if (rv == KERN_SUCCESS)
		return (0);
nogo:
	if (!usermode) {
		if (intr_nesting_level == 0 && curpcb && curpcb->pcb_onfault) {
			frame->tf_eip = (int)curpcb->pcb_onfault;
			return (0);
		}
		trap_fatal(frame, eva);
		return (-1);
	}

	/* kludge to pass faulting virtual address to sendsig */
	frame->tf_err = eva;

	return((rv == KERN_PROTECTION_FAILURE) ? SIGBUS : SIGSEGV);
}
#endif

int
trap_pfault(frame, usermode, eva)
	struct trapframe *frame;
	int usermode;
	vm_offset_t eva;
{
	vm_offset_t va;
	struct vmspace *vm = NULL;
	vm_map_t map = 0;
	int rv = 0;
	vm_prot_t ftype;
	struct proc *p = curproc;

	va = trunc_page(eva);
	if (va >= KERNBASE) {
		/*
		 * Don't allow user-mode faults in kernel address space.
		 * An exception:  if the faulting address is the invalid
		 * instruction entry in the IDT, then the Intel Pentium
		 * F00F bug workaround was triggered, and we need to
		 * treat it is as an illegal instruction, and not a page
		 * fault.
		 */
#if defined(I586_CPU) && !defined(NO_F00F_HACK)
		if ((eva == (unsigned int)&idt[6]) && has_f00f_bug) {
			frame->tf_trapno = T_PRIVINFLT;
			return -2;
		}
#endif
		if (usermode)
			goto nogo;

		map = kernel_map;
	} else {
		/*
		 * This is a fault on non-kernel virtual memory.
		 * vm is initialized above to NULL. If curproc is NULL
		 * or curproc->p_vmspace is NULL the fault is fatal.
		 */
		if (p != NULL)
			vm = p->p_vmspace;

		if (vm == NULL)
			goto nogo;

		map = &vm->vm_map;
	}

	if (frame->tf_err & PGEX_W)
		ftype = VM_PROT_WRITE;
	else
		ftype = VM_PROT_READ;

	if (map != kernel_map) {
		/*
		 * Keep swapout from messing with us during this
		 *	critical time.
		 */
		++p->p_lock;

		/*
		 * Grow the stack if necessary
		 */
		/* grow_stack returns false only if va falls into
		 * a growable stack region and the stack growth
		 * fails.  It returns true if va was not within
		 * a growable stack region, or if the stack 
		 * growth succeeded.
		 */
		if (!grow_stack (p, va)) {
			rv = KERN_FAILURE;
			--p->p_lock;
			goto nogo;
		}

		/* Fault in the user page: */
		rv = vm_fault(map, va, ftype,
			      (ftype & VM_PROT_WRITE) ? VM_FAULT_DIRTY
						      : VM_FAULT_NORMAL);

		--p->p_lock;
	} else {
		/*
		 * Don't have to worry about process locking or stacks in the kernel.
		 */
		rv = vm_fault(map, va, ftype, VM_FAULT_NORMAL);
	}

	if (rv == KERN_SUCCESS)
		return (0);
nogo:
	if (!usermode) {
		if (intr_nesting_level == 0 && curpcb && curpcb->pcb_onfault) {
			frame->tf_eip = (int)curpcb->pcb_onfault;
			return (0);
		}
		trap_fatal(frame, eva);
		return (-1);
	}

	/* kludge to pass faulting virtual address to sendsig */
	frame->tf_err = eva;

	return((rv == KERN_PROTECTION_FAILURE) ? SIGBUS : SIGSEGV);
}

static void
trap_fatal(frame, eva)
	struct trapframe *frame;
	vm_offset_t eva;
{
	int code, type, ss, esp;
	struct soft_segment_descriptor softseg;

	code = frame->tf_err;
	type = frame->tf_trapno;
	sdtossd(&gdt[IDXSEL(frame->tf_cs & 0xffff)].sd, &softseg);

	if (type <= MAX_TRAP_MSG)
		printf("\n\nFatal trap %d: %s while in %s mode\n",
			type, trap_msg[type],
        		frame->tf_eflags & PSL_VM ? "vm86" :
			ISPL(frame->tf_cs) == SEL_UPL ? "user" : "kernel");
#ifdef SMP
	/* three seperate prints in case of a trap on an unmapped page */
	printf("mp_lock = %08x; ", mp_lock);
	printf("cpuid = %d; ", cpuid);
	printf("lapic.id = %08x\n", lapic.id);
#endif
	if (type == T_PAGEFLT) {
		printf("fault virtual address	= 0x%x\n", eva);
		printf("fault code		= %s %s, %s\n",
			code & PGEX_U ? "user" : "supervisor",
			code & PGEX_W ? "write" : "read",
			code & PGEX_P ? "protection violation" : "page not present");
	}
	printf("instruction pointer	= 0x%x:0x%x\n",
	       frame->tf_cs & 0xffff, frame->tf_eip);
        if ((ISPL(frame->tf_cs) == SEL_UPL) || (frame->tf_eflags & PSL_VM)) {
		ss = frame->tf_ss & 0xffff;
		esp = frame->tf_esp;
	} else {
		ss = GSEL(GDATA_SEL, SEL_KPL);
		esp = (int)&frame->tf_esp;
	}
	printf("stack pointer	        = 0x%x:0x%x\n", ss, esp);
	printf("frame pointer	        = 0x%x:0x%x\n", ss, frame->tf_ebp);
	printf("code segment		= base 0x%x, limit 0x%x, type 0x%x\n",
	       softseg.ssd_base, softseg.ssd_limit, softseg.ssd_type);
	printf("			= DPL %d, pres %d, def32 %d, gran %d\n",
	       softseg.ssd_dpl, softseg.ssd_p, softseg.ssd_def32,
	       softseg.ssd_gran);
	printf("processor eflags	= ");
	if (frame->tf_eflags & PSL_T)
		printf("trace trap, ");
	if (frame->tf_eflags & PSL_I)
		printf("interrupt enabled, ");
	if (frame->tf_eflags & PSL_NT)
		printf("nested task, ");
	if (frame->tf_eflags & PSL_RF)
		printf("resume, ");
	if (frame->tf_eflags & PSL_VM)
		printf("vm86, ");
	printf("IOPL = %d\n", (frame->tf_eflags & PSL_IOPL) >> 12);
	printf("current process		= ");
	if (curproc) {
		printf("%lu (%s)\n",
		    (u_long)curproc->p_pid, curproc->p_comm ?
		    curproc->p_comm : "");
	} else {
		printf("Idle\n");
	}
	printf("interrupt mask		= ");
	if ((cpl & net_imask) == net_imask)
		printf("net ");
	if ((cpl & tty_imask) == tty_imask)
		printf("tty ");
	if ((cpl & bio_imask) == bio_imask)
		printf("bio ");
	if ((cpl & cam_imask) == cam_imask)
		printf("cam ");
	if (cpl == 0)
		printf("none");
#ifdef SMP
/**
 *  XXX FIXME:
 *	we probably SHOULD have stopped the other CPUs before now!
 *	another CPU COULD have been touching cpl at this moment...
 */
	printf(" <- SMP: XXX");
#endif
	printf("\n");

#ifdef KDB
	if (kdb_trap(&psl))
		return;
#endif
#ifdef DDB
	if ((debugger_on_panic || db_active) && kdb_trap(type, 0, frame))
		return;
#endif
	printf("trap number		= %d\n", type);
	if (type <= MAX_TRAP_MSG)
		panic("%s", trap_msg[type]);
	else
		panic("unknown/reserved trap");
}

/*
 * Double fault handler. Called when a fault occurs while writing
 * a frame for a trap/exception onto the stack. This usually occurs
 * when the stack overflows (such is the case with infinite recursion,
 * for example).
 *
 * XXX Note that the current PTD gets replaced by IdlePTD when the
 * task switch occurs. This means that the stack that was active at
 * the time of the double fault is not available at <kstack> unless
 * the machine was idle when the double fault occurred. The downside
 * of this is that "trace <ebp>" in ddb won't work.
 */
void
dblfault_handler()
{
	printf("\nFatal double fault:\n");
	printf("eip = 0x%x\n", common_tss.tss_eip);
	printf("esp = 0x%x\n", common_tss.tss_esp);
	printf("ebp = 0x%x\n", common_tss.tss_ebp);
#ifdef SMP
	/* three seperate prints in case of a trap on an unmapped page */
	printf("mp_lock = %08x; ", mp_lock);
	printf("cpuid = %d; ", cpuid);
	printf("lapic.id = %08x\n", lapic.id);
#endif
	panic("double fault");
}

/*
 * Compensate for 386 brain damage (missing URKR).
 * This is a little simpler than the pagefault handler in trap() because
 * it the page tables have already been faulted in and high addresses
 * are thrown out early for other reasons.
 */
int trapwrite(addr)
	unsigned addr;
{
	struct proc *p;
	vm_offset_t va;
	struct vmspace *vm;
	int rv;

	va = trunc_page((vm_offset_t)addr);
	/*
	 * XXX - MAX is END.  Changed > to >= for temp. fix.
	 */
	if (va >= VM_MAXUSER_ADDRESS)
		return (1);

	p = curproc;
	vm = p->p_vmspace;

	++p->p_lock;

	if (!grow_stack (p, va)) {
		--p->p_lock;
		return (1);
	}

	/*
	 * fault the data page
	 */
	rv = vm_fault(&vm->vm_map, va, VM_PROT_WRITE, VM_FAULT_DIRTY);

	--p->p_lock;

	if (rv != KERN_SUCCESS)
		return 1;

	return (0);
}

/*
 *	syscall2 -	MP aware system call request C handler
 *
 *	A system call is essentially treated as a trap except that the
 *	MP lock is not held on entry or return.  We are responsible for
 *	obtaining the MP lock if necessary and for handling ASTs
 *	(e.g. a task switch) prior to return.
 *
 *	In general, only simple access and manipulation of curproc and
 *	the current stack is allowed without having to hold MP lock.
 */
void
syscall2(frame)
	struct trapframe frame;
{
	caddr_t params;
	int i;
	struct sysent *callp;
	struct proc *p = curproc;
	register_t orig_tf_eflags;
	u_quad_t sticks;
	int error;
	int narg;
	int args[8];
	int have_mplock = 0;
	u_int code;

#ifdef DIAGNOSTIC
	if (ISPL(frame.tf_cs) != SEL_UPL) {
		get_mplock();
		panic("syscall");
		/* NOT REACHED */
	}
#endif

	/*
	 * handle atomicy by looping since interrupts are enabled and the 
	 * MP lock is not held.
	 */
	sticks = ((volatile struct proc *)p)->p_sticks;	
	while (sticks != ((volatile struct proc *)p)->p_sticks)
		sticks = ((volatile struct proc *)p)->p_sticks;

	p->p_md.md_regs = &frame;
	params = (caddr_t)frame.tf_esp + sizeof(int);
	code = frame.tf_eax;
	orig_tf_eflags = frame.tf_eflags;

	if (p->p_sysent->sv_prepsyscall) {
		/*
		 * The prep code is not MP aware.
		 */
		get_mplock();
		(*p->p_sysent->sv_prepsyscall)(&frame, args, &code, &params);
		rel_mplock();
	} else {
		/*
		 * Need to check if this is a 32 bit or 64 bit syscall.
		 * fuword is MP aware.
		 */
		if (code == SYS_syscall) {
			/*
			 * Code is first argument, followed by actual args.
			 */
			code = fuword(params);
			params += sizeof(int);
		} else if (code == SYS___syscall) {
			/*
			 * Like syscall, but code is a quad, so as to maintain
			 * quad alignment for the rest of the arguments.
			 */
			code = fuword(params);
			params += sizeof(quad_t);
		}
	}

 	if (p->p_sysent->sv_mask)
 		code &= p->p_sysent->sv_mask;

 	if (code >= p->p_sysent->sv_size)
 		callp = &p->p_sysent->sv_table[0];
  	else
 		callp = &p->p_sysent->sv_table[code];

	narg = callp->sy_narg & SYF_ARGMASK;

	/*
	 * copyin is MP aware, but the tracing code is not
	 */
	if (params && (i = narg * sizeof(int)) &&
	    (error = copyin(params, (caddr_t)args, (u_int)i))) {
		get_mplock();
		have_mplock = 1;
#ifdef KTRACE
		if (KTRPOINT(p, KTR_SYSCALL))
			ktrsyscall(p->p_tracep, code, narg, args);
#endif
		goto bad;
	}

	/*
	 * Try to run the syscall without the MP lock if the syscall
	 * is MP safe.  We have to obtain the MP lock no matter what if 
	 * we are ktracing
	 */
	if ((callp->sy_narg & SYF_MPSAFE) == 0) {
		get_mplock();
		have_mplock = 1;
	}

#ifdef KTRACE
	if (KTRPOINT(p, KTR_SYSCALL)) {
		if (have_mplock == 0) {
			get_mplock();
			have_mplock = 1;
		}
		ktrsyscall(p->p_tracep, code, narg, args);
	}
#endif
	p->p_retval[0] = 0;
	p->p_retval[1] = frame.tf_edx;

	STOPEVENT(p, S_SCE, narg);	/* MP aware */

	error = (*callp->sy_call)(p, args);

	/*
	 * MP SAFE (we may or may not have the MP lock at this point)
	 */
	switch (error) {
	case 0:
		/*
		 * Reinitialize proc pointer `p' as it may be different
		 * if this is a child returning from fork syscall.
		 */
		p = curproc;
		frame.tf_eax = p->p_retval[0];
		frame.tf_edx = p->p_retval[1];
		frame.tf_eflags &= ~PSL_C;
		break;

	case ERESTART:
		/*
		 * Reconstruct pc, assuming lcall $X,y is 7 bytes,
		 * int 0x80 is 2 bytes. We saved this in tf_err.
		 */
		frame.tf_eip -= frame.tf_err;
		break;

	case EJUSTRETURN:
		break;

	default:
bad:
 		if (p->p_sysent->sv_errsize) {
 			if (error >= p->p_sysent->sv_errsize)
  				error = -1;	/* XXX */
   			else
  				error = p->p_sysent->sv_errtbl[error];
		}
		frame.tf_eax = error;
		frame.tf_eflags |= PSL_C;
		break;
	}

	/*
	 * Traced syscall.  trapsignal() is not MP aware.
	 */
	if ((orig_tf_eflags & PSL_T) && !(orig_tf_eflags & PSL_VM)) {
		if (have_mplock == 0) {
			get_mplock();
			have_mplock = 1;
		}
		frame.tf_eflags &= ~PSL_T;
		trapsignal(p, SIGTRAP, 0);
	}

	/*
	 * Handle reschedule and other end-of-syscall issues
	 */
	have_mplock = userret(p, &frame, sticks, have_mplock);

#ifdef KTRACE
	if (KTRPOINT(p, KTR_SYSRET)) {
		if (have_mplock == 0) {
			get_mplock();
			have_mplock = 1;
		}
		ktrsysret(p->p_tracep, code, error, p->p_retval[0]);
	}
#endif

	/*
	 * This works because errno is findable through the
	 * register set.  If we ever support an emulation where this
	 * is not the case, this code will need to be revisited.
	 */
	STOPEVENT(p, S_SCX, code);

	/*
	 * Release the MP lock if we had to get it
	 */
	if (have_mplock)
		rel_mplock();
}

/*
 * Simplified back end of syscall(), used when returning from fork()
 * directly into user mode.  MP lock is held on entry and should be 
 * held on return.
 */
void
fork_return(p, frame)
	struct proc *p;
	struct trapframe frame;
{
	frame.tf_eax = 0;		/* Child returns zero */
	frame.tf_eflags &= ~PSL_C;	/* success */
	frame.tf_edx = 1;

	userret(p, &frame, 0, 1);
#ifdef KTRACE
	if (KTRPOINT(p, KTR_SYSRET))
		ktrsysret(p->p_tracep, SYS_fork, 0, 0);
#endif
}
