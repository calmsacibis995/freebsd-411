/*-
 * Copyright (c) 2000 Alfred Perlstein <alfred@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD: src/sys/netinet/accf_data.c,v 1.1.2.2 2000/09/20 21:34:13 ps Exp $
 */

#define ACCEPT_FILTER_MOD

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/malloc.h> 
#include <sys/unistd.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/stat.h>
#include <sys/mbuf.h>
#include <sys/resource.h>
#include <sys/sysent.h>
#include <sys/resourcevar.h>

/* accept filter that holds a socket until data arrives */

static void	sohasdata(struct socket *so, void *arg, int waitflag);

static struct accept_filter accf_data_filter = {
	"dataready",
	sohasdata,
	NULL,
	NULL
};

static moduledata_t accf_data_mod = {
	"accf_data",
	accept_filt_generic_mod_event,
	&accf_data_filter
};

DECLARE_MODULE(accf_data, accf_data_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);

static void
sohasdata(struct socket *so, void *arg, int waitflag)
{

	if (!soreadable(so)) {
		return;
	}

	so->so_upcall = NULL;
	so->so_rcv.sb_flags &= ~SB_UPCALL;
	soisconnected(so);
	return;
}
