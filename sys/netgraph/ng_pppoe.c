
/*
 * ng_pppoe.c
 *
 * Copyright (c) 1996-1999 Whistle Communications, Inc.
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
 * Author: Julian Elischer <julian@freebsd.org>
 *
 * $FreeBSD: src/sys/netgraph/ng_pppoe.c,v 1.23.2.18 2003/12/29 14:24:58 yar Exp $
 * $Whistle: ng_pppoe.c,v 1.10 1999/11/01 09:24:52 julian Exp $
 */
#if 0
#define AAA printf("pppoe: %s\n", __FUNCTION__ );
#define BBB printf("-%d-", __LINE__ );
#else
#define AAA
#define BBB
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <net/ethernet.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_pppoe.h>

#define SIGNOFF "session closed"

/*
 * This section contains the netgraph method declarations for the
 * pppoe node. These methods define the netgraph pppoe 'type'.
 */

static ng_constructor_t	ng_pppoe_constructor;
static ng_rcvmsg_t	ng_pppoe_rcvmsg;
static ng_shutdown_t	ng_pppoe_rmnode;
static ng_newhook_t	ng_pppoe_newhook;
static ng_connect_t	ng_pppoe_connect;
static ng_rcvdata_t	ng_pppoe_rcvdata;
static ng_disconnect_t	ng_pppoe_disconnect;

/* Netgraph node type descriptor */
static struct ng_type typestruct = {
	NG_VERSION,
	NG_PPPOE_NODE_TYPE,
	NULL,
	ng_pppoe_constructor,
	ng_pppoe_rcvmsg,
	ng_pppoe_rmnode,
	ng_pppoe_newhook,
	NULL,
	ng_pppoe_connect,
	ng_pppoe_rcvdata,
	ng_pppoe_rcvdata,
	ng_pppoe_disconnect,
	NULL
};
NETGRAPH_INIT(pppoe, &typestruct);

/*
 * States for the session state machine.
 * These have no meaning if there is no hook attached yet.
 */
enum state {
    PPPOE_SNONE=0,	/* [both] Initial state */
    PPPOE_LISTENING,	/* [Daemon] Listening for discover initiation pkt */
    PPPOE_SINIT,	/* [Client] Sent discovery initiation */
    PPPOE_PRIMED,	/* [Server] Awaiting PADI from daemon */
    PPPOE_SOFFER,	/* [Server] Sent offer message  (got PADI)*/
    PPPOE_SREQ,		/* [Client] Sent a Request */
    PPPOE_NEWCONNECTED,	/* [Server] Connection established, No data received */
    PPPOE_CONNECTED,	/* [Both] Connection established, Data received */
    PPPOE_DEAD		/* [Both] */
};

#define NUMTAGS 20 /* number of tags we are set up to work with */

/*
 * Information we store for each hook on each node for negotiating the 
 * session. The mbuf and cluster are freed once negotiation has completed.
 * The whole negotiation block is then discarded.
 */

struct sess_neg {
	struct mbuf 		*m; /* holds cluster with last sent packet */
	union	packet		*pkt; /* points within the above cluster */
	struct callout_handle	timeout_handle;   /* see timeout(9) */
	u_int			timeout; /* 0,1,2,4,8,16 etc. seconds */
	u_int			numtags;
	const struct pppoe_tag	*tags[NUMTAGS];
	u_int			service_len;
	u_int			ac_name_len;

	struct datatag		service;
	struct datatag		ac_name;
};
typedef struct sess_neg *negp;

/*
 * Session information that is needed after connection.
 */
struct sess_con {
	hook_p  		hook;
	u_int16_t		Session_ID;
	enum state		state;
	char			creator[NG_NODELEN + 1]; /* who to notify */
	struct pppoe_full_hdr	pkt_hdr;	/* used when connected */
	negp			neg;		/* used when negotiating */
	/*struct sess_con	*hash_next;*/	/* not yet used */
};
typedef struct sess_con *sessp;

/*
 * Information we store for each node
 */
struct PPPOE {
	node_p		node;		/* back pointer to node */
	hook_p  	ethernet_hook;
	hook_p  	debug_hook;
	u_int   	packets_in;	/* packets in from ethernet */
	u_int   	packets_out;	/* packets out towards ethernet */
	u_int32_t	flags;
	/*struct sess_con *buckets[HASH_SIZE];*/	/* not yet used */
};
typedef struct PPPOE *priv_p;

struct ether_header eh_prototype =
	{{0xff,0xff,0xff,0xff,0xff,0xff},
	 {0x00,0x00,0x00,0x00,0x00,0x00},
	 ETHERTYPE_PPPOE_DISC};

#define PPPOE_KEEPSTANDARD	-1	/* never switch to nonstandard mode */
#define PPPOE_STANDARD		0	/* try standard mode (dangerous!) */
#define PPPOE_NONSTANDARD	1	/* just be in nonstandard mode */
static int pppoe_mode = PPPOE_KEEPSTANDARD;

