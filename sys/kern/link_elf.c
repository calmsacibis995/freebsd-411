/*-
 * Copyright (c) 1998 Doug Rabson
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/kern/link_elf.c,v 1.24 1999/12/24 15:33:36 bde Exp $
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/fcntl.h>
#include <sys/vnode.h>
#include <sys/linker.h>
#include <machine/elf.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_zone.h>
#include <sys/lock.h>
#ifdef SPARSE_MAPPING
#include <vm/vm_object.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#endif
#include <vm/pmap.h>
#include <vm/vm_map.h>

static int	link_elf_load_module(const char*, linker_file_t*);
static int	link_elf_load_file(const char*, linker_file_t*);
static int	link_elf_lookup_symbol(linker_file_t, const char*,
				       c_linker_sym_t*);
static int	link_elf_symbol_values(linker_file_t, c_linker_sym_t, linker_symval_t*);
static int	link_elf_search_symbol(linker_file_t, caddr_t value,
				       c_linker_sym_t* sym, long* diffp);

static void	link_elf_unload_file(linker_file_t);
static void	link_elf_unload_module(linker_file_t);

static struct linker_class_ops link_elf_class_ops = {
    link_elf_load_module,
};

static struct linker_file_ops link_elf_file_ops = {
    link_elf_lookup_symbol,
    link_elf_symbol_values,
    link_elf_search_symbol,
    link_elf_unload_file,
};

static struct linker_file_ops link_elf_module_ops = {
    link_elf_lookup_symbol,
    link_elf_symbol_values,
    link_elf_search_symbol,
    link_elf_unload_module,
};
typedef struct elf_file {
    caddr_t		address;	/* Relocation address */
#ifdef SPARSE_MAPPING
    vm_object_t		object;		/* VM object to hold file pages */
#endif
    const Elf_Dyn*	dynamic;	/* Symbol table etc. */
    Elf_Off		nbuckets;	/* DT_HASH info */
    Elf_Off		nchains;
    const Elf_Off*	buckets;
    const Elf_Off*	chains;
    caddr_t		hash;
    caddr_t		strtab;		/* DT_STRTAB */
    int			strsz;		/* DT_STRSZ */
    const Elf_Sym*	symtab;		/* DT_SYMTAB */
    Elf_Addr*		got;		/* DT_PLTGOT */
    const Elf_Rel*	pltrel;		/* DT_JMPREL */
    int			pltrelsize;	/* DT_PLTRELSZ */
    const Elf_Rela*	pltrela;	/* DT_JMPREL */
    int			pltrelasize;	/* DT_PLTRELSZ */
    const Elf_Rel*	rel;		/* DT_REL */
    int			relsize;	/* DT_RELSZ */
    const Elf_Rela*	rela;		/* DT_RELA */
    int			relasize;	/* DT_RELASZ */
    caddr_t		modptr;
    const Elf_Sym*	ddbsymtab;	/* The symbol table we are using */
    long		ddbsymcnt;	/* Number of symbols */
    caddr_t		ddbstrtab;	/* String table */
    long		ddbstrcnt;	/* number of bytes in string table */
    caddr_t		symbase;	/* malloc'ed symbold base */
    caddr_t		strbase;	/* malloc'ed string base */
} *elf_file_t;

static int		parse_dynamic(linker_file_t lf);
static int		load_dependancies(linker_file_t lf);
static int		relocate_file(linker_file_t lf);
static int		parse_module_symbols(linker_file_t lf);

/*
 * The kernel symbol table starts here.
 */
extern struct _dynamic _DYNAMIC;

