/*+===================================================================
  File:      ppc_memory_jit_test.cpp

  Summary:   Implements ppc memory jit test support for synppc PowerPC execution and fragment loading.

  Classes:   Declared: fixture; Used: ROMlib_ppc_environment, ROMlib_ppc_registers, ROMlib_ppc_translation_cache,
             fixture, std::string

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original scalar memory/endian/bounds/alias/native-fallback fixtures. */
#include "synppc/ppc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include <string>
#include <cmath>

struct fixture {
    unsigned char bytes[0x4000];
    uint32_t base;
    unsigned callbacks = 0;
    ROMlib_ppc_environment env = {};
};

static void* map(void* opaque, uint32_t address, size_t size, int)
{
    fixture& f = *static_cast<fixture*>(opaque);
    ++f.callbacks;
    uint32_t relative = address - f.base;
    return address >= f.base && relative <= sizeof f.bytes && size <= sizeof f.bytes - relative ? f.bytes + relative
                                                                                                : nullptr;
}

static void word(fixture& f, unsigned offset, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        f.bytes[offset + i] = value >> (24 - i * 8);
}

static void prefix(ROMlib_ppc_registers& r)
{
    r.gpr[20] += 1;
    r.gpr[21] += 2;
    r.gpr[22] ^= 0xA5A5;
}

static unsigned cases;

static uint64_t widen_reference(uint32_t word)
{
    unsigned exponent = (word >> 23) & 255;
    unsigned fraction = word & 0x7fffff;
    uint64_t sign = uint64_t(word >> 31) << 63;
    if (exponent == 255)
        return sign | UINT64_C(0x7ff0000000000000) | (uint64_t(fraction) << 29);
    // Exact binary arithmetic for all finite binary32 inputs, including subnormals.
    double magnitude =
        exponent ? std::ldexp(double(0x800000 | fraction), int(exponent) - 150) : std::ldexp(double(fraction), -149);
    uint64_t bits;
    memcpy(&bits, &magnitude, sizeof bits);
    return bits | sign;
}

static uint32_t narrow_reference(uint64_t bits, unsigned rounding)
{
    unsigned exponent = (bits >> 52) & 2047;
    uint32_t sign = uint32_t(bits >> 63) << 31;
    if (exponent < 874 || exponent > 896)
        return sign | (uint32_t(bits >> 32) & 0x40000000) | (uint32_t(bits >> 29) & 0x3fffffff);
    // Round in units of the smallest binary32 subnormal.
    uint64_t significand = (bits & UINT64_C(0xfffffffffffff)) | (UINT64_C(1) << 52);
    uint64_t unit = UINT64_C(1) << (926 - exponent);
    uint32_t fraction = significand / unit;
    uint64_t remainder = significand % unit;
    if (rounding == 0)
        fraction += remainder > unit / 2 || (remainder == unit / 2 && (fraction & 1));
    else if (remainder && ((!sign && rounding == 2) || (sign && rounding == 3)))
        ++fraction;
    return sign | fraction;
}

static void bind(fixture& f, ROMlib_ppc_translation_cache* cache)
{
    f.env = {};
    f.callbacks = 0;
    f.env.opaque = &f;
    f.env.memory = map;
    f.env.linear_memory = f.bytes;
    f.env.linear_address = f.base;
    f.env.linear_size = 0x800;
    f.env.execution_flags = ROMLIB_PPC_EXEC_JIT;
    f.env.translation_cache = cache;
}

