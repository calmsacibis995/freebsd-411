/*
 * Copyright (c) 2000 Berkeley Software Design, Inc.
 * Copyright (c) 1997, 1998, 1999, 2000
 *	Bill Paul <wpaul@osd.bsdi.com>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/pci/if_pcn.c,v 1.5.2.11 2004/09/09 13:22:18 brueffer Exp $
 */

/*
 * AMD Am79c972 fast ethernet PCI NIC driver. Datasheets are available
 * from http://www.amd.com.
 *
 * Written by Bill Paul <wpaul@osd.bsdi.com>
 */

/*
 * The AMD PCnet/PCI controllers are more advanced and functional
 * versions of the venerable 7990 LANCE. The PCnet/PCI chips retain
 * backwards compatibility with the LANCE and thus can be made
 * to work with older LANCE drivers. This is in fact how the
 * PCnet/PCI chips were supported in FreeBSD originally. The trouble
 * is that the PCnet/PCI devices offer several performance enhancements
 * which can't be exploited in LANCE compatibility mode. Chief among
 * these enhancements is the ability to perform PCI DMA operations
 * using 32-bit addressing (which eliminates the need for ISA
 * bounce-buffering), and special receive buffer alignment (which
 * allows the receive handler to pass packets to the upper protocol
 * layers without copying on both the x86 and alpha platforms).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <net/bpf.h>

#include <vm/vm.h>              /* for vtophys */
#include <vm/pmap.h>            /* for vtophys */
#include <machine/clock.h>      /* for DELAY */
#include <machine/bus_pio.h>
#include <machine/bus_memio.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <pci/pcireg.h>
#include <pci/pcivar.h>

#define PCN_USEIOSPACE

#include <pci/if_pcnreg.h>

/* "controller miibus0" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

#ifndef lint
static const char rcsid[] =
  "$FreeBSD: src/sys/pci/if_pcn.c,v 1.5.2.11 2004/09/09 13:22:18 brueffer Exp $";
#endif

/*
 * Various supported device vendors/types and their names.
 */
static struct pcn_type pcn_devs[] = {
	{ PCN_VENDORID, PCN_DEVICEID_PCNET, "AMD PCnet/PCI 10/100BaseTX" },
	{ PCN_VENDORID, PCN_DEVICEID_HOME, "AMD PCnet/Home HomePNA" },
	{ 0, 0, NULL }
};

static u_int32_t pcn_csr_read	__P((struct pcn_softc *, int));
static u_int16_t pcn_csr_read16	__P((struct pcn_softc *, int));
static u_int16_t pcn_bcr_read16	__P((struct pcn_softc *, int));
static void pcn_csr_write	__P((struct pcn_softc *, int, int));
static u_int32_t pcn_bcr_read	__P((struct pcn_softc *, int));
static void pcn_bcr_write	__P((struct pcn_softc *, int, int));

static int pcn_probe		__P((device_t));
static int pcn_attach		__P((device_t));
static int pcn_detach		__P((device_t));

static int pcn_newbuf		__P((struct pcn_softc *, int, struct mbuf *));
static int pcn_encap		__P((struct pcn_softc *,
					struct mbuf *, u_int32_t *));
static void pcn_rxeof		__P((struct pcn_softc *));
static void pcn_txeof		__P((struct pcn_softc *));
static void pcn_intr		__P((void *));
static void pcn_tick		__P((void *));
static void pcn_start		__P((struct ifnet *));
static int pcn_ioctl		__P((struct ifnet *, u_long, caddr_t));
static void pcn_init		__P((void *));
static void pcn_stop		__P((struct pcn_softc *));
static void pcn_watchdog		__P((struct ifnet *));
static void pcn_shutdown		__P((device_t));
static int pcn_ifmedia_upd	__P((struct ifnet *));
static void pcn_ifmedia_sts	__P((struct ifnet *, struct ifmediareq *));

static int pcn_miibus_readreg	__P((device_t, int, int));
static int pcn_miibus_writereg	__P((device_t, int, int, int));
static void pcn_miibus_statchg	__P((device_t));

static void pcn_setfilt		__P((struct ifnet *));
static void pcn_setmulti	__P((struct pcn_softc *));
static u_int32_t pcn_crc	__P((caddr_t));
static void pcn_reset		__P((struct pcn_softc *));
static int pcn_list_rx_init	__P((struct pcn_softc *));
static int pcn_list_tx_init	__P((struct pcn_softc *));

#ifdef PCN_USEIOSPACE
#define PCN_RES			SYS_RES_IOPORT
#define PCN_RID			PCN_PCI_LOIO
#else
#define PCN_RES			SYS_RES_MEMORY
#define PCN_RID			PCN_PCI_LOMEM
#endif

