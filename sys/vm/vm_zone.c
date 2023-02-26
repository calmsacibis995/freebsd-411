/*
 * Copyright (c) 1997, 1998 John S. Dyson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice immediately at the beginning of the file, without modification,
 *	this list of conditions, and the following disclaimer.
 * 2. Absolutely no warranty of function or purpose is made by the author
 *	John S. Dyson.
 *
 * $FreeBSD: src/sys/vm/vm_zone.c,v 1.30.2.6 2002/10/10 19:50:16 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <vm/vm_zone.h>

static MALLOC_DEFINE(M_ZONE, "ZONE", "Zone header");

#define ZONE_ERROR_INVALID 0
#define ZONE_ERROR_NOTFREE 1
#define ZONE_ERROR_ALREADYFREE 2

#define ZONE_ROUNDING	32

#define ZENTRY_FREE	0x12342378
/*
 * void *zalloc(vm_zone_t zone) --
 *	Returns an item from a specified zone.
 *
 * void zfree(vm_zone_t zone, void *item) --
 *  Frees an item back to a specified zone.
 */
static __inline__ void *
_zalloc(vm_zone_t z)
{
	void *item;

#ifdef INVARIANTS
	if (z == 0)
		zerror(ZONE_ERROR_INVALID);
#endif

	if (z->zfreecnt <= z->zfreemin) {
		item = _zget(z);
		/*
		 * PANICFAIL allows the caller to assume that the zalloc()
		 * will always succeed.  If it doesn't, we panic here.
		 */
		if (item == NULL && (z->zflags & ZONE_PANICFAIL))
			panic("zalloc(%s) failed", z->zname);
		return(item);
	}

	item = z->zitems;
	z->zitems = ((void **) item)[0];
#ifdef INVARIANTS
	KASSERT(item != NULL, ("zitems unexpectedly NULL"));
	if (((void **) item)[1] != (void *) ZENTRY_FREE)
		zerror(ZONE_ERROR_NOTFREE);
	((void **) item)[1] = 0;
#endif

	z->zfreecnt--;
	z->znalloc++;
	return item;
}

static __inline__ void
_zfree(vm_zone_t z, void *item)
{
	((void **) item)[0] = z->zitems;
#ifdef INVARIANTS
	if (((void **) item)[1] == (void *) ZENTRY_FREE)
		zerror(ZONE_ERROR_ALREADYFREE);
	((void **) item)[1] = (void *) ZENTRY_FREE;
#endif
	z->zitems = item;
	z->zfreecnt++;
}

/*
 * This file comprises a very simple zone allocator.  This is used
 * in lieu of the malloc allocator, where needed or more optimal.
 *
 * Note that the initial implementation of this had coloring, and
 * absolutely no improvement (actually perf degradation) occurred.
 *
 * Note also that the zones are type stable.  The only restriction is
 * that the first two longwords of a data structure can be changed
 * between allocations.  Any data that must be stable between allocations
 * must reside in areas after the first two longwords.
 *
 * zinitna, zinit, zbootinit are the initialization routines.
 * zalloc, zfree, are the interrupt/lock unsafe allocation/free routines.
 * zalloci, zfreei, are the interrupt/lock safe allocation/free routines.
 */

static struct vm_zone *zlist;
static int sysctl_vm_zone(SYSCTL_HANDLER_ARGS);
static int zone_kmem_pages, zone_kern_pages, zone_kmem_kvaspace;

/*
 * Create a zone, but don't allocate the zone structure.  If the
 * zone had been previously created by the zone boot code, initialize
 * various parts of the zone code.
 *
 * If waits are not allowed during allocation (e.g. during interrupt
 * code), a-priori allocate the kernel virtual space, and allocate
 * only pages when needed.
 *
 * Arguments:
 * z		pointer to zone structure.
 * obj		pointer to VM object (opt).
 * name		name of zone.
 * size		size of zone entries.
 * nentries	number of zone entries allocated (only ZONE_INTERRUPT.)
 * flags	ZONE_INTERRUPT -- items can be allocated at interrupt time.
 * zalloc	number of pages allocated when memory is needed.
 *
 * Note that when using ZONE_INTERRUPT, the size of the zone is limited
 * by the nentries argument.  The size of the memory allocatable is
 * unlimited if ZONE_INTERRUPT is not set.
 *
 */
