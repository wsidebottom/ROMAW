/*+===================================================================
  File:      ppc-native-memory.cpp

  Summary:   Instantiate closed native 750 transfer, MMU and cache operation roots.

  Classes:   Uses ppc_native_memory_frame.

  Functions: native_memory_read, native_memory_write, native_memory_reserve,
             native_memory_conditional, native_memory_external, native_memory_translate,
             native_memory_cache, native_memory_sync, native_memory_snoop, native_memory_msr, native_memory_spr.

  Origin:    Original ARDI Executor native PowerPC memory lowering; algorithms
             are compiled from the maintained 750 bus, MMU and cache sources.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ppc-native-memory.h"

#define MEMORY_ROOT(name) \
    extern "C" __attribute__((used, noinline)) void native_memory_##name(ppc_native_memory_frame* f)

MEMORY_ROOT(read)
{
    f->ok = ROMlib_ppc_750_read(f->system, f->bus, f->address, f->size, f->access, &f->value, &f->result);
}

MEMORY_ROOT(write)
{
    f->ok = ROMlib_ppc_750_write(f->system, f->bus, f->address, f->size, f->value, &f->result);
}

MEMORY_ROOT(reserve)
{
    uint32_t value = uint32_t(f->value);
    f->ok = ROMlib_ppc_750_load_reserve(f->system, f->bus, f->address, &value, &f->result);
    if (f->ok)
        f->value = value;
}

MEMORY_ROOT(conditional)
{
    f->ok = ROMlib_ppc_750_store_conditional(f->system, f->bus, f->address, uint32_t(f->value), &f->result);
}

MEMORY_ROOT(external)
{
    uint32_t value = uint32_t(f->value);
    f->ok = ROMlib_ppc_750_external(f->system, f->bus, f->address, f->access, &value, &f->result);
    if (f->ok)
        f->value = value;
}

MEMORY_ROOT(translate)
{
    f->ok = ROMlib_ppc_750_translate(f->system, f->bus, f->address, f->access, &f->result);
}

MEMORY_ROOT(cache)
{
    f->ok = ROMlib_ppc_750_cache_control(f->system, f->bus, f->address, f->selector, &f->result);
}

MEMORY_ROOT(sync)
{
    f->ok = ROMlib_ppc_750_host_sync(f->system, f->bus, &f->result);
}

MEMORY_ROOT(snoop)
{
    f->ok = ROMlib_ppc_750_snoop(f->system, f->bus, f->address, f->selector, f->access, f->size, &f->result);
}

MEMORY_ROOT(msr)
{
    f->ok = f->system != nullptr;
    if (f->system && !f->system->checkstop) {
        ROMlib_ppc_750_set_msr(f->system, uint32_t(f->value), f->address);
        if (f->access) // RFI also acknowledges the old instruction breakpoint.
            f->system->iabr_pending = 0;
    }
}

MEMORY_ROOT(spr)
{
    f->ok = ROMlib_ppc_750_write_spr(f->system, f->selector, uint32_t(f->value), f->address);
}
