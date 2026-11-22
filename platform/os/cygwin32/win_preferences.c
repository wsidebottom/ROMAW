/*+===================================================================
  File:      win_preferences.c

  Summary:   Native Windows view of the shared SDL/ECF preferences model.

  Classes:   None.

  Functions: ROMlib_win_default_preferences, ROMlib_win_drive_preferences, ROMlib_win_preferences

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Native Windows view of the shared SDL/ECF preferences model. */
#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES
#include "rsys/common.h"
#include <windows.h>
#include <shlobj.h>
#include <commctrl.h>
#include <limits.h>
#include "preferences_resource.h"
#include "rsys/prefpanel.h"
#include "rsys/configprefs.h"
#include "rsys/prefs.h"
#include "sdlevents.h"

extern HWND cygwin_sdlwindow(void);

static void drive_folder_add(HWND list, const char* path)
{
    WCHAR wide[4096];
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, 4096))
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)wide);
}

static int save_drive_preferences(HWND dialog)
{
    int formats[26] = {0};
    for (unsigned i = 0; i < 26; ++i)
        if (IsWindowEnabled(GetDlgItem(dialog, 100 + i))) {
            LRESULT index = SendDlgItemMessage(dialog, 100 + i, CB_GETCURSEL, 0, 0);
            if (index < 0 || index > CONFIG_DRIVE_ROMAW - CONFIG_DRIVE_WINDOWS)
                return 0;
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
        return 0;
    }
    return 1;
}

static INT_PTR CALLBACK drive_preferences_proc(HWND dialog, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message) {
    case WM_INITDIALOG:
        SetWindowLongPtr(dialog, DWLP_USER, lp);
        int embedded = GetDlgItem(dialog, 100) != NULL;
        if (!embedded)
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
            if (!embedded)
                CreateWindowA("STATIC", label, WS_CHILD | WS_VISIBLE, x, y + 3, 22, 20, dialog, NULL,
                              GetModuleHandle(NULL), NULL);
            HWND box = GetDlgItem(dialog, 100 + i);
            if (!embedded)
                box =
                    CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                  x + 24, y, 132, 160, dialog, (HMENU)(INT_PTR)(100 + i), GetModuleHandle(NULL), NULL);
            for (int choice = CONFIG_DRIVE_WINDOWS; choice <= CONFIG_DRIVE_ROMAW; ++choice)
                SendMessageA(box, CB_ADDSTRING, 0, (LPARAM)config_drive_format_name(choice));
            SendMessage(box, CB_SETCURSEL, format ? format - CONFIG_DRIVE_WINDOWS : 0, 0);
            EnableWindow(box, !!((available & (1u << i)) || declared));
        }
        if (!embedded)
            CreateWindowA("BUTTON", "Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 318, 454, 82, 26,
                          dialog, (HMENU)IDOK, GetModuleHandle(NULL), NULL);
        if (!embedded)
            CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 412, 454, 82, 26, dialog,
                          (HMENU)IDCANCEL, GetModuleHandle(NULL), NULL);
        if (!embedded)
            CreateWindowA("STATIC", "Mac-compatible folders (mounted at next start):", WS_CHILD | WS_VISIBLE, 18, 310,
                          478, 22, dialog, NULL, GetModuleHandle(NULL), NULL);
        HWND folders = GetDlgItem(dialog, 200);
        if (!embedded)
            folders = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
                                          LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
                                      18, 336, 376, 100, dialog, (HMENU)200, GetModuleHandle(NULL), NULL);
        for (size_t i = 0; i < config_drive_directory_count(); ++i)
            drive_folder_add(folders, config_drive_directory(i));
        SendMessage(folders, LB_SETHORIZONTALEXTENT, 1600, 0);
        if (!embedded)
            CreateWindowA("BUTTON", "Add folder...", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 404, 336, 100, 26, dialog,
                          (HMENU)201, GetModuleHandle(NULL), NULL);
        if (!embedded)
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
            if (!save_drive_preferences(dialog))
                return TRUE;
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

