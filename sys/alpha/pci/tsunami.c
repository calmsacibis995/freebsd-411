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
 * $FreeBSD: src/sys/alpha/pci/tsunami.c,v 1.10 1999/12/14 17:35:08 gallatin Exp $
 */

#include "opt_cpu.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/malloc.h>

#include <pci/pcivar.h>
#include <alpha/isa/isavar.h>
#include <alpha/pci/tsunamireg.h>
#include <alpha/pci/tsunamivar.h>
#include <alpha/pci/pcibus.h>
#include <machine/bwx.h>
#include <machine/intr.h>
#include <machine/intrcnt.h>
#include <machine/cpuconf.h>
#include <machine/rpb.h>
#include <machine/resource.h>
#include <machine/sgmap.h>

#include <vm/vm.h>
#include <vm/vm_page.h>

#define KV(pa)			ALPHA_PHYS_TO_K0SEG(pa)

static devclass_t	tsunami_devclass;
static device_t		tsunami0;		/* XXX only one for now */

struct tsunami_softc {
	int		junk;		/* no softc */
};

int tsunami_num_pchips = 0;
static volatile tsunami_pchip *pchip[2] = {pchip0, pchip1};

#define TSUNAMI_SOFTC(dev)	(struct tsunami_softc*) device_get_softc(dev)

static alpha_chipset_inb_t	tsunami_inb;
static alpha_chipset_inw_t	tsunami_inw;
static alpha_chipset_inl_t	tsunami_inl;
static alpha_chipset_outb_t	tsunami_outb;
static alpha_chipset_outw_t	tsunami_outw;
static alpha_chipset_outl_t	tsunami_outl;
static alpha_chipset_readb_t	tsunami_readb;
static alpha_chipset_readw_t	tsunami_readw;
static alpha_chipset_readl_t	tsunami_readl;
static alpha_chipset_writeb_t	tsunami_writeb;
static alpha_chipset_writew_t	tsunami_writew;
static alpha_chipset_writel_t	tsunami_writel;
static alpha_chipset_maxdevs_t	tsunami_maxdevs;
static alpha_chipset_cfgreadb_t	tsunami_cfgreadb;
static alpha_chipset_cfgreadw_t	tsunami_cfgreadw;
static alpha_chipset_cfgreadl_t	tsunami_cfgreadl;
static alpha_chipset_cfgwriteb_t tsunami_cfgwriteb;
static alpha_chipset_cfgwritew_t tsunami_cfgwritew;
static alpha_chipset_cfgwritel_t tsunami_cfgwritel;
static alpha_chipset_addrcvt_t   tsunami_cvt_dense, tsunami_cvt_bwx;

static alpha_chipset_read_hae_t	tsunami_read_hae;
static alpha_chipset_write_hae_t tsunami_write_hae;

static alpha_chipset_t tsunami_chipset = {
	tsunami_inb,
	tsunami_inw,
	tsunami_inl,
	tsunami_outb,
	tsunami_outw,
	tsunami_outl,
	tsunami_readb,
	tsunami_readw,
	tsunami_readl,
	tsunami_writeb,
	tsunami_writew,
	tsunami_writel,
	tsunami_maxdevs,
	tsunami_cfgreadb,
	tsunami_cfgreadw,
	tsunami_cfgreadl,
	tsunami_cfgwriteb,
	tsunami_cfgwritew,
	tsunami_cfgwritel,
	tsunami_cvt_dense,
	tsunami_cvt_bwx,
	tsunami_read_hae,
	tsunami_write_hae,
};

/*
 * This setup will only allow for one additional hose
 */

#define ADDR_TO_HOSE(x)    ((x)  >> 31)
#define STRIP_HOSE(x)      ((x) & 0x7fffffff)

static void tsunami_intr_enable  __P((int));
static void tsunami_intr_disable __P((int));

static u_int8_t
tsunami_inb(u_int32_t port)
{
	int hose = ADDR_TO_HOSE(port);
	port = STRIP_HOSE(port);
	alpha_mb();
	return ldbu(KV(TSUNAMI_IO(hose) + port));
}

