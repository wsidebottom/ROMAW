/*+===================================================================
  File:      romaw.cpp

  Summary:   Internal host-process launcher for classic software. This module constructs a Windows command line and
             transfers a process handle; CPU emulation and Toolbox initialization happen in the child ROMAW process.

  Classes:   Used: FinderLaunchRequest, FinderLaunchResult, std::wstring

  Functions: FinderLaunch

  Origin:    Existing repository source in executor/src/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

// Internal host-process launcher for classic software. This module constructs
// a Windows command line and transfers a process handle; CPU emulation and
// Toolbox initialization happen in the child ROMAW process.

#include "finder/launcher.h"
#include <windows.h>
#include <string>
#include <vector>
#include <stdexcept>

static_assert(sizeof(void*) == 8, "The ROMAW launcher is Windows x64 only");

namespace {
// Encode a borrowed wide string as one Windows command-line argument,
// including trailing backslashes.
std::wstring quote(const wchar_t* s)
{
    std::wstring out = L"\"";
    unsigned slashes = 0;
    for (; *s; ++s) {
        if (*s == L'\\') {
            ++slashes;
            continue;
        }
        out.append(slashes * (*s == L'"' ? 2 : 1), L'\\');
        if (*s == L'"')
            out += L'\\';
        out += *s;
        slashes = 0;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}

// Classic volume paths carry MacRoman catalog names. The child receives
// Windows ANSI arguments, so use the ::XX escapes decoded by argv_to_appfile
// to preserve non-ASCII catalog bytes across that encoding boundary.
std::wstring quote_file(const wchar_t* path)
{
    std::wstring text(path);
    auto colon = text.find(L':');
    if (colon == std::wstring::npos || colon <= 1 || text[0] == L'\\' || text[0] == L'/')
        return quote(path);
    int count = WideCharToMultiByte(10000, 0, path, -1, nullptr, 0, nullptr, nullptr);
    if (!count)
        throw std::runtime_error("Cannot encode Macintosh pathname");
    std::string roman(count, '\0');
    if (!WideCharToMultiByte(10000, 0, path, -1, roman.data(), count, nullptr, nullptr))
        throw std::runtime_error("Cannot encode Macintosh pathname");
    std::wstring escaped;
    const wchar_t* hex = L"0123456789ABCDEF";
    for (unsigned char byte : roman) {
        if (!byte)
            break;
        if (byte >= 0x80) {
            escaped += L"::";
            escaped += hex[byte >> 4];
            escaped += hex[byte & 15];
        }
        else
            escaped += wchar_t(byte);
    }
    return quote(escaped.c_str());
}

// Validate a host launch request, build its classic-runtime arguments and
// return ownership of the child process handle. No guest pointers enter this
// path.
uint32_t launch(const FinderLaunchRequest* r, FinderLaunchResult* out)
{
    if (!out || out->size != sizeof *out)
        return ERROR_INVALID_PARAMETER;
    out->error = ERROR_INVALID_PARAMETER;
    out->process_id = 0;
    out->process_handle = 0;
    if (!r || r->size != sizeof *r || !r->runtime || !*r->runtime || !r->file || !*r->file ||
        r->document_count > 1024 || (r->document_count && !r->documents) || r->kind < 1 || r->kind > 4 ||
        r->mount_count > 1024 || (r->mount_count && !r->mounts))
        return out->error;
    try {
        std::wstring command = quote(r->runtime);
        for (uint32_t i = 0; i < r->mount_count; ++i) {
            const auto& mount = r->mounts[i];
            if (!mount.source || !*mount.source || mount.mode < -1 || mount.mode > 1)
                return out->error;
            command += mount.mode < 0 ? L" -mount-ro " : mount.mode > 0 ? L" -mount-rw " : L" -mount ";
            command += quote(mount.source);
        }
        // Accessory/panel hosts have no application SIZE resource. Keep their
        // heaps below the 24-bit boundary, with room for the guest framebuffer.
        if (r->kind == FINDER_DESK_ACCESSORY || r->kind == FINDER_CONTROL_PANEL)
            command += L" -syszone 2m -applzone 4m";
        if (r->kind == FINDER_DESK_ACCESSORY) {
            if (r->resource_id < -32768 || r->resource_id > 32767)
                return out->error;
            command += L" -desk-resource " + std::to_wstring(r->resource_id);
        }
        command += L" " + quote_file(r->file);
        for (unsigned i = 0; i < r->document_count; ++i) {
            if (!r->documents[i])
                return out->error;
            command += L" " + quote_file(r->documents[i]);
        }
        if (command.size() >= 32767) {
            out->error = ERROR_FILENAME_EXCED_RANGE;
            return out->error;
        }
        STARTUPINFOW start{};
        start.cb = sizeof start;
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(r->runtime, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &start,
                            &process)) {
            out->error = GetLastError();
            return out->error;
        }
        CloseHandle(process.hThread);
        out->process_id = process.dwProcessId;
        out->process_handle = reinterpret_cast<uintptr_t>(process.hProcess);
        out->error = ERROR_SUCCESS;
    }
    catch (...) {
        out->error = ERROR_NOT_ENOUGH_MEMORY;
    }
    return out->error;
}
}

// Expose the compiled-in launcher to the desktop and its regression tests; no
// DLL loading or plugin negotiation is involved.
uint32_t FinderLaunch(const FinderLaunchRequest* request, FinderLaunchResult* result)
{
    return launch(request, result);
}
