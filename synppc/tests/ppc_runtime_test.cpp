/*+===================================================================
  File:      ppc_runtime_test.cpp

  Summary:   Implements ppc runtime test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

#include "synppc/ppc.h"
#include <initializer_list>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <fenv.h>
#include <chrono>
static unsigned char memory[4096];
static int services, polls, data_maps, memory_maps;

static void* map(void*, uint32_t address, size_t length, int)
{
    ++memory_maps;
    if (address <= 0x200 && length > 0x200 - address)
        ++data_maps;
    return address <= sizeof memory && length <= sizeof memory - address ? memory + address : NULL;
}

static void word(uint32_t address, uint32_t value)
{
    for (int i = 3; i >= 0; --i) {
        memory[address + i] = value;
        value >>= 8;
    }
}

static uint32_t readword(uint32_t address)
{
    return ((uint32_t)memory[address] << 24) | ((uint32_t)memory[address + 1] << 16) |
           ((uint32_t)memory[address + 2] << 8) | memory[address + 3];
}

static int service(void*, uint32_t id, ROMlib_ppc_registers* r)
{
    assert(id == 1);
    ++services;
    r->gpr[3] += 7;
    return 1;
}

static void poll(void*)
{
    ++polls;
}

static void test_format()
{
    ROMlib_ppc_registers r = {};
    char error[256];
    strcpy((char*)memory + 0x100, "%d %u %08x %s %c %% %n%d");
    strcpy((char*)memory + 0x180, "Hi");
    r.gpr[1] = 0x300;
    r.gpr[3] = 0x400;
    r.gpr[4] = 0x100;
    r.gpr[5] = (uint32_t)-7;
    r.gpr[6] = 42;
    r.gpr[7] = 42;
    r.gpr[8] = 0x180;
    r.gpr[9] = '!';
    r.gpr[10] = 0x200;
    word(0x300 + 24 + 8 * 4, 99);
    assert(ROMlib_ppc_format(&r, map, NULL, 0, error, sizeof error));
    const char* expected = "-7 42 0000002a Hi ! % 99";
    assert(!strcmp((char*)memory + 0x400, expected) && r.gpr[3] == strlen(expected));
    assert(readword(0x200) == strlen(expected) - 2);
    strcpy((char*)memory + 0x100, "%08x");
    r.gpr[3] = 0x400;
    r.gpr[4] = 5;
    r.gpr[5] = 0x100;
    r.gpr[6] = 42;
    assert(ROMlib_ppc_format(&r, map, NULL, 1, error, sizeof error) && r.gpr[3] == 8);
    assert(!strcmp((char*)memory + 0x400, "0000"));
    r.gpr[3] = 0;
    r.gpr[4] = 0;
    r.gpr[5] = 0x100;
    r.gpr[6] = 42;
    assert(ROMlib_ppc_format(&r, map, NULL, 1, error, sizeof error) && r.gpr[3] == 8);
    strcpy((char*)memory + 0x100, "%.1f");
    r.gpr[3] = 0x400;
    r.gpr[4] = 0x100;
    r.gpr[5] = 0x40230000;
    r.gpr[6] = 0;
    assert(ROMlib_ppc_format(&r, map, NULL, 0, error, sizeof error));
    assert(!strcmp((char*)memory + 0x400, "9.5"));
    strcpy((char*)memory + 0x100, "%s");
    r.gpr[3] = 0x400;
    r.gpr[4] = 0x100;
    r.gpr[5] = 0xffffffff;
    assert(!ROMlib_ppc_format(&r, map, NULL, 0, error, sizeof error) && error[0]);
    strcpy((char*)memory + 0x100, "%2147483647d");
    r.gpr[3] = 0x400;
    r.gpr[4] = 0x100;
    assert(!ROMlib_ppc_format(&r, map, NULL, 0, error, sizeof error) && strstr(error, "field too large"));
    strcpy((char*)memory + 0x100, "truncated");
    r.gpr[3] = 4095;
    r.gpr[4] = 0x100;
    assert(!ROMlib_ppc_format(&r, map, NULL, 0, error, sizeof error) && error[0]);
    puts("PPC formatted I/O register/stack arguments, floating values, truncation and memory guards passed");
}

static void test_cpu(bool linear)
{
    services = polls = data_maps = 0;
    ROMlib_ppc_registers r = {};
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.service = service;
    env.poll = poll;
    env.instruction_limit = 10000;
    if (linear) {
        env.linear_memory = memory + 0x100;
        env.linear_address = 0x100;
        env.linear_size = sizeof memory - 0x100;
    }
    const uint32_t code[] = {0x38600028, 0x38800002, 0x7c632214, 0x90600200, 0x80a00200, 0x2c05002a, 0x18000000};
    for (size_t i = 0; i < sizeof code / sizeof code[0]; ++i)
        word(0x100 + 4 * i, code[i]);
    r.pc = 0x100;
    assert(ROMlib_ppc_execute(&r, &env));
    assert(r.gpr[3] == 42 && r.gpr[5] == 42 && readword(0x200) == 42);
    assert((r.cr >> 28) == 2 && env.instructions == 7);
    assert(linear ? data_maps == 0 : data_maps == 2);
    /* Floating-point loads, arithmetic and stores use big-endian guest bytes. */
    word(0x100, 0xc8200210);
    word(0x104, 0xc8400218);
    word(0x108, 0xfc61102a);
    word(0x10c, 0xd8600220);
    word(0x110, 0x18000000);
    word(0x210, 0x3ff80000);
    word(0x214, 0);
    word(0x218, 0x40020000);
    word(0x21c, 0);
    r.pc = 0x100;
    assert(ROMlib_ppc_execute(&r, &env));
    assert(r.fpr[3] == 3.75 && readword(0x220) == 0x400e0000 && readword(0x224) == 0);
    /* BL/BLR, LR restoration and a host service returning in r3. */
    word(0x100, 0x48000021);
    word(0x104, 0x18000000); /* bl 0x120 */
    word(0x120, 0x18000001);
    r.pc = 0x100;
    r.gpr[3] = 5;
    assert(ROMlib_ppc_execute(&r, &env));
    assert(services == 1 && r.gpr[3] == 12 && r.lr == 0x104);
    word(0x100, 0x48000000);
    r.pc = 0x100; /* bounded infinite branch */
    assert(!ROMlib_ppc_execute(&r, &env));
    assert(strstr(env.error, "limit") && polls > 0);
    word(0x100, 0x80601000);
    r.pc = 0x100;
    assert(!ROMlib_ppc_execute(&r, &env) && strstr(env.error, "read fault"));
    word(0x100, 0x90601000);
    r.pc = 0x100;
    assert(!ROMlib_ppc_execute(&r, &env) && strstr(env.error, "write fault"));
    word(0x100, 0);
    r.pc = 0x100;
    assert(!ROMlib_ppc_execute(&r, &env) && strstr(env.error, "illegal"));
    /* Invalid update-form RA=0 must not abort the host process. */
    word(0x100, 0x84600000);
    r.pc = 0x100;
    assert(!ROMlib_ppc_execute(&r, &env) && strstr(env.error, "update register"));
    r.pc = 0x101;
    assert(!ROMlib_ppc_execute(&r, &env) && strstr(env.error, "unaligned"));
    /* Crossing the RAM limit must still use the checked callback and fault. */
    word(0x100, 0x80600ffe);
    r.pc = 0x100;
    assert(!ROMlib_ppc_execute(&r, &env) && strstr(env.error, "read fault"));
    puts("PowerPC integer/FPU, big-endian memory, branch/service and fault tests passed");
}

