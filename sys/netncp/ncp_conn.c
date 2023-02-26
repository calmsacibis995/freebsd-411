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
 * $FreeBSD: src/sys/netncp/ncp_conn.c,v 1.3.2.5 2001/02/22 08:54:11 bp Exp $
 *
 * Connection tables
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/sysctl.h>

#include <netncp/ncp.h>
#include <netncp/ncp_subr.h>
#include <netncp/ncp_conn.h>

SLIST_HEAD(ncp_handle_head,ncp_handle);

int ncp_burst_enabled = 1;

struct ncp_conn_head conn_list={NULL};
static int ncp_conn_cnt = 0;
static int ncp_next_ref = 1;
static struct lock listlock;

struct ncp_handle_head lhlist={NULL};
static int ncp_next_handle = 1;
static struct lock lhlock;

static int ncp_sysctl_connstat(SYSCTL_HANDLER_ARGS);
static int ncp_conn_lock_any(struct ncp_conn *conn, struct proc *p, 
    struct ucred *cred);

extern struct linker_set sysctl_net_ncp;

SYSCTL_DECL(_net_ncp);
SYSCTL_INT (_net_ncp, OID_AUTO, burst_enabled, CTLFLAG_RD, &ncp_burst_enabled, 0, "");
SYSCTL_INT (_net_ncp, OID_AUTO, conn_cnt, CTLFLAG_RD, &ncp_conn_cnt, 0, "");
SYSCTL_PROC(_net_ncp, OID_AUTO, conn_stat, CTLFLAG_RD|CTLTYPE_OPAQUE,
	    NULL, 0, ncp_sysctl_connstat, "S,connstat", "Connections list");

MALLOC_DEFINE(M_NCPDATA, "NCP data", "NCP private data");

int
ncp_conn_init(void) {
	lockinit(&listlock, PSOCK, "ncpll", 0, 0);
	lockinit(&lhlock, PSOCK, "ncplh", 0, 0);
	return 0;
}

int
ncp_conn_locklist(int flags, struct proc *p){
	return lockmgr(&listlock, flags | LK_CANRECURSE, 0, p);
}

void
ncp_conn_unlocklist(struct proc *p){
	lockmgr(&listlock, LK_RELEASE, 0, p);
}

int
ncp_conn_access(struct ncp_conn *conn, struct ucred *cred, mode_t mode) {
	int error;

	if (cred == NOCRED || ncp_suser(cred) == 0 ||
	    cred->cr_uid == conn->nc_owner->cr_uid)
		return 0;
	mode >>= 3;
	if (!groupmember(conn->nc_group, cred))
		mode >>= 3;
	error = (conn->li.access_mode & mode) == mode ? 0 : EACCES;
	return error;
}

int
ncp_conn_lock_any(struct ncp_conn *conn, struct proc *p, struct ucred *cred) {
	int error;

	if (conn->nc_id == 0) return EACCES;
	error = lockmgr(&conn->nc_lock, LK_EXCLUSIVE | LK_CANRECURSE, 0, p);
	if (error == ERESTART)
		return EINTR;
	error = ncp_chkintr(conn, p);
	if (error) {
		lockmgr(&conn->nc_lock, LK_RELEASE, 0, p);
		return error;
	}

	if (conn->nc_id == 0) {
		lockmgr(&conn->nc_lock, LK_RELEASE, 0, p);
		return EACCES;
	}
	conn->procp = p;	/* who currently operates */
	conn->ucred = cred;
	return 0;
}

int
ncp_conn_lock(struct ncp_conn *conn, struct proc *p, struct ucred *cred, int mode) {
	int error;

	error = ncp_conn_access(conn,cred,mode);
	if (error) return error;
	return ncp_conn_lock_any(conn, p, cred);
}

/*
 * Lock conn but unlock connlist
 */
static int
ncp_conn_lock2(struct ncp_conn *conn, struct proc *p, struct ucred *cred, int mode) {
	int error;

	error = ncp_conn_access(conn,cred,mode);
	if (error) {
		ncp_conn_unlocklist(p);
		return error;
	}
	conn->nc_lwant++;
	ncp_conn_unlocklist(p);
	error = ncp_conn_lock_any(conn,p,cred);
	conn->nc_lwant--;
	if (conn->nc_lwant == 0) {
		wakeup(&conn->nc_lwant);
	}
	return error;
}

void
ncp_conn_unlock(struct ncp_conn *conn, struct proc *p) {
	/*
	 * note, that LK_RELASE will do wakeup() instead of wakeup_one().
	 * this will do a little overhead
	 */
	lockmgr(&conn->nc_lock, LK_RELEASE, 0, p);
}

