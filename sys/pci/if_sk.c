/*	$OpenBSD: if_sk.c,v 1.33 2003/08/12 05:23:06 nate Exp $	*/

/*
 * Copyright (c) 1997, 1998, 1999, 2000
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 * $FreeBSD: src/sys/pci/if_sk.c,v 1.19.2.12.4.1 2005/01/07 04:52:22 dwhite Exp $
 */

/*
 * Copyright (c) 2003 Nathan L. Binkert <binkertn@umich.edu>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * SysKonnect SK-NET gigabit ethernet driver for FreeBSD. Supports
 * the SK-984x series adapters, both single port and dual port.
 * References:
 * 	The XaQti XMAC II datasheet,
 *  http://www.freebsd.org/~wpaul/SysKonnect/xmacii_datasheet_rev_c_9-29.pdf
 *	The SysKonnect GEnesis manual, http://www.syskonnect.com
 *
 * Note: XaQti has been aquired by Vitesse, and Vitesse does not have the
 * XMAC II datasheet online. I have put my copy at people.freebsd.org as a
 * convenience to others until Vitesse corrects this problem:
 *
 * http://people.freebsd.org/~wpaul/SysKonnect/xmacii_datasheet_rev_c_9-29.pdf
 *
 * Written by Bill Paul <wpaul@ee.columbia.edu>
 * Department of Electrical Engineering
 * Columbia University, New York City
 */

/*
 * The SysKonnect gigabit ethernet adapters consist of two main
 * components: the SysKonnect GEnesis controller chip and the XaQti Corp.
 * XMAC II gigabit ethernet MAC. The XMAC provides all of the MAC
 * components and a PHY while the GEnesis controller provides a PCI
 * interface with DMA support. Each card may have between 512K and
 * 2MB of SRAM on board depending on the configuration.
 *
 * The SysKonnect GEnesis controller can have either one or two XMAC
 * chips connected to it, allowing single or dual port NIC configurations.
 * SysKonnect has the distinction of being the only vendor on the market
 * with a dual port gigabit ethernet NIC. The GEnesis provides dual FIFOs,
 * dual DMA queues, packet/MAC/transmit arbiters and direct access to the
 * XMAC registers. This driver takes advantage of these features to allow
 * both XMACs to operate as independent interfaces.
 */
 
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/queue.h>

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
#include <dev/mii/brgphyreg.h>

#include <pci/pcireg.h>
#include <pci/pcivar.h>

#if 0
#define SK_USEIOSPACE
#endif

#include <pci/if_skreg.h>
#include <pci/xmaciireg.h>
#include <pci/yukonreg.h>

/* "controller miibus0" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

#ifndef lint
static const char rcsid[] =
  "$FreeBSD: src/sys/pci/if_sk.c,v 1.19.2.12.4.1 2005/01/07 04:52:22 dwhite Exp $";
#endif

static struct sk_type sk_devs[] = {
	{
		VENDORID_SK,
		DEVICEID_SK_V1,
		"SysKonnect Gigabit Ethernet (V1.0)"
	},
	{
		VENDORID_SK,
		DEVICEID_SK_V2,
		"SysKonnect Gigabit Ethernet (V2.0)"
	},
	{
		VENDORID_MARVELL,
		DEVICEID_SK_V2,
		"Marvell Gigabit Ethernet"
	},
	{
		VENDORID_3COM,
		DEVICEID_3COM_3C940,
		"3Com 3C940 Gigabit Ethernet"
	},
	{
		VENDORID_LINKSYS,
		DEVICEID_LINKSYS_EG1032,
		"Linksys EG1032 Gigabit Ethernet"
	},
	{
		VENDORID_DLINK,
		DEVICEID_DLINK_DGE530T,
		"D-Link DGE-530T Gigabit Ethernet"
	},
	{ 0, 0, NULL }
};

static int skc_probe		__P((device_t));
static int skc_attach		__P((device_t));
static int skc_detach		__P((device_t));
static void skc_shutdown	__P((device_t));
static int sk_probe		__P((device_t));
static int sk_attach		__P((device_t));
static int sk_detach		__P((device_t));
static void sk_tick		__P((void *));
static void sk_intr		__P((void *));
static void sk_intr_bcom	__P((struct sk_if_softc *));
static void sk_intr_xmac	__P((struct sk_if_softc *));
static void sk_intr_yukon	__P((struct sk_if_softc *));
static void sk_rxeof		__P((struct sk_if_softc *));
static void sk_txeof		__P((struct sk_if_softc *));
static int sk_encap		__P((struct sk_if_softc *, struct mbuf *,
					u_int32_t *));
static void sk_start		__P((struct ifnet *));
static int sk_ioctl		__P((struct ifnet *, u_long, caddr_t));
static void sk_init		__P((void *));
static void sk_init_xmac	__P((struct sk_if_softc *));
static void sk_init_yukon	__P((struct sk_if_softc *));
static void sk_stop		__P((struct sk_if_softc *));
static void sk_watchdog		__P((struct ifnet *));
static int sk_ifmedia_upd	__P((struct ifnet *));
static void sk_ifmedia_sts	__P((struct ifnet *, struct ifmediareq *));
static void sk_reset		__P((struct sk_softc *));
static int sk_newbuf		__P((struct sk_if_softc *,
					struct sk_chain *, struct mbuf *));
static int sk_alloc_jumbo_mem	__P((struct sk_if_softc *));
static void *sk_jalloc		__P((struct sk_if_softc *));
static void sk_jfree		__P((caddr_t, u_int));
static void sk_jref		__P((caddr_t, u_int));
static int sk_init_rx_ring	__P((struct sk_if_softc *));
static void sk_init_tx_ring	__P((struct sk_if_softc *));
static u_int32_t sk_win_read_4	__P((struct sk_softc *, int));
static u_int16_t sk_win_read_2	__P((struct sk_softc *, int));
static u_int8_t sk_win_read_1	__P((struct sk_softc *, int));
static void sk_win_write_4	__P((struct sk_softc *, int, u_int32_t));
static void sk_win_write_2	__P((struct sk_softc *, int, u_int32_t));
static void sk_win_write_1	__P((struct sk_softc *, int, u_int32_t));
static u_int8_t sk_vpd_readbyte	__P((struct sk_softc *, int));
static void sk_vpd_read_res	__P((struct sk_softc *,
					struct vpd_res *, int));
static void sk_vpd_read		__P((struct sk_softc *));

static int sk_miibus_readreg	__P((device_t, int, int));
static int sk_miibus_writereg	__P((device_t, int, int, int));
static void sk_miibus_statchg	__P((device_t));

static int sk_xmac_miibus_readreg	__P((struct sk_if_softc *, int, int));
static int sk_xmac_miibus_writereg	__P((struct sk_if_softc *, int, int,
						int));
static void sk_xmac_miibus_statchg	__P((struct sk_if_softc *));

static int sk_marv_miibus_readreg	__P((struct sk_if_softc *, int, int));
static int sk_marv_miibus_writereg	__P((struct sk_if_softc *, int, int,
						int));
static void sk_marv_miibus_statchg	__P((struct sk_if_softc *));

static u_int32_t sk_calchash	__P((caddr_t));
static void sk_setfilt		__P((struct sk_if_softc *, caddr_t, int));
static void sk_setmulti		__P((struct sk_if_softc *));

#ifdef SK_USEIOSPACE
#define SK_RES		SYS_RES_IOPORT
#define SK_RID		SK_PCI_LOIO
#else
#define SK_RES		SYS_RES_MEMORY
#define SK_RID		SK_PCI_LOMEM
#endif

/*
 * Note that we have newbus methods for both the GEnesis controller
 * itself and the XMAC(s). The XMACs are children of the GEnesis, and
 * the miibus code is a child of the XMACs. We need to do it this way
 * so that the miibus drivers can access the PHY registers on the
 * right PHY. It's not quite what I had in mind, but it's the only
 * design that achieves the desired effect.
 */
static device_method_t skc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		skc_probe),
	DEVMETHOD(device_attach,	skc_attach),
	DEVMETHOD(device_detach,	skc_detach),
	DEVMETHOD(device_shutdown,	skc_shutdown),

	/* bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	{ 0, 0 }
};

static driver_t skc_driver = {
	"skc",
	skc_methods,
	sizeof(struct sk_softc)
};

static devclass_t skc_devclass;

static device_method_t sk_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sk_probe),
	DEVMETHOD(device_attach,	sk_attach),
	DEVMETHOD(device_detach,	sk_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),

	/* bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	sk_miibus_readreg),
	DEVMETHOD(miibus_writereg,	sk_miibus_writereg),
	DEVMETHOD(miibus_statchg,	sk_miibus_statchg),

	{ 0, 0 }
};

static driver_t sk_driver = {
	"sk",
	sk_methods,
	sizeof(struct sk_if_softc)
};

static devclass_t sk_devclass;

DRIVER_MODULE(if_sk, pci, skc_driver, skc_devclass, 0, 0);
DRIVER_MODULE(sk, skc, sk_driver, sk_devclass, 0, 0);
DRIVER_MODULE(miibus, sk, miibus_driver, miibus_devclass, 0, 0);

#define SK_SETBIT(sc, reg, x)		\
	CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) | x)

#define SK_CLRBIT(sc, reg, x)		\
	CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) & ~x)

#define SK_WIN_SETBIT_4(sc, reg, x)	\
	sk_win_write_4(sc, reg, sk_win_read_4(sc, reg) | x)

#define SK_WIN_CLRBIT_4(sc, reg, x)	\
	sk_win_write_4(sc, reg, sk_win_read_4(sc, reg) & ~x)

#define SK_WIN_SETBIT_2(sc, reg, x)	\
	sk_win_write_2(sc, reg, sk_win_read_2(sc, reg) | x)

#define SK_WIN_CLRBIT_2(sc, reg, x)	\
	sk_win_write_2(sc, reg, sk_win_read_2(sc, reg) & ~x)

static u_int32_t sk_win_read_4(sc, reg)
	struct sk_softc		*sc;
	int			reg;
{
#ifdef SK_USEIOSPACE
	CSR_WRITE_4(sc, SK_RAP, SK_WIN(reg));
	return(CSR_READ_4(sc, SK_WIN_BASE + SK_REG(reg)));
#else
	return(CSR_READ_4(sc, reg));
#endif
}

static u_int16_t sk_win_read_2(sc, reg)
	struct sk_softc		*sc;
	int			reg;
{
#ifdef SK_USEIOSPACE
	CSR_WRITE_4(sc, SK_RAP, SK_WIN(reg));
	return(CSR_READ_2(sc, SK_WIN_BASE + SK_REG(reg)));
#else
	return(CSR_READ_2(sc, reg));
#endif
}

static u_int8_t sk_win_read_1(sc, reg)
	struct sk_softc		*sc;
	int			reg;
{
#ifdef SK_USEIOSPACE
	CSR_WRITE_4(sc, SK_RAP, SK_WIN(reg));
	return(CSR_READ_1(sc, SK_WIN_BASE + SK_REG(reg)));
#else
	return(CSR_READ_1(sc, reg));
#endif
}

static void sk_win_write_4(sc, reg, val)
	struct sk_softc		*sc;
	int			reg;
	u_int32_t		val;
{
#ifdef SK_USEIOSPACE
	CSR_WRITE_4(sc, SK_RAP, SK_WIN(reg));
	CSR_WRITE_4(sc, SK_WIN_BASE + SK_REG(reg), val);
#else
	CSR_WRITE_4(sc, reg, val);
#endif
	return;
}

static void sk_win_write_2(sc, reg, val)
	struct sk_softc		*sc;
	int			reg;
	u_int32_t		val;
{
#ifdef SK_USEIOSPACE
	CSR_WRITE_4(sc, SK_RAP, SK_WIN(reg));
	CSR_WRITE_2(sc, SK_WIN_BASE + SK_REG(reg), val);
#else
	CSR_WRITE_2(sc, reg, val);
#endif
	return;
}

static void sk_win_write_1(sc, reg, val)
	struct sk_softc		*sc;
	int			reg;
	u_int32_t		val;
{
#ifdef SK_USEIOSPACE
	CSR_WRITE_4(sc, SK_RAP, SK_WIN(reg));
	CSR_WRITE_1(sc, SK_WIN_BASE + SK_REG(reg), val);
#else
	CSR_WRITE_1(sc, reg, val);
#endif
	return;
}

/*
 * The VPD EEPROM contains Vital Product Data, as suggested in
 * the PCI 2.1 specification. The VPD data is separared into areas
 * denoted by resource IDs. The SysKonnect VPD contains an ID string
 * resource (the name of the adapter), a read-only area resource
 * containing various key/data fields and a read/write area which
 * can be used to store asset management information or log messages.
 * We read the ID string and read-only into buffers attached to
 * the controller softc structure for later use. At the moment,
 * we only use the ID string during sk_attach().
 */
