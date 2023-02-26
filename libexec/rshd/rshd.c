/*-
 * Copyright (c) 1988, 1989, 1992, 1993, 1994
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1988, 1989, 1992, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static const char sccsid[] = "@(#)rshd.c	8.2 (Berkeley) 4/6/94";
#endif
static const char rcsid[] =
  "$FreeBSD: src/libexec/rshd/rshd.c,v 1.30.2.5 2002/05/14 22:27:21 des Exp $";
#endif /* not lint */

/*
 * remote shell server:
 *	[port]\0
 *	remuser\0
 *	locuser\0
 *	command\0
 *	data
 */
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/socket.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#ifdef	LOGIN_CAP
#include <login_cap.h>
#endif

/* wrapper for KAME-special getnameinfo() */
#ifndef NI_WITHSCOPEID
#define	NI_WITHSCOPEID	0
#endif

int	keepalive = 1;
int	log_success;		/* If TRUE, log all successful accesses */
int	sent_null;
int	no_delay;
#ifdef CRYPT
int	doencrypt = 0;
#endif

union sockunion {
	struct sockinet {
		u_char si_len;
		u_char si_family;
		u_short si_port;
	} su_si;
	struct sockaddr_in  su_sin;
	struct sockaddr_in6 su_sin6;
};
#define su_len		su_si.si_len
#define su_family	su_si.si_family
#define su_port		su_si.si_port

void	 doit __P((union sockunion *));
void	 error __P((const char *, ...));
void	 getstr __P((char *, int, char *));
int	 local_domain __P((char *));
char	*topdomain __P((char *));
void	 usage __P((void));

#define	OPTIONS	"alnDL"

int
main(argc, argv)
	int argc;
	char *argv[];
{
	extern int __check_rhosts_file;
	struct linger linger;
	int ch, on = 1, fromlen;
	struct sockaddr_storage from;

	openlog("rshd", LOG_PID | LOG_ODELAY, LOG_DAEMON);

	opterr = 0;
	while ((ch = getopt(argc, argv, OPTIONS)) != -1)
		switch (ch) {
		case 'a':
			/* ignored for compatibility */
			break;
		case 'l':
			__check_rhosts_file = 0;
			break;
		case 'n':
			keepalive = 0;
			break;
#ifdef CRYPT
		case 'x':
			doencrypt = 1;
			break;
#endif
		case 'D':
			no_delay = 1;
			break;
		case 'L':
			log_success = 1;
			break;
		case '?':
		default:
			usage();
			break;
		}

	argc -= optind;
	argv += optind;

#ifdef CRYPT
	if (doencrypt) {
		syslog(LOG_ERR, "-k is required for -x");
		exit(2);
	}
#endif

	fromlen = sizeof (from);
	if (getpeername(0, (struct sockaddr *)&from, &fromlen) < 0) {
		syslog(LOG_ERR, "getpeername: %m");
		exit(1);
	}
	if (keepalive &&
	    setsockopt(0, SOL_SOCKET, SO_KEEPALIVE, (char *)&on,
	    sizeof(on)) < 0)
		syslog(LOG_WARNING, "setsockopt (SO_KEEPALIVE): %m");
	linger.l_onoff = 1;
	linger.l_linger = 60;			/* XXX */
	if (setsockopt(0, SOL_SOCKET, SO_LINGER, (char *)&linger,
	    sizeof (linger)) < 0)
		syslog(LOG_WARNING, "setsockopt (SO_LINGER): %m");
	if (no_delay &&
	    setsockopt(0, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) < 0)
		syslog(LOG_WARNING, "setsockopt (TCP_NODELAY): %m");
	doit((union sockunion *)&from);
	/* NOTREACHED */
	return(0);
}

char	username[20] = "USER=";
char	homedir[64] = "HOME=";
char	shell[64] = "SHELL=";
char	path[100] = "PATH=";
char	*envinit[] =
	    {homedir, shell, path, username, 0};
char	**environ;

