/* SPDX-License-Identifier: MIT */
/* FRESTORE consumes a captured frame image; resumption must not sample it again. */
#define main unused_mmu_fpu_fixture
#include "mmu040-fpu-test.c"
#undef main
#include "mapped-cpu.h"
#include "native040.h"
#include "jit-x64.h"
#include <stdlib.h>
#ifdef VERIFY_NATIVE_RESTORE_IMAGE
#include "fpu-captured.h"

static unsigned complete_captures;
static int expect_direct_restore;
int __real_syn68k_fpu_finish_frame(const Syn68kFPUFrame*, Syn68kMMU040Memory*, Syn68kFPUStep*);
int __wrap_syn68k_fpu_finish_frame(const Syn68kFPUFrame* captured, Syn68kMMU040Memory* memory, Syn68kFPUStep* result)
{
    assert(!expect_direct_restore);
    if ((captured->opcode & 0xffc0) == 0xf340 &&
        (captured->header == 0x41300000 || captured->header == 0x41600000)) {
        unsigned expected = captured->header == 0x41300000 ? 52 : 100;
        assert(captured->image && captured->image_size == expected);
        ++complete_captures;
    }
    return __real_syn68k_fpu_finish_frame(captured, memory, result);
}
#endif

static unsigned visits[25], word_address[25], words, callback_mode, fail_word;
static unsigned source_attempts, duplicate_faults;
static Syn68kMMU040Native native_binding;

static int captured_read(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                         unsigned fc)
{
    for (unsigned word = 0; word < words; ++word)
        if (address == word_address[word]) {
            assert(size == 4 && fc == 5);
            ++source_attempts;
            unsigned visit = ++visits[word];
            if (word == fail_word || (visit > 1 && (callback_mode == 2 || (callback_mode == 3 && word >= 4)))) {
                if (visit > 1)
                    ++duplicate_faults;
                memset(bytes, 0xee, size);
                return 0;
            }
            if (visit > 1 && callback_mode == 1) {
                memset(bytes, 0, size);
                return 1;
            }
            break;
        }
    return operand_read(opaque, address, bytes, size, attributes, fc);
}

static void image_long(unsigned char* image, unsigned offset, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        image[offset + i] = value >> (24 - i * 8);
}

/* Null, idle, software/unimplemented, E1 short, E3 busy, E1 busy,
   simultaneous E1/E3 and the restored busy conversion marker. */
static unsigned make_image(unsigned kind, unsigned char image[100])
{
    memset(image, 0, 100);
    if (kind < 2) {
        image_long(image, 0, kind ? 0x41000000 : 0);
        return 4;
    }
    unsigned size = kind < 4 ? 52 : 100;
    image_long(image, 0, size == 52 ? 0x41300000 : 0x41600000);
    image_long(image, size - 36, (kind == 2 ? 0x5c00u : 0x0022u) << 16);
    image_long(image, size - 28, (kind == 2 ? 0 : kind == 4 || kind == 7 ? 2 : kind == 6 ? 6 : 4) << 24);
    image_long(image, size - 24, 0x40000000);
    image_long(image, size - 20, 0x80000000); /* FPTEMP = 2. */
    image_long(image, size - 12, 0x3fff0000);
    image_long(image, size - 8, 0x80000000); /* ETEMP = 1. */
    if (size == 100) {
        image_long(image, 0x18, 0x3fff0000);
        image_long(image, 0x1c, 0x80000000); /* WBTEMP = 1. */
        image_long(image, 0x28, 0x13572468);
        image_long(image, 0x34, 0x00120000); /* FADD writeback operation, FP0. */
        if (kind == 7)
            image[8] = 0xfe;
    }
    return size;
}

