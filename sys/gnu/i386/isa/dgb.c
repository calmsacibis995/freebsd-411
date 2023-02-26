/*-
 *  dgb.c $FreeBSD: src/sys/gnu/i386/isa/dgb.c,v 1.56.2.1 2001/02/26 04:23:09 jlemon Exp $
 *
 *  Digiboard driver.
 *
 *  Stage 1. "Better than nothing".
 *  Stage 2. "Gee, it works!".
 *
 *  Based on sio driver by Bruce Evans and on Linux driver by Troy 
 *  De Jongh <troyd@digibd.com> or <troyd@skypoint.com> 
 *  which is under GNU General Public License version 2 so this driver 
 *  is forced to be under GPL 2 too.
 *
 *  Written by Serge Babkin,
 *      Joint Stock Commercial Bank "Chelindbank"
 *      (Chelyabinsk, Russia)
 *      babkin@hq.icb.chel.su
 *
 *  Assorted hacks to make it more functional and working under 3.0-current.
 *  Fixed broken routines to prevent processes hanging on closed (thanks
 *  to Bruce for his patience and assistance). Thanks also to Maxim Bolotin
 *  <max@run.net> for his patches which did most of the work to get this
 *  running under 2.2/3.0-current.
 *  Implemented ioctls: TIOCMSDTRWAIT, TIOCMGDTRWAIT, TIOCTIMESTAMP &
 *  TIOCDCDTIMESTAMP.
 *  Sysctl debug flag is now a bitflag, to filter noise during debugging.
 *	David L. Nugent <davidn@blaze.net.au>
 */

#include "opt_compat.h"
#include "opt_dgb.h"

#include "dgb.h"

/* Helg: i.e.25 times per sec board will be polled */
#define POLLSPERSEC 25
/* How many charactes can we write to input tty rawq */
#define DGB_IBUFSIZE (TTYHOG-100)

/* the overall number of ports controlled by this driver */

#ifndef NDGBPORTS
#	define NDGBPORTS (NDGB*16)
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/proc.h>
#include <sys/conf.h>
#include <sys/dkstat.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <machine/clock.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <i386/isa/isa_device.h>

#include <gnu/i386/isa/dgbios.h>
#include <gnu/i386/isa/dgfep.h>

#define DGB_DEBUG		/* Enable debugging info via sysctl */
#include <gnu/i386/isa/dgreg.h>

#define	CALLOUT_MASK		0x80
#define	CONTROL_MASK		0x60
#define	CONTROL_INIT_STATE	0x20
#define	CONTROL_LOCK_STATE	0x40
#define UNIT_MASK			0x30000
#define PORT_MASK			0x1F
#define	DEV_TO_UNIT(dev)	(MINOR_TO_UNIT(minor(dev)))
#define	MINOR_MAGIC_MASK	(CALLOUT_MASK | CONTROL_MASK)
#define	MINOR_TO_UNIT(mynor)	(((mynor) & UNIT_MASK)>>16)
#define MINOR_TO_PORT(mynor)	((mynor) & PORT_MASK)

/* types.  XXX - should be elsewhere */
typedef u_char	bool_t;		/* boolean */

/* digiboard port structure */
struct dgb_p {
	bool_t	status;

	u_char unit;           /* board unit number */
	u_char pnum;           /* port number */
	u_char omodem;         /* FEP output modem status     */
	u_char imodem;         /* FEP input modem status      */
	u_char modemfake;      /* Modem values to be forced   */
	u_char modem;          /* Force values                */
	u_char hflow;
	u_char dsr;
	u_char dcd;
	u_char stopc;
	u_char startc;
	u_char stopca;
	u_char startca;
	u_char fepstopc;
	u_char fepstartc;
	u_char fepstopca;
	u_char fepstartca;
	u_char txwin;
	u_char rxwin;
	ushort fepiflag;
	ushort fepcflag;
	ushort fepoflag;
	ushort txbufhead;
	ushort txbufsize;
	ushort rxbufhead;
	ushort rxbufsize;
	int close_delay;
	int count;
	int blocked_open;
	int event;
	int asyncflags;
	u_long statusflags;
	u_char *txptr;
	u_char *rxptr;
	volatile struct board_chan *brdchan;
	struct tty *tty;

	bool_t  active_out;	/* nonzero if the callout device is open */
	u_int	wopeners;	/* # processes waiting for DCD in open() */

	/* Initial state. */
	struct termios	it_in;	/* should be in struct tty */
	struct termios	it_out;

	/* Lock state. */
	struct termios	lt_in;	/* should be in struct tty */
	struct termios	lt_out;

	bool_t	do_timestamp;
	bool_t	do_dcd_timestamp;
	struct timeval	timestamp;
	struct timeval	dcd_timestamp;

	/* flags of state, are used in sleep() too */
	u_char closing;	/* port is being closed now */
	u_char draining; /* port is being drained now */
	u_char used;	/* port is being used now */
	u_char mustdrain; /* data must be waited to drain in dgbparam() */
};

/* Digiboard per-board structure */
struct dgb_softc {
	/* struct board_info */
	u_char status;	/* status: DISABLED/ENABLED */
	u_char unit;	/* unit number */
	u_char type;	/* type of card: PCXE, PCXI, PCXEVE */
	u_char altpin;	/* do we need alternate pin setting ? */
	int numports;	/* number of ports on card */
	int port;	/* I/O port */
	u_char *vmem; /* virtual memory address */
	long pmem; /* physical memory address */
	int mem_seg;  /* internal memory segment */
	struct dgb_p *ports;	/* pointer to array of port descriptors */
	struct tty *ttys;	/* pointer to array of TTY structures */
	volatile struct global_data *mailbox;
	};
	

static struct dgb_softc dgb_softc[NDGB];
static struct dgb_p dgb_ports[NDGBPORTS];
static struct tty dgb_tty[NDGBPORTS];

/*
 * The public functions in the com module ought to be declared in a com-driver
 * system header.
 */

/* Interrupt handling entry points. */
static void	dgbpoll		__P((void *unit_c));

/* Device switch entry points. */
#define	dgbreset	noreset
#define	dgbmmap		nommap
#define	dgbstrategy	nostrategy

static	int	dgbattach	__P((struct isa_device *dev));
static	int	dgbprobe	__P((struct isa_device *dev));

static void fepcmd(struct dgb_p *port, unsigned cmd, unsigned op1, unsigned op2,
	unsigned ncmds, unsigned bytecmd);

static	void	dgbstart	__P((struct tty *tp));
static	void	dgbstop		__P((struct tty *tp, int rw));
static	int	dgbparam	__P((struct tty *tp, struct termios *t));
static	void	dgbhardclose	__P((struct dgb_p *port));
static	void	dgb_drain_or_flush	__P((struct dgb_p *port));
static	int	dgbdrain	__P((struct dgb_p *port));
static	void	dgb_pause	__P((void *chan));
static	void	wakeflush	__P((void *p));
static	void	disc_optim	__P((struct tty	*tp, struct termios *t));


struct isa_driver	dgbdriver = {
	dgbprobe, dgbattach, "dgb",0
};

static	d_open_t	dgbopen;
static	d_close_t	dgbclose;
static	d_ioctl_t	dgbioctl;

#define	CDEV_MAJOR	58
static struct cdevsw dgb_cdevsw = {
	/* open */	dgbopen,
	/* close */	dgbclose,
	/* read */	ttyread,
	/* write */	ttywrite,
	/* ioctl */	dgbioctl,
	/* poll */	ttypoll,
	/* mmap */	nommap,
	/* strategy */	nostrategy,
	/* name */	"dgb",
	/* maj */	CDEV_MAJOR,
	/* dump */	nodump,
	/* psize */	nopsize,
	/* flags */	D_TTY | D_KQFILTER,
	/* bmaj */	-1,
	/* kqfilter */	ttykqfilter,
};

static	speed_t	dgbdefaultrate = TTYDEF_SPEED;

static	struct speedtab dgbspeedtab[] = {
	0,	FEP_B0, /* old (sysV-like) Bx codes */
	50,	FEP_B50,
	75,	FEP_B75,
	110,	FEP_B110,
	134,	FEP_B134,
	150,	FEP_B150,
	200,	FEP_B200,
	300,	FEP_B300,
	600,	FEP_B600,
	1200,	FEP_B1200,
	1800,	FEP_B1800,
	2400,	FEP_B2400,
	4800,	FEP_B4800,
	9600,	FEP_B9600,
	19200,	FEP_B19200,
	38400,	FEP_B38400,
	57600,	(FEP_FASTBAUD|FEP_B50),	/* B50 & fast baud table */
	115200, (FEP_FASTBAUD|FEP_B110), /* B100 & fast baud table */
	-1,	-1
};

