/*+===================================================================
  File:      images.cpp

  Summary:   Open source-backed image catalogs and stream bounded fork reads and explicit copies.

  Classes:   Declared: Extract, Input, Item, MemoryOutput, Output, Pending, RangeOutput, Roman, Volume,
             romaw_image_set; Used: Extract, Input, Item, MemoryOutput, NdifInput, Output, Pending, RangeOutput,
             Roman, Volume, romaw_image_item, romaw_image_set, std::atomic, std::error_code, std::exception,
             std::function, std::ifstream, std::lock_guard, std::map, std::mutex, std::ofstream, std::runtime_error,
             std::set, std::shared_ptr, std::string, std::unique_ptr, std::vector, std::wstring

  Functions: romaw_images_read_fork_utf8, RegisterArc, romaw_images_close, romaw_images_count, romaw_images_export_utf8, romaw_images_export_progress_utf8,
             romaw_images_fork_utf8, romaw_images_item_size_utf8, romaw_images_kind, romaw_images_list_utf8,
             romaw_images_name, romaw_images_ndif_writable, romaw_images_open_direct,
             romaw_images_open_direct_utf8, romaw_images_open_readonly_utf8, romaw_images_path,
             romaw_images_prepare, romaw_images_prepare_utf8, romaw_images_read_item_utf8,
             romaw_images_stat_utf8, romaw_images_warning

  Origin:    Existing repository source in packages/disk-images/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "romaw_images.h"
#include "Common/MyInitGuid.h"
#include "Common/MyCom.h"
#include "7zip/ICoder.h"
#include "7zip/Common/RegisterArc.h"
#include "Windows/PropVariant.h"
#include "7zCrc.h"
#include "XzCrc64.h"
#include "Sha256.h"
#include <mutex>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <functional>
#include <array>
#include <atomic>
#include <codecvt>
#include <locale>
#include <algorithm>
namespace fs = std::filesystem;
#ifdef _WIN32
static thread_local unsigned image_code_page = CP_ACP;
#endif
static std::vector<const CArcInfo*>& formats()
{
    static std::vector<const CArcInfo*> value;
    return value;
}

void RegisterArc(const CArcInfo* info) throw()
{
    formats().push_back(info);
}

extern "C" void romaw_link_hfs();
extern "C" void romaw_link_dmg();
extern "C" void romaw_link_apm();
extern "C" void romaw_link_iso();
static thread_local void (*pump_events)(void);

static void check(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

static fs::path io_path(const fs::path& path);

static fs::path native_path(const char* text)
{
#ifdef _WIN32
    int n = MultiByteToWideChar(image_code_page, 0, text, -1, nullptr, 0);
    check(n > 0, "Invalid image path");
    std::wstring s(n, 0);
    MultiByteToWideChar(image_code_page, 0, text, -1, s.data(), n);
    s.pop_back();
    return io_path(fs::path(s));
#else
    return fs::u8path(text);
#endif
}

static std::string native_text(const fs::path& p)
{
#ifdef _WIN32
    auto w = p.wstring();
    if (w.compare(0, 8, L"\\\\?\\UNC\\") == 0)
        w = L"\\\\" + w.substr(8);
    else if (w.compare(0, 4, L"\\\\?\\") == 0)
        w.erase(0, 4);
    int n = WideCharToMultiByte(image_code_page, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    check(n > 0, "Invalid cache path");
    std::string s(n, 0);
    WideCharToMultiByte(image_code_page, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    s.pop_back();
    return s;
#else
    return p.u8string();
#endif
}

static fs::path io_path(const fs::path& path)
{
#ifdef _WIN32
    if (path.native().compare(0, 4, L"\\\\?\\") == 0)
        return path;
    auto normalized = fs::absolute(path).lexically_normal();
    normalized.make_preferred();
    auto value = normalized.wstring();
    if (value.compare(0, 4, L"\\\\?\\") != 0)
        value = value.compare(0, 2, L"\\\\") == 0 ? L"\\\\?\\UNC\\" + value.substr(2) : L"\\\\?\\" + value;
    return value;
#else
    return path;
#endif
}

static fs::path catalog_path(fs::path path)
{
#ifdef _WIN32
    // MinGW lexically_normal collapses the extended-path prefix, after which
    // lexically_relative cannot identify descendants. Keep catalog keys in the
    // normal namespace; io_path adds the prefix only for actual file operations.
    auto value = path.wstring();
    if (value.compare(0, 8, L"\\\\?\\UNC\\") == 0)
        value = L"\\\\" + value.substr(8);
    else if (value.compare(0, 4, L"\\\\?\\") == 0)
        value.erase(0, 4);
    path = value;
#endif
    return fs::absolute(path).lexically_normal();
}

namespace {
class Input final : public IInStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IInStream)
  public:
    std::ifstream file;

    explicit Input(const fs::path& p) : file(io_path(p), std::ios::binary)
    {
        check(bool(file), ("Cannot open image: " + io_path(p).u8string()).c_str());
    }

    STDMETHOD(Read)(void* p, UInt32 n, UInt32* done) noexcept override
    {
        if (pump_events)
            pump_events();
        file.read((char*)p, n);
        if (done)
            *done = (UInt32)file.gcount();
        return file.bad() ? E_FAIL : S_OK;
    }

    STDMETHOD(Seek)(Int64 off, UInt32 origin, UInt64* pos) noexcept override
    {
        if (origin > 2)
            return E_INVALIDARG;
        file.clear();
        file.seekg(off, origin == 0 ? std::ios::beg : origin == 1 ? std::ios::cur : std::ios::end);
        if (!file)
            return E_FAIL;
        if (pos)
            *pos = (UInt64)file.tellg();
        return S_OK;
    }
};

#include "ndif.h"

class Output final : public ISequentialOutStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(ISequentialOutStream)
  public:
    std::ofstream file;
    UInt64 written = 0, limit;

    Output(const fs::path& p, UInt64 size, bool append)
        : file(io_path(p), std::ios::binary | (append ? std::ios::app : std::ios::trunc)), limit(size)
    {
        check(bool(file), "Cannot create image cache file");
    }

    STDMETHOD(Write)(const void* p, UInt32 n, UInt32* done) noexcept override
    {
        if (done)
            *done = 0;
        if (n > limit - written)
            return E_FAIL;
        if (pump_events)
            pump_events();
        file.write((const char*)p, n);
        if (!file)
            return E_FAIL;
        written += n;
        if (done)
            *done = n;
        return S_OK;
    }
};

class Extract final : public IArchiveExtractCallback, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IArchiveExtractCallback)
  public:
    CMyComPtr<ISequentialOutStream> output;
    Int32 result = -1;

    STDMETHOD(SetTotal)(UInt64) noexcept override
    {
        return S_OK;
    }

    STDMETHOD(SetCompleted)(const UInt64*) noexcept override
    {
        if (pump_events)
            pump_events();
        return S_OK;
    }
    STDMETHOD(GetStream)

    (UInt32, ISequentialOutStream** p, Int32) noexcept override
    {
        *p = output;
        if (*p)
            (*p)->AddRef();
        return S_OK;
    }

    STDMETHOD(PrepareOperation)(Int32) noexcept override
    {
        return S_OK;
    }

    STDMETHOD(SetOperationResult)(Int32 value) noexcept override
    {
        result = value;
        return S_OK;
    }
};
}

static void extract(IInArchive* a, UInt32 i, const fs::path& p, UInt64 size, bool append = false)
{
    Output* out = new Output(p, size, append);
    CMyComPtr<ISequentialOutStream> keep_out = out;
    Extract* cb = new Extract;
    CMyComPtr<IArchiveExtractCallback> keep_cb = cb;
    cb->output = keep_out;
    HRESULT hr = a->Extract(&i, 1, 0, cb);
    out->file.flush();
    check(hr == S_OK && cb->result == 0 && out->written == size && bool(out->file),
          "Damaged image, unsupported compression, or incomplete fork");
}

static NWindows::NCOM::CPropVariant property(IInArchive* a, UInt32 i, PROPID id)
{
    NWindows::NCOM::CPropVariant p;
    check(a->GetProperty(i, id, &p) == S_OK, "Cannot read image catalog");
    return p;
}

static std::wstring text_property(IInArchive* a, UInt32 i, PROPID id)
{
    auto p = property(a, i, id);
    return p.vt == VT_BSTR ? std::wstring(p.bstrVal, SysStringLen(p.bstrVal)) : L"";
}

static UInt64 size_property(IInArchive* a, UInt32 i)
{
    auto p = property(a, i, kpidSize);
    check(p.vt == VT_UI8 || p.vt == VT_UI4, "Missing item size");
    return p.vt == VT_UI8 ? p.uhVal.QuadPart : p.ulVal;
}

struct Roman {
    unsigned value, base, accent;
};

static const Roman roman[128] = {
#include "macroman.inc"
};

/* ROMAW's %XX host filename spelling preserves Macintosh bytes. HFS+
   decomposed Roman names are composed; other Unicode stays escaped. */