int 
ncp_conn_assert_locked(struct ncp_conn *conn,char *checker, struct proc *p){
	if (conn->nc_lock.lk_flags & LK_HAVE_EXCL) return 0;
	printf("%s: connection isn't locked!\n", checker);
	return EIO;
}

/* 
 * create, fill with defaults and return in locked state
 */
int
ncp_conn_alloc(struct proc *p, struct ucred *cred, struct ncp_conn **conn)
{
	int error;
	struct ncp_conn *ncp;

	MALLOC(ncp, struct ncp_conn *, sizeof(struct ncp_conn), 
	    M_NCPDATA, M_WAITOK | M_ZERO);
	error = 0;
	lockinit(&ncp->nc_lock, PZERO, "ncplck", 0, 0);
	ncp_conn_cnt++;
	ncp->nc_id = ncp_next_ref++;
	ncp->nc_owner = cred;
	ncp->seq = 0;
	ncp->connid = 0xFFFF;
	ncp_conn_lock_any(ncp, p, ncp->nc_owner);
	*conn = ncp;
	ncp_conn_locklist(LK_EXCLUSIVE, p);
	SLIST_INSERT_HEAD(&conn_list,ncp,nc_next);
	ncp_conn_unlocklist(p);
	return (error);
}

/*
 * Remove the connection, on entry it must be locked
 */
int
ncp_conn_free(struct ncp_conn *ncp) {
	int error;
	struct ncp_conn *ncp1;

	if (ncp->nc_id == 0) {
	printf("already!!!!\n");
		return EACCES;
	}
	if (ncp==NULL) {
		NCPFATAL("conn==NULL !\n");
		return(EIO);
	}
	error = ncp_conn_assert_locked(ncp, __FUNCTION__, ncp->procp);
	if (error) return error;
	if (ncp->ref_cnt) {
		NCPFATAL("there are %d referenses left\n",ncp->ref_cnt);
		return(EBUSY);
	}
	/*
	 * Mark conn as died and wait for other process
	 */
	ncp->nc_id = 0;
	ncp_conn_unlock(ncp,ncp->procp);
	/*
	 * if signal is raised - how I do react ?
	 */
	lockmgr(&ncp->nc_lock, LK_DRAIN, 0, ncp->procp);
	while (ncp->nc_lwant) {
		printf("lwant = %d\n", ncp->nc_lwant);
		tsleep(&ncp->nc_lwant, PZERO,"ncpdr",2*hz);
	}
	ncp_conn_locklist(LK_EXCLUSIVE, ncp->procp);
	/*
	 * It is possible, that other process destroy connection while we draining,
	 * and free it. So, we must rescan list
	 */
	SLIST_FOREACH(ncp1, &conn_list, nc_next) {
		if (ncp1 == ncp) break;
	}
	if (ncp1 == NULL) {
		ncp_conn_unlocklist(ncp->procp);
		return 0;
	}
	SLIST_REMOVE(&conn_list, ncp, ncp_conn, nc_next);
	ncp_conn_cnt--;
	ncp_conn_unlocklist(ncp->procp);
	if (ncp->li.user) free(ncp->li.user, M_NCPDATA);
	if (ncp->li.password) free(ncp->li.password, M_NCPDATA);
	crfree(ncp->nc_owner);
	FREE(ncp, M_NCPDATA);
	return (0);
}

/* 
 * Lookup connection by handle, return a locked conn descriptor 
 */
int
ncp_conn_getbyref(int ref,struct proc *p,struct ucred *cred, int mode, struct ncp_conn **connpp){
	struct ncp_conn *ncp;
	int error=0;

	ncp_conn_locklist(LK_SHARED, p);
	SLIST_FOREACH(ncp, &conn_list, nc_next)
		if (ncp->nc_id == ref) break;
	if (ncp == NULL) {
		ncp_conn_unlocklist(p);
		return(EBADF);
	}
	error = ncp_conn_lock2(ncp, p, cred, mode);
	if (!error)
		*connpp = ncp;
	return (error);
}
/*
 * find attached, but not logged in connection to specified server
 */