static struct dbgflagtbl
{
  tcflag_t in_mask;
  tcflag_t in_val;
  tcflag_t out_val;
} dgb_cflags[] =
{
  { PARODD,   PARODD,	  FEP_PARODD  },
  { PARENB,   PARENB,	  FEP_PARENB  },
  { CSTOPB,   CSTOPB,	  FEP_CSTOPB  },
  { CSIZE,    CS5,	  FEP_CS6     },
  { CSIZE,    CS6,	  FEP_CS6     },
  { CSIZE,    CS7,	  FEP_CS7     },
  { CSIZE,    CS8,	  FEP_CS8     },
  { CLOCAL,   CLOCAL,	  FEP_CLOCAL  },
  { (tcflag_t)-1 }
}, dgb_iflags[] =
{
  { IGNBRK,   IGNBRK,     FEP_IGNBRK  },
  { BRKINT,   BRKINT,	  FEP_BRKINT  },
  { IGNPAR,   IGNPAR,	  FEP_IGNPAR  },
  { PARMRK,   PARMRK,	  FEP_PARMRK  },
  { INPCK,    INPCK,	  FEP_INPCK   },
  { ISTRIP,   ISTRIP,	  FEP_ISTRIP  },
  { IXON,     IXON,	  FEP_IXON    },
  { IXOFF,    IXOFF,	  FEP_IXOFF   },
  { IXANY,    IXANY,	  FEP_IXANY   },
  { (tcflag_t)-1 }
}, dgb_flow[] =
{
  { CRTSCTS,  CRTSCTS,	  CTS|RTS     },
  { CRTSCTS,  CCTS_OFLOW, CTS	      },
  { CRTSCTS,  CRTS_IFLOW, RTS	      },
  { (tcflag_t)-1 }
};

/* xlat bsd termios flags to dgb sys-v style */
static tcflag_t
dgbflags(struct dbgflagtbl *tbl, tcflag_t input)
{
  tcflag_t output = 0;
  int i;

  for (i=0; tbl[i].in_mask != (tcflag_t)-1; i++)
  {
    if ((input & tbl[i].in_mask) == tbl[i].in_val)
      output |= tbl[i].out_val;
  }
  return output;
}

#ifdef DGB_DEBUG
static int dgbdebug=0;
SYSCTL_INT(_debug, OID_AUTO, dgb_debug, CTLFLAG_RW, &dgbdebug, 0, "");
#endif

static __inline int setwin __P((struct dgb_softc *sc, unsigned addr));
static __inline int setinitwin __P((struct dgb_softc *sc, unsigned addr));
static __inline void hidewin __P((struct dgb_softc *sc));
static __inline void towin __P((struct dgb_softc *sc, int win));

/*Helg: to allow recursive dgb...() calls */
typedef struct
  {                 /* If we were called and don't want to disturb we need: */
	int port;		/* write to this port */
	u_char data;		/* this data on exit */
	                  /* or DATA_WINOFF  to close memory window on entry */
  } BoardMemWinState; /* so several channels and even boards can coexist */
#define DATA_WINOFF 0
static BoardMemWinState bmws;

/* return current memory window state and close window */
static BoardMemWinState
bmws_get(void)
{
	BoardMemWinState bmwsRet=bmws;
	if(bmws.data!=DATA_WINOFF)
		outb(bmws.port, bmws.data=DATA_WINOFF);
	return bmwsRet;
}

/* restore memory window state */
static void
bmws_set(BoardMemWinState ws)
{
	if(ws.data != bmws.data || ws.port!=bmws.port ) {
		if(bmws.data!=DATA_WINOFF)
			outb(bmws.port,DATA_WINOFF);
		if(ws.data!=DATA_WINOFF)
			outb(ws.port, ws.data);
		bmws=ws;
	}
}

static __inline int 
setwin(sc,addr)
	struct dgb_softc *sc;
	unsigned int addr;
{
	if(sc->type==PCXEVE) {
		outb(bmws.port=sc->port+1, bmws.data=FEPWIN|(addr>>13));
		DPRINT3(DB_WIN,"dgb%d: switched to window 0x%x\n",sc->unit,addr>>13);
		return (addr & 0x1FFF);
	} else {
		outb(bmws.port=sc->port,bmws.data=FEPMEM);
		return addr;
	}
}

static __inline int 
setinitwin(sc,addr)
	struct dgb_softc *sc;
	unsigned int addr;
{
	if(sc->type==PCXEVE) {
		outb(bmws.port=sc->port+1, bmws.data=FEPWIN|(addr>>13));
		DPRINT3(DB_WIN,"dgb%d: switched to window 0x%x\n",sc->unit,addr>>13);
		return (addr & 0x1FFF);
	} else {
		outb(bmws.port=sc->port,bmws.data=inb(sc->port)|FEPMEM);
		return addr;
	}
}

static __inline void
hidewin(sc)
	struct dgb_softc *sc;
{
	bmws.data=0;
	if(sc->type==PCXEVE)
		outb(bmws.port=sc->port+1, bmws.data);
	else
		outb(bmws.port=sc->port, bmws.data);
}

static __inline void
towin(sc,win)
	struct dgb_softc *sc;
	int win;
{
	if(sc->type==PCXEVE) {
		outb(bmws.port=sc->port+1, bmws.data=win);
	} else {
		outb(bmws.port=sc->port,bmws.data=FEPMEM);
	}
}

static int
dgbprobe(dev)
	struct isa_device	*dev;
{
	struct dgb_softc *sc= &dgb_softc[dev->id_unit];
	int i, v;
	u_long win_size;  /* size of vizible memory window */
	int unit=dev->id_unit;
	static int once;

	if (!once++)
		cdevsw_add(&dgb_cdevsw);
	sc->unit=dev->id_unit;
	sc->port=dev->id_iobase;

	if(dev->id_flags & DGBFLAG_ALTPIN)
		sc->altpin=1;
	else
		sc->altpin=0;

	/* left 24 bits only (ISA address) */
	sc->pmem=((intptr_t)(void *)dev->id_maddr & 0xFFFFFF); 
	
	DPRINT4(DB_INFO,"dgb%d: port 0x%x mem 0x%lx\n",unit,sc->port,sc->pmem);

	outb(sc->port, FEPRST);
	sc->status=DISABLED;

	for(i=0; i< 1000; i++) {
		DELAY(1);
		if( (inb(sc->port) & FEPMASK) == FEPRST ) {
			sc->status=ENABLED;
			DPRINT3(DB_EXCEPT,"dgb%d: got reset after %d us\n",unit,i);
			break;
		}
	}

	if(sc->status!=ENABLED) {
		DPRINT2(DB_EXCEPT,"dgb%d: failed to respond\n",dev->id_unit);
		return 0;
	}

	/* check type of card and get internal memory characteristics */

	v=inb(sc->port);

	if( v & 0x1 ) {
		switch( v&0x30 ) {
		case 0:
			sc->mem_seg=0xF000;
			win_size=0x10000;
			printf("dgb%d: PC/Xi 64K\n",dev->id_unit);
			break;
		case 0x10:
			sc->mem_seg=0xE000;
			win_size=0x20000;
			printf("dgb%d: PC/Xi 128K\n",dev->id_unit);
			break;
		case 0x20:
			sc->mem_seg=0xC000;
			win_size=0x40000;
			printf("dgb%d: PC/Xi 256K\n",dev->id_unit);
			break;
		default: /* case 0x30: */
			sc->mem_seg=0x8000;
			win_size=0x80000;
			printf("dgb%d: PC/Xi 512K\n",dev->id_unit);
			break;
		}
		sc->type=PCXI;
	} else {
		outb(sc->port, 1);
		v=inb(sc->port);

		if( v & 0x1 ) {
			printf("dgb%d: PC/Xm isn't supported\n",dev->id_unit);
			sc->status=DISABLED;
			return 0;
			}

		sc->mem_seg=0xF000;

		if(dev->id_flags==DGBFLAG_NOWIN || ( v&0xC0 )==0) {
			win_size=0x10000;
			printf("dgb%d: PC/Xe 64K\n",dev->id_unit);
			sc->type=PCXE;
		} else {
			win_size=0x2000;
			printf("dgb%d: PC/Xe 64/8K (windowed)\n",dev->id_unit);
			sc->type=PCXEVE;
			if((u_long)sc->pmem & ~0xFFE000) {
				printf("dgb%d: warning: address 0x%lx truncated to 0x%lx\n",
					dev->id_unit, sc->pmem,
					sc->pmem & 0xFFE000);

				dev->id_maddr= (u_char *)(void *)(intptr_t)( sc->pmem & 0xFFE000 );
			}
		}
	}

	/* save size of vizible memory segment */
	dev->id_msize=win_size;

	/* map memory */
	dev->id_maddr=sc->vmem=pmap_mapdev(sc->pmem,dev->id_msize);

	outb(sc->port, FEPCLR); /* drop RESET */
	hidewin(sc); /* Helg: to set initial bmws state */

	return 4; /* we need I/O space of 4 ports */
}

static int
dgbattach(dev)
	struct isa_device	*dev;
{
	int unit=dev->id_unit;
	struct dgb_softc *sc= &dgb_softc[dev->id_unit];
	int i, t;
	u_char volatile *mem;
	u_char volatile *ptr;
	int addr;
	struct dgb_p *port;
	volatile struct board_chan *bc;
	int shrinkmem;
	int nfails;
	ushort *pstat;
	int lowwater;
	static int nports=0;
	char suffix;

	if(sc->status!=ENABLED) {
		DPRINT2(DB_EXCEPT,"dbg%d: try to attach a disabled card\n",unit);
		return 0;
		}

	mem=sc->vmem;

	DPRINT3(DB_INFO,"dgb%d: internal memory segment 0x%x\n",unit,sc->mem_seg);

	outb(sc->port, FEPRST); DELAY(1);

