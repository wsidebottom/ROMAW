/*+===================================================================
  File:      win_preferences.c

  Summary:   Native Windows view of the shared SDL/ECF preferences model.

  Classes:   None.

  Functions: ROMlib_win_drive_preferences, ROMlib_win_preferences

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Native Windows view of the shared SDL/ECF preferences model. */
#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES
#include "rsys/common.h"
#include <windows.h>
#include <shlobj.h>
#include "rsys/prefpanel.h"
#include "rsys/configprefs.h"
#include "sdlevents.h"

extern HWND cygwin_sdlwindow(void);

static void drive_folder_add(HWND list, const char* path)
{
    WCHAR wide[4096];
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, 4096))
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)wide);
}

static INT_PTR CALLBACK drive_preferences_proc(HWND dialog, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message) {
    case WM_INITDIALOG:
        SetWindowLongPtr(dialog, DWLP_USER, lp);
        CreateWindowA("STATIC",
                      "Select each drive's format. First-use values are detected.\n"
                      "Windows drives do not rebuild Mac desktops.\n"
                      "The mapped Apps folder always uses OSX sidecars.",
                      WS_CHILD | WS_VISIBLE, 12, 12, 490, 58, dialog, NULL, GetModuleHandle(NULL), NULL);
        DWORD available = GetLogicalDrives();
        uint32_t selected = config_drive_prefs_mask();
        for (unsigned i = 0; i < 26; ++i) {
            char root[] = "A:/", label[] = "A:";
            root[0] += i;
            label[0] += i;
            int format = config_drive_format(root);
            int declared = format != CONFIG_DRIVE_UNDECLARED;
            if (!declared && (available & (1u << i))) {
                DWORD previous = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
                format = config_drive_detect_format(root);
                SetErrorMode(previous);
                if (format == CONFIG_DRIVE_WINDOWS && (selected & (1u << i)))
                    format = CONFIG_DRIVE_OSX;
            }
            int x = 18 + (i / 9) * 164, y = 78 + (i % 9) * 25;
            CreateWindowA("STATIC", label, WS_CHILD | WS_VISIBLE, x, y + 3, 22, 20, dialog, NULL, GetModuleHandle(NULL),
                          NULL);
            HWND box =
                CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                              x + 24, y, 132, 160, dialog, (HMENU)(INT_PTR)(100 + i), GetModuleHandle(NULL), NULL);
            for (int choice = CONFIG_DRIVE_WINDOWS; choice <= CONFIG_DRIVE_ROMAW; ++choice)
                SendMessageA(box, CB_ADDSTRING, 0, (LPARAM)config_drive_format_name(choice));
            SendMessage(box, CB_SETCURSEL, format ? format - CONFIG_DRIVE_WINDOWS : 0, 0);
            EnableWindow(box, !!((available & (1u << i)) || declared));
        }
        CreateWindowA("BUTTON", "Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 318, 454, 82, 26, dialog,
                      (HMENU)IDOK, GetModuleHandle(NULL), NULL);
        CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 412, 454, 82, 26, dialog, (HMENU)IDCANCEL,
                      GetModuleHandle(NULL), NULL);
        CreateWindowA("STATIC", "Mac-compatible folders (mounted at next start):", WS_CHILD | WS_VISIBLE, 18, 310, 478,
                      22, dialog, NULL, GetModuleHandle(NULL), NULL);
        HWND folders = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
                                           LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
                                       18, 336, 376, 100, dialog, (HMENU)200, GetModuleHandle(NULL), NULL);
        for (size_t i = 0; i < config_drive_directory_count(); ++i)
            drive_folder_add(folders, config_drive_directory(i));
        SendMessage(folders, LB_SETHORIZONTALEXTENT, 1600, 0);
        CreateWindowA("BUTTON", "Add folder...", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 404, 336, 100, 26, dialog,
                      (HMENU)201, GetModuleHandle(NULL), NULL);
        CreateWindowA("BUTTON", "Remove", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 404, 370, 100, 26, dialog, (HMENU)202,
                      GetModuleHandle(NULL), NULL);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == 201) {
            HWND list = GetDlgItem(dialog, 200);
            if (SendMessage(list, LB_GETCOUNT, 0, 0) >= CONFIG_DRIVE_DIRECTORY_LIMIT)
                return TRUE;
            BROWSEINFOW browse = {0};
            HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            browse.hwndOwner = dialog;
            browse.lpszTitle = L"Choose a Mac-compatible folder";
            browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_EDITBOX | (SUCCEEDED(initialized) ? BIF_NEWDIALOGSTYLE : 0);
            PIDLIST_ABSOLUTE selected = SHBrowseForFolderW(&browse);
            if (selected) {
                WCHAR wide[MAX_PATH];
                char path[4096];
                if (SHGetPathFromIDListW(selected, wide)) {
                    for (WCHAR* p = wide; *p; ++p)
                        if (*p == L'\\')
                            *p = L'/';
                    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, path, sizeof path, NULL, NULL) &&
                        SendMessageW(list, LB_FINDSTRINGEXACT, -1, (LPARAM)wide) == LB_ERR)
                        drive_folder_add(list, path);
                }
                CoTaskMemFree(selected);
            }
            if (SUCCEEDED(initialized))
                CoUninitialize();
            return TRUE;
        }
        if (LOWORD(wp) == 202) {
            HWND list = GetDlgItem(dialog, 200);
            LRESULT index = SendMessage(list, LB_GETCURSEL, 0, 0);
            if (index != LB_ERR)
                SendMessage(list, LB_DELETESTRING, index, 0);
            return TRUE;
        }
        if (LOWORD(wp) == IDOK) {
            int formats[26] = {0};
            for (unsigned i = 0; i < 26; ++i)
                if (IsWindowEnabled(GetDlgItem(dialog, 100 + i))) {
                    LRESULT index = SendDlgItemMessage(dialog, 100 + i, CB_GETCURSEL, 0, 0);
                    if (index < 0 || index > CONFIG_DRIVE_ROMAW - CONFIG_DRIVE_WINDOWS)
                        return TRUE;
                    formats[i] = (int)index + CONFIG_DRIVE_WINDOWS;
                }
            const char* path = (const char*)GetWindowLongPtr(dialog, DWLP_USER);
            HWND list = GetDlgItem(dialog, 200);
            LRESULT count = SendMessage(list, LB_GETCOUNT, 0, 0);
            char* directories[CONFIG_DRIVE_DIRECTORY_LIMIT] = {0};
            int ok = count >= 0 && count <= CONFIG_DRIVE_DIRECTORY_LIMIT;
            for (LRESULT i = 0; ok && i < count; ++i) {
                WCHAR wide[4096];
                LRESULT size = SendMessageW(list, LB_GETTEXTLEN, i, 0);
                ok = size > 0 && size < 4096 && SendMessageW(list, LB_GETTEXT, i, (LPARAM)wide) != LB_ERR;
                int bytes = ok ? WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL) : 0;
                directories[i] = bytes ? malloc(bytes) : NULL;
                ok = directories[i] && WideCharToMultiByte(CP_UTF8, 0, wide, -1, directories[i], bytes, NULL, NULL);
            }
            if (ok)
                ok = config_drive_prefs_save_formats(path, formats, (const char* const*)directories, count);
            for (unsigned i = 0; i < CONFIG_DRIVE_DIRECTORY_LIMIT; ++i)
                free(directories[i]);
            if (!ok) {
                MessageBoxA(dialog, "Unable to save drive preferences.", "ROMAW", MB_OK | MB_ICONERROR);
                return TRUE;
            }
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

