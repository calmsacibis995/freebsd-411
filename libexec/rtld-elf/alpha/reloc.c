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
 * $FreeBSD: src/libexec/rtld-elf/alpha/reloc.c,v 1.10.2.5 2002/09/02 02:10:20 obrien Exp $
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

extern Elf_Dyn _GOT_END_;

/*
 * Macros for loading/storing unaligned 64-bit values.  These are
 * needed because relocations can point to unaligned data.  This
 * occurs in the DWARF2 exception frame tables generated by the
 * compiler, for instance.
 *
 * We don't use these when relocating jump slots and GOT entries,
 * since they are guaranteed to be aligned.
 */
#define load64(p) ({						\
	Elf_Addr __res;						\
	__asm__("ldq_u %0,%1" : "=r"(__res) : "m"(*(p)));	\
	__res; })

#define store64(p, v)						\
	__asm__("stq_u %1,%0" : "=m"(*(p)) : "r"(v))

/* Relocate a non-PLT object with addend. */
static int
reloc_non_plt_obj(Obj_Entry *obj_rtld, Obj_Entry *obj, const Elf_Rela *rela,
	SymCache *cache)
{
	Elf_Addr *where = (Elf_Addr *) (obj->relocbase + rela->r_offset);

	switch (ELF_R_TYPE(rela->r_info)) {

		case R_ALPHA_NONE:
			break;

		case R_ALPHA_REFQUAD: {
			const Elf_Sym *def;
			const Obj_Entry *defobj;

			def = find_symdef(ELF_R_SYM(rela->r_info), obj,
			    &defobj, false, cache);
			if (def == NULL)
				return -1;
			store64(where,
			    (Elf_Addr) (defobj->relocbase + def->st_value) +
			    load64(where) + rela->r_addend);
		}
		break;

		case R_ALPHA_GLOB_DAT: {
			const Elf_Sym *def;
			const Obj_Entry *defobj;
			Elf_Addr val;

			def = find_symdef(ELF_R_SYM(rela->r_info), obj,
			    &defobj, false, cache);
			if (def == NULL)
				return -1;
			val = (Elf_Addr) (defobj->relocbase + def->st_value +
			    rela->r_addend);
			if (load64(where) != val)
				store64(where, val);
		}
		break;

		case R_ALPHA_RELATIVE: {
			if (obj != obj_rtld ||
			    (caddr_t)where < (caddr_t)_GLOBAL_OFFSET_TABLE_ ||
			    (caddr_t)where >= (caddr_t)&_GOT_END_)
				store64(where,
				    load64(where) + (Elf_Addr) obj->relocbase);
		}
		break;

		case R_ALPHA_COPY: {
			/*
			 * These are deferred until all other relocations
			 * have been done.  All we do here is make sure
			 * that the COPY relocation is not in a shared
			 * library.  They are allowed only in executable
			 * files.
			*/
			if (!obj->mainprog) {
				_rtld_error("%s: Unexpected R_COPY "
				    " relocation in shared library",
				    obj->path);
				return -1;
			}
		}
		break;

		default:
			_rtld_error("%s: Unsupported relocation type %d"
			    " in non-PLT relocations\n", obj->path,
			    ELF_R_TYPE(rela->r_info));
			return -1;
	}
	return(0);
}

