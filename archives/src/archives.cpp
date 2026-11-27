/*+===================================================================
  File:      archives.cpp

  Summary:   Validate archives and preserve Macintosh forks in ditto ZIP and legacy containers.

  Classes:   Item, MacBinaryInfo, Zip; standard containers and RAII handles.

  Functions: romaw::archives::validate, romaw::archives::metadata, romaw::archives::metadata_part,
             romaw::archives::zip, romaw::archives::macbinary, romaw::archives::expand, romaw::archives::canonicalize,
             romaw::archives::inspect_macbinary, romaw::archives::macbinary_metadata.

  Origin:    ROMAW codecs using miniz and stuffit-rs. BinHex follows RFC 1741;
             MacBinary follows the published II/III header layout.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#include "romaw_archives.hpp"
#include "miniz.h"
#include "stuffit_ffi.h"
#include "rsys/mac_storage.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

namespace romaw::archives {
namespace {
void require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

void extent(const Bytes& bytes, size_t at, size_t count)
{
    require(at <= bytes.size() && count <= bytes.size() - at, "Truncated archive");
}

unsigned word(const Bytes& bytes, size_t at)
{
    extent(bytes, at, 2);
    return unsigned(bytes[at]) << 8 | bytes[at + 1];
}

uint32_t number(const Bytes& bytes, size_t at)
{
    return uint32_t(word(bytes, at)) << 16 | word(bytes, at + 2);
}

void store(Bytes& bytes, size_t at, uint32_t value)
{
    extent(bytes, at, 4);
    for (unsigned i = 0; i < 4; ++i)
        bytes[at + i] = value >> (24 - i * 8);
}

Bytes part(const Bytes& bytes, size_t at, size_t size)
{
    extent(bytes, at, size);
    return Bytes(bytes.begin() + at, bytes.begin() + at + size);
}

uint16_t crc(const Bytes& bytes, size_t at, size_t size)
{
    extent(bytes, at, size);
    uint16_t value = 0;
    for (size_t i = at; i < at + size; ++i) {
        value ^= uint16_t(bytes[i]) << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            value = uint16_t(value << 1) ^ ((value & 0x8000) ? 0x1021 : 0);
    }
    return value;
}

void poll(const std::function<bool()>& cancelled)
{
    require(!cancelled || !cancelled(), "Archive operation cancelled");
}

bool valid_utf8(const std::string& text)
{
    for (size_t i = 0; i < text.size();) {
        unsigned char lead = text[i++];
        if (lead < 128)
            continue;
        unsigned count = lead >= 0xc2 && lead <= 0xdf   ? 1
                         : lead >= 0xe0 && lead <= 0xef ? 2
                         : lead >= 0xf0 && lead <= 0xf4 ? 3
                                                        : 0;
        if (!count || count > text.size() - i)
            return false;
        uint32_t point = lead & ((1u << (6 - count)) - 1);
        for (unsigned j = 0; j < count; ++j) {
            unsigned char next = text[i++];
            if ((next & 0xc0) != 0x80)
                return false;
            point = point << 6 | (next & 63);
        }
        if (point < (count == 1   ? 0x80u
                     : count == 2 ? 0x800u
                                  : 0x10000u) ||
            point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff))
            return false;
    }
    return true;
}

std::string mac_name(const Bytes& bytes, size_t at, size_t size, bool cp437 = false)
{
    extent(bytes, at, size);
    static constexpr uint16_t high[] = {
        0x00c4, 0x00c5, 0x00c7, 0x00c9, 0x00d1, 0x00d6, 0x00dc, 0x00e1, 0x00e0, 0x00e2, 0x00e4, 0x00e3, 0x00e5,
        0x00e7, 0x00e9, 0x00e8, 0x00ea, 0x00eb, 0x00ed, 0x00ec, 0x00ee, 0x00ef, 0x00f1, 0x00f3, 0x00f2, 0x00f4,
        0x00f6, 0x00f5, 0x00fa, 0x00f9, 0x00fb, 0x00fc, 0x2020, 0x00b0, 0x00a2, 0x00a3, 0x00a7, 0x2022, 0x00b6,
        0x00df, 0x00ae, 0x00a9, 0x2122, 0x00b4, 0x00a8, 0x2260, 0x00c6, 0x00d8, 0x221e, 0x00b1, 0x2264, 0x2265,
        0x00a5, 0x00b5, 0x2202, 0x2211, 0x220f, 0x03c0, 0x222b, 0x00aa, 0x00ba, 0x03a9, 0x00e6, 0x00f8, 0x00bf,
        0x00a1, 0x00ac, 0x221a, 0x0192, 0x2248, 0x2206, 0x00ab, 0x00bb, 0x2026, 0x00a0, 0x00c0, 0x00c3, 0x00d5,
        0x0152, 0x0153, 0x2013, 0x2014, 0x201c, 0x201d, 0x2018, 0x2019, 0x00f7, 0x25ca, 0x00ff, 0x0178, 0x2044,
        0x20ac, 0x2039, 0x203a, 0xfb01, 0xfb02, 0x2021, 0x00b7, 0x201a, 0x201e, 0x2030, 0x00c2, 0x00ca, 0x00c1,
        0x00cb, 0x00c8, 0x00cd, 0x00ce, 0x00cf, 0x00cc, 0x00d3, 0x00d4, 0xf8ff, 0x00d2, 0x00da, 0x00db, 0x00d9,
        0x0131, 0x02c6, 0x02dc, 0x00af, 0x02d8, 0x02d9, 0x02da, 0x00b8, 0x02dd, 0x02db, 0x02c7};
    static constexpr uint16_t high437[] = {
        0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7, 0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee,
        0x00ec, 0x00c4, 0x00c5, 0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9, 0x00ff, 0x00d6,
        0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192, 0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa,
        0x00ba, 0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb, 0x2591, 0x2592, 0x2593, 0x2502,
        0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510, 0x2514,
        0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f, 0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550,
        0x256c, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b, 0x256a, 0x2518, 0x250c,
        0x2588, 0x2584, 0x258c, 0x2590, 0x2580, 0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
        0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229, 0x2261, 0x00b1, 0x2265, 0x2264, 0x2320,
        0x2321, 0x00f7, 0x2248, 0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0};
    std::string name;
    for (size_t i = at; i < at + size; ++i) {
        unsigned value = bytes[i] < 128 ? bytes[i] : (cp437 ? high437[bytes[i] - 128] : high[bytes[i] - 128]);
        if (value < 128)
            name += char(value);
        else {
            if (value >= 2048) {
                name += char(0xe0 | value >> 12);
                name += char(0x80 | (value >> 6 & 63));
            }
            else
                name += char(0xc0 | value >> 6);
            name += char(0x80 | (value & 63));
        }
    }
    return name;
}

struct Zip {
    mz_zip_archive value{};

    ~Zip()
    {
        if (value.m_zip_mode == MZ_ZIP_MODE_READING)
            mz_zip_reader_end(&value);
        else
            mz_zip_writer_end(&value);
    }
};

std::vector<Item> unzip(const Bytes& bytes, const std::function<bool()>& cancelled)
{
    Zip archive;
    require(mz_zip_reader_init_mem(&archive.value, bytes.data(), bytes.size(), 0), "Invalid ZIP archive");
    unsigned count = mz_zip_reader_get_num_files(&archive.value);
    require(count && count <= 65535, "ZIP entry limit exceeded");
    size_t total = 0;
    std::vector<Item> result;
    for (unsigned i = 0; i < count; ++i) {
        poll(cancelled);
        mz_zip_archive_file_stat info{};
        require(mz_zip_reader_file_stat(&archive.value, i, &info) && info.m_is_supported && !info.m_is_encrypted,
                "Unsupported or encrypted ZIP entry");
        unsigned mode = info.m_external_attr >> 16;
        require(!(mode & 0170000) || (mode & 0170000) == 0100000 || (mode & 0170000) == 0040000,
                "ZIP links and special files cannot be extracted");
        unsigned length = mz_zip_reader_get_filename(&archive.value, i, nullptr, 0);
        require(length > 1 && length <= 32768, "Invalid ZIP entry name");
        std::string name(length, '\0');
        require(mz_zip_reader_get_filename(&archive.value, i, name.data(), length) == length,
                "Cannot read ZIP entry name");
        name.pop_back();
        require(name.find('\0') == std::string::npos, "Embedded NUL in ZIP entry name");
        if (!(info.m_bit_flag & 0x800)) {
            Bytes encoded(name.begin(), name.end());
            name = mac_name(encoded, 0, encoded.size(), true);
        }
        if (info.m_is_directory && name.back() == '/')
            name.pop_back();
        require(info.m_uncomp_size <= byte_limit - total, "Expanded archive exceeds 256 MiB");
        total += size_t(info.m_uncomp_size);
        Item item{name, info.m_is_directory != 0, {}, {}};
        item.data.resize(size_t(info.m_uncomp_size));
        require(mz_zip_reader_extract_to_mem(&archive.value, i, item.data.data(), item.data.size(), 0),
                "ZIP entry decompression or CRC failed");
        result.push_back(std::move(item));
    }
    return result;
}

std::vector<Item> unstuff(const Bytes& bytes, const std::function<bool()>& cancelled)
{
    std::unique_ptr<StuffitArchive, decltype(&stuffit_archive_free)> archive(
        stuffit_archive_parse_bytes(bytes.data(), bytes.size()), stuffit_archive_free);
    require(bool(archive), stuffit_last_error());
    size_t count = stuffit_archive_entry_count(archive.get()), total = 0;
    require(count && count <= 65535, "StuffIt entry limit exceeded");
    std::vector<Item> result;
    for (size_t i = 0; i < count; ++i) {
        poll(cancelled);
        StuffitEntryInfo info{};
        require(!stuffit_archive_entry_info(archive.get(), i, &info), "Cannot read StuffIt catalog");
        require(info.data_len <= byte_limit - total, "Expanded archive exceeds 256 MiB");
        total += info.data_len;
        require(info.resource_len <= byte_limit - total, "Expanded archive exceeds 256 MiB");
        total += info.resource_len;
        Item item{info.name, info.is_folder != 0, {}, {}};
        // Classic folder icon carriers have a literal carriage return in
        // their leaf name. Use the host spelling recognized by Finder.
        if (!item.directory) {
            const size_t leaf = item.name.find_last_of('/') + 1;
            if (item.name.compare(leaf, std::string::npos, MAC_STORAGE_CUSTOM_ICON_NAME) == 0)
                item.name.replace(leaf, std::string::npos, MAC_STORAGE_ESCAPED_ICON_NAME);
            else if (item.name.compare(leaf, std::string::npos, "._" MAC_STORAGE_CUSTOM_ICON_NAME) == 0)
                item.name.replace(leaf, std::string::npos, "._" MAC_STORAGE_ESCAPED_ICON_NAME);
        }
        StuffitBytes data{}, resource{};
        require(!stuffit_archive_entry_fork(archive.get(), i, 0, &data) &&
                    !stuffit_archive_entry_fork(archive.get(), i, 1, &resource),
                "Cannot read StuffIt forks");
        if (data.len)
            item.data.assign(data.ptr, data.ptr + data.len);
        Bytes finder(32);
        require(!stuffit_archive_entry_finder_info(archive.get(), i, finder.data()), "Cannot read StuffIt FinderInfo");
        item.metadata = metadata(finder, resource.len ? Bytes(resource.ptr, resource.ptr + resource.len) : Bytes{});
        result.push_back(std::move(item));
    }
    return result;
}

// Replace only the resource payload. Keeping a trailing resource in place also
// preserves absolute ATTR offsets inside extended FinderInfo. Other layouts
// with such offsets cannot safely be repacked.
Bytes with_resource(const Bytes& raw, const Bytes& resource, size_t offset_limit = SIZE_MAX)
{
    auto finder = metadata_part(raw, 9);
    unsigned count = word(raw, 24);
    for (unsigned i = 0; i < count; ++i) {
        size_t record = 26 + i * 12;
        size_t offset = number(raw, record + 4);
        if (number(raw, record) == 2 && offset <= offset_limit && offset + number(raw, record + 8) == raw.size()) {
            Bytes result(raw.begin(), raw.begin() + offset);
            store(result, record + 8, uint32_t(resource.size()));
            result.insert(result.end(), resource.begin(), resource.end());
            return result;
        }
    }
    require(finder.size() <= 32, "Cannot repack extended Finder information for MacBinary");
    std::vector<std::pair<uint32_t, Bytes>> parts;
    for (unsigned i = 0; i < count; ++i) {
        auto id = number(raw, 26 + i * 12);
        if (id != 2)
            parts.push_back({id, metadata_part(raw, id)});
    }
    parts.push_back({2, resource});
    require(parts.size() <= 65535, "Too many MacBinary metadata entries");
    Bytes result(26 + parts.size() * 12);
    std::copy_n(raw.begin(), 26, result.begin());
    result[24] = parts.size() >> 8;
    result[25] = parts.size();
    size_t record = 26;
    for (const auto& entry : parts) {
        store(result, record, entry.first);
        store(result, record + 4, uint32_t(result.size()));
        store(result, record + 8, uint32_t(entry.second.size()));
        result.insert(result.end(), entry.second.begin(), entry.second.end());
        record += 12;
    }
    return result;
}

std::vector<Item> unbinary(const Bytes& bytes)
{
    auto info = inspect_macbinary(bytes, bytes.size());
    Item item{info.name, false, part(bytes, info.data_offset, info.data_size),
              macbinary_metadata(info, part(bytes, 128, info.secondary_size),
                                 part(bytes, info.resource_offset, info.resource_size))};
    return {std::move(item)};
}

std::vector<Item> unhex(const Bytes& bytes)
{
    std::string text(bytes.begin(), bytes.end());
    auto banner = text.find("(This file must be converted with BinHex 4.0)");
    require(banner != std::string::npos, "Missing BinHex 4.0 banner");
    auto start = text.find(':', banner), end = start == std::string::npos ? start : text.find(':', start + 1);
    require(start != std::string::npos && end != std::string::npos, "Incomplete BinHex stream");
    constexpr char alphabet[] = "!\"#$%&'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr";
    Bytes packed, raw;
    uint32_t bits = 0;
    unsigned bit_count = 0;
    for (size_t i = start + 1; i < end; ++i) {
        unsigned char c = text[i];
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t')
            continue;
        auto found = std::strchr(alphabet, c);
        require(c && found, "Invalid BinHex character");
        bits = bits << 6 | unsigned(found - alphabet);
        bit_count += 6;
        if (bit_count >= 8) {
            bit_count -= 8;
            packed.push_back((bits >> bit_count) & 255);
        }
    }
    require(bit_count < 6 && !(bits & ((1u << bit_count) - 1)), "Invalid BinHex padding");
    for (size_t i = 0; i < packed.size(); ++i) {
        if (packed[i] != 0x90)
            raw.push_back(packed[i]);
        else {
            require(++i < packed.size(), "Incomplete BinHex repeat");
            unsigned count = packed[i];
            if (!count)
                raw.push_back(0x90);
            else {
                require(!raw.empty(), "Invalid BinHex repeat");
                require(count - 1 <= byte_limit - raw.size(), "Expanded BinHex exceeds 256 MiB");
                raw.insert(raw.end(), count - 1, raw.back());
            }
        }
        require(raw.size() <= byte_limit, "Expanded BinHex exceeds 256 MiB");
    }
    extent(raw, 0, 1);
    unsigned length = raw[0];
    require(length && length <= 63, "Invalid BinHex filename");
    size_t header = length + 20;
    extent(raw, 0, header + 2);
    require(!raw[length + 1] && crc(raw, 0, header) == word(raw, header), "BinHex header CRC failed");
    auto data_length = number(raw, length + 12), resource_length = number(raw, length + 16);
    size_t data_at = header + 2, resource_at = data_at + size_t(data_length) + 2;
    auto data = part(raw, data_at, data_length), resource = part(raw, resource_at, resource_length);
    require(crc(raw, data_at, data_length) == word(raw, data_at + data_length) &&
                crc(raw, resource_at, resource_length) == word(raw, resource_at + resource_length),
            "BinHex fork CRC failed");
    require(resource_at + resource_length + 2 == raw.size(), "Unexpected trailing BinHex data");
    Bytes finder(32);
    std::copy_n(raw.begin() + length + 2, 10, finder.begin());
    auto name = mac_name(raw, 1, length);
    require(name.find('/') == std::string::npos, "BinHex filename cannot specify a directory");
    return {{std::move(name), false, std::move(data), metadata(finder, resource)}};
}

std::vector<Item> companions(std::vector<Item> raw)
{
    std::map<std::string, Item> logical;
    std::map<std::string, Bytes> extras;
    for (auto& item : raw) {
        auto name = item.name;
        bool sequestered = name.rfind("__MACOSX/", 0) == 0;
        if (sequestered)
            name.erase(0, 9);
        auto slash = name.rfind('/');
        size_t base = slash == std::string::npos ? 0 : slash + 1;
        if (!item.directory && name.compare(base, 2, "._") == 0) {
            metadata_part(item.data, 9); // Validate the complete container.
            name.erase(base, 2);
            require(extras.emplace(name, std::move(item.data)).second, "Duplicate archive metadata");
        }
        else if (!(sequestered || name == "__MACOSX"))
            require(logical.emplace(name, std::move(item)).second, "Duplicate archive entry");
        else
            require(item.directory, "Unexpected file in ZIP metadata directory");
    }
    for (auto& extra : extras) {
        auto found = logical.find(extra.first);
        if (found == logical.end()) {
            auto child = logical.lower_bound(extra.first + '/');
            bool directory = child != logical.end() && child->first.rfind(extra.first + '/', 0) == 0;
            found = logical.emplace(extra.first, Item{extra.first, directory, {}, {}}).first;
        }
        if (!found->second.metadata.empty()) {
            auto a = metadata_part(found->second.metadata, 2), b = metadata_part(extra.second, 2);
            require(a.empty() || a == b, "Archive metadata resource fork conflicts with the stored fork");
        }
        found->second.metadata = std::move(extra.second);
    }
    std::vector<Item> out;
    for (auto& pair : logical)
        out.push_back(std::move(pair.second));
    return out;
}
}

MacBinaryInfo inspect_macbinary(const Bytes& bytes, uint64_t file_size)
{
    extent(bytes, 0, 128);
    require(file_size <= byte_limit && !bytes[0] && bytes[1] && bytes[1] <= 63 && !bytes[74] && !bytes[82],
            "Invalid or oversized MacBinary header");
    bool modern = bytes[122] == 129 || bytes[122] == 130;
    require(modern ? bytes[123] <= 130 && crc(bytes, 0, 124) == word(bytes, 124)
                   : !bytes[122] && !bytes[123] && !word(bytes, 124),
            "MacBinary header CRC or version failed");
    MacBinaryInfo info;
    info.name = mac_name(bytes, 2, bytes[1]);
    require(info.name.find('/') == std::string::npos, "MacBinary filename cannot specify a directory");
    validate({{info.name, false, {}, {}}});
    info.secondary_size = modern ? word(bytes, 120) : 0;
    info.data_offset = 128 + (info.secondary_size + 127) / 128 * 128;
    info.data_size = number(bytes, 83);
    info.resource_size = number(bytes, 87);
    require(info.data_size <= byte_limit && info.resource_size <= byte_limit - info.data_size,
            "MacBinary fork limit exceeded");
    info.resource_offset = info.data_offset + (info.data_size + 127) / 128 * 128;
    require(128 + info.secondary_size <= file_size && info.data_offset <= file_size &&
                info.data_size <= file_size - info.data_offset && info.resource_offset <= file_size &&
                info.resource_size <= file_size - info.resource_offset,
            "Truncated MacBinary fork");
    info.finder.resize(32);
    std::copy_n(bytes.begin() + 65, 8, info.finder.begin());
    info.finder[8] = bytes[73];
    info.finder[9] = modern ? bytes[101] : 0;
    std::copy_n(bytes.begin() + 75, 6, info.finder.begin() + 10);
    if (bytes[122] == 130) {
        info.finder[24] = bytes[106];
        info.finder[25] = bytes[107];
    }
    return info;
}

Bytes macbinary_metadata(const MacBinaryInfo& info, const Bytes& secondary, const Bytes& resource)
{
    require(secondary.size() == info.secondary_size && resource.size() == info.resource_size,
            "Truncated MacBinary metadata");
    if (secondary.empty())
        return metadata(info.finder, resource);
    metadata_part(secondary, 9);
    auto extra_resource = metadata_part(secondary, 2);
    require(extra_resource.empty() || extra_resource == resource,
            "MacBinary secondary resource fork conflicts with its primary fork");
    return extra_resource.empty() && !resource.empty() ? with_resource(secondary, resource) : secondary;
}

Bytes metadata_part(const Bytes& bytes, uint32_t id)
{
    unsigned count;
    require(romaw_mac_storage_table(bytes.data(), bytes.size(), &count), "Invalid archived AppleDouble header");
    std::set<uint32_t> ids;
    std::vector<std::pair<uint32_t, uint32_t>> spans;
    Bytes result;
    for (unsigned i = 0; i < count; ++i) {
        ROMAWMacStorageEntry entry;
        require(romaw_mac_storage_entry(bytes.data(), bytes.size(), bytes.size(), i, &entry) &&
                    ids.insert(entry.id).second,
                "Invalid archived AppleDouble entry");
        for (auto span : spans)
            require(!entry.length || entry.offset >= span.second || entry.offset + entry.length <= span.first,
                    "Overlapping archived AppleDouble entries");
        if (entry.length)
            spans.push_back({entry.offset, entry.offset + entry.length});
        if (entry.id == id)
            result = part(bytes, entry.offset, entry.length);
    }
    return result;
}

Bytes metadata(const Bytes& finder, const Bytes& resource)
{
    require(finder.size() == 32 && resource.size() <= byte_limit - 82, "Invalid Macintosh metadata");
    Bytes out(82);
    store(out, 0, 0x00051607);
    store(out, 4, 0x00020000);
    out[25] = 2;
    store(out, 26, 9);
    store(out, 30, 50);
    store(out, 34, 32);
    store(out, 38, 2);
    store(out, 42, 82);
    store(out, 46, uint32_t(resource.size()));
    std::copy(finder.begin(), finder.end(), out.begin() + 50);
    out.insert(out.end(), resource.begin(), resource.end());
    return out;
}

void validate(const std::vector<Item>& items)
{
    require(!items.empty() && items.size() <= 65535, "Archive entry limit exceeded");
    std::map<std::string, bool> paths;
    size_t total = 0;
    for (const auto& item : items) {
        require(valid_utf8(item.name), "Invalid UTF-8 archive name");
        require(!item.name.empty() && item.name.size() <= 32767 && item.name.front() != '/' &&
                    item.name.back() != '/' && item.name.find_first_of("\\:\0", 0, 3) == std::string::npos,
                "Unsafe archive path");
        std::string key;
        size_t start = 0, depth = 0;
        do {
            size_t end = item.name.find('/', start);
            if (end == std::string::npos)
                end = item.name.size();
            auto name = item.name.substr(start, end - start);
            require(++depth <= 128 && !name.empty() && name != "." && name != ".." && name.back() != '.' &&
                        name.back() != ' ',
                    "Unsafe archive path component");
            for (unsigned char c : name)
                require(c >= 32 && c != 127 && c != '<' && c != '>' && c != '"' && c != '|' && c != '?' && c != '*',
                        "Unsafe archive filename");
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return c < 128 ? char(std::tolower(c)) : char(c); });
            auto stem = name.substr(0, name.find('.'));
            require(stem != "con" && stem != "prn" && stem != "aux" && stem != "nul" &&
                        !(stem.size() == 4 && (stem.rfind("com", 0) == 0 || stem.rfind("lpt", 0) == 0) &&
                          stem[3] >= '0' && stem[3] <= '9') &&
                        name.rfind(".finder-copy-", 0) != 0,
                    "Reserved archive filename");
            if (!key.empty())
                key += '/';
            key += name;
            start = end == item.name.size() ? end : end + 1;
        } while (start < item.name.size());
        require(paths.emplace(key, item.directory).second, "Archive names collide at the destination");
        require(!item.directory || item.data.empty(), "Archive folder has file data");
        for (size_t size : {item.data.size(), item.metadata.size(), item.name.size(), size_t(256)}) {
            require(size <= byte_limit - total, "Expanded archive exceeds 256 MiB");
            total += size;
        }
        if (!item.metadata.empty())
            metadata_part(item.metadata, 9);
    }
    for (auto& path : paths) {
        auto slash = path.first.find('/');
        while (slash != std::string::npos) {
            auto parent = paths.find(path.first.substr(0, slash));
            require(parent == paths.end() || parent->second, "Archive file is also used as a folder");
            slash = path.first.find('/', slash + 1);
        }
    }
}

Bytes zip(const std::vector<Item>& items, const std::function<bool()>& cancelled)
{
    validate(items);
    Zip archive;
    require(mz_zip_writer_init_heap(&archive.value, 0, 0), "Cannot create ZIP archive");
    auto add = [&](const std::string& name, const Bytes& data) {
        poll(cancelled);
        require(mz_zip_writer_add_mem(&archive.value, name.c_str(), data.data(), data.size(), MZ_DEFAULT_COMPRESSION),
                "Cannot compress ZIP entry");
    };
    for (const auto& item : items) {
        require(item.name != "__MACOSX" && item.name.rfind("__MACOSX/", 0) != 0,
                "The __MACOSX name is reserved for ZIP metadata");
        add(item.name + (item.directory ? "/" : ""), item.data);
        if (!item.metadata.empty()) {
            auto slash = item.name.rfind('/');
            auto parent = slash == std::string::npos ? std::string{} : item.name.substr(0, slash + 1);
            auto base = slash == std::string::npos ? item.name : item.name.substr(slash + 1);
            add("__MACOSX/" + parent + "._" + base, item.metadata);
        }
    }
    void* memory = nullptr;
    size_t size = 0;
    require(mz_zip_writer_finalize_heap_archive(&archive.value, &memory, &size), "Cannot finish ZIP archive");
    std::unique_ptr<void, decltype(&mz_free)> owner(memory, mz_free);
    Bytes output(static_cast<unsigned char*>(memory), static_cast<unsigned char*>(memory) + size);
    // Exercise CRC verification before publication; the caller also verifies its staged disk write.
    auto checked = expand(output, cancelled);
    require(checked.size() == items.size(), "ZIP verification changed the catalog");
    for (const auto& item : items) {
        auto found =
            std::find_if(checked.begin(), checked.end(), [&](const auto& other) { return item.name == other.name; });
        require(found != checked.end() && found->directory == item.directory && found->data == item.data &&
                    found->metadata == item.metadata,
                "ZIP verification changed a fork or Finder metadata");
    }
    return output;
}

Bytes macbinary(const Item& item, const std::function<bool()>& cancelled)
{
    poll(cancelled);
    validate({item});
    require(!item.directory && item.name.find('/') == std::string::npos, "MacBinary requires one file");
    Bytes name;
    // Invert the same MacRoman mapping used by the decoder. Never substitute
    // characters or truncate names silently when creating a legacy container.
    for (size_t at = 0; at < item.name.size();) {
        bool found = false;
        for (unsigned value = 1; value < 256; ++value) {
            auto text = mac_name(Bytes{static_cast<unsigned char>(value)}, 0, 1);
            if (item.name.compare(at, text.size(), text) == 0) {
                name.push_back(value);
                at += text.size();
                found = true;
                break;
            }
        }
        require(found && name.size() <= 63, "MacBinary needs a MacRoman filename of at most 63 bytes");
    }
    auto original = item.metadata.empty() ? metadata(Bytes(32), {}) : item.metadata;
    auto finder = metadata_part(original, 9), resource = metadata_part(original, 2);
    require(finder.empty() || finder.size() >= 32, "Incomplete MacBinary Finder information");
    if (finder.empty())
        finder.resize(32);
    // Appended resource forks can leave obsolete bytes in the AppleDouble
    // prefix. Repack its referenced entries if that prefix cannot fit.
    auto secondary = with_resource(original, {}, UINT16_MAX);
    require(secondary.size() <= UINT16_MAX, "MacBinary secondary metadata exceeds 65535 bytes");
    Bytes result(128);
    result[1] = name.size();
    std::copy(name.begin(), name.end(), result.begin() + 2);
    std::copy_n(finder.begin(), 8, result.begin() + 65);
    result[73] = finder[8];
    std::copy_n(finder.begin() + 10, 6, result.begin() + 75);
    store(result, 83, uint32_t(item.data.size()));
    store(result, 87, uint32_t(resource.size()));
    result[101] = finder[9];
    store(result, 102, 0x6d42494e); // mBIN
    // The name above is MacRoman even if the source record has another script.
    // Preserve that original record in the secondary header instead.
    result[106] = 0;
    result[107] = finder[25];
    result[120] = secondary.size() >> 8;
    result[121] = secondary.size();
    result[122] = name.size() <= 31 ? 130 : 129;
    result[123] = 129;
    auto checksum = crc(result, 0, 124);
    result[124] = checksum >> 8;
    result[125] = checksum;
    for (const auto* bytes : std::array<const Bytes*, 3>{&secondary, &item.data, &resource}) {
        poll(cancelled);
        require(bytes->size() <= byte_limit - result.size(), "MacBinary exceeds 256 MiB");
        result.insert(result.end(), bytes->begin(), bytes->end());
        require((128 - result.size() % 128) % 128 <= byte_limit - result.size(), "MacBinary exceeds 256 MiB");
        result.resize((result.size() + 127) / 128 * 128);
    }
    auto checked = expand(result, cancelled);
    require(checked.size() == 1 && checked[0].name == item.name && checked[0].data == item.data &&
                metadata_part(checked[0].metadata, 9) == metadata_part(original, 9) &&
                metadata_part(checked[0].metadata, 2) == resource,
            "MacBinary verification changed a fork or Finder information");
    return result;
}

std::vector<Item> canonicalize(std::vector<Item> osx_layout)
{
    validate(osx_layout);
    auto logical = companions(std::move(osx_layout));
    validate(logical);
    return logical;
}

std::vector<Item> expand(const Bytes& bytes, const std::function<bool()>& cancelled, Format* format)
{
    poll(cancelled);
    require(bytes.size() <= byte_limit, "Archive exceeds 256 MiB");
    std::vector<Item> out;
    Format detected;
    if (bytes.size() >= 4 && bytes[0] == 'P' && bytes[1] == 'K') {
        detected = Format::zip;
        out = unzip(bytes, cancelled);
    }
    else if (bytes.size() >= 7 &&
             (std::memcmp(bytes.data(), "StuffIt", 7) == 0 || std::memcmp(bytes.data(), "SIT!", 4) == 0 ||
              std::memcmp(bytes.data(), "STi4", 4) == 0)) {
        detected = std::memcmp(bytes.data(), "StuffIt", 7) == 0 ? Format::stuffit5 : Format::stuffit;
        out = unstuff(bytes, cancelled);
    }
    else if (!bytes.empty() && bytes.front() == 0) {
        detected = Format::macbinary;
        out = unbinary(bytes);
    }
    else {
        detected = Format::binhex;
        out = unhex(bytes);
    }
    out = canonicalize(std::move(out));
    poll(cancelled);
    if (format)
        *format = detected;
    return out;
}
}