static Syn68kMappedCPU setup(unsigned config, unsigned page, unsigned kind, unsigned frame, unsigned postincrement,
                             unsigned old_busy, unsigned fpcr)
{
    memory.native = NULL;
    memory.cache = NULL;
    fixture(page, 1);
    memory.read = captured_read;
    for (unsigned reg = 0; reg < 16; ++reg)
        EM_DREG(reg) = 0x11220000u + reg * 0x101;
    EM_A0 = frame;
    EM_A7 = cpu_state.isp = 0x201c0;
    cpu_state.vbr = 0x20000;
    cpu_state.fpu.fpcr = fpcr;
    cpu_state.fpu.fpsr = 0;
    cpu_state.fpu.fpiar = 0x11223344;
    cpu_state.fpu.regs[0].exponent = 0x4002;
    cpu_state.fpu.regs[0].significand = UINT64_C(0x9000000000000000); /* Old FP0 = 9. */
    if (old_busy) {
        cpu_state.fpu.null_state = 0;
        cpu_state.fpu.frame_size = 100;
        cpu_state.fpu.pending_vector = 49;
        memset(cpu_state.fpu.frame, 0x6d, sizeof cpu_state.fpu.frame);
        cpu_state.fpu.frame[0] = 0x41;
        cpu_state.fpu.frame[1] = 0x60;
    }
    unsigned char image[100];
    unsigned size = make_image(kind, image);
    words = size / 4;
    for (unsigned i = 0; i < size; ++i)
        ram[physical(frame + i)] = image[i];
    for (unsigned i = 0; i < words; ++i)
        word_address[i] = physical(frame + i * 4);
    memset(visits, 0, sizeof visits);
    source_attempts = duplicate_faults = callback_mode = 0;
    fail_word = UINT32_MAX;
    code_word(0x10000, postincrement ? 0xf358 : 0xf350);
    if (config & 2)
        assert(syn68k_native040_memory(&memory, &native_binding, (Syn68kNative040RAM){0}));
    return (Syn68kMappedCPU){.pc = 0x10000, .memory = &memory, .native = config & 1};
}

static void expect_registers(const CPUState* before, unsigned frame, unsigned size, unsigned postincrement)
{
    for (unsigned reg = 0; reg < 16; ++reg)
        assert(EM_DREG(reg) == (reg == 8 ? frame + (postincrement ? size : 0) : before->regs[reg].ul.n));
    assert(cpu_state.sr == before->sr && cpu_state.ccc == before->ccc && cpu_state.ccv == before->ccv &&
           cpu_state.ccnz == before->ccnz && cpu_state.ccn == before->ccn && cpu_state.ccx == before->ccx);
}

static void expect_value(unsigned kind)
{
    if (kind == 0) {
        assert(cpu_state.fpu.null_state && !cpu_state.fpu.frame_size && !cpu_state.fpu.pending_vector);
        return;
    }
    assert(!cpu_state.fpu.null_state);
    if (kind == 2) {
        assert(cpu_state.fpu.frame_size == 52 && cpu_state.fpu.pending_vector == 11);
    }
    else {
        assert(!cpu_state.fpu.frame_size && !cpu_state.fpu.pending_vector);
    }
    unsigned exponent = kind == 1 || kind == 2 ? 0x4002 : kind == 4 ? 0x3fff : 0x4000;
    uint64_t significand = kind == 1 || kind == 2 ? UINT64_C(0x9000000000000000)
                           : kind == 4            ? UINT64_C(0x8000000000000000)
                                                  : UINT64_C(0xc000000000000000);
    assert(cpu_state.fpu.regs[0].exponent == exponent && cpu_state.fpu.regs[0].significand == significand);
}

static void expect_once(unsigned consumed)
{
    assert(source_attempts == consumed && !duplicate_faults);
    for (unsigned i = 0; i < words; ++i)
        assert(visits[i] == (i < consumed));
}

