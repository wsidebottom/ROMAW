/*+===================================================================
  File:      iu.c

  Summary:   Implements iu support for Macintosh fonts, scripts and international text.

  Classes:   None.

  Functions: IUCompString, IUEqualString, ROMlib_type_select_clear, ROMlib_type_select_compare, ROMlib_type_select_find,
             ROMlib_type_select_key

  Origin:    Existing repository source in executor/src/text; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1989, 1990, 1994 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in IntlUtil.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "IntlUtil.h"
#include "OSUtil.h"
#include "ResourceMgr.h"
#include "MemoryMgr.h"
#include "ScriptMgr.h"
#include "BinaryDecimal.h"
#include "rsys/glue.h"
#include "rsys/resource.h"
#include "rsys/hook.h"
#include "rsys/typeselect.h"
#include "rsys/pstuff.h"
#include "rsys/intl_map.h"
#include <ctype.h>

/*
 * outl adds the characters in the four bytes of a longint to the place
 * pointed to by the pointer pointed to by opp, the pointer pointed to
 * by opp is incremented for each character that is added.
 */

A2(PRIVATE, void, outl, LONGINT, l, char**, opp)
{
    if (l & 0xFF000000) {
        *(*opp)++ = (l & 0xFF000000) >> 24;
        if (l & 0xFF0000) {
            *(*opp)++ = (l & 0xFF0000) >> 16;
            if (l & 0xFF00) {
                *(*opp)++ = (l & 0xFF00) >> 8;
                if (l & 0xFF)
                    *(*opp)++ = (l & 0xFF);
            }
        }
    }
}

/*
 * outs is similar to outl, except it works with a string and has an additional
 * paramater that is used to specify that an exact number of characters are
 * transfered (used when doing abbreviations).
 */

/*
 * n is the number of characters to transfer, or 0 for entire string
 */

A3(PRIVATE, void, outs, StringPtr, p, INTEGER, n, char**, opp)
{
    int ntocopy;

    ntocopy = n ? n : p[0]; /* This can lead to copying NUL characters */
    /* which is prescribed in IMI-505 (ick) */
    BlockMove((Ptr)(p + 1), (Ptr)*opp, (Size)ntocopy);
    *opp += ntocopy;
}

/*
 * outn is similar to outl and outs but it converts a number to a set
 * of characters and adds them to *opp.  If leading0 is TRUE and the number
 * is less than 10, a leading zero is output.
 */

A3(PRIVATE, void, outn, INTEGER, n, BOOLEAN, leading0, char**, opp)
{
    Str255 s;

    NumToString((LONGINT)n, s);
    if (leading0 && n < 10)
        *(*opp)++ = '0';
    BlockMove((Ptr)(s + 1), (Ptr)*opp, (Size)s[0]);
    *opp += s[0];
}

/*
 * month is similar to the outx routines but is used to print the numerical
 * form of the month and, if non-null, a separator.
 */

A4(PRIVATE, void, month, INTEGER, n, Intl0Ptr, int0p, char, sep, char**, opp)
{
    outn(n, Cx(int0p->shrtDateFmt) & mntLdingZ, opp);
    if (sep)
        *(*opp)++ = sep;
}

/*
 * day is similar to month
 */

A4(PRIVATE, void, day, INTEGER, n, Intl0Ptr, int0p, char, sep, char**, opp)
{
    outn(n, Cx(int0p->shrtDateFmt) & dayLdingZ, opp);
    if (sep)
        *(*opp)++ = sep;
}

/*
 * year is similar to month and day
 */

A4(PRIVATE, void, year, INTEGER, n, Intl0Ptr, int0p, char, sep, char**, opp)
{
    outn(Cx(int0p->shrtDateFmt) & century ? n : n % 100, FALSE, opp);
    if (sep)
        *(*opp)++ = sep;
}

