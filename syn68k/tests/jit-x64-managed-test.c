/*+===================================================================
  File:      jit-x64-managed-test.c

  Summary:   Exercise real RX native frames through eviction, recursive callbacks and nonlocal unwind.

  Classes:   None.

  Functions: main; private intercepted page release and callback fixtures.

  Origin:    Original ARDI Executor native callback-lifetime regression fixture.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#define BOOL Syn68kRuntimeBool
#include "syn68k_private.h"
#undef BOOL
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>

static unsigned frees;
static void* freed[32];

static int __stdcall managed_free(void* address, size_t size, unsigned long type)
{
    assert(frees < sizeof freed / sizeof *freed);
    freed[frees++] = address;
    return VirtualFree(address, size, type);
}

/* Compile the maintained publisher/manager privately so the fixture can emit
   a callback-returning body before architectural memory emission uses it. */
#define VirtualFree managed_free
#define __declspec(x)
#include "../src/runtime/jit-x64.c"
#undef __declspec
#undef VirtualFree

static void *outer_code, *inner_code;
static unsigned depth;
static unsigned packet_mode;
static jmp_buf escape;
static void* checkpoint;

static void mapped(void* code)
{
    MEMORY_BASIC_INFORMATION information;
    assert(VirtualQuery(code, &information, sizeof information) == sizeof information);
    assert(information.State == MEM_COMMIT && information.Protect == PAGE_EXECUTE_READ);
}

static void* callback_body(uint32 (*callback)(CPUState*), unsigned managed)
{
    emitter out = {.managed = managed};
    const unsigned char enter[] = {0x48, 0x83, 0xec, 40, 0x48, 0xb8};
    memcpy(out.bytes, enter, sizeof enter);
    out.size = sizeof enter;
    uintptr_t target = (uintptr_t)callback;
    memcpy(out.bytes + out.size, &target, sizeof target);
    out.size += sizeof target;
    const unsigned char leave[] = {0xff, 0xd0, 0x48, 0x83, 0xc4, 40, 0x83, 0xc0, 7, 0xc3};
    memcpy(out.bytes + out.size, leave, sizeof leave);
    out.size += sizeof leave;
    void* code = publish_code(&out);
    assert(code && !((uintptr_t)code & 15));
    const Syn68kX64CodeHeader* header = (const void*)((unsigned char*)code - sizeof *header);
    assert(header->managed == managed && !(header->reserved[0] | header->reserved[1] | header->reserved[2]));
    mapped(code);
    return code;
}

static uint32 inner_callback(CPUState* state)
{
    assert(state == &cpu_state && syn68k_native_checkpoint());
    memset(&fpu_invocation, 0xa5, sizeof fpu_invocation);
    indexed_next_pc = 0xdeadbeef;
    syn68k_x64_release(inner_code);
    syn68k_x64_release(outer_code);
    assert(!frees);
    mapped(inner_code);
    mapped(outer_code);
    return 0x20;
}

static uint32 outer_callback(CPUState* state)
{
    assert(state == &cpu_state && syn68k_native_checkpoint());
    memset(&fpu_invocation, 0x17, sizeof fpu_invocation);
    FPUInvocation expected = fpu_invocation;
    indexed_next_pc = 0x12345678;
    assert(!syn68k_native_unwind((void*)1));
    if (packet_mode) {
        FPUInvocation packet;
        memset(&packet, 0x31, sizeof packet);
        assert(execute_managed_packet(inner_code, &packet) == 0x27);
        const unsigned char* bytes = (const void*)&packet;
        for (unsigned i = 0; i < sizeof packet; ++i)
            assert(bytes[i] == 0xa5);
    }
    else
        assert(syn68k_x64_execute(inner_code) == 0x27);
    assert(frees == 1 && freed[0] == (unsigned char*)inner_code - sizeof(Syn68kX64CodeHeader));
    assert(!memcmp(&fpu_invocation, &expected, sizeof expected) && indexed_next_pc == 0x12345678);
    mapped(outer_code);
    return 0x1000;
}

