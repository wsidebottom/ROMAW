/*+===================================================================
  File:      ppc-runtime.cpp

  Summary:   Execute PowerPC applications and adapt host memory, floating state and the 750 bus.

  Classes:   Declared: romaw_powerpc_cpu, host_floating_scope, host_callback_scope,
             romaw_ppc_native_bus_scope; Used: romaw_powerpc_cpu,
             romaw_ppc_arch_fault, romaw_ppc_jit_service_scope, floating_environment, host_floating_scope,
             std::exception, std::runtime_error

  Functions: GetTicks_usec, HandleInterrupt, ROMlib_ppc_clock_ticks, ROMlib_ppc_execute, ROMlib_ppc_switch_host,
             romaw_powerpc_cpu::romaw_powerpc_cpu, romaw_powerpc_cpu::host_call, romaw_powerpc_cpu::load,
             romaw_powerpc_cpu::reservation_slot, romaw_powerpc_cpu::reserve_word, romaw_powerpc_cpu::save,
             romaw_powerpc_cpu::store_fpr_integer, romaw_ppc_fetch, romaw_ppc_load, romaw_ppc_load_reserved,
             romaw_ppc_memory_checked, romaw_ppc_set_fpscr_rounding, romaw_ppc_set_rounding, romaw_ppc_step,
             romaw_ppc_store, romaw_ppc_store_conditional, romaw_ppc_transfer_multiple,
             romaw_ppc_transfer_string,
             romaw_ppc_system_service, romaw_ppc_translate, romaw_ppc_external, romaw_ppc_invalid_instruction,
             romaw_ppc_data_breakpoint,
             romaw_ppc_cache_control, romaw_ppc_power_wait, romaw_ppc_forward,
             romaw_ppc_native_bus_scope constructor/destructor, transfer, history and rethrow;
             floating_environment::capture, floating_environment::restore, host_floating_scope::host_floating_scope,
             host_floating_scope::~host_floating_scope

  Origin:    Existing repository source in synppc/src; original attribution and provenance comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

#include "sysdeps.h"
#include "synppc/ppc.h"
#include "synppc/bus750.h"
#include "cpu/ppc/ppc-cpu.hpp"
#include "ppc-jit.h"
#include "ppc-native-bus.h"
#include <chrono>
#include <algorithm>
#include <exception>

int64 TimebaseSpeed = 25000000;
uint32 PVR = 0x00080000; /* PowerPC 750. */

uint64 GetTicks_usec(void)
{
    static const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
}

void HandleInterrupt(powerpc_registers*)
{
}

extern "C" uint64_t ROMlib_ppc_clock_ticks(void)
{
    return GetTicks_usec() * 25;
}

/* All emulated calls run on ROMAW's CPU thread. Nested calls temporarily
   replace this context and restore it before returning to the outer CPU. */
ROMlib_ppc_environment* romaw_ppc_environment;
static uint32* active_reservation;
static unsigned host_callback_depth;

int floating_environment::capture(bool hold)
{
    /* Some MinGW runtimes save only x87 state through the C fenv API.
       SSE status and control must survive callbacks/nested calls too. */
    mxcsr_valid = true;
    if (mxcsr_valid)
        __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
    int result = hold ? feholdexcept(&environment) : fegetenv(&environment);
    if (!result && hold && mxcsr_valid) {
        unsigned guest = (mxcsr & ~0x807fu) | 0x1f80u;
        __asm__ volatile("ldmxcsr %0" : : "m"(guest));
    }
    return result;
}

int floating_environment::restore() const
{
    int result = fesetenv(&environment);
    if (mxcsr_valid)
        __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
    return result;
}

static const floating_environment* host_floating_environment;

int romaw_ppc_set_rounding(int mode)
{
    int result = fesetround(mode);
    if (!result) {
        unsigned csr;
        __asm__ volatile("stmxcsr %0" : "=m"(csr));
        csr = (csr & ~0x6000u) | (mode == FE_TOWARDZERO ? 0x6000u
                                  : mode == FE_UPWARD   ? 0x4000u
                                  : mode == FE_DOWNWARD ? 0x2000u
                                                        : 0);
        __asm__ volatile("ldmxcsr %0" : : "m"(csr));
    }
    return result;
}

