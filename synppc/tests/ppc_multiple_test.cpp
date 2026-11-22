/*+===================================================================
  File:      ppc_multiple_test.cpp

  Summary:   Implements ppc multiple test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Invalid LMW base overlap must fail before loading any register. */
#include "synppc/ppc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char memory[512], expected_memory[512];
static unsigned data_calls;

static void* map(void*, uint32_t address, size_t size, int)
{
    if (address >= 0x100)
        ++data_calls;
    return address <= sizeof memory && size <= sizeof memory - address ? memory + address : NULL;
}

static void put(unsigned char* bytes, unsigned address, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        bytes[address + i] = value >> (24 - i * 8);
}

static uint32_t get(unsigned address)
{
    uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i)
        result = (result << 8) | memory[address + i];
    return result;
}

int main(void)
{
    unsigned cases = 0, invalid = 0;
    auto cache = ROMlib_ppc_translation_cache_create();
    for (unsigned layout = 0; layout < 4; ++layout)
        for (unsigned load = 0; load < 2; ++load)
            for (unsigned mode = 0; mode < 4; ++mode)
                for (unsigned first = 0; first < 32; ++first)
                    for (unsigned ra = 0; ra < 32; ++ra) {
                        for (unsigned i = 0; i < sizeof memory; ++i)
                            memory[i] = (i * 37 + (i >> 2) * 3) & 255;
                        for (unsigned i = 0; i < 4; ++i)
                            put(memory, 0x40 + 4 * i, 0x60000000);
                        /* Include an odd D-form low bit, and negative displacements. */
                        unsigned address = layout == 3 ? 0x1F8 : layout == 1 ? 0x101 : 0x100;
                        unsigned displacement = ra ? 0xFFE0 - (mode & 1) : address;
                        put(memory, 0x50, ((load ? 46u : 47u) << 26) | first << 21 | ra << 16 | displacement);
                        put(memory, 0x54, 6u << 26);
                        ROMlib_ppc_registers r = {};
                        r.pc = 0x40;
                        for (unsigned i = 0; i < 32; ++i)
                            r.gpr[i] = 0x10203000 + i;
                        if (ra)
                            r.gpr[ra] = address + 0x20 + (mode & 1);
                        r.cr = 0x13579BDF;
                        r.xer = 0xE0000003;
                        r.fpscr = 0xE1070084;
                        ROMlib_ppc_registers expected = r;
                        memcpy(expected_memory, memory, sizeof memory);
                        bool bad = load && ra >= first;
                        unsigned linear_size = 0;
                        if (mode & 2)
                            linear_size = layout == 2 ? address + (32 - first) / 2 * 4 + 1 : sizeof memory;
                        bool fault = false;
                        unsigned expected_calls = 0;
                        for (unsigned i = first; !bad && i < 32; ++i) {
                            unsigned offset = address + (i - first) * 4;
                            expected_calls += offset + 4 > linear_size;
                            if (offset + 4 > sizeof memory) {
                                fault = true;
                                break;
                            }
                            if (load)
                                expected.gpr[i] = get(offset);
                            else
                                put(expected_memory, offset, r.gpr[i]);
                        }
                        ROMlib_ppc_environment env = {};
                        env.memory = map;
                        env.instruction_limit = 10;
                        env.execution_flags = mode & 1 ? ROMLIB_PPC_EXEC_JIT : 0;
                        if (mode & 2) {
                            env.linear_memory = memory;
                            env.linear_size = linear_size;
                        }
                        if (mode == 3 && cache) {
                            env.translation_cache = cache;
                            ROMlib_ppc_translate_range(cache, memory + 0x40, 0x40, 20);
                            // The guard can resume dispatch at the transfer itself.
                            ROMlib_ppc_translate_range(cache, memory + 0x50, 0x50, 4);
                        }
                        data_calls = 0;
                        assert(ROMlib_ppc_execute(&r, &env) == !(bad || fault));
                        expected.pc = (bad || fault) ? 0x50 : 0x58;
                        assert(env.instructions == ((bad || fault) ? 5 : 6));
                        if (bad) {
                            assert(strstr(env.error, "illegal PowerPC"));
                            assert(data_calls == 0);
                            ++invalid;
                        }
                        else if (fault)
                            assert(strstr(env.error, load ? "PowerPC read fault" : "PowerPC write fault"));
                        assert(data_calls == expected_calls);
                        if (mode == 3 && cache && !bad) {
                            bool direct = !(address & 3) && (address + (32 - first) * 4 <= linear_size);
                            const unsigned kernels = 0;

                            if (env.native_instructions != (direct ? 5u : 4u) || env.native_compilations ||
                                env.native_kernel_instructions != kernels) {
                                fprintf(stderr,
                                        "layout %u load %u first %u ra %u direct %d: %llu native, %llu builds,"
                                        " %llu kernel, %llu hits\n",
                                        layout, load, first, ra, direct, (unsigned long long)env.native_instructions,
                                        (unsigned long long)env.native_compilations,
                                        (unsigned long long)env.native_kernel_instructions,
                                        (unsigned long long)env.native_cache_hits);
                                assert(false);
                            }
                        }
                        assert(!memcmp(&r, &expected, sizeof r));
                        assert(!memcmp(memory, expected_memory, sizeof memory));
                        ++cases;
                    }
    ROMlib_ppc_translation_cache_destroy(cache);
    printf("%u multiple-load/store register/alias/address cases passed; "
           "%u invalid base overlaps rejected.\n",
           cases, invalid);
}
