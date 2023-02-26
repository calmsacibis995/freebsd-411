/*
 * Copyright (c) 1982, 1986, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
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
 *	@(#)sys_socket.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/kern/sys_socket.c,v 1.28.2.2 2001/02/26 04:23:16 jlemon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/file.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/filio.h>			/* XXX */
#include <sys/sockio.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/filedesc.h>
#include <sys/ucred.h>

#include <net/if.h>
#include <net/route.h>

struct	fileops socketops = {
	soo_read, soo_write, soo_ioctl, soo_poll, sokqfilter,
	soo_stat, soo_close
};

/* ARGSUSED */
int
soo_read(fp, uio, cred, flags, p)
	struct file *fp;
	struct uio *uio;
	struct ucred *cred;
	struct proc *p;
	int flags;
{
	struct socket *so = (struct socket *)fp->f_data;
	return so->so_proto->pr_usrreqs->pru_soreceive(so, 0, uio, 0, 0, 0);
}

/* ARGSUSED */
int
soo_write(fp, uio, cred, flags, p)
	struct file *fp;
	struct uio *uio;
	struct ucred *cred;
	struct proc *p;
	int flags;
{
	struct socket *so = (struct socket *)fp->f_data;
	return so->so_proto->pr_usrreqs->pru_sosend(so, 0, uio, 0, 0, 0,
						    uio->uio_procp);
}

int
soo_ioctl(fp, cmd, data, p)
	struct file *fp;
	u_long cmd;
	register caddr_t data;
	struct proc *p;
{
	register struct socket *so = (struct socket *)fp->f_data;

	switch (cmd) {

	case FIONBIO:
		if (*(int *)data)
			so->so_state |= SS_NBIO;
		else
			so->so_state &= ~SS_NBIO;
		return (0);

	case FIOASYNC:
		if (*(int *)data) {
			so->so_state |= SS_ASYNC;
			so->so_rcv.sb_flags |= SB_ASYNC;
			so->so_snd.sb_flags |= SB_ASYNC;
		} else {
			so->so_state &= ~SS_ASYNC;
			so->so_rcv.sb_flags &= ~SB_ASYNC;
			so->so_snd.sb_flags &= ~SB_ASYNC;
		}
		return (0);

	case FIONREAD:
		*(int *)data = so->so_rcv.sb_cc;
		return (0);

	case FIOSETOWN:
		return (fsetown(*(int *)data, &so->so_sigio));

	case FIOGETOWN:
		*(int *)data = fgetown(so->so_sigio);
		return (0);

	case SIOCSPGRP:
		return (fsetown(-(*(int *)data), &so->so_sigio));

	case SIOCGPGRP:
		*(int *)data = -fgetown(so->so_sigio);
		return (0);

	case SIOCATMARK:
		*(int *)data = (so->so_state&SS_RCVATMARK) != 0;
		return (0);
	}
	/*
	 * Interface/routing/protocol specific ioctls:
	 * interface and routing ioctls should have a
	 * different entry since a socket's unnecessary
	 */
	if (IOCGROUP(cmd) == 'i')
		return (ifioctl(so, cmd, data, p));
	if (IOCGROUP(cmd) == 'r')
		return (rtioctl(cmd, data, p));
	return ((*so->so_proto->pr_usrreqs->pru_control)(so, cmd, data, 0, p));
}

int
soo_poll(fp, events, cred, p)
	struct file *fp;
	int events;
	struct ucred *cred;
	struct proc *p;
{
	struct socket *so = (struct socket *)fp->f_data;
	return so->so_proto->pr_usrreqs->pru_sopoll(so, events, cred, p);
}

int
soo_stat(fp, ub, p)
	struct file *fp;
	struct stat *ub;
	struct proc *p;
{
	struct socket *so = (struct socket *)fp->f_data;

	bzero((caddr_t)ub, sizeof (*ub));
	ub->st_mode = S_IFSOCK;
	/*
	 * If SS_CANTRCVMORE is set, but there's still data left in the
	 * receive buffer, the socket is still readable.
	 */
	if ((so->so_state & SS_CANTRCVMORE) == 0 ||
	    so->so_rcv.sb_cc != 0)
		ub->st_mode |= S_IRUSR | S_IRGRP | S_IROTH;
	if ((so->so_state & SS_CANTSENDMORE) == 0)
		ub->st_mode |= S_IWUSR | S_IWGRP | S_IWOTH;
	ub->st_size = so->so_rcv.sb_cc;
	ub->st_uid = so->so_cred->cr_uid;
	ub->st_gid = so->so_cred->cr_gid;
	return ((*so->so_proto->pr_usrreqs->pru_sense)(so, ub));
}

/* ARGSUSED */
int
soo_close(fp, p)
	struct file *fp;
	struct proc *p;
{
	int error = 0;

	fp->f_ops = &badfileops;
	if (fp->f_data)
		error = soclose((struct socket *)fp->f_data);
	fp->f_data = 0;
	return (error);
}
