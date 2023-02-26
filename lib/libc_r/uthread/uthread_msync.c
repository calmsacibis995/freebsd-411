/*
 * David Leonard <d@openbsd.org>, 1999. Public Domain.
 *
 * $OpenBSD: uthread_msync.c,v 1.2 1999/06/09 07:16:17 d Exp $
 *
 * $FreeBSD: src/lib/libc_r/uthread/uthread_msync.c,v 1.3.2.2 2002/10/22 14:44:03 fjoe Exp $
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <pthread.h>
#include "pthread_private.h"

int
_msync(void *addr, size_t len, int flags)
{
	int ret;

	ret = __sys_msync(addr, len, flags);

	return (ret);
}

int
msync(void *addr, size_t len, int flags)
{
	int	ret;

	/*
	 * XXX This is quite pointless unless we know how to get the
	 * file descriptor associated with the memory, and lock it for
	 * write. The only real use of this wrapper is to guarantee
	 * a cancellation point, as per the standard. sigh.
	 */
	_thread_enter_cancellation_point();
	ret = _msync(addr, len, flags);
	_thread_leave_cancellation_point();

	return ret;
}
