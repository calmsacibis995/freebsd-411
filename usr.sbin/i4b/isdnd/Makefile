# $FreeBSD: src/usr.sbin/i4b/isdnd/Makefile,v 1.3.2.3 2001/08/01 17:45:03 obrien Exp $

PROG=	isdnd
MAN=	isdnd.rc.5 isdnd.rates.5 isdnd.acct.5 isdnd.8
SRCS=	rc_parse.y rc_scan.l main.c rc_config.c log.c curses.c	\
	process.c rates.c msghdl.c fsm.c support.c timer.c	\
	exec.c dial.c monitor.c pcause.c controller.c alias.c	\
	y.tab.h holiday.c

COPTS+= -I. -I${.CURDIR}/../isdnmonitor -I${.CURDIR}/../isdntel -I${.CURDIR}

# compile debug support
COPTS+=	-DDEBUG

# enable rtprio usage
COPTS+=	-DUSE_RTPRIO

.include "${.CURDIR}/../Makefile.inc"

.if !defined(I4B_WITHOUT_CURSES)
COPTS+=	-DUSE_CURSES
DPADD=	${LIBCURSES}
LDADD=	-lcurses
.endif

.if defined(I4B_EXTERNAL_MONITOR)
COPTS+=	-DI4B_EXTERNAL_MONITOR
.if defined(I4B_NOTCPIP_MONITOR)
COPTS+=	-DI4B_NOTCPIP_MONITOR
.endif
.endif

.include <bsd.prog.mk>