void
doit(fromp)
	union sockunion *fromp;
{
	extern char *__rcmd_errstr;	/* syslog hook from libc/net/rcmd.c. */
	struct passwd *pwd;
	u_short port;
	fd_set ready, readfrom;
	int cc, nfd, pv[2], pid, s;
	int one = 1;
	char *errorstr;
	char *cp, sig, buf[BUFSIZ];
	char cmdbuf[NCARGS+1], locuser[16], remuser[16];
	char fromhost[2 * MAXHOSTNAMELEN + 1];
	char numericname[INET6_ADDRSTRLEN];
	int af = fromp->su_family, err;
#ifdef	CRYPT
	int rc;
	int pv1[2], pv2[2];
#endif
#ifdef	LOGIN_CAP
	login_cap_t *lc;
#endif

	(void) signal(SIGINT, SIG_DFL);
	(void) signal(SIGQUIT, SIG_DFL);
	(void) signal(SIGTERM, SIG_DFL);
	fromp->su_port = ntohs((u_short)fromp->su_port);
	if (af != AF_INET
#ifdef INET6
	    && af != AF_INET6
#endif
	    ) {
		syslog(LOG_ERR, "malformed \"from\" address (af %d)\n", af);
		exit(1);
	}
	err = getnameinfo((struct sockaddr *)fromp, fromp->su_len, numericname,
			  sizeof(numericname), NULL, 0,
			  NI_NUMERICHOST|NI_WITHSCOPEID);
	/* XXX: do 'err' check */
#ifdef IP_OPTIONS
      if (af == AF_INET) {
	u_char optbuf[BUFSIZ/3];
	int optsize = sizeof(optbuf), ipproto, i;
	struct protoent *ip;

	if ((ip = getprotobyname("ip")) != NULL)
		ipproto = ip->p_proto;
	else
		ipproto = IPPROTO_IP;
	if (!getsockopt(0, ipproto, IP_OPTIONS, (char *)optbuf, &optsize) &&
	    optsize != 0) {
		for (i = 0; i < optsize; ) {
			u_char c = optbuf[i];
			if (c == IPOPT_LSRR || c == IPOPT_SSRR) {
				syslog(LOG_NOTICE,
					"connection refused from %s with IP option %s",
					numericname,
					c == IPOPT_LSRR ? "LSRR" : "SSRR");
				exit(1);
			}
			if (c == IPOPT_EOL)
				break;
			i += (c == IPOPT_NOP) ? 1 : optbuf[i+1];
		}
	}
      }
#endif

	if (fromp->su_port >= IPPORT_RESERVED ||
	    fromp->su_port < IPPORT_RESERVED/2) {
		syslog(LOG_NOTICE|LOG_AUTH,
		    "connection from %s on illegal port %u",
		    numericname,
		    fromp->su_port);
		exit(1);
	}

	(void) alarm(60);
	port = 0;
	s = 0;		/* not set or used if port == 0 */
	for (;;) {
		char c;
		if ((cc = read(STDIN_FILENO, &c, 1)) != 1) {
			if (cc < 0)
				syslog(LOG_NOTICE, "read: %m");
			shutdown(0, 1+1);
			exit(1);
		}
		if (c == 0)
			break;
		port = port * 10 + c - '0';
	}

	(void) alarm(0);
	if (port != 0) {
		int lport = IPPORT_RESERVED - 1;
		s = rresvport_af(&lport, af);
		if (s < 0) {
			syslog(LOG_ERR, "can't get stderr port: %m");
			exit(1);
		}
		if (port >= IPPORT_RESERVED ||
		    port < IPPORT_RESERVED/2) {
			syslog(LOG_NOTICE|LOG_AUTH,
			    "2nd socket from %s on unreserved port %u",
			    numericname,
			    port);
			exit(1);
		}
		fromp->su_port = htons(port);
		if (connect(s, (struct sockaddr *)fromp, fromp->su_len) < 0) {
			syslog(LOG_INFO, "connect second port %d: %m", port);
			exit(1);
		}
	}

