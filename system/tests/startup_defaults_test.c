/*+===================================================================
  File:      startup_defaults_test.c

  Summary:   Verify Startup Manager trap output widths, byte order and register preservation.

  Classes:   None.

  Functions: main, syn68k_guest_to_host, syn68k_host_to_guest.

  Origin:    Regression fixture for System Picker's overwritten saved frame pointer.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#include "rsys/common.h"
#include "StartMgr.h"
#include "rsys/emustubs.h"
#include <assert.h>

CPUState cpu_state;
uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;
unsigned syn68k_host_alias_count;
static uint8 memory[0x4000];

void* syn68k_guest_to_host(uint32 address)
{
    address &= syn68k_address_mask;
    assert(address < sizeof memory);
    return memory + address;
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    assert(address >= ROMlib_offset && address < ROMlib_offset + sizeof memory);
    return address - ROMlib_offset;
}

static void check_defaults(syn68k_addr_t (*getter)(syn68k_addr_t, void**), uint32 address, const uint8* expected,
                           size_t size)
{
    memset(memory, 0xa5, sizeof memory);
    for (unsigned i = 0; i < 16; ++i)
        cpu_state.regs[i].ul.n = 0x11220000 + i;
    EM_A0 = address;
    EM_A7 = 0x3000;
    WRITEUL(EM_A7, 0x12345678);
    M68kReg saved[15];
    memcpy(saved, cpu_state.regs, sizeof saved);
    assert(getter(0, NULL) == 0x12345678);
    assert(EM_A7 == 0x3004);
    assert(!memcmp(saved, cpu_state.regs, sizeof saved));
    uint32 output = address & syn68k_address_mask;
    assert(!memcmp(memory + output, expected, size));
    /* The four bytes after a startup record are the caller's saved A6 in
       System Picker. Check every other byte, including that frame canary. */
    for (unsigned i = 0; i < sizeof memory; ++i)
        if (!(i >= output && i < output + size) && !(i >= 0x3000 && i < 0x3004))
            assert(memory[i] == 0xa5);
}

int main(void)
{
    static const uint8 startup[] = {0xff, 0xff, 0xff, 0xdf};
    static const uint8 video[] = {0, 0xc8};
    static const uint8 os[] = {0, 1};
    assert(sizeof(DefStartRec) == sizeof startup);
    assert(sizeof(DefVideoRec) == sizeof video);
    assert(sizeof(DefOSRec) == sizeof os);
    ROMlib_offset = (uintptr_t)memory;
    for (unsigned mode = 0; mode < 2; ++mode) {
        syn68k_address_mask = mode ? 0xffffff : 0xffffffff;
        for (unsigned location = 0; location < 3; ++location) {
            uint32 address = location == 0 ? 0 : 0x1000 + (location == 2);
            if (mode)
                address |= 0xa5000000;
            check_defaults(_GetDefaultStartup, address, startup, sizeof startup);
            check_defaults(_GetVideoDefault, address, video, sizeof video);
            check_defaults(_GetOSDefault, address, os, sizeof os);
        }
    }
    puts("Startup defaults: exact output sizes, byte order, guards and registers passed");
    return 0;
}
