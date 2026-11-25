/*+===================================================================
  File:      debug_entries.h

  Summary:   Implements the Carbon DebugLib component and option registry.

  Classes:   None.

  Functions: ROMlib_carbon_free_debug_components and DebugLib frame entries.

  Origin:    CarbonLib 1.4 DebugLib and Universal Interfaces Debugging.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_DEBUG_ENTRIES_H
#define ROMAW_CARBON_DEBUG_ENTRIES_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct carbon_debug_option {
    struct carbon_debug_option* next;
    uint32_t selector;
    Str255 name;
} carbon_debug_option;

struct ROMlib_carbon_debug_component {
    ROMlib_carbon_debug_component* next;
    carbon_debug_option* options;
    uint32_t signature, callback;
    Byte enabled;
    Str255 name;
};

enum {
    debug_execution_context = -13880,
    debug_duplicate_signature = -13881,
    debug_duplicate_option = -13882,
    debug_invalid_signature = -13883,
    debug_invalid_option = -13884,
    debug_invalid_name = -13885,
    debug_no_callback = -13886,
    debug_no_match = -13887,
};

static int debug_result(ROMlib_carbon_ppc_frame* call, int32_t result)
{
    call->words[0] = result;
    return 1;
}

static uint32_t debug_task_level(void)
{
    /* The parked 68K SR has an interrupt mask even during normal PPC calls. */
    uint32_t level = 0;
    /* ROMAW's VBL dispatcher sets bit 6; the classic queue scans bit 14. */
    const uint16_t vbl_flags = CW(VBLQueue.qFlags), deferred_flags = CW(DTQueue.qFlags);
    if (vbl_flags != UINT16_MAX && (vbl_flags & 0x4040))
        level |= 0x10;
    /* The Deferred Task queue retains the low-memory fill until installed. */
    if (deferred_flags != UINT16_MAX && (deferred_flags & 0x4000))
        level |= 0x20;
    return level;
}

static int carbon_TaskLevel(ROMlib_carbon_ppc_frame* call)
{
    return debug_result(call, debug_task_level());
}

static const Byte* debug_name(uint32_t address)
{
    const Byte* length = address ? ROMlib_carbon_try_guest_memory(address, 1, 0) : NULL;
    return length ? ROMlib_carbon_try_guest_memory(address, (size_t)*length + 1, 0) : NULL;
}

static ROMlib_carbon_debug_component** debug_component_link(uint32_t signature)
{
    ROMlib_carbon_debug_component** p = ROMlib_carbon_debug_component_slot();
    while (p && *p && (*p)->signature != signature)
        p = &(*p)->next;
    return p;
}

static void debug_free_component(ROMlib_carbon_debug_component* component)
{
    while (component->options) {
        carbon_debug_option* option = component->options;
        component->options = option->next;
        free(option);
    }
    free(component);
}

void ROMlib_carbon_free_debug_components(ROMlib_carbon_debug_component** components)
{
    while (*components) {
        ROMlib_carbon_debug_component* component = *components;
        *components = component->next;
        debug_free_component(component);
    }
}

static int carbon_NewDebugComponent(ROMlib_carbon_ppc_frame* call)
{
    if (debug_task_level())
        return debug_result(call, debug_execution_context);
    const Byte* name = debug_name(call->words[1]);
    if (!name)
        return debug_result(call, debug_invalid_name);
    ROMlib_carbon_debug_component** link = debug_component_link(call->words[0]);
    if (!link)
        return 0;
    if (*link)
        return debug_result(call, debug_duplicate_signature);
    ROMlib_carbon_debug_component* component = calloc(1, sizeof *component);
    if (!component)
        return debug_result(call, memFullErr);
    component->signature = call->words[0];
    component->callback = call->words[2];
    component->enabled = 1;
    memcpy(component->name, name, (size_t)*name + 1);
    *link = component;
    return debug_result(call, noErr);
}

