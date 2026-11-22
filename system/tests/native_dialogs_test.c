/*+===================================================================
  File:      native_dialogs_test.c

  Summary:   Intercept common dialogs; one real Save panel cancels during initialization. No files are saved and no
             print jobs are submitted.

  Classes:   None.

  Functions: ROMlib_get_fcw_fsw, ROMlib_set_fcw_fsw, cygwin_sdlwindow, romaw_dialog_directory,
             romaw_dialog_remember, main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Intercept common dialogs; one real Save panel cancels during initialization.
   No files are saved and no print jobs are submitted. */
#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES
#include "rsys/common.h"
#include <windows.h>
#include <commdlg.h>
#include <assert.h>
static int response, deleted, dialog_calls;
static DWORD extended_error;
static char remembered_directory[32768];
static const WCHAR* expected_initial = L"D:\\Folder\\*.*";
static int unnamed_save;
static int real_save_check, save_panel_initialized;
static BOOL(WINAPI* system_save)(LPOPENFILENAMEW) = GetSaveFileNameW;
static DWORD(WINAPI* system_dialog_error)(void) = CommDlgExtendedError;

static UINT_PTR CALLBACK cancel_save_panel(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_INITDIALOG) {
        save_panel_initialized = 1;
        PostMessageW(GetParent(window), WM_COMMAND, IDCANCEL, 0);
    }
    return 0;
}

int romaw_dialog_directory(char* out, size_t capacity)
{
    snprintf(out, capacity, "%s", remembered_directory);
    return out[0] != 0;
}

int romaw_dialog_remember(const char* directory)
{
    snprintf(remembered_directory, sizeof remembered_directory, "%s", directory);
    return 1;
}

static BOOL WINAPI test_print(LPPRINTDLGW pd);
static BOOL WINAPI test_page(LPPAGESETUPDLGW ps);
static BOOL WINAPI test_open(LPOPENFILENAMEW ofn);
static BOOL WINAPI test_save(LPOPENFILENAMEW ofn);

static DWORD WINAPI test_error(void)
{
    return extended_error;
}

static BOOL WINAPI test_delete(HDC dc)
{
    assert(dc == (HDC)42);
    ++deleted;
    return TRUE;
}

static int WINAPI test_caps(HDC dc, int cap)
{
    assert(dc == (HDC)42);
    switch (cap) {
    case RASTERCAPS:
        return RC_DIBTODEV;
    case LOGPIXELSX:
    case LOGPIXELSY:
        return 300;
    case PHYSICALWIDTH:
        return 2550;
    case PHYSICALHEIGHT:
        return 3300;
    case HORZRES:
        return 2400;
    case VERTRES:
        return 3150;
    }
    abort();
}

#define PrintDlgW test_print
#define PageSetupDlgW test_page
#define CommDlgExtendedError test_error
#define DeleteDC test_delete
#define GetDeviceCaps test_caps
#define GetOpenFileNameW test_open
#define GetSaveFileNameW test_save
#include "../../platform/os/cygwin32/win_print.c"
#include "../../platform/os/cygwin32/win_stdfile.c"
#undef main
static int enumeration_check, enumeration_calls, enumeration_token;
static WCHAR enumeration_file[MAX_PATH], enumeration_folder[MAX_PATH];

static int enumeration_filter(const char* path, void* context)
{
    assert(context == &enumeration_token);
    assert(strstr(path, "\\") && *path);
    ++enumeration_calls;
    return enumeration_token;
}

static UINT_PTR include_shell_item(LPOPENFILENAMEW dialog, const WCHAR* path)
{
    PIDLIST_ABSOLUTE full;
    IShellFolder* parent;
    PCUITEMID_CHILD child;
    assert(SUCCEEDED(SHParseDisplayName(path, NULL, &full, 0, NULL)));
    assert(SUCCEEDED(SHBindToParent(full, &IID_IShellFolder, (void**)&parent, &child)));
    OFNOTIFYEXW notification = {0};
    notification.hdr.code = CDN_INCLUDEITEM;
    notification.lpOFN = dialog;
    notification.psf = parent;
    notification.pidl = (void*)child;
    UINT_PTR result = dialog->lpfnHook(NULL, WM_NOTIFY, 0, (LPARAM)&notification);
    parent->lpVtbl->Release(parent);
    CoTaskMemFree(full);
    return result;
}

