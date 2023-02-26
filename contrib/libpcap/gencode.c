/*#define CHASE_CHAIN*/
/*
 * Copyright (c) 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998
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
 * $FreeBSD: src/contrib/libpcap/gencode.c,v 1.8.2.2 2002/07/05 14:39:58 fenner Exp $
 */
#ifndef lint
static const char rcsid[] =
    "@(#) $Header: /tcpdump/master/libpcap/gencode.c,v 1.160 2001/11/30 07:25:48 guy Exp $ (LBL)";
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#ifdef __NetBSD__
#include <sys/param.h>
#endif

struct mbuf;
struct rtentry;
#include <net/if.h>

#include <netinet/in.h>

#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <setjmp.h>
#include <stdarg.h>

#include "pcap-int.h"

#include "ethertype.h"
#include "nlpid.h"
#include "llc.h"
#include "gencode.h"
#include "ppp.h"
#include "sll.h"
#include "arcnet.h"
#include <pcap-namedb.h>
#ifdef INET6
#include <netdb.h>
#include <sys/socket.h>
#endif /*INET6*/

#undef ETHERMTU
#define ETHERMTU	1500

#ifndef IPPROTO_SCTP
#define IPPROTO_SCTP 132
#endif

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#define JMP(c) ((c)|BPF_JMP|BPF_K)

/* Locals */
static jmp_buf top_ctx;
static pcap_t *bpf_pcap;

/* Hack for updating VLAN offsets. */
static u_int	orig_linktype = -1, orig_nl = -1;

/* XXX */
#ifdef PCAP_FDDIPAD
int	pcap_fddipad = PCAP_FDDIPAD;
#else
int	pcap_fddipad;
#endif

/* VARARGS */
void
bpf_error(const char *fmt, ...)

{
	va_list ap;

	va_start(ap, fmt);
	if (bpf_pcap != NULL)
		(void)vsnprintf(pcap_geterr(bpf_pcap), PCAP_ERRBUF_SIZE,
		    fmt, ap);
	va_end(ap);
	longjmp(top_ctx, 1);
	/* NOTREACHED */
}

static void init_linktype(int);

static int alloc_reg(void);
static void free_reg(int);

static struct block *root;

/*
 * We divy out chunks of memory rather than call malloc each time so
 * we don't have to worry about leaking memory.  It's probably
 * not a big deal if all this memory was wasted but it this ever
 * goes into a library that would probably not be a good idea.
 */
#define NCHUNKS 16
#define CHUNK0SIZE 1024
struct chunk {
	u_int n_left;
	void *m;
};

static struct chunk chunks[NCHUNKS];
static int cur_chunk;

static void *newchunk(u_int);
static void freechunks(void);
static inline struct block *new_block(int);
static inline struct slist *new_stmt(int);
static struct block *gen_retblk(int);
static inline void syntax(void);

static void backpatch(struct block *, struct block *);
static void merge(struct block *, struct block *);
static struct block *gen_cmp(u_int, u_int, bpf_int32);
static struct block *gen_cmp_gt(u_int, u_int, bpf_int32);
static struct block *gen_mcmp(u_int, u_int, bpf_int32, bpf_u_int32);
static struct block *gen_bcmp(u_int, u_int, const u_char *);
static struct block *gen_uncond(int);
static inline struct block *gen_true(void);
static inline struct block *gen_false(void);
static struct block *gen_linktype(int);
static struct block *gen_snap(bpf_u_int32, bpf_u_int32, u_int);
static struct block *gen_hostop(bpf_u_int32, bpf_u_int32, int, int, u_int, u_int);
#ifdef INET6
static struct block *gen_hostop6(struct in6_addr *, struct in6_addr *, int, int, u_int, u_int);
#endif
static struct block *gen_ahostop(const u_char *, int);
static struct block *gen_ehostop(const u_char *, int);
static struct block *gen_fhostop(const u_char *, int);
static struct block *gen_thostop(const u_char *, int);
static struct block *gen_dnhostop(bpf_u_int32, int, u_int);
static struct block *gen_host(bpf_u_int32, bpf_u_int32, int, int);
#ifdef INET6
static struct block *gen_host6(struct in6_addr *, struct in6_addr *, int, int);
#endif
#ifndef INET6
static struct block *gen_gateway(const u_char *, bpf_u_int32 **, int, int);
#endif
static struct block *gen_ipfrag(void);
static struct block *gen_portatom(int, bpf_int32);
#ifdef INET6
static struct block *gen_portatom6(int, bpf_int32);
#endif
struct block *gen_portop(int, int, int);
static struct block *gen_port(int, int, int);
#ifdef INET6
struct block *gen_portop6(int, int, int);
static struct block *gen_port6(int, int, int);
#endif
static int lookup_proto(const char *, int);
static struct block *gen_protochain(int, int, int);
static struct block *gen_proto(int, int, int);
static struct slist *xfer_to_x(struct arth *);
static struct slist *xfer_to_a(struct arth *);
static struct block *gen_len(int, int);

static void *
newchunk(n)
	u_int n;
{
	struct chunk *cp;
	int k, size;

#ifndef __NetBSD__
	/* XXX Round up to nearest long. */
	n = (n + sizeof(long) - 1) & ~(sizeof(long) - 1);
#else
	/* XXX Round up to structure boundary. */
	n = ALIGN(n);
#endif

	cp = &chunks[cur_chunk];
	if (n > cp->n_left) {
		++cp, k = ++cur_chunk;
		if (k >= NCHUNKS)
			bpf_error("out of memory");
		size = CHUNK0SIZE << k;
		cp->m = (void *)malloc(size);
		memset((char *)cp->m, 0, size);
		cp->n_left = size;
		if (n > size)
			bpf_error("out of memory");
	}
	cp->n_left -= n;
	return (void *)((char *)cp->m + cp->n_left);
}

static void
freechunks()
{
	int i;

	cur_chunk = 0;
	for (i = 0; i < NCHUNKS; ++i)
		if (chunks[i].m != NULL) {
			free(chunks[i].m);
			chunks[i].m = NULL;
		}
}

/*
 * A strdup whose allocations are freed after code generation is over.
 */
char *
sdup(s)
	register const char *s;
{
	int n = strlen(s) + 1;
	char *cp = newchunk(n);

	strlcpy(cp, s, n);
	return (cp);
}

static inline struct block *
new_block(code)
	int code;
{
	struct block *p;

	p = (struct block *)newchunk(sizeof(*p));
	p->s.code = code;
	p->head = p;

	return p;
}

static inline struct slist *
new_stmt(code)
	int code;
{
	struct slist *p;

	p = (struct slist *)newchunk(sizeof(*p));
	p->s.code = code;

	return p;
}

static struct block *
gen_retblk(v)
	int v;
{
	struct block *b = new_block(BPF_RET|BPF_K);

	b->s.k = v;
	return b;
}

static inline void
syntax()
{
	bpf_error("syntax error in filter expression");
}

static bpf_u_int32 netmask;
static int snaplen;
int no_optimize;

int
pcap_compile(pcap_t *p, struct bpf_program *program,
	     char *buf, int optimize, bpf_u_int32 mask)
{
	extern int n_errors;
	int len;

	no_optimize = 0;
	n_errors = 0;
	root = NULL;
	bpf_pcap = p;
	if (setjmp(top_ctx)) {
		lex_cleanup();
		freechunks();
		return (-1);
	}

	netmask = mask;
	
	snaplen = pcap_snapshot(p);
	if (snaplen == 0) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			 "snaplen of 0 rejects all packets");
		return -1;
	}

	lex_init(buf ? buf : "");
	init_linktype(pcap_datalink(p));
	(void)pcap_parse();

	if (n_errors)
		syntax();

	if (root == NULL)
		root = gen_retblk(snaplen);

	if (optimize && !no_optimize) {
		bpf_optimize(&root);
		if (root == NULL ||
		    (root->s.code == (BPF_RET|BPF_K) && root->s.k == 0))
			bpf_error("expression rejects all packets");
	}
	program->bf_insns = icode_to_fcode(root, &len);
	program->bf_len = len;

	lex_cleanup();
	freechunks();
	return (0);
}

/*
 * entry point for using the compiler with no pcap open
 * pass in all the stuff that is needed explicitly instead.
 */
int
pcap_compile_nopcap(int snaplen_arg, int linktype_arg,
		    struct bpf_program *program,
	     char *buf, int optimize, bpf_u_int32 mask)
{
	pcap_t *p;
	int ret;

	p = pcap_open_dead(linktype_arg, snaplen_arg);
	if (p == NULL)
		return (-1);
	ret = pcap_compile(p, program, buf, optimize, mask);
	pcap_close(p);
	return (ret);
}

/*
 * Clean up a "struct bpf_program" by freeing all the memory allocated
 * in it.
 */
void
pcap_freecode(struct bpf_program *program)
{
	program->bf_len = 0;
	if (program->bf_insns != NULL) {
		free((char *)program->bf_insns);
		program->bf_insns = NULL;
	}
}

/*
 * Backpatch the blocks in 'list' to 'target'.  The 'sense' field indicates
 * which of the jt and jf fields has been resolved and which is a pointer
 * back to another unresolved block (or nil).  At least one of the fields
 * in each block is already resolved.
 */
static void
backpatch(list, target)
	struct block *list, *target;
{
	struct block *next;

	while (list) {
		if (!list->sense) {
			next = JT(list);
			JT(list) = target;
		} else {
			next = JF(list);
			JF(list) = target;
		}
		list = next;
	}
}

/*
 * Merge the lists in b0 and b1, using the 'sense' field to indicate
 * which of jt and jf is the link.
 */
static void
merge(b0, b1)
	struct block *b0, *b1;
{
	register struct block **p = &b0;

	/* Find end of list. */
	while (*p)
		p = !((*p)->sense) ? &JT(*p) : &JF(*p);

	/* Concatenate the lists. */
	*p = b1;
}

void
finish_parse(p)
	struct block *p;
{
	backpatch(p, gen_retblk(snaplen));
	p->sense = !p->sense;
	backpatch(p, gen_retblk(0));
	root = p->head;
}

void
gen_and(b0, b1)
	struct block *b0, *b1;
{
	backpatch(b0, b1->head);
	b0->sense = !b0->sense;
	b1->sense = !b1->sense;
	merge(b1, b0);
	b1->sense = !b1->sense;
	b1->head = b0->head;
}

void
gen_or(b0, b1)
	struct block *b0, *b1;
{
	b0->sense = !b0->sense;
	backpatch(b0, b1->head);
	b0->sense = !b0->sense;
	merge(b1, b0);
	b1->head = b0->head;
}

void
gen_not(b)
	struct block *b;
{
	b->sense = !b->sense;
}

static struct block *
gen_cmp(offset, size, v)
	u_int offset, size;
	bpf_int32 v;
{
	struct slist *s;
	struct block *b;

	s = new_stmt(BPF_LD|BPF_ABS|size);
	s->s.k = offset;

	b = new_block(JMP(BPF_JEQ));
	b->stmts = s;
	b->s.k = v;

	return b;
}

static struct block *
gen_cmp_gt(offset, size, v)
	u_int offset, size;
	bpf_int32 v;
{
	struct slist *s;
	struct block *b;

	s = new_stmt(BPF_LD|BPF_ABS|size);
	s->s.k = offset;

	b = new_block(JMP(BPF_JGT));
	b->stmts = s;
	b->s.k = v;

	return b;
}

static struct block *
gen_mcmp(offset, size, v, mask)
	u_int offset, size;
	bpf_int32 v;
	bpf_u_int32 mask;
{
	struct block *b = gen_cmp(offset, size, v);
	struct slist *s;

	if (mask != 0xffffffff) {
		s = new_stmt(BPF_ALU|BPF_AND|BPF_K);
		s->s.k = mask;
		b->stmts->next = s;
	}
	return b;
}

static struct block *
gen_bcmp(offset, size, v)
	register u_int offset, size;
	register const u_char *v;
{
	register struct block *b, *tmp;

	b = NULL;
	while (size >= 4) {
		register const u_char *p = &v[size - 4];
		bpf_int32 w = ((bpf_int32)p[0] << 24) |
		    ((bpf_int32)p[1] << 16) | ((bpf_int32)p[2] << 8) | p[3];

		tmp = gen_cmp(offset + size - 4, BPF_W, w);
		if (b != NULL)
			gen_and(b, tmp);
		b = tmp;
		size -= 4;
	}
	while (size >= 2) {
		register const u_char *p = &v[size - 2];
		bpf_int32 w = ((bpf_int32)p[0] << 8) | p[1];

		tmp = gen_cmp(offset + size - 2, BPF_H, w);
		if (b != NULL)
			gen_and(b, tmp);
		b = tmp;
		size -= 2;
	}
	if (size > 0) {
		tmp = gen_cmp(offset, BPF_B, (bpf_int32)v[0]);
		if (b != NULL)
			gen_and(b, tmp);
		b = tmp;
	}
	return b;
}

/*
 * Various code constructs need to know the layout of the data link
 * layer.  These variables give the necessary offsets.  off_linktype
 * is set to -1 for no encapsulation, in which case, IP is assumed.
 */
static u_int off_linktype;
static u_int off_nl;
static int linktype;

