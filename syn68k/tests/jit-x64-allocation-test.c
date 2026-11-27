/*+===================================================================
  File:      jit-x64-allocation-test.c

  Summary:   Implements jit x64 allocation test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: syn68k_mmu040_access_ssw, syn68k_native040_flat_state, syn68k_native040_memory, syn68k_memory_span, trap_exception, syn68k_invalidate_checked_write;
             main, syn68k_native040_entry, syn68k_fpu_step_decoded, syn68k_fpu_finish_command,
             syn68k_integer_step_decoded, syn68k_fpu_finish_captured, syn68k_fpu_finish_command_at,
             syn68k_fpu_finish_frame, syn68k_fpu_finish_destination,
             syn68k_x64_complex_opcode, syn68k_x64_complex_run, syn68k_x64_fast_memory_opcode,
             syn68k_x64_flag_liveness, syn68k_x64_indexed_address, syn68k_x64_memory_opcode,
             syn68k_x64_native_memory_words, syn68k_x64_prepare_pair, test_alloc, test_flush, test_free, test_process,
             test_protect, trap_direct

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Compile the emitter with intercepted Windows page-management calls. */
#include "syn68k_private.h"
#include <assert.h>
#include <stdlib.h>
#define __declspec(x)
#define VirtualAlloc test_alloc
#define VirtualProtect test_protect
#define VirtualFree test_free
#define GetCurrentProcess test_process
#define FlushInstructionCache test_flush
#include "../src/runtime/jit-x64.c"

Syn68kNative040Entry syn68k_native040_entry(enum Syn68kNative040Operation operation)
{
    (void)operation;
    return NULL; // This fixture tests register/page publication without a native arithmetic image.
}

const Syn68kNative040FlatState* syn68k_native040_flat_state(void)
{
    return NULL;
}

int syn68k_native040_memory(Syn68kMMU040Memory* memory, Syn68kMMU040Native* storage, Syn68kNative040RAM ram)
{
    return 0;
}

int syn68k_memory_span(syn68k_addr_t address, uint32 length)
{
    return 0; // Publication fixtures never execute the memory body.
}

int syn68k_mmu040_access_ssw(const Syn68kMMU040Access* access, uint16_t* ssw)
{
    return 0; // Publication fixtures never execute store-fault continuations.
}

int syn68k_mmu040_cache_push_ssw(const Syn68kMMU040Access* access, uint16_t* ssw)
{
    abort(); // Publication fixtures never execute store-fault continuations.
}

int syn68k_mmu040_operand_status(const Syn68kMMU040Access* access, uint16_t* ssw)
{
    abort();
}

unsigned long destroy_blocks(syn68k_addr_t address, uint32 size)
{
    (void)address;
    (void)size;
    assert(!"allocation fixture must not execute guest cache control");
    return 0;
}

void syn68k_invalidate_checked_write(uint32 address, unsigned size)
{
    abort();
}

uint32 trap_exception(uint32 number, uint32 pc, uint32 address, unsigned format)
{
    abort();
}

CPUState cpu_state;

void syn68k_x64_flag_liveness(const uint16* source, const int8* lengths, unsigned instructions, const uint8* faults,
                              uint8* masks)
{
    memset(masks, ALL_CCS, instructions);
}

uint64 ROMlib_offset;
uint32 syn68k_address_mask, syn68k_host_alias_base, syn68k_host_alias_size;
uint32 syn68k_direct_memory_size;
unsigned syn68k_host_alias_count;
uint32 syn68k_x64_code_pages[SYN68K_X64_PAGE_COUNT];

int syn68k_x64_complex_opcode(unsigned opcode)
{
    return 0;
}

uint32 syn68k_x64_complex_run(uint32 pc, unsigned opcode)
{
    abort();
}

int syn68k_x64_fast_memory_opcode(unsigned opcode, uint32 pc)
{
    return 0;
}

int syn68k_x64_memory_opcode(unsigned opcode)
{
    return 0; /* This fixture tests the emitter's register/page paths alone. */
}

unsigned syn68k_x64_native_memory_words(unsigned opcode)
{
    return 0;
}

