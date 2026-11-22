/*+===================================================================
  File:      pef-loader.cpp

  Summary:   Bounded PEF loader for the interpreted PowerPC runtime. Format: Apple's Mac OS Runtime Architectures,
             chapter 8: https://dev.os9.ca/techpubs/mac/runtimehtml/RTArch-89.html ROMAW's older cfm.c provides the
             original section/relocation reference.

  Classes:   Declared: ROMlib_pef_image, bytes, export_info, import_info, library_info, section; Used:
             ROMlib_pef_environment, ROMlib_pef_image, bytes, export_info, import_info, library_info, section,
             transition_run, translation_input,
             std::exception, std::map, std::runtime_error, std::shared_ptr, std::string, std::unique_ptr, std::vector

  Functions: ROMlib_pef_address_section, ROMlib_pef_architecture, ROMlib_pef_contains, ROMlib_pef_error,
             ROMlib_pef_find_section,
             ROMlib_pef_export_count, ROMlib_pef_export_info, ROMlib_pef_find_symbol,
             ROMlib_pef_image::ROMlib_pef_image, ROMlib_pef_image::adjust, ROMlib_pef_image::entry,
             ROMlib_pef_image::import_info::import_info, ROMlib_pef_image::imported, ROMlib_pef_image::relocate,
             ROMlib_pef_import_count, ROMlib_pef_init, ROMlib_pef_is_code, ROMlib_pef_library_count,
             ROMlib_pef_library_info, ROMlib_pef_link, ROMlib_pef_load, ROMlib_pef_main, ROMlib_pef_prepare,
             ROMlib_pef_prepare_arch, ROMlib_pef_share_code, ROMlib_pef_term, ROMlib_pef_translate,
             ROMlib_pef_translate_group, ROMlib_pef_translate_mode, ROMlib_pef_translation_fingerprint,
             ROMlib_pef_unload

  Origin:    https://dev.os9.ca/techpubs/mac/runtimehtml/RTArch-89.html Executor's older cfm.c provides the original
             section/relocation reference.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* Bounded PEF loader for the interpreted PowerPC runtime.
   Format: Apple's Mac OS Runtime Architectures, chapter 8:
   https://dev.os9.ca/techpubs/mac/runtimehtml/RTArch-89.html
   ROMAW's older cfm.c provides the original section/relocation reference. */
#include "synppc/ppc.h"
#include "ppc-jit.h"
#include <vector>
#include <string>
#include <stdexcept>
#include <memory>
#include <cstring>
#include <cstdio>
#include <limits>
#include <map>
#include <iterator>
#include <new>

