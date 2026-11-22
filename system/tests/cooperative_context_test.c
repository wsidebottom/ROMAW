/*+===================================================================
  File:      cooperative_context_test.c

  Summary:   Exercise the production context exchanger against bounded guest memory. Host interpreter fields are
             deliberately present to catch accidental whole-CPU copies. Actual 68K trap execution is tested separately.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Exercise the production context exchanger against bounded guest memory.
   Host interpreter fields are deliberately present to catch accidental
   whole-CPU copies. Actual 68K trap execution is tested separately. */
#define _COMMON_H_
#define _romlib_float_h_
#define _BLOCKINTERRUPTS_H_
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint32_t syn68k_addr_t;

typedef struct {
    uint8_t bytes[256];
} Syn68kFPUState;

static struct {
    union {
        struct {
            uint32 n;
        } ul;
    } regs[16];

    unsigned sr, ccx, ccn, ccnz, ccv, ccc;
    uint32 usp, msp, isp;
    Syn68kFPUState fpu;
    uint32 jsr_stack[32];
    unsigned jsr_stack_byte_index;
    void* setjmp_buf;
    unsigned interrupt_pending;
} cpu_state;

static uint8 memory[8192];
uint32 ROMlib_cooperative_stack_low, ROMlib_cooperative_stack_high;
void* ROMlib_cooperative_interpreter;
int ROMlib_cooperative_depth;
unsigned ROMlib_cooperative_critical;
static unsigned interrupts_blocked, interrupt_rechecks;

static unsigned block_virtual_ints(void)
{
    unsigned old = interrupts_blocked | ((cpu_state.sr >> 7) & 0xe);
    interrupts_blocked = 1;
    cpu_state.sr |= 0x700;
    return old;
}

static void restore_virtual_ints(unsigned old)
{
    interrupts_blocked = old & 1;
    cpu_state.sr = (cpu_state.sr & ~0x700u) | ((old & 0xe) << 7);
    ++interrupt_rechecks;
}

int emulation_depth = 1;
#define EM_A7 cpu_state.regs[15].ul.n
#define EM_D0 cpu_state.regs[0].ul.n
#define SYN68K_TO_US(a) (memory + (a))

static int syn68k_memory_span(uint32 a, uint32 n)
{
    return a <= sizeof memory && n <= sizeof memory - a;
}

static uint32 read_long(uint32 a)
{
    assert(syn68k_memory_span(a, 4));
    return (uint32)memory[a] << 24 | (uint32)memory[a + 1] << 16 | (uint32)memory[a + 2] << 8 | memory[a + 3];
}

static void write_long(uint32 a, uint32 n)
{
    assert(syn68k_memory_span(a, 4));
    for (unsigned i = 0; i < 4; ++i)
        memory[a + i] = (uint8)(n >> (24 - 8 * i));
}

#define READUL(a) read_long(a)
#define WRITEUL(a, n) write_long((a), (n))
#include "../../runtime/cooperative.c"

static uint32 exchange(uint32 save, uint32 load)
{
    write_long(EM_A7 + 4, save);
    write_long(EM_A7 + 8, load);
    return ROMlib_exchange_guest_context(4000, 0x201b);
}

