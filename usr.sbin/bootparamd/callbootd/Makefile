#	from: @(#)Makefile	5.8 (Berkeley) 7/28/90
# $FreeBSD: src/usr.sbin/bootparamd/callbootd/Makefile,v 1.10 1999/08/28 01:15:40 peter Exp $

PROG=	callbootd
SRCS=	callbootd.c ${GENSRCS}
GENSRCS=bootparam_prot.h bootparam_prot_clnt.c bootparam_prot_xdr.c
NOMAN=
CFLAGS+= -I.
CLEANFILES= ${GENSRCS}

RPCSRC=	${DESTDIR}/usr/include/rpcsvc/bootparam_prot.x

bootparam_prot_clnt.c: ${RPCSRC}
	rpcgen -l -o ${.TARGET} ${RPCSRC}

bootparam_prot_xdr.c: ${RPCSRC}
	rpcgen -c -o ${.TARGET} ${RPCSRC}

bootparam_prot.h: ${RPCSRC}
	rpcgen -h -o ${.TARGET} ${RPCSRC}

.include <bsd.prog.mk>
