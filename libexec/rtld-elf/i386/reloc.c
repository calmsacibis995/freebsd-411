/*-
 * Copyright 1996, 1997, 1998, 1999 John D. Polstra.
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
 * $FreeBSD: src/libexec/rtld-elf/i386/reloc.c,v 1.6.2.2 2002/06/16 20:02:09 dillon Exp $
 */

/*
 * Dynamic linker for ELF.
 *
 * John Polstra <jdp@polstra.com>.
 */

#include <sys/param.h>
#include <sys/mman.h>

#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "rtld.h"

/*
 * Process the special R_386_COPY relocations in the main program.  These
 * copy data from a shared object into a region in the main program's BSS
 * segment.
 *
 * Returns 0 on success, -1 on failure.
 */
int
do_copy_relocations(Obj_Entry *dstobj)
{
    const Elf_Rel *rellim;
    const Elf_Rel *rel;

    assert(dstobj->mainprog);	/* COPY relocations are invalid elsewhere */

    rellim = (const Elf_Rel *) ((caddr_t) dstobj->rel + dstobj->relsize);
    for (rel = dstobj->rel;  rel < rellim;  rel++) {
	if (ELF_R_TYPE(rel->r_info) == R_386_COPY) {
	    void *dstaddr;
	    const Elf_Sym *dstsym;
	    const char *name;
	    unsigned long hash;
	    size_t size;
	    const void *srcaddr;
	    const Elf_Sym *srcsym;
	    Obj_Entry *srcobj;

	    dstaddr = (void *) (dstobj->relocbase + rel->r_offset);
	    dstsym = dstobj->symtab + ELF_R_SYM(rel->r_info);
	    name = dstobj->strtab + dstsym->st_name;
	    hash = elf_hash(name);
	    size = dstsym->st_size;

	    for (srcobj = dstobj->next;  srcobj != NULL;  srcobj = srcobj->next)
		if ((srcsym = symlook_obj(name, hash, srcobj, false)) != NULL)
		    break;

	    if (srcobj == NULL) {
		_rtld_error("Undefined symbol \"%s\" referenced from COPY"
		  " relocation in %s", name, dstobj->path);
		return -1;
	    }

	    srcaddr = (const void *) (srcobj->relocbase + srcsym->st_value);
	    memcpy(dstaddr, srcaddr, size);
	}
    }

    return 0;
}

/* Initialize the special GOT entries. */
void
init_pltgot(Obj_Entry *obj)
{
    if (obj->pltgot != NULL) {
	obj->pltgot[1] = (Elf_Addr) obj;
	obj->pltgot[2] = (Elf_Addr) &_rtld_bind_start;
    }
}

/* Process the non-PLT relocations. */
int
reloc_non_plt(Obj_Entry *obj, Obj_Entry *obj_rtld)
{
	const Elf_Rel *rellim;
	const Elf_Rel *rel;
	SymCache *cache;
	int bytes = obj->nchains * sizeof(SymCache);
	int r = -1;

	/*
	 * The dynamic loader may be called from a thread, we have
	 * limited amounts of stack available so we cannot use alloca().
	 */
	cache = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_ANON, -1, 0);
	if (cache == MAP_FAILED)
	    cache = NULL;
	if (cache != NULL)
	    memset(cache, 0, bytes);

	rellim = (const Elf_Rel *) ((caddr_t) obj->rel + obj->relsize);
	for (rel = obj->rel;  rel < rellim;  rel++) {
	    Elf_Addr *where = (Elf_Addr *) (obj->relocbase + rel->r_offset);

	    switch (ELF_R_TYPE(rel->r_info)) {

	    case R_386_NONE:
		break;

	    case R_386_32:
		{
		    const Elf_Sym *def;
		    const Obj_Entry *defobj;

		    def = find_symdef(ELF_R_SYM(rel->r_info), obj, &defobj,
		      false, cache);
		    if (def == NULL)
			goto done;

		    *where += (Elf_Addr) (defobj->relocbase + def->st_value);
		}
		break;

	    case R_386_PC32:
		/*
		 * I don't think the dynamic linker should ever see this
		 * type of relocation.  But the binutils-2.6 tools sometimes
		 * generate it.
		 */
		{
		    const Elf_Sym *def;
		    const Obj_Entry *defobj;

		    def = find_symdef(ELF_R_SYM(rel->r_info), obj, &defobj,
		      false, cache);
		    if (def == NULL)
			goto done;

		    *where +=
		      (Elf_Addr) (defobj->relocbase + def->st_value) -
		      (Elf_Addr) where;
		}
		break;

	    case R_386_COPY:
		/*
		 * These are deferred until all other relocations have
		 * been done.  All we do here is make sure that the COPY
		 * relocation is not in a shared library.  They are allowed
		 * only in executable files.
		 */
		if (!obj->mainprog) {
		    _rtld_error("%s: Unexpected R_386_COPY relocation"
		      " in shared library", obj->path);
		    goto done;
		}
		break;

	    case R_386_GLOB_DAT:
		{
		    const Elf_Sym *def;
		    const Obj_Entry *defobj;

		    def = find_symdef(ELF_R_SYM(rel->r_info), obj, &defobj,
		      false, cache);
		    if (def == NULL)
			goto done;

		    *where = (Elf_Addr) (defobj->relocbase + def->st_value);
		}
		break;

	    case R_386_RELATIVE:
		*where += (Elf_Addr) obj->relocbase;
		break;

	    default:
		_rtld_error("%s: Unsupported relocation type %d"
		  " in non-PLT relocations\n", obj->path,
		  ELF_R_TYPE(rel->r_info));
		goto done;
	    }
	}
	r = 0;
done:
	if (cache)
	    munmap(cache, bytes);
	return(r);
}

/* Process the PLT relocations. */
int
reloc_plt(Obj_Entry *obj)
{
    const Elf_Rel *rellim;
    const Elf_Rel *rel;

    rellim = (const Elf_Rel *)((char *)obj->pltrel + obj->pltrelsize);
    for (rel = obj->pltrel;  rel < rellim;  rel++) {
	Elf_Addr *where;

	assert(ELF_R_TYPE(rel->r_info) == R_386_JMP_SLOT);

	/* Relocate the GOT slot pointing into the PLT. */
	where = (Elf_Addr *)(obj->relocbase + rel->r_offset);
	*where += (Elf_Addr)obj->relocbase;
    }
    return 0;
}

/* Relocate the jump slots in an object. */
int
reloc_jmpslots(Obj_Entry *obj)
{
    const Elf_Rel *rellim;
    const Elf_Rel *rel;

    if (obj->jmpslots_done)
	return 0;
    rellim = (const Elf_Rel *)((char *)obj->pltrel + obj->pltrelsize);
    for (rel = obj->pltrel;  rel < rellim;  rel++) {
	Elf_Addr *where;
	const Elf_Sym *def;
	const Obj_Entry *defobj;

	assert(ELF_R_TYPE(rel->r_info) == R_386_JMP_SLOT);
	where = (Elf_Addr *)(obj->relocbase + rel->r_offset);
	def = find_symdef(ELF_R_SYM(rel->r_info), obj, &defobj, true, NULL);
	if (def == NULL)
	    return -1;
	reloc_jmpslot(where, (Elf_Addr)(defobj->relocbase + def->st_value));
    }
    obj->jmpslots_done = true;
    return 0;
}
