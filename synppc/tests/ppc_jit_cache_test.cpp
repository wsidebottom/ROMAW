/*+===================================================================
  File:      ppc_jit_cache_test.cpp

  Summary:   Retain warmed native blocks when cold code fills the bounded cache.

  Classes:   Declared: fixture; Used: ROMlib_ppc_environment, ROMlib_ppc_registers, fixture

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* Retain warmed native blocks when cold code fills the bounded cache. */
#include "synppc/ppc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

struct fixture {
    unsigned char memory[16384] = {};
    unsigned calls = 0;
    ROMlib_ppc_environment env = {};
    uint64_t before = 0;
};

static void word(fixture& f, unsigned address, uint32_t value)
{
    for (int i = 3; i >= 0; --i) {
        f.memory[address + i] = value;
        value >>= 8;
    }
}

static void* map(void* opaque, uint32_t address, size_t length, int)
{
    fixture* f = static_cast<fixture*>(opaque);
    return address <= sizeof f->memory && length <= sizeof f->memory - address ? f->memory + address : nullptr;
}

static int service(void* opaque, uint32_t id, ROMlib_ppc_registers* r)
{
    fixture& f = *static_cast<fixture*>(opaque);
    assert(id == 1);
    ++f.calls;
    if (f.calls < 32)
        r->lr = 0x100;
    else if (f.calls < 544)
        r->lr = 0x200 + (f.calls - 32) * 20;
    else if (f.calls == 544) {
        f.before = f.env.native_blocks;
        r->lr = 0x100;
    }
    else {
        if (ROMLIB_PPC_EXEC_JIT == f.env.execution_flags && ROMlib_ppc_jit_available())
            assert(f.env.native_blocks == f.before + 1);
        r->lr = 0x80;
    }
    return 1;
}

int main(void)
{
    for (unsigned mode = 0; mode < 2; ++mode) {
        fixture f;
        word(f, 0x80, 0x18000000);
        for (unsigned block = 0; block <= 512; ++block) {
            unsigned address = block ? 0x200 + (block - 1) * 20 : 0x100;
            for (unsigned i = 0; i < 4; ++i)
                word(f, address + i * 4, 0x38630001); /* addi r3,r3,1 */
            word(f, address + 16, 0x18000001);
        }
        f.env.opaque = &f;
        f.env.memory = map;
        f.env.service = service;
        f.env.linear_memory = f.memory;
        f.env.linear_size = sizeof f.memory;
        f.env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
        ROMlib_ppc_registers r = {};
        r.pc = 0x100;
        assert(ROMlib_ppc_execute(&r, &f.env));
        assert(f.calls == 545 && r.gpr[3] == 545 * 4);
    }
    puts("JIT cache pressure preserves warmed code and interpreter results");
}
