/*+===================================================================
  File:      romaw.c

  Summary:   Implements romaw support for Macintosh startup, traps and runtime services.

  Classes:   None.

  Functions: alinehandler, check_trap_watchpoints, dump_recent_traps, romaw_main, trap_break_me,
             trap_dump_bits, trap_watch, trap_watch_handle

  Origin:    Existing repository source in executor/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1992-1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#define DEBUG

#include "rsys/common.h"
#include "rsys/ecf.h"

#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "ResourceMgr.h"
#include "SegmentLdr.h"
#include "ProcessMgr.h"
#include "MemoryMgr.h"
#include "StdFilePkg.h"
#include "EventMgr.h"
#include "VRetraceMgr.h"
#include "OSUtil.h"
#include "FontMgr.h"
#include "ScrapMgr.h"
#include "ToolboxUtil.h"
#include "FileMgr.h"
#include "ControlMgr.h"
#include "DeviceMgr.h"
#include "SoundDvr.h"
#include "PrintMgr.h"
#include "StartMgr.h"
#include "CommTool.h"

#include "rsys/trapglue.h"
#include "rsys/cquick.h"
#include "rsys/file.h"
#include "rsys/application_registry.h"
#include "rsys/notmac.h"
#include "rsys/soundopts.h"
#include "rsys/pstuff.h"
#include "rsys/jumpvectors.h"
#include "rsys/prefs.h"
#include "rsys/aboutpanel.h"
#include "rsys/segment.h"
#include "rsys/misc.h"
#include "rsys/host.h"
#include "rsys/romaw.h"
#include "rsys/cooperative.h"
#include "rsys/desktop.h"
#include "rsys/hfs.h"
#include "rsys/float.h"
#include "rsys/vdriver.h"
#include "rsys/trapname.h"

#include "rsys/options.h"
#include "rsys/suffix_maps.h"
#include "rsys/string.h"

#undef PRIVATE
#define PRIVATE
#define PUBLIC

LONGINT debugnumber, debugtable[1 << 12], cutoff = 20;

#if !defined(NDEBUG)
typedef struct {
    unsigned trapno;
    int32 when;
} trap_when_t;

static int compare_trap_when(const void* p1, const void* p2)
{
    return ((const trap_when_t*)p1)->when - ((const trap_when_t*)p2)->when;
}

/* Prints out the traps executed within the last NUM_TRAPS_BACK traps. */
void dump_recent_traps(int num_traps_back)
{
    trap_when_t traps[0x1000];
    int i, num_interesting_traps;

    /* Record all the traps that are recent enough. */
    for (i = 0, num_interesting_traps = 0; i < (int)NELEM(traps); i++) {
        int32 when;

        when = debugtable[i];
        if (when != 0 && when >= debugnumber - num_traps_back) {
            traps[num_interesting_traps].trapno = 0xA000 + i;
            traps[num_interesting_traps].when = when;
            num_interesting_traps++;
        }
    }

    /* Sort them by time, oldest first. */
    qsort(traps, num_interesting_traps, sizeof traps[0], compare_trap_when);

    /* Print them out. */
    for (i = 0; i < num_interesting_traps; i++)
        printf("0x%04X\t%ld\t%s\n", traps[i].trapno, (long)traps[i].when, trap_name(traps[i].trapno));
}
#endif

#if !defined(NDEBUG)

char trap_watchpoint_data[1024];
char* trap_watchpoint_name[1024];

struct {
    /* pointer to actual memory */
    int handle_p;
    char* x;
    /* pointer to our `orignal' copy */
    char* orig;
    int size;
} trap_watchpoints[1024];

int trap_watchpoint_next_data;
int trap_watchpoint_next;

