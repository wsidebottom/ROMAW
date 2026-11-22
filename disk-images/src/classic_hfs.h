/*+===================================================================
  File:      classic_hfs.h

  Summary:   Declares classic hfs support for disk images package support.

  Classes:   Declared: ClassicHfs, Extent, Item; Used: ClassicHfs, Extent, Item, romaw_image_set, std::map,
             std::ofstream, std::pair, std::set, std::string, std::vector, std::wstring

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in packages/disk-images/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Bounded, read-only classic HFS projection. Included by images.cpp so filename
   escaping, AppleDouble output and extraction budgets share one implementation. */
class ClassicHfs : public std::enable_shared_from_this<ClassicHfs> {
    CMyComPtr<IInStream> input;
    UInt64 base, length, allocation;
    unsigned block_size, blocks;

    struct Extent {
        unsigned start, count;
    };

    using Extents = std::vector<Extent>;
    std::map<std::pair<UInt32, unsigned>, std::map<unsigned, Extents>> overflow;

    static unsigned word(const unsigned char* p)
    {
        return (unsigned)p[0] << 8 | p[1];
    }

    static UInt32 number(const unsigned char* p)
    {
        return (UInt32)word(p) << 16 | word(p + 2);
    }

    static std::string filename(const unsigned char* p, size_t size)
    {
        std::wstring name;
        for (size_t i = 0; i < size; ++i)
            name += p[i] < 128 ? p[i] : roman[p[i] - 128].value;
        return component(name);
    }

    std::vector<unsigned char> read(UInt64 offset, unsigned size)
    {
        check(offset <= length && size <= length - offset, "HFS read outside image");
        check(input->Seek(base + offset, 0, nullptr) == S_OK, "Cannot seek HFS image");
        std::vector<unsigned char> data(size);
        unsigned done = 0;
        while (done < size) {
            UInt32 got = 0;
            auto result = input->Read(data.data() + done, size - done, &got);
            check(result == S_OK && got && got <= size - done,
                  ("Truncated HFS image at " + std::to_string(base + offset) + " reading " + std::to_string(size) +
                   " got " + std::to_string(got))
                      .c_str());
            done += got;
        }
        return data;
    }

    Extents extents(const unsigned char* p)
    {
        Extents result;
        for (unsigned i = 0; i < 3; ++i) {
            unsigned start = word(p + i * 4), n = word(p + i * 4 + 2);
            check(start <= blocks && n <= blocks - start, "Invalid HFS extent");
            if (n) {
                check(allocation + (UInt64)start * block_size <= length &&
                          (UInt64)n * block_size <= length - allocation - (UInt64)start * block_size,
                      "HFS extent outside image");
                result.push_back({start, n});
            }
        }
        return result;
    }

    Extents chain(Extents result, UInt32 id, unsigned fork, UInt32 size)
    {
        UInt64 covered = 0;
        for (auto e : result)
            covered += e.count;
        while (covered * block_size < size) {
            auto& records = overflow[{id, fork}];
            auto found = records.find((unsigned)covered);
            check(found != records.end(), "Missing HFS overflow extents");
            UInt64 before = covered;
            for (auto e : found->second) {
                result.push_back(e);
                covered += e.count;
            }
            check(covered > before && covered <= blocks, "Invalid HFS extent chain");
        }
        return result;
    }

    std::vector<unsigned char> fork_bytes(Extents e, UInt32 size)
    {
        check(size <= 128 * 1024 * 1024, "HFS B-tree exceeds memory limit");
        std::vector<unsigned char> data;
        for (auto item : e) {
            unsigned n = (unsigned)std::min<UInt64>((UInt64)item.count * block_size, size - data.size());
            auto part = read(allocation + (UInt64)item.start * block_size, n);
            data.insert(data.end(), part.begin(), part.end());
            if (data.size() == size)
                break;
        }
        check(data.size() == size, "Truncated HFS B-tree extents");
        return data;
    }

    void fork_range(const Extents& extents, uint64_t offset, void* buffer, size_t size)
    {
        auto* out = static_cast<unsigned char*>(buffer);
        for (auto extent : extents) {
            uint64_t bytes = (uint64_t)extent.count * block_size;
            if (offset >= bytes) {
                offset -= bytes;
                continue;
            }
            size_t take = (size_t)std::min<uint64_t>(size, bytes - offset);
            auto part = read(allocation + (uint64_t)extent.start * block_size + offset, take);
            memcpy(out, part.data(), take);
            out += take;
            size -= take;
            offset = 0;
            if (!size)
                return;
        }
        check(!size, "Truncated HFS fork extent");
    }

