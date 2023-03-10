/*    pp_ctl.c
 *
 *    Copyright (c) 1991-1999, Larry Wall
 *
 *    You may distribute under the terms of either the GNU General Public
 *    License or the Artistic License, as specified in the README file.
 *
 */

/*
 * Now far ahead the Road has gone,
 * And I must follow, if I can,
 * Pursuing it with eager feet,
 * Until it joins some larger way
 * Where many paths and errands meet.
 * And whither then?  I cannot say.
 */

#include "EXTERN.h"
#include "perl.h"

#ifndef WORD_ALIGN
#define WORD_ALIGN sizeof(U16)
#endif

#define DOCATCH(o) ((CATCH_GET == TRUE) ? docatch(o) : (o))

#ifdef PERL_OBJECT
#define CALLOP this->*PL_op
#else
#define CALLOP *PL_op
static OP *docatch _((OP *o));
static OP *dofindlabel _((OP *o, char *label, OP **opstack, OP **oplimit));
static void doparseform _((SV *sv));
static I32 dopoptoeval _((I32 startingblock));
static I32 dopoptolabel _((char *label));
static I32 dopoptoloop _((I32 startingblock));
static I32 dopoptosub _((I32 startingblock));
static I32 dopoptosub_at _((PERL_CONTEXT *cxstk, I32 startingblock));
static void save_lines _((AV *array, SV *sv));
static I32 sortcv _((SV *a, SV *b));
static void qsortsv _((SV **array, size_t num_elts, I32 (*fun)(SV *a, SV *b)));
static OP *doeval _((int gimme, OP** startop));
#endif

PP(pp_wantarray)
{
    djSP;
    I32 cxix;
    EXTEND(SP, 1);

    cxix = dopoptosub(cxstack_ix);
    if (cxix < 0)
	RETPUSHUNDEF;

    switch (cxstack[cxix].blk_gimme) {
    case G_ARRAY:
	RETPUSHYES;
    case G_SCALAR:
	RETPUSHNO;
    default:
	RETPUSHUNDEF;
    }
}

PP(pp_regcmaybe)
{
    return NORMAL;
}

PP(pp_regcreset)
{
    /* XXXX Should store the old value to allow for tie/overload - and
       restore in regcomp, where marked with XXXX. */
    PL_reginterp_cnt = 0;
    return NORMAL;
}

PP(pp_regcomp)
{
    djSP;
    register PMOP *pm = (PMOP*)cLOGOP->op_other;
    register char *t;
    SV *tmpstr;
    STRLEN len;
    MAGIC *mg = Null(MAGIC*);

    tmpstr = POPs;
    if (SvROK(tmpstr)) {
	SV *sv = SvRV(tmpstr);
	if(SvMAGICAL(sv))
	    mg = mg_find(sv, 'r');
    }
    if (mg) {
	regexp *re = (regexp *)mg->mg_obj;
	ReREFCNT_dec(pm->op_pmregexp);
	pm->op_pmregexp = ReREFCNT_inc(re);
    }
    else {
	t = SvPV(tmpstr, len);

	/* Check against the last compiled regexp. */
	if (!pm->op_pmregexp || !pm->op_pmregexp->precomp ||
	    pm->op_pmregexp->prelen != len ||
	    memNE(pm->op_pmregexp->precomp, t, len))
	{
	    if (pm->op_pmregexp) {
		ReREFCNT_dec(pm->op_pmregexp);
		pm->op_pmregexp = Null(REGEXP*);	/* crucial if regcomp aborts */
	    }
	    if (PL_op->op_flags & OPf_SPECIAL)
		PL_reginterp_cnt = I32_MAX; /* Mark as safe.  */

	    pm->op_pmflags = pm->op_pmpermflags;	/* reset case sensitivity */
	    pm->op_pmregexp = CALLREGCOMP(t, t + len, pm);
	    PL_reginterp_cnt = 0;		/* XXXX Be extra paranoid - needed
					   inside tie/overload accessors.  */
	}
    }

#ifndef INCOMPLETE_TAINTS
    if (PL_tainting) {
	if (PL_tainted)
	    pm->op_pmdynflags |= PMdf_TAINTED;
	else
	    pm->op_pmdynflags &= ~PMdf_TAINTED;
    }
#endif

    if (!pm->op_pmregexp->prelen && PL_curpm)
	pm = PL_curpm;
    else if (strEQ("\\s+", pm->op_pmregexp->precomp))
	pm->op_pmflags |= PMf_WHITE;

    if (pm->op_pmflags & PMf_KEEP) {
	pm->op_private &= ~OPpRUNTIME;	/* no point compiling again */
	cLOGOP->op_first->op_next = PL_op->op_next;
    }
    RETURN;
}

PP(pp_substcont)
{
    djSP;
    register PMOP *pm = (PMOP*) cLOGOP->op_other;
    register PERL_CONTEXT *cx = &cxstack[cxstack_ix];
    register SV *dstr = cx->sb_dstr;
    register char *s = cx->sb_s;
    register char *m = cx->sb_m;
    char *orig = cx->sb_orig;
    register REGEXP *rx = cx->sb_rx;

    rxres_restore(&cx->sb_rxres, rx);

    if (cx->sb_iters++) {
	if (cx->sb_iters > cx->sb_maxiters)
	    DIE("Substitution loop");

	if (!(cx->sb_rxtainted & 2) && SvTAINTED(TOPs))
	    cx->sb_rxtainted |= 2;
	sv_catsv(dstr, POPs);

	/* Are we done */
	if (cx->sb_once || !CALLREGEXEC(rx, s, cx->sb_strend, orig,
				     s == m, Nullsv, NULL,
				     cx->sb_safebase ? 0 : REXEC_COPY_STR))
	{
	    SV *targ = cx->sb_targ;
	    sv_catpvn(dstr, s, cx->sb_strend - s);

	    cx->sb_rxtainted |= RX_MATCH_TAINTED(rx);

	    (void)SvOOK_off(targ);
	    Safefree(SvPVX(targ));
	    SvPVX(targ) = SvPVX(dstr);
	    SvCUR_set(targ, SvCUR(dstr));
	    SvLEN_set(targ, SvLEN(dstr));
	    SvPVX(dstr) = 0;
	    sv_free(dstr);

	    TAINT_IF(cx->sb_rxtainted & 1);
	    PUSHs(sv_2mortal(newSViv((I32)cx->sb_iters - 1)));

	    (void)SvPOK_only(targ);
	    TAINT_IF(cx->sb_rxtainted);
	    SvSETMAGIC(targ);
	    SvTAINT(targ);

	    LEAVE_SCOPE(cx->sb_oldsave);
	    POPSUBST(cx);
	    RETURNOP(pm->op_next);
	}
    }
    if (rx->subbase && rx->subbase != orig) {
	m = s;
	s = orig;
	cx->sb_orig = orig = rx->subbase;
	s = orig + (m - s);
	cx->sb_strend = s + (cx->sb_strend - m);
    }
    cx->sb_m = m = rx->startp[0];
    sv_catpvn(dstr, s, m-s);
    cx->sb_s = rx->endp[0];
    cx->sb_rxtainted |= RX_MATCH_TAINTED(rx);
    rxres_save(&cx->sb_rxres, rx);
    RETURNOP(pm->op_pmreplstart);
}

void
rxres_save(void **rsp, REGEXP *rx)
{
    UV *p = (UV*)*rsp;
    U32 i;

    if (!p || p[1] < rx->nparens) {
	i = 6 + rx->nparens * 2;
	if (!p)
	    New(501, p, i, UV);
	else
	    Renew(p, i, UV);
	*rsp = (void*)p;
    }

    *p++ = (UV)rx->subbase;
    rx->subbase = Nullch;

    *p++ = rx->nparens;

    *p++ = (UV)rx->subbeg;
    *p++ = (UV)rx->subend;
    for (i = 0; i <= rx->nparens; ++i) {
	*p++ = (UV)rx->startp[i];
	*p++ = (UV)rx->endp[i];
    }
}

void
rxres_restore(void **rsp, REGEXP *rx)
{
    UV *p = (UV*)*rsp;
    U32 i;

    Safefree(rx->subbase);
    rx->subbase = (char*)(*p);
    *p++ = 0;

    rx->nparens = *p++;

    rx->subbeg = (char*)(*p++);
    rx->subend = (char*)(*p++);
    for (i = 0; i <= rx->nparens; ++i) {
	rx->startp[i] = (char*)(*p++);
	rx->endp[i] = (char*)(*p++);
    }
}

void
rxres_free(void **rsp)
{
    UV *p = (UV*)*rsp;

    if (p) {
	Safefree((char*)(*p));
	Safefree(p);
	*rsp = Null(void*);
    }
}

PP(pp_formline)
{
    djSP; dMARK; dORIGMARK;
    register SV *tmpForm = *++MARK;
    register U16 *fpc;
    register char *t;
    register char *f;
    register char *s;
    register char *send;
    register I32 arg;
    register SV *sv;
    char *item;
    I32 itemsize;
    I32 fieldsize;
    I32 lines = 0;
    bool chopspace = (strchr(PL_chopset, ' ') != Nullch);
    char *chophere;
    char *linemark;
    double value;
    bool gotsome;
    STRLEN len;

    if (!SvMAGICAL(tmpForm) || !SvCOMPILED(tmpForm)) {
	SvREADONLY_off(tmpForm);
	doparseform(tmpForm);
    }

    SvPV_force(PL_formtarget, len);
    t = SvGROW(PL_formtarget, len + SvCUR(tmpForm) + 1);  /* XXX SvCUR bad */
    t += len;
    f = SvPV(tmpForm, len);
    /* need to jump to the next word */
    s = f + len + WORD_ALIGN - SvCUR(tmpForm) % WORD_ALIGN;

    fpc = (U16*)s;

    for (;;) {
	DEBUG_f( {
	    char *name = "???";
	    arg = -1;
	    switch (*fpc) {
	    case FF_LITERAL:	arg = fpc[1]; name = "LITERAL";	break;
	    case FF_BLANK:	arg = fpc[1]; name = "BLANK";	break;
	    case FF_SKIP:	arg = fpc[1]; name = "SKIP";	break;
	    case FF_FETCH:	arg = fpc[1]; name = "FETCH";	break;
	    case FF_DECIMAL:	arg = fpc[1]; name = "DECIMAL";	break;

	    case FF_CHECKNL:	name = "CHECKNL";	break;
	    case FF_CHECKCHOP:	name = "CHECKCHOP";	break;
	    case FF_SPACE:	name = "SPACE";		break;
	    case FF_HALFSPACE:	name = "HALFSPACE";	break;
	    case FF_ITEM:	name = "ITEM";		break;
	    case FF_CHOP:	name = "CHOP";		break;
	    case FF_LINEGLOB:	name = "LINEGLOB";	break;
	    case FF_NEWLINE:	name = "NEWLINE";	break;
	    case FF_MORE:	name = "MORE";		break;
	    case FF_LINEMARK:	name = "LINEMARK";	break;
	    case FF_END:	name = "END";		break;
	    }
	    if (arg >= 0)
		PerlIO_printf(PerlIO_stderr(), "%-16s%ld\n", name, (long) arg);
	    else
		PerlIO_printf(PerlIO_stderr(), "%-16s\n", name);
	} )
	switch (*fpc++) {
	case FF_LINEMARK:
	    linemark = t;
	    lines++;
	    gotsome = FALSE;
	    break;

	case FF_LITERAL:
	    arg = *fpc++;
	    while (arg--)
		*t++ = *f++;
	    break;

	case FF_SKIP:
	    f += *fpc++;
	    break;

	case FF_FETCH:
	    arg = *fpc++;
	    f += arg;
	    fieldsize = arg;

	    if (MARK < SP)
		sv = *++MARK;
	    else {
		sv = &PL_sv_no;
		if (PL_dowarn)
		    warn("Not enough format arguments");
	    }
	    break;

	case FF_CHECKNL:
	    item = s = SvPV(sv, len);
	    itemsize = len;
	    if (itemsize > fieldsize)
		itemsize = fieldsize;
	    send = chophere = s + itemsize;
	    while (s < send) {
		if (*s & ~31)
		    gotsome = TRUE;
		else if (*s == '\n')
		    break;
		s++;
	    }
	    itemsize = s - item;
	    break;

	case FF_CHECKCHOP:
	    item = s = SvPV(sv, len);
	    itemsize = len;
	    if (itemsize <= fieldsize) {
		send = chophere = s + itemsize;
		while (s < send) {
		    if (*s == '\r') {
			itemsize = s - item;
			break;
		    }
		    if (*s++ & ~31)
			gotsome = TRUE;
		}
	    }
	    else {
		itemsize = fieldsize;
		send = chophere = s + itemsize;
		while (s < send || (s == send && isSPACE(*s))) {
		    if (isSPACE(*s)) {
			if (chopspace)
			    chophere = s;
			if (*s == '\r')
			    break;
		    }
		    else {
			if (*s & ~31)
			    gotsome = TRUE;
			if (strchr(PL_chopset, *s))
			    chophere = s + 1;
		    }
		    s++;
		}
		itemsize = chophere - item;
	    }
	    break;

	case FF_SPACE:
	    arg = fieldsize - itemsize;
	    if (arg) {
		fieldsize -= arg;
		while (arg-- > 0)
		    *t++ = ' ';
	    }
	    break;

	case FF_HALFSPACE:
	    arg = fieldsize - itemsize;
	    if (arg) {
		arg /= 2;
		fieldsize -= arg;
		while (arg-- > 0)
		    *t++ = ' ';
	    }
	    break;

	case FF_ITEM:
	    arg = itemsize;
	    s = item;
	    while (arg--) {
#ifdef EBCDIC
		int ch = *t++ = *s++;
		if (iscntrl(ch))
#else
		if ( !((*t++ = *s++) & ~31) )
#endif
		    t[-1] = ' ';
	    }
	    break;

	case FF_CHOP:
	    s = chophere;
	    if (chopspace) {
		while (*s && isSPACE(*s))
		    s++;
	    }
	    sv_chop(sv,s);
	    break;

	case FF_LINEGLOB:
	    item = s = SvPV(sv, len);
	    itemsize = len;
	    if (itemsize) {
		gotsome = TRUE;
		send = s + itemsize;
		while (s < send) {
		    if (*s++ == '\n') {
			if (s == send)
			    itemsize--;
			else
			    lines++;
		    }
		}
		SvCUR_set(PL_formtarget, t - SvPVX(PL_formtarget));
		sv_catpvn(PL_formtarget, item, itemsize);
		SvGROW(PL_formtarget, SvCUR(PL_formtarget) + SvCUR(tmpForm) + 1);
		t = SvPVX(PL_formtarget) + SvCUR(PL_formtarget);
	    }
	    break;

	case FF_DECIMAL:
	    /* If the field is marked with ^ and the value is undefined,
	       blank it out. */
	    arg = *fpc++;
	    if ((arg & 512) && !SvOK(sv)) {
		arg = fieldsize;
		while (arg--)
		    *t++ = ' ';
		break;
	    }
	    gotsome = TRUE;
	    value = SvNV(sv);
	    /* Formats aren't yet marked for locales, so assume "yes". */
	    SET_NUMERIC_LOCAL();
	    if (arg & 256) {
		sprintf(t, "%#*.*f", (int) fieldsize, (int) arg & 255, value);
	    } else {
		sprintf(t, "%*.0f", (int) fieldsize, value);
	    }
	    t += fieldsize;
	    break;

	case FF_NEWLINE:
	    f++;
	    while (t-- > linemark && *t == ' ') ;
	    t++;
	    *t++ = '\n';
	    break;

	case FF_BLANK:
	    arg = *fpc++;
	    if (gotsome) {
		if (arg) {		/* repeat until fields exhausted? */
		    *t = '\0';
		    SvCUR_set(PL_formtarget, t - SvPVX(PL_formtarget));
		    lines += FmLINES(PL_formtarget);
		    if (lines == 200) {
			arg = t - linemark;
			if (strnEQ(linemark, linemark - arg, arg))
			    DIE("Runaway format");
		    }
		    FmLINES(PL_formtarget) = lines;
		    SP = ORIGMARK;
		    RETURNOP(cLISTOP->op_first);
		}
	    }
	    else {
		t = linemark;
		lines--;
	    }
	    break;

	case FF_MORE:
	    s = chophere;
	    send = item + len;
	    if (chopspace) {
		while (*s && isSPACE(*s) && s < send)
		    s++;
	    }
	    if (s < send) {
		arg = fieldsize - itemsize;
		if (arg) {
		    fieldsize -= arg;
		    while (arg-- > 0)
			*t++ = ' ';
		}
		s = t - 3;
		if (strnEQ(s,"   ",3)) {
		    while (s > SvPVX(PL_formtarget) && isSPACE(s[-1]))
			s--;
		}
		*s++ = '.';
		*s++ = '.';
		*s++ = '.';
	    }
	    break;

	case FF_END:
	    *t = '\0';
	    SvCUR_set(PL_formtarget, t - SvPVX(PL_formtarget));
	    FmLINES(PL_formtarget) += lines;
	    SP = ORIGMARK;
	    RETPUSHYES;
	}
    }
}

