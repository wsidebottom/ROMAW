/*+===================================================================
  File:      dialog_filter_test.c

  Summary:   Check that GetStdFilterProc writes a four-byte guest pointer without corrupting neighboring data.

  Classes:   None.

  Functions: main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Regression fixture for SimpleText and Kid Pix dialog callback stack corruption on Windows x64.

  Copyright and Legal notices: See the repository license.
===================================================================+*/

#define NDEBUG 1
#include "../../dialogs/dialDispatch.c"
#undef main
#undef NDEBUG
#include <assert.h>
#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;
toolstuff_t pstuff[3];

void* syn68k_guest_to_host(uint32 address)
{
    return fixture_guest_to_host(address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return fixture_host_to_guest(address);
}

int main(void)
{
    unsigned char memory[64];
    ROMlib_offset = (uintptr_t)memory;
    memset(memory, 0xa5, sizeof memory);
    pstuff[2].orig = memory + 48;
    HIDDEN_ProcPtr* output = (HIDDEN_ProcPtr*)(memory + 16);
    assert(sizeof *output == 4);
    assert(C_GetStdFilterProc(output) == noErr);
    assert(memory[16] == 0 && memory[17] == 0 && memory[18] == 0 && memory[19] == 48);
    for (unsigned i = 0; i < sizeof memory; ++i)
        if (i < 16 || i >= 20)
            assert(memory[i] == 0xa5);
    assert(C_GetStdFilterProc(NULL) == paramErr);
    puts("PASS: GetStdFilterProc guest pointer width, byte order and output guards");
    return 0;
}
