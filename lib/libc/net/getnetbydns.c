/*-
 * Copyright (c) 1985, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * -
 * Portions Copyright (c) 1993 by Digital Equipment Corporation.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies, and that
 * the name of Digital Equipment Corporation not be used in advertising or
 * publicity pertaining to distribution of the document or software without
 * specific, written prior permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND DIGITAL EQUIPMENT CORP. DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS.   IN NO EVENT SHALL DIGITAL EQUIPMENT
 * CORPORATION BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 * -
 * --Copyright--
 */
/* Portions Copyright (c) 1993 Carlos Leandro and Rui Salgueiro
 *	Dep. Matematica Universidade de Coimbra, Portugal, Europe
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)gethostnamadr.c	8.1 (Berkeley) 6/4/93";
static char rcsid[] = "$FreeBSD: src/lib/libc/net/getnetbydns.c,v 1.13.2.5 2004/02/13 20:59:43 dds Exp $";
#endif /* LIBC_SCCS and not lint */

#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <resolv.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>

#include "res_config.h"

extern int h_errno;

#define BYADDR 0
#define BYNAME 1
#define	MAXALIASES	35

#define MAXPACKET	(64*1024)

typedef union {
	HEADER	hdr;
	u_char	buf[MAXPACKET];
} querybuf;

typedef union {
	long	al;
	char	ac;
} align;

/*
 * Reverse the order of first four dotted entries of in.
 * Out must contain space for at least strlen(in) characters.
 * The result does not include any leading 0s of in.
 */
static void
ipreverse(char *in, char *out)
{
	char *pos[4];
	int len[4];
	char *p, *start;
	int i = 0;
	int leading = 1;

	/* Fill-in element positions and lengths: pos[], len[]. */
	start = p = in;
	for (;;) {
		if (*p == '.' || *p == '\0') {
			/* Leading 0? */
			if (leading && p - start == 1 && *start == '0')
				len[i] = 0;
			else {
				len[i] = p - start;
				leading = 0;
			}
			pos[i] = start;
			start = p + 1;
			i++;
		}
		if (i == 4)
			break;
		if (*p == 0) {
			for (; i < 4; i++) {
				pos[i] = p;
				len[i] = 0;
			}
			break;
		}
		p++;
	}

	/* Copy the entries in reverse order */
	p = out;
	leading = 1;
	for (i = 3; i >= 0; i--) {
		memcpy(p, pos[i], len[i]);
		if (len[i])
			leading = 0;
		p += len[i];
		/* Need a . separator? */
		if (!leading && i > 0 && len[i - 1])
			*p++ = '.';
	}
	*p = '\0';
}

static struct netent *
getnetanswer(answer, anslen, net_i)
	querybuf *answer;
	int anslen;
	int net_i;
{

	register HEADER *hp;
	register u_char *cp;
	register int n;
	u_char *eom;
	int type, class, buflen, ancount, qdcount, haveanswer;
	char aux[MAXHOSTNAMELEN];
	char *in, *bp, **ap;
static	struct netent net_entry;
static	char *net_aliases[MAXALIASES], netbuf[PACKETSZ];
static	char ans[MAXHOSTNAMELEN];

	/*
	 * find first satisfactory answer
	 *
	 *      answer --> +------------+  ( MESSAGE )
	 *		   |   Header   |
	 *		   +------------+
	 *		   |  Question  | the question for the name server
	 *		   +------------+
	 *		   |   Answer   | RRs answering the question
	 *		   +------------+
	 *		   | Authority  | RRs pointing toward an authority
	 *		   | Additional | RRs holding additional information
	 *		   +------------+
	 */
	eom = answer->buf + anslen;
	hp = &answer->hdr;
	ancount = ntohs(hp->ancount); /* #/records in the answer section */
	qdcount = ntohs(hp->qdcount); /* #/entries in the question section */
	bp = netbuf;
	buflen = sizeof(netbuf);
	cp = answer->buf + HFIXEDSZ;
	if (!qdcount) {
		if (hp->aa)
			h_errno = HOST_NOT_FOUND;
		else
			h_errno = TRY_AGAIN;
		return (NULL);
	}
	while (qdcount-- > 0)
		cp += __dn_skipname(cp, eom) + QFIXEDSZ;
	ap = net_aliases;
	*ap = NULL;
	net_entry.n_aliases = net_aliases;
	haveanswer = 0;
	while (--ancount >= 0 && cp < eom) {
		n = dn_expand(answer->buf, eom, cp, bp, buflen);
		if ((n < 0) || !res_dnok(bp))
			break;
		cp += n;
		ans[0] = '\0';
		(void)strncpy(&ans[0], bp, sizeof(ans) - 1);
		ans[sizeof(ans) - 1] = '\0';
		GETSHORT(type, cp);
		GETSHORT(class, cp);
		cp += INT32SZ;		/* TTL */
		GETSHORT(n, cp);
		if (class == C_IN && type == T_PTR) {
			n = dn_expand(answer->buf, eom, cp, bp, buflen);
			if ((n < 0) || !res_hnok(bp)) {
				cp += n;
				return (NULL);
			}
			cp += n; 
			*ap++ = bp;
			n = strlen(bp) + 1;
			bp += n;
			buflen -= n;
			net_entry.n_addrtype =
				(class == C_IN) ? AF_INET : AF_UNSPEC;
			haveanswer++;
		}
	}
	if (haveanswer) {
		*ap = NULL;
		switch (net_i) {
		case BYADDR:
			net_entry.n_name = *net_entry.n_aliases;
			net_entry.n_net = 0L;
			break;
		case BYNAME:
			in = *net_entry.n_aliases;
			net_entry.n_name = &ans[0];
			if (strlen(in) + 1 > sizeof(aux)) {
				h_errno = NETDB_INTERNAL;
				errno = EFAULT;
				return (NULL);
			}
			ipreverse(in, aux);
			net_entry.n_net = inet_network(aux);
			break;
		}
		net_entry.n_aliases++;
		return (&net_entry);
	}
	h_errno = TRY_AGAIN;
	return (NULL);
}