int syn68k_x64_indexed_address(unsigned code, uint32 extension_pc, uint32 result[2])
{
    abort(); /* This fixture never executes generated code. */
}

int syn68k_x64_prepare_pair(unsigned opcode, uint32 pc, Syn68kX64Pair* result)
{
    abort();
}

uint32 trap_direct(uint32 number, uint32 pc, uint32 address)
{
    abort(); /* Page-management fixtures never execute generated code. */
}

int syn68k_fpu_step_decoded(uint32_t pc, unsigned opcode, Syn68kMMU040Memory* memory, Syn68kFPUStep* result)
{
    abort();
}

int syn68k_fpu_finish_command(uint32_t pc, unsigned opcode, unsigned command, Syn68kMMU040Memory* memory,
                              Syn68kFPUStep* result)
{
    abort();
}

int syn68k_fpu_finish_command_at(uint32_t pc, unsigned opcode, unsigned command, Syn68kMMU040Memory* memory,
                                 const Syn68kFPUEntry* entry, Syn68kFPUStep* result)
{
    abort();
}

int syn68k_fpu_finish_captured(const Syn68kFPUCaptured* captured, Syn68kFPUStep* result)
{
    abort();
}

int syn68k_fpu_finish_frame(const Syn68kFPUFrame* captured, Syn68kMMU040Memory* memory, Syn68kFPUStep* result)
{
    (void)captured;
    (void)memory;
    (void)result;
    return 0;
}

int syn68k_fpu_finish_destination(const Syn68kFPUCaptured* captured, Syn68kMMU040Memory* memory, Syn68kFPUStep* result)
{
    abort();
}

int syn68k_integer_step_decoded(uint32_t pc, unsigned opcode, Syn68kMMU040Memory* memory, Syn68kIntegerStep* result)
{
    abort();
}

static unsigned failure, step, freed;
static void* allocation;

void* __stdcall test_alloc(void* address, size_t size, unsigned long kind, unsigned long protection)
{
    assert(!address && size > sizeof(Syn68kX64CodeHeader) && size <= 8192 + sizeof(Syn68kX64CodeHeader) &&
           kind == 0x3000 && protection == 0x04);
    assert(++step == 1);
    return failure == 1 ? NULL : (allocation = malloc(size));
}

int __stdcall test_protect(void* address, size_t size, unsigned long protection, unsigned long* old)
{
    assert(address == allocation && size > sizeof(Syn68kX64CodeHeader) && size <= 8192 + sizeof(Syn68kX64CodeHeader) &&
           protection == 0x20);
    const Syn68kX64CodeHeader* header = address;
    assert(header->managed <= 1 && !(header->reserved[0] | header->reserved[1] | header->reserved[2]));
    assert(++step == 2);
    assert(((unsigned char*)address)[size - 1] == 0xC3);
    *old = 0x04;
    return failure != 2;
}

void* __stdcall test_process(void)
{
    return &step;
}

int __stdcall test_flush(void* process, const void* address, size_t size)
{
    assert(process == &step && address == (unsigned char*)allocation + sizeof(Syn68kX64CodeHeader) && size);
    assert(++step == 3);
    return failure != 3;
}

int __stdcall test_free(void* address, size_t size, unsigned long kind)
{
    assert(address == allocation && size == 0 && kind == 0x8000);
    ++freed;
    free(address);
    return 1;
}

