/*	$NetBSD: puc.c,v 1.7 2000/07/29 17:43:38 jlam Exp $	*/

/*-
 * Copyright (c) 2002 JF Hay.  All rights reserved.
 * Copyright (c) 2000 M. Warner Losh.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1996, 1998, 1999
 *	Christopher G. Demetriou.  All rights reserved.
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
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/sys/dev/puc/puc.c,v 1.3.2.5 2003/04/04 08:42:17 sobomax Exp $");

/*
 * PCI "universal" communication card device driver, glues com, lpt,
 * and similar ports to PCI via bridge chip often much larger than
 * the devices being glued.
 *
 * Author: Christopher G. Demetriou, May 14, 1998 (derived from NetBSD
 * sys/dev/pci/pciide.c, revision 1.6).
 *
 * These devices could be (and some times are) described as
 * communications/{serial,parallel}, etc. devices with known
 * programming interfaces, but those programming interfaces (in
 * particular the BAR assignments for devices, etc.) in fact are not
 * particularly well defined.
 *
 * After I/we have seen more of these devices, it may be possible
 * to generalize some of these bits.  In particular, devices which
 * describe themselves as communications/serial/16[45]50, and
 * communications/parallel/??? might be attached via direct
 * 'com' and 'lpt' attachments to pci.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <pci/pcireg.h>
#include <pci/pcivar.h>
#include <dev/puc/pucvar.h>

#include <opt_puc.h>

struct puc_softc {
	const struct puc_device_description *sc_desc;

	/* card-global dynamic data */
	int			barmuxed;
	int			irqrid;
	struct resource		*irqres;
	void			*intr_cookie;
	int			ilr_enabled;
	bus_space_tag_t		ilr_st;
	bus_space_handle_t	ilr_sh;

	struct {
		struct resource	*res;
	} sc_bar_mappings[PUC_MAX_BAR];

	/* per-port dynamic data */
        struct {
		struct device	*dev;
		/* filled in by bus_setup_intr() */
		void		(*ihand) __P((void *));
		void		*ihandarg;
        } sc_ports[PUC_MAX_PORTS];
};

struct puc_device {
	struct resource_list resources;
	u_int serialfreq;
};

static int puc_pci_probe(device_t dev);
static int puc_pci_attach(device_t dev);
static void puc_intr(void *arg);

static struct resource *puc_alloc_resource(device_t, device_t, int, int *,
    u_long, u_long, u_long, u_int);
static int puc_release_resource(device_t, device_t, int, int,
    struct resource *);
static int puc_get_resource(device_t, device_t, int, int, u_long *, u_long *);
static int puc_setup_intr(device_t, device_t, struct resource *, int,
    void (*)(void *), void *, void **);
static int puc_teardown_intr(device_t, device_t, struct resource *,
    void *);
static int puc_read_ivar(device_t, device_t, int, uintptr_t *);

static const struct puc_device_description *puc_find_description(uint32_t,
    uint32_t, uint32_t, uint32_t);
static void puc_config_superio(device_t);
static void puc_config_win877(struct resource *);
static int puc_find_free_unit(char *);
#ifdef PUC_DEBUG
static void puc_print_win877(bus_space_tag_t, bus_space_handle_t, u_int,
    u_int);
static void puc_print_resource_list(struct resource_list *);
#endif

static int
puc_pci_probe(device_t dev)
{
	uint32_t v1, v2, d1, d2;
	const struct puc_device_description *desc;

	if ((pci_read_config(dev, PCIR_HEADERTYPE, 1) & 0x7f) != 0)
		return (ENXIO);

	v1 = pci_read_config(dev, PCIR_VENDOR, 2);
	d1 = pci_read_config(dev, PCIR_DEVICE, 2);
	v2 = pci_read_config(dev, PCIR_SUBVEND_0, 2);
	d2 = pci_read_config(dev, PCIR_SUBDEV_0, 2);

	desc = puc_find_description(v1, d1, v2, d2);
	if (desc == NULL)
		return (ENXIO);
	device_set_desc(dev, desc->name);
	return (0);
}

