/*+===================================================================
  File:      emustubs.c

  Summary:   Implements emustubs support for Macintosh startup, traps and runtime services.

  Classes:   None.

  Functions: ROMlib_GetTrapAddress_helper, ROMlib_osdispatch_available, ROMlib_reset_bad_trap_addresses,
             ROMlib_tsm_dispatch_available, STUB

  Origin:    Existing repository source in executor/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1994, 1995, 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "DialogMgr.h"
#include "rsys/osutil.h"
#include "rsys/component.h"
#include "rsys/systemnotify.h"
#include "rsys/systemwindow.h"
#include "rsys/layers.h"
#include "rsys/typeselect.h"
#include "rsys/process.h"
#include "rsys/ecf.h"
#include "DeviceMgr.h"
#include "rsys/device.h"

#include <stdarg.h>

#include "ResourceMgr.h"
#include "ScriptMgr.h"
#include "TextEdit.h"
#include "ToolboxUtil.h"
#include "ListMgr.h"
#include "SANE.h"
#include "BinaryDecimal.h"
#include "FileMgr.h"
#include "StdFilePkg.h"
#include "DiskInit.h"
#include "PrintMgr.h"
#include "IntlUtil.h"
#include "MemoryMgr.h"
#include "SysErr.h"
#include "CQuickDraw.h"
#include "SoundMgr.h"
#include "HelpMgr.h"
#include "ADB.h"

#include "SegmentLdr.h"
#include "ToolboxUtil.h"
#include "OSUtil.h"
#include "VRetraceMgr.h"
#include "TimeMgr.h"
#include "ToolboxEvent.h"
#include "OSEvent.h"
#include "Gestalt.h"
#include "rsys/segment.h"
#include "rsys/resource.h"
#include "rsys/toolutil.h"
#include "NotifyMgr.h"
#include "ShutDown.h"
#include "AppleEvents.h"
#include "ProcessMgr.h"
#include "AliasMgr.h"
#include "rsys/alias.h"
#include "EditionMgr.h"
#include "FontMgr.h"
#include "Finder.h"
#include "Iconutil.h"
#include "QuickTime.h"
#include "CommTool.h"

#include "rsys/pstuff.h"
#include "rsys/cquick.h"
#include "rsys/quick.h"
#include "rsys/ctl.h"
#include "rsys/file.h"
#include "rsys/afp_catalog.h"
#include "rsys/mactcp.h"
#include "rsys/time.h"
#include "rsys/mman.h"
#include "rsys/prefs.h"
#include "rsys/soundopts.h"
#include "rsys/fileplay.h"
#include "rsys/system_error.h"
#include "rsys/emustubs.h"
#include "rsys/gestalt.h"
#include "rsys/toolevent.h"
#include "rsys/romaw.h"
#include "rsys/mixed_mode.h"
#include "rsys/cfm.h"
#include "rsys/speech.h"
#include "rsys/mixed_mode.h"
#include "rsys/menu.h"

#define PUBLIC
#undef PRIVATE
#define PRIVATE static

#define RTS() return POPADDR()

#define STUB(x) PUBLIC syn68k_addr_t _##x(syn68k_addr_t ignoreme, void** ignoreme2)

#define ADJUST_CC_BASED_ON_D0()                         \
    do {                                                \
        cpu_state.ccc = 0;                              \
        cpu_state.ccn = (cpu_state.regs[0].sw.n < 0);   \
        cpu_state.ccv = 0;                              \
        cpu_state.ccx = cpu_state.ccx; /* unchanged */  \
        cpu_state.ccnz = (cpu_state.regs[0].sw.n != 0); \
    } while (0)

#define STUB_NEG1_D0(x)          \
    STUB(x)                      \
    {                            \
        EM_D0 = -1;              \
        ADJUST_CC_BASED_ON_D0(); \
        RTS();                   \
    }

#define STUB_RTS(x) \
    STUB(x)         \
    {               \
        RTS();      \
    }

STUB(SysError)
{
    syn68k_addr_t retaddr = POPADDR();
    M68kReg saved_regs[16];
    int16 errorcode = EM_D0;
    memcpy(saved_regs, cpu_state.regs, sizeof saved_regs);
    C_SysError(errorcode);
    memcpy(cpu_state.regs, saved_regs, sizeof saved_regs);
    /* A failed segment load cannot resume at the missing code. */
    if (errorcode == 15 && !ResumeProc)
        C_ExitToShell();
    return retaddr;
}

STUB(GetDefaultStartup)
{
    (SYN68K_TO_US((signed char*)EM_A0))[0] = -1;
    (SYN68K_TO_US((signed char*)EM_A0))[1] = -1;
    (SYN68K_TO_US((signed char*)EM_A0))[2] = -1;
    (SYN68K_TO_US((signed char*)EM_A0))[3] = -33; /* That's what Q610 has */
    RTS();
}

STUB(SetDefaultStartup)
{
    RTS();
}

STUB(GetVideoDefault)
{
    (SYN68K_TO_US((signed char*)EM_A0))[0] = 0; /* Q610 values */
    (SYN68K_TO_US((signed char*)EM_A0))[1] = -56;
    RTS();
}

STUB(SetVideoDefault)
{
    RTS();
}

STUB(GetOSDefault)
{
    (SYN68K_TO_US((signed char*)EM_A0))[0] = 0; /* Q610 values */
    (SYN68K_TO_US((signed char*)EM_A0))[1] = 1;
    RTS();
}

STUB(SetOSDefault)
{
    RTS();
}

STUB(SwapMMUMode)
{
    Byte mode = EM_D0;
    SwapMMUMode(&mode);
    EM_D0 = (EM_D0 & 0xFFFFFF00) | mode;
    RTS();
}

STUB(Launch)
{
    LaunchParamBlockRec* lpbp;
    StringPtr strp;

    lpbp = (LaunchParamBlockRec*)SYN68K_TO_US(EM_A0);
    if (lpbp->launchBlockID == CWC(extendedBlock))
        strp = 0;
    else
        strp = MR(*(StringPtr*)lpbp);
    EM_D0 = NewLaunch(strp, 0, lpbp);
    RTS();
}

STUB(Chain)
{
    Chain(MR(*(StringPtr*)SYN68K_TO_US(EM_A0)), 0);
    RTS();
}

STUB(IMVI_LowerText)
{
    EM_D0 = resNotFound;
    RTS();
}

STUB(SCSIDispatch)
{
    syn68k_addr_t retaddr;

    retaddr = POPADDR();
    EM_A7 += 4; /* get rid of selector and location for errorvalue */
#define scMgrBusyErr 7
    PUSHUW(scMgrBusyErr);
    PUSHADDR(retaddr);
    RTS();
}

STUB(LoadSeg)
{
    syn68k_addr_t retaddr;

    retaddr = POPADDR();

    C_LoadSeg(POPUW());
    PUSHADDR(retaddr - 6);
    RTS();
}

STUB(zerod0SetCtlValue)
{
    syn68k_addr_t retaddr;
    short arg0;

    retaddr = POPADDR();
    arg0 = POPUW();
    C_SetCtlValue((ControlHandle)SYN68K_TO_US((void*)POPUL()), arg0);
    EM_D0 = 0;
    ADJUST_CC_BASED_ON_D0();
    PUSHADDR(retaddr);
    RTS();
}

STUB(HWPriv)
{
    HWPriv(EM_D0, EM_A0); /* NOTE: A0 shouldn't be convered from SYN to US
			       here because it's often used as a long.  See
			       the implementation of HWPriv in segment.c 
			       for details */
    RTS();
}

STUB(ResourceStub)
{
    EM_A0 = (long)US_TO_SYN68K_CHECK0(
        ROMlib_mgetres2((resmaphand)SYN68K_TO_US_CHECK0(EM_A4), (resref*)SYN68K_TO_US_CHECK0(EM_A2)));
    RTS();
}

STUB(DrvrInstall)
{
    EM_D0 = ROMlib_driver_install((INTEGER)EM_D0);
    ADJUST_CC_BASED_ON_D0();
    RTS();
}

STUB(DrvrRemove)
{
    EM_D0 = ROMlib_driver_remove((INTEGER)EM_D0);
    ADJUST_CC_BASED_ON_D0();
    RTS();
}

STUB(ADBReInit)
{
    ADBReInit();
    RTS();
}

STUB(ADBOp)
{
    adbop_t* p;

    p = (adbop_t*)SYN68K_TO_US(EM_A0);

    cpu_state.regs[0].sw.n = ADBOp(PPR(p->data), PPR(p->proc), PPR(p->buffer), EM_D0);
    RTS();
}

STUB(CountADBs)
{
    cpu_state.regs[0].sw.n = CountADBs();
    RTS();
}

STUB(GetIndADB)
{
    cpu_state.regs[0].sw.n = GetIndADB((ADBDataBlock*)SYN68K_TO_US_CHECK0(EM_A0), (char)EM_D0);
    RTS();
}

STUB(GetADBInfo)
{
    cpu_state.regs[0].sw.n = GetADBInfo((ADBDataBlock*)SYN68K_TO_US_CHECK0(EM_A0), (char)EM_D0);
    RTS();
}

STUB(SetADBInfo)
{
    cpu_state.regs[0].sw.n = SetADBInfo((ADBSetInfoBlock*)SYN68K_TO_US_CHECK0(EM_A0), (char)EM_D0);
    RTS();
}

STUB(_GetResource)
{
    syn68k_addr_t retaddr;
    short arg0;
    void* retval;

    retaddr = POPADDR();
    arg0 = POPUW();
    retval = C_GetResource(POPUL(), arg0);
    WRITEUL(EM_A7, (uint32)US_TO_SYN68K_CHECK0(retval));
    PUSHADDR(retaddr);
    EM_D0 = 0;
    RTS();
}

static void do_selector_error(uint32 selector, char* trap_name, syn68k_addr_t (*trap_fp)(syn68k_addr_t, void**))
{
    boolean_t found_trapno_p = FALSE;
    int trapno = /* dummy */ -1, i;
    char buf[256];

    /* search for the trap fp in the trap tables */

    for (i = 0; i < 0x400; i++)
        if (toolstuff[i].ptoc.wheretogo == trap_fp) {
            trapno = i + 0xA800;
            found_trapno_p = TRUE;
            break;
        }
    if (!found_trapno_p)
        for (i = 0; i < 0x100; i++)
            if (osstuff[i].func == trap_fp) {
                trapno = i + 0xA000;
                found_trapno_p = TRUE;
                break;
            }

    if (found_trapno_p)
        sprintf(buf,
                "Fatal error.\r"
                "unknown trap selector `%ld' in trap `%s' trapno `%X'.",
                (long)selector, trap_name, trapno);
    else
        sprintf(buf,
                "Fatal error.\r"
                "unknown trap selector `%ld' in trap `%s'.",
                (long)selector, trap_name);

    system_error(buf, 0, "Restart", NULL, NULL, NULL, NULL, NULL);

    ExitToShell();
}

typedef struct {
    unsigned long first;
    unsigned long last;
    unsigned long divide;
    ptocblock_t* descriptorp;
} selectorblock_t;

#define do_selector_block(sbp, sel, trap) ({ _do_selector_block(sbp, sel, #trap, _##trap); })

PRIVATE syn68k_addr_t _do_selector_block(const selectorblock_t* sbp, unsigned long sel, char* trap_name,
                                         syn68k_addr_t (*trap_fp)(syn68k_addr_t, void**))
{
    char done;
    ptocblock_t* ptocp;
    const selectorblock_t* orig_sbp;

    orig_sbp = sbp;
    for (done = FALSE; (!(sbp->first == 0 && sbp->last == 0) && !done); ++sbp) {
        if (sel >= sbp->first && sel <= sbp->last) {
            ptocp = &sbp->descriptorp[(sel - sbp->first) / sbp->divide];
            if (ptocp->wheretogo)
                return PascalToCCall(0, ptocp);
            done = TRUE;
        }
    }

    do_selector_error(sel, trap_name, trap_fp);

    /* quiet gcc */
    abort();
}

typedef struct selector_table_entry {
    uint32 selector;
    ptocblock_t ptoc;
} selector_table_entry_t;

