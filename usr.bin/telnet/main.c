/*
 * Copyright (c) 1988, 1990, 1993
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
 */

#include <sys/cdefs.h>

__FBSDID("$FreeBSD: src/usr.bin/telnet/main.c,v 1.10.2.5 2002/04/13 11:07:13 markm Exp $");

#ifndef lint
static const char sccsid[] = "@(#)main.c	8.3 (Berkeley) 5/30/95";
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ring.h"
#include "externs.h"
#include "defines.h"


/* These values need to be the same as defined in libtelnet/kerberos5.c */
/* Either define them in both places, or put in some common header file. */
#define OPTS_FORWARD_CREDS	0x00000002
#define OPTS_FORWARDABLE_CREDS	0x00000001

#if 0
#define FORWARD
#endif

#if defined(IPSEC) && defined(IPSEC_POLICY_IPSEC)
char *ipsec_policy_in = NULL;
char *ipsec_policy_out = NULL;
#endif

int family = AF_UNSPEC;

/*
 * Initialize variables.
 */
void
tninit(void)
{
    init_terminal();

    init_network();

    init_telnet();

    init_sys();
}

static void
usage(void)
{
	fprintf(stderr, "Usage: %s %s%s%s%s\n",
	    prompt,
	    "[-4] [-6] [-8] [-E] [-L] [-N] [-S tos] [-c] [-d]",
	    "\n\t[-e char] [-l user] [-n tracefile] ",
	    "[-r] [-s src_addr] [-u] ",
#if defined(IPSEC) && defined(IPSEC_POLICY_IPSEC)
	    "[-P policy] "
#endif
	    "[host-name [port]]"
	);
	exit(1);
}

/*
 * main.  Parse arguments, invoke the protocol or command parser.
 */

int
main(int argc, char *argv[])
{
	int ch;
	char *user;
	char *src_addr = NULL;
#ifdef	FORWARD
	extern int forward_flags;
#endif	/* FORWARD */

	tninit();		/* Clear out things */

	TerminalSaveState();

	if ((prompt = strrchr(argv[0], '/')))
		++prompt;
	else
		prompt = argv[0];

	user = NULL;

	rlogin = (strncmp(prompt, "rlog", 4) == 0) ? '~' : _POSIX_VDISABLE;
	autologin = -1;


#if defined(IPSEC) && defined(IPSEC_POLICY_IPSEC)
#define IPSECOPT	"P:"
#else
#define IPSECOPT
#endif
	while ((ch = getopt(argc, argv,
			    "468EKLNS:X:acde:fFk:l:n:rs:t:uxy" IPSECOPT)) != -1)
#undef IPSECOPT
	{
		switch(ch) {
		case '4':
			family = AF_INET;
			break;
#ifdef INET6
		case '6':
			family = AF_INET6;
			break;
#endif
		case '8':
			eight = 3;	/* binary output and input */
			break;
		case 'E':
			rlogin = escape = _POSIX_VDISABLE;
			break;
		case 'K':
			break;
		case 'L':
			eight |= 2;	/* binary output only */
			break;
		case 'N':
			doaddrlookup = 0;
			break;
		case 'S':
		    {
#ifdef	HAS_GETTOS
			extern int tos;

			if ((tos = parsetos(optarg, "tcp")) < 0)
				fprintf(stderr, "%s%s%s%s\n",
					prompt, ": Bad TOS argument '",
					optarg,
					"; will try to use default TOS");
#else
			fprintf(stderr,
			   "%s: Warning: -S ignored, no parsetos() support.\n",
								prompt);
#endif
		    }
			break;
		case 'X':
			break;
		case 'a':
			autologin = 1;
			break;
		case 'c':
			skiprc = 1;
			break;
		case 'd':
			debug = 1;
			break;
		case 'e':
			set_escape_char(optarg);
			break;
		case 'f':
			fprintf(stderr,
			 "%s: Warning: -f ignored, no Kerberos V5 support.\n",
				prompt);
			break;
		case 'F':
			fprintf(stderr,
			 "%s: Warning: -F ignored, no Kerberos V5 support.\n",
				prompt);
			break;
		case 'k':
			fprintf(stderr,
			   "%s: Warning: -k ignored, no Kerberos V4 support.\n",
								prompt);
			break;
		case 'l':
			autologin = 1;
			user = optarg;
			break;
		case 'n':
				SetNetTrace(optarg);
			break;
		case 'r':
			rlogin = '~';
			break;
		case 's':
			src_addr = optarg;
			break;
		case 'u':
			family = AF_UNIX;
			break;
		case 'x':
			fprintf(stderr,
			    "%s: Warning: -x ignored, no ENCRYPT support.\n",
								prompt);
			break;
		case 'y':
			fprintf(stderr,
			    "%s: Warning: -y ignored, no ENCRYPT support.\n",
								prompt);
			break;
#if defined(IPSEC) && defined(IPSEC_POLICY_IPSEC)
		case 'P':
			if (!strncmp("in", optarg, 2))
				ipsec_policy_in = strdup(optarg);
			else if (!strncmp("out", optarg, 3))
				ipsec_policy_out = strdup(optarg);
			else
				usage();
			break;
#endif
		case '?':
		default:
			usage();
			/* NOTREACHED */
		}
	}
	if (autologin == -1)
		autologin = (rlogin == _POSIX_VDISABLE) ? 0 : 1;

	argc -= optind;
	argv += optind;

	if (argc) {
		char *args[9], **argp = args;

		if (argc > 2)
			usage();
		*argp++ = prompt;
		if (user) {
			*argp++ = strdup("-l");
			*argp++ = user;
		}
		if (src_addr) {
			*argp++ = strdup("-s");
			*argp++ = src_addr;
		}
		*argp++ = argv[0];		/* host */
		if (argc > 1)
			*argp++ = argv[1];	/* port */
		*argp = 0;

		if (setjmp(toplevel) != 0)
			Exit(0);
		if (tn(argp - args, args) == 1)
			return (0);
		else
			return (1);
	}
	(void)setjmp(toplevel);
	for (;;) {
			command(1, 0, 0);
	}
	return 0;
}
