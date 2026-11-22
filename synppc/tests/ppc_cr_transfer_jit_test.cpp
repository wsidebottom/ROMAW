/*+===================================================================
  File:      ppc_cr_transfer_jit_test.cpp

  Summary:   Implements ppc cr transfer jit test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers, std::vector

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original CR transfer/field-mask, XER-clear and native rewrite fixtures. */
#include "synppc/ppc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

static unsigned char memory[512];
static uint32_t base, invalid_word;
static unsigned services, polls, mode;
static bool invalid_run, warm, read_only;
static ROMlib_ppc_registers incoming;
static std::vector<ROMlib_ppc_registers> states;
static const uint32_t values[] = {0, 1, UINT32_MAX, 0xfffffffe, 0x80000000, 0x7fffffff, 0x12345678, 0x87654321};

static void* map(void*, uint32_t address, size_t size, int)
{
    static unsigned char service_code[] = {0x18, 0, 0, 1};
    static unsigned char exit_code[] = {0x18, 0, 0, 0};
    if (size <= 4) {
        if (address == base + 0x118)
            return service_code;
        if (address == base + 0x380)
            return exit_code;
    }
    uint32_t offset = address - base;
    return offset <= 0x118 && size <= 0x118 - offset ? memory + offset : nullptr;
}

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

static uint32_t mfcr(unsigned dest)
{
    return (31u << 26) | (dest << 21) | (19 << 1);
}

static uint32_t mtcrf(unsigned source, unsigned mask)
{
    return (31u << 26) | (source << 21) | (mask << 12) | (144 << 1);
}

static uint32_t mcrxr(unsigned field)
{
    return (31u << 26) | (field << 23) | (512 << 1);
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
        int64_t a = (int32_t)r.gpr[(opcode >> 16) & 31];
        condition(r, (opcode >> 23) & 7, a - (int16_t)opcode);
        return;
    }
    unsigned xo = (opcode >> 1) & 1023, reg = (opcode >> 21) & 31;
    if (xo == 19)
        r.gpr[reg] = r.cr;
    else if (xo == 144) {
        unsigned fields = (opcode >> 12) & 255;
        // Model separate nibbles rather than the native emitter's merged mask.
        for (unsigned field = 0; field < 8; ++field)
            if (fields & (128 >> field)) {
                unsigned shift = 28 - field * 4;
                unsigned nibble = (r.gpr[reg] >> shift) & 15;
                r.cr = (r.cr & ~(15u << shift)) | (nibble << shift);
            }
    }
    else if (xo == 512) {
        unsigned shift = 28 - ((opcode >> 23) & 7) * 4;
        r.cr = (r.cr & ~(15u << shift)) | ((r.xer >> 28) << shift);
        r.xer &= 0x0fffffff;
    }
    else {
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
}

static int service(void*, uint32_t id, ROMlib_ppc_registers* r)
{
    assert(id == 1);
    ROMlib_ppc_registers expected = incoming;
    for (unsigned offset = 0x100; offset < (invalid_run ? 0x110u : 0x118u); offset += 4)
        apply(readword(offset), expected);
    expected.pc = base + (invalid_run ? 0x110 : 0x118);
    assert(!memcmp(r, &expected, sizeof *r));
    if (!mode)
        states.push_back(*r);
    else
        assert(!memcmp(r, &states[services], sizeof *r));
    ++services;
    r->cr = services * 0x9e3779b9u;
    r->xer = ((services & 7) << 29) | (services & 0x7f);
    for (unsigned i = 0; i < 32; ++i)
        r->gpr[i] = values[(services + i) & 7];
    if (invalid_run) {
        if (warm && services == 16)
            word(0x10c, invalid_word);
    }
    else if (services == 12 || services == 24) {
        if (read_only)
            word(0x100, readword(0x100) ^ 0x03e00000);
        else {
            word(0x104, readword(0x104) ^ 0x002ff000);
            word(0x108, readword(0x108) == 0x60000000 ? mcrxr(7) : 0x60000000);
            word(0x110, readword(0x110) ^ 0x400);
        }
    }
    r->lr = services == 256 ? base + 0x380 : base + 0x100;
    incoming = *r;
    return 1;
}

static void poll(void*)
{
    ++polls;
    if (polls == 1) {
        assert(!invalid_run && services == 146);
        // The poll precedes the second instruction, after the first MFCR.
        word(0x104, readword(0x104) ^ (read_only ? 0x03e00000 : 0x00080000));
    }
}

static ROMlib_ppc_registers initial(unsigned seed)
{
    ROMlib_ppc_registers r = {};
    r.pc = base + 0x100;
    r.lr = 0x13579bdf;
    r.ctr = UINT32_MAX;
    r.cr = seed * 0x9e3779b9u;
    r.xer = ((seed & 7) << 29) | ((seed >> 3) & 0x7f);
    for (unsigned i = 0; i < 32; ++i)
        r.gpr[i] = values[(seed + i) & 7];
    return r;
}