static device_method_t pcn_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		pcn_probe),
	DEVMETHOD(device_attach,	pcn_attach),
	DEVMETHOD(device_detach,	pcn_detach),
	DEVMETHOD(device_shutdown,	pcn_shutdown),

	/* bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	pcn_miibus_readreg),
	DEVMETHOD(miibus_writereg,	pcn_miibus_writereg),
	DEVMETHOD(miibus_statchg,	pcn_miibus_statchg),

	{ 0, 0 }
};

static driver_t pcn_driver = {
	"pcn",
	pcn_methods,
	sizeof(struct pcn_softc)
};

static devclass_t pcn_devclass;

DRIVER_MODULE(if_pcn, pci, pcn_driver, pcn_devclass, 0, 0);
DRIVER_MODULE(miibus, pcn, miibus_driver, miibus_devclass, 0, 0);

#define PCN_CSR_SETBIT(sc, reg, x)			\
	pcn_csr_write(sc, reg, pcn_csr_read(sc, reg) | (x))

#define PCN_CSR_CLRBIT(sc, reg, x)			\
	pcn_csr_write(sc, reg, pcn_csr_read(sc, reg) & ~(x))

#define PCN_BCR_SETBIT(sc, reg, x)			\
	pcn_bcr_write(sc, reg, pcn_bcr_read(sc, reg) | (x))

#define PCN_BCR_CLRBIT(sc, reg, x)			\
	pcn_bcr_write(sc, reg, pcn_bcr_read(sc, reg) & ~(x))

static u_int32_t pcn_csr_read(sc, reg)
	struct pcn_softc	*sc;
	int			reg;
{
	CSR_WRITE_4(sc, PCN_IO32_RAP, reg);
	return(CSR_READ_4(sc, PCN_IO32_RDP));
}

static u_int16_t pcn_csr_read16(sc, reg)
	struct pcn_softc	*sc;
	int			reg;
{
	CSR_WRITE_2(sc, PCN_IO16_RAP, reg);
	return(CSR_READ_2(sc, PCN_IO16_RDP));
}

static void pcn_csr_write(sc, reg, val)
	struct pcn_softc	*sc;
	int			reg;
{
	CSR_WRITE_4(sc, PCN_IO32_RAP, reg);
	CSR_WRITE_4(sc, PCN_IO32_RDP, val);
	return;
}

static u_int32_t pcn_bcr_read(sc, reg)
	struct pcn_softc	*sc;
	int			reg;
{
	CSR_WRITE_4(sc, PCN_IO32_RAP, reg);
	return(CSR_READ_4(sc, PCN_IO32_BDP));
}

static u_int16_t pcn_bcr_read16(sc, reg)
	struct pcn_softc	*sc;
	int			reg;
{
	CSR_WRITE_2(sc, PCN_IO16_RAP, reg);
	return(CSR_READ_2(sc, PCN_IO16_BDP));
}

static void pcn_bcr_write(sc, reg, val)
	struct pcn_softc	*sc;
	int			reg;
{
	CSR_WRITE_4(sc, PCN_IO32_RAP, reg);
	CSR_WRITE_4(sc, PCN_IO32_BDP, val);
	return;
}

static int pcn_miibus_readreg(dev, phy, reg)
	device_t		dev;
	int			phy, reg;
{
	struct pcn_softc	*sc;
	int			val;

	sc = device_get_softc(dev);

	if (sc->pcn_phyaddr && phy > sc->pcn_phyaddr)
		return(0);

	pcn_bcr_write(sc, PCN_BCR_MIIADDR, reg | (phy << 5));
	val = pcn_bcr_read(sc, PCN_BCR_MIIDATA) & 0xFFFF;
	if (val == 0xFFFF)
		return(0);

	sc->pcn_phyaddr = phy;

	return(val);
}

static int pcn_miibus_writereg(dev, phy, reg, data)
	device_t		dev;
	int			phy, reg, data;
{
	struct pcn_softc	*sc;

	sc = device_get_softc(dev);

	pcn_bcr_write(sc, PCN_BCR_MIIADDR, reg | (phy << 5));
	pcn_bcr_write(sc, PCN_BCR_MIIDATA, data);

	return(0);
}

static void pcn_miibus_statchg(dev)
	device_t		dev;
{
	struct pcn_softc	*sc;
	struct mii_data		*mii;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->pcn_miibus);

	if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX) {
		PCN_BCR_SETBIT(sc, PCN_BCR_DUPLEX, PCN_DUPLEX_FDEN);
	} else {
		PCN_BCR_CLRBIT(sc, PCN_BCR_DUPLEX, PCN_DUPLEX_FDEN);
	}

	return;
}

#define DC_POLY		0xEDB88320

static u_int32_t pcn_crc(addr)
	caddr_t			addr;
{
	u_int32_t		idx, bit, data, crc;

	/* Compute CRC for the address value. */
	crc = 0xFFFFFFFF; /* initial value */

	for (idx = 0; idx < 6; idx++) {
		for (data = *addr++, bit = 0; bit < 8; bit++, data >>= 1)
			crc = (crc >> 1) ^ (((crc ^ data) & 1) ? DC_POLY : 0);
	}

	return ((crc >> 26) & 0x3F);
}

static void pcn_setmulti(sc)
	struct pcn_softc	*sc;
{
	struct ifnet		*ifp;
	struct ifmultiaddr	*ifma;
	u_int32_t		h, i;
	u_int16_t		hashes[4] = { 0, 0, 0, 0 };

	ifp = &sc->arpcom.ac_if;

	PCN_CSR_SETBIT(sc, PCN_CSR_EXTCTL1, PCN_EXTCTL1_SPND);

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		for (i = 0; i < 4; i++)
			pcn_csr_write(sc, PCN_CSR_MAR0 + i, 0xFFFF);
		PCN_CSR_CLRBIT(sc, PCN_CSR_EXTCTL1, PCN_EXTCTL1_SPND);
		return;
	}

	/* first, zot all the existing hash bits */
	for (i = 0; i < 4; i++)
		pcn_csr_write(sc, PCN_CSR_MAR0 + i, 0);

	/* now program new ones */
	for (ifma = ifp->if_multiaddrs.lh_first; ifma != NULL;
	    ifma = ifma->ifma_link.le_next) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = pcn_crc(LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
		hashes[h >> 4] |= 1 << (h & 0xF);
	}

	for (i = 0; i < 4; i++)
		pcn_csr_write(sc, PCN_CSR_MAR0 + i, hashes[i]);

	PCN_CSR_CLRBIT(sc, PCN_CSR_EXTCTL1, PCN_EXTCTL1_SPND);

	return;
}

