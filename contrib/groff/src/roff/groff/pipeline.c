/* Copyright (C) 1989, 1990, 1991, 1992, 2000, 2001, 2002, 2003
   Free Software Foundation, Inc.
     Written by James Clark (jjc@jclark.com)

This file is part of groff.

groff is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

groff is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License along
with groff; see the file COPYING.  If not, write to the Free Software
Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_STRERROR
#include <string.h>
#else
extern char *strerror();
#endif

#ifdef _POSIX_VERSION

#include <sys/wait.h>

#define PID_T pid_t

#else /* not _POSIX_VERSION */

/* traditional Unix */

#define WIFEXITED(s) (((s) & 0377) == 0)
#define WIFSTOPPED(s) (((s) & 0377) == 0177)
#define WIFSIGNALED(s) (((s) & 0377) != 0 && (((s) & 0377) != 0177))
#define WEXITSTATUS(s) (((s) >> 8) & 0377)
#define WTERMSIG(s) ((s) & 0177)
#define WSTOPSIG(s) (((s) >> 8) & 0377)

#ifndef WCOREFLAG
#define WCOREFLAG 0200
#endif

#define PID_T int

#endif /* not _POSIX_VERSION */

/* SVR4 uses WCOREFLG; Net 2 uses WCOREFLAG. */
#ifndef WCOREFLAG
#ifdef WCOREFLG
#define WCOREFLAG WCOREFLG
#endif /* WCOREFLG */
#endif /* not WCOREFLAG */

#ifndef WCOREDUMP
#ifdef WCOREFLAG
#define WCOREDUMP(s) ((s) & WCOREFLAG)
#else /* not WCOREFLAG */
#define WCOREDUMP(s) (0)
#endif /* WCOREFLAG */
#endif /* not WCOREDUMP */

#include "pipeline.h"

#ifdef __STDC__
#define P(parms) parms
#else
#define P(parms) ()
#ifndef _WIN32
#define const /* as nothing */
#endif
#endif

#define error c_error
extern void error P((const char *, const char *, const char *, const char *));
extern void c_fatal P((const char *, const char *, const char *, const char *));

static void sys_fatal P((const char *));
static const char *xstrsignal P((int));
static char *i_to_a P((int));

/* MSVC can support asynchronous processes, but it's unlikely to have
   fork().  So, until someone writes an emulation, let them at least
   have a workable groff by using the good-ole DOS pipe simulation
   via temporary files...  */

#if defined(__MSDOS__) \
    || (defined(_WIN32) && !defined(_UWIN) && !defined(__CYGWIN__)) \
    || defined(__EMX__)

#include <process.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>

#include "nonposix.h"

/* A signal handler that just records that a signal has happened.  */
static int child_interrupted;

static RETSIGTYPE signal_catcher(int signo)
{
  child_interrupted++;
}

static const char *sh = "sh";
static const char *command = "command";

const char *
system_shell_name(void)
{
  static const char *shell_name;

  /* We want them to be able to use a Unixy shell if they have it
     installed.  Let spawnlp try to find it, but if it fails, default
     to COMMAND.COM.  */
  if (shell_name == NULL) {
    int sh_found = spawnlp(P_WAIT, sh, sh, "-c", ":", NULL) == 0;

    if (sh_found)
      shell_name = sh;
    else
      shell_name = command;
  }
  return shell_name;
}

const char *
system_shell_dash_c(void)
{
  if (strcmp(system_shell_name(), sh) == 0)
    return "-c";
  else
    return "/c";
}

