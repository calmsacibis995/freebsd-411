/*
 * Copyright (c) 1995 Steven Wallace
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
 *      This product includes software developed by Steven Wallace.
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
 * $FreeBSD: src/sys/i386/ibcs2/ibcs2_sysvec.c,v 1.17.2.1 2001/02/22 05:15:01 marcel Exp $
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysent.h>
#include <sys/signalvar.h>
#include <sys/proc.h>

#include <i386/ibcs2/ibcs2_syscall.h>
#include <i386/ibcs2/ibcs2_signal.h>

extern int bsd_to_ibcs2_errno[];
extern struct sysent ibcs2_sysent[IBCS2_SYS_MAXSYSCALL];
extern int szsigcode;
extern char sigcode[];

struct sysentvec ibcs2_svr3_sysvec = {
        sizeof (ibcs2_sysent) / sizeof (ibcs2_sysent[0]),
        ibcs2_sysent,
        0xFF,
        IBCS2_SIGTBLSZ,
        bsd_to_ibcs2_sig,
        ELAST + 1,
        bsd_to_ibcs2_errno,
	0,              /* trap-to-signal translation function */
	0,		/* fixup */
	sendsig,
	sigcode,	/* use generic trampoline */
	&szsigcode,	/* use generic trampoline size */
	0,		/* prepsyscall */
	"IBCS2 COFF",
	NULL,		/* we don't have a COFF coredump function */
	NULL,
	IBCS2_MINSIGSTKSZ
};

/*
 * Create an "ibcs2" module that does nothing but allow checking for
 * the presence of the subsystem.
 */
static int
ibcs2_modevent(module_t mod, int type, void *unused)
{
	struct proc *p = NULL;

	switch(type) {
	case MOD_UNLOAD:
		/* if this was an ELF module we'd use elf_brand_inuse()... */
		for (p = allproc.lh_first; p != 0; p = p->p_list.le_next) {
			if (p->p_sysent == &ibcs2_svr3_sysvec)
				return EBUSY;
		}
	default:
	        /* do not care */
	}
	return 0;
}
static moduledata_t ibcs2_mod = {
	"ibcs2",
	ibcs2_modevent,
	0
};
DECLARE_MODULE(ibcs2, ibcs2_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
