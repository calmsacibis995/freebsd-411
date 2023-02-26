/*
 * Copyright (c) 1982, 1986, 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * sendfile(2) and related extensions:
 * Copyright (c) 1998, David Greenman. All rights reserved. 
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
 *	@(#)uipc_syscalls.c	8.4 (Berkeley) 2/21/94
 * $FreeBSD: src/sys/kern/uipc_syscalls.c,v 1.65.2.21 2004/08/13 14:51:45 ume Exp $
 */

#include "opt_compat.h"
#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysproto.h>
#include <sys/malloc.h>
#include <sys/filedesc.h>
#include <sys/event.h>
#include <sys/proc.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filio.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/signalvar.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/mount.h>
#ifdef KTRACE
#include <sys/ktrace.h>
#endif
#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>

static void sf_buf_init(void *arg);
SYSINIT(sock_sf, SI_SUB_MBUF, SI_ORDER_ANY, sf_buf_init, NULL)

static int sendit __P((struct proc *p, int s, struct msghdr *mp, int flags));
static int recvit __P((struct proc *p, int s, struct msghdr *mp,
		       caddr_t namelenp));
  
static int accept1 __P((struct proc *p, struct accept_args *uap, int compat));
static int do_sendfile __P((struct proc *p, struct sendfile_args *uap,
			    int compat));
static int getsockname1 __P((struct proc *p, struct getsockname_args *uap,
			     int compat));
static int getpeername1 __P((struct proc *p, struct getpeername_args *uap,
			     int compat));

static SLIST_HEAD(, sf_buf) sf_freelist;
static vm_offset_t sf_base;
static struct sf_buf *sf_bufs;
static int sf_buf_alloc_want;

/*
 * System call interface to the socket abstraction.
 */
#if defined(COMPAT_43) || defined(COMPAT_SUNOS)
#define COMPAT_OLDSOCK
#endif

extern	struct fileops socketops;

int
socket(p, uap)
	struct proc *p;
	register struct socket_args /* {
		int	domain;
		int	type;
		int	protocol;
	} */ *uap;
{
	struct filedesc *fdp = p->p_fd;
	struct socket *so;
	struct file *fp;
	int fd, error;

	error = falloc(p, &fp, &fd);
	if (error)
		return (error);
	fhold(fp);
	error = socreate(uap->domain, &so, uap->type, uap->protocol, p);
	if (error) {
		if (fdp->fd_ofiles[fd] == fp) {
			fdp->fd_ofiles[fd] = NULL;
			fdrop(fp, p);
		}
	} else {
		fp->f_data = (caddr_t)so;
		fp->f_flag = FREAD|FWRITE;
		fp->f_ops = &socketops;
		fp->f_type = DTYPE_SOCKET;
		p->p_retval[0] = fd;
	}
	fdrop(fp, p);
	return (error);
}

/* ARGSUSED */
int
bind(p, uap)
	struct proc *p;
	register struct bind_args /* {
		int	s;
		caddr_t	name;
		int	namelen;
	} */ *uap;
{
	struct file *fp;
	struct sockaddr *sa;
	int error;

	error = holdsock(p->p_fd, uap->s, &fp);
	if (error)
		return (error);
	error = getsockaddr(&sa, uap->name, uap->namelen);
	if (error) {
		fdrop(fp, p);
		return (error);
	}
	error = sobind((struct socket *)fp->f_data, sa, p);
	FREE(sa, M_SONAME);
	fdrop(fp, p);
	return (error);
}

/* ARGSUSED */
int
listen(p, uap)
	struct proc *p;
	register struct listen_args /* {
		int	s;
		int	backlog;
	} */ *uap;
{
	struct file *fp;
	int error;

	error = holdsock(p->p_fd, uap->s, &fp);
	if (error)
		return (error);
	error = solisten((struct socket *)fp->f_data, uap->backlog, p);
	fdrop(fp, p);
	return(error);
}

static int
accept1(p, uap, compat)
	struct proc *p;
	register struct accept_args /* {
		int	s;
		caddr_t	name;
		int	*anamelen;
	} */ *uap;
	int compat;
{
	struct filedesc *fdp = p->p_fd;
	struct file *lfp = NULL;
	struct file *nfp = NULL;
	struct sockaddr *sa;
	int namelen, error, s;
	struct socket *head, *so;
	int fd;
	u_int fflag;		/* type must match fp->f_flag */
	int tmp;

	if (uap->name) {
		error = copyin((caddr_t)uap->anamelen, (caddr_t)&namelen,
			sizeof (namelen));
		if(error)
			return (error);
		if (namelen < 0)
			return (EINVAL);
	}
	error = holdsock(fdp, uap->s, &lfp);
	if (error)
		return (error);
	s = splnet();
	head = (struct socket *)lfp->f_data;
	if ((head->so_options & SO_ACCEPTCONN) == 0) {
		splx(s);
		error = EINVAL;
		goto done;
	}
	while (TAILQ_EMPTY(&head->so_comp) && head->so_error == 0) {
		if (head->so_state & SS_CANTRCVMORE) {
			head->so_error = ECONNABORTED;
			break;
		}
		if ((head->so_state & SS_NBIO) != 0) {
			head->so_error = EWOULDBLOCK;
			break;
		}
		error = tsleep((caddr_t)&head->so_timeo, PSOCK | PCATCH,
		    "accept", 0);
		if (error) {
			splx(s);
			goto done;
		}
	}
	if (head->so_error) {
		error = head->so_error;
		head->so_error = 0;
		splx(s);
		goto done;
	}

	/*
	 * At this point we know that there is at least one connection
	 * ready to be accepted. Remove it from the queue prior to
	 * allocating the file descriptor for it since falloc() may
	 * block allowing another process to accept the connection
	 * instead.
	 */
	so = TAILQ_FIRST(&head->so_comp);
	TAILQ_REMOVE(&head->so_comp, so, so_list);
	head->so_qlen--;

	fflag = lfp->f_flag;
	error = falloc(p, &nfp, &fd);
	if (error) {
		/*
		 * Probably ran out of file descriptors. Put the
		 * unaccepted connection back onto the queue and
		 * do another wakeup so some other process might
		 * have a chance at it.
		 */
		TAILQ_INSERT_HEAD(&head->so_comp, so, so_list);
		head->so_qlen++;
		wakeup_one(&head->so_timeo);
		splx(s);
		goto done;
	}
	fhold(nfp);
	p->p_retval[0] = fd;

	/* connection has been removed from the listen queue */
	KNOTE(&head->so_rcv.sb_sel.si_note, 0);

	so->so_state &= ~SS_COMP;
	so->so_head = NULL;
	if (head->so_sigio != NULL)
		fsetown(fgetown(head->so_sigio), &so->so_sigio);

	nfp->f_data = (caddr_t)so;
	nfp->f_flag = fflag;
	nfp->f_ops = &socketops;
	nfp->f_type = DTYPE_SOCKET;
	/* Sync socket nonblocking/async state with file flags */
	tmp = fflag & FNONBLOCK;
	(void) fo_ioctl(nfp, FIONBIO, (caddr_t)&tmp, p);
	tmp = fflag & FASYNC;
	(void) fo_ioctl(nfp, FIOASYNC, (caddr_t)&tmp, p);
	sa = 0;
	error = soaccept(so, &sa);
	if (error) {
		/*
		 * return a namelen of zero for older code which might
	 	 * ignore the return value from accept.
		 */	
		if (uap->name != NULL) {
			namelen = 0;
			(void) copyout((caddr_t)&namelen,
			    (caddr_t)uap->anamelen, sizeof(*uap->anamelen));
		}
		goto noconnection;
	}
	if (sa == NULL) {
		namelen = 0;
		if (uap->name)
			goto gotnoname;
		splx(s);
		error = 0;
		goto done;
	}
	if (uap->name) {
		/* check sa_len before it is destroyed */
		if (namelen > sa->sa_len)
			namelen = sa->sa_len;
#ifdef COMPAT_OLDSOCK
		if (compat)
			((struct osockaddr *)sa)->sa_family =
			    sa->sa_family;
#endif
		error = copyout(sa, (caddr_t)uap->name, (u_int)namelen);
		if (!error)
gotnoname:
			error = copyout((caddr_t)&namelen,
			    (caddr_t)uap->anamelen, sizeof (*uap->anamelen));
	}
noconnection:
	if (sa)
		FREE(sa, M_SONAME);

	/*
	 * close the new descriptor, assuming someone hasn't ripped it
	 * out from under us.
	 */
	if (error) {
		if (fdp->fd_ofiles[fd] == nfp) {
			fdp->fd_ofiles[fd] = NULL;
			fdrop(nfp, p);
		}
	}
	splx(s);

	/*
	 * Release explicitly held references before returning.
	 */
done:
	if (nfp != NULL)
		fdrop(nfp, p);
	fdrop(lfp, p);
	return (error);
}

