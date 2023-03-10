/*	$NetBSD: frexp.c,v 1.1 1995/02/10 17:50:22 cgd Exp $	*/
/* $FreeBSD: src/lib/libc/alpha/gen/frexp.c,v 1.1.1.1.6.1 2000/08/21 21:09:28 jhb Exp $ */

/*
 * Copyright (c) 1994, 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
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
 */

#include <sys/types.h>
#include <machine/ieee.h>
#include <math.h>

double
frexp(value, eptr)
	double value;
	int *eptr;
{
	union doub {
                double v;
		struct ieee_double s;
        } u;

	if (value) {
		u.v = value;
		*eptr = u.s.dbl_exp - (DBL_EXP_BIAS - 1);
		u.s.dbl_exp = DBL_EXP_BIAS - 1;
		return(u.v);
	} else {
		*eptr = 0;
		return((double)0);
	}
}
