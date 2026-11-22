/*+===================================================================
  File:      desk_files_test.c

  Summary:   Folder discovery uses the real collector with a bounded catalog fixture.

  Classes:   None.

  Functions: CToPascalCall, C_FSpGetFInfo, C_FindFolder, C_GetItem, C_HiliteMenu, C_MoveWindow, C_Munger, EqualString,
             GetHandleSize, NewLaunch, PBGetCatInfo, ROMlib_launch_desk_file, RelString, main, syn68k_guest_to_host,
             syn68k_host_to_guest, system_error

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Folder discovery uses the real collector with a bounded catalog fixture. */
#define NDEBUG 1
#include "../../ui/deskfiles.c"
#undef NDEBUG
#undef main
#include <assert.h>
#include <stdarg.h>
#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];
static Size bytes;
static int missing, long_length = 255;
static uint8 contents[2048];
int vdriver_width = 640, vdriver_height = 480;
static int moves;
static INTEGER moved_left, moved_top;

void C_MoveWindow(WindowPtr window, INTEGER left, INTEGER top, BOOLEAN front)
{
    assert(!front);
    ++moves;
    moved_left = left;
    moved_top = top;
}

static int launches, preferences_shown;
static OSType launch_type;
static OSErr info_error, launch_error;

OSErr C_FSpGetFInfo(FSSpecPtr spec, FInfo* info)
{
    info->fdType = CL(launch_type);
    return info_error;
}

void C_GetItem(MenuHandle menu, INTEGER item, StringPtr name)
{
    assert((menu == panels_menu && item == 1) || (menu == apple_menu && (item == 1 || item == 2)));
    if (menu == apple_menu && item == 2)
        memcpy(name, ROMAW_PREFS_MENU_NAME, sizeof ROMAW_PREFS_MENU_NAME);
    else
        memcpy(name, "\012Calculator", 11);
}

void ROMlib_show_preferences(void)
{
    ++preferences_shown;
}

void C_HiliteMenu(INTEGER menu)
{
    assert(!menu);
}

OSErr ROMlib_launch_desk_file(const FSSpec* spec)
{
    assert(CW(spec->vRefNum) == -7 && CL(spec->parID) == 22);
    assert(spec->name[0] == 10 && !memcmp(spec->name + 1, "Calculator", 10));
    ++launches;
    return launch_error;
}

OSErr NewLaunch(StringPtr name, INTEGER volume, LaunchParamBlockRec* launch)
{
    assert(0);
    return paramErr;
}

int system_error(const char* message, int button, const char* a, const char* b, const char* c,
                 system_error_callback_t d, system_error_callback_t e, system_error_callback_t f)
{
    assert(0);
    return 0;
}

void* syn68k_guest_to_host(uint32 p)
{
    return fixture_guest_to_host(p);
}

uint32 syn68k_host_to_guest(ptr_sized_uint p)
{
    return fixture_host_to_guest(p);
}

intptr_t CToPascalCall(void* code, unsigned long convention, ...)
{
    assert(code == contents);
    assert(convention == (4 | (2 << 3) | (2 << 6) | (2 << 9) | (2 << 12) | (5 << 15) | (4 << 18) | (5 << 21)));
    va_list ap;
    va_start(ap, convention);
    assert(va_arg(ap, int) == 1);
    assert(va_arg(ap, int) == 7);
    assert(!va_arg(ap, int) && !va_arg(ap, int));
    assert(va_arg(ap, EventRecord*) == (EventRecord*)0x1234);
    assert(va_arg(ap, LONGINT) == 0x12345678);
    assert(va_arg(ap, DialogPtr) == (DialogPtr)0x5678);
    va_end(ap);
    return 0x12345678;
}

OSErr C_FindFolder(int16 volume, OSType folder, Boolean create, int16* found, int32* directory)
{
    assert(volume == kOnSystemDisk && !create);
    if (missing && folder == kAppleMenuFolderType)
        return fnfErr;
    *found = CWC(-7);
    *directory = CL(folder == kAppleMenuFolderType ? 11 : 22);
    return noErr;
}

