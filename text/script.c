/*+===================================================================
  File:      script.c

  Summary:   Implements script support for Macintosh fonts, scripts and international text.

  Classes:   None.

  Functions: C_Font2Script, C_FontScript, C_IntlScript, C_NMeasureJust, C_SetScript, GetAppFont, GetSysFont, ROMlib_script_globals, TruncString, TruncText

  Origin:    Existing repository source in executor/src/text; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1991 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ScriptMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "FontMgr.h"
#include "IntlUtil.h"
#include "ScriptMgr.h"
#include "MemoryMgr.h"
#include "ResourceMgr.h"
#include "ToolboxUtil.h"
#include "OSUtil.h"

#include "rsys/hook.h"
#include "rsys/quick.h"
#include "rsys/cquick.h"
#include "rsys/osutil.h"
#include "rsys/osevent.h"
#include "rsys/print.h"
#include "rsys/floatconv.h"
#include "rsys/string.h"
#include "rsys/mman.h"

#include <ctype.h>

/* Private Script Manager storage follows the System 7 ScriptPriv layout:
   SMgrRecord is 544 bytes, with 65 script entries at offset 114; ScriptRecord
   is 116 bytes. Offsets are ABI facts from the user's System 7 reference.
   The Roman record backs both the public selectors and guest globals. The
   reserved extension slots remain writable for Text Encoding Converter. */
#include "rsys/script_globals.h"

PUBLIC Ptr ROMlib_script_globals(void)
{
    unsigned char *globals, *roman;
    if (IntlSpec && (uint32)CL(IntlSpec) != UINT32_MAX)
        return (Ptr)SYN68K_TO_US((uint32)CL(IntlSpec));
    globals = (unsigned char*)NewPtrSysClear(544 + 116);
    if (!globals)
        return NULL;
    roman = globals + 544;
    *(uint16*)globals = CWC(0x0700);
    globals[4] = 1;
    *(uint16*)(globals + 52) = BootDrive;
    *(uint16*)(globals + 58) = CWC(0x1000);
    *(uint32*)(globals + 114) = CL(US_TO_SYN68K(roman));
    *(uint16*)roman = CWC(0x0700);
    roman[4] = 1;
    *(uint16*)(roman + 8) = CWC(0);           /* Chicago */
    *(uint16*)(roman + 10) = CWC(3);          /* Geneva */
    *(uint32*)(roman + 72) = CLC(0x00040009); /* Monaco 9 */
    *(uint32*)(roman + 76) = CLC(0x0003000c);
    *(uint32*)(roman + 80) = CLC(0x00030009);
    *(uint32*)(roman + 84) = CLC(0x0000000c);
    *(uint32*)(roman + 88) = CLC(0x0003000c);
    *(uint32*)(roman + 92) = CLC(0x00030009);
    roman[97] = 1; /* One Roman keyboard. */
    roman[98] = 0x7f;
    IntlSpec = CL(US_TO_SYN68K(globals));
    return (Ptr)globals;
}

PRIVATE int script_environment_field(INTEGER verb, unsigned* offset, unsigned* size)
{
    static const struct {
        uint16 offset;
        uint8 size;
    } fields[] = {{0, 2},  {2, 2},  {4, 1},  {5, 1},  {6, 1},  {7, 1}, {8, 1}, {9, 1},
                  {22, 4}, {38, 2}, {38, 2}, {42, 2}, {52, 2}, {0, 0}, {54, 4}};

    if (verb < 0 || (verb & 1) || verb / 2 >= NELEM(fields) || !fields[verb / 2].size)
        return 0;
    *offset = fields[verb / 2].offset;
    *size = fields[verb / 2].size;
    return 1;
}

PRIVATE LONGINT script_read_field(const unsigned char* p, unsigned size)
{
    if (size == 1)
        return *p;
    if (size == 2)
        return (int16)CW(*(const uint16*)p);
    return CL(*(const uint32*)p);
}

P1(PUBLIC pascal trap, LONGINT, GetEnvirons, INTEGER, verb)
{
    LONGINT retval;

    unsigned offset, size;
    Ptr globals = ROMlib_script_globals();
    if (verb == smKCHRCache)
        return (LONGINT)US_TO_SYN68K(ROMlib_kchr_ptr());
    if (globals && script_environment_field(verb, &offset, &size))
        return script_read_field((unsigned char*)globals + offset, size);
    retval = 0;
    return retval;
}

P2(PUBLIC pascal trap, OSErr, SetEnvirons, INTEGER, verb, LONGINT, param)
{
    unsigned offset, size;
    Ptr globals = ROMlib_script_globals();
    /* The supported script set and version are read-only. */
    if (!globals || !script_environment_field(verb, &offset, &size) || verb <= smBiDirect || verb == smSysScript ||
        verb == smAppScript || verb == smKeyScript)
        return smVerbNotFound;
    if (size == 1)
        globals[offset] = !!param;
    else if (size == 2)
        *(uint16*)(globals + offset) = CW(param);
    else
        *(uint32*)(globals + offset) = CL(param);
    *(uint16*)(globals + 2) = CW(CW(*(uint16*)(globals + 2)) + 1);
    return noErr;
}

