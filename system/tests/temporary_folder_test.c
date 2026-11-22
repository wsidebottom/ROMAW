/*+===================================================================
  File:      temporary_folder_test.c

  Summary:   The missing-temp fallback must never report the volume root as a folder.

  Classes:   None.

  Functions: PBDirCreate, PBGetCatInfo, PBHGetVInfo, main, str255_from_c_string, syn68k_guest_to_host,
             syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* The missing-temp fallback must never report the volume root as a folder. */
#define NDEBUG 1
#include "../../files/alias.c"
#undef main
#undef NDEBUG
#include <assert.h>

#if __SIZEOF_POINTER__ == 8
uint64 ROMlib_offset;
#else
uint32 ROMlib_offset;
#endif
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base, syn68k_host_alias_size = 0xffffffff;
static OSErr volume_error, create_error, lookup_errors[3];
static int lookup_calls, create_calls, regular_file;
static const char* names[] = {"tmp", "temp", "Temporary Items"};
static ptr_sized_uint pointers[64];
static unsigned pointer_count;

void* syn68k_guest_to_host(uint32 address)
{
    assert(address > 0 && address <= pointer_count);
    return (void*)pointers[address - 1];
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    for (unsigned i = 0; i < pointer_count; ++i)
        if (pointers[i] == address)
            return i + 1;
    assert(pointer_count < NELEM(pointers));
    pointers[pointer_count++] = address;
    return pointer_count;
}

void str255_from_c_string(StringPtr result, const char* text)
{
    result[0] = strlen(text);
    memcpy(result + 1, text, result[0]);
}

OSErrRET PBHGetVInfo(HParmBlkPtr pb, BOOLEAN async)
{
    assert(!async && CW(pb->volumeParam.ioVRefNum) == -123);
    pb->volumeParam.ioVRefNum = CWC(-5);
    return volume_error;
}

OSErrRET PBGetCatInfo(CInfoPBPtr pb, BOOLEAN async)
{
    StringPtr name = PPR(pb->hFileInfo.ioNamePtr);
    int index = lookup_calls++;
    assert(index < 3 && !async);
    assert(CW(pb->hFileInfo.ioVRefNum) == -5);
    assert(CL(pb->hFileInfo.ioDirID) == 2);
    assert(CW(pb->hFileInfo.ioFDirIndex) == 0);
    assert(name[0] == strlen(names[index]));
    assert(!memcmp(name + 1, names[index], name[0]));
    pb->hFileInfo.ioFlAttrib = regular_file ? 0 : ATTRIB_ISADIR;
    pb->dirInfo.ioDrDirID = CL(0x12345678 + index);
    return lookup_errors[index];
}

OSErrRET PBDirCreate(HParmBlkPtr pb, BOOLEAN async)
{
    StringPtr name = PPR(pb->ioParam.ioNamePtr);
    assert(!async && CW(pb->ioParam.ioVRefNum) == -5);
    assert(CL(pb->fileParam.ioDirID) == 2);
    assert(name[0] == 15 && !memcmp(name + 1, "Temporary Items", 15));
    ++create_calls;
    pb->fileParam.ioDirID = CLC(0x76543210);
    return create_error;
}

static void reset(void)
{
    volume_error = create_error = noErr;
    lookup_calls = create_calls = regular_file = 0;
    for (int i = 0; i < 3; ++i)
        lookup_errors[i] = fnfErr;
}

int main(void)
{
    void* memory = calloc(1, 65536);
    ROMlib_offset = (uintptr_t)memory;
    INTEGER volume;
    LONGINT directory;
    for (int existing = 0; existing < 3; ++existing) {
        reset();
        lookup_errors[existing] = noErr;
        assert(last_chance_tmp_vref_and_dirid(-123, FALSE, &volume, &directory) == noErr);
        assert(volume == -5 && directory == 0x12345678 + existing);
        assert(lookup_calls == existing + 1 && !create_calls);
    }
    reset();
    volume = 123;
    directory = 456;
    assert(last_chance_tmp_vref_and_dirid(-123, FALSE, &volume, &directory) == fnfErr);
    assert(volume == 123 && directory == 456 && !create_calls);
    reset();
    assert(last_chance_tmp_vref_and_dirid(-123, TRUE, &volume, &directory) == noErr);
    assert(volume == -5 && directory == 0x76543210 && create_calls == 1);
    reset();
    create_error = permErr;
    volume = 123;
    directory = 456;
    assert(last_chance_tmp_vref_and_dirid(-123, TRUE, &volume, &directory) == permErr);
    assert(volume == 123 && directory == 456 && create_calls == 1);
    reset();
    regular_file = 1;
    lookup_errors[2] = noErr;
    assert(last_chance_tmp_vref_and_dirid(-123, TRUE, &volume, &directory) == dupFNErr);
    assert(volume == 123 && directory == 456 && !create_calls);
    reset();
    lookup_errors[2] = ioErr;
    assert(last_chance_tmp_vref_and_dirid(-123, TRUE, &volume, &directory) == ioErr);
    assert(volume == 123 && directory == 456 && !create_calls);
    reset();
    volume_error = nsvErr;
    assert(last_chance_tmp_vref_and_dirid(-123, TRUE, &volume, &directory) == nsvErr);
    assert(volume == 123 && directory == 456 && !lookup_calls && !create_calls);
    free(memory);
    puts("Temporary folder fallback tests passed");
    return 0;
}
