/* Draw Finder font samples from classic strike and outline resources. */
#include "finder/font_preview.h"
#include "finder/classic.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <dwrite_3.h>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace finder {
namespace {
constexpr uint32_t fond = 0x464f4e44, font = 0x464f4e54, nfnt = 0x4e464e54, sfnt = 0x73666e74;
constexpr wchar_t window_class[] = L"FinderFontPreview";
constexpr std::string_view sample = "How razorback-jumping frogs can level six piqued gymnasts!";
constexpr int panel_width = 228, text_width = panel_width - 10;

uint16_t word(const Bytes& bytes, size_t offset)
{
    return uint16_t(bytes[offset] << 8 | bytes[offset + 1]);
}

const SuitcaseResource* resource(const SuitcaseFork& fork, ResourceKey key)
{
    auto found = std::find_if(fork.resources.begin(), fork.resources.end(), [&](const auto& entry) {
        return entry.key == key;
    });
    return found == fork.resources.end() ? nullptr : &*found;
}

struct Panel {
    std::wstring label;
    int height;
    std::vector<uint32_t> pixels;

    Panel(std::wstring title, int h) : label(std::move(title)), height(h), pixels(size_t(panel_width) * h)
    {
        for (int x = 0; x < panel_width; x += 2) {
            put(x, 0);
            put(x, height - 1);
        }
        for (int y = 0; y < height; y += 2) {
            put(0, y);
            put(panel_width - 1, y);
        }
    }

    void put(int x, int y)
    {
        if (x >= 0 && x < panel_width && y >= 0 && y < height)
            pixels[size_t(y) * panel_width + x] = 0xff000000;
    }
};

// Wrap using the sampled face's own advances. A narrow font therefore fits
// more characters on each line than a wide font at the same point size.
template <typename DrawLine>
void sample_lines(const std::array<float, sample.size()>& advances, int line_height, DrawLine draw)
{
    int top = 7;
    for (size_t first = 0, next = 0; first < sample.size(); first = next, top += line_height) {
        size_t last_break = first;
        float width = 0;
        next = first;
        while (next < sample.size() && (next == first || width + advances[next] <= text_width)) {
            width += advances[next];
            if (sample[next] == ' ' || sample[next] == '-')
                last_break = next + 1;
            ++next;
        }
        if (next != sample.size() && last_break > first)
            next = last_break;
        auto end = next;
        while (end > first && sample[end - 1] == ' ')
            --end;
        draw(first, end, top);
        while (next < sample.size() && sample[next] == ' ')
            ++next;
    }
}

// FONT and NFNT use a shared one-bit strike followed by the glyph location
// and offset/width tables. Read fields in disk order, even on Win64.
struct Strike {
    const Bytes& bytes;
    unsigned first = 0, count = 0, height = 0, row_bytes = 0, leading = 0;
    size_t locations = 0, widths = 0;
    int kern = 0;

    explicit Strike(const Bytes& data) : bytes(data)
    {
        if (data.size() < 26)
            return;
        first = word(data, 2);
        unsigned last = word(data, 4);
        height = word(data, 14);
        row_bytes = word(data, 24) * 2;
        leading = word(data, 22);
        kern = int16_t(word(data, 10));
        if (first > last || last > 255 || !height || height > 256 || !row_bytes || leading > 256)
            return;
        locations = 26 + size_t(height) * row_bytes;
        // owTLoc is normally relative to its own field. Some large strikes
        // use the word immediately following the location table instead.
        widths = 16 + 2 * size_t(word(data, 16));
        size_t after_locations = locations + 2 * (last - first + 3);
        if (widths < after_locations)
            widths = after_locations;
        if (widths + 2 * (last - first + 2) > data.size() || after_locations > data.size())
            return;
        unsigned previous = 0;
        for (size_t at = locations; at < after_locations; at += 2) {
            unsigned column = word(data, at);
            if (column < previous || column > row_bytes * 8)
                return;
            previous = column;
        }
        count = last - first + 1;
    }

