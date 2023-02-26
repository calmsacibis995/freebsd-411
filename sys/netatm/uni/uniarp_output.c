/*
 *
 * ===================================
 * HARP  |  Host ATM Research Platform
 * ===================================
 *
 *
 * This Host ATM Research Platform ("HARP") file (the "Software") is
 * made available by Network Computing Services, Inc. ("NetworkCS")
 * "AS IS".  NetworkCS does not provide maintenance, improvements or
 * support of any kind.
 *
 * NETWORKCS MAKES NO WARRANTIES OR REPRESENTATIONS, EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE, AS TO ANY ELEMENT OF THE
 * SOFTWARE OR ANY SUPPORT PROVIDED IN CONNECTION WITH THIS SOFTWARE.
 * In no event shall NetworkCS be responsible for any damages, including
 * but not limited to consequential damages, arising from or relating to
 * any use of the Software or related support.
 *
 * Copyright 1994-1998 Network Computing Services, Inc.
 *
 * Copies of this Software may be made, however, the above copyright
 * notice must be reproduced on all copies.
 *
 *	@(#) $FreeBSD: src/sys/netatm/uni/uniarp_output.c,v 1.3 1999/08/28 00:49:03 peter Exp $
 *
 */

/*
 * ATM Forum UNI Support
 * ---------------------
 *
 * UNI ATMARP support (RFC1577) - Output packet processing
 *
 */

#include <netatm/kern_include.h>

#include <netatm/ipatm/ipatm_var.h>
#include <netatm/ipatm/ipatm_serv.h>
#include <netatm/uni/uniip_var.h>

#ifndef lint
__RCSID("@(#) $FreeBSD: src/sys/netatm/uni/uniarp_output.c,v 1.3 1999/08/28 00:49:03 peter Exp $");
#endif


/*
 * Issue an ATMARP Request PDU
 * 
 * Arguments:
 *	uip	pointer to IP interface
 *	tip	pointer to target IP address
 *
 * Returns:
 *	0	PDU was successfully sent
 *	else	unable to send PDU
 *
 */
int
uniarp_arp_req(uip, tip)
	struct uniip	*uip;
	struct in_addr	*tip;
{
	KBuffer		*m;
	struct atmarp_hdr	*ahp;
	struct atm_nif	*nip;
	struct ip_nif	*inp;
	struct ipvcc	*ivp;
	struct siginst	*sip;
	char		*cp;
	int		len, err;

	inp = uip->uip_ipnif;
	nip = inp->inf_nif;
	sip = inp->inf_nif->nif_pif->pif_siginst;

	/*
	 * Figure out how long pdu is going to be
	 */
	len = sizeof(struct atmarp_hdr) + (2 * sizeof(struct in_addr));
	switch (sip->si_addr.address_format) {
	case T_ATM_ENDSYS_ADDR:
		len += sip->si_addr.address_length;
		break;

	case T_ATM_E164_ADDR:
		len += sip->si_addr.address_length;
		if (sip->si_subaddr.address_format == T_ATM_ENDSYS_ADDR)
			len += sip->si_subaddr.address_length;
		break;
	}
	
	/*
	 * Get a buffer for pdu
	 */
	KB_ALLOCPKT(m, len, KB_F_NOWAIT, KB_T_DATA);
	if (m == NULL)
		return (1);

	/*
	 * Place aligned pdu at end of buffer
	 */
	KB_TAILALIGN(m, len);
	KB_DATASTART(m, ahp, struct atmarp_hdr *);

	/*
	 * Setup variable fields pointer
	 */
	cp = (char *)ahp + sizeof(struct atmarp_hdr);

