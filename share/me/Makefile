#	@(#)Makefile	8.2 (Berkeley) 3/27/94
# $FreeBSD: src/share/me/Makefile,v 1.5.6.3 2002/08/08 08:47:58 ru Exp $

MELIB=	${BINDIR}/me
TMLIB=	${BINDIR}/tmac
MESRCS=	acm.me chars.me deltext.me eqn.me float.me footnote.me \
	index.me letterhead.me local.me null.me refer.me sh.me \
	tbl.me thesis.me
TMSRCS=	tmac.orig_me
FILES=	${MESRCS:S/$/.tmp/} ${TMSRCS:S/$/.tmp/} revisions
CLEANFILES+=	${MESRCS:S/$/.tmp/} ${TMSRCS:S/$/.tmp/}

MAN=	orig_me.7

.for file in ${MESRCS} ${TMSRCS}
${file}.tmp: ${file}
	sed -f ${.CURDIR}/strip.sed < ${.ALLSRC} > ${.TARGET}
.endfor

.for file in ${MESRCS:S/$/.tmp/} revisions
FILESDIR_${file}=	${MELIB}
FILESNAME_${file}=	${file:R}
.endfor
.for file in ${TMSRCS:S/$/.tmp/}
FILESDIR_${file}=	${TMLIB}
FILESNAME_${file}=	${file:R}
.endfor

.include <bsd.prog.mk>
