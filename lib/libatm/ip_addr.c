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
 *	@(#) $FreeBSD: src/lib/libatm/ip_addr.c,v 1.3.2.1 2001/09/28 16:52:10 dillon Exp $
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/lib/libatm/ip_addr.c,v 1.3.2.1 2001/09/28 16:52:10 dillon Exp $");

/*
 * User Space Library Functions
 * ----------------------------
 *
 * IP address utilities
 *
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netatm/port.h>
#include <netatm/atm.h>
#include <netatm/atm_if.h>
#include <netatm/atm_sap.h>
#include <netatm/atm_sys.h>
#include <netatm/atm_ioctl.h>

#include <netdb.h>
#include <string.h>

#include "libatm.h"

/*
 * Get IP address
 *
 * Return an IP address in a socket address structure, given a character
 * string with a domain name or a dotted decimal number.
 * 
 * Arguments:
 *	p	pointer to a host name or IP address
 *
 * Returns:
 *	null			error was encountered
 *	struct sockaddr_in *	a pointer to a socket address with the
 *				requested IP address
 *
 */
struct sockaddr_in *
get_ip_addr(p)
	char	*p;
{
	struct hostent			*ip_host;
	static struct sockaddr_in	sin;

	/*
	 * Get IP address of specified host name
	 */
	UM_ZERO(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	if (p[0] >= '0' && p[0] <= '9') {
		/*
		 * IP address is in dotted decimal format
		 */
		if ((sin.sin_addr.s_addr = inet_addr(p)) == -1) {
			return((struct sockaddr_in *)0);
		}
	} else {
		/*
		 * Host name is in domain name system format
		 */
		ip_host = gethostbyname(p);
		if (!ip_host ||
				ip_host->h_addrtype != AF_INET) {
			return((struct sockaddr_in *)0);
		}
		sin.sin_addr.s_addr = *(u_long *)ip_host->h_addr_list[0];
	}
	return(&sin);
}


/*
 * Format an IP address
 *
 * Return a text-formatted string with an IP address and domain name
 * given a sockaddr_in with an IP address.
 * 
 * Arguments:
 *	p	pointer to sockaddr_in with an IP address
 *
 * Returns:
 *	char *	pointer to a text-formatted string
 *
 */
char *
format_ip_addr(addr)
	struct in_addr	*addr;
{
	static char	host_name[128];
	char		*ip_num;
	struct hostent	*ip_host;

	/*
	 * Initialize
	 */
	UM_ZERO(host_name, sizeof(host_name));

	/*
	 * Check for a zero address
	 */
	if (!addr || addr->s_addr == 0) {
		return("-");
	}

	/*
	 * Get address in dotted decimal format
	 */
	ip_num = inet_ntoa(*addr);

	/*
	 * Look up name in DNS
	 */
	ip_host = gethostbyaddr((char *)addr, sizeof(addr), AF_INET);
	if (ip_host && ip_host->h_name &&
			strlen(ip_host->h_name)) {
		/*
		 * Return host name followed by dotted decimal address
		 */
		strcpy(host_name, ip_host->h_name);
		strcat(host_name, " (");
		strcat(host_name, ip_num);
		strcat(host_name, ")");
		return(host_name);
	} else {
		/*
		 * No host name -- just return dotted decimal address
		 */
		return(ip_num);
	}
}
