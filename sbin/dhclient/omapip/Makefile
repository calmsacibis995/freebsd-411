# omapip
# $FreeBSD: src/sbin/dhclient/omapip/Makefile,v 1.2.2.2 2002/07/19 18:46:27 ru Exp $

DIST_DIR= ${.CURDIR}/../../../contrib/isc-dhcp

.PATH: ${DIST_DIR}/omapip

LIB=	 omapi
INTERNALLIB=		true

SRCS=	alloc.c array.c auth.c buffer.c connection.c convert.c dispatch.c \
	errwarn.c generic.c handle.c hash.c inet_addr.c listener.c \
	message.c mrtrace.c protocol.c result.c support.c toisc.c trace.c

MAN=	omapi.3
NOMAN=	true

.include <bsd.lib.mk>
