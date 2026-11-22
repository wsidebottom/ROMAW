/*+===================================================================
  File:      desktop_database.cpp

  Summary:   Classic Desktop DB/DF storage. Layout references: SuperMario DTDBMgrPriv.a, FSPrivate.a, FileMgrPrivate.a
             and BTreeMgr/BTreeFuncs.a (BTInit). This is a fresh implementation; no Apple implementation is
             incorporated.

  Classes:   Declared: Error, Lock, Node; Used: Error, Lock, Node, std::array, std::bad_alloc, std::ifstream,
             std::lock_guard, std::map, std::mutex, std::ofstream, std::pair, std::vector

  Functions: romaw_desktop_database, romaw_desktop_database_wide, romaw_desktop_database_read,
             romaw_desktop_database_comment.

  Origin:    Existing repository source in executor/src/files; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Classic Desktop DB/DF storage. Layout references: SuperMario DTDBMgrPriv.a,
   FSPrivate.a, FileMgrPrivate.a and BTreeMgr/BTreeFuncs.a (BTInit).
   This is a fresh implementation; no Apple implementation is incorporated. */
#include "rsys/desktop_database.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
using Records = std::map<Bytes, Bytes>;
constexpr size_t node_size = 512, header_size = 106, user_size = 128;
constexpr size_t bitmap_offset = 14 + header_size + user_size;
constexpr size_t max_nodes = (504 - bitmap_offset) * 8;
constexpr size_t data_limit = 64 * 1024 * 1024;
std::mutex database_mutex;

struct Error {
    int code;
};

uint32_t get(const unsigned char* p, unsigned n)
{
    uint32_t value = 0;
    while (n--)
        value = value << 8 | *p++;
    return value;
}

void put(unsigned char* p, uint32_t value, unsigned n)
{
    while (n) {
        p[--n] = value;
        value >>= 8;
    }
}

Bytes read_file(const fs::path& path, size_t limit)
{
    auto size = fs::file_size(path);
    if (size > limit)
        throw Error{-1305};
    Bytes result(size);
    std::ifstream input(path, std::ios::binary);
    if (!input || !input.read(reinterpret_cast<char*>(result.data()), size))
        throw Error{-36};
    return result;
}

void write_file(const fs::path& path, const Bytes& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()))
        throw Error{-36};
    output.close();
    if (!output)
        throw Error{-36};
}

/* All file mutation is serialized, including between ROMAW processes.
   The lock is separate from the index because that index is replaced atomically. */
struct Lock {
#ifdef _WIN32
    HANDLE file;

