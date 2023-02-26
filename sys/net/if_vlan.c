/*
 * Copyright 1998 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 * 
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/net/if_vlan.c,v 1.15.2.14 2004/09/09 16:57:33 yar Exp $
 */

/*
 * if_vlan.c - pseudo-device driver for IEEE 802.1Q virtual LANs.
 * Might be extended some day to also handle IEEE 802.1p priority
 * tagging.  This is sort of sneaky in the implementation, since
 * we need to pretend to be enough of an Ethernet implementation
 * to make arp work.  The way we do this is by telling everyone
 * that we are an Ethernet, and then catch the packets that
 * ether_output() left on our output queue queue when it calls
 * if_start(), rewrite them for use by the real outgoing interface,
 * and ask it to send them.
 *
 *
 * XXX It's incorrect to assume that we must always kludge up
 * headers on the physical device's behalf: some devices support
 * VLAN tag insertion and extraction in firmware. For these cases,
 * one can change the behavior of the vlan interface by setting
 * the LINK0 flag on it (that is setting the vlan interface's LINK0
 * flag, _not_ the parent's LINK0 flag; we try to leave the parent
 * alone). If the interface has the LINK0 flag set, then it will
 * not modify the ethernet header on output, because the parent
 * can do that for itself. On input, the parent can call vlan_input_tag()
 * directly in order to supply us with an incoming mbuf and the vlan
 * tag value that goes with it.
 */

#ifndef NVLAN
#include "vlan.h"
#endif
#include "opt_inet.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <machine/bus.h>	/* XXX: Shouldn't really be required! */
#include <sys/rman.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#define VLANNAME	"vlan"
#define VLAN_MAXUNIT	0x7fff	/* ifp->if_unit is only 15 bits */

SYSCTL_DECL(_net_link);
SYSCTL_NODE(_net_link, IFT_L2VLAN, vlan, CTLFLAG_RW, 0, "IEEE 802.1Q VLAN");
SYSCTL_NODE(_net_link_vlan, PF_LINK, link, CTLFLAG_RW, 0, "for consistency");

static MALLOC_DEFINE(M_VLAN, "vlan", "802.1Q Virtual LAN Interface");
static struct rman vlanunits[1];
static LIST_HEAD(, ifvlan) ifv_list;

static	int vlan_clone_create(struct if_clone *, int *);
static	void vlan_clone_destroy(struct ifnet *);
static	void vlan_start(struct ifnet *ifp);
static	void vlan_ifinit(void *foo);
static	int vlan_input(struct ether_header *eh, struct mbuf *m);
static	int vlan_input_tag(struct ether_header *eh, struct mbuf *m,
		u_int16_t t);
static	int vlan_ioctl(struct ifnet *ifp, u_long cmd, caddr_t addr);
static	int vlan_setmulti(struct ifnet *ifp);
static	int vlan_unconfig(struct ifnet *ifp);
static	int vlan_config(struct ifvlan *ifv, struct ifnet *p);

struct if_clone vlan_cloner =
    IF_CLONE_INITIALIZER("vlan", vlan_clone_create, vlan_clone_destroy);

/*
 * Program our multicast filter. What we're actually doing is
 * programming the multicast filter of the parent. This has the
 * side effect of causing the parent interface to receive multicast
 * traffic that it doesn't really want, which ends up being discarded
 * later by the upper protocol layers. Unfortunately, there's no way
 * to avoid this: there really is only one physical interface.
 */