static unsigned char nested_memory[1024];
static bool nested_fault;

static void* nested_map(void*, uint32_t address, size_t length, int)
{
    return address <= sizeof nested_memory && length <= sizeof nested_memory - address ? nested_memory + address : NULL;
}

static int nested_service(void*, uint32_t id, ROMlib_ppc_registers*)
{
    ROMlib_ppc_environment env = {};
    ROMlib_ppc_registers r = {};
    env.memory = nested_map;
    env.linear_memory = nested_memory + 0x100;
    env.linear_address = 0x100;
    env.linear_size = sizeof nested_memory - 0x100;
    r.pc = nested_fault ? 0x101 : 0x100;
    assert(ROMlib_ppc_execute(&r, &env) == !nested_fault);
    if (nested_fault)
        assert(strstr(env.error, "unaligned"));
    else
        assert(r.gpr[3] == 91 && env.instructions == 2);
    return 1;
}

static void test_linear_boundaries_and_nesting()
{
    ROMlib_ppc_environment env = {};
    ROMlib_ppc_registers r = {};
    env.memory = map;
    env.linear_memory = memory + 0x100;
    env.linear_address = 0x100;
    env.linear_size = 6;
    word(0x100, 0x38600005);
    word(0x104, 0x18000000);
    r.pc = 0x100;
    memory_maps = 0;
    assert(ROMlib_ppc_execute(&r, &env));
    /* The second instruction straddles the fast span and uses memory(). */
    assert(r.gpr[3] == 5 && memory_maps == 1);
    env.linear_size = 0;
    r.pc = 0x100;
    memory_maps = 0;
    assert(ROMlib_ppc_execute(&r, &env) && memory_maps == 2);
    word(0x100, 0x48000021);
    word(0x104, 0x80a00200);
    word(0x108, 0x18000000);
    word(0x120, 0x18000001);
    word(0x200, 0x11223344);
    memcpy(nested_memory + 0x100, "\x38\x60\x00\x5B\x18\x00\x00\x00", 8);
    env.service = nested_service;
    env.linear_size = sizeof memory - 0x100;
    for (int fault = 0; fault < 2; ++fault) {
        nested_fault = fault;
        r.pc = 0x100;
        assert(ROMlib_ppc_execute(&r, &env));
        assert(r.gpr[5] == 0x11223344 && env.instructions == 4);
    }
    puts("Linear RAM boundaries and nested success/failure restoration passed");
}

static int rewrite_service(void*, uint32_t id, ROMlib_ppc_registers*)
{
    assert(id == 2);
    word(0x120, 0x38600003);
    return 1;
}

static void rewrite_poll(void*)
{
    ++polls;
    word(0x100, 0x18000000);
}

static void test_code_changes(bool linear)
{
    ROMlib_ppc_environment env = {};
    ROMlib_ppc_registers r = {};
    env.memory = map;
    env.instruction_limit = 10000;
    if (linear) {
        env.linear_memory = memory;
        env.linear_size = sizeof memory;
    }
    /* Execute a subroutine, overwrite its instruction from guest code, then
     execute the same PC again. Future translators must observe the store. */
    word(0x100, 0x48000021);
    word(0x104, 0x90800120);
    word(0x108, 0x48000019);
    word(0x10c, 0x18000000);
    word(0x120, 0x38600001);
    word(0x124, 0x4e800020);
    r.pc = 0x100;
    r.gpr[4] = 0x38600002;
    assert(ROMlib_ppc_execute(&r, &env));
    assert(r.gpr[3] == 2 && env.instructions == 8);
    /* Host services can change code too, without a guest store instruction. */
    word(0x104, 0x4800003d);
    word(0x120, 0x38600001);
    word(0x140, 0x18000002);
    env.service = rewrite_service;
    r.pc = 0x100;
    assert(ROMlib_ppc_execute(&r, &env));
    assert(r.gpr[3] == 3 && env.instructions == 9);
    /* A polling callback changes a running loop into a return. */
    word(0x100, 0x38630001);
    word(0x104, 0x4bfffffc);
    env.poll = rewrite_poll;
    r.pc = 0x100;
    r.gpr[3] = 0;
    polls = 0;
    assert(ROMlib_ppc_execute(&r, &env));
    assert(r.gpr[3] == 512 && env.instructions == 1025 && polls == 1);
    puts("PPC guest, service and polling code changes passed");
}

static int host_rounding;
static int host_exceptions;

static void* floating_map(void* opaque, uint32_t address, size_t length, int write)
{
    assert(fegetround() == host_rounding);
    return map(opaque, address, length, write);
}

static void floating_poll(void*)
{
    assert(fegetround() == host_rounding);
    assert(fetestexcept(FE_ALL_EXCEPT) == host_exceptions);
    ++polls;
}

static int floating_service(void*, uint32_t id, ROMlib_ppc_registers*)
{
    assert(id == 1 && fegetround() == host_rounding);
    assert(fetestexcept(FE_ALL_EXCEPT) == host_exceptions);
    ROMlib_ppc_environment env = {};
    ROMlib_ppc_registers r = {};
    env.memory = floating_map;
    r.pc = 0x200;
    r.fpscr = 3;
    r.fpr[1] = 1.0;
    r.fpr[2] = 0x1p-53;
    word(0x200, 0xfc61102a);
    word(0x204, 0x18000000);
    assert(ROMlib_ppc_execute(&r, &env) && r.fpr[3] == 1.0);
    assert(fegetround() == host_rounding);
    ++services;
    return 1;
}

