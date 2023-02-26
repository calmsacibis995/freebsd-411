/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@login.dknet.dk> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/lib/libdisk/blocks.c,v 1.7.2.3 2001/05/13 21:01:37 jkh Exp $
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "libdisk.h"

void *
read_block(int fd, daddr_t block, u_long sector_size)
{
	void *foo;

	foo = malloc(sector_size);
	if (!foo)
		return NULL;
	if (-1 == lseek(fd, (off_t)block * sector_size, SEEK_SET)) {
		free (foo);
		return NULL;
	}
	if (sector_size != read(fd, foo, sector_size)) {
		free (foo);
		return NULL;
	}
	return foo;
}

int
write_block(int fd, daddr_t block, void *foo, u_long sector_size)
{
	if (-1 == lseek(fd, (off_t)block * sector_size, SEEK_SET))
		return -1;
	if (sector_size != write(fd, foo, sector_size))
		return -1;
	return 0;
}