PP(pp_grepstart)
{
    djSP;
    SV *src;

    if (PL_stack_base + *PL_markstack_ptr == SP) {
	(void)POPMARK;
	if (GIMME_V == G_SCALAR)
	    XPUSHs(&PL_sv_no);
	RETURNOP(PL_op->op_next->op_next);
    }
    PL_stack_sp = PL_stack_base + *PL_markstack_ptr + 1;
    pp_pushmark(ARGS);				/* push dst */
    pp_pushmark(ARGS);				/* push src */
    ENTER;					/* enter outer scope */

    SAVETMPS;
#ifdef USE_THREADS
    /* SAVE_DEFSV does *not* suffice here */
    save_sptr(&THREADSV(0));
#else
    SAVESPTR(GvSV(PL_defgv));
#endif /* USE_THREADS */
    ENTER;					/* enter inner scope */
    SAVESPTR(PL_curpm);

    src = PL_stack_base[*PL_markstack_ptr];
    SvTEMP_off(src);
    DEFSV = src;

    PUTBACK;
    if (PL_op->op_type == OP_MAPSTART)
	pp_pushmark(ARGS);			/* push top */
    return ((LOGOP*)PL_op->op_next)->op_other;
}

PP(pp_mapstart)
{
    DIE("panic: mapstart");	/* uses grepstart */
}

PP(pp_mapwhile)
{
    djSP;
    I32 diff = (SP - PL_stack_base) - *PL_markstack_ptr;
    I32 count;
    I32 shift;
    SV** src;
    SV** dst; 

    ++PL_markstack_ptr[-1];
    if (diff) {
	if (diff > PL_markstack_ptr[-1] - PL_markstack_ptr[-2]) {
	    shift = diff - (PL_markstack_ptr[-1] - PL_markstack_ptr[-2]);
	    count = (SP - PL_stack_base) - PL_markstack_ptr[-1] + 2;
	    
	    EXTEND(SP,shift);
	    src = SP;
	    dst = (SP += shift);
	    PL_markstack_ptr[-1] += shift;
	    *PL_markstack_ptr += shift;
	    while (--count)
		*dst-- = *src--;
	}
	dst = PL_stack_base + (PL_markstack_ptr[-2] += diff) - 1; 
	++diff;
	while (--diff)
	    *dst-- = SvTEMP(TOPs) ? POPs : sv_mortalcopy(POPs); 
    }
    LEAVE;					/* exit inner scope */

    /* All done yet? */
    if (PL_markstack_ptr[-1] > *PL_markstack_ptr) {
	I32 items;
	I32 gimme = GIMME_V;

	(void)POPMARK;				/* pop top */
	LEAVE;					/* exit outer scope */
	(void)POPMARK;				/* pop src */
	items = --*PL_markstack_ptr - PL_markstack_ptr[-1];
	(void)POPMARK;				/* pop dst */
	SP = PL_stack_base + POPMARK;		/* pop original mark */
	if (gimme == G_SCALAR) {
	    dTARGET;
	    XPUSHi(items);
	}
	else if (gimme == G_ARRAY)
	    SP += items;
	RETURN;
    }
    else {
	SV *src;

	ENTER;					/* enter inner scope */
	SAVESPTR(PL_curpm);

	src = PL_stack_base[PL_markstack_ptr[-1]];
	SvTEMP_off(src);
	DEFSV = src;

	RETURNOP(cLOGOP->op_other);
    }
}

#define tryCALL_AMAGICbin(left,right,meth,svp) STMT_START { \
	  *svp = Nullsv;				\
          if (PL_amagic_generation) { \
	    if (SvAMAGIC(left)||SvAMAGIC(right))\
		*svp = amagic_call(left, \
				   right, \
				   CAT2(meth,_amg), \
				   0); \
	  } \
	} STMT_END

STATIC I32
amagic_cmp(register SV *str1, register SV *str2)
{
    SV *tmpsv;
    tryCALL_AMAGICbin(str1,str2,scmp,&tmpsv);
    if (tmpsv) {
    	double d;
    	
        if (SvIOK(tmpsv)) {
            I32 i = SvIVX(tmpsv);
            if (i > 0)
               return 1;
            return i? -1 : 0;
        }
        d = SvNV(tmpsv);
        if (d > 0)
           return 1;
        return d? -1 : 0;
    }
    return sv_cmp(str1, str2);
}

STATIC I32
amagic_cmp_locale(register SV *str1, register SV *str2)
{
    SV *tmpsv;
    tryCALL_AMAGICbin(str1,str2,scmp,&tmpsv);
    if (tmpsv) {
    	double d;
    	
        if (SvIOK(tmpsv)) {
            I32 i = SvIVX(tmpsv);
            if (i > 0)
               return 1;
            return i? -1 : 0;
        }
        d = SvNV(tmpsv);
        if (d > 0)
           return 1;
        return d? -1 : 0;
    }
    return sv_cmp_locale(str1, str2);
}

PP(pp_sort)
{
    djSP; dMARK; dORIGMARK;
    register SV **up;
    SV **myorigmark = ORIGMARK;
    register I32 max;
    HV *stash;
    GV *gv;
    CV *cv;
    I32 gimme = GIMME;
    OP* nextop = PL_op->op_next;
    I32 overloading = 0;

    if (gimme != G_ARRAY) {
	SP = MARK;
	RETPUSHUNDEF;
    }

    ENTER;
    SAVEPPTR(PL_sortcop);
    if (PL_op->op_flags & OPf_STACKED) {
	if (PL_op->op_flags & OPf_SPECIAL) {
	    OP *kid = cLISTOP->op_first->op_sibling;	/* pass pushmark */
	    kid = kUNOP->op_first;			/* pass rv2gv */
	    kid = kUNOP->op_first;			/* pass leave */
	    PL_sortcop = kid->op_next;
	    stash = PL_curcop->cop_stash;
	}
	else {
	    cv = sv_2cv(*++MARK, &stash, &gv, 0);
	    if (!(cv && CvROOT(cv))) {
		if (gv) {
		    SV *tmpstr = sv_newmortal();
		    gv_efullname3(tmpstr, gv, Nullch);
		    if (cv && CvXSUB(cv))
			DIE("Xsub \"%s\" called in sort", SvPVX(tmpstr));
		    DIE("Undefined sort subroutine \"%s\" called",
			SvPVX(tmpstr));
		}
		if (cv) {
		    if (CvXSUB(cv))
			DIE("Xsub called in sort");
		    DIE("Undefined subroutine in sort");
		}
		DIE("Not a CODE reference in sort");
	    }
	    PL_sortcop = CvSTART(cv);
	    SAVESPTR(CvROOT(cv)->op_ppaddr);
	    CvROOT(cv)->op_ppaddr = ppaddr[OP_NULL];

	    SAVESPTR(PL_curpad);
	    PL_curpad = AvARRAY((AV*)AvARRAY(CvPADLIST(cv))[1]);
	}
    }
    else {
	PL_sortcop = Nullop;
	stash = PL_curcop->cop_stash;
    }

    up = myorigmark + 1;
    while (MARK < SP) {	/* This may or may not shift down one here. */
	/*SUPPRESS 560*/
	if (*up = *++MARK) {			/* Weed out nulls. */
	    SvTEMP_off(*up);
	    if (!PL_sortcop && !SvPOK(*up)) {
	        if (SvAMAGIC(*up))
	            overloading = 1;
	        else {
		    STRLEN n_a;
		    (void)sv_2pv(*up, &n_a);
		}
	    }
	    up++;
	}
    }
    max = --up - myorigmark;
    if (PL_sortcop) {
	if (max > 1) {
	    PERL_CONTEXT *cx;
	    SV** newsp;
	    bool oldcatch = CATCH_GET;

	    SAVETMPS;
	    SAVEOP();

	    CATCH_SET(TRUE);
	    PUSHSTACKi(PERLSI_SORT);
	    if (PL_sortstash != stash) {
		PL_firstgv = gv_fetchpv("a", TRUE, SVt_PV);
		PL_secondgv = gv_fetchpv("b", TRUE, SVt_PV);
		PL_sortstash = stash;
	    }

	    SAVESPTR(GvSV(PL_firstgv));
	    SAVESPTR(GvSV(PL_secondgv));

	    PUSHBLOCK(cx, CXt_NULL, PL_stack_base);
	    if (!(PL_op->op_flags & OPf_SPECIAL)) {
		bool hasargs = FALSE;
		cx->cx_type = CXt_SUB;
		cx->blk_gimme = G_SCALAR;
		PUSHSUB(cx);
		if (!CvDEPTH(cv))
		    (void)SvREFCNT_inc(cv); /* in preparation for POPSUB */
	    }
	    PL_sortcxix = cxstack_ix;
	    qsortsv((myorigmark+1), max, FUNC_NAME_TO_PTR(sortcv));

	    POPBLOCK(cx,PL_curpm);
	    POPSTACK;
	    CATCH_SET(oldcatch);
	}
    }
    else {
	if (max > 1) {
	    MEXTEND(SP, 20);	/* Can't afford stack realloc on signal. */
	    qsortsv(ORIGMARK+1, max,
		    (PL_op->op_private & OPpLOCALE)
		    ? ( overloading
		        ? FUNC_NAME_TO_PTR(amagic_cmp_locale)
		        : FUNC_NAME_TO_PTR(sv_cmp_locale))
		    : ( overloading 
		        ? FUNC_NAME_TO_PTR(amagic_cmp)
		        : FUNC_NAME_TO_PTR(sv_cmp) ));
	}
    }
    LEAVE;
    PL_stack_sp = ORIGMARK + max;
    return nextop;
}

/* Range stuff. */

PP(pp_range)
{
    if (GIMME == G_ARRAY)
	return cCONDOP->op_true;
    return SvTRUEx(PAD_SV(PL_op->op_targ)) ? cCONDOP->op_false : cCONDOP->op_true;
}

PP(pp_flip)
{
    djSP;

    if (GIMME == G_ARRAY) {
	RETURNOP(((CONDOP*)cUNOP->op_first)->op_false);
    }
    else {
	dTOPss;
	SV *targ = PAD_SV(PL_op->op_targ);

	if ((PL_op->op_private & OPpFLIP_LINENUM)
	  ? (PL_last_in_gv && SvIV(sv) == (IV)IoLINES(GvIOp(PL_last_in_gv)))
	  : SvTRUE(sv) ) {
	    sv_setiv(PAD_SV(cUNOP->op_first->op_targ), 1);
	    if (PL_op->op_flags & OPf_SPECIAL) {
		sv_setiv(targ, 1);
		SETs(targ);
		RETURN;
	    }
	    else {
		sv_setiv(targ, 0);
		SP--;
		RETURNOP(((CONDOP*)cUNOP->op_first)->op_false);
	    }
	}
	sv_setpv(TARG, "");
	SETs(targ);
	RETURN;
    }
}

PP(pp_flop)
{
    djSP;

    if (GIMME == G_ARRAY) {
	dPOPPOPssrl;
	register I32 i, j;
	register SV *sv;
	I32 max;

	if (SvNIOKp(left) || !SvPOKp(left) ||
	  (looks_like_number(left) && *SvPVX(left) != '0') )
	{
	    if (SvNV(left) < IV_MIN || SvNV(right) > IV_MAX)
		croak("Range iterator outside integer range");
	    i = SvIV(left);
	    max = SvIV(right);
	    if (max >= i) {
		j = max - i + 1;
		EXTEND_MORTAL(j);
		EXTEND(SP, j);
	    }
	    else
		j = 0;
	    while (j--) {
		sv = sv_2mortal(newSViv(i++));
		PUSHs(sv);
	    }
	}
	else {
	    SV *final = sv_mortalcopy(right);
	    STRLEN len;
	    STRLEN n_a;
	    char *tmps = SvPV(final, len);

	    sv = sv_mortalcopy(left);
	    SvPV_force(sv,n_a);
	    while (!SvNIOKp(sv) && SvCUR(sv) <= len) {
		XPUSHs(sv);
	        if (strEQ(SvPVX(sv),tmps))
	            break;
		sv = sv_2mortal(newSVsv(sv));
		sv_inc(sv);
	    }
	}
    }
    else {
	dTOPss;
	SV *targ = PAD_SV(cUNOP->op_first->op_targ);
	sv_inc(targ);
	if ((PL_op->op_private & OPpFLIP_LINENUM)
	  ? (PL_last_in_gv && SvIV(sv) == (IV)IoLINES(GvIOp(PL_last_in_gv)))
	  : SvTRUE(sv) ) {
	    sv_setiv(PAD_SV(((UNOP*)cUNOP->op_first)->op_first->op_targ), 0);
	    sv_catpv(targ, "E0");
	}
	SETs(targ);
    }

    RETURN;
}

/* Control. */

STATIC I32
dopoptolabel(char *label)
{
    dTHR;
    register I32 i;
    register PERL_CONTEXT *cx;

    for (i = cxstack_ix; i >= 0; i--) {
	cx = &cxstack[i];
	switch (CxTYPE(cx)) {
	case CXt_SUBST:
	    if (PL_dowarn)
		warn("Exiting substitution via %s", op_name[PL_op->op_type]);
	    break;
	case CXt_SUB:
	    if (PL_dowarn)
		warn("Exiting subroutine via %s", op_name[PL_op->op_type]);
	    break;
	case CXt_EVAL:
	    if (PL_dowarn)
		warn("Exiting eval via %s", op_name[PL_op->op_type]);
	    break;
	case CXt_NULL:
	    if (PL_dowarn)
		warn("Exiting pseudo-block via %s", op_name[PL_op->op_type]);
	    return -1;
	case CXt_LOOP:
	    if (!cx->blk_loop.label ||
	      strNE(label, cx->blk_loop.label) ) {
		DEBUG_l(deb("(Skipping label #%ld %s)\n",
			(long)i, cx->blk_loop.label));
		continue;
	    }
	    DEBUG_l( deb("(Found label #%ld %s)\n", (long)i, label));
	    return i;
	}
    }
    return i;
}

