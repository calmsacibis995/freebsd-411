# $FreeBSD: src/gnu/usr.bin/bc/Makefile,v 1.17.2.1 2002/02/18 16:26:51 ru Exp $

.PATH:	${.CURDIR}/../../../contrib/bc/bc \
	${.CURDIR}/../../../contrib/bc/lib \
	${.CURDIR}/../../../contrib/bc/doc \
	${.CURDIR}/../../../contrib/bc/Examples

PROG=	bc
SRCS=	bc.y execute.c global.c load.c main.c scan.l storage.c util.c \
	number.c getopt.c getopt1.c
CFLAGS+=-I. -I${.CURDIR} -I${.CURDIR}/../../../contrib/bc/h -DHAVE_CONFIG_H
DPADD=	${LIBREADLINE} ${LIBTERMCAP}
LDADD=	-lreadline -ltermcap

FILES=	ckbook.b pi.b primes.b twins.b
FILESDIR=	${SHAREDIR}/examples/bc

.include <bsd.prog.mk>
