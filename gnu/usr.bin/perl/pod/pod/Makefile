#
# $FreeBSD: src/gnu/usr.bin/perl/pod/pod/Makefile,v 1.5.2.3 2002/08/07 16:31:45 ru Exp $
#

PODS=	perl.pod perl5004delta.pod perlapio.pod perlbook.pod perlbot.pod \
	perlcall.pod perldata.pod perldebug.pod perldelta.pod perldiag.pod \
	perldsc.pod perlembed.pod perlfaq.pod perlfaq1.pod perlfaq2.pod \
	perlfaq3.pod perlfaq4.pod perlfaq5.pod perlfaq6.pod perlfaq7.pod \
	perlfaq8.pod perlfaq9.pod perlform.pod perlfunc.pod perlguts.pod \
	perlhist.pod perlipc.pod perllocale.pod perllol.pod perlmod.pod \
	perlmodinstall.pod perlmodlib.pod perlobj.pod perlop.pod \
	perlopentut.pod perlpod.pod perlport.pod perlre.pod perlref.pod \
	perlreftut.pod perlrun.pod perlsec.pod perlstyle.pod perlsub.pod \
	perlsyn.pod perlthrtut.pod perltie.pod perltoc.pod \
	perltoot.pod perltrap.pod perlvar.pod perlxs.pod perlxstut.pod

.for I in ${PODS:R}
${.OBJDIR}/${I}.pod: ${I}.pod
	@ln -sf ${.OODATE} ${.TARGET}

${I}.1: ${I}.pod
	${MINIPERL} ${MINIPERLOPT} ${.OBJDIR}/../pod2man/pod2man ${.OODATE} \
		> ${.TARGET}

MAN+=	${I}.1

CLEANFILES+= ${.OBJDIR}/${I}.pod ${I}.1
.endfor

.include <bsd.prog.mk>

install:
	cd ${PERL5SRC}/pod ;\
	${INSTALL} -o ${MANOWN} -g ${MANGRP} -m ${MANMODE} \
		${PODS} ${DESTDIR}/usr/libdata/perl/${VERSION}/pod

.PATH: ${PERL5SRC}/pod
