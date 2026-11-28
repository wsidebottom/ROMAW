/*+===================================================================
  File:      main.cpp

  Summary:   Native Finder UI, menus, desktop and folder windows, operation publication and host-shell integration.

  Classes:   Declared: DriveMetadata, Item, FolderWindow, FolderCallback, Operation, InfoData, ClipboardView, SuitcaseView;
             Used: Copies, History, DesktopIcons, DragImage, Search, HostClipboard.

  Functions: romaw_finder_main, ROMlib_finder_style_afp_chooser. UI command handlers and window procedures have internal linkage.

  Origin:    Existing Executor native Finder frontend; reference artwork and behavior originate in finder7.

  Copyright and Legal notices: Existing notices are retained below. License reference: COPYING.
===================================================================+*/

// Native Finder UI and event routing. Folder/operation registries and GDI
// handles belong to the UI thread; worker threads publish job/search state
// without calling these window procedures. Resource geometry and artwork are
// generated from ROMAW's editable catalog.

#include "generated/build_version.h"
#include "finder/files.h"
#include "finder/font_preview.h"
#include "finder/sound_preview.h"
#include "finder/shortcut_icon.h"
#include "finder/stuffit.h"
#include "finder/archive.h"
#include "rsys/archive_types.h"
#include "finder/info.h"
#include "finder/image_copy.h"
#include "finder/recycle.h"
#include "finder/clipboard.h"
#include "finder/actions.h"
#include "finder/file_associations.h"
#include <winioctl.h>
#include <winnetwk.h>
#include <psapi.h>
#include "finder/classic.h"
#include "finder/settings.h"
#include "finder/drag.h"
#include "finder/views.h"
#include "finder/desktop_icons.h"
#include "rsys/configprefs.h"
extern "C" {
#include "rsys/prefpanel.h"
}
#include "finder_metadata/icons.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <commctrl.h>
#include <commdlg.h>
#include <iomanip>
#include <functional>
#include <cstring>
#include <map>
#include <tuple>
#include <set>
#include <shellapi.h>
#include <shlobj.h>
#include <sstream>
#include <uxtheme.h>
#include <windowsx.h>
#include "assets.inc"
#include "volume_status.inc"
#include "disclosure.inc"
#include <utility>
#include "copy_layout.inc"
#include "chrome_art.inc"
#include "label_art.inc"
#include "interface_layout.inc"
#include "about_layout.inc"
#include "rsys/icon_color.h"
#include "finder/image_volumes.h"
#include "rsys/desktop_database.h"
#include "view_menu.inc"
#include "label_menu.inc"

static_assert(sizeof(void*) == 8, "Finder requires Windows x64");
using namespace finder;

extern "C" void ROMlib_finder_style_afp_chooser(void* dialog)
{
    auto window = static_cast<HWND>(dialog);
    classic::style_dialog(window);
    if (auto edit = FindWindowExW(window, nullptr, L"Edit", nullptr))
        classic::style_text_control(edit, true);
}

namespace {
enum Command {
    open_command = 100,
    new_folder_command,
    duplicate_command,
    info_command,
    rename_command,
    trash_command,
    quit_command,
    copy_command,
    paste_command,
    select_all_command,
    icons_command,
    list_command,
    refresh_command,
    about_command = 114,
    about_system_command,
    about_romaw_command,
    parent_command,
    arrange_command,
    close_command,
    clipboard_command,
    label_popup_command,
    undo_command,
    cut_command,
    clear_command,
    alias_command,
    put_away_command,
    find_command,
    find_again_command,
    print_command = 130,
    page_setup_command,
    print_window_command,
    empty_trash_command,
    eject_command,
    erase_command,
    small_icons_command,
    show_hidden_command,
    arrange_name_command,
    arrange_selection_command,
    spring_folders_command,
    special_cleanup_command,
    extract_here_command,
    extract_to_command,
    romaw_prefs_command,
    windows_shortcut_command,
    register_images_command,
    configure_app_command,
    afp_chooser_command,
    label_command = 200,
    sort_command = 300
};

unsigned special_cleanup_action = arrange_command;

// A disconnected mapped drive can spend minutes inside Windows filesystem
// calls. Publish immutable native metadata without retaining any UI objects.
struct DriveMetadata {
    MacFile mac;
    FinderIcon custom{};
    std::wstring stamp;
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    bool attributes_known = false;
    std::atomic<bool> ready{false};
    std::chrono::steady_clock::time_point completed;
};

constexpr int windows_desktop_volume = 5;
constexpr wchar_t windows_desktop_name[] = L"Windows Desktop";

struct Item {
    fs::path path;
    std::wstring name;
    MacFile mac;
    std::shared_ptr<DriveMetadata> drive_metadata;
    ViewEntry details;
    // Roles: ordinary item, drive, ROMAW volume, Trash, mounted image, Windows Desktop. icon is a list index,
    // not an owned HICON; decoded handles are retained in the shared icon cache.
    int special = 0, icon = 0, selected_icon = 0;
    bool folder = false, attributes_known = false, trash_full = false;
    int16_t image_volume = 0;
    uint32_t image_id = 2, image_parent = 1;
    std::string image_name;
    std::wstring image_stamp;
    std::string image_application;
    uint32_t image_application_parent = 0;
    unsigned outline_depth = 0;
    bool can_expand = false;
};

struct FolderWindow {
    HWND window = nullptr, list = nullptr, status = nullptr, used = nullptr, available = nullptr;
    fs::path path;
    // Creation helpers own this state until CreateWindowEx succeeds. Only then
    // may WM_NCDESTROY delete it; failure keeps the caller's unique_ptr in charge.
    bool desktop = false, updating = false, window_owned = false, closing = false;
    HIMAGELIST images = nullptr, small_images = nullptr;
    ViewPreferences presentation;
    std::vector<Item> items;
    std::set<fs::path> expanded_folders;
    std::shared_ptr<Search> search;
    size_t search_count = 0;
    bool search_finished = false;
    bool read_only = false; // The mounted File Manager's effective volume lock, not the requested access mode.
    int16_t image_volume = 0;
    uint32_t image_directory = 2;
    std::wstring image_title;
    unsigned active_callbacks = 0;
    bool delete_pending = false;
};

// DestroyWindow can reenter a window procedure before an outer callback returns.
// Keep its state alive until every callback borrowing it has unwound.
class FolderCallback {
    FolderWindow* folder;

  public:
    explicit FolderCallback(FolderWindow* value) : folder(value)
    {
        if (folder)
            ++folder->active_callbacks;
    }

    FolderCallback(const FolderCallback&) = delete;
    FolderCallback& operator=(const FolderCallback&) = delete;

    ~FolderCallback()
    {
        if (folder && --folder->active_callbacks == 0 && folder->delete_pending)
            delete folder;
    }
};

struct DropPlacement {
    POINT origin;
    size_t columns;
    bool desktop;
    size_t first = 0;
};

struct Extraction {
    ArchiveDestination destination;
    std::vector<ArchiveSource> sources;
    std::wstring formats;
};

struct Operation {
    // The UI registry owns this state even after a completed window is hidden.
    // The shared job survives registry reclamation until its views release it.
    HWND window, bar, stop;
    HWND fields[5]{};
    std::shared_ptr<CopyJob> job;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    bool finished = false;
    DWORD clipboard_sequence = 0; // Consume Cut only after this job succeeds.
    std::function<void(const CopyJob&)> on_complete;
    std::optional<DropPlacement> drop;
    std::optional<Extraction> extraction;
};

HINSTANCE instance;
HWND desktop;
HFONT font;
HICON app_icon;
HICON volume_lock_icon = nullptr;
std::array<HICON, 2> disclosure_icons{};
std::array<HICON, 10> classic_art{};
std::array<HICON, 4> frame_art{};
fs::path executable_folder, preferences, desktop_folder, romaw_folder, windows_desktop_path;
std::vector<FolderWindow*> folders;
std::vector<Item> apple_items;
std::vector<std::unique_ptr<Operation>> operations;
Copies copies;
History history;
FindOptions last_find;
std::vector<SearchRoot> last_search_roots;
PAGESETUPDLGW page_setup{};
std::map<std::wstring, HICON> icon_cache;
std::map<HICON, HICON> selected_icon_cache;
std::map<std::wstring, std::wstring> icon_versions;
std::unique_ptr<DesktopIcons> desktop_icons;
uint64_t icon_generation = 0;
std::map<fs::path, std::shared_ptr<DriveMetadata>> drive_metadata;
std::map<fs::path, std::shared_ptr<DriveMetadata>> drive_snapshots;
std::set<fs::path> published_drives;
std::vector<HANDLE> children;
FolderWindow* current_folder = nullptr;

bool live_folder(FolderWindow* folder)
{
    return folder && std::find(folders.begin(), folders.end(), folder) != folders.end() && !folder->closing &&
           IsWindow(folder->window) && IsWindow(folder->list);
}

FolderWindow* command_folder(FolderWindow* fallback)
{
    if (live_folder(current_folder))
        return current_folder;
    current_folder = nullptr;
    for (auto* folder : folders)
        if (live_folder(folder) && folder->window == classic::active_window())
            return current_folder = folder;
    return fallback;
}

bool show_hidden = false;
bool spring_folders = true;
// A captured drag uses screen-space endpoints and a snapshot of selected row
// indexes. Drop routing must identify the child folder before its desktop.
FolderWindow* drag_folder = nullptr;
POINT drag_start{};
std::vector<int> drag_indices;
DragImage drag_image;
DragHighlight drag_highlight;

struct SpringTarget {
    HWND list = nullptr;
    int index = -1;
    std::chrono::steady_clock::time_point opened_after{};
};

SpringTarget spring_target;
std::vector<HWND> spring_windows;
constexpr int icon_cell_width = 64, icon_cell_height = 52;

// Capture coordinates in the icon canvas at release, before a conflict
// dialog, window scroll, or background worker can change the viewport.
std::optional<DropPlacement> drop_placement(const FolderWindow& folder, POINT screen)
{
    if (folder.search || folder.presentation.view != FolderView::icons)
        return {};
    POINT view_origin{};
    RECT client{};
    ScreenToClient(folder.list, &screen);
    GetClientRect(folder.list, &client);
    if (!PtInRect(&client, screen))
        return {};
    ListView_GetOrigin(folder.list, &view_origin);
    LONG left = std::clamp(screen.x - 16, 0L, std::max(0L, client.right - 32));
    LONG top = std::clamp(screen.y - 16, 0L, std::max(0L, client.bottom - 32));
    size_t columns = 1 + std::max(0L, client.right - 32 - left) / icon_cell_width;
    return DropPlacement{{std::max(0L, left - view_origin.x), std::max(0L, top - view_origin.y)}, columns,
                         folder.desktop};
}

// Locate assets relative to the real executable, independent of the process
// working directory.
fs::path module_folder()
{
    std::vector<wchar_t> path(32768);
    DWORD n = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (!n || n >= path.size())
        throw std::runtime_error("Cannot locate Finder");
    return fs::path(std::wstring(path.data(), n)).parent_path();
}

// Read native settings through the migration-aware INI adapter.
std::wstring profile_string(const wchar_t* section, const std::wstring& key, const wchar_t* fallback = L"")
{
    return settings_value(preferences, section, key, fallback);
}

// Persist one UI setting in the native Finder INI; the caller supplies its
// logical section and stable key.
void save(const wchar_t* section, const std::wstring& key, const std::wstring& value)
{
    WritePrivateProfileStringW(section, key.c_str(), value.c_str(), preferences.c_str());
}

// Use the persistence adapter's historical hash so existing window and icon
// records remain addressable.
std::wstring path_key(const fs::path& p)
{
    return settings_path_key(p);
}

// Use the same normalized HostVolumeNames lookup as emulated volumes. Names
// affect presentation only; host paths and persistence identities stay intact.
std::wstring volume_display_name(const fs::path& root, std::wstring fallback)
{
    char label[256];
    size_t length = config_drive_label(root.u8string().c_str(), label, sizeof label);
    int count = length ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, label, int(length), nullptr, 0) : 0;
    if (!count)
        return fallback;
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, label, int(length), result.data(), count);
    return result;
}

std::shared_ptr<DriveMetadata> request_drive_metadata(const fs::path& path)
{
    auto& result = drive_metadata[path];
    if (result && result->ready.load())
        drive_snapshots[path] = result;
    if (!result ||
        (result->ready.load() && std::chrono::steady_clock::now() - result->completed >= std::chrono::seconds(30))) {
        auto pending = std::make_shared<DriveMetadata>();
        // Each drive has at most one outstanding read. A stalled network drive
        // cannot delay another drive or require the UI to join it on shutdown.
        std::thread([path, pending] {
            try {
                pending->attributes_known =
                    GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &pending->attributes) != FALSE;
                if (pending->attributes_known) {
                    pending->mac = mac_file(path);
                    custom_folder_icon(path, pending->custom);
                    pending->stamp = icon_source_stamp(path, true);
                }
            }
            catch (const std::exception&) {
                // Optional malformed metadata must not hide the volume.
            }
            pending->completed = std::chrono::steady_clock::now();
            pending->ready = true;
        }).detach();
        // Retain the last published snapshot while a refresh is in flight.
        result = pending;
        published_drives.erase(path);
    }
    return drive_snapshots[path];
}

// Drive icons used letter-based desktop keys before display aliases were
// honored. Keep those keys so restoring a volume name does not move its icon.
fs::path icon_position_path(const FolderWindow& folder, const Item& item)
{
    if (item.image_volume)
        return item.path;
    if (!folder.desktop)
        return item.path;
    auto name = item.name;
    auto root = item.path.root_name().wstring();
    if (item.special == 1 && !root.empty())
        name.assign(1, root.front());
    return fs::path(L"Desktop") / name;
}

std::wstring host_icon_name(const fs::path& path, const MacFile& mac)
{
    if (_wcsicmp(path.extension().c_str(), L".lnk") == 0)
        return path.stem().wstring();
    return mac_display_name(path, mac);
}

// Compute the classic label tint and quantize it to the Macintosh color cube
// for consistent selection/text backgrounds.
COLORREF
label_background(unsigned label, bool selected)
{
    if (!label)
        return selected ? RGB(0, 0, 0) : RGB(255, 255, 255);
    unsigned channels[3];
    for (unsigned i = 0; i < 3; ++i)
        channels[i] =
            (selected ? classic_label_rgb[label][i] * 3 / 4 : (classic_label_rgb[label][i] + 3 * 65535u) / 4) >> 8;
    // Classic 8-bit desktop colors use the Macintosh six-level color cube.
    for (auto& component : channels)
        component = std::min(255u, ((component + 25) / 51) * 51);
    return RGB(channels[0], channels[1], channels[2]);
}

// Show a classic-styled notice owned by the command's window.
void message(HWND owner, const std::wstring& text, UINT style = MB_ICONINFORMATION)
{
    classic::message_box(owner, text.c_str(), MB_OK | style);
}

// Convert exception text for display at a window-procedure boundary instead of
// unwinding through Win32 callbacks.
void failure(HWND owner, const std::exception& e)
{
    auto s = std::string(e.what());
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring text(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), text.data(), n);
    message(owner, text, MB_ICONERROR);
}

// Create a visible child control with the shared Finder font and native theme
// disabled.
HWND control(HWND owner, const wchar_t* type, const wchar_t* text, DWORD style, int id, int x, int y, int width,
             int height)
{
    if (wcscmp(type, L"BUTTON") == 0 && (style & BS_TYPEMASK) != BS_GROUPBOX)
        style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW;
    HWND w = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style, x, y, width, height, owner,
                             reinterpret_cast<HMENU>(intptr_t(id)), instance, nullptr);
    SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SetWindowTheme(w, L"", L"");
    classic::style_text_control(w);
    return w;
}

// Convert RGBA artwork into an owned Windows icon; the eventual owner must
// call DestroyIcon.
HICON
rgba_icon(const unsigned char* rgba, unsigned width, unsigned height)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -int(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits;
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color)
        return CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
    auto* out = static_cast<unsigned char*>(bits);
    unsigned mask_stride = ((width + 15) / 16) * 2;
    std::vector<unsigned char> mask(mask_stride * height);
    for (unsigned i = 0; i < width * height; ++i) {
        unsigned a = rgba[i * 4 + 3];
        out[i * 4] = rgba[i * 4 + 2] * a / 255;
        out[i * 4 + 1] = rgba[i * 4 + 1] * a / 255;
        out[i * 4 + 2] = rgba[i * 4] * a / 255;
        out[i * 4 + 3] = a;
        if (!a)
            mask[(i / width) * mask_stride + (i % width) / 8] |= 0x80 >> (i % width % 8);
    }
    HBITMAP mono = CreateBitmap(width, height, 1, 1, mask.data());
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mono;
    HICON result = CreateIconIndirect(&ii);
    DeleteObject(mono);
    DeleteObject(color);
    return result;
}

// Apply the shared Macintosh label transform before creating the native icon
// handle.
HICON
icon(const FinderIcon& source, unsigned label = 0)
{
    std::array<unsigned char, 4096> rgba;
    finder_icon_rgba(&source, rgba.data());
    for (unsigned i = 0; i < 1024; ++i) {
        bool ink = source.mono[i / 8] & (0x80 >> (i % 8));
        if (!(source.present & 12) && ink)
            rgba[i * 4 + 3] = 255; // Classic mono AND/OR drawing preserves ink outside the clearing mask.
        if (label) {
            unsigned depth = source.present & 8 ? 8 : 4;
            unsigned index = source.present & 8   ? source.color8[i]
                             : source.present & 4 ? (source.color4[i / 2] >> ((i & 1) ? 0 : 4)) & 15
                             : ink                ? 15
                                                  : 0;
            uint16_t rgb[] = {uint16_t(rgba[i * 4] * 257), uint16_t(rgba[i * 4 + 1] * 257),
                              uint16_t(rgba[i * 4 + 2] * 257)};
            ROMlib_icon_label_rgb(rgb, classic_label_rgb[label], depth, index);
            for (unsigned channel = 0; channel < 3; ++channel)
                rgba[i * 4 + channel] = rgb[channel] >> 8;
        }
    }
    auto selected_rgba = rgba;
    unsigned depth = source.present & 8 ? 8 : 4;
    for (unsigned i = 0; i < 1024; ++i) {
        bool ink = source.mono[i / 8] & (0x80 >> (i % 8));
        unsigned index = source.present & 8   ? source.color8[i]
                         : source.present & 4 ? (source.color4[i / 2] >> ((i & 1) ? 0 : 4)) & 15
                         : ink                ? 15
                                              : 0;
        uint16_t rgb[] = {uint16_t(rgba[i * 4] * 257), uint16_t(rgba[i * 4 + 1] * 257),
                          uint16_t(rgba[i * 4 + 2] * 257)};
        ROMlib_icon_selected_index_rgb(rgb, depth, index);
        for (unsigned channel = 0; channel < 3; ++channel)
            selected_rgba[i * 4 + channel] = rgb[channel] >> 8;
    }
    HICON normal = rgba_icon(rgba.data(), 32, 32);
    HICON selected = rgba_icon(selected_rgba.data(), 32, 32);
    if (normal && selected)
        selected_icon_cache[normal] = selected;
    else if (selected)
        DestroyIcon(selected);
    return normal;
}

// Decode an embedded icon asset, falling back to an owned copy of the Windows
// application icon.
HICON
asset_icon(unsigned index, unsigned label = 0)
{
    FinderIcon f{};
    if (index < std::size(assets) && finder_parse_icon_file(assets[index].data, assets[index].size, &f))
        return icon(f, label);
    return CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
}

std::wstring item_icon_key(const Item& item)
{
    return item.path.wstring() + L"#" + std::to_wstring(item.special) + L"#label-" +
           std::to_wstring((item.mac.flags >> 1) & 7);
}

// Select custom, special-folder, volume or application artwork and cache the
// owned handle by path, special role and label.
HICON
item_icon(Item& item)
{
    if (!(item.mac.flags & 0x400) && _wcsicmp(item.path.extension().c_str(), L".lnk") == 0) {
        try {
            auto target = shortcut_target(item.path);
            if (!target.empty() && _wcsicmp(target.extension().c_str(), L".lnk")) {
                Item original;
                original.path = target;
                original.mac = mac_file(target);
                original.mac.flags = (original.mac.flags & ~14u) | (item.mac.flags & 14u);
                original.folder = fs::is_directory(target);
                return item_icon(original);
            }
        }
        catch (...) {
        }
    }
    unsigned label = (item.mac.flags >> 1) & 7;
    auto key = item_icon_key(item);
    auto found = icon_cache.find(key);
    auto version = (item.special == 1 ? (item.drive_metadata ? item.drive_metadata->stamp : L"pending")
                                      : icon_source_stamp(item.path, item.folder)) +
                   L"#" + std::to_wstring(desktop_icons ? desktop_icons->generation() : 0);
    if (item.image_volume)
        version += L"#image-" + std::to_wstring(item.image_volume) + L"#" +
                   std::to_wstring(item.image_application_parent) + L"#" +
                   std::to_wstring(std::hash<std::string>{}(item.image_application));
    if (item.special == 4)
        version += L"#source-" + item.image_stamp;
    if (item.special == 3) {
        item.trash_full = recycle_bin_full();
        version += item.trash_full ? L"#full" : L"#empty";
    }
    if (found != icon_cache.end() && icon_versions[key] == version)
        return found->second;
    HICON previous = found == icon_cache.end() ? nullptr : found->second;
    HICON result = nullptr;
    FinderIcon data{};
    if (item.special == 4 && ROMlib_finder_image_volume_icon(item.image_volume, &data))
        result = icon(data, label);
    if (!result && item.image_volume && item.special != 4 &&
        ROMlib_finder_image_icon(item.image_volume, item.folder ? item.image_id : item.image_parent,
                                 item.image_name.c_str(), item.folder, item.mac.flags, item.mac.type, item.mac.creator,
                                 &data))
        result = icon(data, label);
    if (!result && item.image_volume && !item.image_application.empty() &&
        ROMlib_finder_image_icon(item.image_volume, item.image_application_parent, item.image_application.c_str(),
                                 false, 0, item.mac.type, item.mac.creator, &data))
        result = icon(data, label);
    if (item.special == 3)
        result = asset_icon(item.trash_full ? full_trash_asset : 3);
    if (item.special == windows_desktop_volume) {
        // Ask the shell for its Desktop namespace icon, independent of folder customizations.
        PIDLIST_ABSOLUTE desktop_id = nullptr;
        SHFILEINFOW native{};
        if (SUCCEEDED(SHGetSpecialFolderLocation(nullptr, CSIDL_DESKTOP, &desktop_id))) {
            if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(desktop_id), 0, &native, sizeof native,
                               SHGFI_PIDL | SHGFI_ICON | SHGFI_LARGEICON))
                result = native.hIcon;
            CoTaskMemFree(desktop_id);
        }
    }
    if (!result && item.special == 1 && item.drive_metadata && item.drive_metadata->custom.present)
        result = icon(item.drive_metadata->custom, label);
    if (!result && item.special != 1 && item.special != windows_desktop_volume && item.folder && !item.image_volume &&
        custom_folder_icon(item.path, data))
        result = icon(data, label);
    if (!result && !item.mac.fork.empty() && (item.mac.flags & 0x400) &&
        finder_parse_custom_icon(item.mac.fork.data(), item.mac.fork.size(), &data))
        result = icon(data, label);
    if (!result && item.special) {
        unsigned type = item.special == 2   ? FINDER_VOLUME_ROMAW
                        : item.special == 4 ? FINDER_VOLUME_IMAGE
                                            : FINDER_VOLUME_LOCAL;
        if (item.special == 4) {
            auto suffix = item.path.parent_path().extension().wstring();
            if (!_wcsicmp(suffix.c_str(), L".iso") || !_wcsicmp(suffix.c_str(), L".cdr") ||
                !_wcsicmp(suffix.c_str(), L".toast"))
                type = FINDER_VOLUME_OPTICAL;
        }
        if (item.special == 1)
            switch (GetDriveTypeW(item.path.c_str())) {
            case DRIVE_REMOTE:
                type = FINDER_VOLUME_SHARED;
                break;
            case DRIVE_REMOVABLE:
                type = FINDER_VOLUME_FLOPPY;
                break;
            case DRIVE_CDROM:
                type = FINDER_VOLUME_OPTICAL;
                break;
            }
        if (finder_volume_icon(type, &data))
            result = icon(data, label);
    }
    if (!result && !item.folder) {
        auto extension = item.path.extension().wstring();
        if (std::all_of(extension.begin(), extension.end(), [](wchar_t c) { return c > 0 && c < 128; })) {
            std::string suffix(extension.begin(), extension.end());
            if (finder_image_extension_icon(suffix.c_str(), &data))
                result = icon(data, label);
        }
    }
    if (!result && !item.folder && desktop_icons && desktop_icons->lookup(item.path, item.mac, data))
        result = icon(data, label);
    if (!result && !item.mac.fork.empty() &&
        finder_parse_icon(item.mac.fork.data(), item.mac.fork.size(), item.mac.creator, item.mac.type, &data))
        result = icon(data, label);
    if (!result && item.folder) {
        int kind = special_folder_kind(item.path, romaw_folder / L"System Folder");
        if (kind >= 0 && finder_special_folder_icon(unsigned(kind), &data))
            result = icon(data, label);
    }
    if (!result &&
        (!_wcsicmp(item.path.extension().c_str(), L".exe") || !_wcsicmp(item.path.extension().c_str(), L".lnk"))) {
        SHFILEINFOW native{};
        if (SHGetFileInfoW(item.path.c_str(), 0, &native, sizeof native, SHGFI_ICON))
            result = native.hIcon;
    }
    if (!result && !item.folder &&
        (!_wcsicmp(item.path.extension().c_str(), L".ico") || !_wcsicmp(item.path.extension().c_str(), L".icns"))) {
        try {
            auto bytes = item.image_volume ? Bytes{} : read_bytes(item.path, FINDER_ICON_FILE_LIMIT);
            std::array<unsigned char, 4096> pixels{};
            bool decoded = item.image_volume ? ROMlib_finder_image_preview(item.image_volume, item.image_parent,
                                                                           item.image_name.c_str(), pixels.data())
                                             : finder_decode_icon_file(bytes.data(), bytes.size(), 32, pixels.data());
            if (decoded) {
                std::array<unsigned char, 1024> small{};
                for (unsigned y = 0; y < 16; ++y)
                    for (unsigned x = 0; x < 16; ++x)
                        memcpy(small.data() + (y * 16 + x) * 4, pixels.data() + (y * 2 * 32 + x * 2) * 4, 4);
                if (label && finder_icon_from_rgba(pixels.data(), small.data(), &data)) {
                    // Palette roles choose which colours receive the classic
                    // label tint; keep the source RGB precision and alpha.
                    for (unsigned i = 0; i < 1024; ++i) {
                        uint16_t rgb[] = {uint16_t(pixels[i * 4] * 257), uint16_t(pixels[i * 4 + 1] * 257),
                                          uint16_t(pixels[i * 4 + 2] * 257)};
                        ROMlib_icon_label_rgb(rgb, classic_label_rgb[label], 8, data.color8[i]);
                        for (unsigned c = 0; c < 3; ++c)
                            pixels[i * 4 + c] = rgb[c] >> 8;
                    }
                }
                result = rgba_icon(pixels.data(), 32, 32);
                for (unsigned i = 0; i < 1024; ++i) {
                    uint16_t rgb[] = {uint16_t(pixels[i * 4] * 257), uint16_t(pixels[i * 4 + 1] * 257),
                                      uint16_t(pixels[i * 4 + 2] * 257)};
                    ROMlib_icon_selected_rgb(rgb);
                    for (unsigned c = 0; c < 3; ++c)
                        pixels[i * 4 + c] = rgb[c] >> 8;
                }
                HICON selected = rgba_icon(pixels.data(), 32, 32);
                if (result && selected)
                    selected_icon_cache[result] = selected;
                else if (selected)
                    DestroyIcon(selected);
            }
        }
        catch (...) {
            // Unreadable or malformed artwork retains the normal type fallback.
        }
    }
    if (!result && !item.folder && finder_type_icon(item.mac.type, &data))
        result = icon(data, label);
    if (!result)
        result = asset_icon(item.folder ? 1 : item.mac.kind == FINDER_CLASSIC_APPLICATION ? 2 : 0, label);
    icon_cache[key] = result;
    icon_versions[key] = version;
    if (previous && previous != result) {
        auto selected = selected_icon_cache.find(previous);
        if (selected != selected_icon_cache.end()) {
            DestroyIcon(selected->second);
            selected_icon_cache.erase(selected);
        }
        classic::replace_icon(previous, result);
        DestroyIcon(previous);
    }
    return result;
}

bool has_catalog_icon_position(const Item& item)
{
    return !item.special && item.mac.positioned && (item.mac.icon_x || item.mac.icon_y);
}

bool desktop_catalog_position(const FolderWindow& folder, const Item& item)
{
    return folder.desktop && has_catalog_icon_position(item);
}

void save_desktop_catalog_override(const Item& item, const std::wstring& position)
{
    save(L"DesktopCatalogPositionsNative", path_key(item.path),
         position + L"," + std::to_wstring(item.mac.icon_x) + L"," + std::to_wstring(item.mac.icon_y));
}

// Persist deliberate native moves against the imported point so a subsequent
// classic Finder move is visible on the next refresh.
void save_desktop_catalog_position(const FolderWindow& folder, int index, LONG x, LONG y)
{
    const auto& item = folder.items[index];
    if (desktop_catalog_position(folder, item))
        save_desktop_catalog_override(item, std::to_wstring(x) + L"," + std::to_wstring(y));
}

// Persist icon-view client coordinates using stable desktop/volume identities.
void save_positions(FolderWindow& f)
{
    if (ListView_GetView(f.list) != LV_VIEW_ICON)
        return;
    bool writable = f.image_volume && !ROMlib_finder_image_writable(f.image_volume);
    int16_t error = 0;
    std::vector<std::wstring> written;
    for (int i = 0; i < int(f.items.size()); ++i) {
        if (desktop_catalog_position(f, f.items[i]))
            continue;
        POINT p;
        ListView_GetItemPosition(f.list, i, &p);
        auto key = path_key(icon_position_path(f, f.items[i]));
        save(f.image_volume ? L"ImagePositionsNative" : L"PositionsNative", key,
             std::to_wstring(p.x) + L"," + std::to_wstring(p.y));
        if (writable && !error) {
            const auto& item = f.items[i];
            error = ROMlib_finder_image_position(f.image_volume, item.image_parent, item.image_name.c_str(), p.x - 16,
                                                 p.y - 8);
            if (!error)
                written.push_back(key);
        }
    }
    if (writable) {
        auto flush = ROMlib_finder_image_flush(f.image_volume);
        if (!error)
            error = flush;
        if (!flush)
            for (const auto& key : written)
                save(L"ImagePositionsNative", key, L"");
        if (error)
            message(desktop,
                    L"Icon positions were saved locally because the image could not be updated (Macintosh error " +
                        std::to_wstring(error) + L").",
                    MB_ICONERROR);
    }
}

// Translate native frame coordinates back into the catalog's classic content rectangle.
void save_window_geometry(FolderWindow& f)
{
    RECT r;
    GetWindowRect(f.window, &r);
    if (!f.desktop)
        MapWindowPoints(nullptr, desktop, reinterpret_cast<POINT*>(&r), 2);
    auto key = path_key(f.desktop ? fs::path(L"Desktop") : f.path);
    save(f.image_volume ? L"ImageWindowsNative" : L"WindowsNative", key,
         std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L"," + std::to_wstring(r.right - r.left) + L"," +
             std::to_wstring(r.bottom - r.top));
    if (f.image_volume && !ROMlib_finder_image_writable(f.image_volume)) {
        auto error = ROMlib_finder_image_window(f.image_volume, f.image_directory, r.left + 2, r.top + 39, r.right - 2,
                                                r.bottom + 18);
        if (!error)
            error = ROMlib_finder_image_flush(f.image_volume);
        if (!error)
            save(L"ImageWindowsNative", key, L"");
        else
            message(desktop,
                    L"The window size was saved locally because the image could not be updated (Macintosh error " +
                        std::to_wstring(error) + L").",
                    MB_ICONERROR);
    }
}

std::wstring file_date(FILETIME value);
std::wstring image_text(const char* text, unsigned code_page);
using DocumentKinds =
    std::map<std::tuple<fs::path, int16_t, uint32_t, uint32_t>, std::pair<std::wstring, std::wstring>>;

// File Manager paths use the Windows ANSI code page. Reject substitutions
// rather than looking up a different directory with a similar name.
std::string file_manager_path(const std::wstring& name)
{
    BOOL substituted = FALSE;
    bool utf8 = GetACP() == CP_UTF8;
    BOOL* used_default = utf8 ? nullptr : &substituted;
    DWORD flags = utf8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
    int length = WideCharToMultiByte(CP_ACP, flags, name.c_str(), -1, nullptr, 0, nullptr, used_default);
    if (!length || substituted)
        return {};
    std::string path(size_t(length), '\0');
    if (!WideCharToMultiByte(CP_ACP, flags, name.c_str(), -1, path.data(), length, nullptr, used_default) ||
        substituted)
        return {};
    path.pop_back();
    return path;
}

// Reuse creator lookups within one refresh instead of opening the same
// application's resource fork for each document of a given type.
std::wstring item_kind(const Item& item, DocumentKinds& kinds)
{
    if (item.special == windows_desktop_volume)
        return L"PC disk";
    if (!item.folder && item.mac.kind == FINDER_CLASSIC_DOCUMENT && item.mac.type && item.mac.creator &&
        !(item.mac.flags & 0x8000) && (!item.image_volume || !item.image_application.empty())) {
        auto directory = item.image_volume ? fs::path{} : item.path.parent_path();
        auto [entry, first] = kinds.try_emplace({directory, item.image_volume, item.mac.type, item.mac.creator});
        if (first) {
            auto host_directory = file_manager_path(directory.generic_wstring());
            char owner[256], custom[256];
            ROMlib_finder_document_kind(host_directory.empty() ? nullptr : host_directory.data(), item.image_volume,
                                        item.image_application_parent, item.image_application.c_str(), item.mac.type,
                                        item.mac.creator, owner, custom);
            entry->second = {image_text(custom, 10000), image_text(owner, 10000)};
        }
        return info_kind(item.path, item.mac, false, item.image_volume != 0, entry->second.first, entry->second.second);
    }
    return info_kind(item.path, item.mac, item.folder, item.image_volume != 0);
}

