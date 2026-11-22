/*+===================================================================
  File:      launcher_test.cpp

  Summary:   Use this executable as a child argument recorder for quoting/ownership tests. Explicit --probe mode instead
             runs an original fixture through the selected ROMAW runtime.

  Classes:   Used: FinderLaunchRequest, FinderLaunchResult, std::ifstream, std::ofstream, std::vector, std::wstring

  Functions: wmain

  Origin:    Existing repository source in executor/tests/native-finder; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

// Use this executable as a child argument recorder for quoting/ownership
// tests. Explicit --probe mode instead runs an original fixture through the
// selected ROMAW runtime.

#include "finder/launcher.h"
#include <windows.h>
#include <objbase.h>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

// Fail promptly in unattended Windows runs instead of opening a WER dialog.
#undef assert
#define assert(condition)                                                                  \
    do {                                                                                   \
        if (!(condition)) {                                                                \
            std::cerr << __FILE__ << ":" << __LINE__ << ": " << #condition << " failed\n"; \
            std::exit(2);                                                                  \
        }                                                                                  \
    } while (false)

namespace fs = std::filesystem;

int wmain(int argc, wchar_t** argv)
{
    // The same executable acts as a command-line recorder for the launcher.
    if (argc > 1 && std::wstring(argv[1]) != L"--probe") {
        int file = 1;
        while (file + 1 < argc && argv[file][0] == L'-')
            file += 2;
        std::ofstream out(fs::path(argv[file]), std::ios::binary);
        for (int i = 1; i < argc; ++i)
            out.write(reinterpret_cast<const char*>(argv[i]), (wcslen(argv[i]) + 1) * sizeof(wchar_t));
        return out ? 0 : 1;
    }
    wchar_t module[32768];
    assert(GetModuleFileNameW(nullptr, module, std::size(module)));
    auto wait = [](FinderLaunchResult& result) {
        auto handle = reinterpret_cast<HANDLE>(result.process_handle);
        assert(handle && result.process_id);
        if (WaitForSingleObject(handle, 30000) != WAIT_OBJECT_0) {
            // This is exclusively the child created by this test.
            TerminateProcess(handle, 99);
            assert(false && "Launcher child did not finish");
        }
        DWORD exit;
        assert(GetExitCodeProcess(handle, &exit) && exit == 0);
        CloseHandle(handle);
    };
    if (argc == 6 && std::wstring(argv[1]) == L"--probe") {
        FinderLaunchRequest r{sizeof r, uint32_t(_wtoi(argv[4])), argv[2], argv[3], nullptr, 0, _wtoi(argv[5])};
        FinderLaunchResult result{};
        result.size = sizeof result;
        assert(FinderLaunch(&r, &result) == ERROR_SUCCESS);
        wait(result);
        std::cout << "PASS: ROMAW launcher runtime probe\n";
    }
    else {
        wchar_t temporary[32768];
        assert(GetTempPathW(std::size(temporary), temporary));
        GUID guid;
        assert(SUCCEEDED(CoCreateGuid(&guid)));
        wchar_t id[64];
        StringFromGUID2(guid, id, std::size(id));
        auto root = fs::path(temporary) / (L"Finder-launcher-" + std::wstring(id));
        assert(fs::create_directory(root));
        auto file = root / L"Macintosh application capture";
        const wchar_t* docs[] = {L"D:\\Source Code\\a document",
                                 L"C:\\trailing\\",
                                 L"a\"quote",
                                 L"\u65e5\u672c",
                                 L"Recovery:What's New?:\u00a0 What's New? (8/99)",
                                 L"Recovery:Caf\u00e9:Read Me"};
        const wchar_t* expected_docs[] = {L"D:\\Source Code\\a document",
                                          L"C:\\trailing\\",
                                          L"a\"quote",
                                          L"\u65e5\u672c",
                                          L"Recovery:What's New?:::CA What's New? (8/99)",
                                          L"Recovery:Caf::8E:Read Me"};
        for (uint32_t kind = 1; kind <= 4; ++kind) {
            const FinderImageMount mounts[] = {
                {L"D:\\Temp\\read only.image", -1}, {L"D:\\Temp\\write.toast", 1}, {L"D:\\Temp\\default.hfv", 0}};
            FinderLaunchRequest r{sizeof r,     kind,   module,
                                  file.c_str(), docs,   uint32_t(std::size(docs)),
                                  -16415,       mounts, uint32_t(std::size(mounts))};
            FinderLaunchResult result{};
            result.size = sizeof result;
            assert(!FinderLaunch(&r, &result));
            wait(result);
            std::ifstream in(file, std::ios::binary);
            std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
            std::wstring expected;
            auto add = [&](const wchar_t* s) {
                expected += s;
                expected += L'\0';
            };
            for (const auto& mount : mounts) {
                add(mount.mode < 0 ? L"-mount-ro" : mount.mode > 0 ? L"-mount-rw" : L"-mount");
                add(mount.source);
            }
            if (kind == FINDER_DESK_ACCESSORY || kind == FINDER_CONTROL_PANEL) {
                add(L"-syszone");
                add(L"2m");
                add(L"-applzone");
                add(L"4m");
            }
            if (kind == FINDER_DESK_ACCESSORY) {
                add(L"-desk-resource");
                add(L"-16415");
            }
            add(file.c_str());
            for (auto doc : expected_docs)
                add(doc);
            assert(bytes.size() == expected.size() * sizeof(wchar_t));
            assert(!std::memcmp(bytes.data(), expected.data(), bytes.size()));
        }
        FinderLaunchResult result{};
        result.size = sizeof result;
        assert(FinderLaunch(nullptr, &result) == ERROR_INVALID_PARAMETER);
        FinderLaunchRequest bad{sizeof bad, 5, module, file.c_str(), nullptr, 0, 0};
        assert(FinderLaunch(&bad, &result) == ERROR_INVALID_PARAMETER);
        bad.kind = 1;
        bad.document_count = 1;
        assert(FinderLaunch(&bad, &result) == ERROR_INVALID_PARAMETER);
        bad.document_count = 0;
        bad.mount_count = 1;
        assert(FinderLaunch(&bad, &result) == ERROR_INVALID_PARAMETER);
        FinderImageMount invalid_mount{L"D:\\Temp\\invalid.img", 2};
        bad.mounts = &invalid_mount;
        assert(FinderLaunch(&bad, &result) == ERROR_INVALID_PARAMETER);
        invalid_mount.mode = -1;
        invalid_mount.source = nullptr;
        assert(FinderLaunch(&bad, &result) == ERROR_INVALID_PARAMETER);
        bad.mount_count = 0;
        auto missing_runtime = root / L"nonexistent-runtime.exe";
        bad.runtime = missing_runtime.c_str();
        assert(FinderLaunch(&bad, &result) != ERROR_SUCCESS && !result.process_handle);
        fs::remove_all(root);
        std::cout
            << "PASS: built-in launcher, four launch kinds, Unicode/document quoting, invalid inputs and process ownership\n";
    }
    return 0;
}
