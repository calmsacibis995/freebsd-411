/*-
 * Copyright (c) 1997, by Steve Passe
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * $FreeBSD: src/sys/i386/isa/apic_ipl.s,v 1.27.2.2 2000/09/30 02:49:35 ps Exp $
 */


	.data
	ALIGN_DATA

/*
 * Routines used by splz_unpend to build an interrupt frame from a
 * trap frame.  The _vec[] routines build the proper frame on the stack,
 * then call one of _Xintr0 thru _XintrNN.
 *
 * used by:
 *   i386/isa/apic_ipl.s (this file):	splz_unpend JUMPs to HWIs.
 *   i386/isa/clock.c:			setup _vec[clock] to point at _vec8254.
 */
	.globl _vec
_vec:
	.long	 vec0,  vec1,  vec2,  vec3,  vec4,  vec5,  vec6,  vec7
	.long	 vec8,  vec9, vec10, vec11, vec12, vec13, vec14, vec15
	.long	vec16, vec17, vec18, vec19, vec20, vec21, vec22, vec23

/*
 * Note:
 *	This is the UP equivilant of _imen.
 *	It is OPAQUE, and must NOT be accessed directly.
 *	It MUST be accessed along with the IO APIC as a 'critical region'.
 *	Accessed by:
 *		INTREN()
 *		INTRDIS()
 *		MAYBE_MASK_IRQ
 *		MAYBE_UNMASK_IRQ
 *		imen_dump()
 */
	.p2align 2				/* MUST be 32bit aligned */
	.globl _apic_imen
_apic_imen:
	.long	HWI_MASK


/*
 * 
 */
	.text
	SUPERALIGN_TEXT

/*
 * splz() -	dispatch pending interrupts after cpl reduced
 *
 * Interrupt priority mechanism
 *	-- soft splXX masks with group mechanism (cpl)
 *	-- h/w masks for currently active or unused interrupts (imen)
 *	-- ipending = active interrupts currently masked by cpl
 */

ENTRY(splz)
	/*
	 * The caller has restored cpl and checked that (ipending & ~cpl)
	 * is nonzero.  However, since ipending can change at any time
	 * (by an interrupt or, with SMP, by another cpu), we have to
	 * repeat the check.  At the moment we must own the MP lock in
	 * the SMP case because the interruput handlers require it.  We
	 * loop until no unmasked pending interrupts remain.  
	 *
	 * No new unmaksed pending interrupts will be added during the
	 * loop because, being unmasked, the interrupt code will be able
	 * to execute the interrupts.
	 *
	 * Interrupts come in two flavors:  Hardware interrupts and software
	 * interrupts.  We have to detect the type of interrupt (based on the
	 * position of the interrupt bit) and call the appropriate dispatch
	 * routine.
	 * 
	 * NOTE: "bsfl %ecx,%ecx" is undefined when %ecx is 0 so we can't
	 * rely on the secondary btrl tests.
	 */
	movl	_cpl,%eax
splz_next:
	/*
	 * We don't need any locking here.  (ipending & ~cpl) cannot grow 
	 * while we're looking at it - any interrupt will shrink it to 0.
	 */
	movl	%eax,%ecx
	notl	%ecx			/* set bit = unmasked level */
	andl	_ipending,%ecx		/* set bit = unmasked pending INT */
	jne	splz_unpend
	ret

	ALIGN_TEXT
splz_unpend:
	bsfl	%ecx,%ecx
	lock
	btrl	%ecx,_ipending
	jnc	splz_next
	cmpl	$NHWI,%ecx
	jae	splz_swi
	/*
	 * We would prefer to call the intr handler directly here but that
	 * doesn't work for badly behaved handlers that want the interrupt
	 * frame.  Also, there's a problem determining the unit number.
	 * We should change the interface so that the unit number is not
	 * determined at config time.
	 *
	 * The vec[] routines build the proper frame on the stack,
	 * then call one of _Xintr0 thru _XintrNN.
	 */
	jmp	*_vec(,%ecx,4)

	ALIGN_TEXT
splz_swi:
	pushl	%eax
	orl	imasks(,%ecx,4),%eax
	movl	%eax,_cpl
	call	*_ihandlers(,%ecx,4)
	popl	%eax
	movl	%eax,_cpl
	jmp	splz_next

/*
 * Fake clock interrupt(s) so that they appear to come from our caller instead
 * of from here, so that system profiling works.
 * XXX do this more generally (for all vectors; look up the C entry point).
 * XXX frame bogusness stops us from just jumping to the C entry point.
 * We have to clear iactive since this is an unpend call, and it will be
 * set from the time of the original INT.
 */

/*
 * The 'generic' vector stubs.
 */

#define BUILD_VEC(irq_num)						\
	ALIGN_TEXT ;							\
