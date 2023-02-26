/*
 * Copyright (c) 2003 Hidetoshi Shimokawa
 * Copyright (c) 1998-2002 Katsushi Kobayashi and Hidetoshi Shimokawa
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the acknowledgement as bellow:
 *
 *    This product includes software developed by K. Kobayashi and H. SHimokawa
 *
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 * $FreeBSD: src/sys/dev/firewire/fwohci_pci.c,v 1.3.2.20 2004/03/28 11:50:42 simokawa Exp $
 */

#define BOUNCE_BUFFER_TEST	0

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/queue.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/malloc.h>
#if defined(__FreeBSD__) && __FreeBSD_version >= 501102
#include <sys/lock.h>
#include <sys/mutex.h>
#endif
#include <machine/resource.h>

#if defined(__DragonFly__) || __FreeBSD_version < 500000
#include <machine/clock.h>		/* for DELAY() */
#endif

#ifdef __DragonFly__
#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>

#include "firewire.h"
#include "firewirereg.h"

#include "fwdma.h"
#include "fwohcireg.h"
#include "fwohcivar.h"
#else
#if __FreeBSD_version < 500000
#include <pci/pcivar.h>
#include <pci/pcireg.h>
#else
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#endif

#include <dev/firewire/firewire.h>
#include <dev/firewire/firewirereg.h>

#include <dev/firewire/fwdma.h>
#include <dev/firewire/fwohcireg.h>
#include <dev/firewire/fwohcivar.h>
#endif

static int fwohci_pci_attach(device_t self);
static int fwohci_pci_detach(device_t self);

/*
 * The probe routine.
 */
static int
fwohci_pci_probe( device_t dev )
{
#if 1
	u_int32_t id;

	id = pci_get_devid(dev);
	if (id == (FW_VENDORID_NEC | FW_DEVICE_UPD861)) {
		device_set_desc(dev, "NEC uPD72861");
		return 0;
	}
	if (id == (FW_VENDORID_NEC | FW_DEVICE_UPD871)) {
		device_set_desc(dev, "NEC uPD72871/2");
		return 0;
	}
	if (id == (FW_VENDORID_NEC | FW_DEVICE_UPD72870)) {
		device_set_desc(dev, "NEC uPD72870");
		return 0;
	}
	if (id == (FW_VENDORID_NEC | FW_DEVICE_UPD72873)) {
		device_set_desc(dev, "NEC uPD72873");
		return 0;
	}
	if (id == (FW_VENDORID_NEC | FW_DEVICE_UPD72874)) {
		device_set_desc(dev, "NEC uPD72874");
		return 0;
	}
	if (id == (FW_VENDORID_TI | FW_DEVICE_TITSB22)) {
		device_set_desc(dev, "Texas Instruments TSB12LV22");
		return 0;
	}
	if (id == (FW_VENDORID_TI | FW_DEVICE_TITSB23)) {
		device_set_desc(dev, "Texas Instruments TSB12LV23");
		return 0;
	}
	if (id == (FW_VENDORID_TI | FW_DEVICE_TITSB26)) {
		device_set_desc(dev, "Texas Instruments TSB12LV26");
		return 0;
	}
	if (id == (FW_VENDORID_TI | FW_DEVICE_TITSB43)) {
		device_set_desc(dev, "Texas Instruments TSB43AA22");
		return 0;
	}
	if (id == (FW_VENDORID_TI | FW_DEVICE_TITSB43A)) {
		device_set_desc(dev, "Texas Instruments TSB43AB22/A");
		return 0;
	}
	if (id == (FW_VENDORID_TI | FW_DEVICE_TITSB43AB23)) {
		device_set_desc(dev, "Texas Instruments TSB43AB23");
		return 0;
	}
	if (id == (FW_VENDORID_TI | FW_DEVICE_TITSB82AA2)) {
		device_set_desc(dev, "Texas Instruments TSB82AA2");
		return 0;
	}
	if (id == (FW_VENDORID_TI | FW_DEVICE_TIPCI4450)) {
		device_set_desc(dev, "Texas Instruments PCI4450");
		return 0;
	}
	if (id == (FW_VENDORID_TI | FW_DEVICE_TIPCI4410A)) {
		device_set_desc(dev, "Texas Instruments PCI4410A");
		return 0;
	}
	if (id == (FW_VENDORID_TI | FW_DEVICE_TIPCI4451)) {
		device_set_desc(dev, "Texas Instruments PCI4451");
		return 0;
	}
	if (id == (FW_VENDORID_SONY | FW_DEVICE_CX3022)) {
		device_set_desc(dev, "Sony CX3022");
		return 0;
	}
	if (id == (FW_VENDORID_VIA | FW_DEVICE_VT6306)) {
		device_set_desc(dev, "VIA VT6306");
		return 0;
	}
	if (id == (FW_VENDORID_RICOH | FW_DEVICE_R5C551)) {
		device_set_desc(dev, "Ricoh R5C551");
		return 0;
	}
	if (id == (FW_VENDORID_RICOH | FW_DEVICE_R5C552)) {
		device_set_desc(dev, "Ricoh R5C552");
		return 0;
	}
	if (id == (FW_VENDORID_APPLE | FW_DEVICE_PANGEA)) {
		device_set_desc(dev, "Apple Pangea");
		return 0;
	}
	if (id == (FW_VENDORID_APPLE | FW_DEVICE_UNINORTH)) {
		device_set_desc(dev, "Apple UniNorth");
		return 0;
	}
	if (id == (FW_VENDORID_LUCENT | FW_DEVICE_FW322)) {
		device_set_desc(dev, "Lucent FW322/323");
		return 0;
	}
#endif
	if (pci_get_class(dev) == PCIC_SERIALBUS
			&& pci_get_subclass(dev) == PCIS_SERIALBUS_FW
			&& pci_get_progif(dev) == PCI_INTERFACE_OHCI) {
		device_printf(dev, "vendor=%x, dev=%x\n", pci_get_vendor(dev),
			pci_get_device(dev));
		device_set_desc(dev, "1394 Open Host Controller Interface");
		return 0;
	}

	return ENXIO;
}