static void capture_matrix(void)
{
    for (unsigned config = 0; config < 4; ++config)
        for (unsigned page = 0; page < 2; ++page)
            for (unsigned post = 0; post < 2; ++post)
                for (unsigned old = 0; old < 2; ++old)
                    for (unsigned kind = 0; kind < 8; ++kind)
                        for (unsigned precision = 0; precision < 3; ++precision)
                            for (unsigned rounding = 0; rounding < 4; ++rounding)
                                for (unsigned placement = 0; placement < 3; ++placement) {
                                    unsigned char image[100];
                                    unsigned size = make_image(kind, image), page_bytes = page ? 8192 : 4096;
                                    unsigned boundary = 0x20000 + page_bytes;
                                    unsigned frame = placement == 0   ? 0x20040
                                                     : placement == 1 ? boundary - 4
                                                                      : boundary - size + 4;
                                    unsigned fpcr = precision * 0x40 + rounding * 0x10;
                                    Syn68kMappedCPU cpu = setup(0, page, kind, frame, post, old, fpcr);
                                    CPUState before = cpu_state;
                                    Syn68kMappedRun run;
                                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                    assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10002);
                                    expect_value(kind);
                                    Syn68kFPUState expected = cpu_state.fpu;
                                    expect_registers(&before, frame, size, post);
                                    for (unsigned variation = 0; variation < 4; ++variation) {
                                        cpu = setup(config, page, kind, frame, post, old, fpcr);
                                        before = cpu_state;
                                        callback_mode = variation == 3 ? 0 : variation + 1;
#ifdef VERIFY_NATIVE_RESTORE_IMAGE
                                        expect_direct_restore = config & 1;
#endif
                                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
#ifdef VERIFY_NATIVE_RESTORE_IMAGE
                                        expect_direct_restore = 0;
#endif
                                        if (run.reason != SYN68K_MAPPED_BUDGET ||
                                            memcmp(&cpu_state.fpu, &expected, sizeof expected))
                                            fprintf(
                                                stderr,
                                                "capture config=%u page=%u post=%u old=%u kind=%u fpcr=%x place=%u mode=%u reason=%u vector=%u reads=%u\n",
                                                config, page, post, old, kind, fpcr, placement, callback_mode,
                                                run.reason, run.step.vector, source_attempts);
                                        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10002 &&
                                               !memcmp(&cpu_state.fpu, &expected, sizeof expected));
                                        expect_value(kind);
                                        expect_registers(&before, frame, size, post);
                                        expect_once(size / 4);
                                        ++cases;
                                    }
                                }
}

static void fault_matrix(void)
{
    for (unsigned config = 0; config < 4; ++config)
        for (unsigned page = 0; page < 2; ++page)
            for (unsigned post = 0; post < 2; ++post)
                for (unsigned old = 0; old < 2; ++old)
                    for (unsigned kind = 0; kind < 8; ++kind) {
                        unsigned char image[100];
                        unsigned size = make_image(kind, image), count = size / 4;
                        unsigned boundary = 0x20000 + (page ? 8192 : 4096);
                        for (unsigned split = 0; split <= count; ++split)
                            for (unsigned bad = 0; bad < count; ++bad) {
                                unsigned frame = boundary - split * 4;
                                Syn68kMappedCPU cpu = setup(config, page, kind, frame, post, old, 0xa0);
                                CPUState before = cpu_state;
                                fail_word = bad;
                                Syn68kMappedRun run;
                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                assert(
                                    run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                    cpu.pc == 0x10000 && !run.completed && run.step.access.request == frame + bad * 4 &&
                                    run.step.access.logical == frame + bad * 4 && run.step.access.size == 4 &&
                                    run.step.access.fc == 5 && !run.step.access.completed && run.step.access.bus_error);
                                assert(!memcmp(&cpu_state.fpu, &before.fpu, sizeof before.fpu));
                                expect_registers(&before, frame, size, 0);
                                expect_once(bad + 1);
                                ++cases;
                            }
                        for (unsigned split = 0; split < count; ++split) {
                            unsigned frame = boundary - split * 4;
                            Syn68kMappedCPU cpu = setup(config, page, kind, frame, post, old, 0xa0);
                            CPUState before = cpu_state;
                            put32(pte(boundary), 0);
                            Syn68kMappedRun run;
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                            assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && cpu.pc == 0x10000 &&
                                   !run.completed && run.step.access.request == boundary &&
                                   run.step.access.logical == boundary && run.step.access.size == 4 &&
                                   run.step.access.fc == 5 && !run.step.access.completed &&
                                   !run.step.access.bus_error && run.step.access.translation.fault == MMU040_INVALID);
                            assert(!memcmp(&cpu_state.fpu, &before.fpu, sizeof before.fpu));
                            expect_registers(&before, frame, size, 0);
                            expect_once(split);
                            ++cases;
                        }
                    }
}