static std::string component(const std::wstring& name)
{
    check(!name.empty() && name != L"." && name != L"..", "Invalid image filename");
    std::string result;
    for (size_t i = 0; i < name.size(); ++i) {
        unsigned c = (unsigned)name[i];
        check(c != 0, "NUL in image filename");
        if (c >= 128 || (i + 1 < name.size() && name[i + 1] >= 0x300))
            for (unsigned n = 0; n < 128; ++n)
                if (roman[n].value == c || (roman[n].base == c && roman[n].base && i + 1 < name.size() &&
                                            roman[n].accent == (unsigned)name[i + 1])) {
                    if (roman[n].value != c)
                        ++i;
                    c = n + 128;
                    break;
                }
        char b[16];
        if (c > 255) {
            std::snprintf(b, sizeof b, "%%u%04X", c);
            result += b;
        }
        else if (c < 32 || c >= 127 || std::wstring(L"%/\\:*?\"<>|").find(c) != std::wstring::npos ||
                 (i + 1 == name.size() && (c == '.' || c == ' ')) || (i == 1 && name[0] == '.' && c == '_')) {
            std::snprintf(b, sizeof b, "%%%02X", c);
            result += b;
        }
        else
            result += (char)c;
    }
    auto folded = result;
    for (char& c : folded)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    if (folded == ".parent")
        result.replace(0, 1, "%2E");
    auto stem = result.substr(0, result.find('.'));
    for (char& c : stem)
        if (c >= 'a' && c <= 'z')
            c -= 32;
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
        (stem.size() == 4 && (stem.substr(0, 3) == "COM" || stem.substr(0, 3) == "LPT") && stem[3] >= '0' &&
         stem[3] <= '9')) {
        char b[4];
        std::snprintf(b, sizeof b, "%%%02X", (unsigned char)result[0]);
        result.replace(0, 1, b);
    }
    check(result.size() <= 240, "HFS+ filename exceeds host filename limit");
    return result;
}

struct Volume {
    int kind;
    std::string path, name;
    bool classic = false, writable_container = false;
    UInt64 offset = 0, length = 0;
};
struct DirectDevice;

struct romaw_image_set {
    fs::path root;
    std::vector<Volume> volumes;
    UInt64 bytes = 0;
    bool readonly = false;
    bool direct = false;
    fs::path source;
    std::string warning;
    std::map<unsigned, std::shared_ptr<DirectDevice>> devices;
    std::map<fs::path, fs::path> guest_paths;