PRIVATE void date_record_string(const DateTimeRec* record, DateForm form, StringPtr p, Handle h)
{
    Intl1Ptr int1p;
    Intl0Ptr int0p;
    char* op;
    DateTimeRec dtr = *record;
    int abbrev;

    if (!h)
        h = IUGetIntl(form == shortDate ? 0 : 1);

    op = (char*)p + 1;
    if (form == shortDate) {
        if (h && (int0p = (Intl0Ptr)STARH(h))) {
            switch (Cx(int0p->dateOrder)) {
            case mdy:
                month(CW(dtr.month), int0p, Cx(int0p->dateSep), &op);
                day(CW(dtr.day), int0p, Cx(int0p->dateSep), &op);
                year(CW(dtr.year), int0p, 0, &op);
                break;
            case dmy:
                day(CW(dtr.day), int0p, Cx(int0p->dateSep), &op);
                month(CW(dtr.month), int0p, Cx(int0p->dateSep), &op);
                year(CW(dtr.year), int0p, 0, &op);
                break;
            case ymd:
                year(CW(dtr.year), int0p, Cx(int0p->dateSep), &op);
                month(CW(dtr.month), int0p, Cx(int0p->dateSep), &op);
                day(CW(dtr.day), int0p, 0, &op);
                break;
            }
        }
    }
    else {
        if (h && (int1p = (Intl1Ptr)STARH(h))) {
            abbrev = form == longDate ? 0 : Cx(int1p->abbrLen);
            outl(Cx(int1p->st0), &op);
            if (!Cx(int1p->suppressDay)) {
                outs(int1p->days[CW(dtr.dayOfWeek) - 1], abbrev, &op);
                outl(Cx(int1p->st1), &op);
            }
            if (Cx(int1p->lngDateFmt)) {
                outs(int1p->months[CW(dtr.month) - 1], abbrev, &op);
                outl(Cx(int1p->st2), &op);
                outn(CW(dtr.day), Cx(int1p->dayLeading0), &op);
            }
            else {
                outn(CW(dtr.day), Cx(int1p->dayLeading0), &op);
                outl(Cx(int1p->st2), &op);
                outs(int1p->months[CW(dtr.month) - 1], abbrev, &op);
            }
            outl(Cx(int1p->st3), &op);
            outn(CW(dtr.year), FALSE, &op);
            outl(Cx(int1p->st4), &op);
        }
    }
    p[0] = op - (char*)p - 1;
}

P4(PUBLIC pascal trap, void, IUDatePString, LONGINT, date, /* IMI-505 */
   DateForm, form, StringPtr, p, Handle, h)
{
    DateTimeRec record;
    Secs2Date(date, &record);
    date_record_string(&record, form, p, h);
}

P1(PUBLIC pascal trap, Handle, IUGetIntl, INTEGER, id) /* IMI-505 */
{
    INTEGER oldres;
    Handle retval;

    oldres = CurResFile();
    UseResFile(0);
    retval = ROMlib_getrestid(TICK("INTL"), id);
    UseResFile(oldres);

    if (!retval && id == 4) {
        static Handle h = 0;

        warning_unimplemented(NULL_STRING);
        if (!h)
            h = NewHandleClear(100);
        retval = h;
    }

    /* force dates to include century, otherwise we can run into y2k
       problems with programs like Quicken 6.0 */

    if (id == 0 && retval) {
        Intl0Ptr int0p;

        int0p = (Intl0Ptr)STARH(retval);
        int0p->shrtDateFmt |= CBC(century);
    }

    return retval;
}

P3(PUBLIC pascal trap, void, IUDateString, LONGINT, date, /* IMI-504 */
   DateForm, form, StringPtr, p)
{
    IUDatePString(date, form, p, IUGetIntl(form == shortDate ? 0 : 1));
}

PRIVATE void time_record_string(const DateTimeRec* record, BOOLEAN secs, StringPtr p, Handle h)
{
    Intl0Ptr int0p;
    char* op;
    DateTimeRec dtr = *record;
    char *ip, *ep;

    if (!h)
        h = IUGetIntl(0);

    op = (char*)p + 1;
    if (h && (int0p = (Intl0Ptr)STARH(h))) {
        if (int0p->timeCycle)
            outn((CW(dtr.hour) % 12) == 0 ? 12 : CW(dtr.hour) % 12, Cx(int0p->timeFmt) & hrLeadingZ, &op);
        else
            outn(CW(dtr.hour), Cx(int0p->timeFmt) & hrLeadingZ, &op);
        *op++ = int0p->timeSep;
        outn(CW(dtr.minute), Cx(int0p->timeFmt) & minLeadingZ, &op);
        if (secs) {
            *op++ = int0p->timeSep;
            outn(CW(dtr.second), Cx(int0p->timeFmt) & secLeadingZ, &op);
        }
        /* IMI-499 is misleading about the timenSuff fields.  The first four are
   used for AM, the second four for PM.  Yes, that's dumb.  But that's how
   the Mac works.  Sigh. */
        if (!int0p->timeCycle && CW(dtr.hour) < 12)
            for (ip = (char*)&int0p->time1Suff, ep = ip + 4; ip != ep && *ip;)
                *op++ = *ip++;
        else if (!int0p->timeCycle) /* dtr.hour >= 12 */
            for (ip = (char*)&int0p->time5Suff, ep = ip + 4; ip != ep && *ip;)
                *op++ = *ip++;
        else if (CW(dtr.hour) < 12)
            outl(Cx(int0p->mornStr), &op);
        else
            outl(Cx(int0p->eveStr), &op);
    }
    p[0] = op - (char*)p - 1;
}

