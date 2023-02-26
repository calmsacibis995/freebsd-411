/*
 * Copyright (c) 1999, Boris Popov
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
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * $FreeBSD: src/sys/netncp/ncp_sock.c,v 1.2 1999/10/12 10:36:59 bp Exp $
 *
 * Low level socket routines
 */

#include "opt_inet.h"
#include "opt_ipx.h"

#if !defined(INET) && !defined(IPX)
#error "NCP requeires either INET of IPX protocol family"
#endif

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <sys/kernel.h>
#include <sys/uio.h>
#include <sys/syslog.h>
#include <sys/mbuf.h>
#include <net/route.h>

#ifdef IPX
#include <netipx/ipx.h>
#include <netipx/ipx_pcb.h>
#endif

#include <netncp/ncp.h>
#include <netncp/ncp_conn.h>
#include <netncp/ncp_sock.h>
#include <netncp/ncp_subr.h>
#include <netncp/ncp_rq.h>

#ifdef IPX
#define ipx_setnullnet(x) ((x).x_net.s_net[0]=0); ((x).x_net.s_net[1]=0);
#define ipx_setnullhost(x) ((x).x_host.s_host[0] = 0); \
	((x).x_host.s_host[1] = 0); ((x).x_host.s_host[2] = 0);
#endif

/*int ncp_poll(struct socket *so, int events);*/
/*static int ncp_getsockname(struct socket *so, caddr_t asa, int *alen);*/
static int ncp_soconnect(struct socket *so,struct sockaddr *target, struct proc *p);


/* This will need only if native IP used, or (unlikely) NCP will be
 * implemented on the socket level
 */
static int
ncp_soconnect(struct socket *so,struct sockaddr *target, struct proc *p) {
	int error,s;

	error = soconnect(so, (struct sockaddr*)target, p);
	if (error)
		return error;
	/*
	 * Wait for the connection to complete. Cribbed from the
	 * connect system call but with the wait timing out so
	 * that interruptible mounts don't hang here for a long time.
	 */
	error = EIO;
	s = splnet();
	while ((so->so_state & SS_ISCONNECTING) && so->so_error == 0) {
		(void) tsleep((caddr_t)&so->so_timeo, PSOCK, "ncpcon", 2 * hz);
		if ((so->so_state & SS_ISCONNECTING) &&
		    so->so_error == 0 /*&& rep &&*/) {
			so->so_state &= ~SS_ISCONNECTING;
			splx(s);
			goto bad;
		}
	}
	if (so->so_error) {
		error = so->so_error;
		so->so_error = 0;
		splx(s);
		goto bad;
	}
		splx(s);
	error=0;
bad:
	return error;
}
#ifdef notyet
static int
ncp_getsockname(struct socket *so, caddr_t asa, int *alen) {
	struct sockaddr *sa;
	int len=0, error;

	sa = 0;
	error = (*so->so_proto->pr_usrreqs->pru_sockaddr)(so, &sa);
	if (error==0) {
		if (sa) {
			len = min(len, sa->sa_len);
			bcopy(sa, (caddr_t)asa, (u_int)len);
		}
		*alen=len;
	}
	if (sa)
		FREE(sa, M_SONAME);
	return (error);
}
#endif
int ncp_sock_recv(struct socket *so, struct mbuf **mp, int *rlen)
{
	struct uio auio;
	struct proc *p=curproc; /* XXX */
	int error,flags,len;
	
	auio.uio_resid = len = 1000000;
	auio.uio_procp = p;
	flags = MSG_DONTWAIT;

/*	error = so->so_proto->pr_usrreqs->pru_soreceive(so, 0, &auio,
	    (struct mbuf **)0, (struct mbuf **)0, &flags);*/
	error = so->so_proto->pr_usrreqs->pru_soreceive(so, 0, &auio,
	    mp, (struct mbuf **)0, &flags);
	*rlen = len - auio.uio_resid;
/*	if (!error) {
	    *rlen=iov.iov_len;
	} else
	    *rlen=0;*/
#ifdef NCP_SOCKET_DEBUG
	if (error)
		printf("ncp_recv: err=%d\n", error);
#endif
	return (error);
}