int romaw_ppc_set_fpscr_rounding(uint32_t fpscr)
{
    static const int rounding[] = {FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};
    return romaw_ppc_set_rounding(rounding[fpscr & 3]);
}

/* Guest arithmetic uses the host FPU. Host callbacks must see the host's
   original environment, and nested invocations must restore the outer guest. */
class host_floating_scope {
    floating_environment guest;

  public:
    host_floating_scope()
    {
        if (guest.capture(false) || !host_floating_environment || host_floating_environment->restore())
            throw std::runtime_error("cannot switch PowerPC floating environment");
    }

    ~host_floating_scope()
    {
        guest.restore();
    }
};

romaw_ppc_native_bus_scope::romaw_ppc_native_bus_scope(const ROMlib_ppc_physical_bus* bus,
                                                       const uint32_t* packet_serial) noexcept
    : original(bus), serial(packet_serial)
{
    if (!original)
        return;
    checked = *original;
    checked.opaque = this;
    checked.transfer = checked.transfer ? transfer : nullptr;
    checked.history = checked.history ? history : nullptr;
    aborted = checked.callback_aborted ? *checked.callback_aborted : 0;
    checked.callback_aborted = &aborted;
}

romaw_ppc_native_bus_scope::~romaw_ppc_native_bus_scope() = default;

template <class Callback> int romaw_ppc_native_bus_scope::invoke(Callback callback) noexcept
{
    try {
        // Ordinary declared RAM never enters this boundary. Host mutations
        // persist between beats of one scalar packet; a new packet starts
        // with the embedding's original host environment.
        floating_environment guest;
        if (guest.capture(false))
            throw std::runtime_error("cannot switch PowerPC floating environment");

        struct restore_guest {
            floating_environment& guest;

            ~restore_guest()
            {
                guest.restore();
            }
        } restore{guest};

        if (!host_floating_environment)
            throw std::runtime_error("cannot switch PowerPC floating environment");
        uint32_t packet = serial ? *serial : 0;
        if (!host_valid || host_serial != packet) {
            host = *host_floating_environment;
            host_serial = packet;
            host_valid = true;
        }
        if (host.restore())
            throw std::runtime_error("cannot switch PowerPC floating environment");
        int ok;
        try {
            ok = callback();
        }
        catch (...) {
            host.capture(false);
            throw;
        }
        if (host.capture(false))
            throw std::runtime_error("cannot switch PowerPC floating environment");
        if (original->callback_aborted)
            aborted |= *original->callback_aborted;
        return ok;
    }
    catch (...) {
        exception = std::current_exception();
        aborted = 1;
        return 0;
    }
}

int romaw_ppc_native_bus_scope::transfer(void* opaque, uint32_t address, void* bytes, unsigned size, unsigned access,
                                         unsigned wimg, uint32_t* cause)
{
    auto& scope = *static_cast<romaw_ppc_native_bus_scope*>(opaque);
    return scope.invoke(
        [&] { return scope.original->transfer(scope.original->opaque, address, bytes, size, access, wimg, cause); });
}

int romaw_ppc_native_bus_scope::history(void* opaque, uint32_t address, uint32_t mask, uint32_t* cause)
{
    auto& scope = *static_cast<romaw_ppc_native_bus_scope*>(opaque);
    return scope.invoke([&] { return scope.original->history(scope.original->opaque, address, mask, cause); });
}

void romaw_ppc_native_bus_scope::rethrow() const
{
    if (exception)
        std::rethrow_exception(exception);
}

static bool native_bus_packet(ppc_native_memory_kernel::operation operation, ppc_native_memory_frame& frame,
                              const ROMlib_ppc_physical_bus* physical)
{
    auto kernel = romaw_ppc_native_memory_code();
    auto entry = kernel ? kernel->get(operation) : nullptr;
    if (!entry)
        return false;
    romaw_ppc_native_bus_scope boundary(physical, &frame.serial);
    frame.bus = boundary.bus();
    entry(&frame);
    boundary.rethrow();
    return true;
}

