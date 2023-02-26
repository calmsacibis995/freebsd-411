/*
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
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
 *	From: @(#)uipc_usrreq.c	8.3 (Berkeley) 1/4/94
 * $FreeBSD: src/sys/kern/uipc_usrreq.c,v 1.54.2.10 2003/03/04 17:28:09 nectar Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/domain.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>		/* XXX must be before <sys/file.h> */
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/mbuf.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/resourcevar.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#include <sys/unpcb.h>
#include <sys/vnode.h>

#include <vm/vm_zone.h>

static	struct vm_zone *unp_zone;
static	unp_gen_t unp_gencnt;
static	u_int unp_count;

static	struct unp_head unp_shead, unp_dhead;

/*
 * Unix communications domain.
 *
 * TODO:
 *	SEQPACKET, RDM
 *	rethink name space problems
 *	need a proper out-of-band
 *	lock pushdown
 */
static struct	sockaddr sun_noname = { sizeof(sun_noname), AF_LOCAL };
static ino_t	unp_ino;		/* prototype for fake inode numbers */

static int     unp_attach __P((struct socket *));
static void    unp_detach __P((struct unpcb *));
static int     unp_bind __P((struct unpcb *,struct sockaddr *, struct proc *));
static int     unp_connect __P((struct socket *,struct sockaddr *,
				struct proc *));
static void    unp_disconnect __P((struct unpcb *));
static void    unp_shutdown __P((struct unpcb *));
static void    unp_drop __P((struct unpcb *, int));
static void    unp_gc __P((void));
static void    unp_scan __P((struct mbuf *, void (*)(struct file *)));
static void    unp_mark __P((struct file *));
static void    unp_discard __P((struct file *));
static int     unp_internalize __P((struct mbuf *, struct proc *));
static int     unp_listen __P((struct unpcb *, struct proc *));

static int
uipc_abort(struct socket *so)
{
	struct unpcb *unp = sotounpcb(so);

	if (unp == 0)
		return EINVAL;
	unp_drop(unp, ECONNABORTED);
	unp_detach(unp);
	sofree(so);
	return 0;
}

static int
uipc_accept(struct socket *so, struct sockaddr **nam)
{
	struct unpcb *unp = sotounpcb(so);

	if (unp == 0)
		return EINVAL;

	/*
	 * Pass back name of connected socket,
	 * if it was bound and we are still connected
	 * (our peer may have closed already!).
	 */
	if (unp->unp_conn && unp->unp_conn->unp_addr) {
		*nam = dup_sockaddr((struct sockaddr *)unp->unp_conn->unp_addr,
				    1);
	} else {
		*nam = dup_sockaddr((struct sockaddr *)&sun_noname, 1);
	}
	return 0;
}

static int
uipc_attach(struct socket *so, int proto, struct proc *p)
{
	struct unpcb *unp = sotounpcb(so);

	if (unp != 0)
		return EISCONN;
	return unp_attach(so);
}

static int
uipc_bind(struct socket *so, struct sockaddr *nam, struct proc *p)
{
	struct unpcb *unp = sotounpcb(so);

	if (unp == 0)
		return EINVAL;

	return unp_bind(unp, nam, p);
}

static int
uipc_connect(struct socket *so, struct sockaddr *nam, struct proc *p)
{
	struct unpcb *unp = sotounpcb(so);

	if (unp == 0)
		return EINVAL;
	return unp_connect(so, nam, curproc);
}

static int
uipc_connect2(struct socket *so1, struct socket *so2)
{
	struct unpcb *unp = sotounpcb(so1);

	if (unp == 0)
		return EINVAL;

	return unp_connect2(so1, so2);
}

/* control is EOPNOTSUPP */

static int
uipc_detach(struct socket *so)
{
	struct unpcb *unp = sotounpcb(so);

	if (unp == 0)
		return EINVAL;

	unp_detach(unp);
	return 0;
}

static int
uipc_disconnect(struct socket *so)
{
	struct unpcb *unp = sotounpcb(so);

	if (unp == 0)
		return EINVAL;
	unp_disconnect(unp);
	return 0;
}

static int
uipc_listen(struct socket *so, struct proc *p)
{
	struct unpcb *unp = sotounpcb(so);

	if (unp == 0 || unp->unp_vnode == 0)
		return EINVAL;
	return unp_listen(unp, p);
}

static int
uipc_peeraddr(struct socket *so, struct sockaddr **nam)
{
	struct unpcb *unp = sotounpcb(so);

	if (unp == 0)
		return EINVAL;
	if (unp->unp_conn && unp->unp_conn->unp_addr)
		*nam = dup_sockaddr((struct sockaddr *)unp->unp_conn->unp_addr,
				    1);
	else {
		/*
		 * XXX: It seems that this test always fails even when
		 * connection is established.  So, this else clause is
		 * added as workaround to return PF_LOCAL sockaddr.
		 */
		*nam = dup_sockaddr((struct sockaddr *)&sun_noname, 1);
	}
	return 0;
}