    explicit Lock(const fs::path& root)
    {
        file = CreateFileW((root / L".Desktop.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_HIDDEN, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            throw Error{-47};
    }

    ~Lock()
    {
        CloseHandle(file);
    }
#else
    explicit Lock(const fs::path&)
    {
    }
#endif
};

/* Validate the complete leaf chain before exposing or modifying any record.
   Index nodes use padded 37-byte keys; leaves use their actual key lengths. */
Records decode(const Bytes& tree)
{
    if (tree.size() < node_size || tree.size() % node_size || tree[8] != 1 || get(&tree[10], 2) != 3 ||
        get(&tree[510], 2) != 14 || get(&tree[508], 2) != 14 + header_size || get(&tree[506], 2) != bitmap_offset ||
        get(&tree[14 + 18], 2) != node_size || get(&tree[14 + 20], 2) != 37 || tree[14 + 36] != 255 ||
        tree[14 + header_size] != 2 || tree[15 + header_size] != 10 || tree[16 + header_size] != 1)
        throw Error{-1305};
    uint32_t nodes = get(&tree[14 + 22], 4);
    uint32_t expected = get(&tree[14 + 6], 4);
    uint32_t current = get(&tree[14 + 10], 4), previous = 0;
    if (!nodes || nodes > tree.size() / node_size || expected > 65536)
        throw Error{-1305};
    std::vector<bool> seen(nodes);
    Records records;
    Bytes last_key;
    while (current) {
        if (current >= nodes || seen[current])
            throw Error{-1305};
        seen[current] = true;
        auto node = &tree[current * node_size];
        unsigned count = get(node + 10, 2);
        if (node[8] != 255 || node[9] != 1 || !count || count > 165 || get(node + 4, 4) != previous)
            throw Error{-1305};
        unsigned end = get(node + 512 - (count + 1) * 2, 2);
        if (end > 512 - (count + 1) * 2)
            throw Error{-1305};
        for (unsigned i = 0; i < count; ++i) {
            unsigned begin = get(node + 510 - i * 2, 2);
            unsigned next = get(node + 508 - i * 2, 2);
            if (begin < 14 || begin >= next || next > end || begin % 2 || !node[begin] || node[begin] > 37)
                throw Error{-1305};
            unsigned length = node[begin] + 1;
            unsigned value = begin + ((length + 1) & ~1u);
            if (value > next)
                throw Error{-1305};
            Bytes key(node + begin, node + begin + length);
            /* Length is not part of DTKeyCmp's byte comparison. All records
             of each Desktop key type have the same length. */
            if ((key[1] == 1 && length != 12) || (key[1] == 2 && length != 8) || (key[1] == 3 && length != 6) ||
                key[1] < 1 || key[1] > 3 ||
                (!last_key.empty() &&
                 !std::lexicographical_compare(last_key.begin() + 1, last_key.end(), key.begin() + 1, key.end())))
                throw Error{-1305};
            Bytes payload(node + value, node + next);
            if ((key[1] == 1 && payload.size() != 10) || (key[1] == 2 && (payload.size() != 40 || payload[8] > 31)) ||
                (key[1] == 3 && (payload.empty() || payload[0] > 199 || payload.size() < size_t(payload[0]) + 1 ||
                                 payload.size() > size_t(payload[0]) + 2)))
                throw Error{-1305};
            records.emplace(key, payload);
            last_key = std::move(key);
        }
        previous = current;
        current = get(node, 4);
    }
    if (previous != get(&tree[14 + 14], 4) || records.size() != expected)
        throw Error{-1305};
    return records;
}

struct Node {
    std::array<unsigned char, 512> bytes = {};
    Bytes first;
    unsigned used = 14, count = 0;

    bool fits(size_t length) const
    {
        return used + length + (count + 2) * 2 <= 512;
    }

    void add(const Bytes& record)
    {
        put(&bytes[510 - count * 2], used, 2);
        std::copy(record.begin(), record.end(), bytes.begin() + used);
        used += record.size();
        ++count;
        put(&bytes[510 - count * 2], used, 2);
        put(&bytes[10], count, 2);
    }
};

Bytes encode(const Records& records)
{
    std::vector<Node> nodes(1);
    std::vector<std::pair<Bytes, Bytes>> sorted(records.begin(), records.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return std::lexicographical_compare(a.first.begin() + 1, a.first.end(), b.first.begin() + 1, b.first.end());
    });
    std::vector<unsigned> level;
    for (const auto& entry : sorted) {
        Bytes record = entry.first;
        record.resize((record.size() + 1) & ~size_t(1), 0);
        record.insert(record.end(), entry.second.begin(), entry.second.end());
        record.resize((record.size() + 1) & ~size_t(1), 0);
        if (level.empty() || !nodes.back().fits(record.size())) {
            if (record.size() > 490)
                throw Error{-50};
            level.push_back(nodes.size());
            nodes.emplace_back();
            nodes.back().bytes[8] = 255;
            nodes.back().bytes[9] = 1;
            nodes.back().first = entry.first;
        }
        nodes.back().add(record);
    }
    unsigned first = level.empty() ? 0 : level.front();
    unsigned last = level.empty() ? 0 : level.back();
    unsigned depth = level.empty() ? 0 : 1;
    for (;;) {
        for (size_t i = 0; i < level.size(); ++i) {
            put(nodes[level[i]].bytes.data(), i + 1 < level.size() ? level[i + 1] : 0, 4);
            put(nodes[level[i]].bytes.data() + 4, i ? level[i - 1] : 0, 4);
        }
        if (level.size() < 2)
            break;
        std::vector<unsigned> parents;
        ++depth;
        for (unsigned child : level) {
            Bytes record(42, 0);
            std::copy(nodes[child].first.begin(), nodes[child].first.end(), record.begin());
            record[0] = 37;
            put(record.data() + 38, child, 4);
            if (parents.empty() || !nodes.back().fits(record.size())) {
                parents.push_back(nodes.size());
                Node parent;
                parent.first = nodes[child].first;
                parent.bytes[9] = depth;
                nodes.push_back(parent);
            }
            nodes.back().add(record);
        }
        level = std::move(parents);
    }
    if (nodes.size() > max_nodes)
        throw Error{-34};
    unsigned total = (nodes.size() + 31) & ~31u;
    Node& header = nodes[0];
    header.bytes[8] = 1;
    put(&header.bytes[10], 3, 2);
    auto h = header.bytes.data() + 14;
    put(h, depth, 2);
    put(h + 2, level.empty() ? 0 : level[0], 4);
    put(h + 6, records.size(), 4);
    put(h + 10, first, 4);
    put(h + 14, last, 4);
    put(h + 18, 512, 2);
    put(h + 20, 37, 2);
    put(h + 22, total, 4);
    put(h + 26, total - nodes.size(), 4);
    put(h + 32, 16384, 4);
    h[36] = 255;
    h[header_size] = 2;
    h[header_size + 1] = 10;
    h[header_size + 2] = 1;
    for (unsigned i = 0; i < nodes.size(); ++i)
        header.bytes[bitmap_offset + i / 8] |= 0x80 >> (i % 8);
    put(&header.bytes[510], 14, 2);
    put(&header.bytes[508], 14 + header_size, 2);
    put(&header.bytes[506], bitmap_offset, 2);
    put(&header.bytes[504], 504, 2);
    Bytes tree(total * 512, 0);
    for (unsigned i = 0; i < nodes.size(); ++i)
        std::copy(nodes[i].bytes.begin(), nodes[i].bytes.end(), tree.begin() + i * 512);
    return tree;
}

void save(const fs::path& path, const Records& records)
{
    Bytes bytes = encode(records);
    auto temporary = path;
    temporary += ".romaw-new";
    write_file(temporary, bytes);
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw Error{-36};
#else
    fs::rename(temporary, path);
#endif
}
}

