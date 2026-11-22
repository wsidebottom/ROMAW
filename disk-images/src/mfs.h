/*+===================================================================
  File:      mfs.h

  Summary:   Read bounded MFS catalogs and allocation chains directly from image streams.

  Classes:   MfsImage.

  Functions: Internal image reader helpers.

  Origin:    Original adapter using the MFS layouts in SuperMario Files.a and
             HfvExplorer's src/interface_mfs.cpp as format references.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
class MfsImage : public std::enable_shared_from_this<MfsImage> {
    CMyComPtr<IInStream> input;
    uint64_t base, length, allocation = 0;
    unsigned blocks = 0, block_size = 0;
    std::vector<unsigned char> map;

    static unsigned word(const unsigned char* p)
    {
        return unsigned(p[0]) * 256 + p[1];
    }

    static uint32_t number(const unsigned char* p)
    {
        return uint32_t(word(p)) * 65536 + word(p + 2);
    }

    static std::string name(const unsigned char* p, unsigned size)
    {
        std::wstring wide;
        for (unsigned i = 0; i < size; ++i) {
            check(p[i] && p[i] != ':', "Invalid MFS filename");
            wide += p[i] < 128 ? p[i] : roman[p[i] - 128].value;
        }
        return component(wide);
    }

    std::vector<unsigned char> read(uint64_t offset, size_t size)
    {
        check(offset <= length && size <= length - offset && size <= UINT32_MAX, "MFS read outside image");
        check(input->Seek(base + offset, 0, nullptr) == S_OK, "Cannot seek MFS image");
        std::vector<unsigned char> bytes(size);
        size_t at = 0;
        while (at < size) {
            UInt32 got = 0;
            check(input->Read(bytes.data() + at, UInt32(size - at), &got) == S_OK && got && got <= size - at,
                  "Truncated MFS image");
            at += got;
        }
        return bytes;
    }

    std::vector<unsigned> chain(unsigned first, uint32_t size, uint32_t physical)
    {
        check(size <= physical && physical <= uint64_t(blocks) * block_size && physical % block_size == 0,
              "MFS fork exceeds allocation area");
        std::vector<unsigned> result;
        std::set<unsigned> seen;
        while (uint64_t(result.size()) * block_size < physical) {
            check(first >= 2 && first - 2 < blocks && seen.insert(first).second, "Invalid or cyclic MFS fork chain");
            result.push_back(first - 2);
            unsigned index = first - 2, at = index * 3 / 2;
            check(at + 1 < map.size(), "Truncated MFS allocation map");
            first = index & 1 ? ((map[at] & 15) * 256 + map[at + 1]) : (unsigned(map[at]) * 16 + map[at + 1] / 16);
        }
        check(!physical || first == 1, "MFS fork chain has an invalid terminator");
        return result;
    }

    void range(const std::vector<unsigned>& chain, uint32_t length, uint64_t offset, void* buffer, size_t size)
    {
        check(offset <= length && size <= length - offset, "MFS fork read outside bounds");
        auto* out = static_cast<unsigned char*>(buffer);
        while (size) {
            unsigned index = unsigned(offset / block_size), within = unsigned(offset % block_size);
            check(index < chain.size(), "Truncated MFS fork chain");
            size_t take = std::min<size_t>(size, block_size - within);
            auto bytes = read(allocation + uint64_t(chain[index]) * block_size + within, take);
            memcpy(out, bytes.data(), take);
            out += take;
            offset += take;
            size -= take;
        }
    }

  public:
    MfsImage(IInStream* stream, uint64_t base, uint64_t length) : input(stream), base(base), length(length)
    {
    }

    void catalog(romaw_image_set& set)
    {
        auto header = read(1024, 64);
        blocks = word(header.data() + 18);
        block_size = number(header.data() + 20);
        allocation = uint64_t(word(header.data() + 28)) * 512;
        uint64_t directory = uint64_t(word(header.data() + 14)) * 512;
        size_t directory_size = size_t(word(header.data() + 16)) * 512;
        size_t map_size = (blocks * 3 + 1) / 2;
        check(blocks && blocks <= 4094 && block_size >= 512 && block_size % 512 == 0 && allocation <= length &&
                  uint64_t(blocks) * block_size <= length - allocation && directory >= 1088 + map_size &&
                  directory <= allocation && directory_size <= allocation - directory && header[36] && header[36] <= 27,
              "Invalid MFS volume geometry");
        map = read(1088, map_size);
        auto entries = read(directory, directory_size);
        auto root = set.root / ("volume-" + std::to_string(set.volumes.size()));
        image_directory(set, root);
        unsigned count = 0;
        for (size_t at = 0; at < entries.size();) {
            if (!entries[at]) {
                at = (at / 512 + 1) * 512;
                continue;
            }
            check(entries.size() - at >= 51 && at % 512 + 51 <= 512, "Truncated MFS directory entry");
            const auto* p = entries.data() + at;
            unsigned n = p[50];
            check((p[0] & 128) && n && n <= 63 && 51 + n <= entries.size() - at && at % 512 + 51 + n <= 512,
                  "Invalid MFS directory entry");
            auto path = root / fs::u8path(name(p + 51, n));
            check(!image_exists(set, path), "Duplicate MFS filename");
            auto& item = set.items[catalog_path(path)];
            memcpy(item.info.finder_info, p + 2, 16);
            for (unsigned resource = 0; resource < 2; ++resource) {
                uint32_t size = number(p + (resource ? 34 : 24));
                auto extents = chain(word(p + (resource ? 32 : 22)), size, number(p + (resource ? 38 : 28)));
                budget(set, size);
                auto owner = shared_from_this();
                auto range_read = [owner, extents, size](uint64_t at, void* out, size_t n) {
                    owner->range(extents, size, at, out, n);
                };
                auto full_read = [range_read, size] {
                    check(size <= 128 * 1024 * 1024, "MFS fork exceeds memory read limit");
                    std::vector<unsigned char> bytes(size);
                    range_read(0, bytes.data(), size);
                    return bytes;
                };
                if (resource) {
                    item.info.resource_size = size;
                    item.resource_range = range_read;
                    item.resource = full_read;
                }
                else {
                    item.info.data_size = size;
                    item.data_range = range_read;
                    item.data = full_read;
                }
            }
            ++count;
            at += (51 + n + 1) & ~size_t(1);
        }
        check(count == word(header.data() + 12), "MFS directory count mismatch");
        set.volumes.push_back({2, native_text(root), name(header.data() + 37, header[36])});
    }
};

static bool mfs_catalog(romaw_image_set& set, IInStream* input)
{
    if (!set.direct)
        return false;
    UInt64 length = 0;
    check(input->Seek(0, 2, &length) == S_OK, "Cannot size MFS image");
    for (uint64_t base : {0u, 84u}) {
        unsigned char header[84];
        UInt32 got = 0;
        uint64_t available = length;
        if (base) {
            if (length < 84 || input->Seek(0, 0, nullptr) != S_OK || input->Read(header, 84, &got) != S_OK || got != 84)
                continue;
            auto number = [](const unsigned char* p) {
                return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
            };
            available = number(header + 64);
            if (header[0] > 63 || header[82] != 1 || header[83] || available + number(header + 68) + 84 != length)
                continue;
        }
        if (available < 1536 || input->Seek(base + 1024, 0, nullptr) != S_OK || input->Read(header, 2, &got) != S_OK ||
            got != 2 || header[0] != 0xd2 || header[1] != 0xd7)
            continue;
        std::make_shared<MfsImage>(input, base, available)->catalog(set);
        return true;
    }
    return false;
}
