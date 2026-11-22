/*+===================================================================
  File:      icon_action_test.c

  Summary:   Exercise callback selection, guest handle storage, mutation and errors.

  Classes:   None.

  Functions: CToPascalCall, GetHandleSize, _NewHandle_flags, _gui_fatal, _warning, main, syn68k_guest_to_host,
             syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Exercise callback selection, guest handle storage, mutation and errors. */
#define NDEBUG 1
#include "../../ui/icon.c"
#undef main
#undef NDEBUG
#include <assert.h>
__typeof__(ROMlib_offset) ROMlib_offset;
uint32 syn68k_address_mask = 0xFFFFFFFFUL;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;

void* syn68k_guest_to_host(uint32 address)
{
    abort();
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    abort();
}

void _warning(int err, const char* kind, const char* file, int line, const char* fn, const char* fmt, ...)
{
}

void _gui_fatal(const char* file, int line, const char* fn, const char* fmt, ...)
{
    abort();
}
struct qd_color_elt ROMlib_QDColors[8];
static Handle active_suite, replacement;
static int calls, stop_at, relocate, seen[6];

Handle _NewHandle_flags(Size size, boolean_t system, boolean_t clear)
{
    assert(size == sizeof(cotton_suite_layout_t) && !system && clear);
    Handle result = (Handle)((unsigned char*)ROMlib_offset + 0x5000);
    HIDDEN_RAW_SET(*result, RM((Ptr)((unsigned char*)ROMlib_offset + 0x6000)));
    memset(STARH(result), 0, size);
    return result;
}

static Size suite_size = sizeof(cotton_suite_layout_t);

Size GetHandleSize(Handle h)
{
    assert(h == active_suite);
    return suite_size;
}

intptr_t CToPascalCall(void* proc, unsigned long magic, ...)
{
    va_list ap;
    ResType type;
    Handle* guest;
    void* data;
    int i;
    if (proc == (void*)0x9876) {
        assert(magic == (5 | (4 << 3) | (5 << 6)));
        va_start(ap, magic);
        type = va_arg(ap, ResType);
        assert(va_arg(ap, void*) == (void*)0x5678);
        va_end(ap);
        for (i = 0; i < 6 && restype_for_icon[i] != type; ++i) {
        }
        assert(i < 6 && !seen[i]++);
        return i == 2 ? 0 : (intptr_t)replacement;
    }
    assert(proc == (void*)0x1234 && magic == CTOP_IconActionProcTemplate);
    va_start(ap, magic);
    type = va_arg(ap, ResType);
    guest = va_arg(ap, Handle*);
    data = va_arg(ap, void*);
    va_end(ap);
    assert(data == (void*)0x5678);
    for (i = 0; i < 6 && restype_for_icon[i] != type; ++i) {
    }
    assert(i < 6 && !seen[i]++);
    assert(*guest == RM(((Handle*)STARH(active_suite))[i]));
    *guest = RM(replacement);
    ++calls;
    if (relocate) {
        unsigned char* moved = (unsigned char*)ROMlib_offset + 0x3000;
        memcpy(moved, STARH(active_suite), sizeof(cotton_suite_layout_t));
        HIDDEN_RAW_SET(*active_suite, RM((Ptr)moved));
        relocate = 0;
    }
    return calls == stop_at ? -42 : noErr;
}

int main(void)
{
    unsigned char* memory = calloc(1, 65536);
    Handle* icons;
    int i;
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    active_suite = (Handle)(memory + 0x1000);
    HIDDEN_RAW_SET(*active_suite, RM((Ptr)(memory + 0x2000)));
    icons = (Handle*)STARH(active_suite);
    for (i = 0; i < 6; ++i)
        icons[i] = (Handle)(memory + 0x4000 + 4 * i);
    replacement = (Handle)(memory + 0x4100);
    Handle output = NULL;
    assert(C_AddIconToSuite(replacement, active_suite, mini1BitMask) == paramErr);
    assert(C_GetIconFromSuite(&output, active_suite, TICK("icns")) == paramErr);
    assert(output == NULL && icons[0] == (Handle)(memory + 0x4000));
    assert(C_AddIconToSuite(replacement, active_suite, large4BitData) == noErr);
    assert(C_GetIconFromSuite(&output, active_suite, large4BitData) == noErr);
    assert(output == RM(replacement));
    uint32 guest_output[] = {0, CLC(0xa5a5a5a5)};
    assert(C_GetIconFromSuite((Handle*)guest_output, active_suite, large4BitData) == noErr);
    assert(guest_output[0] == (uint32)(uintptr_t)RM(replacement));
    assert(guest_output[1] == CLC(0xa5a5a5a5));
    assert(C_NewIconSuite((Handle*)guest_output) == noErr);
    assert(guest_output[0] == CLC(0x5000));
    assert(guest_output[1] == CLC(0xa5a5a5a5));
    cotton_suite_layout_t* created_suite = (cotton_suite_layout_t*)STARH((Handle)(memory + 0x5000));
    assert(created_suite->signature == CLC(ICON_SUITE_SIGNATURE));
    relocate = 1;
    assert(C_ForEachIconDo(active_suite, svLarge1Bit | svSmall8Bit, (IconActionProcPtr)0x1234, (void*)0x5678) == noErr);
    assert(calls == 2 && seen[0] && seen[5]);
    icons = (Handle*)STARH(active_suite);
    assert(icons[0] == replacement && icons[5] == replacement);
    memset(seen, 0, sizeof seen);
    calls = 0;
    stop_at = 2;
    assert(C_ForEachIconDo(active_suite, svAllAvailableData, (IconActionProcPtr)0x1234, (void*)0x5678) == -42);
    assert(calls == 2 && seen[0] && seen[1] && !seen[2]);
    assert(((Handle*)STARH(active_suite))[1] == replacement);
    memset(icons, 0, sizeof(cotton_suite_layout_t));
    calls = 0;
    assert(C_ForEachIconDo(active_suite, svAllAvailableData, (IconActionProcPtr)0x1234, (void*)0x5678) == noErr &&
           !calls);
    assert(C_ForEachIconDo(NULL, 0, (IconActionProcPtr)0x1234, NULL) == paramErr);
    assert(C_ForEachIconDo(active_suite, 0, NULL, NULL) == paramErr);
    suite_size = 0;
    assert(C_ForEachIconDo(active_suite, svAllAvailableData, (IconActionProcPtr)0x1234, NULL) == paramErr && !calls);
    Handle method_suite;
    memset(seen, 0, sizeof seen);
    assert(suite_from_method((IconGetterProcPtr)0x9876, (void*)0x5678, &method_suite) == noErr);
    assert(method_suite == (Handle)(memory + 0x5000));
    for (i = 0; i < 6; ++i)
        assert(seen[i] == 1 && ((Handle*)STARH(method_suite))[i] == (i == 2 ? NULL : replacement));
    assert(suite_from_method(NULL, NULL, &method_suite) == paramErr);
    Str255 label;
    RGBColor color;
    assert(C_GetLabel(7, NULL, label) == noErr && label[0]);
    assert(C_GetLabel(7, &color, NULL) == noErr);
    assert(C_GetLabel(7, NULL, NULL) == noErr);
    assert(C_GetLabel(8, NULL, NULL) == paramErr);
    free(memory);
    puts(
        "PASS: icon callbacks select existing entries, preserve guest byte order, replace handles, survive relocation and stop on error");
    return 0;
}
