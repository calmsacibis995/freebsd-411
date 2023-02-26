/*-
 * Copyright (c) 2000 Brian Somers <brian@Awfulhak.org>
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
 * $FreeBSD: src/sys/net/intrq.c,v 1.3 2000/01/29 16:13:08 peter Exp $
 */

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/netisr.h>
#include <net/intrq.h>

#ifdef __i386__
#include <netatm/kern_include.h>	/* XXX overkill, fixme! */
#endif

/*
 * If the appropriate intrq_present variable is zero, don't use
 * the queue (as it'll never get processed).
 * When defined, each of the network stacks declares their own
 * *intrq_present variable to be non-zero.
 */
const int	atintrq1_present;
const int	atintrq2_present;
#ifdef NETISR_ATM
const int	atmintrq_present;
#endif
const int	ipintrq_present;
const int	ip6intrq_present;
const int	ipxintrq_present;
const int	natmintrq_present;
const int	nsintrq_present;

struct ifqueue	atintrq1;
struct ifqueue	atintrq2;
#ifdef NETISR_ATM
struct ifqueue	atm_intrq;
#endif
struct ifqueue	ipintrq;
struct ifqueue	ip6intrq;
struct ifqueue	ipxintrq;
struct ifqueue	natmintrq;
struct ifqueue	nsintrq;


static const struct {
	sa_family_t family;
	struct ifqueue *q;
	int const *present;
	int isr;
} queue[] = {
#ifdef NETISR_ATM
	{ AF_ATM, &atm_intrq, &atmintrq_present, NETISR_ATM },
#endif
	{ AF_INET, &ipintrq, &ipintrq_present, NETISR_IP },
	{ AF_INET6, &ip6intrq, &ip6intrq_present, NETISR_IPV6 },
	{ AF_IPX, &ipxintrq, &ipxintrq_present, NETISR_IPX },
	{ AF_NATM, &natmintrq, &natmintrq_present, NETISR_NATM },
	{ AF_APPLETALK, &atintrq2, &atintrq2_present, NETISR_ATALK },
	{ AF_NS, &nsintrq, &nsintrq_present, NETISR_NS }
};

int
family_enqueue(family, m)
	sa_family_t family;
	struct mbuf *m;
{
	int entry, s;

	for (entry = 0; entry < sizeof queue / sizeof queue[0]; entry++)
		if (queue[entry].family == family) {
			if (queue[entry].present) {
				s = splimp();
				if (IF_QFULL(queue[entry].q)) {
					IF_DROP(queue[entry].q);
					splx(s);
					m_freem(m);
					return ENOBUFS;
				}
				IF_ENQUEUE(queue[entry].q, m);
				splx(s);
				schednetisr(queue[entry].isr);
				return 0;
			} else
				break;
		}

	m_freem(m);
	return EAFNOSUPPORT;
}
