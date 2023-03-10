#	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/lib/Makefile,v 1.107.2.18 2004/12/13 14:10:30 marius Exp $

# To satisfy shared library or ELF linkage when only the libraries being
# built are visible:
#
# csu must be built before all shared libaries for ELF.
# libcom_err must be built before libkrb and libpam.
# libcrypt must be built before libkrb, libpam and libskey.
# msun must be built before libg++ and libstdc++.
# libmd must be built before libatm, libopie, libradius, libskey, and
# libtacplus.
# libncurses must be built before libdialog, libedit and libreadline.
# libradius must be built before libpam.
# libskey must be built before libpam.
# libtacplus must be built before libpam.
# libutil must be built before libpam.
# libsbuf must be built before libcam.
#
# Otherwise, the SUBDIR list should be in alphabetical order.

SUBDIR=	${_csu} libcom_err libcrypt msun libmd \
	libncurses libradius libskey libtacplus libutil libsbuf \
	${_compat} libalias libatm ${_libbind} libbz2 libc ${_libc_r} libcalendar \
	libcam libcompat libdevstat libdisk libedit libfetch libform \
	libftpio ${_libio} libipsec libipx libisc libkvm libmenu \
	${_libmilter} ${_libncp} \
	libnetgraph libopie libpam libpanel libpcap \
	libposix1e librpcsvc ${_libsm} ${_libsmb} ${_libsmdb} \
	${_libsmutil} \
	libstand ${_libtelnet} libusbhid ${_libvgl} libwrap libxpg4 liby libz

.if exists(${.CURDIR}/csu/${MACHINE_ARCH}-${OBJFORMAT})
_csu=csu/${MACHINE_ARCH}-${OBJFORMAT}
.elif exists(${.CURDIR}/csu/${MACHINE_ARCH})
_csu=csu/${MACHINE_ARCH}
.endif

.if !defined(NOLIBC_R)
_libc_r=	libc_r
.endif

.if !defined(NO_BIND)
_libbind=	libbind
.endif

.if !defined(NO_SENDMAIL)
_libmilter=	libmilter
_libsm=		libsm
_libsmdb=	libsmdb
_libsmutil=	libsmutil
.endif

.if ${MACHINE_ARCH} == "i386"
_compat=	compat
_libncp=	libncp
_libsmb=	libsmb
_libvgl=	libvgl
.endif

.if ${MACHINE_ARCH} == "alpha"
_libio=		libio
_compat=	compat
.endif

.if defined(RELEASEDIR) || \
    !exists(${.CURDIR}/../crypto) || defined(NOCRYPT) || defined(NO_OPENSSL)
_libtelnet=	libtelnet
.endif

.include <bsd.subdir.mk>