I32
dowantarray(void)
{
    I32 gimme = block_gimme();
    return (gimme == G_VOID) ? G_SCALAR : gimme;
}

I32
block_gimme(void)
{
    dTHR;
    I32 cxix;

    cxix = dopoptosub(cxstack_ix);
    if (cxix < 0)
	return G_VOID;

    switch (cxstack[cxix].blk_gimme) {
    case G_VOID:
	return G_VOID;
    case G_SCALAR:
	return G_SCALAR;
    case G_ARRAY:
	return G_ARRAY;
    default:
	croak("panic: bad gimme: %d\n", cxstack[cxix].blk_gimme);
	/* NOTREACHED */
	return 0;
    }
}

STATIC I32
dopoptosub(I32 startingblock)
{
    dTHR;
    return dopoptosub_at(cxstack, startingblock);
}

STATIC I32
dopoptosub_at(PERL_CONTEXT *cxstk, I32 startingblock)
{
    dTHR;
    I32 i;
    register PERL_CONTEXT *cx;
    for (i = startingblock; i >= 0; i--) {
	cx = &cxstk[i];
	switch (CxTYPE(cx)) {
	default:
	    continue;
	case CXt_EVAL:
	case CXt_SUB:
	    DEBUG_l( deb("(Found sub #%ld)\n", (long)i));
	    return i;
	}
    }
    return i;
}

STATIC I32
dopoptoeval(I32 startingblock)
{
    dTHR;
    I32 i;
    register PERL_CONTEXT *cx;
    for (i = startingblock; i >= 0; i--) {
	cx = &cxstack[i];
	switch (CxTYPE(cx)) {
	default:
	    continue;
	case CXt_EVAL:
	    DEBUG_l( deb("(Found eval #%ld)\n", (long)i));
	    return i;
	}
    }
    return i;
}

STATIC I32
dopoptoloop(I32 startingblock)
{
    dTHR;
    I32 i;
    register PERL_CONTEXT *cx;
    for (i = startingblock; i >= 0; i--) {
	cx = &cxstack[i];
	switch (CxTYPE(cx)) {
	case CXt_SUBST:
	    if (PL_dowarn)
		warn("Exiting substitution via %s", op_name[PL_op->op_type]);
	    break;
	case CXt_SUB:
	    if (PL_dowarn)
		warn("Exiting subroutine via %s", op_name[PL_op->op_type]);
	    break;
	case CXt_EVAL:
	    if (PL_dowarn)
		warn("Exiting eval via %s", op_name[PL_op->op_type]);
	    break;
	case CXt_NULL:
	    if (PL_dowarn)
		warn("Exiting pseudo-block via %s", op_name[PL_op->op_type]);
	    return -1;
	case CXt_LOOP:
	    DEBUG_l( deb("(Found loop #%ld)\n", (long)i));
	    return i;
	}
    }
    return i;
}

void
dounwind(I32 cxix)
{
    dTHR;
    register PERL_CONTEXT *cx;
    SV **newsp;
    I32 optype;

    while (cxstack_ix > cxix) {
	cx = &cxstack[cxstack_ix];
	DEBUG_l(PerlIO_printf(Perl_debug_log, "Unwinding block %ld, type %s\n",
			      (long) cxstack_ix, block_type[CxTYPE(cx)]));
	/* Note: we don't need to restore the base context info till the end. */
	switch (CxTYPE(cx)) {
	case CXt_SUBST:
	    POPSUBST(cx);
	    continue;  /* not break */
	case CXt_SUB:
	    POPSUB(cx);
	    break;
	case CXt_EVAL:
	    POPEVAL(cx);
	    break;
	case CXt_LOOP:
	    POPLOOP(cx);
	    break;
	case CXt_NULL:
	    break;
	}
	cxstack_ix--;
    }
}

OP *
die_where(char *message)
{
    dSP;
    STRLEN n_a;
    if (PL_in_eval) {
	I32 cxix;
	register PERL_CONTEXT *cx;
	I32 gimme;
	SV **newsp;

	if (message) {
	    if (PL_in_eval & 4) {
		SV **svp;
		STRLEN klen = strlen(message);
		
		svp = hv_fetch(ERRHV, message, klen, TRUE);
		if (svp) {
		    if (!SvIOK(*svp)) {
			static char prefix[] = "\t(in cleanup) ";
			SV *err = ERRSV;
			sv_upgrade(*svp, SVt_IV);
			(void)SvIOK_only(*svp);
			if (!SvPOK(err))
			    sv_setpv(err,"");
			SvGROW(err, SvCUR(err)+sizeof(prefix)+klen);
			sv_catpvn(err, prefix, sizeof(prefix)-1);
			sv_catpvn(err, message, klen);
		    }
		    sv_inc(*svp);
		}
	    }
	    else
		sv_setpv(ERRSV, message);
	}
	else
	    message = SvPVx(ERRSV, n_a);

	while ((cxix = dopoptoeval(cxstack_ix)) < 0 && PL_curstackinfo->si_prev) {
	    dounwind(-1);
	    POPSTACK;
	}

	if (cxix >= 0) {
	    I32 optype;

	    if (cxix < cxstack_ix)
		dounwind(cxix);

	    POPBLOCK(cx,PL_curpm);
	    if (CxTYPE(cx) != CXt_EVAL) {
		PerlIO_printf(PerlIO_stderr(), "panic: die %s", message);
		my_exit(1);
	    }
	    POPEVAL(cx);

	    if (gimme == G_SCALAR)
		*++newsp = &PL_sv_undef;
	    PL_stack_sp = newsp;

	    LEAVE;

	    if (optype == OP_REQUIRE) {
		char* msg = SvPVx(ERRSV, n_a);
		DIE("%s", *msg ? msg : "Compilation failed in require");
	    }
	    return pop_return();
	}
    }
    if(!message)
	message = SvPVx(ERRSV, n_a);
    PerlIO_printf(PerlIO_stderr(), "%s",message);
    PerlIO_flush(PerlIO_stderr());
    my_failure_exit();
    /* NOTREACHED */
    return 0;
}

PP(pp_xor)
{
    djSP; dPOPTOPssrl;
    if (SvTRUE(left) != SvTRUE(right))
	RETSETYES;
    else
	RETSETNO;
}

PP(pp_andassign)
{
    djSP;
    if (!SvTRUE(TOPs))
	RETURN;
    else
	RETURNOP(cLOGOP->op_other);
}

PP(pp_orassign)
{
    djSP;
    if (SvTRUE(TOPs))
	RETURN;
    else
	RETURNOP(cLOGOP->op_other);
}
	
PP(pp_caller)
{
    djSP;
    register I32 cxix = dopoptosub(cxstack_ix);
    register PERL_CONTEXT *cx;
    register PERL_CONTEXT *ccstack = cxstack;
    PERL_SI *top_si = PL_curstackinfo;
    I32 dbcxix;
    I32 gimme;
    HV *hv;
    SV *sv;
    I32 count = 0;

    if (MAXARG)
	count = POPi;
    EXTEND(SP, 6);
    for (;;) {
	/* we may be in a higher stacklevel, so dig down deeper */
	while (cxix < 0 && top_si->si_type != PERLSI_MAIN) {
	    top_si = top_si->si_prev;
	    ccstack = top_si->si_cxstack;
	    cxix = dopoptosub_at(ccstack, top_si->si_cxix);
	}
	if (cxix < 0) {
	    if (GIMME != G_ARRAY)
		RETPUSHUNDEF;
	    RETURN;
	}
	if (PL_DBsub && cxix >= 0 &&
		ccstack[cxix].blk_sub.cv == GvCV(PL_DBsub))
	    count++;
	if (!count--)
	    break;
	cxix = dopoptosub_at(ccstack, cxix - 1);
    }

    cx = &ccstack[cxix];
    if (CxTYPE(cx) == CXt_SUB) {
        dbcxix = dopoptosub_at(ccstack, cxix - 1);
	/* We expect that ccstack[dbcxix] is CXt_SUB, anyway, the
	   field below is defined for any cx. */
	if (PL_DBsub && dbcxix >= 0 && ccstack[dbcxix].blk_sub.cv == GvCV(PL_DBsub))
	    cx = &ccstack[dbcxix];
    }

    if (GIMME != G_ARRAY) {
	hv = cx->blk_oldcop->cop_stash;
	if (!hv)
	    PUSHs(&PL_sv_undef);
	else {
	    dTARGET;
	    sv_setpv(TARG, HvNAME(hv));
	    PUSHs(TARG);
	}
	RETURN;
    }

    hv = cx->blk_oldcop->cop_stash;
    if (!hv)
	PUSHs(&PL_sv_undef);
    else
	PUSHs(sv_2mortal(newSVpv(HvNAME(hv), 0)));
    PUSHs(sv_2mortal(newSVpv(SvPVX(GvSV(cx->blk_oldcop->cop_filegv)), 0)));
    PUSHs(sv_2mortal(newSViv((I32)cx->blk_oldcop->cop_line)));
    if (!MAXARG)
	RETURN;
    if (CxTYPE(cx) == CXt_SUB) { /* So is ccstack[dbcxix]. */
	sv = NEWSV(49, 0);
	gv_efullname3(sv, CvGV(ccstack[cxix].blk_sub.cv), Nullch);
	PUSHs(sv_2mortal(sv));
	PUSHs(sv_2mortal(newSViv((I32)cx->blk_sub.hasargs)));
    }
    else {
	PUSHs(sv_2mortal(newSVpv("(eval)",0)));
	PUSHs(sv_2mortal(newSViv(0)));
    }
    gimme = (I32)cx->blk_gimme;
    if (gimme == G_VOID)
	PUSHs(&PL_sv_undef);
    else
	PUSHs(sv_2mortal(newSViv(gimme & G_ARRAY)));
    if (CxTYPE(cx) == CXt_EVAL) {
	if (cx->blk_eval.old_op_type == OP_ENTEREVAL) {
	    PUSHs(cx->blk_eval.cur_text);
	    PUSHs(&PL_sv_no);
	} 
	else if (cx->blk_eval.old_name) { /* Try blocks have old_name == 0. */
	    /* Require, put the name. */
	    PUSHs(sv_2mortal(newSVpv(cx->blk_eval.old_name, 0)));
	    PUSHs(&PL_sv_yes);
	}
    }
    else if (CxTYPE(cx) == CXt_SUB &&
	    cx->blk_sub.hasargs &&
	    PL_curcop->cop_stash == PL_debstash)
    {
	AV *ary = cx->blk_sub.argarray;
	int off = AvARRAY(ary) - AvALLOC(ary);

	if (!PL_dbargs) {
	    GV* tmpgv;
	    PL_dbargs = GvAV(gv_AVadd(tmpgv = gv_fetchpv("DB::args", TRUE,
				SVt_PVAV)));
	    GvMULTI_on(tmpgv);
	    AvREAL_off(PL_dbargs);		/* XXX Should be REIFY */
	}

	if (AvMAX(PL_dbargs) < AvFILLp(ary) + off)
	    av_extend(PL_dbargs, AvFILLp(ary) + off);
	Copy(AvALLOC(ary), AvARRAY(PL_dbargs), AvFILLp(ary) + 1 + off, SV*);
	AvFILLp(PL_dbargs) = AvFILLp(ary) + off;
    }
    RETURN;
}

STATIC I32
sortcv(SV *a, SV *b)
{
    dTHR;
    I32 oldsaveix = PL_savestack_ix;
    I32 oldscopeix = PL_scopestack_ix;
    I32 result;
    GvSV(PL_firstgv) = a;
    GvSV(PL_secondgv) = b;
    PL_stack_sp = PL_stack_base;
    PL_op = PL_sortcop;
    CALLRUNOPS();
    if (PL_stack_sp != PL_stack_base + 1)
	croak("Sort subroutine didn't return single value");
    if (!SvNIOKp(*PL_stack_sp))
	croak("Sort subroutine didn't return a numeric value");
    result = SvIV(*PL_stack_sp);
    while (PL_scopestack_ix > oldscopeix) {
	LEAVE;
    }
    leave_scope(oldsaveix);
    return result;
}

PP(pp_reset)
{
    djSP;
    char *tmps;
    STRLEN n_a;

    if (MAXARG < 1)
	tmps = "";
    else
	tmps = POPpx;
    sv_reset(tmps, PL_curcop->cop_stash);
    PUSHs(&PL_sv_yes);
    RETURN;
}

PP(pp_lineseq)
{
    return NORMAL;
}

PP(pp_dbstate)
{
    PL_curcop = (COP*)PL_op;
    TAINT_NOT;		/* Each statement is presumed innocent */
    PL_stack_sp = PL_stack_base + cxstack[cxstack_ix].blk_oldsp;
    FREETMPS;

    if (PL_op->op_private || SvIV(PL_DBsingle) || SvIV(PL_DBsignal) || SvIV(PL_DBtrace))
    {
	djSP;
	register CV *cv;
	register PERL_CONTEXT *cx;
	I32 gimme = G_ARRAY;
	I32 hasargs;
	GV *gv;

	gv = PL_DBgv;
	cv = GvCV(gv);
	if (!cv)
	    DIE("No DB::DB routine defined");

	if (CvDEPTH(cv) >= 1 && !(PL_debug & (1<<30))) /* don't do recursive DB::DB call */
	    return NORMAL;

	ENTER;
	SAVETMPS;

	SAVEI32(PL_debug);
	SAVESTACK_POS();
	PL_debug = 0;
	hasargs = 0;
	SPAGAIN;

	push_return(PL_op->op_next);
	PUSHBLOCK(cx, CXt_SUB, SP);
	PUSHSUB(cx);
	CvDEPTH(cv)++;
	(void)SvREFCNT_inc(cv);
	SAVESPTR(PL_curpad);
	PL_curpad = AvARRAY((AV*)*av_fetch(CvPADLIST(cv),1,FALSE));
	RETURNOP(CvSTART(cv));
    }
    else
	return NORMAL;
}

PP(pp_scope)
{
    return NORMAL;
}