    struct Item {
        romaw_image_item info = {};
        std::function<std::vector<unsigned char>()> data, resource;
        std::function<void(uint64_t, void*, size_t)> data_range, resource_range;
    };

    std::map<fs::path, Item> items;
    // Container streams can borrow their archive's partition/block tables.
    // Keep those tables alive until all deferred fork readers are destroyed.
    std::vector<CMyComPtr<IInArchive>> containers;

    struct Pending {
        bool resource;
        std::function<void()> read;
    };

    std::map<fs::path, Pending> pending;
    std::map<fs::path, UInt64> sizes;
    std::mutex mutex;
};

static bool image_exists(romaw_image_set& set, const fs::path& path)
{
    return set.direct ? set.items.count(catalog_path(path)) != 0 : fs::exists(path);
}

static bool image_directory(romaw_image_set& set, const fs::path& path)
{
    if (!set.direct) {
        fs::create_directories(path);
        return true;
    }
    set.items[catalog_path(path)].info.directory = 1;
    return true;
}

static void image_fork(romaw_image_set& set, const fs::path& path, UInt64 size, bool resource,
                       std::function<void()> read, std::function<std::vector<unsigned char>()> direct_read = {},
                       std::function<void(uint64_t, void*, size_t)> range = {})
{
    if (set.direct) {
        auto key = resource ? path.parent_path() / fs::u8path(path.filename().u8string().substr(2)) : path;
        auto& item = set.items[catalog_path(key)];
        if (resource) {
            item.info.resource_size = size;
            item.resource = std::move(direct_read);
            item.resource_range = std::move(range);
        }
        else {
            item.info.data_size = size;
            item.data = std::move(direct_read);
            item.data_range = std::move(range);
        }
        return;
    }
    if (!set.readonly) {
        read();
        return;
    }
    if (!resource) {
        set.sizes.emplace(catalog_path(path), size);
        std::ofstream placeholder(io_path(path), std::ios::binary);
        check(bool(placeholder), "Cannot create image catalog entry");
    }
    check(set.pending.emplace(catalog_path(path), romaw_image_set::Pending{resource, std::move(read)}).second,
          "Duplicate image fork");
}

namespace {
class MemoryOutput final : public ISequentialOutStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(ISequentialOutStream)
  public:
    std::vector<unsigned char> bytes;
    size_t limit;

    explicit MemoryOutput(size_t size) : limit(size)
    {
    }
    STDMETHOD(Write)

    (const void* data, UInt32 size, UInt32* written) noexcept override
    {
        if (written)
            *written = 0;
        if (size > limit - bytes.size())
            return E_FAIL;
        try {
            auto p = static_cast<const unsigned char*>(data);
            bytes.insert(bytes.end(), p, p + size);
            if (written)
                *written = size;
            return S_OK;
        }
        catch (...) {
            return E_OUTOFMEMORY;
        }
    }
};

class RangeOutput final : public ISequentialOutStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(ISequentialOutStream)
  public:
    uint64_t position = 0, offset;
    unsigned char* buffer;
    size_t size, copied = 0;

    RangeOutput(uint64_t offset, void* buffer, size_t size)
        : offset(offset), buffer(static_cast<unsigned char*>(buffer)), size(size)
    {
    }
    STDMETHOD(Write)

    (const void* data, UInt32 count, UInt32* written) noexcept override
    {
        if (position <= offset + size && count > (offset > position ? offset - position : 0)) {
            size_t skip = offset > position ? (size_t)(offset - position) : 0;
            size_t take = std::min<size_t>(count - skip, size - copied);
            memcpy(buffer + copied, static_cast<const unsigned char*>(data) + skip, take);
            copied += take;
        }
        position += count;
        if (written)
            *written = count;
        return S_OK;
    }
};
}

static void archive_fork(romaw_image_set& set, IInArchive* archive, UInt32 index, const fs::path& path, UInt64 size,
                         bool resource = false)
{
    CMyComPtr<IInArchive> retained = archive;
    if (set.direct) {
        image_fork(
            set, path, size, resource, {},
            [retained, index, size] {
                check(size <= 128 * 1024 * 1024, "Image fork exceeds memory read limit");
                auto* out = new MemoryOutput((size_t)size);
                CMyComPtr<ISequentialOutStream> output = out;
                auto* cb = new Extract;
                CMyComPtr<IArchiveExtractCallback> keep = cb;
                cb->output = output;
                check(retained->Extract(&index, 1, 0, cb) == S_OK && cb->result == 0 && out->bytes.size() == size,
                      "Cannot read image fork");
                return std::move(out->bytes);
            },
            [retained, index, size](uint64_t at, void* buffer, size_t count) {
                check(at <= size && count <= size - at, "Image fork read outside bounds");
                CMyComPtr<IInArchiveGetStream> streams;
                CMyComPtr<ISequentialInStream> sequential;
                CMyComPtr<IInStream> input;
                if (retained->QueryInterface(IID_IInArchiveGetStream, (void**)&streams) == S_OK &&
                    streams->GetStream(index, &sequential) == S_OK && sequential &&
                    sequential->QueryInterface(IID_IInStream, (void**)&input) == S_OK) {
                    check(input->Seek(at, 0, nullptr) == S_OK, "Cannot seek image fork");
                    auto* out = static_cast<unsigned char*>(buffer);
                    while (count) {
                        UInt32 done = 0;
                        check(input->Read(out, (UInt32)std::min<size_t>(count, 65536), &done) == S_OK && done,
                              "Truncated image fork");
                        count -= done;
                        out += done;
                    }
                    return;
                }
                auto* out = new RangeOutput(at, buffer, count);
                CMyComPtr<ISequentialOutStream> output = out;
                auto* cb = new Extract;
                CMyComPtr<IArchiveExtractCallback> keep = cb;
                cb->output = output;
                check(retained->Extract(&index, 1, 0, cb) == S_OK && cb->result == 0 && out->copied == count,
                      "Cannot read image fork range");
            });
        return;
    }
    UInt64 prefix = 0;
    if (resource)
        prefix = fs::file_size(io_path(path));
    image_fork(set, path, size, resource, [retained, index, path, size, resource, prefix] {
        // Retrying a failed resource read must replace the partial
        // payload, not append a second resource fork behind it.
        if (resource)
            fs::resize_file(io_path(path), prefix);
        extract(retained, index, path, size, resource);
    });
}