	errorstr = NULL;
	realhostname_sa(fromhost, sizeof(fromhost) - 1,
			(struct sockaddr *)fromp,
			fromp->su_len);
	fromhost[sizeof(fromhost) - 1] = '\0';

#ifdef CRYPT
	if (doencrypt && af == AF_INET) {
		struct sockaddr_in local_addr;
		rc = sizeof(local_addr);
		if (getsockname(0, (struct sockaddr *)&local_addr,
		    &rc) < 0) {
			syslog(LOG_ERR, "getsockname: %m");
			error("rlogind: getsockname: %m");
			exit(1);
		}
		authopts = KOPT_DO_MUTUAL;
		rc = krb_recvauth(authopts, 0, ticket,
			"rcmd", instance, &fromaddr,
			&local_addr, kdata, "", schedule,
			version);
		des_set_key(&kdata->session, schedule);
	}
#endif
	(void) alarm(60);
	getstr(remuser, sizeof(remuser), "remuser");
	getstr(locuser, sizeof(locuser), "locuser");
	getstr(cmdbuf, sizeof(cmdbuf), "command");
	(void) alarm(0);
	setpwent();
	pwd = getpwnam(locuser);
	if (pwd == NULL) {
		syslog(LOG_INFO|LOG_AUTH,
		    "%s@%s as %s: unknown login. cmd='%.80s'",
		    remuser, fromhost, locuser, cmdbuf);
		if (errorstr == NULL)
			errorstr = "Login incorrect.\n";
		goto fail;
	}
#ifdef	LOGIN_CAP
	lc = login_getpwclass(pwd);
#endif
	if (chdir(pwd->pw_dir) < 0) {
#ifdef	LOGIN_CAP
		if (chdir("/") < 0 ||
		    login_getcapbool(lc, "requirehome", !!pwd->pw_uid)) {
			syslog(LOG_INFO|LOG_AUTH,
			"%s@%s as %s: no home directory. cmd='%.80s'",
			remuser, fromhost, locuser, cmdbuf);
			error("No remote home directory.\n");
			exit(0);
		}
#else
		(void) chdir("/");
#ifdef notdef
		syslog(LOG_INFO|LOG_AUTH,
		    "%s@%s as %s: no home directory. cmd='%.80s'",
		    remuser, fromhost, locuser, cmdbuf);
		error("No remote directory.\n");
		exit(1);
#endif
#endif
		pwd->pw_dir = "/";
	}

		if (errorstr ||
		    (pwd->pw_expire && time(NULL) >= pwd->pw_expire) ||
		    iruserok_sa(fromp, fromp->su_len, pwd->pw_uid == 0,
				 remuser, locuser) < 0) {
			if (__rcmd_errstr)
				syslog(LOG_INFO|LOG_AUTH,
			    "%s@%s as %s: permission denied (%s). cmd='%.80s'",
				    remuser, fromhost, locuser, __rcmd_errstr,
				    cmdbuf);
			else
				syslog(LOG_INFO|LOG_AUTH,
			    "%s@%s as %s: permission denied. cmd='%.80s'",
				    remuser, fromhost, locuser, cmdbuf);
fail:
			if (errorstr == NULL)
				errorstr = "Login incorrect.\n";
			error(errorstr, fromhost);
			exit(1);
		}

	if (pwd->pw_uid && !access(_PATH_NOLOGIN, F_OK)) {
		error("Logins currently disabled.\n");
		exit(1);
	}
#ifdef	LOGIN_CAP
	if (lc != NULL && fromp->su_family == AF_INET) {	/*XXX*/
		char	remote_ip[MAXHOSTNAMELEN];

		strncpy(remote_ip, numericname,
			sizeof(remote_ip) - 1);
		remote_ip[sizeof(remote_ip) - 1] = 0;
		if (!auth_hostok(lc, fromhost, remote_ip)) {
			syslog(LOG_INFO|LOG_AUTH,
			    "%s@%s as %s: permission denied (%s). cmd='%.80s'",
			    remuser, fromhost, locuser, __rcmd_errstr,
			    cmdbuf);
			error("Login incorrect.\n");
			exit(1);
		}
		if (!auth_timeok(lc, time(NULL))) {
			error("Logins not available right now\n");
			exit(1);
		}
	}
#endif	/* !LOGIN_CAP */
#if	BSD > 43
	/* before fork, while we're session leader */
	if (setlogin(pwd->pw_name) < 0)
		syslog(LOG_ERR, "setlogin() failed: %m");
#endif