static void
link_elf_init(void* arg)
{
#ifdef __ELF__
    Elf_Dyn	*dp;
    caddr_t	modptr, baseptr, sizeptr;
    elf_file_t	ef;
    char	*modname;
#endif

#if ELF_TARG_CLASS == ELFCLASS32
    linker_add_class("elf32", NULL, &link_elf_class_ops);
#else
    linker_add_class("elf64", NULL, &link_elf_class_ops);
#endif

#ifdef __ELF__
    dp = (Elf_Dyn*) &_DYNAMIC;
    if (dp) {
	ef = malloc(sizeof(struct elf_file), M_LINKER, M_NOWAIT);
	if (ef == NULL)
	    panic("link_elf_init: Can't create linker structures for kernel");
	bzero(ef, sizeof(*ef));

	ef->address = 0;
#ifdef SPARSE_MAPPING
	ef->object = 0;
#endif
	ef->dynamic = dp;
	modname = NULL;
	modptr = preload_search_by_type("elf kernel");
	if (modptr)
	    modname = (char *)preload_search_info(modptr, MODINFO_NAME);
	if (modname == NULL)
	    modname = "kernel";
	linker_kernel_file = linker_make_file(modname, ef, &link_elf_file_ops);
	if (linker_kernel_file == NULL)
	    panic("link_elf_init: Can't create linker structures for kernel");
	parse_dynamic(linker_kernel_file);
	linker_kernel_file->address = (caddr_t) KERNBASE;
	linker_kernel_file->size = -(intptr_t)linker_kernel_file->address;

	if (modptr) {
	    ef->modptr = modptr;
	    baseptr = preload_search_info(modptr, MODINFO_ADDR);
	    if (baseptr)
		linker_kernel_file->address = *(caddr_t *)baseptr;
	    sizeptr = preload_search_info(modptr, MODINFO_SIZE);
	    if (sizeptr)
		linker_kernel_file->size = *(size_t *)sizeptr;
	}
	(void)parse_module_symbols(linker_kernel_file);
	linker_current_file = linker_kernel_file;
	linker_kernel_file->flags |= LINKER_FILE_LINKED;
    }
#endif
}

SYSINIT(link_elf, SI_SUB_KLD, SI_ORDER_SECOND, link_elf_init, 0);

static int
parse_module_symbols(linker_file_t lf)
{
    elf_file_t ef = lf->priv;
    caddr_t	pointer;
    caddr_t	ssym, esym, base;
    caddr_t	strtab;
    int		strcnt;
    Elf_Sym*	symtab;
    int		symcnt;

    if (ef->modptr == NULL)
	return 0;
    pointer = preload_search_info(ef->modptr, MODINFO_METADATA|MODINFOMD_SSYM);
    if (pointer == NULL)
	return 0;
    ssym = *(caddr_t *)pointer;
    pointer = preload_search_info(ef->modptr, MODINFO_METADATA|MODINFOMD_ESYM);
    if (pointer == NULL)
	return 0;
    esym = *(caddr_t *)pointer;

    base = ssym;

    symcnt = *(long *)base;
    base += sizeof(long);
    symtab = (Elf_Sym *)base;
    base += roundup(symcnt, sizeof(long));

    if (base > esym || base < ssym) {
	printf("Symbols are corrupt!\n");
	return EINVAL;
    }

    strcnt = *(long *)base;
    base += sizeof(long);
    strtab = base;
    base += roundup(strcnt, sizeof(long));

    if (base > esym || base < ssym) {
	printf("Symbols are corrupt!\n");
	return EINVAL;
    }

    ef->ddbsymtab = symtab;
    ef->ddbsymcnt = symcnt / sizeof(Elf_Sym);
    ef->ddbstrtab = strtab;
    ef->ddbstrcnt = strcnt;

    return 0;
}

