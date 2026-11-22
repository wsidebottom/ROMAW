/*+===================================================================
  File:      jit-cache.h

  Summary:   Persist and restore bounded Win64 native prefixes with symbolic relocations.

  Classes:   None.

  Functions: Private cache key, validation, publication and page helpers.

  Origin:    ARDI Executor persistent 68K translation implementation.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Private to jit-x64.c. Cache files contain executable code and must reside in
   a trusted local directory. The checksum detects damage, not hostile edits. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>

#if !defined(SYN68K_AOT_BUILD_ID) && __has_include("generated/jit-build-id.h")
#include "generated/jit-build-id.h"
#endif
#ifndef SYN68K_AOT_BUILD_ID
#define SYN68K_AOT_BUILD_ID ""
#endif

extern __declspec(dllimport) int __stdcall MoveFileExA(const char*, const char*, unsigned long);

/* No host pointers or compiler-dependent enums are serialized. The build key
   also fences compiler, configuration, helper ABI and CPUState changes. */
typedef struct {
    uint32 pc, instructions, source_size, optimize, mode;
    uint8 lengths[64];
    char build[65];
} cache_key;

typedef struct {
    char magic[8];
    cache_key key;
    uint32 code_size, relocations, translated, terminal, managed;
    uint64 checksum;
} cache_header;

#define CACHE_SOURCE_LIMIT (64 * 127 * 2)
static uint64 cache_hits, cache_emissions;