int
is_system_shell(const char *shell)
{
  size_t shlen;
  size_t ibase = 0, idot, i;

  if (!shell)	/* paranoia */
    return 0;
  idot = shlen = strlen(shell);

  for (i = 0; i < shlen; i++) {
    if (shell[i] == '.')
      idot = i;
    else if (shell[i] == '/' || shell[i] == '\\' || shell[i] == ':') {
      ibase = i + 1;
      idot = shlen;
    }
  }

  /* "sh" and "sh.exe" should compare equal.  */
  return (strncasecmp(shell + ibase, system_shell_name(), idot - ibase) == 0
	  && (idot == shlen
	      || strcasecmp(shell + idot, ".exe") == 0
	      || strcasecmp(shell + idot, ".com") == 0));
}

#ifdef _WIN32

/* Win32 doesn't have fork() */

int
run_pipeline(int ncommands, char ***commands, int no_pipe)
{
  int save_stdin, save_stdout;
  int i;
  int last_input = 0;
  int proc_count = ncommands;
  int ret = 0;
  char err_str[BUFSIZ];
  PID_T pids[MAX_COMMANDS];

  for (i = 0; i < ncommands; i++) {
    int pdes[2];
    PID_T pid;

    /* If no_pipe is set, just run the commands in sequence
       to show the version numbers */
    if (ncommands > 1 && !no_pipe) {
      /* last command doesn't need a new pipe */
      if (i < ncommands - 1) {
	if (_pipe(pdes, BUFSIZ, O_BINARY | O_NOINHERIT) < 0)
	  sys_fatal("pipe");
      }
      /* 1st command; writer */
      if (i == 0) {
	/* save stdout */
	if ((save_stdout = _dup(1)) < 0)
	  sys_fatal("dup stdout");
	/* connect stdout to write end of pipe */
	if (_dup2(pdes[1], 1) < 0) {
	  sprintf(err_str, "%s: dup2(stdout)", commands[i][0]);
	  sys_fatal(err_str);
	}
	if (close(pdes[1]) < 0) {
	  sprintf(err_str, "%s: close(pipe[WRITE])", commands[i][0]);
	  sys_fatal(err_str);
	}
	last_input = pdes[0];
      }
      /* reader and writer */
      else if (i < ncommands - 1) {
	/* connect stdin to read end of last pipe */
	if (_dup2(last_input, 0) < 0) {
	  sprintf(err_str, " %s: dup2(stdin)", commands[i][0]);
	  sys_fatal("err_str");
	}
	/* connect stdout to write end of new pipe */
	if (_dup2(pdes[1], 1) < 0) {
	  sprintf(err_str, "%s: dup2(stdout)", commands[i][0]);
	  sys_fatal("err_str");
	}
	if (close(pdes[1]) < 0) {
	  sprintf(err_str, "%s: close(pipe[WRITE])", commands[i][0]);
	  sys_fatal(err_str);
	}
	last_input = pdes[0];
      }
      /* last command; reader */
      else {
	/* connect stdin to read end of last pipe */
	if (_dup2(last_input, 0) < 0) {
	  sprintf(err_str, "%s: dup2(stdin)", commands[i][0]);
	  sys_fatal("err_str");
	}
	if (close(last_input) < 0) {
	  sprintf(err_str, "%s: close(pipe[READ])", commands[i][0]);
	  sys_fatal(err_str);
	}
	/* restore original stdout */
	if (_dup2(save_stdout, 1) < 0) {
	  sprintf(err_str, "%s: dup2(stdout))", "groff");
	  sys_fatal(err_str);
	}
      }
    }
    if ((pid = _spawnvp(_P_NOWAIT, commands[i][0], commands[i])) < 0) {
      error("couldn't exec %1: %2",
	    commands[i][0], strerror(errno), (char *)0);
      fflush(stderr);			/* just in case error() doesn't */
      _exit(EXEC_FAILED_EXIT_STATUS);
    }
    pids[i] = pid;
  }
  for (i = 0; i < ncommands; i++) {
    int status;
    int pid;

    pid = pids[i];
    if ((pid = _cwait(&status, pid, _WAIT_CHILD)) < 0) {
      perror(NULL);
      sys_fatal("wait");
      if (WIFSIGNALED(status)) {
	int sig = WTERMSIG(status);

	error("%1: %2%3",
	      commands[i][0],
	      xstrsignal(sig),
	      WCOREDUMP(status) ? " (core dumped)" : "");
	ret |= 2;
      }
      else if (WIFEXITED(status)) {
	int exit_status = WEXITSTATUS(status);

	if (exit_status == EXEC_FAILED_EXIT_STATUS)
	  ret |= 4;
	else if (exit_status != 0)
	  ret |= 1;
      }
      else
        error("unexpected status %1", itoa(status), (char *)0, (char *)0);
      break;
    }
  }
  return ret;
}