P4(PUBLIC pascal trap, void, IUTimePString, LONGINT, date, /* IMI-505 */
   BOOLEAN, secs, StringPtr, p, Handle, h)
{
    DateTimeRec record;
    Secs2Date(date, &record);
    time_record_string(&record, secs, p, h);
}

P3(PUBLIC pascal trap, void, IUTimeString, LONGINT, date, /* IMI-505 */
   BOOLEAN, secs, StringPtr, p)
{
    IUTimePString(date, secs, p, IUGetIntl(0));
}

P0(PUBLIC pascal trap, BOOLEAN, IUMetric) /* IMI-505 */
{
    Handle h;

    h = IUGetIntl(0);
    return h ? ((Intl0Ptr)STARH(h))->metricSys : FALSE;
}

P3(PUBLIC pascal trap, void, IUSetIntl, INTEGER, rn, /* IMI-506 */
   INTEGER, id, Handle, newh)
{
    INTEGER oldcurmap;
    Handle h, copy = newh;
    OSErr error;

    oldcurmap = Cx(CurMap);
    if (!rn)
        rn = CW(SysMap);
    if (!newh) {
        ROMlib_setreserr(paramErr);
        return;
    }
    /* The caller may pass an INTL handle owned by the System map. A document
     needs its own bytes, not a second map referencing that same handle. */
    error = HandToHand(&copy);
    if (error != noErr) {
        ROMlib_setreserr(error);
        return;
    }
    UseResFile(rn);
    if (ResErr == noErr) {
        h = Get1Resource(TICK("INTL"), id);
        if (h) {
            /* Look only in the destination map, and replace the full payload,
             including any extended international-record fields. */
            HLock(copy);
            error = PtrToXHand(STARH(copy), h, GetHandleSize(copy));
            HUnlock(copy);
            ROMlib_setreserr(error);
        }
        else if (ResErr == noErr || ResErr == CW(resNotFound)) {
            AddResource(copy, TICK("INTL"), id, (StringPtr)0);
            h = copy;
            if (ResErr == noErr)
                copy = NULL; /* The destination resource map now owns it. */
        }
        if (h && ResErr == noErr) {
            SetResAttrs(h, resPurgeable);
            ChangedResource(h);
            UpdateResFile(rn);
            HPurge(h);
        }
    }
    error = CW(ResErr);
    if (copy)
        DisposHandle(copy);
    UseResFile(oldcurmap);
    ResErr = CW(error);
}

typedef struct {
    unsigned char primary;
    unsigned char secondary;
} sorttype;