__CONCAT(vec,irq_num): ;						\
	popl	%eax ;							\
	pushfl ;							\
	pushl	$KCSEL ;						\
	pushl	%eax ;							\
	cli ;								\
	lock ;					/* MP-safe */		\
	andl	$~IRQ_BIT(irq_num), iactive ;	/* lazy masking */	\
	MEXITCOUNT ;							\
	APIC_ITRACE(apic_itrace_splz, irq_num, APIC_ITRACE_SPLZ) ;	\
	jmp	__CONCAT(_Xintr,irq_num)


	BUILD_VEC(0)
	BUILD_VEC(1)
	BUILD_VEC(2)
	BUILD_VEC(3)
	BUILD_VEC(4)
	BUILD_VEC(5)
	BUILD_VEC(6)
	BUILD_VEC(7)
	BUILD_VEC(8)
	BUILD_VEC(9)
	BUILD_VEC(10)
	BUILD_VEC(11)
	BUILD_VEC(12)
	BUILD_VEC(13)
	BUILD_VEC(14)
	BUILD_VEC(15)
	BUILD_VEC(16)			/* 8 additional INTs in IO APIC */
	BUILD_VEC(17)
	BUILD_VEC(18)
	BUILD_VEC(19)
	BUILD_VEC(20)
	BUILD_VEC(21)
	BUILD_VEC(22)
	BUILD_VEC(23)


/******************************************************************************
 * XXX FIXME: figure out where these belong.
 */

/* this nonsense is to verify that masks ALWAYS have 1 and only 1 bit set */
#define QUALIFY_MASKS_NOT

#ifdef QUALIFY_MASKS
#define QUALIFY_MASK		\
	btrl	%ecx, %eax ;	\
	andl	%eax, %eax ;	\
	jz	1f ;		\
	pushl	$bad_mask ;	\
	call	_panic ;	\
1:

bad_mask:	.asciz	"bad mask"
#else
#define QUALIFY_MASK
#endif

/*
 * (soon to be) MP-safe function to clear ONE INT mask bit.
 * The passed arg is a 32bit u_int MASK.
 * It sets the associated bit in _apic_imen.
 * It sets the mask bit of the associated IO APIC register.
 */
ENTRY(INTREN)
	pushfl				/* save state of EI flag */
	cli				/* prevent recursion */
	IMASK_LOCK			/* enter critical reg */

	movl	8(%esp), %eax		/* mask into %eax */
	bsfl	%eax, %ecx		/* get pin index */
	btrl	%ecx, _apic_imen	/* update _apic_imen */

	QUALIFY_MASK

	shll	$4, %ecx
	movl	CNAME(int_to_apicintpin) + 8(%ecx), %edx
	movl	CNAME(int_to_apicintpin) + 12(%ecx), %ecx
	testl	%edx, %edx
	jz	1f

	movl	%ecx, (%edx)		/* write the target register index */
	movl	16(%edx), %eax		/* read the target register data */
	andl	$~IOART_INTMASK, %eax	/* clear mask bit */
	movl	%eax, 16(%edx)		/* write the APIC register data */
1:	
	IMASK_UNLOCK			/* exit critical reg */
	popfl				/* restore old state of EI flag */
	ret

/*
 * (soon to be) MP-safe function to set ONE INT mask bit.
 * The passed arg is a 32bit u_int MASK.
 * It clears the associated bit in _apic_imen.
 * It clears the mask bit of the associated IO APIC register.
 */
ENTRY(INTRDIS)
	pushfl				/* save state of EI flag */
	cli				/* prevent recursion */
	IMASK_LOCK			/* enter critical reg */

	movl	8(%esp), %eax		/* mask into %eax */
	bsfl	%eax, %ecx		/* get pin index */
	btsl	%ecx, _apic_imen	/* update _apic_imen */

	QUALIFY_MASK

	shll	$4, %ecx
	movl	CNAME(int_to_apicintpin) + 8(%ecx), %edx
	movl	CNAME(int_to_apicintpin) + 12(%ecx), %ecx
	testl	%edx, %edx
	jz	1f

	movl	%ecx, (%edx)		/* write the target register index */
	movl	16(%edx), %eax		/* read the target register data */
	orl	$IOART_INTMASK, %eax	/* set mask bit */
	movl	%eax, 16(%edx)		/* write the APIC register data */
1:	
	IMASK_UNLOCK			/* exit critical reg */
	popfl				/* restore old state of EI flag */
	ret


/******************************************************************************
 *
 */


/*
 * void write_ioapic_mask(int apic, u_int mask); 
 */

#define _INT_MASK	0x00010000
#define _PIN_MASK	0x00ffffff

#define _OLD_ESI	  0(%esp)
#define _OLD_EBX	  4(%esp)
#define _RETADDR	  8(%esp)
#define _APIC		 12(%esp)
#define _MASK		 16(%esp)

	ALIGN_TEXT