static int
puc_probe_ilr(struct puc_softc *sc, struct resource *res)
{
	u_char t1, t2;
	int i;

	switch (sc->sc_desc->ilr_type) {
	case PUC_ILR_TYPE_DIGI:
		sc->ilr_st = rman_get_bustag(res);
		sc->ilr_sh = rman_get_bushandle(res);
		for (i = 0; i < 2; i++) {
			t1 = bus_space_read_1(sc->ilr_st, sc->ilr_sh,
			    sc->sc_desc->ilr_offset[i]);
			t1 = ~t1;
			bus_space_write_1(sc->ilr_st, sc->ilr_sh,
			    sc->sc_desc->ilr_offset[i], t1);
			t2 = bus_space_read_1(sc->ilr_st, sc->ilr_sh,
			    sc->sc_desc->ilr_offset[i]);
			if (t2 == t1)
				return (0);
		}
		return (1);

	default:
		break;
	}
	return (0);
}

static int
puc_pci_attach(device_t dev)
{
	char *typestr;
	int bidx, childunit, i, irq_setup, rid;
	uint32_t v1, v2, d1, d2;
	struct puc_softc *sc;
	struct puc_device *pdev;
	struct resource *res;
	struct resource_list_entry *rle;

	sc = (struct puc_softc *)device_get_softc(dev);
	bzero(sc, sizeof(*sc));
	v1 = pci_read_config(dev, PCIR_VENDOR, 2);
	d1 = pci_read_config(dev, PCIR_DEVICE, 2);
	v2 = pci_read_config(dev, PCIR_SUBVEND_0, 2);
	d2 = pci_read_config(dev, PCIR_SUBDEV_0, 2);
	sc->sc_desc = puc_find_description(v1, d1, v2, d2);
	if (sc->sc_desc == NULL)
		return (ENXIO);

#ifdef PUC_DEBUG
	bootverbose = 1;

	printf("puc: name: %s\n", sc->sc_desc->name);
#endif
	rid = 0;
	res = bus_alloc_resource(dev, SYS_RES_IRQ, &rid, 0, ~0, 1,
	    RF_ACTIVE | RF_SHAREABLE);
	if (!res)
		return (ENXIO);

	sc->irqres = res;
	sc->irqrid = rid;
#ifdef PUC_FASTINTR
	irq_setup = BUS_SETUP_INTR(device_get_parent(dev), dev, res,
	    INTR_TYPE_TTY | INTR_TYPE_FAST, puc_intr, sc, &sc->intr_cookie);
#else
	irq_setup = ENXIO;
#endif
	if (irq_setup != 0)
		irq_setup = BUS_SETUP_INTR(device_get_parent(dev), dev, res,
		    INTR_TYPE_TTY, puc_intr, sc, &sc->intr_cookie);
	if (irq_setup != 0)
		return (ENXIO);

	rid = 0;
	for (i = 0; PUC_PORT_VALID(sc->sc_desc, i); i++) {
		if (rid == sc->sc_desc->ports[i].bar)
			sc->barmuxed = 1;
		rid = sc->sc_desc->ports[i].bar;
		bidx = PUC_PORT_BAR_INDEX(rid);

		if (sc->sc_bar_mappings[bidx].res != NULL)
			continue;
		res = bus_alloc_resource(dev, SYS_RES_IOPORT, &rid,
		    0ul, ~0ul, 1, RF_ACTIVE);
		if (res == NULL) {
			printf("could not get resource\n");
			continue;
		}
		sc->sc_bar_mappings[bidx].res = res;

		if (sc->sc_desc->ilr_type != PUC_ILR_TYPE_NONE) {
			sc->ilr_enabled = puc_probe_ilr(sc, res);
			if (sc->ilr_enabled)
				device_printf(dev, "ILR enabled\n");
			else
				device_printf(dev, "ILR disabled\n");
		}
#ifdef PUC_DEBUG
		printf("port bst %x, start %x, end %x\n",
		    (u_int)rman_get_bustag(res), (u_int)rman_get_start(res),
		    (u_int)rman_get_end(res));
#endif
	}

	puc_config_superio(dev);

	for (i = 0; PUC_PORT_VALID(sc->sc_desc, i); i++) {
		rid = sc->sc_desc->ports[i].bar;
		bidx = PUC_PORT_BAR_INDEX(rid);
		if (sc->sc_bar_mappings[bidx].res == NULL)
			continue;

		switch (sc->sc_desc->ports[i].type) {
		case PUC_PORT_TYPE_COM:
			typestr = "sio";
			break;
		default:
			continue;
		}
		pdev = malloc(sizeof(struct puc_device), M_DEVBUF,
		    M_NOWAIT | M_ZERO);
		if (!pdev)
			continue;
		resource_list_init(&pdev->resources);

		/* First fake up an IRQ resource. */
		resource_list_add(&pdev->resources, SYS_RES_IRQ, 0,
		    rman_get_start(sc->irqres), rman_get_end(sc->irqres),
		    rman_get_end(sc->irqres) - rman_get_start(sc->irqres) + 1);
		rle = resource_list_find(&pdev->resources, SYS_RES_IRQ, 0);
		rle->res = sc->irqres;

		/* Now fake an IOPORT resource */
		res = sc->sc_bar_mappings[bidx].res;
		resource_list_add(&pdev->resources, SYS_RES_IOPORT, 0,
		    rman_get_start(res) + sc->sc_desc->ports[i].offset,
		    rman_get_end(res) + sc->sc_desc->ports[i].offset + 8 - 1,
		    8);
		rle = resource_list_find(&pdev->resources, SYS_RES_IOPORT, 0);

		if (sc->barmuxed == 0) {
			rle->res = sc->sc_bar_mappings[bidx].res;
		} else {
			rle->res = malloc(sizeof(struct resource), M_DEVBUF,
			    M_WAITOK | M_ZERO);
			if (rle->res == NULL) {
				free(pdev, M_DEVBUF);
				return (ENOMEM);
			}

			rle->res->r_start = rman_get_start(res) +
			    sc->sc_desc->ports[i].offset;
			rle->res->r_end = rle->res->r_start + 8 - 1;
			rle->res->r_bustag = rman_get_bustag(res);
			bus_space_subregion(rle->res->r_bustag,
			    rman_get_bushandle(res),
			    sc->sc_desc->ports[i].offset, 8,
			    &rle->res->r_bushandle);
		}

		pdev->serialfreq = sc->sc_desc->ports[i].serialfreq;

		childunit = puc_find_free_unit(typestr);
		sc->sc_ports[i].dev = device_add_child(dev, typestr, childunit);
		if (sc->sc_ports[i].dev == NULL) {
			if (sc->barmuxed) {
				bus_space_unmap(rman_get_bustag(rle->res),
						rman_get_bushandle(rle->res),
						8);
				free(rle->res, M_DEVBUF);
				free(pdev, M_DEVBUF);
			}
			continue;
		}
		device_set_ivars(sc->sc_ports[i].dev, pdev);
		device_set_desc(sc->sc_ports[i].dev, sc->sc_desc->name);
		if (!bootverbose)
			device_quiet(sc->sc_ports[i].dev);
#ifdef PUC_DEBUG
		printf("puc: type %d, bar %x, offset %x\n",
		    sc->sc_desc->ports[i].type,
		    sc->sc_desc->ports[i].bar,
		    sc->sc_desc->ports[i].offset);
		print_resource_list(&pdev->resources);
#endif
		device_set_flags(sc->sc_ports[i].dev,
		    sc->sc_desc->ports[i].flags);
		if (device_probe_and_attach(sc->sc_ports[i].dev) != 0) {
			if (sc->barmuxed) {
				bus_space_unmap(rman_get_bustag(rle->res),
						rman_get_bushandle(rle->res),
						8);
				free(rle->res, M_DEVBUF);
				free(pdev, M_DEVBUF);
			}
		}
	}

#ifdef PUC_DEBUG
	bootverbose = 0;
#endif
	return (0);
}

