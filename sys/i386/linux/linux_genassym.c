/* $FreeBSD: src/sys/i386/linux/linux_genassym.c,v 1.13.2.3 2001/11/05 19:08:23 marcel Exp $ */

#include <sys/param.h>
#include <sys/assym.h>

#include <i386/linux/linux.h>

ASSYM(LINUX_SIGF_HANDLER, offsetof(struct l_sigframe, sf_handler));
ASSYM(LINUX_SIGF_SC, offsetof(struct l_sigframe, sf_sc));
ASSYM(LINUX_SC_GS, offsetof(struct l_sigcontext, sc_gs));
ASSYM(LINUX_SC_EFLAGS, offsetof(struct l_sigcontext, sc_eflags));
ASSYM(LINUX_RT_SIGF_HANDLER, offsetof(struct l_rt_sigframe, sf_handler));
ASSYM(LINUX_RT_SIGF_UC, offsetof(struct l_rt_sigframe, sf_sc));
