# $FreeBSD: src/etc/isdn/Makefile,v 1.4.2.5 2002/08/07 16:31:40 ru Exp $

I4BETCPROG=	answer			\
		isdntel.sh		\
		record			\
		tell			\
		tell-record		\
		unknown_incoming

I4BETCFILE=	holidays.D		\
		isdnd.rates.A		\
		isdnd.rates.D		\
		isdnd.rates.F		\
		isdnd.rates.L		\
		isdnd.rates.UK.BT	\
		isdnd.rc.sample		\
		isdntel.alias.sample

install:
		for i in ${I4BETCPROG} ; do \
		  ${INSTALL} -o root -g wheel -m 700 $$i ${DESTDIR}/etc/isdn ; \
		done ; \
		for i in ${I4BETCFILE} ; do \
		  ${INSTALL} -o root -g wheel -m 600 $$i ${DESTDIR}/etc/isdn ; \
		done
