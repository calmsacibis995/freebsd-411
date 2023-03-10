#
# $FreeBSD: src/gnu/usr.bin/perl/utils/Makefile.inc,v 1.3.2.1 2000/10/05 09:54:52 markm Exp $
#

PERL5SRC=	${.CURDIR}/../../../../../contrib/perl5
PERL5LIBSRC=	${.CURDIR}/../../libperl
MINIPERL=	${.OBJDIR}/../../miniperl/miniperl

.include "../Makefile.inc"

MINIPERLOPT?=	-I${.OBJDIR}/../../perl/lib -I${.OBJDIR}/../../perl -I${PERL5SRC}

${PROG}: ${PROG}.PL
	${MINIPERL} ${MINIPERLOPT} ${.OODATE}

${PROG}.1: ${PROG}.PL
	${MINIPERL} ${MINIPERLOPT} \
		${.OBJDIR}/../../pod/pod2man/pod2man ${.OODATE} > ${.TARGET}

${PROG}.PL: ${PERL5SRC}/utils/${PROG}.PL
	ln -sf ${.OODATE} ${.TARGET}

CLEANFILES+=	${PROG}.1 ${PROG}.PL pstruct

STRIP=
