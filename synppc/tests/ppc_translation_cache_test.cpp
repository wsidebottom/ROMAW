/*+===================================================================
  File:      ppc_translation_cache_test.cpp

  Summary:   Implements ppc translation cache test support for synppc PowerPC execution and fragment loading.

  Classes:   Declared: bus_event, fixture; Used: ROMlib_ppc_environment, ROMlib_ppc_registers, ROMlib_ppc_translation_cache,
             fixture, std::string, std::vector

  Functions: ppc_check_translation_cache

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

#include "synppc/bus750.h"
#include <memory>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

struct bus_event {
    uint32_t address;
    unsigned access, wimg;
};

struct fixture {
    unsigned char memory[1024] = {};
    ROMlib_ppc_environment env = {};
    std::vector<bus_event> bus_events;
    unsigned tail_reads = 0;
};

static void* map(void* opaque, uint32_t address, size_t length, int)
{
    fixture& f = *static_cast<fixture*>(opaque);
    return address <= sizeof f.memory && length <= sizeof f.memory - address ? f.memory + address : nullptr;
}

static void word(fixture& f, unsigned address, uint32_t value)
{
    for (int i = 3; i >= 0; --i) {
        f.memory[address + i] = value;
        value >>= 8;
    }
}

static void program(fixture& f, ROMlib_ppc_translation_cache* cache, unsigned increment)
{
    word(f, 0x80, 0x18000000);
    for (unsigned i = 0; i < 4; ++i)
        word(f, 0x100 + i * 4, 0x38630000 | increment);
    word(f, 0x110, 0x4200fff0); /* bdnz 0x100 */
    word(f, 0x114, 0x18000000);
    f.env.opaque = &f;
    f.env.memory = map;
    f.env.linear_memory = f.memory;
    f.env.linear_size = sizeof f.memory;
    f.env.execution_flags = ROMLIB_PPC_EXEC_JIT;
    f.env.translation_cache = cache;
}

static ROMlib_ppc_registers run(fixture& f, unsigned seed, unsigned increment)
{
    ROMlib_ppc_registers r = {};
    r.pc = 0x100;
    r.ctr = 64;
    r.gpr[3] = seed;
    assert(ROMlib_ppc_execute(&r, &f.env));
    assert(r.gpr[3] == seed + 256 * increment);
    return r;
}

static int physical(void* opaque, uint32_t address, void* bytes, unsigned size, unsigned access, unsigned wimg,
                    uint32_t*)
{
    auto& f = *static_cast<fixture*>(opaque);
    if (access & (ROMLIB_PPC_PHYSICAL_EXTERNAL | ROMLIB_PPC_PHYSICAL_CONTROL))
        f.bus_events.push_back({address, access, wimg});
    if (access & ROMLIB_PPC_PHYSICAL_CONTROL) {
        assert(!size && !bytes);
        return 1;
    }
    auto* memory = map(opaque, address, size, access & ROMLIB_PPC_MMU_WRITE);
    if (!memory)
        return 0;
    if (access & ROMLIB_PPC_MMU_WRITE)
        memcpy(memory, bytes, size);
    else
        memcpy(bytes, memory, size);
    return 1;
}

static void direct_architectural_integers()
{
    auto* cache = ROMlib_ppc_translation_cache_create();
    assert(cache);
    fixture f;
    ROMlib_ppc_physical_bus bus = {&f, physical, [](void*, uint32_t, uint32_t, uint32_t*) { return 1; }};
    f.env.opaque = &f;
    f.env.memory = map;
    f.env.physical_bus = &bus;
    f.env.translation_cache = cache;
    f.env.instruction_limit = 1;
    std::unique_ptr<ROMlib_ppc_system750> state(new ROMlib_ppc_system750{});
    f.env.system = state.get();
    unsigned cases = 0;
    for (unsigned xo : {266u, 778u, 552u, 10u,  8u,   138u, 136u, 522u, 520u, 650u, 648u, 235u, 747u, 491u, 1003u,
                        459u, 971u, 75u,  11u,  40u,  28u,  60u,  124u, 284u, 316u, 412u, 444u, 476u, 24u,  536u,
                        792u, 824u, 26u,  954u, 922u, 104u, 616u, 234u, 202u, 232u, 200u, 746u, 714u, 744u, 712u})
        for (unsigned rc : {0u, 1u})
            for (unsigned alias = 0; alias < 6; ++alias)
                for (uint32_t seed : {0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0xA5A55A5Au}) {
                    unsigned destination = alias == 5 ? 0 : 3;
                    unsigned a = alias == 1 || alias == 4 ? destination : 4;
                    unsigned b = alias == 2 || alias == 4 ? destination : alias == 3 ? a : 5;
                    if (xo == 26 || xo == 954 || xo == 922 || xo == 104 || xo == 616 || (xo & ~512u) == 234 ||
                        (xo & ~512u) == 202 || (xo & ~512u) == 232 || (xo & ~512u) == 200)
                        b = 0;
                    uint32_t opcode = 31u << 26 | destination << 21 | a << 16 | b << 11 | xo << 1 | rc;
                    word(f, 0x100, opcode);
                    assert(ROMlib_ppc_translate_range_mode(cache, f.memory + 0x100, 0x100, 4,
                                                           ROMLIB_PPC_TRANSLATE_750) <= 1);
                    ROMlib_ppc_registers initial = {};
                    initial.pc = 0x100;
                    initial.cr = 0x13579BDF;
                    initial.xer = (seed & 0xE0000000) | 0x42;
                    for (unsigned reg = 0; reg < 32; ++reg)
                        initial.gpr[reg] = seed ^ (0x01010101u * reg);
                    ROMlib_ppc_registers expected = {};
                    for (bool native : {false, true}) {
                        *state = {};
                        state->mmu.msr = 0x2000;
                        f.env.execution_flags = native ? ROMLIB_PPC_EXEC_JIT : 0;
                        auto actual = initial;
                        assert(!ROMlib_ppc_execute(&actual, &f.env) && strstr(f.env.error, "instruction limit"));
                        assert(actual.pc == 0x104 && f.env.instructions == 1);
                        if (native) {
                            assert(!memcmp(&actual, &expected, sizeof actual));
                            assert(f.env.native_instructions == 1 && f.env.native_blocks == 1 &&
                                   f.env.native_cache_hits == 1 && !f.env.native_compilations &&
                                   !f.env.native_kernel_instructions);
                        }
                        else
                            expected = actual;
                    }
                    ++cases;
                }
    ROMlib_ppc_translation_cache_destroy(cache);
    printf("%u architectural integer/record/alias cases execute directly without semantic helpers\n", cases);
}