static int
parse_dynamic(linker_file_t lf)
{
    elf_file_t ef = lf->priv;
    const Elf_Dyn *dp;
    int plttype = DT_REL;

    for (dp = ef->dynamic; dp->d_tag != DT_NULL; dp++) {
	switch (dp->d_tag) {
	case DT_HASH:
	{
	    /* From src/libexec/rtld-elf/rtld.c */
	    const Elf_Off *hashtab = (const Elf_Off *)
		(ef->address + dp->d_un.d_ptr);
	    ef->nbuckets = hashtab[0];
	    ef->nchains = hashtab[1];
	    ef->buckets = hashtab + 2;
	    ef->chains = ef->buckets + ef->nbuckets;
	    break;
	}
	case DT_STRTAB:
	    ef->strtab = (caddr_t) (ef->address + dp->d_un.d_ptr);
	    break;
	case DT_STRSZ:
	    ef->strsz = dp->d_un.d_val;
	    break;
	case DT_SYMTAB:
	    ef->symtab = (Elf_Sym*) (ef->address + dp->d_un.d_ptr);
	    break;
	case DT_SYMENT:
	    if (dp->d_un.d_val != sizeof(Elf_Sym))
		return ENOEXEC;
	    break;
	case DT_PLTGOT:
	    ef->got = (Elf_Addr *) (ef->address + dp->d_un.d_ptr);
	    break;
	case DT_REL:
	    ef->rel = (const Elf_Rel *) (ef->address + dp->d_un.d_ptr);
	    break;
	case DT_RELSZ:
	    ef->relsize = dp->d_un.d_val;
	    break;
	case DT_RELENT:
	    if (dp->d_un.d_val != sizeof(Elf_Rel))
		return ENOEXEC;
	    break;
	case DT_JMPREL:
	    ef->pltrel = (const Elf_Rel *) (ef->address + dp->d_un.d_ptr);
	    break;
	case DT_PLTRELSZ:
	    ef->pltrelsize = dp->d_un.d_val;
	    break;
	case DT_RELA:
	    ef->rela = (const Elf_Rela *) (ef->address + dp->d_un.d_ptr);
	    break;
	case DT_RELASZ:
	    ef->relasize = dp->d_un.d_val;
	    break;
	case DT_RELAENT:
	    if (dp->d_un.d_val != sizeof(Elf_Rela))
		return ENOEXEC;
	    break;
	case DT_PLTREL:
	    plttype = dp->d_un.d_val;
	    if (plttype != DT_REL && plttype != DT_RELA)
		return ENOEXEC;
	    break;
	}
    }

    if (plttype == DT_RELA) {
	ef->pltrela = (const Elf_Rela *) ef->pltrel;
	ef->pltrel = NULL;
	ef->pltrelasize = ef->pltrelsize;
	ef->pltrelsize = 0;
    }

    ef->ddbsymtab = ef->symtab;
    ef->ddbsymcnt = ef->nchains;
    ef->ddbstrtab = ef->strtab;
    ef->ddbstrcnt = ef->strsz;

    return 0;
}

static void
link_elf_error(const char *s)
{
    printf("kldload: %s\n", s);
}

static int
link_elf_load_module(const char *filename, linker_file_t *result)
{
    caddr_t		modptr, baseptr, sizeptr, dynptr;
    char		*type;
    elf_file_t		ef;
    linker_file_t	lf;
    int			error;
    vm_offset_t		dp;

    /* Look to see if we have the module preloaded */
    modptr = preload_search_by_name(filename);
    if (modptr == NULL)
	return (link_elf_load_file(filename, result));

    /* It's preloaded, check we can handle it and collect information */
    type = (char *)preload_search_info(modptr, MODINFO_TYPE);
    baseptr = preload_search_info(modptr, MODINFO_ADDR);
    sizeptr = preload_search_info(modptr, MODINFO_SIZE);
    dynptr = preload_search_info(modptr, MODINFO_METADATA|MODINFOMD_DYNAMIC);
    if (type == NULL || strcmp(type, "elf module") != 0)
	return (EFTYPE);
    if (baseptr == NULL || sizeptr == NULL || dynptr == NULL)
	return (EINVAL);

    ef = malloc(sizeof(struct elf_file), M_LINKER, M_WAITOK);
    if (ef == NULL)
	return (ENOMEM);
    bzero(ef, sizeof(*ef));
    ef->modptr = modptr;
    ef->address = *(caddr_t *)baseptr;
#ifdef SPARSE_MAPPING
    ef->object = 0;
#endif
    dp = (vm_offset_t)ef->address + *(vm_offset_t *)dynptr;
    ef->dynamic = (Elf_Dyn *)dp;
    lf = linker_make_file(filename, ef, &link_elf_module_ops);
    if (lf == NULL) {
	free(ef, M_LINKER);
	return ENOMEM;
    }
    lf->address = ef->address;
    lf->size = *(size_t *)sizeptr;

    error = parse_dynamic(lf);
    if (error) {
	linker_file_unload(lf);
	return error;
    }
    error = load_dependancies(lf);
    if (error) {
	linker_file_unload(lf);
	return error;
    }
    error = relocate_file(lf);
    if (error) {
	linker_file_unload(lf);
	return error;
    }
    (void)parse_module_symbols(lf);
    lf->flags |= LINKER_FILE_LINKED;
    *result = lf;
    return (0);
}