namespace {
uint16_t be16(const unsigned char* p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

uint32_t be32(const unsigned char* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

void put32(unsigned char* p, uint32_t v)
{
    for (int i = 3; i >= 0; --i) {
        p[i] = v;
        v >>= 8;
    }
}

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct bytes {
    const unsigned char* p;
    size_t n;

    bytes(const void* data, size_t size) : p((const unsigned char*)data), n(size)
    {
    }

    const unsigned char* at(size_t offset, size_t count) const
    {
        require(offset <= n && count <= n - offset, "truncated PEF extent");
        return p + offset;
    }

    uint32_t u32(size_t off) const
    {
        return be32(at(off, 4));
    }

    uint16_t u16(size_t off) const
    {
        return be16(at(off, 2));
    }

    std::string string(size_t off) const
    {
        const char* start = (const char*)at(off, 1);
        const char* end = (const char*)memchr(start, 0, n - off);
        require(end != NULL, "unterminated PEF symbol name");
        return std::string(start, end);
    }
};

uint32_t count(bytes input, size_t& pos)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 5; ++i) {
        unsigned v = *input.at(pos++, 1);
        require(value <= UINT32_MAX >> 7, "overflowed PEF pattern count");
        value = (value << 7) | (v & 127);
        if (!(v & 128))
            return value;
    }
    throw std::runtime_error("overlong PEF pattern count");
}

void unpack(bytes input, unsigned char* output, size_t size)
{
    size_t pos = 0, out = 0;
    auto emit = [&](const unsigned char* source, size_t n) {
        require(out <= size && n <= size - out, "PEF pattern output overflow");
        if (n) {
            if (source)
                memcpy(output + out, source, n);
            else
                memset(output + out, 0, n);
        }
        out += n;
    };
    while (pos < input.n) {
        unsigned op = *input.at(pos++, 1), kind = op >> 5;
        uint32_t amount = op & 31;
        if (!amount)
            amount = count(input, pos);
        if (kind == 0)
            emit(NULL, amount);
        else if (kind == 1) {
            emit(input.at(pos, amount), amount);
            pos += amount;
        }
        else if (kind == 2) {
            uint64_t times = (uint64_t)count(input, pos) + 1;
            const unsigned char* source = input.at(pos, amount);
            pos += amount;
            require(!amount || times <= (size - out) / amount, "PEF repeat overflow");
            if (amount)
                while (times--)
                    emit(source, amount);
        }
        else if (kind == 3 || kind == 4) {
            uint32_t custom = count(input, pos), times = count(input, pos);
            uint64_t total = (uint64_t)amount * (times + UINT64_C(1)) + (uint64_t)custom * times;
            require(total <= size - out, "PEF interleave overflow");
            const unsigned char* common = NULL;
            if (kind == 3) {
                common = input.at(pos, amount);
                pos += amount;
            }
            require(!custom || times <= (input.n - pos) / custom, "PEF interleave source overflow");
            /* Empty patterns must not turn a hostile count into a long empty loop. */
            if (amount || custom) {
                for (uint64_t i = 0; i <= times; ++i) {
                    emit(common, amount);
                    if (i < times) {
                        emit(input.at(pos, custom), custom);
                        pos += custom;
                    }
                }
            }
        }
        else
            throw std::runtime_error("unsupported PEF pattern opcode");
    }
    require(out == size, "short PEF pattern output");
}

enum { pef_code_section = 0, pef_exec_data_section = 6 };

struct section {
    void* allocation;
    std::shared_ptr<void> storage;
    unsigned char* data;
    uint32_t address, size, adjust, kind;

    section() : allocation(NULL), data(NULL), address(0), size(0), adjust(0), kind(0)
    {
    }

    bool executable() const
    {
        return kind == pef_code_section || kind == pef_exec_data_section;
    }
};
}

struct ROMlib_pef_image {
    ROMlib_pef_environment env;
    std::vector<section> sections;
    std::vector<uint32_t> imports;

    struct library_info {
        std::string name;
        uint32_t old_version, version, first, count;
    };

    std::vector<library_info> libraries;

    struct import_info {
        std::string library, name;
        unsigned type;
        bool weak;
        unsigned state;

        import_info() : type(0), weak(false), state(0)
        {
        }
    };

    struct export_info {
        uint32_t value;
        int section;
        unsigned type;
    };

    struct transition_run {
        uint32_t section, offset, count, stride;
    };

    std::vector<import_info> import_info_table;
    std::map<std::string, export_info> exports;
    std::vector<unsigned char> loader_data;
    std::vector<transition_run> transition_runs;
    // Bound optional metadata even if the relocation program visits a vector repeatedly.
    unsigned transition_candidates_left = 4096;
    unsigned link_state = 0;
    char error[256] = {};
    uint32_t main, init, term, architecture;

    ROMlib_pef_image(ROMlib_pef_environment& e) : env(e), main(0), init(0), term(0), architecture(0)
    {
    }

    uint32_t adjust(uint32_t index) const
    {
        require(index < sections.size() && sections[index].allocation, "invalid PEF section index");
        return sections[index].adjust;
    }

    uint32_t imported(uint32_t index)
    {
        require(index < imports.size(), "invalid PEF import index");
        import_info& info = import_info_table[index];
        if (info.state == 2)
            return imports[index];
        require(info.state == 0, "cyclic PEF re-export");
        info.state = 1;
        uint32_t value = 0;
        if (!env.resolve(env.opaque, info.library.c_str(), info.name.c_str(), info.type, info.weak, &value)) {
            require(info.weak, ("unresolved PEF import: " + info.library + ":" + info.name).c_str());
            value = 0;
        }
        imports[index] = value;
        info.state = 2;
        return value;
    }

    uint32_t entry(uint32_t index, uint32_t offset) const
    {
        if (index == UINT32_MAX)
            return 0;
        adjust(index);
        require(offset <= sections[index].size && 8 <= sections[index].size - offset,
                "PEF entry transition vector outside section");
        return sections[index].address + offset;
    }

