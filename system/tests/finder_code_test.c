/*+===================================================================
  File:      finder_code_test.c

  Summary:   Exercise translated Finder resource procedures with recording services.

  Classes:   C has no classes.

  Functions: main

  Origin:    ROMAW Finder resource translation regression fixture.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#include "../../finder/guest/finder_code.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t memory[8192];
static char calls[128];
static size_t call_count;
static FinderCodeRect last_rect;
static uint32_t last_region;
static uint16_t rom_flags;
static int16_t allocation_error, palette_index;
static int32_t allocation_size;
static unsigned allocations;
static FinderHcsl0* installed;

static void record(char call)
{
    assert(call_count + 1 < sizeof(calls));
    calls[call_count++] = call;
    calls[call_count] = 0;
}

static uint8_t read8(void* context, uint32_t address)
{
    assert(context == memory && address < sizeof(memory));
    return memory[address];
}

static uint32_t read32(void* context, uint32_t address)
{
    assert(context == memory && address <= sizeof(memory) - 4);
    return (uint32_t)memory[address] << 24 | (uint32_t)memory[address + 1] << 16 | (uint32_t)memory[address + 2] << 8 |
           memory[address + 3];
}

static void write32(void* context, uint32_t address, uint32_t value)
{
    assert(context == memory && address <= sizeof(memory) - 4);
    memory[address] = (uint8_t)(value >> 24);
    memory[address + 1] = (uint8_t)(value >> 16);
    memory[address + 2] = (uint8_t)(value >> 8);
    memory[address + 3] = (uint8_t)value;
}

static void lock(void* context, uint32_t handle)
{
    assert(context == memory && handle == 100);
    record('L');
}

static void unlock(void* context, uint32_t handle)
{
    assert(context == memory && handle == 100);
    record('U');
}

static int16_t new_handle(void* context, uint32_t size, uint32_t* handle)
{
    assert(context == memory && size == 0);
    ++allocations;
    *handle = 500;
    return allocation_error;
}

static int32_t handle_size(void* context, uint32_t handle)
{
    assert(context == memory && handle == 500);
    return allocation_size;
}

static uint16_t rom85(void* context)
{
    assert(context == memory);
    return rom_flags;
}

static void pen_size(void* context, int16_t width, int16_t height)
{
    assert(context == memory && width == 2 && height == 2);
    record('S');
}

static void frame_rect(void* context, const FinderCodeRect* rect)
{
    assert(context == memory);
    last_rect = *rect;
    record('F');
}

static void pen_normal(void* context)
{
    assert(context == memory);
    record('N');
}

static void erase_rect(void* context, const FinderCodeRect* rect)
{
    assert(context == memory);
    last_rect = *rect;
    record('E');
}

static void paint_rect(void* context, const FinderCodeRect* rect)
{
    assert(context == memory);
    last_rect = *rect;
    record('P');
}

static void fill_rect(void* context, const FinderCodeRect* rect, const uint8_t pattern[8])
{
    assert(context == memory);
    for (unsigned i = 0; i < 8; ++i)
        assert(pattern[i] == (i % 2 ? 0x55 : 0xaa));
    last_rect = *rect;
    record('G');
}

static void get_fore_color(void* context, uint16_t rgb[3])
{
    assert(context == memory);
    rgb[0] = 1;
    rgb[1] = 2;
    rgb[2] = 3;
    /* Simulate movement during a Toolbox call. */
    memcpy(memory + 300, memory + 200, 64);
    write32(context, 100, 300);
    memory[338] = 0xff;
    memory[339] = 0xfe;
    record('C');
}

static void pm_fore_color(void* context, int16_t index)
{
    assert(context == memory);
    palette_index = index;
    record('I');
}

static void rgb_fore_color(void* context, const uint16_t rgb[3])
{
    assert(context == memory && rgb[0] == 1 && rgb[1] == 2 && rgb[2] == 3);
    record('R');
}

static void rect_region(void* context, uint32_t region, const FinderCodeRect* rect)
{
    assert(context == memory);
    last_region = region;
    last_rect = *rect;
    record('Q');
}

static int16_t register_handler(void* context, uint32_t selector, FinderHcsl0* handler)
{
    assert(context == memory && selector == UINT32_C(0x6863736c));
    installed = handler;
    return -5552; /* Installation deliberately ignores registration failure. */
}

static const FinderCodeServices services = {read8,         read32,         write32,     lock,      unlock,
                                            new_handle,    handle_size,    rom85,       pen_size,  frame_rect,
                                            pen_normal,    erase_rect,     paint_rect,  fill_rect, get_fore_color,
                                            pm_fore_color, rgb_fore_color, rect_region, NULL};

static void reset_control(void)
{
    memset(memory, 0, sizeof(memory));
    write32(memory, 100, 200);
    write32(memory, 208, UINT32_C(0xfff60014)); /* top=-10, left=20 */
    write32(memory, 212, UINT32_C(0x001e003c)); /* bottom=30, right=60 */
    memory[216] = 1;
    call_count = 0;
    calls[0] = 0;
    rom_flags = 0;
}