	for(i=0; (inb(sc->port) & FEPMASK) != FEPRST ; i++) {
		if(i>10000) {
			printf("dgb%d: 1st reset failed\n",dev->id_unit);
			sc->status=DISABLED;
			hidewin(sc);
			return 0;
		}
		DELAY(1);
	}

	DPRINT3(DB_INFO,"dgb%d: got reset after %d us\n",unit,i);

	/* for PCXEVE set up interrupt and base address */

	if(sc->type==PCXEVE) {
		t=(((u_long)sc->pmem>>8) & 0xFFE0) | 0x10 /* enable windowing */;
		/* IRQ isn't used */
		outb(sc->port+2,t & 0xFF);
		outb(sc->port+3,t>>8);
	} else if(sc->type==PCXE) {
		t=(((u_long)sc->pmem>>8) & 0xFFE0) /* disable windowing */;
		outb(sc->port+2,t & 0xFF);
		outb(sc->port+3,t>>8);
	}


	if(sc->type==PCXI || sc->type==PCXE) {
		outb(sc->port, FEPRST|FEPMEM); DELAY(1);

		for(i=0; (inb(sc->port) & FEPMASK) != (FEPRST|FEPMEM) ; i++) {
			if(i>10000) {
				printf("dgb%d: 2nd reset failed\n",dev->id_unit);
				sc->status=DISABLED;
				hidewin(sc);
				return 0;
			}
			DELAY(1);
		}

		DPRINT3(DB_INFO,"dgb%d: got memory after %d us\n",unit,i);
	}

	mem=sc->vmem;

	/* very short memory test */

	addr=setinitwin(sc,BOTWIN);
	*(u_long volatile *)(mem+addr) = 0xA55A3CC3;
	if(*(u_long volatile *)(mem+addr)!=0xA55A3CC3) {
		printf("dgb%d: 1st memory test failed\n",dev->id_unit);
		sc->status=DISABLED;
		hidewin(sc);
		return 0;
	}
		
	addr=setinitwin(sc,TOPWIN);
	*(u_long volatile *)(mem+addr) = 0x5AA5C33C;
	if(*(u_long volatile *)(mem+addr)!=0x5AA5C33C) {
		printf("dgb%d: 2nd memory test failed\n",dev->id_unit);
		sc->status=DISABLED;
		hidewin(sc);
		return 0;
	}
		
	addr=setinitwin(sc,BIOSCODE+((0xF000-sc->mem_seg)<<4));
	*(u_long volatile *)(mem+addr) = 0x5AA5C33C;
	if(*(u_long volatile *)(mem+addr)!=0x5AA5C33C) {
		printf("dgb%d: 3rd (BIOS) memory test failed\n",dev->id_unit);
	}
		
	addr=setinitwin(sc,MISCGLOBAL);
	for(i=0; i<16; i++) {
		mem[addr+i]=0;
	}

	if(sc->type==PCXI || sc->type==PCXE) {

		addr=BIOSCODE+((0xF000-sc->mem_seg)<<4);

		DPRINT3(DB_INFO,"dgb%d: BIOS local address=0x%x\n",unit,addr);

		ptr= mem+addr;

		for(i=0; i<pcxx_nbios; i++, ptr++)
			*ptr = pcxx_bios[i];

		ptr= mem+addr;

		nfails=0;
		for(i=0; i<pcxx_nbios; i++, ptr++)
			if( *ptr != pcxx_bios[i] ) {
				DPRINT5(DB_EXCEPT,"dgb%d: wrong code in BIOS at addr 0x%x : \
0x%x instead of 0x%x\n", unit, ptr-(mem+addr), *ptr, pcxx_bios[i] );

				if(++nfails>=5) {
					printf("dgb%d: 4th memory test (BIOS load) fails\n",unit);
					break;
					}
				}

		outb(sc->port,FEPMEM);

		for(i=0; (inb(sc->port) & FEPMASK) != FEPMEM ; i++) {
			if(i>10000) {
				printf("dgb%d: BIOS start failed\n",dev->id_unit);
				sc->status=DISABLED;
				hidewin(sc);
				return 0;
			}
			DELAY(1);
		}

		DPRINT3(DB_INFO,"dgb%d: reset dropped after %d us\n",unit,i);

		for(i=0; i<200000; i++) {
			if( *((ushort volatile *)(mem+MISCGLOBAL)) == *((ushort *)"GD") )
				goto load_fep;
			DELAY(1);
		}
		printf("dgb%d: BIOS download failed\n",dev->id_unit);
		DPRINT4(DB_EXCEPT,"dgb%d: code=0x%x must be 0x%x\n",
			dev->id_unit,
			*((ushort volatile *)(mem+MISCGLOBAL)),
			*((ushort *)"GD"));

		sc->status=DISABLED;
		hidewin(sc);
		return 0;
	}

	if(sc->type==PCXEVE) {
		/* set window 7 */
		outb(sc->port+1,0xFF);

		ptr= mem+(BIOSCODE & 0x1FFF);

		for(i=0; i<pcxx_nbios; i++)
			*ptr++ = pcxx_bios[i];

		ptr= mem+(BIOSCODE & 0x1FFF);

		nfails=0;
		for(i=0; i<pcxx_nbios; i++, ptr++)
			if( *ptr != pcxx_bios[i] ) {
				DPRINT5(DB_EXCEPT,"dgb%d: wrong code in BIOS at addr 0x%x : \
0x%x instead of 0x%x\n", unit, ptr-(mem+addr), *ptr, pcxx_bios[i] );

				if(++nfails>=5) {
					printf("dgb%d: 4th memory test (BIOS load) fails\n",unit);
					break;
					}
				}

		outb(sc->port,FEPCLR);

		setwin(sc,0);

		for(i=0; (inb(sc->port) & FEPMASK) != FEPCLR ; i++) {
			if(i>10000) {
				printf("dgb%d: BIOS start failed\n",dev->id_unit);
				sc->status=DISABLED;
				hidewin(sc);
				return 0;
			}
			DELAY(1);
		}

		DPRINT3(DB_INFO,"dgb%d: reset dropped after %d us\n",unit,i);

		addr=setwin(sc,MISCGLOBAL);

		for(i=0; i<200000; i++) {
			if(*(ushort volatile *)(mem+addr)== *(ushort *)"GD")
				goto load_fep;
			DELAY(1);
		}
		printf("dgb%d: BIOS download failed\n",dev->id_unit);
		DPRINT5(DB_EXCEPT,"dgb%d: Error#(0x%x,0x%x) code=0x%x\n",
			dev->id_unit,
			*(ushort volatile *)(mem+0xC12),
			*(ushort volatile *)(mem+0xC14),
			*(ushort volatile *)(mem+MISCGLOBAL));

		sc->status=DISABLED;
		hidewin(sc);
		return 0;
	}

load_fep:
	DPRINT2(DB_INFO,"dgb%d: BIOS loaded\n",dev->id_unit);

	addr=setwin(sc,FEPCODE);

	ptr= mem+addr;

	for(i=0; i<pcxx_ncook; i++)
		*ptr++ = pcxx_cook[i];

	addr=setwin(sc,MBOX);
	*(ushort volatile *)(mem+addr+ 0)=2;
	*(ushort volatile *)(mem+addr+ 2)=sc->mem_seg+FEPCODESEG;
	*(ushort volatile *)(mem+addr+ 4)=0;
	*(ushort volatile *)(mem+addr+ 6)=FEPCODESEG;
	*(ushort volatile *)(mem+addr+ 8)=0;
	*(ushort volatile *)(mem+addr+10)=pcxx_ncook;

	outb(sc->port,FEPMEM|FEPINT); /* send interrupt to BIOS */
	outb(sc->port,FEPMEM);

	for(i=0; *(ushort volatile *)(mem+addr)!=0; i++) {
		if(i>200000) {
			printf("dgb%d: FEP code download failed\n",unit);
			DPRINT3(DB_EXCEPT,"dgb%d: code=0x%x must be 0\n", unit,
				*(ushort volatile *)(mem+addr));
			sc->status=DISABLED;
			hidewin(sc);
			return 0;
		}
	}

	DPRINT2(DB_INFO,"dgb%d: FEP code loaded\n",unit);

	*(ushort volatile *)(mem+setwin(sc,FEPSTAT))=0;
	addr=setwin(sc,MBOX);
	*(ushort volatile *)(mem+addr+0)=1;
	*(ushort volatile *)(mem+addr+2)=FEPCODESEG;
	*(ushort volatile *)(mem+addr+4)=0x4;

	outb(sc->port,FEPINT); /* send interrupt to BIOS */
	outb(sc->port,FEPCLR);

	addr=setwin(sc,FEPSTAT);
	for(i=0; *(ushort volatile *)(mem+addr)!= *(ushort *)"OS"; i++) {
		if(i>200000) {
			printf("dgb%d: FEP/OS start failed\n",dev->id_unit);
			sc->status=DISABLED;
			hidewin(sc);
			return 0;
		}
	}

	DPRINT2(DB_INFO,"dgb%d: FEP/OS started\n",dev->id_unit);

	sc->numports= *(ushort volatile *)(mem+setwin(sc,NPORT));

	printf("dgb%d: %d ports\n",unit,sc->numports);

	if(sc->numports > MAX_DGB_PORTS) {
		printf("dgb%d: too many ports\n",unit);
		sc->status=DISABLED;
		hidewin(sc);
		return 0;
	}