static int drive_preferences(HWND owner, const char* path)
{
    struct {
        DLGTEMPLATE dialog;
        WORD menu, window_class;
        WCHAR title[22];
    } layout = {{WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 0, 0, 20, 20, 270, 260}, 0, 0, L"Drive Formats"};

    INT_PTR result =
        DialogBoxIndirectParamW(GetModuleHandle(NULL), &layout.dialog, owner, drive_preferences_proc, (LPARAM)path);
    return result == IDOK;
}

int ROMlib_win_drive_preferences(const char* path)
{
    int result = drive_preferences(cygwin_sdlwindow(), path);
    sdl_preferences_input_reset();
    return result;
}

enum { NATIVE_PREF_DRIVES = 5, NATIVE_PREF_APPLICATION = 6, NATIVE_PREF_PAGE_COUNT = 7 };

typedef struct {
    ROMlib_extra_preferences edited;
    const ROMlib_extra_preferences* defaults;
    const char* filename;
    HWND pages[NATIVE_PREF_PAGE_COUNT];
    const char* section;
    unsigned page_count;
    int drive_initialized, drive_dirty;
    char version_text[3][64];
} native_preferences;

static int field_page(unsigned field)
{
    return field == 10 || field == 11 ? 2 : field == 12 || field == 13 || field == 14 ? 0 : 1;
}

/* Keep model indices stable while controls live in separate editable page resources. */
static HWND preferences_container(HWND dialog, int id)
{
    native_preferences* state = (native_preferences*)GetWindowLongPtr(dialog, DWLP_USER);
    if (GetDlgItem(dialog, id))
        return dialog;
    for (unsigned i = 0; i < state->page_count; ++i)
        if (GetDlgItem(state->pages[i], id))
            return state->pages[i];
    return dialog;
}

static void show_page(HWND dialog, int page)
{
    native_preferences* state = (native_preferences*)GetWindowLongPtr(dialog, DWLP_USER);
    if (page == NATIVE_PREF_DRIVES && state->filename && !state->drive_initialized) {
        drive_preferences_proc(state->pages[page], WM_INITDIALOG, 0, (LPARAM)state->filename);
        state->drive_initialized = 1;
    }
    TabCtrl_SetCurSel(GetDlgItem(dialog, IDC_PREF_TABS), page);
    for (unsigned i = 0; i < state->page_count; ++i)
        ShowWindow(state->pages[i], (int)i == page ? SW_SHOW : SW_HIDE);
}

static INT_PTR CALLBACK preferences_page_proc(HWND page, UINT message, WPARAM wparam, LPARAM lparam)
{
    HWND parent = GetParent(page);
    native_preferences* state = (native_preferences*)GetWindowLongPtr(parent, DWLP_USER);
    if (message == WM_COMMAND && state && page == state->pages[NATIVE_PREF_DRIVES]) {
        if (LOWORD(wparam) == 201 || LOWORD(wparam) == 202) {
            drive_preferences_proc(page, message, wparam, lparam);
            state->drive_dirty = 1;
            return TRUE;
        }
        if (LOWORD(wparam) >= 100 && LOWORD(wparam) < 126 && HIWORD(wparam) == CBN_SELCHANGE) {
            state->drive_dirty = 1;
            return TRUE;
        }
    }
    if (message == WM_COMMAND || message == WM_HSCROLL)
        return SendMessageW(parent, message, wparam, lparam);
    return FALSE;
}

static INT_PTR show_preferences(HWND owner, native_preferences* state);

static int add_application_profile(const char* section, void* context)
{
    HWND list = (HWND)context;
    if (SendMessageA(list, LB_FINDSTRINGEXACT, -1, (LPARAM)section) != LB_ERR)
        return 1;
    if (SendMessage(list, LB_GETCOUNT, 0, 0) >= 4096)
        return 0;
    LRESULT added = SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)section);
    return added != LB_ERR && added != LB_ERRSPACE;
}