    template <class Function> void leaves(const std::vector<unsigned char>& tree, Function visit)
    {
        check(tree.size() >= 512 && tree[8] == 1 && word(&tree[32]) == 512, "Invalid classic HFS B-tree header");
        UInt32 node = number(&tree[24]);
        std::set<UInt32> seen;
        unsigned total = 0;
        while (node) {
            check(node < tree.size() / 512 && seen.insert(node).second, "Invalid HFS leaf chain");
            const auto* p = tree.data() + (size_t)node * 512;
            unsigned count = word(p + 10);
            check(p[8] == 255 && count <= 248, "Invalid HFS leaf node");
            for (unsigned i = 0; i < count; ++i) {
                unsigned begin = word(p + 510 - i * 2);
                unsigned end = word(p + 508 - i * 2);
                check(begin >= 14 && begin < end && end <= 510 - count * 2, "Invalid HFS record bounds");
                check(++total <= 100000, "HFS catalog exceeds entry limit");
                visit(p + begin, end - begin);
            }
            node = number(p);
            if (pump_events)
                pump_events();
        }
    }

    void output(const fs::path& path, Extents e, UInt32 size, bool append)
    {
        std::ofstream out(io_path(path), std::ios::binary | (append ? std::ios::app : std::ios::trunc));
        check(bool(out), "Cannot write HFS projection");
        UInt64 done = 0;
        for (auto item : e)
            for (UInt64 at = 0, n = std::min<UInt64>((UInt64)item.count * block_size, size - done); at < n;) {
                unsigned take = (unsigned)std::min<UInt64>(65536, n - at);
                auto data = read(allocation + (UInt64)item.start * block_size + at, take);
                out.write((const char*)data.data(), take);
                check(bool(out), "Cannot write HFS fork");
                done += take;
                at += take;
                if (pump_events)
                    pump_events();
            }
        out.close();
        check(done == size && bool(out), "Incomplete HFS fork");
    }

  public:
    ClassicHfs(IInStream* stream, UInt64 start, UInt64 size) : input(stream), base(start), length(size)
    {
    }