	if(nports+sc->numports>NDGBPORTS) {
		printf("dgb%d: only %d ports are usable\n", unit, NDGBPORTS-nports);
		sc->numports=NDGBPORTS-nports;
	}

	/* allocate port and tty structures */
	sc->ports=&dgb_ports[nports];
	sc->ttys=&dgb_tty[nports];
	nports+=sc->numports;

	addr=setwin(sc,PORTBASE);
	pstat=(ushort volatile *)(mem+addr);

	for(i=0; i<sc->numports && pstat[i]; i++)
		if(pstat[i])
			sc->ports[i].status=ENABLED;
		else {
			sc->ports[i].status=DISABLED;
			printf("dgb%d: port%d is broken\n", unit, i);
		}

	/* We should now init per-port structures */
	bc=(volatile struct board_chan *)(mem + CHANSTRUCT);
	sc->mailbox=(volatile struct global_data *)(mem + FEP_GLOBAL);

	if(sc->numports<3)
		shrinkmem=1;
	else
		shrinkmem=0;

	for(i=0; i<sc->numports; i++, bc++) {
		port= &sc->ports[i];

		port->tty=&sc->ttys[i];
		port->unit=unit;

		port->brdchan=bc;

		if(sc->altpin) {
			port->dsr=CD;
			port->dcd=DSR;
		} else {
			port->dcd=CD;
			port->dsr=DSR;
		}

		port->pnum=i;

		if(shrinkmem) {
			DPRINT2(DB_INFO,"dgb%d: shrinking memory\n",unit);
			fepcmd(port, SETBUFFER, 32, 0, 0, 0);
			shrinkmem=0;
			}

		if(sc->type!=PCXEVE) {
			port->txptr=mem+((bc->tseg-sc->mem_seg)<<4);
			port->rxptr=mem+((bc->rseg-sc->mem_seg)<<4);
			port->txwin=port->rxwin=0;
		} else {
			port->txptr=mem+( ((bc->tseg-sc->mem_seg)<<4) & 0x1FFF );
			port->rxptr=mem+( ((bc->rseg-sc->mem_seg)<<4) & 0x1FFF );
			port->txwin=FEPWIN | ((bc->tseg-sc->mem_seg)>>9);
			port->rxwin=FEPWIN | ((bc->rseg-sc->mem_seg)>>9);
		}

		port->txbufhead=0;
		port->rxbufhead=0;
		port->txbufsize=bc->tmax+1;
		port->rxbufsize=bc->rmax+1;

		lowwater= (port->txbufsize>=2000) ? 1024 : (port->txbufsize/2);
		setwin(sc,0);
		fepcmd(port, STXLWATER, lowwater, 0, 10, 0);
		fepcmd(port, SRXLWATER, port->rxbufsize/4, 0, 10, 0);
		fepcmd(port, SRXHWATER, 3*port->rxbufsize/4, 0, 10, 0);

		bc->edelay=100;
		bc->idata=1;

		port->startc=bc->startc;
		port->startca=bc->startca;
		port->stopc=bc->stopc;
		port->stopca=bc->stopca;
			
		/*port->close_delay=50;*/
		port->close_delay=3 * hz;
		port->do_timestamp=0;
		port->do_dcd_timestamp=0;

		/*
		 * We don't use all the flags from <sys/ttydefaults.h> since they
		 * are only relevant for logins.  It's important to have echo off
		 * initially so that the line doesn't start blathering before the
		 * echo flag can be turned off.
		 */
		port->it_in.c_iflag = TTYDEF_IFLAG;
		port->it_in.c_oflag = TTYDEF_OFLAG;
		port->it_in.c_cflag = TTYDEF_CFLAG;
		port->it_in.c_lflag = TTYDEF_LFLAG;
		termioschars(&port->it_in);
		port->it_in.c_ispeed = port->it_in.c_ospeed = dgbdefaultrate;
		port->it_out = port->it_in;
		/* MAX_DGB_PORTS is 32 => [0-9a-v] */
		suffix = i < 10 ? '0' + i : 'a' + i - 10;
		make_dev(&dgb_cdevsw, (unit*32)+i,
		    UID_ROOT, GID_WHEEL, 0600, "ttyD%d%c", unit, suffix);

		make_dev(&dgb_cdevsw, (unit*32)+i+32,
		    UID_ROOT, GID_WHEEL, 0600, "ttyiD%d%c", unit, suffix);

		make_dev(&dgb_cdevsw, (unit*32)+i+64,
		    UID_ROOT, GID_WHEEL, 0600, "ttylD%d%c", unit, suffix);

		make_dev(&dgb_cdevsw, (unit*32)+i+128,
		    UID_UUCP, GID_DIALER, 0660, "cuaD%d%c", unit, suffix);

		make_dev(&dgb_cdevsw, (unit*32)+i+160,
		    UID_UUCP, GID_DIALER, 0660, "cuaiD%d%c", unit, suffix);

		make_dev(&dgb_cdevsw, (unit*32)+i+192,
		    UID_UUCP, GID_DIALER, 0660, "cualD%d%c", unit, suffix);
	}

	hidewin(sc);

	/* register the polling function */
	timeout(dgbpoll, (void *)unit, hz/POLLSPERSEC);

	return 1;
}

/* ARGSUSED */
static	int
dgbopen(dev, flag, mode, p)
	dev_t		dev;
	int		flag;
	int		mode;
	struct proc	*p;
{
	struct dgb_softc *sc;
	struct tty *tp;
	int unit;
	int mynor;
	int pnum;
	struct dgb_p *port;
	int s,cs;
	int error;
	volatile struct board_chan *bc;

	error=0;

	mynor=minor(dev);
	unit=MINOR_TO_UNIT(mynor);
	pnum=MINOR_TO_PORT(mynor);

	if(unit >= NDGB) {
		DPRINT2(DB_EXCEPT,"dgb%d: try to open a nonexisting card\n",unit);
		return ENXIO;
	}

	sc=&dgb_softc[unit];

	if(sc->status!=ENABLED) {
		DPRINT2(DB_EXCEPT,"dgb%d: try to open a disabled card\n",unit);
		return ENXIO;
	}

	if(pnum>=sc->numports) {
		DPRINT3(DB_EXCEPT,"dgb%d: try to open non-existing port %d\n",unit,pnum);
		return ENXIO;
	}

	if(mynor & CONTROL_MASK)
		return 0;

	tp=&sc->ttys[pnum];
	dev->si_tty = tp;
	port=&sc->ports[pnum];
	bc=port->brdchan;

open_top:
	
	s=spltty();

	while(port->closing) {
		error=tsleep(&port->closing, TTOPRI|PCATCH, "dgocl", 0);

		if(error) {
			DPRINT4(DB_OPEN,"dgb%d: port%d: tsleep(dgocl) error=%d\n",unit,pnum,error);
			goto out;
		}
	}

	if (tp->t_state & TS_ISOPEN) {
		/*
		 * The device is open, so everything has been initialized.
		 * Handle conflicts.
		 */
		if (mynor & CALLOUT_MASK) {
			if (!port->active_out) {
				error = EBUSY;
				DPRINT4(DB_OPEN,"dgb%d: port%d: BUSY error=%d\n",unit,pnum,error);
				goto out;
			}
		} else {
			if (port->active_out) {
				if (flag & O_NONBLOCK) {
					error = EBUSY;
					DPRINT4(DB_OPEN,"dgb%d: port%d: BUSY error=%d\n",unit,pnum,error);
					goto out;
				}
				error =	tsleep(&port->active_out,
					       TTIPRI | PCATCH, "dgbi", 0);
				if (error != 0) {
					DPRINT4(DB_OPEN,"dgb%d: port%d: tsleep(dgbi) error=%d\n",
						unit,pnum,error);
					goto out;
				}
				splx(s);
				goto open_top;
			}
		}
		if (tp->t_state & TS_XCLUDE &&
		    suser(p)) {
			error = EBUSY;
			goto out;
		}
	} else {
		/*
		 * The device isn't open, so there are no conflicts.
		 * Initialize it.  Initialization is done twice in many
		 * cases: to preempt sleeping callin opens if we are
		 * callout, and to complete a callin open after DCD rises.
		 */
		tp->t_oproc=dgbstart;
		tp->t_param=dgbparam;
		tp->t_stop=dgbstop;
		tp->t_dev=dev;
		tp->t_termios= (mynor & CALLOUT_MASK) ?
							port->it_out :
							port->it_in;

		cs=splclock();
		setwin(sc,0);
		port->imodem=bc->mstat;
		bc->rout=bc->rin; /* clear input queue */
		bc->idata=1;
#ifdef PRINT_BUFSIZE
		printf("dgb buffers tx=%x:%x rx=%x:%x\n",bc->tseg,bc->tmax,bc->rseg,bc->rmax);
#endif

		hidewin(sc);
		splx(cs);

		port->wopeners++;
		error=dgbparam(tp, &tp->t_termios);
		port->wopeners--;

		if(error!=0) {
			DPRINT4(DB_OPEN,"dgb%d: port%d: dgbparam error=%d\n",unit,pnum,error);
			goto out;
		}

		/* handle fake DCD for callout devices */
		/* and initial DCD */

		if( (port->imodem & port->dcd) || mynor & CALLOUT_MASK )
			linesw[tp->t_line].l_modem(tp,1);

	}