int
zinitna(vm_zone_t z, vm_object_t obj, char *name, int size,
	int nentries, int flags, int zalloc)
{
	int totsize;

	if ((z->zflags & ZONE_BOOT) == 0) {
		z->zsize = (size + ZONE_ROUNDING - 1) & ~(ZONE_ROUNDING - 1);
		simple_lock_init(&z->zlock);
		z->zfreecnt = 0;
		z->ztotal = 0;
		z->zmax = 0;
		z->zname = name;
		z->znalloc = 0;
		z->zitems = NULL;

		z->znext = zlist;
		zlist = z;
	}

	z->zflags |= flags;

	/*
	 * If we cannot wait, allocate KVA space up front, and we will fill
	 * in pages as needed.
	 */
	if (z->zflags & ZONE_INTERRUPT) {

		totsize = round_page(z->zsize * nentries);
		zone_kmem_kvaspace += totsize;

		z->zkva = kmem_alloc_pageable(kernel_map, totsize);
		if (z->zkva == 0) {
			zlist = z->znext;
			return 0;
		}

		z->zpagemax = totsize / PAGE_SIZE;
		if (obj == NULL) {
			z->zobj = vm_object_allocate(OBJT_DEFAULT, z->zpagemax);
		} else {
			z->zobj = obj;
			_vm_object_allocate(OBJT_DEFAULT, z->zpagemax, obj);
		}
		z->zallocflag = VM_ALLOC_INTERRUPT;
		z->zmax += nentries;
	} else {
		z->zallocflag = VM_ALLOC_SYSTEM;
		z->zmax = 0;
	}


	if (z->zsize > PAGE_SIZE)
		z->zfreemin = 1;
	else
		z->zfreemin = PAGE_SIZE / z->zsize;

	z->zpagecount = 0;
	if (zalloc)
		z->zalloc = zalloc;
	else
		z->zalloc = 1;

	return 1;
}

/*
 * Subroutine same as zinitna, except zone data structure is allocated
 * automatically by malloc.  This routine should normally be used, except
 * in certain tricky startup conditions in the VM system -- then
 * zbootinit and zinitna can be used.  Zinit is the standard zone
 * initialization call.
 */
vm_zone_t
zinit(char *name, int size, int nentries, int flags, int zalloc)
{
	vm_zone_t z;

	z = (vm_zone_t) malloc(sizeof (struct vm_zone), M_ZONE, M_NOWAIT);
	if (z == NULL)
		return NULL;

	z->zflags = 0;
	if (zinitna(z, NULL, name, size, nentries, flags, zalloc) == 0) {
		free(z, M_ZONE);
		return NULL;
	}

	return z;
}

/*
 * Initialize a zone before the system is fully up.  This routine should
 * only be called before full VM startup.
 */
void
zbootinit(vm_zone_t z, char *name, int size, void *item, int nitems)
{
	int i;

	z->zname = name;
	z->zsize = size;
	z->zpagemax = 0;
	z->zobj = NULL;
	z->zflags = ZONE_BOOT;
	z->zfreemin = 0;
	z->zallocflag = 0;
	z->zpagecount = 0;
	z->zalloc = 0;
	z->znalloc = 0;
	simple_lock_init(&z->zlock);

	bzero(item, nitems * z->zsize);
	z->zitems = NULL;
	for (i = 0; i < nitems; i++) {
		((void **) item)[0] = z->zitems;
#ifdef INVARIANTS
		((void **) item)[1] = (void *) ZENTRY_FREE;
#endif
		z->zitems = item;
		(char *) item += z->zsize;
	}
	z->zfreecnt = nitems;
	z->zmax = nitems;
	z->ztotal = nitems;

	if (zlist == 0) {
		zlist = z;
	} else {
		z->znext = zlist;
		zlist = z;
	}
}

/*
 * Zone critical region locks.
 */
