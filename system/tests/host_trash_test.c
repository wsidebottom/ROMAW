/*+===================================================================
  File:      host_trash_test.c

  Summary:   Implements host trash test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <assert.h>
#include <stdio.h>
#include <wchar.h>

static unsigned queued, performed, flags, released, uninitialized;
static HRESULT initialize_result, create_result, queue_result, perform_result;
static BOOL aborted;

static ULONG STDMETHODCALLTYPE release_operation(IFileOperation* self)
{
    (void)self;
    ++released;
    return 0;
}

static HRESULT STDMETHODCALLTYPE set_flags(IFileOperation* self, DWORD value)
{
    (void)self;
    flags = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE delete_item(IFileOperation* self, IShellItem* item, IFileOperationProgressSink* sink)
{
    (void)self;
    (void)item;
    assert(!sink);
    ++queued;
    return queue_result;
}

static HRESULT STDMETHODCALLTYPE perform(IFileOperation* self)
{
    (void)self;
    ++performed;
    return perform_result;
}

static HRESULT STDMETHODCALLTYPE get_aborted(IFileOperation* self, BOOL* value)
{
    (void)self;
    *value = aborted;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE release_item(IShellItem* self)
{
    (void)self;
    return 0;
}

static IFileOperationVtbl operation_methods = {.Release = release_operation,
                                               .SetOperationFlags = set_flags,
                                               .DeleteItem = delete_item,
                                               .PerformOperations = perform,
                                               .GetAnyOperationsAborted = get_aborted};
static IFileOperation operation = {&operation_methods};
static IShellItemVtbl item_methods = {.Release = release_item};
static IShellItem item = {&item_methods};

static HRESULT initialize(LPVOID reserved, DWORD mode)
{
    assert(!reserved && mode == COINIT_APARTMENTTHREADED);
    return initialize_result;
}

static void uninitialize(void)
{
    ++uninitialized;
}

static HRESULT create(REFCLSID cls, LPUNKNOWN outer, DWORD context, REFIID iid, LPVOID* out)
{
    assert(IsEqualCLSID(cls, &CLSID_FileOperation) && !outer && context == CLSCTX_INPROC_SERVER &&
           IsEqualIID(iid, &IID_IFileOperation));
    *out = SUCCEEDED(create_result) ? &operation : NULL;
    return create_result;
}

static HRESULT shell_item(LPCWSTR path, IBindCtx* bind, REFIID iid, void** out)
{
    assert(!bind && IsEqualIID(iid, &IID_IShellItem));
    assert(!wcschr(path, L'/') && wcsstr(path, L"D:\\fixture\\"));
    *out = &item;
    return S_OK;
}

static HINSTANCE open_bin(HWND owner, LPCWSTR verb, LPCWSTR path, LPCWSTR args, LPCWSTR directory, INT show)
{
    assert(!owner && !wcscmp(verb, L"open") && !wcscmp(path, L"shell:RecycleBinFolder") && !args && !directory &&
           show == SW_SHOWNORMAL);
    return (HINSTANCE)33;
}

static HRESULT empty_bin(HWND owner, LPCWSTR root, DWORD options)
{
    assert(!owner && !root && !options); /* Keep native confirmation enabled. */
    return S_OK;
}

#define CoInitializeEx initialize
#define CoUninitialize uninitialize
#define CoCreateInstance create
#define SHCreateItemFromParsingName shell_item
#define ShellExecuteW open_bin
#define SHEmptyRecycleBinW empty_bin
#include "../../host/trash.c"

int main(void)
{
    const char* paths[] = {"D:/fixture/._Document", "D:/fixture/Document"};
    assert(!ROMlib_host_trash(0, 0, NULL));
    assert(!ROMlib_host_trash(1, 0, NULL));
    assert(!ROMlib_host_trash(3, 0, NULL));
    assert(!ROMlib_host_trash(2, 2, paths));
    assert(queued == 2 && performed == 1 && released == 1 && uninitialized == 1);
    assert((flags & (FOFX_RECYCLEONDELETE | FOFX_EARLYFAILURE | FOFX_ADDUNDORECORD | FOF_ALLOWUNDO)) ==
           (FOFX_RECYCLEONDELETE | FOFX_EARLYFAILURE | FOFX_ADDUNDORECORD | FOF_ALLOWUNDO));
    queue_result = E_ACCESSDENIED;
    assert(ROMlib_host_trash(2, 2, paths) && performed == 1);
    queue_result = S_OK;
    perform_result = E_FAIL;
    assert(ROMlib_host_trash(2, 2, paths));
    perform_result = S_OK;
    aborted = TRUE;
    assert(ROMlib_host_trash(2, 2, paths));
    aborted = FALSE;
    create_result = E_OUTOFMEMORY;
    unsigned before = performed;
    assert(ROMlib_host_trash(2, 2, paths) && performed == before);
    create_result = S_OK;
    initialize_result = E_FAIL;
    assert(ROMlib_host_trash(2, 2, paths) && performed == before);
    initialize_result = RPC_E_CHANGED_MODE;
    unsigned closed = uninitialized;
    assert(!ROMlib_host_trash(2, 2, paths) && uninitialized == closed);
    assert(ROMlib_host_trash(2, 0, paths));
    assert(ROMlib_host_trash(2, 4, paths));
    puts(
        "Host Trash: recycle-only flags, companion batching, failures, cancellation, COM ownership and native UI routing passed; no real Trash was changed.");
    return 0;
}