/* make a per-trap watchpoint of the data at `x', of size `size' */
void trap_watch(char* name, void* x, int size)
{
    char* trap_data;

    if (trap_watchpoint_next == 1024) {
        fprintf(stderr, "all available trap watchpoints used\n");
        return;
    }
    trap_watchpoints[trap_watchpoint_next].size = size;
    trap_watchpoints[trap_watchpoint_next].handle_p = FALSE;
    trap_watchpoints[trap_watchpoint_next].x = x;
    trap_data = trap_watchpoints[trap_watchpoint_next].orig = &trap_watchpoint_data[trap_watchpoint_next_data];
    trap_watchpoint_next++;
    trap_watchpoint_next_data += size;

    memcpy(trap_data, x, size);
}

void trap_watch_handle(char* name, Handle x, int size)
{
    char* trap_data;

    if (trap_watchpoint_next == 1024) {
        fprintf(stderr, "all available trap watchpoints used\n");
        return;
    }
    trap_watchpoints[trap_watchpoint_next].size = size;
    trap_watchpoints[trap_watchpoint_next].handle_p = TRUE;
    trap_watchpoints[trap_watchpoint_next].x = (void*)x;
    trap_data = trap_watchpoints[trap_watchpoint_next].orig = &trap_watchpoint_data[trap_watchpoint_next_data];
    trap_watchpoint_next++;
    trap_watchpoint_next_data += size;

    memcpy(trap_data, STARH((Handle)x), size);
}

void trap_break_me(void)
{
}

void trap_dump_bits(char* msg, char* data, int size)
{
    int i;

    fprintf(stderr, "%s: ", msg);
    for (i = 0; i < size; i++)
        fprintf(stderr, "%x%x", (int)((data[i] >> 4) & 0xF), (int)(data[i] & 0xF));
    fprintf(stderr, "\n");
}

int check_trap_watchpoints_p = FALSE;

void check_trap_watchpoints(char* msg)
{
    int size, i;

    for (i = 0; i < 1024; i++) {
        if (trap_watchpoints[i].x) {
            void* x;

            x = (trap_watchpoints[i].handle_p ? (void*)STARH((Handle)trap_watchpoints[i].x) : trap_watchpoints[i].x);

            size = trap_watchpoints[i].size;

            if (memcmp(x, trap_watchpoints[i].orig, size)) {
                /* differs */
                fprintf(stderr, "%s", msg);
                trap_dump_bits("old", trap_watchpoints[i].orig, size);
                trap_dump_bits("new", x, size);

                memcpy(trap_watchpoints[i].orig, x, size);

                trap_break_me();
            }
        }
    }
}
#endif /* !NDEBUG */

#define TOOLMASK (0x3FF)
#define OSMASK (0xFF)
#define DONTSAVEA0BIT (0x100)
#define POPBIT (0x400)

#define LOADSEGTRAPWORD (0xA9F0)
#define EXITTOSHELLTRAPWORD (0xA9F4)

enum { MODESWITCHTRAPWORD = 0xaafe };

#define ALINEEXCEPTIONFRAMESIZE 8
#define WEIRDMAGIC (0x1F52)

/*
 * FAKEPascalToCCall pushes a phoney return address and then calls
 * PascalToCCall and throws away the return value.  It will be something
 * like:
 *
 */

#define FAKEPascalToCCall(xxx)                \
    do {                                      \
        PUSHADDR(0xEACE4321);                 \
        (void)PascalToCCall(0x87654231, xxx); \
    } while (0)

unsigned short mostrecenttrap;

/* #define MEMORY_WATCH */

#include "rsys/mman_private.h"

#if defined(MEMORY_WATCH)

int memory_watch = 0;

PRIVATE void dump_difference(uint16 trapn, int i, zone_info_t* currentp, const zone_info_t* newp)
{
    fprintf(stderr, "%d %s(%d): D#rel = %d, D#nrel = %d, D#free = %d, Dtotal = %d\n", debugnumber, trap_name(trapn), i,
            newp->n_rel - currentp->n_rel, newp->n_nrel - currentp->n_nrel, newp->n_free - currentp->n_free,
            newp->total_free - currentp->total_free);
    *currentp = *newp;
}