static void store_code_overlap()
{
    struct test_case {
        unsigned address, size, value, tail;
        unsigned first_add, second_add, native_count;
    };

    const test_case tests[] = {
        {0x104, 4, 0, 0, 1, 2, 4},                   // Rewrite only the store itself.
        {0x107, 5, 0x0038C600, 0x07000000, 7, 2, 2}, // Cross into the first suffix word.
        {0x10F, 1, 0x07000000, 0, 1, 7, 2},          // Rewrite the last byte of the suffix.
        {0x108, 0, 0, 0, 1, 2, 4},                   // Zero-byte store.
        {0x110, 1, 0x18000000, 0, 1, 2, 4},          // One byte just beyond the block.
    };
    for (auto test : tests) {
        fixture original = {};
        word(original, 0x100, 0x356B0001);                                             // ADDIC. r11,r11,1.
        word(original, 0x104, 31u << 26 | 8u << 21 | 3u << 16 | 4u << 11 | 661u << 1); // STSWX r8,r3,r4.
        word(original, 0x108, 0x38C60001);                                             // ADDI r6,r6,1.
        word(original, 0x10C, 0x38E70002);                                             // ADDI r7,r7,2.
        word(original, 0x110, 0x18000000);
        auto cache = ROMlib_ppc_translation_cache_create();
        assert(cache && ROMlib_ppc_translate_range(cache, original.bytes + 0x100, 0x100, 16) == 1);
        fixture interpreted = original, native = original;
        ROMlib_ppc_registers initial = {};
        initial.pc = 0x100;
        initial.gpr[3] = test.address - 3;
        initial.gpr[4] = 3;
        initial.gpr[8] = test.value;
        initial.gpr[9] = test.tail;
        initial.gpr[11] = UINT32_MAX;
        initial.xer = 0x80000000 | test.size;
        auto expected = initial, actual = initial;
        bind(interpreted, cache);
        interpreted.env.execution_flags = 0;
        bind(native, cache);
        assert(ROMlib_ppc_execute(&expected, &interpreted.env));
        assert(ROMlib_ppc_execute(&actual, &native.env));
        assert(actual.gpr[6] == test.first_add && actual.gpr[7] == test.second_add);
        assert(!memcmp(&actual, &expected, sizeof actual) &&
               !memcmp(native.bytes, interpreted.bytes, sizeof native.bytes));
        assert(native.env.instructions == interpreted.env.instructions && native.env.native_blocks == 1 &&
               native.env.native_instructions == test.native_count && !native.env.native_compilations);
        ROMlib_ppc_translation_cache_destroy(cache);
    }
    puts("Native store prefixes distinguish rewritten suffix bytes from disjoint and empty writes");
}

static void status_fallback(ROMlib_ppc_translation_cache* cache)
{
    const uint32_t addresses[] = {0x500, 0x502, 0x900, 0x4000};
    for (unsigned primary : {32U, 36U})
        for (uint32_t address : addresses) {
            fixture f = {}, original = {};
            f.base = 0;
            word(f, 0x100, 13u << 26 | 20u << 21 | 20u << 16 | 0xFFFF);
            word(f, 0x104, 0x2D010000); // CMPWI cr2,r1,0.
            word(f, 0x108,
                 31u << 26 | 21u << 21 | 21u << 16 | 22u << 11 | 778u << 1 | 1); // ADDO. r21,r21,r22.
            word(f, 0x10C, primary << 26 | 4u << 21 | 3u << 16);
            word(f, 0x110, 0x18000000);
            word(f, 0x500, 0x11223344);
            word(f, 0x502, 0xAABBCCDD);
            word(f, 0x900, 0x12345678);
            original = f;
            ROMlib_ppc_registers before = {}, reference = {};
            before.pc = 0x100;
            before.gpr[1] = 0x80000000;
            before.gpr[3] = address;
            before.gpr[4] = 0x99887766;
            before.gpr[21] = 0x7FFFFFFF;
            before.gpr[22] = 1;
            before.cr = 0x12345678;
            before.xer = 0x20000042;
            unsigned char reference_memory[sizeof f.bytes];
            uint64_t instructions = 0;
            char error[256] = {};
            for (unsigned mode = 0; mode < 2; ++mode) {
                f = original;
                bind(f, cache);
                f.env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
                ROMlib_ppc_translate_range(cache, f.bytes + 0x100, 0x100, 16);
                auto r = before;
                assert(!!ROMlib_ppc_execute(&r, &f.env) == (address != 0x4000));
                if (!mode) {
                    reference = r;
                    memcpy(reference_memory, f.bytes, sizeof f.bytes);
                    instructions = f.env.instructions;
                    memcpy(error, f.env.error, sizeof error);
                }
                else {
                    assert(!memcmp(&r, &reference, sizeof r) && !memcmp(reference_memory, f.bytes, sizeof f.bytes) &&
                           instructions == f.env.instructions && !strcmp(error, f.env.error));
                    assert(f.env.native_instructions == (address == 0x500 ? 4 : 3));
                }
            }
            ++cases;
        }
}

