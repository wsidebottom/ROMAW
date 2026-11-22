/*+===================================================================
  File:      list_layout_probe.c

  Summary:   Exercise ListRec offsets used by ResEdit when reflowing its resource picker.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Exercise ListRec offsets used by ResEdit when reflowing its resource picker. */
#include <Quickdraw.h>
#include <Windows.h>
#include <Multiverse.h>
#include <Files.h>
#include <Memory.h>
#include <stdio.h>
#include <string.h>
static short report;
static int failures;
static volatile long draw_calls;

static void check_cell_drawing(WindowPtr window)
{
    Rect view = {0, 0, 20, 100}, bounds = {0, 0, 5, 2};
    Point size = {20, 100};
    ListHandle list = LNew(&view, &bounds, size, 0, window, 1, 0, 0, 0);
    Handle definition = NewHandle(14);
    if (!list || !definition) {
        ++failures;
        if (list)
            LDispose(list);
        if (definition)
            DisposeHandle(definition);
        return;
    }
    /* Original 68K LDEF: increment the counter, then return with its twenty
     bytes of Pascal arguments removed. No Apple code is used in this probe. */
    unsigned char code[14] = {0x52, 0xb9, 0, 0, 0, 0, 0x20, 0x5f, 0x4f, 0xef, 0, 20, 0x4e, 0xd0};
    long counter = (long)&draw_calls;
    memcpy(code + 2, &counter, 4);
    memcpy(*definition, code, sizeof code);
    Handle original = (**list).listDefProc;
    (**list).listDefProc = definition;
    draw_calls = 0;
    Cell cell = {0, 0};
    LDraw(cell, list);
    cell.v = 1;
    LDraw(cell, list);
    cell.v = 0;
    cell.h = 1;
    LDraw(cell, list);
    cell.v = 3;
    cell.h = 0;
    LSetCell((Ptr) "hidden", 6, cell, list);
    cell.v = 0;
    LSetCell((Ptr) "visible", 7, cell, list);
    if (draw_calls != 2)
        ++failures;
    (**list).listDefProc = original;
    DisposeHandle(definition);
    LDispose(list);
}

static void check(ListHandle list, short expected, const char* phase)
{
    char line[128];
    long length = sprintf(line, "%s maxIndex=%d expected=%d\n", phase, (**list).maxIndex, expected);
    FSWrite(report, &length, line);
    if ((**list).maxIndex != expected)
        ++failures;
}

int main(void)
{
    Rect view = {0, 0, 198, 168}, bounds = {0, 0, 2, 1}, windowBounds = {40, 40, 300, 300};
    Point size = {66, 56};
    WindowPtr window;
    ListHandle list;
    Handle saved_data;
    Cell cell = {0, 0};
    short ids[2] = {1001, 1002}, saved[3] = {0, 0, 0};
    long saved_size;
    long length;
    Str255 path = "\pD:Source:ROMAW:build:list-layout-probe.txt";
    HCreate(0, 0, path, 'LsPr', 'TEXT');
    if (HOpen(0, 0, path, fsRdWrPerm, &report))
        return 1;
    SetEOF(report, 0);
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(0);
    window = NewWindow(0, &windowBounds, "\pList layout probe", 0, 0, (WindowPtr)-1, 0, 0);
    list = LNew(&view, &bounds, size, 0, window, 0, 0, 0, 0);
    if (!list)
        return 2;
    check(list, 4, "new");
    LSetCell((Ptr)&ids[0], 2, cell, list);
    cell.v = 1;
    LSetCell((Ptr)&ids[1], 2, cell, list);
    /* Preserve flat offsets/data like ResEdit when reflowing two rows into one. */
    saved_size = (**list).maxIndex + 2;
    if (saved_size < 0 || saved_size > sizeof saved)
        return 3;
    memcpy(saved, (**list).cellArray, saved_size);
    saved_data = (Handle)(**list).cells;
    if (HandToHand(&saved_data))
        return 4;
    LAddColumn(2, 1, list);
    check(list, 12, "add columns");
    LDelRow(1, 1, list);
    check(list, 6, "reflow rows");
    memcpy((**list).cellArray, saved, saved_size);
    (**list).cellArray[3] = (**list).cellArray[2];
    DisposeHandle((Handle)(**list).cells);
    (**list).cells = (DataHandle)saved_data;
    cell.h = 1;
    cell.v = 0;
    length = 2;
    {
        short actual = 0, n = 2;
        LGetCell((Ptr)&actual, &n, cell, list);
        if (n != 2 || actual != 1002)
            ++failures;
    }
    LAddRow(1, 1, list);
    check(list, 12, "add row");
    LDelColumn(1, 2, list);
    check(list, 8, "delete column");
    LDelRow(1, 1, list);
    check(list, 4, "delete row");
    LDelColumn(0, 0, list);
    check(list, 0, "delete all columns");
    LAddColumn(2, 0, list);
    check(list, 4, "restore columns");
    LDelRow(0, 0, list);
    check(list, 0, "delete all rows");
    LAddRow(1, 0, list);
    check(list, 4, "restore rows");
    LDispose(list);
    check_cell_drawing(window);
    DisposeWindow(window);
    {
        char line[64];
        length = sprintf(line, "failures=%d\n", failures);
        FSWrite(report, &length, line);
    }
    FSClose(report);
    return failures != 0;
}