#else  /* _WIN32 */

/* MSDOS doesn't have `fork', so we need to simulate the pipe by running
   the programs in sequence with standard streams redirected fot and
   from temporary files.
*/

int
run_pipeline(int ncommands, char ***commands, int no_pipe)
{
  int save_stdin = dup(0);
  int save_stdout = dup(1);
  char *tmpfiles[2];
  char tem1[L_tmpnam], tem2[L_tmpnam];
  int infile  = 0;
  int outfile = 1;
  int i, f, ret = 0;

  tmpfiles[0] = tmpnam(tem1);
  tmpfiles[1] = tmpnam(tem2);

  for (i = 0; i < ncommands; i++) {
    int exit_status;
    RETSIGTYPE (*prev_handler)(int);

    if (i) {
      /* redirect stdin from temp file */
      f = open(tmpfiles[infile], O_RDONLY|O_BINARY, 0666);
      if (f < 0)
	sys_fatal("open stdin");
      if (dup2(f, 0) < 0)
	sys_fatal("dup2 stdin"); 
      if (close(f) < 0)
	sys_fatal("close stdin");
    }
    if ((i < ncommands - 1) && !no_pipe) {
      /* redirect stdout to temp file */
      f = open(tmpfiles[outfile], O_WRONLY|O_CREAT|O_TRUNC|O_BINARY, 0666);
      if (f < 0)
	sys_fatal("open stdout");
      if (dup2(f, 1) < 0)
	sys_fatal("dup2 stdout");
      if (close(f) < 0)
	sys_fatal("close stdout");
    }
    else if (dup2(save_stdout, 1) < 0)
      sys_fatal("restore stdout");

    /* run the program */
    child_interrupted = 0;
    prev_handler = signal(SIGINT, signal_catcher);
    exit_status = spawnvp(P_WAIT, commands[i][0], commands[i]);
    signal(SIGINT, prev_handler);
    if (child_interrupted) {
      error("%1: Interrupted", commands[i][0], (char *)0, (char *)0);
      ret |= 2;
    }
    else if (exit_status < 0) {
      error("couldn't exec %1: %2",
	    commands[i][0], strerror(errno), (char *)0);
      fflush(stderr);			/* just in case error() doesn't */
      ret |= 4;
    }
    if (exit_status != 0)
      ret |= 1;
    /* There's no sense to continue with the pipe if one of the
       programs has ended abnormally, is there? */
    if (ret != 0)
      break;
    /* swap temp files: make output of this program be input for the next */
    infile = 1 - infile;
    outfile = 1 - outfile;
  }
  if (dup2(save_stdin, 0) < 0)
    sys_fatal("restore stdin");
  unlink(tmpfiles[0]);
  unlink(tmpfiles[1]);
  return ret;
}

#endif /* MS-DOS */

#else /* not __MSDOS__, not _WIN32 */