static void test_floating_environment()
{
    fenv_t original;
    assert(!fegetenv(&original));
    host_rounding = FE_DOWNWARD;
    assert(!fesetround(host_rounding));
    assert(!feclearexcept(FE_ALL_EXCEPT));
    assert(!feraiseexcept(FE_INVALID));
    host_exceptions = fetestexcept(FE_ALL_EXCEPT);
    for (unsigned mode = 0; mode < 4; ++mode) {
        ROMlib_ppc_environment env = {};
        ROMlib_ppc_registers r = {};
        env.memory = floating_map;
        r.pc = 0x100;
        r.fpscr = mode;
        r.fpr[1] = 1.0;
        r.fpr[2] = 0x1p-53;
        word(0x100, 0xfc61102a); /* fadd f3,f1,f2, halfway between doubles. */
        word(0x104, 0x18000000);
        assert(ROMlib_ppc_execute(&r, &env));
        uint64_t result;
        memcpy(&result, &r.fpr[3], sizeof result);
        assert(result == UINT64_C(0x3ff0000000000000) + (mode == 2));
        assert(fegetround() == host_rounding);
        if (fetestexcept(FE_ALL_EXCEPT) != host_exceptions)
            fprintf(stderr, "PPC host fenv mode %u: exceptions %x expected %x\n", mode, fetestexcept(FE_ALL_EXCEPT),
                    host_exceptions);
        assert(fetestexcept(FE_ALL_EXCEPT) == host_exceptions);
    }
    /* A guest instruction changing RN must not leak into host callbacks or
     survive a failed execution. */
    ROMlib_ppc_environment env = {};
    ROMlib_ppc_registers r = {};
    env.memory = floating_map;
    r.pc = 0x100;
    word(0x100, 0xff80210c); /* mtfsfi 7,2: round toward +infinity. */
    word(0x104, 0);          /* Controlled illegal-instruction failure. */
    assert(!ROMlib_ppc_execute(&r, &env) && strstr(env.error, "illegal"));
    assert(fegetround() == host_rounding);
    assert(fetestexcept(FE_ALL_EXCEPT) == host_exceptions);
    env.service = floating_service;
    r.pc = 0x100;
    r.fpr[1] = 1.0;
    r.fpr[2] = 0x1p-53;
    word(0x104, 0x4800001d); /* bl 0x120. */
    word(0x108, 0xfc61102a);
    word(0x10c, 0x18000000);
    word(0x120, 0x18000001);
    services = 0;
    assert(ROMlib_ppc_execute(&r, &env) && services == 1);
    uint64_t result;
    memcpy(&result, &r.fpr[3], sizeof result);
    assert(result == UINT64_C(0x3ff0000000000001));
    assert(fegetround() == host_rounding);
    assert(fetestexcept(FE_ALL_EXCEPT) == host_exceptions);
    env.poll = floating_poll;
    env.instruction_limit = 2048;
    r.pc = 0x100;
    polls = 0;
    word(0x104, 0x48000000);
    assert(!ROMlib_ppc_execute(&r, &env) && strstr(env.error, "limit"));
    assert(polls == 2 && fegetround() == host_rounding);
    assert(fetestexcept(FE_ALL_EXCEPT) == host_exceptions);
    assert(!fesetenv(&original));
    puts("PPC rounding modes and host floating-environment isolation passed");
}

static void test_single_subtract_integer_bias()
{
    for (unsigned jit = 0; jit <= 1; ++jit) {
        ROMlib_ppc_environment env = {};
        ROMlib_ppc_registers r = {};
        env.memory = map;
        env.execution_flags = jit ? ROMLIB_PPC_EXEC_JIT : 0;
        r.pc = 0x100;
        r.fpr[1] = 0x1.0000080000000p52 + 286;
        r.fpr[2] = 0x1.0000080000000p52;
        word(0x100, 0xec611028); /* fsubs f3,f1,f2 */
        word(0x104, 0x18000000);
        assert(ROMlib_ppc_execute(&r, &env) && r.fpr[3] == 286.0);
    }
}

static void test_fused_arithmetic()
{
    for (unsigned single = 0; single < 2; ++single)
        for (unsigned mode = 0; mode < 4; ++mode)
            for (unsigned operation = 28; operation < 32; ++operation) {
                ROMlib_ppc_environment env = {};
                ROMlib_ppc_registers r = {};
                env.memory = map;
                r.pc = 0x100;
                r.fpscr = mode;
                r.fpr[1] = single ? 0x1.001p0 : 1.0 + 0x1p-33;
                r.fpr[3] = single ? 0x1.001p0 : 1.0 - 0x1p-33;
                double addend = single ? 0x1p-80 : -1.0;
                r.fpr[2] = operation & 1 ? addend : -addend;
                double expected = single ? 1.0 + 0x1p-11 + ((mode == 0 || mode == 2) ? 0x1p-23 : 0) : -0x1p-66;
                if (operation >= 30)
                    expected = -expected;
                word(0x100,
                     ((single ? 59u : 63u) << 26) | (4 << 21) | (1 << 16) | (2 << 11) | (3 << 6) | (operation << 1));
                word(0x104, 0x18000000);
                assert(ROMlib_ppc_execute(&r, &env));
                uint64_t actual_bits, expected_bits;
                memcpy(&actual_bits, &r.fpr[4], sizeof actual_bits);
                memcpy(&expected_bits, &expected, sizeof expected_bits);
                if (actual_bits != expected_bits)
                    fprintf(stderr, "PPC fused %u/%u/%u: %016llx expected %016llx\n", single, mode, operation,
                            (unsigned long long)actual_bits, (unsigned long long)expected_bits);
                assert(actual_bits == expected_bits);
            }
    puts("PPC fused single/double rounding and post-round negation passed");
}

static void test_fused_nan_and_tiny()
{
    auto execute = [](unsigned single, unsigned operation, ROMlib_ppc_registers& r) {
        ROMlib_ppc_environment env = {};
        env.memory = map;
        r.pc = 0x100;
        word(0x100, ((single ? 59u : 63u) << 26) | (4 << 21) | (1 << 16) | (2 << 11) | (3 << 6) | (operation << 1));
        word(0x104, 0x18000000);
        assert(ROMlib_ppc_execute(&r, &env));
    };
    const uint64_t nan[] = {UINT64_C(0xfff8000020000123), UINT64_C(0x7ff0000040000456), UINT64_C(0xfff8000060000789)};
    for (unsigned single = 0; single < 2; ++single)
        for (unsigned operation = 28; operation < 32; ++operation)
            for (unsigned mask = 1; mask < 8; ++mask) {
                ROMlib_ppc_registers r = {};
                for (unsigned i = 0; i < 3; ++i)
                    if (mask & (1 << i))
                        memcpy(&r.fpr[i + 1], &nan[i], sizeof nan[i]);
                    else
                        r.fpr[i + 1] = 1.0;
                unsigned preferred = mask & 1 ? 0 : mask & 2 ? 1 : 2;
                uint64_t expected = nan[preferred] | UINT64_C(0x0008000000000000);
                execute(single, operation, r);
                uint64_t actual;
                memcpy(&actual, &r.fpr[4], sizeof actual);
                assert(actual == expected);
            }
    for (unsigned operation = 28; operation < 32; ++operation) {
        ROMlib_ppc_registers r = {};
        r.fpr[1] = 0x1p-126;
        r.fpr[3] = 0.5;
        execute(1, operation, r);
        assert(r.fpr[4] == (operation >= 30 ? -0x1p-127 : 0x1p-127));
        assert((r.fpscr & 0x1f000) == (operation >= 30 ? 0x18000 : 0x14000));
    }
    puts("PPC fused NaN preference/sign and single tiny-result classification passed");
}

static void test_sse_environment()
{
    if (!ROMlib_ppc_jit_available())
        return;
    unsigned original, restored;
    __asm__ volatile("stmxcsr %0" : "=m"(original));
    unsigned host = ((original | 0x9fe1u) & ~0x6000u) | 0x4000u;
    __asm__ volatile("ldmxcsr %0" : : "m"(host));
    word(0x100, 0xfc61102a); /* fadd f3,f1,f2: preserve the smallest double. */
    word(0x104, 0x18000000);
    auto* cache = ROMlib_ppc_translation_cache_create();
    assert(cache && ROMlib_ppc_translate_range(cache, memory + 0x100, 0x100, 4) == 1);
    for (bool native : {false, true})
        for (uint64_t input : {UINT64_C(1), UINT64_C(0x3FF0000000000000)}) {
            ROMlib_ppc_environment env = {};
            ROMlib_ppc_registers r = {};
            env.memory = map;
            env.linear_memory = memory;
            env.linear_size = sizeof memory;
            env.translation_cache = cache;
            env.execution_flags = native ? ROMLIB_PPC_EXEC_JIT : 0;
            r.pc = 0x100;
            memcpy(&r.fpr[1], &input, sizeof input);
            assert(ROMlib_ppc_execute(&r, &env));
            __asm__ volatile("stmxcsr %0" : "=m"(restored));
            uint64_t result;
            memcpy(&result, &r.fpr[3], sizeof result);
            assert(restored == host && result == input);
            if (native)
                assert(env.native_instructions == 1 && !env.native_compilations &&
                       env.native_kernel_instructions == (input == 1 ? 1u : 0u));
        }
    ROMlib_ppc_translation_cache_destroy(cache);
    __asm__ volatile("ldmxcsr %0" : : "m"(original));
    puts("PPC SSE rounding, sticky flags and host DAZ/FTZ isolation passed");
}