static u_int8_t sk_vpd_readbyte(sc, addr)
	struct sk_softc		*sc;
	int			addr;
{
	int			i;

	sk_win_write_2(sc, SK_PCI_REG(SK_PCI_VPD_ADDR), addr);
	for (i = 0; i < SK_TIMEOUT; i++) {
		DELAY(1);
		if (sk_win_read_2(sc,
		    SK_PCI_REG(SK_PCI_VPD_ADDR)) & SK_VPD_FLAG)
			break;
	}

	if (i == SK_TIMEOUT)
		return(0);

	return(sk_win_read_1(sc, SK_PCI_REG(SK_PCI_VPD_DATA)));
}

static void sk_vpd_read_res(sc, res, addr)
	struct sk_softc		*sc;
	struct vpd_res		*res;
	int			addr;
{
	int			i;
	u_int8_t		*ptr;

	ptr = (u_int8_t *)res;
	for (i = 0; i < sizeof(struct vpd_res); i++)
		ptr[i] = sk_vpd_readbyte(sc, i + addr);

	return;
}

static void sk_vpd_read(sc)
	struct sk_softc		*sc;
{
	int			pos = 0, i;
	struct vpd_res		res;

	if (sc->sk_vpd_prodname != NULL)
		free(sc->sk_vpd_prodname, M_DEVBUF);
	if (sc->sk_vpd_readonly != NULL)
		free(sc->sk_vpd_readonly, M_DEVBUF);
	sc->sk_vpd_prodname = NULL;
	sc->sk_vpd_readonly = NULL;

	sk_vpd_read_res(sc, &res, pos);

	/*
	 * Bail out quietly if the eeprom appears to be missing or empty.
	 */
	if (res.vr_id == 0xff && res.vr_len == 0xff && res.vr_pad == 0xff)
		return;

	if (res.vr_id != VPD_RES_ID) {
		printf("skc%d: bad VPD resource id: expected %x got %x\n",
		    sc->sk_unit, VPD_RES_ID, res.vr_id);
		return;
	}

	pos += sizeof(res);
	sc->sk_vpd_prodname = malloc(res.vr_len + 1, M_DEVBUF, M_NOWAIT);
	for (i = 0; i < res.vr_len; i++)
		sc->sk_vpd_prodname[i] = sk_vpd_readbyte(sc, i + pos);
	sc->sk_vpd_prodname[i] = '\0';
	pos += i;

	sk_vpd_read_res(sc, &res, pos);

	if (res.vr_id != VPD_RES_READ) {
		printf("skc%d: bad VPD resource id: expected %x got %x\n",
		    sc->sk_unit, VPD_RES_READ, res.vr_id);
		return;
	}

	pos += sizeof(res);
	sc->sk_vpd_readonly = malloc(res.vr_len, M_DEVBUF, M_NOWAIT);
	for (i = 0; i < res.vr_len; i++)
		sc->sk_vpd_readonly[i] = sk_vpd_readbyte(sc, i + pos);

	return;
}

static int sk_miibus_readreg(dev, phy, reg)
	device_t		dev;
	int			phy, reg;
{
	struct sk_if_softc	*sc_if;

	sc_if = device_get_softc(dev);

	switch(sc_if->sk_softc->sk_type) {
	case SK_GENESIS:
		return(sk_xmac_miibus_readreg(sc_if, phy, reg));
	case SK_YUKON:
		return(sk_marv_miibus_readreg(sc_if, phy, reg));
	}

	return(0);
}

static int sk_miibus_writereg(dev, phy, reg, val)
	device_t		dev;
	int			phy, reg, val;
{
	struct sk_if_softc	*sc_if;

	sc_if = device_get_softc(dev);

	switch(sc_if->sk_softc->sk_type) {
	case SK_GENESIS:
		return(sk_xmac_miibus_writereg(sc_if, phy, reg, val));
	case SK_YUKON:
		return(sk_marv_miibus_writereg(sc_if, phy, reg, val));
	}

	return(0);
}

static void sk_miibus_statchg(dev)
	device_t		dev;
{
	struct sk_if_softc	*sc_if;

	sc_if = device_get_softc(dev);

	switch(sc_if->sk_softc->sk_type) {
	case SK_GENESIS:
		sk_xmac_miibus_statchg(sc_if);
		break;
	case SK_YUKON:
		sk_marv_miibus_statchg(sc_if);
		break;
	}

	return;
}

static int sk_xmac_miibus_readreg(sc_if, phy, reg)
	struct sk_if_softc	*sc_if;
	int			phy, reg;
{
	int			i;

	if (sc_if->sk_phytype == SK_PHYTYPE_XMAC && phy != 0)
		return(0);

	SK_XM_WRITE_2(sc_if, XM_PHY_ADDR, reg|(phy << 8));
	SK_XM_READ_2(sc_if, XM_PHY_DATA);
	if (sc_if->sk_phytype != SK_PHYTYPE_XMAC) {
		for (i = 0; i < SK_TIMEOUT; i++) {
			DELAY(1);
			if (SK_XM_READ_2(sc_if, XM_MMUCMD) &
			    XM_MMUCMD_PHYDATARDY)
				break;
		}

		if (i == SK_TIMEOUT) {
			printf("sk%d: phy failed to come ready\n",
			    sc_if->sk_unit);
			return(0);
		}
	}
	DELAY(1);
	return(SK_XM_READ_2(sc_if, XM_PHY_DATA));
}

static int sk_xmac_miibus_writereg(sc_if, phy, reg, val)
	struct sk_if_softc	*sc_if;
	int			phy, reg, val;
{
	int			i;

	SK_XM_WRITE_2(sc_if, XM_PHY_ADDR, reg|(phy << 8));
	for (i = 0; i < SK_TIMEOUT; i++) {
		if (!(SK_XM_READ_2(sc_if, XM_MMUCMD) & XM_MMUCMD_PHYBUSY))
			break;
	}

	if (i == SK_TIMEOUT) {
		printf("sk%d: phy failed to come ready\n", sc_if->sk_unit);
		return(ETIMEDOUT);
	}

	SK_XM_WRITE_2(sc_if, XM_PHY_DATA, val);
	for (i = 0; i < SK_TIMEOUT; i++) {
		DELAY(1);
		if (!(SK_XM_READ_2(sc_if, XM_MMUCMD) & XM_MMUCMD_PHYBUSY))
			break;
	}

	if (i == SK_TIMEOUT)
		printf("sk%d: phy write timed out\n", sc_if->sk_unit);

	return(0);
}

static void sk_xmac_miibus_statchg(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct mii_data		*mii;

	mii = device_get_softc(sc_if->sk_miibus);

	/*
	 * If this is a GMII PHY, manually set the XMAC's
	 * duplex mode accordingly.
	 */
	if (sc_if->sk_phytype != SK_PHYTYPE_XMAC) {
		if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX) {
			SK_XM_SETBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_GMIIFDX);
		} else {
			SK_XM_CLRBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_GMIIFDX);
		}
	}

	return;
}

static int sk_marv_miibus_readreg(sc_if, phy, reg)
	struct sk_if_softc	*sc_if;
	int			phy, reg;
{
	u_int16_t		val;
	int			i;

	if (phy != 0 ||
	    (sc_if->sk_phytype != SK_PHYTYPE_MARV_COPPER &&
	     sc_if->sk_phytype != SK_PHYTYPE_MARV_FIBER)) {
		return(0);
	}

        SK_YU_WRITE_2(sc_if, YUKON_SMICR, YU_SMICR_PHYAD(phy) |
		      YU_SMICR_REGAD(reg) | YU_SMICR_OP_READ);
        
	for (i = 0; i < SK_TIMEOUT; i++) {
		DELAY(1);
		val = SK_YU_READ_2(sc_if, YUKON_SMICR);
		if (val & YU_SMICR_READ_VALID)
			break;
	}

	if (i == SK_TIMEOUT) {
		printf("sk%d: phy failed to come ready\n",
		    sc_if->sk_unit);
		return(0);
	}
        
	val = SK_YU_READ_2(sc_if, YUKON_SMIDR);

	return(val);
}

static int sk_marv_miibus_writereg(sc_if, phy, reg, val)
	struct sk_if_softc	*sc_if;
	int			phy, reg, val;
{
	int			i;

	SK_YU_WRITE_2(sc_if, YUKON_SMIDR, val);
	SK_YU_WRITE_2(sc_if, YUKON_SMICR, YU_SMICR_PHYAD(phy) |
		      YU_SMICR_REGAD(reg) | YU_SMICR_OP_WRITE);

	for (i = 0; i < SK_TIMEOUT; i++) {
		DELAY(1);
		if (SK_YU_READ_2(sc_if, YUKON_SMICR) & YU_SMICR_BUSY)
			break;
	}

	return(0);
}

static void sk_marv_miibus_statchg(sc_if)
	struct sk_if_softc	*sc_if;
{
	return;
}

#define SK_POLY		0xEDB88320
#define SK_BITS		6

static u_int32_t sk_calchash(addr)
	caddr_t			addr;
{
	u_int32_t		idx, bit, data, crc;

	/* Compute CRC for the address value. */
	crc = 0xFFFFFFFF; /* initial value */

	for (idx = 0; idx < 6; idx++) {
		for (data = *addr++, bit = 0; bit < 8; bit++, data >>= 1)
			crc = (crc >> 1) ^ (((crc ^ data) & 1) ? SK_POLY : 0);
	}

	return (~crc & ((1 << SK_BITS) - 1));
}

static void sk_setfilt(sc_if, addr, slot)
	struct sk_if_softc	*sc_if;
	caddr_t			addr;
	int			slot;
{
	int			base;

	base = XM_RXFILT_ENTRY(slot);

	SK_XM_WRITE_2(sc_if, base, *(u_int16_t *)(&addr[0]));
	SK_XM_WRITE_2(sc_if, base + 2, *(u_int16_t *)(&addr[2]));
	SK_XM_WRITE_2(sc_if, base + 4, *(u_int16_t *)(&addr[4]));

	return;
}

static void sk_setmulti(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_softc		*sc = sc_if->sk_softc;
	struct ifnet		*ifp = &sc_if->arpcom.ac_if;
	u_int32_t		hashes[2] = { 0, 0 };
	int			h, i;
	struct ifmultiaddr	*ifma;
	u_int8_t		dummy[] = { 0, 0, 0, 0, 0 ,0 };


	/* First, zot all the existing filters. */
	switch(sc->sk_type) {
	case SK_GENESIS:
		for (i = 1; i < XM_RXFILT_MAX; i++)
			sk_setfilt(sc_if, (caddr_t)&dummy, i);

		SK_XM_WRITE_4(sc_if, XM_MAR0, 0);
		SK_XM_WRITE_4(sc_if, XM_MAR2, 0);
		break;
	case SK_YUKON:
		SK_YU_WRITE_2(sc_if, YUKON_MCAH1, 0);
		SK_YU_WRITE_2(sc_if, YUKON_MCAH2, 0);
		SK_YU_WRITE_2(sc_if, YUKON_MCAH3, 0);
		SK_YU_WRITE_2(sc_if, YUKON_MCAH4, 0);
		break;
	}

	/* Now program new ones. */
	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		hashes[0] = 0xFFFFFFFF;
		hashes[1] = 0xFFFFFFFF;
	} else {
		i = 1;
		/* First find the tail of the list. */
		for (ifma = ifp->if_multiaddrs.lh_first; ifma != NULL;
					ifma = ifma->ifma_link.le_next) {
			if (ifma->ifma_link.le_next == NULL)
				break;
		}
		/* Now traverse the list backwards. */
		for (; ifma != NULL && ifma != (void *)&ifp->if_multiaddrs;
			ifma = (struct ifmultiaddr *)ifma->ifma_link.le_prev) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			/*
			 * Program the first XM_RXFILT_MAX multicast groups
			 * into the perfect filter. For all others,
			 * use the hash table.
			 */
			if (sc->sk_type == SK_GENESIS && i < XM_RXFILT_MAX) {
				sk_setfilt(sc_if,
			LLADDR((struct sockaddr_dl *)ifma->ifma_addr), i);
				i++;
				continue;
			}

			h = sk_calchash(
				LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
			if (h < 32)
				hashes[0] |= (1 << h);
			else
				hashes[1] |= (1 << (h - 32));
		}
	}

	switch(sc->sk_type) {
	case SK_GENESIS:
		SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_RX_USE_HASH|
			       XM_MODE_RX_USE_PERFECT);
		SK_XM_WRITE_4(sc_if, XM_MAR0, hashes[0]);
		SK_XM_WRITE_4(sc_if, XM_MAR2, hashes[1]);
		break;
	case SK_YUKON:
		SK_YU_WRITE_2(sc_if, YUKON_MCAH1, hashes[0] & 0xffff);
		SK_YU_WRITE_2(sc_if, YUKON_MCAH2, (hashes[0] >> 16) & 0xffff);
		SK_YU_WRITE_2(sc_if, YUKON_MCAH3, hashes[1] & 0xffff);
		SK_YU_WRITE_2(sc_if, YUKON_MCAH4, (hashes[1] >> 16) & 0xffff);
		break;
	}

	return;
}