OSErrRET PBGetCatInfo(CInfoPBPtr pb, BOOLEAN async)
{
    assert(!async && CW(pb->hFileInfo.ioVRefNum) == -7);
    int index = CW(pb->hFileInfo.ioFDirIndex);
    int directory = CL(pb->hFileInfo.ioDirID);
    StringPtr name = PPR(pb->hFileInfo.ioNamePtr);
    const char* text;
    OSType type = TICK("dfil");
    pb->hFileInfo.ioFlAttrib = 0;
    if (directory == 11) {
        if (index > 6)
            return fnfErr;
        const char* names[] = {"Note Pad", "Document", ".Hidden", "Folder", "", "Calculator"};
        text = names[index - 1];
        if (index == 2)
            type = TICK("TEXT");
        if (index == 4)
            pb->hFileInfo.ioFlAttrib = ATTRIB_ISADIR;
        if (index == 5) {
            name[0] = long_length;
            memset(name + 1, 'x', long_length);
            pb->hFileInfo.ioFlFndrInfo.fdType = CL(type);
            return noErr;
        }
    }
    else {
        assert(directory == 22);
        if (index > 3)
            return fnfErr;
        const char* names[] = {"Sound", "Calculator", "Views"};
        text = names[index - 1];
        type = TICK("cdev");
    }
    name[0] = strlen(text);
    memcpy(name + 1, text, name[0]);
    pb->hFileInfo.ioFlFndrInfo.fdType = CL(type);
    return noErr;
}

Size GetHandleSize(Handle handle)
{
    return bytes;
}

INTEGERRET
RelString(StringPtr a, StringPtr b, BOOLEAN case_sensitive, BOOLEAN diacritic)
{
    unsigned n = MIN(a[0], b[0]);
    int order = strncasecmp((char*)a + 1, (char*)b + 1, n);
    return order < 0 ? -1 : order > 0 ? 1 : a[0] < b[0] ? -1 : a[0] > b[0];
}

BOOLEANRET
EqualString(StringPtr a, StringPtr b, BOOLEAN c, BOOLEAN d)
{
    return RelString(a, b, c, d) == 0;
}

LONGINT
C_Munger(Handle h, LONGINT offset, Ptr find, LONGINT length, Ptr replacement, LONGINT count)
{
    assert(!find && !length && offset >= 0 && offset <= bytes);
    assert(bytes + count <= sizeof contents);
    memmove(contents + offset + count, contents + offset, bytes - offset);
    memcpy(contents + offset, replacement, count);
    bytes += count;
    return offset;
}

