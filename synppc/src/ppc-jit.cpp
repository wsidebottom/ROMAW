/*+===================================================================
  File:      ppc-jit.cpp

  Summary:   Implements ppc jit support for synppc PowerPC execution and fragment loading.

  Classes:   Declared: ROMlib_ppc_translation_cache, cache_reader, memory_emitter, native_block, native_context,
             native_store, native_heat, native_heat_table, memory_form, system_operand_kind, system_memory_operand,
             code_range_plan, native_allocation, native_retirement;
             Used: ROMlib_ppc_translation_cache, cache_reader, romaw_ppc_jit_service_scope, memory_emitter, native_block,
             native_context, native_store, std::bad_alloc, std::runtime_error, std::string, std::unique_ptr,
             std::unordered_map, std::unordered_set, std::vector, std::wstring

  Functions: ROMlib_ppc_jit_available, ROMlib_ppc_translate_range, ROMlib_ppc_translation_cache_create,
             ROMlib_ppc_translation_cache_destroy, ROMlib_ppc_translation_cache_load, ROMlib_ppc_translation_cache_save,
             romaw_ppc_jit_scope::romaw_ppc_jit_scope, romaw_ppc_jit_scope::~romaw_ppc_jit_scope,
             romaw_ppc_jit_service_scope::romaw_ppc_jit_service_scope,
             romaw_ppc_jit_service_scope::~romaw_ppc_jit_service_scope,
             romaw_ppc_jit_switch, romaw_ppc_native_step, romaw_ppc_native_system_step,
             romaw_ppc_native_memory_code, ROMlib_ppc_translate_range_mode,
             ROMlib_ppc_translate_published_750, romaw_ppc_translate_image

  Origin:    Existing repository source in synppc/src; original attribution and provenance comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original bounded x86 backend. Guarded x64 terminal memory operations
   access the declared linear RAM only. Generated leaf bodies and frame-free semantic tails retain checked callbacks;
   every cached block checks its source words. */
#include "sysdeps.h"
#include "ppc-jit.h"
#include "ppc-native-fused-state.h"
#include "ppc-native-bus.h"
#include "synppc/system750.h"
#include "cpu/ppc/ppc-cpu.hpp"
#include "cpu/vm.hpp"
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <stdexcept>
#include <string>
#include <cstdio>
#include <atomic>
#include <utility>
#include <type_traits>
#include <fcntl.h>
#if defined(_WIN32)
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

#if __has_include("generated/ppc-jit-build-id.h")
#include "generated/ppc-jit-build-id.h"
#endif
#ifndef SYN_PPC_AOT_BUILD_ID
#define SYN_PPC_AOT_BUILD_ID ""
#endif

#if (defined(_WIN32) || defined(__unix__))
#define ROMAW_PPC_NATIVE_JIT 1
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#else
#define ROMAW_PPC_NATIVE_JIT 0
#endif

struct ROMlib_ppc_translation_cache {
    void* implementation;
};

#if ROMAW_PPC_NATIVE_JIT
namespace {
constexpr unsigned max_words = 32, max_blocks = 256, hot_threshold = 8;
constexpr unsigned max_native_block_size = 32 * 1024;

void release_code(void* code, size_t size)
{
#if defined(_WIN32)
    VirtualFree(code, 0, MEM_RELEASE);
#else
    munmap(code, size);
#endif
}

void* publish_code(const unsigned char* bytes, size_t size)
{
#if defined(_WIN32)
    void* code = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!code)
        return nullptr;
    memcpy(code, bytes, size);
    DWORD previous;
    if (!VirtualProtect(code, size, PAGE_EXECUTE_READ, &previous) ||
        !FlushInstructionCache(GetCurrentProcess(), code, size)) {
        release_code(code, size);
        return nullptr;
    }
#else
    void* code = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED)
        return nullptr;
    memcpy(code, bytes, size);
    if (mprotect(code, size, PROT_READ | PROT_EXEC)) {
        release_code(code, size);
        return nullptr;
    }
    __builtin___clear_cache((char*)code, (char*)code + size);
#endif
    return code;
}

enum class system_operand_kind {
    none,
    integer,
    floating,
    reservation,
    multiple,
    string,
    cache_control,
    external,
    spr_write
};

struct system_memory_operand {
    unsigned size = 0;
    unsigned reg = 0, update_reg = 0;
    unsigned string_limit = 0;
    unsigned selector = 0;
    bool write = false, reverse = false, sign_extend = false;
    system_operand_kind kind = system_operand_kind::none;
};

struct native_allocation {
    void* code = nullptr;
    size_t size = 0;

    ~native_allocation()
    {
        if (code)
            release_code(code, size);
    }
};

struct native_retirement;

struct native_block {
    uint64 last_used = 0;
    std::vector<uint32> words;
    void* code = nullptr;
    size_t code_size = 0;
    std::shared_ptr<native_allocation> allocation;
    bool records = false;
    bool reads_xer = false, writes_xer = false;
    bool branches = false, links = false;
    bool reads_cr = false, reads_ctr = false, writes_ctr = false;
    bool reads_lr = false, writes_lr = false;
    bool reads_fpscr = false, writes_fpscr = false;
    unsigned indirect = 0;
    uint32 next_pc = 0, link_pc = 0;
    uint32 ctr = 0, lr = 0;
    uint32 cr = 0, so = 0, xer = 0;
    uint32 fpscr = 0;
    uint32 fp_condition = 0, fp_raised = 0;
    uint32 fp_control = 0, fp_saved_control = 0, fp_status = 0, fp_flags = 0, fp_integer = 0;
    uint64 fp_conversion = 0;
    uint64 fp_result = 0, fp_reference = 0;
    uint32 fp_class = 0;
    uint32 fp_single_first = 0, fp_single_second = 0;
    unsigned char fp_x87_environment[528] = {};
    ppc_native_fused_state fused = {};
    bool semantic = false;
    bool slow_path = false, tail_completed = false;
    bool architectural = false;
    void (*tail)(powerpc_cpu*, native_block*) = nullptr;
    const native_retirement* description = nullptr;
    uint32* continuation = nullptr;
    bool memory = false;
    unsigned char* ram = nullptr;
    uint32 ram_address = 0, ram_size = 0, executed = 0;
    uint32 exception_vector = 0;
    ROMlib_ppc_system750* system = nullptr;
    bool writes_system_msr = false;
    uint32 msr_value = 0;
    ppc_native_memory_frame native_frame = {};
    ppc_native_memory_kernel::entry native_entry = nullptr;
    bool native_completed = false;
    bool breakpoint_pending = false, invalid_string = false;
    uint32 stream_index = 0, stream_shift = 0, stream_value = 0;
    system_memory_operand system_operand;
    uint32 operand_address = 0, operand_count = 0;
    uint64 operand_value = 0;
    bool source_mismatch = false;
};

// Copy only retirement metadata before entering generated code. Device callbacks
// may replace the cache record while its pinned machine code is still running.
struct native_retirement {
    unsigned words;
    uint32 opcode, link_pc;
    bool writes_fpscr, writes_xer, records, links, writes_lr, writes_ctr;
    bool semantic, branches, architectural, writes_system_msr;
    system_memory_operand system_operand;

    explicit native_retirement(const native_block& block)
        : words(block.words.size()), opcode(block.words.back()), link_pc(block.link_pc),
          writes_fpscr(block.writes_fpscr), writes_xer(block.writes_xer), records(block.records), links(block.links),
          writes_lr(block.writes_lr), writes_ctr(block.writes_ctr), semantic(block.semantic), branches(block.branches),
          architectural(block.architectural), writes_system_msr(block.writes_system_msr),
          system_operand(block.system_operand)
    {
    }
};

bool publish_native_block(native_block& block, const unsigned char* bytes, size_t size)
{
    auto allocation = std::make_shared<native_allocation>();
    allocation->size = size;
    allocation->code = publish_code(bytes, size);
    if (!allocation->code)
        return false;
    block.code = allocation->code;
    block.code_size = size;
    block.allocation = std::move(allocation);
    return true;
}

struct native_heat {
    uint32 pc = 0, opcode = 0;
    unsigned hits = 0;
    bool architectural = false;
};

struct native_heat_table {
    native_heat buckets[64][4];
    unsigned char next[64] = {};
};

using native_key = uint64;

native_key translation_key(uint32 pc, uint32 opcode, bool architectural)
{
    // One effective PC can fetch several opcodes through different 750
    // mappings or instruction-cache views. The low word is always the PC.
    return pc | (architectural ? uint64(opcode) << 32 : 0);
}

struct native_store {
    using block_map = std::unordered_map<native_key, std::unique_ptr<native_block>>;
    using omitted_map = std::unordered_map<native_key, std::vector<uint32>>;

    unsigned busy = 0;
    unsigned limit = max_blocks;
    block_map blocks, system_blocks;
    omitted_map omitted[2];
    uint64 clock = 0;
    std::unique_ptr<native_heat_table> heat;
    std::shared_ptr<ppc_native_memory_kernel> memory_kernel;

    size_t size() const
    {
        return blocks.size() + system_blocks.size();
    }

    bool hot(uint32 pc, uint32 opcode, bool architectural, bool changed)
    {
        // Track interpreted starts separately so one-off code cannot consume
        // executable block slots. Fold address bits for regularly spaced code.
        if (!heat)
            heat.reset(new native_heat_table);
        unsigned bucket = ((pc >> 2) ^ (pc >> 8) ^ (pc >> 16)) & 63;
        for (auto& sample : heat->buckets[bucket])
            if (sample.hits && sample.pc == pc && sample.architectural == architectural &&
                (!architectural || sample.opcode == opcode)) {
                if (changed || sample.opcode != opcode) {
                    sample.opcode = opcode;
                    sample.hits = 0;
                }
                if (sample.hits < hot_threshold)
                    ++sample.hits;
                return sample.hits == hot_threshold;
            }
        auto& sample = heat->buckets[bucket][heat->next[bucket]++ & 3];
        sample.pc = pc;
        sample.opcode = opcode;
        sample.architectural = architectural;
        sample.hits = 1;
        return false;
    }

    native_block* publish(block_map& destination, native_key key, std::unique_ptr<native_block> block)
    {
        block->last_used = ++clock;
        auto& slot = destination.emplace(key, nullptr).first->second;
        slot = std::move(block);
        auto& admitted = *slot;
        if (size() <= limit)
            return &admitted;

        // Publish first so an allocation failure cannot evict useful code.
        // An unexecuted AOT record is the cheapest victim.
        block_map* pool = nullptr;
        block_map::iterator victim;
        for (auto* map : {&blocks, &system_blocks}) {
            for (auto it = map->begin(); it != map->end(); ++it) {
                if (it->second.get() == &admitted)
                    continue;
                if (!it->second->last_used) {
                    map->erase(it);
                    return &admitted;
                }
                if (!pool || it->second->last_used < victim->second->last_used) {
                    pool = map;
                    victim = it;
                }
            }
        }
        if (pool)
            pool->erase(victim);
        return &admitted;
    }
};

struct native_context {
    ROMlib_ppc_environment* environment;
    powerpc_cpu* owner = nullptr;
    bool disabled = false;
    bool in_service = false;
    native_store local;
    native_store* store = &local;

    explicit native_context(ROMlib_ppc_environment* env) : environment(env)
    {
        if (env->translation_cache) {
            auto shared = static_cast<native_store*>(env->translation_cache->implementation);
            store = shared;
        }
        ++store->busy;
    }

    ~native_context()
    {
        --store->busy;
    }
};

native_context* active_context;

bool cache_in_host_service(const native_store& store)
{
    return active_context && active_context->store == &store && active_context->in_service;
}

constexpr size_t cache_byte_limit = 32 * 1024 * 1024;
constexpr long cache_minimum_size = 8 + 64 + 3 * 4; // Magic, build ID, ABI, count and CRC.
constexpr uint32 cache_architectural = 1u << 14;
constexpr uint32 cache_omitted = 1u << 15;
#if defined(_WIN32)
constexpr uint32 cache_abi = 1;
#else
constexpr uint32 cache_abi = 2;
#endif

const char cache_build[] = SYN_PPC_AOT_BUILD_ID;

uint32 cache_crc(const unsigned char* bytes, size_t size)
{
    uint32 crc = ~0u;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

uint32 cache_flags(const native_block& b)
{
    return b.records | (b.reads_xer << 1) | (b.writes_xer << 2) | (b.branches << 3) | (b.links << 4) |
           (b.reads_cr << 5) | (b.reads_ctr << 6) | (b.writes_ctr << 7) | (b.reads_lr << 8) | (b.writes_lr << 9) |
           (b.reads_fpscr << 10) | (b.writes_fpscr << 11) | (b.memory << 12) | (b.semantic << 13) |
           (b.architectural ? cache_architectural : 0);
}

#if defined(_WIN32)
std::wstring cache_wide(const char* path)
{
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
    if (!count)
        throw std::runtime_error("invalid cache filename");
    std::wstring result(count, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, &result[0], count))
        throw std::runtime_error("invalid cache filename");
    return result;
}
#endif

FILE* cache_open(const char* path, bool writing)
{
#if defined(_WIN32)
    return _wfopen(cache_wide(path).c_str(), writing ? L"wb" : L"rb");
#else
    return fopen(path, writing ? "wb" : "rb");
#endif
}

FILE* cache_temporary(const char* path, std::string& temporary)
{
#if defined(_WIN32)
    static std::atomic<unsigned> sequence{0};
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        temporary =
            std::string(path) + ".tmp." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(++sequence);
        int descriptor = _wopen(cache_wide(temporary.c_str()).c_str(),
                                _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY | _O_NOINHERIT, _S_IREAD | _S_IWRITE);
        if (descriptor < 0)
            continue;
        FILE* file = _fdopen(descriptor, "wb");
        if (!file)
            _close(descriptor);
        return file;
    }
    // Do not remove a sibling owned by another publisher on failed creation.
    temporary.clear();
    return nullptr;
#else
    std::string pattern = std::string(path) + ".tmp.XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back(0);
    int descriptor = mkstemp(name.data());
    if (descriptor < 0)
        return nullptr;
    temporary = name.data();
    FILE* file = fdopen(descriptor, "wb");
    if (!file)
        close(descriptor);
    return file;
#endif
}

bool cache_sync(FILE* file)
{
    if (fflush(file))
        return false;
#if defined(_WIN32)
    return !_commit(_fileno(file));
#else
    return !fsync(fileno(file));
#endif
}

struct cache_reader {
    const std::vector<unsigned char>& bytes;
    size_t position = 0;

    explicit cache_reader(const std::vector<unsigned char>& data) : bytes(data)
    {
    }

    const unsigned char* take(size_t size)
    {
        if (position > bytes.size() || size > bytes.size() - position)
            throw std::runtime_error("truncated native cache");
        auto result = bytes.data() + position;
        position += size;
        return result;
    }

    uint32 word()
    {
        auto p = take(4);
        return (uint32)p[0] | (uint32)p[1] << 8 | (uint32)p[2] << 16 | (uint32)p[3] << 24;
    }
};

unsigned spr_number(uint32 opcode)
{
    return ((opcode >> 16) & 31) | ((opcode >> 6) & 0x3e0);
}

struct memory_form {
    unsigned primary;
    bool update;
    bool reverse;
    bool malformed;

    bool writes() const
    {
        return primary == 36 || primary == 38 || primary == 44 || primary == 47 ||
               (primary >= 52 && primary != 533 && primary != 597);
    }

    unsigned operand_size() const
    {
        switch (primary) {
        case 34:
        case 38:
            return 1;
        case 40:
        case 42:
        case 44:
            return 2;
        case 50:
        case 54:
            return 8;
        default:
            return 4;
        }
    }
};

memory_form decode_memory(uint32 opcode)
{
    constexpr memory_form malformed = {0, false, false, true};
    unsigned primary = opcode >> 26;
    unsigned ra = (opcode >> 16) & 31, reg = (opcode >> 21) & 31;
    if (primary == 46 || primary == 47) {
        if (primary == 46 && ra >= reg)
            return malformed;
        return {primary, false, false, false};
    }
    bool update = primary & 1;
    bool reverse = false;
    if (primary == 31) {
        unsigned xo = (opcode >> 1) & 1023;
        switch (xo) {
        case 150: // stwcx.
            return opcode & 1 ? memory_form{xo, false, false, false} : malformed;
        case 20:  // lwarx
        case 983: // stfiwx
        case 661: // stswx
        case 725: // stswi
            return opcode & 1 ? malformed : memory_form{xo, false, false, false};
        case 1014: // dcbz
            return opcode & romaw_ppc_cache_reserved_mask(xo) ? malformed : memory_form{xo, false, false, false};
        case 533:   // lswx
        case 597: { // lswi
            unsigned rb = (opcode >> 11) & 31;
            bool indexed = xo == 533;
            unsigned limit = romaw_ppc_string_load_limit(reg, ra, rb, indexed);
            unsigned registers = indexed ? 1 : ((rb ? rb : 32) + 3) / 4;
            return (opcode & 1) || registers > limit ? malformed : memory_form{xo, false, false, false};
        }
        default:
            break;
        }
        update = xo & 32;
        switch (xo & ~32u) {
        case 23:
            primary = 32;
            break; // lwzx
        case 87:
            primary = 34;
            break; // lbzx
        case 279:
            primary = 40;
            break; // lhzx
        case 343:
            primary = 42;
            break; // lhax
        case 151:
            primary = 36;
            break; // stwx
        case 215:
            primary = 38;
            break; // stbx
        case 407:
            primary = 44;
            break; // sthx
        case 599:
            primary = 50;
            break; // lfdx
        case 535:
            primary = 48;
            break; // lfsx
        case 727:
            primary = 54;
            break; // stfdx
        case 663:
            primary = 52;
            break; // stfsx
        default:
            switch (xo) {
            case 534:
                primary = 32;
                break; // lwbrx
            case 790:
                primary = 40;
                break; // lhbrx
            case 662:
                primary = 36;
                break; // stwbrx
            case 918:
                primary = 44;
                break; // sthbrx
            default:
                return {};
            }
            reverse = true;
            break;
        }
        if (opcode & 1)
            return malformed; // Indexed scalar memory instructions have no Rc form.
    }
    else
        primary &= ~1u;
    switch (primary) {
    case 32:
    case 34:
    case 40:
    case 42:
        if (update && ra == reg)
            return malformed;
        break;
    case 36:
    case 38:
    case 44:
    case 48:
    case 50:
    case 52:
    case 54:
        break;
    default:
        return {};
    }
    return update && !ra ? malformed : memory_form{primary, update, reverse, false};
}

bool memory_opcode(uint32 opcode)
{
    return decode_memory(opcode).primary != 0;
}

enum class flat_cache_op { none, hint, boundary };

flat_cache_op flat_cache_operation(uint32 opcode)
{
#if !PPC_DECODE_CACHE && !PPC_ENABLE_JIT
    // The flat interpreter has no data cache. Its legacy instruction-cache
    // invalidation is also disabled; ROMAW validates native source words.
    if (opcode == 0x4c00012c || opcode == 0x7c0004ac || opcode == 0x7c0006ac)
        return flat_cache_op::boundary; // isync, sync, eieio
    if ((opcode >> 26) == 31 && !(opcode & 0x03e00001)) {
        switch ((opcode >> 1) & 1023) {
        case 54:  // dcbst
        case 86:  // dcbf
        case 246: // dcbtst
        case 278: // dcbt
        case 470: // dcbi
            return flat_cache_op::hint;
        case 982: // icbi
            return flat_cache_op::boundary;
        }
    }
#else
    (void)opcode;
#endif
    return flat_cache_op::none;
}

unsigned fpscr_write_operation(uint32 opcode)
{
    if ((opcode >> 26) == 63) {
        unsigned xo = (opcode >> 1) & 1023;
        switch (xo) {
        case 38: // mtfsb1
        case 70: // mtfsb0
            return opcode & 0x001ff800 ? 0 : xo;
        case 134: // mtfsfi
            return opcode & 0x007f0800 ? 0 : xo;
        case 711: // mtfsf
            return opcode & 0x02010000 ? 0 : xo;
        }
    }
    return 0;
}

uint32 fpscr_write_mask(uint32 opcode, unsigned operation)
{
    uint32 mask = 0;
    if (operation == 38 || operation == 70)
        mask = 0x80000000u >> ((opcode >> 21) & 31);
    else if (operation == 134)
        mask = 0xf0000000u >> (((opcode >> 23) & 7) * 4);
    else if (operation == 711) {
        unsigned fields = (opcode >> 17) & 255;
        for (unsigned field = 0; field < 8; ++field)
            if (fields & (0x80 >> field))
                mask |= 0xf0000000u >> (field * 4);
    }
    // FEX and VX are derived. Bit 20 is reserved.
    return mask & ~0x60000800u;
}

unsigned native_fp_arithmetic_operation(uint32 opcode);
unsigned native_fp_estimate_operation(uint32 opcode);

bool native_fp_fused_operation(uint32 opcode)
{
    unsigned primary = opcode >> 26;
    return (primary == 59 || primary == 63) && ((opcode >> 1) & 31) >= 28;
}