	/*
	 * Wait for DCD if necessary.
	 */
	if (!(tp->t_state & TS_CARR_ON) && !(mynor & CALLOUT_MASK)
	    && !(tp->t_cflag & CLOCAL) && !(flag & O_NONBLOCK)) {
		++port->wopeners;
		error = tsleep(TSA_CARR_ON(tp), TTIPRI | PCATCH, "dgdcd", 0);
		--port->wopeners;
		if (error != 0) {
			DPRINT4(DB_OPEN,"dgb%d: port%d: tsleep(dgdcd) error=%d\n",unit,pnum,error);
			goto out;
		}
		splx(s);
		goto open_top;
	}
	error =	linesw[tp->t_line].l_open(dev, tp);
	disc_optim(tp,&tp->t_termios);
	DPRINT4(DB_OPEN,"dgb%d: port%d: l_open error=%d\n",unit,pnum,error);

	if (tp->t_state & TS_ISOPEN && mynor & CALLOUT_MASK)
		port->active_out = TRUE;

	port->used=1;

	/* If any port is open (i.e. the open() call is completed for it) 
	 * the device is busy
	 */

out:
	disc_optim(tp,&tp->t_termios);
	splx(s);

	if( !(tp->t_state & TS_ISOPEN) && port->wopeners==0 )
		dgbhardclose(port);

	DPRINT4(DB_OPEN,"dgb%d: port%d: open() returns %d\n",unit,pnum,error);

	return error;
}

/*ARGSUSED*/
static	int
dgbclose(dev, flag, mode, p)
	dev_t		dev;
	int		flag;
	int		mode;
	struct proc	*p;
{
	int		mynor;
	struct tty	*tp;
	int unit, pnum;
	struct dgb_softc *sc;
	struct dgb_p *port;
	int s;
	int i;

	mynor=minor(dev);
	if(mynor & CONTROL_MASK)
		return 0;
	unit=MINOR_TO_UNIT(mynor);
	pnum=MINOR_TO_PORT(mynor);

	sc=&dgb_softc[unit];
	tp=&sc->ttys[pnum];
	port=sc->ports+pnum;

	DPRINT3(DB_CLOSE,"dgb%d: port%d: closing\n",unit,pnum);

	DPRINT3(DB_CLOSE,"dgb%d: port%d: draining port\n",unit,pnum);
        dgb_drain_or_flush(port);

	s=spltty();

	port->closing=1;
	DPRINT3(DB_CLOSE,"dgb%d: port%d: closing line disc\n",unit,pnum);
	linesw[tp->t_line].l_close(tp,flag);
	disc_optim(tp,&tp->t_termios);

	DPRINT3(DB_CLOSE,"dgb%d: port%d: hard closing\n",unit,pnum);
	dgbhardclose(port);
	DPRINT3(DB_CLOSE,"dgb%d: port%d: closing tty\n",unit,pnum);
	ttyclose(tp);
	port->closing=0;
	wakeup(&port->closing);
	port->used=0;

	/* mark the card idle when all ports are closed */

	for(i=0; i<sc->numports; i++)
		if(sc->ports[i].used)
			break;

	splx(s);

	DPRINT3(DB_CLOSE,"dgb%d: port%d: closed\n",unit,pnum);

	wakeup(TSA_CARR_ON(tp));
	wakeup(&port->active_out);
	port->active_out=0;

	DPRINT3(DB_CLOSE,"dgb%d: port%d: close exit\n",unit,pnum);

	return 0;
}

static void
dgbhardclose(port)
	struct dgb_p *port;
{
	struct dgb_softc *sc=&dgb_softc[port->unit];
	volatile struct board_chan *bc=port->brdchan;
	int cs;

	cs=splclock();
	port->do_timestamp = 0;
	setwin(sc,0);

	bc->idata=0; bc->iempty=0; bc->ilow=0;
	if(port->tty->t_cflag & HUPCL) {
		port->omodem &= ~(RTS|DTR);
		fepcmd(port, SETMODEM, 0, DTR|RTS, 0, 1);
	}

	hidewin(sc);
	splx(cs);

	timeout(dgb_pause, &port->brdchan, hz/2);
	tsleep(&port->brdchan, TTIPRI | PCATCH, "dgclo", 0);
}

static void 
dgb_pause(chan)
	void *chan;
{
	wakeup((caddr_t)chan);
}

static void
dgbpoll(unit_c)
	void *unit_c;
{
	int unit=(int)unit_c;
	int pnum;
	struct dgb_p *port;
	struct dgb_softc *sc=&dgb_softc[unit];
	int head, tail;
	u_char *eventbuf;
	int event, mstat, lstat;
	volatile struct board_chan *bc;
	struct tty *tp;
	int rhead, rtail;
	int whead, wtail;
	int size;
	u_char *ptr;
	int ocount;
	int ibuf_full,obuf_full;

	BoardMemWinState ws=bmws_get();

	if(sc->status==DISABLED) {
		printf("dgb%d: polling of disabled board stopped\n",unit);
		return;
	}
	
	setwin(sc,0);

	head=sc->mailbox->ein;
	tail=sc->mailbox->eout;

	while(head!=tail) {
		if(head >= FEP_IMAX-FEP_ISTART 
		|| tail >= FEP_IMAX-FEP_ISTART 
		|| (head|tail) & 03 ) {
			printf("dgb%d: event queue's head or tail is wrong! hd=%d,tl=%d\n", unit,head,tail);
			break;
		}

		eventbuf=sc->vmem+tail+FEP_ISTART;
		pnum=eventbuf[0];
		event=eventbuf[1];
		mstat=eventbuf[2];
		lstat=eventbuf[3];

		port=&sc->ports[pnum];
		bc=port->brdchan;
		tp=&sc->ttys[pnum];

		if(pnum>=sc->numports || port->status==DISABLED) {
			printf("dgb%d: port%d: got event on nonexisting port\n",unit,pnum);
		} else if(port->used || port->wopeners>0 ) {

			int wrapmask=port->rxbufsize-1;

			if( !(event & ALL_IND) ) 
				printf("dgb%d: port%d: ? event 0x%x mstat 0x%x lstat 0x%x\n",
					unit, pnum, event, mstat, lstat);

			if(event & DATA_IND) {
				DPRINT3(DB_DATA,"dgb%d: port%d: DATA_IND\n",unit,pnum);

				rhead=bc->rin & wrapmask; 
				rtail=bc->rout & wrapmask;

				if( !(tp->t_cflag & CREAD) || !port->used ) {
					bc->rout=rhead;
					goto end_of_data;
				}

				if(bc->orun) {
					printf("dgb%d: port%d: overrun\n", unit, pnum);
					bc->orun=0;
				}

				if(!(tp->t_state & TS_ISOPEN))
					goto end_of_data;

				for(ibuf_full=FALSE;rhead!=rtail && !ibuf_full;) {
					DPRINT5(DB_RXDATA,"dgb%d: port%d: p rx head=%d tail=%d\n",
						unit,pnum,rhead,rtail);

					if(rhead>rtail)
						size=rhead-rtail;
					else
						size=port->rxbufsize-rtail;

					ptr=port->rxptr+rtail;

/* Helg: */
					if( tp->t_rawq.c_cc + size > DGB_IBUFSIZE ) {
						size=DGB_IBUFSIZE-tp->t_rawq.c_cc;
						DPRINT1(DB_RXDATA,"*");
						ibuf_full=TRUE;
					}

					if(size) {
						if (tp->t_state & TS_CAN_BYPASS_L_RINT) {
							DPRINT1(DB_RXDATA,"!");
							towin(sc,port->rxwin);
							tk_nin += size;
							tk_rawcc += size;
							tp->t_rawcc += size;
							b_to_q(ptr,size,&tp->t_rawq);
							setwin(sc,0);
						} else {
							int i=size;
							unsigned char chr;
							do {
								towin(sc,port->rxwin);
								chr= *ptr++;
								hidewin(sc);
							       (*linesw[tp->t_line].l_rint)(chr, tp);
							} while (--i > 0 );
							setwin(sc,0);
						}
	 				}
					rtail= (rtail + size) & wrapmask;
					bc->rout=rtail;
					rhead=bc->rin & wrapmask;
					hidewin(sc);
					ttwakeup(tp);
					setwin(sc,0);
				}
			end_of_data: ;
			}

			if(event & MODEMCHG_IND) {
				DPRINT3(DB_MODEM,"dgb%d: port%d: MODEMCHG_IND\n",unit,pnum);
				port->imodem=mstat;
				if(mstat & port->dcd) {
					hidewin(sc);
					linesw[tp->t_line].l_modem(tp,1);
					setwin(sc,0);
					wakeup(TSA_CARR_ON(tp));
				} else {
					hidewin(sc);
					linesw[tp->t_line].l_modem(tp,0);
					setwin(sc,0);
					if( port->draining) {
						port->draining=0;
						wakeup(&port->draining);
					}
				}
			}

			if(event & BREAK_IND) {
				if((tp->t_state & TS_ISOPEN) && (tp->t_iflag & IGNBRK))	{
				        DPRINT3(DB_BREAK,"dgb%d: port%d: BREAK_IND\n",unit,pnum);
				        hidewin(sc);
				        linesw[tp->t_line].l_rint(TTY_BI, tp);
				        setwin(sc,0);
			        }
			}

/* Helg: with output flow control */

			if(event & (LOWTX_IND | EMPTYTX_IND) ) {
				DPRINT3(DB_TXDATA,"dgb%d: port%d: LOWTX_IND or EMPTYTX_IND\n",unit,pnum);

				if( (event & EMPTYTX_IND ) && tp->t_outq.c_cc==0
				&& port->draining) {
					port->draining=0;
					wakeup(&port->draining);
					bc->ilow=0; bc->iempty=0;
				} else {

					int wrapmask=port->txbufsize-1;

					for(obuf_full=FALSE; tp->t_outq.c_cc!=0 && !obuf_full; ) {
						int s;
						/* add "last-minute" data to write buffer */
						if(!(tp->t_state & TS_BUSY)) {
							hidewin(sc);
#ifndef TS_ASLEEP	/* post 2.0.5 FreeBSD */
					                ttwwakeup(tp);
#else
					                if(tp->t_outq.c_cc <= tp->t_lowat) {
						                if(tp->t_state & TS_ASLEEP) {
							                tp->t_state &= ~TS_ASLEEP;
							                wakeup(TSA_OLOWAT(tp));
						                }
						                /* selwakeup(&tp->t_wsel); */
					                }
#endif
					                setwin(sc,0);
				                }
						s=spltty();

					whead=bc->tin & wrapmask;
					wtail=bc->tout & wrapmask;

					if(whead<wtail)
						size=wtail-whead-1;
					else {
						size=port->txbufsize-whead;
						if(wtail==0)
							size--;
					}

					if(size==0) {
						DPRINT5(DB_WR,"dgb: head=%d tail=%d size=%d full=%d\n",
							whead,wtail,size,obuf_full);
						bc->iempty=1; bc->ilow=1;
						obuf_full=TRUE;
						splx(s);
						break;
					}

					towin(sc,port->txwin);

					ocount=q_to_b(&tp->t_outq, port->txptr+whead, size);
					whead+=ocount;

					setwin(sc,0);
					bc->tin=whead;
					bc->tin=whead & wrapmask;
					splx(s);
				}

				if(obuf_full) {
					DPRINT1(DB_WR," +BUSY\n");
					tp->t_state|=TS_BUSY;
				} else {
					DPRINT1(DB_WR," -BUSY\n");
					hidewin(sc);
#ifndef TS_ASLEEP	/* post 2.0.5 FreeBSD */
					/* should clear TS_BUSY before ttwwakeup */
					if(tp->t_state & TS_BUSY)	{
						tp->t_state &= ~TS_BUSY;
						linesw[tp->t_line].l_start(tp);
				                ttwwakeup(tp);
					}
#else
				if(tp->t_state & TS_ASLEEP) {
					tp->t_state &= ~TS_ASLEEP;
					wakeup(TSA_OLOWAT(tp));
				}
				tp->t_state &= ~TS_BUSY;
#endif
				        setwin(sc,0);
				        }
			        }
			}
			bc->idata=1;   /* require event on incoming data */ 

		} else {
			bc=port->brdchan;
			DPRINT4(DB_EXCEPT,"dgb%d: port%d: got event 0x%x on closed port\n",
				unit,pnum,event);
			bc->rout=bc->rin;
			bc->idata=bc->iempty=bc->ilow=0;
		}

		tail= (tail+4) & (FEP_IMAX-FEP_ISTART-4);
	}

	sc->mailbox->eout=tail;
	bmws_set(ws);

	timeout(dgbpoll, unit_c, hz/POLLSPERSEC);
}