PRIVATE sorttype highsortvalues[] = {
    {'A', 0x02},  {'A', 0x04},  {'C', 0x01},  {'E', 0x01},  {'N', 0x01},  {'O', 0x01},  {'U', 0x01},  {'A', 0x81},
    {'A', 0x82},  {'A', 0x83},  {'A', 0x84},  {'A', 0x85},  {'A', 0x86},  {'C', 0x81},  {'E', 0x81},  {'E', 0x82},
    {'E', 0x83},  {'E', 0x84},  {'I', 0x81},  {'I', 0x82},  {'I', 0x83},  {'I', 0x84},  {'N', 0x81},  {'O', 0x81},
    {'O', 0x82},  {'O', 0x83},  {'O', 0x84},  {'O', 0x85},  {'U', 0x81},  {'U', 0x82},  {'U', 0x83},  {'U', 0x84},
    {0xA0, 0x00}, {0xA1, 0x00}, {0xA2, 0x00}, {0xA3, 0x00}, {0xA4, 0x00}, {0xA5, 0x00}, {0xA6, 0x00}, {0xA7, 0x00},
    {0xA8, 0x00}, {0xA9, 0x00}, {0xAA, 0x00}, {0xAB, 0x00}, {0xAC, 0x00}, {0xAD, 0x00}, {0xAE, 0x00}, {'O', 0x03},
    {0xB0, 0x00}, {0xB1, 0x00}, {0xB2, 0x00}, {0xB3, 0x00}, {0xB4, 0x00}, {0xB5, 0x00}, {0xB6, 0x00}, {0xB7, 0x00},
    {0xB8, 0x00}, {0xB9, 0x00}, {0xBA, 0x00}, {0xBB, 0x00}, {0xBC, 0x00}, {0xBD, 0x00}, {0xAE, 0x01}, {'O', 0x86},
    {0xC0, 0x00}, {0xC1, 0x00}, {0xC2, 0x00}, {0xC3, 0x00}, {0xC4, 0x00}, {0xC5, 0x00}, {0xC6, 0x00}, {0x22, 0x01},
    {0x22, 0x02}, {0xC9, 0x00}, {0x20, 0x01}, {'A', 0x01},  {'A', 0x03},  {'O', 0x02},  {0xAE, 0x02}, {0xAE, 0x03},
    {0xD0, 0x00}, {0xD1, 0x00}, {0x22, 0x03}, {0x22, 0x04}, {0x27, 0x01}, {0x27, 0x02}, {0xD6, 0x00}, {0xD7, 0x00},
    {'Y', 0x00},
};

A3(PRIVATE, INTEGER, defaultorder, unsigned char*, cp, INTEGER, len, sorttype*, rp) /* i.e. U.S. order */
{
    int index;
    int c;

    c = *cp;
    if (c & 0x80) {
        index = (c & 0x7F);
        if (index < (int)NELEM(highsortvalues))
            *rp = highsortvalues[index];
        else {
            rp->primary = c;
            rp->secondary = 0;
        }
    }
    else {
        if (islower(c)) {
            rp->primary = toupper(c);
            rp->secondary = 0x80;
        }
        else {
            rp->primary = c;
            rp->secondary = 0;
        }
    }
    return 1;
}

A3(PRIVATE, INTEGER, germanylocalization, unsigned char*, cp, INTEGER, len, sorttype*, rp)
{
    INTEGER retval;

    (void)defaultorder(cp, len, rp);
    retval = 1;
    switch (*cp) {
    case 'A':
        if (len > 1 && cp[1] == 'E') { /* A E */
            retval = 2;
            rp->secondary = 0x02;
        }
        break;
    case 0x80: /* A-umlaut */
        rp->secondary = 0x03;
        break;
    case 0xAE: /* AE */
        rp->secondary = 0x04;
        break;
    case 0xCB: /* A~ */
        rp->secondary = 0x05;
        break;
    case 0x81: /* A-jot */
        rp->secondary = 0x06;
        break;

    case 'a':
        if (len > 1 && cp[1] == 'e') { /* a e */
            retval = 2;
            rp->secondary = 0x84;
        }
        break;
    case 0x8A: /* a-umlaut */
        rp->secondary = 0x85;
        break;
    case 0xBE: /* ae */
        rp->secondary = 0x86;
        break;
    case 0x8B: /* a~ */
        rp->secondary = 0x87;
        break;
    case 0x8C: /* a-jot */
        rp->secondary = 0x88;
        break;

    case 'O':
        if (len > 1 && cp[1] == 'E') { /* O E */
            retval = 2;
            rp->secondary = 0x01;
        }
        break;
    case 0x85: /* O-umlaut */
        rp->secondary = 0x02;
        break;
    case 0xCE: /* OE */
        rp->secondary = 0x03;
        break;
    case 0xCD: /* O~ */
        rp->secondary = 0x04;
        break;
    case 0xAF: /* O-slash */
        rp->secondary = 0x05;
        break;

    case 'o':
        if (len > 1 && cp[1] == 'e') { /* o e */
            retval = 2;
            rp->secondary = 0x84;
        }
        break;
    case 0x9A: /* o-umlaut */
        rp->secondary = 0x85;
        break;
    case 0xCF: /* oe */
        rp->secondary = 0x86;
        break;
    case 0x9B: /* o~ */
        rp->secondary = 0x87;
        break;
    case 0xBF: /* o-slash */
        rp->secondary = 0x88;
        break;

    case 'U':
        if (len > 1 && cp[1] == 'E') { /* U E */
            retval = 2;
            rp->secondary = 0x01;
        }
        break;
    case 0x86: /* U-umlaut */
        rp->secondary = 0x02;
        break;

    case 'u':
        if (len > 1 && cp[1] == 'e') { /* u e */
            retval = 2;
            rp->secondary = 0x84;
        }
        break;
    case 0x9F: /* u-umlaut */
        rp->secondary = 0x85;
        break;

    case 's':
        if (len > 1 && cp[1] == 's') { /* s s */
            retval = 2;
            rp->primary = 0xA7;
            rp->secondary = 0x00;
        }
        break;
    case 0xA7: /* Beta? */
        rp->secondary = 0x01;
        break;
    }

    return retval;
}

