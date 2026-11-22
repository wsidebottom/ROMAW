/*+===================================================================
  File:      driver.c

  Summary:   Implements driver support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: reserve_reference_test_memory, test_all_instructions

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#include "driver.h"
#include "testbattery.h"
#include "setup.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "crc.h"

#include <unistd.h>

#if defined(__MINGW32__)
#include <windows.h>
#endif

/* These are set by command-line switches. */
int test_only_non_cc_variants; /* If 0, test only cc setting variants. */
int generate_crc;

uint32 times_called;
uint16* ccr_addr;
uint32* return_address_addr;

static void test_instruction(uint32 times_to_test, const TestInfo* info, DriverMode mode);

#if defined(__linux__)
#include <sys/types.h>
#include <sys/mman.h>
#endif /* __linux__ */

#define SLOP (sizeof(uint16) + sizeof(uint32)) /* for ccr and return_addr */

#if defined(__MINGW32__)
static uint8* reference_memory;

void reserve_reference_test_memory(void)
{
    reference_memory = VirtualAlloc((void*)0x7F000000, MEMORY_OFFSET + MEM_SIZE + 512 + SLOP, MEM_RESERVE | MEM_COMMIT,
                                    PAGE_READWRITE);
    if (!reference_memory) {
        fprintf(stderr, "Unable to reserve CPU test memory; error %lu.\n", (unsigned long)GetLastError());
        exit(EXIT_FAILURE);
    }
}
#endif

void test_all_instructions(uint32 try_count)
{
    const TestInfo* info;

    /* Try to provide a consistent address across multiple platforms so that
   * crc's of address registers containing ptrs to this space turn out
   * the same.  To this end we malloc a large block of memory and hope
   * the pointer we want is in the middle.  If this doesn't work, you'll
   * have to hand-massage this.
   */
    if (mem == NULL) {
        size_t page_size;

#define M_SIZE (40 * 1024 * 1024)
#define CODE_SIZE 512

#if defined(__MINGW32__)
/* Match the Linux reference address without depending on malloc's layout. */
#define DESIRED_PTR ((uint8*)0x7F000000 + MEMORY_OFFSET)
        uint8* m = reference_memory;
        if (m == NULL) {
            fprintf(stderr, "Unable to allocate CPU test memory; error %lu.\n", (unsigned long)GetLastError());
            exit(EXIT_FAILURE);
        }
#elif !defined(__linux__)
#define DESIRED_PTR ((uint8*)0x26C0000 + MEMORY_OFFSET)
        uint8* m = (uint8*)malloc(M_SIZE + CODE_SIZE + MEMORY_OFFSET);
#else /* defined (__linux__) */
#define DESIRED_PTR ((uint8*)0x7F000000 + MEMORY_OFFSET)
        uint8* m = mmap(DESIRED_PTR, (M_SIZE + SLOP + 4095) & ~4095, PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_ANONYMOUS | MAP_FIXED | MAP_PRIVATE, -1, 0);
#endif /* defined (__linux__) */

#if !defined(__MINGW32__)
        page_size = getpagesize();
#else
        page_size = 4096;
#endif

        if ((DESIRED_PTR < m || DESIRED_PTR >= m + M_SIZE - MEM_SIZE - CODE_SIZE) && generate_crc) {
            /* Print out address here in case assertion fails. */
            printf("driver.c: Bad m == 0x%lX, absurd hack didn't work (page_size = %ld, DESIRED_PTR = 0x%lX).\n",
                   (unsigned long)m, (long)page_size, (unsigned long)DESIRED_PTR);
            abort();
        }

        if (generate_crc)
            mem = DESIRED_PTR;
        else
            mem = m;
        return_address_addr = (uint32*)(mem + MEM_SIZE); /* must be a 32 bit address */
        ccr_addr = (uint16*)((char*)return_address_addr + sizeof(*return_address_addr));
    }
    if (generate_crc)
        printf("mem == 0x%lX\n", (unsigned long)DESIRED_PTR);

    for (info = test_info; info->name; info++) {
        uint32 times_to_try = try_count;
        if (info->max_times_to_call != 0 && info->max_times_to_call > times_to_try)
            times_to_try = info->max_times_to_call;
        test_instruction(times_to_try, info, DM_USE_EMULATOR);
    }
}