bool inline_supported(uint32 opcode)
{
    unsigned primary = opcode >> 26;
    if (native_fp_arithmetic_operation(opcode) || native_fp_fused_operation(opcode) ||
        native_fp_estimate_operation(opcode))
        return true;
    if (flat_cache_operation(opcode) != flat_cache_op::none)
        return true;
    if (memory_opcode(opcode))
        return true;
    if (primary == 18)
        return true;
    if (primary == 16)
        // Defined BO encodings; ignored/reserved control bits retain fallback.
        return (0x001f3f3fu >> ((opcode >> 21) & 31)) & 1;
    if (primary == 19) {
        unsigned xo = (opcode >> 1) & 1023, bo = (opcode >> 21) & 31;
        if (xo == 0) // mcrf.
            return !(opcode & 0x0063f801);
        switch (xo) {
        case 257: // crand.
        case 129: // crandc.
        case 289: // creqv.
        case 225: // crnand.
        case 33:  // crnor.
        case 449: // cror.
        case 417: // crorc.
        case 193: // crxor.
            return !(opcode & 1);
        default:
            break;
        }
        // The 750 reserves the complete five-bit branch-hint field.
        return !(opcode & 0xf800) && ((0x001f3f3fu >> bo) & 1) && (xo == 16 || (xo == 528 && (bo & 4)));
    }
    if (primary == 10 || primary == 11)
        return !(opcode & 0x00600000); /* Reserved bit and 64-bit L form. */
    if (primary == 14 || primary == 15 || (primary >= 24 && primary <= 29))
        return true;
    if (primary == 7 || primary == 8 || primary == 12 || primary == 13)
        return true;
    if (primary == 20 || primary == 21 || primary == 23)
        return true;
    if (primary == 63) {
        if (fpscr_write_operation(opcode))
            return true;
        unsigned xo = (opcode >> 1) & 1023;
        if (xo == 0 || xo == 32) // FCMPU / FCMPO
            return !(opcode & 0x00600001);
        if (xo == 14 || xo == 15) // FCTIW / FCTIWZ
            return !(opcode & 0x001f0000);
        if (((opcode >> 1) & 31) == 23)
            return true; // FSEL uses all three operand fields.
        if (xo == 583)
            return !(opcode & 0x001ff800); // MFFS has no source operand fields.
        if (xo == 64)
            return !(opcode & 0x0063f801); // MCRFS reserved fields and Rc.
        return !(opcode & 0x001f0000) && (xo == 72 || xo == 40 || xo == 264 || xo == 136);
    }
    if (primary != 31)
        return false;
    /* Use exact ten-bit secondary opcodes; OE forms are enabled individually. */
    switch ((opcode >> 1) & 1023) {
    case 339: // mfspr.
    case 467: // mtspr; only the three user registers compile.
        return !(opcode & 1) && (spr_number(opcode) == 1 || spr_number(opcode) == 8 || spr_number(opcode) == 9);
    case 19: // mfcr.
        return !(opcode & 0x001ff801);
    case 144: // mtcrf, without the later one-field form.
        return !(opcode & 0x00100801);
    case 512: // mcrxr.
        return !(opcode & 0x007ff801);
    case 0:  /* cmp */
    case 32: /* cmpl */
        return !(opcode & 0x00600001);
    case 26:  /* cntlzw */
    case 954: /* extsb */
    case 922: /* extsh */
    case 104: /* neg, without OE */
    case 616: /* nego */
    case 234: /* addme, without OE */
    case 202: /* addze, without OE */
    case 232: /* subfme, without OE */
    case 200: /* subfze, without OE */
    case 746: /* addmeo */
    case 714: /* addzeo */
    case 744: /* subfmeo */
    case 712: /* subfzeo */
        return !(opcode & 0x0000f800);
    case 266:  /* add */
    case 778:  /* addo */
    case 552:  /* subfo */
    case 10:   /* addc, without OE */
    case 8:    /* subfc, without OE */
    case 138:  /* adde, without OE */
    case 136:  /* subfe, without OE */
    case 522:  /* addco */
    case 520:  /* subfco */
    case 650:  /* addeo */
    case 648:  /* subfeo */
    case 235:  /* mullw, without OE */
    case 747:  /* mullwo */
    case 491:  /* divw */
    case 1003: /* divwo */
    case 459:  /* divwu */
    case 971:  /* divwuo */
    case 75:   /* mulhw */
    case 11:   /* mulhwu */
    case 40:   /* subf */
    case 28:   /* and */
    case 60:   /* andc */
    case 124:  /* nor */
    case 284:  /* eqv */
    case 316:  /* xor */
    case 412:  /* orc */
    case 444:  /* or */
    case 476:  /* nand */
    case 24:   /* slw */
    case 536:  /* srw */
    case 792:  /* sraw */
    case 824:  /* srawi */
        return true;
    default:
        return false;
    }
}

// Architectural operands must pass through MMU/cache/breakpoint helpers.
// A singleton keeps fetch, interrupt, trace and enabled-FP boundaries precise.
bool block_inline(uint32 opcode, bool architectural)
{
    // The 750 dispatcher performs ISYNC's context synchronization after
    // retirement. Architectural singleton blocks never prefetch a successor.
    if (architectural && opcode == 0x4c00012c)
        return true;
    return inline_supported(opcode) &&
           !(architectural && (memory_opcode(opcode) || flat_cache_operation(opcode) != flat_cache_op::none));
}

/* Only instructions actually recognized by the current runtime are eligible.
   Opcode 6 belongs to the embedding service adapter and remains a dispatch
   boundary. Live operands still reach the kernel's existing checks. */
bool supported(uint32 opcode, powerpc_cpu* cpu, bool architectural = false)
{
    if (architectural && romaw_ppc_750_excludes_instruction(opcode, cpu->romaw_instruction(opcode)->name))
        return false;
    if (inline_supported(opcode))
        return true;
    unsigned primary = opcode >> 26;
    if (primary == 6)
        return false;
    unsigned xo = (opcode >> 1) & 1023;
    if (architectural) {
        if (decode_memory(opcode).malformed)
            return false;
        using operation = romaw_ppc_system_operation;
        auto decoded = romaw_ppc_decode_system_operation(opcode);
        if (decoded != operation::other)
            return decoded != operation::invalid;
    }
    if (primary == 31) {
        if (architectural) {
            if (xo == 370) // tlbia
                return false;
            if (uint32 mask = romaw_ppc_cache_reserved_mask(xo))
                return !(opcode & mask);
            if (uint32 mask = romaw_ppc_mmu_reserved_mask(xo))
                return !(opcode & mask);
            if (xo == 339 || xo == 467 || xo == 371)
                return romaw_ppc_spr_selector(opcode) != 0;
        }
        if (xo == 20 || xo == 150 || xo == 983)
            return true; // ROMAW's reservation and raw FPR-store adapters.
    }
    const auto* instruction = cpu->romaw_instruction(opcode);
    return instruction->mnemo != PPC_I(INVALID) &&
           (!architectural || !romaw_ppc_750_excludes_instruction(opcode, instruction->name));
}

bool ends_block(uint32 opcode)
{
    unsigned primary = opcode >> 26;
    unsigned xo = (opcode >> 1) & 1023;
    const auto memory = decode_memory(opcode);
    // Commit a rounding-mode change before any native floating store can use it.
    return native_fp_arithmetic_operation(opcode) || native_fp_fused_operation(opcode) ||
           native_fp_estimate_operation(opcode) || !inline_supported(opcode) || memory.primary == 20 ||
           (fpscr_write_mask(opcode, fpscr_write_operation(opcode)) & 3) ||
           flat_cache_operation(opcode) == flat_cache_op::boundary || primary == 18 || primary == 16 ||
           (primary == 19 && (xo == 16 || xo == 528));
}

uint32 read_word(const unsigned char* p)
{
    return (uint32)p[0] << 24 | (uint32)p[1] << 16 | (uint32)p[2] << 8 | p[3];
}

uint32 direct_branch_target(uint32 opcode, uint32 pc)
{
    bool unconditional = (opcode >> 26) == 18;
    uint32 displacement = opcode & (unconditional ? 0x03fffffc : 0xfffc);
    if (displacement & (unconditional ? 0x02000000 : 0x8000))
        displacement |= unconditional ? 0xfc000000 : 0xffff0000;
    return (opcode & 2 ? 0 : pc) + displacement;
}

bool branch_always(uint32 opcode)
{
    return (opcode >> 26) == 18 || ((opcode >> 21) & 20) == 20;
}

bool ctr_veneer_target(const romaw_ppc_code_range& range, uint32 pc, uint32 opcode, uint32& target)
{
    if (opcode >> 26 != 19 || ((opcode >> 1) & 1023) != 528 || !inline_supported(opcode))
        return false;
    const uint32 offset = pc - range.address;
    if (offset < 12)
        return false;
    const auto* bytes = range.data + offset;
    uint32 high = read_word(bytes - 12), low = read_word(bytes - 8), move = read_word(bytes - 4);
    unsigned reg = (high >> 21) & 31;
    if ((high & 0xFC1F0000) != 0x3C000000 || move != (0x7C0903A6 | (reg << 21)))
        return false;
    uint32 fields = (reg << 21) | (reg << 16);
    uint32 address = high << 16;
    if ((low & 0xFFFF0000) == (0x60000000 | fields)) // ori rN,rN,lo
        address |= low & 0xFFFF;
    else if (reg && (low & 0xFFFF0000) == (0x38000000 | fields)) // addi rN,rN,lo
        address += uint32(int32(int16(low)));
    else
        return false;
    target = address & ~3u;
    return true;
}

void emit_word(std::vector<unsigned char>& bytes, uint32 value)
{
    for (unsigned i = 0; i < 4; ++i)
        bytes.push_back(value >> (i * 8));
}

size_t emit_local_jump(std::vector<unsigned char>& bytes, unsigned condition)
{
    if (condition)
        bytes.insert(bytes.end(), {0x0f, (unsigned char)condition});
    else
        bytes.push_back(0xe9);
    size_t offset = bytes.size();
    emit_word(bytes, 0);
    return offset;
}

void point_jump(std::vector<unsigned char>& bytes, size_t offset, size_t target)
{
    uint32 distance = target - (offset + 4);
    for (unsigned i = 0; i < 4; ++i)
        bytes[offset + i] = distance >> (i * 8);
}

void point_jump_here(std::vector<unsigned char>& bytes, size_t offset)
{
    point_jump(bytes, offset, bytes.size());
}

void widen_single(std::vector<unsigned char>& bytes)
{
    // EAX starts with binary32 bits. RDX holds the sign and R9 holds the exponent.
    bytes.insert(bytes.end(), {0x89, 0xc2, 0x81, 0xe2});
    emit_word(bytes, 0x80000000);
    bytes.insert(bytes.end(), {0x48, 0xc1, 0xe2, 32, 0x41, 0x89, 0xc1, 0x25});
    emit_word(bytes, 0x7fffff);
    bytes.insert(bytes.end(), {0x41, 0xc1, 0xe9, 23, 0x41, 0x81, 0xe1});
    emit_word(bytes, 255);
    size_t zero_exponent = emit_local_jump(bytes, 0x84);
    bytes.insert(bytes.end(), {0x41, 0x81, 0xf9}); // cmp r9d,255
    emit_word(bytes, 255);
    size_t special = emit_local_jump(bytes, 0x84);
    bytes.insert(bytes.end(), {0x41, 0x81, 0xc1}); // add r9d,896
    emit_word(bytes, 896);
    size_t normal = emit_local_jump(bytes, 0);
    point_jump_here(bytes, special);
    bytes.insert(bytes.end(), {0x41, 0xb9}); // mov r9d,2047
    emit_word(bytes, 2047);
    point_jump_here(bytes, normal);
    bytes.insert(bytes.end(), {0x48, 0xc1, 0xe0, 29}); // shl rax,29
    size_t packed = emit_local_jump(bytes, 0);

    point_jump_here(bytes, zero_exponent);
    bytes.insert(bytes.end(), {0x85, 0xc0}); // test eax,eax
    size_t zero = emit_local_jump(bytes, 0x84);
    // Renormalize a binary32 subnormal. R8 can hold the update EA while CL shifts.
    bytes.insert(bytes.end(), {0x44, 0x0f, 0xbd, 0xc8, 0x44, 0x0f, 0xb3, 0xc8}); // bsr r9d,eax; btr eax,r9d
    bytes.insert(bytes.end(), {0x41, 0x89, 0xc8, 0xb9});                         // mov r8d,ecx; mov ecx,52
    emit_word(bytes, 52);
    bytes.insert(bytes.end(), {0x44, 0x29, 0xc9, 0x48, 0xd3, 0xe0, 0x44, 0x89, 0xc1});
    bytes.insert(bytes.end(), {0x41, 0x81, 0xc1}); // add r9d,874
    emit_word(bytes, 874);

    point_jump_here(bytes, packed);
    point_jump_here(bytes, zero);
    bytes.insert(bytes.end(), {0x49, 0xc1, 0xe1, 52, 0x4c, 0x09, 0xc8, 0x48, 0x09, 0xd0});
}

void narrow_single(std::vector<unsigned char>& bytes)
{
    // Match fp_store_single_convert: copy the specified fields except in the
    // binary32 subnormal range, where conversion uses the guest rounding mode.
    bytes.insert(bytes.end(), {0x48, 0x89, 0xc2, 0x48, 0xc1, 0xea, 52, 0x81, 0xe2});
    emit_word(bytes, 0x7ff);
    bytes.insert(bytes.end(), {0x81, 0xea}); // sub edx,874
    emit_word(bytes, 874);
    bytes.insert(bytes.end(), {0x83, 0xfa, 22});
    size_t subnormal = emit_local_jump(bytes, 0x86);
    bytes.insert(bytes.end(), {0x48, 0x89, 0xc2, 0x48, 0xc1, 0xea, 32, 0x81, 0xe2});
    emit_word(bytes, 0xc0000000);
    bytes.insert(bytes.end(), {0x48, 0xc1, 0xe8, 29, 0x25});
    emit_word(bytes, 0x3fffffff);
    bytes.insert(bytes.end(), {0x09, 0xd0});
    size_t copied = emit_local_jump(bytes, 0);

    point_jump_here(bytes, subnormal);
    // movq xmm0,rax; cvtsd2ss xmm0,xmm0; movd eax,xmm0.
    bytes.insert(bytes.end(), {0x66, 0x48, 0x0f, 0x6e, 0xc0, 0xf2, 0x0f, 0x5a, 0xc0, 0x66, 0x0f, 0x7e, 0xc0});
    point_jump_here(bytes, copied);
}

struct memory_emitter {
    const powerpc_cpu* cpu;
    const native_block* block;

    void operator()(std::vector<unsigned char>& bytes, unsigned instruction, const void* address, unsigned operand = 0,
                    bool escaped = false, bool wide = false) const
    {
        uintptr pointer = (uintptr)address;
        uintptr cpu_base = (uintptr)cpu, block_base = (uintptr)block;
        bool state = pointer >= cpu_base && pointer - cpu_base < sizeof(*cpu);
        uintptr base = state ? cpu_base : block_base;
        if (!state && (pointer < base || pointer - base >= sizeof(*block)))
            throw std::logic_error("invalid native operand");
        /* R10 carries the current CPU, R11 the block's live status scratch. */
        bytes.push_back(0x41 | (wide ? 8 : 0) | (operand & 8 ? 4 : 0));
        if (escaped)
            bytes.push_back(0x0f);
        bytes.push_back(instruction);
        bytes.push_back((state ? 0x82 : 0x83) | ((operand & 7) << 3));
        emit_word(bytes, pointer - base);
    }
};

size_t emit_native_entry_guard(std::vector<unsigned char>& bytes, const memory_emitter& emit)
{
    emit(bytes, 0x8b, &emit.block->native_entry, 0, false, true);
    bytes.insert(bytes.end(), {0x48, 0x85, 0xc0});
    return emit_local_jump(bytes, 0x84);
}

void emit_native_packet_call(std::vector<unsigned char>& bytes, const memory_emitter& emit)
{
    const auto& frame = emit.block->native_frame;
    emit(bytes, 0xff, &frame.serial); // Each scalar starts a new host floating environment.
    emit(bytes, 0x8b, &emit.block->native_entry, 0, false, true);
    // The generated leaf's CPU/invocation bases are volatile in both x64 ABIs.
    bytes.insert(bytes.end(), {0x41, 0x52, 0x41, 0x53}); // push r10; push r11.
#if defined(_WIN32)
    emit(bytes, 0x8d, &frame, 1, false, true);         // lea rcx,frame.
    bytes.insert(bytes.end(), {0x48, 0x83, 0xec, 40}); // Align and reserve Win64 shadow space.
#else
    emit(bytes, 0x8d, &frame, 7, false, true); // lea rdi,frame.
    bytes.insert(bytes.end(), {0x48, 0x83, 0xec, 8});
#endif
    bytes.insert(bytes.end(), {0xff, 0xd0}); // call the live emitted entry.
#if defined(_WIN32)
    bytes.insert(bytes.end(), {0x48, 0x83, 0xc4, 40});
#else
    bytes.insert(bytes.end(), {0x48, 0x83, 0xc4, 8});
#endif
    bytes.insert(bytes.end(), {0x41, 0x5b, 0x41, 0x5a}); // pop r11; pop r10.
}

void emit_native_completed(std::vector<unsigned char>& bytes, const memory_emitter& emit)
{
    emit(bytes, 0xc6, &emit.block->native_completed);
    bytes.push_back(1);
}

void emit_state_effect(std::vector<unsigned char>& bytes, const memory_emitter& emit, uint32 pc, unsigned selector,
                       const void* value, bool rfi = false)
{
    auto& frame = emit.block->native_frame;
    emit(bytes, 0xc7, &frame.address);
    emit_word(bytes, pc);
    emit(bytes, 0xc7, &frame.selector);
    emit_word(bytes, selector);
    emit(bytes, 0xc7, &frame.access);
    emit_word(bytes, rfi);
    emit(bytes, 0x8b, value);
    emit(bytes, 0x89, &frame.value);
    auto unavailable = emit_native_entry_guard(bytes, emit);
    emit_native_packet_call(bytes, emit);
    emit_native_completed(bytes, emit);
    point_jump_here(bytes, unavailable);
}

void emit_effective_address(std::vector<unsigned char>& bytes, const memory_emitter& emit, powerpc_cpu& cpu,
                            unsigned ra, bool indexed, unsigned rb, uint32 displacement)
{
    if (indexed) {
        emit(bytes, 0x8b, &cpu.gpr(rb));
        if (ra)
            emit(bytes, 0x03, &cpu.gpr(ra));
        return;
    }
    if (ra) {
        emit(bytes, 0x8b, &cpu.gpr(ra));
        bytes.push_back(0x05); // add eax, signed displacement
    }
    else
        bytes.push_back(0xb8); // mov eax, signed displacement
    emit_word(bytes, displacement);
}

void emit_register_move(std::vector<unsigned char>& bytes, const memory_emitter& emit, powerpc_cpu& cpu, uint32 opcode,
                        const romaw_ppc_register_move& move)
{
    using form = romaw_ppc_register_form;
    auto* gpr = &cpu.gpr((opcode >> 21) & 31);
    if (move.form == form::constant) {
        bytes.push_back(0xb8); // mov eax, immediate
        emit_word(bytes, move.value);
        emit(bytes, 0x89, gpr);
        return;
    }

    emit(bytes, 0x8b, &emit.block->system, 2, false, true); // mov rdx, live system pointer
    bool indexed = move.form == form::indexed_segment;
    if (indexed) {
        emit(bytes, 0x8b, &cpu.gpr((opcode >> 11) & 31), 1);
        bytes.insert(bytes.end(), {0xc1, 0xe9, 28}); // shr ecx, 28
    }
    auto system_word = [&] {
        bytes.push_back(move.write ? 0x89 : 0x8b);
        if (indexed)
            bytes.insert(bytes.end(), {0x84, 0x8a}); // [rdx+rcx*4+disp32]
        else
            bytes.push_back(0x82); // [rdx+disp32]
        emit_word(bytes, move.offset + (move.form == form::time_base_high ? sizeof(uint32) : 0));
    };
    if (move.write)
        emit(bytes, 0x8b, gpr);
    else
        system_word();
    if (move.mask != UINT32_MAX) {
        bytes.push_back(0x25); // and eax, immediate
        emit_word(bytes, move.mask);
    }
    if (move.write)
        system_word();
    else
        emit(bytes, 0x89, gpr);
}

void emit_tlbie(std::vector<unsigned char>& bytes, const memory_emitter& emit, powerpc_cpu& cpu, unsigned rb)
{
    using mmu = ROMlib_ppc_mmu750;
    constexpr size_t mmu_offset = offsetof(ROMlib_ppc_system750, mmu);
    constexpr size_t tlb_offset = mmu_offset + offsetof(mmu, tlb);
    constexpr size_t replace_offset = mmu_offset + offsetof(mmu, replace);
    constexpr size_t set_size = sizeof(std::declval<mmu>().tlb[0][0]);
    constexpr size_t bank_size = sizeof(std::declval<mmu>().tlb[0]);
    constexpr size_t replace_bank_size = sizeof(std::declval<mmu>().replace[0]);
    static_assert(set_size % 16 == 0 && set_size < 128 && bank_size / set_size == 64 && replace_bank_size == 64,
                  "unsupported native TLB set layout");

    // EA[14:19] selects the same set in both TLBs, independent of the tag.
    emit(bytes, 0x8b, &cpu.gpr(rb));
    bytes.insert(bytes.end(), {0xc1, 0xe8, 12, 0x83, 0xe0, 63});      // shr eax,12; and eax,63
    bytes.insert(bytes.end(), {0x6b, 0xc8, (unsigned char)set_size}); // imul ecx,eax,set_size
    emit(bytes, 0x8b, &emit.block->system, 2, false, true);
    bytes.insert(bytes.end(), {0x66, 0x0f, 0xef, 0xc0}); // pxor xmm0,xmm0
    for (unsigned bank = 0; bank < 2; ++bank) {
        for (unsigned offset = 0; offset < set_size; offset += 16) {
            bytes.insert(bytes.end(), {0xf3, 0x0f, 0x7f, 0x84, 0x0a}); // movdqu [rdx+rcx+disp32],xmm0
            emit_word(bytes, tlb_offset + bank * bank_size + offset);
        }
        bytes.insert(bytes.end(), {0xc6, 0x84, 0x02}); // mov byte [rdx+rax+disp32],0
        emit_word(bytes, replace_offset + bank * replace_bank_size);
        bytes.push_back(0);
    }
}

void emit_tlbsync(std::vector<unsigned char>& bytes, const memory_emitter& emit)
{
    emit(bytes, 0x8b, &emit.block->system, 2, false, true);
    bytes.insert(bytes.end(), {0x8b, 0x82}); // mov eax,[rdx+inputs]
    emit_word(bytes, offsetof(ROMlib_ppc_system750, inputs));
    bytes.push_back(0xa9); // test eax,TLBISYNC
    emit_word(bytes, ROMLIB_PPC_750_TLBISYNC);
    bytes.insert(bytes.end(), {0x0f, 0x95, 0xc0, 0x0f, 0xb6, 0xc0, 0x89, 0x82}); // setne; movzx; store
    emit_word(bytes, offsetof(ROMlib_ppc_system750, tlbsync_wait));
}

void emit_rfi(std::vector<unsigned char>& bytes, const memory_emitter& emit)
{
    emit(bytes, 0x8b, &emit.block->system, 2, false, true); // mov rdx, live system pointer
    bytes.insert(bytes.end(), {0x8b, 0x82});                // mov eax,[rdx+msr]
    emit_word(bytes, offsetof(ROMlib_ppc_system750, mmu) + offsetof(ROMlib_ppc_mmu750, msr));
    bytes.push_back(0x25); // and eax, keep fields outside the SRR1 mask
    emit_word(bytes, ~ROMLIB_PPC_750_SRR1_MSR_MASK);
    bytes.insert(bytes.end(), {0x8b, 0x8a}); // mov ecx,[rdx+srr1]
    emit_word(bytes, offsetof(ROMlib_ppc_system750, srr1));
    bytes.insert(bytes.end(), {0x81, 0xe1}); // and ecx, SRR1 MSR fields
    emit_word(bytes, ROMLIB_PPC_750_SRR1_MSR_MASK);
    bytes.insert(bytes.end(), {0x09, 0xc8}); // or eax,ecx
    emit(bytes, 0x89, &emit.block->msr_value);
    bytes.insert(bytes.end(), {0x8b, 0x82}); // mov eax,[rdx+srr0]
    emit_word(bytes, offsetof(ROMlib_ppc_system750, srr0));
    bytes.insert(bytes.end(), {0x83, 0xe0, 0xfc}); // and eax,~3
    emit(bytes, 0x89, &emit.block->next_pc);
}