static void test_fpr_transfers()
{
    const uint64_t sign = UINT64_C(0x8000000000000000);
    const uint64_t values[] = {UINT64_C(0x3ff0000000000000),
                               UINT64_C(0xbff0000000000000),
                               0,
                               sign,
                               1,
                               UINT64_C(0x7ff0000000000000),
                               UINT64_C(0xfff0000000000000),
                               UINT64_C(0x7fefffffffffffff),
                               UINT64_C(0x7ff8000012345678),
                               UINT64_C(0x7ff0000012345678),
                               UINT64_C(0xfff0000080000001)};
    auto execute = [](uint32_t instruction, ROMlib_ppc_registers& r) {
        ROMlib_ppc_environment env = {};
        env.memory = map;
        r.pc = 0x100;
        word(0x100, instruction);
        word(0x104, 0x18000000);
        assert(ROMlib_ppc_execute(&r, &env));
    };
    auto bits = [](double& value) {
        uint64_t result;
        memcpy(&result, &value, sizeof result);
        return result;
    };
    /* Even a return-only invocation must preserve every signaling-NaN bit. */
    for (uint64_t value : values) {
        ROMlib_ppc_registers r = {};
        memcpy(&r.fpr[3], &value, sizeof value);
        execute(0x18000000, r);
        if (bits(r.fpr[3]) != value)
            fprintf(stderr, "PPC FPR transport: %016llx expected %016llx\n", (unsigned long long)bits(r.fpr[3]),
                    (unsigned long long)value);
        assert(bits(r.fpr[3]) == value);
    }
    const unsigned operations[] = {72, 40, 264, 136};
    for (uint64_t value : values)
        for (unsigned operation : operations)
            for (unsigned record = 0; record < 2; ++record) {
                ROMlib_ppc_registers r = {};
                memcpy(&r.fpr[3], &value, sizeof value);
                r.fpscr = 0xb1234563;
                r.cr = 0x12345678;
                uint64_t expected = operation == 40    ? value ^ sign
                                    : operation == 264 ? value & ~sign
                                    : operation == 136 ? value | sign
                                                       : value;
                execute((63u << 26) | (4 << 21) | (3 << 11) | (operation << 1) | record, r);
                assert(bits(r.fpr[3]) == value && bits(r.fpr[4]) == expected);
                assert(r.fpscr == 0xb1234563);
                assert(r.cr == (record ? 0x1b345678u : 0x12345678u));
            }
    for (uint64_t guard : values)
        for (unsigned destination = 1; destination < 5; ++destination) {
            ROMlib_ppc_registers r = {};
            uint64_t positive = UINT64_C(0xfff0000020000000);
            uint64_t negative = UINT64_C(0x7ff0000040000000);
            memcpy(&r.fpr[1], &guard, sizeof guard);
            memcpy(&r.fpr[2], &positive, sizeof positive);
            memcpy(&r.fpr[3], &negative, sizeof negative);
            uint64_t magnitude = guard & ~sign;
            bool yes = (!(guard & sign) || !magnitude) && magnitude <= UINT64_C(0x7ff0000000000000);
            r.fpscr = 0xb1234563;
            execute((63u << 26) | (destination << 21) | (1 << 16) | (3 << 11) | (2 << 6) | (23 << 1), r);
            assert(bits(r.fpr[destination]) == (yes ? positive : negative));
            assert(r.fpscr == 0xb1234563);
        }
    auto memory_transfer = [&](bool wide, bool linear, uint64_t value) {
        ROMlib_ppc_environment env = {};
        ROMlib_ppc_registers r = {};
        env.memory = map;
        if (linear) {
            env.linear_memory = memory;
            env.linear_size = sizeof memory;
        }
        r.pc = 0x100;
        r.fpscr = 0xa1234500;
        word(0x100, ((wide ? 50u : 48u) << 26) | (3 << 21) | 0x200);
        word(0x104, ((wide ? 54u : 52u) << 26) | (3 << 21) | 0x208);
        word(0x108, 0x18000000);
        word(0x200, wide ? value >> 32 : value);
        word(0x204, value);
        word(0x208, 0xabcdef09);
        word(0x20c, 0x12345678);
        assert(ROMlib_ppc_execute(&r, &env));
        uint64_t expected =
            wide ? value : ((value & 0x80000000) << 32) | UINT64_C(0x7ff0000000000000) | ((value & 0x7fffff) << 29);
        if (!wide && (value & 0x7f800000) != 0x7f800000) {
            uint32_t raw = value;
            float input;
            memcpy(&input, &raw, sizeof input);
            double converted = input; /* Independent exact finite conversion. */
            memcpy(&expected, &converted, sizeof expected);
        }
        if (bits(r.fpr[3]) != expected || r.fpscr != 0xa1234500)
            fprintf(stderr,
                    "PPC FP memory %u/%u input %016llx: %016llx expected "
                    "%016llx FPSCR %08x\n",
                    wide, linear, (unsigned long long)value, (unsigned long long)bits(r.fpr[3]),
                    (unsigned long long)expected, r.fpscr);
        assert(bits(r.fpr[3]) == expected && r.fpscr == 0xa1234500);
        assert(readword(0x208) == (wide ? value >> 32 : (uint32_t)value));
        assert(readword(0x20c) == (wide ? (uint32_t)value : 0x12345678));
    };
    for (unsigned linear = 0; linear < 2; ++linear) {
        for (uint64_t value : values)
            memory_transfer(true, linear, value);
        const uint32_t singles[] = {0,          0x80000000, 1,          0x80000001, 0x007fffff, 0x807fffff,
                                    0x00800000, 0x80800000, 0x3f800000, 0xbf800000, 0x7f7fffff, 0xff7fffff,
                                    0x7f801234, 0xff801234, 0x7fc01234, 0xffc01234, 0x7f800000, 0xff800000};
        for (uint32_t value : singles)
            memory_transfer(false, linear, value);
    }
    puts("PPC raw FPR moves, select aliases, memory transfers and FPSCR/CR passed");
}