PP(pp_enteriter)
{
    djSP; dMARK;
    register PERL_CONTEXT *cx;
    I32 gimme = GIMME_V;
    SV **svp;

    ENTER;
    SAVETMPS;

#ifdef USE_THREADS
    if (PL_op->op_flags & OPf_SPECIAL) {
	dTHR;
	svp = &THREADSV(PL_op->op_targ);	/* per-thread variable */
	SAVEGENERICSV(*svp);
	*svp = NEWSV(0,0);
    }
    else
#endif /* USE_THREADS */
    if (PL_op->op_targ) {
	svp = &PL_curpad[PL_op->op_targ];		/* "my" variable */
	SAVESPTR(*svp);
    }
    else {
	svp = &GvSV((GV*)POPs);			/* symbol table variable */
	SAVEGENERICSV(*svp);
	*svp = NEWSV(0,0);
    }

    ENTER;

    PUSHBLOCK(cx, CXt_LOOP, SP);
    PUSHLOOP(cx, svp, MARK);
    if (PL_op->op_flags & OPf_STACKED) {
	cx->blk_loop.iterary = (AV*)SvREFCNT_inc(POPs);
	if (SvTYPE(cx->blk_loop.iterary) != SVt_PVAV) {
	    dPOPss;
	    if (SvNIOKp(sv) || !SvPOKp(sv) ||
		(looks_like_number(sv) && *SvPVX(sv) != '0')) {
		 if (SvNV(sv) < IV_MIN ||
		     SvNV((SV*)cx->blk_loop.iterary) >= IV_MAX)
		     croak("Range iterator outside integer range");
		 cx->blk_loop.iterix = SvIV(sv);
		 cx->blk_loop.itermax = SvIV((SV*)cx->blk_loop.iterary);
	    }
	    else
		cx->blk_loop.iterlval = newSVsv(sv);
	}
    }
    else {
	cx->blk_loop.iterary = PL_curstack;
	AvFILLp(PL_curstack) = SP - PL_stack_base;
	cx->blk_loop.iterix = MARK - PL_stack_base;
    }

    RETURN;
}

PP(pp_enterloop)
{
    djSP;
    register PERL_CONTEXT *cx;
    I32 gimme = GIMME_V;

    ENTER;
    SAVETMPS;
    ENTER;

    PUSHBLOCK(cx, CXt_LOOP, SP);
    PUSHLOOP(cx, 0, SP);

    RETURN;
}

PP(pp_leaveloop)
{
    djSP;
    register PERL_CONTEXT *cx;
    struct block_loop cxloop;
    I32 gimme;
    SV **newsp;
    PMOP *newpm;
    SV **mark;

    POPBLOCK(cx,newpm);
    mark = newsp;
    POPLOOP1(cx);	/* Delay POPLOOP2 until stack values are safe */

    TAINT_NOT;
    if (gimme == G_VOID)
	; /* do nothing */
    else if (gimme == G_SCALAR) {
	if (mark < SP)
	    *++newsp = sv_mortalcopy(*SP);
	else
	    *++newsp = &PL_sv_undef;
    }
    else {
	while (mark < SP) {
	    *++newsp = sv_mortalcopy(*++mark);
	    TAINT_NOT;		/* Each item is independent */
	}
    }
    SP = newsp;
    PUTBACK;

    POPLOOP2();		/* Stack values are safe: release loop vars ... */
    PL_curpm = newpm;	/* ... and pop $1 et al */

    LEAVE;
    LEAVE;

    return NORMAL;
}

PP(pp_return)
{
    djSP; dMARK;
    I32 cxix;
    register PERL_CONTEXT *cx;
    struct block_sub cxsub;
    bool popsub2 = FALSE;
    I32 gimme;
    SV **newsp;
    PMOP *newpm;
    I32 optype = 0;

    if (PL_curstackinfo->si_type == PERLSI_SORT) {
	if (cxstack_ix == PL_sortcxix || dopoptosub(cxstack_ix) <= PL_sortcxix) {
	    if (cxstack_ix > PL_sortcxix)
		dounwind(PL_sortcxix);
	    AvARRAY(PL_curstack)[1] = *SP;
	    PL_stack_sp = PL_stack_base + 1;
	    return 0;
	}
    }

    cxix = dopoptosub(cxstack_ix);
    if (cxix < 0)
	DIE("Can't return outside a subroutine");
    if (cxix < cxstack_ix)
	dounwind(cxix);

    POPBLOCK(cx,newpm);
    switch (CxTYPE(cx)) {
    case CXt_SUB:
	POPSUB1(cx);	/* Delay POPSUB2 until stack values are safe */
	popsub2 = TRUE;
	break;
    case CXt_EVAL:
	POPEVAL(cx);
	if (optype == OP_REQUIRE &&
	    (MARK == SP || (gimme == G_SCALAR && !SvTRUE(*SP))) )
	{
	    /* Unassume the success we assumed earlier. */
	    char *name = cx->blk_eval.old_name;
	    (void)hv_delete(GvHVn(PL_incgv), name, strlen(name), G_DISCARD);
	    DIE("%s did not return a true value", name);
	}
	break;
    default:
	DIE("panic: return");
    }

    TAINT_NOT;
    if (gimme == G_SCALAR) {
	if (MARK < SP) {
	    if (popsub2) {
		if (cxsub.cv && CvDEPTH(cxsub.cv) > 1) {
		    if (SvTEMP(TOPs)) {
			*++newsp = SvREFCNT_inc(*SP);
			FREETMPS;
			sv_2mortal(*newsp);
		    } else {
			FREETMPS;
			*++newsp = sv_mortalcopy(*SP);
		    }
		} else
		    *++newsp = (SvTEMP(*SP)) ? *SP : sv_mortalcopy(*SP);
	    } else
		*++newsp = sv_mortalcopy(*SP);
	} else
	    *++newsp = &PL_sv_undef;
    }
    else if (gimme == G_ARRAY) {
	while (++MARK <= SP) {
	    *++newsp = (popsub2 && SvTEMP(*MARK))
			? *MARK : sv_mortalcopy(*MARK);
	    TAINT_NOT;		/* Each item is independent */
	}
    }
    PL_stack_sp = newsp;

    /* Stack values are safe: */
    if (popsub2) {
	POPSUB2();	/* release CV and @_ ... */
    }
    PL_curpm = newpm;	/* ... and pop $1 et al */

    LEAVE;
    return pop_return();
}

PP(pp_last)
{
    djSP;
    I32 cxix;
    register PERL_CONTEXT *cx;
    struct block_loop cxloop;
    struct block_sub cxsub;
    I32 pop2 = 0;
    I32 gimme;
    I32 optype;
    OP *nextop;
    SV **newsp;
    PMOP *newpm;
    SV **mark = PL_stack_base + cxstack[cxstack_ix].blk_oldsp;

    if (PL_op->op_flags & OPf_SPECIAL) {
	cxix = dopoptoloop(cxstack_ix);
	if (cxix < 0)
	    DIE("Can't \"last\" outside a block");
    }
    else {
	cxix = dopoptolabel(cPVOP->op_pv);
	if (cxix < 0)
	    DIE("Label not found for \"last %s\"", cPVOP->op_pv);
    }
    if (cxix < cxstack_ix)
	dounwind(cxix);

    POPBLOCK(cx,newpm);
    switch (CxTYPE(cx)) {
    case CXt_LOOP:
	POPLOOP1(cx);	/* Delay POPLOOP2 until stack values are safe */
	pop2 = CXt_LOOP;
	nextop = cxloop.last_op->op_next;
	break;
    case CXt_SUB:
	POPSUB1(cx);	/* Delay POPSUB2 until stack values are safe */
	pop2 = CXt_SUB;
	nextop = pop_return();
	break;
    case CXt_EVAL:
	POPEVAL(cx);
	nextop = pop_return();
	break;
    default:
	DIE("panic: last");
    }

    TAINT_NOT;
    if (gimme == G_SCALAR) {
	if (MARK < SP)
	    *++newsp = ((pop2 == CXt_SUB) && SvTEMP(*SP))
			? *SP : sv_mortalcopy(*SP);
	else
	    *++newsp = &PL_sv_undef;
    }
    else if (gimme == G_ARRAY) {
	while (++MARK <= SP) {
	    *++newsp = ((pop2 == CXt_SUB) && SvTEMP(*MARK))
			? *MARK : sv_mortalcopy(*MARK);
	    TAINT_NOT;		/* Each item is independent */
	}
    }
    SP = newsp;
    PUTBACK;

    /* Stack values are safe: */
    switch (pop2) {
    case CXt_LOOP:
	POPLOOP2();	/* release loop vars ... */
	LEAVE;
	break;
    case CXt_SUB:
	POPSUB2();	/* release CV and @_ ... */
	break;
    }
    PL_curpm = newpm;	/* ... and pop $1 et al */

    LEAVE;
    return nextop;
}

PP(pp_next)
{
    I32 cxix;
    register PERL_CONTEXT *cx;
    I32 oldsave;

    if (PL_op->op_flags & OPf_SPECIAL) {
	cxix = dopoptoloop(cxstack_ix);
	if (cxix < 0)
	    DIE("Can't \"next\" outside a block");
    }
    else {
	cxix = dopoptolabel(cPVOP->op_pv);
	if (cxix < 0)
	    DIE("Label not found for \"next %s\"", cPVOP->op_pv);
    }
    if (cxix < cxstack_ix)
	dounwind(cxix);

    TOPBLOCK(cx);
    oldsave = PL_scopestack[PL_scopestack_ix - 1];
    LEAVE_SCOPE(oldsave);
    return cx->blk_loop.next_op;
}

PP(pp_redo)
{
    I32 cxix;
    register PERL_CONTEXT *cx;
    I32 oldsave;

    if (PL_op->op_flags & OPf_SPECIAL) {
	cxix = dopoptoloop(cxstack_ix);
	if (cxix < 0)
	    DIE("Can't \"redo\" outside a block");
    }
    else {
	cxix = dopoptolabel(cPVOP->op_pv);
	if (cxix < 0)
	    DIE("Label not found for \"redo %s\"", cPVOP->op_pv);
    }
    if (cxix < cxstack_ix)
	dounwind(cxix);

    TOPBLOCK(cx);
    oldsave = PL_scopestack[PL_scopestack_ix - 1];
    LEAVE_SCOPE(oldsave);
    return cx->blk_loop.redo_op;
}

STATIC OP *
dofindlabel(OP *o, char *label, OP **opstack, OP **oplimit)
{
    OP *kid;
    OP **ops = opstack;
    static char too_deep[] = "Target of goto is too deeply nested";

    if (ops >= oplimit)
	croak(too_deep);
    if (o->op_type == OP_LEAVE ||
	o->op_type == OP_SCOPE ||
	o->op_type == OP_LEAVELOOP ||
	o->op_type == OP_LEAVETRY)
    {
	*ops++ = cUNOPo->op_first;
	if (ops >= oplimit)
	    croak(too_deep);
    }
    *ops = 0;
    if (o->op_flags & OPf_KIDS) {
	dTHR;
	/* First try all the kids at this level, since that's likeliest. */
	for (kid = cUNOPo->op_first; kid; kid = kid->op_sibling) {
	    if ((kid->op_type == OP_NEXTSTATE || kid->op_type == OP_DBSTATE) &&
		    kCOP->cop_label && strEQ(kCOP->cop_label, label))
		return kid;
	}
	for (kid = cUNOPo->op_first; kid; kid = kid->op_sibling) {
	    if (kid == PL_lastgotoprobe)
		continue;
	    if ((kid->op_type == OP_NEXTSTATE || kid->op_type == OP_DBSTATE) &&
		(ops == opstack ||
		 (ops[-1]->op_type != OP_NEXTSTATE &&
		  ops[-1]->op_type != OP_DBSTATE)))
		*ops++ = kid;
	    if (o = dofindlabel(kid, label, ops, oplimit))
		return o;
	}
    }
    *ops = 0;
    return 0;
}

PP(pp_dump)
{
    return pp_goto(ARGS);
    /*NOTREACHED*/
}

