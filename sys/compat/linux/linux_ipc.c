/*-
 * Copyright (c) 1994-1995 S�ren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer 
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software withough specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/compat/linux/linux_ipc.c,v 1.17.2.3 2001/11/05 19:08:22 marcel Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/proc.h>
#include <sys/sem.h>
#include <sys/shm.h>

#include <machine/../linux/linux.h>
#include <machine/../linux/linux_proto.h>
#include <compat/linux/linux_ipc.h>
#include <compat/linux/linux_util.h>

struct l_seminfo {
	l_int semmap;
	l_int semmni;
	l_int semmns;
	l_int semmnu;
	l_int semmsl;
	l_int semopm;
	l_int semume;
	l_int semusz;
	l_int semvmx;
	l_int semaem;
};

struct l_shminfo {
	l_int shmmax;
	l_int shmmin;
	l_int shmmni;
	l_int shmseg;
	l_int shmall;
};

struct l_shm_info {
	l_int used_ids;
	l_ulong shm_tot;  /* total allocated shm */
	l_ulong shm_rss;  /* total resident shm */
	l_ulong shm_swp;  /* total swapped shm */
	l_ulong swap_attempts;
	l_ulong swap_successes;
};

struct l_ipc_perm {
	l_key_t		key;
	l_uid16_t	uid;
	l_gid16_t	gid;
	l_uid16_t	cuid;
	l_gid16_t	cgid;
	l_ushort	mode;
	l_ushort	seq;
};

static void
linux_to_bsd_ipc_perm(struct l_ipc_perm *lpp, struct ipc_perm *bpp)
{
    bpp->key = lpp->key;
    bpp->uid = lpp->uid;
    bpp->gid = lpp->gid;
    bpp->cuid = lpp->cuid;
    bpp->cgid = lpp->cgid;
    bpp->mode = lpp->mode;
    bpp->seq = lpp->seq;
}


static void
bsd_to_linux_ipc_perm(struct ipc_perm *bpp, struct l_ipc_perm *lpp)
{
    lpp->key = bpp->key;
    lpp->uid = bpp->uid;
    lpp->gid = bpp->gid;
    lpp->cuid = bpp->cuid;
    lpp->cgid = bpp->cgid;
    lpp->mode = bpp->mode;
    lpp->seq = bpp->seq;
}

struct l_semid_ds {
	struct l_ipc_perm	sem_perm;
	l_time_t		sem_otime;
	l_time_t		sem_ctime;
	void			*sem_base;
	void			*sem_pending;
	void			*sem_pending_last;
	void			*undo;
	l_ushort		sem_nsems;
};

struct l_shmid_ds {
	struct l_ipc_perm	shm_perm;
	l_int			shm_segsz;
	l_time_t		shm_atime;
	l_time_t		shm_dtime;
	l_time_t		shm_ctime;
	l_ushort		shm_cpid;
	l_ushort		shm_lpid;
	l_short			shm_nattch;
	l_ushort		private1;
	void			*private2;
	void			*private3;
};

static void
linux_to_bsd_semid_ds(struct l_semid_ds *lsp, struct semid_ds *bsp)
{
    linux_to_bsd_ipc_perm(&lsp->sem_perm, &bsp->sem_perm);
    bsp->sem_otime = lsp->sem_otime;
    bsp->sem_ctime = lsp->sem_ctime;
    bsp->sem_nsems = lsp->sem_nsems;
    bsp->sem_base = lsp->sem_base;
}

static void
bsd_to_linux_semid_ds(struct semid_ds *bsp, struct l_semid_ds *lsp)
{
	bsd_to_linux_ipc_perm(&bsp->sem_perm, &lsp->sem_perm);
	lsp->sem_otime = bsp->sem_otime;
	lsp->sem_ctime = bsp->sem_ctime;
	lsp->sem_nsems = bsp->sem_nsems;
	lsp->sem_base = bsp->sem_base;
}

static void
linux_to_bsd_shmid_ds(struct l_shmid_ds *lsp, struct shmid_ds *bsp)
{
    linux_to_bsd_ipc_perm(&lsp->shm_perm, &bsp->shm_perm);
    bsp->shm_segsz = lsp->shm_segsz;
    bsp->shm_lpid = lsp->shm_lpid;
    bsp->shm_cpid = lsp->shm_cpid;
    bsp->shm_nattch = lsp->shm_nattch;
    bsp->shm_atime = lsp->shm_atime;
    bsp->shm_dtime = lsp->shm_dtime;
    bsp->shm_ctime = lsp->shm_ctime;
    bsp->shm_internal = lsp->private3;	/* this goes (yet) SOS */
}

static void
bsd_to_linux_shmid_ds(struct shmid_ds *bsp, struct l_shmid_ds *lsp)
{
    bsd_to_linux_ipc_perm(&bsp->shm_perm, &lsp->shm_perm);
    lsp->shm_segsz = bsp->shm_segsz;
    lsp->shm_lpid = bsp->shm_lpid;
    lsp->shm_cpid = bsp->shm_cpid;
    lsp->shm_nattch = bsp->shm_nattch;
    lsp->shm_atime = bsp->shm_atime;
    lsp->shm_dtime = bsp->shm_dtime;
    lsp->shm_ctime = bsp->shm_ctime;
    lsp->private3 = bsp->shm_internal;	/* this goes (yet) SOS */
}

