/*+===================================================================
  File:      guest_pointers_test.c

  Summary:   Four-byte guest fields must round-trip host pointers on both Windows ABIs.

  Classes:   None.

  Functions: main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Four-byte guest fields must round-trip host pointers on both Windows ABIs. */
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "syn68k_public.h"
#define PACKED __attribute__((packed))
#define swap16(n) __builtin_bswap16(n)
#define swap32(n) __builtin_bswap32(n)
#include "rsys/mactype.h"
#include "rsys/byteswap.h"
#include "MacTypes.h"

extern __declspec(dllimport) void* __stdcall VirtualAlloc(void*, size_t, unsigned long, unsigned long);
extern __declspec(dllimport) int __stdcall VirtualFree(void*, size_t, unsigned long);
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_RELEASE 0x8000
#define PAGE_READWRITE 4

#if UINTPTR_MAX > UINT32_MAX
uint64 ROMlib_offset;
#else
uint32 ROMlib_offset;
#endif
uint32 syn68k_address_mask = UINT32_MAX;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;

void* syn68k_guest_to_host(uint32 address)
{
    return SYN68K_TO_US_RAW(address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return US_TO_SYN68K_RAW(address);
}

typedef struct PACKED {
    uint8 tag;
    PACKED_MEMBER(Ptr, pointer);
    uint16 length;
    HIDDEN_Ptr hidden;
} GuestRecord;

static unsigned evaluations;

static Ptr pointer_argument(Ptr pointer)
{
    ++evaluations;
    return pointer;
}

int main(void)
{
    _Static_assert(sizeof(HIDDEN_Ptr) == 4, "guest pointer width");
    _Static_assert(sizeof(GuestRecord) == 11, "guest record layout");
    _Static_assert(offsetof(GuestRecord, pointer) == 1, "pointer offset");
    _Static_assert(offsetof(GuestRecord, length) == 5, "length offset");
    _Static_assert(offsetof(GuestRecord, hidden) == 7, "hidden offset");
    void* requested = NULL;
#ifdef _WIN64
    requested = (void*)(uintptr_t)UINT64_C(0x120000000);
#endif
    Ptr memory = VirtualAlloc(requested, 65536, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    assert(memory);
#ifdef _WIN64
    assert((uintptr_t)memory > UINT32_MAX);
#endif
    ROMlib_offset = (uintptr_t)memory;
    GuestRecord* records = (GuestRecord*)(memory + 128);
    Ptr pointer = memory + 0x1234;
    unsigned index = 0;
    PP_ASSIGN(records[index++].pointer, pointer_argument(pointer));
    assert(index == 1 && evaluations == 1);
    assert(PPR(records[0].pointer) == pointer);

    const uint8* bytes = (const uint8*)&records[0].pointer;
    assert(!bytes[0] && !bytes[1] && bytes[2] == 0x12 && bytes[3] == 0x34);
    HIDDEN_RAW_SET(records[0].hidden, RM(pointer));
    assert(HIDDEN_NATIVE(records[0].hidden) == pointer);
    index = 0;
    evaluations = 0;
    assert(FIELD_ASSIGN_RAW(records[index++].pointer, RM(pointer_argument(pointer))) == RM(pointer));
    assert(index == 1 && evaluations == 1);
    assert(PPR(records[0].pointer) == pointer);

    uint32 numeric_encoded = (uint32)(uintptr_t)RM(pointer);
    assert((uintptr_t)MR(numeric_encoded) == (uintptr_t)pointer);
    Ptr encoded = NULL;
    FIELD_ASSIGN_RAW(encoded, records[0].pointer);
    assert(MR(encoded) == pointer);
    uint16 narrow = 0;
    FIELD_ASSIGN_RAW(narrow, (uint32)0x12345678);
    assert(narrow == 0x5678);
    Handle handle = (Handle)(memory + 256);
    HIDDEN_RAW_SET(*handle, RM(pointer));
    assert(STARH(handle) == pointer);
    index = 0;
    assert(HIDDEN_NATIVE(records[index++].hidden) == pointer && index == 1);
    PP_ASSIGN(records[0].pointer, NULL);
    HIDDEN_RAW_SET(records[0].hidden, 0);
    HIDDEN_RAW_SET(*handle, 0);
    assert(!PPR(records[0].pointer) && !STARH(handle));
    assert(VirtualFree(memory, 0, MEM_RELEASE));
    printf("Guest pointer layout/round-trip checks passed (%zu-bit host).\n", sizeof(void*) * 8);
}