static void pcn_reset(sc)
	struct pcn_softc	*sc;
{
	/*
	 * Issue a reset by reading from the RESET register.
	 * Note that we don't know if the chip is operating in
	 * 16-bit or 32-bit mode at this point, so we attempt
	 * to reset the chip both ways. If one fails, the other
	 * will succeed.
	 */
	CSR_READ_2(sc, PCN_IO16_RESET);
	CSR_READ_4(sc, PCN_IO32_RESET);

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);

	/* Select 32-bit (DWIO) mode */
	CSR_WRITE_4(sc, PCN_IO32_RDP, 0);

	/* Select software style 3. */
	pcn_bcr_write(sc, PCN_BCR_SSTYLE, PCN_SWSTYLE_PCNETPCI_BURST);

        return;
}

/*
 * Probe for an AMD chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
static int pcn_probe(dev)
	device_t		dev;
{
	struct pcn_type		*t;
	struct pcn_softc	*sc;
	int			rid;
	u_int32_t		chip_id;

	t = pcn_devs;
	sc = device_get_softc(dev);

	while(t->pcn_name != NULL) {
		if ((pci_get_vendor(dev) == t->pcn_vid) &&
		    (pci_get_device(dev) == t->pcn_did)) {
			/*
			 * Temporarily map the I/O space
			 * so we can read the chip ID register.
			 */
			rid = PCN_RID;
			sc->pcn_res = bus_alloc_resource(dev, PCN_RES, &rid,
			    0, ~0, 1, RF_ACTIVE);
			if (sc->pcn_res == NULL) {
				device_printf(dev,
				    "couldn't map ports/memory\n");
				return(ENXIO);
			}
			sc->pcn_btag = rman_get_bustag(sc->pcn_res);
			sc->pcn_bhandle = rman_get_bushandle(sc->pcn_res);
			/*
			 * Note: we can *NOT* put the chip into
			 * 32-bit mode yet. The lnc driver will only
			 * work in 16-bit mode, and once the chip
			 * goes into 32-bit mode, the only way to
			 * get it out again is with a hardware reset.
			 * So if pcn_probe() is called before the
			 * lnc driver's probe routine, the chip will
			 * be locked into 32-bit operation and the lnc
			 * driver will be unable to attach to it.
			 * Note II: if the chip happens to already
			 * be in 32-bit mode, we still need to check
			 * the chip ID, but first we have to detect
			 * 32-bit mode using only 16-bit operations.
			 * The safest way to do this is to read the
			 * PCI subsystem ID from BCR23/24 and compare
			 * that with the value read from PCI config
			 * space.   
			 */
			chip_id = pcn_bcr_read16(sc, PCN_BCR_PCISUBSYSID);
			chip_id <<= 16;
			chip_id |= pcn_bcr_read16(sc, PCN_BCR_PCISUBVENID);
			/*
			 * Note III: the test for 0x10001000 is a hack to
			 * pacify VMware, who's pseudo-PCnet interface is
			 * broken. Reading the subsystem register from PCI
			 * config space yields 0x00000000 while reading the
			 * same value from I/O space yields 0x10001000. It's
			 * not supposed to be that way.
			 */
			if (chip_id == pci_read_config(dev,
			    PCIR_SUBVEND_0, 4) || chip_id == 0x10001000) {
				/* We're in 16-bit mode. */
				chip_id = pcn_csr_read16(sc, PCN_CSR_CHIPID1);
				chip_id <<= 16;
				chip_id |= pcn_csr_read16(sc, PCN_CSR_CHIPID0);
			} else {
				/* We're in 32-bit mode. */
				chip_id = pcn_csr_read(sc, PCN_CSR_CHIPID1);
				chip_id <<= 16;
				chip_id |= pcn_csr_read(sc, PCN_CSR_CHIPID0);
			}
			bus_release_resource(dev, PCN_RES,
			    PCN_RID, sc->pcn_res);
			chip_id >>= 12;
			sc->pcn_type = chip_id & PART_MASK;
			switch(sc->pcn_type) {
			case Am79C971:
			case Am79C972:
			case Am79C973:
			case Am79C975:
			case Am79C976:
			case Am79C978:
				break;
			default:
				return(ENXIO);
				break;
			}
			device_set_desc(dev, t->pcn_name);
			return(0);
		}
		t++;
	}

	return(ENXIO);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int pcn_attach(dev)
	device_t		dev;
{
	int			s;
	u_int32_t		eaddr[2];
	u_int32_t		command;
	struct pcn_softc	*sc;
	struct ifnet		*ifp;
	int			unit, error = 0, rid;

	s = splimp();

	sc = device_get_softc(dev);
	unit = device_get_unit(dev);

	/*
	 * Handle power management nonsense.
	 */

	command = pci_read_config(dev, PCN_PCI_CAPID, 4) & 0x000000FF;
	if (command == 0x01) {

		command = pci_read_config(dev, PCN_PCI_PWRMGMTCTRL, 4);
		if (command & PCN_PSTATE_MASK) {
			u_int32_t		iobase, membase, irq;

			/* Save important PCI config data. */
			iobase = pci_read_config(dev, PCN_PCI_LOIO, 4);
			membase = pci_read_config(dev, PCN_PCI_LOMEM, 4);
			irq = pci_read_config(dev, PCN_PCI_INTLINE, 4);

			/* Reset the power state. */
			printf("pcn%d: chip is in D%d power mode "
			"-- setting to D0\n", unit, command & PCN_PSTATE_MASK);
			command &= 0xFFFFFFFC;
			pci_write_config(dev, PCN_PCI_PWRMGMTCTRL, command, 4);

			/* Restore PCI config data. */
			pci_write_config(dev, PCN_PCI_LOIO, iobase, 4);
			pci_write_config(dev, PCN_PCI_LOMEM, membase, 4);
			pci_write_config(dev, PCN_PCI_INTLINE, irq, 4);
		}
	}

	/*
	 * Map control/status registers.
	 */
	command = pci_read_config(dev, PCIR_COMMAND, 4);
	command |= (PCIM_CMD_PORTEN|PCIM_CMD_MEMEN|PCIM_CMD_BUSMASTEREN);
	pci_write_config(dev, PCIR_COMMAND, command, 4);
	command = pci_read_config(dev, PCIR_COMMAND, 4);

#ifdef PCN_USEIOSPACE
	if (!(command & PCIM_CMD_PORTEN)) {
		printf("pcn%d: failed to enable I/O ports!\n", unit);
		error = ENXIO;;
		goto fail;
	}
#else
	if (!(command & PCIM_CMD_MEMEN)) {
		printf("pcn%d: failed to enable memory mapping!\n", unit);
		error = ENXIO;;
		goto fail;
	}
#endif

	rid = PCN_RID;
	sc->pcn_res = bus_alloc_resource(dev, PCN_RES, &rid,
	    0, ~0, 1, RF_ACTIVE);

	if (sc->pcn_res == NULL) {
		printf("pcn%d: couldn't map ports/memory\n", unit);
		error = ENXIO;
		goto fail;
	}

	sc->pcn_btag = rman_get_bustag(sc->pcn_res);
	sc->pcn_bhandle = rman_get_bushandle(sc->pcn_res);

	/* Allocate interrupt */
	rid = 0;
	sc->pcn_irq = bus_alloc_resource(dev, SYS_RES_IRQ, &rid, 0, ~0, 1,
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->pcn_irq == NULL) {
		printf("pcn%d: couldn't map interrupt\n", unit);
		bus_release_resource(dev, PCN_RES, PCN_RID, sc->pcn_res);
		error = ENXIO;
		goto fail;
	}

	error = bus_setup_intr(dev, sc->pcn_irq, INTR_TYPE_NET,
	    pcn_intr, sc, &sc->pcn_intrhand);

	if (error) {
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->pcn_res);
		bus_release_resource(dev, PCN_RES, PCN_RID, sc->pcn_res);
		printf("pcn%d: couldn't set up irq\n", unit);
		goto fail;
	}

	/* Reset the adapter. */
	pcn_reset(sc);

	/*
	 * Get station address from the EEPROM.
	 */
	eaddr[0] = CSR_READ_4(sc, PCN_IO32_APROM00);
	eaddr[1] = CSR_READ_4(sc, PCN_IO32_APROM01);
	bcopy(eaddr, (char *)&sc->arpcom.ac_enaddr, ETHER_ADDR_LEN);

	/*
	 * An AMD chip was detected. Inform the world.
	 */
	printf("pcn%d: Ethernet address: %6D\n", unit,
	    sc->arpcom.ac_enaddr, ":");

	sc->pcn_unit = unit;
	callout_handle_init(&sc->pcn_stat_ch);

	sc->pcn_ldata = contigmalloc(sizeof(struct pcn_list_data), M_DEVBUF,
	    M_NOWAIT, 0, 0xffffffff, PAGE_SIZE, 0);

	if (sc->pcn_ldata == NULL) {
		printf("pcn%d: no memory for list buffers!\n", unit);
		bus_teardown_intr(dev, sc->pcn_irq, sc->pcn_intrhand);
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->pcn_irq);
		bus_release_resource(dev, PCN_RES, PCN_RID, sc->pcn_res);
		error = ENXIO;
		goto fail;
	}
	bzero(sc->pcn_ldata, sizeof(struct pcn_list_data));

	ifp = &sc->arpcom.ac_if;
	ifp->if_softc = sc;
	ifp->if_unit = unit;
	ifp->if_name = "pcn";
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = pcn_ioctl;
	ifp->if_output = ether_output;
	ifp->if_start = pcn_start;
	ifp->if_watchdog = pcn_watchdog;
	ifp->if_init = pcn_init;
	ifp->if_baudrate = 10000000;
	ifp->if_snd.ifq_maxlen = PCN_TX_LIST_CNT - 1;

	/*
	 * Do MII setup.
	 */
	if (mii_phy_probe(dev, &sc->pcn_miibus,
	    pcn_ifmedia_upd, pcn_ifmedia_sts)) {
		printf("pcn%d: MII without any PHY!\n", sc->pcn_unit);
		contigfree(sc->pcn_ldata, sizeof(struct pcn_list_data),
		    M_DEVBUF);
		bus_teardown_intr(dev, sc->pcn_irq, sc->pcn_intrhand);
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->pcn_irq);
		bus_release_resource(dev, PCN_RES, PCN_RID, sc->pcn_res);
		error = ENXIO;
		goto fail;
	}

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, ETHER_BPF_SUPPORTED);
	callout_handle_init(&sc->pcn_stat_ch);