int
linux_semop(struct proc *p, struct linux_semop_args *args)
{
	struct semop_args /* {
	int	semid;
	struct	sembuf *sops;
	int		nsops;
	} */ bsd_args;

	bsd_args.semid = args->semid;
	bsd_args.sops = (struct sembuf *)args->tsops;
	bsd_args.nsops = args->nsops;
	return semop(p, &bsd_args);
}

int
linux_semget(struct proc *p, struct linux_semget_args *args)
{
	struct semget_args /* {
	key_t	key;
	int		nsems;
	int		semflg;
	} */ bsd_args;

	bsd_args.key = args->key;
	bsd_args.nsems = args->nsems;
	bsd_args.semflg = args->semflg;
	return semget(p, &bsd_args);
}

int
linux_semctl(struct proc *p, struct linux_semctl_args *args)
{
	struct l_semid_ds linux_semid;
	struct __semctl_args /* {
		int		semid;
		int		semnum;
		int		cmd;
		union semun	*arg;
	} */ bsd_args;
	struct l_seminfo linux_seminfo;
	int error;
	union semun *unptr;
	caddr_t sg;

	sg = stackgap_init();

	/* Make sure the arg parameter can be copied in. */
	unptr = stackgap_alloc(&sg, sizeof(union semun));
	bcopy(&args->arg, unptr, sizeof(union semun));

	bsd_args.semid = args->semid;
	bsd_args.semnum = args->semnum;
	bsd_args.arg = unptr;

	switch (args->cmd) {
	case LINUX_IPC_RMID:
		bsd_args.cmd = IPC_RMID;
		break;
	case LINUX_GETNCNT:
		bsd_args.cmd = GETNCNT;
		break;
	case LINUX_GETPID:
		bsd_args.cmd = GETPID;
		break;
	case LINUX_GETVAL:
		bsd_args.cmd = GETVAL;
		break;
	case LINUX_GETZCNT:
		bsd_args.cmd = GETZCNT;
		break;
	case LINUX_SETVAL:
		bsd_args.cmd = SETVAL;
		break;
	case LINUX_IPC_SET:
		bsd_args.cmd = IPC_SET;
		error = copyin((caddr_t)args->arg.buf, &linux_semid,
		    sizeof(linux_semid));
		if (error)
			return (error);
		unptr->buf = stackgap_alloc(&sg, sizeof(struct semid_ds));
		linux_to_bsd_semid_ds(&linux_semid, unptr->buf);
		return __semctl(p, &bsd_args);
	case LINUX_IPC_STAT:
		bsd_args.cmd = IPC_STAT;
		unptr->buf = stackgap_alloc(&sg, sizeof(struct semid_ds));
		error = __semctl(p, &bsd_args);
		if (error)
			return error;
		p->p_retval[0] = IXSEQ_TO_IPCID(bsd_args.semid, 
							unptr->buf->sem_perm);
		bsd_to_linux_semid_ds(unptr->buf, &linux_semid);
		return copyout(&linux_semid, (caddr_t)args->arg.buf,
					    sizeof(linux_semid));
	case LINUX_IPC_INFO:
	case LINUX_SEM_INFO:
		error = copyin((caddr_t)args->arg.buf, &linux_seminfo, 
						sizeof(linux_seminfo) );
		if (error)
			return error;
		bcopy(&seminfo, &linux_seminfo, sizeof(linux_seminfo) );
/* XXX BSD equivalent?
#define used_semids 10
#define used_sems 10
		linux_seminfo.semusz = used_semids;
		linux_seminfo.semaem = used_sems;
*/
		error = copyout((caddr_t)&linux_seminfo, (caddr_t)args->arg.buf,
						sizeof(linux_seminfo) );
		if (error)
			return error;
		p->p_retval[0] = seminfo.semmni;
		return 0;			/* No need for __semctl call */
	case LINUX_GETALL:
		/* FALLTHROUGH */
	case LINUX_SETALL:
		/* FALLTHROUGH */
	default:
		uprintf("linux: 'ipc' typ=%d not implemented\n", args->cmd);
		return EINVAL;
	}
	return __semctl(p, &bsd_args);
}

int
linux_msgsnd(struct proc *p, struct linux_msgsnd_args *args)
{
    struct msgsnd_args /* {
	int     msqid;   
	void    *msgp;   
	size_t  msgsz;   
	int     msgflg; 
    } */ bsd_args;

    bsd_args.msqid = args->msqid;
    bsd_args.msgp = args->msgp;
    bsd_args.msgsz = args->msgsz;
    bsd_args.msgflg = args->msgflg;
    return msgsnd(p, &bsd_args);
}

