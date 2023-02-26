/*
 * Copyright (c) 1999 Hellmuth Michaelis
 *
 * Copyright (c) 1992, 1995 Hellmuth Michaelis and Joerg Wunsch.
 *
 * Copyright (c) 1992, 1993 Brian Dunford-Shore and Scott Turner.
 *
 * Copyright (c) 1993 Charles Hannum.
 *
 * All rights reserved.
 *
 * Parts of this code regarding the NetBSD interface were written
 * by Charles Hannum.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz and Don Ahn.
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
 *	This product includes software developed by
 *	Hellmuth Michaelis, Brian Dunford-Shore, Joerg Wunsch, Scott Turner
 *	and Charles Hannum.
 * 4. The name authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*---------------------------------------------------------------------------*
 *
 *	pcvt_drv.c	VT220 Driver Main Module / OS - Interface
 *	---------------------------------------------------------
 *
 *	Last Edit-Date: [Mon Dec 27 14:03:36 1999]
 *
 * $FreeBSD: src/sys/i386/isa/pcvt/pcvt_drv.c,v 1.63.2.1 2001/02/26 04:23:13 jlemon Exp $
 *
 *---------------------------------------------------------------------------*/

#include "vt.h"
#if NVT > 0

#define EXTERN			/* allocate mem */

#include <i386/isa/pcvt/pcvt_hdr.h>	/* global include */

#if PCVT_FREEBSD >= 200
#include <sys/bus.h>
#include <machine/stdarg.h>
#else
#include "machine/stdarg.h"
#endif

extern int getchar __P((void));

#if PCVT_NETBSD
	extern u_short *Crtat;
#endif /* PCVT_NETBSD */

static void vgapelinit(void);	/* read initial VGA DAC palette */

#if defined XSERVER && !PCVT_USL_VT_COMPAT
static int pcvt_xmode_set(int on, struct proc *p); /* initialize for X mode */
#endif /* XSERVER && !PCVT_USL_VT_COMPAT */

#ifdef _DEV_KBD_KBDREG_H_
static void detect_kbd(void *arg);
static kbd_callback_func_t pcevent;
#endif

static cn_probe_t	pccnprobe;
static cn_init_t	pccninit;
static cn_term_t	pccnterm;
static cn_getc_t	pccngetc;
static cn_checkc_t	pccncheckc;
static cn_putc_t	pccnputc;

CONS_DRIVER(pc, pccnprobe, pccninit, pccnterm, pccngetc, pccncheckc, pccnputc,
	    NULL);

static	d_open_t	pcopen;
static	d_close_t	pcclose;
static	d_ioctl_t	pcioctl;
static	d_mmap_t	pcmmap;

#define	CDEV_MAJOR	12
static struct cdevsw pc_cdevsw = {
	/* open */	pcopen,
	/* close */	pcclose,
	/* read */	ttyread,
	/* write */	ttywrite,
	/* ioctl */	pcioctl,
	/* poll */	ttypoll,
	/* mmap */	pcmmap,
	/* strategy */	nostrategy,
	/* name */	"vt",
	/* maj */	CDEV_MAJOR,
	/* dump */	nodump,
	/* psize */	nopsize,
	/* flags */	D_TTY | D_KQFILTER,
	/* bmaj */	-1,
	/* kqfilter */	ttykqfilter,
};

#if PCVT_NETBSD > 100	/* NetBSD-current Feb 20 1995 */
int
pcprobe(struct device *parent, void *match, void *aux)
#else
#if PCVT_NETBSD > 9
int
pcprobe(struct device *parent, struct device *self, void *aux)
#else
int
pcprobe(struct isa_device *dev)
#endif /* PCVT_NETBSD > 9 */
#endif /* PCVT_NETBSD > 100 */
{
#ifdef _DEV_KBD_KBDREG_H_
	int i;

	if (kbd == NULL) {
		reset_keyboard = 0;
		kbd_configure(KB_CONF_PROBE_ONLY);
		i = kbd_allocate("*", -1, (void *)&kbd, pcevent, (void *)dev->id_unit);
		if ((i < 0) || ((kbd = kbd_get_keyboard(i)) == NULL))
			return (-1);
	}
	reset_keyboard = 1;		/* it's now safe to do kbd reset */
#endif /* _DEV_KBD_KBDREG_H_ */

	kbd_code_init();

#if PCVT_NETBSD > 9
	((struct isa_attach_args *)aux)->ia_iosize = 16;
	return 1;
#else
#ifdef _DEV_KBD_KBDREG_H_
	return (-1);
#elif PCVT_NETBSD || PCVT_FREEBSD
	return (16);
#else
	return 1;
#endif /* PCVT_NETBSD || PCVT_FREEBSD */
#endif /* PCVT_NETBSD > 9 */

}