static int
uipc_rcvd(struct socket *so, int flags)
{
	struct unpcb *unp = sotounpcb(so);
	struct socket *so2;
	u_long newhiwat;

	if (unp == 0)
		return EINVAL;
	switch (so->so_type) {
	case SOCK_DGRAM:
		panic("uipc_rcvd DGRAM?");
		/*NOTREACHED*/

	case SOCK_STREAM:
		if (unp->unp_conn == 0)
			break;
		so2 = unp->unp_conn->unp_socket;
		/*
		 * Adjust backpressure on sender
		 * and wakeup any waiting to write.
		 */
		so2->so_snd.sb_mbmax += unp->unp_mbcnt - so->so_rcv.sb_mbcnt;
		unp->unp_mbcnt = so->so_rcv.sb_mbcnt;
		newhiwat = so2->so_snd.sb_hiwat + unp->unp_cc -
		    so->so_rcv.sb_cc;
		(void)chgsbsize(so2->so_cred->cr_uidinfo, &so2->so_snd.sb_hiwat,
		    newhiwat, RLIM_INFINITY);
		unp->unp_cc = so->so_rcv.sb_cc;
		sowwakeup(so2);
		break;

	default:
		panic("uipc_rcvd unknown socktype");
	}
	return 0;
}

/* pru_rcvoob is EOPNOTSUPP */

static int
uipc_send(struct socket *so, int flags, struct mbuf *m, struct sockaddr *nam,
	  struct mbuf *control, struct proc *p)
{
	int error = 0;
	struct unpcb *unp = sotounpcb(so);
	struct socket *so2;
	u_long newhiwat;

	if (unp == 0) {
		error = EINVAL;
		goto release;
	}
	if (flags & PRUS_OOB) {
		error = EOPNOTSUPP;
		goto release;
	}

	if (control && (error = unp_internalize(control, p)))
		goto release;

	switch (so->so_type) {
	case SOCK_DGRAM: 
	{
		struct sockaddr *from;

		if (nam) {
			if (unp->unp_conn) {
				error = EISCONN;
				break;
			}
			error = unp_connect(so, nam, p);
			if (error)
				break;
		} else {
			if (unp->unp_conn == 0) {
				error = ENOTCONN;
				break;
			}
		}
		so2 = unp->unp_conn->unp_socket;
		if (unp->unp_addr)
			from = (struct sockaddr *)unp->unp_addr;
		else
			from = &sun_noname;
		if (sbappendaddr(&so2->so_rcv, from, m, control)) {
			sorwakeup(so2);
			m = 0;
			control = 0;
		} else
			error = ENOBUFS;
		if (nam)
			unp_disconnect(unp);
		break;
	}

	case SOCK_STREAM:
		/* Connect if not connected yet. */
		/*
		 * Note: A better implementation would complain
		 * if not equal to the peer's address.
		 */
		if ((so->so_state & SS_ISCONNECTED) == 0) {
			if (nam) {
				error = unp_connect(so, nam, p);
				if (error)
					break;	/* XXX */
			} else {
				error = ENOTCONN;
				break;
			}
		}

		if (so->so_state & SS_CANTSENDMORE) {
			error = EPIPE;
			break;
		}
		if (unp->unp_conn == 0)
			panic("uipc_send connected but no connection?");
		so2 = unp->unp_conn->unp_socket;
		/*
		 * Send to paired receive port, and then reduce
		 * send buffer hiwater marks to maintain backpressure.
		 * Wake up readers.
		 */
		if (control) {
			if (sbappendcontrol(&so2->so_rcv, m, control))
				control = 0;
		} else
			sbappend(&so2->so_rcv, m);
		so->so_snd.sb_mbmax -=
			so2->so_rcv.sb_mbcnt - unp->unp_conn->unp_mbcnt;
		unp->unp_conn->unp_mbcnt = so2->so_rcv.sb_mbcnt;
		newhiwat = so->so_snd.sb_hiwat -
		    (so2->so_rcv.sb_cc - unp->unp_conn->unp_cc);
		(void)chgsbsize(so->so_cred->cr_uidinfo, &so->so_snd.sb_hiwat,
		    newhiwat, RLIM_INFINITY);
		unp->unp_conn->unp_cc = so2->so_rcv.sb_cc;
		sorwakeup(so2);
		m = 0;
		break;

	default:
		panic("uipc_send unknown socktype");
	}

	/*
	 * SEND_EOF is equivalent to a SEND followed by
	 * a SHUTDOWN.
	 */
	if (flags & PRUS_EOF) {
		socantsendmore(so);
		unp_shutdown(unp);
	}

	if (control && error != 0)
		unp_dispose(control);

release:
	if (control)
		m_freem(control);
	if (m)
		m_freem(m);
	return error;
}

static int
uipc_sense(struct socket *so, struct stat *sb)
{
	struct unpcb *unp = sotounpcb(so);
	struct socket *so2;

	if (unp == 0)
		return EINVAL;
	sb->st_blksize = so->so_snd.sb_hiwat;
	if (so->so_type == SOCK_STREAM && unp->unp_conn != 0) {
		so2 = unp->unp_conn->unp_socket;
		sb->st_blksize += so2->so_rcv.sb_cc;
	}
	sb->st_dev = NOUDEV;
	if (unp->unp_ino == 0)		/* make up a non-zero inode number */
		unp->unp_ino = (++unp_ino == 0) ? ++unp_ino : unp_ino;
	sb->st_ino = unp->unp_ino;
	return (0);
}

