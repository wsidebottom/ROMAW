/*+===================================================================
  File:      bus-read-test.c

  Summary:   Verify bounded data-bus callbacks through decoded and native 68K execution.

  Classes:   None.

  Functions: main

  Origin:    Original regression fixture for Executor's fixed-address PowerPC information view.

  Copyright and Legal notices: See syn68k/COPYING.
===================================================================+*/

#include "ea.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned reads, trapped;

static int read_bus(uint32 address, unsigned size, uint32* value)
{
    ++reads;
    if (address != 0x5fffeff0 || size != 4)
        return 0;
    *value = 0x12345678;
    return 1;
}

static syn68k_addr_t bus_fault(syn68k_addr_t pc, void* argument)
{
    (void)argument;
    assert(pc == 0x1020);
    assert(READUL(EM_A7 + 20) == 0x5fffeff0);
    ++trapped;
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

int main(int argc, char** argv)
{
    void* memory = VirtualAlloc(NULL, 1024 * 1024, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(1024 * 1024, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, argc > 1 && atoi(argv[1]), vectors, 0);
    syn68k_bus_read = read_bus;
    cpu_state.sr = 0x2000;
    Syn68kEAContext context = {0};
    assert(syn68k_ea_read_memory(&context, 0x5fffeff0, 4) == 0x12345678 && !context.error);
    assert(reads == 1);
    WRITEUL(0x2000, 42);
    assert(syn68k_ea_read_memory(&context, 0x2000, 4) == 42 && reads == 1);
    /* Instruction reads, writes, invalid sizes, failed contexts and wrapping
       transfers must never reach the data-bus handler. */
    assert(!syn68k_ea_fetch_memory(&context, 0x5fffeff0, 4) && context.error == 2 && reads == 1);
    assert(!syn68k_ea_read_memory(&context, 0x5fffeff0, 4) && reads == 1);
    context = (Syn68kEAContext){0};
    syn68k_ea_write_memory(&context, 0x5fffeff0, 4, 0);
    assert(context.error == 2 && reads == 1);
    context = (Syn68kEAContext){0};
    assert(!syn68k_ea_read_memory(&context, 0x5fffeff0, 3) && context.error == 11 && reads == 1);
    context = (Syn68kEAContext){0};
    assert(!syn68k_ea_read_memory(&context, 0xfffffffe, 4) && context.error == 2 && reads == 1);
    context = (Syn68kEAContext){0};
    assert(!syn68k_ea_read_memory(&context, 0x5fffeff4, 4) && context.error == 2 && reads == 2);
    /* A 24-bit tagged address must not select the 32-bit device window. */
    syn68k_set_addressing_mode(0);
    context = (Syn68kEAContext){0};
    assert(!syn68k_ea_read_memory(&context, 0x5fffeff0, 4) && context.error == 2 && reads == 3);
    syn68k_set_addressing_mode(1);
    for (unsigned i = 0; i < 2; ++i) {
        /* movea.l (a0),a1; move.l (a0),d0; move.l d0,(a2); rts */
        WRITEUW(0x1000, 0x2250);
        WRITEUW(0x1002, 0x2010);
        WRITEUW(0x1004, 0x2480);
        WRITEUW(0x1006, 0x4e75);
        EM_A0 = 0x5fffeff0;
        EM_A2 = 0x2100;
        EM_A7 = 0x8000;
        WRITEUL(EM_A7, MAGIC_EXIT_EMULATOR_ADDRESS);
        interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
        assert(EM_A1 == 0x12345678 && EM_D0 == 0x12345678 && READUL(0x2100) == 0x12345678);
    }
    trap_install_handler(2, bus_fault, NULL);
    WRITEUW(0x1020, 0x2080); /* move.l d0,(a0): remains a bus error. */
    EM_A7 = 0x8000;
    interpret_code(hash_lookup_code_and_create_if_needed(0x1020));
    assert(trapped == 1);
    syn68k_bus_read = NULL;
    context = (Syn68kEAContext){0};
    assert(!syn68k_ea_read_memory(&context, 0x5fffeff0, 4) && context.error == 2);
    printf("Bounded read-only data bus passed (%s)\n", syn68k_native_code_enabled() ? "native" : "interpreter");
    VirtualFree(memory, 0, MEM_RELEASE);
    return 0;
}
