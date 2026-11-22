/*+===================================================================
  File:      main.c

  Summary:   Implements main support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#include "syn68k_public.h"
#include "driver.h"
#include "testtrap.h"
#include "crc.h"
#include "testrt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif

int main(int argc, char* argv[])
{
    static uint32 trap_vectors[64];
    uint32 count = 10000;
    int i;
    int native_p;
    int report_native = 0;
    int report_reference = 0;

    native_p = 1;

    ROMlib_offset = MEMORY_OFFSET;

    /* Set up default values for command line switches. */
    test_only_non_cc_variants = 0;
    generate_crc = 1;

    /* Grab command-line switches. */
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-noncc"))
            test_only_non_cc_variants = 1;
        else if (strcmp(argv[i], "-notnative") == 0)
            native_p = 0;
        else if (strcmp(argv[i], "--native-code-enabled") == 0)
            report_native = 1;
        else if (strcmp(argv[i], "--reference-native-flags") == 0)
            report_native = report_reference = 1;
        else if (atoi(argv[i]) != 0)
            count = atoi(argv[i]);
        else {
            fprintf(stderr, "Usage: %s [test count] [-noncc] [-notnative]\n", argv[0]);
            exit(-1);
        }
    }

    /* Initialize stuff. */
#if defined(__MINGW32__)
    if (!report_native)
        reserve_reference_test_memory();
#endif
    /* This legacy CRC driver uses scattered raw host-pointer guest addresses
     and generates only mapped operands. Explicit bounded-memory/fault checks
     use separate fixtures with real declared RAM extents. Reserve high guest
     aliases before initialization so Win64 callback and RTE addresses retain
     their full host pointers. Keep those mappings alive throughout the run. */
    syn68k_set_memory_window(0xFF000000U, 0, 0);
    initialize_68k_emulator(NULL, native_p, trap_vectors, 0);
    if (report_native) {
        int native = syn68k_native_code_enabled();
        /* Historical CRCs encode the IA-32 backend's undefined DIVS flags.
         The x64 backend retains interpreter semantics for DIVS fallback. */
        if (report_reference && sizeof(void*) == 8)
            native = 0;
        printf("%d\n", native);
        return EXIT_SUCCESS;
    }

#if 0
  test_rangetree ();
  exit (0);
#endif

#if 0
  test_traps ();
#else
    /* Run through test suite. */
    test_all_instructions(count);
#endif

    return EXIT_SUCCESS;
}