static void guest_roundtrips(void)
{
    for (unsigned config = 0; config < 4; ++config)
        for (unsigned page = 0; page < 2; ++page)
            for (unsigned post = 0; post < 2; ++post)
                for (unsigned old = 0; old < 2; ++old)
                    for (unsigned kind = 2; kind < 8; ++kind)
                        for (unsigned atc = 0; atc < 2; ++atc)
                            for (unsigned position = 0; position < 3; ++position) {
                                unsigned char image[100];
                                unsigned size = make_image(kind, image), count = size / 4;
                                unsigned boundary = 0x20000 + (page ? 8192 : 4096);
                                unsigned bad = position == 0 ? 1 : position == 1 ? count / 2 : count - 1;
                                unsigned frame = boundary - bad * 4;
                                Syn68kMappedCPU cpu = setup(0, page, kind, frame, post, old, 0x90);
                                Syn68kMappedRun run;
                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                assert(run.reason == SYN68K_MAPPED_BUDGET);
                                Syn68kFPUState expected = cpu_state.fpu;
                                cpu = setup(config, page, kind, frame, post, old, 0x90);
                                unsigned source_pte = pte(boundary);
                                if (atc)
                                    put32(source_pte, 0);
                                else
                                    fail_word = bad;
                                CPUState before = cpu_state;
                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                       run.step.restartable_read &&
                                       !memcmp(&cpu_state.fpu, &before.fpu, sizeof before.fpu));
                                fail_word = UINT32_MAX;
                                put32(physical(cpu_state.vbr + 8), 0x10080);
                                unsigned steps = 1;
                                if (atc) {
                                    unsigned table_page = source_pte & ~((page ? 8192 : 4096) - 1u);
                                    put32(pte(0x30000), table_page | 1);
                                    unsigned target = 0x30000 + source_pte - table_page;
                                    const unsigned repair[] = {0x23fc,         1,      0xc001, target >> 16,
                                                               target & 65535, 0xf518, 0x4e73};
                                    for (unsigned i = 0; i < sizeof repair / sizeof *repair; ++i)
                                        code_word(0x10080 + 2 * i, repair[i]);
                                    steps = 3;
                                }
                                else
                                    code_word(0x10080, 0x4e73);
                                Syn68kMappedDelivery delivery;
                                assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x10080);
                                assert(syn68k_mapped_cpu_run(&cpu, steps, &run));
                                assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10000 &&
                                       !memcmp(&cpu_state.fpu, &before.fpu, sizeof before.fpu));
                                memset(visits, 0, sizeof visits);
                                source_attempts = duplicate_faults = 0;
                                callback_mode = 3; /* A second frame sample would fault during retry. */
                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10002 &&
                                       !memcmp(&cpu_state.fpu, &expected, sizeof expected));
                                expect_registers(&before, frame, size, post);
                                expect_value(kind);
                                expect_once(count);
                                ++cases;
                            }
}

