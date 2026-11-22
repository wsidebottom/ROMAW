/*+===================================================================
  File:      ppc_alu_test.cpp

  Summary:   Implements ppc alu test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original integer instruction conformance vectors, PowerPC PEM chapter 8. */
#include "synppc/ppc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char memory[8];
static unsigned count;

static void* map(void*, uint32_t address, size_t size, int)
{
    return address <= sizeof memory && size <= sizeof memory - address ? memory + address : NULL;
}

static ROMlib_ppc_registers check(unsigned operation, uint32_t first, uint32_t second, unsigned flags, unsigned alias,
                                  bool oe, bool rc)
{
    unsigned dst = alias == 0 || alias == 3 ? 3 : alias == 1 || alias == 4 ? 1 : 2;
    unsigned other = alias >= 3 ? 1 : 2;
    ROMlib_ppc_registers r = {};
    for (unsigned i = 0; i < 32; ++i)
        r.gpr[i] = 0x91000000 + i * 0x1111;
    r.gpr[1] = first;
    r.gpr[2] = second;
    bool zero_source = operation == 7 && alias == 5;
    if (zero_source)
        r.gpr[0] = first;
    r.cr = 0x13579bdf;
    r.xer = (flags << 29) | 0x42;
    uint32_t a = r.gpr[zero_source ? 0 : 1], b = r.gpr[other];
    unsigned carry = flags & 1;
    bool affects_carry = true, undefined = false;
    int64_t exact;
    uint64_t sum;
    bool unary = false;
    switch (operation) {
    case 266: // ADD
    case 10:  // ADDC
    case 138: // ADDE
        sum = uint64_t(a) + b + (operation == 138 ? carry : 0);
        exact = int64_t(int32_t(a)) + int32_t(b) + (operation == 138 ? carry : 0);
        affects_carry = operation != 266;
        break;
    case 234: // ADDME
        sum = uint64_t(a) + UINT32_MAX + carry;
        exact = int64_t(int32_t(a)) + carry - 1;
        unary = true;
        break;
    case 202: // ADDZE
        sum = uint64_t(a) + carry;
        exact = int64_t(int32_t(a)) + carry;
        unary = true;
        break;
    case 40:  // SUBF
    case 8:   // SUBFC
    case 136: // SUBFE
        sum = uint64_t(~a) + b + (operation == 136 ? carry : 1);
        exact = int64_t(int32_t(b)) - int32_t(a) + (operation == 136 ? int(carry) - 1 : 0);
        affects_carry = operation != 40;
        break;
    case 232: // SUBFME
        sum = uint64_t(~a) + UINT32_MAX + carry;
        exact = -int64_t(int32_t(a)) + carry - 2;
        unary = true;
        break;
    case 200: // SUBFZE
        sum = uint64_t(~a) + carry;
        exact = -int64_t(int32_t(a)) + carry - 1;
        unary = true;
        break;
    case 104: // NEG
        exact = -int64_t(int32_t(a));
        sum = uint64_t(exact);
        affects_carry = false;
        unary = true;
        break;
    case 19: // MFCR
        sum = r.cr;
        exact = 0;
        affects_carry = false;
        unary = true;
        break;
    case 26: // CNTLZW
        sum = 32;
        for (unsigned bit = 0; bit < 32; ++bit)
            if (a & (1u << bit))
                sum = 31 - bit;
        exact = int32_t(sum);
        affects_carry = false;
        unary = true;
        break;
    case 954: // EXTSB
    case 922: // EXTSH
    {
        uint32_t mask = operation == 954 ? 255 : 65535;
        uint32_t sign = operation == 954 ? 128 : 32768;
        sum = (a & mask) | (a & sign ? ~mask : 0);
        exact = int32_t(sum);
        affects_carry = false;
        unary = true;
        break;
    }
    case 7: // MULLI (primary opcode)
        exact = int64_t(int32_t(a)) * int16_t(second);
        sum = uint64_t(exact);
        affects_carry = false;
        break;
    case 11: // MULHWU
        exact = 0;
        sum = (uint64_t(a) * b) >> 32;
        affects_carry = false;
        break;
    case 75: // MULHW
        exact = int64_t(int32_t(a)) * int32_t(b);
        sum = uint64_t(exact) >> 32;
        affects_carry = false;
        break;
    case 235: // MULLW
        exact = int64_t(int32_t(a)) * int32_t(b);
        sum = uint64_t(exact);
        affects_carry = false;
        break;
    case 491: // DIVW
    case 459: // DIVWU
        undefined = !b || (operation == 491 && a == 0x80000000 && b == UINT32_MAX);
        exact = undefined          ? INT64_C(0x80000000)
                : operation == 491 ? int64_t(int32_t(a)) / int32_t(b)
                                   : int64_t(uint64_t(a) / b);
        sum = uint64_t(exact);
        affects_carry = false;
        break;
    case 24:  // SLW
    case 536: // SRW
    case 792: // SRAW
    case 824: // SRAWI
    {
        unsigned shift = operation == 824 ? second & 31 : b & 63;
        bool arithmetic = operation == 792 || operation == 824;
        bool negative = a & 0x80000000;
        uint32_t value = shift >= 32 ? 0 : operation == 24 ? a << shift : a >> shift;
        if (arithmetic && negative && shift)
            value |= shift >= 32 ? UINT32_MAX : UINT32_MAX << (32 - shift);
        affects_carry = arithmetic;
        bool carry = arithmetic && negative && (shift >= 32 || (shift && (a & ((1u << shift) - 1))));
        sum = uint64_t(value) | (uint64_t(carry) << 32);
        exact = int32_t(value);
        break;
    }
    default:
        abort();
    }
    uint32_t output = uint32_t(sum), xer = r.xer;
    if (affects_carry)
        xer = (xer & ~0x20000000u) | (sum >> 32 ? 0x20000000 : 0);
    if (oe) {
        bool overflow = undefined || exact > INT32_MAX || exact < INT32_MIN;
        // Unsigned division cannot overflow when its divisor is nonzero.
        if (operation == 459)
            overflow = undefined;
        xer = (xer & ~0x40000000u) | (overflow ? 0xc0000000 : 0);
    }
    uint32_t cr = r.cr;
    if (rc)
        cr = (cr & 0x0fffffff) | ((!output ? 2u : output & 0x80000000 ? 8u : 4u) << 28) | ((xer >> 3) & 0x10000000);
    uint32_t opcode =
        (31u << 26) | (dst << 21) | (1 << 16) | ((unary ? 0 : other) << 11) | (oe << 10) | (operation << 1) | rc;
    if (operation == 7)
        opcode = (7u << 26) | (dst << 21) | (zero_source ? 0 : 1 << 16) | (second & 65535);
    if (operation == 19)
        opcode = (31u << 26) | (dst << 21) | (19 << 1);
    if (operation == 26 || operation == 954 || operation == 922)
        opcode = (31u << 26) | (1 << 21) | (dst << 16) | (operation << 1) | rc;
    if (operation == 24 || operation == 536 || operation == 792 || operation == 824)
        opcode = (31u << 26) | (1 << 21) | (dst << 16) | ((operation == 824 ? second & 31 : other) << 11) |
                 (operation << 1) | rc;
    for (unsigned i = 0; i < 4; ++i)
        memory[i] = opcode >> (24 - i * 8);
    memory[4] = 0x18;
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.instruction_limit = 4;
    ROMlib_ppc_registers prior = r;
    assert(ROMlib_ppc_execute(&r, &env));
    if ((!undefined && r.gpr[dst] != output) || r.xer != xer ||
        (undefined && rc ? (r.cr & 0x1fffffff) != (cr & 0x1fffffff) : r.cr != cr)) {
        fprintf(stderr,
                "PPC ALU %08x a=%08x b=%08x XER=%08x: "
                "result %08x/%08x XER %08x/%08x CR %08x/%08x\n",
                opcode, a, b, prior.xer, r.gpr[dst], output, r.xer, xer, r.cr, cr);
        exit(1);
    }
    prior.gpr[dst] = r.gpr[dst];
    assert(!memcmp(prior.gpr, r.gpr, sizeof r.gpr));
    assert(r.lr == prior.lr && r.ctr == prior.ctr && r.fpscr == prior.fpscr);
    assert(r.pc == 8 && env.instructions == 2);
    assert(!memcmp(r.fpr, prior.fpr, sizeof r.fpr));
    ++count;
    return r;
}

