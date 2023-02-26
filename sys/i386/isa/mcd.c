/*
 * Copyright 1993 by Holger Veit (data part)
 * Copyright 1993 by Brian Moore (audio part)
 * Changes Copyright 1993 by Gary Clark II
 * Changes Copyright (C) 1994-1995 by Andrey A. Chernov, Moscow, Russia
 *
 * Rewrote probe routine to work on newer Mitsumi drives.
 * Additional changes (C) 1994 by Jordan K. Hubbard
 *
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This software was developed by Holger Veit and Brian Moore
 *	for use with "386BSD" and similar operating systems.
 *    "Similar operating systems" includes mainly non-profit oriented
 *    systems for research and education, including but not restricted to
 *    "NetBSD", "FreeBSD", "Mach" (by CMU).
 * 4. Neither the name of the developer(s) nor the name "386BSD"
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE DEVELOPER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE DEVELOPER(S) BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/isa/mcd.c,v 1.115 2000/01/29 16:17:34 peter Exp $
 */
static const char COPYRIGHT[] = "mcd-driver (C)1993 by H.Veit & B.Moore";

#include "mcd.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/buf.h>
#include <sys/cdio.h>
#include <sys/disklabel.h>
#include <sys/kernel.h>

#include <machine/clock.h>

#include <i386/isa/isa_device.h>
#include <i386/isa/mcdreg.h>