static int
link_elf_load_file(const char* filename, linker_file_t* result)
{
    struct nameidata nd;
    struct proc* p = curproc;	/* XXX */
    Elf_Ehdr *hdr;
    caddr_t firstpage;
    int nbytes, i;
    Elf_Phdr *phdr;
    Elf_Phdr *phlimit;
    Elf_Phdr *segs[2];
    int nsegs;
    Elf_Phdr *phdyn;
    Elf_Phdr *phphdr;
    caddr_t mapbase;
    size_t mapsize;
    Elf_Off base_offset;
    Elf_Addr base_vaddr;
    Elf_Addr base_vlimit;
    int error = 0;
    int resid;
    elf_file_t ef;
    linker_file_t lf;
    char *pathname;
    Elf_Shdr *shdr;
    int symtabindex;
    int symstrindex;
    int symcnt;
    int strcnt;

    shdr = NULL;
    lf = NULL;

    pathname = linker_search_path(filename);
    if (pathname == NULL)
	return ENOENT;

    NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, pathname, p);
    error = vn_open(&nd, FREAD, 0);
    free(pathname, M_LINKER);
    if (error)
	return error;
    NDFREE(&nd, NDF_ONLY_PNBUF);

    /*
     * Read the elf header from the file.
     */
    firstpage = malloc(PAGE_SIZE, M_LINKER, M_WAITOK);
    if (firstpage == NULL) {
	error = ENOMEM;
	goto out;
    }
    hdr = (Elf_Ehdr *)firstpage;
    error = vn_rdwr(UIO_READ, nd.ni_vp, firstpage, PAGE_SIZE, 0,
		    UIO_SYSSPACE, IO_NODELOCKED, p->p_ucred, &resid, p);
    nbytes = PAGE_SIZE - resid;
    if (error)
	goto out;

    if (!IS_ELF(*hdr)) {
	error = ENOEXEC;
	goto out;
    }

    if (hdr->e_ident[EI_CLASS] != ELF_TARG_CLASS
      || hdr->e_ident[EI_DATA] != ELF_TARG_DATA) {
	link_elf_error("Unsupported file layout");
	error = ENOEXEC;
	goto out;
    }
    if (hdr->e_ident[EI_VERSION] != EV_CURRENT
      || hdr->e_version != EV_CURRENT) {
	link_elf_error("Unsupported file version");
	error = ENOEXEC;
	goto out;
    }
    if (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN) {
	link_elf_error("Unsupported file type");
	error = ENOEXEC;
	goto out;
    }
    if (hdr->e_machine != ELF_TARG_MACH) {
	link_elf_error("Unsupported machine");
	error = ENOEXEC;
	goto out;
    }

    /*
     * We rely on the program header being in the first page.  This is
     * not strictly required by the ABI specification, but it seems to
     * always true in practice.  And, it simplifies things considerably.
     */
    if (!((hdr->e_phentsize == sizeof(Elf_Phdr)) &&
	  (hdr->e_phoff + hdr->e_phnum*sizeof(Elf_Phdr) <= PAGE_SIZE) &&
	  (hdr->e_phoff + hdr->e_phnum*sizeof(Elf_Phdr) <= nbytes)))
	link_elf_error("Unreadable program headers");

    /*
     * Scan the program header entries, and save key information.
     *
     * We rely on there being exactly two load segments, text and data,
     * in that order.
     */
    phdr = (Elf_Phdr *) (firstpage + hdr->e_phoff);
    phlimit = phdr + hdr->e_phnum;
    nsegs = 0;
    phdyn = NULL;
    phphdr = NULL;
    while (phdr < phlimit) {
	switch (phdr->p_type) {

	case PT_LOAD:
	    if (nsegs == 2) {
		link_elf_error("Too many sections");
		error = ENOEXEC;
		goto out;
	    }
	    segs[nsegs] = phdr;
	    ++nsegs;
	    break;

	case PT_PHDR:
	    phphdr = phdr;
	    break;

	case PT_DYNAMIC:
	    phdyn = phdr;
	    break;
	}

	++phdr;
    }
    if (phdyn == NULL) {
	link_elf_error("Object is not dynamically-linked");
	error = ENOEXEC;
	goto out;
    }

    /*
     * Allocate the entire address space of the object, to stake out our
     * contiguous region, and to establish the base address for relocation.
     */
    base_offset = trunc_page(segs[0]->p_offset);
    base_vaddr = trunc_page(segs[0]->p_vaddr);
    base_vlimit = round_page(segs[1]->p_vaddr + segs[1]->p_memsz);
    mapsize = base_vlimit - base_vaddr;

    ef = malloc(sizeof(struct elf_file), M_LINKER, M_WAITOK);
    bzero(ef, sizeof(*ef));