static void initialize_choice(HWND page, int id, const char* const* labels, const int* values, unsigned count,
                              const char* text)
{
    char* end;
    long current = strtol(text, &end, 0);
    for (unsigned i = 0; i < count; ++i) {
        LRESULT index = SendDlgItemMessageA(page, id, CB_ADDSTRING, 0, (LPARAM)labels[i]);
        SendDlgItemMessage(page, id, CB_SETITEMDATA, index, values ? values[i] : (int)i);
        if (*text && !*end && current == (values ? values[i] : (int)i))
            SendDlgItemMessage(page, id, CB_SETCURSEL, index, 0);
    }
    if (SendDlgItemMessage(page, id, CB_GETCURSEL, 0, 0) == CB_ERR) {
        if ((GetWindowLongPtr(GetDlgItem(page, id), GWL_STYLE) & 3) == CBS_DROPDOWNLIST) {
            char saved[288];
            snprintf(saved, sizeof saved, "Saved value: %.255s", text);
            LRESULT index = SendDlgItemMessageA(page, id, CB_ADDSTRING, 0, (LPARAM)saved);
            SendDlgItemMessage(page, id, CB_SETITEMDATA, index, LONG_MIN);
            SendDlgItemMessage(page, id, CB_SETCURSEL, index, 0);
        }
        else
            SetDlgItemTextA(page, id, text);
    }
}

static void initialize_version(HWND dialog, native_preferences* state)
{
    const int controls[] = {IDC_PREF_SYSTEM_VERSION, IDC_PREF_VERSION_MINOR, IDC_PREF_VERSION_PATCH};
    const char* part = state->edited.fields[12];
    HWND page = state->pages[0];
    for (unsigned i = 0; i < 3; ++i) {
        const char* dot = strchr(part, '.');
        size_t length = dot ? (size_t)(dot - part) : strlen(part);
        snprintf(state->version_text[i], sizeof state->version_text[i], "%.*s", (int)MIN(length, 63), part);
        if (!*state->version_text[i])
            strcpy(state->version_text[i], "0");
        SendDlgItemMessage(page, controls[i], EM_SETLIMITTEXT, 2, 0);
        SetDlgItemTextA(page, controls[i], state->version_text[i]);
        part = dot ? dot + 1 : "0";
    }
    (void)dialog;
}

static void collect_fields(HWND dialog, native_preferences* state)
{
    const int version_controls[] = {IDC_PREF_SYSTEM_VERSION, IDC_PREF_VERSION_MINOR, IDC_PREF_VERSION_PATCH};
    char version[3][64];
    int version_changed = 0;
    for (unsigned i = 0; i < 3; ++i) {
        GetDlgItemTextA(state->pages[0], version_controls[i], version[i], sizeof version[i]);
        version_changed |= strcmp(version[i], state->version_text[i]) != 0;
    }
    if (version_changed)
        snprintf(state->edited.fields[12], 256, "%s.%s.%s", version[0], version[1], version[2]);
    for (unsigned i = 0; i < 19; ++i) {
        if (i == 12 || !ROMlib_preferences_field_label(i))
            continue;
        HWND page = preferences_container(dialog, 200 + i);
        LRESULT selected =
            (i == 3 || i == 16 || i == 17) ? SendDlgItemMessage(page, 200 + i, CB_GETCURSEL, 0, 0) : CB_ERR;
        if (selected != CB_ERR) {
            LRESULT value = SendDlgItemMessage(page, 200 + i, CB_GETITEMDATA, selected, 0);
            if (value != LONG_MIN)
                snprintf(state->edited.fields[i], 256, "%lld", (long long)value);
        }
        else
            GetDlgItemTextA(page, 200 + i, state->edited.fields[i], 256);
    }
}