static int
uipc_shutdown(struct socket *so)
{
	struct unpcb *unp = sotounpcb(so);

	if (unp == 0)
		return EINVAL;
	socantsendmore(so);
	unp_shutdown(unp);
	return 0;
}

static int
uipc_sockaddr(struct socket *so, struct sockaddr **nam)
{
	struct unpcb *unp = sotounpcb(so);

	if (unp == 0)
		return EINVAL;
	if (unp->unp_addr)
		*nam = dup_sockaddr((struct sockaddr *)unp->unp_addr, 1);
	return 0;
}

struct pr_usrreqs uipc_usrreqs = {
	uipc_abort, uipc_accept, uipc_attach, uipc_bind, uipc_connect,
	uipc_connect2, pru_control_notsupp, uipc_detach, uipc_disconnect,
	uipc_listen, uipc_peeraddr, uipc_rcvd, pru_rcvoob_notsupp,
	uipc_send, uipc_sense, uipc_shutdown, uipc_sockaddr,
	sosend, soreceive, sopoll
};

int
uipc_ctloutput(so, sopt)
	struct socket *so;
	struct sockopt *sopt;
{
	struct unpcb *unp = sotounpcb(so);
	int error;

	switch (sopt->sopt_dir) {
	case SOPT_GET:
		switch (sopt->sopt_name) {
		case LOCAL_PEERCRED:
			if (unp->unp_flags & UNP_HAVEPC)
				error = sooptcopyout(sopt, &unp->unp_peercred,
				    sizeof(unp->unp_peercred));
			else {
				if (so->so_type == SOCK_STREAM)
					error = ENOTCONN;
				else
					error = EINVAL;
			}
			break;
		default:
			error = EOPNOTSUPP;
			break;
		}
		break;
	case SOPT_SET:
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}
	
/*
 * Both send and receive buffers are allocated PIPSIZ bytes of buffering
 * for stream sockets, although the total for sender and receiver is
 * actually only PIPSIZ.
 * Datagram sockets really use the sendspace as the maximum datagram size,
 * and don't really want to reserve the sendspace.  Their recvspace should
 * be large enough for at least one max-size datagram plus address.
 */
#ifndef PIPSIZ
#define	PIPSIZ	8192
#endif
static u_long	unpst_sendspace = PIPSIZ;
static u_long	unpst_recvspace = PIPSIZ;
static u_long	unpdg_sendspace = 2*1024;	/* really max datagram size */
static u_long	unpdg_recvspace = 4*1024;

static int	unp_rights;			/* file descriptors in flight */

SYSCTL_DECL(_net_local_stream);
SYSCTL_INT(_net_local_stream, OID_AUTO, sendspace, CTLFLAG_RW, 
	   &unpst_sendspace, 0, "");
SYSCTL_INT(_net_local_stream, OID_AUTO, recvspace, CTLFLAG_RW,
	   &unpst_recvspace, 0, "");
SYSCTL_DECL(_net_local_dgram);
SYSCTL_INT(_net_local_dgram, OID_AUTO, maxdgram, CTLFLAG_RW,
	   &unpdg_sendspace, 0, "");
SYSCTL_INT(_net_local_dgram, OID_AUTO, recvspace, CTLFLAG_RW,
	   &unpdg_recvspace, 0, "");
SYSCTL_DECL(_net_local);
SYSCTL_INT(_net_local, OID_AUTO, inflight, CTLFLAG_RD, &unp_rights, 0, "");

static int
unp_attach(so)
	struct socket *so;
{
	register struct unpcb *unp;
	int error;

	if (so->so_snd.sb_hiwat == 0 || so->so_rcv.sb_hiwat == 0) {
		switch (so->so_type) {

		case SOCK_STREAM:
			error = soreserve(so, unpst_sendspace, unpst_recvspace);
			break;

		case SOCK_DGRAM:
			error = soreserve(so, unpdg_sendspace, unpdg_recvspace);
			break;

		default:
			panic("unp_attach");
		}
		if (error)
			return (error);
	}
	unp = zalloc(unp_zone);
	if (unp == NULL)
		return (ENOBUFS);
	bzero(unp, sizeof *unp);
	unp->unp_gencnt = ++unp_gencnt;
	unp_count++;
	LIST_INIT(&unp->unp_refs);
	unp->unp_socket = so;
	unp->unp_rvnode = curproc->p_fd->fd_rdir;
	LIST_INSERT_HEAD(so->so_type == SOCK_DGRAM ? &unp_dhead
			 : &unp_shead, unp, unp_link);
	so->so_pcb = (caddr_t)unp;
	return (0);
}

static void
unp_detach(unp)
	register struct unpcb *unp;
{
	LIST_REMOVE(unp, unp_link);
	unp->unp_gencnt = ++unp_gencnt;
	--unp_count;
	if (unp->unp_vnode) {
		unp->unp_vnode->v_socket = 0;
		vrele(unp->unp_vnode);
		unp->unp_vnode = 0;
	}
	if (unp->unp_conn)
		unp_disconnect(unp);
	while (!LIST_EMPTY(&unp->unp_refs))
		unp_drop(LIST_FIRST(&unp->unp_refs), ECONNRESET);
	soisdisconnected(unp->unp_socket);
	unp->unp_socket->so_pcb = 0;
	if (unp_rights) {
		/*
		 * Normally the receive buffer is flushed later,
		 * in sofree, but if our receive buffer holds references
		 * to descriptors that are now garbage, we will dispose
		 * of those descriptor references after the garbage collector
		 * gets them (resulting in a "panic: closef: count < 0").
		 */
		sorflush(unp->unp_socket);
		unp_gc();
	}
	if (unp->unp_addr)
		FREE(unp->unp_addr, M_SONAME);
	zfree(unp_zone, unp);
}

static int
unp_bind(unp, nam, p)
	struct unpcb *unp;
	struct sockaddr *nam;
	struct proc *p;
{
	struct sockaddr_un *soun = (struct sockaddr_un *)nam;
	register struct vnode *vp;
	struct vattr vattr;
	int error, namelen;
	struct nameidata nd;
	char buf[SOCK_MAXADDRLEN];

