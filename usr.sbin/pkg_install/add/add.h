/* $FreeBSD: src/usr.sbin/pkg_install/add/add.h,v 1.9.2.2 2004/08/11 16:16:22 eik Exp $ */

/*
 * FreeBSD install - a package for the installation and maintainance
 * of non-core utilities.
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
 * Jordan K. Hubbard
 * 18 July 1993
 *
 * Include and define various things wanted by the add command.
 *
 */

#ifndef _INST_ADD_H_INCLUDE
#define _INST_ADD_H_INCLUDE

typedef enum { NORMAL, MASTER, SLAVE } add_mode_t;

extern char	*Prefix;
extern Boolean	NoInstall;
extern Boolean	NoRecord;
extern char	*Mode;
extern char	*Owner;
extern char	*Group;
extern char	*Directory;
extern char	*PkgName;
extern char	*PkgAddCmd;
extern char	FirstPen[];
extern add_mode_t AddMode;

int		make_hierarchy(char *);
void		extract_plist(const char *, Package *);
void		apply_perms(const char *, const char *);

#endif	/* _INST_ADD_H_INCLUDE */