static int sk_init_rx_ring(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_chain_data	*cd = &sc_if->sk_cdata;
	struct sk_ring_data	*rd = sc_if->sk_rdata;
	int			i;

	bzero((char *)rd->sk_rx_ring,
	    sizeof(struct sk_rx_desc) * SK_RX_RING_CNT);

	for (i = 0; i < SK_RX_RING_CNT; i++) {
		cd->sk_rx_chain[i].sk_desc = &rd->sk_rx_ring[i];
		if (sk_newbuf(sc_if, &cd->sk_rx_chain[i], NULL) == ENOBUFS)
			return(ENOBUFS);
		if (i == (SK_RX_RING_CNT - 1)) {
			cd->sk_rx_chain[i].sk_next =
			    &cd->sk_rx_chain[0];
			rd->sk_rx_ring[i].sk_next = 
			    vtophys(&rd->sk_rx_ring[0]);
		} else {
			cd->sk_rx_chain[i].sk_next =
			    &cd->sk_rx_chain[i + 1];
			rd->sk_rx_ring[i].sk_next = 
			    vtophys(&rd->sk_rx_ring[i + 1]);
		}
	}

	sc_if->sk_cdata.sk_rx_prod = 0;
	sc_if->sk_cdata.sk_rx_cons = 0;

	return(0);
}

static void sk_init_tx_ring(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_chain_data	*cd = &sc_if->sk_cdata;
	struct sk_ring_data	*rd = sc_if->sk_rdata;
	int			i;

	bzero((char *)sc_if->sk_rdata->sk_tx_ring,
	    sizeof(struct sk_tx_desc) * SK_TX_RING_CNT);

	for (i = 0; i < SK_TX_RING_CNT; i++) {
		cd->sk_tx_chain[i].sk_desc = &rd->sk_tx_ring[i];
		if (i == (SK_TX_RING_CNT - 1)) {
			cd->sk_tx_chain[i].sk_next =
			    &cd->sk_tx_chain[0];
			rd->sk_tx_ring[i].sk_next = 
			    vtophys(&rd->sk_tx_ring[0]);
		} else {
			cd->sk_tx_chain[i].sk_next =
			    &cd->sk_tx_chain[i + 1];
			rd->sk_tx_ring[i].sk_next = 
			    vtophys(&rd->sk_tx_ring[i + 1]);
		}
	}

	sc_if->sk_cdata.sk_tx_prod = 0;
	sc_if->sk_cdata.sk_tx_cons = 0;
	sc_if->sk_cdata.sk_tx_cnt = 0;

	return;
}

static int sk_newbuf(sc_if, c, m)
	struct sk_if_softc	*sc_if;
	struct sk_chain		*c;
	struct mbuf		*m;
{
	struct mbuf		*m_new = NULL;
	struct sk_rx_desc	*r;

	if (m == NULL) {
		caddr_t			*buf = NULL;

		MGETHDR(m_new, M_DONTWAIT, MT_DATA);
		if (m_new == NULL)
			return(ENOBUFS);

		/* Allocate the jumbo buffer */
		buf = sk_jalloc(sc_if);
		if (buf == NULL) {
			m_freem(m_new);
#ifdef SK_VERBOSE
			printf("sk%d: jumbo allocation failed "
			    "-- packet dropped!\n", sc_if->sk_unit);
#endif
			return(ENOBUFS);
		}

		/* Attach the buffer to the mbuf */
		m_new->m_data = m_new->m_ext.ext_buf = (void *)buf;
		m_new->m_flags |= M_EXT;
		m_new->m_ext.ext_size = m_new->m_pkthdr.len =
		    m_new->m_len = SK_MCLBYTES;
		m_new->m_ext.ext_free = sk_jfree;
		m_new->m_ext.ext_ref = sk_jref;
	} else {
		/*
	 	 * We're re-using a previously allocated mbuf;
		 * be sure to re-init pointers and lengths to
		 * default values.
		 */
		m_new = m;
		m_new->m_len = m_new->m_pkthdr.len = SK_MCLBYTES;
		m_new->m_data = m_new->m_ext.ext_buf;
	}

	/*
	 * Adjust alignment so packet payload begins on a
	 * longword boundary. Mandatory for Alpha, useful on
	 * x86 too.
	 */
	m_adj(m_new, ETHER_ALIGN);

	r = c->sk_desc;
	c->sk_mbuf = m_new;
	r->sk_data_lo = vtophys(mtod(m_new, caddr_t));
	r->sk_ctl = m_new->m_len | SK_RXSTAT;

	return(0);
}

/*
 * Allocate jumbo buffer storage. The SysKonnect adapters support
 * "jumbograms" (9K frames), although SysKonnect doesn't currently
 * use them in their drivers. In order for us to use them, we need
 * large 9K receive buffers, however standard mbuf clusters are only
 * 2048 bytes in size. Consequently, we need to allocate and manage
 * our own jumbo buffer pool. Fortunately, this does not require an
 * excessive amount of additional code.
 */
static int sk_alloc_jumbo_mem(sc_if)
	struct sk_if_softc	*sc_if;
{
	caddr_t			ptr;
	register int		i;
	struct sk_jpool_entry   *entry;

	/* Grab a big chunk o' storage. */
	sc_if->sk_cdata.sk_jumbo_buf = contigmalloc(SK_JMEM, M_DEVBUF,
	    M_NOWAIT, 0, 0xffffffff, PAGE_SIZE, 0);

	if (sc_if->sk_cdata.sk_jumbo_buf == NULL) {
		printf("sk%d: no memory for jumbo buffers!\n", sc_if->sk_unit);
		return(ENOBUFS);
	}

	SLIST_INIT(&sc_if->sk_jfree_listhead);
	SLIST_INIT(&sc_if->sk_jinuse_listhead);

	/*
	 * Now divide it up into 9K pieces and save the addresses
	 * in an array. Note that we play an evil trick here by using
	 * the first few bytes in the buffer to hold the the address
	 * of the softc structure for this interface. This is because
	 * sk_jfree() needs it, but it is called by the mbuf management
	 * code which will not pass it to us explicitly.
	 */
	ptr = sc_if->sk_cdata.sk_jumbo_buf;
	for (i = 0; i < SK_JSLOTS; i++) {
		u_int64_t		**aptr;
		aptr = (u_int64_t **)ptr;
		aptr[0] = (u_int64_t *)sc_if;
		ptr += sizeof(u_int64_t);
		sc_if->sk_cdata.sk_jslots[i].sk_buf = ptr;
		sc_if->sk_cdata.sk_jslots[i].sk_inuse = 0;
		ptr += SK_MCLBYTES;
		entry = malloc(sizeof(struct sk_jpool_entry), 
		    M_DEVBUF, M_NOWAIT);
		if (entry == NULL) {
			free(sc_if->sk_cdata.sk_jumbo_buf, M_DEVBUF);
			sc_if->sk_cdata.sk_jumbo_buf = NULL;
			printf("sk%d: no memory for jumbo "
			    "buffer queue!\n", sc_if->sk_unit);
			return(ENOBUFS);
		}
		entry->slot = i;
		SLIST_INSERT_HEAD(&sc_if->sk_jfree_listhead,
		    entry, jpool_entries);
	}

	return(0);
}

/*
 * Allocate a jumbo buffer.
 */
static void *sk_jalloc(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_jpool_entry   *entry;
	
	entry = SLIST_FIRST(&sc_if->sk_jfree_listhead);
	
	if (entry == NULL) {
#ifdef SK_VERBOSE
		printf("sk%d: no free jumbo buffers\n", sc_if->sk_unit);
#endif
		return(NULL);
	}

	SLIST_REMOVE_HEAD(&sc_if->sk_jfree_listhead, jpool_entries);
	SLIST_INSERT_HEAD(&sc_if->sk_jinuse_listhead, entry, jpool_entries);
	sc_if->sk_cdata.sk_jslots[entry->slot].sk_inuse = 1;
	return(sc_if->sk_cdata.sk_jslots[entry->slot].sk_buf);
}

/*
 * Adjust usage count on a jumbo buffer. In general this doesn't
 * get used much because our jumbo buffers don't get passed around
 * a lot, but it's implemented for correctness.
 */
static void sk_jref(buf, size)
	caddr_t			buf;
	u_int			size;
{
	struct sk_if_softc	*sc_if;
	u_int64_t		**aptr;
	register int		i;

	/* Extract the softc struct pointer. */
	aptr = (u_int64_t **)(buf - sizeof(u_int64_t));
	sc_if = (struct sk_if_softc *)(aptr[0]);

	if (sc_if == NULL)
		panic("sk_jref: can't find softc pointer!");

	if (size != SK_MCLBYTES)
		panic("sk_jref: adjusting refcount of buf of wrong size!");

	/* calculate the slot this buffer belongs to */

	i = ((vm_offset_t)aptr 
	     - (vm_offset_t)sc_if->sk_cdata.sk_jumbo_buf) / SK_JLEN;

	if ((i < 0) || (i >= SK_JSLOTS))
		panic("sk_jref: asked to reference buffer "
		    "that we don't manage!");
	else if (sc_if->sk_cdata.sk_jslots[i].sk_inuse == 0)
		panic("sk_jref: buffer already free!");
	else
		sc_if->sk_cdata.sk_jslots[i].sk_inuse++;

	return;
}

/*
 * Release a jumbo buffer.
 */
static void sk_jfree(buf, size)
	caddr_t			buf;
	u_int			size;
{
	struct sk_if_softc	*sc_if;
	u_int64_t		**aptr;
	int		        i;
	struct sk_jpool_entry   *entry;

	/* Extract the softc struct pointer. */
	aptr = (u_int64_t **)(buf - sizeof(u_int64_t));
	sc_if = (struct sk_if_softc *)(aptr[0]);

	if (sc_if == NULL)
		panic("sk_jfree: can't find softc pointer!");

	if (size != SK_MCLBYTES)
		panic("sk_jfree: freeing buffer of wrong size!");

	/* calculate the slot this buffer belongs to */

	i = ((vm_offset_t)aptr 
	     - (vm_offset_t)sc_if->sk_cdata.sk_jumbo_buf) / SK_JLEN;

	if ((i < 0) || (i >= SK_JSLOTS))
		panic("sk_jfree: asked to free buffer that we don't manage!");
	else if (sc_if->sk_cdata.sk_jslots[i].sk_inuse == 0)
		panic("sk_jfree: buffer already free!");
	else {
		sc_if->sk_cdata.sk_jslots[i].sk_inuse--;
		if(sc_if->sk_cdata.sk_jslots[i].sk_inuse == 0) {
			entry = SLIST_FIRST(&sc_if->sk_jinuse_listhead);
			if (entry == NULL)
				panic("sk_jfree: buffer not in use!");
			entry->slot = i;
			SLIST_REMOVE_HEAD(&sc_if->sk_jinuse_listhead, 
					  jpool_entries);
			SLIST_INSERT_HEAD(&sc_if->sk_jfree_listhead, 
					  entry, jpool_entries);
		}
	}

	return;
}