static bool no_instruction_kernel(const ROMlib_ppc_environment& env)
{
    return env.native_kernel_instructions == 0;
}

static void run_persisted_operations(fixture& f)
{
    ROMlib_ppc_physical_bus bus = {&f, physical, [](void*, uint32_t, uint32_t, uint32_t*) { return 1; }};
    std::unique_ptr<ROMlib_ppc_system750> state(new ROMlib_ppc_system750{});
    f.env.system_service_gate = [](void*, uint32_t pc, uint32_t opcode) -> int {
        return pc == 0x128 && opcode == 0x18000000;
    };
    for (bool architectural : {false, true}) {
        state->mmu.msr = 0x2000;
        f.env.system = architectural ? state.get() : nullptr;
        f.env.physical_bus = architectural ? &bus : nullptr;
        f.env.linear_memory = architectural ? nullptr : f.memory;
        ROMlib_ppc_registers r = {};
        r.pc = 0x120;
        r.fpr[1] = 1.5;
        assert(ROMlib_ppc_execute(&r, &f.env));
        uint64_t nearest, truncated;
        memcpy(&nearest, &r.fpr[3], sizeof nearest);
        memcpy(&truncated, &r.fpr[4], sizeof truncated);
        assert(nearest == 2 && truncated == 1 && r.fpscr == 0x82020000 && r.cr == 0x08000000);
        assert(f.env.native_blocks == (architectural ? 2u : 1u) && f.env.native_instructions == 2 &&
               no_instruction_kernel(f.env) && !f.env.native_compilations);
    }

    const struct {
        uint32_t pc, input, vector, saved_pc, cause;
    } exceptions[] = {
        {0x130, 7, 0x700, 0x130, 0x20000}, // TWI taken, saving the faulting PC.
        {0x130, 8, 0xD00, 0x134, 0},       // TWI falls through to the trace exception.
        {0x134, 0, 0xC00, 0x138, 0},       // SC saves the next PC.
    };

    f.env.instruction_limit = 1;
    for (const auto& test : exceptions) {
        state->mmu.msr = 0x2400;
        ROMlib_ppc_registers r = {};
        r.pc = test.pc;
        r.gpr[3] = test.input;
        assert(!ROMlib_ppc_execute(&r, &f.env) && strstr(f.env.error, "instruction limit"));
        assert(r.pc == test.vector && state->srr0 == test.saved_pc && state->srr1 == (0x2400 | test.cause));
        assert(f.env.instructions == 1 && f.env.native_instructions == 1 && f.env.native_cache_hits == 1 &&
               no_instruction_kernel(f.env) && !f.env.native_compilations);
    }
    std::unique_ptr<ROMlib_ppc_system750> second_state(new ROMlib_ppc_system750{});
    for (auto* live_state : {state.get(), second_state.get()}) {
        uint32_t tag = live_state == state.get() ? 0x0001AA55u : 0x7001AA66u;
        live_state->mmu.msr = 0x2000;
        live_state->mmu.sr[10] = tag;
        f.env.system = live_state;
        ROMlib_ppc_registers r = {};
        r.pc = 0x138;
        r.gpr[0] = 0x10000000; // STWBRX treats RA=0 as zero.
        r.gpr[3] = 0xA1000000;
        r.gpr[7] = 0x182;
        r.gpr[8] = 0x188;
        r.gpr[10] = 0x194;
        uint32_t source = (tag & 0xFF) << 16 | 0x80000000u;
        uint32_t fraction = tag & 0xFF;
        word(f, 0x180, source);
        word(f, 0x188, 0);
        word(f, 0x18C, 0xFF800000u | fraction);
        word(f, 0x190, 0);
        word(f, 0x194, 0);
        f.env.instruction_limit = 7;
        assert(!ROMlib_ppc_execute(&r, &f.env) && strstr(f.env.error, "instruction limit"));
        assert(r.pc == 0x154 && r.gpr[3] == tag && r.gpr[4] == 0x2000);
        assert(r.gpr[6] == (0xFFFF8000u | fraction) && r.gpr[7] == 0x180 && r.gpr[8] == 0x18C);
        assert(f.memory[0x188] == (tag & 0xFF) && f.memory[0x189] == 0x80 && f.memory[0x18A] == 0xFF &&
               f.memory[0x18B] == 0xFF);
        uint64_t bits;
        memcpy(&bits, &r.fpr[9], sizeof bits);
        assert(bits == (UINT64_C(0xFFF0000000000000) | (uint64_t(fraction) << 29)));
        assert(!memcmp(f.memory + 0x18C, f.memory + 0x190, 4));
        assert(f.memory[0x194] == (fraction & 7) << 5 && !f.memory[0x195] && !f.memory[0x196] && !f.memory[0x197]);
        assert(f.env.instructions == 7 && f.env.native_instructions == 7 && f.env.native_cache_hits == 7 &&
               no_instruction_kernel(f.env) && !f.env.native_compilations);

        // Checkpoint between all three operations so the cached code must use
        // the persistent 750 reservation across CPU replacement.
        r.gpr[12] = 0x1A0;
        r.cr = 0x9ABCDEFF;
        r.xer = 0x80000000;
        word(f, 0x1A0, 0);
        f.env.instruction_limit = 1;
        for (unsigned step = 0; step < 3; ++step) {
            assert(!ROMlib_ppc_execute(&r, &f.env) && strstr(f.env.error, "instruction limit"));
            assert(r.pc == 0x158 + step * 4 && r.gpr[11] == source);
            assert(f.env.instructions == 1 && f.env.native_instructions == 1 && f.env.native_cache_hits == 1 &&
                   no_instruction_kernel(f.env) && !f.env.native_compilations);
            assert(bool(live_state->reservation_valid) == (step == 0) && live_state->reservation_address == 0x180);
            const uint32_t conditions[] = {0x9ABCDEFF, 0x3ABCDEFF, 0x1ABCDEFF};
            assert(r.cr == conditions[step] && r.xer == 0x80000000);
            if (step)
                assert(f.memory[0x1A0] == 0xFF && f.memory[0x1A1] == 0xFF && f.memory[0x1A2] == 0x80 &&
                       f.memory[0x1A3] == fraction);
        }

        r.gpr[13] = 0x1C0;
        r.gpr[14] = 0;
        r.gpr[15] = 0x1D0;
        r.gpr[16] = 0x1E0;
        r.xer = 0x80000007;
        word(f, 0x1C0, tag);
        word(f, 0x1C4, 0x99887766);
        word(f, 0x1D4, 0xDEADBEEF);
        f.env.instruction_limit = 4;
        assert(!ROMlib_ppc_execute(&r, &f.env) && strstr(f.env.error, "instruction limit"));
        assert(r.pc == 0x170 && r.gpr[30] == tag && r.gpr[31] == 0x99887700 && r.xer == 0x80000007);
        assert(!memcmp(f.memory + 0x1C0, f.memory + 0x1D0, 7) && f.memory[0x1D7] == 0xEF);
        assert(r.gpr[24] == tag && r.gpr[25] == 0x99887700 && r.gpr[26] == r.gpr[0] && r.gpr[27] == r.gpr[1]);
        assert(f.env.instructions == 4 && f.env.native_instructions == 4 && f.env.native_cache_hits == 4 &&
               no_instruction_kernel(f.env) && !f.env.native_compilations);

        r.pc = 0x240;
        r.gpr[17] = 0x220;
        r.gpr[18] = 3;
        r.gpr[19] = 0x200;
        live_state->hid0 = ROMLIB_PPC_750_HID0_DCE | ROMLIB_PPC_750_HID0_ABE;
        unsigned resource = tag & 15;
        live_state->ear = 0x80000000 | resource;
        memset(f.memory + 0x220, 0xA5, 32);
        f.bus_events.clear();
        f.env.instruction_limit = 6;
        assert(!ROMlib_ppc_execute(&r, &f.env) && strstr(f.env.error, "instruction limit"));
        assert(r.pc == 0x258 && r.gpr[20] == tag);
        const unsigned external = ROMLIB_PPC_PHYSICAL_EXTERNAL | (resource << ROMLIB_PPC_PHYSICAL_RESOURCE_SHIFT);
        const bus_event expected[] = {
            {0x200, external | ROMLIB_PPC_MMU_WRITE, 5},
            {0x200, external, 5},
            {0x220, ROMLIB_PPC_PHYSICAL_CONTROL | (ROMLIB_PPC_BUS_DCBZ << ROMLIB_PPC_PHYSICAL_RESOURCE_SHIFT), 3},
            {0x220, ROMLIB_PPC_PHYSICAL_CONTROL | (ROMLIB_PPC_BUS_DCBF << ROMLIB_PPC_PHYSICAL_RESOURCE_SHIFT), 3},
            {0, ROMLIB_PPC_PHYSICAL_CONTROL | (ROMLIB_PPC_BUS_SYNC << ROMLIB_PPC_PHYSICAL_RESOURCE_SHIFT), 0},
            {0, ROMLIB_PPC_PHYSICAL_CONTROL | (ROMLIB_PPC_BUS_EIEIO << ROMLIB_PPC_PHYSICAL_RESOURCE_SHIFT), 0},
        };
        assert(f.bus_events.size() == sizeof expected / sizeof *expected);
        for (unsigned i = 0; i < f.bus_events.size(); ++i) {
            const auto& event = f.bus_events[i];
            assert(event.address == expected[i].address && event.access == expected[i].access &&
                   event.wimg == expected[i].wimg);
        }
        for (unsigned i = 0x220; i < 0x240; ++i)
            assert(!f.memory[i]);
        assert(f.env.instructions == 6 && f.env.native_instructions == 6 && f.env.native_cache_hits == 6 &&
               no_instruction_kernel(f.env) && !f.env.native_compilations);

        unsigned set = tag & 63;
        bool held = live_state == state.get();
        r.gpr[21] = 0x8400007Cu | (set << 12);
        memset(live_state->mmu.tlb, 0xA5, sizeof live_state->mmu.tlb);
        memset(live_state->mmu.replace, 1, sizeof live_state->mmu.replace);
        auto expected_mmu = live_state->mmu;
        for (unsigned bank = 0; bank < 2; ++bank) {
            memset(expected_mmu.tlb[bank][set], 0, sizeof expected_mmu.tlb[bank][set]);
            expected_mmu.replace[bank][set] = 0;
        }
        ROMlib_ppc_750_set_inputs(live_state, ROMLIB_PPC_750_INT | (held ? ROMLIB_PPC_750_TLBISYNC : 0));
        f.env.instruction_limit = 2;
        assert(!ROMlib_ppc_execute(&r, &f.env));
        assert(strstr(f.env.error, held ? "TLBISYNC release" : "instruction limit"));
        assert(r.pc == 0x260 && bool(live_state->tlbsync_wait) == held && bool(f.env.waiting_for_tlb) == held);
        assert(!memcmp(&live_state->mmu, &expected_mmu, sizeof expected_mmu));
        assert(f.env.instructions == 2 && f.env.native_instructions == 2 && f.env.native_cache_hits == 2 &&
               no_instruction_kernel(f.env) && !f.env.native_compilations);
        ROMlib_ppc_750_set_inputs(live_state, 0);
        f.env.instruction_limit = 1;
        assert(!ROMlib_ppc_execute(&r, &f.env) && strstr(f.env.error, "instruction limit"));
        assert(r.pc == 0x264 && r.gpr[4] == 0x2001 && !f.env.waiting_for_tlb);
        assert(f.env.native_instructions == 1 && f.env.native_cache_hits == 1 && !f.env.native_compilations);

        bool user = tag & 1;
        uint32_t target = user ? 0x2A0 : 0x2C0;
        r.gpr[22] = 0x00012000; // ILE is live, without a corresponding saved field.
        live_state->srr0 = target | 3;
        live_state->srr1 = 0x003E2004 | (user ? 0x4000 : 0); // Include unsaved SRR1 bits.
        live_state->mmcr0 = 9;                               // PMC2: PR changes.
        live_state->mmcr1 = 9u << 22;                        // PMC4: supervisor PM changes.
        live_state->pmc[1] = live_state->pmc[3] = 0;
        live_state->iabr = target | 2;
        live_state->iabr_pending = 1;
        live_state->iabr_previous = 0;
        word(f, target, 0x38600007);
        f.env.instruction_limit = 1;
        assert(!ROMlib_ppc_execute(&r, &f.env) && strstr(f.env.error, "instruction limit"));
        assert(r.pc == 0x268 && live_state->mmu.msr == 0x00012000 && live_state->iabr_pending);
        assert(f.env.native_instructions == 1 && f.env.native_cache_hits == 1 && no_instruction_kernel(f.env) &&
               !f.env.native_compilations);
        assert(!ROMlib_ppc_execute(&r, &f.env) && strstr(f.env.error, "instruction limit"));
        assert(r.pc == target && live_state->mmu.msr == (0x00012004u | (user ? 0x4000 : 0)) &&
               !live_state->iabr_pending);
        assert(live_state->pmc[1] == unsigned(user) && live_state->pmc[3] == 1);
        assert(f.env.native_instructions == 1 && f.env.native_cache_hits == 1 && no_instruction_kernel(f.env) &&
               !f.env.native_compilations);
        assert(!ROMlib_ppc_execute(&r, &f.env) && strstr(f.env.error, "instruction limit"));
        assert(r.pc == 0x1300 && live_state->srr0 == target && r.gpr[3] == tag && !f.env.native_instructions);

        r.pc = 0x26C;
        r.gpr[22] = 0x8000000D;
        r.gpr[23] = tag;
        r.gpr[24] = 0x282;
        live_state->mmu.msr = 0x2000; // Reset exception-selected endian mode for this instruction stream.
        live_state->decrementer = 13;
        live_state->decrementer_pending = 0;
        live_state->time_base = UINT64_C(0x123456789ABCDEF0);
        f.env.instruction_limit = 3;
        assert(!ROMlib_ppc_execute(&r, &f.env) && strstr(f.env.error, "instruction limit"));
        assert(r.pc == 0x278 && live_state->decrementer == 0x8000000D && live_state->decrementer_pending);
        assert(live_state->time_base == (UINT64_C(0x1234567800000000) | tag));
        assert(live_state->iabr_pending && live_state->iabr == 0x282 && live_state->iabr_previous == (target | 2));
        assert(f.env.instructions == 3 && f.env.native_instructions == 3 && f.env.native_cache_hits == 3 &&
               no_instruction_kernel(f.env) && !f.env.native_compilations);
    }
    f.env.instruction_limit = 0;
    f.env.system = nullptr;
    f.env.physical_bus = nullptr;
    f.env.linear_memory = f.memory;
}