fail:
	splx(s);
	return(error);
}

static int pcn_detach(dev)
	device_t		dev;
{
	struct pcn_softc	*sc;
	struct ifnet		*ifp;
	int			s;

	s = splimp();

	sc = device_get_softc(dev);
	ifp = &sc->arpcom.ac_if;

	pcn_reset(sc);
	pcn_stop(sc);
	ether_ifdetach(ifp, ETHER_BPF_SUPPORTED);

	if (sc->pcn_miibus != NULL) {
		bus_generic_detach(dev);
		device_delete_child(dev, sc->pcn_miibus);
	}

	bus_teardown_intr(dev, sc->pcn_irq, sc->pcn_intrhand);
	bus_release_resource(dev, SYS_RES_IRQ, 0, sc->pcn_irq);
	bus_release_resource(dev, PCN_RES, PCN_RID, sc->pcn_res);

	contigfree(sc->pcn_ldata, sizeof(struct pcn_list_data), M_DEVBUF);

	splx(s);

	return(0);
}

/*
 * Initialize the transmit descriptors.
 */
static int pcn_list_tx_init(sc)
	struct pcn_softc	*sc;
{
	struct pcn_list_data	*ld;
	struct pcn_ring_data	*cd;
	int			i;

	cd = &sc->pcn_cdata;
	ld = sc->pcn_ldata;