/*
 * Set media options.
 */
static int sk_ifmedia_upd(ifp)
	struct ifnet		*ifp;
{
	struct sk_if_softc	*sc_if = ifp->if_softc;
	struct mii_data		*mii;

	mii = device_get_softc(sc_if->sk_miibus);
	sk_init(sc_if);
	mii_mediachg(mii);

	return(0);
}

/*
 * Report current media status.
 */
static void sk_ifmedia_sts(ifp, ifmr)
	struct ifnet		*ifp;
	struct ifmediareq	*ifmr;
{
	struct sk_if_softc	*sc_if;
	struct mii_data		*mii;

	sc_if = ifp->if_softc;
	mii = device_get_softc(sc_if->sk_miibus);

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;

	return;
}

static int sk_ioctl(ifp, command, data)
	struct ifnet		*ifp;
	u_long			command;
	caddr_t			data;
{
	struct sk_if_softc	*sc_if = ifp->if_softc;
	struct sk_softc		*sc = sc_if->sk_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
	int			s, error = 0;
	struct mii_data		*mii;

	s = splimp();

	switch(command) {
	case SIOCSIFADDR:
	case SIOCGIFADDR:
		error = ether_ioctl(ifp, command, data);
		break;
	case SIOCSIFMTU:
		if (ifr->ifr_mtu > SK_JUMBO_MTU)
			error = EINVAL;
		else {
			ifp->if_mtu = ifr->ifr_mtu;
			sk_init(sc_if);
		}
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING &&
			    ifp->if_flags & IFF_PROMISC &&
			    !(sc_if->sk_if_flags & IFF_PROMISC)) {
				switch(sc->sk_type) {
				case SK_GENESIS:
					SK_XM_SETBIT_4(sc_if, XM_MODE,
					    XM_MODE_RX_PROMISC);
					break;
				case SK_YUKON:
					SK_YU_CLRBIT_2(sc_if, YUKON_RCR,
					    YU_RCR_UFLEN | YU_RCR_MUFLEN);
					break;
				}
				sk_setmulti(sc_if);
			} else if (ifp->if_flags & IFF_RUNNING &&
			    !(ifp->if_flags & IFF_PROMISC) &&
			    sc_if->sk_if_flags & IFF_PROMISC) {
				switch(sc->sk_type) {
				case SK_GENESIS:
					SK_XM_CLRBIT_4(sc_if, XM_MODE,
					    XM_MODE_RX_PROMISC);
					break;
				case SK_YUKON:
					SK_YU_SETBIT_2(sc_if, YUKON_RCR,
					    YU_RCR_UFLEN | YU_RCR_MUFLEN);
					break;
				}
				sk_setmulti(sc_if);
			} else
				sk_init(sc_if);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				sk_stop(sc_if);
		}
		sc_if->sk_if_flags = ifp->if_flags;
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		sk_setmulti(sc_if);
		error = 0;
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc_if->sk_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	default:
		error = EINVAL;
		break;
	}

	(void)splx(s);

	return(error);
}

/*
 * Probe for a SysKonnect GEnesis chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
static int skc_probe(dev)
	device_t		dev;
{
	struct sk_softc		*sc;
	struct sk_type		*t = sk_devs;

	sc = device_get_softc(dev);

	while(t->sk_name != NULL) {
		if ((pci_get_vendor(dev) == t->sk_vid) &&
		    (pci_get_device(dev) == t->sk_did)) {
			device_set_desc(dev, t->sk_name);
			return(0);
		}
		t++;
	}

	return(ENXIO);
}

/*
 * Force the GEnesis into reset, then bring it out of reset.
 */
static void sk_reset(sc)
	struct sk_softc		*sc;
{
	CSR_WRITE_2(sc, SK_CSR, SK_CSR_SW_RESET);
	CSR_WRITE_2(sc, SK_CSR, SK_CSR_MASTER_RESET);
	if (sc->sk_type == SK_YUKON)
		CSR_WRITE_2(sc, SK_LINK_CTRL, SK_LINK_RESET_SET);

	DELAY(1000);
	CSR_WRITE_2(sc, SK_CSR, SK_CSR_SW_UNRESET);
	DELAY(2);
	CSR_WRITE_2(sc, SK_CSR, SK_CSR_MASTER_UNRESET);
	if (sc->sk_type == SK_YUKON)
		CSR_WRITE_2(sc, SK_LINK_CTRL, SK_LINK_RESET_CLEAR);

	if (sc->sk_type == SK_GENESIS) {
		/* Configure packet arbiter */
		sk_win_write_2(sc, SK_PKTARB_CTL, SK_PKTARBCTL_UNRESET);
		sk_win_write_2(sc, SK_RXPA1_TINIT, SK_PKTARB_TIMEOUT);
		sk_win_write_2(sc, SK_TXPA1_TINIT, SK_PKTARB_TIMEOUT);
		sk_win_write_2(sc, SK_RXPA2_TINIT, SK_PKTARB_TIMEOUT);
		sk_win_write_2(sc, SK_TXPA2_TINIT, SK_PKTARB_TIMEOUT);
	}

	/* Enable RAM interface */
	sk_win_write_4(sc, SK_RAMCTL, SK_RAMCTL_UNRESET);

	/*
         * Configure interrupt moderation. The moderation timer
	 * defers interrupts specified in the interrupt moderation
	 * timer mask based on the timeout specified in the interrupt
	 * moderation timer init register. Each bit in the timer
	 * register represents 18.825ns, so to specify a timeout in
	 * microseconds, we have to multiply by 54.
	 */
        sk_win_write_4(sc, SK_IMTIMERINIT, SK_IM_USECS(200));
        sk_win_write_4(sc, SK_IMMR, SK_ISR_TX1_S_EOF|SK_ISR_TX2_S_EOF|
	    SK_ISR_RX1_EOF|SK_ISR_RX2_EOF);
        sk_win_write_1(sc, SK_IMTIMERCTL, SK_IMCTL_START);

	return;
}

static int sk_probe(dev)
	device_t		dev;
{
	struct sk_softc		*sc;

	sc = device_get_softc(device_get_parent(dev));

	/*
	 * Not much to do here. We always know there will be
	 * at least one XMAC present, and if there are two,
	 * skc_attach() will create a second device instance
	 * for us.
	 */
	switch (sc->sk_type) {
	case SK_GENESIS:
		device_set_desc(dev, "XaQti Corp. XMAC II");
		break;
	case SK_YUKON:
		device_set_desc(dev, "Marvell Semiconductor, Inc. Yukon");
		break;
	}

	return(0);
}

/*
 * Each XMAC chip is attached as a separate logical IP interface.
 * Single port cards will have only one logical interface of course.
 */