static	int
dgbioctl(dev, cmd, data, flag, p)
	dev_t		dev;
	u_long		cmd;
	caddr_t		data;
	int		flag;
	struct proc	*p;
{
	struct dgb_softc *sc;
	int unit, pnum;
	struct dgb_p *port;
	int mynor;
	struct tty *tp;
	volatile struct board_chan *bc;
	int error;
	int s,cs;
	int tiocm_xxx;

#if defined(COMPAT_43) || defined(COMPAT_SUNOS)
	u_long		oldcmd;
	struct termios	term;
#endif

	BoardMemWinState ws=bmws_get();

	mynor=minor(dev);
	unit=MINOR_TO_UNIT(mynor);
	pnum=MINOR_TO_PORT(mynor);

	sc=&dgb_softc[unit];
	port=&sc->ports[pnum];
	tp=&sc->ttys[pnum];
	bc=port->brdchan;

	if (mynor & CONTROL_MASK) {
		struct termios *ct;

		switch (mynor & CONTROL_MASK) {
		case CONTROL_INIT_STATE:
			ct = mynor & CALLOUT_MASK ? &port->it_out : &port->it_in;
			break;
		case CONTROL_LOCK_STATE:
			ct = mynor & CALLOUT_MASK ? &port->lt_out : &port->lt_in;
			break;
		default:
			return (ENODEV);	/* /dev/nodev */
		}
		switch (cmd) {
		case TIOCSETA:
			error = suser(p);
			if (error != 0)
				return (error);
			*ct = *(struct termios *)data;
			return (0);
		case TIOCGETA:
			*(struct termios *)data = *ct;
			return (0);
		case TIOCGETD:
			*(int *)data = TTYDISC;
			return (0);
		case TIOCGWINSZ:
			bzero(data, sizeof(struct winsize));
			return (0);
		default:
			return (ENOTTY);
		}
	}

#if defined(COMPAT_43) || defined(COMPAT_SUNOS)
	term = tp->t_termios;
	if (cmd == TIOCSETA || cmd == TIOCSETAW || cmd == TIOCSETAF) {
	  DPRINT6(DB_PARAM,"dgb%d: port%d: dgbioctl-ISNOW c=0x%x i=0x%x l=0x%x\n",unit,pnum,term.c_cflag,term.c_iflag,term.c_lflag);
	}
	oldcmd = cmd;
	error = ttsetcompat(tp, &cmd, data, &term);
	if (error != 0)
		return (error);
	if (cmd != oldcmd)
		data = (caddr_t)&term;
#endif

	if (cmd == TIOCSETA || cmd == TIOCSETAW || cmd == TIOCSETAF) {
		int	cc;
		struct termios *dt = (struct termios *)data;
		struct termios *lt = mynor & CALLOUT_MASK
				     ? &port->lt_out : &port->lt_in;

		DPRINT6(DB_PARAM,"dgb%d: port%d: dgbioctl-TOSET c=0x%x i=0x%x l=0x%x\n",unit,pnum,dt->c_cflag,dt->c_iflag,dt->c_lflag);
		dt->c_iflag = (tp->t_iflag & lt->c_iflag)
			      | (dt->c_iflag & ~lt->c_iflag);
		dt->c_oflag = (tp->t_oflag & lt->c_oflag)
			      | (dt->c_oflag & ~lt->c_oflag);
		dt->c_cflag = (tp->t_cflag & lt->c_cflag)
			      | (dt->c_cflag & ~lt->c_cflag);
		dt->c_lflag = (tp->t_lflag & lt->c_lflag)
			      | (dt->c_lflag & ~lt->c_lflag);
		for (cc = 0; cc < NCCS; ++cc)
			if (lt->c_cc[cc] != 0)
				dt->c_cc[cc] = tp->t_cc[cc];
		if (lt->c_ispeed != 0)
			dt->c_ispeed = tp->t_ispeed;
		if (lt->c_ospeed != 0)
			dt->c_ospeed = tp->t_ospeed;
	}

	if(cmd==TIOCSTOP) {
		cs=splclock();
		setwin(sc,0);
		fepcmd(port, PAUSETX, 0, 0, 0, 0);
		bmws_set(ws);
		splx(cs);
		return 0;
	} else if(cmd==TIOCSTART) {
		cs=splclock();
		setwin(sc,0);
		fepcmd(port, RESUMETX, 0, 0, 0, 0);
		bmws_set(ws);
		splx(cs);
		return 0;
	}

	if(cmd==TIOCSETAW || cmd==TIOCSETAF)
		port->mustdrain=1;

	error = linesw[tp->t_line].l_ioctl(tp, cmd, data, flag, p);
	if (error != ENOIOCTL)
		return error;
	s = spltty();
	error = ttioctl(tp, cmd, data, flag);
	disc_optim(tp,&tp->t_termios);
	port->mustdrain=0;
	if (error != ENOIOCTL) {
		splx(s);
		if (cmd == TIOCSETA || cmd == TIOCSETAW || cmd == TIOCSETAF) {
			DPRINT6(DB_PARAM,"dgb%d: port%d: dgbioctl-RES c=0x%x i=0x%x l=0x%x\n",unit,pnum,tp->t_cflag,tp->t_iflag,tp->t_lflag);
		}
		return error;
	}

