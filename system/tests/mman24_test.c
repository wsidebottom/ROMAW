/*+===================================================================
  File:      mman24_test.c

  Summary:   Exercise handle allocation, relocation and heap layout in both addressing modes.

  Classes:   None.

  Functions: CToPascalCall, C_GetResInfo, ROMlib_destroy_blocks, _gui_fatal, _warning, error_set_enabled,
             gestalt_set_memory_size, main, system_error, vdriver_shutdown

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Exercise handle allocation, relocation and heap layout in both addressing modes. */
#include "rsys/common.h"
#include "MemoryMgr.h"
#include "rsys/mman.h"
#include "rsys/mman_private.h"
#include "ResourceMgr.h"
#include "rsys/system_error.h"
#undef main
#undef NDEBUG
#include <assert.h>

/* UI, resource diagnostics and patched-trap hooks are outside this heap test. */
uint32_t ROMlib_debug_level;
unsigned short mostrecenttrap;
LONGINT ROMlib_creator;
int ROMlib_options;
StringHandle ROMlib_phoney_name_string;
const char NULL_STRING[] = "";
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];

bool error_set_enabled(int err, bool enabled_p)
{
    return FALSE;
}

void gestalt_set_memory_size(uint32 size)
{
}

void vdriver_shutdown(void)
{
}

unsigned long ROMlib_destroy_blocks(syn68k_addr_t start, uint32 count, BOOLEAN flush_only)
{
    return 0;
}

intptr_t CToPascalCall(void* proc, unsigned long magic, ...)
{
    abort();
}

void C_GetResInfo(Handle h, INTEGER* id, ResType* type, StringPtr name)
{
    abort();
}

void _warning(int err, const char* type, const char* file, int line, const char* fn, const char* fmt, ...)
{
}

void _gui_fatal(const char* file, int line, const char* fn, const char* fmt, ...)
{
    fprintf(stderr, "Heap failure at %s:%d in %s: %s\n", file, line, fn, fmt);
    abort();
}

int system_error(const char* message, int def, const char* b0, const char* b1, const char* b2,
                 system_error_callback_t f0, system_error_callback_t f1, system_error_callback_t f2)
{
    abort();
}

/* Use the bytes read by MacWrite's 68000 heap walk, not allocator macros. */
static uint32 read_be32(const unsigned char* p)
{
    return ((uint32)p[0] << 24) | ((uint32)p[1] << 16) | ((uint32)p[2] << 8) | p[3];
}

static void check_guest_heap_walk(THz zone)
{
    unsigned char* p = (unsigned char*)zone + 0x34;
    unsigned char* end = (unsigned char*)zone + (read_be32((unsigned char*)zone) - US_TO_SYN68K(zone));
    unsigned count = 0;
    while (p < end) {
        uint32 word = read_be32(p), size = word & 0x00FFFFFFU;
        assert(++count < 10000);
        assert(size >= 12 && size <= (uint32)(end - p));
        if ((word >> 30) == 2) {
            uint32 offset = read_be32(p + 4);
            unsigned char* master = (unsigned char*)zone + offset;
            assert((read_be32(master) & 0x00FFFFFFU) == US_TO_SYN68K(p + 12));
        }
        p += size;
    }
    assert(p == end && count > 0);
}

