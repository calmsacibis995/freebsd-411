/*-
 * Copyright (c) 1998 Doug Rabson
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
 * $FreeBSD: src/sys/alpha/include/atomic.h,v 1.2.2.1 2004/05/03 20:32:32 jdp Exp $
 */

#ifndef _MACHINE_ATOMIC_H_
#define _MACHINE_ATOMIC_H_

/*
 * Various simple arithmetic on memory which is atomic in the presence
 * of interrupts and SMP safe.
 */

void atomic_set_8(u_int8_t *, u_int8_t);
void atomic_clear_8(u_int8_t *, u_int8_t);
void atomic_add_8(u_int8_t *, u_int8_t);
void atomic_subtract_8(u_int8_t *, u_int8_t);

void atomic_set_16(u_int16_t *, u_int16_t);
void atomic_clear_16(u_int16_t *, u_int16_t);
void atomic_add_16(u_int16_t *, u_int16_t);
void atomic_subtract_16(u_int16_t *, u_int16_t);

void atomic_set_32(u_int32_t *, u_int32_t);
void atomic_clear_32(u_int32_t *, u_int32_t);
void atomic_add_32(u_int32_t *, u_int32_t);
void atomic_subtract_32(u_int32_t *, u_int32_t);

void atomic_set_64(u_int64_t *, u_int64_t);
void atomic_clear_64(u_int64_t *, u_int64_t);
void atomic_add_64(u_int64_t *, u_int64_t);
void atomic_subtract_64(u_int64_t *, u_int64_t);

static __inline u_int32_t
atomic_readandclear_32(volatile u_int32_t *addr)
{
	u_int32_t result,temp;

	__asm __volatile (
		"wmb\n"			/* ensure pending writes have drained */
		"1:\tldl_l %0,%3\n\t"	/* load current value, asserting lock */
		"ldiq %1,0\n\t"		/* value to store */
		"stl_c %1,%2\n\t"	/* attempt to store */
		"beq %1,2f\n\t"		/* if the store failed, spin */
		"br 3f\n"		/* it worked, exit */
		"2:\tbr 1b\n"		/* *addr not updated, loop */
		"3:\tmb\n"		/* it worked */
		: "=&r"(result), "=&r"(temp), "=m" (*addr)
		: "m"(*addr)
		: "memory");

	return result;
}

static __inline u_int64_t
atomic_readandclear_64(volatile u_int64_t *addr)
{
	u_int64_t result,temp;

	__asm __volatile (
		"wmb\n"			/* ensure pending writes have drained */
		"1:\tldq_l %0,%3\n\t"	/* load current value, asserting lock */
		"ldiq %1,0\n\t"		/* value to store */
		"stq_c %1,%2\n\t"	/* attempt to store */
		"beq %1,2f\n\t"		/* if the store failed, spin */
		"br 3f\n"		/* it worked, exit */
		"2:\tbr 1b\n"		/* *addr not updated, loop */
		"3:\tmb\n"		/* it worked */
		: "=&r"(result), "=&r"(temp), "=m" (*addr)
		: "m"(*addr)
		: "memory");

	return result;
}

#define atomic_set_char		atomic_set_8
#define atomic_clear_char	atomic_clear_8
#define atomic_add_char		atomic_add_8
#define atomic_subtract_char	atomic_subtract_8

#define atomic_set_short	atomic_set_16
#define atomic_clear_short	atomic_clear_16
#define atomic_add_short	atomic_add_16
#define atomic_subtract_short	atomic_subtract_16

#define atomic_set_int		atomic_set_32
#define atomic_clear_int	atomic_clear_32
#define atomic_add_int		atomic_add_32
#define atomic_subtract_int	atomic_subtract_32
#define atomic_readandclear_int	atomic_readandclear_32

#define atomic_set_long		atomic_set_64
#define atomic_clear_long	atomic_clear_64
#define atomic_add_long		atomic_add_64
#define atomic_subtract_long	atomic_subtract_64
#define atomic_readandclear_long	atomic_readandclear_64

#endif /* ! _MACHINE_ATOMIC_H_ */