static int
vlan_setmulti(struct ifnet *ifp)
{
	struct ifnet		*ifp_p;
	struct ifmultiaddr	*ifma, *rifma = NULL;
	struct ifvlan		*sc;
	struct vlan_mc_entry	*mc = NULL;
	struct sockaddr_dl	sdl;
	int			error;

	/* Find the parent. */
	sc = ifp->if_softc;
	ifp_p = sc->ifv_p;

	/*
	 * If we don't have a parent, just remember the membership for
	 * when we do.
	 */
	if (ifp_p == NULL)
		return(0);

	bzero((char *)&sdl, sizeof sdl);
	sdl.sdl_len = sizeof sdl;
	sdl.sdl_family = AF_LINK;
	sdl.sdl_index = ifp_p->if_index;
	sdl.sdl_type = IFT_ETHER;
	sdl.sdl_alen = ETHER_ADDR_LEN;

	/* First, remove any existing filter entries. */
	while(SLIST_FIRST(&sc->vlan_mc_listhead) != NULL) {
		mc = SLIST_FIRST(&sc->vlan_mc_listhead);
		bcopy((char *)&mc->mc_addr, LLADDR(&sdl), ETHER_ADDR_LEN);
		error = if_delmulti(ifp_p, (struct sockaddr *)&sdl);
		if (error)
			return(error);
		SLIST_REMOVE_HEAD(&sc->vlan_mc_listhead, mc_entries);
		free(mc, M_VLAN);
	}

	/* Now program new ones. */
	LIST_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		mc = malloc(sizeof(struct vlan_mc_entry), M_VLAN, M_WAITOK);
		bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		    (char *)&mc->mc_addr, ETHER_ADDR_LEN);
		SLIST_INSERT_HEAD(&sc->vlan_mc_listhead, mc, mc_entries);
		bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		    LLADDR(&sdl), ETHER_ADDR_LEN);
		error = if_addmulti(ifp_p, (struct sockaddr *)&sdl, &rifma);
		if (error)
			return(error);
	}

	return(0);
}

static int
vlan_modevent(module_t mod, int type, void *data) 
{ 
	int i;
	int err;

	switch (type) { 
	case MOD_LOAD: 
		vlanunits->rm_type = RMAN_ARRAY;
		vlanunits->rm_descr = "configurable if_vlan units";
		err = rman_init(vlanunits);
		if (err != 0)
			return (err);
		err = rman_manage_region(vlanunits, 0, VLAN_MAXUNIT);
		if (err != 0) {
			printf("%s: vlanunits: rman_manage_region: Failed %d\n",
			    VLANNAME, err);
			rman_fini(vlanunits);
			return (err);
		}
		LIST_INIT(&ifv_list);
		vlan_input_p = vlan_input;
		vlan_input_tag_p = vlan_input_tag;
		if_clone_attach(&vlan_cloner);
		for(i = 0; i < NVLAN; i ++) {
			err = vlan_clone_create(&vlan_cloner, &i);
			KASSERT(err == 0,
			    ("Unexpected error creating initial VLANs"));
		}
		break; 
	case MOD_UNLOAD: 
		if_clone_detach(&vlan_cloner);
		vlan_input_p = NULL;
		vlan_input_tag_p = NULL;
		while (!LIST_EMPTY(&ifv_list))
			vlan_clone_destroy(&LIST_FIRST(&ifv_list)->ifv_if);
		err = rman_fini(vlanunits);
		if (err != 0)
			 return (err);
		break;
	} 
	return 0; 
} 

static moduledata_t vlan_mod = { 
	"if_vlan", 
	vlan_modevent, 
	0
}; 

DECLARE_MODULE(if_vlan, vlan_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);