int ROMlib_win_drive_preferences(const char* path)
{
    struct {
        DLGTEMPLATE dialog;
        WORD menu, window_class;
        WCHAR title[22];
    } layout = {{WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 0, 0, 20, 20, 270, 260}, 0, 0, L"Drive Formats"};

    INT_PTR result = DialogBoxIndirectParamW(GetModuleHandle(NULL), &layout.dialog, cygwin_sdlwindow(),
                                             drive_preferences_proc, (LPARAM)path);
    sdl_preferences_input_reset();
    return result == IDOK;
}

typedef struct {
    ROMlib_extra_preferences edited;
    const char* filename;
} native_preferences;

static int field_page(unsigned field)
{
    return field == 10 || field == 11 ? 2 : field >= 12 ? 0 : 1;
}

static HWND add_control(HWND dialog, const char* kind, const char* text, DWORD style, int x, int y, int width,
                        int height, int id)
{
    RECT rect = {x, y, x + width, y + height};
    HWND item;
    MapDialogRect(dialog, &rect);
    item = CreateWindowExA(!strcmp(kind, "EDIT") ? WS_EX_CLIENTEDGE : 0, kind, text, WS_CHILD | WS_TABSTOP | style,
                           rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, dialog,
                           (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
    if (item)
        SendMessage(item, WM_SETFONT, SendMessage(dialog, WM_GETFONT, 0, 0), TRUE);
    return item;
}

static void show_page(HWND dialog, int page)
{
    unsigned i;
    for (i = 0; i < 19; ++i) {
        if (!ROMlib_preferences_field_label(i))
            continue;
        ShowWindow(GetDlgItem(dialog, 100 + i), field_page(i) == page ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(dialog, 200 + i), field_page(i) == page ? SW_SHOW : SW_HIDE);
    }
    for (i = 0; ROMlib_preferences_check_label(i); ++i)
        ShowWindow(GetDlgItem(dialog, 300 + i), page == 3 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, 400), page == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, 500), page == 0 ? SW_SHOW : SW_HIDE);
    for (i = 0; i < 4; ++i)
        SendDlgItemMessage(dialog, 10 + i, BM_SETCHECK, (int)i == page, 0);
}

