# $FreeBSD: src/gnu/usr.bin/texinfo/makeinfo/Makefile,v 1.13.2.3 2003/06/26 09:05:55 ru Exp $

PROG=	makeinfo
SRCS=	cmds.c defun.c files.c footnote.c html.c \
	index.c insertion.c lang.c macro.c makeinfo.c multi.c \
	node.c sectioning.c toc.c xml.c

DPADD=	${LIBTXI}
LDADD=	${LIBTXI}

.include <bsd.prog.mk>

.PATH:	$(TXIDIR)/makeinfo $(TXIDIR)/doc
