/*+===================================================================
  File:      ppc_system750_test.cpp

  Summary:   Run the 750 hardware suites and check reset, exception entry, checkstop and RFI.

  Classes:   ROMlib_ppc_system750 and ROMlib_ppc_registers.

  Functions: main

  Origin:    Independent bit/exception matrices from MPC750UM chapter 4 and MPCFPE RFI.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "synppc/system750.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

void collision750_checks();
void snoop750_checks();
void bus750_checks();
void cache750_checks();
void clock750_checks();
void mmu750_checks();

static void state750_checks()
{
    unsigned cases = 0;
    for (unsigned bit = 0; bit < 32; ++bit)
        for (bool complement : {false, true}) {
            ROMlib_ppc_system750 state = {};
            uint32_t value = complement ? ~(1u << bit) : (1u << bit);
            state.cache[0].lines[0][0].state = 1;
            state.cache[1].lines[0][0].state = 2;
            state.cache[0].plru[0] = state.cache[1].plru[0] = 0x7F;
            ROMlib_ppc_750_set_hid0(&state, value);
            bool iflash = (value & 0x8800) == 0x8800, dflash = (value & 0x4400) == 0x4400;
            for (unsigned position = 0; position < 32; ++position) {
                bool reserved = position == 26 || position == 19 || position == 18 || position == 17 || position == 4 ||
                                position == 1;
                bool wanted = (value & (1u << position)) && !reserved && !(position == 11 && iflash) &&
                              !(position == 10 && dflash);
                assert(bool(state.hid0 & (1u << position)) == wanted);
            }
            assert(state.cache[0].lines[0][0].state == (iflash ? 0 : 1));
            assert(state.cache[1].lines[0][0].state == (dflash ? 0 : 2));
            assert(state.cache[0].plru[0] == (iflash ? 0 : 0x7F));
            assert(state.cache[1].plru[0] == (dflash ? 0 : 0x7F));
            ++cases;
        }
    ROMlib_ppc_system750 reset;
    ROMlib_ppc_registers registers;
    memset(&reset, 0xA5, sizeof reset);
    memset(&registers, 0xA5, sizeof registers);
    ROMlib_ppc_750_reset(&reset, &registers);
    assert(reset.mmu.msr == 0x40 && registers.pc == 0xFFF00100);
    ROMlib_ppc_system750 cleared = {};
    ROMlib_ppc_registers cleared_registers = {};
    cleared.mmu.msr = 0x40;
    cleared_registers.pc = 0xFFF00100;
    assert(!memcmp(&reset, &cleared, sizeof reset));
    assert(!memcmp(&registers, &cleared_registers, sizeof registers));

    for (unsigned vector : {0x100u, 0x200u, 0x300u, 0x400u, 0x500u, 0x600u, 0x700u, 0x800u, 0x900u, 0xC00u, 0xD00u,
                            0xF00u, 0x1300u, 0x1400u, 0x1700u})
        for (unsigned bit = 0; bit < 32; ++bit)
            for (unsigned complement = 0; complement < 2; ++complement)
                for (uint32_t pc : {0x100u, 0xFFFFFFFCu}) {
                    ROMlib_ppc_system750 state = {};
                    ROMlib_ppc_registers r = {};
                    memset(&r, 0xA5, sizeof r);
                    r.pc = pc;
                    state.mmu.msr = complement ? ~(1u << bit) : 1u << bit;
                    uint32_t old_msr = state.mmu.msr;
                    state.srr0 = 0xBAD01000;
                    state.srr1 = 0xBAD02000;
                    state.dar = 0x12345678;
                    state.dsisr = 0x87654321;
                    auto expected = r;
                    if (vector == 0x200 && !(old_msr & 0x1000)) {
                        auto before = state;
                        before.checkstop = 1;
                        assert(!ROMlib_ppc_750_exception(&state, &r, vector, 0xFFFFFFFF, 0x11223344));
                        assert(!memcmp(&state, &before, sizeof state));
                        assert(!memcmp(&r, &expected, sizeof r));
                        assert(!ROMlib_ppc_750_rfi(&state, &r));
                        assert(!ROMlib_ppc_750_exception(&state, &r, 0x500, 0, 0));
                    }
                    else {
                        assert(ROMlib_ppc_750_exception(&state, &r, vector, 0xFFFFFFFF, 0x11223344));
                        assert(state.srr0 == pc + (vector == 0xC00 ? 4 : 0));
                        uint32_t srr1 = old_msr & 0x87C0FFFF;
                        if (vector == 0x200)
                            srr1 = (old_msr & 0xFFFF) | 0x001F0000;
                        if (vector == 0x400)
                            srr1 |= 0x58000000;
                        if (vector == 0x700)
                            srr1 |= 0x001F0000;
                        assert(state.srr1 == srr1);
                        // Every documented cleared/live field is checked by bit,
                        // avoiding a second copy of the implementation's mask.
                        for (unsigned position = 0; position < 32; ++position) {
                            uint32_t mask = 1u << position;
                            bool clear = position == 18 || position == 15 || position == 14 || position == 13 ||
                                         position == 11 || position == 10 || position == 9 || position == 8 ||
                                         position == 5 || position == 4 || position == 2 || position == 1 ||
                                         position == 0 || (vector == 0x200 && position == 12);
                            bool wanted = position == 0 ? !!(old_msr & 0x10000) : !clear && !!(old_msr & mask);
                            assert(!!(state.mmu.msr & mask) == wanted);
                        }
                        expected.pc = (old_msr & 0x40 ? 0xFFF00000 : 0) | vector;
                        assert(!memcmp(&r, &expected, sizeof r));
                        assert(state.dar == (vector == 0x300 || vector == 0x600 ? 0x11223344u : 0x12345678u));
                        assert(state.dsisr == (vector == 0x300 || vector == 0x600 ? 0xFFFFFFFFu : 0x87654321u));
                    }
                    ++cases;
                }
    for (unsigned bit = 0; bit < 32; ++bit)
        for (unsigned complement = 0; complement < 2; ++complement)
            for (unsigned low = 0; low < 4; ++low) {
                ROMlib_ppc_system750 state = {};
                ROMlib_ppc_registers r = {};
                state.mmu.msr = 0x5A5A1A5A; // Supervisor; keep unsaved handler fields.
                state.srr1 = complement ? ~(1u << bit) : 1u << bit;
                state.srr0 = 0x81234560 | low;
                uint32_t handler = state.mmu.msr;
                assert(ROMlib_ppc_750_rfi(&state, &r));
                assert(r.pc == 0x81234560);
                for (unsigned position = 0; position < 32; ++position) {
                    bool restored = position < 16 || (position >= 22 && position <= 26) || position == 31;
                    uint32_t value = restored ? state.srr1 : handler;
                    assert((state.mmu.msr & (1u << position)) == (value & (1u << position)));
                }
                ++cases;
            }
    for (unsigned prefix = 0; prefix < 2; ++prefix) {
        ROMlib_ppc_system750 state = {};
        ROMlib_ppc_registers r = {};
        state.mmu.msr = 0x4000 | (prefix ? 0x40 : 0);
        r.pc = 0x100;
        state.srr0 = 0xDEADBEEF;
        assert(ROMlib_ppc_750_rfi(&state, &r));
        assert(state.srr0 == 0x100 && state.srr1 == (0x44000u | (prefix ? 0x40 : 0)));
        assert(r.pc == (prefix ? 0xFFF00700u : 0x700u));
        ++cases;
    }
    assert(!ROMlib_ppc_750_exception(nullptr, &registers, 0x700, 0, 0));
    assert(!ROMlib_ppc_750_exception(&reset, nullptr, 0x700, 0, 0));
    assert(!ROMlib_ppc_750_exception(&reset, &registers, 0x1000, 0, 0)); // No software TLB-miss vector on 750.
    assert(!ROMlib_ppc_750_rfi(nullptr, &registers));
    printf("%u PowerPC 750 reset/exception/checkstop/RFI state cases passed\n", cases);
}

int main(int argc, char** argv)
{
    struct suite {
        const char* name;
        void (*run)();
    };

    const suite suites[] = {
        {"state750", state750_checks}, {"collision750", collision750_checks}, {"snoop750", snoop750_checks},
        {"bus750", bus750_checks},     {"cache750", cache750_checks},         {"clock750", clock750_checks},
        {"mmu750", mmu750_checks},
    };
    if (argc > 2)
        return 2;
    unsigned completed = 0;
    for (const auto& test : suites) {
        if (argc == 2 && strcmp(test.name, argv[1]))
            continue;
        printf("Running %s\n", test.name);
        fflush(stdout);
        test.run();
        ++completed;
    }
    if (!completed) {
        fprintf(stderr, "Unknown 750 suite %s\n", argv[1]);
        return 2;
    }
    printf("%u PowerPC 750 hardware suites passed\n", completed);
    return 0;
}