static int
vlan_clone_create(struct if_clone *ifc, int *unit)
{
	struct resource *r;
	struct ifvlan *ifv;
	struct ifnet *ifp;
	int s;

	if (*unit > VLAN_MAXUNIT)
		return (ENXIO);

	if (*unit < 0) {
		r  = rman_reserve_resource(vlanunits, 0, VLAN_MAXUNIT, 1,
		    RF_ALLOCATED | RF_ACTIVE, NULL);
		if (r == NULL)
			return (ENOSPC);
		*unit = rman_get_start(r);
	} else {
		r  = rman_reserve_resource(vlanunits, *unit, *unit, 1,
		    RF_ALLOCATED | RF_ACTIVE, NULL);
		if (r == NULL)
			return (EEXIST);
	}

	ifv = malloc(sizeof(struct ifvlan), M_VLAN, M_WAITOK);
	memset(ifv, 0, sizeof(struct ifvlan));
	ifp = &ifv->ifv_if;
	SLIST_INIT(&ifv->vlan_mc_listhead);

	s = splnet();
	LIST_INSERT_HEAD(&ifv_list, ifv, ifv_list);
	splx(s);

	ifp->if_softc = ifv;
	ifp->if_name = "vlan";
	ifp->if_unit = *unit;
	ifv->r_unit = r;
	/* NB: flags are not set here */
	ifp->if_linkmib = &ifv->ifv_mib;
	ifp->if_linkmiblen = sizeof ifv->ifv_mib;
	/* NB: mtu is not set here */

	ifp->if_init = vlan_ifinit;
	ifp->if_start = vlan_start;
	ifp->if_ioctl = vlan_ioctl;
	ifp->if_output = ether_output;
	ifp->if_snd.ifq_maxlen = ifqmaxlen;
	ether_ifattach(ifp, ETHER_BPF_SUPPORTED);
	/* Now undo some of the damage... */
	ifp->if_data.ifi_type = IFT_L2VLAN;
	ifp->if_data.ifi_hdrlen = EVL_ENCAPLEN;

	return (0);
}

static void
vlan_clone_destroy(struct ifnet *ifp)
{
	struct ifvlan *ifv = ifp->if_softc;
	int s;
	int err;

	s = splnet();
	LIST_REMOVE(ifv, ifv_list);
	vlan_unconfig(ifp);
	splx(s);

	ether_ifdetach(ifp, ETHER_BPF_SUPPORTED);

	err = rman_release_resource(ifv->r_unit);
	KASSERT(err == 0, ("Unexpected error freeing resource"));
	free(ifv, M_VLAN);
}

static void
vlan_ifinit(void *foo)
{
	return;
}

static void
vlan_start(struct ifnet *ifp)
{
	struct ifvlan *ifv;
	struct ifnet *p;
	struct ether_vlan_header *evl;
	struct mbuf *m;

	ifv = ifp->if_softc;
	p = ifv->ifv_p;

	ifp->if_flags |= IFF_OACTIVE;
	for (;;) {
		IF_DEQUEUE(&ifp->if_snd, m);
		if (m == 0)
			break;
		if (ifp->if_bpf)
			bpf_mtap(ifp, m);

		/*
		 * Do not run parent's if_start() if the parent is not up,
		 * or parent's driver will cause a system crash.
		 */
		if ((p->if_flags & (IFF_UP | IFF_RUNNING)) !=
					(IFF_UP | IFF_RUNNING)) {
			m_freem(m);
			ifp->if_data.ifi_collisions++;
			continue;
		}

		/*
		 * If the LINK0 flag is set, it means the underlying interface
		 * can do VLAN tag insertion itself and doesn't require us to
	 	 * create a special header for it. In this case, we just pass
		 * the packet along. However, we need some way to tell the
		 * interface where the packet came from so that it knows how
		 * to find the VLAN tag to use, so we set the rcvif in the
		 * mbuf header to our ifnet.
		 *
		 * Note: we also set the M_PROTO1 flag in the mbuf to let
		 * the parent driver know that the rcvif pointer is really
		 * valid. We need to do this because sometimes mbufs will
		 * be allocated by other parts of the system that contain
		 * garbage in the rcvif pointer. Using the M_PROTO1 flag
		 * lets the driver perform a proper sanity check and avoid
		 * following potentially bogus rcvif pointers off into
		 * never-never land.
		 */
		if (ifp->if_flags & IFF_LINK0) {
			m->m_pkthdr.rcvif = ifp;
			m->m_flags |= M_PROTO1;
		} else {
			M_PREPEND(m, EVL_ENCAPLEN, M_DONTWAIT);
			if (m == NULL) {
				printf("vlan%d: M_PREPEND failed", ifp->if_unit);
				ifp->if_ierrors++;
				continue;
			}
			/* M_PREPEND takes care of m_len, m_pkthdr.len for us */

			m = m_pullup(m, ETHER_HDR_LEN + EVL_ENCAPLEN);
			if (m == NULL) {
				printf("vlan%d: m_pullup failed", ifp->if_unit);
				ifp->if_ierrors++;
				continue;
			}

			/*
			 * Transform the Ethernet header into an Ethernet header
			 * with 802.1Q encapsulation.
			 */
			bcopy(mtod(m, char *) + EVL_ENCAPLEN, mtod(m, char *),
			      sizeof(struct ether_header));
			evl = mtod(m, struct ether_vlan_header *);
			evl->evl_proto = evl->evl_encap_proto;
			evl->evl_encap_proto = htons(ETHERTYPE_VLAN);
			evl->evl_tag = htons(ifv->ifv_tag);
#ifdef DEBUG
			printf("vlan_start: %*D\n", sizeof *evl,
			    (char *)evl, ":");
#endif
		}

		/*
		 * Send it, precisely as ether_output() would have.
		 * We are already running at splimp.
		 */
		if (IF_QFULL(&p->if_snd)) {
			IF_DROP(&p->if_snd);
				/* XXX stats */
			ifp->if_oerrors++;
			m_freem(m);
			continue;
		}
		IF_ENQUEUE(&p->if_snd, m);
		ifp->if_opackets++;
		p->if_obytes += m->m_pkthdr.len;
		if (m->m_flags & M_MCAST)
			p->if_omcasts++;
		if ((p->if_flags & IFF_OACTIVE) == 0)
			p->if_start(p);
	}
	ifp->if_flags &= ~IFF_OACTIVE;

	return;
}

