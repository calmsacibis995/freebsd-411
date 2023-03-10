/* $FreeBSD: src/usr.bin/less/defines.h,v 1.1.2.2 2000/07/19 09:24:25 ps Exp $ */
/* defines.h.  Generated automatically by configure.  */
/* defines.h.in.  Generated automatically from configure.in by autoheader.  */
/* Unix definition file for less.  -*- C -*-
 *
 * This file has 3 sections:
 * User preferences.
 * Settings always true on Unix.
 * Settings automatically determined by configure.
 *
 * * * * * *  WARNING  * * * * * *
 * If you edit defines.h by hand, do "touch stamp-h" before you run make
 * so config.status doesn't overwrite your changes.
 */

/* User preferences.  */

/*
 * SECURE is 1 if you wish to disable a bunch of features in order to
 * be safe to run by unprivileged users.
 */
#define	SECURE		0

/*
 * SHELL_ESCAPE is 1 if you wish to allow shell escapes.
 * (This is possible only if your system supplies the system() function.)
 */
#define	SHELL_ESCAPE	(!SECURE)

/*
 * EXAMINE is 1 if you wish to allow examining files by name from within less.
 */
#define	EXAMINE		(!SECURE)

/*
 * TAB_COMPLETE_FILENAME is 1 if you wish to allow the TAB key
 * to complete filenames at prompts.
 */
#define	TAB_COMPLETE_FILENAME	(!SECURE)

/*
 * CMD_HISTORY is 1 if you wish to allow keys to cycle through
 * previous commands at prompts.
 */
#define	CMD_HISTORY	1

/*
 * HILITE_SEARCH is 1 if you wish to have search targets to be 
 * displayed in standout mode.
 */
#define	HILITE_SEARCH	1

/*
 * EDITOR is 1 if you wish to allow editor invocation (the "v" command).
 * (This is possible only if your system supplies the system() function.)
 * EDIT_PGM is the name of the (default) editor to be invoked.
 */
#define	EDITOR		(!SECURE)

/*
 * TAGS is 1 if you wish to support tag files.
 */
#define	TAGS		(!SECURE)

/*
 * USERFILE is 1 if you wish to allow a .less file to specify 
 * user-defined key bindings.
 */
#define	USERFILE	(!SECURE)

/*
 * GLOB is 1 if you wish to have shell metacharacters expanded in filenames.
 * This will generally work if your system provides the "popen" function
 * and the "echo" shell command.
 */
#define	GLOB		(!SECURE)

/*
 * PIPEC is 1 if you wish to have the "|" command
 * which allows the user to pipe data into a shell command.
 */
#define	PIPEC		(!SECURE)

/*
 * LOGFILE is 1 if you wish to allow the -l option (to create log files).
 */
#define	LOGFILE		(!SECURE)

/*
 * GNU_OPTIONS is 1 if you wish to support the GNU-style command
 * line options --help and --version.
 */
#define	GNU_OPTIONS	1

/*
 * ONLY_RETURN is 1 if you want RETURN to be the only input which
 * will continue past an error message.
 * Otherwise, any key will continue past an error message.
 */
#define	ONLY_RETURN	0

/*
 * LESSKEYFILE is the filename of the default lesskey output file 
 * (in the HOME directory).
 * LESSKEYFILE_SYS is the filename of the system-wide lesskey output file.
 * DEF_LESSKEYINFILE is the filename of the default lesskey input 
 * (in the HOME directory).
 */
#define	LESSKEYFILE		".less"
#define	LESSKEYFILE_SYS		"/etc/lesskey"
#define	DEF_LESSKEYINFILE	".lesskey"


/* Settings always true on Unix.  */

/*
 * Define MSDOS_COMPILER if compiling under Microsoft C.
 */
#define	MSDOS_COMPILER	0

/*
 * Pathname separator character.
 */
#define	PATHNAME_SEP	"/"

/*
 * HAVE_SYS_TYPES_H is 1 if your system has <sys/types.h>.
 */
#define HAVE_SYS_TYPES_H	1

/*
 * Define if you have the <sgstat.h> header file.
 */
/* #undef HAVE_SGSTAT_H */

/*
 * HAVE_PERROR is 1 if your system has the perror() call.
 * (Actually, if it has sys_errlist, sys_nerr and errno.)
 */
#define	HAVE_PERROR	1

/*
 * HAVE_TIME is 1 if your system has the time() call.
 */
#define	HAVE_TIME	1

/*
 * HAVE_SHELL is 1 if your system supports a SHELL command interpreter.
 */
#define	HAVE_SHELL	1

/*
 * Default shell metacharacters and meta-escape character.
 */
#define	DEF_METACHARS	"; \t\n'\"()<>|&^`\\"
#define	DEF_METAESCAPE	"\\"

/* 
 * HAVE_DUP is 1 if your system has the dup() call.
 */
#define	HAVE_DUP	1

/*
 * Sizes of various buffers.
 */
#define	CMDBUF_SIZE	512	/* Buffer for multichar commands */
#define	UNGOT_SIZE	100	/* Max chars to unget() */
#define	LINEBUF_SIZE	1024	/* Max size of line in input file */
#define	OUTBUF_SIZE	1024	/* Output buffer */
#define	PROMPT_SIZE	200	/* Max size of prompt string */
#define	TERMBUF_SIZE	2048	/* Termcap buffer for tgetent */
#define	TERMSBUF_SIZE	1024	/* Buffer to hold termcap strings */
#define	TAGLINE_SIZE	512	/* Max size of line in tags file */

/* Settings automatically determined by configure.  */

/* Define to `long' if <sys/types.h> doesn't define.  */
/* #undef off_t */

