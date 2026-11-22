/*+===================================================================
  File:      native_file_reply_test.c

  Summary:   Check classic file-dialog filtering, icon row geometry and scrollbar transitions.

  Classes:   None.

  Functions: CToPascalCall, C_Count1Resources, C_CurResFile, C_FindFolder, C_Get1IndResource, C_ResError, C_SetResLoad,
             C_UseResFile, GetHandleSize, PBGetCatInfo, PBGetWDInfo, _warning, main, syn68k_guest_to_host,
             syn68k_host_to_guest, C_SetCtlMax, C_HiliteControl

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Classic Standard File filtering uses Macintosh metadata and guest hooks. */
#define NDEBUG 1
#include "../../files/stdfile.c"
#undef main
#undef NDEBUG
#include <assert.h>
#include "../../text/string.c"

#if SIZEOF_CHAR_P == 8
uint64 ROMlib_offset;
#else
uint32 ROMlib_offset;
#endif
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;

void* syn68k_guest_to_host(uint32 address)
{
    return (void*)(uintptr_t)(ROMlib_offset + address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return address - ROMlib_offset;
}

static unsigned filter_calls;
static OSType required_creator;
static Ptr expected_data;
static INTEGER resource_file = 88;
static Handle app_refs[6];
static const Size app_ref_sizes[] = {7, 7, 7, 7, 7, 6};

INTEGER
C_CurResFile(void)
{
    ResErr = 0;
    return resource_file;
}

void C_UseResFile(INTEGER file)
{
    resource_file = file;
    ResErr = file == 77 || file == 88 ? 0 : CW(resNotFound);
}

INTEGER
C_ResError(void)
{
    return CW(ResErr);
}

void C_SetResLoad(BOOLEAN load)
{
    ResLoad = load;
}

INTEGER
C_Count1Resources(ResType type)
{
    assert(resource_file == 77 && type == TICK("FREF"));
    return NELEM(app_refs);
}

Handle C_Get1IndResource(ResType type, INTEGER index)
{
    assert(resource_file == 77 && type == TICK("FREF") && ResLoad);
    return app_refs[index - 1];
}

Size GetHandleSize(Handle handle)
{
    for (unsigned i = 0; i < NELEM(app_refs); ++i)
        if (handle == app_refs[i])
            return app_ref_sizes[i];
    abort();
}

intptr_t CToPascalCall(void* proc, unsigned long convention, ...)
{
    assert(proc == (void*)1);
    assert(convention == CTOP_SystemEvent || convention == CTOP_GetAuxCtl);
    va_list args;
    va_start(args, convention);
    CInfoPBRec* catalog = va_arg(args, CInfoPBRec*);
    if (convention == CTOP_GetAuxCtl)
        assert(va_arg(args, Ptr) == expected_data);
    va_end(args);
    ++filter_calls;
    return catalog->hFileInfo.ioFlFndrInfo.fdCreator != required_creator;
}

void _warning(int error, const char* kind, const char* file, int line, const char* function, const char* format, ...)
{
    abort();
}

static OSErr directory_error;
static INTEGER queried_volume;
static LONGINT queried_directory;
static LONGINT catalog_parent = 2;
static OSErr desktop_error = fnfErr;

OSErrRET PBGetCatInfo(CInfoPBPtr catalog, BOOLEAN async)
{
    assert(!async);
    assert(CW(catalog->dirInfo.ioFDirIndex) == -1);
    queried_volume = CW(catalog->dirInfo.ioVRefNum);
    queried_directory = CL(catalog->dirInfo.ioDrDirID);
    catalog->dirInfo.ioDrParID = CL(catalog_parent);
    return directory_error;
}

OSErr C_FindFolder(INTEGER disk, OSType kind, BOOLEAN create, INTEGER* volume, LONGINT* directory)
{
    assert(disk == kOnSystemDisk && kind == kDesktopFolderType && !create);
    *volume = CW(-7);
    *directory = CL(456);
    return desktop_error;
}

OSErrRET PBGetWDInfo(WDPBPtr working, BOOLEAN async)
{
    assert(!async);
    working->ioWDVRefNum = CW(-7);
    working->ioWDDirID = CL(12345);
    return noErr;
}

static INTEGER scrollbar_maximum, scrollbar_highlight;

void C_SetCtlMax(ControlHandle control, INTEGER maximum)
{
    assert(control == (ControlHandle)(uintptr_t)1);
    scrollbar_maximum = maximum;
}

void C_HiliteControl(ControlHandle control, INTEGER highlight)
{
    assert(control == (ControlHandle)(uintptr_t)1);
    scrollbar_highlight = highlight;
}

int main(void)
{
    unsigned char* memory = calloc(1, 65536);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    FontInfo row_font = {CWC(9), CWC(3), CWC(8), CWC(1)};
    assert(file_row_height(&row_font) == 16);
    row_font.ascent = CWC(18);
    assert(file_row_height(&row_font) == 22);
    fltype scrolling = {0};
    scrolling.flsh = (ControlHandle)(uintptr_t)1;
    scrolling.flnmlin = 17;
    const int counts[] = {3, 17, 30, 0, 40};
    for (unsigned i = 0; i < NELEM(counts); ++i) {
        scrolling.flnmfil = counts[i];
        update_file_scrollbar(&scrolling);
        assert(scrollbar_maximum == MAX(0, counts[i] - 17));
        assert(scrollbar_highlight == (counts[i] > 17 ? 0 : 255));
    }
    const char* codes[] = {"TEXT", "APPL", "PNGf", "TEXT", "BAD!", "BAD!"};
    for (unsigned i = 0; i < NELEM(app_refs); ++i) {
        app_refs[i] = (Handle)(memory + 0x1000 + i * 16);
        Ptr bytes = (Ptr)(memory + 0x2000 + i * 16);
        HIDDEN_RAW_SET(*app_refs[i], RM(bytes));
        memcpy(bytes, codes[i], 4);
        if (i == 4)
            bytes[6] = 1; /* Truncated FREF name. */
    }
    CurApRefNum = CW(77);
    ResErr = CW(1234);
    ResLoad = FALSE;
    uint32_t document_types[6];
    assert(application_document_types(document_types, NELEM(document_types)) == 2);
    assert(document_types[0] == 0x54455854 && document_types[1] == 0x504e4766);
    assert(resource_file == 88 && CW(ResErr) == 1234 && !ResLoad);
    assert(application_document_types(document_types, 1) == 1);
    CurApRefNum = CW(99);
    assert(!application_document_types(document_types, NELEM(document_types)));
    assert(resource_file == 88 && CW(ResErr) == 1234 && !ResLoad);
    INTEGER volume = -3;
    LONGINT directory = 9876;
    resolve_file_directory(&volume, &directory);
    assert(volume == -3 && directory == 9876);
    assert(queried_volume == -3 && queried_directory == 9876);
    directory_error = dirNFErr;
    resolve_file_directory(&volume, &directory);
    assert(volume == -3 && directory == 2);
    directory = 9876;
    directory_error = fnfErr;
    resolve_file_directory(&volume, &directory);
    assert(directory == 2);
    directory_error = ioErr;
    directory = 9876;
    resolve_file_directory(&volume, &directory);
    assert(directory == 9876);
    directory_error = noErr;
    volume = WDMAGIC ^ sizeof(INTEGER);
    resolve_file_directory(&volume, &directory);
    assert(volume == -7 && directory == 12345);
    assert(queried_volume == -7 && queried_directory == 12345);

    fltype navigation = {0};
    SFSaveDisk = CW(17);
    CurDirStore = CL(12345);
    catalog_parent = 2;
    assert(moveuponedir(&navigation));
    assert(CL(CurDirStore) == 2 && CW(SFSaveDisk) == 17);
    assert(!navigation.desktop);
    catalog_parent = 1;
    assert(moveuponedir(&navigation));
    assert(navigation.desktop && !navigation.desktop_files);
    assert(CL(CurDirStore) == 2 && CW(SFSaveDisk) == 17);
    navigation.desktop = FALSE;
    desktop_error = noErr;
    assert(moveuponedir(&navigation));
    assert(navigation.desktop && navigation.desktop_files);
    assert(CL(CurDirStore) == 456 && CW(SFSaveDisk) == 7);

    Rect desktop_bounds = {CWC(44), CWC(4), CWC(596), CWC(796)};
    int width = 1200, height = 900, left = 2000, top = -200;
    fit_file_dialog(&desktop_bounds, 340, 220, &width, &height, &left, &top);
    assert(width == 792 && height == 552 && left == 4 && top == 44);
    width = 10;
    height = 10;
    left = 100;
    top = 90;
    fit_file_dialog(&desktop_bounds, 340, 220, &width, &height, &left, &top);
    assert(width == 340 && height == 220 && left == 100 && top == 90);
    fit_file_dialog(&desktop_bounds, 900, 700, &width, &height, &left, &top);
    assert(width == 900 && height == 700 && left == 4 && top == 44);

    Rect icon_row = {CWC(13), CWC(29), CWC(9), CWC(150)};
    Rect icon_bounds = file_icon_bounds(&icon_row);
    assert(CW(icon_bounds.top) == 13 && CW(icon_bounds.left) == 31 && CW(icon_bounds.bottom) == 29 &&
           CW(icon_bounds.right) == 47);
    Rect list = {CWC(29), CWC(12), CWC(159), CWC(230)};
    Rect open = {CWC(135), CWC(252), CWC(155), CWC(332)};
    Rect cancel = {CWC(104), CWC(252), CWC(124), CWC(332)};
    Rect desktop = {CWC(60), CWC(252), CWC(80), CWC(332)};
    Rect popup = {CWC(6), CWC(12), CWC(25), CWC(230)};
    Rect grown = resized_file_item(list, &list, 7, 7, 2, userItem, 120, 90);
    assert(CW(grown.right) == 350 && CW(grown.bottom) == 249);
    Rect item = resized_file_item(open, &list, 1, 7, 2, ctrlItem, 120, 90);
    assert(CW(item.left) == 372 && CW(item.right) == 452 && CW(item.top) == 225 && CW(item.bottom) == 245);
    item = resized_file_item(item, &grown, 1, 7, 2, ctrlItem, -120, -90);
    assert(!memcmp(&item, &open, sizeof item));
    item = resized_file_item(cancel, &list, 2, 7, 2, ctrlItem, 120, 90);
    assert(CW(item.top) == 194 && CW(item.left) == 372);
    item = resized_file_item(desktop, &list, 6, 7, 2, ctrlItem, 120, 90);
    assert(CW(item.top) == 60 && CW(item.left) == 372);
    item = resized_file_item(popup, &list, 8, 7, 2, userItem, 120, 90);
    assert(CW(item.top) == 6 && CW(item.left) == 12 && CW(item.right) == 350);
    Rect save_list = {CWC(29), CWC(12), CWC(127), CWC(230)};
    Rect filename = {CWC(157), CWC(15), CWC(173), CWC(227)};
    item = resized_file_item(filename, &save_list, 10, 7, 2, editText, 120, 90);
    assert(CW(item.left) == 15 && CW(item.right) == 347 && CW(item.top) == 247 && CW(item.bottom) == 263);
    /* MacWrite widens its list past the side controls and puts radio buttons
     below it. Neither a radio control nor its neighbor may grow over the
     other, and the side controls still belong in the right column. */
    Rect custom_list = {CWC(24), CWC(16), CWC(106), CWC(236)};
    Rect eject = {CWC(152), CWC(217), CWC(170), CWC(287)};
    item = resized_file_item(eject, &custom_list, 5, 8, 2, ctrlItem, 300, 293);
    assert(CW(item.left) == 517 && CW(item.right) == 587);
    Rect radio = {CWC(209), CWC(12), CWC(227), CWC(137)};
    item = resized_file_item(radio, &custom_list, 9, 8, 2, 7, 300, 293);
    assert(CW(item.left) == 12 && CW(item.right) == 137 && CW(item.top) == 502);
    Rect port = {0, 0, CWC(200), CWC(400)};
    item = file_grow_box(&port);
    assert(CW(item.left) == 385 && CW(item.top) == 185 && CW(item.right) == 400 && CW(item.bottom) == 200);

    OSType types[] = {TICKX("TEXT"), TICKX("PICT")};
    assert(typeinarray(TICKX("TEXT"), 2, types));
    assert(typeinarray(TICKX("PICT"), 2, types));
    assert(!typeinarray(TICKX("APPL"), 2, types));
    assert(!typeinarray(TICKX("TEXT"), 0, types));
    for (int flavor = original_sf; flavor <= new_custom_sf; ++flavor) {
        CInfoPBRec catalog = {0};
        fltype state = {0};
        state.flavor = flavor;
        assert(passes_filter(&state, &catalog, 1));
        state.flnumt = 1;
        state.fltl = types;
        catalog.hFileInfo.ioFlFndrInfo.fdType = TICKX("TEXT");
        assert(host_catalog_allowed(&state, &catalog));
        catalog.hFileInfo.ioFlFndrInfo.fdType = TICKX("PICT");
        assert(!host_catalog_allowed(&state, &catalog));
        catalog.hFileInfo.ioFlFndrInfo.fdType = TICKX("TEXT");
        catalog.hFileInfo.ioFlFndrInfo.fdFlags = CWC(0x4000);
        assert(!passes_filter(&state, &catalog, 1));
        assert(!host_catalog_allowed(&state, &catalog));
        catalog.hFileInfo.ioFlFndrInfo.fdFlags = 0;
        state.flfilef.oflfilef = (void*)1;
        state.mydata = expected_data = (Ptr)2;
        required_creator = TICKX("ttxt");
        catalog.hFileInfo.ioFlFndrInfo.fdCreator = required_creator;
        unsigned before = filter_calls;
        assert(passes_filter(&state, &catalog, 1));
        assert(filter_calls == before + 1);
        catalog.hFileInfo.ioFlFndrInfo.fdCreator = TICKX("WRIT");
        assert(!passes_filter(&state, &catalog, 1));
        assert(!host_catalog_allowed(&state, &catalog));
        SFReply old_reply = {0};
        StandardFileReply new_reply = {0};
        if (flavor == original_sf)
            state.flreplyp.oreplyp = &old_reply;
        else
            state.flreplyp.nreplyp = &new_reply;
        set_type_and_name(&state, 0,
                          (StringPtr) "\x04"
                                      "Test");
        assert(!memcmp(SF_NAME(&state),
                       "\x04"
                       "Test",
                       5));
        set_type_and_name(&state, 0, (StringPtr) "");
        assert(SF_NAME(&state)[0] == 0);
    }
    puts(
        "Classic file dialogs: resize layout, type/creator filters, invisible flags, custom callback data and reply names passed");
    free(memory);
    return 0;
}
