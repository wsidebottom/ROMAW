/*+===================================================================
  File:      native040-ram-test.c

  Summary:   Verify native MMU descriptor history and operand transfers bypass ordinary RAM callbacks.

  Classes:   None.

  Functions: main

  Origin:    Original ARDI Executor physical-RAM native 68040 vectors.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "native040.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char ram[0x10000] __attribute__((aligned(16)));
static unsigned descriptor_calls, operand_calls;

static void word(uint32_t address, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        ram[address + i] = value >> (24 - i * 8);
}

static uint32_t read_word(uint32_t address)
{
    return (uint32_t)ram[address] << 24 | (uint32_t)ram[address + 1] << 16 | (uint32_t)ram[address + 2] << 8 |
           ram[address + 3];
}

static int table_read(void* opaque, uint32_t address, uint32_t* value)
{
    (void)opaque;
    (void)address;
    (void)value;
    ++descriptor_calls;
    return 0;
}

static int table_write(void* opaque, uint32_t address, uint32_t value, int locked)
{
    (void)opaque;
    (void)address;
    (void)value;
    (void)locked;
    ++descriptor_calls;
    return 0;
}

static int data_read(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                     unsigned fc)
{
    (void)opaque;
    (void)address;
    (void)bytes;
    (void)size;
    (void)attributes;
    (void)fc;
    ++operand_calls;
    return 0;
}

static int data_write(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size, uint32_t attributes,
                      unsigned fc)
{
    return data_read(opaque, address, (unsigned char*)bytes, size, attributes, fc);
}

int main(void)
{
    unsigned cases = 0;
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned fc = 1; fc <= 6; ++fc) {
            if (fc == 3 || fc == 4)
                continue;
            for (unsigned size = 1; size <= 4; size *= 2)
                for (unsigned split = 0; split < size; ++split) {
                    memset(ram, 0, sizeof ram);
                    Syn68kMMU040 mmu = {.tc = page8k ? 0xc000 : 0x8000, .urp = 0x1000, .srp = 0x1000};
                    Syn68kMMU040Memory memory = {.mmu = &mmu,
                                                 .tables = {NULL, table_read, table_write},
                                                 .opaque = NULL,
                                                 .read = data_read,
                                                 .write = data_write,
                                                 .native = NULL};
                    unsigned page_size = page8k ? 8192 : 4096;
                    unsigned logical = page_size * 4;
                    word(0x1000, 0x2002);
                    word(0x2000, 0x3002);
                    word(0x3010, 0x8001);
                    word(0x3014, 0xc001);
                    unsigned address = logical + (split ? page_size - split : 0);
                    Syn68kNative040MMU frame = {
                        .memory = &memory, .address = address, .size = size, .fc = fc, .ram = {ram, 0, sizeof ram}};
                    for (unsigned i = 0; i < size; ++i) {
                        unsigned offset = (address - logical) + i;
                        ram[offset < page_size ? 0x8000 + offset : 0xc000 + offset - page_size] = 0x31 + i;
                    }
                    descriptor_calls = operand_calls = 0;
                    syn68k_native040_entry(NATIVE040_READ)(&frame);
                    assert(frame.access.translation.fault == MMU040_OK && !frame.access.bus_error &&
                           frame.access.completed == size && !descriptor_calls && !operand_calls);
                    uint32_t expected = 0;
                    for (unsigned i = 0; i < size; ++i)
                        expected = expected << 8 | (0x31 + i);
                    assert(frame.value == expected && (read_word(0x1000) & 8) && (read_word(0x2000) & 8) &&
                           (read_word(0x3010) & 8));
                    // A write on the M-clear ATC hit must rewalk and set U/M.
                    frame.value = 0xa1b2c3d4;
                    frame.fc = fc & 4 ? 5 : 1;
                    syn68k_native040_entry(NATIVE040_WRITE)(&frame);
                    assert(frame.access.translation.fault == MMU040_OK && !frame.access.bus_error &&
                           frame.access.completed == size && !descriptor_calls && !operand_calls);
                    assert((read_word(0x3010) & 24) == 24 && (!split || (read_word(0x3014) & 24) == 24));
                    for (unsigned i = 0; i < size; ++i) {
                        unsigned offset = (address - logical) + i;
                        assert(ram[offset < page_size ? 0x8000 + offset : 0xc000 + offset - page_size] ==
                               ((frame.value >> (8 * (size - i - 1))) & 255));
                    }
                    // Alternate FC spaces use the physical callback even inside RAM.
                    frame.fc = 0;
                    syn68k_native040_entry(NATIVE040_READ)(&frame);
                    assert(frame.access.bus_error && !descriptor_calls && operand_calls == 1);
                    ++cases;
                    // Missing or malformed RAM spans must use the checked bus;
                    // a RAM descriptor hit must not authorize an operand outside it.
                    for (unsigned invalid = 0; invalid < 4; ++invalid) {
                        memset(mmu.atc, 0, sizeof mmu.atc);
                        frame.ram = (Syn68kNative040RAM){ram, invalid == 2 ? 0xfffff000u : 0,
                                                         invalid == 0   ? 0
                                                         : invalid == 1 ? 0x1000
                                                         : invalid == 2 ? 0x2000
                                                                        : 0x4000};
                        frame.fc = 1;
                        descriptor_calls = operand_calls = 0;
                        syn68k_native040_entry(NATIVE040_READ)(&frame);
                        if (invalid < 3)
                            assert(frame.access.translation.fault == MMU040_TABLE_BUS_ERROR && descriptor_calls == 1 &&
                                   !operand_calls);
                        else
                            assert(frame.access.bus_error && !descriptor_calls && operand_calls == 1);
                        ++cases;
                    }
                }
        }
    printf("%u native MMU ordinary-RAM/history/ATC-rewalk/split/alternate-FC cases passed without RAM callbacks\n",
           cases);
    return 0;
}
