/*+===================================================================
  File:      image_desktop_fixture.cpp

  Summary:   Generate original HFS Desktop DB/DF records and custom volume artwork for mounted-volume tests.

  Classes:   Uses standard filesystem and byte containers.

  Functions: main

  Origin:    Executor direct Desktop metadata regression fixture; contains no Apple code.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include "rsys/desktop_database.h"
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
extern "C" {
#include "../../disk-images/third_party/hfsutils/hfs.h"
void romaw_hfs_medium(const char*, uint64_t, uint64_t);
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    namespace fs = std::filesystem;
    fs::path work(argv[1]);
    fs::create_directories(work);
    auto image = (work / "DesktopMetadata.hfv").string();
    constexpr size_t length = 2 * 1024 * 1024;
    {
        std::ofstream output(image, std::ios::binary);
        std::vector<char> zeros(length);
        output.write(zeros.data(), zeros.size());
        assert(output);
    }
    romaw_hfs_medium(image.c_str(), 0, length);
    assert(!hfs_format(image.c_str(), 0, 0, "Desktop Metadata", 0, nullptr));
    auto* volume = hfs_mount(image.c_str(), 0, HFS_MODE_RDWR);
    assert(volume && !hfs_mkdir(volume, ":Tools"));
    // A US English 'kind' resource for TEXT documents owned by Mtst.
    constexpr unsigned char kind[] = {'M', 't', 's', 't', 0,   0,   0,   0,   0,   1,   'T', 'E', 'X', 'T', 14,
                                      'M', 'o', 'u', 'n', 't', 'e', 'd', ' ', 'l', 'e', 't', 't', 'e', 'r', 0};
    constexpr unsigned char kind_header[] = {0, 0, 1, 0, 0, 0, 1, 34, 0, 0, 0, 34, 0, 0, 0, 50};
    std::vector<unsigned char> kind_fork(256);
    memcpy(kind_fork.data(), kind_header, sizeof kind_header);
    kind_fork.insert(kind_fork.end(), {0, 0, 0, sizeof kind});
    kind_fork.insert(kind_fork.end(), std::begin(kind), std::end(kind));
    kind_fork.insert(kind_fork.end(), std::begin(kind_header), std::end(kind_header));
    kind_fork.insert(kind_fork.end(), {0,   0, 0, 0, 0,  0, 0,   0,   0,   28, 0, 50, 0, 0, 'k', 'i', 'n',
                                       'd', 0, 0, 0, 10, 0, 128, 255, 255, 0,  0, 0,  0, 0, 0,   0,   0});
    auto* file = hfs_create(volume, ":Tools:Application", "APPL", "Mtst");
    assert(file && !hfs_setfork(file, 1));
    assert(hfs_write(file, kind_fork.data(), kind_fork.size()) == kind_fork.size() && !hfs_close(file));
    file = hfs_create(volume, ":Document", "TEXT", "Mtst");
    assert(file && hfs_write(file, "source text", 11) == 11 && !hfs_close(file));
    hfsdirent tools{}, document{};
    assert(!hfs_stat(volume, ":Tools", &tools) && !hfs_stat(volume, ":Document", &document));
    auto put32 = [](unsigned char* out, uint32_t value) {
        for (int i = 3; i >= 0; --i) {
            out[i] = value;
            value >>= 8;
        }
    };
    size_t size = 0;
    uint32_t tag = 0;
    auto root = work.string();
    assert(!romaw_desktop_database(root.c_str(), desktop_open, nullptr, 0, nullptr, &size, &tag));
    unsigned char icon_key[38] = {11, 1, 'M', 't', 's', 't', 'T', 'E', 'X', 'T', 1};
    std::array<unsigned char, 256> art;
    art.fill(0x55);
    size = art.size();
    assert(!romaw_desktop_database(root.c_str(), desktop_put, icon_key, 0, art.data(), &size, &tag));
    unsigned char app_key[38] = {7, 2, 'M', 't', 's', 't', 255, 255}, app[40]{};
    put32(app + 4, tools.cnid);
    app[8] = 11;
    memcpy(app + 9, "Application", 11);
    size = sizeof app;
    assert(!romaw_desktop_database(root.c_str(), desktop_put, app_key, 0, app, &size, &tag));
    unsigned char comment_key[38] = {5, 3}, comment[] = "\016source comment";
    put32(comment_key + 2, document.cnid);
    size = sizeof comment - 1;
    assert(!romaw_desktop_database(root.c_str(), desktop_put, comment_key, 0, comment, &size, &tag));
    for (const char* name : {"Desktop DB", "Desktop DF"}) {
        std::ifstream input(work / name, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
        file = hfs_create(volume, (":" + std::string(name)).c_str(), "BTFL", "DMGR");
        assert(file && hfs_write(file, bytes.data(), bytes.size()) == bytes.size() && !hfs_close(file));
    }
    // Original volume artwork in the three common source storage layouts.
    std::vector<unsigned char> icns = {'i', 'c', 'n', 's', 0, 0, 0, 0};
    const char* types[] = {"ICN#", "ics#", "icl4", "icl8", "ics4", "ics8"};
    const unsigned sizes[] = {256, 64, 512, 1024, 128, 256};
    auto append32 = [&](std::vector<unsigned char>& bytes, uint32_t value) {
        size_t at = bytes.size();
        bytes.resize(at + 4);
        put32(bytes.data() + at, value);
    };
    for (unsigned i = 0; i < 6; ++i) {
        icns.insert(icns.end(), types[i], types[i] + 4);
        append32(icns, sizes[i] + 8);
        icns.insert(icns.end(), sizes[i], 0x20 + i);
    }
    put32(icns.data() + 4, icns.size());
    std::vector<unsigned char> resource(256);
    put32(resource.data(), 256);
    put32(resource.data() + 4, 260 + icns.size());
    put32(resource.data() + 8, 4 + icns.size());
    put32(resource.data() + 12, 50);
    append32(resource, icns.size());
    resource.insert(resource.end(), icns.begin(), icns.end());
    size_t map_offset = resource.size();
    resource.resize(map_offset + 16);
    memcpy(resource.data() + map_offset, resource.data(), 16);
    resource.insert(resource.end(), 8, 0);
    const unsigned char map[] = {0,  28,   0,    50,  0,   0, 'i', 'c', 'n', 's', 0, 0, 0,
                                 10, 0xbf, 0xb9, 255, 255, 0, 0,   0,   0,   0,   0, 0, 0};
    resource.insert(resource.end(), std::begin(map), std::end(map));
    file = hfs_create(volume, ":Icon\r", "icon", "MACS");
    assert(file && !hfs_setfork(file, 1));
    assert(hfs_write(file, resource.data(), resource.size()) == resource.size() && !hfs_close(file));
    assert(!hfs_umount(volume));
    for (const char* variant : {"VolumeDataIcon.hfv", "VolumeDesktopIcon.hfv", "VolumeInvalidIcon.hfv"}) {
        auto copy = (work / variant).string();
        fs::copy_file(image, copy, fs::copy_options::overwrite_existing);
        romaw_hfs_medium(copy.c_str(), 0, length);
        volume = hfs_mount(copy.c_str(), 0, HFS_MODE_RDWR);
        assert(volume);
        if (std::string(variant) == "VolumeDesktopIcon.hfv")
            assert(!hfs_rename(volume, ":Icon\r", ":Desktop"));
        else {
            assert(!hfs_delete(volume, ":Icon\r"));
            file = hfs_create(volume, ":.VolumeIcon.icns", "icns", "MACS");
            if (std::string(variant) == "VolumeInvalidIcon.hfv")
                put32(icns.data() + 4, 7);
            assert(file && hfs_write(file, icns.data(), icns.size()) == icns.size() && !hfs_close(file));
        }
        assert(!hfs_umount(volume));
    }

    // Distinct layout fixture: initialized icon coordinates and independent folder bounds.
    auto layout = (work / "LayoutMetadata.hfv").string();
    fs::copy_file(image, layout, fs::copy_options::overwrite_existing);
    romaw_hfs_medium(layout.c_str(), 0, length);
    volume = hfs_mount(layout.c_str(), 0, HFS_MODE_RDWR);
    assert(volume);
    hfsdirent layout_root{};
    assert(!hfs_stat(volume, ":", &layout_root));
    layout_root.u.dir.rect = {80, 100, 280, 540};
    assert(!hfs_setattr(volume, ":", &layout_root));
    assert(!hfs_stat(volume, ":Tools", &tools));
    tools.u.dir.rect = {120, 150, 280, 430};
    tools.fdflags |= HFS_FNDR_HASBEENINITED;
    tools.fdlocation = {60, 320};
    assert(!hfs_setattr(volume, ":Tools", &tools));
    assert(!hfs_stat(volume, ":Document", &document));
    document.fdflags |= HFS_FNDR_HASBEENINITED;
    document.fdlocation = {60, 200};
    assert(!hfs_setattr(volume, ":Document", &document));
    assert(!hfs_umount(volume));

    return 0;
}