A3(PRIVATE, INTEGER, britainlocalization, unsigned char*, cp, INTEGER, len, sorttype*, rp)
{
    (void)defaultorder(cp, len, rp);
    if (rp->primary >= 0x23 && rp->primary <= 0xA3) { /* shift around so */
        if (rp->primary == 0xA3)                      /* real pound sign comes before sharp */
            rp->primary = 0x23;
        else
            rp->primary++;
    }
    return 1;
}

A3(PRIVATE, INTEGER, defaultlocalization, unsigned char*, cp, INTEGER, len, sorttype*, rp)
{
    return 0; /* never consume anything */
}

/*
 * NOTE: currently we can only use natively compiled localization routines
 *	 because we don't envoke syn68k below.  However, since we don't know
 *	 the exact format of localization routines, we don't care, for now.
 */

#if !defined(__STDC__)
typedef INTEGER (*locptype)();
#else  /* __STDC__ */
typedef INTEGER (*locptype)(Ptr ptr, INTEGER len, sorttype* sp);
#endif /* __STDC__ */

A5(PRIVATE, INTEGER, iuhelper, Ptr, ptr1, Ptr, ptr2, INTEGER, len1, INTEGER, len2, BOOLEAN, ignoresec)
{
    Intl0Hndl h;
    locptype locp;
    sorttype sort1, sort2;
    INTEGER ret1, ret2;
    int usesec = 0;

#if !defined(LETGCCWAIL)
    locp = (locptype)defaultlocalization;
#endif /* LETGCCWAIL */

    if ((h = (Intl0Hndl)IUGetIntl(0)) && HIDDEN_RAW((*h))) {
        switch ((Hx(h, intl0Vers) >> 8) & 0xFF) {
        case verBritain:
            locp = (locptype)britainlocalization;
            break;
        case verGermany:
            locp = (locptype)germanylocalization;
            break;
        default:
            locp = (locptype)defaultlocalization;
            break;
        }
    }
    /* Missing locale resources still have the built-in Roman ordering. */
    if (len1 < 0 || !ptr1)
        len1 = 0;
    if (len2 < 0 || !ptr2)
        len2 = 0;
    while (len1 > 0 && len2 > 0) {
        if (!(ret1 = (*locp)(ptr1, len1, &sort1)))
            ret1 = defaultorder((unsigned char*)ptr1, len1, &sort1);
        if (!(ret2 = (*locp)(ptr2, len2, &sort2)))
            ret2 = defaultorder((unsigned char*)ptr2, len2, &sort2);
        if (sort1.primary == sort2.primary) {
            if (!ignoresec && !usesec && sort1.secondary != sort2.secondary) {
                if (sort1.secondary < sort2.secondary)
                    usesec = -1;
                else
                    usesec = 1;
            }
            len1 -= ret1;
            ptr1 += ret1;
            len2 -= ret2;
            ptr2 += ret2;
        }
        else if (sort1.primary < sort2.primary) {
            len1 = 0;
            len2 = 1;
        }
        else {
            len1 = 1;
            len2 = 0;
        }
    }
    if (len1 == len2)
        return ignoresec ? 0 : usesec;
    else
        return (len1 < len2 ? -1 : 1);
}