static u_int16_t
tsunami_inw(u_int32_t port)
{
	int hose = ADDR_TO_HOSE(port);
	port = STRIP_HOSE(port);
	alpha_mb();
	return ldwu(KV(TSUNAMI_IO(hose) + port));
}

static u_int32_t
tsunami_inl(u_int32_t port)
{
	int hose = ADDR_TO_HOSE(port);
	port = STRIP_HOSE(port);
	alpha_mb();
	return ldl(KV(TSUNAMI_IO(hose) + port));
}

static void
tsunami_outb(u_int32_t port, u_int8_t data)
{
	int hose = ADDR_TO_HOSE(port);
	port = STRIP_HOSE(port);
	stb(KV(TSUNAMI_IO(hose) + port), data);
	alpha_mb();
}

static void
tsunami_outw(u_int32_t port, u_int16_t data)
{
	int hose = ADDR_TO_HOSE(port);
	port = STRIP_HOSE(port);
	stw(KV(TSUNAMI_IO(hose) + port), data);
	alpha_mb();
}

static void
tsunami_outl(u_int32_t port, u_int32_t data)
{
	int hose = ADDR_TO_HOSE(port);
	port = STRIP_HOSE(port);
	stl(KV(TSUNAMI_IO(hose) + port), data);
	alpha_mb();
}

static u_int8_t
tsunami_readb(u_int32_t pa)
{
	int hose = ADDR_TO_HOSE(pa);
	pa = STRIP_HOSE(pa);
	alpha_mb();
	return ldbu(KV(TSUNAMI_MEM(hose) + pa));
}

static u_int16_t
tsunami_readw(u_int32_t pa)
{
	int hose = ADDR_TO_HOSE(pa);
	pa = STRIP_HOSE(pa);
	alpha_mb();
	return ldwu(KV(TSUNAMI_MEM(hose) + pa));
}

static u_int32_t
tsunami_readl(u_int32_t pa)
{
	int hose = ADDR_TO_HOSE(pa);
	pa = STRIP_HOSE(pa);
	alpha_mb();
	return ldl(KV(TSUNAMI_MEM(hose) + pa));
}

static void
tsunami_writeb(u_int32_t pa, u_int8_t data)
{
	int hose = ADDR_TO_HOSE(pa);
	pa = STRIP_HOSE(pa);
	stb(KV(TSUNAMI_MEM(hose) + pa), data);
	alpha_mb();
}

static void
tsunami_writew(u_int32_t pa, u_int16_t data)
{
	int hose = ADDR_TO_HOSE(pa);
	pa = STRIP_HOSE(pa);
	stw(KV(TSUNAMI_MEM(hose) + pa), data);
	alpha_mb();
}

static void
tsunami_writel(u_int32_t pa, u_int32_t data)
{
	int hose = ADDR_TO_HOSE(pa);
	pa = STRIP_HOSE(pa);
	stl(KV(TSUNAMI_MEM(hose) + pa), data);
	alpha_mb();
}

static int
tsunami_maxdevs(u_int b)
{
	return 12;		/* XXX */
}

static void
tsunami_clear_abort(void)
{
	alpha_mb();
	alpha_pal_draina();	
}

static int
tsunami_check_abort(void)
{
/*	u_int32_t errbits;*/
	int ba = 0;

	alpha_pal_draina();	
	alpha_mb();
#if 0
	errbits = REGVAL(TSUNAMI_CSR_TSUNAMI_ERR);
	if (errbits & (TSUNAMI_ERR_RCVD_MAS_ABT|TSUNAMI_ERR_RCVD_TAR_ABT))
		ba = 1;

	if (errbits) {
		REGVAL(TSUNAMI_CSR_TSUNAMI_ERR) = errbits;
		alpha_mb();
		alpha_pal_draina();
	}
#endif
	return ba;
}