// Keep classic presentation metadata in host values after resource bytes are
// released. Missing attributes do not prevent the rest of a folder listing.
void describe_item(Item& item, DocumentKinds& kinds)
{
    auto& row = item.details;
    row.name = item.name;
    auto codes = info_codes(item.path, item.mac, item.folder, item.image_volume != 0);
    row.type = std::move(codes.type);
    row.creator = std::move(codes.creator);
    row.comments = item.mac.comment;
    row.label = (item.mac.flags >> 1) & 7;
    row.kind = item_kind(item, kinds);
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    bool have_attributes = false;
    if (item.special == 1) {
        if (item.drive_metadata) {
            attributes = item.drive_metadata->attributes;
            have_attributes = item.drive_metadata->attributes_known;
        }
    }
    else if (!item.image_volume && !item.path.empty())
        have_attributes = GetFileAttributesExW(item.path.c_str(), GetFileExInfoStandard, &attributes) != FALSE;
    if (have_attributes) {
        item.attributes_known = true;
        row.modified =
            (uint64_t(attributes.ftLastWriteTime.dwHighDateTime) << 32) | attributes.ftLastWriteTime.dwLowDateTime;
        if (!item.folder)
            row.size = ((uint64_t(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow) + item.mac.fork.size();
    }
    try {
        auto version = mac_resource(item.mac, 0x76657273);
        if (version.size() >= 7 && version[6] <= version.size() - 7) {
            row.version_number =
                (uint32_t(version[0]) << 24) | (uint32_t(version[1]) << 16) | (uint32_t(version[2]) << 8) | version[3];
            int length = MultiByteToWideChar(10000, 0, reinterpret_cast<const char*>(version.data() + 7), version[6],
                                             nullptr, 0);
            row.version.resize(length);
            if (length)
                MultiByteToWideChar(10000, 0, reinterpret_cast<const char*>(version.data() + 7), version[6],
                                    row.version.data(), length);
        }
    }
    catch (...) {
        // Bad optional vers resources do not make ordinary files unlistable.
    }
}

DWORD native_folder_view(FolderView view)
{
    return view == FolderView::icons ? LV_VIEW_ICON : view == FolderView::small_icons ? LV_VIEW_LIST : LV_VIEW_DETAILS;
}

const std::pair<unsigned, FolderView> folder_view_commands[] = {{icons_command, FolderView::icons},
                                                                {small_icons_command, FolderView::small_icons},
                                                                {list_command, FolderView::list}};

// Use classic STR# 3000 names plus the native Type and Creator columns.
// Large icon positions are saved independently of compact/list arrangements.
void configure_folder_view(FolderWindow& folder)
{
    const auto& mode = folder.presentation;
    ListView_SetView(folder.list, native_folder_view(mode.view));
    if (!ListView_GetHeader(folder.list) || Header_GetItemCount(ListView_GetHeader(folder.list)) == 0) {
        const int widths[] = {184, 148, 80, 120, 180, 100, 100, 76, 76};
        static_assert(std::size(widths) == folder_column_count &&
                      std::size(classic_view_columns) == folder_column_count);
        for (int index = 0; index < int(folder_column_count); ++index) {
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            column.pszText = const_cast<wchar_t*>(classic_view_columns[index]);
            column.cx = widths[index];
            column.fmt = index == 2 ? LVCFMT_RIGHT : LVCFMT_LEFT;
            ListView_InsertColumn(folder.list, index, &column);
        }
        SetWindowTheme(ListView_GetHeader(folder.list), L"", L"");
        SendMessageW(ListView_GetHeader(folder.list), WM_SETFONT, reinterpret_cast<WPARAM>(classic::small_font()),
                     TRUE);
    }
    HWND header = ListView_GetHeader(folder.list);
    for (int index = 0; index < int(folder_column_count); ++index) {
        HDITEMW item{};
        item.mask = HDI_FORMAT;
        Header_GetItem(header, index, &item);
        item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (index == int(mode.sort))
            item.fmt |= mode.descending ? HDF_SORTDOWN : HDF_SORTUP;
        Header_SetItem(header, index, &item);
    }
    if (mode.view == FolderView::small_icons)
        ListView_SetColumnWidth(folder.list, 0, 184);
}

void save_folder_view(const FolderWindow& folder)
{
    if (!folder.desktop && !folder.search) {
        auto key = path_key(folder.path);
        save(folder.image_volume ? L"ImageViewsNative" : L"ViewsNative", key, encode_folder_view(folder.presentation));
        if (folder.image_volume && !ROMlib_finder_image_writable(folder.image_volume)) {
            auto view = encode_catalog_view(folder.presentation);
            auto error = ROMlib_finder_image_view(folder.image_volume, folder.image_directory, view);
            if (!error)
                error = ROMlib_finder_image_flush(folder.image_volume);
            auto restored = decode_catalog_view(view);
            if (!error && encode_folder_view(restored) == encode_folder_view(folder.presentation))
                save(L"ImageViewsNative", key, L"");
            if (error)
                message(desktop,
                        L"The folder view was saved locally because the image could not be updated (Macintosh error " +
                            std::to_wstring(error) + L").",
                        MB_ICONERROR);
        }
    }
}

// Decode the catalog's MacRoman names or a backing image's host pathname.
std::wstring image_text(const char* text, unsigned code_page = 10000)
{
    int length = MultiByteToWideChar(code_page, 0, text, -1, nullptr, 0);
    if (!length)
        throw std::runtime_error("Cannot decode the mounted image name");
    std::wstring result(length, L'\0');
    MultiByteToWideChar(code_page, 0, text, -1, result.data(), length);
    result.pop_back();
    return result;
}

std::vector<FinderImageVolume> image_volumes()
{
    std::vector<FinderImageVolume> result;
    FinderImageVolume volume{};
    for (uint32_t index = 0; ROMlib_finder_image_volume(index, &volume); ++index)
        result.push_back(volume);
    return result;
}

fs::path image_volume_key(const FinderImageVolume& volume)
{
    // This settings identity is beneath a regular image file, never a host directory.
    return fs::path(image_text(volume.source, CP_ACP)) / (L".mounted-volume-" + std::to_wstring(volume.partition));
}

// Preserve selections and positions while rebuilding the desktop or folder list.
void refresh(FolderWindow& f)
{
    classic::RedrawBatch redraw(f.window);
    drag_highlight.clear(f.list);
    classic::WaitingCursor waiting;
    if (desktop_icons && !f.image_volume)
        desktop_icons->scan(f.desktop ? desktop_folder : f.path);
    auto old_selected = std::vector<fs::path>{};
    for (int i = -1; (i = selected_item_after(f.list, f.items.size(), i)) >= 0;)
        if (i < int(f.items.size()))
            old_selected.push_back(f.items[i].path);
    f.updating = true;
    SendMessageW(f.list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(f.list);
    ImageList_RemoveAll(f.images);
    ImageList_RemoveAll(f.small_images);
    f.items.clear();
    DocumentKinds kinds;
    auto prepare_item = [&](Item& item) {
        auto image = item_icon(item);
        item.icon = ImageList_AddIcon(f.images, image);
        ImageList_AddIcon(f.small_images, image);
        auto& selected = selected_icon_cache[image];
        if (!selected)
            selected = classic::selected_icon(image);
        item.selected_icon = ImageList_AddIcon(f.images, selected ? selected : image);
        ImageList_AddIcon(f.small_images, selected ? selected : image);
        describe_item(item, kinds);
        Bytes().swap(item.mac.fork);
    };
    auto add_item = [&](Item item) {
        if (!item.special) {
            EntryVisibility visibility;
            visibility.type = item.mac.type;
            visibility.creator = item.mac.creator;
            visibility.finder_flags = item.mac.flags;
            visibility.directory = item.folder;
            visibility.metadata = hidden_metadata(item.image_volume ? fs::path(item.name) : item.path);
            if (!item.image_volume)
                visibility.host_attributes = GetFileAttributesW(item.path.c_str());
            if (!folder_item_visible(visibility, show_hidden))
                return;
        }
        if (!item.image_volume || item.special)
            prepare_item(item);
        f.items.push_back(std::move(item));
    };
    auto append = [&](fs::path path, std::wstring name, int special = 0) {
        Item item;
        item.path = std::move(path);
        item.name = std::move(name);
        item.special = special;
        item.folder = special == 1 || special == 3 || fs::is_directory(item.path);
        if (!special && item.folder) {
            auto attributes = GetFileAttributesW(item.path.c_str());
            item.can_expand = attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
        }
        if (special == 1) {
            item.drive_metadata = request_drive_metadata(item.path);
            if (item.drive_metadata)
                item.mac = item.drive_metadata->mac;
        }
        else if (special != 3 && special != windows_desktop_volume)
            try {
                item.mac = mac_file(item.path);
            }
            catch (...) {
            }
        if (!special)
            item.name = host_icon_name(item.path, item.mac);
        add_item(std::move(item));
    };
    if (f.desktop) {
        wchar_t drives[512];
        DWORD n = GetLogicalDriveStringsW(std::size(drives), drives);
        if (n && n < std::size(drives))
            for (const wchar_t* p = drives; *p; p += wcslen(p) + 1)
                append(p, volume_display_name(p, std::wstring(1, *p)), 1);
        if (fs::is_directory(romaw_folder))
            append(romaw_folder, L"ROMAW", 2);
        std::error_code desktop_error;
        if (fs::is_directory(windows_desktop_path, desktop_error))
            append(windows_desktop_path, windows_desktop_name, windows_desktop_volume);
        for (const auto& volume : image_volumes()) {
            Item item;
            item.path = image_volume_key(volume);
            item.name = image_text(volume.name);
            item.special = 4;
            item.image_stamp = icon_source_stamp(image_text(volume.source, CP_ACP), false);
            item.folder = true;
            item.image_volume = volume.reference;
            add_item(std::move(item));
        }
    }
    auto path = f.desktop ? desktop_folder : f.path;
    std::error_code error;
    auto append_catalog = [&](fs::path key, int16_t volume, const FinderImageEntry& entry) {
        if (image_copy_private(volume, entry.id))
            return;
        Item item;
        item.path = std::move(key);
        item.name = image_text(entry.name);
        item.image_name = entry.name;
        item.image_volume = volume;
        item.image_id = entry.id;
        item.image_parent = entry.parent;
        item.folder = entry.directory;
        item.can_expand = item.folder;
        item.mac = mac_catalog_info(entry.finder_info, item.folder);
        if (!item.folder)
            item.mac.kind = entry.type == 0x4150504c || entry.type == 0x464e4452   ? FINDER_CLASSIC_APPLICATION
                            : entry.type == 0x63646576                             ? FINDER_CONTROL_PANEL
                            : entry.type == 0x6466696c || entry.type == 0x4446494c ? FINDER_DESK_ACCESSORY
                                                                                   : FINDER_CLASSIC_DOCUMENT;
        item.details.size = uint64_t(entry.data_size) + entry.resource_size;
        item.details.modified = (uint64_t(entry.modified) + 9561628800ULL) * 10000000;
        item.attributes_known = true;
        add_item(std::move(item));
    };
    auto image_children = [&](const fs::path& key, uint32_t directory) {
        for (int index = 1; index <= INT16_MAX; ++index) {
            FinderImageEntry entry{};
            int16_t error = ROMlib_finder_image_entry(f.image_volume, directory, index, &entry);
            if (error) {
                if (error != -43 && directory == f.image_directory)
                    message(f.window,
                            L"Cannot read this image directory (Macintosh error " + std::to_wstring(error) + L").",
                            MB_ICONERROR);
                break;
            }
            append_catalog(key / std::to_wstring(entry.id), f.image_volume, entry);
        }
    };
    auto host_children = [&](const fs::path& folder) {
        std::error_code status;
        for (const auto& entry : fs::directory_iterator(folder, fs::directory_options::skip_permission_denied, status))
            if (show_hidden || !hidden_metadata(entry.path()))
                append(entry.path(), entry.path().filename().wstring());
    };
    if (f.search) {
        f.search_finished = f.search->finished();
        auto found =
            !f.search->options.all_at_once || f.search_finished ? f.search->snapshot() : std::vector<SearchMatch>{};
        f.search_count = found.size();
        std::map<int16_t, fs::path> mounted_keys;
        if (std::any_of(found.begin(), found.end(), [](const auto& match) { return match.image != 0; }))
            for (const auto& volume : image_volumes())
                mounted_keys.emplace(volume.reference, image_volume_key(volume));
        for (const auto& match : found) {
            if (match.image) {
                auto volume = mounted_keys.find(match.image);
                fs::path mounted_path;
                if (volume == mounted_keys.end() ||
                    !relocated_path(match.path, {volume->second, volume->second}, mounted_path))
                    continue;
                FinderImageEntry entry{};
                if (!ROMlib_finder_image_info(match.image, match.catalog.parent, match.catalog.name, &entry) &&
                    entry.id == match.catalog.id)
                    append_catalog(match.path, match.image, entry);
            }
            else if (fs::exists(match.path, error))
                append(match.path, match.path.filename().wstring());
        }
    }
    else if (f.image_volume)
        image_children(path, f.image_directory);
    else
        host_children(path);
    if (f.desktop)
        append({}, L"Trash", 3);
    bool outline = f.presentation.view == FolderView::list && !f.search && !f.desktop;
    if (outline)
        for (size_t i = 0; i < f.items.size(); ++i) {
            const auto& item = f.items[i];
            if (!item.can_expand || item.outline_depth >= 32 || !f.expanded_folders.count(item.path))
                continue;
            auto child_key = item.path;
            auto depth = item.outline_depth + 1;
            auto first_child = f.items.size();
            if (item.image_volume)
                image_children(child_key, item.image_id);
            else
                host_children(child_key);
            for (size_t child = first_child; child < f.items.size(); ++child)
                f.items[child].outline_depth = depth;
        }
    // Register visible application identities before drawing documents. Only
    // names and parent IDs are shared; their resource bytes remain on the image.
    std::map<std::pair<int16_t, uint32_t>, const Item*> image_applications;
    for (const auto& item : f.items)
        if (item.image_volume && item.mac.kind == FINDER_CLASSIC_APPLICATION)
            image_applications.emplace(std::make_pair(item.image_volume, item.mac.creator), &item);
    std::map<std::pair<int16_t, uint32_t>, FinderImageEntry> desktop_applications;
    std::set<std::pair<int16_t, uint32_t>> checked_creators;
    for (auto& item : f.items) {
        if (!item.image_volume || item.special)
            continue;
        auto creator_key = std::make_pair(item.image_volume, item.mac.creator);
        if (!item.folder && item.mac.kind == FINDER_CLASSIC_DOCUMENT && checked_creators.insert(creator_key).second) {
            FinderImageEntry application{};
            if (ROMlib_finder_image_application(item.image_volume, item.mac.creator, &application))
                desktop_applications.emplace(creator_key, application);
        }
        auto stored = desktop_applications.find(creator_key);
        if (stored != desktop_applications.end()) {
            item.image_application = stored->second.name;
            item.image_application_parent = stored->second.parent;
        }
        item.mac.comment = image_search_comment(item.image_volume, item.image_id);
        auto application = image_applications.find({item.image_volume, item.mac.creator});
        if (item.image_volume && !item.folder && item.mac.kind != FINDER_CLASSIC_APPLICATION &&
            item.image_application.empty() && application != image_applications.end()) {
            item.image_application = application->second->image_name;
            item.image_application_parent = application->second->image_parent;
        }
        prepare_item(item);
        if (f.search && f.search->options.query.field == FindField::version) {
            unsigned char version[512]{};
            size_t length = 0;
            if (ROMlib_finder_image_version(item.image_volume, item.image_parent, item.image_name.c_str(), version,
                                            sizeof version, &length))
                item.details.version = find_version(version, length);
        }
    }
    if (f.presentation.view != FolderView::icons)
        std::stable_sort(f.items.begin(), f.items.end(), [&](const Item& left, const Item& right) {
            return folder_entry_less(left.details, right.details, f.presentation.sort, f.presentation.descending);
        });
    if (outline && !f.expanded_folders.empty()) {
        // Preserve the chosen sort within each sibling group. Children
        // appear directly beneath their expanded parent.
        std::map<fs::path, std::vector<size_t>> siblings;
        for (size_t i = 0; i < f.items.size(); ++i)
            siblings[f.items[i].path.parent_path()].push_back(i);
        std::vector<Item> ordered;
        ordered.reserve(f.items.size());
        auto emit = [&](const auto& self, const fs::path& parent) -> void {
            auto rows = siblings.find(parent);
            if (rows == siblings.end())
                return;
            for (size_t index : rows->second) {
                auto key = f.items[index].path;
                ordered.push_back(std::move(f.items[index]));
                self(self, key);
            }
        };
        emit(emit, path);
        f.items = std::move(ordered);
    }
    RECT area;
    GetClientRect(f.list, &area);
    if (!f.desktop) {
        area.right -= 16;
        area.bottom -= 16;
    }
    std::set<std::pair<int, int>> occupied;
    auto reserve = [&](int x, int y, bool insert) {
        bool used = false;
        for (int column = (x - 16) / icon_cell_width; column <= (x - 16 + icon_cell_width - 9) / icon_cell_width;
             ++column)
            for (int row = y / icon_cell_height; row <= (y + icon_cell_height - 9) / icon_cell_height; ++row) {
                if (occupied.count({column, row}))
                    used = true;
                if (insert)
                    occupied.insert({column, row});
            }
        return used;
    };
    std::vector<POINT> positions(f.items.size(), POINT{-1, -1});
    // Reserve restored positions first so newly appearing items never cover them.
    for (size_t i = 0; i < f.items.size(); ++i) {
        const auto& item = f.items[i];
        int x, y, source_x, source_y;
        bool desktop_catalog = desktop_catalog_position(f, item);
        auto position_path = desktop_catalog ? item.path : icon_position_path(f, item);
        const wchar_t* section = desktop_catalog  ? L"DesktopCatalogPositionsNative"
                                 : f.image_volume ? L"ImagePositionsNative"
                                                  : L"PositionsNative";
        auto saved = settings_path_value(preferences, section, position_path);
        if (!f.image_volume && saved.empty() && !desktop_catalog && (f.desktop || !item.mac.positioned))
            saved = settings_path_value(preferences, L"Positions", position_path);
        int fields = swscanf(saved.c_str(), L"%d,%d,%d,%d", &x, &y, &source_x, &source_y);
        bool valid = fields >= 2 && x >= 0 && y >= 0 && x < 1048576 && y < 1048576;
        if (desktop_catalog)
            valid = valid && fields == 4 && source_x == item.mac.icon_x && source_y == item.mac.icon_y;
        if (!valid) {
            if (f.search || !item.mac.positioned || (f.desktop && !desktop_catalog))
                continue;
            x = std::max(0, 16 + item.mac.icon_x);
            y = std::max(0, 8 + item.mac.icon_y);
            // A classic desktop can be larger than the host window.
            if (f.desktop && (x > area.right - 32 || y > area.bottom - 32))
                continue;
        }
        positions[i] = POINT{x, y};
        reserve(x, y, true);
    }
    unsigned ordinary = 0, volumes = 0;
    for (int i = 0; i < int(f.items.size()); ++i) {
        const auto& item = f.items[i];
        LVITEMW row{};
        row.mask = LVIF_TEXT | LVIF_IMAGE;
        row.iItem = i;
        row.pszText = const_cast<wchar_t*>(item.name.c_str());
        row.iImage = item.icon;
        if (outline) {
            row.mask |= LVIF_INDENT;
            row.iIndent = item.outline_depth + 1;
        }
        ListView_InsertItem(f.list, &row);
        if (f.presentation.view == FolderView::list) {
            FILETIME date{DWORD(item.details.modified), DWORD(item.details.modified >> 32)};
            const std::wstring values[] = {item.details.modified ? file_date(date) : L"--",
                                           item.folder || !item.attributes_known ? L"--"
                                                                                 : format_bytes(item.details.size),
                                           item.details.kind,
                                           item.details.comments,
                                           label_menu_names[item.details.label],
                                           item.details.version,
                                           item.details.type,
                                           item.details.creator};
            for (int column = 1; column < int(folder_column_count); ++column)
                ListView_SetItemText(f.list, i, column, const_cast<wchar_t*>(values[column - 1].c_str()));
        }
        if (std::find(old_selected.begin(), old_selected.end(), item.path) != old_selected.end())
            ListView_SetItemState(f.list, i, LVIS_SELECTED, LVIS_SELECTED);
        int x = positions[i].x, y = positions[i].y;
        if (x < 0) {
            if (f.desktop && item.special) {
                unsigned rows = std::max(1L, (area.bottom - 80) / icon_cell_height);
                do {
                    x = std::max(0L, area.right - 60 - LONG(volumes / rows) * icon_cell_width);
                    y = item.special == 3 ? std::max(0L, area.bottom - 80) : 8 + (volumes % rows) * icon_cell_height;
                    ++volumes;
                } while (reserve(x, y, false) && x > 0);
            }
            if (!f.desktop || !item.special || reserve(x, y, false)) {
                unsigned columns = std::max(1L, (area.right - 68) / icon_cell_width + 1);
                do {
                    x = 16 + (ordinary % columns) * icon_cell_width;
                    y = 8 + (ordinary / columns) * icon_cell_height;
                    ++ordinary;
                } while (reserve(x, y, false));
            }
            reserve(x, y, true);
        }
        if (f.presentation.view == FolderView::icons)
            ListView_SetItemPosition32(f.list, i, std::max(0, x), std::max(0, y));
    }
    if (!f.desktop)
        ShowScrollBar(f.list, SB_BOTH, TRUE);
    ULARGE_INTEGER free{}, total{};
    f.read_only = f.image_volume != 0;
    if (f.image_volume) {
        for (const auto& volume : image_volumes())
            if (volume.reference == f.image_volume) {
                free.QuadPart = volume.free_bytes;
                total.QuadPart = volume.total_bytes;
                f.read_only = volume.read_only != 0;
                break;
            }
    }
    else if (!f.search)
        GetDiskFreeSpaceExW(path.c_str(), &free, &total, nullptr);
    SetWindowTextW(f.status, (std::to_wstring(f.items.size()) +
                              (f.search && !f.search_finished             ? L" found (searching...)"
                               : f.search && f.search->limited            ? L" found (limit reached)"
                               : f.search && !f.search->problem().empty() ? L" found (incomplete search)"
                               : f.search                                 ? L" found"
                               : f.items.size() == 1                      ? L" item"
                                                                          : L" items"))
                                 .c_str());
    SetWindowTextW(
        f.used, f.search
                    ? L""
                    : (format_bytes(total.QuadPart > free.QuadPart ? total.QuadPart - free.QuadPart : 0) + L" in disk")
                          .c_str());
    SetWindowTextW(f.available, f.search ? L"" : (format_bytes(free.QuadPart) + L" available").c_str());
    SendMessageW(f.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(f.list, nullptr, TRUE);
    f.updating = false;
}

// Snapshot selected host paths so commands do not depend on list indexes after
// a refresh.
std::vector<fs::path> selected(FolderWindow& f)
{
    std::vector<fs::path> out;
    if (!live_folder(&f))
        return out;
    for (int i = -1; (i = selected_item_after(f.list, f.items.size(), i)) >= 0;)
        if (i < int(f.items.size()) && f.items[i].special != 3 && f.items[i].special != windows_desktop_volume &&
            !f.items[i].image_volume)
            out.push_back(f.items[i].path);
    return out;
}

void open_folder(const fs::path&, std::shared_ptr<Search> = nullptr, int16_t image_volume = 0,
                 uint32_t image_directory = 2, const std::wstring& image_title = {});
void show_operation(std::shared_ptr<CopyJob>, DWORD clipboard_sequence = 0,
                    std::function<void(const CopyJob&)> on_complete = {}, std::optional<DropPlacement> drop = {},
                    std::optional<Extraction> extraction = {});

// Start an independent background copy and its operation window, reporting
// validation errors through the owning folder.
void copy_to(HWND owner, const std::vector<fs::path>& paths, const fs::path& folder,
             TransferKind kind = TransferKind::copy, DWORD clipboard_sequence = 0,
             std::optional<DropPlacement> drop = {})
{
    try {
        bool archive_folders =
            windows_destination(folder) && std::any_of(paths.begin(), paths.end(), [](const auto& path) {
                return fs::is_directory(path) && !windows_destination(path);
            });
        int archive_format = archive_folders ? pick_folder_archive_format(owner) : 0;
        if (archive_folders && !archive_format)
            return;
        if (auto job = copies.start(paths, folder, kind, archive_folders, archive_format == 2))
            show_operation(job, clipboard_sequence, {}, drop);
    }
    catch (const std::exception& e) {
        failure(owner, e);
    }
}

// Preserve the classic same-volume move / cross-volume copy default. A mixed
// selection may create two independent jobs, each with its own cancellation.
void drag_to(HWND owner, const std::vector<fs::path>& paths, const fs::path& folder, bool option, bool shift,
             std::optional<DropPlacement> drop)
{
    std::vector<fs::path> moving, copying;
    for (const auto& path : paths)
        (drag_copies(same_storage_volume(path, folder), option, shift) ? copying : moving).push_back(path);
    if (!moving.empty())
        copy_to(owner, moving, folder, TransferKind::move, 0, drop);
    if (drop)
        drop->first += moving.size();
    if (!copying.empty())
        copy_to(owner, copying, folder, TransferKind::copy, 0, drop);
}

// Launch classic software with this same ROMAW binary; no saved runtime
// override is consulted.
fs::path runtime_path()
{
    wchar_t path[32768];
    DWORD length = GetModuleFileNameW(nullptr, path, std::size(path));
    if (!length || length >= std::size(path))
        throw std::runtime_error("Cannot locate ROMAW");
    return path;
}

// Route host files through Windows associations and classic items through the
// internal launcher; retain successful child handles for later cleanup.
void launch(HWND owner, const Item& item, const std::vector<fs::path>& documents = {})
{
    if (!item.folder && (romaw_archive_extension(fs::path(item.name).extension().u8string().c_str()) ||
                         romaw_archive_type(item.mac.type)))
        throw std::runtime_error("Finder owns this archive type. Use Extract Here or Extract To.");
    if (!item.mac.kind) {
        std::wstring args;
        for (const auto& p : documents) {
            if (!args.empty())
                args += L" ";
            args += quote_argument(p.wstring());
        }
        auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
            owner, L"open", item.path.c_str(), args.empty() ? nullptr : args.c_str(), nullptr, SW_SHOWNORMAL));
        if (result <= 32)
            message(owner, L"Windows could not open this item.", MB_ICONERROR);
        return;
    }
    auto runtime = runtime_path();
    std::vector<const wchar_t*> paths;
    for (const auto& p : documents)
        paths.push_back(p.c_str());
    std::vector<std::pair<std::wstring, int>> selections;
    for (uint32_t i = 0;; ++i) {
        int mode;
        const char* source = ROMlib_finder_image_mount(i, &mode);
        if (!source)
            break;
        selections.emplace_back(image_text(source, CP_ACP), mode);
    }
    std::vector<FinderImageMount> mounts;
    for (const auto& selection : selections)
        mounts.push_back({selection.first.c_str(), selection.second});
    FinderLaunchRequest request{sizeof request,         uint32_t(item.mac.kind), runtime.c_str(), item.path.c_str(),
                                paths.data(),           uint32_t(paths.size()),  item.mac.driver, mounts.data(),
                                uint32_t(mounts.size())};
    FinderLaunchResult result{};
    result.size = sizeof result;
    auto error = FinderLaunch(&request, &result);
    if (error)
        message(owner, error_text(error), MB_ICONERROR);
    else
        children.push_back(reinterpret_cast<HANDLE>(result.process_handle));
}

// Resolve the current catalog identity without materializing either fork.
std::string image_item_path(const Item& item)
{
    char* raw = nullptr;
    if (ROMlib_finder_image_path(item.image_volume, item.image_parent, item.image_name.c_str(), &raw))
        throw std::runtime_error("Cannot resolve the file in this mounted image");
    std::unique_ptr<char, decltype(&free)> path(raw, free);
    return raw;
}

const Item* selected_application(FolderWindow& folder, bool include_windows = false)
{
    if (ListView_GetSelectedCount(folder.list) != 1)
        return nullptr;
    int index = selected_item_after(folder.list, folder.items.size(), -1);
    if (index < 0)
        return nullptr;
    const auto& item = folder.items[index];
    if (item.folder || item.special)
        return nullptr;
    bool native = include_windows && !item.image_volume && !item.mac.kind &&
                  !_wcsicmp(item.path.extension().c_str(), L".exe");
    return item.mac.kind == FINDER_CLASSIC_APPLICATION || native ? &item : nullptr;
}

// Publish a Windows link on the shell Desktop. Keep the ICO in Finder's profile
// so moving the link elsewhere in Windows does not break its artwork.
bool save_shortcut_artwork(Item& item, const fs::path& path);

void make_windows_shortcut(Item item)
{
    if (!fs::is_directory(windows_desktop_path))
        throw std::runtime_error("The Windows Desktop is unavailable.");
    fs::path target = item.path;
    std::wstring arguments;
    auto name = item.name;
    if (item.mac.kind == FINDER_CLASSIC_APPLICATION) {
        target = runtime_path();
        auto operand = item.path.wstring();
        if (item.image_volume) {
            auto volumes = image_volumes();
            auto volume = std::find_if(volumes.begin(), volumes.end(),
                                       [&](const auto& value) { return value.reference == item.image_volume; });
            if (volume == volumes.end())
                throw std::runtime_error("The application's disk image is no longer mounted.");
            arguments = volume->read_only ? L"-mount-ro " : L"-mount ";
            arguments += quote_argument(image_text(volume->source, CP_ACP)) + L" ";
            operand = image_text(image_item_path(item).c_str());
        }
        arguments += finder_launch_file_argument(operand.c_str());
    }
    else
        name = item.path.stem().wstring();
    // Classic volume names may contain characters Windows forbids in filenames.
    for (auto& character : name)
        if (character < 32 || wcschr(L"\\/:*?\"<>|", character))
            character = L'_';
    auto link = unique_item(windows_desktop_path, name + L".lnk");
    auto icons = preferences.parent_path() / L"Shortcut Icons";
    fs::create_directories(icons);
    auto artwork = unique_item(icons, name + L".ico");
    if (!save_shortcut_artwork(item, artwork))
        throw std::runtime_error("The application's Windows shortcut icon could not be saved.");
    try {
        create_shortcut(target, link, arguments, artwork);
    }
    catch (...) {
        DeleteFileW(artwork.c_str());
        throw;
    }
    SHChangeNotify(SHCNE_CREATE, SHCNF_PATHW, link.c_str(), nullptr);
    history.created(link, L"Make Windows Shortcut");
    for (auto* folder : folders)
        if (!folder->desktop && !folder->image_volume &&
            _wcsicmp(folder->path.c_str(), windows_desktop_path.c_str()) == 0)
            refresh(*folder);
}

// Ask once per drop. The caller keeps its original transfer selection for No.
int offer_windows_shortcuts(HWND owner, const std::vector<Item>& applications)
{
    if (applications.empty())
        return IDNO;
    std::wstring question =
        applications.size() == 1
            ? L"“" + applications.front().name + L"” is a Macintosh application. Make a Windows shortcut instead?"
            : L"This selection includes Macintosh applications. Make Windows shortcuts for them instead?";
    auto answer = classic::yes_no_cancel_box(owner, question.c_str());
    if (answer == IDYES) {
        classic::WaitingCursor waiting;
        for (const auto& item : applications)
            make_windows_shortcut(item);
    }
    return answer;
}

void drop_host_items(HWND owner, std::vector<fs::path> paths, fs::path target, std::optional<DropPlacement> drop = {})
{
    const bool option = GetKeyState(VK_MENU) < 0 || GetKeyState(VK_CONTROL) < 0;
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    if (_wcsicmp(target.c_str(), windows_desktop_path.c_str()) == 0) {
        std::vector<Item> applications;
        for (const auto& path : paths) {
            try {
                auto metadata = mac_file(path);
                if (metadata.kind == FINDER_CLASSIC_APPLICATION && !fs::is_directory(path)) {
                    Item item;
                    item.path = path;
                    item.name = mac_display_name(path, metadata);
                    item.mac = std::move(metadata);
                    applications.push_back(std::move(item));
                }
            }
            catch (...) {
                // Ordinary files still use the normal transfer path if metadata is unavailable.
            }
        }
        int answer = offer_windows_shortcuts(owner, applications);
        if (answer == IDCANCEL)
            return;
        if (answer == IDYES)
            paths.erase(std::remove_if(paths.begin(), paths.end(),
                                       [&](const auto& path) {
                                           return std::any_of(applications.begin(), applications.end(),
                                                              [&](const auto& app) { return app.path == path; });
                                       }),
                        paths.end());
    }
    drag_to(IsWindow(owner) ? owner : desktop, paths, target, option, shift, drop);
}

void open_item(HWND owner, Item item);

// Opening a pad creates a regular document. Publish both forks and clear the
// pad flag before dispatching the new document to its application.
void open_stationery(HWND owner, const Item& item)
{
    bool in_image = item.image_volume && !ROMlib_finder_image_writable(item.image_volume);
    auto name = in_image ? item.name.substr(0, 26) + L" copy"
                         : fs::path(item.name).stem().wstring() + L" copy" + fs::path(item.name).extension().wstring();
    if (!classic::input_box(owner,
                            item.image_volume && !in_image
                                ? L"Name the new document to create on the desktop."
                                : L"Name the new document to create from this stationery pad.",
                            name))
        return;
    if (in_image) {
        auto catalog_name = image_catalog_name(name);
        auto parent = item.path.parent_path();
        auto job = copy_into_image(
            copies, {}, {{item.path, 0, {}, item.image_volume, {item.image_parent, item.image_id, item.image_name}}},
            item.image_volume, item.image_parent, parent, TransferKind::stationery, catalog_name);
        show_operation(job, 0, [document = item, catalog_name, parent](const CopyJob&) mutable {
            FinderImageEntry entry{};
            if (ROMlib_finder_image_info(document.image_volume, document.image_parent, catalog_name.c_str(), &entry))
                throw std::runtime_error("Cannot open the new document.");
            document.image_id = entry.id;
            document.image_name = entry.name;
            document.name = image_text(entry.name);
            document.path = parent / std::to_wstring(entry.id);
            document.mac = mac_catalog_info(entry.finder_info, false);
            document.mac.kind = FINDER_CLASSIC_DOCUMENT;
            open_item(desktop, std::move(document));
        });
        return;
    }
    if (name.empty() || name == L"." || name == L".." || name.back() == L'.' || name.back() == L' ' ||
        name.find_first_of(L"<>:\"/\\|?*") != std::wstring::npos)
        throw std::runtime_error("Choose a valid document name.");
    std::shared_ptr<CopyJob> job;
    if (item.image_volume) {
        auto volumes = image_volumes();
        auto volume = std::find_if(volumes.begin(), volumes.end(),
                                   [&](const auto& entry) { return entry.reference == item.image_volume; });
        if (volume == volumes.end())
            throw std::runtime_error("The stationery image is no longer mounted.");
        auto path = image_item_path(item);
        auto relative = path.find(':');
        if (relative == std::string::npos)
            throw std::runtime_error("Cannot resolve the stationery pad.");
        job = copy_image_items(
            copies,
            {{fs::path(image_text(volume->source, CP_ACP)), unsigned(volume->partition), path.substr(relative)}},
            desktop_folder, name);
    }
    else
        job = copies.start_targets({{item.path, item.path.parent_path() / name}}, TransferKind::stationery);
    show_operation(job, 0, [](const CopyJob& completed) {
        Item document;
        document.path = completed.destinations.front();
        document.mac = mac_file(document.path);
        open_item(desktop, std::move(document));
    });
}

constexpr wchar_t suitcase_window_class[] = L"FinderSuitcase";

struct SuitcaseView {
    Item item;
    std::vector<SuitcaseMember> members;
    std::vector<SuitcaseMember> dragged;
    POINT grab{};
    std::vector<classic::IconViewImage> artwork;
    HWND list = nullptr;
    HIMAGELIST images = nullptr, small_images = nullptr;
    FolderView mode = FolderView::icons;
    bool owned = false;

    ~SuitcaseView()
    {
        if (images)
            ImageList_Destroy(images);
        if (small_images)
            ImageList_Destroy(small_images);
    }
};

SuitcaseView* suitcase_view(HWND window)
{
    wchar_t type[32]{};
    if (!GetClassNameW(window, type, std::size(type)) || wcscmp(type, suitcase_window_class))
        return nullptr;
    return reinterpret_cast<SuitcaseView*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

SuitcaseView* active_suitcase()
{
    return suitcase_view(classic::active_window());
}

SuitcaseView* suitcase_at(HWND window)
{
    for (; window && window != desktop; window = GetParent(window)) {
        if (auto* view = suitcase_view(window))
            return view;
        if (!(GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD))
            break;
    }
    return nullptr;
}

void paint_suitcase_icons(HDC dc, void* context)
{
    auto& view = *static_cast<SuitcaseView*>(context);
    const bool large = view.mode == FolderView::icons;
    classic::paint_icon_view(view.list, dc, large ? view.images : view.small_images, view.artwork.data(),
                             view.artwork.size(), false);
    auto previous = SelectObject(dc, classic::small_font());
    RECT client{};
    GetClientRect(view.list, &client);
    SetBkMode(dc, TRANSPARENT);
    for (int index = 0; index < int(view.members.size()); ++index) {
        RECT bounds{};
        if (!ListView_GetItemRect(view.list, index, &bounds, LVIR_LABEL) || !RectVisible(dc, &bounds))
            continue;
        const auto& name = view.members[index].name;
        if (large)
            bounds = classic::icon_label_bounds(dc, name.c_str(), bounds, &client);
        bool selected = ListView_GetItemState(view.list, index, LVIS_SELECTED) != 0;
        classic::fill_rect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(selected ? BLACK_BRUSH : WHITE_BRUSH)));
        SetTextColor(dc, selected ? RGB(255, 255, 255) : RGB(0, 0, 0));
        classic::draw_text(dc, name.c_str(), -1, &bounds,
                           DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | (large ? DT_CENTER : DT_LEFT));
    }
    SelectObject(dc, previous);
}

LRESULT CALLBACK suitcase_list_proc(HWND list, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data)
{
    auto* view = reinterpret_cast<SuitcaseView*>(data);
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(list, suitcase_list_proc, id);
    if (msg == WM_LBUTTONDOWN) {
        view->grab = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ClientToScreen(list, &view->grab);
    }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE && !view->dragged.empty()) {
        SendMessageW(GetParent(list), WM_CANCELMODE, 0, 0);
        return 0;
    }
    if (view->mode != FolderView::list && msg == WM_ERASEBKGND)
        return 1;
    if (view->mode != FolderView::list && (msg == WM_PAINT || msg == WM_PRINTCLIENT)) {
        if (classic::redraw_suspended(list))
            return 0;
        PAINTSTRUCT paint{};
        HDC dc = msg == WM_PAINT ? BeginPaint(list, &paint) : reinterpret_cast<HDC>(wp);
        classic::paint_view(list, dc, paint_suitcase_icons, view);
        if (msg == WM_PAINT)
            EndPaint(list, &paint);
        return 0;
    }
    return DefSubclassProc(list, msg, wp, lp);
}