static void budget(romaw_image_set& s, UInt64 n)
{
    const UInt64 limit = (UInt64)16 << 30;
    check(n <= limit - s.bytes, "Image fork totals exceed the 16 GiB limit");
    s.bytes += n;
}

static CMyComPtr<IInArchive> open_archive(IInStream* s, unsigned id)
{
    for (auto f : formats())
        if (f->Id == id) {
            CMyComPtr<IInArchive> a = f->CreateInArchive();
            check(s->Seek(0, 0, nullptr) == S_OK, "Cannot seek image");
            if (a->Open(s, nullptr, nullptr) == S_OK)
                return a;
        }
    return nullptr;
}

static void put32(std::ofstream& f, UInt32 n)
{
    char b[] = {(char)(n >> 24), (char)(n >> 16), (char)(n >> 8), (char)n};
    f.write(b, 4);
}

static void sidecar(romaw_image_set& set, const fs::path& path, const std::wstring& hex, UInt32 size)
{
    check(hex.size() == 64, "Missing Finder metadata");
    if (set.direct) {
        auto name = path.filename().u8string();
        auto key = name == "._.Parent" ? path.parent_path() : path.parent_path() / fs::u8path(name.substr(2));
        auto& item = set.items[catalog_path(key)];
        for (unsigned i = 0; i < 32; ++i)
            item.info.finder_info[i] = (unsigned char)std::stoul(hex.substr(i * 2, 2), nullptr, 16);
        item.info.resource_size = size;
        return;
    }
    std::ofstream f(io_path(path), std::ios::binary);
    check(bool(f), ("Cannot create resource sidecar: " + path.u8string()).c_str());
    put32(f, 0x51607);
    put32(f, 0x20000);
    char zeros[16] = {};
    f.write(zeros, 16);
    f.put(0);
    f.put(2);
    put32(f, 9);
    put32(f, 50);
    put32(f, 32);
    put32(f, 2);
    put32(f, 82);
    put32(f, size);
    auto digit = [](wchar_t c) { return c >= L'0' && c <= L'9' ? c - L'0' : c - L'A' + 10; };
    for (unsigned i = 0; i < 32; ++i)
        f.put((char)(digit(hex[i * 2]) * 16 + digit(hex[i * 2 + 1])));
    f.flush();
    check(bool(f), "Cannot write resource sidecar");
}

static void hfs_snapshot(romaw_image_set& set, IInArchive* a)
{
    check(set.volumes.size() < 32, "Too many image partitions");
    UInt32 count = 0;
    check(a->GetNumberOfItems(&count) == S_OK && count <= 1000000, "HFS+ catalog exceeds entry limit");
    CMyComPtr<IArchiveGetRawProps> parents;
    check(a->QueryInterface(IID_IArchiveGetRawProps, (void**)&parents) == S_OK, "Missing HFS+ hierarchy");
    auto root = set.root / ("volume-" + std::to_string(set.volumes.size()));
    image_directory(set, root);
    std::map<UInt32, fs::path> paths;
    std::map<UInt32, unsigned> levels;
    bool have_root = false;
    std::set<fs::path> used;
    std::vector<bool> done(count, false);
    unsigned completed = 0;
    for (unsigned pass = 0; completed < count && pass < 256; ++pass) {
        unsigned before = completed;
        for (UInt32 i = 0; i < count; ++i) {
            if (done[i])
                continue;
            UInt32 parent, type;
            check(parents->GetParent(i, &parent, &type) == S_OK, "Invalid catalog parent");
            check(parent == (UInt32)-1 || parent < count, "Invalid catalog parent index");
            if (parent != (UInt32)-1 && !done[parent])
                continue;
            auto alt = property(a, i, kpidIsAltStream);
            if (alt.vt == VT_BOOL && alt.boolVal) {
                done[i] = true;
                ++completed;
                continue;
            }
            unsigned level = parent == (UInt32)-1 ? 0 : levels.at(parent) + 1;
            check(level <= 256, "HFS+ hierarchy exceeds depth limit");
            levels[i] = level;
            bool dir = property(a, i, kpidIsDir).boolVal != 0;
            if (parent == (UInt32)-1 && dir) {
                check(!have_root, "Multiple HFS+ catalog roots");
                have_root = true;
                paths[i] = root;
            }
            else {
                check(parent == (UInt32)-1 || paths.count(parent), "Invalid catalog hierarchy");
                auto dest = (parent == (UInt32)-1 ? root : paths.at(parent)) / component(text_property(a, i, kpidName));
                check(used.insert(dest).second && !image_exists(set, dest), "Colliding HFS+ filenames");
                paths[i] = dest;
                if (dir)
                    image_directory(set, dest);
                else {
                    auto n = size_property(a, i);
                    budget(set, n);
                    archive_fork(set, a, i, dest, n);
                }
            }
            auto p = paths.at(i);
            auto meta = p == root ? root / "._.Parent" : p.parent_path() / ("._" + p.filename().string());
            sidecar(set, meta, text_property(a, i, 0x10000), 0);
            done[i] = true;
            ++completed;
        }
        check(completed > before, "Cyclic or invalid HFS+ catalog hierarchy");
    }
    check(completed == count, "HFS+ hierarchy exceeds depth limit");
    for (UInt32 i = 0; i < count; ++i) {
        auto alt = property(a, i, kpidIsAltStream);
        if (alt.vt != VT_BOOL || !alt.boolVal || text_property(a, i, kpidName) != L"rsrc")
            continue;
        UInt32 parent, type;
        check(parents->GetParent(i, &parent, &type) == S_OK && paths.count(parent), "Invalid resource fork parent");
        auto n = size_property(a, i);
        check(n <= 0xffffffff - 82, "Resource fork exceeds AppleDouble size limit");
        budget(set, n);
        auto p = paths.at(parent);
        auto meta = p.parent_path() / ("._" + p.filename().string());
        sidecar(set, meta, text_property(a, parent, 0x10000), (UInt32)n);
        archive_fork(set, a, i, meta, n, true);
    }
    std::string name = "HFS+ Image";
    for (auto& p : paths)
        if (p.second == root) {
            name = component(text_property(a, p.first, kpidName));
            break;
        }
    set.volumes.push_back({2, native_text(root), name});
}