static Byte* script_get_record(const Byte* globals, INTEGER script)
{
    enum { script_table = 114, script_count = 65, unicode_record = 62, record_size = 116 };

    uint32 address;
    if (script == 0x7e)
        memcpy(&address, globals + unicode_record, sizeof address);
    else if (script >= 0 && script < script_count)
        memcpy(&address, globals + script_table + script * sizeof address, sizeof address);
    else
        return NULL;
    address = CL(address);
    if (!address || (uint64)address + record_size > ROMlib_guest_memory_size)
        return NULL;
    Byte* record = (Byte*)SYN68K_TO_US(address);
    return record[4] ? record : NULL;
}

static INTEGER script_for_font(INTEGER font, bool international);

static Byte* script_resolve_record(const Byte* globals, INTEGER script)
{
    if (script == smSystemScript)
        script = script_read_field(globals + 38, 2);
    else if (script == -2 || script == -3)
        script = script_for_font(thePort ? PORT_TX_FONT(thePort) : systemFont, false);
    return script_get_record(globals, script);
}

static bool script_variable_field(INTEGER verb, unsigned* offset, unsigned* size)
{
    if (verb < 0 || (verb & 1))
        return false;
    if (verb <= 2) {
        *offset = verb;
        *size = 2;
    }
    else if (verb <= 10) {
        *offset = 4 + (verb - 4) / 2;
        *size = 1;
    }
    else if (verb <= 34) {
        *offset = verb - 4;
        *size = 2;
    }
    else if (verb <= 44) {
        *offset = 32 + (verb - 36) * 2;
        *size = 4;
    }
    else if (verb >= smScriptMonoFondSize && verb <= smScriptHelpFondSize) {
        *offset = 72 + (verb - smScriptMonoFondSize) * 2;
        *size = 4;
    }
    else if (verb == 90 || verb == 92) {
        *offset = 98 + (verb - 90) / 2;
        *size = 1;
    }
    else
        return false;
    return true;
}

P2(PUBLIC pascal trap, LONGINT, GetScript, INTEGER, script, INTEGER, verb)
{
    const Byte* globals = (const Byte*)ROMlib_script_globals();
    const Byte* record = globals ? script_resolve_record(globals, script) : NULL;
    unsigned offset, size;
    if (!record || !script_variable_field(verb, &offset, &size))
        return 0;
    return script_read_field(record + offset, size);
}

P3(PUBLIC pascal trap, OSErr, SetScript, INTEGER, script, INTEGER, verb, LONGINT, param)
{
    const Byte* globals = (const Byte*)ROMlib_script_globals();
    Byte* record = globals ? script_resolve_record(globals, script) : NULL;
    if (!record)
        return -2; /* smScriptNotFound */
    unsigned offset, size;
    if (!script_variable_field(verb, &offset, &size))
        return smVerbNotFound;
    if (size == 1)
        record[offset] = (Byte)param;
    else if (size == 2) {
        const uint16 value = CW(param);
        memcpy(record + offset, &value, sizeof value);
    }
    else {
        const uint32 value = CL(param);
        memcpy(record + offset, &value, sizeof value);
    }
    return noErr;
}

static INTEGER script_for_font(INTEGER font, bool international)
{
    Byte* globals = (Byte*)ROMlib_script_globals();
    if (!globals)
        return smRoman;

    enum { font_force = 6, intl_force = 7, forced = 8, defaulted = 9, system_script = 38 };

    const INTEGER system = script_read_field(globals + system_script, 2);
    if (font == systemFont)
        font = GetSysFont();
    else if (font == applFont)
        font = GetAppFont();

    const uint16 id = font;
    INTEGER script = id >= 0x4000 && id < 0xc000 ? 1 + (id - 0x4000) / 512 : smRoman;
    globals[forced] = globals[defaulted] = false;
    if (international ? globals[intl_force] && script != system : globals[font_force] && script == smRoman) {
        globals[forced] = true;
        script = system;
    }

    if (!script_get_record(globals, script)) {
        globals[defaulted] = true;
        script = system;
    }
    return script;
}

P1(PUBLIC pascal trap, INTEGER, Font2Script, INTEGER, fontnum)
{
    return script_for_font(fontnum, false);
}

/*
 * butchered Transliterate provided for Excel 3.0
 */

#define LOWERTOUPPEROFFSET 'A' - 'a'

A1(PRIVATE, char, upper, char, ch)
{
    if (ch >= 'a' && ch <= 'z')
#if 1
        return ch + LOWERTOUPPEROFFSET;
#else  /* 0 */
        return ch & ~0x20;
#endif /* 0 */
    else
        return ch;
}

#define UPPERTOLOWEROFFSET ('a' - 'A')

A1(PRIVATE, char, lower, char, ch)
{
    if (ch >= 'A' && ch <= 'Z')
#if 1
        return ch + UPPERTOLOWEROFFSET;
#else  /* 0 */
        return ch | 0x20;
#endif /* 0 */
    else
        return ch;
}

P4(PUBLIC pascal trap, INTEGER, Transliterate, Handle, srch, Handle, dsth, INTEGER, target, LONGINT, srcmask)
{
    char *sp, *dp, *ep;

    sp = (char*)STARH(srch);
    dp = (char*)STARH(dsth);
    ep = sp + GetHandleSize(srch);
    if (target & smTransLower) {
        if (target & smTransUpper)
            /*-->*/ return -1;
        while (sp < ep)
            *dp++ = lower(*sp++);
    }
    else if (target & smTransUpper) {
        while (sp < ep)
            *dp++ = upper(*sp++);
    }
    else {
        while (sp < ep)
            *dp++ = *sp++;
    }
    return 0;
}