void arrange_suitcase_icons(const SuitcaseView& view)
{
    RECT client{};
    GetClientRect(view.list, &client);
    HDC dc = GetDC(view.list);
    auto font = dc ? SelectObject(dc, classic::small_font()) : nullptr;
    std::vector<IconCleanupEntry> entries;
    entries.reserve(view.members.size());
    for (const auto& member : view.members) {
        auto label = dc ? classic::icon_label_bounds(dc, member.name.c_str(), {}) : RECT{};
        entries.push_back({member.name, {}, false, int(label.right - label.left)});
    }
    if (dc) {
        SelectObject(dc, font);
        ReleaseDC(view.list, dc);
    }
    const auto positions =
        clean_icon_layout(entries, IconCleanup::name, client.right, icon_cell_width, icon_cell_height);
    for (int i = 0; i < int(positions.size()); ++i)
        ListView_SetItemPosition32(view.list, i, positions[i].x, positions[i].y);
}

void set_suitcase_view(SuitcaseView& view, FolderView mode)
{
    classic::RedrawBatch redraw(view.list);
    view.mode = mode;
    ListView_SetView(view.list, native_folder_view(mode));
    if (mode == FolderView::icons)
        arrange_suitcase_icons(view);
    InvalidateRect(view.list, nullptr, TRUE);
}

// A suitcase lists resources, so file commands must never fall through to
// the previously active folder and act on its selection.
constexpr unsigned unavailable_suitcase_commands[] = {open_command,
                                                      new_folder_command,
                                                      duplicate_command,
                                                      info_command,
                                                      rename_command,
                                                      trash_command,
                                                      copy_command,
                                                      cut_command,
                                                      paste_command,
                                                      clear_command,
                                                      alias_command,
                                                      windows_shortcut_command,
                                                      configure_app_command,
                                                      put_away_command,
                                                      parent_command,
                                                      extract_here_command,
                                                      extract_to_command,
                                                      print_command,
                                                      print_window_command,
                                                      eject_command,
                                                      erase_command,
                                                      arrange_command,
                                                      arrange_name_command,
                                                      arrange_selection_command,
                                                      special_cleanup_command,
                                                      label_popup_command};

constexpr uint32_t sound_resource_type = 0x736e6420; // snd
constexpr uint32_t sound_file_type = 0x7366696c; // sfil

bool previewable_member(const SuitcaseMember& member)
{
    return is_preview_font(member.resource_type) || member.resource_type == sound_resource_type;
}

void preview_suitcase_member(const Item& item, const SuitcaseMember& member);

int selected_preview_member(const SuitcaseView& view)
{
    int selected = selected_item_after(view.list, view.members.size(), -1);
    if (selected < 0 || !previewable_member(view.members[selected]) ||
        selected_item_after(view.list, view.members.size(), selected) >= 0)
        return -1;
    return selected;
}

bool suitcase_blocks_command(unsigned id)
{
    return std::find(std::begin(unavailable_suitcase_commands), std::end(unavailable_suitcase_commands), id) !=
               std::end(unavailable_suitcase_commands) ||
           (id >= label_command && id < label_command + 8) ||
           (id >= sort_command && id < sort_command + folder_column_count);
}

bool command_suitcase(SuitcaseView& view, unsigned id)
{
    if (id == open_command) {
        int index = selected_preview_member(view);
        if (index >= 0)
            preview_suitcase_member(view.item, view.members[index]);
        return true;
    }
    for (const auto& [command, mode] : folder_view_commands)
        if (id == command) {
            set_suitcase_view(view, mode);
            ViewPreferences presentation;
            presentation.view = mode;
            save(L"SuitcaseViews", path_key(view.item.path), encode_folder_view(presentation));
            return true;
        }
    if (id == select_all_command) {
        ListView_SetItemState(view.list, -1, LVIS_SELECTED, LVIS_SELECTED);
        return true;
    }
    return suitcase_blocks_command(id);
}

// A specimen window has no selection. File commands must never apply to
// the folder behind an active preview.
bool preview_blocks_command(unsigned id)
{
    return suitcase_blocks_command(id) || id == select_all_command ||
           std::any_of(std::begin(folder_view_commands), std::end(folder_view_commands),
                       [&](const auto& entry) { return entry.first == id; });
}

// Suitcase records have no filesystem path or BNDL. Use the same embedded
// classic type artwork and cache ownership as the rest of Finder.
HICON suitcase_icon(uint32_t type)
{
    auto key = L"suitcase-member-artwork:" + std::to_wstring(type);
    auto cached = icon_cache.find(key);
    if (cached != icon_cache.end())
        return cached->second;
    FinderIcon art{};
    HICON image = finder_type_icon(type, &art) ? icon(art) : asset_icon(0);
    icon_cache.emplace(std::move(key), image);
    return image;
}

// Use the File Manager's read-only fork path for both locked and writable images.
struct SuitcaseSnapshot {
    Bytes bytes;
    bool data_fork;
};

SuitcaseSnapshot read_suitcase(const Item& item, bool data_only = false)
{
    if (!item.image_volume) {
        if (!data_only) {
            auto metadata = mac_file(item.path);
            if (!metadata.fork.empty())
                return {std::move(metadata.fork), false};
        }
        return {read_bytes(item.path), true};
    }
    FinderImageEntry catalog{};
    if (ROMlib_finder_image_info(item.image_volume, item.image_parent, item.image_name.c_str(), &catalog) ||
        catalog.id != item.image_id)
        throw std::runtime_error("The file on the mounted image is no longer available.");
    const bool resource = !data_only && catalog.resource_size != 0;
    const uint32_t size = resource ? catalog.resource_size : catalog.data_size;
    if (!size)
        return {{}, true};
    if (size > 64u * 1024 * 1024)
        throw std::runtime_error("The file is too large to preview.");
    Bytes bytes(size);
    int16_t fork = 0;
    int16_t error = ROMlib_finder_image_open_source_fork(item.image_volume, item.image_parent, item.image_name.c_str(),
                                                         resource, &fork);
    if (!error) {
        for (uint32_t offset = 0; offset < size && !error;) {
            const uint32_t chunk = std::min(65536u, size - offset);
            error = ROMlib_finder_image_read(fork, bytes.data() + offset, chunk);
            offset += chunk;
        }
        const auto close_error = ROMlib_finder_image_close_fork(fork);
        if (!error)
            error = close_error;
    }
    if (error)
        throw std::runtime_error("Cannot read the file's fork (Macintosh error " + std::to_string(error) + ").");
    return {std::move(bytes), !resource};
}

// Match the launcher: classic CODE 0 and application cfrg members qualify.
// Walk the packed resource in disk order without opening guest handles.
bool is_fat_application(const MacFile& application)
{
    bool m68k = !mac_resource(application, 0x434f4445, 0).empty(); // CODE
    bool ppc = false;
    auto fragments = mac_resource(application, 0x63667267, 0); // cfrg
    if (fragments.size() < 32)
        return false;
    auto longword = [&](size_t at) {
        return uint32_t(fragments[at]) << 24 | uint32_t(fragments[at + 1]) << 16 |
               uint32_t(fragments[at + 2]) << 8 | fragments[at + 3];
    };
    size_t at = 32;
    for (uint32_t count = longword(28); count && fragments.size() - at >= 43; --count) {
        unsigned size = unsigned(fragments[at + 40]) << 8 | fragments[at + 41];
        if (size < 43 || size > fragments.size() - at || fragments[at + 42] > size - 43)
            break;
        if (fragments[at + 22] == 1) { // kApplicationCFrag
            m68k |= longword(at) == 0x6d36386b; // m68k
            ppc |= longword(at) == 0x70777063; // pwpc
        }
        if (m68k && ppc)
            return true;
        at += size;
    }
    return false;
}

void configure_application(HWND owner, Item item)
{
    auto prefs = file_manager_path((romaw_folder.parent_path() / L"ROMAW.prefs").native());
    if (prefs.empty())
        throw std::runtime_error("Cannot open the ROMAW preferences path.");
    MacFile application;
    if (item.image_volume) {
        FinderImageEntry info{};
        if (ROMlib_finder_image_info(item.image_volume, item.image_parent, item.image_name.c_str(), &info) ||
            info.id != item.image_id)
            throw std::runtime_error("The application on the mounted image is no longer available.");
        if (info.resource_size)
            application.fork = read_suitcase(item).bytes;
    }
    else
        application = mac_file(item.path);
    char section[CONFIG_PREFS_SECTION_SIZE];
    config_prefs_app_section(item.mac.type ? item.mac.type : 0x4150504c, item.mac.creator, section);
    classic::AppPreferenceDraft draft;
    draft.fat = is_fat_application(application);
    char value[1024];
    for (size_t i = 0; i < draft.fields.size(); ++i) {
        const auto count = config_prefs_get(prefs.c_str(), section, classic::app_preference_keys[i], value, sizeof value);
        draft.fields[i].assign(value, count);
    }
    if (draft.fat && config_prefs_get(prefs.c_str(), section, "PreferM68K", value, sizeof value)) {
        if (strcmp(value, "1") == 0 || strcmp(value, "0") == 0)
            draft.prefer_m68k = value[0] == '1';
    }
    if (std::unique_ptr<FILE, decltype(&fclose)> script{config_prefs_open(prefs.c_str(), section), fclose}) {
        std::string statement;
        for (int byte; (byte = fgetc(script.get())) != EOF;) {
            if (byte != '\n') {
                statement += char(byte);
                continue;
            }
            if (_strnicmp(statement.c_str(), "Gestalt(", 8) == 0)
                draft.gestalts.push_back(statement);
            statement.clear();
        }
    }
    classic::configure_app(owner, item.name, std::move(draft), config_prefs_validate_line,
                           [&](const classic::AppPreferenceDraft& edited) {
        std::unique_ptr<FILE, decltype(&fclose)> script(tmpfile(), fclose);
        if (!script)
            return false;
        for (size_t i = 0; i < edited.fields.size(); ++i)
            if (!edited.fields[i].empty())
                fprintf(script.get(), "%s=%s\n", classic::app_preference_keys[i], edited.fields[i].c_str());
        if (edited.prefer_m68k)
            fprintf(script.get(), "PreferM68K=%d\n", *edited.prefer_m68k);
        for (const auto& statement : edited.gestalts)
            fprintf(script.get(), "%s\n", statement.c_str());
        return !ferror(script.get()) && !fflush(script.get()) &&
               config_prefs_store_app(prefs.c_str(), section, script.get(), edited.fat);
    });
}

bool save_shortcut_artwork(Item& item, const fs::path& path)
{
    if (!item.mac.kind && !item.image_volume) {
        if (save_native_application_icon(item.path, path))
            return true;
    }
    else {
        try {
            // Window rows release their forks after icon decoding. Reload the
            // resource data to retain sizes and depths absent from the display cache.
            auto application = item.image_volume ? item.mac : mac_file(item.path);
            if (item.image_volume) {
                auto contents = read_suitcase(item);
                if (!contents.data_fork)
                    application.fork = std::move(contents.bytes);
            }
            if (save_application_icon(application, path))
                return true;
        }
        catch (...) {
            // A damaged resource fork can still use the desktop database or generic artwork.
        }
        FinderIcon family{};
        bool found = item.image_volume ? ROMlib_finder_image_icon(
                                             item.image_volume, item.image_parent, item.image_name.c_str(), false,
                                             item.mac.flags, item.mac.type, item.mac.creator, &family)
                                       : desktop_icons && desktop_icons->lookup(item.path, item.mac, family);
        if ((found || finder_type_icon(item.mac.type, &family)) && save_application_icon(family, path))
            return true;
    }
    return classic::save_icon_file(item_icon(item), path.c_str());
}

void preview_suitcase_member(const Item& item, const SuitcaseMember& member)
{
    classic::WaitingCursor waiting;
    auto contents = read_suitcase(item).bytes;
    if (member.resource_type == sound_resource_type) {
        MacFile sound;
        sound.fork = std::move(contents);
        play_sound_preview(mac_resource(sound, sound_resource_type, member.resource_id));
        return;
    }
    auto fork = read_suitcase_fork(contents);
    auto identity = item.path.wstring() + L":" + std::to_wstring(member.resource_type) + L":" +
                    std::to_wstring(member.resource_id);
    show_font_preview(desktop, instance, identity, member.name, fork, {member.resource_type, member.resource_id});
}

bool is_sound_file(const Item& item)
{
    switch (item.mac.type) {
    case sound_file_type:
    case sound_resource_type:
    case 0x41494646: // AIFF
    case 0x41494643: // AIFC
    case 0x57415645: // WAVE
        return true;
    }
    if (item.mac.has_finder_info && item.mac.type)
        return false;
    const auto extension = fs::path(item.name).extension().wstring();
    return !_wcsicmp(extension.c_str(), L".aif") || !_wcsicmp(extension.c_str(), L".aiff") ||
           !_wcsicmp(extension.c_str(), L".aifc") || !_wcsicmp(extension.c_str(), L".wav");
}

void preview_sound_file(const Item& item)
{
    classic::WaitingCursor waiting;
    const bool classic_sound = item.mac.type == sound_file_type || item.mac.type == sound_resource_type;
    auto source = read_suitcase(item, !classic_sound);
    if (source.data_fork) {
        play_sound_preview(std::move(source.bytes));
        return;
    }
    MacFile sound;
    sound.fork = std::move(source.bytes);
    auto resource = mac_resource(sound, sound_resource_type);
    play_sound_preview(resource.empty() ? read_suitcase(item, true).bytes : std::move(resource));
}

bool is_standalone_font(uint32_t type)
{
    return type == 0x6666696c || type == 0x7466696c; // ffil or tfil
}

void preview_font_file(const Item& item)
{
    classic::WaitingCursor waiting;
    auto source = read_suitcase(item);
    SuitcaseFork fork;
    // Some standalone outline fonts store the sfnt directly in the data fork.
    if (source.data_fork && source.bytes.size() >= 4 &&
        (std::memcmp(source.bytes.data(), "OTTO", 4) == 0 || std::memcmp(source.bytes.data(), "true", 4) == 0 ||
         std::memcmp(source.bytes.data(), "ttcf", 4) == 0 || std::memcmp(source.bytes.data(), "\0\1\0\0", 4) == 0))
        fork.resources.push_back({{0x73666e74, 0}, 0, {}, std::move(source.bytes)});
    else
        fork = read_suitcase_fork(source.bytes);
    for (uint32_t type : {0x464f4e44u, 0x73666e74u, 0x4e464e54u, 0x464f4e54u})
        for (const auto& entry : fork.resources)
            if (entry.key.first == type) {
                show_font_preview(desktop, instance, item.path.wstring(), item.name, fork, entry.key);
                return;
            }
    throw std::runtime_error("This file does not contain a previewable font.");
}

void drop_suitcase_members(HWND owner, const Item& item, const std::vector<SuitcaseMember>& members, POINT point);

void populate_suitcase(SuitcaseView& view, const Bytes& bytes)
{
    auto members = suitcase_members(bytes);
    std::stable_sort(members.begin(), members.end(),
                     [](const auto& a, const auto& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    const auto list = view.list;
    classic::RedrawBatch redraw(list);
    ListView_DeleteAllItems(list);
    ImageList_RemoveAll(view.images);
    ImageList_RemoveAll(view.small_images);
    view.members.clear();
    view.artwork.clear();
    std::map<uint32_t, classic::IconViewImage> images;
    for (auto& member : members) {
        auto [image, added] = images.try_emplace(member.icon_type);
        if (added) {
            HICON normal = suitcase_icon(member.icon_type);
            auto selected = selected_icon_cache.find(normal);
            HICON alternate = selected == selected_icon_cache.end() ? normal : selected->second;
            image->second = {ImageList_AddIcon(view.images, normal), ImageList_AddIcon(view.images, alternate)};
            if (image->second.normal < 0 || image->second.selected < 0 ||
                ImageList_AddIcon(view.small_images, normal) != image->second.normal ||
                ImageList_AddIcon(view.small_images, alternate) != image->second.selected)
                throw std::runtime_error("Cannot load suitcase icons.");
        }
        LVITEMW row{};
        row.mask = LVIF_TEXT | LVIF_IMAGE;
        row.iItem = int(view.members.size());
        row.pszText = member.name.data();
        row.iImage = image->second.normal;
        if (ListView_InsertItem(list, &row) == -1)
            throw std::runtime_error("Cannot add a suitcase item.");
        ListView_SetItemText(list, row.iItem, 1, const_cast<wchar_t*>(member.kind));
        view.members.push_back(std::move(member));
        view.artwork.push_back(image->second);
    }
    set_suitcase_view(view, view.mode);
}

// Suitcase members are resource records, so display them in a separate
// modeless view rather than dispatching file commands on synthetic paths.
LRESULT CALLBACK suitcase_proc(HWND window, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* view = reinterpret_cast<SuitcaseView*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!view && msg != WM_NCCREATE)
        return DefWindowProcW(window, msg, wp, lp);
    switch (msg) {
    case WM_NCCREATE:
        view = static_cast<SuitcaseView*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
        SetPropW(window, L"FinderWhite", reinterpret_cast<HANDLE>(1));
        classic::decorate(window);
        break;
    case WM_CREATE: {
        RECT client{};
        GetClientRect(window, &client);
        HWND list = view->list = control(window, WC_LISTVIEWW, L"",
                                         LVS_REPORT | LVS_OWNERDRAWFIXED | LVS_NOLABELWRAP | LVS_SHOWSELALWAYS |
                                             LVS_SHAREIMAGELISTS | WS_TABSTOP,
                                         1, 0, 0, client.right, client.bottom);
        if (!list)
            return -1;
        view->images = ImageList_Create(32, 32, ILC_COLOR32 | ILC_MASK, 12, 2);
        view->small_images = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 12, 2);
        if (!view->images || !view->small_images)
            return -1;
        ListView_SetImageList(list, view->images, LVSIL_NORMAL);
        ListView_SetImageList(list, view->small_images, LVSIL_SMALL);
        ListView_SetIconSpacing(list, icon_cell_width, icon_cell_height);
        ListView_SetBkColor(list, RGB(255, 255, 255));
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<wchar_t*>(L"Name");
        column.cx = 235;
        ListView_InsertColumn(list, 0, &column);
        column.pszText = const_cast<wchar_t*>(L"Kind");
        column.cx = 125;
        ListView_InsertColumn(list, 1, &column);
        try {
            classic::style_list(list);
            if (!SetWindowSubclass(list, suitcase_list_proc, 1, reinterpret_cast<DWORD_PTR>(view)))
                return -1;
            populate_suitcase(*view, read_suitcase(view->item).bytes);
        }
        catch (const std::exception&) {
            return -1;
        }
        return 0;
    }
    case WM_NOTIFY: {
        const auto* drag = reinterpret_cast<const NMLISTVIEW*>(lp);
        if (drag->hdr.hwndFrom != view->list)
            break;
        if (drag->hdr.code == NM_DBLCLK ||
            (drag->hdr.code == LVN_KEYDOWN && reinterpret_cast<const NMLVKEYDOWN*>(lp)->wVKey == VK_RETURN)) {
            int index = drag->hdr.code == NM_DBLCLK ? drag->iItem : selected_preview_member(*view);
            if (index >= 0 && size_t(index) < view->members.size() && previewable_member(view->members[index]))
                try {
                    preview_suitcase_member(view->item, view->members[index]);
                }
                catch (const std::exception& error) {
                    failure(window, error);
                }
            return 0;
        }
        if (drag->hdr.code != LVN_BEGINDRAG ||
            drag->iItem < 0 || size_t(drag->iItem) >= view->members.size())
            break;
        try {
            for (int i = -1; (i = selected_item_after(view->list, view->members.size(), i)) >= 0;)
                view->dragged.push_back(view->members[i]);
            if (view->dragged.empty())
                view->dragged.push_back(view->members[drag->iItem]);
            auto position = drag->ptAction;
            ClientToScreen(view->list, &position);
            POINT origin{};
            const auto& member = view->members[drag->iItem];
            auto images = view->mode == FolderView::icons ? view->images : view->small_images;
            auto bitmap = classic::drag_artwork(view->list, images, view->artwork[drag->iItem].selected,
                                                drag->iItem, member.name.c_str(), origin);
            drag_image.begin(view->list, drag->iItem, view->grab, position, nullptr, bitmap, origin);
            SetCapture(window);
        }
        catch (const std::exception& error) {
            view->dragged.clear();
            drag_image.end();
            failure(window, error);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!view->dragged.empty()) {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(window, &point);
            drag_image.move(point);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (!view->dragged.empty()) {
            auto members = std::exchange(view->dragged, {});
            auto item = view->item;
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(window, &point);
            drag_image.end();
            ReleaseCapture();
            try {
                drop_suitcase_members(window, item, members, point);
            }
            catch (const std::exception& error) {
                failure(IsWindow(window) ? window : desktop, error);
            }
            return 0;
        }
        break;
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        if (!view->dragged.empty()) {
            view->dragged.clear();
            drag_image.end();
            if (GetCapture() == window)
                ReleaseCapture();
        }
        break;
    case WM_DRAWITEM: {
        const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
        if (!view || wp != 1 || draw->itemID >= view->members.size())
            break;
        const auto& member = view->members[draw->itemID];
        const bool selected = (draw->itemState & ODS_SELECTED) != 0;
        const int saved = SaveDC(draw->hDC);
        classic::fill_rect(draw->hDC, &draw->rcItem,
                           static_cast<HBRUSH>(GetStockObject(selected ? BLACK_BRUSH : WHITE_BRUSH)));
        SelectObject(draw->hDC, font);
        SetTextColor(draw->hDC, selected ? RGB(255, 255, 255) : RGB(0, 0, 0));
        SetBkMode(draw->hDC, TRANSPARENT);
        RECT kind{};
        if (ListView_GetSubItemRect(draw->hwndItem, int(draw->itemID), 1, LVIR_BOUNDS, &kind)) {
            int left = kind.left - ListView_GetColumnWidth(draw->hwndItem, 0) + 4;
            const auto image = view->artwork[draw->itemID];
            classic::draw_image(view->small_images, selected ? image.selected : image.normal, draw->hDC, left,
                                kind.top + (kind.bottom - kind.top - 16) / 2, 16, 16);
            RECT name{left + 20, kind.top, kind.left - 3, kind.bottom};
            kind.left += 4;
            constexpr UINT text_flags = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS;
            classic::draw_text(draw->hDC, member.name.c_str(), -1, &name, text_flags);
            classic::draw_text(draw->hDC, member.kind, -1, &kind, text_flags);
        }
        RestoreDC(draw->hDC, saved);
        return TRUE;
    }
    case WM_SIZE:
        if (HWND list = GetDlgItem(window, 1))
            MoveWindow(list, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        return 0;
    case WM_SETFOCUS:
        classic::activate(window);
        SetFocus(GetDlgItem(window, 1));
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        RemovePropW(window, L"FinderWhite");
        if (view && view->owned)
            delete view;
        break;
    }
    return DefWindowProcW(window, msg, wp, lp);
}

void open_suitcase(Item& item)
{
    for (HWND existing = nullptr; (existing = FindWindowExW(desktop, existing, suitcase_window_class, nullptr));) {
        auto* view = reinterpret_cast<SuitcaseView*>(GetWindowLongPtrW(existing, GWLP_USERDATA));
        if (view && view->item.image_volume == item.image_volume &&
            (item.image_volume ? view->item.image_id == item.image_id
                               : _wcsicmp(view->item.path.c_str(), item.path.c_str()) == 0)) {
            classic::activate(existing);
            SetFocus(existing);
            return;
        }
    }
    classic::WaitingCursor waiting;
    auto view = std::make_unique<SuitcaseView>();
    view->item = item;
    view->item.mac.fork.clear();
    auto saved = settings_path_value(preferences, L"SuitcaseViews", item.path);
    if (!saved.empty())
        view->mode = decode_folder_view(saved).view;
    WNDCLASSW type{};
    type.hInstance = instance;
    type.lpszClassName = suitcase_window_class;
    type.lpfnWndProc = suitcase_proc;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&type);
    HWND window = CreateWindowW(suitcase_window_class, item.name.c_str(),
                                WS_CHILD | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPSIBLINGS, 96, 72, 399, 230,
                                desktop, nullptr, instance, view.get());
    if (!window)
        throw std::runtime_error("Cannot open the suitcase window.");
    view->owned = true;
    view.release();
    ShowWindow(window, SW_SHOW);
    classic::activate(window);
    SetFocus(window);
}

int archive_kind(const Item& item)
{
    if (item.folder)
        return 0;
    int extension = romaw_archive_extension(fs::path(item.name).extension().u8string().c_str());
    return extension ? extension : romaw_archive_type(item.mac.type);
}

struct ExtractionPlacement {
    const FolderWindow* folder = nullptr;
    std::vector<POINT> positions;
};

// Reserve unoccupied icon cells in the destination's current viewport,
// starting with the cells closest to a selected archive in that folder.
ExtractionPlacement extraction_placement(const Extraction& extract, size_t count)
{
    const auto& destination = extract.destination;
    for (const auto* folder : folders) {
        if (folder->closing || folder->search || folder->presentation.view != FolderView::icons)
            continue;
        if (destination.volume
                ? folder->image_volume != destination.volume || folder->image_directory != destination.directory
                : folder->image_volume ||
                      _wcsicmp((folder->desktop ? desktop_folder : folder->path).c_str(), destination.path.c_str()) != 0)
            continue;

        RECT area{};
        POINT origin{};
        auto list = folder->list;
        GetClientRect(list, &area);
        ListView_GetOrigin(list, &origin);
        if (!folder->desktop) {
            area.right -= 16;
            area.bottom -= 16;
        }
        const RECT limits{std::max(16L, -origin.x), std::max(0L, -origin.y), area.right - 47,
                          area.bottom - icon_cell_height + 1};
        if (IsRectEmpty(&limits))
            return {folder, {}};

        POINT anchor{limits.left, limits.top + 8};
        bool found_archive = false;
        std::vector<POINT> icons;
        for (size_t i = 0; i < folder->items.size(); ++i) {
            POINT point{};
            if (!ListView_GetItemPosition(list, i, &point))
                continue;
            point.x -= origin.x;
            point.y -= origin.y;
            if (point.x > limits.left - icon_cell_width && point.x < limits.right + icon_cell_width &&
                point.y > limits.top - icon_cell_height && point.y < limits.bottom + icon_cell_height)
                icons.push_back(point);
            const auto& item = folder->items[i];
            auto is_archive = [&](const ArchiveSource& source) {
                return item.image_volume ? source.volume == item.image_volume && source.id == item.image_id
                                         : !source.volume && source.path == item.path;
            };
            if (!found_archive && std::any_of(extract.sources.begin(), extract.sources.end(), is_archive)) {
                anchor = point;
                found_archive = PtInRect(&area, anchor);
            }
        }
        anchor.x = std::clamp(anchor.x, limits.left, limits.right - 1);
        anchor.y = std::clamp(anchor.y, limits.top, limits.bottom - 1);

        std::vector<POINT> candidates;
        for (LONG y = limits.top + (anchor.y - limits.top) % icon_cell_height; y < limits.bottom; y += icon_cell_height)
            for (LONG x = limits.left + (anchor.x - limits.left) % icon_cell_width; x < limits.right; x += icon_cell_width)
                if (std::none_of(icons.begin(), icons.end(), [&](POINT icon) {
                        return std::abs(icon.x - x) < icon_cell_width && std::abs(icon.y - y) < icon_cell_height;
                    }))
                    candidates.push_back({x, y});
        auto distance = [&](POINT point) { return std::abs(point.x - anchor.x) + std::abs(point.y - anchor.y); };
        std::stable_sort(candidates.begin(), candidates.end(),
                         [&](POINT left, POINT right) { return distance(left) < distance(right); });
        if (candidates.size() > count)
            candidates.resize(count);
        for (auto& point : candidates) {
            point.x += origin.x;
            point.y += origin.y;
        }
        return {folder, std::move(candidates)};
    }
    return {};
}

void extract_items(HWND owner, const std::vector<Item>& items, ArchiveDestination destination, bool choose)
{
    std::vector<ArchiveSource> sources;
    for (const auto& item : items) {
        if (!archive_kind(item))
            throw std::runtime_error("Select ZIP, StuffIt, BinHex or MacBinary archives");
        sources.push_back({item.path, item.image_volume, item.image_parent, item.image_id, item.image_name});
    }
    if (sources.empty())
        return;
    if (choose || !archive_destination_allowed(destination)) {
        auto picked = pick_archive_destination(owner, destination, {romaw_folder, desktop_folder});
        if (!picked)
            return;
        destination = *picked;
    }
    classic::WaitingCursor waiting;
    Extraction extraction{std::move(destination), std::move(sources), {}};
    if (auto job = extract_archives(copies, extraction.sources, extraction.destination, extraction.formats))
        show_operation(job, 0, {}, {}, std::move(extraction));
}

void open_item(HWND owner, Item item)
{
    if (!item.folder && item.mac.type == 0x6b66696c) { // 'kfil'
        message(owner, L"Keyboard files are not compatible with ROMAW.", MB_ICONERROR);
        return;
    }
    if (!item.folder && item.mac.type == 0x50524552) { // 'PRER'
        message(owner, L"Printer files are not compatible with ROMAW.", MB_ICONERROR);
        return;
    }
    if (!item.folder && item.mac.type == 0x52444556) { // 'RDEV'
        message(owner, L"Chooser items don't work with ROMAW.",
                MB_ICONWARNING);
        return;
    }
    if (archive_kind(item)) {
        extract_items(owner, {item}, {item.path.parent_path(), item.image_volume, item.image_parent}, false);
        return;
    }
    if (item.image_volume) {
        if (item.folder) {
            open_folder(item.path, nullptr, item.image_volume, item.image_id, item.name);
            return;
        }
        if (is_mac_suitcase(item.mac)) {
            open_suitcase(item);
            return;
        }
        if (is_standalone_font(item.mac.type)) {
            preview_font_file(item);
            return;
        }
        if (is_sound_file(item)) {
            preview_sound_file(item);
            return;
        }
        if (item.mac.kind == FINDER_CLASSIC_DOCUMENT && (item.mac.flags & stationery_flag)) {
            open_stationery(owner, item);
            return;
        }
        item.path = image_text(image_item_path(item).c_str());
        if (item.mac.kind == FINDER_CLASSIC_DOCUMENT) {
            FinderImageEntry application{};
            if (ROMlib_finder_image_find_application(item.image_volume, item.mac.creator, &application)) {
                auto document = item.path;
                item.image_name = application.name;
                item.image_parent = application.parent;
                item.path = image_text(image_item_path(item).c_str());
                item.mac.kind = FINDER_CLASSIC_APPLICATION;
                launch(owner, item, {document});
                return;
            }
            if (!item.image_application.empty()) {
                auto document = item.path;
                item.image_name = item.image_application;
                item.image_parent = item.image_application_parent;
                item.path = image_text(image_item_path(item).c_str());
                item.mac.kind = FINDER_CLASSIC_APPLICATION;
                launch(owner, item, {document});
                return;
            }
        }
        launch(owner, item);
        return;
    }
    auto alias = shortcut_target(item.path);
    if (!alias.empty()) {
        if (!fs::exists(alias))
            throw std::runtime_error("The alias could not be opened because its original item could not be found.");
        item.path = alias;
        item.mac = mac_file(alias);
        item.name = mac_display_name(alias, item.mac);
        item.folder = fs::is_directory(alias);
    }
    if (item.special == 3)
        open_recycle_bin(owner);
    else if (archive_kind(item))
        extract_items(owner, {item}, {item.path.parent_path()}, false);
    else if (item.folder)
        open_folder(item.path);
    else if (is_mac_suitcase(item.mac))
        open_suitcase(item);
    else if (is_standalone_font(item.mac.type))
        preview_font_file(item);
    else if (is_sound_file(item))
        preview_sound_file(item);
    else if (FinderIsDiskImage(item.path.c_str())) {
        wchar_t details[1024]{};
        classic::WaitingCursor waiting;
        int32_t error = FinderMountImage(item.path.c_str(), details, std::size(details));
        if (error && error != -128) {
            std::wstring text = L"Cannot mount this disk image (Macintosh error " + std::to_wstring(error) + L").";
            if (*details)
                text += L"\n\n" + std::wstring(details);
            message(owner, text, MB_ICONERROR);
        }
        else if (!error)
            for (auto* folder : folders)
                if (folder->desktop)
                    refresh(*folder);
    }
    else if ((!item.mac.kind || item.mac.kind == FINDER_CLASSIC_DOCUMENT) && (item.mac.flags & stationery_flag))
        open_stationery(owner, item);
    else
        launch(owner, item);
}

// Hold selected identities across name prompts and window activation.
void open_selected(FolderWindow& f)
{
    std::vector<Item> opening;
    HWND owner = f.window;
    for (int i = -1; (i = selected_item_after(f.list, f.items.size(), i)) >= 0;)
        opening.push_back(f.items.at(i));
    for (auto& item : opening)
        open_item(IsWindow(owner) ? owner : desktop, std::move(item));
}

// Format a host timestamp in the user's local date/time conventions, with a
// placeholder for invalid input.
std::wstring file_date(FILETIME value)
{
    FILETIME local{};
    SYSTEMTIME time{};
    if (!FileTimeToLocalFileTime(&value, &local) || !FileTimeToSystemTime(&local, &time))
        return L"--";
    wchar_t date[128]{}, clock[64]{};
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time, nullptr, date, 128);
    GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &time, nullptr, clock, 64);
    return std::wstring(date) + L", " + clock;
}

