#	From: @(#)Makefile	8.1 (Berkeley) 6/10/93
# $FreeBSD: src/share/doc/smm/Makefile,v 1.14.2.3 2002/07/07 08:19:19 dougb Exp $

# The following modules do not describe FreeBSD:
# 14.uucpimpl, 15.uucpnet

# The following modules do not build/install:
# 13.amd (documentation is TeXinfo)
# 16.security 17.password (encumbered)

# The following modules are built with their programs:
# 07.lpd

SUBDIR=	title contents 01.setup 02.config 03.fsck 04.quotas 05.fastfs \
	06.nfs 11.timedop \
	12.timed 18.net

.if !defined(NO_SENDMAIL)
SUBDIR+=08.sendmailop 
.endif

.include <bsd.subdir.mk>
