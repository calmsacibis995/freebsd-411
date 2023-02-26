/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef lint
static const char rcsid[] =
"@(#) $Header: /tcpdump/master/tcpdump/print-pppoe.c,v 1.15 2001/07/05 18:54:17 guy Exp $ (LBL)";
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/param.h>
#include <sys/time.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <stdio.h>
#include <string.h>

#include "interface.h"
#include "addrtoname.h"
#include "ppp.h"
#include "ethertype.h"
#include "ether.h"
#include "extract.h"			/* must come after interface.h */

/* Codes */
enum {
	PPPOE_PADI = 0x09,
	PPPOE_PADO = 0x07,
	PPPOE_PADR = 0x19,
	PPPOE_PADS = 0x65,
	PPPOE_PADT = 0xa7
};

static struct tok pppoecode2str[] = {
	{ PPPOE_PADI, "PADI" },
	{ PPPOE_PADO, "PADO" },
	{ PPPOE_PADR, "PADR" },
	{ PPPOE_PADS, "PADS" },
	{ PPPOE_PADT, "PADT" },
	{ 0, "" }, /* PPP Data */
	{ 0, NULL }
};

/* Tags */
enum {
	PPPOE_EOL = 0,
	PPPOE_SERVICE_NAME = 0x0101,
	PPPOE_AC_NAME = 0x0102,
	PPPOE_HOST_UNIQ = 0x0103,
	PPPOE_AC_COOKIE = 0x0104,
	PPPOE_VENDOR = 0x0105,
	PPPOE_RELAY_SID = 0x0110,
	PPPOE_SERVICE_NAME_ERROR = 0x0201,
	PPPOE_AC_SYSTEM_ERROR = 0x0202,
	PPPOE_GENERIC_ERROR = 0x0203
};

static struct tok pppoetag2str[] = {
	{ PPPOE_EOL, "EOL" },
	{ PPPOE_SERVICE_NAME, "Service-Name" },
	{ PPPOE_AC_NAME, "AC-Name" },
	{ PPPOE_HOST_UNIQ, "Host-Uniq" },
	{ PPPOE_AC_COOKIE, "AC-Cookie" },
	{ PPPOE_VENDOR, "Vendor-Specific" },
	{ PPPOE_RELAY_SID, "Relay-Session-ID" },
	{ PPPOE_SERVICE_NAME_ERROR, "Service-Name-Error" },
	{ PPPOE_AC_SYSTEM_ERROR, "AC-System-Error" },
	{ PPPOE_GENERIC_ERROR, "Generic-Error" },
	{ 0, NULL }
};

#define PPPOE_HDRLEN 6

void
pppoe_if_print(u_char *user, const struct pcap_pkthdr *h,
	     register const u_char *p)
{
	register u_int length = h->len;
	register u_int caplen = h->caplen;

	++infodelay;
	ts_print(&h->ts);

	/*
	 * Some printers want to get back at the link level addresses,
	 * and/or check that they're not walking off the end of the packet.
	 * Rather than pass them all the way down, we set these globals.
	 */
	packetp = p;
	snapend = p + caplen;

	pppoe_print(p, length);
	putchar('\n');
	--infodelay;
	if (infoprint)
		info(0);
}

void
pppoe_print(register const u_char *bp, u_int length)
{
	u_short pppoe_ver, pppoe_type, pppoe_code, pppoe_sessionid, pppoe_length;
	const u_char *pppoe_packet, *pppoe_payload;

	pppoe_packet = bp;
	if (pppoe_packet > snapend) {
		printf("[|pppoe]");
		return;
	}

	pppoe_ver  = (pppoe_packet[0] & 0xF0) >> 4;
	pppoe_type  = (pppoe_packet[0] & 0x0F);
	pppoe_code = pppoe_packet[1];
	pppoe_sessionid = EXTRACT_16BITS(pppoe_packet + 2);
	pppoe_length    = EXTRACT_16BITS(pppoe_packet + 4);
	pppoe_payload = pppoe_packet + PPPOE_HDRLEN;

	if (snapend < pppoe_payload) {
		printf(" truncated PPPoE");
		return;
	}

	if (pppoe_ver != 1) {
		printf(" [ver %d]",pppoe_ver);
	}
	if (pppoe_type != 1) {
		printf(" [type %d]",pppoe_type);
	}

	printf("PPPoE %s", tok2str(pppoecode2str, "PAD-%x", pppoe_code));
	if (pppoe_code == PPPOE_PADI && pppoe_length > 1484 - PPPOE_HDRLEN) {
		printf(" [len %d!]",pppoe_length);
	}
	if (pppoe_sessionid) {
		printf(" [ses 0x%x]", pppoe_sessionid);
	}

	if (pppoe_payload + pppoe_length < snapend) {
#if 0
		const u_char *x = pppoe_payload + pppoe_length;
		printf(" [length %d (%d extra bytes)]",
		    pppoe_length, snapend - pppoe_payload - pppoe_length);
		default_print(x, snapend - x);
#endif
		snapend = pppoe_payload+pppoe_length;
	}

	if (pppoe_code) {
		/* PPP session packets don't contain tags */
		u_short tag_type = 0xffff, tag_len;
		const u_char *p = pppoe_payload;

		/*
		 * loop invariant:
		 * p points to next tag,
		 * tag_type is previous tag or 0xffff for first iteration
		 */
		while (tag_type && p + 4 < pppoe_payload + length &&
		    p + 4 < snapend) {
			tag_type = EXTRACT_16BITS(p);
			tag_len = EXTRACT_16BITS(p + 2);
			p += 4;
			/* p points to tag_value */

			if (tag_len) {
				int isascii = 1;
				const u_char *v = p;
				u_short l;

				for (v = p; v < p + tag_len; v++)
					if (*v >= 127 || *v < 32) {
						isascii = 0;
						break;
					}

				/* TODO print UTF8 decoded text */
				if (isascii) {
					l = (tag_len < 80 ? tag_len : 80);
					printf(" [%s \"%*.*s\"]",
					    tok2str(pppoetag2str, "TAG-0x%x", tag_type),
					    l, l, p);
				} else
					printf(" [%s UTF8]",
					    tok2str(pppoetag2str, "TAG-0x%x", tag_type));
			} else
				printf(" [%s]", tok2str(pppoetag2str,
				    "TAG-0x%x", tag_type));

			p += tag_len;
			/* p points to next tag */
		}
	} else {
		printf(" ");
		ppp_print(pppoe_payload, pppoe_length);
	}
	return;
}