void emit_trap_word(std::vector<unsigned char>& bytes, const memory_emitter& emit, powerpc_cpu& cpu, uint32 opcode)
{
    unsigned options = (opcode >> 21) & 31;
    if (!options)
        return;
    emit(bytes, 0x8b, &cpu.gpr((opcode >> 16) & 31)); // mov eax,rA
    if ((opcode >> 26) == 3) {
        bytes.push_back(0x3d); // cmp eax, sign-extended immediate
        emit_word(bytes, uint32(int32(int16(opcode))));
    }
    else
        emit(bytes, 0x3b, &cpu.gpr((opcode >> 11) & 31)); // cmp eax,rB

    // TO selects signed LT/GT, equality, and unsigned LT/GT.
    const unsigned conditions[] = {0x87, 0x82, 0x84, 0x8f, 0x8c};
    size_t taken[5];
    unsigned count = 0;
    for (unsigned bit = 0; bit < 5; ++bit)
        if (options & (1u << bit))
            taken[count++] = emit_local_jump(bytes, conditions[bit]);
    size_t not_taken = emit_local_jump(bytes, 0);
    for (unsigned i = 0; i < count; ++i)
        point_jump_here(bytes, taken[i]);
    emit(bytes, 0xc7, &emit.block->exception_vector);
    emit_word(bytes, 0x700);
    point_jump_here(bytes, not_taken);
}

// Return the word in R8, the new exception causes in EDX, and FR/FI in ECX.
void emit_fp_integer(std::vector<unsigned char>& bytes, const memory_emitter& emit, powerpc_cpu& cpu, uint32 opcode)
{
    emit(bytes, 0x8b, &cpu.fpr_dw((opcode >> 11) & 31), 9, false, true);
    bytes.insert(bytes.end(), {0x4c, 0x89, 0xc8, 0x48, 0x0f, 0xba, 0xf0, 63});       // mov rax,r9; btr rax,63
    bytes.insert(bytes.end(), {0x45, 0x31, 0xc0});                                   // xor r8d,r8d
    bytes.insert(bytes.end(), {0x48, 0x89, 0xc1, 0x48, 0xc1, 0xe9, 52, 0x81, 0xf9}); // mov; shr rcx,52; cmp
    emit_word(bytes, 1023 + 31);
    size_t large = emit_local_jump(bytes, 0x87);
    bytes.insert(bytes.end(), {0x81, 0xf9}); // cmp ecx,1023
    emit_word(bytes, 1023);
    size_t below_one = emit_local_jump(bytes, 0x82);

    // Split at bit 52 minus the unbiased exponent. Right-align the integer
    // in R8 and left-align the fraction in RAX so bit 63 denotes one half.
    bytes.insert(bytes.end(), {0xf7, 0xd9, 0x81, 0xc1}); // neg ecx; add ecx,1075
    emit_word(bytes, 1023 + 52);
    bytes.insert(bytes.end(), {0x48, 0xc1, 0xe0, 12, 0x48, 0xc1, 0xe8, 12});     // shl; shr, discard exponent
    bytes.insert(bytes.end(), {0x48, 0x0f, 0xba, 0xe8, 52, 0x49, 0x89, 0xc0});   // bts rax,52; mov r8,rax
    bytes.insert(bytes.end(), {0x49, 0xd3, 0xe8, 0xf7, 0xd9, 0x48, 0xd3, 0xe0}); // shr r8,cl; neg ecx; shl rax,cl
    size_t split = emit_local_jump(bytes, 0);

    point_jump_here(bytes, below_one);
    // Below one half the raw magnitude already sorts between zero and the
    // rounding cutoff. In [0.5,1), extract the fraction including its hidden bit.
    bytes.insert(bytes.end(), {0x48, 0xb9}); // movabs rcx,0.5
    emit_word(bytes, 0);
    emit_word(bytes, 0x3fe00000);
    bytes.insert(bytes.end(), {0x48, 0x39, 0xc8}); // cmp rax,rcx
    size_t below_half = emit_local_jump(bytes, 0x82);
    bytes.insert(bytes.end(), {0x48, 0xc1, 0xe0, 12, 0x48, 0xd1, 0xe8}); // shl rax,12; shr rax,1
    bytes.insert(bytes.end(), {0x48, 0x0f, 0xba, 0xe8, 63});             // bts rax,63
    point_jump_here(bytes, split);
    point_jump_here(bytes, below_half);

    bytes.insert(bytes.end(), {0x31, 0xd2, 0x48, 0x85, 0xc0}); // xor edx,edx; test rax,rax
    size_t exact = emit_local_jump(bytes, 0x84);
    bytes.push_back(0xba); // mov edx,FI
    emit_word(bytes, 0x00020000);
    if (((opcode >> 1) & 1023) == 14) {
        emit(bytes, 0x8b, &emit.block->fpscr, 1);
        bytes.insert(bytes.end(), {0x83, 0xe1, 3}); // and ecx,RN
        size_t nearest = emit_local_jump(bytes, 0x84);
        bytes.insert(bytes.end(), {0x83, 0xf9, 1}); // cmp ecx,RZ
        size_t truncate = emit_local_jump(bytes, 0x84);
        // RN=2 increments a positive magnitude; RN=3 increments a negative one.
        bytes.insert(bytes.end(), {0x4c, 0x89, 0xc8, 0x48, 0xc1, 0xe8, 63, 0x83, 0xe1, 1, 0x39, 0xc8});
        size_t opposite_sign = emit_local_jump(bytes, 0x85);
        size_t directed_up = emit_local_jump(bytes, 0);

        point_jump_here(bytes, nearest);
        bytes.insert(bytes.end(), {0x48, 0xb9}); // movabs rcx,half; cmp rax,rcx
        emit_word(bytes, 0);
        emit_word(bytes, 0x80000000);
        bytes.insert(bytes.end(), {0x48, 0x39, 0xc8});
        size_t above_half = emit_local_jump(bytes, 0x87);
        size_t under_half = emit_local_jump(bytes, 0x82);
        bytes.insert(bytes.end(), {0x41, 0xf6, 0xc0, 1}); // test r8b,1 (nearest-even tie)
        size_t even = emit_local_jump(bytes, 0x84);
        point_jump_here(bytes, directed_up);
        point_jump_here(bytes, above_half);
        bytes.insert(bytes.end(), {0x49, 0xff, 0xc0, 0x81, 0xca}); // inc r8; or edx,FR
        emit_word(bytes, 0x00040000);
        for (size_t branch : {truncate, opposite_sign, under_half, even})
            point_jump_here(bytes, branch);
    }
    point_jump_here(bytes, exact);

    // Compare the rounded magnitude to INT32_MAX + sign before narrowing.
    bytes.push_back(0xb9);
    emit_word(bytes, 0x7fffffff);
    bytes.insert(bytes.end(), {0x4c, 0x89, 0xc8, 0x48, 0xc1, 0xe8, 63, 0x48, 0x01, 0xc1, 0x49, 0x39, 0xc8});
    size_t overflow = emit_local_jump(bytes, 0x87);
    bytes.insert(bytes.end(), {0xf7, 0xd8, 0x41, 0x31, 0xc0, 0x41, 0x29, 0xc0}); // neg eax; xor/sub r8d,eax
    bytes.insert(bytes.end(), {0x89, 0xd1, 0xc1, 0xe2, 8, 0x81, 0xe2});          // mov ecx,edx; shl; and edx,XX
    emit_word(bytes, 0x02000000);
    size_t valid = emit_local_jump(bytes, 0);

    point_jump_here(bytes, large);
    point_jump_here(bytes, overflow);
    bytes.push_back(0xba); // mov edx,VXCVI
    emit_word(bytes, 0x00000100);
    bytes.insert(bytes.end(), {0x41, 0xb8}); // mov r8d,INT32_MIN
    emit_word(bytes, 0x80000000);
    bytes.insert(bytes.end(), {0x4c, 0x89, 0xc8, 0x48, 0x0f, 0xba, 0xf0, 63, 0x48, 0xb9}); // abs bits; movabs rcx,inf
    emit_word(bytes, 0);
    emit_word(bytes, 0x7ff00000);
    bytes.insert(bytes.end(), {0x48, 0x39, 0xc8}); // cmp rax,rcx
    size_t nan = emit_local_jump(bytes, 0x87);
    bytes.insert(bytes.end(), {0x4d, 0x85, 0xc9}); // test r9,r9
    size_t negative = emit_local_jump(bytes, 0x88);
    bytes.insert(bytes.end(), {0x41, 0xff, 0xc8}); // dec r8d (INT32_MAX)
    size_t positive = emit_local_jump(bytes, 0);
    point_jump_here(bytes, nan);
    bytes.insert(bytes.end(), {0x49, 0x0f, 0xba, 0xe1, 51}); // bt r9,51 (quiet NaN bit)
    size_t quiet = emit_local_jump(bytes, 0x82);
    bytes.insert(bytes.end(), {0x81, 0xca}); // or edx,VXSNAN
    emit_word(bytes, 0x01000000);
    for (size_t branch : {negative, positive, quiet})
        point_jump_here(bytes, branch);
    bytes.insert(bytes.end(), {0x31, 0xc9}); // xor ecx,ecx
    point_jump_here(bytes, valid);
}

// Return the comparison condition in R8D and any new exception causes in EDX.
void emit_fp_compare(std::vector<unsigned char>& bytes, const memory_emitter& emit, powerpc_cpu& cpu, uint32 opcode)
{
    bool ordered = opcode & 64;
    emit(bytes, 0x8b, &cpu.fpr_dw((opcode >> 16) & 31), 0, false, true);
    emit(bytes, 0x8b, &cpu.fpr_dw((opcode >> 11) & 31), 2, false, true);
    // Shift away each sign, then compare the magnitudes to infinity.
    bytes.insert(bytes.end(), {0x49, 0x89, 0xc0, 0x49, 0xd1, 0xe0}); // mov r8,rax; shl r8,1
    bytes.insert(bytes.end(), {0x49, 0x89, 0xd1, 0x49, 0xd1, 0xe1}); // mov r9,rdx; shl r9,1
    bytes.insert(bytes.end(), {0x48, 0xb9});                         // movabs rcx, infinity << 1
    emit_word(bytes, 0);
    emit_word(bytes, 0xffe00000);
    bytes.insert(bytes.end(), {0x49, 0x39, 0xc8}); // cmp r8,rcx
    size_t nan_a = emit_local_jump(bytes, 0x87);
    bytes.insert(bytes.end(), {0x49, 0x39, 0xc9}); // cmp r9,rcx
    size_t nan_b = emit_local_jump(bytes, 0x87);
    bytes.insert(bytes.end(), {0x4d, 0x09, 0xc8}); // or r8,r9; signed zeros compare equal
    size_t zeros = emit_local_jump(bytes, 0x84);
    bytes.insert(bytes.end(), {0x48, 0x39, 0xd0}); // cmp rax,rdx
    size_t equal = emit_local_jump(bytes, 0x84);
    // Raw unsigned ordering is reversed whenever either operand is negative.
    bytes.insert(bytes.end(), {0x19, 0xc9, 0x48, 0x09, 0xd0, 0x48, 0xc1, 0xf8, 63}); // sbb; or; sar
    bytes.insert(bytes.end(), {0x31, 0xc1, 0x83, 0xe1, 4, 0x44, 0x8d, 0x41, 4});     // xor; and; lea r8d,[rcx+4]
    size_t different = emit_local_jump(bytes, 0);
    point_jump_here(bytes, zeros);
    point_jump_here(bytes, equal);
    bytes.insert(bytes.end(), {0x41, 0xb8}); // mov r8d,EQ
    emit_word(bytes, 2);
    point_jump_here(bytes, different);
    bytes.insert(bytes.end(), {0x31, 0xd2}); // xor edx,edx
    size_t numeric = emit_local_jump(bytes, 0);

    point_jump_here(bytes, nan_a);
    point_jump_here(bytes, nan_b);
    bytes.insert(bytes.end(), {0x49, 0x39, 0xc8}); // cmp r8,rcx
    size_t inspect_b = emit_local_jump(bytes, 0x86);
    bytes.insert(bytes.end(), {0x48, 0x0f, 0xba, 0xe0, 51}); // bt rax,51 (quiet NaN bit)
    size_t snan_a = emit_local_jump(bytes, 0x83);
    point_jump_here(bytes, inspect_b);
    bytes.insert(bytes.end(), {0x49, 0x39, 0xc9}); // cmp r9,rcx
    size_t quiet = emit_local_jump(bytes, 0x86);
    bytes.insert(bytes.end(), {0x48, 0x0f, 0xba, 0xe2, 51}); // bt rdx,51
    size_t snan_b = emit_local_jump(bytes, 0x83);
    point_jump_here(bytes, quiet);
    bytes.push_back(0xba); // mov edx, ordered ? VXVC : 0
    emit_word(bytes, ordered ? 0x00080000 : 0);
    size_t quiet_done = emit_local_jump(bytes, 0);
    point_jump_here(bytes, snan_a);
    point_jump_here(bytes, snan_b);
    bytes.push_back(0xba); // mov edx,VXSNAN
    emit_word(bytes, 0x01000000);
    if (ordered) {
        // An enabled signaling-NaN exception suppresses VXVC.
        emit(bytes, 0xf7, &emit.block->fpscr);
        emit_word(bytes, 0x80);
        size_t enabled = emit_local_jump(bytes, 0x85);
        bytes.insert(bytes.end(), {0x81, 0xca}); // or edx,VXVC
        emit_word(bytes, 0x00080000);
        point_jump_here(bytes, enabled);
    }
    point_jump_here(bytes, quiet_done);
    bytes.insert(bytes.end(), {0x41, 0xb8}); // mov r8d,UN
    emit_word(bytes, 1);
    point_jump_here(bytes, numeric);
    emit(bytes, 0x89, &emit.block->fp_condition, 8);
}

void string_transfer(std::vector<unsigned char>& bytes, bool writing, unsigned first, const memory_emitter& emit,
                     powerpc_cpu& cpu)
{
    // ECX is the number of bytes left, R8 the RAM pointer, R9 the GPR base and EDX the GPR index.
    emit(bytes, 0x8d, &cpu.gpr(0), 9, false, true);
    bytes.push_back(0xba); // mov edx,first
    emit_word(bytes, first);
    bytes.insert(bytes.end(), {0x83, 0xf9, 4});
    size_t to_tail = emit_local_jump(bytes, 0x82);
    size_t next_word = bytes.size();
    if (writing)
        bytes.insert(bytes.end(), {0x41, 0x8b, 0x04, 0x91, 0x0f, 0xc8, 0x41, 0x89, 0x00});
    else
        bytes.insert(bytes.end(), {0x41, 0x8b, 0x00, 0x0f, 0xc8, 0x41, 0x89, 0x04, 0x91});
    bytes.insert(bytes.end(), {0x49, 0x83, 0xc0, 4, 0xff, 0xc2, 0x83, 0xe2, 31, 0x83, 0xe9, 4, 0x83, 0xf9, 4});
    size_t repeat = emit_local_jump(bytes, 0x83);
    point_jump(bytes, repeat, next_word);

    point_jump_here(bytes, to_tail);
    bytes.insert(bytes.end(), {0x85, 0xc9}); // test ecx,ecx
    size_t exhausted = emit_local_jump(bytes, 0x84);
    size_t one_byte;
    if (writing) {
        bytes.insert(bytes.end(), {0x41, 0x8b, 0x04, 0x91, 0x0f, 0xc8}); // Load and swap the final GPR.
        bytes.insert(bytes.end(), {0x83, 0xf9, 1});
        one_byte = emit_local_jump(bytes, 0x84);
        bytes.insert(bytes.end(), {0x66, 0x41, 0x89, 0x00, 0x83, 0xf9, 2});
        size_t two_bytes = emit_local_jump(bytes, 0x84);
        bytes.insert(bytes.end(), {0xc1, 0xe8, 16, 0x41, 0x88, 0x40, 2});
        size_t three_bytes = emit_local_jump(bytes, 0);
        point_jump_here(bytes, one_byte);
        bytes.insert(bytes.end(), {0x41, 0x88, 0x00});
        point_jump_here(bytes, two_bytes);
        point_jump_here(bytes, three_bytes);
    }
    else {
        bytes.insert(bytes.end(), {0x83, 0xf9, 1});
        one_byte = emit_local_jump(bytes, 0x84);
        bytes.insert(bytes.end(), {0x41, 0x0f, 0xb7, 0x00, 0x86, 0xc4, 0xc1, 0xe0, 16, 0x83, 0xf9, 2});
        size_t two_bytes = emit_local_jump(bytes, 0x84);
        bytes.insert(bytes.end(), {0x41, 0x0f, 0xb6, 0x48, 2, 0xc1, 0xe1, 8, 0x09, 0xc8});
        size_t three_bytes = emit_local_jump(bytes, 0);
        point_jump_here(bytes, one_byte);
        bytes.insert(bytes.end(), {0x41, 0x0f, 0xb6, 0x00, 0xc1, 0xe0, 24});
        point_jump_here(bytes, two_bytes);
        point_jump_here(bytes, three_bytes);
        bytes.insert(bytes.end(), {0x41, 0x89, 0x04, 0x91});
    }
    point_jump_here(bytes, exhausted);
}

void emit_sse(std::vector<unsigned char>& bytes, const memory_emitter& emit, unsigned prefix, unsigned instruction,
              const void* address, unsigned reg = 0)
{
    if (prefix)
        bytes.push_back(prefix);
    emit(bytes, instruction, address, reg, true);
}

#include "ppc-native-fp.h"
#include "generated/ppc-native-fused.h"

std::vector<size_t> emit_native_fp_fused(std::vector<unsigned char>& bytes, const memory_emitter& emit,
                                         powerpc_cpu& cpu, uint32 opcode)
{
    std::vector<size_t> exceptional;
    const unsigned registers[] = {(opcode >> 16) & 31, (opcode >> 11) & 31, (opcode >> 6) & 31};
    const void* operands[] = {&emit.block->fused.a, &emit.block->fused.b, &emit.block->fused.c};
    for (unsigned index = 0; index != 3; ++index) {
        auto source = reinterpret_cast<const unsigned char*>(&cpu.fpr_dw(registers[index]));
        auto destination = reinterpret_cast<const unsigned char*>(operands[index]);
        emit(bytes, 0x8b, source + 4);
        bytes.push_back(0x25);
        emit_word(bytes, 0x7ff00000);
        bytes.push_back(0x3d);
        emit_word(bytes, 0x7ff00000);
        exceptional.push_back(emit_local_jump(bytes, 0x84));
        emit(bytes, 0x8b, source);
        emit(bytes, 0x89, destination);
        emit(bytes, 0x8b, source + 4);
        emit(bytes, 0x89, destination + 4);
    }
    emit(bytes, 0x8b, &emit.block->fpscr);
    emit(bytes, 0x89, &emit.block->fused.fpscr);

    // The exact integer sequence is part of this generated block, with a local
    // call/return to preserve its compiler-verified ABI. No runtime helper address
    // is embedded, and its build-time verifier rejects every call and relocation.
    bytes.insert(bytes.end(), {0x41, 0x52, 0x41, 0x53, 0x48, 0x83, 0xec});
#if defined(_WIN32)
    bytes.push_back(40);
    emit(bytes, 0x8d, &emit.block->fused, 1, false, true);
#else
    bytes.push_back(8);
    emit(bytes, 0x8d, &emit.block->fused, 7, false, true);
#endif
    bytes.push_back(0xe8);
    size_t call = bytes.size();
    emit_word(bytes, 0);
    bytes.insert(bytes.end(), {0x48, 0x83, 0xc4});
#if defined(_WIN32)
    bytes.push_back(40);
#else
    bytes.push_back(8);
#endif
    bytes.insert(bytes.end(), {0x41, 0x5b, 0x41, 0x5a});
    auto completed = emit_local_jump(bytes, 0);
    const unsigned char* code = nullptr;
    size_t size = 0;
#define FUSED_TEMPLATE(Name) \
    code = Name;             \
    size = sizeof(Name);     \
    break
    switch (((opcode >> 26 == 59) ? 0 : 4) + (((opcode >> 1) & 31) - 28)) {
    case 0:
        FUSED_TEMPLATE(native_fused_s28);
    case 1:
        FUSED_TEMPLATE(native_fused_s29);
    case 2:
        FUSED_TEMPLATE(native_fused_s30);
    case 3:
        FUSED_TEMPLATE(native_fused_s31);
    case 4:
        FUSED_TEMPLATE(native_fused_d28);
    case 5:
        FUSED_TEMPLATE(native_fused_d29);
    case 6:
        FUSED_TEMPLATE(native_fused_d30);
    case 7:
        FUSED_TEMPLATE(native_fused_d31);
    }
#undef FUSED_TEMPLATE
    point_jump_here(bytes, call);
    bytes.insert(bytes.end(), code, code + size);
    point_jump_here(bytes, completed);
    return exceptional;
}

// A matching data breakpoint is a checked fault path. Return before operand
// effects so its existing priority/preflight path can run exactly once.
size_t emit_data_breakpoint_guard(std::vector<unsigned char>& bytes, const memory_emitter& emit, unsigned size,
                                  bool write)
{
    emit(bytes, 0x8b, &emit.block->system, 2, false, true);
    bytes.insert(bytes.end(), {0x8b, 0x8a}); // mov ecx,[system+dabr].
    emit_word(bytes, offsetof(ROMlib_ppc_system750, dabr));
    bytes.insert(bytes.end(), {0xf7, 0xc1});
    emit_word(bytes, write ? 2 : 1);
    auto disabled = emit_local_jump(bytes, 0x84);
    bytes.insert(bytes.end(), {0x8b, 0x92});
    emit_word(bytes, offsetof(ROMlib_ppc_system750, mmu) + offsetof(ROMlib_ppc_mmu750, msr));
    bytes.insert(bytes.end(), {0xc1, 0xea, 2, 0x31, 0xca, 0xf6, 0xc2, 4});
    auto other_context = emit_local_jump(bytes, 0x85);
    emit(bytes, 0x8b, &emit.block->operand_address);
    bytes.insert(bytes.end(), {0x89, 0xc2, 0x83, 0xe2, 0xf8, 0x83, 0xe1, 0xf8,
                               0x29, 0xd1, 0x83, 0xe0, 7,    0x83, 0xc0, static_cast<unsigned char>(size - 1),
                               0x83, 0xe0, 0xf8, 0x39, 0xc1});
    auto match = emit_local_jump(bytes, 0x86); // unsigned granule delta <= covered extent, including wrap.
    point_jump_here(bytes, disabled);
    point_jump_here(bytes, other_context);
    return match;
}

