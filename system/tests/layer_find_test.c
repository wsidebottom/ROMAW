/*+===================================================================
  File:      layer_find_test.c

  Summary:   LayerFind returns the owner of the frontmost visible structure region.

  Classes:   None.

  Functions: CToPascalCall, C_ClosePort, C_CopyRgn, C_DisposeRgn, C_NewRgn, C_OpenPort, C_PtInRgn, C_SetPort, DisposPtr,
             _NewPtr_flags, main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* LayerFind returns the owner of the frontmost visible structure region. */
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

Ptr _NewPtr_flags(Size n, boolean_t s, boolean_t c)
{
    abort();
}

void DisposPtr(Ptr p)
{
    abort();
}

void C_OpenPort(GrafPtr p)
{
    abort();
}

void C_ClosePort(GrafPtr p)
{
    abort();
}

void C_SetPort(GrafPtr p)
{
    abort();
}

RgnHandle C_NewRgn(void)
{
    abort();
}

void C_DisposeRgn(RgnHandle p)
{
    abort();
}

void C_CopyRgn(RgnHandle a, RgnHandle b)
{
    abort();
}

BOOLEAN
C_PtInRgn(Point p, RgnHandle region)
{
    unsigned id = (uintptr_t)region - ROMlib_offset;
    return p.h >= 0 && p.h < 100 && p.v >= 0 && p.v < (id == 0x7000 ? 100 : 40);
}

int main(void)
{
    unsigned char* memory = calloc(1, 65536);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    root_layer = (layer_record*)(memory + 0x1000);
    application_layer = (layer_record*)(memory + 0x2000);
    floating_layer = (layer_record*)(memory + 0x3000);
    WindowPeek app = (WindowPeek)(memory + 0x4000), floating = (WindowPeek)(memory + 0x5000);
    root_layer->children = CL(US_TO_SYN68K(floating_layer));
    floating_layer->next = CL(US_TO_SYN68K(application_layer));
    floating_layer->visible = application_layer->visible = TRUE;
    floating_layer->children = CL(US_TO_SYN68K(floating));
    application_layer->children = CL(US_TO_SYN68K(app));
    app->visible = floating->visible = TRUE;
    PP_ASSIGN(app->strucRgn, (RgnHandle)(memory + 0x6000));
    PP_ASSIGN(floating->strucRgn, (RgnHandle)(memory + 0x6000));
    HIDDEN_RAW_SET(GrayRgn_H, RM((RgnHandle)(memory + 0x7000)));
    current_layer = floating_layer;
    assert(ROMlib_layer_application() == application_layer);
    current_layer = root_layer;
    assert(ROMlib_layer_application() == application_layer);
    current_layer = NULL;
    Point point = {10, 10};
    assert(ROMlib_layer_find(point) == floating_layer);
    floating->visible = FALSE;
    assert(ROMlib_layer_find(point) == application_layer);
    floating->visible = TRUE;
    floating_layer->visible = FALSE;
    assert(ROMlib_layer_find(point) == application_layer);
    point.v = 70;
    assert(ROMlib_layer_find(point) == application_layer);
    point.h = 200;
    assert(!ROMlib_layer_find(point));
    free(memory);
    puts("LayerFind: frontmost ownership, hidden layers/windows, desktop and outside points passed");
    return 0;
}
