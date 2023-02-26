/*-
 * Copyright (c) 2002 Mitsaru Iwasaki
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
 *	$FreeBSD: src/sys/dev/acpica/Osd/OsdTable.c,v 1.3.4.1 2003/08/22 20:49:21 jhb Exp $
 */

/*
 * ACPI Table interfaces
 */

#include "acpi.h"

#include <sys/kernel.h>
#include <sys/linker.h>

#undef _COMPONENT
#define _COMPONENT      ACPI_TABLES

static char acpi_os_name[128];

ACPI_STATUS
AcpiOsPredefinedOverride (
    const ACPI_PREDEFINED_NAMES *InitVal,
    ACPI_STRING                 *NewVal)
{
    if (InitVal == NULL || NewVal == NULL)
        return(AE_BAD_PARAMETER);

    *NewVal = NULL;
    if (strncmp(InitVal->Name, "_OS_", 4) == 0 &&
      getenv_string("hw.acpi.os_name", acpi_os_name, sizeof(acpi_os_name))) {
        printf("ACPI: Overriding _OS definition with \"%s\"\n", acpi_os_name);
        *NewVal = acpi_os_name;
    }

    return(AE_OK);
}

ACPI_STATUS
AcpiOsTableOverride (
    ACPI_TABLE_HEADER       *ExistingTable,
    ACPI_TABLE_HEADER       **NewTable)
{
    caddr_t                 acpi_dsdt, p;

    if (ExistingTable == NULL || NewTable == NULL)
    {
        return(AE_BAD_PARAMETER);
    }

    (*NewTable) = NULL;

    if (strncmp(ExistingTable->Signature, "DSDT", 4) != 0)
    {
        return(AE_OK);
    }

    if ((acpi_dsdt = preload_search_by_type("acpi_dsdt")) == NULL)
    {
        return(AE_OK);
    }
        
    if ((p = preload_search_info(acpi_dsdt, MODINFO_ADDR)) == NULL)
    {
        return(AE_OK);
    }

    (*NewTable) = *(void **)p;

    printf("ACPI: DSDT was overridden.\n");

    return(AE_OK);
}