static void benchmark_calls(bool loop)
{
    ROMlib_ppc_environment env = {};
    ROMlib_ppc_registers r = {};
    env.memory = map;
    env.linear_memory = memory;
    env.linear_size = sizeof memory;
    word(0x100, loop ? 0x38630001 : 0x3860002a);
    word(0x104, loop ? 0x4200fffc : 0x18000000);
    word(0x108, 0x18000000);
    const unsigned calls = loop ? 1 : 5000;
    auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < calls; ++i) {
        r.pc = 0x100;
        r.gpr[3] = 0;
        r.ctr = 4000000;
        assert(ROMlib_ppc_execute(&r, &env));
        assert(r.gpr[3] == (loop ? 4000000 : 42));
        assert(env.instructions == (loop ? 8000001 : 2));
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
    fprintf(stderr, "%u %s PPC calls: %lld us\n", calls, loop ? "loop" : "short", (long long)elapsed.count());
}

/* A hot block exercises all six emitted instructions, GPR0, signed
   immediates and register dependencies. Compare full architectural state
   with interpretation, including limits that stop inside cached blocks. */
static void jit_program()
{
    const uint32_t code[] = {0x3863ffff, /* addi r3,r3,-1 */
                             0x3c84ffff, /* addis r4,r4,-1 */
                             0x60658001, /* ori r5,r3,0x8001 */
                             0x64a60080, /* oris r6,r5,0x80 */
                             0x68c78000, /* xori r7,r6,0x8000 */
                             0x6ce80080, /* xoris r8,r7,0x80 */
                             0x3800fffe, /* li r0,-2 */
                             0x3d200001, /* lis r9,1 */
                             0x4200ffe0, /* bdnz 0x100 */
                             0x18000000};
    for (unsigned i = 0; i < sizeof code / sizeof *code; ++i)
        word(0x100 + i * 4, code[i]);
}

static void jit_rewrite_poll(void*)
{
    ++polls;
    word(0x100, 0x38630002); /* Replace a previously compiled instruction. */
}

static unsigned nested_jit_flags;
static unsigned char nested_jit_memory[sizeof memory];

static void* jit_nested_map(void*, uint32_t address, size_t length, int)
{
    return address <= sizeof nested_jit_memory && length <= sizeof nested_jit_memory - address
               ? nested_jit_memory + address
               : NULL;
}

static int jit_nested_service(void*, uint32_t id, ROMlib_ppc_registers* outer)
{
    assert(id == 2);
    memcpy(nested_jit_memory, memory, sizeof memory);
    /* A second CPU executes different instructions at the same guest address. */
    nested_jit_memory[0x202] = 0;
    nested_jit_memory[0x203] = 5;
    nested_jit_memory[0x224] = 0x18;
    memset(nested_jit_memory + 0x225, 0, 3);
    ROMlib_ppc_environment env = {};
    env.memory = jit_nested_map;
    env.linear_memory = nested_jit_memory;
    env.linear_size = sizeof nested_jit_memory;
    env.execution_flags = nested_jit_flags;
    ROMlib_ppc_registers r = {};
    r.pc = 0x200;
    r.ctr = 100;
    assert(ROMlib_ppc_execute(&r, &env) && r.gpr[3] == 500);
    if (nested_jit_flags && ROMlib_ppc_jit_available())
        assert(env.native_blocks > 0);
    word(0x200, 0x38630003);
    outer->ctr = 100;
    return 1;
}

static void test_jit_reentry()
{
    for (unsigned service_change = 0; service_change < 2; ++service_change) {
        ROMlib_ppc_registers reference = {};
        uint64_t count = 0;
        for (unsigned mode = 0; mode < 2; ++mode) {
            jit_program();
            memcpy(memory + 0x200, memory + 0x100, 40);
            word(0x224, 0x4e800020); /* blr */
            word(0x100, 0x48000101); /* bl 0x200 */
            if (service_change) {
                word(0x104, 0x480001fd); /* bl 0x300 */
                word(0x108, 0x480000f9); /* bl 0x200 */
                word(0x10c, 0x18000000);
                word(0x300, 0x18000002);
            }
            else {
                word(0x104, 0x914b0000); /* stw r10,0(r11) */
                word(0x108, 0x7d8903a6); /* mtctr r12 */
                word(0x10c, 0x480000f5); /* bl 0x200 */
                word(0x110, 0x18000000);
            }
            ROMlib_ppc_environment env = {};
            env.memory = map;
            env.service = jit_nested_service;
            env.linear_memory = memory;
            env.linear_size = sizeof memory;
            env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
            /* Test interpreted nesting inside JIT and JIT inside interpretation. */
            nested_jit_flags = mode ? 0 : ROMLIB_PPC_EXEC_JIT;
            ROMlib_ppc_registers r = {};
            r.pc = 0x100;
            r.ctr = 100;
            r.gpr[10] = 0x38630003;
            r.gpr[11] = 0x200;
            r.gpr[12] = 100;
            assert(ROMlib_ppc_execute(&r, &env) && r.gpr[3] == 200);
            if (!mode) {
                reference = r;
                count = env.instructions;
            }
            else {
                assert(!memcmp(&r, &reference, sizeof r));
                assert(env.instructions == count);
                if (ROMlib_ppc_jit_available())
                    assert(env.native_blocks > 0);
            }
        }
    }
    puts("PPC hot blocks survive guest writes, host writes and nested CPUs");
}

static void test_jit_registers()
{
    const unsigned operations[] = {266, 40, 28, 60, 124, 284, 316, 412, 444, 476, 24, 536, 26, 954, 922, 104};
    uint32_t seed = 0x98271ab3;
    for (unsigned sample = 0; sample < 32; ++sample)
        for (unsigned alias = 0; alias < 4; ++alias) {
            ROMlib_ppc_registers initial = {};
            for (unsigned i = 0; i < 32; ++i) {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                initial.gpr[i] = sample == 0 ? 0 : sample == 1 ? UINT32_MAX : seed;
            }
            initial.pc = 0x100;
            initial.ctr = 100;
            initial.cr = 0x824769ab;
            initial.xer = sample & 1 ? 0xe0000042 : 0x60000042;
            unsigned n = 0;
            for (unsigned operation : operations) {
                unsigned source = n * 3 & 31, other = (source + 1) & 31;
                unsigned destination = alias == 0 ? (source + 2) & 31 : alias == 1 ? source : other;
                if (alias == 3)
                    other = source;
                bool arithmetic = operation == 266 || operation == 40 || operation == 104;
                bool unary = operation == 26 || operation == 954 || operation == 922 || operation == 104;
                word(0x100 + n++ * 4, (31u << 26) | ((arithmetic ? destination : source) << 21) |
                                          ((arithmetic ? source : destination) << 16) | ((unary ? 0 : other) << 11) |
                                          (operation << 1));
            }
            /* Record forms update CR natively; overflow-enabled forms must retain
           interpreter XER updates between subsequent native blocks. */
            word(0x100 + n++ * 4, 0x7c632215); /* add. r3,r3,r4 */
            word(0x100 + n++ * 4, 0x7ca63614); /* addo r5,r6,r6 */
            word(0x100 + n * 4, 0x42000000 | ((-(int)n * 4) & 0xfffc));
            word(0x104 + n * 4, 0x18000000);
            ROMlib_ppc_registers reference = {};
            uint64_t count = 0;
            for (unsigned mode = 0; mode < 2; ++mode) {
                ROMlib_ppc_environment env = {};
                env.memory = map;
                env.linear_memory = memory;
                env.linear_size = sizeof memory;
                env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
                ROMlib_ppc_registers r = initial;
                assert(ROMlib_ppc_execute(&r, &env));
                if (!mode) {
                    reference = r;
                    count = env.instructions;
                }
                else {
                    assert(!memcmp(&r, &reference, sizeof r));
                    assert(env.instructions == count);
                    if (ROMlib_ppc_jit_available())
                        assert(env.native_blocks > 0);
                }
            }
        }
    puts("PPC native register arithmetic/logic, aliases, CR and XER fallback passed");
}

