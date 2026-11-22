/*+===================================================================
  File:      aot-x64-cache-test.c

  Summary:   Exercise persistent native cache validation and page-publication failures.

  Classes:   None.

  Functions: main; imports the intercepted emitter fixture.

  Origin:    ARDI Executor native cache regression fixture.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#define SYN68K_AOT_BUILD_ID "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define main allocation_fixture_main
#include "jit-x64-allocation-test.c"
#undef main

static void save(const char* path, const void* bytes, size_t size)
{
    FILE* file = fopen(path, "wb");
    assert(file && fwrite(bytes, 1, size, file) == size && !fclose(file));
}

int main(int argc, char** argv)
{
    assert(argc == 2 && !_putenv_s("SYN68K_AOT_CACHE", argv[1]));
    uint16 source[] = {SWAPUW_IFLE(0x4E71)};
    int8 length = 1;
    cache_key key;
    char path[4096];
    assert(cache_path(&key, path, source, &length, 1, 0x1000));
    cache_key mapped_key;
    char mapped_path[4096];
    assert(!key.mode && cache_path_mode(&mapped_key, mapped_path, source, &length, 1, 0x1000, 1));
    assert(mapped_key.mode == 1 && strcmp(path, mapped_path));
    assert(!cache_path_mode(&mapped_key, mapped_path, source, &length, 1, 0x1000, 2));
    emitter original = {.managed = 1}, restored = {0};
    for (unsigned symbol = 0; symbol < RELOC_COUNT; ++symbol)
        pointer_r8(&original, symbol);
    byte(&original, 0xC3);
    cache_write(path, &key, source, &original, 1, 1);
    unsigned count = 0;
    int terminal = 0;
    assert(cache_read(path, &key, source, &restored, &count, &terminal));
    assert(!cache_read(path, &mapped_key, source, &restored, &count, &terminal));
    assert(count == 1 && terminal && original.size == restored.size && restored.managed == 1);
    assert(!memcmp(original.bytes, restored.bytes, original.size));
    unsigned char bytes[4096], copy[4096];
    FILE* file = fopen(path, "rb");
    assert(file);
    size_t size = fread(bytes, 1, sizeof bytes, file);
    assert(size < sizeof bytes && !fclose(file));
    /* Every truncation and every single-byte corruption is a cache miss. */
    for (size_t i = 0; i < size; ++i) {
        save(path, bytes, i);
        assert(!cache_read(path, &key, source, &restored, &count, &terminal));
        memcpy(copy, bytes, size);
        copy[i] ^= 0x80;
        save(path, copy, size);
        assert(!cache_read(path, &key, source, &restored, &count, &terminal));
    }
    memcpy(copy, bytes, size);
    copy[size] = 0;
    save(path, copy, size + 1);
    assert(!cache_read(path, &key, source, &restored, &count, &terminal));
    /* Recompute checksums to reach structural relocation validation. */
    cache_header header;
    memcpy(&header, bytes, sizeof header);
    memcpy(restored.bytes, bytes + sizeof header + key.source_size, header.code_size);
    memcpy(restored.relocations, bytes + sizeof header + key.source_size + header.code_size,
           header.relocations * sizeof(relocation));
    for (unsigned invalid = 0; invalid < 2; ++invalid) {
        header.managed = invalid == 0 ? 2 : 1;
        memcpy(header.magic, invalid == 0 ? "S68AOT03" : "S68AOT02", 8);
        header.checksum = cache_checksum(&header, source, &restored);
        memcpy(copy, bytes, size);
        memcpy(copy, &header, sizeof header);
        save(path, copy, size);
        assert(!cache_read(path, &key, source, &restored, &count, &terminal));
    }
    header.managed = 1;
    memcpy(header.magic, "S68AOT03", 8);
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        relocation old = restored.relocations[1];
        if (invalid == 0)
            restored.relocations[1].symbol = RELOC_COUNT;
        else if (invalid == 1)
            restored.relocations[1].offset = UINT32_MAX;
        else if (invalid == 2)
            restored.relocations[1].offset = restored.relocations[0].offset;
        else
            restored.bytes[restored.relocations[1].offset] = 1;
        header.checksum = cache_checksum(&header, source, &restored);
        memcpy(copy, &header, sizeof header);
        memcpy(copy + sizeof header, source, key.source_size);
        memcpy(copy + sizeof header + key.source_size, restored.bytes, header.code_size);
        memcpy(copy + sizeof header + key.source_size + header.code_size, restored.relocations,
               header.relocations * sizeof(relocation));
        save(path, copy, size);
        assert(!cache_read(path, &key, source, &restored, &count, &terminal));
        restored.relocations[1] = old;
    }
    assert(!step); /* No malformed file reached executable allocation. */
    remove(path);
    /* Real compile entry: cold emission, then restoration without emission.
       Page allocation/protection/flush failures retain interpreter fallback. */
    failure = step = freed = 0;
    void* code = syn68k_x64_compile(source, &length, 1, &count, 0x1000, &terminal);
    assert(code && cache_emissions == 1 && !cache_hits);
    syn68k_x64_release(code);
    for (failure = 0; failure < 4; ++failure) {
        step = freed = 0;
        code = syn68k_x64_compile(source, &length, 1, &count, 0x1000, &terminal);
        assert(!!code == !failure && count == !failure && terminal == !failure);
        assert(cache_emissions == 1 && cache_hits == 1);
        syn68k_x64_release(code);
        assert(step == (failure ? failure : 3) && freed == (failure != 1));
    }
    remove(path);
    /* Missing directories never leave serialized zero pointers in the live
       buffer. Failed file publication must not affect native compilation. */
    unsigned char expected[8192];
    memcpy(expected, original.bytes, original.size);
    char unavailable[4160];
    snprintf(unavailable, sizeof unavailable, "%s/missing/entry.s68", argv[1]);
    cache_write(unavailable, &key, source, &original, 1, 1);
    assert(!memcmp(expected, original.bytes, original.size));
    failure = step = freed = 0;
    save(path, "bad", 3);
    code = syn68k_x64_compile(source, &length, 1, &count, 0x1000, &terminal);
    assert(code && cache_emissions == 2 && cache_hits == 1);
    syn68k_x64_release(code);
    assert(cache_read(path, &key, source, &restored, &count, &terminal));
    remove(path);
    /* A mapped cache cannot restore an unmanaged body, even with an intact
       checksum and the correct mapped key. It must emit a managed body. */
    uint16 mapped_source = SWAPUW_IFLE(0xf200);
    assert(cache_path_mode(&mapped_key, mapped_path, &mapped_source, &length, 1, 0x1000, 1));
    emitter unmanaged = {0};
    byte(&unmanaged, 0xc3);
    cache_write(mapped_path, &mapped_key, &mapped_source, &unmanaged, 1, 1);
    failure = step = freed = 0;
    uint64 emissions = cache_emissions;
    code = compile_mapped(0x1000, 0xf200);
    assert(code && cache_emissions == emissions + 1);
    const Syn68kX64CodeHeader* mapped_header = (const void*)((unsigned char*)code - sizeof *mapped_header);
    assert(mapped_header->managed == 1);
    syn68k_x64_release(code);
    remove(mapped_path);
    assert(!_putenv_s("SYN68K_AOT_CACHE", ""));
    assert(!cache_path(&key, path, source, &length, 1, 0x1000));
    puts("AOT validation, relocations, emitter bypass and publication failures passed.");
    return 0;
}