#define TSUNAMI_CFGADDR(b, s, f, r, h)				\
	KV(TSUNAMI_CONF(h) | ((b) << 16) | ((s) << 11) | ((f) << 8) | (r))

#define CFGREAD(h, b, s, f, r, op, width, type)			\
	int bus;						\
        vm_offset_t va;						\
	type data;						\
	if (h == (u_int8_t)-1)					\
		h = tsunami_hose_from_bus(b);			\
	bus = tsunami_bus_within_hose(h, b);			\
	va = TSUNAMI_CFGADDR(bus, s, f, r, h);			\
	tsunami_clear_abort();					\
	if (badaddr((caddr_t)va, width)) {			\
		tsunami_check_abort();				\
		return ~0;					\
	}							\
	data = ##op##(va);					\
	if (tsunami_check_abort())				\
		return ~0;					\
	return data;			

#define CFWRITE(h, b, s, f, r, data, op, width)			\
	int bus;						\
        vm_offset_t va;						\
	if (h == (u_int8_t)-1)					\
		h = tsunami_hose_from_bus(b);			\
	bus = tsunami_bus_within_hose(h, b);			\
	va = TSUNAMI_CFGADDR(bus, s, f, r, h);			\
	tsunami_clear_abort();					\
	if (badaddr((caddr_t)va, width)) 			\
		return;						\
	##op##(va, data);					\
	tsunami_check_abort();




static u_int8_t
tsunami_cfgreadb(u_int h, u_int b, u_int s, u_int f, u_int r)
{
	CFGREAD(h, b, s, f, r, ldbu, 1, u_int8_t)
}

static u_int16_t
tsunami_cfgreadw(u_int h, u_int b, u_int s, u_int f, u_int r)
{
	CFGREAD(h, b, s, f, r, ldwu, 2, u_int16_t)
}

static u_int32_t
tsunami_cfgreadl(u_int h, u_int b, u_int s, u_int f, u_int r)
{
	CFGREAD(h, b, s, f, r, ldl, 4, u_int32_t)
}

static void
tsunami_cfgwriteb(u_int h, u_int b, u_int s, u_int f, u_int r, u_int8_t data)
{
	CFWRITE(h, b, s, f, r, data, stb, 1)	
}

static void
tsunami_cfgwritew(u_int h, u_int b, u_int s, u_int f, u_int r, u_int16_t data)
{	
	CFWRITE(h, b, s, f, r, data, stw, 2)
}

static void
tsunami_cfgwritel(u_int h, u_int b, u_int s, u_int f, u_int r, u_int32_t data)
{
	CFWRITE(h, b, s, f, r, data, stl, 4)
}


vm_offset_t
tsunami_cvt_bwx(vm_offset_t addr)
{
	int hose;
	vm_offset_t laddr;
	laddr = addr & 0xffffffffUL;
	hose = ADDR_TO_HOSE(laddr);
	laddr = STRIP_HOSE(addr);
        laddr |=  TSUNAMI_MEM(hose);
	return (KV(laddr));
}

vm_offset_t
tsunami_cvt_dense(vm_offset_t addr)
{
	return tsunami_cvt_bwx(addr);
}


/* 
 * There doesn't appear to be an hae on this platform
 */


static u_int64_t
tsunami_read_hae(void)
{
	return 0;  
}

static void
tsunami_write_hae(u_int64_t hae)
{
}

static int tsunami_probe(device_t dev);
static int tsunami_attach(device_t dev);
static int tsunami_setup_intr(device_t dev, device_t child, 
			      struct resource *irq, int flags,
			  driver_intr_t *intr, void *arg, void **cookiep);
static int tsunami_teardown_intr(device_t dev, device_t child,
			     struct resource *irq, void *cookie);

