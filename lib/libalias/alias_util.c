/*-
 * Copyright (c) 2001 Charles Mott <cm@linktel.net>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/lib/libalias/alias_util.c,v 1.4.2.4 2003/06/27 08:37:23 ru Exp $");


/*
    Alias_util.c contains general utilities used by other functions
    in the packet aliasing module.  At the moment, there are functions
    for computing IP header and TCP packet checksums.

    The checksum routines are based upon example code in a Unix networking
    text written by Stevens (sorry, I can't remember the title -- but
    at least this is a good author).

    Initial Version:  August, 1996  (cjm)

    Version 1.7:  January 9, 1997
         Added differential checksum update function.
*/

/*
Note: the checksum routines assume that the actual checksum word has
been zeroed out.  If the checksum word is filled with the proper value,
then these routines will give a result of zero (useful for testing
purposes);
*/

#include <sys/types.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#include "alias.h"
#include "alias_local.h"

u_short
PacketAliasInternetChecksum(u_short *ptr, int nbytes)
{
    int sum, oddbyte;

    sum = 0;
    while (nbytes > 1)
    {
        sum += *ptr++;
        nbytes -= 2;
    }
    if (nbytes == 1)
    {
        oddbyte = 0;
        ((u_char *) &oddbyte)[0] = *(u_char *) ptr;
        ((u_char *) &oddbyte)[1] = 0;
        sum += oddbyte;
    }
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return(~sum);
}

u_short
IpChecksum(struct ip *pip)
{
    return( PacketAliasInternetChecksum((u_short *) pip,
            (pip->ip_hl << 2)) );

}

u_short
TcpChecksum(struct ip *pip)
{
    u_short *ptr;
    struct tcphdr *tc;
    int nhdr, ntcp, nbytes;
    int sum, oddbyte;

    nhdr = pip->ip_hl << 2;
    ntcp = ntohs(pip->ip_len) - nhdr;

    tc = (struct tcphdr *) ((char *) pip + nhdr);
    ptr = (u_short *) tc;

/* Add up TCP header and data */
    nbytes = ntcp;
    sum = 0;
    while (nbytes > 1)
    {
        sum += *ptr++;
        nbytes -= 2;
    }
    if (nbytes == 1)
    {
        oddbyte = 0;
        ((u_char *) &oddbyte)[0] = *(u_char *) ptr;
        ((u_char *) &oddbyte)[1] = 0;
        sum += oddbyte;
    }

/* "Pseudo-header" data */
    ptr = (u_short *) &(pip->ip_dst);
    sum += *ptr++;
    sum += *ptr;
    ptr = (u_short *) &(pip->ip_src);
    sum += *ptr++;
    sum += *ptr;
    sum += htons((u_short) ntcp);
    sum += htons((u_short) pip->ip_p);

/* Roll over carry bits */
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);

/* Return checksum */
    return((u_short) ~sum);
}


void
DifferentialChecksum(u_short *cksum, u_short *new, u_short *old, int n)
{
    int i;
    int accumulate;

    accumulate = *cksum;
    for (i=0; i<n; i++)
    {
        accumulate -= *new++;
        accumulate += *old++;
    }

    if (accumulate < 0)
    {
        accumulate = -accumulate;
        accumulate = (accumulate >> 16) + (accumulate & 0xffff);
        accumulate += accumulate >> 16;
        *cksum = (u_short) ~accumulate;
    }
    else
    {
        accumulate = (accumulate >> 16) + (accumulate & 0xffff);
        accumulate += accumulate >> 16;
        *cksum = (u_short) accumulate;
    }
}