	switch (cmd) {
	case TIOCSBRK:
/* Helg: commented */
/*		error=dgbdrain(port);*/

		if(error!=0) {
			splx(s);
			return error;
		}

		cs=splclock();
		setwin(sc,0);
	
		/* now it sends 250 millisecond break because I don't know */
		/* how to send an infinite break */

		fepcmd(port, SENDBREAK, 250, 0, 10, 0);
		hidewin(sc);
		splx(cs);
		break;
	case TIOCCBRK:
		/* now it's empty */
		break;
	case TIOCSDTR:
		DPRINT3(DB_MODEM,"dgb%d: port%d: set DTR\n",unit,pnum);
		port->omodem |= DTR;
		cs=splclock();
		setwin(sc,0);
		fepcmd(port, SETMODEM, port->omodem, RTS, 0, 1);

		if( !(bc->mstat & DTR) ) {
			DPRINT3(DB_MODEM,"dgb%d: port%d: DTR is off\n",unit,pnum);
		}

		hidewin(sc);
		splx(cs);
		break;
	case TIOCCDTR:
		DPRINT3(DB_MODEM,"dgb%d: port%d: reset DTR\n",unit,pnum);
		port->omodem &= ~DTR;
		cs=splclock();
		setwin(sc,0);
		fepcmd(port, SETMODEM, port->omodem, RTS|DTR, 0, 1);

		if( bc->mstat & DTR ) {
			DPRINT3(DB_MODEM,"dgb%d: port%d: DTR is on\n",unit,pnum);
		}

		hidewin(sc);
		splx(cs);
		break;
	case TIOCMSET:
		if(*(int *)data & TIOCM_DTR)
			port->omodem |=DTR;
		else
			port->omodem &=~DTR;

		if(*(int *)data & TIOCM_RTS)
			port->omodem |=RTS;
		else
			port->omodem &=~RTS;

		cs=splclock();
		setwin(sc,0);
		fepcmd(port, SETMODEM, port->omodem, RTS|DTR, 0, 1);
		hidewin(sc);
		splx(cs);
		break;
	case TIOCMBIS:
		if(*(int *)data & TIOCM_DTR)
			port->omodem |=DTR;

		if(*(int *)data & TIOCM_RTS)
			port->omodem |=RTS;

		cs=splclock();
		setwin(sc,0);
		fepcmd(port, SETMODEM, port->omodem, RTS|DTR, 0, 1);
		hidewin(sc);
		splx(cs);
		break;
	case TIOCMBIC:
		if(*(int *)data & TIOCM_DTR)
			port->omodem &=~DTR;

		if(*(int *)data & TIOCM_RTS)
			port->omodem &=~RTS;

		cs=splclock();
		setwin(sc,0);
		fepcmd(port, SETMODEM, port->omodem, RTS|DTR, 0, 1);
		hidewin(sc);
		splx(cs);
		break;
	case TIOCMGET:
		setwin(sc,0);
		port->imodem=bc->mstat;
		hidewin(sc);

		tiocm_xxx = TIOCM_LE;	/* XXX - always enabled while open */

		DPRINT3(DB_MODEM,"dgb%d: port%d: modem stat -- ",unit,pnum);

		if (port->imodem & DTR) {
			DPRINT1(DB_MODEM,"DTR ");
			tiocm_xxx |= TIOCM_DTR;
		}
		if (port->imodem & RTS) {
			DPRINT1(DB_MODEM,"RTS ");
			tiocm_xxx |= TIOCM_RTS;
		}
		if (port->imodem & CTS) {
			DPRINT1(DB_MODEM,"CTS ");
			tiocm_xxx |= TIOCM_CTS;
		}
		if (port->imodem & port->dcd) {
			DPRINT1(DB_MODEM,"DCD ");
			tiocm_xxx |= TIOCM_CD;
		}
		if (port->imodem & port->dsr) {
			DPRINT1(DB_MODEM,"DSR ");
			tiocm_xxx |= TIOCM_DSR;
		}
		if (port->imodem & RI) {
			DPRINT1(DB_MODEM,"RI ");
			tiocm_xxx |= TIOCM_RI;
		}
		*(int *)data = tiocm_xxx;
		DPRINT1(DB_MODEM,"--\n");
		break;
	case TIOCMSDTRWAIT:
		/* must be root since the wait applies to following logins */
		error = suser(p);
		if (error != 0) {
			splx(s);
			return (error);
		}
		port->close_delay = *(int *)data * hz / 100;
		break;
	case TIOCMGDTRWAIT:
		*(int *)data = port->close_delay * 100 / hz;
		break;
	case TIOCTIMESTAMP:
		port->do_timestamp = TRUE;
		*(struct timeval *)data = port->timestamp;
		break;
	case TIOCDCDTIMESTAMP:
		port->do_dcd_timestamp = TRUE;
		*(struct timeval *)data = port->dcd_timestamp;
		break;
	default:
		bmws_set(ws);
		splx(s);
		return ENOTTY;
	}
	bmws_set(ws);
	splx(s);

	return 0;
}

static void 
wakeflush(p)
	void *p;
{
	struct dgb_p *port=p;

	wakeup(&port->draining);
}

/* wait for the output to drain */

static int
dgbdrain(port)
	struct dgb_p	*port;
{
	struct dgb_softc *sc=&dgb_softc[port->unit];
	volatile struct board_chan *bc=port->brdchan;
	int error;
	int head, tail;

	BoardMemWinState ws=bmws_get();

	setwin(sc,0);

	bc->iempty=1;
	tail=bc->tout;
	head=bc->tin;

	while(tail!=head) {
		DPRINT5(DB_WR,"dgb%d: port%d: drain: head=%d tail=%d\n",
			port->unit, port->pnum, head, tail);

		hidewin(sc);
		port->draining=1;
		timeout(wakeflush,port, hz);
		error=tsleep(&port->draining, TTIPRI | PCATCH, "dgdrn", 0);
		port->draining=0;
		setwin(sc,0);

		if (error != 0) {
			DPRINT4(DB_WR,"dgb%d: port%d: tsleep(dgdrn) error=%d\n",
				port->unit,port->pnum,error);

			bc->iempty=0;
			bmws_set(ws);
			return error;
		}

		tail=bc->tout;
		head=bc->tin;
	}
	DPRINT5(DB_WR,"dgb%d: port%d: drain: head=%d tail=%d\n",
		port->unit, port->pnum, head, tail);
	bmws_set(ws);
	return 0;
}

/* wait for the output to drain */
/* or simply clear the buffer it it's stopped */

static void
dgb_drain_or_flush(port)
	struct dgb_p	*port;
{
	struct tty *tp=port->tty;
	struct dgb_softc *sc=&dgb_softc[port->unit];
	volatile struct board_chan *bc=port->brdchan;
	int error;
	int lasttail;
	int head, tail;

	setwin(sc,0);

	lasttail=-1;
	bc->iempty=1;
	tail=bc->tout;
	head=bc->tin;

	while(tail!=head /* && tail!=lasttail */ ) {
		DPRINT5(DB_WR,"dgb%d: port%d: flush: head=%d tail=%d\n",
			port->unit, port->pnum, head, tail);

		/* if there is no carrier simply clean the buffer */
		if( !(tp->t_state & TS_CARR_ON) ) {
			bc->tout=bc->tin=0;
			bc->iempty=0;
			hidewin(sc);
			return;
		}

		hidewin(sc);
		port->draining=1;
		timeout(wakeflush,port, hz);
		error=tsleep(&port->draining, TTIPRI | PCATCH, "dgfls", 0);
		port->draining=0;
		setwin(sc,0);

		if (error != 0) {
			DPRINT4(DB_WR,"dgb%d: port%d: tsleep(dgfls) error=%d\n",
				port->unit,port->pnum,error);

			/* silently clean the buffer */

			bc->tout=bc->tin=0;
			bc->iempty=0;
			hidewin(sc);
			return;
		}

		lasttail=tail;
		tail=bc->tout;
		head=bc->tin;
	}
	hidewin(sc);
	DPRINT5(DB_WR,"dgb%d: port%d: flush: head=%d tail=%d\n",
			port->unit, port->pnum, head, tail);
}

static int
dgbparam(tp, t)
	struct tty	*tp;
	struct termios	*t;
{
	int unit=MINOR_TO_UNIT(minor(tp->t_dev));
	int pnum=MINOR_TO_PORT(minor(tp->t_dev));
	struct dgb_softc *sc=&dgb_softc[unit];
	struct dgb_p *port=&sc->ports[pnum];
	volatile struct board_chan *bc=port->brdchan;
	int cflag;
	int head;
	int mval;
	int iflag;
	int hflow;
	int cs;

	BoardMemWinState ws=bmws_get();

	DPRINT6(DB_PARAM,"dgb%d: port%d: dgbparm c=0x%x i=0x%x l=0x%x\n",unit,pnum,t->c_cflag,t->c_iflag,t->c_lflag);

	if(port->mustdrain) {
		DPRINT3(DB_PARAM,"dgb%d: port%d: must call dgbdrain()\n",unit,pnum);
		dgbdrain(port);
	}

	cflag=ttspeedtab(t->c_ospeed, dgbspeedtab);

	if (t->c_ispeed == 0)
		t->c_ispeed = t->c_ospeed;

	if (cflag < 0 /* || cflag > 0 && t->c_ispeed != t->c_ospeed */) {
		DPRINT4(DB_PARAM,"dgb%d: port%d: invalid cflag=0%o\n",unit,pnum,cflag);
		return (EINVAL);
	}

	cs=splclock();
	setwin(sc,0);