/*
 * NOTE: These are all recent additions, made just before 1.2.2 was frozen.
 *	 They haven't been tested much, if at all.  In addition, much of
 *	 the code below just tries to return something "reasonable", although
 *	 not necessarily correct!
 */

P0(PUBLIC pascal trap, INTEGER, FontScript)
{
    return script_for_font(thePort ? PORT_TX_FONT(thePort) : systemFont, false);
}

P0(PUBLIC pascal trap, INTEGER, IntlScript)
{
    return script_for_font(thePort ? PORT_TX_FONT(thePort) : systemFont, true);
}

P1(PUBLIC pascal trap, void, KeyScript, INTEGER, scriptcode)
{
    warning_unimplemented(NULL_STRING);
}

P2(PUBLIC pascal trap, INTEGER, CharType, Ptr, textbufp, INTEGER, offset)
{
    INTEGER retval;
    unsigned char c;
    ROMlib_hook(script_notsupported);

    retval = 0;
    c = textbufp[offset];
    if (!isalpha(c)) {
        retval |= smCharPunct;
        if (isspace(c))
            retval |= smPunctBlank;
        else if (isdigit(c))
            retval |= smPunctNumber;
        else if (ispunct(c))
            retval |= smPunctSymbol;
    }
    else {
        retval |= smCharAscii;
        if (islower(c))
            retval |= smCharLower;
        else
            retval |= smCharUpper;
    }
    retval |= smCharLeft;
    retval |= smChar1byte;

    return retval;
}

P4(PUBLIC pascal trap, void, MeasureJust, Ptr, textbufp, int16, length, int16, slop, Ptr, charlocs)
{
    if (slop)
        warning_unimplemented("slop = %d", slop);
    MeasureText(length, textbufp, charlocs);
}

P1(PUBLIC pascal trap, Boolean, ParseTable, CharByteTable, table)
{
    memset(table, 0, sizeof(CharByteTable));
    return TRUE;
}

P2(PUBLIC pascal trap, Boolean, FillParseTable, CharByteTable, table, ScriptCode, script)
{
    /* ### should we even look at `script' */
    memset(table, 0, sizeof(CharByteTable));
    return TRUE;
}

P3(PUBLIC pascal trap, INTEGER, CharacterByteType, Ptr, textBuf, INTEGER, textOffset, ScriptCode, script)
{
    if (script != 0 && script != smSystemScript)
        warning_unimplemented("non-Roman character byte type");
    /* Single-byte character */
    return 0;
}

P3(PUBLIC pascal trap, INTEGER, CharacterType, Ptr, textbufp, INTEGER, offset, ScriptCode, script)
{
    warning_unimplemented(NULL_STRING);
    return CharType(textbufp, offset);
}

P5(PUBLIC pascal trap, INTEGER, TransliterateText, Handle, srch, Handle, dsth, INTEGER, target, LONGINT, srcmask,
   ScriptCode, script)
{
    warning_unimplemented(NULL_STRING);
    return Transliterate(srch, dsth, target, srcmask);
}

/* Validate complete spans for pointers into guest memory; native callers
   remain responsible for the buffers they supply. */
static boolean_t script_span(const void* pointer, size_t size)
{
    if (!pointer)
        return FALSE;
    uintptr_t address = (uintptr_t)pointer, base = (uintptr_t)ROMlib_offset;
    return address < base || address - base > ROMlib_guest_memory_size ||
           size <= ROMlib_guest_memory_size - (address - base);
}

P5(PUBLIC pascal trap, INTEGER, Pixel2Char, Ptr, textbufp, INTEGER, len, INTEGER, slop, INTEGER, pixwidth, BOOLEAN*,
   leftsidep)
{
    Point num, den;
    INTEGER retval;

    warning_unimplemented("poorly implemented");

    num.h = 1;
    num.v = 1;
    den.h = 1;
    den.v = 1;
    Fixed remaining;
    retval =
        C_PixelToChar(textbufp, len, (Fixed)slop * 65536, (Fixed)pixwidth * 65536, leftsidep, &remaining, 0, num, den);
    return retval;
}

P5(PUBLIC pascal trap, INTEGER, Char2Pixel, Ptr, textbufp, INTEGER, len, INTEGER, slop, INTEGER, offset, SignedByte,
   dir)
{
    INTEGER retval;
    Point num, den;

    warning_unimplemented("poorly implemented");

    num.h = 1;
    num.v = 1;
    den.h = 1;
    den.v = 1;

    retval = C_CharToPixel(textbufp, len, (Fixed)slop * 65536, offset, dir, 0, num, den);
    return retval;
}

P6(PUBLIC pascal trap, void, FindWord, Ptr, textbufp, INTEGER, length, INTEGER, offset, BOOLEAN, leftside, Ptr, breaks,
   INTEGER*, offsets)
{
    ROMlib_hook(script_notsupported);
    if (!offsets)
        return;
    if (!textbufp || length <= 0) {
        memset(offsets, 0, 6 * sizeof *offsets);
        return;
    }

    const unsigned char* text = (const unsigned char*)textbufp;
    const int position = MAX(0, MIN(length - 1, (int)offset - !leftside));
    const boolean_t space = isspace(text[position]) != 0;
    int start, stop;
    for (start = position; start > 0 && (isspace(text[start - 1]) != 0) == space; --start)
        ;
    for (stop = position + 1; stop < length && (isspace(text[stop]) != 0) == space; ++stop)
        ;

    offsets[0] = CW(start);
    offsets[1] = CW(stop);
    memset(offsets + 2, 0, 4 * sizeof *offsets);
    warning_unimplemented("poorly implemented");
}