#if defined(__DragonFly__) || __FreeBSD_version < 500000
static void
fwohci_dummy_intr(void *arg)
{
	/* XXX do nothing */
}
#endif

static int
fwohci_pci_init(device_t self)
{
	int olatency, latency, ocache_line, cache_line;
	u_int16_t cmd;

	cmd = pci_read_config(self, PCIR_COMMAND, 2);
	cmd |= PCIM_CMD_MEMEN | PCIM_CMD_BUSMASTEREN | PCIM_CMD_MWRICEN |
		PCIM_CMD_SERRESPEN | PCIM_CMD_PERRESPEN;
#if 1
	cmd &= ~PCIM_CMD_MWRICEN; 
#endif
	pci_write_config(self, PCIR_COMMAND, cmd, 2);

	latency = olatency = pci_read_config(self, PCIR_LATTIMER, 1);
#define DEF_LATENCY 0x20
	if (olatency < DEF_LATENCY) {
		latency = DEF_LATENCY;
		pci_write_config(self, PCIR_LATTIMER, latency, 1);
	}

	cache_line = ocache_line = pci_read_config(self, PCIR_CACHELNSZ, 1);
#define DEF_CACHE_LINE 8
	if (ocache_line < DEF_CACHE_LINE) {
		cache_line = DEF_CACHE_LINE;
		pci_write_config(self, PCIR_CACHELNSZ, cache_line, 1);
	}

	if (firewire_debug) {
		device_printf(self, "latency timer %d -> %d.\n",
			olatency, latency);
		device_printf(self, "cache size %d -> %d.\n",
			ocache_line, cache_line);
	}

	return 0;
}