int
ncp_sock_send(struct socket *so, struct mbuf *top, struct ncp_rq *rqp)
{
	struct proc *p = curproc; /* XXX */
	struct sockaddr *to = 0;
	struct ncp_conn *conn = rqp->conn;
	struct mbuf *m;
	int error, flags=0;
	int sendwait;

	for(;;) {
		m = m_copym(top, 0, M_COPYALL, M_WAIT);
/*		NCPDDEBUG(m);*/
		error = so->so_proto->pr_usrreqs->pru_sosend(so, to, 0, m, 0, flags, p);
		if (error == 0 || error == EINTR || error == ENETDOWN)
			break;
		if (rqp->rexmit == 0) break;
		rqp->rexmit--;
		tsleep(&sendwait, PWAIT, "ncprsn", conn->li.timeout * hz);
		error = ncp_chkintr(conn, p);
		if (error == EINTR) break;
	}
	if (error) {
		log(LOG_INFO, "ncp_send: error %d for server %s", error, conn->li.server);
	}
	return error;
}

int
ncp_poll(struct socket *so, int events){
    struct proc *p = curproc;
    struct ucred *cred=NULL;
    return so->so_proto->pr_usrreqs->pru_sopoll(so, events, cred, p);
}

int
ncp_sock_rselect(struct socket *so,struct proc *p, struct timeval *tv, int events)
{
	struct timeval atv,rtv,ttv;
	int s,timo,error=0;

	if (tv) {
		atv=*tv;
		if (itimerfix(&atv)) {
			error = EINVAL;
			goto done;
		}
		getmicrouptime(&rtv);
		timevaladd(&atv, &rtv);
	}
	timo = 0;
retry:
	p->p_flag |= P_SELECT;
	error = ncp_poll(so, events);
	if (error) {
		error = 0;
		goto done;
	}
	if (tv) {
		getmicrouptime(&rtv);
		if (timevalcmp(&rtv, &atv, >=))
			goto done;
		ttv=atv;
		timevalsub(&ttv, &rtv);
		timo = tvtohz(&ttv);
	}
	s = splhigh();
	if ((p->p_flag & P_SELECT) == 0) {
		splx(s);
		goto retry;
	}
	p->p_flag &= ~P_SELECT;
	error = tsleep((caddr_t)&selwait, PSOCK, "ncpslt", timo);
	splx(s);
done:
	p->p_flag &= ~P_SELECT;
	if (error == ERESTART) {
/*		printf("Signal: %x", CURSIG(p));*/
		error = 0;
	}
	return (error);
}

#ifdef IPX
/* 
 * Connect to specified server via IPX
 */
int
ncp_sock_connect_ipx(struct ncp_conn *conn) {
	struct sockaddr_ipx sipx;
	struct ipxpcb *npcb;
	struct proc *p = conn->procp;
	int addrlen, error, count;

	sipx.sipx_port = htons(0);

	for (count = 0;;count++) {
		if (count > (IPXPORT_WELLKNOWN-IPXPORT_RESERVED)*2) {
			error = EADDRINUSE;
			goto bad;
		}
		conn->ncp_so = conn->wdg_so = NULL;
		checkbad(socreate(AF_IPX, &conn->ncp_so, SOCK_DGRAM, 0, p));
		if (conn->li.opt & NCP_OPT_WDOG)
			checkbad(socreate(AF_IPX, &conn->wdg_so, SOCK_DGRAM,0,p));
		addrlen = sizeof(sipx);
		sipx.sipx_family = AF_IPX;
		ipx_setnullnet(sipx.sipx_addr);
		ipx_setnullhost(sipx.sipx_addr);
		sipx.sipx_len = addrlen;
		error = sobind(conn->ncp_so, (struct sockaddr *)&sipx, p);
		if (error == 0) {
			if ((conn->li.opt & NCP_OPT_WDOG) == 0)
				break;
			sipx.sipx_addr = sotoipxpcb(conn->ncp_so)->ipxp_laddr;
			sipx.sipx_port = htons(ntohs(sipx.sipx_port) + 1);
			ipx_setnullnet(sipx.sipx_addr);
			ipx_setnullhost(sipx.sipx_addr);
			error = sobind(conn->wdg_so, (struct sockaddr *)&sipx, p);
		}
		if (!error) break;
		if (error != EADDRINUSE) goto bad;
		sipx.sipx_port = htons((ntohs(sipx.sipx_port)+4) & 0xfff8);
		soclose(conn->ncp_so);
		if (conn->wdg_so)
			soclose(conn->wdg_so);
	}
	npcb = sotoipxpcb(conn->ncp_so);
	npcb->ipxp_dpt = IPXPROTO_NCP;
	/* IPXrouted must be running, i.e. route must be presented */
	conn->li.ipxaddr.sipx_len = sizeof(struct sockaddr_ipx);
	checkbad(ncp_soconnect(conn->ncp_so, &conn->li.saddr, p));
	if (conn->wdg_so) {
		sotoipxpcb(conn->wdg_so)->ipxp_laddr.x_net = npcb->ipxp_laddr.x_net;
		sotoipxpcb(conn->wdg_so)->ipxp_laddr.x_host= npcb->ipxp_laddr.x_host;
	}
	if (!error) {
		conn->flags |= NCPFL_SOCONN;
	}
#ifdef NCPBURST
	if (ncp_burst_enabled) {
		checkbad(socreate(AF_IPX, &conn->bc_so, SOCK_DGRAM, 0, p));
		bzero(&sipx, sizeof(sipx));
		sipx.sipx_len = sizeof(sipx);
		checkbad(sobind(conn->bc_so, (struct sockaddr *)&sipx, p));
		checkbad(ncp_soconnect(conn->bc_so, &conn->li.saddr, p));
	}
#endif
	if (!error) {
		conn->flags |= NCPFL_SOCONN;
		ncp_sock_checksum(conn, 0);
	}
	return error;
bad:
	ncp_sock_disconnect(conn);
	return (error);
}

