# $FreeBSD: src/sys/modules/ipfilter/Makefile,v 1.2.2.3 2004/07/05 16:23:17 darrenr Exp $ 

.PATH: ${.CURDIR}/../../contrib/ipfilter/netinet
KMOD=	ipl
SRCS=	mlfk_ipl.c ip_nat.c ip_frag.c ip_state.c ip_proxy.c ip_auth.c \
	ip_log.c ip_fil.c fil.c
NOMAN=
.if !defined(NOINET6)
CFLAGS+= -DUSE_INET6
.endif
CFLAGS+= -DIPFILTER_LKM -DIPFILTER_LOG
CFLAGS+=  -I${.CURDIR}/../../contrib/ipfilter
#
# If you don't want log functionality remove -DIPFILTER_LOG
#

.include <bsd.kmod.mk>