	if (unp->unp_vnode != NULL)
		return (EINVAL);
	namelen = soun->sun_len - offsetof(struct sockaddr_un, sun_path);
	if (namelen <= 0)
		return EINVAL;
	strncpy(buf, soun->sun_path, namelen);
	buf[namelen] = 0;	/* null-terminate the string */
	NDINIT(&nd, CREATE, NOFOLLOW | LOCKPARENT, UIO_SYSSPACE,
	    buf, p);
/* SHOULD BE ABLE TO ADOPT EXISTING AND wakeup() ALA FIFO's */
	error = namei(&nd);
	if (error)
		return (error);
	vp = nd.ni_vp;
	if (vp != NULL) {
		NDFREE(&nd, NDF_ONLY_PNBUF);
		if (nd.ni_dvp == vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		vrele(vp);
		return (EADDRINUSE);
	}
	VATTR_NULL(&vattr);
	vattr.va_type = VSOCK;
	vattr.va_mode = (ACCESSPERMS & ~p->p_fd->fd_cmask);
	VOP_LEASE(nd.ni_dvp, p, p->p_ucred, LEASE_WRITE);
	error = VOP_CREATE(nd.ni_dvp, &nd.ni_vp, &nd.ni_cnd, &vattr);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vput(nd.ni_dvp);
	if (error)
		return (error);
	vp = nd.ni_vp;
	vp->v_socket = unp->unp_socket;
	unp->unp_vnode = vp;
	unp->unp_addr = (struct sockaddr_un *)dup_sockaddr(nam, 1);
	VOP_UNLOCK(vp, 0, p);
	return (0);
}

static int
unp_connect(so, nam, p)
	struct socket *so;
	struct sockaddr *nam;
	struct proc *p;
{
	register struct sockaddr_un *soun = (struct sockaddr_un *)nam;
	register struct vnode *vp;
	register struct socket *so2, *so3;
	struct unpcb *unp, *unp2, *unp3;
	int error, len;
	struct nameidata nd;
	char buf[SOCK_MAXADDRLEN];

	len = nam->sa_len - offsetof(struct sockaddr_un, sun_path);
	if (len <= 0)
		return EINVAL;
	strncpy(buf, soun->sun_path, len);
	buf[len] = 0;

	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF, UIO_SYSSPACE, buf, p);
	error = namei(&nd);
	if (error)
		return (error);
	vp = nd.ni_vp;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (vp->v_type != VSOCK) {
		error = ENOTSOCK;
		goto bad;
	}
	error = VOP_ACCESS(vp, VWRITE, p->p_ucred, p);
	if (error)
		goto bad;
	so2 = vp->v_socket;
	if (so2 == 0) {
		error = ECONNREFUSED;
		goto bad;
	}
	if (so->so_type != so2->so_type) {
		error = EPROTOTYPE;
		goto bad;
	}
	if (so->so_proto->pr_flags & PR_CONNREQUIRED) {
		if ((so2->so_options & SO_ACCEPTCONN) == 0 ||
		    (so3 = sonewconn3(so2, 0, p)) == 0) {
			error = ECONNREFUSED;
			goto bad;
		}
		unp = sotounpcb(so);
		unp2 = sotounpcb(so2);
		unp3 = sotounpcb(so3);
		if (unp2->unp_addr)
			unp3->unp_addr = (struct sockaddr_un *)
				dup_sockaddr((struct sockaddr *)
					     unp2->unp_addr, 1);

		/*
		 * unp_peercred management:
		 *
		 * The connecter's (client's) credentials are copied
		 * from its process structure at the time of connect()
		 * (which is now).
		 */
		cru2x(p->p_ucred, &unp3->unp_peercred);
		unp3->unp_flags |= UNP_HAVEPC;
		/*
		 * The receiver's (server's) credentials are copied
		 * from the unp_peercred member of socket on which the
		 * former called listen(); unp_listen() cached that
		 * process's credentials at that time so we can use
		 * them now.
		 */
		KASSERT(unp2->unp_flags & UNP_HAVEPCCACHED,
		    ("unp_connect: listener without cached peercred"));
		memcpy(&unp->unp_peercred, &unp2->unp_peercred,
		    sizeof(unp->unp_peercred));
		unp->unp_flags |= UNP_HAVEPC;

		so2 = so3;
	}
	error = unp_connect2(so, so2);
bad:
	vput(vp);
	return (error);
}

int
unp_connect2(so, so2)
	register struct socket *so;
	register struct socket *so2;
{
	register struct unpcb *unp = sotounpcb(so);
	register struct unpcb *unp2;

	if (so2->so_type != so->so_type)
		return (EPROTOTYPE);
	unp2 = sotounpcb(so2);
	unp->unp_conn = unp2;
	switch (so->so_type) {

	case SOCK_DGRAM:
		LIST_INSERT_HEAD(&unp2->unp_refs, unp, unp_reflink);
		soisconnected(so);
		break;

	case SOCK_STREAM:
		unp2->unp_conn = unp;
		soisconnected(so);
		soisconnected(so2);
		break;

	default:
		panic("unp_connect2");
	}
	return (0);
}

static void
unp_disconnect(unp)
	struct unpcb *unp;
{
	register struct unpcb *unp2 = unp->unp_conn;