int main(void)
{
    uint16 source[70];
    int8 lengths[70];
    for (unsigned i = 0; i < 70; ++i) {
        source[i] = SWAPUW_IFLE(0x4E71);
        lengths[i] = 1;
    }
    for (failure = 0; failure < 4; ++failure) {
        step = freed = 0;
        unsigned count = 99;
        int terminal = 99;
        void* code = syn68k_x64_compile(source, lengths, 70, &count, 0x1000, &terminal);
        assert(!terminal);
        assert(count == (failure ? 0 : 64));
        assert(!!code == !failure);
        syn68k_x64_release(code);
        assert(step == (failure ? failure : 3));
        assert(freed == (failure != 1));
    }
    step = 0;
    unsigned count;
    int terminal;
    failure = step = freed = 0;
    void* single = syn68k_x64_compile(source, lengths, 1, &count, 0x1000, &terminal);
    assert(single && count == 1 && terminal);
    syn68k_x64_release(single);
    assert(step == 3 && freed == 1);
    step = freed = 0;
    source[0] = SWAPUW_IFLE(0x4E75);
    void* early = syn68k_x64_compile(source, lengths, 70, &count, 0x1000, &terminal);
    assert(early && count == 1 && terminal);
    syn68k_x64_release(early);
    assert(step == 3 && freed == 1);
    step = freed = 0;
    /* Nearby opcode families must not be mistaken for register ALU forms. */
    static const unsigned unsupported[] = {
        0xD110, /* ADD.B D0,(A0) */
        0x9108, /* SUBX.B -(A0),-(A0) */
        0xD0D0, /* ADDA.W (A0),A0 */
        0xB108, /* CMPM.B (A0)+,(A0)+ */
        0xC010, /* AND.B (A0),D0 */
        0xD108, /* ADDX.B -(A0),-(A0) */
        0x4800, /* NBCD D0 */
        0x027C, /* ANDI to SR */
    };
    for (unsigned i = 0; i < sizeof unsupported / sizeof unsupported[0]; ++i) {
        source[0] = SWAPUW_IFLE(unsupported[i]);
        lengths[0] = i == 7 ? 2 : 1;
        emitter linear = {.cached_address_register = -1, .cc_mask = ALL_CCS};
        assert(!instruction(&linear, source, lengths[0], 0x1000));
    }
    assert(!step);
    /* Dynamic FPU templates reserve room for every live command class. A
       growing scalar transfer body must not overrun an existing prefix. */
    for (unsigned ea = 0; ea < 64; ++ea) {
        emitter fpu = {.cached_address_register = -1, .cc_mask = ALL_CCS};
        assert(fpu_register(&fpu, 0xf200 | ea, 0x1000) == 1 && fpu.size < 4096);
        fpu = (emitter){.size = 4096, .cached_address_register = -1, .cc_mask = ALL_CCS};
        assert(fpu_register(&fpu, 0xf200 | ea, 0x1000) == 1 && fpu.size < sizeof fpu.bytes);
        fpu.size = 4097;
        assert(fpu_register(&fpu, 0xf200 | ea, 0x1000) == -1 && fpu.size == 4097);
        fpu = (emitter){.cached_address_register = -1, .cc_mask = ALL_CCS};
        assert(fpu_register_mode(&fpu, 0xf200 | ea, 0x1000, 1) == 1 && fpu.managed == 1 && fpu.size < 8192);
        fpu = (emitter){.size = 1, .cached_address_register = -1, .cc_mask = ALL_CCS};
        assert(fpu_register_mode(&fpu, 0xf200 | ea, 0x1000, 1) == -1 && fpu.size == 1);
    }
    assert(!step);
    for (unsigned opcode = 0xf240; opcode <= 0xf2ff; ++opcode) {
        emitter predicate = {.cached_address_register = -1, .cc_mask = ALL_CCS};
        assert(fpu_mapped_conditional(&predicate, opcode, 0x1000) == 1 && predicate.managed == 1 &&
               predicate.size < 3072);
        predicate = (emitter){.size = 5120, .cached_address_register = -1, .cc_mask = ALL_CCS};
        assert(fpu_mapped_conditional(&predicate, opcode, 0x1000) == 1 && predicate.size < sizeof predicate.bytes);
        predicate.size = 5121;
        assert(fpu_mapped_conditional(&predicate, opcode, 0x1000) == -1 && predicate.size == 5121);
    }
    for (unsigned opcode = 0; opcode <= 65535; ++opcode)
        if (mapped_mmu_opcode(opcode)) {
            emitter mmu = {.cached_address_register = -1, .cc_mask = ALL_CCS};
            assert(mapped_mmu(&mmu, opcode, 0x1000) == 1 && mmu.managed == 1 && mmu.size < 1024);
            mmu = (emitter){.size = 7169, .cached_address_register = -1, .cc_mask = ALL_CCS};
            assert(mapped_mmu(&mmu, opcode, 0x1000) == -1 && mmu.size == 7169);
        }
    /* Every host address has an explicit relocation, independent of ASLR.
       Scalar rollback must discard records along with rejected bytes. */
    emitter relocated = {0};
    for (unsigned symbol = 0; symbol < RELOC_COUNT; ++symbol) {
        pointer_r8(&relocated, symbol);
        relocation entry = relocated.relocations[symbol];
        uintptr_t value;
        memcpy(&value, relocated.bytes + entry.offset, sizeof value);
        assert(entry.offset == symbol * 10 + 2 && entry.symbol == symbol);
        assert(value == relocation_address(symbol) && value);
    }
    assert(relocated.relocation_count == RELOC_COUNT);
    source[0] = SWAPUW_IFLE(0x6000); /* Live word displacement: helper tail call. */
    lengths[0] = 1;
    for (failure = 0; failure < 4; ++failure) {
        step = freed = 0;
        void* code = syn68k_x64_compile(source, lengths, 1, &count, 0x1000, &terminal);
        assert(count == !failure && terminal == !failure);
        assert(!!code == !failure);
        syn68k_x64_release(code);
        assert(step == (failure ? failure : 3));
        assert(freed == (failure != 1));
    }
    for (unsigned opcode = 0xf300; opcode < 0xf380; ++opcode) {
        emitter frame = {.cached_address_register = -1, .cc_mask = ALL_CCS};
        assert(fpu_mapped_frame(&frame, opcode, 0x1000) == 1 && frame.managed && frame.size <= FPU_FRAME_EMIT_RESERVE);
        frame = (emitter){.size = sizeof frame.bytes - FPU_FRAME_EMIT_RESERVE, .cached_address_register = -1, .cc_mask = ALL_CCS};
        assert(fpu_mapped_frame(&frame, opcode, 0x1000) == 1 && frame.size < sizeof frame.bytes);
        frame = (emitter){.size = sizeof frame.bytes - FPU_FRAME_EMIT_RESERVE + 1, .cached_address_register = -1, .cc_mask = ALL_CCS};
        assert(fpu_mapped_frame(&frame, opcode, 0x1000) == -1 && frame.size == sizeof frame.bytes - FPU_FRAME_EMIT_RESERVE + 1);
    }
    const unsigned mapped_opcodes[] = {0xf200, 0xf218, 0xf250, 0xf27b, 0xf281, 0xf310, 0xf358};
    for (unsigned i = 0; i < sizeof mapped_opcodes / sizeof *mapped_opcodes; ++i)
        for (failure = 0; failure < 4; ++failure) {
            step = freed = 0;
            void* mapped_code = compile_mapped(0x1000, mapped_opcodes[i]);
            assert(!!mapped_code == !failure);
            if (mapped_code) {
                const Syn68kX64CodeHeader* header = (const void*)((unsigned char*)mapped_code - sizeof *header);
                assert(header->managed == 1);
            }
            syn68k_x64_release(mapped_code);
            assert(step == (failure ? failure : 3) && freed == (failure != 1));
        }
    for (failure = 0; failure < 4; ++failure) {
        step = freed = 0;
        void* mapped_code = compile_mapped(0x1000, 0x4e7a);
        assert(!!mapped_code == !failure);
        syn68k_x64_release(mapped_code);
        assert(step == (failure ? failure : 3) && freed == (failure != 1));
    }
    /* The largest shift template must stop before filling its emitter buffer. */
    for (unsigned i = 0; i < 70; ++i)
        source[i] = SWAPUW_IFLE(0xE370); /* ROXL.W D1,D0 */
    failure = step = freed = 0;
    void* code = syn68k_x64_compile(source, lengths, 70, &count, 0x1000, &terminal);
    assert(code && count > 0 && count < 64 && !terminal);
    syn68k_x64_release(code);
    assert(step == 3 && freed == 1);
    return 0;
}
