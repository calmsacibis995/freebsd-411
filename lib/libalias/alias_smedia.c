/*
 * alias_smedia.c
 *
 * Copyright (c) 2000 Whistle Communications, Inc.
 * All rights reserved.
 *
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 *
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * Copyright (c) 2000  Junichi SATOH <junichi@astec.co.jp>
 *                                   <junichi@junichi.org>
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
 * Authors: Erik Salander <erik@whistle.com>
 *          Junichi SATOH <junichi@astec.co.jp>
 *                        <junichi@junichi.org>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/lib/libalias/alias_smedia.c,v 1.1.2.5 2003/06/27 08:37:23 ru Exp $");

/*
   Alias_smedia.c is meant to contain the aliasing code for streaming media
   protocols.  It performs special processing for RSTP sessions under TCP.
   Specifically, when a SETUP request is sent by a client, or a 200 reply
   is sent by a server, it is intercepted and modified.  The address is
   changed to the gateway machine and an aliasing port is used.

   More specifically, the "client_port" configuration parameter is
   parsed for SETUP requests.  The "server_port" configuration parameter is
   parsed for 200 replies eminating from a server.  This is intended to handle
   the unicast case.

   RTSP also allows a redirection of a stream to another client by using the
   "destination" configuration parameter.  The destination config parm would
   indicate a different IP address.  This function is NOT supported by the
   RTSP translation code below.

   The RTSP multicast functions without any address translation intervention.

   For this routine to work, the SETUP/200 must fit entirely
   into a single TCP packet.  This is typically the case, but exceptions
   can easily be envisioned under the actual specifications.

   Probably the most troubling aspect of the approach taken here is
   that the new SETUP/200 will typically be a different length, and
   this causes a certain amount of bookkeeping to keep track of the
   changes of sequence and acknowledgment numbers, since the client
   machine is totally unaware of the modification to the TCP stream.

   Initial version:  May, 2000 (eds)
*/

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include "alias_local.h"

#define RTSP_CONTROL_PORT_NUMBER_1 554
#define RTSP_CONTROL_PORT_NUMBER_2 7070
#define RTSP_PORT_GROUP            2

#define ISDIGIT(a) (((a) >= '0') && ((a) <= '9'))

static int
search_string(char *data, int dlen, const char *search_str)
{
    int i, j, k;
    int search_str_len;

    search_str_len = strlen(search_str);
    for (i = 0; i < dlen - search_str_len; i++) {
	for (j = i, k = 0; j < dlen - search_str_len; j++, k++) {
	    if (data[j] != search_str[k] &&
		data[j] != search_str[k] - ('a' - 'A')) {
		break;
	    }
	    if (k == search_str_len - 1) {
		return j + 1;
	    }
	}
    }
    return -1;
}