int main(int argc, char** argv)
{
    extern __declspec(dllimport) void __stdcall Sleep(unsigned long);
    (void)argv;
    if (argc > 1)
        Sleep(5000);
    unsigned char* memory = calloc(1, 16 * 1024 * 1024);
    Handle h, blocker;
    Ptr original_data;
    assert(memory);
    {
        block_header_t block = {0};
        THz zone = (THz)(memory + 0x100000);
        Handle master = (Handle)(memory + 0x10000);
        block.location_u = CL((uint32)((char*)master - (char*)zone));
        assert(BLOCK_TO_HANDLE(zone, &block) == master);
        assert((THz)((char*)master - BLOCK_LOCATION_OFFSET(&block)) == zone);
    }
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(16 * 1024 * 1024, 0x400000, 0x400000);
    HIDDEN_RAW_SET(SysZone_H, RM((THz)(memory + 0x10000)));
    HIDDEN_RAW_SET(ApplZone_H, RM((THz)(memory + 0x100000)));
    ROMlib_syszone = (uintptr_t)memory + 0x10000;
    ROMlib_memtop = (uintptr_t)memory + 0x300000;
    InitZone(NULL, 64, (Ptr)memory + 0xF0000, MR(SysZone));
    InitZone(NULL, 64, (Ptr)memory + 0x300000, MR(ApplZone));
    HIDDEN_RAW_SET(TheZone_H, ApplZone);
    {
        THz saved = TheZone;
        ReallocHandle(NULL, 128);
        assert(MemError() == nilHandleErr && TheZone == saved);
        ReallocHandle((Handle)(memory + 0x400000), 128);
        assert(MemError() == memAZErr && TheZone == saved);
    }
    syn68k_address_mask = 0x00FFFFFF;
    {
        THz saved_zone = TheZone;
        Handle low = NewHandleSys(4096);
        Handle high = NewHandleSys(4096);
        assert(low && high);
        uint32 free_only = ROMlib_system_high_free_bytes();
        uint32 low_size = PSIZE(HANDLE_TO_BLOCK(low));
        uint32 high_size = PSIZE(HANDLE_TO_BLOCK(high));
        HPurge(low);
        assert(ROMlib_system_high_free_bytes() == free_only + low_size);
        HLock(high);
        assert(ROMlib_system_high_free_bytes() == free_only);
        HUnlock(high);
        HPurge(high);
        assert(ROMlib_system_high_free_bytes() == free_only + low_size + high_size);
        ROMlib_sync_handle_flags(TRUE);
        syn68k_address_mask = 0xFFFFFFFF;
        assert(ROMlib_system_high_free_bytes() == free_only + low_size + high_size);
        HLock(high);
        assert(ROMlib_system_high_free_bytes() == free_only);
        ROMlib_sync_handle_flags(FALSE);
        syn68k_address_mask = 0x00FFFFFF;
        HUnlock(high);
        DisposHandle(high);
        DisposHandle(low);
        assert(TheZone == saved_zone);
    }
    h = NewHandle(32);
    assert(h && STARH(h));
    memset(STARH(h), 0xA5, 32);
    HLock(h);
    assert((CL((uint32)HIDDEN_RAW(*h)) >> 24) & LOCKBIT);
    assert(HGetState(h) & LOCKBIT);
    assert((unsigned char)STARH(h)[0] == 0xA5);
    HUnlock(h);
    HPurge(h);
    original_data = STARH(h);
    blocker = NewHandle(65536);
    assert(blocker && STARH(blocker));
    HLock(blocker);
    check_guest_heap_walk(MR(SysZone));
    check_guest_heap_walk(MR(ApplZone));
    SetHandleSize(h, 4096);
    assert(MemError() == noErr && GetHandleSize(h) == 4096);
    assert(STARH(h) != original_data && (HGetState(h) & PURGEBIT));
    assert((unsigned char)STARH(h)[0] == 0xA5);
    HPurge(h);
    HSetRBit(h);
    assert((CL((uint32)HIDDEN_RAW(*h)) >> 24) == (PURGEBIT | RSRCBIT));
    ROMlib_sync_handle_flags(TRUE);
    syn68k_address_mask = 0xFFFFFFFF;
    assert(!(CL((uint32)HIDDEN_RAW(*h)) >> 24));
    assert(HGetState(h) == (PURGEBIT | RSRCBIT));
    ROMlib_sync_handle_flags(FALSE);
    syn68k_address_mask = 0x00FFFFFF;
    assert((CL((uint32)HIDDEN_RAW(*h)) >> 24) == (PURGEBIT | RSRCBIT));
    HClrRBit(h);
    EmptyHandle(h);
    assert(MemError() == noErr && !HIDDEN_RAW(*h) && !STARH(h));
    ReallocHandle(h, 128);
    assert(MemError() == noErr && STARH(h) && GetHandleSize(h) == 128);
    DisposHandle(h);
    assert(MemError() == noErr);
    HUnlock(blocker);
    DisposHandle(blocker);
    assert(MemError() == noErr);
    check_guest_heap_walk(MR(ApplZone));
    {
        block_header_t header;
        memset(&header, 0, sizeof header);
        SETUSE(&header, REL);
        SETPSIZE(&header, 0x7FFFFF00U);
        assert(PSIZE(&header) == 0x7FFFFF00U && USE(&header) == REL);
        SETPSIZE(&header, 0x1234);
        assert(PSIZE(&header) == 0x1234 && USE(&header) == REL);
        assert((read_be32((unsigned char*)&header) & 0xFFFFFF) == 0x1234);
    }
    for (unsigned mode = 0; mode < 2; ++mode) {
        syn68k_address_mask = mode ? 0xffffffff : 0xffffff;
        HIDDEN_RAW_SET(TheZone_H, ApplZone);
        InitZone(NULL, 64, (Ptr)memory + 0x300000, MR(ApplZone));
        h = NewHandle(1024);
        blocker = NewHandle(256);
        assert(h && blocker && STARH(blocker) > STARH(h));
        memset(STARH(blocker), 0x6b, 256);
        original_data = STARH(blocker);
        HLock(blocker);
        ROMlib_MoveHLow(blocker);
        assert(MemError() == memLockedErr && STARH(blocker) == original_data);
        HUnlock(blocker);
        HPurge(blocker);
        HSetRBit(blocker);
        DisposHandle(h);
        HIDDEN_RAW_SET(TheZone_H, SysZone);
        ROMlib_MoveHLow(blocker);
        assert(MemError() == noErr && TheZone == SysZone);
        assert(STARH(blocker) < original_data && GetHandleSize(blocker) == 256);
        assert(HGetState(blocker) == (PURGEBIT | RSRCBIT));
        for (unsigned i = 0; i < 256; ++i)
            assert((unsigned char)STARH(blocker)[i] == 0x6b);
        check_guest_heap_walk(MR(ApplZone));
        EmptyHandle(blocker);
        ROMlib_MoveHLow(blocker);
        assert(MemError() == nilHandleErr);
        DisposHandle(blocker);
        ROMlib_MoveHLow(NULL);
        assert(MemError() == nilHandleErr);
    }
    for (unsigned mode = 0; mode < 2; ++mode) {
        syn68k_address_mask = mode ? 0xffffffff : 0xffffff;
        InitZone(NULL, 64, (Ptr)memory + 0x20000, MR(SysZone));
        Size available = FreeMem();
        if (mode && (available & 15)) {
            /* Give the tail case room for exactly one aligned handle. */
            InitZone(NULL, 64, (Ptr)memory + 0x20000 - (available & 15), MR(SysZone));
            available = FreeMem();
        }
        h = NewHandle(available - HDRSIZE);
        assert(h && MemError() == noErr);
        block_header_t* tail = HANDLE_TO_BLOCK(h);
        assert(BLOCK_NEXT(tail) == ZONE_BK_LIM(MR(SysZone)));
        Size size = GetHandleSize(h);
        unsigned char sentinel[MIN_BLOCK_SIZE];
        memcpy(sentinel, ZONE_BK_LIM(MR(SysZone)), sizeof sentinel);
        ReallocHandle(h, size + 4);
        assert(MemError() == memFullErr);
        assert(GetHandleSize(h) == size);
        assert(!memcmp(sentinel, ZONE_BK_LIM(MR(SysZone)), sizeof sentinel));
        check_guest_heap_walk(MR(SysZone));
        DisposHandle(h);
        h = NewHandle(100);
        blocker = NewHandle(40);
        DisposHandle(blocker);
        ReallocHandle(h, 200);
        assert(MemError() == noErr && GetHandleSize(h) == 200);
        check_guest_heap_walk(MR(SysZone));
        DisposHandle(h);
    }
    {
        InitZone(NULL, 64, (Ptr)memory + 0x300000, MR(ApplZone));
        THz zone = MR(ApplZone);
        Handle gap = NewHandle(128);
        h = NewHandle(56);
        blocker = NewHandle(16);
        assert(gap && h && blocker && GetHandleSize(h) == 56);
        block_header_t* block = HANDLE_TO_BLOCK(h);
        assert(PSIZE(block) == 80 && BLOCK_NEXT(block) == HANDLE_TO_BLOCK(blocker));

        /* FMSharedCaches reads a 36-byte font fragment at offset 24. */
        unsigned char fragment[36];
        for (unsigned i = 0; i < sizeof fragment; ++i)
            fragment[i] = i;
        block_header_t next_header = *BLOCK_NEXT(block);
        memcpy(STARH(h) + 24, fragment, sizeof fragment);
        assert(!memcmp(&next_header, BLOCK_NEXT(block), sizeof next_header));

        original_data = STARH(h);
        DisposHandle(gap);
        ROMlib_MoveHLow(h);
        assert(STARH(h) < original_data && GetHandleSize(h) == 56);
        assert(!memcmp(STARH(h) + 24, fragment, sizeof fragment));
        SetHandleSize(h, 65);
        assert(MemError() == noErr && GetHandleSize(h) == 65);
        assert(PSIZE(HANDLE_TO_BLOCK(h)) >= HDRSIZE + 65);
        assert(!memcmp(STARH(h) + 24, fragment, sizeof fragment));

        EmptyHandle(h);
        ReallocHandle(h, 56);
        assert(MemError() == noErr && GetHandleSize(h) == 56 && PSIZE(HANDLE_TO_BLOCK(h)) >= 80);
        memcpy(STARH(h) + 24, fragment, sizeof fragment);
        DisposHandle(blocker);
        ZONE_ALLOC_PTR_X(zone) = CLC_NULL;
        CompactMem(INT32_MAX);
        assert(GetHandleSize(h) == 56 && !memcmp(STARH(h) + 24, fragment, sizeof fragment));
        SetHandleSize(h, INT32_MAX);
        assert(MemError() == memFullErr && GetHandleSize(h) == 56);
        ReallocHandle(h, INT32_MAX);
        assert(MemError() == memFullErr && GetHandleSize(h) == 56);
        DisposHandle(h);
        assert(NewHandle(INT32_MAX) == NULL && MemError() == memFullErr);
        check_guest_heap_walk(zone);
    }
    puts("24-bit and 32-bit memory, relocation and heap-end growth tests passed");
    return 0;
}