#include "classic_hfs.h"
#include "mfs.h"

static bool iso_snapshot(romaw_image_set& set, IInStream* stream)
{
    auto archive = open_archive(stream, 0xe7);
    if (!archive)
        return false;
    UInt32 count = 0;
    check(archive->GetNumberOfItems(&count) == S_OK && count <= 100000, "Too many ISO directory entries");
    check(set.volumes.size() < 32, "Too many image partitions");
    auto root = set.root / ("volume-" + std::to_string(set.volumes.size()));
    image_directory(set, root);
    std::set<fs::path> used;
    for (UInt32 i = 0; i < count; ++i) {
        auto name = text_property(archive, i, kpidPath);
        check(!name.empty() && name[0] != '/' && name[0] != '\\', "Invalid ISO path");
        fs::path target = root;
        unsigned levels = 0;
        for (size_t start = 0; start < name.size();) {
            size_t end = name.find_first_of(L"/\\", start);
            if (end == std::wstring::npos)
                end = name.size();
            check(++levels <= 256 && end > start, "Invalid ISO hierarchy");
            target /= fs::u8path(component(name.substr(start, end - start)));
            start = end + 1;
        }
        check(used.insert(target).second, "Colliding ISO filenames");
        auto dir = property(archive, i, kpidIsDir);
        if (dir.vt == VT_BOOL && dir.boolVal) {
            check(set.direct ? (!image_exists(set, target) || set.items.at(catalog_path(target)).info.directory)
                             : !fs::is_regular_file(target),
                  "Colliding ISO filename");
            image_directory(set, target);
        }
        else {
            check(!image_exists(set, target), "Colliding ISO filename");
            image_directory(set, target.parent_path());
            auto size = size_property(archive, i);
            budget(set, size);
            archive_fork(set, archive, i, target, size);
        }
    }
    std::string label = "ISO 9660";
    NWindows::NCOM::CPropVariant comment;
    if (archive->GetArchiveProperty(kpidComment, &comment) == S_OK && comment.vt == VT_BSTR) {
        std::wstring text(comment.bstrVal, SysStringLen(comment.bstrVal));
        auto start = text.find(L"Volume: ");
        if (start != std::wstring::npos) {
            start += 8;
            auto end = text.find_first_of(L"\r\n", start);
            auto name = text.substr(start, end - start);
            if (!name.empty())
                label = component(name);
        }
    }
    set.volumes.push_back({2, native_text(root), label});
    return true;
}

static void warn_missing_bytes(romaw_image_set& set, uint64_t declared, uint64_t available)
{
    if (declared <= available)
        return;
    set.warning = "This image is incomplete: the filesystem/container declares " + std::to_string(declared) +
                  " bytes, but only " + std::to_string(available) +
                  " bytes are present. Some file contents may be missing. Missing bytes cannot be repaired "
                  "from this file alone. To fix it, restore a complete copy or obtain the missing data "
                  "from the original source.";
}