#define	MCD_TRACE(format, args...)						\
{									\
	if (mcd_data[unit].debug) {					\
		printf("mcd%d: status=0x%02x: ",			\
			unit, mcd_data[unit].status);			\
		printf(format, ## args);				\
	}								\
}

#define mcd_part(dev)	((minor(dev)) & 7)
#define mcd_unit(dev)	(((minor(dev)) & 0x38) >> 3)
#define mcd_phys(dev)	(((minor(dev)) & 0x40) >> 6)
#define RAW_PART        2

/* flags */
#define MCDVALID        0x0001  /* parameters loaded */
#define MCDINIT         0x0002  /* device is init'd */
#define MCDNEWMODEL     0x0004  /* device is new model */
#define MCDLABEL        0x0008  /* label is read */
#define MCDPROBING      0x0010  /* probing */
#define MCDREADRAW      0x0020  /* read raw mode (2352 bytes) */
#define MCDVOLINFO      0x0040  /* already read volinfo */
#define MCDTOC          0x0080  /* already read toc */
#define MCDMBXBSY       0x0100  /* local mbx is busy */

/* status */
#define	MCDAUDIOBSY	MCD_ST_AUDIOBSY		/* playing audio */
#define MCDDSKCHNG	MCD_ST_DSKCHNG		/* sensed change of disk */
#define MCDDSKIN	MCD_ST_DSKIN		/* sensed disk in drive */
#define MCDDOOROPEN	MCD_ST_DOOROPEN		/* sensed door open */

/* These are apparently the different states a mitsumi can get up to */
#define MCDCDABSENT	0x0030
#define MCDCDPRESENT	0x0020
#define MCDSCLOSED	0x0080
#define MCDSOPEN	0x00a0

#define MCD_MD_UNKNOWN  (-1)

/* toc */
#define MCD_MAXTOCS	104	/* from the Linux driver */
#define MCD_LASTPLUS1	170	/* special toc entry */

#define	MCD_TYPE_UNKNOWN	0
#define	MCD_TYPE_LU002S		1
#define	MCD_TYPE_LU005S		2
#define MCD_TYPE_LU006S         3
#define MCD_TYPE_FX001          4
#define MCD_TYPE_FX001D         5

struct mcd_mbx {
	short		unit;
	short		port;
	short		retry;
	short		nblk;
	int		sz;
	u_long		skip;
	struct buf	*bp;
	int		p_offset;
	short		count;
	short           mode;
};

static struct mcd_data {
	short	type;
	char	*name;
	short	config;
	short	flags;
	u_char	read_command;
	short	status;
	int	blksize;
	u_long	disksize;
	int	iobase;
	struct disklabel dlabel;
	int	partflags[MAXPARTITIONS];
	int	openflags;
	struct mcd_volinfo volinfo;
	struct mcd_qchninfo toc[MCD_MAXTOCS];
	short	audio_status;
	short   curr_mode;
	struct mcd_read2 lastpb;
	short	debug;
	struct buf_queue_head head;		/* head of buf queue */
	struct mcd_mbx mbx;
} mcd_data[NMCD];

/* reader state machine */
#define MCD_S_BEGIN	0
#define MCD_S_BEGIN1	1
#define MCD_S_WAITSTAT	2
#define MCD_S_WAITMODE	3
#define MCD_S_WAITREAD	4

/* prototypes */
static	void	mcd_start(int unit);
static	int	mcd_getdisklabel(int unit);
#ifdef NOTYET
static	void	mcd_configure(struct mcd_data *cd);
#endif
static	int	mcd_get(int unit, char *buf, int nmax);
static  int     mcd_setflags(int unit,struct mcd_data *cd);
static	int	mcd_getstat(int unit,int sflg);
static	int	mcd_send(int unit, int cmd,int nretrys);
static	void	hsg2msf(int hsg, bcd_t *msf);
static  int     msf2hsg(bcd_t *msf, int relative);
static	int	mcd_volinfo(int unit);
static	ointhand2_t	mcdintr;
static	int	mcd_waitrdy(int port,int dly);
static	timeout_t mcd_timeout;
static 	void	mcd_doread(int state, struct mcd_mbx *mbxin);
static  void    mcd_soft_reset(int unit);
static  int     mcd_hard_reset(int unit);
static	int 	mcd_setmode(int unit, int mode);
static	int	mcd_getqchan(int unit, struct mcd_qchninfo *q);
static	int	mcd_subchan(int unit, struct ioc_read_subchannel *sc);
static	int	mcd_toc_header(int unit, struct ioc_toc_header *th);
static	int	mcd_read_toc(int unit);
static  int     mcd_toc_entrys(int unit, struct ioc_read_toc_entry *te);
#if 0
static  int     mcd_toc_entry(int unit, struct ioc_read_toc_single_entry *te);
#endif
static	int	mcd_stop(int unit);
static  int     mcd_eject(int unit);
static  int     mcd_inject(int unit);
static	int	mcd_playtracks(int unit, struct ioc_play_track *pt);
static	int	mcd_play(int unit, struct mcd_read2 *pb);
static  int     mcd_playmsf(int unit, struct ioc_play_msf *pt);
static  int     mcd_playblocks(int unit, struct ioc_play_blocks *);
static	int	mcd_pause(int unit);
static	int	mcd_resume(int unit);
static  int     mcd_lock_door(int unit, int lock);
static  int     mcd_close_tray(int unit);

static	int	mcd_probe(struct isa_device *dev);
static	int	mcd_attach(struct isa_device *dev);
struct	isa_driver	mcddriver = { mcd_probe, mcd_attach, "mcd" };

static	d_open_t	mcdopen;
static	d_close_t	mcdclose;
static	d_ioctl_t	mcdioctl;
static	d_psize_t	mcdsize;
static	d_strategy_t	mcdstrategy;

#define CDEV_MAJOR 29
#define BDEV_MAJOR 7



static struct cdevsw mcd_cdevsw = {
	/* open */	mcdopen,
	/* close */	mcdclose,
	/* read */	physread,
	/* write */	nowrite,
	/* ioctl */	mcdioctl,
	/* poll */	nopoll,
	/* mmap */	nommap,
	/* strategy */	mcdstrategy,
	/* name */	"mcd",
	/* maj */	CDEV_MAJOR,
	/* dump */	nodump,
	/* psize */	nopsize,
	/* flags */	D_DISK,
	/* bmaj */	BDEV_MAJOR
};

#define mcd_put(port,byte)	outb(port,byte)

#define MCD_RETRYS	5
#define MCD_RDRETRYS	8

#define CLOSE_TRAY_SECS 8
#define DISK_SENSE_SECS 3
#define WAIT_FRAC 4

/* several delays */
#define RDELAY_WAITSTAT 300
#define RDELAY_WAITMODE 300
#define RDELAY_WAITREAD	800

#define MIN_DELAY       15
#define DELAY_GETREPLY  5000000

int mcd_attach(struct isa_device *dev)
{
	int	unit = dev->id_unit;
	struct mcd_data *cd = mcd_data + unit;

	dev->id_ointr = mcdintr;
	cd->iobase = dev->id_iobase;
	cd->flags |= MCDINIT;
	mcd_soft_reset(unit);
	bufq_init(&cd->head);

#ifdef NOTYET
	/* wire controller for interrupts and dma */
	mcd_configure(cd);
#endif
	/* name filled in probe */
	make_dev(&mcd_cdevsw, dkmakeminor(unit, 0, 0),
	    UID_ROOT, GID_OPERATOR, 0640, "rmcd%da", unit);
	make_dev(&mcd_cdevsw, dkmakeminor(unit, 0, RAW_PART),
	    UID_ROOT, GID_OPERATOR, 0640, "rmcd%dc", unit);
	make_dev(&mcd_cdevsw, dkmakeminor(unit, 0, 0),
	    UID_ROOT, GID_OPERATOR, 0640, "mcd%da", unit);
	make_dev(&mcd_cdevsw, dkmakeminor(unit, 0, RAW_PART),
	    UID_ROOT, GID_OPERATOR, 0640, "mcd%dc", unit);
	return 1;
}

int mcdopen(dev_t dev, int flags, int fmt, struct proc *p)
{
	int unit,part,phys,r,retry;
	struct mcd_data *cd;

	unit = mcd_unit(dev);
	if (unit >= NMCD)
		return ENXIO;

	cd = mcd_data + unit;
	part = mcd_part(dev);
	phys = mcd_phys(dev);

	/* not initialized*/
	if (!(cd->flags & MCDINIT))
		return ENXIO;

	/* invalidated in the meantime? mark all open part's invalid */
	if (!(cd->flags & MCDVALID) && cd->openflags)
		return ENXIO;

	if (mcd_getstat(unit,1) == -1)
		return EIO;

	if (    (cd->status & (MCDDSKCHNG|MCDDOOROPEN))
	    || !(cd->status & MCDDSKIN))
		for (retry = 0; retry < DISK_SENSE_SECS * WAIT_FRAC; retry++) {
			(void) tsleep((caddr_t)cd, PSOCK | PCATCH, "mcdsn1", hz/WAIT_FRAC);
			if ((r = mcd_getstat(unit,1)) == -1)
				return EIO;
			if (r != -2)
				break;
		}

	if ((   (cd->status & (MCDDOOROPEN|MCDDSKCHNG))
	     || !(cd->status & MCDDSKIN)
	    )
	    && major(dev) == CDEV_MAJOR && part == RAW_PART
	   ) {
		cd->openflags |= (1<<part);
		if (phys)
			cd->partflags[part] |= MCDREADRAW;
		return 0;
	}
	if (cd->status & MCDDOOROPEN) {
		printf("mcd%d: door is open\n", unit);
		return ENXIO;
	}
	if (!(cd->status & MCDDSKIN)) {
		printf("mcd%d: no CD inside\n", unit);
		return ENXIO;
	}
	if (cd->status & MCDDSKCHNG) {
		printf("mcd%d: CD not sensed\n", unit);
		return ENXIO;
	}

	if (mcdsize(dev) < 0) {
		if (major(dev) == CDEV_MAJOR && part == RAW_PART) {
			cd->openflags |= (1<<part);
			if (phys)
				cd->partflags[part] |= MCDREADRAW;
			return 0;
		}
		printf("mcd%d: failed to get disk size\n",unit);
		return ENXIO;
	} else
		cd->flags |= MCDVALID;

	/* XXX get a default disklabel */
	mcd_getdisklabel(unit);

MCD_TRACE("open: partition=%d, disksize = %ld, blksize=%d\n",
	part, cd->disksize, cd->blksize);

	dev->si_bsize_phys = cd->blksize;

	if (part == RAW_PART ||
		(part < cd->dlabel.d_npartitions &&
		cd->dlabel.d_partitions[part].p_fstype != FS_UNUSED)) {
		cd->openflags |= (1<<part);
		if (part == RAW_PART && phys)
			cd->partflags[part] |= MCDREADRAW;
		(void) mcd_lock_door(unit, MCD_LK_LOCK);
		if (!(cd->flags & MCDVALID))
			return ENXIO;
		return 0;
	}

	return ENXIO;
}

int mcdclose(dev_t dev, int flags, int fmt, struct proc *p)
{
	int unit,part;
	struct mcd_data *cd;

	unit = mcd_unit(dev);
	if (unit >= NMCD)
		return ENXIO;

	cd = mcd_data + unit;
	part = mcd_part(dev);

	if (!(cd->flags & MCDINIT) || !(cd->openflags & (1<<part)))
		return ENXIO;

	MCD_TRACE("close: partition=%d\n", part);

	(void) mcd_lock_door(unit, MCD_LK_UNLOCK);
	cd->openflags &= ~(1<<part);
	cd->partflags[part] &= ~MCDREADRAW;

	return 0;
}

void
mcdstrategy(struct buf *bp)
{
	struct mcd_data *cd;
	int s;

	int unit = mcd_unit(bp->b_dev);

	cd = mcd_data + unit;

	/* test validity */
/*MCD_TRACE("strategy: buf=0x%lx, unit=%ld, block#=%ld bcount=%ld\n",
	bp,unit,bp->b_blkno,bp->b_bcount);*/
	if (unit >= NMCD || bp->b_blkno < 0) {
		printf("mcdstrategy: unit = %d, blkno = %ld, bcount = %ld\n",
			unit, (long)bp->b_blkno, bp->b_bcount);
		printf("mcd: mcdstratregy failure");
		bp->b_error = EINVAL;
		bp->b_flags |= B_ERROR;
		goto bad;
	}

	/* if device invalidated (e.g. media change, door open), error */
	if (!(cd->flags & MCDVALID)) {
MCD_TRACE("strategy: drive not valid\n");
		bp->b_error = EIO;
		goto bad;
	}

	/* read only */
	if (!(bp->b_flags & B_READ)) {
		bp->b_error = EROFS;
		goto bad;
	}

	/* no data to read */
	if (bp->b_bcount == 0)
		goto done;

	/* for non raw access, check partition limits */
	if (mcd_part(bp->b_dev) != RAW_PART) {
		if (!(cd->flags & MCDLABEL)) {
			bp->b_error = EIO;
			goto bad;
		}
		/* adjust transfer if necessary */
		if (bounds_check_with_label(bp,&cd->dlabel,1) <= 0) {
			goto done;
		}
	} else {
		bp->b_pblkno = bp->b_blkno;
		bp->b_resid = 0;
	}

	/* queue it */
	s = splbio();
	bufqdisksort(&cd->head, bp);
	splx(s);

	/* now check whether we can perform processing */
	mcd_start(unit);
	return;

bad:
	bp->b_flags |= B_ERROR;
done:
	bp->b_resid = bp->b_bcount;
	biodone(bp);
	return;
}

static void mcd_start(int unit)
{
	struct mcd_data *cd = mcd_data + unit;
	struct partition *p;
	struct buf *bp;
	int s = splbio();

	if (cd->flags & MCDMBXBSY) {
		splx(s);
		return;
	}

	bp = bufq_first(&cd->head);
	if (bp != 0) {
		/* block found to process, dequeue */
		/*MCD_TRACE("mcd_start: found block bp=0x%x\n",bp,0,0,0);*/
		bufq_remove(&cd->head, bp);
		splx(s);
	} else {
		/* nothing to do */
		splx(s);
		return;
	}

	/* changed media? */
	if (!(cd->flags	& MCDVALID)) {
		MCD_TRACE("mcd_start: drive not valid\n");
		return;
	}

	p = cd->dlabel.d_partitions + mcd_part(bp->b_dev);

	cd->flags |= MCDMBXBSY;
	if (cd->partflags[mcd_part(bp->b_dev)] & MCDREADRAW)
		cd->flags |= MCDREADRAW;
	cd->mbx.unit = unit;
	cd->mbx.port = cd->iobase;
	cd->mbx.retry = MCD_RETRYS;
	cd->mbx.bp = bp;
	cd->mbx.p_offset = p->p_offset;

	/* calling the read routine */
	mcd_doread(MCD_S_BEGIN,&(cd->mbx));
	/* triggers mcd_start, when successful finished */
	return;
}

int mcdioctl(dev_t dev, u_long cmd, caddr_t addr, int flags, struct proc *p)
{
	struct mcd_data *cd;
	int unit,part,retry,r;

	unit = mcd_unit(dev);
	part = mcd_part(dev);
	cd = mcd_data + unit;

	if (mcd_getstat(unit, 1) == -1) /* detect disk change too */
		return EIO;
MCD_TRACE("ioctl called 0x%lx\n", cmd);

	switch (cmd) {
	case CDIOCSETPATCH:
	case CDIOCGETVOL:
	case CDIOCSETVOL:
	case CDIOCSETMONO:
	case CDIOCSETSTERIO:
	case CDIOCSETMUTE:
	case CDIOCSETLEFT:
	case CDIOCSETRIGHT:
		return EINVAL;
	case CDIOCEJECT:
		return mcd_eject(unit);
	case CDIOCSETDEBUG:
		cd->debug = 1;
		return 0;
	case CDIOCCLRDEBUG:
		cd->debug = 0;
		return 0;
	case CDIOCRESET:
		return mcd_hard_reset(unit);
	case CDIOCALLOW:
		return mcd_lock_door(unit, MCD_LK_UNLOCK);
	case CDIOCPREVENT:
		return mcd_lock_door(unit, MCD_LK_LOCK);
	case CDIOCCLOSE:
		return mcd_inject(unit);
	}

	if (!(cd->flags & MCDVALID)) {
		if (   major(dev) != CDEV_MAJOR
		    || part != RAW_PART
		    || !(cd->openflags & (1<<RAW_PART))
		   )
			return ENXIO;
		if (    (cd->status & (MCDDSKCHNG|MCDDOOROPEN))
		    || !(cd->status & MCDDSKIN))
			for (retry = 0; retry < DISK_SENSE_SECS * WAIT_FRAC; retry++) {
				(void) tsleep((caddr_t)cd, PSOCK | PCATCH, "mcdsn2", hz/WAIT_FRAC);
				if ((r = mcd_getstat(unit,1)) == -1)
					return EIO;
				if (r != -2)
					break;
			}
		if (   (cd->status & (MCDDOOROPEN|MCDDSKCHNG))
		    || !(cd->status & MCDDSKIN)
		    || mcdsize(dev) < 0
		   )
			return ENXIO;
		cd->flags |= MCDVALID;
		mcd_getdisklabel(unit);
		if (mcd_phys(dev))
			cd->partflags[part] |= MCDREADRAW;
		(void) mcd_lock_door(unit, MCD_LK_LOCK);
		if (!(cd->flags & MCDVALID))
			return ENXIO;
	}

	switch (cmd) {
	case DIOCGDINFO:
		*(struct disklabel *) addr = cd->dlabel;
		return 0;
	case DIOCGPART:
		((struct partinfo *) addr)->disklab = &cd->dlabel;
		((struct partinfo *) addr)->part =
		    &cd->dlabel.d_partitions[mcd_part(dev)];
		return 0;

		/*
		 * a bit silly, but someone might want to test something on a
		 * section of cdrom.
		 */
	case DIOCWDINFO:
	case DIOCSDINFO:
		if ((flags & FWRITE) == 0)
			return EBADF;
		else {
			return setdisklabel(&cd->dlabel,
			    (struct disklabel *) addr,
			    0);
		}
	case DIOCWLABEL:
		return EBADF;
	case CDIOCPLAYTRACKS:
		return mcd_playtracks(unit, (struct ioc_play_track *) addr);
	case CDIOCPLAYBLOCKS:
		return mcd_playblocks(unit, (struct ioc_play_blocks *) addr);
	case CDIOCPLAYMSF:
		return mcd_playmsf(unit, (struct ioc_play_msf *) addr);
	case CDIOCREADSUBCHANNEL:
		return mcd_subchan(unit, (struct ioc_read_subchannel *) addr);
	case CDIOREADTOCHEADER:
		return mcd_toc_header(unit, (struct ioc_toc_header *) addr);
	case CDIOREADTOCENTRYS:
		return mcd_toc_entrys(unit, (struct ioc_read_toc_entry *) addr);
	case CDIOCRESUME:
		return mcd_resume(unit);
	case CDIOCPAUSE:
		return mcd_pause(unit);
	case CDIOCSTART:
		if (mcd_setmode(unit, MCD_MD_COOKED) != 0)
			return EIO;
		return 0;
	case CDIOCSTOP:
		return mcd_stop(unit);
	default:
		return ENOTTY;
	}
	/*NOTREACHED*/
}

/* this could have been taken from scsi/cd.c, but it is not clear
 * whether the scsi cd driver is linked in
 */
static int mcd_getdisklabel(int unit)
{
	struct mcd_data *cd = mcd_data + unit;

	if (cd->flags & MCDLABEL)
		return -1;

	bzero(&cd->dlabel,sizeof(struct disklabel));
	/* filled with spaces first */
	strncpy(cd->dlabel.d_typename,"               ",
		sizeof(cd->dlabel.d_typename));
	strncpy(cd->dlabel.d_typename, cd->name,
		min(strlen(cd->name), sizeof(cd->dlabel.d_typename) - 1));
	strncpy(cd->dlabel.d_packname,"unknown        ",
		sizeof(cd->dlabel.d_packname));
	cd->dlabel.d_secsize 	= cd->blksize;
	cd->dlabel.d_nsectors	= 100;
	cd->dlabel.d_ntracks	= 1;
	cd->dlabel.d_ncylinders	= (cd->disksize/100)+1;
	cd->dlabel.d_secpercyl	= 100;
	cd->dlabel.d_secperunit	= cd->disksize;
	cd->dlabel.d_rpm	= 300;
	cd->dlabel.d_interleave	= 1;
	cd->dlabel.d_flags	= D_REMOVABLE;
	cd->dlabel.d_npartitions= 1;
	cd->dlabel.d_partitions[0].p_offset = 0;
	cd->dlabel.d_partitions[0].p_size = cd->disksize;
	cd->dlabel.d_partitions[0].p_fstype = 9;
	cd->dlabel.d_magic	= DISKMAGIC;
	cd->dlabel.d_magic2	= DISKMAGIC;
	cd->dlabel.d_checksum	= dkcksum(&cd->dlabel);

	cd->flags |= MCDLABEL;
	return 0;
}

int mcdsize(dev_t dev)
{
	int size;
	int unit = mcd_unit(dev);
	struct mcd_data *cd = mcd_data + unit;

	if (mcd_volinfo(unit) == 0) {
		cd->blksize = MCDBLK;
		size = msf2hsg(cd->volinfo.vol_msf, 0);
		cd->disksize = size * (MCDBLK/DEV_BSIZE);
		return 0;
	}
	return -1;
}

/***************************************************************
 * lower level of driver starts here
 **************************************************************/

#ifdef NOTDEF
static char
irqs[] = {
	0x00,0x00,0x10,0x20,0x00,0x30,0x00,0x00,
	0x00,0x10,0x40,0x50,0x00,0x00,0x00,0x00
};

static char
drqs[] = {
	0x00,0x01,0x00,0x03,0x00,0x05,0x06,0x07,
};
#endif

#ifdef NOT_YET
static void
mcd_configure(struct mcd_data *cd)
{
	outb(cd->iobase+mcd_config,cd->config);
}
#endif

/* Wait for non-busy - return 0 on timeout */
static int
twiddle_thumbs(int port, int unit, int count, char *whine)
{
	int i;

	for (i = 0; i < count; i++) {
		if (!(inb(port+MCD_FLAGS) & MFL_STATUS_NOT_AVAIL))
			return 1;
		}
	if (bootverbose)
		printf("mcd%d: timeout %s\n", unit, whine);
	return 0;
}

/* check to see if a Mitsumi CD-ROM is attached to the ISA bus */

int
mcd_probe(struct isa_device *dev)
{
	int port = dev->id_iobase;
	int unit = dev->id_unit;
	int i, j;
	unsigned char stbytes[3];
	static int once;

	if (!once++)
		cdevsw_add(&mcd_cdevsw);

	mcd_data[unit].flags = MCDPROBING;

#ifdef NOTDEF
	/* get irq/drq configuration word */
	mcd_data[unit].config = irqs[dev->id_irq]; /* | drqs[dev->id_drq];*/
#else
	mcd_data[unit].config = 0;
#endif

	/* send a reset */
	outb(port+MCD_FLAGS, M_RESET);

	/*
	 * delay awhile by getting any pending garbage (old data) and
	 * throwing it away.
	 */
	for (i = 1000000; i != 0; i--)
		inb(port+MCD_FLAGS);

	/* Get status */
	outb(port+MCD_DATA, MCD_CMDGETSTAT);
	if (!twiddle_thumbs(port, unit, 1000000, "getting status"))
		return 0;	/* Timeout */
	/* Get version information */
	outb(port+MCD_DATA, MCD_CMDCONTINFO);
	for (j = 0; j < 3; j++) {
		if (!twiddle_thumbs(port, unit, 3000, "getting version info"))
			return 0;
		stbytes[j] = (inb(port+MCD_DATA) & 0xFF);
	}
	if (stbytes[1] == stbytes[2])
		return 0;
	if (stbytes[2] >= 4 || stbytes[1] != 'M') {
		outb(port+MCD_CTRL, M_PICKLE);
		mcd_data[unit].flags |= MCDNEWMODEL;
	}
	mcd_data[unit].read_command = MCD_CMDSINGLESPEEDREAD;
	switch (stbytes[1]) {
	case 'M':
		if (stbytes[2] <= 2) {
			mcd_data[unit].type = MCD_TYPE_LU002S;
			mcd_data[unit].name = "Mitsumi LU002S";
		} else if (stbytes[2] <= 5) {
			mcd_data[unit].type = MCD_TYPE_LU005S;
			mcd_data[unit].name = "Mitsumi LU005S";
		} else {
			mcd_data[unit].type = MCD_TYPE_LU006S;
			mcd_data[unit].name = "Mitsumi LU006S";
		}
		break;
	case 'F':
		mcd_data[unit].type = MCD_TYPE_FX001;
		mcd_data[unit].name = "Mitsumi FX001";
		break;
	case 'D':
		mcd_data[unit].type = MCD_TYPE_FX001D;
		mcd_data[unit].name = "Mitsumi FX001D";
		mcd_data[unit].read_command = MCD_CMDDOUBLESPEEDREAD;
		break;
	default:
		mcd_data[unit].type = MCD_TYPE_UNKNOWN;
		mcd_data[unit].name = "Mitsumi ???";
		break;
	}
	printf("mcd%d: type %s, version info: %c %x\n", unit, mcd_data[unit].name,
		stbytes[1], stbytes[2]);

	return 4;
}


static int
mcd_waitrdy(int port,int dly)
{
	int i;

	/* wait until flag port senses status ready */
	for (i=0; i<dly; i+=MIN_DELAY) {
		if (!(inb(port+MCD_FLAGS) & MFL_STATUS_NOT_AVAIL))
			return 0;
		DELAY(MIN_DELAY);
	}
	return -1;
}

static int
mcd_getreply(int unit,int dly)
{
	struct	mcd_data *cd = mcd_data + unit;
	int	port = cd->iobase;

	/* wait data to become ready */
	if (mcd_waitrdy(port,dly)<0) {
		printf("mcd%d: timeout getreply\n",unit);
		return -1;
	}

	/* get the data */
	return inb(port+mcd_status) & 0xFF;
}

static int
mcd_getstat(int unit,int sflg)
{
	int	i;
	struct	mcd_data *cd = mcd_data + unit;
	int	port = cd->iobase;

	/* get the status */
	if (sflg)
		outb(port+mcd_command, MCD_CMDGETSTAT);
	i = mcd_getreply(unit,DELAY_GETREPLY);
	if (i<0 || (i & MCD_ST_CMDCHECK)) {
		cd->curr_mode = MCD_MD_UNKNOWN;
		return -1;
	}

	cd->status = i;

	if (mcd_setflags(unit,cd) < 0)
		return -2;
	return cd->status;
}

static int
mcd_setflags(int unit, struct mcd_data *cd)
{
	/* check flags */
	if (    (cd->status & (MCDDSKCHNG|MCDDOOROPEN))
	    || !(cd->status & MCDDSKIN)) {
		MCD_TRACE("setflags: sensed DSKCHNG or DOOROPEN or !DSKIN\n");
		mcd_soft_reset(unit);
		return -1;
	}

	if (cd->status & MCDAUDIOBSY)
		cd->audio_status = CD_AS_PLAY_IN_PROGRESS;
	else if (cd->audio_status == CD_AS_PLAY_IN_PROGRESS)
		cd->audio_status = CD_AS_PLAY_COMPLETED;
	return 0;
}

static int
mcd_get(int unit, char *buf, int nmax)
{
	int i,k;

	for (i=0; i<nmax; i++) {
		/* wait for data */
		if ((k = mcd_getreply(unit,DELAY_GETREPLY)) < 0) {
			printf("mcd%d: timeout mcd_get\n",unit);
			return -1;
		}
		buf[i] = k;
	}
	return i;
}

static int
mcd_send(int unit, int cmd,int nretrys)
{
	int i,k=0;
	int port = mcd_data[unit].iobase;

/*MCD_TRACE("mcd_send: command = 0x%02x\n",cmd,0,0,0);*/
	for (i=0; i<nretrys; i++) {
		outb(port+mcd_command, cmd);
		if ((k=mcd_getstat(unit,0)) != -1)
			break;
	}
	if (k == -2) {
		printf("mcd%d: media changed\n",unit);
		return -1;
	}
	if (i == nretrys) {
		printf("mcd%d: mcd_send retry cnt exceeded\n",unit);
		return -1;
	}
/*MCD_TRACE("mcd_send: done\n",0,0,0,0);*/
	return 0;
}

static void
hsg2msf(int hsg, bcd_t *msf)
{
	hsg += 150;
	F_msf(msf) = bin2bcd(hsg % 75);
	hsg /= 75;
	S_msf(msf) = bin2bcd(hsg % 60);
	hsg /= 60;
	M_msf(msf) = bin2bcd(hsg);
}

static int
msf2hsg(bcd_t *msf, int relative)
{
	return (bcd2bin(M_msf(msf)) * 60 + bcd2bin(S_msf(msf))) * 75 +
		bcd2bin(F_msf(msf)) - (!relative) * 150;
}

static int
mcd_volinfo(int unit)
{
	struct mcd_data *cd = mcd_data + unit;

	/* Just return if we already have it */
	if (cd->flags & MCDVOLINFO) return 0;

/*MCD_TRACE("mcd_volinfo: enter\n",0,0,0,0);*/

	/* send volume info command */
	if (mcd_send(unit,MCD_CMDGETVOLINFO,MCD_RETRYS) < 0)
		return EIO;

	/* get data */
	if (mcd_get(unit,(char*) &cd->volinfo,sizeof(struct mcd_volinfo)) < 0) {
		printf("mcd%d: mcd_volinfo: error read data\n",unit);
		return EIO;
	}

	if (cd->volinfo.trk_low > 0 &&
	    cd->volinfo.trk_high >= cd->volinfo.trk_low
	   ) {
		cd->flags |= MCDVOLINFO;	/* volinfo is OK */
		return 0;
	}

	return EINVAL;
}

static void
mcdintr(unit)
	int unit;
{
	MCD_TRACE("stray interrupt\n");
}

/* state machine to process read requests
 * initialize with MCD_S_BEGIN: calculate sizes, and read status
 * MCD_S_WAITSTAT: wait for status reply, set mode
 * MCD_S_WAITMODE: waits for status reply from set mode, set read command
 * MCD_S_WAITREAD: wait for read ready, read data
 */
static struct mcd_mbx *mbxsave;
static struct callout_handle tohandle = CALLOUT_HANDLE_INITIALIZER(&tohandle);

static void
mcd_timeout(void *arg)
{
	mcd_doread((int)arg, mbxsave);
}

static void
mcd_doread(int state, struct mcd_mbx *mbxin)
{
	struct mcd_mbx *mbx = (state!=MCD_S_BEGIN) ? mbxsave : mbxin;
	int	unit = mbx->unit;
	int	port = mbx->port;
	int     com_port = mbx->port + mcd_command;
	int     data_port = mbx->port + mcd_rdata;
	struct	buf *bp = mbx->bp;
	struct	mcd_data *cd = mcd_data + unit;

	int	rm,i,k;
	struct mcd_read2 rbuf;
	int	blknum;
	caddr_t	addr;

loop:
	switch (state) {
	case MCD_S_BEGIN:
		mbx = mbxsave = mbxin;

	case MCD_S_BEGIN1:
retry_status:
		/* get status */
		outb(com_port, MCD_CMDGETSTAT);
		mbx->count = RDELAY_WAITSTAT;
		tohandle = timeout(mcd_timeout,
				   (caddr_t)MCD_S_WAITSTAT,hz/100); /* XXX */
		return;
	case MCD_S_WAITSTAT:
		untimeout(mcd_timeout,(caddr_t)MCD_S_WAITSTAT, tohandle);
		if (mbx->count-- >= 0) {
			if (inb(port+MCD_FLAGS) & MFL_STATUS_NOT_AVAIL) {
				timeout(mcd_timeout,
				    (caddr_t)MCD_S_WAITSTAT,hz/100); /* XXX */
				return;
			}
			cd->status = inb(port+mcd_status) & 0xFF;
			if (cd->status & MCD_ST_CMDCHECK)
				goto retry_status;
			if (mcd_setflags(unit,cd) < 0)
				goto changed;
			MCD_TRACE("got WAITSTAT delay=%d\n",
				RDELAY_WAITSTAT-mbx->count);
			/* reject, if audio active */
			if (cd->status & MCDAUDIOBSY) {
				printf("mcd%d: audio is active\n",unit);
				goto readerr;
			}

retry_mode:
			/* to check for raw/cooked mode */
			if (cd->flags & MCDREADRAW) {
				rm = MCD_MD_RAW;
				mbx->sz = MCDRBLK;
			} else {
				rm = MCD_MD_COOKED;
				mbx->sz = cd->blksize;
			}

			if (rm == cd->curr_mode)
				goto modedone;

			mbx->count = RDELAY_WAITMODE;

			cd->curr_mode = MCD_MD_UNKNOWN;
			mbx->mode = rm;
			mcd_put(com_port, MCD_CMDSETMODE);
			mcd_put(com_port, rm);

			tohandle = timeout(mcd_timeout,
					   (caddr_t)MCD_S_WAITMODE,hz/100); /* XXX */
			return;
		} else {
			printf("mcd%d: timeout getstatus\n",unit);
			goto readerr;
		}

	case MCD_S_WAITMODE:
		untimeout(mcd_timeout,(caddr_t)MCD_S_WAITMODE, tohandle);
		if (mbx->count-- < 0) {
			printf("mcd%d: timeout set mode\n",unit);
			goto readerr;
		}
		if (inb(port+MCD_FLAGS) & MFL_STATUS_NOT_AVAIL) {
			tohandle = timeout(mcd_timeout,
					   (caddr_t)MCD_S_WAITMODE,hz/100);
			return;
		}
		cd->status = inb(port+mcd_status) & 0xFF;
		if (cd->status & MCD_ST_CMDCHECK) {
			cd->curr_mode = MCD_MD_UNKNOWN;
			goto retry_mode;
		}
		if (mcd_setflags(unit,cd) < 0)
			goto changed;
		cd->curr_mode = mbx->mode;
		MCD_TRACE("got WAITMODE delay=%d\n",
			RDELAY_WAITMODE-mbx->count);
modedone:
		/* for first block */
		mbx->nblk = (bp->b_bcount + (mbx->sz-1)) / mbx->sz;
		mbx->skip = 0;

nextblock:
		blknum 	= (bp->b_blkno / (mbx->sz/DEV_BSIZE))
			+ mbx->p_offset + mbx->skip/mbx->sz;

		MCD_TRACE("mcd_doread: read blknum=%d for bp=%p\n",
			blknum, bp);

		/* build parameter block */
		hsg2msf(blknum,rbuf.start_msf);
retry_read:
		/* send the read command */
		disable_intr();
		mcd_put(com_port,cd->read_command);
		mcd_put(com_port,rbuf.start_msf[0]);
		mcd_put(com_port,rbuf.start_msf[1]);
		mcd_put(com_port,rbuf.start_msf[2]);
		mcd_put(com_port,0);
		mcd_put(com_port,0);
		mcd_put(com_port,1);
		enable_intr();

		/* Spin briefly (<= 2ms) to avoid missing next block */
		for (i = 0; i < 20; i++) {
			k = inb(port+MCD_FLAGS);
			if (!(k & MFL_DATA_NOT_AVAIL))
				goto got_it;
			DELAY(100);
		}

		mbx->count = RDELAY_WAITREAD;
		tohandle = timeout(mcd_timeout,
				   (caddr_t)MCD_S_WAITREAD,hz/100); /* XXX */
		return;
	case MCD_S_WAITREAD:
		untimeout(mcd_timeout,(caddr_t)MCD_S_WAITREAD, tohandle);
		if (mbx->count-- > 0) {
			k = inb(port+MCD_FLAGS);
			if (!(k & MFL_DATA_NOT_AVAIL)) { /* XXX */
				MCD_TRACE("got data delay=%d\n",
					RDELAY_WAITREAD-mbx->count);
			got_it:
				/* data is ready */
				addr	= bp->b_data + mbx->skip;

				outb(port+mcd_ctl2,0x04);	/* XXX */
				for (i=0; i<mbx->sz; i++)
					*addr++ = inb(data_port);
				outb(port+mcd_ctl2,0x0c);	/* XXX */

				k = inb(port+MCD_FLAGS);
				/* If we still have some junk, read it too */
				if (!(k & MFL_DATA_NOT_AVAIL)) {
					outb(port+mcd_ctl2,0x04);       /* XXX */
					(void)inb(data_port);
					(void)inb(data_port);
					outb(port+mcd_ctl2,0x0c);       /* XXX */
				}

				if (--mbx->nblk > 0) {
					mbx->skip += mbx->sz;
					goto nextblock;
				}

				/* return buffer */
				bp->b_resid = 0;
				biodone(bp);

				cd->flags &= ~(MCDMBXBSY|MCDREADRAW);
				mcd_start(mbx->unit);
				return;
			}
			if (!(k & MFL_STATUS_NOT_AVAIL)) {
				cd->status = inb(port+mcd_status) & 0xFF;
				if (cd->status & MCD_ST_CMDCHECK)
					goto retry_read;
				if (mcd_setflags(unit,cd) < 0)
					goto changed;
			}
			tohandle = timeout(mcd_timeout,
					   (caddr_t)MCD_S_WAITREAD,hz/100); /* XXX */
			return;
		} else {
			printf("mcd%d: timeout read data\n",unit);
			goto readerr;
		}
	}

readerr:
	if (mbx->retry-- > 0) {
		printf("mcd%d: retrying\n",unit);
		state = MCD_S_BEGIN1;
		goto loop;
	}
harderr:
	/* invalidate the buffer */
	bp->b_flags |= B_ERROR;
	bp->b_resid = bp->b_bcount;
	biodone(bp);

	cd->flags &= ~(MCDMBXBSY|MCDREADRAW);
	mcd_start(mbx->unit);
	return;

changed:
	printf("mcd%d: media changed\n", unit);
	goto harderr;

#ifdef NOTDEF
	printf("mcd%d: unit timeout, resetting\n",mbx->unit);
	outb(mbx->port+mcd_reset,MCD_CMDRESET);
	DELAY(300000);
	(void)mcd_getstat(mbx->unit,1);
	(void)mcd_getstat(mbx->unit,1);
	/*cd->status &= ~MCDDSKCHNG; */
	cd->debug = 1; /* preventive set debug mode */

#endif

}

static int
mcd_lock_door(int unit, int lock)
{
	struct mcd_data *cd = mcd_data + unit;
	int port = cd->iobase;

	outb(port+mcd_command, MCD_CMDLOCKDRV);
	outb(port+mcd_command, lock);
	if (mcd_getstat(unit,0) == -1)
		return EIO;
	return 0;
}

static int
mcd_close_tray(int unit)
{
	struct mcd_data *cd = mcd_data + unit;
	int port = cd->iobase;
	int retry, r;

	if (mcd_getstat(unit,1) == -1)
		return EIO;
	if (cd->status & MCDDOOROPEN) {
		outb(port+mcd_command, MCD_CMDCLOSETRAY);
		for (retry = 0; retry < CLOSE_TRAY_SECS * WAIT_FRAC; retry++) {
			if (inb(port+MCD_FLAGS) & MFL_STATUS_NOT_AVAIL)
				(void) tsleep((caddr_t)cd, PSOCK | PCATCH, "mcdcls", hz/WAIT_FRAC);
			else {
				if ((r = mcd_getstat(unit,0)) == -1)
					return EIO;
				return 0;
			}
		}
		return ENXIO;
	}
	return 0;
}

static int
mcd_eject(int unit)
{
	struct mcd_data *cd = mcd_data + unit;
	int port = cd->iobase, r;

	if (mcd_getstat(unit,1) == -1)    /* detect disk change too */
		return EIO;
	if (cd->status & MCDDOOROPEN)
		return 0;
	if ((r = mcd_stop(unit)) == EIO)
		return r;
	outb(port+mcd_command, MCD_CMDEJECTDISK);
	if (mcd_getstat(unit,0) == -1)
		return EIO;
	return 0;
}

static int
mcd_inject(int unit)
{
	struct mcd_data *cd = mcd_data + unit;

	if (mcd_getstat(unit,1) == -1)    /* detect disk change too */
		return EIO;
	if (cd->status & MCDDOOROPEN)
		return mcd_close_tray(unit);
	return 0;
}

static int
mcd_hard_reset(int unit)
{
	struct mcd_data *cd = mcd_data + unit;
	int port = cd->iobase;

	outb(port+mcd_reset,MCD_CMDRESET);
	cd->curr_mode = MCD_MD_UNKNOWN;
	cd->audio_status = CD_AS_AUDIO_INVALID;
	return 0;
}

static void
mcd_soft_reset(int unit)
{
	struct mcd_data *cd = mcd_data + unit;
	int i;

	cd->flags &= (MCDINIT|MCDPROBING|MCDNEWMODEL);
	cd->curr_mode = MCD_MD_UNKNOWN;
	for (i=0; i<MAXPARTITIONS; i++) cd->partflags[i] = 0;
	cd->audio_status = CD_AS_AUDIO_INVALID;
}

static int
mcd_setmode(int unit, int mode)
{
	struct mcd_data *cd = mcd_data + unit;
	int port = cd->iobase;
	int retry, st;

	if (cd->curr_mode == mode)
		return 0;
	if (cd->debug)
		printf("mcd%d: setting mode to %d\n", unit, mode);
	for(retry=0; retry<MCD_RETRYS; retry++)
	{
		cd->curr_mode = MCD_MD_UNKNOWN;
		outb(port+mcd_command, MCD_CMDSETMODE);
		outb(port+mcd_command, mode);
		if ((st = mcd_getstat(unit, 0)) >= 0) {
			cd->curr_mode = mode;
			return 0;
		}
		if (st == -2) {
			printf("mcd%d: media changed\n", unit);
			break;
		}
	}

	return -1;
}

static int
mcd_toc_header(int unit, struct ioc_toc_header *th)
{
	struct mcd_data *cd = mcd_data + unit;
	int r;

	if ((r = mcd_volinfo(unit)) != 0)
		return r;

	th->starting_track = bcd2bin(cd->volinfo.trk_low);
	th->ending_track = bcd2bin(cd->volinfo.trk_high);
	th->len = 2 * sizeof(u_char) /* start & end tracks */ +
		  (th->ending_track + 1 - th->starting_track + 1) *
		  sizeof(struct cd_toc_entry);

	return 0;
}

static int
mcd_read_toc(int unit)
{
	struct mcd_data *cd = mcd_data + unit;
	struct ioc_toc_header th;
	struct mcd_qchninfo q;
	int rc, trk, idx, retry;

	/* Only read TOC if needed */
	if (cd->flags & MCDTOC)
		return 0;

	if (cd->debug)
		printf("mcd%d: reading toc header\n", unit);

	if ((rc = mcd_toc_header(unit, &th)) != 0)
		return rc;

	if (mcd_send(unit, MCD_CMDSTOPAUDIO, MCD_RETRYS) < 0)
		return EIO;

	if (mcd_setmode(unit, MCD_MD_TOC) != 0)
		return EIO;

	if (cd->debug)
		printf("mcd%d: get_toc reading qchannel info\n",unit);

	for(trk=th.starting_track; trk<=th.ending_track; trk++)
		cd->toc[trk].idx_no = 0;
	trk = th.ending_track - th.starting_track + 1;
	for(retry=0; retry<600 && trk>0; retry++)
	{
		if (mcd_getqchan(unit, &q) < 0) break;
		idx = bcd2bin(q.idx_no);
		if (idx>=th.starting_track && idx<=th.ending_track && q.trk_no==0) {
			if (cd->toc[idx].idx_no == 0) {
				cd->toc[idx] = q;
				trk--;
			}
		}
	}

	if (mcd_setmode(unit, MCD_MD_COOKED) != 0)
		return EIO;

	if (trk != 0)
		return ENXIO;

	/* add a fake last+1 */
	idx = th.ending_track + 1;
	cd->toc[idx].control = cd->toc[idx-1].control;
	cd->toc[idx].addr_type = cd->toc[idx-1].addr_type;
	cd->toc[idx].trk_no = 0;
	cd->toc[idx].idx_no = MCD_LASTPLUS1;
	cd->toc[idx].hd_pos_msf[0] = cd->volinfo.vol_msf[0];
	cd->toc[idx].hd_pos_msf[1] = cd->volinfo.vol_msf[1];
	cd->toc[idx].hd_pos_msf[2] = cd->volinfo.vol_msf[2];

	if (cd->debug)
	{ int i;
	for (i = th.starting_track; i <= idx; i++)
		printf("mcd%d: trk %d idx %d pos %d %d %d\n",
			unit, i,
			cd->toc[i].idx_no > 0x99 ? cd->toc[i].idx_no :
			bcd2bin(cd->toc[i].idx_no),
			bcd2bin(cd->toc[i].hd_pos_msf[0]),
			bcd2bin(cd->toc[i].hd_pos_msf[1]),
			bcd2bin(cd->toc[i].hd_pos_msf[2]));
	}

	cd->flags |= MCDTOC;

	return 0;
}

#if 0
static int
mcd_toc_entry(int unit, struct ioc_read_toc_single_entry *te)
{
	struct mcd_data *cd = mcd_data + unit;
	struct ioc_toc_header th;
	int rc, trk;

	if (te->address_format != CD_MSF_FORMAT
	    && te->address_format != CD_LBA_FORMAT)
		return EINVAL;

	/* Copy the toc header */
	if ((rc = mcd_toc_header(unit, &th)) != 0)
		return rc;

	/* verify starting track */
	trk = te->track;
	if (trk == 0)
		trk = th.starting_track;
	else if (trk == MCD_LASTPLUS1)
		trk = th.ending_track + 1;
	else if (trk < th.starting_track || trk > th.ending_track + 1)
		return EINVAL;

	/* Make sure we have a valid toc */
	if ((rc=mcd_read_toc(unit)) != 0)
		return rc;

	/* Copy the TOC data. */
	if (cd->toc[trk].idx_no == 0)
		return EIO;

	te->entry.control = cd->toc[trk].control;
	te->entry.addr_type = cd->toc[trk].addr_type;
	te->entry.track =
		cd->toc[trk].idx_no > 0x99 ? cd->toc[trk].idx_no :
		bcd2bin(cd->toc[trk].idx_no);
	switch (te->address_format) {
	case CD_MSF_FORMAT:
		te->entry.addr.msf.unused = 0;
		te->entry.addr.msf.minute = bcd2bin(cd->toc[trk].hd_pos_msf[0]);
		te->entry.addr.msf.second = bcd2bin(cd->toc[trk].hd_pos_msf[1]);
		te->entry.addr.msf.frame = bcd2bin(cd->toc[trk].hd_pos_msf[2]);
		break;
	case CD_LBA_FORMAT:
		te->entry.addr.lba = htonl(msf2hsg(cd->toc[trk].hd_pos_msf, 0));
		break;
	}
	return 0;
}
#endif

static int
mcd_toc_entrys(int unit, struct ioc_read_toc_entry *te)
{
	struct mcd_data *cd = mcd_data + unit;
	struct cd_toc_entry entries[MCD_MAXTOCS];
	struct ioc_toc_header th;
	int rc, n, trk, len;

	if (   te->data_len < sizeof(entries[0])
	    || (te->data_len % sizeof(entries[0])) != 0
	    || (te->address_format != CD_MSF_FORMAT
	        && te->address_format != CD_LBA_FORMAT)
	   )
		return EINVAL;

	/* Copy the toc header */
	if ((rc = mcd_toc_header(unit, &th)) != 0)
		return rc;

	/* verify starting track */
	trk = te->starting_track;
	if (trk == 0)
		trk = th.starting_track;
	else if (trk == MCD_LASTPLUS1)
		trk = th.ending_track + 1;
	else if (trk < th.starting_track || trk > th.ending_track + 1)
		return EINVAL;

	len = ((th.ending_track + 1 - trk) + 1) *
		sizeof(entries[0]);
	if (te->data_len < len)
		len = te->data_len;
	if (len > sizeof(entries))
		return EINVAL;

	/* Make sure we have a valid toc */
	if ((rc=mcd_read_toc(unit)) != 0)
		return rc;

	/* Copy the TOC data. */
	for (n = 0; len > 0 && trk <= th.ending_track + 1; trk++) {
		if (cd->toc[trk].idx_no == 0)
			continue;
		entries[n].control = cd->toc[trk].control;
		entries[n].addr_type = cd->toc[trk].addr_type;
		entries[n].track =
			cd->toc[trk].idx_no > 0x99 ? cd->toc[trk].idx_no :
			bcd2bin(cd->toc[trk].idx_no);
		switch (te->address_format) {
		case CD_MSF_FORMAT:
			entries[n].addr.msf.unused = 0;
			entries[n].addr.msf.minute = bcd2bin(cd->toc[trk].hd_pos_msf[0]);
			entries[n].addr.msf.second = bcd2bin(cd->toc[trk].hd_pos_msf[1]);
			entries[n].addr.msf.frame = bcd2bin(cd->toc[trk].hd_pos_msf[2]);
			break;
		case CD_LBA_FORMAT:
			entries[n].addr.lba = htonl(msf2hsg(cd->toc[trk].hd_pos_msf, 0));
			break;
		}
		len -= sizeof(struct cd_toc_entry);
		n++;
	}

	/* copy the data back */
	return copyout(entries, te->data, n * sizeof(struct cd_toc_entry));
}

static int
mcd_stop(int unit)
{
	struct mcd_data *cd = mcd_data + unit;

	/* Verify current status */
	if (cd->audio_status != CD_AS_PLAY_IN_PROGRESS &&
	    cd->audio_status != CD_AS_PLAY_PAUSED &&
	    cd->audio_status != CD_AS_PLAY_COMPLETED) {
		if (cd->debug)
			printf("mcd%d: stop attempted when not playing, audio status %d\n",
				unit, cd->audio_status);
		return EINVAL;
	}
	if (cd->audio_status == CD_AS_PLAY_IN_PROGRESS)
		if (mcd_send(unit, MCD_CMDSTOPAUDIO, MCD_RETRYS) < 0)
			return EIO;
	cd->audio_status = CD_AS_PLAY_COMPLETED;
	return 0;
}

static int
mcd_getqchan(int unit, struct mcd_qchninfo *q)
{
	struct mcd_data *cd = mcd_data + unit;

	if (mcd_send(unit, MCD_CMDGETQCHN, MCD_RETRYS) < 0)
		return -1;
	if (mcd_get(unit, (char *) q, sizeof(struct mcd_qchninfo)) < 0)
		return -1;
	if (cd->debug) {
		printf("mcd%d: getqchan control=0x%x addr_type=0x%x trk=%d ind=%d ttm=%d:%d.%d dtm=%d:%d.%d\n",
		unit,
		q->control, q->addr_type, bcd2bin(q->trk_no),
		bcd2bin(q->idx_no),
		bcd2bin(q->trk_size_msf[0]), bcd2bin(q->trk_size_msf[1]),
		bcd2bin(q->trk_size_msf[2]),
		bcd2bin(q->hd_pos_msf[0]), bcd2bin(q->hd_pos_msf[1]),
		bcd2bin(q->hd_pos_msf[2]));
	}
	return 0;
}

static int
mcd_subchan(int unit, struct ioc_read_subchannel *sc)
{
	struct mcd_data *cd = mcd_data + unit;
	struct mcd_qchninfo q;
	struct cd_sub_channel_info data;
	int lba;

	if (cd->debug)
		printf("mcd%d: subchan af=%d, df=%d\n", unit,
			sc->address_format,
			sc->data_format);

	if (sc->address_format != CD_MSF_FORMAT &&
	    sc->address_format != CD_LBA_FORMAT)
		return EINVAL;

	if (sc->data_format != CD_CURRENT_POSITION &&
	    sc->data_format != CD_MEDIA_CATALOG)
		return EINVAL;

	if (mcd_setmode(unit, MCD_MD_COOKED) != 0)
		return EIO;

	if (mcd_getqchan(unit, &q) < 0)
		return EIO;

	data.header.audio_status = cd->audio_status;
	data.what.position.data_format = sc->data_format;

	switch (sc->data_format) {
	case CD_MEDIA_CATALOG:
		data.what.media_catalog.mc_valid = 1;
		data.what.media_catalog.mc_number[0] = '\0';
		break;

	case CD_CURRENT_POSITION:
		data.what.position.control = q.control;
		data.what.position.addr_type = q.addr_type;
		data.what.position.track_number = bcd2bin(q.trk_no);
		data.what.position.index_number = bcd2bin(q.idx_no);
		switch (sc->address_format) {
		case CD_MSF_FORMAT:
			data.what.position.reladdr.msf.unused = 0;
			data.what.position.reladdr.msf.minute = bcd2bin(q.trk_size_msf[0]);
			data.what.position.reladdr.msf.second = bcd2bin(q.trk_size_msf[1]);
			data.what.position.reladdr.msf.frame = bcd2bin(q.trk_size_msf[2]);
			data.what.position.absaddr.msf.unused = 0;
			data.what.position.absaddr.msf.minute = bcd2bin(q.hd_pos_msf[0]);
			data.what.position.absaddr.msf.second = bcd2bin(q.hd_pos_msf[1]);
			data.what.position.absaddr.msf.frame = bcd2bin(q.hd_pos_msf[2]);
			break;
		case CD_LBA_FORMAT:
			lba = msf2hsg(q.trk_size_msf, 1);
			/*
			 * Pre-gap has index number of 0, and decreasing MSF
			 * address.  Must be converted to negative LBA, per
			 * SCSI spec.
			 */
			if (data.what.position.index_number == 0)
				lba = -lba;
			data.what.position.reladdr.lba = htonl(lba);
			data.what.position.absaddr.lba = htonl(msf2hsg(q.hd_pos_msf, 0));
			break;
		}
		break;
	}

	return copyout(&data, sc->data, min(sizeof(struct cd_sub_channel_info), sc->data_len));
}

static int
mcd_playmsf(int unit, struct ioc_play_msf *p)
{
	struct mcd_data *cd = mcd_data + unit;
	struct mcd_read2 pb;

	if (cd->debug)
		printf("mcd%d: playmsf: from %d:%d.%d to %d:%d.%d\n",
		    unit,
		    p->start_m, p->start_s, p->start_f,
		    p->end_m, p->end_s, p->end_f);

	if ((p->start_m * 60 * 75 + p->start_s * 75 + p->start_f) >=
	    (p->end_m * 60 * 75 + p->end_s * 75 + p->end_f) ||
	    (p->end_m * 60 * 75 + p->end_s * 75 + p->end_f) >
	    M_msf(cd->volinfo.vol_msf) * 60 * 75 +
	    S_msf(cd->volinfo.vol_msf) * 75 +
	    F_msf(cd->volinfo.vol_msf))
		return EINVAL;

	pb.start_msf[0] = bin2bcd(p->start_m);
	pb.start_msf[1] = bin2bcd(p->start_s);
	pb.start_msf[2] = bin2bcd(p->start_f);
	pb.end_msf[0] = bin2bcd(p->end_m);
	pb.end_msf[1] = bin2bcd(p->end_s);
	pb.end_msf[2] = bin2bcd(p->end_f);

	if (mcd_setmode(unit, MCD_MD_COOKED) != 0)
		return EIO;

	return mcd_play(unit, &pb);
}

static int
mcd_playtracks(int unit, struct ioc_play_track *pt)
{
	struct mcd_data *cd = mcd_data + unit;
	struct mcd_read2 pb;
	int a = pt->start_track;
	int z = pt->end_track;
	int rc, i;

	if ((rc = mcd_read_toc(unit)) != 0)
		return rc;

	if (cd->debug)
		printf("mcd%d: playtracks from %d:%d to %d:%d\n", unit,
			a, pt->start_index, z, pt->end_index);

	if (   a < bcd2bin(cd->volinfo.trk_low)
	    || a > bcd2bin(cd->volinfo.trk_high)
	    || a > z
	    || z < bcd2bin(cd->volinfo.trk_low)
	    || z > bcd2bin(cd->volinfo.trk_high))
		return EINVAL;

	for (i = 0; i < 3; i++) {
		pb.start_msf[i] = cd->toc[a].hd_pos_msf[i];
		pb.end_msf[i] = cd->toc[z+1].hd_pos_msf[i];
	}

	if (mcd_setmode(unit, MCD_MD_COOKED) != 0)
		return EIO;

	return mcd_play(unit, &pb);
}

static int
mcd_playblocks(int unit, struct ioc_play_blocks *p)
{
	struct mcd_data *cd = mcd_data + unit;
	struct mcd_read2 pb;

	if (cd->debug)
		printf("mcd%d: playblocks: blkno %d length %d\n",
		    unit, p->blk, p->len);

	if (p->blk > cd->disksize || p->len > cd->disksize ||
	    p->blk < 0 || p->len < 0 ||
	    (p->blk + p->len) > cd->disksize)
		return EINVAL;

	hsg2msf(p->blk, pb.start_msf);
	hsg2msf(p->blk + p->len, pb.end_msf);

	if (mcd_setmode(unit, MCD_MD_COOKED) != 0)
		return EIO;

	return mcd_play(unit, &pb);
}

static int
mcd_play(int unit, struct mcd_read2 *pb)
{
	struct mcd_data *cd = mcd_data + unit;
	int com_port = cd->iobase + mcd_command;
	int retry, st = -1, status;

	cd->lastpb = *pb;
	for(retry=0; retry<MCD_RETRYS; retry++) {

		disable_intr();
		outb(com_port, MCD_CMDSINGLESPEEDREAD);
		outb(com_port, pb->start_msf[0]);
		outb(com_port, pb->start_msf[1]);
		outb(com_port, pb->start_msf[2]);
		outb(com_port, pb->end_msf[0]);
		outb(com_port, pb->end_msf[1]);
		outb(com_port, pb->end_msf[2]);
		enable_intr();

		status=mcd_getstat(unit, 0);
		if (status == -1)
			continue;
		else if (status != -2)
			st = 0;
		break;
	}

	if (status == -2) {
		printf("mcd%d: media changed\n", unit);
		return ENXIO;
	}
	if (cd->debug)
		printf("mcd%d: mcd_play retry=%d, status=0x%02x\n", unit, retry, status);
	if (st < 0)
		return ENXIO;
	cd->audio_status = CD_AS_PLAY_IN_PROGRESS;
	return 0;
}

static int
mcd_pause(int unit)
{
	struct mcd_data *cd = mcd_data + unit;
	struct mcd_qchninfo q;
	int rc;

	/* Verify current status */
	if (cd->audio_status != CD_AS_PLAY_IN_PROGRESS &&
	    cd->audio_status != CD_AS_PLAY_PAUSED) {
		if (cd->debug)
			printf("mcd%d: pause attempted when not playing, audio status %d\n",
			       unit, cd->audio_status);
		return EINVAL;
	}

	/* Get the current position */
	if (mcd_getqchan(unit, &q) < 0)
		return EIO;

	/* Copy it into lastpb */
	cd->lastpb.start_msf[0] = q.hd_pos_msf[0];
	cd->lastpb.start_msf[1] = q.hd_pos_msf[1];
	cd->lastpb.start_msf[2] = q.hd_pos_msf[2];

	/* Stop playing */
	if ((rc=mcd_stop(unit)) != 0)
		return rc;

	/* Set the proper status and exit */
	cd->audio_status = CD_AS_PLAY_PAUSED;
	return 0;
}

static int
mcd_resume(int unit)
{
	struct mcd_data *cd = mcd_data + unit;

	if (cd->audio_status != CD_AS_PLAY_PAUSED)
		return EINVAL;
	return mcd_play(unit, &cd->lastpb);
}
