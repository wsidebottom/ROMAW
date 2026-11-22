/*+===================================================================
  File:      image_warning_test.c

  Summary:   Check incomplete-image choices and exact backups without displaying dialogs.

  Classes:   None.

  Functions: ROMlib_get_fcw_fsw, ROMlib_set_fcw_fsw, cygwin_sdlwindow, main.

  Origin:    Executor Windows image-warning regression fixture.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES
#include "rsys/common.h"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <assert.h>

static WCHAR destination[MAX_PATH];
static unsigned choices, messages, saves, copies;
static int scenario;

static HRESULT WINAPI choose(const TASKDIALOGCONFIG* dialog, int* selected, int* radio, BOOL* verification)
{
    (void)radio;
    (void)verification;
    assert(dialog->cButtons == 3 && dialog->nDefaultButton == 102);
    assert(wcsstr(dialog->pButtons[1].pszButtonText, L"Back up"));
    assert(wcsstr(dialog->pszContent, L"Missing bytes"));
    *selected = scenario == 0 ? 102 : choices++ == 0 ? 101 : scenario == 2 ? 102 : 100;
    return S_OK;
}

static BOOL WINAPI mock_save_file(OPENFILENAMEW* dialog)
{
    ++saves;
    if (scenario == 3)
        return FALSE;
    assert(wcslen(destination) < dialog->nMaxFile);
    wcscpy(dialog->lpstrFile, destination);
    return TRUE;
}

static int WINAPI mock_image_message(HWND owner, LPCWSTR text, LPCWSTR title, UINT flags)
{
    (void)owner;
    (void)text;
    (void)title;
    assert(flags & (MB_ICONINFORMATION | MB_ICONERROR));
    ++messages;
    return IDOK;
}

static HRESULT WINAPI no_progress(REFCLSID clsid, LPUNKNOWN outer, DWORD flags, REFIID iid, LPVOID* object)
{
    (void)clsid;
    (void)outer;
    (void)flags;
    (void)iid;
    *object = NULL;
    return E_NOINTERFACE;
}

static BOOL WINAPI copy(LPCWSTR source, LPCWSTR target, LPPROGRESS_ROUTINE progress, LPVOID context, LPBOOL cancel,
                        DWORD flags)
{
    ++copies;
    assert(flags == COPY_FILE_FAIL_IF_EXISTS);
    return CopyFileExW(source, target, progress, context, cancel, flags);
}

#define TaskDialogIndirect choose
#define GetSaveFileNameW mock_save_file
#define MessageBoxW mock_image_message
#define CoCreateInstance no_progress
#define CopyFileExW copy
#include "../../platform/os/cygwin32/win_stdfile.c"
#undef main

uint32 ROMlib_get_fcw_fsw(void)
{
    return 0;
}

void ROMlib_set_fcw_fsw(uint32 state)
{
    (void)state;
}

HWND cygwin_sdlwindow(void)
{
    return NULL;
}

int main(void)
{
    WCHAR directory[MAX_PATH], source[MAX_PATH];
    assert(GetTempPathW(MAX_PATH, directory));
    assert(GetTempFileNameW(directory, L"img", 0, source));
    assert(GetTempFileNameW(directory, L"bak", 0, destination));
    assert(DeleteFileW(destination));
    HANDLE file = CreateFileW(source, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    assert(file != INVALID_HANDLE_VALUE);
    const char original[] = "incomplete image bytes\0\xff\x81";
    DWORD done;
    assert(WriteFile(file, original, sizeof original, &done, NULL) && done == sizeof original);
    assert(CloseHandle(file));
    char path[MAX_PATH];
    assert(WideCharToMultiByte(CP_ACP, 0, source, -1, path, sizeof path, NULL, NULL));
    assert(!host_image_confirm_incomplete(path, "Missing bytes require a replacement."));
    assert(!choices && !saves && !copies && !messages);
    scenario = 1;
    assert(host_image_confirm_incomplete(path, "Missing bytes require a replacement."));
    assert(saves == 1 && copies == 1 && messages == 1);
    file = CreateFileW(destination, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    char bytes[sizeof original + 1];
    assert(file != INVALID_HANDLE_VALUE && ReadFile(file, bytes, sizeof bytes, &done, NULL));
    assert(done == sizeof original && !memcmp(bytes, original, done));
    assert(CloseHandle(file));
    choices = 0;
    scenario = 2;
    assert(!host_image_confirm_incomplete(path, "Missing bytes require a replacement."));
    assert(saves == 2 && copies == 2 && messages == 2); /* Existing backup is preserved. */
    choices = 0;
    scenario = 3;
    assert(host_image_confirm_incomplete(path, "Missing bytes require a replacement."));
    assert(saves == 3 && copies == 2 && messages == 2); /* Cancelling Save returns to choices. */
    assert(DeleteFileW(source) && DeleteFileW(destination));
    puts("PASS: incomplete-image cancel/read-only/backup choices, exact backup bytes, no overwrite and cancelled Save");
    return 0;
}
