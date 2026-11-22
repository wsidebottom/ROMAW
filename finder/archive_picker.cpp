/*+===================================================================
  File:      archive_picker.cpp

  Summary:   Movable classic folder and archive-format pickers for native Finder extraction.

  Classes:   Picker, Row; ArchiveDestination.

  Functions: finder::pick_archive_destination, finder::pick_folder_archive_format.

  Origin:    ROMAW classic controls with host and live Macintosh catalog navigation.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#include "finder/archive.h"
#include "finder/classic.h"
#include "finder/image_volumes.h"
#include "finder/image_copy.h"
#include "rsys/configprefs.h"
#include <algorithm>
#include <commctrl.h>
#include <stdexcept>

namespace finder {
namespace {
struct Row {
    std::wstring name;
    ArchiveDestination destination;
};

struct Picker {
    ArchiveDestination current, result;
    std::vector<fs::path> shortcuts;
    std::vector<Row> rows;
    bool volumes = false, format = false;
    int chosen = 0;
};

std::wstring catalog_text(const char* text, unsigned codepage = 10000)
{
    int size = MultiByteToWideChar(codepage, 0, text, -1, nullptr, 0);
    if (size < 1)
        throw std::runtime_error("Invalid volume name");
    std::wstring value(size, 0);
    MultiByteToWideChar(codepage, 0, text, -1, value.data(), size);
    value.pop_back();
    return value;
}

void enable_choice(HWND window, Picker& picker)
{
    int index = ListView_GetNextItem(GetDlgItem(window, 20), -1, LVNI_SELECTED);
    auto selected = index >= 0 && size_t(index) < picker.rows.size() ? picker.rows[index].destination : picker.current;
    EnableWindow(GetDlgItem(window, IDOK), (!picker.volumes || index >= 0) && archive_destination_allowed(selected));
}

void refresh(HWND window, Picker& picker)
{
    picker.rows.clear();
    auto list = GetDlgItem(window, 20);
    ListView_DeleteAllItems(list);
    if (picker.volumes) {
        SetDlgItemTextW(window, 10, L"Mac-compatible drives and images");
        DWORD drives = GetLogicalDrives();
        for (unsigned i = 0; i < 26; ++i)
            if (drives & (1u << i)) {
                wchar_t root[] = L"A:\\";
                root[0] += i;
                ArchiveDestination destination{root};
                if (archive_destination_allowed(destination))
                    picker.rows.push_back({root, destination});
            }
        for (size_t i = 0; i < config_drive_directory_count(); ++i)
            picker.shortcuts.push_back(fs::u8path(config_drive_directory(i)));
        std::sort(picker.shortcuts.begin(), picker.shortcuts.end());
        picker.shortcuts.erase(std::unique(picker.shortcuts.begin(), picker.shortcuts.end()), picker.shortcuts.end());
        for (const auto& path : picker.shortcuts) {
            ArchiveDestination destination{path};
            if (archive_destination_allowed(destination))
                picker.rows.push_back({path.wstring(), destination});
        }
        for (unsigned i = 0;; ++i) {
            FinderImageVolume volume{};
            if (!ROMlib_finder_image_volume(i, &volume))
                break;
            if (volume.read_only || ROMlib_finder_image_writable(volume.reference))
                continue;
            auto path = fs::path(catalog_text(volume.source, CP_ACP)) /
                        (L".mounted-volume-" + std::to_wstring(volume.partition));
            picker.rows.push_back({catalog_text(volume.name), {path, volume.reference, 2}});
        }
    }
    else if (picker.current.volume) {
        FinderImageEntry current{};
        if (ROMlib_finder_image_entry(picker.current.volume, picker.current.directory, -1, &current))
            throw std::runtime_error("The image folder is unavailable");
        SetDlgItemTextW(window, 10, catalog_text(current.name).c_str());
        for (int index = 1; index <= current.children; ++index) {
            FinderImageEntry item{};
            if (index > INT16_MAX ||
                ROMlib_finder_image_entry(picker.current.volume, picker.current.directory, int16_t(index), &item))
                throw std::runtime_error("Cannot list image folders");
            if (item.directory && !image_copy_private(picker.current.volume, item.id))
                picker.rows.push_back(
                    {catalog_text(item.name),
                     {picker.current.path / std::to_wstring(item.id), picker.current.volume, item.id}});
        }
    }
    else {
        SetDlgItemTextW(window, 10, picker.current.path.c_str());
        for (const auto& item :
             fs::directory_iterator(picker.current.path, fs::directory_options::skip_permission_denied)) {
            auto attributes = GetFileAttributesW(item.path().c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                item.is_directory() && item.path().filename().wstring().rfind(L".", 0) != 0)
                picker.rows.push_back({item.path().filename().wstring(), {item.path()}});
        }
    }
    std::stable_sort(picker.rows.begin(), picker.rows.end(),
                     [](const auto& a, const auto& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    for (size_t i = 0; i < picker.rows.size(); ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = int(i);
        item.pszText = picker.rows[i].name.data();
        ListView_InsertItem(list, &item);
    }
    EnableWindow(GetDlgItem(window, 3), !picker.volumes);
    enable_choice(window, picker);
}

INT_PTR CALLBACK picker_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    auto* picker = reinterpret_cast<Picker*>(GetWindowLongPtrW(window, DWLP_USER));
    try {
        if (message == WM_INITDIALOG) {
            picker = reinterpret_cast<Picker*>(lp);
            SetWindowLongPtrW(window, DWLP_USER, lp);
            SetWindowPos(window, nullptr, 0, 0, 500, picker->format ? 200 : 380, SWP_NOMOVE | SWP_NOZORDER);
            auto control = [&](const wchar_t* type, const wchar_t* text, DWORD style, int id, int x, int y, int w,
                               int h) {
                return CreateWindowW(type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window,
                                     reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
            };
            if (picker->format) {
                control(L"STATIC", L"MacBinary cannot contain folders. Choose an archive format:", 0, 10, 16, 16, 460,
                        32);
                auto combo =
                    control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 15, 16, 52, 460, 160);
                SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"ZIP (OSX ditto format)"));
                SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"StuffIt 5.5-compatible archive"));
                SendMessageW(combo, CB_SETCURSEL, 0, 0);
            }
            else {
                control(L"STATIC", L"", SS_PATHELLIPSIS, 10, 16, 12, 458, 30);
                auto list = control(WC_LISTVIEWW, L"",
                                    LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
                                    20, 16, 48, 458, 232);
                LVCOLUMNW column{};
                column.mask = LVCF_WIDTH;
                column.cx = 435;
                ListView_InsertColumn(list, 0, &column);
                ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
                control(L"BUTTON", L"Up", BS_PUSHBUTTON | WS_TABSTOP, 3, 16, 294, 70, 24);
                control(L"BUTTON", L"Volumes", BS_PUSHBUTTON | WS_TABSTOP, 4, 96, 294, 86, 24);
                classic::style_list(list);
            }
            int y = picker->format ? 110 : 294;
            control(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 278, y, 90, 24);
            control(L"BUTTON", picker->format ? L"Use Format" : L"Select", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 382, y,
                    92, 24);
            classic::style_dialog(window);
            if (!picker->format)
                refresh(window, *picker);
            return TRUE;
        }
        if (!picker)
            return FALSE;
        if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wp) == IDCANCEL)) {
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
        if (message == WM_COMMAND && LOWORD(wp) == IDOK) {
            if (picker->format) {
                picker->chosen = SendDlgItemMessageW(window, 15, CB_GETCURSEL, 0, 0) == 0 ? 2 : 1;
            }
            else {
                int index = ListView_GetNextItem(GetDlgItem(window, 20), -1, LVNI_SELECTED);
                picker->result = index >= 0 && size_t(index) < picker->rows.size() ? picker->rows[index].destination
                                                                                   : picker->current;
                if ((picker->volumes && index < 0) || !archive_destination_allowed(picker->result))
                    return TRUE;
            }
            EndDialog(window, IDOK);
            return TRUE;
        }
        if (message == WM_COMMAND && LOWORD(wp) == 4) {
            picker->volumes = true;
            refresh(window, *picker);
            return TRUE;
        }
        if (message == WM_COMMAND && LOWORD(wp) == 3) {
            if (picker->current.volume) {
                FinderImageEntry entry{};
                if (picker->current.directory == 2)
                    picker->volumes = true;
                else if (ROMlib_finder_image_entry(picker->current.volume, picker->current.directory, -1, &entry))
                    throw std::runtime_error("Cannot find the parent image folder");
                else {
                    picker->current.directory = entry.parent;
                    picker->current.path = picker->current.path.parent_path();
                }
            }
            else if (picker->current.path == picker->current.path.root_path())
                picker->volumes = true;
            else
                picker->current.path = picker->current.path.parent_path();
            refresh(window, *picker);
            return TRUE;
        }
        if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lp)->idFrom == 20) {
            auto code = reinterpret_cast<NMHDR*>(lp)->code;
            if (code == NM_DBLCLK || code == LVN_ITEMACTIVATE) {
                int index = ListView_GetNextItem(GetDlgItem(window, 20), -1, LVNI_SELECTED);
                if (index >= 0 && size_t(index) < picker->rows.size()) {
                    picker->current = picker->rows[index].destination;
                    picker->volumes = false;
                    refresh(window, *picker);
                }
            }
            else if (code == LVN_ITEMCHANGED)
                enable_choice(window, *picker);
            return TRUE;
        }
    }
    catch (const std::exception&) {
        classic::message_box(window, L"This folder is unavailable. Choose another Mac-compatible drive or image.",
                             MB_ICONWARNING);
        if (picker && !picker->format) {
            picker->volumes = true;
            picker->rows.clear();
            ListView_DeleteAllItems(GetDlgItem(window, 20));
            EnableWindow(GetDlgItem(window, IDOK), FALSE);
        }
    }
    return FALSE;
}

bool show(HWND owner, Picker& picker)
{
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    std::vector<WORD> dialog{WORD(style), WORD(style >> 16), 0, 0, 0, 30, 30, 300, 220, 0, 0};
    const wchar_t* title = picker.format ? L"Folder Archive Format" : L"Extract To";
    do {
        dialog.push_back(*title);
    } while (*title++);
    INT_PTR result =
        DialogBoxIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<const DLGTEMPLATE*>(dialog.data()), owner,
                                picker_proc, reinterpret_cast<LPARAM>(&picker));
    if (result == -1)
        throw std::runtime_error("Cannot open the classic folder picker");
    return result == IDOK;
}
}

std::optional<ArchiveDestination> pick_archive_destination(HWND owner, const ArchiveDestination& initial,
                                                           const std::vector<fs::path>& shortcuts)
{
    Picker picker;
    picker.current = initial;
    picker.shortcuts = shortcuts;
    picker.volumes = initial.path.empty();
    if (show(owner, picker))
        return picker.result;
    return {};
}

int pick_folder_archive_format(HWND owner)
{
    Picker picker;
    picker.format = true;
    return show(owner, picker) ? picker.chosen : 0;
}
}