#ifdef SPARSE_MAPPING
    ef->object = vm_object_allocate(OBJT_DEFAULT, mapsize >> PAGE_SHIFT);
    if (ef->object == NULL) {
	free(ef, M_LINKER);
	error = ENOMEM;
	goto out;
    }
    vm_object_reference(ef->object);
    ef->address = (caddr_t) vm_map_min(kernel_map);
    error = vm_map_find(kernel_map, ef->object, 0,
			(vm_offset_t *) &ef->address,
			mapsize, 1,
			VM_PROT_ALL, VM_PROT_ALL, 0);
    if (error) {
	vm_object_deallocate(ef->object);
	free(ef, M_LINKER);
	goto out;
    }
#else
    ef->address = malloc(mapsize, M_LINKER, M_WAITOK);
#endif
    mapbase = ef->address;

    /*
     * Read the text and data sections and zero the bss.
     */
    for (i = 0; i < 2; i++) {
	caddr_t segbase = mapbase + segs[i]->p_vaddr - base_vaddr;
	error = vn_rdwr(UIO_READ, nd.ni_vp,
			segbase, segs[i]->p_filesz, segs[i]->p_offset,
			UIO_SYSSPACE, IO_NODELOCKED, p->p_ucred, &resid, p);
	if (error) {
#ifdef SPARSE_MAPPING
	    vm_map_remove(kernel_map, (vm_offset_t) ef->address,
			  (vm_offset_t) ef->address
			  + (ef->object->size << PAGE_SHIFT));
	    vm_object_deallocate(ef->object);
#else
	    free(ef->address, M_LINKER);
#endif
	    free(ef, M_LINKER);
	    goto out;
	}
	bzero(segbase + segs[i]->p_filesz,
	      segs[i]->p_memsz - segs[i]->p_filesz);

#ifdef SPARSE_MAPPING
	/*
	 * Wire down the pages
	 */
	vm_map_pageable(kernel_map,
			(vm_offset_t) segbase,
			(vm_offset_t) segbase + segs[i]->p_memsz,
			FALSE);
#endif
    }

    ef->dynamic = (const Elf_Dyn *) (mapbase + phdyn->p_vaddr - base_vaddr);

    lf = linker_make_file(filename, ef, &link_elf_file_ops);
    if (lf == NULL) {
#ifdef SPARSE_MAPPING
	vm_map_remove(kernel_map, (vm_offset_t) ef->address,
		      (vm_offset_t) ef->address
		      + (ef->object->size << PAGE_SHIFT));
	vm_object_deallocate(ef->object);
#else
	free(ef->address, M_LINKER);
#endif
	free(ef, M_LINKER);
	error = ENOMEM;
	goto out;
    }
    lf->address = ef->address;
    lf->size = mapsize;

    error = parse_dynamic(lf);
    if (error)
	goto out;
    error = load_dependancies(lf);
    if (error)
	goto out;
    error = relocate_file(lf);
    if (error)
	goto out;

    /* Try and load the symbol table if it's present.  (you can strip it!) */
    nbytes = hdr->e_shnum * hdr->e_shentsize;
    if (nbytes == 0 || hdr->e_shoff == 0)
	goto nosyms;
    shdr = malloc(nbytes, M_LINKER, M_WAITOK);
    if (shdr == NULL) {
	error = ENOMEM;
	goto out;
    }
    bzero(shdr, nbytes);
    error = vn_rdwr(UIO_READ, nd.ni_vp,
		    (caddr_t)shdr, nbytes, hdr->e_shoff,
		    UIO_SYSSPACE, IO_NODELOCKED, p->p_ucred, &resid, p);
    if (error)
	goto out;
    symtabindex = -1;
    symstrindex = -1;
    for (i = 0; i < hdr->e_shnum; i++) {
	if (shdr[i].sh_type == SHT_SYMTAB) {
	    symtabindex = i;
	    symstrindex = shdr[i].sh_link;
	}
    }
    if (symtabindex < 0 || symstrindex < 0)
	goto nosyms;

    symcnt = shdr[symtabindex].sh_size;
    ef->symbase = malloc(symcnt, M_LINKER, M_WAITOK);
    strcnt = shdr[symstrindex].sh_size;
    ef->strbase = malloc(strcnt, M_LINKER, M_WAITOK);

    if (ef->symbase == NULL || ef->strbase == NULL) {
	error = ENOMEM;
	goto out;
    }
    error = vn_rdwr(UIO_READ, nd.ni_vp,
		    ef->symbase, symcnt, shdr[symtabindex].sh_offset,
		    UIO_SYSSPACE, IO_NODELOCKED, p->p_ucred, &resid, p);
    if (error)
	goto out;
    error = vn_rdwr(UIO_READ, nd.ni_vp,
		    ef->strbase, strcnt, shdr[symstrindex].sh_offset,
		    UIO_SYSSPACE, IO_NODELOCKED, p->p_ucred, &resid, p);
    if (error)
	goto out;

    ef->ddbsymcnt = symcnt / sizeof(Elf_Sym);
    ef->ddbsymtab = (const Elf_Sym *)ef->symbase;
    ef->ddbstrcnt = strcnt;
    ef->ddbstrtab = ef->strbase;

    lf->flags |= LINKER_FILE_LINKED;