	if (unp2 == 0)
		return;
	unp->unp_conn = 0;
	switch (unp->unp_socket->so_type) {

	case SOCK_DGRAM:
		LIST_REMOVE(unp, unp_reflink);
		unp->unp_socket->so_state &= ~SS_ISCONNECTED;
		break;

	case SOCK_STREAM:
		soisdisconnected(unp->unp_socket);
		unp2->unp_conn = 0;
		soisdisconnected(unp2->unp_socket);
		break;
	}
}

#ifdef notdef
void
unp_abort(unp)
	struct unpcb *unp;
{

	unp_detach(unp);
}
#endif

static int
prison_unpcb(struct proc *p, struct unpcb *unp)
{
	if (!p->p_prison)
		return (0);
	if (p->p_fd->fd_rdir == unp->unp_rvnode)
		return (0);
	return (1);
}

static int
unp_pcblist(SYSCTL_HANDLER_ARGS)
{
	int error, i, n;
	struct unpcb *unp, **unp_list;
	unp_gen_t gencnt;
	struct xunpgen xug;
	struct unp_head *head;

	head = ((intptr_t)arg1 == SOCK_DGRAM ? &unp_dhead : &unp_shead);

	/*
	 * The process of preparing the PCB list is too time-consuming and
	 * resource-intensive to repeat twice on every request.
	 */
	if (req->oldptr == 0) {
		n = unp_count;
		req->oldidx = 2 * (sizeof xug)
			+ (n + n/8) * sizeof(struct xunpcb);
		return 0;
	}

	if (req->newptr != 0)
		return EPERM;

	/*
	 * OK, now we're committed to doing something.
	 */
	gencnt = unp_gencnt;
	n = unp_count;

	xug.xug_len = sizeof xug;
	xug.xug_count = n;
	xug.xug_gen = gencnt;
	xug.xug_sogen = so_gencnt;
	error = SYSCTL_OUT(req, &xug, sizeof xug);
	if (error)
		return error;

	unp_list = malloc(n * sizeof *unp_list, M_TEMP, M_WAITOK);
	if (unp_list == 0)
		return ENOMEM;
	
	for (unp = LIST_FIRST(head), i = 0; unp && i < n;
	     unp = LIST_NEXT(unp, unp_link)) {
		if (unp->unp_gencnt <= gencnt && !prison_unpcb(req->p, unp))
			unp_list[i++] = unp;
	}
	n = i;			/* in case we lost some during malloc */

	error = 0;
	for (i = 0; i < n; i++) {
		unp = unp_list[i];
		if (unp->unp_gencnt <= gencnt) {
			struct xunpcb xu;
			xu.xu_len = sizeof xu;
			xu.xu_unpp = unp;
			/*
			 * XXX - need more locking here to protect against
			 * connect/disconnect races for SMP.
			 */
			if (unp->unp_addr)
				bcopy(unp->unp_addr, &xu.xu_addr, 
				      unp->unp_addr->sun_len);
			if (unp->unp_conn && unp->unp_conn->unp_addr)
				bcopy(unp->unp_conn->unp_addr,
				      &xu.xu_caddr,
				      unp->unp_conn->unp_addr->sun_len);
			bcopy(unp, &xu.xu_unp, sizeof *unp);
			sotoxsocket(unp->unp_socket, &xu.xu_socket);
			error = SYSCTL_OUT(req, &xu, sizeof xu);
		}
	}
	if (!error) {
		/*
		 * Give the user an updated idea of our state.
		 * If the generation differs from what we told
		 * her before, she knows that something happened
		 * while we were processing this request, and it
		 * might be necessary to retry.
		 */
		xug.xug_gen = unp_gencnt;
		xug.xug_sogen = so_gencnt;
		xug.xug_count = unp_count;
		error = SYSCTL_OUT(req, &xug, sizeof xug);
	}
	free(unp_list, M_TEMP);
	return error;
}

SYSCTL_PROC(_net_local_dgram, OID_AUTO, pcblist, CTLFLAG_RD, 
	    (caddr_t)(long)SOCK_DGRAM, 0, unp_pcblist, "S,xunpcb",
	    "List of active local datagram sockets");
SYSCTL_PROC(_net_local_stream, OID_AUTO, pcblist, CTLFLAG_RD, 
	    (caddr_t)(long)SOCK_STREAM, 0, unp_pcblist, "S,xunpcb",
	    "List of active local stream sockets");

static void
unp_shutdown(unp)
	struct unpcb *unp;
{
	struct socket *so;

