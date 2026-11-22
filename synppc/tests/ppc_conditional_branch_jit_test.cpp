/*+===================================================================
  File:      ppc_conditional_branch_jit_test.cpp

  Summary:   Implements ppc conditional branch jit test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers, std::vector

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original conditional-branch model, live-state and native rewrite fixtures. */
#include "synppc/ppc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

static unsigned char memory[512];
static uint32_t base, taken_pc, opcode;
static unsigned services, polls, mode, compare_field, displacement_index;
static bool records;
static ROMlib_ppc_registers incoming;
static std::vector<ROMlib_ppc_registers> states;

static void* map(void*, uint32_t address, size_t size, int)
{
    static unsigned char service_code[] = {0x18, 0, 0, 1};
    static unsigned char exit_code[] = {0x18, 0, 0, 0};
    if (size <= 4) {
        if (address == taken_pc || address == base + 0x110)
            return service_code;
        if (address == base + 0x380)
            return exit_code;
    }
    uint32_t offset = address - base;
    return offset <= 0x110 && size <= 0x110 - offset ? memory + offset : nullptr;
}

static void word(unsigned offset, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        memory[offset + i] = value >> (24 - i * 8);
}

static void branch(unsigned bo, unsigned bi, bool absolute, bool link)
{
    const int32_t displacements[] = {-32768, -512, 512, 32764};
    uint32_t displacement = displacements[displacement_index++ & 3];
    taken_pc = absolute ? displacement : base + 0x10c + displacement;
    opcode = (16u << 26) | (bo << 21) | (bi << 16) | (displacement & 0xfffc) | (absolute ? 2 : 0) | link;
    word(0x10c, opcode);
}

static int service(void*, uint32_t id, ROMlib_ppc_registers* r)
{
    assert(id == 1);
    unsigned bo = (opcode >> 21) & 31, bi = (opcode >> 16) & 31;
    uint32_t cr = incoming.cr;
    if (records) {
        int32_t value = incoming.gpr[3];
        unsigned nibble = (value == 0 ? 2 : value < 0 ? 8 : 4) | (incoming.xer >> 31);
        unsigned shift = 28 - compare_field * 4;
        cr = (cr & ~(15u << shift)) | (nibble << shift);
    }
    uint32_t ctr = (bo & 4) ? incoming.ctr : incoming.ctr - 1;
    bool ctr_ok = (bo & 4) || (!!ctr != !!(bo & 2));
    bool cr_ok = (bo & 16) || (!!(cr & (0x80000000u >> bi)) == !!(bo & 8));
    assert(r->pc == (ctr_ok && cr_ok ? taken_pc : base + 0x110));
    assert(r->ctr == ctr && r->cr == cr && r->xer == incoming.xer);
    assert(r->lr == ((opcode & 1) ? base + 0x110 : incoming.lr));
    assert(!memcmp(r->gpr, incoming.gpr, sizeof r->gpr));
    if (!mode)
        states.push_back(*r);
    else
        assert(!memcmp(r, &states[services], sizeof *r));
    ++services;
    const uint32_t counters[] = {0, 1, 2, UINT32_MAX};
    r->ctr = counters[services & 3];
    r->cr = services * 0x9e3779b9u;
    r->xer = ((services & 7) << 29) | 0x42;
    r->gpr[3] = services % 3 - 1;
    if (services == 12 || services == 24)
        branch(services == 12 ? 12 : 18, (bi + 7) & 31, !(opcode & 2), !(opcode & 1));
    // Discard warmed code for a reserved BO alias, then restore a defined form.
    if (services == 36 || services == 44)
        branch(services == 36 ? 21 : 18, (bi + 11) & 31, !(opcode & 2), !(opcode & 1));
    r->lr = services == 256 ? base + 0x380 : base + 0x100;
    incoming = *r;
    return 1;
}

static void poll(void*)
{
    ++polls;
    if (polls == 1) {
        assert(services == 204);
        branch(0, ((opcode >> 16) + 13) & 31, !(opcode & 2), !(opcode & 1));
    }
}

int main(void)
{
    unsigned cases = 0, fallback_cases = 0;
    // Nonzero z-bit encodings compare legacy fallback, not architectural results.
    for (unsigned bo = 0; bo < 32; ++bo)
        for (unsigned bi = 0; bi < 32; ++bi)
            for (uint32_t ctr : {0u, 1u, 2u, UINT32_MAX})
                for (uint32_t cr : {0u, UINT32_MAX})
                    for (unsigned link = 0; link < 2; ++link)
                        for (unsigned producer = 0; producer < 2; ++producer) {
                            base = (cases & 4) ? 0xfffffef0 : 0;
                            records = producer;
                            compare_field = bi / 4;
                            bool limited = cases % 7 == 0;
                            ROMlib_ppc_registers reference = {};
                            uint64_t count = 0;
                            unsigned poll_count = 0;
                            states.clear();
                            for (mode = 0; mode < 2; ++mode) {
                                for (unsigned i = 0; i < 3; ++i)
                                    word(0x100 + i * 4, 0x60000000);
                                if (records)
                                    word(0x100, (11u << 26) | (compare_field << 23) | (3 << 16));
                                displacement_index = cases;
                                branch(bo, bi, cases & 8, link);
                                ROMlib_ppc_registers r = {};
                                r.pc = base + 0x100;
                                r.lr = 0x13579bdf;
                                r.ctr = ctr;
                                r.cr = cr;
                                r.xer = ((cases & 7) << 29) | 0x42;
                                for (unsigned i = 0; i < 32; ++i)
                                    r.gpr[i] = 0x87654321u * i;
                                r.gpr[3] = cases % 3 - 1;
                                incoming = r;
                                ROMlib_ppc_environment env = {};
                                env.memory = map;
                                env.linear_memory = memory;
                                env.linear_address = base;
                                env.linear_size = 0x110;
                                env.service = service;
                                env.poll = poll;
                                env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
                                env.instruction_limit = limited ? 300 + cases % 5 : 2000;
                                services = polls = 0;
                                int ok = ROMlib_ppc_execute(&r, &env);
                                if (!!ok == limited)
                                    fprintf(stderr, "bo=%u bi=%u case=%u mode=%u pc=%08x: %s\n", bo, bi, cases, mode,
                                            r.pc, env.error);
                                assert(!!ok != limited);
                                assert(services == (limited ? 60u : 256u));
                                if (!mode) {
                                    reference = r;
                                    count = env.instructions;
                                    poll_count = polls;
                                }
                                else {
                                    assert(!memcmp(&r, &reference, sizeof r));
                                    assert(env.instructions == count && polls == poll_count);
                                    if (ROMlib_ppc_jit_available())
                                        assert(env.native_instructions >= (limited ? 64u : 800u));
                                }
                            }
                            ++cases;
                            fallback_cases += !((0x001f3f3fu >> bo) & 1);
                        }
    printf("%u conditional-branch/BO/BI/CTR/CR/LK/AA/limit/rewrite cases passed; "
           "%u reserved-BO fallback starts\n",
           cases, fallback_cases);
}