HWND cygwin_sdlwindow(void)
{
    return (HWND)12345;
}

uint32 ROMlib_get_fcw_fsw(void)
{
    return 0;
}

void ROMlib_set_fcw_fsw(uint32 value)
{
}

static BOOL WINAPI test_print(LPPRINTDLGW pd)
{
    ++dialog_calls;
    assert(pd->hwndOwner == (HWND)12345 && pd->nMinPage == 1);
    assert(pd->nFromPage == 2 && pd->nToPage == 9);
    assert((pd->Flags & PD_PAGENUMS) && !(pd->Flags & PD_NOPAGENUMS));
    if (!response)
        return FALSE;
    if (!pd->hDevMode)
        pd->hDevMode = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DEVMODEW));
    if (!pd->hDevNames)
        pd->hDevNames = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DEVNAMES));
    pd->hDC = (HDC)42;
    pd->nFromPage = 3;
    pd->nToPage = 8;
    return TRUE;
}

static BOOL WINAPI test_page(LPPAGESETUPDLGW ps)
{
    ++dialog_calls;
    assert(ps->hwndOwner == (HWND)12345 && (ps->Flags & PSD_MARGINS));
    if (!response)
        return FALSE;
    ps->ptPaperSize.x = 21000;
    ps->ptPaperSize.y = 29700;
    ps->rtMargin = (RECT){1270, 1270, 1270, 1270};
    return TRUE;
}

static BOOL WINAPI test_open(LPOPENFILENAMEW ofn)
{
    ++dialog_calls;
    assert(ofn->hwndOwner == (HWND)12345);
    assert((ofn->Flags & OFN_NOCHANGEDIR) && (ofn->Flags & OFN_FILEMUSTEXIST));
    assert(!ofn->lpstrInitialDir);
    assert(!wcscmp(ofn->lpstrFile, expected_initial));
    if (enumeration_check) {
        assert((ofn->Flags & (OFN_ENABLEHOOK | OFN_ENABLEINCLUDENOTIFY)) == (OFN_ENABLEHOOK | OFN_ENABLEINCLUDENOTIFY));
        assert(ofn->lpfnHook && ofn->lCustData);
        enumeration_token = 1;
        assert(include_shell_item(ofn, enumeration_file));
        enumeration_token = 0;
        assert(!include_shell_item(ofn, enumeration_file));
        assert(include_shell_item(ofn, enumeration_folder));
        assert(enumeration_calls == 2);
    }
    if (response)
        wcscpy(ofn->lpstrFile, L"D:\\Folder\\test.txt");
    return response;
}

static BOOL WINAPI test_save(LPOPENFILENAMEW ofn)
{
    ++dialog_calls;
    if (real_save_check) {
        ofn->hwndOwner = NULL;
        ofn->Flags |= OFN_ENABLEHOOK;
        ofn->lpfnHook = cancel_save_panel;
        BOOL accepted = system_save(ofn);
        assert(!accepted && !system_dialog_error());
        return accepted;
    }
    assert(ofn->Flags & OFN_OVERWRITEPROMPT);
    if (unnamed_save) {
        assert(ofn->lpstrInitialDir);
        assert(!wcscmp(ofn->lpstrInitialDir, L"D:\\Folder"));
        assert(!ofn->lpstrFile[0]);
    }
    else {
        assert(!ofn->lpstrInitialDir);
        assert(!wcscmp(ofn->lpstrFile, L"D:\\Folder\\Untitled"));
    }
    if (response)
        wcscpy(ofn->lpstrFile, L"D:\\Folder\\Untitled");
    return response;
}