	if (unp->unp_socket->so_type == SOCK_STREAM && unp->unp_conn &&
	    (so = unp->unp_conn->unp_socket))
		socantrcvmore(so);
}

static void
unp_drop(unp, errno)
	struct unpcb *unp;
	int errno;
{
	struct socket *so = unp->unp_socket;

	so->so_error = errno;
	unp_disconnect(unp);
}

#ifdef notdef
void
unp_drain()
{

}
#endif

int
unp_externalize(rights)
	struct mbuf *rights;
{
	struct proc *p = curproc;		/* XXX */
	register int i;
	register struct cmsghdr *cm = mtod(rights, struct cmsghdr *);
	register int *fdp;
	register struct file **rp;
	register struct file *fp;
	int newfds = (cm->cmsg_len - (CMSG_DATA(cm) - (u_char *)cm))
		/ sizeof (struct file *);
	int f;

	/*
	 * if the new FD's will not fit, then we free them all
	 */
	if (!fdavail(p, newfds)) {
		rp = (struct file **)CMSG_DATA(cm);
		for (i = 0; i < newfds; i++) {
			fp = *rp;
			/*
			 * zero the pointer before calling unp_discard,
			 * since it may end up in unp_gc()..
			 */
			*rp++ = 0;
			unp_discard(fp);
		}
		return (EMSGSIZE);
	}
	/*
	 * now change each pointer to an fd in the global table to 
	 * an integer that is the index to the local fd table entry
	 * that we set up to point to the global one we are transferring.
	 * If sizeof (struct file *) is bigger than or equal to sizeof int,
	 * then do it in forward order. In that case, an integer will
	 * always come in the same place or before its corresponding
	 * struct file pointer.
	 * If sizeof (struct file *) is smaller than sizeof int, then
	 * do it in reverse order.
	 */
	if (sizeof (struct file *) >= sizeof (int)) {
		fdp = (int *)(cm + 1);
		rp = (struct file **)CMSG_DATA(cm);
		for (i = 0; i < newfds; i++) {
			if (fdalloc(p, 0, &f))
				panic("unp_externalize");
			fp = *rp++;
			p->p_fd->fd_ofiles[f] = fp;
			fp->f_msgcount--;
			unp_rights--;
			*fdp++ = f;
		}
	} else {
		fdp = (int *)(cm + 1) + newfds - 1;
		rp = (struct file **)CMSG_DATA(cm) + newfds - 1;
		for (i = 0; i < newfds; i++) {
			if (fdalloc(p, 0, &f))
				panic("unp_externalize");
			fp = *rp--;
			p->p_fd->fd_ofiles[f] = fp;
			fp->f_msgcount--;
			unp_rights--;
			*fdp-- = f;
		}
	}

	/*
	 * Adjust length, in case sizeof(struct file *) and sizeof(int)
	 * differs.
	 */
	cm->cmsg_len = CMSG_LEN(newfds * sizeof(int));
	rights->m_len = cm->cmsg_len;
	return (0);
}

void
unp_init(void)
{
	unp_zone = zinit("unpcb", sizeof(struct unpcb), nmbclusters, 0, 0);
	if (unp_zone == 0)
		panic("unp_init");
	LIST_INIT(&unp_dhead);
	LIST_INIT(&unp_shead);
}

#ifndef MIN
#define	MIN(a,b) (((a)<(b))?(a):(b))
#endif

static int
unp_internalize(control, p)
	struct mbuf *control;
	struct proc *p;
{
	struct filedesc *fdescp = p->p_fd;
	register struct cmsghdr *cm = mtod(control, struct cmsghdr *);
	register struct file **rp;
	register struct file *fp;
	register int i, fd, *fdp;
	register struct cmsgcred *cmcred;
	int oldfds;
	u_int newlen;

	if ((cm->cmsg_type != SCM_RIGHTS && cm->cmsg_type != SCM_CREDS) ||
	    cm->cmsg_level != SOL_SOCKET || cm->cmsg_len != control->m_len)
		return (EINVAL);

	/*
	 * Fill in credential information.
	 */
	if (cm->cmsg_type == SCM_CREDS) {
		cmcred = (struct cmsgcred *)(cm + 1);
		cmcred->cmcred_pid = p->p_pid;
		cmcred->cmcred_uid = p->p_cred->p_ruid;
		cmcred->cmcred_gid = p->p_cred->p_rgid;
		cmcred->cmcred_euid = p->p_ucred->cr_uid;
		cmcred->cmcred_ngroups = MIN(p->p_ucred->cr_ngroups,
							CMGROUP_MAX);
		for (i = 0; i < cmcred->cmcred_ngroups; i++)
			cmcred->cmcred_groups[i] = p->p_ucred->cr_groups[i];
		return(0);
	}