static u_int32_t
puc_ilr_read(struct puc_softc *sc)
{
	u_int32_t mask;
	int i;

	mask = 0;
	switch (sc->sc_desc->ilr_type) {
	case PUC_ILR_TYPE_DIGI:
		for (i = 1; i >= 0; i--) {
			mask = (mask << 8) | (bus_space_read_1(sc->ilr_st,
			    sc->ilr_sh, sc->sc_desc->ilr_offset[i]) & 0xff);
		}
		break;

	default:
		mask = 0xffffffff;
		break;
	}
	return (mask);
}

/*
 * This is an interrupt handler. For boards that can't tell us which
 * device generated the interrupt it just calls all the registered
 * handlers sequencially, but for boards that can tell us which
 * device(s) generated the interrupt it calls only handlers for devices
 * that actually generated the interrupt.
 */
static void
puc_intr(void *arg)
{
	int i;
	u_int32_t ilr_mask;
	struct puc_softc *sc;

	sc = (struct puc_softc *)arg;
	ilr_mask = sc->ilr_enabled ? puc_ilr_read(sc) : 0xffffffff;
	for (i = 0; i < PUC_MAX_PORTS; i++)
		if (sc->sc_ports[i].ihand != NULL &&
		    ((ilr_mask >> i) & 0x00000001))
			(sc->sc_ports[i].ihand)(sc->sc_ports[i].ihandarg);
}

