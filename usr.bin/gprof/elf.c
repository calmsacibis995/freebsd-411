#ifndef lint
static const char rcsid[] =
  "$FreeBSD: src/usr.bin/gprof/elf.c,v 1.2.2.1 2001/07/11 23:59:11 obrien Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <machine/elf.h>

#include <err.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "gprof.h"

static bool wantsym(const Elf_Sym *, const char *);

/* Things which get -E excluded by default. */
static char	*excludes[] = { ".mcount", "_mcleanup", NULL };

int
elf_getnfile(const char *filename, char ***defaultEs)
{
    int fd;
    Elf_Ehdr h;
    struct stat s;
    void *mapbase;
    const char *base;
    const Elf_Shdr *shdrs;
    const Elf_Shdr *sh_symtab;
    const Elf_Shdr *sh_strtab;
    const char *strtab;
    const Elf_Sym *symtab;
    int symtabct;
    int i;

    if ((fd = open(filename, O_RDONLY)) == -1)
	err(1, "%s", filename);
    if (read(fd, &h, sizeof h) != sizeof h || !IS_ELF(h)) {
	close(fd);
	return -1;
    }
    if (fstat(fd, &s) == -1)
	err(1, "Cannot fstat %s", filename);
    if ((mapbase = mmap(0, s.st_size, PROT_READ, MAP_SHARED, fd, 0)) ==
      MAP_FAILED)
	err(1, "Cannot mmap %s", filename);
    close(fd);

    base = (const char *)mapbase;
    shdrs = (const Elf_Shdr *)(base + h.e_shoff);

    /* Find the symbol table and associated string table section. */
    for (i = 1;  i < h.e_shnum;  i++)
	if (shdrs[i].sh_type == SHT_SYMTAB)
	    break;
    if (i == h.e_shnum)
	errx(1, "%s has no symbol table", filename);
    sh_symtab = &shdrs[i];
    sh_strtab = &shdrs[sh_symtab->sh_link];

    symtab = (const Elf_Sym *)(base + sh_symtab->sh_offset);
    symtabct = sh_symtab->sh_size / sh_symtab->sh_entsize;
    strtab = (const char *)(base + sh_strtab->sh_offset);

    /* Count the symbols that we're interested in. */
    nname = 0;
    for (i = 1;  i < symtabct;  i++)
	if (wantsym(&symtab[i], strtab))
	    nname++;

    /* Allocate memory for them, plus a terminating entry. */
    if ((nl = (nltype *)calloc(nname + 1, sizeof(nltype))) == NULL)
	errx(1, "Insufficient memory for symbol table");

    /* Read them in. */
    npe = nl;
    for (i = 1;  i < symtabct;  i++) {
	const Elf_Sym *sym = &symtab[i];

	if (wantsym(sym, strtab)) {
	    npe->value = sym->st_value;
	    npe->name = strtab + sym->st_name;
	    npe++;
	}
    }
    npe->value = -1;

    *defaultEs = excludes;
    return 0;
}

static bool
wantsym(const Elf_Sym *sym, const char *strtab)
{
    int type;
    int bind;

    type = ELF_ST_TYPE(sym->st_info);
    bind = ELF_ST_BIND(sym->st_info);

    if (type != STT_FUNC ||
      (aflag && bind == STB_LOCAL) ||
      (uflag && strchr(strtab + sym->st_name, '.') != NULL))
	return 0;

    return 1;
}
