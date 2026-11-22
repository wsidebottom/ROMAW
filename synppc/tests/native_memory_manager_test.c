/*+===================================================================
  File:      native_memory_manager_test.c

  Summary:   Check native Memory Manager ownership, malformed references and allocation failure.

  Classes:   None.

  Functions: main.

  Origin:    Original ROMAW fixture using an isolated bounded guest-memory backend.

  Copyright and Legal notices: Applicable repository license: synppc/COPYING.
===================================================================+*/

#include "../src/romaw/memory-manager.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char ram[65536];
static uint32_t cursor = 256, live, fail_next, kinds[3], releases;
static unsigned char active[65536];

static void* allocate(void* opaque, uint32_t size, unsigned kind, uint32_t* address)
{
    (void)opaque;
    if (fail_next) {
        fail_next = 0;
        return NULL;
    }
    if (size > sizeof ram - cursor)
        return NULL;
    *address = cursor;
    active[cursor] = 1;
    void* result = ram + cursor;
    cursor = (cursor + size + 15) & ~15U;
    ++live;
    ++kinds[kind];
    memset(result, 0xcc, size);
    return result;
}

static void release(void* opaque, uint32_t address)
{
    (void)opaque;
    assert(address < sizeof ram && active[address]);
    active[address] = 0;
    --live;
    ++releases;
}

static void* memory(void* opaque, uint32_t address, size_t size, int writing)
{
    (void)opaque;
    (void)writing;
    return address <= sizeof ram && size <= sizeof ram - address ? ram + address : NULL;
}

static void space(void* opaque, unsigned kind, uint32_t* total, uint32_t* largest)
{
    (void)opaque;
    *total = 0x12345678 + kind;
    *largest = 0x01020304 + kind;
}

int main(void)
{
    ROMlib_native_memory* state = NULL;
    ROMlib_native_memory_backend backend = {NULL, allocate, release, memory, space};
    uint32_t args[8] = {1, 4096, 8192}, result;
#define CALL(name) ROMlib_native_memory_service(&state, &backend, name, args, &result)
    assert(!ROMlib_native_memory_handles("MMUnknown"));
    assert(CALL("MMNewHeap") == 1 && result);
    uint32_t application = result;
    args[0] = 2;
    assert(CALL("MMNewHeap") == 1 && result);
    uint32_t temporary = result;
    args[0] = application;
    assert(CALL("MMSetDefaultHeap") == 1 && result == 0);
    args[0] = 32;
    assert(CALL("MMAllocateClear") == 1 && result);
    uint32_t pointer = result;
    for (unsigned i = 0; i < 32; ++i)
        assert(ram[pointer + i] == 0);
    memcpy(ram + pointer, "preserve", 8);
    args[0] = pointer;
    assert(CALL("MMGetHeap") == 1 && result == application);
    assert(CALL("MMBlockSize") == 1 && result == 32);
    args[1] = 64;
    fail_next = 1;
    assert(CALL("MMReallocate") == 1 && result == 0);
    assert(active[pointer] && !memcmp(ram + pointer, "preserve", 8));
    args[0] = temporary;
    assert(CALL("MMSetDefaultHeap") == 1 && result == application);
    args[0] = pointer;
    assert(CALL("MMReallocate") == 1 && result);
    assert(!active[pointer]);
    pointer = result;
    assert(!memcmp(ram + pointer, "preserve", 8));
    args[0] = pointer;
    assert(CALL("MMGetHeap") == 1 && result == application);
    args[0] = 20;
    args[1] = temporary;
    assert(CALL("MMAllocateIn") == 1 && result);
    uint32_t other = result;
    args[0] = application;
    args[1] = 16;
    args[2] = 24;
    memset(ram + 12, 0xaa, 20);
    assert(CALL("MMGetFreeSpace") == 1 && result == 0x01020305);
    assert(!memcmp(ram + 16, "\x12\x34\x56\x79", 4));
    assert(!memcmp(ram + 24, "\x01\x02\x03\x05", 4));
    assert(ram[15] == 0xaa && ram[20] == 0xaa && ram[28] == 0xaa);
    args[2] = sizeof ram - 1;
    memset(ram + 16, 0xaa, 4);
    assert(CALL("MMGetFreeSpace") == -1);
    assert(ram[16] == 0xaa);
    args[0] = other + 1;
    assert(CALL("MMFree") == -1 && active[other]);
    args[0] = application;
    assert(CALL("MMDisposeHeap") == 1);
    assert(!active[pointer] && active[other]);
    args[0] = pointer;
    assert(CALL("MMFree") == -1);
    args[0] = 0;
    assert(CALL("MMFree") == 1);
    args[0] = 0xffffffff;
    assert(CALL("MMAllocate") == 1 && result == 0);
    args[0] = temporary;
    assert(CALL("MMDisposeHeap") == 1);
    assert(!active[other]);
    assert(live == 0 && kinds[1] && kinds[2]);
    args[0] = 1;
    fail_next = 1;
    assert(CALL("MMNewHeap") == 1 && result == 0);
    ROMlib_native_memory_destroy(state);
    assert(!live && releases >= 5);
    state = NULL;
    fail_next = 1;
    args[0] = 64;
    assert(CALL("MMAllocate") == 1 && result == 0);
    assert(live == 0);
    assert(CALL("MMAllocate") == 1 && result);
    ROMlib_native_memory_destroy(state);
    assert(live == 0);
    puts("Native Memory Manager: ownership, allocation failure and bounds checks passed");
    return 0;
}