nosyms:

    *result = lf;

out:
    if (error && lf)
	linker_file_unload(lf);
    if (shdr)
	free(shdr, M_LINKER);
    if (firstpage)
	free(firstpage, M_LINKER);
    VOP_UNLOCK(nd.ni_vp, 0, p);
    vn_close(nd.ni_vp, FREAD, p->p_ucred, p);

    return error;
}

static void
link_elf_unload_file(linker_file_t file)
{
    elf_file_t ef = file->priv;

    if (ef) {
#ifdef SPARSE_MAPPING
	if (ef->object) {
	    vm_map_remove(kernel_map, (vm_offset_t) ef->address,
			  (vm_offset_t) ef->address
			  + (ef->object->size << PAGE_SHIFT));
	    vm_object_deallocate(ef->object);
	}
#else
	if (ef->address)
	    free(ef->address, M_LINKER);
#endif
	if (ef->symbase)
	    free(ef->symbase, M_LINKER);
	if (ef->strbase)
	    free(ef->strbase, M_LINKER);
	free(ef, M_LINKER);
    }
}

static void
link_elf_unload_module(linker_file_t file)
{
    elf_file_t ef = file->priv;

    if (ef)
	free(ef, M_LINKER);
    if (file->filename)
	preload_delete_name(file->filename);
}