static int
fwohci_pci_attach(device_t self)
{
	fwohci_softc_t *sc = device_get_softc(self);
	int err;
	int rid;
#if defined(__DragonFly__) || __FreeBSD_version < 500000
	int intr;
	/* For the moment, put in a message stating what is wrong */
	intr = pci_read_config(self, PCIR_INTLINE, 1);
	if (intr == 0 || intr == 255) {
		device_printf(self, "Invalid irq %d\n", intr);
#ifdef __i386__
		device_printf(self, "Please switch PNP-OS to 'No' in BIOS\n");
#endif
	}
#endif

	if (bootverbose)
		firewire_debug = bootverbose;

	fwohci_pci_init(self);

	rid = PCI_CBMEM;
#if __FreeBSD_version >= 502109
	sc->bsr = bus_alloc_resource_any(self, SYS_RES_MEMORY, &rid, RF_ACTIVE);
#else
	sc->bsr = bus_alloc_resource(self, SYS_RES_MEMORY, &rid,
	    0, ~0, 1, RF_ACTIVE);
#endif
	if (!sc->bsr) {
		device_printf(self, "Could not map memory\n");
		return ENXIO;
        }

	sc->bst = rman_get_bustag(sc->bsr);
	sc->bsh = rman_get_bushandle(sc->bsr);

	rid = 0;
#if __FreeBSD_version >= 502109
	sc->irq_res = bus_alloc_resource_any(self, SYS_RES_IRQ, &rid,
				     RF_SHAREABLE | RF_ACTIVE);
#else
	sc->irq_res = bus_alloc_resource(self, SYS_RES_IRQ, &rid, 0, ~0, 1,
				     RF_SHAREABLE | RF_ACTIVE);
#endif
	if (sc->irq_res == NULL) {
		device_printf(self, "Could not allocate irq\n");
		fwohci_pci_detach(self);
		return ENXIO;
	}


	err = bus_setup_intr(self, sc->irq_res,
#if FWOHCI_TASKQUEUE
			INTR_TYPE_NET | INTR_MPSAFE,
#else
			INTR_TYPE_NET,
#endif
		     (driver_intr_t *) fwohci_intr, sc, &sc->ih);
#if defined(__DragonFly__) || __FreeBSD_version < 500000
	/* XXX splcam() should mask this irq for sbp.c*/
	err = bus_setup_intr(self, sc->irq_res, INTR_TYPE_CAM,
		     (driver_intr_t *) fwohci_dummy_intr, sc, &sc->ih_cam);
	/* XXX splbio() should mask this irq for physio()/fwmem_strategy() */
	err = bus_setup_intr(self, sc->irq_res, INTR_TYPE_BIO,
		     (driver_intr_t *) fwohci_dummy_intr, sc, &sc->ih_bio);
#endif
	if (err) {
		device_printf(self, "Could not setup irq, %d\n", err);
		fwohci_pci_detach(self);
		return ENXIO;
	}

	err = bus_dma_tag_create(/*parent*/NULL, /*alignment*/1,
				/*boundary*/0,
#if BOUNCE_BUFFER_TEST
				/*lowaddr*/BUS_SPACE_MAXADDR_24BIT,
#else
				/*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
#endif
				/*highaddr*/BUS_SPACE_MAXADDR,
				/*filter*/NULL, /*filterarg*/NULL,
				/*maxsize*/0x100000,
				/*nsegments*/0x20,
				/*maxsegsz*/0x8000,
				/*flags*/BUS_DMA_ALLOCNOW,
#if defined(__FreeBSD__) && __FreeBSD_version >= 501102
				/*lockfunc*/busdma_lock_mutex,
				/*lockarg*/&Giant,
#endif
				&sc->fc.dmat);
	if (err != 0) {
		printf("fwohci_pci_attach: Could not allocate DMA tag "
			"- error %d\n", err);
			return (ENOMEM);
	}

	err = fwohci_init(sc, self);

	if (err) {
		device_printf(self, "fwohci_init failed with err=%d\n", err);
		fwohci_pci_detach(self);
		return EIO;
	}

	/* probe and attach a child device(firewire) */
	bus_generic_probe(self);
	bus_generic_attach(self);

	return 0;
}

