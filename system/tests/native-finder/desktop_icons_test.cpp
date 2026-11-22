/*+===================================================================
  File:      desktop_icons_test.cpp

  Summary:   Actual Windows filesystem, resource parser and classic DB/DF storage. No Apple binaries are needed: bundles
             deliberately use nonzero local icon IDs.

  Classes:   Declared: Reference; Used: Copies, DesktopIcons, FinderIcon, Reference, color, std::map, std::ofstream,
             std::pair, std::vector

  Functions: main

  Origin:    Existing repository source in executor/tests/native-finder; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

// Actual Windows filesystem, resource parser and classic DB/DF storage. No
// Apple binaries are needed: bundles deliberately use nonzero local icon IDs.
#include "finder/desktop_icons.h"
#include "assets.inc"
#include "rsys/desktop_database.h"
#include "rsys/mac_storage.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
using namespace finder;

namespace {
void put(Bytes& bytes, size_t at, uint32_t value, unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
        bytes.at(at + i) = value >> (8 * (count - 1 - i));
}

void write(const fs::path& path, const Bytes& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    assert(out.good());
}

using Resource = std::pair<int, Bytes>;

Bytes fork(unsigned color, bool custom = false)
{
    std::map<uint32_t, std::vector<Resource>> resources;
    Bytes bundle(36);
    put(bundle, 0, 0x54455354, 4);
    put(bundle, 6, 1, 2);
    put(bundle, 8, 0x49434e23, 4);
    put(bundle, 12, 1, 2);
    put(bundle, 14, 9, 2);
    put(bundle, 16, 501, 2);
    put(bundle, 18, 42, 2);
    put(bundle, 20, 502, 2);
    put(bundle, 22, 0x46524546, 4);
    put(bundle, 26, 1, 2);
    put(bundle, 28, 0, 2);
    put(bundle, 30, 128, 2);
    put(bundle, 32, 1, 2);
    put(bundle, 34, 129, 2);
    resources[0x424e444c].push_back({128, bundle});
    Bytes ref(7);
    put(ref, 0, 0x54455854, 4);
    put(ref, 4, 42, 2);
    resources[0x46524546].push_back({129, ref});
    put(ref, 0, 0x4150504c, 4);
    put(ref, 4, 9, 2);
    resources[0x46524546].push_back({128, ref});
    for (unsigned i = 0; i < 6; ++i) {
        Bytes icon(finder_icon_sizes[i], color);
        if (i < 2)
            std::fill(icon.begin() + icon.size() / 2, icon.end(), 255);
        resources[finder_icon_types[i]].push_back({custom ? -16455 : 502, icon});
        if (!custom)
            resources[finder_icon_types[i]].push_back({501, icon});
    }
    Bytes bytes(256);

    struct Reference {
        int id;
        size_t offset;
    };

    std::map<uint32_t, std::vector<Reference>> refs;
    for (const auto& type : resources)
        for (const auto& resource : type.second) {
            size_t offset = bytes.size();
            bytes.resize(offset + 4 + resource.second.size());
            put(bytes, offset, resource.second.size(), 4);
            std::copy(resource.second.begin(), resource.second.end(), bytes.begin() + offset + 4);
            refs[type.first].push_back({resource.first, offset - 256});
        }
    size_t map = bytes.size(), types = map + 28, refpos = types + 2 + refs.size() * 8;
    size_t count = 0;
    for (const auto& type : refs)
        count += type.second.size();
    bytes.resize(refpos + count * 12);
    put(bytes, 0, 256, 4);
    put(bytes, 4, map, 4);
    put(bytes, 8, map - 256, 4);
    put(bytes, 12, bytes.size() - map, 4);
    std::copy_n(bytes.begin(), 16, bytes.begin() + map);
    put(bytes, map + 24, 28, 2);
    put(bytes, map + 26, bytes.size() - map, 2);
    put(bytes, types, refs.size() - 1, 2);
    size_t index = types + 2;
    for (const auto& type : refs) {
        put(bytes, index, type.first, 4);
        put(bytes, index + 4, type.second.size() - 1, 2);
        put(bytes, index + 6, refpos - types, 2);
        index += 8;
        for (const auto& ref : type.second) {
            put(bytes, refpos, ref.id, 2);
            put(bytes, refpos + 2, 65535, 2);
            put(bytes, refpos + 5, ref.offset, 3);
            refpos += 12;
        }
    }
    return bytes;
}

void metadata(const fs::path& path, bool app, unsigned color = 1, bool custom = false)
{
    auto resource = app ? fork(color, custom) : Bytes();
    Bytes bytes(82 + resource.size());
    put(bytes, 0, 0x00051607, 4);
    put(bytes, 4, 0x00020000, 4);
    put(bytes, 24, 2, 2);
    put(bytes, 26, 9, 4);
    put(bytes, 30, 50, 4);
    put(bytes, 34, 32, 4);
    put(bytes, 38, 2, 4);
    put(bytes, 42, 82, 4);
    put(bytes, 46, resource.size(), 4);
    put(bytes, 50, app ? 0x4150504c : 0x54455854, 4);
    put(bytes, 54, 0x54455354, 4);
    put(bytes, 58, app ? 0x2000 : 0, 2);
    std::copy(resource.begin(), resource.end(), bytes.begin() + 82);
    write(sidecar(path), bytes);
}

void check(const FinderIcon& icon, unsigned color)
{
    assert(icon.present == 63);
    assert(icon.mono[0] == color && icon.mono[128] == 255);
    assert(icon.small[0] == color && icon.small[32] == 255);
    assert(icon.color4[0] == color && icon.color8[0] == color);
    assert(icon.small4[0] == color && icon.small8[0] == color);
}
}

int main()
{
    // Both system Trash families must actually be embedded and decode into
    // different images; selecting a second name for the same artwork is not enough.
    FinderIcon empty_trash{}, full_trash{};
    assert(finder_parse_icns_icon(assets[3].data, assets[3].size, &empty_trash));
    assert(finder_parse_icns_icon(assets[full_trash_asset].data, assets[full_trash_asset].size, &full_trash));
    unsigned char empty_pixels[32 * 32 * 4], full_pixels[32 * 32 * 4];
    finder_icon_rgba(&empty_trash, empty_pixels);
    finder_icon_rgba(&full_trash, full_pixels);
    assert(memcmp(empty_pixels, full_pixels, sizeof empty_pixels) != 0);
    std::cout << "PASS: distinct embedded empty/full Trash artwork decodes through the shared icon parser\n";

    auto base = fs::temp_directory_path() / (L"finder-desktop-icons-" + std::to_wstring(GetCurrentProcessId()));
    auto root = base / L"Volume \u03a9", nested = root / L"Folder", outside = base / L"Outside";
    fs::create_directories(nested);
    fs::create_directories(outside);
    auto app = nested / L"Owner", doc = nested / L"Document";
    write(app, {1});
    write(doc, {2});
    metadata(app, true, 7);
    metadata(doc, false);
    auto info = mac_file(doc);
    assert(info.fork.empty());
    FinderIcon icon{};
    {
        DesktopIcons service({base, root});
        service.scan(root);
        service.wait_idle();
        assert(fs::exists(root / L"Desktop DB") && fs::exists(root / L"Desktop DF"));
        auto db_info = mac_file(root / L"Desktop DB"), df_info = mac_file(root / L"Desktop DF");
        assert(db_info.type == 0x4254464c && df_info.type == 0x4454464c);
        assert(db_info.creator == 0x444d4752 && df_info.creator == 0x444d4752);
        assert((db_info.flags & 0x4000) && (df_info.flags & 0x4000));
        assert(service.lookup(doc, info, icon));
        check(icon, 7);
        // Verify all six documented Desktop Manager icon-type keys directly.
        const unsigned kinds[] = {1, 4, 2, 3, 5, 6};
        for (unsigned i = 0; i < 6; ++i) {
            unsigned char key[38] = {11, 1, 'T', 'E', 'S', 'T', 'T', 'E', 'X', 'T', 0};
            key[10] = kinds[i];
            Bytes data(finder_icon_sizes[i]);
            size_t size = data.size();
            uint32_t tag = 0;
            assert(!romaw_desktop_database_wide(root.c_str(), desktop_get, key, 0, data.data(), &size, &tag));
            assert(size == data.size() && data[0] == 7);
        }
        auto length = fs::file_size(root / L"Desktop DF");
        auto generation = service.generation();
        service.scan(root);
        service.wait_idle();
        assert(fs::file_size(root / L"Desktop DF") == length && service.generation() == generation);
        // A fresh reader must obtain the artwork without opening its owner.
        fs::rename(sidecar(app), outside / L"Saved fork");
        {
            DesktopIcons reader({root});
            assert(reader.lookup(doc, info, icon));
            check(icon, 7);
        }
        fs::rename(outside / L"Saved fork", sidecar(app));
        // Preserve unrelated classic comments while updating registered artwork.
        unsigned char comment[38] = {5, 3, 0, 0, 0, 2};
        unsigned char text[] = {3, 'K', 'e', 'e'};
        size_t size = sizeof text;
        uint32_t tag = 0;
        assert(!romaw_desktop_database_wide(root.c_str(), desktop_put, comment, 0, text, &size, &tag));
        metadata(app, true, 9);
        fs::last_write_time(sidecar(app), fs::last_write_time(sidecar(app)) + std::chrono::seconds(2));
        service.poll();
        service.wait_idle();
        assert(service.lookup(doc, info, icon));
        check(icon, 9);
        unsigned char read[4]{};
        size = sizeof read;
        assert(!romaw_desktop_database_wide(root.c_str(), desktop_get, comment, 0, read, &size, &tag));
        assert(!memcmp(read, text, 4));
        // Deleting either half or both halves triggers real volume discovery.
        for (unsigned which = 0; which < 3; ++which) {
            if (which != 0)
                fs::remove(root / L"Desktop DB");
            if (which != 1)
                fs::remove(root / L"Desktop DF");
            service.poll();
            service.wait_idle();
            assert(service.lookup(doc, info, icon));
            check(icon, 9);
            if (!which) {
                size = sizeof read;
                assert(!romaw_desktop_database_wide(root.c_str(), desktop_get, comment, 0, read, &size, &tag));
                assert(!memcmp(read, text, 4));
            }
        }
        assert(!fs::exists(base / L"Desktop DB"));
    }
    {
        DesktopIcons limited({root});
        limited.scan(outside);
        limited.wait_idle();
        assert(!fs::exists(outside / L"Desktop DB") && !fs::exists(outside / L".Desktop.lock"));
    }
    // Corrupt databases are preserved, while source bundles still supply icons.
    write(root / L"Desktop DB", {0, 1, 2});
    {
        DesktopIcons service({root});
        service.scan(root);
        service.wait_idle();
        assert(service.lookup(doc, info, icon));
        check(icon, 9);
        assert(fs::file_size(root / L"Desktop DB") == 3);
    }
    // Basilisk/SheepShaver volumes retain split metadata/forks through reading,
    // database registration, labels and comments, then convert copies to OS X.
    auto legacy = base / L"Split volume", destination = base / L"Destination";
    fs::create_directories(legacy / L".finf");
    fs::create_directories(legacy / L".rsrc");
    fs::create_directory(destination);
    auto owner = legacy / L"Owner", document = legacy / L"Document";
    write(owner, {1});
    write(document, {2});
    Bytes finfo(32);
    put(finfo, 0, 0x4150504c, 4);
    put(finfo, 4, 0x54455354, 4);
    put(finfo, 8, 0x2000, 2);
    finfo[24] = 0x80;
    write(legacy / L".finf/Owner", finfo);
    write(legacy / L".rsrc/Owner", fork(13));
    metadata(document, false);
    {
        DesktopIcons service({legacy});
        service.scan(legacy);
        service.wait_idle();
        assert(service.lookup(document, mac_file(document), icon));
        check(icon, 13);
    }
    auto catalog = mac_file(owner);
    assert(catalog.type == 0x4150504c && catalog.creator == 0x54455354 && !catalog.fork.empty());
    assert(hidden_metadata(legacy / L".finf") && hidden_metadata(legacy / L".rsrc"));
    set_label(owner, 3);
    assert(!fs::exists(sidecar(owner)) && ((mac_file(owner).flags >> 1) & 7) == 3);
    assert(read_bytes(legacy / L".finf/Owner")[24] == 0x80);
    set_comment(owner, L"Split forks retained");
    set_label(owner, 4);
    assert(mac_file(owner).comment == L"Split forks retained" && mac_file(owner).fork == fork(13));
    assert(((read_bytes(legacy / L".finf/Owner")[9] >> 1) & 7) == 4);
    Copies copies;
    auto job = copies.start({owner}, destination);
    job->worker.join();
    assert(job->state == CopyJob::complete);
    auto copied = job->destinations.front();
    assert(mac_file(copied).fork == fork(13));
    assert(mac_file(copied).comment == L"Split forks retained");
    auto copied_metadata = read_bytes(sidecar(copied));
    ROMAWMacStorageEntry copied_info;
    assert(romaw_mac_storage_find(copied_metadata.data(), copied_metadata.size(), copied_metadata.size(), 9, 0,
                                  &copied_info));
    assert(copied_metadata[copied_info.offset + 24] == 0x80);
    assert(!fs::exists(destination / L".finf") && !fs::exists(destination / L".rsrc"));
    auto renamed = destination / L"Renamed";
    HANDLE locked =
        CreateFileW(sidecar(copied).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    assert(locked != INVALID_HANDLE_VALUE);
    bool rejected = false;
    try {
        rename_item(copied, renamed);
    }
    catch (...) {
        rejected = true;
    }
    assert(rejected && fs::exists(copied) && !fs::exists(renamed));
    assert(mac_file(copied).fork == fork(13));
    CloseHandle(locked);
    rename_item(copied, renamed);
    assert(mac_file(renamed).fork == fork(13) && !fs::exists(copied));
    assert(!fs::exists(destination / L".finf" / copied.filename()));
    assert(!fs::exists(destination / L".rsrc" / copied.filename()));
    // SMB maps the Icon<CR> name to U+F00D. Its resource-only AppleDouble
    // companion must work without a data fork, and changes must expire caches.
    auto custom = base / L"SMB custom folder";
    fs::create_directory(custom);
    auto mapped = custom / L"Icon\uf00d", escaped = custom / L"Icon%0D";
    metadata(mapped, true, 17, true);
    assert(!fs::exists(mapped));
    assert(custom_folder_icon(custom, icon));
    check(icon, 17);
    auto signature = icon_source_stamp(custom, true);
    metadata(mapped, true, 18, true);
    fs::last_write_time(sidecar(mapped), fs::last_write_time(sidecar(mapped)) + std::chrono::seconds(2));
    assert(icon_source_stamp(custom, true) != signature);
    assert(custom_folder_icon(custom, icon));
    check(icon, 18);
    write(sidecar(escaped), {1, 2, 3});
    assert(custom_folder_icon(custom, icon));
    check(icon, 18);
    metadata(escaped, true, 9, true);
    assert(custom_folder_icon(custom, icon));
    check(icon, 9);
    fs::remove(sidecar(escaped));
    signature = icon_source_stamp(custom, true);
    fs::remove(sidecar(mapped));
    assert(icon_source_stamp(custom, true) != signature);
    assert(!custom_folder_icon(custom, icon));
    fs::create_directory(custom / L".rsrc");
    write(custom / L".rsrc" / mapped.filename(), fork(13, true));
    assert(custom_folder_icon(custom, icon));
    check(icon, 13);
    // Hidden helper names remain physically copyable and retain folder artwork.
    write(mapped, {});
    assert(hidden_metadata(mapped) && hidden_metadata(escaped));
    auto folder_copy = copies.start({custom}, destination);
    folder_copy->worker.join();
    assert(folder_copy->state == CopyJob::complete);
    const auto& copied_folder = folder_copy->destinations.front();
    assert(fs::exists(copied_folder / mapped.filename()));
    assert(mac_file(copied_folder / mapped.filename()).fork == fork(13, true));
    assert(fs::exists(sidecar(copied_folder / mapped.filename())));
    assert(!fs::exists(copied_folder / L".rsrc"));
    assert(custom_folder_icon(copied_folder, icon));
    check(icon, 13);
    auto original = mac_file(renamed);
    auto original_resource = mac_resource(original, 0x49434e23);
    set_host_custom_icon(renamed, false, &icon);
    auto edited = mac_file(renamed);
    FinderIcon edited_icon{};
    assert((edited.flags & 0x400) && finder_parse_custom_icon(edited.fork.data(), edited.fork.size(), &edited_icon));
    check(edited_icon, 13);
    assert(edited.type == original.type && edited.creator == original.creator && edited.comment == original.comment);
    set_host_custom_icon(renamed, false, nullptr);
    edited = mac_file(renamed);
    assert(!(edited.flags & 0x400) && !finder_parse_custom_icon(edited.fork.data(), edited.fork.size(), &edited_icon));
    assert(mac_resource(edited, 0x49434e23) == original_resource);
    auto plain = base / L"Folder with new artwork";
    fs::create_directory(plain);
    set_host_custom_icon(plain, true, &icon);
    assert(custom_folder_icon(plain, edited_icon) && (mac_file(plain).flags & 0x400));
    check(edited_icon, 13);
    set_host_custom_icon(plain, true, nullptr);
    assert(!custom_folder_icon(plain, edited_icon) && !(mac_file(plain).flags & 0x400));
    set_host_custom_icon(copied_folder, true, nullptr);
    assert(!custom_folder_icon(copied_folder, edited_icon));
    fs::remove_all(base);
    std::cout
        << "PASS: native Desktop DB/DF persistence, six icon types, ownerless documents, updates, deletion rebuilds, Unicode, comments and corrupt-file preservation\n";
}
