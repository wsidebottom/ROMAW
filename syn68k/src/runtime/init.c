/*+===================================================================
  File:      init.c

  Summary:   Implements init support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: initialize_68k_emulator, syn68k_native_code_enabled, syn68k_set_addressing_mode,
             syn68k_threaded_dispatch_enabled

  Origin:    Existing repository source in syn68k/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#include "config.h"

#include "syn68k_private.h"
#include "hash.h"
#include "rangetree.h"
#include "callback.h"
#include "trap.h"
#include "alloc.h"
#include "mapping.h"
#include "translate.h"
#include "checksum.h"
#include "deathqueue.h"
#include "interrupt.h"
#include "destroyblock.h"
#include "jit-x64.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Global CPU state struct. */
CPUState cpu_state;

#if SIZEOF_CHAR_P == 4
uint32 ROMlib_offset;
#else
uint64 ROMlib_offset;
#endif

int syn68k_threaded_dispatch_enabled(void)
{
#ifdef USE_DIRECT_DISPATCH
    return 1;
#else
    return 0;
#endif
}

int syn68k_native_code_enabled(void)
{
#ifdef GENERATE_X64_CODE
    return syn68k_x64_enabled;
#else
    return 0;
#endif
}

void syn68k_set_addressing_mode(int mode32)
{
    uint32 mask = mode32 ? 0xFFFFFFFFUL : 0x00FFFFFFUL;
    if (mask == syn68k_address_mask)
        return;
    fprintf(stderr, "trace: MMU switch %d -> %d aliases=%u\n", syn68k_address_mask == 0xFFFFFFFFUL ? 32 : 24,
            mode32 ? 32 : 24, syn68k_host_alias_count);
    destroy_blocks(0, ~0UL);
    syn68k_address_mask = mask;
    memset(cpu_state.jsr_stack, -1, sizeof(cpu_state.jsr_stack));
    cpu_state.jsr_stack_byte_index = 0;
}

/* Initialize syn68k exactly once, before any other syn68k calls.
 * The final parameter is reserved for ABI compatibility; pass zero.
 */
void initialize_68k_emulator(void (*while_busy)(int), int native_p, uint32 trap_vector_storage[64],
                             uint32 reserved_interrupt_flag_addr)
{
    static uint16 rte =
#ifdef LITTLEENDIAN
        0x734E;
#else
        0x4E73;
#endif
    Block* b;
    uint16* code;

#ifdef __CHECKER__
    /* Checker can't work with native code. */
    native_p = FALSE;
#endif

    /* Zero out CPU state (not necessary since statics are 0'd) */
    memset(&cpu_state, 0, sizeof cpu_state);
    syn68k_fpu_reset();
    memset(&cpu_state.jsr_stack, -1, sizeof cpu_state.jsr_stack);

    /* Record the function to periodically call while we are busy doing stuff. */
    call_while_busy_func = while_busy;

#ifdef GENERATE_X64_CODE
    syn68k_x64_enabled = native_p != 0;
#endif
    trap_vector_array = trap_vector_storage;

    /* Set up the status register & interrupt stack pointer. */
    cpu_state.sr = 0x2000;      /* supervisor/interrupt mode (this appears
			    * to be what programs really expect, e.g.
			    * PGA Tour Golf does movew #8192,sr).
			    */
    cpu_state.usp = 0xDEADF00D; /* Never use this stack pointer! */
    cpu_state.msp = 0xDEAD0666; /* Never use this stack pointer! */

    SET_INTERRUPT_STATUS(INTERRUPT_STATUS_UNCHANGED);

    /* Call various initialization routines. */
#if defined(USE_LOCAL_DISPATCH)
    interpret_code(NULL);
#endif
    hash_init();
    range_tree_init();
    callback_init();
    trap_init();

    /* Create the magical block that exits the emulator if you ever
   * jump to it.  Don't add it to the death queue, since we never want
   * this block to go away.
   */
    b = make_artificial_block(NULL, MAGIC_EXIT_EMULATOR_ADDRESS, OPCODE_WORDS, &code);

#ifdef USE_DIRECT_DISPATCH
    *(const void**)code = direct_dispatch_table[0];
#else
    *(const void**)code = (const void*)0; /* Exit emulator opc. */
#endif

    hash_insert(b);
    range_tree_insert(b);

    /* Create the magical block that contains an RTE. */
    b = NULL;
    /* Host-pointer mappings keep the immutable RTE reachable on Win64 too. */
    generate_block(NULL, US_TO_SYN68K(&rte), &b);
    assert(b != NULL);
    hash_remove(b);
    range_tree_remove(b);
    death_queue_dequeue(b);
    b->m68k_start_address = MAGIC_RTE_ADDRESS;
    WRITE_LONG(&b->compiled_code[-PTR_WORDS], MAGIC_RTE_ADDRESS);
    b->m68k_code_length = 1;
    b->checksum = compute_block_checksum(b);
    b->immortal = TRUE;
    hash_insert(b);
    range_tree_insert(b);
}
