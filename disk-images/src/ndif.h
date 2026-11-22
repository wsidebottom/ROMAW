/*+===================================================================
  File:      ndif.h

  Summary:   Declares ndif support for disk images package support.

  Classes:   Declared: Chunk, NdifInput; Used: Chunk, NdifInput, std::ifstream, std::vector

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    NDIF layout reference: https://github.com/mhjacobson/ndif2raw

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* NDIF layout reference: https://github.com/mhjacobson/ndif2raw
   Decode bounded chunks on demand; never write or expand the source image. */
static uint32_t ndif_number(const unsigned char* p)
{
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

static unsigned ndif_word(const unsigned char* p)
{
    return unsigned(p[0]) << 8 | p[1];
}

static bool ndif_extent(size_t size, uint64_t at, uint64_t count)
{
    return at <= size && count <= size - at;
}

static std::vector<unsigned char> ndif_resource(const fs::path& path)
{
    const auto name = path.filename().u8string();
    for (const auto& candidate :
         {path.parent_path() / fs::u8path("._" + name), path.parent_path() / fs::u8path("%" + name),
          path.parent_path() / ".AppleDouble" / path.filename(), path.parent_path() / ".rsrc" / path.filename(),
          path / "..namedfork" / "rsrc"}) {
        std::ifstream file(io_path(candidate), std::ios::binary | std::ios::ate);
        if (!file)
            continue;
        auto length = file.tellg();
        check(length >= 0 && length <= 128 * 1024 * 1024, "NDIF resource fork exceeds memory limit");
        std::vector<unsigned char> bytes((size_t)length);
        file.seekg(0);
        check(bool(file.read((char*)bytes.data(), length)), "Truncated NDIF resource fork");
        auto* r = bytes.data();
        size_t size = bytes.size();
        if (size >= 26 && ndif_number(r) == 0x00051607) {
            unsigned entries = ndif_word(r + 24);
            check(ndif_extent(size, 26, uint64_t(entries) * 12), "Invalid NDIF AppleDouble table");
            size_t offset = 0, count = 0;
            for (unsigned i = 0; i < entries; ++i)
                if (ndif_number(r + 26 + i * 12) == 2) {
                    offset = ndif_number(r + 30 + i * 12);
                    count = ndif_number(r + 34 + i * 12);
                    break;
                }
            check(ndif_extent(size, offset, count), "Invalid NDIF resource extent");
            r += offset;
            size = count;
        }
        if (size < 16)
            continue;
        size_t data = ndif_number(r), map = ndif_number(r + 4);
        size_t data_size = ndif_number(r + 8), map_size = ndif_number(r + 12);
        check(ndif_extent(size, data, data_size) && ndif_extent(size, map, map_size) && map_size >= 28,
              "Invalid NDIF resource map");
        size_t types = ndif_word(r + map + 24);
        check(ndif_extent(map_size, types, 2), "Invalid NDIF type table");
        types += map;
        unsigned count = ndif_word(r + types);
        if (count == 0xffff)
            continue;
        ++count;
        check(ndif_extent(map + map_size, types + 2, uint64_t(count) * 8), "Truncated NDIF type table");
        for (unsigned i = 0; i < count; ++i) {
            const auto* type = r + types + 2 + i * 8;
            if (ndif_number(type) != 0x6263656d)
                continue;
            unsigned refs = ndif_word(type + 4) + 1;
            size_t at = types + ndif_word(type + 6);
            check(ndif_extent(map + map_size, at, uint64_t(refs) * 12), "Truncated NDIF reference table");
            for (unsigned j = 0; j < refs; ++j) {
                const auto* ref = r + at + j * 12;
                if (ndif_word(ref) != 128)
                    continue;
                size_t offset = ndif_number(ref + 4) & 0xffffff;
                check(ndif_extent(data_size, offset, 4), "Invalid NDIF header offset");
                size_t n = ndif_number(r + data + offset);
                check(ndif_extent(data_size, offset + 4, n), "Truncated NDIF header");
                return {r + data + offset + 4, r + data + offset + 4 + n};
            }
        }
    }
    return {};
}

class NdifInput final : public IInStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IInStream)

    struct Chunk {
        uint64_t start, end, offset;
        uint32_t packed;
        unsigned type;
    };

    CMyComPtr<IInStream> input;
    std::vector<Chunk> chunks;
    std::vector<unsigned char> decoded;
    size_t cached = SIZE_MAX;
    uint64_t size, position = 0;

    void load(size_t index)
    {
        const auto& chunk = chunks[index];
        check(chunk.packed <= 128 * 1024 * 1024 && chunk.end - chunk.start <= 128 * 1024 * 1024,
              "NDIF chunk exceeds memory limit");
        std::vector<unsigned char> packed(chunk.packed);
        UInt32 got = 0;
        check(input->Seek(chunk.offset, 0, nullptr) == S_OK && input->Read(packed.data(), chunk.packed, &got) == S_OK &&
                  got == chunk.packed,
              "Truncated NDIF chunk");
        decoded.assign(size_t(chunk.end - chunk.start), 0);
        size_t in = 0, out = 0;
        while (in < packed.size()) {
            unsigned command = packed[in++];
            size_t count, distance = 0;
            if (command & 128) {
                count = (command & 127) + 1;
                check(count <= packed.size() - in && count <= decoded.size() - out, "Invalid NDIF ADC literal");
                memcpy(decoded.data() + out, packed.data() + in, count);
                in += count;
                out += count;
                continue;
            }
            if (command & 64) {
                check(packed.size() - in >= 2, "Truncated NDIF ADC reference");
                count = (command & 63) + 4;
                distance = ndif_word(packed.data() + in) + 1;
                in += 2;
            }
            else {
                check(in < packed.size(), "Truncated NDIF ADC reference");
                count = (command >> 2) + 3;
                distance = ((command & 3) << 8 | packed[in++]) + 1;
            }
            check(distance <= out && count <= decoded.size() - out, "Invalid NDIF ADC reference");
            while (count--) {
                decoded[out] = decoded[out - distance];
                ++out;
            }
        }
        check(out == decoded.size(), "Short NDIF ADC output");
        cached = index;
    }

  public:
    bool writable_identity = false;

    NdifInput(IInStream* stream, const std::vector<unsigned char>& header) : input(stream)
    {
        check(header.size() >= 128, "Truncated NDIF header");
        auto* h = header.data();
        unsigned version = ndif_word(h);
        check(version >= 10 && version <= 12 && !ndif_number(h + 84), "Unsupported or segmented NDIF image");
        size = uint64_t(ndif_number(h + 68)) * 512;
        check(size >= 1536 && size <= INT32_MAX, "NDIF volume exceeds classic size limit");
        uint64_t backing = ndif_number(h + 76), physical = 0;
        check(input->Seek(0, 2, &physical) == S_OK, "Cannot size NDIF data");
        unsigned count = ndif_number(h + 124);
        check(count >= 2 && count <= 1000000 && ndif_extent(header.size(), 128, uint64_t(count) * 12),
              "Invalid NDIF chunk count");
        for (unsigned i = 0; i < count; ++i) {
            auto* c = h + 128 + i * 12;
            uint64_t start = uint64_t(ndif_number(c) >> 8) * 512;
            unsigned type = c[3];
            check(i ? start > chunks.back().start : start == 0, "Unordered NDIF chunks");
            check(start <= size, "NDIF chunk outside volume");
            if (i)
                chunks.back().end = start;
            if (type == 255) {
                check(i + 1 == count && start == size, "Invalid NDIF terminator");
                writable_identity = chunks.size() == 1 && chunks[0].type == 2 && chunks[0].offset == 0 &&
                                    chunks[0].packed == size && !ndif_number(h + 80);
                return;
            }
            check(type == 0 || type == 2 || type == 131,
                  "Unsupported NDIF compression (KenCode images are not yet supported)");
            uint64_t offset = backing + ndif_number(c + 4);
            uint32_t packed = ndif_number(c + 8);
            check(offset <= physical && packed <= physical - offset, "NDIF chunk outside data fork");
            chunks.push_back({start, size, offset, packed, type});
        }
        check(false, "Missing NDIF terminator");
    }

    STDMETHOD(Read)(void* buffer, UInt32 count, UInt32* done) noexcept override
    {
        if (done)
            *done = 0;
        try {
            auto* out = static_cast<unsigned char*>(buffer);
            uint64_t remaining = std::min<uint64_t>(count, size - position);
            while (remaining) {
                auto it = std::upper_bound(chunks.begin(), chunks.end(), position,
                                           [](uint64_t n, const Chunk& c) { return n < c.start; });
                --it;
                size_t take = size_t(std::min<uint64_t>(remaining, it->end - position));
                if (it->type == 0)
                    memset(out, 0, take);
                else if (it->type == 2) {
                    uint64_t skip = position - it->start;
                    UInt32 got = 0;
                    check(skip <= it->packed && take <= it->packed - skip &&
                              input->Seek(it->offset + skip, 0, nullptr) == S_OK &&
                              input->Read(out, UInt32(take), &got) == S_OK && got == take,
                          "Short raw NDIF chunk");
                }
                else {
                    size_t index = size_t(it - chunks.begin());
                    if (cached != index)
                        load(index);
                    memcpy(out, decoded.data() + size_t(position - it->start), take);
                }
                out += take;
                position += take;
                remaining -= take;
                if (done)
                    *done += UInt32(take);
            }
            return S_OK;
        }
        catch (...) {
            return E_FAIL;
        }
    }

    STDMETHOD(Seek)(Int64 offset, UInt32 origin, UInt64* out) noexcept override
    {
        if (origin > 2)
            return E_INVALIDARG;
        uint64_t base = origin == 0 ? 0 : origin == 1 ? position : size;
        if (offset < 0 ? uint64_t(-(offset + 1)) + 1 > base : uint64_t(offset) > size - base)
            return E_INVALIDARG;
        position = base + offset;
        if (out)
            *out = position;
        return S_OK;
    }
};