static int
load_dependancies(linker_file_t lf)
{
    elf_file_t ef = lf->priv;
    linker_file_t lfdep;
    char* name;
    const Elf_Dyn *dp;
    int error = 0;

    /*
     * All files are dependant on /kernel.
     */
    if (linker_kernel_file) {
	linker_kernel_file->refs++;
	linker_file_add_dependancy(lf, linker_kernel_file);
    }

    for (dp = ef->dynamic; dp->d_tag != DT_NULL; dp++) {
	if (dp->d_tag == DT_NEEDED) {
	    name = ef->strtab + dp->d_un.d_val;

	    error = linker_load_file(name, &lfdep);
	    if (error)
		goto out;
	    error = linker_file_add_dependancy(lf, lfdep);
	    if (error)
		goto out;
	}
    }

out:
    return error;
}

static const char *
symbol_name(elf_file_t ef, Elf_Word r_info)
{
    const Elf_Sym *ref;

    if (ELF_R_SYM(r_info)) {
	ref = ef->symtab + ELF_R_SYM(r_info);
	return ef->strtab + ref->st_name;
    } else
	return NULL;
}

static int
relocate_file(linker_file_t lf)
{
    elf_file_t ef = lf->priv;
    const Elf_Rel *rellim;
    const Elf_Rel *rel;
    const Elf_Rela *relalim;
    const Elf_Rela *rela;
    const char *symname;

    /* Perform relocations without addend if there are any: */
    rel = ef->rel;
    if (rel) {
	rellim = (const Elf_Rel *)((const char *)ef->rel + ef->relsize);
	while (rel < rellim) {
	    symname = symbol_name(ef, rel->r_info);
	    if (elf_reloc(lf, rel, ELF_RELOC_REL, symname)) {
		printf("link_elf: symbol %s undefined\n", symname);
		return ENOENT;
	    }
	    rel++;
	}
    }

    /* Perform relocations with addend if there are any: */
    rela = ef->rela;
    if (rela) {
	relalim = (const Elf_Rela *)((const char *)ef->rela + ef->relasize);
	while (rela < relalim) {
	    symname = symbol_name(ef, rela->r_info);
	    if (elf_reloc(lf, rela, ELF_RELOC_RELA, symname)) {
		printf("link_elf: symbol %s undefined\n", symname);
		return ENOENT;
	    }
	    rela++;
	}
    }

    /* Perform PLT relocations without addend if there are any: */
    rel = ef->pltrel;
    if (rel) {
	rellim = (const Elf_Rel *)((const char *)ef->pltrel + ef->pltrelsize);
	while (rel < rellim) {
	    symname = symbol_name(ef, rel->r_info);
	    if (elf_reloc(lf, rel, ELF_RELOC_REL, symname)) {
		printf("link_elf: symbol %s undefined\n", symname);
		return ENOENT;
	    }
	    rel++;
	}
    }

    /* Perform relocations with addend if there are any: */
    rela = ef->pltrela;
    if (rela) {
	relalim = (const Elf_Rela *)((const char *)ef->pltrela + ef->pltrelasize);
	while (rela < relalim) {
	    symname = symbol_name(ef, rela->r_info);
	    if (elf_reloc(lf, rela, ELF_RELOC_RELA, symname)) {
		printf("link_elf: symbol %s undefined\n", symname);
		return ENOENT;
	    }
	    rela++;
	}
    }

    return 0;
}

/*
 * Hash function for symbol table lookup.  Don't even think about changing
 * this.  It is specified by the System V ABI.
 */
static unsigned long
elf_hash(const char *name)
{
    const unsigned char *p = (const unsigned char *) name;
    unsigned long h = 0;
    unsigned long g;

    while (*p != '\0') {
	h = (h << 4) + *p++;
	if ((g = h & 0xf0000000) != 0)
	    h ^= g >> 24;
	h &= ~g;
    }
    return h;
}