int
ncp_conn_getattached(struct ncp_conn_args *li,struct proc *p,struct ucred *cred,int mode, struct ncp_conn **connpp){
	struct ncp_conn *ncp, *ncp2=NULL;
	int error = 0;

	ncp_conn_locklist(LK_SHARED, p);
	SLIST_FOREACH(ncp, &conn_list, nc_next) {
		if ((ncp->flags & NCPFL_LOGGED) != 0 ||
		    strcmp(ncp->li.server,li->server) != 0 || 
		    ncp->li.saddr.sa_len != li->saddr.sa_len ||
		    bcmp(&ncp->li.saddr,&ncp->li.saddr,li->saddr.sa_len) != 0)
			continue;
		if (ncp_suser(cred) == 0 || 
		    cred->cr_uid == ncp->nc_owner->cr_uid)
			break;
		error = ncp_conn_access(ncp,cred,mode);
		if (!error && ncp2 == NULL)
			ncp2 = ncp;
	}
	if (ncp == NULL) ncp = ncp2;
	if (ncp == NULL) {
		ncp_conn_unlocklist(p);
		return(EBADF);
	}
	error = ncp_conn_lock2(ncp,p,cred,mode);
	if (!error)
		*connpp=ncp;
	return (error);
}

/* 
 * Lookup connection by server/user pair, return a locked conn descriptor.
 * if li is NULL or server/user pair incomplete, try to select best connection 
 * based on owner.
 * Connection selected in next order:
 * 1. Try to search conn with ucred owner, if li is NULL also find a primary
 * 2. If 1. fails try to get first suitable shared connection
 * 3. If 2. fails then nothing can help to poor ucred owner
 */

int
ncp_conn_getbyli(struct ncp_conn_args *li,struct proc *p,struct ucred *cred,int mode, struct ncp_conn **connpp){
	struct ncp_conn *ncp, *ncp2=NULL;
	int error=0, partial, haveserv;

	partial = (li == NULL || li->server[0] == 0 || li->user == NULL);
	haveserv = (li && li->server[0]);
	ncp_conn_locklist(LK_SHARED, p);
	SLIST_FOREACH(ncp, &conn_list, nc_next) {
		if (partial) {
			if (cred->cr_uid == ncp->nc_owner->cr_uid) {
				if (haveserv) {
					if (strcmp(ncp->li.server,li->server) == 0)
						break;
				} else {
					if (ncp->flags & NCPFL_PRIMARY)
						break;
					ncp2 = ncp;
				}
				continue;
			}
		} else {
			if (strcmp(ncp->li.server,li->server) != 0 || 
			    ncp->li.user == NULL ||
			    strcmp(ncp->li.user,li->user) != 0)
				continue;
			if (cred->cr_uid == ncp->nc_owner->cr_uid)
				break;
			if (ncp_suser(cred) == 0)
				ncp2 = ncp;
		}
		error = ncp_conn_access(ncp,cred,mode);
		if (!error && ncp2 == NULL)
			ncp2 = ncp;
	}
	if (ncp == NULL) ncp = ncp2;
	if (ncp == NULL) {
		ncp_conn_unlocklist(p);
		return(EBADF);
	}
	error = ncp_conn_lock2(ncp,p,cred,mode);
	if (!error)
		*connpp=ncp;
	return (error);
}

/*
 * Set primary connection flag, since it have sence only for an owner,
 * only owner can modify this flag.
 * connection expected to be locked.
 */
int
ncp_conn_setprimary(struct ncp_conn *conn, int on){
	struct ncp_conn *ncp=NULL;

	if (conn->ucred->cr_uid != conn->nc_owner->cr_uid)
		return EACCES;
	ncp_conn_locklist(LK_SHARED, conn->procp);
	SLIST_FOREACH(ncp, &conn_list, nc_next) {
		if (conn->ucred->cr_uid == ncp->nc_owner->cr_uid)
			ncp->flags &= ~NCPFL_PRIMARY;
	}
	ncp_conn_unlocklist(conn->procp);
	if (on)
		conn->flags |= NCPFL_PRIMARY;
	return 0;
}
/* 
 * Lease conn to given proc, returning unique handle
 * problem: how locks should be applied ?
 */
int
ncp_conn_gethandle(struct ncp_conn *conn, struct proc *p, struct ncp_handle **handle){
	struct ncp_handle *refp;

	lockmgr(&lhlock, LK_EXCLUSIVE, 0, p);
	SLIST_FOREACH(refp, &lhlist, nh_next)
		if (refp->nh_conn == conn && p == refp->nh_proc) break;
	if (refp) {
		conn->ref_cnt++;
		refp->nh_ref++;
		*handle = refp;
		lockmgr(&lhlock, LK_RELEASE, 0, p);
		return 0;
	}
	MALLOC(refp,struct ncp_handle *,sizeof(struct ncp_handle),M_NCPDATA,
	    M_WAITOK | M_ZERO);
	SLIST_INSERT_HEAD(&lhlist,refp,nh_next);
	refp->nh_ref++;
	refp->nh_proc = p;
	refp->nh_conn = conn;
	refp->nh_id = ncp_next_handle++;
	*handle = refp;
	conn->ref_cnt++;
	lockmgr(&lhlock, LK_RELEASE, 0, p);
	return 0;
}
/*
 * release reference, if force - ignore refcount
 */
