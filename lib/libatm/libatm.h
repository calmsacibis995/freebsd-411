/*
 *
 * ===================================
 * HARP  |  Host ATM Research Platform
 * ===================================
 *
 *
 * This Host ATM Research Platform ("HARP") file (the "Software") is
 * made available by Network Computing Services, Inc. ("NetworkCS")
 * "AS IS".  NetworkCS does not provide maintenance, improvements or
 * support of any kind.
 *
 * NETWORKCS MAKES NO WARRANTIES OR REPRESENTATIONS, EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE, AS TO ANY ELEMENT OF THE
 * SOFTWARE OR ANY SUPPORT PROVIDED IN CONNECTION WITH THIS SOFTWARE.
 * In no event shall NetworkCS be responsible for any damages, including
 * but not limited to consequential damages, arising from or relating to
 * any use of the Software or related support.
 *
 * Copyright 1994-1998 Network Computing Services, Inc.
 *
 * Copies of this Software may be made, however, the above copyright
 * notice must be reproduced on all copies.
 *
 *	@(#) $FreeBSD: src/lib/libatm/libatm.h,v 1.3 1999/08/27 23:58:05 peter Exp $
 *
 */

/*
 * User Space Library Functions
 * ----------------------------
 *
 * Library functions
 *
 */

#ifndef _HARP_LIBHARP_H
#define _HARP_LIBHARP_H

/*
 * Start a HARP user-space timer
 *
 *	tp	pointer to timer control block
 *	time	number of seconds for timer to run
 *	fp	pointer to function to call at expiration
 */
#define HARP_TIMER(tp, time, fp)				\
{								\
	(tp)->ht_ticks = (time);				\
	(tp)->ht_mark = 0;					\
	(tp)->ht_func = (fp);					\
	LINK2HEAD((tp), Harp_timer, harp_timer_head, ht_next);	\
}

/*
 * Cancel a HARP user-space timer
 *
 *	tp	pointer to timer control block
 */
#define HARP_CANCEL(tp)						\
{								\
	UNLINK((tp), Harp_timer, harp_timer_head, ht_next);	\
}


/*
 * HARP user-space timer control block
 */
struct harp_timer {
	struct harp_timer	*ht_next;	/* Timer chain */
	int			ht_ticks;	/* Seconds till exp */
	int			ht_mark;	/* Processing flag */
	void			(*ht_func)();	/* Function to call */
};
typedef struct harp_timer	Harp_timer;


/*
 * Externally-visible variables and functions
 */

/* atm_addr.c */
extern int		get_hex_atm_addr __P((char *, u_char *, int));
extern char		*format_atm_addr __P((Atm_addr *));

/* cache_key.c */
extern void		scsp_cache_key __P((Atm_addr *,
				struct in_addr  *, int, char *));

/* ioctl_subr.c */
extern int		do_info_ioctl __P((struct atminfreq *, int));
extern int		get_vcc_info __P((char *,
				struct air_vcc_rsp **));
extern int		get_subnet_mask __P((char *,
				struct sockaddr_in *));
extern int		get_mtu __P((char *));
extern int		verify_nif_name __P((char *));
extern int		get_cfg_info __P((char *, struct air_cfg_rsp **));
extern int		get_intf_info __P((char *, struct air_int_rsp **));
extern int		get_netif_info __P((char *, struct air_netif_rsp **));

/* ip_addr.c */
extern struct sockaddr_in	*get_ip_addr __P((char *));
extern char		*format_ip_addr __P((struct in_addr *));

/* ip_checksum.c */
extern short		ip_checksum __P((char *, int));

/* timer.c */
extern Harp_timer	*harp_timer_head;
extern int		harp_timer_exec;
extern void		timer_proc __P(());
extern int		init_timer __P(());
extern int		block_timer __P(());
extern void		enable_timer __P((int));


#endif	/* _HARP_LIBHARP_H */