static void persistent_ram(const char* path, bool load_only = false)
{
    fixture first = {}, second = {};
    first.base = second.base = 0x10000000;
    auto cache = ROMlib_ppc_translation_cache_create();
    assert(cache);
    word(first, 0x100, 0x3A940001);
    word(first, 0x104, 0x3AB50002);
    word(first, 0x108, 0x6AD6A5A5);
    word(first, 0x10C, 0x80830000); // LWZ r4,0(r3).
    word(first, 0x110, 0xC8830008); // LFD f4,8(r3).
    word(first, 0x114, 0xD8830010); // STFD f4,16(r3).
    word(first, 0x118, 0x18000000);
    for (unsigned i = 0; i < 32; ++i)
        word(first, 0x200 + i * 4, 0xBC030000); // STMW r0,0(r3).
    word(first, 0x280, 0x18000000);
    memcpy(second.bytes, first.bytes, sizeof first.bytes);
    word(first, 0x500, 0x11223344);
    word(second, 0x500, 0xAABBCCDD);
    word(first, 0x508, 0x7FF00000);
    word(first, 0x50C, 0xDEADBEEF);
    word(second, 0x508, 0x80000000);
    word(second, 0x50C, 0);
    if (load_only)
        assert(ROMlib_ppc_translation_cache_load(cache, path) == 2);
    else {
        assert(ROMlib_ppc_translate_range(cache, first.bytes + 0x100, first.base + 0x100, 24) == 1);
        assert(ROMlib_ppc_translate_range(cache, first.bytes + 0x200, first.base + 0x200, 128) == 1);
    }
    for (unsigned pass = 0; pass < 4; ++pass) {
        if (pass == 2 && path && !load_only) {
            assert(ROMlib_ppc_translation_cache_save(cache, path));
            ROMlib_ppc_translation_cache_destroy(cache);
            cache = ROMlib_ppc_translation_cache_create();
            assert(ROMlib_ppc_translation_cache_load(cache, path) == 2);
        }
        fixture& f = pass & 1 ? second : first;
        bind(f, cache);
        ROMlib_ppc_registers r = {};
        r.pc = f.base + 0x100;
        r.gpr[3] = f.base + 0x500;
        assert(ROMlib_ppc_execute(&r, &f.env));
        uint64_t bits;
        memcpy(&bits, &r.fpr[4], sizeof bits);
        assert(bits == (pass & 1 ? UINT64_C(0x8000000000000000) : UINT64_C(0x7FF00000DEADBEEF)));
        assert(!memcmp(f.bytes + 0x510, f.bytes + 0x508, 8));
        assert(r.gpr[4] == (pass & 1 ? 0xAABBCCDD : 0x11223344) && f.env.native_instructions == 6 &&
               f.env.native_blocks == 1 && !f.env.native_compilations);
        bind(f, cache);
        r.pc = f.base + 0x200;
        r.gpr[3] = f.base + 0x640;
        r.gpr[31] = 0x21436587;
        assert(ROMlib_ppc_execute(&r, &f.env));
        const unsigned char last_word[] = {0x21, 0x43, 0x65, 0x87};
        assert(!memcmp(f.bytes + 0x640 + 31 * 4, last_word, sizeof last_word));
        assert(f.env.native_instructions == 32 && f.env.native_blocks == 1 && !f.env.native_compilations);
        ++cases;
    }
    ROMlib_ppc_translation_cache_destroy(cache);
}

static void single_values(ROMlib_ppc_translation_cache* cache)
{
    fixture f = {};
    word(f, 0x100, 0xC4840004); // LFSU f4,4(r4).
    word(f, 0x104, 0xD4850008); // STFSU f4,8(r5).
    word(f, 0x108, 0xD4C50008); // STFSU f6,8(r5).
    word(f, 0x10C, 0x18000000);
    assert(ROMlib_ppc_translate_range(cache, f.bytes + 0x100, 0x100, 12) == 1);
    for (unsigned exponent = 0; exponent <= 255; ++exponent)
        for (unsigned sample = 0; sample <= 23; ++sample)
            for (unsigned sign : {0u, 1u})
                for (unsigned rounding : {0u, 1u, 2u, 3u}) {
                    uint32_t fraction32 = sample ? (1u << (sample - 1)) | ((sample >> 1) & 1) : 0;
                    uint32_t input = sign << 31 | exponent << 23 | fraction32;
                    const unsigned store_exponent = 874 + (exponent % 23);
                    const uint64_t unit = UINT64_C(1) << (926 - store_exponent);
                    const uint64_t fraction = (UINT64_C(1) << 52) - (UINT64_C(1) + sample % 2) * unit / 2;
                    const uint64_t store_bits = (uint64_t(sign) << 63) | (uint64_t(store_exponent) << 52) | fraction;
                    uint64_t widened = widen_reference(input);
                    for (bool native : {false, true}) {
                        bind(f, cache);
                        f.env.execution_flags = native ? ROMLIB_PPC_EXEC_JIT : 0;
                        word(f, 0x500, input);
                        word(f, 0x508, ~input);
                        word(f, 0x510, 0);
                        ROMlib_ppc_registers r = {};
                        r.pc = 0x100;
                        r.gpr[4] = 0x4FC;
                        r.gpr[5] = 0x500;
                        r.fpscr = 0x82004010 | rounding;
                        memcpy(&r.fpr[6], &store_bits, sizeof store_bits);
                        assert(ROMlib_ppc_execute(&r, &f.env));
                        assert(!memcmp(&r.fpr[4], &widened, sizeof widened));
                        assert(r.gpr[4] == 0x500 && r.gpr[5] == 0x510 && r.fpscr == (0x82004010 | rounding));
                        assert(!memcmp(f.bytes + 0x508, f.bytes + 0x500, 4));
                        uint32_t expected = narrow_reference(store_bits, rounding);
                        for (unsigned i = 0; i < 4; ++i)
                            assert(f.bytes[0x510 + i] == (uint8_t)(expected >> (24 - 8 * i)));
                        assert(f.env.native_instructions == (native ? 3u : 0u) && !f.env.native_compilations);
                    }
                    ++cases;
                }
}

