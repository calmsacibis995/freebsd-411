/*-
 * Copyright (c) 1998,1999,2000,2001,2002 S�ren Schmidt <sos@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * $FreeBSD: src/sys/dev/ata/ata-card.c,v 1.4.2.1 2002/03/18 08:37:33 sos Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/disk.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/devicestat.h>
#include <sys/sysctl.h>
#include <machine/stdarg.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <dev/ata/ata-all.h>

static int
ata_pccard_probe(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);
    struct resource *io;
    int rid, len, start, end;
    u_long tmp;

    /* allocate the io range to get start and length */
    rid = ATA_IOADDR_RID;
    len = bus_get_resource_count(dev, SYS_RES_IOPORT, rid);
    io = bus_alloc_resource(dev, SYS_RES_IOPORT, &rid, 0, ~0,
			    ATA_IOSIZE, RF_ACTIVE);
    if (!io)
	return ENOMEM;

    /* reallocate the io address to only cover the io ports */
    start = rman_get_start(io);
    end = start + ATA_IOSIZE - 1;
    bus_release_resource(dev, SYS_RES_IOPORT, rid, io);
    io = bus_alloc_resource(dev, SYS_RES_IOPORT, &rid,
			    start, end, ATA_IOSIZE, RF_ACTIVE);
    bus_release_resource(dev, SYS_RES_IOPORT, rid, io);

    /* 
     * if we got more than the default ATA_IOSIZE ports, this is likely
     * a pccard system where the altio ports are located at offset 14
     * otherwise its the normal altio offset
     */
    if (bus_get_resource(dev, SYS_RES_IOPORT, ATA_ALTADDR_RID, &tmp, &tmp)) {
	if (len > ATA_IOSIZE) {
	    bus_set_resource(dev, SYS_RES_IOPORT, ATA_ALTADDR_RID,
			     start + ATA_PCCARD_ALTOFFSET, ATA_ALTIOSIZE);
	}
	else {
	    bus_set_resource(dev, SYS_RES_IOPORT, ATA_ALTADDR_RID, 
			     start + ATA_ALTOFFSET, ATA_ALTIOSIZE);
	}
    }
    else
	return ENOMEM;

    ch->unit = 0;
    ch->flags |= (ATA_USE_16BIT | ATA_NO_SLAVE);
    return ata_probe(dev);
}

static device_method_t ata_pccard_methods[] = {
    /* device interface */
    DEVMETHOD(device_probe,	ata_pccard_probe),
    DEVMETHOD(device_attach,	ata_attach),
    DEVMETHOD(device_detach,	ata_detach),
    { 0, 0 }
};

static driver_t ata_pccard_driver = {
    "ata",
    ata_pccard_methods,
    sizeof(struct ata_channel),
};

DRIVER_MODULE(ata, pccard, ata_pccard_driver, ata_devclass, 0, 0);
