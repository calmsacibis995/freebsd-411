/*
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
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
 *
 * $FreeBSD: src/contrib/tcpdump/print-sunrpc.c,v 1.5.2.3 2003/03/03 05:11:03 fenner Exp $
 */

#ifndef lint
static const char rcsid[] =
    "@(#) $Header: /tcpdump/master/tcpdump/print-sunrpc.c,v 1.39.6.1 2002/06/01 23:51:16 guy Exp $ (LBL)";
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/param.h>
#include <sys/time.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <rpc/rpc.h>
#ifdef HAVE_RPC_RPCENT_H
#include <rpc/rpcent.h>
#endif
#include <rpc/pmap_prot.h>

#include <ctype.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>

#include "interface.h"
#include "addrtoname.h"

#include "ip.h"
#ifdef INET6
#include "ip6.h"
#endif

static struct tok proc2str[] = {
	{ PMAPPROC_NULL,	"null" },
	{ PMAPPROC_SET,		"set" },
	{ PMAPPROC_UNSET,	"unset" },
	{ PMAPPROC_GETPORT,	"getport" },
	{ PMAPPROC_DUMP,	"dump" },
	{ PMAPPROC_CALLIT,	"call" },
	{ 0,			NULL }
};

/* Forwards */
static char *progstr(u_int32_t);

void
sunrpcrequest_print(register const u_char *bp, register u_int length,
		    register const u_char *bp2)
{
	register const struct rpc_msg *rp;
	register const struct ip *ip;
#ifdef INET6
	register const struct ip6_hdr *ip6;
#endif
	u_int32_t x;
	char srcid[20], dstid[20];	/*fits 32bit*/

	rp = (struct rpc_msg *)bp;

	if (!nflag) {
		snprintf(srcid, sizeof(srcid), "0x%x",
		    (u_int32_t)ntohl(rp->rm_xid));
		strlcpy(dstid, "sunrpc", sizeof(dstid));
	} else {
		snprintf(srcid, sizeof(srcid), "0x%x",
		    (u_int32_t)ntohl(rp->rm_xid));
		snprintf(dstid, sizeof(dstid), "0x%x", PMAPPORT);
	}

	switch (IP_V((struct ip *)bp2)) {
	case 4:
		ip = (struct ip *)bp2;
		printf("%s.%s > %s.%s: %d",
		    ipaddr_string(&ip->ip_src), srcid,
		    ipaddr_string(&ip->ip_dst), dstid, length);
		break;
#ifdef INET6
	case 6:
		ip6 = (struct ip6_hdr *)bp2;
		printf("%s.%s > %s.%s: %d",
		    ip6addr_string(&ip6->ip6_src), srcid,
		    ip6addr_string(&ip6->ip6_dst), dstid, length);
		break;
#endif
	default:
		printf("%s.%s > %s.%s: %d", "?", srcid, "?", dstid, length);
		break;
	}

	printf(" %s", tok2str(proc2str, " proc #%u",
	    (u_int32_t)ntohl(rp->rm_call.cb_proc)));
	x = ntohl(rp->rm_call.cb_rpcvers);
	if (x != 2)
		printf(" [rpcver %u]", x);

	switch (ntohl(rp->rm_call.cb_proc)) {

	case PMAPPROC_SET:
	case PMAPPROC_UNSET:
	case PMAPPROC_GETPORT:
	case PMAPPROC_CALLIT:
		x = ntohl(rp->rm_call.cb_prog);
		if (!nflag)
			printf(" %s", progstr(x));
		else
			printf(" %u", x);
		printf(".%u", (u_int32_t)ntohl(rp->rm_call.cb_vers));
		break;
	}
}

static char *
progstr(prog)
	u_int32_t prog;
{
	register struct rpcent *rp;
	static char buf[32];
	static int lastprog = 0;

	if (lastprog != 0 && prog == lastprog)
		return (buf);
	rp = getrpcbynumber(prog);
	if (rp == NULL)
		(void) snprintf(buf, sizeof(buf), "#%u", prog);
	else
		strlcpy(buf, rp->r_name, sizeof(buf));
	return (buf);
}
