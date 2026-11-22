/*+===================================================================
  File:      win_stdfile.c

  Summary:   Windows common file dialogs. No Macintosh dialog fallback.

  Classes:   ImageBackupProgress, ImageBackupProgressVtable.

  Functions: host_dialog_error, host_file_dialog, host_file_dialog_filtered, host_image_dialog,
             host_image_confirm_incomplete

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Windows common file dialogs. No Macintosh dialog fallback. */
#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES
#include "rsys/common.h"
#include "rsys/hostdialog.h"
#include "rsys/float_fcw.h"
#include "rsys/user_profile.h"
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shlobj.h>
#include "win_win.h"

typedef struct {
    host_file_filter filter;
    void* context;
} file_filter_context;

static UINT_PTR CALLBACK file_filter_hook(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message != WM_NOTIFY || !lp)
        return 0;
    OFNOTIFYEXW* notice = (OFNOTIFYEXW*)lp;
    if (notice->hdr.code != CDN_INCLUDEITEM)
        return 0;
    file_filter_context* context = (file_filter_context*)notice->lpOFN->lCustData;
    IShellFolder* folder = notice->psf;
    PCUITEMID_CHILD child = notice->pidl;
    SFGAOF attributes = SFGAO_FOLDER;
    STRRET name;
    char* path = NULL;
    int included = 0;
    if (!context || !context->filter || !folder || !child)
        return 1;
    if (FAILED(folder->lpVtbl->GetAttributesOf(folder, 1, &child, &attributes)))
        return 0;
    if (attributes & SFGAO_FOLDER)
        return 1;
    if (FAILED(folder->lpVtbl->GetDisplayNameOf(folder, child, SHGDN_FORPARSING, &name)))
        return 0;
    if (name.uType == STRRET_WSTR) {
        BOOL substituted = FALSE;
        BOOL* used_default = GetACP() == CP_UTF8 ? NULL : &substituted;
        DWORD flags = GetACP() == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
        int size = WideCharToMultiByte(CP_ACP, flags, name.pOleStr, -1, NULL, 0, NULL, used_default);
        if (size > 0 && !substituted) {
            path = malloc(size);
            if (path && (!WideCharToMultiByte(CP_ACP, flags, name.pOleStr, -1, path, size, NULL, used_default) ||
                         substituted)) {
                free(path);
                path = NULL;
            }
        }
        CoTaskMemFree(name.pOleStr);
    }
    else if (name.uType == STRRET_CSTR)
        path = strdup(name.cStr);
    else if (name.uType == STRRET_OFFSET) {
        UINT size = ILGetSize(child);
        if (name.uOffset < size && memchr((char*)child + name.uOffset, 0, size - name.uOffset))
            path = strdup((char*)child + name.uOffset);
    }
    if (path) {
        uint32 saved_fpu = ROMlib_get_fcw_fsw();
        included = context->filter(path, context->context);
        ROMlib_set_fcw_fsw(saved_fpu);
    }
    free(path);
    return included != 0;
}

void host_dialog_error(const char* message)
{
    MessageBoxA(cygwin_sdlwindow(), message, "ROMAW", MB_OK | MB_ICONERROR);
}

static WCHAR* to_wide(const char* text, UINT codepage)
{
    int size;
    WCHAR* wide;
    if (!text)
        return NULL;
    size = MultiByteToWideChar(codepage, 0, text, -1, NULL, 0);
    wide = size > 0 ? malloc((size_t)size * sizeof *wide) : NULL;
    if (wide && !MultiByteToWideChar(codepage, 0, text, -1, wide, size)) {
        free(wide);
        return NULL;
    }
    return wide;
}