static INT_PTR CALLBACK preferences_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    native_preferences* state = (native_preferences*)GetWindowLongPtr(dialog, DWLP_USER);
    unsigned i;
    switch (message) {
    case WM_INITDIALOG: {
        static const char* pages[] = {"General", "Display", "Memory", "Compatibility"};
        unsigned rows[4] = {0};
        RECT owner, bounds, work;
        state = (native_preferences*)lparam;
        SetWindowLongPtr(dialog, DWLP_USER, (LONG_PTR)state);
        for (i = 0; i < 4; ++i)
            if (!add_control(dialog, "BUTTON", pages[i],
                             BS_AUTORADIOBUTTON | BS_PUSHLIKE | WS_VISIBLE | (i ? 0 : WS_GROUP), 8 + i * 96, 8, 92, 18,
                             10 + i))
                goto failed;
        for (i = 0; i < 19; ++i) {
            const char* label = ROMlib_preferences_field_label(i);
            if (!label)
                continue;
            unsigned row = rows[field_page(i)]++;
            if (!add_control(dialog, "STATIC", label, 0, 10, 36 + row * 17, 186, 13, 100 + i) ||
                !add_control(dialog, i == 18 ? "COMBOBOX" : "EDIT", state->edited.fields[i],
                             i == 18 ? CBS_DROPDOWN | WS_VSCROLL : ES_AUTOHSCROLL, 198, 33 + row * 17, 184,
                             i == 18 ? 120 : 15, 200 + i))
                goto failed;
            if (i == 18) {
                const char* choices[] = {"1x", "1.5x", "2x", "4x", "8x", "Custom"};
                for (unsigned j = 0; j < 6; ++j)
                    SendDlgItemMessageA(dialog, 218, CB_ADDSTRING, 0, (LPARAM)choices[j]);
                SetDlgItemTextA(dialog, 218, state->edited.fields[i]);
                SendDlgItemMessage(dialog, 218, CB_LIMITTEXT, 255, 0);
            }
            else
                SendDlgItemMessage(dialog, 200 + i, EM_SETLIMITTEXT, 255, 0);
        }
        for (i = 0; ROMlib_preferences_check_label(i); ++i) {
            if (!add_control(dialog, "BUTTON", ROMlib_preferences_check_label(i), BS_AUTOCHECKBOX, 10 + (i / 12) * 192,
                             34 + (i % 12) * 14, 188, 13, 300 + i))
                goto failed;
            SendDlgItemMessage(dialog, 300 + i, BM_SETCHECK, ROMlib_preferences_check(&state->edited, i), 0);
        }
        if (!add_control(dialog, "STATIC",
                         "Enter bytes, or use k/m suffixes (for example 8m).\r\n\r\n"
                         "Blank or 0 uses startup defaults. Changes apply on the next launch.\r\n\r\n"
                         "Command-line -syszone, -applzone and -memory take precedence.",
                         0, 10, 80, 372, 100, 400) ||
            !add_control(dialog, "BUTTON", "OK", BS_DEFPUSHBUTTON | WS_VISIBLE, 216, 222, 50, 18, IDOK) ||
            !add_control(dialog, "BUTTON", "Save", BS_PUSHBUTTON | WS_VISIBLE, 274, 222, 50, 18, 3) ||
            !add_control(dialog, "BUTTON", "Cancel", BS_PUSHBUTTON | WS_VISIBLE, 332, 222, 50, 18, IDCANCEL))
            goto failed;
        add_control(dialog, "STATIC", "Save retains settings for this application in ROMAW.prefs.", WS_VISIBLE, 10, 205,
                    372, 13, 401);
        if (!add_control(dialog, "BUTTON", "Drive sidecars...", BS_PUSHBUTTON, 10, 182, 100, 18, 500))
            goto failed;
        GetWindowRect(dialog, &bounds);
        SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
        if (!GetWindowRect(GetParent(dialog), &owner))
            owner = work;
        {
            int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
            int x = owner.left + ((owner.right - owner.left) - width) / 2;
            int y = owner.top + ((owner.bottom - owner.top) - height) / 2;
            x = MAX(work.left, MIN(x, work.right - width));
            y = MAX(work.top, MIN(y, work.bottom - height));
            SetWindowPos(dialog, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        show_page(dialog, 0);
        return TRUE;
    failed:
        EndDialog(dialog, -1);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == 500) {
            if (state->filename)
                ROMlib_win_drive_preferences(state->filename);
            return TRUE;
        }
        if (LOWORD(wparam) == 218 && HIWORD(wparam) == CBN_SELCHANGE &&
            SendDlgItemMessage(dialog, 218, CB_GETCURSEL, 0, 0) == 5) {
            SetDlgItemTextA(dialog, 218, "");
            SetFocus(GetDlgItem(dialog, 218));
            return TRUE;
        }
        if (LOWORD(wparam) >= 10 && LOWORD(wparam) <= 13) {
            show_page(dialog, LOWORD(wparam) - 10);
            return TRUE;
        }
        if (LOWORD(wparam) == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        if (LOWORD(wparam) == IDOK || LOWORD(wparam) == 3) {
            unsigned bad = 0;
            for (i = 0; i < 19; ++i)
                if (ROMlib_preferences_field_label(i))
                    GetDlgItemTextA(dialog, 200 + i, state->edited.fields[i], 256);
            if (!ROMlib_extra_preferences_valid(&state->edited, &bad)) {
                show_page(dialog, field_page(bad));
                MessageBoxA(dialog, "Invalid setting. Check the value and its supported range.",
                            ROMlib_preferences_field_label(bad), MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(dialog, 200 + bad));
                SendDlgItemMessage(dialog, 200 + bad, EM_SETSEL, 0, -1);
                return TRUE;
            }
            for (i = 0; ROMlib_preferences_check_label(i); ++i)
                if (!!SendDlgItemMessage(dialog, 300 + i, BM_GETCHECK, 0, 0) !=
                    ROMlib_preferences_check(&state->edited, i))
                    ROMlib_preferences_toggle(&state->edited, i);
            if (!ROMlib_extra_preferences_apply(&state->edited)) {
                MessageBoxA(dialog, "Unable to apply settings (out of memory).", "ROMAW Preferences",
                            MB_OK | MB_ICONERROR);
                return TRUE;
            }
            if (LOWORD(wparam) == 3 && (!state->filename || !saveprefvalues((char*)state->filename, 0, 0))) {
                MessageBoxA(dialog, "Unable to write ROMAW.prefs. Check the folder and file permissions.",
                            "ROMAW Preferences", MB_OK | MB_ICONERROR);
                return TRUE;
            }
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

/* A negative result selects the portable SDL fallback. */
int ROMlib_win_preferences(ROMlib_extra_preferences* prefs, const char* filename)
{
    struct {
        DLGTEMPLATE dialog;
        WORD menu, window_class;
        WCHAR title[21];
        WORD point_size;
        WCHAR font[9];
    } layout = {{WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT, 0, 0, 0, 0, 392, 248},
                0,
                0,
                L"ROMAW Preferences",
                9,
                L"Segoe UI"};

    native_preferences state = {*prefs, filename};
    INT_PTR result = DialogBoxIndirectParamW(GetModuleHandle(NULL), &layout.dialog, cygwin_sdlwindow(),
                                             preferences_proc, (LPARAM)&state);
    if (result == IDOK)
        *prefs = state.edited;
    sdl_preferences_input_reset();
    ROMlib_preferences_redraw();
    return result < 0 ? -1 : result == IDOK;
}
