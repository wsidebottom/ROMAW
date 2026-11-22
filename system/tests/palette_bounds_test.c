/*+===================================================================
  File:      palette_bounds_test.c

  Summary:   Negative palette indices previously overwrote pmEntries and adjacent handles.

  Classes:   None.

  Functions: CToPascalCall, C_DisposeRgn, C_FrontWindow, C_NewRgn, C_OffsetRect, C_RectRgn, C_SectRect, C_SectRgn,
             C_UnionRgn, ROMlib_window_first_all, ROMlib_window_next_all, _warning, main, syn68k_guest_to_host,
             syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Negative palette indices previously overwrote pmEntries and adjacent handles. */
#define NDEBUG 1
#include "../../quickdraw/qPaletteMgr.c"
#include "../../windows/windUpdate.c"
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
static ptr_sized_uint pointers[64];
static unsigned pointer_count;
CPUState cpu_state;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];
static WindowRecord windows[4];
static HIDDEN_RgnPtr contents[4], updates[4];
static int invalidations[4], allocations, disposals, fail_allocation;
static int windows_detached;
static RgnHandle content_region = (RgnHandle)0x2000;
static Rect device_rect;

intptr_t CToPascalCall(void* proc, unsigned long magic, ...)
{
    abort();
}

WindowPtr C_FrontWindow(void)
{
    return (WindowPtr)&windows[0];
}

WindowPeek ROMlib_window_first_all(void)
{
    return windows_detached ? NULL : &windows[0];
}

WindowPeek ROMlib_window_next_all(WindowPeek w)
{
    return w == &windows[3] ? NULL : w + 1;
}

RgnHandle C_NewRgn(void)
{
    ++allocations;
    if (allocations == fail_allocation)
        return NULL;
    return content_region;
}

void C_DisposeRgn(RgnHandle r)
{
    assert(r == content_region);
    ++disposals;
}

void C_RectRgn(RgnHandle r, Rect* rect)
{
    assert(r == content_region);
    device_rect = *rect;
}

void C_SectRgn(RgnHandle a, RgnHandle b, RgnHandle out)
{
    assert(a == content_region && b == (RgnHandle)0x3000 && out == a);
}

void C_OffsetRect(Rect* r, INTEGER h, INTEGER v)
{
    r->left = CW(CW(r->left) + h);
    r->right = CW(CW(r->right) + h);
    r->top = CW(CW(r->top) + v);
    r->bottom = CW(CW(r->bottom) + v);
}

BOOLEAN
C_SectRect(const Rect* a, const Rect* b, Rect* out)
{
    out->left = CW(MAX(CW(a->left), CW(b->left)));
    out->top = CW(MAX(CW(a->top), CW(b->top)));
    out->right = CW(MIN(CW(a->right), CW(b->right)));
    out->bottom = CW(MIN(CW(a->bottom), CW(b->bottom)));
    return CW(out->left) < CW(out->right) && CW(out->top) < CW(out->bottom);
}

void C_UnionRgn(RgnHandle a, RgnHandle b, RgnHandle out)
{
    assert(b == content_region && out == a);
    for (int i = 0; i < 4; ++i)
        if (a == &updates[i]) {
            ++invalidations[i];
            return;
        }
    abort();
}