static void
init_linktype(type)
	int type;
{
	linktype = type;

	orig_linktype = -1;
	orig_nl = -1;

	switch (type) {

	case DLT_ARCNET:
		off_linktype = 2;
		off_nl = 6;	/* XXX in reality, variable! */
		return;

	case DLT_EN10MB:
		off_linktype = 12;
		off_nl = 14;
		return;

	case DLT_SLIP:
		/*
		 * SLIP doesn't have a link level type.  The 16 byte
		 * header is hacked into our SLIP driver.
		 */
		off_linktype = -1;
		off_nl = 16;
		return;

	case DLT_SLIP_BSDOS:
		/* XXX this may be the same as the DLT_PPP_BSDOS case */
		off_linktype = -1;
		/* XXX end */
		off_nl = 24;
		return;

	case DLT_NULL:
	case DLT_LOOP:
		off_linktype = 0;
		off_nl = 4;
		return;

	case DLT_PPP:
	case DLT_C_HDLC:		/* BSD/OS Cisco HDLC */
	case DLT_PPP_SERIAL:		/* NetBSD sync/async serial PPP */
		off_linktype = 2;
		off_nl = 4;
		return;

	case DLT_PPP_ETHER:
		/*
		 * This does no include the Ethernet header, and
		 * only covers session state.
		 */
		off_linktype = 6;
		off_nl = 8;
		return;

	case DLT_PPP_BSDOS:
		off_linktype = 5;
		off_nl = 24;
		return;

	case DLT_FDDI:
		/*
		 * FDDI doesn't really have a link-level type field.
		 * We set "off_linktype" to the offset of the LLC header.
		 *
		 * To check for Ethernet types, we assume that SSAP = SNAP
		 * is being used and pick out the encapsulated Ethernet type.
		 * XXX - should we generate code to check for SNAP?
		 */
		off_linktype = 13;
#ifdef PCAP_FDDIPAD
		off_linktype += pcap_fddipad;
#endif
		off_nl = 21;
#ifdef PCAP_FDDIPAD
		off_nl += pcap_fddipad;
#endif
		return;

	case DLT_IEEE802:
		/*
		 * Token Ring doesn't really have a link-level type field.
		 * We set "off_linktype" to the offset of the LLC header.
		 *
		 * To check for Ethernet types, we assume that SSAP = SNAP
		 * is being used and pick out the encapsulated Ethernet type.
		 * XXX - should we generate code to check for SNAP?
		 *
		 * XXX - the header is actually variable-length.
		 * Some various Linux patched versions gave 38
		 * as "off_linktype" and 40 as "off_nl"; however,
		 * if a token ring packet has *no* routing
		 * information, i.e. is not source-routed, the correct
		 * values are 20 and 22, as they are in the vanilla code.
		 *
		 * A packet is source-routed iff the uppermost bit
		 * of the first byte of the source address, at an
		 * offset of 8, has the uppermost bit set.  If the
		 * packet is source-routed, the total number of bytes
		 * of routing information is 2 plus bits 0x1F00 of
		 * the 16-bit value at an offset of 14 (shifted right
		 * 8 - figure out which byte that is).
		 */
		off_linktype = 14;
		off_nl = 22;
		return;

	case DLT_IEEE802_11:
		/*
		 * 802.11 doesn't really have a link-level type field.
		 * We set "off_linktype" to the offset of the LLC header.
		 *
		 * To check for Ethernet types, we assume that SSAP = SNAP
		 * is being used and pick out the encapsulated Ethernet type.
		 * XXX - should we generate code to check for SNAP?
		 *
		 * XXX - the header is actually variable-length.  We
		 * assume a 24-byte link-layer header, as appears in
		 * data frames in networks with no bridges.
		 */
		off_linktype = 24;
		off_nl = 30;
		return;

	case DLT_PRISM_HEADER:
		/*
		 * Same as 802.11, but with an additional header before
		 * the 802.11 header, containing a bunch of additional
		 * information including radio-level information.
		 *
		 * The header is 144 bytes long.
		 *
		 * XXX - same variable-length header problem; at least
		 * the Prism header is fixed-length.
		 */
		off_linktype = 144+24;
		off_nl = 144+30;
		return;

	case DLT_ATM_RFC1483:
		/*
		 * assume routed, non-ISO PDUs
		 * (i.e., LLC = 0xAA-AA-03, OUT = 0x00-00-00)
		 */
		off_linktype = 6;
		off_nl = 8;
		return;

	case DLT_RAW:
		off_linktype = -1;
		off_nl = 0;
		return;

	case DLT_ATM_CLIP:	/* Linux ATM defines this */
		off_linktype = 6;
		off_nl = 8;
		return;

	case DLT_LINUX_SLL:	/* fake header for Linux cooked socket */
		off_linktype = 14;
		off_nl = 16;
		return;

	case DLT_LTALK:
		/*
		 * LocalTalk does have a 1-byte type field in the LLAP header,
		 * but really it just indicates whether there is a "short" or
		 * "long" DDP packet following.
		 */
		off_linktype = -1;
		off_nl = 0;
		return;
	}
	bpf_error("unknown data link type %d", linktype);
	/* NOTREACHED */
}

static struct block *
gen_uncond(rsense)
	int rsense;
{
	struct block *b;
	struct slist *s;

	s = new_stmt(BPF_LD|BPF_IMM);
	s->s.k = !rsense;
	b = new_block(JMP(BPF_JEQ));
	b->stmts = s;

	return b;
}

static inline struct block *
gen_true()
{
	return gen_uncond(1);
}

static inline struct block *
gen_false()
{
	return gen_uncond(0);
}

/*
 * Byte-swap a 32-bit number.
 * ("htonl()" or "ntohl()" won't work - we want to byte-swap even on
 * big-endian platforms.)
 */
#define	SWAPLONG(y) \
((((y)&0xff)<<24) | (((y)&0xff00)<<8) | (((y)&0xff0000)>>8) | (((y)>>24)&0xff))

