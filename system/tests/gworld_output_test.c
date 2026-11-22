/*+===================================================================
  File:      gworld_output_test.c

  Summary:   GetGWorld writes two guest pointers beside a saved Macintosh stack frame.

  Classes:   None.

  Functions: main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* GetGWorld writes two guest pointers beside a saved Macintosh stack frame. */
#define NDEBUG 1
#include "../../quickdraw/qGWorld.c"
#undef main
#undef NDEBUG
#include <assert.h>

uint64 ROMlib_offset;
CPUState cpu_state;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;

void* syn68k_guest_to_host(uint32 address)
{
    return (void*)(uintptr_t)(ROMlib_offset + address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return address - ROMlib_offset;
}

int main(void)
{
    unsigned char* memory = calloc(1, 65536);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    EM_A5 = 0x4000;
    HIDDEN_HIDDEN_CGrafPtr_Ptr* port_handle = (void*)(memory + 0x4000);
    HIDDEN_CGrafPtr* port = (void*)(memory + 0x4100);
    PP_ASSIGN(*port_handle, port);
    PP_ASSIGN(*port, (CGrafPtr)(memory + 0x5000));
    PP_ASSIGN(TheGDevice_H, (GDHandle)(memory + 0x6000));

    struct PACKED {
        uint16 before;
        HIDDEN_CGrafPtr port;
        HIDDEN_GDHandle device;
        uint32 saved_frame;
    } outputs;

    memset(&outputs, 0xa5, sizeof outputs);
    C_GetGWorld(&outputs.port, &outputs.device);
    assert(HIDDEN_NATIVE(outputs.port) == (CGrafPtr)(memory + 0x5000));
    assert(HIDDEN_NATIVE(outputs.device) == (GDHandle)(memory + 0x6000));
    assert(outputs.before == 0xa5a5 && outputs.saved_frame == 0xa5a5a5a5);
    HIDDEN_RAW_SET(*port, 0);
    HIDDEN_RAW_SET(TheGDevice_H, 0);
    C_GetGWorld(&outputs.port, &outputs.device);
    assert(!PP_RAW(outputs.port) && !PP_RAW(outputs.device));
    assert(outputs.before == 0xa5a5 && outputs.saved_frame == 0xa5a5a5a5);
    free(memory);
    puts("GetGWorld: guest outputs, null pointers and adjacent frame preserved");
    return 0;
}