    unsigned index(unsigned character) const
    {
        return character >= first && character - first < count && word(bytes, widths + 2 * (character - first)) != 0xffff
                   ? character - first
                   : count;
    }

    unsigned column(unsigned glyph) const { return word(bytes, locations + 2 * glyph); }
    unsigned advance(unsigned glyph) const
    {
        return glyph == count ? column(glyph + 1) - column(glyph) : word(bytes, widths + glyph * 2) & 0xff;
    }
};

bool render_bitmap(Panel& panel, const Bytes& bytes, float scale)
{
    const Strike strike(bytes);
    if (!strike.count)
        return false;
    std::array<float, sample.size()> advances{};
    for (size_t i = 0; i < sample.size(); ++i)
        advances[i] = strike.advance(strike.index(sample[i])) * scale;
    int line_height = std::max(1, int(std::round((strike.height + strike.leading) * scale)));
    sample_lines(advances, line_height, [&](size_t first, size_t end, int top) {
        float pen = 7;
        for (size_t i = first; i < end; ++i) {
            unsigned glyph = strike.index(sample[i]);
            unsigned column = strike.column(glyph), last = strike.column(glyph + 1);
            int width = last == column ? 0 : std::max(1, int(std::round((last - column) * scale)));
            int bearing = glyph == strike.count ? 0 : int8_t(strike.bytes[strike.widths + glyph * 2]) + strike.kern;
            int x = int(std::round(pen + bearing * scale));
            int height = std::min(int(std::round(strike.height * scale)), panel.height - 2 - top);
            for (int y = 0; y < height; ++y) {
                size_t source_row = 26 + std::min<unsigned>(strike.height - 1, unsigned(y / scale)) * strike.row_bytes;
                for (int dx = std::max(0, 2 - x); dx < std::min(width, panel_width - 2 - x); ++dx) {
                    unsigned bit = std::min(last - 1, column + unsigned(dx / scale));
                    if (strike.bytes[source_row + bit / 8] & (0x80 >> (bit & 7)))
                        panel.put(x + dx, top + y);
                }
            }
            pen += advances[i];
        }
    });
    return true;
}

template <typename T>
struct ComPtr {
    T* ptr = nullptr;
    ~ComPtr() { if (ptr) ptr->Release(); }
    T* operator->() const { return ptr; }
};

// DirectWrite opens the supplied sfnt bytes as a private face and provides
// the glyph masks. The preview window composites those masks with QuickDraw.
struct Outline {
    ComPtr<IDWriteFactory5> factory;
    ComPtr<IDWriteInMemoryFontFileLoader> loader;
    ComPtr<IDWriteFontFace> face;
    bool registered = false;