static int desktop_database(const char* root, const wchar_t* wide_root, enum desktop_operation op,
                            unsigned char key[38], unsigned index, void* data, size_t* size, uint32_t* tag)
{
    try {
        if ((!root && !wide_root) || !size || !tag)
            return -50;
        std::lock_guard<std::mutex> guard(database_mutex);
        fs::path directory = wide_root ? fs::path(wide_root) : fs::path(root);
        fs::path db = directory / "Desktop DB", df = directory / "Desktop DF";
        Lock lock(directory);
        bool exists = fs::exists(db) && fs::exists(df);
        if (op == desktop_delete) {
            fs::remove(db);
            fs::remove(df);
            return 0;
        }
        if (op == desktop_open && !exists) {
            Records retained;
            if (fs::exists(db) && !fs::exists(df)) {
                // Losing icon pixels does not invalidate APPL paths/comments.
                // Validate the index before changing either file, then remove
                // only records whose offsets refer to the missing data file.
                retained = decode(read_file(db, 16 * 1024 * 1024));
                for (auto i = retained.begin(); i != retained.end();)
                    if (i->first[1] == 1)
                        i = retained.erase(i);
                    else
                        ++i;
            }
            /* Never truncate the remaining DF: a still-existing DB may refer to
             it if creation fails. An empty index has no references to it. */
            if (!fs::exists(df))
                write_file(df, {});
            save(db, retained);
        }
        if (!exists && op != desktop_open)
            return -1305;
        Records records = decode(read_file(db, 16 * 1024 * 1024));
        auto df_size = fs::file_size(df);
        for (const auto& entry : records)
            if (entry.first[1] == 1 &&
                uint64_t(get(entry.second.data() + 4, 4)) + get(entry.second.data() + 8, 2) > df_size)
                return -1305;
        if (op == desktop_open) {
            *size = exists;
            return 0;
        }
        if (op == desktop_reset) {
            save(db, {});
            write_file(df, {});
            return 0;
        }
        if (op == desktop_info) {
            uint64_t length = fs::file_size(db) + df_size;
            if (length > INT32_MAX)
                return -34;
            *size = length;
            return 0;
        }
        if (!key || key[0] < 1 || key[0] > 37)
            return -50;
        Bytes lookup(key, key + key[0] + 1);
        auto found = records.find(lookup);
        if (op == desktop_index) {
            if (!index)
                return -413;
            std::vector<Records::iterator> matches;
            for (auto i = records.begin(); i != records.end(); ++i)
                if (i->first.size() >= lookup.size() &&
                    std::equal(lookup.begin() + 1, lookup.end(), i->first.begin() + 1))
                    matches.push_back(i);
            if (index > matches.size())
                return -413;
            found = matches[index - 1];
            std::copy(found->first.begin(), found->first.end(), key);
        }
        if (op == desktop_get || op == desktop_index) {
            if (found == records.end())
                return -413;
            const Bytes& value = found->second;
            if (key[1] == 1 && op == desktop_get) {
                *tag = get(value.data(), 4);
                size_t count = std::min(*size, size_t(get(value.data() + 8, 2)));
                if (count && !data)
                    return -50;
                std::ifstream input(df, std::ios::binary);
                input.seekg(get(value.data() + 4, 4));
                if (!input.read(static_cast<char*>(data), count))
                    return -36;
                *size = count;
            }
            else {
                size_t count = std::min(*size, value.size());
                if (count && !data)
                    return -50;
                if (count)
                    memcpy(data, value.data(), count);
                *size = count;
            }
            return 0;
        }
        if (op == desktop_remove) {
            if (found == records.end())
                return -413;
            records.erase(found);
        }
        else if (op == desktop_put) {
            if ((*size && !data) || *size > 4500 || (key[1] == 1 && key[0] != 11) ||
                (key[1] == 2 && (key[0] != 7 || *size != 40)) ||
                (key[1] == 3 && (key[0] != 5 || !*size || *size > 200)) || key[1] < 1 || key[1] > 3)
                return -50;
            Bytes value;
            if (key[1] == 1) {
                if (found != records.end() && get(found->second.data() + 8, 2) != *size)
                    return -5030;
                if (df_size + *size > data_limit)
                    return -34;
                std::ofstream output(df, std::ios::binary | std::ios::app);
                if (!output.write(static_cast<const char*>(data), *size))
                    return -36;
                output.close();
                if (!output)
                    return -36;
                value.resize(10);
                put(value.data(), *tag, 4);
                put(value.data() + 4, df_size, 4);
                put(value.data() + 8, *size, 2);
            }
            else
                value.assign(static_cast<unsigned char*>(data), static_cast<unsigned char*>(data) + *size);
            records[lookup] = std::move(value);
        }
        else
            return -50;
        save(db, records);
        return 0;
    }
    catch (const Error& error) {
        return error.code;
    }
    catch (const std::bad_alloc&) {
        return -108;
    }
    catch (...) {
        return -36;
    }
}