static void reserved_multiply(void)
{
    const unsigned operations[] = {11, 75};
    for (unsigned operation : operations)
        for (unsigned dst = 0; dst < 32; ++dst)
            for (unsigned rc = 0; rc < 2; ++rc)
                for (unsigned flags = 0; flags < 8; ++flags) {
                    uint32_t opcode =
                        (31u << 26) | (dst << 21) | (1 << 16) | (2 << 11) | (1 << 10) | (operation << 1) | rc;
                    for (unsigned i = 0; i < 4; ++i)
                        memory[i] = opcode >> (24 - i * 8);
                    memory[4] = 0x18;
                    ROMlib_ppc_registers r = {};
                    for (unsigned i = 0; i < 32; ++i)
                        r.gpr[i] = 0xf1234567u * (i + 1);
                    r.cr = 0x13579bdf;
                    r.xer = (flags << 29) | 0x42;
                    ROMlib_ppc_registers prior = r;
                    ROMlib_ppc_environment env = {};
                    env.memory = map;
                    assert(!ROMlib_ppc_execute(&r, &env));
                    assert(strstr(env.error, "illegal PowerPC instruction"));
                    assert(!memcmp(&r, &prior, sizeof r));
                }
    puts("1024 reserved multiply encodings fault before register/CR/XER changes");
}

