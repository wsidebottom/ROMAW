/*+===================================================================
  File:      ppc_native_memory_checks.cpp

  Summary:   Execute existing fixed-result hardware tests through relocated native MMU/cache code.

  Classes:   native_boundary; uses ppc_native_memory_frame and ppc_native_memory_kernel.

  Functions: native_test_read, native_test_write, native_test_reserve, native_test_conditional,
             native_test_external, native_test_translate, native_test_cache, native_test_sync, native_test_snoop.

  Origin:    Original ARDI Executor native PowerPC memory regression adapter.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/
#include "../src/ppc-native-memory-code.h"
#include <cassert>
#include <exception>

namespace {
struct native_boundary {
    const ROMlib_ppc_physical_bus* original;
    unsigned aborted = 0;
    std::exception_ptr exception;

    template <class F> int invoke(F callback)
    {
        try {
            int result = callback();
            if (original->callback_aborted)
                aborted |= *original->callback_aborted;
            return result;
        }
        catch (...) {
            exception = std::current_exception();
            aborted = 1;
            return 0;
        }
    }

    static int transfer(void* opaque, uint32_t address, void* bytes, unsigned size, unsigned access, unsigned wimg,
                        uint32_t* cause)
    {
        auto& b = *static_cast<native_boundary*>(opaque);
        return b.invoke(
            [&] { return b.original->transfer(b.original->opaque, address, bytes, size, access, wimg, cause); });
    }

    static int history(void* opaque, uint32_t address, uint32_t mask, uint32_t* cause)
    {
        auto& b = *static_cast<native_boundary*>(opaque);
        return b.invoke([&] { return b.original->history(b.original->opaque, address, mask, cause); });
    }
};

int invoke(ppc_native_memory_frame& frame, ppc_native_memory_kernel::operation op, ROMlib_ppc_bus_result* result)
{
    static ppc_native_memory_kernel kernel;
    auto entry = kernel.get(op);
    assert(entry);
    native_boundary boundary;
    boundary.original = frame.bus;
    ROMlib_ppc_physical_bus checked;
    if (frame.bus) {
        checked = *frame.bus;
        checked.opaque = &boundary;
        checked.transfer = checked.transfer ? native_boundary::transfer : nullptr;
        checked.history = checked.history ? native_boundary::history : nullptr;
        boundary.aborted = checked.callback_aborted ? *checked.callback_aborted : 0;
        checked.callback_aborted = &boundary.aborted;
        frame.bus = &checked;
    }
    entry(&frame);
    if (result)
        *result = frame.result;
    if (boundary.exception)
        std::rethrow_exception(boundary.exception);
    return frame.ok;
}
} // namespace

extern "C" int native_test_read(ROMlib_ppc_system750* s, const ROMlib_ppc_physical_bus* b, uint32_t a, unsigned n,
                                unsigned access, uint64_t* value, ROMlib_ppc_bus_result* result)
{
    if (!value || !result)
        return ROMlib_ppc_750_read(s, b, a, n, access, value, result);
    ppc_native_memory_frame f = {s, b, a, n, access, 0, *value, {}, 0, 0};
    int ok = invoke(f, ppc_native_memory_kernel::read, result);
    if (ok)
        *value = f.value;
    return ok;
}

extern "C" int native_test_write(ROMlib_ppc_system750* s, const ROMlib_ppc_physical_bus* b, uint32_t a, unsigned n,
                                 uint64_t value, ROMlib_ppc_bus_result* result)
{
    if (!result)
        return ROMlib_ppc_750_write(s, b, a, n, value, result);
    ppc_native_memory_frame f = {s, b, a, n, 0, 0, value, {}, 0, 0};
    return invoke(f, ppc_native_memory_kernel::write, result);
}

extern "C" int native_test_reserve(ROMlib_ppc_system750* s, const ROMlib_ppc_physical_bus* b, uint32_t a,
                                   uint32_t* value, ROMlib_ppc_bus_result* result)
{
    if (!value || !result)
        return ROMlib_ppc_750_load_reserve(s, b, a, value, result);
    ppc_native_memory_frame f = {s, b, a, 4, 0, 0, *value, {}, 0, 0};
    int ok = invoke(f, ppc_native_memory_kernel::reserve, result);
    if (ok)
        *value = uint32_t(f.value);
    return ok;
}

extern "C" int native_test_conditional(ROMlib_ppc_system750* s, const ROMlib_ppc_physical_bus* b, uint32_t a,
                                       uint32_t value, ROMlib_ppc_bus_result* result)
{
    if (!result)
        return ROMlib_ppc_750_store_conditional(s, b, a, value, result);
    ppc_native_memory_frame f = {s, b, a, 4, 0, 0, value, {}, 0, 0};
    return invoke(f, ppc_native_memory_kernel::conditional, result);
}

extern "C" int native_test_external(ROMlib_ppc_system750* s, const ROMlib_ppc_physical_bus* b, uint32_t a, int write,
                                    uint32_t* value, ROMlib_ppc_bus_result* result)
{
    if (!value || !result)
        return ROMlib_ppc_750_external(s, b, a, write, value, result);
    ppc_native_memory_frame f = {s, b, a, 4, unsigned(write), 0, *value, {}, 0, 0};
    int ok = invoke(f, ppc_native_memory_kernel::external, result);
    if (ok)
        *value = uint32_t(f.value);
    return ok;
}

extern "C" int native_test_translate(ROMlib_ppc_system750* s, const ROMlib_ppc_physical_bus* b, uint32_t a,
                                     unsigned access, ROMlib_ppc_bus_result* result)
{
    if (!result)
        return ROMlib_ppc_750_translate(s, b, a, access, result);
    ppc_native_memory_frame f = {s, b, a, 0, access, 0, 0, {}, 0, 0};
    return invoke(f, ppc_native_memory_kernel::translate, result);
}

extern "C" int native_test_cache(ROMlib_ppc_system750* s, const ROMlib_ppc_physical_bus* b, uint32_t a, unsigned op,
                                 ROMlib_ppc_bus_result* result)
{
    if (!result)
        return ROMlib_ppc_750_cache_control(s, b, a, op, result);
    ppc_native_memory_frame f = {s, b, a, 0, 0, op, 0, {}, 0, 0};
    return invoke(f, ppc_native_memory_kernel::cache, result);
}

extern "C" int native_test_sync(ROMlib_ppc_system750* s, const ROMlib_ppc_physical_bus* b,
                                ROMlib_ppc_bus_result* result)
{
    if (!result)
        return ROMlib_ppc_750_host_sync(s, b, result);
    ppc_native_memory_frame f = {s, b, 0, 0, 0, 0, 0, {}, 0, 0};
    return invoke(f, ppc_native_memory_kernel::sync, result);
}

extern "C" int native_test_snoop(ROMlib_ppc_system750* s, const ROMlib_ppc_physical_bus* b, uint32_t a, unsigned type,
                                 int global, int inhibited, ROMlib_ppc_bus_result* result)
{
    if (!result)
        return ROMlib_ppc_750_snoop(s, b, a, type, global, inhibited, result);
    ppc_native_memory_frame f = {s, b, a, unsigned(inhibited), unsigned(global), type, 0, {}, 0, 0};
    return invoke(f, ppc_native_memory_kernel::snoop, result);
}