struct HostInfoSize {
    std::atomic<bool> stop{false}, done{false};
    std::optional<FileSize> size; // Read only after done is published.
};

struct InfoData {
    fs::path path;
    std::wstring comment;
    std::wstring name;
    std::optional<std::array<DWORD, 3>> host_identity;
    HWND name_edit = nullptr;
    HWND icon_control = nullptr;
    HICON icon_preview = nullptr;
    Item icon_item;
    bool icon_editable = false, custom_icon = false;
    HWND edit = nullptr, check = nullptr;
    HWND pad = nullptr, minimum = nullptr, preferred = nullptr;
    HWND size_text = nullptr;
    std::shared_ptr<HostInfoSize> host_size;
    std::optional<ImageFolderSize> image_size;
    FinderApplicationMemory memory{};
    bool owned = false, image = false, locked = false, stationery = false, saving = false;
    int16_t image_volume = 0;
    uint32_t image_parent = 0, image_id = 0;
    std::string image_name;
};

constexpr UINT_PTR info_size_timer = 1;
constexpr UINT info_icon_message = WM_APP + 6;

// Render two backgrounds to recover the transparency of both alpha and
// classic masked HICONs. Regular clipboard bitmaps become opaque artwork.
FinderIcon clipboard_artwork(HICON source, HBITMAP bitmap = nullptr)
{
    BITMAPINFO format{};
    format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    format.bmiHeader.biWidth = 32;
    format.bmiHeader.biHeight = -32;
    format.bmiHeader.biPlanes = 1;
    format.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    HBITMAP target = CreateDIBSection(nullptr, &format, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    auto previous = dc && target ? SelectObject(dc, target) : nullptr;
    std::array<unsigned char, 4096> rgba{};
    bool ok = false;
    if (previous && previous != HGDI_ERROR) {
        auto* words = static_cast<uint32_t*>(pixels);
        if (source) {
            std::fill_n(words, 1024, 0);
            ok = DrawIconEx(dc, 0, 0, source, 32, 32, 0, nullptr, DI_NORMAL);
            GdiFlush();
            std::array<uint32_t, 1024> black;
            std::copy_n(words, black.size(), black.begin());
            std::fill_n(words, 1024, 0x00ffffff);
            ok = DrawIconEx(dc, 0, 0, source, 32, 32, 0, nullptr, DI_NORMAL) && ok;
            GdiFlush();
            for (unsigned i = 0; i < 1024; ++i) {
                int alpha = 255 - std::clamp(int(words[i] & 255) - int(black[i] & 255), 0, 255);
                for (unsigned channel = 0; channel < 3; ++channel) {
                    unsigned value = (black[i] >> ((2 - channel) * 8)) & 255;
                    rgba[i * 4 + channel] = alpha ? std::min(255u, (value * 255u + alpha / 2) / alpha) : 0;
                }
                rgba[i * 4 + 3] = alpha;
            }
        }
        else if (bitmap) {
            BITMAP dimensions{};
            HDC input = CreateCompatibleDC(dc);
            auto old = input ? SelectObject(input, bitmap) : nullptr;
            if (old && old != HGDI_ERROR && GetObjectW(bitmap, sizeof dimensions, &dimensions)) {
                SetStretchBltMode(dc, HALFTONE);
                ok = StretchBlt(dc, 0, 0, 32, 32, input, 0, 0, dimensions.bmWidth, dimensions.bmHeight, SRCCOPY);
                GdiFlush();
                for (unsigned i = 0; i < 1024; ++i) {
                    rgba[i * 4] = words[i] >> 16;
                    rgba[i * 4 + 1] = words[i] >> 8;
                    rgba[i * 4 + 2] = words[i];
                    rgba[i * 4 + 3] = 255;
                }
                SelectObject(input, old);
            }
            if (input)
                DeleteDC(input);
        }
        SelectObject(dc, previous);
    }
    if (target)
        DeleteObject(target);
    if (dc)
        DeleteDC(dc);
    FinderIcon result{};
    if (!ok)
        throw std::runtime_error("The icon could not be converted to Macintosh artwork.");
    std::array<unsigned char, 1024> small{};
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 16; ++x)
            memcpy(small.data() + (y * 16 + x) * 4, rgba.data() + (y * 64 + x * 2) * 4, 4);
    finder_icon_from_rgba(rgba.data(), small.data(), &result);
    return result;
}

bool read_info_custom_icon(const Item& item, FinderIcon& result)
{
    if (item.special)
        return false;
    if (item.image_volume)
        return (item.folder || (item.mac.flags & 0x400)) &&
               ROMlib_finder_image_custom_icon(item.image_volume, item.folder ? item.image_id : item.image_parent,
                                               item.image_name.c_str(), item.folder, &result);
    return (item.folder && custom_folder_icon(item.path, result)) ||
           ((item.mac.flags & 0x400) && finder_parse_custom_icon(item.mac.fork.data(), item.mac.fork.size(), &result));
}

void edit_info_icon(HWND window, InfoData& info, unsigned command)
{
    Item& item = info.icon_item;
    if (command != copy_command && !info.icon_editable)
        return;
    classic::WaitingCursor waiting;
    if (!item.special && !info.image) {
        if (info.host_identity && host_item_identity(info.path) != *info.host_identity)
            throw std::runtime_error("The original item is no longer available.");
        item.mac = mac_file(info.path);
    }
    else if (info.image && !item.special) {
        FinderImageEntry entry{};
        if (ROMlib_finder_image_info(info.image_volume, info.image_parent, info.image_name.c_str(), &entry) ||
            entry.id != info.image_id)
            throw std::runtime_error("The original image item is no longer available.");
        item.mac.flags = entry.flags;
    }
    item.path = info.path;
    item.name = info.name;
    item.image_name = info.image_name;
    item.image_parent = info.image_parent;
    FinderIcon family{};
    info.custom_icon = read_info_custom_icon(item, family);
    if (command == copy_command || command == cut_command) {
        if (!info.custom_icon) {
            auto untinted = item;
            untinted.mac.flags &= ~14u;
            family = clipboard_artwork(item_icon(untinted));
        }
        Bytes encoded(4096);
        encoded.resize(finder_icon_icns_encode(&family, encoded.data(), encoded.size()));
        std::array<unsigned char, 4096> rgba{};
        finder_icon_rgba(&family, rgba.data());
        write_host_clipboard_icon(window, encoded, rgba.data());
        if (command == copy_command)
            return;
    }
    const FinderIcon* replacement = nullptr;
    if (command == paste_command) {
        auto encoded = read_host_clipboard_icon(window);
        if (!finder_parse_icns_icon(encoded.data(), encoded.size(), &family)) {
            auto clipboard = read_host_clipboard(window);
            if (!clipboard || !clipboard->picture)
                throw std::runtime_error("The clipboard does not contain an icon or bitmap.");
            family = clipboard_artwork(nullptr, clipboard->picture);
        }
        replacement = &family;
    }
    if (!replacement && !info.custom_icon)
        return;
    if (info.image) {
        auto error = ROMlib_finder_image_set_custom_icon(info.image_volume, info.image_parent, info.image_name.c_str(),
                                                         info.image_id, replacement);
        if (error)
            throw std::runtime_error("Cannot save the custom icon (Macintosh error " + std::to_string(error) + ").");
        item.mac.flags = (item.mac.flags & ~0x400u) | (replacement ? 0x400 : 0);
    }
    else {
        DWORD attributes = GetFileAttributesW(info.path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (!(attributes & FILE_ATTRIBUTE_DIRECTORY) && (attributes & FILE_ATTRIBUTE_READONLY)))
            throw std::runtime_error("The item is unavailable or locked.");
        set_host_custom_icon(info.path, item.folder, replacement);
        item.mac = mac_file(info.path);
    }
    info.custom_icon = replacement != nullptr;
    icon_versions.erase(item_icon_key(item));
    auto preview = CopyIcon(item_icon(item));
    if (info.icon_preview)
        DestroyIcon(info.icon_preview);
    info.icon_preview = preview;
    InvalidateRect(info.icon_control, nullptr, FALSE);
    PostMessageW(desktop, WM_APP + 5, 0, 0);
}

LRESULT CALLBACK info_icon_proc(HWND control, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)
{
    auto& info = *reinterpret_cast<InfoData*>(data);
    if (msg == WM_LBUTTONDOWN) {
        SetFocus(control);
        return 0;
    }
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS)
        InvalidateRect(control, nullptr, FALSE);
    if (msg == WM_PAINT) {
        PAINTSTRUCT paint;
        auto dc = BeginPaint(control, &paint);
        RECT area{};
        GetClientRect(control, &area);
        classic::fill_rect(dc, &area, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        if (info.icon_preview)
            classic::draw_icon(dc, 2, 2, info.icon_preview, 32, 32);
        if (GetFocus() == control)
            DrawFocusRect(dc, &area);
        EndPaint(control, &paint);
        return 0;
    }
    if (msg == WM_KEYDOWN) {
        unsigned command = 0;
        if (wp == VK_DELETE || wp == VK_BACK)
            command = clear_command;
        else if (GetKeyState(VK_CONTROL) < 0)
            command = wp == 'C' ? copy_command : wp == 'V' ? paste_command : wp == 'X' ? cut_command : 0;
        if (command) {
            SendMessageW(GetParent(control), info_icon_message, command, 0);
            return 0;
        }
    }
    return DefSubclassProc(control, msg, wp, lp);
}

// Host directory reads may block on the filesystem. The worker owns its
// state and never talks to a window, so closing Get Info never waits for it.
void start_info_size(HWND w, InfoData& info, bool directory)
{
    if (!SetTimer(w, info_size_timer, 25, nullptr)) {
        SetWindowTextW(info.size_text, L"Unavailable");
        return;
    }
    try {
        if (info.image)
            info.image_size.emplace(info.image_volume, info.image_id);
        else {
            info.host_size = std::make_shared<HostInfoSize>();
            std::thread([path = info.path, result = info.host_size, directory] {
                try {
                    result->size = directory ? host_folder_size(path, result->stop) : host_file_size(path);
                }
                catch (...) {
                    // Report an unavailable total instead of publishing a partial size.
                }
                result->done = true;
            }).detach();
        }
    }
    catch (...) {
        KillTimer(w, info_size_timer);
        SetWindowTextW(info.size_text, L"Unavailable");
    }
}

void update_info_size(HWND w, InfoData& info)
{
    std::optional<FileSize> size;
    try {
        if (info.image_size) {
            if (!info.image_size->advance())
                return;
            size = info.image_size->size();
        }
        else if (info.host_size) {
            if (!info.host_size->done)
                return;
            size = info.host_size->size;
        }
    }
    catch (...) {
        // The source can disappear while Get Info is still open.
    }
    KillTimer(w, info_size_timer);
    info.image_size.reset();
    info.host_size.reset();
    auto text = size ? format_file_size(*size) : L"Unavailable";
    SetWindowTextW(info.size_text, text.c_str());
}

std::wstring info_edit_text(HWND edit)
{
    std::wstring text(GetWindowTextLengthW(edit) + 1, L'\0');
    text.resize(GetWindowTextW(edit, text.data(), int(text.size())));
    return text;
}

void follow_moves(const std::vector<Relocation>& changes);

// Commit the name separately so subsequent metadata edits use the new path
// or catalog spelling. The rename uses the same collision rules as icon edits.
void save_info_name(HWND w, InfoData& info)
{
    if (!info.name_edit)
        return;
    auto name = info_edit_text(info.name_edit);
    if (name == info.name)
        return;
    classic::WaitingCursor waiting;
    if (info.image) {
        info.image_name =
            rename_image_item(info.image_volume, {info.image_parent, info.image_id, info.image_name}, name);
        info.name = image_text(info.image_name.c_str());
    }
    else {
        if (host_item_identity(info.path) != *info.host_identity)
            throw std::runtime_error("The original item is no longer available.");
        auto attributes = GetFileAttributesW(info.path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (!(attributes & FILE_ATTRIBUTE_DIRECTORY) && (attributes & FILE_ATTRIBUTE_READONLY)))
            throw std::runtime_error("The item is unavailable or locked.");
        auto target = host_rename_target(info.path, name);
        std::vector<Relocation> changes{{info.path, target}};
        history.move(changes, L"Rename");
        bool alias = _wcsicmp(info.path.extension().c_str(), L".lnk") == 0;
        info.path = std::move(target);
        info.name = alias ? info.path.stem().wstring() : std::move(name);
        follow_moves(changes);
    }
    SetWindowTextW(info.name_edit, info.name.c_str());
    SetWindowTextW(w, (info.name + L" Info").c_str());
    PostMessageW(desktop, WM_APP + 5, 1, 0);
}

LRESULT CALLBACK info_name_proc(HWND edit, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)
{
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        auto& info = *reinterpret_cast<InfoData*>(data);
        if (wp == VK_ESCAPE)
            SetWindowTextW(edit, info.name.c_str());
        else if (!info.saving) {
            info.saving = true;
            try {
                save_info_name(GetParent(edit), info);
            }
            catch (const std::exception& error) {
                failure(GetParent(edit), error);
            }
            info.saving = false;
        }
        SendMessageW(edit, EM_SETSEL, 0, -1);
        return 0;
    }
    return DefSubclassProc(edit, msg, wp, lp);
}

std::wstring memory_kib(uint32_t bytes)
{
    return std::to_wstring((uint64_t(bytes) + 1023) / 1024);
}

int16_t access_info_memory(InfoData& info, const FinderApplicationMemory* update = nullptr)
{
    if (info.image)
        return ROMlib_finder_image_memory(info.image_volume, info.image_parent, info.image_name.c_str(), info.image_id,
                                          &info.memory, update);
    auto path = file_manager_path(info.path.native());
    return path.empty() ? -37 : ROMlib_finder_host_memory(path.data(), &info.memory, update);
}

// Keep the exact byte count of each untouched field, even when a legacy SIZE
// record is not an exact multiple of a kilobyte.
bool save_info_memory(HWND owner, InfoData& info)
{
    if (!info.minimum || !info.memory.editable)
        return true;
    auto update = info.memory;
    auto minimum_text = info_edit_text(info.minimum);
    auto preferred_text = info_edit_text(info.preferred);
    if (minimum_text == memory_kib(update.minimum) && preferred_text == memory_kib(update.preferred))
        return true;
    auto read_field = [](const std::wstring& value, uint32_t old) {
        return value == memory_kib(old) ? std::optional<uint32_t>(old) : info_memory_bytes(value);
    };
    auto minimum = read_field(minimum_text, update.minimum);
    auto preferred = read_field(preferred_text, update.preferred);
    if (!minimum || !preferred || !*preferred || *minimum > *preferred) {
        SetFocus(!minimum || (minimum && preferred && *minimum > *preferred) ? info.minimum : info.preferred);
        throw std::runtime_error("Enter whole sizes in K. Preferred size must be positive and at least the minimum.");
    }
    update.minimum = *minimum;
    update.preferred = *preferred;
    if (update.minimum == info.memory.minimum && update.preferred == info.memory.preferred)
        return true;
    if (update.minimum < info.memory.original_minimum && update.minimum < info.memory.minimum &&
        !classic::confirm(owner, L"The minimum is below the application's original minimum size.", L"Use Size"))
        return false;
    auto error = access_info_memory(info, &update);
    if (error)
        throw std::runtime_error("Cannot save the application's memory sizes (Macintosh error " +
                                 std::to_string(error) + ").");
    return true;
}

// Manage the modeless Get Info window, applying changed comments/attributes on
// close and releasing adopted state on destruction.
LRESULT CALLBACK info_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* info = reinterpret_cast<InfoData*>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        info = static_cast<InfoData*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(info));
        classic::decorate(w);
        SetPropW(w, L"FinderWhite", reinterpret_cast<HANDLE>(1));
    }
    if (msg == WM_TIMER && wp == info_size_timer && info && (info->host_size || info->image_size)) {
        update_info_size(w, *info);
        return 0;
    }
    if (msg == info_icon_message && info && !info->saving) {
        info->saving = true;
        try {
            edit_info_icon(w, *info, unsigned(wp));
        }
        catch (const std::exception& error) {
            failure(w, error);
        }
        info->saving = false;
        return 0;
    }
    if (msg == WM_CLOSE && info) {
        if (info->saving)
            return 0;
        info->saving = true;
        try {
            if (!save_info_memory(w, *info)) {
                info->saving = false;
                return 0;
            }
            save_info_name(w, *info);
            if (info->pad) {
                bool checked = SendMessageW(info->pad, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (checked != info->stationery) {
                    if (info->image)
                        stationery_image_item(info->image_volume,
                                              {info->image_parent, info->image_id, info->image_name}, checked);
                    else
                        set_stationery(info->path, checked);
                    info->stationery = checked;
                    PostMessageW(desktop, WM_APP + 5, 0, 0);
                }
            }
            if (info->edit) {
                auto comment = info_edit_text(info->edit);
                if (comment != info->comment) {
                    classic::WaitingCursor waiting;
                    if (info->image) {
                        std::wstring classic_text;
                        for (size_t i = 0; i < comment.size(); ++i) {
                            if (comment[i] == L'\n' && i && comment[i - 1] == L'\r')
                                continue;
                            classic_text += comment[i] == L'\n' ? L'\r' : comment[i];
                        }
                        auto text = encode_comment(classic_text, 199);
                        auto error = ROMlib_finder_image_set_comment(info->image_volume, info->image_parent,
                                                                     info->image_name.c_str(), info->image_id,
                                                                     text.data(), text.size());
                        if (error)
                            throw std::runtime_error("Cannot save the image comment (Macintosh error " +
                                                     std::to_string(error) + ").");
                        PostMessageW(desktop, WM_APP + 5, 0, 0);
                    }
                    else
                        set_comment(info->path, comment);
                    info->comment = comment;
                }
            }
            if (info->check && info->image) {
                bool locked = SendMessageW(info->check, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (locked != info->locked) {
                    auto error = ROMlib_finder_image_lock(info->image_volume, info->image_parent,
                                                          info->image_name.c_str(), info->image_id, locked);
                    if (!error)
                        error = ROMlib_finder_image_flush(info->image_volume);
                    if (error)
                        throw std::runtime_error("The image item's lock state could not be saved (Macintosh error " +
                                                 std::to_string(error) + ")");
                    info->locked = locked;
                }
            }
            else if (info->check) {
                DWORD flags = GetFileAttributesW(info->path.c_str());
                if (flags == INVALID_FILE_ATTRIBUTES)
                    throw std::runtime_error("The original item is no longer available.");
                DWORD changed = SendMessageW(info->check, BM_GETCHECK, 0, 0) == BST_CHECKED
                                    ? flags | FILE_ATTRIBUTE_READONLY
                                    : flags & ~FILE_ATTRIBUTE_READONLY;
                if (changed != flags && !SetFileAttributesW(info->path.c_str(), changed))
                    throw std::runtime_error("The item's lock state could not be changed.");
            }
            DestroyWindow(w);
        }
        catch (const std::exception& error) {
            failure(w, error);
            info->saving = false;
        }
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        KillTimer(w, info_size_timer);
        if (info && info->host_size)
            info->host_size->stop = true;
        if (info && info->icon_preview)
            DestroyIcon(info->icon_preview);
        RemovePropW(w, L"FinderWhite");
        if (info && info->owned)
            delete info;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

// Choose the generated file/folder/application layout and transfer its state
// to the window only after creation succeeds.
void show_info(Item item)
{
    classic::WaitingCursor waiting;
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    FinderImageEntry catalog{};
    FinderImageVolume volume{};
    Bytes vers;
    std::wstring where;
    if (item.image_volume) {
        int16_t error = item.folder ? ROMlib_finder_image_entry(item.image_volume, item.image_id, -1, &catalog)
                                    : ROMlib_finder_image_info(item.image_volume, item.image_parent,
                                                               item.image_name.c_str(), &catalog);
        if (error || (item.special != 4 && catalog.id != item.image_id))
            throw std::runtime_error("The original image item is no longer available");
        auto volumes = image_volumes();
        auto found = std::find_if(volumes.begin(), volumes.end(),
                                  [&](const auto& v) { return v.reference == item.image_volume; });
        if (found == volumes.end())
            throw std::runtime_error("The image is no longer mounted");
        volume = *found;
        auto kind = item.mac.kind;
        item.mac = mac_catalog_info(catalog.finder_info, item.folder);
        item.mac.kind = kind;
        auto comment = image_search_comment(item.image_volume, catalog.id);
        for (size_t i = 0; i < comment.size(); ++i) {
            item.mac.comment += comment[i];
            if (comment[i] == L'\r' && (i + 1 == comment.size() || comment[i + 1] != L'\n'))
                item.mac.comment += L'\n';
        }
        attributes.dwFileAttributes = catalog.locked ? FILE_ATTRIBUTE_READONLY : 0;
        auto date = [](uint32_t seconds) {
            uint64_t ticks = (uint64_t(seconds) + 9561628800ULL) * 10000000;
            return FILETIME{DWORD(ticks), DWORD(ticks >> 32)};
        };
        attributes.ftCreationTime = date(catalog.created);
        attributes.ftLastWriteTime = date(catalog.modified);
        attributes.nFileSizeLow = catalog.data_size;
        if (item.special == 4)
            where = image_text(volume.source, CP_ACP);
        else {
            where = image_text(image_item_path(item).c_str());
            auto separator = where.rfind(L':');
            if (separator != std::wstring::npos)
                where.resize(separator + 1);
        }
        if (!item.folder) {
            unsigned char bytes[512];
            size_t size = 0;
            if (ROMlib_finder_image_version(item.image_volume, item.image_parent, item.image_name.c_str(), bytes,
                                            sizeof bytes, &size))
                vers.assign(bytes, bytes + size);
        }
    }
    else {
        item.mac = mac_file(item.path);
        if (!GetFileAttributesExW(item.path.c_str(), GetFileExInfoStandard, &attributes))
            throw std::runtime_error("The item is no longer available.");
        where = item.path.parent_path().wstring();
        vers = mac_resource(item.mac, 0x76657273);
    }
    auto info = std::make_unique<InfoData>();
    info->path = item.path;
    info->name = item.name;
    info->comment = item.mac.comment;
    info->image = item.image_volume != 0;
    info->image_volume = item.image_volume;
    info->image_parent = catalog.parent;
    info->image_id = catalog.id;
    info->image_name = catalog.name;
    info->locked = (attributes.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
    info->stationery = (item.mac.flags & stationery_flag) != 0;
    bool renameable = !item.special && (item.folder || !info->locked);
    if (info->image)
        renameable = renameable && !volume.read_only && info->image_id > 2;
    else if (renameable && item.path != item.path.root_path() &&
             !(attributes.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        try {
            info->host_identity = host_item_identity(item.path);
        }
        catch (const std::exception&) {
            renameable = false;
        }
    }
    else
        renameable = false;
    info->icon_editable = renameable;
    bool memory_available = false;
    WNDCLASSW type{};
    type.hInstance = instance;
    type.lpszClassName = L"FinderInfo";
    type.lpfnWndProc = info_proc;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&type);
    const FinderInfoField* fields = finder_info_11261;
    size_t count = std::size(finder_info_11261);
    int info_width = info_width_11261, info_height = info_height_11261;
    if (item.folder) {
        fields = finder_info_11511;
        count = std::size(finder_info_11511);
        info_width = info_width_11511;
        info_height = info_height_11511;
    }
    else if (item.mac.kind == FINDER_CLASSIC_APPLICATION) {
        fields = finder_info_17261;
        count = std::size(finder_info_17261);
        info_width = info_width_17261;
        info_height = info_height_17261;
        memory_available = access_info_memory(*info) == 0;
        info->memory.editable = memory_available && info->memory.editable && !volume.read_only && !info->locked;
    }
    else if ((!item.mac.kind || item.mac.kind == FINDER_CLASSIC_DOCUMENT) &&
             (item.image_volume ||
              (_wcsicmp(item.path.extension().c_str(), L".exe") && !FinderIsDiskImage(item.path.c_str())))) {
        fields = finder_info_17011;
        count = std::size(finder_info_17011);
        info_width = info_width_17011;
        info_height = info_height_17011;
    }
    HWND w = CreateWindowExW(0, type.lpszClassName, (item.name + L" Info").c_str(),
                             WS_CHILD | WS_CAPTION | WS_SYSMENU | WS_CLIPSIBLINGS, 80, 48, info_width + 4,
                             info_height + 21, desktop, nullptr, instance, info.get());
    if (!w)
        throw std::runtime_error("Cannot open Get Info");
    auto checkbox = [&](const wchar_t* label, const RECT& bounds, bool checked, bool enabled) {
        auto button = CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, bounds.left,
                                    bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, w, nullptr,
                                    instance, nullptr);
        classic::style_checkbox(button);
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(button, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(button, enabled);
        return button;
    };
    auto codes = info_codes(item.path, item.mac, item.folder, item.image_volume != 0);
    std::wstring version = item.image_volume ? L"--" : L"";
    if (vers.size() >= 7 && vers[6] <= vers.size() - 7) {
        int count = MultiByteToWideChar(10000, 0, reinterpret_cast<const char*>(vers.data() + 7), vers[6], nullptr, 0);
        version.resize(count);
        MultiByteToWideChar(10000, 0, reinterpret_cast<const char*>(vers.data() + 7), vers[6], version.data(), count);
    }
    HWND memory_panel = nullptr;
    for (size_t i = 0; i < count; ++i) {
        const auto& field = fields[i];
        std::wstring text = field.label;
        switch (field.id) {
        case 20816:
            text = item.name;
            break;
        case 3051:
            if (item.special == 4 && std::strncmp(volume.source, "afp://", 6) == 0)
                text = L"AFP volume (read-only)";
            else if (item.special == 4)
                text = volume.read_only ? L"disk image (read-only)" : L"disk image";
            else {
                DocumentKinds kinds;
                text = item_kind(item, kinds);
            }
            break;
        case 32001:
            text = codes.type;
            break;
        case 32002:
            text = codes.creator;
            break;
        case 3054:
            if (item.special == 4)
                text =
                    format_bytes(volume.total_bytes) + L" total\r\n" + format_bytes(volume.free_bytes) + L" available";
            else if (item.folder || !item.image_volume)
                text = L"Calculating...";
            else
                text = format_file_size(image_file_size(catalog));
            break;
        case 3056:
            text = where;
            break;
        case 20803:
            text = file_date(attributes.ftCreationTime);
            break;
        case 20805:
            text = file_date(attributes.ftLastWriteTime);
            break;
        case 12801:
            text = version;
            break;
        case 17300:
            text = info->memory.suggested ? memory_kib(info->memory.suggested) : L"--";
            break;
        default:
            break;
        }
        const auto& r = field.bounds;
        if (field.id == 0) {
            info->icon_preview = CopyIcon(item_icon(item));
            info->icon_control =
                control(w, L"STATIC", L"Get Info icon", SS_NOTIFY | WS_TABSTOP, 0, r.left + 1, r.top + 1, 36, 36);
            SetWindowSubclass(info->icon_control, info_icon_proc, 1, reinterpret_cast<DWORD_PTR>(info.get()));
        }
        else if (field.id == 20806) {
            info->check =
                checkbox(L"Locked", r, info->locked, !item.image_volume || (!volume.read_only && !item.folder));
        }
        else if (field.id == 20816 && renameable) {
            info->name_edit = control(w, L"EDIT", text.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, 0, r.left, r.top,
                                      r.right - r.left, r.bottom - r.top);
            SetWindowSubclass(info->name_edit, info_name_proc, 1, reinterpret_cast<DWORD_PTR>(info.get()));
        }
        else if (field.id == 17050)
            info->pad = checkbox(L"Stationery Pad", r, info->stationery, !volume.read_only && !info->locked);
        else if (field.id == 17304) {
            memory_panel =
                control(w, L"BUTTON", text.c_str(), BS_GROUPBOX, 0, r.left, r.top, r.right - r.left, r.bottom - r.top);
        }
        else if (field.id == 17301 || field.id == 17302) {
            bool minimum = field.id == 17301;
            auto value = memory_available ? memory_kib(minimum ? info->memory.minimum : info->memory.preferred) : L"--";
            HWND edit =
                control(w, L"EDIT", value.c_str(),
                        WS_TABSTOP | ES_RIGHT | ES_AUTOHSCROLL | (info->memory.editable ? ES_NUMBER : ES_READONLY),
                        field.id, r.left, r.top, r.right - r.left, r.bottom - r.top);
            classic::style_text_control(edit, true);
            SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(3, 3));
            SendMessageW(edit, EM_SETLIMITTEXT, 10, 0);
            (minimum ? info->minimum : info->preferred) = edit;
        }
        else if (field.id == 20807) {
            bool read_only = info->image && (volume.read_only || info->locked || info->image_id <= 2);
            info->edit = control(w, L"EDIT", info->comment.c_str(),
                                 WS_BORDER | WS_TABSTOP | ES_MULTILINE | (read_only ? ES_READONLY : 0), 0, r.left,
                                 r.top, r.right - r.left, r.bottom - r.top);
            if (!info->image)
                SendMessageW(info->edit, EM_SETLIMITTEXT, 200, 0);
        }
        else if (field.id == 3054 && item.special != 4 && (item.folder || !item.image_volume))
            info->size_text = control(w, L"STATIC", text.c_str(), SS_LEFT | SS_NOPREFIX, 0, r.left, r.top,
                                      r.right - r.left, r.bottom - r.top);
        else if (!text.empty())
            control(w, L"STATIC", text.c_str(),
                    field.id == -1     ? SS_RIGHT
                    : field.id == 3056 ? SS_PATHELLIPSIS
                                       : SS_LEFT | SS_NOPREFIX,
                    0, r.left, r.top, r.right - r.left, r.bottom - r.top);
    }
    // Keep the decorative frame behind its labels and editable siblings.
    if (memory_panel)
        SetWindowPos(memory_panel, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (info->size_text)
        start_info_size(w, *info, item.folder);
    FinderIcon custom{};
    info->custom_icon = read_info_custom_icon(item, custom);
    info->icon_item = std::move(item);
    info->owned = true;
    info.release();
    ShowWindow(w, SW_SHOW);
    classic::activate(w);
}

// Distinguish a drive root from an ordinary directory before enabling volume
// commands.
bool volume_root(const fs::path& path)
{
    return path.has_root_name() && path == path.root_path();
}

// Enable eject/disconnect only for removable, optical or mapped-network roots.
bool can_eject(const fs::path& path)
{
    if (!volume_root(path))
        return false;
    auto type = GetDriveTypeW(path.c_str());
    return type == DRIVE_REMOVABLE || type == DRIVE_CDROM || type == DRIVE_REMOTE;
}

// Limit formatting to removable roots outside the Windows, executable and
// settings volumes.
bool can_erase(const fs::path& path)
{
    if (!volume_root(path) || GetDriveTypeW(path.c_str()) != DRIVE_REMOVABLE)
        return false;
    wchar_t system[32768]{};
    GetWindowsDirectoryW(system, 32768);
    auto root = path.root_name().wstring();
    for (const auto& protected_path : {fs::path(system), executable_folder, preferences})
        if (_wcsicmp(root.c_str(), protected_path.root_name().c_str()) == 0)
            return false;
    return true;
}

// Disconnect a network mapping or invoke the shell's eject verb after
// validating the selected volume.
void eject_volume(HWND owner, const fs::path& path)
{
    if (!can_eject(path))
        throw std::runtime_error("This disk cannot be ejected.");
    if (GetDriveTypeW(path.c_str()) == DRIVE_REMOTE) {
        DWORD result = WNetCancelConnection2W(path.root_name().c_str(), 0, FALSE);
        if (result)
            throw std::runtime_error("The network drive could not be disconnected. Close files on it and try again.");
    }
    else {
        auto device = L"\\\\.\\" + path.root_name().wstring();
        HANDLE handle = CreateFileW(device.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            throw std::runtime_error("The disk could not be opened for ejection.");
        DWORD returned;
        BOOL success = DeviceIoControl(handle, IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0, nullptr, 0, &returned, nullptr);
        DWORD error = GetLastError();
        CloseHandle(handle);
        if (!success)
            throw std::runtime_error(
                "Windows could not eject this disk. Close its files and use Safely Remove Hardware if required. Error " +
                std::to_string(error));
    }
    for (auto* folder : folders)
        if (folder->desktop)
            refresh(*folder);
    (void)owner;
}

// Apply classic styling to a Windows print/page-setup dialog after its
// controls are created.
UINT_PTR CALLBACK print_hook(HWND window, UINT message, WPARAM, LPARAM)
{
    if (message == WM_INITDIALOG)
        classic::style_dialog(window);
    return 0;
}

// Print the current listing through a Windows printer DC, unwinding the
// document and GDI resources on failure.
void print_folder(FolderWindow& folder)
{
    PRINTDLGW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = desktop;
    dialog.Flags = PD_RETURNDC | PD_NOSELECTION | PD_NOPAGENUMS | PD_ENABLEPRINTHOOK;
    dialog.lpfnPrintHook = print_hook;
    dialog.hDevMode = page_setup.hDevMode;
    dialog.hDevNames = page_setup.hDevNames;
    dialog.nCopies = 1;
    BOOL accepted = PrintDlgW(&dialog);
    page_setup.hDevMode = dialog.hDevMode;
    page_setup.hDevNames = dialog.hDevNames;
    if (!accepted) {
        if (CommDlgExtendedError())
            throw std::runtime_error("The printer dialog could not be opened.");
        return;
    }
    HDC dc = dialog.hDC;
    if (!dc)
        throw std::runtime_error("Windows did not supply a printer.");
    auto title = folder.desktop ? L"Desktop" : folder.path.filename().wstring();
    DOCINFOW document{};
    document.cbSize = sizeof document;
    document.lpszDocName = title.c_str();
    bool started = false;
    HFONT print_font = CreateFontW(-MulDiv(9, GetDeviceCaps(dc, LOGPIXELSY), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                                   FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                   DEFAULT_PITCH, L"Geneva");
    HGDIOBJ old_font = SelectObject(dc, print_font);
    try {
        if (StartDocW(dc, &document) <= 0)
            throw std::runtime_error("The print job could not be started.");
        started = true;
        int dpi_x = GetDeviceCaps(dc, LOGPIXELSX), dpi_y = GetDeviceCaps(dc, LOGPIXELSY);
        int x = MulDiv(page_setup.rtMargin.left ? page_setup.rtMargin.left : 500, dpi_x, 1000);
        int top = MulDiv(page_setup.rtMargin.top ? page_setup.rtMargin.top : 500, dpi_y, 1000);
        int bottom = GetDeviceCaps(dc, VERTRES) -
                     MulDiv(page_setup.rtMargin.bottom ? page_setup.rtMargin.bottom : 500, dpi_y, 1000);
        int width = GetDeviceCaps(dc, HORZRES) - x -
                    MulDiv(page_setup.rtMargin.right ? page_setup.rtMargin.right : 500, dpi_x, 1000);
        int line = MulDiv(14, dpi_y, 72), y = bottom;
        if (width <= 0 || bottom - top < line * 3)
            throw std::runtime_error("The page margins leave no room to print.");
        auto new_page = [&] {
            if (StartPage(dc) <= 0)
                throw std::runtime_error("The printer could not start a page.");
            y = top;
            RECT caption{x, y, x + width, y + line};
            classic::draw_text(dc, title.c_str(), int(title.size()), &caption, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            y += line * 2;
        };
        new_page();
        for (const auto& item : folder.items) {
            if (y + line > bottom) {
                if (EndPage(dc) <= 0)
                    throw std::runtime_error("The page could not be printed.");
                new_page();
            }
            RECT text{x, y, x + width, y + line};
            classic::draw_text(dc, item.name.c_str(), -1, &text,
                               DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            y += line;
        }
        if (EndPage(dc) <= 0 || EndDoc(dc) <= 0)
            throw std::runtime_error("The print job could not be completed.");
        started = false;
    }
    catch (...) {
        if (started)
            AbortDoc(dc);
        SelectObject(dc, old_font);
        DeleteObject(print_font);
        DeleteDC(dc);
        throw;
    }
    SelectObject(dc, old_font);
    DeleteObject(print_font);
    DeleteDC(dc);
}

// Build commands from generated classic menu resources, then attach native
// additions and the shared owner-draw styling.
HMENU
menu()
{
    HMENU bar = CreateMenu();
    auto section = [&](const wchar_t* title, std::initializer_list<std::pair<unsigned, const wchar_t*>> items) {
        HMENU m = CreatePopupMenu();
        for (const auto& i : items)
            AppendMenuW(m, i.first ? MF_STRING : MF_SEPARATOR, i.first, i.second);
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(m), title);
        return m;
    };
    HMENU apple = CreatePopupMenu();
    AppendMenuW(apple, MF_STRING, about_command, L"About Finder...");
    AppendMenuW(apple, MF_STRING, about_system_command, L"About This PC...");
    AppendMenuW(apple, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(apple, MF_STRING, about_romaw_command, L"About ROMAW...");
    AppendMenuW(apple, MF_STRING, romaw_prefs_command, L"" ROMAW_PREFS_MENU_LABEL);
    AppendMenuW(apple, MF_SEPARATOR, 0, nullptr);
    std::function<void(HMENU, const fs::path&, unsigned)> populate;
    populate = [&](HMENU target, const fs::path& directory, unsigned depth) {
        if (depth > 3 || apple_items.size() >= 128)
            return;
        std::vector<fs::path> files;
        std::error_code error;
        for (const auto& entry :
             fs::directory_iterator(directory, fs::directory_options::skip_permission_denied, error))
            if (!hidden_metadata(entry.path()))
                files.push_back(entry.path());
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
            return _wcsicmp(a.filename().c_str(), b.filename().c_str()) < 0;
        });
        for (const auto& path : files) {
            if (target == apple && _wcsicmp(path.filename().c_str(), L"Chooser") == 0)
                continue;
            if (apple_items.size() >= 128)
                break;
            Item item;
            item.path = path;
            item.name = path.filename().wstring();
            item.folder = fs::is_directory(path);
            try {
                item.mac = mac_file(path);
            }
            catch (...) {
            }
            unsigned id = 1000 + unsigned(apple_items.size());
            HICON art = nullptr;
            try {
                auto mono = mac_resource(item.mac, 0x5349434e, item.mac.driver);
                if (mono.empty())
                    mono = mac_resource(item.mac, 0x5349434e);
                if (mono.size() >= 32) {
                    std::array<unsigned char, 1024> pixels{};
                    for (unsigned bit = 0; bit < 256; ++bit)
                        pixels[bit * 4 + 3] = (mono[bit / 8] & (0x80 >> (bit % 8))) ? 255 : 0;
                    auto key = path.wstring() + L"#apple-SICN";
                    auto found = icon_cache.find(key);
                    if (found != icon_cache.end())
                        art = found->second;
                    else {
                        art = rgba_icon(pixels.data(), 16, 16);
                        icon_cache.emplace(key, art);
                    }
                }
            }
            catch (...) {
            }
            if (!art)
                art = item_icon(item);
            classic::menu_icon(id, art);
            Bytes().swap(item.mac.fork);
            apple_items.push_back(std::move(item));
            if (apple_items.back().folder) {
                auto sub = CreatePopupMenu();
                populate(sub, path, depth + 1);
                AppendMenuW(target, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), path.filename().c_str());
                MENUITEMINFOW info{};
                info.cbSize = sizeof info;
                info.fMask = MIIM_ID;
                info.wID = id;
                SetMenuItemInfoW(target, GetMenuItemCount(target) - 1, TRUE, &info);
            }
            else
                AppendMenuW(target, MF_STRING, id, path.filename().c_str());
        }
    };
    AppendMenuW(apple, MF_STRING, afp_chooser_command, L"Chooser");
    classic::menu_icon(afp_chooser_command, asset_icon(chooser_asset));
    populate(apple, romaw_folder / L"System Folder/Apple Menu Items", 0);
    auto panels = CreatePopupMenu();
    auto panel_path = romaw_folder / L"System Folder/Control Panels";
    populate(panels, panel_path, 0);
    if (GetMenuItemCount(panels) > 0)
        AppendMenuW(apple, MF_POPUP, reinterpret_cast<UINT_PTR>(panels), L"Control Panels");
    else
        DestroyMenu(panels);
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(apple), L"Apple");
    auto resource_menu = [&](const wchar_t* title, const auto& entries) {
        HMENU popup = CreatePopupMenu();
        static const std::map<std::string, unsigned> commands = {
            {"new ", new_folder_command},   {"sope", open_command},
            {"clos", close_command},        {"sinf", info_command},
            {"sdup", duplicate_command},    {"copy", copy_command},
            {"past", paste_command},        {"sela", select_all_command},
            {"shcl", clipboard_command},    {"clea", special_cleanup_command},
            {"undo", undo_command},         {"cut ", cut_command},
            {"cler", clear_command},        {"sali", alias_command},
            {"sput", put_away_command},     {"find", find_command},
            {"finn", find_again_command},
            {"spri", print_command},        {"pgsu", page_setup_command},
            {"pwin", print_window_command}, {"empt", empty_trash_command},
            {"seje", eject_command},        {"sera", erase_command}};
        unsigned unavailable = 400;
        for (const auto& entry : entries) {
            if (wcscmp(entry.text, L"-") == 0)
                AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
            else {
                auto found = commands.find(entry.command);
                // The classic Print Window title is filled dynamically by Finder.
                // Reserve popup width for the longest Clean Up alternate.
                auto text = strcmp(entry.command, "clea") == 0 ? L"Clean Up Selection"
                            : wcscmp(entry.text, L"m") == 0    ? L"Print Window..."
                                                               : entry.text;
                AppendMenuW(popup, MF_STRING | (found == commands.end() ? MF_GRAYED : 0),
                            found == commands.end() ? unavailable++ : found->second, text);
                if (found != commands.end() && found->second == alias_command)
                    AppendMenuW(popup, MF_STRING | MF_GRAYED, windows_shortcut_command, L"Make Windows Shortcut");
            }
        }
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(popup), title);
        return popup;
    };
    HMENU file = resource_menu(L"&File", finder_menu_1252);
    InsertMenuW(file, info_command, MF_BYCOMMAND | MF_STRING | MF_GRAYED, configure_app_command, L"Configure App...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, parent_command, L"Open Enclosing Folder");
    AppendMenuW(file, MF_STRING, extract_here_command, L"Extract Here");
    AppendMenuW(file, MF_STRING, extract_to_command, L"Extract To...");
    AppendMenuW(file, MF_STRING, rename_command, L"Rename\tF2");
    AppendMenuW(file, MF_STRING, trash_command, L"Move to Trash\tDelete");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, quit_command, L"Quit\tCtrl+Q");
    resource_menu(L"&Edit", finder_menu_1253);
    HMENU view = section(
        L"&View",
        {{icons_command, L"as Icons"}, {small_icons_command, L"as Small Icons"}, {list_command, L"as List"}, {0, L""}});
    for (unsigned column = 0; column < folder_column_count; ++column)
        AppendMenuW(view, MF_STRING, sort_command + column,
                    (std::wstring(L"by ") + classic_view_columns[column]).c_str());
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, arrange_name_command, L"Clean Up by Name");
    AppendMenuW(view, MF_STRING, arrange_command, L"Clean Up to Grid");
    AppendMenuW(view, MF_STRING, arrange_selection_command, L"Clean Up Selection");
    AppendMenuW(view, MF_STRING, refresh_command, L"Refresh\tF5");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, show_hidden_command, L"Show Hidden");
    AppendMenuW(view, MF_STRING, spring_folders_command, L"Spring-loaded Folders");
    AppendMenuW(bar, MF_STRING, label_popup_command, L"&Label");
    HMENU special = resource_menu(L"&Special", finder_menu_1255);
    AppendMenuW(special, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(special, MF_STRING, register_images_command, L"Register Disk Image Types...");
    classic::style_menu(bar);
    return bar;
}

// Paint the resource-derived system information layout and release its window
// properties on destruction.
LRESULT CALLBACK about_system_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_NCCREATE) {
        classic::decorate(window);
        SetPropW(window, L"FinderWhite", reinterpret_cast<HANDLE>(1));
    }
    if (message == WM_CREATE)
        SetTimer(window, 1, 2000, nullptr);
    if (message == WM_TIMER) {
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT client;
        GetClientRect(window, &client);
        classic::fill_rect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        auto old_font = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        auto text = [&](RECT bounds, const std::wstring& value, UINT flags = DT_LEFT) {
            classic::draw_text(dc, value.c_str(), -1, &bounds, flags | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        };
        classic::draw_icon(dc, about_rect_1.left, about_rect_1.top, app_icon, 32, 32, 0, nullptr, DI_NORMAL);
        text({52, 10, 185, 24}, L"Finder");
        text({52, 27, 185, 39}, L"Windows x64");
        text(about_rect_3, L"System Software", DT_RIGHT);
        auto version_bounds = about_rect_4;
        OffsetRect(&version_bounds, 3, 0);
        text(version_bounds, L"Windows");
        text(about_rect_5, L"ROMAW Finder");
        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof memory;
        GlobalMemoryStatusEx(&memory);
        text(about_rect_6, L"Total Memory:", DT_RIGHT);
        auto total_bounds = about_rect_7;
        OffsetRect(&total_bounds, 3, 0);
        text(total_bounds, format_bytes(memory.ullTotalPhys));
        text(about_rect_8, L"Available Memory:", DT_RIGHT);
        auto available_bounds = about_rect_9;
        OffsetRect(&available_bounds, 3, 0);
        text(available_bounds, format_bytes(memory.ullAvailPhys));
        PROCESS_MEMORY_COUNTERS process{};
        GetProcessMemoryInfo(GetCurrentProcess(), &process, sizeof process);
        uint64_t native = process.WorkingSetSize, guest = 0;
        for (HANDLE child : children) {
            process = {};
            if (GetProcessMemoryInfo(child, &process, sizeof process))
                guest += process.WorkingSetSize;
        }
        uint64_t used = memory.ullTotalPhys - std::min(memory.ullTotalPhys, memory.ullAvailPhys);
        uint64_t values[] = {native, guest, used > native + guest ? used - native - guest : 0};
        const wchar_t* names[] = {L"Finder", L"Mac Subsystem", L"Other software"};
        for (unsigned row = 0; row < 3; ++row) {
            int y = about_header_height + row * about_row_height;
            classic::draw_icon(dc, 16, y + 1,
                               row == 0   ? app_icon
                               : row == 1 ? app_icon
                                          : classic_art[9],
                               16, 16, 0, nullptr, DI_NORMAL);
            text({40, y + 3, 127, y + 15}, names[row]);
            text({127, y + 3, 185, y + 15}, format_bytes(values[row]));
            RECT bar{191, y + 3, 358, y + 14};
            classic::draw_progress(dc, bar, values[row], memory.ullTotalPhys);
        }
        SelectObject(dc, old_font);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        KillTimer(window, 1);
        RemovePropW(window, L"FinderWhite");
    }
    return DefWindowProcW(window, message, wp, lp);
}

// Create and activate a native About This PC-style information window.
void show_about_system()
{
    WNDCLASSW type{};
    type.hInstance = instance;
    type.lpszClassName = L"FinderSystemInfo";
    type.lpfnWndProc = about_system_proc;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&type);
    HWND window = CreateWindowW(type.lpszClassName, L"About This PC",
                                WS_CHILD | WS_CAPTION | WS_SYSMENU | WS_CLIPSIBLINGS, 100, 70, about_width + 4,
                                about_header_height + 3 * about_row_height + 21, desktop, nullptr, instance, nullptr);
    ShowWindow(window, SW_SHOW);
    classic::activate(window);
}