int
run_pipeline(int ncommands, char ***commands, int no_pipe)
{
  int i;
  int last_input = 0;
  PID_T pids[MAX_COMMANDS];
  int ret = 0;
  int proc_count = ncommands;

  for (i = 0; i < ncommands; i++) {
    int pdes[2];
    PID_T pid;

    if ((i != ncommands - 1) && !no_pipe) {
      if (pipe(pdes) < 0)
	sys_fatal("pipe");
    }
    pid = fork();
    if (pid < 0)
      sys_fatal("fork");
    if (pid == 0) {
      /* child */
      if (last_input != 0) {
	if (close(0) < 0)
	  sys_fatal("close");
	if (dup(last_input) < 0)
	  sys_fatal("dup");
	if (close(last_input) < 0)
	  sys_fatal("close");
      }
      if ((i != ncommands - 1) && !no_pipe) {
	if (close(1) < 0)
	  sys_fatal("close");
	if (dup(pdes[1]) < 0)
	  sys_fatal("dup");
	if (close(pdes[1]) < 0)
	  sys_fatal("close");
	if (close(pdes[0]))
	  sys_fatal("close");
      }
      execvp(commands[i][0], commands[i]);
      error("couldn't exec %1: %2",
	    commands[i][0], strerror(errno), (char *)0);
      fflush(stderr);			/* just in case error() doesn't */
      _exit(EXEC_FAILED_EXIT_STATUS);
    }
    /* in the parent */
    if (last_input != 0) {
      if (close(last_input) < 0)
	sys_fatal("close");
    }
    if ((i != ncommands - 1) && !no_pipe) {
      if (close(pdes[1]) < 0)
	sys_fatal("close");
      last_input = pdes[0];
    }
    pids[i] = pid;
  }
  while (proc_count > 0) {
    int status;
    PID_T pid = wait(&status);

    if (pid < 0)
      sys_fatal("wait");
    for (i = 0; i < ncommands; i++)
      if (pids[i] == pid) {
	pids[i] = -1;
	--proc_count;
	if (WIFSIGNALED(status)) {
	  int sig = WTERMSIG(status);
#ifdef SIGPIPE
	  if (sig == SIGPIPE) {
	    if (i == ncommands - 1) {
	      /* This works around a problem that occurred when using the
		 rerasterize action in gxditview.  What seemed to be
		 happening (on SunOS 4.1.1) was that pclose() closed the
		 pipe and waited for groff, gtroff got a SIGPIPE, but
		 gpic blocked writing to gtroff, and so groff blocked
		 waiting for gpic and gxditview blocked waiting for
		 groff.  I don't understand why gpic wasn't getting a
		 SIGPIPE. */
	      int j;

	      for (j = 0; j < ncommands; j++)
		if (pids[j] > 0)
		  (void)kill(pids[j], SIGPIPE);
	    }
	  }
	  else
#endif /* SIGPIPE */
	  {
	    error("%1: %2%3",
		  commands[i][0],
		  xstrsignal(sig),
		  WCOREDUMP(status) ? " (core dumped)" : "");
	    ret |= 2;
	  }
	}
	else if (WIFEXITED(status)) {
	  int exit_status = WEXITSTATUS(status);

	  if (exit_status == EXEC_FAILED_EXIT_STATUS)
	    ret |= 4;
	  else if (exit_status != 0)
	    ret |= 1;
	}
	else
	  error("unexpected status %1",	i_to_a(status), (char *)0, (char *)0);
	break;
      }
  }
  return ret;
}

#endif /* not __MSDOS__, not _WIN32 */

static void
sys_fatal(const char *s)
{
  c_fatal("%1: %2", s, strerror(errno), (char *)0);
}

static char *
i_to_a(int n)
{
  static char buf[12];
  sprintf(buf, "%d", n);
  return buf;
}

static const char *
xstrsignal(int n)
{
  static char buf[sizeof("Signal ") + 1 + sizeof(int) * 3];

#ifdef NSIG
#ifdef SYS_SIGLIST_DECLARED
  if (n >= 0 && n < NSIG && sys_siglist[n] != 0)
    return sys_siglist[n];
#endif /* SYS_SIGLIST_DECLARED */
#endif /* NSIG */
  sprintf(buf, "Signal %d", n);
  return buf;
}