static int
vlan_input_tag(struct ether_header *eh, struct mbuf *m, u_int16_t t)
{
	struct ifvlan *ifv;

	/*
	 * Fake up a header and send the packet to the physical interface's
	 * bpf tap if active.
	 */
	if (m->m_pkthdr.rcvif->if_bpf != NULL) {
		struct m_hdr mh;
		struct ether_vlan_header evh;

		bcopy(eh, &evh, 2*ETHER_ADDR_LEN);
		evh.evl_encap_proto = htons(ETHERTYPE_VLAN);
		evh.evl_tag = htons(t);
		evh.evl_proto = eh->ether_type;

		/* This kludge is OK; BPF treats the "mbuf" as read-only */
		mh.mh_next = m;
		mh.mh_data = (char *)&evh;
		mh.mh_len = ETHER_HDR_LEN + EVL_ENCAPLEN;
		bpf_mtap(m->m_pkthdr.rcvif, (struct mbuf *)&mh);
	}

	for (ifv = LIST_FIRST(&ifv_list); ifv != NULL;
	    ifv = LIST_NEXT(ifv, ifv_list)) {
		if (m->m_pkthdr.rcvif == ifv->ifv_p
		    && ifv->ifv_tag == EVL_VLANOFTAG(t))
			break;
	}

	if (ifv == NULL || (ifv->ifv_if.if_flags & IFF_UP) == 0) {
		m_freem(m);
		return -1;	/* So the parent can take note */
	}

	/*
	 * Having found a valid vlan interface corresponding to
	 * the given source interface and vlan tag, run the
	 * the real packet through ether_input().
	 */
	m->m_pkthdr.rcvif = &ifv->ifv_if;

	ifv->ifv_if.if_ipackets++;
	ether_input(&ifv->ifv_if, eh, m);
	return 0;
}

static int
vlan_input(struct ether_header *eh, struct mbuf *m)
{
	struct ifvlan *ifv;

	for (ifv = LIST_FIRST(&ifv_list); ifv != NULL;
	    ifv = LIST_NEXT(ifv, ifv_list)) {
		if (m->m_pkthdr.rcvif == ifv->ifv_p
		    && (EVL_VLANOFTAG(ntohs(*mtod(m, u_int16_t *)))
			== ifv->ifv_tag))
			break;
	}

	if (ifv == NULL || (ifv->ifv_if.if_flags & IFF_UP) == 0) {
		m->m_pkthdr.rcvif->if_noproto++;
		m_freem(m);
		return -1;	/* so ether_input can take note */
	}

	/*
	 * Having found a valid vlan interface corresponding to
	 * the given source interface and vlan tag, remove the
	 * encapsulation, and run the real packet through
	 * ether_input() a second time (it had better be
	 * reentrant!).
	 */
	m->m_pkthdr.rcvif = &ifv->ifv_if;
	eh->ether_type = mtod(m, u_int16_t *)[1];
	m->m_data += EVL_ENCAPLEN;
	m->m_len -= EVL_ENCAPLEN;
	m->m_pkthdr.len -= EVL_ENCAPLEN;

	ifv->ifv_if.if_ipackets++;
	ether_input(&ifv->ifv_if, eh, m);
	return 0;
}

