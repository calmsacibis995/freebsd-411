/*
 * Mach Operating System
 * Copyright (c) 1992, 1991 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 *
 *	from: Mach, Revision 2.2  92/04/04  11:34:26  rpd
 * $FreeBSD: src/sys/i386/boot/biosboot/bios.S,v 1.15 1999/08/28 00:43:11 peter Exp $
 */

/*
  Copyright 1988, 1989, 1990, 1991, 1992 
   by Intel Corporation, Santa Clara, California.

                All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appears in all
copies and that both the copyright notice and this permission notice
appear in supporting documentation, and that the name of Intel
not be used in advertising or publicity pertaining to distribution
of the software without specific, written prior permission.

INTEL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS,
IN NO EVENT SHALL INTEL BE LIABLE FOR ANY SPECIAL, INDIRECT, OR
CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT,
NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

/*
 * Extensions for El Torito CD-ROM booting:
 *
 * Copyright ? 1997 Pluto Technologies International, Inc.  Boulder CO
 * Copyright ? 1997 interface business GmbH, Dresden.
 *      All rights reserved.
 *
 * This code has been written by J?rg Wunsch, Dresden.
 * Direct comments to <joerg_wunsch@interface-business.de>.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

	.file	"bios.s"

#include "asm.h"
	.text

#ifndef CDBOOT

/*
 * biosread(dev, cyl, head, sec, nsec, offset)
 *	Read "nsec" sectors from disk to offset "offset" in boot segment
 * BIOS call "INT 0x13 Function 0x2" to read sectors from disk into memory
 *	Call with	%ah = 0x2
 *			%al = number of sectors
 *			%ch = cylinder
 *			%cl = sector
 *			%dh = head
 *			%dl = drive (0x80 for hard disk, 0x0 for floppy disk)
 *			%es:%bx = segment:offset of buffer
 *	Return:		
 *			%al = 0x0 on success; err code on failure
 */

ENTRY(biosread)
	push	%ebp
	mov	%esp, %ebp

	push	%ebx
	push	%esi
	push	%edi

	movb	0x10(%ebp), %dh
	movw	0x0c(%ebp), %cx
	/* cylinder; the highest 2 bits of cyl is in %cl */
	xchgb	%ch, %cl
	rorb	$2, %cl
	movb	0x14(%ebp), %al
	orb	%al, %cl
	incb	%cl			/* sector; sec starts from 1, not 0 */
	movb	0x8(%ebp), %dl		/* device */
	movl	0x1c(%ebp), %ebx	/* offset */

	/* prot_to_real will set %es to BOOTSEG */
	call	EXT(prot_to_real)	/* enter real mode */
	movb	$0x2, %ah		/* subfunction */
	addr32
	movb	0x18(%ebp), %al		/* number of sectors */

	sti
	int	$0x13
	cli

	/* save return value (actually movw %ax, %bx) */
	mov	%eax, %ebx

	data32
	call	EXT(real_to_prot)	/* back to protected mode */

	xor	%eax, %eax
	movb	%bh, %al		/* return value in %ax */

	pop	%edi
	pop	%esi
	pop	%ebx
	pop	%ebp

	ret

#else /* CDBOOT */


/*
 * int
 * getbootspec(struct specpacket *offset)
 *
 * Read CD-ROM boot specification packet to "offset".
 */
ENTRY(getbootspec)
	push	%ebp
	mov	%esp, %ebp

	push	%esi
	push	%ebx

	movw	0x8(%ebp), %si
	mov	$0x7f, %edx

	/* prot_to_real will set %es to BOOTSEG */
	call	EXT(prot_to_real)	/* enter real mode */
	movw	$0x4b01, %ax		/* (do not) terminate disk emulation */
	movb	$0x7f, %dl		/* any drive */

	sti
	int	$0x13
	cli

	/* save return value (actually movw %ax, %bx) */
	mov	%eax, %ebx

	data32
	call	EXT(real_to_prot)	/* back to protected mode */

	xor	%eax, %eax
	movb	%bh, %al		/* return value in %ax */

	pop	%ebx
	pop	%esi
	pop	%ebp

	ret


/*
 * int
 * biosreadlba(struct daddrpacket *daddr)
 *	Read sectors using the BIOS "read extended" function
 * BIOS call "INT 0x13 Function 0x42" to read sectors from disk into memory
 *	Call with	%ah = 0x42
 *			%dl = drive (0x0 for floppy disk, or emulated CD)
 *			%ds:%si = ptr to disk address packet
 *	Return:
 *			%ah = 0x0 on success; err code on failure
 */

ENTRY(biosreadlba)
	push	%ebp
	mov	%esp, %ebp

	push	%ebx
	push	%esi

	movw	8(%ebp), %si
	movl	$0, %edx		/* emulated CD is always drive 0 */

	/* prot_to_real will set %es to BOOTSEG */
	call	EXT(prot_to_real)	/* enter real mode */
	movw	$0x4200, %ax		/* subfunction */
	movb	$0, %dl

	sti
	int	$0x13
	cli

	/* save return value (actually movw %ax, %bx) */
	mov	%eax, %ebx

	data32
	call	EXT(real_to_prot)	/* back to protected mode */

	xor	%eax, %eax
	movb	%bh, %al		/* return value in %ax */

	pop	%esi
	pop	%ebx
	pop	%ebp

	ret

#endif /* !CDBOOT */

/*
 * putc(ch)
 * BIOS call "INT 10H Function 0Eh" to write character to console
 *	Call with	%ah = 0x0e
 *			%al = character
 *			%bh = page
 *			%bl = foreground color ( graphics modes)
 */