static void run_persisted_arithmetic(fixture& f)
{
    std::unique_ptr<ROMlib_ppc_system750> state(new ROMlib_ppc_system750{});
    ROMlib_ppc_physical_bus bus = {&f, physical, [](void*, uint32_t, uint32_t, uint32_t*) { return 1; }};
    f.env.system_service_gate = [](void*, uint32_t pc, uint32_t opcode) -> int {
        return pc == 0x29C && opcode == 0x18000000;
    };
    for (bool architectural : {false, true})
        for (uint64_t first : {UINT64_C(0x3FF0000000000000), UINT64_C(1), UINT64_C(0x7FF0000000000001)})
            for (unsigned rounding = 0; rounding < 4; ++rounding) {
                f.env.system = architectural ? state.get() : nullptr;
                f.env.physical_bus = architectural ? &bus : nullptr;
                f.env.linear_memory = architectural ? nullptr : f.memory;
                ROMlib_ppc_registers initial = {};
                initial.pc = 0x280;
                initial.cr = 0x13579BDF;
                initial.fpscr = rounding | 0x70000;
                memcpy(&initial.fpr[1], &first, sizeof first);
                initial.fpr[2] = 3.0;
                auto expected = initial;
                for (bool native : {false, true}) {
                    *state = {};
                    state->mmu.msr = 0x2000;
                    f.env.execution_flags = native ? ROMLIB_PPC_EXEC_JIT : 0;
                    auto actual = initial;
                    assert(ROMlib_ppc_execute(&actual, &f.env));
                    if (native) {
                        assert(!memcmp(&actual, &expected, sizeof actual));
                        unsigned helpers = first == UINT64_C(0x3FF0000000000000) ? 0u : first == UINT64_C(1) ? 4u : 6u;
                        assert(f.env.native_instructions == 7 && f.env.native_blocks == 7 &&
                               f.env.native_cache_hits == 7 && !f.env.native_compilations &&
                               f.env.native_kernel_instructions == helpers);
                    }
                    else
                        expected = actual;
                }
            }
    f.env.system = nullptr;
    f.env.physical_bus = nullptr;
    f.env.linear_memory = f.memory;
    f.env.execution_flags = ROMLIB_PPC_EXEC_JIT;
    puts("Saved native arithmetic reuses direct normal and checked exceptional paths in flat/750 modes");
}

