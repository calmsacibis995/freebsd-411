# $FreeBSD: src/gnu/usr.bin/perl/library/Makefile.inc,v 1.5.2.4 2002/07/16 14:05:30 ru Exp $

PERL5SRC?=	${.CURDIR}/../../../../../contrib/perl5
PERL5LIBSRC?=	${.CURDIR}/../../libperl

MODULEDIR?=	${MODULE}

MAKEMAKER_ARGS=	INSTALLDIRS=perl PERL_SRC=${.OBJDIR} \
		INSTALLMAN3DIR=${DESTDIR}/usr/share/perl/man3 \
		PERL=${MINIPERL} FULLPERL=perl DEFINE=-I${DESTDIR}/usr/include \
		DEFINE=-DPERL_CORE

CFLAGS+=	-L${.OBJDIR}/../../libperl

all:	lib/auto/${MODULE}.so

.include "../Makefile.inc"

CLEANFILES+= ext.libs

lib/auto/${MODULE}.so: ext/${MODULEDIR}/Makefile ${DYNALOADER}
	@cd ext/${MODULEDIR}; \
	make -B all PERL_SRC=${.OBJDIR}

ext/${MODULEDIR}/Makefile: scripts
	@cd ext/${MODULEDIR}; \
	${MINIPERL} -I${.OBJDIR}/lib Makefile.PL ${MAKEMAKER_ARGS} \
		LINKTYPE=dynamic LIBS="-lperl -lm" \
		INST_LIB=${.OBJDIR}/build/${MODULEDIR} \
		INST_ARCHLIB=${.OBJDIR}/build/${MODULEDIR}; \
	make -B config PERL_SRC=${.OBJDIR}

realinstall:
	@cd ${.OBJDIR}/ext/${MODULEDIR} ;\
	make -B install \
		INSTALLPRIVLIB=${DESTDIR}/usr/libdata/perl/${VERSION} \
		INSTALLARCHLIB=${DESTDIR}/usr/libdata/perl/${VERSION}/mach

.PATH:	${PERL5SRC}