static struct block *
gen_linktype(proto)
	register int proto;
{
	struct block *b0, *b1;

	switch (linktype) {

	case DLT_EN10MB:
		switch (proto) {

		case LLCSAP_ISONS:
			/*
			 * OSI protocols always use 802.2 encapsulation.
			 * XXX - should we check both the DSAP and the
			 * SSAP, like this, or should we check just the
			 * DSAP?
			 */
			b0 = gen_cmp_gt(off_linktype, BPF_H, ETHERMTU);
			gen_not(b0);
			b1 = gen_cmp(off_linktype + 2, BPF_H, (bpf_int32)
				     ((LLCSAP_ISONS << 8) | LLCSAP_ISONS));
			gen_and(b0, b1);
			return b1;

		case LLCSAP_NETBEUI:
			/*
			 * NetBEUI always uses 802.2 encapsulation.
			 * XXX - should we check both the DSAP and the
			 * SSAP, like this, or should we check just the
			 * DSAP?
			 */
			b0 = gen_cmp_gt(off_linktype, BPF_H, ETHERMTU);
			gen_not(b0);
			b1 = gen_cmp(off_linktype + 2, BPF_H, (bpf_int32)
				     ((LLCSAP_NETBEUI << 8) | LLCSAP_NETBEUI));
			gen_and(b0, b1);
			return b1;

		case LLCSAP_IPX:
			/*
			 * Check for;
			 *
			 *	Ethernet_II frames, which are Ethernet
			 *	frames with a frame type of ETHERTYPE_IPX;
			 *
			 *	Ethernet_802.3 frames, which are 802.3
			 *	frames (i.e., the type/length field is
			 *	a length field, <= ETHERMTU, rather than
			 *	a type field) with the first two bytes
			 *	after the Ethernet/802.3 header being
			 *	0xFFFF;
			 *
			 *	Ethernet_802.2 frames, which are 802.3
			 *	frames with an 802.2 LLC header and
			 *	with the IPX LSAP as the DSAP in the LLC
			 *	header;
			 *
			 *	Ethernet_SNAP frames, which are 802.3
			 *	frames with an LLC header and a SNAP
			 *	header and with an OUI of 0x000000
			 *	(encapsulated Ethernet) and a protocol
			 *	ID of ETHERTYPE_IPX in the SNAP header.
			 *
			 * XXX - should we generate the same code both
			 * for tests for LLCSAP_IPX and for ETHERTYPE_IPX?
			 */

			/*
			 * This generates code to check both for the
			 * IPX LSAP (Ethernet_802.2) and for Ethernet_802.3.
			 */
			b0 = gen_cmp(off_linktype + 2, BPF_B,
			    (bpf_int32)LLCSAP_IPX);
			b1 = gen_cmp(off_linktype + 2, BPF_H,
			    (bpf_int32)0xFFFF);
			gen_or(b0, b1);

			/*
			 * Now we add code to check for SNAP frames with
			 * ETHERTYPE_IPX, i.e. Ethernet_SNAP.
			 */
			b0 = gen_snap(0x000000, ETHERTYPE_IPX, 14);
			gen_or(b0, b1);

			/*
			 * Now we generate code to check for 802.3
			 * frames in general.
			 */
			b0 = gen_cmp_gt(off_linktype, BPF_H, ETHERMTU);
			gen_not(b0);

			/*
			 * Now add the check for 802.3 frames before the
			 * check for Ethernet_802.2 and Ethernet_802.3,
			 * as those checks should only be done on 802.3
			 * frames, not on Ethernet frames.
			 */
			gen_and(b0, b1);

			/*
			 * Now add the check for Ethernet_II frames, and
			 * do that before checking for the other frame
			 * types.
			 */
			b0 = gen_cmp(off_linktype, BPF_H,
			    (bpf_int32)ETHERTYPE_IPX);
			gen_or(b0, b1);
			return b1;

		case ETHERTYPE_ATALK:
		case ETHERTYPE_AARP:
			/*
			 * EtherTalk (AppleTalk protocols on Ethernet link
			 * layer) may use 802.2 encapsulation.
			 */

			/*
			 * Check for 802.2 encapsulation (EtherTalk phase 2?);
			 * we check for an Ethernet type field less than
			 * 1500, which means it's an 802.3 length field.
			 */
			b0 = gen_cmp_gt(off_linktype, BPF_H, ETHERMTU);
			gen_not(b0);

			/*
			 * 802.2-encapsulated ETHERTYPE_ATALK packets are
			 * SNAP packets with an organization code of
			 * 0x080007 (Apple, for Appletalk) and a protocol
			 * type of ETHERTYPE_ATALK (Appletalk).
			 *
			 * 802.2-encapsulated ETHERTYPE_AARP packets are
			 * SNAP packets with an organization code of
			 * 0x000000 (encapsulated Ethernet) and a protocol
			 * type of ETHERTYPE_AARP (Appletalk ARP).
			 */
			if (proto == ETHERTYPE_ATALK)
				b1 = gen_snap(0x080007, ETHERTYPE_ATALK, 14);
			else	/* proto == ETHERTYPE_AARP */
				b1 = gen_snap(0x000000, ETHERTYPE_AARP, 14);
			gen_and(b0, b1);

			/*
			 * Check for Ethernet encapsulation (Ethertalk
			 * phase 1?); we just check for the Ethernet
			 * protocol type.
			 */
			b0 = gen_cmp(off_linktype, BPF_H, (bpf_int32)proto);

			gen_or(b0, b1);
			return b1;

		default:
			if (proto <= ETHERMTU) {
				/*
				 * This is an LLC SAP value, so the frames
				 * that match would be 802.2 frames.
				 * Check that the frame is an 802.2 frame
				 * (i.e., that the length/type field is
				 * a length field, <= ETHERMTU) and
				 * then check the DSAP.
				 */
				b0 = gen_cmp_gt(off_linktype, BPF_H, ETHERMTU);
				gen_not(b0);
				b1 = gen_cmp(off_linktype + 2, BPF_B,
				     (bpf_int32)proto);
				gen_and(b0, b1);
				return b1;
			} else {
				/*
				 * This is an Ethernet type, so compare
				 * the length/type field with it (if
				 * the frame is an 802.2 frame, the length
				 * field will be <= ETHERMTU, and, as
				 * "proto" is > ETHERMTU, this test
				 * will fail and the frame won't match,
				 * which is what we want).
				 */
				return gen_cmp(off_linktype, BPF_H,
				    (bpf_int32)proto);
			}
		}
		break;

	case DLT_IEEE802_11:
	case DLT_PRISM_HEADER:
	case DLT_FDDI:
	case DLT_IEEE802:
	case DLT_ATM_RFC1483:
	case DLT_ATM_CLIP:
		/*
		 * XXX - handle token-ring variable-length header.
		 */
		switch (proto) {

		case LLCSAP_ISONS:
			return gen_cmp(off_linktype, BPF_H, (long)
				     ((LLCSAP_ISONS << 8) | LLCSAP_ISONS));

		case LLCSAP_NETBEUI:
			return gen_cmp(off_linktype, BPF_H, (long)
				     ((LLCSAP_NETBEUI << 8) | LLCSAP_NETBEUI));

		case LLCSAP_IPX:
			/*
			 * XXX - are there ever SNAP frames for IPX on
			 * non-Ethernet 802.x networks?
			 */
			return gen_cmp(off_linktype, BPF_B,
			    (bpf_int32)LLCSAP_IPX);

		case ETHERTYPE_ATALK:
			/*
			 * 802.2-encapsulated ETHERTYPE_ATALK packets are
			 * SNAP packets with an organization code of
			 * 0x080007 (Apple, for Appletalk) and a protocol
			 * type of ETHERTYPE_ATALK (Appletalk).
			 *
			 * XXX - check for an organization code of
			 * encapsulated Ethernet as well?
			 */
			return gen_snap(0x080007, ETHERTYPE_ATALK,
			    off_linktype);
			break;

		default:
			/*
			 * XXX - we don't have to check for IPX 802.3
			 * here, but should we check for the IPX Ethertype?
			 */
			if (proto <= ETHERMTU) {
				/*
				 * This is an LLC SAP value, so check
				 * the DSAP.
				 */
				return gen_cmp(off_linktype, BPF_B,
				     (bpf_int32)proto);
			} else {
				/*
				 * This is an Ethernet type; we assume
				 * that it's unlikely that it'll
				 * appear in the right place at random,
				 * and therefore check only the
				 * location that would hold the Ethernet
				 * type in a SNAP frame with an organization
				 * code of 0x000000 (encapsulated Ethernet).
				 *
				 * XXX - if we were to check for the SNAP DSAP
				 * and LSAP, as per XXX, and were also to check
				 * for an organization code of 0x000000
				 * (encapsulated Ethernet), we'd do
				 *
				 *	return gen_snap(0x000000, proto,
				 *	    off_linktype);
				 *
				 * here; for now, we don't, as per the above.
				 * I don't know whether it's worth the
				 * extra CPU time to do the right check
				 * or not.
				 */
				return gen_cmp(off_linktype+6, BPF_H,
				    (bpf_int32)proto);
			}
		}
		break;

	case DLT_LINUX_SLL:
		switch (proto) {

		case LLCSAP_ISONS:
			/*
			 * OSI protocols always use 802.2 encapsulation.
			 * XXX - should we check both the DSAP and the
			 * LSAP, like this, or should we check just the
			 * DSAP?
			 */
			b0 = gen_cmp(off_linktype, BPF_H, LINUX_SLL_P_802_2);
			b1 = gen_cmp(off_linktype + 2, BPF_H, (bpf_int32)
				     ((LLCSAP_ISONS << 8) | LLCSAP_ISONS));
			gen_and(b0, b1);
			return b1;

		case LLCSAP_NETBEUI:
			/*
			 * NetBEUI always uses 802.2 encapsulation.
			 * XXX - should we check both the DSAP and the
			 * LSAP, like this, or should we check just the
			 * DSAP?
			 */
			b0 = gen_cmp(off_linktype, BPF_H, LINUX_SLL_P_802_2);
			b1 = gen_cmp(off_linktype + 2, BPF_H, (bpf_int32)
				     ((LLCSAP_NETBEUI << 8) | LLCSAP_NETBEUI));
			gen_and(b0, b1);
			return b1;

		case LLCSAP_IPX:
			/*
			 *	Ethernet_II frames, which are Ethernet
			 *	frames with a frame type of ETHERTYPE_IPX;
			 *
			 *	Ethernet_802.3 frames, which have a frame
			 *	type of LINUX_SLL_P_802_3;
			 *
			 *	Ethernet_802.2 frames, which are 802.3
			 *	frames with an 802.2 LLC header (i.e, have
			 *	a frame type of LINUX_SLL_P_802_2) and
			 *	with the IPX LSAP as the DSAP in the LLC
			 *	header;
			 *
			 *	Ethernet_SNAP frames, which are 802.3
			 *	frames with an LLC header and a SNAP
			 *	header and with an OUI of 0x000000
			 *	(encapsulated Ethernet) and a protocol
			 *	ID of ETHERTYPE_IPX in the SNAP header.
			 *
			 * First, do the checks on LINUX_SLL_P_802_2
			 * frames; generate the check for either
			 * Ethernet_802.2 or Ethernet_SNAP frames, and
			 * then put a check for LINUX_SLL_P_802_2 frames
			 * before it.
			 */
			b0 = gen_cmp(off_linktype + 2, BPF_B,
			    (bpf_int32)LLCSAP_IPX);
			b1 = gen_snap(0x000000, ETHERTYPE_IPX,
			    off_linktype + 2);
			gen_or(b0, b1);
			b0 = gen_cmp(off_linktype, BPF_H, LINUX_SLL_P_802_2);
			gen_and(b0, b1);

			/*
			 * Now check for 802.3 frames and OR that with
			 * the previous test.
			 */
			b0 = gen_cmp(off_linktype, BPF_H, LINUX_SLL_P_802_3);
			gen_or(b0, b1);

			/*
			 * Now add the check for Ethernet_II frames, and
			 * do that before checking for the other frame
			 * types.
			 */
			b0 = gen_cmp(off_linktype, BPF_H,
			    (bpf_int32)ETHERTYPE_IPX);
			gen_or(b0, b1);
			return b1;

		case ETHERTYPE_ATALK:
		case ETHERTYPE_AARP:
			/*
			 * EtherTalk (AppleTalk protocols on Ethernet link
			 * layer) may use 802.2 encapsulation.
			 */

			/*
			 * Check for 802.2 encapsulation (EtherTalk phase 2?);
			 * we check for the 802.2 protocol type in the
			 * "Ethernet type" field.
			 */
			b0 = gen_cmp(off_linktype, BPF_H, LINUX_SLL_P_802_2);

			/*
			 * 802.2-encapsulated ETHERTYPE_ATALK packets are
			 * SNAP packets with an organization code of
			 * 0x080007 (Apple, for Appletalk) and a protocol
			 * type of ETHERTYPE_ATALK (Appletalk).
			 *
			 * 802.2-encapsulated ETHERTYPE_AARP packets are
			 * SNAP packets with an organization code of
			 * 0x000000 (encapsulated Ethernet) and a protocol
			 * type of ETHERTYPE_AARP (Appletalk ARP).
			 */
			if (proto == ETHERTYPE_ATALK)
				b1 = gen_snap(0x080007, ETHERTYPE_ATALK,
				    off_linktype + 2);
			else	/* proto == ETHERTYPE_AARP */
				b1 = gen_snap(0x000000, ETHERTYPE_AARP,
				    off_linktype + 2);
			gen_and(b0, b1);

			/*
			 * Check for Ethernet encapsulation (Ethertalk
			 * phase 1?); we just check for the Ethernet
			 * protocol type.
			 */
			b0 = gen_cmp(off_linktype, BPF_H, (bpf_int32)proto);

			gen_or(b0, b1);
			return b1;

		default:
			if (proto <= ETHERMTU) {
				/*
				 * This is an LLC SAP value, so the frames
				 * that match would be 802.2 frames.
				 * Check for the 802.2 protocol type
				 * in the "Ethernet type" field, and
				 * then check the DSAP.
				 */
				b0 = gen_cmp(off_linktype, BPF_H,
				    LINUX_SLL_P_802_2);
				b1 = gen_cmp(off_linktype + 2, BPF_B,
				     (bpf_int32)proto);
				gen_and(b0, b1);
				return b1;
			} else {
				/*
				 * This is an Ethernet type, so compare
				 * the length/type field with it (if
				 * the frame is an 802.2 frame, the length
				 * field will be <= ETHERMTU, and, as
				 * "proto" is > ETHERMTU, this test
				 * will fail and the frame won't match,
				 * which is what we want).
				 */
				return gen_cmp(off_linktype, BPF_H,
				    (bpf_int32)proto);
			}
		}
		break;

	case DLT_SLIP:
	case DLT_SLIP_BSDOS:
	case DLT_RAW:
		/*
		 * These types don't provide any type field; packets
		 * are always IP.
		 *
		 * XXX - for IPv4, check for a version number of 4, and,
		 * for IPv6, check for a version number of 6?
		 */
		switch (proto) {

		case ETHERTYPE_IP:
#ifdef INET6
		case ETHERTYPE_IPV6:
#endif
			return gen_true();		/* always true */

		default:
			return gen_false();		/* always false */
		}
		break;

	case DLT_PPP:
	case DLT_PPP_SERIAL:
	case DLT_PPP_ETHER:
		/*
		 * We use Ethernet protocol types inside libpcap;
		 * map them to the corresponding PPP protocol types.
		 */
		switch (proto) {

		case ETHERTYPE_IP:
			proto = PPP_IP;			/* XXX was 0x21 */
			break;

#ifdef INET6
		case ETHERTYPE_IPV6:
			proto = PPP_IPV6;
			break;
#endif

		case ETHERTYPE_DN:
			proto = PPP_DECNET;
			break;

		case ETHERTYPE_ATALK:
			proto = PPP_APPLE;
			break;

		case ETHERTYPE_NS:
			proto = PPP_NS;
			break;

		case LLCSAP_ISONS:
			proto = PPP_OSI;
			break;

		case LLCSAP_8021D:
			/*
			 * I'm assuming the "Bridging PDU"s that go
			 * over PPP are Spanning Tree Protocol
			 * Bridging PDUs.
			 */
			proto = PPP_BRPDU;
			break;

		case LLCSAP_IPX:
			proto = PPP_IPX;
			break;
		}
		break;

	case DLT_PPP_BSDOS:
		/*
		 * We use Ethernet protocol types inside libpcap;
		 * map them to the corresponding PPP protocol types.
		 */
		switch (proto) {

		case ETHERTYPE_IP:
			b0 = gen_cmp(off_linktype, BPF_H, PPP_IP);
			b1 = gen_cmp(off_linktype, BPF_H, PPP_VJC);
			gen_or(b0, b1);
			b0 = gen_cmp(off_linktype, BPF_H, PPP_VJNC);
			gen_or(b1, b0);
			return b0;

#ifdef INET6
		case ETHERTYPE_IPV6:
			proto = PPP_IPV6;
			/* more to go? */
			break;
#endif

		case ETHERTYPE_DN:
			proto = PPP_DECNET;
			break;

		case ETHERTYPE_ATALK:
			proto = PPP_APPLE;
			break;

		case ETHERTYPE_NS:
			proto = PPP_NS;
			break;

		case LLCSAP_ISONS:
			proto = PPP_OSI;
			break;

		case LLCSAP_8021D:
			/*
			 * I'm assuming the "Bridging PDU"s that go
			 * over PPP are Spanning Tree Protocol
			 * Bridging PDUs.
			 */
			proto = PPP_BRPDU;
			break;

		case LLCSAP_IPX:
			proto = PPP_IPX;
			break;
		}
		break;

	case DLT_NULL:
	case DLT_LOOP:
		/*
		 * For DLT_NULL, the link-layer header is a 32-bit
		 * word containing an AF_ value in *host* byte order.
		 *
		 * In addition, if we're reading a saved capture file,
		 * the host byte order in the capture may not be the
		 * same as the host byte order on this machine.
		 *
		 * For DLT_LOOP, the link-layer header is a 32-bit
		 * word containing an AF_ value in *network* byte order.
		 *
		 * XXX - AF_ values may, unfortunately, be platform-
		 * dependent; for example, FreeBSD's AF_INET6 is 24
		 * whilst NetBSD's and OpenBSD's is 26.
		 *
		 * This means that, when reading a capture file, just
		 * checking for our AF_INET6 value won't work if the
		 * capture file came from another OS.
		 */
		switch (proto) {

		case ETHERTYPE_IP:
			proto = AF_INET;
			break;

#ifdef INET6
		case ETHERTYPE_IPV6:
			proto = AF_INET6;
			break;
#endif

		default:
			/*
			 * Not a type on which we support filtering.
			 * XXX - support those that have AF_ values
			 * #defined on this platform, at least?
			 */
			return gen_false();
		}

		if (linktype == DLT_NULL) {
			/*
			 * The AF_ value is in host byte order, but
			 * the BPF interpreter will convert it to
			 * network byte order.
			 *
			 * If this is a save file, and it's from a
			 * machine with the opposite byte order to
			 * ours, we byte-swap the AF_ value.
			 *
			 * Then we run it through "htonl()", and
			 * generate code to compare against the result.
			 */
			if (bpf_pcap->sf.rfile != NULL &&
			    bpf_pcap->sf.swapped)
				proto = SWAPLONG(proto);
			proto = htonl(proto);
		}
		return (gen_cmp(0, BPF_W, (bpf_int32)proto));

	case DLT_ARCNET:
		/*
		 * XXX should we check for first fragment if the protocol
		 * uses PHDS?
		 */
		switch(proto) {
		default:
			return gen_false();
#ifdef INET6
		case ETHERTYPE_IPV6:
			return(gen_cmp(2, BPF_B,
					(bpf_int32)htonl(ARCTYPE_INET6)));
#endif /* INET6 */
		case ETHERTYPE_IP:
			b0 = gen_cmp(2, BPF_B, (bpf_int32)htonl(ARCTYPE_IP));
			b1 = gen_cmp(2, BPF_B,
					(bpf_int32)htonl(ARCTYPE_IP_OLD));
			gen_or(b0, b1);
			return(b1);
		case ETHERTYPE_ARP:
			b0 = gen_cmp(2, BPF_B, (bpf_int32)htonl(ARCTYPE_ARP));
			b1 = gen_cmp(2, BPF_B,
					(bpf_int32)htonl(ARCTYPE_ARP_OLD));
			gen_or(b0, b1);
			return(b1);
		case ETHERTYPE_REVARP:
			return(gen_cmp(2, BPF_B,
					(bpf_int32)htonl(ARCTYPE_REVARP)));
		case ETHERTYPE_ATALK:
			return(gen_cmp(2, BPF_B,
					(bpf_int32)htonl(ARCTYPE_ATALK)));
		}
		break;

	case DLT_LTALK:
		switch (proto) {
		case ETHERTYPE_ATALK:
			return gen_true();
		default:
			return gen_false();
		}
		break;
	}

	/*
	 * All the types that have no encapsulation should either be
	 * handled as DLT_SLIP, DLT_SLIP_BSDOS, and DLT_RAW are, if
	 * all packets are IP packets, or should be handled in some
	 * special case, if none of them are (if some are and some
	 * aren't, the lack of encapsulation is a problem, as we'd
	 * have to find some other way of determining the packet type).
	 *
	 * Therefore, if "off_linktype" is -1, there's an error.
	 */
	if (off_linktype == -1)
		abort();

	/*
	 * Any type not handled above should always have an Ethernet
	 * type at an offset of "off_linktype".  (PPP is partially
	 * handled above - the protocol type is mapped from the
	 * Ethernet and LLC types we use internally to the corresponding
	 * PPP type - but the PPP type is always specified by a value
	 * at "off_linktype", so we don't have to do the code generation
	 * above.)
	 */
	return gen_cmp(off_linktype, BPF_H, (bpf_int32)proto);
}

/*
 * Check for an LLC SNAP packet with a given organization code and
 * protocol type; we check the entire contents of the 802.2 LLC and
 * snap headers, checking for DSAP and SSAP of SNAP and a control
 * field of 0x03 in the LLC header, and for the specified organization
 * code and protocol type in the SNAP header.
 */
