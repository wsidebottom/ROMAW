/*+===================================================================
  File:      image_mount_test.cpp

  Summary:   Check Finder image recognition and routing into the initialized runtime.

  Classes:   None.

  Functions: ROMlib_mount_image, ROMlib_image_error, main.

  Origin:    Executor native Finder image-open regression fixture.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "finder/launcher.h"
#include <windows.h>
#include <cassert>
#include <cwchar>
#include <string>
#include <vector>
#include <cstdio>

static unsigned mounts, explanations;
static int16_t result;
static std::string mounted_path;

extern "C" int16_t ROMlib_mount_image(const char* path)
{
    ++mounts;
    mounted_path = path;
    return result;
}

extern "C" const char* ROMlib_image_error(void)
{
    ++explanations;
    return "Incomplete image: missing source bytes.";
}

int main()
{
    for (auto extension : {L".iso", L".cdr", L".toast", L".dmg", L".hfv", L".hda", L".dsk", L".img", L".image", L".ima",
                           L".dc42", L".hfs", L".hfsplus", L".hfsx", L".smi"}) {
        auto path = std::wstring(L"D:/Temp/disk with spaces") + extension;
        assert(FinderIsDiskImage(path.c_str()));
        CharUpperBuffW(path.data(), (DWORD)path.size());
        assert(FinderIsDiskImage(path.c_str()));
    }
    for (auto path : {L"", L"D:/Temp/disk.img.txt", L"D:/folder.img/application", L"D:\\folder.iso\\document", L"file"})
        assert(!FinderIsDiskImage(path));
    assert(!FinderIsDiskImage(nullptr));
    wchar_t details[128];
    assert(FinderMountImage(L"D:\\Temp\\disk with spaces.toast", details, 128) == 0);
    assert(mounts == 1 && mounted_path == "D:\\Temp\\disk with spaces.toast" && !*details);
    result = -50;
    assert(FinderMountImage(L"D:/broken.image", details, 128) == -50);
    assert(explanations == 1 && wcsstr(details, L"missing source bytes"));
    result = -128;
    assert(FinderMountImage(L"D:/cancelled.dsk", details, 128) == -128);
    assert(!*details && explanations == 1);
    auto before = mounts;
    assert(FinderMountImage(nullptr, details, 128) == -50);
    assert(FinderMountImage(L"", details, 128) == -50);
    assert(FinderMountImage(L"D:/disk.img", nullptr, 1) == -50);
    std::wstring oversized(32768, L'x');
    assert(FinderMountImage(oversized.c_str(), details, 128) == -50 && mounts == before);
    wchar_t guards[] = {L'L', L'x', L'R'};
    assert(FinderMountImage(nullptr, guards + 1, 1) == -50);
    assert(guards[0] == L'L' && guards[1] == 0 && guards[2] == L'R');
    const wchar_t* unicode = L"D:/Temp/\u65e5\u672c.image";
    BOOL substituted = FALSE;
    BOOL* used = GetACP() == CP_UTF8 ? nullptr : &substituted;
    DWORD flags = GetACP() == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
    int count = WideCharToMultiByte(CP_ACP, flags, unicode, -1, nullptr, 0, nullptr, used);
    result = 0;
    if (!count || substituted)
        assert(FinderMountImage(unicode, details, 128) == -50 && mounts == before);
    else {
        assert(!FinderMountImage(unicode, details, 128));
        wchar_t decoded[128];
        assert(MultiByteToWideChar(CP_ACP, 0, mounted_path.c_str(), -1, decoded, 128));
        assert(!wcscmp(decoded, unicode));
    }
    puts(
        "PASS: Finder image extensions, runtime mounting, access-policy routing, cancellation, bounded errors and path encoding");
}
