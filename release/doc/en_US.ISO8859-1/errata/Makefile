# $FreeBSD: src/release/doc/en_US.ISO8859-1/errata/Makefile,v 1.3.2.3 2001/09/04 12:12:11 dd Exp $

RELN_ROOT?= ${.CURDIR}/../..
.ifdef NO_LANGCODE_IN_DESTDIR
DESTDIR?= ${DOCDIR}/errata
.else
DESTDIR?= ${DOCDIR}/en_US.ISO8859-1/errata
.endif

DOC?= article
FORMATS?= html
INSTALL_COMPRESSED?= gz
INSTALL_ONLY_COMPRESSED?=

# SGML content
SRCS+=	article.sgml

.include "${RELN_ROOT}/share/mk/doc.relnotes.mk"
.include "${DOC_PREFIX}/share/mk/doc.project.mk"