PP(pp_goto)
{
    djSP;
    OP *retop = 0;
    I32 ix;
    register PERL_CONTEXT *cx;
#define GOTO_DEPTH 64
    OP *enterops[GOTO_DEPTH];
    char *label;
    int do_dump = (PL_op->op_type == OP_DUMP);

    label = 0;
    if (PL_op->op_flags & OPf_STACKED) {
	SV *sv = POPs;
	STRLEN n_a;

	/* This egregious kludge implements goto &subroutine */
	if (SvROK(sv) && SvTYPE(SvRV(sv)) == SVt_PVCV) {
	    I32 cxix;
	    register PERL_CONTEXT *cx;
	    CV* cv = (CV*)SvRV(sv);
	    SV** mark;
	    I32 items = 0;
	    I32 oldsave;
	    int arg_was_real = 0;

	retry:
	    if (!CvROOT(cv) && !CvXSUB(cv)) {
		GV *gv = CvGV(cv);
		GV *autogv;
		if (gv) {
		    SV *tmpstr;
		    /* autoloaded stub? */
		    if (cv != GvCV(gv) && (cv = GvCV(gv)))
			goto retry;
		    autogv = gv_autoload4(GvSTASH(gv), GvNAME(gv),
					  GvNAMELEN(gv), FALSE);
		    if (autogv && (cv = GvCV(autogv)))
			goto retry;
		    tmpstr = sv_newmortal();
		    gv_efullname3(tmpstr, gv, Nullch);
		    DIE("Goto undefined subroutine &%s",SvPVX(tmpstr));
		}
		DIE("Goto undefined subroutine");
	    }

	    /* First do some returnish stuff. */
	    cxix = dopoptosub(cxstack_ix);
	    if (cxix < 0)
		DIE("Can't goto subroutine outside a subroutine");
	    if (cxix < cxstack_ix)
		dounwind(cxix);
	    TOPBLOCK(cx);
	    if (CxTYPE(cx) == CXt_EVAL && cx->blk_eval.old_op_type == OP_ENTEREVAL) 
		DIE("Can't goto subroutine from an eval-string");
	    mark = PL_stack_sp;
	    if (CxTYPE(cx) == CXt_SUB &&
		cx->blk_sub.hasargs) {   /* put @_ back onto stack */
		AV* av = cx->blk_sub.argarray;
		
		items = AvFILLp(av) + 1;
		PL_stack_sp++;
		EXTEND(PL_stack_sp, items); /* @_ could have been extended. */
		Copy(AvARRAY(av), PL_stack_sp, items, SV*);
		PL_stack_sp += items;
#ifndef USE_THREADS
		SvREFCNT_dec(GvAV(PL_defgv));
		GvAV(PL_defgv) = cx->blk_sub.savearray;
#endif /* USE_THREADS */
		if (AvREAL(av)) {
		    arg_was_real = 1;
		    AvREAL_off(av);	/* so av_clear() won't clobber elts */
		}
		av_clear(av);
	    }
	    else if (CvXSUB(cv)) {	/* put GvAV(defgv) back onto stack */
		AV* av;
		int i;
#ifdef USE_THREADS
		av = (AV*)PL_curpad[0];
#else
		av = GvAV(PL_defgv);
#endif
		items = AvFILLp(av) + 1;
		PL_stack_sp++;
		EXTEND(PL_stack_sp, items); /* @_ could have been extended. */
		Copy(AvARRAY(av), PL_stack_sp, items, SV*);
		PL_stack_sp += items;
	    }
	    if (CxTYPE(cx) == CXt_SUB &&
		!(CvDEPTH(cx->blk_sub.cv) = cx->blk_sub.olddepth))
		SvREFCNT_dec(cx->blk_sub.cv);
	    oldsave = PL_scopestack[PL_scopestack_ix - 1];
	    LEAVE_SCOPE(oldsave);

	    /* Now do some callish stuff. */
	    SAVETMPS;
	    if (CvXSUB(cv)) {
		if (CvOLDSTYLE(cv)) {
		    I32 (*fp3)_((int,int,int));
		    while (SP > mark) {
			SP[1] = SP[0];
			SP--;
		    }
		    fp3 = (I32(*)_((int,int,int)))CvXSUB(cv);
		    items = (*fp3)(CvXSUBANY(cv).any_i32,
		                   mark - PL_stack_base + 1,
				   items);
		    SP = PL_stack_base + items;
		}
		else {
		    SV **newsp;
		    I32 gimme;

		    PL_stack_sp--;		/* There is no cv arg. */
		    /* Push a mark for the start of arglist */
		    PUSHMARK(mark); 
		    (void)(*CvXSUB(cv))(cv _PERL_OBJECT_THIS);
		    /* Pop the current context like a decent sub should */
		    POPBLOCK(cx, PL_curpm);
		    /* Do _not_ use PUTBACK, keep the XSUB's return stack! */
		}
		LEAVE;
		return pop_return();
	    }
	    else {
		AV* padlist = CvPADLIST(cv);
		SV** svp = AvARRAY(padlist);
		if (CxTYPE(cx) == CXt_EVAL) {
		    PL_in_eval = cx->blk_eval.old_in_eval;
		    PL_eval_root = cx->blk_eval.old_eval_root;
		    cx->cx_type = CXt_SUB;
		    cx->blk_sub.hasargs = 0;
		}
		cx->blk_sub.cv = cv;
		cx->blk_sub.olddepth = CvDEPTH(cv);
		CvDEPTH(cv)++;
		if (CvDEPTH(cv) < 2)
		    (void)SvREFCNT_inc(cv);
		else {	/* save temporaries on recursion? */
		    if (CvDEPTH(cv) == 100 && PL_dowarn)
			sub_crush_depth(cv);
		    if (CvDEPTH(cv) > AvFILLp(padlist)) {
			AV *newpad = newAV();
			SV **oldpad = AvARRAY(svp[CvDEPTH(cv)-1]);
			I32 ix = AvFILLp((AV*)svp[1]);
			svp = AvARRAY(svp[0]);
			for ( ;ix > 0; ix--) {
			    if (svp[ix] != &PL_sv_undef) {
				char *name = SvPVX(svp[ix]);
				if ((SvFLAGS(svp[ix]) & SVf_FAKE)
				    || *name == '&')
				{
				    /* outer lexical or anon code */
				    av_store(newpad, ix,
					SvREFCNT_inc(oldpad[ix]) );
				}
				else {		/* our own lexical */
				    if (*name == '@')
					av_store(newpad, ix, sv = (SV*)newAV());
				    else if (*name == '%')
					av_store(newpad, ix, sv = (SV*)newHV());
				    else
					av_store(newpad, ix, sv = NEWSV(0,0));
				    SvPADMY_on(sv);
				}
			    }
			    else {
				av_store(newpad, ix, sv = NEWSV(0,0));
				SvPADTMP_on(sv);
			    }
			}
			if (cx->blk_sub.hasargs) {
			    AV* av = newAV();
			    av_extend(av, 0);
			    av_store(newpad, 0, (SV*)av);
			    AvFLAGS(av) = AVf_REIFY;
			}
			av_store(padlist, CvDEPTH(cv), (SV*)newpad);
			AvFILLp(padlist) = CvDEPTH(cv);
			svp = AvARRAY(padlist);
		    }
		}
#ifdef USE_THREADS
		if (!cx->blk_sub.hasargs) {
		    AV* av = (AV*)PL_curpad[0];
		    
		    items = AvFILLp(av) + 1;
		    if (items) {
			/* Mark is at the end of the stack. */
			EXTEND(SP, items);
			Copy(AvARRAY(av), SP + 1, items, SV*);
			SP += items;
			PUTBACK ;		    
		    }
		}
#endif /* USE_THREADS */		
		SAVESPTR(PL_curpad);
		PL_curpad = AvARRAY((AV*)svp[CvDEPTH(cv)]);
#ifndef USE_THREADS
		if (cx->blk_sub.hasargs)
#endif /* USE_THREADS */
		{
		    AV* av = (AV*)PL_curpad[0];
		    SV** ary;

#ifndef USE_THREADS
		    cx->blk_sub.savearray = GvAV(PL_defgv);
		    GvAV(PL_defgv) = (AV*)SvREFCNT_inc(av);
#endif /* USE_THREADS */
		    cx->blk_sub.argarray = av;
		    ++mark;

		    if (items >= AvMAX(av) + 1) {
			ary = AvALLOC(av);
			if (AvARRAY(av) != ary) {
			    AvMAX(av) += AvARRAY(av) - AvALLOC(av);
			    SvPVX(av) = (char*)ary;
			}
			if (items >= AvMAX(av) + 1) {
			    AvMAX(av) = items - 1;
			    Renew(ary,items+1,SV*);
			    AvALLOC(av) = ary;
			    SvPVX(av) = (char*)ary;
			}
		    }
		    Copy(mark,AvARRAY(av),items,SV*);
		    AvFILLp(av) = items - 1;
		    /* preserve @_ nature */
		    if (arg_was_real) {
			AvREIFY_off(av);
			AvREAL_on(av);
		    }
		    while (items--) {
			if (*mark)
			    SvTEMP_off(*mark);
			mark++;
		    }
		}
		if (PERLDB_SUB) {	/* Checking curstash breaks DProf. */
		    /*
		     * We do not care about using sv to call CV;
		     * it's for informational purposes only.
		     */
		    SV *sv = GvSV(PL_DBsub);
		    CV *gotocv;
		    
		    if (PERLDB_SUB_NN) {
			SvIVX(sv) = (IV)cv; /* Already upgraded, saved */
		    } else {
			save_item(sv);
			gv_efullname3(sv, CvGV(cv), Nullch);
		    }
		    if (  PERLDB_GOTO
			  && (gotocv = perl_get_cv("DB::goto", FALSE)) ) {
			PUSHMARK( PL_stack_sp );
			perl_call_sv((SV*)gotocv, G_SCALAR | G_NODEBUG);
			PL_stack_sp--;
		    }
		}
		RETURNOP(CvSTART(cv));
	    }
	}
	else
	    label = SvPV(sv,n_a);
    }
    else if (PL_op->op_flags & OPf_SPECIAL) {
	if (! do_dump)
	    DIE("goto must have label");
    }
    else
	label = cPVOP->op_pv;

    if (label && *label) {
	OP *gotoprobe = 0;

	/* find label */

	PL_lastgotoprobe = 0;
	*enterops = 0;
	for (ix = cxstack_ix; ix >= 0; ix--) {
	    cx = &cxstack[ix];
	    switch (CxTYPE(cx)) {
	    case CXt_EVAL:
		gotoprobe = PL_eval_root; /* XXX not good for nested eval */
		break;
	    case CXt_LOOP:
		gotoprobe = cx->blk_oldcop->op_sibling;
		break;
	    case CXt_SUBST:
		continue;
	    case CXt_BLOCK:
		if (ix)
		    gotoprobe = cx->blk_oldcop->op_sibling;
		else
		    gotoprobe = PL_main_root;
		break;
	    case CXt_SUB:
		if (CvDEPTH(cx->blk_sub.cv)) {
		    gotoprobe = CvROOT(cx->blk_sub.cv);
		    break;
		}
		/* FALL THROUGH */
	    case CXt_NULL:
		DIE("Can't \"goto\" outside a block");
	    default:
		if (ix)
		    DIE("panic: goto");
		gotoprobe = PL_main_root;
		break;
	    }
	    retop = dofindlabel(gotoprobe, label,
				enterops, enterops + GOTO_DEPTH);
	    if (retop)
		break;
	    PL_lastgotoprobe = gotoprobe;
	}
	if (!retop)
	    DIE("Can't find label %s", label);

	/* pop unwanted frames */

	if (ix < cxstack_ix) {
	    I32 oldsave;

	    if (ix < 0)
		ix = 0;
	    dounwind(ix);
	    TOPBLOCK(cx);
	    oldsave = PL_scopestack[PL_scopestack_ix];
	    LEAVE_SCOPE(oldsave);
	}

	/* push wanted frames */

	if (*enterops && enterops[1]) {
	    OP *oldop = PL_op;
	    for (ix = 1; enterops[ix]; ix++) {
		PL_op = enterops[ix];
		/* Eventually we may want to stack the needed arguments
		 * for each op.  For now, we punt on the hard ones. */
		if (PL_op->op_type == OP_ENTERITER)
		    DIE("Can't \"goto\" into the middle of a foreach loop",
			label);
		(CALLOP->op_ppaddr)(ARGS);
	    }
	    PL_op = oldop;
	}
    }

    if (do_dump) {
#ifdef VMS
	if (!retop) retop = PL_main_start;
#endif
	PL_restartop = retop;
	PL_do_undump = TRUE;

	my_unexec();

	PL_restartop = 0;		/* hmm, must be GNU unexec().. */
	PL_do_undump = FALSE;
    }

    RETURNOP(retop);
}

PP(pp_exit)
{
    djSP;
    I32 anum;

    if (MAXARG < 1)
	anum = 0;
    else {
	anum = SvIVx(POPs);
#ifdef VMSISH_EXIT
	if (anum == 1 && VMSISH_EXIT)
	    anum = 0;
#endif
    }
    my_exit(anum);
    PUSHs(&PL_sv_undef);
    RETURN;
}

#ifdef NOTYET
PP(pp_nswitch)
{
    djSP;
    double value = SvNVx(GvSV(cCOP->cop_gv));
    register I32 match = I_32(value);

    if (value < 0.0) {
	if (((double)match) > value)
	    --match;		/* was fractional--truncate other way */
    }
    match -= cCOP->uop.scop.scop_offset;
    if (match < 0)
	match = 0;
    else if (match > cCOP->uop.scop.scop_max)
	match = cCOP->uop.scop.scop_max;
    PL_op = cCOP->uop.scop.scop_next[match];
    RETURNOP(PL_op);
}

PP(pp_cswitch)
{
    djSP;
    register I32 match;

    if (PL_multiline)
	PL_op = PL_op->op_next;			/* can't assume anything */
    else {
	STRLEN n_a;
	match = *(SvPVx(GvSV(cCOP->cop_gv), n_a)) & 255;
	match -= cCOP->uop.scop.scop_offset;
	if (match < 0)
	    match = 0;
	else if (match > cCOP->uop.scop.scop_max)
	    match = cCOP->uop.scop.scop_max;
	PL_op = cCOP->uop.scop.scop_next[match];
    }
    RETURNOP(PL_op);
}
#endif

/* Eval. */

STATIC void
save_lines(AV *array, SV *sv)
{
    register char *s = SvPVX(sv);
    register char *send = SvPVX(sv) + SvCUR(sv);
    register char *t;
    register I32 line = 1;

    while (s && s < send) {
	SV *tmpstr = NEWSV(85,0);

	sv_upgrade(tmpstr, SVt_PVMG);
	t = strchr(s, '\n');
	if (t)
	    t++;
	else
	    t = send;

	sv_setpvn(tmpstr, s, t - s);
	av_store(array, line++, tmpstr);
	s = t;
    }
}

STATIC OP *
docatch(OP *o)
{
    dTHR;
    int ret;
    OP *oldop = PL_op;
    dJMPENV;

    PL_op = o;
#ifdef DEBUGGING
    assert(CATCH_GET == TRUE);
    DEBUG_l(deb("Setting up local jumplevel %p, was %p\n", &cur_env, PL_top_env));
#endif
    JMPENV_PUSH(ret);
    switch (ret) {
    default:				/* topmost level handles it */
pass_the_buck:
	JMPENV_POP;
	PL_op = oldop;
	JMPENV_JUMP(ret);
	/* NOTREACHED */
    case 3:
	if (!PL_restartop)
	    goto pass_the_buck;
	PL_op = PL_restartop;
	PL_restartop = 0;
	/* FALL THROUGH */
    case 0:
        CALLRUNOPS();
	break;
    }
    JMPENV_POP;
    PL_op = oldop;
    return Nullop;
}

OP *
sv_compile_2op(SV *sv, OP** startop, char *code, AV** avp)
/* sv Text to convert to OP tree. */
/* startop op_free() this to undo. */
/* code Short string id of the caller. */
{
    dSP;				/* Make POPBLOCK work. */
    PERL_CONTEXT *cx;
    SV **newsp;
    I32 gimme = 0;   /* SUSPECT - INITIALZE TO WHAT?  NI-S */
    I32 optype;
    OP dummy;
    OP *oop = PL_op, *rop;
    char tmpbuf[TYPE_DIGITS(long) + 12 + 10];
    char *safestr;

    ENTER;
    lex_start(sv);
    SAVETMPS;
    /* switch to eval mode */

    if (PL_curcop == &PL_compiling) {
	SAVESPTR(PL_compiling.cop_stash);
	PL_compiling.cop_stash = PL_curstash;
    }
    SAVESPTR(PL_compiling.cop_filegv);
    SAVEI16(PL_compiling.cop_line);
    sprintf(tmpbuf, "_<(%.10s_eval %lu)", code, (unsigned long)++PL_evalseq);
    PL_compiling.cop_filegv = gv_fetchfile(tmpbuf+2);
    PL_compiling.cop_line = 1;
    /* XXX For C<eval "...">s within BEGIN {} blocks, this ends up
       deleting the eval's FILEGV from the stash before gv_check() runs
       (i.e. before run-time proper). To work around the coredump that
       ensues, we always turn GvMULTI_on for any globals that were
       introduced within evals. See force_ident(). GSAR 96-10-12 */
    safestr = savepv(tmpbuf);
    SAVEDELETE(PL_defstash, safestr, strlen(safestr));
    SAVEHINTS();
#ifdef OP_IN_REGISTER
    PL_opsave = op;
#else
    SAVEPPTR(PL_op);
#endif
    PL_hints = 0;

    PL_op = &dummy;
    PL_op->op_type = 0;			/* Avoid uninit warning. */
    PL_op->op_flags = 0;			/* Avoid uninit warning. */
    PUSHBLOCK(cx, CXt_EVAL, SP);
    PUSHEVAL(cx, 0, PL_compiling.cop_filegv);
    rop = doeval(G_SCALAR, startop);
    POPBLOCK(cx,PL_curpm);
    POPEVAL(cx);

    (*startop)->op_type = OP_NULL;
    (*startop)->op_ppaddr = ppaddr[OP_NULL];
    lex_end();
    *avp = (AV*)SvREFCNT_inc(PL_comppad);
    LEAVE;
#ifdef OP_IN_REGISTER
    op = PL_opsave;
#endif
    return rop;
}

