/*+===================================================================
  File:      recycle_test.cpp

  Summary:   Verify native recycle routing, companion batches, irreversible-delete warning flags and shell cancellation.

  Classes:   None.

  Functions: main; finder::testing shell API replacements.

  Origin:    Executor native Finder regression fixture; shell calls are intercepted to protect the user's bins.

  Copyright and Legal notices: License reference: COPYING.
===================================================================+*/

#include "finder/recycle.h"
#include <shellapi.h>
#include <shlobj.h>
#include <cassert>
#include <fstream>
#include <iostream>

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
HWND owner = reinterpret_cast<HWND>(123);
std::vector<fs::path> deleted;
unsigned delete_calls = 0, empty_calls = 0, open_calls = 0;
int delete_result = 0;
bool aborted = false, execute_ok = true, properties = false;
HRESULT query_result = S_OK, empty_result = S_OK, folder_result = S_OK;
LONGLONG items = 0;
}

namespace finder::testing {
int WINAPI recycle_delete(SHFILEOPSTRUCTW* operation)
{
    ++delete_calls;
    assert(operation->hwnd == owner && operation->wFunc == FO_DELETE && !operation->pTo);
    assert(operation->fFlags & FOF_ALLOWUNDO);
    assert(operation->fFlags & FOF_WANTNUKEWARNING);
    assert(operation->fFlags & FOF_NO_CONNECTED_ELEMENTS);
    assert(!(operation->fFlags & (FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT)));
    deleted.clear();
    const wchar_t* name = operation->pFrom;
    while (*name) {
        deleted.emplace_back(name);
        assert(deleted.back().is_absolute());
        name += wcslen(name) + 1;
    }
    assert(name > operation->pFrom && name[-1] == 0); // Full double terminator.
    operation->fAnyOperationsAborted = aborted;
    return delete_result;
}

HRESULT WINAPI recycle_query(LPCWSTR root, SHQUERYRBINFO* info)
{
    assert(!root && info->cbSize == sizeof *info);
    info->i64NumItems = items;
    return query_result;
}

HRESULT WINAPI recycle_empty(HWND window, LPCWSTR root, DWORD flags)
{
    ++empty_calls;
    assert(window == owner && !root && flags == 0); // Never suppress the destructive confirmation.
    return empty_result;
}

HRESULT WINAPI recycle_folder(REFKNOWNFOLDERID id, DWORD flags, HANDLE token, PIDLIST_ABSOLUTE* result)
{
    assert(IsEqualGUID(id, FOLDERID_RecycleBinFolder) && !flags && !token);
    *result = nullptr;
    if (SUCCEEDED(folder_result)) {
        *result = static_cast<PIDLIST_ABSOLUTE>(CoTaskMemAlloc(sizeof(USHORT)));
        assert(*result);
        (*result)->mkid.cb = 0;
    }
    return folder_result;
}

BOOL WINAPI recycle_execute(SHELLEXECUTEINFOW* operation)
{
    ++open_calls;
    assert(operation->hwnd == owner && operation->cbSize == sizeof *operation);
    assert(operation->fMask & SEE_MASK_IDLIST);
    assert(operation->lpIDList && !operation->lpFile);
    assert(wcscmp(operation->lpVerb, properties ? L"properties" : L"open") == 0);
    return execute_ok;
}
}

int main()
{
    wchar_t temporary[32768], id[64];
    assert(GetTempPathW(32768, temporary));
    GUID guid;
    assert(SUCCEEDED(CoCreateGuid(&guid)));
    StringFromGUID2(guid, id, 64);
    auto root = fs::path(temporary) / (L"Finder-recycle-" + std::wstring(id));
    fs::create_directories(root / L"one");
    fs::create_directories(root / L"two");
    auto first = root / L"one/Same name", second = root / L"two/Same name";
    auto companion = first.parent_path() / L"._Same name";
    std::ofstream(first) << "first";
    std::ofstream(second) << "second";
    std::ofstream(companion) << "resource fork";
    trash_items(owner, {});
    assert(delete_calls == 0);
    trash_items(owner, {first, second, first});
    assert(deleted == std::vector<fs::path>({first, companion, second}));
    assert(fs::exists(first) && fs::exists(second) && fs::exists(companion)); // Mocks never delete.
    trash_items(owner, {root / L"one", first});
    assert(deleted == std::vector<fs::path>({root / L"one"}));
    trash_items(owner, {fs::path((root / L"one").wstring() + L"\\")});
    assert(deleted == std::vector<fs::path>({root / L"one"}));
    auto fails = [](auto operation) {
        try {
            operation();
        }
        catch (const std::exception&) {
            return true;
        }
        return false;
    };
    for (auto path : {fs::path(), fs::path(L"C:\\"), root / L"*", fs::path(L"\\\\server\\share\\")})
        if (!fails([&] { trash_items(owner, {path}); })) {
            std::wcerr << L"Unexpected accepted path: [" << path.native() << L"]\n";
            assert(false);
        }
    // Both unavailable/disabled-bin fallback and an ordinary recycle operation
    // retain the shell's WANTNUKEWARNING contract; refusal is not an error.
    aborted = true;
    delete_result = ERROR_CANCELLED;
    trash_items(owner, {first});
    assert(fs::exists(first) && fs::exists(companion));
    aborted = false;
    delete_result = ERROR_ACCESS_DENIED;
    assert(fails([&] { trash_items(owner, {first}); }));
    assert(!recycle_bin_full());
    items = 2;
    assert(recycle_bin_full());
    query_result = E_FAIL;
    assert(recycle_bin_full()); // A failed query must not paint a misleading empty icon.
    empty_recycle_bin(owner);
    empty_result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    empty_recycle_bin(owner);
    empty_result = E_ABORT;
    empty_recycle_bin(owner);
    empty_result = E_ACCESSDENIED;
    assert(fails([&] { empty_recycle_bin(owner); }));
    assert(empty_calls == 4);
    open_recycle_bin(owner);
    properties = true;
    open_recycle_bin(owner, true);
    execute_ok = false;
    assert(fails([&] { open_recycle_bin(owner, true); }));
    assert(open_calls == 3);
    folder_result = E_FAIL;
    assert(fails([&] { open_recycle_bin(owner); }));
    assert(open_calls == 3);
    auto backing = root / L"$Recycle.Bin";
    fs::create_directory(backing);
    assert(!hidden_metadata(backing)); // An ordinary same-name folder is not a native bin.
    assert(SetFileAttributesW(backing.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
    assert(hidden_metadata(backing));
    assert(SetFileAttributesW(backing.c_str(), FILE_ATTRIBUTE_NORMAL));
    assert(fs::exists(first) && fs::exists(second) && fs::exists(companion));
    // Only this GUID fixture is removed; native bins were never called.
    fs::remove_all(root);
    std::cout
        << "PASS: native per-source recycle routing, companion batches, permanent-delete warning flags, cancellation, bin status/open/properties/empty dispatch; real bins untouched\n";
}