static INT_PTR CALLBACK preferences_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    native_preferences* state = (native_preferences*)GetWindowLongPtr(dialog, DWLP_USER);
    unsigned i;
    switch (message) {
    case WM_INITDIALOG: {
        static const char* pages[] = {
            "General", "Display", "Memory", "Compatibility", "Advanced", "Drive formats", "Application settings"};
        static const int resources[] = {IDD_PREF_GENERAL,       IDD_PREF_DISPLAY,  IDD_PREF_MEMORY,
                                        IDD_PREF_COMPATIBILITY, IDD_PREF_ADVANCED, IDD_PREF_DRIVES,
                                        IDD_PREF_APPLICATION};
        RECT owner, bounds, work, area;
        state = (native_preferences*)lparam;
        SetWindowLongPtr(dialog, DWLP_USER, (LONG_PTR)state);
        HWND tabs = GetDlgItem(dialog, IDC_PREF_TABS);
        for (i = 0; i < state->page_count; ++i) {
            TCITEMA item = {0};
            item.mask = TCIF_TEXT;
            item.pszText = (char*)pages[i];
            if (SendMessageA(tabs, TCM_INSERTITEMA, i, (LPARAM)&item) == -1)
                goto failed;
        }
        GetClientRect(tabs, &area);
        TabCtrl_AdjustRect(tabs, FALSE, &area);
        MapWindowPoints(tabs, dialog, (POINT*)&area, 2);
        for (i = 0; i < state->page_count; ++i) {
            state->pages[i] = CreateDialogParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(resources[i]), dialog,
                                                 preferences_page_proc, 0);
            if (!state->pages[i])
                goto failed;
            if (i == NATIVE_PREF_APPLICATION || i == NATIVE_PREF_DRIVES) {
                HWND list = GetDlgItem(state->pages[i], i == NATIVE_PREF_APPLICATION ? IDC_PREF_APP_LIST : 200);
                /* Notifications are required behavior even when the designer clears the resource style. */
                SetWindowLongPtr(list, GWL_STYLE, GetWindowLongPtr(list, GWL_STYLE) | LBS_NOTIFY);
            }
            SetWindowPos(state->pages[i], HWND_TOP, area.left, area.top, area.right - area.left, area.bottom - area.top,
                         SWP_NOACTIVATE);
        }
        if (state->page_count > NATIVE_PREF_DRIVES && !state->filename)
            EnableWindow(state->pages[NATIVE_PREF_DRIVES], FALSE);
        for (i = 0; i < 19; ++i) {
            if (!ROMlib_preferences_field_label(i))
                continue;
            HWND page = preferences_container(dialog, 200 + i);
            if (!GetDlgItem(page, 200 + i))
                goto failed;
            if (i == 12)
                continue;
            if (i == 3) {
                static const char* const labels[] = {"None", "Command", "Shift", "Option", "Control"};
                static const int values[] = {0, 256, 512, 2048, 4096};
                initialize_choice(page, 203, labels, values, 5, state->edited.fields[i]);
            }
            else if (i == 16) {
                static const char* const labels[] = {"Normal (event processing)", "At end of Toolbox trap",
                                                     "Often (animation)"};
                initialize_choice(page, 216, labels, NULL, 3, state->edited.fields[i]);
            }
            else if (i == 17) {
                static const char* const labels[] = {"1 bit (monochrome)",  "2 bits (4 colours)",
                                                     "4 bits (16 colours)", "8 bits (256 colours)",
                                                     "16 bits (thousands)", "32 bits (millions)"};
                static const int values[] = {1, 2, 4, 8, 16, 32};
                initialize_choice(page, 217, labels, values, 6, state->edited.fields[i]);
            }
            else {
                if (i == 10 || i == 11 || i == 18) {
                    static const char* const memory[] = {"0", "4m", "8m", "16m", "32m", "64m", "128m", "256m"};
                    static const char* const scales[] = {"1x", "1.5x", "2x", "4x", "8x", "Custom"};
                    const char* const* choices = i == 18 ? scales : memory;
                    unsigned count = i == 18 ? 6 : 8;
                    for (unsigned j = 0; j < count; ++j)
                        SendDlgItemMessageA(page, 200 + i, CB_ADDSTRING, 0, (LPARAM)choices[j]);
                    SendDlgItemMessage(page, 200 + i, CB_LIMITTEXT, 255, 0);
                }
                else
                    SendDlgItemMessage(page, 200 + i, EM_SETLIMITTEXT, 255, 0);
                SetDlgItemTextA(page, 200 + i, state->edited.fields[i]);
            }
        }
        initialize_version(dialog, state);
        SendDlgItemMessage(state->pages[0], IDC_PREF_REFRESH_SLIDER, TBM_SETRANGE, TRUE, MAKELPARAM(0, 60));
        SendDlgItemMessage(state->pages[0], IDC_PREF_REFRESH_SLIDER, TBM_SETPOS, TRUE,
                           MIN(60, MAX(0, atoi(state->edited.fields[13]))));
        for (i = 0; ROMlib_preferences_check_label(i); ++i) {
            HWND page = preferences_container(dialog, 300 + i);
            if (!GetDlgItem(page, 300 + i))
                goto failed;
            SendDlgItemMessage(page, 300 + i, BM_SETCHECK, ROMlib_preferences_check(&state->edited, i), 0);
        }
        SendDlgItemMessage(state->pages[4], IDC_PREF_CPU_PPC, BM_SETCHECK,
                           ROMlib_preferences_check(&state->edited, 16) ? BST_UNCHECKED : BST_CHECKED, 0);
        if (state->page_count > NATIVE_PREF_APPLICATION) {
            HWND application = state->pages[NATIVE_PREF_APPLICATION];
            HWND list = GetDlgItem(application, IDC_PREF_APP_LIST);
            if (state->filename)
                config_prefs_visit_applications(state->filename, add_application_profile, list);
            SendMessage(list, LB_SETCURSEL, 0, 0);
            EnableWindow(GetDlgItem(application, IDC_PREF_APP_EDIT), SendMessage(list, LB_GETCOUNT, 0, 0) > 0);
            char current[1024];
            snprintf(current, sizeof current, "%s\r\nUse the other tabs to edit these settings.",
                     state->defaults ? "Finder: defaults for subsequent application launches" : state->section);
            SetDlgItemTextA(application, IDC_PREF_APP_CURRENT_TEXT, current);
        }
        if (state->defaults) {
            DestroyWindow(GetDlgItem(dialog, IDOK));
            HWND save = GetDlgItem(dialog, IDC_PREF_SAVE);
            SetWindowLongPtr(save, GWLP_ID, IDOK);
            SendMessage(save, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
            SendMessage(dialog, DM_SETDEFID, IDOK, 0);
            char description[1024];
            snprintf(description, sizeof description, "Save updates [%s] for subsequent application launches.",
                     state->section);
            SetDlgItemTextA(dialog, IDC_PREF_DESCRIPTION, description);
        }
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
    case WM_NOTIFY:
        if (((NMHDR*)lparam)->idFrom == IDC_PREF_TABS && ((NMHDR*)lparam)->code == TCN_SELCHANGE) {
            int page = TabCtrl_GetCurSel(GetDlgItem(dialog, IDC_PREF_TABS));
            if (page >= 0 && page < (int)state->page_count)
                show_page(dialog, page);
            return TRUE;
        }
        break;
    case WM_HSCROLL:
        if ((HWND)lparam == GetDlgItem(state->pages[0], IDC_PREF_REFRESH_SLIDER)) {
            char text[16];
            snprintf(text, sizeof text, "%ld", (long)SendMessage((HWND)lparam, TBM_GETPOS, 0, 0));
            SetDlgItemTextA(state->pages[0], IDC_PREF_REFRESH, text);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_PREF_REFRESH && HIWORD(wparam) == EN_CHANGE) {
            char text[32];
            GetDlgItemTextA(state->pages[0], IDC_PREF_REFRESH, text, sizeof text);
            SendDlgItemMessage(state->pages[0], IDC_PREF_REFRESH_SLIDER, TBM_SETPOS, TRUE, MIN(60, MAX(0, atoi(text))));
            return TRUE;
        }
        if (LOWORD(wparam) == IDC_PREF_APP_CURRENT) {
            show_page(dialog, 0);
            SetFocus(GetDlgItem(state->pages[0], IDC_PREF_SYSTEM_VERSION));
            return TRUE;
        }
        if (LOWORD(wparam) == IDC_PREF_APP_EDIT ||
            (LOWORD(wparam) == IDC_PREF_APP_LIST && HIWORD(wparam) == LBN_DBLCLK)) {
            HWND list = GetDlgItem(state->pages[NATIVE_PREF_APPLICATION], IDC_PREF_APP_LIST);
            LRESULT selected = SendMessage(list, LB_GETCURSEL, 0, 0);
            if (selected == LB_ERR || SendMessage(list, LB_GETTEXTLEN, selected, 0) >= CONFIG_PREFS_SECTION_SIZE)
                return TRUE;
            char section[CONFIG_PREFS_SECTION_SIZE];
            SendMessageA(list, LB_GETTEXT, selected, (LPARAM)section);
            if (!strcmp(section, state->section)) {
                show_page(dialog, 0);
                return TRUE;
            }
            ROMlib_extra_preferences original;
            ROMlib_profile_preferences_init(&original, state->filename, section);
            native_preferences profile = {.edited = original,
                                          .defaults = &original,
                                          .filename = state->filename,
                                          .section = section,
                                          .page_count = ROMAW_PREF_PAGE_COUNT};
            show_preferences(dialog, &profile);
            return TRUE;
        }
        if (LOWORD(wparam) == 218 && HIWORD(wparam) == CBN_SELCHANGE &&
            SendDlgItemMessage(preferences_container(dialog, 218), 218, CB_GETCURSEL, 0, 0) == 5) {
            SetDlgItemTextA(preferences_container(dialog, 218), 218, "");
            SetFocus(GetDlgItem(preferences_container(dialog, 218), 218));
            return TRUE;
        }
        if (LOWORD(wparam) == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        if (LOWORD(wparam) == IDOK || LOWORD(wparam) == 3) {
            unsigned bad = 0;
            collect_fields(dialog, state);
            if (!ROMlib_extra_preferences_valid(&state->edited, &bad)) {
                show_page(dialog, field_page(bad));
                MessageBoxA(dialog, "Invalid setting. Check the value and its supported range.",
                            ROMlib_preferences_field_label(bad), MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(preferences_container(dialog, 200 + bad), 200 + bad));
                SendDlgItemMessage(preferences_container(dialog, 200 + bad), 200 + bad, EM_SETSEL, 0, -1);
                return TRUE;
            }
            for (i = 0; ROMlib_preferences_check_label(i); ++i)
                if (!!SendDlgItemMessage(preferences_container(dialog, 300 + i), 300 + i, BM_GETCHECK, 0, 0) !=
                    ROMlib_preferences_check(&state->edited, i))
                    ROMlib_preferences_toggle(&state->edited, i);
            if (state->drive_dirty && !save_drive_preferences(state->pages[NATIVE_PREF_DRIVES]))
                return TRUE;
            state->drive_dirty = 0;
            if (state->defaults) {
                if (ROMlib_profile_preferences_save(&state->edited, state->defaults, state->filename, state->section))
                    EndDialog(dialog, IDOK);
                else
                    MessageBoxA(dialog, "Unable to write ROMAW.prefs. Check the folder and file permissions.",
                                "ROMAW Preferences", MB_OK | MB_ICONERROR);
                return TRUE;
            }
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

static INT_PTR show_preferences(HWND owner, native_preferences* state)
{
    INITCOMMONCONTROLSEX controls = {sizeof controls, ICC_TAB_CLASSES | ICC_BAR_CLASSES};
    if (!InitCommonControlsEx(&controls))
        return -1;
    return DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_PREFERENCES), owner, preferences_proc,
                           (LPARAM)state);
}

int ROMlib_win_default_preferences(void* owner, const char* filename)
{
    ROMlib_extra_preferences defaults;
    ROMlib_default_preferences_init(&defaults, filename);
    native_preferences state = {.edited = defaults,
                                .defaults = &defaults,
                                .filename = filename,
                                .section = "Defaults",
                                .page_count = NATIVE_PREF_PAGE_COUNT};
    return show_preferences(owner, &state) == IDOK;
}

/* A negative result selects the portable SDL fallback. */
int ROMlib_win_preferences(ROMlib_extra_preferences* prefs, const char* filename)
{
    native_preferences state = {
        .edited = *prefs, .filename = filename, .section = ROMlib_configsection, .page_count = NATIVE_PREF_PAGE_COUNT};
    INT_PTR result = show_preferences(cygwin_sdlwindow(), &state);
    if (result == IDOK)
        *prefs = state.edited;
    sdl_preferences_input_reset();
    ROMlib_preferences_redraw();
    return result < 0 ? -1 : result == IDOK;
}
