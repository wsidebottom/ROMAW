/*+===================================================================
  File:      file_associations_test.cpp

  Summary:   Verify embedded Windows icons and native disk-image registration in a private registry tree.

  Classes:   RegistryFixture.

  Functions: main.

  Origin:    ROMAW native Finder Windows integration regression.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include "finder/file_associations.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <shellapi.h>
#include <vector>
#include "image-file-icons/associations.inc"

struct RegistryFixture {
    std::wstring path = L"Software\\ROMAW-ImageAssociationTest-" + std::to_wstring(GetCurrentProcessId());
    HKEY root = nullptr;

    RegistryFixture()
    {
        assert(RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &root,
                               nullptr) == ERROR_SUCCESS);
    }

    ~RegistryFixture()
    {
        RegCloseKey(root);
        RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    }

    void set(const std::wstring& key, const wchar_t* name, const std::wstring& value)
    {
        HKEY handle;
        assert(RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &handle, nullptr) ==
               ERROR_SUCCESS);
        assert(RegSetValueExW(handle, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                              DWORD((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
        RegCloseKey(handle);
    }

    std::wstring get(const std::wstring& key, const wchar_t* name = L"")
    {
        HKEY handle;
        assert(RegOpenKeyExW(root, key.c_str(), 0, KEY_QUERY_VALUE, &handle) == ERROR_SUCCESS);
        DWORD size = 0, type = 0;
        assert(RegQueryValueExW(handle, name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS && type == REG_SZ);
        std::vector<wchar_t> value(size / sizeof(wchar_t));
        assert(RegQueryValueExW(handle, name, nullptr, nullptr, reinterpret_cast<BYTE*>(value.data()), &size) ==
               ERROR_SUCCESS);
        RegCloseKey(handle);
        assert(!value.empty() && value.back() == 0);
        return value.data();
    }
};

static unsigned word(const unsigned char* p)
{
    return p[0] | (unsigned(p[1]) << 8);
}

static unsigned dword(const unsigned char* p)
{
    return word(p) | (word(p + 2) << 16);
}

static HMODULE icon_module = nullptr;

static std::vector<unsigned char> resource(unsigned type, unsigned id)
{
    auto module = icon_module;
    auto found = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(type));
    assert(found);
    auto data = static_cast<const unsigned char*>(LockResource(LoadResource(module, found)));
    assert(data);
    return {data, data + SizeofResource(module, found)};
}

int main(int argc, char** argv)
{
    assert(argc == 2 || argc == 3);
    std::vector<wchar_t> module_name(32768);
    DWORD module_length = GetModuleFileNameW(nullptr, module_name.data(), DWORD(module_name.size()));
    assert(module_length && module_length < module_name.size());
    const std::filesystem::path module_path =
        argc == 3 ? std::filesystem::path(argv[2])
                  : std::filesystem::path(std::wstring(module_name.data(), module_length));
    icon_module =
        LoadLibraryExW(module_path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    assert(icon_module);
    unsigned extensions = 0;
    RegistryFixture registry;
    registry.set(L"Software\\Classes\\.dmg", L"", L"Other.DMG");
    const auto user_choice = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.iso\\UserChoice";
    registry.set(user_choice, L"ProgId", L"Other.ISO");
    const std::wstring executable = L"D:\\ROMAW space é\\ROMAW.exe";
    finder::register_disk_image_types(executable, registry.root);
    finder::register_disk_image_types(executable, registry.root);
    assert(registry.get(L"Software\\Classes\\.dmg") == L"Other.DMG");
    assert(registry.get(user_choice, L"ProgId") == L"Other.ISO");
    for (const auto& type : disk_image_types) {
        const auto base = L"Software\\Classes\\ROMAW.DiskImage." + std::wstring(type.key);
        assert(registry.get(base + L"\\DefaultIcon") == L"\"" + executable + L"\",-" + std::to_wstring(type.resource));
        assert(registry.get(base + L"\\shell\\open\\command") == L"\"" + executable + L"\" -mount \"%1\"");
        for (const auto* extension : type.extensions) {
            if (!extension)
                break;
            ++extensions;
            registry.get(L"Software\\Classes\\" + std::wstring(extension) + L"\\OpenWithProgids",
                         (L"ROMAW.DiskImage." + std::wstring(type.key)).c_str());
        }
        auto group = resource(14, type.resource);
        HICON large = nullptr, small = nullptr;
        assert(ExtractIconExW(module_path.c_str(), -int(type.resource), &large, &small, 1) == 2);
        assert(large && small);
        DestroyIcon(large);
        DestroyIcon(small);
        assert(group.size() == 34 && word(group.data()) == 0 && word(group.data() + 2) == 1 &&
               word(group.data() + 4) == 2);
        std::ifstream input(std::filesystem::path(argv[1]) / (std::wstring(type.key) + L".ico"), std::ios::binary);
        std::vector<unsigned char> ico((std::istreambuf_iterator<char>(input)), {});
        assert(ico.size() > 38);
        for (unsigned i = 0; i < 2; ++i) {
            const auto* icon_entry = ico.data() + 6 + i * 16;
            const auto* group_entry = group.data() + 6 + i * 14;
            assert(icon_entry[0] == group_entry[0] && icon_entry[1] == group_entry[1]);
            auto pixels = resource(3, word(group_entry + 12));
            auto offset = dword(icon_entry + 12), size = dword(icon_entry + 8);
            assert(size == pixels.size() && offset + size <= ico.size());
            assert(std::equal(pixels.begin(), pixels.end(), ico.begin() + offset));
        }
    }
    assert(extensions == 10);
    const auto first_icon = registry.get(L"Software\\Classes\\ROMAW.DiskImage.hfs\\DefaultIcon");
    registry.set(L"Software\\ROMAW\\DiskImages\\Capabilities", L"ApplicationDescription",
                 std::wstring(9 * 1024 * 1024, L'x'));
    bool failed = false;
    try {
        finder::register_disk_image_types(L"D:\\Replacement\\ROMAW.exe", registry.root);
    }
    catch (const std::exception&) {
        failed = true;
    }
    assert(failed && registry.get(L"Software\\Classes\\ROMAW.DiskImage.hfs\\DefaultIcon") == first_icon);
    FreeLibrary(icon_module);
    std::cout
        << "PASS: six embedded icon families, ten extensions, preserved defaults, repeat registration and rollback\n";
}
