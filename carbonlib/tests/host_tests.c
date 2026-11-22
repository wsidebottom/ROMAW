/*+===================================================================
  File:      host_tests.c

  Summary:   Keep Windows SDK types separate from the classic Macintosh headers.

  Classes:   None.

  Functions: ROMlib_carbon_host_tests

  Origin:    Existing repository source in packages/carbonlib/tests; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Keep Windows SDK types separate from the classic Macintosh headers. */
#include "carbonlib.h"
#include <windows.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

enum { workers = 4, iterations = 4000, operations = workers * iterations };

typedef struct {
    int32_t (*change)(volatile int32_t*);
    volatile int32_t* counter;
    LONG* seen;
    int decrement;
} atomic_worker;

static DWORD WINAPI run_atomic_worker(void* opaque)
{
    atomic_worker* worker = opaque;
    for (unsigned i = 0; i < iterations; ++i) {
        int32_t previous = worker->change(worker->counter);
        int32_t index = previous - worker->decrement;
        if (index < 0 || index >= operations || InterlockedIncrement(&worker->seen[index]) != 1)
            return 1;
    }
    return 0;
}

static void concurrent_atomics(void)
{
    int32_t (*increment)(volatile int32_t*) = ROMlib_carbon_find_export("IncrementAtomic", 2)->function;
    int32_t (*decrement)(volatile int32_t*) = ROMlib_carbon_find_export("DecrementAtomic", 2)->function;
    unsigned char memory[8] = {0xa5, 0, 0, 0, 0, 0xa5, 0xa5, 0xa5};
    LONG* seen = calloc(operations, sizeof *seen);
    assert(seen);
    for (unsigned down = 0; down < 2; ++down) {
        memset(seen, 0, operations * sizeof *seen);
        HANDLE threads[workers];
        atomic_worker worker = {down ? decrement : increment, (volatile int32_t*)(memory + 1), seen, down};
        for (unsigned i = 0; i < workers; ++i) {
            threads[i] = CreateThread(NULL, 0, run_atomic_worker, &worker, 0, NULL);
            assert(threads[i]);
        }
        assert(WaitForMultipleObjects(workers, threads, TRUE, 15000) == WAIT_OBJECT_0);
        for (unsigned i = 0; i < workers; ++i) {
            DWORD result;
            assert(GetExitCodeThread(threads[i], &result) && !result);
            assert(CloseHandle(threads[i]));
        }
        for (unsigned i = 0; i < operations; ++i)
            assert(seen[i] == 1);
        uint32_t value = (uint32_t)memory[1] << 24 | (uint32_t)memory[2] << 16 | (uint32_t)memory[3] << 8 | memory[4];
        assert(value == (down ? 0 : operations));
        assert(memory[0] == 0xa5 && memory[5] == 0xa5 && memory[6] == 0xa5 && memory[7] == 0xa5);
    }
    free(seen);
}

typedef struct {
    uint32_t (*read)(uint32_t, volatile uint32_t*);
    int8_t (*compare)(uint32_t, uint32_t, volatile uint32_t*);
    volatile uint32_t* counter;
    LONG* seen;
} swap_worker;

static DWORD WINAPI run_swap_worker(void* opaque)
{
    swap_worker* worker = opaque;
    for (unsigned i = 0; i < iterations; ++i) {
        uint32_t previous;
        do {
            previous = worker->read(0, worker->counter);
        } while (!worker->compare(previous, previous + 1, worker->counter));
        if (previous >= operations || InterlockedIncrement(&worker->seen[previous]) != 1)
            return 1;
    }
    return 0;
}

static void concurrent_compare_swap(void)
{
    unsigned char memory[8] = {0xa5, 0, 0, 0, 0, 0xa5, 0xa5, 0xa5};
    LONG* seen = calloc(operations, sizeof *seen);
    assert(seen);
    swap_worker worker = {
        ROMlib_carbon_find_export("BitOrAtomic", 2)->function,
        ROMlib_carbon_find_export("CompareAndSwap", 2)->function,
        (volatile uint32_t*)(memory + 1),
        seen,
    };
    HANDLE threads[workers];
    for (unsigned i = 0; i < workers; ++i) {
        threads[i] = CreateThread(NULL, 0, run_swap_worker, &worker, 0, NULL);
        assert(threads[i]);
    }
    assert(WaitForMultipleObjects(workers, threads, TRUE, 15000) == WAIT_OBJECT_0);
    for (unsigned i = 0; i < workers; ++i) {
        DWORD result;
        assert(GetExitCodeThread(threads[i], &result) && !result);
        assert(CloseHandle(threads[i]));
    }
    for (unsigned i = 0; i < operations; ++i)
        assert(seen[i] == 1);
    uint32_t value = (uint32_t)memory[1] << 24 | (uint32_t)memory[2] << 16 | (uint32_t)memory[3] << 8 | memory[4];
    assert(value == operations);
    assert(memory[0] == 0xa5 && memory[5] == 0xa5 && memory[6] == 0xa5 && memory[7] == 0xa5);
    free(seen);
}