PRIVATE void compare_zone_infos(uint16 trapn, zone_info_t current[3], zone_info_t new[3])
{
    int i;

    for (i = 0; i < 2; ++i) {
        if ((ABS(current[i].n_rel - new[i].n_rel) > 2) || (ABS(current[i].n_nrel - new[i].n_nrel) > 2) ||
            (ABS(current[i].total_free - new[i].total_free) > 12 * 1024))
            dump_difference(trapn, i, &current[i], &new[i]);
    }
}
#endif

PUBLIC syn68k_addr_t alinehandler(syn68k_addr_t pc, void* ignored)
{
    syn68k_addr_t retval;
    unsigned short trapno, status;
    uint32 savea0, savea1, savea2, saved1, saved2;
    unsigned short trapword;
    static unsigned long trap_count;
    void* togoto; /* FIXME: really a syn68k_addr_t */
#if defined(MEMORY_WATCH)
    zone_info_t current_zone_infos[3];
    zone_info_t new_zone_infos[3];
#endif

#if defined(MEMORY_WATCH)
    if (memory_watch) {
        ROMlib_sledgehammer_zones(__PRETTY_FUNCTION__, __FILE__, __LINE__, "after aline", current_zone_infos);
    }
#endif

    mostrecenttrap = trapword = READUW(pc);
    /* The first trap after beginexecutingat belongs to this launch's outer
     interpreter, before a Toolbox callback can create a nested frame. */
    if (!ROMlib_cooperative_depth) {
        ROMlib_cooperative_depth = emulation_depth;
        ROMlib_cooperative_interpreter = cpu_state.setjmp_buf;
    }
    retval = pc + 2;
    ++trap_count;
#if defined(CYGWIN32)
#if !defined(vdriver_system_busy) || vdriver_system_busy
    vdriver_system_busy(TRUE);
#endif
#endif
    if (ERROR_ENABLED_P(ERROR_TRACE_INFO) &&
        (trap_count <= 200 || (trap_count >= 1400 && trap_count <= 1800) || trapword == 0xA970 || trapword == 0xA860 ||
         trapword == LOADSEGTRAPWORD || ((trap_count & 0x3FFF) == 0))) {
        fprintf(stderr, "trace: aline #%lu pc=0x%08lx trap=0x%04x\n", trap_count, (unsigned long)pc, trapword);
        fflush(stderr);
    }

#if !defined(NDEBUG)
    if (check_trap_watchpoints_p)
        check_trap_watchpoints("entering `alinehandler ()'\n");
#endif

        /*
 * Code for debugging
 */

#if !defined(NDEBUG)
    debugtable[trapword & 0xFFF] = ++debugnumber;
#endif /* !NDEBUG */

    status = READUW(EM_A7);
    EM_A7 += ALINEEXCEPTIONFRAMESIZE;
    if (trapword == ROMAW_CONTEXT_TRAP)
        return ROMlib_exchange_guest_context(pc, status);
    if (trapword & TOOLBIT) {
        trapno = trapword & TOOLMASK;
        togoto = tooltraptable[trapno];
        if (trapword & POPBIT)
            retval = POPADDR();
        if (togoto == toolstuff[trapno].orig) {
            if (toolstuff[trapno].ptoc.magic == (ULONGINT)-1) {
                syn68k_addr_t new_addr;

                PUSHADDR(0x80014321); /* better not use this */
                if (trapword == LOADSEGTRAPWORD) {
                    fprintf(stderr, "trace: aline LoadSeg callback begin a7=0x%08lx\n", (unsigned long)EM_A7);
                    fflush(stderr);
                }
                new_addr = (*(callback_handler_t)toolstuff[trapno].ptoc.wheretogo)(pc, (void*)0);
                if (trapword == LOADSEGTRAPWORD) {
                    fprintf(stderr, "trace: aline LoadSeg callback complete new=0x%08lx a7=0x%08lx\n",
                            (unsigned long)new_addr, (unsigned long)EM_A7);
                    fflush(stderr);
                }
                if (trapword == LOADSEGTRAPWORD)
                    retval -= 6;
                else if (trapword == MODESWITCHTRAPWORD)
                    retval = new_addr;
            }
            else {
                FAKEPascalToCCall(&toolstuff[trapno].ptoc);
                if (trapno == 0x1B1 || trapno == 0x01B) {
                    /* CreateResFile/HCreateResFile have no Pascal result, but
                     the classic Resource Manager returns ResErr in D0.W
                     with MOVE.W condition codes. MacWrite branches on Z
                     immediately after creating its document resource fork. */
                    int16 error = CW(ResErr);
                    EM_D0 = (EM_D0 & 0xFFFF0000U) | (uint16)error;
                    cpu_state.ccn = error < 0;
                    cpu_state.ccnz = error != 0;
                    cpu_state.ccv = 0;
                    cpu_state.ccc = 0;
                }
            }
        }
        else {
            PUSHADDR(retval);               /* Where they'll return to */
            retval = (syn68k_addr_t)togoto; /* Where they have patched */
                                            /* us to go to */
        }
    }
    else {
        trapno = trapword & OSMASK;
        togoto = ostraptable[trapno];
        if (togoto == osstuff[trapno].orig) {
            saved1 = EM_D1;
            saved2 = EM_D2;
            savea1 = EM_A1;
            savea2 = EM_A2;
            EM_D1 = trapword;
            EM_D2 = (trapword & 0xFF) << 2;
            EM_A2 = (LONGINT)togoto;
            savea0 = EM_A0;
            PUSHADDR(0x88A84321); /* better not use this */
            (*(callback_handler_t)osstuff[trapno].func)(pc, callback_argument((syn68k_addr_t)togoto));
            if (!(trapword & DONTSAVEA0BIT))
                EM_A0 = savea0;
            EM_D1 = saved1;
            EM_D2 = saved2;
            EM_A1 = savea1;
            EM_A2 = savea2;
        }
        else {
            PUSHADDR(retval);
            PUSHUW(status);
            PUSHUW(WEIRDMAGIC);
            PUSHUL(EM_A2);
            PUSHUL(EM_D2);
            PUSHUL(EM_D1);
            PUSHUL(EM_A1);
            EM_D1 = trapword;
            EM_D2 = (trapword & 0xFF) << 2;
            EM_A2 = (LONGINT)togoto;
            if (!(trapword & DONTSAVEA0BIT))
                PUSHUL(EM_A0);
            CALL_EMULATOR((syn68k_addr_t)togoto);
            if (!(trapword & DONTSAVEA0BIT))
                EM_A0 = POPUL();
            EM_A1 = POPUL();
            EM_D1 = POPUL();
            EM_D2 = POPUL();
            EM_A2 = POPUL();
            EM_A7 += 4;
            retval = POPADDR();
        }
        cpu_state.ccc = 0;
        cpu_state.ccn = (cpu_state.regs[0].sw.n < 0);
        cpu_state.ccv = 0;
        cpu_state.ccx = cpu_state.ccx; /* unchanged */
        cpu_state.ccnz = (cpu_state.regs[0].sw.n != 0);
    }

#if defined(MEMORY_WATCH)
    if (memory_watch) {
        ROMlib_sledgehammer_zones(__PRETTY_FUNCTION__, __FILE__, __LINE__, "after aline", new_zone_infos);
        compare_zone_infos(trapword, current_zone_infos, new_zone_infos);
    }
#endif

#if !defined(NDEBUG)
    if (check_trap_watchpoints_p)
        check_trap_watchpoints("exiting `alinehandler ()'\n");
#endif

    return retval;
}