static int sk_attach(dev)
	device_t		dev;
{
	struct sk_softc		*sc;
	struct sk_if_softc	*sc_if;
	struct ifnet		*ifp;
	int			i, port;

	if (dev == NULL)
		return(EINVAL);

	sc_if = device_get_softc(dev);
	sc = device_get_softc(device_get_parent(dev));
	port = *(int *)device_get_ivars(dev);
	free(device_get_ivars(dev), M_DEVBUF);
	device_set_ivars(dev, NULL);
	sc_if->sk_dev = dev;

	bzero((char *)sc_if, sizeof(struct sk_if_softc));

	sc_if->sk_dev = dev;
	sc_if->sk_unit = device_get_unit(dev);
	sc_if->sk_port = port;
	sc_if->sk_softc = sc;
	sc->sk_if[port] = sc_if;
	if (port == SK_PORT_A)
		sc_if->sk_tx_bmu = SK_BMU_TXS_CSR0;
	if (port == SK_PORT_B)
		sc_if->sk_tx_bmu = SK_BMU_TXS_CSR1;

	/*
	 * Get station address for this interface. Note that
	 * dual port cards actually come with three station
	 * addresses: one for each port, plus an extra. The
	 * extra one is used by the SysKonnect driver software
	 * as a 'virtual' station address for when both ports
	 * are operating in failover mode. Currently we don't
	 * use this extra address.
	 */
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		sc_if->arpcom.ac_enaddr[i] =
		    sk_win_read_1(sc, SK_MAC0_0 + (port * 8) + i);

	printf("sk%d: Ethernet address: %6D\n",
	    sc_if->sk_unit, sc_if->arpcom.ac_enaddr, ":");

	/*
	 * Set up RAM buffer addresses. The NIC will have a certain
	 * amount of SRAM on it, somewhere between 512K and 2MB. We
	 * need to divide this up a) between the transmitter and
 	 * receiver and b) between the two XMACs, if this is a
	 * dual port NIC. Our algotithm is to divide up the memory
	 * evenly so that everyone gets a fair share.
	 */
	if (sk_win_read_1(sc, SK_CONFIG) & SK_CONFIG_SINGLEMAC) {
		u_int32_t		chunk, val;

		chunk = sc->sk_ramsize / 2;
		val = sc->sk_rboff / sizeof(u_int64_t);
		sc_if->sk_rx_ramstart = val;
		val += (chunk / sizeof(u_int64_t));
		sc_if->sk_rx_ramend = val - 1;
		sc_if->sk_tx_ramstart = val;
		val += (chunk / sizeof(u_int64_t));
		sc_if->sk_tx_ramend = val - 1;
	} else {
		u_int32_t		chunk, val;

		chunk = sc->sk_ramsize / 4;
		val = (sc->sk_rboff + (chunk * 2 * sc_if->sk_port)) /
		    sizeof(u_int64_t);
		sc_if->sk_rx_ramstart = val;
		val += (chunk / sizeof(u_int64_t));
		sc_if->sk_rx_ramend = val - 1;
		sc_if->sk_tx_ramstart = val;
		val += (chunk / sizeof(u_int64_t));
		sc_if->sk_tx_ramend = val - 1;
	}

	/* Read and save PHY type and set PHY address */
	sc_if->sk_phytype = sk_win_read_1(sc, SK_EPROM1) & 0xF;
	switch(sc_if->sk_phytype) {
	case SK_PHYTYPE_XMAC:
		sc_if->sk_phyaddr = SK_PHYADDR_XMAC;
		break;
	case SK_PHYTYPE_BCOM:
		sc_if->sk_phyaddr = SK_PHYADDR_BCOM;
		break;
	case SK_PHYTYPE_MARV_COPPER:
		sc_if->sk_phyaddr = SK_PHYADDR_MARV;
		break;
	default:
		printf("skc%d: unsupported PHY type: %d\n",
		    sc->sk_unit, sc_if->sk_phytype);
		return(ENODEV);
	}

	/* Allocate the descriptor queues. */
	sc_if->sk_rdata = contigmalloc(sizeof(struct sk_ring_data), M_DEVBUF,
	    M_NOWAIT, 0, 0xffffffff, PAGE_SIZE, 0);

	if (sc_if->sk_rdata == NULL) {
		printf("sk%d: no memory for list buffers!\n", sc_if->sk_unit);
		sc->sk_if[port] = NULL;
		return(ENOMEM);
	}

	bzero(sc_if->sk_rdata, sizeof(struct sk_ring_data));

	/* Try to allocate memory for jumbo buffers. */
	if (sk_alloc_jumbo_mem(sc_if)) {
		printf("sk%d: jumbo buffer allocation failed\n",
		    sc_if->sk_unit);
		contigfree(sc_if->sk_rdata,
		    sizeof(struct sk_ring_data), M_DEVBUF);
		sc->sk_if[port] = NULL;
		return(ENOMEM);
	}

	ifp = &sc_if->arpcom.ac_if;
	ifp->if_softc = sc_if;
	ifp->if_unit = sc_if->sk_unit; 
	ifp->if_name = "sk";
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = sk_ioctl;
	ifp->if_output = ether_output;
	ifp->if_start = sk_start;
	ifp->if_watchdog = sk_watchdog;
	ifp->if_init = sk_init;
	ifp->if_baudrate = 1000000000;
	ifp->if_snd.ifq_maxlen = SK_TX_RING_CNT - 1;

	/*
	 * Do miibus setup.
	 */
	switch (sc->sk_type) {
	case SK_GENESIS:
		sk_init_xmac(sc_if);
		break;
	case SK_YUKON:
		sk_init_yukon(sc_if);
		break;
	}

	if (mii_phy_probe(dev, &sc_if->sk_miibus,
	    sk_ifmedia_upd, sk_ifmedia_sts)) {
		printf("skc%d: no PHY found!\n", sc_if->sk_unit);
		contigfree(sc_if->sk_cdata.sk_jumbo_buf, SK_JMEM,
		    M_DEVBUF);
		contigfree(sc_if->sk_rdata,
		    sizeof(struct sk_ring_data), M_DEVBUF);
		return(ENXIO);
	}

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, ETHER_BPF_SUPPORTED);
	callout_handle_init(&sc_if->sk_tick_ch);

	return(0);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int skc_attach(dev)
	device_t		dev;
{
	int			s;
	u_int32_t		command;
	struct sk_softc		*sc;
	int			unit, error = 0, rid, *port;
	uint8_t			skrs;

	s = splimp();

	sc = device_get_softc(dev);
	unit = device_get_unit(dev);
	bzero(sc, sizeof(struct sk_softc));
	switch (pci_get_device(dev)) {
	case DEVICEID_SK_V1:
		sc->sk_type = SK_GENESIS;
		break;
	case DEVICEID_SK_V2:
	case DEVICEID_3COM_3C940:
	case DEVICEID_LINKSYS_EG1032:
	case DEVICEID_DLINK_DGE530T:
		sc->sk_type = SK_YUKON;
		break;
	default:
		printf("skc%d: unknown device!\n", unit);
		error = ENXIO;
		goto fail;
	}

	/*
	 * Handle power management nonsense.
	 */
	command = pci_read_config(dev, SK_PCI_CAPID, 4) & 0x000000FF;
	if (command == 0x01) {
		command = pci_read_config(dev, SK_PCI_PWRMGMTCTRL, 4);
		if (command & SK_PSTATE_MASK) {
			u_int32_t		iobase, membase, irq;

			/* Save important PCI config data. */
			iobase = pci_read_config(dev, SK_PCI_LOIO, 4);
			membase = pci_read_config(dev, SK_PCI_LOMEM, 4);
			irq = pci_read_config(dev, SK_PCI_INTLINE, 4);

			/* Reset the power state. */
			printf("skc%d: chip is in D%d power mode "
			"-- setting to D0\n", unit, command & SK_PSTATE_MASK);
			command &= 0xFFFFFFFC;
			pci_write_config(dev, SK_PCI_PWRMGMTCTRL, command, 4);

			/* Restore PCI config data. */
			pci_write_config(dev, SK_PCI_LOIO, iobase, 4);
			pci_write_config(dev, SK_PCI_LOMEM, membase, 4);
			pci_write_config(dev, SK_PCI_INTLINE, irq, 4);
		}
	}

	/*
	 * Map control/status registers.
	 */
	command = pci_read_config(dev, PCIR_COMMAND, 4);
	command |= (PCIM_CMD_PORTEN|PCIM_CMD_MEMEN|PCIM_CMD_BUSMASTEREN);
	pci_write_config(dev, PCIR_COMMAND, command, 4);
	command = pci_read_config(dev, PCIR_COMMAND, 4);

#ifdef SK_USEIOSPACE
	if (!(command & PCIM_CMD_PORTEN)) {
		printf("skc%d: failed to enable I/O ports!\n", unit);
		error = ENXIO;
		goto fail;
	}
#else
	if (!(command & PCIM_CMD_MEMEN)) {
		printf("skc%d: failed to enable memory mapping!\n", unit);
		error = ENXIO;
		goto fail;
	}
#endif

	rid = SK_RID;
	sc->sk_res = bus_alloc_resource(dev, SK_RES, &rid,
	    0, ~0, 1, RF_ACTIVE);

	if (sc->sk_res == NULL) {
		printf("sk%d: couldn't map ports/memory\n", unit);
		error = ENXIO;
		goto fail;
	}

	sc->sk_btag = rman_get_bustag(sc->sk_res);
	sc->sk_bhandle = rman_get_bushandle(sc->sk_res);

	/* Allocate interrupt */
	rid = 0;
	sc->sk_irq = bus_alloc_resource(dev, SYS_RES_IRQ, &rid, 0, ~0, 1,
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->sk_irq == NULL) {
		printf("skc%d: couldn't map interrupt\n", unit);
		bus_release_resource(dev, SK_RES, SK_RID, sc->sk_res);
		error = ENXIO;
		goto fail;
	}

	error = bus_setup_intr(dev, sc->sk_irq, INTR_TYPE_NET,
	    sk_intr, sc, &sc->sk_intrhand);

	if (error) {
		printf("skc%d: couldn't set up irq\n", unit);
		bus_release_resource(dev, SK_RES, SK_RID, sc->sk_res);
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sk_irq);
		goto fail;
	}

	/* Reset the adapter. */
	sk_reset(sc);

	sc->sk_unit = unit;

	/* Read and save vital product data from EEPROM. */
	sk_vpd_read(sc);

	skrs = sk_win_read_1(sc, SK_EPROM0);
	if (sc->sk_type == SK_GENESIS) {
		/* Read and save RAM size and RAMbuffer offset */
		switch(skrs) {
		case SK_RAMSIZE_512K_64:
			sc->sk_ramsize = 0x80000;
			sc->sk_rboff = SK_RBOFF_0;
			break;
		case SK_RAMSIZE_1024K_64:
			sc->sk_ramsize = 0x100000;
			sc->sk_rboff = SK_RBOFF_80000;
			break;
		case SK_RAMSIZE_1024K_128:
			sc->sk_ramsize = 0x100000;
			sc->sk_rboff = SK_RBOFF_0;
			break;
		case SK_RAMSIZE_2048K_128:
			sc->sk_ramsize = 0x200000;
			sc->sk_rboff = SK_RBOFF_0;
			break;
		default:
			printf("skc%d: unknown ram size: %d\n",
			    sc->sk_unit, sk_win_read_1(sc, SK_EPROM0));
			bus_teardown_intr(dev, sc->sk_irq, sc->sk_intrhand);
			bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sk_irq);
			bus_release_resource(dev, SK_RES, SK_RID, sc->sk_res);
			error = ENXIO;
			goto fail;
			break;
		}
	} else {
		if (skrs == 0x00)
			sc->sk_ramsize = 0x20000;
		else
			sc->sk_ramsize = skrs * (1<<12);
		sc->sk_rboff = SK_RBOFF_0;
	}

	/* Read and save physical media type */
	switch(sk_win_read_1(sc, SK_PMDTYPE)) {
	case SK_PMD_1000BASESX:
		sc->sk_pmd = IFM_1000_SX;
		break;
	case SK_PMD_1000BASELX:
		sc->sk_pmd = IFM_1000_LX;
		break;
	case SK_PMD_1000BASECX:
		sc->sk_pmd = IFM_1000_CX;
		break;
	case SK_PMD_1000BASETX:
		sc->sk_pmd = IFM_1000_TX;
		break;
	default:
		printf("skc%d: unknown media type: 0x%x\n",
		    sc->sk_unit, sk_win_read_1(sc, SK_PMDTYPE));
		bus_teardown_intr(dev, sc->sk_irq, sc->sk_intrhand);
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sk_irq);
		bus_release_resource(dev, SK_RES, SK_RID, sc->sk_res);
		error = ENXIO;
		goto fail;
	}

	/* Announce the product name. */
	if (sc->sk_vpd_prodname != NULL)
	    printf("skc%d: %s\n", sc->sk_unit, sc->sk_vpd_prodname);
	sc->sk_devs[SK_PORT_A] = device_add_child(dev, "sk", -1);
	port = malloc(sizeof(int), M_DEVBUF, M_NOWAIT);
	*port = SK_PORT_A;
	device_set_ivars(sc->sk_devs[SK_PORT_A], port);

	if (!(sk_win_read_1(sc, SK_CONFIG) & SK_CONFIG_SINGLEMAC)) {
		sc->sk_devs[SK_PORT_B] = device_add_child(dev, "sk", -1);
		port = malloc(sizeof(int), M_DEVBUF, M_NOWAIT);
		*port = SK_PORT_B;
		device_set_ivars(sc->sk_devs[SK_PORT_B], port);
	}

	/* Turn on the 'driver is loaded' LED. */
	CSR_WRITE_2(sc, SK_LED, SK_LED_GREEN_ON);

	bus_generic_attach(dev);

fail:
	splx(s);
	return(error);
}

static int sk_detach(dev)
	device_t		dev;
{
	struct sk_softc		*sc;
	struct sk_if_softc	*sc_if;
	struct ifnet		*ifp;
	int			s;

	s = splimp();

	sc = device_get_softc(device_get_parent(dev));
	sc_if = device_get_softc(dev);
	ifp = &sc_if->arpcom.ac_if;
	sk_stop(sc_if);
	ether_ifdetach(ifp, ETHER_BPF_SUPPORTED);
	bus_generic_detach(dev);
	if (sc_if->sk_miibus != NULL)
		device_delete_child(dev, sc_if->sk_miibus);
	contigfree(sc_if->sk_cdata.sk_jumbo_buf, SK_JMEM, M_DEVBUF);
	contigfree(sc_if->sk_rdata, sizeof(struct sk_ring_data), M_DEVBUF);

	return(0);
}

static int skc_detach(dev)
	device_t		dev;
{
	struct sk_softc		*sc;
	int			s;

	s = splimp();

	sc = device_get_softc(dev);

	bus_generic_detach(dev);
	if (sc->sk_devs[SK_PORT_A] != NULL)
		device_delete_child(dev, sc->sk_devs[SK_PORT_A]);
	if (sc->sk_devs[SK_PORT_B] != NULL)
		device_delete_child(dev, sc->sk_devs[SK_PORT_B]);

	bus_teardown_intr(dev, sc->sk_irq, sc->sk_intrhand);
	bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sk_irq);
	bus_release_resource(dev, SK_RES, SK_RID, sc->sk_res);

	splx(s);

	return(0);
}

static int sk_encap(sc_if, m_head, txidx)
        struct sk_if_softc	*sc_if;
        struct mbuf		*m_head;
        u_int32_t		*txidx;
{
	struct sk_tx_desc	*f = NULL;
	struct mbuf		*m;
	u_int32_t		frag, cur, cnt = 0;

	m = m_head;
	cur = frag = *txidx;

	/*
	 * Start packing the mbufs in this chain into
	 * the fragment pointers. Stop when we run out
	 * of fragments or hit the end of the mbuf chain.
	 */
	for (m = m_head; m != NULL; m = m->m_next) {
		if (m->m_len != 0) {
			if ((SK_TX_RING_CNT -
			    (sc_if->sk_cdata.sk_tx_cnt + cnt)) < 2)
				return(ENOBUFS);
			f = &sc_if->sk_rdata->sk_tx_ring[frag];
			f->sk_data_lo = vtophys(mtod(m, vm_offset_t));
			f->sk_ctl = m->m_len | SK_OPCODE_DEFAULT;
			if (cnt == 0)
				f->sk_ctl |= SK_TXCTL_FIRSTFRAG;
			else
				f->sk_ctl |= SK_TXCTL_OWN;
			cur = frag;
			SK_INC(frag, SK_TX_RING_CNT);
			cnt++;
		}
	}

	if (m != NULL)
		return(ENOBUFS);

	sc_if->sk_rdata->sk_tx_ring[cur].sk_ctl |=
		SK_TXCTL_LASTFRAG|SK_TXCTL_EOF_INTR;
	sc_if->sk_cdata.sk_tx_chain[cur].sk_mbuf = m_head;
	sc_if->sk_rdata->sk_tx_ring[*txidx].sk_ctl |= SK_TXCTL_OWN;
	sc_if->sk_cdata.sk_tx_cnt += cnt;

	*txidx = frag;

	return(0);
}

static void sk_start(ifp)
	struct ifnet		*ifp;
{
        struct sk_softc		*sc;
        struct sk_if_softc	*sc_if;
        struct mbuf		*m_head = NULL;
        u_int32_t		idx;

	sc_if = ifp->if_softc;
	sc = sc_if->sk_softc;

	idx = sc_if->sk_cdata.sk_tx_prod;