	if(cflag==0) { /* hangup */
		DPRINT3(DB_PARAM,"dgb%d: port%d: hangup\n",unit,pnum);
		head=bc->rin;
		bc->rout=head;
		head=bc->tin;
		fepcmd(port, STOUT, (unsigned)head, 0, 0, 0);
		mval= port->omodem & ~(DTR|RTS);
	} else {
		cflag |= dgbflags(dgb_cflags, t->c_cflag);

		if(cflag!=port->fepcflag) {
			port->fepcflag=cflag;
			DPRINT5(DB_PARAM,"dgb%d: port%d: set cflag=0x%x c=0x%x\n",
					unit,pnum,cflag,t->c_cflag&~CRTSCTS);
			fepcmd(port, SETCTRLFLAGS, (unsigned)cflag, 0, 0, 0);
		}
		mval= port->omodem | (DTR|RTS);
	}

	iflag=dgbflags(dgb_iflags, t->c_iflag);
	if(iflag!=port->fepiflag) {
		port->fepiflag=iflag;
		DPRINT5(DB_PARAM,"dgb%d: port%d: set iflag=0x%x c=0x%x\n",unit,pnum,iflag,t->c_iflag);
		fepcmd(port, SETIFLAGS, (unsigned)iflag, 0, 0, 0);
	}

	bc->mint=port->dcd;

	hflow=dgbflags(dgb_flow, t->c_cflag);
	if(hflow!=port->hflow) {
		port->hflow=hflow;
		DPRINT5(DB_PARAM,"dgb%d: port%d: set hflow=0x%x f=0x%x\n",unit,pnum,hflow,t->c_cflag&CRTSCTS);
		fepcmd(port, SETHFLOW, (unsigned)hflow, 0xff, 0, 1);
	}
	
	if(port->omodem != mval) {
		DPRINT5(DB_PARAM,"dgb%d: port%d: setting modem parameters 0x%x was 0x%x\n",
			unit,pnum,mval,port->omodem);
		port->omodem=mval;
		fepcmd(port, SETMODEM, (unsigned)mval, RTS|DTR, 0, 1);
	}

	if(port->fepstartc!=t->c_cc[VSTART] || port->fepstopc!=t->c_cc[VSTOP]) {
		DPRINT5(DB_PARAM,"dgb%d: port%d: set startc=%d, stopc=%d\n",unit,pnum,t->c_cc[VSTART],t->c_cc[VSTOP]);
		port->fepstartc=t->c_cc[VSTART];
		port->fepstopc=t->c_cc[VSTOP];
		fepcmd(port, SONOFFC, port->fepstartc, port->fepstopc, 0, 1);
	}

	bmws_set(ws);
	splx(cs);

	return 0;

}

static void
dgbstart(tp)
	struct tty	*tp;
{
	int unit;
	int pnum;
	struct dgb_p *port;
	struct dgb_softc *sc;
	volatile struct board_chan *bc;
	int head, tail;
	int size, ocount;
	int s;
	int wmask;

	BoardMemWinState ws=bmws_get();

	unit=MINOR_TO_UNIT(minor(tp->t_dev));
	pnum=MINOR_TO_PORT(minor(tp->t_dev));
	sc=&dgb_softc[unit];
	port=&sc->ports[pnum];
	bc=port->brdchan;

	wmask=port->txbufsize-1;

	s=spltty();

	while( tp->t_outq.c_cc!=0 ) {
		int cs;
#ifndef TS_ASLEEP	/* post 2.0.5 FreeBSD */
		ttwwakeup(tp);
#else
		if(tp->t_outq.c_cc <= tp->t_lowat) {
			if(tp->t_state & TS_ASLEEP) {
				tp->t_state &= ~TS_ASLEEP;
				wakeup(TSA_OLOWAT(tp));
			}
			/*selwakeup(&tp->t_wsel);*/
		}
#endif
		cs=splclock();
		setwin(sc,0);

		head=bc->tin & wmask;

		do { tail=bc->tout; } while (tail != bc->tout);
		tail=bc->tout & wmask;

		DPRINT5(DB_WR,"dgb%d: port%d: s tx head=%d tail=%d\n",unit,pnum,head,tail);

#ifdef LEAVE_FREE_CHARS 
		if(tail>head) {
			size=tail-head-LEAVE_FREE_CHARS;
			if (size <0)
			        size=0;
		        } else {
			        size=port->txbufsize-head;
			        if(tail+port->txbufsize < head)
				        size=0;
		        }
		}
#else
		if(tail>head)
			size=tail-head-1;
		else {
			size=port->txbufsize-head/*-1*/;
			if(tail==0)
				size--;
		}
#endif

		if(size==0) {
			bc->iempty=1; bc->ilow=1;
			splx(cs);
			bmws_set(ws);
			tp->t_state|=TS_BUSY;
			splx(s);
			return;
		}

		towin(sc,port->txwin);

		ocount=q_to_b(&tp->t_outq, port->txptr+head, size);
		head+=ocount;
		if(head>=port->txbufsize)
			head-=port->txbufsize;

		setwin(sc,0);
		bc->tin=head;

		DPRINT5(DB_WR,"dgb%d: port%d: tx avail=%d count=%d\n",unit,pnum,size,ocount);
		hidewin(sc);
		splx(cs);
	}

	bmws_set(ws);
	splx(s);

#ifndef TS_ASLEEP	/* post 2.0.5 FreeBSD */
	if(tp->t_state & TS_BUSY) {	
		tp->t_state&=~TS_BUSY;
		linesw[tp->t_line].l_start(tp);
		ttwwakeup(tp);
	}
#else
	if(tp->t_state & TS_ASLEEP) {
		tp->t_state &= ~TS_ASLEEP;
		wakeup(TSA_OLOWAT(tp));
	}
	tp->t_state&=~TS_BUSY;
#endif
}

void
dgbstop(tp, rw)
	struct tty	*tp;
	int		rw;
{
	int unit;
	int pnum;
	struct dgb_p *port;
	struct dgb_softc *sc;
	volatile struct board_chan *bc;
	int s;

	BoardMemWinState ws=bmws_get();

	unit=MINOR_TO_UNIT(minor(tp->t_dev));
	pnum=MINOR_TO_PORT(minor(tp->t_dev));

	sc=&dgb_softc[unit];
	port=&sc->ports[pnum];
	bc=port->brdchan;

	DPRINT3(DB_WR,"dgb%d: port%d: stop\n",port->unit, port->pnum);

	s = spltty();
	setwin(sc,0);

	if (rw & FWRITE) {
		/* clear output queue */
		bc->tout=bc->tin=0;
		bc->ilow=0;bc->iempty=0;
	}
	if (rw & FREAD) {
		/* clear input queue */
		bc->rout=bc->rin;
		bc->idata=1;
	}
	hidewin(sc);
	bmws_set(ws);
	splx(s);
	dgbstart(tp);
}

static void 
fepcmd(port, cmd, op1, op2, ncmds, bytecmd)
	struct dgb_p *port;
	unsigned cmd, op1, op2, ncmds, bytecmd;
{
	struct dgb_softc *sc=&dgb_softc[port->unit];
	u_char *mem=sc->vmem;
	unsigned tail, head;
	int count, n;

	if(port->status==DISABLED) {
		printf("dgb%d: port%d: FEP command on disabled port\n", 
			port->unit, port->pnum);
		return;
	}

	/* setwin(sc,0); Require this to be set by caller */
	head=sc->mailbox->cin;

	if(head>=(FEP_CMAX-FEP_CSTART) || (head & 3)) {
		printf("dgb%d: port%d: wrong pointer head of command queue : 0x%x\n",
			port->unit, port->pnum, head);
		return;
	}

	mem[head+FEP_CSTART+0]=cmd;
	mem[head+FEP_CSTART+1]=port->pnum;
	if(bytecmd) {
		mem[head+FEP_CSTART+2]=op1;
		mem[head+FEP_CSTART+3]=op2;
	} else {
		mem[head+FEP_CSTART+2]=op1&0xff;
		mem[head+FEP_CSTART+3]=(op1>>8)&0xff;
	}

	DPRINT7(DB_FEP,"dgb%d: port%d: %s cmd=0x%x op1=0x%x op2=0x%x\n", port->unit, port->pnum,
			(bytecmd)?"byte":"word", cmd, mem[head+FEP_CSTART+2], mem[head+FEP_CSTART+3]);

	head=(head+4) & (FEP_CMAX-FEP_CSTART-4);
	sc->mailbox->cin=head;

	count=FEPTIMEOUT;

	while (count-- != 0) {
		head=sc->mailbox->cin;
		tail=sc->mailbox->cout;

		n = (head-tail) & (FEP_CMAX-FEP_CSTART-4);
		if(n <= ncmds * (sizeof(ushort)*4))
			return;
	}
	printf("dgb%d(%d): timeout on FEP cmd=0x%x\n", port->unit, port->pnum, cmd);
}

static void 
disc_optim(tp, t)
	struct tty	*tp;
	struct termios	*t;
{
	if (!(t->c_iflag & (ICRNL | IGNCR | IMAXBEL | INLCR | ISTRIP | IXON))
	    && (!(t->c_iflag & BRKINT) || (t->c_iflag & IGNBRK))
	    && (!(t->c_iflag & PARMRK)
		|| (t->c_iflag & (IGNPAR | IGNBRK)) == (IGNPAR | IGNBRK))
	    && !(t->c_lflag & (ECHO | ICANON | IEXTEN | ISIG | PENDIN))
	    && linesw[tp->t_line].l_rint == ttyinput)
		tp->t_state |= TS_CAN_BYPASS_L_RINT;
	else
		tp->t_state &= ~TS_CAN_BYPASS_L_RINT;
}