/* Process the non-PLT relocations. */
int
reloc_non_plt(Obj_Entry *obj, Obj_Entry *obj_rtld)
{
	const Elf_Rel *rellim;
	const Elf_Rel *rel;
	const Elf_Rela *relalim;
	const Elf_Rela *rela;
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

	/* Perform relocations without addend if there are any: */
	rellim = (const Elf_Rel *) ((caddr_t) obj->rel + obj->relsize);
	for (rel = obj->rel;  obj->rel != NULL && rel < rellim;  rel++) {
		Elf_Rela locrela;

		locrela.r_info = rel->r_info;
		locrela.r_offset = rel->r_offset;
		locrela.r_addend = 0;
		if (reloc_non_plt_obj(obj_rtld, obj, &locrela, cache))
			goto done;
	}

	/* Perform relocations with addend if there are any: */
	relalim = (const Elf_Rela *) ((caddr_t) obj->rela + obj->relasize);
	for (rela = obj->rela;  obj->rela != NULL && rela < relalim;  rela++) {
		if (reloc_non_plt_obj(obj_rtld, obj, rela, cache))
			goto done;
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
    /* All PLT relocations are the same kind: either Elf_Rel or Elf_Rela. */
    if (obj->pltrelsize != 0) {
	const Elf_Rel *rellim;
	const Elf_Rel *rel;

	rellim = (const Elf_Rel *)((char *)obj->pltrel + obj->pltrelsize);
	for (rel = obj->pltrel;  rel < rellim;  rel++) {
	    Elf_Addr *where;

	    assert(ELF_R_TYPE(rel->r_info) == R_ALPHA_JMP_SLOT);

	    /* Relocate the GOT slot pointing into the PLT. */
	    where = (Elf_Addr *)(obj->relocbase + rel->r_offset);
	    *where += (Elf_Addr)obj->relocbase;
	}
    } else {
	const Elf_Rela *relalim;
	const Elf_Rela *rela;

	relalim = (const Elf_Rela *)((char *)obj->pltrela + obj->pltrelasize);
	for (rela = obj->pltrela;  rela < relalim;  rela++) {
	    Elf_Addr *where;

	    assert(ELF_R_TYPE(rela->r_info) == R_ALPHA_JMP_SLOT);

	    /* Relocate the GOT slot pointing into the PLT. */
	    where = (Elf_Addr *)(obj->relocbase + rela->r_offset);
	    *where += (Elf_Addr)obj->relocbase;
	}
    }
    return 0;
}

/* Relocate the jump slots in an object. */
int
reloc_jmpslots(Obj_Entry *obj)
{
    if (obj->jmpslots_done)
	return 0;
    /* All PLT relocations are the same kind: either Elf_Rel or Elf_Rela. */
    if (obj->pltrelsize != 0) {
	const Elf_Rel *rellim;
	const Elf_Rel *rel;

	rellim = (const Elf_Rel *)((char *)obj->pltrel + obj->pltrelsize);
	for (rel = obj->pltrel;  rel < rellim;  rel++) {
	    Elf_Addr *where;
	    const Elf_Sym *def;
	    const Obj_Entry *defobj;

	    assert(ELF_R_TYPE(rel->r_info) == R_ALPHA_JMP_SLOT);
	    where = (Elf_Addr *)(obj->relocbase + rel->r_offset);
	    def = find_symdef(ELF_R_SYM(rel->r_info), obj, &defobj, true,
	        NULL);
	    if (def == NULL)
		return -1;
	    reloc_jmpslot(where,
	      (Elf_Addr)(defobj->relocbase + def->st_value));
	}
    } else {
	const Elf_Rela *relalim;
	const Elf_Rela *rela;

	relalim = (const Elf_Rela *)((char *)obj->pltrela + obj->pltrelasize);
	for (rela = obj->pltrela;  rela < relalim;  rela++) {
	    Elf_Addr *where;
	    const Elf_Sym *def;
	    const Obj_Entry *defobj;

	    assert(ELF_R_TYPE(rela->r_info) == R_ALPHA_JMP_SLOT);
	    where = (Elf_Addr *)(obj->relocbase + rela->r_offset);
	    def = find_symdef(ELF_R_SYM(rela->r_info), obj, &defobj, true,
	        NULL);
	    if (def == NULL)
		return -1;
	    reloc_jmpslot(where,
	      (Elf_Addr)(defobj->relocbase + def->st_value));
	}
    }
    obj->jmpslots_done = true;
    return 0;
}

/* Fixup the jump slot at "where" to transfer control to "target". */
void
reloc_jmpslot(Elf_Addr *where, Elf_Addr target)
{
    Elf_Addr stubaddr;

    dbg(" reloc_jmpslot: where=%p, target=%p", (void *)where, (void *)target);
    stubaddr = *where;
    if (stubaddr != target) {
	int64_t delta;
	u_int32_t inst[3];
	int instct;
	Elf_Addr pc;
	int64_t idisp;
	u_int32_t *stubptr;

	/* Point this GOT entry directly at the target. */
	*where = target;

	/*
	 * There may be multiple GOT tables, each with an entry
	 * pointing to the stub in the PLT.  But we can only find and
	 * fix up the first GOT entry.  So we must rewrite the stub as
	 * well, to perform a call to the target if it is executed.
	 *
	 * When the stub gets control, register pv ($27) contains its
	 * address.  We adjust its value so that it points to the
	 * target, and then jump indirect through it.
	 *
	 * Each PLT entry has room for 3 instructions.  If the
	 * adjustment amount fits in a signed 32-bit integer, we can
	 * simply add it to register pv.  Otherwise we must load the
	 * GOT entry itself into the pv register.
	 */
	delta = target - stubaddr;
	dbg("  stubaddr=%p, where-stubaddr=%ld, delta=%ld", (void *)stubaddr,
	  (long)where - (long)stubaddr, (long)delta);
	instct = 0;
	if ((int32_t)delta == delta) {
	    /*
	     * We can adjust pv with a LDA, LDAH sequence.
	     *
	     * First build an LDA instruction to adjust the low 16 bits.
	     */
	    inst[instct++] = 0x08 << 26 | 27 << 21 | 27 << 16 |
	      (delta & 0xffff);
	    dbg("  LDA  $27,%d($27)", (int16_t)delta);
	    /*
	     * Adjust the delta to account for the effects of the LDA,
	     * including sign-extension.
	     */
	    delta -= (int16_t)delta;
	    if (delta != 0) {
		/* Build an LDAH instruction to adjust the high 16 bits. */
		inst[instct++] = 0x09 << 26 | 27 << 21 | 27 << 16 |
		  (delta >> 16 & 0xffff);
		dbg("  LDAH $27,%d($27)", (int16_t)(delta >> 16));
	    }
	} else {
	    int64_t dhigh;

	    /* We must load the GOT entry from memory. */
	    delta = (Elf_Addr)where - stubaddr;
	    /*
	     * If the GOT entry is too far away from the PLT entry,
	     * then punt. This PLT entry will have to be looked up
	     * manually for all GOT entries except the first one.
	     * The program will still run, albeit very slowly.  It's
	     * extremely unlikely that this case could ever arise in
	     * practice, but we might as well handle it correctly if
	     * it does.
	     */
	    if ((int32_t)delta != delta) {
		dbg("  PLT stub too far from GOT to relocate");
		return;
	    }
	    dhigh = delta - (int16_t)delta;
	    if (dhigh != 0) {
		/* Build an LDAH instruction to adjust the high 16 bits. */
		inst[instct++] = 0x09 << 26 | 27 << 21 | 27 << 16 |
		  (dhigh >> 16 & 0xffff);
		dbg("  LDAH $27,%d($27)", (int16_t)(dhigh >> 16));
	    }
	    /* Build an LDQ to load the GOT entry. */
	    inst[instct++] = 0x29 << 26 | 27 << 21 | 27 << 16 |
	      (delta & 0xffff);
	    dbg("  LDQ  $27,%d($27)", (int16_t)delta);
	}

	/*
	 * Build a JMP or BR instruction to jump to the target.  If
	 * the instruction displacement fits in a sign-extended 21-bit
	 * field, we can use the more efficient BR instruction.
	 * Otherwise we have to jump indirect through the pv register.
	 */
	pc = stubaddr + 4 * (instct + 1);
	idisp = (int64_t)(target - pc) >> 2;
	if (-0x100000 <= idisp && idisp < 0x100000) {
	    inst[instct++] = 0x30 << 26 | 31 << 21 | (idisp & 0x1fffff);
	    dbg("  BR   $31,%p", (void *)target);
	} else {
	    inst[instct++] = 0x1a << 26 | 31 << 21 | 27 << 16 |
	      (idisp & 0x3fff);
	    dbg("  JMP  $31,($27),%d", (int)(idisp & 0x3fff));
	}

	/*
	 * Fill in the tail of the PLT entry first for reentrancy.
	 * Until we have overwritten the first instruction (an
	 * unconditional branch), the remaining instructions have no
	 * effect.
	 */
	stubptr = (u_int32_t *)stubaddr;
	while (instct > 1) {
	    instct--;
	    stubptr[instct] = inst[instct];
	}
	/*
	 * Commit the tail of the instruction sequence to memory
	 * before overwriting the first instruction.
	 */
	__asm__ __volatile__("wmb" : : : "memory");
	stubptr[0] = inst[0];
    }
}

/* Process an R_ALPHA_COPY relocation. */
static int
do_copy_relocation(Obj_Entry *dstobj, const Elf_Rela *rela)
{
	void *dstaddr;
	const Elf_Sym *dstsym;
	const char *name;
	unsigned long hash;
	size_t size;
	const void *srcaddr;
	const Elf_Sym *srcsym;
	Obj_Entry *srcobj;

	dstaddr = (void *) (dstobj->relocbase + rela->r_offset);
	dstsym = dstobj->symtab + ELF_R_SYM(rela->r_info);
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
	return 0;
}

/*
 * Process the special R_ALPHA_COPY relocations in the main program.  These
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
	const Elf_Rela *relalim;
	const Elf_Rela *rela;

	assert(dstobj->mainprog);	/* COPY relocations are invalid elsewhere */

	rellim = (const Elf_Rel *) ((caddr_t) dstobj->rel + dstobj->relsize);
	for (rel = dstobj->rel; dstobj->rel != NULL && rel < rellim;  rel++) {
		if (ELF_R_TYPE(rel->r_info) == R_ALPHA_COPY) {
			Elf_Rela locrela;

			locrela.r_info = rel->r_info;
			locrela.r_offset = rel->r_offset;
			locrela.r_addend = 0;
			if (do_copy_relocation(dstobj, &locrela))
				return -1;
		}
	}

	relalim = (const Elf_Rela *) ((caddr_t) dstobj->rela +
	    dstobj->relasize);
	for (rela = dstobj->rela; dstobj->rela != NULL && rela < relalim;
	    rela++) {
		if (ELF_R_TYPE(rela->r_info) == R_ALPHA_COPY) {
			if (do_copy_relocation(dstobj, rela))
				return -1;
		}
	}

	return 0;
}

/* Initialize the special PLT entries. */
void
init_pltgot(Obj_Entry *obj)
{
	u_int32_t *pltgot;

	if (obj->pltgot != NULL &&
	    (obj->pltrelsize != 0 || obj->pltrelasize != 0)) {
		/*
		 * This function will be called to perform the relocation.
		 * Look for the ldah instruction from the old PLT format since
		 * that will tell us what format we are trying to relocate.
		 */
		pltgot = (u_int32_t *) obj->pltgot;
		if ((pltgot[8] & 0xffff0000) == 0x279f0000)
			obj->pltgot[2] = (Elf_Addr) &_rtld_bind_start_old;
		else
			obj->pltgot[2] = (Elf_Addr) &_rtld_bind_start;
		/* Identify this shared object */
		obj->pltgot[3] = (Elf_Addr) obj;
	}
}
