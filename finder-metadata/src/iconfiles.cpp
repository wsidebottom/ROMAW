/*+===================================================================
  File:      iconfiles.cpp

  Summary:   Decode bounded ICO and ICNS files into native RGBA or classic icon families without executing content.

  Classes:   Declared: ComPointer, ComScope, Frame.

  Functions: finder_decode_icon_file, finder_parse_icon_file.

  Origin:    Original Executor icon-file adapter. Windows codec interfaces follow
             https://learn.microsoft.com/windows/win32/wic/-wic-creating-decoder.

  Copyright and Legal notices: Applicable repository license: finder-metadata/LICENSE.
===================================================================+*/
#include "finder_metadata/icons.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <initguid.h>
#include <wincodec.h>
#endif

namespace {
uint32_t big(const unsigned char* p)
{
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

uint32_t little(const unsigned char* p)
{
    return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0];
}

unsigned word(const unsigned char* p)
{
    return unsigned(p[0]) << 8 | p[1];
}

struct Frame {
    const unsigned char* bytes = nullptr;
    size_t length = 0;
    unsigned width = 0, height = 0, depth = 0, index = 0;
    bool exact = false;
};

unsigned distance(const Frame& f, unsigned size)
{
    return unsigned(std::abs(int(f.width) - int(size)) + std::abs(int(f.height) - int(size)));
}

bool better(const Frame& next, const Frame& old, unsigned size)
{
    if (!old.bytes)
        return true;
    if (next.exact != old.exact)
        return next.exact;
    if (distance(next, size) != distance(old, size))
        return distance(next, size) < distance(old, size);
    if ((next.width < size) != (old.width < size))
        return next.width >= size;
    return next.depth > old.depth;
}

#ifdef _WIN32
template <class T> struct ComPointer {
    T* pointer = nullptr;

    ~ComPointer()
    {
        if (pointer)
            pointer->Release();
    }

    ComPointer() = default;
    ComPointer(const ComPointer&) = delete;
    ComPointer& operator=(const ComPointer&) = delete;

    T* operator->() const
    {
        return pointer;
    }
};

struct ComScope {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ~ComScope()
    {
        if (SUCCEEDED(result))
            CoUninitialize();
    }
};

// WIC returns straight-alpha RGBA. No HICON premultiplication or background
// guessing is involved, and the byte stream never becomes a temporary file.
bool codec(const unsigned char* bytes, size_t length, unsigned index, unsigned width, unsigned height,
           std::vector<unsigned char>& rgba)
{
    ComScope scope;
    if (FAILED(scope.result) && scope.result != RPC_E_CHANGED_MODE)
        return false;
    ComPointer<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                reinterpret_cast<void**>(&factory.pointer))))
        return false;
    ComPointer<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream.pointer)))
        return false;
    ULONG written = 0;
    if (FAILED(stream->Write(bytes, ULONG(length), &written)) || written != length ||
        FAILED(stream->Seek({}, STREAM_SEEK_SET, nullptr)))
        return false;
    ComPointer<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.pointer, nullptr, WICDecodeMetadataCacheOnDemand,
                                                &decoder.pointer)))
        return false;
    ComPointer<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(index, &frame.pointer)))
        return false;
    UINT w = 0, h = 0;
    if (FAILED(frame->GetSize(&w, &h)) || w != width || h != height)
        return false;
    ComPointer<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter.pointer)) ||
        FAILED(converter->Initialize(frame.pointer, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeCustom)))
        return false;
    rgba.resize(size_t(width) * height * 4);
    return SUCCEEDED(converter->CopyPixels(nullptr, width * 4, UINT(rgba.size()), rgba.data()));
}
#else
bool codec(const unsigned char*, size_t, unsigned, unsigned, unsigned, std::vector<unsigned char>&)
{
    return false;
}
#endif