static void architectural(fixture& f, unsigned seed, unsigned increment, unsigned compilations = 0)
{
    std::unique_ptr<ROMlib_ppc_system750> state(new ROMlib_ppc_system750{});
    ROMlib_ppc_physical_bus bus = {&f, physical, [](void*, uint32_t, uint32_t, uint32_t*) { return 1; }};
    f.env.system = state.get();
    f.env.physical_bus = &bus;
    f.env.linear_memory = nullptr;
    f.env.system_service_gate = [](void*, uint32_t pc, uint32_t opcode) -> int {
        return pc == 0x114 && opcode == 0x18000000;
    };
    run(f, seed, increment);
    assert(f.env.native_compilations == compilations);
    assert(f.env.native_blocks == f.env.native_instructions);
    assert(f.env.native_instructions == (64 - (compilations ? 7 : 0)) * 5);
    f.env.system = nullptr;
    f.env.physical_bus = nullptr;
    f.env.linear_memory = f.memory;
}

static void test_full_cache()
{
    auto* cache = ROMlib_ppc_translation_cache_create();
    assert(cache);
    fixture f;
    program(f, cache, 5);
    word(f, 0x200, 0x4E800020); // A previously executed flat entry must survive eviction.
    assert(ROMlib_ppc_translate_range(cache, f.memory + 0x200, 0x200, 4) == 1);
    std::vector<unsigned char> filler(4095 * 4);
    for (size_t i = 0; i < filler.size(); i += 4) {
        filler[i] = 0x48;
        filler[i + 3] = 4; // b +4 ends each eagerly prepared block.
    }
    assert(ROMlib_ppc_translate_range(cache, filler.data(), 0x1000, filler.size()) == 4095);
    auto retained = [&]() {
        ROMlib_ppc_registers r = {};
        r.pc = 0x200;
        r.lr = 0x114;
        assert(ROMlib_ppc_execute(&r, &f.env));
        assert(!f.env.native_compilations && f.env.native_blocks == 1 && f.env.native_cache_hits == 1);
    };
    retained();
    run(f, 1, 5);
    assert(f.env.native_compilations == 1 && f.env.native_blocks == 57);
    run(f, 2, 5);
    assert(!f.env.native_compilations && f.env.native_blocks == 64);
    architectural(f, 3, 5, 5);
    architectural(f, 4, 5);
    retained();
    run(f, 5, 5);
    assert(!f.env.native_compilations && f.env.native_blocks == 64);
    assert(!ROMlib_ppc_translate_range(cache, filler.data(), 0x6000, 4));

    // The new start has no slot, but the rewritten block can reuse its own.
    word(f, 0x1fc, 0x60000000);
    word(f, 0x200, 0x38601234);
    word(f, 0x204, 0x4E800020);
    assert(ROMlib_ppc_translate_range(cache, f.memory + 0x1fc, 0x1fc, 12) == 1);
    assert(!ROMlib_ppc_translate_range(cache, f.memory + 0x1fc, 0x1fc, 12));
    ROMlib_ppc_registers result = {};
    result.pc = 0x1fc;
    result.lr = 0x114;
    assert(ROMlib_ppc_execute(&result, &f.env) && result.gpr[3] == 0x1234);
    assert(!f.env.native_compilations && f.env.native_instructions == 2 && f.env.native_cache_hits == 1);

    for (unsigned i = 0; i < 4; ++i)
        word(f, 0x100 + i * 4, 0x38630007);
    assert(ROMlib_ppc_translate_range_mode(cache, f.memory + 0x100, 0x100, 24, ROMLIB_PPC_TRANSLATE_750) == 4);
    architectural(f, 6, 7);
    for (unsigned i = 0; i < 4; ++i)
        word(f, 0x100 + i * 4, 0x38630005);
    run(f, 7, 5);
    assert(!f.env.native_compilations && f.env.native_blocks == 64);

    word(f, 0x200, 0); // An unsupported rewrite retires the old translation.
    assert(!ROMlib_ppc_translate_range(cache, f.memory + 0x200, 0x200, 4));
    assert(ROMlib_ppc_translate_range(cache, filler.data(), 0x6000, 4) == 1);
    ROMlib_ppc_translation_cache_destroy(cache);
}

