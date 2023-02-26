/*
 * Copyright (c) 1995, David Greenman
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 * $FreeBSD: src/sys/dev/snc/if_snc_pccard.c,v 1.1.2.1 2000/10/21 03:30:03 nyan Exp $
 */

/*
 *	National Semiconductor  DP8393X SONIC Driver
 *
 *	This is the PC-Card attachment on FreeBSD
 *		written by Motomichi Matsuzaki <mzaki@e-mail.ne.jp> and
 *			   Hiroshi Yamashita <bluemoon@msj.biglobe.ne.jp>
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/kernel.h>

#include <sys/module.h>
#include <sys/bus.h>
#include <machine/bus.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_media.h>
#include <net/if_mib.h>

#include <dev/pccard/pccardvar.h>

#include <dev/snc/dp83932var.h>
#include <dev/snc/dp83932reg.h>
#include <dev/snc/if_sncvar.h>
#include <dev/snc/if_sncreg.h>

/*
 *      PC-Card (PCMCIA) specific code.
 */
static int	snc_pccard_probe(device_t);
static int	snc_pccard_attach(device_t);
static int	snc_pccard_detach(device_t);


static device_method_t snc_pccard_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		snc_pccard_probe),
	DEVMETHOD(device_attach,	snc_pccard_attach),
	DEVMETHOD(device_detach,	snc_pccard_detach),

	{ 0, 0 }
};

static driver_t snc_pccard_driver = {
	"snc",
	snc_pccard_methods,
	sizeof(struct snc_softc)
};

DRIVER_MODULE(if_snc, pccard, snc_pccard_driver, snc_devclass, 0, 0);

/*
 *      snc_pccard_detach - unload the driver and clear the table.
 *      XXX TODO:
 *      This is usually called when the card is ejected, but
 *      can be caused by a modunload of a controller driver.
 *      The idea is to reset the driver's view of the device
 *      and ensure that any driver entry points such as
 *      read and write do not hang.
 */
static int
snc_pccard_detach(device_t dev)
{
	struct snc_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->sc_if;

	if (sc->gone) {
		device_printf(dev, "already unloaded\n");
		return (0);
	}
	sncshutdown(sc);
	ifp->if_flags &= ~IFF_RUNNING;
	if_detach(ifp);
	sc->gone = 1;
	bus_teardown_intr(dev, sc->irq, sc->irq_handle);
	snc_release_resources(dev);
	return (0);
}

/* 
 * Probe framework for pccards.  Replicates the standard framework,
 * minus the pccard driver registration and ignores the ether address
 * supplied (from the CIS), relying on the probe to find it instead.
 */
static int
snc_pccard_probe(device_t dev)
{
	int     error;

	error = snc_alloc_port(dev, 0);
	error = max(error, snc_alloc_memory(dev, 0));
	error = max(error, snc_alloc_irq(dev, 0, 0));

	if (!error && !snc_probe(dev, SNEC_TYPE_PNP))
		error = ENOENT;

	snc_release_resources(dev);
	return (error);
}

static int
snc_pccard_attach(device_t dev)
{
	struct snc_softc *sc = device_get_softc(dev);
	int error;
	
	bzero(sc, sizeof(struct snc_softc));

	snc_alloc_port(dev, 0);
	snc_alloc_memory(dev, 0);
	snc_alloc_irq(dev, 0, 0);
		
	error = bus_setup_intr(dev, sc->irq, INTR_TYPE_NET,
			       sncintr, sc, &sc->irq_handle);
	if (error) {
		printf("snc_isa_attach: bus_setup_intr() failed\n");
		snc_release_resources(dev);
		return (error);
	}	       

	/* This interface is always enabled. */
	sc->sc_enabled = 1;

	/* pccard_get_ether(dev, ether_addr); */

	return snc_attach(dev);
} 