	while(sc_if->sk_cdata.sk_tx_chain[idx].sk_mbuf == NULL) {
		IF_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (sk_encap(sc_if, m_head, &idx)) {
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
	if (idx != sc_if->sk_cdata.sk_tx_prod) {
		sc_if->sk_cdata.sk_tx_prod = idx;
		CSR_WRITE_4(sc, sc_if->sk_tx_bmu, SK_TXBMU_TX_START);

		/* Set a timeout in case the chip goes out to lunch. */
		ifp->if_timer = 5;
	}

	return;
}


static void sk_watchdog(ifp)
	struct ifnet		*ifp;
{
	struct sk_if_softc	*sc_if;

	sc_if = ifp->if_softc;

	printf("sk%d: watchdog timeout\n", sc_if->sk_unit);
	sk_init(sc_if);

	return;
}

static void skc_shutdown(dev)
	device_t		dev;
{
	struct sk_softc		*sc;

	sc = device_get_softc(dev);

	/* Turn off the 'driver is loaded' LED. */
	CSR_WRITE_2(sc, SK_LED, SK_LED_GREEN_OFF);

	/*
	 * Reset the GEnesis controller. Doing this should also
	 * assert the resets on the attached XMAC(s).
	 */
	sk_reset(sc);

	return;
}

static void sk_rxeof(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct ether_header	*eh;
	struct mbuf		*m;
	struct ifnet		*ifp;
	struct sk_chain		*cur_rx;
	int			total_len = 0;
	int			i;
	u_int32_t		rxstat;

	ifp = &sc_if->arpcom.ac_if;
	i = sc_if->sk_cdata.sk_rx_prod;
	cur_rx = &sc_if->sk_cdata.sk_rx_chain[i];

	while(!(sc_if->sk_rdata->sk_rx_ring[i].sk_ctl & SK_RXCTL_OWN)) {

		cur_rx = &sc_if->sk_cdata.sk_rx_chain[i];
		rxstat = sc_if->sk_rdata->sk_rx_ring[i].sk_xmac_rxstat;
		m = cur_rx->sk_mbuf;
		cur_rx->sk_mbuf = NULL;
		total_len = SK_RXBYTES(sc_if->sk_rdata->sk_rx_ring[i].sk_ctl);
		SK_INC(i, SK_RX_RING_CNT);

		if (rxstat & XM_RXSTAT_ERRFRAME) {
			ifp->if_ierrors++;
			sk_newbuf(sc_if, cur_rx, m);
			continue;
		}

		/*
		 * Try to allocate a new jumbo buffer. If that
		 * fails, copy the packet to mbufs and put the
		 * jumbo buffer back in the ring so it can be
		 * re-used. If allocating mbufs fails, then we
		 * have to drop the packet.
		 */
		if (sk_newbuf(sc_if, cur_rx, NULL) == ENOBUFS) {
			struct mbuf		*m0;
			m0 = m_devget(mtod(m, char *) - ETHER_ALIGN,
			    total_len + ETHER_ALIGN, 0, ifp, NULL);
			sk_newbuf(sc_if, cur_rx, m);
			if (m0 == NULL) {
				printf("sk%d: no receive buffers "
				    "available -- packet dropped!\n",
				    sc_if->sk_unit);
				ifp->if_ierrors++;
				continue;
			}
			m_adj(m0, ETHER_ALIGN);
			m = m0;
		} else {
			m->m_pkthdr.rcvif = ifp;
			m->m_pkthdr.len = m->m_len = total_len;
		}

		ifp->if_ipackets++;
		eh = mtod(m, struct ether_header *);

		/* Remove header from mbuf and pass it on. */
		m_adj(m, sizeof(struct ether_header));
		ether_input(ifp, eh, m);
	}

	sc_if->sk_cdata.sk_rx_prod = i;

	return;
}

static void sk_txeof(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_softc		*sc;
	struct sk_tx_desc	*cur_tx;
	struct ifnet		*ifp;
	u_int32_t		idx;

	sc = sc_if->sk_softc;
	ifp = &sc_if->arpcom.ac_if;

	/*
	 * Go through our tx ring and free mbufs for those
	 * frames that have been sent.
	 */
	idx = sc_if->sk_cdata.sk_tx_cons;
	while(idx != sc_if->sk_cdata.sk_tx_prod) {
		cur_tx = &sc_if->sk_rdata->sk_tx_ring[idx];
		if (cur_tx->sk_ctl & SK_TXCTL_OWN)
			break;
		if (cur_tx->sk_ctl & SK_TXCTL_LASTFRAG)
			ifp->if_opackets++;
		if (sc_if->sk_cdata.sk_tx_chain[idx].sk_mbuf != NULL) {
			m_freem(sc_if->sk_cdata.sk_tx_chain[idx].sk_mbuf);
			sc_if->sk_cdata.sk_tx_chain[idx].sk_mbuf = NULL;
		}
		sc_if->sk_cdata.sk_tx_cnt--;
		SK_INC(idx, SK_TX_RING_CNT);
	}

	if (sc_if->sk_cdata.sk_tx_cnt == 0)
		ifp->if_timer = 0;
	else /* nudge chip to keep tx ring moving */
		CSR_WRITE_4(sc, sc_if->sk_tx_bmu, SK_TXBMU_TX_START);
 
	if (sc_if->sk_cdata.sk_tx_cnt < SK_TX_RING_CNT - 2)
		ifp->if_flags &= ~IFF_OACTIVE;

	sc_if->sk_cdata.sk_tx_cons = idx;
}

static void sk_tick(xsc_if)
	void			*xsc_if;
{
	struct sk_if_softc	*sc_if;
	struct mii_data		*mii;
	struct ifnet		*ifp;
	int			i;

	sc_if = xsc_if;
	ifp = &sc_if->arpcom.ac_if;
	mii = device_get_softc(sc_if->sk_miibus);

	if (!(ifp->if_flags & IFF_UP))
		return;

	if (sc_if->sk_phytype == SK_PHYTYPE_BCOM) {
		sk_intr_bcom(sc_if);
		return;
	}

	/*
	 * According to SysKonnect, the correct way to verify that
	 * the link has come back up is to poll bit 0 of the GPIO
	 * register three times. This pin has the signal from the
	 * link_sync pin connected to it; if we read the same link
	 * state 3 times in a row, we know the link is up.
	 */
	for (i = 0; i < 3; i++) {
		if (SK_XM_READ_2(sc_if, XM_GPIO) & XM_GPIO_GP0_SET)
			break;
	}

	if (i != 3) {
		sc_if->sk_tick_ch = timeout(sk_tick, sc_if, hz);
		return;
	}

	/* Turn the GP0 interrupt back on. */
	SK_XM_CLRBIT_2(sc_if, XM_IMR, XM_IMR_GP0_SET);
	SK_XM_READ_2(sc_if, XM_ISR);
	mii_tick(mii);
	mii_pollstat(mii);
	untimeout(sk_tick, sc_if, sc_if->sk_tick_ch);

	return;
}

static void sk_intr_bcom(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_softc		*sc;
	struct mii_data		*mii;
	struct ifnet		*ifp;
	int			status;

	sc = sc_if->sk_softc;
	mii = device_get_softc(sc_if->sk_miibus);
	ifp = &sc_if->arpcom.ac_if;

	SK_XM_CLRBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_TX_ENB|XM_MMUCMD_RX_ENB);

	/*
	 * Read the PHY interrupt register to make sure
	 * we clear any pending interrupts.
	 */
	status = sk_xmac_miibus_readreg(sc_if, SK_PHYADDR_BCOM, BRGPHY_MII_ISR);

	if (!(ifp->if_flags & IFF_RUNNING)) {
		sk_init_xmac(sc_if);
		return;
	}

	if (status & (BRGPHY_ISR_LNK_CHG|BRGPHY_ISR_AN_PR)) {
		int			lstat;
		lstat = sk_xmac_miibus_readreg(sc_if, SK_PHYADDR_BCOM,
		    BRGPHY_MII_AUXSTS);

		if (!(lstat & BRGPHY_AUXSTS_LINK) && sc_if->sk_link) {
			mii_mediachg(mii);
			/* Turn off the link LED. */
			SK_IF_WRITE_1(sc_if, 0,
			    SK_LINKLED1_CTL, SK_LINKLED_OFF);
			sc_if->sk_link = 0;
		} else if (status & BRGPHY_ISR_LNK_CHG) {
			sk_xmac_miibus_writereg(sc_if, SK_PHYADDR_BCOM,
	    		    BRGPHY_MII_IMR, 0xFF00);
			mii_tick(mii);
			sc_if->sk_link = 1;
			/* Turn on the link LED. */
			SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL,
			    SK_LINKLED_ON|SK_LINKLED_LINKSYNC_OFF|
			    SK_LINKLED_BLINK_OFF);
			mii_pollstat(mii);
		} else {
			mii_tick(mii);
			sc_if->sk_tick_ch = timeout(sk_tick, sc_if, hz);
		}
	}

	SK_XM_SETBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_TX_ENB|XM_MMUCMD_RX_ENB);

	return;
}

static void sk_intr_xmac(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_softc		*sc;
	u_int16_t		status;
	struct mii_data		*mii;

	sc = sc_if->sk_softc;
	mii = device_get_softc(sc_if->sk_miibus);
	status = SK_XM_READ_2(sc_if, XM_ISR);

	/*
	 * Link has gone down. Start MII tick timeout to
	 * watch for link resync.
	 */
	if (sc_if->sk_phytype == SK_PHYTYPE_XMAC) {
		if (status & XM_ISR_GP0_SET) {
			SK_XM_SETBIT_2(sc_if, XM_IMR, XM_IMR_GP0_SET);
			sc_if->sk_tick_ch = timeout(sk_tick, sc_if, hz);
		}

		if (status & XM_ISR_AUTONEG_DONE) {
			sc_if->sk_tick_ch = timeout(sk_tick, sc_if, hz);
		}
	}

	if (status & XM_IMR_TX_UNDERRUN)
		SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_FLUSH_TXFIFO);

	if (status & XM_IMR_RX_OVERRUN)
		SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_FLUSH_RXFIFO);

	status = SK_XM_READ_2(sc_if, XM_ISR);

	return;
}

static void sk_intr_yukon(sc_if)
	struct sk_if_softc	*sc_if;
{
	int status;

	status = SK_IF_READ_2(sc_if, 0, SK_GMAC_ISR);

	return;
}

static void sk_intr(xsc)
	void			*xsc;
{
	struct sk_softc		*sc = xsc;
	struct sk_if_softc	*sc_if0 = NULL, *sc_if1 = NULL;
	struct ifnet		*ifp0 = NULL, *ifp1 = NULL;
	u_int32_t		status;

	sc_if0 = sc->sk_if[SK_PORT_A];
	sc_if1 = sc->sk_if[SK_PORT_B];

	if (sc_if0 != NULL)
		ifp0 = &sc_if0->arpcom.ac_if;
	if (sc_if1 != NULL)
		ifp1 = &sc_if1->arpcom.ac_if;

	for (;;) {
		status = CSR_READ_4(sc, SK_ISSR);
		if (!(status & sc->sk_intrmask))
			break;

		/* Handle receive interrupts first. */
		if (status & SK_ISR_RX1_EOF) {
			sk_rxeof(sc_if0);
			CSR_WRITE_4(sc, SK_BMU_RX_CSR0,
			    SK_RXBMU_CLR_IRQ_EOF|SK_RXBMU_RX_START);
		}
		if (status & SK_ISR_RX2_EOF) {
			sk_rxeof(sc_if1);
			CSR_WRITE_4(sc, SK_BMU_RX_CSR1,
			    SK_RXBMU_CLR_IRQ_EOF|SK_RXBMU_RX_START);
		}

		/* Then transmit interrupts. */
		if (status & SK_ISR_TX1_S_EOF) {
			sk_txeof(sc_if0);
			CSR_WRITE_4(sc, SK_BMU_TXS_CSR0,
			    SK_TXBMU_CLR_IRQ_EOF);
		}
		if (status & SK_ISR_TX2_S_EOF) {
			sk_txeof(sc_if1);
			CSR_WRITE_4(sc, SK_BMU_TXS_CSR1,
			    SK_TXBMU_CLR_IRQ_EOF);
		}

		/* Then MAC interrupts. */
		if (status & SK_ISR_MAC1 && ifp0->if_flags & IFF_RUNNING) {
			if (sc->sk_type == SK_GENESIS)
				sk_intr_xmac(sc_if0);
			else
				sk_intr_yukon(sc_if0);
		}

		if (status & SK_ISR_MAC2 && ifp1->if_flags & IFF_RUNNING) {
			if (sc->sk_type == SK_GENESIS)
				sk_intr_xmac(sc_if1);
			else
				sk_intr_yukon(sc_if0);
		}

		if (status & SK_ISR_EXTERNAL_REG) {
			if (ifp0 != NULL &&
			    sc_if0->sk_phytype == SK_PHYTYPE_BCOM)
				sk_intr_bcom(sc_if0);
			if (ifp1 != NULL &&
			    sc_if1->sk_phytype == SK_PHYTYPE_BCOM)
				sk_intr_bcom(sc_if1);
		}
	}

	CSR_WRITE_4(sc, SK_IMR, sc->sk_intrmask);

	if (ifp0 != NULL && ifp0->if_snd.ifq_head != NULL)
		sk_start(ifp0);
	if (ifp1 != NULL && ifp1->if_snd.ifq_head != NULL)
		sk_start(ifp1);

	return;
}

