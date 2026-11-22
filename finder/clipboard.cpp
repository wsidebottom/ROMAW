/*+===================================================================
  File:      clipboard.cpp

  Summary:   Bounded Windows clipboard access, file Copy/Cut interoperability and owned text/image snapshots.

  Classes:   Declared: ClipboardOpen, LockedData, GlobalBlock, PathLess; Used: HostClipboard.

  Functions: finder::HostClipboard::HostClipboard, finder::HostClipboard::~HostClipboard,
             finder::HostClipboard::operator=, finder::read_host_clipboard,
             finder::write_host_clipboard_files, finder::host_clipboard_has_files, finder::host_clipboard_sequence,
             finder::finish_host_clipboard_move, finder::read_host_clipboard_icon, finder::write_host_clipboard_icon,
             finder::host_clipboard_has_icon.

  Origin:    Executor native Finder integration using Windows CF_HDROP and Preferred DropEffect contracts.

  Copyright and Legal notices: License reference: COPYING.
===================================================================+*/

#include "finder/clipboard.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <set>
#include <utility>
#include <shlobj.h>

namespace finder {
#ifdef FINDER_CLIPBOARD_TESTING
namespace testing {
BOOL WINAPI clip_open(HWND);
BOOL WINAPI clip_close();
BOOL WINAPI clip_empty();
HANDLE WINAPI clip_get(UINT);
HANDLE WINAPI clip_set(UINT, HANDLE);
BOOL WINAPI clip_available(UINT);
DWORD WINAPI clip_sequence();
UINT WINAPI clip_register(LPCWSTR);
int WINAPI clip_count();
}
#endif
namespace {
#ifdef FINDER_CLIPBOARD_TESTING
constexpr auto open_clip = testing::clip_open;
constexpr auto close_clip = testing::clip_close;
constexpr auto empty_clip = testing::clip_empty;
constexpr auto get_clip = testing::clip_get;
constexpr auto set_clip = testing::clip_set;
constexpr auto available = testing::clip_available;
constexpr auto sequence = testing::clip_sequence;
constexpr auto register_format = testing::clip_register;
constexpr auto count_formats = testing::clip_count;
#else
constexpr auto open_clip = OpenClipboard;
constexpr auto close_clip = CloseClipboard;
constexpr auto empty_clip = EmptyClipboard;
constexpr auto get_clip = GetClipboardData;
constexpr auto set_clip = SetClipboardData;
constexpr auto available = IsClipboardFormatAvailable;
constexpr auto sequence = GetClipboardSequenceNumber;
constexpr auto register_format = RegisterClipboardFormatW;
constexpr auto count_formats = CountClipboardFormats;
#endif
constexpr size_t maximum_bytes = 64 * 1024 * 1024;

struct PathLess {
    bool operator()(const fs::path& a, const fs::path& b) const
    {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    }
};

UINT effect_format()
{
    return register_format(L"Preferred DropEffect");
}

struct ClipboardOpen {
    bool opened;

    explicit ClipboardOpen(HWND owner) : opened(open_clip(owner) != FALSE)
    {
    }

    ~ClipboardOpen()
    {
        if (opened)
            close_clip();
    }

    ClipboardOpen(const ClipboardOpen&) = delete;
    ClipboardOpen& operator=(const ClipboardOpen&) = delete;
};

struct LockedData {
    HGLOBAL handle;
    size_t size;
    const unsigned char* bytes = nullptr;

    explicit LockedData(UINT format) : handle(get_clip(format)), size(handle ? GlobalSize(handle) : 0)
    {
        if (size && size <= maximum_bytes)
            bytes = static_cast<const unsigned char*>(GlobalLock(handle));
    }

    ~LockedData()
    {
        if (bytes)
            GlobalUnlock(handle);
    }

    LockedData(const LockedData&) = delete;
    LockedData& operator=(const LockedData&) = delete;
};

struct GlobalBlock {
    HGLOBAL handle = nullptr;

    GlobalBlock(const void* bytes, size_t size)
    {
        handle = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!handle)
            throw std::bad_alloc();
        auto target = GlobalLock(handle);
        if (!target) {
            GlobalFree(handle);
            handle = nullptr;
            throw std::runtime_error("Windows could not lock clipboard data.");
        }
        memcpy(target, bytes, size);
        GlobalUnlock(handle);
    }