bool ico_frame(const unsigned char* bytes, size_t length, unsigned size, Frame& selected)
{
    if (length < 6 || little(bytes) != 0x00010000)
        return false;
    unsigned count = unsigned(bytes[4]) | unsigned(bytes[5]) << 8;
    if (!count || count > 1024 || 6 + size_t(count) * 16 > length)
        return false;
    for (unsigned i = 0; i < count; ++i) {
        const auto* entry = bytes + 6 + i * 16;
        size_t extent = little(entry + 8), offset = little(entry + 12);
        if (entry[3] || offset < 6 + count * 16 || offset > length || !extent || extent > length - offset)
            return false;
        unsigned w = entry[0] ? entry[0] : 256, h = entry[1] ? entry[1] : 256;
        unsigned depth = unsigned(entry[6]) | unsigned(entry[7]) << 8;
        const auto* data = bytes + offset;
        if (extent >= 24 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8)) {
            if (big(data + 16) != w || big(data + 20) != h)
                return false;
            depth = 32;
        }
        else {
            if (extent < 40 || little(data) < 40 || little(data) > extent || little(data + 4) != w ||
                (little(data + 8) != h * 2 && int32_t(little(data + 8)) != -int32_t(h * 2)))
                return false;
            depth = unsigned(data[14]) | unsigned(data[15]) << 8;
            if (depth != 1 && depth != 4 && depth != 8 && depth != 16 && depth != 24 && depth != 32)
                return false;
        }
        Frame candidate{data, extent, w, h, depth, i, false};
        if (better(candidate, selected, size))
            selected = candidate;
    }
    return selected.bytes != nullptr;
}

unsigned png_size(uint32_t type)
{
    switch (type) {
    case 0x69637034:
        return 16; // icp4
    case 0x69637035:
    case 0x69633131:
        return 32;
    case 0x69637036:
    case 0x69633132:
        return 64;
    case 0x69633037:
    case 0x69633133:
        return 128;
    case 0x69633038:
    case 0x69633134:
        return 256;
    case 0x69633039:
        return 512;
    case 0x69633130:
        return 1024;
    default:
        return 0;
    }
}

bool icns_frame(const unsigned char* bytes, size_t length, unsigned size, Frame& selected)
{
    if (length < 8 || memcmp(bytes, "icns", 4) || big(bytes + 4) != length)
        return false;
    for (size_t at = 8; at < length;) {
        if (length - at < 8)
            return false;
        uint32_t type = big(bytes + at), extent = big(bytes + at + 4);
        if (extent < 8 || extent > length - at)
            return false;
        const auto* data = bytes + at + 8;
        size_t payload = extent - 8;
        Frame candidate{};
        unsigned dimension = png_size(type);
        if (dimension && payload >= 24 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8)) {
            if (big(data + 16) != dimension || big(data + 20) != dimension)
                return false;
            candidate = {data, payload, dimension, dimension, 32, 0, false};
        }
        else if (type == 0x65786672) { // exfr: editable frames used by the shared resource tools.
            if (payload < 10)
                return false;
            unsigned w = word(data), h = word(data + 2), depth = word(data + 4), colors = word(data + 6);
            if (!w || !h || w > 1024 || h > 1024 || word(data + 8) != 1 ||
                (depth != 1 && depth != 4 && depth != 8 && depth != 24 && depth != 32) ||
                (depth <= 8 ? !colors || colors > (1u << depth) : colors != 0))
                return false;
            size_t expected = 10 + colors * 3 + ((size_t(w) * depth + 7) / 8) * h + ((w + 7) / 8) * h;
            if (payload != expected)
                return false;
            candidate = {data, payload, w, h, depth, 0, true};
        }
        if (candidate.bytes && better(candidate, selected, size))
            selected = candidate;
        at += extent;
    }
    return true;
}

