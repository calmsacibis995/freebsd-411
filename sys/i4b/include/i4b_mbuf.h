/*
 * Copyright (c) 1997, 1999 Hellmuth Michaelis. All rights reserved.
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
 *---------------------------------------------------------------------------
 *
 *	i4b - mbuf handling support routines
 *	--------------------------------------
 *
 *	$Id: i4b_mbuf.h,v 1.9 2000/03/07 14:21:18 hm Exp $
 *
 * $FreeBSD: src/sys/i4b/include/i4b_mbuf.h,v 1.6.2.1 2001/08/10 14:08:36 obrien Exp $
 *
 *	last edit-date: [Fri Mar  3 14:30:09 2000]
 *
 *---------------------------------------------------------------------------*/

#ifndef _I4B_MBUF_H_
#define _I4B_MBUF_H_

/* layer 1 / layer 2 comunication: 3rd ph_data_req parameter */
#define MBUF_DONTFREE	0
#define MBUF_FREE	1	

#define IF_QEMPTY(ifq)	((ifq)->ifq_len == 0)

struct mbuf *i4b_Dgetmbuf( int );
void i4b_Dfreembuf( struct mbuf *m );
void i4b_Dcleanifq( struct ifqueue * );

struct mbuf *i4b_Bgetmbuf( int );
void i4b_Bfreembuf( struct mbuf *m );
void i4b_Bcleanifq( struct ifqueue * );

#endif /* _I4B_MBUF_H_ */

/* EOF */