static int carbon_DisposeDebugComponent(ROMlib_carbon_ppc_frame* call)
{
    if (debug_task_level())
        return debug_result(call, debug_execution_context);
    ROMlib_carbon_debug_component** link = debug_component_link(call->words[0]);
    if (!link || !*link)
        return debug_result(call, debug_invalid_signature);
    ROMlib_carbon_debug_component* component = *link;
    *link = component->next;
    debug_free_component(component);
    return debug_result(call, noErr);
}

static int carbon_NewDebugOption(ROMlib_carbon_ppc_frame* call)
{
    if (debug_task_level())
        return debug_result(call, debug_execution_context);
    const Byte* name = debug_name(call->words[2]);
    if (!name)
        return debug_result(call, debug_invalid_name);
    ROMlib_carbon_debug_component** link = debug_component_link(call->words[0]);
    if (!link || !*link)
        return debug_result(call, debug_invalid_signature);
    ROMlib_carbon_debug_component* component = *link;
    if (!component->callback)
        return debug_result(call, debug_no_callback);
    const uint32_t selector = call->words[1];
    if (!selector)
        return debug_result(call, debug_duplicate_option);
    carbon_debug_option** option = &component->options;
    while (*option && (*option)->selector != selector)
        option = &(*option)->next;
    if (*option)
        return debug_result(call, debug_duplicate_option);

    carbon_debug_option* entry = calloc(1, sizeof *entry);
    if (!entry)
        return debug_result(call, memFullErr);
    entry->selector = selector;
    memcpy(entry->name, name, (size_t)*name + 1);
    *option = entry;
    return debug_result(call, noErr);
}

static int carbon_GetDebugComponentInfo(ROMlib_carbon_ppc_frame* call)
{
    uint32_t index = call->words[0];
    ROMlib_carbon_debug_component** link = ROMlib_carbon_debug_component_slot();
    ROMlib_carbon_debug_component* component = link ? *link : NULL;
    while (component && index > 1) {
        --index;
        component = component->next;
    }
    if (!index || !component)
        return debug_result(call, debug_no_match);
    void* signature = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], 4, 1) : NULL;
    void* name = call->words[2] ? ROMlib_carbon_try_guest_memory(call->words[2], sizeof(Str255), 1) : NULL;
    if (!signature || (call->words[2] && !name))
        return debug_result(call, paramErr);
    const uint32_t encoded = CL(component->signature);
    memcpy(signature, &encoded, sizeof encoded);
    if (name)
        memcpy(name, component->name, sizeof component->name);
    return debug_result(call, noErr);
}

static int debug_option_callback(uint32_t callback, uint32_t selector, bool set, uint32_t setting,
                                  ROMlib_carbon_ppc_frame* call)
{
    const ROMlib_carbon_ppc_frame arguments = {.words = {selector, set ? 2 : 1, setting}};
    uint32_t result;
    return ROMlib_carbon_invoke_pascal(callback, 0xfc0, &arguments, &result) && debug_result(call, noErr);
}

static int carbon_GetDebugOptionInfo(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_debug_component** link = debug_component_link(call->words[1]);
    if (!link || !*link)
        return debug_result(call, debug_invalid_signature);
    ROMlib_carbon_debug_component* component = *link;
    uint32_t index = call->words[0];
    const int builtin = !index;
    const carbon_debug_option* option = component->options;
    while (option && index > 1) {
        --index;
        option = option->next;
    }
    if (!builtin && !option)
        return debug_result(call, debug_no_match);
    void* selector = call->words[2] ? ROMlib_carbon_try_guest_memory(call->words[2], 4, 1) : NULL;
    Byte* name = call->words[3] ? ROMlib_carbon_try_guest_memory(call->words[3], sizeof(Str255), 1) : NULL;
    Byte* setting = call->words[4] ? ROMlib_carbon_try_guest_memory(call->words[4], 1, 1) : NULL;
    if (!selector || !setting || (call->words[3] && !name))
        return debug_result(call, paramErr);
    const uint32_t number = builtin ? 0 : option->selector;
    const uint32_t encoded = CL(number);
    memcpy(selector, &encoded, sizeof encoded);
    if (builtin) {
        if (name)
            name[0] = 0;
        *setting = component->enabled;
        return debug_result(call, noErr);
    }
    if (name)
        memcpy(name, option->name, sizeof option->name);
    return debug_option_callback(component->callback, number, false, call->words[4], call);
}

