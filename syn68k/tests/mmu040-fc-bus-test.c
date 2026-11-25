/*+===================================================================
  File:      mmu040-fc-bus-test.c

  Summary:   Implements mmu040 fc bus test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* One physical address can select distinct banks through its function code. */
#include "mmu040-memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char banks[8][4];
static uint32_t base;
static unsigned reads, writes;

static int read_bytes(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                      unsigned fc)
{
    assert(opaque == banks && !attributes && fc < 8);
    assert(address >= base && address - base <= 4 && size <= 4 - (address - base));
    memcpy(bytes, banks[fc] + address - base, size);
    ++reads;
    return 1;
}

static int write_bytes(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size, uint32_t attributes,
                       unsigned fc)
{
    assert(opaque == banks && !attributes && fc < 8);
    assert(address >= base && address - base <= 4 && size <= 4 - (address - base));
    memcpy(banks[fc] + address - base, bytes, size);
    ++writes;
    return 1;
}

int main(void)
{
    unsigned cases = 0;
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned fc = 0; fc < 8; ++fc)
            for (unsigned size = 1; size <= 4; size *= 2) {
                Syn68kMMU040 mmu;
                syn68k_mmu040_init(&mmu);
                mmu.tc = page8k ? 0x4000 : 0;
                base = (page8k ? 8192 : 4096) - 2;
                for (unsigned bank = 0; bank < 8; ++bank) {
                    banks[bank][0] = 0x11;
                    banks[bank][1] = 0x22;
                    banks[bank][2] = 0x33;
                    banks[bank][3] = bank;
                }
                Syn68kMMU040Memory memory = {.mmu = &mmu,
                                             .tables = {0},
                                             .opaque = banks,
                                             .read = read_bytes,
                                             .write = write_bytes,
                                             .native = NULL};
                reads = writes = 0;
                uint32_t value = 0;
                Syn68kMMU040Access access = syn68k_mmu040_read(&memory, base, fc, size, 0, &value);
                assert(access.completed == size && !access.bus_error);
                assert(value == (size == 1 ? 0x11u : size == 2 ? 0x1122u : 0x11223300u | fc));
                assert(reads == (size == 4 ? ((fc & 3) == 2 ? 2u : 3u) : 1u));
                unsigned char expected[8][4];
                memcpy(expected, banks, sizeof banks);
                value = 0xA1B2C300 | fc;
                for (unsigned i = 0; i < size; ++i)
                    expected[fc][i] = value >> ((size - i - 1) * 8);
                access = syn68k_mmu040_write(&memory, base, fc, size, value);
                assert(access.completed == size && !access.bus_error);
                assert(writes == (size == 4 ? ((fc & 3) == 2 ? 2u : 3u) : 1u));
                assert(!memcmp(banks, expected, sizeof banks));
                ++cases;
            }
    printf("%u function-code bank/width/split bus cases passed.\n", cases);
}