bool exact_rgba(const Frame& frame, std::vector<unsigned char>& rgba)
{
    unsigned colors = word(frame.bytes + 6);
    const auto* palette = frame.bytes + 10;
    const auto* image = palette + colors * 3;
    size_t stride = (size_t(frame.width) * frame.depth + 7) / 8;
    const auto* mask = image + stride * frame.height;
    bool alpha = false;
    if (frame.depth == 32)
        for (size_t i = 3; i < stride * frame.height; i += 4)
            alpha |= image[i] != 0;
    rgba.resize(size_t(frame.width) * frame.height * 4);
    for (unsigned y = 0; y < frame.height; ++y)
        for (unsigned x = 0; x < frame.width; ++x) {
            auto* pixel = rgba.data() + (size_t(y) * frame.width + x) * 4;
            if (frame.depth <= 8) {
                unsigned bit = x * frame.depth;
                unsigned index =
                    (image[y * stride + bit / 8] >> (8 - frame.depth - bit % 8)) & ((1u << frame.depth) - 1);
                if (index >= colors)
                    return false;
                memcpy(pixel, palette + index * 3, 3);
                pixel[3] = 255;
            }
            else {
                const auto* source = image + y * stride + x * (frame.depth / 8);
                pixel[0] = source[2];
                pixel[1] = source[1];
                pixel[2] = source[0];
                pixel[3] = alpha ? source[3] : 255;
            }
            if (!(mask[y * ((frame.width + 7) / 8) + x / 8] & (128 >> (x % 8))))
                pixel[3] = 0;
        }
    return true;
}
}

extern "C" int finder_decode_icon_file(const unsigned char* bytes, size_t length, unsigned size, unsigned char* rgba)
{
    if (!bytes || !rgba || length < 6 || length > FINDER_ICON_FILE_LIMIT || (size != 16 && size != 32))
        return 0;
    try {
        Frame frame{};
        std::vector<unsigned char> decoded;
        if (!memcmp(bytes, "icns", 4)) {
            if (!icns_frame(bytes, length, size, frame))
                return 0;
            if (!frame.bytes) {
                FinderIcon classic{};
                if (!finder_parse_icns_icon(bytes, length, &classic))
                    return 0;
                finder_icon_rgba_size(&classic, size, rgba);
                return 1;
            }
            if (frame.exact ? !exact_rgba(frame, decoded)
                            : !codec(frame.bytes, frame.length, 0, frame.width, frame.height, decoded))
                return 0;
        }
        else {
            if (!ico_frame(bytes, length, size, frame))
                return 0;
            // WIC's frame ordering can differ from the ICO directory ordering.
            // Give it only the chosen entry so 16px artwork stays distinct
            // from 32px artwork and lower-depth frames cannot win by accident.
            std::vector<unsigned char> single(22 + frame.length);
            single[2] = single[4] = 1;
            memcpy(single.data() + 6, bytes + 6 + frame.index * 16, 16);
            single[18] = 22;
            single[19] = single[20] = single[21] = 0;
            memcpy(single.data() + 22, frame.bytes, frame.length);
            if (!codec(single.data(), single.size(), 0, frame.width, frame.height, decoded))
                return 0;
        }
        for (unsigned y = 0; y < size; ++y)
            for (unsigned x = 0; x < size; ++x)
                memcpy(rgba + (y * size + x) * 4,
                       decoded.data() + ((size_t(y) * frame.height / size) * frame.width + x * frame.width / size) * 4,
                       4);
        return 1;
    }
    catch (...) {
        return 0;
    }
}

extern "C" int finder_parse_icon_file(const unsigned char* bytes, size_t length, FinderIcon* icon)
{
    if (!bytes || !icon || length > FINDER_ICON_FILE_LIMIT)
        return 0;
    // Keep original classic palette indices, which also carry label roles.
    FinderIcon result{};
    if (finder_parse_icns_icon(bytes, length, &result)) {
        *icon = result;
        return 1;
    }
    std::array<unsigned char, 4096> large{};
    std::array<unsigned char, 1024> small{};
    if (!finder_decode_icon_file(bytes, length, 32, large.data()) ||
        !finder_decode_icon_file(bytes, length, 16, small.data()))
        return 0;
    return finder_icon_from_rgba(large.data(), small.data(), icon);
}