int
accept(p, uap)
	struct proc *p;
	struct accept_args *uap;
{

	return (accept1(p, uap, 0));
}

#ifdef COMPAT_OLDSOCK
int
oaccept(p, uap)
	struct proc *p;
	struct accept_args *uap;
{

	return (accept1(p, uap, 1));
}
#endif /* COMPAT_OLDSOCK */

/* ARGSUSED */
int
connect(p, uap)
	struct proc *p;
	register struct connect_args /* {
		int	s;
		caddr_t	name;
		int	namelen;
	} */ *uap;
{
	struct file *fp;
	register struct socket *so;
	struct sockaddr *sa;
	int error, s;

	error = holdsock(p->p_fd, uap->s, &fp);
	if (error)
		return (error);
	so = (struct socket *)fp->f_data;
	if ((so->so_state & SS_NBIO) && (so->so_state & SS_ISCONNECTING)) {
		error = EALREADY;
		goto done;
	}
	error = getsockaddr(&sa, uap->name, uap->namelen);
	if (error)
		goto done;
	error = soconnect(so, sa, p);
	if (error)
		goto bad;
	if ((so->so_state & SS_NBIO) && (so->so_state & SS_ISCONNECTING)) {
		FREE(sa, M_SONAME);
		error = EINPROGRESS;
		goto done;
	}
	s = splnet();
	while ((so->so_state & SS_ISCONNECTING) && so->so_error == 0) {
		error = tsleep((caddr_t)&so->so_timeo, PSOCK | PCATCH,
		    "connec", 0);
		if (error)
			break;
	}
	if (error == 0) {
		error = so->so_error;
		so->so_error = 0;
	}
	splx(s);
bad:
	so->so_state &= ~SS_ISCONNECTING;
	FREE(sa, M_SONAME);
	if (error == ERESTART)
		error = EINTR;
done:
	fdrop(fp, p);
	return (error);
}

int
socketpair(p, uap)
	struct proc *p;
	register struct socketpair_args /* {
		int	domain;
		int	type;
		int	protocol;
		int	*rsv;
	} */ *uap;
{
	register struct filedesc *fdp = p->p_fd;
	struct file *fp1, *fp2;
	struct socket *so1, *so2;
	int fd, error, sv[2];

	error = socreate(uap->domain, &so1, uap->type, uap->protocol, p);
	if (error)
		return (error);
	error = socreate(uap->domain, &so2, uap->type, uap->protocol, p);
	if (error)
		goto free1;
	error = falloc(p, &fp1, &fd);
	if (error)
		goto free2;
	fhold(fp1);
	sv[0] = fd;
	fp1->f_data = (caddr_t)so1;
	error = falloc(p, &fp2, &fd);
	if (error)
		goto free3;
	fhold(fp2);
	fp2->f_data = (caddr_t)so2;
	sv[1] = fd;
	error = soconnect2(so1, so2);
	if (error)
		goto free4;
	if (uap->type == SOCK_DGRAM) {
		/*
		 * Datagram socket connection is asymmetric.
		 */
		 error = soconnect2(so2, so1);
		 if (error)
			goto free4;
	}
	fp1->f_flag = fp2->f_flag = FREAD|FWRITE;
	fp1->f_ops = fp2->f_ops = &socketops;
	fp1->f_type = fp2->f_type = DTYPE_SOCKET;
	error = copyout((caddr_t)sv, (caddr_t)uap->rsv, 2 * sizeof (int));
	fdrop(fp1, p);
	fdrop(fp2, p);
	return (error);
free4:
	if (fdp->fd_ofiles[sv[1]] == fp2) {
		fdp->fd_ofiles[sv[1]] = NULL;
		fdrop(fp2, p);
	}
	fdrop(fp2, p);
free3:
	if (fdp->fd_ofiles[sv[0]] == fp1) {
		fdp->fd_ofiles[sv[0]] = NULL;
		fdrop(fp1, p);
	}
	fdrop(fp1, p);
free2:
	(void)soclose(so2);
free1:
	(void)soclose(so1);
	return (error);
}