int main(void)
{
    host_print_settings settings = {612, 792, 2, 9, 1, 36, 36, 36, 36};
    host_print_settings before = settings;
    char* path = NULL;
    assert(host_print_dialog(0, &settings) == 0);
    assert(!memcmp(&settings, &before, sizeof settings) && !native_job);
    extended_error = CDERR_INITIALIZATION;
    assert(host_print_dialog(1, &settings) == -1);
    assert(!memcmp(&settings, &before, sizeof settings));
    extended_error = 0;
    response = 1;
    assert(host_print_dialog(0, &settings) == 1);
    assert(settings.first == 3 && settings.last == 8 && native_job);
    assert(settings.width == 612 && settings.height == 792 && !deleted);
    HGLOBAL mode = native_devmode, names = native_devnames;
    response = 0;
    assert(!host_print_dialog(1, &settings));
    assert(native_devmode == mode && native_devnames == names && native_job);
    response = 1;
    assert(host_print_dialog(1, &settings) == 1);
    assert(settings.width == 595 && settings.height == 842 && settings.left == 36);
    assert(settings.first == 3 && settings.last == 8);
    host_print_reset();
    assert(deleted == 1 && !native_job && !native_devmode && !native_devnames);
    response = 0;
    assert(host_file_dialog(0, "Open", "D:\\Folder", "", &path) == 0 && !path);
    extended_error = CDERR_INITIALIZATION;
    assert(host_file_dialog(1, "Save As", "D:\\Folder", "Untitled", &path) == -1 && !path);
    extended_error = 0;
    response = 1;
    assert(host_file_dialog(0, "Open", "D:\\Folder", "", &path) == 1);
    assert(!strcmp(path, "D:\\Folder\\test.txt"));
    free(path);
    assert(!strcmp(remembered_directory, "D:\\Folder\\"));
    assert(host_file_dialog(0, "Open", NULL, "", &path) == 1);
    free(path);
    response = 0;
    expected_initial = L"C:\\Requested\\*.*";
    assert(!host_file_dialog(0, "Open", "C:/Requested", "", &path));
    assert(!strcmp(remembered_directory, "D:\\Folder\\"));
    response = 1;
    expected_initial = L"D:\\Folder\\*.*";
    assert(host_file_dialog(1, "Save As", "D:\\Folder", "Untitled", &path) == 1);
    assert(!strcmp(path, "D:\\Folder\\Untitled"));
    free(path);
    unnamed_save = 1;
    for (int missing_name = 0; missing_name < 2; ++missing_name) {
        assert(host_file_dialog(1, "Save As", "D:\\Folder", missing_name ? NULL : "", &path) == 1);
        assert(!strcmp(path, "D:\\Folder\\Untitled"));
        free(path);
    }
    response = 0;
    assert(!host_file_dialog(1, "Save As", "D:\\Folder", "", &path) && !path);
    {
        assert(SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)));
        assert(GetTempPathW(MAX_PATH, enumeration_folder));
        assert(GetTempFileNameW(enumeration_folder, L"exf", 0, enumeration_file));
        enumeration_check = 1;
        assert(!host_file_dialog_filtered(0, "Open", "D:\\Folder", "", &path, enumeration_filter, &enumeration_token));
        enumeration_check = 0;
        assert(DeleteFileW(enumeration_file));
        CoUninitialize();
    }
    {
        char directory[MAX_PATH];
        assert(GetTempPathA(sizeof directory, directory));
        real_save_check = 1;
        assert(!host_file_dialog(1, "Save As", directory, "", &path) && !path && save_panel_initialized);
    }
    FILE* spool = host_print_spool(&path);
    assert(spool && path);
    assert(fputs("%!PS\n", spool) >= 0 && !fclose(spool));
    assert(GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES);
    DeleteFileA(path);
    free(path);
    puts(
        "Native Windows dialogs: open/save, print/page setup, cancel, failure, settings, DC ownership and temporary spool passed");
    return 0;
}