int
link_elf_lookup_symbol(linker_file_t lf, const char* name, c_linker_sym_t* sym)
{
    elf_file_t ef = lf->priv;
    unsigned long symnum;
    const Elf_Sym* symp;
    const char *strp;
    unsigned long hash;
    int i;

    /* First, search hashed global symbols */
    hash = elf_hash(name);
    symnum = ef->buckets[hash % ef->nbuckets];

    while (symnum != STN_UNDEF) {
	if (symnum >= ef->nchains) {
	    printf("link_elf_lookup_symbol: corrupt symbol table\n");
	    return ENOENT;
	}

	symp = ef->symtab + symnum;
	if (symp->st_name == 0) {
	    printf("link_elf_lookup_symbol: corrupt symbol table\n");
	    return ENOENT;
	}

	strp = ef->strtab + symp->st_name;

	if (strcmp(name, strp) == 0) {
	    if (symp->st_shndx != SHN_UNDEF ||
		(symp->st_value != 0 &&
		 ELF_ST_TYPE(symp->st_info) == STT_FUNC)) {
		*sym = (c_linker_sym_t) symp;
		return 0;
	    } else
		return ENOENT;
	}

	symnum = ef->chains[symnum];
    }

    /* If we have not found it, look at the full table (if loaded) */
    if (ef->symtab == ef->ddbsymtab)
	return ENOENT;

    /* Exhaustive search */
    for (i = 0, symp = ef->ddbsymtab; i < ef->ddbsymcnt; i++, symp++) {
	strp = ef->ddbstrtab + symp->st_name;
	if (strcmp(name, strp) == 0) {
	    if (symp->st_shndx != SHN_UNDEF ||
		(symp->st_value != 0 &&
		 ELF_ST_TYPE(symp->st_info) == STT_FUNC)) {
		*sym = (c_linker_sym_t) symp;
		return 0;
	    } else
		return ENOENT;
	}
    }

    return ENOENT;
}

static int
link_elf_symbol_values(linker_file_t lf, c_linker_sym_t sym, linker_symval_t* symval)
{
	elf_file_t ef = lf->priv;
	const Elf_Sym* es = (const Elf_Sym*) sym;

	if (es >= ef->symtab && ((es - ef->symtab) < ef->nchains)) {
	    symval->name = ef->strtab + es->st_name;
	    symval->value = (caddr_t) ef->address + es->st_value;
	    symval->size = es->st_size;
	    return 0;
	}
	if (ef->symtab == ef->ddbsymtab)
	    return ENOENT;
	if (es >= ef->ddbsymtab && ((es - ef->ddbsymtab) < ef->ddbsymcnt)) {
	    symval->name = ef->ddbstrtab + es->st_name;
	    symval->value = (caddr_t) ef->address + es->st_value;
	    symval->size = es->st_size;
	    return 0;
	}
	return ENOENT;
}

static int
link_elf_search_symbol(linker_file_t lf, caddr_t value,
		       c_linker_sym_t* sym, long* diffp)
{
	elf_file_t ef = lf->priv;
	u_long off = (uintptr_t) (void *) value;
	u_long diff = off;
	u_long st_value;
	const Elf_Sym* es;
	const Elf_Sym* best = 0;
	int i;

	for (i = 0, es = ef->ddbsymtab; i < ef->ddbsymcnt; i++, es++) {
		if (es->st_name == 0)
			continue;
		st_value = es->st_value + (uintptr_t) (void *) ef->address;
		if (off >= st_value) {
			if (off - st_value < diff) {
				diff = off - st_value;
				best = es;
				if (diff == 0)
					break;
			} else if (off - st_value == diff) {
				best = es;
			}
		}
	}
	if (best == 0)
		*diffp = off;
	else
		*diffp = diff;
	*sym = (c_linker_sym_t) best;

	return 0;
}