/* Native callbacks access physical guest RAM. Fence their shared data view,
   while retaining architectural I-cache staleness until explicit maintenance. */
static bool sync_host_memory(ROMlib_ppc_environment& env, bool deferred = false)
{
    if (!env.system)
        return true;
    ROMlib_ppc_bus_result result = {};
    int ok;
    ppc_native_memory_frame frame = {env.system, nullptr, 0, 0, 0, 0, 0, {}, 0, 1};
    if (native_bus_packet(ppc_native_memory_kernel::sync, frame, env.physical_bus)) {
        result = frame.result;
        ok = frame.ok;
    }
    else {
        host_floating_scope host;
        ok = ROMlib_ppc_750_host_sync(env.system, env.physical_bus, &result);
    }
    if (ok && !env.system->bus_check_pending)
        return true;
    if (deferred)
        return false;
    if (env.system->bus_check_pending)
        throw romaw_ppc_arch_fault{0x200, env.system->bus_check_cause, result.mapping.physical};
    throw std::runtime_error("PowerPC host-memory boundary during an active cache transaction");
}

class host_callback_scope {
    unsigned previous;

  public:
    host_callback_scope() : previous(host_callback_depth)
    {
        ++host_callback_depth;
    }

    ~host_callback_scope()
    {
        host_callback_depth = previous;
    }
};

template <class Callback> static bool with_host_memory(Callback callback, bool deferred = false)
{
    auto& env = *romaw_ppc_environment;
    if (!sync_host_memory(env, deferred))
        return false;
    try {
        host_callback_scope scope;
        callback();
    }
    catch (...) {
        sync_host_memory(env, deferred);
        throw;
    }
    return sync_host_memory(env, deferred);
}

uint8* romaw_ppc_memory_checked(uint32 address, size_t length, bool write)
{
    ROMlib_ppc_environment* active_environment = romaw_ppc_environment;
    if (!active_environment || !active_environment->memory || length > UINT32_MAX ||
        (uint64)address + length > UINT64_C(0x100000000))
        throw std::runtime_error("invalid PowerPC memory extent");
    void* p;
    {
        host_floating_scope host;
        p = active_environment->memory(active_environment->opaque, address, length, write);
    }
    if (!p) {
        char text[96];
        snprintf(text, sizeof text, "PowerPC %s at 0x%08x (%lu bytes)", write ? "write fault" : "read fault", address,
                 (unsigned long)length);
        throw std::runtime_error(text);
    }
    return (uint8*)p;
}

/* DABR compares effective bytes, independently of physical aliases and LE lanes.
   Check translation first: MPC750UM table 4-3 puts protection and direct-store
   faults above a data breakpoint. Preflight both pages before operand effects.
   Reference/change history can be updated even when the instruction faults
   (MPC750UM 5.4.1.2); it is not an operand store. */
void romaw_ppc_data_breakpoint(uint32 address, unsigned size, bool write)
{
    auto& env = *romaw_ppc_environment;
    auto& s = *env.system;
    if (!(s.dabr & (write ? 2 : 1)) || bool(s.dabr & 4) != bool(s.mmu.msr & 0x10))
        return;
    bool match = false;
    for (unsigned i = 0; i < size; ++i)
        match |= ((address + i) & ~7u) == (s.dabr & ~7u);
    if (!match)
        return;
    unsigned access = write ? ROMLIB_PPC_MMU_WRITE : 0;
    for (unsigned done = 0; done < size;) {
        uint32 logical = address + done;
        ROMlib_ppc_bus_result result = {};
        if (!romaw_ppc_translate(logical, access, &result)) {
            if (!result.mapping.vector)
                throw std::runtime_error("invalid PowerPC 750 breakpoint translation");
            throw romaw_ppc_arch_fault{result.mapping.vector, result.mapping.cause, result.mapping.address};
        }
        done += std::min(size - done, 4096 - (logical & 4095));
    }
    throw romaw_ppc_arch_fault{0x300, 0x00400000u | (write ? 0x02000000u : 0), address};
}