static uint64 cache_hash(uint64 hash, const void* data, size_t size)
{
    const unsigned char* bytes = data;
    while (size--) {
        hash ^= *bytes++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64 cache_checksum(const cache_header* header, const void* source, const emitter* out)
{
    uint64 hash = cache_hash(UINT64_C(14695981039346656037), header, offsetof(cache_header, checksum));
    hash = cache_hash(hash, source, header->key.source_size);
    hash = cache_hash(hash, out->bytes, header->code_size);
    return cache_hash(hash, out->relocations, header->relocations * sizeof(relocation));
}

static int cache_path_mode(cache_key* key, char path[4096], const uint16* source, const int8* lengths,
                           unsigned instructions, uint32 pc, unsigned mode)
{
    const char* directory = getenv("SYN68K_AOT_CACHE");
    if (!directory || !*directory || sizeof(SYN68K_AOT_BUILD_ID) != 65 || !instructions || mode > 1)
        return 0;
    memset(key, 0, sizeof *key);
    key->pc = pc;
    key->instructions = instructions > 64 ? 65 : instructions;
    key->optimize = !!syn68k_x64_optimize_flags;
    key->mode = mode;
    memcpy(key->build, SYN68K_AOT_BUILD_ID, sizeof(SYN68K_AOT_BUILD_ID));
    for (unsigned i = 0; i < instructions && i < 64; ++i) {
        if (lengths[i] <= 0)
            return 0;
        key->lengths[i] = lengths[i];
        key->source_size += 2 * lengths[i];
    }
    uint64 hash = cache_hash(UINT64_C(14695981039346656037), key, sizeof *key);
    hash = cache_hash(hash, source, key->source_size);
    int length = snprintf(path, 4096, "%s/%016llx.s68", directory, (unsigned long long)hash);
    return length > 0 && length < 4096;
}

static int cache_path(cache_key* key, char path[4096], const uint16* source, const int8* lengths, unsigned instructions,
                      uint32 pc)
{
    return cache_path_mode(key, path, source, lengths, instructions, pc, 0);
}

/* All metadata, source and checksum validation precedes executable allocation.
   Stored pointer slots are zero; ordered, disjoint records patch current hosts. */
static int cache_read(const char* path, const cache_key* key, const uint16* source, emitter* out, unsigned* translated,
                      int* terminal)
{
    FILE* file = fopen(path, "rb");
    if (!file)
        return 0;
    cache_header header;
    unsigned char saved_source[CACHE_SOURCE_LIMIT];
    int valid = fread(&header, 1, sizeof header, file) == sizeof header && memcmp(header.magic, "S68AOT03", 8) == 0 &&
                memcmp(&header.key, key, sizeof *key) == 0 && header.code_size > 0 &&
                header.code_size <= sizeof out->bytes &&
                header.relocations <= sizeof out->relocations / sizeof out->relocations[0] && header.translated > 0 &&
                header.translated <= 64 && header.translated <= key->instructions && header.terminal <= 1 &&
                header.managed <= 1 && key->source_size <= sizeof saved_source;
    if (valid)
        valid = fread(saved_source, 1, key->source_size, file) == key->source_size &&
                memcmp(saved_source, source, key->source_size) == 0 &&
                fread(out->bytes, 1, header.code_size, file) == header.code_size &&
                fread(out->relocations, sizeof(relocation), header.relocations, file) == header.relocations &&
                fgetc(file) == EOF && !ferror(file);
    if (fclose(file))
        valid = 0;
    if (!valid || header.checksum != cache_checksum(&header, source, out))
        return 0;
    for (unsigned i = 0; i < header.relocations; ++i) {
        relocation entry = out->relocations[i];
        uintptr_t previous;
        if (entry.symbol >= RELOC_COUNT || header.code_size < sizeof previous ||
            entry.offset > header.code_size - sizeof previous ||
            (i && entry.offset < out->relocations[i - 1].offset + sizeof previous))
            return 0;
        memcpy(&previous, out->bytes + entry.offset, sizeof previous);
        if (previous)
            return 0;
    }
    for (unsigned i = 0; i < header.relocations; ++i) {
        relocation entry = out->relocations[i];
        uintptr_t value = relocation_address(entry.symbol);
        memcpy(out->bytes + entry.offset, &value, sizeof value);
    }
    out->size = header.code_size;
    out->relocation_count = header.relocations;
    out->managed = header.managed;
    *translated = header.translated;
    *terminal = header.terminal;
    return 1;
}

static void cache_write(const char* path, const cache_key* key, const uint16* source, emitter* out, unsigned translated,
                        int terminal)
{
    cache_header header = {0};
    memcpy(header.magic, "S68AOT03", 8);
    memcpy(&header.key, key, sizeof *key);
    header.code_size = out->size;
    header.relocations = out->relocation_count;
    header.translated = translated;
    header.terminal = terminal;
    header.managed = out->managed;
    /* Serialize canonical pointer-free bytes; restore the publication buffer
       before returning, including on file and allocation failures. */
    for (unsigned i = 0; i < out->relocation_count; ++i)
        memset(out->bytes + out->relocations[i].offset, 0, sizeof(uintptr_t));
    header.checksum = cache_checksum(&header, source, out);
    char temporary[4160];
    static unsigned sequence;
    int descriptor = -1;
    for (unsigned attempt = 0; attempt < 8 && descriptor < 0; ++attempt) {
        snprintf(temporary, sizeof temporary, "%s.%u.%u.tmp", path, (unsigned)_getpid(), ++sequence);
        descriptor = _open(temporary, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
    }
    if (descriptor >= 0) {
        FILE* file = _fdopen(descriptor, "wb");
        int valid = 0;
        if (file) {
            valid =
                fwrite(&header, 1, sizeof header, file) == sizeof header &&
                fwrite(source, 1, key->source_size, file) == key->source_size &&
                fwrite(out->bytes, 1, out->size, file) == out->size &&
                fwrite(out->relocations, sizeof(relocation), out->relocation_count, file) == out->relocation_count &&
                !fflush(file) && !_commit(descriptor);
            if (fclose(file))
                valid = 0;
        }
        else
            _close(descriptor);
        if (valid)
            MoveFileExA(temporary, path, 0x9); /* Replace atomically, write through. */
        remove(temporary);
    }
    for (unsigned i = 0; i < out->relocation_count; ++i) {
        relocation entry = out->relocations[i];
        uintptr_t value = relocation_address(entry.symbol);
        memcpy(out->bytes + entry.offset, &value, sizeof value);
    }
}

static void* publish_code(const emitter* out)
{
    if (out->managed > 1)
        return NULL;
    size_t size = sizeof(Syn68kX64CodeHeader) + out->size;
    Syn68kX64CodeHeader* allocation = VirtualAlloc(NULL, size, 0x3000, 0x04);
    if (!allocation)
        return NULL;
    *allocation = (Syn68kX64CodeHeader){.managed = out->managed};
    void* code = allocation + 1;
    memcpy(code, out->bytes, out->size);
    unsigned long old_protection;
    if (!VirtualProtect(allocation, size, 0x20, &old_protection) ||
        !FlushInstructionCache(GetCurrentProcess(), code, out->size)) {
        syn68k_x64_release(code);
        return NULL;
    }
    return code;
}
