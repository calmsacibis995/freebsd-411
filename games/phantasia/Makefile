#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/phantasia/Makefile,v 1.16.2.2 2002/08/07 16:31:42 ru Exp $

PROG=	phantasia
SRCS=	main.c fight.c io.c interplayer.c gamesupport.c misc.c phantglobs.c
DPADD=	${LIBM} ${LIBCURSES} ${LIBTERMCAP} ${LIBCOMPAT}
LDADD=	-lm -lcurses -ltermcap -lcompat
DATAFILES=characs gold lastdead mess monsters motd scoreboard void
HIDEGAME=hidegame
MAN=	phantasia.6
CLEANFILES=${DATAFILES} cross-phantglobs.o map setup setup.o stamp.setuprun

all: stamp.setuprun

build-tools: setup

cross-phantglobs.o: phantglobs.c
	${CC} ${CFLAGS} -c -o ${.TARGET} ${.ALLSRC}

stamp.setuprun: monsters.asc setup
	./setup -m ${.CURDIR}/monsters.asc
	touch ${.TARGET}

setup: cross-phantglobs.o setup.o ${LIBM}
	${CC} -static ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${.ALLSRC:M*.o} -lm

beforeinstall:
.for file in ${DATAFILES}
.if !exists(${DESTDIR}/var/games/phantasia/${file})
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 660 \
	    ${file} ${DESTDIR}/var/games/phantasia
.endif
.endfor

# Make Phantasia map.  Change the map commands reflect your installation.
# PLOTDEVICE is used for plotting the map.  Change as appropriate.
map: map.c
	${CC} -static -O ${.CURDIR}/map.c -lplot -o ${.TARGET}
	./map | plot > /dev/tty

.include <bsd.prog.mk>
