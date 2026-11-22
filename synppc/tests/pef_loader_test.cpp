/*+===================================================================
  File:      pef_loader_test.cpp

  Summary:   Implements pef loader test support for synppc PowerPC execution and fragment loading.

  Classes:   Declared: region; Used: ROMlib_pef_environment, ROMlib_pef_image, ROMlib_ppc_environment,
             ROMlib_ppc_registers, region, std::string, std::vector

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

#include "synppc/ppc.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

struct region {
    unsigned char* p;
    uint32_t address, size;
};

static std::vector<region> regions;
static uint32_t next_address;
static unsigned allocations;

static void put16(unsigned char* p, uint16_t v)
{
    p[0] = v >> 8;
    p[1] = v;
}

static void put(unsigned char* p, uint32_t v)
{
    for (int i = 3; i >= 0; --i) {
        p[i] = v;
        v >>= 8;
    }
}

static uint32_t get(const unsigned char* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void* allocate(void*, uint32_t size, uint32_t alignment, uint32_t* address)
{
    if (size > 2 * 1024 * 1024)
        return NULL;
    region r;
    r.p = (unsigned char*)calloc(1, size);
    assert(r.p);
    r.size = size;
    r.address = (next_address + alignment - 1) & ~(alignment - 1);
    next_address = r.address + size + 16;
    *address = r.address;
    regions.push_back(r);
    ++allocations;
    return r.p;
}

static void release(void*, void* p)
{
    for (size_t i = 0; i < regions.size(); ++i)
        if (regions[i].p == p) {
            regions.erase(regions.begin() + i);
            free(p);
            --allocations;
            return;
        }
    assert(false);
}

static void* map(void*, uint32_t address, size_t size, int)
{
    for (size_t i = 0; i < regions.size(); ++i) {
        region& r = regions[i];
        if (address >= r.address && address - r.address <= r.size && size <= r.size - (address - r.address))
            return r.p + address - r.address;
    }
    return NULL;
}

static unsigned resolved;

static int resolve_all(void*, const char*, const char*, unsigned, int, uint32_t* address)
{
    *address = 0x01000000 + 4 * resolved++;
    return 1;
}

static int resolve(void*, const char*, const char*, unsigned, int, uint32_t*)
{
    return 0;
}

static std::vector<unsigned char> fixture()
{
    std::vector<unsigned char> f(280, 0);
    put(&f[0], 0x4a6f7921);
    put(&f[4], 0x70656666);
    put(&f[8], 0x70777063);
    put(&f[12], 1);
    put16(&f[32], 3);
    put16(&f[34], 2);
    for (int i = 0; i < 3; ++i)
        put(&f[40 + i * 28], 0xffffffff);
    put(&f[48], 16);
    put(&f[52], 16);
    put(&f[56], 16);
    put(&f[60], 256);
    f[64] = 0;
    f[66] = 4;
    put(&f[76], 8);
    put(&f[80], 8);
    put(&f[84], 8);
    put(&f[88], 272);
    f[92] = 1;
    f[94] = 4;
    put(&f[112], 72);
    put(&f[116], 128);
    f[120] = 4;
    put(&f[128], 1);
    put(&f[136], 0xffffffff);
    put(&f[144], 0xffffffff);
    put(&f[160], 1);
    put(&f[164], 68);
    put(&f[168], 70);
    put(&f[172], 70);
    put16(&f[184], 1);
    put(&f[188], 1);
    put16(&f[196], 0x4600);
    put(&f[256], 0x3860002a);
    put(&f[260], 0x4e800020);
    put(&f[264], 0x18000000);
    return f;
}

static std::vector<unsigned char> exported_fixture(bool forward)
{
    std::vector<unsigned char> f = fixture();
    unsigned char code[16];
    memcpy(code, &f[256], 16);
    f.resize(536);
    memset(&f[198], 0, 338);
    memcpy(&f[512], code, 16);
    put(&f[60], 512);
    put(&f[88], 528);
    put(&f[112], 136);
    unsigned char* l = &f[128];
    if (forward) {
        put(l + 24, 1);
        put(l + 28, 1);
        put(l + 36, 96);
        put(l + 40, 98);
        put(l + 44, 116);
        put(l + 48, 0);
        put(l + 52, 1);
        memset(l + 56, 0, 80);
        put(l + 56 + 12, 1);
        put(l + 80, 0x02000005);
        put16(l + 84, 1);
        put(l + 88, 1);
        put16(l + 96, 0x4600);
        memcpy(l + 98, "LibB\0real\0answer", 16);
        put(l + 120, 6 << 16);
        put(l + 124, 0x0200000a);
        put16(l + 132, 0xfffd);
    }
    else {
        put(l + 44, 76);
        put(l + 48, 0);
        put(l + 52, 1);
        memcpy(l + 70, "answer", 6);
        put(l + 80, 6 << 16);
        put(l + 84, 0x02000000);
        put16(l + 92, 1);
    }
    return f;
}

static ROMlib_pef_image* peer;

static void test_aot_fingerprint(ROMlib_pef_environment env, unsigned kind)
{
    auto f = fixture();
    f[40 + 24] = kind;
    constexpr uint64_t seed = 123;
    auto fingerprint = [&](ROMlib_pef_image* image, unsigned mode) {
        uint64_t hash = seed;
        assert(ROMlib_pef_translation_fingerprint(&image, 1, mode, &hash));
        return hash;
    };
    next_address = 0x1000;
    auto* image = ROMlib_pef_prepare(f.data(), f.size(), &env);
    assert(image);
    uint64_t unchanged = seed;
    assert(!ROMlib_pef_translation_fingerprint(&image, 1, 0, &unchanged) && unchanged == seed);
    assert(ROMlib_pef_link(image));
    assert(ROMlib_pef_is_code(image, regions[0].address));
    assert(!ROMlib_pef_is_code(image, regions[1].address));
    if (auto* cache = ROMlib_ppc_translation_cache_create()) {
        assert(ROMlib_pef_translate(image, cache));
        assert(ROMlib_pef_translate_mode(image, cache, ROMLIB_PPC_TRANSLATE_750));
        ROMlib_ppc_translation_cache_destroy(cache);
    }
    const uint64_t original = fingerprint(image, 0);
    assert(fingerprint(image, ROMLIB_PPC_TRANSLATE_750) != original);
    auto* code = regions[0].p;
    auto* vector = regions[1].p;
    put(code, 0x3860002B);
    assert(fingerprint(image, 0) != original);
    put(code, 0x3860002A);
    put(vector, 0x1004);
    assert(fingerprint(image, 0) != original);
    put(vector, 0x1000);
    assert(fingerprint(image, 0) == original);

    next_address = 0x1000;
    auto* same_guest = ROMlib_pef_load(f.data(), f.size(), &env);
    assert(same_guest && regions[2].p != code && fingerprint(same_guest, 0) == original);
    ROMlib_pef_unload(same_guest);
    next_address = 0x2000;
    auto* relocated = ROMlib_pef_load(f.data(), f.size(), &env);
    assert(relocated && !memcmp(regions[2].p, code, regions[0].size) && fingerprint(relocated, 0) != original);
    ROMlib_pef_unload(relocated);
    ROMlib_pef_unload(image);
    assert(!allocations);
}

static void test_aot_entries(ROMlib_pef_environment env, const char* executable)
{
    auto cache = ROMlib_ppc_translation_cache_create();
    if (!cache)
        return;
    ROMlib_ppc_translation_cache_destroy(cache);
    for (uint32_t branch : {0x48000054u, 0x4800105Au, 0x42800054u}) {
        auto previous = exported_fixture(false);
        std::vector<unsigned char> f(584, 0);
        memcpy(f.data(), previous.data(), 40);
        put16(f.data() + 32, 4);
        put16(f.data() + 34, 3);
        auto section = [&](unsigned index, unsigned size, unsigned offset, unsigned kind) {
            auto* h = f.data() + 40 + 28 * index;
            put(h, UINT32_MAX);
            put(h + 8, size);
            put(h + 12, size);
            put(h + 16, size);
            put(h + 20, offset);
            h[24] = kind;
            h[26] = 4;
        };
        section(0, 32, 512, 0);
        section(1, 16, 568, 1);
        section(2, 24, 544, 0);
        section(3, 136, 256, 4);
        memcpy(f.data() + 256, previous.data() + 128, 136);
        put16(f.data() + 256 + 68, 0x4601); // Relocate both transition vectors.
        put(f.data() + 256 + 88, 8);        // The export uses the second vector.
        put(f.data() + 568, 4);             // Main enters section 0 at +4.
        put(f.data() + 576, 0x54);          // The export enters section 2 at +4.
        put(f.data() + 512, 0x386000EE);
        put(f.data() + 516, branch); // 0x1004 -> 0x1058.
        put(f.data() + 520, 0x18000000);
        put(f.data() + 544, 0x386000EE);
        put(f.data() + 548, 0x38630003);
        put(f.data() + 552, 0x38630007);
        put(f.data() + 556, 0x4E800020);
        next_address = 0x1000;
        auto image = ROMlib_pef_load(f.data(), f.size(), &env);
        assert(image && allocations == 3);
        uint32_t exported = 0;
        assert(ROMlib_pef_find_symbol(image, "answer", &exported, nullptr));
        uint32_t main_pc = get(static_cast<unsigned char*>(map(nullptr, ROMlib_pef_main(image), 8, 0)));
        uint32_t export_pc = get(static_cast<unsigned char*>(map(nullptr, exported, 8, 0)));
        assert(main_pc == 0x1004 && export_pc == 0x1054);
        unsigned char linear[0x68] = {};
        for (const auto& r : regions)
            memcpy(linear + (r.address - 0x1000), r.p, r.size);
        auto copy = ROMlib_pef_prepare(f.data(), f.size(), &env);
        assert(copy && ROMlib_pef_share_code(copy, image) == 2 && ROMlib_pef_link(copy));
        ROMlib_pef_image* group[] = {image, copy};
        cache = ROMlib_ppc_translation_cache_create();
        assert(ROMlib_pef_translate_group(group, 2, cache, 0) == 5);
        assert(!ROMlib_pef_translate(image, cache));
        for (bool main : {true, false}) {
            ROMlib_ppc_environment cpu = {};
            cpu.memory = map;
            cpu.linear_memory = linear;
            cpu.linear_address = 0x1000;
            cpu.linear_size = sizeof linear;
            cpu.translation_cache = cache;
            cpu.execution_flags = ROMLIB_PPC_EXEC_JIT;
            ROMlib_ppc_registers r = {};
            r.pc = main ? main_pc : export_pc;
            r.lr = 0x1008;
            assert(ROMlib_ppc_execute(&r, &cpu));
            assert(r.gpr[3] == (main ? 7u : 10u) && cpu.native_instructions == 3 && !cpu.native_compilations);
        }
        ROMlib_ppc_translation_cache_destroy(cache);
        ROMlib_pef_unload(copy);
        ROMlib_pef_unload(image);
        assert(!allocations);
    }

    // The section sweep reaches its budget before main and the private entries.
    auto f = fixture();
    const unsigned prefix = 4097 * 4, halt = prefix + 48, code_size = halt + 4;
    const unsigned data = 256 + code_size;
    f.resize(data + 40);
    put(f.data() + 48, code_size);
    put(f.data() + 52, code_size);
    put(f.data() + 56, code_size);
    put(f.data() + 76, 40);
    put(f.data() + 80, 40);
    put(f.data() + 84, 40);
    put(f.data() + 88, data);
    put(f.data() + 112, 80);
    put(f.data() + 168, 80);
    put(f.data() + 172, 80);
    const uint16_t relocs[] = {0x4600, 0x4401, 0x4600, 0xA000, 32, 0x4000};
    put(f.data() + 188, sizeof relocs / sizeof *relocs);
    for (unsigned i = 0; i < sizeof relocs / sizeof *relocs; ++i)
        put16(f.data() + 196 + i * 2, relocs[i]);
    for (unsigned i = 0; i < prefix; i += 4)
        put(f.data() + 256 + i, 0x48000004);
    const unsigned offsets[] = {0, 12, 24, 36};
    for (unsigned i = 0; i < sizeof offsets / sizeof *offsets; ++i) {
        auto* code = f.data() + 256 + prefix + offsets[i];
        put(code, 0x3860002A + i);
        put(code + 4, 0x4E800020);
        put(code + 8, 0x386000EE); // Supported predecessor of the next local entry.
    }
    put(f.data() + 256 + halt, 0x18000000);
    put(f.data() + data, prefix);
    put(f.data() + data + 8, prefix + 12);
    put(f.data() + data + 16, 0xDEADBEEF);
    put(f.data() + data + 20, prefix + 24);
    put(f.data() + data + 28, 0xFEEDFACE);
    put(f.data() + data + 32, prefix + 36 - 0x1000); // The last relocation adds section C again.
    next_address = 0x1000;
    auto image = ROMlib_pef_load(f.data(), f.size(), &env);
    assert(image);
    cache = ROMlib_ppc_translation_cache_create();
    assert(ROMlib_pef_translate(image, cache) == 4096);
    auto exercise_entries = [&]() {
        const unsigned vectors[] = {0, 8, 20, 32};
        for (unsigned i = 0; i < sizeof vectors / sizeof *vectors; ++i) {
            ROMlib_ppc_environment cpu = {};
            cpu.memory = map;
            cpu.linear_memory = regions[0].p;
            cpu.linear_address = regions[0].address;
            cpu.linear_size = regions[0].size;
            cpu.translation_cache = cache;
            cpu.execution_flags = ROMLIB_PPC_EXEC_JIT;
            ROMlib_ppc_registers r = {};
            r.pc = get(regions[1].p + vectors[i]);
            assert(r.pc == 0x1000 + prefix + offsets[i]);
            r.lr = cpu.linear_address + halt;
            cpu.instruction_limit = 32;
            assert(ROMlib_ppc_execute(&r, &cpu));
            assert(r.gpr[3] == 42 + i && cpu.native_instructions == 2 && !cpu.native_compilations);
        }
    };
    exercise_entries();
    assert(get(regions[1].p + 16) == 0xDEADBEEF && get(regions[1].p + 28) == 0xFEEDFACE);
    ROMlib_ppc_translation_cache_destroy(cache);

    // Persist a full cache with one live entry, one stale entry and two missing
    // entries, as can happen after runtime compilation displaces startup code.
    constexpr uint32_t filler_base = 0x200000;
    cache = ROMlib_ppc_translation_cache_create();
    assert(cache);
    assert(ROMlib_ppc_translate_range(cache, regions[0].p + prefix, 0x1000 + prefix, 8) == 1);
    assert(ROMlib_ppc_translate_range(cache, regions[0].p + prefix, 0x1000 + prefix + offsets[1], 8) == 1);
    assert(ROMlib_ppc_translate_range(cache, regions[0].p, filler_base, 4094 * 4) == 4094);
    const std::string cache_path = std::string(executable) + ".full-pef.cache";
    assert(ROMlib_ppc_translation_cache_save(cache, cache_path.c_str()));
    ROMlib_ppc_translation_cache_destroy(cache);
    cache = ROMlib_ppc_translation_cache_create();
    assert(cache && ROMlib_ppc_translation_cache_load(cache, cache_path.c_str()) == 4096);
    assert(!remove(cache_path.c_str()));
    assert(ROMlib_pef_translate(image, cache) == 3);
    assert(!ROMlib_pef_translate(image, cache));
    assert(!ROMlib_ppc_translate_range(cache, regions[0].p, filler_base + 4096 * 4, 4));
    exercise_entries();
    ROMlib_ppc_translation_cache_destroy(cache);
    ROMlib_pef_unload(image);
    assert(!allocations);
}

static int resolve_peer(void*, const char* lib, const char* name, unsigned, int, uint32_t* address)
{
    return !strcmp(lib, "LibB") && !strcmp(name, "real") && ROMlib_pef_find_symbol(peer, "answer", address, NULL);
}

static void test_exports(ROMlib_pef_environment env)
{
    std::vector<unsigned char> f = exported_fixture(false);
    next_address = 0x1000;
    peer = ROMlib_pef_prepare(f.data(), f.size(), &env);
    assert(peer);
    assert(ROMlib_pef_library_count(peer) == 0 && ROMlib_pef_import_count(peer) == 0);
    uint32_t address;
    unsigned type;
    assert(ROMlib_pef_find_symbol(peer, "answer", &address, &type) && type == 2);
    assert(!ROMlib_pef_find_symbol(peer, "answe", &address, &type));
    assert(ROMlib_pef_export_count(peer) == 1);
    assert(!strcmp(ROMlib_pef_export_info(peer, 0, &address, &type), "answer"));
    assert(type == 2);
    assert(!ROMlib_pef_export_info(peer, 1, &address, &type));
    assert(!ROMlib_pef_export_info(peer, UINT32_MAX, &address, &type));
    assert(!ROMlib_pef_export_info(peer, 0, NULL, &type));
    assert(!ROMlib_pef_export_count(NULL));
    env.resolve = resolve_peer;
    f = exported_fixture(true);
    ROMlib_pef_image* alias = ROMlib_pef_prepare(f.data(), f.size(), &env);
    assert(alias);
    uint32_t library_info[4];
    assert(ROMlib_pef_library_count(alias) == 1 && ROMlib_pef_import_count(alias) == 1);
    assert(!strcmp(ROMlib_pef_library_info(alias, 0, library_info), "LibB"));
    assert(library_info[2] == 0 && library_info[3] == 1);
    assert(!ROMlib_pef_library_info(alias, 1, library_info));
    uint32_t second;
    assert(ROMlib_pef_find_symbol(alias, "answer", &second, &type) && second == address && type == 2);
    assert(!strcmp(ROMlib_pef_export_info(alias, 0, &second, &type), "answer") && second == address && type == 2);
    assert(ROMlib_pef_link(alias) && ROMlib_pef_link(peer));
    assert(ROMlib_pef_link(peer)); /* Never relocate an already linked image again. */
    ROMlib_pef_unload(alias);
    ROMlib_pef_unload(peer);
    assert(!allocations);
    f = exported_fixture(false);
    put(&f[172], 0xfffffffc);
    assert(!ROMlib_pef_prepare(f.data(), f.size(), &env) && !allocations);
    f = exported_fixture(false);
    put16(&f[220], 999);
    assert(!ROMlib_pef_prepare(f.data(), f.size(), &env) && !allocations);
    f = exported_fixture(false);
    put(&f[208], 0xffff0000);
    assert(!ROMlib_pef_prepare(f.data(), f.size(), &env) && !allocations);
    puts("PEF named exports, re-exports, prepare/link and malformed export tests passed");
}