static struct block *
gen_snap(orgcode, ptype, offset)
	bpf_u_int32 orgcode;
	bpf_u_int32 ptype;
	u_int offset;
{
	u_char snapblock[8];

	snapblock[0] = LLCSAP_SNAP;	/* DSAP = SNAP */
	snapblock[1] = LLCSAP_SNAP;	/* SSAP = SNAP */
	snapblock[2] = 0x03;	/* control = UI */
	snapblock[3] = (orgcode >> 16);	/* upper 8 bits of organization code */
	snapblock[4] = (orgcode >> 8);	/* middle 8 bits of organization code */
	snapblock[5] = (orgcode >> 0);	/* lower 8 bits of organization code */
	snapblock[6] = (ptype >> 8);	/* upper 8 bits of protocol type */
	snapblock[7] = (ptype >> 0);	/* lower 8 bits of protocol type */
	return gen_bcmp(offset, 8, snapblock);
}

static struct block *
gen_hostop(addr, mask, dir, proto, src_off, dst_off)
	bpf_u_int32 addr;
	bpf_u_int32 mask;
	int dir, proto;
	u_int src_off, dst_off;
{
	struct block *b0, *b1;
	u_int offset;

	switch (dir) {

	case Q_SRC:
		offset = src_off;
		break;

	case Q_DST:
		offset = dst_off;
		break;

	case Q_AND:
		b0 = gen_hostop(addr, mask, Q_SRC, proto, src_off, dst_off);
		b1 = gen_hostop(addr, mask, Q_DST, proto, src_off, dst_off);
		gen_and(b0, b1);
		return b1;

	case Q_OR:
	case Q_DEFAULT:
		b0 = gen_hostop(addr, mask, Q_SRC, proto, src_off, dst_off);
		b1 = gen_hostop(addr, mask, Q_DST, proto, src_off, dst_off);
		gen_or(b0, b1);
		return b1;

	default:
		abort();
	}
	b0 = gen_linktype(proto);
	b1 = gen_mcmp(offset, BPF_W, (bpf_int32)addr, mask);
	gen_and(b0, b1);
	return b1;
}

#ifdef INET6
static struct block *
gen_hostop6(addr, mask, dir, proto, src_off, dst_off)
	struct in6_addr *addr;
	struct in6_addr *mask;
	int dir, proto;
	u_int src_off, dst_off;
{
	struct block *b0, *b1;
	u_int offset;
	u_int32_t *a, *m;

	switch (dir) {

	case Q_SRC:
		offset = src_off;
		break;

	case Q_DST:
		offset = dst_off;
		break;

	case Q_AND:
		b0 = gen_hostop6(addr, mask, Q_SRC, proto, src_off, dst_off);
		b1 = gen_hostop6(addr, mask, Q_DST, proto, src_off, dst_off);
		gen_and(b0, b1);
		return b1;

	case Q_OR:
	case Q_DEFAULT:
		b0 = gen_hostop6(addr, mask, Q_SRC, proto, src_off, dst_off);
		b1 = gen_hostop6(addr, mask, Q_DST, proto, src_off, dst_off);
		gen_or(b0, b1);
		return b1;

	default:
		abort();
	}
	/* this order is important */
	a = (u_int32_t *)addr;
	m = (u_int32_t *)mask;
	b1 = gen_mcmp(offset + 12, BPF_W, ntohl(a[3]), ntohl(m[3]));
	b0 = gen_mcmp(offset + 8, BPF_W, ntohl(a[2]), ntohl(m[2]));
	gen_and(b0, b1);
	b0 = gen_mcmp(offset + 4, BPF_W, ntohl(a[1]), ntohl(m[1]));
	gen_and(b0, b1);
	b0 = gen_mcmp(offset + 0, BPF_W, ntohl(a[0]), ntohl(m[0]));
	gen_and(b0, b1);
	b0 = gen_linktype(proto);
	gen_and(b0, b1);
	return b1;
}
#endif /*INET6*/

static struct block *
gen_ehostop(eaddr, dir)
	register const u_char *eaddr;
	register int dir;
{
	register struct block *b0, *b1;

	switch (dir) {
	case Q_SRC:
		return gen_bcmp(6, 6, eaddr);

	case Q_DST:
		return gen_bcmp(0, 6, eaddr);

	case Q_AND:
		b0 = gen_ehostop(eaddr, Q_SRC);
		b1 = gen_ehostop(eaddr, Q_DST);
		gen_and(b0, b1);
		return b1;

	case Q_DEFAULT:
	case Q_OR:
		b0 = gen_ehostop(eaddr, Q_SRC);
		b1 = gen_ehostop(eaddr, Q_DST);
		gen_or(b0, b1);
		return b1;
	}
	abort();
	/* NOTREACHED */
}

/*
 * Like gen_ehostop, but for DLT_FDDI
 */
static struct block *
gen_fhostop(eaddr, dir)
	register const u_char *eaddr;
	register int dir;
{
	struct block *b0, *b1;

	switch (dir) {
	case Q_SRC:
#ifdef PCAP_FDDIPAD
		return gen_bcmp(6 + 1 + pcap_fddipad, 6, eaddr);
#else
		return gen_bcmp(6 + 1, 6, eaddr);
#endif

	case Q_DST:
#ifdef PCAP_FDDIPAD
		return gen_bcmp(0 + 1 + pcap_fddipad, 6, eaddr);
#else
		return gen_bcmp(0 + 1, 6, eaddr);
#endif

	case Q_AND:
		b0 = gen_fhostop(eaddr, Q_SRC);
		b1 = gen_fhostop(eaddr, Q_DST);
		gen_and(b0, b1);
		return b1;

	case Q_DEFAULT:
	case Q_OR:
		b0 = gen_fhostop(eaddr, Q_SRC);
		b1 = gen_fhostop(eaddr, Q_DST);
		gen_or(b0, b1);
		return b1;
	}
	abort();
	/* NOTREACHED */
}

/*
 * Like gen_ehostop, but for DLT_IEEE802 (Token Ring)
 */
static struct block *
gen_thostop(eaddr, dir)
	register const u_char *eaddr;
	register int dir;
{
	register struct block *b0, *b1;

	switch (dir) {
	case Q_SRC:
		return gen_bcmp(8, 6, eaddr);

	case Q_DST:
		return gen_bcmp(2, 6, eaddr);

	case Q_AND:
		b0 = gen_thostop(eaddr, Q_SRC);
		b1 = gen_thostop(eaddr, Q_DST);
		gen_and(b0, b1);
		return b1;

	case Q_DEFAULT:
	case Q_OR:
		b0 = gen_thostop(eaddr, Q_SRC);
		b1 = gen_thostop(eaddr, Q_DST);
		gen_or(b0, b1);
		return b1;
	}
	abort();
	/* NOTREACHED */
}

/*
 * This is quite tricky because there may be pad bytes in front of the
 * DECNET header, and then there are two possible data packet formats that
 * carry both src and dst addresses, plus 5 packet types in a format that
 * carries only the src node, plus 2 types that use a different format and
 * also carry just the src node.
 *
 * Yuck.
 *
 * Instead of doing those all right, we just look for data packets with
 * 0 or 1 bytes of padding.  If you want to look at other packets, that
 * will require a lot more hacking.
 *
 * To add support for filtering on DECNET "areas" (network numbers)
 * one would want to add a "mask" argument to this routine.  That would
 * make the filter even more inefficient, although one could be clever
 * and not generate masking instructions if the mask is 0xFFFF.
 */
static struct block *
gen_dnhostop(addr, dir, base_off)
	bpf_u_int32 addr;
	int dir;
	u_int base_off;
{
	struct block *b0, *b1, *b2, *tmp;
	u_int offset_lh;	/* offset if long header is received */
	u_int offset_sh;	/* offset if short header is received */

	switch (dir) {

	case Q_DST:
		offset_sh = 1;	/* follows flags */
		offset_lh = 7;	/* flgs,darea,dsubarea,HIORD */
		break;

	case Q_SRC:
		offset_sh = 3;	/* follows flags, dstnode */
		offset_lh = 15;	/* flgs,darea,dsubarea,did,sarea,ssub,HIORD */
		break;

	case Q_AND:
		/* Inefficient because we do our Calvinball dance twice */
		b0 = gen_dnhostop(addr, Q_SRC, base_off);
		b1 = gen_dnhostop(addr, Q_DST, base_off);
		gen_and(b0, b1);
		return b1;

	case Q_OR:
	case Q_DEFAULT:
		/* Inefficient because we do our Calvinball dance twice */
		b0 = gen_dnhostop(addr, Q_SRC, base_off);
		b1 = gen_dnhostop(addr, Q_DST, base_off);
		gen_or(b0, b1);
		return b1;

	case Q_ISO:
	        bpf_error("ISO host filtering not implemented");
		
	default:
		abort();
	}
	b0 = gen_linktype(ETHERTYPE_DN);
	/* Check for pad = 1, long header case */
	tmp = gen_mcmp(base_off + 2, BPF_H,
	    (bpf_int32)ntohs(0x0681), (bpf_int32)ntohs(0x07FF));
	b1 = gen_cmp(base_off + 2 + 1 + offset_lh,
	    BPF_H, (bpf_int32)ntohs(addr));
	gen_and(tmp, b1);
	/* Check for pad = 0, long header case */
	tmp = gen_mcmp(base_off + 2, BPF_B, (bpf_int32)0x06, (bpf_int32)0x7);
	b2 = gen_cmp(base_off + 2 + offset_lh, BPF_H, (bpf_int32)ntohs(addr));
	gen_and(tmp, b2);
	gen_or(b2, b1);
	/* Check for pad = 1, short header case */
	tmp = gen_mcmp(base_off + 2, BPF_H,
	    (bpf_int32)ntohs(0x0281), (bpf_int32)ntohs(0x07FF));
	b2 = gen_cmp(base_off + 2 + 1 + offset_sh,
	    BPF_H, (bpf_int32)ntohs(addr));
	gen_and(tmp, b2);
	gen_or(b2, b1);
	/* Check for pad = 0, short header case */
	tmp = gen_mcmp(base_off + 2, BPF_B, (bpf_int32)0x02, (bpf_int32)0x7);
	b2 = gen_cmp(base_off + 2 + offset_sh, BPF_H, (bpf_int32)ntohs(addr));
	gen_and(tmp, b2);
	gen_or(b2, b1);

	/* Combine with test for linktype */
	gen_and(b0, b1);
	return b1;
}

static struct block *
gen_host(addr, mask, proto, dir)
	bpf_u_int32 addr;
	bpf_u_int32 mask;
	int proto;
	int dir;
{
	struct block *b0, *b1;

	switch (proto) {

	case Q_DEFAULT:
		b0 = gen_host(addr, mask, Q_IP, dir);
		if (off_linktype != -1) {
		    b1 = gen_host(addr, mask, Q_ARP, dir);
		    gen_or(b0, b1);
		    b0 = gen_host(addr, mask, Q_RARP, dir);
		    gen_or(b1, b0);
		}
		return b0;

	case Q_IP:
		return gen_hostop(addr, mask, dir, ETHERTYPE_IP,
				  off_nl + 12, off_nl + 16);

	case Q_RARP:
		return gen_hostop(addr, mask, dir, ETHERTYPE_REVARP,
				  off_nl + 14, off_nl + 24);

	case Q_ARP:
		return gen_hostop(addr, mask, dir, ETHERTYPE_ARP,
				  off_nl + 14, off_nl + 24);

	case Q_TCP:
		bpf_error("'tcp' modifier applied to host");

	case Q_SCTP:
		bpf_error("'sctp' modifier applied to host");

	case Q_UDP:
		bpf_error("'udp' modifier applied to host");

	case Q_ICMP:
		bpf_error("'icmp' modifier applied to host");

	case Q_IGMP:
		bpf_error("'igmp' modifier applied to host");

	case Q_IGRP:
		bpf_error("'igrp' modifier applied to host");

	case Q_PIM:
		bpf_error("'pim' modifier applied to host");

	case Q_VRRP:
		bpf_error("'vrrp' modifier applied to host");

	case Q_ATALK:
		bpf_error("ATALK host filtering not implemented");

	case Q_AARP:
		bpf_error("AARP host filtering not implemented");

	case Q_DECNET:
		return gen_dnhostop(addr, dir, off_nl);

	case Q_SCA:
		bpf_error("SCA host filtering not implemented");

	case Q_LAT:
		bpf_error("LAT host filtering not implemented");

	case Q_MOPDL:
		bpf_error("MOPDL host filtering not implemented");

	case Q_MOPRC:
		bpf_error("MOPRC host filtering not implemented");

#ifdef INET6
	case Q_IPV6:
		bpf_error("'ip6' modifier applied to ip host");

	case Q_ICMPV6:
		bpf_error("'icmp6' modifier applied to host");
#endif /* INET6 */

	case Q_AH:
		bpf_error("'ah' modifier applied to host");

	case Q_ESP:
		bpf_error("'esp' modifier applied to host");

	case Q_ISO:
		bpf_error("ISO host filtering not implemented");

	case Q_ESIS:
		bpf_error("'esis' modifier applied to host");

	case Q_ISIS:
		bpf_error("'isis' modifier applied to host");

	case Q_CLNP:
		bpf_error("'clnp' modifier applied to host");

	case Q_STP:
		bpf_error("'stp' modifier applied to host");

	case Q_IPX:
		bpf_error("IPX host filtering not implemented");

	case Q_NETBEUI:
		bpf_error("'netbeui' modifier applied to host");

	default:
		abort();
	}
	/* NOTREACHED */
}

