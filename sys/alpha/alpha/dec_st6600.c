/*-
 * Copyright (c) 1999 Andrew Gallatin
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
 * $FreeBSD: src/sys/alpha/alpha/dec_st6600.c,v 1.5.2.2 2000/07/20 06:12:13 obrien Exp $
 */

#include "opt_ddb.h"
#include <sys/param.h>
#include <sys/reboot.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <machine/intr.h>

#include <sys/termios.h>

#include <machine/rpb.h>
#include <machine/cpuconf.h>
#include <machine/clock.h>
#include <pci/pcireg.h>
#include <pci/pcivar.h>
#include <alpha/pci/tsunamireg.h>
#include <alpha/pci/tsunamivar.h>

#include "sio.h"
#include "sc.h"

#ifndef	CONSPEED
#define	CONSPEED TTYDEF_SPEED
#endif
static int comcnrate = CONSPEED;

void st6600_init __P((void));
static void st6600_cons_init __P((void));
static void st6600_intr_init __P((void));
static void st6600_intr_map __P((void *));

#define ST6600_PCI_IRQ_BEGIN 8
#define ST6600_PCI_MAX_IRQ  63

extern int siocnattach __P((int, int));
extern int siogdbattach __P((int, int));
extern int sccnattach __P((void));

void
st6600_init()
{
	platform.family = "ST6600";

	if ((platform.model = alpha_dsr_sysname()) == NULL) {
		/* XXX Don't know the system variations, yet. */
		platform.model = alpha_unknown_sysname();
	}

	platform.iobus = "tsunami";
	platform.cons_init = st6600_cons_init;
	platform.pci_intr_init = st6600_intr_init;
	platform.pci_intr_map = st6600_intr_map;
}

extern int comconsole;

static void
st6600_cons_init()
{
	struct ctb *ctb;

	tsunami_init();
#ifdef DDB
	siogdbattach(0x2f8, 57600);
#endif

	ctb = (struct ctb *)(((caddr_t)hwrpb) + hwrpb->rpb_ctb_off);

	switch (ctb->ctb_term_type) {
	case 2:
		/* serial console ... */
		/* XXX */
		{
			/*
			 * Delay to allow PROM putchars to complete.
			 * FIFO depth * character time,
			 * character time = (1000000 / (defaultrate / 10))
			 */
			DELAY(160000000 / comcnrate);
			comconsole = 0;
			if (siocnattach(0x3f8, comcnrate))
				panic("can't init serial console");

			boothowto |= RB_SERIAL;
			break;
		}

	case 3:
		/* display console ... */
		/* XXX */
#if NSC > 0
		sccnattach();
#else
		panic("not configured to use display && keyboard console");
#endif
		break;

	default:
		printf("ctb->ctb_term_type = 0x%lx\n", ctb->ctb_term_type);
		printf("ctb->ctb_turboslot = 0x%lx\n", ctb->ctb_turboslot);

		panic("consinit: unknown console type %ld\n",
		    ctb->ctb_term_type);
	}
}

static void
st6600_intr_init()
{
	int i;

	for(i = ST6600_PCI_IRQ_BEGIN; i <= ST6600_PCI_MAX_IRQ; i++)
		platform.pci_intr_disable(i);
	/* From Linux... */

	platform.pci_intr_enable(55);	
	platform.pci_intr_enable(2);	
}

static void
st6600_intr_map(void *arg)
{

	return;
}