/* Physical callbacks run in the host floating environment, just like the legacy
   memory callback. Architectural faults unwind only to the instruction boundary. */
static uint64 system_transfer(uint32 address, unsigned size, bool write, uint64 value, unsigned access = 0,
                              bool conditional = false, uint32* physical = nullptr)
{
    auto& env = *romaw_ppc_environment;
    ROMlib_ppc_bus_result result = {};
    int ok;
    if (!(access & ROMLIB_PPC_MMU_INSTRUCTION))
        romaw_ppc_data_breakpoint(address, size, write);
    {
        host_floating_scope host;
        ok = conditional ? ROMlib_ppc_750_store_conditional(env.system, env.physical_bus, address, value, &result)
             : write     ? ROMlib_ppc_750_write(env.system, env.physical_bus, address, size, value, &result)
                         : ROMlib_ppc_750_read(env.system, env.physical_bus, address, size, access, &value, &result);
    }
    if (!ok) {
        if (!result.mapping.vector)
            throw std::runtime_error("invalid PowerPC 750 bus configuration");
        throw romaw_ppc_arch_fault{result.mapping.vector, result.mapping.cause, result.mapping.address};
    }
    if (physical)
        *physical = result.mapping.physical;
    return value;
}

[[noreturn]] void romaw_ppc_invalid_instruction(const char* reason)
{
    if (romaw_ppc_environment && romaw_ppc_environment->system)
        throw romaw_ppc_arch_fault{0x700, 0x80000, 0};
    throw std::runtime_error(reason);
}

uint64 romaw_ppc_load(uint32 address, unsigned size)
{
    if (romaw_ppc_environment->system)
        return system_transfer(address, size, false, 0);
    uint8* bytes = romaw_ppc_memory(address, size, false);
    uint64 value = 0;
    for (unsigned i = 0; i < size; ++i)
        value = (value << 8) | bytes[i];
    return value;
}

uint64 romaw_ppc_fetch(uint32 address, uint32& physical)
{
    auto& env = *romaw_ppc_environment;
    ppc_native_memory_frame frame = {env.system, nullptr, address, 4, ROMLIB_PPC_MMU_INSTRUCTION, 0, 0, {}, 0, 1};
    if (native_bus_packet(ppc_native_memory_kernel::read, frame, env.physical_bus)) {
        if (!frame.ok) {
            if (!frame.result.mapping.vector)
                throw std::runtime_error("invalid PowerPC 750 bus configuration");
            const auto& mapping = frame.result.mapping;
            throw romaw_ppc_arch_fault{mapping.vector, mapping.cause, mapping.address};
        }
        physical = frame.result.mapping.physical;
        return frame.value;
    }
    return system_transfer(address, 4, false, 0, ROMLIB_PPC_MMU_INSTRUCTION, false, &physical);
}

bool romaw_ppc_system_service(uint32 pc, uint32 physical, uint32 opcode)
{
    auto& env = *romaw_ppc_environment;
    if (!env.physical_service_gate && !env.system_service_gate)
        return false;
    int allowed;
    {
        host_floating_scope host;
        allowed = env.physical_service_gate ? env.physical_service_gate(env.opaque, physical, opcode)
                                            : env.system_service_gate(env.opaque, pc, opcode);
    }
    if (allowed < 0)
        throw std::runtime_error("PowerPC 750 host service rejected by embedding");
    return allowed != 0;
}

bool romaw_ppc_translate(uint32 address, unsigned access, ROMlib_ppc_bus_result* result)
{
    auto& env = *romaw_ppc_environment;
    ppc_native_memory_frame frame = {env.system, nullptr, address, 0, access, 0, 0, {}, 0, 1};
    if (result && native_bus_packet(ppc_native_memory_kernel::translate, frame, env.physical_bus)) {
        *result = frame.result;
        return frame.ok != 0;
    }
    host_floating_scope host;
    return ROMlib_ppc_750_translate(env.system, env.physical_bus, address, access, result) != 0;
}