static int
alias_rtsp_out(struct ip *pip,
		   struct alias_link *link,
		   char *data,
		   const char *port_str)
{
    int     hlen, tlen, dlen;
    struct tcphdr *tc;
    int     i, j, pos, state, port_dlen, new_dlen, delta;
    u_short p[2], new_len;
    u_short sport, eport, base_port;
    u_short salias = 0, ealias = 0, base_alias = 0;
    const char *transport_str = "transport:";
    char    newdata[2048], *port_data, *port_newdata, stemp[80];
    int     links_created = 0, pkt_updated = 0;
    struct alias_link *rtsp_link = NULL;
    struct in_addr null_addr;

    /* Calculate data length of TCP packet */
    tc = (struct tcphdr *) ((char *) pip + (pip->ip_hl << 2));
    hlen = (pip->ip_hl + tc->th_off) << 2;
    tlen = ntohs(pip->ip_len);
    dlen = tlen - hlen;

    /* Find keyword, "Transport: " */
    pos = search_string(data, dlen, transport_str);
    if (pos < 0) {
	return -1;
    }
    port_data = data + pos;
    port_dlen = dlen - pos;

    memcpy(newdata, data, pos);
    port_newdata = newdata + pos;

    while (port_dlen > strlen(port_str)) {
	/* Find keyword, appropriate port string */
	pos = search_string(port_data, port_dlen, port_str);
	if (pos < 0) {
	    break;
	}

	memcpy (port_newdata, port_data, pos + 1);
	port_newdata += (pos + 1);

	p[0] = p[1] = 0;
	sport = eport = 0;
	state = 0;
	for (i = pos; i < port_dlen; i++) {
	    switch(state) {
	    case 0:
		if (port_data[i] == '=') {
		    state++;
		}
		break;
	    case 1:
		if (ISDIGIT(port_data[i])) {
		    p[0] = p[0] * 10 + port_data[i] - '0';
		} else {
		    if (port_data[i] == ';') {
			state = 3;
		    }
		    if (port_data[i] == '-') {
			state++;
		    }
		}
		break;
	    case 2:
		if (ISDIGIT(port_data[i])) {
		    p[1] = p[1] * 10 + port_data[i] - '0';
		} else {
		    state++;
		}
		break;
	    case 3:
		base_port = p[0];
		sport = htons(p[0]);
		eport = htons(p[1]);

		if (!links_created) {

	  	  links_created = 1;
		  /* Find an even numbered port number base that
		     satisfies the contiguous number of ports we need  */
		  null_addr.s_addr = 0;
		  if (0 == (salias = FindNewPortGroup(null_addr,
	       			    FindAliasAddress(pip->ip_src),
				    sport, 0,
				    RTSP_PORT_GROUP,
				    IPPROTO_UDP, 1))) {
#ifdef DEBUG
		    fprintf(stderr,
		    "PacketAlias/RTSP: Cannot find contiguous RTSP data ports\n");
#endif
		  } else {

  		    base_alias = ntohs(salias);
		    for (j = 0; j < RTSP_PORT_GROUP; j++) {
		      /* Establish link to port found in RTSP packet */
		      rtsp_link = FindRtspOut(GetOriginalAddress(link), null_addr,
                                htons(base_port + j), htons(base_alias + j),
                                IPPROTO_UDP);
		      if (rtsp_link != NULL) {
#ifndef NO_FW_PUNCH
		        /* Punch hole in firewall */
		        PunchFWHole(rtsp_link);
#endif
		      } else {
#ifdef DEBUG
		        fprintf(stderr,
		        "PacketAlias/RTSP: Cannot allocate RTSP data ports\n");
#endif
		        break;
		      }
		    }
		  }
                  ealias = htons(base_alias + (RTSP_PORT_GROUP - 1));
		}

		if (salias && rtsp_link) {

		  pkt_updated = 1;

	          /* Copy into IP packet */
		  sprintf(stemp, "%d", ntohs(salias));
		  memcpy(port_newdata, stemp, strlen(stemp));
		  port_newdata += strlen(stemp);

		  if (eport != 0) {
		    *port_newdata = '-';
		    port_newdata++;

		    /* Copy into IP packet */
		    sprintf(stemp, "%d", ntohs(ealias));
		    memcpy(port_newdata, stemp, strlen(stemp));
		    port_newdata += strlen(stemp);
		  }

	          *port_newdata = ';';
		  port_newdata++;
		}
		state++;
		break;
	    }
	    if (state > 3) {
		break;
	    }
	}
	port_data += i;
	port_dlen -= i;
    }

    if (!pkt_updated)
      return -1;

    memcpy (port_newdata, port_data, port_dlen);
    port_newdata += port_dlen;
    *port_newdata = '\0';

    /* Create new packet */
    new_dlen = port_newdata - newdata;
    memcpy (data, newdata, new_dlen);

    SetAckModified(link);
    delta = GetDeltaSeqOut(pip, link);
    AddSeq(pip, link, delta + new_dlen - dlen);

    new_len = htons(hlen + new_dlen);
    DifferentialChecksum(&pip->ip_sum,
			 &new_len,
			 &pip->ip_len,
			 1);
    pip->ip_len = new_len;

    tc->th_sum = 0;
    tc->th_sum = TcpChecksum(pip);

    return 0;
}