P4(PUBLIC pascal trap, void, HiliteText, Ptr, textbufp, INTEGER, firstoffset, INTEGER, secondoffset, INTEGER*, offsets)
{
    ROMlib_hook(script_notsupported);
    offsets[0] = CW(firstoffset);
    offsets[1] = CW(secondoffset);
    offsets[2] = 0;
    offsets[3] = 0;
    offsets[4] = 0;
    offsets[5] = 0;
}

PRIVATE int16 count_spaces(Ptr textbufp, int16 length)
{
    int16 retval;

    retval = 0;
    while (length-- > 0)
        if (*textbufp++ == ' ')
            ++retval;

    return retval;
}

P3(PUBLIC pascal trap, void, DrawJust, Ptr, textbufp, int16, length, int16, slop)
{
    Fixed save_sp_extra_x;
    int n_spaces;

    save_sp_extra_x = PORT_SP_EXTRA_X(thePort);
    n_spaces = count_spaces(textbufp, length);
    if (n_spaces) {
        Fixed extra;

        extra = CL(save_sp_extra_x) + FixRatio(slop, n_spaces);
        PORT_SP_EXTRA_X(thePort) = CL(extra);
    }
    DrawText(textbufp, 0, length);
    PORT_SP_EXTRA_X(thePort) = save_sp_extra_x;
}

#include "rsys/date_parse.h"

_Static_assert(sizeof(romaw_date_cache) <= sizeof(DateCacheRec), "Opaque date cache fits the guest record");

static void cache_time_name(unsigned char result[5], const void* packed)
{
    const unsigned char* p = packed;
    unsigned start = 0, end = 4;
    while (start < end && date_space(p[start]))
        ++start;
    while (end > start && (!p[end - 1] || date_space(p[end - 1])))
        --end;
    result[0] = end - start;
    memcpy(result + 1, p + start, end - start);
}

static OSErr initialize_date_cache(DateCachePtr output)
{
    romaw_date_cache cache = {0};
    Intl0Rec numeric;
    Handle h;
    DateTimeRec today;
    LONGINT seconds;
    if (!output)
        return paramErr;
    memset(output, 0, sizeof *output);
    h = IUGetIntl(0);
    if (!h || !STARH(h) || GetHandleSize(h) < sizeof numeric)
        return resNotFound;
    memcpy(&numeric, STARH(h), sizeof numeric);
    cache.order = numeric.dateOrder;
    cache.date_sep = numeric.dateSep;
    cache.time_sep = numeric.timeSep;
    cache_time_name(cache.am, &numeric.mornStr);
    cache_time_name(cache.pm, &numeric.eveStr);
    h = IUGetIntl(1);
    if (!h || !STARH(h) || GetHandleSize(h) < sizeof(Intl1Rec))
        return resNotFound;
    {
        Intl1Ptr names = (Intl1Ptr)STARH(h);
        memcpy(cache.days, names->days, sizeof cache.days);
        memcpy(cache.months, names->months, sizeof cache.months);
        cache.abbreviation = names->abbrLen;
    }
    GetDateTime(&seconds);
    Secs2Date(CL(seconds), &today);
    memcpy(cache.year, &today.year, 2);
    memcpy(cache.magic, "EXD1", 4);
    memcpy(output, &cache, sizeof cache);
    return noErr;
}

static OSErr get_date_cache(DateCachePtr record, romaw_date_cache* cache)
{
    OSErr err;
    if (!record)
        return paramErr;
    if (memcmp(record, "EXD1", 4)) {
        err = initialize_date_cache(record);
        if (err != noErr)
            return err;
    }
    memcpy(cache, record, sizeof *cache);
    return noErr;
}

P5(PUBLIC pascal trap, String2DateStatus, String2Time, Ptr, textp, LONGINT, len, DateCachePtr, cachep, LONGINT*,
   lenusedp, LongDatePtr, datetimep)
{
    romaw_date_cache cache;
    romaw_date_input input;
    int hour, minute, second, status;
    size_t used = 0;
    if (lenusedp)
        *lenusedp = 0;
    if (!lenusedp || !datetimep || !textp || len <= 0)
        return (String2DateStatus)0x8400;
    if (get_date_cache(cachep, &cache) != noErr)
        return (String2DateStatus)0x8200;
    input = (romaw_date_input){(unsigned char*)textp, len, 0};
    status = romaw_parse_time(input, &cache, &hour, &minute, &second, &used);
    if (!(status & 0x8000)) {
        datetimep->hour = CW(hour);
        datetimep->minute = CW(minute);
        datetimep->second = CW(second);
        datetimep->pm = CW(hour >= 12);
        *lenusedp = CL(used);
    }
    return (String2DateStatus)status;
}