    ~GlobalBlock()
    {
        if (handle)
            GlobalFree(handle);
    }

    GlobalBlock(const GlobalBlock&) = delete;
    GlobalBlock& operator=(const GlobalBlock&) = delete;

    bool publish(UINT format)
    {
        if (!set_clip(format, handle))
            return false;
        handle = nullptr; // Windows owns successful publications.
        return true;
    }
};

std::optional<std::wstring> text_value(const unsigned char* bytes, size_t size, bool wide)
{
    if (wide) {
        std::wstring text;
        for (size_t offset = 0; offset + sizeof(wchar_t) <= size; offset += sizeof(wchar_t)) {
            wchar_t character;
            memcpy(&character, bytes + offset, sizeof character);
            if (!character)
                return text;
            text += character;
        }
    }
    else {
        auto end = static_cast<const unsigned char*>(memchr(bytes, 0, size));
        if (!end)
            return std::nullopt;
        int length = int(end - bytes);
        int count = MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(bytes), length, nullptr, 0);
        if (length && !count)
            return std::nullopt;
        std::wstring text(count, 0);
        if (count)
            MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(bytes), length, text.data(), count);
        return text;
    }
    return std::nullopt;
}

bool absolute_name(const std::wstring& name)
{
    auto slash = [](wchar_t c) { return c == L'\\' || c == L'/'; };
    if (name.find_first_of(L"*?") != std::wstring::npos)
        return false;
    if (name.size() > 3 && ((name[0] >= L'A' && name[0] <= L'Z') || (name[0] >= L'a' && name[0] <= L'z')) &&
        name[1] == L':' && slash(name[2]))
        return true;
    if (name.size() > 2 && slash(name[0]) && slash(name[1])) {
        auto server = name.find_first_of(L"\\/", 2);
        if (server != std::wstring::npos) {
            auto share = name.find_first_of(L"\\/", server + 1);
            return share != std::wstring::npos && share + 1 < name.size();
        }
    }
    return false;
}

// A CF_HDROP from Finder includes physical forks so Explorer can copy them.
// Collapse only companions paired with a selected payload. Standalone hidden
// files copied by another application remain valid host clipboard selections.
void collapse_companions(std::vector<fs::path>& paths)
{
    auto equal = [](const fs::path& a, const fs::path& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; };
    std::set<fs::path, PathLess> selected(paths.begin(), paths.end());
    paths.erase(
        std::remove_if(paths.begin(), paths.end(),
                       [&](const fs::path& path) {
                           auto name = path.filename().wstring();
                           fs::path payload;
                           if (name.rfind(L"._", 0) == 0)
                               payload = path.parent_path() / name.substr(2);
                           else if (!name.empty() && name.front() == L'%')
                               payload = path.parent_path() / name.substr(1);
                           else {
                               auto directory = path.parent_path().filename().wstring();
                               if (directory == L".AppleDouble" || directory == L".finf" || directory == L".rsrc")
                                   payload = path.parent_path().parent_path() / path.filename();
                           }
                           if (payload.empty() || selected.find(payload) == selected.end())
                               return false;
                           auto companions = mac_companions(payload);
                           return std::any_of(companions.begin(), companions.end(),
                                              [&](const fs::path& companion) { return equal(path, companion); });
                       }),
        paths.end());
}

bool drop_paths(const LockedData& data, std::vector<fs::path>& paths)
{
    if (!data.bytes || data.size < sizeof(DROPFILES))
        return false;
    DROPFILES drop;
    memcpy(&drop, data.bytes, sizeof drop);
    size_t unit = drop.fWide ? sizeof(wchar_t) : 1;
    if (drop.pFiles < sizeof drop || drop.pFiles > data.size || data.size - drop.pFiles < 2 * unit)
        return false;
    size_t offset = drop.pFiles;
    bool had_path = false;
    while (offset < data.size) {
        auto name = text_value(data.bytes + offset, data.size - offset, drop.fWide != FALSE);
        if (!name)
            return false;
        // ANSI paths use byte lengths, which may differ from UTF-16 length.
        size_t length = drop.fWide
                            ? name->size() * unit
                            : static_cast<const unsigned char*>(memchr(data.bytes + offset, 0, data.size - offset)) -
                                  (data.bytes + offset);
        if (name->empty()) {
            if (!had_path) {
                for (size_t i = 0; i < unit; ++i)
                    if (data.bytes[offset + unit + i])
                        return false;
            }
            return true;
        }
        if (name->size() > 32767 || !absolute_name(*name))
            return false;
        paths.emplace_back(*name);
        had_path = true;
        offset += length + unit;
    }
    return false;
}
}

