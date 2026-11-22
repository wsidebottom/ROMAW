/*+===================================================================
  File:      window_prefs_test.c

  Summary:   Implements window prefs test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include <assert.h>
#include <stdio.h>
#include <windows.h>
#include "windowprefs.h"

static const unsigned char coloring[] = "\021Coloring Book 3.2";
static const unsigned char browser[] = "\007Browser";

int main(int argc, char** argv)
{
    int w = 17, h = 19;
    const char* invalid[] = {"",         "511x342",  "512x341",  "4097x480",
                             "800x4097", "-800x600", "800x-600", "800x600junk",
                             "800x",     "800x600 ", " 800x600", "999999999999999999999999x480",
                             NULL};
    unsigned i;
    assert(argc == 2);
    for (i = 0; invalid[i]; ++i) {
        assert(!window_size_parse(invalid[i], 512, 342, 4096, 4096, &w, &h));
        assert(w == 17 && h == 19);
    }
    assert(window_size_parse("512x342", 512, 342, 4096, 4096, &w, &h));
    assert(w == 512 && h == 342);
    assert(window_size_parse("800X600", 512, 342, 4096, 4096, &w, &h));
    assert(w == 800 && h == 600);

    window_prefs_init(argv[1], 512, 342, 4096, 4096);
    assert(!window_prefs_select(coloring, 0x4150504c, 0x43424f4b, &w, &h));
    assert(window_prefs_save(700, 500));
    assert(!window_prefs_select(browser, 0x4150504c, 0x42524f57, &w, &h));
    assert(window_prefs_save(900, 650));
    {
        char contents[4096];
        FILE* file = fopen(argv[1], "rb");
        assert(file);
        size_t length = fread(contents, 1, sizeof contents - 1, file);
        contents[length] = 0;
        assert(!fclose(file));
        assert(strstr(contents, "\r\n\r\n[APPL.BROW]"));
    }
    assert(!window_prefs_save(511, 342));
    assert(!window_prefs_save(800, 4097));

    /* Identity is type/creator; changing the displayed name preserves settings. */
    window_prefs_init(argv[1], 512, 342, 4096, 4096);
    assert(window_prefs_select(coloring, 0x4150504c, 0x43424f4b, &w, &h));
    assert(w == 700 && h == 500);
    assert(window_prefs_select(browser, 0x4150504c, 0x42524f57, &w, &h));
    assert(w == 900 && h == 650);
    assert(window_prefs_select(browser, 0x4150504c, 0x43424f4b, &w, &h));
    assert(w == 700 && h == 500);
    assert(!window_prefs_select(browser, 0x44525652, 0x43424f4b, &w, &h));
    assert(window_prefs_save(850, 550));
    assert(window_prefs_select(coloring, 0x4150504c, 0x43424f4b, &w, &h));
    assert(w == 700 && h == 500);

    /* Corrupt or manually edited values must not reach the video driver. */
    assert(WritePrivateProfileStringA("APPL.CBOK", "Size", "999999999999999999999x600", argv[1]));
    w = 17;
    h = 19;
    assert(!window_prefs_select(coloring, 0x4150504c, 0x43424f4b, &w, &h));
    assert(w == 17 && h == 19);
    assert(window_prefs_save(1024, 768));
    assert(window_prefs_select(coloring, 0x4150504c, 0x43424f4b, &w, &h));
    assert(w == 1024 && h == 768);
    {
        int x = 17, y = 19;
        assert(window_location_parse("-1920,100", &x, &y));
        assert(x == -1920 && y == 100);
        assert(!window_location_parse("1,2junk", &x, &y));
        assert(!window_location_parse("999999999999999,2", &x, &y));
        assert(!window_location_parse("1, 2", &x, &y));
        assert(x == -1920 && y == 100);
        assert(window_prefs_save_location(-1280, 75));
        window_prefs_init(argv[1], 512, 342, 4096, 4096);
        assert(window_prefs_select(browser, 0x4150504c, 0x43424f4b, &w, &h));
        assert(window_prefs_location(&x, &y));
        assert(x == -1280 && y == 75);
        assert(!window_prefs_save_location(1000001, 0));
        assert(WritePrivateProfileStringA("APPL.0x11223344", "Size", "777x555", argv[1]));
        assert(window_prefs_select(browser, 0x4150504c, 0x11223344, &w, &h));
        assert(w == 777 && h == 555);
    }
    /* A memory-profile restart exits with the bootstrap window still at its
       default geometry. Its resize/shutdown callbacks must preserve the saved
       application geometry for the replacement ROMAW process. */
    {
        int x = 0, y = 0;
        window_prefs_select(coloring, 0x4150504c, 0x43424f4b, &w, &h);
        assert(window_prefs_save(900, 650));
        assert(window_prefs_save_location(100, 120));
        window_prefs_set_saving_enabled(0);
        assert(!window_prefs_save(640, 480));
        assert(!window_prefs_save_location(1592, 808));
        window_prefs_init(argv[1], 512, 342, 4096, 4096);
        assert(window_prefs_select(coloring, 0x4150504c, 0x43424f4b, &w, &h));
        assert(w == 900 && h == 650);
        assert(window_prefs_location(&x, &y) && x == 100 && y == 120);
        assert(!window_prefs_save(640, 480));
        window_prefs_set_saving_enabled(1);
        assert(window_prefs_save(950, 700));
        assert(window_prefs_save_location(150, 170));
        assert(window_prefs_select(coloring, 0x4150504c, 0x43424f4b, &w, &h));
        assert(w == 950 && h == 700);
        assert(window_prefs_location(&x, &y) && x == 150 && y == 170);
        window_prefs_select(browser, 0x4150504c, 0x11223344, &w, &h);
    }
    /* Open and Save retain independent geometry across launches and apps. */
    {
        int x = 3, y = 5;
        assert(file_dialog_prefs_save(0, 600, 420, 80, 70));
        assert(file_dialog_prefs_save(1, 500, 340, 120, 90));
        window_prefs_init(argv[1], 512, 342, 4096, 4096);
        window_prefs_select(browser, 0x4150504c, 0x11223344, &w, &h);
        assert(file_dialog_prefs_load(0, &w, &h, &x, &y));
        assert(w == 600 && h == 420 && x == 80 && y == 70);
        assert(file_dialog_prefs_load(1, &w, &h, &x, &y));
        assert(w == 500 && h == 340 && x == 120 && y == 90);
        assert(!file_dialog_prefs_save(0, 0, 420, 80, 70));
        assert(!file_dialog_prefs_save(0, 600, 420, 1000001, 70));
        assert(WritePrivateProfileStringA("APPL.0x11223344", "OpenDialogBounds", "600x420@3,4junk", argv[1]));
        assert(!file_dialog_prefs_load(0, &w, &h, &x, &y));
        assert(w == 500 && h == 340 && x == 120 && y == 90);
        window_prefs_select(coloring, 0x4150504c, 0x43424f4b, &w, &h);
        assert(!file_dialog_prefs_load(1, &w, &h, &x, &y));
    }
    puts("Window preferences: per-app persistence, reload, bounds and malformed data passed");
    return 0;
}