static void test_jit_rotates()
{
    const unsigned operations[] = {20, 21, 23};
    for (unsigned primary : operations)
        for (unsigned begin = 0; begin < 32; ++begin)
            for (unsigned end = 0; end < 32; ++end)
                for (unsigned alias = 0; alias < 5; ++alias) {
                    unsigned destination = alias == 4 ? 1 : alias;
                    unsigned other = alias >= 3 ? 1 : 2;
                    unsigned shift = (begin * 17 + end * 13) & 31;
                    uint32_t opcode = (primary << 26) | (1 << 21) | (destination << 16) |
                                      ((primary == 23 ? other : shift) << 11) | (begin << 6) | (end << 1);
                    for (unsigned i = 0; i < 5; ++i)
                        word(0x100 + i * 4, opcode | (i == 4));
                    word(0x114, 0x4200ffec); // bdnz 0x100.
                    word(0x118, 0x18000000);
                    ROMlib_ppc_registers initial = {};
                    for (unsigned i = 0; i < 32; ++i)
                        initial.gpr[i] = 0x80000001u ^ (0x9e3779b9u * (i + begin * 32 + end));
                    initial.pc = 0x100;
                    initial.ctr = 16;
                    initial.cr = 0x824769ab;
                    initial.xer = 0xe0000042;
                    /* Independent bit-at-a-time mask and rotation model. */
                    ROMlib_ppc_registers expected = initial;
                    for (unsigned iteration = 0; iteration < 16 * 5; ++iteration) {
                        unsigned rotation = primary == 23 ? expected.gpr[other] & 31 : shift;
                        uint32_t result = primary == 20 ? expected.gpr[destination] : 0;
                        for (unsigned bit = 0; bit < 32; ++bit)
                            if (begin <= end ? bit >= begin && bit <= end : bit >= begin || bit <= end) {
                                uint32_t value = (expected.gpr[1] >> (31 - ((bit + rotation) & 31))) & 1;
                                result = (result & ~(1u << (31 - bit))) | (value << (31 - bit));
                            }
                        expected.gpr[destination] = result;
                    }
                    ROMlib_ppc_registers reference = {};
                    uint64_t count = 0;
                    for (unsigned mode = 0; mode < 2; ++mode) {
                        ROMlib_ppc_environment env = {};
                        env.memory = map;
                        env.linear_memory = memory;
                        env.linear_size = sizeof memory;
                        env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
                        ROMlib_ppc_registers r = initial;
                        assert(ROMlib_ppc_execute(&r, &env));
                        assert(!memcmp(r.gpr, expected.gpr, sizeof r.gpr));
                        if (!mode) {
                            reference = r;
                            count = env.instructions;
                        }
                        else {
                            assert(!memcmp(&r, &reference, sizeof r));
                            assert(env.instructions == count);
                            if (ROMlib_ppc_jit_available())
                                assert(env.native_blocks && env.native_instructions);
                        }
                    }
                }
    puts("PPC native rotates, all masks, aliases and record forms passed");
}

static unsigned record_services, record_destination;
static uint32_t record_opcode, record_expected_cr;
static bool record_rewrite, record_enabled;

static int jit_record_service(void*, uint32_t, ROMlib_ppc_registers* r)
{
    ++record_services;
    uint32_t result = r->gpr[record_destination];
    unsigned condition = !result ? 2 : result & 0x80000000 ? 8 : 4;
    if (record_enabled)
        assert((r->cr >> 28) == (condition | (r->xer >> 31)));
    else
        assert(r->cr == record_expected_cr);
    r->lr = r->pc + 4;
    r->cr = (r->cr & 0xf0000000) | ((record_services * 0x13579bdu) & 0x0fffffff);
    r->xer = (record_services & 1 ? 0x80000000u : 0) | 0x60000042;
    record_expected_cr = r->cr;
    if (record_rewrite && (record_services == 12 || record_services == 24)) {
        record_enabled = !record_enabled;
        for (unsigned i = 0; i < 4; ++i)
            word(0x100 + 4 * i, record_opcode ^ (record_enabled ? 0 : 1));
    }
    return 1;
}

static void test_jit_records()
{
    const unsigned operations[] = {266, 40, 28, 60, 124, 284, 316, 412, 444, 476, 24, 536, 26, 954, 922, 104};
    const unsigned register_count = sizeof operations / sizeof *operations;
    const uint32_t values[] = {0,  1,   UINT32_MAX, 0x80000000, 0x7fffffff, 0x12345678, 0x87654321, 31,    32,   63,
                               64, 127, 128,        255,        256,        32767,      32768,      65535, 65536};
    unsigned cases = 0;
    for (unsigned operation = 0; operation < register_count + 5; ++operation)
        for (unsigned sample = 0; sample < sizeof values / sizeof *values; ++sample)
            for (unsigned alias = 0; alias < 4; ++alias)
                for (unsigned so = 0; so < 2; ++so) {
                    unsigned dst = alias == 0 ? 3 : alias == 1 ? 1 : 2;
                    unsigned other = alias == 3 ? 1 : 2;
                    record_destination = dst;
                    uint32_t opcode;
                    if (operation < register_count) {
                        unsigned secondary = operations[operation];
                        bool arithmetic = secondary == 266 || secondary == 40 || secondary == 104;
                        bool unary = secondary == 26 || secondary == 954 || secondary == 922 || secondary == 104;
                        opcode = (31u << 26) | ((arithmetic ? dst : 1) << 21) | ((arithmetic ? 1 : dst) << 16) |
                                 ((unary ? 0 : other) << 11) | (secondary << 1) | 1;
                    }
                    else if (operation < register_count + 3) {
                        unsigned primary = operation == register_count ? 20 : operation == register_count + 1 ? 21 : 23;
                        opcode = (primary << 26) | (1 << 21) | (dst << 16) | ((primary == 23 ? other : 7) << 11) |
                                 (5 << 6) | (22 << 1) | 1;
                    }
                    else
                        opcode = ((operation == register_count + 3 ? 28u : 29u) << 26) | (1 << 21) | (dst << 16) |
                                 (sample & 1 ? 0xffff : 0);
                    for (unsigned i = 0; i < 4; ++i)
                        word(0x100 + 4 * i, opcode);
                    // Service changes other CR fields and SO between compiled entries.
                    word(0x110, 0x18000001);
                    word(0x114, 0x4200ffec);
                    word(0x118, 0x18000000);
                    ROMlib_ppc_registers initial = {};
                    initial.pc = 0x100;
                    initial.ctr = 40;
                    initial.gpr[1] = values[sample];
                    initial.gpr[2] = values[(sample + 3) % (sizeof values / sizeof *values)];
                    initial.gpr[3] = 0xabcdef01;
                    initial.cr = 0x13579bdf;
                    initial.xer = (so ? 0x80000000u : 0) | 0x60000042;
                    ROMlib_ppc_registers reference = {};
                    uint64_t count = 0;
                    for (unsigned mode = 0; mode < 2; ++mode) {
                        ROMlib_ppc_environment env = {};
                        env.memory = map;
                        env.linear_memory = memory;
                        env.linear_size = sizeof memory;
                        env.service = jit_record_service;
                        env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
                        ROMlib_ppc_registers r = initial;
                        record_services = 0;
                        record_opcode = opcode;
                        record_rewrite = operation < register_count + 3;
                        record_enabled = true;
                        record_expected_cr = r.cr;
                        for (unsigned i = 0; i < 4; ++i)
                            word(0x100 + 4 * i, opcode);
                        assert(ROMlib_ppc_execute(&r, &env));
                        assert(record_services == 40);
                        uint32_t result = r.gpr[dst];
                        // The preceding service left SO=1 for the final record.
                        uint32_t condition = (!result ? 2 : result & 0x80000000 ? 8 : 4) | 1;
                        assert((r.cr >> 28) == condition);
                        assert((r.cr & 0x0fffffff) == ((40u * 0x13579bdu) & 0x0fffffff));
                        if (!mode) {
                            reference = r;
                            count = env.instructions;
                        }
                        else {
                            assert(!memcmp(&r, &reference, sizeof r));
                            assert(env.instructions == count);
                            if (ROMlib_ppc_jit_available())
                                assert(env.native_blocks && env.native_instructions);
                        }
                    }
                    ++cases;
                }
    printf("%u native record/ANDI cases, aliases, live SO and CR service updates passed\n", cases);
}