/* Define if you need to in order for stat and other things to work.  */
/* #undef _POSIX_SOURCE */

/* Define as the return type of signal handlers (int or void).  */
#define RETSIGTYPE void

/* Define if you have the ANSI C header files.  */
#define STDC_HEADERS 1

/*
 * Regular expression library.
 * Define exactly one of the following to be 1:
 * HAVE_POSIX_REGCOMP: POSIX regcomp() and regex.h
 * HAVE_PCRE: PCRE (Perl-compatible regular expression) library
 * HAVE_RE_COMP: BSD re_comp()
 * HAVE_REGCMP: System V regcmp()
 * HAVE_V8_REGCOMP: Henry Spencer V8 regcomp() and regexp.h
 * NO_REGEX: pattern matching is supported, but without metacharacters.
 */
#define HAVE_POSIX_REGCOMP 1
/* #undef HAVE_PCRE */
/* #undef HAVE_RE_COMP */
/* #undef HAVE_REGCMP */
/* #undef HAVE_V8_REGCOMP */
/* #undef NO_REGEX */
/* #undef HAVE_REGEXEC2 */

/* Define HAVE_VOID if your compiler supports the "void" type. */
#define HAVE_VOID 1

/* Define HAVE_CONST if your compiler supports the "const" modifier. */
#define HAVE_CONST 1

/* Define HAVE_TIME_T if your system supports the "time_t" type. */
#define HAVE_TIME_T 1

/* Define HAVE_STRERROR if you have the strerror() function. */
#define HAVE_STRERROR 1

/* Define HAVE_FILENO if you have the fileno() macro. */
#define HAVE_FILENO 1

/* Define HAVE_ERRNO if you have the errno variable */
/* Define MUST_DEFINE_ERRNO if you have errno but it is not define 
 * in errno.h */
#define HAVE_ERRNO 1
/* #undef MUST_DEFINE_ERRNO */

/* Define HAVE_SYS_ERRLIST if you have the sys_errlist[] variable */
#define HAVE_SYS_ERRLIST 1

/* Define HAVE_OSPEED if your termcap library has the ospeed variable */
/* Define MUST_DEFINE_OSPEED if you have ospeed but it is not defined
 * in termcap.h. */
#define HAVE_OSPEED 1
/* #undef MUST_DEFINE_OSPEED */

/* Define HAVE_LOCALE if you have locale.h and setlocale. */
#define HAVE_LOCALE 1

/* Define HAVE_TERMIOS_FUNCS if you have tcgetattr/tcsetattr */
#define HAVE_TERMIOS_FUNCS 1

/* Define HAVE_UPPER_LOWER if you have isupper, islower, toupper, tolower */
#define HAVE_UPPER_LOWER 1

/* Define HAVE_SIGSET_T you have the sigset_t type */
/* #undef HAVE_SIGSET_T */

/* Define HAVE_SIGEMPTYSET if you have the sigemptyset macro */
#define HAVE_SIGEMPTYSET 1

/* Define EDIT_PGM to your editor. */
#define EDIT_PGM "vi"

/* Define if you have the _setjmp function.  */
#define HAVE__SETJMP 1

/* Define if you have the memcpy function.  */
#define HAVE_MEMCPY 1

/* Define if you have the popen function.  */
#define HAVE_POPEN 1

/* Define if you have the sigprocmask function.  */
#define HAVE_SIGPROCMASK 1

/* Define if you have the sigsetmask function.  */
#define HAVE_SIGSETMASK 1

/* Define if you have the stat function.  */
#define HAVE_STAT 1

/* Define if you have the strchr function.  */
#define HAVE_STRCHR 1

/* Define if you have the strstr function.  */
#define HAVE_STRSTR 1

/* Define if you have the system function.  */
#define HAVE_SYSTEM 1

/* Define if you have the <ctype.h> header file.  */
#define HAVE_CTYPE_H 1

/* Define if you have the <errno.h> header file.  */
#define HAVE_ERRNO_H 1

/* Define if you have the <fcntl.h> header file.  */
#define HAVE_FCNTL_H 1

/* Define if you have the <limits.h> header file.  */
#define HAVE_LIMITS_H 1

/* Define if you have the <stdio.h> header file.  */
#define HAVE_STDIO_H 1

/* Define if you have the <stdlib.h> header file.  */
#define HAVE_STDLIB_H 1

/* Define if you have the <string.h> header file.  */
#define HAVE_STRING_H 1

/* Define if you have the <sys/ioctl.h> header file.  */
#define HAVE_SYS_IOCTL_H 1

/* Define if you have the <sys/ptem.h> header file.  */
/* #undef HAVE_SYS_PTEM_H */

/* Define if you have the <sys/stream.h> header file.  */
/* #undef HAVE_SYS_STREAM_H */

/* Define if you have the <termcap.h> header file.  */
#define HAVE_TERMCAP_H 1

/* Define if you have the <termio.h> header file.  */
/* #undef HAVE_TERMIO_H */

/* Define if you have the <termios.h> header file.  */
#define HAVE_TERMIOS_H 1

/* Define if you have the <time.h> header file.  */
#define HAVE_TIME_H 1

/* Define if you have the <unistd.h> header file.  */
#define HAVE_UNISTD_H 1

/* Define if you have the <values.h> header file.  */
/* #undef HAVE_VALUES_H */

/* Define if you have the PW library (-lPW).  */
/* #undef HAVE_LIBPW */

/* Define if you have the gen library (-lgen).  */
/* #undef HAVE_LIBGEN */

/* Define if you have the intl library (-lintl).  */
/* #undef HAVE_LIBINTL */
