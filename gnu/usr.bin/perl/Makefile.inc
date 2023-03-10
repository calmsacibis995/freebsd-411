# $FreeBSD: src/gnu/usr.bin/perl/Makefile.inc,v 1.12.2.5 2002/03/19 15:52:16 ru Exp $

PERL5SRC?= ${.CURDIR}/../../../../contrib/perl5
PERL5LIBSRC?= ${.CURDIR}/../libperl

.if exists(${.OBJDIR}/../miniperl/miniperl)
MINIPERL?=	${.OBJDIR}/../miniperl/miniperl
.else
MINIPERL?=	${.OBJDIR}/../../miniperl/miniperl
.endif

BINDIR?=	/usr/bin

SHLIB_MAJOR?=	3

VERSION=	5.00503

DYNALOADER= lib/auto/DynaLoader/DynaLoader.a

HEADERS=EXTERN.h INTERN.h XSUB.h XSlock.h av.h bytecode.h byterun.h \
	cc_runtime.h cop.h cv.h dosish.h embed.h embedvar.h fakethr.h form.h \
	gv.h handy.h hv.h intrpvar.h iperlsys.h keywords.h mg.h nostdio.h \
	objXSUB.h objpp.h op.h opcode.h patchlevel.h perl.h perlio.h \
	perlsdio.h perlsfio.h perlvars.h perly.h pp.h pp_proto.h proto.h \
	regcomp.h regexp.h regnodes.h scope.h sv.h thrdvar.h thread.h \
	unixish.h util.h

DEPEND_H=

.for I in ${HEADERS}
${.OBJDIR}/${I}: ${I}
	@ln -sf ${.OODATE} ${.TARGET}

CLEANFILES+= ${.OBJDIR}/${I}
DEPEND_H+= ${.OBJDIR}/${I}
.endfor

LDFLAGS+=	-L${.OBJDIR}/../libperl

links: ${DEPEND_H}
	@for d in ${FARMDIRS} ; do \
		rm -rf $${d} ;\
		for i in `cd ${PERL5SRC}; find $${d} -type d -a \! -name CVS` ;\
		do \
			mkdir -p $${i} ;\
		done ;\
		for i in `cd ${PERL5SRC}; find $${d} -type f | grep -v CVS` ;\
		do \
			ln -s ${PERL5SRC}/$${i} $${i} ;\
		done ;\
	done
	@-mkdir lib/auto
	@-mkdir build
	@ln -sf ${PERL5SRC}/config_h.SH config_h.sh
	@ln -sf ${PERL5SRC}/cflags.SH cflags.sh
	@ln -sf ${PERL5SRC}/ext/re/re.pm lib/re.pm
	@ln -sf ${PERL5SRC}/myconfig myconfig
	@ln -sf ${PERL5SRC}/miniperlmain.c miniperlmain.c
	@ln -sf ${PERL5SRC}/writemain.SH writemain.sh
	@ln -sf ${PERL5SRC}/regcomp.c regcomp.c
	@ln -sf ${PERL5SRC}/regexec.c regexec.c
	@ln -sf ${PERL5LIBSRC}/config.SH-${OBJFORMAT}.${MACHINE_ARCH} \
		config.sh
	@touch ${.TARGET}

scripts: links
	@sh config_h.sh
	@sh cflags.sh
	@sh writemain.sh
	@${MINIPERL} ${PERL5SRC}/configpm \
		Config.pm Porting/Glossary myconfig
	@ln -sf ../Config.pm lib/Config.pm
	@touch ${.TARGET}

.if !defined(NO_PERL_SCRIPT_MAKE)
config.h:	scripts
.endif

CLEANFILES+=	links scripts
CLEANFILES+=	config.h config.sh config_h.sh Config.pm
CLEANFILES+=	regcomp.c regexec.c miniperlmain.c
CLEANFILES+=	writemain writemain.sh cflags cflags.sh myconfig
CLEANDIRS+=	lib ext Porting hints build

FARMDIRS=	lib ext Porting hints

MAKEMAKER_ARGS=	INSTALLDIRS=perl PERL_SRC=${.OBJDIR} \
		INSTALLMAN3DIR=${DESTDIR}/usr/share/perl/man3 \
		PERL=${MINIPERL} FULLPERL=perl DEFINE=-I${DESTDIR}/usr/include \
		DEFINE=-DPERL_CORE

.if defined(PERL_DEBUGGING) && ${PERL_DEBUGGING} == "true"
CFLAGS+=        -DDEBUGGING
.endif

autosplit: scripts lib/*.pm lib/*/*.pm
	${MINIPERL} -I${.OBJDIR}/lib \
		-e 'use AutoSplit; autosplit_lib_modules(@ARGV)' \
		lib/*.pm lib/*/*.pm
	@touch ${.TARGET}

${DYNALOADER}:	scripts \
		ext/DynaLoader/DynaLoader.pm \
		ext/DynaLoader/Makefile
	@cd ext/DynaLoader; \
	make -B all PERL_SRC=${.OBJDIR}

ext/DynaLoader/DynaLoader.pm: scripts ext/DynaLoader/DynaLoader_pm.PL
	@cd ext/DynaLoader; \
	${MINIPERL} -I${.OBJDIR} -I${.OBJDIR}/lib DynaLoader_pm.PL DynaLoader.pm

ext/DynaLoader/XSLoader.pm: scripts ext/DynaLoader/XSLoader_pm.PL
	@cd ext/DynaLoader; \
	${MINIPERL} -I${.OBJDIR} -I${.OBJDIR}/lib XSLoader_pm.PL XSLoader.pm

ext/DynaLoader/Makefile: scripts ext/DynaLoader/Makefile.PL
	@cd ext/DynaLoader; \
	${MINIPERL} -I${.OBJDIR} -I${.OBJDIR}/lib Makefile.PL ${MAKEMAKER_ARGS} \
		LINKTYPE=static LIBS="-lperl -lm"; \
	make -B config PERL_SRC=${.OBJDIR}
