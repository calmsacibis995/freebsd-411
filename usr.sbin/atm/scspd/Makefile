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
#	@(#) $FreeBSD: src/usr.sbin/atm/scspd/Makefile,v 1.6.2.2 2001/04/25 12:09:19 ru Exp $
#
#

PROG=	scspd
SRCS=	scspd.c scsp_cafsm.c scsp_config.c scsp_config_lex.c \
	scsp_config_parse.y \
	scsp_hfsm.c scsp_if.c scsp_input.c scsp_log.c scsp_msg.c \
	scsp_output.c scsp_print.c scsp_socket.c scsp_subr.c \
	scsp_timer.c
MAN=	scspd.8

CFLAGS+=	-I. -I${.CURDIR}/../../../sys -I${.CURDIR}
LDADD+=		-latm -lmd
DPADD+=		${LIBATM} ${LIBMD}

YFLAGS=		-d

.include <bsd.prog.mk>