static void sk_init_xmac(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_softc		*sc;
	struct ifnet		*ifp;
	struct sk_bcom_hack	bhack[] = {
	{ 0x18, 0x0c20 }, { 0x17, 0x0012 }, { 0x15, 0x1104 }, { 0x17, 0x0013 },
	{ 0x15, 0x0404 }, { 0x17, 0x8006 }, { 0x15, 0x0132 }, { 0x17, 0x8006 },
	{ 0x15, 0x0232 }, { 0x17, 0x800D }, { 0x15, 0x000F }, { 0x18, 0x0420 },
	{ 0, 0 } };

	sc = sc_if->sk_softc;
	ifp = &sc_if->arpcom.ac_if;

	/* Unreset the XMAC. */
	SK_IF_WRITE_2(sc_if, 0, SK_TXF1_MACCTL, SK_TXMACCTL_XMAC_UNRESET);
	DELAY(1000);

	/* Reset the XMAC's internal state. */
	SK_XM_SETBIT_2(sc_if, XM_GPIO, XM_GPIO_RESETMAC);

	/* Save the XMAC II revision */
	sc_if->sk_xmac_rev = XM_XMAC_REV(SK_XM_READ_4(sc_if, XM_DEVID));

	/*
	 * Perform additional initialization for external PHYs,
	 * namely for the 1000baseTX cards that use the XMAC's
	 * GMII mode.
	 */
	if (sc_if->sk_phytype == SK_PHYTYPE_BCOM) {
		int			i = 0;
		u_int32_t		val;

		/* Take PHY out of reset. */
		val = sk_win_read_4(sc, SK_GPIO);
		if (sc_if->sk_port == SK_PORT_A)
			val |= SK_GPIO_DIR0|SK_GPIO_DAT0;
		else
			val |= SK_GPIO_DIR2|SK_GPIO_DAT2;
		sk_win_write_4(sc, SK_GPIO, val);

		/* Enable GMII mode on the XMAC. */
		SK_XM_SETBIT_2(sc_if, XM_HWCFG, XM_HWCFG_GMIIMODE);

		sk_xmac_miibus_writereg(sc_if, SK_PHYADDR_BCOM,
		    BRGPHY_MII_BMCR, BRGPHY_BMCR_RESET);
		DELAY(10000);
		sk_xmac_miibus_writereg(sc_if, SK_PHYADDR_BCOM,
		    BRGPHY_MII_IMR, 0xFFF0);

		/*
		 * Early versions of the BCM5400 apparently have
		 * a bug that requires them to have their reserved
		 * registers initialized to some magic values. I don't
		 * know what the numbers do, I'm just the messenger.
		 */
		if (sk_xmac_miibus_readreg(sc_if, SK_PHYADDR_BCOM, 0x03)
		    == 0x6041) {
			while(bhack[i].reg) {
				sk_xmac_miibus_writereg(sc_if, SK_PHYADDR_BCOM,
				    bhack[i].reg, bhack[i].val);
				i++;
			}
		}
	}

	/* Set station address */
	SK_XM_WRITE_2(sc_if, XM_PAR0,
	    *(u_int16_t *)(&sc_if->arpcom.ac_enaddr[0]));
	SK_XM_WRITE_2(sc_if, XM_PAR1,
	    *(u_int16_t *)(&sc_if->arpcom.ac_enaddr[2]));
	SK_XM_WRITE_2(sc_if, XM_PAR2,
	    *(u_int16_t *)(&sc_if->arpcom.ac_enaddr[4]));
	SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_RX_USE_STATION);

	if (ifp->if_flags & IFF_PROMISC) {
		SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_RX_PROMISC);
	} else {
		SK_XM_CLRBIT_4(sc_if, XM_MODE, XM_MODE_RX_PROMISC);
	}

	if (ifp->if_flags & IFF_BROADCAST) {
		SK_XM_CLRBIT_4(sc_if, XM_MODE, XM_MODE_RX_NOBROAD);
	} else {
		SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_RX_NOBROAD);
	}

	/* We don't need the FCS appended to the packet. */
	SK_XM_SETBIT_2(sc_if, XM_RXCMD, XM_RXCMD_STRIPFCS);

	/* We want short frames padded to 60 bytes. */
	SK_XM_SETBIT_2(sc_if, XM_TXCMD, XM_TXCMD_AUTOPAD);

	/*
	 * Enable the reception of all error frames. This is is
	 * a necessary evil due to the design of the XMAC. The
	 * XMAC's receive FIFO is only 8K in size, however jumbo
	 * frames can be up to 9000 bytes in length. When bad
	 * frame filtering is enabled, the XMAC's RX FIFO operates
	 * in 'store and forward' mode. For this to work, the
	 * entire frame has to fit into the FIFO, but that means
	 * that jumbo frames larger than 8192 bytes will be
	 * truncated. Disabling all bad frame filtering causes
	 * the RX FIFO to operate in streaming mode, in which
	 * case the XMAC will start transfering frames out of the
	 * RX FIFO as soon as the FIFO threshold is reached.
	 */
	SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_RX_BADFRAMES|
	    XM_MODE_RX_GIANTS|XM_MODE_RX_RUNTS|XM_MODE_RX_CRCERRS|
	    XM_MODE_RX_INRANGELEN);

	if (ifp->if_mtu > (ETHERMTU + ETHER_HDR_LEN + ETHER_CRC_LEN))
		SK_XM_SETBIT_2(sc_if, XM_RXCMD, XM_RXCMD_BIGPKTOK);
	else
		SK_XM_CLRBIT_2(sc_if, XM_RXCMD, XM_RXCMD_BIGPKTOK);

	/*
	 * Bump up the transmit threshold. This helps hold off transmit
	 * underruns when we're blasting traffic from both ports at once.
	 */
	SK_XM_WRITE_2(sc_if, XM_TX_REQTHRESH, SK_XM_TX_FIFOTHRESH);

	/* Set multicast filter */
	sk_setmulti(sc_if);

	/* Clear and enable interrupts */
	SK_XM_READ_2(sc_if, XM_ISR);
	if (sc_if->sk_phytype == SK_PHYTYPE_XMAC)
		SK_XM_WRITE_2(sc_if, XM_IMR, XM_INTRS);
	else
		SK_XM_WRITE_2(sc_if, XM_IMR, 0xFFFF);

	/* Configure MAC arbiter */
	switch(sc_if->sk_xmac_rev) {
	case XM_XMAC_REV_B2:
		sk_win_write_1(sc, SK_RCINIT_RX1, SK_RCINIT_XMAC_B2);
		sk_win_write_1(sc, SK_RCINIT_TX1, SK_RCINIT_XMAC_B2);
		sk_win_write_1(sc, SK_RCINIT_RX2, SK_RCINIT_XMAC_B2);
		sk_win_write_1(sc, SK_RCINIT_TX2, SK_RCINIT_XMAC_B2);
		sk_win_write_1(sc, SK_MINIT_RX1, SK_MINIT_XMAC_B2);
		sk_win_write_1(sc, SK_MINIT_TX1, SK_MINIT_XMAC_B2);
		sk_win_write_1(sc, SK_MINIT_RX2, SK_MINIT_XMAC_B2);
		sk_win_write_1(sc, SK_MINIT_TX2, SK_MINIT_XMAC_B2);
		sk_win_write_1(sc, SK_RECOVERY_CTL, SK_RECOVERY_XMAC_B2);
		break;
	case XM_XMAC_REV_C1:
		sk_win_write_1(sc, SK_RCINIT_RX1, SK_RCINIT_XMAC_C1);
		sk_win_write_1(sc, SK_RCINIT_TX1, SK_RCINIT_XMAC_C1);
		sk_win_write_1(sc, SK_RCINIT_RX2, SK_RCINIT_XMAC_C1);
		sk_win_write_1(sc, SK_RCINIT_TX2, SK_RCINIT_XMAC_C1);
		sk_win_write_1(sc, SK_MINIT_RX1, SK_MINIT_XMAC_C1);
		sk_win_write_1(sc, SK_MINIT_TX1, SK_MINIT_XMAC_C1);
		sk_win_write_1(sc, SK_MINIT_RX2, SK_MINIT_XMAC_C1);
		sk_win_write_1(sc, SK_MINIT_TX2, SK_MINIT_XMAC_C1);
		sk_win_write_1(sc, SK_RECOVERY_CTL, SK_RECOVERY_XMAC_B2);
		break;
	default:
		break;
	}
	sk_win_write_2(sc, SK_MACARB_CTL,
	    SK_MACARBCTL_UNRESET|SK_MACARBCTL_FASTOE_OFF);

	sc_if->sk_link = 1;

	return;
}

static void sk_init_yukon(sc_if)
	struct sk_if_softc	*sc_if;
{
	u_int32_t		phy;
	u_int16_t		reg;
	struct sk_softc		*sc;
	struct ifnet		*ifp;
	int			i;

	sc = sc_if->sk_softc;
	ifp = &sc_if->arpcom.ac_if;

	/* GMAC and GPHY Reset */
	SK_IF_WRITE_4(sc_if, 0, SK_GPHY_CTRL, SK_GPHY_RESET_SET);
	SK_IF_WRITE_4(sc_if, 0, SK_GMAC_CTRL, SK_GMAC_RESET_SET);
	DELAY(1000);
	SK_IF_WRITE_4(sc_if, 0, SK_GMAC_CTRL, SK_GMAC_RESET_CLEAR);
	SK_IF_WRITE_4(sc_if, 0, SK_GMAC_CTRL, SK_GMAC_RESET_SET);
	DELAY(1000);

	phy = SK_GPHY_INT_POL_HI | SK_GPHY_DIS_FC | SK_GPHY_DIS_SLEEP |
		SK_GPHY_ENA_XC | SK_GPHY_ANEG_ALL | SK_GPHY_ENA_PAUSE;

	switch(sc_if->sk_softc->sk_pmd) {
	case IFM_1000_SX:
	case IFM_1000_LX:
		phy |= SK_GPHY_FIBER;
		break;

	case IFM_1000_CX:
	case IFM_1000_TX:
		phy |= SK_GPHY_COPPER;
		break;
	}

	SK_IF_WRITE_4(sc_if, 0, SK_GPHY_CTRL, phy | SK_GPHY_RESET_SET);
	DELAY(1000);
	SK_IF_WRITE_4(sc_if, 0, SK_GPHY_CTRL, phy | SK_GPHY_RESET_CLEAR);
	SK_IF_WRITE_4(sc_if, 0, SK_GMAC_CTRL, SK_GMAC_LOOP_OFF |
		      SK_GMAC_PAUSE_ON | SK_GMAC_RESET_CLEAR);

	/* unused read of the interrupt source register */
	SK_IF_READ_2(sc_if, 0, SK_GMAC_ISR);

	reg = SK_YU_READ_2(sc_if, YUKON_PAR);

	/* MIB Counter Clear Mode set */
	reg |= YU_PAR_MIB_CLR;
	SK_YU_WRITE_2(sc_if, YUKON_PAR, reg);

	/* MIB Counter Clear Mode clear */
	reg &= ~YU_PAR_MIB_CLR;
	SK_YU_WRITE_2(sc_if, YUKON_PAR, reg);

