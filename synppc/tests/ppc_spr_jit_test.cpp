/*+===================================================================
  File:      ppc_spr_jit_test.cpp

  Summary:   Implements ppc spr jit test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers, std::vector

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original user-SPR, carry/record, live-target and native rewrite fixtures. */
#include "synppc/ppc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

static unsigned char memory[512];
static uint32_t base, invalid_word;
static unsigned services, polls, mode;
static bool invalid_run, warm, read_only, fault_case, reject_target;
static ROMlib_ppc_registers incoming;
static std::vector<ROMlib_ppc_registers> states;
static const uint32_t values[] = {0, 1, UINT32_MAX, 0xfffffffe, 0x80000000, 0x7fffffff, 0x12345678, 0x87654321};
static const uint32_t counters[] = {0, 1, 2, UINT32_MAX};

static void word(unsigned offset, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        memory[offset + i] = value >> (24 - i * 8);
}

static uint32_t readword(unsigned offset)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value = value * 256 + memory[offset + i];
    return value;
}

static uint32_t move(bool writing, unsigned spr, unsigned reg)
{
    return (31u << 26) | (reg << 21) | ((spr & 31) << 16) | ((spr >> 5) << 11) | ((writing ? 467 : 339) << 1);
}

static void condition(ROMlib_ppc_registers& r, unsigned field, int64_t value)
{
    unsigned nibble = (value == 0 ? 2 : value < 0 ? 8 : 4) | (r.xer >> 31);
    unsigned shift = 28 - field * 4;
    r.cr = (r.cr & ~(15u << shift)) | (nibble << shift);
}

static void apply(uint32_t opcode, ROMlib_ppc_registers& r)
{
    if (opcode == 0x60000000)
        return;
    if ((opcode >> 26) == 11) {
        condition(r, (opcode >> 23) & 7, (int64_t)(int32_t)r.gpr[(opcode >> 16) & 31] - (int16_t)opcode);
        return;
    }
    unsigned xo = (opcode >> 1) & 1023, reg = (opcode >> 21) & 31;
    if (xo == 339 || xo == 467) {
        unsigned spr = ((opcode >> 16) & 31) | ((opcode >> 6) & 0x3e0);
        uint32_t& value = spr == 1 ? r.xer : spr == 8 ? r.lr : r.ctr;
        if (xo == 339)
            r.gpr[reg] = value;
        else
            // Reserved XER data follows legacy interpretation, not a hardware oracle.
            value = r.gpr[reg] & (spr == 1 ? 0xe000007fu : UINT32_MAX);
        return;
    }
    assert((xo & 511) == 138);
    uint32_t a = r.gpr[(opcode >> 16) & 31], b = r.gpr[(opcode >> 11) & 31];
    unsigned carry = (r.xer >> 29) & 1;
    uint64_t sum = (uint64_t)a + b + carry;
    r.gpr[reg] = sum;
    r.xer = (r.xer & ~0x20000000u) | ((sum >> 32) << 29);
    if (xo & 512) {
        int64_t signed_sum = (int64_t)(int32_t)a + (int32_t)b + carry;
        bool overflow = signed_sum < INT32_MIN || signed_sum > INT32_MAX;
        r.xer = (r.xer & ~0x40000000u) | (overflow ? 0xc0000000u : 0);
    }
    condition(r, 0, (int32_t)r.gpr[reg]);
}

static ROMlib_ppc_registers result(void)
{
    ROMlib_ppc_registers r = incoming;
    unsigned end = invalid_run ? 0x110 : read_only ? 0x11c : 0x118;
    for (unsigned offset = 0x100; offset < end; offset += 4)
        apply(readword(offset), r);
    r.pc = base + (invalid_run ? 0x110 : 0x11c);
    if (!invalid_run && !read_only) {
        uint32_t opcode = readword(0x118);
        unsigned bo = (opcode >> 21) & 31, bi = (opcode >> 16) & 31;
        uint32_t target;
        if ((opcode >> 26) == 18) {
            uint32_t displacement = opcode & 0x03fffffc;
            if (displacement & 0x02000000)
                displacement |= 0xfc000000;
            target = (opcode & 2) ? displacement : base + 0x118 + displacement;
            bo = 20;
        }
        else if ((opcode >> 26) == 16) {
            uint32_t displacement = (int16_t)(opcode & 0xfffc);
            target = (opcode & 2) ? displacement : base + 0x118 + displacement;
        }
        else
            target = ((opcode >> 1) & 1023) == 16 ? r.lr & ~3u : r.ctr & ~3u;
        if (!(bo & 4))
            --r.ctr;
        bool ctr_ok = (bo & 4) || (!!r.ctr != !!(bo & 2));
        bool cr_ok = (bo & 16) || (!!(r.cr & (0x80000000u >> bi)) == !!(bo & 8));
        if (ctr_ok && cr_ok)
            r.pc = target;
        if (opcode & 1)
            r.lr = base + 0x11c;
    }
    return r;
}