HostClipboard::~HostClipboard()
{
    if (picture)
        DeleteObject(picture);
}

HostClipboard::HostClipboard(HostClipboard&& other) noexcept
{
    *this = std::move(other);
}

HostClipboard& HostClipboard::operator=(HostClipboard&& other) noexcept
{
    if (this != &other) {
        if (picture)
            DeleteObject(picture);
        files = std::move(other.files);
        text = std::move(other.text);
        move = other.move;
        sequence = other.sequence;
        picture = std::exchange(other.picture, nullptr);
        picture_size = other.picture_size;
    }
    return *this;
}

std::optional<HostClipboard> read_host_clipboard(HWND owner)
{
    ClipboardOpen clipboard(owner);
    if (!clipboard.opened)
        return std::nullopt;
    HostClipboard result;
    if (available(CF_HDROP)) {
        LockedData data(CF_HDROP);
        if (!drop_paths(data, result.files)) {
            result.files.clear();
            result.text = L"The clipboard file list could not be read.";
        }
        else {
            for (const auto& file : result.files)
                result.text += file.wstring() + L"\r\n";
            collapse_companions(result.files);
            UINT format = effect_format();
            if (format && available(format)) {
                LockedData effect(format);
                if (effect.bytes && effect.size >= sizeof(DWORD)) {
                    DWORD value;
                    memcpy(&value, effect.bytes, sizeof value);
                    result.move = value == DROPEFFECT_MOVE; // Ambiguous/unknown masks default to copying.
                }
            }
        }
    }
    else if (available(CF_BITMAP)) {
        auto source = static_cast<HBITMAP>(get_clip(CF_BITMAP));
        BITMAP bitmap{};
        if (source && GetObjectW(source, sizeof bitmap, &bitmap) && bitmap.bmWidth > 0 && bitmap.bmHeight > 0 &&
            uint64_t(bitmap.bmWidth) * bitmap.bmHeight <= 16 * 1024 * 1024) {
            result.picture = static_cast<HBITMAP>(CopyImage(source, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
            result.picture_size = SIZE{bitmap.bmWidth, bitmap.bmHeight};
        }
        if (!result.picture)
            result.text = L"The clipboard image could not be displayed.";
    }
    else if (available(CF_UNICODETEXT) || available(CF_TEXT)) {
        bool wide = available(CF_UNICODETEXT) != FALSE;
        LockedData data(wide ? CF_UNICODETEXT : CF_TEXT);
        auto text = data.bytes ? text_value(data.bytes, data.size, wide) : std::nullopt;
        result.text = text ? std::move(*text) : L"The clipboard text could not be read.";
    }
    else if (count_formats())
        result.text = L"The clipboard contains a format this viewer cannot display.";
    result.sequence = sequence(); // Delayed rendering may change the sequence during the read.
    return result;
}

void write_host_clipboard_files(HWND owner, const std::vector<fs::path>& paths, bool move)
{
    if (paths.empty())
        return;
    std::set<fs::path, PathLess> physical;
    std::wstring names, text;
    auto append = [&](const fs::path& file) {
        if (physical.insert(file).second) {
            names += file.wstring();
            names += L'\0';
        }
    };
    for (const auto& path : paths) {
        if (path.empty())
            throw std::runtime_error("An empty filename cannot be copied.");
        wchar_t full[32768];
        DWORD length = GetFullPathNameW(path.c_str(), 32768, full, nullptr);
        if (!length || length >= 32768)
            throw std::runtime_error("Windows could not resolve a clipboard filename.");
        std::wstring name(full, length);
        while (name.size() > 3 && (name.back() == L'\\' || name.back() == L'/'))
            name.pop_back();
        if (!absolute_name(name))
            throw std::runtime_error(
                "Only ordinary files and folders can be copied, not volume roots or wildcard paths.");
        fs::path file(name);
        text += file.wstring() + L"\r\n";
        append(file);
        for (const auto& companion : mac_companions(file))
            append(companion);
        if (names.size() > maximum_bytes / sizeof(wchar_t) || text.size() > maximum_bytes / sizeof(wchar_t))
            throw std::runtime_error("The clipboard file selection is too large.");
    }
    names += L'\0';
    DROPFILES header{};
    header.pFiles = sizeof header;
    header.fWide = TRUE;
    std::vector<unsigned char> bytes(sizeof header + names.size() * sizeof(wchar_t));
    memcpy(bytes.data(), &header, sizeof header);
    memcpy(bytes.data() + sizeof header, names.data(), names.size() * sizeof(wchar_t));
    DWORD effect = move ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
    UINT format = effect_format();
    if (!format)
        throw std::runtime_error("Windows could not register the clipboard file-transfer format.");
    // Allocate and lock everything before EmptyClipboard can affect host data.
    GlobalBlock files(bytes.data(), bytes.size()), intent(&effect, sizeof effect),
        description(text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    ClipboardOpen clipboard(owner);
    if (!clipboard.opened || !empty_clip())
        throw std::runtime_error("Windows could not open or clear the clipboard.");
    if (!files.publish(CF_HDROP) || !intent.publish(format) || !description.publish(CF_UNICODETEXT))
        throw std::runtime_error("Windows could not publish the clipboard file selection.");
}

bool host_clipboard_has_files()
{
    return available(CF_HDROP) != FALSE;
}

static UINT icon_format()
{
    return register_format(L"ROMAW Macintosh icon family");
}

Bytes read_host_clipboard_icon(HWND owner)
{
    ClipboardOpen clipboard(owner);
    if (!clipboard.opened)
        throw std::runtime_error("The Windows clipboard is busy.");
    auto format = icon_format();
    if (format && available(format)) {
        LockedData family(format);
        if (family.bytes && family.size <= 4096)
            return Bytes(family.bytes, family.bytes + family.size);
    }
    return {};
}

void write_host_clipboard_icon(HWND owner, const Bytes& family, const unsigned char* rgba)
{
    auto format = icon_format();
    if (!format || family.empty() || family.size() > 4096 || !rgba)
        throw std::runtime_error("The icon could not be prepared for the clipboard.");
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof header;
    header.bV5Width = 32;
    header.bV5Height = -32;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00ff0000;
    header.bV5GreenMask = 0x0000ff00;
    header.bV5BlueMask = 0xff;
    header.bV5AlphaMask = 0xff000000;
    header.bV5CSType = LCS_sRGB;
    Bytes dib(sizeof header + 4096);
    memcpy(dib.data(), &header, sizeof header);
    for (size_t i = 0; i < 4096; i += 4) {
        dib[sizeof header + i] = rgba[i + 2];
        dib[sizeof header + i + 1] = rgba[i + 1];
        dib[sizeof header + i + 2] = rgba[i];
        dib[sizeof header + i + 3] = rgba[i + 3];
    }
    GlobalBlock payload(family.data(), family.size()), preview(dib.data(), dib.size());
    ClipboardOpen clipboard(owner);
    if (!clipboard.opened || !empty_clip() || !payload.publish(format))
        throw std::runtime_error("Windows could not publish the clipboard icon.");
    preview.publish(CF_DIBV5);
}

bool host_clipboard_has_icon()
{
    auto format = icon_format();
    return (format && available(format)) || available(CF_BITMAP);
}

DWORD host_clipboard_sequence()
{
    return sequence();
}

bool finish_host_clipboard_move(HWND owner, DWORD expected)
{
    if (!expected || sequence() != expected)
        return true;
    ClipboardOpen clipboard(owner);
    if (!clipboard.opened)
        return false;
    if (sequence() != expected)
        return true;
    return empty_clip() != FALSE;
}
}