int
ncp_sock_checksum(struct ncp_conn *conn, int enable) {

#ifdef SO_IPX_CHECKSUM
	if (enable) {
		sotoipxpcb(conn->ncp_so)->ipxp_flags |= IPXP_CHECKSUM;
	} else {
		sotoipxpcb(conn->ncp_so)->ipxp_flags &= ~IPXP_CHECKSUM;
	}
#endif
	return 0;
}
#endif

#ifdef INET
/* 
 * Connect to specified server via IP
 */
int
ncp_sock_connect_in(struct ncp_conn *conn) {
	struct sockaddr_in sin;
	struct proc *p=conn->procp;
	int addrlen=sizeof(sin), error;

	conn->flags = 0;
	bzero(&sin,addrlen);
	conn->ncp_so = conn->wdg_so = NULL;
	checkbad(socreate(AF_INET, &conn->ncp_so, SOCK_DGRAM, IPPROTO_UDP, p));
	sin.sin_family = AF_INET;
	sin.sin_len = addrlen;
	checkbad(sobind(conn->ncp_so, (struct sockaddr *)&sin, p));
	checkbad(ncp_soconnect(conn->ncp_so,(struct sockaddr*)&conn->li.addr, p));
	if  (!error)
		conn->flags |= NCPFL_SOCONN;
	return error;
bad:
	ncp_sock_disconnect(conn);
	return (error);
}
#endif


/*
 * Connection expected to be locked
 */
int
ncp_sock_disconnect(struct ncp_conn *conn) {
	register struct socket *so;
	conn->flags &= ~(NCPFL_SOCONN | NCPFL_ATTACHED | NCPFL_LOGGED);
	if (conn->ncp_so) {
		so = conn->ncp_so;
		conn->ncp_so = (struct socket *)0;
		soshutdown(so, 2);
		soclose(so);
	}
	if (conn->wdg_so) {
		so = conn->wdg_so;
		conn->wdg_so = (struct socket *)0;
		soshutdown(so, 2);
		soclose(so);
	}
#ifdef NCPBURST
	if (conn->bc_so) {
		so = conn->bc_so;
		conn->bc_so = (struct socket *)NULL;
		soshutdown(so, 2);
		soclose(so);
	}
#endif
	return 0;
}

#ifdef IPX
static void
ncp_watchdog(struct ncp_conn *conn) {
	char *buf;
	struct mbuf *m;
	int error, len, flags;
	struct socket *so;
	struct sockaddr *sa;
	struct uio auio;

	sa = NULL;
	while (conn->wdg_so) { /* not a loop */
		so = conn->wdg_so;
		auio.uio_resid = len = 1000000;
		auio.uio_procp = curproc;
		flags = MSG_DONTWAIT;
		error = so->so_proto->pr_usrreqs->pru_soreceive(so, 
		    (struct sockaddr**)&sa, &auio, &m, (struct mbuf**)0, &flags);
		if (error) break;
		len -= auio.uio_resid;
		NCPSDEBUG("got watch dog %d\n",len);
		if (len != 2) break;
		buf = mtod(m, char*);
		if (buf[1] != '?') break;
		buf[1] = 'Y';
		error = so->so_proto->pr_usrreqs->pru_sosend(so, (struct sockaddr*)sa, 0, m, 0, 0, curproc);
		NCPSDEBUG("send watch dog %d\n",error);
		break;
	}
	if (sa) FREE(sa, M_SONAME);
	return;
}
#endif /* IPX */

void
ncp_check_conn(struct ncp_conn *conn) {
	int s;

	if (conn == NULL || !(conn->flags & NCPFL_ATTACHED))
	        return;
	s = splnet();
	ncp_check_rq(conn);
	splx(s);
#ifdef IPX
	ncp_watchdog(conn);
#endif
}