static const struct puc_device_description *
puc_find_description(uint32_t vend, uint32_t prod, uint32_t svend, 
    uint32_t sprod)
{
	int i;

#define checkreg(val, index) \
    (((val) & puc_devices[i].rmask[(index)]) == puc_devices[i].rval[(index)])

	for (i = 0; puc_devices[i].name != NULL; i++) {
		if (checkreg(vend, PUC_REG_VEND) &&
		    checkreg(prod, PUC_REG_PROD) &&
		    checkreg(svend, PUC_REG_SVEND) &&
		    checkreg(sprod, PUC_REG_SPROD))
			return (&puc_devices[i]);
	}

#undef checkreg

	return (NULL);
}

/*
 * It might be possible to make these more generic if we can detect patterns.
 * For instance maybe if the size of a bar is 0x400 (the old isa space) it
 * might contain one or more superio chips.
 */
static void
puc_config_superio(device_t dev)
{
	struct puc_softc *sc = (struct puc_softc *)device_get_softc(dev);

	if (sc->sc_desc->rval[PUC_REG_VEND] == 0x1592 &&
	    sc->sc_desc->rval[PUC_REG_PROD] == 0x0781)
		puc_config_win877(sc->sc_bar_mappings[0].res);
}

#define rdspio(indx)		(bus_space_write_1(bst, bsh, efir, indx), \
				bus_space_read_1(bst, bsh, efdr))
#define wrspio(indx,data)	(bus_space_write_1(bst, bsh, efir, indx), \
				bus_space_write_1(bst, bsh, efdr, data))

#ifdef PUC_DEBUG
static void
puc_print_win877(bus_space_tag_t bst, bus_space_handle_t bsh, u_int efir,
	u_int efdr)
{
	u_char cr00, cr01, cr04, cr09, cr0d, cr14, cr15, cr16, cr17;
	u_char cr18, cr19, cr24, cr25, cr28, cr2c, cr31, cr32;

	cr00 = rdspio(0x00);
	cr01 = rdspio(0x01);
	cr04 = rdspio(0x04);
	cr09 = rdspio(0x09);
	cr0d = rdspio(0x0d);
	cr14 = rdspio(0x14);
	cr15 = rdspio(0x15);
	cr16 = rdspio(0x16);
	cr17 = rdspio(0x17);
	cr18 = rdspio(0x18);
	cr19 = rdspio(0x19);
	cr24 = rdspio(0x24);
	cr25 = rdspio(0x25);
	cr28 = rdspio(0x28);
	cr2c = rdspio(0x2c);
	cr31 = rdspio(0x31);
	cr32 = rdspio(0x32);
	printf("877T: cr00 %x, cr01 %x, cr04 %x, cr09 %x, cr0d %x, cr14 %x, "
	    "cr15 %x, cr16 %x, cr17 %x, cr18 %x, cr19 %x, cr24 %x, cr25 %x, "
	    "cr28 %x, cr2c %x, cr31 %x, cr32 %x\n", cr00, cr01, cr04, cr09,
	    cr0d, cr14, cr15, cr16, cr17,
	    cr18, cr19, cr24, cr25, cr28, cr2c, cr31, cr32);
}
#endif