static int
vlan_config(struct ifvlan *ifv, struct ifnet *p)
{
	struct ifaddr *ifa1, *ifa2;
	struct sockaddr_dl *sdl1, *sdl2;

	if (p->if_data.ifi_type != IFT_ETHER)
		return EPROTONOSUPPORT;
	if (ifv->ifv_p)
		return EBUSY;
	ifv->ifv_p = p;
	if (p->if_data.ifi_hdrlen == sizeof(struct ether_vlan_header))
		ifv->ifv_if.if_mtu = p->if_mtu;
	else
		ifv->ifv_if.if_mtu = p->if_data.ifi_mtu - EVL_ENCAPLEN;

	/*
	 * Copy only a selected subset of flags from the parent.
	 * Other flags are none of our business.
	 */
	ifv->ifv_if.if_flags = (p->if_flags &
	    (IFF_BROADCAST | IFF_MULTICAST | IFF_SIMPLEX | IFF_POINTOPOINT));

	/*
	 * Set up our ``Ethernet address'' to reflect the underlying
	 * physical interface's.
	 */
	ifa1 = ifnet_addrs[ifv->ifv_if.if_index - 1];
	ifa2 = ifnet_addrs[p->if_index - 1];
	sdl1 = (struct sockaddr_dl *)ifa1->ifa_addr;
	sdl2 = (struct sockaddr_dl *)ifa2->ifa_addr;
	sdl1->sdl_type = IFT_ETHER;
	sdl1->sdl_alen = ETHER_ADDR_LEN;
	bcopy(LLADDR(sdl2), LLADDR(sdl1), ETHER_ADDR_LEN);
	bcopy(LLADDR(sdl2), ifv->ifv_ac.ac_enaddr, ETHER_ADDR_LEN);

	/*
	 * Configure multicast addresses that may already be
	 * joined on the vlan device.
	 */
	(void)vlan_setmulti(&ifv->ifv_if);

	return 0;
}

static int
vlan_unconfig(struct ifnet *ifp)
{
	struct ifaddr *ifa;
	struct sockaddr_dl *sdl;
	struct vlan_mc_entry *mc;
	struct ifvlan *ifv;
	struct ifnet *p;
	int error;

	ifv = ifp->if_softc;
	p = ifv->ifv_p;

	if (p) {
		struct sockaddr_dl sdl;

		/*
		 * Since the interface is being unconfigured, we need to
		 * empty the list of multicast groups that we may have joined
		 * while we were alive from the parent's list.
		 */
		bzero((char *)&sdl, sizeof sdl);
		sdl.sdl_len = sizeof sdl;
		sdl.sdl_family = AF_LINK;
		sdl.sdl_index = p->if_index;
		sdl.sdl_type = IFT_ETHER;
		sdl.sdl_alen = ETHER_ADDR_LEN;

		while(SLIST_FIRST(&ifv->vlan_mc_listhead) != NULL) {
			mc = SLIST_FIRST(&ifv->vlan_mc_listhead);
			bcopy((char *)&mc->mc_addr, LLADDR(&sdl), ETHER_ADDR_LEN);
			error = if_delmulti(p, (struct sockaddr *)&sdl);
			if (error)
				return(error);
			SLIST_REMOVE_HEAD(&ifv->vlan_mc_listhead, mc_entries);
			free(mc, M_VLAN);
		}
	}

	/* Disconnect from parent. */
	ifv->ifv_p = NULL;
	ifv->ifv_if.if_mtu = ETHERMTU;

	/* Clear our MAC address. */
	ifa = ifnet_addrs[ifv->ifv_if.if_index - 1];
	sdl = (struct sockaddr_dl *)ifa->ifa_addr;
	sdl->sdl_type = IFT_ETHER;
	sdl->sdl_alen = ETHER_ADDR_LEN;
	bzero(LLADDR(sdl), ETHER_ADDR_LEN);
	bzero(ifv->ifv_ac.ac_enaddr, ETHER_ADDR_LEN);

	return 0;
}