	/*
	 * Build fields
	 */
	ahp->ah_hrd = htons(ARP_ATMFORUM);
	ahp->ah_pro = htons(ETHERTYPE_IP);
	len = sip->si_addr.address_length;
	switch (sip->si_addr.address_format) {
	case T_ATM_ENDSYS_ADDR:
		ahp->ah_shtl = ARP_TL_NSAPA | (len & ARP_TL_LMASK);

		/* ah_sha */
		KM_COPY(sip->si_addr.address, cp, len - 1);
		((struct atm_addr_nsap *)cp)->aan_sel = nip->nif_sel;
		cp += len;

		ahp->ah_sstl = 0;
		break;

	case T_ATM_E164_ADDR:
		ahp->ah_shtl = ARP_TL_E164 | (len & ARP_TL_LMASK);

		/* ah_sha */
		KM_COPY(sip->si_addr.address, cp, len);
		cp += len;

		if (sip->si_subaddr.address_format == T_ATM_ENDSYS_ADDR) {
			len = sip->si_subaddr.address_length;
			ahp->ah_sstl = ARP_TL_NSAPA | (len & ARP_TL_LMASK);

			/* ah_ssa */
			KM_COPY(sip->si_subaddr.address, cp, len - 1);
			((struct atm_addr_nsap *)cp)->aan_sel = nip->nif_sel;
			cp += len;
		} else
			ahp->ah_sstl = 0;
		break;

	default:
		ahp->ah_shtl = 0;
		ahp->ah_sstl = 0;
	}

	ahp->ah_op = htons(ARP_REQUEST);
	ahp->ah_spln = sizeof(struct in_addr);

	/* ah_spa */
	KM_COPY((caddr_t)&(IA_SIN(inp->inf_addr)->sin_addr), cp, 
		sizeof(struct in_addr));
	cp += sizeof(struct in_addr);

	ahp->ah_thtl = 0;
	ahp->ah_tstl = 0;

	ahp->ah_tpln = sizeof(struct in_addr);

	/* ah_tpa */
	KM_COPY((caddr_t)tip, cp, sizeof(struct in_addr));

	/*
	 * Finally, send the pdu to the ATMARP server
	 */
	ivp = uip->uip_arpsvrvcc;
	if (uniarp_print)
		uniarp_pdu_print(ivp, m, "send");
	err = atm_cm_cpcs_data(ivp->iv_arpconn, m);
	if (err) {
		/*
		 * Didn't make it
		 */
		KB_FREEALL(m);
		return (1);
	}

	return (0);
}


/*
 * Issue an ATMARP Response PDU
 * 
 * Arguments:
 *	uip	pointer to IP interface
 *	amp	pointer to source map entry
 *	tip	pointer to target IP address
 *	tatm	pointer to target ATM address
 *	tsub	pointer to target ATM subaddress
 *	ivp	pointer to vcc over which to send pdu
 *
 * Returns:
 *	0	PDU was successfully sent
 *	else	unable to send PDU
 *
 */
int
uniarp_arp_rsp(uip, amp, tip, tatm, tsub, ivp)
	struct uniip	*uip;
	struct arpmap	*amp;
	struct in_addr	*tip;
	Atm_addr	*tatm;
	Atm_addr	*tsub;
	struct ipvcc	*ivp;
{
	KBuffer		*m;
	struct atmarp_hdr	*ahp;
	char		*cp;
	int		len, err;

	/*
	 * Figure out how long pdu is going to be
	 */
	len = sizeof(struct atmarp_hdr) + (2 * sizeof(struct in_addr));
	switch (amp->am_dstatm.address_format) {
	case T_ATM_ENDSYS_ADDR:
		len += amp->am_dstatm.address_length;
		break;

	case T_ATM_E164_ADDR:
		len += amp->am_dstatm.address_length;
		if (amp->am_dstatmsub.address_format == T_ATM_ENDSYS_ADDR)
			len += amp->am_dstatmsub.address_length;
		break;
	}

	switch (tatm->address_format) {
	case T_ATM_ENDSYS_ADDR:
		len += tatm->address_length;
		break;

	case T_ATM_E164_ADDR:
		len += tatm->address_length;
		if (tsub->address_format == T_ATM_ENDSYS_ADDR)
			len += tsub->address_length;
		break;
	}

	/*
	 * Get a buffer for pdu
	 */
	KB_ALLOCPKT(m, len, KB_F_NOWAIT, KB_T_DATA);
	if (m == NULL)
		return (1);