/* With USE_THREADS, eval_owner must be held on entry to doeval */
STATIC OP *
doeval(int gimme, OP** startop)
{
    dSP;
    OP *saveop = PL_op;
    HV *newstash;
    CV *caller;
    AV* comppadlist;
    I32 i;

    PL_in_eval = 1;

    PUSHMARK(SP);

    /* set up a scratch pad */

    SAVEI32(PL_padix);
    SAVESPTR(PL_curpad);
    SAVESPTR(PL_comppad);
    SAVESPTR(PL_comppad_name);
    SAVEI32(PL_comppad_name_fill);
    SAVEI32(PL_min_intro_pending);
    SAVEI32(PL_max_intro_pending);

    caller = PL_compcv;
    for (i = cxstack_ix - 1; i >= 0; i--) {
	PERL_CONTEXT *cx = &cxstack[i];
	if (CxTYPE(cx) == CXt_EVAL)
	    break;
	else if (CxTYPE(cx) == CXt_SUB) {
	    caller = cx->blk_sub.cv;
	    break;
	}
    }

    SAVESPTR(PL_compcv);
    PL_compcv = (CV*)NEWSV(1104,0);
    sv_upgrade((SV *)PL_compcv, SVt_PVCV);
    CvEVAL_on(PL_compcv);
#ifdef USE_THREADS
    CvOWNER(PL_compcv) = 0;
    New(666, CvMUTEXP(PL_compcv), 1, perl_mutex);
    MUTEX_INIT(CvMUTEXP(PL_compcv));
#endif /* USE_THREADS */

    PL_comppad = newAV();
    av_push(PL_comppad, Nullsv);
    PL_curpad = AvARRAY(PL_comppad);
    PL_comppad_name = newAV();
    PL_comppad_name_fill = 0;
    PL_min_intro_pending = 0;
    PL_padix = 0;
#ifdef USE_THREADS
    av_store(PL_comppad_name, 0, newSVpv("@_", 2));
    PL_curpad[0] = (SV*)newAV();
    SvPADMY_on(PL_curpad[0]);	/* XXX Needed? */
#endif /* USE_THREADS */

    comppadlist = newAV();
    AvREAL_off(comppadlist);
    av_store(comppadlist, 0, (SV*)PL_comppad_name);
    av_store(comppadlist, 1, (SV*)PL_comppad);
    CvPADLIST(PL_compcv) = comppadlist;

    if (!saveop || saveop->op_type != OP_REQUIRE)
	CvOUTSIDE(PL_compcv) = (CV*)SvREFCNT_inc(caller);

    SAVEFREESV(PL_compcv);

    /* make sure we compile in the right package */

    newstash = PL_curcop->cop_stash;
    if (PL_curstash != newstash) {
	SAVESPTR(PL_curstash);
	PL_curstash = newstash;
    }
    SAVESPTR(PL_beginav);
    PL_beginav = newAV();
    SAVEFREESV(PL_beginav);

    /* try to compile it */

    PL_eval_root = Nullop;
    PL_error_count = 0;
    PL_curcop = &PL_compiling;
    PL_curcop->cop_arybase = 0;
    SvREFCNT_dec(PL_rs);
    PL_rs = newSVpv("\n", 1);
    if (saveop && saveop->op_flags & OPf_SPECIAL)
	PL_in_eval |= 4;
    else
	sv_setpv(ERRSV,"");
    if (yyparse() || PL_error_count || !PL_eval_root) {
	SV **newsp;
	I32 gimme;
	PERL_CONTEXT *cx;
	I32 optype = 0;			/* Might be reset by POPEVAL. */
	STRLEN n_a;

	PL_op = saveop;
	if (PL_eval_root) {
	    op_free(PL_eval_root);
	    PL_eval_root = Nullop;
	}
	SP = PL_stack_base + POPMARK;		/* pop original mark */
	if (!startop) {
	    POPBLOCK(cx,PL_curpm);
	    POPEVAL(cx);
	    pop_return();
	}
	lex_end();
	LEAVE;
	if (optype == OP_REQUIRE) {
	    char* msg = SvPVx(ERRSV, n_a);
	    DIE("%s", *msg ? msg : "Compilation failed in require");
	} else if (startop) {
	    char* msg = SvPVx(ERRSV, n_a);

	    POPBLOCK(cx,PL_curpm);
	    POPEVAL(cx);
	    croak("%sCompilation failed in regexp", (*msg ? msg : "Unknown error\n"));
	}
	SvREFCNT_dec(PL_rs);
	PL_rs = SvREFCNT_inc(PL_nrs);
#ifdef USE_THREADS
	MUTEX_LOCK(&PL_eval_mutex);
	PL_eval_owner = 0;
	COND_SIGNAL(&PL_eval_cond);
	MUTEX_UNLOCK(&PL_eval_mutex);
#endif /* USE_THREADS */
	RETPUSHUNDEF;
    }
    SvREFCNT_dec(PL_rs);
    PL_rs = SvREFCNT_inc(PL_nrs);
    PL_compiling.cop_line = 0;
    if (startop) {
	*startop = PL_eval_root;
	SvREFCNT_dec(CvOUTSIDE(PL_compcv));
	CvOUTSIDE(PL_compcv) = Nullcv;
    } else
	SAVEFREEOP(PL_eval_root);
    if (gimme & G_VOID)
	scalarvoid(PL_eval_root);
    else if (gimme & G_ARRAY)
	list(PL_eval_root);
    else
	scalar(PL_eval_root);

    DEBUG_x(dump_eval());

    /* Register with debugger: */
    if (PERLDB_INTER && saveop->op_type == OP_REQUIRE) {
	CV *cv = perl_get_cv("DB::postponed", FALSE);
	if (cv) {
	    dSP;
	    PUSHMARK(SP);
	    XPUSHs((SV*)PL_compiling.cop_filegv);
	    PUTBACK;
	    perl_call_sv((SV*)cv, G_DISCARD);
	}
    }

    /* compiled okay, so do it */

    CvDEPTH(PL_compcv) = 1;
    SP = PL_stack_base + POPMARK;		/* pop original mark */
    PL_op = saveop;			/* The caller may need it. */
#ifdef USE_THREADS
    MUTEX_LOCK(&PL_eval_mutex);
    PL_eval_owner = 0;
    COND_SIGNAL(&PL_eval_cond);
    MUTEX_UNLOCK(&PL_eval_mutex);
#endif /* USE_THREADS */

    RETURNOP(PL_eval_start);
}

PP(pp_require)
{
    djSP;
    register PERL_CONTEXT *cx;
    SV *sv;
    char *name;
    STRLEN len;
    char *tryname;
    SV *namesv = Nullsv;
    SV** svp;
    I32 gimme = G_SCALAR;
    PerlIO *tryrsfp = 0;
    STRLEN n_a;

    sv = POPs;
    if (SvNIOKp(sv) && !SvPOKp(sv)) {
	SET_NUMERIC_STANDARD();
	if (atof(PL_patchlevel) + 0.00000999 < SvNV(sv))
	    DIE("Perl %s required--this is only version %s, stopped",
		SvPV(sv,n_a),PL_patchlevel);
	RETPUSHYES;
    }
    name = SvPV(sv, len);
    if (!(name && len > 0 && *name))
	DIE("Null filename used");
    TAINT_PROPER("require");
    if (PL_op->op_type == OP_REQUIRE &&
      (svp = hv_fetch(GvHVn(PL_incgv), name, len, 0)) &&
      *svp != &PL_sv_undef)
	RETPUSHYES;

    /* prepare to compile file */

    if (*name == '/' ||
	(*name == '.' && 
	    (name[1] == '/' ||
	     (name[1] == '.' && name[2] == '/')))
#ifdef DOSISH
      || (name[0] && name[1] == ':')
#endif
#ifdef WIN32
      || (name[0] == '\\' && name[1] == '\\')	/* UNC path */
#endif
#ifdef VMS
	|| (strchr(name,':')  || ((*name == '[' || *name == '<') &&
	    (isALNUM(name[1]) || strchr("$-_]>",name[1]))))
#endif
    )
    {
	tryname = name;
	tryrsfp = PerlIO_open(name,PERL_SCRIPT_MODE);
    }
    else {
	AV *ar = GvAVn(PL_incgv);
	I32 i;
#ifdef VMS
	char *unixname;
	if ((unixname = tounixspec(name, Nullch)) != Nullch)
#endif
	{
	    namesv = NEWSV(806, 0);
	    for (i = 0; i <= AvFILL(ar); i++) {
		char *dir = SvPVx(*av_fetch(ar, i, TRUE), n_a);
#ifdef VMS
		char *unixdir;
		if ((unixdir = tounixpath(dir, Nullch)) == Nullch)
		    continue;
		sv_setpv(namesv, unixdir);
		sv_catpv(namesv, unixname);
#else
		sv_setpvf(namesv, "%s/%s", dir, name);
#endif
		TAINT_PROPER("require");
		tryname = SvPVX(namesv);
		tryrsfp = PerlIO_open(tryname, PERL_SCRIPT_MODE);
		if (tryrsfp) {
		    if (tryname[0] == '.' && tryname[1] == '/')
			tryname += 2;
		    break;
		}
	    }
	}
    }
    SAVESPTR(PL_compiling.cop_filegv);
    PL_compiling.cop_filegv = gv_fetchfile(tryrsfp ? tryname : name);
    SvREFCNT_dec(namesv);
    if (!tryrsfp) {
	if (PL_op->op_type == OP_REQUIRE) {
	    SV *msg = sv_2mortal(newSVpvf("Can't locate %s in @INC", name));
	    SV *dirmsgsv = NEWSV(0, 0);
	    AV *ar = GvAVn(PL_incgv);
	    I32 i;
	    if (instr(SvPVX(msg), ".h "))
		sv_catpv(msg, " (change .h to .ph maybe?)");
	    if (instr(SvPVX(msg), ".ph "))
		sv_catpv(msg, " (did you run h2ph?)");
	    sv_catpv(msg, " (@INC contains:");
	    for (i = 0; i <= AvFILL(ar); i++) {
		char *dir = SvPVx(*av_fetch(ar, i, TRUE), n_a);
		sv_setpvf(dirmsgsv, " %s", dir);
	        sv_catsv(msg, dirmsgsv);
	    }
	    sv_catpvn(msg, ")", 1);
    	    SvREFCNT_dec(dirmsgsv);
	    DIE("%_", msg);
	}

	RETPUSHUNDEF;
    }
    else
	SETERRNO(0, SS$_NORMAL);

    /* Assume success here to prevent recursive requirement. */
    (void)hv_store(GvHVn(PL_incgv), name, strlen(name),
	newSVsv(GvSV(PL_compiling.cop_filegv)), 0 );

    ENTER;
    SAVETMPS;
    lex_start(sv_2mortal(newSVpv("",0)));
    SAVEGENERICSV(PL_rsfp_filters);
    PL_rsfp_filters = Nullav;

    PL_rsfp = tryrsfp;
    name = savepv(name);
    SAVEFREEPV(name);
    SAVEHINTS();
    PL_hints = 0;
 
    /* switch to eval mode */

    push_return(PL_op->op_next);
    PUSHBLOCK(cx, CXt_EVAL, SP);
    PUSHEVAL(cx, name, PL_compiling.cop_filegv);

    SAVEI16(PL_compiling.cop_line);
    PL_compiling.cop_line = 0;

    PUTBACK;
#ifdef USE_THREADS
    MUTEX_LOCK(&PL_eval_mutex);
    if (PL_eval_owner && PL_eval_owner != thr)
	while (PL_eval_owner)
	    COND_WAIT(&PL_eval_cond, &PL_eval_mutex);
    PL_eval_owner = thr;
    MUTEX_UNLOCK(&PL_eval_mutex);
#endif /* USE_THREADS */
    return DOCATCH(doeval(G_SCALAR, NULL));
}

PP(pp_dofile)
{
    return pp_require(ARGS);
}

PP(pp_entereval)
{
    djSP;
    register PERL_CONTEXT *cx;
    dPOPss;
    I32 gimme = GIMME_V, was = PL_sub_generation;
    char tmpbuf[TYPE_DIGITS(long) + 12];
    char *safestr;
    STRLEN len;
    OP *ret;

    if (!SvPV(sv,len) || !len)
	RETPUSHUNDEF;
    TAINT_PROPER("eval");

    ENTER;
    lex_start(sv);
    SAVETMPS;
 
    /* switch to eval mode */

    SAVESPTR(PL_compiling.cop_filegv);
    sprintf(tmpbuf, "_<(eval %lu)", (unsigned long)++PL_evalseq);
    PL_compiling.cop_filegv = gv_fetchfile(tmpbuf+2);
    PL_compiling.cop_line = 1;
    /* XXX For C<eval "...">s within BEGIN {} blocks, this ends up
       deleting the eval's FILEGV from the stash before gv_check() runs
       (i.e. before run-time proper). To work around the coredump that
       ensues, we always turn GvMULTI_on for any globals that were
       introduced within evals. See force_ident(). GSAR 96-10-12 */
    safestr = savepv(tmpbuf);
    SAVEDELETE(PL_defstash, safestr, strlen(safestr));
    SAVEHINTS();
    PL_hints = PL_op->op_targ;

    push_return(PL_op->op_next);
    PUSHBLOCK(cx, (CXt_EVAL|CXp_REAL), SP);
    PUSHEVAL(cx, 0, PL_compiling.cop_filegv);

    /* prepare to compile string */

    if (PERLDB_LINE && PL_curstash != PL_debstash)
	save_lines(GvAV(PL_compiling.cop_filegv), PL_linestr);
    PUTBACK;
#ifdef USE_THREADS
    MUTEX_LOCK(&PL_eval_mutex);
    if (PL_eval_owner && PL_eval_owner != thr)
	while (PL_eval_owner)
	    COND_WAIT(&PL_eval_cond, &PL_eval_mutex);
    PL_eval_owner = thr;
    MUTEX_UNLOCK(&PL_eval_mutex);
#endif /* USE_THREADS */
    ret = doeval(gimme, NULL);
    if (PERLDB_INTER && was != PL_sub_generation /* Some subs defined here. */
	&& ret != PL_op->op_next) {	/* Successive compilation. */
	strcpy(safestr, "_<(eval )");	/* Anything fake and short. */
    }
    return DOCATCH(ret);
}

PP(pp_leaveeval)
{
    djSP;
    register SV **mark;
    SV **newsp;
    PMOP *newpm;
    I32 gimme;
    register PERL_CONTEXT *cx;
    OP *retop;
    U8 save_flags = PL_op -> op_flags;
    I32 optype;

    POPBLOCK(cx,newpm);
    POPEVAL(cx);
    retop = pop_return();

    TAINT_NOT;
    if (gimme == G_VOID)
	MARK = newsp;
    else if (gimme == G_SCALAR) {
	MARK = newsp + 1;
	if (MARK <= SP) {
	    if (SvFLAGS(TOPs) & SVs_TEMP)
		*MARK = TOPs;
	    else
		*MARK = sv_mortalcopy(TOPs);
	}
	else {
	    MEXTEND(mark,0);
	    *MARK = &PL_sv_undef;
	}
    }
    else {
	/* in case LEAVE wipes old return values */
	for (mark = newsp + 1; mark <= SP; mark++) {
	    if (!(SvFLAGS(*mark) & SVs_TEMP)) {
		*mark = sv_mortalcopy(*mark);
		TAINT_NOT;	/* Each item is independent */
	    }
	}
    }
    PL_curpm = newpm;	/* Don't pop $1 et al till now */

    /*
     * Closures mentioned at top level of eval cannot be referenced
     * again, and their presence indirectly causes a memory leak.
     * (Note that the fact that compcv and friends are still set here
     * is, AFAIK, an accident.)  --Chip
     */
    if (AvFILLp(PL_comppad_name) >= 0) {
	SV **svp = AvARRAY(PL_comppad_name);
	I32 ix;
	for (ix = AvFILLp(PL_comppad_name); ix >= 0; ix--) {
	    SV *sv = svp[ix];
	    if (sv && sv != &PL_sv_undef && *SvPVX(sv) == '&') {
		SvREFCNT_dec(sv);
		svp[ix] = &PL_sv_undef;

		sv = PL_curpad[ix];
		if (CvCLONE(sv)) {
		    SvREFCNT_dec(CvOUTSIDE(sv));
		    CvOUTSIDE(sv) = Nullcv;
		}
		else {
		    SvREFCNT_dec(sv);
		    sv = NEWSV(0,0);
		    SvPADTMP_on(sv);
		    PL_curpad[ix] = sv;
		}
	    }
	}
    }

#ifdef DEBUGGING
    assert(CvDEPTH(PL_compcv) == 1);
#endif
    CvDEPTH(PL_compcv) = 0;
    lex_end();

    if (optype == OP_REQUIRE &&
	!(gimme == G_SCALAR ? SvTRUE(*SP) : SP > newsp))
    {
	/* Unassume the success we assumed earlier. */
	char *name = cx->blk_eval.old_name;
	(void)hv_delete(GvHVn(PL_incgv), name, strlen(name), G_DISCARD);
	retop = die("%s did not return a true value", name);
	/* die_where() did LEAVE, or we won't be here */
    }
    else {
	LEAVE;
	if (!(save_flags & OPf_SPECIAL))
	    sv_setpv(ERRSV,"");
    }

    RETURNOP(retop);
}