// The modeless viewer owns host-data copies, never clipboard-owned handles.
// Sequence polling also retries a busy clipboard without losing the last view.
struct ClipboardView {
    HostClipboard contents;
    bool known = false;
};

void update_clipboard_view(HWND window, ClipboardView& view)
{
    if (view.known && view.contents.sequence && view.contents.sequence == host_clipboard_sequence())
        return;
    try {
        auto contents = read_host_clipboard(window);
        if (!contents)
            return;
        view.contents = std::move(*contents);
        view.known = true;
        auto edit = GetDlgItem(window, 1);
        SetWindowTextW(edit, view.contents.text.empty() ? L"The clipboard is empty." : view.contents.text.c_str());
        // Host clipboard updates can arrive while another application owns
        // focus. Refresh this child without activating Finder or its edit box.
        ShowWindow(edit, view.contents.picture ? SW_HIDE : SW_SHOWNOACTIVATE);
        InvalidateRect(window, nullptr, TRUE);
    }
    catch (...) {
        // Retry read/allocation failures on the next timer without changing
        // the real clipboard or replacing the last complete snapshot.
    }
}

LRESULT CALLBACK clipboard_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    auto* view = reinterpret_cast<ClipboardView*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        classic::decorate(window);
        SetPropW(window, L"FinderWhite", reinterpret_cast<HANDLE>(1));
    }
    if (message == WM_CREATE) {
        view = new (std::nothrow) ClipboardView;
        if (!view)
            return -1;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
        control(window, L"EDIT", L"Clipboard is currently unavailable.", ES_MULTILINE | ES_READONLY | WS_VSCROLL, 1, 8,
                8, 344, 208);
        SetTimer(window, 1, 250, nullptr);
        update_clipboard_view(window, *view);
        return 0;
    }
    if (message == WM_TIMER && view) {
        update_clipboard_view(window, *view);
        return 0;
    }
    if (message == WM_PAINT && view && view->contents.picture) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT bounds;
        GetClientRect(window, &bounds);
        classic::fill_rect(dc, &bounds, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        int width = view->contents.picture_size.cx, height = view->contents.picture_size.cy;
        double scale = std::min(
            {1.0, std::max(0L, bounds.right - 16) / double(width), std::max(0L, bounds.bottom - 16) / double(height)});
        int shown_width = int(width * scale), shown_height = int(height * scale);
        int left = (bounds.right - shown_width) / 2, top = (bounds.bottom - shown_height) / 2;
        classic::draw_bitmap(dc, {left, top, left + shown_width, top + shown_height}, view->contents.picture);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_SIZE) {
        MoveWindow(GetDlgItem(window, 1), 8, 8, std::max(0, int(LOWORD(lp)) - 16), std::max(0, int(HIWORD(lp)) - 16),
                   TRUE);
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    }
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        KillTimer(window, 1);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        delete view;
        RemovePropW(window, L"FinderWhite");
    }
    return DefWindowProcW(window, message, wp, lp);
}

void show_clipboard()
{
    WNDCLASSW type{};
    type.hInstance = instance;
    type.lpszClassName = L"FinderClipboard";
    type.lpfnWndProc = clipboard_proc;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&type);
    HWND window = CreateWindowW(type.lpszClassName, L"Clipboard",
                                WS_CHILD | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPSIBLINGS, 90, 60, 364, 245,
                                desktop, nullptr, instance, nullptr);
    ShowWindow(window, SW_SHOW);
    classic::activate(window);
}

// Identify focused text editing so menu commands can target text instead of
// filesystem selections.
HWND active_edit()
{
    HWND focus = GetFocus();
    wchar_t name[32]{};
    if (focus && GetClassNameW(focus, name, 32) && _wcsicmp(name, L"EDIT") == 0)
        return focus;
    return nullptr;
}

InfoData* active_info_icon()
{
    HWND focus = GetFocus();
    HWND parent = focus ? GetParent(focus) : nullptr;
    wchar_t type[32]{};
    if (!parent || !GetClassNameW(parent, type, std::size(type)) || wcscmp(type, L"FinderInfo"))
        return nullptr;
    auto* info = reinterpret_cast<InfoData*>(GetWindowLongPtrW(parent, GWLP_USERDATA));
    return info && focus == info->icon_control ? info : nullptr;
}

bool folder_input_active()
{
    if (GetCapture() || std::any_of(folders.begin(), folders.end(), [](const FolderWindow* folder) {
            return ListView_GetEditControl(folder->list) != nullptr;
        }))
        return true;
    GUITHREADINFO gui{};
    gui.cbSize = sizeof gui;
    return GetGUIThreadInfo(GetCurrentThreadId(), &gui) && (gui.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE));
}

// Open folder windows follow their items, not obsolete host path strings.
// Windows representing a displaced/deleted item close rather than exposing
// private replacement storage. Cached item paths keep their saved layout keys.
void follow_moves(const std::vector<Relocation>& changes)
{
    for (const auto& change : changes) {
        fs::path updated;
        for (auto it = last_search_roots.begin(); it != last_search_roots.end();)
            if (!it->image && relocated_path(it->path, change, updated)) {
                if (change.target.empty())
                    it = last_search_roots.erase(it);
                else {
                    it->path = updated;
                    ++it;
                }
            }
            else
                ++it;
        auto open = folders;
        for (auto* folder : open) {
            if (folder->desktop || !relocated_path(folder->path, change, updated))
                continue;
            if (change.target.empty() || hidden_metadata(change.target.parent_path()) || folder->search) {
                SendMessageW(folder->window, WM_CLOSE, 0, 0);
                continue;
            }
            folder->path = updated;
            for (auto& item : folder->items) {
                fs::path relocated;
                if (relocated_path(item.path, change, relocated))
                    item.path = relocated;
            }
            save_positions(*folder);
            save_folder_view(*folder);
            MacFile catalog;
            try {
                catalog = mac_file(folder->path);
            }
            catch (...) { /* A damaged icon fork must not orphan the window. */
            }
            auto title = mac_display_name(folder->path, catalog);
            SetWindowTextW(folder->window, title.c_str());
        }
    }
}

// An image window follows a catalog directory, including after its name changes.
// Close windows whose catalog object was removed by a replacement or deletion.
void refresh_catalog_window(FolderWindow& folder)
{
    if (folder.image_volume) {
        FinderImageEntry directory{};
        auto error = ROMlib_finder_image_entry(folder.image_volume, folder.image_directory, -1, &directory);
        if (error == -43 || error == -120 || error == -35) {
            SendMessageW(folder.window, WM_CLOSE, 0, 0);
            return;
        }
        if (!error) {
            auto title = image_text(directory.name);
            if (title != folder.image_title) {
                folder.image_title = title;
                SetWindowTextW(folder.window, title.c_str());
            }
            // Geometry/settings keys include ancestor IDs. Follow catalog
            // moves even when the moved item is an ancestor of this window.
            auto root = folder.path;
            while (root.filename().wstring().find(L".mounted-volume-") != 0 && root.has_relative_path())
                root = root.parent_path();
            std::vector<uint32_t> ancestors;
            for (unsigned depth = 0; directory.id != 2 && depth < 128; ++depth) {
                ancestors.push_back(directory.id);
                if (ROMlib_finder_image_entry(folder.image_volume, directory.parent, -1, &directory))
                    break;
            }
            if (directory.id == 2 && root.filename().wstring().find(L".mounted-volume-") == 0) {
                for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
                    root /= std::to_wstring(*it);
                folder.path = std::move(root);
            }
        }
    }
    refresh(folder);
}

void confirm_image_deletion(HWND owner, int16_t volume, const std::vector<ImageCatalogItem>& items)
{
    if (items.empty())
        return;
    auto text = L"This Macintosh image has no Windows Recycle Bin. Permanently delete " +
                (items.size() == 1 ? L"\x201c" + image_text(items[0].name.c_str()) + L"\x201d"
                                   : std::to_wstring(items.size()) + L" selected items") +
                L" and any contents?";
    if (!classic::confirm(owner, text.c_str(), L"Delete"))
        return;
    classic::WaitingCursor waiting;
    // A failed disk write can still leave earlier catalog entries deleted.
    std::exception_ptr error;
    try {
        delete_image_items(volume, items);
    }
    catch (...) {
        error = std::current_exception();
    }
    auto open = folders;
    for (auto* folder : open)
        if (folder->image_volume == volume)
            refresh_catalog_window(*folder);
    if (error)
        std::rethrow_exception(error);
}

// Persist published positions using the folder's normal icon catalog keys.
void save_operation_position(const fs::path& path, uint32_t image_id, bool desktop_view, POINT point)
{
    auto position = std::to_wstring(point.x) + L"," + std::to_wstring(point.y);
    if (image_id) {
        save(L"ImagePositionsNative", path_key(path.parent_path() / std::to_wstring(image_id)), position);
    }
    else if (desktop_view) {
        Item item;
        item.path = path;
        try {
            item.mac = mac_file(path);
        }
        catch (...) {
            // An unreadable fork still has a place on the desktop.
        }
        item.name = host_icon_name(path, item.mac);
        if (has_catalog_icon_position(item))
            save_desktop_catalog_override(item, position);
        else
            save(L"PositionsNative", path_key(fs::path(L"Desktop") / item.name), position);
    }
    else
        save(L"PositionsNative", path_key(path), position);
}

// Save before refreshing the destination so a newly published icon is
// drawn at the drop point on its first paint, even if its window was closed.
void save_drop_positions(const CopyJob& job, const DropPlacement& drop)
{
    for (const auto& result : job.published_items) {
        size_t slot = drop.first + result.index;
        POINT point{drop.origin.x + LONG(slot % drop.columns) * icon_cell_width,
                    drop.origin.y + LONG(slot / drop.columns) * icon_cell_height};
        save_operation_position(job.destinations.at(result.index), result.image_id, drop.desktop, point);
    }
}

// Publish worker results before command/menu dispatch as well as during idle.
// An immediate Undo must refer to the completed move, not an older operation.
void publish_completed_operations()
{
    // Drag indexes and inline editors belong to the current row snapshot.
    // Queue completed results until input releases that snapshot.
    if (folder_input_active())
        return;
    // Retry a busy clipboard after a successful Cut. A newer clipboard from
    // any application must survive even if this background move just finished.
    for (auto& operation : operations)
        if (operation->clipboard_sequence && operation->job->state >= CopyJob::complete) {
            if (operation->job->state != CopyJob::complete ||
                finish_host_clipboard_move(desktop, operation->clipboard_sequence))
                operation->clipboard_sequence = 0;
        }
    if (std::none_of(operations.begin(), operations.end(), [](const std::unique_ptr<Operation>& operation) {
            return !operation->finished && operation->job->state >= CopyJob::complete;
        }))
        return;
    std::vector<std::pair<std::shared_ptr<CopyJob>, std::function<void(const CopyJob&)>>> completions;
    classic::RedrawBatch redraw(desktop);
    for (auto& op : operations)
        if (!op->finished && op->job->state >= CopyJob::complete) {
            op->finished = true;
            std::vector<Relocation> moved;
            for (const auto& change : op->job->moved) {
                moved.push_back({change.first, change.second});
                if (change.second.parent_path() == desktop_folder)
                    save(L"PutAway", path_key(change.second), change.first.wstring());
            }
            auto recovery = std::make_shared<std::vector<std::shared_ptr<void>>>(op->job->replacements);
            if (op->job->archived_sources.empty())
                history.remember_moves(moved, L"Move", recovery);
            else {
                history.clear();
                std::vector<Relocation> archived;
                for (const auto& source : op->job->archived_sources)
                    archived.push_back({source, {}});
                follow_moves(archived);
            }
            follow_moves(moved);
            ExtractionPlacement placement;
            if (op->extraction && !op->job->published_items.empty()) {
                placement = extraction_placement(*op->extraction, op->job->published_items.size());
                for (size_t i = 0; i < placement.positions.size(); ++i) {
                    const auto& result = op->job->published_items[i];
                    save_operation_position(op->job->destinations.at(result.index), result.image_id,
                                            placement.folder->desktop, placement.positions[i]);
                }
            }
            if (op->drop)
                save_drop_positions(*op->job, *op->drop);
            op->job->replacements.clear();
            op->job->source_owner.reset();
            if (op->job->state == CopyJob::complete && op->on_complete)
                completions.emplace_back(op->job, std::move(op->on_complete));
            auto open = folders;
            for (auto* folder : open) {
                HWND viewport = placement.folder == folder ? folder->list : nullptr;
                POINT before{};
                if (viewport)
                    ListView_GetOrigin(viewport, &before);
                refresh_catalog_window(*folder);
                // Rebuilding the rows can reset a scrolled icon view. Keep
                // the visible part of the canvas where extraction placed icons.
                POINT after{};
                if (viewport && IsWindow(viewport) && ListView_GetOrigin(viewport, &after) &&
                    (before.x != after.x || before.y != after.y))
                    ListView_Scroll(viewport, before.x - after.x, before.y - after.y);
                // A packed viewport may have no spare cell. Reveal the
                // first result in its normal layout position in that case.
                if (viewport && IsWindow(viewport) && placement.positions.empty()) {
                    const auto& result = op->job->published_items.front();
                    const auto& path = op->job->destinations.at(result.index);
                    const auto& items = folder->items;
                    auto row = std::find_if(items.begin(), items.end(), [&](const Item& item) {
                        return result.image_id ? item.image_id == result.image_id : item.path == path;
                    });
                    if (row != items.end())
                        ListView_EnsureVisible(viewport, std::distance(items.begin(), row), FALSE);
                }
            }
            if (op->job->state == CopyJob::complete)
                ShowWindow(op->window, SW_HIDE);
        }
    for (auto& [job, after_copy] : completions) {
        try {
            after_copy(*job);
        }
        catch (const std::exception& error) {
            failure(desktop, error);
        }
    }
}

// Shell operations may be cancelled after recycling part of a selection.
// Publish actual source removals even on an error, without retaining private
// paths or recording guessed Recycle Bin backing names as ordinary moves.
void recycle_selection(HWND owner, const std::vector<fs::path>& paths)
{
    if (paths.empty())
        return;
    std::exception_ptr error;
    try {
        trash_items(owner, paths);
    }
    catch (...) {
        error = std::current_exception();
    }
    history.clear();
    std::vector<Relocation> removed;
    for (const auto& path : paths) {
        std::error_code status;
        if (!fs::exists(path, status) && !status)
            removed.push_back({path, {}});
    }
    follow_moves(removed);
    for (auto* folder : folders)
        refresh(*folder);
    if (error)
        std::rethrow_exception(error);
}

std::vector<SearchRoot> all_find_disks()
{
    std::vector<SearchRoot> roots;
    wchar_t drives[512]{};
    DWORD length = GetLogicalDriveStringsW(std::size(drives), drives);
    if (length && length < std::size(drives))
        for (const wchar_t* drive = drives; *drive; drive += wcslen(drive) + 1) {
            auto type = GetDriveTypeW(drive);
            if (type != DRIVE_NO_ROOT_DIR && type != DRIVE_UNKNOWN) {
                SearchRoot root{fs::path(drive)};
                root.title = L"on " + volume_display_name(root.path, drive);
                roots.push_back(std::move(root));
            }
        }
    for (const auto& volume : image_volumes()) {
        SearchRoot root{image_volume_key(volume), volume.reference};
        root.title = L"on " + image_text(volume.name);
        roots.push_back(std::move(root));
    }
    return roots;
}

void find_items(FolderWindow& folder, bool again)
{
    auto options = last_find;
    auto roots = last_search_roots;
    auto disks = all_find_disks();
    if (!again || roots.empty()) {
        std::vector<SearchRoot> selection;
        for (int i = -1; (i = selected_item_after(folder.list, folder.items.size(), i)) >= 0;) {
            const auto& item = folder.items[i];
            if (item.special != 3)
                selection.push_back(
                    {item.path, item.image_volume, item.image_id, item.image_parent, item.image_name, !item.special});
        }
        SearchRoot current{folder.desktop ? romaw_folder : folder.path, folder.image_volume, folder.image_directory};
        if (folder.search) {
            if (folder.search->roots.size() == 1 && !folder.search->roots.front().include_item)
                current = folder.search->roots.front();
            else
                current = {romaw_folder};
        }
        HWND owner = folder.window;
        for (;;) {
            if (!classic::find_box(IsWindow(owner) ? owner : desktop, options, !selection.empty(), disks))
                return;
            auto error = options.query.validate();
            if (!error)
                break;
            message(IsWindow(owner) ? owner : desktop, error, MB_ICONWARNING);
        }
        roots = options.scope == FindScope::selection ? std::move(selection) : std::vector<SearchRoot>{current};
    }
    if (options.scope == FindScope::all_disks)
        roots = std::move(disks);
    else if (options.scope == FindScope::disk) {
        auto disk =
            std::find_if(disks.begin(), disks.end(), [&](const auto& root) { return root.path == options.disk; });
        if (disk == disks.end())
            throw std::runtime_error("The selected disk is no longer mounted.");
        roots.assign(1, *disk);
    }
    if (roots.empty())
        throw std::runtime_error("There are no available locations to search.");
    auto search = std::make_shared<Search>(roots, options, show_hidden);
    open_folder(roots.front().path, std::move(search));
    last_find = std::move(options);
    last_search_roots = std::move(roots);
}

