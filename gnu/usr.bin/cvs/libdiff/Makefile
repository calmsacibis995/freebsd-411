# $FreeBSD: src/gnu/usr.bin/cvs/libdiff/Makefile,v 1.4.2.5 2004/06/29 16:10:53 des Exp $

MAINTAINER=	peter@FreeBSD.org

.include "${.CURDIR}/../Makefile.inc"

.PATH:	${CVSDIR}/diff

LIB=		diff
INTERNALLIB=	YES

# gnu must be before lib to pick correct regex.h
CFLAGS+= -I../lib -I${DESTDIR}/usr/include/gnu \
	 -I${CVSDIR}/lib -DHAVE_CONFIG_H

SRCS =	diff.c diff3.c analyze.c cmpbuf.c cmpbuf.h io.c context.c ed.c \
        normal.c ifdef.c util.c dir.c version.c diff.h side.c

.include <bsd.lib.mk>
