/*+===================================================================
  File:      icon_lifetime_test.c

  Summary:   Reload purged icon families before drawing, across resource heap moves.

  Classes:   None.

  Functions: CToPascalCall, C_LoadResource, GetHandleSize, HGetState, HLock, HSetState, ROMlib_host_disk_color_icon,
             _gui_fatal, _warning, main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Reload purged icon families before drawing, across resource heap moves. */
#define NDEBUG 1
#include "../../ui/icon.c"
#undef main
#undef NDEBUG
#include <assert.h>

__typeof__(ROMlib_offset) ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];
const int ROMlib_log2[] = {0, 0, 1, 0, 2, 0, 0, 0, 3};
struct qd_color_elt ROMlib_QDColors[8];
static Handle suite, mask_handle, color, small_mask;
static unsigned loads, fail_mask, fail_color;
static SignedByte mask_flags = 0x60;

void* syn68k_guest_to_host(uint32 address)
{
    return (void*)(uintptr_t)(ROMlib_offset + address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return address - ROMlib_offset;
}

void _warning(int err, const char* kind, const char* file, int line, const char* fn, const char* fmt, ...)
{
}

void _gui_fatal(const char* file, int line, const char* fn, const char* fmt, ...)
{
    abort();
}

intptr_t CToPascalCall(void* procedure, unsigned long magic, ...)
{
    abort();
}

Size GetHandleSize(Handle handle)
{
    if (handle == suite)
        return sizeof(cotton_suite_layout_t);
    assert(handle == mask_handle || handle == color || handle == small_mask);
    return !STARH(handle) ? 0 : handle == color ? 1024 : handle == mask_handle ? 256 : 64;
}

SignedByte HGetState(Handle handle)
{
    assert(handle == mask_handle || handle == small_mask);
    return handle == mask_handle ? mask_flags : 0;
}

void HLock(Handle handle)
{
    assert(handle == mask_handle || handle == small_mask);
    if (handle == mask_handle)
        mask_flags |= LOCKBIT;
}

void HSetState(Handle handle, SignedByte state)
{
    assert(handle == mask_handle || handle == small_mask);
    if (handle == mask_handle)
        mask_flags = state;
}

void C_LoadResource(Handle handle)
{
    ++loads;
    assert(handle == mask_handle || handle == color);
    if ((handle == mask_handle && fail_mask) || (handle == color && fail_color))
        return;
    if (handle == color) {
        assert(mask_flags & LOCKBIT);
        void* old = STARH(suite);
        void* moved = (void*)(uintptr_t)(ROMlib_offset + 0x8000);
        memcpy(moved, old, sizeof(cotton_suite_layout_t));
        memset(old, 0, sizeof(cotton_suite_layout_t));
        HIDDEN_RAW_SET(*suite, RM((Ptr)moved));
    }
    HIDDEN_RAW_SET(*handle, RM((Ptr)(uintptr_t)(ROMlib_offset + (handle == mask_handle ? 0x4000 : 0x5000))));
}

Handle ROMlib_host_disk_color_icon(Handle icon, int* depth)
{
    assert(icon == mask_handle || icon == small_mask);
    return NULL;
}

int main(void)
{
    unsigned char* memory = calloc(1, 65536);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    suite = (Handle)(memory + 0x1000);
    mask_handle = (Handle)(memory + 0x2000);
    color = (Handle)(memory + 0x2004);
    small_mask = (Handle)(memory + 0x2008);
    HIDDEN_RAW_SET(*suite, RM((Ptr)(memory + 0x3000)));
    Handle* icons = (Handle*)STARH(suite);
    icons[0] = mask_handle;
    icons[2] = color;
    Handle image, selected_mask;
    boolean_t small;
    int depth;
    assert(find_best_icon(FALSE, 8, suite, &image, &selected_mask, &small, &depth) == noErr);
    assert(image == color && selected_mask == mask_handle && !small && depth == 8);
    assert(loads == 2 && mask_flags == 0x60 && STARH(mask_handle));
    fail_color = 1;
    HIDDEN_RAW_SET(*color, 0);
    assert(find_best_icon(FALSE, 8, suite, &image, &selected_mask, &small, &depth) == noErr);
    assert(image == mask_handle && depth == 1 && mask_flags == 0x60);
    fail_mask = 1;
    HIDDEN_RAW_SET(*mask_handle, 0);
    icons = (Handle*)STARH(suite);
    icons[3] = small_mask;
    HIDDEN_RAW_SET(*small_mask, RM((Ptr)(memory + 0x7000)));
    assert(find_best_icon(FALSE, 8, suite, &image, &selected_mask, &small, &depth) == noErr);
    assert(small && image == small_mask && selected_mask == small_mask);
    HIDDEN_RAW_SET(*small_mask, 0);
    /* No third resource load for an unrecoverable empty borrowed small mask_handle. */
    icons[3] = NULL;
    assert(find_best_icon(FALSE, 8, suite, &image, &selected_mask, &small, &depth) == noMaskFoundErr);
    assert(find_best_icon(FALSE, 8, NULL, &image, &selected_mask, &small, &depth) == paramErr);
    free(memory);
    puts("purged icon masks, suite relocation and colour fallback passed");
    return 0;
}