static uint32_t unary_invalid_opcode;
static ROMlib_ppc_registers unary_before_fault;

static int jit_unary_invalid_service(void* opaque, uint32_t id, ROMlib_ppc_registers* r)
{
    jit_record_service(opaque, id, r);
    if (record_services == 12) {
        unary_before_fault = *r;
        word(0x100, unary_invalid_opcode);
    }
    return 1;
}

static void test_jit_unary_invalid()
{
    const unsigned operations[] = {26, 954, 922, 104, 616};
    unsigned cases = 0;
    for (unsigned op : operations)
        for (unsigned rb = 1; rb < 32; ++rb)
            for (unsigned rc = 0; rc < 2; ++rc)
                for (unsigned mode = 0; mode < 2; ++mode) {
                    bool negate = op == 104 || op == 616;
                    uint32_t opcode =
                        (31u << 26) | ((negate ? 3 : 1) << 21) | ((negate ? 1 : 3) << 16) | (op << 1) | rc;
                    for (unsigned i = 0; i < 4; ++i)
                        word(0x100 + 4 * i, opcode);
                    word(0x110, 0x18000001);
                    word(0x114, 0x4200ffec);
                    word(0x118, 0x18000000);
                    ROMlib_ppc_registers r = {};
                    r.pc = 0x100;
                    r.ctr = 40;
                    r.gpr[1] = 0x8765fedc;
                    r.gpr[3] = 0x12345678;
                    r.cr = 0x13579bdf;
                    r.xer = 0xe0000042;
                    record_destination = 3;
                    record_services = 0;
                    record_enabled = rc;
                    record_expected_cr = r.cr;
                    record_rewrite = false;
                    unary_invalid_opcode = opcode | (rb << 11);
                    ROMlib_ppc_environment env = {};
                    env.memory = map;
                    env.linear_memory = memory;
                    env.linear_size = sizeof memory;
                    env.service = jit_unary_invalid_service;
                    env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
                    assert(!ROMlib_ppc_execute(&r, &env));
                    assert(strstr(env.error, "illegal PowerPC instruction"));
                    assert(record_services == 12 && r.pc == 0x100);
                    assert(!memcmp(r.gpr, unary_before_fault.gpr, sizeof r.gpr));
                    assert(r.cr == unary_before_fault.cr && r.xer == unary_before_fault.xer);
                    if (mode && ROMlib_ppc_jit_available())
                        assert(env.native_blocks && env.native_instructions);
                    ++cases;
                }
    printf("%u hot unary reserved-field rewrites fault before state changes\n", cases);
}

static void test_jit(bool benchmark)
{
    for (unsigned scenario = 0; scenario < (benchmark ? 1u : 7u); ++scenario) {
        ROMlib_ppc_registers reference = {};
        uint64_t count = 0;
        int expected_polls = 0, expected_result = 0;
        for (unsigned mode = 0; mode < 2; ++mode) {
            jit_program();
            ROMlib_ppc_environment env = {};
            env.memory = map;
            env.linear_memory = memory;
            env.linear_size = sizeof memory;
            if (scenario == 5)
                env.linear_memory = NULL;
            if (scenario == 6)
                env.linear_size = 0x114; /* End the fast span inside the block. */
            env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
            env.poll = scenario == 1 ? jit_rewrite_poll : poll;
            env.instruction_limit = scenario >= 2 && scenario < 5 ? 1800 + scenario : 0;
            ROMlib_ppc_registers r = {};
            for (unsigned i = 0; i < 32; ++i)
                r.gpr[i] = 0xf000ffff + i;
            r.pc = 0x100;
            r.ctr = benchmark ? 1000000 : 1000;
            r.cr = 0x12345678;
            r.xer = 0xe0000042;
            polls = 0;
            auto start = std::chrono::steady_clock::now();
            int result = ROMlib_ppc_execute(&r, &env);
            if (!mode) {
                reference = r;
                count = env.instructions;
                expected_polls = polls;
                expected_result = result;
            }
            else {
                assert(result == expected_result);
                assert(!memcmp(&r, &reference, sizeof r));
                assert(env.instructions == count && polls == expected_polls);
                if (ROMlib_ppc_jit_available() && scenario != 5)
                    assert(env.native_blocks > 0 && env.native_instructions > 0);
                if (scenario == 5)
                    assert(!env.native_blocks && !env.native_instructions);
            }
            if (benchmark)
                fprintf(stderr, "%s integer blocks: %lld us, %llu native instructions\n", mode ? "JIT" : "Interpreter",
                        (long long)std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count(),
                        (unsigned long long)env.native_instructions);
        }
    }
    puts("PPC native blocks match interpretation, polling, code changes and limits");
}

static unsigned compare_services;
static uint32_t compare_cr, compare_xer, compare_gprs[32];
static uint32_t compare_invalid_mask;

