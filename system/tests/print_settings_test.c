/*+===================================================================
  File:      print_settings_test.c

  Summary:   Test the actual paper-default calculation without opening a print dialog.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Test the actual paper-default calculation without opening a print dialog. */
#include "../../printing/prLowLevel.c"
#undef main
#include <assert.h>

int ROMlib_paper_x, ROMlib_paper_y;
int ROMlib_rotation, ROMlib_translate_x, ROMlib_translate_y;
char *ROMlib_document_paper_sizes, *ROMlib_paper_size;
const char *ROMlib_paper_size_name, *ROMlib_paper_size_name_terminator;

int main(void)
{
    update_printing_globals();
    assert(ROMlib_paper_x == 612 && ROMlib_paper_y == 792);
    assert(ROMlib_rotation == 0 && ROMlib_translate_x == 0 && ROMlib_translate_y == 792);
    assert(!strcmp(ROMlib_paper_orientation, "Portrait"));
    assert(!strcmp(ROMlib_paper_size_name, "Letter"));
    ROMlib_paper_orientation = "Landscape";
    update_printing_globals();
    assert(ROMlib_paper_x == 792 && ROMlib_paper_y == 612);
    assert(ROMlib_rotation == -90 && ROMlib_translate_x == -792 && ROMlib_translate_y == 612);
    ROMlib_paper_orientation = NULL;
    update_printing_globals();
    assert(ROMlib_paper_x == 612 && ROMlib_paper_y == 792);
    ROMlib_native_paper_width = 595;
    ROMlib_native_paper_height = 842;
    ROMlib_print_margins[0] = 18;
    ROMlib_print_margins[1] = 24;
    ROMlib_paper_orientation = "Landscape";
    update_printing_globals();
    assert(ROMlib_paper_x == 595 && ROMlib_paper_y == 842);
    assert(ROMlib_rotation == 0 && ROMlib_translate_x == 18 && ROMlib_translate_y == 818);
    ROMlib_native_paper_width = 842;
    ROMlib_native_paper_height = 595;
    update_printing_globals();
    assert(ROMlib_paper_x == 842 && ROMlib_paper_y == 595);
    assert(ROMlib_rotation == 0 && ROMlib_translate_x == 18 && ROMlib_translate_y == 571);
    puts("Printing: initial paper defaults, orientation and host dimensions passed");
    return 0;
}