static uint32 recursive_callback(CPUState* state)
{
    assert(state == &cpu_state);
    unsigned level = depth++;
    memset(&fpu_invocation, level + 1, sizeof fpu_invocation);
    FPUInvocation expected = fpu_invocation;
    indexed_next_pc = level + 0x12340000;
    if (level < 3)
        assert(syn68k_x64_execute(outer_code) == 0x1007 + 7 * (2 - level));
    else
        syn68k_x64_release(outer_code);
    assert(!frees && !memcmp(&fpu_invocation, &expected, sizeof expected));
    assert(indexed_next_pc == level + 0x12340000);
    mapped(outer_code);
    --depth;
    return 0x1000 + 7 * (3 - level);
}

static uint32 unwind_inner(CPUState* state)
{
    assert(state == &cpu_state);
    memset(&fpu_invocation, 0xef, sizeof fpu_invocation);
    indexed_next_pc = 0xdeadbeef;
    syn68k_x64_release(inner_code);
    syn68k_x64_release(outer_code);
    assert(!frees && syn68k_native_unwind(checkpoint));
    assert(frees == 1 && syn68k_native_checkpoint() == checkpoint);
    mapped(outer_code);
    longjmp(escape, 1);
}

static uint32 unwind_outer(CPUState* state)
{
    assert(state == &cpu_state);
    checkpoint = syn68k_native_checkpoint();
    memset(&fpu_invocation, 0x71, sizeof fpu_invocation);
    indexed_next_pc = 0x76543210;
    if (!SYN68K_NATIVE_SETJMP(escape)) {
        if (packet_mode) {
            FPUInvocation packet;
            memset(&packet, 0x31, sizeof packet);
            execute_managed_packet(inner_code, &packet);
        }
        else
            syn68k_x64_execute(inner_code);
        abort();
    }
    assert(syn68k_native_checkpoint() == checkpoint && frees == 1 && indexed_next_pc == 0x76543210);
    const unsigned char* bytes = (const void*)&fpu_invocation;
    for (unsigned i = 0; i < sizeof fpu_invocation; ++i)
        assert(bytes[i] == 0x71);
    return 0x1000;
}

static uint32 unwind_all(CPUState* state)
{
    assert(state == &cpu_state);
    syn68k_x64_release(outer_code);
    assert(!frees && syn68k_native_unwind(NULL) && frees == 1 && !syn68k_native_checkpoint());
    longjmp(escape, 1);
}

int main(void)
{
    assert(!_putenv_s("SYN68K_AOT_CACHE", ""));
    assert(!syn68k_native_checkpoint() && syn68k_native_unwind(NULL) && !syn68k_native_unwind((void*)1));
    outer_code = callback_body(outer_callback, 1);
    inner_code = callback_body(inner_callback, 0); // Nested old-style bodies also preserve the active packet.
    assert(syn68k_x64_execute(outer_code) == 0x1007 && frees == 2 && !syn68k_native_checkpoint());
    assert(freed[1] == (unsigned char*)outer_code - sizeof(Syn68kX64CodeHeader));
    packet_mode = 1;
    frees = 0;
    outer_code = callback_body(outer_callback, 1);
    inner_code = callback_body(inner_callback, 1);
    assert(syn68k_x64_execute(outer_code) == 0x1007 && frees == 2 && !syn68k_native_checkpoint());
    packet_mode = 0;
    frees = 0;
    outer_code = callback_body(recursive_callback, 1);
    assert(syn68k_x64_execute(outer_code) == 0x101c && frees == 1 && !depth && !syn68k_native_checkpoint());
    frees = 0;
    outer_code = callback_body(unwind_outer, 1);
    inner_code = callback_body(unwind_inner, 1);
    assert(syn68k_x64_execute(outer_code) == 0x1007 && frees == 2 && !syn68k_native_checkpoint());
    packet_mode = 1;
    frees = 0;
    outer_code = callback_body(unwind_outer, 1);
    inner_code = callback_body(unwind_inner, 1);
    assert(syn68k_x64_execute(outer_code) == 0x1007 && frees == 2 && !syn68k_native_checkpoint());
    packet_mode = 0;
    frees = 0;
    outer_code = callback_body(unwind_all, 1);
    if (!SYN68K_NATIVE_SETJMP(escape)) {
        syn68k_x64_execute(outer_code);
        abort();
    }
    assert(frees == 1 && !syn68k_native_checkpoint());
    puts(
        "Native RX callback return, nested scratch restoration, recursive retirement and checkpoint/full unwind passed");
    return 0;
}