static void* map(void*, uint32_t address, size_t size, int)
{
    static unsigned char service_code[] = {0x18, 0, 0, 1};
    static unsigned char exit_code[] = {0x18, 0, 0, 0};
    if (size <= 4) {
        if (address == base + 0x380)
            return exit_code;
        if (address == result().pc)
            return reject_target ? nullptr : service_code;
    }
    uint32_t offset = address - base;
    return offset <= 0x11c && size <= 0x11c - offset ? memory + offset : nullptr;
}

static void rewrite_branch(void)
{
    uint32_t opcode = readword(0x118);
    if ((opcode >> 26) == 18) {
        word(0x118,
             opcode ^ 0x303); // LI, AA and LK; targets remain outside code.
        return;
    }
    unsigned bo = (opcode >> 21) & 31;
    bool count_target = (opcode >> 26) == 19 && ((opcode >> 1) & 1023) == 528;
    unsigned replacement = count_target ? (bo == 20 ? 4 : 20) : (bo == 20 ? 0 : 20);
    word(0x118, (opcode & ~0x03e00000u) | (replacement << 21));
    word(0x118, readword(0x118) ^ 1);
}

static int service(void*, uint32_t id, ROMlib_ppc_registers* r)
{
    assert(id == 1);
    ROMlib_ppc_registers expected = result();
    assert(!memcmp(r, &expected, sizeof *r));
    if (!mode)
        states.push_back(*r);
    else
        assert(!memcmp(r, &states[services], sizeof *r));
    ++services;
    r->cr = services * 0x9e3779b9u;
    r->xer = ((services & 7) << 29) | (services & 0x7f);
    r->ctr = counters[services & 3];
    for (unsigned i = 0; i < 32; ++i)
        r->gpr[i] = values[(services + i) & 7];
    r->gpr[31] = 0x80000003;
    if (invalid_run) {
        if (warm && services == 16)
            word(0x10c, invalid_word);
    }
    else if (services == 12 || services == 24) {
        if (read_only)
            word(0x100, readword(0x100) ^ 0x03e00000);
        else {
            word(0x104, readword(0x104) ^ 0x00200000);
            word(0x10c, readword(0x10c) ^ 0x400);
            rewrite_branch();
        }
    }
    r->lr = services == 256 ? base + 0x380 : base + (invalid_run ? 0x100 : 0x80);
    incoming = *r;
    if (!invalid_run)
        incoming.lr = r->gpr[31]; // The trampoline executes MTLR r31.
    if (!invalid_run && fault_case && services == 64)
        reject_target = true;
    return 1;
}

static void poll(void*)
{
    ++polls;
    if (polls == 1) {
        assert(!invalid_run && services == 102);
        // The poll precedes the body's first instruction, after the trampoline.
        if (read_only)
            word(0x100, readword(0x100) ^ 0x03e00000);
        else
            rewrite_branch();
    }
}

static ROMlib_ppc_registers initial(unsigned seed)
{
    ROMlib_ppc_registers r = {};
    r.pc = base + (invalid_run ? 0x100 : 0x7c);
    r.lr = 0x13579bdf;
    r.ctr = counters[seed & 3];
    r.cr = seed * 0x9e3779b9u;
    r.xer = ((seed & 7) << 29) | ((seed >> 3) & 0x7f);
    for (unsigned i = 0; i < 32; ++i)
        r.gpr[i] = values[(seed + i) & 7];
    r.gpr[31] = 0x80000003;
    return r;
}

static ROMlib_ppc_environment environment(void)
{
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.linear_memory = memory;
    env.linear_address = base;
    env.linear_size = 0x11c;
    env.service = service;
    env.poll = poll;
    env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
    return env;
}