static int
sendit(p, s, mp, flags)
	register struct proc *p;
	int s;
	register struct msghdr *mp;
	int flags;
{
	struct file *fp;
	struct uio auio;
	register struct iovec *iov;
	register int i;
	struct mbuf *control;
	struct sockaddr *to;
	int len, error;
	struct socket *so;
#ifdef KTRACE
	struct iovec *ktriov = NULL;
	struct uio ktruio;
#endif

	error = holdsock(p->p_fd, s, &fp);
	if (error)
		return (error);
	auio.uio_iov = mp->msg_iov;
	auio.uio_iovcnt = mp->msg_iovlen;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_rw = UIO_WRITE;
	auio.uio_procp = p;
	auio.uio_offset = 0;			/* XXX */
	auio.uio_resid = 0;
	iov = mp->msg_iov;
	for (i = 0; i < mp->msg_iovlen; i++, iov++) {
		if ((auio.uio_resid += iov->iov_len) < 0) {
			fdrop(fp, p);
			return (EINVAL);
		}
	}
	if (mp->msg_name) {
		error = getsockaddr(&to, mp->msg_name, mp->msg_namelen);
		if (error) {
			fdrop(fp, p);
			return (error);
		}
	} else {
		to = 0;
	}
	if (mp->msg_control) {
		if (mp->msg_controllen < sizeof(struct cmsghdr)
#ifdef COMPAT_OLDSOCK
		    && mp->msg_flags != MSG_COMPAT
#endif
		) {
			error = EINVAL;
			goto bad;
		}
		error = sockargs(&control, mp->msg_control,
		    mp->msg_controllen, MT_CONTROL);
		if (error)
			goto bad;
#ifdef COMPAT_OLDSOCK
		if (mp->msg_flags == MSG_COMPAT) {
			register struct cmsghdr *cm;

			M_PREPEND(control, sizeof(*cm), M_WAIT);
			if (control == 0) {
				error = ENOBUFS;
				goto bad;
			} else {
				cm = mtod(control, struct cmsghdr *);
				cm->cmsg_len = control->m_len;
				cm->cmsg_level = SOL_SOCKET;
				cm->cmsg_type = SCM_RIGHTS;
			}
		}
#endif
	} else {
		control = 0;
	}
#ifdef KTRACE
	if (KTRPOINT(p, KTR_GENIO)) {
		int iovlen = auio.uio_iovcnt * sizeof (struct iovec);

		MALLOC(ktriov, struct iovec *, iovlen, M_TEMP, M_WAITOK);
		bcopy((caddr_t)auio.uio_iov, (caddr_t)ktriov, iovlen);
		ktruio = auio;
	}
#endif
	len = auio.uio_resid;
	so = (struct socket *)fp->f_data;
	error = so->so_proto->pr_usrreqs->pru_sosend(so, to, &auio, 0, control,
						     flags, p);
	if (error) {
		if (auio.uio_resid != len && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
		if (error == EPIPE)
			psignal(p, SIGPIPE);
	}
	if (error == 0)
		p->p_retval[0] = len - auio.uio_resid;
#ifdef KTRACE
	if (ktriov != NULL) {
		if (error == 0) {
			ktruio.uio_iov = ktriov;
			ktruio.uio_resid = p->p_retval[0];
			ktrgenio(p->p_tracep, s, UIO_WRITE, &ktruio, error);
		}
		FREE(ktriov, M_TEMP);
	}
#endif
bad:
	fdrop(fp, p);
	if (to)
		FREE(to, M_SONAME);
	return (error);
}

int
sendto(p, uap)
	struct proc *p;
	register struct sendto_args /* {
		int	s;
		caddr_t	buf;
		size_t	len;
		int	flags;
		caddr_t	to;
		int	tolen;
	} */ *uap;
{
	struct msghdr msg;
	struct iovec aiov;

	msg.msg_name = uap->to;
	msg.msg_namelen = uap->tolen;
	msg.msg_iov = &aiov;
	msg.msg_iovlen = 1;
	msg.msg_control = 0;
#ifdef COMPAT_OLDSOCK
	msg.msg_flags = 0;
#endif
	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->len;
	return (sendit(p, uap->s, &msg, uap->flags));
}

#ifdef COMPAT_OLDSOCK
int
osend(p, uap)
	struct proc *p;
	register struct osend_args /* {
		int	s;
		caddr_t	buf;
		int	len;
		int	flags;
	} */ *uap;
{
	struct msghdr msg;
	struct iovec aiov;

	msg.msg_name = 0;
	msg.msg_namelen = 0;
	msg.msg_iov = &aiov;
	msg.msg_iovlen = 1;
	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->len;
	msg.msg_control = 0;
	msg.msg_flags = 0;
	return (sendit(p, uap->s, &msg, uap->flags));
}

int
osendmsg(p, uap)
	struct proc *p;
	register struct osendmsg_args /* {
		int	s;
		caddr_t	msg;
		int	flags;
	} */ *uap;
{
	struct msghdr msg;
	struct iovec aiov[UIO_SMALLIOV], *iov;
	int error;

	error = copyin(uap->msg, (caddr_t)&msg, sizeof (struct omsghdr));
	if (error)
		return (error);
	if ((u_int)msg.msg_iovlen >= UIO_SMALLIOV) {
		if ((u_int)msg.msg_iovlen >= UIO_MAXIOV)
			return (EMSGSIZE);
		MALLOC(iov, struct iovec *,
		      sizeof(struct iovec) * (u_int)msg.msg_iovlen, M_IOV,
		      M_WAITOK);
	} else
		iov = aiov;
	error = copyin((caddr_t)msg.msg_iov, (caddr_t)iov,
	    (unsigned)(msg.msg_iovlen * sizeof (struct iovec)));
	if (error)
		goto done;
	msg.msg_flags = MSG_COMPAT;
	msg.msg_iov = iov;
	error = sendit(p, uap->s, &msg, uap->flags);
done:
	if (iov != aiov)
		FREE(iov, M_IOV);
	return (error);
}
#endif

int
sendmsg(p, uap)
	struct proc *p;
	register struct sendmsg_args /* {
		int	s;
		caddr_t	msg;
		int	flags;
	} */ *uap;
{
	struct msghdr msg;
	struct iovec aiov[UIO_SMALLIOV], *iov;
	int error;

	error = copyin(uap->msg, (caddr_t)&msg, sizeof (msg));
	if (error)
		return (error);
	if ((u_int)msg.msg_iovlen >= UIO_SMALLIOV) {
		if ((u_int)msg.msg_iovlen >= UIO_MAXIOV)
			return (EMSGSIZE);
		MALLOC(iov, struct iovec *,
		       sizeof(struct iovec) * (u_int)msg.msg_iovlen, M_IOV,
		       M_WAITOK);
	} else
		iov = aiov;
	if (msg.msg_iovlen &&
	    (error = copyin((caddr_t)msg.msg_iov, (caddr_t)iov,
	    (unsigned)(msg.msg_iovlen * sizeof (struct iovec)))))
		goto done;
	msg.msg_iov = iov;
#ifdef COMPAT_OLDSOCK
	msg.msg_flags = 0;
#endif
	error = sendit(p, uap->s, &msg, uap->flags);
done:
	if (iov != aiov)
		FREE(iov, M_IOV);
	return (error);
}

static int
recvit(p, s, mp, namelenp)
	register struct proc *p;
	int s;
	register struct msghdr *mp;
	caddr_t namelenp;
{
	struct file *fp;
	struct uio auio;
	register struct iovec *iov;
	register int i;
	int len, error;
	struct mbuf *m, *control = 0;
	caddr_t ctlbuf;
	struct socket *so;
	struct sockaddr *fromsa = 0;
#ifdef KTRACE
	struct iovec *ktriov = NULL;
	struct uio ktruio;
#endif

	error = holdsock(p->p_fd, s, &fp);
	if (error)
		return (error);
	auio.uio_iov = mp->msg_iov;
	auio.uio_iovcnt = mp->msg_iovlen;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_rw = UIO_READ;
	auio.uio_procp = p;
	auio.uio_offset = 0;			/* XXX */
	auio.uio_resid = 0;
	iov = mp->msg_iov;
	for (i = 0; i < mp->msg_iovlen; i++, iov++) {
		if ((auio.uio_resid += iov->iov_len) < 0) {
			fdrop(fp, p);
			return (EINVAL);
		}
	}
#ifdef KTRACE
	if (KTRPOINT(p, KTR_GENIO)) {
		int iovlen = auio.uio_iovcnt * sizeof (struct iovec);

		MALLOC(ktriov, struct iovec *, iovlen, M_TEMP, M_WAITOK);
		bcopy((caddr_t)auio.uio_iov, (caddr_t)ktriov, iovlen);
		ktruio = auio;
	}
#endif
	len = auio.uio_resid;
	so = (struct socket *)fp->f_data;
	error = so->so_proto->pr_usrreqs->pru_soreceive(so, &fromsa, &auio,
	    (struct mbuf **)0, mp->msg_control ? &control : (struct mbuf **)0,
	    &mp->msg_flags);
	if (error) {
		if (auio.uio_resid != len && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
	}
#ifdef KTRACE
	if (ktriov != NULL) {
		if (error == 0) {
			ktruio.uio_iov = ktriov;
			ktruio.uio_resid = len - auio.uio_resid;
			ktrgenio(p->p_tracep, s, UIO_READ, &ktruio, error);
		}
		FREE(ktriov, M_TEMP);
	}
#endif
	if (error)
		goto out;
	p->p_retval[0] = len - auio.uio_resid;
	if (mp->msg_name) {
		len = mp->msg_namelen;
		if (len <= 0 || fromsa == 0)
			len = 0;
		else {
#ifndef MIN
#define MIN(a,b) ((a)>(b)?(b):(a))
#endif
			/* save sa_len before it is destroyed by MSG_COMPAT */
			len = MIN(len, fromsa->sa_len);
#ifdef COMPAT_OLDSOCK
			if (mp->msg_flags & MSG_COMPAT)
				((struct osockaddr *)fromsa)->sa_family =
				    fromsa->sa_family;
#endif
			error = copyout(fromsa,
			    (caddr_t)mp->msg_name, (unsigned)len);
			if (error)
				goto out;
		}
		mp->msg_namelen = len;
		if (namelenp &&
		    (error = copyout((caddr_t)&len, namelenp, sizeof (int)))) {
#ifdef COMPAT_OLDSOCK
			if (mp->msg_flags & MSG_COMPAT)
				error = 0;	/* old recvfrom didn't check */
			else
#endif
			goto out;
		}
	}
	if (mp->msg_control) {
#ifdef COMPAT_OLDSOCK
		/*
		 * We assume that old recvmsg calls won't receive access
		 * rights and other control info, esp. as control info
		 * is always optional and those options didn't exist in 4.3.
		 * If we receive rights, trim the cmsghdr; anything else
		 * is tossed.
		 */
		if (control && mp->msg_flags & MSG_COMPAT) {
			if (mtod(control, struct cmsghdr *)->cmsg_level !=
			    SOL_SOCKET ||
			    mtod(control, struct cmsghdr *)->cmsg_type !=
			    SCM_RIGHTS) {
				mp->msg_controllen = 0;
				goto out;
			}
			control->m_len -= sizeof (struct cmsghdr);
			control->m_data += sizeof (struct cmsghdr);
		}
#endif
		len = mp->msg_controllen;
		m = control;
		mp->msg_controllen = 0;
		ctlbuf = (caddr_t) mp->msg_control;

		while (m && len > 0) {
			unsigned int tocopy;

			if (len >= m->m_len) 
				tocopy = m->m_len;
			else {
				mp->msg_flags |= MSG_CTRUNC;
				tocopy = len;
			}
		
			if ((error = copyout((caddr_t)mtod(m, caddr_t),
					ctlbuf, tocopy)) != 0)
				goto out;

			ctlbuf += tocopy;
			len -= tocopy;
			m = m->m_next;
		}
		mp->msg_controllen = ctlbuf - (caddr_t)mp->msg_control;
	}
out:
	fdrop(fp, p);
	if (fromsa)
		FREE(fromsa, M_SONAME);
	if (control)
		m_freem(control);
	return (error);
}

int
recvfrom(p, uap)
	struct proc *p;
	register struct recvfrom_args /* {
		int	s;
		caddr_t	buf;
		size_t	len;
		int	flags;
		caddr_t	from;
		int	*fromlenaddr;
	} */ *uap;
{
	struct msghdr msg;
	struct iovec aiov;
	int error;

	if (uap->fromlenaddr) {
		error = copyin((caddr_t)uap->fromlenaddr,
		    (caddr_t)&msg.msg_namelen, sizeof (msg.msg_namelen));
		if (error)
			return (error);
	} else
		msg.msg_namelen = 0;
	msg.msg_name = uap->from;
	msg.msg_iov = &aiov;
	msg.msg_iovlen = 1;
	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->len;
	msg.msg_control = 0;
	msg.msg_flags = uap->flags;
	return (recvit(p, uap->s, &msg, (caddr_t)uap->fromlenaddr));
}

#ifdef COMPAT_OLDSOCK
int
orecvfrom(p, uap)
	struct proc *p;
	struct recvfrom_args *uap;
{

	uap->flags |= MSG_COMPAT;
	return (recvfrom(p, uap));
}
#endif


#ifdef COMPAT_OLDSOCK
int
orecv(p, uap)
	struct proc *p;
	register struct orecv_args /* {
		int	s;
		caddr_t	buf;
		int	len;
		int	flags;
	} */ *uap;
{
	struct msghdr msg;
	struct iovec aiov;

	msg.msg_name = 0;
	msg.msg_namelen = 0;
	msg.msg_iov = &aiov;
	msg.msg_iovlen = 1;
	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->len;
	msg.msg_control = 0;
	msg.msg_flags = uap->flags;
	return (recvit(p, uap->s, &msg, (caddr_t)0));
}

/*
 * Old recvmsg.  This code takes advantage of the fact that the old msghdr
 * overlays the new one, missing only the flags, and with the (old) access
 * rights where the control fields are now.
 */
int
orecvmsg(p, uap)
	struct proc *p;
	register struct orecvmsg_args /* {
		int	s;
		struct	omsghdr *msg;
		int	flags;
	} */ *uap;
{
	struct msghdr msg;
	struct iovec aiov[UIO_SMALLIOV], *iov;
	int error;

	error = copyin((caddr_t)uap->msg, (caddr_t)&msg,
	    sizeof (struct omsghdr));
	if (error)
		return (error);
	if ((u_int)msg.msg_iovlen >= UIO_SMALLIOV) {
		if ((u_int)msg.msg_iovlen >= UIO_MAXIOV)
			return (EMSGSIZE);
		MALLOC(iov, struct iovec *,
		      sizeof(struct iovec) * (u_int)msg.msg_iovlen, M_IOV,
		      M_WAITOK);
	} else
		iov = aiov;
	msg.msg_flags = uap->flags | MSG_COMPAT;
	error = copyin((caddr_t)msg.msg_iov, (caddr_t)iov,
	    (unsigned)(msg.msg_iovlen * sizeof (struct iovec)));
	if (error)
		goto done;
	msg.msg_iov = iov;
	error = recvit(p, uap->s, &msg, (caddr_t)&uap->msg->msg_namelen);

	if (msg.msg_controllen && error == 0)
		error = copyout((caddr_t)&msg.msg_controllen,
		    (caddr_t)&uap->msg->msg_accrightslen, sizeof (int));
done:
	if (iov != aiov)
		FREE(iov, M_IOV);
	return (error);
}
#endif

int
recvmsg(p, uap)
	struct proc *p;
	register struct recvmsg_args /* {
		int	s;
		struct	msghdr *msg;
		int	flags;
	} */ *uap;
{
	struct msghdr msg;
	struct iovec aiov[UIO_SMALLIOV], *uiov, *iov;
	register int error;

	error = copyin((caddr_t)uap->msg, (caddr_t)&msg, sizeof (msg));
	if (error)
		return (error);
	if ((u_int)msg.msg_iovlen >= UIO_SMALLIOV) {
		if ((u_int)msg.msg_iovlen >= UIO_MAXIOV)
			return (EMSGSIZE);
		MALLOC(iov, struct iovec *,
		       sizeof(struct iovec) * (u_int)msg.msg_iovlen, M_IOV,
		       M_WAITOK);
	} else
		iov = aiov;
#ifdef COMPAT_OLDSOCK
	msg.msg_flags = uap->flags &~ MSG_COMPAT;
#else
	msg.msg_flags = uap->flags;
#endif
	uiov = msg.msg_iov;
	msg.msg_iov = iov;
	error = copyin((caddr_t)uiov, (caddr_t)iov,
	    (unsigned)(msg.msg_iovlen * sizeof (struct iovec)));
	if (error)
		goto done;
	error = recvit(p, uap->s, &msg, (caddr_t)0);
	if (!error) {
		msg.msg_iov = uiov;
		error = copyout((caddr_t)&msg, (caddr_t)uap->msg, sizeof(msg));
	}
done:
	if (iov != aiov)
		FREE(iov, M_IOV);
	return (error);
}

/* ARGSUSED */
int
shutdown(p, uap)
	struct proc *p;
	register struct shutdown_args /* {
		int	s;
		int	how;
	} */ *uap;
{
	struct file *fp;
	int error;

	error = holdsock(p->p_fd, uap->s, &fp);
	if (error)
		return (error);
	error = soshutdown((struct socket *)fp->f_data, uap->how);
	fdrop(fp, p);
	return(error);
}

/* ARGSUSED */
int
setsockopt(p, uap)
	struct proc *p;
	register struct setsockopt_args /* {
		int	s;
		int	level;
		int	name;
		caddr_t	val;
		int	valsize;
	} */ *uap;
{
	struct file *fp;
	struct sockopt sopt;
	int error;

	if (uap->val == 0 && uap->valsize != 0)
		return (EFAULT);
	if (uap->valsize < 0)
		return (EINVAL);

	error = holdsock(p->p_fd, uap->s, &fp);
	if (error)
		return (error);

	sopt.sopt_dir = SOPT_SET;
	sopt.sopt_level = uap->level;
	sopt.sopt_name = uap->name;
	sopt.sopt_val = uap->val;
	sopt.sopt_valsize = uap->valsize;
	sopt.sopt_p = p;
	error = sosetopt((struct socket *)fp->f_data, &sopt);
	fdrop(fp, p);
	return(error);
}

/* ARGSUSED */
int
getsockopt(p, uap)
	struct proc *p;
	register struct getsockopt_args /* {
		int	s;
		int	level;
		int	name;
		caddr_t	val;
		int	*avalsize;
	} */ *uap;
{
	int	valsize, error;
	struct	file *fp;
	struct	sockopt sopt;

	error = holdsock(p->p_fd, uap->s, &fp);
	if (error)
		return (error);
	if (uap->val) {
		error = copyin((caddr_t)uap->avalsize, (caddr_t)&valsize,
		    sizeof (valsize));
		if (error) {
			fdrop(fp, p);
			return (error);
		}
		if (valsize < 0) {
			fdrop(fp, p);
			return (EINVAL);
		}
	} else {
		valsize = 0;
	}

	sopt.sopt_dir = SOPT_GET;
	sopt.sopt_level = uap->level;
	sopt.sopt_name = uap->name;
	sopt.sopt_val = uap->val;
	sopt.sopt_valsize = (size_t)valsize; /* checked non-negative above */
	sopt.sopt_p = p;

	error = sogetopt((struct socket *)fp->f_data, &sopt);
	if (error == 0) {
		valsize = sopt.sopt_valsize;
		error = copyout((caddr_t)&valsize,
				(caddr_t)uap->avalsize, sizeof (valsize));
	}
	fdrop(fp, p);
	return (error);
}

/*
 * Get socket name.
 */
/* ARGSUSED */
static int
getsockname1(p, uap, compat)
	struct proc *p;
	register struct getsockname_args /* {
		int	fdes;
		caddr_t	asa;
		int	*alen;
	} */ *uap;
	int compat;
{
	struct file *fp;
	register struct socket *so;
	struct sockaddr *sa;
	int len, error;

	error = holdsock(p->p_fd, uap->fdes, &fp);
	if (error)
		return (error);
	error = copyin((caddr_t)uap->alen, (caddr_t)&len, sizeof (len));
	if (error) {
		fdrop(fp, p);
		return (error);
	}
	if (len < 0) {
		fdrop(fp, p);
		return (EINVAL);
	}
	so = (struct socket *)fp->f_data;
	sa = 0;
	error = (*so->so_proto->pr_usrreqs->pru_sockaddr)(so, &sa);
	if (error)
		goto bad;
	if (sa == 0) {
		len = 0;
		goto gotnothing;
	}

	len = MIN(len, sa->sa_len);
#ifdef COMPAT_OLDSOCK
	if (compat)
		((struct osockaddr *)sa)->sa_family = sa->sa_family;
#endif
	error = copyout(sa, (caddr_t)uap->asa, (u_int)len);
	if (error == 0)
gotnothing:
		error = copyout((caddr_t)&len, (caddr_t)uap->alen,
		    sizeof (len));
bad:
	if (sa)
		FREE(sa, M_SONAME);
	fdrop(fp, p);
	return (error);
}

int
getsockname(p, uap)
	struct proc *p;
	struct getsockname_args *uap;
{

	return (getsockname1(p, uap, 0));
}

#ifdef COMPAT_OLDSOCK
int
ogetsockname(p, uap)
	struct proc *p;
	struct getsockname_args *uap;
{

	return (getsockname1(p, uap, 1));
}
#endif /* COMPAT_OLDSOCK */

/*
 * Get name of peer for connected socket.
 */
/* ARGSUSED */
static int
getpeername1(p, uap, compat)
	struct proc *p;
	register struct getpeername_args /* {
		int	fdes;
		caddr_t	asa;
		int	*alen;
	} */ *uap;
	int compat;
{
	struct file *fp;
	register struct socket *so;
	struct sockaddr *sa;
	int len, error;

	error = holdsock(p->p_fd, uap->fdes, &fp);
	if (error)
		return (error);
	so = (struct socket *)fp->f_data;
	if ((so->so_state & (SS_ISCONNECTED|SS_ISCONFIRMING)) == 0) {
		fdrop(fp, p);
		return (ENOTCONN);
	}
	error = copyin((caddr_t)uap->alen, (caddr_t)&len, sizeof (len));
	if (error) {
		fdrop(fp, p);
		return (error);
	}
	if (len < 0) {
		fdrop(fp, p);
		return (EINVAL);
	}
	sa = 0;
	error = (*so->so_proto->pr_usrreqs->pru_peeraddr)(so, &sa);
	if (error)
		goto bad;
	if (sa == 0) {
		len = 0;
		goto gotnothing;
	}
	len = MIN(len, sa->sa_len);
#ifdef COMPAT_OLDSOCK
	if (compat)
		((struct osockaddr *)sa)->sa_family =
		    sa->sa_family;
#endif
	error = copyout(sa, (caddr_t)uap->asa, (u_int)len);
	if (error)
		goto bad;
gotnothing:
	error = copyout((caddr_t)&len, (caddr_t)uap->alen, sizeof (len));
bad:
	if (sa)
		FREE(sa, M_SONAME);
	fdrop(fp, p);
	return (error);
}

int
getpeername(p, uap)
	struct proc *p;
	struct getpeername_args *uap;
{

	return (getpeername1(p, uap, 0));
}

#ifdef COMPAT_OLDSOCK
int
ogetpeername(p, uap)
	struct proc *p;
	struct ogetpeername_args *uap;
{

	/* XXX uap should have type `getpeername_args *' to begin with. */
	return (getpeername1(p, (struct getpeername_args *)uap, 1));
}
#endif /* COMPAT_OLDSOCK */

int
sockargs(mp, buf, buflen, type)
	struct mbuf **mp;
	caddr_t buf;
	int buflen, type;
{
	register struct sockaddr *sa;
	register struct mbuf *m;
	int error;

	if ((u_int)buflen > MLEN) {
#ifdef COMPAT_OLDSOCK
		if (type == MT_SONAME && (u_int)buflen <= 112)
			buflen = MLEN;		/* unix domain compat. hack */
		else
#endif
			if ((u_int)buflen > MCLBYTES)
				return (EINVAL);
	}
	m = m_get(M_WAIT, type);
	if (m == NULL)
		return (ENOBUFS);
	if ((u_int)buflen > MLEN)
		MCLGET(m, M_WAIT);
	m->m_len = buflen;
	error = copyin(buf, mtod(m, caddr_t), (u_int)buflen);
	if (error)
		(void) m_free(m);
	else {
		*mp = m;
		if (type == MT_SONAME) {
			sa = mtod(m, struct sockaddr *);

#if defined(COMPAT_OLDSOCK) && BYTE_ORDER != BIG_ENDIAN
			if (sa->sa_family == 0 && sa->sa_len < AF_MAX)
				sa->sa_family = sa->sa_len;
#endif
			sa->sa_len = buflen;
		}
	}
	return (error);
}

int
getsockaddr(namp, uaddr, len)
	struct sockaddr **namp;
	caddr_t uaddr;
	size_t len;
{
	struct sockaddr *sa;
	int error;

	if (len > SOCK_MAXADDRLEN)
		return ENAMETOOLONG;
	if (len < offsetof(struct sockaddr, sa_data[0]))
		return EINVAL;
	MALLOC(sa, struct sockaddr *, len, M_SONAME, M_WAITOK);
	error = copyin(uaddr, sa, len);
	if (error) {
		FREE(sa, M_SONAME);
	} else {
#if defined(COMPAT_OLDSOCK) && BYTE_ORDER != BIG_ENDIAN
		if (sa->sa_family == 0 && sa->sa_len < AF_MAX)
			sa->sa_family = sa->sa_len;
#endif
		sa->sa_len = len;
		*namp = sa;
	}
	return error;
}

/*
 * holdsock() - load the struct file pointer associated
 * with a socket into *fpp.  If an error occurs, non-zero
 * will be returned and *fpp will be set to NULL.
 */
int
holdsock(fdp, fdes, fpp)
	struct filedesc *fdp;
	int fdes;
	struct file **fpp;
{
	register struct file *fp = NULL;
	int error = 0;

	if ((unsigned)fdes >= fdp->fd_nfiles ||
	    (fp = fdp->fd_ofiles[fdes]) == NULL) {
		error = EBADF;
	} else if (fp->f_type != DTYPE_SOCKET) {
		error = ENOTSOCK;
		fp = NULL;
	} else {
		fhold(fp);
	}
	*fpp = fp;
	return(error);
}

/*
 * Allocate a pool of sf_bufs (sendfile(2) or "super-fast" if you prefer. :-))
 */
static void
sf_buf_init(void *arg)
{
	int i;

	SLIST_INIT(&sf_freelist);
	sf_base = kmem_alloc_pageable(kernel_map, nsfbufs * PAGE_SIZE);
	sf_bufs = malloc(nsfbufs * sizeof(struct sf_buf), M_TEMP, M_NOWAIT);
	bzero(sf_bufs, nsfbufs * sizeof(struct sf_buf));
	for (i = 0; i < nsfbufs; i++) {
		sf_bufs[i].kva = sf_base + i * PAGE_SIZE;
		SLIST_INSERT_HEAD(&sf_freelist, &sf_bufs[i], free_list);
	}
}

/*
 * Get an sf_buf from the freelist. Will block if none are available.
 */
struct sf_buf *
sf_buf_alloc()
{
	struct sf_buf *sf;
	int s;
	int error;

	s = splimp();
	while ((sf = SLIST_FIRST(&sf_freelist)) == NULL) {
		sf_buf_alloc_want = 1;
		mbstat.sf_allocwait++;
		error = tsleep(&sf_freelist, PVM|PCATCH, "sfbufa", 0);
		if (error)
			break;
	}
	if (sf != NULL) {
		SLIST_REMOVE_HEAD(&sf_freelist, free_list);
		sf->refcnt = 1;
		nsfbufsused++;
		nsfbufspeak = imax(nsfbufspeak, nsfbufsused);
	}
	splx(s);
	return (sf);
}

#define dtosf(x)	(&sf_bufs[((uintptr_t)(x) - (uintptr_t)sf_base) >> PAGE_SHIFT])
void
sf_buf_ref(caddr_t addr, u_int size)
{
	struct sf_buf *sf;

	sf = dtosf(addr);
	if (sf->refcnt == 0)
		panic("sf_buf_ref: referencing a free sf_buf");
	sf->refcnt++;
}

/*
 * Lose a reference to an sf_buf. When none left, detach mapped page
 * and release resources back to the system.
 *
 * Must be called at splimp.
 */
void
sf_buf_free(caddr_t addr, u_int size)
{
	struct sf_buf *sf;
	struct vm_page *m;
	int s;

	sf = dtosf(addr);
	if (sf->refcnt == 0)
		panic("sf_buf_free: freeing free sf_buf");
	sf->refcnt--;
	if (sf->refcnt == 0) {
		nsfbufsused--;
		pmap_qremove((vm_offset_t)addr, 1);
		m = sf->m;
		s = splvm();
		vm_page_unwire(m, 0);
		/*
		 * Check for the object going away on us. This can
		 * happen since we don't hold a reference to it.
		 * If so, we're responsible for freeing the page.
		 */
		if (m->wire_count == 0 && m->object == NULL)
			vm_page_free(m);
		splx(s);
		sf->m = NULL;
		SLIST_INSERT_HEAD(&sf_freelist, sf, free_list);
		if (sf_buf_alloc_want) {
			sf_buf_alloc_want = 0;
			wakeup(&sf_freelist);
		}
	}
}

/*
 * sendfile(2).
 * int sendfile(int fd, int s, off_t offset, size_t nbytes,
 *	 struct sf_hdtr *hdtr, off_t *sbytes, int flags)
 *
 * Send a file specified by 'fd' and starting at 'offset' to a socket
 * specified by 's'. Send only 'nbytes' of the file or until EOF if
 * nbytes == 0. Optionally add a header and/or trailer to the socket
 * output. If specified, write the total number of bytes sent into *sbytes.
 */
int
sendfile(struct proc *p, struct sendfile_args *uap)
{

	return (do_sendfile(p, uap, 0));
}

#ifdef COMPAT_43
int
osendfile(struct proc *p, struct osendfile_args *uap)
{
	struct sendfile_args args;

	args.fd = uap->fd;
	args.s = uap->s;
	args.offset = uap->offset;
	args.nbytes = uap->nbytes;
	args.hdtr = uap->hdtr;
	args.sbytes = uap->sbytes;
	args.flags = uap->flags;

	return (do_sendfile(p, &args, 1));
}
#endif

int
do_sendfile(struct proc *p, struct sendfile_args *uap, int compat)
{
	struct file *fp;
	struct filedesc *fdp = p->p_fd;
	struct vnode *vp;
	struct vm_object *obj;
	struct socket *so;
	struct mbuf *m;
	struct sf_buf *sf;
	struct vm_page *pg;
	struct writev_args nuap;
	struct sf_hdtr hdtr;
	off_t off, xfsize, hdtr_size, sbytes = 0;
	int error = 0, s;

	vp = NULL;
	hdtr_size = 0;
	/*
	 * Do argument checking. Must be a regular file in, stream
	 * type and connected socket out, positive offset.
	 */
	fp = holdfp(fdp, uap->fd, FREAD);
	if (fp == NULL) {
		error = EBADF;
		goto done;
	}
	if (fp->f_type != DTYPE_VNODE) {
		error = EINVAL;
		goto done;
	}
	vp = (struct vnode *)fp->f_data;
	vref(vp);
	if (vp->v_type != VREG || VOP_GETVOBJECT(vp, &obj) != 0) {
		error = EINVAL;
		goto done;
	}
	fdrop(fp, p);
	error = holdsock(p->p_fd, uap->s, &fp);
	if (error)
		goto done;
	so = (struct socket *)fp->f_data;
	if (so->so_type != SOCK_STREAM) {
		error = EINVAL;
		goto done;
	}
	if ((so->so_state & SS_ISCONNECTED) == 0) {
		error = ENOTCONN;
		goto done;
	}
	if (uap->offset < 0) {
		error = EINVAL;
		goto done;
	}

	/*
	 * If specified, get the pointer to the sf_hdtr struct for
	 * any headers/trailers.
	 */
	if (uap->hdtr != NULL) {
		error = copyin(uap->hdtr, &hdtr, sizeof(hdtr));
		if (error)
			goto done;
		/*
		 * Send any headers. Wimp out and use writev(2).
		 */
		if (hdtr.headers != NULL) {
			nuap.fd = uap->s;
			nuap.iovp = hdtr.headers;
			nuap.iovcnt = hdtr.hdr_cnt;
			error = writev(p, &nuap);
			if (error)
				goto done;
			if (compat)
				sbytes += p->p_retval[0];
			else
				hdtr_size += p->p_retval[0];
		}
	}

	/*
	 * Protect against multiple writers to the socket.
	 */
	(void) sblock(&so->so_snd, M_WAITOK);

	/*
	 * Loop through the pages in the file, starting with the requested
	 * offset. Get a file page (do I/O if necessary), map the file page
	 * into an sf_buf, attach an mbuf header to the sf_buf, and queue
	 * it on the socket.
	 */
	for (off = uap->offset; ; off += xfsize, sbytes += xfsize) {
		vm_pindex_t pindex;
		vm_offset_t pgoff;

		pindex = OFF_TO_IDX(off);
retry_lookup:
		/*
		 * Calculate the amount to transfer. Not to exceed a page,
		 * the EOF, or the passed in nbytes.
		 */
		xfsize = obj->un_pager.vnp.vnp_size - off;
		if (xfsize > PAGE_SIZE)
			xfsize = PAGE_SIZE;
		pgoff = (vm_offset_t)(off & PAGE_MASK);
		if (PAGE_SIZE - pgoff < xfsize)
			xfsize = PAGE_SIZE - pgoff;
		if (uap->nbytes && xfsize > (uap->nbytes - sbytes))
			xfsize = uap->nbytes - sbytes;
		if (xfsize <= 0)
			break;
		/*
		 * Optimize the non-blocking case by looking at the socket space
		 * before going to the extra work of constituting the sf_buf.
		 */
		if ((so->so_state & SS_NBIO) && sbspace(&so->so_snd) <= 0) {
			if (so->so_state & SS_CANTSENDMORE)
				error = EPIPE;
			else
				error = EAGAIN;
			sbunlock(&so->so_snd);
			goto done;
		}
		/*
		 * Attempt to look up the page.  
		 *
		 *	Allocate if not found
		 *
		 *	Wait and loop if busy.
		 */
		pg = vm_page_lookup(obj, pindex);

		if (pg == NULL) {
			pg = vm_page_alloc(obj, pindex, VM_ALLOC_NORMAL);
			if (pg == NULL) {
				VM_WAIT;
				goto retry_lookup;
			}
			vm_page_wakeup(pg);
		} else if (vm_page_sleep_busy(pg, TRUE, "sfpbsy")) {
			goto retry_lookup;
		}

		/*
		 * Wire the page so it does not get ripped out from under
		 * us. 
		 */

		vm_page_wire(pg);

		/*
		 * If page is not valid for what we need, initiate I/O
		 */

		if (!pg->valid || !vm_page_is_valid(pg, pgoff, xfsize)) {
			struct uio auio;
			struct iovec aiov;
			int bsize;

			/*
			 * Ensure that our page is still around when the I/O 
			 * completes.
			 */
			vm_page_io_start(pg);

			/*
			 * Get the page from backing store.
			 */
			bsize = vp->v_mount->mnt_stat.f_iosize;
			auio.uio_iov = &aiov;
			auio.uio_iovcnt = 1;
			aiov.iov_base = 0;
			aiov.iov_len = MAXBSIZE;
			auio.uio_resid = MAXBSIZE;
			auio.uio_offset = trunc_page(off);
			auio.uio_segflg = UIO_NOCOPY;
			auio.uio_rw = UIO_READ;
			auio.uio_procp = p;
			vn_lock(vp, LK_SHARED | LK_NOPAUSE | LK_RETRY, p);
			error = VOP_READ(vp, &auio, IO_VMIO | ((MAXBSIZE / bsize) << 16),
			        p->p_ucred);
			VOP_UNLOCK(vp, 0, p);
			vm_page_flag_clear(pg, PG_ZERO);
			vm_page_io_finish(pg);
			if (error) {
				vm_page_unwire(pg, 0);
				/*
				 * See if anyone else might know about this page.
				 * If not and it is not valid, then free it.
				 */
				if (pg->wire_count == 0 && pg->valid == 0 &&
				    pg->busy == 0 && !(pg->flags & PG_BUSY) &&
				    pg->hold_count == 0) {
					vm_page_busy(pg);
					vm_page_free(pg);
				}
				sbunlock(&so->so_snd);
				goto done;
			}
			mbstat.sf_iocnt++;
		}


		/*
		 * Get a sendfile buf. We usually wait as long as necessary,
		 * but this wait can be interrupted.
		 */
		if ((sf = sf_buf_alloc()) == NULL) {
			mbstat.sf_allocfail++;
			s = splvm();
			vm_page_unwire(pg, 0);
			if (pg->wire_count == 0 && pg->object == NULL)
				vm_page_free(pg);
			splx(s);
			sbunlock(&so->so_snd);
			error = EINTR;
			goto done;
		}


		/*
		 * Allocate a kernel virtual page and insert the physical page
		 * into it.
		 */

		sf->m = pg;
		pmap_qenter(sf->kva, &pg, 1);
		/*
		 * Get an mbuf header and set it up as having external storage.
		 */
		MGETHDR(m, M_WAIT, MT_DATA);
		if (m == NULL) {
			error = ENOBUFS;
			sf_buf_free((void *)sf->kva, PAGE_SIZE);
			sbunlock(&so->so_snd);
			goto done;
		}
		m->m_ext.ext_free = sf_buf_free;
		m->m_ext.ext_ref = sf_buf_ref;
		m->m_ext.ext_buf = (void *)sf->kva;
		m->m_ext.ext_size = PAGE_SIZE;
		m->m_data = (char *) sf->kva + pgoff;
		m->m_flags |= M_EXT;
		m->m_pkthdr.len = m->m_len = xfsize;
		/*
		 * Add the buffer to the socket buffer chain.
		 */
		s = splnet();
retry_space:
		/*
		 * Make sure that the socket is still able to take more data.
		 * CANTSENDMORE being true usually means that the connection
		 * was closed. so_error is true when an error was sensed after
		 * a previous send.
		 * The state is checked after the page mapping and buffer
		 * allocation above since those operations may block and make
		 * any socket checks stale. From this point forward, nothing
		 * blocks before the pru_send (or more accurately, any blocking
		 * results in a loop back to here to re-check).
		 */
		if ((so->so_state & SS_CANTSENDMORE) || so->so_error) {
			if (so->so_state & SS_CANTSENDMORE) {
				error = EPIPE;
			} else {
				error = so->so_error;
				so->so_error = 0;
			}
			m_freem(m);
			sbunlock(&so->so_snd);
			splx(s);
			goto done;
		}
		/*
		 * Wait for socket space to become available. We do this just
		 * after checking the connection state above in order to avoid
		 * a race condition with sbwait().
		 */
		if (sbspace(&so->so_snd) < so->so_snd.sb_lowat) {
			if (so->so_state & SS_NBIO) {
				m_freem(m);
				sbunlock(&so->so_snd);
				splx(s);
				error = EAGAIN;
				goto done;
			}
			error = sbwait(&so->so_snd);
			/*
			 * An error from sbwait usually indicates that we've
			 * been interrupted by a signal. If we've sent anything
			 * then return bytes sent, otherwise return the error.
			 */
			if (error) {
				m_freem(m);
				sbunlock(&so->so_snd);
				splx(s);
				goto done;
			}
			goto retry_space;
		}
		error = (*so->so_proto->pr_usrreqs->pru_send)(so, 0, m, 0, 0, p);
		splx(s);
		if (error) {
			sbunlock(&so->so_snd);
			goto done;
		}
	}
	sbunlock(&so->so_snd);

	/*
	 * Send trailers. Wimp out and use writev(2).
	 */
	if (uap->hdtr != NULL && hdtr.trailers != NULL) {
			nuap.fd = uap->s;
			nuap.iovp = hdtr.trailers;
			nuap.iovcnt = hdtr.trl_cnt;
			error = writev(p, &nuap);
			if (error)
				goto done;
			if (compat)
				sbytes += p->p_retval[0];
			else
				hdtr_size += p->p_retval[0];
	}

done:
	if (uap->sbytes != NULL) {
		if (compat == 0)
			sbytes += hdtr_size;
		copyout(&sbytes, uap->sbytes, sizeof(off_t));
	}
	if (vp)
		vrele(vp);
	if (fp)
		fdrop(fp, p);
	/*
	 * sendfile cannot be restarted.
	 */
	if (error == ERESTART)
		error = EINTR;
	return (error);
}
