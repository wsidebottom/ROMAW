/*+===================================================================
  File:      fill_pattern_test.c

  Summary:   Implements fill pattern test support for regression tests and test fixtures.

  Classes:   None.

  Functions: CToPascalCall, C_CopyRgn, C_NewPixPat, GetHandleSize, ROMlib_fill_pixpat, main, syn68k_guest_to_host,
             syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#define NDEBUG 1
/* A fill pattern at guest address zero is data, not a host null pointer. */
#include "rsys/common.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "rsys/cquick.h"
static GrafPtr test_port;
#undef thePort
#undef theCPort
#define thePort test_port
#define theCPort ((CGrafPtr)test_port)
#include "../../quickdraw/qGrafport.c"
#undef main
#undef NDEBUG
#include <assert.h>
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];

intptr_t CToPascalCall(void* proc, unsigned long magic, ...)
{
    abort();
}

#include "guest_test_memory.h"

uint64 ROMlib_offset;
INTEGER ROMlib_qd_error;

Size GetHandleSize(Handle handle)
{
    return 0x32;
}

uint32 syn68k_address_mask = 0xFFFFFFFFUL;
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

PixPatHandle C_NewPixPat(void)
{
    abort();
}

void ROMlib_fill_pixpat(PixPatHandle pattern)
{
    abort();
}

static int region_copies;

void C_CopyRgn(RgnHandle source, RgnHandle destination)
{
    assert(source && destination);
    ++region_copies;
    memcpy(STARH(destination), STARH(source), 10);
}

int main(void)
{
    unsigned char* memory = calloc(1, 65536);
    unsigned char explicit_pattern[8] = {0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81};
    PixPatHandle fill_handle;
    int color, zero, iteration;
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    test_port = (GrafPtr)(memory + 0x1000);
    fill_handle = (PixPatHandle)(memory + 0x2000);
    *(uint32*)fill_handle = CL(0x2100);
    Point origin, expected_origin = {-3, 5};
    memset(test_port, 0, sizeof(CGrafPort));
    ROMlib_QDGetPatternOrigin(&origin);
    assert(!origin.h && !origin.v && !ROMlib_qd_error);
    ROMlib_QDGetPatternOrigin(NULL);
    assert(ROMlib_qd_error == paramErr);
    ROMlib_QDSetPatternOrigin(expected_origin);
    ROMlib_QDGetPatternOrigin(&origin);
    assert(CW(origin.v) == -3 && CW(origin.h) == 5);
    Point native = ROMlib_pattern_origin();
    assert(native.v == -3 && native.h == 5);
    /* Normal color ports share the global origin. Extended System 9 GrafVars
     can opt into independent per-port storage without modifying old layouts. */
    ((CGrafPtr)test_port)->portVersion = CWC(0xC000);
    ROMlib_QDGetPatternOrigin(&origin);
    assert(CW(origin.v) == -3 && CW(origin.h) == 5);
    GrafVarsHandle vars = (GrafVarsHandle)(memory + 0x3000);
    *(uint32*)vars = CL(0x3100);
    PP_ASSIGN(CPORT_GRAFVARS_X(test_port), (Handle)vars);
    STARH(vars)->pmFlags = CWC(0x4000);
    ROMlib_QDSetPatternOrigin((Point){7, -9});
    ROMlib_QDGetPatternOrigin(&origin);
    assert(CW(origin.v) == 7 && CW(origin.h) == -9);
    assert(!memcmp(&origin, memory + 0x312e, sizeof origin));
    PP_RAW(CPORT_GRAFVARS_X(test_port)) = 0;
    ROMlib_QDGetPatternOrigin(&origin);
    assert(CW(origin.v) == -3 && CW(origin.h) == 5);
    ROMlib_QDSetPatternOrigin((Point){0, 0});
    for (color = 0; color < 2; ++color) {
        memset(test_port, 0, sizeof(CGrafPort));
        if (color) {
            ((CGrafPtr)test_port)->portVersion = CWC(0xC000);
            PP_ASSIGN(CPORT_FILL_PIXPAT_X(test_port), fill_handle);
            PIXPAT_TYPE_X(fill_handle) = CWC(pixpat_type_orig);
        }
        for (zero = 0; zero < 2; ++zero)
            for (iteration = 0; iteration < 256; ++iteration) {
                const void* expected;
                memset(memory, iteration, 8);
                expected = zero ? memory : explicit_pattern;
                ROMlib_fill_pat(zero ? NULL : (Ptr)explicit_pattern);
                assert(!memcmp(color ? PIXPAT_1DATA(fill_handle) : PORT_FILL_PAT(test_port), expected, 8));
                assert(memory[0] == iteration && memory[7] == iteration);
            }
    }
    RgnHandle source = (RgnHandle)(memory + 0x4000), destination = (RgnHandle)(memory + 0x4200);
    *(uint32*)source = CL(0x4100);
    *(uint32*)destination = CL(0x4300);
    const unsigned char region[10] = {0, 10, 0, 1, 0, 2, 0, 3, 0, 4};
    memcpy(STARH(source), region, sizeof region);
    memset(STARH(destination), 0xa5, sizeof region);
    PP_ASSIGN(PORT_CLIP_REGION_X(test_port), destination);
    C_SetClip(NULL);
    assert(!region_copies && PORT_CLIP_REGION(test_port) == destination);
    for (unsigned i = 0; i < sizeof region; ++i)
        assert(((unsigned char*)STARH(destination))[i] == 0xa5);
    C_SetClip(source);
    assert(region_copies == 1 && !memcmp(STARH(destination), region, sizeof region));
    free(memory);
    puts("PASS: guest-zero and nonzero fill patterns in monochrome and color ports");
    return 0;
}