	/* receive control reg */
	SK_YU_WRITE_2(sc_if, YUKON_RCR, YU_RCR_UFLEN | YU_RCR_MUFLEN |
		      YU_RCR_CRCR);

	/* transmit parameter register */
	SK_YU_WRITE_2(sc_if, YUKON_TPR, YU_TPR_JAM_LEN(0x3) |
		      YU_TPR_JAM_IPG(0xb) | YU_TPR_JAM2DATA_IPG(0x1a) );

	/* serial mode register */
	reg = YU_SMR_DATA_BLIND(0x1c) | YU_SMR_MFL_VLAN | YU_SMR_IPG_DATA(0x1e);
	if (ifp->if_mtu > (ETHERMTU + ETHER_HDR_LEN + ETHER_CRC_LEN))
		reg |= YU_SMR_MFL_JUMBO;
	SK_YU_WRITE_2(sc_if, YUKON_SMR, reg);

	/* Setup Yukon's address */
	for (i = 0; i < 3; i++) {
		/* Write Source Address 1 (unicast filter) */
		SK_YU_WRITE_2(sc_if, YUKON_SAL1 + i * 4, 
			      sc_if->arpcom.ac_enaddr[i * 2] |
			      sc_if->arpcom.ac_enaddr[i * 2 + 1] << 8);
	}

	for (i = 0; i < 3; i++) {
		reg = sk_win_read_2(sc_if->sk_softc,
				    SK_MAC1_0 + i * 2 + sc_if->sk_port * 8);
		SK_YU_WRITE_2(sc_if, YUKON_SAL2 + i * 4, reg);
	}

	/* clear all Multicast filter hash registers */
	SK_YU_WRITE_2(sc_if, YUKON_MCAH1, 0);
	SK_YU_WRITE_2(sc_if, YUKON_MCAH2, 0);
	SK_YU_WRITE_2(sc_if, YUKON_MCAH3, 0);
	SK_YU_WRITE_2(sc_if, YUKON_MCAH4, 0);

	/* enable interrupt mask for counter overflows */
	SK_YU_WRITE_2(sc_if, YUKON_TIMR, 0);
	SK_YU_WRITE_2(sc_if, YUKON_RIMR, 0);
	SK_YU_WRITE_2(sc_if, YUKON_TRIMR, 0);

	/* Configure RX MAC FIFO */
	SK_IF_WRITE_1(sc_if, 0, SK_RXMF1_CTRL_TEST, SK_RFCTL_RESET_CLEAR);
	SK_IF_WRITE_4(sc_if, 0, SK_RXMF1_CTRL_TEST, SK_RFCTL_OPERATION_ON);

	/* Configure TX MAC FIFO */
	SK_IF_WRITE_1(sc_if, 0, SK_TXMF1_CTRL_TEST, SK_TFCTL_RESET_CLEAR);
	SK_IF_WRITE_4(sc_if, 0, SK_TXMF1_CTRL_TEST, SK_TFCTL_OPERATION_ON);
}

/*
 * Note that to properly initialize any part of the GEnesis chip,
 * you first have to take it out of reset mode.
 */
static void sk_init(xsc)
	void			*xsc;
{
	struct sk_if_softc	*sc_if = xsc;
	struct sk_softc		*sc;
	struct ifnet		*ifp;
	struct mii_data		*mii;
	int			s;
	u_int16_t		reg;

	s = splimp();

	ifp = &sc_if->arpcom.ac_if;
	sc = sc_if->sk_softc;
	mii = device_get_softc(sc_if->sk_miibus);

	/* Cancel pending I/O and free all RX/TX buffers. */
	sk_stop(sc_if);

	if (sc->sk_type == SK_GENESIS) {
		/* Configure LINK_SYNC LED */
		SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL, SK_LINKLED_ON);
		SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL,
			SK_LINKLED_LINKSYNC_ON);

		/* Configure RX LED */
		SK_IF_WRITE_1(sc_if, 0, SK_RXLED1_CTL,	
			SK_RXLEDCTL_COUNTER_START);

		/* Configure TX LED */
		SK_IF_WRITE_1(sc_if, 0, SK_TXLED1_CTL,
			SK_TXLEDCTL_COUNTER_START);
	}

	/* Configure I2C registers */

	/* Configure XMAC(s) */
	switch (sc->sk_type) {
	case SK_GENESIS:
		sk_init_xmac(sc_if);
		break;
	case SK_YUKON:
		sk_init_yukon(sc_if);
		break;
	}
	mii_mediachg(mii);

	if (sc->sk_type == SK_GENESIS) {
		/* Configure MAC FIFOs */
		SK_IF_WRITE_4(sc_if, 0, SK_RXF1_CTL, SK_FIFO_UNRESET);
		SK_IF_WRITE_4(sc_if, 0, SK_RXF1_END, SK_FIFO_END);
		SK_IF_WRITE_4(sc_if, 0, SK_RXF1_CTL, SK_FIFO_ON);

		SK_IF_WRITE_4(sc_if, 0, SK_TXF1_CTL, SK_FIFO_UNRESET);
		SK_IF_WRITE_4(sc_if, 0, SK_TXF1_END, SK_FIFO_END);
		SK_IF_WRITE_4(sc_if, 0, SK_TXF1_CTL, SK_FIFO_ON);
	}

	/* Configure transmit arbiter(s) */
	SK_IF_WRITE_1(sc_if, 0, SK_TXAR1_COUNTERCTL,
	    SK_TXARCTL_ON|SK_TXARCTL_FSYNC_ON);

	/* Configure RAMbuffers */
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_CTLTST, SK_RBCTL_UNRESET);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_START, sc_if->sk_rx_ramstart);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_WR_PTR, sc_if->sk_rx_ramstart);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_RD_PTR, sc_if->sk_rx_ramstart);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_END, sc_if->sk_rx_ramend);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_CTLTST, SK_RBCTL_ON);

	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_CTLTST, SK_RBCTL_UNRESET);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_CTLTST, SK_RBCTL_STORENFWD_ON);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_START, sc_if->sk_tx_ramstart);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_WR_PTR, sc_if->sk_tx_ramstart);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_RD_PTR, sc_if->sk_tx_ramstart);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_END, sc_if->sk_tx_ramend);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_CTLTST, SK_RBCTL_ON);

	/* Configure BMUs */
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, SK_RXBMU_ONLINE);
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_CURADDR_LO,
	    vtophys(&sc_if->sk_rdata->sk_rx_ring[0]));
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_CURADDR_HI, 0);

	SK_IF_WRITE_4(sc_if, 1, SK_TXQS1_BMU_CSR, SK_TXBMU_ONLINE);
	SK_IF_WRITE_4(sc_if, 1, SK_TXQS1_CURADDR_LO,
	    vtophys(&sc_if->sk_rdata->sk_tx_ring[0]));
	SK_IF_WRITE_4(sc_if, 1, SK_TXQS1_CURADDR_HI, 0);

	/* Init descriptors */
	if (sk_init_rx_ring(sc_if) == ENOBUFS) {
		printf("sk%d: initialization failed: no "
		    "memory for rx buffers\n", sc_if->sk_unit);
		sk_stop(sc_if);
		(void)splx(s);
		return;
	}
	sk_init_tx_ring(sc_if);

	/* Configure interrupt handling */
	CSR_READ_4(sc, SK_ISSR);
	if (sc_if->sk_port == SK_PORT_A)
		sc->sk_intrmask |= SK_INTRS1;
	else
		sc->sk_intrmask |= SK_INTRS2;

	sc->sk_intrmask |= SK_ISR_EXTERNAL_REG;

	CSR_WRITE_4(sc, SK_IMR, sc->sk_intrmask);

	/* Start BMUs. */
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, SK_RXBMU_RX_START);

	switch(sc->sk_type) {
	case SK_GENESIS:
		/* Enable XMACs TX and RX state machines */
		SK_XM_CLRBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_IGNPAUSE);
		SK_XM_SETBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_TX_ENB|XM_MMUCMD_RX_ENB);
		break;
	case SK_YUKON:
		reg = SK_YU_READ_2(sc_if, YUKON_GPCR);
		reg |= YU_GPCR_TXEN | YU_GPCR_RXEN;
		reg &= ~(YU_GPCR_SPEED_EN | YU_GPCR_DPLX_EN);
		SK_YU_WRITE_2(sc_if, YUKON_GPCR, reg);
	}

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	splx(s);

	return;
}

static void sk_stop(sc_if)
	struct sk_if_softc	*sc_if;
{
	int			i;
	struct sk_softc		*sc;
	struct ifnet		*ifp;

	sc = sc_if->sk_softc;
	ifp = &sc_if->arpcom.ac_if;

	untimeout(sk_tick, sc_if, sc_if->sk_tick_ch);

	if (sc_if->sk_phytype == SK_PHYTYPE_BCOM) {
		u_int32_t		val;

		/* Put PHY back into reset. */
		val = sk_win_read_4(sc, SK_GPIO);
		if (sc_if->sk_port == SK_PORT_A) {
			val |= SK_GPIO_DIR0;
			val &= ~SK_GPIO_DAT0;
		} else {
			val |= SK_GPIO_DIR2;
			val &= ~SK_GPIO_DAT2;
		}
		sk_win_write_4(sc, SK_GPIO, val);
	}

	/* Turn off various components of this interface. */
	SK_XM_SETBIT_2(sc_if, XM_GPIO, XM_GPIO_RESETMAC);
	switch (sc->sk_type) {
	case SK_GENESIS:
		SK_IF_WRITE_2(sc_if, 0, SK_TXF1_MACCTL, SK_TXMACCTL_XMAC_RESET);
		SK_IF_WRITE_4(sc_if, 0, SK_RXF1_CTL, SK_FIFO_RESET);
		break;
	case SK_YUKON:
		SK_IF_WRITE_1(sc_if,0, SK_RXMF1_CTRL_TEST, SK_RFCTL_RESET_SET);
		SK_IF_WRITE_1(sc_if,0, SK_TXMF1_CTRL_TEST, SK_TFCTL_RESET_SET);
		break;
	}
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, SK_RXBMU_OFFLINE);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_CTLTST, SK_RBCTL_RESET|SK_RBCTL_OFF);
	SK_IF_WRITE_4(sc_if, 1, SK_TXQS1_BMU_CSR, SK_TXBMU_OFFLINE);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_CTLTST, SK_RBCTL_RESET|SK_RBCTL_OFF);
	SK_IF_WRITE_1(sc_if, 0, SK_TXAR1_COUNTERCTL, SK_TXARCTL_OFF);
	SK_IF_WRITE_1(sc_if, 0, SK_RXLED1_CTL, SK_RXLEDCTL_COUNTER_STOP);
	SK_IF_WRITE_1(sc_if, 0, SK_TXLED1_CTL, SK_RXLEDCTL_COUNTER_STOP);
	SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL, SK_LINKLED_OFF);
	SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL, SK_LINKLED_LINKSYNC_OFF);

	/* Disable interrupts */
	if (sc_if->sk_port == SK_PORT_A)
		sc->sk_intrmask &= ~SK_INTRS1;
	else
		sc->sk_intrmask &= ~SK_INTRS2;
	CSR_WRITE_4(sc, SK_IMR, sc->sk_intrmask);

	SK_XM_READ_2(sc_if, XM_ISR);
	SK_XM_WRITE_2(sc_if, XM_IMR, 0xFFFF);

	/* Free RX and TX mbufs still in the queues. */
	for (i = 0; i < SK_RX_RING_CNT; i++) {
		if (sc_if->sk_cdata.sk_rx_chain[i].sk_mbuf != NULL) {
			m_freem(sc_if->sk_cdata.sk_rx_chain[i].sk_mbuf);
			sc_if->sk_cdata.sk_rx_chain[i].sk_mbuf = NULL;
		}
	}

	for (i = 0; i < SK_TX_RING_CNT; i++) {
		if (sc_if->sk_cdata.sk_tx_chain[i].sk_mbuf != NULL) {
			m_freem(sc_if->sk_cdata.sk_tx_chain[i].sk_mbuf);
			sc_if->sk_cdata.sk_tx_chain[i].sk_mbuf = NULL;
		}
	}

	ifp->if_flags &= ~(IFF_RUNNING|IFF_OACTIVE);

	return;
}
