# $FreeBSD: src/gnu/lib/libregex/Makefile,v 1.22.2.1 2002/07/22 14:21:45 ru Exp $

SUBDIR+= doc

LIB=		gnuregex
SHLIB_MAJOR=	2
SHLIB_MINOR=	0

SRCS=	regex.c
INCS=	regex.h
INCSNAME= gnuregex.h
NOMAN=	noman

CFLAGS+=-I${.CURDIR} -DHAVE_STRING_H=1 -DSTDC_HEADERS=1 -DNO_POSIX_COMPAT=1

.include <bsd.lib.mk>