int main(void)
{
    for (unsigned i = 0; i < 16; ++i)
        cpu_state.regs[i].ul.n = 100 + i;
    EM_A7 = 7000;
    cpu_state.usp = 6000;
    cpu_state.msp = 6100;
    cpu_state.isp = 6200;
    memset(&cpu_state.fpu, 0x6a, sizeof cpu_state.fpu);
    cpu_state.setjmp_buf = memory + 4096;
    cpu_state.interrupt_pending = 0x51;
    assert(exchange(1536, ROMAW_CONTEXT_TEMPLATE) == 4002 && EM_D0 == 0);
    assert(!ROMlib_cooperative_interpreter && read_long(1536 + context_pc) == 0);
    assert(exchange(512, 0) == 4002 && EM_D0 == 0);
    assert(read_long(512) == ROMAW_CONTEXT_MAGIC);
    assert(read_long(512 + 76) == 7000);
    assert(read_long(512 + 8) == 4002);
    assert(memory[511] == 0 && memory[1024] == 0);

    memcpy(memory + 1024, memory + 512, ROMAW_CONTEXT_SIZE);
    write_long(1024 + 8, 4200);
    write_long(1024 + 76, 6500);
    write_long(1024 + 16 + 4, 0xabcddcba);
    write_long(1024 + 12, 0x2004);
    memory[1024 + context_fpu] = 0x19;
    write_long(1024 + context_stack_low, 6000);
    write_long(1024 + context_stack_high, 6800);
    memset(cpu_state.jsr_stack, 0x11, sizeof cpu_state.jsr_stack);
    cpu_state.jsr_stack_byte_index = 8;
    assert(exchange(512, 1024) == 4200);
    assert(ROMlib_cooperative_stack_low == 6000 && ROMlib_cooperative_stack_high == 6800);
    assert(EM_A7 == 6500 && cpu_state.regs[1].ul.n == 0xabcddcba);
    assert(cpu_state.ccnz == 0 && !cpu_state.ccx && !cpu_state.ccc);
    assert(cpu_state.fpu.bytes[0] == 0x19);
    assert(cpu_state.jsr_stack_byte_index == 0);
    assert(cpu_state.setjmp_buf == memory + 4096);
    assert(cpu_state.interrupt_pending == 0x51);
    assert(exchange(1024, 512) == 4002);
    assert(!ROMlib_cooperative_stack_low && !ROMlib_cooperative_stack_high);
    assert(EM_A7 == 7000 && cpu_state.regs[1].ul.n == 101);
    assert(cpu_state.fpu.bytes[0] == 0x6a);
    assert(cpu_state.ccnz && cpu_state.ccx && cpu_state.ccc);

    cpu_state.setjmp_buf = memory + 4098;
    uint8 before[512];
    memcpy(before, memory + 512, sizeof before);
    assert(exchange(512, 1024) == 4002 && EM_D0 == 1);
    assert(!memcmp(before, memory + 512, sizeof before));
    assert(EM_A7 == 7000);
    cpu_state.setjmp_buf = memory + 4096;

    /* Switch-dispatch interpreters keep setjmp_buf null even when nested. */
    ROMlib_cooperative_interpreter = cpu_state.setjmp_buf = NULL;
    emulation_depth = 2;
    assert(exchange(512, 1024) == 4002 && EM_D0 == 1);
    assert(!memcmp(before, memory + 512, sizeof before));
    emulation_depth = 1;
    assert(exchange(512, 512) == 4002 && EM_D0 == 0);
    ROMlib_cooperative_interpreter = cpu_state.setjmp_buf = memory + 4096;

    /* Alternate independent stacks and counters repeatedly. */
    for (unsigned i = 0; i < 1000; ++i) {
        cpu_state.regs[2].ul.n = i;
        assert(exchange(512, 1024) == 4002);
        cpu_state.regs[3].ul.n = 1000 - i;
        assert(exchange(1024, 512) == 4002);
        assert(cpu_state.regs[2].ul.n == i);
    }

    const uint32 invalid[] = {1, 8191, 8192, 0xfffffffe, 2048, 1536};
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        uint8 previous[512];
        memcpy(previous, memory + 512, sizeof previous);
        assert(exchange(512, invalid[i]) == 4002);
        assert((int32_t)EM_D0 == -50);
        assert(!memcmp(previous, memory + 512, sizeof previous));
    }
    write_long(1024 + 8, 8192);
    assert(exchange(512, 1024) == 4002 && (int32_t)EM_D0 == -50);
    write_long(1024 + 8, 4200);
    write_long(1024 + 76, 6501);
    assert(exchange(512, 1024) == 4002 && (int32_t)EM_D0 == -50);
    assert(exchange(512, 514) == 4002 && (int32_t)EM_D0 == -50);
    assert(exchange(512, 512) == 4002 && EM_D0 == 0);
    /* Guest-global updates are protected until the architecture is committed.
     Deferral leaves the guard held so the caller can roll its globals back. */
    write_long(1024 + 76, 6500);
    cpu_state.sr = 0x2000;
    assert(exchange(0, ROMAW_CONTEXT_BEGIN_SWITCH) == 4002 && !EM_D0);
    assert(ROMlib_cooperative_critical && interrupts_blocked);
    assert((cpu_state.sr & 0x700) == 0x700);
    assert(exchange(0, ROMAW_CONTEXT_BEGIN_SWITCH) == 4002);
    assert((int32_t)EM_D0 == -50 && interrupts_blocked);
    emulation_depth = 2;
    assert(exchange(512, 1024) == 4002 && EM_D0 == 1);
    assert(interrupts_blocked && ROMlib_cooperative_critical);
    emulation_depth = 1;
    assert(exchange(0, ROMAW_CONTEXT_END_SWITCH) == 4002 && !EM_D0);
    assert(!interrupts_blocked && !ROMlib_cooperative_critical);
    assert(!(cpu_state.sr & 0x700) && interrupt_rechecks);
    assert(exchange(0, ROMAW_CONTEXT_END_SWITCH) == 4002);
    assert((int32_t)EM_D0 == -50);
    write_long(1024 + 76, 6500);
    write_long(1024 + context_sr, 0x2304);
    assert(exchange(0, ROMAW_CONTEXT_BEGIN_SWITCH) == 4002 && !EM_D0);
    assert(exchange(512, 1024) == 4200);
    assert(!interrupts_blocked && !ROMlib_cooperative_critical);
    assert((cpu_state.sr & 0x700) == 0x300);
    assert(!(read_long(512 + context_sr) & 0x700));
    assert(exchange(1024, 512) == 4002);
    emulation_depth = 2;
    assert(exchange(0, ROMAW_CONTEXT_BEGIN_SWITCH) == 4002 && EM_D0 == 1);
    assert(!interrupts_blocked && !ROMlib_cooperative_critical);
    emulation_depth = 1;
    EM_A7 = sizeof memory - 4;
    assert(ROMlib_exchange_guest_context(4000, 0x2000) == 4002);
    assert((int32_t)EM_D0 == -50);
    puts("cooperative guest contexts passed");
}