    void relocate(bytes ops, uint32_t target)
    {
        adjust(target);
        section& s = sections[target];
        uint32_t c = sections.size() > 0 && sections[0].allocation ? sections[0].adjust : 0;
        uint32_t d = sections.size() > 1 && sections[1].allocation ? sections[1].adjust : 0;
        uint32_t import_index = 0;
        size_t position = 0, ip = 0;
        uint64_t budget = (uint64_t)s.size * 8 + ops.n * 16 + 1024;
        auto skip = [&](uint64_t n) {
            require(n <= s.size - position, "PEF relocation address overflow");
            position += (size_t)n;
        };
        auto add = [&](uint32_t value) {
            require(position <= s.size && 4 <= s.size - position, "PEF relocation write outside section");
            put32(s.data + position, be32(s.data + position) + value);
            position += 4;
        };
        auto run = [&](size_t n, uint32_t value) {
            require(n <= (s.size - position) / 4, "PEF relocation run overflow");
            while (n--)
                add(value);
        };
        size_t repeat_at = SIZE_MAX;
        uint32_t repeat_left = 0;
        while (ip < ops.n) {
            require(budget-- != 0, "excessive PEF relocation work");
            size_t start = ip;
            uint16_t op = ops.u16(ip);
            ip += 2;
            if ((op >> 14) == 0) {
                skip(((op >> 6) & 255) * 4);
                run(op & 63, d);
            }
            else if ((op >> 13) == 2) {
                unsigned sub = (op >> 9) & 15, n = (op & 511) + 1;
                if (sub == 0)
                    run(n, c);
                else if (sub == 1)
                    run(n, d);
                else if (sub >= 2 && sub <= 4) {
                    unsigned stride = sub == 2 ? 12 : 8;
                    require(n <= (s.size - position) / stride, "PEF transition-vector relocation overflow");
                    if (sub != 4 && architecture == ROMLIB_PEF_POWERPC && transition_candidates_left) {
                        unsigned recorded = n < transition_candidates_left ? n : transition_candidates_left;
                        try {
                            transition_runs.push_back({target, uint32_t(position), recorded, stride});
                            transition_candidates_left -= recorded;
                        }
                        catch (const std::bad_alloc&) {
                            transition_candidates_left = 0;
                        }
                    }
                    while (n--) {
                        if (sub == 4) {
                            add(d);
                            skip(4);
                        }
                        else {
                            add(c);
                            add(d);
                            if (sub == 2)
                                skip(4);
                        }
                    }
                }
                else if (sub == 5) {
                    require(n <= imports.size() && import_index <= imports.size() - n, "PEF import run overflow");
                    while (n--)
                        add(imported(import_index++));
                }
                else
                    throw std::runtime_error("unknown PEF relocation value opcode");
            }
            else if ((op >> 13) == 3) {
                unsigned sub = (op >> 9) & 15, index = op & 511;
                if (sub == 0) {
                    import_index = index;
                    add(imported(import_index++));
                }
                else if (sub == 1)
                    c = adjust(index);
                else if (sub == 2)
                    d = adjust(index);
                else if (sub == 3)
                    add(adjust(index));
                else
                    throw std::runtime_error("unknown PEF relocation index opcode");
            }
            else if ((op >> 12) == 8)
                skip((op & 4095) + 1);
            else if ((op >> 12) == 9 || (op >> 10) == 0x2c) {
                uint32_t chunks, times;
                if ((op >> 12) == 9) {
                    chunks = ((op >> 8) & 15) + 1;
                    times = (op & 255) + 1;
                }
                else {
                    chunks = ((op >> 6) & 15) + 1;
                    times = ((op & 63) << 16) | ops.u16(ip);
                    ip += 2;
                }
                require(chunks <= start / 2, "PEF repeat before relocation stream");
                if (repeat_at == SIZE_MAX) {
                    repeat_at = start;
                    repeat_left = times;
                }
                else
                    require(repeat_at == start, "nested PEF relocation repeat");
                if (repeat_left) {
                    --repeat_left;
                    ip = start - 2 * chunks;
                }
                else
                    repeat_at = SIZE_MAX;
            }
            else if ((op >> 10) == 0x28 || (op >> 10) == 0x29) {
                uint32_t value = ((op & 1023) << 16) | ops.u16(ip);
                ip += 2;
                if ((op >> 10) == 0x28) {
                    require(value <= s.size, "PEF relocation position outside section");
                    position = value;
                }
                else {
                    import_index = value;
                    add(imported(import_index++));
                }
            }
            else if ((op >> 10) == 0x2d) {
                unsigned sub = (op >> 6) & 15;
                uint32_t index = ((op & 63) << 16) | ops.u16(ip);
                ip += 2;
                if (sub == 0)
                    add(adjust(index));
                else if (sub == 1)
                    c = adjust(index);
                else if (sub == 2)
                    d = adjust(index);
                else
                    throw std::runtime_error("unknown PEF large section opcode");
            }
            else
                throw std::runtime_error("unknown PEF relocation opcode");
        }
        require(repeat_at == SIZE_MAX, "unfinished PEF relocation repeat");
    }
};