static int jit_compare_service(void*, uint32_t, ROMlib_ppc_registers* r)
{
    uint32_t expected = compare_cr;
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t op = readword(0x100 + 4 * i);
        unsigned primary = op >> 26, field = (op >> 23) & 7;
        bool logical = primary == 10 || (primary == 31 && (op & 64));
        uint32_t a = compare_gprs[(op >> 16) & 31];
        uint32_t b = primary == 31 ? compare_gprs[(op >> 11) & 31]
                     : logical     ? op & 65535
                                   : (uint32_t)(int32_t)(int16_t)op;
        int64_t first = logical ? (int64_t)a : (int32_t)a;
        int64_t second = logical ? (int64_t)b : (int32_t)b;
        unsigned condition = (first < second ? 8 : first > second ? 4 : 2) | (compare_xer >> 31);
        unsigned shift = 28 - field * 4;
        expected = (expected & ~(15u << shift)) | (condition << shift);
    }
    assert(r->cr == expected && r->xer == compare_xer);
    assert(!memcmp(r->gpr, compare_gprs, sizeof compare_gprs));
    ++compare_services;
    r->lr = r->pc + 4;
    compare_cr = r->cr = 0x13579bdu * compare_services;
    compare_xer = r->xer = (compare_services & 1 ? 0x80000000u : 0) | 0x60000042;
    // Read live inputs again at each compiled entry, including r0.
    for (unsigned i = 0; i < 32; ++i)
        compare_gprs[i] = r->gpr[i] = (r->gpr[i] << 1) | (r->gpr[i] >> 31);
    if (compare_services == 12 || compare_services == 24)
        for (unsigned i = 0; i < 4; ++i) {
            // Change destination fields and signedness after native publication.
            unsigned address = 0x100 + 4 * i;
            memory[address + 1] ^= 0x80;
            uint32_t op = readword(address);
            word(address, op ^ (op >> 26 == 31 ? 64u : 1u << 26));
        }
    if (compare_services == 12 && compare_invalid_mask)
        word(0x100, readword(0x100) | compare_invalid_mask);
    return 1;
}

static void test_jit_compares()
{
    compare_invalid_mask = 0;
    const uint32_t values[] = {0,          1,      0xffffffff, 0x80000000, 0x7fffffff,
                               0xffff8000, 0x7fff, 0x8000,     0xffff,     0x12345678};
    unsigned cases = 0;
    for (unsigned operation = 0; operation < 4; ++operation)
        for (unsigned field = 0; field < 8; ++field)
            for (unsigned sample = 0; sample < sizeof values / sizeof *values; ++sample)
                for (unsigned alias = 0; alias < 3; ++alias)
                    for (unsigned so = 0; so < 2; ++so) {
                        ROMlib_ppc_registers initial = {}, reference = {};
                        for (unsigned i = 0; i < 32; ++i)
                            initial.gpr[i] = values[(sample + i) % 10];
                        initial.pc = 0x100;
                        initial.ctr = 40;
                        initial.cr = 0x13579bdf;
                        initial.xer = (so ? 0x80000000u : 0) | 0x60000042;
                        uint64_t count = 0;
                        for (unsigned mode = 0; mode < 2; ++mode) {
                            for (unsigned i = 0; i < 4; ++i) {
                                unsigned ra = alias == 0 ? 0 : 1;
                                unsigned rb = alias == 2 ? ra : 2;
                                uint32_t op = ((operation < 2    ? 31u
                                                : operation == 2 ? 11u
                                                                 : 10u)
                                               << 26) |
                                              (((field + i) & 7) << 23) | (ra << 16);
                                op |= operation < 2 ? (rb << 11) | (operation << 6) : values[sample] & 65535;
                                word(0x100 + 4 * i, op);
                            }
                            word(0x110, 0x18000001);
                            word(0x114, 0x4200ffec);
                            word(0x118, 0x18000000);
                            ROMlib_ppc_registers r = initial;
                            compare_cr = r.cr;
                            compare_xer = r.xer;
                            memcpy(compare_gprs, r.gpr, sizeof compare_gprs);
                            compare_services = 0;
                            ROMlib_ppc_environment env = {};
                            env.memory = map;
                            env.linear_memory = memory;
                            env.linear_size = sizeof memory;
                            env.service = jit_compare_service;
                            env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
                            assert(ROMlib_ppc_execute(&r, &env));
                            assert(compare_services == 40);
                            if (!mode) {
                                reference = r;
                                count = env.instructions;
                            }
                            else {
                                assert(!memcmp(&r, &reference, sizeof r));
                                assert(env.instructions == count);
                                if (ROMlib_ppc_jit_available())
                                    assert(env.native_blocks && env.native_instructions);
                            }
                        }
                        ++cases;
                    }
    printf("%u native signed/unsigned compares, CR fields, live inputs and code changes passed\n", cases);
    // Rewriting an already compiled compare to an invalid form must fault before
    // modifying its destination field. Both reserved bits and unsupported L=1
    // forms are checked; register comparisons also reserve Rc.
    const uint32_t invalid_masks[] = {0x00200000u, 0x00400000u, 1u};
    for (unsigned operation = 0; operation < 4; ++operation)
        for (uint32_t mask : invalid_masks) {
            if (operation >= 2 && mask == 1)
                continue;
            for (unsigned mode = 0; mode < 2; ++mode) {
                uint32_t op = ((operation < 2 ? 31u : operation == 2 ? 11u : 10u) << 26) | (3 << 23) | (1 << 16);
                op |= operation < 2 ? (2 << 11) | (operation << 6) : 0x8000;
                for (unsigned i = 0; i < 4; ++i)
                    word(0x100 + 4 * i, op);
                word(0x110, 0x18000001);
                word(0x114, 0x4200ffec);
                word(0x118, 0x18000000);
                ROMlib_ppc_registers r = {};
                r.pc = 0x100;
                r.ctr = 40;
                r.cr = compare_cr = 0x13579bdf;
                r.xer = compare_xer = 0xe0000042;
                for (unsigned i = 0; i < 32; ++i)
                    r.gpr[i] = compare_gprs[i] = 0x7fffffff + i;
                compare_services = 0;
                compare_invalid_mask = mask;
                ROMlib_ppc_environment env = {};
                env.memory = map;
                env.linear_memory = memory;
                env.linear_size = sizeof memory;
                env.service = jit_compare_service;
                env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
                assert(!ROMlib_ppc_execute(&r, &env));
                assert(strstr(env.error, "illegal PowerPC instruction"));
                assert(compare_services == 12 && r.pc == 0x100);
                assert(r.cr == compare_cr && r.xer == compare_xer);
                assert(!memcmp(r.gpr, compare_gprs, sizeof compare_gprs));
                if (mode && ROMlib_ppc_jit_available())
                    assert(env.native_blocks && env.native_instructions);
            }
        }
    compare_invalid_mask = 0;
    puts("Invalid and 64-bit compare rewrites fault before CR changes");
}

int main(int argc, char** argv)
{
    if (argc == 2 && !strcmp(argv[1], "--benchmark-jit")) {
        test_jit(true);
        return 0;
    }
    if (argc == 2 && (!strcmp(argv[1], "--benchmark") || !strcmp(argv[1], "--benchmark-loop"))) {
        benchmark_calls(!strcmp(argv[1], "--benchmark-loop"));
        return 0;
    }
    test_format();
    test_cpu(false);
    test_cpu(true);
    test_linear_boundaries_and_nesting();
    test_code_changes(false);
    test_code_changes(true);
    test_floating_environment();
    test_single_subtract_integer_bias();
    test_fused_arithmetic();
    test_fused_nan_and_tiny();
    test_sse_environment();
    test_fpr_transfers();
    test_jit(false);
    test_jit_reentry();
    test_jit_registers();
    test_jit_rotates();
    test_jit_records();
    test_jit_unary_invalid();
    test_jit_compares();
    ROMlib_ppc_registers r = {};
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.linear_memory = memory;
    env.linear_address = 0xfffffff0;
    env.linear_size = 32;
    assert(!ROMlib_ppc_execute(&r, &env) && strstr(env.error, "RAM extent"));
}
