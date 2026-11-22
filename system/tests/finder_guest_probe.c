/*+===================================================================
  File:      finder_guest_probe.c

  Summary:   Exercise real Finder resource callbacks, Toolbox services and Pascal ABIs.

  Classes:   None.

  Functions: main, finder_hlock_hook, finder_gestalt_hook (assembly trap hooks).

  Origin:    ROMAW Finder guest integration regression fixture using Retro68.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#include <Quickdraw.h>
#include <Windows.h>
#include <Dialogs.h>
#include <Resources.h>
#include <Gestalt.h>
#include <Memory.h>
#include <LowMem.h>
#include <Files.h>
#include <Multiverse.h>
#include <stdio.h>
#include <string.h>

static short report;
static int failures, checks;
static long frame_calls;
static Rect patched_frame_rect;
static ProcPtr original_frame;
static volatile long hlock_calls __asm__("finder_hlock_calls");
static volatile long gestalt_calls __asm__("finder_gestalt_calls");
static ProcPtr original_hlock __asm__("finder_original_hlock");
static ProcPtr original_gestalt __asm__("finder_original_gestalt");
extern void finder_hlock_hook(void) __asm__("finder_hlock_hook");
extern void finder_gestalt_hook(void) __asm__("finder_gestalt_hook");

/* Register-ABI OS trap patches tail-chain without disturbing A0 or D1. */
__asm__(".globl finder_hlock_hook\n"
        "finder_hlock_hook:\n\t"
        "addq.l #1,finder_hlock_calls\n\t"
        "move.l finder_original_hlock,-(%sp)\n\t"
        "rts\n"
        ".globl finder_gestalt_hook\n"
        "finder_gestalt_hook:\n\t"
        "addq.l #1,finder_gestalt_calls\n\t"
        "move.l finder_original_gestalt,-(%sp)\n\t"
        "rts\n");

static pascal void frame_patch(Rect* rect)
{
    ++frame_calls;
    patched_frame_rect = *rect;
    ((pascal void (*)(Rect*))original_frame)(rect);
}

static void check(int passed, const char* name)
{
    char line[160];
    long length = sprintf(line, "%s %s\n", passed ? "PASS" : "FAIL", name);
    FSWrite(report, &length, line);
    ++checks;
    if (!passed)
        ++failures;
}

/* Call the original resource ABIs directly, rather than a host-side C test
 * adapter. Arguments are in registers before touching SP. The result slot
 * and stack cleanup therefore also test the native callback marshaling.
 */
static long definition(Ptr procedure, short variant, Handle object, short message, long parameter)
{
    long result;
    __asm__ volatile("subq.l #4,%%sp\n\t"
                     "move.w %[variant],-(%%sp)\n\t"
                     "move.l %[object],-(%%sp)\n\t"
                     "move.w %[message],-(%%sp)\n\t"
                     "move.l %[parameter],-(%%sp)\n\t"
                     "jsr (%[procedure])\n\t"
                     "move.l (%%sp)+,%[result]"
                     : [result] "=&d"(result)
                     : [procedure] "a"(procedure), [variant] "d"(variant), [object] "a"(object), [message] "d"(message),
                       [parameter] "d"(parameter)
                     : "a0", "a1", "d0", "d1", "cc", "memory");
    return result;
}

static short query(Ptr procedure, long selector, long* response)
{
    short result;
    __asm__ volatile("subq.l #2,%%sp\n\t"
                     "move.l %[selector],-(%%sp)\n\t"
                     "move.l %[response],-(%%sp)\n\t"
                     "jsr (%[procedure])\n\t"
                     "move.w (%%sp)+,%[result]"
                     : [result] "=&d"(result)
                     : [procedure] "a"(procedure), [selector] "d"(selector), [response] "a"(response)
                     : "a0", "a1", "d0", "d1", "cc", "memory");
    return result;
}

static void install(Ptr procedure, long value)
{
    __asm__ volatile("move.l %[value],-(%%sp)\n\t"
                     "jsr (%[procedure])\n\t"
                     "addq.l #4,%%sp"
                     :
                     : [procedure] "a"(procedure), [value] "d"(value)
                     : "a0", "a1", "d0", "d1", "cc", "memory");
}

