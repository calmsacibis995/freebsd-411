# $FreeBSD: src/sbin/vinum/Makefile,v 1.12.2.2 2001/08/01 09:13:24 obrien Exp $

MAINTAINER  =  grog@FreeBSD.org
PROG=	vinum
SRCS=	v.c list.c vinumparser.c vinumutil.c vext.h commands.c
MAN=	vinum.8

CFLAGS+=	-I${.CURDIR}/../../sys -Wall -DVINUMDEBUG
DPADD=	${LIBUTIL} ${LIBREADLINE} ${LIBTERMCAP}
LDADD=	-lreadline -ltermcap -ldevstat

.PATH: ${.CURDIR}/../../sys/dev/vinum

.include <bsd.prog.mk>
