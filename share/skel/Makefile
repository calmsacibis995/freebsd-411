#	@(#)Makefile	8.1 (Berkeley) 6/8/93
# $FreeBSD: src/share/skel/Makefile,v 1.6.6.2 2002/08/07 16:31:51 ru Exp $

FILES1= dot.cshrc dot.login dot.login_conf dot.mailrc dot.profile dot.shrc
FILES2=	dot.mail_aliases dot.rhosts 
MODE1=	0644
MODE2=	0600

NOOBJ=	noobj

all clean cleandir depend lint tags:

install:
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${MODE1} ${FILES1} \
	    ${DESTDIR}${BINDIR}/skel
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m ${MODE2} ${FILES2} \
	    ${DESTDIR}${BINDIR}/skel

.include <bsd.prog.mk>