static int
ngpppoe_set_ethertype(SYSCTL_HANDLER_ARGS)
{
	int error;
	int val;

	val = pppoe_mode;
	error = sysctl_handle_int(oidp, &val, sizeof(int), req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	switch (val) {
	case PPPOE_NONSTANDARD:
		pppoe_mode = PPPOE_NONSTANDARD;
		eh_prototype.ether_type = ETHERTYPE_PPPOE_STUPID_DISC;
		break;
	case PPPOE_STANDARD:
		pppoe_mode = PPPOE_STANDARD;
		eh_prototype.ether_type = ETHERTYPE_PPPOE_DISC;
		break;
	case PPPOE_KEEPSTANDARD:
		pppoe_mode = PPPOE_KEEPSTANDARD;
		eh_prototype.ether_type = ETHERTYPE_PPPOE_DISC;
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

SYSCTL_PROC(_net_graph, OID_AUTO, nonstandard_pppoe, CTLTYPE_INT | CTLFLAG_RW,
    0, sizeof(int), ngpppoe_set_ethertype, "I", "nonstandard ethertype");

union uniq {
	char bytes[sizeof(void *)];
	void * pointer;
	};

#define	LEAVE(x) do { error = x; goto quit; } while(0)
static void	pppoe_start(sessp sp);
static void	sendpacket(sessp sp);
static void	pppoe_ticker(void *arg);
static const	struct pppoe_tag *scan_tags(sessp sp,
			const struct pppoe_hdr* ph);
static	int	pppoe_send_event(sessp sp, enum cmd cmdid);

/*************************************************************************
 * Some basic utilities  from the Linux version with author's permission.*
 * Author:	Michal Ostrowski <mostrows@styx.uwaterloo.ca>		 *
 ************************************************************************/

/*
 * Generate a new session id
 * XXX find out the FreeBSD locking scheme.
 */
static u_int16_t
get_new_sid(node_p node)
{
	static int pppoe_sid = 10;
	sessp sp;
	hook_p	hook;
	u_int16_t val; 
	priv_p privp = node->private;

AAA
restart:
	val = pppoe_sid++;
	/*
	 * Spec says 0xFFFF is reserved.
	 * Also don't use 0x0000
	 */
	if (val == 0xffff) {
		pppoe_sid = 20;
		goto restart;
	}

	/* Check it isn't already in use */
	LIST_FOREACH(hook, &node->hooks, hooks) {
		/* don't check special hooks */
		if ((hook->private == &privp->debug_hook)
		||  (hook->private == &privp->ethernet_hook)) 
			continue;
		sp = hook->private;
		if (sp->Session_ID == val)
			goto restart;
	}

	return val;
}


/*
 * Return the location where the next tag can be put 
 */
static __inline const struct pppoe_tag*
next_tag(const struct pppoe_hdr* ph)
{
	return (const struct pppoe_tag*)(((const char*)&ph->tag[0])
	    + ntohs(ph->length));
}

/*
 * Look for a tag of a specific type
 * Don't trust any length the other end says.
 * but assume we already sanity checked ph->length.
 */
static const struct pppoe_tag*
get_tag(const struct pppoe_hdr* ph, u_int16_t idx)
{
	const char *const end = (const char *)next_tag(ph);
	const char *ptn;
	const struct pppoe_tag *pt = &ph->tag[0];
	/*
	 * Keep processing tags while a tag header will still fit.
	 */
AAA
	while((const char*)(pt + 1) <= end) {
	    /*
	     * If the tag data would go past the end of the packet, abort.
	     */
	    ptn = (((const char *)(pt + 1)) + ntohs(pt->tag_len));
	    if(ptn > end)
		return NULL;

	    if(pt->tag_type == idx)
		return pt;

	    pt = (const struct pppoe_tag*)ptn;
	}
	return NULL;
}

/**************************************************************************
 * inlines to initialise or add tags to a session's tag list,
 **************************************************************************/
/*
 * Initialise the session's tag list
 */
static void
init_tags(sessp sp)
{
AAA
	if(sp->neg == NULL) {
		printf("pppoe: asked to init NULL neg pointer\n");
		return;
	}
	sp->neg->numtags = 0;
}

static void
insert_tag(sessp sp, const struct pppoe_tag *tp)
{
	int	i;
	negp neg;

AAA
	if((neg = sp->neg) == NULL) {
		printf("pppoe: asked to use NULL neg pointer\n");
		return;
	}
	if ((i = neg->numtags++) < NUMTAGS) {
		neg->tags[i] = tp;
	} else {
		printf("pppoe: asked to add too many tags to packet\n");
		neg->numtags--;
	}
}

/*
 * Make up a packet, using the tags filled out for the session.
 *
 * Assume that the actual pppoe header and ethernet header 
 * are filled out externally to this routine.
 * Also assume that neg->wh points to the correct 
 * location at the front of the buffer space.
 */
static void
make_packet(sessp sp) {
	struct pppoe_full_hdr *wh = &sp->neg->pkt->pkt_header;
	const struct pppoe_tag **tag;
	char *dp;
	int count;
	int tlen;
	u_int16_t length = 0;

AAA
	if ((sp->neg == NULL) || (sp->neg->m == NULL)) {
		printf("pppoe: make_packet called from wrong state\n");
	}
	dp = (char *)wh->ph.tag;
	for (count = 0, tag = sp->neg->tags;
	    ((count < sp->neg->numtags) && (count < NUMTAGS)); 
	    tag++, count++) {
		tlen = ntohs((*tag)->tag_len) + sizeof(**tag);
		if ((length + tlen) > (ETHER_MAX_LEN - 4 - sizeof(*wh))) {
			printf("pppoe: tags too long\n");
			sp->neg->numtags = count;
			break;	/* XXX chop off what's too long */
		}
		bcopy(*tag, (char *)dp, tlen);
		length += tlen;
		dp += tlen;
	}
 	wh->ph.length = htons(length);
	sp->neg->m->m_len = length + sizeof(*wh);
	sp->neg->m->m_pkthdr.len = length + sizeof(*wh);
}

/**************************************************************************
 * Routine to match a service offered					  *
 **************************************************************************/
/* 
 * Find a hook that has a service string that matches that
 * we are seeking. for now use a simple string.
 * In the future we may need something like regexp().
 * for testing allow a null string to match 1st found and a null service
 * to match all requests. Also make '*' do the same.
 */

#define NG_MATCH_EXACT	1
#define NG_MATCH_ANY	2

static hook_p
pppoe_match_svc(node_p node, const char *svc_name, int svc_len, int match)
{
	sessp	sp	= NULL;
	negp	neg	= NULL;
	priv_p	privp	= node->private;
	hook_p	allhook	= NULL;
	hook_p	hook;

AAA
	LIST_FOREACH(hook, &node->hooks, hooks) {

		/* skip any hook that is debug or ethernet */
		if ((hook->private == &privp->debug_hook)
		||  (hook->private == &privp->ethernet_hook))
			continue;
		sp = hook->private;

		/* Skip any sessions which are not in LISTEN mode. */
		if ( sp->state != PPPOE_LISTENING)
			continue;

		neg = sp->neg;

		/* Special case for a blank or "*" service name (wildcard) */
		if (match == NG_MATCH_ANY && neg->service_len == 1 &&
		    neg->service.data[0] == '*') {
			allhook = hook;
			continue;
		}

		/* If the lengths don't match, that aint it. */
		if (neg->service_len != svc_len)
			continue;

		/* An exact match? */
		if (svc_len == 0)
			break;

		if (strncmp(svc_name, neg->service.data, svc_len) == 0)
			break;
	}
	return (hook ? hook : allhook);
}
/**************************************************************************
 * Routine to find a particular session that matches an incoming packet	  *
 **************************************************************************/
static hook_p
pppoe_findsession(node_p node, const struct pppoe_full_hdr *wh)
{
	sessp	sp = NULL;
	hook_p hook = NULL;
	priv_p	privp = node->private;
	u_int16_t	session = ntohs(wh->ph.sid);

	/*
	 * find matching peer/session combination.
	 */
AAA
	LIST_FOREACH(hook, &node->hooks, hooks) {
		/* don't check special hooks */
		if ((hook->private == &privp->debug_hook)
		||  (hook->private == &privp->ethernet_hook)) {
			continue;
		}
		sp = hook->private;
		if ( ( (sp->state == PPPOE_CONNECTED)
		    || (sp->state == PPPOE_NEWCONNECTED) )
		&& (sp->Session_ID == session)
		&& (bcmp(sp->pkt_hdr.eh.ether_dhost,
		    wh->eh.ether_shost,
		    ETHER_ADDR_LEN)) == 0) {
			break;
		}
	}
	return (hook);
}

static hook_p
pppoe_finduniq(node_p node, const struct pppoe_tag *tag)
{
	hook_p hook = NULL;
	priv_p	privp = node->private;
	union uniq		uniq;

AAA
	bcopy(tag->tag_data, uniq.bytes, sizeof(void *));
	/* cycle through all known hooks */
	LIST_FOREACH(hook, &node->hooks, hooks) {
		/* don't check special hooks */
		if ((hook->private == &privp->debug_hook)
		||  (hook->private == &privp->ethernet_hook)) 
			continue;
		if (uniq.pointer == hook->private)
			break;
	}
	return (hook);
}

/**************************************************************************
 * start of Netgraph entrypoints					  *
 **************************************************************************/

/*
 * Allocate the private data structure and the generic node
 * and link them together.
 *
 * ng_make_node_common() returns with a generic node struct
 * with a single reference for us.. we transfer it to the
 * private structure.. when we free the private struct we must
 * unref the node so it gets freed too.
 */
static int
ng_pppoe_constructor(node_p *nodep)
{
	priv_p privdata;
	int error;

AAA
	/* Initialize private descriptor */
	MALLOC(privdata, priv_p, sizeof(*privdata), M_NETGRAPH, M_NOWAIT);
	if (privdata == NULL)
		return (ENOMEM);
	bzero(privdata, sizeof(*privdata));

	/* Call the 'generic' (ie, superclass) node constructor */
	if ((error = ng_make_node_common(&typestruct, nodep))) {
		FREE(privdata, M_NETGRAPH);
		return (error);
	}

	/* Link structs together; this counts as our one reference to *nodep */
	(*nodep)->private = privdata;
	privdata->node = *nodep;
	return (0);
}

/*
 * Give our ok for a hook to be added...
 * point the hook's private info to the hook structure.
 *
 * The following hook names are special:
 *  Ethernet:  the hook that should be connected to a NIC.
 *  debug:	copies of data sent out here  (when I write the code).
 * All other hook names need only be unique. (the framework checks this).
 */
static int
ng_pppoe_newhook(node_p node, hook_p hook, const char *name)
{
	const priv_p privp = node->private;
	sessp sp;

AAA
	if (strcmp(name, NG_PPPOE_HOOK_ETHERNET) == 0) {
		privp->ethernet_hook = hook;
		hook->private = &privp->ethernet_hook;
	} else if (strcmp(name, NG_PPPOE_HOOK_DEBUG) == 0) {
		privp->debug_hook = hook;
		hook->private = &privp->debug_hook;
	} else {
		/*
		 * Any other unique name is OK.
		 * The infrastructure has already checked that it's unique,
		 * so just allocate it and hook it in.
		 */
		MALLOC(sp, sessp, sizeof(*sp), M_NETGRAPH, M_NOWAIT);
		if (sp == NULL) {
				return (ENOMEM);
		}
		bzero(sp, sizeof(*sp));

		hook->private = sp;
		sp->hook = hook;
	}
	return(0);
}

/*
 * Get a netgraph control message.
 * Check it is one we understand. If needed, send a response.
 * We sometimes save the address for an async action later.
 * Always free the message.
 */
static int
ng_pppoe_rcvmsg(node_p node,
	   struct ng_mesg *msg, const char *retaddr, struct ng_mesg **rptr)
{
	priv_p privp = node->private;
	struct ngpppoe_init_data *ourmsg = NULL;
	struct ng_mesg *resp = NULL;
	int error = 0;
	hook_p hook = NULL;
	sessp sp = NULL;
	negp neg = NULL;

AAA
	/* Deal with message according to cookie and command */
	switch (msg->header.typecookie) {
	case NGM_PPPOE_COOKIE: 
		switch (msg->header.cmd) {
		case NGM_PPPOE_CONNECT:
		case NGM_PPPOE_LISTEN: 
		case NGM_PPPOE_OFFER: 
		case NGM_PPPOE_SERVICE: 
			ourmsg = (struct ngpppoe_init_data *)msg->data;
			if (( sizeof(*ourmsg) > msg->header.arglen)
			|| ((sizeof(*ourmsg) + ourmsg->data_len)
			    > msg->header.arglen)) {
				printf("pppoe_rcvmsg: bad arg size");
				LEAVE(EMSGSIZE);
			}
			if (ourmsg->data_len > PPPOE_SERVICE_NAME_SIZE) {
				printf("pppoe: init data too long (%d)\n",
							ourmsg->data_len);
				LEAVE(EMSGSIZE);
			}
			/* make sure strcmp will terminate safely */
			ourmsg->hook[sizeof(ourmsg->hook) - 1] = '\0';

			/* cycle through all known hooks */
			LIST_FOREACH(hook, &node->hooks, hooks) {
				if (hook->name
				&& strcmp(hook->name, ourmsg->hook) == 0)
					break;
			}
			if (hook == NULL) {
				LEAVE(ENOENT);
			}
			if ((hook->private == &privp->debug_hook)
			||  (hook->private == &privp->ethernet_hook)) {
				LEAVE(EINVAL);
			}
			sp = hook->private;

			if (msg->header.cmd == NGM_PPPOE_LISTEN) {
				/*
				 * Ensure we aren't already listening for this
				 * service.
				 */
				if (pppoe_match_svc(node, ourmsg->data,
				    ourmsg->data_len, NG_MATCH_EXACT) != NULL) {
					LEAVE(EEXIST);
				}
			}

			/*
			 * PPPOE_SERVICE advertisments are set up
			 * on sessions that are in PRIMED state.
			 */
			if (msg->header.cmd == NGM_PPPOE_SERVICE) {
				break;
			}
			if (sp->state |= PPPOE_SNONE) {
				printf("pppoe: Session already active\n");
				LEAVE(EISCONN);
			}

			/*
			 * set up prototype header
			 */
			MALLOC(neg, negp, sizeof(*neg), M_NETGRAPH, M_NOWAIT);

			if (neg == NULL) {
				printf("pppoe: Session out of memory\n");
				LEAVE(ENOMEM);
			}
			bzero(neg, sizeof(*neg));
			MGETHDR(neg->m, M_DONTWAIT, MT_DATA);
			if(neg->m == NULL) {
				printf("pppoe: Session out of mbufs\n");
				FREE(neg, M_NETGRAPH);
				LEAVE(ENOBUFS);
			}
			neg->m->m_pkthdr.rcvif = NULL;
			MCLGET(neg->m, M_DONTWAIT);
			if ((neg->m->m_flags & M_EXT) == 0) {
				printf("pppoe: Session out of mcls\n");
				m_freem(neg->m);
				FREE(neg, M_NETGRAPH);
				LEAVE(ENOBUFS);
			}
			sp->neg = neg;
			callout_handle_init( &neg->timeout_handle);
			neg->m->m_len = sizeof(struct pppoe_full_hdr);
			neg->pkt = mtod(neg->m, union packet*);
			neg->pkt->pkt_header.eh = eh_prototype;
			neg->pkt->pkt_header.ph.ver = 0x1;
			neg->pkt->pkt_header.ph.type = 0x1;
			neg->pkt->pkt_header.ph.sid = 0x0000;
			neg->timeout = 0;

			strncpy(sp->creator, retaddr, NG_NODELEN);
			sp->creator[NG_NODELEN] = '\0';
		}
		switch (msg->header.cmd) {
		case NGM_PPPOE_GET_STATUS:
		    {
			struct ngpppoestat *stats;

			NG_MKRESPONSE(resp, msg, sizeof(*stats), M_NOWAIT);
			if (!resp) {
				LEAVE(ENOMEM);
			}
			stats = (struct ngpppoestat *) resp->data;
			stats->packets_in = privp->packets_in;
			stats->packets_out = privp->packets_out;
			break;
		    }
		case NGM_PPPOE_CONNECT:
			/*
			 * Check the hook exists and is Uninitialised.
			 * Send a PADI request, and start the timeout logic.
			 * Store the originator of this message so we can send
			 * a success of fail message to them later.
			 * Move the session to SINIT
			 * Set up the session to the correct state and
			 * start it.
			 */
			neg->service.hdr.tag_type = PTT_SRV_NAME;
			neg->service.hdr.tag_len =
					htons((u_int16_t)ourmsg->data_len);
			if (ourmsg->data_len) {
				bcopy(ourmsg->data,
					neg->service.data, ourmsg->data_len);
			}
			neg->service_len = ourmsg->data_len;
			pppoe_start(sp);
			break;
		case NGM_PPPOE_LISTEN:
			/*
			 * Check the hook exists and is Uninitialised.
			 * Install the service matching string.
			 * Store the originator of this message so we can send
			 * a success of fail message to them later.
			 * Move the hook to 'LISTENING'
			 */
			neg->service.hdr.tag_type = PTT_SRV_NAME;
			neg->service.hdr.tag_len =
					htons((u_int16_t)ourmsg->data_len);

			if (ourmsg->data_len) {
				bcopy(ourmsg->data,
					neg->service.data, ourmsg->data_len);
			}
			neg->service_len = ourmsg->data_len;
			neg->pkt->pkt_header.ph.code = PADT_CODE;
			/*
			 * wait for PADI packet coming from ethernet
			 */
			sp->state = PPPOE_LISTENING;
			break;
		case NGM_PPPOE_OFFER:
			/*
			 * Check the hook exists and is Uninitialised.
			 * Store the originator of this message so we can send
			 * a success of fail message to them later.
			 * Store the AC-Name given and go to PRIMED.
			 */
			neg->ac_name.hdr.tag_type = PTT_AC_NAME;
			neg->ac_name.hdr.tag_len =
					htons((u_int16_t)ourmsg->data_len);
			if (ourmsg->data_len) {
				bcopy(ourmsg->data,
					neg->ac_name.data, ourmsg->data_len);
			}
			neg->ac_name_len = ourmsg->data_len;
			neg->pkt->pkt_header.ph.code = PADO_CODE;
			/*
			 * Wait for PADI packet coming from hook
			 */
			sp->state = PPPOE_PRIMED;
			break;
		case NGM_PPPOE_SERVICE: 
			/* 
			 * Check the session is primed.
			 * for now just allow ONE service to be advertised.
			 * If you do it twice you just overwrite.
			 */
			if (sp->state != PPPOE_PRIMED) {
				printf("pppoe: Session not primed\n");
				LEAVE(EISCONN);
			}
			neg = sp->neg;
			neg->service.hdr.tag_type = PTT_SRV_NAME;
			neg->service.hdr.tag_len =
			    htons((u_int16_t)ourmsg->data_len);

			if (ourmsg->data_len)
				bcopy(ourmsg->data, neg->service.data,
				    ourmsg->data_len);
			neg->service_len = ourmsg->data_len;
			break;
		default:
			LEAVE(EINVAL);
		}
		break;
	default:
		LEAVE(EINVAL);
	}

	/* Take care of synchronous response, if any */
	if (rptr)
		*rptr = resp;
	else if (resp)
		FREE(resp, M_NETGRAPH);

	/* Free the message and return */
quit:
	FREE(msg, M_NETGRAPH);
	return(error);
}

/*
 * Start a client into the first state. A separate function because
 * it can be needed if the negotiation times out.
 */
static void
pppoe_start(sessp sp)
{
	struct {
		struct pppoe_tag hdr;
		union	uniq	data;
	} __attribute ((packed)) uniqtag;

	/* 
	 * kick the state machine into starting up
	 */
AAA
	sp->state = PPPOE_SINIT;
	/* reset the packet header to broadcast */
	sp->neg->pkt->pkt_header.eh = eh_prototype;
	sp->neg->pkt->pkt_header.ph.code = PADI_CODE;
	uniqtag.hdr.tag_type = PTT_HOST_UNIQ;
	uniqtag.hdr.tag_len = htons((u_int16_t)sizeof(uniqtag.data));
	uniqtag.data.pointer = sp;
	init_tags(sp);
	insert_tag(sp, &uniqtag.hdr);
	insert_tag(sp, &sp->neg->service.hdr);
	make_packet(sp);
	sendpacket(sp);
}

static int
send_acname(sessp sp, const struct pppoe_tag *tag)
{
	int error, tlen;
	struct ng_mesg *msg;
	struct ngpppoe_sts *sts;

	NG_MKMESSAGE(msg, NGM_PPPOE_COOKIE, NGM_PPPOE_ACNAME,
	    sizeof(struct ngpppoe_sts), M_NOWAIT);
	if (msg == NULL)
		return (ENOMEM);

	sts = (struct ngpppoe_sts *)msg->data;
	tlen = min(NG_HOOKLEN, ntohs(tag->tag_len));
	strncpy(sts->hook, tag->tag_data, tlen);
	sts->hook[tlen] = '\0';
	error = ng_send_msg(sp->hook->node, msg, sp->creator, NULL);

	return (error);
}

static int
send_sessionid(sessp sp)
{
	int error;
	struct ng_mesg *msg;

	NG_MKMESSAGE(msg, NGM_PPPOE_COOKIE, NGM_PPPOE_SESSIONID,
	    sizeof(u_int16_t), M_NOWAIT);
	if (msg == NULL)
		return (ENOMEM);

	*(u_int16_t *)msg->data = sp->Session_ID;
	error = ng_send_msg(sp->hook->node, msg, sp->creator, NULL);

	return (error);
}

/*
 * Receive data, and do something with it.
 * The caller will never free m or meta, so
 * if we use up this data or abort we must free BOTH of these.
 */
static int
ng_pppoe_rcvdata(hook_p hook, struct mbuf *m, meta_p meta)
{
	node_p			node = hook->node;
	const priv_p		privp = node->private;
	sessp			sp = hook->private;
	const struct pppoe_full_hdr *wh;
	const struct pppoe_hdr	*ph;
	int			error = 0;
	u_int16_t		session;
	u_int16_t		length;
	u_int8_t		code;
	const struct pppoe_tag	*utag = NULL, *tag = NULL;
	hook_p 			sendhook;
	struct {
		struct pppoe_tag hdr;
		union	uniq	data;
	} __attribute ((packed)) uniqtag;
	negp			neg = NULL;

AAA
	if (hook->private == &privp->debug_hook) {
		/*
		 * Data from the debug hook gets sent without modification
		 * straight to the ethernet. 
		 */
		NG_SEND_DATA( error, privp->ethernet_hook, m, meta);
	 	privp->packets_out++;
	} else if (hook->private == &privp->ethernet_hook) {
		/*
		 * Incoming data. 
		 * Dig out various fields from the packet.
		 * use them to decide where to send it.
		 */
		
 		privp->packets_in++;
		if( m->m_len < sizeof(*wh)) {
			m = m_pullup(m, sizeof(*wh)); /* Checks length */
			if (m == NULL) {
				printf("couldn't m_pullup\n");
				LEAVE(ENOBUFS);
			}
		}
		wh = mtod(m, struct pppoe_full_hdr *);
		ph = &wh->ph;
		session = ntohs(wh->ph.sid);
		length = ntohs(wh->ph.length);
		code = wh->ph.code; 
		switch(wh->eh.ether_type) {
		case	ETHERTYPE_PPPOE_STUPID_DISC:
			if (pppoe_mode == PPPOE_STANDARD) {
				pppoe_mode = PPPOE_NONSTANDARD;
				eh_prototype.ether_type =
				    ETHERTYPE_PPPOE_STUPID_DISC;
				log(LOG_NOTICE,
				    "Switched to nonstandard PPPoE mode due to "
				    "packet from %*D\n",
				    ETHER_ADDR_LEN,
				    wh->eh.ether_shost, ":");
			} else if (pppoe_mode == PPPOE_KEEPSTANDARD)
				log(LOG_NOTICE,
				    "Ignored nonstandard PPPoE packet "
				    "from %*D\n",
				    ETHER_ADDR_LEN,
				    wh->eh.ether_shost, ":");
			/* fall through */
		case	ETHERTYPE_PPPOE_DISC:
			/*
			 * We need to try to make sure that the tag area
			 * is contiguous, or we could wander off the end
			 * of a buffer and make a mess. 
			 * (Linux wouldn't have this problem).
			 */
/*XXX fix this mess */
			
			if (m->m_pkthdr.len <= MHLEN) {
				if( m->m_len < m->m_pkthdr.len) {
					m = m_pullup(m, m->m_pkthdr.len);
					if (m == NULL) {
						printf("couldn't m_pullup\n");
						LEAVE(ENOBUFS);
					}
				}
			}
			if (m->m_len != m->m_pkthdr.len) {
				/*
				 * It's not all in one piece.
				 * We need to do extra work.
				 */
				printf("packet fragmented\n");
				LEAVE(EMSGSIZE);
			}

			switch(code) {
			case	PADI_CODE:
				/*
				 * We are a server:
				 * Look for a hook with the required service
				 * and send the ENTIRE packet up there.
				 * It should come back to a new hook in 
				 * PRIMED state. Look there for further
				 * processing.
				 */
				tag = get_tag(ph, PTT_SRV_NAME);
				if (tag == NULL) {
					printf("no service tag\n");
					LEAVE(ENETUNREACH);
				}
				sendhook = pppoe_match_svc(hook->node,
			    		tag->tag_data, ntohs(tag->tag_len),
					NG_MATCH_ANY);
				if (sendhook) {
					NG_SEND_DATA(error, sendhook, m, meta);
				} else {
					LEAVE(ENETUNREACH);
				}
				break;
			case	PADO_CODE:
				/*
				 * We are a client:
				 * Use the host_uniq tag to find the 
				 * hook this is in response to.
				 * Received #2, now send #3
				 * For now simply accept the first we receive.
				 */
				utag = get_tag(ph, PTT_HOST_UNIQ);
				if ((utag == NULL)
				|| (ntohs(utag->tag_len) != sizeof(sp))) {
					printf("no host unique field\n");
					LEAVE(ENETUNREACH);
				}

				sendhook = pppoe_finduniq(node, utag);
				if (sendhook == NULL) {
					printf("no matching session\n");
					LEAVE(ENETUNREACH);
				}

				/*
				 * Check the session is in the right state.
				 * It needs to be in PPPOE_SINIT.
				 */
				sp = sendhook->private;
				if (sp->state != PPPOE_SINIT) {
					printf("session in wrong state\n");
					LEAVE(ENETUNREACH);
				}
				neg = sp->neg;
				untimeout(pppoe_ticker, sendhook,
				    neg->timeout_handle);

				/*
				 * This is the first time we hear
				 * from the server, so note it's
				 * unicast address, replacing the
				 * broadcast address .
				 */
				bcopy(wh->eh.ether_shost,
					neg->pkt->pkt_header.eh.ether_dhost,
					ETHER_ADDR_LEN);
				neg->timeout = 0;
				neg->pkt->pkt_header.ph.code = PADR_CODE;
				init_tags(sp);
				insert_tag(sp, utag);      /* Host Unique */
				if ((tag = get_tag(ph, PTT_AC_COOKIE)))
					insert_tag(sp, tag); /* return cookie */
				if ((tag = get_tag(ph, PTT_AC_NAME))) {	
					insert_tag(sp, tag); /* return it */
					send_acname(sp, tag);
				}
				insert_tag(sp, &neg->service.hdr); /* Service */
				scan_tags(sp, ph);
				make_packet(sp);
				sp->state = PPPOE_SREQ;
				sendpacket(sp);
				break;
			case	PADR_CODE:

				/*
				 * We are a server:
				 * Use the ac_cookie tag to find the 
				 * hook this is in response to.
				 */
				utag = get_tag(ph, PTT_AC_COOKIE);
				if ((utag == NULL)
				|| (ntohs(utag->tag_len) != sizeof(sp))) {
					LEAVE(ENETUNREACH);
				}

				sendhook = pppoe_finduniq(node, utag);
				if (sendhook == NULL) {
					LEAVE(ENETUNREACH);
				}

				/*
				 * Check the session is in the right state.
				 * It needs to be in PPPOE_SOFFER
				 * or PPPOE_NEWCONNECTED. If the latter,
				 * then this is a retry by the client.
				 * so be nice, and resend.
				 */
				sp = sendhook->private;
				if (sp->state == PPPOE_NEWCONNECTED) {
					/*
					 * Whoa! drop back to resend that 
					 * PADS packet.
					 * We should still have a copy of it.
					 */
					sp->state = PPPOE_SOFFER;
				}
				if (sp->state != PPPOE_SOFFER) {
					LEAVE (ENETUNREACH);
					break;
				}
				neg = sp->neg;
				untimeout(pppoe_ticker, sendhook,
				    neg->timeout_handle);
				neg->pkt->pkt_header.ph.code = PADS_CODE;
				if (sp->Session_ID == 0)
					neg->pkt->pkt_header.ph.sid =
					    htons(sp->Session_ID
						= get_new_sid(node));
				send_sessionid(sp);
				neg->timeout = 0;
				/*
				 * start working out the tags to respond with.
				 */
				init_tags(sp);
				insert_tag(sp, &neg->ac_name.hdr); /* AC_NAME */
				if ((tag = get_tag(ph, PTT_SRV_NAME)))
					insert_tag(sp, tag);/* return service */
				if ((tag = get_tag(ph, PTT_HOST_UNIQ)))
					insert_tag(sp, tag); /* return it */
				insert_tag(sp, utag);	/* ac_cookie */
				scan_tags(sp, ph);
				make_packet(sp);
				sp->state = PPPOE_NEWCONNECTED;
				sendpacket(sp);
				/*
				 * Having sent the last Negotiation header,
				 * Set up the stored packet header to 
				 * be correct for the actual session.
				 * But keep the negotialtion stuff
				 * around in case we need to resend this last 
				 * packet. We'll discard it when we move
				 * from NEWCONNECTED to CONNECTED
				 */
				sp->pkt_hdr = neg->pkt->pkt_header;
				if (pppoe_mode == PPPOE_NONSTANDARD)
					sp->pkt_hdr.eh.ether_type
						= ETHERTYPE_PPPOE_STUPID_SESS;
				else
					sp->pkt_hdr.eh.ether_type
						= ETHERTYPE_PPPOE_SESS;
				sp->pkt_hdr.ph.code = 0;
				pppoe_send_event(sp, NGM_PPPOE_SUCCESS);
				break;
			case	PADS_CODE:
				/*
				 * We are a client:
				 * Use the host_uniq tag to find the 
				 * hook this is in response to.
				 * take the session ID and store it away.
				 * Also make sure the pre-made header is
				 * correct and set us into Session mode.
				 */
				utag = get_tag(ph, PTT_HOST_UNIQ);
				if ((utag == NULL)
				|| (ntohs(utag->tag_len) != sizeof(sp))) {
					LEAVE (ENETUNREACH);
					break;
				}
				sendhook = pppoe_finduniq(node, utag);
				if (sendhook == NULL) {
					LEAVE(ENETUNREACH);
				}

				/*
				 * Check the session is in the right state.
				 * It needs to be in PPPOE_SREQ.
				 */
				sp = sendhook->private;
				if (sp->state != PPPOE_SREQ) {
					LEAVE(ENETUNREACH);
				}
				neg = sp->neg;
				untimeout(pppoe_ticker, sendhook,
				    neg->timeout_handle);
				neg->pkt->pkt_header.ph.sid = wh->ph.sid;
				sp->Session_ID = ntohs(wh->ph.sid);
				send_sessionid(sp);
				neg->timeout = 0;
				sp->state = PPPOE_CONNECTED;
				/*
				 * Now we have gone to Connected mode, 
				 * Free all resources needed for 
				 * negotiation.
				 * Keep a copy of the header we will be using.
				 */
				sp->pkt_hdr = neg->pkt->pkt_header;
				if (pppoe_mode == PPPOE_NONSTANDARD)
					sp->pkt_hdr.eh.ether_type
						= ETHERTYPE_PPPOE_STUPID_SESS;
				else
					sp->pkt_hdr.eh.ether_type
						= ETHERTYPE_PPPOE_SESS;
				sp->pkt_hdr.ph.code = 0;
				m_freem(neg->m);
				FREE(sp->neg, M_NETGRAPH);
				sp->neg = NULL;
				pppoe_send_event(sp, NGM_PPPOE_SUCCESS);
				break;
			case	PADT_CODE:
				/*
				 * Send a 'close' message to the controlling
				 * process (the one that set us up);
				 * And then tear everything down.
				 *
				 * Find matching peer/session combination.
				 */
				sendhook = pppoe_findsession(node, wh);
				NG_FREE_DATA(m, meta); /* no longer needed */
				if (sendhook == NULL) {
					LEAVE(ENETUNREACH);
				}
				/* send message to creator */
				/* close hook */
				if (sendhook) {
					ng_destroy_hook(sendhook);
				}
				break;
			default:
				LEAVE(EPFNOSUPPORT);
			}
			break;
		case	ETHERTYPE_PPPOE_STUPID_SESS:
		case	ETHERTYPE_PPPOE_SESS:
			/*
			 * find matching peer/session combination.
			 */
			sendhook = pppoe_findsession(node, wh);
			if (sendhook == NULL) {
				LEAVE (ENETUNREACH);
				break;
			}
			sp = sendhook->private;
			m_adj(m, sizeof(*wh));
			if (m->m_pkthdr.len < length) {
				/* Packet too short, dump it */
				LEAVE(EMSGSIZE);
			}

			/* Also need to trim excess at the end */
			if (m->m_pkthdr.len > length) {
				m_adj(m, -((int)(m->m_pkthdr.len - length)));
			}
			if ( sp->state != PPPOE_CONNECTED) {
				if (sp->state == PPPOE_NEWCONNECTED) {
					sp->state = PPPOE_CONNECTED;
					/*
					 * Now we have gone to Connected mode, 
					 * Free all resources needed for 
					 * negotiation. Be paranoid about
					 * whether there may be a timeout.
					 */
					m_freem(sp->neg->m);
					untimeout(pppoe_ticker, sendhook,
				    		sp->neg->timeout_handle);
					FREE(sp->neg, M_NETGRAPH);
					sp->neg = NULL;
				} else {
					LEAVE (ENETUNREACH);
					break;
				}
			}
			NG_SEND_DATA( error, sendhook, m, meta);
			break;
		default:
			LEAVE(EPFNOSUPPORT);
		}
	} else {
		/*
		 * 	Not ethernet or debug hook..
		 *
		 * The packet has come in on a normal hook.
		 * We need to find out what kind of hook,
		 * So we can decide how to handle it.
		 * Check the hook's state.
		 */
		sp = hook->private;
		switch (sp->state) {
		case	PPPOE_NEWCONNECTED:
		case	PPPOE_CONNECTED: {
			static const u_char addrctrl[] = { 0xff, 0x03 };
			struct pppoe_full_hdr *wh;

			/*
			 * Remove PPP address and control fields, if any.
			 * For example, ng_ppp(4) always sends LCP packets
			 * with address and control fields as required by
			 * generic PPP. PPPoE is an exception to the rule.
			 */
			if (m->m_pkthdr.len >= 2) {
				if (m->m_len < 2 && !(m = m_pullup(m, 2)))
					LEAVE(ENOBUFS);
				if (bcmp(mtod(m, u_char *), addrctrl, 2) == 0)
					m_adj(m, 2);
			}
			/*
			 * Bang in a pre-made header, and set the length up
			 * to be correct. Then send it to the ethernet driver.
			 * But first correct the length.
			 */
			sp->pkt_hdr.ph.length = htons((short)(m->m_pkthdr.len));
			M_PREPEND(m, sizeof(*wh), M_DONTWAIT);
			if (m == NULL) {
				LEAVE(ENOBUFS);
			}
			wh = mtod(m, struct pppoe_full_hdr *);
			bcopy(&sp->pkt_hdr, wh, sizeof(*wh));
			NG_SEND_DATA( error, privp->ethernet_hook, m, meta);
			privp->packets_out++;
			break;
			}
		case	PPPOE_PRIMED:
			/*
			 * A PADI packet is being returned by the application
			 * that has set up this hook. This indicates that it 
			 * wants us to offer service.
			 */
			neg = sp->neg;
			if (m->m_len < sizeof(*wh)) {
				m = m_pullup(m, sizeof(*wh));
				if (m == NULL) {
					LEAVE(ENOBUFS);
				}
			}
			wh = mtod(m, struct pppoe_full_hdr *);
			ph = &wh->ph;
			session = ntohs(wh->ph.sid);
			length = ntohs(wh->ph.length);
			code = wh->ph.code; 
			if ( code != PADI_CODE) {
				LEAVE(EINVAL);
			};
			untimeout(pppoe_ticker, hook,
				    neg->timeout_handle);

			/*
			 * This is the first time we hear
			 * from the client, so note it's
			 * unicast address, replacing the
			 * broadcast address.
			 */
			bcopy(wh->eh.ether_shost,
				neg->pkt->pkt_header.eh.ether_dhost,
				ETHER_ADDR_LEN);
			sp->state = PPPOE_SOFFER;
			neg->timeout = 0;
			neg->pkt->pkt_header.ph.code = PADO_CODE;

			/*
			 * start working out the tags to respond with.
			 */
			uniqtag.hdr.tag_type = PTT_AC_COOKIE;
			uniqtag.hdr.tag_len = htons((u_int16_t)sizeof(sp));
			uniqtag.data.pointer = sp;
			init_tags(sp);
			insert_tag(sp, &neg->ac_name.hdr); /* AC_NAME */
			if ((tag = get_tag(ph, PTT_SRV_NAME)))
				insert_tag(sp, tag);	  /* return service */
			/*
			 * If we have a NULL service request
			 * and have an extra service defined in this hook,
			 * then also add a tag for the extra service.
			 * XXX this is a hack. eventually we should be able
			 * to support advertising many services, not just one 
			 */
			if (((tag == NULL) || (tag->tag_len == 0))
			&& (neg->service.hdr.tag_len != 0)) {
				insert_tag(sp, &neg->service.hdr); /* SERVICE */
			}
			if ((tag = get_tag(ph, PTT_HOST_UNIQ)))
				insert_tag(sp, tag); /* returned hostunique */
			insert_tag(sp, &uniqtag.hdr);
			scan_tags(sp, ph);
			make_packet(sp);
			sendpacket(sp);
			break;

		/*
		 * Packets coming from the hook make no sense
		 * to sessions in these states. Throw them away.
		 */
		case	PPPOE_SINIT:
		case	PPPOE_SREQ:
		case	PPPOE_SOFFER:
		case	PPPOE_SNONE:
		case	PPPOE_LISTENING:
		case	PPPOE_DEAD:
		default:
			LEAVE(ENETUNREACH);
		}
	}
quit:
	NG_FREE_DATA(m, meta);
	return error;
}

/*
 * Do local shutdown processing..
 * If we are a persistant device, we might refuse to go away, and
 * we'd only remove our links and reset ourself.
 */
static int
ng_pppoe_rmnode(node_p node)
{
	const priv_p privdata = node->private;

AAA
	node->flags |= NG_INVALID;
	ng_cutlinks(node);
	ng_unname(node);
	node->private = NULL;
	ng_unref(privdata->node);
	FREE(privdata, M_NETGRAPH);
	return (0);
}

/*
 * This is called once we've already connected a new hook to the other node.
 * It gives us a chance to balk at the last minute.
 */
static int
ng_pppoe_connect(hook_p hook)
{
	/* be really amiable and just say "YUP that's OK by me! " */
	return (0);
}

/*
 * Hook disconnection
 *
 * Clean up all dangling links and information about the session/hook.
 * For this type, removal of the last link destroys the node
 */
static int
ng_pppoe_disconnect(hook_p hook)
{
	node_p node = hook->node;
	priv_p privp = node->private;
	sessp	sp;
	int 	hooks;

AAA
	hooks = node->numhooks; /* this one already not counted */
	if (hook->private == &privp->debug_hook) {
		privp->debug_hook = NULL;
	} else if (hook->private == &privp->ethernet_hook) {
		privp->ethernet_hook = NULL;
		ng_rmnode(node);
	} else {
		sp = hook->private;
		if (sp->state != PPPOE_SNONE ) {
			pppoe_send_event(sp, NGM_PPPOE_CLOSE);
		}
		/*
		 * According to the spec, if we are connected,
		 * we should send a DISC packet if we are shutting down
		 * a session.
		 */
		if ((privp->ethernet_hook)
		&& ((sp->state == PPPOE_CONNECTED)
		 || (sp->state == PPPOE_NEWCONNECTED))) {
			struct mbuf *m;
			struct pppoe_full_hdr *wh;
			struct pppoe_tag *tag;
			int	msglen = strlen(SIGNOFF);
			void *dummy = NULL;
			int error = 0;

			/* revert the stored header to DISC/PADT mode */
		 	wh = &sp->pkt_hdr;
			wh->ph.code = PADT_CODE;
			if (pppoe_mode == PPPOE_NONSTANDARD)
				wh->eh.ether_type = ETHERTYPE_PPPOE_STUPID_DISC;
			else
				wh->eh.ether_type = ETHERTYPE_PPPOE_DISC;

			/* generate a packet of that type */
			MGETHDR(m, M_DONTWAIT, MT_DATA);
			if(m == NULL)
				printf("pppoe: Session out of mbufs\n");
			else {
				m->m_pkthdr.rcvif = NULL;
				m->m_pkthdr.len = m->m_len = sizeof(*wh);
				bcopy((caddr_t)wh, mtod(m, caddr_t),
				    sizeof(*wh));
				/*
				 * Add a General error message and adjust
				 * sizes
				 */
				wh = mtod(m, struct pppoe_full_hdr *);
				tag = wh->ph.tag;
				tag->tag_type = PTT_GEN_ERR;
				tag->tag_len = htons((u_int16_t)msglen);
				strncpy(tag->tag_data, SIGNOFF, msglen);
				m->m_pkthdr.len = (m->m_len += sizeof(*tag) +
				    msglen);
				wh->ph.length = htons(sizeof(*tag) + msglen);
				NG_SEND_DATA(error, privp->ethernet_hook, m,
				    dummy);
			}
		}
		/*
		 * As long as we have somewhere to store the timeout handle,
		 * we may have a timeout pending.. get rid of it.
		 */
		if (sp->neg) {
			untimeout(pppoe_ticker, hook, sp->neg->timeout_handle);
			if (sp->neg->m)
				m_freem(sp->neg->m);
			FREE(sp->neg, M_NETGRAPH);
		}
		FREE(sp, M_NETGRAPH);
		hook->private = NULL;
		/* work out how many session hooks there are */
		/* Node goes away on last session hook removal */
		if (privp->ethernet_hook) hooks -= 1;
		if (privp->debug_hook) hooks -= 1;
	}
	if (node->numhooks == 0)
		ng_rmnode(node);
	return (0);
}

/*
 * timeouts come here.
 */
static void
pppoe_ticker(void *arg)
{
	int s = splnet();
	hook_p hook = arg;
	sessp	sp = hook->private;
	negp	neg = sp->neg;
	int	error = 0;
	struct mbuf *m0 = NULL;
	priv_p privp = hook->node->private;
	meta_p dummy = NULL;

AAA
	switch(sp->state) {
		/*
		 * resend the last packet, using an exponential backoff.
		 * After a period of time, stop growing the backoff,
		 * and either leave it, or revert to the start.
		 */
	case	PPPOE_SINIT:
	case	PPPOE_SREQ:
		/* timeouts on these produce resends */
		m0 = m_copypacket(sp->neg->m, M_DONTWAIT);
		NG_SEND_DATA( error, privp->ethernet_hook, m0, dummy);
		neg->timeout_handle = timeout(pppoe_ticker,
					hook, neg->timeout * hz);
		if ((neg->timeout <<= 1) > PPPOE_TIMEOUT_LIMIT) {
			if (sp->state == PPPOE_SREQ) {
				/* revert to SINIT mode */
				pppoe_start(sp);
			} else {
				neg->timeout = PPPOE_TIMEOUT_LIMIT;
			}
		}
		break;
	case	PPPOE_PRIMED:
	case	PPPOE_SOFFER:
		/* a timeout on these says "give up" */
		ng_destroy_hook(hook);
		break;
	default:
		/* timeouts have no meaning in other states */
		printf("pppoe: unexpected timeout\n");
	}
	splx(s);
}


static void
sendpacket(sessp sp)
{
	int	error = 0;
	struct mbuf *m0 = NULL;
	hook_p hook = sp->hook;
	negp	neg = sp->neg;
	priv_p	privp = hook->node->private;
	meta_p dummy = NULL;

AAA
	switch(sp->state) {
	case	PPPOE_LISTENING:
	case	PPPOE_DEAD:
	case	PPPOE_SNONE:
	case	PPPOE_CONNECTED:
		printf("pppoe: sendpacket: unexpected state\n");
		break;

	case	PPPOE_NEWCONNECTED:
		/* send the PADS without a timeout - we're now connected */
		m0 = m_copypacket(sp->neg->m, M_DONTWAIT);
		NG_SEND_DATA( error, privp->ethernet_hook, m0, dummy);
		break;

	case	PPPOE_PRIMED:
		/* No packet to send, but set up the timeout */
		neg->timeout_handle = timeout(pppoe_ticker,
					hook, PPPOE_OFFER_TIMEOUT * hz);
		break;

	case	PPPOE_SOFFER:
		/*
		 * send the offer but if they don't respond
		 * in PPPOE_OFFER_TIMEOUT seconds, forget about it.
		 */
		m0 = m_copypacket(sp->neg->m, M_DONTWAIT);
		NG_SEND_DATA( error, privp->ethernet_hook, m0, dummy);
		neg->timeout_handle = timeout(pppoe_ticker,
					hook, PPPOE_OFFER_TIMEOUT * hz);
		break;

	case	PPPOE_SINIT:
	case	PPPOE_SREQ:
		m0 = m_copypacket(sp->neg->m, M_DONTWAIT);
		NG_SEND_DATA( error, privp->ethernet_hook, m0, dummy);
		neg->timeout_handle = timeout(pppoe_ticker, hook,
					(hz * PPPOE_INITIAL_TIMEOUT));
		neg->timeout = PPPOE_INITIAL_TIMEOUT * 2;
		break;

	default:
		error = EINVAL;
		printf("pppoe: timeout: bad state\n");
	}
	/* return (error); */
}

/*
 * Parse an incoming packet to see if any tags should be copied to the
 * output packet. Don't do any tags that have been handled in the main
 * state machine.
 */
static const struct pppoe_tag* 
scan_tags(sessp	sp, const struct pppoe_hdr* ph)
{
	const char *const end = (const char *)next_tag(ph);
	const char *ptn;
	const struct pppoe_tag *pt = &ph->tag[0];
	/*
	 * Keep processing tags while a tag header will still fit.
	 */
AAA
	while((const char*)(pt + 1) <= end) {
		/*
		 * If the tag data would go past the end of the packet, abort.
		 */
		ptn = (((const char *)(pt + 1)) + ntohs(pt->tag_len));
		if(ptn > end)
			return NULL;

		switch (pt->tag_type) {
		case	PTT_RELAY_SID:
			insert_tag(sp, pt);
			break;
		case	PTT_EOL:
			return NULL;
		case	PTT_SRV_NAME:
		case	PTT_AC_NAME:
		case	PTT_HOST_UNIQ:
		case	PTT_AC_COOKIE:
		case	PTT_VENDOR:
		case	PTT_SRV_ERR:
		case	PTT_SYS_ERR:
		case	PTT_GEN_ERR:
			break;
		}
		pt = (const struct pppoe_tag*)ptn;
	}
	return NULL;
}
	
static	int
pppoe_send_event(sessp sp, enum cmd cmdid)
{
	int error;
	struct ng_mesg *msg;
	struct ngpppoe_sts *sts;

AAA
	NG_MKMESSAGE(msg, NGM_PPPOE_COOKIE, cmdid,
			sizeof(struct ngpppoe_sts), M_NOWAIT);
	if (msg == NULL)
		return (ENOMEM);
	sts = (struct ngpppoe_sts *)msg->data;
	strncpy(sts->hook, sp->hook->name, NG_HOOKLEN + 1);
	error = ng_send_msg(sp->hook->node, msg, sp->creator, NULL);
	return (error);
}