    void snapshot(romaw_image_set& set)
    {
        auto m = read(1024, 512);
        blocks = word(&m[18]);
        block_size = number(&m[20]);
        allocation = (UInt64)word(&m[28]) * 512;
        /* Some distributed floppies omit unused trailing allocation blocks.
           Validate each referenced extent against the actual file instead of
           requiring unallocated space to be physically present. */
        check(blocks && block_size >= 512 && block_size % 512 == 0 && allocation <= length,
              "Invalid HFS allocation map");
        check(m[36] && m[36] <= 27, "Invalid HFS volume name");
        auto name = filename(&m[37], m[36]);
        auto ext = fork_bytes(extents(&m[134]), number(&m[130]));
        leaves(ext, [&](const unsigned char* p, unsigned n) {
            check(n >= 20 && p[0] == 7, "Invalid HFS overflow record");
            overflow[{number(p + 2), p[1]}][word(p + 6)] = extents(p + 8);
        });
        auto catalog = fork_bytes(chain(extents(&m[150]), 4, 0, number(&m[146])), number(&m[146]));

        struct Item {
            UInt32 parent, id;
            std::string name;
            std::vector<unsigned char> body;
        };

        std::vector<Item> items;
        leaves(catalog, [&](const unsigned char* p, unsigned n) {
            check(n >= 8 && p[0] >= 6 && p[0] <= 37 && p[6] <= 31 && p[0] >= 6 + p[6], "Invalid HFS catalog key");
            unsigned at = (p[0] + 2) & ~1u;
            check(at + 2 <= n, "Truncated HFS catalog record");
            const auto* b = p + at;
            if (b[0] != 1 && b[0] != 2)
                return;
            check(n - at >= (b[0] == 1 ? 70u : 102u), "Truncated HFS catalog entry");
            auto text = filename(p + 7, p[6]);
            items.push_back({number(p + 2), number(b + (b[0] == 1 ? 6 : 20)), text, {b, p + n}});
        });
        check(set.volumes.size() < 32, "Too many image volumes");
        auto root = set.root / ("volume-" + std::to_string(set.volumes.size()));
        image_directory(set, root);
        std::map<UInt32, std::pair<fs::path, unsigned>> directories = {{1, {root, 0}}};
        std::set<UInt32> ids;
        size_t remaining = items.size();
        for (unsigned pass = 0; remaining && pass <= 256; ++pass) {
            size_t before = remaining;
            for (auto& item : items) {
                if (item.body.empty())
                    continue;
                auto parent = directories.find(item.parent);
                if (parent == directories.end())
                    continue;
                check(ids.insert(item.id).second, "Duplicate HFS catalog identity");
                auto& b = item.body;
                bool directory = b[0] == 1;
                bool volume = directory && item.id == 2 && item.parent == 1;
                check(parent->second.second < 256, "HFS hierarchy exceeds depth limit");
                auto path = volume ? root : parent->second.first / fs::u8path(item.name);
                check(volume || !image_exists(set, path), "Colliding HFS filenames");
                if (directory) {
                    if (!volume)
                        check(image_directory(set, path), "Cannot create HFS directory");
                    directories[item.id] = {path, parent->second.second + 1};
                }
                else {
                    UInt32 data = number(&b[26]), resource = number(&b[36]);
                    budget(set, (UInt64)data + resource + 82);
                    auto owner = shared_from_this();
                    auto data_chain = chain(extents(&b[74]), item.id, 0, data);
                    image_fork(
                        set, path, data, false,
                        [owner, path, data_chain, data] { owner->output(path, data_chain, data, false); },
                        [owner, data_chain, data] { return owner->fork_bytes(data_chain, data); },
                        [owner, data_chain](uint64_t at, void* out, size_t n) {
                            owner->fork_range(data_chain, at, out, n);
                        });
                    unsigned char info[32] = {};
                    std::copy(b.begin() + 4, b.begin() + 20, info);
                    std::copy(b.begin() + 56, b.begin() + 72, info + 16);
                    std::wstring hex;
                    for (auto c : info) {
                        hex += L"0123456789ABCDEF"[c >> 4];
                        hex += L"0123456789ABCDEF"[c & 15];
                    }
                    auto meta = path.parent_path() / fs::u8path("._" + path.filename().u8string());
                    check(!fs::exists(meta), "Colliding HFS sidecar");
                    sidecar(set, meta, hex, resource);
                    auto resource_chain = chain(extents(&b[86]), item.id, 255, resource);
                    image_fork(
                        set, meta, resource, true,
                        [owner, meta, resource_chain, resource] {
                            fs::resize_file(io_path(meta), 82);
                            owner->output(meta, resource_chain, resource, true);
                        },
                        [owner, resource_chain, resource] { return owner->fork_bytes(resource_chain, resource); },
                        [owner, resource_chain](uint64_t at, void* out, size_t n) {
                            owner->fork_range(resource_chain, at, out, n);
                        });
                }
                if (directory) {
                    budget(set, 82);
                    std::wstring hex;
                    for (unsigned i = 22; i < 54; ++i) {
                        hex += L"0123456789ABCDEF"[b[i] >> 4];
                        hex += L"0123456789ABCDEF"[b[i] & 15];
                    }
                    auto meta = path.parent_path() / fs::u8path("._" + path.filename().u8string());
                    check(!fs::exists(meta), "Colliding HFS folder sidecar");
                    sidecar(set, meta, hex, 0);
                }
                item.body.clear();
                --remaining;
            }
            check(remaining < before, "Missing or cyclic HFS catalog parent");
        }
        check(!remaining && ids.count(2), "Missing HFS root directory");
        set.volumes.push_back({2, native_text(root), name});
    }
};

static bool classic_snapshot(romaw_image_set& set, IInStream* input)
{
    UInt64 size = 0;
    check(input->Seek(0, 2, &size) == S_OK, "Cannot size image");
    for (UInt64 base : {0u, 84u}) {
        unsigned char m[512];
        UInt32 got = 0;
        UInt64 available = size - base;
        if (base) {
            unsigned char header[84];
            if (size < 84 || input->Seek(0, 0, nullptr) != S_OK || input->Read(header, 84, &got) != S_OK || got != 84)
                continue;
            auto u32 = [](const unsigned char* p) {
                return (UInt32)p[0] << 24 | (UInt32)p[1] << 16 | (UInt32)p[2] << 8 | p[3];
            };
            available = u32(header + 64);
            if (header[0] > 63 || header[82] != 1 || header[83] != 0 || available + u32(header + 68) + 84 != size)
                continue;
        }
        if (size < base + 1536 || input->Seek(base + 1024, 0, nullptr) != S_OK ||
            input->Read(m, sizeof m, &got) != S_OK || got != sizeof m)
            continue;
        if (m[0] != 'B' || m[1] != 'D' || (m[124] == 'H' && m[125] == '+'))
            continue;
        std::make_shared<ClassicHfs>(input, base, available)->snapshot(set);
        auto& volume = set.volumes.back();
        volume.classic = true;
        volume.offset = base;
        volume.length = available;
        return true;
    }
    return false;
}