static void quiescent_frame_matrix(void)
{
    for (unsigned page = 0; page < 2; ++page)
        for (unsigned post = 0; post < 2; ++post)
            for (unsigned old = 0; old < 2; ++old)
                for (unsigned split = 0; split <= 25; ++split) {
                    unsigned frame = 0x20000 + (page ? 8192 : 4096) - split * 4;
                    for (unsigned count = 0; count <= 25; ++count) {
                        Syn68kMappedCPU cpu = setup(0, page, 4, frame, post, old, 0xa0);
                        ram[physical(frame + 0x48)] = 0;
                        fail_word = count;
                        Syn68kMappedRun expected;
                        assert(syn68k_mapped_cpu_run(&cpu, 1, &expected));
                        Syn68kFPUState expected_fpu = cpu_state.fpu;
                        for (unsigned config = 1; config < 4; config += 2) {
                            cpu = setup(config, page, 4, frame, post, old, 0xa0);
                            ram[physical(frame + 0x48)] = 0;
                            CPUState before = cpu_state;
                            fail_word = count;
                            callback_mode = 2;
#ifdef VERIFY_NATIVE_RESTORE_IMAGE
                            expect_direct_restore = count == 25;
#endif
                            Syn68kMappedRun actual;
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &actual));
#ifdef VERIFY_NATIVE_RESTORE_IMAGE
                            expect_direct_restore = 0;
#endif
                            assert(actual.reason == expected.reason && actual.completed == expected.completed);
                            assert(!memcmp(&cpu_state.fpu, &expected_fpu, sizeof expected_fpu));
                            expect_registers(&before, frame, 100, post && count == 25);
                            expect_once(count == 25 ? 25 : count + 1);
                            if (count < 25)
                                assert(actual.step.vector == 2 && cpu.pc == 0x10000 &&
                                       actual.step.access.logical == frame + count * 4);
                            else
                                assert(actual.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10002);
                            ++cases;
                        }
                    }
                }
}

static Syn68kMappedCPU short_frame(unsigned config, unsigned command, unsigned flags, unsigned frame)
{
    unsigned page = command & 1, post = !!(command & 2), old = !!(command & 4);
    Syn68kMappedCPU cpu = setup(config, page, 2, frame, post, old, 0xa0);
    ram[physical(frame + 16)] = command >> 8;
    ram[physical(frame + 17)] = command & 255;
    ram[physical(frame + 24)] = flags & 1;
    ram[physical(frame + 25)] = (flags & 2) << 6;
    return cpu;
}

static void short_frame_command_matrix(void)
{
    for (unsigned flags = 0; flags < 4; ++flags)
        for (unsigned command = 0; command < 65536; ++command) {
            unsigned boundary = 0x20000 + ((command & 1) ? 8192 : 4096);
            unsigned frame = boundary - 4 * ((command >> 3) % 14);
            Syn68kMappedCPU cpu = short_frame(0, command, flags, frame);
            Syn68kMappedRun run;
            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
            assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10002);
            Syn68kFPUState expected = cpu_state.fpu;
            cpu = short_frame(command & 8 ? 3 : 1, command, flags, frame);
            CPUState before = cpu_state;
#ifdef VERIFY_NATIVE_RESTORE_IMAGE
            unsigned captures = complete_captures;
            expect_direct_restore = 1;
#endif
            callback_mode = 2;
            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
#ifdef VERIFY_NATIVE_RESTORE_IMAGE
            expect_direct_restore = 0;
            assert(complete_captures == captures);
#endif
            assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10002);
            assert(!memcmp(&cpu_state.fpu, &expected, sizeof expected));
            expect_registers(&before, frame, 52, !!(command & 2));
            expect_once(13);
            ++cases;
        }
}

#ifndef CAPTURE_TEST_ENTRY
#define CAPTURE_TEST_ENTRY main
#endif
static void pending_arithmetic_frames(void)
{
    for (unsigned config = 0; config < 4; ++config)
        for (unsigned shape = 0; shape < 3; ++shape) {
            unsigned size = shape ? 100 : 52, stages = shape == 2 ? 6 : 4;
            unsigned frame = 0x20040;
            Syn68kMappedCPU cpu = setup(config, 0, shape ? 4 : 2, frame, 1, 1, shape == 1 ? 0 : 0x4000);
            ram[physical(frame + size - 36)] = 0;
            ram[physical(frame + size - 35)] = 0x22; /* FADD. */
            ram[physical(frame + size - 28)] = stages;
            cpu_state.fpu.fpsr = shape ? 0x5000 : 0x4000;
            Syn68kFP80 before = cpu_state.fpu.regs[0];
#ifdef VERIFY_NATIVE_RESTORE_IMAGE
            expect_direct_restore = config & 1;
#endif
            Syn68kMappedRun run;
            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
#ifdef VERIFY_NATIVE_RESTORE_IMAGE
            expect_direct_restore = 0;
#endif
            assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10002);
            assert(EM_A0 == frame + size && cpu_state.fpu.frame_size == size);
            assert(cpu_state.fpu.pending_vector == (shape ? 53 : 54));
            assert(cpu_state.fpu.frame[size - 28] == stages);
            assert(cpu_state.fpu.regs[0].exponent == before.exponent &&
                   cpu_state.fpu.regs[0].significand == before.significand);
            ++cases;
        }
}