#if PCVT_NETBSD > 9
void
pcattach(struct device *parent, struct device *self, void *aux)
{
	struct isa_attach_args *ia = aux;
	static struct intrhand vthand;
#else
int
pcattach(struct isa_device *dev)
{
#endif /* PCVT_NETBSD > 9 */

	int i;

	vt_coldmalloc();		/* allocate memory for screens */

#ifdef _DEV_KBD_KBDREG_H_
	if (kbd == NULL)
		timeout(detect_kbd, (void *)dev->id_unit, hz*2);
#endif /* _DEV_KBD_KBDREG_H_ */

#if PCVT_NETBSD || PCVT_FREEBSD

#if PCVT_NETBSD > 9
	printf(": ");
#else
	printf("vt%d: ", dev->id_unit);
#endif /* PCVT_NETBSD > 9 */

	switch(adaptor_type)
	{
		case MDA_ADAPTOR:
			printf("mda");
			break;

		case CGA_ADAPTOR:
			printf("cga");
			break;

		case EGA_ADAPTOR:
			printf("ega");
			break;

		case VGA_ADAPTOR:
			printf("%s, ", (char *)vga_string(vga_type));
			if(can_do_132col)
				printf("80/132 col");
			else
				printf("80 col");
			vgapelinit();
			break;

		default:
			printf("unknown");
			break;
	}

	if(color == 0)
		printf(", mono");
	else
		printf(", color");

	printf(", %d scr, ", totalscreens);

	switch(keyboard_type)
	{
		case KB_AT:
			printf("at-");
			break;

		case KB_MFII:
			printf("mf2-");
			break;

		default:
			printf("unknown ");
			break;
	}

	printf("kbd, [R%s]\n", PCVT_REL);

#if PCVT_NETBSD || (PCVT_FREEBSD > 110 && PCVT_FREEBSD < 200)

	for(i = 0; i < totalscreens; i++)
	{

#if PCVT_NETBSD
		pc_tty[i] = ttymalloc();
		vs[i].vs_tty = pc_tty[i];
#else /* !PCVT_NETBSD */
		pccons[i] = ttymalloc(pccons[i]);
		vs[i].vs_tty = pccons[i];
#endif /* PCVT_NETBSD */

	}

#if PCVT_EMU_MOUSE
#if PCVT_NETBSD
	pc_tty[totalscreens] = ttymalloc(); /* the mouse emulator tty */
#else /* !PCVT_NETBSD */
	/* the mouse emulator tty */
	pc_tty[totalscreens] = ttymalloc(pccons[totalscreens]);
#endif /* PCVT_NETBSD */
#endif /* PCVT_EMU_MOUSE */

#if PCVT_NETBSD
	pcconsp = pc_tty[0];
#else  /* !PCVT_NETBSD */
	pcconsp = pccons[0];
#endif  /* PCVT_NETBSD */

#endif /* #if PCVT_NETBSD || (PCVT_FREEBSD > 110 && PCVT_FREEBSD < 200) */

#else /* !PCVT_NETBSD && !PCVT_FREEBSD*/

	switch(adaptor_type)
	{
		case MDA_ADAPTOR:
			printf(" <mda");
			break;

		case CGA_ADAPTOR:
			printf(" <cga");
			break;

		case EGA_ADAPTOR:
			printf(" <ega");
			break;

		case VGA_ADAPTOR:
			printf(" <%s,", (char *)vga_string(vga_type));
			if(can_do_132col)
				printf("80/132 col");
			else
				printf("80 col");
			vgapelinit();
			break;

		default:
			printf(" <unknown");
			break;
	}

	if(color == 0)
		printf(",mono");
	else
		printf(",color");

	printf(",%d scr,", totalscreens);

	switch(keyboard_type)
	{
		case KB_AT:
			printf("at-");
			break;

		case KB_MFII:
			printf("mf2-");
			break;

		default:
			printf("unknown ");
			break;
	}

	printf("kbd,[R%s]>", PCVT_REL);

#endif  /* PCVT_NETBSD || PCVT_FREEBSD */

#if !PCVT_NETBSD && !(PCVT_FREEBSD > 110 && PCVT_FREEBSD < 200)
	for(i = 0; i < totalscreens; i++)
	{
		ttyregister(&pccons[i]);
		vs[i].vs_tty = &pccons[i];
		make_dev(&pc_cdevsw, i, UID_ROOT, GID_WHEEL, 0600, "ttyv%r", i);
	}
#endif /* !PCVT_NETBSD && !(PCVT_FREEBSD > 110 && PCVT_FREEBSD < 200) */

	async_update(UPDATE_START);	/* start asynchronous updates */

#if PCVT_NETBSD > 9

	vthand.ih_fun = pcrint;
	vthand.ih_arg = 0;
	vthand.ih_level = IPL_TTY;

#if (PCVT_NETBSD > 100) && defined(IST_EDGE)
	intr_establish(ia->ia_irq, IST_EDGE, &vthand);
#else /* PCVT_NETBSD > 100 */
	intr_establish(ia->ia_irq, &vthand);
#endif /* PCVT_NETBSD > 100 */

#else /* PCVT_NETBSD > 9 */

	dev->id_ointr = pcrint;

	return 1;

#endif /* PCVT_NETBSD > 9 */

}

/* had a look at the friedl driver */

#if !PCVT_NETBSD

struct tty *
get_pccons(Dev_t dev)
{
	register int i = minor(dev);

#if PCVT_EMU_MOUSE
 	if(i == totalscreens)
#if !(PCVT_FREEBSD > 110 && PCVT_FREEBSD < 200)
 		return(&pccons[i]);
#else
 		return(pccons[i]);
#endif /* !(PCVT_FREEBSD > 110 && PCVT_FREEBSD < 200) */
#endif /* PCVT_EMU_MOUSE */

	if(i >= PCVT_NSCREENS)
		return(NULL);
#if !(PCVT_FREEBSD > 110 && PCVT_FREEBSD < 200)
	return(&pccons[i]);
#else
	return(pccons[i]);
#endif
}

#else

struct tty *
get_pccons(Dev_t dev)
{
	register int i = minor(dev);

#if PCVT_EMU_MOUSE
	if(i == totalscreens)
		return(pc_tty[i]);
#endif /* PCVT_EMU_MOUSE */

	if(i >= PCVT_NSCREENS)
		return(NULL);
	return(pc_tty[i]);
}

#endif /* !PCVT_NETBSD */

/*---------------------------------------------------------------------------*
 *		/dev/ttyc0, /dev/ttyc1, etc.
 *---------------------------------------------------------------------------*/
int
pcopen(Dev_t dev, int flag, int mode, struct proc *p)
{
	register struct tty *tp;
	register struct video_state *vsx;
	int s, retval;
	int winsz = 0;
	int i = minor(dev);

#if PCVT_EMU_MOUSE
	if(i == totalscreens)
		vsx = 0;
	else
#endif /* PCVT_EMU_MOUSE */

	vsx = &vs[i];

  	if((tp = get_pccons(dev)) == NULL)
		return ENXIO;

	dev->si_tty = tp;

#if PCVT_EMU_MOUSE
	if(i == totalscreens)
	{
		if(mouse.opened == 0)
			mouse.buttons = mouse.extendedseen =
				mouse.breakseen = mouse.lastmove.tv_sec = 0;
		mouse.minor = i;
		mouse.opened++;
	}
	else
#endif /* PCVT_EMU_MOUSE */

	vsx->openf++;

	tp->t_oproc = pcstart;
	tp->t_param = pcparam;
	tp->t_stop = nottystop;
	tp->t_dev = dev;

	if ((tp->t_state & TS_ISOPEN) == 0)
	{

#ifdef TS_WOPEN /* not (FreeBSD-1.1.5 or FreeBSD some time after 2.0.5) */
		tp->t_state |= TS_WOPEN;
#endif

		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_cflag = TTYDEF_CFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = tp->t_ospeed = TTYDEF_SPEED;
		pcparam(tp, &tp->t_termios);
		(*linesw[tp->t_line].l_modem)(tp, 1);	/* fake connection */
		winsz = 1;			/* set winsize later */
	}
	else if (tp->t_state & TS_XCLUDE && suser(p))
		return (EBUSY);

#if PCVT_NETBSD || (PCVT_FREEBSD >= 200)
	retval = ((*linesw[tp->t_line].l_open)(dev, tp));
#else
	retval = ((*linesw[tp->t_line].l_open)(dev, tp, flag));
#endif /* PCVT_NETBSD || (PCVT_FREEBSD >= 200) */

	if(winsz == 1)
	{

		/*
		 * The line discipline has clobbered t_winsize if TS_ISOPEN
	         * was clear. (NetBSD PR #400 from Bill Sommerfeld)
	         * We have to do this after calling the open routine, because
	         * it does some other things in other/older *BSD releases -hm
		 */

		s = spltty();

		tp->t_winsize.ws_col = vsx->maxcol;
		tp->t_winsize.ws_row = vsx->screen_rows;
		tp->t_winsize.ws_xpixel = (vsx->maxcol == 80)? 720: 1056;
		tp->t_winsize.ws_ypixel = 400;

		splx(s);
	}

	return(retval);
}

int
pcclose(Dev_t dev, int flag, int mode, struct proc *p)
{
	register struct tty *tp;
	register struct video_state *vsx;
	int i = minor(dev);

#if PCVT_EMU_MOUSE
	if(i == totalscreens)
		vsx = 0;
	else
#endif /* PCVT_EMU_MOUSE */

	vsx = &vs[i];

	if((tp = get_pccons(dev)) == NULL)
		return ENXIO;

	(*linesw[tp->t_line].l_close)(tp, flag);
	ttyclose(tp);

#if PCVT_EMU_MOUSE
	if(i == totalscreens)
		mouse.opened = 0;
	else
#endif /* PCVT_EMU_MOUSE */

	vsx->openf = 0;

#if PCVT_USL_VT_COMPAT
#if PCVT_EMU_MOUSE

	if(i == totalscreens)
		return (0);

#endif /* PCVT_EMU_MOUSE */

	reset_usl_modes(vsx);

#endif /* PCVT_USL_VT_COMPAT */

	return(0);
}

int
pcioctl(Dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	register int error;
	register struct tty *tp;

	if((tp = get_pccons(dev)) == NULL)
		return(ENXIO);

	/* note that some ioctl's are global, e.g.  KBSTPMAT: There is
	 * only one keyboard and different repeat rates for instance between
	 * sessions are a suspicious wish. If you really need this make the
	 * appropriate variables arrays
	 */

#if PCVT_EMU_MOUSE
	if(minor(dev) == totalscreens)
	{
		if((error = mouse_ioctl(dev, cmd, data)) >= 0)
			return error;
		goto do_standard;
	}
#endif /* PCVT_EMU_MOUSE */

#ifdef XSERVER
#if PCVT_USL_VT_COMPAT

	if((error = usl_vt_ioctl(dev, cmd, data, flag, p)) >= 0)
		return error;

	/*
	 * just for compatibility:
	 * XFree86 < 2.0 and SuperProbe still might use it
	 *
	 * NB: THIS IS A HACK! Do not use it unless you explicitly need.
	 * Especially, since the vty is not put into process-controlled
	 * mode (this would require the application to co-operate), any
	 * attempts to switch vtys while this kind of X mode is active
	 * may cause serious trouble.
	 */
	switch(cmd)
	{
	  case CONSOLE_X_MODE_ON:
	  {
	    int i;

	    if((error = usl_vt_ioctl(dev, KDENABIO, 0, flag, p)) > 0)
	      return error;

	    i = KD_GRAPHICS;
	    if((error = usl_vt_ioctl(dev, KDSETMODE, (caddr_t)&i, flag, p))
	       > 0)
	      return error;

	    i = K_RAW;
	    error = usl_vt_ioctl(dev, KDSKBMODE, (caddr_t)&i, flag, p);
	    return error;
	  }

	  case CONSOLE_X_MODE_OFF:
	  {
	    int i;

	    (void)usl_vt_ioctl(dev, KDDISABIO, 0, flag, p);

	    i = KD_TEXT;
	    (void)usl_vt_ioctl(dev, KDSETMODE, (caddr_t)&i, flag, p);

	    i = K_XLATE;
	    (void)usl_vt_ioctl(dev, KDSKBMODE, (caddr_t)&i, flag, p);
	    return 0;
	  }


	  case CONSOLE_X_BELL:

		/*
		 * If `data' is non-null, the first int value denotes
		 * the pitch, the second a duration. Otherwise, behaves
		 * like BEL.
		 */

		if (data)
		{

#if PCVT_NETBSD
			sysbeep(((int *)data)[0],
				((int *)data)[1] * hz / 1000);
#else /* PCVT_NETBSD */
			sysbeep(PCVT_SYSBEEPF / ((int *)data)[0],
				((int *)data)[1] * hz / 3000);
#endif /* PCVT_NETBSD */

		}
		else
		{
			sysbeep(PCVT_SYSBEEPF / 1493, hz / 4);
		}
		return (0);

	  default: /* fall through */ ;
	}

#else /* PCVT_USL_VT_COMPAT */

	switch(cmd)
	{
	  case CONSOLE_X_MODE_ON:
		return pcvt_xmode_set(1, p);

	  case CONSOLE_X_MODE_OFF:
		return pcvt_xmode_set(0, p);

	  case CONSOLE_X_BELL:

		/*
		 * If `data' is non-null, the first int value denotes
		 * the pitch, the second a duration. Otherwise, behaves
		 * like BEL.
		 */

		if (data)
		{

#if PCVT_NETBSD
			sysbeep(((int *)data)[0],
				((int *)data)[1] * hz / 1000);
#else /* PCVT_NETBSD */
			sysbeep(PCVT_SYSBEEPF / ((int *)data)[0],
				((int *)data)[1] * hz / 3000);
#endif /* PCVT_NETBSD */

		}
		else
		{
			sysbeep(PCVT_SYSBEEPF / 1493, hz / 4);
		}
		return (0);

	  default: /* fall through */ ;
	}

#endif /* PCVT_USL_VT_COMPAT */
#endif /* XSERVER */

	if((error = kbdioctl(dev,cmd,data,flag)) >= 0)
		return error;

	if((error = vgaioctl(dev,cmd,data,flag)) >= 0)
		return error;

#if PCVT_EMU_MOUSE
do_standard:
#endif

#if PCVT_NETBSD > 9 || PCVT_FREEBSD >= 200
	if((error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag, p))
	    != ENOIOCTL)
		return (error);
#else
	if((error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag)) >= 0)
		return(error);