    explicit Outline(const Bytes& bytes)
    {
        if (bytes.empty())
            return;
        static const HMODULE module = LoadLibraryExW(L"dwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module)
            return;
        auto create = reinterpret_cast<decltype(&DWriteCreateFactory)>(GetProcAddress(module, "DWriteCreateFactory"));
        ComPtr<IDWriteFactory> base;
        if (!create || FAILED(create(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(&base.ptr))) ||
            FAILED(base->QueryInterface(__uuidof(IDWriteFactory5), reinterpret_cast<void**>(&factory.ptr))) ||
            FAILED(factory->CreateInMemoryFontFileLoader(&loader.ptr)) ||
            FAILED(factory->RegisterFontFileLoader(loader.ptr)))
            return;
        registered = true;
        ComPtr<IDWriteFontFile> file;
        BOOL supported = FALSE;
        DWRITE_FONT_FILE_TYPE file_type{};
        DWRITE_FONT_FACE_TYPE face_type{};
        UINT32 faces = 0;
        if (SUCCEEDED(loader->CreateInMemoryFontFileReference(factory.ptr, bytes.data(), UINT32(bytes.size()),
                                                             nullptr, &file.ptr)) &&
            SUCCEEDED(file->Analyze(&supported, &file_type, &face_type, &faces)) && supported && faces)
            factory->CreateFontFace(face_type, 1, &file.ptr, 0, DWRITE_FONT_SIMULATIONS_NONE, &face.ptr);
    }

    ~Outline()
    {
        if (face.ptr)
            std::exchange(face.ptr, nullptr)->Release();
        if (registered)
            factory->UnregisterFontFileLoader(loader.ptr);
    }

    bool render(Panel& panel, unsigned size) const
    {
        if (!face.ptr)
            return false;
        DWRITE_FONT_METRICS metrics{};
        face->GetMetrics(&metrics);
        if (!metrics.designUnitsPerEm)
            return false;
        std::array<UINT32, sample.size()> characters{};
        std::copy(sample.begin(), sample.end(), characters.begin());
        std::array<UINT16, sample.size()> glyphs{};
        std::array<DWRITE_GLYPH_METRICS, sample.size()> widths{};
        if (FAILED(face->GetGlyphIndices(characters.data(), characters.size(), glyphs.data())) ||
            FAILED(face->GetDesignGlyphMetrics(glyphs.data(), glyphs.size(), widths.data())))
            return false;
        float scale = float(size) / metrics.designUnitsPerEm;
        std::array<float, sample.size()> advances{};
        for (size_t i = 0; i < advances.size(); ++i)
            advances[i] = widths[i].advanceWidth * scale;
        int line_height = std::max(1, int(std::ceil((metrics.ascent + metrics.descent + metrics.lineGap) * scale)));
        bool success = true;
        sample_lines(advances, line_height, [&](size_t first, size_t end, int top) {
            if (end == first || top >= panel.height - 2 || !success)
                return;
            float baseline = top + std::ceil(metrics.ascent * scale);
            DWRITE_GLYPH_RUN run{face.ptr, float(size), UINT32(end - first), glyphs.data() + first,
                                 advances.data() + first, nullptr, FALSE, 0};
            ComPtr<IDWriteGlyphRunAnalysis> analysis;
            constexpr auto texture = DWRITE_TEXTURE_ALIASED_1x1;
            RECT box{};
            if (FAILED(static_cast<IDWriteFactory*>(factory.ptr)->CreateGlyphRunAnalysis(
                    &run, 1, nullptr, DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_NATURAL, 7, baseline,
                    &analysis.ptr)) || FAILED(analysis->GetAlphaTextureBounds(texture, &box))) {
                success = false;
                return;
            }
            const RECT interior{2, 6, panel_width - 2, panel.height - 2};
            if (!IntersectRect(&box, &box, &interior))
                return;
            int width = box.right - box.left, height = box.bottom - box.top;
            Bytes mask(size_t(width) * height);
            if (FAILED(analysis->CreateAlphaTexture(texture, &box, mask.data(), UINT32(mask.size())))) {
                success = false;
                return;
            }
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    if (mask[size_t(y) * width + x])
                        panel.put(box.left + x, box.top + y);
        });
        return success;
    }
};

struct Preview {
    std::wstring identity;
    std::vector<Panel> panels;
    bool bitmap_only = false;
    unsigned family_strike_size = 0;
};

std::wstring preview_title(const SuitcaseFork& fork, const SuitcaseResource& selected, const std::wstring& fallback)
{
    if ((selected.key.first != nfnt && selected.key.first != font) ||
        (!selected.name.empty() && selected.name.front()))
        return fallback;
    for (const auto& family : fork.resources) {
        if (family.key.first != fond || family.name.size() < 2)
            continue;
        for (const auto& strike : font_associations(family.payload)) {
            if (!strike.size || strike.id != selected.key.second)
                continue;
            wchar_t name[255]{};
            int count = MultiByteToWideChar(10000, 0, reinterpret_cast<const char*>(family.name.data() + 1),
                                            int(family.name.size() - 1), name, std::size(name));
            if (count)
                return std::wstring(name, count) + L" " + std::to_wstring(strike.size);
        }
    }
    return fallback;
}

void render_preview(Preview& preview, const SuitcaseFork& fork, const SuitcaseResource& selected)
{
    bool family = selected.key.first == fond;
    const SuitcaseResource* outline = selected.key.first == sfnt ? &selected : nullptr;
    std::vector<std::pair<unsigned, const SuitcaseResource*>> bitmaps;
    if (family) {
        for (const auto& entry : font_associations(selected.payload)) {
            if (entry.style)
                continue;
            if (!entry.size) {
                outline = resource(fork, {sfnt, entry.id});
                continue;
            }
            auto* bitmap = resource(fork, {nfnt, entry.id});
            if (!bitmap)
                bitmap = resource(fork, {font, entry.id});
            if (bitmap && Strike(bitmap->payload).count)
                bitmaps.emplace_back(entry.size, bitmap);
        }
        if (bitmaps.empty())
            for (const auto& entry : fork.resources)
                if ((entry.key.first == nfnt || entry.key.first == font) && entry.key.second > 0 &&
                    entry.key.second / 128 == selected.key.second && entry.key.second % 128 &&
                    Strike(entry.payload).count)
                    bitmaps.emplace_back(entry.key.second % 128, &entry);
    }
    const Bytes empty;
    Outline outline_font(outline ? outline->payload : empty);
    // A bitmap-only family previews one actual strike at its native size.
    // An individual FONT or NFNT always uses the same single-sample layout.
    if (!outline_font.face.ptr && (family || selected.key.first == font || selected.key.first == nfnt)) {
        const SuitcaseResource* strike = &selected;
        if (family && !bitmaps.empty()) {
            auto nearest = std::min_element(bitmaps.begin(), bitmaps.end(), [](const auto& a, const auto& b) {
                return std::abs(int(a.first) - 12) < std::abs(int(b.first) - 12);
            });
            strike = nearest->second;
            preview.family_strike_size = nearest->first;
        }
        Panel panel(L"Sample", 163);
        if ((family && bitmaps.empty()) || !render_bitmap(panel, strike->payload, 1))
            throw std::runtime_error("This font does not contain a usable bitmap or outline.");
        preview.bitmap_only = true;
        preview.panels.push_back(std::move(panel));
        return;
    }
    auto add_panel = [&](unsigned size, int height) {
        auto label = family ? std::to_wstring(size) + L" point" : std::wstring(L"Sample");
        Panel panel(std::move(label), height);
        auto closest = std::min_element(bitmaps.begin(), bitmaps.end(), [&](const auto& a, const auto& b) {
            return std::abs(int(a.first) - int(size)) < std::abs(int(b.first) - int(size));
        });
        bool rendered = closest != bitmaps.end() && closest->first == size &&
                        render_bitmap(panel, closest->second->payload, 1);
        if (!rendered && outline)
            rendered = outline_font.render(panel, size);
        if (!rendered && closest != bitmaps.end())
            rendered = render_bitmap(panel, closest->second->payload,
                                     family ? float(size) / closest->first : 1);
        if (!rendered)
            throw std::runtime_error("This font does not contain a usable bitmap or outline.");
        preview.panels.push_back(std::move(panel));
    };
    if (family) {
        add_panel(9, 40);
        add_panel(12, 58);
        add_panel(18, 108);
    }
    else
        add_panel(12, 156);
}

void paint_preview(HWND window, HDC dc, const Preview& preview)
{
    RECT client{};
    GetClientRect(window, &client);
    classic::fill_rect(dc, &client, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    auto previous = SelectObject(dc, classic::small_font());
    SetTextColor(dc, RGB(0, 0, 0));
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, RGB(255, 255, 255));
    constexpr int x = 8;
    int y = 10;
    for (const auto& panel : preview.panels) {
        classic::draw_pixels(dc, {x, y, x + panel_width, y + panel.height}, panel.pixels.data(),
                             panel.pixels.size() * sizeof(uint32_t));
        RECT label{x + 8, y - 5, x + panel_width - 8, y + 7};
        classic::draw_text(dc, panel.label.c_str(), -1, &label, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        label.bottom = y + 7;
        classic::fill_rect(dc, &label, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        classic::draw_text(dc, panel.label.c_str(), -1, &label, DT_SINGLELINE | DT_NOPREFIX);
        y += panel.height + 10;
    }
    RECT footer{x, y, client.right - 2, y + 14};
    classic::draw_text(dc, L"To use this font, drag it onto your System Folder", -1, &footer,
                       DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, previous);
}

LRESULT CALLBACK preview_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    auto* preview = reinterpret_cast<Preview*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE:
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        SetPropW(window, L"FinderWhite", reinterpret_cast<HANDLE>(1));
        classic::decorate(window);
        break;
    case WM_SETFOCUS:
        classic::activate(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CONTEXTMENU:
        return 0;
    case WM_PRINTCLIENT:
        if (preview)
            paint_preview(window, reinterpret_cast<HDC>(wp), *preview);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        auto dc = BeginPaint(window, &paint);
        if (preview)
            paint_preview(window, dc, *preview);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_NCDESTROY:
        // Ownership transfers only after CreateWindowW returns successfully.
        if (RemovePropW(window, L"FinderPreviewOwned"))
            delete preview;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        RemovePropW(window, L"FinderWhite");
        break;
    }
    return DefWindowProcW(window, message, wp, lp);
}
}

bool is_preview_font(uint32_t type)
{
    return type == fond || type == font || type == nfnt || type == sfnt;
}

bool is_font_preview_window(HWND window)
{
    wchar_t type[std::size(window_class)]{};
    return GetClassNameW(window, type, int(std::size(type))) && wcscmp(type, window_class) == 0;
}

void show_font_preview(HWND desktop, HINSTANCE instance, const std::wstring& identity, const std::wstring& name,
                       const SuitcaseFork& fork, const ResourceKey& key)
{
    for (HWND window = nullptr; (window = FindWindowExW(desktop, window, window_class, nullptr));) {
        auto* preview = reinterpret_cast<Preview*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (preview && preview->identity == identity) {
            classic::activate(window);
            SetFocus(window);
            return;
        }
    }
    const auto* selected = resource(fork, key);
    if (!selected)
        throw std::runtime_error("The selected font resource is no longer available.");
    auto preview = std::make_unique<Preview>();
    preview->identity = identity;
    render_preview(*preview, fork, *selected);
    WNDCLASSW type{};
    type.hInstance = instance;
    type.lpszClassName = window_class;
    type.lpfnWndProc = preview_proc;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&type);
    constexpr DWORD style = WS_CHILD | WS_CAPTION | WS_SYSMENU | WS_CLIPSIBLINGS;
    int height = preview->panels.size() > 1 ? 288 : preview->bitmap_only ? 225 : 216;
    int width = preview->bitmap_only ? 244 : 248;
    auto title = preview_title(fork, *selected, name);
    if (preview->family_strike_size)
        title += L" " + std::to_wstring(preview->family_strike_size);
    HWND window = CreateWindowW(window_class, title.c_str(), style, 116, 88, width, height, desktop, nullptr,
                                instance, preview.get());
    if (!window || !SetPropW(window, L"FinderPreviewOwned", reinterpret_cast<HANDLE>(1))) {
        if (window)
            DestroyWindow(window);
        throw std::runtime_error("Cannot open the font preview.");
    }
    preview.release();
    ShowWindow(window, SW_SHOW);
    classic::activate(window);
    SetFocus(window);
}

}