static void reserved_unary(void)
{
    unsigned invalid_count = 0;
    const unsigned operations[] = {26, 954, 922, 104, 19};
    for (unsigned operation : operations)
        for (unsigned fields = 1; fields < (operation == 19 ? 2048u : 32u); ++fields)
            for (unsigned rc = 0; rc < (operation == 19 ? 1u : 2u); ++rc)
                for (unsigned oe = 0; oe < (operation == 104 ? 2u : 1u); ++oe) {
                    uint32_t opcode =
                        (31u << 26) | (1 << 21) | (2 << 16) | (fields << 11) | (oe << 10) | (operation << 1) | rc;
                    if (operation == 19)
                        opcode = (31u << 26) | (1 << 21) | ((fields & 1023) << 11) | (fields >> 10) | (19 << 1);
                    for (unsigned i = 0; i < 4; ++i)
                        memory[i] = opcode >> (24 - i * 8);
                    memory[4] = 0x18;
                    ROMlib_ppc_registers r = {};
                    for (unsigned i = 0; i < 32; ++i)
                        r.gpr[i] = 0xf1234567u * (i + 1);
                    r.cr = 0x9abcdef0;
                    r.xer = 0xe0000042;
                    ROMlib_ppc_registers prior = r;
                    ROMlib_ppc_environment env = {};
                    env.memory = map;
                    assert(!ROMlib_ppc_execute(&r, &env));
                    assert(strstr(env.error, "illegal PowerPC instruction"));
                    assert(!memcmp(&r, &prior, sizeof r));
                    ++invalid_count;
                }
    printf("%u reserved unary/MFCR encodings fault before state changes\n", invalid_count);
}

int main(int argc, char** argv)
{
    if (argc == 2 && !strcmp(argv[1], "--reserved-multiply")) {
        reserved_multiply();
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--reserved-unary")) {
        reserved_unary();
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--oracle")) {
        unsigned op, a, b, flags, alias, oe, rc, result, xer, cr;
        while (scanf("%u %x %x %u %u %u %u %x %x %x", &op, &a, &b, &flags, &alias, &oe, &rc, &result, &xer, &cr) ==
               10) {
            ROMlib_ppc_registers r = check(op, a, b, flags, alias, oe, rc);
            unsigned dst = alias == 0 || alias == 3 ? 3 : alias == 1 || alias == 4 ? 1 : 2;
            if (r.gpr[dst] != result || r.xer != xer || r.cr != cr) {
                fprintf(stderr,
                        "QEMU 750 mismatch op=%u a=%08x b=%08x: "
                        "%08x/%08x %08x/%08x %08x/%08x\n",
                        op, a, b, r.gpr[dst], result, r.xer, xer, r.cr, cr);
                return 1;
            }
        }
        assert(count >= 5000);
        printf("%u independent QEMU 750 integer cases passed\n", count);
        return 0;
    }
    const unsigned operations[] = {266, 10,  138, 234, 202, 40, 8,  136, 232, 200,
                                   104, 235, 491, 459, 11,  75, 19, 26,  954, 922};
    const uint32_t values[] = {0,          1,          2,          UINT32_MAX, 0xfffffffe,
                               0x80000000, 0x80000001, 0x7fffffff, 0x12345678, 0x87654321};
    for (unsigned op : operations)
        for (uint32_t a : values)
            for (uint32_t b : values)
                for (unsigned flags = 0; flags < 8; ++flags)
                    for (unsigned alias = 0; alias < 5; ++alias)
                        for (unsigned oe = 0; oe < 2; ++oe)
                            for (unsigned rc = 0; rc < 2; ++rc) {
                                bool extra_unary = op == 19 || op == 26 || op == 954 || op == 922;
                                if (extra_unary && (b || oe || (op == 19 && rc)))
                                    continue;
                                if (!oe || (op != 11 && op != 75))
                                    check(op, a, b, flags, alias, oe, rc);
                            }
    const uint32_t immediates[] = {0, 1, 0x7fff, 0x8000, 0xffff, 0xfffe, 0x8001, 0x1234};
    for (uint32_t a : values)
        for (uint32_t b : immediates)
            for (unsigned flags = 0; flags < 8; ++flags)
                for (unsigned alias = 0; alias < 6; ++alias)
                    check(7, a, b, flags, alias, false, false);
    const unsigned shifts[] = {24, 536, 792, 824};
    const uint32_t amounts[] = {0, 1, 7, 15, 16, 31, 32, 33, 63, 64, 65, 127, 128, UINT32_MAX};
    for (unsigned op : shifts)
        for (uint32_t a : values)
            for (uint32_t b : amounts)
                for (unsigned flags = 0; flags < 8; ++flags)
                    for (unsigned alias = 0; alias < 5; ++alias)
                        for (unsigned rc = 0; rc < 2; ++rc)
                            check(op, a, b, flags, alias, false, rc);
    reserved_multiply();
    reserved_unary();
    printf("%u PPC integer carry/overflow/multiply/division/shift/CR/alias cases passed\n", count);
}