void emit_architectural_scalar(std::vector<unsigned char>& bytes, const memory_emitter& emit, powerpc_cpu& cpu,
                               const system_memory_operand& operand)
{
    auto unavailable = emit_native_entry_guard(bytes, emit);
    emit_native_completed(bytes, emit);
    bool scalar = operand.kind == system_operand_kind::integer || operand.kind == system_operand_kind::floating;
    const auto& frame = emit.block->native_frame;
    emit(bytes, 0x8b, &emit.block->operand_address);
    emit(bytes, 0x89, &frame.address);
    emit(bytes, 0xc7, &frame.size);
    emit_word(bytes, operand.size);
    emit(bytes, 0xc7, &frame.access);
    emit_word(bytes, operand.kind == system_operand_kind::external && operand.write);
    emit(bytes, 0xc7, &frame.selector);
    emit_word(bytes, operand.selector);
    size_t breakpoint = scalar ? emit_data_breakpoint_guard(bytes, emit, operand.size, operand.write) : 0;
    if (operand.write) {
        emit(bytes, 0x8b, &emit.block->operand_value, 0, false, operand.size == 8);
        if (operand.reverse)
            bytes.insert(bytes.end(), operand.size == 2 ? std::initializer_list<unsigned char>{0x86, 0xc4}
                                                        : std::initializer_list<unsigned char>{0x0f, 0xc8});
        emit(bytes, 0x89, &frame.value, 0, false, true && operand.size == 8);
    }
    emit_native_packet_call(bytes, emit);
    emit(bytes, 0x83, &frame.ok, 7);
    bytes.push_back(0);
    auto fault = emit_local_jump(bytes, 0x84);
    if (!operand.write && operand.kind != system_operand_kind::cache_control) {
        emit(bytes, 0x8b, &frame.value, 0, false, operand.size == 8);
        if (operand.kind == system_operand_kind::floating) {
            if (operand.size == 4)
                widen_single(bytes);
            emit(bytes, 0x89, &cpu.fpr_dw(operand.reg), 0, false, true);
        }
        else {
            if (operand.reverse)
                bytes.insert(bytes.end(), operand.size == 2 ? std::initializer_list<unsigned char>{0x86, 0xc4}
                                                            : std::initializer_list<unsigned char>{0x0f, 0xc8});
            if (operand.sign_extend)
                bytes.insert(bytes.end(), {0x0f, 0xbf, 0xc0});
            emit(bytes, 0x89, &cpu.gpr(operand.reg));
        }
    }
    if (operand.update_reg) {
        emit(bytes, 0x8b, &emit.block->operand_address);
        emit(bytes, 0x89, &cpu.gpr(operand.update_reg));
    }
    if (scalar) {
        auto successful = emit_local_jump(bytes, 0);
        point_jump_here(bytes, breakpoint);
        emit(bytes, 0xc6, &emit.block->breakpoint_pending);
        bytes.push_back(1);
        point_jump_here(bytes, successful);
    }
    point_jump_here(bytes, fault);
    point_jump_here(bytes, unavailable);
}

// Each packet commits only its own successful prefix. The live breakpoint is
// checked again after callbacks, which may have changed DABR or source GPRs.
void emit_architectural_stream(std::vector<unsigned char>& bytes, const memory_emitter& emit, powerpc_cpu& cpu,
                               const system_memory_operand& operand)
{
    const auto& frame = emit.block->native_frame;
    auto unavailable = emit_native_entry_guard(bytes, emit);
    emit_native_completed(bytes, emit);
    emit(bytes, 0xc7, &frame.ok);
    emit_word(bytes, 1);
    std::vector<size_t> faults, breakpoints, completed;
    auto constant = [&](const void* address, uint32 value) {
        emit(bytes, 0xc7, address);
        emit_word(bytes, value);
    };
    auto add = [&](const void* address, unsigned value) {
        emit(bytes, 0x83, address);
        bytes.push_back(value);
    };
    auto packet = [&](unsigned size) {
        emit(bytes, 0x8b, &emit.block->operand_address);
        emit(bytes, 0x89, &frame.address);
        constant(&frame.size, size);
        breakpoints.push_back(emit_data_breakpoint_guard(bytes, emit, size, operand.write));
        emit_native_packet_call(bytes, emit);
        emit(bytes, 0x83, &frame.ok, 7);
        bytes.push_back(0);
        faults.push_back(emit_local_jump(bytes, 0x84));
    };
    auto indexed_gpr = [&](bool write) {
        emit(bytes, 0x8b, &emit.block->stream_index, 2);
        emit(bytes, 0x8d, &cpu.gpr(0), 8, false, true);
        bytes.insert(bytes.end(), {0x41, static_cast<unsigned char>(write ? 0x89 : 0x8b), 0x04, 0x90});
    };
    auto loop_to = [&](size_t start, unsigned condition = 0) {
        auto back = emit_local_jump(bytes, condition);
        point_jump(bytes, back, start);
    };
    if (operand.kind == system_operand_kind::multiple) {
        for (unsigned reg = operand.reg; reg < 32; ++reg) {
            if (operand.write) {
                emit(bytes, 0x8b, &cpu.gpr(reg));
                emit(bytes, 0x89, &frame.value);
            }
            packet(4);
            if (!operand.write) {
                emit(bytes, 0x8b, &frame.value);
                emit(bytes, 0x89, &cpu.gpr(reg));
            }
            add(&emit.block->operand_address, 4);
        }
    }
    else {
        if (!operand.write) {
            emit(bytes, 0x81, &emit.block->operand_count, 7);
            emit_word(bytes, operand.string_limit);
            auto valid = emit_local_jump(bytes, 0x86);
            emit(bytes, 0xc6, &emit.block->invalid_string);
            bytes.push_back(1);
            completed.push_back(emit_local_jump(bytes, 0));
            point_jump_here(bytes, valid);
        }
        constant(&emit.block->stream_index, operand.reg);
        if (operand.write) {
            constant(&emit.block->stream_shift, 24);
            emit(bytes, 0x83, &emit.block->operand_count, 7);
            bytes.push_back(0);
            completed.push_back(emit_local_jump(bytes, 0x84));
            size_t start = bytes.size();
            indexed_gpr(false);
            emit(bytes, 0x8b, &emit.block->stream_shift, 1);
            bytes.insert(bytes.end(), {0xd3, 0xe8}); // shr eax,cl.
            emit(bytes, 0x89, &frame.value);
            packet(1);
            add(&emit.block->operand_address, 1);
            emit(bytes, 0x83, &emit.block->operand_count, 5);
            bytes.push_back(1);
            completed.push_back(emit_local_jump(bytes, 0x84));
            emit(bytes, 0x83, &emit.block->stream_shift, 5);
            bytes.push_back(8);
            loop_to(start, 0x89); // jns: still within the source word.
            constant(&emit.block->stream_shift, 24);
            add(&emit.block->stream_index, 1);
            emit(bytes, 0x83, &emit.block->stream_index, 4);
            bytes.push_back(31);
            loop_to(start);
        }
        else {
            size_t start = bytes.size();
            emit(bytes, 0x83, &emit.block->operand_count, 7);
            bytes.push_back(4);
            auto tail = emit_local_jump(bytes, 0x82);
            packet(4);
            emit(bytes, 0x8b, &frame.value);
            indexed_gpr(true);
            add(&emit.block->operand_address, 4);
            emit(bytes, 0x83, &emit.block->operand_count, 5);
            bytes.push_back(4);
            add(&emit.block->stream_index, 1);
            emit(bytes, 0x83, &emit.block->stream_index, 4);
            bytes.push_back(31);
            loop_to(start);
            point_jump_here(bytes, tail);
            emit(bytes, 0x83, &emit.block->operand_count, 7);
            bytes.push_back(0);
            completed.push_back(emit_local_jump(bytes, 0x84));
            emit(bytes, 0x83, &emit.block->operand_count, 7);
            bytes.push_back(1);
            auto one = emit_local_jump(bytes, 0x84);
            packet(2);
            emit(bytes, 0x8b, &frame.value);
            bytes.insert(bytes.end(), {0xc1, 0xe0, 16});
            emit(bytes, 0x89, &emit.block->stream_value);
            emit(bytes, 0x83, &emit.block->operand_count, 7);
            bytes.push_back(2);
            auto two = emit_local_jump(bytes, 0x84);
            add(&emit.block->operand_address, 2);
            packet(1);
            emit(bytes, 0x8b, &frame.value);
            bytes.insert(bytes.end(), {0xc1, 0xe0, 8});
            emit(bytes, 0x0b, &emit.block->stream_value);
            auto three = emit_local_jump(bytes, 0);
            point_jump_here(bytes, two);
            emit(bytes, 0x8b, &emit.block->stream_value);
            auto commit = emit_local_jump(bytes, 0);
            point_jump_here(bytes, one);
            packet(1);
            emit(bytes, 0x8b, &frame.value);
            bytes.insert(bytes.end(), {0xc1, 0xe0, 24});
            point_jump_here(bytes, three);
            point_jump_here(bytes, commit);
            indexed_gpr(true);
        }
    }
    completed.push_back(emit_local_jump(bytes, 0));
    for (size_t branch : breakpoints)
        point_jump_here(bytes, branch);
    emit(bytes, 0xc6, &emit.block->breakpoint_pending);
    bytes.push_back(1);
    for (size_t branch : completed)
        point_jump_here(bytes, branch);
    for (size_t branch : faults)
        point_jump_here(bytes, branch);
    point_jump_here(bytes, unavailable);
}

void emit_architectural_reservation(std::vector<unsigned char>& bytes, const memory_emitter& emit, powerpc_cpu& cpu,
                                    const system_memory_operand& operand)
{
    static_assert(std::is_standard_layout<powerpc_cr_register>::value && sizeof(powerpc_cr_register) == 4,
                  "native CR object representation");
    static_assert(std::is_standard_layout<powerpc_xer_register>::value && sizeof(powerpc_xer_register) == 4,
                  "native XER object representation");
    const auto& frame = emit.block->native_frame;
    auto unavailable = emit_native_entry_guard(bytes, emit);
    emit_native_completed(bytes, emit);
    emit(bytes, 0x8b, &emit.block->operand_address);
    emit(bytes, 0x89, &frame.address);
    emit(bytes, 0xc7, &frame.size);
    emit_word(bytes, 4);
    emit(bytes, 0xc7, &frame.ok);
    emit_word(bytes, 1);
    emit(bytes, 0x8b, &emit.block->system, 2, false, true);
    if (operand.write) {
        bytes.insert(bytes.end(), {0x8b, 0x82});
        emit_word(bytes, offsetof(ROMlib_ppc_system750, reservation_valid));
        bytes.insert(bytes.end(), {0x85, 0xc0, 0x0f, 0x95, 0xc0, 0x0f, 0xb6, 0xc0});
        emit(bytes, 0x89, &emit.block->operand_count);
        emit(bytes, 0xc7, &cpu.romaw_registers().reserve_valid);
        emit_word(bytes, 0);
    }
    bytes.insert(bytes.end(), {0xc7, 0x82});
    emit_word(bytes, offsetof(ROMlib_ppc_system750, reservation_valid));
    emit_word(bytes, 0);
    size_t no_reservation = 0;
    if (operand.write) {
        emit(bytes, 0x83, &emit.block->operand_count, 7);
        bytes.push_back(0);
        no_reservation = emit_local_jump(bytes, 0x84);
        emit(bytes, 0x8b, &emit.block->operand_value);
        emit(bytes, 0x89, &frame.value);
    }
    auto breakpoint = emit_data_breakpoint_guard(bytes, emit, 4, operand.write);
    emit_native_packet_call(bytes, emit);
    emit(bytes, 0x83, &frame.ok, 7);
    bytes.push_back(0);
    auto fault = emit_local_jump(bytes, 0x84);
    if (operand.write) {
        point_jump_here(bytes, no_reservation);
        emit(bytes, 0x8b, &emit.block->operand_count, 1);
        bytes.insert(bytes.end(), {0xc1, 0xe1, 29}); // EQ after an accepted store.
        emit(bytes, 0xb6, &cpu.romaw_registers().xer, 0, true);
        bytes.insert(bytes.end(), {0x85, 0xc0, 0x0f, 0x95, 0xc0, 0x0f, 0xb6, 0xc0, 0xc1, 0xe0, 28, 0x09, 0xc1});
        emit(bytes, 0x8b, &cpu.romaw_registers().cr);
        bytes.push_back(0x25);
        emit_word(bytes, 0x0fffffff);
        bytes.insert(bytes.end(), {0x09, 0xc8});
        emit(bytes, 0x89, &cpu.romaw_registers().cr);
    }
    else {
        emit(bytes, 0x8b, &frame.value);
        emit(bytes, 0x89, &cpu.gpr(operand.reg));
    }
    auto completed = emit_local_jump(bytes, 0);
    point_jump_here(bytes, breakpoint);
    emit(bytes, 0xc6, &emit.block->breakpoint_pending);
    bytes.push_back(1);
    point_jump_here(bytes, completed);
    point_jump_here(bytes, fault);
    point_jump_here(bytes, unavailable);
}