extern "C" ROMlib_pef_image* ROMlib_pef_prepare(const void* data, size_t length, ROMlib_pef_environment* env)
{
    return ROMlib_pef_prepare_arch(data, length, env, ROMLIB_PEF_POWERPC);
}

extern "C" ROMlib_pef_image* ROMlib_pef_prepare_arch(const void* data, size_t length, ROMlib_pef_environment* env,
                                                     uint32_t architecture)
{
    if (!env)
        return NULL;
    env->error[0] = 0;
    try {
        require(data && env->allocate && env->release && env->resolve, "missing PEF loader callbacks");
        bytes file(data, length);
        require(architecture == ROMLIB_PEF_POWERPC || architecture == ROMLIB_PEF_M68K, "unsupported PEF architecture");
        require(file.u32(0) == 0x4a6f7921 && file.u32(4) == 0x70656666, "not a PEF container");
        require(file.u32(8) == architecture, "PEF architecture mismatch");
        require(file.u32(12) == 1, "unsupported PEF container version");
        unsigned n = file.u16(32), instances = file.u16(34);
        require(n && instances <= n, "invalid PEF section counts");
        file.at(40, (size_t)n * 28);
        std::unique_ptr<ROMlib_pef_image> image(new ROMlib_pef_image(*env));
        image->architecture = architecture;
        image->sections.resize(n);
        size_t loader_offset = 0, loader_size = 0;
        for (unsigned i = 0; i < n; ++i) {
            size_t at = 40 + 28 * i;
            uint32_t total = file.u32(at + 8), unpacked = file.u32(at + 12), packed = file.u32(at + 16),
                     offset = file.u32(at + 20);
            unsigned kind = *file.at(at + 24, 1), alignment = *file.at(at + 26, 1);
            file.at(offset, packed);
            if (kind == 4) {
                require(!loader_size, "multiple PEF loader sections");
                loader_offset = offset;
                loader_size = packed;
                continue;
            }
            if (i >= instances)
                continue;
            require(kind == pef_code_section || kind == 1 || kind == 2 || kind == 3 || kind == pef_exec_data_section,
                    "unsupported instantiated PEF section");
            require(unpacked <= total && alignment <= 16, "invalid PEF section size/alignment");
            if (kind != 2)
                require(packed == unpacked, "inconsistent unpacked PEF section size");
            section& s = image->sections[i];
            s.size = total;
            s.kind = kind;
            uint32_t allocated = total ? total : 1;
            s.allocation = env->allocate(env->opaque, allocated, 1U << alignment, &s.address);
            require(s.allocation != NULL, "not enough memory for PEF section");
            auto release = env->release;
            void* opaque = env->opaque;
            s.storage = std::shared_ptr<void>(s.allocation, [release, opaque](void* p) { release(opaque, p); });
            require((uint64_t)s.address + allocated <= UINT64_C(0x100000000) && !(s.address & ((1U << alignment) - 1)),
                    "invalid allocated PEF section address");
            s.data = (unsigned char*)s.allocation;
            s.adjust = s.address - file.u32(at + 4);
            memset(s.data, 0, allocated);
            if (kind == 2)
                unpack(bytes(file.at(offset, packed), packed), s.data, unpacked);
            else if (unpacked)
                memcpy(s.data, file.at(offset, packed), unpacked);
        }
        require(loader_size >= 56, "missing or truncated PEF loader");
        bytes loader(file.at(loader_offset, loader_size), loader_size);
        uint32_t libs = loader.u32(24), imports = loader.u32(28), relocs = loader.u32(32),
                 reloc_offset = loader.u32(36), strings = loader.u32(40);
        uint64_t symbols64 = 56 + (uint64_t)libs * 24, headers64 = symbols64 + (uint64_t)imports * 4;
        require(symbols64 <= loader.n && headers64 <= loader.n && (uint64_t)relocs * 12 <= loader.n - headers64,
                "PEF import table overflow");
        size_t symbols = (size_t)symbols64, headers = (size_t)headers64;
        loader.at(headers, (size_t)relocs * 12);
        loader.at(strings, 0);
        loader.at(reloc_offset, 0);
        image->imports.resize(imports);
        image->import_info_table.resize(imports);
        image->loader_data.assign(loader.p, loader.p + loader.n);
        std::vector<unsigned char> assigned(imports, 0);
        for (uint32_t i = 0; i < libs; ++i) {
            size_t at = 56 + (size_t)i * 24;
            uint64_t name_offset = (uint64_t)strings + loader.u32(at);
            require(name_offset < loader.n, "PEF library name outside loader");
            std::string library = loader.string((size_t)name_offset);
            uint32_t number = loader.u32(at + 12), first = loader.u32(at + 16);
            ROMlib_pef_image::library_info li = {library, loader.u32(at + 4), loader.u32(at + 8), first, number};
            image->libraries.push_back(li);
            require(first <= imports && number <= imports - first, "PEF library import range overflow");
            for (uint32_t j = first; j < first + number; ++j) {
                require(!assigned[j], "overlapping PEF import ranges");
                assigned[j] = 1;
                uint32_t symbol = loader.u32(symbols + (size_t)j * 4);
                unsigned symbol_class = (symbol >> 24) & 15;
                bool weak = (symbol & 0x80000000) != 0;
                name_offset = (uint64_t)strings + (symbol & 0x00ffffff);
                require(name_offset < loader.n, "PEF symbol name outside loader");
                std::string name = loader.string((size_t)name_offset);
                ROMlib_pef_image::import_info& info = image->import_info_table[j];
                info.library = library;
                info.name = name;
                info.type = symbol_class;
                info.weak = weak;
            }
        }
        for (size_t i = 0; i < assigned.size(); ++i)
            require(assigned[i], "unassigned PEF import");
        uint32_t hash = loader.u32(44), power = loader.u32(48), export_count = loader.u32(52);
        require(power <= 20, "invalid PEF export hash power");
        uint64_t keys64 = (uint64_t)hash + (UINT64_C(1) << power) * 4;
        uint64_t table64 = keys64 + (uint64_t)export_count * 4;
        if (export_count) {
            require(keys64 <= loader.n && table64 <= loader.n && (uint64_t)export_count * 10 <= loader.n - table64,
                    "PEF export table overflow");
            for (uint32_t i = 0; i < export_count; ++i) {
                size_t at = (size_t)table64 + (size_t)i * 10;
                uint32_t entry = loader.u32(at), name_length = loader.u32((size_t)keys64 + (size_t)i * 4) >> 16;
                uint64_t name_at = (uint64_t)strings + (entry & 0x00ffffff);
                require(name_at <= loader.n && name_length <= loader.n - name_at, "PEF export name outside loader");
                std::string name((const char*)loader.at((size_t)name_at, name_length), name_length);
                ROMlib_pef_image::export_info info = {loader.u32(at + 4), (int16_t)loader.u16(at + 8), entry >> 24};
                if (info.section >= 0) {
                    image->adjust(info.section);
                    require(info.value <= image->sections[info.section].size, "PEF export outside section");
                }
                else
                    require(info.section == -2 || (info.section == -3 && info.value < imports),
                            "invalid PEF export section");
                require(image->exports.insert(std::make_pair(name, info)).second, "duplicate PEF export name");
            }
        }
        image->main = image->entry(loader.u32(0), loader.u32(4));
        image->init = image->entry(loader.u32(8), loader.u32(12));
        image->term = image->entry(loader.u32(16), loader.u32(20));
        return image.release();
    }
    catch (const std::exception& e) {
        snprintf(env->error, sizeof env->error, "%s", e.what());
    }
    catch (...) {
        snprintf(env->error, sizeof env->error, "PEF preparation failed");
    }
    return NULL;
}

