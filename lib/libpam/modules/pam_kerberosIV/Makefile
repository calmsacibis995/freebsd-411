# Copyright 1998 Juniper Networks, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
#	$FreeBSD: src/lib/libpam/modules/pam_kerberosIV/Makefile,v 1.5.2.5 2004/12/13 13:49:59 marius Exp $


LIB=		pam_kerberosIV
SRCS=		pam_kerberosIV.c klogin.c
CFLAGS+=	-Wall
CFLAGS+=	-I${PAMDIR}/libpam/include
CFLAGS+=	-I${.CURDIR}/../../libpam
CFLAGS+=	-DKERBEROS
DPADD+=		${LIBKRB}
LDADD+=		-lkrb
DPADD+=		${LIBCRYPTO}
LDADD+=		-lcrypto
DPADD+=		${LIBCOM_ERR}
LDADD+=		-lcom_err

.include <bsd.lib.mk>