static device_method_t tsunami_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		tsunami_probe),
	DEVMETHOD(device_attach,	tsunami_attach),

	/* Bus interface */
	DEVMETHOD(bus_print_child,      bus_generic_print_child),
	DEVMETHOD(bus_alloc_resource,	pci_alloc_resource),
	DEVMETHOD(bus_release_resource,	pci_release_resource),
	DEVMETHOD(bus_activate_resource, pci_activate_resource),
	DEVMETHOD(bus_deactivate_resource, pci_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	tsunami_setup_intr),
	DEVMETHOD(bus_teardown_intr,	tsunami_teardown_intr),

	{ 0, 0 }
};

static driver_t tsunami_driver = {
	"tsunami",
	tsunami_methods,
	sizeof(struct tsunami_softc),
};

static void
pchip_init(volatile tsunami_pchip *pchip, int index)
{

	int i;
	
	/*
	 * initialize the direct map DMA windows.
	 *
	 * leave window 0 untouched; we'll set that up for S/G DMA for 
	 * isa devices later in the boot process
	 *
	 * window 1 goes at 2GB and has a length of 1 GB. It maps
	 * physical address 0 - 1GB. The SRM console typically sets
	 * this window up here.
	 */
	
        pchip->wsba[1].reg = (2UL*1024*1024*1024) | WINDOW_ENABLE;
        pchip->wsm[1].reg  = (1UL*1024*1024*1024 - 1) & 0xfff00000UL;
        pchip->tba[1].reg  = 0;
	
	/*
	 * window 2 goes at 3GB and has a length of 1 GB.  It maps
	 * physical address 1GB-2GB. 
	 */
	
        pchip->wsba[2].reg = (3UL*1024*1024*1024) | WINDOW_ENABLE;
        pchip->wsm[2].reg  = (1UL*1024*1024*1024 - 1) & 0xfff00000UL;
        pchip->tba[2].reg  = 1UL*1024*1024*1024;
	
	/*
	 * window 3 is disabled.  The SRM console typically leaves it
	 * disabled
	 */
	
        pchip->wsba[3].reg = 0;
        alpha_mb();
	
	if(bootverbose) {
		printf("pchip%d:\n", index);
		for (i = 0; i < 4; i++) {
			printf("\twsba[%d].reg = 0x%lx\n", 
			       i, pchip->wsba[i].reg);
			printf("\t wsm[%d].reg = 0x%lx\n", 
			       i, pchip->wsm[i].reg);
			printf("\t tba[%d].reg = 0x%lx\n", 
			       i, pchip->tba[i].reg);
		}
	}
}	

#define TSUNAMI_SGMAP_BASE		(8*1024*1024)
#define TSUNAMI_SGMAP_SIZE		(8*1024*1024)

static void
tsunami_sgmap_invalidate(void)
{
	alpha_mb();
	switch (tsunami_num_pchips) {
	case 2:
		pchip[1]->tlbia.reg = (u_int64_t)0;
	case 1:
		pchip[0]->tlbia.reg = (u_int64_t)0;
	}
	alpha_mb();
}

static void
tsunami_sgmap_map(void *arg, vm_offset_t ba, vm_offset_t pa)
{
	u_int64_t *sgtable = arg;
	int index = alpha_btop(ba - TSUNAMI_SGMAP_BASE);

	if (pa) {
		if (pa > (1L<<32))
			panic("tsunami_sgmap_map: can't map address 0x%lx", pa);
		sgtable[index] = ((pa >> 13) << 1) | 1;
	} else {
		sgtable[index] = 0;
	}
	alpha_mb();
	tsunami_sgmap_invalidate();
}


static void
tsunami_init_sgmap(void)
{
	void *sgtable;
	int i;

	sgtable = contigmalloc(8192, M_DEVBUF, M_NOWAIT,
			       0, (1L<<34),
			       32*1024, (1L<<34));
	if (!sgtable)
		panic("tsunami_init_sgmap: can't allocate page table");

	for(i=0; i < tsunami_num_pchips; i++){
		pchip[i]->tba[0].reg =
			pmap_kextract((vm_offset_t) sgtable);
		pchip[i]->wsba[0].reg |= WINDOW_ENABLE | WINDOW_SCATTER_GATHER;
	}

	chipset.sgmap = sgmap_map_create(TSUNAMI_SGMAP_BASE,
					 TSUNAMI_SGMAP_BASE + TSUNAMI_SGMAP_SIZE,
					 tsunami_sgmap_map, sgtable);
}

