# $FreeBSD: src/gnu/usr.bin/perl/miniperl/Makefile,v 1.11.2.2 2002/02/19 16:21:34 ru Exp $

PROG=	miniperl
NOMAN=	true
SRCS=	miniperlmain.c
SRCS+=	perl.c gv.c toke.c perly.c op.c regcomp.c dump.c util.c mg.c \
	hv.c av.c run.c pp_hot.c sv.c pp.c scope.c pp_ctl.c pp_sys.c \
	doop.c doio.c regexec.c taint.c universal.c \
	globals.c perlio.c config.h
CFLAGS+=-I${.OBJDIR} -I${PERL5SRC}
DPADD=	${LIBM} ${LIBCRYPT}
LDADD=	-lm -lcrypt

# In FreeBSD 4.0, setproctitle(3) is in -lutil.
.if defined(BOOTSTRAPPING)
DPADD+=	${LIBUTIL}
LDADD+=	-lutil
.endif

# Miniperl _must_ be static!!
NOSHARED= yes

# We need miniperl early in `depend'.
afterdepend: all

build-tools: depend all

install:

NO_PERL_SCRIPT_MAKE=	true

.include <bsd.prog.mk>

.PATH:	${PERL5SRC}

config.h: links
	@sh config_h.sh

.SUFFIXES:

.SUFFIXES: .o .po .So .s .S .c
