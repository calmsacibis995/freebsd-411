# $FreeBSD: src/usr.sbin/named.restart/Makefile,v 1.6.2.2 2001/04/25 14:04:55 ru Exp $

.include "${.CURDIR}/../named/Makefile.inc"

SCRIPTS=	named.restart
MAN=		named.restart.8
CLEANFILES+=	${SCRIPTS}
SCRIPTSNAME=	${SCRIPTS}

named.restart:	named.restart.sh ${BIND_DIR}/Makefile
	sed -e "s|%INDOT%|${INDOT}|" \
	    -e "s|%DESTSBIN%|${DESTSBIN}|" \
	    < ${.CURDIR}/named.restart.sh > named.restart

.include <bsd.prog.mk>