static int
vlan_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ifaddr *ifa;
	struct ifnet *p;
	struct ifreq *ifr;
	struct ifvlan *ifv;
	struct vlanreq vlr;
	int error = 0;

	ifr = (struct ifreq *)data;
	ifa = (struct ifaddr *)data;
	ifv = ifp->if_softc;

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;

		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			arp_ifinit(&ifv->ifv_if, ifa);
			break;
#endif
		default:
			break;
		}
		break;

	case SIOCGIFADDR:
		{
			struct sockaddr *sa;

			sa = (struct sockaddr *) &ifr->ifr_data;
			bcopy(((struct arpcom *)ifp->if_softc)->ac_enaddr,
			      (caddr_t) sa->sa_data, ETHER_ADDR_LEN);
		}
		break;

	case SIOCGIFMEDIA:
		if (ifv->ifv_p != NULL) {
			error = (ifv->ifv_p->if_ioctl)(ifv->ifv_p, SIOCGIFMEDIA, data);
			/* Limit the result to the parent's current config. */
			if (error == 0) {
				struct ifmediareq *ifmr;

				ifmr = (struct ifmediareq *) data;
				if (ifmr->ifm_count >= 1 && ifmr->ifm_ulist) {
					ifmr->ifm_count = 1;
					error = copyout(&ifmr->ifm_current,
						ifmr->ifm_ulist, 
						sizeof(int));
				}
			}
		} else
			error = EINVAL;
		break;

	case SIOCSIFMEDIA:
		error = EINVAL;
		break;

	case SIOCSIFMTU:
		/*
		 * Set the interface MTU.
		 * This is bogus. The underlying interface might support
	 	 * jumbo frames.
		 */
		if (ifr->ifr_mtu > ETHERMTU) {
			error = EINVAL;
		} else {
			ifp->if_mtu = ifr->ifr_mtu;
		}
		break;

	case SIOCSETVLAN:
		error = copyin(ifr->ifr_data, &vlr, sizeof vlr);
		if (error)
			break;
		if (vlr.vlr_tag & ~EVL_VLID_MASK) {
			error = EINVAL;
			break;
		}
		if (vlr.vlr_parent[0] == '\0') {
			vlan_unconfig(ifp);
			if (ifp->if_flags & IFF_UP) {
				int s = splimp();
				if_down(ifp);
				splx(s);
			}		
			ifp->if_flags &= ~IFF_RUNNING;
			break;
		}
		p = ifunit(vlr.vlr_parent);
		if (p == 0) {
			error = ENOENT;
			break;
		}
		error = vlan_config(ifv, p);
		if (error)
			break;
		ifv->ifv_tag = vlr.vlr_tag;
		ifp->if_flags |= IFF_RUNNING;
		break;
		
	case SIOCGETVLAN:
		bzero(&vlr, sizeof vlr);
		if (ifv->ifv_p) {
			snprintf(vlr.vlr_parent, sizeof(vlr.vlr_parent),
			    "%s%d", ifv->ifv_p->if_name, ifv->ifv_p->if_unit);
			vlr.vlr_tag = ifv->ifv_tag;
		}
		error = copyout(&vlr, ifr->ifr_data, sizeof vlr);
		break;
		
	case SIOCSIFFLAGS:
		/*
		 * We don't support promiscuous mode
		 * right now because it would require help from the
		 * underlying drivers, which hasn't been implemented.
		 */
		if (ifr->ifr_flags & (IFF_PROMISC)) {
			ifp->if_flags &= ~(IFF_PROMISC);
			error = EINVAL;
		}
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		error = vlan_setmulti(ifp);
		break;
	default:
		error = EINVAL;
	}
	return error;
}