PP(pp_entertry)
{
    djSP;
    register PERL_CONTEXT *cx;
    I32 gimme = GIMME_V;

    ENTER;
    SAVETMPS;

    push_return(cLOGOP->op_other->op_next);
    PUSHBLOCK(cx, CXt_EVAL, SP);
    PUSHEVAL(cx, 0, 0);
    PL_eval_root = PL_op;		/* Only needed so that goto works right. */

    PL_in_eval = 1;
    sv_setpv(ERRSV,"");
    PUTBACK;
    return DOCATCH(PL_op->op_next);
}

PP(pp_leavetry)
{
    djSP;
    register SV **mark;
    SV **newsp;
    PMOP *newpm;
    I32 gimme;
    register PERL_CONTEXT *cx;
    I32 optype;

    POPBLOCK(cx,newpm);
    POPEVAL(cx);
    pop_return();

    TAINT_NOT;
    if (gimme == G_VOID)
	SP = newsp;
    else if (gimme == G_SCALAR) {
	MARK = newsp + 1;
	if (MARK <= SP) {
	    if (SvFLAGS(TOPs) & (SVs_PADTMP|SVs_TEMP))
		*MARK = TOPs;
	    else
		*MARK = sv_mortalcopy(TOPs);
	}
	else {
	    MEXTEND(mark,0);
	    *MARK = &PL_sv_undef;
	}
	SP = MARK;
    }
    else {
	/* in case LEAVE wipes old return values */
	for (mark = newsp + 1; mark <= SP; mark++) {
	    if (!(SvFLAGS(*mark) & (SVs_PADTMP|SVs_TEMP))) {
		*mark = sv_mortalcopy(*mark);
		TAINT_NOT;	/* Each item is independent */
	    }
	}
    }
    PL_curpm = newpm;	/* Don't pop $1 et al till now */

    LEAVE;
    sv_setpv(ERRSV,"");
    RETURN;
}

STATIC void
doparseform(SV *sv)
{
    STRLEN len;
    register char *s = SvPV_force(sv, len);
    register char *send = s + len;
    register char *base;
    register I32 skipspaces = 0;
    bool noblank;
    bool repeat;
    bool postspace = FALSE;
    U16 *fops;
    register U16 *fpc;
    U16 *linepc;
    register I32 arg;
    bool ischop;

    if (len == 0)
	croak("Null picture in formline");
    
    New(804, fops, (send - s)*3+10, U16);    /* Almost certainly too long... */
    fpc = fops;

    if (s < send) {
	linepc = fpc;
	*fpc++ = FF_LINEMARK;
	noblank = repeat = FALSE;
	base = s;
    }

    while (s <= send) {
	switch (*s++) {
	default:
	    skipspaces = 0;
	    continue;

	case '~':
	    if (*s == '~') {
		repeat = TRUE;
		*s = ' ';
	    }
	    noblank = TRUE;
	    s[-1] = ' ';
	    /* FALL THROUGH */
	case ' ': case '\t':
	    skipspaces++;
	    continue;
	    
	case '\n': case 0:
	    arg = s - base;
	    skipspaces++;
	    arg -= skipspaces;
	    if (arg) {
		if (postspace)
		    *fpc++ = FF_SPACE;
		*fpc++ = FF_LITERAL;
		*fpc++ = arg;
	    }
	    postspace = FALSE;
	    if (s <= send)
		skipspaces--;
	    if (skipspaces) {
		*fpc++ = FF_SKIP;
		*fpc++ = skipspaces;
	    }
	    skipspaces = 0;
	    if (s <= send)
		*fpc++ = FF_NEWLINE;
	    if (noblank) {
		*fpc++ = FF_BLANK;
		if (repeat)
		    arg = fpc - linepc + 1;
		else
		    arg = 0;
		*fpc++ = arg;
	    }
	    if (s < send) {
		linepc = fpc;
		*fpc++ = FF_LINEMARK;
		noblank = repeat = FALSE;
		base = s;
	    }
	    else
		s++;
	    continue;

	case '@':
	case '^':
	    ischop = s[-1] == '^';

	    if (postspace) {
		*fpc++ = FF_SPACE;
		postspace = FALSE;
	    }
	    arg = (s - base) - 1;
	    if (arg) {
		*fpc++ = FF_LITERAL;
		*fpc++ = arg;
	    }

	    base = s - 1;
	    *fpc++ = FF_FETCH;
	    if (*s == '*') {
		s++;
		*fpc++ = 0;
		*fpc++ = FF_LINEGLOB;
	    }
	    else if (*s == '#' || (*s == '.' && s[1] == '#')) {
		arg = ischop ? 512 : 0;
		base = s - 1;
		while (*s == '#')
		    s++;
		if (*s == '.') {
		    char *f;
		    s++;
		    f = s;
		    while (*s == '#')
			s++;
		    arg |= 256 + (s - f);
		}
		*fpc++ = s - base;		/* fieldsize for FETCH */
		*fpc++ = FF_DECIMAL;
		*fpc++ = arg;
	    }
	    else {
		I32 prespace = 0;
		bool ismore = FALSE;

		if (*s == '>') {
		    while (*++s == '>') ;
		    prespace = FF_SPACE;
		}
		else if (*s == '|') {
		    while (*++s == '|') ;
		    prespace = FF_HALFSPACE;
		    postspace = TRUE;
		}
		else {
		    if (*s == '<')
			while (*++s == '<') ;
		    postspace = TRUE;
		}
		if (*s == '.' && s[1] == '.' && s[2] == '.') {
		    s += 3;
		    ismore = TRUE;
		}
		*fpc++ = s - base;		/* fieldsize for FETCH */

		*fpc++ = ischop ? FF_CHECKCHOP : FF_CHECKNL;

		if (prespace)
		    *fpc++ = prespace;
		*fpc++ = FF_ITEM;
		if (ismore)
		    *fpc++ = FF_MORE;
		if (ischop)
		    *fpc++ = FF_CHOP;
	    }
	    base = s;
	    skipspaces = 0;
	    continue;
	}
    }
    *fpc++ = FF_END;

    arg = fpc - fops;
    { /* need to jump to the next word */
        int z;
	z = WORD_ALIGN - SvCUR(sv) % WORD_ALIGN;
	SvGROW(sv, SvCUR(sv) + z + arg * sizeof(U16) + 4);
	s = SvPVX(sv) + SvCUR(sv) + z;
    }
    Copy(fops, s, arg, U16);
    Safefree(fops);
    sv_magic(sv, Nullsv, 'f', Nullch, 0);
    SvCOMPILED_on(sv);
}

/*
 * The rest of this file was derived from source code contributed
 * by Tom Horsley.
 *
 * NOTE: this code was derived from Tom Horsley's qsort replacement
 * and should not be confused with the original code.
 */

/* Copyright (C) Tom Horsley, 1997. All rights reserved.

   Permission granted to distribute under the same terms as perl which are
   (briefly):

    This program is free software; you can redistribute it and/or modify
    it under the terms of either:

	a) the GNU General Public License as published by the Free
	Software Foundation; either version 1, or (at your option) any
	later version, or

	b) the "Artistic License" which comes with this Kit.

   Details on the perl license can be found in the perl source code which
   may be located via the www.perl.com web page.

   This is the most wonderfulest possible qsort I can come up with (and
   still be mostly portable) My (limited) tests indicate it consistently
   does about 20% fewer calls to compare than does the qsort in the Visual
   C++ library, other vendors may vary.

   Some of the ideas in here can be found in "Algorithms" by Sedgewick,
   others I invented myself (or more likely re-invented since they seemed
   pretty obvious once I watched the algorithm operate for a while).

   Most of this code was written while watching the Marlins sweep the Giants
   in the 1997 National League Playoffs - no Braves fans allowed to use this
   code (just kidding :-).

   I realize that if I wanted to be true to the perl tradition, the only
   comment in this file would be something like:

   ...they shuffled back towards the rear of the line. 'No, not at the
   rear!'  the slave-driver shouted. 'Three files up. And stay there...

   However, I really needed to violate that tradition just so I could keep
   track of what happens myself, not to mention some poor fool trying to
   understand this years from now :-).
*/

/* ********************************************************** Configuration */

#ifndef QSORT_ORDER_GUESS
#define QSORT_ORDER_GUESS 2	/* Select doubling version of the netBSD trick */
#endif

/* QSORT_MAX_STACK is the largest number of partitions that can be stacked up for
   future processing - a good max upper bound is log base 2 of memory size
   (32 on 32 bit machines, 64 on 64 bit machines, etc). In reality can
   safely be smaller than that since the program is taking up some space and
   most operating systems only let you grab some subset of contiguous
   memory (not to mention that you are normally sorting data larger than
   1 byte element size :-).
*/
#ifndef QSORT_MAX_STACK
#define QSORT_MAX_STACK 32
#endif

/* QSORT_BREAK_EVEN is the size of the largest partition we should insertion sort.
   Anything bigger and we use qsort. If you make this too small, the qsort
   will probably break (or become less efficient), because it doesn't expect
   the middle element of a partition to be the same as the right or left -
   you have been warned).
*/
#ifndef QSORT_BREAK_EVEN
#define QSORT_BREAK_EVEN 6
#endif

/* ************************************************************* Data Types */

/* hold left and right index values of a partition waiting to be sorted (the
   partition includes both left and right - right is NOT one past the end or
   anything like that).
*/
struct partition_stack_entry {
   int left;
   int right;
#ifdef QSORT_ORDER_GUESS
   int qsort_break_even;
#endif
};

/* ******************************************************* Shorthand Macros */

/* Note that these macros will be used from inside the qsort function where
   we happen to know that the variable 'elt_size' contains the size of an
   array element and the variable 'temp' points to enough space to hold a
   temp element and the variable 'array' points to the array being sorted
   and 'compare' is the pointer to the compare routine.

   Also note that there are very many highly architecture specific ways
   these might be sped up, but this is simply the most generally portable
   code I could think of.
*/

/* Return < 0 == 0 or > 0 as the value of elt1 is < elt2, == elt2, > elt2
*/
#ifdef PERL_OBJECT
#define qsort_cmp(elt1, elt2) \
   ((this->*compare)(array[elt1], array[elt2]))
#else
#define qsort_cmp(elt1, elt2) \
   ((*compare)(array[elt1], array[elt2]))
#endif

#ifdef QSORT_ORDER_GUESS
#define QSORT_NOTICE_SWAP swapped++;
#else
#define QSORT_NOTICE_SWAP
#endif

/* swaps contents of array elements elt1, elt2.
*/
#define qsort_swap(elt1, elt2) \
   STMT_START { \
      QSORT_NOTICE_SWAP \
      temp = array[elt1]; \
      array[elt1] = array[elt2]; \
      array[elt2] = temp; \
   } STMT_END

/* rotate contents of elt1, elt2, elt3 such that elt1 gets elt2, elt2 gets
   elt3 and elt3 gets elt1.
*/
#define qsort_rotate(elt1, elt2, elt3) \
   STMT_START { \
      QSORT_NOTICE_SWAP \
      temp = array[elt1]; \
      array[elt1] = array[elt2]; \
      array[elt2] = array[elt3]; \
      array[elt3] = temp; \
   } STMT_END

/* ************************************************************ Debug stuff */

#ifdef QSORT_DEBUG

static void
break_here()
{
   return; /* good place to set a breakpoint */
}

#define qsort_assert(t) (void)( (t) || (break_here(), 0) )

static void
doqsort_all_asserts(
   void * array,
   size_t num_elts,
   size_t elt_size,
   int (*compare)(const void * elt1, const void * elt2),
   int pc_left, int pc_right, int u_left, int u_right)
{
   int i;

   qsort_assert(pc_left <= pc_right);
   qsort_assert(u_right < pc_left);
   qsort_assert(pc_right < u_left);
   for (i = u_right + 1; i < pc_left; ++i) {
      qsort_assert(qsort_cmp(i, pc_left) < 0);
   }
   for (i = pc_left; i < pc_right; ++i) {
      qsort_assert(qsort_cmp(i, pc_right) == 0);
   }
   for (i = pc_right + 1; i < u_left; ++i) {
      qsort_assert(qsort_cmp(pc_right, i) < 0);
   }
}

#define qsort_all_asserts(PC_LEFT, PC_RIGHT, U_LEFT, U_RIGHT) \
   doqsort_all_asserts(array, num_elts, elt_size, compare, \
                 PC_LEFT, PC_RIGHT, U_LEFT, U_RIGHT)

#else

#define qsort_assert(t) ((void)0)

#define qsort_all_asserts(PC_LEFT, PC_RIGHT, U_LEFT, U_RIGHT) ((void)0)

#endif

/* ****************************************************************** qsort */

STATIC void
#ifdef PERL_OBJECT
qsortsv(SV ** array, size_t num_elts, SVCOMPARE compare)
#else
qsortsv(
   SV ** array,
   size_t num_elts,
   I32 (*compare)(SV *a, SV *b))