#ifdef INET6
static struct block *
gen_host6(addr, mask, proto, dir)
	struct in6_addr *addr;
	struct in6_addr *mask;
	int proto;
	int dir;
{
	switch (proto) {

	case Q_DEFAULT:
		return gen_host6(addr, mask, Q_IPV6, dir);

	case Q_IP:
		bpf_error("'ip' modifier applied to ip6 host");

	case Q_RARP:
		bpf_error("'rarp' modifier applied to ip6 host");

	case Q_ARP:
		bpf_error("'arp' modifier applied to ip6 host");

	case Q_SCTP:
		bpf_error("'sctp' modifier applied to host");

	case Q_TCP:
		bpf_error("'tcp' modifier applied to host");

	case Q_UDP:
		bpf_error("'udp' modifier applied to host");

	case Q_ICMP:
		bpf_error("'icmp' modifier applied to host");

	case Q_IGMP:
		bpf_error("'igmp' modifier applied to host");

	case Q_IGRP:
		bpf_error("'igrp' modifier applied to host");

	case Q_PIM:
		bpf_error("'pim' modifier applied to host");

	case Q_VRRP:
		bpf_error("'vrrp' modifier applied to host");

	case Q_ATALK:
		bpf_error("ATALK host filtering not implemented");

	case Q_AARP:
		bpf_error("AARP host filtering not implemented");

	case Q_DECNET:
		bpf_error("'decnet' modifier applied to ip6 host");

	case Q_SCA:
		bpf_error("SCA host filtering not implemented");

	case Q_LAT:
		bpf_error("LAT host filtering not implemented");

	case Q_MOPDL:
		bpf_error("MOPDL host filtering not implemented");

	case Q_MOPRC:
		bpf_error("MOPRC host filtering not implemented");

	case Q_IPV6:
		return gen_hostop6(addr, mask, dir, ETHERTYPE_IPV6,
				  off_nl + 8, off_nl + 24);

	case Q_ICMPV6:
		bpf_error("'icmp6' modifier applied to host");

	case Q_AH:
		bpf_error("'ah' modifier applied to host");

	case Q_ESP:
		bpf_error("'esp' modifier applied to host");

	case Q_ISO:
		bpf_error("ISO host filtering not implemented");

	case Q_ESIS:
		bpf_error("'esis' modifier applied to host");

	case Q_ISIS:
		bpf_error("'isis' modifier applied to host");

	case Q_CLNP:
		bpf_error("'clnp' modifier applied to host");

	case Q_STP:
		bpf_error("'stp' modifier applied to host");

	case Q_IPX:
		bpf_error("IPX host filtering not implemented");

	case Q_NETBEUI:
		bpf_error("'netbeui' modifier applied to host");

	default:
		abort();
	}
	/* NOTREACHED */
}
#endif /*INET6*/

#ifndef INET6
static struct block *
gen_gateway(eaddr, alist, proto, dir)
	const u_char *eaddr;
	bpf_u_int32 **alist;
	int proto;
	int dir;
{
	struct block *b0, *b1, *tmp;

	if (dir != 0)
		bpf_error("direction applied to 'gateway'");

	switch (proto) {
	case Q_DEFAULT:
	case Q_IP:
	case Q_ARP:
	case Q_RARP:
		if (linktype == DLT_EN10MB)
			b0 = gen_ehostop(eaddr, Q_OR);
		else if (linktype == DLT_FDDI)
			b0 = gen_fhostop(eaddr, Q_OR);
		else if (linktype == DLT_IEEE802)
			b0 = gen_thostop(eaddr, Q_OR);
		else
			bpf_error(
			    "'gateway' supported only on ethernet, FDDI or token ring");

		b1 = gen_host(**alist++, 0xffffffff, proto, Q_OR);
		while (*alist) {
			tmp = gen_host(**alist++, 0xffffffff, proto, Q_OR);
			gen_or(b1, tmp);
			b1 = tmp;
		}
		gen_not(b1);
		gen_and(b0, b1);
		return b1;
	}
	bpf_error("illegal modifier of 'gateway'");
	/* NOTREACHED */
}
#endif

struct block *
gen_proto_abbrev(proto)
	int proto;
{
#ifdef INET6
	struct block *b0;
#endif
	struct block *b1;

	switch (proto) {

	case Q_SCTP:
		b1 = gen_proto(IPPROTO_SCTP, Q_IP, Q_DEFAULT);
#ifdef INET6
		b0 = gen_proto(IPPROTO_SCTP, Q_IPV6, Q_DEFAULT);
		gen_or(b0, b1);
#endif
		break;

	case Q_TCP:
		b1 = gen_proto(IPPROTO_TCP, Q_IP, Q_DEFAULT);
#ifdef INET6
		b0 = gen_proto(IPPROTO_TCP, Q_IPV6, Q_DEFAULT);
		gen_or(b0, b1);
#endif
		break;

	case Q_UDP:
		b1 = gen_proto(IPPROTO_UDP, Q_IP, Q_DEFAULT);
#ifdef INET6
		b0 = gen_proto(IPPROTO_UDP, Q_IPV6, Q_DEFAULT);
		gen_or(b0, b1);
#endif
		break;

	case Q_ICMP:
		b1 = gen_proto(IPPROTO_ICMP, Q_IP, Q_DEFAULT);
		break;

#ifndef	IPPROTO_IGMP
#define	IPPROTO_IGMP	2
#endif

	case Q_IGMP:
		b1 = gen_proto(IPPROTO_IGMP, Q_IP, Q_DEFAULT);
		break;

#ifndef	IPPROTO_IGRP
#define	IPPROTO_IGRP	9
#endif
	case Q_IGRP:
		b1 = gen_proto(IPPROTO_IGRP, Q_IP, Q_DEFAULT);
		break;

#ifndef IPPROTO_PIM
#define IPPROTO_PIM	103
#endif

	case Q_PIM:
		b1 = gen_proto(IPPROTO_PIM, Q_IP, Q_DEFAULT);
#ifdef INET6
		b0 = gen_proto(IPPROTO_PIM, Q_IPV6, Q_DEFAULT);
		gen_or(b0, b1);
#endif
		break;

#ifndef IPPROTO_VRRP
#define IPPROTO_VRRP	112
#endif

	case Q_VRRP:
		b1 = gen_proto(IPPROTO_VRRP, Q_IP, Q_DEFAULT);
		break;

	case Q_IP:
		b1 =  gen_linktype(ETHERTYPE_IP);
		break;

	case Q_ARP:
		b1 =  gen_linktype(ETHERTYPE_ARP);
		break;

	case Q_RARP:
		b1 =  gen_linktype(ETHERTYPE_REVARP);
		break;

	case Q_LINK:
		bpf_error("link layer applied in wrong context");

	case Q_ATALK:
		b1 =  gen_linktype(ETHERTYPE_ATALK);
		break;

	case Q_AARP:
		b1 =  gen_linktype(ETHERTYPE_AARP);
		break;

	case Q_DECNET:
		b1 =  gen_linktype(ETHERTYPE_DN);
		break;

	case Q_SCA:
		b1 =  gen_linktype(ETHERTYPE_SCA);
		break;

	case Q_LAT:
		b1 =  gen_linktype(ETHERTYPE_LAT);
		break;

	case Q_MOPDL:
		b1 =  gen_linktype(ETHERTYPE_MOPDL);
		break;

	case Q_MOPRC:
		b1 =  gen_linktype(ETHERTYPE_MOPRC);
		break;

#ifdef INET6
	case Q_IPV6:
		b1 = gen_linktype(ETHERTYPE_IPV6);
		break;

#ifndef IPPROTO_ICMPV6
#define IPPROTO_ICMPV6	58
#endif
	case Q_ICMPV6:
		b1 = gen_proto(IPPROTO_ICMPV6, Q_IPV6, Q_DEFAULT);
		break;
#endif /* INET6 */

#ifndef IPPROTO_AH
#define IPPROTO_AH	51
#endif
	case Q_AH:
		b1 = gen_proto(IPPROTO_AH, Q_IP, Q_DEFAULT);
#ifdef INET6
		b0 = gen_proto(IPPROTO_AH, Q_IPV6, Q_DEFAULT);
		gen_or(b0, b1);
#endif
		break;

#ifndef IPPROTO_ESP
#define IPPROTO_ESP	50
#endif
	case Q_ESP:
		b1 = gen_proto(IPPROTO_ESP, Q_IP, Q_DEFAULT);
#ifdef INET6
		b0 = gen_proto(IPPROTO_ESP, Q_IPV6, Q_DEFAULT);
		gen_or(b0, b1);
#endif
		break;

	case Q_ISO:
	        b1 = gen_linktype(LLCSAP_ISONS);
		break;

	case Q_ESIS:
	        b1 = gen_proto(ISO9542_ESIS, Q_ISO, Q_DEFAULT);
		break;

	case Q_ISIS:
	        b1 = gen_proto(ISO10589_ISIS, Q_ISO, Q_DEFAULT);
		break;

	case Q_CLNP:
	        b1 = gen_proto(ISO8473_CLNP, Q_ISO, Q_DEFAULT);
		break;

	case Q_STP:
	        b1 = gen_linktype(LLCSAP_8021D);
		break;

	case Q_IPX:
	        b1 = gen_linktype(LLCSAP_IPX);
		break;

	case Q_NETBEUI:
	        b1 = gen_linktype(LLCSAP_NETBEUI);
		break;

	default:
		abort();
	}
	return b1;
}

static struct block *
gen_ipfrag()
{
	struct slist *s;
	struct block *b;

	/* not ip frag */
	s = new_stmt(BPF_LD|BPF_H|BPF_ABS);
	s->s.k = off_nl + 6;
	b = new_block(JMP(BPF_JSET));
	b->s.k = 0x1fff;
	b->stmts = s;
	gen_not(b);

	return b;
}

static struct block *
gen_portatom(off, v)
	int off;
	bpf_int32 v;
{
	struct slist *s;
	struct block *b;

	s = new_stmt(BPF_LDX|BPF_MSH|BPF_B);
	s->s.k = off_nl;

	s->next = new_stmt(BPF_LD|BPF_IND|BPF_H);
	s->next->s.k = off_nl + off;

	b = new_block(JMP(BPF_JEQ));
	b->stmts = s;
	b->s.k = v;

	return b;
}

#ifdef INET6
static struct block *
gen_portatom6(off, v)
	int off;
	bpf_int32 v;
{
	return gen_cmp(off_nl + 40 + off, BPF_H, v);
}
#endif/*INET6*/

struct block *
gen_portop(port, proto, dir)
	int port, proto, dir;
{
	struct block *b0, *b1, *tmp;

	/* ip proto 'proto' */
	tmp = gen_cmp(off_nl + 9, BPF_B, (bpf_int32)proto);
	b0 = gen_ipfrag();
	gen_and(tmp, b0);

	switch (dir) {
	case Q_SRC:
		b1 = gen_portatom(0, (bpf_int32)port);
		break;

	case Q_DST:
		b1 = gen_portatom(2, (bpf_int32)port);
		break;

	case Q_OR:
	case Q_DEFAULT:
		tmp = gen_portatom(0, (bpf_int32)port);
		b1 = gen_portatom(2, (bpf_int32)port);
		gen_or(tmp, b1);
		break;

	case Q_AND:
		tmp = gen_portatom(0, (bpf_int32)port);
		b1 = gen_portatom(2, (bpf_int32)port);
		gen_and(tmp, b1);
		break;

	default:
		abort();
	}
	gen_and(b0, b1);

	return b1;
}

static struct block *
gen_port(port, ip_proto, dir)
	int port;
	int ip_proto;
	int dir;
{
	struct block *b0, *b1, *tmp;

	/* ether proto ip */
	b0 =  gen_linktype(ETHERTYPE_IP);

	switch (ip_proto) {
	case IPPROTO_UDP:
	case IPPROTO_TCP:
	case IPPROTO_SCTP:
		b1 = gen_portop(port, ip_proto, dir);
		break;

	case PROTO_UNDEF:
		tmp = gen_portop(port, IPPROTO_TCP, dir);
		b1 = gen_portop(port, IPPROTO_UDP, dir);
		gen_or(tmp, b1);
		tmp = gen_portop(port, IPPROTO_SCTP, dir);
		gen_or(tmp, b1);
		break;

	default:
		abort();
	}
	gen_and(b0, b1);
	return b1;
}

#ifdef INET6
struct block *
gen_portop6(port, proto, dir)
	int port, proto, dir;
{
	struct block *b0, *b1, *tmp;

	/* ip proto 'proto' */
	b0 = gen_cmp(off_nl + 6, BPF_B, (bpf_int32)proto);

	switch (dir) {
	case Q_SRC:
		b1 = gen_portatom6(0, (bpf_int32)port);
		break;

	case Q_DST:
		b1 = gen_portatom6(2, (bpf_int32)port);
		break;

	case Q_OR:
	case Q_DEFAULT:
		tmp = gen_portatom6(0, (bpf_int32)port);
		b1 = gen_portatom6(2, (bpf_int32)port);
		gen_or(tmp, b1);
		break;

	case Q_AND:
		tmp = gen_portatom6(0, (bpf_int32)port);
		b1 = gen_portatom6(2, (bpf_int32)port);
		gen_and(tmp, b1);
		break;

	default:
		abort();
	}
	gen_and(b0, b1);

	return b1;
}

static struct block *
gen_port6(port, ip_proto, dir)
	int port;
	int ip_proto;
	int dir;
{
	struct block *b0, *b1, *tmp;

	/* ether proto ip */
	b0 =  gen_linktype(ETHERTYPE_IPV6);

	switch (ip_proto) {
	case IPPROTO_UDP:
	case IPPROTO_TCP:
	case IPPROTO_SCTP:
		b1 = gen_portop6(port, ip_proto, dir);
		break;

	case PROTO_UNDEF:
		tmp = gen_portop6(port, IPPROTO_TCP, dir);
		b1 = gen_portop6(port, IPPROTO_UDP, dir);
		gen_or(tmp, b1);
		tmp = gen_portop6(port, IPPROTO_SCTP, dir);
		gen_or(tmp, b1);
		break;

	default:
		abort();
	}
	gen_and(b0, b1);
	return b1;
}
#endif /* INET6 */

static int
lookup_proto(name, proto)
	register const char *name;
	register int proto;
{
	register int v;

	switch (proto) {

	case Q_DEFAULT:
	case Q_IP:
	case Q_IPV6:
		v = pcap_nametoproto(name);
		if (v == PROTO_UNDEF)
			bpf_error("unknown ip proto '%s'", name);
		break;

	case Q_LINK:
		/* XXX should look up h/w protocol type based on linktype */
		v = pcap_nametoeproto(name);
		if (v == PROTO_UNDEF)
			bpf_error("unknown ether proto '%s'", name);
		break;

	case Q_ISO:
		if (strcmp(name, "esis") == 0)
			v = ISO9542_ESIS;
		else if (strcmp(name, "isis") == 0)
			v = ISO10589_ISIS;
		else if (strcmp(name, "clnp") == 0)
			v = ISO8473_CLNP;
		else
			bpf_error("unknown osi proto '%s'", name);
		break;

	default:
		v = PROTO_UNDEF;
		break;
	}
	return v;
}