/* Private CFM instances keep independently prepared data. Identical PIC code
   can share storage, provided neither image relocates that code section. Code
   with instance-specific relocations must retain its own allocation. */
extern "C" unsigned ROMlib_pef_share_code(ROMlib_pef_image* image, ROMlib_pef_image* source)
{
    if (!image || !source || image == source || image->link_state || image->architecture != source->architecture ||
        image->env.opaque != source->env.opaque || image->env.release != source->env.release)
        return 0;
    unsigned shared = 0;
    try {
        auto relocated = [](ROMlib_pef_image* candidate, unsigned section_index) {
            bytes loader(candidate->loader_data.data(), candidate->loader_data.size());
            size_t headers = 56 + (size_t)loader.u32(24) * 24 + (size_t)loader.u32(28) * 4;
            for (uint32_t i = 0; i < loader.u32(32); ++i) {
                size_t at = headers + (size_t)i * 12;
                if (loader.u16(at) == section_index && loader.u32(at + 4))
                    return true;
            }
            return false;
        };
        for (unsigned i = 0; i < image->sections.size() && i < source->sections.size(); ++i) {
            section& target = image->sections[i];
            const section& original = source->sections[i];
            if (target.kind != pef_code_section || original.kind != pef_code_section || !target.allocation ||
                !original.allocation || target.size != original.size || relocated(image, i) || relocated(source, i) ||
                memcmp(target.data, original.data, target.size))
                continue;
            uint32_t default_address = target.address - target.adjust;
            target.storage = original.storage;
            target.allocation = original.allocation;
            target.data = original.data;
            target.address = original.address;
            target.adjust = target.address - default_address;
            ++shared;
        }
        bytes loader(image->loader_data.data(), image->loader_data.size());
        image->main = image->entry(loader.u32(0), loader.u32(4));
        image->init = image->entry(loader.u32(8), loader.u32(12));
        image->term = image->entry(loader.u32(16), loader.u32(20));
    }
    catch (const std::exception&) {
        /* Malformed relocation metadata is still rejected by normal linking.
           Sharing is optional and must never make an invalid image acceptable. */
    }
    return shared;
}

