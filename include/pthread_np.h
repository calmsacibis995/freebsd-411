/*
 * Copyright (c) 1996-98 John Birrell <jb@cimlogic.com.au>.
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
 *	This product includes software developed by John Birrell.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
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
 * $FreeBSD: src/include/pthread_np.h,v 1.7.2.4 2003/01/10 15:41:17 fjoe Exp $
 */
#ifndef _PTHREAD_NP_H_
#define _PTHREAD_NP_H_

/*
 * Non-POSIX type definitions:
 */
typedef void	(*pthread_switch_routine_t) __P((pthread_t, pthread_t));

/*
 * Non-POSIX thread function prototype definitions:
 */
__BEGIN_DECLS
int pthread_attr_setcreatesuspend_np __P((pthread_attr_t *));
int pthread_attr_get_np __P((pthread_t, pthread_attr_t *));
int pthread_main_np __P((void));
int pthread_multi_np __P((void));
int pthread_mutexattr_getkind_np __P((pthread_mutexattr_t));
int pthread_mutexattr_setkind_np __P((pthread_mutexattr_t *, int));
void pthread_resume_all_np __P((void));
int pthread_resume_np __P((pthread_t));
void pthread_set_name_np __P((pthread_t, const char *));
int pthread_single_np __P((void));
void pthread_suspend_all_np __P((void));
int pthread_suspend_np __P((pthread_t));
int pthread_switch_add_np __P((pthread_switch_routine_t));
int pthread_switch_delete_np __P((pthread_switch_routine_t));
__END_DECLS

#endif