extern "C" int romaw_desktop_database(const char* root, enum desktop_operation op, unsigned char key[38],
                                      unsigned index, void* data, size_t* size, uint32_t* tag)
{
    return desktop_database(root, nullptr, op, key, index, data, size, tag);
}
#ifdef _WIN32
extern "C" int romaw_desktop_database_wide(const wchar_t* root, enum desktop_operation op, unsigned char key[38],
                                           unsigned index, void* data, size_t* size, uint32_t* tag)
{
    return desktop_database(nullptr, root, op, key, index, data, size, tag);
}
#endif

extern "C" int romaw_desktop_database_comment(romaw_desktop_read read, romaw_desktop_publish publish, void* opaque,
                                              size_t db_size, uint32_t id, const void* text, size_t size)
{
    if (!read || !publish || id <= 2 || size > 199 || (size && !text))
        return -50;
    if (db_size > 16 * 1024 * 1024)
        return -1305;
    try {
        Bytes old(db_size);
        for (size_t offset = 0; offset < db_size;) {
            size_t count = std::min(size_t(32768), db_size - offset);
            if (!read(opaque, 0, offset, old.data() + offset, count))
                return -36;
            offset += count;
        }
        Records records = db_size ? decode(old) : Records{};
        Bytes key{5,
                  3,
                  static_cast<unsigned char>(id >> 24),
                  static_cast<unsigned char>(id >> 16),
                  static_cast<unsigned char>(id >> 8),
                  static_cast<unsigned char>(id)};
        if (size) {
            Bytes value(size + 1);
            value[0] = size;
            memcpy(value.data() + 1, text, size);
            auto found = records.find(key);
            if (found != records.end() && found->second.size() >= value.size() &&
                std::equal(value.begin(), value.end(), found->second.begin()))
                return 0;
            records[key] = std::move(value);
        }
        else if (!records.erase(key))
            return 0;
        Bytes tree = encode(records);
        return publish(opaque, tree.data(), tree.size(), old.data(), old.size());
    }
    catch (const Error& error) {
        return error.code;
    }
    catch (const std::bad_alloc&) {
        return -108;
    }
    catch (...) {
        return -36;
    }
}