static void persistent_indexed(const char* path, bool load_only = false, bool update = false, bool indexed = true)
{
    fixture first = {}, second = {};
    first.base = second.base = 0x10000000;
    const unsigned forms[] = {23, 87, 279, 343, 151, 215, 407, 534, 790, 662, 918};
    const unsigned displaced[] = {32, 34, 40, 42, 36, 38, 44};
    const unsigned count = indexed && !update ? sizeof forms / sizeof *forms : sizeof displaced / sizeof *displaced;
    auto cache = ROMlib_ppc_translation_cache_create();
    assert(cache);
    std::string filename =
        path ? std::string(path) + (update ? (indexed ? ".update-indexed" : ".update-displaced") : ".indexed") : "";
    for (unsigned i = 0; i < count; ++i) {
        unsigned pc = 0x100 + i * 32;
        word(first, pc, 0x3A940001);
        word(first, pc + 4, 0x3AB50002);
        word(first, pc + 8, 0x6AD6A5A5);
        word(first, pc + 12,
             indexed ? (31u << 26 | 4u << 21 | 3u << 16 | 5u << 11 | (forms[i] | (update ? 32 : 0)) << 1)
                     : ((displaced[i] | 1u) << 26 | 4u << 21 | 3u << 16 | 0x100));
        word(first, pc + 16, 0x18000000);
        if (!load_only)
            assert(ROMlib_ppc_translate_range(cache, first.bytes + pc, first.base + pc, 16) == 1);
    }
    memcpy(second.bytes, first.bytes, sizeof first.bytes);
    if (load_only)
        assert(ROMlib_ppc_translation_cache_load(cache, filename.c_str()) == count);
    for (unsigned pass = 0; pass < 4; ++pass) {
        if (pass == 2 && path && !load_only) {
            assert(ROMlib_ppc_translation_cache_save(cache, filename.c_str()));
            ROMlib_ppc_translation_cache_destroy(cache);
            cache = ROMlib_ppc_translation_cache_create();
            assert(ROMlib_ppc_translation_cache_load(cache, filename.c_str()) == count);
        }
        fixture& f = pass & 1 ? second : first;
        for (unsigned i = 0; i < count; ++i) {
            bind(f, cache);
            uint32_t input = pass & 1 ? 0x76543210 : 0x89ABCDEF;
            word(f, 0x500, input);
            ROMlib_ppc_registers r = {};
            r.pc = f.base + 0x100 + i * 32;
            r.gpr[3] = f.base + 0x400;
            r.gpr[5] = 0x100;
            r.gpr[4] = 0xDDEEFF11;
            assert(ROMlib_ppc_execute(&r, &f.env));
            const uint32_t expected_loads[] = {
                input,
                input >> 24,
                input >> 16,
                (uint32_t)(int32_t)(int16_t)(input >> 16),
                0xDDEEFF11,
                0xDDEEFF11,
                0xDDEEFF11,
                pass & 1 ? 0x10325476u : 0xEFCDAB89u,
                pass & 1 ? 0x5476u : 0xAB89u,
                0xDDEEFF11,
                0xDDEEFF11,
            };
            uint32_t expected = expected_loads[i];
            assert(r.gpr[4] == expected && r.gpr[3] == f.base + (update ? 0x500 : 0x400) && r.gpr[5] == 0x100 &&
                   f.env.native_instructions == 4 && !f.env.native_compilations);
            constexpr unsigned store_sizes[] = {0, 0, 0, 0, 4, 1, 2, 0, 0, 4, 2};
            unsigned size = store_sizes[i];
            if (size) {
                for (unsigned n = 0; n < size; ++n)
                    assert(f.bytes[0x500 + n] == ((expected >> ((i >= 9 ? n : size - n - 1) * 8)) & 255));
            }
            ++cases;
        }
    }
    ROMlib_ppc_translation_cache_destroy(cache);
}