#if 0
struct stmt *
gen_joinsp(s, n)
	struct stmt **s;
	int n;
{
	return NULL;
}
#endif

static struct block *
gen_protochain(v, proto, dir)
	int v;
	int proto;
	int dir;
{
#ifdef NO_PROTOCHAIN
	return gen_proto(v, proto, dir);
#else
	struct block *b0, *b;
	struct slist *s[100];
	int fix2, fix3, fix4, fix5;
	int ahcheck, again, end;
	int i, max;
	int reg2 = alloc_reg();

	memset(s, 0, sizeof(s));
	fix2 = fix3 = fix4 = fix5 = 0;

	switch (proto) {
	case Q_IP:
	case Q_IPV6:
		break;
	case Q_DEFAULT:
		b0 = gen_protochain(v, Q_IP, dir);
		b = gen_protochain(v, Q_IPV6, dir);
		gen_or(b0, b);
		return b;
	default:
		bpf_error("bad protocol applied for 'protochain'");
		/*NOTREACHED*/
	}

	no_optimize = 1; /*this code is not compatible with optimzer yet */

	/*
	 * s[0] is a dummy entry to protect other BPF insn from damaged
	 * by s[fix] = foo with uninitialized variable "fix".  It is somewhat
	 * hard to find interdependency made by jump table fixup.
	 */
	i = 0;
	s[i] = new_stmt(0);	/*dummy*/
	i++;

	switch (proto) {
	case Q_IP:
		b0 = gen_linktype(ETHERTYPE_IP);

		/* A = ip->ip_p */
		s[i] = new_stmt(BPF_LD|BPF_ABS|BPF_B);
		s[i]->s.k = off_nl + 9;
		i++;
		/* X = ip->ip_hl << 2 */
		s[i] = new_stmt(BPF_LDX|BPF_MSH|BPF_B);
		s[i]->s.k = off_nl;
		i++;
		break;
#ifdef INET6
	case Q_IPV6:
		b0 = gen_linktype(ETHERTYPE_IPV6);

		/* A = ip6->ip_nxt */
		s[i] = new_stmt(BPF_LD|BPF_ABS|BPF_B);
		s[i]->s.k = off_nl + 6;
		i++;
		/* X = sizeof(struct ip6_hdr) */
		s[i] = new_stmt(BPF_LDX|BPF_IMM);
		s[i]->s.k = 40;
		i++;
		break;
#endif
	default:
		bpf_error("unsupported proto to gen_protochain");
		/*NOTREACHED*/
	}

	/* again: if (A == v) goto end; else fall through; */
	again = i;
	s[i] = new_stmt(BPF_JMP|BPF_JEQ|BPF_K);
	s[i]->s.k = v;
	s[i]->s.jt = NULL;		/*later*/
	s[i]->s.jf = NULL;		/*update in next stmt*/
	fix5 = i;
	i++;

#ifndef IPPROTO_NONE
#define IPPROTO_NONE	59
#endif
	/* if (A == IPPROTO_NONE) goto end */
	s[i] = new_stmt(BPF_JMP|BPF_JEQ|BPF_K);
	s[i]->s.jt = NULL;	/*later*/
	s[i]->s.jf = NULL;	/*update in next stmt*/
	s[i]->s.k = IPPROTO_NONE;
	s[fix5]->s.jf = s[i];
	fix2 = i;
	i++;

#ifdef INET6
	if (proto == Q_IPV6) {
		int v6start, v6end, v6advance, j;

		v6start = i;
		/* if (A == IPPROTO_HOPOPTS) goto v6advance */
		s[i] = new_stmt(BPF_JMP|BPF_JEQ|BPF_K);
		s[i]->s.jt = NULL;	/*later*/
		s[i]->s.jf = NULL;	/*update in next stmt*/
		s[i]->s.k = IPPROTO_HOPOPTS;
		s[fix2]->s.jf = s[i];
		i++;
		/* if (A == IPPROTO_DSTOPTS) goto v6advance */
		s[i - 1]->s.jf = s[i] = new_stmt(BPF_JMP|BPF_JEQ|BPF_K);
		s[i]->s.jt = NULL;	/*later*/
		s[i]->s.jf = NULL;	/*update in next stmt*/
		s[i]->s.k = IPPROTO_DSTOPTS;
		i++;
		/* if (A == IPPROTO_ROUTING) goto v6advance */
		s[i - 1]->s.jf = s[i] = new_stmt(BPF_JMP|BPF_JEQ|BPF_K);
		s[i]->s.jt = NULL;	/*later*/
		s[i]->s.jf = NULL;	/*update in next stmt*/
		s[i]->s.k = IPPROTO_ROUTING;
		i++;
		/* if (A == IPPROTO_FRAGMENT) goto v6advance; else goto ahcheck; */
		s[i - 1]->s.jf = s[i] = new_stmt(BPF_JMP|BPF_JEQ|BPF_K);
		s[i]->s.jt = NULL;	/*later*/
		s[i]->s.jf = NULL;	/*later*/
		s[i]->s.k = IPPROTO_FRAGMENT;
		fix3 = i;
		v6end = i;
		i++;

		/* v6advance: */
		v6advance = i;

		/*
		 * in short,
		 * A = P[X];
		 * X = X + (P[X + 1] + 1) * 8;
		 */
		/* A = X */
		s[i] = new_stmt(BPF_MISC|BPF_TXA);
		i++;
		/* A = P[X + packet head] */
		s[i] = new_stmt(BPF_LD|BPF_IND|BPF_B);
		s[i]->s.k = off_nl;
		i++;
		/* MEM[reg2] = A */
		s[i] = new_stmt(BPF_ST);
		s[i]->s.k = reg2;
		i++;
		/* A = X */
		s[i] = new_stmt(BPF_MISC|BPF_TXA);
		i++;
		/* A += 1 */
		s[i] = new_stmt(BPF_ALU|BPF_ADD|BPF_K);
		s[i]->s.k = 1;
		i++;
		/* X = A */
		s[i] = new_stmt(BPF_MISC|BPF_TAX);
		i++;
		/* A = P[X + packet head]; */
		s[i] = new_stmt(BPF_LD|BPF_IND|BPF_B);
		s[i]->s.k = off_nl;
		i++;
		/* A += 1 */
		s[i] = new_stmt(BPF_ALU|BPF_ADD|BPF_K);
		s[i]->s.k = 1;
		i++;
		/* A *= 8 */
		s[i] = new_stmt(BPF_ALU|BPF_MUL|BPF_K);
		s[i]->s.k = 8;
		i++;
		/* X = A; */
		s[i] = new_stmt(BPF_MISC|BPF_TAX);
		i++;
		/* A = MEM[reg2] */
		s[i] = new_stmt(BPF_LD|BPF_MEM);
		s[i]->s.k = reg2;
		i++;

		/* goto again; (must use BPF_JA for backward jump) */
		s[i] = new_stmt(BPF_JMP|BPF_JA);
		s[i]->s.k = again - i - 1;
		s[i - 1]->s.jf = s[i];
		i++;

		/* fixup */
		for (j = v6start; j <= v6end; j++)
			s[j]->s.jt = s[v6advance];
	} else
#endif
	{
		/* nop */
		s[i] = new_stmt(BPF_ALU|BPF_ADD|BPF_K);
		s[i]->s.k = 0;
		s[fix2]->s.jf = s[i];
		i++;
	}

	/* ahcheck: */
	ahcheck = i;
	/* if (A == IPPROTO_AH) then fall through; else goto end; */
	s[i] = new_stmt(BPF_JMP|BPF_JEQ|BPF_K);
	s[i]->s.jt = NULL;	/*later*/
	s[i]->s.jf = NULL;	/*later*/
	s[i]->s.k = IPPROTO_AH;
	if (fix3)
		s[fix3]->s.jf = s[ahcheck];
	fix4 = i;
	i++;

	/*
	 * in short,
	 * A = P[X];
	 * X = X + (P[X + 1] + 2) * 4;
	 */
	/* A = X */
	s[i - 1]->s.jt = s[i] = new_stmt(BPF_MISC|BPF_TXA);
	i++;
	/* A = P[X + packet head]; */
	s[i] = new_stmt(BPF_LD|BPF_IND|BPF_B);
	s[i]->s.k = off_nl;
	i++;
	/* MEM[reg2] = A */
	s[i] = new_stmt(BPF_ST);
	s[i]->s.k = reg2;
	i++;
	/* A = X */
	s[i - 1]->s.jt = s[i] = new_stmt(BPF_MISC|BPF_TXA);
	i++;
	/* A += 1 */
	s[i] = new_stmt(BPF_ALU|BPF_ADD|BPF_K);
	s[i]->s.k = 1;
	i++;
	/* X = A */
	s[i] = new_stmt(BPF_MISC|BPF_TAX);
	i++;
	/* A = P[X + packet head] */
	s[i] = new_stmt(BPF_LD|BPF_IND|BPF_B);
	s[i]->s.k = off_nl;
	i++;
	/* A += 2 */
	s[i] = new_stmt(BPF_ALU|BPF_ADD|BPF_K);
	s[i]->s.k = 2;
	i++;
	/* A *= 4 */
	s[i] = new_stmt(BPF_ALU|BPF_MUL|BPF_K);
	s[i]->s.k = 4;
	i++;
	/* X = A; */
	s[i] = new_stmt(BPF_MISC|BPF_TAX);
	i++;
	/* A = MEM[reg2] */
	s[i] = new_stmt(BPF_LD|BPF_MEM);
	s[i]->s.k = reg2;
	i++;

	/* goto again; (must use BPF_JA for backward jump) */
	s[i] = new_stmt(BPF_JMP|BPF_JA);
	s[i]->s.k = again - i - 1;
	i++;

	/* end: nop */
	end = i;
	s[i] = new_stmt(BPF_ALU|BPF_ADD|BPF_K);
	s[i]->s.k = 0;
	s[fix2]->s.jt = s[end];
	s[fix4]->s.jf = s[end];
	s[fix5]->s.jt = s[end];
	i++;

	/*
	 * make slist chain
	 */
	max = i;
	for (i = 0; i < max - 1; i++)
		s[i]->next = s[i + 1];
	s[max - 1]->next = NULL;

	/*
	 * emit final check
	 */
	b = new_block(JMP(BPF_JEQ));
	b->stmts = s[1];	/*remember, s[0] is dummy*/
	b->s.k = v;

	free_reg(reg2);

	gen_and(b0, b);
	return b;
#endif
}

static struct block *
gen_proto(v, proto, dir)
	int v;
	int proto;
	int dir;
{
	struct block *b0, *b1;

	if (dir != Q_DEFAULT)
		bpf_error("direction applied to 'proto'");

	switch (proto) {
	case Q_DEFAULT:
#ifdef INET6
		b0 = gen_proto(v, Q_IP, dir);
		b1 = gen_proto(v, Q_IPV6, dir);
		gen_or(b0, b1);
		return b1;
#else
		/*FALLTHROUGH*/
#endif
	case Q_IP:
		b0 = gen_linktype(ETHERTYPE_IP);
#ifndef CHASE_CHAIN
		b1 = gen_cmp(off_nl + 9, BPF_B, (bpf_int32)v);
#else
		b1 = gen_protochain(v, Q_IP);
#endif
		gen_and(b0, b1);
		return b1;

	case Q_ISO:
		b0 = gen_linktype(LLCSAP_ISONS);
		b1 = gen_cmp(off_nl + 3, BPF_B, (long)v);
		gen_and(b0, b1);
		return b1;

	case Q_ARP:
		bpf_error("arp does not encapsulate another protocol");
		/* NOTREACHED */

	case Q_RARP:
		bpf_error("rarp does not encapsulate another protocol");
		/* NOTREACHED */

	case Q_ATALK:
		bpf_error("atalk encapsulation is not specifiable");
		/* NOTREACHED */

	case Q_DECNET:
		bpf_error("decnet encapsulation is not specifiable");
		/* NOTREACHED */

	case Q_SCA:
		bpf_error("sca does not encapsulate another protocol");
		/* NOTREACHED */

	case Q_LAT:
		bpf_error("lat does not encapsulate another protocol");
		/* NOTREACHED */

	case Q_MOPRC:
		bpf_error("moprc does not encapsulate another protocol");
		/* NOTREACHED */

	case Q_MOPDL:
		bpf_error("mopdl does not encapsulate another protocol");
		/* NOTREACHED */

	case Q_LINK:
		return gen_linktype(v);

	case Q_UDP:
		bpf_error("'udp proto' is bogus");
		/* NOTREACHED */

	case Q_TCP:
		bpf_error("'tcp proto' is bogus");
		/* NOTREACHED */

	case Q_SCTP:
		bpf_error("'sctp proto' is bogus");
		/* NOTREACHED */

	case Q_ICMP:
		bpf_error("'icmp proto' is bogus");
		/* NOTREACHED */

	case Q_IGMP:
		bpf_error("'igmp proto' is bogus");
		/* NOTREACHED */

	case Q_IGRP:
		bpf_error("'igrp proto' is bogus");
		/* NOTREACHED */

	case Q_PIM:
		bpf_error("'pim proto' is bogus");
		/* NOTREACHED */

	case Q_VRRP:
		bpf_error("'vrrp proto' is bogus");
		/* NOTREACHED */

#ifdef INET6
	case Q_IPV6:
		b0 = gen_linktype(ETHERTYPE_IPV6);
#ifndef CHASE_CHAIN
		b1 = gen_cmp(off_nl + 6, BPF_B, (bpf_int32)v);
#else
		b1 = gen_protochain(v, Q_IPV6);
#endif
		gen_and(b0, b1);
		return b1;

	case Q_ICMPV6:
		bpf_error("'icmp6 proto' is bogus");
#endif /* INET6 */

	case Q_AH:
		bpf_error("'ah proto' is bogus");

	case Q_ESP:
		bpf_error("'ah proto' is bogus");

	case Q_STP:
		bpf_error("'stp proto' is bogus");

	case Q_IPX:
		bpf_error("'ipx proto' is bogus");

	case Q_NETBEUI:
		bpf_error("'netbeui proto' is bogus");

	default:
		abort();
		/* NOTREACHED */
	}
	/* NOTREACHED */
}

