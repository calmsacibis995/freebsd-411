#	pccardc Makefile
#
# $FreeBSD: src/usr.sbin/pccard/pccardc/Makefile,v 1.11.2.2 2003/07/19 19:37:18 imp Exp $
#

.PATH: ${.CURDIR}/../pccardd

PROG=	pccardc
MAN=	pccardc.8
SRCS=	beep.c dumpcis.c dumpcisfile.c enabler.c pccardc.c pccardmem.c power.c \
	printcis.c rdattr.c rdmap.c rdreg.c readcis.c wrattr.c wrreg.c

CFLAGS+= -I${.CURDIR}/../pccardd

.include <bsd.prog.mk>
