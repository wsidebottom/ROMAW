/*+===================================================================
  File:      recycle.cpp

  Summary:   Use Windows Shell bins, permanent-deletion warnings, emptying and restore UI instead of private Trash storage.

  Classes:   None.

  Functions: finder::trash_items, finder::recycle_bin_full, finder::empty_recycle_bin, finder::open_recycle_bin.

  Origin:    Executor Finder Windows Shell integration; deletion retains the existing data/companion batching.
             API contracts: https://learn.microsoft.com/windows/win32/api/shellapi/ns-shellapi-shfileopstructw

  Copyright and Legal notices: License reference: COPYING.
===================================================================+*/

#include "finder/recycle.h"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

namespace finder {
#ifdef FINDER_RECYCLE_TESTING
// The regression fixture intercepts every destructive or UI-opening shell call.
// It never empties, queries, opens or changes the user's real Recycle Bin.
namespace testing {
int WINAPI recycle_delete(SHFILEOPSTRUCTW* operation);
HRESULT WINAPI recycle_query(LPCWSTR root, SHQUERYRBINFO* info);
HRESULT WINAPI recycle_empty(HWND owner, LPCWSTR root, DWORD flags);
HRESULT WINAPI recycle_folder(REFKNOWNFOLDERID id, DWORD flags, HANDLE token, PIDLIST_ABSOLUTE* result);
BOOL WINAPI recycle_execute(SHELLEXECUTEINFOW* operation);
}
#endif
namespace {
#ifdef FINDER_RECYCLE_TESTING
constexpr auto delete_files = testing::recycle_delete;
constexpr auto query_bin = testing::recycle_query;
constexpr auto empty_bin = testing::recycle_empty;
constexpr auto bin_folder = testing::recycle_folder;
constexpr auto execute = testing::recycle_execute;
#else
constexpr auto delete_files = SHFileOperationW;
constexpr auto query_bin = SHQueryRecycleBinW;
constexpr auto empty_bin = SHEmptyRecycleBinW;
constexpr auto bin_folder = SHGetKnownFolderIDList;
constexpr auto execute = ShellExecuteExW;
#endif

bool volume_root(const fs::path& path)
{
    auto text = path.native();
    auto slash = [](wchar_t c) { return c == L'\\' || c == L'/'; };
    while (text.size() > 3 && slash(text.back()))
        text.pop_back();
    if (text.size() >= 2 && slash(text[0]) && slash(text[1])) {
        auto server_end = text.find_first_of(L"\\/", 2);
        return server_end == std::wstring::npos || text.find_first_of(L"\\/", server_end + 1) == std::wstring::npos;
    }
    return PathIsRootW(path.c_str()) != FALSE;
}

bool inside(const fs::path& parent, const fs::path& child)
{
    auto part = child.begin();
    for (const auto& component : parent) {
        if (part == child.end() || _wcsicmp(component.c_str(), part->c_str()))
            return false;
        ++part;
    }
    return true;
}
}

void trash_items(HWND owner, const std::vector<fs::path>& paths)
{
    std::vector<fs::path> selected;
    for (const auto& path : paths) {
        if (path.empty())
            throw std::runtime_error("An empty path cannot be sent to the Recycle Bin.");
        if (volume_root(path) || path.native().find_first_of(L"*?") != std::wstring::npos)
            throw std::runtime_error("A drive root, wildcard or extended path cannot be sent to the Recycle Bin.");
        // Resolve spelling through the Windows path API, not canonicalization:
        // preserve UNC prefixes and delete junctions/symlinks, not their targets.
        wchar_t full[32768];
        DWORD length = GetFullPathNameW(path.c_str(), 32768, full, nullptr);
        if (!length || length >= 32768)
            throw std::runtime_error("Windows could not resolve the selected path.");
        std::wstring name(full, length);
        while (name.size() > 3 && (name.back() == L'\\' || name.back() == L'/'))
            name.pop_back();
        fs::path source(name);
        if (volume_root(source))
            throw std::runtime_error("A drive root cannot be sent to the Recycle Bin.");
        selected.push_back(std::move(source));
    }
    std::wstring names;
    std::vector<fs::path> queued;
    auto append = [&](const fs::path& path) {
        if (std::none_of(queued.begin(), queued.end(),
                         [&](const fs::path& old) { return _wcsicmp(old.c_str(), path.c_str()) == 0; })) {
            queued.push_back(path);
            names += path.wstring();
            names += L'\0';
        }
    };
    for (size_t i = 0; i < selected.size(); ++i) {
        bool covered = false;
        for (size_t j = 0; j < selected.size(); ++j)
            if (j != i && inside(selected[j], selected[i]) &&
                (_wcsicmp(selected[j].c_str(), selected[i].c_str()) || j < i)) {
                covered = true;
                break;
            }
        if (covered)
            continue;
        append(selected[i]);
        for (const auto& companion : mac_companions(selected[i]))
            append(companion);
    }
    if (names.empty())
        return;
    names += L'\0'; // The shell requires a double-null-terminated absolute path list.
    SHFILEOPSTRUCTW operation{};
    operation.hwnd = owner;
    operation.wFunc = FO_DELETE;
    operation.pFrom = names.c_str();
    // Windows decides whether the source drive/configuration supports recycle.
    // WANTNUKEWARNING keeps its explicit permanent-delete confirmation even
    // when the bin is unavailable, disabled or unable to contain this item.
    operation.fFlags = FOF_ALLOWUNDO | FOF_WANTNUKEWARNING | FOF_NO_CONNECTED_ELEMENTS;
    int error = delete_files(&operation);
    if (error && !operation.fAnyOperationsAborted)
        throw std::runtime_error("Windows could not recycle the selected items.");
}

bool recycle_bin_full()
{
    SHQUERYRBINFO info{};
    info.cbSize = sizeof info;
    return FAILED(query_bin(nullptr, &info)) || info.i64NumItems > 0;
}

void empty_recycle_bin(HWND owner)
{
    if (!recycle_bin_full())
        return;
    // Never remove the native bin's backing directories ourselves. Flags zero
    // preserves Windows' permanent-deletion confirmation and progress UI.
    HRESULT result = empty_bin(owner, nullptr, 0);
    if (FAILED(result) && result != E_ABORT && result != HRESULT_FROM_WIN32(ERROR_CANCELLED))
        throw std::runtime_error("Windows could not empty the Recycle Bin.");
}

void open_recycle_bin(HWND owner, bool properties)
{
    PIDLIST_ABSOLUTE folder = nullptr;
    HRESULT result = bin_folder(FOLDERID_RecycleBinFolder, 0, nullptr, &folder);
    if (FAILED(result) || !folder) {
        CoTaskMemFree(folder);
        throw std::runtime_error("Windows could not locate the Recycle Bin.");
    }
    SHELLEXECUTEINFOW operation{};
    operation.cbSize = sizeof operation;
    operation.fMask = SEE_MASK_IDLIST | SEE_MASK_INVOKEIDLIST;
    operation.hwnd = owner;
    operation.lpVerb = properties ? L"properties" : L"open";
    operation.lpIDList = folder;
    operation.nShow = SW_SHOWNORMAL;
    bool opened = execute(&operation) != FALSE;
    CoTaskMemFree(folder);
    if (!opened)
        throw std::runtime_error("Windows could not open the Recycle Bin.");
}
}