struct block *
gen_scode(name, q)
	register const char *name;
	struct qual q;
{
	int proto = q.proto;
	int dir = q.dir;
	int tproto;
	u_char *eaddr;
	bpf_u_int32 mask, addr;
#ifndef INET6
	bpf_u_int32 **alist;
#else
	int tproto6;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	struct addrinfo *res, *res0;
	struct in6_addr mask128;
#endif /*INET6*/
	struct block *b, *tmp;
	int port, real_proto;

	switch (q.addr) {

	case Q_NET:
		addr = pcap_nametonetaddr(name);
		if (addr == 0)
			bpf_error("unknown network '%s'", name);
		/* Left justify network addr and calculate its network mask */
		mask = 0xffffffff;
		while (addr && (addr & 0xff000000) == 0) {
			addr <<= 8;
			mask <<= 8;
		}
		return gen_host(addr, mask, proto, dir);

	case Q_DEFAULT:
	case Q_HOST:
		if (proto == Q_LINK) {
			switch (linktype) {

			case DLT_EN10MB:
				eaddr = pcap_ether_hostton(name);
				if (eaddr == NULL)
					bpf_error(
					    "unknown ether host '%s'", name);
				b = gen_ehostop(eaddr, dir);
				free(eaddr);
				return b;

			case DLT_FDDI:
				eaddr = pcap_ether_hostton(name);
				if (eaddr == NULL)
					bpf_error(
					    "unknown FDDI host '%s'", name);
				b = gen_fhostop(eaddr, dir);
				free(eaddr);
				return b;

			case DLT_IEEE802:
				eaddr = pcap_ether_hostton(name);
				if (eaddr == NULL)
					bpf_error(
					    "unknown token ring host '%s'", name);
				b = gen_thostop(eaddr, dir);
				free(eaddr);
				return b;

			default:
				bpf_error(
			"only ethernet/FDDI/token ring supports link-level host name");
				break;
			}
		} else if (proto == Q_DECNET) {
			unsigned short dn_addr = __pcap_nametodnaddr(name);
			/*
			 * I don't think DECNET hosts can be multihomed, so
			 * there is no need to build up a list of addresses
			 */
			return (gen_host(dn_addr, 0, proto, dir));
		} else {
#ifndef INET6
			alist = pcap_nametoaddr(name);
			if (alist == NULL || *alist == NULL)
				bpf_error("unknown host '%s'", name);
			tproto = proto;
			if (off_linktype == -1 && tproto == Q_DEFAULT)
				tproto = Q_IP;
			b = gen_host(**alist++, 0xffffffff, tproto, dir);
			while (*alist) {
				tmp = gen_host(**alist++, 0xffffffff,
					       tproto, dir);
				gen_or(b, tmp);
				b = tmp;
			}
			return b;
#else
			memset(&mask128, 0xff, sizeof(mask128));
			res0 = res = pcap_nametoaddrinfo(name);
			if (res == NULL)
				bpf_error("unknown host '%s'", name);
			b = tmp = NULL;
			tproto = tproto6 = proto;
			if (off_linktype == -1 && tproto == Q_DEFAULT) {
				tproto = Q_IP;
				tproto6 = Q_IPV6;
			}
			for (res = res0; res; res = res->ai_next) {
				switch (res->ai_family) {
				case AF_INET:
					if (tproto == Q_IPV6)
						continue;

					sin = (struct sockaddr_in *)
						res->ai_addr;
					tmp = gen_host(ntohl(sin->sin_addr.s_addr),
						0xffffffff, tproto, dir);
					break;
				case AF_INET6:
					if (tproto6 == Q_IP)
						continue;

					sin6 = (struct sockaddr_in6 *)
						res->ai_addr;
					tmp = gen_host6(&sin6->sin6_addr,
						&mask128, tproto6, dir);
					break;
				default:
					continue;
				}
				if (b)
					gen_or(b, tmp);
				b = tmp;
			}
			freeaddrinfo(res0);
			if (b == NULL) {
				bpf_error("unknown host '%s'%s", name,
				    (proto == Q_DEFAULT)
					? ""
					: " for specified address family");
			}
			return b;
#endif /*INET6*/
		}

	case Q_PORT:
		if (proto != Q_DEFAULT &&
		    proto != Q_UDP && proto != Q_TCP && proto != Q_SCTP)
			bpf_error("illegal qualifier of 'port'");
		if (pcap_nametoport(name, &port, &real_proto) == 0)
			bpf_error("unknown port '%s'", name);
		if (proto == Q_UDP) {
			if (real_proto == IPPROTO_TCP)
				bpf_error("port '%s' is tcp", name);
			else if (real_proto == IPPROTO_SCTP)
				bpf_error("port '%s' is sctp", name);
			else
				/* override PROTO_UNDEF */
				real_proto = IPPROTO_UDP;
		}
		if (proto == Q_TCP) {
			if (real_proto == IPPROTO_UDP)
				bpf_error("port '%s' is udp", name);

			else if (real_proto == IPPROTO_SCTP)
				bpf_error("port '%s' is sctp", name);
			else
				/* override PROTO_UNDEF */
				real_proto = IPPROTO_TCP;
		}
		if (proto == Q_SCTP) {
			if (real_proto == IPPROTO_UDP)
				bpf_error("port '%s' is udp", name);

			else if (real_proto == IPPROTO_TCP)
				bpf_error("port '%s' is tcp", name);
			else
				/* override PROTO_UNDEF */
				real_proto = IPPROTO_SCTP;
		}
#ifndef INET6
		return gen_port(port, real_proto, dir);
#else
	    {
		struct block *b;
		b = gen_port(port, real_proto, dir);
		gen_or(gen_port6(port, real_proto, dir), b);
		return b;
	    }
#endif /* INET6 */

	case Q_GATEWAY:
#ifndef INET6
		eaddr = pcap_ether_hostton(name);
		if (eaddr == NULL)
			bpf_error("unknown ether host: %s", name);

		alist = pcap_nametoaddr(name);
		if (alist == NULL || *alist == NULL)
			bpf_error("unknown host '%s'", name);
		b = gen_gateway(eaddr, alist, proto, dir);
		free(eaddr);
		return b;
#else
		bpf_error("'gateway' not supported in this configuration");
#endif /*INET6*/

	case Q_PROTO:
		real_proto = lookup_proto(name, proto);
		if (real_proto >= 0)
			return gen_proto(real_proto, proto, dir);
		else
			bpf_error("unknown protocol: %s", name);

	case Q_PROTOCHAIN:
		real_proto = lookup_proto(name, proto);
		if (real_proto >= 0)
			return gen_protochain(real_proto, proto, dir);
		else
			bpf_error("unknown protocol: %s", name);


	case Q_UNDEF:
		syntax();
		/* NOTREACHED */
	}
	abort();
	/* NOTREACHED */
}

struct block *
gen_mcode(s1, s2, masklen, q)
	register const char *s1, *s2;
	register int masklen;
	struct qual q;
{
	register int nlen, mlen;
	bpf_u_int32 n, m;

	nlen = __pcap_atoin(s1, &n);
	/* Promote short ipaddr */
	n <<= 32 - nlen;

	if (s2 != NULL) {
		mlen = __pcap_atoin(s2, &m);
		/* Promote short ipaddr */
		m <<= 32 - mlen;
		if ((n & ~m) != 0)
			bpf_error("non-network bits set in \"%s mask %s\"",
			    s1, s2);
	} else {
		/* Convert mask len to mask */
		if (masklen > 32)
			bpf_error("mask length must be <= 32");
		m = 0xffffffff << (32 - masklen);
		if ((n & ~m) != 0)
			bpf_error("non-network bits set in \"%s/%d\"",
			    s1, masklen);
	}

	switch (q.addr) {

	case Q_NET:
		return gen_host(n, m, q.proto, q.dir);

	default:
		bpf_error("Mask syntax for networks only");
		/* NOTREACHED */
	}
}

struct block *
gen_ncode(s, v, q)
	register const char *s;
	bpf_u_int32 v;
	struct qual q;
{
	bpf_u_int32 mask;
	int proto = q.proto;
	int dir = q.dir;
	register int vlen;

	if (s == NULL)
		vlen = 32;
	else if (q.proto == Q_DECNET)
		vlen = __pcap_atodn(s, &v);
	else
		vlen = __pcap_atoin(s, &v);

	switch (q.addr) {

	case Q_DEFAULT:
	case Q_HOST:
	case Q_NET:
		if (proto == Q_DECNET)
			return gen_host(v, 0, proto, dir);
		else if (proto == Q_LINK) {
			bpf_error("illegal link layer address");
		} else {
			mask = 0xffffffff;
			if (s == NULL && q.addr == Q_NET) {
				/* Promote short net number */
				while (v && (v & 0xff000000) == 0) {
					v <<= 8;
					mask <<= 8;
				}
			} else {
				/* Promote short ipaddr */
				v <<= 32 - vlen;
				mask <<= 32 - vlen;
			}
			return gen_host(v, mask, proto, dir);
		}

	case Q_PORT:
		if (proto == Q_UDP)
			proto = IPPROTO_UDP;
		else if (proto == Q_TCP)
			proto = IPPROTO_TCP;
		else if (proto == Q_SCTP)
			proto = IPPROTO_SCTP;
		else if (proto == Q_DEFAULT)
			proto = PROTO_UNDEF;
		else
			bpf_error("illegal qualifier of 'port'");

#ifndef INET6
		return gen_port((int)v, proto, dir);
#else
	    {
		struct block *b;
		b = gen_port((int)v, proto, dir);
		gen_or(gen_port6((int)v, proto, dir), b);
		return b;
	    }
#endif /* INET6 */

	case Q_GATEWAY:
		bpf_error("'gateway' requires a name");
		/* NOTREACHED */

	case Q_PROTO:
		return gen_proto((int)v, proto, dir);

	case Q_PROTOCHAIN:
		return gen_protochain((int)v, proto, dir);

	case Q_UNDEF:
		syntax();
		/* NOTREACHED */

	default:
		abort();
		/* NOTREACHED */
	}
	/* NOTREACHED */
}

#ifdef INET6
struct block *
gen_mcode6(s1, s2, masklen, q)
	register const char *s1, *s2;
	register int masklen;
	struct qual q;
{
	struct addrinfo *res;
	struct in6_addr *addr;
	struct in6_addr mask;
	struct block *b;
	u_int32_t *a, *m;

	if (s2)
		bpf_error("no mask %s supported", s2);

	res = pcap_nametoaddrinfo(s1);
	if (!res)
		bpf_error("invalid ip6 address %s", s1);
	if (res->ai_next)
		bpf_error("%s resolved to multiple address", s1);
	addr = &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr;

	if (sizeof(mask) * 8 < masklen)
		bpf_error("mask length must be <= %u", (unsigned int)(sizeof(mask) * 8));
	memset(&mask, 0, sizeof(mask));
	memset(&mask, 0xff, masklen / 8);
	if (masklen % 8) {
		mask.s6_addr[masklen / 8] =
			(0xff << (8 - masklen % 8)) & 0xff;
	}

	a = (u_int32_t *)addr;
	m = (u_int32_t *)&mask;
	if ((a[0] & ~m[0]) || (a[1] & ~m[1])
	 || (a[2] & ~m[2]) || (a[3] & ~m[3])) {
		bpf_error("non-network bits set in \"%s/%d\"", s1, masklen);
	}

	switch (q.addr) {

	case Q_DEFAULT:
	case Q_HOST:
		if (masklen != 128)
			bpf_error("Mask syntax for networks only");
		/* FALLTHROUGH */

	case Q_NET:
		b = gen_host6(addr, &mask, q.proto, q.dir);
		freeaddrinfo(res);
		return b;

	default:
		bpf_error("invalid qualifier against IPv6 address");
		/* NOTREACHED */
	}
}
#endif /*INET6*/

struct block *
gen_ecode(eaddr, q)
	register const u_char *eaddr;
	struct qual q;
{
	if ((q.addr == Q_HOST || q.addr == Q_DEFAULT) && q.proto == Q_LINK) {
		if (linktype == DLT_EN10MB)
			return gen_ehostop(eaddr, (int)q.dir);
		if (linktype == DLT_FDDI)
			return gen_fhostop(eaddr, (int)q.dir);
		if (linktype == DLT_IEEE802)
			return gen_thostop(eaddr, (int)q.dir);
		bpf_error("ethernet addresses supported only on ethernet, FDDI or token ring");
	}
	bpf_error("ethernet address used in non-ether expression");
	/* NOTREACHED */
}

void
sappend(s0, s1)
	struct slist *s0, *s1;
{
	/*
	 * This is definitely not the best way to do this, but the
	 * lists will rarely get long.
	 */
	while (s0->next)
		s0 = s0->next;
	s0->next = s1;
}

static struct slist *
xfer_to_x(a)
	struct arth *a;
{
	struct slist *s;

	s = new_stmt(BPF_LDX|BPF_MEM);
	s->s.k = a->regno;
	return s;
}

static struct slist *
xfer_to_a(a)
	struct arth *a;
{
	struct slist *s;

	s = new_stmt(BPF_LD|BPF_MEM);
	s->s.k = a->regno;
	return s;
}

