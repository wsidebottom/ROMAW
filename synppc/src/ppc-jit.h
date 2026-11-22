/*+===================================================================
  File:      ppc-jit.h

  Summary:   Declares ppc jit support for synppc PowerPC execution and fragment loading.

  Classes:   Declared: romaw_ppc_code_range, romaw_ppc_jit_scope, romaw_ppc_jit_service_scope,
                       romaw_ppc_system_operation, romaw_ppc_register_form, romaw_ppc_register_move;
             Used: ROMlib_ppc_environment, powerpc_cpu (forward declaration)

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in synppc/src; original attribution and provenance comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ROMAW_PPC_JIT_H
#define ROMAW_PPC_JIT_H

#include "synppc/ppc.h"
#include "ppc-native-memory-code.h"
#include <memory>

class romaw_ppc_jit_scope {
    void *context, *previous;

  public:
    explicit romaw_ppc_jit_scope(ROMlib_ppc_environment* environment) noexcept;
    ~romaw_ppc_jit_scope();
    romaw_ppc_jit_scope(const romaw_ppc_jit_scope&) = delete;
    romaw_ppc_jit_scope& operator=(const romaw_ppc_jit_scope&) = delete;
};

class romaw_ppc_jit_service_scope {
    bool* in_service;
    bool previous;

  public:
    romaw_ppc_jit_service_scope() noexcept;
    ~romaw_ppc_jit_service_scope();
    romaw_ppc_jit_service_scope(const romaw_ppc_jit_service_scope&) = delete;
    romaw_ppc_jit_service_scope& operator=(const romaw_ppc_jit_service_scope&) = delete;
};

class powerpc_cpu;
std::shared_ptr<ppc_native_memory_kernel> romaw_ppc_native_memory_code();
bool romaw_ppc_native_system_step(powerpc_cpu*, uint32_t*, uint32_t);
void romaw_ppc_system_kernel(powerpc_cpu*, uint32_t);
int romaw_ppc_set_fpscr_rounding(uint32_t fpscr);
void romaw_ppc_data_breakpoint(uint32_t address, unsigned size, bool write);
enum class romaw_ppc_system_operation { other, invalid, trap, system_call, rfi, external };
romaw_ppc_system_operation romaw_ppc_decode_system_operation(uint32_t opcode);
bool romaw_ppc_750_excludes_instruction(uint32_t opcode, const char* mnemonic);
// Primary opcode 31. A zero mask identifies an opcode outside the MMU controls.
uint32_t romaw_ppc_mmu_reserved_mask(unsigned secondary);
// Primary opcode 31. A zero mask identifies an opcode outside the cache and synchronization controls.
uint32_t romaw_ppc_cache_reserved_mask(unsigned secondary);
// Canonical selector for MFSPR, MTSPR or MFTB, or zero for unsupported forms.
unsigned romaw_ppc_spr_selector(uint32_t opcode);

enum class romaw_ppc_register_form { none, word, indexed_segment, time_base_low, time_base_high, constant, effect };

struct romaw_ppc_register_move {
    romaw_ppc_register_form form;
    size_t offset;
    bool write;
    uint32_t mask;
    uint32_t value;

    romaw_ppc_register_move(romaw_ppc_register_form form = romaw_ppc_register_form::none, size_t offset = 0,
                            bool write = false, uint32_t mask = UINT32_MAX, uint32_t value = 0)
        : form(form), offset(offset), write(write), mask(mask), value(value)
    {
    }
};

// Decode a 750 state transfer. For effect writes, value names the SPR.
romaw_ppc_register_move romaw_ppc_750_register_move(uint32_t opcode);
void romaw_ppc_750_write_spr(unsigned selector, uint32_t value, uint32_t pc);

void romaw_ppc_jit_switch(void (*switch_host)(void*), void* target);

struct romaw_ppc_code_range {
    const unsigned char* data;
    uint32_t address, size;
};

unsigned romaw_ppc_translate_image(ROMlib_ppc_translation_cache*, const romaw_ppc_code_range*, size_t range_count,
                                   const uint32_t* entries, size_t entry_count, unsigned mode);

#endif
