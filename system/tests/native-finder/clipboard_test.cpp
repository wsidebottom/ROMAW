/*+===================================================================
  File:      clipboard_test.cpp

  Summary:   Exercise host clipboard file/text/image interchange with intercepted clipboard APIs and owned fixtures.

  Classes:   None.

  Functions: main; finder::testing clipboard API replacements.

  Origin:    Executor native Finder regression fixture. The user's clipboard is never opened or changed.

  Copyright and Legal notices: License reference: COPYING.
===================================================================+*/

#include "finder/clipboard.h"
#include <shlobj.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>

#undef assert
#define assert(condition)                                               \
    do {                                                                \
        if (!(condition)) {                                             \
            std::cerr << __LINE__ << ": " << #condition << " failed\n"; \
            std::exit(2);                                               \
        }                                                               \
    } while (false)

using namespace finder;

namespace {
std::map<UINT, HANDLE> formats;
bool opened = false, busy = false, empty_ok = true, register_ok = true, delayed = false, change_on_open = false;
UINT rejected = 0;
DWORD generation = 10;
unsigned opens = 0, closes = 0, empties = 0;
constexpr UINT preferred = 0xc001;
HANDLE rejected_handle = nullptr;

void clear_formats()
{
    for (auto [format, handle] : formats)
        if (format == CF_BITMAP)
            DeleteObject(handle);
        else
            GlobalFree(handle);
    formats.clear();
}

void put(UINT format, const void* bytes, size_t size)
{
    if (formats.count(format)) {
        GlobalFree(formats[format]);
        formats.erase(format);
    }
    auto memory = GlobalAlloc(GMEM_MOVEABLE, size);
    assert(memory);
    auto pointer = GlobalLock(memory);
    assert(pointer);
    // GlobalSize may include allocation padding. Unterminated fixtures must
    // remain unterminated throughout the whole readable global block.
    memset(pointer, 0xff, GlobalSize(memory));
    memcpy(pointer, bytes, size);
    GlobalUnlock(memory);
    formats[format] = memory;
    ++generation;
}

void text(const std::wstring& value)
{
    clear_formats();
    put(CF_UNICODETEXT, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
}

void drop(const std::wstring& paths, bool wide = true)
{
    clear_formats();
    DROPFILES header{};
    header.pFiles = sizeof header;
    header.fWide = wide;
    std::vector<unsigned char> bytes(sizeof header);
    memcpy(bytes.data(), &header, sizeof header);
    if (wide) {
        auto first = reinterpret_cast<const unsigned char*>(paths.data());
        bytes.insert(bytes.end(), first, first + paths.size() * sizeof(wchar_t));
    }
    else
        for (auto c : paths)
            bytes.push_back(static_cast<unsigned char>(c));
    put(CF_HDROP, bytes.data(), bytes.size());
}
}

namespace finder::testing {
BOOL WINAPI clip_open(HWND)
{
    assert(!opened);
    if (busy)
        return FALSE;
    opened = true;
    if (change_on_open) {
        ++generation;
        change_on_open = false;
    }
    ++opens;
    return TRUE;
}

BOOL WINAPI clip_close()
{
    assert(opened);
    opened = false;
    ++closes;
    return TRUE;
}

BOOL WINAPI clip_empty()
{
    assert(opened);
    if (!empty_ok)
        return FALSE;
    clear_formats();
    ++generation;
    ++empties;
    return TRUE;
}

HANDLE WINAPI clip_get(UINT format)
{
    assert(opened);
    if (delayed) {
        ++generation;
        delayed = false;
    }
    auto found = formats.find(format);
    return found == formats.end() ? nullptr : found->second;
}

HANDLE WINAPI clip_set(UINT format, HANDLE value)
{
    assert(opened);
    if (format == rejected) {
        rejected_handle = value;
        return nullptr;
    }
    assert(!formats.count(format));
    formats[format] = value;
    ++generation;
    return value;
}

BOOL WINAPI clip_available(UINT format)
{
    return formats.count(format) != 0;
}

DWORD WINAPI clip_sequence()
{
    return generation;
}

UINT WINAPI clip_register(LPCWSTR name)
{
    if (!wcscmp(name, L"ROMAW Macintosh icon family"))
        return register_ok ? preferred + 1 : 0;
    assert(!wcscmp(name, L"Preferred DropEffect"));
    return register_ok ? preferred : 0;
}

int WINAPI clip_count()
{
    assert(opened);
    return int(formats.size());
}
}

int main()
{
    HWND owner = reinterpret_cast<HWND>(123);
    auto fails = [](auto action) {
        try {
            action();
        }
        catch (const std::exception&) {
            return true;
        }
        return false;
    };
    text(L"Host text \u65e5\u672c\r\nSecond line");
    auto snapshot = read_host_clipboard(owner);
    assert(snapshot && snapshot->text == L"Host text \u65e5\u672c\r\nSecond line" && snapshot->files.empty());
    busy = true;
    assert(!read_host_clipboard(owner));
    busy = false;
    delayed = true;
    snapshot = read_host_clipboard(owner);
    assert(snapshot && snapshot->sequence == generation);
    clear_formats();
    const char ansi[] = "Windows ANSI text";
    put(CF_TEXT, ansi, sizeof ansi);
    assert(read_host_clipboard(owner)->text == L"Windows ANSI text");
    const wchar_t truncated[] = {L'x', L'y'};
    clear_formats();
    put(CF_UNICODETEXT, truncated, sizeof truncated);
    assert(read_host_clipboard(owner)->text.find(L"could not be read") != std::wstring::npos);
    Bytes family{0x69, 0x63, 0x6e, 0x73};
    Bytes rgba(4096);
    rgba[0] = 12;
    rgba[2] = 42;
    rgba[3] = 128;
    write_host_clipboard_icon(owner, family, rgba.data());
    assert(host_clipboard_has_icon() && read_host_clipboard_icon(owner) == family);
    auto dib = formats.at(CF_DIBV5);
    auto* header = static_cast<BITMAPV5HEADER*>(GlobalLock(dib));
    assert(header && header->bV5Width == 32 && header->bV5Height == -32 && header->bV5AlphaMask == 0xff000000);
    auto* pixel = reinterpret_cast<unsigned char*>(header + 1);
    assert(pixel[0] == 42 && pixel[2] == 12 && pixel[3] == 128);
    GlobalUnlock(dib);
    clear_formats();

    wchar_t temporary[32768], id[64];
    assert(GetTempPathW(32768, temporary));
    GUID guid;
    assert(SUCCEEDED(CoCreateGuid(&guid)));
    StringFromGUID2(guid, id, 64);
    auto root = fs::path(temporary) / (L"Finder-clipboard-" + std::wstring(id));
    fs::create_directory(root);
    auto file = root / L"A \u65e5\u672c file", companion = root / L"._A \u65e5\u672c file";
    std::ofstream(file) << "data";
    std::ofstream(companion) << "fork";
    write_host_clipboard_files(owner, {file}, false);
    assert(host_clipboard_has_files());
    {
        auto memory = static_cast<HGLOBAL>(formats[CF_HDROP]);
        auto bytes = static_cast<const unsigned char*>(GlobalLock(memory));
        DROPFILES header;
        memcpy(&header, bytes, sizeof header);
        assert(header.fWide && header.pFiles == sizeof header);
        auto path = reinterpret_cast<const wchar_t*>(bytes + header.pFiles);
        assert(fs::path(path) == file);
        path += wcslen(path) + 1;
        assert(fs::path(path) == companion); // Explorer receives both physical forks.
        GlobalUnlock(memory);
    }
    snapshot = read_host_clipboard(owner);
    assert(snapshot && snapshot->files == std::vector<fs::path>{file} && !snapshot->move);
    assert(snapshot->text.find(file.filename().native()) != std::wstring::npos);
    write_host_clipboard_files(owner, {file}, true);
    snapshot = read_host_clipboard(owner);
    assert(snapshot && snapshot->move);
    DWORD cut_sequence = snapshot->sequence;
    busy = true;
    assert(!finish_host_clipboard_move(owner, cut_sequence));
    busy = false;
    text(L"New host content while the background move runs");
    assert(finish_host_clipboard_move(owner, cut_sequence));
    assert(read_host_clipboard(owner)->text == L"New host content while the background move runs");
    write_host_clipboard_files(owner, {file}, true);
    cut_sequence = host_clipboard_sequence();
    assert(finish_host_clipboard_move(owner, cut_sequence) && formats.empty());
    write_host_clipboard_files(owner, {file}, true);
    cut_sequence = host_clipboard_sequence();
    change_on_open = true;
    assert(finish_host_clipboard_move(owner, cut_sequence) && !formats.empty());

    drop(companion.wstring() + L'\0' + L'\0');
    assert(read_host_clipboard(owner)->files == std::vector<fs::path>{companion});

    // External Explorer-style lists do not need a private Finder format.
    std::wstring names = file.wstring() + L'\0' + L'\0';
    drop(names);
    assert(read_host_clipboard(owner)->files == std::vector<fs::path>{file});
    DWORD effect = DROPEFFECT_MOVE;
    put(preferred, &effect, sizeof effect);
    assert(read_host_clipboard(owner)->move);
    effect = DROPEFFECT_MOVE | DROPEFFECT_COPY;
    put(preferred, &effect, sizeof effect);
    assert(!read_host_clipboard(owner)->move);
    effect = DROPEFFECT_MOVE | DROPEFFECT_LINK;
    put(preferred, &effect, sizeof effect);
    assert(!read_host_clipboard(owner)->move);
    drop(std::wstring(L"C:\\Temp\\ASCII file") + L'\0' + L'\0', false);
    assert(read_host_clipboard(owner)->files.size() == 1);
    for (auto malformed : {std::wstring(L"C:\\"), std::wstring(L"relative"), std::wstring(L"C:\\a*"),
                           std::wstring(L"\\\\server\\share")}) {
        drop(malformed + L'\0' + L'\0');
        assert(read_host_clipboard(owner)->files.empty());
    }
    DROPFILES malformed{};
    malformed.pFiles = 1000;
    malformed.fWide = TRUE;
    clear_formats();
    put(CF_HDROP, &malformed, sizeof malformed);
    assert(read_host_clipboard(owner)->files.empty());
    drop(file.wstring()); // No terminating nulls, including allocation padding.
    assert(read_host_clipboard(owner)->files.empty());

    text(L"Preserve on failed Open/Empty/Register");
    busy = true;
    assert(fails([&] { write_host_clipboard_files(owner, {file}, false); }));
    busy = false;
    empty_ok = false;
    assert(fails([&] { write_host_clipboard_files(owner, {file}, false); }));
    empty_ok = true;
    register_ok = false;
    assert(fails([&] { write_host_clipboard_files(owner, {file}, false); }));
    register_ok = true;
    assert(fails([&] { write_host_clipboard_files(owner, {fs::path(L"C:\\")}, false); }));
    assert(read_host_clipboard(owner)->text == L"Preserve on failed Open/Empty/Register");
    rejected = CF_HDROP;
    assert(fails([&] { write_host_clipboard_files(owner, {file}, false); }));
    assert(rejected_handle && GlobalFlags(rejected_handle) == GMEM_INVALID_HANDLE);
    rejected = 0;
    clear_formats();
    assert(read_host_clipboard(owner)->text.empty());

    // Use an actual background move with the same host snapshot/backend path
    // as Paste. Completion preserves the fork and then consumes the Cut.
    write_host_clipboard_files(owner, {file}, true);
    snapshot = read_host_clipboard(owner);
    auto destination = root / L"Pasted";
    fs::create_directory(destination);
    {
        Copies manager;
        auto job =
            manager.start(snapshot->files, destination, snapshot->move ? TransferKind::move : TransferKind::copy);
        assert(job);
        ULONGLONG deadline = GetTickCount64() + 10000;
        while (job->state < CopyJob::complete && GetTickCount64() < deadline)
            Sleep(1);
        assert(job->state == CopyJob::complete);
        assert(!fs::exists(file) && !fs::exists(companion));
        assert(fs::exists(destination / file.filename()) && fs::exists(destination / companion.filename()));
        assert(finish_host_clipboard_move(owner, snapshot->sequence) && formats.empty());
    }

    // Viewer snapshots own a distinct bitmap that survives clipboard changes.
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = 8;
    info.bmiHeader.biHeight = -8;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* pixels;
    HBITMAP original = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    assert(original);
    formats[CF_BITMAP] = original;
    ++generation;
    snapshot = read_host_clipboard(owner);
    assert(snapshot && snapshot->picture && snapshot->picture != original);
    assert(snapshot->picture_size.cx == 8 && snapshot->picture_size.cy == 8);
    clear_formats();
    BITMAP dimensions{};
    assert(GetObjectW(snapshot->picture, sizeof dimensions, &dimensions));
    HostClipboard moved = std::move(*snapshot);
    assert(!snapshot->picture && moved.picture);
    snapshot.reset();
    assert(opens == closes && !opened);
    fs::remove_all(root); // Only this fixture; clipboard calls above were all mocked.
    std::cout
        << "PASS: host Unicode/ANSI text, standard file lists, copy/cut effects, sidecars, malformed data, image ownership, busy/publication failures and sequence-safe Cut completion; real clipboard untouched\n";
}