extern "C" int ROMlib_pef_link(ROMlib_pef_image* image)
{
    if (!image)
        return 0;
    if (image->link_state == 2)
        return 1;
    if (image->link_state)
        return 0;
    image->link_state = 1;
    try {
        for (size_t i = 0; i < image->imports.size(); ++i)
            image->imported(i);
        bytes loader(image->loader_data.data(), image->loader_data.size());
        uint32_t libs = loader.u32(24), imports = loader.u32(28), relocs = loader.u32(32),
                 reloc_offset = loader.u32(36);
        size_t headers = 56 + (size_t)libs * 24 + (size_t)imports * 4;
        for (uint32_t i = 0; i < relocs; ++i) {
            size_t at = headers + (size_t)i * 12;
            uint32_t count = loader.u32(at + 4), first = loader.u32(at + 8);
            uint64_t offset = (uint64_t)reloc_offset + first, extent = (uint64_t)count * 2;
            require(offset <= loader.n && extent <= loader.n - offset, "PEF relocation table overflow");
            image->relocate(bytes(loader.at((size_t)offset, (size_t)extent), (size_t)extent), loader.u16(at));
        }
        image->link_state = 2;
        return 1;
    }
    catch (const std::exception& e) {
        snprintf(image->error, sizeof image->error, "%s", e.what());
        image->link_state = 3;
        return 0;
    }
}

extern "C" int ROMlib_pef_find_symbol(ROMlib_pef_image* image, const char* name, uint32_t* address, unsigned* type)
{
    if (!image || !name || !address)
        return 0;
    image->error[0] = 0;
    try {
        require(image->link_state != 3, "failed PEF image");
        auto it = image->exports.find(name);
        if (it == image->exports.end())
            return 0;
        const ROMlib_pef_image::export_info& info = it->second;
        if (info.section == -3)
            *address = image->imported(info.value);
        else if (info.section == -2)
            *address = info.value;
        else
            *address = image->sections[info.section].address + info.value;
        if (type)
            *type = info.type;
        return 1;
    }
    catch (const std::exception& e) {
        snprintf(image->error, sizeof image->error, "%s", e.what());
        return 0;
    }
}