static void test_private_code_storage(ROMlib_pef_environment env)
{
    std::vector<unsigned char> f = exported_fixture(false);
    f.resize(544, 0);
    put(&f[76], 16);
    put(&f[80], 16);
    put(&f[84], 16);
    for (unsigned reverse = 0; reverse < 2; ++reverse) {
        next_address = 0x1000;
        ROMlib_pef_image* source = ROMlib_pef_load(f.data(), f.size(), &env);
        assert(source);
        uint32_t original, copied;
        assert(ROMlib_pef_find_symbol(source, "answer", &original, NULL));
        unsigned char* first = (unsigned char*)map(NULL, original, 16, 1);
        put(first + 8, 0x12345678);
        ROMlib_pef_image* copy = ROMlib_pef_prepare(f.data(), f.size(), &env);
        assert(copy && ROMlib_pef_share_code(copy, source) == 1 && allocations == 3);
        assert(ROMlib_pef_link(copy));
        assert(ROMlib_pef_find_symbol(copy, "answer", &copied, NULL) && copied != original);
        unsigned char* second = (unsigned char*)map(NULL, copied, 16, 1);
        assert(get(first) == get(second) && get(first + 4) != get(second + 4));
        assert(get(first + 8) == 0x12345678 && !get(second + 8));
        assert(!ROMlib_pef_share_code(copy, source));
        uint32_t survivor = reverse ? original : copied;
        ROMlib_pef_unload(reverse ? copy : source);
        assert(allocations == 2);
        unsigned char* vector = (unsigned char*)map(NULL, survivor, 8, 0);
        assert(map(NULL, get(vector), 4, 0));
        ROMlib_pef_unload(reverse ? source : copy);
        assert(!allocations);
    }
    /* Code relocations and changed code cannot alias another instance. */
    for (unsigned variant = 0; variant < 2; ++variant) {
        std::vector<unsigned char> altered = f;
        if (!variant)
            put16(&altered[184], 0);
        else
            put(&altered[512], 0x38600043);
        next_address = 0x1000;
        ROMlib_pef_image* source = ROMlib_pef_load(f.data(), f.size(), &env);
        ROMlib_pef_image* copy = ROMlib_pef_prepare(altered.data(), altered.size(), &env);
        assert(source && copy && !ROMlib_pef_share_code(copy, source) && allocations == 4);
        ROMlib_pef_unload(copy);
        ROMlib_pef_unload(source);
        assert(!allocations);
    }

    f[40 + 24] = 6; // Executable data stays writable and private in each instance.
    next_address = 0x1000;
    auto* source = ROMlib_pef_load(f.data(), f.size(), &env);
    auto* copy = ROMlib_pef_prepare(f.data(), f.size(), &env);
    assert(source && copy && !ROMlib_pef_share_code(copy, source) && ROMlib_pef_link(copy) && allocations == 4);
    assert(ROMlib_pef_is_code(source, regions[0].address) && ROMlib_pef_is_code(copy, regions[2].address));
    uint32_t opcode = get(regions[2].p);
    assert(get(regions[0].p) == opcode);
    put(regions[0].p, opcode ^ 1);
    assert(get(regions[2].p) == opcode);
    ROMlib_pef_unload(copy);
    ROMlib_pef_unload(source);
    assert(!allocations);
}

