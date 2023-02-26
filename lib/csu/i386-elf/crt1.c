/*-
 * Copyright 1996-1998 John D. Polstra.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/lib/csu/i386-elf/crt1.c,v 1.4.2.2 2002/11/18 04:57:13 bde Exp $
 */

#ifndef __GNUC__
#error "GCC is needed to compile this file"
#endif

#include <stddef.h>
#include <stdlib.h>
#include "crtbrand.c"

typedef void (*fptr)(void);

extern void _fini(void);
extern void _init(void);
extern int main(int, char **, char **);

#ifdef GCRT
extern void _mcleanup(void);
extern void monstartup(void *, void *);
extern int eprol;
extern int etext;
#endif

extern int _DYNAMIC;
#pragma weak _DYNAMIC

#ifdef __i386__
#define get_rtld_cleanup()				\
    ({ fptr __value;					\
       __asm__("movl %%edx,%0" : "=rm"(__value));	\
       __value; })
#else
#error "This file only supports the i386 architecture"
#endif

char **environ;
char *__progname = "";

void
_start(char *arguments, ...)
{
    fptr rtld_cleanup;
    int argc;
    char **argv;
    char **env;

    rtld_cleanup = get_rtld_cleanup();
    argv = &arguments;
    argc = * (int *) (argv - 1);
    env = argv + argc + 1;
    environ = env;
    if(argc > 0 && argv[0] != NULL) {
	char *s;
	__progname = argv[0];
	for (s = __progname; *s != '\0'; s++)
	    if (*s == '/')
		__progname = s + 1;
    }

    if(&_DYNAMIC != NULL)
	atexit(rtld_cleanup);

#ifdef GCRT
    atexit(_mcleanup);
#endif
    atexit(_fini);
#ifdef GCRT
    monstartup(&eprol, &etext);
#endif
    _init();
#ifndef __GNUC__
    exit( main(argc, argv, env) );
#else
	/*
	 * Some versions of gcc-2 expect the stack frame to be aligned as
	 * follows after it is set up in main():
	 *
	 *  +--------------+ <--- aligned by PREFERRED_STACK_BOUNDARY
	 *  +%ebp (if any) +
	 *  +--------------+
	 *  |return address|
	 *  +--------------+
	 *  |  arguments   |
	 *  |      :       |
	 *  |      :       |
	 *  +--------------+
	 *
	 * We implement the above to fix just the usual case in FreeBSD-4.
	 * Alignment for main() is too compiler-dependent to handle correctly
	 * in all cases here (or in the kernel).  E.g., a different alignment
	 * is required for at least gcc-2.95.4 even for the small variation
	 * of compiling main() with -fomit-frame-pointer.
	 */
	__asm__("
	andl	$~0xf, %%esp		# align stack to 16-byte boundary
	subl	$12+12, %%esp		# space for args and padding
	movl	%0, 0(%%esp)
	movl	%1, 4(%%esp)
	movl	%2, 8(%%esp)
	call	main
	movl	%%eax, 0(%%esp)
	call	exit
	" : : "r" (argc), "r" (argv), "r" (env) : "ax", "cx", "dx", "memory");
#endif
}

#ifdef GCRT
__asm__(".text");
__asm__("eprol:");
__asm__(".previous");
#endif
