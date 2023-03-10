#	from: @(#)Makefile	6.1 (Berkeley) 3/3/91
# $FreeBSD: src/gnu/usr.bin/as/Makefile,v 1.18.2.1 2001/04/25 12:03:26 ru Exp $

.include "config/Makefile.$(MACHINE_ARCH)"

.if !defined (gas_hosttype)
gas_hosttype=$(MACHINE_ARCH)
.endif
.if !defined (gas_target)
gas_target=$(MACHINE_ARCH)
.endif
.if !defined (gas_objformat)
gas_objformat=aout
.endif

ADDINCLUDE=-I${.OBJDIR}

PROG=		as
BINDIR= /usr/libexec/aout
MAN=		as.1aout
SRCS+=		app.c as.c atof-generic.c bignum-copy.c \
		cond.c expr.c flo-const.c flo-copy.c flonum-mult.c \
		frags.c hash.c hex-value.c input-file.c input-scrub.c \
		listing.c messages.c obstack.c output-file.c read.c subsegs.c \
		symbols.c version.c write.c xmalloc.c xrealloc.c \
		obj-$(gas_objformat).c ${CONF_HEADERS}
CFLAGS+=	-I$(.CURDIR) ${ADDINCLUDE} -I$(.CURDIR)/config \
		-DOLD_GAS -DSIGTY=void -Derror=as_fatal \
		-DSUB_SEGMENT_ALIGN=4 -DFREEBSD_AOUT

CONF_HEADERS=	targ-cpu.h obj-format.h host.h targ-env.h

.PATH: $(.CURDIR)/config

targ-cpu.h: Makefile config/Makefile.$(MACHINE_ARCH) $(.CURDIR)/config/tc-$(gas_target).h
	@cmp -s $(.CURDIR)/config/tc-$(gas_target).h targ-cpu.h || \
	    ( ${ECHO} "updating ${.TARGET}..." ; /bin/rm -f targ-cpu.h ; \
	    cp $(.CURDIR)/config/tc-$(gas_target).h targ-cpu.h )

obj-format.h: Makefile config/Makefile.$(MACHINE_ARCH) $(.CURDIR)/config/obj-$(gas_objformat).h
	@cmp -s $(.CURDIR)/config/obj-$(gas_objformat).h obj-format.h || \
	    ( ${ECHO} "updating ${.TARGET}..." ; /bin/rm -f obj-format.h ; \
	    cp $(.CURDIR)/config/obj-$(gas_objformat).h obj-format.h )

.if exists ($(.CURDIR)/config/ho-$(gas_hosttype).h)
config_hostfile=	$(.CURDIR)/config/ho-$(gas_hosttype).h
.else
config_hostfile=	$(.CURDIR)/config/ho-generic.h
.endif

host.h: Makefile config/Makefile.$(MACHINE_ARCH) $(config_hostfile)
	@cmp -s $(config_hostfile) host.h || \
	    ( ${ECHO} "updating ${.TARGET}..." ; /bin/rm -f host.h ; \
	    cp $(config_hostfile) host.h )

.if exists ($(.CURDIR)/config/te-$(MACHINE_ARCH).h)
config_targenvfile=	$(.CURDIR)/config/te-$(MACHINE_ARCH).h
.else
config_targenvfile=	$(.CURDIR)/config/te-generic.h
.endif

targ-env.h: Makefile config/Makefile.$(MACHINE_ARCH) $(config_targenvfile)
	@cmp -s $(config_targenvfile) targ-env.h || \
	    ( ${ECHO} "updating ${.TARGET}..." ; /bin/rm -f targ-env.h ; \
	    cp $(config_targenvfile) targ-env.h )

CLEANFILES+= ${CONF_HEADERS}

.include <bsd.prog.mk>
