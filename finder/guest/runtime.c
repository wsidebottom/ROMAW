/*+===================================================================
  File:      runtime.c

  Summary:   Connect Finder C procedures to real ROMAW services and guest Pascal calls.

  Classes:   None.

  Functions: ROMlib_finder_definition

  Origin:    ROMAW native Finder procedure integration.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#include "rsys/common.h"
#include "MemoryMgr.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "Gestalt.h"
#include "rsys/mman.h"
#include "finder_code.h"
#include "runtime.h"

typedef struct {
    OSErr error;
    uint32 resource;
    uint32 table_handle, table_begin, table_size;
} FinderBridge;

static int valid_range(FinderBridge* bridge, uint32 address, uint32 length)
{
    if (!address || address > ROMlib_guest_memory_size || length > ROMlib_guest_memory_size - address) {
        bridge->error = paramErr;
        return 0;
    }
    return 1;
}

static uint8_t guest_read8(void* context, uint32_t address)
{
    return valid_range(context, address, 1) ? *(uint8*)SYN68K_TO_US(address) : 0;
}

static uint32_t guest_read32(void* context, uint32_t address)
{
    FinderBridge* bridge = context;
    if (!valid_range(bridge, address, 4))
        return 0;
    if (bridge->table_handle && address != bridge->table_handle &&
        (address < bridge->table_begin || address - bridge->table_begin > bridge->table_size ||
         bridge->table_size - (address - bridge->table_begin) < 4)) {
        bridge->error = gestaltUndefSelectorErr;
        return 0;
    }
    uint32 value;
    memcpy(&value, SYN68K_TO_US(address), sizeof(value));
    return CL(value);
}

static void guest_write32(void* context, uint32_t address, uint32_t value)
{
    if (valid_range(context, address, 4)) {
        value = CL(value);
        memcpy(SYN68K_TO_US(address), &value, sizeof(value));
    }
}

static Handle guest_handle(FinderBridge* bridge, uint32 address)
{
    if (!valid_range(bridge, address, 4))
        return NULL;
    uint32 pointer;
    memcpy(&pointer, SYN68K_TO_US(address), 4);
    pointer = CL(pointer);
    if (pointer && (pointer < 16 || !valid_range(bridge, pointer - 16, 17))) {
        bridge->error = paramErr;
        return NULL;
    }
    return (Handle)SYN68K_TO_US(address);
}

static void lock(void* context, uint32_t handle)
{
    Handle native = guest_handle(context, handle);
    if (native)
        HLock(native);
}

static void unlock(void* context, uint32_t handle)
{
    Handle native = guest_handle(context, handle);
    if (native)
        HUnlock(native);
}

static int16_t new_handle(void* context, uint32_t size, uint32_t* result)
{
    (void)context;
    Handle handle = NewHandleSysClear(size);
    OSErr error = MemError();
    *result = handle ? US_TO_SYN68K(handle) : 0;
    return error;
}

static int32_t handle_size(void* context, uint32_t address)
{
    FinderBridge* bridge = context;
    Handle handle = guest_handle(bridge, address);
    if (!handle)
        return paramErr;
    Size size = GetHandleSize(handle);
    OSErr error = MemError();
    if (error)
        return error;
    uint32 begin = guest_read32(bridge, address);
    if (size && !valid_range(bridge, begin, size))
        return paramErr;
    bridge->table_handle = address;
    bridge->table_begin = begin;
    bridge->table_size = size;
    return size;
}

static uint16_t rom85(void* context)
{
    (void)context;
    return CW(ROM85);
}

static Rect guest_rect(const FinderCodeRect* source)
{
    return (Rect){CW(source->top), CW(source->left), CW(source->bottom), CW(source->right)};
}

static void pen_size(void* context, int16_t width, int16_t height)
{
    if (!((FinderBridge*)context)->error)
        C_PenSize(width, height);
}

static void frame_rect(void* context, const FinderCodeRect* source)
{
    Rect rect = guest_rect(source);
    if (!((FinderBridge*)context)->error)
        C_FrameRect(&rect);
}

static void pen_normal(void* context)
{
    if (!((FinderBridge*)context)->error)
        C_PenNormal();
}

static void erase_rect(void* context, const FinderCodeRect* source)
{
    Rect rect = guest_rect(source);
    if (!((FinderBridge*)context)->error)
        C_EraseRect(&rect);
}

static void paint_rect(void* context, const FinderCodeRect* source)
{
    Rect rect = guest_rect(source);
    if (!((FinderBridge*)context)->error)
        C_PaintRect(&rect);
}

static void fill_rect(void* context, const FinderCodeRect* source, const uint8_t pattern[8])
{
    Rect rect = guest_rect(source);
    if (!((FinderBridge*)context)->error)
        C_FillRect(&rect, (Byte*)pattern);
}

static void get_fore_color(void* context, uint16_t color[3])
{
    RGBColor native = {0};
    if (!((FinderBridge*)context)->error)
        C_GetForeColor(&native);
    color[0] = CW(native.red);
    color[1] = CW(native.green);
    color[2] = CW(native.blue);
}

static void pm_fore_color(void* context, int16_t index)
{
    if (!((FinderBridge*)context)->error)
        C_PmForeColor(index);
}

static void rgb_fore_color(void* context, const uint16_t color[3])
{
    RGBColor native = {CW(color[0]), CW(color[1]), CW(color[2])};
    if (!((FinderBridge*)context)->error)
        C_RGBForeColor(&native);
}

static void rect_region(void* context, uint32_t address, const FinderCodeRect* source)
{
    FinderBridge* bridge = context;
    Handle region = guest_handle(bridge, address);
    Rect rect = guest_rect(source);
    if (region && valid_range(bridge, guest_read32(bridge, address), 10))
        C_RectRgn((RgnHandle)region, &rect);
}

static int failed(void* context)
{
    return ((FinderBridge*)context)->error != noErr;
}

static const FinderCodeServices services = {
    .read8 = guest_read8,
    .read32 = guest_read32,
    .write32 = guest_write32,
    .lock = lock,
    .unlock = unlock,
    .new_handle = new_handle,
    .handle_size = handle_size,
    .rom85 = rom85,
    .pen_size = pen_size,
    .frame_rect = frame_rect,
    .pen_normal = pen_normal,
    .erase_rect = erase_rect,
    .paint_rect = paint_rect,
    .fill_rect = fill_rect,
    .get_fore_color = get_fore_color,
    .pm_fore_color = pm_fore_color,
    .rgb_fore_color = rgb_fore_color,
    .rect_region = rect_region,
    .failed = failed,
};

static int16_t register_hcsl(void* context, uint32_t selector, FinderHcsl0* state)
{
    FinderBridge* bridge = context;
    guest_write32(bridge, bridge->resource + 22, state->response);
    OSErr error = NewGestalt(selector, (ProcPtr)SYN68K_TO_US(bridge->resource + 26));
    EM_D0 = (EM_D0 & 0xffff0000) | (uint16)error;
    return error;
}

static syn68k_addr_t finder_callback(syn68k_addr_t pc, void* argument)
{
    (void)pc;
    unsigned index = (unsigned)(uintptr_t)argument;
    uint32 stack = EM_A7;
    FinderBridge bridge = {.resource = EM_A1};
    /* Keep callee-saved registers across any nested Toolbox/guest calls. */
    M68kReg saved[16];
    memcpy(saved, cpu_state.regs, sizeof(saved));
    if (!valid_range(&bridge, stack, index < 2 ? 20 : 14))
        gui_fatal("Invalid Finder procedure stack");
    syn68k_addr_t ret = READUL(stack);
    int32 result = 0;
    uint32 consumed = index < 2 ? 16 : 12;
    if (index == 0) {
        uint32 control = READUL(stack + 10);
        if (guest_handle(&bridge, control) && valid_range(&bridge, guest_read32(&bridge, control), 40))
            result =
                finder_cdef128(&services, &bridge, READUW(stack + 14), control, READUW(stack + 8), READUL(stack + 4));
    }
    else if (index == 1)
        result = finder_wdef128(READUW(stack + 14), READUL(stack + 10), READUW(stack + 8), READUL(stack + 4));
    else if (index == 2 && valid_range(&bridge, bridge.resource, 132)) {
        FinderGest128 state = {guest_read32(&bridge, bridge.resource + 128), bridge.resource};
        result = finder_gest128(&services, &bridge, &state, READUL(stack + 8), READUL(stack + 4));
        guest_write32(&bridge, bridge.resource + 128, state.handle);
    }
    else if (index == 3 && valid_range(&bridge, bridge.resource, 42)) {
        FinderHcsl0 state;
        finder_hcsl0_install(&state, READUL(stack + 4), register_hcsl, &bridge);
        consumed = 4; /* Original installer is caller-clean, plain RTS. */
    }
    else if (index == 4 && valid_range(&bridge, bridge.resource, 42)) {
        FinderHcsl0 state = {guest_read32(&bridge, bridge.resource + 22)};
        result = finder_hcsl0_query(&services, &bridge, &state, READUL(stack + 4));
    }
    if (bridge.error)
        result = bridge.error;
    for (unsigned reg = 2; reg < 15; ++reg)
        if (reg != 8 && reg != 9)
            cpu_state.regs[reg] = saved[reg];
    EM_A7 = stack + consumed;
    if (index < 2)
        WRITEUL(EM_A7, result);
    else if (index != 3)
        WRITEUW(EM_A7, result);
    return ret;
}

ProcPtr ROMlib_finder_definition(unsigned index)
{
    static syn68k_addr_t callbacks[5];
    if (index >= 5)
        return NULL;
    if (!callbacks[index])
        callbacks[index] = callback_install(finder_callback, (void*)(uintptr_t)index);
    return (ProcPtr)SYN68K_TO_US(callbacks[index]);
}
