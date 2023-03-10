/*	$NetBSD: fpsetsticky.c,v 1.1 1995/04/29 05:11:04 cgd Exp $	*/
/* $FreeBSD: src/lib/libc/alpha/gen/fpsetsticky.c,v 1.2.6.1 2000/08/17 08:08:15 jhb Exp $ */

/*
 * Copyright (c) 1995 Christopher G. Demetriou
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
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 */

#include <sys/types.h>
#include <ieeefp.h>
#include <machine/fpu.h>

fp_except_t
fpsetsticky(sticky)
	fp_except_t sticky;
{
	double fpcrval;
	u_int64_t old,new ;

	GET_FPCR(fpcrval);
	old = *(u_int64_t *)&fpcrval;
	new = old & ~ (IEEE_STATUS_MASK << IEEE_STATUS_TO_FPCR_SHIFT);
	new |= ((sticky << IEEE_STATUS_TO_EXCSUM_SHIFT) & IEEE_STATUS_MASK)
					<< IEEE_STATUS_TO_FPCR_SHIFT;
	*(u_int64_t *)&fpcrval = new;
	SET_FPCR(fpcrval);

	return (((old >> IEEE_STATUS_TO_FPCR_SHIFT) & IEEE_STATUS_MASK)
					>> IEEE_STATUS_TO_EXCSUM_SHIFT);
}