	oldfds = (cm->cmsg_len - sizeof (*cm)) / sizeof (int);
	/*
	 * check that all the FDs passed in refer to legal OPEN files
	 * If not, reject the entire operation.
	 */
	fdp = (int *)(cm + 1);
	for (i = 0; i < oldfds; i++) {
		fd = *fdp++;
		if ((unsigned)fd >= fdescp->fd_nfiles ||
		    fdescp->fd_ofiles[fd] == NULL)
			return (EBADF);
		if (fdescp->fd_ofiles[fd]->f_type == DTYPE_KQUEUE)
			return (EOPNOTSUPP);
	}
	/*
	 * Now replace the integer FDs with pointers to
	 * the associated global file table entry..
	 * Allocate a bigger buffer as necessary. But if an cluster is not
	 * enough, return E2BIG.
	 */
	newlen = CMSG_LEN(oldfds * sizeof(struct file *));
	if (newlen > MCLBYTES)
		return (E2BIG);
	if (newlen - control->m_len > M_TRAILINGSPACE(control)) {
		if (control->m_flags & M_EXT)
			return (E2BIG);
		MCLGET(control, M_WAIT);
		if ((control->m_flags & M_EXT) == 0)
			return (ENOBUFS);

		/* copy the data to the cluster */
		memcpy(mtod(control, char *), cm, cm->cmsg_len);
		cm = mtod(control, struct cmsghdr *);
	}

	/*
	 * Adjust length, in case sizeof(struct file *) and sizeof(int)
	 * differs.
	 */
	control->m_len = cm->cmsg_len = newlen;

	/*
	 * Transform the file descriptors into struct file pointers.
	 * If sizeof (struct file *) is bigger than or equal to sizeof int,
	 * then do it in reverse order so that the int won't get until
	 * we're done.
	 * If sizeof (struct file *) is smaller than sizeof int, then
	 * do it in forward order.
	 */
	if (sizeof (struct file *) >= sizeof (int)) {
		fdp = (int *)(cm + 1) + oldfds - 1;
		rp = (struct file **)CMSG_DATA(cm) + oldfds - 1;
		for (i = 0; i < oldfds; i++) {
			fp = fdescp->fd_ofiles[*fdp--];
			*rp-- = fp;
			fp->f_count++;
			fp->f_msgcount++;
			unp_rights++;
		}
	} else {
		fdp = (int *)(cm + 1);
		rp = (struct file **)CMSG_DATA(cm);
		for (i = 0; i < oldfds; i++) {
			fp = fdescp->fd_ofiles[*fdp++];
			*rp++ = fp;
			fp->f_count++;
			fp->f_msgcount++;
			unp_rights++;
		}
	}
	return (0);
}

static int	unp_defer, unp_gcing;

static void
unp_gc()
{
	register struct file *fp, *nextfp;
	register struct socket *so;
	struct file **extra_ref, **fpp;
	int nunref, i;

	if (unp_gcing)
		return;
	unp_gcing = 1;
	unp_defer = 0;
	/* 
	 * before going through all this, set all FDs to 
	 * be NOT defered and NOT externally accessible
	 */
	LIST_FOREACH(fp, &filehead, f_list)
		fp->f_flag &= ~(FMARK|FDEFER);
	do {
		LIST_FOREACH(fp, &filehead, f_list) {
			/*
			 * If the file is not open, skip it
			 */
			if (fp->f_count == 0)
				continue;
			/*
			 * If we already marked it as 'defer'  in a
			 * previous pass, then try process it this time
			 * and un-mark it
			 */
			if (fp->f_flag & FDEFER) {
				fp->f_flag &= ~FDEFER;
				unp_defer--;
			} else {
				/*
				 * if it's not defered, then check if it's
				 * already marked.. if so skip it
				 */
				if (fp->f_flag & FMARK)
					continue;
				/* 
				 * If all references are from messages
				 * in transit, then skip it. it's not 
				 * externally accessible.
				 */ 
				if (fp->f_count == fp->f_msgcount)
					continue;
				/* 
				 * If it got this far then it must be
				 * externally accessible.
				 */
				fp->f_flag |= FMARK;
			}
			/*
			 * either it was defered, or it is externally 
			 * accessible and not already marked so.
			 * Now check if it is possibly one of OUR sockets.
			 */ 
			if (fp->f_type != DTYPE_SOCKET ||
			    (so = (struct socket *)fp->f_data) == 0)
				continue;
			if (so->so_proto->pr_domain != &localdomain ||
			    (so->so_proto->pr_flags&PR_RIGHTS) == 0)
				continue;
#ifdef notdef
			if (so->so_rcv.sb_flags & SB_LOCK) {
				/*
				 * This is problematical; it's not clear
				 * we need to wait for the sockbuf to be
				 * unlocked (on a uniprocessor, at least),
				 * and it's also not clear what to do
				 * if sbwait returns an error due to receipt
				 * of a signal.  If sbwait does return
				 * an error, we'll go into an infinite
				 * loop.  Delete all of this for now.
				 */
				(void) sbwait(&so->so_rcv);
				goto restart;
			}
#endif
			/*
			 * So, Ok, it's one of our sockets and it IS externally
			 * accessible (or was defered). Now we look
			 * to see if we hold any file descriptors in its
			 * message buffers. Follow those links and mark them 
			 * as accessible too.
			 */
			unp_scan(so->so_rcv.sb_mb, unp_mark);
		}
	} while (unp_defer);
	/*
	 * We grab an extra reference to each of the file table entries
	 * that are not otherwise accessible and then free the rights
	 * that are stored in messages on them.
	 *
	 * The bug in the orginal code is a little tricky, so I'll describe
	 * what's wrong with it here.
	 *
	 * It is incorrect to simply unp_discard each entry for f_msgcount
	 * times -- consider the case of sockets A and B that contain
	 * references to each other.  On a last close of some other socket,
	 * we trigger a gc since the number of outstanding rights (unp_rights)
	 * is non-zero.  If during the sweep phase the gc code un_discards,
	 * we end up doing a (full) closef on the descriptor.  A closef on A
	 * results in the following chain.  Closef calls soo_close, which
	 * calls soclose.   Soclose calls first (through the switch
	 * uipc_usrreq) unp_detach, which re-invokes unp_gc.  Unp_gc simply
	 * returns because the previous instance had set unp_gcing, and
	 * we return all the way back to soclose, which marks the socket
	 * with SS_NOFDREF, and then calls sofree.  Sofree calls sorflush
	 * to free up the rights that are queued in messages on the socket A,
	 * i.e., the reference on B.  The sorflush calls via the dom_dispose
	 * switch unp_dispose, which unp_scans with unp_discard.  This second
	 * instance of unp_discard just calls closef on B.
	 *
	 * Well, a similar chain occurs on B, resulting in a sorflush on B,
	 * which results in another closef on A.  Unfortunately, A is already
	 * being closed, and the descriptor has already been marked with
	 * SS_NOFDREF, and soclose panics at this point.
	 *
	 * Here, we first take an extra reference to each inaccessible
	 * descriptor.  Then, we call sorflush ourself, since we know
	 * it is a Unix domain socket anyhow.  After we destroy all the
	 * rights carried in messages, we do a last closef to get rid
	 * of our extra reference.  This is the last close, and the
	 * unp_detach etc will shut down the socket.
	 *
	 * 91/09/19, bsy@cs.cmu.edu
	 */
	extra_ref = malloc(nfiles * sizeof(struct file *), M_FILE, M_WAITOK);
	for (nunref = 0, fp = LIST_FIRST(&filehead), fpp = extra_ref; fp != 0;
	    fp = nextfp) {
		nextfp = LIST_NEXT(fp, f_list);
		/* 
		 * If it's not open, skip it
		 */
		if (fp->f_count == 0)
			continue;
		/* 
		 * If all refs are from msgs, and it's not marked accessible
		 * then it must be referenced from some unreachable cycle
		 * of (shut-down) FDs, so include it in our
		 * list of FDs to remove
		 */
		if (fp->f_count == fp->f_msgcount && !(fp->f_flag & FMARK)) {
			*fpp++ = fp;
			nunref++;
			fp->f_count++;
		}
	}
	/* 
	 * for each FD on our hit list, do the following two things
	 */
	for (i = nunref, fpp = extra_ref; --i >= 0; ++fpp) {
		struct file *tfp = *fpp;
		if (tfp->f_type == DTYPE_SOCKET && tfp->f_data != NULL)
			sorflush((struct socket *)(tfp->f_data));
	}
	for (i = nunref, fpp = extra_ref; --i >= 0; ++fpp)
		closef(*fpp, (struct proc *) NULL);
	free((caddr_t)extra_ref, M_FILE);
	unp_gcing = 0;
}

void
unp_dispose(m)
	struct mbuf *m;
{