uint32 romaw_ppc_external(uint32 address, bool write, uint32 value)
{
    auto& env = *romaw_ppc_environment;
    ROMlib_ppc_bus_result result = {};
    int ok;
    {
        host_floating_scope host;
        ok = ROMlib_ppc_750_external(env.system, env.physical_bus, address, write, &value, &result);
    }
    if (!ok) {
        if (!result.mapping.vector)
            throw std::runtime_error("invalid PowerPC 750 external-control bus");
        throw romaw_ppc_arch_fault{result.mapping.vector, result.mapping.cause, address};
    }
    return value;
}

void romaw_ppc_cache_control(uint32 address, unsigned opcode)
{
    auto& env = *romaw_ppc_environment;
    ROMlib_ppc_bus_result result = {};
    int ok;
    {
        host_floating_scope host;
        ok = ROMlib_ppc_750_cache_control(env.system, env.physical_bus, address, opcode, &result);
    }
    if (!ok) {
        if (!result.mapping.vector)
            throw std::runtime_error("invalid PowerPC 750 cache-control bus");
        throw romaw_ppc_arch_fault{result.mapping.vector, result.mapping.cause, result.mapping.address};
    }
}

void romaw_ppc_store(uint32 address, unsigned size, uint64 value)
{
    if (romaw_ppc_environment->system) {
        system_transfer(address, size, true, value);
        return;
    }
    uint8* bytes = romaw_ppc_memory(address, size, true);
    for (unsigned i = size; i--;) {
        bytes[i] = value;
        value >>= 8;
    }
}

uint32 romaw_ppc_load_reserved(powerpc_cpu* cpu, uint32 address)
{
    auto& env = *romaw_ppc_environment;
    if (!env.system) {
        uint32 value = vm_read_memory_4(address);
        cpu->romaw_registers().reserve_valid = 1;
        return value;
    }

    env.system->reservation_valid = 0;
    romaw_ppc_data_breakpoint(address, 4, false);
    uint32 value;
    ROMlib_ppc_bus_result result = {};
    int ok;
    {
        host_floating_scope host;
        ok = ROMlib_ppc_750_load_reserve(env.system, env.physical_bus, address, &value, &result);
    }
    if (!ok) {
        if (!result.mapping.vector)
            throw std::runtime_error("invalid PowerPC 750 reservation bus");
        throw romaw_ppc_arch_fault{result.mapping.vector, result.mapping.cause, result.mapping.address};
    }
    return value;
}

void romaw_ppc_store_conditional(powerpc_cpu* cpu, uint32 address, uint32 value)
{
    // MPC750UM 3.3.5.3: every conditional store consumes the nonspecific
    // reservation regardless of the target address, even if the bus faults.
    auto& registers = cpu->romaw_registers();
    auto* system = romaw_ppc_environment->system;
    bool available = system ? system->reservation_valid : registers.reserve_valid;
    registers.reserve_valid = 0;
    if (system)
        system->reservation_valid = 0;
    if (available) {
        if (system)
            system_transfer(address, 4, true, value, 0, true);
        else
            vm_write_memory_4(address, value);
    }
    registers.cr.clear(0);
    if (available)
        registers.cr.set(0, standalone_CR_EQ_field::mask());
    registers.cr.set_so(0, registers.xer.get_so());
}

void romaw_ppc_transfer_multiple(powerpc_cpu* cpu, uint32 address, unsigned reg, bool write)
{
    for (; reg < 32; ++reg, address += 4) {
        if (write)
            vm_write_memory_4(address, cpu->gpr(reg));
        else
            cpu->gpr(reg) = vm_read_memory_4(address);
    }
}