static void valid(unsigned seed, unsigned spr, unsigned source, unsigned dest, bool pure)
{
    static const unsigned options[] = {0, 2, 4, 8, 10, 12, 16, 18, 20};
    invalid_run = false;
    read_only = pure;
    bool limited = seed % 7 == 0;
    fault_case = !limited && !pure && seed % 11 == 0;
    base = (seed & 4) ? 0xfffffee0 : 0x10000000;
    states.clear();
    ROMlib_ppc_registers reference = {};
    uint64_t count = 0;
    unsigned poll_count = 0;
    for (mode = 0; mode < 2; ++mode) {
        word(0x7c, 0x60000000);
        word(0x80, move(true, 8, 31));
        word(0x84, 0x4800007c);
        if (pure)
            for (unsigned i = 0; i < 7; ++i)
                word(0x100 + i * 4, move(false, spr, (dest + i * 7) & 31));
        else {
            word(0x100, move(false, spr, dest));
            word(0x104, move(true, spr, source));
            word(0x108, move(false, spr, (dest + 17) & 31));
            unsigned arithmetic_dest = (dest + 7) & 31;
            word(0x10c, (31u << 26) | (arithmetic_dest << 21) | (source << 16) | (dest << 11) |
                            ((138 | (seed & 16 ? 512 : 0)) << 1) | 1);
            word(0x110, (11u << 26) | (((seed >> 7) & 7) << 23) | (arithmetic_dest << 16));
            word(0x114, move(false, spr, (dest + 23) & 31));
            unsigned kind = (seed + (seed >> 5)) & 3;
            unsigned bo = kind == 2 ? (seed % 3 == 0 ? 20 : seed % 3 == 1 ? 4 : 12) : options[seed % 9];
            uint32_t branch = kind == 0   ? (16u << 26) | 0x100 | (seed & 2)
                              : kind == 3 ? (18u << 26) | 0x100 | (seed & 2)
                                          : (19u << 26) | ((kind == 1 ? 16 : 528) << 1);
            if (kind != 3)
                branch |= (bo << 21) | ((seed & 31) << 16);
            word(0x118, branch | (seed & 1));
        }
        ROMlib_ppc_registers r = initial(seed);
        incoming = r;
        incoming.lr = r.gpr[31];
        services = polls = 0;
        reject_target = false;
        ROMlib_ppc_environment env = environment();
        env.instruction_limit = limited ? 500 + seed % 13 : 3000;
        assert(!!ROMlib_ppc_execute(&r, &env) != (limited || fault_case));
        assert(services == (limited ? (env.instruction_limit - 1) / 10 : fault_case ? 64u : 256u));
        if (fault_case) {
            ROMlib_ppc_registers expected = result();
            assert(!memcmp(&r, &expected, sizeof r));
        }
        if (!mode) {
            reference = r;
            count = env.instructions;
            poll_count = polls;
        }
        else {
            assert(!memcmp(&r, &reference, sizeof r));
            assert(env.instructions == count && polls == poll_count);
            if (ROMlib_ppc_jit_available())
                assert(env.native_instructions >= (limited || fault_case ? 100u : 600u));
        }
    }
}

static void reserved(uint32_t good)
{
    invalid_run = true;
    invalid_word = good | 1;
    base = 0x10000000;
    for (unsigned warmed = 0; warmed < 2; ++warmed) {
        warm = warmed;
        states.clear();
        for (mode = 0; mode < 2; ++mode) {
            for (unsigned offset = 0x100; offset < 0x10c; offset += 4)
                word(offset, 0x60000000);
            word(0x10c, warm ? good : invalid_word);
            word(0x110, 0x18000001);
            ROMlib_ppc_registers r = initial(good);
            incoming = r;
            services = polls = 0;
            reject_target = false;
            ROMlib_ppc_environment env = environment();
            env.instruction_limit = 1000;
            assert(!ROMlib_ppc_execute(&r, &env));
            assert(strstr(env.error, "illegal PowerPC instruction"));
            incoming.pc = base + 0x10c;
            assert(!memcmp(&r, &incoming, sizeof r));
            assert(services == (warm ? 16u : 0u));
            assert(env.instructions == (warm ? 84u : 4u));
            if (mode && warm && ROMlib_ppc_jit_available())
                assert(env.native_instructions >= 16);
        }
    }
}

int main(void)
{
    unsigned cases = 0, invalid = 0;
    for (unsigned spr : {1u, 8u, 9u}) {
        for (unsigned source = 0; source < 32; ++source)
            for (unsigned dest = 0; dest < 32; ++dest)
                for (unsigned sample = 0; sample < 8; ++sample)
                    valid(cases++, spr, source, dest, false);
        for (unsigned dest = 0; dest < 32; ++dest)
            for (unsigned flags = 0; flags < 8; ++flags)
                valid(cases++, spr, 0, dest, true);
        for (unsigned reg = 0; reg < 32; ++reg)
            for (unsigned writing = 0; writing < 2; ++writing) {
                reserved(move(writing, spr, reg));
                ++invalid;
            }
    }
    fflush(stdout);
    fprintf(stderr,
            "%u user SPR/GPR/alias/XER/live-target/LK/limit/rewrite cases; "
            "%u cold and hot reserved encodings passed\n",
            cases, invalid);
}