ENTRY(putc)
	push	%ebp
	mov	%esp, %ebp
	push	%ebx
	push	%esi
	push    %edi

	movb	0x8(%ebp), %cl

	call	EXT(prot_to_real)

	data32
	mov	$0x1, %ebx		/* %bh=0, %bl=1 (blue) */
	movb	$0xe, %ah
	movb	%cl, %al
	sti
	int	$0x10			/* display a byte */
	cli

	data32
	call	EXT(real_to_prot)

	pop	%edi
	pop	%esi
	pop	%ebx
	pop	%ebp
	ret


/*
 * getc()
 * BIOS call "INT 16H Function 00H" to read character from keyboard
 *	Call with	%ah = 0x0
 *	Return:		%ah = keyboard scan code
 *			%al = ASCII character
 */

ENTRY(getc)
	push	%ebp
	mov	%esp, %ebp
	push	%ebx			/* save %ebx */
	push    %esi
	push    %edi

	call	EXT(prot_to_real)

	movb	$0x0, %ah
	sti
	int	$0x16
	cli

	movb	%al, %bl		/* real_to_prot uses %eax */

	data32
	call	EXT(real_to_prot)

	xor	%eax, %eax
	movb	%bl, %al

	pop	%edi
	pop	%esi
	pop	%ebx
	pop	%ebp
	ret
/*
 * ischar()
 *	if there is a character pending, return it; otherwise return 0
 * BIOS call "INT 16H Function 01H" to check whether a character is pending
 *	Call with	%ah = 0x1
 *	Return:
 *		If key waiting to be input:
 *			%ah = keyboard scan code
 *			%al = ASCII character
 *			Zero flag = clear
 *		else
 *			Zero flag = set
 */
ENTRY(ischar)
	push	%ebp
	mov	%esp, %ebp
	push	%ebx
	push    %esi
	push    %edi

	call	EXT(prot_to_real)	/* enter real mode */

	xor	%ebx, %ebx
	movb	$0x1, %ah
	sti
	int	$0x16
	cli
	data32
	jz	nochar
	movb	%al, %bl

nochar:
	data32
	call	EXT(real_to_prot)

	xor	%eax, %eax
	movb	%bl, %al

	pop	%edi
	pop     %esi
	pop	%ebx
	pop	%ebp
	ret

/*
 *
 * get_diskinfo():  return a word that represents the
 *	max number of sectors and heads and drives for this device
 *
 */

ENTRY(get_diskinfo)
	push	%ebp
	mov	%esp, %ebp
	push	%ebx
	push	%esi
	push	%edi

	movb	0x8(%ebp), %dl		/* diskinfo(drive #) */
	call	EXT(prot_to_real)	/* enter real mode */

	movb	$0x8, %ah		/* ask for disk info */

	sti
	int	$0x13
	cli

	jnc	ok
	/*
	 * Urk.  Call failed.  It is not supported for floppies by old BIOS's.
	 * Guess it's a 15-sector floppy.
	 */
	subb	%ah, %ah		/* %ax = 0 */
	movb	%al, %al
	movb	%ah, %bh		/* %bh = 0 */
	movb	$2, %bl			/* %bl	bits 0-3 = drive type,
						bit    2 = 1.2M */
	movb	$79, %ch		/* max track */
	movb	$15, %cl		/* max sector */
	movb	$1, %dh			/* max head */
	movb	$1, %dl			/* # floppy drives installed */
	/* es:di = parameter table */
	/* carry = 0 */
ok:

	data32
	call	EXT(real_to_prot)	/* back to protected mode */

	/* 
	 * form a longword representing all this gunk:
	 *       6 bit zero
	 *	10 bit max cylinder (0 based)
	 *	 8 bit max head (0 based)
	 *	 2 bit zero
	 *	 6 bit max sector (1 based) = # sectors
	 */
	movb	%cl, %al		/* Upper two bits of cylinder count */
	andl	$192,%eax	
	leal	0(,%eax,4),%eax		/* << 2 */
	movb	%ch, %al		/* Lower 8 bits */
	sall	$16,%eax		/* << 16 */
	movb	%dh, %ah		/* max head */
	andb	$0x3f, %cl		/* mask of cylinder gunk */
	movb	%cl, %al		/* max sector (and # sectors) */

	pop	%edi
	pop	%esi
	pop	%ebx
	pop	%ebp
	ret

/*
 *
 * memsize(i) :  return the memory size in KB. i == 0 for conventional memory,
 *		i == 1 for extended memory
 *	BIOS call "INT 12H" to get conventional memory size
 *	BIOS call "INT 15H, AH=88H" to get extended memory size
 *		Both have the return value in AX.
 *
 */

ENTRY(memsize)
	push	%ebp
	mov	%esp, %ebp
	push	%ebx
	push    %esi
	push    %edi

	mov	8(%ebp), %ebx

	call	EXT(prot_to_real)	/* enter real mode */

	cmpb	$0x1, %bl
	data32
	je	xext
	
	sti
	int	$0x12
	cli
	data32
	jmp	xdone

xext:	movb	$0x88, %ah
	sti
	int	$0x15
	cli

xdone:
	pushl	$0			/* actually pushw $0 */
	pushl	%eax			/* actually pushw %ax */

	data32
	call	EXT(real_to_prot)

	pop	%eax
	pop     %edi
	pop	%esi
	pop	%ebx
	pop	%ebp
	ret
