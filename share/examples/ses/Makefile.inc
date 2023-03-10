# $NetBSD: $
# $OpenBSD: $
# $FreeBSD: src/share/examples/ses/Makefile.inc,v 1.1 2000/02/29 05:44:06 mjacob Exp $
#
# Copyright (c) 2000 by Matthew Jacob
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions, and the following disclaimer,
#    without modification, immediately at the beginning of the file.
# 2. The name of the author may not be used to endorse or promote products
#    derived from this software without specific prior written permission.
#
# Alternatively, this software may be distributed under the terms of the
# the GNU Public License ("GPL").
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
# 
# Matthew Jacob
# Feral Software
# mjacob@feral.com
#

SYS	!=	uname -s

.if ${SYS} == OpenBSD
COPTS	+=	-I/usr/include/scsi -DSESINC="<ses.h>"
.elif	${SYS} == NetBSD
COPTS	+=	-I/usr/include/dev/scsipi -DSESINC="<ses.h>"
.else
COPTS	+=	-I/usr/include/cam/scsi -DSESINC="<scsi_ses.h>"
.endif
BINDIR	?=	${DESTDIR}/usr/sbin

.PATH:	../srcs