	/*
	 * Place aligned pdu at end of buffer
	 */
	KB_TAILALIGN(m, len);
	KB_DATASTART(m, ahp, struct atmarp_hdr *);

	/*
	 * Setup variable fields pointer
	 */
	cp = (char *)ahp + sizeof(struct atmarp_hdr);

	/*
	 * Build fields
	 */
	ahp->ah_hrd = htons(ARP_ATMFORUM);
	ahp->ah_pro = htons(ETHERTYPE_IP);
	len = amp->am_dstatm.address_length;
	switch (amp->am_dstatm.address_format) {
	case T_ATM_ENDSYS_ADDR:
		ahp->ah_shtl = ARP_TL_NSAPA | (len & ARP_TL_LMASK);

		/* ah_sha */
		KM_COPY(amp->am_dstatm.address, cp, len);
		cp += len;

		ahp->ah_sstl = 0;
		break;

	case T_ATM_E164_ADDR:
		ahp->ah_shtl = ARP_TL_E164 | (len & ARP_TL_LMASK);

		/* ah_sha */
		KM_COPY(amp->am_dstatm.address, cp, len);
		cp += len;

		if (amp->am_dstatmsub.address_format == T_ATM_ENDSYS_ADDR) {
			len = amp->am_dstatmsub.address_length;
			ahp->ah_sstl = ARP_TL_NSAPA | (len & ARP_TL_LMASK);

			/* ah_ssa */
			KM_COPY(amp->am_dstatmsub.address, cp, len);
			cp += len;
		} else
			ahp->ah_sstl = 0;
		break;

	default:
		ahp->ah_shtl = 0;
		ahp->ah_sstl = 0;
	}

	ahp->ah_op = htons(ARP_REPLY);
	ahp->ah_spln = sizeof(struct in_addr);

	/* ah_spa */
	KM_COPY((caddr_t)&amp->am_dstip, cp, sizeof(struct in_addr));
	cp += sizeof(struct in_addr);

	len = tatm->address_length;
	switch (tatm->address_format) {
	case T_ATM_ENDSYS_ADDR:
		ahp->ah_thtl = ARP_TL_NSAPA | (len & ARP_TL_LMASK);

		/* ah_tha */
		KM_COPY(tatm->address, cp, len);
		cp += len;

		ahp->ah_tstl = 0;
		break;

	case T_ATM_E164_ADDR:
		ahp->ah_thtl = ARP_TL_E164 | (len & ARP_TL_LMASK);

		/* ah_tha */
		KM_COPY(tatm->address, cp, len);
		cp += len;

		if (tsub->address_format == T_ATM_ENDSYS_ADDR) {
			len = tsub->address_length;
			ahp->ah_tstl = ARP_TL_NSAPA | (len & ARP_TL_LMASK);

			/* ah_tsa */
			KM_COPY(tsub->address, cp, len);
			cp += len;
		} else
			ahp->ah_tstl = 0;
		break;

	default:
		ahp->ah_thtl = 0;
		ahp->ah_tstl = 0;
	}

	ahp->ah_tpln = sizeof(struct in_addr);

	/* ah_tpa */
	KM_COPY((caddr_t)tip, cp, sizeof(struct in_addr));

	/*
	 * Finally, send the pdu to the vcc peer
	 */
	if (uniarp_print)
		uniarp_pdu_print(ivp, m, "send");
	err = atm_cm_cpcs_data(ivp->iv_arpconn, m);
	if (err) {
		/*
		 * Didn't make it
		 */
		KB_FREEALL(m);
		return (1);
	}

	return (0);
}


/*
 * Issue an ATMARP NAK PDU
 * 
 * Arguments:
 *	uip	pointer to IP interface
 *	m	pointer to ATMARP_REQ buffer chain
 *	ivp	pointer to vcc over which to send pdu
 *
 * Returns:
 *	0	PDU was successfully sent
 *	else	unable to send PDU
 *
 */
int
uniarp_arp_nak(uip, m, ivp)
	struct uniip	*uip;
	KBuffer		*m;
	struct ipvcc	*ivp;
{
	struct atmarp_hdr	*ahp;
	int		err;

