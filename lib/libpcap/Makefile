# Makefile for libpcap
# $FreeBSD: src/lib/libpcap/Makefile,v 1.24.2.4 2002/07/22 14:21:50 ru Exp $

LIB=    pcap
SRCS=	grammar.y tokdefs.h pcap-bpf.c \
	pcap.c inet.c gencode.c optimize.c nametoaddr.c \
	etherent.c savefile.c bpf_filter.c bpf_image.c bpf_dump.c \
	scanner.l version.c
INCS=	pcap.h pcap-int.h pcap-namedb.h
MAN=	pcap.3
CLEANFILES=tokdefs.h version.c

CFLAGS+=-DHAVE_CONFIG_H -Dyylval=pcap_lval -I${.CURDIR} -I.
.if !defined(NOINET6)
CFLAGS+=-DINET6
.endif

SHLIB_MAJOR=2
SHLIB_MINOR=5

#
# Magic to grab sources out of src/contrib
#
PCAP_DISTDIR?=${.CURDIR}/../../contrib/libpcap
CFLAGS+=-I${PCAP_DISTDIR}
.PATH:	${PCAP_DISTDIR}
.PATH:	${PCAP_DISTDIR}/bpf/net

version.c: ${PCAP_DISTDIR}/VERSION
	@rm -f $@
	sed 's/.*/char pcap_version[] = "&";/' ${PCAP_DISTDIR}/VERSION > $@

tokdefs.h: grammar.h
	ln -sf grammar.h tokdefs.h

.include <bsd.lib.mk>