void romaw_ppc_transfer_string(powerpc_cpu* cpu, uint32 address, unsigned reg, unsigned bytes, bool write)
{
    if (write) {
        for (unsigned i = 0; i < bytes; ++i)
            vm_write_memory_1(address + i, cpu->gpr((reg + i / 4) & 31) >> (24 - (i % 4) * 8));
        return;
    }

    while (bytes >= 4) {
        cpu->gpr(reg) = vm_read_memory_4(address);
        bytes -= 4;
        address += 4;
        reg = (reg + 1) & 31;
    }
    if (bytes == 1)
        cpu->gpr(reg) = vm_read_memory_1(address) << 24;
    else if (bytes >= 2) {
        // A fault on the third byte must leave this entire GPR untouched.
        uint32 value = vm_read_memory_2(address) << 16;
        if (bytes == 3)
            value |= vm_read_memory_1(address + 2) << 8;
        cpu->gpr(reg) = value;
    }
}

bool romaw_ppc_forward(uint32 pc)
{
    auto& env = *romaw_ppc_environment;
    auto& s = *env.system;
    unsigned interval = s.ictc >> 1;
    if (!(s.ictc & 1) || !interval)
        return false;
    // Model a contiguous group of up to four words per aligned fetch block.
    // A redirect or a new block starts another forwarding interval.
    bool group = !s.forward_valid || s.forward_next != pc || !(pc & 15);
    s.forward_next = pc + 4;
    if (!group)
        return false;
    bool sampled = !s.forward_valid && env.wait_forward;
    if (sampled) {
        host_floating_scope host;
        env.wait_forward(env.opaque, s.core_cycles, 0);
    }
    uint64_t elapsed = s.core_cycles - s.forward_cycle;
    bool waited = s.forward_valid && elapsed < interval;
    if (waited) {
        host_floating_scope host;
        if (env.wait_forward) {
            env.wait_forward(env.opaque, s.forward_cycle, interval);
            if (s.core_cycles - s.forward_cycle < interval)
                throw std::runtime_error("750 board did not advance the instruction forwarding clock");
        }
        else
            ROMlib_ppc_750_advance_core(&s, interval - elapsed);
    }
    s.forward_valid = 1;
    s.forward_cycle = s.core_cycles;
    return waited || sampled;
}

void romaw_ppc_power_wait(bool waiting)
{
    auto& env = *romaw_ppc_environment;
    host_floating_scope host;
    bool supplied = env.power_wait && env.power_wait(env.opaque, waiting);
    if (waiting && !supplied) {
        env.waiting_for_power = 1;
        throw std::runtime_error("PowerPC 750 waiting for a wake event");
    }
}

void romaw_ppc_step(uint32 pc)
{
    ROMlib_ppc_environment& env = *romaw_ppc_environment;
    if (pc & 3)
        throw std::runtime_error("unaligned PowerPC instruction address");
    if (env.instruction_limit && env.instructions >= env.instruction_limit)
        throw std::runtime_error("PowerPC instruction limit exceeded");
    ++env.instructions;
    if (env.poll && !(env.instructions & 1023)) {
        if (active_reservation)
            *active_reservation = 0;
        if (env.system)
            env.system->reservation_valid = 0;
        host_floating_scope host;
        with_host_memory([&] { env.poll(env.opaque); }, true);
    }
}

class romaw_powerpc_cpu : public powerpc_cpu {
  public:
    romaw_powerpc_cpu()
    {
        romaw_registers().reserve_valid = 0;
        const instr_info_t host = {"romaw",   (execute_pmf)&romaw_powerpc_cpu::host_call, PPC_I(MAX), D_form, 6, 0,
                                   CFLOW_TRAP};
        init_decoder_entry(&host);
        const instr_info_t adapted[] = {
            {"lwarx", (execute_pmf)&romaw_powerpc_cpu::reserve_word, PPC_I(LWARX), X_form, 31, 20, CFLOW_NORMAL},
            {"stwcx.", (execute_pmf)&romaw_powerpc_cpu::reserve_word, PPC_I(STWCX), X_form, 31, 150, CFLOW_NORMAL},
            {"stfiwx", (execute_pmf)&romaw_powerpc_cpu::store_fpr_integer, PPC_I(MAX), X_form, 31, 983, CFLOW_NORMAL}};
        for (const instr_info_t& entry : adapted)
            init_decoder_entry(&entry);
    }

