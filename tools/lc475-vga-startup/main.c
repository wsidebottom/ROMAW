/*+===================================================================
  File:      main.c

  Summary:   Implements main support for ROMAW build and development support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in tools/lc475-vga-startup; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include <Files.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <Menus.h>
#include <Processes.h>
#include <Windows.h>
#include <stdio.h>
#include <string.h>

#include "display.h"

static short log_ref = -1;
static unsigned long selected_mode, selected_depth;
static Boolean found;

static void log_line(const char* message, long value)
{
    char buffer[160];
    long length;

    if (log_ref == -1)
        return;
    length = snprintf(buffer, sizeof buffer, "%s: %ld\r", message, value);
    if (length > 0 && length < (long)sizeof buffer)
        FSWrite(log_ref, &length, buffer);
}

static void open_log(void)
{
    ProcessSerialNumber psn;
    ProcessInfoRec info;
    FSSpec spec;
    Str255 name;
    const char filename[] = "VGA Startup Log";

    memset(&info, 0, sizeof info);
    info.processInfoLength = sizeof info;
    info.processName = name;
    info.processAppSpec = &spec;
    if (GetCurrentProcess(&psn) != noErr || GetProcessInformation(&psn, &info) != noErr)
        return;
    spec.name[0] = sizeof filename - 1;
    memcpy(spec.name + 1, filename, sizeof filename - 1);
    FSpCreate(&spec, 'ttxt', 'TEXT', 0);
    if (FSpOpenDF(&spec, fsWrPerm, &log_ref) != noErr)
        log_ref = -1;
    else if (SetEOF(log_ref, 0) != noErr) {
        FSClose(log_ref);
        log_ref = -1;
    }
}

static OSErr driver_call(GDHandle device, short selector, void* data, Boolean status)
{
    CntrlParam pb;
    memset(&pb, 0, sizeof pb);
    pb.ioCRefNum = (*device)->gdRefNum;
    pb.csCode = selector;
    /* Video controls take a pointer in csParam, not an inline record. */
    memcpy(pb.csParam, &data, sizeof data);
    return status ? PBStatusSync((ParmBlkPtr)&pb) : PBControlSync((ParmBlkPtr)&pb);
}

static void save_startup_mode(GDHandle device)
{
    unsigned char default_mode[2] = {0, 0};
    VGASwitch preferred;
    OSErr error;

    error = VGASavePreferences(0, 0, 0);
    log_line("Save screen preferences error", error);
    /* Classic built-in drivers use SetDefaultMode to store the functional
     sResource ID in their own slot PRAM. Never guess raw PRAM offsets. */
    if (selected_mode > 0 && selected_mode <= 255) {
        default_mode[0] = selected_mode;
        error = driver_call(device, 9, default_mode, false);
        log_line("SetDefaultMode PRAM write error", error);
        if (error == noErr) {
            default_mode[0] = 0;
            error = driver_call(device, 9, default_mode, true);
            log_line("GetDefaultMode error", error);
            if (error == noErr)
                log_line("PRAM default matches requested mode", default_mode[0] == selected_mode);
            return;
        }
    }
    memset(&preferred, 0, sizeof preferred);
    preferred.data = selected_mode;
    preferred.mode = selected_depth;
    error = driver_call(device, 16, &preferred, false);
    log_line("SavePreferredConfiguration error", error);
    if (error == noErr) {
        memset(&preferred, 0, sizeof preferred);
        error = driver_call(device, 16, &preferred, true);
        log_line("GetPreferredConfiguration error", error);
        if (error == noErr)
            log_line("Saved configuration matches requested mode",
                     preferred.data == selected_mode && preferred.mode == selected_depth);
    }
}