static uint8 saved_mem[MEM_SIZE];

static void test_instruction(uint32 times_to_test, const TestInfo* info, DriverMode mode)
{
    uint32 emulator_return_addr;
    /* We'll use arrays of uint8 to guarantee big-endianness. */
    const uint8 header[] = {
        0x44,
        0xF9, /* movew abs.l,ccr */
        US_TO_SYN68K(ccr_addr) >> 24,
        US_TO_SYN68K(ccr_addr) >> 16,
        US_TO_SYN68K(ccr_addr) >> 8,
        US_TO_SYN68K(ccr_addr),
    };
    uint8 trailer[] = {

        /* These guys smash the ccr so that we can test the non-cc bit setting
     * versions of different instructions.  To test the cc-bit setting versions
     * of instructions, we clobber these with NOPs below.
     */
        0x44,
        0xF9,
        US_TO_SYN68K(ccr_addr) >> 24,
        US_TO_SYN68K(ccr_addr) >> 16,
        US_TO_SYN68K(ccr_addr) >> 8,
        US_TO_SYN68K(ccr_addr),

        /* Here's the main code. */
        0x42,
        0xF9, /* movew ccr,abs.l */
        US_TO_SYN68K(ccr_addr) >> 24,
        US_TO_SYN68K(ccr_addr) >> 16,
        US_TO_SYN68K(ccr_addr) >> 8,
        US_TO_SYN68K(ccr_addr),
        0x4E,
        0xF0, /* jmp memory indirect, with index suppressed */
        0x01,
        0xF1, /* BS + IS + long BD + null OD; IS=1/IIS=101 is reserved. */
        US_TO_SYN68K(return_address_addr) >> 24,
        US_TO_SYN68K(return_address_addr) >> 16,
        US_TO_SYN68K(return_address_addr) >> 8,
        US_TO_SYN68K(return_address_addr),
    };
    uint16* m68k_code = (uint16*)(mem + MEM_SIZE);
    uint16* code_dest;
    uint32 entry_point, tc;
    uint16 cc;
    uint16 pre_crc = 0, mem_crc = 0, reg_crc = 0, cc_crc = 0;

    /* NOTE: cc_crc is the old reg_crc, where we include condition codes.
           reg_crc now has regs only.  I think the way we handle the "seed"
	   in computer_crc makes having a "crc" of just the condition codes
	   not work w/o some fiddling.  */

    printf("Testing %s...", info->name), fflush(stdout);

    /* Copy in trailer. */
    if (!test_only_non_cc_variants) /* NOP out cc clobber if we want cc's. */
    {
        static const unsigned char nop_nop_nop[] = {0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71};
        memcpy(trailer, nop_nop_nop, sizeof nop_nop_nop);
    }
    memcpy(((void*)&m68k_code[128]) - sizeof trailer, trailer, sizeof trailer);

    /* Compute where the 68k code should go. */
    code_dest = ((uint16*)(((void*)&m68k_code[128]) - sizeof trailer) - info->code_words);

    /* Copy in header. */
    memcpy(((void*)code_dest) - sizeof header, header, sizeof header);

    entry_point = US_TO_SYN68K(((void*)code_dest) - sizeof header);

    if (mode != DM_USE_EMULATOR) {
        fprintf(stderr, "Cannot test on real CPU if it's not a 68000!\n");
        exit(-1);
    }

    emulator_return_addr = SWAPUL_IFLE(MAGIC_EXIT_EMULATOR_ADDRESS);

    for (tc = 0; tc < times_to_test; tc++) {
        M68kReg saved_regs[16];

        times_called = tc;

        if (((times_called + 1) % 2000) == 0)
            printf("%lu...", times_called + 1), fflush(stdout);

        fully_randomize_regs();

        if (mode != DM_USE_68000) {
            /* Destroy any previously compiled code we may have been testing. */
            destroy_blocks(US_TO_SYN68K(m68k_code), CODE_SIZE);
        }

        if (info->might_change_memory)
            randomize_mem();

        /* Create the 68k code. */
        info->code_creator(code_dest);

        /* Byte swap it if appropriate. */
#ifdef LITTLEENDIAN
        {
            int i;
            for (i = info->code_words - 1; i >= 0; i--)
                code_dest[i] = SWAPUW(code_dest[i]);
        }
#endif

        memcpy(saved_regs, cpu_state.regs, sizeof saved_regs);
        if (info->might_change_memory)
            memcpy(saved_mem, mem, sizeof saved_mem);

        /* Compute pre_crc if appropriate.  pre_crc refers to the checksum
       * before the code is emulated.
       */
        if (mode != DM_COMPARE_AS_YOU_GO) {
            unsigned char buf[16 * 4]; /* Regs */
            unsigned char* p;
            int i;

            pre_crc = compute_crc(saved_mem, MEM_SIZE, pre_crc);

            /* Guarantee byte-order independent crc */
            for (p = buf, i = 0; i < 16; i++) {
                *p++ = saved_regs[i].ul.n >> 24;
                *p++ = saved_regs[i].ul.n >> 16;
                *p++ = saved_regs[i].ul.n >> 8;
                *p++ = saved_regs[i].ul.n;
            }
            pre_crc = compute_crc(buf, p - buf, pre_crc);
        }

        /* Run it. */
        for (cc = 0; cc <= ALL_CCS; cc++) {
            uint16 emulated_cc = 0;
            M68kReg emulated_regs[16];
            static uint8 emulated_mem[MEM_SIZE];
            int mask = info->cc_mask;

            if (mode != DM_USE_68000) {
                *ccr_addr = SWAPUW_IFLE(cc);
                *return_address_addr = emulator_return_addr;
                memcpy(cpu_state.regs, saved_regs, sizeof saved_regs);
                if (info->might_change_memory)
                    memcpy(mem, saved_mem, sizeof saved_mem);
                interpret_code(hash_lookup_code_and_create_if_needed(entry_point));
                memcpy(emulated_regs, cpu_state.regs, sizeof emulated_regs);
                if (info->might_change_memory)
                    memcpy(emulated_mem, mem, sizeof emulated_mem);
                emulated_cc = SWAPUW_IFLE(*ccr_addr);
            }

            /* Compute CRC if appropriate. */
            if (mode != DM_COMPARE_AS_YOU_GO) {
                unsigned char buf[16 * 4 + 1]; /* Regs + cc bits. */
                unsigned char* p;
                int i;

                if (info->might_change_memory)
                    mem_crc = compute_crc(mem, MEM_SIZE, mem_crc);

                /* Guarantee byte-order independent crc */
                for (p = buf, i = 0; i < 16; i++) {
                    *p++ = cpu_state.regs[i].ul.n >> 24;
                    *p++ = cpu_state.regs[i].ul.n >> 16;
                    *p++ = cpu_state.regs[i].ul.n >> 8;
                    *p++ = cpu_state.regs[i].ul.n;
                }
                *p++ = emulated_cc & mask;
                reg_crc = compute_crc(buf, p - buf - 1, reg_crc);
                cc_crc = compute_crc(buf, p - buf, cc_crc);
            }
        }
    }

    if (mode != DM_COMPARE_AS_YOU_GO)
        printf("done.\tpre_crc:0x%04X, mem_crc:0x%04X, reg_crc:0x%04X, cc_crc:0x%04X\n", (unsigned)pre_crc,
               (unsigned)mem_crc, (unsigned)reg_crc, (unsigned)cc_crc);
    else
        puts("done");
}