/* Support the protocol used by early versions of RealPlayer */

static int
alias_pna_out(struct ip *pip,
		  struct alias_link *link,
		  char *data,
		  int dlen)
{
    struct alias_link *pna_links;
    u_short msg_id, msg_len;
    char    *work;
    u_short alias_port, port;
    struct  tcphdr *tc;

    work = data;
    work += 5;
    while (work + 4 < data + dlen) {
	memcpy(&msg_id, work, 2);
	work += 2;
	memcpy(&msg_len, work, 2);
	work += 2;
	if (ntohs(msg_id) == 0) {
	    /* end of options */
	    return 0;
	}
	if ((ntohs(msg_id) == 1) || (ntohs(msg_id) == 7)) {
	    memcpy(&port, work, 2);
	    pna_links = FindUdpTcpOut(pip->ip_src, GetDestAddress(link),
				      port, 0, IPPROTO_UDP, 1);
	    if (pna_links != NULL) {
#ifndef NO_FW_PUNCH
		/* Punch hole in firewall */
		PunchFWHole(pna_links);
#endif
		tc = (struct tcphdr *) ((char *) pip + (pip->ip_hl << 2));
		alias_port = GetAliasPort(pna_links);
		memcpy(work, &alias_port, 2);

		/* Compute TCP checksum for revised packet */
		tc->th_sum = 0;
		tc->th_sum = TcpChecksum(pip);
	    }
	}
	work += ntohs(msg_len);
    }

    return 0;
}

void
AliasHandleRtspOut(struct ip *pip, struct alias_link *link, int maxpacketsize)
{
    int    hlen, tlen, dlen;
    struct tcphdr *tc;
    char   *data;
    const  char *setup = "SETUP", *pna = "PNA", *str200 = "200";
    const  char *okstr = "OK", *client_port_str = "client_port";
    const  char *server_port_str = "server_port";
    int    i, parseOk;

    tc = (struct tcphdr *)((char *)pip + (pip->ip_hl << 2));
    hlen = (pip->ip_hl + tc->th_off) << 2;
    tlen = ntohs(pip->ip_len);
    dlen = tlen - hlen;

    data = (char*)pip;
    data += hlen;

    /* When aliasing a client, check for the SETUP request */
    if ((ntohs(tc->th_dport) == RTSP_CONTROL_PORT_NUMBER_1) ||
      (ntohs(tc->th_dport) == RTSP_CONTROL_PORT_NUMBER_2)) {

      if (dlen >= strlen(setup)) {
        if (memcmp(data, setup, strlen(setup)) == 0) {
	    alias_rtsp_out(pip, link, data, client_port_str);
	    return;
	}
      }
      if (dlen >= strlen(pna)) {
	if (memcmp(data, pna, strlen(pna)) == 0) {
	    alias_pna_out(pip, link, data, dlen);
	}
      }

    } else {

      /* When aliasing a server, check for the 200 reply
         Accomodate varying number of blanks between 200 & OK */

      if (dlen >= strlen(str200)) {

        for (parseOk = 0, i = 0;
             i <= dlen - strlen(str200);
             i++) {
          if (memcmp(&data[i], str200, strlen(str200)) == 0) {
            parseOk = 1;
            break;
          }
        }
        if (parseOk) {

          i += strlen(str200);        /* skip string found */
          while(data[i] == ' ')       /* skip blank(s) */
	    i++;

          if ((dlen - i) >= strlen(okstr)) {

            if (memcmp(&data[i], okstr, strlen(okstr)) == 0)
              alias_rtsp_out(pip, link, data, server_port_str);

          }
        }
      }
    }
}