struct arth *
gen_load(proto, index, size)
	int proto;
	struct arth *index;
	int size;
{
	struct slist *s, *tmp;
	struct block *b;
	int regno = alloc_reg();

	free_reg(index->regno);
	switch (size) {

	default:
		bpf_error("data size must be 1, 2, or 4");

	case 1:
		size = BPF_B;
		break;

	case 2:
		size = BPF_H;
		break;

	case 4:
		size = BPF_W;
		break;
	}
	switch (proto) {
	default:
		bpf_error("unsupported index operation");

	case Q_LINK:
		s = xfer_to_x(index);
		tmp = new_stmt(BPF_LD|BPF_IND|size);
		sappend(s, tmp);
		sappend(index->s, s);
		break;

	case Q_IP:
	case Q_ARP:
	case Q_RARP:
	case Q_ATALK:
	case Q_DECNET:
	case Q_SCA:
	case Q_LAT:
	case Q_MOPRC:
	case Q_MOPDL:
#ifdef INET6
	case Q_IPV6:
#endif
		/* XXX Note that we assume a fixed link header here. */
		s = xfer_to_x(index);
		tmp = new_stmt(BPF_LD|BPF_IND|size);
		tmp->s.k = off_nl;
		sappend(s, tmp);
		sappend(index->s, s);

		b = gen_proto_abbrev(proto);
		if (index->b)
			gen_and(index->b, b);
		index->b = b;
		break;

	case Q_SCTP:
	case Q_TCP:
	case Q_UDP:
	case Q_ICMP:
	case Q_IGMP:
	case Q_IGRP:
	case Q_PIM:
	case Q_VRRP:
		s = new_stmt(BPF_LDX|BPF_MSH|BPF_B);
		s->s.k = off_nl;
		sappend(s, xfer_to_a(index));
		sappend(s, new_stmt(BPF_ALU|BPF_ADD|BPF_X));
		sappend(s, new_stmt(BPF_MISC|BPF_TAX));
		sappend(s, tmp = new_stmt(BPF_LD|BPF_IND|size));
		tmp->s.k = off_nl;
		sappend(index->s, s);

		gen_and(gen_proto_abbrev(proto), b = gen_ipfrag());
		if (index->b)
			gen_and(index->b, b);
#ifdef INET6
		gen_and(gen_proto_abbrev(Q_IP), b);
#endif
		index->b = b;
		break;
#ifdef INET6
	case Q_ICMPV6:
		bpf_error("IPv6 upper-layer protocol is not supported by proto[x]");
		/*NOTREACHED*/
#endif
	}
	index->regno = regno;
	s = new_stmt(BPF_ST);
	s->s.k = regno;
	sappend(index->s, s);

	return index;
}

struct block *
gen_relation(code, a0, a1, reversed)
	int code;
	struct arth *a0, *a1;
	int reversed;
{
	struct slist *s0, *s1, *s2;
	struct block *b, *tmp;

	s0 = xfer_to_x(a1);
	s1 = xfer_to_a(a0);
	s2 = new_stmt(BPF_ALU|BPF_SUB|BPF_X);
	b = new_block(JMP(code));
	if (code == BPF_JGT || code == BPF_JGE) {
		reversed = !reversed;
		b->s.k = 0x80000000;
	}
	if (reversed)
		gen_not(b);

	sappend(s1, s2);
	sappend(s0, s1);
	sappend(a1->s, s0);
	sappend(a0->s, a1->s);

	b->stmts = a0->s;

	free_reg(a0->regno);
	free_reg(a1->regno);

	/* 'and' together protocol checks */
	if (a0->b) {
		if (a1->b) {
			gen_and(a0->b, tmp = a1->b);
		}
		else
			tmp = a0->b;
	} else
		tmp = a1->b;

	if (tmp)
		gen_and(tmp, b);

	return b;
}

struct arth *
gen_loadlen()
{
	int regno = alloc_reg();
	struct arth *a = (struct arth *)newchunk(sizeof(*a));
	struct slist *s;

	s = new_stmt(BPF_LD|BPF_LEN);
	s->next = new_stmt(BPF_ST);
	s->next->s.k = regno;
	a->s = s;
	a->regno = regno;

	return a;
}

struct arth *
gen_loadi(val)
	int val;
{
	struct arth *a;
	struct slist *s;
	int reg;

	a = (struct arth *)newchunk(sizeof(*a));

	reg = alloc_reg();

	s = new_stmt(BPF_LD|BPF_IMM);
	s->s.k = val;
	s->next = new_stmt(BPF_ST);
	s->next->s.k = reg;
	a->s = s;
	a->regno = reg;

	return a;
}

struct arth *
gen_neg(a)
	struct arth *a;
{
	struct slist *s;

	s = xfer_to_a(a);
	sappend(a->s, s);
	s = new_stmt(BPF_ALU|BPF_NEG);
	s->s.k = 0;
	sappend(a->s, s);
	s = new_stmt(BPF_ST);
	s->s.k = a->regno;
	sappend(a->s, s);

	return a;
}

struct arth *
gen_arth(code, a0, a1)
	int code;
	struct arth *a0, *a1;
{
	struct slist *s0, *s1, *s2;

	s0 = xfer_to_x(a1);
	s1 = xfer_to_a(a0);
	s2 = new_stmt(BPF_ALU|BPF_X|code);

	sappend(s1, s2);
	sappend(s0, s1);
	sappend(a1->s, s0);
	sappend(a0->s, a1->s);

	free_reg(a1->regno);

	s0 = new_stmt(BPF_ST);
	a0->regno = s0->s.k = alloc_reg();
	sappend(a0->s, s0);

	return a0;
}

/*
 * Here we handle simple allocation of the scratch registers.
 * If too many registers are alloc'd, the allocator punts.
 */
static int regused[BPF_MEMWORDS];
static int curreg;

/*
 * Return the next free register.
 */
static int
alloc_reg()
{
	int n = BPF_MEMWORDS;

	while (--n >= 0) {
		if (regused[curreg])
			curreg = (curreg + 1) % BPF_MEMWORDS;
		else {
			regused[curreg] = 1;
			return curreg;
		}
	}
	bpf_error("too many registers needed to evaluate expression");
	/* NOTREACHED */
}

/*
 * Return a register to the table so it can
 * be used later.
 */
static void
free_reg(n)
	int n;
{
	regused[n] = 0;
}

static struct block *
gen_len(jmp, n)
	int jmp, n;
{
	struct slist *s;
	struct block *b;

	s = new_stmt(BPF_LD|BPF_LEN);
	b = new_block(JMP(jmp));
	b->stmts = s;
	b->s.k = n;

	return b;
}

struct block *
gen_greater(n)
	int n;
{
	return gen_len(BPF_JGE, n);
}

/*
 * Actually, this is less than or equal.
 */
struct block *
gen_less(n)
	int n;
{
	struct block *b;

	b = gen_len(BPF_JGT, n);
	gen_not(b);

	return b;
}

struct block *
gen_byteop(op, idx, val)
	int op, idx, val;
{
	struct block *b;
	struct slist *s;

	switch (op) {
	default:
		abort();

	case '=':
		return gen_cmp((u_int)idx, BPF_B, (bpf_int32)val);

	case '<':
		b = gen_cmp((u_int)idx, BPF_B, (bpf_int32)val);
		b->s.code = JMP(BPF_JGE);
		gen_not(b);
		return b;

	case '>':
		b = gen_cmp((u_int)idx, BPF_B, (bpf_int32)val);
		b->s.code = JMP(BPF_JGT);
		return b;

	case '|':
		s = new_stmt(BPF_ALU|BPF_OR|BPF_K);
		break;

	case '&':
		s = new_stmt(BPF_ALU|BPF_AND|BPF_K);
		break;
	}
	s->s.k = val;
	b = new_block(JMP(BPF_JEQ));
	b->stmts = s;
	gen_not(b);

	return b;
}

static u_char abroadcast[] = { 0x0 };

struct block *
gen_broadcast(proto)
	int proto;
{
	bpf_u_int32 hostmask;
	struct block *b0, *b1, *b2;
	static u_char ebroadcast[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	switch (proto) {

	case Q_DEFAULT:
	case Q_LINK:
		if (linktype == DLT_ARCNET)
			return gen_ahostop(abroadcast, Q_DST);
		if (linktype == DLT_EN10MB)
			return gen_ehostop(ebroadcast, Q_DST);
		if (linktype == DLT_FDDI)
			return gen_fhostop(ebroadcast, Q_DST);
		if (linktype == DLT_IEEE802)
			return gen_thostop(ebroadcast, Q_DST);
		bpf_error("not a broadcast link");
		break;

	case Q_IP:
		b0 = gen_linktype(ETHERTYPE_IP);
		hostmask = ~netmask;
		b1 = gen_mcmp(off_nl + 16, BPF_W, (bpf_int32)0, hostmask);
		b2 = gen_mcmp(off_nl + 16, BPF_W,
			      (bpf_int32)(~0 & hostmask), hostmask);
		gen_or(b1, b2);
		gen_and(b0, b2);
		return b2;
	}
	bpf_error("only ether/ip broadcast filters supported");
}

struct block *
gen_multicast(proto)
	int proto;
{
	register struct block *b0, *b1;
	register struct slist *s;

	switch (proto) {

	case Q_DEFAULT:
	case Q_LINK:
		if (linktype == DLT_ARCNET)
			/* all ARCnet multicasts use the same address */
			return gen_ahostop(abroadcast, Q_DST);

		if (linktype == DLT_EN10MB) {
			/* ether[0] & 1 != 0 */
			s = new_stmt(BPF_LD|BPF_B|BPF_ABS);
			s->s.k = 0;
			b0 = new_block(JMP(BPF_JSET));
			b0->s.k = 1;
			b0->stmts = s;
			return b0;
		}

		if (linktype == DLT_FDDI) {
			/* XXX TEST THIS: MIGHT NOT PORT PROPERLY XXX */
			/* fddi[1] & 1 != 0 */
			s = new_stmt(BPF_LD|BPF_B|BPF_ABS);
			s->s.k = 1;
			b0 = new_block(JMP(BPF_JSET));
			b0->s.k = 1;
			b0->stmts = s;
			return b0;
		}

		/* TODO - check how token ring handles multicast */
		/* if (linktype == DLT_IEEE802) ... */

		/* Link not known to support multicasts */
		break;

	case Q_IP:
		b0 = gen_linktype(ETHERTYPE_IP);
		b1 = gen_cmp(off_nl + 16, BPF_B, (bpf_int32)224);
		b1->s.code = JMP(BPF_JGE);
		gen_and(b0, b1);
		return b1;

#ifdef INET6
	case Q_IPV6:
		b0 = gen_linktype(ETHERTYPE_IPV6);
		b1 = gen_cmp(off_nl + 24, BPF_B, (bpf_int32)255);
		gen_and(b0, b1);
		return b1;
#endif /* INET6 */
	}
	bpf_error("only IP multicast filters supported on ethernet/FDDI");
}

/*
 * generate command for inbound/outbound.  It's here so we can
 * make it link-type specific.  'dir' = 0 implies "inbound",
 * = 1 implies "outbound".
 */
struct block *
gen_inbound(dir)
	int dir;
{
	register struct block *b0;

	/*
	 * Only some data link types support inbound/outbound qualifiers.
	 */
	switch (linktype) {
	case DLT_SLIP:
	case DLT_PPP:
		b0 = gen_relation(BPF_JEQ,
			  gen_load(Q_LINK, gen_loadi(0), 1),
			  gen_loadi(0),
			  dir);
		break;

	default:
		bpf_error("inbound/outbound not supported on linktype %d\n",
		    linktype);
		b0 = NULL;
		/* NOTREACHED */
	}
	return (b0);
}

struct block *
gen_acode(eaddr, q)
	register const u_char *eaddr;
	struct qual q;
{
	if ((q.addr == Q_HOST || q.addr == Q_DEFAULT) && q.proto == Q_LINK) {
		if (linktype == DLT_ARCNET)
			return gen_ahostop(eaddr, (int)q.dir);
	}
	bpf_error("ARCnet address used in non-arc expression");
	/* NOTREACHED */
}

static struct block *
gen_ahostop(eaddr, dir)
	register const u_char *eaddr;
	register int dir;
{
	register struct block *b0, *b1;

	switch (dir) {
	/* src comes first, different from Ethernet */
	case Q_SRC:
		return gen_bcmp(0, 1, eaddr);

	case Q_DST:
		return gen_bcmp(1, 1, eaddr);

	case Q_AND:
		b0 = gen_ahostop(eaddr, Q_SRC);
		b1 = gen_ahostop(eaddr, Q_DST);
		gen_and(b0, b1);
		return b1;

	case Q_DEFAULT:
	case Q_OR:
		b0 = gen_ahostop(eaddr, Q_SRC);
		b1 = gen_ahostop(eaddr, Q_DST);
		gen_or(b0, b1);
		return b1;
	}
	abort();
	/* NOTREACHED */
}

/*
 * support IEEE 802.1Q VLAN trunk over ethernet
 */
struct block *
gen_vlan(vlan_num)
	int vlan_num;
{
	struct	block	*b0;

	/*
	 * Change the offsets to point to the type and data fields within
	 * the VLAN packet.  This is somewhat of a kludge.
	 */
	if (orig_nl == (u_int)-1) {
		orig_linktype = off_linktype;	/* save original values */
		orig_nl = off_nl;

		switch (linktype) {

		case DLT_EN10MB:
			off_linktype = 16;
			off_nl = 18;
			break;

		default:
			bpf_error("no VLAN support for data link type %d",
				  linktype);
			/*NOTREACHED*/
		}
	}

	/* check for VLAN */
	b0 = gen_cmp(orig_linktype, BPF_H, (bpf_int32)ETHERTYPE_8021Q);

	/* If a specific VLAN is requested, check VLAN id */
	if (vlan_num >= 0) {
		struct block *b1;

		b1 = gen_cmp(orig_nl, BPF_H, (bpf_int32)vlan_num);
		gen_and(b0, b1);
		b0 = b1;
	}

	return (b0);
}
