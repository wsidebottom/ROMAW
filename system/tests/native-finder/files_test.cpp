/*+===================================================================
  File:      files_test.cpp

  Summary:   Exercise the real Windows copy backend with temporary data/forks. Progress callbacks hold workers at
             controlled points to prove overlap and independent cancellation.

  Classes:   Used: Copies, CopyJob, MacFile, std::atomic, std::exception, std::ofstream, std::shared_ptr, std::wstring

  Functions: finder::copy_test_staged_moves, finder::copy_test_before_publish, finder::copy_test_progress, main

  Origin:    Existing repository source in executor/tests/native-finder; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

// Exercise the real Windows copy backend with temporary data/forks. Progress
// callbacks hold workers at controlled points to prove overlap and independent
// cancellation.

#include "finder/files.h"
#include "finder/shortcut_icon.h"
#include <winioctl.h>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <shellapi.h>
#include <objbase.h>

using namespace finder;
std::atomic<bool> hold_copies{false}, staged_moves{false}, hold_publication{false}, publication_waiting{false};
std::atomic<bool> lock_publication{false};
std::thread publication_release;
// Fail promptly in unattended Windows runs instead of opening a WER dialog.
#undef assert
#define assert(condition)                                                                  \
    do {                                                                                   \
        if (!(condition)) {                                                                \
            std::cerr << __FILE__ << ":" << __LINE__ << ": " << #condition << " failed\n"; \
            std::exit(2);                                                                  \
        }                                                                                  \
    } while (false)

namespace finder {
bool copy_test_staged_moves()
{
    return staged_moves;
}

void copy_test_before_publish(CopyJob& job)
{
    if (lock_publication.exchange(false)) {
        HANDLE lock = INVALID_HANDLE_VALUE;
        for (const auto& entry : fs::directory_iterator(job.destinations.front().parent_path()))
            if (entry.is_directory() && entry.path().filename().wstring().rfind(L".Finder-copy-", 0) == 0) {
                auto payload = entry.path() / L"data";
                if (fs::is_directory(payload)) {
                    lock = CreateFileW(payload.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
                    break;
                }
            }
        assert(lock != INVALID_HANDLE_VALUE);
        publication_release = std::thread([lock] {
            Sleep(70);
            CloseHandle(lock);
        });
    }
    if (hold_publication) {
        publication_waiting = true;
        while (hold_publication && !job.stop)
            Sleep(1);
    }
}

void copy_test_progress(CopyJob& job)
{
    while (hold_copies && !job.stop)
        Sleep(1);
}
}

namespace {
void put32(Bytes& b, size_t p, uint32_t n)
{
    for (unsigned i = 0; i < 4; ++i)
        b.at(p + i) = n >> (24 - i * 8);
}

void put16(Bytes& b, size_t p, unsigned n)
{
    b.at(p) = n >> 8;
    b.at(p + 1) = n;
}

void write(const fs::path& p, const Bytes& b)
{
    std::ofstream f(p, std::ios::binary);
    assert(f);
    f.write(reinterpret_cast<const char*>(b.data()), b.size());
    assert(f);
}

void check_shortcut_icons(const fs::path& root)
{
    SuitcaseFork fork;
    fork.resources.push_back({{0x46524546, 128}, 0, {}, {'A', 'P', 'P', 'L', 0, 0}});
    fork.resources.push_back({{0x424e444c, 128}, 0, {},
                             {'T', 'E', 'S', 'T', 0, 0, 0, 0, 'I', 'C', 'N', '#', 0, 0, 0, 0, 0, 128}});
    for (unsigned i = 0; i < 6; ++i)
        fork.resources.push_back({{finder_icon_types[i], 128}, 0, {}, Bytes(finder_icon_sizes[i], 0x80)});
    fork.resources.push_back({{0x69636823, 128}, 0, {}, Bytes(576, 0x80)}); // ich#
    fork.resources.push_back({{0x69636838, 128}, 0, {}, Bytes(2304, 0x7f)}); // ich8
    fork.resources.push_back({{0x696c3332, 128}, 0, {}, Bytes(4096, 0x40)}); // il32
    fork.resources.push_back({{0x6c386d6b, 128}, 0, {}, Bytes(1024, 0xa0)}); // l8mk
    // A document icon in the same fork must not become a shortcut representation.
    fork.resources.push_back({{0x69636d23, 149}, 0, {}, Bytes(48, 0xff)});
    MacFile application;
    application.type = 0x4150504c;
    application.creator = 0x54455354;
    application.fork = encode_suitcase_fork(fork);
    auto path = root / L"Macintosh Application.ico";
    assert(save_application_icon(application, path));
    assert(!save_application_icon(application, path)); // Existing artwork remains intact.
    auto ico = read_bytes(path);
    auto word = [&](size_t at) { return unsigned(ico.at(at)) | unsigned(ico.at(at + 1)) << 8; };
    auto dword = [&](size_t at) { return word(at) | word(at + 2) << 16; };
    assert(word(0) == 0 && word(2) == 1 && word(4) == 9);
    std::set<std::pair<unsigned, unsigned>> formats;
    for (unsigned i = 0; i < word(4); ++i) {
        const size_t entry = 6 + 16 * i;
        unsigned width = ico.at(entry), depth = word(entry + 6);
        size_t image = dword(entry + 12), length = dword(entry + 8);
        assert(image >= 6 + 16 * word(4) && image + length <= ico.size());
        assert(dword(image) == 40 && dword(image + 4) == width && dword(image + 8) == 2 * width);
        assert(word(image + 14) == depth);
        formats.emplace(width, depth);
        if (width == 32 && depth == 32)
            assert(ico.at(image + 43) == 0xa0);
        if (width == 16 && depth == 4) {
            // Preserve the opaque index and write black under the AND bit.
            unsigned packed = ico.at(image + 40 + 64);
            assert((packed >> 4) == 8);
            size_t clear_color = image + 40 + (packed & 15) * 4;
            assert(dword(clear_color) == 0);
            assert((ico.at(image + 40 + 64 + 8 * 16) & 0xc0) == 0x40);
        }
    }
    const std::set<std::pair<unsigned, unsigned>> expected{
        {16, 1}, {16, 4}, {16, 8}, {32, 1}, {32, 4}, {32, 8}, {32, 32}, {48, 1}, {48, 8}};
    assert(formats == expected);
    std::cout << "PASS shortcut ICO preserves Macintosh icon dimensions, depths, masks and alpha\n";

    // Modern-only bundles can map icns directly and have no ICN# resource.
    Bytes family(8 + 8 + 10 + 6 + 12 * 16 + 3 * 16, 0xff);
    put32(family, 0, 0x69636e73);
    put32(family, 4, family.size());
    put32(family, 8, 0x65786672); // exfr
    put32(family, 12, family.size() - 8);
    put16(family, 16, 24);
    put16(family, 18, 16);
    put16(family, 20, 4);
    put16(family, 22, 2);
    put16(family, 24, 1);
    std::fill(family.begin() + 32, family.end() - 3 * 16, 0x10);
    fork.resources.resize(2);
    put32(fork.resources[1].payload, 8, 0x69636e73);
    fork.resources.push_back({{0x69636e73, 128}, 0, {}, std::move(family)});
    application.fork = encode_suitcase_fork(fork);
    auto modern = root / L"Modern Application.ico";
    assert(save_application_icon(application, modern));
    ico = read_bytes(modern);
    assert(word(4) == 1 && ico.at(6) == 24 && ico.at(7) == 16 && word(12) == 4);
    std::cout << "PASS shortcut ICO includes an icns-only application bundle\n";

    auto module = GetModuleHandleW(nullptr);
    const auto embedded = [&](LPCWSTR type, unsigned id) {
        auto resource = FindResourceW(module, MAKEINTRESOURCEW(id), type);
        assert(resource);
        auto* data = static_cast<const unsigned char*>(LockResource(LoadResource(module, resource)));
        assert(data);
        return Bytes(data, data + SizeofResource(module, resource));
    };
    wchar_t executable[32768];
    assert(GetModuleFileNameW(module, executable, std::size(executable)));
    auto native = root / L"Windows Application.ico";
    assert(save_native_application_icon(executable, native));
    ico = read_bytes(native);
    auto group = embedded(RT_GROUP_ICON, 101); // The first group in the shared icon resource fixture.
    assert(word(4) > 1 && group.size() == 6 + 14 * word(4));
    for (unsigned i = 0; i < word(4); ++i) {
        const size_t entry = 6 + 16 * i;
        auto* original = group.data() + 6 + i * 14;
        assert(std::equal(original, original + 8, ico.begin() + entry));
        auto data = embedded(RT_ICON, original[12] | unsigned(original[13]) << 8);
        size_t offset = dword(entry + 12);
        assert(data.size() == dword(entry + 8) && data.size() + offset <= ico.size());
        assert(std::equal(data.begin(), data.end(), ico.begin() + offset));
    }
    std::cout << "PASS shortcut ICO preserves every payload in the default Windows icon group\n";
}

Bytes metadata(unsigned marker)
{
    Bytes fork(256 + 4 + 16 + 50);
    put32(fork, 0, 256);
    put32(fork, 4, 276);
    put32(fork, 8, 20);
    put32(fork, 12, 50);
    put32(fork, 256, 16);
    for (unsigned i = 0; i < 16; ++i)
        fork[260 + i] = marker;
    std::copy(fork.begin(), fork.begin() + 16, fork.begin() + 276);
    put16(fork, 276 + 24, 28);
    put16(fork, 276 + 26, 50);
    put32(fork, 276 + 30, 0x54455354);
    put16(fork, 276 + 36, 10);
    put16(fork, 276 + 38, 128);
    put16(fork, 276 + 40, 0xffff);
    Bytes b(82 + fork.size());
    put32(b, 0, 0x51607);
    put32(b, 4, 0x20000);
    put16(b, 24, 2);
    put32(b, 26, 9);
    put32(b, 30, 50);
    put32(b, 34, 32);
    put32(b, 38, 2);
    put32(b, 42, 82);
    put32(b, 46, fork.size());
    put32(b, 50, 0x54455854);
    put32(b, 54, 0x54455354);
    std::copy(fork.begin(), fork.end(), b.begin() + 82);
    return b;
}

void copying(const std::shared_ptr<CopyJob>& j)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!j->done && j->state <= CopyJob::copying && std::chrono::steady_clock::now() < deadline)
        Sleep(1);
    assert(j->state == CopyJob::copying && j->done > 0);
}

void conflict(const std::shared_ptr<CopyJob>& job, ConflictAnswer answer)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (job->state != CopyJob::waiting && job->state < CopyJob::complete &&
           std::chrono::steady_clock::now() < deadline)
        Sleep(1);
    assert(job->state == CopyJob::waiting);
    job->answer_conflict(answer);
}

void wait(const std::shared_ptr<CopyJob>& j)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (j->state <= CopyJob::copying && std::chrono::steady_clock::now() < deadline)
        Sleep(1);
    if (j->worker.joinable())
        j->worker.join();
    assert(j->state >= CopyJob::complete);
}
}

int main()
{
    wchar_t temp[32768];
    assert(GetTempPathW(std::size(temp), temp));
    GUID guid;
    assert(SUCCEEDED(CoCreateGuid(&guid)));
    wchar_t id[64];
    StringFromGUID2(guid, id, std::size(id));
    assert(hidden_metadata(L".Finder-copy-old"));
    assert(hidden_metadata(L".Finder64-copy-old"));
    assert(hidden_metadata(L"Icon\r") && hidden_metadata(L"Icon%0d") && hidden_metadata(L"Icon\uf00d"));
    assert(!hidden_metadata(L"Icon") && !hidden_metadata(L"Icon.icns") && !hidden_metadata(L"Icon%0D notes"));
    auto root = fs::path(temp) / (L"Finder-tests-" + std::wstring(id));
    assert(fs::create_directory(root));
    try {
        {
            auto conversion_root = root / L"Conversions";
            fs::create_directory(conversion_root);
            const auto& root = conversion_root;
            // Basilisk copies into OS X destinations combine raw forks, including
            // nested folders and carriers without data forks, without changing source.
            auto split = root / L"Basilisk", osx = root / L"OS X";
            fs::create_directories(split / L".finf");
            fs::create_directories(split / L".rsrc");
            fs::create_directory(osx);
            write(osx / L".DS_Store", {});
            auto apple = metadata(0x73);
            Bytes info(apple.begin() + 50, apple.begin() + 82);
            info[24] = 0x80;
            Bytes resource(apple.begin() + 82, apple.end());
            write(split / L"Document", {1, 2, 3});
            write(split / L".finf/Document", info);
            write(split / L".rsrc/Document", resource);
            write(split / L".rsrc/Icon%0D", resource);
            fs::create_directory(split / L"Nested");
            write(split / L".finf/Nested", info);
            fs::create_directory(split / L"Nested/.finf");
            write(split / L"Nested/.finf/Info only", Bytes(info.begin(), info.begin() + 16));
            write(split / L"Nested/Info only", {});
            Copies conversions;
            auto single = conversions.start({split / L"Document"}, osx);
            wait(single);
            assert(single->state == CopyJob::complete);
            auto converted = mac_file(osx / L"Document");
            assert(converted.fork == resource && converted.type == 0x54455854 && converted.creator == 0x54455354);
            auto packed = read_bytes(osx / L"._Document");
            assert(Bytes(packed.begin() + 50, packed.begin() + 82) == info);
            assert(!fs::exists(osx / L".finf") && !fs::exists(osx / L".rsrc"));

            // A SUBST drive's root is the storage boundary even when Win32
            // cannot return a volume mount point for the source pathname.
            wchar_t alias[] = L"X:";
            const auto backing = L"\\??\\" + split.wstring();
            wchar_t mapped[32768]{};
            bool mapped_drive = false;
            for (const wchar_t letter : std::wstring(L"XVPQUTKR")) {
                alias[0] = letter;
                if (!QueryDosDeviceW(alias, mapped, std::size(mapped))) {
                    mapped_drive = DefineDosDeviceW(DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM, alias,
                                                     backing.c_str()) != 0;
                    if (mapped_drive)
                        break;
                }
            }
            assert(mapped_drive);
            auto alias_root = fs::path(std::wstring(alias) + L"\\");
            auto alias_destination = root / L"Desktop";
            fs::create_directory(alias_destination);
            write(alias_destination / L".DS_Store", {});
            bool copied_from_alias = false;
            try {
                auto job = conversions.start({alias_root / L"Document"}, alias_destination);
                wait(job);
                copied_from_alias = job->state == CopyJob::complete;
            }
            catch (const std::exception& e) {
                std::cerr << "SUBST source copy: " << e.what() << "\n";
            }
            assert(DefineDosDeviceW(DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE | DDD_RAW_TARGET_PATH |
                                        DDD_NO_BROADCAST_SYSTEM,
                                    alias, backing.c_str()));
            assert(copied_from_alias);
            assert(read_bytes(alias_destination / L"Document") == (Bytes{1, 2, 3}));
            assert(mac_file(alias_destination / L"Document").fork == resource);
            assert(read_bytes(split / L".finf/Document") == info && read_bytes(split / L".rsrc/Document") == resource);

            auto tree = conversions.start({split}, osx);
            wait(tree);
            assert(tree->state == CopyJob::complete);
            auto copied_tree = osx / split.filename();
            assert(mac_file(copied_tree / L"Document").fork == resource);
            assert(mac_file(copied_tree / L"Icon%0D").fork == resource);
            assert(!fs::exists(copied_tree / L"Icon%0D"));
            assert(fs::exists(copied_tree / L"._Nested"));
            assert(mac_file(copied_tree / L"Nested/Info only").type == 0x54455854);
            for (const auto& entry : fs::recursive_directory_iterator(copied_tree))
                assert(entry.path().filename() != L".finf" && entry.path().filename() != L".rsrc" &&
                       entry.path().filename().wstring().rfind(L".Finder-copy-", 0) != 0);
            assert(read_bytes(split / L".finf/Document") == info);
            assert(read_bytes(split / L".rsrc/Document") == resource);
            // An explicit Basilisk destination retains its split convention.
            auto basilisk_destination = root / L"Basilisk destination";
            fs::create_directories(basilisk_destination / L".finf");
            auto retained = conversions.start({split / L"Document"}, basilisk_destination);
            wait(retained);
            assert(retained->state == CopyJob::complete);
            assert(read_bytes(basilisk_destination / L".rsrc/Document") == resource);
            // Every source/destination pair follows the destination convention,
            // inherited through an empty child folder. Nested files and orphan
            // carriers use that same convention, regardless of source markers.
            auto put_metadata = [&](const fs::path& file, unsigned format) {
                fs::create_directories(file.parent_path());
                if (format == 0)
                    write(file.parent_path() / (L"._" + file.filename().wstring()), apple);
                else if (format == 1) {
                    fs::create_directories(file.parent_path() / L".AppleDouble");
                    write(file.parent_path() / L".AppleDouble" / file.filename(), apple);
                }
                else if (format == 2) {
                    fs::create_directories(file.parent_path() / L".finf");
                    fs::create_directories(file.parent_path() / L".rsrc");
                    write(file.parent_path() / L".finf" / file.filename(),
                          Bytes(apple.begin() + 50, apple.begin() + 82));
                    write(file.parent_path() / L".rsrc" / file.filename(), resource);
                }
                else
                    write(file.parent_path() / (L"%" + file.filename().wstring()), apple);
            };
            auto check_format = [&](const fs::path& file, unsigned format) {
                auto companions = mac_companions(file);
                assert(companions.size() == (format == 2 ? 2 : 1));
                auto expected = format == 0   ? file.parent_path() / (L"._" + file.filename().wstring())
                                : format == 1 ? file.parent_path() / L".AppleDouble" / file.filename()
                                : format == 2 ? file.parent_path() / L".finf" / file.filename()
                                              : file.parent_path() / (L"%" + file.filename().wstring());
                assert(std::find(companions.begin(), companions.end(), expected) != companions.end());
                auto decoded = mac_file(file);
                assert(decoded.fork == resource && decoded.type == 0x54455854 && decoded.creator == 0x54455354);
            };
            wchar_t move_base[32768]{};
            auto move_root = root / L"Move format destinations";
            bool physical_moves =
                GetEnvironmentVariableW(L"FINDER_CROSS_VOLUME_TEST_ROOT", move_base, std::size(move_base)) != 0;
            if (physical_moves)
                move_root = fs::path(move_base) / (L"Finder-formats-" + std::wstring(id));
            fs::create_directories(move_root);
            if (physical_moves)
                assert(!same_storage_volume(root, move_root));
            for (unsigned from = 0; from < 4; ++from) {
                auto source = root / (L"Source " + std::to_wstring(from));
                auto folder = source / L"Folder";
                fs::create_directories(folder / L"Nested");
                write(folder / L"Payload", {1, 2, 3});
                write(folder / L"Nested/Child", {4, 5});
                put_metadata(folder, from);
                put_metadata(folder / L"Payload", from);
                put_metadata(folder / L"Nested/Child", from);
                put_metadata(folder / L"Icon%0D", from);
                for (unsigned to = 0; to < 5; ++to) {
                    auto volume = root / (L"Destination " + std::to_wstring(from) + L"-" + std::to_wstring(to));
                    fs::create_directories(volume / L"Empty child");
                    if (to < 4)
                        put_metadata(volume / L"Marker", to);
                    auto destination = volume / L"Empty child";
                    auto file_job = conversions.start({folder / L"Payload"}, destination);
                    wait(file_job);
                    assert(file_job->state == CopyJob::complete);
                    auto folder_job = conversions.start({folder}, destination);
                    wait(folder_job);
                    assert(folder_job->state == CopyJob::complete);
                    unsigned expected = to == 4 ? 0 : to;
                    check_format(destination / L"Payload", expected);
                    check_format(destination / L"Folder", expected);
                    check_format(destination / L"Folder/Payload", expected);
                    check_format(destination / L"Folder/Nested/Child", expected);
                    check_format(destination / L"Folder/Icon%0D", expected);
                    assert(!fs::exists(destination / L"Folder/Icon%0D"));
                    assert(read_bytes(destination / L"Folder/Payload") == Bytes({1, 2, 3}));
                    assert(read_bytes(folder / L"Payload") == Bytes({1, 2, 3}));
                    check_format(folder / L"Payload", from);
                    auto move_volume = move_root / (std::to_wstring(from) + L"-" + std::to_wstring(to));
                    auto move_destination = move_volume / L"Empty child";
                    fs::create_directories(move_destination);
                    if (to < 4)
                        put_metadata(move_volume / L"Marker", to);
                    auto input = source / (L"Moving " + std::to_wstring(to));
                    auto moving_file = input / L"Moving file", moving_folder = input / L"Moving folder";
                    fs::create_directories(moving_folder / L"Nested");
                    write(moving_file, {7, 8});
                    write(moving_folder / L"Nested/Child", {9});
                    put_metadata(moving_file, from);
                    put_metadata(moving_folder, from);
                    put_metadata(moving_folder / L"Nested/Child", from);
                    put_metadata(moving_folder / L"Icon%0D", from);
                    staged_moves = !physical_moves;
                    auto move_file_job = conversions.start({moving_file}, move_destination, TransferKind::move);
                    wait(move_file_job);
                    auto move_folder_job = conversions.start({moving_folder}, move_destination, TransferKind::move);
                    wait(move_folder_job);
                    staged_moves = false;
                    assert(move_file_job->state == CopyJob::complete && move_folder_job->state == CopyJob::complete);
                    assert(move_file_job->moved.size() == 1 && move_folder_job->moved.size() == 1);
                    check_format(move_destination / moving_file.filename(), expected);
                    check_format(move_destination / moving_folder.filename(), expected);
                    check_format(move_destination / moving_folder.filename() / L"Nested/Child", expected);
                    check_format(move_destination / moving_folder.filename() / L"Icon%0D", expected);
                    assert(read_bytes(move_destination / moving_file.filename()) == Bytes({7, 8}));
                    assert(read_bytes(move_destination / moving_folder.filename() / L"Nested/Child") == Bytes({9}));
                    assert(!fs::exists(moving_file) && mac_companions(moving_file).empty());
                    assert(!fs::exists(moving_folder) && mac_companions(moving_folder).empty());
                }
            }
            fs::remove_all(move_root);
            std::cout << "PASS: 20 cross-volume move format pairs, files, nested folders, orphan forks and OS X default"
                      << (physical_moves ? " on distinct Windows volumes\n" : " through the staged move branch\n");
            // A same-drive rename retains the individual item's convention.
            auto same_drive_source = root / L"Same-drive source";
            write(same_drive_source, {1});
            put_metadata(same_drive_source, 0);
            auto same_drive_destination = root / L"Same-drive Basilisk destination";
            fs::create_directories(same_drive_destination / L".finf");
            assert(same_storage_volume(root, same_drive_destination));
            auto same_drive_move = conversions.start({same_drive_source}, same_drive_destination, TransferKind::move);
            wait(same_drive_move);
            assert(same_drive_move->state == CopyJob::complete);
            check_format(same_drive_destination / same_drive_source.filename(), 0);
            // Existing OS X companions take precedence over leftover split folders.
            auto mixed = root / L"Mixed destination";
            fs::create_directories(mixed / L".finf");
            write(mixed / L"._Existing", metadata(1));
            set_comment(split / L"Document", L"Keep this comment");
            auto merged = conversions.start({split / L"Document"}, mixed);
            wait(merged);
            assert(merged->state == CopyJob::complete);
            assert(fs::exists(mixed / L"._Document") && !fs::exists(mixed / L".finf/Document"));
            assert(!fs::exists(mixed / L".rsrc"));
            assert(mac_file(mixed / L"Document").comment == L"Keep this comment");
            assert(mac_file(mixed / L"Document").fork == resource);
            auto lossy = conversions.start({split / L"Document"}, basilisk_destination, TransferKind::copy);
            conflict(lossy, ConflictAnswer::replace);
            wait(lossy);
            assert(lossy->state == CopyJob::failed);
            assert(read_bytes(basilisk_destination / L".rsrc/Document") == resource);
            assert(mac_file(split / L"Document").comment == L"Keep this comment");
            auto move_failure_destination = root / L"Lossy move destination";
            fs::create_directories(move_failure_destination / L".finf");
            staged_moves = true;
            auto lossy_move = conversions.start({split / L"Document"}, move_failure_destination, TransferKind::move);
            wait(lossy_move);
            staged_moves = false;
            assert(lossy_move->state == CopyJob::failed && lossy_move->moved.empty());
            assert(mac_file(split / L"Document").comment == L"Keep this comment");
            assert(read_bytes(split / L"Document") == Bytes({1, 2, 3}));
            assert(read_bytes(split / L".finf/Document") == info && read_bytes(split / L".rsrc/Document") == resource);
            assert(!fs::exists(move_failure_destination / L"Document"));
            assert(!fs::exists(move_failure_destination / L".finf/Document"));
            assert(!fs::exists(move_failure_destination / L".rsrc"));
            // Netatalk's .Parent folder record becomes a sibling companion.
            auto parent_source = root / L"Parent record source";
            fs::create_directories(parent_source / L"Nested/.AppleDouble");
            write(parent_source / L"Nested/.AppleDouble/.Parent", apple);
            auto parent_copy = conversions.start({parent_source}, osx);
            wait(parent_copy);
            assert(parent_copy->state == CopyJob::complete);
            check_format(parent_copy->destinations.front() / L"Nested", 0);
            assert(!fs::exists(parent_copy->destinations.front() / L"Nested/.AppleDouble"));
            // Layout-only conversions preserve modern extended FinderInfo and
            // opaque payloads exactly, without moving internal ATTR offsets.
            auto extended_source = root / L"Extended source";
            fs::create_directory(extended_source);
            write(extended_source / L"Extended", {});
            auto extended = apple;
            extended.resize(extended.size() + 64);
            put32(extended, 34, 96);
            put32(extended, 42, 146);
            std::copy(resource.begin(), resource.end(), extended.begin() + 146);
            std::fill(extended.begin() + 82, extended.begin() + 146, 0x31);
            write(extended_source / L"._Extended", extended);
            assert(SetFileAttributesW((extended_source / L"._Extended").c_str(), FILE_ATTRIBUTE_READONLY));
            auto extended_destination = root / L"Extended Netatalk destination";
            fs::create_directories(extended_destination / L".AppleDouble");
            auto extended_copy = conversions.start({extended_source / L"Extended"}, extended_destination);
            wait(extended_copy);
            assert(extended_copy->state == CopyJob::complete);
            assert(read_bytes(extended_destination / L".AppleDouble/Extended") == extended);
            assert(!fs::exists(extended_destination / L"._Extended"));
            assert(GetFileAttributesW((extended_source / L"._Extended").c_str()) & FILE_ATTRIBUTE_READONLY);
            assert(SetFileAttributesW((extended_source / L"._Extended").c_str(), FILE_ATTRIBUTE_NORMAL));
            // Malformed FinderInfo fails before publishing the logical folder.
            write(split / L"Nested/.finf/Invalid", {1, 2, 3});
            auto malformed_destination = root / L"Malformed destination";
            fs::create_directory(malformed_destination);
            auto malformed = conversions.start({split}, malformed_destination);
            wait(malformed);
            assert(malformed->state == CopyJob::failed);
            assert(fs::is_empty(malformed_destination));
            fs::remove_all(conversion_root);
        }
        auto system = root / L"System Folder";
        fs::create_directories(system / L"Extensions");
        fs::create_directories(root / L"Unblessed/System Folder");
        assert(special_folder_kind(system, system) == 0);
        assert(special_folder_kind(system / L"Extensions", system) == 5);
        assert(special_folder_kind(root / L"Unblessed/System Folder", system) == -1);
        assert(special_folder_kind(root / L"Extensions", system) == -1);
        auto a = root / L"Alpha document.bin", b = root / L"Beta document.bin";
        write(a, Bytes(8 * 1024 * 1024, 0xa1));
        write(b, Bytes(8 * 1024 * 1024, 0xb2));
        write(sidecar(a), metadata(0xa1));
        write(sidecar(b), metadata(0xb2));
        std::atomic<bool> stop_size{false};
        const auto fork_size = metadata(0xa1).size() - 82;
        auto first_size = host_file_size(a);
        auto second_size = host_file_size(b);
        auto folder_size = host_folder_size(root, stop_size);
        assert(first_size.logical == 8 * 1024 * 1024 + fork_size);
        assert(first_size.allocated && second_size.allocated && folder_size);
        assert(folder_size->logical == 16 * 1024 * 1024 + 2 * fork_size);
        assert(folder_size->allocated == *first_size.allocated + *second_size.allocated);
        auto empty_size = host_folder_size(system, stop_size);
        assert(empty_size && empty_size->logical == 0 && empty_size->allocated == 0);
        assert(format_file_size({1, 4096}) == L"4 KB on disk\r\n1 byte");
        assert(format_file_size({4400, std::nullopt}) == L"Size on disk unavailable\r\n4400 bytes");
        FileSize partial{4, 4096};
        partial.add({6, std::nullopt});
        assert(partial.logical == 10 && !partial.allocated);
        auto sparse_path = root / L"Sparse payload";
        HANDLE sparse = CreateFileW(sparse_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
        assert(sparse != INVALID_HANDLE_VALUE);
        DWORD returned = 0;
        bool supports_sparse = DeviceIoControl(sparse, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr);
        LARGE_INTEGER sparse_length{};
        sparse_length.QuadPart = 16 * 1024 * 1024;
        if (supports_sparse)
            assert(SetFilePointerEx(sparse, sparse_length, nullptr, FILE_BEGIN) && SetEndOfFile(sparse));
        assert(CloseHandle(sparse));
        if (supports_sparse) {
            auto sparse_size = host_file_size(sparse_path);
            assert(sparse_size.logical == uint64_t(sparse_length.QuadPart) && sparse_size.allocated == 0);
        }
        fs::remove(sparse_path);
        auto info = mac_file(a);
        assert(info.type == 0x54455854 && info.creator == 0x54455354 && !info.fork.empty());
        assert(mac_resource(info, 0x54455354, 128) == Bytes(16, 0xa1));
        assert(mac_resource(info, 0x54455354, 129).empty());
        MacFile classic_name;
        classic_name.kind = 1;
        assert(mac_display_name(fs::path(L"SimCity%20"), classic_name) == L"SimCity ");
        assert(mac_display_name(fs::path(L"Collection%a8"), classic_name) == L"Collection\u00ae");
        assert(mac_display_name(fs::path(L"1994 %c4"), classic_name) == L"1994 \u0192");
        assert(mac_display_name(fs::path(L"literal%20"), MacFile{}) == L"literal%20");
        auto position = metadata(1);
        put16(position, 58, 0x100);
        put16(position, 60, 52);
        put16(position, 62, 128);
        auto located = root / L"Located";
        write(located, {});
        write(sidecar(located), position);
        auto location = mac_file(located);
        assert(location.positioned && location.icon_x == 128 && location.icon_y == 52);
        auto folder_info = root / L"Window bounds";
        fs::create_directory(folder_info);
        put16(position, 50, 39);
        put16(position, 52, 3);
        put16(position, 54, 306);
        put16(position, 56, 562);
        write(sidecar(folder_info), position);
        auto bounds = mac_file(folder_info);
        assert(bounds.windowed && bounds.window_bounds.top == 39 && bounds.window_bounds.right == 562);
        auto raw_bounds = mac_catalog_info(position.data() + 50, true);
        assert(raw_bounds.windowed && raw_bounds.positioned && raw_bounds.icon_x == 128);
        // Bounds remain meaningful independently of the icon initialization flag.
        put16(position, 58, 0);
        raw_bounds = mac_catalog_info(position.data() + 50, true);
        assert(raw_bounds.windowed && !raw_bounds.positioned);
        put16(position, 54, 20); // Inverted rectangles must not become window sizes.
        assert(!mac_catalog_info(position.data() + 50, true).windowed);
        put16(position, 58, 0x100);
        put16(position, 60, 0x8000); // Unplaced icon sentinel.
        assert(!mac_catalog_info(position.data() + 50, false).positioned);
        put16(position, 60, 0xfff8);
        put16(position, 62, 0xfff0);
        auto edge = mac_catalog_info(position.data() + 50, false);
        assert(edge.positioned && edge.icon_x == -16 && edge.icon_y == -8);
        put16(position, 64, 0x140);
        assert(mac_catalog_info(position.data() + 50, true).folder_view == 0x140);
        assert(mac_catalog_info(position.data() + 50, false).folder_view == 0);

        set_label(a, 4);
        assert(((mac_file(a).flags >> 1) & 7) == 4);
        auto comment_fork = mac_file(a).fork;
        set_comment(a, L"Caf\u00e9 notes");
        assert(mac_file(a).comment == L"Caf\u00e9 notes" && mac_file(a).fork == comment_fork);
        set_stationery(a, true);
        auto pad = mac_file(a);
        assert(pad.flags & stationery_flag);
        assert(((pad.flags >> 1) & 7) == 4 && pad.comment == L"Caf\u00e9 notes" && pad.fork == comment_fork);
        assert(SetFileAttributesW(a.c_str(), FILE_ATTRIBUTE_READONLY));
        auto document = root / L"Fresh document.bin";
        Copies stationery_copies;
        auto fresh = stationery_copies.start_targets({{a, document}}, TransferKind::stationery);
        wait(fresh);
        assert(fresh->state == CopyJob::complete && !(GetFileAttributesW(document.c_str()) & FILE_ATTRIBUTE_READONLY));
        assert((mac_file(a).flags & stationery_flag) && (GetFileAttributesW(a.c_str()) & FILE_ATTRIBUTE_READONLY));
        auto copy = mac_file(document);
        assert(copy.flags == (pad.flags & ~stationery_flag) && copy.fork == pad.fork && copy.comment == pad.comment);
        assert(read_bytes(a) == read_bytes(document));
        bool duplicate_name = false;
        try {
            stationery_copies.start_targets({{a, document}}, TransferKind::stationery);
        }
        catch (const std::exception&) {
            duplicate_name = true;
        }
        assert(duplicate_name && mac_file(document).fork == pad.fork);
        fs::remove(sidecar(document));
        fs::remove(document);
        assert(SetFileAttributesW(a.c_str(), FILE_ATTRIBUTE_NORMAL));
        set_stationery(a, false);
        auto commented = read_bytes(sidecar(a));
        bool rejected_comment = false;
        try {
            set_comment(a, L"\u65e5\u672c");
        }
        catch (const std::exception&) {
            rejected_comment = true;
        }
        assert(rejected_comment && read_bytes(sidecar(a)) == commented);
        auto fresh_comment = root / L"Comment-only file";
        write(fresh_comment, {});
        set_comment(fresh_comment, L"Native comment");
        set_label(fresh_comment, 4);
        assert(mac_file(fresh_comment).comment == L"Native comment" && ((mac_file(fresh_comment).flags >> 1) & 7) == 4);
        auto original = read_bytes(sidecar(a));
        try {
            set_label(a, 8);
            assert(false);
        }
        catch (const std::exception&) {
        }
        assert(read_bytes(sidecar(a)) == original);
        Copies manager;
        hold_copies = true;
        auto first = manager.start({a}, root), second = manager.start({b}, root);
        auto repeat = manager.start({a}, root);
        assert(first->destinations != repeat->destinations);
        copying(first);
        copying(second);
        copying(repeat);
        assert(first->items == 1 && second->items == 1);
        hold_copies = false;
        wait(first);
        wait(second);
        wait(repeat);
        for (const auto& j : {first, second, repeat}) {
            assert(j->state == CopyJob::complete && j->done == j->total && j->items == 0);
            assert(read_bytes(j->sources[0]) == read_bytes(j->destinations[0]));
            assert(read_bytes(sidecar(j->sources[0])) == read_bytes(sidecar(j->destinations[0])));
        }
        hold_copies = true;
        auto stopped = manager.start({a}, root), other = manager.start({b}, root);
        copying(stopped);
        copying(other);
        stopped->stop = true;
        wait(stopped);
        assert(other->state == CopyJob::copying);
        hold_copies = false;
        wait(stopped);
        wait(other);
        assert(stopped->state == CopyJob::cancelled && other->state == CopyJob::complete);
        assert(!fs::exists(stopped->destinations[0]) && !fs::exists(sidecar(stopped->destinations[0])));
        hold_copies = true;
        auto collision = manager.start({b}, root);
        copying(collision);
        write(collision->destinations[0], Bytes{42});
        write(sidecar(collision->destinations[0]), Bytes{43});
        hold_copies = false;
        wait(collision);
        assert(collision->state == CopyJob::failed);
        assert(read_bytes(collision->destinations[0]) == Bytes({42}));
        assert(read_bytes(sidecar(collision->destinations[0])) == Bytes({43}));
        fs::create_directory(root / L".AppleDouble");
        auto legacy = root / L"Netatalk document";
        write(legacy, Bytes{9, 8, 7});
        write(root / L".AppleDouble/Netatalk document", metadata(11));
        assert(mac_file(legacy).kind == FINDER_CLASSIC_DOCUMENT);
        auto legacy_copy = manager.start({legacy}, root);
        wait(legacy_copy);
        assert(legacy_copy->state == CopyJob::complete);
        assert(read_bytes(sidecar(legacy_copy->destinations[0])) == read_bytes(sidecar(legacy)));
        const uint32_t resources[] = {0x434f4445, 0x63667267, 0x44525652, 0x63646576};
        const uint32_t types[] = {0x4150504c, 0x4150504c, 0x6466696c, 0x63646576};
        const int kinds[] = {1, 1, 2, 3};
        for (unsigned i = 0; i < 4; ++i) {
            auto name = root / (L"Classic " + std::to_wstring(i));
            write(name, {});
            auto data = metadata(i);
            put32(data, 50, types[i]);
            put32(data, 82 + 276 + 30, resources[i]);
            write(sidecar(name), data);
            assert(mac_file(name).kind == kinds[i]);
            if (kinds[i] == 2)
                assert(mac_file(name).driver == 128);
        }
        auto tree = root / L"Folder";
        fs::create_directories(tree / L"Nested");
        write(tree / L"Nested/Document", Bytes{1, 2, 3});
        write(sidecar(tree / L"Nested/Document"), metadata(7));
        write(sidecar(tree), metadata(9));
        auto directory = manager.start({tree}, root);
        wait(directory);
        assert(directory->state == CopyJob::complete);
        auto target = directory->destinations[0];
        assert(read_bytes(target / L"Nested/Document") == Bytes({1, 2, 3}));
        assert(read_bytes(sidecar(target)) == read_bytes(sidecar(tree)));
        try {
            manager.start({tree}, tree / L"Nested");
            assert(false);
        }
        catch (const std::exception&) {
        }
        // Fast same-volume moves preserve forks and publish immutable results.
        auto moving_to = root / L"Move destination";
        fs::create_directory(moving_to);
        auto movable = root / L"Movable";
        write(movable, Bytes{4, 5, 6});
        write(sidecar(movable), metadata(4));
        auto moved = manager.start({movable}, moving_to, TransferKind::move);
        wait(moved);
        assert(moved->state == CopyJob::complete && moved->moved.size() == 1);
        assert(!fs::exists(movable) && !fs::exists(sidecar(movable)));
        assert(read_bytes(moving_to / L"Movable") == Bytes({4, 5, 6}));
        assert(read_bytes(sidecar(moving_to / L"Movable")) == metadata(4));
        assert(!manager.start({moving_to / L"Movable"}, moving_to, TransferKind::move));
        assert(same_storage_volume(moving_to, root));
        // Exercise the cross-volume copy/verify/delete branch on the isolated
        // fixture volume, including cancellation while another copy continues.
        staged_moves = true;
        auto cross_source = root / L"Staged move";
        write(cross_source, Bytes(2 * 1024 * 1024, 37));
        write(sidecar(cross_source), metadata(5));
        hold_copies = true;
        auto cancelled_move = manager.start({cross_source}, moving_to, TransferKind::move);
        auto parallel_copy = manager.start({b}, moving_to);
        copying(cancelled_move);
        copying(parallel_copy);
        cancelled_move->stop = true;
        wait(cancelled_move);
        assert(cancelled_move->state == CopyJob::cancelled && fs::exists(cross_source));
        assert(fs::exists(sidecar(cross_source)) && !fs::exists(cancelled_move->destinations[0]));
        assert(parallel_copy->state == CopyJob::copying);
        hold_copies = false;
        wait(parallel_copy);
        auto staged_move = manager.start({cross_source}, moving_to, TransferKind::move);
        wait(staged_move);
        assert(staged_move->state == CopyJob::complete && !fs::exists(cross_source));
        assert(read_bytes(sidecar(staged_move->destinations[0])) == metadata(5));
        auto staged_tree = manager.start({tree}, moving_to, TransferKind::move);
        wait(staged_tree);
        assert(staged_tree->state == CopyJob::complete && !fs::exists(tree));
        assert(read_bytes(staged_tree->destinations[0] / L"Nested/Document") == Bytes({1, 2, 3}));
        // A file edited after planning must remain at source, with no published
        // destination, even if it retains the same byte count.
        auto changing = root / L"Changing";
        write(changing, Bytes(2 * 1024 * 1024, 11));
        publication_waiting = false;
        hold_publication = true;
        auto changed_move = manager.start({changing}, moving_to, TransferKind::move);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!publication_waiting && std::chrono::steady_clock::now() < deadline)
            Sleep(1);
        assert(publication_waiting);
        write(changing, Bytes(2 * 1024 * 1024, 12));
        hold_publication = false;
        wait(changed_move);
        assert(changed_move->state == CopyJob::failed && fs::exists(changing));
        assert(!fs::exists(changed_move->destinations[0]));
        staged_moves = false;
        // Selected links must be rejected before canonicalization; resolving
        // first would move the link's real target instead of the selected name.
        auto real_folder = root / L"Real folder", link = root / L"Selected link";
        fs::create_directory(real_folder);
        write(real_folder / L"Retained", Bytes{9});
        if (CreateSymbolicLinkW(link.c_str(), real_folder.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | 2)) {
            bool rejected = false;
            try {
                manager.start({link}, moving_to, TransferKind::move);
            }
            catch (const std::exception&) {
                rejected = true;
            }
            assert(rejected);
            assert(fs::exists(real_folder / L"Retained"));
            assert(GetFileAttributesW(link.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT);
            // MinGW filesystem may classify a directory link as a directory;
            // remove only the link through Win32 before recursive fixture cleanup.
            assert(RemoveDirectoryW(link.c_str()));
        }
        else
            std::cout << "SKIP: directory symlink fixture unavailable (Windows error " << GetLastError() << ")\n";
        // Put Away restores exact original names, even when a Trash collision
        // renamed the stored item, and can return items to different parents.
        auto returned_a = root / L"Returned A", returned_b = root / L"Returned B";
        fs::create_directories(returned_a);
        fs::create_directories(returned_b);
        auto stored_a = moving_to / L"Stored copy 2", stored_b = moving_to / L"Another stored item";
        write(stored_a, Bytes{31});
        write(sidecar(stored_a), metadata(31));
        write(stored_b, Bytes{32});
        auto put_away =
            manager.start_targets({{stored_a, returned_a / L"Original"}, {stored_b, returned_b / L"Original"}});
        wait(put_away);
        assert(put_away->state == CopyJob::complete && put_away->moved.size() == 2);
        assert(read_bytes(returned_a / L"Original") == Bytes({31}));
        assert(read_bytes(sidecar(returned_a / L"Original")) == metadata(31));
        assert(read_bytes(returned_b / L"Original") == Bytes({32}));
        assert(!fs::exists(stored_a) && !fs::exists(stored_b));
        wchar_t cross_base[32768]{};
        if (GetEnvironmentVariableW(L"FINDER_CROSS_VOLUME_TEST_ROOT", cross_base, 32768)) {
            fs::path base(cross_base);
            fs::create_directories(base);
            auto destination = base / (L"Finder-transfer-" + std::wstring(id));
            assert(fs::create_directory(destination));
            if (!same_storage_volume(root, destination)) {
                auto cross_tree = root / L"Physical cross-volume folder";
                fs::create_directories(cross_tree / L"Nested");
                write(cross_tree / L"Nested/Document", Bytes{61, 62});
                write(sidecar(cross_tree / L"Nested/Document"), metadata(6));
                write(sidecar(cross_tree), metadata(8));
                auto physical_move = manager.start({cross_tree}, destination, TransferKind::move);
                wait(physical_move);
                assert(physical_move->state == CopyJob::complete && !fs::exists(cross_tree));
                assert(read_bytes(physical_move->destinations[0] / L"Nested/Document") == Bytes({61, 62}));
                assert(read_bytes(sidecar(physical_move->destinations[0])) == metadata(8));
                assert(read_bytes(sidecar(physical_move->destinations[0] / L"Nested/Document")) == metadata(6));
                auto cancel_source = root / L"Physical move Stop";
                write(cancel_source, Bytes(2 * 1024 * 1024, 57));
                write(sidecar(cancel_source), metadata(9));
                hold_copies = true;
                auto physical_stop = manager.start({cancel_source}, destination, TransferKind::move);
                copying(physical_stop);
                physical_stop->stop = true;
                wait(physical_stop);
                hold_copies = false;
                assert(physical_stop->state == CopyJob::cancelled && fs::exists(cancel_source));
                assert(fs::exists(sidecar(cancel_source)) && !fs::exists(physical_stop->destinations[0]));
                std::cout << "PASS: physical cross-volume folder/fork move and Stop preservation\n";
            }
            else
                std::cout << "SKIP: cross-volume test root is on the same volume\n";
            fs::remove_all(destination);
        }
        // Trash keeps same-name items independently, including simultaneous
        // operations, and never asks to replace an earlier discarded item.
        auto trash = root / L"Trash", origin_a = root / L"Trash origin A", origin_b = root / L"Trash origin B";
        fs::create_directories(trash / L".AppleDouble");
        fs::create_directories(trash / L".Finder-copy-staging");
        fs::create_directory(origin_a);
        fs::create_directory(origin_b);
        write(trash / L"._orphan", metadata(1));
        write(trash / L".AppleDouble/orphan", metadata(2));
        write(trash / L".Finder-copy-staging/data", Bytes{3});
        assert(!trash_contains_items(trash));
        auto same_a = origin_a / L"Same name", same_b = origin_b / L"Same name";
        write(same_a, Bytes(1024 * 1024, 41));
        write(sidecar(same_a), metadata(4));
        write(same_b, Bytes(1024 * 1024, 42));
        write(sidecar(same_b), metadata(5));
        staged_moves = true;
        hold_copies = true;
        auto trashed_a = manager.start({same_a}, trash, TransferKind::trash);
        copying(trashed_a);
        auto trashed_b = manager.start({same_b}, trash, TransferKind::trash);
        copying(trashed_b);
        assert(trashed_a->destinations[0] != trashed_b->destinations[0]);
        assert(!trash_contains_items(trash)); // Unpublished staging is not Trash content.
        trashed_a->stop = true;
        wait(trashed_a);
        assert(trashed_a->state == CopyJob::cancelled && fs::exists(same_a));
        assert(trashed_b->state == CopyJob::copying);
        hold_copies = false;
        wait(trashed_b);
        assert(trashed_b->state == CopyJob::complete && !fs::exists(same_b));
        assert(trash_contains_items(trash));
        staged_moves = false;
        trashed_a = manager.start({same_a}, trash, TransferKind::trash);
        wait(trashed_a);
        assert(trashed_a->state == CopyJob::complete && !fs::exists(same_a));
        assert(read_bytes(trashed_a->destinations[0]) == Bytes(1024 * 1024, 41));
        assert(read_bytes(trashed_b->destinations[0]) == Bytes(1024 * 1024, 42));
        assert(read_bytes(sidecar(trashed_a->destinations[0])) == metadata(4));
        assert(read_bytes(sidecar(trashed_b->destinations[0])) == metadata(5));
        auto restored =
            manager.start_targets({{trashed_a->destinations[0], same_a}, {trashed_b->destinations[0], same_b}});
        wait(restored);
        assert(restored->state == CopyJob::complete && !trash_contains_items(trash));
        assert(read_bytes(sidecar(same_a)) == metadata(4) && read_bytes(sidecar(same_b)) == metadata(5));
        // Empty user folders count as content; metadata containers do not.
        auto empty_folder = origin_a / L"Empty folder";
        fs::create_directory(empty_folder);
        auto trashed_folder = manager.start({empty_folder}, trash, TransferKind::trash);
        wait(trashed_folder);
        assert(trashed_folder->state == CopyJob::complete && trash_contains_items(trash));
        auto restore_folder = manager.start_targets({{trashed_folder->destinations[0], empty_folder}});
        wait(restore_folder);
        assert(restore_folder->state == CopyJob::complete && !trash_contains_items(trash));
        assert(!trash_contains_items(root / L"Absent Trash"));
        assert(moving_transfer(TransferKind::trash) && !moving_transfer(TransferKind::copy));
        // Explicit copying into Trash must also avoid collisions, but must not
        // turn Copy into a source-deleting move.
        auto copied_to_trash_a = manager.start({same_a}, trash, TransferKind::trash_copy);
        auto copied_to_trash_b = manager.start({same_b}, trash, TransferKind::trash_copy);
        wait(copied_to_trash_a);
        wait(copied_to_trash_b);
        assert(copied_to_trash_a->state == CopyJob::complete && copied_to_trash_b->state == CopyJob::complete);
        assert(copied_to_trash_a->destinations[0] != copied_to_trash_b->destinations[0]);
        assert(fs::exists(same_a) && fs::exists(same_b) && !moving_transfer(TransferKind::trash_copy));
        assert(copied_to_trash_a->moved.empty() && copied_to_trash_b->moved.empty());
        for (const auto& copied : {copied_to_trash_a, copied_to_trash_b}) {
            for (const auto& companion : mac_companions(copied->destinations[0]))
                fs::remove(companion);
            fs::remove(copied->destinations[0]);
        }
        assert(!trash_contains_items(trash));
        // Real copies reserve exact names and wait for their own conflict
        // decision, without blocking unrelated workers or deleting originals.
        auto conflict_source = root / L"Conflict item";
        auto conflict_target = moving_to / conflict_source.filename();
        write(conflict_source, Bytes{1, 2, 3});
        write(sidecar(conflict_source), metadata(1));
        write(conflict_target, Bytes{7, 8, 9});
        write(sidecar(conflict_target), metadata(7));
        auto skipped = manager.start({conflict_source}, moving_to, TransferKind::copy);
        auto independent = manager.start({b}, moving_to);
        conflict(skipped, ConflictAnswer::skip);
        wait(skipped);
        wait(independent);
        assert(skipped->state == CopyJob::complete && independent->state == CopyJob::complete);
        assert(read_bytes(conflict_target) == Bytes({7, 8, 9}));
        auto stopped_conflict = manager.start({conflict_source}, moving_to, TransferKind::move);
        conflict(stopped_conflict, ConflictAnswer::stop);
        wait(stopped_conflict);
        assert(stopped_conflict->state == CopyJob::cancelled && fs::exists(conflict_source));
        assert(read_bytes(sidecar(conflict_target)) == metadata(7));
        auto replaced_copy = manager.start({conflict_source}, moving_to, TransferKind::copy);
        conflict(replaced_copy, ConflictAnswer::replace);
        wait(replaced_copy);
        assert(replaced_copy->state == CopyJob::complete && fs::exists(conflict_source));
        assert(read_bytes(conflict_target) == Bytes({1, 2, 3}));
        assert(read_bytes(sidecar(conflict_target)) == metadata(1));
        // A destination edited after the decision is never silently overwritten.
        write(conflict_source, Bytes(1024 * 1024, 28));
        hold_copies = true;
        auto changed_target = manager.start({conflict_source}, moving_to, TransferKind::copy);
        conflict(changed_target, ConflictAnswer::replace);
        copying(changed_target);
        write(conflict_target, Bytes{45, 46});
        hold_copies = false;
        wait(changed_target);
        assert(changed_target->state == CopyJob::failed);
        assert(read_bytes(conflict_target) == Bytes({45, 46}));
        // Replacement moves publish both the recovery move and source move for
        // UI-thread Undo. Resource streams remain attached to the logical backup.
        auto replaced_move = manager.start({conflict_source}, moving_to, TransferKind::move);
        conflict(replaced_move, ConflictAnswer::replace);
        wait(replaced_move);
        assert(replaced_move->state == CopyJob::complete && !fs::exists(conflict_source));
        assert(replaced_move->moved.size() == 2 && replaced_move->replacements.size() == 1);
        auto backup = replaced_move->moved[0].second;
        assert(read_bytes(backup) == Bytes({45, 46}) && read_bytes(sidecar(backup)) == metadata(1));
        assert(read_bytes(sidecar(conflict_target)) == metadata(1));
        // If the new resource stream cannot move, roll the data move back and
        // restore the replaced logical item, rather than leaving mixed forks.
        auto rollback_source = root / L"Rollback item";
        auto rollback_target = moving_to / rollback_source.filename();
        write(rollback_source, Bytes{21});
        write(sidecar(rollback_source), metadata(21));
        write(rollback_target, Bytes{22});
        write(sidecar(rollback_target), metadata(22));
        HANDLE held_fork = CreateFileW(sidecar(rollback_source).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, 0, nullptr);
        assert(held_fork != INVALID_HANDLE_VALUE);
        auto rollback = manager.start({rollback_source}, moving_to, TransferKind::move);
        conflict(rollback, ConflictAnswer::replace);
        wait(rollback);
        CloseHandle(held_fork);
        assert(rollback->state == CopyJob::failed && rollback->moved.empty());
        assert(read_bytes(rollback_source) == Bytes({21}) && read_bytes(sidecar(rollback_source)) == metadata(21));
        assert(read_bytes(rollback_target) == Bytes({22}) && read_bytes(sidecar(rollback_target)) == metadata(22));
        // Folder replacement never merges old and new children accidentally.
        auto incoming_folder = root / L"Replace folder";
        auto existing_folder = moving_to / incoming_folder.filename();
        fs::create_directories(incoming_folder);
        fs::create_directories(existing_folder);
        write(incoming_folder / L"New", Bytes{1});
        write(existing_folder / L"Old", Bytes{2});
        auto replaced_folder = manager.start({incoming_folder}, moving_to, TransferKind::copy);
        conflict(replaced_folder, ConflictAnswer::replace);
        wait(replaced_folder);
        assert(replaced_folder->state == CopyJob::complete);
        assert(fs::exists(existing_folder / L"New") && !fs::exists(existing_folder / L"Old"));
        // Cancellation also wakes an unanswered decision during shutdown.
        auto unanswered = manager.start({incoming_folder}, moving_to, TransferKind::copy);
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (unanswered->state != CopyJob::waiting && std::chrono::steady_clock::now() < until)
            Sleep(1);
        assert(unanswered->state == CopyJob::waiting);
        unanswered->stop = true;
        wait(unanswered);
        assert(unanswered->state == CopyJob::cancelled);
        auto renamed = root / (std::wstring(80, L'x') + L"-\u65e5\u672c.bin");
        rename_item(a, renamed);
        assert(fs::exists(renamed) && !fs::exists(a) && fs::exists(sidecar(renamed)) && !fs::exists(sidecar(a)));
        assert(read_bytes(sidecar(renamed)) == original);
        auto case_name = renamed.parent_path() / (std::wstring(80, L'X') + L"-\u65e5\u672c.bin");
        rename_item(renamed, case_name);
        assert(read_bytes(sidecar(case_name)) == original);
        auto empty = root / L"Empty metadata";
        write(empty, {});
        write(sidecar(empty), {});
        try {
            set_label(empty, 1);
            assert(false);
        }
        catch (const std::exception&) {
        }
        assert(read_bytes(sidecar(empty)).empty());
        auto malformed = root / L"Malformed";
        write(malformed, {});
        write(sidecar(malformed), Bytes{0, 5, 22, 7});
        try {
            set_label(malformed, 1);
            assert(false);
        }
        catch (const std::exception&) {
        }
        assert(read_bytes(sidecar(malformed)) == Bytes({0, 5, 22, 7}));
        auto overlap = metadata(1);
        put32(overlap, 30, 1);
        write(sidecar(malformed), overlap);
        try {
            mac_file(malformed);
            assert(false);
        }
        catch (const std::exception&) {
        }
        auto outside = metadata(1);
        put32(outside, 42, UINT32_MAX);
        write(sidecar(malformed), outside);
        try {
            mac_file(malformed);
            assert(false);
        }
        catch (const std::exception&) {
        }
        assert(hidden_metadata(sidecar(renamed)) && !hidden_metadata(renamed));
        assert(format_bytes(UINT64_C(2) * 1024 * 1024 * 1024 * 1024) == L"2 TB");
        for (const auto& arg : {L"", L"D:\\Source Code\\", L"a\"quoted\\value", L"\u65e5\u672c"}) {
            auto cmd = L"program " + quote_argument(arg);
            int count;
            auto values = CommandLineToArgvW(cmd.c_str(), &count);
            assert(values && count == 2 && values[1] == std::wstring(arg));
            LocalFree(values);
        }
        // Image copies have no physical source; the writer streams straight
        // into requested-destination staging with an ordinary AppleDouble fork.
        auto stream_source = root / L"nonexistent-image-root" / L"Stream";
        auto stream_target = root / L"Stream";
        auto stream = manager.start_targets({{stream_source, stream_target}}, TransferKind::copy,
                                            [&](size_t index, const fs::path& target, CopyJob& job) {
                                                assert(index == 0 && !fs::exists(stream_source));
                                                write(target, Bytes{1, 2, 3});
                                                write(sidecar(target), original);
                                                job.total = job.done = 3;
                                            },
                                            {false});
        stream->worker.join();
        assert(stream->state == CopyJob::complete && stream->items == 0);
        assert(read_bytes(stream_target) == Bytes({1, 2, 3}));
        assert(read_bytes(sidecar(stream_target)) == original);
        auto stream_folder = root / L"Stream folder";
        auto folder_stream = manager.start_targets({{stream_source, stream_folder}}, TransferKind::copy,
                                                   [&](size_t, const fs::path& target, CopyJob&) {
                                                       fs::create_directory(target);
                                                       write(target / L"Child", Bytes{4});
                                                       write(sidecar(target), original);
                                                   },
                                                   {true});
        folder_stream->worker.join();
        assert(folder_stream->state == CopyJob::complete && folder_stream->items == 0);
        assert(read_bytes(stream_folder / L"Child") == Bytes({4}));
        assert(read_bytes(sidecar(stream_folder)) == original);
        auto cancelled_target = root / L"Cancelled stream";
        auto cancelled_stream = manager.start_targets({{stream_source, cancelled_target}}, TransferKind::copy,
                                                      [&](size_t, const fs::path& target, CopyJob& job) {
                                                          write(target, Bytes{5});
                                                          job.stop = true;
                                                          throw std::runtime_error("Stopped");
                                                      },
                                                      {false});
        cancelled_stream->worker.join();
        assert(cancelled_stream->state == CopyJob::cancelled && !fs::exists(cancelled_target));
        assert(!manager.active());
        {
            auto source = root / L"Temporarily busy folder", destination = root / L"Busy destination";
            fs::create_directories(source);
            fs::create_directory(destination);
            write(source / L"Data", Bytes{1, 2, 3});
            write(sidecar(source / L"Data"), original);
            staged_moves = true;
            lock_publication = true;
            auto job = manager.start({source}, destination, TransferKind::move);
            wait(job);
            assert(publication_release.joinable());
            publication_release.join();
            staged_moves = false;
            assert(job->state == CopyJob::complete && !fs::exists(source));
            assert(read_bytes(destination / source.filename() / L"Data") == Bytes({1, 2, 3}));
            assert(read_bytes(sidecar(destination / source.filename() / L"Data")) == original);
        }
        for (const auto& entry : fs::directory_iterator(root))
            assert(entry.path().filename().wstring().rfind(L".Finder-copy-", 0) != 0);
        check_shortcut_icons(root);
        fs::remove_all(root);
        std::cout
            << "PASS: native concurrent copies/moves, verified source removal, changed-source protection, forks, independent Stop, reuse, nested folders, same-name Trash/state/Put Away, Unicode rename, labels, bounds and Windows quoting\n";
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << "\nFixture retained: " << root << '\n';
        return 1;
    }
}