    uint32* reservation_slot()
    {
        return &romaw_registers().reserve_valid;
    }

    void store_fpr_integer(uint32 opcode)
    {
        if (opcode & 1)
            romaw_ppc_invalid_instruction("illegal PowerPC instruction");
        unsigned ra = (opcode >> 16) & 31, rb = (opcode >> 11) & 31;
        uint32 address = (ra ? gpr(ra) : 0) + gpr(rb);
        // MPCFPE, stfiwx: transfer the low word without any FP conversion.
        vm_write_memory_4(address, uint32(fpr_dw((opcode >> 21) & 31)));
        increment_pc(4);
    }

    void reserve_word(uint32 opcode)
    {
        bool conditional = ((opcode >> 1) & 1023) == 150;
        if ((opcode & 1) != conditional) {
            romaw_ppc_invalid_instruction("illegal PowerPC instruction");
        }
        unsigned ra = (opcode >> 16) & 31, rb = (opcode >> 11) & 31;
        uint32 address = (ra ? gpr(ra) : 0) + gpr(rb);
        if (address & 3)
            throw std::runtime_error("unaligned PowerPC reservation address");
        unsigned reg = (opcode >> 21) & 31;
        if (conditional)
            romaw_ppc_store_conditional(this, address, gpr(reg));
        else
            gpr(reg) = romaw_ppc_load_reserved(this, address);
        increment_pc(4);
    }

    void load(const ROMlib_ppc_registers& r)
    {
        for (int i = 0; i < 32; ++i) {
            gpr(i) = r.gpr[i];
            uint64 bits;
            memcpy(&bits, &r.fpr[i], sizeof bits);
            fpr_dw(i) = bits;
        }
        pc() = r.pc;
        lr() = r.lr;
        ctr() = r.ctr;
        cr().set(r.cr);
        xer().set(r.xer);
        fpscr() = r.fpscr;
        if (romaw_ppc_set_fpscr_rounding(r.fpscr))
            throw std::runtime_error("cannot set PowerPC rounding mode");
    }

    void save(ROMlib_ppc_registers& r)
    {
        for (int i = 0; i < 32; ++i) {
            r.gpr[i] = gpr(i);
            uint64 bits = fpr_dw(i);
            memcpy(&r.fpr[i], &bits, sizeof bits);
        }
        r.pc = pc();
        r.lr = lr();
        r.ctr = ctr();
        r.cr = cr().get();
        r.xer = xer().get();
        r.fpscr = fpscr();
    }

    void host_call(uint32 opcode)
    {
        const uint32 id = opcode & 0x03FFFFFF;
        if (!id) {
            with_host_memory([] {});
            spcflags().set(SPCFLAG_CPU_EXEC_RETURN);
            increment_pc(4);
            return;
        }
        ROMlib_ppc_registers r;
        romaw_registers().reserve_valid = 0;
        if (romaw_ppc_environment->system)
            romaw_ppc_environment->system->reservation_valid = 0;
        save(r);
        {
            host_floating_scope host;
            with_host_memory([&] {
                auto& env = *romaw_ppc_environment;
                // The guest host-service opcode reaches here after native
                // code has returned. CFM can prepare newly linked code before
                // entering a library initializer in a nested CPU scope.
                romaw_ppc_jit_service_scope jit_service;
                if (env.system && (env.system->mmu.msr & 0x10) && env.mapped_service) {
                    ROMlib_ppc_bus_result fault = {};
                    if (env.mapped_service(env.opaque, id, &r, &fault))
                        return;
                    if (fault.mapping.vector)
                        throw romaw_ppc_arch_fault{fault.mapping.vector, fault.mapping.cause, fault.mapping.address};
                    throw std::runtime_error("unhandled translated PowerPC service");
                }
                if (!romaw_ppc_environment->service ||
                    !romaw_ppc_environment->service(romaw_ppc_environment->opaque, id, &r))
                    throw std::runtime_error("unhandled PowerPC host service");
            });
        }
        load(r);
        pc() = lr();
    }
};