static void test_device_updates(void)
{
    GDevice device = {0};
    HIDDEN_GDevicePtr handle = {0};
    HIDDEN_RAW_SET(handle, RM(&device));
    GDHandle gd = &handle;
    device.gdRect = (Rect){CWC(20), CWC(10), CWC(480), CWC(640)};
    struct window_palette_alist defaults = {.w = (WindowPtr)-1, .c_update = pmAllUpdates};
    struct window_palette_alist front = {.w = (WindowPtr)&windows[0], .c_update = pmFgUpdates, .next = &defaults};
    struct window_palette_alist background = {.w = (WindowPtr)&windows[2], .c_update = pmFgUpdates, .next = &front};
    window_palette_alist = &background;
    PixMap desktop_map = {0};
    HIDDEN_PixMapPtr desktop_map_handle = {0};
    for (int i = 0; i < 4; ++i) {
        windows[i].visible = TRUE;
        PP_ASSIGN(windows[i].contRgn, &contents[i]);
        PP_ASSIGN(windows[i].updateRgn, &updates[i]);
        windows[i].port.portRect = (Rect){CWC(0), CWC(0), CWC(600), CWC(800)};
        windows[i].port.portBits.bounds.top = CWC(-30);
        windows[i].port.portBits.bounds.left = CWC(-40);
    }
    /* The real Finder desktop is a color port: its origin belongs to its
     pixmap rather than the bytes occupying GrafPort.portBits.bounds. */
    CGrafPtr desktop_port = (CGrafPtr)&windows[1];
    desktop_port->portVersion = CWC(0xc000);
    HIDDEN_RAW_SET(desktop_map_handle, RM(&desktop_map));
    PP_ASSIGN(desktop_port->portPixMap, &desktop_map_handle);
    desktop_map.bounds.top = CWC(-30);
    desktop_map.bounds.left = CWC(-40);
    /* Window 1 models Finder's desktop: no structure, default palette and an
     unusable contRgn (aliased to the restricted BeginUpdate visRgn).
     Window 2 requests foreground updates but is behind the front window. */
    windows[3].visible = FALSE;
    pm_invalidate_device_windows(gd);
    assert(invalidations[0] == 1 && invalidations[1] == 1 && !invalidations[2] && !invalidations[3]);
    Rect expected = {CWC(30), CWC(40), CWC(480), CWC(640)};
    assert(!memcmp(&device_rect, &expected, sizeof device_rect));
    assert(allocations == 1 && disposals == 1);
    front.c_update = pmBkUpdates;
    background.c_update = pmBkUpdates;
    defaults.c_update = pmNoUpdates;
    allocations = disposals = 0;
    pm_invalidate_device_windows(gd);
    assert(invalidations[0] == 1 && invalidations[1] == 1 && invalidations[2] == 1 && !invalidations[3]);
    fail_allocation = 1;
    allocations = disposals = 0;
    pm_invalidate_device_windows(gd);
    assert(!disposals && invalidations[0] == 1 && invalidations[1] == 1 && invalidations[2] == 1);
    /* PaintBehind/SendBehind also repaint the desktop without a palette change.
     This damage must survive a temporary restriction of its content region. */
    fail_allocation = 0;
    allocations = disposals = 0;
    ROMlib_invalidate_window_contents((RgnHandle)0x3000);
    assert(invalidations[0] == 2 && invalidations[1] == 2 && invalidations[2] == 2 && !invalidations[3]);
    assert(disposals == 1);
    struct window_palette_alist desktop = {
        .w = (WindowPtr)&windows[1], .c_update = pmAllUpdates, .next = window_palette_alist};
    window_palette_alist = &desktop;
    windows_detached = 1;
    allocations = disposals = 0;
    ROMlib_invalidate_window_contents((RgnHandle)0x3000);
    assert(invalidations[0] == 3 && invalidations[1] == 3 && invalidations[2] == 3 && !invalidations[3]);
    assert(disposals == 1);
    windows_detached = 0;
    window_palette_alist = NULL;
}

void* syn68k_guest_to_host(uint32 p)
{
    assert(p > 0 && p <= pointer_count);
    return (void*)pointers[p - 1];
}

uint32 syn68k_host_to_guest(ptr_sized_uint p)
{
    for (unsigned i = 0; i < pointer_count; ++i)
        if (pointers[i] == p)
            return i + 1;
    assert(pointer_count < NELEM(pointers));
    pointers[pointer_count++] = p;
    return pointer_count;
}

void _warning(int e, const char* k, const char* f, int l, const char* n, const char* fmt, ...)
{
}

int main(void)
{
    test_device_updates();
    unsigned char memory[64] = {0}, before[64];
    PalettePtr ptr = (PalettePtr)(memory + 8);
    HIDDEN_PalettePtr handle;
    HIDDEN_RAW_SET(handle, RM(ptr));
    PaletteHandle palette = &handle;
    ptr->pmEntries = CWC(1);
    RGBColor color = {CWC(0x1234), CWC(0x5678), CWC(0x9abc)};
    INTEGER usage = 123, tolerance = 456;
    memcpy(before, memory, sizeof memory);
    C_SetEntryColor(palette, -1, &color);
    C_SetEntryColor(palette, 1, &color);
    C_SetEntryUsage(palette, -1, 0, 0);
    C_SetEntryUsage(palette, 1, 0, 0);
    C_GetEntryColor(palette, -1, &color);
    C_GetEntryUsage(palette, -1, &usage, &tolerance);
    assert(!memcmp(before, memory, sizeof memory));
    assert(color.red == CWC(0x1234) && usage == 123 && tolerance == 456);
    puts(
        "Palette bounds and device updates: default desktop, explicit policies, hidden windows and allocation failures passed");
    return 0;
}