static void test_controls(WindowPtr window)
{
    Rect bounds = {10, 20, 50, 80};
    Handle code = GetResource('CDEF', 128);
    check(code != 0, "CDEF available through Resource Manager");
    if (!code)
        return;
    ControlHandle control = NewControl(window, &bounds, "\p", 1, 0, 0, 0, 2048, 0);
    check(control != 0, "NewControl loads native CDEF 128");
    if (!control)
        return;
    HLock(code);
    original_hlock = GetOSTrapAddress(0xa029);
    hlock_calls = 0;
    SetOSTrapAddress((ProcPtr)finder_hlock_hook, 0xa029);
    check(definition(*code, 0, (Handle)control, 1, (20L << 16) | 30) == 10, "CDEF interior hit");
    SetOSTrapAddress(original_hlock, 0xa029);
    check(hlock_calls == 1, "CDEF honors guest HLock trap patch");
    check(definition(*code, 0, (Handle)control, 1, (20L << 16) | 77) == 0, "CDEF right edge excluded");
    RgnHandle region = NewRgn();
    check(region != 0, "region allocated");
    if (region) {
        long tagged_region = (long)region | 0xaa000000UL;
        check(definition(*code, 0, (Handle)control, 2, tagged_region) == 10, "CDEF region masks high address byte");
        check(!memcmp(&(**region).rgnBBox, &bounds, sizeof(bounds)), "CDEF region uses original bounds");
        DisposeRgn(region);
    }
    RGBColor before, after;
    GetForeColor(&before);
    original_frame = GetToolTrapAddress(0xa8a1);
    SetToolTrapAddress((ProcPtr)frame_patch, 0xa8a1);
    check(definition(*code, 0, (Handle)control, 0, 0) == 10, "CDEF draws using real QuickDraw");
    SetToolTrapAddress(original_frame, 0xa8a1);
    check(frame_calls == 1 && !memcmp(&patched_frame_rect, &bounds, sizeof(bounds)),
          "CDEF honors FrameRect patch with readable guest rectangle");
    GetForeColor(&after);
    check(!memcmp(&before, &after, sizeof(before)), "CDEF restores foreground color");
    HiliteControl(control, 255);
    check(definition(*code, 0, (Handle)control, 0, 0) == 10, "CDEF highlighted drawing");
    PenState pen;
    GetPenState(&pen);
    check(pen.pnSize.v == 1 && pen.pnSize.h == 1, "highlighted CDEF restores normal pen");
    HideControl(control);
    check(definition(*code, 0, (Handle)control, 0, 0) == 0, "invisible CDEF returns zero");
    check(definition(*code, 0, (Handle)control, -1, 0) == 10, "CDEF unknown message preserves default result");
    DisposeControl(control);
    HUnlock(code);
    EmptyHandle(code);
    LoadResource(code);
    check(*code != 0 && GetHandleSize(code) == 14, "CDEF reload restores generated veneer");

    Handle window_code = GetResource('WDEF', 128);
    check(window_code != 0, "WDEF available through Resource Manager");
    if (window_code) {
        HLock(window_code);
        for (short message = 0; message < 10; ++message)
            check(definition(*window_code, 0, (Handle)window, message, 0) == 0,
                  "WDEF returns zero with Pascal cleanup");
        HUnlock(window_code);
    }
}

