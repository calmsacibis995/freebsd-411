#
# ----------------------------------------------------------------------------
# "THE BEER-WARE LICENSE" (Revision 42):
# <phk@login.dknet.dk> wrote this file.  As long as you retain this notice you
# can do whatever you want with this stuff. If we meet some day, and you think
# this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
# ----------------------------------------------------------------------------
#
# $FreeBSD: src/usr.sbin/ctm/ctm/Makefile,v 1.13.2.1 2001/04/25 12:09:24 ru Exp $
#

PROG=		ctm
NOTYET=		ctm_ed.c
SRCS=   	ctm.c ctm_input.c ctm_pass1.c ctm_pass2.c ctm_pass3.c \
		ctm_passb.c ctm_syntax.c ctm_ed.c
LDADD+=		-lmd
DPADD+=		${LIBMD}
MAN=		ctm.1 ctm.5
CFLAGS+=	-Wall

.if exists(${.CURDIR}/../../Makefile.inc)
.include "${.CURDIR}/../../Makefile.inc"
.endif
.include <bsd.prog.mk>
