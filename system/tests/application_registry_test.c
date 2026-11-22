/*+===================================================================
  File:      application_registry_test.c

  Summary:   Association lookup must never eagerly load unrelated application resources.

  Classes:   None.

  Functions: ROMlib_UNIX7_to_Mac, ROMlib_automount, ROMlib_filename_from_fsspec, ROMlib_vcbbybiggestunixname,
             filetype_map_application_path, filetype_map_register_application, main, syn68k_guest_to_host,
             syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Association lookup must never eagerly load unrelated application resources. */
#define NDEBUG 1
#include "rsys/common.h"
#include "FileMgr.h"
#include "ResourceMgr.h"
#include "MemoryMgr.h"
#include "rsys/file.h"
#undef main
#undef NDEBUG
#include <assert.h>

static INTEGER current_file = 7;
static int fail_open;
static Handle reference;
static unsigned catalog_calls, folder_calls, registrations;
int ROMlib_catalog_skip_counts;

static OSErr test_folder(INTEGER volume, OSType selector, BOOLEAN create, INTEGER* found, LONGINT* directory)
{
    (void)volume;
    assert(selector == TICK("exdr") && !create);
    ++folder_calls;
    *found = CW(7);
    *directory = CL(9);
    return noErr;
}

static OSErr test_catalog(CInfoPBPtr pb, BOOLEAN async)
{
    assert(!async && ROMlib_catalog_skip_counts);
    ++catalog_calls;
    StringPtr name = PPR(pb->dirInfo.ioNamePtr);
    unsigned index = CW(pb->dirInfo.ioFDirIndex);
    if (!index) {
        assert(!memcmp(name, "\004Apps", 5));
        pb->dirInfo.ioFlAttrib = ATTRIB_ISADIR;
        pb->dirInfo.ioDrDirID = CL(10);
        return noErr;
    }
    if (index > 20)
        return fnfErr;
    memcpy(name, "\004File", 5);
    pb->hFileInfo.ioFlFndrInfo.fdType = TICKX("TEXT");
    if (index == 9) {
        memcpy(name, "\003App", 4);
        pb->hFileInfo.ioFlFndrInfo.fdType = TICKX("APPL");
        pb->hFileInfo.ioFlFndrInfo.fdCreator = TICKX("TEST");
    }
    return noErr;
}

static INTEGER test_open(INTEGER volume, LONGINT directory, StringPtr name, SignedByte permission)
{
    (void)volume;
    (void)directory;
    (void)name;
    assert(permission == fsRdPerm && !ResLoad);
    if (fail_open)
        return -1;
    current_file = 9;
    return 9;
}

static INTEGER test_count(ResType type)
{
    assert(type == TICK("FREF") && ResLoad && current_file == 9);
    return 1;
}

static Handle test_reference(ResType type, INTEGER index)
{
    assert(type == TICK("FREF") && index == 1 && ResLoad);
    return reference;
}

static void test_close(INTEGER file)
{
    assert(file == 9);
}

#undef CurResFile
#undef SetResLoad
#undef HOpenResFile
#undef Count1Resources
#undef Get1IndResource
#undef GetHandleSize
#undef CloseResFile
#undef UseResFile
#undef FindFolder
#undef PBGetCatInfo
#define CurResFile() current_file
#define SetResLoad(value) (ResLoad = (value))
#define HOpenResFile test_open
#define Count1Resources test_count
#define Get1IndResource test_reference
#define GetHandleSize(handle) ((Size)7)
#define CloseResFile test_close
#define UseResFile(file) (current_file = (file))
#define FindFolder test_folder
#define PBGetCatInfo test_catalog
#include "../../files/application_registry.c"

int filetype_map_application_path(uint32_t type, uint32_t creator, char* path, size_t size, int native)
{
    (void)type;
    (void)creator;
    (void)path;
    (void)size;
    (void)native;
    return 0; /* A cold Desktop database. */
}

void ROMlib_automount(char* path)
{
    (void)path;
    assert(0);
}

HVCB* ROMlib_vcbbybiggestunixname(const char* path)
{
    (void)path;
    assert(0);
    return NULL;
}

INTEGER
ROMlib_UNIX7_to_Mac(char* name, INTEGER length)
{
    (void)name;
    (void)length;
    assert(0);
    return 0;
}

P4(PUBLIC pascal trap, OSErr, FSMakeFSSpec, INTEGER, volume, LONGINT, directory, StringPtr, name, FSSpecPtr, result)
{
    (void)volume;
    (void)directory;
    (void)name;
    (void)result;
    assert(0);
    return fnfErr;
}

OSErr ROMlib_filename_from_fsspec(char** path, FSSpec* spec)
{
    assert(spec->name[0] == 3);
    *path = strdup("/Apps/App");
    return *path ? noErr : memFullErr;
}

int filetype_map_register_application(const char* path, uint32_t creator, const uint32_t* types, size_t n, int native)
{
    assert(!strcmp(path, "/Apps/App") && creator == TICK("TEST") && n == 1 &&
           (types[0] == TICK("APPL") || types[0] == TICK("TEXT")) && native);
    ++registrations;
    return 1;
}

#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;

void* syn68k_guest_to_host(uint32 address)
{
    return fixture_guest_to_host(address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return fixture_host_to_guest(address);
}

int main(void)
{
    unsigned char* memory = calloc(1, 65536);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    reference = (Handle)(memory + 0x2000);
    HIDDEN_RAW_SET(*reference, RM((Ptr)(memory + 0x3000)));
    memcpy(memory + 0x3000, "TEXT\0\0\0", 7);
    application app = {0};
    for (int mode = 0; mode < 2; ++mode) {
        ResLoad = mode;
        fail_open = 0;
        assert(supports_type(&app, TICK("TEXT")));
        assert(ResLoad == mode && current_file == 7);
        assert(!supports_type(&app, TICK("PICT")));
        assert(ResLoad == mode && current_file == 7);
        fail_open = 1;
        assert(!supports_type(&app, TICK("TEXT")));
        assert(ResLoad == mode && current_file == 7);
    }
    fail_open = 0;
    FSSpec spec;
    assert(ROMlib_application_spec_cached(0, 0, &spec) == fnfErr);
    assert(!catalog_calls && !folder_calls && !indexing_requested);
    assert(ROMlib_application_spec_cached(TICK("NOPE"), TICK("TEXT"), &spec) == fnfErr);
    assert(!catalog_calls && !folder_calls && indexing_requested);
    assert(ROMlib_application_registry_pending());
    assert(!ROMlib_application_registry_idle());
    assert(catalog_calls == 9 && folder_calls == 1 && count == 0);
    unsigned calls = catalog_calls;
    assert(ROMlib_application_registry_idle());
    assert(catalog_calls - calls <= 8 && count == 1 && registrations == 1);
    assert(!ROMlib_application_spec_cached(TICK("TEST"), TICK("TEXT"), &spec));
    assert(catalog_calls - calls <= 8); /* Lookup itself did not scan. */
    assert(ROMlib_application_registry_idle());
    assert(ready && handler_count == 1 && registrations == 2);
    assert(!ROMlib_application_spec_cached(TICK("NOPE"), TICK("TEXT"), &spec));
    calls = catalog_calls;
    assert(!ROMlib_application_registry_idle() && catalog_calls == calls);
    assert(!ROMlib_application_registry_pending());
    ROMlib_application_registry_reset();
    assert(!count && !ready && !indexing_requested && !pending_type_count);
    free(memory);
    puts("Application associations: lazy resource maps and caller state preserved");
    return 0;
}
