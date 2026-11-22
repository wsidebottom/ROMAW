/*+===================================================================
  File:      ppc_system_execution_test.cpp

  Summary:   Execute original supervisor/MMU programs through the real 750 instruction loop.

  Classes:   fixture, local packet_fp_fixture; uses ROMlib_ppc_environment and ROMlib_ppc_system750.

  Functions: main, invalid_forms, cache_execution, reservation_snoops, native_execution, thermal_execution, performance_execution,
             cache_performance, l2_execution; optional __wrap_ROMlib_ppc_750_read, __wrap_ROMlib_ppc_750_write,
             __wrap_ROMlib_ppc_750_cache_control, __wrap_ROMlib_ppc_750_external,
             __wrap_ROMlib_ppc_750_load_reserve, __wrap_ROMlib_ppc_750_store_conditional, __wrap_ROMlib_ppc_750_write_spr and
             __wrap_ROMlib_ppc_750_set_msr regression boundaries.

  Origin:    Original instruction encodings and physical memory fixtures; no vendor ROM code.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/
#include "synppc/bus750.h"
#include "../src/ppc-jit.h"
#include "../src/ppc-native-bus.h"
#include <cfenv>
#include <stdexcept>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>
#include <iterator>
#include <memory>

#ifdef SYN_PPC_CHECK_NATIVE_FETCH
extern ROMlib_ppc_environment* romaw_ppc_environment;
static bool native_state_helper_forbidden;
static bool native_data_helper_forbidden;
extern "C" int __real_ROMlib_ppc_750_read(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, unsigned,
                                          unsigned, uint64_t*, ROMlib_ppc_bus_result*);

extern "C" int __wrap_ROMlib_ppc_750_read(ROMlib_ppc_system750* s, const ROMlib_ppc_physical_bus* b, uint32_t a,
                                          unsigned n, unsigned access, uint64_t* value, ROMlib_ppc_bus_result* result)
{
    if ((access & ROMLIB_PPC_MMU_INSTRUCTION) && romaw_ppc_environment && ROMlib_ppc_jit_available())
        assert(!(romaw_ppc_environment->execution_flags & ROMLIB_PPC_EXEC_JIT));
    if (!(access & ROMLIB_PPC_MMU_INSTRUCTION))
        assert(!native_data_helper_forbidden);
    return __real_ROMlib_ppc_750_read(s, b, a, n, access, value, result);
}

extern "C" int __real_ROMlib_ppc_750_write(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, unsigned,
                                           uint64_t, ROMlib_ppc_bus_result*);

extern "C" int __wrap_ROMlib_ppc_750_write(ROMlib_ppc_system750* s, const ROMlib_ppc_physical_bus* b, uint32_t a,
                                           unsigned n, uint64_t value, ROMlib_ppc_bus_result* result)
{
    assert(!native_data_helper_forbidden);
    return __real_ROMlib_ppc_750_write(s, b, a, n, value, result);
}

extern "C" int __real_ROMlib_ppc_750_cache_control(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t,
                                                   unsigned, ROMlib_ppc_bus_result*);

extern "C" int __wrap_ROMlib_ppc_750_cache_control(ROMlib_ppc_system750* s, const ROMlib_ppc_physical_bus* b,
                                                   uint32_t a, unsigned operation, ROMlib_ppc_bus_result* result)
{
    assert(!native_data_helper_forbidden);
    return __real_ROMlib_ppc_750_cache_control(s, b, a, operation, result);
}

extern "C" int __real_ROMlib_ppc_750_external(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, int,
                                              uint32_t*, ROMlib_ppc_bus_result*);

extern "C" int __wrap_ROMlib_ppc_750_external(ROMlib_ppc_system750* s, const ROMlib_ppc_physical_bus* b, uint32_t a,
                                              int write, uint32_t* value, ROMlib_ppc_bus_result* result)
{
    assert(!native_data_helper_forbidden);
    return __real_ROMlib_ppc_750_external(s, b, a, write, value, result);
}

extern "C" int __real_ROMlib_ppc_750_load_reserve(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t,
                                                  uint32_t*, ROMlib_ppc_bus_result*);

extern "C" int __wrap_ROMlib_ppc_750_load_reserve(ROMlib_ppc_system750* s, const ROMlib_ppc_physical_bus* b, uint32_t a,
                                                  uint32_t* value, ROMlib_ppc_bus_result* result)
{
    assert(!native_data_helper_forbidden);
    return __real_ROMlib_ppc_750_load_reserve(s, b, a, value, result);
}

extern "C" int __real_ROMlib_ppc_750_store_conditional(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t,
                                                       uint32_t, ROMlib_ppc_bus_result*);

extern "C" int __wrap_ROMlib_ppc_750_store_conditional(ROMlib_ppc_system750* s, const ROMlib_ppc_physical_bus* b,
                                                       uint32_t a, uint32_t value, ROMlib_ppc_bus_result* result)
{
    assert(!native_data_helper_forbidden);
    return __real_ROMlib_ppc_750_store_conditional(s, b, a, value, result);
}

extern "C" int __real_ROMlib_ppc_750_write_spr(ROMlib_ppc_system750*, unsigned, uint32_t, uint32_t);

extern "C" int __wrap_ROMlib_ppc_750_write_spr(ROMlib_ppc_system750* s, unsigned selector, uint32_t value, uint32_t pc)
{
    assert(!native_state_helper_forbidden);
    return __real_ROMlib_ppc_750_write_spr(s, selector, value, pc);
}

extern "C" void __real_ROMlib_ppc_750_set_msr(ROMlib_ppc_system750*, uint32_t, uint32_t);

extern "C" void __wrap_ROMlib_ppc_750_set_msr(ROMlib_ppc_system750* s, uint32_t value, uint32_t pc)
{
    assert(!native_state_helper_forbidden);
    __real_ROMlib_ppc_750_set_msr(s, value, pc);
}
#endif

struct fixture {
    std::vector<unsigned char> ram = std::vector<unsigned char>(0x100000);
    std::unique_ptr<ROMlib_ppc_system750> state_owner{new ROMlib_ppc_system750{}};
    ROMlib_ppc_system750& state = *state_owner;
    std::unique_ptr<ROMlib_ppc_l2cache750> l2;
    ROMlib_ppc_registers registers = {};
    ROMlib_ppc_physical_bus bus = {this, transfer, history};
    ROMlib_ppc_environment env = {};
    unsigned core_ticks_per_fetch = 0, core_ticks_per_transfer = 0;
    unsigned fetches = 0, tables = 0, updates = 0, data_transfers = 0;
    unsigned controls = 0, last_control = 0;
    uint32_t control_address = 0;
    bool fail_control = false;
    uint64_t bad_data_address = UINT64_MAX;
    uint64_t throwing_data_address = UINT64_MAX;
    unsigned throwing_data_count = 0;
    uint32_t arm_dabr_after_data = 0;
    bool nested_native = false;
    uint64_t snoop_fetch = UINT64_MAX;
    unsigned snoop_type = 2, snoop_global = 0, snoop_count = 0;
    uint32_t snoop_address = 0;
    bool interrupt_reserve = false;
    bool allow_snoop_failure = false, snoop_failed = false;
    uint32_t device_word = 0xCAFEBABE, device_address = 0, device_access = 0, device_wimg = 0;
    bool fail_device = false, fail_history = false;
    uint32_t allocation = 0x20000;

    fixture(unsigned flags)
    {
        env.system = &state;
        env.physical_bus = &bus;
        env.execution_flags = flags;
    }

    void enable_l2(uint32_t control = 0xB9000000)
    {
        l2.reset(new ROMlib_ppc_l2cache750{});
        assert(ROMlib_ppc_750_set_l2(&state, l2.get()));
        assert(ROMlib_ppc_750_set_l2cr(&state, control));
    }

    uint32_t word(uint32_t a) const
    {
        assert(a + 4 <= ram.size());
        return uint32_t(ram[a]) << 24 | uint32_t(ram[a + 1]) << 16 | uint32_t(ram[a + 2]) << 8 | ram[a + 3];
    }

    void word(uint32_t a, uint32_t value)
    {
        assert(a + 4 <= ram.size());
        for (unsigned i = 0; i < 4; ++i)
            ram[a + i] = value >> (24 - 8 * i);
    }

    static int transfer(void* opaque, uint32_t a, void* buffer, unsigned size, unsigned access, unsigned wimg,
                        uint32_t* cause)
    {
        auto& f = *static_cast<fixture*>(opaque);
        ROMlib_ppc_750_advance_core(&f.state, f.core_ticks_per_transfer);
        if (access & ROMLIB_PPC_PHYSICAL_CONTROL) {
            assert(!size && !buffer);
            ++f.controls;
            f.last_control = access >> ROMLIB_PPC_PHYSICAL_RESOURCE_SHIFT;
            f.control_address = a;
            if (f.fail_control) {
                *cause = 0x20000;
                return 0;
            }
            return 1;
        }
        if (access & ROMLIB_PPC_PHYSICAL_EXTERNAL) {
            assert(size == 4);
            ++f.data_transfers;
            f.device_address = a;
            f.device_access = access;
            f.device_wimg = wimg;
            if (f.fail_device) {
                *cause = 0x20000;
                return 0;
            }
            auto* bytes = static_cast<unsigned char*>(buffer);
            if (access & ROMLIB_PPC_MMU_WRITE)
                f.device_word =
                    uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 8 | bytes[3];
            else
                for (unsigned i = 0; i < 4; ++i)
                    bytes[i] = f.device_word >> (24 - i * 8);
            return 1;
        }
        if (a == f.throwing_data_address) {
            ++f.throwing_data_count;
            throw std::bad_alloc();
        }
        if (f.nested_native && a == 0x2000) {
            f.nested_native = false;
            f.word(0x1000, 0x38630001);
            f.word(0x1004, 0x4200FFFC);
            auto child = f.env;
            std::unique_ptr<ROMlib_ppc_system750> child_state(new ROMlib_ppc_system750{});
            child.system = child_state.get();
            child.instruction_limit = 64;
            ROMlib_ppc_registers r = {};
            r.pc = 0x1000;
            r.ctr = 32;
            assert(!ROMlib_ppc_execute(&r, &child) && strstr(child.error, "instruction limit"));
            assert(r.gpr[3] == 32 && child.native_compilations == 2);
        }
        if (a == f.bad_data_address) {
            *cause = 0x20000;
            return 0;
        }
        if (uint64_t(a) + size > f.ram.size())
            return 0;
        if (access & ROMLIB_PPC_MMU_INSTRUCTION) {
            ++f.fetches;
            ROMlib_ppc_750_advance_core(&f.state, f.core_ticks_per_fetch);
        }
        if (access & ROMLIB_PPC_PHYSICAL_TABLE)
            ++f.tables;
        if (!(access & (ROMLIB_PPC_MMU_INSTRUCTION | ROMLIB_PPC_PHYSICAL_TABLE)))
            ++f.data_transfers;
        if (access & ROMLIB_PPC_MMU_WRITE)
            memcpy(&f.ram[a], buffer, size);
        else
            memcpy(buffer, &f.ram[a], size);
        if (f.arm_dabr_after_data && !(access & (ROMLIB_PPC_MMU_INSTRUCTION | ROMLIB_PPC_PHYSICAL_TABLE))) {
            f.state.dabr = f.arm_dabr_after_data;
            f.arm_dabr_after_data = 0;
        }
        if (((access & ROMLIB_PPC_MMU_INSTRUCTION) && a == f.snoop_fetch) ||
            (f.interrupt_reserve && (access & ROMLIB_PPC_PHYSICAL_ATOMIC) && !(access & ROMLIB_PPC_MMU_WRITE))) {
            bool reserve = f.interrupt_reserve && (access & ROMLIB_PPC_PHYSICAL_ATOMIC);
            f.snoop_fetch = UINT64_MAX;
            f.interrupt_reserve = false;
            ++f.snoop_count;
            ROMlib_ppc_bus_result result = {};
            int ok = ROMlib_ppc_750_snoop(&f.state, &f.bus, reserve ? a : f.snoop_address, reserve ? 2 : f.snoop_type,
                                          reserve ? 0 : f.snoop_global, 0, &result);
            f.snoop_failed = !ok;
            assert(ok || f.allow_snoop_failure);
        }
        return 1;
    }

    static int history(void* opaque, uint32_t a, uint32_t mask, uint32_t* cause)
    {
        auto& f = *static_cast<fixture*>(opaque);
        ROMlib_ppc_750_advance_core(&f.state, f.core_ticks_per_transfer);
        if (f.fail_history) {
            *cause = 0x20000;
            return 0;
        }
        if (uint64_t(a) + 8 > f.ram.size())
            return 0;
        f.word(a + 4, f.word(a + 4) | mask);
        ++f.updates;
        return 1;
    }

    static void* allocate(void* opaque, uint32_t size, uint32_t alignment, uint32_t* address)
    {
        auto& f = *static_cast<fixture*>(opaque);
        f.allocation = (f.allocation + alignment - 1) & ~(alignment - 1);
        *address = f.allocation;
        f.allocation += size;
        assert(f.allocation < f.ram.size());
        return &f.ram[*address];
    }

    static int gate(void*, uint32_t pc, uint32_t opcode)
    {
        return pc == 0x1000 && opcode == 0x18000000;
    }

    void run(unsigned limit)
    {
        env.instruction_limit = limit;
        assert(!ROMlib_ppc_execute(&registers, &env));
        if (!strstr(env.error, "instruction limit"))
            fprintf(stderr, "Unexpected CPU exit at %08x (test opcode %08x, MSR %08x): %s\n", registers.pc,
                    word(0x1000), state.mmu.msr, env.error);
        assert(strstr(env.error, "instruction limit"));
        assert(env.instructions == limit);
        assert(env.execution_flags || !env.native_instructions);
    }

    void run_direct_state(unsigned limit)
    {
        assert(env.execution_flags & ROMLIB_PPC_EXEC_JIT);
#ifdef SYN_PPC_CHECK_NATIVE_FETCH
        bool saved = native_state_helper_forbidden;
        native_state_helper_forbidden = true;
#endif
        run(limit);
#ifdef SYN_PPC_CHECK_NATIVE_FETCH
        native_state_helper_forbidden = saved;
#endif
    }

    void run_direct_memory(unsigned limit)
    {
        assert(env.execution_flags & ROMLIB_PPC_EXEC_JIT);
#ifdef SYN_PPC_CHECK_NATIVE_FETCH
        bool saved = native_data_helper_forbidden;
        native_data_helper_forbidden = true;
#endif
        run(limit);
#ifdef SYN_PPC_CHECK_NATIVE_FETCH
        native_data_helper_forbidden = saved;
#endif
    }

    void mapped()
    {
        state.mmu.msr = 0x30;
        state.mmu.ibat[0][0] = 0x80000002;
        state.mmu.ibat[0][1] = 2;
        state.mmu.dbat[0][0] = 0x90000002;
        state.mmu.dbat[0][1] = 0x20002;
        registers.pc = 0x80001000;
        registers.gpr[4] = 0x90000080;
        registers.gpr[5] = 0x90000084;
        word(0x1000, 0x80640000); // lwz r3,0(r4)
        word(0x1004, 0x90650000); // stw r3,0(r5)
        word(0x20080, 0x1234ABCD);
    }

    void floating(uint32_t opcode, uint32_t fpscr, uint64_t a, uint64_t b)
    {
        registers.pc = 0x1000;
        registers.cr = 0xDEADBEEF;
        registers.fpscr = fpscr;
        state.mmu.msr = 0x2000;
        memcpy(&registers.fpr[1], &a, 8);
        memcpy(&registers.fpr[2], &b, 8);
        uint64_t sentinel = UINT64_C(0x123456789ABCDEF0);
        memcpy(&registers.fpr[3], &sentinel, 8);
        word(0x1000, opcode);
    }
};

static uint32_t mtspr(unsigned spr, unsigned r)
{
    return 0x7C0003A6 | r << 21 | (spr & 31) << 16 | (spr & 0x3E0) << 6;
}

static unsigned invalid_forms(unsigned flags)
{
    std::vector<uint32_t> opcodes = {
        0x10000000u, 0x7C6428CEu, 0x7C6429CEu, 0x7C042AACu, // Vector arithmetic, LVX, STVX and DST.
        0xFC60102Cu, 0xEC60102Cu, 0x7C0002E4u,              // FSQRT, FSQRTS and TLBIA.
    };
    for (unsigned xo : {54u, 86u, 246u, 278u, 470u, 598u, 854u, 982u, 1014u})
        for (uint32_t reserved : {1u, 1u << 21})
            opcodes.push_back(31u << 26 | xo << 1 | reserved);
    for (unsigned xo : {598u, 854u})
        opcodes.push_back(31u << 26 | xo << 1 | 4 << 16 | 5 << 11); // Barrier operand fields.
    opcodes.push_back(0x4C00012D);                                  // ISYNC Rc.
    for (unsigned bit = 11; bit < 26; ++bit)
        opcodes.push_back(0x4C00012C | (1u << bit)); // ISYNC reserved XL operand fields.
    for (unsigned primary : {33u, 35u, 41u, 43u, 37u, 39u, 45u, 49u, 51u, 53u, 55u}) {
        opcodes.push_back(primary << 26 | 3 << 21); // Update with rA=0.
        if (primary == 33 || primary == 35 || primary == 41 || primary == 43)
            opcodes.push_back(primary << 26 | 3 << 21 | 3 << 16);
    }
    for (unsigned xo : {23u,  55u,  87u,  119u, 279u, 311u, 343u, 375u, 151u, 183u, 215u, 247u, 407u, 439u, 534u, 790u,
                        662u, 918u, 535u, 567u, 599u, 631u, 663u, 695u, 727u, 759u, 983u, 20u,  597u, 533u, 725u, 661u})
        opcodes.push_back(31u << 26 | 3 << 21 | 4 << 16 | 5 << 11 | xo << 1 | 1); // Reserved Rc.
    opcodes.push_back(31u << 26 | 3 << 21 | 4 << 16 | 5 << 11 | 150 << 1);        // stwcx without Rc.
    for (unsigned xo : {55u, 119u, 311u, 375u, 183u, 247u, 439u, 567u, 631u, 695u, 759u}) {
        opcodes.push_back(31u << 26 | 3 << 21 | 5 << 11 | xo << 1);
        if (xo == 55 || xo == 119 || xo == 311 || xo == 375)
            opcodes.push_back(31u << 26 | 3 << 21 | 3 << 16 | 5 << 11 | xo << 1);
    }
    for (unsigned rd = 0; rd < 32; ++rd) {
        opcodes.push_back(46u << 26 | rd << 21 | rd << 16);
        opcodes.push_back(46u << 26 | rd << 21 | 31 << 16);
    }
    opcodes.push_back(31u << 26 | 3 << 21 | 4 << 16 | 8 << 11 | 597 << 1); // lswi base overwritten.
    opcodes.push_back(31u << 26 | 5 << 21 | 4 << 16 | 5 << 11 | 533 << 1); // lswx index overwritten.
    const uint32_t live_count = 31u << 26 | 3 << 21 | 4 << 16 | 5 << 11 | 533 << 1;
    opcodes.push_back(live_count); // LSWX overlaps its base only if the live XER count exceeds four.
    auto* cache = flags && ROMlib_ppc_jit_available() ? ROMlib_ppc_translation_cache_create() : nullptr;
    assert(cache || !flags || !ROMlib_ppc_jit_available());
    unsigned cases = 0;
    for (uint32_t opcode : opcodes) {
        fixture f(flags);
        f.registers.pc = 0x1000;
        for (unsigned r = 0; r < 32; ++r)
            f.registers.gpr[r] = 0x2000;
        f.registers.cr = 0xDEADBEEF;
        f.registers.xer = 8;
        f.registers.fpr[3] = 123.5;
        auto before = f.registers;
        f.state.mmu.msr = 0x2000;
        f.state.dabr = 0x2003;
        f.word(0x1000, opcode);
        if (cache) {
            f.env.translation_cache = cache;
            unsigned translated =
                ROMlib_ppc_translate_range_mode(cache, &f.ram[0x1000], 0x1000, 4, ROMLIB_PPC_TRANSLATE_750);
            if (translated != unsigned(opcode == live_count))
                fprintf(stderr, "Unexpected 750 AOT admission for %08x: %u blocks\n", opcode, translated);
            assert(translated == unsigned(opcode == live_count));
        }
        f.run(1);
        if (f.registers.pc != 0x700 || f.state.srr1 != 0x82000)
            fprintf(stderr, "Invalid form %08x: PC %08x SRR1 %08x\n", opcode, f.registers.pc, f.state.srr1);
        assert(f.registers.pc == 0x700 && f.state.srr0 == 0x1000 && f.state.srr1 == 0x82000);
        assert(!f.data_transfers && f.registers.cr == before.cr && f.registers.xer == before.xer);
        assert(!memcmp(f.registers.gpr, before.gpr, sizeof before.gpr));
        assert(!memcmp(f.registers.fpr, before.fpr, sizeof before.fpr));
        assert(f.env.native_instructions == unsigned(cache && opcode == live_count) &&
               !f.env.native_kernel_instructions && !f.env.native_compilations);
        ++cases;
        if (opcode == live_count) {
            f.registers.pc = 0x1000;
            f.registers.xer = 4;
            f.state.mmu.msr = 0x2000;
            f.word(0x4000, 0x12345678);
            f.run(1);
            assert(f.registers.pc == 0x1004 && f.registers.gpr[3] == 0x12345678 && f.data_transfers == 1);
            assert(f.env.native_cache_hits == unsigned(cache != nullptr) && !f.env.native_compilations);
            ++cases;
        }
    }
    ROMlib_ppc_translation_cache_destroy(cache);
    {
        fixture f(flags);
        f.registers.pc = 0x1000;
        f.registers.gpr[4] = 0x2000;
        f.registers.gpr[10] = 0x80640000;
        f.registers.gpr[11] = 0x1000;
        f.word(0x1000, 0x84600000); // Invalid lwzu with rA=0.
        f.word(0x700, 0x914B0000);  // Handler installs valid lwz at stopped PC.
        f.word(0x704, 0x4C000064);
        f.word(0x2000, 0x12345678);
        f.run(4);
        assert(f.registers.pc == 0x1004 && f.registers.gpr[3] == 0x12345678);
        assert(f.state.srr0 == 0x1000 && f.state.srr1 == 0x80000 && f.data_transfers == 2);
        ++cases;
    }
    return cases;
}

static unsigned cache_execution(unsigned flags)
{
    unsigned cases = 0;
    for (unsigned xo : {246u, 278u})
        for (unsigned condition = 0; condition < 8; ++condition) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[4] = 0x90000080;
            f.state.mmu.msr = 0x1010;
            f.state.hid0 = 0x4000 | (condition == 1 ? 1 : condition == 2 ? 0x1000 : 0);
            f.state.mmu.sdr1 = 0x200000; // A hint miss must not start a physical table walk.
            f.state.mmu.dbat[0][0] = condition == 6 ? 0 : 0x90000002;
            f.state.mmu.dbat[0][1] = 0x20000 |
                                     (condition == 3   ? 0x20
                                      : condition == 4 ? 8
                                                       : 0) |
                                     (condition == 5 ? 0 : 2);
            if (condition == 7)
                f.bad_data_address = 0x20080;
            f.word(0x1000, 0x7C002000 | xo << 1);
            f.run(1);
            if (condition == 7)
                assert(f.registers.pc == 0x200 && f.state.srr0 == 0x1000 && f.state.srr1 == 0x21010);
            else
                assert(f.registers.pc == 0x1004 && !f.state.srr0);
            assert(!f.tables);
            assert(f.data_transfers == (condition == 0 ? 4u : 0u));
            ++cases;
        }
    {
        fixture f(flags);
        f.registers.pc = 0x1000;
        f.registers.gpr[4] = 0x90000080;
        f.state.mmu.msr = 0x10;
        f.state.hid0 = 0x4000;
        f.state.mmu.sdr1 = 0x10000;
        f.state.mmu.sr[9] = 0x23;
        f.word(0x108C0, 0x80001180);
        f.word(0x108C4, 0x20002);
        f.word(0x20080, 0x12345678);
        ROMlib_ppc_bus_result result = {};
        assert(ROMlib_ppc_750_translate(&f.state, &f.bus, 0x90000080, 0, &result));
        assert(f.tables == 4 && !f.data_transfers);
        f.word(0x1000, 0x7C00222C);
        f.word(0x1004, 0x80640000);
        f.run(2);
        assert(f.registers.gpr[3] == 0x12345678 && f.tables == 4 && f.data_transfers == 4);
        ++cases;
    }
    for (unsigned attributes : {0u, 4u, 8u, 12u})
        for (unsigned protection : {0u, 2u}) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[4] = 0x90000080;
            f.state.mmu.msr = 0x10;
            f.state.hid0 = 0x4000;
            f.state.mmu.dbat[0][0] = 0x90000002;
            f.state.mmu.dbat[0][1] = 0x20000 | attributes << 3 | protection;
            f.word(0x1000, 0x7C0027EC);
            f.run(1);
            assert(f.registers.pc == (attributes ? 0x600u : protection ? 0x1004u : 0x300u));
            assert(!f.controls && !f.data_transfers && !f.tables);
            assert(f.state.cache[1].lines[4][0].state == (!attributes && protection ? 2 : 0));
            ++cases;
        }

    for (bool writing : {false, true}) {
        fixture f(flags);
        f.state.hid0 = 0x4000;
        f.state.mmu.msr = 0x1000;
        f.bad_data_address = 0x2020;
        f.word(0x201C, 0x11223344);
        ROMlib_ppc_bus_result result = {};
        uint64_t value = UINT64_C(0x123456789ABCDEF0);
        int ok = writing ? ROMlib_ppc_750_write(&f.state, &f.bus, 0x201F, 4, 0xAABBCCDD, &result)
                         : ROMlib_ppc_750_read(&f.state, &f.bus, 0x201F, 4, 0, &value, &result);
        assert(!ok && result.completed == 1 && result.mapping.vector == 0x200);
        assert(result.mapping.physical == 0x2020 && result.mapping.address == 0x2020);
        assert(result.mapping.cause == 0x20000 && value == UINT64_C(0x123456789ABCDEF0));
        const auto& line = f.state.cache[1].lines[0][0];
        assert(line.state == (writing ? 2 : 1) && line.data[31] == (writing ? 0xAA : 0x44));
        assert(f.word(0x201C) == 0x11223344);
        f.registers.pc = 0x1000;
        f.registers.gpr[3] = 0xAABBCCDD;
        f.registers.gpr[4] = 0x201F;
        f.word(0x1000, writing ? 0x90640000 : 0x80640000);
        f.run(1);
        assert(f.registers.pc == 0x200 && f.state.srr1 == 0x21000 && f.state.srr0 == 0x1000);
        assert(f.registers.gpr[3] == 0xAABBCCDD);
        ++cases;
    }
    for (bool user : {false, true})
        for (bool write : {false, true})
            for (uint32_t value : {0u, 0xFFFFFFFFu, 0xCC00u, 0xC00u}) {
                fixture f(flags);
                f.registers.pc = 0x1000;
                f.registers.gpr[3] = value;
                f.state.mmu.msr = user ? 0x4000 : 0;
                f.state.hid0 = 0x80000000;
                f.state.cache[0].lines[0][0].state = 1;
                f.state.cache[1].lines[0][0].state = 2;
                f.state.cache[0].plru[0] = f.state.cache[1].plru[0] = 0x7F;
                f.word(0x1000, mtspr(1008, 3) ^ (write ? 0 : 0x100));
                f.run(1);
                if (user) {
                    assert(f.registers.pc == 0x700 && f.state.srr1 == 0x44000);
                    assert(f.state.hid0 == 0x80000000 && f.registers.gpr[3] == value);
                }
                else {
                    assert(f.registers.pc == 0x1004);
                    uint32_t expected = value & 0xFBF1FFED;
                    if ((expected & 0x8800) == 0x8800)
                        expected &= ~0x800u;
                    if ((expected & 0x4400) == 0x4400)
                        expected &= ~0x400u;
                    assert(f.state.hid0 == (write ? expected : 0x80000000));
                    assert(f.registers.gpr[3] == (write ? value : 0x80000000));
                }
                bool flash = !user && write && (value == 0xFFFFFFFF || value == 0xCC00);
                assert(f.state.cache[0].lines[0][0].state == (flash ? 0 : 1));
                assert(f.state.cache[1].lines[0][0].state == (flash ? 0 : 2));
                assert(f.state.cache[0].plru[0] == (flash ? 0 : 0x7F));
                ++cases;
            }
    for (bool little : {false, true}) {
        fixture f(flags);
        f.registers.pc = 0x1000;
        f.registers.gpr[3] = 0x11223344;
        f.registers.gpr[4] = 0x2000;
        f.state.mmu.msr = little;
        f.state.hid0 = 0x4000;
        f.word(0x1000 ^ (little ? 4 : 0), 0x90640000);
        f.word(0x1004 ^ (little ? 4 : 0), 0x80A40000);
        f.word(0x1008 ^ (little ? 4 : 0), 0x7C00206C);
        f.word(0x2000 ^ (little ? 4 : 0), 0xAABBCCDD);
        f.run(2);
        assert(f.registers.gpr[5] == 0x11223344 && f.word(0x2000 ^ (little ? 4 : 0)) == 0xAABBCCDD);
        assert(f.data_transfers == 4);
        f.run(1); // DC BST survives replacement of the CPU object.
        assert(f.word(0x2000 ^ (little ? 4 : 0)) == 0x11223344 && f.data_transfers == 8);
        ++cases;
    }
    {
        fixture f(flags);
        f.registers.pc = 0x1000;
        f.registers.gpr[4] = 0x1000;
        f.state.hid0 = 0x8000;
        f.word(0x1000, 0x38600001);
        f.word(0x1040, 0x7C0027AC);
        f.run(1);
        assert(f.registers.gpr[3] == 1 && f.fetches == 4);
        f.word(0x1000, 0x38600002);
        f.registers.pc = 0x1000;
        f.run(1);
        assert(f.registers.gpr[3] == 1 && f.fetches == 4);
        f.registers.pc = 0x1040;
        f.run(1);
        f.registers.pc = 0x1000;
        f.run(1);
        assert(f.registers.gpr[3] == 2 && f.fetches == 12);
        ++cases;
    }
    {
        fixture f(flags);
        f.registers.pc = 0x1000;
        f.registers.gpr[4] = 0x2024;
        f.state.hid0 = 0x5000;
        memset(f.ram.data() + 0x2020, 0xAA, 32);
        f.word(0x1000, 0x7C0027EC);
        f.word(0x1004, 0x7C00206C);
        f.run(1);
        assert(f.controls == 1 && f.last_control == ROMLIB_PPC_BUS_DCBZ && f.control_address == 0x2020);
        assert(!f.data_transfers && f.word(0x2020) == 0xAAAAAAAA);
        f.run(1);
        for (unsigned i = 0; i < 32; ++i)
            assert(!f.ram[0x2020 + i]);
        assert(f.data_transfers == 4);
        ++cases;
    }
    {
        fixture f(flags);
        f.registers.pc = 0x1000;
        f.registers.gpr[4] = 0x90000080;
        f.registers.gpr[5] = 0x108C0;
        f.state.hid0 = 0x4000;
        f.state.mmu.msr = 0x10;
        f.state.mmu.sdr1 = 0x10000;
        f.state.mmu.sr[9] = 0x23;
        f.state.mmu.dbat[1][0] = 2;
        f.state.mmu.dbat[1][1] = 2;
        f.word(0x108C0, 0x80001180);
        f.word(0x108C4, 0x30002);
        f.word(0x30080, 0x89ABCDEF);
        f.word(0x1000, 0x80640000);
        f.word(0x1004, 0x7C00286C);
        f.word(0x1008, 0x90640004);
        f.word(0x100C, 0x7C00286C);
        f.run(1);
        assert(f.registers.gpr[3] == 0x89ABCDEF && f.word(0x108C4) == 0x30002);
        assert(f.tables == 4 && !f.updates);
        f.run(1);
        assert(f.word(0x108C4) == 0x30102);
        f.run(1);
        assert(!f.word(0x30084) && f.word(0x108C4) == 0x30102);
        assert(f.tables == 4 && !f.updates);
        f.run(1);
        assert(f.word(0x108C4) == 0x30182);
        ++cases;
    }
    {
        fixture f(flags);
        f.registers.pc = 0x1000;
        f.registers.gpr[3] = 0x12345678;
        f.registers.gpr[4] = 0x2000;
        f.state.hid0 = 0x4000;
        f.word(0x2000, 0xAABBCCDD);
        f.word(0x1000, 0x7CC02028);
        f.word(0x1004, 0x7C60212D);
        f.word(0x1008, 0x7C60212D);
        f.run(3);
        assert(f.registers.gpr[6] == 0xAABBCCDD && f.word(0x2000) == 0x12345678);
        assert(!(f.registers.cr & 0x20000000) && f.data_transfers == 5);
        assert(f.state.cache[1].lines[0][0].state == 1);
        ++cases;
    }
    // Literal architectural encodings: DCBST=54 retains a clean copy;
    // DCBF=86 invalidates after copyback. Both act while disabled/locked.
    for (unsigned xo : {54u, 86u})
        for (bool enabled : {false, true})
            for (bool locked : {false, true})
                for (unsigned coherence = 0; coherence < 3; ++coherence)
                    for (bool fail : {false, true}) {
                        fixture f(flags);
                        f.registers.pc = 0x1000;
                        f.registers.gpr[4] = 0x2000;
                        f.state.mmu.msr = 0x1000;
                        f.state.hid0 = (enabled ? 0x4000 : 0) | (locked ? 0x1000 : 0) | 8;
                        auto& line = f.state.cache[1].lines[0][0];
                        line.address = 0x2000;
                        line.state = coherence;
                        memset(line.data, 0xAA, sizeof line.data);
                        f.word(0x2000, 0x11223344);
                        f.bad_data_address = fail ? 0x2000 : UINT64_MAX;
                        f.word(0x1000, 0x7C002000 | xo << 1);
                        f.run(1);
                        bool fault = fail && coherence == 2;
                        assert(f.registers.pc == (fault ? 0x200u : 0x1004u));
                        assert(line.state == (fault ? 2u : xo == 86 ? 0u : coherence ? 1u : 0u));
                        assert(f.word(0x2000) == (coherence == 2 && !fault ? 0xAAAAAAAAu : 0x11223344u));
                        assert(f.last_control == (xo == 54 ? ROMLIB_PPC_BUS_DCBST : ROMLIB_PPC_BUS_DCBF));
                        ++cases;
                    }
    for (unsigned opcode : {54u, 86u, 470u, 598u, 854u})
        for (bool fail : {false, true}) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[4] = 0x2000;
            f.state.hid0 = 8;
            f.state.mmu.msr = 0x1000;
            f.fail_control = fail;
            bool barrier = opcode == 598 || opcode == 854;
            f.word(0x1000, 0x7C000000 | (barrier ? 0 : 4 << 11) | opcode << 1);
            f.run(1);
            assert(f.controls == 1 && f.control_address == (barrier ? 0u : 0x2000u));
            assert(f.registers.pc == (fail ? 0x200u : 0x1004u));
            if (fail)
                assert(f.state.srr1 == 0x21000 && f.state.srr0 == 0x1000);
            assert(!f.data_transfers);
            ++cases;
        }
    return cases;
}

static unsigned reservation_snoops(unsigned flags)
{
    unsigned cases = 0;
    std::unique_ptr<ROMlib_ppc_translation_cache, decltype(&ROMlib_ppc_translation_cache_destroy)> cache(
        flags && ROMlib_ppc_jit_available() ? ROMlib_ppc_translation_cache_create() : nullptr,
        ROMlib_ppc_translation_cache_destroy);
    if (cache) {
        const unsigned char instructions[] = {0x7C, 0x60, 0x20, 0x28, 0x7C, 0xC0, 0x29, 0x2D};
        assert(ROMlib_ppc_translate_range_mode(cache.get(), instructions, 0x1000, sizeof instructions,
                                               ROMLIB_PPC_TRANSLATE_750) == 2);
    }
    {
        fixture f(flags);
        f.state.hid0 = 0x4000;
        f.state.mmu.msr = 0x1000;
        auto& line = f.state.cache[1].lines[8][0];
        line.address = 0x2100;
        line.state = 2;
        memset(line.data, 0xAA, 32);
        f.registers.pc = 0x1000;
        f.word(0x1000, 0x38600007);
        f.word(0x1004, 0x38600008);
        f.word(0x200, 0x38800009);
        f.snoop_fetch = 0x1000;
        f.snoop_global = 1;
        f.snoop_address = f.bad_data_address = 0x2100;
        f.allow_snoop_failure = true;
        f.run(2);
        assert(f.snoop_failed && f.registers.gpr[3] == 7 && f.registers.pc == 0x200);
        assert(f.state.srr0 == 0x1004 && f.state.srr1 == 0x21000);
        assert(!f.state.bus_check_pending && !f.state.bus_check_cause && line.state == 2);
        f.run(1);
        assert(f.registers.pc == 0x204 && f.registers.gpr[4] == 9 && !f.state.checkstop);
        ++cases;
    }

    for (unsigned type : {2u, 6u, 12u, 18u, 10u, 11u, 14u})
        for (bool global : {false, true})
            for (bool cached : {false, true})
                for (bool mapped : {false, true})
                    for (bool little : {false, true})
                        for (bool matching : {false, true}) {
                            fixture f(flags);
                            f.env.translation_cache = cache.get();
                            uint32_t physical = mapped ? 0x20080 : 0x80;
                            uint32_t effective = mapped ? 0x90000080 : 0x80;
                            f.state.mmu.msr = (mapped ? 0x10 : 0) | little;
                            f.state.mmu.dbat[0][0] = 0x90000002;
                            f.state.mmu.dbat[0][1] = 0x20002;
                            f.state.hid0 = cached ? 0x4000 : 0;
                            f.registers.pc = 0x1000;
                            f.registers.gpr[4] = effective;
                            f.registers.gpr[5] = effective + 64; // 750 STWCX need not match the LWARX address.
                            f.registers.gpr[6] = 0xAABBCCDD;
                            f.word(physical ^ (little ? 4 : 0), 0x11223344);
                            f.word(0x1000 ^ (little ? 4 : 0), 0x7C602028);
                            f.word(0x1004 ^ (little ? 4 : 0), 0x7CC0292D);
                            f.snoop_fetch = 0x1004 ^ (little ? 4 : 0);
                            f.snoop_type = type;
                            f.snoop_global = global;
                            f.snoop_address = physical + (matching ? 31 : 96);
                            f.run(2);
                            if (cache)
                                assert(f.env.native_instructions == 2 && f.env.native_cache_hits == 2 &&
                                       !f.env.native_kernel_instructions && !f.env.native_compilations);
                            bool canceled = type == 18 || (matching && (type == 2 || type == 6 || type == 12));
                            assert(f.snoop_count == 1 && f.registers.gpr[3] == 0x11223344);
                            assert(bool(f.registers.cr & 0x20000000) == !canceled && !f.state.reservation_valid);
                            assert(f.state.reservation_address == physical);
                            assert(f.word((physical + 64) ^ (little ? 4 : 0)) == (canceled ? 0 : 0xAABBCCDD));
                            ++cases;
                        }
    for (bool cached : {false, true})
        for (bool cancel : {false, true}) {
            fixture f(flags);
            f.env.translation_cache = cache.get();
            f.state.hid0 = cached ? 0x4000 : 0;
            f.registers.pc = 0x1000;
            f.registers.gpr[4] = 0x2000;
            f.registers.gpr[5] = 0x2040;
            f.registers.gpr[6] = 0xAABBCCDD;
            f.word(0x2000, 0x11223344);
            f.word(0x1000, 0x7C602028);
            f.word(0x1004, 0x7CC0292D);
            f.run(1);
            assert(f.state.reservation_valid && f.state.reservation_address == 0x2000);
            if (cancel) {
                ROMlib_ppc_bus_result result = {};
                assert(ROMlib_ppc_750_snoop(&f.state, &f.bus, 0x201C, 2, 0, 0, &result));
            }
            f.run(1); // Reservation and physical monitor survive CPU replacement.
            if (cache)
                assert(f.env.native_instructions == 1 && f.env.native_cache_hits == 1 &&
                       !f.env.native_kernel_instructions && !f.env.native_compilations);
            assert(bool(f.registers.cr & 0x20000000) == !cancel && !f.state.reservation_valid);
            assert(f.word(0x2040) == (cancel ? 0 : 0xAABBCCDD));
            ++cases;
        }
    for (bool cached : {false, true}) {
        fixture f(flags);
        f.env.translation_cache = cache.get();
        f.state.hid0 = cached ? 0x4000 : 0;
        f.registers.pc = 0x1000;
        f.registers.gpr[4] = 0x2000;
        f.registers.gpr[5] = 0x2040;
        f.registers.gpr[6] = 0xAABBCCDD;
        f.word(0x2000, 0x11223344);
        f.word(0x1000, 0x7C602028);
        f.word(0x1004, 0x7CC0292D);
        f.interrupt_reserve = true;
        f.run(2);
        if (cache)
            assert(f.env.native_instructions == 2 && f.env.native_cache_hits == 2 &&
                   !f.env.native_kernel_instructions && !f.env.native_compilations);
        assert(f.snoop_count == 1 && f.registers.gpr[3] == 0x11223344);
        assert(!(f.registers.cr & 0x20000000) && !f.word(0x2040) && !f.state.reservation_valid);
        ++cases;
    }
    return cases;
}

static unsigned thermal_execution(unsigned flags)
{
    unsigned cases = 0;
    for (unsigned pll = 0; pll < 16; ++pll)
        for (unsigned number : {287u, 1009u})
            for (bool write : {false, true})
                for (bool user : {false, true})
                    for (uint32_t value : {0u, 0x12345678u, 0xFFFFFFFFu}) {
                        fixture f(flags);
                        assert(ROMlib_ppc_750_set_pll(&f.state, pll));
                        f.registers.pc = 0x1000;
                        f.registers.gpr[3] = value;
                        f.state.mmu.msr = user ? 0x4000 : 0;
                        f.word(0x1000, mtspr(number, 3) ^ (write ? 0 : 0x100));
                        f.run(1);
                        assert(f.state.hid1 == uint32_t(pll) << 28);
                        if (user)
                            assert(f.registers.pc == 0x700 && f.state.srr1 == 0x44000 && f.registers.gpr[3] == value);
                        else {
                            assert(f.registers.pc == 0x1004 && !f.state.srr0);
                            assert(f.registers.gpr[3] == (write           ? value
                                                          : number == 287 ? 0x80000
                                                                          : uint32_t(pll) << 28));
                        }
                        ++cases;
                    }
    for (unsigned number : {1020u, 1021u, 1022u})
        for (bool write : {false, true})
            for (bool user : {false, true})
                for (uint32_t value : {0u, 0xFFFFFFFFu, 0x20800007u}) {
                    fixture f(flags);
                    f.registers.pc = 0x1000;
                    f.registers.gpr[3] = value;
                    f.state.mmu.msr = user ? 0x4000 : 0;
                    f.state.thrm[0] = f.state.thrm[1] = 0xC0000000;
                    f.state.thrm[2] = 0x22000005;
                    f.word(0x1000, mtspr(number, 3) ^ (write ? 0 : 0x100));
                    f.run(1);
                    if (user)
                        assert(f.registers.pc == 0x700 && f.registers.gpr[3] == value);
                    else {
                        assert(f.registers.pc == 0x1004);
                        if (write)
                            assert(f.state.thrm[number - 1020] ==
                                   (number == 1022 ? value & 0x3E003FFF : 0x80000000 | (value & 0x3F800007)));
                        else
                            assert(f.registers.gpr[3] == (number == 1022 ? 0x22000005 : 0xC0000000));
                    }
                    ++cases;
                }
    {
        bool native = flags & ROMLIB_PPC_EXEC_JIT;
        fixture f(flags);
        f.state.mmu.msr = 0x8000;
        f.state.decrementer = 0x7FFFFFFF;
        ROMlib_ppc_750_set_temperature(&f.state, 65);
        ROMlib_ppc_750_set_thermal(&f.state, 0, 60u << 23 | 3);
        ROMlib_ppc_750_set_thermal(&f.state, 2, 41); // Twenty core cycles, during a warmed branch loop.
        f.core_ticks_per_fetch = 1;
        f.registers.pc = 0x1000;
        f.word(0x1000, 0x38630001);
        f.word(0x1004, 0x4BFFFFFC);
        f.word(0x1700, mtspr(1020, 6) ^ 0x100); // Handler reads status, disables the TAU and returns.
        f.word(0x1704, mtspr(1022, 7));
        f.word(0x1708, 0x4C000064);
        f.run(28);
        assert(f.state.srr0 == 0x1000 && f.state.srr1 == 0x8000 && !f.state.thrm[2] && !f.state.thermal_pending);
        assert((f.registers.gpr[6] & 0xC0000000) == 0xC0000000 && f.registers.pc == 0x1000);
        if (native)
            assert(f.env.native_instructions > 0);
        ++cases;
    }
    return cases;
}

static unsigned performance_execution(unsigned flags)
{
    unsigned cases = 0;
    for (unsigned alias : {0u, 16u})
        for (unsigned number = 952; number <= 958; ++number)
            for (bool user : {false, true})
                for (bool write : {false, true}) {
                    fixture f(flags);
                    f.state.mmcr0 = 0x80000000;
                    f.state.mmcr1 = 0xFFC00000;
                    f.state.sia = 0x12345000;
                    for (unsigned i = 0; i < 4; ++i)
                        f.state.pmc[i] = 0x60000000 + i;
                    uint32_t old = number == 952   ? f.state.mmcr0
                                   : number == 956 ? f.state.mmcr1
                                   : number == 955 ? f.state.sia
                                                   : f.state.pmc[number < 955 ? number - 953 : number - 955];
                    f.registers.pc = 0x1000;
                    f.registers.gpr[3] = 0x80000000;
                    f.state.mmu.msr = user ? 0x4000 : 0;
                    f.word(0x1000, mtspr(number - alias, 3) ^ (write ? 0 : 0x100));
                    f.run(1);
                    bool privileged = user && !alias, illegal = alias && write;
                    if (privileged || illegal) {
                        assert(f.registers.pc == 0x700 && f.state.srr0 == 0x1000);
                        assert(f.state.srr1 == ((user ? 0x4000u : 0u) | (privileged ? 0x40000u : 0x80000u)));
                    }
                    else {
                        assert(f.registers.pc == 0x1004 && f.registers.gpr[3] == (write ? 0x80000000u : old));
                        if (write) {
                            uint32_t value = number == 952   ? f.state.mmcr0
                                             : number == 956 ? f.state.mmcr1
                                             : number == 955 ? f.state.sia
                                                             : f.state.pmc[number < 955 ? number - 953 : number - 955];
                            assert(value == 0x80000000);
                        }
                    }
                    ++cases;
                }
    for (uint32_t opcode : {0x38630001u, 0x80640000u, 0x44000002u, 0x00000000u, 0x4C000064u}) {
        fixture f(flags);
        f.registers.pc = 0x1000;
        f.registers.gpr[4] = 0x100000; // Unmapped load has no completed event.
        f.state.mmu.msr = opcode == 0x80640000 ? 0x10 : 0;
        f.state.mmcr0 = 8 << 6 | 4;           // Fetch / dispatch.
        f.state.mmcr1 = 2u << 27 | 13u << 22; // Complete / integer.
        f.word(0x1000, opcode);
        f.run(1);
        if (f.state.pmc[0] != 1 || f.state.pmc[1] != unsigned(opcode != 0))
            fprintf(stderr, "event opcode %08x: fetch %u dispatch %u complete %u\n", opcode, f.state.pmc[0],
                    f.state.pmc[1], f.state.pmc[2]);
        assert(f.state.pmc[0] == 1 && f.state.pmc[1] == unsigned(opcode != 0));
        assert(f.state.pmc[2] == unsigned(opcode != 0 && opcode != 0x80640000));
        assert(f.state.pmc[3] == unsigned(opcode == 0x38630001));
        ++cases;
    }
    for (uint32_t opcode :
         {0x4C000382u, 0x7C600026u, 0x7C6FF120u, 0x7C0004ACu, 0x7C0000ACu, 0x7C000264u, 0x7C00046Cu, 0xFC000090u}) {
        fixture f(flags);
        f.registers.pc = 0x1000;
        f.state.mmu.msr = 0x2000;
        f.state.mmcr0 = 14;                    // SRU completed.
        f.state.mmcr1 = 11u << 27 | 13u << 22; // FPU / integer completed.
        f.word(0x1000, opcode);
        f.run(1);
        assert(f.state.pmc[1] ==
               unsigned(opcode == 0x4C000382 || opcode == 0x7C600026 || opcode == 0x7C6FF120 || opcode == 0x7C0004AC));
        assert(f.state.pmc[2] == unsigned(opcode == 0xFC000090) && !f.state.pmc[3]);
        ++cases;
    }
    for (unsigned bo = 0; bo < 32; ++bo)
        for (unsigned bit : {0u, 1u})
            for (unsigned ctr : {0u, 1u, 2u})
                for (unsigned form : {0u, 1u, 2u}) {
                    if (form == 2 && !(bo & 4))
                        continue; // BCCTR cannot decrement its target CTR.
                    fixture f(flags);
                    f.registers.pc = 0x1000;
                    f.registers.cr = bit << 31;
                    f.registers.ctr = ctr;
                    f.registers.lr = 0x1004;
                    f.state.mmcr0 = 8;        // Branches not taken.
                    f.state.mmcr1 = 8u << 27; // Branches taken.
                    uint32_t opcode = form == 0 ? 0x40000004 : form == 1 ? 0x4C000020 : 0x4C000420;
                    f.word(0x1000, opcode | bo << 21);
                    f.run(1);
                    bool taken =
                        ((bo & 4) || ((ctr != 1) != bool(bo & 2))) && ((bo & 16) || (bool(bit) == bool(bo & 8)));
                    assert(f.state.pmc[1] == unsigned(!taken) && f.state.pmc[2] == unsigned(taken));
                    ++cases;
                }
    {
        fixture f(flags);
        f.state.mmcr0 = 2 << 6 | 8;
        f.state.mmcr1 = 8u << 27 | 13u << 22;
        f.registers.pc = 0x1000;
        f.word(0x1000, 0x38630001);
        f.word(0x1004, 0x4BFFFFFC);
        f.run(100);
        assert(f.state.pmc[0] == 100 && !f.state.pmc[1] && f.state.pmc[2] == 50 && f.state.pmc[3] == 50);
        if (flags)
            assert(f.env.native_instructions > 0);
        ++cases;
    }
    {
        fixture f(flags);
        f.state.mmcr0 = 0x06008080; // Completed instructions; freeze at overflow and signal.
        f.state.pmc[0] = 0x7FFFFFFE;
        f.state.mmu.msr = 0x8000;
        f.state.decrementer = 0x7FFFFFFF;
        f.registers.pc = 0x1000;
        f.word(0x1000, 0x38630001);
        f.word(0x1004, 0x38630001);
        f.word(0xF00, mtspr(955, 5) ^ 0x100); // SIA identifies the overflowing instruction.
        f.word(0xF04, 0x4C000064);
        f.run(5);
        assert(f.state.srr0 == 0x1008 && f.registers.pc == 0x1008 && f.registers.gpr[5] == 0x1004);
        assert(f.state.pmc[0] == 0x80000000 && !f.state.performance_pending);
        ++cases;
    }
    return cases;
}

static unsigned cache_performance(unsigned flags)
{
    unsigned cases = 0;
    for (bool enabled : {false, true})
        for (bool locked : {false, true})
            for (bool writing : {false, true})
                for (unsigned wimg = 0; wimg < 16; ++wimg) {
                    fixture f(flags);
                    auto& s = f.state;
                    s.hid0 = (enabled ? 0xC000 : 0) | (locked ? 0x3000 : 0);
                    s.mmu.msr = 0x10;
                    s.mmu.dbat[0][0] = 0x90000002;
                    s.mmu.dbat[0][1] = wimg << 3 | 2;
                    s.mmcr0 = 5;        // L1I misses.
                    s.mmcr1 = 5u << 27; // L1D misses.
                    s.performance_current_pc = 0x1000;
                    ROMlib_ppc_bus_result result = {};
                    uint64_t value = 42;
                    for (unsigned i = 0; i < 3; ++i)
                        assert(writing ? ROMlib_ppc_750_write(&s, &f.bus, 0x90000080, 4, value, &result)
                                       : ROMlib_ppc_750_read(&s, &f.bus, 0x90000080, 4, 0, &value, &result));
                    unsigned misses = enabled && !(wimg & 4) ? locked || (writing && (wimg & 8)) ? 3 : 1 : 0;
                    assert(s.pmc[2] == misses && !s.pmc[1]);
                    for (unsigned i = 0; i < 3; ++i)
                        assert(ROMlib_ppc_750_read(&s, &f.bus, 0x1000, 4, ROMLIB_PPC_MMU_INSTRUCTION, &value, &result));
                    assert(s.pmc[1] == (enabled ? locked ? 3u : 1u : 0u) && s.pmc[2] == misses);
                    ++cases;
                }
    for (bool instruction : {false, true})
        for (bool missing : {false, true})
            for (bool fail : {false, true}) {
                fixture f(flags);
                auto& s = f.state;
                s.mmu.msr = 0x30;
                s.mmu.sdr1 = 0x10000;
                s.mmcr0 = 6;        // ITLB misses.
                s.mmcr1 = 6u << 27; // DTLB misses.
                f.word(0x10040, missing ? 0 : 0x80000000);
                f.word(0x10044, 0x5002);
                f.bad_data_address = fail ? 0x10040 : UINT64_MAX;
                ROMlib_ppc_bus_result result = {};
                uint64_t value = 0;
                unsigned access = instruction ? ROMLIB_PPC_MMU_INSTRUCTION : 0;
                assert(bool(ROMlib_ppc_750_read(&s, &f.bus, 0x1000, 4, access, &value, &result)) ==
                       (!missing && !fail));
                assert(s.pmc[instruction ? 1 : 2] == 1 && !s.pmc[instruction ? 2 : 1]);
                if (!missing && !fail) {
                    assert(ROMlib_ppc_750_read(&s, &f.bus, 0x1000, 4, access, &value, &result));
                    assert(result.mapping.tlb_hit && s.pmc[instruction ? 1 : 2] == 1);
                    if (!instruction) {
                        // A TLB hit with C=0 still starts the complete store table search.
                        assert(ROMlib_ppc_750_write(&s, &f.bus, 0x1000, 4, 42, &result));
                        assert(s.pmc[2] == 2);
                        assert(ROMlib_ppc_750_write(&s, &f.bus, 0x1000, 4, 42, &result));
                        assert(s.pmc[2] == 2);
                    }
                    ROMlib_ppc_mmu_tlbie(&s.mmu, 0x1000);
                    assert(ROMlib_ppc_750_read(&s, &f.bus, 0x1000, 4, access, &value, &result));
                    assert(s.pmc[instruction ? 1 : 2] == (instruction ? 2u : 3u));
                }
                ++cases;
            }
    // A miss overflow samples the effective instruction PC, not the bus address.
    {
        fixture f(flags);
        auto& s = f.state;
        s.hid0 = 0x4000;
        s.mmcr0 = 0x04004000;
        s.mmcr1 = 5u << 27;
        s.pmc[2] = 0x7FFFFFFF;
        s.performance_current_pc = 0x90001000;
        ROMlib_ppc_bus_result result = {};
        uint64_t value = 0;
        assert(ROMlib_ppc_750_read(&s, &f.bus, 0x80, 4, 0, &value, &result));
        assert(s.performance_pending && s.sia == 0x90001000 && s.pmc[2] == 0x80000000);
        ++cases;
    }
    for (unsigned threshold = 0; threshold < 64; ++threshold)
        for (unsigned cycles : {0u, 1u, 4u, 16u}) {
            fixture f(flags);
            auto& s = f.state;
            s.hid0 = 0x4000;
            s.mmcr0 = threshold << 16 | 10 << 6;
            s.mmcr1 = 15u << 27; // Cycles generated by L1 load misses.
            s.core_cycles = UINT64_MAX - 3;
            f.core_ticks_per_transfer = cycles;
            ROMlib_ppc_bus_result result = {};
            uint64_t value = 0;
            assert(ROMlib_ppc_750_read(&s, &f.bus, 0x80, 4, 0, &value, &result));
            assert(s.pmc[0] == unsigned(4 * cycles > threshold) && s.pmc[2] == 4 * cycles);
            assert(ROMlib_ppc_750_read(&s, &f.bus, 0x80, 4, 0, &value, &result));
            assert(s.pmc[0] == unsigned(4 * cycles > threshold) && s.pmc[2] == 4 * cycles);
            ++cases;
        }
    for (bool instruction : {false, true}) {
        fixture f(flags);
        auto& s = f.state;
        s.mmu.msr = 0x30;
        s.mmu.sdr1 = 0x10000;
        s.mmcr0 = 6 << 6;   // ITLB table-search cycles.
        s.mmcr1 = 6u << 22; // DTLB table-search cycles.
        f.word(0x10040, 0x80000000);
        f.word(0x10044, 0x5002);
        f.core_ticks_per_transfer = 3;
        ROMlib_ppc_bus_result result = {};
        uint64_t value = 0;
        unsigned access = instruction ? ROMLIB_PPC_MMU_INSTRUCTION : 0;
        assert(ROMlib_ppc_750_read(&s, &f.bus, 0x1000, 4, access, &value, &result));
        assert(s.pmc[instruction ? 0 : 3] == 6 && !s.pmc[instruction ? 3 : 0]); // PTE plus R history.
        assert(ROMlib_ppc_750_read(&s, &f.bus, 0x1000, 4, access, &value, &result));
        assert(s.pmc[instruction ? 0 : 3] == 6); // TLB hit does not count operand latency as table time.
        ++cases;
    }
    {
        fixture f(flags);
        auto& s = f.state;
        s.hid0 = 0x4000;
        s.mmcr0 = 13; // Dirty L1 replacement castouts, excluding explicit copyback.
        ROMlib_ppc_bus_result result = {};
        for (unsigned i = 0; i < 8; ++i)
            assert(ROMlib_ppc_750_write(&s, &f.bus, 0x80 + i * 4096, 4, i + 1, &result));
        assert(!s.pmc[1]);
        assert(ROMlib_ppc_750_write(&s, &f.bus, 0x80 + 8 * 4096, 4, 9, &result));
        assert(s.pmc[1] == 1);
        assert(ROMlib_ppc_750_cache_control(&s, &f.bus, 0x80 + 8 * 4096, 86, &result));
        assert(s.pmc[1] == 1);
        ++cases;
    }
    {
        fixture f(flags);
        f.state.hid0 = 0xC000;
        f.state.mmcr0 = 8 << 6 | 5;
        f.state.mmcr1 = 5u << 27 | 13u << 22;
        f.registers.pc = 0x1000;
        f.registers.gpr[4] = 0x2000;
        f.word(0x1000, 0x80640000);
        f.word(0x1004, 0x90640004);
        f.word(0x1008, 0x38A50001);
        f.word(0x100C, 0x4BFFFFF4);
        f.run(100);
        assert(f.state.pmc[0] == 100 && f.state.pmc[1] == 1 && f.state.pmc[2] == 1 && f.state.pmc[3] == 25);
        if (flags)
            assert(f.env.native_instructions > 0);
        ++cases;
    }
    for (unsigned operation : {0u, 278u, 1014u}) {
        fixture f(flags);
        auto& s = f.state;
        s.hid0 = 0x4000;
        s.mmu.msr = 0x10;
        s.mmu.dbat[0][0] = 0x90000002;
        s.mmu.dbat[0][1] = 2;
        s.mmcr0 = 0x06008040; // Cycle overflow freezes all counters inside the fill callback.
        s.mmcr1 = 5u << 27;
        s.pmc[0] = 0x7FFFFFFF;
        f.core_ticks_per_transfer = 1;
        ROMlib_ppc_bus_result result = {};
        uint64_t value = 0;
        assert(operation ? ROMlib_ppc_750_cache_control(&s, &f.bus, 0x90000080, operation, &result)
                         : ROMlib_ppc_750_read(&s, &f.bus, 0x90000080, 4, 0, &value, &result));
        assert(s.pmc[2] == 1);                                      // Lookup precedes the interrupting bus callback.
        assert(bool(s.performance_pending) == (operation != 1014)); // ZERO needs no fill transfer.
        ++cases;
    }
    return cases;
}

static unsigned l2_execution(unsigned flags)
{
    unsigned cases = 0;
    for (bool paged : {false, true})
        for (bool cached : {false, true}) {
            fixture f(flags);
            f.enable_l2(0x80000000); // Enabled without an SRAM size or clock.
            f.state.hid0 = cached ? 0xC000 : 0;
            f.state.mmu.msr = paged ? 0x10 : 0;
            f.state.mmu.sdr1 = 0x10000;
            uint64_t value = 0xDEADBEEF;
            ROMlib_ppc_bus_result result = {};
            assert(!ROMlib_ppc_750_read(&f.state, &f.bus, 0x1000, 4, 0, &value, &result));
            assert(result.mapping.fault == ROMLIB_PPC_MMU_CONFIGURATION && !result.mapping.vector &&
                   !result.mapping.cause && value == 0xDEADBEEF && bool(result.table) == paged);
            if (paged) {
                assert(!ROMlib_ppc_750_translate(&f.state, &f.bus, 0x1000, 0, &result));
                assert(result.mapping.fault == ROMLIB_PPC_MMU_CONFIGURATION && !result.mapping.vector && result.table);
            }
            ++cases;
        }
    for (bool machine_check : {false, true}) {
        fixture f(flags);
        f.enable_l2(0xF9000000);
        unsigned char line[32] = {0x38, 0x60, 0, 1}; // li r3,1
        ROMlib_ppc_l2cache750_result result = {};
        assert(ROMlib_ppc_l2cache750_transfer(f.l2.get(), &f.bus, 1048576, 0, line, 32, 0, ROMLIB_PPC_MMU_WRITE, 0,
                                              ROMLIB_PPC_L2CACHE750_ENABLE | ROMLIB_PPC_L2CACHE750_PARITY, &result));
        f.l2->parity[0] ^= 1;
        f.registers.pc = 0;
        f.state.mmu.msr = machine_check ? 0x1000 : 0;
        if (machine_check) {
            f.run(1);
            assert(f.registers.pc == 0x200 && f.state.srr0 == 0 && f.state.srr1 == 0x101000);
        }
        else
            assert(!ROMlib_ppc_execute(&f.registers, &f.env) && f.state.checkstop);
        assert(!f.registers.gpr[3]);
        ++cases;
    }
    for (unsigned size = 1; size < 4; ++size) {
        uint32_t control = 0x89000000 | size << 28;
        {
            fixture f(flags);
            f.enable_l2(control);
            f.state.hid0 = 0x4000;
            f.state.mmcr0 = 7 << 6 | 7;
            f.state.mmcr1 = 7u << 27 | 5u << 22;
            f.word(0x2000, 0x11223344);
            ROMlib_ppc_bus_result result = {};
            for (unsigned i = 0; i < 9; ++i)
                assert(ROMlib_ppc_750_write(&f.state, &f.bus, 0x2000 + i * 4096, 4, 0xAABB0000 | i, &result));
            assert(f.word(0x2000) == 0x11223344); // The L1 castout is dirty in L2, not in RAM.
            uint64_t value = 0;
            assert(ROMlib_ppc_750_read(&f.state, &f.bus, 0x2000, 4, 0, &value, &result));
            assert(value == 0xAABB0000 && f.word(0x2000) == 0x11223344);
            assert(f.state.pmc[0] == 3 && !f.state.pmc[1] && f.state.pmc[2] == 9 && !f.state.pmc[3]);
            uint32_t counters[4];
            memcpy(counters, f.state.pmc, sizeof counters);
            assert(ROMlib_ppc_750_host_sync(&f.state, &f.bus, &result));
            for (unsigned i = 0; i < 9; ++i)
                assert(f.word(0x2000 + i * 4096) == (0xAABB0000 | i));
            assert(!memcmp(counters, f.state.pmc, sizeof counters));
            for (const auto& set : f.l2->tags)
                for (const auto& tag : set)
                    for (auto state : tag.state)
                        assert(!state);
            ++cases;
        }
        {
            fixture f(flags);
            f.enable_l2(control);
            f.state.hid0 = 0xC000;
            f.state.mmcr0 = 7 << 6 | 7;
            f.state.mmcr1 = 7u << 27;
            f.registers.pc = 0x1000;
            f.registers.gpr[4] = 0x2000;
            f.word(0x1000, 0x80640000);
            f.word(0x1004, 0x90640004);
            f.word(0x1008, 0x38A50001);
            f.word(0x100C, 0x4BFFFFF4);
            auto* translations = ROMlib_ppc_translation_cache_create();
            assert(translations);
            f.env.translation_cache = translations;
            assert(ROMlib_ppc_translate_range_mode(translations, &f.ram[0x1000], 0x1000, 16,
                                                   ROMLIB_PPC_TRANSLATE_750) == 4);
            f.run(100);
            if (flags)
                assert(f.env.native_instructions == 100 && !f.env.native_compilations);
            assert(f.state.pmc[0] == 0 && f.state.pmc[1] == 1 && f.state.pmc[2] == 1);
            unsigned fetches = f.fetches;
            ROMlib_ppc_750_set_hid0(&f.state, 0xC800);
            f.run(4);
            assert(f.fetches == fetches && f.state.pmc[0] == 1 && f.state.pmc[1] == 1);
            if (flags)
                assert(f.env.native_instructions == 4 && !f.env.native_compilations);
            ROMlib_ppc_translation_cache_destroy(translations);
            ++cases;
        }
        for (bool user : {false, true})
            for (bool writing : {false, true}) {
                fixture f(flags);
                f.enable_l2(control);
                f.registers.pc = 0x1000;
                f.registers.gpr[3] = 0x39000001; // IP is read-only, including software attempts to set it.
                f.state.mmu.msr = user ? 0x4000 : 0;
                f.word(0x1000, mtspr(1017, 3) ^ (writing ? 0 : 0x100));
                f.run(1);
                if (user)
                    assert(f.registers.pc == 0x700 && f.state.srr1 == 0x44000 && f.state.l2cr == control);
                else if (writing)
                    assert(f.registers.pc == 0x1004 && f.state.l2cr == 0x39000000);
                else
                    assert(f.registers.pc == 0x1004 && f.registers.gpr[3] == control);
                ++cases;
            }
        {
            fixture f(flags);
            f.enable_l2(control & ~0x80000000u);
            f.l2->tags[0][0].address = 0x2000;
            f.l2->tags[0][0].state[0] = 2;
            f.l2->data[0] = 0xAA;
            assert(ROMlib_ppc_750_set_l2cr(&f.state, (control & ~0x80000000u) | 0x00200000));
            assert(f.state.l2cr & 1);
            ROMlib_ppc_750_advance_core(&f.state, 32767);
            assert((f.state.l2cr & 1) && f.l2->tags[0][0].state[0] == 2);
            ROMlib_ppc_750_advance_core(&f.state, 1);
            assert(!(f.state.l2cr & 1) && (f.state.l2cr & 0x00200000) && !f.l2->tags[0][0].state[0]);
            assert(f.l2->data[0] == 0xAA);
            assert(ROMlib_ppc_750_set_l2cr(&f.state, control));
            assert(!ROMlib_ppc_750_set_l2(&f.state, nullptr));
            ++cases;
        }
        for (unsigned l1 : {0u, 0x4000u, 0x5000u})
            for (bool wt : {false, true})
                for (bool writing : {false, true}) {
                    fixture f(flags);
                    f.enable_l2(control | (wt ? 0x80000 : 0));
                    f.state.hid0 = l1;
                    f.state.mmu.msr = 0x10;
                    f.state.mmu.sdr1 = 0x10000;
                    f.word(0x10040, 0x80000000);
                    f.word(0x10044, 0x5002);
                    unsigned char bytes[32];
                    ROMlib_ppc_l2cache750_result lower = {};
                    assert(ROMlib_ppc_l2cache750_transfer(f.l2.get(), &f.bus, 131072u << size, 0x10040, bytes, 32, 0, 0,
                                                          2, wt ? 5 : 1, &lower));
                    ROMlib_ppc_bus_result result = {};
                    assert(ROMlib_ppc_750_translate(&f.state, &f.bus, 0x1000, writing ? 1 : 0, &result));
                    uint32_t expected = writing ? 0x5182 : 0x5102;
                    assert(f.word(0x10044) == (wt && l1 != 0x4000 ? expected : 0x5002));
                    assert(ROMlib_ppc_750_host_sync(&f.state, &f.bus, &result));
                    assert(f.word(0x10044) == expected);
                    ++cases;
                }
    }
    {
        fixture f(flags);
        f.enable_l2(0x99000000);
        f.state.hid0 = 0x4000;
        f.state.mmcr0 = 0x06008040;
        f.state.mmcr1 = 7u << 27;
        f.state.pmc[0] = 0x7FFFFFFF;
        f.core_ticks_per_transfer = 1;
        ROMlib_ppc_bus_result result = {};
        uint64_t value = 0;
        assert(ROMlib_ppc_750_read(&f.state, &f.bus, 0x2000, 4, 0, &value, &result));
        assert(f.state.performance_pending && f.state.pmc[2] == 1); // L2 miss precedes the interrupting fill beat.
        ++cases;
    }
    {
        fixture f(flags);
        f.enable_l2(0x99000000);
        unsigned char bytes[32];
        memset(bytes, 0xAA, sizeof bytes);
        ROMlib_ppc_l2cache750_result lower = {};
        assert(ROMlib_ppc_l2cache750_transfer(f.l2.get(), &f.bus, 262144, 0x2000, bytes, 32, 0, 1, 0, 1, &lower));
        f.bad_data_address = 0x2000;
        ROMlib_ppc_bus_result result = {};
        assert(!ROMlib_ppc_750_host_sync(&f.state, &f.bus, &result));
        assert(f.state.bus_check_pending && f.state.bus_check_cause == 0x20000 && result.mapping.physical == 0x2000);
        f.bad_data_address = UINT64_MAX;
        assert(ROMlib_ppc_750_host_sync(&f.state, &f.bus, &result));
        assert(f.word(0x2000) == 0xAAAAAAAA && f.state.bus_check_pending);
        ++cases;
    }
    {
        fixture f(flags);
        f.enable_l2(0x99080000);
        f.state.mmu.msr = 0x10;
        f.state.mmu.sdr1 = 0x10000;
        f.word(0x10040, 0x80000000);
        f.word(0x10044, 0x5002);
        unsigned char bytes[32];
        ROMlib_ppc_l2cache750_result lower = {};
        assert(ROMlib_ppc_l2cache750_transfer(f.l2.get(), &f.bus, 262144, 0x10040, bytes, 32, 0, 0, 2, 5, &lower));
        f.fail_history = true;
        ROMlib_ppc_bus_result result = {};
        uint64_t value = 0xDEADBEEF;
        assert(!ROMlib_ppc_750_read(&f.state, &f.bus, 0x1000, 4, 0, &value, &result));
        assert(value == 0xDEADBEEF && result.table && result.mapping.physical == 0x10044 &&
               result.mapping.cause == 0x20000);
        assert(f.word(0x10044) == 0x5002);
        ++cases;
    }
    // L1 dirty data wins over L2 on external interventions. Disabled L1
    // tags are ignored; uncached reads retain coherent exclusive copies.
    for (unsigned type : {2u, 6u, 10u, 11u})
        for (bool inhibited : {false, true})
            for (bool enabled_l1 : {false, true})
                for (unsigned first = 0; first < 3; ++first)
                    for (unsigned second = 0; second < 3; ++second)
                        for (bool test : {false, true}) {
                            fixture f(flags);
                            f.enable_l2(0x99000000 | (test ? 0x40000 : 0));
                            f.state.hid0 = enabled_l1 ? 0x4000 : 0;
                            memset(f.ram.data() + 0x2000, 0x11, 32);
                            unsigned char bytes[32];
                            memset(bytes, 0xBB, sizeof bytes);
                            ROMlib_ppc_l2cache750_result lower = {};
                            if (second)
                                assert(ROMlib_ppc_l2cache750_transfer(f.l2.get(), &f.bus, 262144, 0x2000, bytes, 32, 0,
                                                                      second == 2 ? 1 : 0, 0, 1, &lower));
                            auto& line = f.state.cache[1].lines[0][0];
                            line.address = 0x2000;
                            line.state = first;
                            memset(line.data, first == 2 ? 0xAA : second == 2 ? 0xBB : 0x11, 32);
                            ROMlib_ppc_bus_result result = {};
                            assert(ROMlib_ppc_750_snoop(&f.state, &f.bus, 0x2000, type, 1, inhibited, &result));
                            bool killed = type == 6;
                            bool retain = type == 11 || (type == 10 && inhibited);
                            bool first_push = enabled_l1 && first == 2 && !killed;
                            uint32_t expected = first_push               ? 0xAAAAAAAA
                                                : second == 2 && !killed ? 0xBBBBBBBB
                                                                         : 0x11111111;
                            assert(f.word(0x2000) == expected);
                            assert(line.state == (!enabled_l1 ? first : retain && first ? 1u : 0u));
                            assert(ROMlib_ppc_l2cache750_transfer(f.l2.get(), &f.bus, 262144, 0x2000, bytes, 4, 0, 0, 0,
                                                                  1, &lower));
                            assert(lower.hits == unsigned(retain && second));
                            if (retain && second)
                                assert((uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 8 |
                                        bytes[3]) == expected);
                            ++cases;
                        }
    for (unsigned opcode : {54u, 86u, 470u})
        for (unsigned first = 0; first < 3; ++first)
            for (unsigned second = 0; second < 3; ++second)
                for (bool test : {false, true}) {
                    fixture f(flags);
                    f.enable_l2(0x99000000 | (test ? 0x40000 : 0));
                    f.state.hid0 = 0x4008;
                    memset(f.ram.data() + 0x2000, 0x11, 32);
                    unsigned char bytes[32];
                    memset(bytes, 0xBB, sizeof bytes);
                    ROMlib_ppc_l2cache750_result lower = {};
                    if (second)
                        assert(ROMlib_ppc_l2cache750_transfer(f.l2.get(), &f.bus, 262144, 0x2000, bytes, 32, 0,
                                                              second == 2 ? 1 : 0, 0, 1, &lower));
                    auto& line = f.state.cache[1].lines[0][0];
                    line.address = 0x2000;
                    line.state = first;
                    memset(line.data, first == 2 ? 0xAA : second == 2 ? 0xBB : 0x11, 32);
                    ROMlib_ppc_bus_result result = {};
                    assert(ROMlib_ppc_750_cache_control(&f.state, &f.bus, 0x2000, opcode, &result));
                    bool push = opcode != 470 && first == 2;
                    bool lower_push = opcode != 470 && !push && second == 2;
                    uint32_t expected = push && !test ? 0xAAAAAAAA : lower_push ? 0xBBBBBBBB : 0x11111111;
                    assert(f.word(0x2000) == expected);
                    assert(line.state == (opcode == 54 && first ? 1u : 0u));
                    assert(f.controls == unsigned(opcode == 470 || (!push && !lower_push)));
                    assert(ROMlib_ppc_l2cache750_transfer(f.l2.get(), &f.bus, 262144, 0x2000, bytes, 4, 0, 0, 0, 1,
                                                          &lower));
                    assert(lower.hits == unsigned(push && test));
                    if (push && test)
                        for (unsigned i = 0; i < 4; ++i)
                            assert(bytes[i] == 0xAA);
                    ++cases;
                }
    for (unsigned second = 0; second < 3; ++second)
        for (bool test : {false, true})
            for (bool global : {false, true}) {
                fixture f(flags);
                f.enable_l2(0x99000000 | (test ? 0x40000 : 0));
                f.state.hid0 = 0x4000;
                f.state.mmu.msr = 0x10;
                f.state.mmu.dbat[0][0] = 0x90000002;
                f.state.mmu.dbat[0][1] = global ? 18 : 2;
                memset(f.ram.data() + 0x2000, 0x11, 32);
                unsigned char bytes[32];
                memset(bytes, 0xBB, sizeof bytes);
                ROMlib_ppc_l2cache750_result lower = {};
                if (second)
                    assert(ROMlib_ppc_l2cache750_transfer(f.l2.get(), &f.bus, 262144, 0x2000, bytes, 32, 0,
                                                          second == 2 ? 1 : 0, 0, 1, &lower));
                ROMlib_ppc_bus_result result = {};
                assert(ROMlib_ppc_750_cache_control(&f.state, &f.bus, 0x90002000, 1014, &result));
                assert(f.controls == unsigned(global && !test && !second));
                assert(f.word(0x2000) == 0x11111111 && f.state.cache[1].lines[0][0].state == 2);
                for (auto byte : f.state.cache[1].lines[0][0].data)
                    assert(!byte);
                ++cases;
            }
    for (bool instruction_cache : {false, true}) {
        fixture f(flags);
        f.enable_l2(0x99000000);
        f.state.hid0 = instruction_cache ? 0x8000 : 0;
        f.state.mmcr1 = 12u << 22;
        f.registers.pc = 0x1000;
        f.word(0x1000, 0x38600001);
        f.snoop_fetch = 0x1000;
        f.snoop_address = 0x1000;
        f.snoop_global = 1;
        f.allow_snoop_failure = true;
        f.run(1);
        assert(f.snoop_failed && f.snoop_count == 1 && f.state.pmc[3] == 1 && !f.state.bus_check_pending);
        assert(f.registers.pc == 0x1004 && f.registers.gpr[3] == 1);
        ++cases;
    }
    for (unsigned first : {0u, 0x4000u, 0x5000u})
        for (unsigned second = 0; second < 3; ++second)
            for (bool page_wt : {false, true})
                for (bool global_wt : {false, true}) {
                    fixture f(flags);
                    f.enable_l2(0x99000000 | (global_wt ? 0x80000 : 0));
                    f.state.hid0 = first;
                    f.state.mmu.msr = 0x10;
                    f.state.mmu.dbat[0][0] = 0x90000002;
                    f.state.mmu.dbat[0][1] = page_wt ? 66 : 2;
                    f.word(0x2000, 0x11223344);
                    unsigned char bytes[32] = {};
                    bytes[0] = 0xAA;
                    bytes[1] = 0xBB;
                    bytes[2] = 0xCC;
                    bytes[3] = 0xDD;
                    ROMlib_ppc_l2cache750_result lower = {};
                    if (second)
                        assert(ROMlib_ppc_l2cache750_transfer(f.l2.get(), &f.bus, 262144, 0x2000, bytes, 32, 0,
                                                              second == 2 ? 1 : 0, 0, 1, &lower));
                    f.registers.pc = 0x1000;
                    f.registers.gpr[4] = 0x90002000;
                    f.registers.gpr[5] = 0xDEADBEEF;
                    f.word(0x1000, 0x7C602028); // lwarx r3,0,r4
                    f.word(0x1004, 0x7CA0212D); // stwcx. r5,0,r4
                    auto* translations = ROMlib_ppc_translation_cache_create();
                    assert(translations);
                    f.env.translation_cache = translations;
                    assert(ROMlib_ppc_translate_range_mode(translations, &f.ram[0x1000], 0x1000, 8,
                                                           ROMLIB_PPC_TRANSLATE_750) == 2);
                    f.run(2);
                    if (flags)
                        assert(f.env.native_instructions == 2 && !f.env.native_compilations);
                    assert((f.registers.cr & 0x20000000) && !f.state.reservation_valid);
                    assert(f.registers.gpr[3] == (second == 2 ? 0xAABBCCDDu : 0x11223344u));
                    bool first_hit = first == 0x4000;
                    bool second_hit = second || first_hit;
                    bool local = second_hit && (second == 2 || (first_hit && !page_wt && !global_wt));
                    assert(f.word(0x2000) == (local ? 0x11223344u : 0xDEADBEEFu));
                    ROMlib_ppc_bus_result result = {};
                    assert(ROMlib_ppc_750_host_sync(&f.state, &f.bus, &result));
                    assert(f.word(0x2000) == 0xDEADBEEF);
                    ROMlib_ppc_translation_cache_destroy(translations);
                    ++cases;
                }
    return cases;
}

static unsigned packet_floating_checks()
{
    struct packet_fp_fixture {
        std::unique_ptr<ROMlib_ppc_system750> state{new ROMlib_ppc_system750{}};
        ROMlib_ppc_physical_bus physical;
        unsigned beat = 0, packets = 0, host_csr = 0, modified_csr = 0;
        bool throwing = false;

        static unsigned csr()
        {
            unsigned value;
            __asm__ volatile("stmxcsr %0" : "=m"(value) : : "memory");
            return value;
        }

        static int transfer(void* opaque, uint32_t, void* bytes, unsigned size, unsigned access, unsigned, uint32_t*)
        {
            auto& f = *static_cast<packet_fp_fixture*>(opaque);
            assert(!access && size);
            if (!f.beat++) {
                assert(fegetround() == FE_DOWNWARD && csr() == f.host_csr);
                assert((fetestexcept(FE_ALL_EXCEPT) & (FE_DIVBYZERO | FE_INEXACT)) == FE_DIVBYZERO);
            }
            else {
                assert(fegetround() == FE_TOWARDZERO && csr() == f.modified_csr);
                assert(fetestexcept(FE_INEXACT));
            }
            assert(!romaw_ppc_set_fpscr_rounding(1) && !feraiseexcept(FE_INEXACT));
            f.modified_csr = csr();
            if (f.throwing && f.beat == 2)
                throw std::runtime_error("packet floating callback fixture");
            memset(bytes, 0x5A, size);
            return 1;
        }
    } context;

    floating_environment saved;
    assert(!saved.capture(true));
    assert(!romaw_ppc_set_fpscr_rounding(3) && !feclearexcept(FE_ALL_EXCEPT) && !feraiseexcept(FE_DIVBYZERO));
    context.host_csr = packet_fp_fixture::csr();
    context.physical = {&context, packet_fp_fixture::transfer, [](void*, uint32_t, uint32_t, uint32_t*) { return 1; }};
    fixture guest(ROMLIB_PPC_EXEC_JIT);
    guest.registers.pc = 0x1000;
    guest.registers.lr = 0x1004;
    guest.env.instruction_limit = 4;
    guest.word(0x1000, 0x18000001);
    guest.word(0x1004, 0x18000000);
    guest.env.opaque = &context;
    guest.env.system_service_gate = [](void*, uint32_t, uint32_t opcode) -> int {
        return opcode == 0x18000000 || opcode == 0x18000001;
    };
    guest.env.service = [](void* opaque, uint32_t id, ROMlib_ppc_registers*) {
        assert(id == 1);
        auto& f = *static_cast<packet_fp_fixture*>(opaque);
        auto kernel = romaw_ppc_native_memory_code();
        assert(kernel);
        ppc_native_memory_frame frame = {f.state.get(), nullptr, 0xFFC, 8, 0, 0, 0, {}, 0, 0};
        romaw_ppc_native_bus_scope boundary(&f.physical, &frame.serial);
        frame.bus = boundary.bus();
        for (unsigned packet = 1; packet <= 3; ++packet) {
            frame.serial = packet;
            f.beat = 0;
            f.throwing = packet == 3;
            assert(!romaw_ppc_set_fpscr_rounding(2) && !feclearexcept(FE_ALL_EXCEPT) && !feraiseexcept(FE_INVALID));
            unsigned guest_csr = packet_fp_fixture::csr();
            kernel->get(ppc_native_memory_kernel::read)(&frame);
            assert(f.beat == 2 && bool(frame.ok) == !f.throwing);
            bool caught = false;
            try {
                boundary.rethrow();
            }
            catch (const std::runtime_error& error) {
                assert(!strcmp(error.what(), "packet floating callback fixture"));
                caught = true;
            }
            assert(caught == f.throwing && !f.state->bus_check_pending);
            assert(fegetround() == FE_UPWARD && packet_fp_fixture::csr() == guest_csr);
            assert((fetestexcept(FE_ALL_EXCEPT) & (FE_INVALID | FE_INEXACT | FE_DIVBYZERO)) == FE_INVALID);
            ++f.packets;
        }
        return 1;
    };
    assert(ROMlib_ppc_execute(&guest.registers, &guest.env) && context.packets == 3 && guest.registers.pc == 0x1008);
    assert(fegetround() == FE_DOWNWARD && packet_fp_fixture::csr() == context.host_csr);
    assert((fetestexcept(FE_ALL_EXCEPT) & (FE_DIVBYZERO | FE_INEXACT | FE_INVALID)) == FE_DIVBYZERO);
    assert(!saved.restore());
    puts("3 emitted callback packets preserve per-beat host mutations, guest FP state and thrown exceptions");
    return 3;
}

static unsigned native_execution()
{
    if (!ROMlib_ppc_jit_available())
        return 0;
    unsigned cases = packet_floating_checks();
    for (unsigned number : {1u, 8u, 9u})
        for (bool write : {false, true})
            for (unsigned reg : {0u, 31u})
                for (uint32_t msr : {0u, 0x4000u, 0x4400u, 0x4021u})
                    for (bool reserved : {false, true}) {
                        fixture native(ROMLIB_PPC_EXEC_JIT), oracle(0);
                        uint32_t opcode = mtspr(number, reg) ^ (write ? 0 : 0x100);
                        uint32_t physical = 0x1000 ^ ((msr & 1) ? 4 : 0);
                        for (auto* f : {&native, &oracle}) {
                            f->state.mmu.msr = msr;
                            f->state.mmu.ibat[0][0] = 0x80000003;
                            f->state.mmu.ibat[0][1] = 2;
                            f->state.mmcr0 = (3u << 6) | 4;
                            f->registers.pc = (msr & 0x20) ? 0x80001000 : 0x1000;
                            f->registers.xer = 0xA000006D;
                            f->registers.lr = 0x10203041;
                            f->registers.ctr = 0x87654322;
                            f->registers.gpr[reg] = 0xFEDCBA98;
                            f->word(physical, opcode);
                        }
                        auto* cache = ROMlib_ppc_translation_cache_create();
                        assert(cache);
                        native.env.translation_cache = cache;
                        assert(ROMlib_ppc_translate_range_mode(cache, &native.ram[physical], native.registers.pc, 4,
                                                               ROMLIB_PPC_TRANSLATE_750) == 1);
                        if (reserved)
                            for (auto* f : {&native, &oracle})
                                f->word(physical, opcode | 1);
                        native.run(1);
                        oracle.run(1);
                        assert(!memcmp(&native.registers, &oracle.registers, sizeof native.registers));
                        assert(!memcmp(&native.state, &oracle.state, sizeof native.state));
                        assert(native.env.native_instructions == unsigned(!reserved) &&
                               native.env.native_cache_hits == unsigned(!reserved) && !native.env.native_compilations &&
                               !native.env.native_kernel_instructions);
                        ROMlib_ppc_translation_cache_destroy(cache);
                        ++cases;
                    }
    {
        fixture f(ROMLIB_PPC_EXEC_JIT);
        auto* cache = ROMlib_ppc_translation_cache_create();
        assert(cache);
        f.env.translation_cache = cache;
        f.state.mmu.msr = 0x4000;
        f.word(0x1000, mtspr(9, 3));
        for (unsigned iteration = 0; iteration < 9; ++iteration) {
            f.registers.pc = 0x1000;
            f.registers.gpr[3] = 0x76540000 + iteration;
            f.run(1);
            assert(f.registers.ctr == f.registers.gpr[3]);
            assert(f.env.native_instructions == unsigned(iteration >= 7) &&
                   f.env.native_compilations == unsigned(iteration == 7) &&
                   f.env.native_cache_hits == unsigned(iteration == 8) && !f.env.native_kernel_instructions);
            ++cases;
        }
        // A live rewrite to privileged MTSDR1 must bypass the warm user block.
        f.word(0x1000, mtspr(25, 3));
        f.registers.pc = 0x1000;
        f.run(1);
        assert(f.registers.pc == 0x700 && f.state.srr0 == 0x1000 && (f.state.srr1 & 0x40000) && !f.state.mmu.sdr1 &&
               !f.env.native_instructions);
        ROMlib_ppc_translation_cache_destroy(cache);
        ++cases;
    }
    // The same effective PC can see two different words as IBAT0 is remapped.
    // Alternate them before either word is hot, then exercise both cached views.
    for (bool eager : {false, true}) {
        fixture f(ROMLIB_PPC_EXEC_JIT);
        auto* cache = ROMlib_ppc_translation_cache_create();
        assert(cache);
        f.env.translation_cache = cache;
        f.state.mmu.msr = 0x20;
        f.state.mmu.ibat[0][0] = 0x80000002;
        for (unsigned offset : {0u, 0x20000u}) {
            auto physical = 0x1000 + offset;
            f.word(physical, 0x38630001 + (offset >> 17)); // addi r3,r3,1 or 2
            if (eager)
                assert(ROMlib_ppc_translate_range_mode(cache, &f.ram[physical], 0x80001000, 4,
                                                       ROMLIB_PPC_TRANSLATE_750) == 1);
        }
        for (unsigned pass = 0; pass < 10; ++pass)
            for (unsigned offset : {0u, 0x20000u}) {
                f.state.mmu.ibat[0][1] = offset | 2;
                f.registers.pc = 0x80001000;
                f.registers.gpr[3] = 10;
                f.run(1);
                assert(f.registers.pc == 0x80001004 && f.registers.gpr[3] == 11 + (offset >> 17));
                assert(f.env.native_instructions == unsigned(eager || pass >= 7) &&
                       f.env.native_compilations == unsigned(!eager && pass == 7) &&
                       f.env.native_cache_hits == unsigned(eager || pass > 7));
                ++cases;
            }
        f.state.mmu.ibat[0][0] = 0;
        f.registers.pc = 0x80001000;
        f.run(1);
        assert(f.registers.pc == 0x400 && f.state.srr0 == 0x80001000 && !f.env.native_instructions);
        ROMlib_ppc_translation_cache_destroy(cache);
        ++cases;
    }
    for (unsigned operation : {54u, 86u, 246u, 278u, 470u, 598u, 854u, 982u, 1014u})
        for (bool user : {false, true})
            for (unsigned mapping : {0u, 1u, 2u}) // Physical, BAT mapped, missing translation.
                for (bool reserved : {false, true}) {
                    fixture native(ROMLIB_PPC_EXEC_JIT), oracle(0);
                    uint32_t opcode = 0x7C000000 | (operation << 1);
                    if (operation != 598 && operation != 854)
                        opcode |= (mapping ? 4 << 16 : 0) | (5 << 11);
                    for (auto* f : {&native, &oracle}) {
                        f->registers.pc = 0x1000;
                        f->registers.gpr[0] = 0xFFFFFFFF;
                        f->registers.gpr[4] = 0x90002000;
                        f->registers.gpr[5] = mapping ? 3 : 0x2003;
                        f->state.mmu.msr = (user ? 0x4000 : 0) | (mapping ? 0x10 : 0) | 0x400;
                        f->state.mmu.dbat[0][0] = mapping == 1 ? 0x90000003 : 0;
                        f->state.mmu.dbat[0][1] = 2;
                        f->state.hid0 = 0xC000;
                        f->state.mmcr0 = 4;
                        memset(&f->ram[0x2000], 0xA5, 32);
                        f->word(0x1000, opcode);
                    }
                    auto* cache = ROMlib_ppc_translation_cache_create();
                    assert(cache);
                    native.env.translation_cache = cache;
                    assert(ROMlib_ppc_translate_range_mode(cache, &native.ram[0x1000], 0x1000, 4,
                                                           ROMLIB_PPC_TRANSLATE_750) == 1);
                    if (reserved)
                        for (auto* f : {&native, &oracle})
                            f->word(0x1000, opcode | 1);
                    native.run_direct_memory(1);
                    oracle.run(1);
                    assert(!memcmp(&native.registers, &oracle.registers, sizeof native.registers));
                    assert(!memcmp(&native.state, &oracle.state, sizeof native.state));
                    assert(native.ram == oracle.ram && native.controls == oracle.controls);
                    unsigned allowed = !reserved && (!user || operation != 470);
                    assert(native.env.native_instructions == allowed && !native.env.native_kernel_instructions &&
                           native.env.native_cache_hits == allowed && !native.env.native_compilations);
                    ROMlib_ppc_translation_cache_destroy(cache);
                    ++cases;
                }
    for (uint32_t opcode :
         {0x7C6000A6u, 0x7C600124u, 0x7C6B04A6u, 0x7C6B01A4u, 0x7C602526u, 0x7C6021E4u, 0x7C002264u, 0x7C00046Cu})
        for (bool user : {false, true})
            for (bool trace : {false, true})
                for (bool reserved : {false, true}) {
                    fixture native(ROMLIB_PPC_EXEC_JIT), oracle(0);
                    for (auto* f : {&native, &oracle}) {
                        f->registers.pc = 0x1000;
                        f->registers.gpr[3] = 0x00002030;
                        f->registers.gpr[4] = 0xB1234000;
                        f->state.mmu.msr = (user ? 0x4000 : 0) | (trace ? 0x400 : 0);
                        f->state.mmu.sr[11] = 0x710000AB;
                        f->state.mmcr0 = 4;
                        for (unsigned kind = 0; kind < 2; ++kind) {
                            f->state.mmu.tlb[kind][0x34][0].valid = 1;
                            f->state.mmu.tlb[kind][0x34][1].valid = 1;
                            f->state.mmu.tlb[kind][0x35][0].valid = 1;
                        }
                        f->word(0x1000, opcode);
                    }
                    auto* cache = ROMlib_ppc_translation_cache_create();
                    assert(cache);
                    native.env.translation_cache = cache;
                    assert(ROMlib_ppc_translate_range_mode(cache, &native.ram[0x1000], 0x1000, 4,
                                                           ROMLIB_PPC_TRANSLATE_750) == 1);
                    if (reserved)
                        for (auto* f : {&native, &oracle})
                            f->word(0x1000, opcode | 1);
                    native.run_direct_state(1);
                    oracle.run(1);
                    assert(!memcmp(&native.registers, &oracle.registers, sizeof native.registers));
                    assert(!memcmp(&native.state, &oracle.state, sizeof native.state));
                    unsigned allowed = !reserved && !user;
                    assert(native.env.native_instructions == allowed && !native.env.native_kernel_instructions &&
                           native.env.native_cache_hits == allowed && !native.env.native_compilations);
                    if (allowed && opcode == 0x7C002264)
                        for (unsigned kind = 0; kind < 2; ++kind) {
                            assert(!native.state.mmu.tlb[kind][0x34][0].valid);
                            assert(!native.state.mmu.tlb[kind][0x34][1].valid);
                            assert(native.state.mmu.tlb[kind][0x35][0].valid);
                        }
                    ROMlib_ppc_translation_cache_destroy(cache);
                    ++cases;
                }
    {
        auto* cache = ROMlib_ppc_translation_cache_create();
        assert(cache);
        for (unsigned set = 0; set < 64; ++set) {
            fixture f(ROMLIB_PPC_EXEC_JIT);
            f.env.translation_cache = cache;
            f.registers.pc = 0x1000;
            f.registers.gpr[4] = 0xFFFC0D45u | (set << 12);
            f.word(0x1000, 0x7C002264); // tlbie r4
            memset(f.state.mmu.tlb, 0xA5, sizeof f.state.mmu.tlb);
            memset(f.state.mmu.replace, 1, sizeof f.state.mmu.replace);
            auto expected = f.state.mmu;
            for (unsigned bank = 0; bank < 2; ++bank) {
                memset(expected.tlb[bank][set], 0, sizeof expected.tlb[bank][set]);
                expected.replace[bank][set] = 0;
            }
            if (set == 0)
                assert(ROMlib_ppc_translate_range_mode(cache, &f.ram[0x1000], 0x1000, 4, ROMLIB_PPC_TRANSLATE_750) ==
                       1);
            f.run(1);
            assert(!memcmp(&f.state.mmu, &expected, sizeof expected) && f.registers.pc == 0x1004);
            assert(f.env.native_instructions == 1 && f.env.native_cache_hits == 1 &&
                   !f.env.native_kernel_instructions && !f.env.native_compilations);
            ++cases;
        }
        ROMlib_ppc_translation_cache_destroy(cache);
    }
    {
        auto* cache = ROMlib_ppc_translation_cache_create();
        assert(cache);
        constexpr uint32_t mtsrin_r3_r3 = 0x7C6019E4;
        constexpr uint32_t mfsrin_r3_r3 = 0x7C601D26;
        for (unsigned segment = 0; segment < 16; ++segment) {
            fixture f(ROMLIB_PPC_EXEC_JIT);
            f.env.translation_cache = cache;
            f.word(0x1000, mtsrin_r3_r3);
            f.word(0x1004, mfsrin_r3_r3);
            if (segment == 0)
                assert(ROMlib_ppc_translate_range_mode(cache, &f.ram[0x1000], 0x1000, 8, ROMLIB_PPC_TRANSLATE_750) ==
                       2);
            uint32_t value = (segment << 28) | (0x123000 + segment);
            f.registers.pc = 0x1000;
            f.registers.gpr[3] = value;
            f.run(1);
            for (unsigned i = 0; i < 16; ++i)
                assert(f.state.mmu.sr[i] == (i == segment ? value : 0));
            assert(f.registers.gpr[3] == value && f.registers.pc == 0x1004);
            assert(f.env.native_instructions == 1 && f.env.native_cache_hits == 1 && !f.env.native_kernel_instructions);
            f.registers.gpr[3] = (segment << 28) | 0x500;
            f.run(1);
            assert(f.registers.gpr[3] == value && f.registers.pc == 0x1008);
            assert(f.env.native_instructions == 1 && f.env.native_cache_hits == 1 &&
                   !f.env.native_kernel_instructions && !f.env.native_compilations);
            ++cases;
        }
        ROMlib_ppc_translation_cache_destroy(cache);
    }
    for (uint32_t saved_msr : {0u, 0x9000u, 0x2800u, 0x40000u}) {
        fixture native(ROMLIB_PPC_EXEC_JIT), oracle(0);
        for (auto* f : {&native, &oracle}) {
            f->registers.pc = 0x1000;
            f->state.srr0 = 0x11223347;
            f->state.srr1 = saved_msr;
            f->state.iabr_pending = 1;
            f->word(0x1000, 0x4C000064);
        }
        auto* cache = ROMlib_ppc_translation_cache_create();
        assert(cache);
        native.env.translation_cache = cache;
        assert(ROMlib_ppc_translate_range_mode(cache, &native.ram[0x1000], 0x1000, 4, ROMLIB_PPC_TRANSLATE_750) == 1);
        native.run_direct_state(1);
        oracle.run(1);
        assert(!memcmp(&native.registers, &oracle.registers, sizeof native.registers));
        assert(!memcmp(&native.state, &oracle.state, sizeof native.state));
        assert(native.registers.pc == 0x11223344 && !native.state.iabr_pending);
        assert(native.env.native_instructions == 1 && !native.env.native_kernel_instructions);
        ROMlib_ppc_translation_cache_destroy(cache);
        ++cases;
    }
    for (unsigned number : {18u,  19u,  22u,   25u,   26u,   27u,   268u,  269u,  272u,  282u,  284u,  285u,
                            287u, 528u, 535u,  536u,  543u,  936u,  942u,  952u,  953u,  954u,  955u,  956u,
                            957u, 958u, 1000u, 1008u, 1009u, 1010u, 1013u, 1017u, 1019u, 1020u, 1021u, 1022u})
        for (bool write : {false, true})
            for (bool user : {false, true}) {
                fixture native(ROMLIB_PPC_EXEC_JIT), oracle(0);
                uint32_t opcode = mtspr(number, 3) ^ (write ? 0 : 0x100);
                for (auto* f : {&native, &oracle}) {
                    f->registers.pc = 0x1000;
                    f->registers.gpr[3] = number == 1017 ? 0 : 0x12345678;
                    f->state.mmu.msr = user ? 0x4000 : 0;
                    f->state.mmu.sdr1 = 0x20000;
                    f->state.time_base = UINT64_C(0x89ABCDEF01234567);
                    f->state.decrementer = 100;
                    f->state.pmc[0] = 0x4A5B6C7D;
                    f->word(0x1000, opcode);
                }
                auto* cache = ROMlib_ppc_translation_cache_create();
                assert(cache);
                native.env.translation_cache = cache;
                bool read_only = number == 268 || number == 269 || (number >= 936 && number <= 942);
                bool write_only = number == 284 || number == 285;
                bool modeled = number != 1000 && !(write && read_only) && !(!write && write_only);
                assert(ROMlib_ppc_translate_range_mode(cache, &native.ram[0x1000], 0x1000, 4,
                                                       ROMLIB_PPC_TRANSLATE_750) == unsigned(modeled));
                native.run_direct_state(1);
                oracle.run(1);
                assert(!memcmp(&native.registers, &oracle.registers, sizeof native.registers));
                assert(!memcmp(&native.state, &oracle.state, sizeof native.state));
                unsigned allowed = modeled && (!user || !(number & 16));
                if (allowed && write && number == 282)
                    assert(native.state.ear == 8);
                assert(native.env.native_instructions == allowed && !native.env.native_kernel_instructions &&
                       native.env.native_cache_hits == allowed && !native.env.native_compilations);
                ROMlib_ppc_translation_cache_destroy(cache);
                ++cases;
            }
    {
        fixture f(ROMLIB_PPC_EXEC_JIT);
        f.registers.pc = 0x1000;
        f.registers.gpr[3] = 0x80000000;
        f.word(0x1000, mtspr(1017, 3));
        auto* cache = ROMlib_ppc_translation_cache_create();
        assert(cache);
        f.env.translation_cache = cache;
        assert(ROMlib_ppc_translate_range_mode(cache, &f.ram[0x1000], 0x1000, 4, ROMLIB_PPC_TRANSLATE_750) == 1);
        assert(!ROMlib_ppc_execute(&f.registers, &f.env) && strstr(f.env.error, "L2 storage is unavailable"));
        assert(f.registers.pc == 0x1000 && !f.state.l2cr && !f.state.l2invalidate_remaining);
        assert(f.env.instructions == 1 && f.env.native_instructions == 1 && f.env.native_cache_hits == 1 &&
               !f.env.native_kernel_instructions && !f.env.native_compilations);
        ROMlib_ppc_translation_cache_destroy(cache);
        ++cases;
    }
    for (unsigned number : {268u, 269u, 284u})
        for (bool reserved : {false, true}) {
            fixture f(ROMLIB_PPC_EXEC_JIT);
            f.registers.pc = 0x1000;
            f.state.mmu.msr = 0x4000;
            f.state.time_base = UINT64_C(0x89ABCDEF01234567);
            uint32_t opcode = (mtspr(number, 3) & ~0x7FEu) | (371u << 1) | unsigned(reserved);
            f.word(0x1000, opcode);
            auto* cache = ROMlib_ppc_translation_cache_create();
            assert(cache);
            f.env.translation_cache = cache;
            bool allowed = !reserved && number != 284;
            assert(ROMlib_ppc_translate_range_mode(cache, &f.ram[0x1000], 0x1000, 4, ROMLIB_PPC_TRANSLATE_750) ==
                   unsigned(allowed));
            f.run(1);
            if (allowed)
                assert(f.registers.gpr[3] == (number == 268 ? 0x01234567 : 0x89ABCDEF) && f.registers.pc == 0x1004);
            else
                assert(f.registers.pc == 0x700 && f.state.srr0 == 0x1000 && (f.state.srr1 & 0x80000));
            assert(f.env.native_instructions == unsigned(allowed) && !f.env.native_kernel_instructions &&
                   !f.env.native_compilations);
            ROMlib_ppc_translation_cache_destroy(cache);
            ++cases;
        }
    for (uint32_t opcode : {0x0C83FFFFu, 0x7C832008u, 0x44000002u, 0x4C000064u})
        for (uint32_t msr : {0x2000u, 0x2400u, 0x6000u, 0x6400u})
            for (bool condition : {false, true})
                for (bool reserved : {false, true}) {
                    bool immediate = opcode >> 26 == 3;
                    if (immediate && reserved)
                        continue; // TWI uses all sixteen immediate bits.
                    bool rfi = opcode == 0x4C000064;
                    bool sc = opcode == 0x44000002;
                    bool privileged = rfi && (msr & 0x4000) && !reserved;
                    fixture native(ROMLIB_PPC_EXEC_JIT), oracle(0);
                    for (auto* f : {&native, &oracle}) {
                        f->registers.pc = 0x1000;
                        f->registers.gpr[3] = condition ? UINT32_MAX : 0;
                        f->registers.gpr[4] = UINT32_MAX;
                        f->registers.fpscr = 0xC4000010;
                        f->state.mmu.msr = msr;
                        f->state.srr0 = 0x80002003;
                        f->state.srr1 = condition ? 0x2820 : 0x2020;
                        f->state.mmcr0 = 4 | (3u << 6);
                        f->word(0x1000, opcode | unsigned(reserved));
                    }
                    auto* cache = ROMlib_ppc_translation_cache_create();
                    assert(cache);
                    native.env.translation_cache = cache;
                    assert(ROMlib_ppc_translate_range_mode(cache, &native.ram[0x1000], 0x1000, 4,
                                                           ROMLIB_PPC_TRANSLATE_750) == unsigned(!reserved));
                    native.run(1);
                    oracle.run(1);
                    assert(!memcmp(&native.registers, &oracle.registers, sizeof native.registers));
                    assert(!memcmp(&native.state, &oracle.state, sizeof native.state));
                    unsigned allowed = !reserved && !privileged;
                    assert(native.env.native_instructions == allowed && !native.env.native_kernel_instructions &&
                           native.env.native_cache_hits == allowed && !native.env.native_compilations);
                    if (reserved || privileged) {
                        assert(native.registers.pc == 0x700 && native.state.srr0 == 0x1000);
                        assert(native.state.srr1 == (msr | (reserved ? 0x80000 : 0x40000)));
                    }
                    else if (sc)
                        assert(native.registers.pc == 0xC00 && native.state.srr0 == 0x1004 && native.state.srr1 == msr);
                    else if (rfi) {
                        if (condition)
                            assert(native.registers.pc == 0x700 && native.state.srr0 == 0x80002000 &&
                                   native.state.srr1 == 0x112820);
                        else
                            assert(native.registers.pc == 0x80002000 && native.state.mmu.msr == 0x2020);
                    }
                    else if (condition)
                        assert(native.registers.pc == 0x700 && native.state.srr0 == 0x1000 &&
                               native.state.srr1 == (msr | 0x20000));
                    else if (msr & 0x400)
                        assert(native.registers.pc == 0xD00 && native.state.srr0 == 0x1004);
                    else
                        assert(native.registers.pc == 0x1004);
                    ROMlib_ppc_translation_cache_destroy(cache);
                    ++cases;
                }
    {
        // The same comparison can have opposite signed and unsigned results.
        const struct {
            uint32_t left, right;
            unsigned matches;
        } comparisons[] = {
            {0, 0, 4},
            {0, 1, 16 | 2},
            {0xFFFFFFFF, 0, 16 | 1},
            {0, 0xFFFFFFFF, 8 | 2},
            {2, 1, 8 | 1},
            {0x80000000, 0xFFFF8000, 16 | 2},
            {0x7FFFFFFF, 0xFFFF8000, 8 | 2},
            {0x8000, 0x7FFF, 8 | 1},
            {0xFFFF8000, 0xFFFF8000, 4},
        };

        fixture f(ROMLIB_PPC_EXEC_JIT);
        auto* cache = ROMlib_ppc_translation_cache_create();
        assert(cache);
        f.env.translation_cache = cache;
        f.state.mmu.ibat[0][0] = 0x80000003;
        f.state.mmu.ibat[0][1] = 2;
        for (bool immediate : {false, true})
            for (unsigned options = 0; options < 32; ++options)
                for (const auto& test : comparisons) {
                    uint32_t opcode = (options << 21) | (immediate ? 0x0C000000 | (test.right & 0xFFFF) : 0x7C00F808);
                    bool taken = options & test.matches;
                    uint32_t msr = 0x6020 | (cases & 1);
                    uint32_t physical = 0x1000 ^ ((msr & 1) ? 4 : 0);
                    f.word(physical, opcode);
                    f.state.mmu.msr = msr;
                    f.state.srr0 = 0x11223344;
                    f.state.srr1 = 0x55667788;
                    f.registers.pc = 0x80001000;
                    f.registers.gpr[0] = test.left;
                    f.registers.gpr[31] = test.right;
                    f.registers.cr = 0x11223344;
                    f.registers.xer = 0xE000007F;
                    assert(ROMlib_ppc_translate_range_mode(cache, &f.ram[physical], f.registers.pc, 4,
                                                           ROMLIB_PPC_TRANSLATE_750) <= 1);
                    f.run(1);
                    assert(f.registers.pc == (taken ? 0x700u : 0x80001004u));
                    assert(f.state.srr0 == (taken ? 0x80001000u : 0x11223344u));
                    assert(f.state.srr1 == (taken ? (msr | 0x20000) : 0x55667788u));
                    assert(f.registers.gpr[0] == test.left && f.registers.gpr[31] == test.right);
                    assert(f.registers.cr == 0x11223344 && f.registers.xer == 0xE000007F);
                    assert(f.env.native_instructions == 1 && f.env.native_cache_hits == 1 &&
                           !f.env.native_kernel_instructions && !f.env.native_compilations);
                    ++cases;
                }
        ROMlib_ppc_translation_cache_destroy(cache);
    }
    {
        fixture f(ROMLIB_PPC_EXEC_JIT);
        auto* cache = ROMlib_ppc_translation_cache_create();
        assert(cache);
        f.env.translation_cache = cache;
        // Replace the warm RFI with SC at the same PC and require it to heat anew.
        for (uint32_t opcode : {0x4C000064u, 0x44000002u}) {
            f.word(0x1000, opcode);
            for (unsigned iteration = 0; iteration < 9; ++iteration) {
                f.registers.pc = 0x1000;
                f.state.mmu.msr = 0x400;
                f.state.srr0 = 0x2003 + 4 * iteration;
                f.state.srr1 = 0x20;
                f.run(1);
                if (opcode == 0x4C000064)
                    assert(f.registers.pc == 0x2000 + 4 * iteration && f.state.mmu.msr == 0x20);
                else
                    assert(f.registers.pc == 0xC00 && f.state.srr0 == 0x1004 && f.state.srr1 == 0x400);
                assert(f.env.native_instructions == unsigned(iteration >= 7) && !f.env.native_kernel_instructions &&
                       f.env.native_compilations == unsigned(iteration == 7) &&
                       f.env.native_cache_hits == unsigned(iteration == 8));
                ++cases;
            }
        }
        ROMlib_ppc_translation_cache_destroy(cache);
    }
    for (uint32_t opcode : {0x7C600124u, 0x4C000064u}) {
        fixture native(ROMLIB_PPC_EXEC_JIT), oracle(0);
        for (auto* f : {&native, &oracle}) {
            f->registers.pc = 0x1000;
            f->registers.gpr[3] = 0x6400;
            f->state.mmu.msr = 0x2000;
            f->state.srr0 = 0x2003;
            f->state.srr1 = 0x6400;
            f->state.iabr_pending = 1;
            f->word(0x1000, opcode);
            f->bus.transfer = [](void* opaque, uint32_t address, void* bytes, unsigned size, unsigned access,
                                 unsigned wimg, uint32_t* cause) {
                int ok = fixture::transfer(opaque, address, bytes, size, access, wimg, cause);
                if (ok && (access & ROMLIB_PPC_MMU_INSTRUCTION))
                    static_cast<fixture*>(opaque)->state.checkstop = 1;
                return ok;
            };
        }
        auto* cache = ROMlib_ppc_translation_cache_create();
        assert(cache);
        native.env.translation_cache = cache;
        assert(ROMlib_ppc_translate_range_mode(cache, &native.ram[0x1000], 0x1000, 4, ROMLIB_PPC_TRANSLATE_750) == 1);
        for (auto* f : {&native, &oracle}) {
            assert(!ROMlib_ppc_execute(&f->registers, &f->env) && strstr(f->env.error, "checkstop"));
            assert(f->registers.pc == (opcode == 0x4C000064 ? 0x1000u : 0x1004u));
            assert(f->fetches == 1 && f->state.mmu.msr == 0x2000 && f->state.iabr_pending);
        }
        assert(!memcmp(&native.registers, &oracle.registers, sizeof native.registers));
        assert(!memcmp(&native.state, &oracle.state, sizeof native.state));
        assert(native.env.native_instructions == 1 && native.env.native_cache_hits == 1 &&
               !native.env.native_kernel_instructions && !native.env.native_compilations);
        ROMlib_ppc_translation_cache_destroy(cache);
        ++cases;
    }
    for (bool instruction_mapping : {false, true})
        for (bool little : {false, true})
            for (unsigned hid0 : {0u, 0xC000u, 0x5000u})
                for (uint32_t opcode : {0x80640000u, 0x90640000u, 0x84640004u, 0x94640004u})
                    for (unsigned fault = 0; fault < 5; ++fault) {
                        fixture native(ROMLIB_PPC_EXEC_JIT), oracle(0);
                        for (auto* f : {&native, &oracle}) {
                            f->state.hid0 = hid0;
                            f->state.mmcr0 = 8 << 6 | 4;
                            f->state.mmcr1 = 2u << 27 | 13u << 22;
                            f->state.mmu.msr = 0x1010 | (instruction_mapping ? 0x20 : 0) | little;
                            f->state.mmu.ibat[0][0] = 0x80000002;
                            f->state.mmu.ibat[0][1] = 2;
                            f->state.mmu.dbat[0][0] = fault == 2 ? 0 : 0x90000002;
                            f->state.mmu.dbat[0][1] = fault == 1 ? 0x20000 : 0x20002;
                            f->state.mmu.sdr1 = 0x10000;
                            f->state.mmu.sr[9] = 0x23;
                            f->registers.pc = instruction_mapping ? 0x80001000 : 0x1000;
                            f->registers.gpr[3] = 0xAABBCCDD;
                            f->registers.gpr[4] = 0x90000080;
                            f->word(0x1000 ^ (little ? 4 : 0), opcode);
                            f->word(0x20080, 0x12345678);
                            f->word(0x20084, 0x99887766);
                            uint32_t ea = 0x90000080 + (opcode & 0xFFFF);
                            if (fault == 3) {
                                uint32_t physical = (0x20000 | (ea & 0x1FFFF)) ^ (little ? 4 : 0);
                                f->bad_data_address = ((hid0 & 0x4000) && !(hid0 & 0x1000)) ? physical & ~7u : physical;
                            }
                            if (fault == 4)
                                f->state.dabr = (ea & ~7u) | 7;
                        }
                        auto* cache = ROMlib_ppc_translation_cache_create();
                        assert(cache);
                        native.env.translation_cache = cache;
                        assert(ROMlib_ppc_translate_range_mode(cache, &native.ram[0x1000 ^ (little ? 4 : 0)],
                                                               native.registers.pc, 4, ROMLIB_PPC_TRANSLATE_750) == 1);
                        if (fault == 4)
                            native.run(1);
                        else
                            native.run_direct_memory(1);
                        oracle.run(1);
                        assert(!memcmp(&native.registers, &oracle.registers, sizeof native.registers));
                        assert(!memcmp(&native.state, &oracle.state, sizeof native.state));
                        assert(native.ram == oracle.ram);
                        assert(native.env.native_instructions == 1 && !native.env.native_kernel_instructions &&
                               !native.env.native_compilations && native.env.native_cache_hits == 1);
                        ROMlib_ppc_translation_cache_destroy(cache);
                        ++cases;
                    }
    for (bool little : {false, true})
        for (unsigned xo :
             {23u, 55u, 87u, 119u, 151u, 183u, 215u, 247u, 279u, 311u, 343u, 375u, 407u, 439u, 534u, 662u, 790u, 918u})
            for (uint32_t address : {0x2100u, 0x2FFFu}) {
                fixture native(ROMLIB_PPC_EXEC_JIT), oracle(0);
                // r3 is both the index and the data register. The addition wraps.
                uint32_t opcode = 0x7C641800 | (xo << 1);
                uint32_t fetch = little ? 0x1004 : 0x1000;
                for (auto* f : {&native, &oracle}) {
                    f->state.mmu.msr = little;
                    f->registers.pc = 0x1000;
                    f->registers.gpr[3] = address + 0x100;
                    f->registers.gpr[4] = 0xFFFFFF00;
                    f->word(fetch, opcode);
                    for (uint32_t start = (address & ~7u) - 8; start < (address & ~7u) + 16; start += 4)
                        f->word(start, 0x80F12345);
                }
                auto* cache = ROMlib_ppc_translation_cache_create();
                assert(cache);
                native.env.translation_cache = cache;
                assert(ROMlib_ppc_translate_range_mode(cache, &native.ram[fetch], 0x1000, 4,
                                                       ROMLIB_PPC_TRANSLATE_750) == 1);
                native.run_direct_memory(1);
                oracle.run(1);
                assert(!memcmp(&native.registers, &oracle.registers, sizeof native.registers));
                assert(!memcmp(&native.state, &oracle.state, sizeof native.state) && native.ram == oracle.ram);
                assert(native.registers.pc == 0x1004 && native.env.native_instructions == 1 &&
                       native.env.native_cache_hits == 1 && !native.env.native_kernel_instructions &&
                       !native.env.native_compilations);
                if (!little && address == 0x2100) {
                    if (xo == 343 || xo == 375)
                        assert(native.registers.gpr[3] == 0xFFFF80F1);
                    if (xo == 534)
                        assert(native.registers.gpr[3] == 0x4523F180);
                    if (xo == 790)
                        assert(native.registers.gpr[3] == 0xF180);
                }
                ROMlib_ppc_translation_cache_destroy(cache);
                ++cases;
            }
    const unsigned floating_indexed[] = {535, 567, 599, 631, 663, 695, 727, 759, 983};
    for (unsigned form = 0; form < 9; ++form)
        for (bool indexed : {false, true}) {
            if (!indexed && form == 8)
                continue; // STFIWX is indexed only.
            uint32_t opcode = indexed ? 0x7C642800 | (floating_indexed[form] << 1) : ((48 + form) << 26) | 0x0064FFFC;
            bool write = form >= 4;
            unsigned size = (form == 2 || form == 3 || form == 6 || form == 7) ? 8 : 4;
            for (bool little : {false, true})
                for (unsigned scenario = 0; scenario < 6; ++scenario) {
                    fixture native(ROMLIB_PPC_EXEC_JIT), oracle(0);
                    uint32_t ea = scenario == 1 || scenario == 2 ? 0x90000081 : scenario == 5 ? 0x90000084 : 0x90000080;
                    uint32_t physical = (0x20000 | (ea & 0x1FFFF)) ^ (little ? 8 - size : 0);
                    uint32_t fetch = little ? 0x1004 : 0x1000;
                    uint64_t source = UINT64_C(0x7FF0000123456789);
                    for (auto* f : {&native, &oracle}) {
                        f->state.mmu.msr = 0x1010 | little | (scenario == 2 ? 0 : 0x2000);
                        f->state.mmu.dbat[0][0] = 0x90000002;
                        f->state.mmu.dbat[0][1] = 0x20002;
                        f->registers.pc = 0x1000;
                        f->registers.gpr[4] = ea + 4;
                        f->registers.gpr[5] = 0xFFFFFFFC;
                        f->registers.fpscr = 0x4000;
                        memcpy(&f->registers.fpr[3], &source, sizeof source);
                        f->word(fetch, opcode);
                        f->word(physical, size == 8 ? 0xFFF0ABCD : 0xFF800001);
                        if (size == 8)
                            f->word(physical + 4, 0xBEEF0123);
                        if (scenario == 3)
                            f->bad_data_address = physical;
                        if (scenario == 4)
                            f->state.dabr = (ea & ~7u) | 7;
                    }
                    auto* cache = ROMlib_ppc_translation_cache_create();
                    assert(cache);
                    native.env.translation_cache = cache;
                    assert(ROMlib_ppc_translate_range_mode(cache, &native.ram[fetch], 0x1000, 4,
                                                           ROMLIB_PPC_TRANSLATE_750) == 1);
                    if (scenario == 4)
                        native.run(1);
                    else
                        native.run_direct_memory(1);
                    oracle.run(1);
                    assert(!memcmp(&native.registers, &oracle.registers, sizeof native.registers));
                    assert(!memcmp(&native.state, &oracle.state, sizeof native.state) && native.ram == oracle.ram);
                    bool attempted = scenario != 1 && scenario != 2;
                    assert(native.env.native_instructions == unsigned(attempted) &&
                           native.env.native_cache_hits == unsigned(attempted) &&
                           !native.env.native_kernel_instructions && !native.env.native_compilations);
                    const uint32_t expected_pc[] = {0x1004, 0x600, 0x800, 0x200, 0x300, 0x1004};
                    assert(native.registers.pc == expected_pc[scenario] && native.registers.fpscr == 0x4000);
                    if (!scenario) {
                        if (!write) {
                            uint64_t result;
                            memcpy(&result, &native.registers.fpr[3], sizeof result);
                            assert(result == (size == 8 ? UINT64_C(0xFFF0ABCDBEEF0123) : UINT64_C(0xFFF0000020000000)));
                        }
                        else if (size == 8)
                            assert(native.word(physical) == uint32_t(source >> 32) &&
                                   native.word(physical + 4) == uint32_t(source));
                        else
                            assert(native.word(physical) == (form == 8 ? uint32_t(source) : 0x7F800009));
                    }
                    ROMlib_ppc_translation_cache_destroy(cache);
                    ++cases;
                }
        }
    for (bool negative : {false, true})
        for (unsigned rounding = 0; rounding < 4; ++rounding) {
            fixture f(ROMLIB_PPC_EXEC_JIT);
            f.state.mmu.msr = 0x2000;
            f.registers.pc = 0x1000;
            f.registers.gpr[4] = 0x2000;
            f.registers.fpscr = 0x4000 | rounding;
            uint64_t source = UINT64_C(0x36A8000000000000) | (uint64_t(negative) << 63);
            memcpy(&f.registers.fpr[3], &source, sizeof source);
            f.word(0x1000, 0xD0640000); // stfs f3,0(r4)
            auto* cache = ROMlib_ppc_translation_cache_create();
            assert(cache);
            f.env.translation_cache = cache;
            assert(ROMlib_ppc_translate_range_mode(cache, &f.ram[0x1000], 0x1000, 4, ROMLIB_PPC_TRANSLATE_750) == 1);
            f.run(1);
            bool round_up = rounding == 0 || rounding == (negative ? 3u : 2u);
            assert(f.word(0x2000) == ((uint32_t(negative) << 31) | (round_up ? 2 : 1)));
            assert(f.registers.fpscr == (0x4000 | rounding) && f.env.native_cache_hits == 1 &&
                   !f.env.native_kernel_instructions && !f.env.native_compilations);
            ROMlib_ppc_translation_cache_destroy(cache);
            ++cases;
        }
    for (bool writing : {false, true})
        for (bool little : {false, true})
            for (bool summary_overflow : {false, true})
                for (unsigned scenario = 0; scenario < 6; ++scenario) {
                    fixture native(ROMLIB_PPC_EXEC_JIT), oracle(0);
                    uint32_t opcode = writing ? 0x7C60212D : 0x7C802028; // stwcx. r3,0,r4 or lwarx r4,0,r4
                    uint32_t fetch = little ? 0x1004 : 0x1000;
                    uint32_t physical = little ? 0x20084 : 0x20080;
                    uint32_t ea = scenario == 1 ? 0x90000081 : 0x90000080;
                    bool no_reservation = scenario == 5;
                    for (auto* f : {&native, &oracle}) {
                        f->state.mmu.msr = 0x1010 | little;
                        f->state.mmu.sdr1 = 0x10000;
                        f->state.mmu.dbat[0][0] = scenario == 3 || (writing && no_reservation) ? 0 : 0x90000002;
                        f->state.mmu.dbat[0][1] = 0x20002;
                        f->state.reservation_valid = !no_reservation;
                        f->state.reservation_address = 0x20040;
                        f->registers.pc = 0x1000;
                        f->registers.gpr[0] = 0x40000000;
                        f->registers.gpr[3] = 0xAABBCCDD;
                        f->registers.gpr[4] = ea;
                        f->registers.cr = 0x975ABCDE;
                        f->registers.xer = (uint32_t(summary_overflow) << 31) | 0x60000000;
                        f->word(physical, 0x11223344);
                        f->word(fetch, opcode);
                        if (scenario == 2 || (writing && no_reservation))
                            f->state.dabr = ea | 7;
                        if (scenario == 4)
                            f->bad_data_address = physical;
                    }
                    auto* cache = ROMlib_ppc_translation_cache_create();
                    assert(cache);
                    native.env.translation_cache = cache;
                    assert(ROMlib_ppc_translate_range_mode(cache, &native.ram[fetch], 0x1000, 4,
                                                           ROMLIB_PPC_TRANSLATE_750) == 1);
                    native.run_direct_memory(1);
                    oracle.run(1);
                    assert(!memcmp(&native.registers, &oracle.registers, sizeof native.registers));
                    assert(!memcmp(&native.state, &oracle.state, sizeof native.state) && native.ram == oracle.ram);
                    unsigned attempted = scenario != 1;
                    assert(native.env.native_instructions == attempted && native.env.native_cache_hits == attempted &&
                           !native.env.native_kernel_instructions && !native.env.native_compilations);
                    const uint32_t expected_pc[] = {0x1004, 0x600, 0x300, 0x300, 0x200, 0x1004};
                    bool retired = expected_pc[scenario] == 0x1004;
                    assert(native.registers.pc == expected_pc[scenario]);
                    if (writing && retired) {
                        uint32_t field = (no_reservation ? 0 : 0x20000000) | (uint32_t(summary_overflow) << 28);
                        assert(native.registers.cr == (0x075ABCDE | field) && !native.state.reservation_valid);
                        assert(native.word(physical) == (no_reservation ? 0x11223344u : 0xAABBCCDDu));
                        if (no_reservation)
                            assert(!native.data_transfers && !native.tables && !native.updates);
                    }
                    else {
                        assert(native.registers.cr == 0x975ABCDE);
                        if (!writing)
                            assert(native.registers.gpr[4] == (retired ? 0x11223344u : ea));
                    }
                    ROMlib_ppc_translation_cache_destroy(cache);
                    ++cases;
                }

    const struct {
        uint32_t opcode;
        unsigned reg, bytes;
        bool write, indexed;
    } register_transfers[] = {
        {0xBBA40000, 29, 12, false, false}, // lmw r29,0(r4)
        {0xBFA40000, 29, 12, true, false},  // stmw r29,0(r4)
        {0x7FE43CAA, 31, 7, false, false},  // lswi r31,r4,7
        {0x7FE43DAA, 31, 7, true, false},   // stswi r31,r4,7
        {0x7FE4342A, 31, 7, false, true},   // lswx r31,r4,r6
        {0x7FE4352A, 31, 7, true, true},    // stswx r31,r4,r6
    };

    for (const auto& test : register_transfers)
        for (unsigned scenario = 0; scenario < (test.indexed ? 5u : 4u); ++scenario) {
            fixture native(ROMLIB_PPC_EXEC_JIT), oracle(0);
            bool multiple = test.bytes == 12;
            bool little = scenario == 3;
            bool empty = scenario == 4;
            uint32_t fetch = little ? 0x1004 : 0x1000;
            const uint32_t initial[] = {0x11223344, 0x55667788, 0x99AABBCC};
            const uint32_t sources[] = {0xA1B2C3D4, 0xE5F60718, 0x293A4B5C};
            unsigned fault_offset = scenario == 1 ? 4 : multiple ? 8 : 6;
            for (auto* f : {&native, &oracle}) {
                f->state.mmu.msr = 0x1000 | little;
                f->registers.pc = 0x1000;
                f->registers.xer = 0xA0000000 | (empty ? 0 : test.bytes);
                f->registers.gpr[4] = test.indexed ? 0x2010 : 0x2000;
                f->registers.gpr[6] = 0xFFFFFFF0;
                for (unsigned i = 0; i < 3; ++i) {
                    f->registers.gpr[(test.reg + i) & 31] = sources[i];
                    f->word(0x2000 + i * 4, initial[i]);
                }
                f->word(fetch, test.opcode);
                if (scenario == 1 || scenario == 2)
                    f->bad_data_address = 0x2000 + fault_offset;
            }
            auto* cache = ROMlib_ppc_translation_cache_create();
            assert(cache);
            native.env.translation_cache = cache;
            assert(ROMlib_ppc_translate_range_mode(cache, &native.ram[fetch], 0x1000, 4, ROMLIB_PPC_TRANSLATE_750) ==
                   1);
            native.run_direct_memory(1);
            oracle.run(1);
            assert(!memcmp(&native.registers, &oracle.registers, sizeof native.registers));
            assert(!memcmp(&native.state, &oracle.state, sizeof native.state) && native.ram == oracle.ram);
            unsigned attempted = !little;
            assert(native.env.native_instructions == attempted && native.env.native_cache_hits == attempted &&
                   !native.env.native_kernel_instructions && !native.env.native_compilations);
            const uint32_t expected_pc[] = {0x1004, 0x200, 0x200, 0x600, 0x1004};
            assert(native.registers.pc == expected_pc[scenario] &&
                   native.registers.xer == (0xA0000000u | (empty ? 0 : test.bytes)));
            unsigned committed = little || empty ? 0 : scenario ? fault_offset : test.bytes;
            if (test.write) {
                for (unsigned i = 0; i < 12; ++i) {
                    uint32_t word = i < committed ? sources[i / 4] : initial[i / 4];
                    assert(native.ram[0x2000 + i] == (word >> (24 - 8 * (i % 4)) & 255));
                }
            }
            else {
                unsigned words = scenario ? committed / 4 : (committed + 3) / 4;
                for (unsigned i = 0; i < 3; ++i) {
                    uint32_t expected = i < words ? initial[i] : sources[i];
                    if (!scenario && !multiple && i == 1)
                        expected &= 0xFFFFFF00;
                    assert(native.registers.gpr[(test.reg + i) & 31] == expected);
                }
            }
            ROMlib_ppc_translation_cache_destroy(cache);
            ++cases;
        }

    // The first physical callback arms a breakpoint in the next granule.
    // Successful prefixes stay committed; a three-byte load tail stays atomic.
    for (unsigned kind = 0; kind < 4; ++kind) {
        fixture native(ROMLIB_PPC_EXEC_JIT), oracle(0);
        const uint32_t opcodes[] = {0xBBA40000, 0xBFA40000, 0x7FE41CAA, 0x7FE41DAA};
        uint32_t address = kind < 2 ? 0x2004 : kind == 2 ? 0x2006 : 0x2007;
        for (auto* f : {&native, &oracle}) {
            f->state.mmu.msr = 0x1000;
            f->registers.pc = 0x1000;
            f->registers.gpr[4] = address;
            for (unsigned i = 29; i < 32; ++i)
                f->registers.gpr[i] = 0xA1B2C3D4 + i;
            for (unsigned i = 0; i < 24; ++i)
                f->ram[0x2000 + i] = i + 1;
            f->word(0x1000, opcodes[kind]);
            f->arm_dabr_after_data = 0x200B; // Data read/write, untranslated context.
        }
        auto* cache = ROMlib_ppc_translation_cache_create();
        assert(cache);
        native.env.translation_cache = cache;
        assert(ROMlib_ppc_translate_range_mode(cache, &native.ram[0x1000], 0x1000, 4, ROMLIB_PPC_TRANSLATE_750) == 1);
        native.run_direct_memory(1);
        oracle.run(1);
        assert(native.registers.pc == 0x300 && native.state.dar == 0x2008);
        assert(!memcmp(&native.registers, &oracle.registers, sizeof native.registers));
        assert(!memcmp(&native.state, &oracle.state, sizeof native.state) && native.ram == oracle.ram);
        assert(native.data_transfers == 1 && oracle.data_transfers == 1);
        assert(native.env.native_instructions == 1 && !native.env.native_kernel_instructions);
        ROMlib_ppc_translation_cache_destroy(cache);
        ++cases;
    }
    for (unsigned guard = 0; guard < 6; ++guard) {
        fixture native(ROMLIB_PPC_EXEC_JIT), oracle(0);
        uint32_t opcode = guard == 3 ? 0xFC61102A : guard == 4 ? 0xC8640001 : guard == 5 ? 0xFC20102C : 0x38630001;
        for (auto* f : {&native, &oracle}) {
            f->registers.pc = 0x1000;
            f->registers.gpr[4] = 0x2000;
            f->word(0x1000, opcode);
            if (guard == 0) {
                f->state.mmu.msr = 0x8000;
                ROMlib_ppc_750_set_inputs(&f->state, ROMLIB_PPC_750_INT);
            }
            if (guard == 1)
                f->state.iabr = 0x1002;
            if (guard == 2) {
                f->state.mmu.msr = 0x20;
                f->state.mmu.ibat[0][0] = 2;
            }
            if (guard == 4)
                f->state.mmu.msr = 0x2000;
        }
        auto* cache = ROMlib_ppc_translation_cache_create();
        native.env.translation_cache = cache;
        assert(ROMlib_ppc_translate_range_mode(cache, &native.ram[0x1000], 0x1000, 4, ROMLIB_PPC_TRANSLATE_750) ==
               unsigned(guard != 5)); // The optional FSQRT operation is unavailable on the 750.
        native.run(1);
        oracle.run(1);
        assert(!memcmp(&native.registers, &oracle.registers, sizeof native.registers));
        assert(!memcmp(&native.state, &oracle.state, sizeof native.state) && !native.env.native_instructions);
        ROMlib_ppc_translation_cache_destroy(cache);
        ++cases;
    }
    for (unsigned fe : {0u, 0x800u, 0x100u, 0x900u, 0x400u}) {
        fixture native(ROMLIB_PPC_EXEC_JIT), oracle(0);
        for (auto* f : {&native, &oracle}) {
            f->floating(0xFC61102A, 0x80, UINT64_C(0x7FF0000000000001), UINT64_C(0x3FF0000000000000));
            f->state.mmu.msr |= fe;
        }
        auto* cache = ROMlib_ppc_translation_cache_create();
        native.env.translation_cache = cache;
        assert(ROMlib_ppc_translate_range_mode(cache, &native.ram[0x1000], 0x1000, 4, ROMLIB_PPC_TRANSLATE_750) == 1);
        native.run(1);
        oracle.run(1);
        assert(!memcmp(&native.registers, &oracle.registers, sizeof native.registers));
        assert(!memcmp(&native.state, &oracle.state, sizeof native.state));
        assert(native.env.native_instructions == 1 && native.env.native_kernel_instructions == 1);
        ROMlib_ppc_translation_cache_destroy(cache);
        ++cases;
    }
    for (bool nested : {false, true}) {
        fixture f(ROMLIB_PPC_EXEC_JIT);
        f.registers.pc = 0x1000;
        f.registers.gpr[4] = 0x2000;
        f.word(0x1000, 0x80640000);
        f.word(0x2000, 0x12345678);
        auto* cache = ROMlib_ppc_translation_cache_create();
        f.env.translation_cache = cache;
        assert(ROMlib_ppc_translate_range_mode(cache, &f.ram[0x1000], 0x1000, 4, ROMLIB_PPC_TRANSLATE_750) == 1);
        if (nested) {
            f.nested_native = true;
            f.run(1);
            assert(f.registers.gpr[3] == 0x12345678 && !f.nested_native && f.word(0x1000) == 0x38630001);
        }
        else {
            f.throwing_data_address = 0x2000;
            f.env.instruction_limit = 1;
            assert(!ROMlib_ppc_execute(&f.registers, &f.env));
            assert(f.throwing_data_count == 1 && f.env.instructions == 1 && !f.registers.gpr[3]);
        }
        assert(f.env.native_instructions == 1 && !f.env.native_kernel_instructions);
        ROMlib_ppc_translation_cache_destroy(cache);
        ++cases;
    }
    return cases;
}

static void run_guest_image(const char* path, unsigned flags)
{
    std::ifstream input(path, std::ios::binary);
    assert(input);
    std::vector<char> image_bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    fixture f(flags);
    ROMlib_pef_environment loader = {};
    loader.opaque = &f;
    loader.allocate = fixture::allocate;
    loader.release = [](void*, void*) {};
    loader.resolve = [](void*, const char*, const char*, unsigned, int, uint32_t*) { return 0; };
    auto* image = ROMlib_pef_load(image_bytes.data(), image_bytes.size(), &loader);
    assert(image);
    uint32_t tv = ROMlib_pef_init(image);
    assert(tv);
    f.registers.pc = f.word(tv);
    f.registers.gpr[2] = f.word(tv + 4);
    f.registers.gpr[1] = 0x80000;
    f.registers.lr = 0x1000;
    f.state.mmu.msr = 0x2000;
    f.env.system_service_gate = fixture::gate;
    f.env.opaque = &f;
    f.env.power_wait = [](void* opaque, int waiting) {
        auto& f = *static_cast<fixture*>(opaque);
        ROMlib_ppc_750_set_qack(&f.state, waiting && ROMlib_ppc_750_qreq(&f.state));
        if (waiting)
            ROMlib_ppc_750_advance(&f.state, 32);
        return 1;
    };
    if (strstr(path, "Thermal"))
        f.state.decrementer_pending = 1; // ROMAW can have a masked DEC request before the test enables EE.
    f.env.instruction_limit = 1000;
    f.core_ticks_per_fetch = 1;
    f.word(0x1000, 0x18000000);
    assert(ROMlib_ppc_execute(&f.registers, &f.env));
    assert(!f.registers.gpr[3] && f.state.mmu.msr == 0x2000 && f.registers.pc == 0x1004);
    assert(!f.state.iabr && !f.state.iabr_pending && !f.state.dabr && !f.state.hid0);
    assert(!f.state.reservation_valid && !f.state.bus_check_pending);
    assert(!f.state.thermal_pending && !f.state.thermal_halted && !f.state.thrm[0] && !f.state.thrm[1] &&
           !f.state.thrm[2]);
    for (uint32_t a = 0x1700; a < 0x1718; a += 4)
        assert(!f.word(a));
    assert(!f.word(0x900));
    for (const auto& bank : f.state.cache)
        for (const auto& set : bank.lines)
            for (const auto& line : set)
                assert(line.state != 2);
    for (uint32_t a = 0x300; a < 0x310; a += 4)
        assert(!f.word(a));
    for (uint32_t a = 0x700; a < 0x714; a += 4)
        assert(!f.word(a));
    for (uint32_t a = 0x1300; a < 0x1314; a += 4)
        assert(!f.word(a));
    for (auto& bat : f.state.mmu.dbat)
        assert(!bat[0] && !bat[1]);
    ROMlib_pef_unload(image);
}

static unsigned run_cpu_cases()
{
    unsigned cases = native_execution();
    for (unsigned flags : {0u, unsigned(ROMLIB_PPC_EXEC_JIT)}) {
        fixture f(flags);
        f.registers.pc = 0x1000;
        f.throwing_data_address = 0x1000; // An actual exception during instruction fetch.
        f.env.instruction_limit = 1;
        assert(!ROMlib_ppc_execute(&f.registers, &f.env));
        assert(f.throwing_data_count == 1 && f.env.instructions == 1 && f.registers.pc == 0x1000);
        assert(!f.state.checkstop && !f.state.srr0 && !f.state.srr1 && !f.state.bus_check_pending);
        ++cases;
    }
    for (unsigned flags : {0u, unsigned(ROMLIB_PPC_EXEC_JIT)}) {
        cases += thermal_execution(flags);
        cases += performance_execution(flags);
        cases += cache_performance(flags);
        cases += l2_execution(flags);
        cases += invalid_forms(flags);
        cases += cache_execution(flags);
        cases += reservation_snoops(flags);
        for (bool translated : {false, true})
            for (bool user : {false, true})
                for (bool little : {false, true})
                    for (bool writing : {false, true})
                        for (unsigned control = 0; control < 8; ++control)
                            for (uint32_t watched : {0x2000u, 0x2008u, 0x2010u}) {
                                fixture f(flags);
                                f.registers.pc = 0x1000;
                                f.registers.gpr[3] = 0x11223344;
                                f.registers.gpr[4] = 0x2000;
                                f.state.mmu.msr = (translated ? 0x10 : 0) | (user ? 0x4000 : 0) | little;
                                f.state.mmu.dbat[0][0] = 3;
                                f.state.mmu.dbat[0][1] = 2;
                                f.state.dabr = watched | control;
                                f.word(0x1000 ^ (little ? 4 : 0), writing ? 0x94640005 : 0x84640005);
                                for (unsigned i = 0; i < 24; ++i)
                                    f.ram[0x2000 + i] = i + 1;
                                auto before = f.ram;
                                f.run(1);
                                bool match = watched != 0x2010 && bool(control & 4) == translated &&
                                             (control & (writing ? 2 : 1));
                                if (match) {
                                    assert(f.registers.pc == 0x300 && f.state.srr0 == 0x1000);
                                    assert(f.state.dar == 0x2005 &&
                                           f.state.dsisr == (writing ? 0x02400000u : 0x00400000u));
                                    assert(f.registers.gpr[3] == 0x11223344 && f.registers.gpr[4] == 0x2000);
                                    assert(!f.data_transfers && f.ram == before);
                                }
                                else {
                                    assert(f.registers.pc == 0x1004 && f.registers.gpr[4] == 0x2005);
                                    assert(f.data_transfers && !f.state.srr0);
                                }
                                ++cases;
                            }
        for (bool writing : {false, true})
            for (bool user : {false, true})
                for (uint32_t value : {0u, 0xFFFFFFFFu, 0xA2000007u}) {
                    fixture f(flags);
                    f.registers.pc = 0x1000;
                    f.registers.gpr[3] = value;
                    f.state.dabr = 0x12345678;
                    f.state.mmu.msr = user ? 0x4000 : 0;
                    f.word(0x1000, mtspr(1013, 3) ^ (writing ? 0 : 0x100));
                    f.run(1);
                    if (user) {
                        assert(f.registers.pc == 0x700 && f.state.srr1 == 0x44000);
                        assert(f.state.dabr == 0x12345678 && f.registers.gpr[3] == value);
                    }
                    else {
                        assert(f.registers.pc == 0x1004);
                        assert(f.state.dabr == (writing ? value : 0x12345678));
                        assert(f.registers.gpr[3] == (writing ? value : 0x12345678));
                    }
                    ++cases;
                }
        for (bool writing : {false, true})
            for (unsigned mapping = 0; mapping < 6; ++mapping) {
                fixture f(flags);
                f.registers.pc = 0x1000;
                f.registers.gpr[3] = 0x11223344;
                f.registers.gpr[4] = 0x90000080;
                f.state.mmu.msr = 0x1010;
                f.state.dabr = 0x90000084 | (writing ? 2 : 1);
                f.state.mmu.sdr1 = mapping == 5 ? 0x200000 : 0x10000;
                f.state.mmu.sr[9] = mapping == 2 ? 0x80000000 : 0x40000023;
                if (mapping < 2) {
                    f.state.mmu.dbat[0][0] = 0x90000002;
                    f.state.mmu.dbat[0][1] = mapping ? 0 : 2;
                }
                if (mapping == 3) {
                    f.word(0x108C0, 0x80001180); // VSID 0x23, page 0; key=1, PP=0.
                    f.word(0x108C4, 0x30000);
                }
                f.word(0x1000, writing ? 0x90640000 : 0x80640000);
                f.run(1);
                assert(f.registers.pc == (mapping == 5 ? 0x200u : 0x300u));
                if (mapping != 5) {
                    uint32_t cause = mapping == 0   ? 0x00400000
                                     : mapping == 2 ? 0x04000000
                                     : mapping == 4 ? 0x40000000
                                                    : 0x08000000;
                    if (f.state.dsisr != (cause | (writing ? 0x02000000 : 0)))
                        fprintf(stderr, "DABR mapping %u write %u: DSISR %08x expected %08x\n", mapping, writing,
                                f.state.dsisr, cause | (writing ? 0x02000000 : 0));
                    assert(f.state.dsisr == (cause | (writing ? 0x02000000 : 0)));
                    assert(f.state.dar == 0x90000080);
                }
                assert(f.registers.gpr[3] == 0x11223344 && !f.data_transfers);
                ++cases;
            }
        for (bool writing : {false, true}) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[3] = 0x11223344;
            f.registers.gpr[4] = 0x90000FFF;
            f.state.mmu.msr = 0x10;
            f.state.mmu.sdr1 = 0x10000;
            f.state.mmu.sr[9] = 0x40000023;
            f.state.dabr = 0x90000FFC | (writing ? 2 : 1);
            f.word(0x108C0, 0x80001180);
            f.word(0x108C4, 0x30002);
            f.word(0x10880, 0x80001180);
            f.word(0x10884, 0x31000); // The second page is protected.
            f.word(0x1000, writing ? 0x90640000 : 0x80640000);
            f.run(1);
            assert(f.registers.pc == 0x300 && f.state.dar == 0x90001000);
            assert(f.state.dsisr == (writing ? 0x0A000000u : 0x08000000u));
            assert(!f.data_transfers && f.registers.gpr[3] == 0x11223344);
            ++cases;
        }

        struct watched_operation {
            uint32_t opcode, address;
        };

        for (watched_operation operation : {watched_operation{0x88640000, 0x2008}, // lbz
                                            {0xA0640000, 0x2007},                  // lhz, crosses doubleword
                                            {0x80640000, 0x2005},                  // lwz
                                            {0xC8640000, 0x2004},                  // lfd, aligned to a word
                                            {0x98640000, 0x2008},
                                            {0xB0640000, 0x2007},
                                            {0x90640000, 0x2005},
                                            {0xD8640000, 0x2004},
                                            {0x7C642C2C, 0x2005}, // lwbrx
                                            {0x7C642D2C, 0x2005}, // stwbrx
                                            {0x7C642828, 0x2008}, // lwarx
                                            {0xB8A40000, 0x2008},
                                            {0xBC640000, 0x2008}, // lmw/stmw
                                            {0x7CA444AA, 0x2008},
                                            {0x7C6445AA, 0x2008}}) { // lswi/stswi
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[3] = 0x11223344;
            f.registers.gpr[4] = operation.address;
            f.registers.cr = 0xDEADBEEF;
            f.registers.fpr[3] = 123.5;
            f.state.mmu.msr = 0x2000;
            f.state.dabr = 0x200B;
            f.word(0x1000, operation.opcode);
            f.run(1);
            assert(f.registers.pc == 0x300 && f.state.srr0 == 0x1000 && f.state.dar == operation.address);
            assert((f.state.dsisr & ~0x02000000u) == 0x00400000);
            assert(!f.data_transfers && f.registers.gpr[3] == 0x11223344 && f.registers.fpr[3] == 123.5);
            assert(f.registers.cr == 0xDEADBEEF);
            ++cases;
        }
        for (uint32_t watched : {1u, 0xFFFFFFF9u}) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[4] = 0xFFFFFFFF;
            f.state.dabr = watched;
            f.word(0x1000, 0x80640000);
            f.run(1);
            assert(f.registers.pc == 0x300 && f.state.dar == 0xFFFFFFFF && f.state.dsisr == 0x00400000);
            assert(!f.data_transfers);
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.state.dabr = 0x1001; // Instruction fetch is never watched.
            f.word(0x1000, 0x38601234);
            f.run(1);
            assert(f.registers.pc == 0x1004 && f.registers.gpr[3] == 0x1234);
            ++cases;
        }
        {
            fixture f(flags);
            f.mapped();
            f.state.dabr = 0x20085; // Physical alias is not the effective address.
            f.run(1);
            assert(f.registers.pc == 0x80001004 && f.registers.gpr[3] == 0x1234ABCD);
            ++cases;
        }
        for (bool writing : {false, true}) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[4] = 0x90000080;
            f.state.mmu.msr = 0x10;
            f.state.mmu.sdr1 = 0x10000;
            f.state.mmu.sr[9] = 0x23;
            f.state.dabr = 0x90000084 | (writing ? 2 : 1);
            f.word(0x108C0, 0x80001180);
            f.word(0x108C4, 0x30002);
            f.word(0x1000, writing ? 0x90640000 : 0x80640000);
            f.run(1);
            assert(f.registers.pc == 0x300 && !f.data_transfers);
            assert(f.word(0x108C4) == (writing ? 0x30182u : 0x30102u));
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[4] = 0x2000;
            f.state.dabr = 0x2001;
            f.word(0x1000, 0x80640000);
            f.word(0x300, mtspr(1013, 5));
            f.word(0x304, 0x4C000064);
            f.word(0x2000, 0x12345678);
            f.run(1);
            assert(f.registers.pc == 0x300 && f.state.dabr == 0x2001 && !f.data_transfers);
            f.run(3); // A replacement CPU continues the handler and retries.
            assert(f.registers.pc == 0x1004 && f.registers.gpr[3] == 0x12345678);
            assert(!f.state.dabr && f.data_transfers == 1);
            ++cases;
        }

        for (bool fp : {false, true}) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[4] = 0x2005;
            f.state.mmu.msr = fp ? 0x2000 : 0;
            f.state.dabr = 0x2003;
            f.word(0x1000, 0xC8640000);
            f.run(1);
            assert(f.registers.pc == (fp ? 0x600u : 0x800u) && !f.data_transfers);
            ++cases;
        }
        for (uint32_t opcode : {0x7C64292Du, 0x7CA4252Au}) { // Failed stwcx.; zero-length stswx.
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[4] = 0x2000;
            f.state.dabr = 0x2003;
            f.word(0x1000, opcode);
            f.run(1);
            assert(f.registers.pc == 0x1004 && !f.data_transfers && !f.state.srr0);
            ++cases;
        }

        for (unsigned half : {0u, 1u})
            for (bool user : {false, true}) {
                for (bool tbr : {false, true}) {
                    fixture f(flags);
                    f.registers.pc = 0x1000;
                    f.state.mmu.msr = user ? 0x4000 : 0;
                    f.state.time_base = UINT64_C(0x1122334455667788);
                    uint32_t opcode = mtspr(268 + half, 3) & ~0x100u;
                    if (tbr)
                        opcode |= 0x40;
                    f.word(0x1000, opcode);
                    f.run(1);
                    assert(f.registers.pc == 0x1004 && f.registers.gpr[3] == (half ? 0x11223344u : 0x55667788u));
                    ++cases;
                }
                {
                    fixture f(flags);
                    f.registers.pc = 0x1000;
                    f.registers.gpr[3] = 0xAABBCCDD;
                    f.state.mmu.msr = user ? 0x4000 : 0;
                    f.state.time_base = UINT64_C(0x1122334455667788);
                    f.word(0x1000, mtspr(268 + half, 3)); // Read selectors cannot be written.
                    f.run(1);
                    assert(f.registers.pc == 0x700 && f.state.srr1 == (user ? 0x84000u : 0x80000u));
                    assert(f.state.time_base == UINT64_C(0x1122334455667788));
                    ++cases;
                }
                {
                    fixture f(flags);
                    f.registers.pc = 0x1000;
                    f.registers.gpr[3] = 0xAABBCCDD;
                    f.state.mmu.msr = user ? 0x4000 : 0;
                    f.state.time_base = UINT64_C(0x1122334455667788);
                    f.word(0x1000, mtspr(284 + half, 3));
                    f.run(1);
                    if (user) {
                        assert(f.registers.pc == 0x700 && f.state.srr1 == 0x44000);
                        assert(f.state.time_base == UINT64_C(0x1122334455667788));
                    }
                    else {
                        assert(f.registers.pc == 0x1004);
                        assert(f.state.time_base ==
                               (half ? UINT64_C(0xAABBCCDD55667788) : UINT64_C(0x11223344AABBCCDD)));
                    }
                    ++cases;
                }
                for (bool tbr : {false, true}) {
                    fixture f(flags);
                    f.registers.pc = 0x1000;
                    f.state.mmu.msr = user ? 0x4000 : 0;
                    f.state.time_base = UINT64_C(0x1122334455667788);
                    uint32_t opcode = mtspr(284 + half, 3) & ~0x100u;
                    if (tbr)
                        opcode |= 0x40;
                    f.word(0x1000, opcode);
                    f.run(1);
                    assert(f.registers.pc == 0x700);
                    assert(f.state.srr1 == (user ? (tbr ? 0x84000u : 0x44000u) : 0x80000u));
                    assert(f.state.time_base == UINT64_C(0x1122334455667788));
                    ++cases;
                }
            }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[3] = 0xAABBCCDD;
            f.state.time_base = UINT64_C(0x1122334455667788);
            f.state.decrementer = 5;
            ROMlib_ppc_750_set_tben(&f.state, 0);
            f.word(0x1000, mtspr(284, 3));
            f.word(0x1004, mtspr(268, 4) & ~0x100u);
            f.word(0x1008, (mtspr(269, 5) & ~0x100u) | 0x40);
            f.word(0x100C, mtspr(268, 6) & ~0x100u);
            f.word(0x1010, mtspr(22, 7) & ~0x100u);
            f.run(3);
            assert(f.registers.gpr[4] == 0xAABBCCDD && f.registers.gpr[5] == 0x11223344);
            ROMlib_ppc_750_advance(&f.state, 1);
            f.run(1);
            assert(f.registers.gpr[6] == 0xAABBCCDD && f.state.decrementer == 4);
            ROMlib_ppc_750_set_tben(&f.state, 1);
            ROMlib_ppc_750_advance(&f.state, 1);
            f.registers.pc = 0x100C;
            f.run(2);
            assert(f.registers.gpr[6] == 0xAABBCCDE && f.registers.gpr[7] == 3);
            ++cases;
        }
        for (bool translated : {false, true})
            for (bool user : {false, true})
                for (bool little : {false, true})
                    for (bool enabled : {false, true})
                        for (bool te : {false, true})
                            for (bool matching : {false, true}) {
                                fixture f(flags);
                                uint32_t pc = translated ? 0x80001000 : 0x1000;
                                f.registers.pc = pc;
                                f.registers.gpr[3] = 0x12345678;
                                f.state.mmu.msr = (translated ? 0x20 : 0) | (user ? 0x4000 : 0) | little;
                                f.state.mmu.ibat[0][0] = 0x80000003;
                                f.state.mmu.ibat[0][1] = 2;
                                f.state.iabr = (matching ? pc : pc + 4) | enabled * 2 | te;
                                f.word(0x1000 ^ (little ? 4 : 0), 0x38600001);
                                f.run(1);
                                bool hit = enabled && matching && te == translated;
                                assert(f.registers.pc == (hit ? 0x1300u : pc + 4));
                                assert(f.registers.gpr[3] == (hit ? 0x12345678u : 1u));
                                if (hit)
                                    assert(f.state.srr0 == pc && !f.state.iabr_pending);
                                ++cases;
                            }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.state.iabr = 0x1002;
            f.word(0x1000, 0); // IABR precedes illegal instruction dispatch.
            f.run(1);
            assert(f.registers.pc == 0x1300 && f.state.srr0 == 0x1000);
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.state.iabr = 0x1002;
            f.word(0x1000, 0x3860002A);
            f.word(0x1300, 0x38800000);
            f.word(0x1304, mtspr(1010, 4));
            f.word(0x1308, 0x4C00012C);
            f.word(0x130C, 0x39EF0001);
            f.word(0x1310, 0x4C000064);
            f.run(7);
            assert(f.registers.pc == 0x1004 && f.registers.gpr[3] == 42 && f.registers.gpr[15] == 1);
            assert(f.state.srr0 == 0x1000 && !f.state.iabr && !f.state.iabr_pending);
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x80001000;
            f.state.mmu.msr = 0x20;
            f.state.iabr = 0x80001003;
            f.run(1); // ISI fetch miss precedes the instruction breakpoint.
            assert(f.registers.pc == 0x400 && f.state.srr0 == 0x80001000 && f.state.srr1 == 0x40000020);
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[3] = 0x1006;
            f.word(0x1000, mtspr(1010, 3));
            f.word(0x1004, 0x4C00012C);
            f.word(0x1008, 0x4BFFFFFC);
            f.run(4); // First ISYNC cannot match the immediately preceding enable.
            assert(f.registers.pc == 0x1300 && f.state.srr0 == 0x1004 && f.state.iabr == 0x1006);
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.state.iabr = 0x1006;
            f.word(0x1000, mtspr(1010, 3)); // Disable, but the immediately following ISYNC still matches old IABR.
            f.word(0x1004, 0x4C00012C);
            f.run(2);
            assert(f.registers.pc == 0x1300 && f.state.srr0 == 0x1004 && !f.state.iabr && !f.state.iabr_pending);
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[3] = 0x100A;
            f.word(0x1000, mtspr(1010, 3));
            f.word(0x1004, 0x7C0004AC); // SYNC is execution-synchronizing, not context-synchronizing.
            f.word(0x1008, 0x4C00012C);
            f.word(0x100C, 0x4BFFFFFC);
            f.run(5);
            assert(f.registers.pc == 0x1300 && f.state.srr0 == 0x1008);
            ++cases;
        }
        for (bool rfi : {false, true}) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            uint32_t target = rfi ? 0x2000 : 0xC00;
            f.registers.gpr[3] = target | 2;
            f.state.srr0 = target;
            f.word(0x1000, mtspr(1010, 3));
            f.word(0x1004, rfi ? 0x4C000064 : 0x44000002);
            f.word(target, 0x38800063);
            f.run(3);
            assert(f.registers.pc == 0x1300 && f.state.srr0 == target && !f.registers.gpr[4]);
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[3] = 0x2002;
            f.word(0x1000, mtspr(1010, 3));
            f.word(0x1004, mtspr(1010, 4) & ~0x100u);
            f.run(2);
            assert(f.registers.gpr[4] == 0x2002 && f.state.iabr_pending && !f.state.iabr_previous);
            ++cases;
        }
        for (bool writing : {false, true}) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.state.mmu.msr = 0x4000;
            f.word(0x1000, writing ? mtspr(1010, 3) : mtspr(1010, 3) & ~0x100u);
            f.run(1);
            assert(f.registers.pc == 0x700 && f.state.srr1 == 0x44000 && !f.state.iabr);
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.state.mmu.msr = 0x8000;
            f.state.iabr = 0x502;
            f.state.iabr_pending = 1;
            ROMlib_ppc_750_set_inputs(&f.state, ROMLIB_PPC_750_INT);
            f.run(2);
            assert(f.registers.pc == 0x1300 && f.state.srr0 == 0x500 && !f.state.iabr_pending);
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[3] = 0x1006;
            f.word(0x1000, mtspr(1010, 3));
            f.word(0x1004, 0x4C00012C);
            f.word(0x1008, 0x4BFFFFFC);
            f.run(1);
            assert(f.registers.pc == 0x1004 && f.state.iabr_pending);
            f.run(1); // Replacing the CPU object must retain the unsynchronized context.
            assert(f.registers.pc == 0x1008 && !f.state.iabr_pending);
            f.run(2);
            assert(f.registers.pc == 0x1300 && f.state.srr0 == 0x1004);
            ++cases;
        }
        const uint32_t external_opcodes[2][2] = {{0x7C642A6C, 0x7C642B6C}, {0x7C602A6C, 0x7C602B6C}};
        auto* external_cache = flags && ROMlib_ppc_jit_available() ? ROMlib_ppc_translation_cache_create() : nullptr;
        if (external_cache) {
            fixture code(flags);
            for (const auto& pair : external_opcodes)
                for (uint32_t opcode : pair) {
                    code.word(0x1000, opcode);
                    assert(ROMlib_ppc_translate_range_mode(external_cache, &code.ram[0x1000], 0x1000, 4,
                                                           ROMLIB_PPC_TRANSLATE_750) == 1);
                }
        }
        for (bool little : {false, true})
            for (bool user : {false, true})
                for (bool writing : {false, true})
                    for (unsigned protection = 0; protection < 4; ++protection)
                        for (unsigned resource = 0; resource < 16; ++resource) {
                            fixture f(flags);
                            f.env.translation_cache = external_cache;
                            f.registers.pc = 0x1000;
                            f.registers.gpr[0] = 0x11111111;
                            f.registers.gpr[3] = 0x12345678;
                            f.registers.gpr[4] = 0x900000A0;
                            f.registers.gpr[5] = user ? 0x90000080 : 0xFFFFFFE0;
                            f.state.mmu.msr = 0x10 | little | (user ? 0x4000 : 0);
                            f.state.mmu.dbat[0][0] = 0x90000003;
                            f.state.mmu.dbat[0][1] = 0x20000 | protection;
                            f.state.ear = 0x80000000 | resource;
                            f.state.dabr = 0x90000087; // Undefined external-control matches are chosen absent.
                            uint32_t opcode = external_opcodes[user][writing];
                            f.word(0x1000 ^ (little ? 4 : 0), opcode);
                            if (external_cache)
                                f.run_direct_memory(1);
                            else
                                f.run(1);
                            bool allowed = writing ? protection == 2 : protection != 0;
                            if (allowed) {
                                assert(f.registers.pc == 0x1004 && f.data_transfers == 1);
                                assert(f.device_address == 0x20080 && f.device_wimg == 5);
                                assert(f.device_access ==
                                       (ROMLIB_PPC_PHYSICAL_EXTERNAL | resource << ROMLIB_PPC_PHYSICAL_RESOURCE_SHIFT |
                                        (writing ? ROMLIB_PPC_MMU_WRITE : 0)));
                                assert(f.registers.gpr[3] == (writing ? 0x12345678u : 0xCAFEBABEu));
                                assert(f.device_word == (writing ? 0x12345678u : 0xCAFEBABEu));
                            }
                            else {
                                assert(f.registers.pc == 0x300 && !f.data_transfers);
                                assert(f.state.dar == 0x90000080);
                                assert(f.state.dsisr == (0x08000000u | (writing ? 0x02000000 : 0)));
                                assert(f.registers.gpr[3] == 0x12345678);
                            }
                            unsigned native = external_cache != nullptr;
                            assert(f.env.native_instructions == native && f.env.native_cache_hits == native &&
                                   !f.env.native_kernel_instructions && !f.env.native_compilations);
                            ++cases;
                        }
        ROMlib_ppc_translation_cache_destroy(external_cache);
        for (bool writing : {false, true})
            for (unsigned condition = 0; condition < 5; ++condition) {
                fixture f(flags);
                bool success = condition == 4;
                f.registers.pc = 0x1000;
                f.registers.gpr[3] = 0x12345678;
                f.registers.gpr[4] = condition == 2 || success ? 0x90000080 : 0x90000081;
                f.state.mmu.msr = 0x1010;
                f.state.mmu.dbat[0][0] = 0x90000002;
                f.state.mmu.dbat[0][1] = 0x20002;
                f.state.ear = condition ? 0x80000000 : 0;
                f.fail_device = condition == 2;
                bool reserved = condition == 3;
                f.word(0x1000, (writing ? 0x7C642B6Cu : 0x7C642A6Cu) | unsigned(reserved));
                auto* cache = flags && ROMlib_ppc_jit_available() ? ROMlib_ppc_translation_cache_create() : nullptr;
                if (cache) {
                    f.env.translation_cache = cache;
                    assert(ROMlib_ppc_translate_range_mode(cache, &f.ram[0x1000], 0x1000, 4,
                                                           ROMLIB_PPC_TRANSLATE_750) == unsigned(!reserved));
                }
                f.run(1);
                assert(f.registers.gpr[3] == (success && !writing ? 0xCAFEBABEu : 0x12345678u));
                if (reserved) {
                    assert(f.registers.pc == 0x700 && f.state.srr0 == 0x1000 && f.state.srr1 == 0x81010);
                    assert(!f.data_transfers && !f.tables);
                }
                else if (!condition) {
                    assert(f.registers.pc == 0x300 && !f.data_transfers && !f.tables);
                    assert(f.state.dsisr == (0x00100000u | (writing ? 0x02000000 : 0)));
                }
                else if (condition == 1) {
                    assert(f.registers.pc == 0x600 && !f.data_transfers);
                    assert(f.state.dsisr == (writing ? 0x15864u : 0x15064u));
                }
                else if (!success)
                    assert(f.registers.pc == 0x200 && f.state.srr1 == 0x21010 && f.data_transfers == 1);
                else {
                    assert(f.registers.pc == 0x1004 && f.data_transfers == 1);
                    assert(f.device_address == 0x20080 && f.device_wimg == 5);
                    assert(f.device_word == (writing ? 0x12345678u : 0xCAFEBABEu));
                }
                unsigned native = cache && !reserved;
                assert(f.env.native_instructions == native && f.env.native_cache_hits == native &&
                       !f.env.native_kernel_instructions && !f.env.native_compilations);
                ROMlib_ppc_translation_cache_destroy(cache);
                ++cases;
            }
        for (bool writing : {false, true}) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.word(0x1000, (writing ? 0x7C642B6C : 0x7C642A6C) | 1);
            f.run(1);
            assert(f.registers.pc == 0x700 && f.state.srr1 == 0x80000 && !f.data_transfers && !f.tables);
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[3] = 0xFFFFFFFF;
            f.word(0x1000, mtspr(282, 3));
            f.word(0x1004, mtspr(282, 4) & ~0x100u);
            f.run(2);
            assert(f.state.ear == 0x8000000F && f.registers.gpr[4] == 0x8000000F);
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.state.mmu.msr = 0x4000;
            f.word(0x1000, mtspr(282, 3));
            f.run(1);
            assert(f.registers.pc == 0x700 && f.state.srr1 == 0x44000 && !f.state.ear);
            ++cases;
        }
        for (bool writing : {false, true}) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[3] = 0x12345678;
            f.registers.gpr[4] = 0x90000080;
            f.state.mmu.msr = 0x10;
            f.state.mmu.sr[9] = 0x80000000;
            f.state.ear = 0x80000000;
            f.word(0x1000, writing ? 0x7C642B6C : 0x7C642A6C);
            f.run(1);
            assert(f.registers.pc == 0x300 && !f.data_transfers && !f.tables);
            assert(f.state.dsisr == (0x04000000u | (writing ? 0x02000000 : 0)));
            ++cases;
        }
        for (bool writing : {false, true}) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[3] = 0x12345678;
            f.registers.gpr[4] = 0x90000080;
            f.state.mmu.msr = 0x10;
            f.state.mmu.sdr1 = 0x10000;
            f.state.mmu.sr[9] = 0x23;
            f.state.ear = 0x80000003;
            uint32_t pte = 0x10000 | 0x23 << 6;
            f.word(pte, 0x80000000 | 0x23 << 7);
            f.word(pte + 4, 0x30012);
            f.word(0x1000, writing ? 0x7C642B6C : 0x7C642A6C);
            f.run(1);
            assert(f.registers.pc == 0x1004 && f.tables == 1 && f.updates == 1 && f.data_transfers == 1);
            assert(f.device_address == 0x30080 && f.device_wimg == 5);
            assert(f.word(pte + 4) == (writing ? 0x30192u : 0x30112u));
            ROMlib_ppc_bus_result query;
            assert(ROMlib_ppc_750_translate(&f.state, &f.bus, 0x90000080, 0, &query));
            assert(query.mapping.tlb_hit && query.mapping.wimg == 2); // Device override cannot contaminate the TLB.
            ++cases;
        }

        struct alignment_case {
            uint32_t opcode, status;
            bool little_only;
        };

        const alignment_case alignment_cases[] = {
            {0xC8640000, 0x2464, false},  // lfd
            {0xCC640000, 0x6464, false},  // lfdu
            {0xD8640000, 0x2C64, false},  // stfd
            {0xDC640000, 0x6C64, false},  // stfdu
            {0x7C642CAE, 0x1A464, false}, // lfdx
            {0x7C642CEE, 0x1E464, false}, // lfdux
            {0x7C642DAE, 0x1AC64, false}, // stfdx
            {0x7C642DEE, 0x1EC64, false}, // stfdux
            {0xB8640000, 0x1C64, false},  // lmw
            {0xBC640000, 0x5C64, false},  // stmw
            {0x7C642828, 0x64, false},    // lwarx
            {0x7C64292D, 0x10864, false}, // stwcx.
            {0x7C643CAA, 0xA464, true},   // lswi, NB=7 is not a register operand
            {0x7C643DAA, 0xAC64, true},   // stswi
            {0x7C642C2A, 0xA064, true},   // lswx
            {0x7C642D2A, 0xA864, true},   // stswx
            {0x7C042FEC, 0x17C04, false}, // dcbz with disabled D-cache
        };
        for (unsigned xo : {54u, 86u, 470u, 278u, 246u, 982u, 1014u})
            for (unsigned mapping = 0; mapping < 5; ++mapping) {
                fixture f(flags);
                f.registers.pc = 0x1000;
                f.registers.gpr[4] = 0x90002000;
                f.state.mmu.msr = 0x10;
                uint32_t opcode = 0x7C000000 | 4 << 16 | 5 << 11 | xo << 1;
                f.word(0x1000, opcode);
                if (mapping < 3) {
                    f.state.mmu.dbat[0][0] = 0x90000002;
                    f.state.mmu.dbat[0][1] = mapping;
                }
                if (mapping == 4)
                    f.state.mmu.sr[9] = 0x80000000; // T=1 cache controls are no-ops.
                f.run(1);
                bool hint = xo == 278 || xo == 246 || xo == 982;
                if (hint || mapping == 4) {
                    assert(f.registers.pc == 0x1004 && !f.tables);
                }
                else if (xo == 1014) {
                    assert(f.registers.pc == 0x600 && f.state.dar == 0x90002000 && !f.tables);
                }
                else if (mapping == 0 || (mapping == 1 && xo == 470) || mapping == 3) {
                    uint32_t cause = mapping == 3 ? 0x40000000 : 0x08000000;
                    if (xo == 470)
                        cause |= 0x02000000;
                    assert(f.registers.pc == 0x300 && f.state.dsisr == cause && f.state.dar == 0x90002000);
                }
                else
                    assert(f.registers.pc == 0x1004);
                assert(!f.data_transfers && !f.updates);
                ++cases;
            }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.state.mmu.msr = 0x4010;
            f.word(0x1000, 0x7C042BAC); // dcbi r4,r5 in user mode, before any translation.
            f.run(1);
            assert(f.registers.pc == 0x700 && f.state.srr1 == 0x44010 && !f.tables && !f.data_transfers);
            ++cases;
        }
        for (unsigned xo : {54u, 86u, 470u}) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[4] = 0x90000080;
            f.state.mmu.msr = 0x10;
            f.state.mmu.sdr1 = 0x10000;
            f.state.mmu.sr[9] = 0x23;
            uint32_t pte = 0x10000 | 0x23 << 6;
            f.word(pte, 0x80000000 | 0x23 << 7);
            f.word(pte + 4, 0x30002);
            f.word(0x1000, 0x7C000000 | 4 << 16 | 5 << 11 | xo << 1);
            f.run(1);
            assert(f.registers.pc == 0x1004 && f.tables == 1 && f.updates == 1 && !f.data_transfers);
            assert(f.word(pte + 4) == (xo == 470 ? 0x30182u : 0x30102u));
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[4] = 0x90000080;
            f.state.mmu.msr = 0x1010;
            f.state.mmu.sdr1 = 0x200000; // Physical PTE transfer fails, preserving TEA cause.
            f.word(0x1000, 0x7C04286C);  // dcbst r4,r5
            f.run(1);
            assert(f.registers.pc == 0x200 && f.state.srr1 == 0x41010 && !f.data_transfers);
            ++cases;
        }
        for (const auto& test : alignment_cases)
            for (bool little : {false, true}) {
                if (test.little_only && !little)
                    continue;
                fixture f(flags);
                f.registers.pc = 0x1000;
                f.registers.gpr[3] = 0x12345678;
                f.registers.gpr[4] = test.little_only ? 0x2000 : 0x2001;
                f.registers.gpr[7] = 0x7000;
                f.state.mmu.msr = 0x2000 | little;
                f.word(0x1000 ^ (little ? 4 : 0), test.opcode);
                ROMlib_ppc_registers before = f.registers;
                f.run(1);
                assert(f.registers.pc == 0x600 && f.state.srr0 == 0x1000);
                assert(f.state.dar == before.gpr[4] && f.state.dsisr == test.status);
                assert(!memcmp(f.registers.gpr, before.gpr, sizeof before.gpr));
                assert(!memcmp(f.registers.fpr, before.fpr, sizeof before.fpr));
                assert(f.fetches == 1 && !f.data_transfers && !f.tables);
                ++cases;
            }

        struct fp_case {
            uint32_t opcode, enable;
            uint64_t a, b;
            bool inhibits, inline_fp;
        };

        const fp_case floating_cases[] = {
            {0xFC611024, 0x10, UINT64_C(0x3FF0000000000000), 0, true, false}, // divide by zero
            {0xFC61102A, 0x80, UINT64_C(0x7FF0000000000042), UINT64_C(0x3FF0000000000000), true, false},  // SNaN
            {0xFC61102A, 0x40, UINT64_C(0x7FEFFFFFFFFFFFFF), UINT64_C(0x7FEFFFFFFFFFFFFF), false, false}, // overflow
            {0xFC611024, 0x20, UINT64_C(0x0010000000000000), UINT64_C(0x7FEFFFFFFFFFFFFF), false, false}, // underflow
            {0xFC61102A, 8, UINT64_C(0x3FF0000000000000), UINT64_C(0x3C90000000000000), false, true},     // inexact
            {0xFCA0004C, 0x10, 0, 0, false, true},                                                        // mtfsb1 ZX
            {0xFCA0008D, 0x04000000, 0, 0, false, true},                                                  // mtfsb0. ZX
            {0xFDFE158E, 0, 0, UINT64_C(0x04000010), false, true},                                        // mtfsf ZX/ZE
            {0xFF00110D, 0x04000000, 0, 0, false, true},                      // mtfsfi. 6,1 (ZE)
            {0xFD011000, 0x80, UINT64_C(0xFFF0000000000001), 0, false, true}, // fcmpu cr2,SNaN,zero
            {0xFD011040, 0x80, UINT64_C(0x7FF8000000000001), UINT64_C(0xFFF0000000000002), false, true},
            {0xFD011040, 0x80, UINT64_C(0x7FF8000000000001), 0, false, true}, // fcmpo cr2,QNaN,zero
            {0xFC60101D, 0x80, 0, UINT64_C(0x7FF0000000000001), true, true},  // fctiw. f3,SNaN
            {0xFC60101E, 0x80, 0, UINT64_C(0x41E0000000000000), true, true},  // fctiwz f3,2^31
            {0xFC60101F, 8, 0, UINT64_C(0xBFF8000000000000), false, true},    // fctiwz. f3,-1.5
        };
        auto* fp_cache = flags ? ROMlib_ppc_translation_cache_create() : nullptr;
        assert(!flags || fp_cache);
        for (const auto& test : floating_cases) {
            for (bool enabled : {false, true}) {
                fixture reference(0);
                reference.floating(test.opcode, enabled ? test.enable : 0, test.a, test.b);
                reference.run(1); // Disabled MSR FE mode still uses enabled FPSCR result rules.
                for (uint32_t mode : {0u, 0x100u, 0x800u, 0x900u}) {
                    fixture f(flags);
                    f.floating(test.opcode, enabled ? test.enable : 0, test.a, test.b);
                    f.state.mmu.msr |= mode;
                    if (fp_cache) {
                        f.env.translation_cache = fp_cache;
                        assert(ROMlib_ppc_translate_range_mode(fp_cache, &f.ram[0x1000], 0x1000, 4,
                                                               ROMLIB_PPC_TRANSLATE_750) <= 1);
                    }
                    f.run(1);
                    bool trapped = mode && (reference.registers.fpscr & 0x40000000);
                    assert(f.registers.pc == (trapped ? 0x700u : 0x1004u));
                    assert(f.registers.fpscr == reference.registers.fpscr && f.registers.cr == reference.registers.cr);
                    assert(!memcmp(f.registers.fpr, reference.registers.fpr, sizeof f.registers.fpr));
                    if (fp_cache)
                        assert(f.env.native_instructions == 1 && f.env.native_cache_hits == 1 &&
                               f.env.native_kernel_instructions == unsigned(!test.inline_fp));
                    if (trapped) {
                        assert(f.state.srr0 == 0x1000 && f.state.srr1 == (0x102000 | mode));
                        assert(!f.state.mmu.msr);
                        if (test.inhibits) {
                            uint64_t result;
                            memcpy(&result, &f.registers.fpr[3], 8);
                            assert(result == UINT64_C(0x123456789ABCDEF0));
                        }
                    }
                    ++cases;
                }
            }
            if (test.inline_fp) {
                fixture f(flags);
                f.floating(test.opcode, 0x04000000, test.a, test.b);
                f.state.mmu.msr = 0;
                f.env.translation_cache = fp_cache;
                f.run(1);
                assert(f.registers.pc == 0x800 && f.state.srr0 == 0x1000 && f.registers.fpscr == 0x04000000 &&
                       f.registers.cr == 0xDEADBEEF && !f.env.native_instructions);
                ++cases;
            }
        }
        ROMlib_ppc_translation_cache_destroy(fp_cache);
        for (bool rfi : {false, true}) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.fpscr = 0xC4000010; // Existing enabled ZX, currently masked by MSR.
            f.state.mmu.msr = 0x2000;
            f.registers.gpr[3] = 0x2800;
            f.state.srr0 = 0x12345678;
            f.state.srr1 = 0x2800;
            f.word(0x1000, rfi ? 0x4C000064 : 0x7C600124);
            auto* transition = flags ? ROMlib_ppc_translation_cache_create() : nullptr;
            if (transition) {
                f.env.translation_cache = transition;
                assert(ROMlib_ppc_translate_range_mode(transition, &f.ram[0x1000], 0x1000, 4,
                                                       ROMLIB_PPC_TRANSLATE_750) == 1);
            }
            f.run(1);
            assert(f.registers.pc == 0x700 && f.state.srr1 == 0x112800);
            assert(f.state.srr0 == (rfi ? 0x12345678u : 0x1004u));
            if (transition)
                assert(f.env.native_instructions == 1 && f.env.native_cache_hits == 1 &&
                       !f.env.native_kernel_instructions && !f.env.native_compilations);
            ROMlib_ppc_translation_cache_destroy(transition);
            ++cases;
        }
        {
            fixture f(flags);
            f.floating(0xFC611024, 0x10, UINT64_C(0x3FF0000000000000), 0);
            f.state.mmu.msr = 0x2800;
            f.word(0x700, 0x38802000); // enable FP in the handler, retaining disabled FE
            f.word(0x704, 0x7C800124);
            f.word(0x708, 0xFDFE058E); // mtfsf 255,f0 clears FPSCR enables/conditions
            f.word(0x70C, 0x39EF0001);
            f.word(0x710, 0x4C000064);
            f.run(7);
            uint64_t result;
            memcpy(&result, &f.registers.fpr[3], 8);
            assert(result == UINT64_C(0x7FF0000000000000) && f.registers.gpr[15] == 1);
            assert(f.registers.pc == 0x1004 && f.state.mmu.msr == 0x2800);
            assert(f.state.srr0 == 0x1000 && f.state.srr1 == 0x102800);
            assert(!(f.registers.fpscr & 0x40000000));
            ++cases;
        }
        {
            fixture f(flags);
            f.mapped();
            f.run(2);
            assert(f.registers.gpr[3] == 0x1234ABCD && f.word(0x20084) == 0x1234ABCD);
            assert(f.registers.pc == 0x80001008 && f.fetches == 2 && !f.tables);
            ++cases;
        }
        {
            // The real guest DSI handler installs a DBAT and RFI retries the
            // faulting load, preserving GPRs and restoring IR/DR from SRR1.
            fixture f(flags);
            f.mapped();
            f.state.mmu.dbat[0][0] = 0;
            f.registers.gpr[10] = 0x20002;
            f.registers.gpr[11] = 0x90000002;
            f.word(0x300, mtspr(537, 10));
            f.word(0x304, mtspr(536, 11));
            f.word(0x308, 0x4C000064);
            f.run(6);
            assert(f.state.srr0 == 0x80001000 && f.state.srr1 == 0x30);
            assert(f.state.dar == 0x90000080 && f.state.dsisr == 0x40000000);
            assert(f.registers.gpr[3] == 0x1234ABCD && f.word(0x20084) == 0x1234ABCD);
            assert(f.state.mmu.msr == 0x30 && f.registers.pc == 0x80001008);
            ++cases;
        }
        {
            // A real hardware hash-table walk updates R, then C on store.
            fixture f(flags);
            f.state.mmu.msr = 0x10;
            f.state.mmu.sdr1 = 0x10000;
            f.state.mmu.sr[9] = 0x23;
            f.registers.pc = 0x1000;
            f.registers.gpr[4] = 0x90000080;
            f.registers.gpr[5] = 0x90000084;
            f.word(0x1000, 0x80640000);
            f.word(0x1004, 0x90650000);
            uint32_t pte = 0x10000 | (0x23 << 6);
            f.word(pte, 0x80000000 | 0x23 << 7);
            f.word(pte + 4, 0x30002);
            f.word(0x30080, 0x89ABCDEF);
            f.run(2);
            assert(f.registers.gpr[3] == 0x89ABCDEF && f.word(0x30084) == 0x89ABCDEF);
            assert(f.word(pte + 4) == 0x30182 && f.tables == 2 && f.updates == 2);
            ++cases;
        }
        {
            // ISI fetch miss runs the real-mode handler, then RFI refetches
            // through the newly installed instruction BAT.
            fixture f(flags);
            f.state.mmu.msr = 0x20;
            f.registers.pc = 0x80001000;
            f.registers.gpr[10] = 2;
            f.registers.gpr[11] = 0x80000002;
            f.word(0x400, mtspr(529, 10));
            f.word(0x404, mtspr(528, 11));
            f.word(0x408, 0x4C000064);
            f.word(0x1000, 0x38601234); // li r3,0x1234
            f.run(5);
            assert(f.state.srr0 == 0x80001000 && f.state.srr1 == 0x40000020);
            assert(f.registers.pc == 0x80001004 && f.registers.gpr[3] == 0x1234);
            ++cases;
        }
        {
            // LE instruction fetch swaps word lanes; unaligned scalar data
            // follows byte lanes within doublewords, including boundary crossing.
            fixture f(flags);
            f.state.mmu.msr = 1;
            f.registers.pc = 0x1000;
            f.registers.gpr[4] = 0x2005;
            f.registers.gpr[5] = 0x3007;
            f.word(0x1004, 0x80640000);
            f.word(0x1000, 0x90650000);
            for (unsigned i = 0; i < 4; ++i)
                f.ram[(0x2005 + i) ^ 7] = 0x12 + i * 0x11;
            f.run(2);
            assert(f.registers.gpr[3] == 0x45342312 && f.registers.pc == 0x1008);
            for (unsigned i = 0; i < 4; ++i)
                assert(f.ram[(0x3007 + i) ^ 7] == 0x12 + i * 0x11);
            ++cases;
        }
        for (uint32_t opcode : {0x7C6000A6u, 0x4C000064u, mtspr(25, 3)}) {
            fixture f(flags);
            f.state.mmu.msr = 0x4000;
            f.registers.pc = 0x1000;
            f.registers.gpr[3] = 0x11223344;
            f.word(0x1000, opcode);
            f.run(1);
            assert(f.registers.pc == 0x700 && f.state.srr0 == 0x1000);
            assert(f.state.srr1 == 0x44000 && f.registers.gpr[3] == 0x11223344);
            ++cases;
        }
        for (uint32_t opcode : {0x00000000u, 0x18000000u, mtspr(1000, 3), 0x7C0002E4u}) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.word(0x1000, opcode);
            f.run(1);
            if (f.registers.pc != 0x700 || f.state.srr1 != 0x80000)
                fprintf(stderr, "opcode %08x pc %08x srr1 %08x\n", opcode, f.registers.pc, f.state.srr1);
            assert(f.registers.pc == 0x700 && f.state.srr1 == 0x80000 && f.state.srr0 == 0x1000);
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.word(0x1000, 0x44000002);
            f.run(1);
            assert(f.registers.pc == 0xC00 && f.state.srr0 == 0x1004);
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.word(0x1000, 0x2C030000); // cmpwi r3,0 remains an ordinary CPU kernel.
            f.word(0x1004, 0x0C830000); // twi EQ,r3,0
            f.run(2);
            assert(f.registers.pc == 0x700 && f.state.srr0 == 0x1004 && f.state.srr1 == 0x20000);
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.word(0x1000, 0xC8640000); // lfd f3,0(r4), FP disabled
            f.run(1);
            assert(f.registers.pc == 0x800 && f.state.srr0 == 0x1000);
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.state.mmu.msr = 0x2000;
            f.registers.gpr[4] = 0x2001;
            f.word(0x1000, 0xC8640000);
            f.run(1);
            assert(f.registers.pc == 0x600 && f.state.dar == 0x2001 && f.state.srr0 == 0x1000);
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x100000;
            f.state.mmu.msr = 0x1000;
            f.run(1);
            assert(f.registers.pc == 0x200 && f.state.srr1 == 0x41000 && !f.state.checkstop);
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x100000;
            assert(!ROMlib_ppc_execute(&f.registers, &f.env));
            assert(strstr(f.env.error, "checkstop") && f.state.checkstop);
            assert(f.registers.pc == 0x100000);
            ++cases;
        }
        {
            // DEC becomes pending while masked; MTMSR enables delivery before
            // the following instruction. The real handler rearms DEC and RFI.
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.registers.gpr[3] = 0xFFFFFFFF;
            f.registers.gpr[4] = 0x8000;
            f.registers.gpr[5] = 123;
            f.state.time_base = UINT64_C(0x1122334489ABCDEF);
            f.word(0x1000, mtspr(22, 3));
            f.word(0x1004, 0x7C800124); // mtmsr r4
            f.word(0x1008, 0x7CCC42E6); // mftb r6,TBL
            f.word(0x900, mtspr(22, 5));
            f.word(0x904, 0x4C000064);
            f.run(6);
            assert(f.registers.pc == 0x100C && f.registers.gpr[6] == 0x89ABCDEF);
            assert(f.state.srr0 == 0x1008 && f.state.srr1 == 0x8000);
            assert(f.state.decrementer == 123 && !f.state.decrementer_pending);
            ++cases;
        }
        for (unsigned inputs : {unsigned(ROMLIB_PPC_750_INT), unsigned(ROMLIB_PPC_750_SMI)}) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.state.mmu.msr = 0x8000;
            ROMlib_ppc_750_set_inputs(&f.state, inputs);
            f.run(1);
            assert(f.registers.pc == (inputs == ROMLIB_PPC_750_INT ? 0x500u : 0x1400u));
            assert(f.state.srr0 == 0x1000 && !f.fetches);
            ++cases;
        }
        for (uint32_t opcode : {0x38600001u, 0x48000008u, 0x7C6000A6u, 0x4C00012Cu}) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.state.mmu.msr = 0x400;
            f.word(0x1000, opcode);
            auto* cache = flags && opcode == 0x4C00012C ? ROMlib_ppc_translation_cache_create() : nullptr;
            if (cache) {
                f.env.translation_cache = cache;
                assert(ROMlib_ppc_translate_range_mode(cache, &f.ram[0x1000], 0x1000, 4, ROMLIB_PPC_TRANSLATE_750) ==
                       1);
            }
            f.run(1);
            if (opcode == 0x4C00012C)
                assert(f.registers.pc == 0x1004); // 750 ISYNC exclusion.
            else {
                assert(f.registers.pc == 0xD00 && f.state.srr1 == 0x400);
                assert(f.state.srr0 == (opcode == 0x48000008 ? 0x1008u : 0x1004u));
            }
            if (cache)
                assert(f.env.native_instructions == 1 && f.env.native_cache_hits == 1 &&
                       !f.env.native_kernel_instructions && !f.env.native_compilations);
            ROMlib_ppc_translation_cache_destroy(cache);
            ++cases;
        }
        for (bool little : {false, true}) {
            fixture f(flags);
            f.mapped();
            f.state.mmu.msr |= little;
            f.registers.pc += little ? 4 : 0;
            f.registers.lr = 0x80003000;
            uint32_t stub = f.registers.pc;
            f.word(0x1000, 0x18000001);
            f.env.physical_service_gate = [](void*, uint32_t physical, uint32_t opcode) -> int {
                return physical == 0x1000 && opcode == 0x18000001;
            };
            f.env.opaque = &f;
            f.env.mapped_service = [](void* opaque, uint32_t, ROMlib_ppc_registers* r, ROMlib_ppc_bus_result* fault) {
                auto& f = *static_cast<fixture*>(opaque);
                return ROMlib_ppc_750_write(&f.state, &f.bus, r->gpr[4], 2, 0xFACE, fault);
            };
            f.state.mmu.dbat[0][1] = 0x20001; // Read-only rectangle in mapped data.
            f.run(1);
            assert(f.registers.pc == 0x300 && f.state.srr0 == stub && f.state.dar == 0x90000080);
            assert(f.state.dsisr == 0x0A000000 && f.word(0x20080) == 0x1234ABCD);
            f.state.mmu.dbat[0][1] = 0x20002;
            f.word(0x300, 0x4C000064); // RFI retries the native call at its effective PC.
            f.run(2);
            assert(f.registers.pc == f.registers.lr);
            assert(f.word(little ? 0x20084 : 0x20080) == (little ? 0x0000FACEu : 0xFACEABCDu));
            ++cases;
        }
        {
            fixture f(flags);
            f.env.opaque = &f;
            f.state.mmu.msr = 0x8000;
            f.registers.pc = 0x100C;
            f.state.ictc = (17u << 1) | 1;
            f.state.core_cycles = UINT64_MAX - 4;
            f.word(0x100C, 0x38600001);
            f.word(0x1010, 0x38630001);
            f.env.wait_forward = [](void* opaque, uint64_t since, unsigned interval) {
                auto& f = *static_cast<fixture*>(opaque);
                assert(since == UINT64_MAX - 4);
                if (interval) {
                    assert(interval == 17);
                    ROMlib_ppc_750_advance_core(&f.state, interval);
                    ROMlib_ppc_750_advance(&f.state, 1); // DEC wakes before the next group fetch.
                }
            };
            f.run(2);
            assert(f.registers.pc == 0x900 && f.state.srr0 == 0x1010 && f.state.core_cycles == 12);
            assert(f.fetches == 1 && f.registers.gpr[3] == 1);
            ++cases;
        }
        for (unsigned interval : {0u, 1u, 17u, 255u})
            for (bool enabled : {false, true}) {
                fixture f(flags);
                f.registers.pc = 0x1000;
                f.registers.gpr[3] = 0xFFFFFE00 | (interval << 1) | enabled;
                f.word(0x1000, mtspr(1019, 3));
                f.word(0x1004, mtspr(1019, 5) ^ 0x100);
                for (unsigned pc = 0x1008; pc <= 0x1010; pc += 4)
                    f.word(pc, 0x38840001);
                f.word(0x1014, 0x4BFFFFFC); // Redirect into the previous word.
                f.run(7);
                assert(f.registers.gpr[5] == ((interval << 1) | enabled) && f.registers.gpr[4] == 4);
                assert(f.state.core_cycles == (enabled ? interval * 2 : 0));
                ++cases;
            }
        for (bool writing : {false, true}) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.state.mmu.msr = 0x4000;
            f.word(0x1000, mtspr(1019, 3) ^ (writing ? 0 : 0x100));
            f.run(1);
            assert(f.registers.pc == 0x700 && f.state.srr1 == 0x44000);
            ++cases;
        }
        for (bool release : {false, true}) {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.word(0x1000, 0x38600001); // A held input alone does not stop execution.
            f.word(0x1004, 0x7C00046C);
            f.word(0x1008, 0x38630001);
            auto* translations = flags ? ROMlib_ppc_translation_cache_create() : nullptr;
            if (translations) {
                f.env.translation_cache = translations;
                assert(ROMlib_ppc_translate_range_mode(translations, &f.ram[0x1004], 0x1004, 4,
                                                       ROMLIB_PPC_TRANSLATE_750) == 1);
            }
            ROMlib_ppc_750_set_inputs(&f.state, ROMLIB_PPC_750_TLBISYNC);
            assert(!ROMlib_ppc_execute(&f.registers, &f.env));
            assert(f.registers.pc == 0x1008 && f.registers.gpr[3] == 1 && f.env.instructions == 2);
            if (translations)
                assert(f.env.native_instructions == 1 && !f.env.native_kernel_instructions &&
                       f.env.native_cache_hits == 1 && !f.env.native_compilations);
            assert(f.env.waiting_for_tlb && !f.env.waiting_for_power && f.fetches == 2);
            assert(!ROMlib_ppc_execute(&f.registers, &f.env) && f.env.waiting_for_tlb && !f.env.instructions);
            if (release) {
                ROMlib_ppc_750_set_inputs(&f.state, 0);
                ROMlib_ppc_750_set_inputs(&f.state, ROMLIB_PPC_750_TLBISYNC);
                f.run(1);
                assert(f.registers.pc == 0x100C && f.registers.gpr[3] == 2 && !f.env.waiting_for_tlb);
            }
            else {
                ROMlib_ppc_750_set_inputs(&f.state, ROMLIB_PPC_750_TLBISYNC | ROMLIB_PPC_750_SRESET);
                f.run(1);
                assert(f.registers.pc == 0x100 && f.state.srr0 == 0x1008 && !f.state.tlbsync_wait);
            }
            ROMlib_ppc_translation_cache_destroy(translations);
            ++cases;
        }
        for (unsigned selector : {0x800000u, 0x400000u, 0x200000u}) {
            fixture f(flags);
            f.env.opaque = &f;
            f.registers.pc = 0x1000;
            f.state.decrementer = 100;
            f.registers.gpr[3] = 0x48000;
            f.word(0x1000, 0x7C600124); // MTMSR enters selected power mode.
            f.word(0x1004, 0x38800007);
            auto* translations = flags ? ROMlib_ppc_translation_cache_create() : nullptr;
            if (translations) {
                f.env.translation_cache = translations;
                assert(ROMlib_ppc_translate_range_mode(translations, &f.ram[0x1000], 0x1000, 4,
                                                       ROMLIB_PPC_TRANSLATE_750) == 1);
            }
            ROMlib_ppc_750_set_hid0(&f.state, selector);
            assert(!ROMlib_ppc_execute(&f.registers, &f.env));
            assert(f.env.waiting_for_power && f.env.instructions == 1 && f.registers.pc == 0x1004 && f.fetches == 1);
            if (translations)
                assert(f.env.native_instructions == 1 && !f.env.native_kernel_instructions &&
                       f.env.native_cache_hits == 1 && !f.env.native_compilations);
            f.env.power_wait = [](void* opaque, int waiting) {
                auto& f = *static_cast<fixture*>(opaque);
                if (waiting) {
                    assert(!f.env.instructions && f.fetches == 1);
                    ROMlib_ppc_750_set_qack(&f.state, ROMlib_ppc_750_qreq(&f.state));
                    ROMlib_ppc_750_advance(&f.state, 5);
                    ROMlib_ppc_750_set_inputs(&f.state, ROMLIB_PPC_750_INT);
                }
                else {
                    ROMlib_ppc_750_set_inputs(&f.state, 0);
                    ROMlib_ppc_750_set_qack(&f.state, 0);
                }
                return 1;
            };
            f.word(0x500, 0x4C000064); // RFI resumes the suspended guest.
            f.run(2);
            assert(f.state.srr0 == 0x1004 && f.registers.gpr[4] == 7 && f.registers.pc == 0x1008);
            assert(f.state.time_base == (selector == 0x200000 ? 0u : 5u));
            assert(!f.env.waiting_for_power && !f.state.qack && !f.state.power_mode);
            ROMlib_ppc_translation_cache_destroy(translations);
            ++cases;
        }
        for (bool little : {false, true})
            for (bool trusted : {false, true}) {
                fixture f(flags);
                f.mapped();
                f.state.mmu.msr |= little;
                uint32_t physical = 0x1000;
                uint32_t address = f.registers.pc + (little ? 4 : 0) + (trusted ? 0 : 8);
                f.registers.pc = address;
                f.word(physical + (trusted ? 0 : 8), 0x18000000);
                f.env.physical_service_gate = fixture::gate;
                if (trusted) {
                    assert(ROMlib_ppc_execute(&f.registers, &f.env));
                    assert(f.registers.pc == address + 4);
                }
                else {
                    f.run(1);
                    assert(f.registers.pc == 0x700 && f.state.srr0 == address);
                }
                ++cases;
            }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.env.system_service_gate = fixture::gate;
            f.word(0x1000, 0x18000000);
            assert(ROMlib_ppc_execute(&f.registers, &f.env));
            assert(f.registers.pc == 0x1004 && f.env.instructions == 1);
            ++cases;
        }
        {
            fixture f(flags);
            f.registers.pc = 0x1000;
            f.env.system_service_gate = [](void*, uint32_t, uint32_t) { return -1; };
            f.word(0x1000, 0x18000000);
            assert(!ROMlib_ppc_execute(&f.registers, &f.env));
            assert(strstr(f.env.error, "rejected by embedding") && f.registers.pc == 0x1000 && !f.state.srr0);
            ++cases;
        }
    }
    return cases;
}

int main(int argc, char** argv)
{
    bool suite = argc > 1 && !strcmp(argv[1], "--suite");
    unsigned cases = 0;
    for (int i = suite ? 2 : 1; i < argc; ++i) {
        printf("Running %s\n", argv[i]);
        fflush(stdout);
        for (unsigned flags : {0u, unsigned(ROMLIB_PPC_EXEC_JIT)})
            run_guest_image(argv[i], flags);
        cases += 2;
        printf("%s passed in interpreter and JIT modes\n", argv[i]);
    }
    if (argc == 1 || suite)
        cases += run_cpu_cases();
    printf("%u PowerPC 750 translated CPU/guest repair/exception cases passed\n", cases);
}