// Dispatch menu/keyboard commands on the UI thread, preserving text-edit
// shortcuts before acting on selected files.
void command(FolderWindow& f, unsigned id)
{
    if (drag_folder)
        SendMessageW(drag_folder->window, WM_CANCELMODE, 0, 0);
    publish_completed_operations();
    if (std::find(folders.begin(), folders.end(), &f) == folders.end())
        return;
    if (auto* info = active_info_icon();
        info && (id == copy_command || id == cut_command || id == paste_command || id == clear_command)) {
        SendMessageW(GetParent(info->icon_control), info_icon_message, id, 0);
        return;
    }
    if (auto edit = active_edit()) {
        UINT message = id == undo_command    ? WM_UNDO
                       : id == cut_command   ? WM_CUT
                       : id == copy_command  ? WM_COPY
                       : id == paste_command ? WM_PASTE
                       : id == clear_command ? WM_CLEAR
                                             : 0;
        if (message) {
            SendMessageW(edit, message, 0, 0);
            return;
        }
        if (id == select_all_command) {
            SendMessageW(edit, EM_SETSEL, 0, -1);
            return;
        }
    }
    if (f.search && (id == new_folder_command || id == paste_command || id == parent_command))
        return;
    auto paths = selected(f);
    std::vector<int16_t> images;
    for (int index = -1; (index = selected_item_after(f.list, f.items.size(), index)) >= 0;)
        if (f.items[index].special == 4)
            images.push_back(f.items[index].image_volume);
    if ((id == put_away_command || id == eject_command || id == trash_command || id == clear_command) &&
        !images.empty()) {
        for (int16_t reference : images) {
            int16_t error = ROMlib_finder_image_unmount(reference);
            if (error) {
                message(f.window,
                        L"Cannot put away this image (Macintosh error " + std::to_wstring(error) +
                            L"). Close files using the disk and try again.",
                        MB_ICONERROR);
                continue;
            }
            auto windows = folders;
            for (auto* folder : windows)
                if (folder->image_volume == reference)
                    SendMessageW(folder->window, WM_CLOSE, 0, 0);
        }
        for (auto* folder : folders)
            if (folder->desktop)
                refresh(*folder);
        return;
    }
    std::vector<ImageCatalogItem> catalog_selection;
    if (f.image_volume)
        for (int i = -1; (i = selected_item_after(f.list, f.items.size(), i)) >= 0;) {
            const auto& item = f.items[i];
            catalog_selection.push_back({item.image_parent, item.image_id, item.image_name});
        }
    auto target = f.desktop ? desktop_folder : f.path;
    switch (id) {
    case extract_here_command:
    case extract_to_command: {
        std::vector<Item> archives;
        for (int index = -1; (index = selected_item_after(f.list, f.items.size(), index)) >= 0;)
            archives.push_back(f.items[index]);
        if (id == extract_here_command && !archive_destination_allowed({target, f.image_volume, f.image_directory}))
            throw std::runtime_error("Extract Here requires a writable Mac-compatible drive or image");
        extract_items(f.window, archives, {target, f.image_volume, f.image_directory}, id == extract_to_command);
        break;
    }
    case open_command:
        open_selected(f);
        break;
    case parent_command:
        if (f.image_volume && f.image_directory != 2) {
            FinderImageEntry child{}, parent{};
            if (ROMlib_finder_image_entry(f.image_volume, f.image_directory, -1, &child) ||
                ROMlib_finder_image_entry(f.image_volume, child.parent, -1, &parent))
                throw std::runtime_error("Cannot open the enclosing image folder");
            open_folder(f.path.parent_path(), nullptr, f.image_volume, parent.id, image_text(parent.name));
        }
        else if (!f.image_volume && !f.desktop && f.path != f.path.root_path() &&
                 _wcsicmp(f.path.c_str(), windows_desktop_path.c_str()) != 0)
            open_folder(f.path.parent_path());
        break;
    case new_folder_command: {
        classic::WaitingCursor waiting;
        if (f.image_volume) {
            auto created = create_image_folder(f.image_volume, f.image_directory);
            refresh(f);
            for (int i = 0; i < int(f.items.size()); ++i)
                if (f.items[i].image_id == created) {
                    ListView_SetItemState(f.list, -1, 0, LVIS_SELECTED);
                    ListView_SetItemState(f.list, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_EnsureVisible(f.list, i, FALSE);
                    break;
                }
            break;
        }
        auto p = target / L"Untitled Folder";
        for (unsigned n = 2; fs::exists(p); ++n)
            p = target / (L"Untitled Folder " + std::to_wstring(n));
        fs::create_directory(p);
        history.created(p, L"New Folder");
        refresh(f);
        break;
    }
    case duplicate_command:
        if (f.image_volume && !catalog_selection.empty()) {
            classic::WaitingCursor waiting;
            std::map<fs::path, std::vector<ImageCopySource>> groups;
            for (int i = -1; (i = selected_item_after(f.list, f.items.size(), i)) >= 0;) {
                const auto& row = f.items[i];
                groups[row.path.parent_path()].push_back(
                    {f.path, 0, {}, f.image_volume, {row.image_parent, row.image_id, row.image_name}});
            }
            for (const auto& [parent, sources] : groups)
                if (auto job = copy_into_image(copies, {}, sources, f.image_volume, sources.front().catalog.parent,
                                               parent, TransferKind::duplicate))
                    show_operation(job);
        }
        else if (!f.image_volume && !paths.empty()) {
            std::vector<std::pair<fs::path, fs::path>> changes;
            for (const auto& path : paths)
                changes.emplace_back(path, path);
            if (auto job = copies.start_targets(changes, TransferKind::duplicate))
                show_operation(job);
        }
        break;
    case copy_command:
        write_host_clipboard_files(desktop, paths, false);
        break;
    case paste_command: {
        auto contents = read_host_clipboard(desktop);
        if (!contents)
            throw std::runtime_error("The Windows clipboard is busy. Try Paste again.");
        if (f.image_volume) {
            if (auto job = copy_into_image(copies, contents->files, {}, f.image_volume, f.image_directory, f.path,
                                           contents->move ? TransferKind::move : TransferKind::copy))
                show_operation(job, contents->move ? contents->sequence : 0);
            break;
        }
        if (!contents->files.empty())
            copy_to(f.window, contents->files, target, contents->move ? TransferKind::move : TransferKind::copy,
                    contents->move ? contents->sequence : 0);
        break;
    }
    case rename_command: {
        int i = selected_item_after(f.list, f.items.size(), -1);
        if (i >= 0 && !f.items[i].special && (!f.image_volume || !f.read_only)) {
            SetFocus(f.list);
            ListView_EditLabel(f.list, i);
        }
        break;
    }
    case clear_command:
    case trash_command: {
        if (f.image_volume) {
            if (!f.read_only)
                confirm_image_deletion(f.window, f.image_volume, catalog_selection);
            break;
        }
        recycle_selection(f.window, paths);
        break;
    }
    case label_popup_command: {
        if (paths.empty() && catalog_selection.empty())
            break;
        int current = -1;
        bool mixed = false;
        for (int i = -1; (i = selected_item_after(f.list, f.items.size(), i)) >= 0;) {
            int value = (f.items[i].mac.flags >> 1) & 7;
            if (current >= 0 && current != value)
                mixed = true;
            current = value;
        }
        int chosen = classic::choose_label(desktop, mixed ? -1 : current);
        if (chosen >= 0 && live_folder(&f)) {
            classic::WaitingCursor waiting;
            for (const auto& path : paths)
                set_label(path, unsigned(chosen));
            for (const auto& item : catalog_selection)
                label_image_item(f.image_volume, item, unsigned(chosen));
            refresh(f);
        }
        break;
    }
    case undo_command: {
        classic::WaitingCursor waiting;
        follow_moves(history.undo());
        for (auto* folder : folders)
            refresh(*folder);
        break;
    }
    case cut_command:
        write_host_clipboard_files(desktop, paths, true);
        break;
    case alias_command: {
        classic::WaitingCursor waiting;
        for (const auto& p : paths) {
            auto destination = unique_item(p.parent_path(), p.filename().wstring() + L" alias.lnk");
            create_shortcut(p, destination);
            history.created(destination, L"Make Alias");
        }
        refresh(f);
        break;
    }
    case windows_shortcut_command:
        if (const auto* application = selected_application(f, true)) {
            classic::WaitingCursor waiting;
            make_windows_shortcut(*application);
        }
        break;
    case configure_app_command:
        if (const auto* application = selected_application(f))
            configure_application(f.window, *application);
        break;
    case put_away_command: {
        std::vector<std::pair<fs::path, fs::path>> changes;
        for (const auto& path : paths) {
            auto original = settings_path_value(preferences, L"PutAway", path);
            if (!original.empty())
                changes.push_back({path, fs::path(original)});
        }
        if (auto job = copies.start_targets(changes))
            show_operation(job);
        break;
    }
    case find_command:
    case find_again_command:
        find_items(f, id == find_again_command);
        break;
    case empty_trash_command:
        empty_recycle_bin(f.window);
        for (auto* folder : folders)
            refresh(*folder);
        break;
    case eject_command:
        if (paths.size() == 1)
            eject_volume(f.window, paths[0]);
        break;
    case erase_command:
        if (paths.size() == 1 && can_erase(paths[0]) &&
            classic::confirm(
                f.window,
                L"Erase this removable disk? All its contents will be lost. Windows will ask you to confirm the format settings.",
                L"Erase Disk"))
            SHFormatDrive(f.window, towupper(paths[0].wstring()[0]) - L'A', SHFMT_ID_DEFAULT, 0);
        break;
    case page_setup_command:
        page_setup.lStructSize = sizeof page_setup;
        page_setup.hwndOwner = desktop;
        page_setup.Flags = PSD_MARGINS | PSD_INTHOUSANDTHSOFINCHES | PSD_ENABLEPAGESETUPHOOK;
        page_setup.lpfnPageSetupHook = print_hook;
        if (!page_setup.rtMargin.left)
            page_setup.rtMargin = {500, 500, 500, 500};
        if (!PageSetupDlgW(&page_setup) && CommDlgExtendedError())
            throw std::runtime_error("Page Setup could not be opened. Check the installed printers.");
        break;
    case print_window_command:
        print_folder(f);
        break;
    case print_command:
        for (const auto& p : paths) {
            SHELLEXECUTEINFOW launch{};
            launch.cbSize = sizeof launch;
            launch.hwnd = desktop;
            launch.lpVerb = L"print";
            launch.lpFile = p.c_str();
            launch.nShow = SW_SHOWNORMAL;
            if (!ShellExecuteExW(&launch))
                throw std::runtime_error("No Windows print handler is installed for this document.");
        }
        break;
    case info_command:
        for (int i = -1; (i = selected_item_after(f.list, f.items.size(), i)) >= 0;) {
            auto item = f.items[i];
            if (item.special == 3) {
                open_recycle_bin(f.window, true);
                continue;
            }
            show_info(item);
        }
        break;
    case close_command:
        if (auto window = classic::active_window(); window && window != desktop)
            SendMessageW(window, WM_CLOSE, 0, 0);
        break;
    case clipboard_command:
        show_clipboard();
        break;
    case select_all_command:
        ListView_SetItemState(f.list, -1, LVIS_SELECTED, LVIS_SELECTED);
        break;
    case icons_command:
    case small_icons_command:
    case list_command:
        if (!f.desktop) {
            save_positions(f);
            for (const auto& [command, mode] : folder_view_commands)
                if (command == id)
                    f.presentation.view = mode;
            configure_folder_view(f);
            refresh(f);
            save_folder_view(f);
        }
        break;
    case arrange_command:
    case arrange_selection_command:
    case arrange_name_command: {
        if (f.presentation.view != FolderView::icons)
            break;
        std::vector<IconCleanupEntry> entries;
        HDC dc = GetDC(f.list);
        auto previous_font = dc ? SelectObject(dc, classic::small_font()) : nullptr;
        for (size_t i = 0; i < f.items.size(); ++i) {
            POINT position{};
            ListView_GetItemPosition(f.list, int(i), &position);
            bool fixed = (f.desktop && f.items[i].special != 0) ||
                         (id == arrange_selection_command && !(ListView_GetItemState(f.list, int(i), LVIS_SELECTED)));
            const auto& name = f.items[i].name;
            RECT label = dc ? classic::icon_label_bounds(dc, name.c_str(), {}) : RECT{};
            entries.push_back({name, {int(position.x), int(position.y)}, fixed, int(label.right - label.left)});
        }
        if (dc) {
            SelectObject(dc, previous_font);
            ReleaseDC(f.list, dc);
        }
        RECT client{};
        GetClientRect(f.list, &client);
        auto positions = clean_icon_layout(entries, id == arrange_name_command ? IconCleanup::name : IconCleanup::grid,
                                           client.right, icon_cell_width, icon_cell_height);
        {
            classic::RedrawBatch redraw(f.window);
            for (size_t i = 0; i < positions.size(); ++i) {
                ListView_SetItemPosition32(f.list, int(i), positions[i].x, positions[i].y);
                if (positions[i].x != entries[i].position.x || positions[i].y != entries[i].position.y)
                    save_desktop_catalog_position(f, int(i), positions[i].x, positions[i].y);
            }
            InvalidateRect(f.list, nullptr, TRUE);
        }
        save_positions(f);
        break;
    }
    case refresh_command:
        refresh(f);
        break;
    case show_hidden_command:
        show_hidden = !show_hidden;
        save(L"View", L"ShowHidden", show_hidden ? L"1" : L"0");
        for (auto* folder : folders) {
            if (folder->search)
                folder->search = std::make_shared<Search>(folder->search->roots, folder->search->options, show_hidden);
            refresh(*folder);
        }
        break;
    case spring_folders_command:
        spring_folders = !spring_folders;
        save(L"View", L"SpringFolders", spring_folders ? L"1" : L"0");
        break;
    case quit_command:
        SendMessageW(desktop, WM_CLOSE, 0, 0);
        break;
    case register_images_command:
        if (classic::confirm(f.window,
                             L"Register ROMAW for disk image files for this Windows user?\n\n"
                             L"Existing Windows default-app choices will be preserved.",
                             L"Register")) {
            register_disk_image_types();
            message(f.window, L"Disk image types registered.\n\n"
                              L"To change an existing default, choose ROMAW in Windows Open with or Default apps.");
        }
        break;
    case afp_chooser_command:
        if (auto chooser = static_cast<HWND>(ROMlib_finder_afp_chooser(desktop)))
            classic::activate(chooser);
        break;
    case about_system_command:
        show_about_system();
        break;
    case about_romaw_command:
        classic::about_box(desktop);
        break;
    case romaw_prefs_command: {
        const auto path = file_manager_path((romaw_folder.parent_path() / L"ROMAW.prefs").native());
        if (!path.empty())
            ROMlib_win_default_preferences(desktop, path.c_str());
        else
            message(desktop, L"Unable to open the ROMAW preferences path.");
        break;
    }
    case about_command:
        message(
            f.window,
            ROMAW_VERSION_W L"\nROMAW Finder\n\nNative x64 desktop with classic Macintosh layout and background file operations.");
        break;
    default:
        if (id >= 1000 && id - 1000 < apple_items.size()) {
            const auto& item = apple_items[id - 1000];
            if (item.folder)
                open_folder(item.path);
            else
                launch(f.window, item);
            break;
        }
        if (id >= sort_command && id < sort_command + folder_column_count && !f.desktop) {
            save_positions(f);
            f.presentation.view = FolderView::list;
            f.presentation.sort = FolderSort(id - sort_command);
            f.presentation.descending =
                f.presentation.sort == FolderSort::date || f.presentation.sort == FolderSort::size;
            configure_folder_view(f);
            refresh(f);
            save_folder_view(f);
        }
        if (id >= label_command && id < label_command + 8) {
            classic::WaitingCursor waiting;
            for (const auto& p : paths)
                set_label(p, id - label_command);
            for (const auto& item : catalog_selection)
                label_image_item(f.image_volume, item, id - label_command);
            refresh(f);
        }
    }
}

// Use the regular command state for a pointer or keyboard context menu.
void folder_context_menu(FolderWindow& folder, POINT screen)
{
    if (drag_folder || !live_folder(&folder))
        return;
    HWND window = folder.window;
    bool keyboard = screen.x == -1 && screen.y == -1;
    int index = -1;
    if (keyboard) {
        index = selected_item_after(folder.list, folder.items.size(), -1);
        RECT bounds{};
        if (index >= 0)
            ListView_GetItemRect(folder.list, index, &bounds, LVIR_LABEL);
        screen = {bounds.left + 10, bounds.bottom + 4};
        ClientToScreen(folder.list, &screen);
    }
    else {
        LVHITTESTINFO hit{};
        hit.pt = screen;
        ScreenToClient(folder.list, &hit.pt);
        index = ListView_HitTest(folder.list, &hit);
        if (index >= 0 && !ListView_GetItemState(folder.list, index, LVIS_SELECTED)) {
            ListView_SetItemState(folder.list, -1, 0, LVIS_SELECTED);
            ListView_SetItemState(folder.list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
    }
    bool item_menu = index >= 0 && size_t(index) < folder.items.size();
    classic::activate(window);
    SetFocus(folder.list);
    current_folder = &folder;
    HMENU bar = classic::window_menu(desktop);
    SendMessageW(desktop, WM_INITMENU, reinterpret_cast<WPARAM>(bar), 0);
    if (!IsWindow(window))
        return;
    std::unique_ptr<std::remove_pointer_t<HMENU>, decltype(&DestroyMenu)> popup(CreatePopupMenu(), DestroyMenu);
    if (!popup)
        return;
    auto add = [&](unsigned id, const wchar_t* label) {
        UINT state = GetMenuState(bar, id, MF_BYCOMMAND);
        AppendMenuW(popup.get(), MF_STRING | (state == UINT(-1) ? MF_GRAYED : state & (MF_GRAYED | MF_DISABLED)), id,
                    label);
    };
    if (item_menu) {
        add(open_command, L"Open");
        add(extract_here_command, L"Extract Here");
        add(extract_to_command, L"Extract To...");
        add(info_command, L"Get Info");
        add(configure_app_command, L"Configure App...");
        add(duplicate_command, L"Duplicate");
        add(rename_command, L"Rename");
        AppendMenuW(popup.get(), MF_SEPARATOR, 0, nullptr);
        add(alias_command, L"Make Alias");
        if (selected_application(folder, true))
            add(windows_shortcut_command, L"Make Windows Shortcut");
        AppendMenuW(popup.get(), MF_SEPARATOR, 0, nullptr);
        add(copy_command, L"Copy");
        add(trash_command, L"Move to Trash");
        add(arrange_selection_command, L"Clean Up Selection");
    }
    else {
        add(new_folder_command, L"New Folder");
        add(paste_command, L"Paste");
        add(arrange_name_command, L"Clean Up by Name");
        add(arrange_command, L"Clean Up to Grid");
        add(refresh_command, L"Refresh");
        if (folder.desktop)
            add(empty_trash_command, L"Empty Trash");
    }
    unsigned selected_command = classic::track_context_menu(desktop, popup.get(), screen);
    if (selected_command && IsWindow(window))
        PostMessageW(window, WM_COMMAND, selected_command, 0);
}

bool same_suitcase(const Item& a, const Item& b)
{
    return a.image_volume == b.image_volume &&
           (a.image_volume ? a.image_id == b.image_id : !_wcsicmp(a.path.c_str(), b.path.c_str()));
}

bool copy_suitcase_drop(const Item& source, int16_t volume, const fs::path& path)
{
    bool same = source.image_volume || volume ? source.image_volume == volume
                                              : same_storage_volume(source.path, path);
    return drag_copies(same, GetKeyState(VK_MENU) < 0 || GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_SHIFT) < 0);
}

void require_writable_suitcase(const Item& item, const SuitcaseFork& fork)
{
    if (fork.attributes & 128) // mapReadOnly
        throw std::runtime_error("The suitcase resource map is locked.");
    if (item.image_volume) {
        if (ROMlib_finder_image_check_mutable(item.image_volume, item.image_parent, item.image_name.c_str(),
                                              item.image_id))
            throw std::runtime_error("The suitcase is locked or unavailable.");
    }
    else {
        auto attributes = GetFileAttributesW(item.path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || attributes & FILE_ATTRIBUTE_READONLY)
            throw std::runtime_error("The suitcase is locked or unavailable.");
    }
}

void replace_suitcase(const Item& item, const SuitcaseSnapshot& before, const Bytes& after)
{
    if (item.image_volume) {
        auto error = ROMlib_finder_image_replace_fork(item.image_volume, item.image_parent, item.image_name.c_str(),
                                                     item.image_id, !before.data_fork, before.bytes.data(),
                                                     uint32_t(before.bytes.size()), after.data(), uint32_t(after.size()));
        if (error)
            throw std::runtime_error("Cannot update the image suitcase (Macintosh error " + std::to_string(error) + ").");
    }
    else {
        auto actual = read_suitcase(item);
        if (actual.data_fork != before.data_fork || actual.bytes != before.bytes)
            throw std::runtime_error("The suitcase changed during the transfer.");
        replace_host_suitcase_fork(item.path, after, before.data_fork);
    }
}

void refresh_suitcase(const Item& item, const Bytes& bytes)
{
    for (HWND window = nullptr; (window = FindWindowExW(desktop, window, suitcase_window_class, nullptr));)
        if (auto* view = suitcase_view(window); view && same_suitcase(view->item, item))
            populate_suitcase(*view, bytes);
}

auto find_suitcase_resource(const SuitcaseFork& fork, const ResourceKey& key)
{
    return std::find_if(fork.resources.begin(), fork.resources.end(), [&](const auto& r) { return r.key == key; });
}

// Preserve the accessory's owned resources (Inside Macintosh, Resource
// Manager) and a font family's size/style associations.
std::set<ResourceKey> suitcase_dependencies(const SuitcaseFork& fork, const SuitcaseResource& resource)
{
    std::set<ResourceKey> keys;
    if (resource.key.first == 0x44525652 && resource.key.second >= 0 && resource.key.second <= 63) {
        const unsigned base = 0xc000u | (unsigned(resource.key.second) << 5);
        for (const auto& candidate : fork.resources)
            if ((uint16_t(candidate.key.second) & 0xffe0u) == base)
                keys.insert(candidate.key);
        return keys;
    }
    if (resource.key.first != 0x464f4e44) // FOND
        return keys;
    for (const auto& entry : font_associations(resource.payload)) {
        if (!entry.size) {
            ResourceKey key{0x73666e74, entry.id}; // sfnt
            if (find_suitcase_resource(fork, key) != fork.resources.end())
                keys.insert(key);
        }
        else {
            ResourceKey key{0x4e464e54, entry.id}; // NFNT, or the old FONT slot
            if (find_suitcase_resource(fork, key) == fork.resources.end())
                key.first = 0x464f4e54;
            if (find_suitcase_resource(fork, key) != fork.resources.end())
                keys.insert(key);
        }
    }
    return keys;
}

std::set<ResourceKey> suitcase_member_keys(const SuitcaseFork& fork, const SuitcaseMember& member)
{
    ResourceKey key{member.resource_type, member.resource_id};
    auto resource = find_suitcase_resource(fork, key);
    if (resource == fork.resources.end())
        throw std::runtime_error("The suitcase member changed during the drag.");
    auto keys = suitcase_dependencies(fork, *resource);
    keys.insert(key);
    return keys;
}

SuitcaseFork select_suitcase_resources(const SuitcaseFork& source, const std::set<ResourceKey>& keys)
{
    SuitcaseFork result;
    for (const auto& resource : source.resources)
        if (keys.count(resource.key))
            result.resources.push_back(resource);
    return result;
}

void require_removable_resources(const SuitcaseFork& source, const std::set<ResourceKey>& keys)
{
    for (const auto& resource : source.resources) {
        if (keys.count(resource.key)) {
            if (resource.attributes & 8) // resProtected
                throw std::runtime_error("A selected suitcase resource is protected.");
            continue;
        }
        for (auto key : suitcase_dependencies(source, resource))
            if (keys.count(key))
                throw std::runtime_error("This font resource belongs to a family still in the suitcase. Copy it or move the family.");
    }
}

void remove_suitcase_resources(SuitcaseFork& source, const std::set<ResourceKey>& keys)
{
    require_removable_resources(source, keys);
    source.resources.erase(std::remove_if(source.resources.begin(), source.resources.end(),
                                          [&](const auto& r) { return keys.count(r.key) != 0; }),
                           source.resources.end());
}

// Test every collision before changing the destination. An identical
// dependency can be shared by more than one item in a suitcase.
bool merge_suitcase_resources(HWND owner, SuitcaseFork& target, const SuitcaseFork& incoming)
{
    auto equal = [](const auto& a, const auto& b) {
        return a.attributes == b.attributes && a.name == b.name && a.payload == b.payload;
    };
    bool conflict = false;
    for (const auto& resource : incoming.resources) {
        auto found = find_suitcase_resource(target, resource.key);
        if (found == target.resources.end() || equal(*found, resource))
            continue;
        if (found->attributes & 8)
            throw std::runtime_error("The destination contains a protected resource with the same ID.");
        for (const auto& owner_resource : target.resources)
            if (find_suitcase_resource(incoming, owner_resource.key) == incoming.resources.end() &&
                suitcase_dependencies(target, owner_resource).count(resource.key))
                throw std::runtime_error("An existing suitcase member still uses a resource with that ID.");
        conflict = true;
    }
    if (conflict && !classic::confirm(owner, L"Replace resources with matching IDs in the destination suitcase?",
                                      L"Replace"))
        return false;
    for (const auto& resource : incoming.resources) {
        auto found = find_suitcase_resource(target, resource.key);
        if (found == target.resources.end())
            target.resources.push_back(resource);
        else
            target.resources[size_t(found - target.resources.begin())] = resource;
    }
    return true;
}

bool suitcase_accepts(const Item& item, const SuitcaseMember& member)
{
    if (item.mac.type == 0x5a535953 || item.mac.type == 0x7a737973) // ZSYS
        return true;
    if (item.mac.type == 0x4446494c || item.mac.type == 0x6466696c) // DFIL
        return member.resource_type == 0x44525652;
    return member.icon_type == 0x4646494c || member.icon_type == 0x6666696c || member.icon_type == 0x7466696c;
}

struct SuitcaseDrop {
    FolderWindow* window = nullptr;
    fs::path path;
    int16_t volume = 0;
    uint32_t directory = 0;
    std::optional<Item> suitcase;
};

SuitcaseDrop suitcase_drop_at(POINT point)
{
    HWND hit = WindowFromPoint(point);
    if (auto* view = suitcase_at(hit))
        return {nullptr, view->item.path, view->item.image_volume, 0, view->item};
    for (HWND window = hit; window; window = GetParent(window)) {
        auto found = std::find_if(folders.begin(), folders.end(), [&](auto* f) { return window == f->window; });
        if (found != folders.end()) {
            auto* folder = *found;
            SuitcaseDrop target{folder, folder->desktop ? desktop_folder : folder->path, folder->image_volume,
                                 folder->image_directory, {}};
            LVHITTESTINFO position{};
            position.pt = point;
            ScreenToClient(folder->list, &position.pt);
            int index = ListView_HitTest(folder->list, &position);
            if (index >= 0 && index < int(folder->items.size())) {
                const auto& item = folder->items[index];
                if (item.folder)
                    return {folder, item.path, item.image_volume, item.image_id, {}};
                if (is_mac_suitcase(item.mac))
                    return {folder, item.path, item.image_volume, 0, item};
                if (item.special == 3)
                    return {}; // A suitcase resource has no independent recycle-bin identity.
            }
            return target;
        }
        if (!(GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD))
            break;
    }
    return {};
}

void drop_suitcase_members(HWND owner, const Item& item, const std::vector<SuitcaseMember>& members, POINT point)
{
    auto target = suitcase_drop_at(point);
    if ((!target.window && !target.suitcase) || (target.suitcase && same_suitcase(item, *target.suitcase)))
        return;
    std::optional<DropPlacement> placement;
    if (target.window && target.path == (target.window->desktop ? desktop_folder : target.window->path))
        placement = drop_placement(*target.window, point);
    classic::WaitingCursor waiting;
    const bool copying = copy_suitcase_drop(item, target.volume, target.path);
    auto before = read_suitcase(item);
    auto source = read_suitcase_fork(before.bytes);
    std::vector<std::set<ResourceKey>> per_member;
    std::set<ResourceKey> keys;
    for (const auto& member : members) {
        if (target.suitcase && !suitcase_accepts(*target.suitcase, member))
            throw std::runtime_error("The destination suitcase cannot contain this kind of resource.");
        per_member.push_back(suitcase_member_keys(source, member));
        keys.insert(per_member.back().begin(), per_member.back().end());
    }
    if (!copying) {
        require_writable_suitcase(item, source);
        require_removable_resources(source, keys);
    }
    if (target.suitcase) {
        auto original = read_suitcase(*target.suitcase);
        auto destination = read_suitcase_fork(original.bytes);
        require_writable_suitcase(*target.suitcase, destination);
        if (!merge_suitcase_resources(owner, destination, select_suitcase_resources(source, keys)))
            return;
        auto after = encode_suitcase_fork(destination);
        Bytes remaining;
        if (!copying) {
            remove_suitcase_resources(source, keys);
            remaining = encode_suitcase_fork(source);
        }
        replace_suitcase(*target.suitcase, original, after);
        try {
            if (!copying)
                replace_suitcase(item, before, remaining);
        }
        catch (...) {
            // If the source changed, retain the destination copy.
            refresh_suitcase(*target.suitcase, after);
            throw;
        }
        if (!copying)
            refresh_suitcase(item, remaining);
        refresh_suitcase(*target.suitcase, after);
        return;
    }
    // Use normal resource files so host and image transfers can handle
    // file conflicts and Windows MacBinary conversion.
    auto scratch = std::make_shared<TransferStaging>();
    std::vector<fs::path> paths;
    for (size_t i = 0; i < members.size(); ++i) {
        const auto& member = members[i];
        auto name = member.name;
        for (auto& character : name)
            if (character < 32 || wcschr(L"<>:\"/\\|?*", character))
                character = L'_';
        while (!name.empty() && (name.back() == L'.' || name.back() == L' '))
            name.pop_back();
        if (name.empty() || hidden_metadata(scratch->path / name))
            name = std::wstring(member.kind) + L" " + std::to_wstring(member.resource_id);
        auto path = unique_item(scratch->path, name);
        create_host_resource_file(path, member.icon_type, 0,
                                  encode_suitcase_fork(select_suitcase_resources(source, per_member[i])));
        paths.push_back(std::move(path));
    }
    auto job = target.volume ? copy_into_image(copies, paths, {}, target.volume, target.directory, target.path)
                             : copies.start(paths, target.path, TransferKind::copy);
    if (!job)
        return;
    job->source_owner = scratch;
    if (copying) {
        show_operation(job, 0, {}, placement);
        return;
    }
    show_operation(job, 0, [item, before = std::move(before), source = std::move(source), paths = std::move(paths),
                            per_member = std::move(per_member)](const CopyJob& finished) mutable {
        std::set<ResourceKey> moved;
        for (const auto& result : finished.published_items) {
            auto path = std::find(paths.begin(), paths.end(), finished.sources.at(result.index));
            if (path != paths.end()) {
                const auto& keys = per_member[size_t(path - paths.begin())];
                moved.insert(keys.begin(), keys.end());
            }
        }
        if (moved.empty())
            return;
        remove_suitcase_resources(source, moved);
        auto remaining = encode_suitcase_fork(source);
        replace_suitcase(item, before, remaining);
        refresh_suitcase(item, remaining);
    }, placement);
}

bool standalone_suitcase_member(uint32_t type)
{
    switch (type) {
    case 0x6466696c: // dfil
    case 0x6666696c: // ffil
    case 0x7466696c: // tfil
    case 0x7366696c: // sfil
    case 0x6b66696c: // kfil
        return true;
    default:
        return false;
    }
}

void drop_files_in_suitcase(HWND owner, const Item& target, const std::vector<Item>& items)
{
    classic::WaitingCursor waiting;
    for (const auto& item : items) {
        if (same_suitcase(item, target))
            continue;
        if (item.folder || item.special || !(standalone_suitcase_member(item.mac.type) ||
                                            is_mac_suitcase(item.mac)))
            throw std::runtime_error("Only Macintosh fonts, sounds, keyboard layouts, and desk accessories can be installed here.");
        if (item.mac.type == 0x5a535953 || item.mac.type == 0x7a737973)
            throw std::runtime_error("Open the System suitcase and drag the individual items to transfer.");
        const bool copying = copy_suitcase_drop(item, target.image_volume, target.path);
        std::vector<HostFileStamp> stamps;
        std::vector<fs::path> companions;
        FinderImageEntry catalog{};
        if (item.image_volume) {
            if (ROMlib_finder_image_info(item.image_volume, item.image_parent, item.image_name.c_str(), &catalog) ||
                catalog.id != item.image_id)
                throw std::runtime_error("The source file is no longer available.");
        }
        else if (!copying) {
            companions = mac_companions(item.path);
            stamps.push_back(host_file_stamp(item.path));
            for (const auto& companion : companions)
                stamps.push_back(host_file_stamp(companion));
        }
        auto source = read_suitcase(item);
        auto members = suitcase_members(source.bytes);
        if (members.empty() || std::any_of(members.begin(), members.end(),
                                          [&](const auto& member) { return !suitcase_accepts(target, member); }))
            throw std::runtime_error("The destination suitcase cannot contain this file's resources.");
        auto fork = read_suitcase_fork(source.bytes);
        std::set<ResourceKey> keys;
        for (const auto& member : members) {
            auto associated = suitcase_member_keys(fork, member);
            keys.insert(associated.begin(), associated.end());
        }
        auto incoming = select_suitcase_resources(fork, keys);
        if (!copying) {
            uint64_t data_size = item.image_volume ? catalog.data_size : fs::file_size(item.path);
            if (item.mac.macbinary) {
                ImportedFile wrapper;
                if (!read_macbinary(item.path, wrapper))
                    throw std::runtime_error("The MacBinary source is no longer available.");
                data_size = wrapper.data_size;
            }
            if ((!source.data_fork && data_size) || incoming.resources.size() != fork.resources.size())
                throw std::runtime_error("This file contains other data. Hold Option or Control to copy its resources.");
            if (item.image_volume)
                require_writable_suitcase(item, fork);
        }
        auto original = read_suitcase(target);
        auto destination = read_suitcase_fork(original.bytes);
        require_writable_suitcase(target, destination);
        if (!merge_suitcase_resources(owner, destination, incoming))
            continue;
        std::shared_ptr<HostMoveLease> lease;
        if (!copying) {
            auto actual = read_suitcase(item);
            if (actual.data_fork != source.data_fork || actual.bytes != source.bytes)
                throw std::runtime_error("The source file changed during the transfer.");
            if (!item.image_volume) {
                if (mac_companions(item.path) != companions)
                    throw std::runtime_error("The source file's forks changed during the transfer.");
                lease = lock_host_move(stamps);
                lease->verify();
            }
        }
        auto after = encode_suitcase_fork(destination);
        replace_suitcase(target, original, after);
        refresh_suitcase(target, after);
        if (!copying) {
            if (item.image_volume)
                delete_image_items(item.image_volume, {{item.image_parent, item.image_id, item.image_name}});
            else {
                lease->remove();
                lease.reset();
            }
            history.clear();
            follow_moves({{item.path, {}}});
            for (HWND window = nullptr; (window = FindWindowExW(desktop, window, suitcase_window_class, nullptr));)
                if (auto* view = suitcase_view(window); view && same_suitcase(view->item, item)) {
                    DestroyWindow(window);
                    break;
                }
        }
    }
    for (auto* folder : folders)
        refresh_catalog_window(*folder);
}

// Interpret a screen-space drop as a folder copy or document launch, using a
// snapshot of source host paths.
void drop(FolderWindow& destination, POINT screen, const std::vector<fs::path>& paths)
{
    LVHITTESTINFO hit{};
    hit.pt = screen;
    ScreenToClient(destination.list, &hit.pt);
    int index = ListView_HitTest(destination.list, &hit);
    if (index >= 0 && index < int(destination.items.size())) {
        auto item = destination.items[index];
        if (item.image_volume && item.folder) {
            if (auto job = copy_into_image(copies, paths, {}, item.image_volume, item.image_id, item.path))
                show_operation(job);
            return;
        }
        auto alias = shortcut_target(item.path);
        if (!alias.empty()) {
            item.path = alias;
            item.mac = mac_file(alias);
            item.folder = fs::is_directory(alias);
        }
        if (item.special == 3) {
            if (GetKeyState(VK_MENU) < 0 || GetKeyState(VK_CONTROL) < 0)
                throw std::runtime_error(
                    "The Recycle Bin does not accept copies. Release Control or Option to recycle these items.");
            recycle_selection(destination.window, paths);
            return;
        }
        if (!item.folder && (item.mac.kind == FINDER_CLASSIC_APPLICATION || item.path.extension() == L".exe")) {
            if (item.image_volume)
                item.path = image_text(image_item_path(item).c_str());
            launch(destination.window, item, paths);
            return;
        }
        if (item.folder) {
            drop_host_items(destination.window, paths, item.path);
            return;
        }
    }
    if (destination.image_volume) {
        auto placement = drop_placement(destination, screen);
        if (auto job = copy_into_image(copies, paths, {}, destination.image_volume, destination.image_directory,
                                       destination.path))
            show_operation(job, 0, {}, placement);
        return;
    }
    drop_host_items(destination.window, paths, destination.desktop ? desktop_folder : destination.path,
                    drop_placement(destination, screen));
}

// Mounted sources are copied, preserving their original image and both forks.
void drop_image(FolderWindow& source, FolderWindow& destination, POINT screen)
{
    fs::path target = destination.desktop ? desktop_folder : destination.path;
    int16_t target_volume = destination.image_volume;
    uint32_t target_directory = destination.image_directory;
    LVHITTESTINFO hit{};
    hit.pt = screen;
    ScreenToClient(destination.list, &hit.pt);
    int index = ListView_HitTest(destination.list, &hit);
    if (index >= 0 && index < int(destination.items.size())) {
        const auto& item = destination.items[index];
        if (item.mac.kind == FINDER_CLASSIC_APPLICATION) {
            auto application = item;
            if (application.image_volume)
                application.path = image_text(image_item_path(application).c_str());
            std::vector<fs::path> documents;
            for (int selected : drag_indices)
                documents.emplace_back(image_text(image_item_path(source.items.at(selected)).c_str()));
            launch(destination.window, application, documents);
            return;
        }
        if (item.special == 3) {
            if (GetKeyState(VK_MENU) < 0 || GetKeyState(VK_CONTROL) < 0 || source.read_only)
                throw std::runtime_error("Choose a writable destination folder for these image items");
            std::vector<ImageCatalogItem> items;
            for (int index : drag_indices) {
                const auto& selected_item = source.items.at(index);
                items.push_back({selected_item.image_parent, selected_item.image_id, selected_item.image_name});
            }
            confirm_image_deletion(source.window, source.image_volume, items);
            return;
        }
        if (item.folder) {
            target = item.path;
            target_volume = item.image_volume;
            target_directory = item.image_id;
        }
    }
    auto volumes = image_volumes();
    std::vector<ImageCopySource> selections;
    std::vector<Item> applications;
    std::vector<size_t> application_indices;
    const bool windows_target = !target_volume && _wcsicmp(target.c_str(), windows_desktop_path.c_str()) == 0;
    for (int selected : drag_indices) {
        const auto& item = source.items.at(selected);
        if (windows_target && !item.folder && item.mac.kind == FINDER_CLASSIC_APPLICATION) {
            application_indices.push_back(selections.size());
            applications.push_back(item);
        }
        auto volume = std::find_if(volumes.begin(), volumes.end(),
                                   [&](const auto& v) { return v.reference == item.image_volume; });
        if (volume == volumes.end())
            throw std::runtime_error("The source image is no longer mounted");
        auto path = image_item_path(item);
        if (target_volume == item.image_volume && item.folder) {
            uint32_t parent = target_directory;
            for (unsigned depth = 0;; ++depth) {
                if (parent == item.image_id)
                    throw std::runtime_error("A folder cannot be copied into itself");
                if (parent == 2)
                    break;
                FinderImageEntry entry{};
                if (depth >= 128 || ROMlib_finder_image_entry(target_volume, parent, -1, &entry))
                    throw std::runtime_error("The destination image folder is unavailable");
                parent = entry.parent;
            }
        }
        const char* relative = strchr(path.c_str(), ':');
        if (!relative)
            throw std::runtime_error("Invalid image item path");
        selections.push_back({fs::path(image_text(volume->source, CP_ACP)),
                              unsigned(volume->partition),
                              relative,
                              item.image_volume,
                              {item.image_parent, item.image_id, item.image_name}});
    }
    bool same_volume = target_volume == source.image_volume;
    bool option = GetKeyState(VK_MENU) < 0 || GetKeyState(VK_CONTROL) < 0;
    auto kind = !same_volume                                 ? TransferKind::copy
                : !option                                    ? TransferKind::move
                : target_directory == source.image_directory ? TransferKind::duplicate
                                                             : TransferKind::copy;
    const int answer = offer_windows_shortcuts(destination.window, applications);
    if (answer == IDCANCEL)
        return;
    if (answer == IDYES)
        for (auto app = application_indices.rbegin(); app != application_indices.rend(); ++app)
            selections.erase(selections.begin() + *app);
    if (selections.empty())
        return;
    classic::WaitingCursor waiting;
    auto placement = target_volume == destination.image_volume && target_directory == destination.image_directory &&
                             target == (destination.desktop ? desktop_folder : destination.path)
                         ? drop_placement(destination, screen)
                         : std::nullopt;
    auto job = target_volume ? copy_into_image(copies, {}, selections, target_volume, target_directory, target, kind)
                             : copy_image_items(copies, selections, target);
    if (job)
        show_operation(job, 0, {}, placement);
}

// Use the same acceptance rule for hover feedback and the eventual drop.
bool accepts_drag_item(const FolderWindow& source, const FolderWindow& destination, int index)
{
    if (index < 0 || index >= int(destination.items.size()))
        return false;
    const auto& item = destination.items[index];
    if (item.special == 3 && source.image_volume &&
        (source.read_only || GetKeyState(VK_MENU) < 0 || GetKeyState(VK_CONTROL) < 0))
        return false;
    if (item.mac.kind != FINDER_CLASSIC_APPLICATION && item.image_volume &&
        ROMlib_finder_image_writable(item.image_volume))
        return false;
    bool accepts = item.folder || item.mac.kind == FINDER_CLASSIC_APPLICATION || item.path.extension() == L".exe";
    return drag_target_allowed(source.window, drag_indices, destination.window, index, accepts);
}

void update_drag_highlight(POINT screen)
{
    if (!drag_folder) {
        drag_highlight.clear();
        spring_target = {};
        return;
    }
    std::vector<HWND> windows;
    for (const auto* folder : folders)
        windows.push_back(folder->window);
    HWND target = drag_destination(drag_image.window_at(screen), windows);
    for (const auto* folder : folders)
        if (folder->window == target) {
            RECT client;
            GetClientRect(folder->list, &client);
            LVHITTESTINFO hit{};
            hit.pt = screen;
            ScreenToClient(folder->list, &hit.pt);
            int index = PtInRect(&client, hit.pt) ? ListView_HitTest(folder->list, &hit) : -1;
            if (accepts_drag_item(*drag_folder, *folder, index)) {
                drag_highlight.set(folder->list, index);
                const auto& item = folder->items[index];
                if (!spring_folders || !item.folder || item.special == 3)
                    spring_target = {};
                else if (spring_target.list != folder->list || spring_target.index != index)
                    spring_target = {folder->list, index,
                                     std::chrono::steady_clock::now() + std::chrono::milliseconds(900)};
                return;
            }
            break;
        }
    drag_highlight.clear();
    spring_target = {};
}

// A spring-opened window participates in the original drag. The source
// keeps capture, so releasing over the new window follows the normal drop path.
void spring_open_folder(bool immediate)
{
    if (!drag_folder || !spring_target.list ||
        (!immediate && std::chrono::steady_clock::now() < spring_target.opened_after))
        return;
    POINT screen{};
    if (!GetCursorPos(&screen))
        return;
    auto target = spring_target;
    update_drag_highlight(screen);
    if (target.list != spring_target.list || target.index != spring_target.index)
        return;
    auto owner =
        std::find_if(folders.begin(), folders.end(), [&](const auto* folder) { return folder->list == target.list; });
    if (owner == folders.end() || size_t(target.index) >= (*owner)->items.size())
        return;
    auto item = (*owner)->items[target.index];
    spring_target.opened_after = std::chrono::steady_clock::time_point::max();
    if (item.path == drag_folder->path)
        return;
    bool already_open = std::any_of(folders.begin(), folders.end(), [&](const auto* folder) {
        return !folder->search && !_wcsicmp(folder->path.c_str(), item.path.c_str());
    });
    auto open_count = folders.size();
    open_folder(item.path, nullptr, item.image_volume, item.image_id, item.name);
    if (!already_open && folders.size() > open_count && drag_folder)
        spring_windows.push_back(folders.back()->window);
    update_drag_highlight(screen);
}

void close_spring_windows(const std::vector<HWND>& opened)
{
    for (auto it = opened.rbegin(); it != opened.rend(); ++it)
        if (IsWindow(*it))
            SendMessageW(*it, WM_CLOSE, 0, 0);
}

void paint_folder_contents(HDC dc, void* context);

bool disclosure_bounds(const FolderWindow& folder, int row, RECT& bounds)
{
    if (folder.search || folder.presentation.view != FolderView::list || row < 0 ||
        size_t(row) >= folder.items.size() || !folder.items[row].can_expand || folder.items[row].outline_depth >= 32)
        return false;
    RECT icon{}, column{};
    HWND header = ListView_GetHeader(folder.list);
    if (!ListView_GetItemRect(folder.list, row, &icon, LVIR_ICON) || !Header_GetItemRect(header, 0, &column))
        return false;
    MapWindowPoints(header, folder.list, reinterpret_cast<POINT*>(&column), 2);
    int top = (icon.top + icon.bottom - 16) / 2;
    bounds = {icon.left - 16, top, icon.left, top + 16};
    return bounds.left >= column.left && bounds.right <= column.right;
}

bool change_outline(FolderWindow& folder, int row, int action)
{
    RECT bounds{};
    if (!disclosure_bounds(folder, row, bounds))
        return false;
    auto path = folder.items[row].path;
    bool expanded = folder.expanded_folders.count(path);
    bool open = action ? action > 0 : !expanded;
    if (open == expanded)
        return false;
    if (open)
        folder.expanded_folders.insert(path);
    else
        folder.expanded_folders.erase(path);
    refresh(folder);
    for (int index = 0; index < int(folder.items.size()); ++index)
        if (folder.items[index].path == path) {
            RECT icon{};
            if (ListView_GetItemRect(folder.list, index, &icon, LVIR_ICON))
                ListView_Scroll(folder.list, 0, (icon.top + icon.bottom - 16) / 2 - bounds.top);
            ListView_SetItemState(folder.list, index, LVIS_FOCUSED, LVIS_FOCUSED);
            ListView_EnsureVisible(folder.list, index, FALSE);
            break;
        }
    return true;
}

// Adapt native list-view input to Finder activation and commands; capture
// press-event coordinates before queued mouse movement advances the cursor.
LRESULT CALLBACK list_proc(HWND w, UINT message_id, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)
{
    auto* f = reinterpret_cast<FolderWindow*>(data);
    FolderCallback callback(f);
    if (message_id == WM_ERASEBKGND)
        // Erasure belongs inside the same view update as icons and labels.
        return 1;
    if (message_id == WM_PAINT || message_id == WM_PRINTCLIENT) {
        if (classic::redraw_suspended(w))
            return 0;
        PAINTSTRUCT paint{};
        HDC dc = message_id == WM_PAINT ? BeginPaint(w, &paint) : reinterpret_cast<HDC>(wp);
        classic::paint_view(w, dc, paint_folder_contents, f);
        if (message_id == WM_PAINT)
            EndPaint(w, &paint);
        return 0;
    }
    if (message_id == WM_LBUTTONDOWN || message_id == WM_LBUTTONDBLCLK) {
        if (!drag_folder && f->presentation.view == FolderView::list) {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int first = std::max(0, ListView_GetTopIndex(w));
            int end = std::min(int(f->items.size()), first + ListView_GetCountPerPage(w) + 2);
            for (int row = first; row < end; ++row) {
                RECT bounds{};
                if (!disclosure_bounds(*f, row, bounds) || !PtInRect(&bounds, point))
                    continue;
                classic::activate(f->window);
                SetFocus(w);
                if (message_id == WM_LBUTTONDOWN)
                    SendMessageW(f->window, WM_APP + 4, row, 0);
                return 0;
            }
        }
    }
    if (message_id == WM_LBUTTONDOWN) {
        // Preserve the press event's position before activation or queued mouse
        // moves advance the global cursor used by GetCursorPos.
        drag_start = POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ClientToScreen(w, &drag_start);
        current_folder = f;
        classic::activate(f->window);
    }
    if (message_id == WM_DROPFILES)
        return SendMessageW(f->window, message_id, wp, lp);
    if (message_id == WM_CONTEXTMENU)
        return SendMessageW(f->window, message_id, wp, lp);
    if (message_id == WM_KEYDOWN) {
        if (drag_folder && wp == VK_SPACE) {
            PostMessageW(desktop, WM_APP + 3, 0, 0);
            return 0;
        }
        if (drag_folder && wp == VK_ESCAPE) {
            SendMessageW(drag_folder->window, WM_CANCELMODE, 0, 0);
            return 0;
        }
        unsigned c = 0;
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        if (!ctrl && GetKeyState(VK_MENU) >= 0 && (wp == VK_LEFT || wp == VK_RIGHT) &&
            SendMessageW(f->window, WM_APP + 4, ListView_GetNextItem(w, -1, LVNI_FOCUSED), wp == VK_RIGHT ? 1 : -1))
            return 0;
        if (ctrl)
            switch (wp) {
            case 'W':
                c = close_command;
                break;
            case 'O':
                c = open_command;
                break;
            case 'N':
                c = new_folder_command;
                break;
            case 'D':
                c = duplicate_command;
                break;
            case 'I':
                c = info_command;
                break;
            case 'Z':
                c = undo_command;
                break;
            case 'X':
                c = cut_command;
                break;
            case 'M':
                c = alias_command;
                break;
            case 'Y':
                c = put_away_command;
                break;
            case 'F':
                c = find_command;
                break;
            case 'G':
                c = find_again_command;
                break;
            case 'P':
                c = print_command;
                break;
            case 'E':
                c = eject_command;
                break;
            case 'C':
                c = copy_command;
                break;
            case 'V':
                c = paste_command;
                break;
            case 'A':
                c = select_all_command;
                break;
            case 'Q':
                c = quit_command;
                break;
            }
        else
            switch (wp) {
            case VK_RETURN:
                c = open_command;
                break;
            case VK_F2:
                c = rename_command;
                break;
            case VK_F5:
                c = refresh_command;
                break;
            case VK_DELETE:
                c = trash_command;
                break;
            case VK_BACK:
                c = parent_command;
                break;
            }
        if (c && !ListView_GetEditControl(w)) {
            SendMessageW(f->window, WM_COMMAND, c, 0);
            return 0;
        }
    }
    auto result = DefSubclassProc(w, message_id, wp, lp);
    if (message_id == LVM_SCROLL || message_id == WM_HSCROLL || message_id == WM_VSCROLL ||
        message_id == WM_MOUSEWHEEL || message_id == WM_MOUSEHWHEEL || message_id == WM_SIZE ||
        message_id == LVM_SETITEMPOSITION32 || message_id == LVM_SETITEMSTATE || message_id == WM_LBUTTONUP ||
        message_id == WM_KEYDOWN || message_id == WM_SETFOCUS || message_id == WM_KILLFOCUS)
        // Native scrolling/selection updates the model only. Reconstruct the
        // visible view, including text beyond Windows' short label rectangles.
        InvalidateRect(w, nullptr, FALSE);
    return result;
}

// Draw complete single-line labels after the icons, with white backgrounds
// for ordinary names and contrasting selection/drag-highlight backgrounds.
void paint_labels(FolderWindow& folder, HDC dc)
{
    if (folder.closing)
        return;
    int editing = ListView_GetEditControl(folder.list) ? ListView_GetNextItem(folder.list, -1, LVNI_FOCUSED) : -1;
    if (ListView_GetView(folder.list) != LV_VIEW_ICON) {
        int saved = SaveDC(dc);
        SelectObject(dc, classic::small_font());
        SetBkMode(dc, TRANSPARENT);
        HWND header = ListView_GetHeader(folder.list);
        int columns = ListView_GetView(folder.list) == LV_VIEW_DETAILS ? Header_GetItemCount(header) : 1;
        RECT client;
        GetClientRect(folder.list, &client);
        for (int index = 0; !folder.closing && index < int(folder.items.size()); ++index) {
            bool selected = ListView_GetItemState(folder.list, index, LVIS_SELECTED) != 0;
            auto background = label_background((folder.items[index].mac.flags >> 1) & 7, selected);
            auto brush = CreateSolidBrush(background);
            SetTextColor(dc, selected ? RGB(255, 255, 255) : RGB(0, 0, 0));
            for (int column = 0; !folder.closing && column < columns; ++column) {
                if (index == editing && column == 0)
                    continue;
                RECT bounds{}, visible{};
                bool located = column == 0 ? ListView_GetItemRect(folder.list, index, &bounds, LVIR_LABEL)
                                           : ListView_GetSubItemRect(folder.list, index, column, LVIR_LABEL, &bounds);
                if (!located)
                    continue;
                if (!IntersectRect(&visible, &bounds, &client))
                    continue;
                classic::fill_rect(dc, &bounds, brush);
                if (folder.closing)
                    break;
                wchar_t text[4096]{};
                ListView_GetItemText(folder.list, index, column, text, std::size(text));
                bounds.left += 2;
                bounds.right -= 2;
                classic::draw_text(dc, text, -1, &bounds, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            DeleteObject(brush);
        }
        RestoreDC(dc, saved);
        return;
    }

    struct Label {
        int index;
        RECT bounds;
        bool selected;
    };

    std::vector<Label> labels;
    int saved = SaveDC(dc);
    SelectObject(dc, classic::small_font());
    RECT client;
    GetClientRect(folder.list, &client);
    for (int i = 0; !folder.closing && i < int(folder.items.size()); ++i) {
        if (i == editing)
            continue;
        RECT bounds, visible;
        ListView_GetItemRect(folder.list, i, &bounds, LVIR_LABEL);
        bounds = classic::icon_label_bounds(dc, folder.items[i].name.c_str(), bounds, &client);
        if (folder.closing)
            break;
        if (!IntersectRect(&visible, &bounds, &client))
            continue;
        bool selected = ListView_GetItemState(folder.list, i, LVIS_SELECTED | LVIS_DROPHILITED) != 0;
        labels.push_back({i, bounds, selected});
    }
    // Highlighted labels sit above ordinary text if long names overlap.
    std::stable_sort(labels.begin(), labels.end(),
                     [](const Label& left, const Label& right) { return left.selected < right.selected; });
    // Paint every background before any lettering. An adjacent long label must
    // not lose its glyphs to a selection rectangle painted afterward.
    for (const auto& label : labels) {
        if (folder.closing)
            break;
        if (label.selected) {
            auto brush = CreateSolidBrush(label_background((folder.items[label.index].mac.flags >> 1) & 7, true));
            classic::fill_rect(dc, &label.bounds, brush);
            DeleteObject(brush);
        }
        else
            classic::fill_rect(dc, &label.bounds, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    }
    SetBkMode(dc, TRANSPARENT);
    for (const auto& label : labels) {
        if (folder.closing)
            break;
        RECT bounds = label.bounds;
        COLORREF color = RGB(0, 0, 0);
        if (label.selected) {
            auto background = label_background((folder.items[label.index].mac.flags >> 1) & 7, true);
            unsigned luminance = 5 * GetRValue(background) + 9 * GetGValue(background) + 2 * GetBValue(background);
            color = luminance >= 2048 ? RGB(0, 0, 0) : RGB(255, 255, 255);
        }
        SetTextColor(dc, color);
        classic::draw_text(dc, folder.items[label.index].name.c_str(), -1, &bounds,
                           DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    RestoreDC(dc, saved);
}

void paint_folder_contents(HDC dc, void* context)
{
    auto& folder = *static_cast<FolderWindow*>(context);
    bool large = ListView_GetView(folder.list) == LV_VIEW_ICON;
    std::vector<classic::IconViewImage> images;
    images.reserve(folder.items.size());
    for (const auto& item : folder.items)
        images.push_back({item.icon, item.selected_icon});
    classic::paint_icon_view(folder.list, dc, large ? folder.images : folder.small_images, images.data(), images.size(),
                             folder.desktop);
    paint_labels(folder, dc);
    if (folder.closing)
        return;
    if (folder.presentation.view == FolderView::list && !folder.search) {
        RECT client{};
        GetClientRect(folder.list, &client);
        int first = std::max(0, ListView_GetTopIndex(folder.list));
        int end = std::min(int(folder.items.size()), first + ListView_GetCountPerPage(folder.list) + 2);
        for (int row = first; row < end; ++row) {
            RECT glyph{}, clipped{};
            if (disclosure_bounds(folder, row, glyph) && IntersectRect(&clipped, &glyph, &client)) {
                auto art = disclosure_icons[folder.expanded_folders.count(folder.items[row].path) != 0];
                classic::draw_icon(dc, glyph.left, glyph.top, art, 16, 16, 0, nullptr, DI_NORMAL);
            }
        }
    }
}

// Own folder-window lifetime and route list notifications, commands, resizing
// and captured drag completion on the UI thread.
LRESULT CALLBACK folder_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* f = reinterpret_cast<FolderWindow*>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_NCCREATE)
        f = static_cast<FolderWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    FolderCallback callback(f);
    try {
        if (msg == WM_NCCREATE) {
            classic::decorate(w, f->desktop);
            f->window = w;
            SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(f));
        }
        if (!f)
            return DefWindowProcW(w, msg, wp, lp);
        switch (msg) {
        case WM_DRAWITEM: {
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (draw && draw->CtlType == ODT_STATIC && draw->CtlID == 1) {
                if (!classic::redraw_suspended(w)) {
                    wchar_t text[128]{};
                    GetWindowTextW(draw->hwndItem, text, std::size(text));
                    classic::draw_folder_status(draw->hDC, draw->rcItem, text,
                                                f->read_only ? volume_lock_icon : nullptr);
                }
                return TRUE;
            }
            break;
        }
        case WM_CREATE: {
            folders.push_back(f);
            if (!f->desktop)
                SetPropW(w, L"FinderWhite", reinterpret_cast<HANDLE>(1));
            if (f->desktop)
                classic::attach_menu(w, menu());
            f->status = control(w, L"STATIC", L"", SS_OWNERDRAW, 1, 8, 0, 100, 16);
            f->used = control(w, L"STATIC", L"", SS_CENTER, 4, 150, 3, 150, 12);
            f->available = control(w, L"STATIC", L"", SS_RIGHT, 5, 350, 3, 150, 12);
            SetPropW(f->status, L"FinderWhite", reinterpret_cast<HANDLE>(1));
            SetPropW(f->used, L"FinderWhite", reinterpret_cast<HANDLE>(1));
            SetPropW(f->available, L"FinderWhite", reinterpret_cast<HANDLE>(1));
            f->list = control(w, WC_LISTVIEWW, L"",
                              LVS_ICON | (f->desktop ? LVS_NOSCROLL : 0) | LVS_NOLABELWRAP | LVS_EDITLABELS |
                                  LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS | WS_TABSTOP,
                              2, 0, 18, 500, 350);
            RECT client;
            GetClientRect(w, &client);
            if (f->desktop) {
                ShowWindow(f->status, SW_HIDE);
                ShowWindow(f->used, SW_HIDE);
                ShowWindow(f->available, SW_HIDE);
            }
            int inset = f->desktop ? 0 : 18;
            MoveWindow(f->list, 0, inset, client.right, std::max(0L, client.bottom - inset), FALSE);
            ListView_SetExtendedListViewStyle(f->list, LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
            ListView_SetIconSpacing(f->list, icon_cell_width, icon_cell_height);
            f->images = ImageList_Create(32, 32, ILC_COLOR32 | ILC_MASK, 32, 16);
            f->small_images = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 32, 16);
            if (!f->images || !f->small_images)
                throw std::runtime_error("Cannot allocate folder icons");
            ListView_SetImageList(f->list, f->images, LVSIL_NORMAL);
            ListView_SetImageList(f->list, f->small_images, LVSIL_SMALL);
            if (f->desktop) {
                classic::style_desktop(f->list);
                ListView_SetTextBkColor(f->list, RGB(255, 255, 255));
            }
            else {
                classic::style_list(f->list);
                ListView_SetBkColor(f->list, RGB(255, 255, 255));
                ListView_SetTextBkColor(f->list, RGB(255, 255, 255));
            }
            if (!f->desktop) {
                if (f->search)
                    f->presentation.view = FolderView::list;
                else {
                    auto saved = settings_path_value(preferences,
                                                     f->image_volume ? L"ImageViewsNative" : L"ViewsNative", f->path);
                    if (!f->image_volume || !saved.empty())
                        f->presentation = decode_folder_view(saved);
                }
                configure_folder_view(*f);
            }
            SetWindowSubclass(f->list, list_proc, 1, reinterpret_cast<DWORD_PTR>(f));
            DragAcceptFiles(w, TRUE);
            DragAcceptFiles(f->list, TRUE);
            refresh(*f);
            SetFocus(f->list);
            return 0;
        }
        case WM_EXITSIZEMOVE:
            if (f->desktop)
                save_host_window(w, preferences);
            else if (f->image_volume)
                save_window_geometry(*f);
            break;
        case WM_SIZE:
            MoveWindow(f->status, 8, 0, std::max(0, int(LOWORD(lp)) / 3 - 8), 16, TRUE);
            MoveWindow(f->used, LOWORD(lp) / 3, 3, LOWORD(lp) / 3, 12, TRUE);
            MoveWindow(f->available, 2 * LOWORD(lp) / 3, 3, std::max(0, int(LOWORD(lp)) / 3 - 8), 12, TRUE);
            MoveWindow(f->list, 0, f->desktop ? 0 : 18, LOWORD(lp),
                       std::max(0, int(HIWORD(lp)) - (f->desktop ? 0 : 18)), TRUE);
            if (!f->desktop) {
                RECT header{0, 0, LOWORD(lp), 18};
                RedrawWindow(w, &header, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            }
            return 0;
        case WM_SETFOCUS:
            if (f->closing)
                return 0;
            if (f->desktop && live_folder(current_folder) && current_folder != f) {
                SetFocus(current_folder->list);
                return 0;
            }
            current_folder = f;
            classic::activate(w);
            SetFocus(f->list);
            return 0;
        case WM_INITMENU:
            if (f->desktop) {
                publish_completed_operations();
                auto* target = command_folder(f);
                auto paths = selected(*target);
                HMENU bar = classic::window_menu(w);
                CheckMenuItem(bar, show_hidden_command, MF_BYCOMMAND | (show_hidden ? MF_CHECKED : MF_UNCHECKED));
                CheckMenuItem(bar, spring_folders_command, MF_BYCOMMAND | (spring_folders ? MF_CHECKED : MF_UNCHECKED));
                CheckMenuItem(bar, icons_command,
                              MF_BYCOMMAND |
                                  (ListView_GetView(target->list) == LV_VIEW_ICON ? MF_CHECKED : MF_UNCHECKED));
                CheckMenuItem(bar, list_command,
                              MF_BYCOMMAND |
                                  (target->presentation.view == FolderView::list ? MF_CHECKED : MF_UNCHECKED));
                CheckMenuItem(bar, small_icons_command,
                              MF_BYCOMMAND |
                                  (target->presentation.view == FolderView::small_icons ? MF_CHECKED : MF_UNCHECKED));
                for (unsigned column = 0; column < folder_column_count; ++column) {
                    CheckMenuItem(bar, sort_command + column,
                                  MF_BYCOMMAND | (target->presentation.view == FolderView::list &&
                                                          int(target->presentation.sort) == int(column)
                                                      ? MF_CHECKED
                                                      : MF_UNCHECKED));
                    EnableMenuItem(bar, sort_command + column,
                                   MF_BYCOMMAND | (target->desktop ? MF_GRAYED : MF_ENABLED));
                }
                for (UINT id : {small_icons_command, list_command})
                    EnableMenuItem(bar, id, MF_BYCOMMAND | (target->desktop ? MF_GRAYED : MF_ENABLED));
                EnableMenuItem(bar, arrange_command,
                               MF_BYCOMMAND |
                                   (target->presentation.view == FolderView::icons ? MF_ENABLED : MF_GRAYED));
                EnableMenuItem(bar, arrange_name_command,
                               MF_BYCOMMAND |
                                   (target->presentation.view == FolderView::icons ? MF_ENABLED : MF_GRAYED));
                unsigned label = 0;
                bool have = false, mixed = false;
                for (int i = -1; (i = selected_item_after(target->list, target->items.size(), i)) >= 0;) {
                    unsigned value = (target->items[i].mac.flags >> 1) & 7;
                    if (have && value != label)
                        mixed = true;
                    label = value;
                    have = true;
                }
                for (unsigned i = 0; i < 8; ++i)
                    CheckMenuItem(bar, label_command + i,
                                  MF_BYCOMMAND | (!mixed && i == label ? MF_CHECKED : MF_UNCHECKED));

                for (unsigned id : {open_command, info_command, copy_command, cut_command, clear_command, alias_command,
                                    duplicate_command, rename_command, trash_command})
                    EnableMenuItem(bar, id, MF_BYCOMMAND | (paths.empty() ? MF_GRAYED : MF_ENABLED));
                bool any = ListView_GetSelectedCount(target->list) != 0;
                bool shift = GetKeyState(VK_SHIFT) < 0;
                bool option = !target->desktop && GetKeyState(VK_MENU) < 0;
                special_cleanup_action = shift    ? arrange_selection_command
                                         : option ? arrange_name_command
                                                  : arrange_command;
                HMENU special = GetSubMenu(bar, 5);
                classic::set_menu_caption(special, special_cleanup_command,
                                          shift             ? L"Clean Up Selection"
                                          : option          ? L"Clean Up by Name"
                                          : target->desktop ? L"Clean Up Desktop"
                                                            : L"Clean Up Window");
                EnableMenuItem(
                    special, special_cleanup_command,
                    MF_BYCOMMAND |
                        (target->presentation.view == FolderView::icons && (!shift || any) ? MF_ENABLED : MF_GRAYED));
                EnableMenuItem(bar, arrange_selection_command,
                               MF_BYCOMMAND |
                                   (any && target->presentation.view == FolderView::icons ? MF_ENABLED : MF_GRAYED));
                bool image_selected = false, image_root = false;
                for (int index = -1; (index = selected_item_after(target->list, target->items.size(), index)) >= 0;) {
                    image_selected |= target->items[index].image_volume != 0;
                    image_root |= target->items[index].special == 4;
                }
                EnableMenuItem(bar, open_command, MF_BYCOMMAND | (any ? MF_ENABLED : MF_GRAYED));
                EnableMenuItem(bar, windows_shortcut_command,
                               MF_BYCOMMAND | (selected_application(*target, true) ? MF_ENABLED : MF_GRAYED));
                EnableMenuItem(bar, configure_app_command,
                               MF_BYCOMMAND | (selected_application(*target) ? MF_ENABLED : MF_GRAYED));
                bool archives = any;
                for (int index = -1; (index = selected_item_after(target->list, target->items.size(), index)) >= 0;)
                    archives &= archive_kind(target->items[index]) != 0;
                EnableMenuItem(bar, extract_to_command, MF_BYCOMMAND | (archives ? MF_ENABLED : MF_GRAYED));
                EnableMenuItem(bar, extract_here_command,
                               MF_BYCOMMAND | (archives && archive_destination_allowed(
                                                               {target->desktop ? desktop_folder : target->path,
                                                                target->image_volume, target->image_directory})
                                                   ? MF_ENABLED
                                                   : MF_GRAYED));
                EnableMenuItem(bar, info_command, MF_BYCOMMAND | (any ? MF_ENABLED : MF_GRAYED));
                bool ordinary = !paths.empty();
                for (const auto& path : paths)
                    ordinary &= path != path.root_path();
                for (unsigned id :
                     {cut_command, clear_command, trash_command, rename_command, duplicate_command, alias_command})
                    EnableMenuItem(bar, id, MF_BYCOMMAND | (ordinary ? MF_ENABLED : MF_GRAYED));
                EnableMenuItem(bar, close_command,
                               MF_BYCOMMAND | (classic::active_window() != desktop ? MF_ENABLED : MF_GRAYED));
                EnableMenuItem(bar, 4, MF_BYPOSITION | (paths.empty() ? MF_GRAYED : MF_ENABLED));
                EnableMenuItem(bar, undo_command, MF_BYCOMMAND | (history.available() ? MF_ENABLED : MF_GRAYED));
                EnableMenuItem(bar, find_again_command,
                               MF_BYCOMMAND | (last_search_roots.empty() ? MF_GRAYED : MF_ENABLED));
                bool removable = image_root || (paths.size() == 1 && can_eject(paths[0]));
                EnableMenuItem(bar, eject_command, MF_BYCOMMAND | (removable ? MF_ENABLED : MF_GRAYED));
                EnableMenuItem(bar, erase_command,
                               MF_BYCOMMAND | (paths.size() == 1 && can_erase(paths[0]) ? MF_ENABLED : MF_GRAYED));
                bool put_away = !paths.empty();
                for (const auto& path : paths)
                    put_away &= !settings_path_value(preferences, L"PutAway", path).empty();
                put_away |= image_root;
                EnableMenuItem(bar, put_away_command, MF_BYCOMMAND | (put_away ? MF_ENABLED : MF_GRAYED));
                bool printable = !paths.empty();
                for (const auto& path : paths)
                    printable &= fs::is_regular_file(path) && mac_file(path).kind == 0;
                EnableMenuItem(bar, print_command, MF_BYCOMMAND | (printable ? MF_ENABLED : MF_GRAYED));
                EnableMenuItem(bar, paste_command,
                               MF_BYCOMMAND | (!host_clipboard_has_files() || target->search || target->image_volume
                                                   ? MF_GRAYED
                                                   : MF_ENABLED));
                for (unsigned id : {new_folder_command, parent_command})
                    EnableMenuItem(bar, id,
                                   MF_BYCOMMAND | (target->image_volume || target->search ? MF_GRAYED : MF_ENABLED));
                if (image_selected) {
                    EnableMenuItem(bar, 4, MF_BYPOSITION | MF_GRAYED);
                    EnableMenuItem(bar, trash_command, MF_BYCOMMAND | (image_root ? MF_ENABLED : MF_GRAYED));
                }
                if (target->image_volume) {
                    bool writable = !target->read_only;
                    for (unsigned command : {clear_command, trash_command, duplicate_command})
                        EnableMenuItem(bar, command, MF_BYCOMMAND | (writable && any ? MF_ENABLED : MF_GRAYED));
                    EnableMenuItem(
                        bar, rename_command,
                        MF_BYCOMMAND |
                            (writable && ListView_GetSelectedCount(target->list) == 1 ? MF_ENABLED : MF_GRAYED));
                    EnableMenuItem(bar, 4, MF_BYPOSITION | (writable && any ? MF_ENABLED : MF_GRAYED));
                    EnableMenuItem(bar, new_folder_command, MF_BYCOMMAND | (writable ? MF_ENABLED : MF_GRAYED));
                    EnableMenuItem(bar, parent_command,
                                   MF_BYCOMMAND | (target->image_directory != 2 ? MF_ENABLED : MF_GRAYED));
                    EnableMenuItem(bar, paste_command,
                                   MF_BYCOMMAND | (writable && host_clipboard_has_files() ? MF_ENABLED : MF_GRAYED));
                }
                if (auto edit = active_edit()) {
                    DWORD start = 0, end = 0;
                    SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
                    EnableMenuItem(bar, undo_command,
                                   MF_BYCOMMAND | (SendMessageW(edit, EM_CANUNDO, 0, 0) ? MF_ENABLED : MF_GRAYED));
                    bool writable = !(GetWindowLongPtrW(edit, GWL_STYLE) & ES_READONLY);
                    for (unsigned command : {cut_command, copy_command, clear_command})
                        EnableMenuItem(
                            bar, command,
                            MF_BYCOMMAND |
                                (start != end && (writable || command == copy_command) ? MF_ENABLED : MF_GRAYED));
                    EnableMenuItem(bar, paste_command,
                                   MF_BYCOMMAND | (IsClipboardFormatAvailable(CF_UNICODETEXT) &&
                                                           !(GetWindowLongPtrW(edit, GWL_STYLE) & ES_READONLY)
                                                       ? MF_ENABLED
                                                       : MF_GRAYED));
                }
                if (auto* info = active_info_icon()) {
                    EnableMenuItem(bar, copy_command, MF_BYCOMMAND | MF_ENABLED);
                    for (unsigned item : {cut_command, clear_command})
                        EnableMenuItem(bar, item,
                                       MF_BYCOMMAND |
                                           (info->icon_editable && info->custom_icon ? MF_ENABLED : MF_GRAYED));
                    EnableMenuItem(bar, paste_command,
                                   MF_BYCOMMAND |
                                       (info->icon_editable && host_clipboard_has_icon() ? MF_ENABLED : MF_GRAYED));
                }
                if (auto* suitcase = active_suitcase()) {
                    for (const auto& [command, mode] : folder_view_commands) {
                        EnableMenuItem(bar, command, MF_BYCOMMAND | MF_ENABLED);
                        CheckMenuItem(bar, command,
                                      MF_BYCOMMAND | (suitcase->mode == mode ? MF_CHECKED : MF_UNCHECKED));
                    }
                    for (unsigned id : unavailable_suitcase_commands)
                        EnableMenuItem(bar, id, MF_BYCOMMAND | MF_GRAYED);
                    if (selected_preview_member(*suitcase) >= 0)
                        EnableMenuItem(bar, open_command, MF_BYCOMMAND | MF_ENABLED);
                    EnableMenuItem(bar, 4, MF_BYPOSITION | MF_GRAYED);
                    for (unsigned i = 0; i < folder_column_count; ++i) {
                        EnableMenuItem(bar, sort_command + i, MF_BYCOMMAND | MF_GRAYED);
                        CheckMenuItem(bar, sort_command + i, MF_BYCOMMAND | MF_UNCHECKED);
                    }
                }
                bool preview = is_font_preview_window(classic::active_window());
                if (preview) {
                    for (unsigned id : unavailable_suitcase_commands)
                        EnableMenuItem(bar, id, MF_BYCOMMAND | MF_GRAYED);
                    for (const auto& [command, mode] : folder_view_commands)
                        EnableMenuItem(bar, command, MF_BYCOMMAND | MF_GRAYED);
                    EnableMenuItem(bar, select_all_command, MF_BYCOMMAND | MF_GRAYED);
                    EnableMenuItem(bar, 4, MF_BYPOSITION | MF_GRAYED);
                }
                EnableMenuItem(bar, print_window_command,
                               MF_BYCOMMAND | (preview || active_suitcase() ? MF_GRAYED : MF_ENABLED));
            }
            return 0;
        case WM_PAINT:
            if (!f->desktop) {
                PAINTSTRUCT paint;
                auto dc = BeginPaint(w, &paint);
                RECT r;
                GetClientRect(w, &r);
                r.bottom = 18;
                classic::fill_rect(dc, &r, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
                classic::draw_line(dc, 0, 16, r.right - 1, 16, RGB(0, 0, 0));
                EndPaint(w, &paint);
                return 0;
            }
            break;
        case WM_COMMAND:
            if (f->desktop) {
                if (auto* suitcase = active_suitcase(); suitcase && command_suitcase(*suitcase, LOWORD(wp)))
                    return 0;
                if (is_font_preview_window(classic::active_window()) && preview_blocks_command(LOWORD(wp)))
                    return 0;
            }
            if (LOWORD(wp) == label_popup_command) {
                // Leave the native menu loop before starting the custom popup.
                EndMenu();
                PostMessageW(w, WM_APP + 1, 0, 0);
                return 0;
            }
            command(f->desktop ? *command_folder(f) : *f,
                    LOWORD(wp) == special_cleanup_command ? special_cleanup_action : LOWORD(wp));
            return 0;
        case WM_CONTEXTMENU:
            folder_context_menu(*f, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;
        case WM_APP + 1:
            if (f->desktop && (active_suitcase() || is_font_preview_window(classic::active_window())))
                return 0;
            command(f->desktop ? *command_folder(f) : *f, label_popup_command);
            return 0;
        case WM_APP + 3:
            spring_open_folder(true);
            return 0;
        case WM_APP + 4:
            return change_outline(*f, int(wp), int(lp));
        case WM_APP + 5:
            if (f->desktop && !folder_input_active()) {
                auto open = folders;
                for (auto* folder : open) {
                    if (wp)
                        refresh_catalog_window(*folder);
                    else
                        refresh(*folder);
                }
            }
            return 0;
        case WM_NOTIFY: {
            auto* notice = reinterpret_cast<NMHDR*>(lp);
            if (notice->code == NM_RCLICK && notice->hwndFrom == f->list) {
                POINT point = reinterpret_cast<NMITEMACTIVATE*>(lp)->ptAction;
                ClientToScreen(f->list, &point);
                PostMessageW(w, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(f->list), MAKELPARAM(point.x, point.y));
                return TRUE; // This window owns the contextual menu.
            }
            if (notice->code == LVN_COLUMNCLICK && notice->hwndFrom == f->list && !f->desktop) {
                int column = reinterpret_cast<NMLISTVIEW*>(lp)->iSubItem;
                if (column >= 0 && column < int(folder_column_count)) {
                    bool same = int(f->presentation.sort) == column;
                    f->presentation.sort = FolderSort(column);
                    f->presentation.descending = same ? !f->presentation.descending : column == 1 || column == 2;
                    configure_folder_view(*f);
                    refresh(*f);
                    save_folder_view(*f);
                }
                return 0;
            }
            if (notice->code == NM_DBLCLK) {
                open_selected(*f);
                return 0;
            }
            if (notice->code == LVN_BEGINLABELEDITW && notice->hwndFrom == f->list) {
                auto* edit = reinterpret_cast<NMLVDISPINFOW*>(lp);
                if (edit->item.iItem < 0 || size_t(edit->item.iItem) >= f->items.size() ||
                    f->items[edit->item.iItem].special || (f->image_volume && f->read_only))
                    return TRUE;
                HWND field = ListView_GetEditControl(f->list);
                SendMessageW(field, WM_SETFONT, reinterpret_cast<WPARAM>(classic::small_font()), FALSE);
                classic::style_text_control(field);
                InvalidateRect(f->list, nullptr, FALSE);
                return FALSE;
            }
            if (notice->code == LVN_ENDLABELEDITW) {
                InvalidateRect(f->list, nullptr, FALSE);
                auto* edit = reinterpret_cast<NMLVDISPINFOW*>(lp);
                if (!edit->item.pszText || edit->item.iItem < 0)
                    return FALSE;
                auto& item = f->items.at(edit->item.iItem);
                if (item.image_volume && !item.special) {
                    if (f->read_only)
                        return FALSE;
                    auto name = rename_image_item(
                        item.image_volume, {item.image_parent, item.image_id, item.image_name}, edit->item.pszText);
                    item.image_name = name;
                    item.name = image_text(name.c_str());
                    for (auto* child : folders)
                        if (child->image_volume == item.image_volume && child->image_directory == item.image_id) {
                            child->image_title = item.name;
                            SetWindowTextW(child->window, item.name.c_str());
                        }
                    PostMessageW(w, WM_COMMAND, refresh_command, 0);
                    return TRUE;
                }
                if (item.special || item.image_volume)
                    return FALSE;
                auto target = host_rename_target(item.path, edit->item.pszText);
                bool alias = _wcsicmp(item.path.extension().c_str(), L".lnk") == 0;
                classic::WaitingCursor waiting;
                history.move({{item.path, target}}, L"Rename");
                item.path = target;
                item.name = alias ? target.stem().wstring() : target.filename().wstring();
                if (f->presentation.view != FolderView::icons)
                    PostMessageW(w, WM_COMMAND, refresh_command, 0);
                return TRUE;
            }
            if (notice->code == LVN_ITEMCHANGED && ListView_GetView(f->list) == LV_VIEW_ICON) {
                auto* changed = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((changed->uChanged & LVIF_STATE) &&
                    ((changed->uOldState ^ changed->uNewState) & (LVIS_SELECTED | LVIS_DROPHILITED)) &&
                    changed->iItem >= 0 && size_t(changed->iItem) < f->items.size()) {
                    RECT bounds{};
                    if (ListView_GetItemRect(f->list, changed->iItem, &bounds, LVIR_LABEL)) {
                        HDC dc = GetDC(f->list);
                        if (dc) {
                            auto previous = SelectObject(dc, classic::small_font());
                            RECT client;
                            GetClientRect(f->list, &client);
                            auto text =
                                classic::icon_label_bounds(dc, f->items[changed->iItem].name.c_str(), bounds, &client);
                            SelectObject(dc, previous);
                            ReleaseDC(f->list, dc);
                            UnionRect(&bounds, &bounds, &text);
                            // Include the complete QuickDraw width and descenders
                            // when highlighting changes, so old fills and glyphs clear.
                            InvalidateRect(f->list, &bounds, FALSE);
                        }
                    }
                }
            }
            if (notice->code == NM_CUSTOMDRAW)
                return CDRF_SKIPDEFAULT; // The view owns its complete update transaction.
            if (notice->code == LVN_BEGINDRAG) {
                drag_folder = f;
                drag_indices.clear();
                for (int i = -1; (i = selected_item_after(f->list, f->items.size(), i)) >= 0;)
                    drag_indices.push_back(i);
                SetCapture(w);
                auto* drag = reinterpret_cast<NMLISTVIEW*>(lp);
                POINT position = drag->ptAction;
                ClientToScreen(f->list, &position);
                drag_highlight.clear();
                if (drag->iItem >= 0 && size_t(drag->iItem) < f->items.size()) {
                    const auto& item = f->items[drag->iItem];
                    bool large = ListView_GetView(f->list) == LV_VIEW_ICON;
                    POINT origin{};
                    auto bitmap = classic::drag_artwork(f->list, large ? f->images : f->small_images,
                                                        item.selected_icon, drag->iItem, item.name.c_str(), origin);
                    if (bitmap)
                        drag_image.begin(f->list, drag->iItem, drag_start, position, nullptr, bitmap, origin);
                }
                update_drag_highlight(position);
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            }
            break;
        }
        case WM_MOUSEMOVE:
            if (drag_folder) {
                POINT position{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ClientToScreen(w, &position);
                drag_image.move(position);
                update_drag_highlight(position);
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (drag_folder) {
                auto* source = drag_folder;
                drag_folder = nullptr;
                spring_target = {};

                struct CloseSpringWindows {
                    std::vector<HWND> opened;

                    ~CloseSpringWindows()
                    {
                        close_spring_windows(opened);
                    }
                } spring{std::exchange(spring_windows, {})};

                drag_image.end();
                drag_highlight.clear();
                ReleaseCapture();
                // Capture delivers this event in the source frame's coordinates.
                // The live cursor may already have moved beyond the drop point.
                POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ClientToScreen(w, &point);
                if (auto suitcase = suitcase_drop_at(point).suitcase) {
                    std::vector<Item> items;
                    for (int index : drag_indices)
                        items.push_back(source->items.at(index));
                    drop_files_in_suitcase(w, *suitcase, items);
                    return 0;
                }
                std::vector<HWND> folder_windows;
                for (const auto* folder : folders)
                    folder_windows.push_back(folder->window);
                HWND target = drag_destination(WindowFromPoint(point), folder_windows);
                for (auto* other : folders)
                    if (target == other->window) {
                        LVHITTESTINFO hit{};
                        hit.pt = point;
                        ScreenToClient(other->list, &hit.pt);
                        int hit_index = ListView_HitTest(other->list, &hit);
                        bool on_target = accepts_drag_item(*source, *other, hit_index);
                        bool volume = std::any_of(drag_indices.begin(), drag_indices.end(), [&](int index) {
                            return source->items[index].special != 0 && source->items[index].special != 3;
                        });
                        bool image_root = !drag_indices.empty() &&
                                          std::all_of(drag_indices.begin(), drag_indices.end(),
                                                      [&](int index) { return source->items[index].special == 4; });
                        if (image_root && hit_index >= 0 && other->items[hit_index].special == 3) {
                            command(*source, put_away_command);
                            return 0;
                        }
                        bool copy_modifier = GetKeyState(VK_MENU) < 0 || GetKeyState(VK_CONTROL) < 0;
                        if (volume && (source != other || on_target || copy_modifier))
                            throw std::runtime_error("Volumes cannot be transferred as folders");
                        if (source->image_volume && (source != other || on_target || copy_modifier)) {
                            drop_image(*source, *other, point);
                        }
                        else if (source == other && !on_target && copy_modifier) {
                            copy_to(other->window, selected(*source), other->desktop ? desktop_folder : other->path,
                                    TransferKind::duplicate, 0, drop_placement(*other, point));
                        }
                        else if (source == other && !on_target) {
                            for (int i : drag_indices) {
                                POINT p;
                                ListView_GetItemPosition(source->list, i, &p);
                                LONG x = std::max(0L, p.x + point.x - drag_start.x);
                                LONG y = std::max(0L, p.y + point.y - drag_start.y);
                                ListView_SetItemPosition32(source->list, i, x, y);
                                if (x != p.x || y != p.y)
                                    save_desktop_catalog_position(*source, i, x, y);
                            }
                            save_positions(*source);
                        }
                        else
                            drop(*other, point, selected(*source));
                        break;
                    }
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            drag_image.end();
            drag_highlight.clear();
            drag_folder = nullptr;
            spring_target = {};
            close_spring_windows(std::exchange(spring_windows, {}));
            break;
        case WM_CANCELMODE:
            drag_highlight.clear();
            if (drag_folder) {
                drag_folder = nullptr;
                drag_image.end();
                ReleaseCapture();
            }
            break;
        case WM_DROPFILES: {
            auto h = reinterpret_cast<HDROP>(wp);
            std::vector<fs::path> paths;
            UINT n = DragQueryFileW(h, 0xffffffff, nullptr, 0);
            for (UINT i = 0; i < n; ++i) {
                UINT len = DragQueryFileW(h, i, nullptr, 0);
                std::wstring p(len + 1, 0);
                DragQueryFileW(h, i, p.data(), len + 1);
                p.resize(len);
                paths.emplace_back(p);
            }
            POINT pt;
            GetCursorPos(&pt);
            DragFinish(h);
            drop(*f, pt, paths);
            return 0;
        }
        case WM_TIMER:
            if (f->desktop && !folder_input_active()) {
                bool changed = false;
                for (const auto& [path, metadata] : drive_metadata)
                    if (metadata->ready.load() && published_drives.insert(path).second)
                        changed = true;
                if (changed)
                    refresh(*f);
            }
            if (f->desktop)
                spring_open_folder(false);
            if (f->desktop)
                copies.tick();
            if (f->desktop && !folder_input_active()) {
                // Compare the actual File Manager volumes, including restored
                // mounts and Toolbox ejects. This stores identities, never file data.
                std::vector<std::pair<fs::path, std::wstring>> actual, displayed;
                auto mounted = image_volumes();
                for (auto* folder : folders)
                    if (folder->image_volume)
                        for (const auto& volume : mounted)
                            if (volume.reference == folder->image_volume &&
                                folder->read_only != (volume.read_only != 0)) {
                                folder->read_only = volume.read_only != 0;
                                InvalidateRect(folder->status, nullptr, TRUE);
                                break;
                            }
                for (const auto& volume : mounted)
                    actual.emplace_back(image_volume_key(volume),
                                        image_text(volume.name) + L"#" + std::to_wstring(volume.reference) + L"#" +
                                            icon_source_stamp(image_text(volume.source, CP_ACP), false));
                for (const auto& item : f->items)
                    if (item.special == 4)
                        displayed.emplace_back(item.path, item.name + L"#" + std::to_wstring(item.image_volume) + L"#" +
                                                              item.image_stamp);
                std::sort(actual.begin(), actual.end());
                std::sort(displayed.begin(), displayed.end());
                if (actual != displayed) {
                    auto windows = folders;
                    for (auto* folder : windows)
                        if (folder->image_volume &&
                            std::none_of(mounted.begin(), mounted.end(),
                                         [&](const auto& volume) { return volume.reference == folder->image_volume; }))
                            SendMessageW(folder->window, WM_CLOSE, 0, 0);
                    for (auto* folder : folders)
                        if (folder->search)
                            refresh(*folder);
                    refresh(*f);
                }
                static unsigned trash_ticks = 0;
                if (++trash_ticks % 5 == 0) {
                    auto trash = std::find_if(f->items.begin(), f->items.end(),
                                              [](const Item& item) { return item.special == 3; });
                    if (trash != f->items.end() && trash->trash_full != recycle_bin_full())
                        refresh(*f);
                }
            }
            if (f->desktop) {
                // Nested alert loops still dispatch timers. Present one decision
                // at a time while other workers continue without holding UI locks.
                static bool deciding = false;
                if (!deciding)
                    for (const auto& operation : operations) {
                        auto job = operation->job;
                        std::wstring target;
                        bool directory = false;
                        {
                            std::lock_guard<std::mutex> lock(job->mutex);
                            if (job->conflict_pending) {
                                target = job->conflict_name;
                                directory = job->conflict_directory;
                            }
                        }
                        if (target.empty())
                            continue;
                        auto prompt = L"An item named \x201c" + target +
                                      L"\x201d already exists in this location. Do you want to replace it?";
                        if (directory)
                            prompt += L" The existing folder and all its contents will be replaced.";
                        deciding = true;

                        struct DecisionScope {
                            bool& active;

                            ~DecisionScope()
                            {
                                active = false;
                            }
                        } decision_scope{deciding};

                        int answer = classic::conflict_box(desktop, prompt.c_str());
                        job->answer_conflict(answer == IDYES  ? ConflictAnswer::replace
                                             : answer == IDNO ? ConflictAnswer::skip
                                                              : ConflictAnswer::stop);
                        break;
                    }
            }
            if (f->desktop && desktop_icons) {
                static unsigned icon_ticks = 0;
                if (++icon_ticks % 5 == 0)
                    desktop_icons->poll();
                GUITHREADINFO gui{};
                gui.cbSize = sizeof gui;
                GetGUIThreadInfo(GetCurrentThreadId(), &gui);
                auto generation = desktop_icons->generation();
                if (generation != icon_generation && !folder_input_active() &&
                    !(gui.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE))) {
                    icon_generation = generation;
                    classic::RedrawBatch redraw(desktop);
                    for (auto* folder : folders)
                        refresh(*folder);
                    for (unsigned i = 0; i < apple_items.size(); ++i) {
                        auto& item = apple_items[i];
                        if (icon_cache.count(item.path.wstring() + L"#apple-SICN"))
                            continue;
                        try {
                            item.mac = mac_file(item.path);
                            classic::menu_icon(1000 + i, item_icon(item));
                            Bytes().swap(item.mac.fork);
                        }
                        catch (...) {
                        }
                    }
                }
            }
            if (f->desktop && !folder_input_active())
                for (auto* folder : folders)
                    if (folder->search) {
                        folder->search->advance_images();
                        if (folder->search_count != folder->search->result_count() ||
                            folder->search_finished != folder->search->finished())
                            refresh(*folder);
                    }
            if (f->desktop) {
                for (auto it = children.begin(); it != children.end();)
                    if (WaitForSingleObject(*it, 0) == WAIT_OBJECT_0) {
                        CloseHandle(*it);
                        it = children.erase(it);
                    }
                    else
                        ++it;
            }
            if (f->desktop)
                publish_completed_operations();
            return 0;
        case WM_CLOSE:
            if (f->desktop)
                for (HWND child = GetWindow(w, GW_CHILD); child;) {
                    HWND next = GetWindow(child, GW_HWNDNEXT);
                    wchar_t type[32]{};
                    GetClassNameW(child, type, 32);
                    if (wcscmp(type, L"FinderInfo") == 0) {
                        SendMessageW(child, WM_CLOSE, 0, 0);
                        if (IsWindow(child))
                            return 0;
                    }
                    child = next;
                }
            if (f->desktop && copies.active() &&
                classic::message_box(w, L"Stop active file operations and quit Finder?", MB_YESNO | MB_ICONQUESTION) !=
                    IDYES)
                return 0;
            if (f->desktop) {
                save_host_window(w, preferences);
                copies.cancel_all();
                unsigned n = 0;
                for (auto* folder : folders)
                    if (!folder->desktop && !folder->search)
                        save(L"Session", L"Window" + std::to_wstring(++n), folder->path.wstring());
                save(L"Session", L"Count", std::to_wstring(n));
                PostQuitMessage(0);
            }
            else
                DestroyWindow(w);
            return 0;
        case WM_DESTROY:
            f->closing = true;
            drag_highlight.clear(f->list);
            save_positions(*f);
            save_folder_view(*f);
            save_window_geometry(*f);
            if (current_folder == f)
                current_folder = nullptr;
            folders.erase(std::remove(folders.begin(), folders.end(), f), folders.end());
            return 0;
        case WM_NCDESTROY:
            if (current_folder == f)
                current_folder = nullptr;
            if (f->images)
                ImageList_Destroy(f->images);
            if (f->small_images)
                ImageList_Destroy(f->small_images);
            SetWindowLongPtrW(w, GWLP_USERDATA, 0);
            if (f->window_owned)
                f->delete_pending = true;
            break;
        }
    }
    catch (const std::exception& e) {
        failure(w, e);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

// Leave room for the longer extraction labels and one additional detail row.
constexpr int extraction_extra_width = 32, extraction_extra_height = 22;

// Poll atomic transfer progress and locked text fields; Stop sets only this job's
// cancellation flag, leaving other operations usable.
LRESULT CALLBACK operation_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE)
        classic::decorate(w);
    auto* op = reinterpret_cast<Operation*>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_CREATE) {
        op = static_cast<Operation*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(op));
        op->window = w;
        auto view = [&](const wchar_t* type, const wchar_t* text, DWORD style, int id, FinderCopyBounds r) {
            return control(w, type, text, style, id, r.x, r.y, r.width, r.height);
        };
        bool extracting = op->extraction.has_value();
        bool moving = moving_transfer(op->job->kind);
        auto heading_rect = copy_rect_8805, count_rect = copy_rect_8800, stop_rect = copy_rect_3063;
        if (extracting) {
            heading_rect.width += extraction_extra_width;
            count_rect.x += extraction_extra_width;
            stop_rect.x += extraction_extra_width;
        }
        auto heading = view(L"STATIC", extracting ? L"Items remaining to be extracted:"
                                       : moving   ? L"Items remaining to be moved:"
                                                  : L"Items remaining to be copied:",
                            SS_LEFT, 1, heading_rect);
        SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(classic::system_font()), TRUE);
        auto count = view(L"STATIC", L"", SS_RIGHT, 9, count_rect);
        SendMessageW(count, WM_SETFONT, reinterpret_cast<WPARAM>(classic::system_font()), TRUE);
        op->bar = view(PROGRESS_CLASSW, L"", PBS_SMOOTH, 2, copy_rect_8803);
        classic::style_progress(op->bar);
        SendMessageW(op->bar, PBM_SETRANGE32, 0, 10000);
        op->stop = view(L"BUTTON", L"Stop", BS_PUSHBUTTON, 3, stop_rect);
        view(L"STATIC", L"Time remaining:", SS_LEFT, 20, copy_rect_8830);
        auto action_rect = copy_rect_8801, bytes_rect = copy_rect_8834;
        if (extracting) {
            action_rect.x -= 10;
            action_rect.width += 10;
            bytes_rect.width += 16;
        }
        view(L"STATIC", extracting ? L"Extracting:" : moving ? L"Moving:" : L"Copying:", SS_LEFT, 21, action_rect);
        view(L"STATIC", L"From:", SS_LEFT, 22, copy_rect_8832);
        view(L"STATIC", L"To:", SS_LEFT, 23, copy_rect_8833);
        view(L"STATIC", extracting ? L"Bytes Extracted:" : moving ? L"Bytes Moved:" : L"Bytes Copied:", SS_LEFT, 24,
             bytes_rect);
        const FinderCopyBounds rows[] = {copy_rect_8830, copy_rect_8802, copy_rect_8832, copy_rect_8833,
                                         copy_rect_8834};
        for (unsigned i = 0; i < 5; ++i) {
            int x = i ? copy_rect_8802.x : copy_rect_8830.x + copy_rect_8830.width + 2;
            int right = i ? copy_rect_8802.x + copy_rect_8802.width : copy_width - 6;
            if (extracting) {
                right += extraction_extra_width;
                if (i == 4)
                    x = bytes_rect.x + bytes_rect.width + 3;
            }
            op->fields[i] = control(w, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 10 + i, x, rows[i].y, right - x,
                                    rows[i].height);
        }
        if (extracting) {
            auto text = L"Format: " + op->extraction->formats;
            view(L"STATIC", text.c_str(), SS_LEFT | SS_ENDELLIPSIS, 25,
                 {bytes_rect.x, bytes_rect.y + extraction_extra_height, copy_width + extraction_extra_width - 15,
                  bytes_rect.height});
            const auto& sources = op->extraction->sources;
            auto source = sources.size() > 1 ? std::to_wstring(sources.size()) + L" archives"
                          : sources.front().volume ? image_text(sources.front().name.c_str())
                                                   : sources.front().path.filename().wstring();
            SetWindowTextW(op->fields[2], source.c_str());
        }
        for (int id = 10; id <= 25; ++id)
            if (auto text = GetDlgItem(w, id))
                SendMessageW(text, WM_SETFONT, reinterpret_cast<WPARAM>(classic::small_font()), TRUE);
        SetTimer(w, 1, 100, nullptr);
        return 0;
    }
    if (!op)
        return DefWindowProcW(w, msg, wp, lp);
    if (msg == WM_COMMAND && LOWORD(wp) == 3) {
        if (op->job->state >= CopyJob::complete)
            ShowWindow(w, SW_HIDE);
        else {
            op->job->stop = true;
            EnableWindow(op->stop, FALSE);
        }
        return 0;
    }
    if (msg == WM_CLOSE) {
        if (op->job->state <= CopyJob::copying) {
            op->job->stop = true;
            EnableWindow(op->stop, FALSE);
        }
        else
            ShowWindow(w, SW_HIDE);
        return 0;
    }
    if (msg == WM_TIMER) {
        auto& job = *op->job;
        auto state = job.state.load();
        auto done = job.done.load(), total = job.total.load();
        unsigned value =
            total ? unsigned(std::min<long double>(10000, static_cast<long double>(done) * 10000 / total)) : 0;
        SendMessageW(op->bar, PBM_SETPOS, value, 0);
        std::wstring remaining;
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - op->started).count();
        if (state == CopyJob::waiting)
            remaining = L"Waiting for a decision...";
        else if (state == CopyJob::planning || !done || seconds < 1)
            remaining = L"Calculating...";
        else
            remaining = std::to_wstring(uint64_t((total > done ? total - done : 0) * seconds / done)) + L" seconds";
        if (state == CopyJob::complete)
            remaining = L"Complete";
        if (state == CopyJob::cancelled)
            remaining = L"Stopped";
        if (state == CopyJob::failed)
            remaining = op->extraction ? L"Extraction failed" : moving_transfer(job.kind) ? L"Move failed" : L"Copy failed";
        SetWindowTextW(op->fields[0], remaining.c_str());
        {
            std::lock_guard<std::mutex> guard(job.mutex);
            SetWindowTextW(op->fields[1], job.current.c_str());
            if (!op->extraction)
                SetWindowTextW(op->fields[2], job.from.c_str());
            SetWindowTextW(op->fields[3], job.to.c_str());
        }
        auto bytes = format_bytes(done) + L" of " + format_bytes(total);
        SetWindowTextW(op->fields[4], bytes.c_str());
        SetWindowTextW(GetDlgItem(w, 9), std::to_wstring(job.items.load()).c_str());
        if (state >= CopyJob::complete) {
            KillTimer(w, 1);
            SetWindowTextW(op->stop, L"Close");
            EnableWindow(op->stop, TRUE);
            if (state == CopyJob::failed) {
                std::wstring error;
                {
                    std::lock_guard<std::mutex> guard(job.mutex);
                    error = job.error;
                }
                message(w, error, MB_ICONERROR);
            }
        }
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

// Create a separate copy window for a shared job and retain its state in the
// UI-owned operation registry.
void show_operation(std::shared_ptr<CopyJob> job, DWORD clipboard_sequence,
                    std::function<void(const CopyJob&)> on_complete, std::optional<DropPlacement> drop,
                    std::optional<Extraction> extraction)
{
    auto op = std::make_unique<Operation>();
    op->job = std::move(job);
    op->clipboard_sequence = clipboard_sequence;
    op->on_complete = std::move(on_complete);
    op->drop = std::move(drop);
    op->extraction = std::move(extraction);
    bool extracting = op->extraction.has_value();
    auto destination = op->job->destinations[0].parent_path();
    auto destination_name = destination.has_filename() ? destination.filename().wstring() : destination.wstring();
    if (extracting && op->extraction->destination.volume) {
        const auto& target = op->extraction->destination;
        FinderImageEntry directory{};
        if (!ROMlib_finder_image_entry(target.volume, target.directory, -1, &directory))
            destination_name = image_text(directory.name);
    }
    auto title = std::wstring(extracting ? L"Extracting to \x201c"
                              : moving_transfer(op->job->kind) ? L"Move to \x201c" : L"Copy to \x201c") +
                 destination_name + L"\x201d";
    unsigned slot = 0;
    for (const auto& o : operations)
        if (o->job->state <= CopyJob::copying)
            ++slot;
    HWND w =
        CreateWindowExW(WS_EX_TOOLWINDOW, L"FinderOperation", title.c_str(),
                        WS_CHILD | WS_CLIPSIBLINGS | WS_CAPTION | WS_SYSMENU, 140 + (slot % 6) * 26,
                        160 + (slot % 6) * 26, copy_width + 4 + (extracting ? extraction_extra_width : 0),
                        copy_height + 21 + (extracting ? extraction_extra_height : 0), desktop, nullptr, instance,
                        op.get());
    if (!w)
        throw std::runtime_error("Cannot create a copy window");
    operations.push_back(std::move(op));
    ShowWindow(w, SW_SHOWNORMAL);
    classic::activate(w);
}

// Reuse an existing folder window or restore geometry for a new child; search
// windows keep independent result state.
void open_folder(const fs::path& input, std::shared_ptr<Search> search, int16_t image_volume, uint32_t image_directory,
                 const std::wstring& image_title)
{
    classic::WaitingCursor waiting;
    auto path = fs::absolute(input).lexically_normal();
    for (auto* f : folders)
        if (!search && !f->search && !f->desktop && _wcsicmp(f->path.c_str(), path.c_str()) == 0) {
            ShowWindow(f->window, IsIconic(f->window) ? SW_RESTORE : SW_SHOW);
            // Folder frames are children of the ROMAW host. Activate through
            // the classic window manager, then let WM_SETFOCUS select this
            // folder and focus its list, just as for a newly opened window.
            classic::activate(f->window);
            SetFocus(f->window);
            return;
        }
    if (!image_volume && !search && !fs::is_directory(path))
        throw std::runtime_error("The folder is not available");
    auto f = std::make_unique<FolderWindow>();
    f->path = path;
    f->search = std::move(search);
    f->image_volume = image_volume;
    f->image_directory = image_directory;
    f->image_title = image_title;
    int x = 150, y = 130, width = 620, height = 430;
    auto geometry =
        f->search ? std::wstring{}
                  : settings_path_value(preferences, image_volume ? L"ImageWindowsNative" : L"WindowsNative", path);
    MacFile catalog;
    try {
        if (image_volume) {
            FinderImageEntry entry{};
            if (!ROMlib_finder_image_entry(image_volume, image_directory, -1, &entry))
                catalog = mac_catalog_info(entry.finder_info, true);
        }
        else if (!f->search)
            catalog = mac_file(path);
    }
    catch (...) {
    }
    if (image_volume)
        f->presentation = decode_catalog_view(catalog.folder_view);
    if (geometry.empty() && catalog.windowed) {
        const auto& r = catalog.window_bounds;
        x = r.left - 2;
        y = r.top - 39;
        width = r.right - r.left + 4;
        height = r.bottom - r.top + 21;
        geometry = std::to_wstring(x) + L"," + std::to_wstring(y) + L"," + std::to_wstring(width) + L"," +
                   std::to_wstring(height);
    }
    if (geometry.empty() && !f->search && !image_volume)
        geometry = settings_path_value(preferences, L"Windows", path);
    if (swscanf(geometry.c_str(), L"%d,%d,%d,%d", &x, &y, &width, &height) != 4) {
        x += int(folders.size() % 8) * 24;
        y += int(folders.size() % 8) * 24;
    }
    RECT workspace;
    GetClientRect(desktop, &workspace);
    width = std::clamp(width, image_volume ? 104 : 320, 1600);
    height = std::clamp(height, image_volume ? 121 : 240, 1000);
    width = std::min(width, int(workspace.right));
    height = std::min(height, int(workspace.bottom));
    x = std::clamp(x, 0, std::max(0, int(workspace.right) - width));
    y = std::clamp(y, 0, std::max(0, int(workspace.bottom) - height));
    const bool windows_desktop_root = _wcsicmp(path.c_str(), windows_desktop_path.c_str()) == 0;
    auto title = f->search                                           ? L"Items Found"
                 : image_volume                                      ? image_title
                 : windows_desktop_root                              ? windows_desktop_name
                 : _wcsicmp(path.c_str(), romaw_folder.c_str()) == 0 ? L"ROMAW"
                 : path.filename().empty()                           ? volume_display_name(path, path.wstring())
                                                                     : mac_display_name(path, catalog);
    HWND w = CreateWindowExW(WS_EX_ACCEPTFILES, L"FinderFolder", title.c_str(),
                             WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, x,
                             y, width, height, desktop, nullptr, instance, f.get());
    if (!w)
        throw std::runtime_error("Cannot create a folder window");
    Item title_item;
    title_item.path = path;
    title_item.name = title;
    title_item.folder = true;
    if (windows_desktop_root)
        title_item.special = windows_desktop_volume;
    try {
        title_item.mac = mac_file(path);
    }
    catch (...) {
    }
    SendMessageW(w, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(item_icon(title_item)));
    f->window_owned = true;
    current_folder = f.get();
    f.release();
    ShowWindow(w, SW_SHOWNORMAL);
    classic::activate(w);
    SetFocus(w);
}
}

// Run the native desktop after shared Toolbox initialization; restore the session
// and release windows, workers and owned artwork on shutdown.
extern "C" int romaw_finder_main(void)
{
    HINSTANCE h = GetModuleHandleW(nullptr);
    instance = h;
    try {
        SetProcessDPIAware();
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        INITCOMMONCONTROLSEX common{sizeof common, ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS};
        InitCommonControlsEx(&common);
        executable_folder = module_folder();
        set_drive_format_lookup(config_drive_format);
        set_folder_archive_writer(write_stuffit_folder);
        set_file_archive_writer(write_macbinary_file);
        set_host_import_services(read_macbinary_file, lookup_host_type);
        wchar_t user[32768];
        if (!GetEnvironmentVariableW(L"USERPROFILE", user, std::size(user)))
            throw std::runtime_error("USERPROFILE is unavailable");
        // Use the current shell location, including a redirected or moved desktop.
        PWSTR shell_desktop = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DONT_VERIFY, nullptr, &shell_desktop))) {
            std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> path(shell_desktop, CoTaskMemFree);
            windows_desktop_path = fs::path(path.get()).lexically_normal();
            if (!config_drive_set_windows_desktop(windows_desktop_path.u8string().c_str()))
                throw std::runtime_error("Cannot configure the Windows Desktop volume");
        }
        romaw_folder = (fs::path(user) / L".romaw/Apps").lexically_normal();
        remove_startup_preferences(romaw_folder.parent_path() / L"ROMAW.prefs");
        wchar_t* local = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
            throw std::runtime_error("Cannot locate application settings");
        std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> local_directory(local, CoTaskMemFree);
        preferences = settings_directory(romaw_folder.parent_path(), fs::path(local_directory.get())) / L"settings.ini";
        show_hidden = profile_string(L"View", L"ShowHidden", L"0") == L"1";
        spring_folders = profile_string(L"View", L"SpringFolders", L"1") == L"1";
        desktop_folder = preferences.parent_path() / L"Desktop";
        if (fs::is_directory(romaw_folder / L"Desktop Folder"))
            desktop_folder = romaw_folder / L"Desktop Folder";
        fs::create_directories(desktop_folder);
        classic::initialize(font_chicago, sizeof font_chicago, font_geneva, sizeof font_geneva);
        classic::use_graphics(romaw_classic_graphics());
        std::vector<fs::path> icon_roots{romaw_folder};
        auto drives = config_drive_prefs_mask();
        for (unsigned drive = 0; drive < 26; ++drive)
            if (drives & (1u << drive))
                icon_roots.emplace_back(std::wstring(1, wchar_t(L'A' + drive)) + L":\\");
        for (size_t i = 0; i < config_drive_directory_count(); ++i)
            icon_roots.push_back(fs::u8path(config_drive_directory(i)));
        desktop_icons = std::make_unique<DesktopIcons>(icon_roots);
        for (const auto& root : icon_roots)
            desktop_icons->scan(root);
        font = classic::small_font();
        app_icon = asset_icon(4);
        volume_lock_icon = rgba_icon(volume_lock_rgba, 16, 16);
        disclosure_icons = {rgba_icon(disclosure_closed_rgba, 16, 16), rgba_icon(disclosure_open_rgba, 16, 16)};
        for (unsigned i = 7; i < classic_art.size(); ++i)
            classic_art[i] = asset_icon(5 + i);
        const unsigned char* label_pixels[] = {label_1_rgba, label_2_rgba, label_3_rgba, label_4_rgba,
                                               label_5_rgba, label_6_rgba, label_7_rgba};
        for (unsigned i = 0; i < 7; ++i) {
            classic_art[i] = rgba_icon(label_pixels[i], 32, 32);
        }
        classic::artwork(classic_art[7], classic_art[8], app_icon, classic_art[9], classic_art.data());
        frame_art = {rgba_icon(chrome_close_rgba, 16, 16), rgba_icon(chrome_zoom_rgba, 16, 16),
                     rgba_icon(chrome_grow_rgba, 16, 16), rgba_icon(chrome_close_clicked_rgba, 16, 16)};
        classic::chrome_art(frame_art[0], frame_art[1], frame_art[2], frame_art[3]);
        WNDCLASSEXW c{};
        c.cbSize = sizeof c;
        c.hInstance = h;
        c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        c.hIcon = app_icon;
        c.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        c.lpszClassName = L"FinderFolder";
        c.lpfnWndProc = folder_proc;
        RegisterClassExW(&c);
        c.lpszClassName = L"FinderOperation";
        c.lpfnWndProc = operation_proc;
        RegisterClassExW(&c);
        auto f = std::make_unique<FolderWindow>();
        f->desktop = true;
        desktop = CreateWindowExW(0, L"FinderFolder", L"ROMAW", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 80, 80, 1000,
                                  760, nullptr, nullptr, h, f.get());
        if (!desktop)
            throw std::runtime_error("Cannot create the Finder desktop");
        f->window_owned = true;
        f.release();
        if (!restore_host_window(desktop, preferences))
            ShowWindow(desktop, SW_SHOWNORMAL);
        SetTimer(desktop, 1, 200, nullptr);
        {
            unsigned count =
                std::min<unsigned>(32, GetPrivateProfileIntW(L"Session", L"Count", 0, preferences.c_str()));
            for (unsigned i = 1; i <= count; ++i) {
                auto p = profile_string(L"Session", L"Window" + std::to_wstring(i));
                if (!p.empty() && fs::is_directory(p))
                    open_folder(p);
            }
        }
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (ROMlib_finder_afp_message(&msg) || classic::translate_menu_key(msg))
                continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        stop_sound_preview();
        desktop_icons.reset();
        copies.cancel_all();
        while (!folders.empty())
            DestroyWindow(folders.back()->window);
        for (auto& op : operations)
            if (IsWindow(op->window))
                DestroyWindow(op->window);
        for (auto p : children)
            CloseHandle(p);
        for (auto& i : selected_icon_cache)
            DestroyIcon(i.second);
        selected_icon_cache.clear();
        for (auto& i : icon_cache)
            DestroyIcon(i.second);
        DestroyIcon(app_icon);
        DestroyIcon(volume_lock_icon);
        volume_lock_icon = nullptr;
        for (auto& icon : disclosure_icons) {
            DestroyIcon(icon);
            icon = nullptr;
        }
        for (auto icon : classic_art)
            DestroyIcon(icon);
        for (auto icon : frame_art)
            DestroyIcon(icon);
        classic::finish();
        CoUninitialize();
        return 0;
    }
    catch (const std::exception& e) {
        stop_sound_preview();
        copies.cancel_all();
        failure(nullptr, e);
        return 1;
    }
}