write_ioapic_mask:
	pushl %ebx			/* scratch */
	pushl %esi			/* scratch */

	movl	_apic_imen, %ebx
	xorl	_MASK, %ebx		/* %ebx = _apic_imen ^ mask */
	andl	$_PIN_MASK, %ebx	/* %ebx = _apic_imen & 0x00ffffff */
	jz	all_done		/* no change, return */

	movl	_APIC, %esi		/* APIC # */
	movl	_ioapic, %ecx
	movl	(%ecx,%esi,4), %esi	/* %esi holds APIC base address */

next_loop:				/* %ebx = diffs, %esi = APIC base */
	bsfl	%ebx, %ecx		/* %ecx = index if 1st/next set bit */
	jz	all_done

	btrl	%ecx, %ebx		/* clear this bit in diffs */
	leal	16(,%ecx,2), %edx	/* calculate register index */

	movl	%edx, (%esi)		/* write the target register index */
	movl	16(%esi), %eax		/* read the target register data */

	btl	%ecx, _MASK		/* test for mask or unmask */
	jnc	clear			/* bit is clear */
	orl	$_INT_MASK, %eax	/* set mask bit */
	jmp	write
clear:	andl	$~_INT_MASK, %eax	/* clear mask bit */

write:	movl	%eax, 16(%esi)		/* write the APIC register data */

	jmp	next_loop		/* try another pass */

all_done:
	popl	%esi
	popl	%ebx
	ret

#undef _OLD_ESI
#undef _OLD_EBX
#undef _RETADDR
#undef _APIC
#undef _MASK

#undef _PIN_MASK
#undef _INT_MASK

#ifdef oldcode

_INTREN:
	movl _apic_imen, %eax
	notl %eax			/* mask = ~mask */
	andl _apic_imen, %eax		/* %eax = _apic_imen & ~mask */

	pushl %eax			/* new (future) _apic_imen value */
	pushl $0			/* APIC# arg */
	call write_ioapic_mask		/* modify the APIC registers */

	addl $4, %esp			/* remove APIC# arg from stack */
	popl _apic_imen			/* _apic_imen |= mask */
	ret

_INTRDIS:
	movl _apic_imen, %eax
	orl 4(%esp), %eax		/* %eax = _apic_imen | mask */

	pushl %eax			/* new (future) _apic_imen value */
	pushl $0			/* APIC# arg */
	call write_ioapic_mask		/* modify the APIC registers */

	addl $4, %esp			/* remove APIC# arg from stack */
	popl _apic_imen			/* _apic_imen |= mask */
	ret

#endif /* oldcode */


#ifdef ready

/*
 * u_int read_io_apic_mask(int apic); 
 */
	ALIGN_TEXT
read_io_apic_mask:
	ret

/*
 * Set INT mask bit for each bit set in 'mask'.
 * Ignore INT mask bit for all others.
 *
 * void set_io_apic_mask(apic, u_int32_t bits); 
 */
	ALIGN_TEXT
set_io_apic_mask:
	ret

/*
 * void set_ioapic_maskbit(int apic, int bit); 
 */
	ALIGN_TEXT
set_ioapic_maskbit:
	ret

/*
 * Clear INT mask bit for each bit set in 'mask'.
 * Ignore INT mask bit for all others.
 *
 * void clr_io_apic_mask(int apic, u_int32_t bits); 
 */
	ALIGN_TEXT
clr_io_apic_mask:
	ret

/*
 * void clr_ioapic_maskbit(int apic, int bit); 
 */
	ALIGN_TEXT
clr_ioapic_maskbit:
	ret

#endif /** ready */

/******************************************************************************
 * 
 */

/*
 * u_int io_apic_write(int apic, int select);
 */
ENTRY(io_apic_read)
	movl	4(%esp), %ecx		/* APIC # */
	movl	_ioapic, %eax
	movl	(%eax,%ecx,4), %edx	/* APIC base register address */
	movl	8(%esp), %eax		/* target register index */
	movl	%eax, (%edx)		/* write the target register index */
	movl	16(%edx), %eax		/* read the APIC register data */
	ret				/* %eax = register value */

/*
 * void io_apic_write(int apic, int select, int value);
 */
ENTRY(io_apic_write)
	movl	4(%esp), %ecx		/* APIC # */
	movl	_ioapic, %eax
	movl	(%eax,%ecx,4), %edx	/* APIC base register address */
	movl	8(%esp), %eax		/* target register index */
	movl	%eax, (%edx)		/* write the target register index */
	movl	12(%esp), %eax		/* target register value */
	movl	%eax, 16(%edx)		/* write the APIC register data */
	ret				/* %eax = void */

/*
 * Send an EOI to the local APIC.
 */
ENTRY(apic_eoi)
	movl	$0, _lapic+0xb0
	ret