static void
puc_config_win877(struct resource *res)
{
	u_char val;
	u_int efir, efdr;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;

	bst = rman_get_bustag(res);
	bsh = rman_get_bushandle(res);

	/* configure the first W83877TF */
	bus_space_write_1(bst, bsh, 0x250, 0x89);
	efir = 0x251;
	efdr = 0x252;
	val = rdspio(0x09) & 0x0f;
	if (val != 0x0c) {
		printf("conf_win877: Oops not a W83877TF\n");
		return;
	}

#ifdef PUC_DEBUG
	printf("before: ");
	puc_print_win877(bst, bsh, efir, efdr);
#endif

	val = rdspio(0x16);
	val |= 0x04;
	wrspio(0x16, val);
	val &= ~0x04;
	wrspio(0x16, val);

	wrspio(0x24, 0x2e8 >> 2);
	wrspio(0x25, 0x2f8 >> 2);
	wrspio(0x17, 0x03);
	wrspio(0x28, 0x43);

#ifdef PUC_DEBUG
	printf("after: ");
	puc_print_win877(bst, bsh, efir, efdr);
#endif

	bus_space_write_1(bst, bsh, 0x250, 0xaa);

	/* configure the second W83877TF */
	bus_space_write_1(bst, bsh, 0x3f0, 0x87);
	bus_space_write_1(bst, bsh, 0x3f0, 0x87);
	efir = 0x3f0;
	efdr = 0x3f1;
	val = rdspio(0x09) & 0x0f;
	if (val != 0x0c) {
		printf("conf_win877: Oops not a W83877TF\n");
		return;
	}

#ifdef PUC_DEBUG
	printf("before: ");
	puc_print_win877(bst, bsh, efir, efdr);
#endif

	val = rdspio(0x16);
	val |= 0x04;
	wrspio(0x16, val);
	val &= ~0x04;
	wrspio(0x16, val);

	wrspio(0x24, 0x3e8 >> 2);
	wrspio(0x25, 0x3f8 >> 2);
	wrspio(0x17, 0x03);
	wrspio(0x28, 0x43);

#ifdef PUC_DEBUG
	printf("after: ");
	puc_print_win877(bst, bsh, efir, efdr);
#endif

	bus_space_write_1(bst, bsh, 0x3f0, 0xaa);
}

#undef rdspio
#undef wrspio

static int puc_find_free_unit(char *name)
{
	devclass_t dc;
	int start;
	int unit;

	unit = 0;
	start = 0;
	while (resource_int_value(name, unit, "port", &start) == 0 && 
	    start > 0)
		unit++;
	dc = devclass_find(name);
	if (dc == NULL)
		return (-1);
	while (devclass_get_device(dc, unit))
		unit++;
#ifdef PUC_DEBUG
	printf("puc: Using %s%d\n", name, unit);
#endif
	return (unit);
}

#ifdef PUC_DEBUG
static void
puc_print_resource_list(struct resource_list *rl)
{
	struct resource_list_entry *rle;

	printf("print_resource_list: rl %p\n", rl);
	SLIST_FOREACH(rle, rl, link)
		printf("type %x, rid %x\n", rle->type, rle->rid);
	printf("print_resource_list: end.\n");
}
#endif

static struct resource *
puc_alloc_resource(device_t dev, device_t child, int type, int *rid,
    u_long start, u_long end, u_long count, u_int flags)
{
	struct puc_device *pdev;
	struct resource *retval;
	struct resource_list *rl;
	struct resource_list_entry *rle;

	pdev = device_get_ivars(child);
	rl = &pdev->resources;

#ifdef PUC_DEBUG
	printf("puc_alloc_resource: pdev %p, looking for t %x, r %x\n",
	    pdev, type, *rid);
	puc_print_resource_list(rl);
#endif
	retval = NULL;
	rle = resource_list_find(rl, type, *rid);
	if (rle) {
		start = rle->start;
		end = rle->end;
		count = rle->count;
#ifdef PUC_DEBUG
		printf("found rle, %lx, %lx, %lx\n", start, end, count);
#endif
		retval = rle->res;
	} else
		printf("oops rle is gone\n");

	return (retval);
}