#endif
{
   register SV * temp;

   struct partition_stack_entry partition_stack[QSORT_MAX_STACK];
   int next_stack_entry = 0;

   int part_left;
   int part_right;
#ifdef QSORT_ORDER_GUESS
   int qsort_break_even;
   int swapped;
#endif

   /* Make sure we actually have work to do.
   */
   if (num_elts <= 1) {
      return;
   }

   /* Setup the initial partition definition and fall into the sorting loop
   */
   part_left = 0;
   part_right = (int)(num_elts - 1);
#ifdef QSORT_ORDER_GUESS
   qsort_break_even = QSORT_BREAK_EVEN;
#else
#define qsort_break_even QSORT_BREAK_EVEN
#endif
   for ( ; ; ) {
      if ((part_right - part_left) >= qsort_break_even) {
         /* OK, this is gonna get hairy, so lets try to document all the
            concepts and abbreviations and variables and what they keep
            track of:

            pc: pivot chunk - the set of array elements we accumulate in the
                middle of the partition, all equal in value to the original
                pivot element selected. The pc is defined by:

                pc_left - the leftmost array index of the pc
                pc_right - the rightmost array index of the pc

                we start with pc_left == pc_right and only one element
                in the pivot chunk (but it can grow during the scan).

            u:  uncompared elements - the set of elements in the partition
                we have not yet compared to the pivot value. There are two
                uncompared sets during the scan - one to the left of the pc
                and one to the right.

                u_right - the rightmost index of the left side's uncompared set
                u_left - the leftmost index of the right side's uncompared set

                The leftmost index of the left sides's uncompared set
                doesn't need its own variable because it is always defined
                by the leftmost edge of the whole partition (part_left). The
                same goes for the rightmost edge of the right partition
                (part_right).

                We know there are no uncompared elements on the left once we
                get u_right < part_left and no uncompared elements on the
                right once u_left > part_right. When both these conditions
                are met, we have completed the scan of the partition.

                Any elements which are between the pivot chunk and the
                uncompared elements should be less than the pivot value on
                the left side and greater than the pivot value on the right
                side (in fact, the goal of the whole algorithm is to arrange
                for that to be true and make the groups of less-than and
                greater-then elements into new partitions to sort again).

            As you marvel at the complexity of the code and wonder why it
            has to be so confusing. Consider some of the things this level
            of confusion brings:

            Once I do a compare, I squeeze every ounce of juice out of it. I
            never do compare calls I don't have to do, and I certainly never
            do redundant calls.

            I also never swap any elements unless I can prove there is a
            good reason. Many sort algorithms will swap a known value with
            an uncompared value just to get things in the right place (or
            avoid complexity :-), but that uncompared value, once it gets
            compared, may then have to be swapped again. A lot of the
            complexity of this code is due to the fact that it never swaps
            anything except compared values, and it only swaps them when the
            compare shows they are out of position.
         */
         int pc_left, pc_right;
         int u_right, u_left;

         int s;

         pc_left = ((part_left + part_right) / 2);
         pc_right = pc_left;
         u_right = pc_left - 1;
         u_left = pc_right + 1;

         /* Qsort works best when the pivot value is also the median value
            in the partition (unfortunately you can't find the median value
            without first sorting :-), so to give the algorithm a helping
            hand, we pick 3 elements and sort them and use the median value
            of that tiny set as the pivot value.

            Some versions of qsort like to use the left middle and right as
            the 3 elements to sort so they can insure the ends of the
            partition will contain values which will stop the scan in the
            compare loop, but when you have to call an arbitrarily complex
            routine to do a compare, its really better to just keep track of
            array index values to know when you hit the edge of the
            partition and avoid the extra compare. An even better reason to
            avoid using a compare call is the fact that you can drop off the
            edge of the array if someone foolishly provides you with an
            unstable compare function that doesn't always provide consistent
            results.

            So, since it is simpler for us to compare the three adjacent
            elements in the middle of the partition, those are the ones we
            pick here (conveniently pointed at by u_right, pc_left, and
            u_left). The values of the left, center, and right elements
            are refered to as l c and r in the following comments.
         */

#ifdef QSORT_ORDER_GUESS
         swapped = 0;
#endif
         s = qsort_cmp(u_right, pc_left);
         if (s < 0) {
            /* l < c */
            s = qsort_cmp(pc_left, u_left);
            /* if l < c, c < r - already in order - nothing to do */
            if (s == 0) {
               /* l < c, c == r - already in order, pc grows */
               ++pc_right;
               qsort_all_asserts(pc_left, pc_right, u_left + 1, u_right - 1);
            } else if (s > 0) {
               /* l < c, c > r - need to know more */
               s = qsort_cmp(u_right, u_left);
               if (s < 0) {
                  /* l < c, c > r, l < r - swap c & r to get ordered */
                  qsort_swap(pc_left, u_left);
                  qsort_all_asserts(pc_left, pc_right, u_left + 1, u_right - 1);
               } else if (s == 0) {
                  /* l < c, c > r, l == r - swap c&r, grow pc */
                  qsort_swap(pc_left, u_left);
                  --pc_left;
                  qsort_all_asserts(pc_left, pc_right, u_left + 1, u_right - 1);
               } else {
                  /* l < c, c > r, l > r - make lcr into rlc to get ordered */
                  qsort_rotate(pc_left, u_right, u_left);
                  qsort_all_asserts(pc_left, pc_right, u_left + 1, u_right - 1);
               }
            }
         } else if (s == 0) {
            /* l == c */
            s = qsort_cmp(pc_left, u_left);
            if (s < 0) {
               /* l == c, c < r - already in order, grow pc */
               --pc_left;
               qsort_all_asserts(pc_left, pc_right, u_left + 1, u_right - 1);
            } else if (s == 0) {
               /* l == c, c == r - already in order, grow pc both ways */
               --pc_left;
               ++pc_right;
               qsort_all_asserts(pc_left, pc_right, u_left + 1, u_right - 1);
            } else {
               /* l == c, c > r - swap l & r, grow pc */
               qsort_swap(u_right, u_left);
               ++pc_right;
               qsort_all_asserts(pc_left, pc_right, u_left + 1, u_right - 1);
            }
         } else {
            /* l > c */
            s = qsort_cmp(pc_left, u_left);
            if (s < 0) {
               /* l > c, c < r - need to know more */
               s = qsort_cmp(u_right, u_left);
               if (s < 0) {
                  /* l > c, c < r, l < r - swap l & c to get ordered */
                  qsort_swap(u_right, pc_left);
                  qsort_all_asserts(pc_left, pc_right, u_left + 1, u_right - 1);
               } else if (s == 0) {
                  /* l > c, c < r, l == r - swap l & c, grow pc */
                  qsort_swap(u_right, pc_left);
                  ++pc_right;
                  qsort_all_asserts(pc_left, pc_right, u_left + 1, u_right - 1);
               } else {
                  /* l > c, c < r, l > r - rotate lcr into crl to order */
                  qsort_rotate(u_right, pc_left, u_left);
                  qsort_all_asserts(pc_left, pc_right, u_left + 1, u_right - 1);
               }
            } else if (s == 0) {
               /* l > c, c == r - swap ends, grow pc */
               qsort_swap(u_right, u_left);
               --pc_left;
               qsort_all_asserts(pc_left, pc_right, u_left + 1, u_right - 1);
            } else {
               /* l > c, c > r - swap ends to get in order */
               qsort_swap(u_right, u_left);
               qsort_all_asserts(pc_left, pc_right, u_left + 1, u_right - 1);
            }
         }
         /* We now know the 3 middle elements have been compared and
            arranged in the desired order, so we can shrink the uncompared
            sets on both sides
         */
         --u_right;
         ++u_left;
         qsort_all_asserts(pc_left, pc_right, u_left, u_right);

         /* The above massive nested if was the simple part :-). We now have
            the middle 3 elements ordered and we need to scan through the
            uncompared sets on either side, swapping elements that are on
            the wrong side or simply shuffling equal elements around to get
            all equal elements into the pivot chunk.
         */

         for ( ; ; ) {
            int still_work_on_left;
            int still_work_on_right;

            /* Scan the uncompared values on the left. If I find a value
               equal to the pivot value, move it over so it is adjacent to
               the pivot chunk and expand the pivot chunk. If I find a value
               less than the pivot value, then just leave it - its already
               on the correct side of the partition. If I find a greater
               value, then stop the scan.
            */
            while (still_work_on_left = (u_right >= part_left)) {
               s = qsort_cmp(u_right, pc_left);
               if (s < 0) {
                  --u_right;
               } else if (s == 0) {
                  --pc_left;
                  if (pc_left != u_right) {
                     qsort_swap(u_right, pc_left);
                  }
                  --u_right;
               } else {
                  break;
               }
               qsort_assert(u_right < pc_left);
               qsort_assert(pc_left <= pc_right);
               qsort_assert(qsort_cmp(u_right + 1, pc_left) <= 0);
               qsort_assert(qsort_cmp(pc_left, pc_right) == 0);
            }

            /* Do a mirror image scan of uncompared values on the right
            */
            while (still_work_on_right = (u_left <= part_right)) {
               s = qsort_cmp(pc_right, u_left);
               if (s < 0) {
                  ++u_left;
               } else if (s == 0) {
                  ++pc_right;
                  if (pc_right != u_left) {
                     qsort_swap(pc_right, u_left);
                  }
                  ++u_left;
               } else {
                  break;
               }
               qsort_assert(u_left > pc_right);
               qsort_assert(pc_left <= pc_right);
               qsort_assert(qsort_cmp(pc_right, u_left - 1) <= 0);
               qsort_assert(qsort_cmp(pc_left, pc_right) == 0);
            }

            if (still_work_on_left) {
               /* I know I have a value on the left side which needs to be
                  on the right side, but I need to know more to decide
                  exactly the best thing to do with it.
               */
               if (still_work_on_right) {
                  /* I know I have values on both side which are out of
                     position. This is a big win because I kill two birds
                     with one swap (so to speak). I can advance the
                     uncompared pointers on both sides after swapping both
                     of them into the right place.
                  */
                  qsort_swap(u_right, u_left);
                  --u_right;
                  ++u_left;
                  qsort_all_asserts(pc_left, pc_right, u_left, u_right);
               } else {
                  /* I have an out of position value on the left, but the
                     right is fully scanned, so I "slide" the pivot chunk
                     and any less-than values left one to make room for the
                     greater value over on the right. If the out of position
                     value is immediately adjacent to the pivot chunk (there
                     are no less-than values), I can do that with a swap,
                     otherwise, I have to rotate one of the less than values
                     into the former position of the out of position value
                     and the right end of the pivot chunk into the left end
                     (got all that?).
                  */
                  --pc_left;
                  if (pc_left == u_right) {
                     qsort_swap(u_right, pc_right);
                     qsort_all_asserts(pc_left, pc_right-1, u_left, u_right-1);
                  } else {
                     qsort_rotate(u_right, pc_left, pc_right);
                     qsort_all_asserts(pc_left, pc_right-1, u_left, u_right-1);
                  }
                  --pc_right;
                  --u_right;
               }
            } else if (still_work_on_right) {
               /* Mirror image of complex case above: I have an out of
                  position value on the right, but the left is fully
                  scanned, so I need to shuffle things around to make room
                  for the right value on the left.
               */
               ++pc_right;
               if (pc_right == u_left) {
                  qsort_swap(u_left, pc_left);
                  qsort_all_asserts(pc_left+1, pc_right, u_left+1, u_right);
               } else {
                  qsort_rotate(pc_right, pc_left, u_left);
                  qsort_all_asserts(pc_left+1, pc_right, u_left+1, u_right);
               }
               ++pc_left;
               ++u_left;
            } else {
               /* No more scanning required on either side of partition,
                  break out of loop and figure out next set of partitions
               */
               break;
            }
         }

         /* The elements in the pivot chunk are now in the right place. They
            will never move or be compared again. All I have to do is decide
            what to do with the stuff to the left and right of the pivot
            chunk.

            Notes on the QSORT_ORDER_GUESS ifdef code:

            1. If I just built these partitions without swapping any (or
               very many) elements, there is a chance that the elements are
               already ordered properly (being properly ordered will
               certainly result in no swapping, but the converse can't be
               proved :-).

            2. A (properly written) insertion sort will run faster on
               already ordered data than qsort will.

            3. Perhaps there is some way to make a good guess about
               switching to an insertion sort earlier than partition size 6
               (for instance - we could save the partition size on the stack
               and increase the size each time we find we didn't swap, thus
               switching to insertion sort earlier for partitions with a
               history of not swapping).

            4. Naturally, if I just switch right away, it will make
               artificial benchmarks with pure ascending (or descending)
               data look really good, but is that a good reason in general?
               Hard to say...
         */

#ifdef QSORT_ORDER_GUESS
         if (swapped < 3) {
#if QSORT_ORDER_GUESS == 1
            qsort_break_even = (part_right - part_left) + 1;
#endif
#if QSORT_ORDER_GUESS == 2
            qsort_break_even *= 2;
#endif
#if QSORT_ORDER_GUESS == 3
            int prev_break = qsort_break_even;
            qsort_break_even *= qsort_break_even;
            if (qsort_break_even < prev_break) {
               qsort_break_even = (part_right - part_left) + 1;
            }
#endif
         } else {
            qsort_break_even = QSORT_BREAK_EVEN;
         }
#endif

         if (part_left < pc_left) {
            /* There are elements on the left which need more processing.
               Check the right as well before deciding what to do.
            */
            if (pc_right < part_right) {
               /* We have two partitions to be sorted. Stack the biggest one
                  and process the smallest one on the next iteration. This
                  minimizes the stack height by insuring that any additional
                  stack entries must come from the smallest partition which
                  (because it is smallest) will have the fewest
                  opportunities to generate additional stack entries.
               */
               if ((part_right - pc_right) > (pc_left - part_left)) {
                  /* stack the right partition, process the left */
                  partition_stack[next_stack_entry].left = pc_right + 1;
                  partition_stack[next_stack_entry].right = part_right;
#ifdef QSORT_ORDER_GUESS
                  partition_stack[next_stack_entry].qsort_break_even = qsort_break_even;
#endif
                  part_right = pc_left - 1;
               } else {
                  /* stack the left partition, process the right */
                  partition_stack[next_stack_entry].left = part_left;
                  partition_stack[next_stack_entry].right = pc_left - 1;
#ifdef QSORT_ORDER_GUESS
                  partition_stack[next_stack_entry].qsort_break_even = qsort_break_even;
#endif
                  part_left = pc_right + 1;
               }
               qsort_assert(next_stack_entry < QSORT_MAX_STACK);
               ++next_stack_entry;
            } else {
               /* The elements on the left are the only remaining elements
                  that need sorting, arrange for them to be processed as the
                  next partition.
               */
               part_right = pc_left - 1;
            }
         } else if (pc_right < part_right) {
            /* There is only one chunk on the right to be sorted, make it
               the new partition and loop back around.
            */
            part_left = pc_right + 1;
         } else {
            /* This whole partition wound up in the pivot chunk, so
               we need to get a new partition off the stack.
            */
            if (next_stack_entry == 0) {
               /* the stack is empty - we are done */
               break;
            }
            --next_stack_entry;
            part_left = partition_stack[next_stack_entry].left;
            part_right = partition_stack[next_stack_entry].right;
#ifdef QSORT_ORDER_GUESS
            qsort_break_even = partition_stack[next_stack_entry].qsort_break_even;
#endif
         }
      } else {
         /* This partition is too small to fool with qsort complexity, just
            do an ordinary insertion sort to minimize overhead.
         */
         int i;
         /* Assume 1st element is in right place already, and start checking
            at 2nd element to see where it should be inserted.
         */
         for (i = part_left + 1; i <= part_right; ++i) {
            int j;
            /* Scan (backwards - just in case 'i' is already in right place)
               through the elements already sorted to see if the ith element
               belongs ahead of one of them.
            */
            for (j = i - 1; j >= part_left; --j) {
               if (qsort_cmp(i, j) >= 0) {
                  /* i belongs right after j
                  */
                  break;
               }
            }
            ++j;
            if (j != i) {
               /* Looks like we really need to move some things
               */
	       int k;
	       temp = array[i];
	       for (k = i - 1; k >= j; --k)
		  array[k + 1] = array[k];
               array[j] = temp;
            }
         }

         /* That partition is now sorted, grab the next one, or get out
            of the loop if there aren't any more.
         */

         if (next_stack_entry == 0) {
            /* the stack is empty - we are done */
            break;
         }
         --next_stack_entry;
         part_left = partition_stack[next_stack_entry].left;
         part_right = partition_stack[next_stack_entry].right;
#ifdef QSORT_ORDER_GUESS
         qsort_break_even = partition_stack[next_stack_entry].qsort_break_even;
#endif
      }
   }

   /* Believe it or not, the array is sorted at this point! */
}
