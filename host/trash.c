/*+===================================================================
  File:      trash.c

  Summary:   Implements trash support for host operating-system support.

  Classes:   None.

  Functions: ROMlib_host_trash

  Origin:    Existing repository source in executor/src/host; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Host-only types stay separate from the Macintosh Toolbox headers. */
#include <stdlib.h>
#include <string.h>
#include "rsys/hosttrash.h"
#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <versionhelpers.h>

static wchar_t* trash_path(const char* path)
{
    int count = MultiByteToWideChar(CP_ACP, 0, path, -1, NULL, 0);
    wchar_t* wide = count > 0 ? malloc(count * sizeof *wide) : NULL;
    if (wide && !MultiByteToWideChar(CP_ACP, 0, path, -1, wide, count)) {
        free(wide);
        wide = NULL;
    }
    if (wide)
        for (wchar_t* p = wide; *p; ++p)
            if (*p == L'/')
                *p = L'\\';
    return wide;
}

int ROMlib_host_trash(unsigned action, unsigned count, const char* const* paths)
{
    /* Recycle-only and session undo flags require Windows 8 or later. */
    if (!IsWindows8OrGreater())
        return -1;
    if (!action)
        return 0;
    if (action == 1)
        return (INT_PTR)ShellExecuteW(NULL, L"open", L"shell:RecycleBinFolder", NULL, NULL, SW_SHOWNORMAL) > 32 ? 0
                                                                                                                : -1;
    if (action == 3)
        return SUCCEEDED(SHEmptyRecycleBinW(NULL, NULL, 0)) ? 0 : -1;
    if (action != 2 || !count || count > 3 || !paths)
        return -1;
    HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
        return -1;
    IFileOperation* operation = NULL;
    HRESULT result =
        CoCreateInstance(&CLSID_FileOperation, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOperation, (void**)&operation);
    if (SUCCEEDED(result))
        result = IFileOperation_SetOperationFlags(operation, FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI |
                                                                 FOFX_RECYCLEONDELETE | FOFX_EARLYFAILURE |
                                                                 FOFX_ADDUNDORECORD);
    for (unsigned i = 0; SUCCEEDED(result) && i < count; ++i) {
        wchar_t* wide = paths[i] ? trash_path(paths[i]) : NULL;
        IShellItem* item = NULL;
        result = wide ? SHCreateItemFromParsingName(wide, NULL, &IID_IShellItem, (void**)&item) : E_OUTOFMEMORY;
        if (SUCCEEDED(result))
            result = IFileOperation_DeleteItem(operation, item, NULL);
        if (item)
            IShellItem_Release(item);
        free(wide);
    }
    BOOL aborted = TRUE;
    if (SUCCEEDED(result))
        result = IFileOperation_PerformOperations(operation);
    if (SUCCEEDED(result))
        result = IFileOperation_GetAnyOperationsAborted(operation, &aborted);
    if (operation)
        IFileOperation_Release(operation);
    if (SUCCEEDED(initialized))
        CoUninitialize();
    return SUCCEEDED(result) && !aborted ? 0 : -1;
}
#else
int ROMlib_host_trash(unsigned action, unsigned count, const char* const* paths)
{
    (void)action;
    (void)count;
    (void)paths;
    return -1;
}
#endif