extern "C" void ROMlib_ppc_switch_host(void (*switch_host)(void*), void* target)
{
    ROMlib_ppc_environment* environment = romaw_ppc_environment;
    uint32* reservation = active_reservation;
    const floating_environment* floating = host_floating_environment;
    const unsigned callback_depth = host_callback_depth;
    if (environment)
        sync_host_memory(*environment);
    if (reservation)
        *reservation = 0;
    if (environment && environment->system)
        environment->system->reservation_valid = 0;
    try {
        romaw_ppc_jit_switch(switch_host, target);
    }
    catch (...) {
        romaw_ppc_environment = environment;
        active_reservation = reservation;
        host_floating_environment = floating;
        host_callback_depth = callback_depth;
        if (environment)
            sync_host_memory(*environment);
        throw;
    }
    romaw_ppc_environment = environment;
    active_reservation = reservation;
    host_floating_environment = floating;
    host_callback_depth = callback_depth;
    if (environment)
        sync_host_memory(*environment);
}

extern "C" int ROMlib_ppc_execute(ROMlib_ppc_registers* r, ROMlib_ppc_environment* env)
{
    if (!r || !env || (!env->system && !env->memory))
        return 0;
    if (env->system && (!env->physical_bus || !env->physical_bus->transfer || !env->physical_bus->history)) {
        snprintf(env->error, sizeof env->error, "missing PowerPC 750 physical bus");
        return 0;
    }
    if (env->execution_flags & ~ROMLIB_PPC_EXEC_JIT) {
        snprintf(env->error, sizeof env->error, "invalid PowerPC execution flags");
        return 0;
    }
    if (env->linear_memory && (uint64_t)env->linear_address + env->linear_size > UINT64_C(0x100000000)) {
        snprintf(env->error, sizeof env->error, "invalid PowerPC linear RAM extent");
        return 0;
    }
    ROMlib_ppc_environment* previous = romaw_ppc_environment;
    unsigned previous_callback_depth = host_callback_depth;
    bool nested_host = previous && previous_callback_depth;
    uint32* previous_reservation = active_reservation;
    if (active_reservation)
        *active_reservation = 0;
    if (previous && previous->system)
        previous->system->reservation_valid = 0;
    floating_environment host_environment;
    if (host_environment.capture(true)) {
        snprintf(env->error, sizeof env->error, "cannot isolate host floating environment");
        return 0;
    }
    const floating_environment* previous_floating_environment = host_floating_environment;
    host_floating_environment = &host_environment;
    romaw_ppc_environment = env;
    host_callback_depth = 0;
    env->error[0] = 0;
    env->waiting_for_power = env->waiting_for_tlb = 0;
    env->instructions = 0;
    env->native_blocks = env->native_instructions = 0;
    env->native_compilations = env->native_cache_hits = 0;
    env->native_kernel_instructions = 0;
    romaw_ppc_jit_scope jit(env);
    int success = 0;
    romaw_powerpc_cpu* cpu = NULL;
    try {
        if (nested_host)
            sync_host_memory(*env);
        cpu = new romaw_powerpc_cpu;
        cpu->load(*r);
        active_reservation = cpu->reservation_slot();
        cpu->execute(r->pc);
        success = 1;
    }
    catch (const std::exception& e) {
        snprintf(env->error, sizeof env->error, "%s", e.what());
    }
    catch (...) {
        snprintf(env->error, sizeof env->error, "PowerPC execution failed");
    }
    if (nested_host) {
        try {
            if (!sync_host_memory(*env, true)) {
                snprintf(env->error, sizeof env->error, "PowerPC nested host-memory synchronization failed");
                success = 0;
            }
        }
        catch (...) {
            snprintf(env->error, sizeof env->error, "PowerPC nested host-memory callback failed");
            success = 0;
        }
    }
    if (cpu) {
        cpu->save(*r);
        delete cpu;
    }
    romaw_ppc_environment = previous;
    host_callback_depth = previous_callback_depth;
    active_reservation = previous_reservation;
    host_floating_environment = previous_floating_environment;
    host_environment.restore();
    return success;
}
