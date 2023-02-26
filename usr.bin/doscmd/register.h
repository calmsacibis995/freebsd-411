/*
** Copyright (c) 1996
**	Michael Smith.  All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
**
** THIS SOFTWARE IS PROVIDED BY Michael Smith ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED.  IN NO EVENT SHALL Michael Smith BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
** OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
** HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
**
** $FreeBSD: src/usr.bin/doscmd/register.h,v 1.4 1999/10/08 09:02:38 marcel Exp $
*/

/******************************************************************************
** Abstractions to hide register access methods across different platforms.
**
*/

#ifndef _MACHINE_VM86_H_

/* standard register representation */
typedef union 
{
    u_long	r_ex;
    struct 
    {
	u_short	r_x;
	u_short	:16;
    } r_w;
    struct
    {
	u_char	r_l;
	u_char	r_h;
	u_short :16;
    } r_b;
} reg86_t;

#endif

#ifdef __FreeBSD__

/* layout must match definition of struct sigcontext in <machine/signal.h> */

typedef struct
{
    int		onstack;
    reg86_t	gs;
    reg86_t	fs;
    reg86_t	es;
    reg86_t	ds;
    reg86_t	edi;
    reg86_t	esi;
    reg86_t	ebp;
    reg86_t	isp;
    reg86_t	ebx;
    reg86_t	edx;
    reg86_t	ecx;
    reg86_t	eax;
    int		pad[2];
    reg86_t	eip;
    reg86_t	cs;
    reg86_t	efl;
    reg86_t	esp;
    reg86_t	ss;
} registers_t;

typedef union 
{
    mcontext_t	sc;
    registers_t	r;
} regcontext_t;

/* 
** passed around as a reference to the registers.  This must be in
** scope for the following register macros to work.
*/

/* register shorthands */
#define R_ESP		(REGS->r.esp.r_ex)
#define R_SP		(REGS->r.esp.r_w.r_x)
#define R_EBP		(REGS->r.ebp.r_ex)
#define R_BP		(REGS->r.ebp.r_w.r_x)
#define R_ISP		(REGS->r.isp.r_ex)
#define R_EIP		(REGS->r.eip.r_ex)
#define R_IP		(REGS->r.eip.r_w.r_x)
#define R_EFLAGS	(REGS->r.efl.r_ex)
#define R_FLAGS		(REGS->r.efl.r_w.r_x)
#define R_EES		(REGS->r.es.r_ex)
#define R_ES		(REGS->r.es.r_w.r_x)
#define R_EDS		(REGS->r.ds.r_ex)
#define R_DS		(REGS->r.ds.r_w.r_x)
#define R_ECS		(REGS->r.cs.r_ex)
#define R_CS		(REGS->r.cs.r_w.r_x)
#define R_ESS		(REGS->r.ss.r_ex)
#define R_SS		(REGS->r.ss.r_w.r_x)
#define R_EDI		(REGS->r.edi.r_ex)
#define R_DI		(REGS->r.edi.r_w.r_x)
#define R_ESI		(REGS->r.esi.r_ex)
#define R_SI		(REGS->r.esi.r_w.r_x)
#define R_EBX		(REGS->r.ebx.r_ex)
#define R_BX		(REGS->r.ebx.r_w.r_x)
#define R_BL		(REGS->r.ebx.r_b.r_l)
#define R_BH		(REGS->r.ebx.r_b.r_h)
#define R_EDX		(REGS->r.edx.r_ex)
#define R_DX		(REGS->r.edx.r_w.r_x)
#define R_DL		(REGS->r.edx.r_b.r_l)
#define R_DH		(REGS->r.edx.r_b.r_h)
#define R_ECX		(REGS->r.ecx.r_ex)
#define R_CX		(REGS->r.ecx.r_w.r_x)
#define R_CL		(REGS->r.ecx.r_b.r_l)
#define R_CH		(REGS->r.ecx.r_b.r_h)
#define R_EAX		(REGS->r.eax.r_ex)
#define R_AX		(REGS->r.eax.r_w.r_x)
#define R_AL		(REGS->r.eax.r_b.r_l)
#define R_AH		(REGS->r.eax.r_b.r_h)
#define R_EGS		(REGS->r.gs.r_ex)
#define R_GS		(REGS->r.gs.r_w.r_x)
#define R_EFS		(REGS->r.fs.r_ex)
#define R_FS		(REGS->r.fs.r_w.r_x)

#endif

#ifdef __bsdi__
#endif

#ifdef __NetBSD__
#endif

/*
** register manipulation macros 
*/

#define	PUTVEC(s, o, x)	((s) = ((x) >> 16), (o) = (x) & 0xffff)
#define MAKEVEC(s, o)		(((s) << 16) + (o))

#define PUTPTR(s, o, x)	(((s) = ((x) & 0xf0000) >> 4), (o) = (x) & 0xffff)
#define MAKEPTR(s, o)		(((s) << 4) + (o))

#define VECPTR(x)		MAKEPTR((x) >> 16, (x) & 0xffff)

#define REGISTERS		regcontext_t *REGS

inline static void
PUSH(u_short x, REGISTERS)
{
    R_SP -= 2;
    *(u_short *)MAKEPTR(R_SS, R_SP) = (x);
}

inline static u_short
POP(REGISTERS)
{
    u_short	x;
    
    x = *(u_short *)MAKEPTR(R_SS, R_SP);
    R_SP += 2;
    return(x);
}

# ifndef PSL_ALLCC	/* Grr, FreeBSD doesn't have this */
# define PSL_ALLCC        (PSL_C|PSL_PF|PSL_AF|PSL_Z|PSL_N)
# endif
