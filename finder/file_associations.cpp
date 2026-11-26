/*+===================================================================
  File:      file_associations.cpp

  Summary:   Publish per-user Windows disk-image handlers and embedded icon references.

  Classes:   Key, SavedValue.

  Functions: finder::register_disk_image_types.

  Origin:    ROMAW native Finder integration with Windows registry and shell APIs.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include "finder/file_associations.h"
#include <shellapi.h>
#include <shlobj.h>
#include <system_error>
#include <stdexcept>
#include <utility>
#include <vector>
#include "image-file-icons/associations.inc"

namespace finder {
namespace {
void check(LSTATUS status)
{
    if (status != ERROR_SUCCESS)
        throw std::system_error(status, std::system_category(), "Cannot register disk image types");
}

struct Key {
    HKEY value = nullptr;

    Key(HKEY root, const std::wstring& path)
    {
        check(RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &value,
                              nullptr));
    }

    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;

    ~Key()
    {
        if (value)
            RegCloseKey(value);
    }
};

struct SavedValue {
    std::wstring path, name;
    DWORD type = REG_NONE;
    std::vector<BYTE> data;
    bool exists = false;
};
}

void register_disk_image_types(const std::filesystem::path& executable, HKEY root)
{
    std::wstring path = executable.native();
    if (path.empty()) {
        std::vector<wchar_t> buffer(32768);
        DWORD length = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
        if (!length || length >= buffer.size())
            throw std::system_error(length ? ERROR_INSUFFICIENT_BUFFER : GetLastError(), std::system_category());
        path.assign(buffer.data(), length);
    }
    if (!std::filesystem::path(path).is_absolute() || path.find(L'"') != std::wstring::npos)
        throw std::invalid_argument("ROMAW executable must have an absolute Windows path");

    std::vector<SavedValue> saved;
    auto write = [&](const std::wstring& key_path, const std::wstring& name, const std::wstring& text) {
        Key key(root, key_path);
        SavedValue previous{key_path, name, REG_NONE, {}, false};
        DWORD size = 0;
        LSTATUS status = RegQueryValueExW(key.value, name.c_str(), nullptr, &previous.type, nullptr, &size);
        if (status != ERROR_FILE_NOT_FOUND) {
            check(status);
            if (size > 16 * 1024 * 1024)
                throw std::runtime_error("Existing file association value is too large");
            previous.exists = true;
            previous.data.resize(size);
            check(RegQueryValueExW(key.value, name.c_str(), nullptr, &previous.type, previous.data.data(), &size));
            previous.data.resize(size);
        }
        saved.push_back(std::move(previous));
        check(RegSetValueExW(key.value, name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(text.c_str()),
                             DWORD((text.size() + 1) * sizeof(wchar_t))));
    };
    const std::wstring classes = L"Software\\Classes\\";
    const std::wstring capabilities = L"Software\\ROMAW\\DiskImages\\Capabilities";
    const std::wstring command = L"\"" + path + L"\" -mount \"%1\"";
    auto has_handler = [](HKEY base, const std::wstring& key_path) {
        HKEY key = nullptr;
        LSTATUS status = RegOpenKeyExW(base, key_path.c_str(), 0, KEY_QUERY_VALUE, &key);
        if (status == ERROR_FILE_NOT_FOUND)
            return false;
        check(status);
        DWORD size = 0;
        status = RegQueryValueExW(key, L"", nullptr, nullptr, nullptr, &size);
        RegCloseKey(key);
        if (status == ERROR_FILE_NOT_FOUND)
            return false;
        check(status);
        return size != 0;
    };
    try {
        for (const auto& type : disk_image_types) {
            const std::wstring program = L"ROMAW.DiskImage." + std::wstring(type.key);
            const std::wstring base = classes + program;
            const std::wstring icon = L"\"" + path + L"\",-" + std::to_wstring(type.resource);
            write(base, L"", L"Macintosh " + std::wstring(type.key) + L" disk image");
            write(base + L"\\DefaultIcon", L"", icon);
            write(base + L"\\shell\\open\\command", L"", command);
            write(base + L"\\Application", L"ApplicationName", L"ROMAW Disk Images");
            write(base + L"\\Application", L"ApplicationIcon", icon);
            if (std::wstring(type.key) == L"hfs")
                write(capabilities, L"ApplicationIcon", icon);
            for (const auto* extension : type.extensions) {
                if (!extension)
                    break;
                const std::wstring suffix = extension;
                if (!has_handler(root, classes + suffix) &&
                    (root != HKEY_CURRENT_USER || !has_handler(HKEY_CLASSES_ROOT, suffix)))
                    write(classes + suffix, L"", program);
                // Advertise the handler without changing another application's
                // extension default or Windows' protected UserChoice values.
                write(classes + suffix + L"\\OpenWithProgids", program, L"");
                write(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\" + suffix +
                          L"\\OpenWithProgids",
                      program, L"");
                write(capabilities + L"\\FileAssociations", suffix, program);
                write(classes + L"Applications\\ROMAW.exe\\SupportedTypes", suffix, L"");
            }
        }
        write(classes + L"Applications\\ROMAW.exe", L"FriendlyAppName", L"ROMAW Disk Images");
        write(classes + L"Applications\\ROMAW.exe\\shell\\open\\command", L"", command);
        write(L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\ROMAW.exe", L"", path);
        write(capabilities, L"ApplicationName", L"ROMAW Disk Images");
        write(capabilities, L"ApplicationDescription", L"Mount Macintosh disk images directly in ROMAW.");
        write(L"Software\\RegisteredApplications", L"ROMAW Disk Images", L"Software\\ROMAW\\DiskImages\\Capabilities");
    }
    catch (...) {
        // Restore every value already published if a later write fails. Empty
        // newly created keys may remain, but no partial handler is advertised.
        for (auto i = saved.rbegin(); i != saved.rend(); ++i) {
            HKEY key = nullptr;
            if (RegOpenKeyExW(root, i->path.c_str(), 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
                if (i->exists)
                    RegSetValueExW(key, i->name.c_str(), 0, i->type, i->data.data(), DWORD(i->data.size()));
                else
                    RegDeleteValueW(key, i->name.c_str());
                RegCloseKey(key);
            }
        }
        throw;
    }
    if (root == HKEY_CURRENT_USER)
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
}
