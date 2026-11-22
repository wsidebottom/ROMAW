/*+===================================================================
  File:      testtrap.c

  Summary:   Implements testtrap support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: test_traps

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#include "testtrap.h"
#include "syn68k_public.h"
#include <stdio.h>

static void test_callbacks(void);

void test_traps()
{
    test_callbacks();
}

static uint32 handle_callback(uint32 addr, void* arg)
{
    printf("Callback: addr == 0x%08lX, arg == %p\n", addr, arg);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void test_callbacks()
{
    uint32 callback_addr[10];
    int i, j;

    for (i = 0; i < 10; i++)
        callback_addr[i] = callback_install(handle_callback, (void*)i);

    for (j = 0; j < 2; j++)
        for (i = 0; i < 10; i++)
            interpret_code(hash_lookup_code_and_create_if_needed(callback_addr[i]));
}
