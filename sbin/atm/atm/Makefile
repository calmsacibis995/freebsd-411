#
#
# ===================================
# HARP  |  Host ATM Research Platform
# ===================================
#
#
# This Host ATM Research Platform ("HARP") file (the "Software") is
# made available by Network Computing Services, Inc. ("NetworkCS")
# "AS IS".  NetworkCS does not provide maintenance, improvements or
# support of any kind.
#
# NETWORKCS MAKES NO WARRANTIES OR REPRESENTATIONS, EXPRESS OR IMPLIED,
# INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY
# AND FITNESS FOR A PARTICULAR PURPOSE, AS TO ANY ELEMENT OF THE
# SOFTWARE OR ANY SUPPORT PROVIDED IN CONNECTION WITH THIS SOFTWARE.
# In no event shall NetworkCS be responsible for any damages, including
# but not limited to consequential damages, arising from or relating to
# any use of the Software or related support.
#
# Copyright 1994-1998 Network Computing Services, Inc.
#
# Copies of this Software may be made, however, the above copyright
# notice must be reproduced on all copies.
#
#	@(#) $FreeBSD: src/sbin/atm/atm/Makefile,v 1.4.2.2 2003/07/22 15:04:50 harti Exp $
#
#

PROG=	atm
SRCS=	atm.c atm_fore200.c atm_eni.c atm_inet.c atm_print.c \
	atm_set.c atm_show.c atm_subr.c atm_proatm.c
MAN=	atm.8

CFLAGS+=	-I${.CURDIR}/../../../sys
LDADD+=		-latm
DPADD+=		 ${LIBATM}

.include <bsd.prog.mk>
