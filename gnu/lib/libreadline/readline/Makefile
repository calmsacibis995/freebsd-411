# $FreeBSD: src/gnu/lib/libreadline/readline/Makefile,v 1.5.2.3 2002/07/23 10:20:20 ru Exp $

SUBDIR= doc

LIB=    readline
MAN=	readline.3

TILDESRC= tilde.c
SRCS=     readline.c vi_mode.c funmap.c keymaps.c parens.c search.c \
	  rltty.c complete.c bind.c isearch.c display.c signals.c \
	  util.c kill.c undo.c macro.c input.c callback.c terminal.c \
	  nls.c xmalloc.c \
	  $(HISTSRC) $(TILDESRC)

INSTALLED_HEADERS= readline.h chardefs.h keymaps.h history.h tilde.h \
		   rlstdc.h rlconf.h

DPADD=	${LIBNCURSES}
LDADD=	-lncurses

INCSDIR=${INCLUDEDIR}/readline

.for hdr in ${INSTALLED_HEADERS}
INCS+=	${SRCDIR}/${hdr}
.endfor

.include <bsd.lib.mk>