struct netent *
_getnetbydnsaddr(net, net_type)
	register unsigned long net;
	register int net_type;
{
	unsigned int netbr[4];
	int nn, anslen;
	querybuf *buf;
	char qbuf[MAXDNAME];
	unsigned long net2;
	struct netent *net_entry;

	if (net_type != AF_INET)
		return (NULL);

	for (nn = 4, net2 = net; net2; net2 >>= 8)
		netbr[--nn] = net2 & 0xff;
	switch (nn) {
	case 3: 	/* Class A */
		sprintf(qbuf, "0.0.0.%u.in-addr.arpa", netbr[3]);
		break;
	case 2: 	/* Class B */
		sprintf(qbuf, "0.0.%u.%u.in-addr.arpa", netbr[3], netbr[2]);
		break;
	case 1: 	/* Class C */
		sprintf(qbuf, "0.%u.%u.%u.in-addr.arpa", netbr[3], netbr[2],
		    netbr[1]);
		break;
	case 0: 	/* Class D - E */
		sprintf(qbuf, "%u.%u.%u.%u.in-addr.arpa", netbr[3], netbr[2],
		    netbr[1], netbr[0]);
		break;
	}
	if ((buf = malloc(sizeof(*buf))) == NULL) {
		h_errno = NETDB_INTERNAL;
		return (NULL);
	}
	anslen = res_query(qbuf, C_IN, T_PTR, (u_char *)buf, sizeof(*buf));
	if (anslen < 0) {
		free(buf);
#ifdef DEBUG
		if (_res.options & RES_DEBUG)
			printf("res_search failed\n");
#endif
		return (NULL);
	} else if (anslen > sizeof(*buf)) {
		free(buf);
#ifdef DEBUG
		if (_res.options & RES_DEBUG)
			printf("res_search static buffer too small\n");
#endif
		return (NULL);
	}
	net_entry = getnetanswer(buf, anslen, BYADDR);
	free(buf);
	if (net_entry) {
		unsigned u_net = net;	/* maybe net should be unsigned ? */

		/* Strip trailing zeros */
		while ((u_net & 0xff) == 0 && u_net != 0)
			u_net >>= 8;
		net_entry->n_net = u_net;
		return (net_entry);
	}
	return (NULL);
}

struct netent *
_getnetbydnsname(net)
	register const char *net;
{
	int anslen;
	querybuf *buf;
	char qbuf[MAXDNAME];
	struct netent *net_entry;

	if ((_res.options & RES_INIT) == 0 && res_init() == -1) {
		h_errno = NETDB_INTERNAL;
		return (NULL);
	}
	if ((buf = malloc(sizeof(*buf))) == NULL) {
		h_errno = NETDB_INTERNAL;
		return (NULL);
	}
	strncpy(qbuf, net, sizeof(qbuf) - 1);
	qbuf[sizeof(qbuf) - 1] = '\0';
	anslen = res_search(qbuf, C_IN, T_PTR, (u_char *)buf, sizeof(*buf));
	if (anslen < 0) {
		free(buf);
#ifdef DEBUG
		if (_res.options & RES_DEBUG)
			printf("res_search failed\n");
#endif
		return (NULL);
	} else if (anslen > sizeof(*buf)) {
		free(buf);
#ifdef DEBUG
		if (_res.options & RES_DEBUG)
			printf("res_search static buffer too small\n");
#endif
		return (NULL);
	}
	net_entry = getnetanswer(buf, anslen, BYNAME);
	free(buf);
	return net_entry;
}

void
_setnetdnsent(stayopen)
	int stayopen;
{
	if (stayopen)
		_res.options |= RES_STAYOPEN | RES_USEVC;
}

void
_endnetdnsent()
{
	_res.options &= ~(RES_STAYOPEN | RES_USEVC);
	res_close();
}