static int
fwohci_pci_detach(device_t self)
{
	fwohci_softc_t *sc = device_get_softc(self);
	int s;


	s = splfw();

	if (sc->bsr)
		fwohci_stop(sc, self);

	bus_generic_detach(self);
	if (sc->fc.bdev) {
		device_delete_child(self, sc->fc.bdev);
		sc->fc.bdev = NULL;
	}

	/* disable interrupts that might have been switched on */
	if (sc->bst && sc->bsh)
		bus_space_write_4(sc->bst, sc->bsh,
				  FWOHCI_INTMASKCLR, OHCI_INT_EN);

	if (sc->irq_res) {
		int err = bus_teardown_intr(self, sc->irq_res, sc->ih);
		if (err)
			/* XXX or should we panic? */
			device_printf(self, "Could not tear down irq, %d\n",
				      err);
#if defined(__DragonFly__) || __FreeBSD_version < 500000
		bus_teardown_intr(self, sc->irq_res, sc->ih_cam);
		bus_teardown_intr(self, sc->irq_res, sc->ih_bio);
#endif
		sc->ih = NULL;
	}

	if (sc->irq_res) {
		bus_release_resource(self, SYS_RES_IRQ, 0, sc->irq_res);
		sc->irq_res = NULL;
	}

	if (sc->bsr) {
		bus_release_resource(self, SYS_RES_MEMORY,PCI_CBMEM,sc->bsr);
		sc->bsr = NULL;
		sc->bst = 0;
		sc->bsh = 0;
	}

	fwohci_detach(sc, self);
	splx(s);

	return 0;
}

static int
fwohci_pci_suspend(device_t dev)
{
	fwohci_softc_t *sc = device_get_softc(dev);
	int err;

	device_printf(dev, "fwohci_pci_suspend\n");
	err = bus_generic_suspend(dev);
	if (err)
		return err;
	fwohci_stop(sc, dev);
	return 0;
}

static int
fwohci_pci_resume(device_t dev)
{
	fwohci_softc_t *sc = device_get_softc(dev);

#ifndef BURN_BRIDGES
	device_printf(dev, "fwohci_pci_resume: power_state = 0x%08x\n",
					pci_get_powerstate(dev));
	pci_set_powerstate(dev, PCI_POWERSTATE_D0);
#endif
	fwohci_pci_init(dev);
	fwohci_resume(sc, dev);
	return 0;
}

static int
fwohci_pci_shutdown(device_t dev)
{
	fwohci_softc_t *sc = device_get_softc(dev);

	bus_generic_shutdown(dev);
	fwohci_stop(sc, dev);
	return 0;
}

static device_t
fwohci_pci_add_child(device_t dev, int order, const char *name, int unit)
{
	struct fwohci_softc *sc;
	device_t child;
	int s, err = 0;

	sc = (struct fwohci_softc *)device_get_softc(dev);
	child = device_add_child(dev, name, unit);
	if (child == NULL)
		return (child);

	sc->fc.bdev = child;
	device_set_ivars(child, (void *)&sc->fc);

	err = device_probe_and_attach(child);
	if (err) {
		device_printf(dev, "probe_and_attach failed with err=%d\n",
		    err);
		fwohci_pci_detach(dev);
		device_delete_child(dev, child);
		return NULL;
	}

	/* XXX
	 * Clear the bus reset event flag to start transactions even when
	 * interrupt is disabled during the boot process.
	 */
	DELAY(250); /* 2 cycles */
	s = splfw();
	fwohci_poll((void *)sc, 0, -1);
	splx(s);

	return (child);
}

static device_method_t fwohci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		fwohci_pci_probe),
	DEVMETHOD(device_attach,	fwohci_pci_attach),
	DEVMETHOD(device_detach,	fwohci_pci_detach),
	DEVMETHOD(device_suspend,	fwohci_pci_suspend),
	DEVMETHOD(device_resume,	fwohci_pci_resume),
	DEVMETHOD(device_shutdown,	fwohci_pci_shutdown),

	/* Bus interface */
	DEVMETHOD(bus_add_child,	fwohci_pci_add_child),
	DEVMETHOD(bus_print_child,	bus_generic_print_child),

	{ 0, 0 }
};

static driver_t fwohci_driver = {
	"fwohci",
	fwohci_methods,
	sizeof(fwohci_softc_t),
};

static devclass_t fwohci_devclass;

#ifdef FWOHCI_MODULE
MODULE_DEPEND(fwohci, firewire, 1, 1, 1);
#endif
DRIVER_MODULE(fwohci, pci, fwohci_driver, fwohci_devclass, 0, 0);
DRIVER_MODULE(fwohci, cardbus, fwohci_driver, fwohci_devclass, 0, 0);
