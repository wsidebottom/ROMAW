/*+===================================================================
  File:      font_cache_test.c

  Summary:   Verify released font resources invalidate dependent cached metrics.

  Classes:   None.

  Functions: DisposHandle, GetHandleSize, HLock, HUnlock, main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Regression fixture for Executor's font-resource lifetime handling.

  Copyright and Legal notices: See the repository license.
===================================================================+*/

#define NDEBUG 1
#include "../../text/font.c"
#undef main
#undef NDEBUG
#include <assert.h>
#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;
static Handle list;
static unsigned disposed;

void* syn68k_guest_to_host(uint32 address)
{
    return fixture_guest_to_host(address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return fixture_host_to_guest(address);
}

Size GetHandleSize(Handle handle)
{
    assert(handle == list);
    return 2 * sizeof(HIDDEN_Handle);
}

void HLock(Handle handle)
{
    assert(handle == list);
}

void HUnlock(Handle handle)
{
    assert(handle == list);
}

void DisposHandle(Handle handle)
{
    if (handle) {
        ++disposed;
        HIDDEN_RAW_SET(*handle, 0);
    }
}

int main(void)
{
    unsigned char* memory = calloc(1, 65536);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    Handle resource = (Handle)(memory + 0x1200);
    WHandle width = (WHandle)(memory + 0x1100);
    WPtr table = (WPtr)(memory + 0x3000);
    list = (Handle)(memory + 0x1000);
    HIDDEN_Handle* entries = (HIDDEN_Handle*)(memory + 0x2000);
    HIDDEN_RAW_SET(*list, RM((Ptr)entries));
    HIDDEN_RAW_SET(WidthListHand_H, RM(list));
    for (unsigned mode = 0; mode < 3; ++mode) {
        memset(table, 0, sizeof *table);
        memset(&ROMlib_fmo, 0, sizeof ROMlib_fmo);
        HIDDEN_RAW_SET(*width, RM(table));
        HIDDEN_RAW_SET(entries[0], RM(width));
        HIDDEN_RAW_SET(entries[1], 0);
        ROMlib_myfmi.family = CWC(12);
        if (mode == 0)
            PP_ASSIGN(table->tabFont, resource);
        else if (mode == 1)
            PP_ASSIGN(table->fHand, resource);
        else
            PP_ASSIGN(ROMlib_fmo.fontHandle, resource);
        unsigned before = disposed;
        ROMlib_forget_font_resource((Handle)(memory + 0x1300));
        assert(disposed == before && HIDDEN_RAW(entries[0]));
        ROMlib_forget_font_resource(resource);
        assert(disposed == before + 1 && !HIDDEN_RAW(entries[0]));
        assert(ROMlib_myfmi.family == -1 && !PP_RAW(ROMlib_fmo.fontHandle) && !LastFOND);
    }
    HIDDEN_RAW_SET(WidthListHand_H, 0);
    ROMlib_forget_font_resource(resource);
    ROMlib_forget_font_resource(NULL);
    free(memory);
    puts("PASS: font/ FOND resource disposal invalidates cached metrics before handle reuse");
    return 0;
}
