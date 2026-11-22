/*+===================================================================
  File:      window_state_test.c

  Summary:   Saved window records must tolerate nonzoomable desk accessories.

  Classes:   None.

  Functions: CToPascalCall, C_DiffRgn, C_DisposeRgn, C_EmptyRgn, C_GetWVariant, C_MoveWindow, C_NewRgn, C_OffsetRect,
             C_RectRgn, C_SizeWindow, GetHandleSize, MemError, ROMlib_window_zoomed, SetHandleSize, main,
             syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Saved window records must tolerate nonzoomable desk accessories. */
#define NDEBUG 1
#include "../../windows/layers.c"
#undef main
#undef NDEBUG
#include <assert.h>
CPUState cpu_state;
#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];
static Size state_size = 12, zoom_size = sizeof(WStateData);
static Handle state_handle;
static OSErr error;
static int fail_resize, fail_region, moves, sizes, zoom_checks;
static Rect checked, moved;
static WStateData zoom_data;
static int variant;

void* syn68k_guest_to_host(uint32 p)
{
    return fixture_guest_to_host(p);
}

uint32 syn68k_host_to_guest(ptr_sized_uint p)
{
    return fixture_host_to_guest(p);
}

intptr_t CToPascalCall(void* p, unsigned long m, ...)
{
    abort();
}

Size GetHandleSize(Handle h)
{
    return h == state_handle ? state_size : zoom_size;
}

OSErr MemError(void)
{
    return error;
}

void SetHandleSize(Handle h, Size n)
{
    assert(h == state_handle && n == 12);
    error = fail_resize ? memFullErr : noErr;
    if (!error)
        state_size = n;
}

BOOLEAN
ROMlib_window_zoomed(WindowPeek w)
{
    ++zoom_checks;
    assert(WINDOW_DATA(w));
    return TRUE;
}

void C_OffsetRect(Rect* r, INTEGER h, INTEGER v)
{
    r->left = CW(CW(r->left) + h);
    r->right = CW(CW(r->right) + h);
    r->top = CW(CW(r->top) + v);
    r->bottom = CW(CW(r->bottom) + v);
}

INTEGER
C_GetWVariant(WindowPtr w)
{
    return variant;
}

RgnHandle C_NewRgn(void)
{
    return fail_region ? NULL : (RgnHandle)0x1000;
}

void C_DisposeRgn(RgnHandle r)
{
    assert(r == (RgnHandle)0x1000);
}

void C_RectRgn(RgnHandle r, Rect* rect)
{
    checked = *rect;
}

void C_DiffRgn(RgnHandle a, RgnHandle b, RgnHandle c)
{
}

BOOLEAN
C_EmptyRgn(RgnHandle r)
{
    return CW(checked.left) >= 0 && CW(checked.top) >= 20 && CW(checked.right) <= 640 && CW(checked.bottom) <= 480;
}

void C_MoveWindow(WindowPtr w, INTEGER h, INTEGER v, BOOLEAN front)
{
    ++moves;
    moved.left = CW(h);
    moved.top = CW(v);
    assert(!front);
}

void C_SizeWindow(WindowPtr w, INTEGER h, INTEGER v, BOOLEAN update)
{
    ++sizes;
    moved.right = CW(CW(moved.left) + h);
    moved.bottom = CW(CW(moved.top) + v);
    assert(update);
}

int main(void)
{
    unsigned char* memory = calloc(1, 65536);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    window_state record = {CWC(1), {CWC(80), CWC(70), CWC(180), CWC(170)}, 0, 0};
    HIDDEN_Ptr master, zoom_master;
    HIDDEN_RAW_SET(master, RM((Ptr)&record));
    HIDDEN_RAW_SET(zoom_master, RM((Ptr)&zoom_data));
    state_handle = (Handle)&master;
    WindowRecord window = {0};
    WindowPtr w = (WindowPtr)&window;
    PORT_RECT(w) = (Rect){0, 0, CWC(100), CWC(100)};
    PORT_BOUNDS(w) = (Rect){CWC(-80), CWC(-70), CWC(400), CWC(570)};
    LONGINT length = CL(99);
    assert(ROMlib_get_window_state(NULL, state_handle, &length) == paramErr);
    assert(ROMlib_get_window_state(w, NULL, &length) == paramErr);
    assert(!ROMlib_get_window_state(w, state_handle, &length));
    assert(CL(length) == 12 && !record.zoomed && !zoom_checks);
    assert(CW(record.version) == 1 && CW(record.bounds.top) == 80 && CW(record.bounds.left) == 70);
    window.spareFlag = TRUE;
    assert(!ROMlib_get_window_state(w, state_handle, NULL) && !zoom_checks);
    PP_ASSIGN(window.dataHandle, (Handle)&zoom_master);
    zoom_data.userState = record.bounds;
    zoom_data.stdState = (Rect){CWC(40), CWC(10), CWC(460), CWC(630)};
    assert(!ROMlib_get_window_state(w, state_handle, NULL) && record.zoomed && zoom_checks == 1);
    window_state saved = record;
    fail_resize = 1;
    length = CL(99);
    assert(ROMlib_get_window_state(w, state_handle, &length) == memFullErr && CL(length) == 99);
    assert(!memcmp(&saved, &record, 12));
    fail_resize = 0;
    assert(!ROMlib_set_window_state(w, state_handle));
    assert(!memcmp(&moved, &zoom_data.stdState, sizeof moved));
    record.zoomed = FALSE;
    window.spareFlag = FALSE;
    PP_RAW(window.dataHandle) = 0;
    assert(!ROMlib_set_window_state(w, state_handle));
    assert(!memcmp(&moved, &record.bounds, sizeof moved));
    int before = moves;
    assert(ROMlib_set_window_state(NULL, state_handle) == paramErr);
    assert(ROMlib_set_window_state(w, NULL) == paramErr);
    state_size = 11;
    assert(ROMlib_set_window_state(w, state_handle) == paramErr);
    state_size = 12;
    record.version = CWC(2);
    assert(ROMlib_set_window_state(w, state_handle) == 703);
    record.version = CWC(1);
    record.zoomed = TRUE;
    assert(ROMlib_set_window_state(w, state_handle) == 702);
    window.spareFlag = TRUE;
    PP_ASSIGN(window.dataHandle, (Handle)&zoom_master);
    zoom_size = 1;
    assert(ROMlib_set_window_state(w, state_handle) == 702);
    record.zoomed = FALSE;
    record.bounds.top = CWC(10);
    assert(ROMlib_set_window_state(w, state_handle) == 704);
    record.bounds.top = CWC(80);
    record.bounds.right = record.bounds.left;
    assert(ROMlib_set_window_state(w, state_handle) == 704);
    record.bounds.right = CWC(170);
    fail_region = 1;
    assert(ROMlib_set_window_state(w, state_handle) == memFullErr);
    assert(moves == before && sizes == moves);
    puts("Window-state records: nonzoomable, zoomed, bounds, malformed and allocation failures passed");
    free(memory);
    return 0;
}