static ROMlib_ppc_environment environment(void)
{
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.linear_memory = memory;
    env.linear_address = base;
    env.linear_size = 0x118;
    env.service = service;
    env.poll = poll;
    env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
    return env;
}

static void valid(unsigned seed, unsigned mask, unsigned source, unsigned dest, bool pure)
{
    invalid_run = false;
    read_only = pure;
    base = (seed & 4) ? 0xfffffee0 : 0;
    states.clear();
    ROMlib_ppc_registers reference = {};
    uint64_t count = 0;
    unsigned poll_count = 0;
    for (mode = 0; mode < 2; ++mode) {
        if (pure)
            for (unsigned i = 0; i < 6; ++i)
                word(0x100 + i * 4, mfcr((dest + i * 7) & 31));
        else {
            word(0x100, mfcr(dest));
            word(0x104, mtcrf(source, mask));
            word(0x108, seed & 8 ? mcrxr((seed >> 7) & 7) : 0x60000000);
            word(0x10c, mfcr((dest + 17) & 31));
            unsigned arithmetic_dest = (dest + 7) & 31;
            word(0x110, (31u << 26) | (arithmetic_dest << 21) | (source << 16) | (dest << 11) |
                            ((138 | (seed & 16 ? 512 : 0)) << 1) | 1);
            word(0x114, (11u << 26) | (((seed >> 7) & 7) << 23) | (arithmetic_dest << 16) | (seed & 32 ? 0x8000 : 0));
        }
        ROMlib_ppc_registers r = initial(seed);
        incoming = r;
        services = polls = 0;
        ROMlib_ppc_environment env = environment();
        bool limited = seed % 7 == 0;
        env.instruction_limit = limited ? 350 + seed % 11 : 2000;
        assert(!!ROMlib_ppc_execute(&r, &env) != limited);
        assert(services == (limited ? env.instruction_limit / 7 : 256));
        if (!mode) {
            reference = r;
            count = env.instructions;
            poll_count = polls;
        }
        else {
            assert(!memcmp(&r, &reference, sizeof r));
            assert(env.instructions == count && polls == poll_count);
            if (ROMlib_ppc_jit_available())
                assert(env.native_instructions >= (limited ? 100u : 600u));
        }
    }
}

static void reserved(uint32_t good, uint32_t bad)
{
    invalid_run = true;
    invalid_word = bad;
    base = 0;
    for (unsigned warmed = 0; warmed < 2; ++warmed) {
        warm = warmed;
        states.clear();
        for (mode = 0; mode < 2; ++mode) {
            for (unsigned offset = 0x100; offset < 0x10c; offset += 4)
                word(offset, 0x60000000);
            word(0x10c, warm ? good : bad);
            word(0x110, 0x18000001);
            ROMlib_ppc_registers r = initial(bad);
            incoming = r;
            services = polls = 0;
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
    for (unsigned mask = 0; mask < 256; ++mask)
        for (unsigned source = 0; source < 32; ++source)
            for (unsigned alias = 0; alias < 4; ++alias) {
                unsigned dest = alias == 0 ? source : alias == 1 ? (source + 1) & 31 : alias == 2 ? 0 : 31;
                valid(cases++, mask, source, dest, false);
            }
    for (unsigned dest = 0; dest < 32; ++dest)
        for (unsigned flags = 0; flags < 8; ++flags)
            valid(cases++, 0, 0, dest, true);
    for (unsigned dest = 0; dest < 32; ++dest)
        for (unsigned bit = 0; bit < 26; ++bit)
            if ((1u << bit) & 0x001ff801) {
                reserved(mfcr(dest), mfcr(dest) | (1u << bit));
                ++invalid;
            }
    for (unsigned source = 0; source < 32; ++source)
        for (unsigned mask : {0u, 0x81u, 255u})
            for (unsigned bit : {0u, 11u, 20u}) {
                reserved(mtcrf(source, mask), mtcrf(source, mask) | (1u << bit));
                ++invalid;
            }
    for (unsigned field = 0; field < 8; ++field)
        for (unsigned bit = 0; bit < 26; ++bit)
            if ((1u << bit) & 0x007ff801) {
                reserved(mcrxr(field), mcrxr(field) | (1u << bit));
                ++invalid;
            }
    fflush(stdout);
    fprintf(stderr,
            "%u CR transfer/mask/GPR/alias/XER/producer/limit/rewrite cases; "
            "%u cold and hot reserved encodings passed\n",
            cases, invalid);
}