static bool prepare_stream(romaw_image_set& set, IInStream* stream, unsigned depth, UInt64 physical = 0,
                           bool writable_container = true)
{
    check(depth < 4, "Nested image containers exceed depth limit");
    if (mfs_catalog(set, stream))
        return true;
    if (classic_snapshot(set, stream)) {
        auto& volume = set.volumes.back();
        volume.offset += physical;
        volume.writable_container = writable_container;
        return true;
    }
    if (auto a = open_archive(stream, 0xe3)) {
        UInt64 available = 0;
        NWindows::NCOM::CPropVariant declared;
        check(stream->Seek(0, 2, &available) == S_OK, "Cannot size HFS+ source");
        if (a->GetArchiveProperty(kpidPhySize, &declared) == S_OK && declared.vt == VT_UI8)
            warn_missing_bytes(set, declared.uhVal.QuadPart, available);
        hfs_snapshot(set, a);
        return true;
    }
    for (unsigned id : {0xe4u, 0xd4u})
        if (auto a = open_archive(stream, id)) {
            if (set.readonly)
                set.containers.push_back(a);
            CMyComPtr<IInArchiveGetStream> streams;
            check(a->QueryInterface(IID_IInArchiveGetStream, (void**)&streams) == S_OK, "Cannot open image partitions");
            UInt32 count = 0;
            check(a->GetNumberOfItems(&count) == S_OK && count <= 4096, "Too many container partitions");
            bool found = false;
            bool iso = false;
            UInt64 container_size = 0;
            check(stream->Seek(0, 2, &container_size) == S_OK, "Cannot size image container");
            for (UInt32 i = 0; i < count; ++i) {
                // Hybrid CD images commonly have an ISO partition beginning at
                // block zero. Reopening it as APM just rediscovers this same map.
                if (id == 0xd4u) {
                    auto offset = property(a, i, kpidOffset);
                    if ((offset.vt == VT_UI8 && !offset.uhVal.QuadPart) || (offset.vt == VT_UI4 && !offset.ulVal)) {
                        if (!iso) {
                            iso = iso_snapshot(set, stream);
                            found |= iso;
                        }
                        continue;
                    }
                }
                CMyComPtr<ISequentialInStream> part;
                if (streams->GetStream(i, &part) != S_OK || !part)
                    continue;
                CMyComPtr<IInStream> input;
                if (part->QueryInterface(IID_IInStream, (void**)&input) != S_OK)
                    continue;
                auto offset = property(a, i, kpidOffset);
                UInt64 part_offset = offset.vt == VT_UI8   ? offset.uhVal.QuadPart
                                     : offset.vt == VT_UI4 ? offset.ulVal
                                                           : 0;
                if (id == 0xd4u) {
                    uint64_t part_size = size_property(a, i);
                    check(part_size <= UINT64_MAX - part_offset, "Image partition size overflow");
                    warn_missing_bytes(set, part_offset + part_size, container_size);
                }
                if (prepare_stream(set, input, depth + 1, physical + part_offset, writable_container && id == 0xd4u)) {
                    found = true;
                    continue;
                }
                /* Direct mounts never fall back to an extracted partition. */
                if (set.direct)
                    continue;
                unsigned char h[2];
                UInt32 got = 0;
                if (input->Seek(1024, 0, nullptr) != S_OK || input->Read(h, 2, &got) != S_OK || got != 2 ||
                    h[0] != 'B' || h[1] != 'D')
                    continue;
                check(set.volumes.size() < 32, "Too many image partitions");
                auto p = set.root / ("partition-" + std::to_string(set.volumes.size()) + ".hfv");
                auto n = size_property(a, i);
                budget(set, n);
                extract(a, i, p, n);
                set.volumes.push_back({1, native_text(p), "HFS Image"});
                found = true;
            }
            return found;
        }
    return iso_snapshot(set, stream);
}

static romaw_image_set* prepare_images(const char* source, const char* cache, void (*pump)(void), char* error,
                                       size_t capacity, bool readonly = false, bool direct = false)
{
    std::unique_ptr<romaw_image_set> set;
    try {
        set = std::make_unique<romaw_image_set>();
        set->readonly = readonly;
        set->direct = direct;
        check(source && *source && (direct || (cache && *cache)), "Missing image/cache path");
        set->source = native_path(source);
        romaw_link_hfs();
        romaw_link_dmg();
        romaw_link_apm();
        romaw_link_iso();
        static std::once_flag codecs;
        std::call_once(codecs, [] {
            CrcGenerateTable();
            Crc64GenerateTable();
            Sha256Prepare();
        });
        if (direct) {
            static std::atomic<unsigned long long> sequence{0};
            do {
                set->root =
                    catalog_path(native_path(source)).parent_path() / (".romaw-image-" + std::to_string(++sequence));
            } while (fs::exists(set->root));
        }
        else {
            fs::create_directories(native_path(cache));
            auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            for (unsigned i = 0; i < 100; ++i) {
                auto root = native_path(cache) / ("image-" + std::to_string(stamp) + "-" + std::to_string(i));
                if (fs::create_directory(root)) {
                    set->root = root;
                    break;
                }
            }
            check(!set->root.empty(), "Cannot create private image cache");
        }
        check(fs::is_regular_file(native_path(source)), "Choose a regular disk image file");
        pump_events = pump;
        CMyComPtr<IInStream> input = new Input(native_path(source));
        auto ndif = ndif_resource(native_path(source));
        bool writable = true;
        if (!ndif.empty()) {
            auto* mapped = new NdifInput(input, ndif);
            input = mapped;
            writable = mapped->writable_identity;
        }
        check(
            prepare_stream(*set, input, 0, 0, writable) && !set->volumes.empty(),
            "Unsupported or damaged image. Expected classic HFS, HFS+, HFSX or an unencrypted UDIF DMG containing HFS/HFS+.");
        pump_events = nullptr;
        return set.release();
    }
    catch (const std::exception& e) {
        if (error && capacity)
            std::snprintf(error, capacity, "%s", e.what());
    }
    catch (...) {
        if (error && capacity)
            std::snprintf(error, capacity, "Image decoder failed");
    }
    pump_events = nullptr;
    if (set && !set->direct && !set->root.empty()) {
        std::error_code ignored;
        fs::remove_all(set->root, ignored);
    }
    return nullptr;
}

extern "C" romaw_image_set* romaw_images_prepare(const char* source, const char* cache, void (*pump)(void), char* error,
                                                 size_t capacity)
{
#ifdef _WIN32
    unsigned previous = image_code_page;
    image_code_page = CP_ACP;
#endif
    auto set = prepare_images(source, cache, pump, error, capacity);
#ifdef _WIN32
    image_code_page = previous;
#endif
    return set;
}

extern "C" romaw_image_set* romaw_images_prepare_utf8(const char* source, const char* cache, void (*pump)(void),
                                                      char* error, size_t capacity)
{
#ifdef _WIN32
    unsigned previous = image_code_page;
    image_code_page = CP_UTF8;
#endif
    auto set = prepare_images(source, cache, pump, error, capacity);
#ifdef _WIN32
    image_code_page = previous;
#endif
    return set;
}

extern "C" romaw_image_set* romaw_images_open_readonly_utf8(const char* source, const char* cache, char* error,
                                                            size_t capacity)
{
#ifdef _WIN32
    unsigned previous = image_code_page;
    image_code_page = CP_UTF8;
#endif
    auto set = prepare_images(source, cache, nullptr, error, capacity, true);
#ifdef _WIN32
    image_code_page = previous;
#endif
    return set;
}