static void self_modification(bool indexed = false, bool update = false, bool publication = false)
{
    for (unsigned own_prefix = 0; own_prefix < 2; ++own_prefix) {
        fixture f = {};
        f.base = 0;
        auto cache = ROMlib_ppc_translation_cache_create();
        word(f, 0x100, 0x3A940001);
        word(f, 0x104, 0x3AB50002);
        word(f, 0x108, 0x6AD6A5A5);
        word(f, 0x10C,
             indexed ? (31u << 26 | 5u << 21 | 3u << 16 | 4u << 11 | (151u | (update ? 32u : 0u)) << 1)
                     : ((36u | (update ? 1u : 0u)) << 26 | 5u << 21 | 3u << 16 | 4));
        unsigned target = 0x110;
        unsigned cache_barriers = 0;
        if (publication) {
            word(f, 0x110, 0x7C0004AC); // sync
            word(f, 0x114, 0x7C003FAC); // icbi 0,r7
            word(f, 0x118, 0x4C00012C); // isync
            target += 12;
            cache_barriers = 3;
        }
        word(f, target, 0x38C60001); // ADDI r6,r6,1, possibly replaced before execution.
        word(f, target + 4, 0x18000000);
        assert(ROMlib_ppc_translate_range(cache, f.bytes + 0x100, 0x100, target + 4 - 0x100) == 1 + cache_barriers);
        for (unsigned pass = 0; pass < 10; ++pass) {
            bind(f, cache);
            ROMlib_ppc_registers r = {};
            r.pc = 0x100;
            r.gpr[7] = own_prefix ? 0x100 : target;
            r.gpr[3] = r.gpr[7] - 4;
            r.gpr[4] = 4;
            r.gpr[5] = own_prefix ? 0x3A940003 : 0x38C60007;
            assert(ROMlib_ppc_execute(&r, &f.env));
            assert(r.gpr[20] == (own_prefix && pass ? 3 : 1) && r.gpr[6] == (own_prefix ? 1 : 7) &&
                   f.env.instructions == 6 + cache_barriers);
            assert(r.gpr[3] == r.gpr[7] - (update ? 0 : 4));
            if (!pass || pass == 9)
                assert(f.env.native_instructions == 4u + cache_barriers + (own_prefix || pass == 9));
            if (publication)
                assert(!f.env.native_kernel_instructions);
            ++cases;
        }
        ROMlib_ppc_translation_cache_destroy(cache);
    }
}