void emit_native_block(native_block& block, powerpc_cpu* cpu, uint32 start_pc, std::vector<unsigned char>& bytes)
{
    bytes.clear();
    memory_emitter emit_memory = {cpu, &block};
    block.records = false;
    block.reads_xer = block.writes_xer = false;
    block.branches = block.links = false;
    block.reads_cr = block.reads_ctr = block.writes_ctr = false;
    block.reads_lr = block.writes_lr = false;
    block.reads_fpscr = block.writes_fpscr = false;
    block.indirect = 0;
    block.next_pc = block.link_pc = 0;
    block.memory = false;
    block.semantic = false;
    block.slow_path = false;
    block.writes_system_msr = false;
    block.system_operand = {};
    auto store_cr_field = [&](unsigned field) {
        block.records = true;
        if (field)
            bytes.insert(bytes.end(), {0xc1, 0xe8, (unsigned char)(field * 4)});
        bytes.insert(bytes.end(), {0x89, 0xc1}); // mov ecx,eax.
        emit_memory(bytes, 0x8b, &block.cr);
        bytes.push_back(0x25); // Preserve all other CR fields.
        emit_word(bytes, ~(0xf0000000u >> (field * 4)));
        bytes.insert(bytes.end(), {0x09, 0xc8}); // or eax,ecx.
        emit_memory(bytes, 0x89, &block.cr);
    };
    auto store_condition = [&](unsigned field) {
        emit_memory(bytes, 0x0b, &block.so);
        store_cr_field(field);
    };
    auto record = [&]() {
        /* The GPR result is already committed. Compute signed LT/GT/EQ from EAX;
       MOV preserves TEST's flags, and both jumps remain within this sequence.
       Only volatile host registers are used. SO is captured before entry;
       overflow-enabled operations update the cached SO within the block. */
        bytes.insert(bytes.end(), {0x85, 0xc0, 0xb8}); // test eax,eax; mov eax,EQ.
        emit_word(bytes, 0x20000000);
        bytes.insert(bytes.end(), {0x74, 12, 0xb8}); // jz done; mov eax,GT.
        emit_word(bytes, 0x40000000);
        bytes.insert(bytes.end(), {0x79, 5, 0xb8}); // jns done; mov eax,LT.
        emit_word(bytes, 0x80000000);
        store_condition(0);
    };
    auto write_overflow = [&]() {
        // ECX holds Boolean overflow and the GPR result is already committed.
        // Replace OV and set sticky SO, preserving CA/BC and live CR inputs.
        block.writes_xer = true;
        bytes.insert(bytes.end(), {0xc1, 0xe1, 30, 0x89, 0xca, 0xd1, 0xe2});
        emit_memory(bytes, 0x8b, &block.xer);
        bytes.push_back(0x25);
        emit_word(bytes, ~0x40000000u);
        bytes.insert(bytes.end(), {0x09, 0xc8, 0x09, 0xd0});
        emit_memory(bytes, 0x89, &block.xer);
        // Later compares/record forms must observe the updated SO.
        bytes.insert(bytes.end(), {0xc1, 0xe8, 3});
        bytes.push_back(0x25);
        emit_word(bytes, 0x10000000);
        emit_memory(bytes, 0x89, &block.so);
    };
    auto summarize_fpscr = [&]() {
        // EAX holds FPSCR. Derive VX, then FEX without changing sticky FX.
        bytes.push_back(0x25);
        emit_word(bytes, ~0x60000000u);
        bytes.insert(bytes.end(), {0x89, 0xc1, 0x81, 0xe1});
        emit_word(bytes, 0x01f80700);
        bytes.insert(bytes.end(), {0xf7, 0xd9, 0x19, 0xc9, 0x81, 0xe1});
        emit_word(bytes, 0x20000000);
        bytes.insert(bytes.end(), {0x09, 0xc8, 0x89, 0xc1, 0xc1, 0xe9, 25,   0x89, 0xc2, 0xc1, 0xea, 3,
                                   0x21, 0xd1, 0x83, 0xe1, 31,   0xf7, 0xd9, 0x19, 0xc9, 0x81, 0xe1});
        emit_word(bytes, 0x40000000);
        bytes.insert(bytes.end(), {0x09, 0xc8});
    };
    auto raise_fpscr = [&]() {
        // Merge EDX exception causes into EAX and set FX for newly set bits.
        bytes.insert(bytes.end(), {0x89, 0xc1, 0xf7, 0xd1, 0x21, 0xd1, 0x09, 0xd0}); // mov; not; and; or
        bytes.insert(bytes.end(), {0xf7, 0xd9, 0x19, 0xc9, 0x81, 0xe1});             // neg; sbb; and ecx,FX
        emit_word(bytes, 0x80000000);
        bytes.insert(bytes.end(), {0x09, 0xc8}); // or eax,ecx
    };
    auto record_cr1 = [&]() {
        block.reads_fpscr = true;
        emit_memory(bytes, 0x8b, &block.fpscr);
        bytes.push_back(0x25);
        emit_word(bytes, 0xf0000000u);
        store_cr_field(1);
    };
    auto emit_tail = [&]() {
#if defined(_WIN32)
        bytes.insert(bytes.end(), {0x4c, 0x89, 0xd1, 0x4c, 0x89, 0xda});
#else
        bytes.insert(bytes.end(), {0x4c, 0x89, 0xd7, 0x4c, 0x89, 0xde});
#endif
        bytes.insert(bytes.end(), {0x49, 0x8b, 0x83});
        emit_word(bytes, (uintptr)&block.tail - (uintptr)&block);
        bytes.insert(bytes.end(), {0xff, 0xe0});
    };
    bytes.insert(bytes.end(), {0xf3, 0x0f, 0x1e, 0xfa}); /* ENDBR64. */
#if defined(_WIN32)
    bytes.insert(bytes.end(), {0x49, 0x89, 0xca, 0x49, 0x89, 0xd3});
#else
    bytes.insert(bytes.end(), {0x49, 0x89, 0xfa, 0x49, 0x89, 0xf3});
#endif
    size_t budget_stops[max_words - 1];
    unsigned stop_count = 0;

    struct guarded_transfer {
        unsigned prefix;
        std::vector<size_t> branches;
    };

    std::vector<guarded_transfer> transfers;
    unsigned instruction_index = 0;
    for (uint32 opcode : block.words) {
        unsigned primary = opcode >> 26;
        if (instruction_index) {
            // cmp dword ptr [r11+executed], instruction_index; je return.
            // Leave the terminal operation and its effects out of a bounded prefix.
            emit_memory(bytes, 0x83, &block.executed, 7);
            bytes.push_back(instruction_index);
            budget_stops[stop_count++] = emit_local_jump(bytes, 0x84);
        }
        uint32 instruction_pc = start_pc + instruction_index++ * 4;
        if (native_fp_fused_operation(opcode)) {
            block.reads_fpscr = block.writes_fpscr = block.slow_path = true;
            block.next_pc = instruction_pc;
            auto exceptional = emit_native_fp_fused(bytes, emit_memory, *cpu, opcode);
            emit_memory(bytes, 0x8b, &block.fused.fpscr);
            emit_memory(bytes, 0x89, &block.fpscr);
            auto source = reinterpret_cast<const unsigned char*>(&block.fused.result);
            auto destination = reinterpret_cast<unsigned char*>(&cpu->fpr_dw((opcode >> 21) & 31));
            emit_memory(bytes, 0x8b, source);
            emit_memory(bytes, 0x89, destination);
            emit_memory(bytes, 0x8b, source + 4);
            emit_memory(bytes, 0x89, destination + 4);
            if (opcode & 1)
                record_cr1();
            auto completed = emit_local_jump(bytes, 0);
            for (size_t branch : exceptional)
                point_jump_here(bytes, branch);
            emit_tail();
            point_jump_here(bytes, completed);
            continue;
        }
        if (native_fp_arithmetic_operation(opcode) || native_fp_estimate_operation(opcode)) {
            block.reads_fpscr = block.writes_fpscr = block.slow_path = true;
            block.next_pc = instruction_pc;
            auto exceptional = native_fp_estimate_operation(opcode)
                                   ? emit_native_fp_estimate(bytes, emit_memory, *cpu, opcode)
                                   : emit_native_fp_arithmetic(bytes, emit_memory, *cpu, opcode);
            emit_memory(bytes, 0x8b, &block.fpscr);
            bytes.push_back(0x25);
            emit_word(bytes, ~0x0007F000u);
            emit_memory(bytes, 0x0b, &block.fp_flags);
            emit_memory(bytes, 0x8b, &block.fp_raised, 2);
            raise_fpscr();
            summarize_fpscr();
            emit_memory(bytes, 0x0b, &block.fp_class);
            emit_memory(bytes, 0x89, &block.fpscr);
            auto destination = reinterpret_cast<unsigned char*>(&cpu->fpr_dw((opcode >> 21) & 31));
            auto result = reinterpret_cast<const unsigned char*>(&block.fp_result);
            emit_memory(bytes, 0x8b, result);
            emit_memory(bytes, 0x89, destination);
            emit_memory(bytes, 0x8b, result + 4);
            emit_memory(bytes, 0x89, destination + 4);
            if (opcode & 1)
                record_cr1();
            auto completed = emit_local_jump(bytes, 0);
            for (size_t branch : exceptional)
                point_jump_here(bytes, branch);
            emit_tail();
            point_jump_here(bytes, completed);
            continue;
        }
        const auto memory = decode_memory(opcode);
        if (block.architectural) {
            if (opcode == 0x4C00012C) {
                emit_memory(bytes, 0x8b, &block.system, 2, false, true);
                bytes.insert(bytes.end(), {0xc7, 0x82}); // mov dword [system+iabr_pending],0.
                emit_word(bytes, offsetof(ROMlib_ppc_system750, iabr_pending));
                emit_word(bytes, 0);
                continue;
            }
            using operation = romaw_ppc_system_operation;
            auto decoded = romaw_ppc_decode_system_operation(opcode);
            if (decoded == operation::trap) {
                emit_trap_word(bytes, emit_memory, *cpu, opcode);
                continue;
            }
            if (decoded == operation::system_call) {
                emit_memory(bytes, 0xc7, &block.exception_vector);
                emit_word(bytes, 0xC00);
                continue;
            }
            if (decoded == operation::rfi) {
                block.writes_system_msr = true;
                block.branches = true;
                emit_rfi(bytes, emit_memory);
                emit_state_effect(bytes, emit_memory, instruction_pc, 0, &block.msr_value, true);
                continue;
            }
            auto move = romaw_ppc_750_register_move(opcode);
            if (move.form == romaw_ppc_register_form::effect) {
                block.system_operand.kind = system_operand_kind::spr_write;
                block.system_operand.selector = move.value;
                emit_memory(bytes, 0x8b, &cpu->gpr((opcode >> 21) & 31));
                emit_memory(bytes, 0x89, &block.operand_value);
                emit_state_effect(bytes, emit_memory, instruction_pc, move.value, &block.operand_value);
                continue;
            }
            if (move.form != romaw_ppc_register_form::none) {
                emit_register_move(bytes, emit_memory, *cpu, opcode, move);
                continue;
            }
            unsigned xo = (opcode >> 1) & 1023;
            if (primary == 31 && xo == 146 && !(opcode & romaw_ppc_mmu_reserved_mask(xo))) {
                block.writes_system_msr = true;
                emit_memory(bytes, 0x8b, &cpu->gpr((opcode >> 21) & 31));
                emit_memory(bytes, 0x89, &block.msr_value);
                emit_state_effect(bytes, emit_memory, instruction_pc, 0, &block.msr_value);
                continue;
            }
            if (primary == 31 && (xo == 306 || xo == 566) && !(opcode & romaw_ppc_mmu_reserved_mask(xo))) {
                if (xo == 306)
                    emit_tlbie(bytes, emit_memory, *cpu, (opcode >> 11) & 31);
                else
                    emit_tlbsync(bytes, emit_memory);
                continue;
            }
            uint32 cache_mask = primary == 31 ? romaw_ppc_cache_reserved_mask(xo) : 0;
            bool external = decoded == operation::external;
            if (external || (cache_mask && !(opcode & cache_mask))) {
                auto& operand = block.system_operand;
                operand.kind = external ? system_operand_kind::external : system_operand_kind::cache_control;
                operand.selector = xo;
                operand.reg = (opcode >> 21) & 31;
                operand.write = external && xo == 438;
                // SYNC and EIEIO use the invocation's zero address.
                if (xo != 598 && xo != 854) {
                    emit_effective_address(bytes, emit_memory, *cpu, (opcode >> 16) & 31, true, (opcode >> 11) & 31, 0);
                    emit_memory(bytes, 0x89, &block.operand_address);
                }
                if (operand.write) {
                    emit_memory(bytes, 0x8b, &cpu->gpr(operand.reg));
                    emit_memory(bytes, 0x89, &block.operand_value);
                }
                emit_architectural_scalar(bytes, emit_memory, *cpu, operand);
                continue;
            }
            bool floating = (memory.primary >= 48 && memory.primary <= 54) || memory.primary == 983;
            bool reservation = memory.primary == 20 || memory.primary == 150;
            bool multiple = memory.primary == 46 || memory.primary == 47;
            bool immediate_string = memory.primary == 597 || memory.primary == 725;
            bool string = immediate_string || memory.primary == 533 || memory.primary == 661;
            if ((memory.primary >= 32 && memory.primary <= 44) || floating || reservation || multiple || string) {
                unsigned ra = (opcode >> 16) & 31;
                unsigned rb = (opcode >> 11) & 31;
                unsigned reg = (opcode >> 21) & 31;
                auto& operand = block.system_operand;
                operand.size = memory.operand_size();
                operand.reg = reg;
                operand.update_reg = memory.update ? ra : 0;
                operand.write = memory.writes();
                operand.reverse = memory.reverse;
                operand.sign_extend = memory.primary == 42;
                if (reservation)
                    operand.kind = system_operand_kind::reservation;
                else if (floating)
                    operand.kind = system_operand_kind::floating;
                else if (multiple)
                    operand.kind = system_operand_kind::multiple;
                else if (string)
                    operand.kind = system_operand_kind::string;
                else
                    operand.kind = system_operand_kind::integer;
                emit_effective_address(bytes, emit_memory, *cpu, ra, primary == 31 && !immediate_string, rb,
                                       immediate_string ? 0 : uint32(int32(int16(opcode))));
                emit_memory(bytes, 0x89, &block.operand_address);
                if (string) {
                    operand.string_limit = 4 * romaw_ppc_string_load_limit(reg, ra, rb, !immediate_string);
                    if (immediate_string) {
                        emit_memory(bytes, 0xc7, &block.operand_count);
                        emit_word(bytes, rb ? rb : 32);
                    }
                    else {
                        block.reads_xer = true;
                        emit_memory(bytes, 0x8b, &block.xer);
                        bytes.insert(bytes.end(), {0x83, 0xe0, 127}); // and eax, XER byte-count mask
                        emit_memory(bytes, 0x89, &block.operand_count);
                    }
                }
                else if (operand.write && !multiple) {
                    if (floating) {
                        // STFIWX captures just the low word. STFS converts in
                        // the live guest rounding mode before the bus runs.
                        emit_memory(bytes, 0x8b, &cpu->fpr_dw(reg), 0, false, memory.primary != 983);
                        if (memory.primary == 52)
                            narrow_single(bytes);
                    }
                    else
                        emit_memory(bytes, 0x8b, &cpu->gpr(reg));
                    emit_memory(bytes, 0x89, &block.operand_value, 0, false, operand.size == 8);
                }
                if (operand.kind == system_operand_kind::integer || operand.kind == system_operand_kind::floating)
                    emit_architectural_scalar(bytes, emit_memory, *cpu, operand);
                else if (operand.kind == system_operand_kind::reservation)
                    emit_architectural_reservation(bytes, emit_memory, *cpu, operand);
                else
                    emit_architectural_stream(bytes, emit_memory, *cpu, operand);
                continue;
            }
        }
        if (!block_inline(opcode, block.architectural)) {
            block.semantic = true;
            block.next_pc = instruction_pc;
            // Tail-call through this invocation's current helper. No generated
            // frame survives callbacks, invalidation or C++ exception unwinding.
            emit_tail();
            break;
        }
        if (memory.primary) {
            block.memory = true;
            unsigned reg = (opcode >> 21) & 31, ra = (opcode >> 16) & 31;
            unsigned form = memory.primary;
            bool string_load = form == 533 || form == 597;
            bool string = string_load || form == 661 || form == 725;
            bool immediate_string = form == 597 || form == 725;
            bool indexed = primary == 31 && !immediate_string;
            bool multiple = form == 46 || form == 47;
            bool conditional = form == 150;
            bool zero_line = form == 1014;
            bool floating = form >= 48 && !conditional && !zero_line;
            bool writing = memory.writes();
            bool wide = form == 50 || form == 54;
            unsigned size = memory.operand_size();
            unsigned extent = zero_line ? 32 : multiple ? (32 - reg) * 4 : size;
            transfers.push_back({instruction_index - 1, {}});
            auto& guards = transfers.back().branches;
            auto guard = [&](unsigned condition) { guards.push_back(emit_local_jump(bytes, condition)); };
            unsigned rb = (opcode >> 11) & 31;
            size_t empty_string = 0;
            if (string) {
                if (immediate_string) {
                    bytes.push_back(0xb9); // mov ecx,count
                    emit_word(bytes, rb ? rb : 32);
                }
                else {
                    block.reads_xer = true;
                    emit_memory(bytes, 0x8b, &block.xer, 1);
                    bytes.insert(bytes.end(), {0x83, 0xe1, 127});
                    if (string_load) {
                        bytes.insert(bytes.end(),
                                     {0x83, 0xf9, (unsigned char)(romaw_ppc_string_load_limit(reg, ra, rb, true) * 4)});
                        guard(0x87);
                    }
                    bytes.insert(bytes.end(), {0x85, 0xc9});
                    empty_string = emit_local_jump(bytes, 0x84);
                }
            }
            emit_effective_address(bytes, emit_memory, *cpu, ra, indexed, rb,
                                   immediate_string ? 0 : uint32(int32(int16(opcode))));
            // Preserve the guest EA across host-pointer calculation and transfer.
            // No architectural writeback occurs until all guards have succeeded.
            if (memory.update)
                bytes.insert(bytes.end(), {0x89, 0xc1}); // mov ecx,eax.
            if (zero_line)
                bytes.insert(bytes.end(), {0x83, 0xe0, 0xe0}); // and eax,~31
            else if (size > 1 && !string) {
                bytes.push_back(0xa9); // test eax, alignment mask.
                emit_word(bytes, size - 1);
                guard(0x85); // jne interpreter.
            }
            size_t no_reservation = 0;
            auto reservation = &cpu->romaw_registers().reserve_valid;
            if (conditional) {
                emit_memory(bytes, 0x83, reservation, 7); // cmp dword [reserve_valid],0
                bytes.push_back(0);
                no_reservation = emit_local_jump(bytes, 0x84);
            }
            emit_memory(bytes, 0x2b, &block.ram_address);
            emit_memory(bytes, 0x3b, &block.ram_size);
            guard(0x87); // ja interpreter: unsigned relative > size.
            emit_memory(bytes, 0x8b, &block.ram_size, 2);
            bytes.insert(bytes.end(), {0x29, 0xc2});
            if (string)
                bytes.insert(bytes.end(), {0x39, 0xca}); // cmp edx,ecx
            else {
                bytes.insert(bytes.end(), {0x81, 0xfa});
                emit_word(bytes, extent);
            }
            guard(0x82); // jb interpreter: remaining bytes < width.
            if (writing && instruction_index < block.words.size()) {
                // EAX is the offset in RAM. No successful transfer can wrap the
                // guest address space, so both inclusive endpoints fit 32 bits.
                emit_memory(bytes, 0x8b, &block.ram_address, 2);
                bytes.insert(bytes.end(), {0x01, 0xc2, 0x81, 0xfa}); // add edx,eax; cmp edx,last byte
                emit_word(bytes, start_pc + uint32(block.words.size() * 4) - 1);
                size_t beyond_suffix = emit_local_jump(bytes, 0x87);
                if (string)
                    bytes.insert(bytes.end(), {0x8d, 0x54, 0x0a, 0xff}); // lea edx,[rdx+rcx-1]
                else if (extent > 1) {
                    bytes.insert(bytes.end(), {0x81, 0xc2});
                    emit_word(bytes, extent - 1);
                }
                bytes.insert(bytes.end(), {0x81, 0xfa}); // cmp edx,first byte of the suffix
                emit_word(bytes, instruction_pc + 4);
                size_t before_suffix = emit_local_jump(bytes, 0x82);
                // Retire the store, then return at the next budget check so
                // the dispatcher fetches the first word of the changed suffix.
                emit_memory(bytes, 0xc7, &block.executed);
                emit_word(bytes, instruction_index);
                point_jump_here(bytes, beyond_suffix);
                point_jump_here(bytes, before_suffix);
            }
            // Load the current invocation's full-width RAM pointer into R8.
            bytes.insert(bytes.end(), {0x4d, 0x8b, 0x83});
            emit_word(bytes, (uintptr)&block.ram - (uintptr)&block);
            bytes.insert(bytes.end(), {0x49, 0x01, 0xc0}); // add r8,rax.
            if (zero_line) {
                bytes.insert(bytes.end(), {0x31, 0xc0, 0x49, 0x89, 0x00}); // xor eax,eax; mov [r8],rax
                for (unsigned char offset : {8, 16, 24})
                    bytes.insert(bytes.end(), {0x49, 0x89, 0x40, offset});
            }
            else if (string)
                string_transfer(bytes, writing, reg, emit_memory, *cpu);
            else if (multiple) {
                // The full span fits flat RAM. Otherwise the guard leaves the
                // interpreter to preserve word-by-word faults and callbacks.
                for (unsigned item = reg; item < 32; ++item) {
                    unsigned char offset = (item - reg) * 4;
                    if (writing) {
                        emit_memory(bytes, 0x8b, &cpu->gpr(item));
                        bytes.insert(bytes.end(), {0x0f, 0xc8, 0x41, 0x89, 0x40, offset});
                    }
                    else {
                        bytes.insert(bytes.end(), {0x41, 0x8b, 0x40, offset, 0x0f, 0xc8});
                        emit_memory(bytes, 0x89, &cpu->gpr(item));
                    }
                }
            }
            else if (floating) {
                if (writing) {
                    emit_memory(bytes, 0x8b, &cpu->fpr_dw(reg), 0, false, form != 983);
                    if (form == 52)
                        narrow_single(bytes);
                    if (wide)
                        bytes.insert(bytes.end(), {0x48, 0x0f, 0xc8, 0x49, 0x89, 0x00});
                    else
                        bytes.insert(bytes.end(), {0x0f, 0xc8, 0x41, 0x89, 0x00});
                }
                else {
                    if (wide)
                        bytes.insert(bytes.end(), {0x49, 0x8b, 0x00, 0x48, 0x0f, 0xc8});
                    else {
                        bytes.insert(bytes.end(), {0x41, 0x8b, 0x00, 0x0f, 0xc8});
                        widen_single(bytes);
                    }
                    emit_memory(bytes, 0x89, &cpu->fpr_dw(reg), 0, false, true);
                }
            }
            else if (writing) {
                emit_memory(bytes, 0x8b, &cpu->gpr(reg));
                if (size == 1)
                    bytes.insert(bytes.end(), {0x41, 0x88, 0x00});
                else if (size == 2) {
                    if (!memory.reverse)
                        bytes.insert(bytes.end(), {0x66, 0xc1, 0xc8, 8});
                    bytes.insert(bytes.end(), {0x66, 0x41, 0x89, 0x00});
                }
                else {
                    if (!memory.reverse)
                        bytes.insert(bytes.end(), {0x0f, 0xc8});
                    bytes.insert(bytes.end(), {0x41, 0x89, 0x00});
                }
            }
            else {
                if (size == 1)
                    bytes.insert(bytes.end(), {0x41, 0x0f, 0xb6, 0x00});
                else if (size == 2) {
                    bytes.insert(bytes.end(), {0x41, 0x0f, 0xb7, 0x00});
                    if (!memory.reverse)
                        bytes.insert(bytes.end(), {0x86, 0xc4});
                    if (form == 42)
                        bytes.insert(bytes.end(), {0x0f, 0xbf, 0xc0});
                }
                else {
                    bytes.insert(bytes.end(), {0x41, 0x8b, 0x00});
                    if (!memory.reverse)
                        bytes.insert(bytes.end(), {0x0f, 0xc8});
                }
                emit_memory(bytes, 0x89, &cpu->gpr(reg));
            }
            if (form == 20) {
                emit_memory(bytes, 0xc7, reservation);
                emit_word(bytes, 1);
            }
            if (conditional) {
                // A bounds miss leaves the reservation for the checked store,
                // which consumes it before attempting its potentially faulting write.
                bytes.push_back(0xb8);
                emit_word(bytes, 0x20000000); // CR0.EQ on a successful write.
                size_t stored = emit_local_jump(bytes, 0);
                point_jump_here(bytes, no_reservation);
                bytes.insert(bytes.end(), {0x31, 0xc0});
                point_jump_here(bytes, stored);
                emit_memory(bytes, 0xc7, reservation);
                emit_word(bytes, 0);
                store_condition(0);
            }
            if (memory.update)
                emit_memory(bytes, 0x89, &cpu->gpr(ra), 1);
            if (empty_string)
                point_jump_here(bytes, empty_string);
            continue;
        }
        if (flat_cache_operation(opcode) != flat_cache_op::none)
            continue;
        if (primary == 63) {
            unsigned xo = (opcode >> 1) & 1023;
            if (xo == 14 || xo == 15) {
                block.reads_fpscr = block.writes_fpscr = true;
                emit_fp_integer(bytes, emit_memory, *cpu, opcode);
                emit_memory(bytes, 0x89, &block.fp_integer, 8);
                emit_memory(bytes, 0x89, &block.fp_raised, 2);
                emit_memory(bytes, 0x8b, &block.fpscr);
                bytes.push_back(0x25); // and eax,~(FR|FI)
                emit_word(bytes, ~0x00060000u);
                bytes.insert(bytes.end(), {0x09, 0xc8}); // or eax,ecx
                raise_fpscr();
                summarize_fpscr();
                emit_memory(bytes, 0x89, &block.fpscr);
                emit_memory(bytes, 0xf7, &block.fp_raised);
                emit_word(bytes, 0x00000100);
                size_t valid = emit_local_jump(bytes, 0x84);
                bytes.insert(bytes.end(), {0xa8, 0x80}); // test al,VE
                size_t inhibited = emit_local_jump(bytes, 0x85);
                point_jump_here(bytes, valid);
                auto destination = reinterpret_cast<unsigned char*>(&cpu->fpr_dw((opcode >> 21) & 31));
                emit_memory(bytes, 0x8b, &block.fp_integer);
                emit_memory(bytes, 0x89, destination);
                emit_memory(bytes, 0xc7, destination + 4);
                emit_word(bytes, 0);
                point_jump_here(bytes, inhibited);
                if (opcode & 1)
                    record_cr1();
                continue;
            }
            if (xo == 0 || xo == 32) {
                block.reads_fpscr = block.writes_fpscr = true;
                emit_fp_compare(bytes, emit_memory, *cpu, opcode);
                emit_memory(bytes, 0x8b, &block.fpscr);
                raise_fpscr();
                bytes.push_back(0x25); // and eax,~FPCC
                emit_word(bytes, ~0x0000f000u);
                emit_memory(bytes, 0x8b, &block.fp_condition, 2);
                bytes.insert(bytes.end(), {0xc1, 0xe2, 12, 0x09, 0xd0});
                summarize_fpscr();
                emit_memory(bytes, 0x89, &block.fpscr);
                emit_memory(bytes, 0x8b, &block.fp_condition);
                bytes.insert(bytes.end(), {0xc1, 0xe0, 28});
                store_cr_field((opcode >> 23) & 7);
                continue;
            }
            if (unsigned operation = fpscr_write_operation(opcode)) {
                uint32 mask = fpscr_write_mask(opcode, operation);
                block.reads_fpscr = block.writes_fpscr = true;
                emit_memory(bytes, 0x8b, &block.fpscr);
                if (operation == 38) {
                    if (mask & 0x1ff80700u) {
                        // MTFSB1 sets FX only when it adds a new exception cause.
                        bytes.push_back(0xa9);
                        emit_word(bytes, mask);
                        size_t already_set = emit_local_jump(bytes, 0x85);
                        bytes.push_back(0x0d);
                        emit_word(bytes, 0x80000000);
                        point_jump_here(bytes, already_set);
                    }
                    bytes.push_back(0x0d);
                    emit_word(bytes, mask);
                }
                else {
                    bytes.push_back(0x25);
                    emit_word(bytes, ~mask);
                    if (operation == 134) {
                        unsigned shift = 28 - ((opcode >> 23) & 7) * 4;
                        bytes.push_back(0x0d);
                        emit_word(bytes, (((opcode >> 12) & 15) << shift) & mask);
                    }
                    else if (operation == 711) {
                        emit_memory(bytes, 0x8b, &cpu->fpr_dw((opcode >> 11) & 31), 1);
                        bytes.insert(bytes.end(), {0x81, 0xe1});
                        emit_word(bytes, mask);
                        bytes.insert(bytes.end(), {0x09, 0xc8});
                    }
                }
                summarize_fpscr();
                emit_memory(bytes, 0x89, &block.fpscr);
                if (opcode & 1)
                    record_cr1();
                continue;
            }
            if (xo == 64) {
                unsigned src = 28 - ((opcode >> 18) & 7) * 4;
                unsigned dst = 28 - ((opcode >> 23) & 7) * 4;
                block.reads_fpscr = block.writes_fpscr = block.records = true;
                // Copy the old field before clearing its exception causes.
                emit_memory(bytes, 0x8b, &block.fpscr);
                bytes.insert(bytes.end(),
                             {0xc1, 0xe8, (unsigned char)src, 0x83, 0xe0, 15, 0xc1, 0xe0, (unsigned char)dst});
                emit_memory(bytes, 0x81, &block.cr, 4);
                emit_word(bytes, ~(15u << dst));
                emit_memory(bytes, 0x09, &block.cr);
                emit_memory(bytes, 0x8b, &block.fpscr);
                bytes.push_back(0x25);
                emit_word(bytes, ~((15u << src) & 0x9ff80700u));
                summarize_fpscr();
                emit_memory(bytes, 0x89, &block.fpscr);
                continue;
            }
            // Raw integer copies preserve all payload bits, including SNaNs.
            // Host and guest FPRs are never loaded into an x87/SSE register.
            unsigned destination = (opcode >> 21) & 31;
            unsigned source = (opcode >> 11) & 31;
            auto to = reinterpret_cast<unsigned char*>(&cpu->fpr_dw(destination));
            auto copy = [&](unsigned reg, unsigned operation) {
                auto from = reinterpret_cast<unsigned char*>(&cpu->fpr_dw(reg));
                emit_memory(bytes, 0x8b, from);
                emit_memory(bytes, 0x89, to);
                emit_memory(bytes, 0x8b, from + 4);
                if (operation != 72) {
                    bytes.push_back(operation == 40 ? 0x35 : operation == 264 ? 0x25 : 0x0d);
                    emit_word(bytes, operation == 264 ? 0x7fffffffu : 0x80000000u);
                }
                emit_memory(bytes, 0x89, to + 4);
            };
            if (xo == 583) {
                // Match the interpreter's zero upper word; UISA leaves it
                // undefined. Read live status, without a host FP conversion.
                block.reads_fpscr = true;
                emit_memory(bytes, 0x8b, &block.fpscr);
                emit_memory(bytes, 0x89, to);
                bytes.insert(bytes.end(), {0x31, 0xc0}); // xor eax,eax.
                emit_memory(bytes, 0x89, to + 4);
            }
            else if (((opcode >> 1) & 31) == 23) {
                auto guard = reinterpret_cast<unsigned char*>(&cpu->fpr_dw((opcode >> 16) & 31));
                auto jump = [&](unsigned condition) {
                    bytes.insert(bytes.end(), {0x0f, (unsigned char)condition});
                    size_t position = bytes.size();
                    emit_word(bytes, 0);
                    return position;
                };
                auto patch = [&](size_t position) {
                    uint32 displacement = bytes.size() - position - 4;
                    for (unsigned i = 0; i < 4; ++i)
                        bytes[position + i] = displacement >> (i * 8);
                };
                // NaNs select FRB, including negative signaling payloads. Both
                // signed zeros and all nonnegative non-NaNs select FRC.
                emit_memory(bytes, 0x8b, guard + 4);
                bytes.push_back(0x25);
                emit_word(bytes, 0x7fffffff);
                bytes.push_back(0x3d);
                emit_word(bytes, 0x7ff00000);
                size_t nan_high = jump(0x87); // ja FRB.
                size_t finite = jump(0x82);   // jb sign check.
                emit_memory(bytes, 0x8b, guard);
                bytes.insert(bytes.end(), {0x85, 0xc0});
                size_t nan_low = jump(0x85); // jnz FRB.
                patch(finite);
                emit_memory(bytes, 0x8b, guard + 4);
                bytes.insert(bytes.end(), {0x85, 0xc0});
                size_t positive = jump(0x89); // jns FRC.
                bytes.push_back(0x25);
                emit_word(bytes, 0x7fffffff);
                emit_memory(bytes, 0x0b, guard);
                size_t negative = jump(0x85); // jnz FRB, except negative zero.
                patch(positive);
                copy((opcode >> 6) & 31, 72);
                bytes.push_back(0xe9);
                size_t done = bytes.size();
                emit_word(bytes, 0);
                patch(nan_high);
                patch(nan_low);
                patch(negative);
                copy(source, 72);
                patch(done);
            }
            else
                copy(source, xo);
            if (opcode & 1)
                record_cr1();
            continue;
        }
        if (ends_block(opcode)) {
            // Branch exits return to the dispatcher before fetching the target.
            block.branches = true;
            block.links = opcode & 1;
            block.next_pc = primary == 19 ? 0 : direct_branch_target(opcode, instruction_pc);
            block.link_pc = instruction_pc + 4;
            if (primary == 19) {
                block.indirect = ((opcode >> 1) & 1023) == 16 ? 1 : 2;
                block.reads_lr |= block.indirect == 1;
                block.reads_ctr |= block.indirect == 2;
            }
            unsigned bo = (opcode >> 21) & 31;
            auto store_target = [&]() {
                if (block.indirect) {
                    // A preceding MTSPR in this block may have replaced the target.
                    emit_memory(bytes, 0x8b, block.indirect == 1 ? &block.lr : &block.ctr);
                    bytes.push_back(0x25);
                    emit_word(bytes, ~3u);
                }
                else {
                    bytes.push_back(0xb8);
                    emit_word(bytes, block.next_pc);
                }
                emit_memory(bytes, 0x89, &block.next_pc);
            };
            if (!branch_always(opcode)) {
                bytes.push_back(0xb8);
                emit_word(bytes,
                          block.link_pc); // Fallthrough, regardless of LK.
                emit_memory(bytes, 0x89, &block.next_pc);
                std::vector<size_t> skips;
                auto skip = [&](unsigned condition) {
                    bytes.insert(bytes.end(), {0x0f, (unsigned char)condition});
                    skips.push_back(bytes.size());
                    emit_word(bytes, 0);
                };
                if (!(bo & 4)) {
                    block.writes_ctr = true;
                    emit_memory(bytes, 0x8b, &block.ctr);
                    bytes.insert(bytes.end(), {0x83, 0xe8, 1});
                    emit_memory(bytes, 0x89, &block.ctr);
                    bytes.insert(bytes.end(), {0x85, 0xc0});
                    skip((bo & 2) ? 0x85 : 0x84);
                }
                if (!(bo & 16)) {
                    block.reads_cr = true;
                    emit_memory(bytes, 0x8b, &block.cr);
                    bytes.push_back(0xa9); // test eax,selected CR bit.
                    emit_word(bytes, 0x80000000u >> ((opcode >> 16) & 31));
                    skip((bo & 8) ? 0x84 : 0x85);
                }
                store_target();
                for (size_t offset : skips) {
                    uint32 delta = bytes.size() - offset - 4;
                    for (unsigned i = 0; i < 4; ++i)
                        bytes[offset + i] = delta >> (8 * i);
                }
            }
            else if (block.indirect)
                store_target();
            break;
        }
        if (primary == 19) {
            unsigned xo = (opcode >> 1) & 1023;
            unsigned dest_shift;
            uint32 mask;
            block.records = true;
            emit_memory(bytes, 0x8b, &block.cr);
            if (xo == 0) {
                dest_shift = 28 - ((opcode >> 23) & 7) * 4;
                unsigned source_shift = 28 - ((opcode >> 18) & 7) * 4;
                bytes.insert(bytes.end(), {0xc1, 0xe8, (unsigned char)source_shift, 0x83, 0xe0, 15});
                mask = 15u << dest_shift;
            }
            else {
                unsigned left_shift = 31 - ((opcode >> 16) & 31);
                unsigned right_shift = 31 - ((opcode >> 11) & 31);
                dest_shift = 31 - ((opcode >> 21) & 31);
                bytes.insert(bytes.end(), {0xc1, 0xe8, (unsigned char)left_shift, 0x83, 0xe0, 1});
                emit_memory(bytes, 0x8b, &block.cr, 1);
                bytes.insert(bytes.end(), {0xc1, 0xe9, (unsigned char)right_shift, 0x83, 0xe1, 1});
                if (xo == 129 || xo == 417)
                    bytes.insert(bytes.end(), {0x83, 0xf1, 1}); // xor ecx,1.
                unsigned operation = xo == 257 || xo == 129 || xo == 225  ? 0x21
                                     : xo == 449 || xo == 417 || xo == 33 ? 0x09
                                                                          : 0x31;
                bytes.insert(bytes.end(), {(unsigned char)operation, 0xc8});
                if (xo == 289 || xo == 225 || xo == 33)
                    bytes.insert(bytes.end(), {0x83, 0xf0, 1}); // xor eax,1.
                mask = 1u << dest_shift;
            }
            bytes.insert(bytes.end(), {0xc1, 0xe0, (unsigned char)dest_shift});
            emit_memory(bytes, 0x81, &block.cr, 4); // and [cr],~mask.
            emit_word(bytes, ~mask);
            emit_memory(bytes, 0x09, &block.cr); // or [cr],eax.
            continue;
        }
        unsigned ra = (opcode >> 16) & 31, rs = (opcode >> 21) & 31;
        unsigned secondary = (opcode >> 1) & 1023;
        if (primary == 31 && (secondary == 339 || secondary == 467)) {
            unsigned spr = spr_number(opcode);
            uint32* value = spr == 1 ? &block.xer : spr == 8 ? &block.lr : &block.ctr;
            if (secondary == 339) {
                block.reads_xer |= spr == 1;
                block.reads_lr |= spr == 8;
                block.reads_ctr |= spr == 9;
                emit_memory(bytes, 0x8b, value);
                emit_memory(bytes, 0x89, &cpu->gpr(rs));
            }
            else {
                block.writes_xer |= spr == 1;
                block.writes_lr |= spr == 8;
                block.writes_ctr |= spr == 9;
                emit_memory(bytes, 0x8b, &cpu->gpr(rs));
                if (spr == 1) {
                    // Preserve the interpreter's choice for reserved XER data bits.
                    bytes.push_back(0x25);
                    emit_word(bytes, 0xe000007f);
                }
                emit_memory(bytes, 0x89, value);
                if (spr == 1) {
                    bytes.insert(bytes.end(), {0xc1, 0xe8, 3, 0x25});
                    emit_word(bytes, 0x10000000);
                    emit_memory(bytes, 0x89, &block.so);
                }
            }
            continue;
        }
        if (primary == 31 && (secondary == 19 || secondary == 144 || secondary == 512)) {
            if (secondary == 19) {
                block.reads_cr = true;
                emit_memory(bytes, 0x8b, &block.cr);
                emit_memory(bytes, 0x89, &cpu->gpr(rs));
            }
            else {
                block.records = true;
                uint32 mask;
                if (secondary == 144) {
                    mask = 0;
                    for (unsigned field = 0; field < 8; ++field)
                        if (opcode & (0x80000u >> field))
                            mask |= 0xf0000000u >> (field * 4);
                    emit_memory(bytes, 0x8b, &cpu->gpr(rs));
                    bytes.push_back(0x25); // and eax,selected fields.
                    emit_word(bytes, mask);
                }
                else {
                    block.writes_xer = true;
                    unsigned shift = 28 - ((opcode >> 23) & 7) * 4;
                    mask = 15u << shift;
                    emit_memory(bytes, 0x8b, &block.xer);
                    bytes.insert(bytes.end(), {0xc1, 0xe8, 28, 0xc1, 0xe0, (unsigned char)shift});
                }
                emit_memory(bytes, 0x81, &block.cr, 4); // and [cr],~mask.
                emit_word(bytes, ~mask);
                emit_memory(bytes, 0x09, &block.cr); // or [cr],eax.
                if (secondary == 512) {
                    emit_memory(bytes, 0x81, &block.xer, 4);
                    emit_word(bytes, 0x0fffffff);
                    bytes.insert(bytes.end(), {0x31, 0xc0}); // xor eax,eax.
                    emit_memory(bytes, 0x89, &block.so);
                }
            }
            continue;
        }
        if (primary == 31 && (secondary == 778 || secondary == 552 || secondary == 616)) {
            unsigned rb = (opcode >> 11) & 31;
            emit_memory(bytes, 0x8b, &cpu->gpr(secondary == 552 ? rb : ra));
            if (secondary == 616)
                bytes.insert(bytes.end(), {0xf7, 0xd8}); // neg eax.
            else
                emit_memory(bytes, secondary == 778 ? 0x03 : 0x2b, &cpu->gpr(secondary == 778 ? rb : ra));
            emit_memory(bytes, 0x89, &cpu->gpr(rs));
            // MOV preserves the arithmetic OF for SETO.
            bytes.insert(bytes.end(), {0x0f, 0x90, 0xc1, 0x0f, 0xb6, 0xc9});
            write_overflow();
            if (opcode & 1) {
                emit_memory(bytes, 0x8b, &cpu->gpr(rs));
                record();
            }
            continue;
        }
        if (primary == 31 && ((secondary & 511) == 491 || (secondary & 511) == 459)) {
            bool sign = (secondary & 511) == 491;
            bool oe = secondary & 512;
            // Load both inputs before committing an aliased destination.
            emit_memory(bytes, 0x8b, &cpu->gpr((opcode >> 11) & 31));
            bytes.insert(bytes.end(), {0x89, 0xc1}); // mov ecx,eax.
            emit_memory(bytes, 0x8b, &cpu->gpr(ra));
            auto jump = [&](unsigned condition) {
                bytes.insert(bytes.end(), {0x0f, (unsigned char)condition});
                size_t offset = bytes.size();
                emit_word(bytes, 0);
                return offset;
            };
            auto patch = [&](size_t offset) {
                uint32 displacement = (uint32)(bytes.size() - offset - 4);
                for (unsigned i = 0; i < 4; ++i)
                    bytes[offset + i] = displacement >> (8 * i);
            };
            bytes.insert(bytes.end(), {0x85, 0xc9}); // test ecx,ecx.
            size_t zero = jump(0x84);
            size_t overflow = 0;
            if (sign) {
                bytes.push_back(0x3d);
                emit_word(bytes, 0x80000000);
                size_t ordinary = jump(0x85);
                bytes.insert(bytes.end(), {0x83, 0xf9, 0xff});
                overflow = jump(0x84); // INT_MIN / -1 faults on x86.
                patch(ordinary);
                bytes.insert(bytes.end(), {0x99, 0xf7, 0xf9}); // cdq; idiv ecx.
            }
            else
                bytes.insert(bytes.end(), {0x31, 0xd2, 0xf7, 0xf1}); // div ecx.
            if (oe)
                bytes.insert(bytes.end(), {0x31, 0xc9}); // xor ecx,ecx.
            bytes.push_back(0xe9);
            size_t done = bytes.size();
            emit_word(bytes, 0);
            patch(zero);
            if (overflow)
                patch(overflow);
            // Architecturally undefined cases retain the interpreter's choice.
            if (sign)
                bytes.insert(bytes.end(), {0xc1, 0xf8, 31});
            else
                bytes.insert(bytes.end(), {0x31, 0xc0});
            if (oe) {
                bytes.push_back(0xb9); // mov ecx,1 (overflow).
                emit_word(bytes, 1);
            }
            patch(done);
            emit_memory(bytes, 0x89, &cpu->gpr(rs));
            if (oe) {
                write_overflow();
                if (opcode & 1)
                    emit_memory(bytes, 0x8b, &cpu->gpr(rs));
            }
            if (opcode & 1)
                record();
            continue;
        }
        if (primary == 7 ||
            (primary == 31 && (secondary == 235 || secondary == 747 || secondary == 75 || secondary == 11))) {
            emit_memory(bytes, 0x8b, &cpu->gpr(ra));
            if (primary == 7) {
                bytes.insert(bytes.end(), {0x69, 0xc0}); // imul eax,eax,imm32.
                emit_word(bytes, (uint32)(int32)(int16)opcode);
            }
            else if (secondary == 235 || secondary == 747) {
                emit_memory(bytes, 0xaf, &cpu->gpr((opcode >> 11) & 31), 0, true);
            }
            else {
                // MUL/IMUL's implicit EDX:EAX result supplies the high word.
                emit_memory(bytes, 0xf7, &cpu->gpr((opcode >> 11) & 31), secondary == 75 ? 5 : 4);
                bytes.insert(bytes.end(), {0x89, 0xd0}); // mov eax,edx.
            }
            emit_memory(bytes, 0x89, &cpu->gpr(rs));
            if (primary == 31 && secondary == 747) {
                // IMUL's OF reports signed 32-bit overflow; MOV preserves it.
                bytes.insert(bytes.end(), {0x0f, 0x90, 0xc1, 0x0f, 0xb6, 0xc9});
                write_overflow();
                if (opcode & 1)
                    emit_memory(bytes, 0x8b, &cpu->gpr(rs));
            }
            if (primary == 31 && (opcode & 1))
                record();
            continue;
        }
        unsigned carry_xo = secondary & 511;
        bool extended = primary == 31 && (carry_xo == 138 || carry_xo == 136 || carry_xo == 234 || carry_xo == 202 ||
                                          carry_xo == 232 || carry_xo == 200);
        if (primary == 8 || primary == 12 || primary == 13 || (primary == 31 && (carry_xo == 8 || carry_xo == 10)) ||
            extended) {
            bool oe = primary == 31 && (secondary & 512);
            bool subtract = primary == 8 || (primary == 31 && carry_xo == 8);
            if (extended) {
                // BT loads live CA into CF. MOV and NOT preserve it; ADC computes
                // RA + RB + CA, or ~RA + RB + CA for PowerPC subtraction.
                emit_memory(bytes, 0x8b, &block.xer);
                bytes.insert(bytes.end(), {0x0f, 0xba, 0xe0, 29});
                emit_memory(bytes, 0x8b, &cpu->gpr(ra));
                if (carry_xo == 136 || carry_xo == 232 || carry_xo == 200)
                    bytes.insert(bytes.end(), {0xf7, 0xd0}); // not eax.
                if (carry_xo == 138 || carry_xo == 136)
                    emit_memory(bytes, 0x13, &cpu->gpr((opcode >> 11) & 31));
                else {
                    bytes.push_back(0x15); // adc eax,zero or minus one.
                    emit_word(bytes, carry_xo == 234 || carry_xo == 232 ? UINT32_MAX : 0);
                }
            }
            else if (primary == 8) {
                bytes.push_back(0xb8); // mov eax,signed immediate.
                emit_word(bytes, (uint32)(int32)(int16)opcode);
                emit_memory(bytes, 0x2b, &cpu->gpr(ra));
            }
            else if (primary == 31) {
                unsigned rb = (opcode >> 11) & 31;
                emit_memory(bytes, 0x8b, &cpu->gpr(subtract ? rb : ra));
                emit_memory(bytes, subtract ? 0x2b : 0x03, &cpu->gpr(subtract ? ra : rb));
            }
            else {
                // Unlike ADDI, ADDIC reads GPR0 when RA is zero.
                emit_memory(bytes, 0x8b, &cpu->gpr(ra));
                bytes.push_back(0x05); // add eax,signed immediate.
                emit_word(bytes, (uint32)(int32)(int16)opcode);
            }
            emit_memory(bytes, 0x89, &cpu->gpr(rs));
            if (oe)
                bytes.insert(bytes.end(), {0x0f, 0x90, 0xc2}); // seto dl.
            // MOV preserves CF. PowerPC subtraction CA means no borrow.
            bytes.insert(bytes.end(),
                         {0x0f, (unsigned char)(subtract ? 0x93 : 0x92), 0xc0, 0x0f, 0xb6, 0xc8, 0xc1, 0xe1, 29});
            block.writes_xer = true;
            emit_memory(bytes, 0x8b, &block.xer);
            bytes.push_back(0x25);
            emit_word(bytes, ~0x20000000u);
            bytes.insert(bytes.end(), {0x09, 0xc8});
            emit_memory(bytes, 0x89, &block.xer);
            if (oe) {
                bytes.insert(bytes.end(), {0x0f, 0xb6, 0xca}); // movzx ecx,dl.
                write_overflow();
            }
            if (primary == 13 || (primary == 31 && (opcode & 1))) {
                emit_memory(bytes, 0x8b, &cpu->gpr(rs));
                record();
            }
            continue;
        }
        if (primary == 10 || primary == 11 || (primary == 31 && (secondary == 0 || secondary == 32))) {
            bool logical = primary == 10 || (primary == 31 && secondary == 32);
            emit_memory(bytes, 0x8b, &cpu->gpr(ra));
            if (primary == 31)
                emit_memory(bytes, 0x3b, &cpu->gpr((opcode >> 11) & 31));
            else {
                bytes.push_back(0x3d); // cmp eax, immediate.
                emit_word(bytes, logical ? opcode & 0xffff : (uint32)(int32)(int16)opcode);
            }
            // CMP may overflow: use signed GE, or unsigned AE, rather than SF.
            bytes.push_back(0xb8);
            emit_word(bytes, 0x20000000);
            bytes.insert(bytes.end(), {0x74, 12, 0xb8});
            emit_word(bytes, 0x40000000);
            bytes.insert(bytes.end(), {(unsigned char)(logical ? 0x73 : 0x7d), 5, 0xb8});
            emit_word(bytes, 0x80000000);
            store_condition((opcode >> 23) & 7);
            continue;
        }
        if (primary == 20 || primary == 21 || primary == 23) {
            unsigned shift = (opcode >> 11) & 31;
            unsigned begin = (opcode >> 6) & 31, end = (opcode >> 1) & 31;
            uint32 left = UINT32_MAX >> begin;
            uint32 right = UINT32_MAX << (31 - end);
            uint32 mask = begin <= end ? left & right : left | right;
            if (primary == 23) {
                emit_memory(bytes, 0x8b, &cpu->gpr(shift));
                bytes.insert(bytes.end(), {0x89, 0xc1}); // mov ecx,eax.
            }
            emit_memory(bytes, 0x8b, &cpu->gpr(rs));
            if (primary == 23)
                bytes.insert(bytes.end(), {0xd3, 0xc0}); // rol eax,cl.
            else
                bytes.insert(bytes.end(), {0xc1, 0xc0, (unsigned char)shift});
            bytes.push_back(0x25); // and eax,mask.
            emit_word(bytes, mask);
            if (primary == 20) {
                bytes.insert(bytes.end(), {0x89, 0xc1}); // mov ecx,eax.
                emit_memory(bytes, 0x8b, &cpu->gpr(ra));
                bytes.push_back(0x25);
                emit_word(bytes, ~mask);
                bytes.insert(bytes.end(), {0x09, 0xc8}); // or eax,ecx.
            }
            emit_memory(bytes, 0x89, &cpu->gpr(ra));
            if (opcode & 1)
                record();
            continue;
        }
        if (primary == 31) {
            unsigned rb = (opcode >> 11) & 31;
            if (secondary == 26 || secondary == 954 || secondary == 922 || secondary == 104) {
                unsigned source = secondary == 104 ? ra : rs;
                unsigned destination = secondary == 104 ? rs : ra;
                emit_memory(bytes, 0x8b, &cpu->gpr(source));
                if (secondary == 104)
                    bytes.insert(bytes.end(), {0xf7, 0xd8}); // neg eax.
                else if (secondary == 954 || secondary == 922)
                    bytes.insert(bytes.end(),
                                 {0x0f, (unsigned char)(secondary == 954 ? 0xbe : 0xbf), 0xc0}); // movsx eax,al/ax.
                else {
                    // BSR's zero destination is undefined; explicitly select 32.
                    bytes.insert(bytes.end(), {0x85, 0xc0, 0x74, 8, 0x0f, 0xbd, 0xc0, 0x83, 0xf0, 31, 0xeb, 5, 0xb8});
                    emit_word(bytes, 32);
                }
                emit_memory(bytes, 0x89, &cpu->gpr(destination));
                if (opcode & 1)
                    record();
                continue;
            }
            if (secondary == 24 || secondary == 536 || secondary == 792 || secondary == 824) {
                bool arithmetic = secondary == 792 || secondary == 824;
                if (secondary == 824) {
                    bytes.push_back(0xb9); // mov ecx,SH.
                    emit_word(bytes, rb);
                }
                else {
                    emit_memory(bytes, 0x8b, &cpu->gpr(rb));
                    bytes.insert(bytes.end(), {0x89, 0xc1}); // mov ecx,eax.
                }
                emit_memory(bytes, 0x8b, &cpu->gpr(rs));
                if (arithmetic)
                    bytes.insert(bytes.end(), {0x89, 0xc2}); // mov edx,RS.
                bytes.insert(bytes.end(), {0xd3, (unsigned char)(arithmetic ? 0xf8 : secondary == 24 ? 0xe0 : 0xe8)});
                // x86 masks counts to five bits. PPC bit 5 selects zero for
                // logical shifts or the sign word for arithmetic shifts.
                bytes.insert(bytes.end(), {0xf6, 0xc1, 0x20, 0x74});
                if (arithmetic)
                    bytes.insert(bytes.end(), {3, 0xc1, 0xf8, 31});
                else
                    bytes.insert(bytes.end(), {2, 0x31, 0xc0});
                emit_memory(bytes, 0x89, &cpu->gpr(ra));
                if (arithmetic) {
                    block.writes_xer = true;
                    // Reconstruct the retained bits; a negative source sets CA
                    // iff any bits were discarded. Counts >=32 discard all bits.
                    bytes.insert(bytes.end(), {0xd3, 0xe0}); // shl eax,cl.
                    bytes.insert(bytes.end(), {0xf6, 0xc1, 0x20, 0x74, 2, 0x31, 0xc0});
                    bytes.insert(bytes.end(), {0x39, 0xd0, 0x0f, 0x95, 0xc0}); // AL=lost.
                    bytes.insert(bytes.end(), {0x85, 0xd2, 0x0f, 0x98, 0xc2}); // DL=sign.
                    bytes.insert(bytes.end(),
                                 {0x20, 0xd0, 0x0f, 0xb6, 0xc0, 0xc1, 0xe0, 29, 0x89, 0xc1}); // ECX=(lost & sign)<<29.
                    emit_memory(bytes, 0x8b, &block.xer);
                    bytes.push_back(0x25);
                    emit_word(bytes, ~0x20000000u);
                    bytes.insert(bytes.end(), {0x09, 0xc8});
                    emit_memory(bytes, 0x89, &block.xer);
                    if (opcode & 1)
                        emit_memory(bytes, 0x8b, &cpu->gpr(ra));
                }
                if (opcode & 1)
                    record();
                continue;
            }
            bool arithmetic = secondary == 266 || secondary == 40;
            bool complement_source = secondary == 60 || secondary == 412;
            unsigned source = secondary == 40 || complement_source ? rb : arithmetic ? ra : rs;
            unsigned other = secondary == 40 ? ra : complement_source ? rs : rb;
            unsigned destination = arithmetic ? rs : ra;
            emit_memory(bytes, 0x8b, &cpu->gpr(source));
            if (complement_source) {
                bytes.push_back(0xf7);
                bytes.push_back(0xd0); /* not eax */
            }
            unsigned instruction = secondary == 266                                           ? 0x03
                                   : secondary == 40                                          ? 0x2b
                                   : secondary == 28 || secondary == 60 || secondary == 476   ? 0x23
                                   : secondary == 124 || secondary == 412 || secondary == 444 ? 0x0b
                                                                                              : 0x33;
            emit_memory(bytes, instruction, &cpu->gpr(other));
            if (secondary == 124 || secondary == 284 || secondary == 476) {
                bytes.push_back(0xf7);
                bytes.push_back(0xd0);
            }
            emit_memory(bytes, 0x89, &cpu->gpr(destination));
            if (opcode & 1)
                record();
            continue;
        }
        bool add = primary == 14 || primary == 15;
        unsigned source = add ? ra : rs, destination = add ? rs : ra;
        uint32 immediate = add ? (uint32)(int32)(int16)opcode : opcode & 0xffff;
        if (primary == 15 || primary == 25 || primary == 27 || primary == 29)
            immediate <<= 16;
        if (add && !ra) {
            bytes.push_back(0xb8); /* mov eax, immediate. */
            emit_word(bytes, immediate);
        }
        else {
            emit_memory(bytes, 0x8b, &cpu->gpr(source));
            bytes.push_back(add ? 0x05 : primary <= 25 ? 0x0d : primary <= 27 ? 0x35 : 0x25);
            emit_word(bytes, immediate); /* add/or/xor/and eax, immediate. */
        }
        emit_memory(bytes, 0x89, &cpu->gpr(destination));
        if (primary == 28 || primary == 29)
            record();
    }
    for (unsigned i = 0; i < stop_count; ++i)
        point_jump_here(bytes, budget_stops[i]);
    bytes.push_back(0xc3); /* ret; EAX is volatile in the host ABI. */
    // Failed transfer guards return before their own memory operation.
    for (const auto& transfer : transfers) {
        for (size_t branch : transfer.branches)
            point_jump_here(bytes, branch);
        bytes.push_back(0xb8);
        emit_word(bytes, transfer.prefix);
        emit_memory(bytes, 0x89, &block.executed);
        bytes.push_back(0xc3);
    }
}

