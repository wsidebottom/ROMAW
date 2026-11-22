/*+===================================================================
  File:      ppc_kernel_jit_test.cpp

  Summary:   Compare eager native semantic tails with isolated interpreter state and memory.

  Classes:   fixture, result; ROMlib_ppc_environment and ROMlib_ppc_registers.

  Functions: ppc_check_native_kernels; main for the standalone sanitized check.

  Origin:    ARDI Executor PPC native translation regression fixture.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "synppc/ppc.h"
#include "synppc/bus750.h"
#include <memory>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cfenv>

struct fixture {
    unsigned char memory[4096] = {};
    ROMlib_ppc_environment env = {};
    unsigned accesses = 0, polls = 0;
    bool reject = false;
    unsigned reduced_limit = 0;
};

static void word(fixture& f, unsigned address, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        f.memory[address + i] = value >> (24 - 8 * i);
}

static void* map(void* opaque, uint32_t address, size_t size, int)
{
    auto& f = *static_cast<fixture*>(opaque);
    ++f.accesses;
    if (f.reject && address >= 0x400)
        return nullptr;
    return address <= sizeof f.memory && size <= sizeof f.memory - address ? f.memory + address : nullptr;
}

struct result {
    ROMlib_ppc_registers registers = {};
    std::vector<unsigned char> memory;
    uint64_t instructions;
    unsigned accesses;
    bool success;
    char error[256];
};

static result run(uint32_t opcode, unsigned prefix, unsigned limit, bool reject, bool linear, bool native,
                  bool semantic)
{
    if ((opcode >> 26) == 59 && ((opcode >> 1) & 31) == 24)
        semantic = false;
    if ((opcode >> 26) == 63) {
        unsigned operation = (opcode >> 1) & 31;
        if (operation == 12 || operation == 18 || operation == 20 || operation == 21 || operation == 22 ||
            operation == 25 || operation == 26 || operation >= 28)
            semantic = false;
    }
    fixture f;
    for (unsigned i = 0; i < sizeof f.memory; ++i)
        f.memory[i] = i * 37;
    for (unsigned i = 0; i < prefix; ++i)
        word(f, 0x100 + i * 4, i == 0 ? 0x394A0001 : i == 1 ? 0x356B0001 : 0x658C0001);
    word(f, 0x100 + prefix * 4, opcode);
    word(f, 0x104 + prefix * 4, 0x18000000);
    f.reject = reject;
    f.env.opaque = &f;
    f.env.memory = map;
    f.env.linear_memory = f.memory;
    f.env.linear_size = linear && !reject ? sizeof f.memory : 0x200;
    f.env.execution_flags = native ? ROMLIB_PPC_EXEC_JIT : 0;
    f.env.instruction_limit = limit;
    auto cache = ROMlib_ppc_translation_cache_create();
    assert(cache);
    f.env.translation_cache = cache;
    if (native) {
        unsigned prepared = ROMlib_ppc_translate_range(cache, f.memory + 0x100, 0x100, (prefix + 1) * 4);
        if (prepared != 1)
            fprintf(stderr, "unprepared kernel %08x prefix %u\n", opcode, prefix);
        assert(prepared == 1);
    }
    result output;
    auto& r = output.registers;
    for (unsigned i = 0; i < 32; ++i) {
        r.gpr[i] = 0x400;
        r.fpr[i] = i + 0.5;
    }
    r.gpr[0] = 0;
    r.gpr[4] = 0;
    r.gpr[10] = 0xFFFFFFFF;
    r.xer = 0xA0000008;
    r.cr = 0x12345678;
    r.pc = 0x100;
    r.lr = 0x104 + prefix * 4;
    r.ctr = 1;
    int rounding = fegetround();
    output.success = ROMlib_ppc_execute(&r, &f.env);
    assert(fegetround() == rounding);
    if (native) {
        assert(!f.env.native_compilations);
        if (!limit || limit >= prefix + 1) {
            assert(f.env.native_cache_hits && f.env.native_blocks);
            assert(f.env.native_kernel_instructions == unsigned(semantic));
            assert(f.env.native_instructions == prefix + 1);
        }
        else {
            assert(f.env.native_blocks == 1 && f.env.native_cache_hits == 1);
            assert(f.env.native_instructions == limit && !f.env.native_kernel_instructions);
        }
    }
    output.instructions = f.env.instructions;
    output.accesses = f.accesses;
    memcpy(output.error, f.env.error, sizeof output.error);
    output.memory.assign(f.memory, f.memory + sizeof f.memory);
    ROMlib_ppc_translation_cache_destroy(cache);
    return output;
}

static void test_poll_boundary(bool tighten_limit)
{
    for (bool native : {false, true}) {
        fixture f;
        word(f, 0x100, 0x42000000); // BDNZ 0x100.
        word(f, 0x104, 0x7CA803A6); // MTLR r5.
        word(f, 0x108, 0x38630001); // The poll replaces this ADDI.
        word(f, 0x10C, 0x48000009); // BL 0x114 must leave LR untouched here.
        word(f, 0x114, 0x18000000);
        f.env.opaque = &f;
        f.env.memory = map;
        f.env.linear_memory = f.memory;
        f.env.linear_size = sizeof f.memory;
        f.env.execution_flags = native ? ROMLIB_PPC_EXEC_JIT : 0;
        f.reduced_limit = tighten_limit ? 1022 : 0;
        f.env.poll = [](void* opaque) {
            auto& f = *static_cast<fixture*>(opaque);
            assert(++f.polls == 1 && f.env.instructions == 1024);
            if (f.reduced_limit)
                f.env.instruction_limit = f.reduced_limit;
            else
                word(f, 0x108, 0x18000000);
        };
        auto* cache = ROMlib_ppc_translation_cache_create();
        assert(cache && ROMlib_ppc_translate_range(cache, f.memory + 0x100, 0x100, 16) == 2);
        f.env.translation_cache = cache;
        ROMlib_ppc_registers r = {};
        r.pc = 0x100;
        r.lr = 0x120;
        r.ctr = tighten_limit ? 1023 : 1022;
        r.gpr[3] = 47;
        r.gpr[5] = 0x124;
        assert(ROMlib_ppc_execute(&r, &f.env) == !tighten_limit);
        assert(f.polls == 1 && f.env.instructions == 1024 && !r.ctr);
        assert(r.pc == (tighten_limit ? 0x108u : 0x10Cu) && r.lr == 0x124 && r.gpr[3] == 47);
        unsigned native_instructions = tighten_limit ? 1024 : 1023;
        assert(f.env.native_instructions == (native ? native_instructions : 0) && !f.env.native_compilations);
        ROMlib_ppc_translation_cache_destroy(cache);
    }
}

static void test_block_limits()
{
    fixture f;
    for (unsigned i = 0; i < 32; ++i)
        word(f, 0x100 + i * 4, 0x38630001);
    word(f, 0x180, 0x18000000);
    f.env.opaque = &f;
    f.env.memory = map;
    f.env.linear_memory = f.memory;
    f.env.linear_size = sizeof f.memory;
    f.env.execution_flags = ROMLIB_PPC_EXEC_JIT;
    auto* cache = ROMlib_ppc_translation_cache_create();
    assert(cache && ROMlib_ppc_translate_range(cache, f.memory + 0x100, 0x100, 128) == 1);
    f.env.translation_cache = cache;
    for (unsigned limit = 1; limit <= 32; ++limit) {
        ROMlib_ppc_registers r = {};
        r.pc = 0x100;
        f.env.instruction_limit = limit;
        assert(!ROMlib_ppc_execute(&r, &f.env));
        assert(r.pc == 0x100 + limit * 4 && r.gpr[3] == limit);
        assert(f.env.instructions == limit && f.env.native_instructions == limit && !f.env.native_compilations);
    }
    ROMlib_ppc_translation_cache_destroy(cache);
    puts("Every limit of a 32-word block retires exactly the native prefix");
}

static void test_guarded_load_block()
{
    for (uint32_t address : {0x500u, 0x501u, 0x900u, 0x1000u}) {
        fixture f;
        word(f, 0x100, 0x356B0001); // ADDIC. r11,r11,1.
        word(f, 0x104, 0x84830004); // LWZU r4,4(r3).
        word(f, 0x108, 0x80A40000); // LWZ r5,0(r4).
        word(f, 0x10C, 0x38C50001); // ADDI r6,r5,1.
        word(f, 0x110, 0xFC22182A); // FADD f1,f2,f3 through a semantic tail.
        word(f, 0x114, 0x18000000);
        word(f, 0x404, address);
        if (address != 0x1000)
            word(f, address, 0x12345678);
        f.env.opaque = &f;
        f.env.memory = map;
        f.env.linear_memory = f.memory;
        f.env.linear_size = 0x800;
        auto* cache = ROMlib_ppc_translation_cache_create();
        assert(cache && ROMlib_ppc_translate_range(cache, f.memory + 0x100, 0x100, 20) == 1);
        f.env.translation_cache = cache;
        for (unsigned limit : {0u, 2u, 3u}) {
            ROMlib_ppc_registers initial = {};
            initial.pc = 0x100;
            initial.gpr[3] = 0x400;
            initial.gpr[11] = UINT32_MAX;
            initial.xer = 0x80000000;
            initial.fpr[2] = 2.0;
            initial.fpr[3] = 3.0;
            auto reference = initial, actual = initial;
            f.env.instruction_limit = limit;
            f.env.execution_flags = 0;
            int success = ROMlib_ppc_execute(&reference, &f.env);
            uint64_t instructions = f.env.instructions;
            char error[sizeof f.env.error];
            memcpy(error, f.env.error, sizeof error);
            f.env.execution_flags = ROMLIB_PPC_EXEC_JIT;
            assert(ROMlib_ppc_execute(&actual, &f.env) == success);
            assert(!memcmp(&reference, &actual, sizeof actual) && f.env.instructions == instructions &&
                   !strcmp(error, f.env.error));
            unsigned retired = address == 0x500 ? (limit ? limit : 5) : 2;
            assert(f.env.native_blocks == 1 && f.env.native_instructions == retired && !f.env.native_compilations);
            assert(!f.env.native_kernel_instructions);
        }
        ROMlib_ppc_translation_cache_destroy(cache);
    }
    puts("Native load blocks retire the exact prefix before limits, callbacks and faults");
}

static void test_memory_tails()
{
    unsigned cases = 0;
    // Keep scalar, floating, reversed, update and raw transfers in this runner.
    // Both ABIs must agree at each attempted access, including failed updates.
    const uint32_t opcodes[] = {
        0x80640000, 0x84640000, 0x88640000, 0xA0640000, 0xA8640000, 0x90640000, 0x94640000, 0x98640000,
        0xB0640000, 0x7C64282E, 0x7C64292E, 0x7C642C2C, 0x7C642D2C, 0xC0240000, 0xC8240000, 0xD0240000,
        0xD8240000, 0xC4240000, 0xCC240000, 0xD4240000, 0xDC240000, 0x7C242FAE,
        0x80640001, // Valid unaligned displacement.
        0x7C64282F, // Reserved indexed Rc, rejected before data effects.
        0x84630000, // Invalid LWZU RA=RT, preserving both registers.
    };
    for (uint32_t opcode : opcodes)
        for (uint32_t base : {0u, 0x10000000u})
            for (uint32_t address : {0x400u, 0x401u, 0x7FCu, 0x7FFu, 0x800u, 0xFFCu, 0x1000u, 0xFFFFFFFCu})
                for (unsigned limit : {0u, 1u, 2u, 3u})
                    for (bool linear : {false, true}) {
                        result reference;
                        for (bool native : {false, true}) {
                            fixture f;
                            for (unsigned i = 0; i < sizeof f.memory; ++i)
                                f.memory[i] = i * 37;
                            word(f, 0x100, 0x356B0001); // ADDIC. commits live CR/XER before the access.
                            word(f, 0x104, opcode);
                            word(f, 0x108, 0x38C60001); // A successful access continues at this word.
                            word(f, 0x10C, 0x18000000);
                            f.env.opaque = &f;
                            f.env.memory = [](void* opaque, uint32_t at, size_t size, int write) -> void* {
                                auto& fixture = *static_cast<struct fixture*>(opaque);
                                return map(opaque, at - fixture.env.linear_address, size, write);
                            };
                            f.env.linear_address = base;
                            f.env.linear_memory = f.memory;
                            f.env.linear_size = linear ? 0x800 : 0x200;
                            f.env.instruction_limit = limit;
                            f.env.execution_flags = native ? ROMLIB_PPC_EXEC_JIT : 0;
                            auto* cache = ROMlib_ppc_translation_cache_create();
                            assert(cache);
                            f.env.translation_cache = cache;
                            if (native)
                                assert(ROMlib_ppc_translate_range(cache, f.memory + 0x100, base + 0x100, 16));
                            result actual;
                            auto& r = actual.registers;
                            r.pc = base + 0x100;
                            r.gpr[3] = r.gpr[4] = base + address;
                            r.gpr[5] = 0;
                            r.gpr[6] = 42;
                            r.gpr[11] = UINT32_MAX;
                            r.fpr[1] = -1.5;
                            r.cr = 0x12345678;
                            r.xer = 0x80000000;
                            actual.success = ROMlib_ppc_execute(&r, &f.env);
                            actual.instructions = f.env.instructions;
                            actual.accesses = f.accesses;
                            actual.memory.assign(f.memory, f.memory + sizeof f.memory);
                            memcpy(actual.error, f.env.error, sizeof actual.error);
                            if (native) {
                                assert(actual.success == reference.success &&
                                       actual.instructions == reference.instructions);
                                assert(!memcmp(&r, &reference.registers, sizeof r));
                                assert(actual.memory == reference.memory && actual.accesses == reference.accesses);
                                assert(!strcmp(actual.error, reference.error));
                                assert(!f.env.native_compilations && f.env.native_instructions >= 1);
                                assert(f.env.native_kernel_instructions <= unsigned(limit != 1));
                            }
                            else
                                reference = actual;
                            ROMlib_ppc_translation_cache_destroy(cache);
                        }
                        ++cases;
                    }
    printf("%u native memory state/fault/update/limit cases passed in the shared runner\n", cases);
}

static void test_direct_floating_arithmetic()
{
    fenv_t original;
    assert(!fegetenv(&original) && !fesetround(FE_DOWNWARD));
    const int exceptions = fetestexcept(FE_ALL_EXCEPT);

    const struct {
        uint64_t a, b;
        bool exceptional, single_mismatch;
    } values[] = {
        {UINT64_C(0x3FF0000000000000), UINT64_C(0x4008000000000000), false, false},
        {UINT64_C(0xBFF0000000000000), UINT64_C(0x4008000000000000), false, false},
        {UINT64_C(0x3FF8000000000000), UINT64_C(0x4002000000000000), false, false},
        {UINT64_C(0x8000000000000000), 0, false, false},
        {UINT64_C(0x4000000000000000), UINT64_C(0xC010000000000000), false, false},
        {UINT64_C(0x3FF0000000000000), UINT64_C(0x3FD5555555555555), false, true},
        {UINT64_C(0x4330000080000001), UINT64_C(0x4330000080000000), false, true},
        {UINT64_C(0x7FF0000000000000), UINT64_C(0x7FF0000000000000), true, false},
        {UINT64_C(0x7FF0000000000001), UINT64_C(0x7FF0000000000001), true, false},
        {1, 1, true, false},
    };

    auto* cache = ROMlib_ppc_translation_cache_create();
    assert(cache);
    unsigned cases = 0;
    for (unsigned primary : {59u, 63u})
        for (unsigned operation : {12u, 18u, 20u, 21u, 22u, 24u, 25u, 26u, 28u, 29u, 30u, 31u}) {
            if ((primary == 59 && (operation == 12 || operation == 26)) || (primary == 63 && operation == 24))
                continue;
            bool unary = operation == 12 || operation == 22 || operation == 24 || operation == 26;
            for (unsigned rc : {0u, 1u})
                for (unsigned destination : {1u, 2u, 3u})
                    for (unsigned prefix : {0u, 2u})
                        for (bool architectural : {false, true}) {
                            // FSQRT/S is optional and excluded by this 750 profile.
                            if (architectural && operation == 22)
                                continue;
                            fixture f;
                            for (unsigned i = 0; i < prefix; ++i)
                                word(f, 0x100 + i * 4, i ? 0x38630001 : 0x356B0001);
                            uint32_t opcode = primary << 26 | destination << 21 | operation << 1 | rc;
                            opcode |= unary ? 2 << 11 : (1 << 16) | (operation == 25 ? 2 << 6 : 2 << 11);
                            if (operation >= 28)
                                opcode |= 3 << 6;
                            word(f, 0x100 + prefix * 4, opcode);
                            f.env.opaque = &f;
                            f.env.memory = map;
                            f.env.linear_memory = f.memory;
                            f.env.linear_size = sizeof f.memory;
                            f.env.instruction_limit = prefix + 1;
                            f.env.translation_cache = cache;
                            std::unique_ptr<ROMlib_ppc_system750> state(new ROMlib_ppc_system750{});
                            ROMlib_ppc_physical_bus bus = {&f,
                                                           [](void* opaque, uint32_t at, void* bytes, unsigned size,
                                                              unsigned access, unsigned, uint32_t*) -> int {
                                                               auto* memory =
                                                                   map(opaque, at, size, access & ROMLIB_PPC_MMU_WRITE);
                                                               if (!memory)
                                                                   return 0;
                                                               if (access & ROMLIB_PPC_MMU_WRITE)
                                                                   memcpy(memory, bytes, size);
                                                               else
                                                                   memcpy(bytes, memory, size);
                                                               return 1;
                                                           },
                                                           [](void*, uint32_t, uint32_t, uint32_t*) { return 1; }};
                            f.env.system = architectural ? state.get() : nullptr;
                            f.env.physical_bus = architectural ? &bus : nullptr;
                            assert(ROMlib_ppc_translate_range_mode(cache, f.memory + 0x100, 0x100, (prefix + 1) * 4,
                                                                   architectural ? ROMLIB_PPC_TRANSLATE_750 : 0) <=
                                   (architectural ? prefix + 1 : 1));
                            for (const auto& value : values)
                                for (unsigned rounding = 0; rounding < 4; ++rounding)
                                    for (unsigned enables : {0u, 0x90u, 0xF8u, 0xFCu}) {
                                        ROMlib_ppc_registers initial = {};
                                        initial.pc = 0x100;
                                        initial.cr = 0x13579BDF;
                                        initial.xer = 0xE0000042;
                                        initial.gpr[11] = UINT32_MAX;
                                        initial.fpscr = 0x00070000 | rounding | enables;
                                        for (unsigned reg = 0; reg < 32; ++reg) {
                                            uint64_t bits = UINT64_C(0x7FF0000000000010) + reg;
                                            memcpy(&initial.fpr[reg], &bits, sizeof bits);
                                        }
                                        memcpy(&initial.fpr[1], &value.a, sizeof value.a);
                                        memcpy(&initial.fpr[2], &value.b, sizeof value.b);
                                        if (operation >= 28)
                                            memcpy(&initial.fpr[3], &value.a, sizeof value.a);
                                        auto expected = initial;
                                        for (bool native : {false, true}) {
                                            *state = {};
                                            state->mmu.msr = 0x2000;
                                            f.env.execution_flags = native ? ROMLIB_PPC_EXEC_JIT : 0;
                                            auto actual = initial;
                                            bool ok = ROMlib_ppc_execute(&actual, &f.env);
                                            assert(fegetround() == FE_DOWNWARD &&
                                                   fetestexcept(FE_ALL_EXCEPT) == exceptions);
                                            if (ok || !strstr(f.env.error, "instruction limit"))
                                                fprintf(stderr, "direct FP launch %08x native=%u system=%u: %s\n",
                                                        opcode, native, architectural, f.env.error);
                                            assert(!ok && strstr(f.env.error, "instruction limit"));
                                            assert(actual.pc == 0x104 + prefix * 4 && f.env.instructions == prefix + 1);
                                            if (native) {
                                                if (memcmp(&actual, &expected, sizeof actual))
                                                    fprintf(stderr,
                                                            "direct FP %08x a=%016llx b=%016llx RN=%u system=%u\n",
                                                            opcode, (unsigned long long)value.a,
                                                            (unsigned long long)value.b, rounding, architectural);
                                                assert(!memcmp(&actual, &expected, sizeof actual));
                                                bool exceptional = value.exceptional || (operation == 18 && !value.b) ||
                                                                   (operation == 22 && (value.b >> 63));
                                                if (operation == 24 || operation == 26)
                                                    exceptional = value.exceptional || !(value.b << 1) ||
                                                                  (operation == 26 && (value.b >> 63));
                                                if (operation >= 28)
                                                    exceptional = ((value.a >> 52) & 0x7ff) == 0x7ff ||
                                                                  ((value.b >> 52) & 0x7ff) == 0x7ff;
                                                assert(f.env.native_kernel_instructions == unsigned(exceptional));
                                                assert(f.env.native_instructions == prefix + 1 &&
                                                       f.env.native_blocks == (architectural ? prefix + 1 : 1) &&
                                                       !f.env.native_compilations);
                                            }
                                            else
                                                expected = actual;
                                        }
                                        ++cases;
                                    }
                        }
        }
    ROMlib_ppc_translation_cache_destroy(cache);
    assert(!fesetenv(&original));
    printf("%u direct floating arithmetic/status/alias/rounding and exceptional-path cases passed\n", cases);
}

void ppc_check_native_kernels()
{
    const uint32_t kernels[] = {
        0xFC22182A, 0xFC221828, 0xFC2200F2, 0xFC221824, 0xFC20182C, 0xFC201818, 0xFC221800, 0xEC201830, 0xFC2218BA,
        0x7C0327AD, // Reserved ICBI Rc retains the flat interpreter behavior.
        0xFC231890, // Reserved FMR operand: checked rejection from a native tail.
    };
    const uint32_t inline_tails[] = {
        0x4C00012C, 0x7C0004AC, 0x7C0006AC, 0x7C0327AC,             // isync, sync, eieio, icbi
        0x7C03206C, 0x7C0320AC, 0x7C0321EC, 0x7C03222C, 0x7C0323AC, // dcbst, dcbf, dcbtst, dcbt, dcbi
        0x48000005, 0x42000005, 0x4E800021,                         // bl, bdnzl, bclrl
        0xFC00010C, 0xFC00058E, 0xFC00004C, 0xFC00008C,             // mtfsfi, mtfsf, mtfsb1, mtfsb0
        0xFC021800, 0xFC20181D, 0xFC20181E,                         // fctiw., fctiwz
    };
    auto check = [&](const uint32_t* opcodes, size_t count, bool semantic) {
        unsigned cases = 0;
        for (size_t i = 0; i < count; ++i)
            for (unsigned prefix : {0u, 3u})
                for (unsigned limit = 0; limit <= prefix + 2; ++limit)
                    for (bool reject : {false, true})
                        for (bool linear : {false, true}) {
                            auto opcode = opcodes[i];
                            auto reference = run(opcode, prefix, limit, reject, linear, false, semantic);
                            auto actual = run(opcode, prefix, limit, reject, linear, true, semantic);
                            if (reference.success != actual.success ||
                                memcmp(&reference.registers, &actual.registers, sizeof reference.registers) ||
                                reference.memory != actual.memory || reference.instructions != actual.instructions ||
                                reference.accesses != actual.accesses || strcmp(reference.error, actual.error)) {
                                fprintf(stderr,
                                        "kernel %08x prefix %u limit %u reject %u linear %u: "
                                        "success %u/%u instructions %llu/%llu PC %x/%x\n",
                                        opcode, prefix, limit, reject, linear, reference.success, actual.success,
                                        (unsigned long long)reference.instructions,
                                        (unsigned long long)actual.instructions, reference.registers.pc,
                                        actual.registers.pc);
                                assert(false);
                            }
                            ++cases;
                        }
        return cases;
    };
    printf("%u eager native-kernel state/memory/fault/poll-limit cases passed\n",
           check(kernels, sizeof kernels / sizeof *kernels, true));
    printf("%u eager native branch, cache, synchronization and floating-point cases passed\n",
           check(inline_tails, sizeof inline_tails / sizeof *inline_tails, false));
    test_poll_boundary(false);
    test_poll_boundary(true);
    puts("Native prefixes return at a poll boundary and honor a rewritten word or reduced instruction limit");
    test_block_limits();
    test_guarded_load_block();
    test_memory_tails();
    test_direct_floating_arithmetic();
}

#if !defined(SYNPPC_TRANSLATION_CHECKS)
int main()
{
    ppc_check_native_kernels();
}
#endif
