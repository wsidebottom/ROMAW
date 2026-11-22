/*+===================================================================
  File:      trash_folder_test.c

  Summary:   A missing Trash must not become the volume root or a host temp directory.

  Classes:   None.

  Functions: PBDirCreate, PBGetCatInfo, PBGetWDInfo, PBHGetVInfo, main, str255_from_c_string, syn68k_guest_to_host,
             syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* A missing Trash must not become the volume root or a host temp directory. */
#define NDEBUG 1
#include "../../files/alias.c"
#undef main
#undef NDEBUG
#include <assert.h>

#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;
static OSErr lookup_error, create_error, volume_error;
static int create_calls;
static INTEGER expected_volume;

void* syn68k_guest_to_host(uint32 address)
{
    return fixture_guest_to_host(address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return fixture_host_to_guest(address);
}

void str255_from_c_string(StringPtr result, const char* text)
{
    result[0] = strlen(text);
    memcpy(result + 1, text, result[0]);
}

OSErrRET PBHGetVInfo(HParmBlkPtr pb, BOOLEAN async)
{
    assert(!async);
    pb->volumeParam.ioVRefNum = CWC(-5);
    return volume_error;
}

OSErrRET PBGetWDInfo(WDPBPtr pb, BOOLEAN async)
{
    assert(!async);
    pb->ioWDVRefNum = CWC(-7);
    pb->ioWDDirID = CLC(20);
    return volume_error;
}

OSErrRET PBGetCatInfo(CInfoPBPtr pb, BOOLEAN async)
{
    StringPtr name = PPR(pb->hFileInfo.ioNamePtr);
    assert(!async && CW(pb->hFileInfo.ioVRefNum) == expected_volume);
    assert(CL(pb->hFileInfo.ioDirID) == 2);
    assert(CW(pb->hFileInfo.ioFDirIndex) == 0);
    assert(name[0] == 5 && !memcmp(name + 1, "Trash", 5));
    pb->hFileInfo.ioFlAttrib = ATTRIB_ISADIR;
    pb->dirInfo.ioDrDirID = CLC(77);
    return lookup_error;
}

OSErrRET PBDirCreate(HParmBlkPtr pb, BOOLEAN async)
{
    assert(!async && CW(pb->ioParam.ioVRefNum) == expected_volume);
    assert(CL(pb->fileParam.ioDirID) == 2);
    ++create_calls;
    pb->fileParam.ioDirID = CLC(88);
    return create_error;
}

int main(void)
{
    void* memory = calloc(1, 65536);
    ROMlib_offset = (uintptr_t)memory;
    INTEGER volume = 123;
    LONGINT directory = 456;
    expected_volume = -5;
    assert(find_volume_trash(-5, FALSE, &volume, &directory) == noErr);
    assert(volume == -5 && directory == 77 && !create_calls);
    lookup_error = fnfErr;
    volume = 123;
    directory = 456;
    assert(find_volume_trash(-5, FALSE, &volume, &directory) == fnfErr);
    assert(volume == 123 && directory == 456 && !create_calls);
    assert(find_volume_trash(0, TRUE, &volume, &directory) == noErr);
    assert(volume == -5 && directory == 88 && create_calls == 1);
    create_error = permErr;
    volume = 123;
    directory = 456;
    assert(find_volume_trash(-5, TRUE, &volume, &directory) == permErr);
    assert(volume == 123 && directory == 456);
    expected_volume = -7;
    lookup_error = noErr;
    assert(find_volume_trash((INTEGER)0x8000, FALSE, &volume, &directory) == noErr);
    assert(volume == -7 && directory == 77);
    volume_error = nsvErr;
    volume = 123;
    directory = 456;
    assert(find_volume_trash(-5, TRUE, &volume, &directory) == nsvErr);
    assert(volume == 123 && directory == 456);
    free(memory);
    puts("Trash folder tests passed");
    return 0;
}
