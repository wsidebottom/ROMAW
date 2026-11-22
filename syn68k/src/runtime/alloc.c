/*+===================================================================
  File:      alloc.c

  Summary:   Implements alloc support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: xcalloc, xmake_executable, xmalloc, xrealloc

  Origin:    Existing repository source in syn68k/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#include "alloc.h"
#include "destroyblock.h"
#include <stdio.h>
#include <stdlib.h>

#if defined(__MINGW32__)
typedef unsigned long win_dword_t;
extern __declspec(dllimport) int __stdcall VirtualProtect(void*, size_t, win_dword_t, win_dword_t*);
extern __declspec(dllimport) win_dword_t __stdcall GetLastError(void);
#define PAGE_EXECUTE_READWRITE 0x40
#endif

void* xmalloc(size_t size)
{
    void* p;

    while ((p = malloc(size)) == NULL) {
        if (!destroy_any_block()) {
            fprintf(stderr, "Out of memory.  Tried to alloc 0x%lX bytes.\n", (unsigned long)size);
            exit(-1);
        }
    }

    return p;
}

void* xrealloc(void* old, size_t new_size)
{
    void* p;

    while ((p = realloc(old, new_size)) == NULL && new_size) {
        if (!destroy_any_block()) {
            fprintf(stderr, "Out of memory.  Tried to realloc 0x%lX bytes.\n", (unsigned long)new_size);
            exit(-1);
        }
    }

    return p;
}

void* xcalloc(size_t num_elems, size_t byte_size)
{
    void* p;

    while ((p = calloc(num_elems, byte_size)) == NULL) {
        if (!destroy_any_block()) {
            fprintf(stderr, "Out of memory.  Tried to calloc 0x%lX bytes.\n", (unsigned long)(num_elems * byte_size));
            exit(-1);
        }
    }

    return p;
}

void xmake_executable(const void* p, size_t size)
{
#if defined(__MINGW32__)
    win_dword_t old_protect;

    if (!VirtualProtect((void*)p, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        fprintf(stderr, "Unable to make translated code executable; error %lu.\n", (unsigned long)GetLastError());
        exit(-1);
    }
#else
    (void)p;
    (void)size;
#endif
}