static void dyadic_replay_operations(void)
{
    const unsigned operations[] = {0x20, 0x22, 0x23, 0x28, 0x24, 0x27};
    for (unsigned i = 0; i < sizeof operations / sizeof *operations; ++i)
        for (unsigned variant = 0; variant < 5; ++variant) {
            unsigned frame = 0x20040;
            int divide_inexact = (variant == 2 || variant == 4) && (operations[i] == 0x20 || operations[i] == 0x24);
            Syn68kFPUState expected;
            for (unsigned config = 0; config < 2; ++config) {
                Syn68kMappedCPU cpu = setup(config, 0, 6, frame, 1, 0, variant == 4 ? 0x0200 : 0);
                ram[physical(frame + 0x41)] = operations[i] | 0x80; /* FP1 differs from the older E3 FP0. */
                if (variant == 1)
                    ram[physical(frame + 0x58)] |= 0x80;
                if (variant == 3)
                    ram[physical(frame + 0x3d)] = 0x80; /* Older E3 rounding sets INEX. */
                if (divide_inexact) {
                    ram[physical(frame + 0x58)] = 0x40;
                    ram[physical(frame + 0x59)] = 0;
                    ram[physical(frame + 0x5c)] = 0xc0; /* ETEMP = 3. */
                }
#ifdef VERIFY_NATIVE_RESTORE_IMAGE
                unsigned captures = complete_captures;
                expect_direct_restore = config && !(variant == 4 && divide_inexact);
#endif
                Syn68kMappedRun run;
                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
#ifdef VERIFY_NATIVE_RESTORE_IMAGE
                expect_direct_restore = 0;
                assert(complete_captures - captures == (unsigned)(config && variant == 4 && divide_inexact));
#endif
                assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10002);
                assert(EM_A0 == frame + 100);
                if (divide_inexact || variant == 3) {
                    assert((cpu_state.fpu.fpsr & 0x0208) == 0x0208);
                    assert(cpu_state.fpu.pending_vector == (variant == 4 ? 49 : 0));
                }
                if (!config)
                    expected = cpu_state.fpu;
                else
                    assert(!memcmp(&cpu_state.fpu, &expected, sizeof expected));
                ++cases;
            }
        }
}

int CAPTURE_TEST_ENTRY(int argc, char** argv)
{
    assert(argc == 1 || argc == 3);
#ifdef CAPTURE_PROTOTYPE
    assert(argc == 1);
#endif
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 3 ? argv[1] : ""));
    uint64_t compiled = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
    capture_matrix();
    fault_matrix();
    guest_roundtrips();
    quiescent_frame_matrix();
    short_frame_command_matrix();
    pending_arithmetic_frames();
    dyadic_replay_operations();
    uint64_t emitted = syn68k_native_compile_count() - compiled, loaded = syn68k_aot_hit_count() - hits;
    if (argc == 3)
        assert(atoi(argv[2]) ? !emitted && loaded : emitted);
    printf("%u FRESTORE captured-frame/volatile-read/body-fault/ATC/state cases passed; emitted=%llu hits=%llu\n",
           cases, (unsigned long long)emitted, (unsigned long long)loaded);
#ifdef VERIFY_NATIVE_RESTORE_IMAGE
    printf("%u complete native FRESTORE images handed off\n", complete_captures);
#endif
    return 0;
}
