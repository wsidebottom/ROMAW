/*+===================================================================
  File:      ppc_branch_jit_test.cpp

  Summary:   Implements ppc branch jit test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers, std::vector

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original direct-branch exit, link, rewrite and polling fixtures. */
#include "synppc/ppc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

static unsigned char memory[4096];
static uint32_t base, target, branch_word;
static unsigned services, polls, mode;
static unsigned run_kind, limit_tail;
static bool reject_target;
static ROMlib_ppc_environment* active_env;
static std::vector<ROMlib_ppc_registers> states;

static void* map(void*, uint32_t address, size_t size, int)
{
    uint32_t relative = address - base;
    if (reject_target && relative == 0x300)
        return nullptr;
    static unsigned char external_service[] = {0x18, 0, 0, 1};
    uint32_t external_offset = address - target;
    if (relative >= sizeof memory && external_offset < sizeof external_service &&
        size <= sizeof external_service - external_offset)
        return external_service + external_offset;
    return relative <= sizeof memory && size <= sizeof memory - relative ? memory + relative : nullptr;
}

static void word(unsigned offset, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        memory[offset + i] = value >> (24 - i * 8);
}

static void set_branch(unsigned destination, bool absolute, bool link)
{
    target = base + destination;
    uint32_t displacement = absolute ? target : target - (base + 0x10c);
    assert((int32_t)displacement >= -0x2000000 && (int32_t)displacement <= 0x1fffffc);
    branch_word = 0x48000000 | (displacement & 0x03fffffc) | (absolute ? 2 : 0) | link;
    word(0x10c, branch_word);
}

static int service(void*, uint32_t id, ROMlib_ppc_registers* r)
{
    assert(id == 1 && r->pc == target);
    // LK=0 preserves the incoming LR; LK=1 records the branch's following PC.
    assert(r->lr == ((branch_word & 1) ? base + 0x110 : services ? base + 0x100 : 0x13579bdf));
    if (!mode)
        states.push_back(*r);
    else
        assert(!memcmp(r, &states[services], sizeof *r));
    ++services;
    r->cr = services * 0x13579bdu;
    r->xer = ((services & 7) << 29) | (services & 0x7f);
    for (unsigned i = 0; i < 32; ++i)
        r->gpr[i] = (r->gpr[i] << 1) | (r->gpr[i] >> 31);
    if (services == 12 || services == 24)
        set_branch(services == 12 ? 0x80 : 0x300, !(branch_word & 2) && base != 0x80000000, !(branch_word & 1));
    if (services == 64) {
        reject_target = run_kind == 1;
        if (run_kind == 2)
            active_env->instruction_limit = active_env->instructions + limit_tail;
    }
    r->lr = services == 256 ? base + 0x380 : base + 0x100;
    return 1;
}

static void poll(void*)
{
    ++polls;
    if (polls == 1) {
        assert(services == 204); // The poll falls immediately before B/BL.
        set_branch(0x200, !(branch_word & 2) && base != 0x80000000, !(branch_word & 1));
    }
}

int main(void)
{
    unsigned cases = 0;
    for (run_kind = 0; run_kind < 3; ++run_kind)
        for (uint32_t address : {0u, 0xfffffef0u, 0x80000000u})
            for (unsigned absolute = 0; absolute < 2; ++absolute)
                for (unsigned link = 0; link < 2; ++link)
                    for (unsigned destination :
                         {0x80u, 0x200u, 0x300u, 0x02000108u, 0xfe00010cu, 0x01fffffcu, 0xfe000000u})
                        for (unsigned flags = 0; flags < 8; ++flags) {
                            int32_t displacement = absolute ? address + destination : destination - 0x10c;
                            if (displacement < -0x2000000 || displacement > 0x1fffffc)
                                continue;
                            base = address;
                            ROMlib_ppc_registers reference = {};
                            uint64_t instruction_count = 0;
                            unsigned poll_count = 0;
                            states.clear();
                            limit_tail = flags + 1;
                            for (mode = 0; mode < 2; ++mode) {
                                memset(memory, 0, sizeof memory);
                                word(0x100, 0x38630001); // addi r3,r3,1.
                                word(0x104, 0x34840001); // addic. r4,r4,1.
                                word(0x108, 0x60a50055); // ori r5,r5,0x55.
                                set_branch(destination, absolute, link);
                                for (unsigned offset : {0x80u, 0x200u, 0x300u})
                                    word(offset, 0x18000001);
                                word(0x110,
                                     0); // Must stop gathering at the branch.
                                word(0x380, 0x18000000);
                                ROMlib_ppc_registers r = {};
                                r.pc = base + 0x100;
                                r.lr = 0x13579bdf;
                                r.cr = 0x87654321;
                                r.xer = (flags << 29) | 0x42;
                                r.ctr = 0x76543210;
                                r.gpr[3] = 0xffffffff;
                                r.gpr[4] = 0x7fffffff;
                                ROMlib_ppc_environment env = {};
                                env.memory = map;
                                env.linear_memory = memory;
                                env.linear_address = base;
                                // The declared linear window cannot wrap; low targets after
                                // 32-bit PC wrap are supplied by the memory callback instead.
                                env.linear_size = base == 0xfffffef0 ? 0x110 : 0x200;
                                env.service = service;
                                env.poll = poll;
                                env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
                                env.instruction_limit = 20000;
                                services = polls = 0;
                                reject_target = false;
                                active_env = &env;
                                int ok = ROMlib_ppc_execute(&r, &env);
                                bool expected_ok = run_kind == 0;
                                if (!!ok != expected_ok || (expected_ok && services != 256))
                                    fprintf(stderr, "base=%08x AA=%u LK=%u dest=%x mode=%u pc=%08x services=%u: %s\n",
                                            base, absolute, link, destination, mode, r.pc, services, env.error);
                                assert(!!ok == expected_ok);
                                if (!run_kind)
                                    assert(services == 256);
                                else if (run_kind == 1) {
                                    assert(services == 64 && r.pc == target);
                                    assert(r.lr == ((branch_word & 1) ? base + 0x110 : base + 0x100));
                                }
                                else
                                    assert(services >= 64 && services <= 65);
                                if (!mode) {
                                    reference = r;
                                    instruction_count = env.instructions;
                                    poll_count = polls;
                                }
                                else {
                                    assert(!memcmp(&r, &reference, sizeof r));
                                    assert(env.instructions == instruction_count && polls == poll_count);
                                    if (!run_kind)
                                        assert(polls);
                                    if (ROMlib_ppc_jit_available())
                                        assert(env.native_instructions >= (run_kind ? 128u : 800u));
                                }
                            }
                            ++cases;
                        }
    printf("%u direct-branch/AA/LK/wrap/rewrite/full-state/poll cases passed\n", cases);
}
