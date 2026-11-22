/*+===================================================================
  File:      image_mount.cpp

  Summary:   Route Finder image-open actions into the initialized Macintosh runtime.

  Classes:   None; uses standard string and vector containers.

  Functions: FinderIsDiskImage, FinderMountImage.

  Origin:    Executor native Finder adapter for the existing direct image mounter.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "finder/launcher.h"
#include <windows.h>
#include <algorithm>
#include <cwchar>
#include <vector>

extern "C" int16_t ROMlib_mount_image(const char* path);
extern "C" const char* ROMlib_image_error(void);

int FinderIsDiskImage(const wchar_t* path)
{
    if (!path || !*path)
        return 0;
    const wchar_t* suffix = wcsrchr(path, L'.');
    if (!suffix || wcspbrk(suffix, L"/\\"))
        return 0;
    const wchar_t* extensions[] = {L".dmg", L".hfsplus", L".hfsx", L".hfv", L".hda",   L".hfs", L".dsk", L".image",
                                   L".img", L".ima",     L".dc42", L".iso", L".toast", L".cdr", L".smi"};
    for (auto extension : extensions)
        if (!_wcsicmp(suffix, extension))
            return 1;
    return 0;
}

int32_t FinderMountImage(const wchar_t* path, wchar_t* details, uint32_t capacity)
{
    auto explain = [&](const wchar_t* text) {
        if (details && capacity) {
            size_t length = std::min<size_t>(wcslen(text), capacity - 1);
            wmemcpy(details, text, length);
            details[length] = 0;
        }
    };
    explain(L"");
    if (!path || !*path || wcslen(path) >= 32768 || (capacity && !details)) {
        explain(L"Choose a valid disk image file.");
        return -50;
    }
    try {
        BOOL substituted = FALSE;
        BOOL* used_default = GetACP() == CP_UTF8 ? nullptr : &substituted;
        DWORD flags = GetACP() == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
        int length = WideCharToMultiByte(CP_ACP, flags, path, -1, nullptr, 0, nullptr, used_default);
        if (length <= 0 || substituted) {
            explain(L"This filename cannot be represented by the Macintosh runtime's filesystem encoding.");
            return -50;
        }
        std::vector<char> encoded(length);
        if (!WideCharToMultiByte(CP_ACP, flags, path, -1, encoded.data(), length, nullptr, used_default) ||
            substituted) {
            explain(L"This filename cannot be represented by the Macintosh runtime's filesystem encoding.");
            return -50;
        }
        int32_t error = ROMlib_mount_image(encoded.data());
        if (error && error != -128) {
            const char* text = ROMlib_image_error();
            int count = text ? MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0) : 0;
            if (count > 0) {
                std::vector<wchar_t> converted(count);
                if (MultiByteToWideChar(CP_UTF8, 0, text, -1, converted.data(), count))
                    explain(converted.data());
            }
        }
        return error;
    }
    catch (...) {
        explain(L"There is not enough memory to open this disk image.");
        return -108;
    }
}