bool compile(native_block& block, powerpc_cpu* cpu, uint32 pc)
{
    std::vector<unsigned char> bytes;
    emit_native_block(block, cpu, pc, bytes);
    if (bytes.size() > max_native_block_size)
        return false;
    return publish_native_block(block, bytes.data(), bytes.size());
}

void commit_native(powerpc_cpu* cpu, const native_retirement& block, native_block& invocation)
{
    ROMlib_ppc_environment* env = romaw_ppc_environment;
    bool complete = invocation.executed == block.words;
    if (block.writes_fpscr) {
        any_register value;
        value.i = invocation.fpscr;
        bool changed_rounding = (value.i ^ cpu->get_register(powerpc_registers::FPSCR).i) & 3;
        cpu->set_register(powerpc_registers::FPSCR, value);
        if (changed_rounding)
            romaw_ppc_set_fpscr_rounding(value.i);
    }
    if (block.writes_xer) {
        any_register value;
        value.i = invocation.xer;
        cpu->set_register(powerpc_registers::XER, value);
    }
    if (block.records) {
        any_register value;
        value.i = invocation.cr;
        cpu->set_register(powerpc_registers::CR, value);
    }
    if ((complete && block.links) || block.writes_lr) {
        any_register value;
        value.i = complete && block.links ? block.link_pc : invocation.lr;
        cpu->set_register(powerpc_registers::LR, value);
    }
    if (block.writes_ctr) {
        any_register value;
        value.i = invocation.ctr;
        cpu->set_register(powerpc_registers::CTR, value);
    }
    *invocation.continuation = complete && (block.semantic || block.branches || invocation.tail_completed)
                                   ? invocation.next_pc
                                   : *invocation.continuation + invocation.executed * 4;
    env->instructions += invocation.executed - 1;
    ++env->native_blocks;
    env->native_instructions += invocation.executed;
}

