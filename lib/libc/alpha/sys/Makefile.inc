# $FreeBSD: src/lib/libc/alpha/sys/Makefile.inc,v 1.6.2.1 2002/10/15 19:46:45 fjoe Exp $

MDASM+=	Ovfork.S brk.S cerror.S exect.S fork.S pipe.S ptrace.S \
	sbrk.S setlogin.S sigreturn.S syscall.S

# Don't generate default code for these syscalls:
NOASM=	__semctl.o break.o exit.o ftruncate.o getdomainname.o getlogin.o \
	lseek.o mlockall.o mmap.o msgctl.o msgget.o msgrcv.o msgsnd.o \
	munlockall.o openbsd_poll.o pread.o pwrite.o semconfig.o semget.o \
	semop.o setdomainname.o shmat.o shmctl.o shmdt.o shmget.o sstk.o \
	thr_sleep.o thr_wakeup.o truncate.o uname.o vfork.o yield.o

PSEUDO=	_getlogin.o

# Pseudo syscalls that are renamed as __sys_{pseudo} when
# building libc_r.
PSEUDOR= _exit.o