int
ncp_conn_puthandle(struct ncp_handle *handle, struct proc *p, int force) {
	struct ncp_handle *refp = handle;

	lockmgr(&lhlock, LK_EXCLUSIVE, 0, p);
	refp->nh_ref--;
	refp->nh_conn->ref_cnt--;
	if (force) {
		refp->nh_conn->ref_cnt -= refp->nh_ref;
		refp->nh_ref = 0;
	}
	if (refp->nh_ref == 0) {
		SLIST_REMOVE(&lhlist, refp, ncp_handle, nh_next);
		FREE(refp, M_NCPDATA);
	}
	lockmgr(&lhlock, LK_RELEASE, 0, p);
	return 0;
}
/*
 * find a connHandle
 */
int
ncp_conn_findhandle(int connHandle, struct proc *p, struct ncp_handle **handle) {
	struct ncp_handle *refp;

	lockmgr(&lhlock, LK_SHARED, 0, p);
	SLIST_FOREACH(refp, &lhlist, nh_next)
		if (refp->nh_proc == p && refp->nh_id == connHandle) break;
	lockmgr(&lhlock, LK_RELEASE, 0, p);
	if (refp == NULL) {
		return EBADF;
	}
	*handle = refp;
	return 0;
}
/*
 * Clear handles associated with specified process
 */
int
ncp_conn_putprochandles(struct proc *p) {
	struct ncp_handle *hp, *nhp;
	int haveone = 0;

	lockmgr(&lhlock, LK_EXCLUSIVE, 0, p);
	for (hp = SLIST_FIRST(&lhlist); hp; hp = nhp) {
		nhp = SLIST_NEXT(hp, nh_next);
		if (hp->nh_proc != p) continue;
		haveone = 1;
		hp->nh_conn->ref_cnt -= hp->nh_ref;
		SLIST_REMOVE(&lhlist, hp, ncp_handle, nh_next);
		FREE(hp, M_NCPDATA);
	}
	lockmgr(&lhlock, LK_RELEASE, 0, p);
	return haveone;
}
/*
 * remove references in all possible connections,
 * XXX - possible problem is a locked list.
 */
/*void
ncp_conn_list_rm_ref(pid_t pid) {
	struct ncp_conn *ncp;

	ncp_conn_locklist(LK_SHARED, NULL);
	SLIST_FOREACH(ncp, &conn_list, nc_next) {
		ncp_conn_rm_ref(ncp,pid,1);
	}
	ncp_conn_unlocklist(NULL);
	return;
}
*/
int
ncp_conn_getinfo(struct ncp_conn *ncp, struct ncp_conn_stat *ncs) {
	bzero(ncs,sizeof(*ncs));
	ncs->li = ncp->li;
	ncs->li.user = ncs->user;
	if (ncp->li.user)
		strcpy(ncs->user, ncp->li.user);
	ncs->li.password = NULL;
	ncs->connRef = ncp->nc_id;
	ncs->ref_cnt = ncp->ref_cnt;
	ncs->connid = ncp->connid;
	ncs->owner = ncp->nc_owner->cr_uid;
	ncs->group = ncp->nc_group;
	ncs->flags = ncp->flags;
	ncs->buffer_size = ncp->buffer_size;
	return 0;
}

static int
ncp_sysctl_connstat(SYSCTL_HANDLER_ARGS) {
	int error;
	struct ncp_conn_stat ncs;
	struct ncp_conn *ncp;
/*	struct ucred *cred = req->p->p_ucred;*/

	error = 0;
	ncp_conn_locklist(LK_SHARED, req->p);
	error = SYSCTL_OUT(req, &ncp_conn_cnt, sizeof(ncp_conn_cnt));
	SLIST_FOREACH(ncp, &conn_list, nc_next) {
		if (error) break;
		/* I can't do conn_lock while list is locked */
		ncp->nc_lwant++;
		if (!error) {
			ncp_conn_getinfo(ncp, &ncs);
		} else {
			bzero(&ncs,sizeof(ncs));
			ncs.connRef = ncp->nc_id;
			strcpy(ncs.li.server,"***");
		}
		ncp->nc_lwant--;
		error = SYSCTL_OUT(req, &ncs, sizeof(ncs));
	}
	ncp_conn_unlocklist(req->p);
	return(error);
}
