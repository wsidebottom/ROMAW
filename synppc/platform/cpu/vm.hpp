/*+===================================================================
  File:      vm.hpp

  Summary:   Route interpreter scalar operands through the ROMAW memory or 750 bus adapter.

  Classes:   None; uses the active ROMlib_ppc_environment.

  Functions: Inline scalar reads/writes, byte-reversed accesses, romaw_ppc_widen_fp_single and vm_memset.

  Origin:    Executor-owned adaptation of the SheepShaver memory interface, moved from third_party.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/
#ifndef SYN_PPC_VM_HPP
#define SYN_PPC_VM_HPP
#include "sysdeps.h"
typedef uint32 vm_addr_t;

static inline uint32 vm_read_memory_1(vm_addr_t a)
{
    return romaw_ppc_load(a, 1);
}

static inline void vm_write_memory_1(vm_addr_t a, uint32 v)
{
    romaw_ppc_store(a, 1, v);
}

static inline uint32 vm_read_memory_2(vm_addr_t a)
{
    return romaw_ppc_load(a, 2);
}

static inline void vm_write_memory_2(vm_addr_t a, uint32 v)
{
    romaw_ppc_store(a, 2, v);
}

static inline uint32 vm_read_memory_4(vm_addr_t a)
{
    return romaw_ppc_load(a, 4);
}

static inline void vm_write_memory_4(vm_addr_t a, uint32 v)
{
    romaw_ppc_store(a, 4, v);
}

static inline uint64 vm_read_memory_8(vm_addr_t a)
{
    return romaw_ppc_load(a, 8);
}

static inline void vm_write_memory_8(vm_addr_t a, uint64 v)
{
    romaw_ppc_store(a, 8, v);
}

#define vm_read_memory_1_reversed vm_read_memory_1
#define vm_write_memory_1_reversed vm_write_memory_1

static inline uint32 vm_read_memory_2_reversed(vm_addr_t a)
{
    return __builtin_bswap16(vm_read_memory_2(a));
}

static inline uint32 vm_read_memory_4_reversed(vm_addr_t a)
{
    return __builtin_bswap32(vm_read_memory_4(a));
}

static inline void vm_write_memory_2_reversed(vm_addr_t a, uint32 v)
{
    vm_write_memory_2(a, __builtin_bswap16(v));
}

static inline void vm_write_memory_4_reversed(vm_addr_t a, uint32 v)
{
    vm_write_memory_4(a, __builtin_bswap32(v));
}

static inline uint64 romaw_ppc_widen_fp_single(uint32 value)
{
    // LFS is a format transfer and retains signaling NaNs.
    uint64 sign = uint64(value & 0x80000000u) << 32;
    unsigned exponent = (value >> 23) & 255, fraction = value & 0x7fffff;
    if (exponent)
        return sign | uint64(exponent == 255 ? 2047 : exponent + 896) << 52 | uint64(fraction) << 29;
    if (!fraction)
        return sign;
    unsigned leading = 31 - __builtin_clz(fraction);
    return sign | uint64(leading + 874) << 52 | uint64(fraction ^ (1u << leading)) << (52 - leading);
}

static inline void* vm_memset(vm_addr_t a, int c, size_t n)
{
    if (!romaw_ppc_environment->system)
        return memset(romaw_ppc_memory(a, n, true), c, n);
    for (size_t i = 0; i < n; ++i)
        vm_write_memory_1(a + i, c);
    return nullptr;
}
#endif