extern "C" const char* ROMlib_pef_export_info(ROMlib_pef_image* image, unsigned index, uint32_t* address,
                                              unsigned* type)
{
    if (!image || !address || index >= image->exports.size())
        return NULL;
    auto entry = image->exports.begin();
    std::advance(entry, index);
    const char* name = entry->first.c_str();
    return ROMlib_pef_find_symbol(image, name, address, type) ? name : NULL;
}

extern "C" unsigned ROMlib_pef_export_count(ROMlib_pef_image* image)
{
    return image ? image->exports.size() : 0;
}

extern "C" const char* ROMlib_pef_error(ROMlib_pef_image* image)
{
    return image ? image->error : "missing PEF image";
}

extern "C" ROMlib_pef_image* ROMlib_pef_load(const void* data, size_t length, ROMlib_pef_environment* env)
{
    ROMlib_pef_image* image = ROMlib_pef_prepare(data, length, env);
    if (image && !ROMlib_pef_link(image)) {
        snprintf(env->error, sizeof env->error, "%s", image->error);
        delete image;
        image = NULL;
    }
    return image;
}

extern "C" void ROMlib_pef_unload(ROMlib_pef_image* image)
{
    delete image;
}

extern "C" uint32_t ROMlib_pef_main(ROMlib_pef_image* image)
{
    return image ? image->main : 0;
}

extern "C" uint32_t ROMlib_pef_init(ROMlib_pef_image* image)
{
    return image ? image->init : 0;
}

extern "C" uint32_t ROMlib_pef_term(ROMlib_pef_image* image)
{
    return image ? image->term : 0;
}

extern "C" int ROMlib_pef_find_section(ROMlib_pef_image* image, uint32_t address, size_t length, uint32_t* base,
                                       uint32_t* size, unsigned* kind, unsigned* section_index)
{
    if (base)
        *base = 0;
    if (size)
        *size = 0;
    if (kind)
        *kind = 0;
    if (section_index)
        *section_index = 0;
    if (!image)
        return 0;
    for (unsigned index = 0; index < image->sections.size(); ++index) {
        const section& s = image->sections[index];
        if (s.allocation && address >= s.address && (uint64_t)address - s.address <= s.size &&
            length <= s.size - (address - s.address)) {
            if (base)
                *base = s.address;
            if (size)
                *size = s.size;
            if (kind)
                *kind = s.kind;
            if (section_index)
                *section_index = index;
            return 1;
        }
    }
    return 0;
}

extern "C" int ROMlib_pef_address_section(ROMlib_pef_image* image, uint32_t address, size_t length, uint32_t* base,
                                          uint32_t* size, unsigned* kind)
{
    return ROMlib_pef_find_section(image, address, length, base, size, kind, nullptr);
}

extern "C" int ROMlib_pef_contains(ROMlib_pef_image* image, uint32_t address, size_t length)
{
    return ROMlib_pef_address_section(image, address, length, nullptr, nullptr, nullptr);
}

extern "C" int ROMlib_pef_is_code(ROMlib_pef_image* image, uint32_t address)
{
    if (!image)
        return 0;
    unsigned width = image->architecture == ROMLIB_PEF_M68K ? 2 : 4;
    if (address & (width - 1))
        return 0;
    for (const auto& s : image->sections)
        if (s.allocation && s.executable() && address >= s.address &&
            (uint64_t)address + width <= (uint64_t)s.address + s.size)
            return 1;
    return 0;
}

extern "C" unsigned ROMlib_pef_translate(ROMlib_pef_image* image, ROMlib_ppc_translation_cache* cache)
{
    return ROMlib_pef_translate_mode(image, cache, 0);
}

extern "C" unsigned ROMlib_pef_translate_mode(ROMlib_pef_image* image, ROMlib_ppc_translation_cache* cache,
                                              unsigned mode)
{
    return ROMlib_pef_translate_group(&image, 1, cache, mode);
}

namespace {
struct translation_input {
    std::vector<romaw_ppc_code_range> code;
    std::vector<uint32_t> entries;