static int file_dialog(int save, const char* title, const char* directory, const char* name, char** path, int images,
                       host_file_filter filter, void* filter_data)
{
    file_filter_context filter_context = {filter, filter_data};
    uint32 saved_fpu = ROMlib_get_fcw_fsw();
    OPENFILENAMEW dialog = {0};
    WCHAR* filename = calloc(32768, sizeof *filename);
    WCHAR *wide_title = to_wide(title, 10000), *wide_directory = to_wide(directory, CP_ACP);
    WCHAR* wide_name = to_wide(name, CP_ACP);
    char remembered[32768];
    BOOL accepted, substituted = FALSE;
    BOOL* used_default = GetACP() == CP_UTF8 ? NULL : &substituted;
    DWORD conversion_flags = GetACP() == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
    int size, result = -1;
    *path = NULL;
    if (!filename || (title && !wide_title) || (directory && !wide_directory) || (name && !wide_name))
        goto done;
    if ((!wide_directory || !wide_directory[0]) && romaw_dialog_directory(remembered, sizeof remembered)) {
        free(wide_directory);
        wide_directory = to_wide(remembered, CP_UTF8);
    }
    /* Supply the path in the filename buffer with InitialDir NULL. Windows'
     legacy InitialDir heuristic can otherwise ignore repeated app requests. */
    if (wide_directory && wide_directory[0]) {
        DWORD length = GetFullPathNameW(wide_directory, 32768, filename, NULL);
        if (!length || length >= 32767)
            goto done;
        for (WCHAR* p = filename; *p; ++p)
            if (*p == L'/')
                *p = L'\\';
        if (filename[length - 1] != L'\\')
            filename[length++] = L'\\';
        filename[length] = 0;
    }
    /* With OFN_FILEMUSTEXIST, a bare directory ending in a separator can be
     rejected as FNERR_INVALIDFILENAME before the panel opens. A wildcard
     navigates to the requested directory without proposing a missing file. */
    if (!save && filename[0]) {
        if (wcslen(filename) + 3 >= 32768)
            goto done;
        wcscat(filename, L"*.*");
    }
    if (save && wide_name) {
        if (wcslen(filename) + wcslen(wide_name) >= 32768)
            goto done;
        wcscat(filename, wide_name);
    }
    /* An unnamed document has no filename component. A directory ending in a
     separator is invalid in Save's filename field, just as it is in Open's.
     Leave that field empty and supply the requested directory separately. */
    if (save && (!wide_name || !wide_name[0])) {
        dialog.lpstrInitialDir = wide_directory;
        filename[0] = 0;
    }
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = cygwin_sdlwindow();
    dialog.lpstrFile = filename;
    dialog.nMaxFile = 32768;
    dialog.lpstrTitle = wide_title;
    dialog.lpstrFilter =
        images
            ? L"Macintosh disk images\0*.dmg;*.hfsplus;*.hfsx;*.hfv;*.hda;*.hfs;*.dsk;*.image;*.img;*.ima;*.dc42;*.iso;*.toast;*.cdr;*.smi\0All files\0*.*\0\0"
        : filter ? L"Macintosh files\0*.*\0\0"
                 : L"All files\0*.*\0\0";
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST |
                   (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (images) {
        dialog.Flags &= ~OFN_HIDEREADONLY;
        dialog.Flags |= OFN_READONLY;
    }
    if (filter) {
        dialog.Flags |= OFN_ENABLEHOOK | OFN_ENABLEINCLUDENOTIFY;
        dialog.lpfnHook = file_filter_hook;
        dialog.lCustData = (LPARAM)&filter_context;
    }
    accepted = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (!accepted) {
        result = CommDlgExtendedError() ? -1 : 0;
        goto done;
    }
    /* The existing Windows File Manager uses the ANSI filesystem APIs.
     Reject unrepresentable paths instead of silently selecting another file. */
    size = WideCharToMultiByte(CP_ACP, conversion_flags, filename, -1, NULL, 0, NULL, used_default);
    if (size <= 0 || substituted)
        goto done;
    *path = malloc(size);
    if (!*path || !WideCharToMultiByte(CP_ACP, conversion_flags, filename, -1, *path, size, NULL, used_default) ||
        substituted) {
        free(*path);
        *path = NULL;
        goto done;
    }
    result = images && !(dialog.Flags & OFN_READONLY) ? 2 : 1;
    WCHAR* separator = wcsrchr(filename, L'\\');
    if (separator) {
        separator[1] = 0;
        int bytes = WideCharToMultiByte(CP_UTF8, 0, filename, -1, remembered, sizeof remembered, NULL, NULL);
        if (bytes > 0)
            romaw_dialog_remember(remembered);
    }
done:
    free(filename);
    free(wide_title);
    free(wide_directory);
    free(wide_name);
    ROMlib_set_fcw_fsw(saved_fpu);
    return result;
}

int host_file_dialog(int save, const char* title, const char* directory, const char* name, char** path)
{
    return file_dialog(save, title, directory, name, path, 0, NULL, NULL);
}

int host_file_dialog_filtered(int save, const char* title, const char* directory, const char* name, char** path,
                              host_file_filter filter, void* context)
{
    return file_dialog(save, title, directory, name, path, 0, filter, context);
}

int host_image_dialog(char** path)
{
    return file_dialog(0, "Mount Disk Image", NULL, NULL, path, 1, NULL, NULL);
}

/* MinGW's C IProgressDialog declaration omits inherited IUnknown slots.
   Declare the complete COM ABI so progress methods use their real slots. */
typedef struct ImageBackupProgress ImageBackupProgress;

typedef struct {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(ImageBackupProgress*, REFIID, void**);
    ULONG(STDMETHODCALLTYPE* AddRef)(ImageBackupProgress*);
    ULONG(STDMETHODCALLTYPE* Release)(ImageBackupProgress*);
    HRESULT(STDMETHODCALLTYPE* StartProgressDialog)(ImageBackupProgress*, HWND, IUnknown*, DWORD, const void*);
    HRESULT(STDMETHODCALLTYPE* StopProgressDialog)(ImageBackupProgress*);
    HRESULT(STDMETHODCALLTYPE* SetTitle)(ImageBackupProgress*, const WCHAR*);
    HRESULT(STDMETHODCALLTYPE* SetAnimation)(ImageBackupProgress*, HINSTANCE, UINT);
    BOOL(STDMETHODCALLTYPE* HasUserCancelled)(ImageBackupProgress*);
    HRESULT(STDMETHODCALLTYPE* SetProgress)(ImageBackupProgress*, DWORD, DWORD);
    HRESULT(STDMETHODCALLTYPE* SetProgress64)(ImageBackupProgress*, ULONGLONG, ULONGLONG);
    HRESULT(STDMETHODCALLTYPE* SetLine)(ImageBackupProgress*, DWORD, const WCHAR*, BOOL, const void*);
} ImageBackupProgressVtable;

struct ImageBackupProgress {
    const ImageBackupProgressVtable* lpVtbl;
};

static DWORD CALLBACK image_backup_progress(LARGE_INTEGER total, LARGE_INTEGER transferred, LARGE_INTEGER stream_size,
                                            LARGE_INTEGER stream_transferred, DWORD stream, DWORD reason, HANDLE source,
                                            HANDLE destination, void* opaque)
{
    ImageBackupProgress* progress = opaque;
    (void)stream_size;
    (void)stream_transferred;
    (void)stream;
    (void)reason;
    (void)source;
    (void)destination;
    if (progress) {
        progress->lpVtbl->SetProgress64(progress, transferred.QuadPart, total.QuadPart);
        if (progress->lpVtbl->HasUserCancelled(progress))
            return PROGRESS_CANCEL;
    }
    return PROGRESS_CONTINUE;
}

static void backup_image(const WCHAR* source)
{
    WCHAR* destination = calloc(32768, sizeof *destination);
    if (!destination)
        return;
    size_t length = wcslen(source);
    if (length + 8 >= 32768) {
        free(destination);
        return;
    }
    wcscpy(destination, source);
    wcscat(destination, L".backup");
    OPENFILENAMEW dialog = {0};
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = cygwin_sdlwindow();
    dialog.lpstrTitle = L"Back up disk image - choose a new file";
    dialog.lpstrFile = destination;
    dialog.nMaxFile = 32768;
    dialog.lpstrFilter = L"Disk image backup\0*.*\0\0";
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    if (GetSaveFileNameW(&dialog)) {
        ImageBackupProgress* progress = NULL;
        HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        CoCreateInstance(&CLSID_ProgressDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IProgressDialog, (void**)&progress);
        if (progress) {
            progress->lpVtbl->SetTitle(progress, L"Backing up disk image");
            progress->lpVtbl->SetLine(progress, 1, destination, TRUE, NULL);
            progress->lpVtbl->StartProgressDialog(progress, cygwin_sdlwindow(), NULL, PROGDLG_MODAL | PROGDLG_AUTOTIME,
                                                  NULL);
        }
        BOOL cancelled = FALSE;
        BOOL copied =
            CopyFileExW(source, destination, image_backup_progress, progress, &cancelled, COPY_FILE_FAIL_IF_EXISTS);
        DWORD error = copied ? ERROR_SUCCESS : GetLastError();
        if (progress) {
            progress->lpVtbl->StopProgressDialog(progress);
            progress->lpVtbl->Release(progress);
        }
        if (SUCCEEDED(initialized))
            CoUninitialize();
        if (copied)
            MessageBoxW(cygwin_sdlwindow(),
                        L"The original image was copied exactly. The backup retains the same damage; "
                        L"it is a recovery copy, not a repaired image.",
                        L"Disk image backup created", MB_OK | MB_ICONINFORMATION);
        else if (error != ERROR_REQUEST_ABORTED) {
            WCHAR message[256];
            swprintf(message, 256,
                     error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS
                         ? L"That backup file already exists. Choose a new filename."
                         : L"The image could not be backed up (Windows error %lu).",
                     (unsigned long)error);
            MessageBoxW(cygwin_sdlwindow(), message, L"Disk image backup", MB_OK | MB_ICONERROR);
        }
    }
    free(destination);
}

int host_image_confirm_incomplete(const char* path, const char* explanation)
{
    WCHAR* wide_path = to_wide(path, CP_ACP);
    WCHAR* wide_explanation = to_wide(explanation, CP_UTF8);
    int result = 0;
    uint32 saved_fpu = ROMlib_get_fcw_fsw();
    if (wide_path && wide_explanation) {
        const TASKDIALOG_BUTTON buttons[] = {
            {100, L"Open available contents read-only"},
            {101, L"Back up image..."},
            {102, L"Cancel and replace image"},
        };
        TASKDIALOGCONFIG dialog = {0};
        dialog.cbSize = sizeof dialog;
        dialog.hwndParent = cygwin_sdlwindow();
        dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
        dialog.pszWindowTitle = L"Incomplete disk image";
        dialog.pszMainIcon = TD_WARNING_ICON;
        dialog.pszMainInstruction = L"A complete replacement is needed to fix this image";
        dialog.pszContent = wide_explanation;
        dialog.pszExpandedInformation = wide_path;
        dialog.cButtons = sizeof buttons / sizeof buttons[0];
        dialog.pButtons = buttons;
        dialog.nDefaultButton = 102;
        for (;;) {
            int selected = 102;
            if (FAILED(TaskDialogIndirect(&dialog, &selected, NULL, NULL)))
                break;
            if (selected == 101) {
                backup_image(wide_path);
                continue;
            }
            result = selected == 100;
            break;
        }
    }
    free(wide_path);
    free(wide_explanation);
    ROMlib_set_fcw_fsw(saved_fpu);
    return result;
}