P4(PUBLIC pascal trap, INTEGER, IUMagString, Ptr, ptr1, /* IMI-506 */
   Ptr, ptr2, INTEGER, len1, INTEGER, len2)
{
    return iuhelper(ptr1, ptr2, len1, len2, FALSE);
}

A2(PUBLIC, INTEGER, IUCompString, StringPtr, str1, StringPtr, str2) /* IMI-506 */
{
    return IUMagString((Ptr)(str1 + 1), (Ptr)(str2 + 1), str1[0], str2[0]);
}

P4(PUBLIC pascal trap, INTEGER, IUMagIDString, Ptr, ptr1, /* IMI-507 */
   Ptr, ptr2, INTEGER, len1, INTEGER, len2)
{
    return iuhelper(ptr1, ptr2, len1, len2, TRUE);
}

A2(PUBLIC, INTEGER, IUEqualString, StringPtr, str1, /* IMI-506 */
   StringPtr, str2)
{
    return IUMagIDString((Ptr)(str1 + 1), (Ptr)(str2 + 1), str1[0], str2[0]);
}

P4(PUBLIC pascal trap, /* INTEGER */ void, IUMystery, Ptr, arg1, Ptr, arg2, INTEGER, arg3, INTEGER, arg4)
{
    /* Microsoft Excel calls Pack6 with a selector of 18.
       This is being done on the Mac+.
       Neither, Inside Macintosh, nor any of the .h and .a files help.
       This is here just to pop the arguments in a desperate hope that
       nothing important is being done anyway.
       */
}

/* Decode the big-endian signed seconds since 1904 without narrowing to the
   32-bit File Manager date range. Gregorian 400-year cycles allow dates
   before the epoch as well as after the unsigned 32-bit rollover. */
PRIVATE BOOLEAN long_date_record(const LongDateTime* value, DateTimeRec* record)
{
    long long seconds, days, day_seconds, era, year;
    unsigned day_of_era, year_of_era, day_of_year, month_index, month, day;
    uint64 bits;
    int weekday;
    if (!value)
        return FALSE;
    bits = ((uint64)CL(value->hilo.hi) << 32) | (uint32)CL(value->hilo.lo);
    memcpy(&seconds, &bits, sizeof seconds);
    days = seconds / 86400;
    day_seconds = seconds % 86400;
    if (day_seconds < 0) {
        day_seconds += 86400;
        --days;
    }
    weekday = (days + 5) % 7;
    if (weekday < 0)
        weekday += 7;
    /* Shift to a March-based Gregorian era; 1904-01-01 is day 695361. */
    days += 695361;
    era = (days >= 0 ? days : days - 146096) / 146097;
    day_of_era = days - era * 146097;
    year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    year = year_of_era + era * 400;
    day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    month_index = (5 * day_of_year + 2) / 153;
    day = day_of_year - (153 * month_index + 2) / 5 + 1;
    month = month_index < 10 ? month_index + 3 : month_index - 9;
    year += month <= 2;
    if (year < 1 || year > 32767)
        return FALSE;
    record->year = CW(year);
    record->month = CW(month);
    record->day = CW(day);
    record->hour = CW(day_seconds / 3600);
    record->minute = CW(day_seconds / 60 % 60);
    record->second = CW(day_seconds % 60);
    record->dayOfWeek = CW(weekday + 1);
    return TRUE;
}

P4(PUBLIC pascal trap, void, IULDateString, LongDateTime*, datetimep, DateForm, longflag, Str255, result, Handle,
   intlhand)
{
    DateTimeRec record;
    result[0] = 0;
    if (long_date_record(datetimep, &record))
        date_record_string(&record, longflag, result, intlhand);
}

P4(PUBLIC pascal trap, void, IULTimeString, LongDateTime*, datetimep, BOOLEAN, wantseconds, Str255, result, Handle,
   intlhand)
{
    DateTimeRec record;
    result[0] = 0;
    if (long_date_record(datetimep, &record))
        time_record_string(&record, wantseconds, result, intlhand);
}

#include "itl2_sort.h"

/* There is no native international-resource cache to invalidate. */
P0(PUBLIC pascal trap, void, IUClearCache)
{
}

