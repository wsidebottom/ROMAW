/*+===================================================================
  File:      info.cpp

  Summary:   Format Macintosh codes for Get Info and infer missing DOS metadata through ROMAW's current FTM.

  Classes:   Uses InfoCodes, MacFile and FileTypeMapping.

  Functions: finder::info_codes, finder::info_kind, finder::info_memory_bytes.

  Origin:    Executor native Finder Get Info presentation.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include "finder/info.h"
#include "finder/launcher.h"
#include "rsys/filetypemap.h"
#include "rsys/archive_types.h"
#include "rsys/memsize.h"

namespace finder {
namespace {
bool plain_host_file(const MacFile& metadata, bool image)
{
    return !image && !metadata.has_finder_info && !metadata.type && !metadata.creator && !metadata.kind &&
           metadata.fork.empty();
}

// Four-character codes are MacRoman bytes, not UTF-8 or host-endian strings.
// Preserve spaces; escape controls so a malformed code cannot break the layout.
std::wstring display_code(uint32_t code)
{
    if (!code)
        return L"--";
    std::wstring text = L"'";
    for (int shift = 24; shift >= 0; shift -= 8) {
        unsigned char byte = code >> shift;
        if (byte < 32 || byte == 127) {
            constexpr wchar_t hex[] = L"0123456789ABCDEF";
            text += L"\\x";
            text += hex[byte >> 4];
            text += hex[byte & 15];
        }
        else {
            wchar_t character;
            char encoded = char(byte);
            if (MultiByteToWideChar(10000, 0, &encoded, 1, &character, 1))
                text += character;
        }
    }
    return text + L"'";
}
}

InfoCodes info_codes(const fs::path& path, const MacFile& metadata, bool directory, bool image)
{
    if (directory)
        return {L"--", L"--"};
    uint32_t type = metadata.type, creator = metadata.creator;
    if (plain_host_file(metadata, image)) {
        FileTypeMapping mapping{};
        if (filetype_map_lookup_path(path.extension().u8string().c_str(), &mapping)) {
            type = mapping.type;
            creator = mapping.creator;
        }
    }
    return {display_code(type), display_code(creator)};
}

std::wstring info_kind(const fs::path& path, const MacFile& metadata, bool directory, bool image,
                       const std::wstring& custom, const std::wstring& owner)
{
    if (directory)
        return L"folder";
    int archive = romaw_archive_extension(path.extension().u8string().c_str());
    if (!archive)
        archive = romaw_archive_type(metadata.type);
    if (archive) {
        const char* description = romaw_archive_description(archive);
        return std::wstring(description, description + strlen(description));
    }
    switch (metadata.kind) {
    case FINDER_CLASSIC_APPLICATION:
        return L"application program";
    case FINDER_DESK_ACCESSORY:
        return L"desk accessory";
    case FINDER_CONTROL_PANEL:
        return L"control panel";
    }
    if (metadata.flags & 0x8000)
        return L"alias";
    if (!custom.empty())
        return custom;
    auto extension = metadata.type || plain_host_file(metadata, image) ? path.extension().u8string() : std::string{};
    char description[1024];
    if ((metadata.type || !extension.empty()) &&
        filetype_map_description_path(extension.c_str(), metadata.type, metadata.creator, description,
                                      sizeof description)) {
        unsigned encoding = CP_UTF8;
        int length = MultiByteToWideChar(encoding, MB_ERR_INVALID_CHARS, description, -1, nullptr, 0);
        if (!length) {
            encoding = CP_ACP;
            length = MultiByteToWideChar(encoding, 0, description, -1, nullptr, 0);
        }
        if (length > 1) {
            std::wstring text(length, L'\0');
            MultiByteToWideChar(encoding, 0, description, -1, text.data(), length);
            text.pop_back();
            return text;
        }
    }
    if (!owner.empty())
        return owner + L" document";
    switch (metadata.type) {
    case 0x5a535953: // ZSYS
        return L"file";
    case 0x494e4954: // INIT
    case 0x4c57464e: // LWFN
        return L"system extension";
    case 0x73686c62: // shlb
        return L"shared library";
    case 0x4646494c: // FFIL
        return L"font suitcase";
    case 0x6666696c: // ffil
    case 0x7466696c: // tfil
        return L"font";
    case 0x7a737973: // zsys
        return L"suitcase";
    case 0x7366696c: // sfil
        return L"sound";
    case 0x6b66696c: // kfil
        return L"keyboard layout";
    case 0x54455854: // TEXT
        return L"text document";
    default:
        return L"document";
    }
}

std::optional<uint32_t> info_memory_bytes(const std::wstring& kilobytes)
{
    if (kilobytes.empty())
        return {};
    constexpr uint32_t maximum = MAX_APPLZONE_SIZE / 1024;
    uint32_t value = 0;
    for (auto character : kilobytes) {
        if (character < L'0' || character > L'9')
            return {};
        uint32_t digit = character - L'0';
        if (value > (maximum - digit) / 10)
            return {};
        value = value * 10 + digit;
    }
    return value * 1024;
}
}