int
linux_msgrcv(struct proc *p, struct linux_msgrcv_args *args)
{
    struct msgrcv_args /* {     
        int 	msqid;   
	void	*msgp;   
	size_t	msgsz;   
	long	msgtyp; 
	int	msgflg; 
    } */ bsd_args; 

    bsd_args.msqid = args->msqid;
    bsd_args.msgp = args->msgp;
    bsd_args.msgsz = args->msgsz;
    bsd_args.msgtyp = 0; /* XXX - args->msgtyp; */
    bsd_args.msgflg = args->msgflg;
    return msgrcv(p, &bsd_args);
}

int
linux_msgget(struct proc *p, struct linux_msgget_args *args)
{
    struct msgget_args /* {
	key_t	key;
        int 	msgflg;
    } */ bsd_args;

    bsd_args.key = args->key;
    bsd_args.msgflg = args->msgflg;
    return msgget(p, &bsd_args);
}

int
linux_msgctl(struct proc *p, struct linux_msgctl_args *args)
{
    struct msgctl_args /* {
	int     msqid; 
	int     cmd;
	struct	msqid_ds *buf;
    } */ bsd_args;
    int error;

    bsd_args.msqid = args->msqid;
    bsd_args.cmd = args->cmd;
    bsd_args.buf = (struct msqid_ds *)args->buf;
    error = msgctl(p, &bsd_args);
    return ((args->cmd == LINUX_IPC_RMID && error == EINVAL) ? 0 : error);
}

int
linux_shmat(struct proc *p, struct linux_shmat_args *args)
{
    struct shmat_args /* {
	int shmid;
	void *shmaddr;
	int shmflg;
    } */ bsd_args;
    int error;

    bsd_args.shmid = args->shmid;
    bsd_args.shmaddr = args->shmaddr;
    bsd_args.shmflg = args->shmflg;
    if ((error = shmat(p, &bsd_args)))
	return error;
#ifdef __i386__
    if ((error = copyout(p->p_retval, (caddr_t)args->raddr, sizeof(l_ulong))))
	return error;
    p->p_retval[0] = 0;
#endif
    return 0;
}

int
linux_shmdt(struct proc *p, struct linux_shmdt_args *args)
{
    struct shmdt_args /* {
	void *shmaddr;
    } */ bsd_args;

    bsd_args.shmaddr = args->shmaddr;
    return shmdt(p, &bsd_args);
}

int
linux_shmget(struct proc *p, struct linux_shmget_args *args)
{
    struct shmget_args /* {
	key_t key;
	int size;
	int shmflg;
    } */ bsd_args;

    bsd_args.key = args->key;
    bsd_args.size = args->size;
    bsd_args.shmflg = args->shmflg;
    return shmget(p, &bsd_args);
}

int
linux_shmctl(struct proc *p, struct linux_shmctl_args *args)
{
    struct l_shmid_ds linux_shmid;
    struct shmctl_args /* {
	int shmid;
	int cmd;
	struct shmid_ds *buf;
    } */ bsd_args;
    int error;
    caddr_t sg = stackgap_init();

    switch (args->cmd) {
    case LINUX_IPC_STAT:
	bsd_args.shmid = args->shmid;
	bsd_args.cmd = IPC_STAT;
	bsd_args.buf = (struct shmid_ds*)stackgap_alloc(&sg, sizeof(struct shmid_ds));
	if ((error = shmctl(p, &bsd_args)))
	    return error;
	bsd_to_linux_shmid_ds(bsd_args.buf, &linux_shmid);
	return copyout(&linux_shmid, (caddr_t)args->buf, sizeof(linux_shmid));

    case LINUX_IPC_SET:
	if ((error = copyin((caddr_t)args->buf, &linux_shmid,
		sizeof(linux_shmid))))
	    return error;
	bsd_args.buf = (struct shmid_ds*)stackgap_alloc(&sg, sizeof(struct shmid_ds));
	linux_to_bsd_shmid_ds(&linux_shmid, bsd_args.buf);
	bsd_args.shmid = args->shmid;
	bsd_args.cmd = IPC_SET;
	return shmctl(p, &bsd_args);

    case LINUX_IPC_RMID:
	bsd_args.shmid = args->shmid;
	bsd_args.cmd = IPC_RMID;
	if (args->buf == NULL)
	    bsd_args.buf = NULL;
	else {
	    if ((error = copyin((caddr_t)args->buf, &linux_shmid, 
		    		sizeof(linux_shmid))))
		return error;
	    bsd_args.buf = (struct shmid_ds*)stackgap_alloc(&sg, sizeof(struct shmid_ds));
	    linux_to_bsd_shmid_ds(&linux_shmid, bsd_args.buf);
	}
	return shmctl(p, &bsd_args);

    case LINUX_IPC_INFO:
    case LINUX_SHM_STAT:
    case LINUX_SHM_INFO:
    case LINUX_SHM_LOCK:
    case LINUX_SHM_UNLOCK:
    default:
	uprintf("linux: 'ipc' typ=%d not implemented\n", args->cmd);
	return EINVAL;
    }
}