	/*
	 * Get the fixed fields together
	 */
	if (KB_LEN(m) < sizeof(struct atmarp_hdr)) {
		KB_PULLUP(m, sizeof(struct atmarp_hdr), m);
		if (m == NULL)
			return (1);
	}
	KB_DATASTART(m, ahp, struct atmarp_hdr *);

	/*
	 * Set new op-code
	 */
	ahp->ah_op = htons(ARP_NAK);

	/*
	 * Finally, send the pdu to the vcc peer
	 */
	if (uniarp_print)
		uniarp_pdu_print(ivp, m, "send");
	err = atm_cm_cpcs_data(ivp->iv_arpconn, m);
	if (err) {
		/*
		 * Didn't make it
		 */
		KB_FREEALL(m);
		return (1);
	}

	return (0);
}


/*
 * Issue an InATMARP Request PDU
 * 
 * Arguments:
 *	uip	pointer to IP interface
 *	tatm	pointer to target ATM address
 *	tsub	pointer to target ATM subaddress
 *	ivp	pointer to vcc over which to send pdu
 *
 * Returns:
 *	0	PDU was successfully sent
 *	else	unable to send PDU
 *
 */
int
uniarp_inarp_req(uip, tatm, tsub, ivp)
	struct uniip	*uip;
	Atm_addr	*tatm;
	Atm_addr	*tsub;
	struct ipvcc	*ivp;
{
	KBuffer		*m;
	struct atmarp_hdr	*ahp;
	struct atm_nif	*nip;
	struct ip_nif	*inp;
	struct siginst	*sip;
	char		*cp;
	int		len, err;

	inp = uip->uip_ipnif;
	nip = inp->inf_nif;
	sip = inp->inf_nif->nif_pif->pif_siginst;

	/*
	 * Figure out how long pdu is going to be
	 */
	len = sizeof(struct atmarp_hdr) + sizeof(struct in_addr);
	switch (sip->si_addr.address_format) {
	case T_ATM_ENDSYS_ADDR:
		len += sip->si_addr.address_length;
		break;

	case T_ATM_E164_ADDR:
		len += sip->si_addr.address_length;
		if (sip->si_subaddr.address_format == T_ATM_ENDSYS_ADDR)
			len += sip->si_subaddr.address_length;
		break;
	}

	switch (tatm->address_format) {
	case T_ATM_ENDSYS_ADDR:
		len += tatm->address_length;
		break;

	case T_ATM_E164_ADDR:
		len += tatm->address_length;
		if (tsub->address_format == T_ATM_ENDSYS_ADDR)
			len += tsub->address_length;
		break;
	}

	/*
	 * Get a buffer for pdu
	 */
	KB_ALLOCPKT(m, len, KB_F_NOWAIT, KB_T_DATA);
	if (m == NULL)
		return (1);

	/*
	 * Place aligned pdu at end of buffer
	 */
	KB_TAILALIGN(m, len);
	KB_DATASTART(m, ahp, struct atmarp_hdr *);

	/*
	 * Setup variable fields pointer
	 */
	cp = (char *)ahp + sizeof(struct atmarp_hdr);

	/*
	 * Build fields
	 */
	ahp->ah_hrd = htons(ARP_ATMFORUM);
	ahp->ah_pro = htons(ETHERTYPE_IP);
	len = sip->si_addr.address_length;
	switch (sip->si_addr.address_format) {
	case T_ATM_ENDSYS_ADDR:
		ahp->ah_shtl = ARP_TL_NSAPA | (len & ARP_TL_LMASK);

		/* ah_sha */
		KM_COPY(sip->si_addr.address, cp, len - 1);
		((struct atm_addr_nsap *)cp)->aan_sel = nip->nif_sel;
		cp += len;

		ahp->ah_sstl = 0;
		break;

	case T_ATM_E164_ADDR:
		ahp->ah_shtl = ARP_TL_E164 | (len & ARP_TL_LMASK);

		/* ah_sha */
		KM_COPY(sip->si_addr.address, cp, len);
		cp += len;

		if (sip->si_subaddr.address_format == T_ATM_ENDSYS_ADDR) {
			len = sip->si_subaddr.address_length;
			ahp->ah_sstl = ARP_TL_NSAPA | (len & ARP_TL_LMASK);

			/* ah_ssa */
			KM_COPY(sip->si_subaddr.address, cp, len - 1);
			((struct atm_addr_nsap *)cp)->aan_sel = nip->nif_sel;
			cp += len;
		} else
			ahp->ah_sstl = 0;
		break;

	default:
		ahp->ah_shtl = 0;
		ahp->ah_sstl = 0;
	}

