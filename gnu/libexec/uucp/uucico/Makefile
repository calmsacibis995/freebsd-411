# Makefile for uucico
# $FreeBSD: src/gnu/libexec/uucp/uucico/Makefile,v 1.12.2.1 2001/04/25 12:03:22 ru Exp $

BINDIR=         $(libxdir)
BINOWN=		$(owner)
BINGRP=         dialer
BINMODE=        6555

PROG=		uucico
SRCS=		uucico.c trans.c send.c rec.c xcmd.c prot.c protg.c protf.c \
		prott.c prote.c proti.c protj.c proty.c protz.c time.c log.c \
		chat.c conn.c util.c copy.c
LDADD+=		$(LIBUNIX) $(LIBUUCONF) $(LIBUUCP)
DPADD+=		$(LIBUNIX) $(LIBUUCONF) $(LIBUUCP)
CFLAGS+=	-I$(.CURDIR)/../common_sources\
		-DVERSION=\"$(VERSION)\" -DOWNER=\"$(owner)\"

MAN=		uucico.8

.include <bsd.prog.mk>
.PATH: $(.CURDIR)/../common_sources
