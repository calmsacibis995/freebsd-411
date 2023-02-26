/*-
 * Copyright (c) 1994, Garrett Wollman
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
 * THIS SOFTWARE IS PROVIDED BY THE CONTRIBUTORS ``AS IS'' AND
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
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)$FreeBSD: src/lib/libc/net/gethostbynis.c,v 1.10.2.1 2000/10/01 16:39:47 nectar Exp $";
static char rcsid[] = "$FreeBSD: src/lib/libc/net/gethostbynis.c,v 1.10.2.1 2000/10/01 16:39:47 nectar Exp $";
#endif /* LIBC_SCCS and not lint */

#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#ifdef YP
#include <rpc/rpc.h>
#include <rpcsvc/yp_prot.h>
#include <rpcsvc/ypclnt.h>
#endif

#define	MAXALIASES	35
#define	MAXADDRS	35

extern int h_errno;

#ifdef YP
static char *host_aliases[MAXALIASES];
static char hostaddr[MAXADDRS];
static char *host_addrs[2];
#endif /* YP */

static struct hostent *
_gethostbynis(name, map, af)
	const char *name;
	char *map;
	int af;
{
#ifdef YP
	register char *cp, **q;
	char *result;
	int resultlen,size;
	static struct hostent h;
	static char *domain = (char *)NULL;
	static char ypbuf[YPMAXRECORD + 2];

	switch(af) {
	case AF_INET:
		size = NS_INADDRSZ;
		break;
	default:
	case AF_INET6:
		size = NS_IN6ADDRSZ;
		errno = EAFNOSUPPORT;
		h_errno = NETDB_INTERNAL;
		return NULL;
	}

	if (domain == (char *)NULL)
		if (yp_get_default_domain (&domain)) {
			h_errno = NETDB_INTERNAL;
			return ((struct hostent *)NULL);
		}

	if (yp_match(domain, map, name, strlen(name), &result, &resultlen)) {
		h_errno = HOST_NOT_FOUND;
		return ((struct hostent *)NULL);
	}

	/* avoid potential memory leak */
	bcopy((char *)result, (char *)&ypbuf, resultlen);
	ypbuf[resultlen] = '\0';
	free(result);
	result = (char *)&ypbuf;

	if ((cp = index(result, '\n')))
		*cp = '\0';

	cp = strpbrk(result, " \t");
	*cp++ = '\0';
	h.h_addr_list = host_addrs;
	h.h_addr = hostaddr;
	*((u_long *)h.h_addr) = inet_addr(result);
	h.h_length = size;
	h.h_addrtype = AF_INET;
	while (*cp == ' ' || *cp == '\t')
		cp++;
	h.h_name = cp;
	q = h.h_aliases = host_aliases;
	cp = strpbrk(cp, " \t");
	if (cp != NULL)
		*cp++ = '\0';
	while (cp && *cp) {
		if (*cp == ' ' || *cp == '\t') {
			cp++;
			continue;
		}
		if (q < &host_aliases[MAXALIASES - 1])
			*q++ = cp;
		cp = strpbrk(cp, " \t");
		if (cp != NULL)
			*cp++ = '\0';
	}
	*q = NULL;
	return (&h);
#else
	return (NULL);
#endif /* YP */
}

struct hostent *
_gethostbynisname(name, af)
	const char *name;
	int af;
{
	return _gethostbynis(name, "hosts.byname", af);
}

struct hostent *
_gethostbynisaddr(addr, len, af)
	const char *addr;
	int len;
	int af;
{
	return _gethostbynis(inet_ntoa(*(struct in_addr *)addr),"hosts.byaddr", af);
}