void ROMlib_carbon_host_tests(void)
{
    void (*to_c)(const unsigned char*, char*) = ROMlib_carbon_find_export("CopyPascalStringToC", 2)->function;
    void (*to_pascal)(const char*, unsigned char*) = ROMlib_carbon_find_export("CopyCStringToPascal", 2)->function;
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    unsigned char* pages = VirtualAlloc(NULL, 2 * info.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(pages);
    DWORD protection;
    assert(VirtualProtect(pages + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &protection));
    unsigned char* end = pages + info.dwPageSize;
    unsigned char output[256];
    memset(end - 255, 'x', 255); /* No accessible terminator is required past the cap. */
    to_pascal((char*)end - 255, output);
    assert(output[0] == 255);
    end[-256] = 255;
    to_c(end - 256, (char*)output);
    assert(!output[255]);
    int16_t (*pl_length)(const unsigned char*) = ROMlib_carbon_find_export("PLstrlen", 2)->function;
    int16_t (*pl_compare)(const unsigned char*, const unsigned char*) =
        ROMlib_carbon_find_export("PLstrcmp", 2)->function;
    int16_t (*pl_compare_n)(const unsigned char*, const unsigned char*, int16_t) =
        ROMlib_carbon_find_export("PLstrncmp", 2)->function;
    unsigned char* (*pl_copy)(unsigned char*, const unsigned char*) =
        ROMlib_carbon_find_export("PLstrcpy", 2)->function;
    unsigned char* (*pl_copy_n)(unsigned char*, const unsigned char*, int16_t) =
        ROMlib_carbon_find_export("PLstrncpy", 2)->function;
    unsigned char* (*pl_append)(unsigned char*, const unsigned char*) =
        ROMlib_carbon_find_export("PLstrcat", 2)->function;
    unsigned char* (*pl_append_n)(unsigned char*, const unsigned char*, int16_t) =
        ROMlib_carbon_find_export("PLstrncat", 2)->function;
    const unsigned char* (*pl_find_char)(const unsigned char*, int16_t) =
        ROMlib_carbon_find_export("PLstrchr", 2)->function;
    const unsigned char* (*pl_find_last)(const unsigned char*, int16_t) =
        ROMlib_carbon_find_export("PLstrrchr", 2)->function;
    const unsigned char* (*pl_find_any)(const unsigned char*, const unsigned char*) =
        ROMlib_carbon_find_export("PLstrpbrk", 2)->function;
    int16_t (*pl_span)(const unsigned char*, const unsigned char*) = ROMlib_carbon_find_export("PLstrspn", 2)->function;
    const unsigned char* (*pl_find_string)(const unsigned char*, const unsigned char*) =
        ROMlib_carbon_find_export("PLstrstr", 2)->function;
    int16_t (*pl_position)(const unsigned char*, const unsigned char*) =
        ROMlib_carbon_find_export("PLpos", 2)->function;
    const unsigned char needle[] = {1, 0x5a}, absent[] = {1, 0x6a};
    for (unsigned length = 0; length < 256; ++length) {
        unsigned char* source = end - length - 1;
        memset(source + 1, 0x5a, length);
        source[0] = length;
        assert(pl_length(source) == length && pl_compare(source, source) == 0);
        assert(pl_compare_n(source, source, 32767) == 0);
        assert(pl_copy(output, source) == output && !memcmp(output, source, length + 1));
        output[0] = 0;
        assert(pl_copy_n(output, source, 32767) == output && !memcmp(output, source, length + 1));
        output[0] = 0;
        assert(pl_append(output, source) == output && !memcmp(output, source, length + 1));
        output[0] = 0;
        assert(pl_append_n(output, source, 32767) == output && !memcmp(output, source, length + 1));
        assert(pl_find_char(source, 0x5a) == (length ? source + 1 : NULL));
        assert(pl_find_last(source, 0x5a) == (length ? end - 1 : NULL));
        assert(!pl_find_char(source, 0x6a) && !pl_find_last(source, 0x6a));
        assert(pl_find_any(source, needle) == (length ? source + 1 : NULL));
        assert(pl_span(source, needle) == length);
        assert(pl_find_string(source, needle) == (length ? source + 1 : NULL));
        assert(pl_position(needle, source) == !!length);
        assert(!pl_find_string(source, absent) && !pl_find_any(source, absent));
    }
    int32_t (*cf_count)(const void*) = ROMlib_carbon_find_export("CFArrayGetCount", 2)->function;
    int32_t (*cf_length)(const void*) = ROMlib_carbon_find_export("CFDataGetLength", 2)->function;
    uint32_t (*cf_type)(const void*) = ROMlib_carbon_find_export("CFGetTypeID", 2)->function;
    unsigned char* (*cf_bytes)(const void*) = ROMlib_carbon_find_export("CFDataGetBytePtr", 2)->function;
    unsigned char* (*cf_mutable)(const void*) = ROMlib_carbon_find_export("CFDataGetMutableBytePtr", 2)->function;
    unsigned char* object = end - 12;
    memset(object, 0, 12);
    object[6] = 0xcd;
    object[8] = 0x80;
    object[9] = 0x12;
    object[10] = 0x34;
    object[11] = 0x56;
    assert((uint32_t)cf_count(object) == 0x80123456 && (uint32_t)cf_length(object) == 0x80123456);
    assert(cf_type(object) == 0xcd);
    object = end - 24;
    memset(object, 0, 24);
    assert(!cf_bytes(object) && !cf_mutable(object));
    assert(VirtualFree(pages, 0, MEM_RELEASE));
    concurrent_atomics();
    concurrent_compare_swap();
}