void semantic_tail(powerpc_cpu*, native_block*);

void transfer_system_operand(powerpc_cpu* cpu, system_memory_operand operand, uint32 address, uint64 value,
                             unsigned count)
{
    switch (operand.kind) {
    case system_operand_kind::spr_write:
        romaw_ppc_750_write_spr(operand.selector, uint32(value), cpu->romaw_registers().pc);
        return;
    case system_operand_kind::cache_control:
        romaw_ppc_cache_control(address, operand.selector);
        return;
    case system_operand_kind::external: {
        uint32 result = romaw_ppc_external(address, operand.write, uint32(value));
        if (!operand.write)
            cpu->gpr(operand.reg) = result;
        return;
    }
    case system_operand_kind::multiple:
        romaw_ppc_transfer_multiple(cpu, address, operand.reg, operand.write);
        return;
    case system_operand_kind::string:
        if (!operand.write && count > operand.string_limit)
            romaw_ppc_invalid_instruction("illegal PowerPC string-load register");
        romaw_ppc_transfer_string(cpu, address, operand.reg, count, operand.write);
        return;
    case system_operand_kind::reservation:
        if (operand.write)
            romaw_ppc_store_conditional(cpu, address, uint32(value));
        else
            cpu->gpr(operand.reg) = romaw_ppc_load_reserved(cpu, address);
        return;
    case system_operand_kind::integer:
    case system_operand_kind::floating:
    case system_operand_kind::none:
        break;
    }
    auto reverse = [operand](uint32 input) -> uint32 {
        return operand.size == 2 ? __builtin_bswap16(input) : __builtin_bswap32(input);
    };
    if (operand.write)
        romaw_ppc_store(address, operand.size, operand.reverse ? reverse(value) : value);
    else {
        value = romaw_ppc_load(address, operand.size);
        if (operand.kind == system_operand_kind::floating)
            cpu->fpr_dw(operand.reg) = operand.size == 4 ? romaw_ppc_widen_fp_single(uint32(value)) : value;
        else {
            if (operand.reverse)
                value = reverse(value);
            if (operand.sign_extend)
                value = uint32(int32(int16(value)));
            cpu->gpr(operand.reg) = value;
        }
    }
    if (operand.update_reg)
        cpu->gpr(operand.update_reg) = address;
}

bool run_native(powerpc_cpu* cpu, uint32* pc, native_block& block, unsigned budget)
{
    auto* env = romaw_ppc_environment;
    const native_retirement retirement(block);
    const auto allocation = block.allocation;
    native_block invocation;
    invocation.next_pc = block.next_pc;
    invocation.system = env->system;
    const auto operand = retirement.system_operand;
    const bool state_effect =
        retirement.writes_system_msr || retirement.system_operand.kind == system_operand_kind::spr_write;
    const bool direct_bus =
        operand.kind == system_operand_kind::integer || operand.kind == system_operand_kind::floating ||
        operand.kind == system_operand_kind::cache_control || operand.kind == system_operand_kind::external ||
        operand.kind == system_operand_kind::multiple || operand.kind == system_operand_kind::string ||
        operand.kind == system_operand_kind::reservation;
    const auto memory_kernel =
        (state_effect || direct_bus) ? romaw_ppc_native_memory_code() : std::shared_ptr<ppc_native_memory_kernel>();
    invocation.native_frame.system = env->system;
    if (memory_kernel) {
        auto operation = retirement.writes_system_msr ? ppc_native_memory_kernel::msr : ppc_native_memory_kernel::spr;
        if (direct_bus)
            operation =
                operand.kind == system_operand_kind::cache_control ? ppc_native_memory_kernel::cache
                : operand.kind == system_operand_kind::external    ? ppc_native_memory_kernel::external
                : operand.kind == system_operand_kind::reservation
                    ? (operand.write ? ppc_native_memory_kernel::conditional : ppc_native_memory_kernel::reserve)
                : operand.write ? ppc_native_memory_kernel::write
                                : ppc_native_memory_kernel::read;
        invocation.native_entry = memory_kernel->get(operation);
    }
    invocation.executed = std::min<unsigned>(block.words.size(), budget);
    invocation.continuation = pc;
    if (block.semantic || block.slow_path) {
        invocation.tail = semantic_tail;
        invocation.description = &retirement;
    }
    if (block.memory) {
        invocation.ram = env->linear_memory;
        invocation.ram_address = env->linear_address;
        invocation.ram_size = env->linear_size;
    }
    typedef void (*entry_point)(powerpc_cpu*, native_block*);
    if (block.reads_xer || block.writes_xer)
        invocation.xer = cpu->get_register(powerpc_registers::XER).i;
    if (block.reads_ctr || block.writes_ctr)
        invocation.ctr = cpu->get_register(powerpc_registers::CTR).i;
    if (block.reads_lr || block.writes_lr)
        invocation.lr = cpu->get_register(powerpc_registers::LR).i;
    if (block.reads_fpscr)
        invocation.fpscr = cpu->get_register(powerpc_registers::FPSCR).i;
    if (block.records || block.reads_cr) {
        invocation.cr = cpu->get_register(powerpc_registers::CR).i;
        invocation.so = (cpu->get_register(powerpc_registers::XER).i >> 3) & 0x10000000;
    }
    if (operand.kind != system_operand_kind::none) {
        // These architectural operand records are singletons. Callbacks must
        // observe the attempted native step before entering the emitted bus.
        ++env->native_blocks;
        ++env->native_instructions;
    }
    if (direct_bus) {
        romaw_ppc_native_bus_scope boundary(env->physical_bus, &invocation.native_frame.serial);
        invocation.native_frame.bus = boundary.bus();
        reinterpret_cast<entry_point>(block.code)(cpu, &invocation);
        boundary.rethrow();
    }
    else
        reinterpret_cast<entry_point>(block.code)(cpu, &invocation);
    if (invocation.tail_completed)
        return true; // The tail committed before a callback could replace this block.
    if (operand.kind != system_operand_kind::none) {
        if (invocation.breakpoint_pending) {
            romaw_ppc_data_breakpoint(invocation.native_frame.address, invocation.native_frame.size, operand.write);
            throw std::runtime_error("PowerPC data breakpoint changed during fault handling");
        }
        if (invocation.invalid_string)
            romaw_ppc_invalid_instruction("illegal PowerPC string-load register");
        if (invocation.native_completed) {
            if (operand.kind == system_operand_kind::spr_write) {
                if (invocation.native_frame.ok < 0)
                    throw std::runtime_error("PowerPC 750 L2 storage is unavailable or busy");
                if (!invocation.native_frame.ok)
                    romaw_ppc_invalid_instruction("invalid PowerPC 750 SPR selector");
            }
            else if (!invocation.native_frame.ok) {
                const auto& mapping = invocation.native_frame.result.mapping;
                if (!mapping.vector)
                    throw std::runtime_error(
                        operand.kind == system_operand_kind::cache_control ? "invalid PowerPC 750 cache-control bus"
                        : operand.kind == system_operand_kind::external    ? "invalid PowerPC 750 external-control bus"
                        : operand.kind == system_operand_kind::reservation && !operand.write
                            ? "invalid PowerPC 750 reservation bus"
                            : "invalid PowerPC 750 bus configuration");
                throw romaw_ppc_arch_fault{mapping.vector, mapping.cause,
                                           operand.kind == system_operand_kind::external ? invocation.operand_address
                                                                                         : mapping.address};
            }
        }
        else
            transfer_system_operand(cpu, operand, invocation.operand_address, invocation.operand_value,
                                    invocation.operand_count);
        *pc += 4;
        return true;
    }
    if (invocation.exception_vector) {
        // Architectural exception records are singletons. Count the step
        // and raise from C++ with the faulting PC and guest state intact.
        ++env->native_blocks;
        ++env->native_instructions;
        throw romaw_ppc_arch_fault{invocation.exception_vector, invocation.exception_vector == 0x700 ? 0x20000u : 0, 0};
    }
    if (retirement.writes_system_msr) {
        // A board can assert checkstop during the checked instruction fetch.
        // RFI then leaves both the MSR and the return context untouched.
        if (retirement.branches && invocation.system->checkstop)
            invocation.next_pc = *pc;
        else if (!invocation.native_completed) {
            ROMlib_ppc_750_set_msr(invocation.system, invocation.msr_value, *pc);
            if (retirement.branches)
                invocation.system->iabr_pending = 0;
        }
    }
    if (!invocation.executed)
        return false; // A singleton memory guard leaves the counted step live.
    commit_native(cpu, retirement, invocation);
    return true;
}

void semantic_tail(powerpc_cpu* cpu, native_block* invocation)
{
    const native_retirement& block = *invocation->description;
    uint32 opcode = block.opcode;
    const bool architectural = block.architectural;
    invocation->tail_completed = true;
    commit_native(cpu, block, *invocation);
    // The complete prefix state and instruction count are visible before any
    // memory/service callback or fault. No cache metadata is read afterwards.
    ++romaw_ppc_environment->native_kernel_instructions;
    if (architectural) {
        // Prevent recursive compilation of the checked control helper. A nested
        // guest invocation installs its own context and restores this suspension.
        auto* previous = active_context;
        active_context = nullptr;
        try {
            romaw_ppc_system_kernel(cpu, opcode);
        }
        catch (...) {
            active_context = previous;
            throw;
        }
        active_context = previous;
        return;
    }
    cpu->romaw_instruction(opcode)->execute(cpu, opcode);
}

bool matches_words(const std::vector<uint32>& words, const romaw_ppc_code_range& range)
{
    if (words.size() > range.size / 4)
        return false;
    for (size_t i = 0; i < words.size(); ++i)
        if (words[i] != read_word(range.data + i * 4))
            return false;
    return true;
}

bool matches_translation(const native_block& block, const romaw_ppc_code_range& range)
{
    return block.code && matches_words(block.words, range);
}

native_key translation_key(const romaw_ppc_code_range& range, bool architectural)
{
    return translation_key(range.address, architectural ? read_word(range.data) : 0, architectural);
}

std::unique_ptr<native_block> build_translation(powerpc_cpu& layout, const romaw_ppc_code_range& range,
                                                bool architectural)
{
    std::unique_ptr<native_block> block(new native_block);
    block->architectural = architectural;
    for (unsigned i = 0; i < max_words && (i + 1) * 4 <= range.size; ++i) {
        uint32 opcode = read_word(range.data + i * 4);
        if (!supported(opcode, &layout, architectural))
            break;
        block->words.push_back(opcode);
        if (architectural || ends_block(opcode))
            break;
    }
    if (!block->words.empty() && !compile(*block, &layout, range.address))
        return nullptr;
    return block;
}

using unused_block = std::pair<native_store::block_map*, native_key>;
using unused_blocks = std::vector<unused_block>;

uint32 translate_block(native_store::block_map& blocks, native_key key, native_store::block_map::iterator found,
                       powerpc_cpu& layout, const romaw_ppc_code_range& range, bool architectural, unsigned& translated,
                       unused_blocks* unused = nullptr, const native_store::omitted_map* omitted = nullptr)
{
    if (found != blocks.end() && matches_translation(*found->second, range))
        return found->second->words.size() * 4;
    if (found == blocks.end() && omitted) {
        auto skipped = omitted->find(key);
        if (skipped != omitted->end() && matches_words(skipped->second, range))
            return skipped->second.size() * 4;
    }
    auto block = build_translation(layout, range, architectural);
    if (!block)
        return 0;
    if (block->words.empty()) {
        if (found != blocks.end())
            blocks.erase(found);
        return 4;
    }

    uint32 size = block->words.size() * 4;
    if (found != blocks.end()) {
        // Replace only after the new code is executable. A failed publication
        // leaves the previous record available if its original source returns.
        block->last_used = found->second->last_used;
        if (found->first == key)
            found->second = std::move(block);
        else {
            auto previous = found->first;
            blocks.emplace(key, std::move(block));
            blocks.erase(previous);
        }
    }
    else {
        blocks.emplace(key, std::move(block));
        if (unused) {
            auto victim = unused->back();
            victim.first->erase(victim.second);
            unused->pop_back();
        }
    }
    ++translated;
    return size;
}

void translate_blocks(native_store& store, powerpc_cpu& layout, const romaw_ppc_code_range& range, bool architectural,
                      unsigned& translated, size_t ceiling, unused_blocks* unused = nullptr,
                      const native_store::omitted_map* omitted = nullptr)
{
    auto& blocks = architectural ? store.system_blocks : store.blocks;
    for (uint32 position = 0; range.size - position >= 4 && store.size() < ceiling + (unused ? unused->size() : 0);) {
        romaw_ppc_code_range remaining = {range.data + position, range.address + position, range.size - position};
        auto victims = store.size() == store.limit ? unused : nullptr;
        auto key = translation_key(remaining, architectural);
        uint32 size = translate_block(blocks, key, blocks.find(key), layout, remaining, architectural, translated,
                                      victims, omitted);
        if (!size)
            break;
        position += size;
    }
}

void refresh_full_range(native_store::block_map& blocks, powerpc_cpu& layout, const romaw_ppc_code_range& range,
                        bool architectural, unsigned& translated)
{
    // Only existing starts can reuse a slot at capacity. Bound the scan by
    // the number of cached blocks, regardless of the requested range size.
    std::vector<native_key> candidates;
    for (const auto& entry : blocks) {
        uint32 pc = uint32(entry.first), offset = pc - range.address;
        if (offset > range.size - 4)
            continue;
        if (architectural && blocks.count(translation_key(pc, read_word(range.data + offset), true)))
            continue;
        candidates.push_back(entry.first);
    }
    if (architectural) {
        // If several resident views share an address, replace the least
        // recently used one when there is no room for the new view.
        std::sort(candidates.begin(), candidates.end(), [&](native_key a, native_key b) {
            auto used_a = blocks.at(a)->last_used, used_b = blocks.at(b)->last_used;
            return used_a != used_b ? used_a < used_b : a < b;
        });
    }
    for (auto previous : candidates) {
        uint32 pc = uint32(previous), offset = pc - range.address;
        romaw_ppc_code_range remaining = {range.data + offset, pc, range.size - offset};
        auto key = translation_key(remaining, architectural);
        if (previous != key && (blocks.count(key) || !supported(read_word(remaining.data), &layout, true)))
            continue;
        if (!translate_block(blocks, key, blocks.find(previous), layout, remaining, architectural, translated))
            break;
    }
}

struct code_range_plan {
    romaw_ppc_code_range range;
    std::vector<uint32> starts;
    bool limited;
};
}
#endif

extern "C" int ROMlib_ppc_jit_available(void)
{
    return ROMAW_PPC_NATIVE_JIT;
}

extern "C" ROMlib_ppc_translation_cache* ROMlib_ppc_translation_cache_create(void)
{
#if ROMAW_PPC_NATIVE_JIT
    if (!ROMlib_ppc_jit_available())
        return nullptr;
    try {
        std::unique_ptr<native_store> store(new native_store);
        store->limit = 4096;
        std::unique_ptr<ROMlib_ppc_translation_cache> cache(new ROMlib_ppc_translation_cache);
        cache->implementation = store.release();
        return cache.release();
    }
    catch (...) {
    }
#endif
    return nullptr;
}

extern "C" void ROMlib_ppc_translation_cache_destroy(ROMlib_ppc_translation_cache* cache)
{
    if (!cache)
        return;
#if ROMAW_PPC_NATIVE_JIT
    delete static_cast<native_store*>(cache->implementation);
#endif
    delete cache;
}

extern "C" unsigned ROMlib_ppc_translation_cache_load(ROMlib_ppc_translation_cache* cache, const char* path)
{
#if ROMAW_PPC_NATIVE_JIT
    if (!cache || !path || !*path || sizeof cache_build != 65)
        return 0;
    auto store = static_cast<native_store*>(cache->implementation);
    if (store->busy)
        return 0;
    try {
        std::unique_ptr<FILE, decltype(&fclose)> file(cache_open(path, false), fclose);
        if (!file || fseek(file.get(), 0, SEEK_END))
            return 0;
        long size = ftell(file.get());
        if (size < cache_minimum_size || (uint64)size > cache_byte_limit || fseek(file.get(), 0, SEEK_SET))
            return 0;
        std::vector<unsigned char> bytes(size);
        if (fread(bytes.data(), 1, size, file.get()) != (size_t)size)
            return 0;
        cache_reader tail(bytes);
        tail.position = size - 4;
        if (tail.word() != cache_crc(bytes.data(), size - 4))
            return 0;
        cache_reader input(bytes);
        if (memcmp(input.take(8), "EXPAOT04", 8) || memcmp(input.take(64), cache_build, 64) ||
            input.word() != cache_abi)
            return 0;
        unsigned count = input.word();
        if (count > 2 * store->limit)
            return 0;
        native_store candidate;
        std::vector<std::pair<native_block*, const unsigned char*>> pending;
        std::vector<unsigned char> expected;
        powerpc_cpu layout;
        for (unsigned index = 0; index < count; ++index) {
            uint32 pc = input.word(), words = input.word();
            uint32 flags = input.word();
            if ((pc & 3) || words < 1 || words > max_words || (flags & ~0xffffu) ||
                (uint64)pc + words * 4 > UINT64_C(0x100000000))
                return 0;
            std::unique_ptr<native_block> block(new native_block);
            block->architectural = flags & cache_architectural;
            uint32 next_pc = input.word(), link_pc = input.word(), indirect = input.word();
            uint32 code_size = input.word();
            bool omitted_record = flags & cache_omitted;
            if ((block->architectural && words != 1) || indirect > 2)
                return 0;
            if (omitted_record) {
                if ((flags & ~(cache_omitted | cache_architectural)) || next_pc || link_pc || indirect || code_size)
                    return 0;
            }
            else if (code_size < 11 || code_size > max_native_block_size || pending.size() == store->limit)
                return 0;
            for (unsigned i = 0; i < words; ++i) {
                uint32 opcode = input.word();
                if (!supported(opcode, &layout, block->architectural) || (i + 1 < words && ends_block(opcode)))
                    return 0;
                block->words.push_back(opcode);
            }
            auto& records = block->architectural ? candidate.system_blocks : candidate.blocks;
            auto& omitted = candidate.omitted[block->architectural];
            auto key = translation_key(pc, block->words.front(), block->architectural);
            if (omitted_record) {
                if (records.count(key) || !omitted.emplace(key, std::move(block->words)).second)
                    return 0;
                continue;
            }
            if (omitted.count(key))
                return 0;
            auto native = input.take(code_size);
            // Build identity and CRC cannot prove that the machine code or
            // register/branch metadata corresponds to these guest words.
            emit_native_block(*block, &layout, pc, expected);
            if (cache_flags(*block) != flags || block->next_pc != next_pc || block->link_pc != link_pc ||
                block->indirect != indirect || expected.size() != code_size ||
                memcmp(native, expected.data(), code_size))
                return 0;
            block->code_size = code_size;
            auto description = block.get();
            if (!records.emplace(key, std::move(block)).second)
                return 0;
            pending.emplace_back(description, native);
        }
        if (input.position != bytes.size() - 4)
            return 0;
        // No executable allocation until every record and the final extent
        // have validated. Failure still leaves the caller's old cache intact.
        for (auto& entry : pending) {
            if (!publish_native_block(*entry.first, entry.second, entry.first->code_size))
                return 0;
        }
        store->blocks.swap(candidate.blocks);
        store->system_blocks.swap(candidate.system_blocks);
        for (unsigned mode = 0; mode < 2; ++mode)
            store->omitted[mode].swap(candidate.omitted[mode]);
        store->heat.reset();
        store->clock = 0;
        return pending.size();
    }
    catch (...) {
    }
#endif
    return 0;
}

