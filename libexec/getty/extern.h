/*
 * Copyright (c) 1993
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
 *
 *	from: @(#)extern.h	8.1 (Berkeley) 6/4/93
 * $FreeBSD: src/libexec/getty/extern.h,v 1.7 2000/01/28 07:12:02 bde Exp $
 */

struct delayval;
struct termios;
struct gettyflags;
struct gettynums;
struct gettystrs;

extern	char hostname[];
extern	int hopcount;
extern	struct termios tmode, omode;
extern	struct gettyflags gettyflags[];
extern	struct gettynums gettynums[];
extern	struct gettystrs gettystrs[];

int	 adelay __P((int, struct delayval *));
const char *autobaud __P((void));
int	 delaybits __P((void));
void	 edithost __P((const char *));
void	 gendefaults __P((void));
void	 gettable __P((const char *, char *));
void	 makeenv __P((char *[]));
const char *portselector __P((void));
void	 set_ttydefaults __P((int));
void	 setchars __P((void));
void	 setdefaults __P((void));
void	 set_flags __P((int));
int	 speed __P((int));
int	 getty_chat __P((char *, int, int));
