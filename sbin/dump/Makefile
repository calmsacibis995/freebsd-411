#	@(#)Makefile	8.1 (Berkeley) 6/5/93
# $FreeBSD: src/sbin/dump/Makefile,v 1.12.2.4 2003/01/25 18:54:59 dillon Exp $

#	dump.h			header file
#	itime.c			reads /etc/dumpdates
#	main.c			driver
#	optr.c			operator interface
#	dumprmt.c		handles remote tape via rmt(8)
#	tape.c			handles the mag tape and opening/closing
#	traverse.c		traverses the file system
#	unctime.c		undo ctime
#
#	DEBUG			use local directory to find ddate and dumpdates
#	TDEBUG			trace out the process forking

PROG=	dump
LINKS=	${BINDIR}/dump ${BINDIR}/rdump
CFLAGS+=-DRDUMP
CFLAGS+=-I${.CURDIR}/../../libexec/rlogind
SRCS=	itime.c main.c optr.c dumprmt.c tape.c traverse.c unctime.c cache.c
MAN=	dump.8
MLINKS+=dump.8 rdump.8

.if defined(MAKE_KERBEROS4) && !defined(NO_OPENSSL) && !defined(NOCRYPT)
.PATH:	${.CURDIR}/../../crypto/kerberosIV/appl/bsd
SRCS+=	krcmd.c kcmd.c rcmd_util.c
DPADD=	${LIBKRB} ${LIBCRYPTO}
LDADD+=	-lkrb -lcrypto
CFLAGS+=-DKERBEROS -DHAVE_CONFIG_H \
	-I${.CURDIR}/../../kerberosIV/include \
	-I${.CURDIR}/../../crypto/kerberosIV/include \
	-I${.CURDIR}/../../crypto/kerberosIV/lib/roken \
	-I${.CURDIR}/../../crypto/kerberosIV/appl/bsd
DISTRIBUTION=	krb4
.endif

.include <bsd.prog.mk>
