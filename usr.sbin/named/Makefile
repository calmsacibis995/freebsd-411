# $FreeBSD: src/usr.sbin/named/Makefile,v 1.25.2.5 2002/07/07 08:17:07 dougb Exp $

USE_LIBBIND=	true
.include "${.CURDIR}/Makefile.inc"

.PATH: ${BIND_DIR}/bin/named
.PATH: ${BIND_DIR}/doc/man

PROG=	named
MAN=	named.conf.5 named.8 named-bootconf.8 nsupdate.8
SRCS=	tmp_version.c pathnames.h \
	db_dump.c db_load.c db_lookup.c db_save.c db_update.c \
	db_glue.c db_ixfr.c db_sec.c db_tsig.c \
	ns_parser.y ns_lexer.c ns_parseutil.c ns_ctl.c \
	ns_forw.c ns_init.c ns_main.c ns_maint.c ns_req.c \
	ns_resp.c ns_stats.c ns_ncache.c ns_xfr.c ns_glue.c \
	ns_udp.c ns_config.c ns_update.c ns_ixfr.c ns_signal.c \
	ns_sort.c ns_notify.c

.if exists(${.OBJDIR}/../../lib/libisc)
LIBISCDIR:=    ${.OBJDIR}/../../lib/libisc
.else
LIBISCDIR!=    cd ${.CURDIR}/../../lib/libisc; make -V .OBJDIR
.endif
LIBISC:=       ${LIBISCDIR}/libisc.a

DPADD+=         ${LIBISC}
LDADD+=         ${LIBISC}

HTMLS=	acl.html address_list.html comments.html config.html controls.html \
	docdef.html example.html include.html index.html key.html \
	logging.html master.html options.html server.html trusted-keys.html \
	zone.html
MISCS=	DynamicUpdate FAQ.1of2 FAQ.2of2 rfc2317-notes.txt style.txt
FILES=	${HTMLS} ${MISCS}
.PATH: ${BIND_DIR}/doc/html ${BIND_DIR}/doc/misc

.for file in ${HTMLS}
FILESDIR_${file}=	${DOCDIR}/bind/html
.endfor
.for file in ${MISCS}
FILESDIR_${file}=	${DOCDIR}/bind/misc
.endfor

.include <bsd.prog.mk>