// Traverse source-backed leaf nodes, retaining only the current metadata node.
extern "C" int romaw_desktop_database_read(romaw_desktop_read read, void* opaque, uint64_t db_size, uint64_t df_size,
                                           desktop_operation op, unsigned char key[38], unsigned index, void* data,
                                           size_t* size, uint32_t* tag)
{
    try {
        if (!read || !size || !tag)
            return -50;
        if (op != desktop_get && op != desktop_index && op != desktop_info && op != desktop_open)
            return -44;
        unsigned char header[120];
        if (db_size < sizeof header || !read(opaque, 0, 0, header, sizeof header))
            return -36;
        unsigned node_bytes = get(header + 32, 2);
        uint32_t nodes = get(header + 36, 4), current = get(header + 24, 4), previous = 0;
        if (header[8] != 1 || node_bytes < 512 || node_bytes > 32768 || (node_bytes & (node_bytes - 1)) || !nodes ||
            nodes > db_size / node_bytes || get(header + 34, 2) > 37)
            return -1305;
        if (op == desktop_info) {
            if (db_size > INT32_MAX || df_size > INT32_MAX - db_size)
                return -34;
            *size = size_t(db_size + df_size);
            return 0;
        }
        if (op == desktop_open) {
            *size = 1;
            return 0;
        }
        if (!key || key[0] < 1 || key[0] > 37 || (op == desktop_index && !index))
            return -50;
        std::array<unsigned char, 38> lookup{};
        memcpy(lookup.data(), key, key[0] + 1);
        Bytes node(node_bytes);
        for (uint32_t visited = 0; current; ++visited) {
            if (visited >= nodes || current >= nodes ||
                !read(opaque, 0, uint64_t(current) * node_bytes, node.data(), node.size()))
                return -36;
            unsigned count = get(node.data() + 10, 2);
            if (node[8] != 255 || node[9] != 1 || get(node.data() + 4, 4) != previous ||
                count > (node_bytes - 14) / 4 - 1)
                return -1305;
            unsigned limit = node_bytes - (count + 1) * 2;
            for (unsigned record = 0; record < count; ++record) {
                unsigned start = get(node.data() + node_bytes - 2 * (record + 1), 2);
                unsigned end = get(node.data() + node_bytes - 2 * (record + 2), 2);
                if (start < 14 || start >= end || end > limit || start % 2)
                    return -1305;
                unsigned length = node[start];
                unsigned payload = start + ((length + 2) & ~1u);
                if (!length || length > 37 || payload > end)
                    return -1305;
                auto* value = node.data() + payload;
                size_t value_bytes = end - payload;
                unsigned kind = node[start + 1];
                if ((kind == 1 && (length != 11 || value_bytes != 10)) ||
                    (kind == 2 && (length != 7 || value_bytes != 40 || value[8] > 31)) ||
                    (kind == 3 && (length != 5 || !value_bytes || value[0] > 199 ||
                                   value_bytes < size_t(value[0]) + 1 || value_bytes > size_t(value[0]) + 2)) ||
                    kind < 1 || kind > 3)
                    return -1305;
                bool match = op == desktop_get ? length == lookup[0] : length >= lookup[0];
                match = match && !memcmp(node.data() + start + 1, lookup.data() + 1, lookup[0]);
                if (!match || (op == desktop_index && --index))
                    continue;
                memcpy(key, node.data() + start, length + 1);
                if (key[1] == 1 && op == desktop_get) {
                    if (value_bytes != 10)
                        return -1305;
                    uint64_t offset = get(value + 4, 4);
                    size_t bytes = get(value + 8, 2);
                    if (offset > df_size || bytes > df_size - offset)
                        return -1305;
                    bytes = std::min(bytes, *size);
                    if (bytes && (!data || !read(opaque, 1, offset, data, bytes)))
                        return -36;
                    *tag = get(value, 4);
                    *size = bytes;
                }
                else {
                    value_bytes = std::min(value_bytes, *size);
                    if (value_bytes && !data)
                        return -50;
                    if (value_bytes)
                        memcpy(data, value, value_bytes);
                    *size = value_bytes;
                }
                return 0;
            }
            previous = current;
            current = get(node.data(), 4);
        }
        return -413;
    }
    catch (const std::bad_alloc&) {
        return -108;
    }
    catch (...) {
        return -36;
    }
}