void
tsunami_init()
{
	static int initted = 0;

	if (initted) return;
	initted = 1;

	chipset = tsunami_chipset;
	platform.pci_intr_enable =  tsunami_intr_enable;
	platform.pci_intr_disable = tsunami_intr_disable;
	alpha_XXX_dmamap_or = 2UL * 1024UL * 1024UL * 1024UL;

	if (platform.pci_intr_init)
		platform.pci_intr_init();
}

static int
tsunami_probe(device_t dev)
{
	device_t child;
	int *hose;
	int i;
	if (tsunami0)
		return ENXIO;
	tsunami0 = dev;
	device_set_desc(dev, "21271 Core Logic chipset"); 
	if(cchip->csc.reg & CSC_P1P)
		tsunami_num_pchips = 2;
	else
		tsunami_num_pchips = 1;

	pci_init_resources();
	isa_init_intr();

	for(i = 0; i < tsunami_num_pchips; i++) {
		hose = malloc(sizeof(int), M_DEVBUF, M_NOWAIT);
		*hose = i;
		child = device_add_child(dev, "pcib", i);
		device_set_ivars(child, hose);
		pchip_init(pchip[i], i);		
	}

	return 0;
}

	

static int
tsunami_attach(device_t dev)
{
	tsunami_init();

	if (!platform.iointr)	/* XXX */
		set_iointr(alpha_dispatch_intr);

	snprintf(chipset_type, sizeof(chipset_type), "tsunami");
	chipset_bwx = 1;

	chipset_ports = TSUNAMI_IO(0);
	chipset_memory = TSUNAMI_MEM(0);
	chipset_dense = TSUNAMI_MEM(0);
	bus_generic_attach(dev);
	tsunami_init_sgmap();

	return 0;
}

static int
tsunami_setup_intr(device_t dev, device_t child,
	       struct resource *irq, int flags,
	       driver_intr_t *intr, void *arg, void **cookiep)
{
	int error;

	error = rman_activate_resource(irq);
	if (error)
		return error;

	error = alpha_setup_intr(0x900 + (irq->r_start << 4),
			intr, arg, cookiep,
			&intrcnt[INTRCNT_EB164_IRQ + irq->r_start]);
	if (error)
		return error;

	/* Enable PCI interrupt */
	platform.pci_intr_enable(irq->r_start);

	device_printf(child, "interrupting at TSUNAMI irq %d\n",
		      (int) irq->r_start);

	return 0;
}

static int
tsunami_teardown_intr(device_t dev, device_t child,
		  struct resource *irq, void *cookie)
{

	alpha_teardown_intr(cookie);
	return rman_deactivate_resource(irq);

}


/*
 * Currently, all interrupts will be funneled through CPU 0
 */

static void
tsunami_intr_enable(int irq)
{
	volatile u_int64_t *mask;
	u_int64_t saved_mask;

	mask = &cchip->dim0.reg;
	saved_mask = *mask;

	saved_mask |= (1UL << (unsigned long)irq);
	*mask = saved_mask;
	alpha_mb();
	alpha_mb();
	saved_mask = *mask;
	alpha_mb();
	alpha_mb();
}

static void
tsunami_intr_disable(int irq)
{
	volatile u_int64_t *mask;
	u_int64_t saved_mask;

	mask = &cchip->dim0.reg;
	saved_mask = *mask;

	saved_mask &= ~(1UL << (unsigned long)irq);
	*mask = saved_mask;
	alpha_mb();
	saved_mask = *mask;
	alpha_mb();
	alpha_mb();

}



DRIVER_MODULE(tsunami, root, tsunami_driver, tsunami_devclass, 0, 0);