static int carbon_SetDebugOptionValue(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_debug_component** link = debug_component_link(call->words[0]);
    if (!link || !*link)
        return debug_result(call, debug_invalid_signature);
    ROMlib_carbon_debug_component* component = *link;
    const uint32_t selector = call->words[1];
    _Alignas(8) Byte setting = (Byte)call->words[2];
    if (!selector) {
        component->enabled = setting;
        return debug_result(call, noErr);
    }
    const carbon_debug_option* option = component->options;
    while (option && option->selector != selector)
        option = option->next;
    if (!option)
        return debug_result(call, debug_invalid_option);
    return debug_option_callback(component->callback, selector, true, US_TO_SYN68K(&setting), call);
}

static int carbon_InstallDebugAssertOutputHandler(ROMlib_carbon_ppc_frame* call)
{
    uint32_t* handler = ROMlib_carbon_debug_handler_slot();
    if (!handler)
        return 0;
    *handler = call->words[0];
    return 1;
}

static void debug_append(Str255 output, const char* text)
{
    while (*text && output[0] < 255) {
        const unsigned position = (unsigned)output[0] + 1;
        output[position] = *text++;
        output[0] = position;
    }
}

static void debug_append_guest(Str255 output, uint32_t address)
{
    for (; address && output[0] < 255; ++address) {
        const Byte* character = ROMlib_carbon_try_guest_memory(address, 1, 0);
        if (!character || !*character)
            break;
        const unsigned position = (unsigned)output[0] + 1;
        output[position] = *character;
        output[0] = position;
    }
}

static uint32_t debug_basename(uint32_t address)
{
    uint32_t basename = address;
    for (unsigned position = 0; address && position < 4096; ++position, ++address) {
        const Byte* character = ROMlib_carbon_try_guest_memory(address, 1, 0);
        if (!character || !*character)
            break;
        if (*character == ':' && address < UINT32_MAX) {
            const Byte* next = ROMlib_carbon_try_guest_memory(address + 1, 1, 0);
            if (next && *next)
                basename = address + 1;
        }
    }
    return basename;
}

static int carbon_DebugAssert(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* slot = ROMlib_carbon_debug_handler_slot();
    const uint32_t handler = slot ? *slot : 0;
    if (!handler) {
        ROMlib_carbon_reference reference;
        if (ROMlib_carbon_load_reference("DebugLib", "DebugAssert", &reference))
            return ROMlib_carbon_invoke_frame(reference.function, call);
    }

    _Alignas(256) Str255 message = {0};
    debug_append(message, "Assert failed: ");
    for (unsigned field = 2; field <= 4; ++field) {
        if (call->words[field]) {
            debug_append_guest(message, call->words[field]);
            debug_append(message, "\r");
        }
    }
    if (call->words[5]) {
        debug_append(message, "     file: ");
        debug_append_guest(message, debug_basename(call->words[5]));
    }

    char number[16];
    if (call->words[6]) {
        snprintf(number, sizeof number, "%lu", (unsigned long)call->words[6]);
        debug_append(message, "\r     line: ");
        debug_append(message, number);
    }
    if (call->words[7]) {
        snprintf(number, sizeof number, "%ld", (long)(int32_t)call->words[7]);
        debug_append(message, "\r    error: ");
        debug_append(message, number);
    }

    if (handler) {
        ROMlib_carbon_ppc_frame arguments = *call;
        _Alignas(64) uint32_t parameter_area[15] = {0};
        parameter_area[14] = CL(US_TO_SYN68K(message));
        arguments.stack = US_TO_SYN68K(parameter_area);
        uint32_t ignored;
        return ROMlib_carbon_invoke_pascal(handler, 0xffffc0, &arguments, &ignored);
    }
    fprintf(stderr, "debugstr: %.*s\n", (int)message[0], (const char*)message + 1);
    return 1;
}

#endif
