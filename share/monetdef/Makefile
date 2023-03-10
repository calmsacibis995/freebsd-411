# $FreeBSD: src/share/monetdef/Makefile,v 1.37.2.6 2003/11/04 05:03:27 murray Exp $

NOMAN=YES
CLEANFILES+= ${LOCALES:S/$/.out/g}

LOCALES=        af_ZA.ISO8859-1 \
		am_ET.UTF-8 \
		bg_BG.CP1251 \
		cs_CZ.ISO8859-2 \
		da_DK.ISO8859-1 \
		de_AT.ISO8859-1 \
		de_CH.ISO8859-1 \
		de_DE.ISO8859-1 \
		el_GR.ISO8859-7 \
		en_AU.ISO8859-1 \
		en_CA.ISO8859-1 \
		en_GB.ISO8859-1 \
		en_NZ.ISO8859-1 \
		en_US.ISO8859-1 \
		es_ES.ISO8859-1 \
		et_EE.ISO8859-15 \
		fi_FI.ISO8859-1 \
		fr_BE.ISO8859-1 \
		fr_CA.ISO8859-1 \
		fr_FR.ISO8859-1 \
		hi_IN.ISCII-DEV \
		hr_HR.ISO8859-2 \
		hu_HU.ISO8859-2 \
		is_IS.ISO8859-1 \
		it_IT.ISO8859-1 \
		ja_JP.eucJP \
		ko_KR.eucKR \
		nl_BE.ISO8859-1 \
		nl_NL.ISO8859-1 \
		no_NO.ISO8859-1 \
		pl_PL.ISO8859-2 \
		pt_BR.ISO8859-1 \
		pt_PT.ISO8859-1 \
		ro_RO.ISO8859-2 \
		ru_RU.CP1251 \
		ru_RU.CP866 \
		ru_RU.ISO8859-5 \
		ru_RU.KOI8-R \
		sk_SK.ISO8859-2 \
		sl_SI.ISO8859-2 \
		sv_SE.ISO8859-1 \
		tr_TR.ISO8859-9 \
		uk_UA.ISO8859-5 \
		uk_UA.KOI8-U \
		zh_CN.eucCN \
		zh_CN.GB18030 \
		zh_TW.Big5

LOCALEDIR=      ${DESTDIR}/usr/share/locale

ASCIILINKS=	en_AU en_CA en_GB en_NZ en_US
LATIN15LINKS=	af_ZA da_DK de_AT de_CH de_DE en_AU en_CA en_GB en_NZ en_US \
		es_ES fi_FI fr_BE fr_CA fr_CH fr_FR is_IS is_IS it_IT it_CH \
		nl_BE nl_NL no_NO pt_PT sv_SE

CH_SRC=		de_CH
CH_LINKS=	fr_CH it_CH

.SUFFIXES: .src .out

.src.out:
	grep -v '^#' < ${.IMPSRC} > ${.TARGET}

all: ${LOCALES:S/$/.out/g}

afterinstall:
.for lang in ${LOCALES}
	${INSTALL} -m 644 -o ${BINOWN} -g ${BINGRP} ${lang}.out \
		${LOCALEDIR}/${lang}/LC_MONETARY
.endfor
.for link in ${LATIN15LINKS}
	ln -sf ../${link}.ISO8859-1/LC_MONETARY \
	       ${LOCALEDIR}/${link}.ISO8859-15/LC_MONETARY
.endfor
.for link in ${ASCIILINKS}
	ln -sf ../${link}.ISO8859-1/LC_MONETARY \
		${LOCALEDIR}/${link}.US-ASCII/LC_MONETARY
.endfor
.for link in ${CH_LINKS}
	ln -sf ../${CH_SRC}.ISO8859-1/LC_MONETARY \
		${LOCALEDIR}/${link}.ISO8859-1/LC_MONETARY
.endfor
	ln -sf ../ja_JP.eucJP/LC_MONETARY \
		${LOCALEDIR}/ja_JP.SJIS/LC_MONETARY

.include <bsd.prog.mk>
