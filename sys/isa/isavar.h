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
 * $FreeBSD: src/sys/isa/isavar.h,v 1.16.2.2 2000/10/29 13:07:56 nyan Exp $
 */

#ifndef _ISA_ISAVAR_H_
#define _ISA_ISAVAR_H_

struct isa_config;
struct isa_pnp_id;
typedef void isa_config_cb(void *arg, struct isa_config *config, int enable);

#include "isa_if.h"
#include <isa/pnpvar.h>

#ifdef _KERNEL

/*
 * ISA devices are partially ordered to ensure that devices which are
 * sensitive to other driver probe routines are probed first. Plug and
 * Play devices are added after devices with speculative probes so that
 * the legacy hardware can claim resources allowing the Plug and Play
 * hardware to choose different resources.
 */
#define ISA_ORDER_SENSITIVE	0 /* legacy sensitive hardware */
#define ISA_ORDER_SPECULATIVE	1 /* legacy non-sensitive hardware */
#define ISA_ORDER_PNP		2 /* plug-and-play hardware */

#define	ISA_NPORT	50
#define	ISA_NMEM	8
#define	ISA_NIRQ	2
#define	ISA_NDRQ	2

#define ISADMA_READ	0x00100000
#define ISADMA_WRITE	0
#define ISADMA_RAW	0x00080000
/*
 * Plug and play cards can support a range of resource
 * configurations. This structure is used by the isapnp parser to
 * inform the isa bus about the resource possibilities of the
 * device. Each different alternative should be supplied by calling
 * ISA_ADD_CONFIG().
 */
struct isa_range {
	u_int32_t		ir_start;
	u_int32_t		ir_end;
	u_int32_t		ir_size;
	u_int32_t		ir_align;
};

struct isa_config {
	struct isa_range	ic_mem[ISA_NMEM];
	struct isa_range	ic_port[ISA_NPORT];
	u_int32_t		ic_irqmask[ISA_NIRQ];
	u_int32_t		ic_drqmask[ISA_NDRQ];
	int			ic_nmem;
	int			ic_nport;
	int			ic_nirq;
	int			ic_ndrq;
};

/*
 * Used to build lists of IDs and description strings for PnP drivers.
 */
struct isa_pnp_id {
	u_int32_t		ip_id;
	const char		*ip_desc;
};

enum isa_device_ivars {
	ISA_IVAR_PORT,
	ISA_IVAR_PORT_0 = ISA_IVAR_PORT,
	ISA_IVAR_PORT_1,
	ISA_IVAR_PORTSIZE,
	ISA_IVAR_PORTSIZE_0 = ISA_IVAR_PORTSIZE,
	ISA_IVAR_PORTSIZE_1,
	ISA_IVAR_MADDR,
	ISA_IVAR_MADDR_0 = ISA_IVAR_MADDR,
	ISA_IVAR_MADDR_1,
	ISA_IVAR_MSIZE,
	ISA_IVAR_MSIZE_0 = ISA_IVAR_MSIZE,
	ISA_IVAR_MSIZE_1,
	ISA_IVAR_IRQ,
	ISA_IVAR_IRQ_0 = ISA_IVAR_IRQ,
	ISA_IVAR_IRQ_1,
	ISA_IVAR_DRQ,
	ISA_IVAR_DRQ_0 = ISA_IVAR_DRQ,
	ISA_IVAR_DRQ_1,
	ISA_IVAR_VENDORID,
	ISA_IVAR_SERIAL,
	ISA_IVAR_LOGICALID,
	ISA_IVAR_COMPATID
};

/*
 * Simplified accessors for isa devices
 */
#define ISA_ACCESSOR(A, B, T)						\
									\
static __inline T isa_get_ ## A(device_t dev)				\
{									\
	uintptr_t v;							\
	BUS_READ_IVAR(device_get_parent(dev), dev, ISA_IVAR_ ## B, &v);	\
	return (T) v;							\
}									\
									\
static __inline void isa_set_ ## A(device_t dev, T t)			\
{									\
	u_long v = (u_long) t;						\
	BUS_WRITE_IVAR(device_get_parent(dev), dev, ISA_IVAR_ ## B, v);	\
}

ISA_ACCESSOR(port, PORT, int)
ISA_ACCESSOR(portsize, PORTSIZE, int)
ISA_ACCESSOR(irq, IRQ, int)
ISA_ACCESSOR(drq, DRQ, int)
ISA_ACCESSOR(maddr, MADDR, int)
ISA_ACCESSOR(msize, MSIZE, int)
ISA_ACCESSOR(vendorid, VENDORID, int)
ISA_ACCESSOR(serial, SERIAL, int)
ISA_ACCESSOR(logicalid, LOGICALID, int)
ISA_ACCESSOR(compatid, COMPATID, int)

extern intrmask_t isa_irq_pending(void);
extern intrmask_t isa_irq_mask(void);
#ifdef __i386__
extern void	 isa_wrap_old_drivers(void);
#endif
extern void	isa_probe_children(device_t dev);

extern void	isa_dmacascade __P((int chan));
extern void	isa_dmadone __P((int flags, caddr_t addr, int nbytes, int chan));
extern void	isa_dmainit __P((int chan, u_int bouncebufsize));
extern void	isa_dmastart __P((int flags, caddr_t addr, u_int nbytes, int chan));
extern int	isa_dma_acquire __P((int chan));
extern void	isa_dma_release __P((int chan));
extern int	isa_dmastatus __P((int chan));
extern int	isa_dmastop __P((int chan));

#ifdef PC98
#include <machine/bus.h>

/*
 * Allocate discontinuous resources for ISA bus.
 */
struct resource *
isa_alloc_resourcev(device_t child, int type, int *rid,
		    bus_addr_t *res, bus_size_t count, u_int flags);
int
isa_load_resourcev(struct resource *re, bus_addr_t *res, bus_size_t count);
#endif

#endif /* _KERNEL */

#endif /* !_ISA_ISAVAR_H_ */