P5(PUBLIC pascal trap, INTEGER, IUMagPString, Ptr, ptra, Ptr, ptrb, INTEGER, lena, INTEGER, lenb, Handle, itl2hand)
{
    INTEGER result;
    if (itl2hand && itl2_custom_compare(ptra, ptrb, lena, lenb, itl2hand, FALSE, &result))
        return result;
    return iuhelper(ptra, ptrb, lena, lenb, FALSE);
}

P5(PUBLIC pascal trap, INTEGER, IUMagIDPString, Ptr, ptra, Ptr, ptrb, INTEGER, lena, INTEGER, lenb, Handle, itl2hand)
{
    INTEGER result;
    if (itl2hand && itl2_custom_compare(ptra, ptrb, lena, lenb, itl2hand, TRUE, &result))
        return result;
    return iuhelper(ptra, ptrb, lena, lenb, TRUE) != 0;
}

static INTEGER international_order(int first, int second, int first_script, int second_script, BOOLEAN languages)
{
    romaw_intl_table scripts, language_table;
    Handle h;
    Size size;
    INTEGER saved_error = ResError();
    int current = languages ? GetScript(0, 28) : 0;
    int result;
    h = GetResource(TICK("itlm"), 0);
    if (h && STARH(h) && (size = GetHandleSize(h)) >= 30 &&
        intl_map_table((unsigned char*)STARH(h), size, 0, &scripts) &&
        (!languages || intl_map_table((unsigned char*)STARH(h), size, 1, &language_table))) {
        if (languages)
            result =
                intl_language_order(first, second, first_script, second_script, current, &scripts, &language_table);
        else {
            unsigned a = intl_script_position(first, &scripts);
            unsigned b = intl_script_position(second, &scripts);
            result = a < b ? -1 : a > b;
        }
    }
    else {
        /* The built-in Roman environment works without itlm. Unknown selectors
         sort last instead of accidentally preceding the system script. */
        if (!languages) {
            if (first == -1 || first == -2)
                first = 0;
            if (second == -1 || second == -2)
                second = 0;
            if (first < 0 || first > 64)
                first = 32767;
            if (second < 0 || second > 64)
                second = 32767;
        }
        else {
            if (first >= -7 && first <= -2 &&
                (first >= -5 || first_script == 0 || first_script == -1 || first_script == -2))
                first = current;
            if (second >= -7 && second <= -2 &&
                (second >= -5 || second_script == 0 || second_script == -1 || second_script == -2))
                second = current;
            if (first < 0)
                first = 32767;
            if (second < 0)
                second = 32767;
        }
        result = first < second ? -1 : first > second;
    }
    ROMlib_setreserr(saved_error);
    return result;
}

P2(PUBLIC pascal trap, INTEGER, IUScriptOrder, ScriptCode, script1, ScriptCode, script2)
{
    return international_order(script1, script2, 0, 0, FALSE);
}

P2(PUBLIC pascal trap, INTEGER, IULangOrder, LangCode, l1, LangCode, l2)
{
    return international_order(l1, l2, 32767, 32767, TRUE);
}

P8(PUBLIC pascal trap, INTEGER, IUTextOrder, Ptr, ptra, Ptr, ptrb, INTEGER, lena, INTEGER, lenb, ScriptCode, scripta,
   ScriptCode, bscript, LangCode, langa, LangCode, langb)
{
    INTEGER order = IUScriptOrder(scripta, bscript);
    if (!order)
        order = international_order(langa, langb, scripta, bscript, TRUE);
    if (!order) {
        INTEGER saved_error = ResError();
        Handle sorting = NULL;
        if (scripta >= -2 && scripta <= 0)
            sorting = GetResource(TICK("itl2"), GetScript(0, smScriptSort));
        order = IUMagPString(ptra, ptrb, lena, lenb, sorting);
        ROMlib_setreserr(saved_error);
    }
    return order;
}