static void check(ROMlib_ppc_translation_cache* cache, uint32_t base, unsigned extent, unsigned primary, unsigned ra,
                  unsigned reg, uint32_t target, unsigned variant, unsigned limit = 0, int rb = -1, bool update = false,
                  bool reverse = false)
{
    fixture f, initial;
    for (unsigned i = 0; i < sizeof f.bytes; ++i)
        f.bytes[i] = (i * 37 + (i >> 3) * 17 + 0x93) & 255;
    f.base = base;
    bool floating = primary >= 48;
    bool wide = primary == 50 || primary == 54;
    unsigned size = wide                                                ? 8
                    : (primary == 34 || primary == 38)                  ? 1
                    : (primary == 40 || primary == 42 || primary == 44) ? 2
                                                                        : 4;
    bool writing = primary == 36 || primary == 38 || primary == 44 || primary >= 52;
    if (update && (!ra || (!writing && !floating && ra == reg)))
        return; // Invalid update encodings are exercised separately below.
    int16_t displacement = ra ? (variant & 1 ? -19 : 13) : (int16_t)target;
    word(f, 0x100, 0x3A940001); // ADDI r20,r20,1.
    word(f, 0x104, 0x3AB50002); // ADDI r21,r21,2.
    word(f, 0x108, 0x6AD6A5A5); // XORI r22,r22,A5A5.
    if (rb < 0)
        word(f, 0x10C, (primary | (update ? 1u : 0u)) << 26 | reg << 21 | ra << 16 | (uint16_t)displacement);
    else {
        unsigned xo = primary == 32   ? 23
                      : primary == 34 ? 87
                      : primary == 40 ? 279
                      : primary == 42 ? 343
                      : primary == 36 ? 151
                      : primary == 38 ? 215
                      : primary == 44 ? 407
                      : primary == 48 ? 535
                      : primary == 50 ? 599
                      : primary == 52 ? 663
                      : primary == 54 ? 727
                                      : 983;
        if (reverse)
            xo += 511; // The four reverse forms set XO bit 9 and clear bit 0.
        word(f, 0x10C, 31u << 26 | reg << 21 | ra << 16 | (unsigned)rb << 11 | (xo | (update ? 32u : 0u)) << 1);
    }
    word(f, 0x110, 0x18000000);
    initial = f;
    ROMlib_ppc_registers before = {};
    for (unsigned i = 0; i < 32; ++i) {
        before.gpr[i] = 0x81234567u * (i + 1);
        uint64_t bits = UINT64_C(0x7FF0000000000001) + i;
        memcpy(&before.fpr[i], &bits, sizeof bits);
    }
    auto set_live = [&](unsigned r, uint32_t value) {
        before.gpr[r] = r == 20 ? value - 1 : r == 21 ? value - 2 : r == 22 ? value ^ 0xA5A5 : value;
    };
    if (rb >= 0) {
        uint32_t index = ra == (unsigned)rb && ra ? target / 2 : ra ? 0x90000000u + variant : target;
        if (ra && ra != (unsigned)rb)
            set_live(ra, target - index);
        set_live(rb, index);
    }
    else if (ra) {
        before.gpr[ra] = target - (int32_t)displacement;
        if (ra == 20)
            --before.gpr[ra];
        if (ra == 21)
            before.gpr[ra] -= 2;
        if (ra == 22)
            before.gpr[ra] ^= 0xA5A5;
    }
    before.pc = base + 0x100;
    before.lr = 0x12345678;
    before.ctr = 0xABCD1234;
    before.cr = 0x87324561;
    before.xer = 0xE0000042;
    before.fpscr = 0x82004010;
    ROMlib_ppc_registers expected = before;
    prefix(expected);
    uint32_t ea = (ra ? expected.gpr[ra] : 0) + (rb < 0 ? (uint32_t)(int32_t)displacement : expected.gpr[rb]);
    uint32_t relative = ea - base;
    bool allowed = ea >= base && relative <= sizeof f.bytes && size <= sizeof f.bytes - relative &&
                   (uint64_t)ea + size <= UINT64_C(0x100000000);
    unsigned char expected_bytes[sizeof f.bytes];
    memcpy(expected_bytes, f.bytes, sizeof expected_bytes);
    if (allowed && (!limit || limit >= 4)) {
        if (writing) {
            uint64_t value = expected.gpr[reg];
            if (floating)
                memcpy(&value, &expected.fpr[reg], sizeof value);
            if (primary == 52)
                value = narrow_reference(value, before.fpscr & 3);
            for (unsigned i = 0; i < size; ++i)
                expected_bytes[relative + i] = value >> ((reverse ? i : size - 1 - i) * 8);
        }
        else {
            uint64_t value = 0;
            for (unsigned i = 0; i < size; ++i)
                value = value << 8 | f.bytes[relative + (reverse ? size - 1 - i : i)];
            if (primary == 48)
                value = widen_reference((uint32_t)value);
            if (floating)
                memcpy(&expected.fpr[reg], &value, sizeof value);
            else
                expected.gpr[reg] = primary == 42 ? (uint32_t)(int32_t)(int16_t)value : (uint32_t)value;
        }
        if (update)
            expected.gpr[ra] = ea;
    }
    ROMlib_ppc_registers interpreted = {};
    char error[256] = {};
    uint64_t instructions = 0;
    for (unsigned mode = 0; mode < 2; ++mode) {
        f = initial;
        f.env.opaque = &f;
        f.env.memory = map;
        f.env.linear_memory = f.bytes;
        f.env.linear_address = base;
        f.env.linear_size = extent;
        f.env.instruction_limit = limit;
        f.env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
        f.env.translation_cache = cache;
        assert(ROMlib_ppc_translate_range(cache, f.bytes + 0x100, base + 0x100, 16) <= 1);
        ROMlib_ppc_registers r = before;
        int success = ROMlib_ppc_execute(&r, &f.env);
        assert(!!success == (allowed && (!limit || limit >= 5)));
        bool linear = relative <= extent && size <= extent - relative;
        bool reaches_memory = !limit || limit >= 4;
        bool valid_extent = (uint64_t)ea + size <= UINT64_C(0x100000000);
        assert(f.callbacks == (reaches_memory && !linear && valid_extent ? 1U : 0U));
        if (!limit || limit >= 3) {
            assert(!memcmp(r.gpr, expected.gpr, sizeof r.gpr));
            assert(!memcmp(f.bytes, expected_bytes, sizeof expected_bytes));
        }
        assert(r.cr == before.cr && r.xer == before.xer && r.ctr == before.ctr && r.lr == before.lr &&
               r.fpscr == before.fpscr && !memcmp(r.fpr, expected.fpr, sizeof r.fpr));
        if (!mode) {
            interpreted = r;
            instructions = f.env.instructions;
            memcpy(error, f.env.error, sizeof error);
        }
        else {
            assert(!memcmp(&r, &interpreted, sizeof r) && f.env.instructions == instructions &&
                   !strcmp(error, f.env.error));
            bool direct = !(ea & (size - 1)) && relative <= extent && size <= extent - relative;
            unsigned native_count = reaches_memory ? (direct ? 4 : 3) : limit;
            assert(f.env.native_blocks == 1 && f.env.native_instructions == native_count &&
                   !f.env.native_kernel_instructions);
        }
    }
    ++cases;
}