static void test_control(void)
{
    reset_control();
    assert(finder_cdef128(&services, memory, 123, 100, 0, 0) == 10);
    assert(!strcmp(calls, "LEFCIPRU") && palette_index == -2);
    assert(last_rect.top == -7 && last_rect.left == 23 && last_rect.bottom == 27 && last_rect.right == 57);

    reset_control();
    rom_flags = 0x4000;
    assert(finder_cdef128(&services, memory, 0, 100, 0, 99) == 10);
    assert(!strcmp(calls, "LEFGU"));

    reset_control();
    memory[217] = 255;
    assert(finder_cdef128(&services, memory, 0, 100, 0, 0) == 10);
    assert(!strcmp(calls, "LSFNU") && last_rect.top == -10 && last_rect.left == 20);

    reset_control();
    memory[216] = 0;
    assert(finder_cdef128(&services, memory, 0, 100, 0, 0) == 0);
    assert(!strcmp(calls, "LU"));
    assert(finder_cdef128(&services, memory, 0, 100, 1, UINT32_C(0xfff90017)) == 0);
    memory[216] = 1;
    memory[217] = 255;
    assert(finder_cdef128(&services, memory, 0, 100, 1, UINT32_C(0xfff90017)) == 10);
    assert(finder_cdef128(&services, memory, 0, 100, 1, UINT32_C(0x001b0017)) == 0);
    assert(finder_cdef128(&services, memory, 0, 100, 1, UINT32_C(0xfff90039)) == 0);
    assert(finder_cdef128(&services, memory, 0, 100, 1, UINT32_C(0xfff80017)) == 0);
    assert(finder_cdef128(&services, memory, 0, 100, 2, UINT32_C(0xab000123)) == 10);
    assert(last_region == 0x123 && last_rect.top == -10 && last_rect.right == 60);
    assert(finder_cdef128(&services, memory, 0, 100, -1, 0) == 10);
}

static void test_gestalt(void)
{
    FinderGest128 state = {0, 0x12345678};
    allocation_error = -108;
    write32(memory, 700, 0xfeedface);
    assert(finder_gest128(&services, memory, &state, 0x666e648c, 700) == -108);
    assert(!state.handle && read32(memory, 700) == 0xfeedface && allocations == 1);
    allocation_error = 0;
    allocation_size = 0;
    assert(finder_gest128(&services, memory, &state, 0x666e648c, 700) == 0);
    assert(state.handle == 500 && read32(memory, 700) == state.entry_address && allocations == 2);
    assert(finder_gest128(&services, memory, &state, 0x666e64f6, 700) == 0);
    assert(read32(memory, 700) == 500 && allocations == 2);
    allocation_size = -109;
    assert(finder_gest128(&services, memory, &state, 0x666e64f6, 700) == -109);
    assert(read32(memory, 700) == 500);
    allocation_size = 0;
    assert(finder_gest128(&services, memory, &state, 42, 700) == -5551);
    assert(read32(memory, 700) == 500);

    write32(memory, 500, 1000);
    write32(memory, 1000, 42);
    write32(memory, 1004, 0xcafebabe);
    allocation_size = 8;
    assert(finder_gest128(&services, memory, &state, 42, 700) == 0);
    assert(read32(memory, 700) == 0xcafebabe);
    /* The original shifts size left by 8: a size of 1 still searches
     * 256 entries. A corrected size/8 implementation would fail this. */
    allocation_size = 1;
    write32(memory, 1008, 43);
    write32(memory, 1012, 0x10203040);
    assert(finder_gest128(&services, memory, &state, 43, 700) == 0);
    assert(read32(memory, 700) == 0x10203040);
    assert(finder_gest128(&services, memory, &state, 44, 700) == -5551);
    assert(read32(memory, 700) == 0x10203040);
    allocation_size = 0x800000;
    assert(finder_gest128(&services, memory, &state, 42, 700) == -5551);
}

int main(void)
{
    test_control();
    test_gestalt();
    FinderHcsl0 state = {0};
    assert(finder_hcsl0_query(&services, memory, &state, 700) == 0 && read32(memory, 700) == 0);
    finder_hcsl0_install(&state, 0x12345678, register_handler, memory);
    assert(installed == &state);
    assert(finder_hcsl0_query(&services, memory, &state, 700) == 0 && read32(memory, 700) == 0x12345678);
    finder_hcsl0_install(&state, 0x87654321, register_handler, memory);
    assert(finder_hcsl0_query(&services, memory, &state, 700) == 0 && read32(memory, 700) == 0x87654321);
    for (int16_t message = -1; message < 10; ++message)
        assert(finder_wdef128(12, 0xffffffff, message, 0xffffffff) == 0);
    puts("Finder code translations: all checks passed");
    return 0;
}