P5(PUBLIC pascal trap, void, IUGetItlTable, ScriptCode, script, INTEGER, tablecode, HIDDEN_Handle*, itlhandlep,
   LONGINT*, offsetp, LONGINT*, lengthp)
{
    static const unsigned offsets[] = {22, 24, 36, 32, 40};
    static const unsigned lengths[] = {54, 56, 60, 58, 62};
    Handle h;
    Size size;
    unsigned offset, length;
    unsigned char* p;
    if (itlhandlep)
        HIDDEN_RAW_SET(*itlhandlep, NULL);
    if (offsetp)
        *offsetp = 0;
    if (lengthp)
        *lengthp = 0;
    if (!itlhandlep || !offsetp || !lengthp || script < -2 || script > 0 || tablecode < 0 || tablecode > 4)
        return;
    h = GetResource(tablecode < 2 ? TICK("itl2") : TICK("itl4"), GetScript(0, tablecode < 2 ? smScriptSort : 24));
    if (!h || !STARH(h))
        return;
    size = GetHandleSize(h);
    if (size < (Size)(lengths[tablecode] + 2))
        return;
    p = (unsigned char*)STARH(h);
    offset = (unsigned)p[offsets[tablecode]] * 256 + p[offsets[tablecode] + 1];
    if (tablecode >= 2)
        offset = offset * 65536U + (unsigned)p[offsets[tablecode] + 2] * 256 + p[offsets[tablecode] + 3];
    length = (unsigned)p[lengths[tablecode]] * 256 + p[lengths[tablecode] + 1];
    if (offset > (unsigned)size || length > (unsigned)size - offset)
        return;
    HIDDEN_RAW_SET(*itlhandlep, RM(h));
    *offsetp = CL(offset);
    *lengthp = CL(length);
}

/* Type-selection uses the existing Roman collation, like the Script Manager. */
void ROMlib_type_select_clear(ROMlib_type_select_record* record)
{
    if (record)
        memset(record, 0, sizeof *record);
}

BOOLEAN
ROMlib_type_select_key(EventRecord* event, ROMlib_type_select_record* record)
{
    if (!event || !record || (CW(event->what) != keyDown && CW(event->what) != autoKey) ||
        (CW(event->modifiers) & cmdKey))
        return FALSE;
    unsigned ch = CL(event->message) & 255;
    if (ch != 8 && (ch < 32 || ch == 127))
        return FALSE;
    uint32 when = CL(event->when);
    if (record->keys[0] > 63 || CW(record->script) != 0 || when - CL(record->last_key_time) > 60)
        record->keys[0] = 0;
    record->script = 0;
    record->last_key_time = CL(when);
    if (ch == 8) {
        if (record->keys[0])
            --record->keys[0];
    }
    else if (record->keys[0] < 63)
        record->keys[++record->keys[0]] = ch;
    return record->keys[0] != 0;
}

INTEGER
ROMlib_type_select_compare(const ROMlib_type_select_record* record, INTEGER script, StringPtr string)
{
    Str63 prefix;
    if (!record || record->keys[0] > 63 || !string)
        return 1;
    if (script > 0 || CW(record->script) > 0)
        return script > CW(record->script) ? -1 : 1;
    prefix[0] = MIN(string[0], record->keys[0]);
    memcpy(prefix + 1, string + 1, prefix[0]);
    return RelString((StringPtr)record->keys, prefix, FALSE, TRUE);
}

INTEGER
ROMlib_type_select_find(const ROMlib_type_select_record* record, INTEGER count, INTEGER mode, ProcPtr callback,
                        void* data)
{
    INTEGER match = 0;
    Str255 best = {0};
    if (!record || !record->keys[0] || record->keys[0] > 63 || !callback || count <= 0 || mode < -1 || mode > 1)
        return 0;
    for (int item = 1; item <= count; ++item) {
        INTEGER script = 0;
        HIDDEN_Ptr string = {0};
        if (!CToPascalCall(callback, 1 | (2 << 3) | (5 << 6) | (5 << 9) | (5 << 12), item, &script, &string, data) ||
            !HIDDEN_RAW(string))
            continue;
        StringPtr candidate = (StringPtr)MR(HIDDEN_RAW(string));
        INTEGER order = ROMlib_type_select_compare(record, CW(script), candidate);
        if (!mode && !order)
            return item;
        if ((mode < 0 && order > 0) || (mode >= 0 && order < 0)) {
            INTEGER best_order = match ? RelString(candidate, best, FALSE, TRUE) : 0;
            if (!match || (mode < 0 ? best_order > 0 : best_order < 0)) {
                match = item;
                memcpy(best, candidate, candidate[0] + 1);
            }
        }
    }
    return match;
}