static __inline int
zlock(vm_zone_t z)
{
	int s;

	s = splhigh();
	simple_lock(&z->zlock);
	return s;
}

static __inline void
zunlock(vm_zone_t z, int s)
{
	simple_unlock(&z->zlock);
	splx(s);
}

/*
 * void *zalloc(vm_zone_t zone) --
 *	Returns an item from a specified zone.
 *
 * void zfree(vm_zone_t zone, void *item) --
 *  Frees an item back to a specified zone.
 *
 * void *zalloci(vm_zone_t zone) --
 *	Returns an item from a specified zone, interrupt safe.
 *
 * void zfreei(vm_zone_t zone, void *item) --
 *  Frees an item back to a specified zone, interrupt safe.
 *
 */

void *
zalloc(vm_zone_t z)
{
#if defined(SMP)
	return zalloci(z);
#else
	return _zalloc(z);
#endif
}

void
zfree(vm_zone_t z, void *item)
{
#ifdef SMP
	zfreei(z, item);
#else
	_zfree(z, item);
#endif
}
 
/*
 * Zone allocator/deallocator.  These are interrupt / (or potentially SMP)
 * safe.  The raw zalloc/zfree routines are not interrupt safe, but are fast.
 */
void *
zalloci(vm_zone_t z)
{
	int s;
	void *item;

	s = zlock(z);
	item = _zalloc(z);
	zunlock(z, s);
	return item;
}

void
zfreei(vm_zone_t z, void *item)
{
	int s;

	s = zlock(z);
	_zfree(z, item);
	zunlock(z, s);
	return;
}

/*
 * Internal zone routine.  Not to be called from external (non vm_zone) code.
 */
void *
_zget(vm_zone_t z)
{
	int i;
	vm_page_t m;
	int nitems, nbytes;
	void *item;

	if (z == NULL)
		panic("zget: null zone");

	if (z->zflags & ZONE_INTERRUPT) {
		nbytes = z->zpagecount * PAGE_SIZE;
		nbytes -= nbytes % z->zsize;
		item = (char *) z->zkva + nbytes;
		for (i = 0; ((i < z->zalloc) && (z->zpagecount < z->zpagemax));
		     i++) {
			vm_offset_t zkva;

			m = vm_page_alloc(z->zobj, z->zpagecount,
					  z->zallocflag);
			if (m == NULL)
				break;

			zkva = z->zkva + z->zpagecount * PAGE_SIZE;
			pmap_kenter(zkva, VM_PAGE_TO_PHYS(m));
			bzero((caddr_t) zkva, PAGE_SIZE);
			z->zpagecount++;
			zone_kmem_pages++;
			cnt.v_wire_count++;
		}
		nitems = ((z->zpagecount * PAGE_SIZE) - nbytes) / z->zsize;
	} else {
		nbytes = z->zalloc * PAGE_SIZE;

		/*
		 * Check to see if the kernel map is already locked.  We could allow
		 * for recursive locks, but that eliminates a valuable debugging
		 * mechanism, and opens up the kernel map for potential corruption
		 * by inconsistent data structure manipulation.  We could also use
		 * the interrupt allocation mechanism, but that has size limitations.
		 * Luckily, we have kmem_map that is a submap of kernel map available
		 * for memory allocation, and manipulation of that map doesn't affect
		 * the kernel map structures themselves.
		 *
		 * We can wait, so just do normal map allocation in the appropriate
		 * map.
		 */
		if (lockstatus(&kernel_map->lock, NULL)) {
			int s;
			s = splvm();
#ifdef SMP
			simple_unlock(&z->zlock);
#endif
			item = (void *) kmem_malloc(kmem_map, nbytes, M_WAITOK);
#ifdef SMP
			simple_lock(&z->zlock);
#endif
			if (item != NULL)
				zone_kmem_pages += z->zalloc;
			splx(s);
		} else {
#ifdef SMP
			simple_unlock(&z->zlock);
#endif
			item = (void *) kmem_alloc(kernel_map, nbytes);
#ifdef SMP
			simple_lock(&z->zlock);
#endif
			if (item != NULL)
				zone_kern_pages += z->zalloc;
		}
		if (item != NULL) {
			bzero(item, nbytes);
		} else {
			nbytes = 0;
		}
		nitems = nbytes / z->zsize;
	}
	z->ztotal += nitems;

	/*
	 * Save one for immediate allocation
	 */
	if (nitems != 0) {
		nitems -= 1;
		for (i = 0; i < nitems; i++) {
			((void **) item)[0] = z->zitems;
#ifdef INVARIANTS
			((void **) item)[1] = (void *) ZENTRY_FREE;
#endif
			z->zitems = item;
			(char *) item += z->zsize;
		}
		z->zfreecnt += nitems;
		z->znalloc++;
	} else if (z->zfreecnt > 0) {
		item = z->zitems;
		z->zitems = ((void **) item)[0];
#ifdef INVARIANTS
		if (((void **) item)[1] != (void *) ZENTRY_FREE)
			zerror(ZONE_ERROR_NOTFREE);
		((void **) item)[1] = 0;
#endif
		z->zfreecnt--;
		z->znalloc++;
	} else {
		item = NULL;
	}

	return item;
}