#endif /* PCVT_NETBSD > 9 || PCVT_FREEBSD >= 200 */

#if PCVT_NETBSD > 9
	if((error = ttioctl(tp, cmd, data, flag, p)) >= 0)
		return (error);
#else
	if((error = ttioctl(tp, cmd, data, flag)) != ENOIOCTL)
		return (error);
#endif /* PCVT_NETBSD > 9 */

	return (ENOTTY);
}

int
pcmmap(Dev_t dev, vm_offset_t offset, int nprot)
{
	if (offset > 0x20000 - PAGE_SIZE)
		return -1;
	return i386_btop((0xa0000 + offset));
}

/*---------------------------------------------------------------------------*
 *
 *	handle a keyboard receive interrupt
 *
 *	NOTE: the keyboard is multiplexed by means of "pcconsp"
 *	between virtual screens. pcconsp - switching is done in
 *	the vgapage() routine
 *
 *---------------------------------------------------------------------------*/

#if PCVT_KBD_FIFO

u_char pcvt_kbd_fifo[PCVT_KBD_FIFO_SZ];
static int pcvt_kbd_wptr = 0;
int pcvt_kbd_rptr = 0;
short pcvt_kbd_count= 0;
static u_char pcvt_timeout_scheduled = 0;

static void
pcvt_timeout(void *arg)
{
	u_char *cp;

#if PCVT_SLOW_INTERRUPT
	int	s;
#endif

	pcvt_timeout_scheduled = 0;

#if PCVT_SCREENSAVER
	pcvt_scrnsv_reset();
#endif /* PCVT_SCREENSAVER */

	while (pcvt_kbd_count)
	{
		if (((cp = sgetc(1)) != 0) &&
		    (vs[current_video_screen].openf))
		{

#if PCVT_NULLCHARS
			if(*cp == '\0')
			{
				/* pass a NULL character */
				(*linesw[pcconsp->t_line].l_rint)('\0', pcconsp);
			}
/* XXX */		else
#endif /* PCVT_NULLCHARS */

			while (*cp)
				(*linesw[pcconsp->t_line].l_rint)(*cp++ & 0xff, pcconsp);
		}

		PCVT_DISABLE_INTR ();

		if (!pcvt_kbd_count)
			pcvt_timeout_scheduled = 0;

		PCVT_ENABLE_INTR ();
	}

	return;
}
#endif