static void replace_active_semantic_tail()
{
    auto* cache = ROMlib_ppc_translation_cache_create();
    assert(cache);
    fixture outer;
    program(outer, cache, 1);
    outer.env.linear_size = 0x200;  // Fetch code directly but route the operand through the callback.
    word(outer, 0x104, 0x7C00208E); // lvewx v0,0,r4; checked memory kernel.
    word(outer, 0x108, 0x18000000);
    word(outer, 0x300, 0x12345678);
    outer.env.memory = [](void* opaque, uint32_t address, size_t length, int write) -> void* {
        auto& f = *static_cast<fixture*>(opaque);
        if (address == 0x300) {
            assert(length == 4 && !write && f.tail_reads == 0);
            ++f.tail_reads;
            fixture inner;
            program(inner, f.env.translation_cache, 7);
            run(inner, 31, 7);
            assert(inner.env.native_compilations == 1);
        }
        return map(opaque, address, length, write);
    };
    assert(ROMlib_ppc_translate_range(cache, outer.memory + 0x100, 0x100, 12) == 1);
    ROMlib_ppc_registers registers = {};
    registers.pc = 0x100;
    registers.gpr[4] = 0x300;
    assert(ROMlib_ppc_execute(&registers, &outer.env));
    assert(registers.gpr[3] == 1 && outer.tail_reads == 1 && outer.env.instructions == 3 &&
           outer.env.native_instructions == 2 && outer.env.native_kernel_instructions == 1 &&
           !outer.env.native_compilations);
    ROMlib_ppc_translation_cache_destroy(cache);
    puts("Native semantic tail returns after its memory callback replaces the active block");
}