PUBLIC void romaw_main(void)
{
    /* The PPC client addresses the complete record through a guest alias. */
    _Alignas(256) char quickbytes[grafSize] = {0};
    LONGINT tmpA5;
    INTEGER mess, count, exevrefnum, toskip;
    AppFile thefile;
    Byte* p;
    int i;
    WDPBRec wdpb;
    CInfoPBRec hpb;
    Str255 name;
    StringPtr fName;

    fprintf(stderr, "trace: romaw_main init globals\n");
    fflush(stderr);

    SCSIFlags = CWC(0xEC00); /* scsi+clock+xparam+mmu+adb
				 (no fpu,aux or pwrmgr) */

    MCLKPCmiss1 = 0; /* &MCLKPCmiss1 = 0x358 + 72 (MacLinkPC starts
			   adding the 72 byte offset to VCB pointers too
			   soon, starting with 0x358, which is not the
			   address of a VCB) */

    MCLKPCmiss2 = 0; /* &MCLKPCmiss1 = 0x358 + 78 (MacLinkPC misses) */
    HIDDEN_RAW_SET(AuxCtlHead_H, 0);
    HIDDEN_RAW_SET(CurDeactive_H, 0);
    HIDDEN_RAW_SET(CurActivate_H, 0);
    macfpstate[0] = 0;
    fondid = 0;
    PrintErr = 0;
    mouseoffset = 0;
    HIDDEN_RAW_SET(heapcheck_H, 0);
    DefltStack = CLC(0x2000); /* nobody really cares about these two */
    MinStack = CLC(0x400);    /* values ... */
    HIDDEN_RAW_SET(IAZNotify_H, 0);
    CurPitch = 0;
    HIDDEN_RAW_SET(JSwapFont_H, (ProcPtr)RM(P_FMSwapFont));
    HIDDEN_RAW_SET(JInitCrsr_H, (ProcPtr)RM(P_InitCursor));

    HIDDEN_RAW_SET(Key1Trans_H, (Ptr)RM(P_Key1Trans));
    HIDDEN_RAW_SET(Key2Trans_H, (Ptr)RM(P_Key2Trans));
    HIDDEN_RAW_SET(JFLUSH_H, (ProcPtr)RM(P_flushcache));
    HIDDEN_RAW_SET(JResUnknown1_H, JFLUSH); /* I don't know what these are supposed to */
    HIDDEN_RAW_SET(JResUnknown2_H, JFLUSH); /* do, but they're not called enough for
				   us to worry about the cache flushing
				   overhead */

#if !defined(SYN68K)
    CPUFlag = 2; /* mc68020 */
#else
    CPUFlag = 4; /* mc68040 */
#endif
    UnitNtryCnt = 0; /* how many units in the table */

    HIDDEN_RAW_SET(TheZone_H, SysZone);
    HIDDEN_RAW_SET(VIA_H, RM(NewPtr(16 * 512))); /* IMIII-43 */
    memset(MR(VIA), 0, (LONGINT)16 * 512);
    *(char*)MR(VIA) = 0x80; /* Sound Off */

#define SCC_SIZE 1024

    HIDDEN_RAW_SET(SCCRd_H, RM(NewPtrSysClear(SCC_SIZE)));
    HIDDEN_RAW_SET(SCCWr_H, RM(NewPtrSysClear(SCC_SIZE)));

    HIDDEN_RAW_SET(SoundBase_H, RM(NewPtr(370 * sizeof(INTEGER))));
    for (i = 0; i < 370; ++i)
        ((INTEGER*)MR(SoundBase))[i] = CWC(0x8000); /* reference 0 sound */
    HIDDEN_RAW_SET(TheZone_H, ApplZone);
    HiliteMode = CB(0xFF);
    /* Mac II has 0x3FFF here */
    ROM85 = CWC(0x3FFF);

    a5 = US_TO_SYN68K((uintptr_t)&tmpA5);
    HIDDEN_RAW_SET(CurrentA5_H, (Ptr)CL(a5));
    fprintf(stderr, "trace: romaw_main toolbox init begin\n");
    fflush(stderr);
    fprintf(stderr, "trace: InitGraf begin\n");
    fflush(stderr);
    InitGraf((Ptr)quickbytes + sizeof(quickbytes) - 4);
    fprintf(stderr, "trace: InitGraf complete\n");
    fflush(stderr);
    fprintf(stderr, "trace: romaw InitFonts begin\n");
    fflush(stderr);
    InitFonts();
    fprintf(stderr, "trace: romaw InitFonts complete\n");
    fflush(stderr);
    fprintf(stderr, "trace: InitCRM begin\n");
    fflush(stderr);
    InitCRM();
    fprintf(stderr, "trace: InitCRM complete\n");
    fflush(stderr);
    fprintf(stderr, "trace: FlushEvents begin\n");
    fflush(stderr);
    FlushEvents(everyEvent, 0);
    fprintf(stderr, "trace: FlushEvents complete\n");
    fflush(stderr);
    fprintf(stderr, "trace: InitWindows begin\n");
    fflush(stderr);
    InitWindows();
    fprintf(stderr, "trace: InitWindows complete\n");
    fflush(stderr);
    fprintf(stderr, "trace: TEInit begin\n");
    fflush(stderr);
    TEInit();
    fprintf(stderr, "trace: TEInit complete\n");
    fflush(stderr);
    fprintf(stderr, "trace: InitDialogs begin\n");
    fflush(stderr);
    InitDialogs((ProcPtr)0);
    fprintf(stderr, "trace: InitDialogs complete\n");
    fflush(stderr);
    fprintf(stderr, "trace: InitCursor begin\n");
    fflush(stderr);
    InitCursor();
    fprintf(stderr, "trace: InitCursor complete\n");
    fflush(stderr);
    fprintf(stderr, "trace: romaw_main toolbox init complete\n");
    fflush(stderr);

    if (ROMlib_desktop_run()) {
        ROMlib_exit = TRUE;
        return;
    }

    loadtrap = 0;

    /* ROMlib_WriteWhen(WriteInOSEvent); */

    str255assign(FinderName, (StringPtr) "\006Finder");

    fprintf(stderr, "trace: CountAppFiles begin\n");
    fflush(stderr);
    CountAppFiles(&mess, &count);
    count = CW(count);
    fprintf(stderr, "trace: CountAppFiles complete count=%d\n", count);
    fflush(stderr);
    if (count > 0)
        GetAppFiles(1, &thefile);
    else
        thefile.fType = 0;
    fprintf(stderr, "trace: app file type=0x%08lx\n", (unsigned long)CL(thefile.fType));
    fflush(stderr);
    fprintf(stderr, "trace: after app file type flush\n");
    fflush(stderr);

#if defined(SYN68K)
#define ALINETRAPNUMBER 0xA
    fprintf(stderr, "trace: trap_install_handler begin\n");
    fflush(stderr);
    trap_install_handler(ALINETRAPNUMBER, alinehandler, (void*)0);
    fprintf(stderr, "trace: trap_install_handler complete\n");
    fflush(stderr);
#endif
    if (count == 0) {
        fprintf(stderr,
                "No Macintosh application was supplied. Start ROMAW without arguments for the native Finder.\n");
        ROMlib_exit = 2;
        return;
    }
    ROMlib_ecf_application_operand(thefile.fType == TICKX("APPL") || thefile.fType == TICKX("FNDR") ||
                                   thefile.fType == TICKX("cdev") || thefile.fType == TICKX("dfil"));
    if (thefile.fType == TICKX("APPL") || thefile.fType == TICKX("FNDR") || thefile.fType == TICKX("cdev") ||
        thefile.fType == TICKX("dfil")) {
        ClrAppFiles(1);
        Munger(MR(AppParmHandle), 2L * sizeof(INTEGER), (Ptr)0, (LONGINT)sizeof(AppFile), (Ptr) "", 0L);
    }
    FIELD_ASSIGN_RAW(hpb.hFileInfo.ioNamePtr, RM(&thefile.fName[0]));
    hpb.hFileInfo.ioVRefNum = thefile.vRefNum;
    hpb.hFileInfo.ioFDirIndex = CWC(0);
    hpb.hFileInfo.ioDirID = CLC(0);
    fprintf(stderr, "trace: first PBGetCatInfo begin\n");
    fflush(stderr);
    PBGetCatInfo(&hpb, FALSE);
    fprintf(stderr, "trace: first PBGetCatInfo complete err=%d\n", CW(hpb.hFileInfo.ioResult));
    fflush(stderr);

    if (thefile.fType == TICKX("APPL") || thefile.fType == TICKX("FNDR") || thefile.fType == TICKX("cdev") ||
        thefile.fType == TICKX("dfil"))
        fName = thefile.fName;
    else {
        const char* p;

        if (count > 0) {
            p = ROMlib_find_best_creator_type_match(CL(hpb.hFileInfo.ioFlFndrInfo.fdCreator),
                                                    CL(hpb.hFileInfo.ioFlFndrInfo.fdType));
            if (!p) {
                FSSpec application;
                if (ROMlib_application_spec(CL(hpb.hFileInfo.ioFlFndrInfo.fdCreator),
                                            CL(hpb.hFileInfo.ioFlFndrInfo.fdType), 1, &application) == noErr) {
                    LaunchParamBlockRec launch = {0};
                    launch.launchBlockID = CWC(extendedBlock);
                    launch.launchEPBLength = CLC(extendedBlockLen);
                    PP_ASSIGN(launch.launchAppSpec, &application);
                    ROMlib_exit = TRUE;
                    NewLaunch(NULL, 0, &launch);
                    return;
                }
            }
        }
        else {
            p = NULL;
        }
        fName = name;
        if (!p)
            ExitToShell();
        else {
            ROMlib_exit = TRUE;
            str255_from_c_string(fName, p);
            FIELD_ASSIGN_RAW(hpb.hFileInfo.ioNamePtr, RM(fName));
            hpb.hFileInfo.ioVRefNum = CWC(0);
            hpb.hFileInfo.ioFDirIndex = CWC(0);
            hpb.hFileInfo.ioDirID = CLC(0);
            fprintf(stderr, "trace: default app PBGetCatInfo begin\n");
            fflush(stderr);
            PBGetCatInfo(&hpb, FALSE);
            fprintf(stderr, "trace: default app PBGetCatInfo complete err=%d\n", CW(hpb.hFileInfo.ioResult));
            fflush(stderr);

            {
                HParamBlockRec hp;
                Str255 fName2;

                memset(&hp, 0, sizeof hp);
                str255assign(fName2, fName);
                FIELD_ASSIGN_RAW(hp.ioParam.ioNamePtr, RM((StringPtr)fName2));
                hp.volumeParam.ioVolIndex = CWC(-1);
                fprintf(stderr, "trace: PBHGetVInfo begin\n");
                fflush(stderr);
                PBHGetVInfo(&hp, FALSE);
                fprintf(stderr, "trace: PBHGetVInfo complete err=%d\n", CW(hp.ioParam.ioResult));
                fflush(stderr);
                hpb.hFileInfo.ioVRefNum = hp.ioParam.ioVRefNum;
            }
        }
    }

    for (p = fName + fName[0] + 1; p > fName && *--p != ':';)
        ;
    toskip = p - fName;
    CurApName[0] = MIN(fName[0] - toskip, 31);
    BlockMove((Ptr)fName + 1 + toskip, (Ptr)CurApName + 1, (Size)CurApName[0]);

    wdpb.ioVRefNum = hpb.hFileInfo.ioVRefNum;
    wdpb.ioWDDirID = hpb.hFileInfo.ioFlParID;
    SFSaveDisk_Update(CW(hpb.hFileInfo.ioVRefNum), fName);
    CurDirStore = hpb.hFileInfo.ioFlParID;
    wdpb.ioWDProcID = CLC(T('X', 'c', 't', 'r'));
    FIELD_ASSIGN_RAW(wdpb.ioNamePtr, 0);
    fprintf(stderr, "trace: PBOpenWD begin\n");
    fflush(stderr);
    PBOpenWD(&wdpb, FALSE);
    fprintf(stderr, "trace: PBOpenWD complete err=%d vref=%d\n", CW(wdpb.ioResult), CW(wdpb.ioVRefNum));
    fflush(stderr);
    exevrefnum = CW(wdpb.ioVRefNum);
    fprintf(stderr, "trace: Launch begin app='%.*s' vref=%d\n", fName[0], fName + 1, exevrefnum);
    fflush(stderr);
    Launch(fName, exevrefnum);
    fprintf(stderr, "trace: Launch returned\n");
    fflush(stderr);
}