	(void) write(STDERR_FILENO, "\0", 1);
	sent_null = 1;

	if (port) {
		if (pipe(pv) < 0) {
			error("Can't make pipe.\n");
			exit(1);
		}
#ifdef CRYPT
		if (doencrypt) {
			if (pipe(pv1) < 0) {
				error("Can't make 2nd pipe.\n");
				exit(1);
			}
			if (pipe(pv2) < 0) {
				error("Can't make 3rd pipe.\n");
				exit(1);
			}
		}
#endif
		pid = fork();
		if (pid == -1)  {
			error("Can't fork; try again.\n");
			exit(1);
		}
		if (pid) {
#ifdef CRYPT
			if (doencrypt) {
				static char msg[] = SECURE_MESSAGE;
				(void) close(pv1[1]);
				(void) close(pv2[1]);
				des_enc_write(s, msg, sizeof(msg) - 1, 
					schedule, &kdata->session);

			} else
#endif
			{
				(void) close(0);
				(void) close(1);
			}
			(void) close(2);
			(void) close(pv[1]);

			FD_ZERO(&readfrom);
			FD_SET(s, &readfrom);
			FD_SET(pv[0], &readfrom);
			if (pv[0] > s)
				nfd = pv[0];
			else
				nfd = s;
#ifdef CRYPT
			if (doencrypt) {
				FD_ZERO(&writeto);
				FD_SET(pv2[0], &writeto);
				FD_SET(pv1[0], &readfrom);

				nfd = MAX(nfd, pv2[0]);
				nfd = MAX(nfd, pv1[0]);
			} else
#endif
				ioctl(pv[0], FIONBIO, (char *)&one);

			/* should set s nbio! */
			nfd++;
			do {
				ready = readfrom;
#ifdef CRYPT
				if (doencrypt) {
					wready = writeto;
					if (select(nfd, &ready,
					    &wready, (fd_set *) 0,
					    (struct timeval *) 0) < 0)
						break;
				} else
#endif
					if (select(nfd, &ready, (fd_set *)0,
					  (fd_set *)0, (struct timeval *)0) < 0)
						break;
				if (FD_ISSET(s, &ready)) {
					int	ret;
#ifdef CRYPT
					if (doencrypt)
						ret = des_enc_read(s, &sig, 1,
						schedule, &kdata->session);
					else
#endif
						ret = read(s, &sig, 1);
					if (ret <= 0)
						FD_CLR(s, &readfrom);
					else
						killpg(pid, sig);
				}
				if (FD_ISSET(pv[0], &ready)) {
					errno = 0;
					cc = read(pv[0], buf, sizeof(buf));
					if (cc <= 0) {
						shutdown(s, 1+1);
						FD_CLR(pv[0], &readfrom);
					} else {
#ifdef CRYPT
						if (doencrypt)
							(void)
							  des_enc_write(s, buf, cc,
								schedule, &kdata->session);
						else
#endif
							(void)
							  write(s, buf, cc);
					}
				}
#ifdef CRYPT
				if (doencrypt && FD_ISSET(pv1[0], &ready)) {
					errno = 0;
					cc = read(pv1[0], buf, sizeof(buf));
					if (cc <= 0) {
						shutdown(pv1[0], 1+1);
						FD_CLR(pv1[0], &readfrom);
					} else
						(void) des_enc_write(STDOUT_FILENO,
						    buf, cc,
							schedule, &kdata->session);
				}

				if (doencrypt && FD_ISSET(pv2[0], &wready)) {
					errno = 0;
					cc = des_enc_read(STDIN_FILENO,
					    buf, sizeof(buf),
						schedule, &kdata->session);
					if (cc <= 0) {
						shutdown(pv2[0], 1+1);
						FD_CLR(pv2[0], &writeto);
					} else
						(void) write(pv2[0], buf, cc);
				}
#endif

			} while (FD_ISSET(s, &readfrom) ||
#ifdef CRYPT
			    (doencrypt && FD_ISSET(pv1[0], &readfrom)) ||
#endif
			    FD_ISSET(pv[0], &readfrom));
			exit(0);
		}
		setpgrp(0, getpid());
		(void) close(s);
		(void) close(pv[0]);
#ifdef CRYPT
		if (doencrypt) {
			close(pv1[0]); close(pv2[0]);
			dup2(pv1[1], 1);
			dup2(pv2[1], 0);
			close(pv1[1]);
			close(pv2[1]);
		}
#endif
		dup2(pv[1], 2);
		close(pv[1]);
	}
	if (*pwd->pw_shell == '\0')
		pwd->pw_shell = _PATH_BSHELL;
	environ = envinit;
	strncat(homedir, pwd->pw_dir, sizeof(homedir)-6);
	strcat(path, _PATH_DEFPATH);
	strncat(shell, pwd->pw_shell, sizeof(shell)-7);
	strncat(username, pwd->pw_name, sizeof(username)-6);
	cp = strrchr(pwd->pw_shell, '/');
	if (cp)
		cp++;
	else
		cp = pwd->pw_shell;
#ifdef	LOGIN_CAP
	if (setusercontext(lc, pwd, pwd->pw_uid, LOGIN_SETALL) != 0) {
                syslog(LOG_ERR, "setusercontext: %m");
		exit(1);
	}
	login_close(lc);
#else
	(void) setgid((gid_t)pwd->pw_gid);
	initgroups(pwd->pw_name, pwd->pw_gid);
	(void) setuid((uid_t)pwd->pw_uid);
#endif
	endpwent();
	if (log_success || pwd->pw_uid == 0) {
		    syslog(LOG_INFO|LOG_AUTH, "%s@%s as %s: cmd='%.80s'",
			remuser, fromhost, locuser, cmdbuf);
	}
	execl(pwd->pw_shell, cp, "-c", cmdbuf, 0);
	perror(pwd->pw_shell);
	exit(1);
}