static unsigned services;
static unsigned inner_increment = 2;

static int nested(void* opaque, uint32_t id, ROMlib_ppc_registers* outer)
{
    fixture& f = *static_cast<fixture*>(opaque);
    assert(id == 1);
    if (++services == 1) {
        fixture inner;
        program(inner, f.env.translation_cache, inner_increment);
        run(inner, 101, inner_increment);
        if (inner_increment == 2)
            assert(!inner.env.native_compilations && inner.env.native_cache_hits);
        else
            assert(inner.env.native_compilations);
        outer->ctr = 64;
        outer->lr = 0x100;
    }
    else
        outer->lr = 0x80;
    return 1;
}

static std::vector<unsigned char> read_file(const std::string& path)
{
    FILE* file = fopen(path.c_str(), "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long size = ftell(file);
    assert(size >= 0 && !fseek(file, 0, SEEK_SET));
    std::vector<unsigned char> bytes(size);
    assert(fread(bytes.data(), 1, bytes.size(), file) == bytes.size());
    assert(!fclose(file));
    return bytes;
}

static void write_file(const std::string& path, const std::vector<unsigned char>& bytes)
{
    FILE* file = fopen(path.c_str(), "wb");
    assert(file && (bytes.empty() || fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size()));
    assert(!fclose(file));
}

static void checksum(std::vector<unsigned char>& bytes)
{
    uint32_t crc = ~0u;
    for (unsigned i = 0; i + 4 < bytes.size(); ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    crc = ~crc;
    for (unsigned i = 0; i < 4; ++i)
        bytes[bytes.size() - 4 + i] = crc >> (i * 8);
}

int ppc_check_translation_cache(int argc, char** argv)
{
    auto cache = ROMlib_ppc_translation_cache_create();
    if (!cache)
        return 0;
    if (argc == 3) {
        fixture separate;
        program(separate, cache, 5);
        word(separate, 0x120, 0xFC60081D); // fctiw. f3,f1
        word(separate, 0x124, 0xFC80081E); // fctiwz f4,f1
        word(separate, 0x128, 0x18000000);
        word(separate, 0x130, 0x0C830007); // twi 4,r3,7
        word(separate, 0x134, 0x44000002); // sc
        word(separate, 0x138, 0x7C8000A6); // mfmsr r4
        word(separate, 0x13C, 0x7C601D26); // mfsrin r3,r3
        word(separate, 0x140, 0xACC7FFFE); // lhau r6,-2(r7)
        word(separate, 0x144, 0x7CC0452C); // stwbrx r6,0,r8
        word(separate, 0x148, 0xC5280004); // lfsu f9,4(r8)
        word(separate, 0x14C, 0xD1280004); // stfs f9,4(r8)
        word(separate, 0x150, 0x7D2057AE); // stfiwx f9,0,r10
        word(separate, 0x154, 0x7D603828); // lwarx r11,0,r7
        word(separate, 0x158, 0x7CC0612D); // stwcx. r6,0,r12
        word(separate, 0x15C, 0x7C60612D); // stwcx. r3,0,r12
        word(separate, 0x160, 0x7FCD742A); // lswx r30,r13,r14
        word(separate, 0x164, 0x7FCF3DAA); // stswi r30,r15,7
        word(separate, 0x168, 0x7F5005AA); // stswi r26,r16,0 (32 bytes)
        word(separate, 0x16C, 0x7E9004AA); // lswi r20,r16,0 (32 bytes)
        word(separate, 0x240, 0x7C609B6C); // ecowx r3,0,r19
        word(separate, 0x244, 0x7E809A6C); // eciwx r20,0,r19
        word(separate, 0x248, 0x7C1197EC); // dcbz r17,r18
        word(separate, 0x24C, 0x7C1190AC); // dcbf r17,r18
        word(separate, 0x250, 0x7C0004AC); // sync
        word(separate, 0x254, 0x7C0006AC); // eieio
        word(separate, 0x258, 0x7C00AA64); // tlbie r21
        word(separate, 0x25C, 0x7C00046C); // tlbsync
        word(separate, 0x260, 0x38840001); // addi r4,r4,1
        word(separate, 0x264, 0x7EC00124); // mtmsr r22
        word(separate, 0x268, 0x4C000064); // rfi
        word(separate, 0x26C, 0x7ED603A6); // mtdec r22
        word(separate, 0x270, 0x7EFC43A6); // mttbl r23
        word(separate, 0x274, 0x7F12FBA6); // mtiabr r24
        word(separate, 0x280, 0xFC61102B); // fadd. f3,f1,f2
        word(separate, 0x284, 0xFC811025); // fdiv. f4,f1,f2
        word(separate, 0x288, 0xFCA01019); // frsp. f5,f2
        word(separate, 0x28C, (63u << 26) | (6 << 21) | (1 << 16) | (2 << 11) | (2 << 6) | (29 << 1) | 1); // fmadd.
        word(separate, 0x290, (59u << 26) | (7 << 21) | (1 << 16) | (2 << 11) | (2 << 6) | (30 << 1) | 1); // fnmsubs.
        word(separate, 0x294, (59u << 26) | (8 << 21) | (1 << 11) | (24 << 1) | 1);                        // fres.
        word(separate, 0x298, (63u << 26) | (9 << 21) | (1 << 11) | (26 << 1) | 1);                        // frsqrte.
        word(separate, 0x29C, 0x18000000);
        if (!strcmp(argv[1], "--save")) {
            assert(ROMlib_ppc_translate_range(cache, separate.memory + 0x100, 0x100, 24) == 1);
            assert(ROMlib_ppc_translate_range_mode(cache, separate.memory + 0x100, 0x100, 24,
                                                   ROMLIB_PPC_TRANSLATE_750) == 5);
            program(separate, cache, 7);
            assert(ROMlib_ppc_translate_range_mode(cache, separate.memory + 0x100, 0x100, 24,
                                                   ROMLIB_PPC_TRANSLATE_750) == 4);
            assert(ROMlib_ppc_translate_range(cache, separate.memory + 0x120, 0x120, 8) == 1);
            assert(ROMlib_ppc_translate_range_mode(cache, separate.memory + 0x120, 0x120, 8,
                                                   ROMLIB_PPC_TRANSLATE_750) == 2);
            assert(ROMlib_ppc_translate_range_mode(cache, separate.memory + 0x130, 0x130, 64,
                                                   ROMLIB_PPC_TRANSLATE_750) == 16);
            assert(ROMlib_ppc_translate_range_mode(cache, separate.memory + 0x240, 0x240, 56,
                                                   ROMLIB_PPC_TRANSLATE_750) == 14);
            for (unsigned mode : {0u, unsigned(ROMLIB_PPC_TRANSLATE_750)})
                assert(ROMlib_ppc_translate_range_mode(cache, separate.memory + 0x280, 0x280, 28, mode) == 7);
            assert(ROMlib_ppc_translation_cache_save(cache, argv[2]));
        }
        else {
            assert(!strcmp(argv[1], "--load"));
            assert(ROMlib_ppc_translation_cache_load(cache, argv[2]) == 57u);
            run(separate, 69, 5);
            assert(!separate.env.native_compilations && separate.env.native_blocks == 64);
            architectural(separate, 71, 5);
            program(separate, cache, 7);
            architectural(separate, 75, 7);
            run_persisted_operations(separate);
            run_persisted_arithmetic(separate);
            puts("Saved flat and architectural PPC code reused by a new process without compilation");
        }
        ROMlib_ppc_translation_cache_destroy(cache);
        return 0;
    }
    direct_architectural_integers();
    replace_active_semantic_tail();
    fixture first, second;
    program(first, cache, 1);
    run(first, 0, 1);
    assert(first.env.native_compilations == 1);
    program(second, cache, 1);
    run(second, 37, 1);
    assert(!second.env.native_compilations && second.env.native_cache_hits);
    program(second, cache, 2);
    run(second, 93, 2);
    assert(second.env.native_compilations > 0);
    auto flags = second.env.execution_flags;
    second.env.execution_flags = 0;
    auto interpreted = run(second, 93, 2);
    second.env.execution_flags = flags;
    auto translated = run(second, 93, 2);
    assert(!memcmp(&interpreted, &translated, sizeof translated));
    assert(!second.env.native_compilations);
    word(second, 0x114, 0x18000001);
    second.env.service = nested;
    services = 0;
    run(second, 93, 4); /* Two outer loops; inner code uses the same guest PC. */
    assert(services == 2 && !second.env.native_compilations);
    services = 0;
    inner_increment = 7;
    run(second, 93, 4);
    assert(services == 2 && second.env.native_compilations);
    ROMlib_ppc_registers invalid = {};
    invalid.pc = 0xfffffff0;
    assert(!ROMlib_ppc_execute(&invalid, &second.env));
    second.env.service = nullptr;
    word(second, 0x114, 0x18000000);
    run(second, 201, 2);
    assert(!second.env.native_compilations);
    ROMlib_ppc_translation_cache_destroy(cache);
    ROMlib_ppc_translation_cache_destroy(nullptr);
    cache = ROMlib_ppc_translation_cache_create();
    fixture eager;
    program(eager, cache, 3);
    assert(!ROMlib_ppc_translate_range(cache, nullptr, 0x100, 20));
    assert(!ROMlib_ppc_translate_range(cache, eager.memory, 0x101, 20));
    assert(!ROMlib_ppc_translate_range(cache, eager.memory, 0xfffffffc, 20));
    assert(ROMlib_ppc_translate_range(cache, eager.memory + 0x100, 0x100, 24) == 1);
    assert(!ROMlib_ppc_translate_range(cache, eager.memory + 0x100, 0x100, 24));
    run(eager, 19, 3);
    assert(!eager.env.native_compilations && eager.env.native_blocks == 64);
    program(eager, cache, 4);
    assert(ROMlib_ppc_translate_range(cache, eager.memory + 0x100, 0x100, 24) == 1);
    run(eager, 97, 4);
    assert(!eager.env.native_compilations);
    std::string path = std::string(argv[0]) + ".cache", bad = path + ".bad";
    assert(ROMlib_ppc_translation_cache_save(cache, path.c_str()));
    auto original = read_file(path);
    auto loaded = ROMlib_ppc_translation_cache_create();
    assert(ROMlib_ppc_translation_cache_load(loaded, path.c_str()) == 1);
    eager.env.translation_cache = loaded;
    run(eager, 135, 4);
    assert(!eager.env.native_compilations && eager.env.native_blocks == 64);
    for (unsigned i = 0; i < original.size(); ++i) {
        auto bytes = original;
        bytes[i] ^= 1;
        write_file(bad, bytes);
        assert(!ROMlib_ppc_translation_cache_load(loaded, bad.c_str()));
    }
    for (unsigned n = 0; n < original.size(); ++n) {
        std::vector<unsigned char> bytes(original.begin(), original.begin() + n);
        write_file(bad, bytes);
        assert(!ROMlib_ppc_translation_cache_load(loaded, bad.c_str()));
    }
    for (auto offset : {8u, 72u, 76u, 80u, 84u, 88u, 100u, 104u, 128u}) {
        auto bytes = original;
        for (unsigned i = 0; i < 4; ++i)
            bytes[offset + i] = 0xff;
        checksum(bytes);
        write_file(bad, bytes);
        assert(!ROMlib_ppc_translation_cache_load(loaded, bad.c_str()));
    }
    run(eager, 139, 4);
    assert(!eager.env.native_compilations);
    assert(!ROMlib_ppc_translation_cache_save(loaded, ""));
    assert(!ROMlib_ppc_translation_cache_load(loaded, ""));
    ROMlib_ppc_translation_cache_destroy(loaded);
    remove(path.c_str());
    remove(bad.c_str());
    ROMlib_ppc_translation_cache_destroy(cache);
    cache = ROMlib_ppc_translation_cache_create();
    fixture modes;
    program(modes, cache, 3);
    assert(ROMlib_ppc_translate_range(cache, modes.memory + 0x100, 0x100, 24) == 1);
    assert(ROMlib_ppc_translate_range_mode(cache, modes.memory + 0x100, 0x100, 24, ROMLIB_PPC_TRANSLATE_750) == 5);
    assert(!ROMlib_ppc_translate_range_mode(cache, modes.memory + 0x100, 0x100, 24, 2));
    assert(ROMlib_ppc_translation_cache_save(cache, path.c_str()));
    loaded = ROMlib_ppc_translation_cache_create();
    assert(ROMlib_ppc_translation_cache_load(loaded, path.c_str()) == 6);
    modes.env.translation_cache = loaded;
    run(modes, 17, 3);
    assert(!modes.env.native_compilations && modes.env.native_blocks == 64);
    architectural(modes, 19, 3);
    std::unique_ptr<ROMlib_ppc_system750> state(new ROMlib_ppc_system750{});
    state->hid0 = 0x8000;
    ROMlib_ppc_physical_bus bus = {&modes, physical, [](void*, uint32_t, uint32_t, uint32_t*) { return 1; }};
    modes.env.system = state.get();
    modes.env.physical_bus = &bus;
    modes.env.linear_memory = nullptr;
    run(modes, 23, 3);
    assert(!modes.env.native_compilations && modes.env.native_instructions == 320);
    for (unsigned i = 0; i < 4; ++i)
        word(modes, 0x100 + i * 4, 0x38630007);
    run(modes, 29, 3); // Raw RAM changed; the resident I-cache still supplies old instructions.
    assert(!modes.env.native_compilations && modes.env.native_instructions == 320);
    ROMlib_ppc_cache750_invalidate_set(&state->cache[0], 0x100);
    run(modes, 31, 7);
    assert(modes.env.native_compilations == 4 && modes.env.native_instructions > 0);
    for (unsigned i = 0; i < 4; ++i)
        word(modes, 0x100 + i * 4, 0x38630003);
    run(modes, 37, 7); // The newer instruction view is now stale in the same way.
    assert(!modes.env.native_compilations && modes.env.native_instructions == 320);
    ROMlib_ppc_cache750_invalidate_set(&state->cache[0], 0x100);
    run(modes, 41, 3);
    assert(!modes.env.native_compilations && modes.env.native_instructions == 320);
    ROMlib_ppc_translation_cache_destroy(loaded);
    ROMlib_ppc_translation_cache_destroy(cache);
    remove(path.c_str());
    puts("Persistent PPC translations survive CPU replacement, rewrites, nesting and faults");
    test_full_cache();
    puts("A full AOT cache admits hot blocks and refreshes rewritten flat and 750 code");
    return 0;
}