	if (m)
		unp_scan(m, unp_discard);
}

static int
unp_listen(unp, p)
	struct unpcb *unp;
	struct proc *p;
{

	cru2x(p->p_ucred, &unp->unp_peercred);
	unp->unp_flags |= UNP_HAVEPCCACHED;
	return (0);
}

static void
unp_scan(m0, op)
	register struct mbuf *m0;
	void (*op) __P((struct file *));
{
	register struct mbuf *m;
	register struct file **rp;
	register struct cmsghdr *cm;
	register int i;
	int qfds;

	while (m0) {
		for (m = m0; m; m = m->m_next)
			if (m->m_type == MT_CONTROL &&
			    m->m_len >= sizeof(*cm)) {
				cm = mtod(m, struct cmsghdr *);
				if (cm->cmsg_level != SOL_SOCKET ||
				    cm->cmsg_type != SCM_RIGHTS)
					continue;
				qfds = (cm->cmsg_len -
					(CMSG_DATA(cm) - (u_char *)cm))
						/ sizeof (struct file *);
				rp = (struct file **)CMSG_DATA(cm);
				for (i = 0; i < qfds; i++)
					(*op)(*rp++);
				break;		/* XXX, but saves time */
			}
		m0 = m0->m_act;
	}
}

static void
unp_mark(fp)
	struct file *fp;
{

	if (fp->f_flag & FMARK)
		return;
	unp_defer++;
	fp->f_flag |= (FMARK|FDEFER);
}

static void
unp_discard(fp)
	struct file *fp;
{

	fp->f_msgcount--;
	unp_rights--;
	(void) closef(fp, (struct proc *)NULL);
}