static int
sysctl_vm_zone(SYSCTL_HANDLER_ARGS)
{
	int error=0;
	vm_zone_t curzone, nextzone;
	char tmpbuf[128];
	char tmpname[14];

	snprintf(tmpbuf, sizeof(tmpbuf),
	    "\nITEM            SIZE     LIMIT    USED    FREE  REQUESTS\n");
	error = SYSCTL_OUT(req, tmpbuf, strlen(tmpbuf));
	if (error)
		return (error);

	for (curzone = zlist; curzone; curzone = nextzone) {
		int i;
		int len;
		int offset;

		nextzone = curzone->znext;
		len = strlen(curzone->zname);
		if (len >= (sizeof(tmpname) - 1))
			len = (sizeof(tmpname) - 1);
		for(i = 0; i < sizeof(tmpname) - 1; i++)
			tmpname[i] = ' ';
		tmpname[i] = 0;
		memcpy(tmpname, curzone->zname, len);
		tmpname[len] = ':';
		offset = 0;
		if (curzone == zlist) {
			offset = 1;
			tmpbuf[0] = '\n';
		}

		snprintf(tmpbuf + offset, sizeof(tmpbuf) - offset,
			"%s %6.6u, %8.8u, %6.6u, %6.6u, %8.8u\n",
			tmpname, curzone->zsize, curzone->zmax,
			(curzone->ztotal - curzone->zfreecnt),
			curzone->zfreecnt, curzone->znalloc);

		len = strlen((char *)tmpbuf);
		if (nextzone == NULL)
			tmpbuf[len - 1] = 0;

		error = SYSCTL_OUT(req, tmpbuf, len);

		if (error)
			return (error);
	}
	return (0);
}

#ifdef INVARIANT_SUPPORT
void
zerror(int error)
{
	char *msg;

	switch (error) {
	case ZONE_ERROR_INVALID:
		msg = "zone: invalid zone";
		break;
	case ZONE_ERROR_NOTFREE:
		msg = "zone: entry not free";
		break;
	case ZONE_ERROR_ALREADYFREE:
		msg = "zone: freeing free entry";
		break;
	default:
		msg = "zone: invalid error";
		break;
	}
	panic(msg);
}
#endif

SYSCTL_OID(_vm, OID_AUTO, zone, CTLTYPE_STRING|CTLFLAG_RD, \
	NULL, 0, sysctl_vm_zone, "A", "Zone Info");

SYSCTL_INT(_vm, OID_AUTO, zone_kmem_pages,
	CTLFLAG_RD, &zone_kmem_pages, 0, "Number of interrupt safe pages allocated by zone");
SYSCTL_INT(_vm, OID_AUTO, zone_kmem_kvaspace,
	CTLFLAG_RD, &zone_kmem_kvaspace, 0, "KVA space allocated by zone");
SYSCTL_INT(_vm, OID_AUTO, zone_kern_pages,
	CTLFLAG_RD, &zone_kern_pages, 0, "Number of non-interrupt safe pages allocated by zone");