extern "C" int ROMlib_ppc_translation_cache_save(ROMlib_ppc_translation_cache* cache, const char* path)
{
#if ROMAW_PPC_NATIVE_JIT
    if (!cache || !path || !*path || sizeof cache_build != 65)
        return 0;
    auto store = static_cast<native_store*>(cache->implementation);
    // The owning host-service scope has suspended guest execution and can
    // snapshot fully published records without changing the live cache.
    if (store->busy && !cache_in_host_service(*store))
        return 0;
    std::string temporary;
    try {
        struct cache_record {
            uint32 pc;
            bool architectural;
            const std::vector<uint32>* words;
            const native_block* block;
        };

        std::vector<cache_record> records;
        records.reserve(store->size());
        size_t metadata_size = cache_minimum_size, native_bytes = 0;
        auto add = [&](uint32 pc, bool architectural, const std::vector<uint32>& words, const native_block* block) {
            records.push_back({pc, architectural, &words, block});
            metadata_size += 7 * 4 + words.size() * 4;
            if (block)
                native_bytes += block->code_size;
        };
        for (auto* map : {&store->blocks, &store->system_blocks})
            for (const auto& entry : *map)
                if (entry.second->code)
                    add(uint32(entry.first), entry.second->architectural, entry.second->words, entry.second.get());
        for (unsigned mode = 0; mode < 2; ++mode) {
            const auto& blocks = mode ? store->system_blocks : store->blocks;
            for (const auto& entry : store->omitted[mode]) {
                if (records.size() >= 2 * store->limit)
                    break;
                if (!blocks.count(entry.first))
                    add(uint32(entry.first), mode != 0, entry.second, nullptr);
            }
        }
        if (metadata_size > cache_byte_limit)
            return 0;
        size_t available = cache_byte_limit - metadata_size;
        if (native_bytes > available) {
            // The resident cache is limited by block count. Under disk byte
            // pressure retain recent executions and priority entries first.
            std::sort(records.begin(), records.end(), [](const cache_record& a, const cache_record& b) {
                uint64 used_a = a.block ? a.block->last_used : 0, used_b = b.block ? b.block->last_used : 0;
                if (used_a != used_b)
                    return used_a > used_b;
                if (a.pc != b.pc)
                    return a.pc < b.pc;
                if (a.architectural != b.architectural)
                    return a.architectural < b.architectural;
                return a.architectural && a.words->front() < b.words->front();
            });
        }
        std::vector<unsigned char> bytes = {'E', 'X', 'P', 'A', 'O', 'T', '0', '4'};
        bytes.reserve(metadata_size + std::min(native_bytes, available));
        bytes.insert(bytes.end(), cache_build, cache_build + 64);
        emit_word(bytes, cache_abi);
        emit_word(bytes, records.size());
        // Keep source words even when native code will not fit. These records
        // only inform eager scans and are never published as executable code.
        for (const auto& record : records) {
            const native_block* block = record.block;
            if (block && block->code_size > available)
                block = nullptr; // A smaller body may still fit.
            emit_word(bytes, record.pc);
            emit_word(bytes, record.words->size());
            emit_word(bytes,
                      block ? cache_flags(*block) : cache_omitted | (record.architectural ? cache_architectural : 0));
            emit_word(bytes, block ? block->next_pc : 0);
            emit_word(bytes, block ? block->link_pc : 0);
            emit_word(bytes, block ? block->indirect : 0);
            emit_word(bytes, block ? block->code_size : 0);
            for (uint32 word : *record.words)
                emit_word(bytes, word);
            if (block) {
                auto code = static_cast<const unsigned char*>(block->code);
                bytes.insert(bytes.end(), code, code + block->code_size);
                available -= block->code_size;
            }
        }
        emit_word(bytes, cache_crc(bytes.data(), bytes.size()));
#if defined(_WIN32)
        auto destination = cache_wide(path);
#endif
        std::unique_ptr<FILE, decltype(&fclose)> file(cache_temporary(path, temporary), fclose);
        if (file) {
            bool ok = fwrite(bytes.data(), 1, bytes.size(), file.get()) == bytes.size();
            ok = cache_sync(file.get()) && ok;
            ok = fclose(file.release()) == 0 && ok;
#if defined(_WIN32)
            if (ok && MoveFileExW(cache_wide(temporary.c_str()).c_str(), destination.c_str(),
                                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return 1;
#else
            if (ok && !rename(temporary.c_str(), path))
                return 1;
#endif
        }
    }
    catch (...) {
    }
    if (!temporary.empty()) {
#if defined(_WIN32)
        try {
            _wremove(cache_wide(temporary.c_str()).c_str());
        }
        catch (...) {
        }
#else
        remove(temporary.c_str());
#endif
    }
#endif
    return 0;
}

extern "C" unsigned ROMlib_ppc_translate_range(ROMlib_ppc_translation_cache* cache, const void* data, uint32_t address,
                                               uint32_t length)
{
    return ROMlib_ppc_translate_range_mode(cache, data, address, length, 0);
}

extern "C" unsigned ROMlib_ppc_translate_range_mode(ROMlib_ppc_translation_cache* cache, const void* data,
                                                    uint32_t address, uint32_t length, unsigned mode)
{
#if ROMAW_PPC_NATIVE_JIT
    if ((mode & ~ROMLIB_PPC_TRANSLATE_750) || !cache || !data || length < 4 || (address & 3) ||
        (uint64)address + length > UINT64_C(0x100000000))
        return 0;
    auto store = static_cast<native_store*>(cache->implementation);
    if (store->busy)
        return 0;
    unsigned translated = 0;
    try {
        powerpc_cpu layout;
        romaw_ppc_code_range range = {static_cast<const unsigned char*>(data), address, length};
        if (store->size() >= store->limit)
            refresh_full_range(mode ? store->system_blocks : store->blocks, layout, range, mode != 0, translated);
        else
            translate_blocks(*store, layout, range, mode != 0, translated, store->limit);
    }
    catch (...) {
        /* Eager translation is optional. Already published blocks remain valid;
         all other entry points retain interpretation/lazy translation. */
    }
    return translated;
#else
    return 0;
#endif
}

extern "C" unsigned ROMlib_ppc_translate_published_750(ROMlib_ppc_translation_cache* cache,
                                                       const ROMlib_ppc_published_word* words, size_t count)
{
#if ROMAW_PPC_NATIVE_JIT
    if (!cache || !words || !count)
        return 0;
    auto& store = *static_cast<native_store*>(cache->implementation);
    if (!cache_in_host_service(store))
        return 0;
    unsigned translated = 0;
    try {
        powerpc_cpu layout;
        std::unordered_set<native_key> seen;
        std::vector<ROMlib_ppc_published_word> missing;
        count = std::min(count, size_t(std::min(unsigned(ROMLIB_PPC_PUBLISHED_750_MAX_WORDS), store.limit)));
        // Protect all matching words before replacing any slots at capacity.
        for (size_t i = 0; i < count; ++i) {
            const auto& word = words[i];
            auto key = translation_key(word.address, word.opcode, true);
            if ((word.address & 3) || !supported(word.opcode, &layout, true) || !seen.insert(key).second)
                continue;
            auto found = store.system_blocks.find(key);
            if (found == store.system_blocks.end())
                missing.push_back(word);
            else
                found->second->last_used = ++store.clock;
        }
        for (const auto& word : missing) {
            std::unique_ptr<native_block> block(new native_block);
            block->architectural = true;
            block->words.push_back(word.opcode);
            if (!compile(*block, &layout, word.address))
                break;
            store.publish(store.system_blocks, translation_key(word.address, word.opcode, true), std::move(block));
            ++translated;
        }
    }
    catch (...) {
        // Code publication itself remains valid if eager translation fails.
    }
    return translated;
#else
    return 0;
#endif
}

unsigned romaw_ppc_translate_image(ROMlib_ppc_translation_cache* cache, const romaw_ppc_code_range* code,
                                   size_t range_count, const uint32_t* entries, size_t entry_count, unsigned mode)
{
#if ROMAW_PPC_NATIVE_JIT
    if (!cache || (mode & ~ROMLIB_PPC_TRANSLATE_750))
        return 0;
    auto& store = *static_cast<native_store*>(cache->implementation);
    bool service = cache_in_host_service(store);
    if (!service && (store.busy || (!entry_count && store.size() >= store.limit)))
        return 0;
    unsigned translated = 0;
    try {
        std::vector<code_range_plan> plans;
        plans.reserve(range_count);
        for (size_t i = 0; i < range_count; ++i) {
            const auto& range = code[i];
            if (range.data && range.size >= 4 && !(range.address & 3) &&
                uint64(range.address) + range.size <= UINT64_C(0x100000000))
                plans.push_back({range, {range.address}, false});
        }
        std::sort(plans.begin(), plans.end(),
                  [](const code_range_plan& a, const code_range_plan& b) { return a.range.address < b.range.address; });
        plans.erase(std::unique(plans.begin(), plans.end(),
                                [](const code_range_plan& a, const code_range_plan& b) {
                                    return a.range.address == b.range.address && a.range.size == b.range.size &&
                                           a.range.data == b.range.data;
                                }),
                    plans.end());
        for (size_t i = 1; i < plans.size(); ++i)
            if (uint64(plans[i - 1].range.address) + plans[i - 1].range.size > plans[i].range.address)
                return 0;
        auto find_range = [&](uint32 pc) -> code_range_plan* {
            if (pc & 3)
                return nullptr;
            auto it = std::upper_bound(plans.begin(), plans.end(), pc, [](uint32 address, const code_range_plan& p) {
                return address < p.range.address;
            });
            if (it == plans.begin())
                return nullptr;
            auto& plan = *--it;
            return uint64(pc) + 4 <= uint64(plan.range.address) + plan.range.size ? &plan : nullptr;
        };
        for (size_t i = 0; i < entry_count; ++i)
            if (auto plan = find_range(entries[i]))
                plan->starts.push_back(entries[i]);

        // Branches may cross section boundaries. Collect targets from the
        // linked bytes before compiling any flat code range.
        if (!mode)
            for (const auto& plan : plans)
                for (uint32 at = 0; at <= plan.range.size - 4; at += 4) {
                    uint32 opcode = read_word(plan.range.data + at);
                    const uint32 pc = plan.range.address + at;
                    uint32 target;
                    if ((opcode >> 26) == 18 || (opcode >> 26) == 16)
                        target = direct_branch_target(opcode, pc);
                    else if (!ctr_veneer_target(plan.range, pc, opcode, target))
                        continue;
                    if (auto destination = find_range(target))
                        destination->starts.push_back(target);
                }
        for (auto& plan : plans) {
            std::sort(plan.starts.begin(), plan.starts.end());
            plan.starts.erase(std::unique(plan.starts.begin(), plan.starts.end()), plan.starts.end());
        }
        powerpc_cpu layout;
        auto span = [](const code_range_plan& plan, uint32 start, std::vector<uint32>::const_iterator next) {
            uint64 end = next == plan.starts.end() ? uint64(plan.range.address) + plan.range.size : *next;
            return romaw_ppc_code_range{plan.range.data + (start - plan.range.address), start, uint32(end - start)};
        };
        // Protect every matching priority entry before restoring entries
        // missing from a full cache saved after runtime compilation.
        auto& blocks = mode ? store.system_blocks : store.blocks;
        std::unordered_set<uint32> prioritized;
        std::vector<romaw_ppc_code_range> missing;
        std::vector<romaw_ppc_code_range> reachable;
        for (size_t i = 0; i < entry_count && prioritized.size() < store.limit; ++i) {
            uint32 pc = entries[i];
            auto plan = find_range(pc);
            if (!plan || !supported(read_word(plan->range.data + (pc - plan->range.address)), &layout, mode != 0) ||
                !prioritized.insert(pc).second)
                continue;
            auto range = span(*plan, pc, std::upper_bound(plan->starts.begin(), plan->starts.end(), pc));
            if (mode)
                reachable.push_back({range.data, pc, 4});
            auto found = blocks.find(translation_key(range, mode != 0));
            if (found != blocks.end() && matches_translation(*found->second, range))
                found->second->last_used = ++store.clock;
            else
                missing.push_back(range);
        }
        for (const auto& range : missing) {
            auto block = build_translation(layout, range, mode != 0);
            if (block && block->code) {
                store.publish(blocks, translation_key(range, mode != 0), std::move(block));
                ++translated;
            }
        }
        unused_blocks unused;
        if (service) {
            // A late fragment can replace unexecuted speculative blocks while
            // its caller is suspended. Keep blocks in the newly linked group,
            // including those restored on a warm launch.
            for (auto* map : {&store.blocks, &store.system_blocks})
                for (const auto& entry : *map) {
                    if (entry.second->last_used)
                        continue;
                    uint32 pc = uint32(entry.first);
                    auto plan = map == &blocks ? find_range(pc) : nullptr;
                    bool in_group = plan && (!mode || uint32(entry.first >> 32) ==
                                                          read_word(plan->range.data + (pc - plan->range.address)));
                    if (!in_group)
                        unused.emplace_back(map, entry.first);
                }
            // Within this unused set prefer high addresses, which the eager
            // sweep ordinarily reaches after lower application addresses.
            std::sort(unused.begin(), unused.end(), [](const unused_block& a, const unused_block& b) {
                return uint32(a.second) != uint32(b.second) ? uint32(a.second) < uint32(b.second) : a.second < b.second;
            });
        }
        const auto& omitted = store.omitted[mode != 0];
        size_t omitted_slots = 0;
        // Count only omissions whose live source still fits the current plan.
        // Otherwise a warm, byte-limited snapshot would extend an unchanged
        // scan past the original block limit.
        for (const auto& entry : omitted)
            if (!blocks.count(entry.first))
                if (auto plan = find_range(uint32(entry.first))) {
                    uint32 pc = uint32(entry.first);
                    auto range = span(*plan, pc, std::upper_bound(plan->starts.begin(), plan->starts.end(), pc));
                    omitted_slots += matches_words(entry.second, range);
                }
        auto occupancy = [&]() { return store.size() - unused.size() + omitted_slots; };
        if (!reachable.empty() && occupancy() < store.limit) {
            const size_t roots = reachable.size();
            const size_t visit_limit = size_t(store.limit) * 2;
            std::unordered_set<uint32> loop_words;
            auto enqueue = [&](uint32 pc) {
                if (reachable.size() == visit_limit || prioritized.count(pc))
                    return;
                if (auto plan = find_range(pc)) {
                    auto data = plan->range.data + (pc - plan->range.address);
                    if (supported(read_word(data), &layout, true)) {
                        prioritized.insert(pc);
                        reachable.push_back({data, pc, 4});
                    }
                }
            };
            // Look beyond the publication budget for short loops reached
            // after a long straight-line prefix. The walk uses linked bytes
            // only and does not allocate executable pages.
            for (size_t i = 0; i < reachable.size(); ++i) {
                const auto instruction = reachable[i];
                uint32 pc = instruction.address, opcode = read_word(instruction.data);
                unsigned primary = opcode >> 26, xo = (opcode >> 1) & 1023;
                bool direct = primary == 16 || primary == 18;
                bool indirect = primary == 19 && (xo == 16 || xo == 528);
                if (direct) {
                    uint32 target = direct_branch_target(opcode, pc);
                    enqueue(target);
                    if (primary == 16 && !(opcode & 3) && !branch_always(opcode) && target < pc &&
                        pc - target < max_words * 4) {
                        for (uint32 offset = 0; offset <= pc - target; offset += 4)
                            loop_words.insert(target + offset);
                    }
                }
                else if (primary == 19 && xo == 528) {
                    // Treat a linked CTR veneer as a preparation hint.
                    // The actual branch still uses live CTR and checked fetch.
                    if (auto plan = find_range(pc)) {
                        uint32 target;
                        if (ctr_veneer_target(plan->range, pc, opcode, target))
                            enqueue(target);
                    }
                }
                // A conditional branch can fall through. A linked branch
                // may return to the next word, even when its target is unknown.
                if (opcode != 0x4C000064 && (!(direct || indirect) || !branch_always(opcode) || (opcode & 1)))
                    enqueue(pc + 4);
            }
            // Keep the original breadth-first order within each group.
            for (bool loop : {true, false})
                for (size_t i = roots; i < reachable.size() && occupancy() < store.limit; ++i) {
                    const auto& instruction = reachable[i];
                    if (bool(loop_words.count(instruction.address)) != loop)
                        continue;
                    auto key = translation_key(instruction, true);
                    auto victims = store.size() == store.limit ? &unused : nullptr;
                    if (!translate_block(blocks, key, blocks.find(key), layout, instruction, true, translated, victims,
                                         &omitted))
                        return translated;
                    if (loop) {
                        auto found = blocks.find(key);
                        // Treat selected loops as priority entries when a
                        // host service needs cache slots before they run.
                        if (found != blocks.end() && !found->second->last_used)
                            found->second->last_used = ++store.clock;
                    }
                }
        }
        auto sweep = [&](const code_range_plan& plan, size_t ceiling) {
            for (auto start = plan.starts.begin(); start != plan.starts.end() && occupancy() < ceiling; ++start)
                translate_blocks(store, layout, span(plan, *start, start + 1), mode != 0, translated,
                                 ceiling - omitted_slots, &unused, &omitted);
        };
        // Share the remaining slots among code sections, then return unused
        // shares to any section that still has work.
        for (size_t i = 0; i < plans.size() && occupancy() < store.limit; ++i) {
            size_t used = occupancy();
            size_t ceiling = used + 1 + (store.limit - used - 1) / (plans.size() - i);
            sweep(plans[i], ceiling);
            plans[i].limited = occupancy() == ceiling;
        }
        for (const auto& plan : plans)
            if (plan.limited && occupancy() < store.limit)
                sweep(plan, store.limit);
    }
    catch (...) {
        // An incomplete plan remains usable through the normal dispatcher.
    }
    return translated;
#else
    return 0;
#endif
}

romaw_ppc_jit_scope::romaw_ppc_jit_scope(ROMlib_ppc_environment* environment) noexcept
    : context(nullptr), previous(nullptr)
{
#if ROMAW_PPC_NATIVE_JIT
    previous = active_context;
    active_context = nullptr;
    if ((environment->execution_flags & ROMLIB_PPC_EXEC_JIT) && ROMlib_ppc_jit_available())
        try {
            active_context = new native_context(environment);
            context = active_context;
        }
        catch (...) {
            /* A performance option must not turn allocation failure into a
           failed guest execution. Leave interpretation available. */
        }
#endif
}

romaw_ppc_jit_scope::~romaw_ppc_jit_scope()
{
#if ROMAW_PPC_NATIVE_JIT
    active_context = static_cast<native_context*>(previous);
    delete static_cast<native_context*>(context);
#endif
}

std::shared_ptr<ppc_native_memory_kernel> romaw_ppc_native_memory_code()
{
#if ROMAW_PPC_NATIVE_JIT
    auto* context = active_context;
    if (!context || context->disabled || context->environment != romaw_ppc_environment)
        return {};
    auto& kernel = context->store->memory_kernel;
    try {
        if (!kernel)
            kernel = std::make_shared<ppc_native_memory_kernel>();
    }
    catch (const std::bad_alloc&) {
        return {};
    }
    if (kernel->get(ppc_native_memory_kernel::read))
        return kernel;
#endif
    return {};
}

romaw_ppc_jit_service_scope::romaw_ppc_jit_service_scope() noexcept : in_service(nullptr), previous(false)
{
#if ROMAW_PPC_NATIVE_JIT
    if (active_context) {
        in_service = &active_context->in_service;
        previous = *in_service;
        *in_service = true;
    }
#endif
}

romaw_ppc_jit_service_scope::~romaw_ppc_jit_service_scope()
{
    if (in_service)
        *in_service = previous;
}

void romaw_ppc_jit_switch(void (*switch_host)(void*), void* target)
{
#if ROMAW_PPC_NATIVE_JIT
    native_context* saved = active_context;
    active_context = nullptr;
    try {
        switch_host(target);
    }
    catch (...) {
        active_context = saved;
        throw;
    }
    active_context = saved;
#else
    switch_host(target);
#endif
}

bool romaw_ppc_native_step(powerpc_cpu* cpu, uint32* pc)
{
#if ROMAW_PPC_NATIVE_JIT
    native_context* context = active_context;
    ROMlib_ppc_environment* env = romaw_ppc_environment;
    if (!context || context->disabled || context->environment != env || !env->linear_memory ||
        (context->owner && context->owner != cpu))
        return false;
    context->owner = cpu;
    uint32 relative = *pc - env->linear_address;
    if (relative > env->linear_size || env->linear_size - relative < 4)
        return false;
    const unsigned char* source = env->linear_memory + relative;
    uint32 first_opcode = read_word(source);
    if (!supported(first_opcode, cpu))
        return false;
    unsigned available = (env->linear_size - relative) / 4;
    unsigned budget = env->poll ? 1024 - (env->instructions & 1023) : max_words;
    if (env->instruction_limit) {
        // A poll may lower the limit after the dispatcher counted this word.
        // Finish the current instruction and leave the next check to the dispatcher.
        uint64 remaining =
            env->instruction_limit > env->instructions ? env->instruction_limit - env->instructions + 1 : 1;
        if (remaining < budget)
            budget = remaining;
    }
    native_block* ready = nullptr;
    try {
        auto& store = *context->store;
        auto& blocks = store.blocks;
        auto found = blocks.find(*pc);
        bool changed = false;
        if (found != blocks.end()) {
            const auto& words = found->second->words;
            changed = words.size() > available;
            for (unsigned i = 0; !changed && i < words.size(); ++i)
                changed = words[i] != read_word(source + i * 4);
        }
        if (found == blocks.end() || changed) {
            // Keep the old view until its replacement is executable. A
            // subsequent fetch may restore the original source words.
            bool first_mismatch = changed && !found->second->source_mismatch;
            if (changed)
                found->second->source_mismatch = true;
            if (!store.hot(*pc, first_opcode, false, first_mismatch))
                return false;
            std::unique_ptr<native_block> block(new native_block);
            for (unsigned i = 0; i < available && i < max_words; ++i) {
                uint32 opcode = read_word(source + i * 4);
                if (!supported(opcode, cpu))
                    break;
                block->words.push_back(opcode);
                if (ends_block(opcode))
                    break;
            }
            if (block->words.empty())
                return false;
            if (!compile(*block, cpu, *pc)) {
                context->disabled = true;
                return false;
            }
            ready = store.publish(blocks, *pc, std::move(block));
            ++env->native_compilations;
        }
        else {
            ++env->native_cache_hits;
            ready = found->second.get();
            ready->source_mismatch = false;
            ready->last_used = ++store.clock;
        }
        /* romaw_ppc_step already counted the first instruction and polled
         before compilation/validation. Stop before the next poll or limit. */
        /* Leaf bodies do not call out; semantic tails leave the generated frame.
           Each invocation has private status scratch across nested CPUs. */
    }
    catch (const std::bad_alloc&) {
        context->disabled = true;
    }
    if (ready)
        return run_native(cpu, pc, *ready, budget);
#endif
    return false;
}

bool romaw_ppc_native_system_step(powerpc_cpu* cpu, uint32* pc, uint32 opcode)
{
#if ROMAW_PPC_NATIVE_JIT
    auto* context = active_context;
    auto* env = romaw_ppc_environment;
    if (!context || context->disabled || context->environment != env || !env->system ||
        (context->owner && context->owner != cpu) || !supported(opcode, cpu, true))
        return false;
    context->owner = cpu;
    native_block* ready = nullptr;
    try {
        auto& store = *context->store;
        auto& blocks = store.system_blocks;
        auto key = translation_key(*pc, opcode, true);
        auto found = blocks.find(key);
        if (found == blocks.end()) {
            if (!store.hot(*pc, opcode, true, false))
                return false;
            std::unique_ptr<native_block> block(new native_block);
            block->architectural = true;
            block->words.push_back(opcode);
            if (!compile(*block, cpu, *pc)) {
                context->disabled = true;
                return false;
            }
            ready = store.publish(blocks, key, std::move(block));
            ++env->native_compilations;
        }
        else {
            ++env->native_cache_hits;
            ready = found->second.get();
            ready->last_used = ++store.clock;
        }
    }
    catch (const std::bad_alloc&) {
        context->disabled = true;
    }
    // Kernel callbacks and architectural faults must never be retried as an
    // allocation/compilation fallback after their guest effects have occurred.
    if (ready)
        return run_native(cpu, pc, *ready, 1);
#endif
    return false;
}