int main(int argc, char** argv)
{
    ROMlib_pef_environment env = {};
    env.allocate = allocate;
    env.release = release;
    env.resolve = resolve;
    {
        auto m68k = fixture();
        put(&m68k[8], ROMLIB_PEF_M68K);
        /* MOVEQ #42,D0; RTS, with an eight-byte code/A5 transition vector. */
        put(&m68k[256], 0x702a4e75);
        next_address = 0x1000;
        assert(!ROMlib_pef_prepare(m68k.data(), m68k.size(), &env));
        assert(!allocations);
        auto image = ROMlib_pef_prepare_arch(m68k.data(), m68k.size(), &env, ROMLIB_PEF_M68K);
        assert(image && ROMlib_pef_architecture(image) == ROMLIB_PEF_M68K);
        assert(ROMlib_pef_link(image));
        auto entry = static_cast<unsigned char*>(map(nullptr, ROMlib_pef_main(image), 8, 0));
        assert(entry && get(entry) == 0x1000 && get(entry + 4) == 0x1020);
        assert(ROMlib_pef_is_code(image, 0x1002));
        assert(!ROMlib_pef_is_code(image, 0x1001));
        assert(ROMlib_pef_contains(image, 0x1000, 16));
        assert(ROMlib_pef_contains(image, 0x1010, 0));
        assert(!ROMlib_pef_contains(image, 0x1000, 17));
        assert(!ROMlib_pef_contains(image, 0x0fff, 1));
        assert(!ROMlib_pef_contains(image, 0x1000, SIZE_MAX));
        assert(!ROMlib_pef_contains(image, UINT32_MAX, 2));
        uint32_t section_base, section_size;
        unsigned section_kind;
        assert(ROMlib_pef_address_section(image, 0x1002, 2, &section_base, &section_size, &section_kind));
        assert(section_base == 0x1000 && section_size == 16 && section_kind == 0);
        assert(ROMlib_pef_address_section(image, 0x1020, 8, &section_base, &section_size, &section_kind));
        assert(section_base == 0x1020 && section_size == 8 && section_kind == 1);
        assert(ROMlib_pef_address_section(image, 0x1010, 0, nullptr, nullptr, nullptr));
        for (size_t length : {size_t(17), SIZE_MAX}) {
            section_base = section_size = section_kind = UINT32_MAX;
            assert(!ROMlib_pef_address_section(image, 0x1000, length, &section_base, &section_size, &section_kind));
            assert(!section_base && !section_size && !section_kind);
        }
        assert(!ROMlib_pef_address_section(nullptr, 0, 0, &section_base, &section_size, &section_kind));
        assert(!ROMlib_pef_address_section(image, UINT32_MAX, 2, nullptr, nullptr, nullptr));
        assert(!ROMlib_pef_translate(image, nullptr));
        ROMlib_pef_unload(image);
        assert(!allocations);
        for (size_t n = 0; n < m68k.size(); ++n) {
            assert(!ROMlib_pef_prepare_arch(m68k.data(), n, &env, ROMLIB_PEF_M68K));
            assert(!allocations);
        }
        assert(!ROMlib_pef_prepare_arch(m68k.data(), m68k.size(), &env, 0));
        assert(!allocations);
        puts("CFM-68K PEF architecture, relocation and truncation tests passed");
    }
    test_private_code_storage(env);
    test_exports(env);
    test_aot_fingerprint(env, 0);
    test_aot_fingerprint(env, 6);
    test_aot_entries(env, argv[0]);
    std::vector<unsigned char> f = fixture();
    next_address = 0x1000;
    ROMlib_pef_image* image = ROMlib_pef_load(&f[0], f.size(), &env);
    if (!image)
        fprintf(stderr, "%s\n", env.error);
    assert(image && allocations == 2);
    assert(ROMlib_pef_is_code(image, 0x1000));
    assert(!ROMlib_pef_is_code(image, 0x1001));
    assert(!ROMlib_pef_is_code(image, 0x1020));
    assert(!ROMlib_pef_is_code(image, 0xfffffffc));
    uint32_t tv = ROMlib_pef_main(image);
    unsigned char* entry = (unsigned char*)map(NULL, tv, 8, 0);
    assert(entry);
    assert(get(entry) == 0x1000 && get(entry + 4) == 0x1020);
    ROMlib_ppc_registers r = {};
    ROMlib_ppc_environment cpu = {};
    cpu.memory = map;
    cpu.instruction_limit = 100;
    r.pc = get(entry);
    r.gpr[2] = get(entry + 4);
    r.lr = 0x1008;
    assert(ROMlib_ppc_execute(&r, &cpu) && r.gpr[3] == 42);
    ROMlib_pef_unload(image);
    assert(!allocations);
    {
        auto eager = fixture();
        eager.resize(320);
        for (unsigned offset : {48u, 52u, 56u, 76u, 80u, 84u})
            put(&eager[offset], 32);
        put(&eager[88], 288);
        for (unsigned i = 0; i < 4; ++i) {
            put(&eager[256 + i * 4], 0x38630002);
            put(&eager[296 + i * 4], 0x38630001);
        }
        put(&eager[272], 0x4e800020);
        put(&eager[276], 0x18000000);
        next_address = 0x1000;
        image = ROMlib_pef_load(eager.data(), eager.size(), &env);
        assert(image);
        auto cache = ROMlib_ppc_translation_cache_create();
        if (cache) {
            assert(ROMlib_pef_translate(image, cache) == 1);
            assert(!ROMlib_pef_translate(image, cache));
            ROMlib_ppc_environment translated = {};
            translated.memory = map;
            translated.linear_address = 0x1000;
            translated.linear_memory = static_cast<unsigned char*>(map(nullptr, 0x1000, 32, 0));
            translated.linear_size = 32;
            translated.translation_cache = cache;
            translated.execution_flags = ROMLIB_PPC_EXEC_JIT;
            ROMlib_ppc_registers state = {};
            state.pc = 0x1000;
            state.lr = 0x1014;
            state.gpr[3] = 17;
            assert(ROMlib_ppc_execute(&state, &translated));
            assert(state.gpr[3] == 25 && translated.native_blocks == 1 && !translated.native_compilations);
            ROMlib_ppc_translation_cache_destroy(cache);
        }
        ROMlib_pef_unload(image);
        assert(!allocations);
    }
    for (size_t n = 0; n < f.size(); ++n) {
        next_address = 0x1000;
        image = ROMlib_pef_load(&f[0], n, &env);
        assert(!image && !allocations);
    }
    /* Input arithmetic must not wrap on the 32-bit target. */
    put(&f[152], 0x80000000);
    assert(!ROMlib_pef_load(&f[0], f.size(), &env) && !allocations);
    f = fixture();
    put(&f[188], 0xffffffff);
    assert(!ROMlib_pef_load(&f[0], f.size(), &env) && !allocations);
    f = fixture();
    put16(&f[196], 0x4fff);
    assert(!ROMlib_pef_load(&f[0], f.size(), &env) && !allocations);
    if (argc > 1) {
        FILE* file = fopen(argv[1], "rb");
        assert(file);
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        rewind(file);
        f.resize(size);
        assert(fread(&f[0], 1, f.size(), file) == f.size());
        fclose(file);
        next_address = 0x1000;
        image = ROMlib_pef_load(&f[0], f.size(), &env);
        assert(!image && !allocations);
        printf("Application loader diagnostic: %s\n", env.error);
        env.resolve = resolve_all;
        resolved = 0;
        next_address = 0x1000;
        image = ROMlib_pef_load(&f[0], f.size(), &env);
        if (!image)
            fprintf(stderr, "Full relocation failure: %s\n", env.error);
        assert(image && ROMlib_pef_main(image) && resolved);
        printf("Application sections and relocations loaded with %u synthetic imports\n", resolved);
        ROMlib_pef_unload(image);
        assert(!allocations);
    }
    puts("PEF relocation/execution, truncation, overflow and allocation cleanup tests passed");
}