extern "C" int romaw_images_read_item_utf8(romaw_image_set* set, const char* path, int resources_only, char* error,
                                           size_t capacity)
{
    try {
        check(set && path && *path, "Missing image item");
        std::lock_guard<std::mutex> lock(set->mutex);
        auto requested = catalog_path(fs::u8path(path));
        auto sidecar_path = requested.parent_path() / fs::u8path("._" + requested.filename().u8string());
        auto read_fork = [&](const fs::path& fork, bool resource_only) {
            auto item = set->pending.find(fork);
            if (item != set->pending.end() && (!resource_only || item->second.resource)) {
                item->second.read();
                set->pending.erase(item);
            }
        };
        // Icon reads must not scan the entire catalog for every visible item.
        read_fork(requested, resources_only);
        read_fork(sidecar_path, false);
        if (!resources_only)
            for (auto item = set->pending.lower_bound(requested); item != set->pending.end();) {
                auto relative = item->first.lexically_relative(requested);
                if (relative.empty() || *relative.begin() == "..")
                    break;
                item->second.read();
                item = set->pending.erase(item);
            }
        return 1;
    }
    catch (const std::exception& e) {
        if (error && capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
    catch (...) {
        if (error && capacity)
            std::snprintf(error, capacity, "Cannot read image item");
        return 0;
    }
}

extern "C" int romaw_images_item_size_utf8(romaw_image_set* set, const char* path, uint64_t* size)
{
    if (!set || !path || !size)
        return 0;
    try {
        std::lock_guard<std::mutex> lock(set->mutex);
        auto requested = catalog_path(fs::u8path(path));
        auto found = set->sizes.find(requested);
        if (found == set->sizes.end())
            return 0;
        *size = found->second;
        return 1;
    }
    catch (...) {
        return 0;
    }
}

extern "C" unsigned romaw_images_count(const romaw_image_set* s)
{
    return s ? (unsigned)s->volumes.size() : 0;
}

extern "C" int romaw_images_kind(const romaw_image_set* s, unsigned i)
{
    return s && i < s->volumes.size() ? s->volumes[i].kind : 0;
}

extern "C" const char* romaw_images_path(const romaw_image_set* s, unsigned i)
{
    return s && i < s->volumes.size() ? s->volumes[i].path.c_str() : nullptr;
}

extern "C" const char* romaw_images_name(const romaw_image_set* s, unsigned i)
{
    return s && i < s->volumes.size() ? s->volumes[i].name.c_str() : nullptr;
}

extern "C" void romaw_images_close(romaw_image_set* s, int remove_files)
{
    if (!s)
        return;
    if (remove_files && !s->direct) {
        std::error_code ignored;
        fs::remove_all(s->root, ignored);
    }
    delete s;
}

extern "C" romaw_image_set* romaw_images_open_direct_utf8(const char* source, char* error, size_t capacity)
{
#ifdef _WIN32
    unsigned previous = image_code_page;
    image_code_page = CP_UTF8;
#endif
    auto* set = prepare_images(source, nullptr, nullptr, error, capacity, true, true);
#ifdef _WIN32
    image_code_page = previous;
#endif
    return set;
}

extern "C" romaw_image_set* romaw_images_open_direct(const char* source, void (*pump)(void), char* error,
                                                     size_t capacity)
{
    return prepare_images(source, nullptr, pump, error, capacity, true, true);
}

extern "C" int romaw_images_ndif_writable(const char* source)
{
    if (!source || !*source)
        return 0;
    try {
        auto path = native_path(source);
        auto header = ndif_resource(path);
        if (header.empty())
            return 1;
        CMyComPtr<IInStream> input = new Input(path);
        auto* mapped = new NdifInput(input, header);
        CMyComPtr<IInStream> keep = mapped;
        return mapped->writable_identity;
    }
    catch (...) {
        return 0;
    }
}

extern "C" int romaw_images_stat_utf8(romaw_image_set* set, const char* path, romaw_image_item* item)
{
    if (!set || !set->direct || !path || !item)
        return 0;
    try {
        std::lock_guard<std::mutex> lock(set->mutex);
        auto found = set->items.find(catalog_path(fs::u8path(path)));
        if (found == set->items.end())
            return 0;
        *item = found->second.info;
        return 1;
    }
    catch (...) {
        return 0;
    }
}

extern "C" int romaw_images_list_utf8(romaw_image_set* set, const char* path,
                                      int (*visit)(const char*, const romaw_image_item*, void*), void* opaque)
{
    if (!set || !set->direct || !path || !visit)
        return 0;
    try {
        std::lock_guard<std::mutex> lock(set->mutex);
        auto parent = catalog_path(fs::u8path(path));
        auto found = set->items.find(parent);
        check(found != set->items.end() && found->second.info.directory, "Missing image directory");
        for (auto it = set->items.upper_bound(parent); it != set->items.end(); ++it) {
            auto relative = it->first.lexically_relative(parent);
            if (relative.empty() || *relative.begin() == "..")
                break;
            if (it->first.parent_path() == parent)
                if (!visit(it->first.filename().u8string().c_str(), &it->second.info, opaque))
                    return 0;
        }
        return 1;
    }
    catch (...) {
        return 0;
    }
}

static std::vector<unsigned char> direct_fork(romaw_image_set::Item& item, bool resource)
{
    auto& read = resource ? item.resource : item.data;
    auto size = resource ? item.info.resource_size : item.info.data_size;
    check(size <= 128 * 1024 * 1024, "Image fork exceeds memory read limit");
    if (!size)
        return {};
    check(bool(read), "Missing image fork reader");
    auto bytes = read();
    check(bytes.size() == size, "Incomplete image fork");
    return bytes;
}

static void direct_range(romaw_image_set::Item& item, bool resource, uint64_t at, void* out, size_t count)
{
    uint64_t size = resource ? item.info.resource_size : item.info.data_size;
    check(at <= size && count <= size - at, "Image fork read outside bounds");
    if (!count)
        return;
    auto& range = resource ? item.resource_range : item.data_range;
    check(bool(range), "Missing image fork range reader");
    range(at, out, count);
}

extern "C" int romaw_images_read_fork_utf8(romaw_image_set* set, const char* path, int resource, uint64_t offset,
                                           void* bytes, size_t size, char* error, size_t capacity)
{
    try {
        check(set && set->direct && path && (bytes || !size) && size <= 65536, "Invalid direct fork read");
        std::lock_guard<std::mutex> lock(set->mutex);
        auto found = set->items.find(catalog_path(fs::u8path(path)));
        check(found != set->items.end(), "Missing image item");
        direct_range(found->second, resource != 0, offset, bytes, size);
        return 1;
    }
    catch (const std::exception& e) {
        if (error && capacity)
            snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}

static void copy_fork(romaw_image_set::Item& item, bool resource, std::ostream& out, romaw_image_copy_progress progress,
                      void* opaque, uint64_t& done, uint64_t total)
{
    uint64_t size = resource ? item.info.resource_size : item.info.data_size;
    unsigned char buffer[65536];
    for (uint64_t at = 0; at < size;) {
        size_t n = (size_t)std::min<uint64_t>(sizeof buffer, size - at);
        direct_range(item, resource, at, buffer, n);
        out.write((const char*)buffer, n);
        check(bool(out), "Cannot copy image fork");
        at += n;
        done += n;
        check(!progress || progress(opaque, done, total), "Image copy cancelled");
    }
}

extern "C" int romaw_images_fork_utf8(romaw_image_set* set, const char* path, int resource, unsigned char** bytes,
                                      size_t* size, char* error, size_t capacity)
{
    if (bytes)
        *bytes = nullptr;
    if (size)
        *size = 0;
    try {
        check(set && set->direct && path && bytes && size, "Missing image fork");
        std::lock_guard<std::mutex> lock(set->mutex);
        auto found = set->items.find(catalog_path(fs::u8path(path)));
        check(found != set->items.end(), "Missing image item");
        auto data = direct_fork(found->second, resource != 0);
        auto* result = static_cast<unsigned char*>(malloc(data.empty() ? 1 : data.size()));
        check(result != nullptr, "Cannot allocate image fork");
        if (!data.empty())
            memcpy(result, data.data(), data.size());
        *bytes = result;
        *size = data.size();
        return 1;
    }
    catch (const std::exception& e) {
        if (error && capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
    catch (...) {
        return 0;
    }
}

extern "C" int romaw_images_export_utf8(romaw_image_set* set, const char* path, const char* destination, char* error,
                                        size_t capacity)
{
    return romaw_images_export_progress_utf8(set, path, destination, nullptr, nullptr, error, capacity);
}

extern "C" int romaw_images_export_progress_utf8(romaw_image_set* set, const char* path, const char* destination,
                                                 romaw_image_copy_progress progress, void* opaque, char* error,
                                                 size_t capacity)
{
    try {
        check(set && set->direct && path && destination, "Missing image copy path");
        std::lock_guard<std::mutex> lock(set->mutex);
        auto source = catalog_path(fs::u8path(path));
        auto dest = fs::u8path(destination);
        check(set->items.count(source), "Missing image item");
        check(!fs::exists(dest), "The destination already exists");
        uint64_t total = 0, done = 0;
        for (auto it = set->items.lower_bound(source); it != set->items.end(); ++it) {
            auto relative = it->first.lexically_relative(source);
            if (relative.empty() || *relative.begin() == "..")
                break;
            check(it->second.info.data_size <= UINT64_MAX - total, "Image copy size overflow");
            total += it->second.info.data_size;
            check(it->second.info.resource_size <= UINT64_MAX - total, "Image copy size overflow");
            total += it->second.info.resource_size;
        }
        for (auto& entry : set->items) {
            auto relative = entry.first.lexically_relative(source);
            if (relative.empty() || *relative.begin() == "..")
                continue;
            check(!progress || progress(opaque, done, total), "Image copy cancelled");
            auto target = relative == "." ? dest : dest / relative;
            auto& item = entry.second;
            check(!fs::exists(target), "The destination already exists");
            if (item.info.directory)
                fs::create_directories(target);
            else {
                std::ofstream out(io_path(target), std::ios::binary);
                copy_fork(item, false, out, progress, opaque, done, total);
                out.close();
                check(bool(out), "Cannot copy image data fork");
            }
            if (item.info.resource_size ||
                std::any_of(std::begin(item.info.finder_info), std::end(item.info.finder_info),
                            [](unsigned char c) { return c != 0; })) {
                auto meta = target.parent_path() / fs::u8path("._" + target.filename().u8string());
                check(!fs::exists(meta), "The destination sidecar already exists");
                std::wstring hex;
                for (auto c : item.info.finder_info) {
                    hex += L"0123456789ABCDEF"[c >> 4];
                    hex += L"0123456789ABCDEF"[c & 15];
                }
                romaw_image_set writer;
                check(item.info.resource_size <= UINT32_MAX, "Resource fork exceeds AppleDouble size limit");
                sidecar(writer, meta, hex, (UInt32)item.info.resource_size);
                std::ofstream out(io_path(meta), std::ios::binary | std::ios::app);
                copy_fork(item, true, out, progress, opaque, done, total);
                out.close();
                check(bool(out), "Cannot copy image resource fork");
            }
        }
        return 1;
    }
    catch (const std::exception& e) {
        if (error && capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
    catch (...) {
        return 0;
    }
}

#include "hfs_write.h"
#include "virtual_hfs.h"

extern "C" const char* romaw_images_warning(const romaw_image_set* set)
{
    return set ? set->warning.c_str() : "";
}
