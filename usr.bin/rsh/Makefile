#	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD: src/usr.bin/rsh/Makefile,v 1.17.2.1 2002/07/17 19:08:22 ru Exp $

PROG=	rsh
SRCS=	rsh.c
CFLAGS+=-Wall
CFLAGS+=-I${.CURDIR}/../../libexec/rlogind

.if defined(MAKE_KERBEROS4) && !defined(NO_OPENSSL) && !defined(NOCRYPT)
SRCS+=	krcmd.c kcmd.c rcmd_util.c
CFLAGS+=-DKERBEROS -DCRYPT -DHAVE_CONFIG_H \
	-I${.CURDIR}/../rlogin -I${.CURDIR} -I- \
	-I${.CURDIR}/../../kerberosIV/include \
	-I${.CURDIR}/../../crypto/kerberosIV/include \
	-I${.CURDIR}/../../crypto/kerberosIV/lib/roken \
	-I${.CURDIR}/../../crypto/kerberosIV/appl/bsd
DPADD=	${LIBUTIL} ${LIBKRB} ${LIBCRYPTO} ${LIBCRYPT} ${LIBCOM_ERR}
LDADD=	-lutil -lkrb -lcrypto -lcrypt -lcom_err
DISTRIBUTION=	krb4
.PATH:	${.CURDIR}/../../crypto/kerberosIV/appl/bsd
.endif

BINMODE=4555
INSTALLFLAGS=-fschg

.include <bsd.prog.mk>