static pascal void inspect_mode(void* context, unsigned long index, VGAMode* mode)
{
    unsigned long i;
    Boolean vga_timing;
    (void)context;
    (void)index;

    if (!mode || !mode->resolution || !mode->depths)
        return;
    log_line("Mode ID", mode->resolution->mode);
    log_line("Width", mode->resolution->width);
    log_line("Height", mode->resolution->height);
    log_line("Refresh (16.16 Hz)", mode->resolution->refresh);
    /* A recognized VGA timing ID also handles old ROMs reporting zero Hz.
     Never substitute Apple's 640x480/67 Hz or an unknown timing. */
    vga_timing = (mode->timing && mode->timing->format == 'decl' && mode->timing->data == 150);
    if (mode->resolution->width != 640 || mode->resolution->height != 480 ||
        (!vga_timing && (mode->resolution->refresh < (59L << 16) || mode->resolution->refresh > (61L << 16))))
        return;
    if (!mode->depths->depths)
        return;
    for (i = 0; i < mode->depths->count; ++i) {
        VGADepth* depth = &mode->depths->depths[i];
        VGAParameters* parameters = depth->parameters;
        if (parameters && depth->switchInfo && parameters->pixelSize == 8 &&
            parameters->bounds.right - parameters->bounds.left == 640 &&
            parameters->bounds.bottom - parameters->bounds.top == 480 && !found) {
            selected_mode = depth->switchInfo->data;
            selected_depth = depth->switchInfo->mode;
            found = true;
        }
    }
}

int main(void)
{
    long attributes = 0, version = 0;
    unsigned long id = 0, count = 0, index;
    GDHandle device;
    void* list = NULL;
    OSErr error;
    unsigned char keys[16];
    VGASwitch actual;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    open_log();
    GetKeys(keys);
    /* Either Shift key suppresses the switch, including manual launches. */
    if (((unsigned char*)keys)[7] & 0x11) {
        log_line("Skipped (Shift held)", 0);
        goto done;
    }
    if (Gestalt('dply', &attributes) != noErr || !(attributes & 1) || Gestalt('dplv', &version) != noErr ||
        version < 0x00020000) {
        log_line("Requires Display Manager 2.0 extension on System 7.1", version);
        goto done;
    }
    device = GetMainDevice();
    if (!device || VGAGetDisplayID(device, &id, false) != noErr) {
        log_line("Cannot identify main display", -1);
        goto done;
    }
    /* Include optional modes, regardless of the monitor's recommended list. */
    error = VGANewList(id, 1, 0, &count, &list);
    log_line("Enumerate error", error);
    if (error != noErr)
        goto done;
    for (index = 0; index < count; ++index) {
        error = VGAGetMode(list, index, 0, inspect_mode, NULL);
        if (error != noErr) {
            log_line("Read mode error", error);
            found = false;
            break;
        }
    }
    VGADisposeList(list);
    if (!found) {
        log_line("Driver exposes no 640x480 60 Hz 256-color mode", -1);
        goto done;
    }
    log_line("Request VGA mode", selected_mode);
    /* Deliberately no confirmation dialog, DMCheckDisplayMode safe-mode
     filter, timeout rollback, or restore-on-exit. Finder keeps this mode. */
    error = VGASetMode(device, selected_mode, &selected_depth, 0, NULL);
    log_line("Set mode error (0 means success)", error);
    if (error == noErr) {
        log_line("Result width", (*device)->gdRect.right - (*device)->gdRect.left);
        log_line("Result height", (*device)->gdRect.bottom - (*device)->gdRect.top);
        memset(&actual, 0, sizeof actual);
        error = VGAReadMode(device, &actual);
        log_line("Read active mode error", error);
        if (error == noErr && actual.data == selected_mode && actual.mode == selected_depth &&
            (*device)->gdRect.right - (*device)->gdRect.left == 640 &&
            (*device)->gdRect.bottom - (*device)->gdRect.top == 480)
            save_startup_mode(device);
        else
            log_line("Active mode not verified; startup setting not saved", -1);
    }
done:
    if (log_ref != -1) {
        FSClose(log_ref);
        FlushVol(NULL, 0);
    }
    return 0;
}