#define do_selector_table(selector, table, table_size, fail_fn, trap) \
    ({ _do_selector_table(selector, table, table_size, fail_fn, #trap, _##trap); })

static syn68k_addr_t _do_selector_table(uint32 selector, selector_table_entry_t* table, int table_size,
                                        syn68k_addr_t (*fail_fn)(void), char* trap_name,
                                        syn68k_addr_t (*trap_fp)(syn68k_addr_t, void**))
{
    int i;

    for (i = 0; i < table_size; i++) {
        if (table[i].selector == selector)
            return PascalToCCall(0, &table[i].ptoc);
    }

    if (fail_fn)
        return (*fail_fn)();
    else
        do_selector_error(selector, trap_name, trap_fp);

    /* quiet gcc */
    abort();
}

#define PTOCBLOCK(name) {C_##name, PTOC_##name}

#define C_ZERO ((void*)0)
#define PTOC_ZERO (0)

PRIVATE ptocblock_t scriptptoc0[] = {
    PTOCBLOCK(FontScript),    /* 0 FontScript */
    PTOCBLOCK(IntlScript),    /* 2 IntlScript */
    PTOCBLOCK(KeyScript),     /* 4 KybdScript */
    PTOCBLOCK(Font2Script),   /* 6 Font2Script */
    PTOCBLOCK(GetEnvirons),   /* 8 */
    PTOCBLOCK(SetEnvirons),   /* 10 */
    PTOCBLOCK(GetScript),     /* 12 */
    PTOCBLOCK(SetScript),     /* 14 */
    PTOCBLOCK(CharByte),      /* 16 CharByte */
    PTOCBLOCK(CharType),      /* 18 CharType */
    PTOCBLOCK(Pixel2Char),    /* 20 Pixel2Char */
    PTOCBLOCK(Char2Pixel),    /* 22 Char2Pixel */
    PTOCBLOCK(Transliterate), /* 24 Translit*/
    PTOCBLOCK(FindWord),      /* 26 FindWord */
    PTOCBLOCK(HiliteText),    /* 28 HiliteText */
    PTOCBLOCK(DrawJust),      /* 30 DrawJust */
    PTOCBLOCK(MeasureJust),   /* 0x20 */

    PTOCBLOCK(ParseTable),    /* 0x22 */
    PTOCBLOCK(ZERO),          /* PortionText 0x24 */
    PTOCBLOCK(FindScriptRun), /* 0x26 */
    PTOCBLOCK(VisibleLength), /* 0x28 */
    PTOCBLOCK(ZERO),          /* IsSpecialFont 0x2A */
    PTOCBLOCK(ZERO),          /* RawPrinterValues 0x2C */
    PTOCBLOCK(PixelToChar),   /* 0x2E */
    PTOCBLOCK(CharToPixel),   /* 0x30 */
    PTOCBLOCK(DrawJustified), /* 0x32 */
    PTOCBLOCK(NMeasureJust),  /* 0x34 */
    PTOCBLOCK(PortionLine),   /* 0x36 */
};

/* D0 IsCmdChar */

PRIVATE ptocblock_t scriptptoc1[] = {
    PTOCBLOCK(ReplaceText), /* 0xDC */

#if defined(NEWSTUBS)
    PTOCBLOCK(TruncText),   /* 0xde */
    PTOCBLOCK(TruncString), /* 0xE0 */
    PTOCBLOCK(NFindWord),   /* 0xe2 */
    PTOCBLOCK(ValidDate),   /* 0xe4 */
    PTOCBLOCK(FormatStr2X), /* 0xe6 */
    PTOCBLOCK(FormatX2Str), /* 0xe8 */
    PTOCBLOCK(Format2Str),  /* 0xea */
    PTOCBLOCK(Str2Format),  /* 0xec */
    PTOCBLOCK(ToggleDate),  /* 0xee */
#else
    PTOCBLOCK(TruncText),         /* 0xDE */
    PTOCBLOCK(TruncString),       /* 0xE0 */
    PTOCBLOCK(ZERO),              /* 0xE2 */
    PTOCBLOCK(ZERO),              /* 0xE4 */
    PTOCBLOCK(StringToExtended),  /* 0xE6 */
    PTOCBLOCK(ExtendedToString),  /* 0xE8 */
    PTOCBLOCK(ZERO),              /* 0xEA */
    PTOCBLOCK(StringToFormatRec), /* 0xEC */
    PTOCBLOCK(ToggleDate),        /* 0xEE */
#endif

    PTOCBLOCK(LongSecs2Date), /* 0xf0 */
    PTOCBLOCK(LongDate2Secs), /* 0xf2 */

    PTOCBLOCK(String2Time),   /* 0xF4 String2Time */
    PTOCBLOCK(String2Date),   /* 0xF6 String2Date */
    PTOCBLOCK(InitDateCache), /* 0xF8 InitDateCache */

#if defined(NEWSTUBS)
    PTOCBLOCK(IntlTokenize),   /* 0xfa */
    PTOCBLOCK(GetFormatOrder), /* 0xfc */
#else
    PTOCBLOCK(ZERO), /* 0xFA */
    PTOCBLOCK(ZERO), /* 0xFC */
#endif
    PTOCBLOCK(StyledLineBreak), /* 0xFE */

};

PRIVATE selectorblock_t scriptutil_block[] = {
    {0, 0x36, 2, scriptptoc0},
    {0xDC, 0xFE, 2, scriptptoc1},
    {0, 0, 0, 0},
};

PRIVATE ptocblock_t high_scriptutil_ptoc0[] = {
    PTOCBLOCK(CharacterByteType), /* 0x10; 0xc2060010 */
    PTOCBLOCK(CharacterType),     /* 0x12; 0xC2060012 */
    PTOCBLOCK(ZERO),              /* 0x14 */
    PTOCBLOCK(ZERO),              /* 0x16 */
    PTOCBLOCK(TransliterateText), /* 0x18; 0xC20E0018 */
    PTOCBLOCK(ZERO),              /* 0x1A */
    PTOCBLOCK(ZERO),              /* 0x1C */
    PTOCBLOCK(ZERO),              /* 0x1E */
    PTOCBLOCK(ZERO),              /* 0x20 */
    PTOCBLOCK(FillParseTable),    /* 0x22; 0xC2040022 */
#if 0
  GetScriptUtilityAddress /* 0x38; 0xC4040038 */
  SetScriptUtilityAddress /* 0x3A; 0xC008003A */
  GetScriptQDPatchAddress /* 0x3C; 0xC406003C */
  SetScriptQDPatchAddress /* 0x3E; 0xC00A003E */
#endif
};

PRIVATE selectorblock_t high_scriptutil_block[] = {
    {0x10, 0x22, 2, high_scriptutil_ptoc0},
    {0, 0, 0, 0},
};

PRIVATE ptocblock_t textutils_ptoc[] = {
    PTOCBLOCK(LowercaseText),            /* 0x0000 */
    PTOCBLOCK(StripDiacritics),          /* 0x0200 */
    PTOCBLOCK(UppercaseText),            /* 0x0400 */
    PTOCBLOCK(UppercaseStripDiacritics), /* 0x0600 */
};

PRIVATE selectorblock_t textutils_block[] = {
    {0x0000, 0x0600, 0x0200, textutils_ptoc},
    {0, 0, 0, 0},
};

STUB(ScriptUtil)
{
    syn68k_addr_t retaddr;
    uint32 selector;
    uint8 low_selector_byte;
    boolean_t use_high_table_p;
    syn68k_addr_t retval;

    retaddr = POPADDR();

    selector = POPUL();

    /* Private KeyScriptEnable(count, scripts): System 9 ScriptUtils
       __KeyScriptEnable always includes Roman. The list contains big-endian
       ScriptCodes, not a terminating sentinel or an output record. */
    if (selector == 0x8006ffae) {
        const uint32 address = POPUL();
        const int16 count = POPUW();
        if (count > 0 && (!address || (uint64)address + (unsigned)count * 2 > ROMlib_guest_memory_size))
            return retaddr;
        const uint16* scripts = (const uint16*)SYN68K_TO_US(address);
        uint32 mask = 1;
        BOOLEAN unicode = FALSE;
        for (int i = 0; i < count; ++i) {
            const uint16 script = CW(scripts[i]);
            if (script < 32)
                mask |= (uint32)1 << script;
            else if (script == 0x7e)
                unicode = C_GetScript(script, smScriptEnabled) != 0;
        }
        ROMlib_process_enable_scripts(mask, unicode);
        return retaddr;
    }

    /* The original TruncString/TruncText selectors omit truncWhere. System 7 accepts
     both forms and supplies smTruncEnd for the older argument lists
     (SuperMario ScriptMgrTruncRepl.a). Normalize it before Pascal marshalling
     so the modern entry does not consume two bytes beyond the arguments. */
    if (selector == 0x8206ffe0 || selector == 0x820affde)
        PUSHUW(0);

    if (selector == 0x800affb6) {
        uint16 sel2;

        sel2 = POPUW();
        PUSHADDR(retaddr);
        retval = do_selector_block(textutils_block, sel2, ScriptUtil);
    }
    else {
        PUSHADDR(retaddr);

        use_high_table_p = ((selector & 0x40000000) == 0x40000000);

        low_selector_byte = selector & 0xFF;

        retval = do_selector_block((use_high_table_p ? high_scriptutil_block : scriptutil_block), low_selector_byte,
                                   ScriptUtil);
    }
    return retval;
}

#if defined(NEWSTUBS)
{BOn 0 AOn 4 AOnIgnoreModem 5 BOff 80 AOff 84}
#endif

PRIVATE ptocblock_t tedispatchptoc[] = {
    PTOCBLOCK(TEStylPaste),       /* 0 */
    PTOCBLOCK(TESetStyle),        /* 1 */
    PTOCBLOCK(TEReplaceStyle),    /* 2 */
    PTOCBLOCK(TEGetStyle),        /* 3 */
    PTOCBLOCK(GetStylHandle),     /* 4 */
    PTOCBLOCK(SetStylHandle),     /* 5 */
    PTOCBLOCK(GetStylScrap),      /* 6 */
    PTOCBLOCK(TEStylInsert),      /* 7 */
    PTOCBLOCK(TEGetPoint),        /* 8 */
    PTOCBLOCK(TEGetHeight),       /* 9 */
    PTOCBLOCK(TEContinuousStyle), /* 10 */
    PTOCBLOCK(SetStylScrap),      /* 11 */
    PTOCBLOCK(TECustomHook),      /* 12 */
    PTOCBLOCK(TENumStyles),       /* 13 */
    PTOCBLOCK(TEFeatureFlag),     /* 14 */
    PTOCBLOCK(TEGetHiliteRgn),    /* 15 */
};

PRIVATE selectorblock_t tedispatchblock[] = {
    {0, 15, 1, tedispatchptoc},
    {0, 0, 0, 0},
};

STUB(TEDispatch)
{
    syn68k_addr_t retaddr;
    unsigned short us;

    retaddr = POPADDR();
    us = POPUW();
    PUSHADDR(retaddr);

    return do_selector_block(tedispatchblock, us, TEDispatch);
}

PRIVATE ptocblock_t fontdispatch_ptoc[] = {
    PTOCBLOCK(IsOutline),           /* 0x0 */
    PTOCBLOCK(SetOutlinePreferred), /* 0x1 */
    PTOCBLOCK(ZERO),                /* 0x2 */
    PTOCBLOCK(ZERO),                /* 0x3 */
    PTOCBLOCK(ZERO),                /* 0x4 */
    PTOCBLOCK(ZERO),                /* 0x5 */
    PTOCBLOCK(ZERO),                /* 0x6 */
    PTOCBLOCK(ZERO),                /* 0x7 */
    PTOCBLOCK(OutlineMetrics),      /* 0x8 */
    PTOCBLOCK(GetOutlinePreferred), /* 0x9 */
    PTOCBLOCK(SetPreserveGlyph),    /* 0xA */
    PTOCBLOCK(GetPreserveGlyph),    /* 0xB */
    PTOCBLOCK(FlushFonts),          /* 0xC */
};

PRIVATE ptocblock_t fontdispatch_antialias_ptoc[] = {
    PTOCBLOCK(SetAntiAliasedTextEnabled), /* 0x11 */
    PTOCBLOCK(IsAntiAliasedTextEnabled),  /* 0x12 */
};

PRIVATE selectorblock_t fontdispatch_block[] = {
    {0x0, 0xC, 1, fontdispatch_ptoc},
    {0x11, 0x12, 1, fontdispatch_antialias_ptoc},
    {0, 0, 0, 0},
};

STUB(FontDispatch)
{
    if ((EM_D0 & 0xFFFF) == 0x13) {
        const syn68k_addr_t ret = POPADDR();
        const uint32 bounds = POPUL();
        const uint32 text = POPUL();
        const INTEGER count = POPSW();
        ROMlib_QDTextBounds(count, (Ptr)SYN68K_TO_US_CHECK0(text), (Rect*)SYN68K_TO_US_CHECK0(bounds));
        return ret;
    }
    return do_selector_block(fontdispatch_block, EM_D0 & 0xFFFF, FontDispatch);
}

STUB(DragManagerDispatch)
{
    /* Give Mixed Mode callers a distinct original vector. An installed
       classic Drag Manager can still patch this trap. */
    return _Unimplemented(ignoreme, ignoreme2);
}

PRIVATE ptocblock_t resource_dispatch_ptoc[] = {
    PTOCBLOCK(ReadPartialResource),  /* 0x1 */
    PTOCBLOCK(WritePartialResource), /* 0x2 */
    PTOCBLOCK(SetResourceSize),      /* 0x3 */
};

PRIVATE ptocblock_t resource_dispatch_ptoc10[] = {
    PTOCBLOCK(GetNextFOND), /* 10 */
};

PRIVATE selectorblock_t resource_dispatch_block[] = {
    {0x1, 0x3, 1, resource_dispatch_ptoc},
    {10, 10, 1, resource_dispatch_ptoc10},
    {0, 0, 0, 0},
};

STUB(ResourceDispatch)
{
    if ((EM_D0 & 0xffff) == 8) {
        syn68k_addr_t retaddr = POPADDR();
        Handle map = (Handle)SYN68K_TO_US_CHECK0(POPUL());
        ROMlib_get_override_attributes(map, (uint8*)SYN68K_TO_US(EM_A7));
        EM_D0 = (EM_D0 & 0xffff0000) | (uint16)Cx(ResErr);
        ADJUST_CC_BASED_ON_D0();
        return retaddr;
    }
    if ((EM_D0 & 0xffff) == 0) {
        syn68k_addr_t retaddr = POPADDR();
        INTEGER rn = POPSW();
        Handle map = C_GetMap(rn);

        WRITEUL(EM_A7, US_TO_SYN68K_CHECK0(map));
        /* GetMap returns its Handle in the Pascal result slot; D0.W and
         condition codes describe ResErr, with D0.H and X preserved. */
        EM_D0 = (EM_D0 & 0xffff0000) | (uint16)Cx(ResErr);
        ADJUST_CC_BASED_ON_D0();
        return retaddr;
    }
    return do_selector_block(resource_dispatch_block, EM_D0 & 0xffff, ResourceDispatch);
}

#if defined(NEWSTUBS)
{InitDBPack 100 DBKill 20e DBDisposeQuery 210 DBRemoveResultHandler 215 DBGetNewQuery 30f DBEnd 403 DBExec 408 DBState 409 DBUnGetItem 40d DBResultsToText 413 DBBreak 50b DBInstallResultHandler 514 DBGetResultHandler 516 DBGetSessionNum 605 DBSend 706 DBStartQuery 811 DBGetQueryResults
     a12 DBSendItem b07 DBInit e02 DBGetErr e0a DBGetItem 100c DBGetConnInfo 1704}
#endif

STUB(Fix2X)
{
    syn68k_addr_t retaddr;
    void* retp;
    Fixed f;

    retaddr = POPADDR();
    f = (Fixed)POPUL();
    retp = (void*)POPADDR();

    R_Fix2X((void*)SYN68K_TO_US(retaddr), f, (extended80*)SYN68K_TO_US(retp));

    PUSHADDR((syn68k_addr_t)retp);
    return retaddr;
}

STUB(Frac2X)
{
    syn68k_addr_t retaddr;
    void* retp;
    Fract f;

    retaddr = POPADDR();
    f = (Fract)POPUL();
    retp = (void*)POPADDR();

    R_Frac2X((void*)SYN68K_TO_US(retaddr), f, (extended80*)SYN68K_TO_US(retp));

    PUSHADDR((syn68k_addr_t)retp);
    return retaddr;
}

#define SAVE_A1_D1_D2()            \
    uint32 savea1, saved1, saved2; \
    savea1 = EM_A1;                \
    saved1 = EM_D1;                \
    saved2 = EM_D2

#define RESTORE_A1_D1_D2() \
    EM_A1 = savea1;        \
    EM_D1 = saved1;        \
    EM_D2 = saved2

STUB(HandToHand)
{
    Handle vp;

    SAVE_A1_D1_D2();
    vp = (Handle)SYN68K_TO_US_CHECK0(EM_A0);
    EM_D0 = HandToHand(&vp);
    EM_A0 = (long)US_TO_SYN68K_CHECK0(vp);
    RESTORE_A1_D1_D2();
    ADJUST_CC_BASED_ON_D0();
    RTS();
}

STUB(PtrToHand)
{
    Handle dsthand = NULL;

    SAVE_A1_D1_D2();
    EM_D0 = PtrToHand((Ptr)SYN68K_TO_US_CHECK0(EM_A0), &dsthand, EM_D0);
    EM_A0 = US_TO_SYN68K_CHECK0(dsthand);
    RESTORE_A1_D1_D2();
    ADJUST_CC_BASED_ON_D0();
    RTS();
}

STUB(PtrToXHand)
{
    long save_a1;

    SAVE_A1_D1_D2();
    save_a1 = EM_A1;
    EM_D0 = PtrToXHand((Ptr)SYN68K_TO_US_CHECK0(EM_A0), (Handle)SYN68K_TO_US_CHECK0(EM_A1), EM_D0);
    EM_A0 = save_a1; /* a1 == dsthand, which goes into a0 at end */
    RESTORE_A1_D1_D2();
    ADJUST_CC_BASED_ON_D0();
    RTS();
}

STUB(HandAndHand)
{
    long savehand;

    SAVE_A1_D1_D2();
    savehand = EM_A1;
    EM_D0 = HandAndHand((Handle)SYN68K_TO_US_CHECK0(EM_A0), (Handle)SYN68K_TO_US_CHECK0(EM_A1));
    EM_A0 = savehand;
    RESTORE_A1_D1_D2();
    ADJUST_CC_BASED_ON_D0();
    RTS();
}

STUB(PtrAndHand)
{
    long savehand;

    SAVE_A1_D1_D2();
    savehand = EM_A1;
    EM_D0 = PtrAndHand((Ptr)SYN68K_TO_US_CHECK0(EM_A0), (Handle)SYN68K_TO_US_CHECK0(EM_A1), EM_D0);
    EM_A0 = savehand;
    RESTORE_A1_D1_D2();
    ADJUST_CC_BASED_ON_D0();
    RTS();
}

STUB(Date2Secs)
{
    LONGINT l;

    SAVE_A1_D1_D2();
    Date2Secs((DateTimeRec*)SYN68K_TO_US_CHECK0(EM_A0), &l);
    EM_D0 = l;
    RESTORE_A1_D1_D2();
    RTS();
}

STUB(Secs2Date)
{
    SAVE_A1_D1_D2();
    Secs2Date(EM_D0, (DateTimeRec*)SYN68K_TO_US_CHECK0(EM_A0));
    RESTORE_A1_D1_D2();
    RTS();
}

STUB(Enqueue)
{
    SAVE_A1_D1_D2();
    Enqueue((QElemPtr)SYN68K_TO_US_CHECK0(EM_A0), (QHdrPtr)SYN68K_TO_US_CHECK0(EM_A1)); /* TODO: check to see if Mac
					       alters D0 */
    RESTORE_A1_D1_D2();
    RTS();
}

STUB(Dequeue)
{
    SAVE_A1_D1_D2();
    EM_D0 = Dequeue((QElemPtr)SYN68K_TO_US_CHECK0(EM_A0), (QHdrPtr)SYN68K_TO_US_CHECK0(EM_A1));
    RESTORE_A1_D1_D2();
    ADJUST_CC_BASED_ON_D0();
    RTS();
}

/*
 * NOTE: The Key1Trans and Key2Trans implementations are just transcriptions
 *	 of what I had in stubs.s.  I'm still not satisified that we have
 *	 the real semantics of these two routines down.
 */

#define KEYTRANSMACRO()                                         \
    unsigned short uw;                                          \
                                                                \
    uw = EM_D1 << 8;                                            \
    uw |= cpu_state.regs[2].uw.n;                               \
    EM_D0 = (KeyTrans((void*)0, uw, (LONGINT*)0) >> 16) & 0xFF; \
    RTS();

STUB(Key1Trans)
{
    KEYTRANSMACRO();
}

STUB(Key2Trans)
{
    KEYTRANSMACRO();
}

STUB(NMInstall)
{
    EM_D0 = NMInstall((NMRecPtr)SYN68K_TO_US_CHECK0(EM_A0));
    RTS();
}

STUB(NMRemove)
{
    EM_D0 = NMRemove((NMRecPtr)SYN68K_TO_US_CHECK0(EM_A0));
    RTS();
}

PRIVATE ptocblock_t alias_dispatch_ptoc[] = {
    PTOCBLOCK(FindFolder), /* 00 */
    PTOCBLOCK(ZERO),
    PTOCBLOCK(NewAlias),     /* 02 */
    PTOCBLOCK(ResolveAlias), /* 03 */
    PTOCBLOCK(ZERO),
    PTOCBLOCK(MatchAlias),                  /* 05 */
    PTOCBLOCK(UpdateAlias),                 /* 06 */
    PTOCBLOCK(GetAliasInfo),                /* 07 */
    PTOCBLOCK(NewAliasMinimal),             /* 08 */
    PTOCBLOCK(NewAliasMinimalFromFullPath), /* 09 */
    PTOCBLOCK(ZERO),
    PTOCBLOCK(ZERO),
    PTOCBLOCK(ResolveAliasFile), /* 0C */
};

/* Pascal OSErr (short, OSType, short *, StringPtr), arguments popped in reverse. */
PRIVATE ptocblock_t alias_folder_name_ptoc[] = {
    {ROMlib_get_folder_name, 2 | (5 << 3) | (5 << 6) | (4 << 9) | (2 << 12)},
};

/* Pascal OSErr (FSSpec*, AliasHandle, Boolean, FSSpec*, Boolean*). */
PRIVATE ptocblock_t alias_follow_finder_ptoc[] = {
    {ROMlib_follow_finder_alias, 2 | (5 << 3) | (5 << 6) | (1 << 9) | (5 << 12) | (5 << 15)},
};

PRIVATE ptocblock_t alias_file_ptoc[] = {
    {ROMlib_resolve_alias_file_with_mount_flags, 2 | (4 << 3) | (5 << 6) | (5 << 9) | (1 << 12) | (5 << 15)},
    {ROMlib_is_alias_file, 2 | (5 << 3) | (5 << 6) | (5 << 9)},
};

PRIVATE selectorblock_t alias_dispatch_block[] = {
    {0, 0xC, 1, alias_dispatch_ptoc},
    {14, 14, 1, alias_folder_name_ptoc},
    {15, 15, 1, alias_follow_finder_ptoc},
    {0x29, 0x2a, 1, alias_file_ptoc},
    {0, 0, 0, 0},
};

STUB(AliasDispatch)
{
    return do_selector_block(alias_dispatch_block, EM_D0 & 0xFFFF, AliasDispatch);
}

PRIVATE ptocblock_t pack4ptoc[] = {
    PTOCBLOCK(ROMlib_Faddx),      /* 00 */
    PTOCBLOCK(ROMlib_Fsetenv),    /* 01 */
    PTOCBLOCK(ROMlib_Fsubx),      /* 02 */
    PTOCBLOCK(ROMlib_Fgetenv),    /* 03 */
    PTOCBLOCK(ROMlib_Fmulx),      /* 04 */
    PTOCBLOCK(ROMlib_Fsethv),     /* 05 */
    PTOCBLOCK(ROMlib_Fdivx),      /* 06 */
    PTOCBLOCK(ROMlib_Fgethv),     /* 07 */
    PTOCBLOCK(ROMlib_Fcmpx),      /* 08 */
    PTOCBLOCK(ROMlib_Fdec2x),     /* 09 */
    PTOCBLOCK(ROMlib_FcpXx),      /* 0A */
    PTOCBLOCK(ROMlib_Fx2dec),     /* 0B */
    PTOCBLOCK(ROMlib_Fremx),      /* 0C */
    PTOCBLOCK(ROMlib_FnegX),      /* 0D */
    PTOCBLOCK(ROMlib_Fx2X),       /* 0E */
    PTOCBLOCK(ROMlib_FabsX),      /* 0F */
    PTOCBLOCK(ROMlib_FX2x),       /* 10 */
    PTOCBLOCK(ROMlib_Fcpysgnx),   /* 11 */
    PTOCBLOCK(ROMlib_FsqrtX),     /* 12 */
    PTOCBLOCK(ROMlib_FnextX),     /* 13 */
    PTOCBLOCK(ROMlib_FrintX),     /* 14 */
    PTOCBLOCK(ZERO),              /* 15 */
    PTOCBLOCK(ROMlib_FtintX),     /* 16 */
    PTOCBLOCK(ROMlib_Fprocentry), /* 17 */
    PTOCBLOCK(ROMlib_FscalbX),    /* 18 */
    PTOCBLOCK(ROMlib_Fprocexit),  /* 19 */
    PTOCBLOCK(ROMlib_FlogbX),     /* 1A */
    PTOCBLOCK(ROMlib_Ftestxcp),   /* 1B */
    PTOCBLOCK(ROMlib_Fclassx),    /* 1C */
};

PRIVATE selectorblock_t pack4block[] = {
    {0, 0x1C, 1, pack4ptoc},
    {0, 0, 0, 0},
};

STUB(Pack4)
{
    unsigned short us;

    us = READUW(EM_A7 + 4);
    return do_selector_block(pack4block, us & 0xFF, Pack4);
}

PRIVATE ptocblock_t pack5ptoc[] = {
    PTOCBLOCK(ROMlib_FlnX),      /* 00 */
    PTOCBLOCK(ROMlib_Flog2X),    /* 02 */
    PTOCBLOCK(ROMlib_Fln1X),     /* 04 */
    PTOCBLOCK(ROMlib_Flog21X),   /* 06 */
    PTOCBLOCK(ROMlib_FexpX),     /* 08 */
    PTOCBLOCK(ROMlib_Fexp2X),    /* 0A */
    PTOCBLOCK(ROMlib_Fexp1X),    /* 0C */
    PTOCBLOCK(ROMlib_Fexp21X),   /* 0E */
    PTOCBLOCK(ROMlib_Fxpwri),    /* 10 */
    PTOCBLOCK(ROMlib_Fxpwry),    /* 12 */
    PTOCBLOCK(ROMlib_Fcompound), /* 14 */
    PTOCBLOCK(ROMlib_Fannuity),  /* 16 */
    PTOCBLOCK(ROMlib_FsinX),     /* 18 */
    PTOCBLOCK(ROMlib_FcosX),     /* 1A */
    PTOCBLOCK(ROMlib_FtanX),     /* 1C */
    PTOCBLOCK(ROMlib_FatanX),    /* 1E */
    PTOCBLOCK(ROMlib_FrandX),    /* 20 */
};

PRIVATE selectorblock_t pack5block[] = {
    {0, 0x20, 2, pack5ptoc},
    {0, 0, 0, 0},
};

STUB(Pack5)
{
    syn68k_addr_t retaddr;
    unsigned short uw;

    retaddr = POPADDR();
    uw = POPUW();
    PUSHADDR(retaddr);
    return do_selector_block(pack5block, uw & 0xFF, Pack5);
}

PRIVATE ptocblock_t pack0ptoc[] = {
    PTOCBLOCK(LActivate),   /* 0 */
    PTOCBLOCK(LAddColumn),  /* 1 */
    PTOCBLOCK(LAddRow),     /* 2 */
    PTOCBLOCK(LAddToCell),  /* 3 */
    PTOCBLOCK(LAutoScroll), /* 4 */
    PTOCBLOCK(LCellSize),   /* 5 */
    PTOCBLOCK(LClick),      /* 6 */
    PTOCBLOCK(LClrCell),    /* 7 */
    PTOCBLOCK(LDelColumn),  /* 8 */
    PTOCBLOCK(LDelRow),     /* 9 */
    PTOCBLOCK(LDispose),    /* 10 */
    PTOCBLOCK(LDoDraw),     /* 11 */
    PTOCBLOCK(LDraw),       /* 12 */
    PTOCBLOCK(LFind),       /* 13 */
    PTOCBLOCK(LGetCell),    /* 14 */
    PTOCBLOCK(LGetSelect),  /* 15 */
    PTOCBLOCK(LLastClick),  /* 16 */
    PTOCBLOCK(LNew),        /* 17 */
    PTOCBLOCK(LNextCell),   /* 18 */
    PTOCBLOCK(LRect),       /* 19 */
    PTOCBLOCK(LScroll),     /* 20 */
    PTOCBLOCK(LSearch),     /* 21 */
    PTOCBLOCK(LSetCell),    /* 22 */
    PTOCBLOCK(LSetSelect),  /* 23 */
    PTOCBLOCK(LSize),       /* 24 */
    PTOCBLOCK(LUpdate),     /* 25 */
};

PRIVATE selectorblock_t pack0block[] = {
    {0, 100, 4, pack0ptoc},
    {0, 0, 0, 0},
};

STUB(Pack0)
{
    syn68k_addr_t retaddr;
    unsigned short uw;

    retaddr = POPADDR();
    uw = POPUW();
    PUSHADDR(retaddr);
    return do_selector_block(pack0block, uw, Pack0);
}

PRIVATE ptocblock_t osdispatch_ptoc0[] = {
    PTOCBLOCK(TempMaxMem),  /* 0x0015 */
    PTOCBLOCK(TempTopMem),  /* 0x0016 */
    PTOCBLOCK(ZERO),        /* 0x0017 */
    PTOCBLOCK(TempFreeMem), /* 0x0018 */
};

PRIVATE ptocblock_t osdispatch_ptoc2[] = {
    PTOCBLOCK(TempNewHandle),     /* 0x001D */
    PTOCBLOCK(TempHLock),         /* 0x001E */
    PTOCBLOCK(TempHUnlock),       /* 0x001F */
    PTOCBLOCK(TempDisposeHandle), /* 0x0020 */
};

PRIVATE ptocblock_t osdispatch_ptoc3[] = {
    PTOCBLOCK(AcceptHighLevelEvent),               /* 0x0033 */
    PTOCBLOCK(PostHighLevelEvent),                 /* 0x0034 */
    PTOCBLOCK(GetProcessSerialNumberFromPortName), /* 0x0035 */
};

PRIVATE ptocblock_t osdispatch_ptoc4[] = {
    PTOCBLOCK(GetCurrentProcess),     /* 0x0037 */
    PTOCBLOCK(GetNextProcess),        /* 0x0038 */
    PTOCBLOCK(GetFrontProcess),       /* 0x0039 */
    PTOCBLOCK(GetProcessInformation), /* 0x003A */
    PTOCBLOCK(SetFrontProcess),       /* 0x003B */
    PTOCBLOCK(WakeUpProcess),         /* 0x003C */
    PTOCBLOCK(SameProcess),           /* 0x003D */
};

PRIVATE ptocblock_t osdispatch_ptoc5[] = {
    PTOCBLOCK(GetSpecificHighLevelEvent),          /* 0x0045 */
    PTOCBLOCK(GetPortNameFromProcessSerialNumber), /* 0x0046 */
};

PRIVATE selectorblock_t osdispatch_block[] = {
    {0x0015, 0x0018, 1, osdispatch_ptoc0}, {0x001D, 0x0020, 1, osdispatch_ptoc2},

    {0x0033, 0x0035, 1, osdispatch_ptoc3}, {0x0037, 0x003D, 1, osdispatch_ptoc4},
    {0x0045, 0x0046, 1, osdispatch_ptoc5}, {0, 0, 0, 0},
};

/* Let the PPC bridge reject unsupported selectors before entering the 68K
   dispatcher, whose historical failure path opens a Restart-only dialog. */
PUBLIC boolean_t ROMlib_osdispatch_available(uint32 selector)
{
    const selectorblock_t* block;
    if (selector == 0x10)
        return ROMlib_creator == TICK("MACS") && !(size_info.size_flags & SZisHighLevelEventAware);
    if (selector == 0x01 || selector == 0x02 || selector == 0x04 || selector == 0x08 || selector == 0x13 ||
        selector == 0x14 || (selector >= 0x3f && selector <= 0x43) || selector == 0x36 || selector == 0x47 ||
        selector == 0x5a || selector == 0x5b || selector == 0x63 || selector == 0x74 || selector == 0x75 ||
        selector == 0x7c)
        return TRUE;
    for (block = osdispatch_block; block->descriptorp; ++block)
        if (selector >= block->first && selector <= block->last)
            return TRUE;
    return FALSE;
}

/* Layer Manager selectors and Pascal stack ABI: System 7.1 Internal/C/Layers.h.
   Window-state records are shared by desk accessories such as Puzzle. */
STUB(LayerDispatch)
{
    syn68k_addr_t retaddr = POPADDR();
    uint32 state, window, size;
    switch ((int8)EM_D0) {
    case -7: { /* GetParent, including a guest Layer Manager patch's tail call. */
        window = POPUL();
        const void* parent = ROMlib_layer_parent(SYN68K_TO_US_CHECK0(window));
        WRITEUL(EM_A7, US_TO_SYN68K_CHECK0(parent));
        break;
    }
    case 2: /* IsLayer */
        window = POPUL();
        WRITEUB(EM_A7, ROMlib_layer_p(SYN68K_TO_US_CHECK0(window)));
        break;
    case 3: /* GetCurLayer */
        WRITEUL(EM_A7, US_TO_SYN68K_CHECK0(ROMlib_layer_current()));
        break;
    case 4: /* SetCurLayer */
        window = POPUL();
        ROMlib_layer_set(SYN68K_TO_US_CHECK0(window));
        break;
    case 5: { /* SwapCurLayer */
        window = POPUL();
        const void* previous = ROMlib_layer_swap(SYN68K_TO_US_CHECK0(window));
        WRITEUL(EM_A7, US_TO_SYN68K_CHECK0(previous));
        break;
    }
    case 6: { /* GetSubWindows */
        window = POPUL();
        WindowPtr first = ROMlib_layer_children(SYN68K_TO_US_CHECK0(window));
        WRITEUL(EM_A7, US_TO_SYN68K_CHECK0(first));
        break;
    }
    case 10:
        size = POPUL();
        state = POPUL();
        window = POPUL();
        WRITEUW(EM_A7, ROMlib_get_window_state(window ? SYN68K_TO_US(window) : NULL, state ? SYN68K_TO_US(state) : NULL,
                                               size ? SYN68K_TO_US(size) : NULL));
        break;
    case 11:
        state = POPUL();
        window = POPUL();
        WRITEUW(EM_A7,
                ROMlib_set_window_state(window ? SYN68K_TO_US(window) : NULL, state ? SYN68K_TO_US(state) : NULL));
        break;
    default:
        PUSHUL(retaddr);
        return _Unimplemented(ignoreme, ignoreme2);
    }
    return retaddr;
}

STUB(OSDispatch)
{
    syn68k_addr_t retaddr;
    unsigned short uw;

    retaddr = POPADDR();
    uw = POPUW();
    if (uw == 0x10 && ROMlib_creator == TICK("MACS") && !(size_info.size_flags & SZisHighLevelEventAware)) {
        /* Finder 6 CODE 4:317a uses the older five-argument KernelLaunch,
           not System 7's InternalLaunchPB pointer. The stack contains stack
           size, SIZE task flags, working-directory reference, partition size
           and Pascal application name, followed by a short result slot. */
        uint32 stack_size = POPUL();
        POPUL(); /* The child selects task flags from the actual SIZE resource. */
        INTEGER volume = POPUW();
        uint32 partition_size = POPUL(), name = POPUL();
        FSSpec application;
        OSErr error = !name || !partition_size || partition_size > 0x7fffffff || stack_size > partition_size
                          ? paramErr
                          : C_FSMakeFSSpec(volume, 0, SYN68K_TO_US(name), &application);
        if (!error)
            error = ROMlib_launch_continue(&application, NULL);
        /* The application runs in a real separate host process. Return its
           launch status, rather than inventing a guest scheduler task ID. */
        WRITEUW(EM_A7, error);
        return retaddr;
    }
    if (uw == 0x13) {
        WRITEUW(EM_A7, ROMlib_current_process_id());
        return retaddr;
    }
    if (uw == 0x08) {
        /* ProcessMgr/LayerMgrPatches.c: LClipAbove swaps to the specified
           layer, performs the real ClipAbove operation, then restores it. */
        uint32 window = POPUL(), layer = POPUL();
        void* previous = ROMlib_layer_swap(SYN68K_TO_US_CHECK0(layer));
        ClipAbove(SYN68K_TO_US_CHECK0(window));
        ROMlib_layer_set(previous);
        return retaddr;
    }
    if (uw == 0x17) {
        uint32 result = POPUL();
        INTEGER descriptor = POPUW();
        WRITEUL(EM_A7, ROMlib_get_legacy_process_info(descriptor, result ? SYN68K_TO_US(result) : NULL));
        return retaddr;
    }
    if (uw == 0x01) {
        INTEGER slots = POPUW();
        uint32 tasks = POPUL();
        WRITEUW(EM_A7, ROMlib_get_active_task_info(tasks ? SYN68K_TO_US(tasks) : NULL, slots));
        return retaddr;
    }
    if (uw == 0x02) {
        /* GetSwitchInfo is a procedure with one eight-byte output record and
           no result slot (ProcessMgr/Puppet.h and Puppet.c). ROMAW has no
           Switcher/MultiFinder scheduler; report its absence so callers use
           their single-application path instead of assuming SWIT services. */
        uint32 info = POPUL();
        if (info) {
            WRITEUL(info, 0);
            WRITEUL(info + 4, 0);
        }
        return retaddr;
    }
    if (uw == 0x36) {
        uint32 name = POPUL(), file = POPUL();
        WRITEUW(EM_A7, C_LaunchDeskAccessory(file ? SYN68K_TO_US(file) : NULL, name ? SYN68K_TO_US(name) : NULL));
        return retaddr;
    }
    if (uw == 0x04) {
        /* Private Process Manager GetLayer: no arguments, LayerPtr result. */
        void* layer = ROMlib_layer_application();
        WRITEUL(EM_A7, layer ? US_TO_SYN68K(layer) : 0);
        return retaddr;
    }
    if (uw == 0x0f) {
        uint32 packed = POPUL();
        Point point = {(INTEGER)(packed >> 16), (INTEGER)packed};
        void* layer = ROMlib_layer_find(point);
        WRITEUL(EM_A7, layer ? US_TO_SYN68K(layer) : 0);
        return retaddr;
    }
    if (uw == 0x31) {
        /* Cross-process Apple-menu registration is optional in this
         single-application runtime. Leave the caller's local menu intact. */
        POPUL(); /* refCon */
        POPUL(); /* help */
        POPUW(); /* mark */
        POPUL(); /* icon */
        POPUW(); /* style */
        POPUW(); /* script */
        POPUL(); /* name */
        WRITEUW(EM_A7, -4 /* unimpErr */);
        return retaddr;
    }
    if (uw == 0x32) {
        POPUL(); /* refCon; zero clears all registered entries. */
        WRITEUW(EM_A7, noErr);
        return retaddr;
    }
    if (uw == 0x14) {
        uint32 output = POPUL();
        /* Pre-AppleEvent Finder declares the old region-input procedure.
           Later Finder releases advertise high-level events and use the
           LayerPtr-output function. This is an ABI choice, not an OS-version
           requirement; spoofing SysVersion must not change its stack shape. */
        if (ROMlib_creator == TICK("MACS") && !(size_info.size_flags & SZisHighLevelEventAware)) {
            OSErr error = ROMlib_legacy_desktop_layer_owner(output ? SYN68K_TO_US(output) : NULL);
            if (error)
                MemErr = CW(error);
            return retaddr;
        }
        WRITEUW(EM_A7, ROMlib_new_desktop_layer_owner(output ? SYN68K_TO_US(output) : NULL));
        return retaddr;
    }
    if (uw == 0x47) {
        /* There is no separate Temp Memory partition to lend space. The system
         side includes purgeable blocks above its last immovable block, as in
         ProcessMgr's SysZoneFloatSizes/GetSystemHiFreeBytes. */
        uint32 low = POPUL(), high = POPUL();
        if (high)
            WRITEUL(high, ROMlib_system_high_free_bytes());
        if (low)
            WRITEUL(low, 0);
        WRITEUW(EM_A7, noErr);
        return retaddr;
    }
    if (uw == 0x42 || uw == 0x43) {
        /* Volume notification is optional. Reject registration explicitly while
         preserving the two-longword Pascal ABI; callers can poll volumes. */
        POPUL();
        POPUL();
        WRITEUW(EM_A7, uw == 0x42 ? -4 /* unimpErr */ : noErr);
        return retaddr;
    }
    if (uw == 0x74) {
        /* FinderLib's no-argument LONGINT query feeds Largest Unused Block,
           after the caller subtracts DefltStack. Temporary allocations use
           the system zone in ROMAW; report that allocator's actual maximum. */
        Size growth = 0;
        Size largest = C_TempMaxMem(&growth);
        WRITEUL(EM_A7, largest > 0 ? largest : 0);
        return retaddr;
    }
    if (uw == 0x75) {
        /* Finder's private per-process memory query has three longword
           pointer arguments. Extended accounting is unavailable; preserve
           its output storage and return the genuine unsupported status. */
        POPUL();
        POPUL();
        POPUL();
        WRITEUW(EM_A7, -4 /* unimpErr */);
        return retaddr;
    }
    if (uw == 0x7c) {
        uint32 message = POPUL();
        HighLevelEventMsgPtr previous =
            ROMlib_hle_exchange_current(message ? (HighLevelEventMsgPtr)SYN68K_TO_US(message) : NULL);
        WRITEUL(EM_A7, previous ? US_TO_SYN68K(previous) : 0);
        return retaddr;
    }
    if (uw == 0x3f) {
        uint32 serial = POPUL();
        WRITEUW(EM_A7, ROMlib_get_system_client_process(serial ? SYN68K_TO_US(serial) : NULL));
        return retaddr;
    }
    if (uw == 0x40 || uw == 0x41) {
        WRITEUW(EM_A7, uw == 0x40 ? ROMlib_begin_system_mode() : ROMlib_end_system_mode());
        return retaddr;
    }
    if (uw == 0x5a) {
        uint32 mouse = POPUL(), callback = POPUL();
        LONGINT refcon = POPUL();
        BOOLEAN wind_close = POPUB();
        uint32 behind = POPUL();
        INTEGER proc_id = POPUW();
        BOOLEAN visible = POPUB();
        uint32 title = POPUL(), bounds = POPUL();
        WindowPtr w = ROMlib_system_window_new(
            bounds ? SYN68K_TO_US(bounds) : NULL, title ? SYN68K_TO_US(title) : NULL, visible, proc_id,
            behind == UINT32_MAX ? (WindowPtr)-1
            : behind             ? SYN68K_TO_US(behind)
                                 : NULL,
            wind_close, refcon, callback ? (ProcPtr)SYN68K_TO_US(callback) : NULL, mouse ? SYN68K_TO_US(mouse) : NULL);
        WRITEUL(EM_A7, w ? US_TO_SYN68K(w) : 0);
        return retaddr;
    }
    if (uw == 0x5b) {
        uint32 window = POPUL();
        WRITEUW(EM_A7, ROMlib_system_window_dispose(window ? SYN68K_TO_US(window) : NULL));
        return retaddr;
    }
    if (uw == 0x63) {
        /* MacDialogsLib compares this WindowPtr with the clicked dialog
           before selecting it. WindowsLib uses it as the front repaint target. */
        WindowPtr window = FrontWindow();
        WRITEUL(EM_A7, window ? US_TO_SYN68K(window) : 0);
        return retaddr;
    }
    PUSHADDR(retaddr);
    return do_selector_block(osdispatch_block, uw, OSDispatch);
}

PRIVATE selector_table_entry_t pack8_table[] = {
    {0x011E, PTOCBLOCK(AESetInteractionAllowed)},
    {0x0204, PTOCBLOCK(AEDisposeDesc)},
    {0x0219, PTOCBLOCK(AEResetTimer)},
    {0x021A, PTOCBLOCK(AEGetTheCurrentEvent)},
    {0x021B, PTOCBLOCK(AEProcessAppleEvent)},
    {0x021D, PTOCBLOCK(AEGetInteractionAllowed)},
    {0x022B, PTOCBLOCK(AESuspendTheCurrentEvent)},
    {0x022C, PTOCBLOCK(AESetTheCurrentEvent)},
    {0x023A, PTOCBLOCK(AEDisposeToken)},
    {0x0405, PTOCBLOCK(AEDuplicateDesc)},
    {0x0407, PTOCBLOCK(AECountItems)},
    {0x040E, PTOCBLOCK(AEDeleteItem)},
    /* AEDeleteParam */
    {0x0413, PTOCBLOCK(AEDeleteKeyDesc)},
    {0x0441, PTOCBLOCK(AEManagerInfo)},
    {0x0500, PTOCBLOCK(AEInstallSpecialHandler)},
    {0x0501, PTOCBLOCK(AERemoveSpecialHandler)},
    {0x052D, PTOCBLOCK(AEGetSpecialHandler)},
    {0x0536, PTOCBLOCK(AEREesolve)},
    {0x0603, PTOCBLOCK(AECoerceDesc)},
    {0x0609, PTOCBLOCK(AEPutDesc)},
    /* AEPutParamDesc */
    {0x0610, PTOCBLOCK(AEPutKeyDesc)},
    {0x061C, PTOCBLOCK(AEInteractWithUser)},
    {0x0627, PTOCBLOCK(AEPutAttributeDesc)},
    {0x0632, PTOCBLOCK(_AE_hdlr_delete)}, /* internal */
    {0x0706, PTOCBLOCK(AECreateList)},
    {0x0720, PTOCBLOCK(AERemoveEventHandler)},
    {0x0723, PTOCBLOCK(AERemoveCoercionHandler)},
    {0x0738, PTOCBLOCK(AERemoveObjectAccessor)},
    {0x0812, PTOCBLOCK(AEGetKeyDesc)},
    {0x0818, PTOCBLOCK(AEResumeTheCurrentEvent)},
    {0x0825, PTOCBLOCK(AECreateDesc)},
    {0x0826, PTOCBLOCK(AEGetAttributeDesc)},
    {0x0828, PTOCBLOCK(AESizeOfAttribute)},
    {0x0829, PTOCBLOCK(AESizeOfKeyDesc)},
    {0x082A, PTOCBLOCK(AESizeOfNthItem)},
    {0x0831, PTOCBLOCK(_AE_hdlr_install)}, /* internal */
    {0x0833, PTOCBLOCK(_AE_hdlr_lookup)},  /* internal */
    {0x091F, PTOCBLOCK(AEInstallEventHandler)},
    {0x0921, PTOCBLOCK(AEGetEventHandler)},
    {0x092E, PTOCBLOCK(_AE_hdlr_table_alloc)}, /* internal */
    {0x0937, PTOCBLOCK(AEInstallObjectAccessor)},
    {0x0939, PTOCBLOCK(AEGetObjectAccessor)},
    {0x0A02, PTOCBLOCK(AECoercePtr)},
    {0x0A08, PTOCBLOCK(AEPutPtr)},
    {0x0A0B, PTOCBLOCK(AEGetNthDesc)},
    {0x0A0F, PTOCBLOCK(AEPutKeyPtr)},
    {0x0A16, PTOCBLOCK(AEPutAttributePtr)},
    {0x0A22, PTOCBLOCK(AEInstallCoercionHandler)},
    {0x0B0D, PTOCBLOCK(AEPutArray)},
    {0x0B14, PTOCBLOCK(AECreateAppleEvent)},
    {0x0B24, PTOCBLOCK(AEGetCoercionHandler)},
    {0x0C3B, PTOCBLOCK(AECallObjectAccessor)},
    {0x0D0C, PTOCBLOCK(AEGetArray)},
    {0x0D17, PTOCBLOCK(AESend)},
    /* AEGetParamPtr */
    {0x0E11, PTOCBLOCK(AEGetKeyPtr)},
    {0x0E15, PTOCBLOCK(AEGetAttributePtr)},
    {0x0E35, PTOCBLOCK(AESetObjectCallbacks)},
    {0x100A, PTOCBLOCK(AEGetNthPtr)},
};

static syn68k_addr_t pack8_fail_fn()
{
    syn68k_addr_t retaddr;

    if (AE_OSL_select_fn == NULL)
        C_pack8_unknown_selector();

    warning_unexpected("calling OSL 'selh' special handler");

    retaddr = POPADDR();

    /* #### just clobber a1? */
    EM_A1 = (uint32)US_TO_SYN68K_CHECK0(P_pack8_unknown_selector);

    CALL_EMULATOR((syn68k_addr_t)AE_OSL_select_fn);

    return retaddr;
}

STUB(Pack8)
{
    return do_selector_table(EM_D0 & 0xFFFF, pack8_table, NELEM(pack8_table), pack8_fail_fn, Pack8);
}

P0(PUBLIC pascal, void, pack8_unknown_selector)
{
    do_selector_error(EM_D0 & 0xFFFF, "Pack8", _Pack8);
}

PRIVATE ptocblock_t pack12ptoc[] = {
    PTOCBLOCK(Fix2SmallFract), /* 1 */
    PTOCBLOCK(SmallFract2Fix), /* 2 */
    PTOCBLOCK(CMY2RGB),        /* 3 */
    PTOCBLOCK(RGB2CMY),        /* 4 */
    PTOCBLOCK(HSL2RGB),        /* 5 */
    PTOCBLOCK(RGB2HSL),        /* 6 */
    PTOCBLOCK(HSV2RGB),        /* 7 */
    PTOCBLOCK(RGB2HSV),        /* 8 */
    PTOCBLOCK(GetColor),       /* 9 */
};

PRIVATE selectorblock_t pack12block[] = {
    {1, 9, 1, pack12ptoc},
    {0, 0, 0, 0},
};

STUB(Pack12)
{
    syn68k_addr_t retaddr;
    unsigned short uw;

    retaddr = POPADDR();
    uw = POPUW();
    PUSHADDR(retaddr);
    return do_selector_block(pack12block, uw, Pack12);
}

/*
 * NOTE: We only look at the last two bytes as the Palette selector.  Some
 *	 Apps don't load the other bytes "properly".
 */

PRIVATE ptocblock_t palette_dispatch_ptoc_1[] = {
    PTOCBLOCK(Entry2Index),       /* 0x00 */
    PTOCBLOCK(ZERO),              /* 0x01 */
    PTOCBLOCK(RestoreClutDevice), /* 0x02 */
    PTOCBLOCK(ResizePalette),     /* 0x03 */
};

PRIVATE ptocblock_t palette_dispatch_ptoc_2[] = {
    PTOCBLOCK(SaveFore),          /* 0x40D */
    PTOCBLOCK(SaveBack),          /* 0x40E */
    PTOCBLOCK(RestoreFore),       /* 0x40F */
    PTOCBLOCK(RestoreBack),       /* 0x410 */
    PTOCBLOCK(ZERO),              /* 0x11 */
    PTOCBLOCK(ZERO),              /* 0x12 */
    PTOCBLOCK(SetDepth),          /* 0xA13 */
    PTOCBLOCK(HasDepth),          /* 0xA14 */
    PTOCBLOCK(PMgrVersion),       /* 0x15 */
    PTOCBLOCK(SetPaletteUpdates), /* 0x616 */
    PTOCBLOCK(GetPaletteUpdates), /* 0x417 */
    PTOCBLOCK(ZERO),              /* 0x18 */
    PTOCBLOCK(GetGray),           /* 0x1219 */
};

PRIVATE selectorblock_t palette_dispatch_block[] = {
    {0x00, 0x03, 1, palette_dispatch_ptoc_1},
    {0x0D, 0x19, 1, palette_dispatch_ptoc_2},
    {0, 0, 0, 0},
};

STUB(PaletteDispatch)
{
    /* PaletteMgr.a uses D0's low byte. Selector 11 takes no arguments. */
    if ((EM_D0 & 0xff) == 11) {
        ROMlib_palette_hilite_changed();
        EM_D0 = 0;
        RTS();
    }
    return do_selector_block(palette_dispatch_block, EM_D0 & 0xFF, PaletteDispatch);
}

PRIVATE ptocblock_t QDExtensions_ptoc[] = {
    PTOCBLOCK(NewGWorld),        /* 0 */
    PTOCBLOCK(LockPixels),       /* 1 */
    PTOCBLOCK(UnlockPixels),     /* 2 */
    PTOCBLOCK(UpdateGWorld),     /* 3 */
    PTOCBLOCK(DisposeGWorld),    /* 4 */
    PTOCBLOCK(GetGWorld),        /* 5 */
    PTOCBLOCK(SetGWorld),        /* 6 */
    PTOCBLOCK(CTabChanged),      /* 7 */
    PTOCBLOCK(PixPatChanged),    /* 8 */
    PTOCBLOCK(PortChanged),      /* 9 */
    PTOCBLOCK(GDeviceChanged),   /* 10 */
    PTOCBLOCK(AllowPurgePixels), /* 11 */
    PTOCBLOCK(NoPurgePixels),    /* 12 */
    PTOCBLOCK(GetPixelsState),   /* 13 */
    PTOCBLOCK(SetPixelsState),   /* 14 */
    PTOCBLOCK(GetPixBaseAddr),   /* 15 */
    PTOCBLOCK(NewScreenBuffer),  PTOCBLOCK(DisposeScreenBuffer), PTOCBLOCK(GetGWorldDevice), PTOCBLOCK(QDDone),
    PTOCBLOCK(OffscreenVersion), PTOCBLOCK(NewTempScreenBuffer), PTOCBLOCK(PixMap32Bit),     PTOCBLOCK(GetGWorldPixMap),
};

PRIVATE selectorblock_t QDExtensions_block[] = {
    {0x0, 0x17, 1, QDExtensions_ptoc},
    {0, 0, 0, 0},
};

STUB(QDExtensions)
{
    unsigned long selector;

    /* i don't think this should have the `&' rewrite the selector
     blocks to handle the actual selector values */
    selector = EM_D0 & 0xFFFF;
    return do_selector_block(QDExtensions_block, selector, QDExtensions);
}

PRIVATE ptocblock_t shutdwn_ptoc[] = {
    PTOCBLOCK(ShutDwnPower),
    PTOCBLOCK(ShutDwnStart),
    PTOCBLOCK(ShutDwnInstall),
    PTOCBLOCK(ShutDwnRemove),
};

PRIVATE selectorblock_t shutdwn_block[] = {
    {
        1,
        4,
        1,
        shutdwn_ptoc,
    },
    {0, 0, 0, 0},
};

STUB(ShutDown)
{
    syn68k_addr_t retaddr;
    unsigned short uw;

    retaddr = POPADDR();
    uw = POPUW();
    PUSHADDR(retaddr);
    return do_selector_block(shutdwn_block, uw, ShutDown);
}

PRIVATE ptocblock_t pack3ptoc[] = {
    PTOCBLOCK(SFPutFile),       /* 1 */
    PTOCBLOCK(SFGetFile),       /* 2 */
    PTOCBLOCK(SFPPutFile),      /* 3 */
    PTOCBLOCK(SFPGetFile),      /* 4 */
    PTOCBLOCK(StandardPutFile), /* 5 */
    PTOCBLOCK(StandardGetFile), /* 6 */
    PTOCBLOCK(CustomPutFile),   /* 7 */
    PTOCBLOCK(CustomGetFile),   /* 8 */
};

PRIVATE selectorblock_t pack3block[] = {
    {1, 8, 1, pack3ptoc},
    {0, 0, 0, 0},
};

STUB(Pack3)
{
    syn68k_addr_t retaddr;
    unsigned short uw;

    retaddr = POPADDR();
    uw = POPUW();
    PUSHADDR(retaddr);
    return do_selector_block(pack3block, uw, Pack3);
}

PRIVATE ptocblock_t pack2ptoc[] = {
    PTOCBLOCK(DIBadMount), /* 0 */
    PTOCBLOCK(DILoad),     /* 2 */
    PTOCBLOCK(DIUnload),   /* 4 */
    PTOCBLOCK(DIFormat),   /* 6 */
    PTOCBLOCK(DIVerify),   /* 8 */
    PTOCBLOCK(DIZero),     /* 10 */
};

PRIVATE selectorblock_t pack2block[] = {
    {0, 10, 2, pack2ptoc},
    {0, 0, 0, 0},
};

STUB(Pack2)
{
    syn68k_addr_t retaddr;
    unsigned short uw;

    retaddr = POPADDR();
    uw = POPUW();
    PUSHADDR(retaddr);
    return do_selector_block(pack2block, uw, Pack2);
}

PRIVATE ptocblock_t pack6ptoc[] = {
    PTOCBLOCK(IUDateString),   /* 0x00 */
    PTOCBLOCK(IUTimeString),   /* 0x02 */
    PTOCBLOCK(IUMetric),       /* 0x04 */
    PTOCBLOCK(IUGetIntl),      /* 0x06 */
    PTOCBLOCK(IUSetIntl),      /* 0x08 */
    PTOCBLOCK(IUMagString),    /* 0x0A */
    PTOCBLOCK(IUMagIDString),  /* 0x0C */
    PTOCBLOCK(IUDatePString),  /* 0x0E */
    PTOCBLOCK(IUTimePString),  /* 0x10 */
    PTOCBLOCK(IUMystery),      /* 0x12 */
    PTOCBLOCK(IULDateString),  /* 0x14 */
    PTOCBLOCK(IULTimeString),  /* 0x16 */
    PTOCBLOCK(IUClearCache),   /* 0x18 */
    PTOCBLOCK(IUMagPString),   /* 0x1A */
    PTOCBLOCK(IUMagIDPString), /* 0x1C */
    PTOCBLOCK(IUScriptOrder),  /* 0x1E */
    PTOCBLOCK(IULangOrder),    /* 0x20 */
    PTOCBLOCK(IUTextOrder),    /* 0x22 */
    PTOCBLOCK(IUGetItlTable),  /* 0x24 */

};

PRIVATE selectorblock_t pack6block[] = {
    {0, 36, 2, pack6ptoc},
    {0, 0, 0, 0},
};

STUB(Pack6)
{
    syn68k_addr_t retaddr;
    unsigned short uw;

    retaddr = POPADDR();
    uw = POPUW();
    if (uw == 0x28) {
        uint32 record = POPUL();
        ROMlib_type_select_clear(record ? SYN68K_TO_US(record) : NULL);
        return retaddr;
    }
    if (uw == 0x2a) {
        uint32 record = POPUL(), event = POPUL();
        BOOLEAN accepted =
            ROMlib_type_select_key(event ? SYN68K_TO_US(event) : NULL, record ? SYN68K_TO_US(record) : NULL);
        WRITEUW(EM_A7, 0);
        WRITEUB(EM_A7, accepted);
        return retaddr;
    }
    if (uw == 0x2c) {
        uint32 data = POPUL(), callback = POPUL();
        INTEGER mode = POPUW(), count = POPUW();
        uint32 record = POPUL();
        WRITEUW(EM_A7, ROMlib_type_select_find(record ? SYN68K_TO_US(record) : NULL, count, mode,
                                               callback ? (ProcPtr)SYN68K_TO_US(callback) : NULL,
                                               data ? SYN68K_TO_US(data) : NULL));
        return retaddr;
    }
    if (uw == 0x2e) {
        uint32 string = POPUL();
        INTEGER script = POPUW();
        uint32 record = POPUL();
        WRITEUW(EM_A7, ROMlib_type_select_compare(record ? SYN68K_TO_US(record) : NULL, script,
                                                  string ? SYN68K_TO_US(string) : NULL));
        return retaddr;
    }
    PUSHADDR(retaddr);
    return do_selector_block(pack6block, uw, Pack6);
}

PRIVATE ptocblock_t pack7ptoc[] = {
    PTOCBLOCK(ROMlib_Fpstr2dec), /* 02 */
    PTOCBLOCK(ROMlib_Fdec2str),  /* 03 */
    PTOCBLOCK(ROMlib_Fcstr2dec), /* 04 */
};

PRIVATE selectorblock_t pack7block[] = {
    {2, 4, 1, pack7ptoc},
    {0, 0, 0, 0},
};

STUB(Pack7)
{
    syn68k_addr_t retaddr;
    unsigned short uw;
    LONGINT l;

    retaddr = POPADDR();
    uw = POPUW();
    PUSHADDR(retaddr);
    switch (uw) {
    case 0:
        NumToString(EM_D0, (StringPtr)SYN68K_TO_US_CHECK0(EM_A0));
        break;
    case 1:
        StringToNum((StringPtr)SYN68K_TO_US_CHECK0(EM_A0), &l);
        EM_D0 = l;
        break;
    default:
        return do_selector_block(pack7block, uw, Pack7);
    }

    RTS();
}

PRIVATE ptocblock_t pack11_ptoc0[] = {
    PTOCBLOCK(InitEditionPack), /* 0x0100 */
};

PRIVATE ptocblock_t pack11_ptoc1[] = {
    PTOCBLOCK(UnRegisterSection),         /* 0x0206 */
    PTOCBLOCK(IsRegisteredSection),       /* 0x0208 */
    PTOCBLOCK(ZERO),                      /* 0x020A */
    PTOCBLOCK(ZERO),                      /* 0x020C */
    PTOCBLOCK(ZERO),                      /* 0x020E */
    PTOCBLOCK(DeleteEditionContainerFile) /* 0x0210 */
};

PRIVATE ptocblock_t pack11_ptoc2[] = {
    PTOCBLOCK(GoToPublisherSection),        /* 0x0224 */
    PTOCBLOCK(GetLastEditionContainerUsed), /* 0x0226 */
    PTOCBLOCK(ZERO),                        /* 0x0228 */
    PTOCBLOCK(GetEditionOpenerProc),        /* 0x022A */
    PTOCBLOCK(SetEditionOpenerProc),        /* 0x022C */
    PTOCBLOCK(ZERO),                        /* 0x022E */
    PTOCBLOCK(ZERO),                        /* 0x0230 */
    PTOCBLOCK(NewSubscriberDialog),         /* 0x0232 */
    PTOCBLOCK(ZERO),                        /* 0x0234 */
    PTOCBLOCK(NewPublisherDialog),          /* 0x0236 */
    PTOCBLOCK(ZERO),                        /* 0x0238 */
    PTOCBLOCK(SectionOptionsDialog),        /* 0x023A */
};

PRIVATE ptocblock_t pack11_ptoc3[] = {
    PTOCBLOCK(CloseEdition), /* 0x0316 */
};

PRIVATE ptocblock_t pack11_ptoc4[] = {
    PTOCBLOCK(AssociateSection), /* 0x040C */
    PTOCBLOCK(ZERO),             /* 0x040E */
    PTOCBLOCK(ZERO),             /* 0x0410 */
    PTOCBLOCK(OpenEdition),      /* 0x0412 */
};

PRIVATE ptocblock_t pack11_ptoc5[] = {
    PTOCBLOCK(GetEditionInfo), /* 0x0422 */
};

PRIVATE ptocblock_t pack11_ptoc6[] = {
    PTOCBLOCK(CreateEditionContainerFile), /* 0x050E */
};

PRIVATE ptocblock_t pack11_ptoc7[] = {
    PTOCBLOCK(CallEditionOpenerProc), /* 0x052E */
    PTOCBLOCK(CallFormatIOProc),      /* 0x0530 */
};

PRIVATE ptocblock_t pack11_ptoc8[] = {
    PTOCBLOCK(RegisterSection), /* 0x0604 */
};

PRIVATE ptocblock_t pack11_ptoc9[] = {
    PTOCBLOCK(EditionHasFormat),     /* 0x0618 */
    PTOCBLOCK(ZERO),                 /* 0x061A */
    PTOCBLOCK(ZERO),                 /* 0x061C */
    PTOCBLOCK(GetEditionFormatMark), /* 0x061E */
    PTOCBLOCK(SetEditionFormatMark), /* 0x0620 */
};

PRIVATE ptocblock_t pack11_ptoc10[] = {
    PTOCBLOCK(OpenNewEdition), /* 0x0814 */
    PTOCBLOCK(ZERO),           /* 0x0816 */
    PTOCBLOCK(ZERO),           /* 0x0818 */
    PTOCBLOCK(ReadEdition),    /* 0x081A */
    PTOCBLOCK(WriteEdition),   /* 0x081C */
};

PRIVATE ptocblock_t pack11_ptoc11[] = {
    PTOCBLOCK(NewSection), /* 0x0A02 */
};

PRIVATE ptocblock_t pack11_ptoc12[] = {
    PTOCBLOCK(GetStandardFormats), /* 0x0A28 */
};

PRIVATE ptocblock_t pack11_ptoc13[] = {
    PTOCBLOCK(NewSubscriberExpDialog),  /* 0x0B34 */
    PTOCBLOCK(NewPublisherExpDialog),   /* 0x0B38 */
    PTOCBLOCK(SectionOptionsExpDialog), /* 0x0B3C */
};

PRIVATE selectorblock_t pack11block[] = {
    {0x0100, 0x0100, 1, pack11_ptoc0},  {0x0206, 0x0210, 2, pack11_ptoc1},  {0x0224, 0x023A, 2, pack11_ptoc2},
    {0x0316, 0x0316, 1, pack11_ptoc3},  {0x040C, 0x0412, 2, pack11_ptoc4},  {0x0422, 0x0422, 1, pack11_ptoc5},
    {0x050E, 0x050E, 1, pack11_ptoc6},  {0x052E, 0x0530, 2, pack11_ptoc7},  {0x0604, 0x0604, 1, pack11_ptoc8},
    {0x0618, 0x0620, 2, pack11_ptoc9},  {0x0814, 0x081C, 2, pack11_ptoc10}, {0x0A02, 0x0A02, 1, pack11_ptoc11},
    {0x0A28, 0x0A28, 1, pack11_ptoc12}, {0x0B34, 0x0B3C, 4, pack11_ptoc13}, {0, 0, 0, 0},
};

STUB(Pack11)
{
    return do_selector_block(pack11block, EM_D0 & 0xFFFF, Pack11);
}

PRIVATE ptocblock_t highlevel_fs_dispatch_ptoc[] = {
    PTOCBLOCK(FSMakeFSSpec),     /* 0x0001 */
    PTOCBLOCK(FSpOpenDF),        /* 0x0002 */
    PTOCBLOCK(FSpOpenRF),        /* 0x0003 */
    PTOCBLOCK(FSpCreate),        /* 0x0004 */
    PTOCBLOCK(FSpDirCreate),     /* 0x0005 */
    PTOCBLOCK(FSpDelete),        /* 0x0006 */
    PTOCBLOCK(FSpGetFInfo),      /* 0x0007 */
    PTOCBLOCK(FSpSetFInfo),      /* 0x0008 */
    PTOCBLOCK(FSpSetFLock),      /* 0x0009 */
    PTOCBLOCK(FSpRstFLock),      /* 0x000A */
    PTOCBLOCK(FSpRename),        /* 0x000B */
    PTOCBLOCK(FSpCatMove),       /* 0x000C */
    PTOCBLOCK(FSpOpenResFile),   /* 0x000D */
    PTOCBLOCK(FSpCreateResFile), /* 0x000E */
    PTOCBLOCK(FSpExchangeFiles), /* 0x000F */
};

PRIVATE selectorblock_t highlevel_fs_dispatch_block[] = {
    {0x0001, 0x000F, 1, highlevel_fs_dispatch_ptoc},
};

STUB(HighLevelFSDispatch)
{
    return do_selector_block(highlevel_fs_dispatch_block, EM_D0 & 0xF, HighLevelFSDispatch);
}

PRIVATE ptocblock_t dialog_dispatch_ptoc0[] = {
    PTOCBLOCK(GetStdFilterProc), /* 0x0203 */
};

PRIVATE ptocblock_t dialog_dispatch_ptoc1[] = {
    PTOCBLOCK(SetDialogDefaultItem),  /* 0x0304 */
    PTOCBLOCK(SetDialogCancelItem),   /* 0x0305 */
    PTOCBLOCK(SetDialogTracksCursor), /* 0x0306 */
};

PRIVATE ptocblock_t dialog_dispatch_geometry[] = {
    {ROMlib_move_dialog_item, 0x5492}, /* 0x0510: dialog, item, h, v */
    {ROMlib_size_dialog_item, 0x5492}, /* 0x0511: dialog, item, width, height */
};

PRIVATE selectorblock_t dialog_dispatch_block[] = {
    {0x0203, 0x0203, 1, dialog_dispatch_ptoc0},
    {0x0304, 0x0306, 1, dialog_dispatch_ptoc1},
    {0x0510, 0x0511, 1, dialog_dispatch_geometry},
    {0, 0, 0, NULL},
};

PRIVATE ptocblock_t menu_set_item_font_ptoc[] = {{ROMlib_set_menu_item_font, 2 | (2 << 3) | (2 << 6) | (5 << 9)}};
PRIVATE ptocblock_t menu_get_item_font_ptoc[] = {{ROMlib_get_menu_item_font, 2 | (5 << 3) | (2 << 6) | (5 << 9)}};
PRIVATE ptocblock_t menu_set_item_icon_ptoc[] = {
    {ROMlib_set_menu_item_icon, 2 | (5 << 3) | (1 << 6) | (2 << 9) | (5 << 12)}};
PRIVATE ptocblock_t menu_get_item_icon_ptoc[] = {
    {ROMlib_get_menu_item_icon, 2 | (5 << 3) | (5 << 6) | (2 << 9) | (5 << 12)}};
PRIVATE selectorblock_t menu_item_dispatch_block[] = {
    {0x040f, 0x040f, 1, menu_set_item_font_ptoc},
    {0x0510, 0x0510, 1, menu_get_item_font_ptoc},
    {0x0606, 0x0606, 1, menu_set_item_icon_ptoc},
    {0x0707, 0x0707, 1, menu_get_item_icon_ptoc},
    {0, 0, 0, NULL},
};

STUB(MenuDispatch)
{
    unsigned selector = EM_D0 & 0xffff;
    if (selector == 0x0502 || selector == 0x0503) {
        const syn68k_addr_t return_address = POPADDR();
        const uint32 value = POPUL();
        const INTEGER item = POPUW();
        MenuHandle menu = (MenuHandle)SYN68K_TO_US_CHECK0(POPUL());
        const OSErr status = selector == 0x0502
                                 ? ROMlib_set_menu_item_command(menu, item, value)
                                 : ROMlib_get_menu_item_command(menu, item, (uint32*)SYN68K_TO_US_CHECK0(value));
        WRITEUW(EM_A7, status);
        return return_address;
    }
    if (selector == 0x08FB) {
        /* MenuMgrPriv DrawMBARString: Pascal string, script, justification. */
        syn68k_addr_t retaddr = POPADDR();
        INTEGER justification = POPUW(), script = POPUW();
        uint32 text = POPUL();
        INTEGER parameter = ((justification & 0xff) << 8) | (script & 0xff);
        MBDFCALL(mbDrawMBARString, parameter, text);
        MENU_BAR_GLOBAL_INVALID |= MENU_BAR_GLOBAL_INVALID_MASK;
        *(INTEGER*)SYN68K_TO_US(EM_A7) = CWC(noErr);
        return retaddr;
    }
    if (selector == 0x0400 || selector == 0x0601) {
        syn68k_addr_t retaddr = POPADDR();
        INTEGER script = POPUW(), after = POPUW();
        ResType type = selector == 0x0601 ? POPUL() : TICK("FONT");
        uint32 menu = POPUL();
        /* The current Script Manager implements the Roman system script. */
        if (menu && (script == -1 || script == -2 || script == 0))
            C_InsertResMenu(SYN68K_TO_US(menu), type, after);
        return retaddr;
    }
    return do_selector_block(menu_item_dispatch_block, selector, MenuDispatch);
}

PRIVATE ptocblock_t control_supervisor_ptoc[] = {
    {ROMlib_set_control_supervisor, 2 | (5 << 3) | (5 << 6)},
};

PRIVATE ptocblock_t control_input_ptoc[] = {
    {ROMlib_handle_control_click, 2 | (5 << 3) | (3 << 6) | (2 << 9) | (5 << 12)},
    {ROMlib_handle_control_key, 2 | (5 << 3) | (2 << 6) | (2 << 9) | (2 << 12)},
};

static OSErr control_dispatch_set_data(ControlHandle control, INTEGER part, ResType tag, Size size, Byte* data)
{
    return ROMlib_control_data(control, part, tag, size, data, NULL, ROMLIB_CONTROL_WRITE);
}

static OSErr control_dispatch_get_data(ControlHandle control, INTEGER part, ResType tag, Size size, Byte* data,
                                       Size* actual)
{
    return ROMlib_control_data(control, part, tag, size, data, actual, ROMLIB_CONTROL_READ);
}

static OSErr control_dispatch_measure_data(ControlHandle control, INTEGER part, ResType tag, Size* actual)
{
    if (!actual)
        return paramErr;
    return ROMlib_control_data(control, part, tag, 0, NULL, actual, ROMLIB_CONTROL_MEASURE);
}

PRIVATE ptocblock_t control_data_ptoc[] = {
    {ROMlib_get_control_features, 2 | (5 << 3) | (5 << 6)},
    {control_dispatch_set_data, 2 | (5 << 3) | (2 << 6) | (4 << 9) | (4 << 12) | (5 << 15)},
    {control_dispatch_get_data, 2 | (5 << 3) | (2 << 6) | (4 << 9) | (4 << 12) | (5 << 15) | (5 << 18)},
    {control_dispatch_measure_data, 2 | (5 << 3) | (2 << 6) | (4 << 9) | (5 << 12)},
};

PRIVATE selectorblock_t control_dispatch_block[] = {
    {0x0a, 0x0b, 1, control_input_ptoc},
    {0x11, 0x14, 1, control_data_ptoc},
    {0x1a, 0x1a, 1, control_supervisor_ptoc},
    {0, 0, 0, NULL},
};

STUB(ControlDispatch)
{
    const uint16 selector = EM_D0 & 0xffff;
    if (selector == 0x0f || selector == 0x10) {
        const syn68k_addr_t return_address = POPADDR();
        const uint32 window = POPUL();
        WRITEUW(EM_A7, ROMlib_control_cycle_focus(SYN68K_TO_US_CHECK0(window), selector == 0x10));
        return return_address;
    }
    return do_selector_block(control_dispatch_block, selector, ControlDispatch);
}

STUB(DialogDispatch)
{
    if ((EM_D0 & 0xff) == 7) {
        /* IsUserCancelEvent: Boolean Pascal result occupies the high byte. */
        syn68k_addr_t retaddr = POPADDR();
        uint32 address = POPUL();
        EventRecord* event = address ? SYN68K_TO_US(address) : NULL;
        BOOLEAN cancel = ROMlib_user_cancel_event(event);
        WRITEUW(EM_A7, 0);
        WRITEUB(EM_A7, cancel);
        return retaddr;
    }
    if ((EM_D0 & 0xff) == 8) {
        syn68k_addr_t retaddr = POPADDR();
        uint32 address = POPUL();
        EventRecord ignored;
        OSEventAvail(0, &ignored);
        BOOLEAN cancel = ROMlib_next_user_cancel_event(address ? SYN68K_TO_US(address) : NULL);
        WRITEUW(EM_A7, 0);
        WRITEUB(EM_A7, cancel);
        return retaddr;
    }
    return do_selector_block(dialog_dispatch_block, EM_D0 & 0xFFFF, DialogDispatch);
}

STUB(IMVI_PPC)
{
    EM_D0 = paramErr; /* this is good enough for NetScape */
    RTS();
}

#if defined(NEWSTUBS)
{
    PPCBrowser, /* 0x0d00 */
}

{
    PPCInit,            /* 0x0 */
    PPCOpen,            /* 0x1 */
    PPCStart,           /* 0x2 */
    PPCInform,          /* 0x3 */
    PPCAccept,          /* 0x4 */
    PPCReject,          /* 0x5 */
    PPCWrite,           /* 0x6 */
    PPCRead,            /* 0x7 */
    PPCEnd,             /* 0x8 */
    PPCClose,           /* 0x9 */
    IPCListPorts,       /* 0xa */
    DeleteUserIdentity, /* 0xc */
    GetDefaultUser,     /* 0xd */
    StartSecureSession, /* 0xe */
}

#endif

PRIVATE ptocblock_t balloon_ptoc[] = {
    PTOCBLOCK(HMGetHelpMenuHandle), /* 0x0200, */
    PTOCBLOCK(HMShowBalloon),       /* 0x0b01, */
    PTOCBLOCK(HMRemoveBalloon),     /* 0x0002, */
    PTOCBLOCK(HMGetBalloons),       /* 0x0003, */
    PTOCBLOCK(HMSetBalloons),       /* 0x0104, */
    PTOCBLOCK(HMShowMenuBalloon),   /* 0x0e05, */
    PTOCBLOCK(ZERO),                /* PTOCBLOCK (HMGetIndHelpMsg),		/ * 0x1306, */
    PTOCBLOCK(HMIsBalloon),         /* 0x0007, */
    PTOCBLOCK(HMSetFont),           /* 0x0108, */
    PTOCBLOCK(HMSetFontSize),       /* 0x0109, */
    PTOCBLOCK(HMGetFont),           /* 0x020a, */
    PTOCBLOCK(HMGetFontSize),       /* 0x020b, */
    PTOCBLOCK(HMSetDialogResID),    /* 0x010c, */
    PTOCBLOCK(HMSetMenuResID),      /* 0x020d, */
    PTOCBLOCK(HMBalloonRect),       /* 0x040e, */
    PTOCBLOCK(HMGetMenuResID),      /* 0x040f, */
    PTOCBLOCK(HMScanTemplateItems), /* 0x0410, */
    PTOCBLOCK(HMExtractHelpMsg),    /* 0x0711, */
    PTOCBLOCK(ZERO),                /* 0x0012, */
    PTOCBLOCK(HMGetDialogResID),    /* 0x0213, */
    PTOCBLOCK(HMGetMenuResID),      /* 0x0314, */
    PTOCBLOCK(HMGetBalloonWindow),  /* 0x0215, */
};

PRIVATE selectorblock_t balloon_block[] = {
    {0x00, 0x015, 1, balloon_ptoc},
    {0, 0, 0, 0},
};

STUB(Pack14)
{
    return do_selector_block(balloon_block, EM_D0 & 0xFF, Pack14);
}

PRIVATE ptocblock_t pack15_ptoc[] = {
    PTOCBLOCK(GetPictInfo),      /* 0x00 */
    PTOCBLOCK(GetPixMapInfo),    /* 0x01 */
    PTOCBLOCK(NewPictInfo),      /* 0x02 */
    PTOCBLOCK(RecordPictInfo),   /* 0x03 */
    PTOCBLOCK(RecordPixMapInfo), /* 0x04 */
    PTOCBLOCK(RetrievePictInfo), /* 0x05 */
    PTOCBLOCK(DisposePictInfo),  /* 0x06 */
};

PRIVATE selectorblock_t pack15_block[] = {
    {0x00, 0x06, 1, pack15_ptoc},
    {0, 0, 0, 0},
};

STUB(Pack15)
{
    return do_selector_block(pack15_block, EM_D0 & 0xFF, Pack15);
}

STUB(CommToolboxDispatch)
{
    comm_toolbox_dispatch_args_t* arg_block;
    int selector;

    arg_block = (comm_toolbox_dispatch_args_t*)SYN68K_TO_US(EM_A0);

    selector = CW(arg_block->selector);
    switch (selector) {
    case 0x0402:
        AppendDITL(PPR(arg_block->args.append_args.dp), PPR(arg_block->args.append_args.new_items_h),
                   CW(arg_block->args.append_args.method));
        break;
    case 0x0403:
        EM_D0 = CountDITL(PPR(arg_block->args.count_args.dp));
        break;
    case 0x0404:
        ShortenDITL(PPR(arg_block->args.shorten_args.dp), CW(arg_block->args.shorten_args.n_items));
        break;
    case 1286:
        EM_D0 = CRMGetCRMVersion();
        break;
    case 1282:
        EM_D0 = (LONGINT)US_TO_SYN68K_CHECK0(CRMGetHeader());
        break;
    case 1283:
        CRMInstall(PPR(arg_block->args.crm_args.qp));
        break;
    case 1284:
        EM_D0 = CRMRemove(PPR(arg_block->args.crm_args.qp));
        break;
    case 1285:
        EM_D0 = (LONGINT)US_TO_SYN68K_CHECK0(CRMSearch(PPR(arg_block->args.crm_args.qp)));
        break;
    case 1281:
        EM_D0 = InitCRM();
        break;
    default:
        warning_unimplemented(NULL_STRING); /* now Quicken 6 will limp */
        EM_D0 = 0;
        break;
    }
    RTS();
}

STUB(SysEnvirons)
{
    EM_D0 = SysEnvirons(EM_D0, (SysEnvRecPtr)SYN68K_TO_US_CHECK0(EM_A0));
    RTS();
}

STUB(PostEvent)
{
    HIDDEN_EvQElPtr qelemp;

#warning the first argument to PPostEvent looks suspicious
    EM_D0 = PPostEvent(EM_A0, EM_D0, (HIDDEN_EvQElPtr*)&qelemp);
    EM_A0 = (long)HIDDEN_RAW(qelemp);
    RTS();
}

STUB(FlushEvents)
{
    FlushEvents(EM_D0, EM_D0 >> 16);
    EM_D0 = 0; /* LIE? */
    RTS();
}

STUB(IMVI_DeferUserFn)
{
    syn68k_addr_t function = EM_A0;
    /* Guest memory is resident, so no page fault can require deferral. The
     documented immediate path passes D0's argument to the function in A0. */
    if (!function)
        EM_D0 = paramErr;
    else {
        EM_A0 = EM_D0;
        CALL_EMULATOR(function);
        EM_D0 = noErr;
    }
    RTS();
}

STUB(GetOSEvent)
{
    EM_D0 = GetOSEvent(EM_D0, (EventRecord*)SYN68K_TO_US_CHECK0(EM_A0)) ? 0 : -1;
    RTS();
}

STUB(OSEventAvail)
{
    EM_D0 = OSEventAvail(EM_D0, (EventRecord*)SYN68K_TO_US_CHECK0(EM_A0)) ? 0 : -1;
    RTS();
}

STUB(SlotVInstall)
{
    /* FIXME: for now, ignore the slot argument */
    EM_D0 = SlotVInstall((VBLTaskPtr)SYN68K_TO_US_CHECK0(EM_A0), (INTEGER)EM_D0);
    RTS();
}

STUB(VInstall)
{
    EM_D0 = VInstall((VBLTaskPtr)SYN68K_TO_US_CHECK0(EM_A0));
    RTS();
}

STUB(SlotVRemove)
{
    EM_D0 = SlotVRemove((VBLTaskPtr)SYN68K_TO_US_CHECK0(EM_A0), (INTEGER)EM_D0);
    RTS();
}

STUB(VRemove)
{
    EM_D0 = VRemove((VBLTaskPtr)SYN68K_TO_US_CHECK0(EM_A0));
    RTS();
}

STUB(InsTime)
{
    InsTime((QElemPtr)SYN68K_TO_US_CHECK0(EM_A0));
    EM_D0 = 0;
    RTS();
}

STUB(RmvTime)
{
    RmvTime((QElemPtr)SYN68K_TO_US_CHECK0(EM_A0));
    EM_D0 = 0;
    RTS();
}

STUB(PrimeTime)
{
    PrimeTime((QElemPtr)SYN68K_TO_US_CHECK0(EM_A0), EM_D0);
    EM_D0 = 0;
    RTS();
}

STUB(ReadDateTime)
{
    EM_D0 = ReadDateTime((LONGINT*)SYN68K_TO_US_CHECK0(EM_A0));
    RTS();
}

STUB(SetDateTime)
{
    const uint32 seconds = EM_D0;
    EM_D0 = SetDateTime(seconds);
    if (!EM_D0)
        Time = CL(seconds);
    RTS();
}

STUB(Delay)
{
    LONGINT tempp;

#warning is a0 really the argument to delay?  That sounds weird
    Delay(EM_A0, &tempp);
    EM_D0 = tempp;
    RTS();
}

#define DIACBIT (1 << 9)
#define CASEBIT (1 << 10)

STUB(EqualString)
{
    EM_D0 = !!ROMlib_RelString((unsigned char*)SYN68K_TO_US_CHECK0(EM_A0), (unsigned char*)SYN68K_TO_US_CHECK0(EM_A1),
                               !!(EM_D1 & CASEBIT), !(EM_D1 & DIACBIT), EM_D0);
    RTS();
}

STUB(RelString)
{
    EM_D0 = ROMlib_RelString((unsigned char*)SYN68K_TO_US_CHECK0(EM_A0), (unsigned char*)SYN68K_TO_US_CHECK0(EM_A1),
                             !!(EM_D1 & CASEBIT), !(EM_D1 & DIACBIT), EM_D0);
    RTS();
}

STUB(UprString)
{
    long savea0;

    savea0 = EM_A0;
    ROMlib_UprString((StringPtr)SYN68K_TO_US_CHECK0(EM_A0), !(EM_D1 & DIACBIT), EM_D0);
    EM_A0 = savea0;
    RTS();
}

STUB(StripAddress)
{
    EM_D0 = StripAddress(EM_D0);
    RTS();
}

PRIVATE ptocblock_t prglueptoc[] = {
    PTOCBLOCK(PrOpenDoc),          /* 0 */
    PTOCBLOCK(PrCloseDoc),         /* 1 */
    PTOCBLOCK(PrOpenPage),         /* 2 */
    PTOCBLOCK(PrClosePage),        /* 3 */
    PTOCBLOCK(PrintDefault),       /* 4 */
    PTOCBLOCK(PrStlDialog),        /* 5 */
    PTOCBLOCK(PrJobDialog),        /* 6 */
    PTOCBLOCK(PrStlInit),          /* 7 */
    PTOCBLOCK(PrJobInit),          /* 8 */
    PTOCBLOCK(PrDlgMain),          /* 9 */
    PTOCBLOCK(PrValidate),         /* 10 */
    PTOCBLOCK(PrJobMerge),         /* 11 */
    PTOCBLOCK(PrPicFile),          /* 12 */
    PTOCBLOCK(ZERO),               /* 13 */
    PTOCBLOCK(PrGeneral),          /* 14 */
    PTOCBLOCK(ZERO),               /* 15 */
    PTOCBLOCK(PrDrvrOpen),         /* 16 */
    PTOCBLOCK(PrDrvrClose),        /* 17 */
    PTOCBLOCK(PrDrvrDCE),          /* 18 */
    PTOCBLOCK(PrDrvrVers),         /* 19 */
    PTOCBLOCK(PrCtlCall),          /* 20 */
    PTOCBLOCK(ZERO /*PrPurge*/),   /* 21 */
    PTOCBLOCK(ZERO /*PrNoPurge*/), /* 22 */
    PTOCBLOCK(PrError),            /* 23 */
    PTOCBLOCK(PrSetError),         /* 24 */
    PTOCBLOCK(PrOpen),             /* 25 */
    PTOCBLOCK(PrClose),            /* 26 */
};

PRIVATE selectorblock_t prglueblock[] = {
    {0, 26, 1, prglueptoc},
    {0, 0, 0, 0},
};

PRIVATE long apparent_nop(long unused1, long unused2)
{
    long retval;

    retval = 0;

    warning_unexpected("d0 = 0x%lx", (unsigned long)EM_D0);
    return retval;
}

STUB(PrGlue)
{
    syn68k_addr_t retaddr;
    unsigned long ul;

    retaddr = POPADDR();
    ul = POPUL();
    PUSHADDR(retaddr);
    return do_selector_block(prglueblock, ul >> 27, PrGlue);
}

typedef OSErrRET (*fsprocp_t)(void*, BOOLEAN);

fsprocp_t hfstab[] = {
    (fsprocp_t)apparent_nop,       /* 0 */
    (fsprocp_t)PBOpenWD,           /* 1 */
    (fsprocp_t)PBCloseWD,          /* 2 */
    (fsprocp_t)apparent_nop,       /* 3 */
    (fsprocp_t)apparent_nop,       /* 4 */
    (fsprocp_t)PBCatMove,          /* 5 */
    (fsprocp_t)PBDirCreate,        /* 6 */
    (fsprocp_t)PBGetWDInfo,        /* 7 */
    (fsprocp_t)PBGetFCBInfo,       /* 8 */
    (fsprocp_t)PBGetCatInfo,       /* 9 */
    (fsprocp_t)PBSetCatInfo,       /* 10 */
    (fsprocp_t)PBSetVInfo,         /* 11 */
    (fsprocp_t)apparent_nop,       /* 12 */
    (fsprocp_t)apparent_nop,       /* 13 */
    (fsprocp_t)apparent_nop,       /* 14 */
    (fsprocp_t)apparent_nop,       /* 15 */
    (fsprocp_t)PBLockRange,        /* 0x10 */
    (fsprocp_t)PBUnlockRange,      /* 0x11 */
    (fsprocp_t)apparent_nop,       /* 0x12 */
    (fsprocp_t)apparent_nop,       /* 0x13 */
    (fsprocp_t)PBCreateFileIDRef,  /* 0x14 */
    (fsprocp_t)PBDeleteFileIDRef,  /* 0x15 */
    (fsprocp_t)PBResolveFileIDRef, /* 0x16 */
    (fsprocp_t)PBExchangeFiles,    /* 0x17 */
    (fsprocp_t)PBCatSearch,        /* 0x18 */
    (fsprocp_t)0,                  /* 0x19 */
    (fsprocp_t)PBOpenDF,           /* 0x1A */
    (fsprocp_t)PBMakeFSSpec,       /* 0x1B */
    (fsprocp_t)0,                  /* 0x1C */
    (fsprocp_t)0,                  /* 0x1D */
    (fsprocp_t)0,                  /* 0x1E */
    (fsprocp_t)0,                  /* 0x1F */
    (fsprocp_t)PBDTGetPath,        /* 0x20 */
    (fsprocp_t)PBDTCloseDown,      /* 0x21 */
    (fsprocp_t)PBDTAddIcon,        /* 0x22 */
    (fsprocp_t)PBDTGetIcon,        /* 0x23 */
    (fsprocp_t)PBDTGetIconInfo,    /* 0x24 */
    (fsprocp_t)PBDTAddAPPL,        /* 0x25 */
    (fsprocp_t)PBDTRemoveAPPL,     /* 0x26 */
    (fsprocp_t)PBDTGetAPPL,        /* 0x27 */
    (fsprocp_t)PBDTSetComment,     /* 0x28 */
    (fsprocp_t)PBDTRemoveComment,  /* 0x29 */
    (fsprocp_t)PBDTGetComment,     /* 0x2A */
    (fsprocp_t)PBDTFlush,          /* 0x2B */
    (fsprocp_t)PBDTReset,          /* 0x2C */
    (fsprocp_t)PBDTGetInfo,        /* 0x2D */
    (fsprocp_t)PBDTOpenInform,     /* 0x2E */
    (fsprocp_t)PBDTDelete,         /* 0x2F */
    (fsprocp_t)PBHGetVolParms,     /* 0x30 */
    (fsprocp_t)PBHGetLogInInfo,    /* 0x31 */
    (fsprocp_t)PBHGetDirAccess,    /* 0x32 */
    (fsprocp_t)PBHSetDirAccess,    /* 0x33 */
    (fsprocp_t)PBHMapID,           /* 0x34 */
    (fsprocp_t)PBHMapName,         /* 0x35 */
    (fsprocp_t)PBHCopyFile,        /* 0x36 */
    (fsprocp_t)PBHMoveRename,      /* 0x37 */
    (fsprocp_t)OpenDeny,           /* 0x38 */
    (fsprocp_t)OpenRFDeny,         /* 0x39 */
#if 0
    (fsprocp_t) 0, /* 0x3A */
    (fsprocp_t) 0, /* 0x3b */
    (fsprocp_t) 0, /* 0x3c */
    (fsprocp_t) 0, /* 0x3d */
    (fsprocp_t) 0, /* 0x3e */
    (fsprocp_t) PBGetVolMountInfoSize, /* 0x3f */
    (fsprocp_t) PBGetVolMountInfo, /* 0x40 */
    (fsprocp_t) 0, /* 0x41 */
    (fsprocp_t) 0, /* 0x42 */
    (fsprocp_t) 0, /* 0x43 */
    (fsprocp_t) 0, /* 0x44 */
    (fsprocp_t) 0, /* 0x45 */
    (fsprocp_t) 0, /* 0x46 */
    (fsprocp_t) 0, /* 0x47 */
    (fsprocp_t) 0, /* 0x48 */
    (fsprocp_t) 0, /* 0x49 */
    (fsprocp_t) 0, /* 0x4a */
    (fsprocp_t) 0, /* 0x4b */
    (fsprocp_t) 0, /* 0x4c */
    (fsprocp_t) 0, /* 0x4d */
    (fsprocp_t) 0, /* 0x4e */
    (fsprocp_t) 0, /* 0x4f */
    (fsprocp_t) 0, /* 0x50 */
    (fsprocp_t) 0, /* 0x51 */
    (fsprocp_t) 0, /* 0x52 */
    (fsprocp_t) 0, /* 0x53 */
    (fsprocp_t) 0, /* 0x54 */
    (fsprocp_t) 0, /* 0x55 */
    (fsprocp_t) 0, /* 0x56 */
    (fsprocp_t) 0, /* 0x57 */
    (fsprocp_t) 0, /* 0x58 */
    (fsprocp_t) 0, /* 0x59 */
    (fsprocp_t) 0, /* 0x5a */
    (fsprocp_t) 0, /* 0x5b */
    (fsprocp_t) 0, /* 0x5c */
    (fsprocp_t) 0, /* 0x5d */
    (fsprocp_t) 0, /* 0x5e */
    (fsprocp_t) 0, /* 0x5f */
    (fsprocp_t) PBGetForeignPrivs, /* 0x60 */
    (fsprocp_t) PBSetForeignPrivs, /* 0x61 */
#endif
};

#define ASYNCBIT (1 << 10)
#define HFSBIT (1 << 9)

STUB(HFSDispatch)
{
    fsprocp_t vp;

    if ((EM_D0 & 0xFFFF) == 0x41) {
        ParmBlkPtr pb = ROMlib_mactcp_memory(EM_A0, sizeof(ioParam), TRUE);
        EM_D0 = pb ? ROMlib_afp_volume_mount(pb, !!(EM_D1 & ASYNCBIT)) : paramErr;
        RTS();
    }

    if ((EM_D0 & 0xFFFF) >= NELEM(hfstab)) {
        warning_unexpected("d0 = 0x%lx", (unsigned long)EM_D0);
        EM_D0 = paramErr;
    }
    else {
        if (((EM_D0 & 0xFFFF) == 0x1A) && (EM_D1 & 0x200) == 0)
            vp = (fsprocp_t)PBOpen;
        else
            vp = hfstab[(EM_D0 & 0xFFFF)];
        if (vp)
            EM_D0 = (*vp)(SYN68K_TO_US_CHECK0(EM_A0), !!(EM_D1 & ASYNCBIT));
        else {
            warning_unexpected("d0 = 0x%lx", (unsigned long)EM_D0);
            EM_D0 = paramErr;
        }
    }
    RTS();
}

#if defined(NEWSTUBS)
{
    AppendDITL,  /* 0x402 */
    CountDITL,   /* 0x403 */
    ShortenDITL, /* 0x404 */
};

{
    DebuggerGetMax,       /* 0x0 */
    DebuggerEnter,        /* 0x1 */
    DebuggerExit,         /* 0x2 */
    DebuggerPoll,         /* 0x3 */
    GetPageState,         /* 0x4 */
    PageFaultFatal,       /* 0x5 */
    DebuggerLockMemory,   /* 0x6 */
    DebuggerUnlockMemory, /* 0x7 */
    EnterSupervisorMode,  /* 0x8 */
};

{
    GetCPUSpeed, /* 0xffff */
    EnableIdle,  /* 0x0000 */
    DisableIdle, /* 0x0001 */
};

{
    HoldMemory,           /* 0 */
    UnholdMemory,         /* 1 */
    LockMemory,           /* 2 */
    UnlockMemory,         /* 3 */
    LockMemoryCongiguous, /* 4 */
    GetPhysical,          /* 5 */
};
#endif

STUB(FInitQueue)
{
    RTS();
}

STUB(HFSRoutines)
{
    typedef OSErrRET (*fsprocp_t)(void*, BOOLEAN);
    fsprocp_t vp;

    vp = (fsprocp_t)((EM_D1 & HFSBIT) ? ignoreme2[1] : ignoreme2[0]);
    EM_D0 = (*vp)(SYN68K_TO_US_CHECK0(EM_A0), !!(EM_D1 & ASYNCBIT));
    RTS();
}

#define GETTRAPNEWBIT (1 << 9)
#define GETTRAPTOOLBIT (1 << 10)

#define UNIMPLEMENTEDINDEX (0x9F)

PRIVATE long istool(uint32* d0p, uint32 d1)
{
    long retval;

    if (d1 & GETTRAPNEWBIT) {
        retval = d1 & GETTRAPTOOLBIT;
        if (retval)
            *d0p &= 0x3FF;
        else
            *d0p &= 0xFF;
    }
    else {
        *d0p &= 0x1FF;
        retval = (*d0p > 0x4F) && (*d0p != 0x54) && (*d0p != 0x57);
    }
    return retval;
}

PRIVATE long unimplementedos(long d0)
{
    long retval;

    switch (d0) {
    case 0x77: /* CountADBs */
    case 0x78: /* GetIndADB */
    case 0x79: /* GetADBInfo */
    case 0x7A: /* SetADBInfo */
    case 0x7B: /* ADBReInit */
    case 0x7C: /* ADBOp */
    case 0x4F: /* RDrvrInstall */
        retval = 1;
        break;
    case 0x8B:                                   /* Communications Toolbox */
        retval = ROMlib_creator == TICK("KR09"); /* kermit */
        break;
    default:
        retval = 0;
        break;
    }
    return retval;
}

PRIVATE long unimplementedtool(long d0)
{
    long retval;

    switch (d0) {
    case 0x00: /* SoundDispatch -- if sound is off, soundispatch is unimpl */
        retval = ROMlib_PretendSound == soundoff;
        break;
    case 0x8F: /* OSDispatch (Word uses old, undocumented selectors) */
        retval = system_version < 0x700;
        break;
    case 0x30: /* Pack14 */
        retval = 1;
        break;
    case 0xB5: /* ScriptUtil */
        retval = ROMlib_pretend_script ? 0 : 1;
        break;
    case 0x3ED: /* DragManagerDispatch has native PPC services, but no 68K dispatcher. */
        retval = tooltraptable[d0] == toolstuff[d0].orig;
        break;
    default:
        retval = 0;
        break;
    }
    return retval;
}

/*
 * Cheezoid implementation, but we don't have to worry about running out
 * of memory and if we have more than 10 traps displaying them all doesn't
 * buy us much, anyway
 */

PRIVATE uint16 bad_traps[10];
PRIVATE int n_bad_traps = 0;

PUBLIC void ROMlib_reset_bad_trap_addresses(void)
{
    n_bad_traps = 0;
}

PRIVATE void add_to_bad_trap_addresses(boolean_t tool_p, unsigned short index)
{
    int i;
    uint16 aline_trap;

    aline_trap = 0xA000 + index;
    if (tool_p)
        aline_trap += 0x800;

    for (i = 0; i < n_bad_traps && bad_traps[i] != aline_trap; ++i)
        ;
    if (i >= n_bad_traps) {
        bad_traps[n_bad_traps % NELEM(bad_traps)] = aline_trap;
        ++n_bad_traps;
    }
}

STUB(bad_trap_unimplemented)
{
    char buf[1024];

    /* TODO: more */
    switch (mostrecenttrap) {
    default:
        sprintf(buf, "Fatal error.\r"
                     "Jumped to unimplemented trap handler, "
                     "probably by getting the address of one of these traps: [");
        {
            int i;
            boolean_t need_comma_p;

            need_comma_p = FALSE;
            for (i = 0; i < (int)NELEM(bad_traps) && i < n_bad_traps; ++i) {
                if (need_comma_p)
                    strcat(buf, ",");
                {
                    char trap_buf[7];
                    sprintf(trap_buf, "0x%04x", bad_traps[i]);
                    gui_assert(trap_buf[6] == 0);
                    strcat(buf, trap_buf);
                }
                need_comma_p = TRUE;
            }
        }
        strcat(buf, "].");
        system_error(buf, 0, "Restart", NULL, NULL, NULL, NULL, NULL);
        break;
    }

    ExitToShell();
    return /* dummy */ -1;
}

PUBLIC void ROMlib_GetTrapAddress_helper(uint32* d0p, uint32 d1, uint32* a0p)
{
    boolean_t tool_p;

    if (*d0p == READUL((syn68k_addr_t)0xA198))
        *d0p = 0xA198;
    if (*d0p == READUL((syn68k_addr_t)0xA89F))
        *d0p = 0xA89F;
    tool_p = istool(d0p, d1);
    if (tool_p) {
        *a0p = (long)(tooltraptable[unimplementedtool(*d0p) ? UNIMPLEMENTEDINDEX : *d0p]);
    }
    else {
        *a0p = (long)(unimplementedos(*d0p) ? tooltraptable[UNIMPLEMENTEDINDEX] : ostraptable[*d0p]);
    }
    if (*a0p == (uint32)tooltraptable[UNIMPLEMENTEDINDEX]) {
        add_to_bad_trap_addresses(tool_p, *d0p);
        *a0p = (long)US_TO_SYN68K_CHECK0(P_bad_trap_unimplemented);
    }
    *d0p = 0;
}

STUB(GetTrapAddress)
{
    ROMlib_GetTrapAddress_helper(&EM_D0, EM_D1, &EM_A0);
    RTS();
}

STUB(SetTrapAddress)
{
    void** tablep;

    if (istool(&EM_D0, EM_D1))
        tablep = (void**)tooltraptable;
    else
        tablep = (void**)ostraptable;
    tablep[EM_D0] = (void*)EM_A0;

    if (EM_D0 != 0xED) /* Temporary MacWrite hack */
        ROMlib_destroy_blocks(0, ~0, TRUE);
    EM_D0 = 0;
    RTS();
}

STUB(Gestalt)
{
    LONGINT l;
    ProcPtr oldp;

    switch (EM_D1 & 0xFFFF) {
    case 0xA1AD:
    default:
        l = 0;
        EM_D0 = Gestalt(EM_D0, &l);
        EM_A0 = CL(l);
        break;
    case 0xA3AD:
        if (EM_D0 == DONGLE_GESTALT)
            EM_D0 = Gestalt(EM_D0, (LONGINT*)SYN68K_TO_US_CHECK0(EM_A0));
        else
            EM_D0 = NewGestalt(EM_D0, (ProcPtr)SYN68K_TO_US_CHECK0(EM_A0));
        break;
    case 0xA5AD:
        EM_D0 = ReplaceGestalt(EM_D0, (ProcPtr)SYN68K_TO_US_CHECK0(EM_A0), &oldp);
        EM_A0 = (LONGINT)US_TO_SYN68K_CHECK0(oldp);
        break;
    case 0xA7AD:
        gui_abort();
        /* GetGestaltProcPtr(); no docs on this call */
        break;
    }
    RTS();
}

/* unlike the 68k version, every unknown trap gets vectored to
   `Unimplemented ()' */

/* Package 1 is supplied by applications such as ResEdit. Its selector and
   arguments belong to the guest package, rather than a native Toolbox ABI. */
STUB(Pack1)
{
    M68kReg saved_regs[16];
    Handle package;
    SignedByte state;
    syn68k_addr_t retaddr;
    memcpy(saved_regs, cpu_state.regs, sizeof saved_regs);
    package = GetResource(TICK("PACK"), 1);
    if (package)
        LoadResource(package);
    if (!package || !STARH(package) || GetHandleSize(package) < 2) {
        memcpy(cpu_state.regs, saved_regs, sizeof saved_regs);
        return _Unimplemented(ignoreme, ignoreme2);
    }
    state = HGetState(package);
    HLock(package);
    memcpy(cpu_state.regs, saved_regs, sizeof saved_regs);
    retaddr = POPADDR();
    CALL_EMULATOR((syn68k_addr_t)US_TO_SYN68K(STARH(package)));
    HSetState(package, state);
    return retaddr;
}

STUB(Unimplemented)
{
    char buf[1024];

    switch (mostrecenttrap) {
    default:
        sprintf(buf,
                "Fatal error.\r"
                "encountered unknown, unimplemented trap `%X'.",
                mostrecenttrap);
        system_error(buf, 0, "Restart", NULL, NULL, NULL, NULL, NULL);
        break;
    }

    ExitToShell();
    RTS(); /* in case we want to get return from within gdb */
    return /* dummy */ -1;
}

/*
 * This is just to trick out NIH Image... it's really not supported
 */

/* #warning SlotManager not properly implemented */

STUB(SlotManager)
{
    EM_D0 = -300; /* smEmptySlot */
    RTS();
}

STUB(WackyQD32Trap)
{
    gui_fatal("This trap shouldn't be called");
}

STUB(HGetState)
{
    EM_D0 = HGetState((Handle)SYN68K_TO_US_CHECK0(EM_A0));
    RTS();
}

STUB(HSetState)
{
    HSetState((Handle)SYN68K_TO_US_CHECK0(EM_A0), EM_D0);
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(HLock)
{
    HLock((Handle)SYN68K_TO_US_CHECK0(EM_A0));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(HUnlock)
{
    HUnlock((Handle)SYN68K_TO_US_CHECK0(EM_A0));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(HPurge)
{
    HPurge((Handle)SYN68K_TO_US_CHECK0(EM_A0));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(HNoPurge)
{
    HNoPurge((Handle)SYN68K_TO_US_CHECK0(EM_A0));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(HSetRBit)
{
    HSetRBit((Handle)SYN68K_TO_US_CHECK0(EM_A0));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(HClrRBit)
{
    HClrRBit((Handle)SYN68K_TO_US_CHECK0(EM_A0));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(InitApplZone)
{
    InitApplZone();
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(SetApplBase)
{
    SetApplBase((Ptr)SYN68K_TO_US_CHECK0(EM_A0));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(MoreMasters)
{
    MoreMasters();
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(InitZone)
{
    initzonehiddenargs_t* ip;

    ip = (initzonehiddenargs_t*)SYN68K_TO_US(EM_A0);
    InitZone(PPR(ip->pGrowZone), CW(ip->cMoreMasters), PPR(ip->limitPtr), PPR(ip->startPtr));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(SetZone)
{
    SetZone((THz)SYN68K_TO_US_CHECK0(EM_A0));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(DisposHandle)
{
    DisposHandle((Handle)SYN68K_TO_US_CHECK0(EM_A0));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(GetHandleSize)
{
    EM_D0 = GetHandleSize((Handle)SYN68K_TO_US_CHECK0(EM_A0));
    if (CW(MemErr) < 0)
        EM_D0 = CW(MemErr);
    RTS();
}

STUB(SetHandleSize)
{
    SetHandleSize((Handle)SYN68K_TO_US_CHECK0(EM_A0), EM_D0);
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(ReallocHandle)
{
    ReallocHandle((Handle)SYN68K_TO_US_CHECK0(EM_A0), EM_D0);
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(DisposPtr)
{
    DisposPtr((Ptr)SYN68K_TO_US_CHECK0(EM_A0));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(GetPtrSize)
{
    EM_D0 = GetPtrSize((Ptr)SYN68K_TO_US_CHECK0(EM_A0));
    if (CW(MemErr) < 0)
        EM_D0 = CW(MemErr);
    RTS();
}

STUB(SetPtrSize)
{
    SetPtrSize((Ptr)SYN68K_TO_US_CHECK0(EM_A0), EM_D0);
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(FreeMem)
{
    EM_D0 = _FreeMem_flags(SYS_P(EM_D1, 0xA01C));
    RTS();
}

STUB(CompactMem)
{
    EM_D0 = _CompactMem_flags(EM_D0, SYS_P(EM_D1, 0xA04C));
    RTS();
}

STUB(ResrvMem)
{
    _ResrvMem_flags(EM_D0, SYS_P(EM_D1, 0xA040));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(PurgeMem)
{
    _PurgeMem_flags(EM_D0, SYS_P(EM_D1, 0xA04D));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(BlockMove)
{
    BlockMove_the_trap((Ptr)SYN68K_TO_US_CHECK0(EM_A0), (Ptr)SYN68K_TO_US_CHECK0(EM_A1), EM_D0, !(EM_D1 & 0x200));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(MaxApplZone)
{
    MaxApplZone();
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(MoveHLow)
{
    ROMlib_MoveHLow((Handle)SYN68K_TO_US_CHECK0(EM_A0));
    EM_D0 = (int16)CW(MemErr);
    RTS();
}

STUB(MoveHHi)
{
    MoveHHi((Handle)SYN68K_TO_US_CHECK0(EM_A0));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(MaxBlock)
{
    EM_D0 = _MaxBlock_flags(SYS_P(EM_D1, 0xA061));
    RTS();
}

STUB(StackSpace)
{
    EM_D0 = StackSpace();
    RTS();
}

STUB(SetApplLimit)
{
    SetApplLimit((Ptr)SYN68K_TO_US_CHECK0(EM_A0));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(SetGrowZone)
{
    SetGrowZone((ProcPtr)SYN68K_TO_US_CHECK0(EM_A0));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(GetZone)
{
    EM_A0 = (uint32)US_TO_SYN68K_CHECK0(GetZone());
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(NewEmptyHandle)
{
    EM_A0 = (uint32)US_TO_SYN68K_CHECK0(_NewEmptyHandle_flags(SYS_P(EM_D1, 0xA166)));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(NewHandle)
{
    /* #### d1 options */

    EM_A0 = (uint32)US_TO_SYN68K_CHECK0(_NewHandle_flags(EM_D0, SYS_P(EM_D1, 0xA122), CLEAR_P(EM_D1, 0xA122)));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(HandleZone)
{
    EM_A0 = (uint32)US_TO_SYN68K_CHECK0(HandleZone((Handle)SYN68K_TO_US_CHECK0(EM_A0)));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(RecoverHandle)
{
    EM_A0 = (uint32)US_TO_SYN68K_CHECK0(_RecoverHandle_flags((Ptr)SYN68K_TO_US_CHECK0(EM_A0), SYS_P(EM_D1, 0xA128)));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(NewPtr)
{
    EM_A0 = (uint32)US_TO_SYN68K_CHECK0(_NewPtr_flags(EM_D0, SYS_P(EM_D1, 0xA11E), CLEAR_P(EM_D1, 0xA11E)));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(PtrZone)
{
    EM_A0 = (uint32)US_TO_SYN68K_CHECK0(PtrZone((Ptr)SYN68K_TO_US_CHECK0(EM_A0)));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(MaxMem)
{
    EM_D0 = _MaxMem_flags((Size*)&EM_A0, SYS_P(EM_D1, 0xA11D));
    RTS();
}

STUB(PurgeSpace)
{
    int32 total, contig;

    _PurgeSpace_flags(&total, &contig, SYS_P(EM_D1, 0xA062));
    EM_D0 = total;
    EM_A0 = contig;
    RTS();
}

STUB(EmptyHandle)
{
    EmptyHandle((Handle)SYN68K_TO_US_CHECK0(EM_A0));
    EM_D0 = CW(MemErr);
    RTS();
}

STUB(WriteParam)
{
    EM_D0 = WriteParam();
    RTS();
}

STUB(InitUtil)
{
    EM_D0 = InitUtil();
    RTS();
}

STUB(flushcache)
{
    flushcache();
    RTS();
}

STUB(Microseconds)
{
    unsigned long ms = msecs_elapsed();
    EM_D0 = ms * 1000;
    EM_A0 = ((uint64_t)ms * 1000) >> 32;
    RTS();
}

enum { power_unimplemented = -4 };

typedef struct {
    uint32 time;
    BOOLEAN enabled;
} power_timer_preference_t;

/* Power Manager preferences belong to the emulated machine. Keeping a timeout
   configured does not confer the corresponding portable hardware feature. */
static struct {
    power_timer_preference_t wakeup_timer;
    power_timer_preference_t startup_timer;
    uint8 sleep_timeout;
    uint8 disk_timeout;
    uint8 dim_timeout;
    BOOLEAN automatic_sleep_disabled;
    BOOLEAN spindown_disabled;
    BOOLEAN dimming_disabled;
    BOOLEAN dimming_suspended;
} power_preferences;

STUB(IMVI_IdleUpdate)
{
    if (EM_D1 == 0xa285) {
        const uint32 ticks = CL(Ticks_UL.u);
        ROMlib_power_note_activity(ROMlib_power_overall_activity, ticks);
        EM_D0 = ticks;
    }
    else
        EM_D0 = power_unimplemented;
    RTS();
}

static int16 power_timer_preference(power_timer_preference_t* timer, syn68k_addr_t record, bool setting)
{
    if (!record)
        return paramErr;
    if (setting) {
        timer->time = READUL(record);
        timer->enabled = READUB(record + 4) != 0;
    }
    else {
        WRITEUL(record, timer->time);
        WRITEUB(record + 4, timer->enabled);
    }
    return noErr;
}

STUB(PowerDispatch)
{
    const uint8 argument = EM_D0 >> 16;
    switch ((uint16)EM_D0) {
    case 0x01: /* PMFeatures: fixed-clock desktop board, with no portable power hardware. */
    case 0x0c: /* GetScaledBatteryInfo: empty flag, warning and level bytes. */
    case 0x0e: /* GetIntModemInfo: no modem or modem state flags. */
    case 0x13: /* SetProcessorSpeed: FALSE when the speed did not change. */
    case 0x18: /* IsProcessorCyclingEnabled. */
    case 0x1a: /* BatteryCount: no internal battery slots. */
    case 0x1b: /* GetBatteryVoltage: 0.0 when the requested battery is unavailable. */
        EM_D0 = 0;
        break;
    case 0x36: /* MinimumProcessorSpeed. */
    case 0x10: /* MaximumProcessorSpeed. */
    case 0x11: /* CurrentProcessorSpeed. */
        EM_D0 = ROMLIB_PPC_CORE_MHZ;
        break;
    case 0x12: /* FullProcessorSpeed: the next restart uses the fixed clock. */
        EM_D0 = TRUE;
        break;
    case 0x02: /* GetSleepTimeout */
        EM_D0 = power_preferences.sleep_timeout;
        break;
    case 0x03: /* SetSleepTimeout */
        power_preferences.sleep_timeout = argument;
        break;
    case 0x04: /* GetHardDiskTimeout */
        EM_D0 = power_preferences.disk_timeout;
        break;
    case 0x05: /* SetHardDiskTimeout */
        power_preferences.disk_timeout = argument;
        break;
    case 0x08: /* IsSpindownDisabled */
        EM_D0 = power_preferences.spindown_disabled;
        break;
    case 0x09: /* SetSpindownDisable */
        power_preferences.spindown_disabled = argument != 0;
        break;
    case 0x0d: /* AutoSleepControl */
        power_preferences.automatic_sleep_disabled = argument == 0;
        break;
    case 0x16: /* GetWakeupTimer */
    case 0x17: /* SetWakeupTimer */
        EM_D0 = power_timer_preference(&power_preferences.wakeup_timer, EM_A0, (uint16)EM_D0 == 0x17);
        break;
    case 0x26: /* GetStartupTimer */
    case 0x27: /* SetStartupTimer */
        EM_D0 = power_timer_preference(&power_preferences.startup_timer, EM_A0, (uint16)EM_D0 == 0x27);
        break;
    case 0x1c: /* Unavailable BatteryTimeRec measurements are zero. */
        if (EM_A0)
            for (unsigned field = 0; field < 4; ++field)
                WRITEUL(EM_A0 + field * sizeof(uint32), 0);
        break;
    case 0x1d: /* GetDimmingTimeout */
        EM_D0 = power_preferences.dim_timeout;
        break;
    case 0x1e: /* SetDimmingTimeout */
        power_preferences.dim_timeout = argument;
        break;
    case 0x1f: /* DimmingControl */
        power_preferences.dimming_disabled = argument == 0;
        break;
    case 0x20: /* IsDimmingControlDisabled */
        EM_D0 = power_preferences.dimming_disabled;
        break;
    case 0x21: /* IsAutoSlpControlDisabled */
        EM_D0 = power_preferences.automatic_sleep_disabled;
        break;
    case 0x24: {
        const unsigned activity = argument;
        if (activity < ROMlib_power_activity_count) {
            ROMlib_power_note_activity(activity, CL(Ticks_UL.u));
            EM_D0 = noErr;
        }
        else
            EM_D0 = paramErr;
        break;
    }
    case 0x28: {
        /* ActivityInfo contains a short followed by a two-byte-aligned long. */
        const unsigned activity = EM_A0 ? READUW(EM_A0) : ROMlib_power_activity_count;
        if (activity < ROMlib_power_activity_count) {
            WRITEUL(EM_A0 + 2, ROMlib_power_last_activity(activity));
            EM_D0 = noErr;
        }
        else
            EM_D0 = paramErr;
        break;
    }
    case 0x2b: /* GetDimSuspendState */
        EM_D0 = power_preferences.dimming_suspended;
        break;
    case 0x2c: /* SetDimSuspendState */
        power_preferences.dimming_suspended = argument != 0;
        break;
    default:
        EM_D0 = power_unimplemented;
        break;
    }
    ADJUST_CC_BASED_ON_D0();
    RTS();
}

PRIVATE selector_table_entry_t icon_dispatch_table[] = {
    {0x0207, PTOCBLOCK(NewIconSuite)},     {0x0217, PTOCBLOCK(GetSuiteLabel)},    {0x0302, PTOCBLOCK(DisposeIconSuite)},
    {0x0316, PTOCBLOCK(SetSuiteLabel)},    {0x0419, PTOCBLOCK(GetIconCacheData)}, {0x041A, PTOCBLOCK(SetIconCacheData)},
    {0x041B, PTOCBLOCK(GetIconCacheProc)}, {0x041C, PTOCBLOCK(SetIconCacheProc)}, {0x0500, PTOCBLOCK(PlotIconID)},
    {0x0501, PTOCBLOCK(GetIconSuite)},     {0x050B, PTOCBLOCK(GetLabel)},         {0x0603, PTOCBLOCK(PlotIconSuite)},
    {0x0604, PTOCBLOCK(MakeIconCache)},    {0x0606, PTOCBLOCK(LoadIconCache)},    {0x0608, PTOCBLOCK(AddIconToSuite)},
    {0x0609, PTOCBLOCK(GetIconFromSuite)}, {0x060D, PTOCBLOCK(PtInIconID)},       {0x0610, PTOCBLOCK(RectInIconID)},
    {0x0613, PTOCBLOCK(IconIDToRgn)},      {0x061D, PTOCBLOCK(PlotIconHandle)},   {0x061E, PTOCBLOCK(PlotSICNHandle)},
    {0x061F, PTOCBLOCK(PlotCIconHandle)},  {0x070E, PTOCBLOCK(PtInIconSuite)},    {0x0711, PTOCBLOCK(RectInIconSuite)},
    {0x0714, PTOCBLOCK(IconSuiteToRgn)},   {0x080A, PTOCBLOCK(ForEachIconDo)},    {0x0805, PTOCBLOCK(PlotIconMethod)},
    {0x090F, PTOCBLOCK(PtInIconMethod)},   {0x0912, PTOCBLOCK(RectInIconMethod)}, {0x0915, PTOCBLOCK(IconMethodToRgn)},
};

STUB(IconDispatch)
{
    /* Private System 7 icon-device selectors. ROMAW has one screen; NULL
     restores automatic device selection, which resolves to that screen. */
    if ((EM_D0 & 0xFFFF) == 0x0218) {
        syn68k_addr_t ret = POPADDR();
        (void)POPUL();
        return ret;
    }
    if ((EM_D0 & 0xFFFF) == 0x0220) {
        syn68k_addr_t ret = POPADDR();
        HIDDEN_GDHandle* device = SYN68K_TO_US_CHECK0(POPUL());
        if (device)
            HIDDEN_RAW_SET(*(device), MainDevice);
        return ret;
    }
    return do_selector_table(EM_D0 & 0xFFFF, icon_dispatch_table, NELEM(icon_dispatch_table), NULL, IconDispatch);
}

#warning should include speech manager selectors
/*
 * NOTE: IM Sound p. 4-109 has a table of speech manager selectors:
 * 
 * 0x0000000c SpeechManagerVersion
 * 0x003c000c SpeechBusy
 * 0x0040000c SpeechBusySystemWide
 * 0x0108000c CountVoices
 * 0x021c000c DisposeSpeechChannel
 * 0x0220000c SpeakString
 * 0x022c000c StopSpeech
 * 0x0238000c ContinueSpeech
 * 0x030c000c GetIndVoice
 * 0x0418000c NewSpeechChannel
 * 0x0430000c StopSpeechAt
 * 0x0434000c PauseSpeechAt
 * 0x0444000c SetSpeechRate
 * 0x0448000c GetSpeechRate
 * 0x044c000c SetSpeechPitch
 * 0x0450000c GetSpeechPitch
 * 0x0460000c UseDictionary
 * 0x0604000c MakeVoiceSpec
 * 0x0610000c GetVoiceDescription
 * 0x0614000c GetVoiceInfo
 * 0x0624000c SpeakText
 * 0x0654000c SetSpeechInfo
 * 0x0658000c GetSpeechInfo
 * 0x0828000c SpeakBuffer
 * 0x0a5c000c TextToPhonemes
 */

PRIVATE selector_table_entry_t sound_table[] = {
    {0x00000000, PTOCBLOCK(FinaleUnknown1)},
    {0x00000004, PTOCBLOCK(DirectorUnknown3)},
    {0x00000010, PTOCBLOCK(MACEVersion)},
    {0x00000014, PTOCBLOCK(SPBVersion)},
    {0x00040004, PTOCBLOCK(FinaleUnknown2)},
    {0x00040010, PTOCBLOCK(Comp3to1)},
    {0x00080010, PTOCBLOCK(Exp1to3)},
    {0x000C0008, PTOCBLOCK(SndSoundManagerVersion)},
    {0x000C0010, PTOCBLOCK(Comp6to1)},
    {0x00100008, PTOCBLOCK(SndChannelStatus)}, /* may be wrong */
    {0x00100010, PTOCBLOCK(Exp1to6)},
    {0x00140008, PTOCBLOCK(SndManagerStatus)},   /* may be wrong */
    {0x00180008, PTOCBLOCK(SndGetSysBeepState)}, /* may be wrong */
    {0x001C0004, PTOCBLOCK(DirectorUnknown4)},
    {0x001C0008, PTOCBLOCK(SndSetSysBeepState)},  /* may be wrong */
    {0x00200008, PTOCBLOCK(SndPlayDoubleBuffer)}, /* may be wrong */
    {0x01100014, PTOCBLOCK(SPBSignOutDevice)},
    {0x011C0008, PTOCBLOCK(SndSetSysBeepState)},
    {0x02040008, PTOCBLOCK(SndPauseFilePlay)},
    {0x02180008, PTOCBLOCK(SndGetSysBeepState)},
    {0x021C0014, PTOCBLOCK(SPBCloseDevice)},
    {0x02240018, PTOCBLOCK(GetSysBeepVolume)},
    {0x02240024, PTOCBLOCK(GetSysBeepVolume)},
    {0x02280014, PTOCBLOCK(SPBPauseRecording)},
    {0x02280018, PTOCBLOCK(SetSysBeepVolume)},
    {0x02280024, PTOCBLOCK(SetSysBeepVolume)},
    {0x022C0014, PTOCBLOCK(SPBResumeRecording)},
    {0x022C0018, PTOCBLOCK(GetDefaultOutputVolume)},
    {0x022C0024, PTOCBLOCK(GetDefaultOutputVolume)},
    {0x02300014, PTOCBLOCK(SPBStopRecording)},
    {0x02300018, PTOCBLOCK(SetDefaultOutputVolume)},
    {0x02300024, PTOCBLOCK(SetDefaultOutputVolume)},
    {0x03080008, PTOCBLOCK(SndStopFilePlay)},
    {0x030C0014, PTOCBLOCK(SPBSignInDevice)},
    {0x03140008, PTOCBLOCK(SndManagerStatus)},
    {0x03200014, PTOCBLOCK(SPBRecord)},
    {0x04040018, PTOCBLOCK(GetSoundHeaderOffset)},
    {0x04200008, PTOCBLOCK(SndPlayDoubleBuffer)},
    {0x04240014, PTOCBLOCK(SPBRecordToFile)},
    {0x04400014, PTOCBLOCK(SPBMillisecondsToBytes)},
    {0x04440014, PTOCBLOCK(SPBBytesToMilliseconds)},
    {0x044C0018, {ROMlib_get_sound_compression_name, 0x16a}}, /* Pascal: OSErr (OSType, Str255). */
    {0x05100008, PTOCBLOCK(SndChannelStatus)},
    {0x05140014, PTOCBLOCK(SPBGetIndexedDevice)},
    {0x05180014, PTOCBLOCK(SPBOpenDevice)},
    {0x060C0018, PTOCBLOCK(UnsignedFixedMulDiv)},
    {0x06340018, PTOCBLOCK(SetSoundPreference)},
    {0x06380014, PTOCBLOCK(SPBGetDeviceInfo)},
    {0x06380018, PTOCBLOCK(GetSoundPreference)},
    {0x063C0014, PTOCBLOCK(SPBSetDeviceInfo)},
    {0x063C0018, PTOCBLOCK(SndGetInfo)},
    {0x06400018, PTOCBLOCK(SndSetInfo)},
    {0x06440018, PTOCBLOCK(GetSoundOutputInfo)},
    {0x06480018, PTOCBLOCK(SetSoundOutputInfo)},
    {0x07080014, PTOCBLOCK(SndRecordToFile)},
    {0x07100018, PTOCBLOCK(GetCompressionInfo)},
    {0x07580014, {ROMlib_parse_aiff_header, 0x2b6a}},
    {0x08040014, PTOCBLOCK(SndRecord)},
    {0x085C0014, {ROMlib_parse_snd_header, 0x5b6a}},
    {0x0B4C0014, PTOCBLOCK(SetupAIFFHeader)},
    {0x0D000008, PTOCBLOCK(SndStartFilePlay)},
    {0x0D480014, PTOCBLOCK(SetupSndHeader)},
    {0x0E340014, PTOCBLOCK(SPBGetRecordingStatus)},
};

STUB(SoundDispatch)
{
    uint32 return_address;
    if (ROMlib_speech_dispatch(EM_D0, &return_address))
        return return_address;
    return do_selector_table(EM_D0, sound_table, NELEM(sound_table), NULL, SoundDispatch);
}

STUB(IMVI_ReadXPRam)
{
    if (EM_D0 == ROMLIB_MACHINE_LOCATION_XPRAM && EM_A0)
        ROMlib_read_machine_location(SYN68K_TO_US(EM_A0));
    RTS();
}

STUB(IMVI_WriteXPRam)
{
    if (EM_D0 == ROMLIB_MACHINE_LOCATION_XPRAM && EM_A0)
        ROMlib_write_machine_location(SYN68K_TO_US(EM_A0));
    RTS();
}

/* These are the QuickTime routines called by Quicken Preview from Quicken 6
   Deluxe, which is going to be our first QT 3.0 test application */

PRIVATE selector_table_entry_t qt_table[] = {
    {1, PTOCBLOCK(EnterMovies)},
    {2, PTOCBLOCK(ExitMovies)},
    {3, PTOCBLOCK(GetMoviesError)},
    {4, PTOCBLOCK(GetMoviesStickyError)},
    {5, PTOCBLOCK(MoviesTask)},
    {6, PTOCBLOCK(PrerollMovie)},
    {9, PTOCBLOCK(SetMovieActive)},
    {11, PTOCBLOCK(StartMovie)},
    {12, PTOCBLOCK(StopMovie)},
    {13, PTOCBLOCK(GoToBeginningOfMovie)},
    {22, PTOCBLOCK(SetMovieGWorld)},
    {31, PTOCBLOCK(UpdateMovie)},
    {35, PTOCBLOCK(DisposeMovie)},
    {46, PTOCBLOCK(GetMovieVolume)},
    {213, PTOCBLOCK(CloseMovieFile)},
    {221, PTOCBLOCK(IsMovieDone)},
    {222, PTOCBLOCK(ClearMoviesStickyError)},
    {240, PTOCBLOCK(NewMovieFromFile)},
    {243, PTOCBLOCK(GetMoviePreferredRate)},
    {249, PTOCBLOCK(GetMovieBox)},
    {250, PTOCBLOCK(SetMovieBox)},
    {394, PTOCBLOCK(NewMovieController)},
    {395, PTOCBLOCK(DisposeMovieController)},
    {402, PTOCBLOCK(OpenMovieFile)},
};

/* QuickTime preview pickers use the same Pascal ABI as Standard File.
   The existing dialog handles selection, file filters and cancellation.
   Native panels omit guest controls/hooks and QuickTime thumbnail previews. */
PRIVATE selector_table_entry_t image_compression_table[] = {
    {0x43, PTOCBLOCK(StandardGetFile)},
    {0x44, PTOCBLOCK(CustomGetFile)},
};

STUB(ImageCompression)
{
    return do_selector_table(EM_D0 & 0xFFFF, image_compression_table, NELEM(image_compression_table), NULL,
                             ImageCompression);
}

STUB(QuickTime)
{
    return do_selector_table(EM_D0 & 0xFFFF, qt_table, NELEM(qt_table), NULL, QuickTime);
}

#if defined(CYGWIN32) && defined(SDL) && defined(SYN68K)
STUB(CodeFragment)
{
    extern uint32 ROMlib_ppc_code_fragment(void);
    return ROMlib_ppc_code_fragment();
}
#endif

#if defined(CYGWIN32) && defined(SDL) && defined(SYN68K)
STUB(DisplayDispatch)
{
    const uint16 selector = EM_D0;
    if (selector == 0 || selector == 1) {
        const syn68k_addr_t ret = POPADDR();
        const BOOLEAN active_only = POPUB();
        const uint32 previous = selector == 1 ? POPUL() : 0;
        WRITEUL(EM_A7, ROMlib_ppc_display_device(selector, previous, active_only));
        return ret;
    }
    unsigned count;
    switch (selector) {
    case 0xffff:
    case 0xfffe:
        count = 0;
        break;
    case 2:
    case 3:
        count = 1;
        break;
    case 0x4e2:
    case 0x414:
    case 0x415:
        count = 2;
        break;
    case 0x51f:
    case 0x520:
        count = 3;
        break;
    case 0x8e4:
    case 0x7ef:
    case 0x726:
        count = 4;
        break;
    default:
        return _Unimplemented(ignoreme, ignoreme2);
    }

    const syn68k_addr_t ret = POPADDR();
    uint32 args[4] = {0};
    while (count) {
        const unsigned index = --count;
        if ((selector == 0x51f || selector == 0x520) && index == 2)
            args[index] = POPUB();
        else if ((selector == 0x7ef && index == 2) || (selector == 0x726 && index == 3))
            args[index] = POPUW();
        else
            args[index] = POPUL();
    }
    const OSErr result = ROMlib_ppc_display_dispatch(selector, args);
    if (selector != 2 && selector != 3)
        WRITEUW(EM_A7, result);
    return ret;
}
#endif

STUB(ThreadDispatch)
{
    /* Universal Interfaces 2.2 Threads.h: YieldToThread and
       YieldToAnyThread use selector 0x0205 (one longword argument).
       No secondary 68K threads are created by ROMAW, so yielding to
       any/current/application thread keeps the application running. */
    syn68k_addr_t ret;
    uint32 thread;

    if ((EM_D0 & 0xffff) != 0x0205)
        return _Unimplemented(ignoreme, ignoreme2);
    ret = POPADDR();
    thread = POPUL();
    WRITEUW(EM_A7, thread <= 2 ? noErr : -618 /* threadNotFoundErr */);
    return ret;
}

STUB(GestaltValueDispatch)
{
    syn68k_addr_t ret = POPADDR();
    uint32 selector, value = 0;
    OSErr err;
    switch (EM_D0 & 0xffff) {
    case 0x401:
    case 0x402:
    case 0x404:
        value = POPUL();
        selector = POPUL();
        err = ROMlib_gestalt_value(selector, value, EM_D0 & 0xff);
        break;
    case 0x203:
        selector = POPUL();
        err = ROMlib_gestalt_value(selector, 0, 3);
        break;
    default:
        err = paramErr;
        break;
    }
    WRITEUW(EM_A7, err);
    return ret;
}

STUB(IMVI_MemoryDispatch)
{
    /* Guest RAM has a stable mapping; no guest VM backing store or DMA map
     exists. Unsupported VM requests return an error instead of a trap abort. */
    uint64 end = (uint64)EM_A0 + EM_A1;
    switch (EM_D0 & 0xffff) {
    case 0xfffd: /* VM_GetHoldableBytes: VMCallsPriv.h, register result in D0. */
        /* HoldMemory above accepts the entire fixed guest RAM span. Guest VM
           never pages these bytes out, independently of host paging policy. */
        EM_D0 = ROMlib_guest_memory_size;
        break;
    case 0:
    case 1:
    case 2:
    case 3:
    case 6:
    case 7: /* Protect/UnprotectMemory: guest VM is not enabled. */
#if defined(CYGWIN32) && defined(SDL) && defined(SYN68K)
        EM_D0 = end <= ROMlib_guest_memory_size ? noErr : paramErr;
#else
        EM_D0 = paramErr;
#endif
        break;
    default:
        EM_D0 = paramErr;
        break;
    }
    RTS();
}

STUB(ComponentDispatch)
{
    unsigned selector = EM_D0 & 0xffff, n, i;
    const char* signature = ROMlib_component_signature(selector);
    syn68k_addr_t ret;
    uint32 args[6] = {0}, result;
    if (selector && !signature)
        return _Unimplemented(ignoreme, ignoreme2);
    ret = POPADDR();
    if (!selector) {
        unsigned char* parameters = SYN68K_TO_US(EM_A7);
        unsigned size = parameters[1];
        uint32 instance = READUL(EM_A7 + 4 + size);
        result = size & 1 ? paramErr : ROMlib_component_call(instance, parameters);
        EM_A7 += 8 + size;
        WRITEUL(EM_A7, result);
        return ret;
    }
    n = strlen(signature) - 1;
    for (i = n; i; --i)
        args[i - 1] = signature[i] == '2' ? POPUW() : POPUL();
    fprintf(stderr, "trace: Component Manager selector=%u\n", selector);
    ROMlib_component_service(selector, args, &result);
    if (*signature == '2')
        WRITEUW(EM_A7, result);
    else if (*signature == '4')
        WRITEUL(EM_A7, result);
    return ret;
}

STUB(SystemNotifyDispatch)
{
    unsigned selector = EM_D0 & 0xffff;
    syn68k_addr_t ret;
    uint32 kind, context;
    int32 result;
    if (selector != 0x300 && selector != 0x301)
        return _Unimplemented(ignoreme, ignoreme2);
    ret = POPADDR();
    context = POPUL();
    kind = POPUL();
    if (selector == 0x301) {
        uint32 callback = POPUL();
        result = ROMlib_system_notify_register(callback ? (ProcPtr)SYN68K_TO_US(callback) : NULL, kind, context);
    }
    else
        result = ROMlib_system_notify(kind, context ? SYN68K_TO_US(context) : NULL);
    WRITEUL(EM_A7, result);
    return ret;
}

PUBLIC boolean_t ROMlib_tsm_dispatch_available(uint32 selector)
{
    return selector == 4 || selector == 8 || selector == 0x14 || selector == 0x15 || selector == 0x1c ||
           selector == 0x1d || selector == 0x20 || selector == 0x21;
}

STUB(TextServicesDispatch)
{
    unsigned selector = EM_D0 & 0xffff;
    syn68k_addr_t ret;
    if (selector == 8) {
        ret = POPADDR();
        const uint32 seed = POPUL();
        const uint32 list = POPUL();
        const uint32 types = POPUL();
        const INTEGER count = POPUW();
        WRITEUW(EM_A7, ROMlib_component_text_services(count, (const OSType*)SYN68K_TO_US_CHECK0(types),
                                                      (HIDDEN_Handle*)SYN68K_TO_US_CHECK0(list),
                                                      (LONGINT*)SYN68K_TO_US_CHECK0(seed)));
        return ret;
    }
    if (selector == 0x21) {
        ret = POPADDR();
        /* Only the Roman text-services interface is implemented. */
        WRITEUB(EM_A7, FALSE);
        return ret;
    }
    if (selector == 0x20 && READUW(EM_A7 + 12) == 0 && READUL(EM_A7 + 4) && READUL(EM_A7 + 8)) {
        uint32 second, first;
        ret = POPADDR();
        second = POPUL();
        first = POPUL();
        POPUW(); /* Roman script has no registered input method. */
        WRITEUL(first, 0);
        WRITEUL(second, 0);
        WRITEUB(EM_A7, FALSE);
        return ret;
    }
    if (selector == 0x1d) {
        ret = POPADDR();
        uint32 id = POPUL();
        WRITEUB(EM_A7, ROMlib_tsm_document_input_window(id ? (Handle)SYN68K_TO_US(id) : NULL));
        return ret;
    }
    if (selector == 0x1c) {
        ret = POPADDR();
        Handle id = ROMlib_tsm_active_document();
        WRITEUL(EM_A7, id ? US_TO_SYN68K(id) : 0);
        return ret;
    }
    if (selector == 4) {
        ret = POPADDR();
        POPUL(); /* EventRecord pointer. */
        /* No input method is installed, including for valid Roman TSM
           documents. Leave the event for the application. */
        WRITEUB(EM_A7, FALSE);
        return ret;
    }
    if (selector != 0x14 && selector != 0x15)
        return _Unimplemented(ignoreme, ignoreme2);
    ret = POPADDR();
    WRITEUW(EM_A7, ROMlib_tsm_registration(selector == 0x14));
    return ret;
}

STUB(MixedModeEntry)
{
#if defined(CYGWIN32) && defined(SDL) && defined(SYN68K)
    extern int ROMlib_ppc_mixed_entry(uint32, uint32*);
    extern void ROMlib_ppc_mixed_entry_error(void);
    uint32 ret;
    if (ROMlib_ppc_mixed_entry(ignoreme, &ret))
        return ret;
    ROMlib_ppc_mixed_entry_error();
    ROMlib_exit = 2;
    C_ExitToShell();
#endif
    return _Unimplemented(ignoreme, ignoreme2);
}
