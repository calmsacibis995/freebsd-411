# $FreeBSD: src/release/doc/de_DE.ISO8859-1/readme/Makefile,v 1.1.2.1 2002/03/12 21:27:03 ue Exp $
# $FreeBSDde: de-docproj/relnotes/de_DE.ISO8859-1/readme/Makefile,v 1.2.2.1 2002/03/11 01:35:05 ue Exp $
# $Id: Makefile,v 1.5 2002/03/10 15:37:18 ue Exp $
# basiert auf: 1.3.2.5

RELN_ROOT?= ${.CURDIR}/../..
.ifdef NO_LANGCODE_IN_DESTDIR
DESTDIR?= ${DOCDIR}/readme
.else
DESTDIR?= ${DOCDIR}/de_DE.ISO8859-1/readme
.endif

DOC?= article
FORMATS?= html
INSTALL_COMPRESSED?= gz
INSTALL_ONLY_COMPRESSED?=

# 
# SRCS lists the individual SGML files that make up the document. Changes
# to any of these files will force a rebuild
#

# SGML content
SRCS+=	article.sgml

.include "${RELN_ROOT}/share/mk/doc.relnotes.mk"
.include "${DOC_PREFIX}/share/mk/doc.project.mk"
