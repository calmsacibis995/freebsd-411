# $FreeBSD: src/usr.sbin/named.reload/Makefile,v 1.6.2.2 2001/04/25 14:04:54 ru Exp $

.include "${.CURDIR}/../named/Makefile.inc"

SCRIPTS=	named.reload
MAN=		named.reload.8
CLEANFILES+=	${SCRIPTS}
SCRIPTSNAME=	${SCRIPTS}

named.reload:	named.reload.sh ${BIND_DIR}/Makefile
	sed -e "s|%INDOT%|${INDOT}|" \
	    -e "s|%DESTSBIN%|${DESTSBIN}|" \
	    < ${.CURDIR}/named.reload.sh > ${.TARGET}

.include <bsd.prog.mk>