	ahp->ah_op = htons(INARP_REQUEST);
	ahp->ah_spln = sizeof(struct in_addr);

	/* ah_spa */
	KM_COPY((caddr_t)&(IA_SIN(inp->inf_addr)->sin_addr), cp, 
		sizeof(struct in_addr));
	cp += sizeof(struct in_addr);

	len = tatm->address_length;
	switch (tatm->address_format) {
	case T_ATM_ENDSYS_ADDR:
		ahp->ah_thtl = ARP_TL_NSAPA | (len & ARP_TL_LMASK);

		/* ah_tha */
		KM_COPY(tatm->address, cp, len);
		cp += len;

		ahp->ah_tstl = 0;
		break;

	case T_ATM_E164_ADDR:
		ahp->ah_thtl = ARP_TL_E164 | (len & ARP_TL_LMASK);

		/* ah_tha */
		KM_COPY(tatm->address, cp, len);
		cp += len;

		if (tsub->address_format == T_ATM_ENDSYS_ADDR) {
			len = tsub->address_length;
			ahp->ah_tstl = ARP_TL_NSAPA | (len & ARP_TL_LMASK);

			/* ah_tsa */
			KM_COPY(tsub->address, cp, len);
			cp += len;
		} else
			ahp->ah_tstl = 0;
		break;

	default:
		ahp->ah_thtl = 0;
		ahp->ah_tstl = 0;
	}

	ahp->ah_tpln = 0;

	/*
	 * Finally, send the pdu to the vcc peer
	 */
	if (uniarp_print)
		uniarp_pdu_print(ivp, m, "send");
	err = atm_cm_cpcs_data(ivp->iv_arpconn, m);
	if (err) {
		/*
		 * Didn't make it
		 */
		KB_FREEALL(m);
		return (1);
	}

	return (0);
}


/*
 * Issue an InATMARP Response PDU
 * 
 * Arguments:
 *	uip	pointer to IP interface
 *	tip	pointer to target IP address
 *	tatm	pointer to target ATM address
 *	tsub	pointer to target ATM subaddress
 *	ivp	pointer to vcc over which to send pdu
 *
 * Returns:
 *	0	PDU was successfully sent
 *	else	unable to send PDU
 *
 */
int
uniarp_inarp_rsp(uip, tip, tatm, tsub, ivp)
	struct uniip	*uip;
	struct in_addr	*tip;
	Atm_addr	*tatm;
	Atm_addr	*tsub;
	struct ipvcc	*ivp;
{
	KBuffer		*m;
	struct atmarp_hdr	*ahp;
	struct atm_nif	*nip;
	struct ip_nif	*inp;
	struct siginst	*sip;
	char		*cp;
	int		len, err;

	inp = uip->uip_ipnif;
	nip = inp->inf_nif;
	sip = inp->inf_nif->nif_pif->pif_siginst;

	/*
	 * Figure out how long pdu is going to be
	 */
	len = sizeof(struct atmarp_hdr) + (2 * sizeof(struct in_addr));
	switch (sip->si_addr.address_format) {
	case T_ATM_ENDSYS_ADDR:
		len += sip->si_addr.address_length;
		break;

	case T_ATM_E164_ADDR:
		len += sip->si_addr.address_length;
		if (sip->si_subaddr.address_format == T_ATM_ENDSYS_ADDR)
			len += sip->si_subaddr.address_length;
		break;
	}

	switch (tatm->address_format) {
	case T_ATM_ENDSYS_ADDR:
		len += tatm->address_length;
		break;

	case T_ATM_E164_ADDR:
		len += tatm->address_length;
		if (tsub->address_format == T_ATM_ENDSYS_ADDR)
			len += tsub->address_length;
		break;
	}

