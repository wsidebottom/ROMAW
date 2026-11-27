/* Preserve original application icon representations in Windows shortcuts. */
#include "finder/shortcut_icon.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <tuple>

namespace finder {
namespace {
constexpr uint32_t code(const char (&name)[5])
{
    return uint32_t(name[0]) << 24 | uint32_t(name[1]) << 16 | uint32_t(name[2]) << 8 | uint32_t(name[3]);
}

unsigned le16(const unsigned char* p)
{
    return p[0] | unsigned(p[1]) << 8;
}

uint32_t be32(const unsigned char* p)
{
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

void store(Bytes& output, size_t at, unsigned value, unsigned count)
{
    for (unsigned byte = 0; byte < count; ++byte)
        output[at + byte] = value >> (8 * byte);
}

struct Image {
    unsigned width, height, depth, colors = 0, planes = 1;
    Bytes payload;
};

bool save(const std::vector<Image>& images, const fs::path& path)
{
    if (images.empty() || images.size() > UINT16_MAX)
        return false;
    Bytes bytes(6 + 16 * images.size());
    bytes[2] = 1;
    store(bytes, 4, unsigned(images.size()), 2);
    for (size_t i = 0; i < images.size(); ++i) {
        const auto& image = images[i];
        if (image.payload.size() > FINDER_ICON_FILE_LIMIT - bytes.size())
            return false;
        size_t at = 6 + i * 16;
        bytes[at] = image.width; // Zero represents the ICO limit of 256.
        bytes[at + 1] = image.height;
        bytes[at + 2] = image.colors;
        store(bytes, at + 4, image.planes, 2);
        store(bytes, at + 6, image.depth, 2);
        store(bytes, at + 8, unsigned(image.payload.size()), 4);
        store(bytes, at + 12, unsigned(bytes.size()), 4);
        bytes.insert(bytes.end(), image.payload.begin(), image.payload.end());
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    bool ok = WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size();
    ok = CloseHandle(file) && ok;
    if (!ok)
        DeleteFileW(path.c_str());
    return ok;
}

bool save_mac_icons(std::vector<Image>& images, const fs::path& path)
{
    std::stable_sort(images.begin(), images.end(), [](const Image& left, const Image& right) {
        return std::tie(left.width, left.height, left.depth) < std::tie(right.width, right.height, right.depth);
    });
    return save(images, path);
}

// ICO DIBs store bottom-up color rows and a separate, inverted 1-bit mask.
void add_bitmap(std::vector<Image>& images, unsigned width, unsigned height, unsigned depth,
                const unsigned char* pixels, const unsigned char* mask, const unsigned char* palette = nullptr,
                unsigned palette_size = 0)
{
    if (std::any_of(images.begin(), images.end(), [&](const auto& frame) {
            return frame.width == width && frame.height == height && frame.depth == depth;
        }))
        return;
    const unsigned colors = depth <= 8 ? 1u << depth : 0;
    const size_t pixel_stride = ((width * depth + 31) / 32) * 4, source_stride = (width * depth + 7) / 8;
    const size_t mask_stride = ((width + 31) / 32) * 4, source_mask_stride = (width + 7) / 8;
    BITMAPINFOHEADER dib{};
    dib.biSize = sizeof dib;
    dib.biWidth = width;
    dib.biHeight = 2 * height;
    dib.biPlanes = 1;
    dib.biBitCount = depth;
    Image frame{width, height, depth, colors == 256 ? 0 : colors, 1,
                Bytes(sizeof dib + 4 * colors + (pixel_stride + mask_stride) * height)};
    memcpy(frame.payload.data(), &dib, sizeof dib);
    auto* output = frame.payload.data() + sizeof dib;
    // Keep the Macintosh white-at-zero palette and pixel indices together.
    // Only the separate opacity plane is inverted for the ICO AND mask.
    for (unsigned index = 0; index < colors; ++index) {
        unsigned char rgb[3]{};
        if (!palette)
            finder_icon_palette_rgb(depth, index, rgb);
        else if (index < palette_size)
            memcpy(rgb, palette + index * 3, sizeof rgb);
        output[index * 4] = rgb[2];
        output[index * 4 + 1] = rgb[1];
        output[index * 4 + 2] = rgb[0];
    }
    output += 4 * colors;
    auto* and_bits = output + pixel_stride * height;
    bool has_alpha = false;
    if (depth == 32)
        for (size_t i = 3; i < source_stride * height; i += 4)
            has_alpha |= pixels[i] != 0;
    for (unsigned y = 0; y < height; ++y) {
        unsigned row = height - 1 - y;
        auto* image_row = output + y * pixel_stride;
        memcpy(image_row, pixels + row * source_stride, source_stride);
        for (unsigned x = 0; x < width; ++x) {
            bool opaque = !mask || (mask[row * source_mask_stride + x / 8] & (0x80 >> (x % 8)));
            if (depth == 32) {
                if (!opaque || (!has_alpha && mask))
                    image_row[x * 4 + 3] = opaque ? 255 : 0;
                opaque = image_row[x * 4 + 3] != 0;
            }
            if (!opaque) {
                and_bits[y * mask_stride + x / 8] |= 0x80 >> (x % 8);
                // Clear the BGRA/BGR value under a fully transparent pixel.
                if (depth >= 24)
                    memset(image_row + x * (depth / 8), 0, depth / 8);
            }
        }
    }
    images.push_back(std::move(frame));
}

struct ClassicSize {
    unsigned width, height;
    uint32_t mono, four, eight, rgb, alpha;
};

constexpr ClassicSize sizes[] = {
    {16, 12, code("icm#"), code("icm4"), code("icm8"), 0, 0},
    {16, 16, code("ics#"), code("ics4"), code("ics8"), code("is32"), code("s8mk")},
    {32, 32, code("ICN#"), code("icl4"), code("icl8"), code("il32"), code("l8mk")},
    {48, 48, code("ich#"), code("ich4"), code("ich8"), code("ih32"), code("h8mk")},
    {128, 128, 0, 0, 0, code("it32"), code("t8mk")},
};

// Old 32-bit ICNS chunks carry three planar PackBits-style RGB channels
// and an independent alpha chunk. Some carry uncompressed xRGB instead.
Bytes color_pixels(const Bytes& rgb, unsigned pixels, bool thumbnail)
{
    if (rgb.empty())
        return {};
    Bytes output(size_t(pixels) * 4, 255);
    if (rgb.size() == output.size()) {
        for (unsigned i = 0; i < pixels; ++i)
            for (unsigned channel = 0; channel < 3; ++channel)
                output[i * 4 + channel] = rgb[i * 4 + 3 - channel];
        return output;
    }
    size_t input = thumbnail ? 4 : 0;
    for (int channel = 2; channel >= 0; --channel) {
        for (unsigned i = 0; i < pixels;) {
            if (input >= rgb.size())
                return {};
            unsigned control = rgb[input++];
            unsigned count = control < 128 ? control + 1 : control - 125;
            if (count > pixels - i || (control < 128 ? count : 1) > rgb.size() - input)
                return {};
            for (unsigned j = 0; j < count; ++j)
                output[(i + j) * 4 + channel] = rgb[input + (control < 128 ? j : 0)];
            input += control < 128 ? count : 1;
            i += count;
        }
    }
    return output;
}

template <typename Part>
void add_classic(std::vector<Image>& images, Part part)
{
    for (const auto& size : sizes) {
        unsigned pixels = size.width * size.height;
        auto mono = part(size.mono);
        const auto* mask = mono.size() == 2 * pixels / 8 ? mono.data() + pixels / 8 : nullptr;
        if (mask)
            add_bitmap(images, size.width, size.height, 1, mono.data(), mask);
        auto four = part(size.four);
        if (four.size() == pixels / 2)
            add_bitmap(images, size.width, size.height, 4, four.data(), mask);
        auto eight = part(size.eight);
        if (eight.size() == pixels)
            add_bitmap(images, size.width, size.height, 8, eight.data(), mask);
        auto rgb = color_pixels(part(size.rgb), pixels, size.width == 128);
        if (!rgb.empty()) {
            auto alpha = part(size.alpha);
            if (alpha.size() == pixels)
                for (unsigned i = 0; i < pixels; ++i)
                    rgb[i * 4 + 3] = alpha[i];
            add_bitmap(images, size.width, size.height, 32, rgb.data(), alpha.size() == pixels ? nullptr : mask);
        }
    }
}

void add_icns(std::vector<Image>& images, const Bytes& bytes)
{
    if (bytes.size() < 8 || be32(bytes.data()) != code("icns") || be32(bytes.data() + 4) != bytes.size())
        return;
    struct Chunk {
        uint32_t type;
        const unsigned char* data;
        size_t length;
    };
    std::vector<Chunk> chunks;
    for (size_t at = 8; at < bytes.size();) {
        if (chunks.size() >= 4096 || bytes.size() - at < 8)
            return;
        const auto* start = bytes.data() + at;
        size_t length = be32(start + 4);
        if (length < 8 || length > bytes.size() - at)
            return;
        chunks.push_back({be32(start), start + 8, length - 8});
        at += length;
    }
    for (const auto& chunk : chunks) {
        auto* data = chunk.data;
        if (chunk.length >= 33 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8) && !memcmp(data + 12, "IHDR", 4)) {
            unsigned width = be32(data + 16), height = be32(data + 20);
            if (width && height && width <= 256 && height <= 256)
                images.push_back({width, height, 32, 0, 1, Bytes(data, data + chunk.length)});
        }
        else if (chunk.type == code("exfr") && chunk.length >= 10) {
            auto word = [](const unsigned char* p) { return unsigned(p[0]) << 8 | p[1]; };
            unsigned width = word(data), height = word(data + 2), depth = word(data + 4), colors = word(data + 6);
            if (!width || !height || width > 256 || height > 256 || word(data + 8) != 1 ||
                (depth != 1 && depth != 4 && depth != 8 && depth != 24 && depth != 32) ||
                (depth <= 8 ? !colors || colors > (1u << depth) : colors != 0))
                continue;
            size_t image_bytes = size_t((width * depth + 7) / 8) * height;
            if (chunk.length != 10 + colors * 3 + image_bytes + ((width + 7) / 8) * height)
                continue;
            const auto* pixel = data + 10 + colors * 3;
            add_bitmap(images, width, height, depth, pixel, pixel + image_bytes, data + 10, colors);
        }
    }
    add_classic(images, [&](uint32_t type) -> Bytes {
        auto found = std::find_if(chunks.begin(), chunks.end(), [&](const auto& chunk) { return chunk.type == type; });
        return !type || found == chunks.end() ? Bytes{} : Bytes(found->data, found->data + found->length);
    });
}

void add_mac_resources(std::vector<Image>& images, const MacFile& application, int id)
{
    add_icns(images, mac_resource(application, code("icns"), id));
    add_classic(images, [&](uint32_t type) { return type ? mac_resource(application, type, id) : Bytes{}; });
}

BOOL CALLBACK capture_group(HMODULE module, LPCWSTR, LPWSTR name, LONG_PTR data)
{
    auto& images = *reinterpret_cast<std::vector<Image>*>(data);
    HRSRC group = FindResourceW(module, name, RT_GROUP_ICON);
    size_t size = group ? SizeofResource(module, group) : 0;
    const auto* directory = group ? static_cast<const unsigned char*>(LockResource(LoadResource(module, group)))
                                  : nullptr;
    if (!directory || size < 6 || le16(directory) || le16(directory + 2) != 1)
        return TRUE;
    unsigned count = le16(directory + 4);
    if (!count || count > (size - 6) / 14)
        return TRUE;
    // The default Windows icon is the first icon group. Copy that group's
    // actual payloads, including PNG and indexed DIBs, without extracting HICONs.
    size_t total = 6 + size_t(count) * 16;
    for (unsigned i = 0; i < count; ++i) {
        const auto* entry = directory + 6 + i * 14;
        auto resource = FindResourceW(module, MAKEINTRESOURCEW(le16(entry + 12)), RT_ICON);
        size_t length = resource ? SizeofResource(module, resource) : 0;
        const auto* bytes = resource ? static_cast<const unsigned char*>(LockResource(LoadResource(module, resource)))
                                     : nullptr;
        if (bytes && length && length <= FINDER_ICON_FILE_LIMIT - total) {
            images.push_back({entry[0] ? entry[0] : 256u, entry[1] ? entry[1] : 256u, le16(entry + 6),
                              entry[2], le16(entry + 4), Bytes(bytes, bytes + length)});
            total += length;
        }
    }
    return images.empty() ? TRUE : FALSE;
}
}

bool save_application_icon(const MacFile& application, const fs::path& path)
{
    std::vector<Image> images;
    if (application.flags & 0x400)
        add_mac_resources(images, application, -16455);
    int id = 0;
    if (images.empty() &&
        finder_icon_resource_id(application.fork.data(), application.fork.size(), application.creator,
                                application.type, &id))
        add_mac_resources(images, application, id);
    return save_mac_icons(images, path);
}

bool save_application_icon(const FinderIcon& family, const fs::path& path)
{
    const unsigned char* planes[] = {family.mono, family.small, family.color4, family.color8, family.small4, family.small8};
    std::vector<Image> images;
    add_classic(images, [&](uint32_t type) -> Bytes {
        for (unsigned i = 0; i < 6; ++i)
            if (finder_icon_types[i] == type && (family.present & (1u << i)))
                return Bytes(planes[i], planes[i] + finder_icon_sizes[i]);
        return {};
    });
    return save_mac_icons(images, path);
}

bool save_native_application_icon(const fs::path& application, const fs::path& path)
{
    auto module = LoadLibraryExW(application.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module)
        return false;
    std::vector<Image> images;
    // Allocations during enumeration must not unwind through the Win32 callback.
    auto collect = [](HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR context) -> BOOL {
        try {
            return capture_group(module, type, name, context);
        }
        catch (...) {
            reinterpret_cast<std::vector<Image>*>(context)->clear();
            return FALSE;
        }
    };
    EnumResourceNamesW(module, RT_GROUP_ICON, collect, reinterpret_cast<LONG_PTR>(&images));
    FreeLibrary(module);
    return save(images, path);
}

}
