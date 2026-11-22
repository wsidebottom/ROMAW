/*+===================================================================
  File:      ppc_indirect_branch_jit_test.cpp

  Summary:   Implements ppc indirect branch jit test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers, std::vector

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original indirect-branch model, live targets and native rewrite fixtures. */
#include "synppc/ppc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

static unsigned char memory[512];
static uint32_t base, taken_pc, opcode;
static unsigned services, polls, mode, compare_field, target_low;
static bool records, fault_case, reject_target;
static ROMlib_ppc_registers incoming;
static std::vector<ROMlib_ppc_registers> states;

static void* map(void*, uint32_t address, size_t size, int)
{
    static unsigned char service_code[] = {0x18, 0, 0, 1};
    static unsigned char exit_code[] = {0x18, 0, 0, 0};
    if (size <= 4) {
        if (address == taken_pc || address == base + 0x110)
            return reject_target ? nullptr : service_code;
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

static void branch(bool ctr_target, unsigned bo, unsigned bi, bool link, unsigned hint = 0)
{
    opcode = (19u << 26) | (bo << 21) | (bi << 16) | (hint << 11) | ((ctr_target ? 528 : 16) << 1) | link;
    word(0x10c, opcode);
}

static void prepare(ROMlib_ppc_registers* r)
{
    const uint32_t counters[] = {0, 1, 2, UINT32_MAX};
    // Keep changing the effective target while the same native block is hot.
    uint32_t raw_target =
        (services & 1 ? (base ? 0x7ffffffcu : 0xfffffffcu) : 0x80000000u) | ((services + target_low) & 3);
    taken_pc = raw_target & ~3u;
    bool ctr_target = ((opcode >> 1) & 1023) == 528;
    r->gpr[4] = ctr_target ? 0x13579bdfu ^ (services << 2) : raw_target;
    r->gpr[5] = ctr_target ? raw_target : counters[(services + target_low) & 3];
    r->lr = services == 256 ? base + 0x380 : base + 0x80;
    incoming = *r;
    // The trampoline loads LR/CTR before entering the four-word native block.
    incoming.lr = r->gpr[4];
    incoming.ctr = r->gpr[5];
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
    if (r->pc != (ctr_ok && cr_ok ? taken_pc : base + 0x110))
        fprintf(stderr,
                "mode=%u services=%u op=%08x pc=%08x taken=%08x "
                "in-lr=%08x in-ctr=%08x ctr=%08x cr=%08x\n",
                mode, services, opcode, r->pc, taken_pc, incoming.lr, incoming.ctr, ctr, cr);
    assert(r->pc == (ctr_ok && cr_ok ? taken_pc : base + 0x110));
    assert(r->ctr == ctr && r->cr == cr && r->xer == incoming.xer);
    assert(r->lr == ((opcode & 1) ? base + 0x110 : incoming.lr));
    assert(!memcmp(r->gpr, incoming.gpr, sizeof r->gpr));
    if (!mode)
        states.push_back(*r);
    else
        assert(!memcmp(r, &states[services], sizeof *r));
    ++services;
    r->cr = services * 0x9e3779b9u;
    r->xer = ((services & 7) << 29) | 0x42;
    r->gpr[3] = services % 3 - 1;
    bool ctr_target = ((opcode >> 1) & 1023) == 528;
    if (services == 12 || services == 24)
        branch(!ctr_target, services == 12 ? 12 : 20, (bi + 7) & 31, !(opcode & 1));
    // A reserved hint field must discard warmed code and use legacy fallback.
    if (services == 36 || services == 44)
        branch(ctr_target, 12, (bi + 11) & 31, !(opcode & 1), services == 36 ? (bi ? bi : 1) : 0);
    prepare(r);
    if (services == 64 && fault_case)
        reject_target = true;
    return 1;
}

static void poll(void*)
{
    ++polls;
    if (polls == 1) {
        bool ctr_target = ((opcode >> 1) & 1023) == 528;
        branch(ctr_target, ctr_target ? 12 : 0, ((opcode >> 16) + 13) & 31, !(opcode & 1));
    }
}

int main(void)
{
    unsigned cases = 0, fallback_cases = 0;
    // Nonzero z-bit encodings compare legacy fallback, not architectural results.
    for (unsigned source = 0; source < 2; ++source)
        for (unsigned bo = 0; bo < 32; ++bo) {
            if (source && !(bo & 4))
                continue;
            for (unsigned bi = 0; bi < 32; ++bi)
                for (target_low = 0; target_low < 4; ++target_low)
                    for (unsigned link = 0; link < 2; ++link)
                        for (unsigned producer = 0; producer < 2; ++producer) {
                            base = (cases & 4) ? 0xfffffef0 : 0;
                            records = producer;
                            compare_field = bi / 4;
                            bool limited = cases % 7 == 0;
                            fault_case = !limited && cases % 11 == 0;
                            ROMlib_ppc_registers reference = {};
                            uint64_t count = 0;
                            unsigned poll_count = 0;
                            states.clear();
                            for (mode = 0; mode < 2; ++mode) {
                                word(0x7c,
                                     0x60000000);       // Align the first poll with BC.
                                word(0x80, 0x7c8803a6); // mtlr r4.
                                word(0x84, 0x7ca903a6); // mtctr r5.
                                word(0x88, 0x48000078); // b 0x100.
                                for (unsigned i = 0; i < 3; ++i)
                                    word(0x100 + i * 4, 0x60000000);
                                if (records)
                                    word(0x100, (11u << 26) | (compare_field << 23) | (3 << 16));
                                branch(source, bo, bi, link);
                                ROMlib_ppc_registers r = {};
                                r.pc = base + 0x7c;
                                r.cr = cases * 0x9e3779b9u;
                                r.xer = ((cases & 7) << 29) | 0x42;
                                for (unsigned i = 0; i < 32; ++i)
                                    r.gpr[i] = 0x87654321u * i;
                                r.gpr[3] = cases % 3 - 1;
                                services = polls = 0;
                                reject_target = false;
                                prepare(&r);
                                ROMlib_ppc_environment env = {};
                                env.memory = map;
                                env.linear_memory = memory;
                                env.linear_address = base;
                                env.linear_size = 0x110;
                                env.service = service;
                                env.poll = poll;
                                env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
                                env.instruction_limit = limited ? 300 + cases % 8 : 3000;
                                int ok = ROMlib_ppc_execute(&r, &env);
                                if (!!ok == (limited || fault_case))
                                    fprintf(stderr, "case=%u mode=%u pc=%08x: %s\n", cases, mode, r.pc, env.error);
                                assert(!!ok != (limited || fault_case));
                                assert(services == (limited      ? (env.instruction_limit - 1) / 8
                                                    : fault_case ? 64u
                                                                 : 256u));
                                if (!mode) {
                                    reference = r;
                                    count = env.instructions;
                                    poll_count = polls;
                                }
                                else {
                                    assert(!memcmp(&r, &reference, sizeof r));
                                    assert(env.instructions == count && polls == poll_count);
                                    if (ROMlib_ppc_jit_available())
                                        assert(env.native_instructions >= (limited || fault_case ? 32u : 700u));
                                }
                            }
                            ++cases;
                            fallback_cases += !((0x001f3f3fu >> bo) & 1);
                        }
        }
    printf("%u LR/CTR branch/low-bit/LK/CR/limit/rewrite cases passed; "
           "%u reserved-BO fallback starts\n",
           cases, fallback_cases);
}
