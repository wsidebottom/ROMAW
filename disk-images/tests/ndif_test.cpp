/*+===================================================================
  File:      ndif_test.cpp

  Summary:   Implements ndif test support for disk images package support.

  Classes:   Declared: Memory; Used: Memory, NdifInput, std::exception, std::runtime_error, std::vector

  Functions: Memory::Memory, Memory::STDMETHOD, main

  Origin:    Existing repository source in packages/disk-images/tests; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "Common/MyInitGuid.h"
#include "Common/MyCom.h"
#include "7zip/IStream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace fs = std::filesystem;

static void check(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

static fs::path io_path(const fs::path& path)
{
    return path;
}

#include "../src/ndif.h"

class Memory final : public IInStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IInStream)
  public:
    std::vector<unsigned char> bytes;
    uint64_t pos = 0;

    explicit Memory(std::vector<unsigned char> data) : bytes(std::move(data))
    {
    }

    STDMETHOD(Read)(void* out, UInt32 n, UInt32* done) noexcept override
    {
        n = UInt32(std::min<uint64_t>(n, bytes.size() - pos));
        if (n)
            memcpy(out, bytes.data() + pos, n);
        pos += n;
        if (done)
            *done = n;
        return S_OK;
    }

    STDMETHOD(Seek)(Int64 n, UInt32 origin, UInt64* out) noexcept override
    {
        uint64_t base = origin == 0 ? 0 : origin == 1 ? pos : bytes.size();
        if (origin > 2 || (n < 0 ? uint64_t(-(n + 1)) + 1 > base : uint64_t(n) > bytes.size() - base))
            return E_INVALIDARG;
        pos = base + n;
        if (out)
            *out = pos;
        return S_OK;
    }
};

static void put(std::vector<unsigned char>& b, size_t at, uint32_t n)
{
    for (unsigned i = 0; i < 4; ++i)
        b[at + i] = n >> (24 - i * 8);
}

static std::vector<unsigned char> header(unsigned type, unsigned packed)
{
    std::vector<unsigned char> h(152);
    h[1] = 11;
    put(h, 68, 3);
    put(h, 72, 3);
    put(h, 124, 2);
    h[131] = type;
    put(h, 136, packed);
    put(h, 140, 3 << 8 | 255);
    return h;
}

static bool decode(const std::vector<unsigned char>& h, const std::vector<unsigned char>& data,
                   std::vector<unsigned char>& out)
{
    try {
        CMyComPtr<IInStream> source = new Memory(data);
        CMyComPtr<IInStream> input = new NdifInput(source, h);
        UInt32 n = 0;
        out.assign(1536, 0xa5);
        return input->Read(out.data(), 1536, &n) == S_OK && n == 1536;
    }
    catch (const std::exception&) {
        return false;
    }
}

int main()
{
    std::vector<unsigned char> out;
    std::vector<unsigned char> raw(1536);
    for (size_t i = 0; i < raw.size(); ++i)
        raw[i] = i;
    assert(decode(header(2, raw.size()), raw, out) && out == raw);
    {
        CMyComPtr<IInStream> source = new Memory(raw);
        auto h = header(2, raw.size());
        auto* mapped = new NdifInput(source, h);
        CMyComPtr<IInStream> keep = mapped;
        assert(mapped->writable_identity);
        put(h, 80, 123); /* A raw image with a stored CRC needs CRC-aware writes. */
        auto* protected_map = new NdifInput(source, h);
        CMyComPtr<IInStream> protected_keep = protected_map;
        assert(!protected_map->writable_identity);
    }
    assert(decode(header(0, 0), {}, out) && out == std::vector<unsigned char>(1536));
    std::vector<unsigned char> adc;
    for (unsigned i = 0; i < 12; ++i) {
        adc.push_back(255);
        adc.insert(adc.end(), raw.begin() + i * 128, raw.begin() + (i + 1) * 128);
    }
    assert(decode(header(131, adc.size()), adc, out) && out == raw);
    // One literal followed by overlapping references, both ADC distance widths.
    adc = {128, 'Q', 0, 0}; // 1 literal + 3 repeated bytes.
    for (unsigned i = 0; i < 22; ++i) {
        adc.push_back(127);
        adc.push_back(0);
        adc.push_back(0);
    } // 22*67
    adc.insert(adc.end(), {118, 0, 0}); // 58-byte reference = 1536 total.
    assert(decode(header(131, adc.size()), adc, out) && out == std::vector<unsigned char>(1536, 'Q'));
    for (size_t n = 0; n < 128; ++n) {
        auto h = header(2, 1536);
        h.resize(n);
        assert(!decode(h, raw, out));
    }
    auto bad = header(2, 1536);
    put(bad, 124, 0xffffffff);
    assert(!decode(bad, raw, out));
    bad = header(2, 1536);
    put(bad, 132, 0xffffffff);
    assert(!decode(bad, raw, out));
    bad = header(2, 1536);
    put(bad, 140, 2 << 8 | 255);
    assert(!decode(bad, raw, out));
    bad = header(2, 1536);
    put(bad, 84, 1);
    assert(!decode(bad, raw, out));
    for (auto bytes : std::vector<std::vector<unsigned char>>{{0}, {64, 0}, {0, 0}, {255, 1}, {128, 'x'}})
        assert(!decode(header(131, bytes.size()), bytes, out));
    assert(!decode(header(128, 0), {}, out));
    puts("NDIF raw, zero, ADC literals/overlap, truncations and bounds passed");
}