    translation_input(ROMlib_pef_image* const* images, size_t count)
    {
        std::vector<uint32_t> exports, local;
        auto add_transition = [](const ROMlib_pef_image& image, uint32_t address, std::vector<uint32_t>& entries) {
            for (const auto& s : image.sections)
                if (s.allocation && address >= s.address && uint64_t(address) + 8 <= uint64_t(s.address) + s.size) {
                    entries.push_back(be32(s.data + (address - s.address)));
                    break;
                }
        };
        for (size_t index = 0; index < count; ++index) {
            const auto* image = images[index];
            if (!image || image->architecture != ROMLIB_PEF_POWERPC || image->link_state != 2)
                continue;
            for (const auto& s : image->sections)
                if (s.allocation && s.executable())
                    code.push_back({s.data, s.address, s.size});
            for (uint32_t transition : {image->init, image->main, image->term})
                if (transition)
                    add_transition(*image, transition, entries);
            for (const auto& symbol : image->exports) {
                const auto& info = symbol.second;
                if (info.section < 0 && info.section != -2)
                    continue; // Re-exports are prepared by the owning fragment.
                uint64_t address = info.value;
                if (info.section >= 0)
                    address += image->sections[info.section].address;
                if (address > UINT32_MAX)
                    continue;
                if (info.type == 2) // PEF transition vector.
                    add_transition(*image, uint32_t(address), exports);
                else if (info.type == 0 || info.type == 4) // Code or linker glue.
                    exports.push_back(uint32_t(address));
            }
            // A later relocation may have changed the first word.
            for (const auto& run : image->transition_runs) {
                const auto* vector = image->sections[run.section].data + run.offset;
                for (unsigned i = 0; i < run.count; ++i)
                    local.push_back(be32(vector + i * run.stride));
            }
        }
        entries.insert(entries.end(), exports.begin(), exports.end());
        entries.insert(entries.end(), local.begin(), local.end());
    }
};
}

extern "C" unsigned ROMlib_pef_translate_group(ROMlib_pef_image* const* images, size_t count,
                                               ROMlib_ppc_translation_cache* cache, unsigned mode)
{
    if (!images || !cache || !count)
        return 0;
    try {
        translation_input input(images, count);
        return romaw_ppc_translate_image(cache, input.code.data(), input.code.size(), input.entries.data(),
                                         input.entries.size(), mode);
    }
    catch (...) {
        // AOT planning must not prevent the linked fragment from executing.
        return 0;
    }
}

extern "C" int ROMlib_pef_translation_fingerprint(ROMlib_pef_image* const* images, size_t count, unsigned mode,
                                                  uint64_t* key)
{
    if (!images || !count || !key || (mode & ~ROMLIB_PPC_TRANSLATE_750))
        return 0;
    try {
        const translation_input input(images, count);
        if (input.code.empty())
            return 0;
        uint64_t hash = *key;
        auto byte = [&](unsigned char value) { hash = (hash ^ value) * UINT64_C(1099511628211); };
        auto integer = [&](uint64_t value) {
            for (unsigned shift = 0; shift < 64; shift += 8)
                byte(value >> shift);
        };
        integer(mode);
        integer(input.code.size());
        for (const auto& range : input.code) {
            integer(range.address);
            integer(range.size);
            for (uint32_t i = 0; i < range.size; ++i)
                byte(range.data[i]);
        }
        integer(input.entries.size());
        for (uint32_t entry : input.entries)
            integer(entry);
        *key = hash;
        return 1;
    }
    catch (...) {
        return 0;
    }
}

extern "C" uint32_t ROMlib_pef_architecture(ROMlib_pef_image* image)
{
    return image ? image->architecture : 0;
}

extern "C" unsigned ROMlib_pef_library_count(ROMlib_pef_image* image)
{
    return image ? image->libraries.size() : 0;
}

extern "C" unsigned ROMlib_pef_import_count(ROMlib_pef_image* image)
{
    return image ? image->imports.size() : 0;
}

extern "C" const char* ROMlib_pef_library_info(ROMlib_pef_image* image, unsigned index, uint32_t* values)
{
    if (!image || index >= image->libraries.size() || !values)
        return NULL;
    const auto& li = image->libraries[index];
    values[0] = li.old_version;
    values[1] = li.version;
    values[2] = li.first;
    values[3] = li.count;
    return li.name.c_str();
}