	for (i = 0; i < PCN_TX_LIST_CNT; i++) {
		cd->pcn_tx_chain[i] = NULL;
		ld->pcn_tx_list[i].pcn_tbaddr = 0;
		ld->pcn_tx_list[i].pcn_txctl = 0;
		ld->pcn_tx_list[i].pcn_txstat = 0;
	}

	cd->pcn_tx_prod = cd->pcn_tx_cons = cd->pcn_tx_cnt = 0;

	return(0);
}


/*
 * Initialize the RX descriptors and allocate mbufs for them.
 */
static int pcn_list_rx_init(sc)
	struct pcn_softc	*sc;
{
	struct pcn_list_data	*ld;
	struct pcn_ring_data	*cd;
	int			i;

	ld = sc->pcn_ldata;
	cd = &sc->pcn_cdata;

	for (i = 0; i < PCN_RX_LIST_CNT; i++) {
		if (pcn_newbuf(sc, i, NULL) == ENOBUFS)
			return(ENOBUFS);
	}

	cd->pcn_rx_prod = 0;

	return(0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
static int pcn_newbuf(sc, idx, m)
	struct pcn_softc	*sc;
	int			idx;
	struct mbuf		*m;
{
	struct mbuf		*m_new = NULL;
	struct pcn_rx_desc	*c;

	c = &sc->pcn_ldata->pcn_rx_list[idx];

	if (m == NULL) {
		MGETHDR(m_new, M_DONTWAIT, MT_DATA);
		if (m_new == NULL)
			return(ENOBUFS);

		MCLGET(m_new, M_DONTWAIT);
		if (!(m_new->m_flags & M_EXT)) {
			m_freem(m_new);
			return(ENOBUFS);
		}
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
	} else {
		m_new = m;
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
		m_new->m_data = m_new->m_ext.ext_buf;
	}

	m_adj(m_new, ETHER_ALIGN);

	sc->pcn_cdata.pcn_rx_chain[idx] = m_new;
	c->pcn_rbaddr = vtophys(mtod(m_new, caddr_t));
	c->pcn_bufsz = (~(PCN_RXLEN) + 1) & PCN_RXLEN_BUFSZ;
	c->pcn_bufsz |= PCN_RXLEN_MBO;
	c->pcn_rxstat = PCN_RXSTAT_STP|PCN_RXSTAT_ENP|PCN_RXSTAT_OWN;

	return(0);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
static void pcn_rxeof(sc)
	struct pcn_softc	*sc;
{
        struct ether_header	*eh;
        struct mbuf		*m;
        struct ifnet		*ifp;
	struct pcn_rx_desc	*cur_rx;
	int			i;

	ifp = &sc->arpcom.ac_if;
	i = sc->pcn_cdata.pcn_rx_prod;

	while(PCN_OWN_RXDESC(&sc->pcn_ldata->pcn_rx_list[i])) {
		cur_rx = &sc->pcn_ldata->pcn_rx_list[i];
		m = sc->pcn_cdata.pcn_rx_chain[i];
		sc->pcn_cdata.pcn_rx_chain[i] = NULL;

		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
	 	 * comes up in the ring.
		 */
		if (cur_rx->pcn_rxstat & PCN_RXSTAT_ERR) {
			ifp->if_ierrors++;
			pcn_newbuf(sc, i, m);
			PCN_INC(i, PCN_RX_LIST_CNT);
			continue;
		}

		if (pcn_newbuf(sc, i, NULL)) {
			/* Ran out of mbufs; recycle this one. */
			pcn_newbuf(sc, i, m);
			ifp->if_ierrors++;
			PCN_INC(i, PCN_RX_LIST_CNT);
			continue;
		}

		PCN_INC(i, PCN_RX_LIST_CNT);

		/* No errors; receive the packet. */
		ifp->if_ipackets++;
		eh = mtod(m, struct ether_header *);
		m->m_len = m->m_pkthdr.len =
		    cur_rx->pcn_rxlen - ETHER_CRC_LEN;
		m->m_pkthdr.rcvif = ifp;

		/* Remove header from mbuf and pass it on. */
		m_adj(m, sizeof(struct ether_header));
		ether_input(ifp, eh, m);
	}

	sc->pcn_cdata.pcn_rx_prod = i;

	return;
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */

static void pcn_txeof(sc)
	struct pcn_softc	*sc;
{
	struct pcn_tx_desc	*cur_tx = NULL;
	struct ifnet		*ifp;
	u_int32_t		idx;

	ifp = &sc->arpcom.ac_if;

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	idx = sc->pcn_cdata.pcn_tx_cons;
	while (idx != sc->pcn_cdata.pcn_tx_prod) {
		cur_tx = &sc->pcn_ldata->pcn_tx_list[idx];

		if (!PCN_OWN_TXDESC(cur_tx))
			break;

		if (!(cur_tx->pcn_txctl & PCN_TXCTL_ENP)) {
			sc->pcn_cdata.pcn_tx_cnt--;
			PCN_INC(idx, PCN_TX_LIST_CNT);
			continue;
		}

		if (cur_tx->pcn_txctl & PCN_TXCTL_ERR) {
			ifp->if_oerrors++;
			if (cur_tx->pcn_txstat & PCN_TXSTAT_EXDEF)
				ifp->if_collisions++;
			if (cur_tx->pcn_txstat & PCN_TXSTAT_RTRY)
				ifp->if_collisions++;
		}

		ifp->if_collisions +=
		    cur_tx->pcn_txstat & PCN_TXSTAT_TRC;

		ifp->if_opackets++;
		if (sc->pcn_cdata.pcn_tx_chain[idx] != NULL) {
			m_freem(sc->pcn_cdata.pcn_tx_chain[idx]);
			sc->pcn_cdata.pcn_tx_chain[idx] = NULL;
		}

		sc->pcn_cdata.pcn_tx_cnt--;
		PCN_INC(idx, PCN_TX_LIST_CNT);
	}

	if (idx != sc->pcn_cdata.pcn_tx_cons) {
		/* Some buffers have been freed. */
		sc->pcn_cdata.pcn_tx_cons = idx;
		ifp->if_flags &= ~IFF_OACTIVE;
	}
	ifp->if_timer = (sc->pcn_cdata.pcn_tx_cnt == 0) ? 0 : 5;

	return;
}

static void pcn_tick(xsc)
	void			*xsc;
{
	struct pcn_softc	*sc;
	struct mii_data		*mii;
	struct ifnet		*ifp;
	int			s;

	s = splimp();

	sc = xsc;
	ifp = &sc->arpcom.ac_if;

	mii = device_get_softc(sc->pcn_miibus);
	mii_tick(mii);

	if (sc->pcn_link & !(mii->mii_media_status & IFM_ACTIVE))
		sc->pcn_link = 0;

	if (!sc->pcn_link) {
		mii_pollstat(mii);
		if (mii->mii_media_status & IFM_ACTIVE &&
		    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE)
			sc->pcn_link++;
			if (ifp->if_snd.ifq_head != NULL)
				pcn_start(ifp);
	}

	sc->pcn_stat_ch = timeout(pcn_tick, sc, hz);

	splx(s);

	return;
}

static void pcn_intr(arg)
	void			*arg;
{
	struct pcn_softc	*sc;
	struct ifnet		*ifp;
	u_int32_t		status;

	sc = arg;
	ifp = &sc->arpcom.ac_if;

	/* Suppress unwanted interrupts */
	if (!(ifp->if_flags & IFF_UP)) {
		pcn_stop(sc);
		return;
	}

	CSR_WRITE_4(sc, PCN_IO32_RAP, PCN_CSR_CSR);

	while ((status = CSR_READ_4(sc, PCN_IO32_RDP)) & PCN_CSR_INTR) {
		CSR_WRITE_4(sc, PCN_IO32_RDP, status);

		if (status & PCN_CSR_RINT)
			pcn_rxeof(sc);

		if (status & PCN_CSR_TINT)
			pcn_txeof(sc);

		if (status & PCN_CSR_ERR) {
			pcn_init(sc);
			break;
		}
	}

	if (ifp->if_snd.ifq_head != NULL)
		pcn_start(ifp);

	return;
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
static int pcn_encap(sc, m_head, txidx)
	struct pcn_softc	*sc;
	struct mbuf		*m_head;
	u_int32_t		*txidx;
{
	struct pcn_tx_desc	*f = NULL;
	struct mbuf		*m;
	int			frag, cur, cnt = 0;

	/*
 	 * Start packing the mbufs in this chain into
	 * the fragment pointers. Stop when we run out
 	 * of fragments or hit the end of the mbuf chain.
	 */
	m = m_head;
	cur = frag = *txidx;

	for (m = m_head; m != NULL; m = m->m_next) {
		if (m->m_len != 0) {
			if ((PCN_TX_LIST_CNT -
			    (sc->pcn_cdata.pcn_tx_cnt + cnt)) < 2)
				return(ENOBUFS);
			f = &sc->pcn_ldata->pcn_tx_list[frag];
			f->pcn_txctl = (~(m->m_len) + 1) & PCN_TXCTL_BUFSZ;
			f->pcn_txctl |= PCN_TXCTL_MBO;
			f->pcn_tbaddr = vtophys(mtod(m, vm_offset_t));
			if (cnt == 0)
				f->pcn_txctl |= PCN_TXCTL_STP;
			else
				f->pcn_txctl |= PCN_TXCTL_OWN;
			cur = frag;
			PCN_INC(frag, PCN_TX_LIST_CNT);
			cnt++;
		}
	}

	if (m != NULL)
		return(ENOBUFS);

	sc->pcn_cdata.pcn_tx_chain[cur] = m_head;
	sc->pcn_ldata->pcn_tx_list[cur].pcn_txctl |=
	    PCN_TXCTL_ENP|PCN_TXCTL_ADD_FCS|PCN_TXCTL_MORE_LTINT;
	sc->pcn_ldata->pcn_tx_list[*txidx].pcn_txctl |= PCN_TXCTL_OWN;
	sc->pcn_cdata.pcn_tx_cnt += cnt;
	*txidx = frag;

	return(0);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */
static void pcn_start(ifp)
	struct ifnet		*ifp;
{
	struct pcn_softc	*sc;
	struct mbuf		*m_head = NULL;
	u_int32_t		idx;

	sc = ifp->if_softc;

	if (!sc->pcn_link)
		return;

	idx = sc->pcn_cdata.pcn_tx_prod;

	if (ifp->if_flags & IFF_OACTIVE)
		return;

	while(sc->pcn_cdata.pcn_tx_chain[idx] == NULL) {
		IF_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		if (pcn_encap(sc, m_head, &idx)) {
			IF_PREPEND(&ifp->if_snd, m_head);
			ifp->if_flags |= IFF_OACTIVE;
			break;
		}

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		if (ifp->if_bpf)
			bpf_mtap(ifp, m_head);

	}

	/* Transmit */
	sc->pcn_cdata.pcn_tx_prod = idx;
	pcn_csr_write(sc, PCN_CSR_CSR, PCN_CSR_TX|PCN_CSR_INTEN);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;

	return;
}

void pcn_setfilt(ifp)
	struct ifnet		*ifp;
{
	struct pcn_softc	*sc;

	sc = ifp->if_softc;

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC) {
		PCN_CSR_SETBIT(sc, PCN_CSR_MODE, PCN_MODE_PROMISC);
	} else {
		PCN_CSR_CLRBIT(sc, PCN_CSR_MODE, PCN_MODE_PROMISC);
	}

	/* Set the capture broadcast bit to capture broadcast frames. */
	if (ifp->if_flags & IFF_BROADCAST) {
		PCN_CSR_CLRBIT(sc, PCN_CSR_MODE, PCN_MODE_RXNOBROAD);
	} else {
		PCN_CSR_SETBIT(sc, PCN_CSR_MODE, PCN_MODE_RXNOBROAD);
	}

	return;
}

static void pcn_init(xsc)
	void			*xsc;
{
	struct pcn_softc	*sc = xsc;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct mii_data		*mii = NULL;
	int			s;

	s = splimp();

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	pcn_stop(sc);
	pcn_reset(sc);

	mii = device_get_softc(sc->pcn_miibus);

	/* Set MAC address */
	pcn_csr_write(sc, PCN_CSR_PAR0,
	    ((u_int16_t *)sc->arpcom.ac_enaddr)[0]);
	pcn_csr_write(sc, PCN_CSR_PAR1,
	    ((u_int16_t *)sc->arpcom.ac_enaddr)[1]);
	pcn_csr_write(sc, PCN_CSR_PAR2,
	    ((u_int16_t *)sc->arpcom.ac_enaddr)[2]);

	/* Init circular RX list. */
	if (pcn_list_rx_init(sc) == ENOBUFS) {
		printf("pcn%d: initialization failed: no "
		    "memory for rx buffers\n", sc->pcn_unit);
		pcn_stop(sc);
		(void)splx(s);
		return;
	}

	/* Set up RX filter. */
	pcn_setfilt(ifp);

	/*
	 * Init tx descriptors.
	 */
	pcn_list_tx_init(sc);

	/* Set up the mode register. */
	pcn_csr_write(sc, PCN_CSR_MODE, PCN_PORT_MII);

	/*
	 * Load the multicast filter.
	 */
	pcn_setmulti(sc);

	/*
	 * Load the addresses of the RX and TX lists.
	 */
	pcn_csr_write(sc, PCN_CSR_RXADDR0,
	    vtophys(&sc->pcn_ldata->pcn_rx_list[0]) & 0xFFFF);
	pcn_csr_write(sc, PCN_CSR_RXADDR1,
	    (vtophys(&sc->pcn_ldata->pcn_rx_list[0]) >> 16) & 0xFFFF);
	pcn_csr_write(sc, PCN_CSR_TXADDR0,
	    vtophys(&sc->pcn_ldata->pcn_tx_list[0]) & 0xFFFF);
	pcn_csr_write(sc, PCN_CSR_TXADDR1,
	    (vtophys(&sc->pcn_ldata->pcn_tx_list[0]) >> 16) & 0xFFFF);

	/* Set the RX and TX ring sizes. */
	pcn_csr_write(sc, PCN_CSR_RXRINGLEN, (~PCN_RX_LIST_CNT) + 1);
	pcn_csr_write(sc, PCN_CSR_TXRINGLEN, (~PCN_TX_LIST_CNT) + 1);

	/* We're not using the initialization block. */
	pcn_csr_write(sc, PCN_CSR_IAB1, 0);

	/* Enable fast suspend mode. */
	PCN_CSR_SETBIT(sc, PCN_CSR_EXTCTL2, PCN_EXTCTL2_FASTSPNDE);

	/*
	 * Enable burst read and write. Also set the no underflow
	 * bit. This will avoid transmit underruns in certain
	 * conditions while still providing decent performance.
	 */
	PCN_BCR_SETBIT(sc, PCN_BCR_BUSCTL, PCN_BUSCTL_NOUFLOW|
	    PCN_BUSCTL_BREAD|PCN_BUSCTL_BWRITE);

	/* Enable graceful recovery from underflow. */
	PCN_CSR_SETBIT(sc, PCN_CSR_IMR, PCN_IMR_DXSUFLO);

	/* Enable auto-padding of short TX frames. */
	PCN_CSR_SETBIT(sc, PCN_CSR_TFEAT, PCN_TFEAT_PAD_TX);

	/* Disable MII autoneg (we handle this ourselves). */
	PCN_BCR_SETBIT(sc, PCN_BCR_MIICTL, PCN_MIICTL_DANAS);

	if (sc->pcn_type == Am79C978)
		pcn_bcr_write(sc, PCN_BCR_PHYSEL,
		    PCN_PHYSEL_PCNET|PCN_PHY_HOMEPNA);

	/* Enable interrupts and start the controller running. */
	pcn_csr_write(sc, PCN_CSR_CSR, PCN_CSR_INTEN|PCN_CSR_START);

	mii_mediachg(mii);

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	(void)splx(s);
	sc->pcn_stat_ch = timeout(pcn_tick, sc, hz);

	return;
}

/*
 * Set media options.
 */
static int pcn_ifmedia_upd(ifp)
	struct ifnet		*ifp;
{
	struct pcn_softc	*sc;
	struct mii_data		*mii;

	sc = ifp->if_softc;
	mii = device_get_softc(sc->pcn_miibus);

	sc->pcn_link = 0;
	if (mii->mii_instance) {
		struct mii_softc        *miisc;
		for (miisc = LIST_FIRST(&mii->mii_phys); miisc != NULL;
		    miisc = LIST_NEXT(miisc, mii_list))
			mii_phy_reset(miisc);
	}
	mii_mediachg(mii);

	return(0);
}

/*
 * Report current media status.
 */
static void pcn_ifmedia_sts(ifp, ifmr)
	struct ifnet		*ifp;
	struct ifmediareq	*ifmr;
{
	struct pcn_softc	*sc;
	struct mii_data		*mii;

	sc = ifp->if_softc;

	mii = device_get_softc(sc->pcn_miibus);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;

	return;
}

static int pcn_ioctl(ifp, command, data)
	struct ifnet		*ifp;
	u_long			command;
	caddr_t			data;
{
	struct pcn_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
	struct mii_data		*mii = NULL;
	int			s, error = 0;

	s = splimp();

	switch(command) {
	case SIOCSIFADDR:
	case SIOCGIFADDR:
	case SIOCSIFMTU:
		error = ether_ioctl(ifp, command, data);
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
                        if (ifp->if_flags & IFF_RUNNING &&
			    ifp->if_flags & IFF_PROMISC &&
			    !(sc->pcn_if_flags & IFF_PROMISC)) {
				PCN_CSR_SETBIT(sc, PCN_CSR_EXTCTL1,
				    PCN_EXTCTL1_SPND);
				pcn_setfilt(ifp);
				PCN_CSR_CLRBIT(sc, PCN_CSR_EXTCTL1,
				    PCN_EXTCTL1_SPND);
				pcn_csr_write(sc, PCN_CSR_CSR,
				    PCN_CSR_INTEN|PCN_CSR_START);
			} else if (ifp->if_flags & IFF_RUNNING &&
			    !(ifp->if_flags & IFF_PROMISC) &&
				sc->pcn_if_flags & IFF_PROMISC) {
				PCN_CSR_SETBIT(sc, PCN_CSR_EXTCTL1,
				    PCN_EXTCTL1_SPND);
				pcn_setfilt(ifp);
				PCN_CSR_CLRBIT(sc, PCN_CSR_EXTCTL1,
				    PCN_EXTCTL1_SPND);
				pcn_csr_write(sc, PCN_CSR_CSR,
				    PCN_CSR_INTEN|PCN_CSR_START);
			} else if (!(ifp->if_flags & IFF_RUNNING))
				pcn_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				pcn_stop(sc);
		}
		sc->pcn_if_flags = ifp->if_flags;
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		pcn_setmulti(sc);
		error = 0;
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc->pcn_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	default:
		error = EINVAL;
		break;
	}

	(void)splx(s);

	return(error);
}

static void pcn_watchdog(ifp)
	struct ifnet		*ifp;
{
	struct pcn_softc	*sc;

	sc = ifp->if_softc;

	ifp->if_oerrors++;
	printf("pcn%d: watchdog timeout\n", sc->pcn_unit);

	pcn_stop(sc);
	pcn_reset(sc);
	pcn_init(sc);

	if (ifp->if_snd.ifq_head != NULL)
		pcn_start(ifp);

	return;
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void pcn_stop(sc)
	struct pcn_softc	*sc;
{
	register int		i;
	struct ifnet		*ifp;

	ifp = &sc->arpcom.ac_if;
	ifp->if_timer = 0;

	untimeout(pcn_tick, sc, sc->pcn_stat_ch);
	PCN_CSR_SETBIT(sc, PCN_CSR_CSR, PCN_CSR_STOP);
	sc->pcn_link = 0;

	/*
	 * Free data in the RX lists.
	 */
	for (i = 0; i < PCN_RX_LIST_CNT; i++) {
		if (sc->pcn_cdata.pcn_rx_chain[i] != NULL) {
			m_freem(sc->pcn_cdata.pcn_rx_chain[i]);
			sc->pcn_cdata.pcn_rx_chain[i] = NULL;
		}
	}
	bzero((char *)&sc->pcn_ldata->pcn_rx_list,
		sizeof(sc->pcn_ldata->pcn_rx_list));

	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < PCN_TX_LIST_CNT; i++) {
		if (sc->pcn_cdata.pcn_tx_chain[i] != NULL) {
			m_freem(sc->pcn_cdata.pcn_tx_chain[i]);
			sc->pcn_cdata.pcn_tx_chain[i] = NULL;
		}
	}

	bzero((char *)&sc->pcn_ldata->pcn_tx_list,
		sizeof(sc->pcn_ldata->pcn_tx_list));

	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);

	return;
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static void pcn_shutdown(dev)
	device_t		dev;
{
	struct pcn_softc	*sc;

	sc = device_get_softc(dev);

	pcn_reset(sc);
	pcn_stop(sc);

	return;
}
