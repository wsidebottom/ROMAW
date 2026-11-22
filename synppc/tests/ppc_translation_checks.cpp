/*+===================================================================
  File:      ppc_translation_checks.cpp

  Summary:   Run PPC native emitter coverage and persistent translation cache checks in one executable.

  Classes:   Uses the private native emitter and translation-cache fixture.

  Functions: main; calls ppc_check_native_kernels and ppc_check_translation_cache.

  Origin:    ARDI Executor persistent PPC native-code cache regression fixture.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#include <cassert>
static unsigned failure, allocations, releases;
static auto real_allocate = VirtualAlloc;
static auto real_protect = VirtualProtect;
static auto real_flush = FlushInstructionCache;
static auto real_release = VirtualFree;
static auto real_commit = _commit;

static void* WINAPI test_allocate(void* address, SIZE_T size, DWORD kind, DWORD protection)
{
    ++allocations;
    assert(!address && size && protection == PAGE_READWRITE);
    if (failure == 1 || (failure == 5 && allocations == 2) || (failure == 6 && allocations == 3))
        return nullptr;
    return real_allocate(address, size, kind, protection);
}

static BOOL WINAPI test_protect(void* address, SIZE_T size, DWORD protection, DWORD* previous)
{
    assert(protection == PAGE_EXECUTE_READ);
    return failure == 2 ? FALSE : real_protect(address, size, protection, previous);
}

static BOOL WINAPI test_flush(HANDLE process, const void* address, SIZE_T size)
{
    return failure == 3 ? FALSE : real_flush(process, address, size);
}

static BOOL WINAPI test_release(void* address, SIZE_T size, DWORD kind)
{
    ++releases;
    return real_release(address, size, kind);
}

static int test_commit(int descriptor)
{
    return failure == 4 ? -1 : real_commit(descriptor);
}

#define VirtualAlloc test_allocate
#define VirtualProtect test_protect
#define FlushInstructionCache test_flush
#define VirtualFree test_release
#define _commit test_commit
#endif
#include "../src/ppc-jit.cpp"
#include <cassert>

#if defined(_WIN32)
static std::vector<unsigned char> read_file(const std::string& path)
{
    std::unique_ptr<FILE, decltype(&fclose)> file(cache_open(path.c_str(), false), fclose);
    assert(file);
    std::vector<unsigned char> bytes;
    for (int c; (c = fgetc(file.get())) != EOF;)
        bytes.push_back(c);
    assert(!ferror(file.get()));
    return bytes;
}

static void write_file(const std::string& path, const std::vector<unsigned char>& bytes)
{
    FILE* file = cache_open(path.c_str(), true);
    assert(file && fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size() && !fclose(file));
}

static void add(native_store& store, powerpc_cpu& cpu, uint32 pc, uint32 opcode, bool architectural = false,
                unsigned words = 1)
{
    std::unique_ptr<native_block> block(new native_block);
    block->architectural = architectural;
    block->words.assign(words, opcode);
    assert(compile(*block, &cpu, pc));
    (architectural ? store.system_blocks : store.blocks)
        .emplace(translation_key(pc, opcode, architectural), std::move(block));
}

static void save_oversized_cache(const std::string& path, powerpc_cpu& cpu)
{
    auto* source = ROMlib_ppc_translation_cache_create();
    auto* target = ROMlib_ppc_translation_cache_create();
    assert(source && target);
    auto& store = *static_cast<native_store*>(source->implementation);
    const uint32 start = 0x10000, stride = 32 * 4;
    const uint32 system_pc = 0x80000000, small_pc = 0x80000100;
    const auto system_key = translation_key(system_pc, 0x80640000, true);
    const uint32 stmw = 0xBC030000; // STMW r0,0(r3).
    add(store, cpu, start, stmw, false, 32);
    add(store, cpu, system_pc, 0x80640000, true);
    add(store, cpu, small_pc, 0x38630001);
    const size_t large_body = store.blocks.at(start)->code_size;
    const size_t system_body = store.system_blocks.at(system_key)->code_size;
    const size_t small_body = store.blocks.at(small_pc)->code_size;
    const unsigned large_blocks = (cache_byte_limit - 84) / (28 + stride + large_body) + 2;
    const size_t metadata = 84 + (28 + stride) * large_blocks + 2 * (28 + 4);
    const size_t available = cache_byte_limit - metadata - system_body;
    const unsigned retained = available / large_body;
    assert(retained >= 2 && retained + 2 <= large_blocks && large_blocks + 2 <= store.limit);
    for (unsigned i = 1; i < large_blocks; ++i)
        add(store, cpu, start + i * stride, stmw, false, 32);
    const uint32 hot_pc = start + (large_blocks - 1) * stride;
    store.system_blocks.at(system_key)->last_used = ++store.clock;
    store.blocks.at(hot_pc)->last_used = ++store.clock;
    auto* cold = store.blocks.at(start + retained * stride).get();
    const unsigned native_allocations = allocations, native_releases = releases;

    assert(ROMlib_ppc_translation_cache_save(source, path.c_str()));
    assert(store.size() == large_blocks + 2 && store.blocks.at(start + retained * stride).get() == cold);
    assert(store.clock == 2 && allocations == native_allocations && releases == native_releases);
    bool small_fits = small_body <= available % large_body;
    unsigned saved_count = ROMlib_ppc_translation_cache_load(target, path.c_str());
    assert(saved_count == retained + 1 + unsigned(small_fits));
    auto& saved = *static_cast<native_store*>(target->implementation);
    assert(saved.system_blocks.count(system_key) && saved.blocks.count(hot_pc));
    for (unsigned i = 0; i + 1 < large_blocks; ++i)
        assert(saved.blocks.count(start + i * stride) == unsigned(i < retained - 1));
    assert(saved.blocks.count(small_pc) == unsigned(small_fits));
    printf("Oversized PPC AOT cache reloaded %u of %zu resident blocks within 32 MiB\n", saved_count, store.size());

    // The section extends beyond the original capped scan.
    saved.limit = saved_count + large_blocks - retained;
    std::vector<unsigned char> section((large_blocks + 2) * stride);
    for (size_t offset = 0; offset < section.size(); offset += 4) {
        section[offset] = stmw >> 24;
        section[offset + 1] = (stmw >> 16) & 255;
    }
    romaw_ppc_code_range range = {section.data(), start, static_cast<uint32>(section.size())};
    uint32 omitted = start + (retained - 1) * stride;
    uint32 other_omitted = start + retained * stride;
    const unsigned warm_allocations = allocations;
    assert(!romaw_ppc_translate_image(target, &range, 1, &hot_pc, 1, 0));
    assert(allocations == warm_allocations && !saved.blocks.count(omitted) && !saved.blocks.count(other_omitted));

    const std::string warm_path = path + ".warm";
    assert(ROMlib_ppc_translation_cache_save(target, warm_path.c_str()));
    assert(ROMlib_ppc_translation_cache_load(target, warm_path.c_str()) == saved_count);
    assert(!romaw_ppc_translate_image(target, &range, 1, &hot_pc, 1, 0));
    // Changed code in a late link remains eligible, even at an omitted start.
    range.size = large_blocks * stride;
    section[omitted - start + 3] = 4;
    ROMlib_ppc_environment env = {};
    env.execution_flags = ROMLIB_PPC_EXEC_JIT;
    env.translation_cache = target;
    {
        romaw_ppc_jit_scope scope(&env);
        romaw_ppc_jit_service_scope service;
        assert(romaw_ppc_translate_image(target, &range, 1, nullptr, 0, 0) == 1);
        assert(saved.blocks.at(omitted)->words[0] == (stmw | 4) && !saved.blocks.count(other_omitted));
    }
    // An explicit entry point still gets native code at its original source.
    assert(romaw_ppc_translate_image(target, &range, 1, &other_omitted, 1, 0) == 1);
    assert(saved.blocks.count(other_omitted));
    assert(!_wremove(cache_wide(warm_path.c_str()).c_str()));
    puts("Warm AOT skips unchanged omitted code and prepares changed code and entry points");
    ROMlib_ppc_translation_cache_destroy(source);
    ROMlib_ppc_translation_cache_destroy(target);
}

static void replace_rewritten_flat_code()
{
    constexpr uint32 pc = 0x200;
    for (unsigned mode : {0u, 1u, 2u, 3u}) {
        auto* cache = ROMlib_ppc_translation_cache_create();
        assert(cache);
        auto& store = *static_cast<native_store*>(cache->implementation);
        store.limit = 1;
        unsigned char ram[] = {0x38, 0x63, 0x00, 0x01, 0x38, 0x63, 0x00, 0x02,
                               0x4E, 0x80, 0x00, 0x20, 0x18, 0x00, 0x00, 0x00};
        ROMlib_ppc_environment env = {};
        env.memory = [](void* opaque, uint32_t address, size_t size, int) -> void* {
            uint32 offset = address - pc;
            return offset <= sizeof ram && size <= sizeof ram - offset ? static_cast<unsigned char*>(opaque) + offset
                                                                       : nullptr;
        };
        env.opaque = ram;
        env.linear_memory = ram;
        env.linear_address = pc;
        env.linear_size = sizeof ram;
        env.execution_flags = ROMLIB_PPC_EXEC_JIT;
        env.translation_cache = cache;
        auto run = [&](unsigned expected) {
            ROMlib_ppc_registers registers = {};
            registers.pc = pc;
            registers.lr = pc + 12;
            assert(ROMlib_ppc_execute(&registers, &env) && registers.gpr[3] == expected);
        };
        assert(ROMlib_ppc_translate_range(cache, ram, pc, sizeof ram) == 1);
        auto* old = store.blocks.at(pc).get();
        run(3);
        assert(env.native_instructions == 3 && !env.native_compilations);

        // Change a later word while preserving the first opcode and block address.
        ram[7] = 4;
        failure = mode;
        allocations = releases = 0;
        for (unsigned hit = 1; hit < hot_threshold; ++hit) {
            run(5);
            assert(!allocations && !env.native_instructions && store.blocks.at(pc).get() == old);
        }
        run(5);
        assert(allocations == 1 && releases == unsigned(mode != 1) && store.size() == 1);
        if (mode) {
            assert(!env.native_compilations && !env.native_instructions && store.blocks.at(pc).get() == old);
            ram[7] = 2;
            run(3);
            assert(allocations == 1 && env.native_instructions == 3 && env.native_cache_hits == 1 &&
                   !env.native_compilations);
        }
        else
            assert(env.native_compilations == 1 && env.native_instructions == 3 &&
                   store.blocks.at(pc)->words[1] == 0x38630004);
        failure = 0;
        ROMlib_ppc_translation_cache_destroy(cache);
    }
    puts("Flat PPC JIT rewrites preserve the previous executable block through publication failures");
}

static void replace_full_views(powerpc_cpu& cpu)
{
    auto* cache = ROMlib_ppc_translation_cache_create();
    assert(cache);
    auto& store = *static_cast<native_store*>(cache->implementation);
    constexpr uint32 pc = 0x100;
    constexpr uint32 cold = 0x38630001, hot = 0x38630002, new_view = 0x38630003;
    auto cold_key = translation_key(pc, cold, true);
    auto hot_key = translation_key(pc, hot, true);
    auto new_key = translation_key(pc, new_view, true);
    store.limit = 2;
    add(store, cpu, pc, cold, true);
    add(store, cpu, pc, hot, true);
    store.system_blocks.at(hot_key)->last_used = ++store.clock;
    const unsigned char source[] = {0x38, 0x63, 0x00, 0x03};
    const unsigned char invalid[4] = {};
    assert(!ROMlib_ppc_translate_range_mode(cache, invalid, pc, sizeof invalid, ROMLIB_PPC_TRANSLATE_750));
    assert(store.size() == 2);
    for (unsigned mode : {1u, 2u, 3u}) {
        failure = mode;
        assert(!ROMlib_ppc_translate_range_mode(cache, source, pc, sizeof source, ROMLIB_PPC_TRANSLATE_750));
        assert(store.size() == 2 && store.system_blocks.count(cold_key) && store.system_blocks.count(hot_key));
    }
    failure = 0;
    assert(ROMlib_ppc_translate_range_mode(cache, source, pc, sizeof source, ROMLIB_PPC_TRANSLATE_750) == 1);
    assert(store.size() == 2 && store.system_blocks.count(new_key) && store.system_blocks.count(hot_key));
    assert(!ROMlib_ppc_translate_range_mode(cache, source, pc, sizeof source, ROMLIB_PPC_TRANSLATE_750));

    // A late image may replace an unused opcode at this same effective PC.
    const unsigned char linked[] = {0x38, 0x63, 0x00, 0x04};
    const romaw_ppc_code_range range = {linked, pc, sizeof linked};
    auto linked_key = translation_key(pc, 0x38630004, true);
    ROMlib_ppc_environment env = {};
    env.execution_flags = ROMLIB_PPC_EXEC_JIT;
    env.translation_cache = cache;
    {
        romaw_ppc_jit_scope scope(&env);
        romaw_ppc_jit_service_scope service;
        assert(romaw_ppc_translate_image(cache, &range, 1, nullptr, 0, ROMLIB_PPC_TRANSLATE_750) == 1);
        assert(store.size() == 2 && store.system_blocks.count(linked_key) && store.system_blocks.count(hot_key));
        assert(!romaw_ppc_translate_image(cache, &range, 1, &pc, 1, ROMLIB_PPC_TRANSLATE_750));
    }
    ROMlib_ppc_translation_cache_destroy(cache);
}

static void prepare_published_words(powerpc_cpu& cpu)
{
    auto* cache = ROMlib_ppc_translation_cache_create();
    auto* other = ROMlib_ppc_translation_cache_create();
    assert(cache && other);
    auto& store = *static_cast<native_store*>(cache->implementation);
    store.limit = 3;
    const ROMlib_ppc_published_word words[] = {
        {0x100, 0x38630001},
        {0x100, 0x38630002},
        {0x100, 0x38630001},
        {0x104, 0x4E800020},
    };
    auto old = translation_key(words[1].address, words[1].opcode, true);
    auto newer = translation_key(words[0].address, words[0].opcode, true);
    add(store, cpu, 0x100, words[1].opcode, true);
    add(store, cpu, 0x200, 0x38630004);
    add(store, cpu, 0x300, 0x38630005);
    auto prepare = [&]() { return ROMlib_ppc_translate_published_750(cache, words, 4); };
    assert(!prepare());
    ROMlib_ppc_environment env = {};
    env.execution_flags = ROMLIB_PPC_EXEC_JIT;
    env.translation_cache = cache;
    {
        romaw_ppc_jit_scope scope(&env);
        assert(!prepare());
        romaw_ppc_jit_service_scope service;
        assert(!ROMlib_ppc_translate_published_750(other, words, 4));
        for (unsigned mode : {1u, 2u, 3u}) {
            failure = mode;
            assert(!prepare() && store.size() == 3 && store.blocks.size() == 2 && store.system_blocks.count(old));
        }
        failure = 0;
        assert(prepare() == 1 && store.size() == 3 && store.system_blocks.size() == 2);
        assert(store.system_blocks.count(old) && store.system_blocks.count(newer));
        assert(!prepare()); // Duplicate source and the fourth candidate do not compile.
        const ROMlib_ppc_published_word malformed[] = {{0x101, 0x38630001}, {0x104, 0x18000000}, {0x108, 0}};
        assert(!ROMlib_ppc_translate_published_750(cache, malformed, 3) && store.size() == 3);
    }
    ROMlib_ppc_translation_cache_destroy(other);
    ROMlib_ppc_translation_cache_destroy(cache);
}

static void prepare_linked_branches(const std::string& path)
{
    auto* cache = ROMlib_ppc_translation_cache_create();
    assert(cache);
    auto& store = *static_cast<native_store*>(cache->implementation);
    auto code = [](std::initializer_list<uint32> words) {
        std::vector<unsigned char> bytes;
        for (uint32 word : words)
            for (int shift = 24; shift >= 0; shift -= 8)
                bytes.push_back(word >> shift);
        return bytes;
    };
    std::vector<unsigned char> unused(64 * 4);
    for (size_t i = 0; i < unused.size(); i += 4)
        unused[i] = 0x60; // nop
    const auto entry = code({0x48002001, 0x41822802, 0x48000000, 0x60000000});
    const auto fault_return = code({0x4C000064, 0x60000000});
    const auto callee = code({0x38630001, 0x4D820420, 0x4E800421, 0x4E800020, 0x60000000});
    const romaw_ppc_code_range ranges[] = {
        {unused.data(), 0x1000, unsigned(unused.size())},
        {entry.data(), 0x2000, unsigned(entry.size())},
        {fault_return.data(), 0x2800, unsigned(fault_return.size())},
        {callee.data(), 0x4000, unsigned(callee.size())},
    };
    // BL reaches the callee and a return site. The absolute conditional
    // branch reaches RFI or a self-branch. Conditional BCCTR and linked
    // BCTRL can continue, while RFI and BLR have no known successor.
    const uint32 entries[] = {0x2000, 0x2000, 0x4001, 0x9000};
    store.limit = 8;
    auto prepare = [&]() { return romaw_ppc_translate_image(cache, ranges, 4, entries, 4, ROMLIB_PPC_TRANSLATE_750); };
    auto check = [&]() {
        for (const auto& range : ranges)
            for (unsigned i = 0; i < range.size; i += 4) {
                auto pc = range.address + i;
                bool expected = pc == 0x2800 || (pc >= 0x2000 && pc <= 0x2008) || (pc >= 0x4000 && pc <= 0x400C);
                assert(store.system_blocks.count(translation_key(pc, read_word(range.data + i), true)) == expected);
            }
    };
    assert(prepare() == store.limit);
    check();
    assert(ROMlib_ppc_translation_cache_save(cache, path.c_str()));
    assert(ROMlib_ppc_translation_cache_load(cache, path.c_str()) == store.limit);
    const unsigned restored = allocations;
    assert(!prepare() && allocations == restored);
    check();

    store.system_blocks.clear();
    std::vector<unsigned char> long_prefix(12 * 4);
    for (size_t i = 0; i < long_prefix.size(); i += 4)
        long_prefix[i] = 0x60;                                    // nop
    const auto loop = code({0x38630001, 0x4200FFFC, 0x4E800020}); // addi; bdnz back; blr
    long_prefix.insert(long_prefix.end(), loop.begin(), loop.end());
    const uint32 main = 0x5000;
    const romaw_ppc_code_range larger = {long_prefix.data(), main, unsigned(long_prefix.size())};
    assert(romaw_ppc_translate_image(cache, &larger, 1, &main, 1, ROMLIB_PPC_TRANSLATE_750) == store.limit);
    assert(store.size() == store.limit && store.system_blocks.count(translation_key(main, 0x60000000, true)));
    assert(store.system_blocks.at(translation_key(main + 48, 0x38630001, true))->last_used);
    assert(store.system_blocks.at(translation_key(main + 52, 0x4200FFFC, true))->last_used);

    for (bool signed_low : {false, true})
        for (bool matching_register : {false, true}) {
            store.system_blocks.clear();
            store.blocks.clear();
            const auto veneer = code({signed_low ? 0x3D800002u : 0x3D800001u, signed_low ? 0x398C8043u : 0x618C2043u,
                                      matching_register ? 0x7D8903A6u : 0x7D6903A6u, 0x4E800420, 0x60000000});
            auto destination = unused;
            const auto body = code({0x38630007, 0x4E800020});
            memcpy(destination.data() + 0x40, body.data(), body.size());
            uint32 section = signed_low ? 0x18000 : 0x12000;
            const romaw_ppc_code_range regions[] = {
                {unused.data(), 0x1000, unsigned(unused.size())},
                {veneer.data(), main, unsigned(veneer.size())},
                {destination.data(), section, unsigned(destination.size())},
            };
            store.limit = 6;
            auto prepare_veneer = [&] {
                return romaw_ppc_translate_image(cache, regions, 3, &main, 1, ROMLIB_PPC_TRANSLATE_750);
            };
            auto entry_key = translation_key(section + 0x40, 0x38630007, true);
            auto return_key = translation_key(section + 0x44, 0x4E800020, true);
            assert(prepare_veneer() == store.limit);
            assert(store.system_blocks.count(entry_key) == unsigned(matching_register));
            assert(store.system_blocks.count(return_key) == unsigned(matching_register));
            if (signed_low && matching_register) {
                const auto veneer_path = path + ".ctr";
                assert(ROMlib_ppc_translation_cache_save(cache, veneer_path.c_str()));
                store.system_blocks.clear();
                assert(ROMlib_ppc_translation_cache_load(cache, veneer_path.c_str()) == store.limit);
                assert(!prepare_veneer() && store.system_blocks.count(entry_key) &&
                       store.system_blocks.count(return_key));
                assert(!_wremove(cache_wide(veneer_path.c_str()).c_str()));
            }

            // Flat AOT must split the destination span at the target rather
            // than hiding it inside the preceding block.
            store.system_blocks.clear();
            store.limit = 16;
            assert(romaw_ppc_translate_image(cache, regions, 3, &main, 1, 0));
            assert(store.blocks.count(section + 0x40) == unsigned(matching_register));
            assert(store.blocks.at(section)->words.size() == (matching_register ? 16 : 18));
            if (matching_register)
                assert(store.blocks.at(section + 0x40)->words == std::vector<uint32>({0x38630007, 0x4E800020}));
        }
    ROMlib_ppc_translation_cache_destroy(cache);
}
#endif

static void check_active_native_replacement()
{
#if ROMAW_PPC_NATIVE_JIT
    struct observation {
        ROMlib_ppc_translation_cache* cache;
        std::weak_ptr<native_allocation> allocation;
        void* code;
        unsigned callbacks;
    };

    static observation* active;
    auto callback = +[](powerpc_cpu* cpu, native_block*) {
        assert(active && !active->allocation.expired());
        any_register value;
        value.i = 999;
        cpu->set_register(powerpc_registers::CTR, value);
        const unsigned char rewritten[] = {0x38, 0x63, 0x00, 0x07}; // ADDI r3,r3,7.
        assert(ROMlib_ppc_translate_range(active->cache, rewritten, 0x100, sizeof rewritten) == 1);
        auto& store = *static_cast<native_store*>(active->cache->implementation);
        assert(store.blocks.at(0x100)->code != active->code && !active->allocation.expired());
        ++active->callbacks;
    };
    auto* cache = ROMlib_ppc_translation_cache_create();
    assert(cache);
    auto& store = *static_cast<native_store*>(cache->implementation);
    powerpc_cpu cpu;
    std::unique_ptr<native_block> block(new native_block);
    block->words.push_back(0x42000008); // BDNZ +8: retirement must still write CTR after replacement.
    std::vector<unsigned char> bytes;
    emit_native_block(*block, &cpu, 0x100, bytes);
    const uintptr target = reinterpret_cast<uintptr>(callback);
    bytes.pop_back(); // Replace RET with a checked callback and a resumed native suffix.
    bytes.insert(bytes.end(), {0x41, 0x52, 0x41, 0x53, 0x48, 0x83, 0xec});
#if defined(_WIN32)
    bytes.push_back(40);
    bytes.insert(bytes.end(), {0x4c, 0x89, 0xd1, 0x4c, 0x89, 0xda});
#else
    bytes.push_back(8);
    bytes.insert(bytes.end(), {0x4c, 0x89, 0xd7, 0x4c, 0x89, 0xde});
#endif
    bytes.insert(bytes.end(), {0x48, 0xb8});
    emit_word(bytes, target);
    emit_word(bytes, target >> 32);
    bytes.insert(bytes.end(), {0xff, 0xd0, 0x48, 0x83, 0xc4});
#if defined(_WIN32)
    bytes.push_back(40);
#else
    bytes.push_back(8);
#endif
    bytes.insert(bytes.end(), {0x41, 0x5b, 0x41, 0x5a, 0xc3});
    assert(publish_native_block(*block, bytes.data(), bytes.size()));
    observation observed = {cache, block->allocation, block->code, 0};
    active = &observed;
    auto* original = block.get();
    store.blocks.emplace(0x100, std::move(block));
    ROMlib_ppc_environment environment = {};
    auto* previous = romaw_ppc_environment;
    romaw_ppc_environment = &environment;
    any_register value;
    value.i = 2;
    cpu.set_register(powerpc_registers::CTR, value);
    cpu.gpr(3) = 41;
    uint32 pc = 0x100;
    assert(run_native(&cpu, &pc, *original, 1));
    assert(pc == 0x108 && cpu.get_register(powerpc_registers::CTR).i == 1 && cpu.gpr(3) == 41);
    assert(observed.callbacks == 1 && observed.allocation.expired() && environment.native_instructions == 1 &&
           environment.native_blocks == 1 && !environment.native_kernel_instructions);
    pc = 0x100;
    assert(run_native(&cpu, &pc, *store.blocks.at(0x100), 1));
    assert(pc == 0x104 && cpu.gpr(3) == 48 && cpu.get_register(powerpc_registers::CTR).i == 1);
    romaw_ppc_environment = previous;
    active = nullptr;
    ROMlib_ppc_translation_cache_destroy(cache);
    puts("An evicted native body resumes after a callback and retires its original CTR/PC metadata before release");
#endif
}

static void check_native_estimate_environment()
{
#if ROMAW_PPC_NATIVE_JIT
    powerpc_cpu cpu;
    native_block block, invocation;
    block.words.push_back(0xec601030); // FRES f3,f2.
    assert(compile(block, &cpu, 0x100) && !block.semantic);
    cpu.fpr_dw(2) = UINT64_C(0x4008000000000000);
    invocation.tail = [](powerpc_cpu*, native_block*) { assert(false && "normal reciprocal used a semantic helper"); };

    struct alignas(16) image {
        unsigned char bytes[512];
    } original = {}, before = {}, after = {};

#define SAVE_FP "fxsave64 %0"
#define RESTORE_FP "fxrstor64 %0"
    __asm__ __volatile__(SAVE_FP : "=m"(original) : : "memory");
    for (unsigned mode = 0; mode < 4; ++mode) {
        unsigned short control = 0x037f | mode << 10;
        __asm__ __volatile__("fninit; fldcw %0; fldz; fldz; fdivp; fldpi; fld1; fld1; fld1; fld1"
                             :
                             : "m"(control)
                             : "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "memory");
        __asm__ __volatile__(SAVE_FP : "=m"(before) : : "memory");
        reinterpret_cast<void (*)(powerpc_cpu*, native_block*)>(block.code)(&cpu, &invocation);
        __asm__ __volatile__(SAVE_FP : "=m"(after) : : "memory");
        __asm__ __volatile__(RESTORE_FP : : "m"(original) : "memory");
        // The environment plus eight 80-bit stack slots must be byte exact;
        // XMM registers are caller-saved and used to publish the final result.
        assert(!memcmp(before.bytes, after.bytes, 160));
    }
#undef SAVE_FP
#undef RESTORE_FP
    puts("Direct FRES preserves nonempty x87 stacks, all rounding modes, sticky status and full pointer fields");
#endif
}

static void check_decoder_coverage()
{
#if ROMAW_PPC_NATIVE_JIT
    powerpc_cpu cpu;
    unsigned total = 0, semantic = 0;
    for (unsigned primary = 0; primary < 64; ++primary)
        for (unsigned secondary = 0; secondary < 1024; ++secondary)
            for (unsigned variant = 0; variant < 2; ++variant) {
                uint32 opcode = primary << 26 | secondary << 1 | variant;
                if (primary == 6 || cpu.romaw_instruction(opcode)->mnemo == PPC_I(INVALID))
                    continue;
                assert(supported(opcode, &cpu));
                native_block block;
                block.words.push_back(opcode);
                assert(compile(block, &cpu, 0x100));
                assert(block.semantic == !inline_supported(opcode));
                semantic += block.semantic;
                ++total;
            }
    assert(total > 40000 && semantic > 10000);
    printf("PPC decoder samples lowered: %u; semantic tails: %u\n", total, semantic);
#endif
}

static void check_750_decoder_coverage()
{
#if ROMAW_PPC_NATIVE_JIT
    powerpc_cpu cpu;
    std::vector<unsigned char> bytes;
    unsigned total = 0, semantic = 0;
    for (unsigned primary = 0; primary < 64; ++primary)
        for (unsigned secondary = 0; secondary < 1024; ++secondary)
            for (unsigned variant = 0; variant < 2; ++variant)
                for (uint32 fields : {0u, 0x00E40000u}) {
                    uint32 opcode = primary << 26 | secondary << 1 | variant | fields;
                    if (!supported(opcode, &cpu, true))
                        continue;
                    native_block block;
                    block.architectural = true;
                    block.words.push_back(opcode);
                    emit_native_block(block, &cpu, 0x100, bytes);
                    assert(bytes.size() >= 11 && bytes.size() <= max_native_block_size);
                    assert(!block.memory); // Architectural operands must never bypass the checked bus.
                    semantic += block.semantic;
                    ++total;
                }
    assert(total > 100000 && total > semantic);
    printf("PPC 750 decoder samples lowered: %u; semantic tails: %u\n", total, semantic);

    // Canonical instructions with zero and populated fields that are lowered in the 750 path.
    for (uint32 opcode :
         {0x0C830007u, 0x44000002u, 0x4C000064u, 0x4C00012Cu, 0x7C002264u, 0x7C00046Cu, 0x7C600124u, 0x7C642A6Cu,
          0x7C642B6Cu, 0x7C042FECu, 0x7C0004ACu, 0x7C0006ACu, 0xBBA40000u, 0xBFA40000u, 0x7FE4342Au, 0x7FE43DAAu}) {
        assert(supported(opcode, &cpu, true));
        native_block block;
        block.architectural = true;
        block.words.push_back(opcode);
        assert(compile(block, &cpu, 0x100));
        assert(!block.memory && !block.semantic);
    }
    for (uint32 opcode :
         {0x18000000u, 0x44000000u, 0x4C000065u, 0x4C00012Du, 0x4C00112Cu, 0x7C0002E4u, 0x7C002265u, 0x7C20046Cu,
          0x7C042FEDu, 0x7C04042Au, 0xB8840000u, 0xFC60102Cu, 0xEC60102Cu, 0x10000000u, 0x7C6428CEu})
        assert(!supported(opcode, &cpu, true));
#endif
}

static void check_publication(const char* executable)
{
#if defined(_WIN32)
    std::string path = std::string(executable) + u8"-\u00e5.cache";
    powerpc_cpu cpu;
    auto source = ROMlib_ppc_translation_cache_create();
    auto target = ROMlib_ppc_translation_cache_create();
    assert(source && target);
    auto& store = *static_cast<native_store*>(source->implementation);
    auto& existing = *static_cast<native_store*>(target->implementation);
    add(store, cpu, 0x100, 0xFC22182A);       // Software floating-point semantic tail.
    add(store, cpu, 0x200, 0x38630001);       // Singleton inline integer body.
    add(store, cpu, 0x100, 0x80640000, true); // Architectural scalar memory must keep its checked bus transfer.
    const auto system_key = translation_key(0x100, 0x80640000, true);
    add(existing, cpu, 0x900, 0x38630002);
    void* original_code = existing.blocks.at(0x900)->code;
    assert(ROMlib_ppc_translation_cache_save(source, path.c_str()));
    auto original = read_file(path);
    auto reject = [&](std::vector<unsigned char> corrupt) {
        uint32 crc = cache_crc(corrupt.data(), corrupt.size() - 4);
        for (unsigned i = 0; i < 4; ++i)
            corrupt[corrupt.size() - 4 + i] = crc >> (8 * i);
        write_file(path, corrupt);
        failure = allocations = releases = 0;
        assert(!ROMlib_ppc_translation_cache_load(target, path.c_str()));
        assert(!allocations && !releases && existing.blocks.at(0x900)->code == original_code);
    };
    // A valid CRC with trailing bytes reaches final record/extent validation.
    // No candidate code may have become executable when that fails.
    auto corrupt = original;
    corrupt.insert(corrupt.end() - 4, 0x55);
    reject(std::move(corrupt));
    // Old IA-32 ABI tags must fail before any native body is published, even
    // when the rest of the artifact and its recomputed checksum are valid.
    for (uint32 retired_abi : {3u, 4u}) {
        corrupt = original;
        for (unsigned i = 0; i < 4; ++i)
            corrupt[72 + i] = retired_abi >> (i * 8);
        reject(std::move(corrupt));
    }
    write_file(path, original);
    for (unsigned mode : {1u, 2u, 3u, 5u, 6u}) {
        failure = mode;
        allocations = releases = 0;
        assert(!ROMlib_ppc_translation_cache_load(target, path.c_str()));
        assert(existing.blocks.size() == 1 && existing.blocks.at(0x900)->code == original_code);
        assert(allocations == (mode == 6 ? 3u : mode == 5 ? 2u : 1u));
        assert(releases == (mode == 6 ? 2u : mode == 1 ? 0u : 1u));
    }
    // The final architectural record must validate before any earlier flat
    // record is made executable. Clearing its mode bit would enable a raw-RAM
    // lowering classification inconsistent with the persisted semantic tail.
    cache_reader scan(original);
    scan.position = 80;
    size_t inline_word = 0;
    for (unsigned i = 0; i < 2; ++i) {
        uint32 pc = scan.word();
        unsigned words = scan.word();
        scan.take(16);
        unsigned code_size = scan.word();
        if (pc == 0x200)
            inline_word = scan.position;
        scan.take(words * 4 + code_size);
    }
    const size_t record = scan.position;
    corrupt = original;
    corrupt.insert(corrupt.end() - 4, original.begin() + record, original.end() - 4);
    ++corrupt[76]; // The same PC and opcode cannot be serialized twice.
    reject(std::move(corrupt));
    corrupt = original;
    corrupt[record + 9] &= ~0x40; // Clear architectural flag bit 14, little-endian flags word.
    reject(std::move(corrupt));
    corrupt = original;
    corrupt[record + 9] |= 0x80; // An omitted record cannot carry native code or execution flags.
    reject(std::move(corrupt));
    // The original prefix, final return byte and recomputed CRC all remain
    // valid. The final record must still match the trusted emitter exactly.
    for (size_t offset : {8u, 12u, 16u, 20u, 32u + 11u}) {
        corrupt = original;
        corrupt[record + offset] ^= 1;
        reject(std::move(corrupt));
    }
    assert(inline_word);
    corrupt = original;
    corrupt[inline_word] ^= 2; // Change the saved ADDI immediate independently of its native body.
    reject(std::move(corrupt));
    write_file(path, original);
    failure = 4; // A failed disk commit cannot replace the previous artifact.
    assert(!ROMlib_ppc_translation_cache_save(target, path.c_str()));
    assert(read_file(path) == original);
    failure = 0;
    assert(!ROMlib_ppc_translation_cache_save(target, (path + "/missing/file").c_str()));
    assert(ROMlib_ppc_translation_cache_load(target, path.c_str()) == 3);
    assert(existing.blocks.size() == 2 && existing.system_blocks.size() == 1 && !existing.blocks.count(0x900));
    const auto& checked = *existing.system_blocks.at(system_key);
    assert(!checked.semantic && !checked.memory && checked.system_operand.size == 4);

    const unsigned char replacement[] = {0x38, 0x63, 0x00, 0x04};
    auto* old = existing.blocks.at(0x200).get();
    old->last_used = 42;
    existing.clock = 42;
    for (unsigned limit : {4u, 3u}) {
        existing.limit = limit;
        for (unsigned mode : {1u, 2u, 3u}) {
            failure = mode;
            allocations = releases = 0;
            assert(!ROMlib_ppc_translate_range(target, replacement, 0x200, sizeof replacement));
            assert(allocations == 1 && releases == (mode == 1 ? 0u : 1u));
            assert(existing.size() == 3 && existing.blocks.at(0x200).get() == old);
        }
    }
    failure = 0;
    assert(ROMlib_ppc_translate_range(target, replacement, 0x200, sizeof replacement) == 1);
    assert(existing.size() == 3 && existing.blocks.at(0x200)->last_used == 42 && existing.clock == 42);
    assert(existing.blocks.at(0x200)->words[0] == 0x38630004);

    existing.system_blocks.at(system_key)->last_used = ++existing.clock;
    ROMlib_ppc_environment env = {};
    env.execution_flags = ROMLIB_PPC_EXEC_JIT;
    env.translation_cache = target;
    const unsigned char return_code[] = {0x4E, 0x80, 0x00, 0x20};
    const romaw_ppc_code_range late = {return_code, 0x400, sizeof return_code};
    {
        romaw_ppc_jit_scope scope(&env);
        assert(!romaw_ppc_translate_image(target, &late, 1, nullptr, 0, 0));
        assert(!ROMlib_ppc_translation_cache_save(target, path.c_str()));
        romaw_ppc_jit_service_scope service;
        for (unsigned mode : {1u, 2u, 3u}) {
            failure = mode;
            allocations = releases = 0;
            assert(!romaw_ppc_translate_image(target, &late, 1, nullptr, 0, 0));
            assert(allocations == 1 && releases == (mode == 1 ? 0u : 1u));
            assert(existing.size() == 3 && existing.blocks.count(0x100));
        }
        failure = 0;
        assert(romaw_ppc_translate_image(target, &late, 1, nullptr, 0, 0) == 1);
        assert(existing.size() == 3 && !existing.blocks.count(0x100) && existing.blocks.count(0x400));
        assert(existing.blocks.at(0x200)->last_used == 42 && existing.system_blocks.at(system_key)->last_used == 43);
        existing.blocks.at(0x400)->last_used = ++existing.clock;
        const romaw_ppc_code_range another = {return_code, 0x404, sizeof return_code};
        assert(!romaw_ppc_translate_image(target, &another, 1, nullptr, 0, 0));
        assert(existing.size() == 3 && !existing.blocks.count(0x404));

        // A different CPU's service cannot save this suspended store.
        {
            ROMlib_ppc_environment other_env = env;
            other_env.translation_cache = source;
            romaw_ppc_jit_scope other_scope(&other_env);
            romaw_ppc_jit_service_scope other_service;
            assert(!ROMlib_ppc_translation_cache_save(target, path.c_str()));
        }
        failure = 4;
        assert(!ROMlib_ppc_translation_cache_save(target, path.c_str()));
        assert(read_file(path) == original);
        failure = 0;
        assert(ROMlib_ppc_translation_cache_save(target, path.c_str()));
        assert(!ROMlib_ppc_translation_cache_load(target, path.c_str()));
        auto* checkpoint = ROMlib_ppc_translation_cache_create();
        assert(checkpoint && ROMlib_ppc_translation_cache_load(checkpoint, path.c_str()) == 3);
        auto& saved = *static_cast<native_store*>(checkpoint->implementation);
        assert(!saved.blocks.count(0x100) && saved.blocks.count(0x400));
        assert(saved.blocks.at(0x200)->words[0] == 0x38630004 && saved.system_blocks.count(system_key));
        ROMlib_ppc_translation_cache_destroy(checkpoint);
    }
    ROMlib_ppc_translation_cache_destroy(source);
    ROMlib_ppc_translation_cache_destroy(target);
    const std::string oversized_path = path + ".large";
    save_oversized_cache(oversized_path, cpu);
    replace_rewritten_flat_code();
    replace_full_views(cpu);
    prepare_published_words(cpu);
    const std::string reachable_path = path + ".reachable";
    prepare_linked_branches(reachable_path);
    assert(!_wremove(cache_wide(reachable_path.c_str()).c_str()));
    assert(!_wremove(cache_wide(oversized_path.c_str()).c_str()));
    assert(!_wremove(cache_wide(path.c_str()).c_str()));
    puts("PPC AOT complete validation, allocation/protection/flush rollback and atomic-save failures passed");
#else
    (void)executable;
    puts("Windows publication interception is not available on this host");
#endif
}

void ppc_check_native_kernels();
int ppc_check_translation_cache(int argc, char** argv);

static void check_native_context_sync()
{
    powerpc_cpu cpu;
    native_block block;
    block.architectural = true;
    block.words = {0x4C00012C};
    std::vector<unsigned char> bytes;
    emit_native_block(block, &cpu, 0x100, bytes);
    assert(!block.semantic && publish_native_block(block, bytes.data(), bytes.size()));
    std::unique_ptr<ROMlib_ppc_system750> state(new ROMlib_ppc_system750{});
    block.system = state.get();
    using entry = void (*)(powerpc_cpu*, native_block*);
    for (uint32_t pending : {0u, 1u, UINT32_MAX}) {
        state->iabr_pending = pending;
        state->iabr = 0x1234567A;
        state->iabr_previous = 0x87654322;
        reinterpret_cast<entry>(block.code)(&cpu, &block);
        assert(!state->iabr_pending && state->iabr == 0x1234567A && state->iabr_previous == 0x87654322);
    }
    puts("ISYNC acknowledges the instruction breakpoint inside emitted code");
}

static void check_native_reservation_callback_state()
{
    powerpc_cpu cpu;
    native_block block;
    block.architectural = true;
    block.words = {0x7C60212D}; // stwcx. r3,0,r4.
    std::vector<unsigned char> bytes;
    emit_native_block(block, &cpu, 0x100, bytes);
    assert(!block.semantic && publish_native_block(block, bytes.data(), bytes.size()));
    std::unique_ptr<ROMlib_ppc_system750> state(new ROMlib_ppc_system750{});
    static powerpc_cpu* callback_cpu;
    static unsigned calls;
    callback_cpu = &cpu;
    block.system = state.get();
    block.native_entry = +[](ppc_native_memory_frame* frame) {
        ++calls;
        // These are live CPU changes, rather than the invocation's old snapshot.
        callback_cpu->romaw_registers().cr.set(0x89ABCDEF);
        callback_cpu->romaw_registers().xer.set_so(4);
        frame->ok = 1;
    };
    using entry = void (*)(powerpc_cpu*, native_block*);
    for (bool available : {false, true}) {
        calls = 0;
        state->reservation_valid = available;
        cpu.romaw_registers().reserve_valid = 1;
        cpu.romaw_registers().cr.set(0x76543210);
        cpu.romaw_registers().xer.set_so(0);
        cpu.gpr(3) = 0x11223344;
        cpu.gpr(4) = 0x2000;
        block.native_completed = false;
        reinterpret_cast<entry>(block.code)(&cpu, &block);
        assert(block.native_completed && block.native_frame.ok && !state->reservation_valid &&
               !cpu.romaw_registers().reserve_valid);
        assert(calls == unsigned(available));
        assert(cpu.romaw_registers().cr.get() == (available ? 0x39ABCDEF : 0x06543210));
    }
    puts("STWCX consumes reservations and commits live callback CR/XER state in emitted code");
}

int main(int argc, char** argv)
{
    if (!ROMlib_ppc_jit_available()) {
        puts("Native PPC translation is unavailable on this host");
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--reject")) {
        FILE* foreign = cache_open(argv[2], false);
        assert(foreign && fgetc(foreign) == 'E' && !fclose(foreign));
        auto* cache = ROMlib_ppc_translation_cache_create();
        assert(cache);
        assert(!ROMlib_ppc_translation_cache_load(cache, argv[2]));
        ROMlib_ppc_translation_cache_destroy(cache);
        puts("Foreign PPC native cache rejected before execution");
        return 0;
    }
    if (argc == 3 && (!strcmp(argv[1], "--save") || !strcmp(argv[1], "--load")))
        return ppc_check_translation_cache(argc, argv);
    bool jit = argc == 2 && !strcmp(argv[1], "--jit");
    bool aot = argc == 2 && !strcmp(argv[1], "--aot");
    if (argc != 1 && !jit && !aot) {
        fputs("Usage: ppc-translation-checks [--jit | --aot | --save PATH | --load PATH | --reject PATH]\n", stderr);
        return 2;
    }
    if (!jit) {
        check_publication(argv[0]);
        if (int status = ppc_check_translation_cache(1, argv))
            return status;
    }
    if (!aot) {
        check_active_native_replacement();
        check_native_context_sync();
        check_native_reservation_callback_state();
        check_native_estimate_environment();
        ppc_check_native_kernels();
        check_decoder_coverage();
        check_750_decoder_coverage();
    }
    return 0;
}