static void test_gestalt(void)
{
    Handle code = GetResource('gest', 128);
    check(code != 0, "gest resource available");
    if (!code)
        return;
    check((GetResAttrs(code) & 0x50) == 0x50, "gest is locked in the system heap");
    long response = 0x12345678;
    check(query(*code, 'none', &response) == -5551 && response == 0x12345678, "empty table leaves response untouched");
    check(query(*code, 0x666e648c, &response) == 0 && response == (long)*code, "gest returns callable guest entry");
    check(query(*code, 0x666e64f6, &response) == 0 && response == *(long*)(*code + 128),
          "gest caches real system Handle");
    Handle table = (Handle)response;
    check(HandleZone(table) == LMGetSysZone(), "cached Handle belongs to system heap");
    SetHandleSize(table, 16);
    check(MemError() == 0, "private selector table resized");
    if (MemError())
        return;
    ((long*)*table)[0] = 'one!';
    ((long*)*table)[1] = 0x11223344;
    ((long*)*table)[2] = 'two!';
    ((long*)*table)[3] = 0x55667788;
    check(query(*code, 'one!', &response) == 0 && response == 0x11223344, "gest first selector");
    check(query(*code, 'two!', &response) == 0 && response == 0x55667788, "gest second selector");
    check(query(*code, 'none', &response) == -5551 && response == 0x55667788,
          "gest missing selector stops at allocation boundary");
    SetHandleSize(table, 12);
    check(query(*code, 'two!', &response) == -5551 && response == 0x55667788, "truncated table preserves response");
    SetHandleSize(table, 256);
    memset(*table, 0, 256);
    check(query(*code, 'none', &response) == -5551, "malformed original counter cannot loop forever");

    Handle second = NewHandleSys(132);
    check(second != 0, "second resource instance allocated");
    if (second) {
        HLock(second);
        memcpy(*second, *code, 132);
        *(long*)(*second + 128) = 0;
        query(*second, 0x666e64f6, &response);
        check((Handle)response != table && (Handle)response != 0, "resource instances have independent cached state");
        DisposeHandle((Handle)response);
        DisposeHandle(second);
    }
}

static void test_hcsl(void)
{
    Handle code = GetResource('hcsl', 0);
    check(code != 0, "hcsl resource available");
    if (!code)
        return;
    Handle application_copy = NewHandle(42);
    long response = 0;
    if (application_copy) {
        HLock(application_copy);
        memcpy(*application_copy, *code, 42);
        install(*application_copy, 123);
        check(Gestalt('hcsl', &response) != 0, "NewGestalt rejects an application-heap handler");
        check(query(*application_copy + 26, 'test', &response) == 0 && response == 123,
              "failed installation still retains instance response");
    }
    original_gestalt = GetOSTrapAddress(0xa1ad);
    gestalt_calls = 0;
    SetOSTrapAddress((ProcPtr)finder_gestalt_hook, 0xa1ad);
    install(*code, 0x11223344);
    SetOSTrapAddress(original_gestalt, 0xa1ad);
    check(gestalt_calls == 1, "hcsl installer honors guest NewGestalt trap patch");
    check(Gestalt('hcsl', &response) == 0 && response == 0x11223344, "hcsl registers through real NewGestalt");
    install(*code, 0x55667788);
    check(Gestalt('hcsl', &response) == 0 && response == 0x55667788, "duplicate installation updates stored response");
    check(query(*code + 26, 'test', &response) == 0 && response == 0x55667788, "hcsl handler ignores selector");
    if (application_copy) {
        check(query(*application_copy + 26, 'test', &response) == 0 && response == 123,
              "hcsl instances retain separate state");
        DisposeHandle(application_copy);
    }
}

int main(void)
{
    Str255 path = "\pD:Source:ROMAW:build:finder-guest:guest-results.txt";
    HCreate(0, 0, path, 'FGPr', 'TEXT');
    if (HOpen(0, 0, path, fsRdWrPerm, &report))
        return 1;
    SetEOF(report, 0);
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(0);
    Rect bounds = {40, 40, 240, 340};
    WindowPtr window = NewCWindow(0, &bounds, "\pFinder guest procedures", 0, 0, (WindowPtr)-1, 0, 0);
    check(window != 0, "color window created");
    if (window) {
        SetPort(window);
        test_controls(window);
        DisposeWindow(window);
    }
    test_gestalt();
    test_hcsl();
    char line[80];
    long length = sprintf(line, "failures=%d checks=%d\n", failures, checks);
    FSWrite(report, &length, line);
    FSClose(report);
    return failures != 0;
}