static void invalid_updates(void)
{
    const unsigned forms[] = {33, 35, 41, 43, 37, 39, 45, 49, 51, 53, 55};
    const unsigned indexed_forms[] = {55, 119, 311, 375, 183, 247, 439, 567, 631, 695, 759};
    for (unsigned form = 0; form < sizeof forms / sizeof *forms; ++form)
        for (bool indexed : {false, true})
            for (unsigned reg = 0; reg < 32; ++reg)
                for (unsigned invalid = 0; invalid < (form < 4 ? 2u : 1u); ++invalid)
                    for (bool warm : {false, true}) {
                        fixture f = {};
                        auto cache = ROMlib_ppc_translation_cache_create();
                        assert(cache);
                        unsigned valid_ra = reg == 1 ? 2 : 1;
                        auto opcode = [&](unsigned ra) {
                            return indexed ? (31u << 26 | reg << 21 | ra << 16 | 3u << 11 | indexed_forms[form] << 1)
                                           : (forms[form] << 26 | reg << 21 | ra << 16);
                        };
                        word(f, 0x100, 0x3A940001);
                        word(f, 0x104, 0x3AB50002);
                        word(f, 0x108, 0x6AD6A5A5);
                        word(f, 0x10C, opcode(valid_ra));
                        word(f, 0x110, 0x18000000);
                        ROMlib_ppc_registers before = {};
                        before.pc = 0x100;
                        before.gpr[valid_ra] = 0x500;
                        before.cr = 0x13579BDF;
                        before.xer = 0xE0000042;
                        before.fpscr = 0x82004010;
                        if (warm) {
                            assert(ROMlib_ppc_translate_range(cache, f.bytes + 0x100, 0x100, 16) == 1);
                            bind(f, cache);
                            auto r = before;
                            assert(ROMlib_ppc_execute(&r, &f.env));
                            assert(f.env.native_instructions == 4);
                        }
                        word(f, 0x10C, opcode(invalid ? reg : 0));
                        unsigned char saved[sizeof f.bytes];
                        memcpy(saved, f.bytes, sizeof saved);
                        auto expected = before;
                        prefix(expected);
                        expected.pc = 0x10C;
                        for (unsigned mode = 0; mode < 2; ++mode) {
                            bind(f, cache);
                            f.env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
                            auto r = before;
                            assert(!ROMlib_ppc_execute(&r, &f.env));
                            assert(strstr(f.env.error, "illegal PowerPC update register"));
                            assert(!memcmp(&r, &expected, sizeof r) && !memcmp(saved, f.bytes, sizeof saved) &&
                                   !f.callbacks && f.env.instructions == 4);
                        }
                        ROMlib_ppc_translation_cache_destroy(cache);
                        ++cases;
                    }
}