	/*
	 * Get a buffer for pdu
	 */
	KB_ALLOCPKT(m, len, KB_F_NOWAIT, KB_T_DATA);
	if (m == NULL)
		return (1);

	/*
	 * Place aligned pdu at end of buffer
	 */
	KB_TAILALIGN(m, len);
	KB_DATASTART(m, ahp, struct atmarp_hdr *);

	/*
	 * Setup variable fields pointer
	 */
	cp = (char *)ahp + sizeof(struct atmarp_hdr);

	/*
	 * Build fields
	 */
	ahp->ah_hrd = htons(ARP_ATMFORUM);
	ahp->ah_pro = htons(ETHERTYPE_IP);
	len = sip->si_addr.address_length;
	switch (sip->si_addr.address_format) {
	case T_ATM_ENDSYS_ADDR:
		ahp->ah_shtl = ARP_TL_NSAPA | (len & ARP_TL_LMASK);

		/* ah_sha */
		KM_COPY(sip->si_addr.address, cp, len - 1);
		((struct atm_addr_nsap *)cp)->aan_sel = nip->nif_sel;
		cp += len;

		ahp->ah_sstl = 0;
		break;

	case T_ATM_E164_ADDR:
		ahp->ah_shtl = ARP_TL_E164 | (len & ARP_TL_LMASK);

		/* ah_sha */
		KM_COPY(sip->si_addr.address, cp, len);
		cp += len;

		if (sip->si_subaddr.address_format == T_ATM_ENDSYS_ADDR) {
			len = sip->si_subaddr.address_length;
			ahp->ah_sstl = ARP_TL_NSAPA | (len & ARP_TL_LMASK);

			/* ah_ssa */
			KM_COPY(sip->si_subaddr.address, cp, len - 1);
			((struct atm_addr_nsap *)cp)->aan_sel = nip->nif_sel;
			cp += len;
		} else
			ahp->ah_sstl = 0;
		break;

	default:
		ahp->ah_shtl = 0;
		ahp->ah_sstl = 0;
	}

	ahp->ah_op = htons(INARP_REPLY);
	ahp->ah_spln = sizeof(struct in_addr);

	/* ah_spa */
	KM_COPY((caddr_t)&(IA_SIN(inp->inf_addr)->sin_addr), cp, 
		sizeof(struct in_addr));
	cp += sizeof(struct in_addr);

	len = tatm->address_length;
	switch (tatm->address_format) {
	case T_ATM_ENDSYS_ADDR:
		ahp->ah_thtl = ARP_TL_NSAPA | (len & ARP_TL_LMASK);

		/* ah_tha */
		KM_COPY(tatm->address, cp, len);
		cp += len;

		ahp->ah_tstl = 0;
		break;

	case T_ATM_E164_ADDR:
		ahp->ah_thtl = ARP_TL_E164 | (len & ARP_TL_LMASK);

		/* ah_tha */
		KM_COPY(tatm->address, cp, len);
		cp += len;

		if (tsub->address_format == T_ATM_ENDSYS_ADDR) {
			len = tsub->address_length;
			ahp->ah_tstl = ARP_TL_NSAPA | (len & ARP_TL_LMASK);

			/* ah_tsa */
			KM_COPY(tsub->address, cp, len);
			cp += len;
		} else
			ahp->ah_tstl = 0;
		break;

	default:
		ahp->ah_thtl = 0;
		ahp->ah_tstl = 0;
	}

	ahp->ah_tpln = sizeof(struct in_addr);

	/* ah_tpa */
	KM_COPY((caddr_t)tip, cp, sizeof(struct in_addr));

	/*
	 * Finally, send the pdu to the vcc peer
	 */
	if (uniarp_print)
		uniarp_pdu_print(ivp, m, "send");
	err = atm_cm_cpcs_data(ivp->iv_arpconn, m);
	if (err) {
		/*
		 * Didn't make it
		 */
		KB_FREEALL(m);
		return (1);
	}

	return (0);
}