/*
 * Report error to client.  Note: can't be used until second socket has
 * connected to client, or older clients will hang waiting for that
 * connection first.
 */
#if __STDC__
#include <stdarg.h>
#else
#include <varargs.h>
#endif

void
#if __STDC__
error(const char *fmt, ...)
#else
error(fmt, va_alist)
	char *fmt;
        va_dcl
#endif
{
	va_list ap;
	int len;
	char *bp, buf[BUFSIZ];
#if __STDC__
	va_start(ap, fmt);
#else
	va_start(ap);
#endif
	bp = buf;
	if (sent_null == 0) {
		*bp++ = 1;
		len = 1;
	} else
		len = 0;
	(void)vsnprintf(bp, sizeof(buf) - 1, fmt, ap);
	(void)write(STDERR_FILENO, buf, len + strlen(bp));
}

void
getstr(buf, cnt, err)
	char *buf, *err;
	int cnt;
{
	char c;

	do {
		if (read(STDIN_FILENO, &c, 1) != 1)
			exit(1);
		*buf++ = c;
		if (--cnt == 0) {
			error("%s too long\n", err);
			exit(1);
		}
	} while (c != 0);
}

/*
 * Check whether host h is in our local domain,
 * defined as sharing the last two components of the domain part,
 * or the entire domain part if the local domain has only one component.
 * If either name is unqualified (contains no '.'),
 * assume that the host is local, as it will be
 * interpreted as such.
 */
int
local_domain(h)
	char *h;
{
	char localhost[MAXHOSTNAMELEN];
	char *p1, *p2;

	localhost[0] = 0;
	(void) gethostname(localhost, sizeof(localhost) - 1);
	localhost[sizeof(localhost) - 1] = '\0';
	p1 = topdomain(localhost);
	p2 = topdomain(h);
	if (p1 == NULL || p2 == NULL || !strcasecmp(p1, p2))
		return (1);
	return (0);
}

char *
topdomain(h)
	char *h;
{
	char *p, *maybe = NULL;
	int dots = 0;

	for (p = h + strlen(h); p >= h; p--) {
		if (*p == '.') {
			if (++dots == 2)
				return (p);
			maybe = p;
		}
	}
	return (maybe);
}

void
usage()
{

	syslog(LOG_ERR, "usage: rshd [-%s]", OPTIONS);
	exit(2);
}
