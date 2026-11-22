/*+===================================================================
  File:      info_test.cpp

  Summary:   Check Get Info's metadata precedence, live FTM fallback and safe four-character code display.

  Classes:   Uses MacFile and InfoCodes.

  Functions: romaw_profile_path, main.

  Origin:    Executor native Finder Get Info regression fixture; uses isolated original test data.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include "finder/info.h"
#include "rsys/user_profile.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
static std::string mapping_path;

extern "C" int romaw_profile_path(const char* leaf, char* output, size_t capacity, char*, size_t)
{
    assert(std::strcmp(leaf, "ROMAW.ftm") == 0);
    if (mapping_path.size() >= capacity)
        return 0;
    std::memcpy(output, mapping_path.c_str(), mapping_path.size() + 1);
    return 1;
}

int main()
{
    namespace fs = std::filesystem;
    auto work = fs::temp_directory_path() / (L"romaw-info-" + std::to_wstring(GetCurrentProcessId()));
    assert(fs::create_directory(work));
    mapping_path = (work / "test.ftm").u8string();
    auto write_map = [](const char* text) { std::ofstream(mapping_path, std::ios::binary) << text; };
    write_map("TXT,,TEXT,ttxt,x,x,Text\nTXT,,WRNG,WRNG,x,,Host only\n"
              "SCAN,,PICT,TEST,,,Scanned Picture\nPICT,,PICT,TEST,,,Mac Picture\nPICT,,PICT,TEST,,,\n"
              "PIC,,PICT,othr,,,Other Picture\n");
    finder::MacFile metadata;
    auto codes = finder::info_codes(L"sample.TXT", metadata, false, false);
    assert(codes.type == L"'TEXT'" && codes.creator == L"'ttxt'");
    assert(finder::info_kind(L"sample.TXT", metadata, false, false) == L"Host only");
    assert(finder::info_kind(L"sample.TXT", metadata, false, true) == L"document");
    metadata.type = 0x50494354;
    metadata.creator = 0x54455354;
    codes = finder::info_codes(L"sample.txt", metadata, false, false);
    assert(codes.type == L"'PICT'" && codes.creator == L"'TEST'");
    assert(finder::info_kind(L"sample.scan", metadata, false, true) == L"Scanned Picture");
    assert(finder::info_kind(L"sample.pic", metadata, false, true) == L"Mac Picture");
    assert(finder::info_kind(L"sample.pic", metadata, false, false, L"Custom Picture", L"Paint") == L"Custom Picture");
    assert(finder::info_kind(L"sample.pic", metadata, false, false, {}, L"Paint") == L"Mac Picture");
    metadata.creator = 0;
    codes = finder::info_codes(L"sample.txt", metadata, false, false);
    assert(codes.type == L"'PICT'" && codes.creator == L"--");
    assert(finder::info_kind(L"sample.pic", metadata, false, false, {}, L"Paint") == L"Paint document");
    metadata = {};
    metadata.has_finder_info = true;
    assert(finder::info_codes(L"sample.txt", metadata, false, false).type == L"--");
    assert(finder::info_kind(L"sample.txt", metadata, false, false) == L"document");
    metadata = {};
    assert(finder::info_codes(L"sample.txt", metadata, true, false).type == L"--");
    assert(finder::info_codes(L"sample.txt", metadata, false, true).type == L"--");
    metadata.fork = {0};
    assert(finder::info_codes(L"sample.txt", metadata, false, false).type == L"--");
    metadata = {};
    assert(finder::info_codes(L"unknown.foo", metadata, false, false).type == L"--");
    write_map("TXT,,BINA,TEST,x,x,Changed\n");
    codes = finder::info_codes(L"sample.txt", metadata, false, false);
    assert(codes.type == L"'BINA'" && codes.creator == L"'TEST'");
    assert(finder::info_kind(L"sample.txt", metadata, false, false) == L"Changed");
    assert(finder::info_kind(L"sample.txt", metadata, true, false) == L"folder");
    metadata.type = 0x41000a42;
    metadata.creator = 0x8e202020;
    codes = finder::info_codes(L"sample.txt", metadata, false, false);
    assert(codes.type == L"'A\\x00\\x0AB'" && codes.creator == L"'\u00e9   '");
    fs::remove(mapping_path);
    metadata = {};
    assert(finder::info_codes(L"sample.txt", metadata, false, false).type == L"--");
    // Finder 7 labels observed on the BasiliskII and Macintosh volumes.
    const std::pair<uint32_t, const wchar_t*> classic_kinds[] = {
        {0x4c57464e, L"system extension"},
        {0x6b66696c, L"keyboard layout"},
        {0x4646494c, L"font suitcase"},
        {0x6666696c, L"font"},
        {0x7466696c, L"font"},
        {0x7366696c, L"sound"},
        {0x5a535953, L"file"},
    };
    for (const auto& [type, kind] : classic_kinds) {
        metadata.type = type;
        assert(finder::info_kind(L"Sample", metadata, false, false) == kind);
    }
    assert(finder::info_memory_bytes(L"0") == 0);
    assert(finder::info_memory_bytes(L"0512") == 512 * 1024);
    assert(finder::info_memory_bytes(L"2096128") == 2047u * 1024 * 1024);
    for (const wchar_t* invalid : {L"", L"-1", L"30 K", L"4.5", L"2096129", L"9999999999"})
        assert(!finder::info_memory_bytes(invalid));
    fs::remove(work);
    std::cout << "PASS: Get Info codes, Kind precedence, live FTM, MacRoman and application-memory limits\n";
}