int main(int argc, char** argv)
{
    assert(ROMlib_ppc_jit_available());
    if (argc == 3 && !strcmp(argv[1], "--load")) {
        persistent_ram(argv[2], true);
        persistent_indexed(argv[2], true);
        persistent_indexed(argv[2], true, true);
        persistent_indexed(argv[2], true, true, false);
        puts("Persisted native memory blocks reused in a new process without compilation");
        return 0;
    }
    store_code_overlap();
    auto cache = ROMlib_ppc_translation_cache_create();
    assert(cache);
    const unsigned forms[] = {32, 34, 40, 42, 36, 38, 44};
    const unsigned registers[] = {0, 1, 2, 20, 21, 22, 31};
    const uint32_t bases[] = {0, 0x10000000, 0xFFFFE000};
    const unsigned extents[] = {0x800, 0x803, 0x2000};
    for (unsigned form : forms)
        for (uint32_t base : bases)
            for (unsigned extent : extents)
                for (unsigned ra : registers)
                    for (unsigned reg : registers) {
                        uint32_t positions[] = {0x400,      0x401,      0x402,      0x403,  extent - 4,
                                                extent - 3, extent - 2, extent - 1, extent, extent + 1,
                                                extent + 4, 0xFFFFFFFF, 0xFFFFFFFC};
                        for (unsigned i = 0; i < sizeof positions / sizeof *positions; ++i)
                            for (bool update : {false, true})
                                check(cache, base, extent, form, ra, reg, base + positions[i], i, 0, -1, update);
                    }
    for (unsigned limit = 1; limit <= 6; ++limit)
        for (unsigned form : forms) {
            for (bool update : {false, true}) {
                check(cache, 0, 0x800, form, 1, 2, 0x404, 0, limit, -1, update);
                check(cache, 0, 0x800, form, 1, 2, 0x404, 0, limit, 3, update);
            }
        }
    for (unsigned form : forms)
        for (uint32_t base : bases)
            for (unsigned ra : registers)
                for (unsigned rb : registers)
                    for (unsigned reg : registers)
                        for (uint32_t position :
                             {0x400U, 0x401U, 0x402U, 0x7FCU, 0x7FEU, 0x800U, 0x900U, 0x4000U, 0xFFFFFFFCU})
                            for (bool update : {false, true})
                                check(cache, base, 0x800, form, ra, reg, base + position, position, 0, rb, update);
    for (unsigned form : {32u, 40u, 36u, 44u})
        for (uint32_t base : bases)
            for (unsigned ra : registers)
                for (unsigned rb : registers)
                    for (unsigned reg : registers)
                        for (uint32_t position : {0x400u, 0x401u, 0x7FEu, 0x900u, 0x4000u, 0xFFFFFFFCu})
                            check(cache, base, 0x800, form, ra, reg, base + position, position, 0, rb, false, true);
    for (unsigned form : {32u, 40u, 36u, 44u})
        for (unsigned limit = 1; limit <= 6; ++limit)
            for (uint32_t position : {0x400u, 0x401u, 0x900u, 0x4000u})
                check(cache, 0, 0x800, form, 1, 2, position, 0, limit, 3, false, true);
    for (unsigned form : {48u, 50u, 52u, 54u, 983u})
        for (uint32_t base : bases)
            for (unsigned ra : registers)
                for (int rb : {-1, 0, 2, 20, 31})
                    for (unsigned reg : registers)
                        for (uint32_t position : {0x400u, 0x404u, 0x7F8u, 0x7FCu, 0x900u, 0x4000u, 0xFFFFFFFCu})
                            for (bool update : {false, true})
                                if (form != 983 || (rb >= 0 && !update))
                                    check(cache, base, 0x800, form, ra, reg, base + position, position, 0, rb, update);
    for (unsigned form : {48u, 50u, 52u, 54u})
        for (unsigned limit = 1; limit <= 6; ++limit)
            for (uint32_t position : {0x400u, 0x404u, 0x900u, 0x4000u})
                check(cache, 0, 0x800, form, 1, 1, position, 0, limit, 3, true);
    status_fallback(cache);
    invalid_updates();
    single_values(cache);
    persistent_ram(argc > 1 ? argv[1] : nullptr);
    persistent_indexed(argc > 1 ? argv[1] : nullptr);
    persistent_indexed(argc > 1 ? argv[1] : nullptr, false, true);
    persistent_indexed(argc > 1 ? argv[1] : nullptr, false, true, false);
    for (bool indexed : {false, true})
        for (bool update : {false, true})
            self_modification(indexed, update);
    self_modification(false, false, true);
    ROMlib_ppc_translation_cache_destroy(cache);
    printf("%u native scalar memory/endian/alias/bounds/fallback/limit cases passed\n", cases);
}
