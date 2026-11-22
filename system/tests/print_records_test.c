/*+===================================================================
  File:      print_records_test.c

  Summary:   Implements print records test support for regression tests and test fixtures.

  Classes:   None.

  Functions: CToPascalCall, C_CloseResFile, C_FindFolder, C_GetString, C_HOpenResFile, C_OpenRFPerm, C_ResError,
             GetHandleSize, HGetState, HLock, HSetState, ROMlib_acknowledge_job_dialog, host_dialog_error,
             host_print_dialog, main, sdl_preferences_input_reset, syn68k_guest_to_host, update_printing_globals

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#define NDEBUG 1
#include "../../printing/prInit.c"
#include "../../printing/prRecords.c"
#undef main
#undef NDEBUG
#include <assert.h>

#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xFFFFFFFFUL;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;

void* syn68k_guest_to_host(uint32 address)
{
    return fixture_guest_to_host(address);
}

void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];

intptr_t CToPascalCall(void* proc, unsigned long magic, ...)
{
    abort();
}

int ROMlib_paper_x = 612, ROMlib_paper_y = 792;
int ROMlib_resolution_x, ROMlib_resolution_y;
uint32 ROMlib_PrDrvrVers = 70;
int ROMlib_print_margins[4] = {36, 36, 36, 36};
int ROMlib_native_paper_width, ROMlib_native_paper_height;
const char* ROMlib_paper_orientation;
static int native_result, native_calls, acknowledged, errors;
static host_print_settings chosen;

int host_print_dialog(int page_setup, host_print_settings* settings)
{
    ++native_calls;
    if (native_result == 1)
        *settings = chosen;
    return native_result;
}

void host_dialog_error(const char* message)
{
    ++errors;
}

void sdl_preferences_input_reset(void)
{
}

void ROMlib_acknowledge_job_dialog(THPrint print)
{
    ++acknowledged;
}

static StringHandle name;
static THPrint record;
static Size record_size = sizeof(TPrint), string_size = 8;
static int lookup_error, system_result = -1, legacy_result = -1;
static int system_calls, legacy_calls, close_calls, settings_calls;
static SignedByte string_state = 0x60;

void update_printing_globals(void)
{
    ++settings_calls;
    if (ROMlib_native_paper_width) {
        ROMlib_paper_x = ROMlib_native_paper_width;
        ROMlib_paper_y = ROMlib_native_paper_height;
    }
}

Size GetHandleSize(Handle h)
{
    return h == (Handle)name ? string_size : h == (Handle)record ? record_size : -1;
}

SignedByte HGetState(Handle h)
{
    assert(h == (Handle)name);
    return string_state;
}

void HLock(Handle h)
{
    assert(h == (Handle)name);
    string_state |= 0x80;
}

void HSetState(Handle h, SignedByte state)
{
    assert(h == (Handle)name);
    string_state = state;
}

StringHandle C_GetString(INTEGER id)
{
    assert(id == -8192);
    return name;
}

OSErr C_FindFolder(int16 volume, OSType type, Boolean create, int16* out_volume, int32* directory)
{
    assert(volume == kOnSystemDisk && type == kSystemFolderType && !create);
    *out_volume = CW(-7);
    *directory = CL(0x11223344);
    return lookup_error;
}

INTEGER
C_HOpenResFile(INTEGER volume, LONGINT directory, StringPtr file, SignedByte permission)
{
    assert(volume == -7 && directory == 0x11223344 && permission == fsCurPerm);
    assert(!memcmp(file, "\007Printer", 8));
    ++system_calls;
    return system_result;
}

INTEGER
C_OpenRFPerm(StringPtr file, INTEGER volume, Byte permission)
{
    assert(volume == -32765 && permission == fsCurPerm);
    ++legacy_calls;
    return legacy_result;
}

void C_CloseResFile(INTEGER ref)
{
    assert(ref == 17);
    ++close_calls;
}

INTEGER
C_ResError(void)
{
    return fnfErr;
}

int main(void)
{
    unsigned char* memory = calloc(1, 65536);
    StringHandle saved_name;
    int calls;
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    record = (THPrint)(memory + 0x2000);
    *(uint32*)record = CL(0x3000);
    name = (StringHandle)(memory + 0x4000);
    *(uint32*)name = CL(0x4100);
    memcpy(memory + 0x4100, "\007Printer", 8);
    BootDrive = CW(-32765);
    C_PrintDefault(NULL);
    assert(CW(PrintErr) == nilHandleErr && !settings_calls);
    assert(!C_PrValidate(NULL) && CW(PrintErr) == nilHandleErr);
    *(uint32*)record = 0;
    C_PrintDefault(record);
    assert(CW(PrintErr) == nilHandleErr);
    assert(!C_PrValidate(record) && CW(PrintErr) == nilHandleErr);
    *(uint32*)record = CL(0x3000);
    record_size = sizeof(TPrint) - 1;
    memset(memory + 0x3000, 0xA5, sizeof(TPrint));
    C_PrintDefault(record);
    assert(CW(PrintErr) == paramErr && !settings_calls);
    assert(!C_PrValidate(record) && CW(PrintErr) == paramErr && memory[0x3000] == 0xA5);
    record_size = -1;
    C_PrintDefault(record);
    assert(CW(PrintErr) == paramErr && !settings_calls);
    record_size = sizeof(TPrint);
    C_PrintDefault(record);
    assert(settings_calls == 1 && Hx(record, prInfo.iHRes) == 72 && Hx(record, prJob.iCopies) == 1);
    {
        TPrint before;
        memcpy(&before, STARH(record), sizeof before);
        assert(!ROMlib_native_print_dialog(NULL, FALSE) && !native_calls);
        native_result = 0;
        assert(!ROMlib_native_print_dialog(record, FALSE));
        assert(!memcmp(&before, STARH(record), sizeof before) && !PrintErr);
        assert(!acknowledged);
        native_result = -1;
        assert(!ROMlib_native_print_dialog(record, TRUE));
        assert(!memcmp(&before, STARH(record), sizeof before) && errors == 1);
        native_result = 1;
        chosen = (host_print_settings){842, 595, 2, 7, 3, 18, 24, 30, 36};
        assert(ROMlib_native_print_dialog(record, TRUE));
        assert(Hx(record, prJob.iFstPage) == 1 && !acknowledged);
        assert(Hx(record, prInfo.rPage.right) == 794);
        assert(Hx(record, prInfo.rPage.bottom) == 535);
        assert(Hx(record, rPaper.left) == -18);
        assert(Hx(record, rPaper.top) == -24);
        assert(ROMlib_native_print_dialog(record, FALSE));
        assert(Hx(record, prJob.iFstPage) == 2);
        assert(Hx(record, prJob.iLstPage) == 7);
        assert(Hx(record, prJob.iCopies) == 1 && acknowledged == 1);
        memcpy(&before, STARH(record), sizeof before);
        chosen.left = 100000;
        assert(!ROMlib_native_print_dialog(record, FALSE));
        assert(!memcmp(&before, STARH(record), sizeof before));
        assert(CW(PrintErr) == paramErr && acknowledged == 1);
    }
    saved_name = name;
    name = NULL;
    C_PrOpen();
    assert(CW(PrintErr) == resNotFound && !system_calls && !legacy_calls);
    name = saved_name;
    string_size = 7;
    C_PrOpen();
    assert(CW(PrintErr) == paramErr && !system_calls && !legacy_calls && string_state == 0x60);
    string_size = 8;
    C_PrOpen();
    assert(CW(PrintErr) == fnfErr && system_calls == 1 && legacy_calls == 1 && string_state == 0x60);
    system_result = 17;
    C_PrOpen();
    assert(!PrintErr && ROMlib_printresfile == 17 && system_calls == 2 && legacy_calls == 1 && string_state == 0x60);
    calls = system_calls;
    C_PrOpen();
    assert(system_calls == calls && !PrintErr);
    C_PrClose();
    assert(close_calls == 1 && ROMlib_printresfile == -1 && !PrintErr);
    lookup_error = fnfErr;
    legacy_result = 17;
    C_PrOpen();
    assert(!PrintErr && ROMlib_printresfile == 17 && system_calls == calls && legacy_calls == 2);
    C_PrClose();
    assert(close_calls == 2);
    free(memory);
    puts(
        "Printing: null/empty/short records, real defaults, System Folder lookup, legacy fallback and handle-state preservation passed");
    return 0;
}
