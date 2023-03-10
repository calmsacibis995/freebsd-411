/*
 * Copyright (C) 1993-2001 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */

/*
                                            icmp type
 lnth proto         source     destination   src port   dst port

etherfind -n

   60  tcp   128.250.20.20  128.250.133.13       2419     telnet

etherfind -n -t

 0.32    91   04    131.170.1.10  128.250.133.13
 0.33   566  udp  128.250.37.155   128.250.133.3        901        901
*/
#if defined(__sgi) && (IRIX > 602)
# include <sys/ptimers.h>
#endif
#include <stdio.h>
#include <string.h>
#if !defined(__SVR4) && !defined(__GNUC__)
#include <strings.h>
#endif
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/in_systm.h>
#ifndef	linux
#include <netinet/ip_var.h>
#endif
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <net/if.h>
#include <netdb.h>
#include "ip_compat.h"
#include <netinet/tcpip.h>
#include "ipf.h"
#include "ipt.h"

#if !defined(lint)
static const char sccsid[] = "@(#)ipft_ef.c	1.6 2/4/96 (C)1995 Darren Reed";
static const char rcsid[] = "@(#)$Id: ipft_ef.c,v 2.2.2.5 2003/05/19 12:02:35 darrenr Exp $";
#endif

static	int	etherf_open __P((char *));
static	int	etherf_close __P((void));
static	int	etherf_readip __P((char *, int, char **, int *));

struct	ipread	etherf = { etherf_open, etherf_close, etherf_readip };

static	FILE	*efp = NULL;
static	int	efd = -1;


static	int	etherf_open(fname)
char	*fname;
{
	if (efd != -1)
		return efd;

	if (!strcmp(fname, "-")) {
		efd = 0;
		efp = stdin;
	} else {
		efd = open(fname, O_RDONLY);
		efp = fdopen(efd, "r");
	}
	return efd;
}


static	int	etherf_close()
{
	return close(efd);
}


static	int	etherf_readip(buf, cnt, ifn, dir)
char	*buf, **ifn;
int	cnt, *dir;
{
	struct	tcpiphdr pkt;
	ip_t	*ip = (ip_t *)&pkt;
	struct	protoent *p = NULL;
	char	src[16], dst[16], sprt[16], dprt[16];
	char	lbuf[128], len[8], prot[8], time[8], *s;
	int	slen, extra = 0, i;

	if (!fgets(lbuf, sizeof(lbuf) - 1, efp))
		return 0;

	if ((s = strchr(lbuf, '\n')))
		*s = '\0';
	lbuf[sizeof(lbuf)-1] = '\0';

	bzero(&pkt, sizeof(pkt));

	if (sscanf(lbuf, "%7s %7s %15s %15s %15s %15s", len, prot, src, dst,
		   sprt, dprt) != 6)
		if (sscanf(lbuf, "%7s %7s %7s %15s %15s %15s %15s", time,
			   len, prot, src, dst, sprt, dprt) != 7)
			return -1;

	ip->ip_p = atoi(prot);
	if (ip->ip_p == 0) {
		if (!(p = getprotobyname(prot)))
			return -1;
		ip->ip_p = p->p_proto;
	}

	switch (ip->ip_p) {
	case IPPROTO_TCP :
	case IPPROTO_UDP :
		s = strtok(NULL, " :");
		ip->ip_len += atoi(s);
		if (p->p_proto == IPPROTO_TCP)
			extra = sizeof(struct tcphdr);
		else if (p->p_proto == IPPROTO_UDP)
			extra = sizeof(struct udphdr);
		break;
#ifdef	IGMP
	case IPPROTO_IGMP :
		extra = sizeof(struct igmp);
		break;
#endif
	case IPPROTO_ICMP :
		extra = sizeof(struct icmp);
		break;
	default :
		break;
	}

	(void) inet_aton(src, &ip->ip_src);
	(void) inet_aton(dst, &ip->ip_dst);
	ip->ip_len = atoi(len);
	ip->ip_hl = sizeof(ip_t);

	slen = ip->ip_hl + extra;
	i = MIN(cnt, slen);
	bcopy((char *)&pkt, buf, i);
	return i;
}