#ifdef _DEV_KBD_KBDREG_H_
static void
detect_kbd(void *arg)
{
	int unit = (int)arg;
	int i;

	if (kbd != NULL)
		return;
	i = kbd_allocate("*", -1, (void *)&kbd, pcevent, (void *)unit);
	if (i >= 0)
		kbd = kbd_get_keyboard(i);
	if (kbd != NULL)
	{
		reset_keyboard = 1;	/* ok to reset the keyboard */
		kbd_code_init();
		return;
	}
	reset_keyboard = 0;
	timeout(detect_kbd, (void *)unit, hz*2);
}

int
pcevent(keyboard_t *thiskbd, int event, void *arg)
{
	int unit = (int)arg;

	if (thiskbd != kbd)
		return EINVAL;		/* shouldn't happen */

	switch (event) {
	case KBDIO_KEYINPUT:
		pcrint(unit);
		return 0;
	case KBDIO_UNLOADING:
		reset_keyboard = 0;
		kbd = NULL;
		kbd_release(thiskbd, (void *)&kbd);
		timeout(detect_kbd, (void *)unit, hz*4);
		return 0;
	default:
		return EINVAL;
	}
}
#endif /* _DEV_KBD_KBDREG_H_ */

void
pcrint(int unit)
{

#if PCVT_KBD_FIFO
	u_char	dt;
	u_char	ret = -1;

# if PCVT_SLOW_INTERRUPT
	int	s;
# endif

# ifdef _DEV_KBD_KBDREG_H_
	int	c;
# endif

#else /* !PCVT_KBD_FIFO */
	u_char	*cp;
#endif /* PCVT_KBD_FIFO */

#if PCVT_SCREENSAVER
	pcvt_scrnsv_reset();
#endif /* PCVT_SCREENSAVER */

#if PCVT_KBD_FIFO
	if (kbd_polling)
	{
		sgetc(1);
		return;
	}

# ifndef _DEV_KBD_KBDREG_H_
	while (inb(CONTROLLER_CTRL) & STATUS_OUTPBF)	/* check 8042 buffer */
	{
		ret = 1;				/* got something */

		PCVT_KBD_DELAY();			/* 7 us delay */

		dt = inb(CONTROLLER_DATA);		/* get it 8042 data */
# else 
	while ((c = (*kbdsw[kbd->kb_index]->read)(kbd, FALSE)) != -1)
	{
		ret = 1;				/* got something */
		dt = c;
# endif /* _DEV_KBD_KBDREG_H_ */

		if (pcvt_kbd_count >= PCVT_KBD_FIFO_SZ)	/* fifo overflow ? */
		{
			log (LOG_WARNING, "pcvt: keyboard buffer overflow\n");
		}
		else
		{
			pcvt_kbd_fifo[pcvt_kbd_wptr++] = dt; /* data -> fifo */

			PCVT_DISABLE_INTR ();	/* XXX necessary ? */
			pcvt_kbd_count++;		/* update fifo count */
			PCVT_ENABLE_INTR ();

			if (pcvt_kbd_wptr >= PCVT_KBD_FIFO_SZ)
				pcvt_kbd_wptr = 0;	/* wraparound pointer */
		}
	}

	if (ret == 1)	/* got data from keyboard ? */
	{
		if (!pcvt_timeout_scheduled)	/* if not already active .. */
		{
			PCVT_DISABLE_INTR ();
			pcvt_timeout_scheduled = 1;	/* flag active */
			timeout(pcvt_timeout, NULL, hz / 100);	/* fire off */
			PCVT_ENABLE_INTR ();
		}
	}

#else /* !PCVT_KBD_FIFO */

	if((cp = sgetc(1)) == 0)
		return;

	if (kbd_polling)
		return;

	if(!(vs[current_video_screen].openf))	/* XXX was vs[minor(dev)] */
		return;

#if PCVT_NULLCHARS
	if(*cp == '\0')
	{
		/* pass a NULL character */
		(*linesw[pcconsp->t_line].l_rint)('\0', pcconsp);
		return;
	}
#endif /* PCVT_NULLCHARS */

	while (*cp)
		(*linesw[pcconsp->t_line].l_rint)(*cp++ & 0xff, pcconsp);

#endif /* PCVT_KBD_FIFO */
}


#if PCVT_NETBSD || PCVT_FREEBSD >= 200

void
pcstart(register struct tty *tp)
{
	register struct clist *rbp;
	int s, len;
	u_char buf[PCVT_PCBURST];

	s = spltty();

	if (tp->t_state & (TS_TIMEOUT|TS_BUSY|TS_TTSTOP))
		goto out;

	tp->t_state |= TS_BUSY;

	splx(s);

	async_update(UPDATE_KERN);

	rbp = &tp->t_outq;

	/*
	 * Call q_to_b() at spltty() to ensure that the queue is empty when
	 * the loop terminates.
	 */

	s = spltty();

	while((len = q_to_b(rbp, buf, PCVT_PCBURST)) > 0)
	{
		if(vs[minor(tp->t_dev)].scrolling)
			sgetc(31337);
		
		/*
		 * We need to do this outside spl since it could be fairly
		 * expensive and we don't want our serial ports to overflow.
		 */
		splx(s);
		sput(&buf[0], 0, len, minor(tp->t_dev));
		s = spltty();
	}

	tp->t_state &= ~TS_BUSY;

#ifndef TS_ASLEEP /* FreeBSD some time after 2.0.5 */
	ttwwakeup(tp);
#else
	if (rbp->c_cc <= tp->t_lowat)
	{
		if (tp->t_state&TS_ASLEEP)
		{
			tp->t_state &= ~TS_ASLEEP;
			wakeup((caddr_t)rbp);
		}
		selwakeup(&tp->t_wsel);
	}
#endif
out:
	splx(s);
}

void
pcstop(struct tty *tp, int flag)
{
}

#else /* PCVT_NETBSD || PCVT_FREEBSD >= 200 */

void
pcstart(struct tty *tp)
{
	int s;
	unsigned char c;

	s = spltty();

	if (tp->t_state & (TS_TIMEOUT|TS_BUSY|TS_TTSTOP))
	{
		goto out;
	}

	for(;;)
	{

#if !(PCVT_FREEBSD > 114)

#if !(PCVT_FREEBSD > 111)
		if (RB_LEN(&tp->t_out) <= tp->t_lowat)
#else
		if (RB_LEN(tp->t_out) <= tp->t_lowat)
#endif
		{
			if (tp->t_state&TS_ASLEEP)
			{
				tp->t_state &= ~TS_ASLEEP;
#if !(PCVT_FREEBSD > 111)
				wakeup((caddr_t)&tp->t_out);
#else
				wakeup((caddr_t)tp->t_out);
#endif
			}

			if (tp->t_wsel)
			{
				selwakeup(tp->t_wsel, tp->t_state & TS_WCOLL);
				tp->t_wsel = 0;
				tp->t_state &= ~TS_WCOLL;
			}
		}

#else /* PCVT_FREEBSD > 114 */
		if (tp->t_state & (TS_SO_OCOMPLETE | TS_SO_OLOWAT)
		    || tp->t_wsel) {
			ttwwakeup(tp);
		}
#endif /* !PCVT_FREEBSD > 114 */

#if !(PCVT_FREEBSD > 111)
		if (RB_LEN(&tp->t_out) == 0)
#else
		if (RB_LEN(tp->t_out) == 0)
#endif
		{
			goto out;
		}

#if !(PCVT_FREEBSD > 111)
		c = getc(&tp->t_out);
#else
		c = getc(tp->t_out);
#endif

		tp->t_state |= TS_BUSY;	/* patch from Frank Maclachlan */
		splx(s);
		sput(&c, 0, 1, minor(tp->t_dev));
		spltty();
		tp->t_state &= ~TS_BUSY; /* patch from Frank Maclachlan */
	}
out:
	splx(s);
}

#endif /* PCVT_NETBSD || PCVT_FREEBSD >= 200 */

/*---------------------------------------------------------------------------*
 *		/dev/console
 *---------------------------------------------------------------------------*/

#if !PCVT_NETBSD	/* has moved to cons.c in netbsd-current */
void
consinit()		/* init for kernel messages during boot */
{
}
#endif /* PCVT_NETBSD */

#if PCVT_FREEBSD > 205
static void
#else
int
#endif
pccnprobe(struct consdev *cp)
{
	int unit = 0;
	int i;

	/* See if this driver is disabled in probe hint. */ 
	if (resource_int_value("vt", unit, "disabled", &i) == 0 && i) {
		cp->cn_pri = CN_DEAD;
		return;
	}

#ifdef _DEV_KBD_KBDREG_H_
	kbd_configure(KB_CONF_PROBE_ONLY);
	if (kbd_find_keyboard("*", unit) < 0)
	{
		cp->cn_pri = CN_DEAD;
		return;
	}
#endif /* _DEV_KBD_KBDREG_H_ */

	/* initialize required fields */

	cp->cn_dev = makedev(CDEV_MAJOR, 0);
	cp->cn_pri = CN_INTERNAL;

#if !PCVT_NETBSD

#if !(PCVT_FREEBSD > 110 && PCVT_FREEBSD < 200)
	cp->cn_tp = &pccons[0];
#else
	cp->cn_tp = pccons[0];
#endif /* !(PCVT_FREEBSD > 110 && PCVT_FREEBSD < 200) */

#endif /* !PCVT_NETBSD */

#if PCVT_FREEBSD <= 205
	return 1;
#endif
}

#if PCVT_FREEBSD > 205
static void
#else
int
#endif
pccninit(struct consdev *cp)
{
	int unit = 0;
	int i;

	pcvt_is_console = 1;

#ifdef _DEV_KBD_KBDREG_H_
	/*
	 * Don't reset the keyboard via `kbdio' just yet.
	 * The system clock has not been calibrated...
	 */
	reset_keyboard = 0;

	if (kbd)
	{
		kbd_release(kbd, (void *)&kbd);
		kbd = NULL;
	}
	i = kbd_allocate("*", -1, (void *)&kbd, pcevent, (void *)unit);
	if (i >= 0)
		kbd = kbd_get_keyboard(i);

#if PCVT_SCANSET == 2
	/*
	 * Turn off scancode translation early so that UserConfig 
	 * and DDB can read the keyboard.
	 */
	if (kbd)
	{
		empty_both_buffers(*(KBDC *)kbd->kb_data, 10);
		set_controller_command_byte(*(KBDC *)kbd->kb_data,
					    KBD_TRANSLATION, 0);
	}
#endif /* PCVT_SCANSET == 2 */

#endif /* _DEV_KBD_KBDREG_H_ */

#if PCVT_FREEBSD <= 205
	return 0;
#endif
}

static void
pccnterm(struct consdev *cp)
{
#ifdef _DEV_KBD_KBDREG_H_
	if (kbd)
	{
		kbd_release(kbd, (void *)&kbd);
		kbd = NULL;
	}
#endif /* _DEV_KBD_KBDREG_H_ */
}

#if PCVT_FREEBSD > 205
static void
#else
int
#endif
pccnputc(Dev_t dev, U_char c)
{

#if PCVT_SW0CNOUTP

	if(current_video_screen != 0)
	{

#if !PCVT_USL_VT_COMPAT
		vgapage(0);
#else
		switch_screen(0, 0);
#endif /* !PCVT_USL_VT_COMPAT */

	}

#endif /* PCVT_SW0CNOUTP */

	if (c == '\n')
		sput("\r", 1, 1, 0);

	sput((char *) &c, 1, 1, 0);

 	async_update(UPDATE_KERN);

#if PCVT_FREEBSD <= 205
	return 0;
#endif
}

static int
pccngetc(Dev_t dev)
{
	register int s;
	static u_char *cp, cbuf[4]; /* Temp buf for multi-char key sequence. */
	register u_char c;

#ifdef XSERVER

#if !PCVT_USL_VT_COMPAT
	if (pcvt_xmode)
		return 0;
#else /* !PCVT_USL_VT_COMPAT */
	if (pcvt_kbd_raw)
		return 0;
#endif /* !PCVT_USL_VT_COMPAT */

#endif /* XSERVER */

	if (cp && *cp)
		/*
		 * We still have a pending key sequence, e.g.
		 * from an arrow key.  Deliver this one first.
		 */
		return (*cp++);

#ifdef _DEV_KBD_KBDREG_H_
	if (kbd == NULL)
		return 0;
#endif	

	s = spltty();		/* block pcrint while we poll */
	kbd_polling = 1;
#ifdef _DEV_KBD_KBDREG_H_
	(*kbdsw[kbd->kb_index]->enable)(kbd);
#endif	
	cp = sgetc(0);
#ifdef _DEV_KBD_KBDREG_H_
	(*kbdsw[kbd->kb_index]->disable)(kbd);
#endif	
	kbd_polling = 0;
	splx(s);
	c = *cp++;
	if (c && *cp) {
		/* Preserve the multi-char sequence for the next call. */
		bcopy(cp, cbuf, 3); /* take care for a trailing '\0' */
		cp = cbuf;
	} else
		cp = 0;

#if ! (PCVT_FREEBSD >= 201)
	/* this belongs to cons.c */
	if (c == '\r')
		c = '\n';
#endif /* ! (PCVT_FREEBSD >= 201) */

	return c;
}

#if PCVT_FREEBSD >= 200
static int
pccncheckc(Dev_t dev)
{
	char *cp;
	int x;

#ifdef _DEV_KBD_KBDREG_H_
	if (kbd == NULL)
		return 0;
#endif	

	x = spltty();
	kbd_polling = 1;
#ifdef _DEV_KBD_KBDREG_H_
	(*kbdsw[kbd->kb_index]->enable)(kbd);
#endif	
	cp = sgetc(1);
#ifdef _DEV_KBD_KBDREG_H_
	(*kbdsw[kbd->kb_index]->disable)(kbd);
#endif	
	kbd_polling = 0;
	splx(x);
	return (cp == NULL ? -1 : *cp);
}
#endif /* PCVT_FREEBSD >= 200 */

#if PCVT_NETBSD >= 100
void
pccnpollc(Dev_t dev, int on)
{
	kbd_polling = on;
	if (!on) {
		register int s;

		/*
		 * If disabling polling, make sure there are no bytes left in
		 * the FIFO, holding up the interrupt line.  Otherwise we
		 * won't get any further interrupts.
		 */
		s = spltty();
		pcrint();
		splx(s);
	}
}
#endif /* PCVT_NETBSD >= 100 */

/*---------------------------------------------------------------------------*
 *	Set line parameters
 *---------------------------------------------------------------------------*/
int
pcparam(struct tty *tp, struct termios *t)
{
	register int cflag = t->c_cflag;

        /* and copy to tty */

        tp->t_ispeed = t->c_ispeed;
        tp->t_ospeed = t->c_ospeed;
        tp->t_cflag = cflag;

	return(0);
}

/*----------------------------------------------------------------------*
 *	read initial VGA palette (as stored by VGA ROM BIOS) into
 *	palette save area
 *----------------------------------------------------------------------*/
void
vgapelinit(void)
{
	register unsigned idx;
	register struct rgb *val;

	/* first, read all and store to first screen's save buffer */
	for(idx = 0, val = vs[0].palette; idx < NVGAPEL; idx++, val++)
		vgapaletteio(idx, val, 0 /* read it */);

	/* now, duplicate for remaining screens */
	for(idx = 1; idx < PCVT_NSCREENS; idx++)
		bcopy(vs[0].palette, vs[idx].palette,
		      NVGAPEL * sizeof(struct rgb));
}

#if defined XSERVER && !PCVT_USL_VT_COMPAT
/*----------------------------------------------------------------------*
 *	initialize for X mode
 *	i.e.: grant current process (the X server) all IO privileges,
 *	and mark in static variable so other hooks can test for it,
 *	save all loaded fonts and screen pages to pageable buffers;
 *	if parameter `on' is false, the same procedure is done reverse.
 *----------------------------------------------------------------------*/
static int
pcvt_xmode_set(int on, struct proc *p)
{
	static unsigned char *saved_fonts[NVGAFONTS];

#if PCVT_SCREENSAVER
	static unsigned saved_scrnsv_tmo = 0;
#endif /* PCVT_SCREENSAVER */

#if (PCVT_NETBSD > 9) || (PCVT_FREEBSD > 102)
	struct trapframe *fp;
#else
	struct syscframe *fp;
#endif /* PCVT_NETBSD > 9 */

	int error, i;

	/* X will only run on VGA and Hercules adaptors */

	if(adaptor_type != VGA_ADAPTOR && adaptor_type != MDA_ADAPTOR)
		return (EINVAL);

#if PCVT_NETBSD > 9
	fp = (struct trapframe *)p->p_regs;
#else
	fp = (struct syscframe *)p->p_regs;
#endif /* PCVT_NETBSD > 9 */

	if(on)
	{
		/*
		 * Test whether the calling process has super-user privileges
		 * and we're in insecure mode.
		 * This prevents us from granting the potential security hole
		 * `IO priv' to insufficiently privileged processes.
		 */
		error = suser(p);
		if (error != 0)
			return (error);
		if (securelevel > 0)
			return (EPERM);

		if(pcvt_xmode)
			return 0;

		pcvt_xmode = pcvt_kbd_raw = 1;

		for(i = 0; i < totalfonts; i++)
		{
			if(vgacs[i].loaded)
			{
				saved_fonts[i] = (unsigned char *)
					malloc(32 * 256, M_DEVBUF, M_WAITOK);
				if(saved_fonts[i] == 0)
				{
					printf(
				  "pcvt_xmode_set: no font buffer available\n");
					return (EAGAIN);
				}
				else
				{
					vga_move_charset(i, saved_fonts[i], 1);
				}
			}
			else
			{
				saved_fonts[i] = 0;
			}
		}

#if PCVT_SCREENSAVER
		if(saved_scrnsv_tmo = scrnsv_timeout)
			pcvt_set_scrnsv_tmo(0);	/* turn it off */
#endif /* PCVT_SCREENSAVER */

		async_update(UPDATE_STOP);	/* turn off */

		/* disable text output and save screen contents */
		/* video board memory -> kernel memory */

		bcopy(vsp->Crtat, vsp->Memory,
		       vsp->screen_rowsize * vsp->maxcol * CHR);

		vsp->Crtat = vsp->Memory;	/* operate in memory now */

#ifndef _DEV_KBD_KBDREG_H_

#if PCVT_SCANSET == 2
		/* put keyboard to return ancient PC scan codes */
		kbc_8042cmd(CONTR_WRITE);
#if PCVT_USEKBDSEC		/* security enabled */
		outb(CONTROLLER_DATA,
		 (COMMAND_SYSFLG|COMMAND_IRQEN|COMMAND_PCSCAN));
#else				/* security disabled */
		outb(CONTROLLER_DATA,
		 (COMMAND_INHOVR|COMMAND_SYSFLG|COMMAND_IRQEN|COMMAND_PCSCAN));
#endif /* PCVT_USEKBDSEC */
#endif /* PCVT_SCANSET == 2 */

#else /* _DEV_KBD_KBDREG_H_ */

#if PCVT_SCANSET == 2
		/* put keyboard to return ancient PC scan codes */
		set_controller_command_byte(*(KBDC *)kbd->kb_data, 
			KBD_TRANSLATION, KBD_TRANSLATION); 
#endif /* PCVT_SCANSET == 2 */

#endif /* !_DEV_KBD_KBDREG_H_ */

#if PCVT_NETBSD > 9
		fp->tf_eflags |= PSL_IOPL;
#else
		fp->sf_eflags |= PSL_IOPL;
#endif /* PCVT_NETBSD > 9 */

	}
	else
	{
		if(!pcvt_xmode)		/* verify if in X */
			return 0;

		pcvt_xmode = pcvt_kbd_raw = 0;

		for(i = 0; i < totalfonts; i++)
		{
			if(saved_fonts[i])
			{
				vga_move_charset(i, saved_fonts[i], 0);
				free(saved_fonts[i], M_DEVBUF);
				saved_fonts[i] = 0;
			}
		}

#if PCVT_NETBSD > 9
		fp->tf_eflags &= ~PSL_IOPL;
#else
		fp->sf_eflags &= ~PSL_IOPL;
#endif /* PCVT_NETBSD > 9 */

#if PCVT_SCREENSAVER
		if(saved_scrnsv_tmo)
			pcvt_set_scrnsv_tmo(saved_scrnsv_tmo);
#endif /* PCVT_SCREENSAVER */

#ifndef _DEV_KBD_KBDREG_H_

#if PCVT_SCANSET == 2
		kbc_8042cmd(CONTR_WRITE);
#if PCVT_USEKBDSEC		/* security enabled */
		outb(CONTROLLER_DATA,
		 (COMMAND_SYSFLG|COMMAND_IRQEN));
#else				/* security disabled */
		outb(CONTROLLER_DATA,
		 (COMMAND_INHOVR|COMMAND_SYSFLG|COMMAND_IRQEN));
#endif /* PCVT_USEKBDSEC */
#endif /* PCVT_SCANSET == 2 */

#else /* _DEV_KBD_KBDREG_H_ */

#if PCVT_SCANSET == 2
		set_controller_command_byte(*(KBDC *)kbd->kb_data,
			KBD_TRANSLATION, 0);
#endif /* PCVT_SCANSET == 2 */

#endif /* !_DEV_KBD_KBDREG_H_ */

		if(adaptor_type == MDA_ADAPTOR)
		{
		    /*
		     * Due to the fact that HGC registers are write-only,
		     * the Xserver can only make guesses about the state
		     * the HGC adaptor has been before turning on X mode.
		     * Thus, the display must be re-enabled now, and the
		     * cursor shape and location restored.
		     */
		    outb(GN_DMCNTLM, 0x28); /* enable display, text mode */
		    outb(addr_6845, CRTC_CURSORH); /* select high register */
		    outb(addr_6845+1,
			 ((vsp->Crtat + vsp->cur_offset) - Crtat) >> 8);
		    outb(addr_6845, CRTC_CURSORL); /* select low register */
		    outb(addr_6845+1,
			 ((vsp->Crtat + vsp->cur_offset) - Crtat));

		    outb(addr_6845, CRTC_CURSTART); /* select high register */
		    outb(addr_6845+1, vsp->cursor_start);
		    outb(addr_6845, CRTC_CUREND); /* select low register */
		    outb(addr_6845+1, vsp->cursor_end);
		  }

		/* restore screen and re-enable text output */
		/* kernel memory -> video board memory */

		bcopy(vsp->Memory, Crtat,
		       vsp->screen_rowsize * vsp->maxcol * CHR);

		vsp->Crtat = Crtat;	/* operate on-screen now */

		/* set crtc screen memory start address */

		outb(addr_6845, CRTC_STARTADRH);
		outb(addr_6845+1, (vsp->Crtat - Crtat) >> 8);
		outb(addr_6845, CRTC_STARTADRL);
		outb(addr_6845+1, (vsp->Crtat - Crtat));

		async_update(UPDATE_START);
	}
	return 0;
}
#endif	/* XSERVER && !PCVT_USL_VT_COMPAT */

#endif	/* NVT > 0 */

/*-------------------------- E O F -------------------------------------*/
