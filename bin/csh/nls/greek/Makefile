# $FreeBSD: src/bin/csh/nls/greek/Makefile,v 1.4.2.3 2002/08/07 16:31:38 ru Exp $

NL=     greek
DL=     el_GR.ISO8859-7

.PATH: ${BASESRC}/${NL}

tcsh.msg: set[0-9]*
	cat ${BASESRC}/${NL}/set[0-9] \
	    ${BASESRC}/${NL}/set[0-9][0-9] > ${.TARGET}

install:
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${NOBINMODE} \
		tcsh.cat ${DESTDIR}${NLSDIR}/${DL}/tcsh.cat

.include <bsd.prog.mk>
