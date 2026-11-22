/*+===================================================================
  File:      tests.c

  Summary:   Executes the maintained selection and persistence code on synthetic records. All video driver writes below
             are intercepted; PRAM is never touched.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in tools/lc475-vga-startup; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Executes the maintained selection and persistence code on synthetic records.
   All video driver writes below are intercepted; PRAM is never touched. */
#include <Files.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <Menus.h>
#include <Processes.h>
#include <Windows.h>
#include <string.h>

#include "display.h"

static unsigned char saved_id;
static unsigned long saved_mode, saved_depth;
static Boolean fail_classic;
static unsigned int controls, statuses, preferences;

static OSErr mock_control(ParmBlkPtr pointer)
{
    CntrlParam* pb = (CntrlParam*)pointer;
    void* data;
    memcpy(&data, pb->csParam, sizeof data);
    ++controls;
    if (pb->csCode == 9) {
        if (fail_classic)
            return -17;
        saved_id = *(unsigned char*)data;
        return noErr;
    }
    if (pb->csCode == 16) {
        saved_mode = ((VGASwitch*)data)->data;
        saved_depth = ((VGASwitch*)data)->mode;
        return noErr;
    }
    return -17;
}

static OSErr mock_status(ParmBlkPtr pointer)
{
    CntrlParam* pb = (CntrlParam*)pointer;
    void* data;
    memcpy(&data, pb->csParam, sizeof data);
    ++statuses;
    if (pb->csCode == 9)
        *(unsigned char*)data = saved_id;
    else if (pb->csCode == 16) {
        ((VGASwitch*)data)->data = saved_mode;
        ((VGASwitch*)data)->mode = saved_depth;
    }
    else
        return -18;
    return noErr;
}

static OSErr mock_preferences(unsigned long a, unsigned long b, unsigned long c)
{
    (void)a;
    (void)b;
    (void)c;
    ++preferences;
    return noErr;
}

#define PBControlSync mock_control
#define PBStatusSync mock_status
#define VGASavePreferences mock_preferences
#define main startup_main
#include "main.c"
#undef main

int main(void)
{
    VGAParameters pixels = {0};
    VGASwitch switch_info = {131, 145, 0, NULL, 0};
    VGADepth depth = {&switch_info, &pixels, 0, 0, 0};
    VGADepths depths = {1, &depth, 0, 0, 0};
    VGAResolution resolution = {0, 145, 640, 480, 67L << 16, 131, 0, 0};
    VGATiming timing = {145, 0, 'decl', 140, 0};
    VGAMode mode = {0, &switch_info, &resolution, &timing, &depths, 1, NULL, NULL};
    GDevice record = {0};
    GDPtr device = &record;
    int failures = 0;

    InitGraf(&qd.thePort);
    open_log();
    pixels.pixelSize = 8;
    pixels.bounds.right = 640;
    pixels.bounds.bottom = 480;
    inspect_mode(NULL, 0, &mode);
    failures += found;             /* Apple's 67 Hz is not VGA. */
    resolution.refresh = 3928228L; /* 59.94 Hz in 16.16. */
    inspect_mode(NULL, 0, &mode);
    failures += !found || selected_mode != 145 || selected_depth != 131;
    found = false;
    pixels.pixelSize = 16;
    inspect_mode(NULL, 0, &mode);
    failures += found;
    pixels.pixelSize = 8;
    resolution.width = 832;
    inspect_mode(NULL, 0, &mode);
    failures += found;
    resolution.width = 640;
    resolution.refresh = 0;
    inspect_mode(NULL, 0, &mode);
    failures += found; /* Unknown refresh must not select an arbitrary mode. */
    timing.data = 150;
    inspect_mode(NULL, 0, &mode);
    failures += !found; /* Recognized VGA timing with old-ROM zero refresh. */
    inspect_mode(NULL, 0, NULL);
    save_startup_mode(&device);
    failures += saved_id != 145 || controls != 1 || statuses != 1 || preferences != 1;
    fail_classic = true;
    save_startup_mode(&device);
    failures += saved_mode != 145 || saved_depth != 131 || controls != 3 || statuses != 2 || preferences != 2;
    selected_mode = 0x1234;
    save_startup_mode(&device);
    failures += saved_mode != 0x1234 || controls != 4 || statuses != 3;
    log_line("Test failures", failures);
    if (log_ref != -1)
        FSClose(log_ref);
    return failures ? 1 : 0;
}
