/*+===================================================================
  File:      partial_resource_test.c

  Summary:   Verify real partial-resource traps and their persistence/error boundaries.

  Classes:   None.

  Functions: CToPascalCall, C_ChangedResource, C_LoadResource, C_ResError, C_UpdateResFile, C_WriteResource,
             DisposHandle, GetHandleSize, HGetState, HLock, HSetState, MemError, ROMlib_findres, ROMlib_setreserr,
             SetHandleSize, _NewHandle_flags, main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Verify real partial-resource traps and their persistence/error boundaries. */
#define NDEBUG 1
#include "../../resources/resPartial.c"
#undef main
#undef NDEBUG
#include <assert.h>

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base, syn68k_host_alias_size = 0xffffffff;
static void* pointers[32];
static unsigned pointer_count;
static HIDDEN_Ptr master, scratch;
static unsigned char data[256], staged[256], disk[256];
static Size data_size, scratch_size, disk_size;
static resmap map_record;
static HIDDEN_resmapptr map_master;
static resref reference;
static int allocation_failure, resize_failure, write_failure, updates;
static OSErr error;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];

intptr_t CToPascalCall(void* p, unsigned long magic, ...)
{
    abort();
}

void* syn68k_guest_to_host(uint32 address)
{
    assert(address && address <= pointer_count);
    return pointers[address - 1];
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    for (unsigned i = 0; i < pointer_count; ++i)
        if (pointers[i] == (void*)address)
            return i + 1;
    pointers[pointer_count++] = (void*)address;
    return pointer_count;
}

INTEGER
ROMlib_setreserr(INTEGER err)
{
    return error = err;
}

INTEGER
C_ResError(void)
{
    return error;
}

OSErr ROMlib_findres(Handle h, resmaphand* map, typref** type, resref** ref)
{
    assert(h == &master);
    *map = &map_master;
    *ref = &reference;
    *type = NULL;
    return noErr;
}

void C_LoadResource(Handle volatile h)
{
    assert(h == &master);
    if (!STARH(h)) {
        memcpy(data, disk, disk_size);
        data_size = disk_size;
        HIDDEN_RAW_SET(*h, RM((Ptr)data));
    }
    error = noErr;
}

Size GetHandleSize(Handle h)
{
    return h == &master ? data_size : scratch_size;
}

SignedByte HGetState(Handle h)
{
    return 0;
}

void HSetState(Handle h, SignedByte state)
{
}

void HLock(Handle h)
{
}

Handle _NewHandle_flags(Size size, boolean_t system, boolean_t clear)
{
    assert(!system && clear && size <= 256);
    if (allocation_failure)
        return NULL;
    memset(staged, 0, sizeof staged);
    HIDDEN_RAW_SET(scratch, RM((Ptr)staged));
    scratch_size = size;
    return &scratch;
}

OSErr MemError(void)
{
    return allocation_failure || resize_failure ? memFullErr : noErr;
}

void SetHandleSize(Handle h, Size size)
{
    assert(h == &master && size <= 256);
    if (!resize_failure)
        data_size = size;
}

void DisposHandle(Handle h)
{
    assert(h == &scratch);
}

void C_ChangedResource(Handle h)
{
    reference.ratr |= resChanged;
    error = noErr;
}

void C_WriteResource(Handle h)
{
    if (write_failure)
        error = ioErr;
    else {
        memcpy(disk, data, data_size);
        disk_size = data_size;
        reference.ratr &= ~resChanged;
        error = noErr;
    }
}

void C_UpdateResFile(INTEGER ref)
{
    assert(ref == 7);
    ++updates;
    error = noErr;
}

int main(void)
{
    HIDDEN_RAW_SET(map_master, RM(&map_record));
    map_record.resfn = CWC(7);
    memcpy(disk, "abcdef", 6);
    disk_size = 6;
    char out[8] = {0};
    C_ReadPartialResource(&master, 2, (Ptr)out, 3);
    assert(!error && !memcmp(out, "cde", 3));
    memcpy(data, "ABCDEF", 6);
    C_ReadPartialResource(&master, 0, (Ptr)out, 6);
    assert(!error && !memcmp(out, "ABCDEF", 6));
    C_ReadPartialResource(&master, INT32_MAX, (Ptr)out, 2);
    assert(error == inputOutOfBounds);
    C_WritePartialResource(&master, 100, NULL, 0);
    assert(error == inputOutOfBounds && data_size == 6 && disk_size == 6);
    C_WritePartialResource(&master, 2, NULL, 0);
    assert(!error && !updates && data_size == 6);
    C_ReadPartialResource(&master, -1, (Ptr)out, 1);
    assert(error == inputOutOfBounds);
    C_WritePartialResource(&master, 2, (Ptr) "XY", 2);
    assert(!error && !memcmp(disk, "ABXYEF", 6) && updates == 1);
    C_WritePartialResource(&master, 2, STARH(&master), 4);
    assert(!error && !memcmp(disk, "ABABXY", 6));
    C_WritePartialResource(&master, 8, (Ptr) "Z", 1);
    assert(!error && disk_size == 9 && !disk[6] && !disk[7] && disk[8] == 'Z');
    C_SetResourceSize(&master, 3);
    assert(!error && disk_size == 3 && !memcmp(disk, "ABA", 3));
    C_SetResourceSize(&master, 8);
    assert(!error && disk_size == 8 && !disk[3] && !disk[7]);
    for (int failure = 1; failure <= 4; ++failure) {
        allocation_failure = failure == 1;
        resize_failure = failure == 2;
        map_record.resfatr = CW(failure == 3 ? mapReadOnly : 0);
        reference.ratr = failure == 4 ? resProtected : 0;
        C_WritePartialResource(&master, 0, (Ptr) "Q", 1);
        assert(error == (failure <= 2 ? memFullErr : failure == 3 ? mapReadErr : resAttrErr));
        assert(data_size == 8 && data[0] == 'A' && disk[0] == 'A');
    }
    allocation_failure = resize_failure = 0;
    map_record.resfatr = reference.ratr = 0;
    write_failure = 1;
    C_WritePartialResource(&master, 0, (Ptr) "Q", 1);
    assert(error == ioErr && disk[0] == 'A' && (reference.ratr & resChanged));
    C_WritePartialResource(&master, INT32_MAX, (Ptr) "Q", 1);
    assert(error == inputOutOfBounds);
    C_SetResourceSize(&master, -1);
    assert(error == inputOutOfBounds);
    C_SetResourceSize(&master, 0x1000000);
    assert(error == inputOutOfBounds);
    puts("PASS: partial resource edits, persistence, resizing, bounds and errors");
    return 0;
}