P5(PUBLIC pascal trap, String2DateStatus, String2Date, Ptr, text, int32, length, DateCachePtr, cachep, int32*,
   length_used_ret, LongDatePtr, date_time)
{
    romaw_date_cache cache;
    romaw_date_input input;
    int year, month, day, status;
    size_t used = 0;
    if (length_used_ret)
        *length_used_ret = 0;
    if (!length_used_ret || !date_time || !text || length <= 0)
        return (String2DateStatus)0x8400;
    if (get_date_cache(cachep, &cache) != noErr)
        return (String2DateStatus)0x8200;
    input = (romaw_date_input){(unsigned char*)text, length, 0};
    status = romaw_parse_date(input, &cache, &year, &month, &day, &used);
    if (!(status & 0x8000)) {
        static const int before[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
        int prior = year - 1;
        int leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        int ordinal = before[month - 1] + day + (month > 2 && leap);
        date_time->era = CWC(1);
        date_time->year = CW(year);
        date_time->month = CW(month);
        date_time->day = CW(day);
        date_time->dayOfYear = CW(ordinal);
        date_time->dayOfWeek = CW((prior * 365 + prior / 4 - prior / 100 + prior / 400 + ordinal) % 7 + 1);
        date_time->weekOfYear = CW((ordinal - 1) / 7 + 1);
        *length_used_ret = CL(used);
    }
    return (String2DateStatus)status;
}

P7(PUBLIC pascal trap, StyledLineBreakCode, StyledLineBreak, Ptr, textp, int32, length, int32, text_start, int32,
   text_end, int32, flags, Fixed*, text_width_fp, int32*, text_offset)
{
    char* text = (char*)textp;
    /* the index into `text' that began the last word, which is where we
     want to break if the current word extends past the end of the
     current line */
    int last_word_break = -1;
    char current_char;
    int current_index;
    int text_width;
    int width = 0;

    /* ### are we losing information here? */
    text_width = Fix2Long(CL(*text_width_fp));

    for (current_index = text_start; current_index < text_end; current_index++) {
        current_char = text[current_index];
        /* ### do we do this? */
        if (current_char == '\r') {
            *text_offset = CL(current_index + 1);
            return smBreakWord;
        }

        if (current_index > text_start && current_char != ' ' && text[current_index - 1] == ' ') {
            last_word_break = current_index - 1;
        }

        width += CharWidth(current_char);

        if (width > text_width) {
            /* we got our char */
            if (last_word_break == -1) {
                /* d'oh, we are on the first word */
                if (*text_offset) {
                    /* beginning of the line, break here */
                    *text_offset = CL(current_index - 1);
                    return smBreakChar;
                }
                *text_offset = CL(current_index - 1);
                return smBreakWord;
            }
            else {
                *text_offset = CL(last_word_break);
                return smBreakWord;
            }
        }
    }
    /* if we got here, that means the run did not extend past the end of
     the current line */
    *text_width_fp = CL(Long2Fix(text_width - width));
    *text_offset = CL(current_index);
    return smBreakOverflow;
}

P3(PUBLIC pascal trap, INTEGER, ReplaceText, Handle, base_text, Handle, subst_text, Str15, key)
{
    INTEGER retval;

    warning_unimplemented("not tested much");

    retval = 0;
    LOCK_HANDLE_EXCURSION_1(subst_text, {
        Ptr p;
        INTEGER len;
        LONGINT offset;
        LONGINT l;

        p = (Ptr)STARH(subst_text);
        len = GetHandleSize(subst_text);
        offset = 0;
        while (retval >= 0 && (l = Munger(base_text, offset, (Ptr)key + 1, key[0], NULL, 1)) >= 0) {
            offset = Munger(base_text, l, (Ptr)key + 1, key[0], p, len);
            if (offset < 0)
                retval = offset;
            else
                ++retval;
        }
    });

    return retval;
}

/* FormatStr2X is now StringToExtended */
P4(PUBLIC pascal trap, FormatStatus, StringToExtended, Str255, string, NumFormatStringRec*, formatp, NumberParts*,
   partsp, Extended80*, xp)
{
    FormatStatus retval;
    double d;
    char buf[256];

    memcpy(buf, string + 1, string[0]);
    buf[string[0]] = 0;
    sscanf(buf, "%lg", &d);
    ieee_to_x80((ieee_t)d, xp);
    warning_unimplemented(NULL_STRING);
    retval = noErr;
    return retval;
}

P4(PUBLIC pascal trap, FormatStatus, ExtendedToString, Extended80*, xp, NumFormatStringRec*, formatp, NumberParts*,
   partsp, Str255, string)
{
    ieee_t val;
    FormatStatus retval;
    char buf[256];

    val = x80_to_ieee(xp);
#if !defined(CYGWIN32)
    sprintf(buf, "%Lg", val);
#else
#warning may lose bits of precision here
    sprintf(buf, "%g", (double)val);
#endif
    str255_from_c_string(string, buf);
    warning_unimplemented(NULL_STRING);
    retval = noErr;
    return retval;
}

P3(PUBLIC pascal trap, FormatStatus, StringToFormatRec, Str255, in_string, NumberParts*, partsp, NumFormatStringRec*,
   out_string)
{
    FormatStatus retval;

    warning_unimplemented(NULL_STRING);
    retval = 0;
    return retval;
}

P5(PUBLIC pascal trap, ToggleResults, ToggleDate, LongDateTime*, lsecsp, LongDateField, field, DateDelta, delta,
   INTEGER, ch, TogglePB*, paramsp)
{
    ToggleResults retval;

    warning_unimplemented(NULL_STRING);
    retval = 0;
    return retval;
}

P3(PUBLIC pascal trap, INTEGER, TruncString, INTEGER, width, Str255, string, TruncCode, code)
{
    if (!script_span(string, 1) || !script_span(string, (size_t)string[0] + 1))
        return TruncErr;
    INTEGER length = CW((INTEGER)string[0]);
    INTEGER result = C_TruncText(width, (Ptr)string + 1, &length, code);
    string[0] = CW(length);
    return result;
}

P2(PUBLIC pascal trap, LONGINT, VisibleLength, Ptr, textp, LONGINT, len)
{
    if (len <= 0 || !script_span(textp, (size_t)len))
        return 0;
    /* Roman text excludes trailing space bytes without modifying the run. */
    while (len > 0 && textp[len - 1] == ' ')
        --len;
    return len;
}

P2(PUBLIC pascal trap, void, LongDate2Secs, LongDateRec*, ldatep, ULONGINT*, secs_outp)
{
    long long secs;
    LONGINT high, low;
    INTEGER hour;

    hour = CW(ldatep->hour);
    if (ldatep->pm && hour < 12)
        hour += 12;

    secs = ROMlib_long_long_secs(CW(ldatep->year), CW(ldatep->month), CW(ldatep->day), hour, CW(ldatep->minute),
                                 CW(ldatep->second));
    high = secs >> 32;
    low = secs;
    secs_outp[0] = CL(high);
    secs_outp[1] = CL(low);
}

P2(PUBLIC pascal trap, void, LongSecs2Date, ULONGINT*, secs_inp, LongDateRec*, ldatep)

{
    long long secs;
    INTEGER pm;

    secs = ((long long)CL(secs_inp[0]) << 32) | CL(secs_inp[1]);
    date_to_swapped_fields(secs, &ldatep->year, &ldatep->month, &ldatep->day, &ldatep->hour, &ldatep->minute,
                           &ldatep->second, &ldatep->dayOfWeek, &ldatep->dayOfYear, &ldatep->weekOfYear);

    pm = (CW(ldatep->hour) > 12) ? 1 : 0;
    ldatep->pm = CW(pm);
}

/*
 * NOTE: At least some of these need to be implemented if we want
 *	 Resolve to work
 */

#if 0
ParseTable

PortionText

FindScriptRun

IsSpecialFont

RawPrinterValues

NPixel2Char

NChar2Pixel

NDrawJust

NPortionText

ReplaceText

TruncText

TrunString

NFindWord

ValidDate

FormatStr2X

FormatX2Str

Format2Str

Str2Format

ToggleDate

LongSecs2Date

LongDate2Secs

IntlTokenize

GetFormatOrder
#endif

A0(PUBLIC, INTEGER, GetSysFont)
{
    return CW(SysFontFam);
}

A0(PUBLIC, INTEGER, GetAppFont)
{
    return CW(ApFontID);
}

P1(PUBLIC pascal trap, OSErr, InitDateCache, DateCachePtr, cache)
{
    return initialize_date_cache(cache);
}

P2(PUBLIC pascal trap, INTEGER, CharByte, Ptr, textBuf, INTEGER, textOffset)
{
    return C_CharacterByteType(textBuf, textOffset, C_FontScript());
}

P5(PUBLIC pascal trap, Fixed, PortionLine, Ptr, textPtr, LONGINT, textLen, JustStyleCode, styleRunPosition, Point,
   numer, Point, denom)
{
    /* Inside Macintosh: Text, QuickDraw Text: Roman justification distributes
       extra width over spaces, including leading/trailing spaces. Style-run
       position affects intercharacter scripts, not Roman text. */
    if (!textPtr || textLen <= 0 || !thePort || numer.h <= 0 || denom.h <= 0)
        return 0;
    uintptr_t address = (uintptr_t)textPtr;
    uintptr_t base = (uintptr_t)ROMlib_offset;
    if (address >= base && address - base <= ROMlib_guest_memory_size &&
        (uint32)textLen > ROMlib_guest_memory_size - (address - base))
        return 0;
    uint32 spaces = 0;
    const unsigned char* text = (const unsigned char*)textPtr;
    for (LONGINT i = 0; i < textLen; ++i)
        spaces += text[i] == ' ';
    if (!spaces)
        return 0;
    int width = CharWidth(' ');
    if (width <= 0)
        return 0;
    uint64 weight = (uint64)spaces * (unsigned)width * (unsigned)numer.h;
    uint64 limit = (uint64)INT32_MAX * (unsigned)denom.h;
    if (weight > limit / 65536)
        return INT32_MAX;
    return (Fixed)(weight * 65536 / (unsigned)denom.h);
}

P6(PUBLIC pascal trap, void, DrawJustified, Ptr, textPtr, LONGINT, textLength, Fixed, slop, JustStyleCode,
   styleRunPosition, Point, numer, Point, denom)
{
    if (!thePort || textLength <= 0 || textLength > INT16_MAX || !script_span(textPtr, (size_t)textLength) ||
        numer.h <= 0 || numer.v <= 0 || denom.h <= 0 || denom.v <= 0)
        return;
    /* Roman justification expands space advances. spExtra is in unscaled
       coordinates; preserve the caller's existing extra spacing on return. */
    GrafPtr port = thePort;
    Fixed saved_extra = PORT_SP_EXTRA_X(port);
    unsigned spaces = 0;
    for (LONGINT i = 0; i < textLength; ++i)
        spaces += (unsigned char)textPtr[i] == ' ';
    if (spaces && slop) {
        int64 extra = (int64)PORT_SP_EXTRA(port) + (int64)slop * denom.h / ((int64)numer.h * spaces);
        PORT_SP_EXTRA_X(port) = CL((Fixed)MAX(INT32_MIN, MIN(extra, INT32_MAX)));
    }
    Point swapped_numer = {CW(numer.v), CW(numer.h)};
    Point swapped_denom = {CW(denom.v), CW(denom.h)};
    text_helper(textLength, textPtr, &swapped_numer, &swapped_denom, NULL, NULL, text_helper_draw);
    PORT_SP_EXTRA_X(port) = saved_extra;
}

P3(PUBLIC pascal trap, ScriptRunStatus, FindScriptRun, Ptr, textPtr, LONGINT, textLen, LONGINT*, lenUsedp)
{
    /* The Roman script consumes the entire single-byte run. Negative lengths
     describe an empty run, as in the System 7 Script Manager. */
    if (lenUsedp)
        *lenUsedp = CL(textLen > 0 ? textLen : 0);
    return 0;
}

static int64* roman_caret_positions(Ptr text, LONGINT length, Fixed slop, Point numer, Point denom)
{
    if (length < 0 || length > INT16_MAX || (length && (!thePort || !script_span(text, length))) || numer.h <= 0 ||
        numer.v <= 0 || denom.h <= 0 || denom.v <= 0)
        return NULL;
    int64* positions = calloc((size_t)length + 1, sizeof *positions);
    if (!positions || !length)
        return positions;
    /* The legacy text renderer accumulates into a 16.16 position. Estimate
       a conservative extent before asking it to measure a complete run. */
    int advances[256];
    memset(advances, -1, sizeof advances);
    int64 extent = 0;
    for (LONGINT i = 0; i < length; ++i) {
        unsigned c = (unsigned char)text[i];
        if (advances[c] < 0)
            advances[c] = MAX(0, CharWidth(c));
        extent += ((int64)advances[c] * numer.h + denom.h - 1) / denom.h + 1;
    }
    if (extent >= INT16_MAX) {
        unsigned spaces = 0, used = 0;
        int64 advance = 0;
        for (LONGINT i = 0; i < length; ++i)
            spaces += (unsigned char)text[i] == ' ';
        for (LONGINT i = 0; i < length; ++i) {
            unsigned c = (unsigned char)text[i];
            advance += (int64)advances[c] * numer.h * 65536 / denom.h;
            used += c == ' ';
            int64 extra = spaces ? (int64)slop * used / spaces : 0;
            positions[i + 1] = MAX(positions[i], advance + extra);
        }
        return positions;
    }
    INTEGER* widths = calloc((size_t)length + 1, sizeof *widths);
    if (!widths) {
        free(positions);
        return NULL;
    }
    Point num = {CW(numer.v), CW(numer.h)}, den = {CW(denom.v), CW(denom.h)};
    LONGINT total = text_helper(length, text, &num, &den, NULL, widths, text_helper_measure);
    if (total < 0) {
        free(widths);
        free(positions);
        return NULL;
    }
    unsigned spaces = 0, used = 0;
    for (LONGINT i = 0; i < length; ++i)
        spaces += (unsigned char)text[i] == ' ';
    for (LONGINT i = 1; i <= length; ++i) {
        used += (unsigned char)text[i - 1] == ' ';
        int64 extra = spaces ? (int64)slop * used / spaces : 0;
        int64 position = (int64)(uint16)CW(widths[i]) * 65536 + extra;
        positions[i] = MAX(positions[i - 1], position);
    }
    free(widths);
    return positions;
}

P7(PUBLIC pascal trap, void, NMeasureJust, Ptr, text, int32, length, Fixed, slop, Ptr, charLocs, JustStyleCode, run_pos,
   Point, numer, Point, denom)
{
    if (length < 0 || length > INT16_MAX || !script_span(charLocs, ((size_t)length + 1) * sizeof(INTEGER)))
        return;
    int64* positions = roman_caret_positions(text, length, slop, numer, denom);
    if (!positions)
        return;
    for (LONGINT i = 0; i <= length; ++i) {
        const uint16 position = CW(MIN((positions[i] + 65535) / 65536, INT16_MAX));
        memcpy(charLocs + i * sizeof position, &position, sizeof position);
    }
    free(positions);
}

P4(PUBLIC pascal trap, INTEGER, TruncText, INTEGER, width, Ptr, text, INTEGER*, lengthp, TruncCode, code)
{
    if (!script_span(lengthp, sizeof *lengthp))
        return TruncErr;
    int length = CW(*lengthp);
    if (length < 0 || (length && !script_span(text, length)))
        return TruncErr;
    if (!length && width >= 0)
        return NotTruncated;
    Point scale = {1, 1};
    int64* positions = roman_caret_positions(text, length, 0, scale, scale);
    if (!positions)
        return TruncErr;
    int64 limit = (int64)width * 65536;
    if (positions[length] <= limit) {
        free(positions);
        return NotTruncated;
    }
    /* ROMAW's Roman script uses the MacRoman ellipsis. As in the System 7
       truncation worker, reserve its bytes first and shorten the longer half
       (the tail on ties) for middle truncation. Other scripts are unsupported. */
    int64 marker_width = (int64)CharWidth((CHAR)0xc9) * 65536;
    if (!length || marker_width < 0 || marker_width > limit) {
        *lengthp = 0;
        free(positions);
        return TruncErr;
    }
    limit -= marker_width;
    int head = length - 1, tail = 0;
    if (code) {
        head /= 2;
        tail = length - 1 - head;
    }
    while (positions[head] + positions[length] - positions[length - tail] > limit) {
        if (head > tail)
            --head;
        else
            --tail;
    }
    if (tail)
        memmove(text + head + 1, text + length - tail, tail);
    text[head] = (char)0xc9;
    *lengthp = CW(head + 1 + tail);
    free(positions);
    return Truncated;
}

P9(PUBLIC pascal trap, INTEGER, PixelToChar, Ptr, textBuf, LONGINT, textLen, Fixed, slop, Fixed, pixelWidth, BOOLEAN*,
   leadingEdgep, Fixed*, widthRemainingp, JustStyleCode, styleRunPosition, Point, numer, Point, denom)
{
    if (!script_span(leadingEdgep, sizeof *leadingEdgep) || !script_span(widthRemainingp, sizeof *widthRemainingp))
        return 0;
    *leadingEdgep = FALSE;
    *widthRemainingp = CL(pixelWidth);
    int64* positions = roman_caret_positions(textBuf, textLen, slop, numer, denom);
    if (!positions)
        return 0;
    INTEGER result = 0;
    if (pixelWidth < 0)
        *widthRemainingp = CLC(-1);
    else if ((int64)pixelWidth >= positions[textLen]) {
        result = textLen;
        *leadingEdgep = TRUE;
        *widthRemainingp = CL((Fixed)((int64)pixelWidth - positions[textLen]));
    }
    else {
        *widthRemainingp = CLC(-1);
        for (LONGINT i = 0; i < textLen; ++i)
            if ((int64)pixelWidth < positions[i + 1]) {
                if ((int64)pixelWidth - positions[i] < positions[i + 1] - pixelWidth) {
                    result = i;
                    *leadingEdgep = TRUE;
                }
                else
                    result = i + 1;
                break;
            }
    }
    free(positions);
    return result;
}

P8(PUBLIC pascal trap, INTEGER, CharToPixel, Ptr, textBuf, LONGINT, textLen, Fixed, slop, LONGINT, offset, INTEGER,
   direction, JustStyleCode, styleRunPosition, Point, numer, Point, denom)
{
    int64* positions = roman_caret_positions(textBuf, textLen, slop, numer, denom);
    if (!positions)
        return 0;
    LONGINT index = MAX(0, MIN(offset, textLen));
    int64 width = positions[index] / 65536;
    free(positions);
    return MIN(width, INT16_MAX);
}

P3(PUBLIC pascal trap, void, LowercaseText, Ptr, textp, INTEGER, len, ScriptCode, script)
{
    static const unsigned char roman_pairs[][2] = {
        {0x80, 0x8a}, {0x81, 0x8c}, {0x82, 0x8d}, {0x83, 0x8e}, {0x84, 0x96}, {0x85, 0x9a}, {0x86, 0x9f}, {0xae, 0xbe},
        {0xaf, 0xbf}, {0xcb, 0x88}, {0xcc, 0x8b}, {0xcd, 0x9b}, {0xce, 0xcf}, {0xd9, 0xd8}, {0xe5, 0x89}, {0xe6, 0x90},
        {0xe7, 0x87}, {0xe8, 0x91}, {0xe9, 0x8f}, {0xea, 0x92}, {0xeb, 0x94}, {0xec, 0x95}, {0xed, 0x93}, {0xee, 0x97},
        {0xef, 0x99}, {0xf1, 0x98}, {0xf2, 0x9c}, {0xf3, 0x9e}, {0xf4, 0x9d}};
    unsigned char* p = (unsigned char*)textp;
    unsigned i;
    if (script != 0 && script != smSystemScript) {
        warning_unimplemented("non-Roman lowercase conversion");
        return;
    }
    if (!p)
        return;
    while (len-- > 0) {
        if (*p >= 'A' && *p <= 'Z')
            *p += 'a' - 'A';
        else
            for (i = 0; i < sizeof roman_pairs / sizeof roman_pairs[0]; ++i)
                if (*p == roman_pairs[i][0]) {
                    *p = roman_pairs[i][1];
                    break;
                }
        ++p;
    }
}

P3(PUBLIC pascal trap, void, UppercaseText, Ptr, textp, INTEGER, len, ScriptCode, script)
{
    if (script == smRoman || script == smSystemScript)
        ROMlib_UprString((StringPtr)textp, TRUE, len);
}

P3(PUBLIC pascal trap, void, StripDiacritics, Ptr, textp, INTEGER, len, ScriptCode, script)
{
    if (script == smRoman || script == smSystemScript)
        ROMlib_strip_diacritics((Byte*)textp, len);
}

P3(PUBLIC pascal trap, void, UppercaseStripDiacritics, Ptr, textp, INTEGER, len, ScriptCode, script)
{
    if (script == smRoman || script == smSystemScript)
        ROMlib_UprString((StringPtr)textp, FALSE, len);
}