static int
puc_release_resource(device_t dev, device_t child, int type, int rid,
    struct resource *res)
{
	return (0);
}

static int
puc_get_resource(device_t dev, device_t child, int type, int rid,
    u_long *startp, u_long *countp)
{
	struct puc_device *pdev;
	struct resource_list *rl;
	struct resource_list_entry *rle;

	pdev = device_get_ivars(child);
	rl = &pdev->resources;

#ifdef PUC_DEBUG
	printf("puc_get_resource: pdev %p, looking for t %x, r %x\n", pdev,
	    type, rid);
	puc_print_resource_list(rl);
#endif
	rle = resource_list_find(rl, type, rid);
	if (rle) {
#ifdef PUC_DEBUG
		printf("found rle %p,", rle);
#endif
		if (startp != NULL)
			*startp = rle->start;
		if (countp != NULL)
			*countp = rle->count;
#ifdef PUC_DEBUG
		printf(" %lx, %lx\n", rle->start, rle->count);
#endif
		return (0);
	} else
		printf("oops rle is gone\n");
	return (ENXIO);
}

static int
puc_setup_intr(device_t dev, device_t child, struct resource *r, int flags,
	       void (*ihand)(void *), void *arg, void **cookiep)
{
	int i;
	struct puc_softc *sc;

	sc = (struct puc_softc *)device_get_softc(dev);
	for (i = 0; PUC_PORT_VALID(sc->sc_desc, i); i++) {
		if (sc->sc_ports[i].dev == child) {
			if (sc->sc_ports[i].ihand != 0)
				return (ENXIO);
			sc->sc_ports[i].ihand = ihand;
			sc->sc_ports[i].ihandarg = arg;
			*cookiep = arg;
			return (0);
		}
	}
	return (ENXIO);
}

static int
puc_teardown_intr(device_t dev, device_t child, struct resource *r,
		  void *cookie)
{
	int i;
	struct puc_softc *sc;

	sc = (struct puc_softc *)device_get_softc(dev);
	for (i = 0; PUC_PORT_VALID(sc->sc_desc, i); i++) {
		if (sc->sc_ports[i].dev == child) {
			sc->sc_ports[i].ihand = NULL;
			sc->sc_ports[i].ihandarg = NULL;
			return (0);
		}
	}
	return (ENXIO);
}

static int
puc_read_ivar(device_t dev, device_t child, int index, uintptr_t *result)
{
	struct puc_device *pdev;

	pdev = device_get_ivars(child);
	if (pdev == NULL)
		return (ENOENT);

	switch(index) {
	case PUC_IVAR_FREQ:
		*result = pdev->serialfreq;
		break;
	default:
		return (ENOENT);
	}
	return (0);
}

static device_method_t puc_pci_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,		puc_pci_probe),
    DEVMETHOD(device_attach,		puc_pci_attach),

    DEVMETHOD(bus_alloc_resource,	puc_alloc_resource),
    DEVMETHOD(bus_release_resource,	puc_release_resource),
    DEVMETHOD(bus_get_resource,		puc_get_resource),
    DEVMETHOD(bus_read_ivar,		puc_read_ivar),
    DEVMETHOD(bus_setup_intr,		puc_setup_intr),
    DEVMETHOD(bus_teardown_intr,	puc_teardown_intr),
    DEVMETHOD(bus_print_child,		bus_generic_print_child),
    DEVMETHOD(bus_driver_added,		bus_generic_driver_added),
    { 0, 0 }
};

static driver_t puc_pci_driver = {
	"puc",
	puc_pci_methods,
	sizeof(struct puc_softc),
};

static devclass_t puc_devclass;

DRIVER_MODULE(puc, pci, puc_pci_driver, puc_devclass, 0, 0);
