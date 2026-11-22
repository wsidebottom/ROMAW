/*+===================================================================
  File:      ppc-system.cpp

  Summary:   Execute translated 750 instructions and enter guest exception handlers.

  Classes:   Uses powerpc_cpu, ROMlib_ppc_system750, ROMlib_ppc_mmu750, romaw_ppc_arch_fault,
             romaw_ppc_register_form, romaw_ppc_register_move and romaw_ppc_system_operation.

  Functions: romaw_ppc_decode_system_operation, romaw_ppc_750_excludes_instruction,
             romaw_ppc_mmu_reserved_mask, romaw_ppc_cache_reserved_mask, romaw_ppc_spr_selector,
             romaw_ppc_750_register_move, romaw_ppc_750_write_spr, romaw_ppc_system_kernel,
             romaw_ppc_system_step

  Origin:    Original integration using MPC750UM supervisor and exception definitions.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/
#include "sysdeps.h"
#include "synppc/bus750.h"
#include "ppc-jit.h"
#include "cpu/ppc/ppc-cpu.hpp"

romaw_ppc_system_operation romaw_ppc_decode_system_operation(uint32_t opcode)
{
    using operation = romaw_ppc_system_operation;
    unsigned primary = opcode >> 26, xo = (opcode >> 1) & 1023;
    if (primary == 3)
        return operation::trap;
    if (primary == 17)
        return opcode == 0x44000002 ? operation::system_call : operation::invalid;
    if (primary == 19 && xo == 50)
        return opcode == 0x4C000064 ? operation::rfi : operation::invalid;
    if (primary == 19 && xo == 150 && opcode != 0x4C00012C)
        return operation::invalid; // ISYNC reserves every XL operand field and Rc.
    if (primary == 31 && (xo == 4 || xo == 310 || xo == 438)) {
        if (opcode & 1)
            return operation::invalid;
        return xo == 4 ? operation::trap : operation::external;
    }
    return operation::other;
}

bool romaw_ppc_750_excludes_instruction(uint32_t opcode, const char* mnemonic)
{
    return (opcode >> 26) == 4 || mnemonic[0] == 'v' || !strncmp(mnemonic, "lv", 2) || !strncmp(mnemonic, "stv", 3) ||
           !strncmp(mnemonic, "dst", 3) || !strcmp(mnemonic, "dss") || !strcmp(mnemonic, "fsqrt") ||
           !strcmp(mnemonic, "fsqrts");
}

uint32_t romaw_ppc_mmu_reserved_mask(unsigned secondary)
{
    switch (secondary) {
    case 83:  // mfmsr
    case 146: // mtmsr
        return 0x001FF801;
    case 595: // mfsr
    case 210: // mtsr
        return 0x0010F801;
    case 659: // mfsrin
    case 242: // mtsrin
        return 0x001F0001;
    case 306: // tlbie
        return 0x03FF0001;
    case 566: // tlbsync
        return 0x03FFF801;
    default:
        return 0;
    }
}

uint32_t romaw_ppc_cache_reserved_mask(unsigned secondary)
{
    switch (secondary) {
    case 598: // sync
    case 854: // eieio
        return 0x03FFF801;
    case 54:   // dcbst
    case 86:   // dcbf
    case 246:  // dcbtst
    case 278:  // dcbt
    case 470:  // dcbi
    case 982:  // icbi
    case 1014: // dcbz
        return 0x03E00001;
    default:
        return 0;
    }
}

unsigned romaw_ppc_spr_selector(uint32_t opcode)
{
    if (opcode & 1)
        return 0;
    unsigned number = ((opcode >> 16) & 31) | ((opcode >> 6) & 0x3E0);
    unsigned secondary = (opcode >> 1) & 1023;
    if (secondary == 371)
        return (number == 268 || number == 269) ? number : 0;
    bool write = secondary == 467;
    if (number >= 936 && number <= 942)
        return write ? 0 : number + 16;
    if (number == 268 || number == 269)
        return write ? 0 : number;
    if (number == 284 || number == 285)
        return write ? number : 0;
    if ((number >= 272 && number <= 275) || (number >= 528 && number <= 543) || (number >= 952 && number <= 958) ||
        (number >= 1019 && number <= 1022))
        return number;
    switch (number) {
    case 1:
    case 8:
    case 9:
    case 18:
    case 19:
    case 22:
    case 25:
    case 26:
    case 27:
    case 282:
    case 287:
    case 1008:
    case 1009:
    case 1010:
    case 1013:
    case 1017:
        return number;
    default:
        return 0;
    }
}

romaw_ppc_register_move romaw_ppc_750_register_move(uint32_t opcode)
{
    using form = romaw_ppc_register_form;
    using move = romaw_ppc_register_move;
    using system = ROMlib_ppc_system750;
    using mmu = ROMlib_ppc_mmu750;
    constexpr size_t mmu_offset = offsetof(system, mmu);
    constexpr size_t segment_offset = mmu_offset + offsetof(mmu, sr);
    if (opcode >> 26 != 31)
        return {};
    unsigned secondary = (opcode >> 1) & 1023;
    if (opcode & romaw_ppc_mmu_reserved_mask(secondary))
        return {};
    switch (secondary) {
    case 83: // mfmsr
        return {form::word, mmu_offset + offsetof(mmu, msr)};
    case 595: // mfsr
    case 210: // mtsr
        return {form::word, segment_offset + ((opcode >> 16) & 15) * sizeof(uint32), secondary == 210};
    case 659: // mfsrin
    case 242: // mtsrin
        return {form::indexed_segment, segment_offset, secondary == 242};
    case 339: // mfspr
    case 467: // mtspr
    case 371: // mftb
        break;
    default:
        return {};
    }

    bool write = secondary == 467;
    unsigned number = romaw_ppc_spr_selector(opcode);
    if (number >= 272 && number <= 275)
        return {form::word, offsetof(system, sprg) + (number - 272) * sizeof(uint32), write};
    if (number >= 528 && number <= 535)
        return {form::word, mmu_offset + offsetof(mmu, ibat) + (number - 528) * sizeof(uint32), write};
    if (number >= 536 && number <= 543)
        return {form::word, mmu_offset + offsetof(mmu, dbat) + (number - 536) * sizeof(uint32), write};
    const move effect = {form::effect, 0, true, UINT32_MAX, number};
    auto read_or_effect = [write, &effect](size_t offset) -> move { return write ? effect : move{form::word, offset}; };
    if (number >= 1020 && number <= 1022)
        return read_or_effect(offsetof(system, thrm) + (number - 1020) * sizeof(uint32));
    switch (number) {
    case 18:
        return {form::word, offsetof(system, dsisr), write};
    case 19:
        return {form::word, offsetof(system, dar), write};
    case 22:
        return read_or_effect(offsetof(system, decrementer));
    case 25:
        return {form::word, mmu_offset + offsetof(mmu, sdr1), write};
    case 26:
        return {form::word, offsetof(system, srr0), write};
    case 27:
        return {form::word, offsetof(system, srr1), write};
    case 268:
    case 269:
        // Read selectors, also accepted by MFTB. Figure 1-5/MPCFPE distinguish
        // these from write selectors 284/285; MPC750UM table 2-48 mislabels them.
        return {number == 268 ? form::time_base_low : form::time_base_high, offsetof(system, time_base)};
    case 282:
        return {form::word, offsetof(system, ear), write, write ? 0x8000000Fu : UINT32_MAX};
    case 284:
    case 285:
        return effect;
    case 287:
        return write ? effect : move{form::constant, 0, false, UINT32_MAX, 0x00080000};
    case 952:
        return read_or_effect(offsetof(system, mmcr0));
    case 953:
    case 954:
    case 957:
    case 958:
        return read_or_effect(offsetof(system, pmc) + (number < 955 ? number - 953 : number - 955) * sizeof(uint32));
    case 955:
        return read_or_effect(offsetof(system, sia));
    case 956:
        return read_or_effect(offsetof(system, mmcr1));
    case 1008:
        return read_or_effect(offsetof(system, hid0));
    case 1009:
        return write ? effect : move{form::word, offsetof(system, hid1), false, 0xF0000000};
    case 1010:
        return read_or_effect(offsetof(system, iabr));
    case 1013:
        return {form::word, offsetof(system, dabr), write};
    case 1017:
        return read_or_effect(offsetof(system, l2cr));
    case 1019:
        return read_or_effect(offsetof(system, ictc));
    default:
        return {};
    }
}

namespace {
[[noreturn]] void illegal()
{
    throw romaw_ppc_arch_fault{0x700, 0x80000, 0};
}

void supervisor()
{
    if (romaw_ppc_environment->system->mmu.msr & 0x4000)
        throw romaw_ppc_arch_fault{0x700, 0x40000, 0};
}

bool register_move(powerpc_cpu* cpu, uint32 opcode)
{
    using form = romaw_ppc_register_form;
    auto move = romaw_ppc_750_register_move(opcode);
    if (move.form == form::none)
        return false;
    auto& registers = cpu->romaw_registers();
    if (romaw_ppc_native_system_step(cpu, &registers.pc, opcode))
        return true;

    auto& state = *romaw_ppc_environment->system;
    auto& gpr = cpu->gpr((opcode >> 21) & 31);
    if (move.form == form::constant)
        gpr = move.value;
    else if (move.form == form::effect)
        romaw_ppc_750_write_spr(move.value, gpr, registers.pc);
    else if (move.form == form::time_base_low || move.form == form::time_base_high)
        gpr = uint32(state.time_base >> (move.form == form::time_base_high ? 32 : 0));
    else {
        size_t offset = move.offset;
        if (move.form == form::indexed_segment)
            offset += (cpu->gpr((opcode >> 11) & 31) >> 28) * sizeof(uint32);
        auto* slot = reinterpret_cast<unsigned char*>(&state) + offset;
        if (move.write) {
            uint32 value = gpr & move.mask;
            memcpy(slot, &value, sizeof value);
        }
        else {
            uint32 value;
            memcpy(&value, slot, sizeof value);
            gpr = value & move.mask;
        }
    }
    registers.pc += 4;
    return true;
}

void execute_cpu_spr(powerpc_cpu* cpu, unsigned number, unsigned r, bool write)
{
    auto& registers = cpu->romaw_registers();
    if (number == 1) {
        if (write)
            registers.xer.set(cpu->gpr(r));
        else
            cpu->gpr(r) = registers.xer.get();
    }
    else {
        auto& slot = number == 8 ? registers.lr : registers.ctr;
        if (write)
            slot = cpu->gpr(r);
        else
            cpu->gpr(r) = slot;
    }
    registers.pc += 4;
}
}

void romaw_ppc_750_write_spr(unsigned number, uint32 value, uint32 pc)
{
    int accepted = ROMlib_ppc_750_write_spr(romaw_ppc_environment->system, number, value, pc);
    if (accepted < 0)
        throw std::runtime_error("PowerPC 750 L2 storage is unavailable or busy");
    if (!accepted)
        illegal();
}

namespace {
void spr(powerpc_cpu* cpu, uint32 opcode)
{
    unsigned number = ((opcode >> 16) & 31) | ((opcode >> 6) & 0x3E0);
    if (opcode & 1)
        illegal();
    // The encoded selector controls privilege, including the read-only PMU aliases.
    if (((opcode >> 1) & 1023) != 371 && (number & 16))
        supervisor();
    if (!romaw_ppc_spr_selector(opcode))
        illegal();
    if (register_move(cpu, opcode))
        return;
    if (number != 1 && number != 8 && number != 9)
        illegal();
    if (!romaw_ppc_native_system_step(cpu, &cpu->romaw_registers().pc, opcode))
        execute_cpu_spr(cpu, number, (opcode >> 21) & 31, ((opcode >> 1) & 1023) == 467);
}

void execute_cache_control(powerpc_cpu* cpu, uint32 opcode)
{
    unsigned operation = (opcode >> 1) & 1023;
    unsigned a = (opcode >> 16) & 31, b = (opcode >> 11) & 31;
    uint32 address = (operation == 598 || operation == 854) ? 0 : (a ? cpu->gpr(a) : 0) + cpu->gpr(b);
    romaw_ppc_cache_control(address, operation);
    cpu->romaw_registers().pc += 4;
}

void execute_mmu_control(powerpc_cpu* cpu, uint32 opcode)
{
    auto& s = *romaw_ppc_environment->system;
    unsigned xo = (opcode >> 1) & 1023;
    unsigned r = (opcode >> 21) & 31, b = (opcode >> 11) & 31;
    switch (xo) {
    case 146: // mtmsr
        ROMlib_ppc_750_set_msr(&s, cpu->gpr(r), cpu->romaw_registers().pc);
        break;
    case 306: // tlbie
        ROMlib_ppc_mmu_tlbie(&s.mmu, cpu->gpr(b));
        break;
    case 566: // tlbsync
        s.tlbsync_wait = !!(s.inputs & ROMLIB_PPC_750_TLBISYNC);
        break;
    default:
        illegal();
    }
    cpu->romaw_registers().pc += 4;
}

void execute_system_operation(powerpc_cpu* cpu, uint32 opcode)
{
    using operation = romaw_ppc_system_operation;
    unsigned primary = opcode >> 26, xo = (opcode >> 1) & 1023;
    unsigned r = (opcode >> 21) & 31, a = (opcode >> 16) & 31, b = (opcode >> 11) & 31;
    switch (romaw_ppc_decode_system_operation(opcode)) {
    case operation::trap: {
        uint32 left = cpu->gpr(a);
        uint32 right = primary == 3 ? uint32(int32(int16(opcode))) : cpu->gpr(b);
        if (((r & 16) && int32(left) < int32(right)) || ((r & 8) && int32(left) > int32(right)) ||
            ((r & 4) && left == right) || ((r & 2) && left < right) || ((r & 1) && left > right))
            throw romaw_ppc_arch_fault{0x700, 0x20000, 0};
        break;
    }
    case operation::system_call:
        throw romaw_ppc_arch_fault{0xC00, 0, 0};
    case operation::rfi: {
        ROMlib_ppc_registers registers = {};
        registers.pc = cpu->romaw_registers().pc;
        ROMlib_ppc_750_rfi(romaw_ppc_environment->system, &registers);
        cpu->romaw_registers().pc = registers.pc;
        return;
    }
    case operation::external: {
        uint32 ea = (a ? cpu->gpr(a) : 0) + cpu->gpr(b);
        uint32 value = romaw_ppc_external(ea, xo == 438, cpu->gpr(r));
        if (xo == 310)
            cpu->gpr(r) = value;
        break;
    }
    default:
        illegal();
    }
    cpu->romaw_registers().pc += 4;
}

bool control(powerpc_cpu* cpu, uint32 opcode)
{
    using operation = romaw_ppc_system_operation;
    auto decoded = romaw_ppc_decode_system_operation(opcode);
    if (decoded != operation::other) {
        if (decoded == operation::invalid)
            illegal();
        if (decoded == operation::rfi)
            supervisor();
        if (!romaw_ppc_native_system_step(cpu, &cpu->romaw_registers().pc, opcode))
            execute_system_operation(cpu, opcode);
        return true;
    }
    unsigned primary = opcode >> 26, xo = (opcode >> 1) & 1023;
    if (primary != 31)
        return false;
    if (xo == 339 || xo == 467 || xo == 371) {
        spr(cpu, opcode);
        return true;
    }
    if (uint32 mask = romaw_ppc_cache_reserved_mask(xo)) {
        if (opcode & mask)
            illegal();
        if (xo == 470)
            supervisor();
        if (!romaw_ppc_native_system_step(cpu, &cpu->romaw_registers().pc, opcode))
            execute_cache_control(cpu, opcode);
        return true;
    }
    if (uint32 mask = romaw_ppc_mmu_reserved_mask(xo)) {
        if (opcode & mask)
            illegal();
        supervisor();
        if (register_move(cpu, opcode))
            return true;
        if (!romaw_ppc_native_system_step(cpu, &cpu->romaw_registers().pc, opcode))
            execute_mmu_control(cpu, opcode);
        return true;
    }
    if (xo == 370) // tlbia is not implemented on the 750.
        illegal();
    return false;
}

// Completion classification describes functional execution, not dispatch-port
// occupancy or speculative execution. Branch direction uses the input CR/CTR:
// a taken branch can target the immediately following instruction.
unsigned completion_events(powerpc_cpu* cpu, uint32 opcode, const char* name)
{
    unsigned primary = opcode >> 26, xo = (opcode >> 1) & 1023;
    if (primary == 18)
        return ROMLIB_PPC_750_COMPLETE_BRANCH | ROMLIB_PPC_750_COMPLETE_TAKEN;
    if (primary == 16 || (primary == 19 && (xo == 16 || xo == 528))) {
        unsigned bo = (opcode >> 21) & 31, bi = (opcode >> 16) & 31;
        bool ctr = (bo & 4) || ((cpu->romaw_registers().ctr - 1 != 0) != bool(bo & 2));
        bool condition = (bo & 16) || (bool(cpu->romaw_registers().cr.get() & (0x80000000u >> bi)) == bool(bo & 8));
        return ROMLIB_PPC_750_COMPLETE_BRANCH | (ctr && condition ? ROMLIB_PPC_750_COMPLETE_TAKEN : 0);
    }
    if (primary == 59 || primary == 63)
        return ROMLIB_PPC_750_COMPLETE_FLOATING;
    if ((primary >= 32 && primary <= 55) ||
        (primary == 31 &&
         (!strncmp(name, "lw", 2) || !strncmp(name, "lb", 2) || !strncmp(name, "lh", 2) || !strncmp(name, "lf", 2) ||
          !strncmp(name, "lsw", 3) || !strncmp(name, "st", 2) || xo == 310 || xo == 438))) {
        unsigned result = ROMLIB_PPC_750_COMPLETE_MEMORY;
        if (primary == 31 && xo == 20)
            result |= ROMLIB_PPC_750_COMPLETE_LWARX;
        if (primary == 31 && xo == 150)
            result |= ROMLIB_PPC_750_COMPLETE_STWCX;
        return result;
    }
    if (primary == 17 || primary == 19 ||
        (primary == 31 && (xo == 339 || xo == 467 || xo == 371 || xo == 83 || xo == 146 || xo == 595 || xo == 210 ||
                           xo == 659 || xo == 242 || xo == 598 || xo == 854 || xo == 512 || xo == 19 || xo == 144))) {
        unsigned result = ROMLIB_PPC_750_COMPLETE_SYSTEM;
        if (primary == 31 && xo == 598)
            result |= ROMLIB_PPC_750_COMPLETE_SYNC;
        if (primary == 31 && xo == 854)
            result |= ROMLIB_PPC_750_COMPLETE_EIEIO;
        return result;
    }
    // Cache controls and TLBIE execute through the LSU but are not load/store
    // instructions. TLBSYNC retires without an execution unit (table 6-4).
    if (primary == 31 && (xo == 306 || xo == 566 || xo == 54 || xo == 86 || xo == 470 || xo == 278 || xo == 246 ||
                          xo == 982 || xo == 1014))
        return 0;
    return primary == 19 ? 0 : ROMLIB_PPC_750_COMPLETE_INTEGER;
}

void trace(powerpc_cpu* cpu, uint32 opcode, uint32 old_msr)
{
    // The 750 excludes ISYNC as well as the architectural RFI/SC exclusions.
    if (opcode == 0x4C000064 || opcode == 0x4C00012C)
        return;
    unsigned primary = opcode >> 26, xo = (opcode >> 1) & 1023;
    bool branch = primary == 16 || primary == 18 || (primary == 19 && (xo == 16 || xo == 528));
    if (!(old_msr & 0x400) && !((old_msr & 0x200) && branch))
        return;
    ROMlib_ppc_registers registers = {};
    registers.pc = cpu->romaw_registers().pc;
    ROMlib_ppc_750_exception(romaw_ppc_environment->system, &registers, 0xD00, 0, 0);
    cpu->romaw_registers().pc = registers.pc;
}

bool floating_exception(powerpc_cpu* cpu, uint32 pc, bool next)
{
    auto& state = *romaw_ppc_environment->system;
    if (!(state.mmu.msr & 0x900) || !(cpu->romaw_registers().fpscr & 0x40000000))
        return false;
    // The 750 treats every nonzero FE mode as precise. Arithmetic kernels
    // already inhibit invalid/zero-divide destinations or deliver the required
    // scaled range result. Preserve those results and FPSCR/CR on entry.
    ROMlib_ppc_registers registers = {};
    registers.pc = pc;
    ROMlib_ppc_750_exception(&state, &registers, 0x700, 0x100000 | (next ? 0x10000 : 0), 0);
    cpu->romaw_registers().pc = registers.pc;
    return true;
}
}

void romaw_ppc_system_kernel(powerpc_cpu* cpu, uint32 opcode)
{
    // Called after a generated semantic tail has committed the instruction
    // boundary and suspended native dispatch. Reuse the checked control path.
    if (!control(cpu, opcode))
        cpu->romaw_instruction(opcode)->execute(cpu, opcode);
}

static uint32 alignment_status(uint32 opcode)
{
    // MPCFPE table 6-12, 32-bit form. Copy the encoded operation, data register
    // and rA so a guest alignment handler can emulate without refetching code.
    uint32 status = (opcode >> 16) & 0x3FF;
    if ((opcode >> 26) == 31)
        status |= ((opcode & 6) << 14) | ((opcode & 0x40) << 8) | ((opcode & 0x780) << 3);
    else
        status |= ((opcode >> 12) & 0x4000) | ((opcode >> 17) & 0x3C00);
    return status;
}

void romaw_ppc_system_step(powerpc_cpu* cpu)
{
    auto& env = *romaw_ppc_environment;
    if (env.system->checkstop)
        throw std::runtime_error("PowerPC 750 checkstop");
    if (env.system->tlbsync_wait && !env.system->machine_check_pending && !env.system->bus_check_pending &&
        !env.system->soft_reset_pending) {
        env.waiting_for_tlb = 1;
        throw std::runtime_error("PowerPC 750 waiting for TLBISYNC release");
    }
    if (env.system->power_mode != ROMLIB_PPC_750_RUN) {
        if (env.instruction_limit && env.instructions >= env.instruction_limit)
            throw std::runtime_error("PowerPC instruction limit exceeded");
        ROMlib_ppc_registers suspended = {};
        suspended.pc = cpu->romaw_registers().pc;
        int interrupt = ROMlib_ppc_750_interrupt(env.system, &suspended);
        if (interrupt < 0)
            throw std::runtime_error("PowerPC 750 checkstop");
        if (interrupt) {
            cpu->romaw_registers().pc = suspended.pc;
            romaw_ppc_power_wait(false);
        }
        else
            romaw_ppc_power_wait(true);
        return;
    }
    // The limit/poll boundary remains outside architectural fault handling.
    romaw_ppc_step(cpu->romaw_registers().pc);
    ROMlib_ppc_registers boundary = {};
    boundary.pc = cpu->romaw_registers().pc;
    int interrupt = ROMlib_ppc_750_interrupt(env.system, &boundary);
    if (!interrupt && (env.system->ictc & 1) && romaw_ppc_forward(boundary.pc))
        interrupt = ROMlib_ppc_750_interrupt(env.system, &boundary);
    if (interrupt < 0)
        throw std::runtime_error("PowerPC 750 checkstop");
    if (interrupt) {
        cpu->romaw_registers().pc = boundary.pc;
        return;
    }
    if (floating_exception(cpu, cpu->romaw_registers().pc, true))
        return;
    uint32 instruction_pc = cpu->romaw_registers().pc;
    uint32 old_msr = env.system->mmu.msr;
    uint32 opcode = 0;
    unsigned completed = 0;
    env.system->performance_current_pc = instruction_pc;
    try {
        // Fetch is distinct from a data load: IR, IBAT, NX/G and LE lanes apply.
        uint32 physical;
        ROMlib_ppc_750_performance_event(env.system, ROMLIB_PPC_750_PME_FETCH, 1, instruction_pc);
        opcode = romaw_ppc_fetch(instruction_pc, physical);
        uint32 iabr = env.system->iabr_pending ? env.system->iabr_previous : env.system->iabr;
        if ((iabr & 2) && (iabr & ~3u) == instruction_pc && (iabr & 1) == ((old_msr >> 5) & 1))
            throw romaw_ppc_arch_fault{0x1300, 0, 0};
        const auto* instruction = cpu->romaw_instruction(opcode);
        unsigned primary = opcode >> 26;
        if (primary == 6) {
            if (!romaw_ppc_system_service(instruction_pc, physical, opcode))
                illegal();
            instruction->execute(cpu, opcode);
            return;
        }
        if (romaw_ppc_750_excludes_instruction(opcode, instruction->name))
            illegal();
        // Control operations have their own strict reserved-bit checks below.
        completed = completion_events(cpu, opcode, instruction->name);
        unsigned xo = (opcode >> 1) & 1023;
        if (instruction->mnemo != PPC_I(INVALID) || (completed & ROMLIB_PPC_750_COMPLETE_SYSTEM) || primary == 3 ||
            (primary == 31 && (xo == 4 || xo == 310 || xo == 438 || xo == 306 || xo == 566)))
            ROMlib_ppc_750_performance_event(env.system, ROMLIB_PPC_750_PME_DISPATCHED, 1, instruction_pc);
        if (control(cpu, opcode)) {
            ROMlib_ppc_750_performance_complete(env.system, instruction_pc, old_msr, completed);
            // MTMSR/RFI can enable an existing FPSCR condition. SRR0 then
            // identifies the next instruction and SRR1[15] distinguishes it.
            if (floating_exception(cpu, cpu->romaw_registers().pc, true))
                return;
            trace(cpu, opcode, old_msr);
            return;
        }
        if (instruction->mnemo == PPC_I(INVALID))
            illegal();
        if ((primary >= 48 && primary <= 55) || primary == 59 || primary == 63 ||
            (primary == 31 && instruction->name[0] &&
             (strstr(instruction->name, "lf") == instruction->name ||
              strstr(instruction->name, "stf") == instruction->name))) {
            if (!(env.system->mmu.msr & 0x2000))
                throw romaw_ppc_arch_fault{0x800, 0, 0};
        }
        const char* name = instruction->name;
        bool floating_memory = (primary >= 48 && primary <= 55) ||
                               (primary == 31 && (!strncmp(name, "lf", 2) || !strncmp(name, "stf", 3)));
        bool multiple = primary == 46 || primary == 47;
        bool string = primary == 31 && (!strncmp(name, "lsw", 3) || !strncmp(name, "stsw", 4));
        bool reserve = primary == 31 && (!strcmp(name, "lwarx") || !strcmp(name, "stwcx."));
        if (floating_memory || multiple || string || reserve) {
            unsigned a = (opcode >> 16) & 31;
            bool string_immediate = !strcmp(name, "lswi") || !strcmp(name, "stswi");
            uint32 ea = (a ? cpu->gpr(a) : 0) + (primary == 31 ? (string_immediate ? 0 : cpu->gpr((opcode >> 11) & 31))
                                                               : uint32(int32(int16(opcode))));
            if ((floating_memory && (ea & 3)) || ((multiple || reserve) && (ea & 3)) ||
                ((multiple || string) && (env.system->mmu.msr & 1)))
                throw romaw_ppc_arch_fault{0x600, 0, ea};
        }
        bool native = romaw_ppc_native_system_step(cpu, &cpu->romaw_registers().pc, opcode);
        if (!native)
            instruction->execute(cpu, opcode);
        if (!native && opcode == 0x4C00012C)
            env.system->iabr_pending = 0;
        if ((primary == 59 || primary == 63) && floating_exception(cpu, instruction_pc, false))
            return;
        if ((completed & ROMLIB_PPC_750_COMPLETE_STWCX) && (cpu->romaw_registers().cr.get() & 0x20000000))
            completed |= ROMLIB_PPC_750_COMPLETE_STWCX_SUCCESS;
        ROMlib_ppc_750_performance_complete(env.system, instruction_pc, old_msr, completed);
        trace(cpu, opcode, old_msr);
    }
    catch (const romaw_ppc_arch_fault& fault) {
        if (fault.vector == 0xC00)
            ROMlib_ppc_750_performance_complete(env.system, instruction_pc, old_msr, completed);
        ROMlib_ppc_registers registers = {};
        registers.pc = instruction_pc;
        uint32 cause = fault.vector == 0x600 ? alignment_status(opcode) : fault.cause;
        if (!ROMlib_ppc_750_exception(env.system, &registers, fault.vector, cause, fault.address))
            throw std::runtime_error("PowerPC 750 checkstop");
        cpu->romaw_registers().pc = registers.pc;
    }
}
