/*+===================================================================
  File:      callback_width_test.c

  Summary:   Exercise the real classic callback bridge with host pointers above 4 GB.

  Classes:   None.

  Functions: ROMlib_ppc_callback, main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Exercise the real classic callback bridge with host pointers above 4 GB. */
#include "rsys/common.h"
#include "rsys/trapglue.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#undef main

extern __declspec(dllimport) void* __stdcall VirtualAlloc(void*, size_t, unsigned long, unsigned long);
extern __declspec(dllimport) int __stdcall VirtualFree(void*, size_t, unsigned long);
extern __declspec(dllimport) void __stdcall Sleep(unsigned long);

int ROMlib_ppc_callback(void* procedure, uint64 magic, va_list arguments, intptr_t* result)
{
    (void)procedure;
    (void)magic;
    (void)arguments;
    (void)result;
    return 0;
}

static void* expected;
static unsigned calls;

static void* echo_pointer(void* pointer)
{
    assert(pointer == expected);
    ++calls;
    return pointer;
}

static LONGINT negative_result(void)
{
    return -123;
}

int main(int argc, char** argv)
{
    (void)argv;
    if (argc > 1)
        Sleep(5000);
    void* requested = NULL;
#ifdef _WIN64
    requested = (void*)(uintptr_t)UINT64_C(0x120000000);
#endif
    void* memory = VirtualAlloc(requested, 65536, 0x3000, 4);
    assert(memory);
#ifdef _WIN64
    assert((uintptr_t)memory > UINT32_MAX);
#endif
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(65536, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 0, vectors, 0);
    expected = (char*)memory + 0x2340;
    ptocblock_t procedure = {(void*)echo_pointer, 5 | (5 << 3)};
    EM_A7 = 0x8000;
    WRITEUL(EM_A7, MAGIC_EXIT_EMULATOR_ADDRESS);
    WRITEUL(EM_A7 + 4, US_TO_SYN68K(expected));
    WRITEUL(EM_A7 + 8, 0);
    assert(PascalToCCall(0, &procedure) == MAGIC_EXIT_EMULATOR_ADDRESS);
    assert(EM_A7 == 0x8008 && calls == 1);
    assert(READUL(EM_A7) == US_TO_SYN68K(expected));
    procedure.wheretogo = (void*)negative_result;
    procedure.magic = 4;
    EM_A7 = 0x8000;
    WRITEUL(EM_A7, MAGIC_EXIT_EMULATOR_ADDRESS);
    PascalToCCall(0, &procedure);
    assert((int32)READUL(EM_A7) == -123);

    /* MOVE.L 4(SP),8(SP); RTD #4: return the Pascal pointer argument. */
    const uint16 code[] = {0x2f6f, 4, 8, 0x4e74, 4};
    for (unsigned i = 0; i < sizeof code / sizeof code[0]; ++i)
        WRITEUW(0x1000 + i * 2, code[i]);
    EM_A7 = 0x9000;
    intptr_t result = CToPascalCall(SYN68K_TO_US(0x1000), 5 | (5 << 3), expected);
    assert(result == (intptr_t)expected && EM_A7 == 0x9000);
    assert(VirtualFree(memory, 0, 0x8000));
    puts("Classic callback host-pointer width checks passed.");
}