int main(void)
{
    HIDDEN_Ptr master = {(Ptr)RM((Ptr)contents)};
    Handle names = (Handle)&master;
    ROMlib_desk_folder_names(names);
    const uint8 expected[] = "\012Calculator\010Note Pad";
    assert(bytes == sizeof expected - 1 && !memcmp(contents, expected, bytes));
    ROMlib_desk_folder_names(names);
    assert(bytes == sizeof expected - 1 && !memcmp(contents, expected, bytes));

    struct {
        unsigned before;
        FSSpec spec;
        unsigned after;
    } guarded;

    for (int length = 63; length <= 255; ++length) {
        memset(&guarded, 0xa5, sizeof guarded);
        long_length = length;
        OSType type;
        assert(folder_item(0, 5, &guarded.spec, &type));
        assert(guarded.before == 0xa5a5a5a5 && guarded.after == 0xa5a5a5a5);
        assert(guarded.spec.name[0] == (length == 63 ? 63 : 0));
        assert(type == (length == 63 ? TICK("dfil") : 0));
    }
    missing = 1;
    bytes = 0;
    ROMlib_desk_folder_names(names);
    assert(bytes == 0);
    folder_names(names, 1);
    const uint8 remaining[] = "\012Calculator\005Sound\005Views";
    assert(bytes == sizeof remaining - 1 && !memcmp(contents, remaining, bytes));
    ROMlib_desk_folder_names(NULL);
    {
        MenuInfo apple = {0};
        HIDDEN_Ptr apple_pointer = {(Ptr)RM((Ptr)&apple)};
        apple_menu = (MenuHandle)&apple_pointer;
        apple.menuID = CWC(128);
        assert(!ROMlib_desk_menu_select(128L << 16));
        assert(!ROMlib_desk_menu_select((128L << 16) | 1));
        assert(!ROMlib_desk_menu_select((129L << 16) | 2));
        assert(ROMlib_desk_menu_select((128L << 16) | 2));
        assert(preferences_shown == 1 && launches == 0);
        apple_menu = NULL;
        MenuInfo info = {0};
        HIDDEN_Ptr pointer = {(Ptr)RM((Ptr)&info)};
        panels_menu = (MenuHandle)&pointer;
        info.menuID = CWC(200);
        assert(!ROMlib_desk_menu_select((129L << 16) | 1));
        assert(!ROMlib_desk_menu_select(200L << 16));
        assert(ROMlib_desk_menu_select((200L << 16) | 1));
        assert(launches == 1);
        panels_menu = NULL;
        FSSpec spec = {CWC(-7), CLC(22), "\012Calculator"};
        OSErr result = 123;
        launch_type = TICK("dfil");
        assert(ROMlib_launch_separate_desk_spec(&spec, &result));
        assert(result == noErr && launches == 2);
        launch_type = TICK("cdev");
        assert(ROMlib_launch_separate_desk_spec(&spec, &result));
        assert(result == noErr && launches == 3);
        launch_error = paramErr;
        assert(ROMlib_launch_separate_desk_spec(&spec, &result));
        assert(result == paramErr && launches == 4);
        result = 123;
        launch_type = TICK("APPL");
        assert(!ROMlib_launch_separate_desk_spec(&spec, &result));
        launch_type = TICK("INIT");
        assert(!ROMlib_launch_separate_desk_spec(&spec, &result));
        launch_type = TICK("dfil");
        info_error = fnfErr;
        assert(!ROMlib_launch_separate_desk_spec(&spec, &result));
        assert(!ROMlib_launch_separate_desk_spec(NULL, &result));
        assert(!ROMlib_launch_separate_desk_spec(&spec, NULL));
        assert(result == 123 && launches == 4);
    }
    assert(panel_call(names, 1, 7, (EventRecord*)0x1234, 0x12345678, (DialogPtr)0x5678) == 0x12345678);
    {
        WindowRecord window = {0};
        Region structure = {CWC(10), {CWC(161), CWC(388), CWC(438), CWC(775)}};
        Region content = {CWC(10), {CWC(181), CWC(389), CWC(437), CWC(774)}};
        HIDDEN_RgnPtr sh = {RM(&structure)}, ch = {RM(&content)};
        PP_ASSIGN(WINDOW_STRUCT_REGION_X(&window), (RgnHandle)&sh);
        PP_ASSIGN(WINDOW_CONT_REGION_X(&window), (RgnHandle)&ch);
        fit_accessory_window((WindowPtr)&window);
        assert(moves == 1 && moved_left == 254 && moved_top == 181);
        structure.rgnBBox = (Rect){CWC(20), CWC(10), CWC(297), CWC(397)};
        content.rgnBBox = (Rect){CWC(40), CWC(11), CWC(296), CWC(396)};
        fit_accessory_window((WindowPtr)&window);
        assert(moves == 1);
        structure.rgnBBox = (Rect){CWC(-10), CWC(-40), CWC(267), CWC(347)};
        content.rgnBBox = (Rect){CWC(10), CWC(-39), CWC(266), CWC(346)};
        fit_accessory_window((WindowPtr)&window);
        assert(moves == 2 && moved_left == 1 && moved_top == 40);
        structure.rgnBBox = (Rect){CWC(300), CWC(10), CWC(577), CWC(397)};
        content.rgnBBox = (Rect){CWC(320), CWC(11), CWC(576), CWC(396)};
        fit_accessory_window((WindowPtr)&window);
        assert(moves == 3 && moved_left == 11 && moved_top == 223);
        PP_RAW(WINDOW_STRUCT_REGION_X(&window)) = 0;
        fit_accessory_window((WindowPtr)&window);
        assert(moves == 3);
    }
    puts("Desk folders: sorting, separate control-panel names, submenu routing, duplicates and FSSpec guards passed");
}
