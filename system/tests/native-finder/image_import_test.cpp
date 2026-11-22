/*+===================================================================
  File:      image_import_test.cpp

  Summary:   Exercise cooperative image imports with intercepted mounted File Manager operations.

  Classes:   Record; uses finder::Copies and finder::CopyJob.

  Functions: Mounted image bridge interceptors; main.

  Origin:    Executor native Finder regression fixture.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include "finder/image_copy.h"
#include "finder/actions.h"
#include "finder/image_volumes.h"
#include "rsys/desktop_database.h"
#include <array>
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>

using namespace finder;

struct Record {
    uint32_t parent;
    std::string name;
    bool directory;
    std::array<unsigned char, 32> info;
    std::vector<unsigned char> forks[2];
};

static std::map<uint32_t, Record> records;
static std::map<int16_t, std::pair<uint32_t, int>> opened;
static std::map<int16_t, size_t> read_positions;
static uint32_t next_id = 20;
static int16_t next_file = 1;
static bool readonly = false, fail_write = false, fail_publish = false;
static unsigned writes;

extern "C" int16_t ROMlib_finder_image_writable(int16_t)
{
    return readonly ? -44 : 0;
}

extern "C" int16_t ROMlib_finder_image_lookup(int16_t, uint32_t parent, const char* name, uint32_t* id)
{
    for (const auto& r : records)
        if (r.second.parent == parent && !_stricmp(r.second.name.c_str(), name)) {
            if (id)
                *id = r.first;
            return 0;
        }
    return -43;
}

extern "C" int16_t ROMlib_finder_image_create(int16_t v, uint32_t parent, const char* name, int dir,
                                              const unsigned char info[32], uint32_t* id)
{
    if (readonly)
        return -44;
    if (!ROMlib_finder_image_lookup(v, parent, name, nullptr))
        return -48;
    Record r{parent, name, dir != 0, {}, {}};
    std::copy_n(info, 32, r.info.begin());
    records.emplace(*id = next_id++, std::move(r));
    return 0;
}

extern "C" int16_t ROMlib_finder_image_info(int16_t v, uint32_t parent, const char* name, FinderImageEntry* entry)
{
    uint32_t id;
    if (auto error = ROMlib_finder_image_lookup(v, parent, name, &id))
        return error;
    const auto& record = records.at(id);
    *entry = {};
    entry->id = id;
    entry->parent = parent;
    entry->directory = record.directory;
    strcpy(entry->name, record.name.c_str());
    std::copy(record.info.begin(), record.info.end(), entry->finder_info);
    entry->data_size = record.forks[0].size();
    entry->resource_size = record.forks[1].size();
    entry->data_allocated = (entry->data_size + 511) & ~511u;
    entry->resource_allocated = (entry->resource_size + 511) & ~511u;
    auto info = mac_catalog_info(record.info.data(), record.directory);
    entry->type = info.type;
    entry->creator = info.creator;
    entry->flags = info.flags;
    entry->children =
        std::count_if(records.begin(), records.end(), [id](const auto& child) { return child.second.parent == id; });
    return 0;
}

extern "C" int ROMlib_finder_image_version(int16_t, uint32_t, const char* name, void* bytes, size_t capacity,
                                           size_t* count)
{
    const unsigned char version[]{1, 0x20, 0x80, 0, 0, 0, 3, '1', '.', '2'};
    if (strcmp(name, "Payload") || capacity < sizeof version)
        return 0;
    memcpy(bytes, version, sizeof version);
    *count = sizeof version;
    return 1;
}

extern "C" int ROMlib_finder_image_desktop(int16_t, int operation, unsigned char key[38], unsigned index, void* data,
                                           size_t* count, uint32_t*)
{
    uint32_t id = uint32_t(key[2]) << 24 | uint32_t(key[3]) << 16 | uint32_t(key[4]) << 8 | key[5];
    const unsigned char comment[]{7, 'F', 'i', 'x', 't', 'u', 'r', 'e'};
    auto found = records.find(id);
    if (operation != desktop_get || index || found == records.end() || found->second.name != "Payload" ||
        *count < sizeof comment)
        return -43;
    memcpy(data, comment, sizeof comment);
    *count = sizeof comment;
    return 0;
}

extern "C" int16_t ROMlib_finder_image_entry(int16_t v, uint32_t parent, int16_t index, FinderImageEntry* entry)
{
    if (index == -1) {
        auto found = records.find(parent);
        if (found == records.end() || !found->second.directory)
            return -43;
        return ROMlib_finder_image_info(v, found->second.parent, found->second.name.c_str(), entry);
    }
    for (const auto& child : records)
        if (child.second.parent == parent && !--index)
            return ROMlib_finder_image_info(v, parent, child.second.name.c_str(), entry);
    return -43;
}

extern "C" int16_t ROMlib_finder_image_set_info(int16_t v, uint32_t parent, const char* name,
                                                const unsigned char info[32])
{
    uint32_t id;
    auto e = ROMlib_finder_image_lookup(v, parent, name, &id);
    if (!e)
        std::copy_n(info, 32, records.at(id).info.begin());
    return e;
}

extern "C" int16_t ROMlib_finder_image_open_fork(int16_t v, uint32_t parent, const char* name, int fork, int16_t* file)
{
    uint32_t id;
    auto e = ROMlib_finder_image_lookup(v, parent, name, &id);
    if (!e)
        opened[*file = next_file++] = {id, fork};
    return e;
}

extern "C" int16_t ROMlib_finder_image_write(int16_t file, const void* bytes, uint32_t count)
{
    assert(count <= 65536);
    if (fail_write)
        return -34;
    ++writes;
    auto f = opened.at(file);
    auto& out = records.at(f.first).forks[f.second];
    auto begin = static_cast<const unsigned char*>(bytes);
    out.insert(out.end(), begin, begin + count);
    // Ensure cancellation exercises a live, partially written destination.
    std::this_thread::sleep_for(std::chrono::milliseconds(9));
    return 0;
}

extern "C" int16_t ROMlib_finder_image_open_source_fork(int16_t v, uint32_t parent, const char* name, int fork,
                                                        int16_t* file)
{
    auto error = ROMlib_finder_image_open_fork(v, parent, name, fork, file);
    if (!error)
        read_positions[*file] = 0;
    return error;
}

extern "C" int16_t ROMlib_finder_image_read(int16_t file, void* bytes, uint32_t count)
{
    auto& position = read_positions.at(file);
    auto source = opened.at(file);
    const auto& contents = records.at(source.first).forks[source.second];
    if (count > contents.size() - position)
        return -39;
    memcpy(bytes, contents.data() + position, count);
    position += count;
    return 0;
}

extern "C" int16_t ROMlib_finder_image_close_fork(int16_t file)
{
    opened.erase(file);
    read_positions.erase(file);
    return 0;
}

extern "C" int16_t ROMlib_finder_image_delete(int16_t v, uint32_t parent, const char* name)
{
    uint32_t id;
    auto e = ROMlib_finder_image_lookup(v, parent, name, &id);
    if (!e) {
        for (const auto& f : opened)
            assert(f.second.first != id);
        for (const auto& r : records)
            assert(r.second.parent != id);
        records.erase(id);
    }
    return e;
}

extern "C" int16_t ROMlib_finder_image_rename(int16_t v, uint32_t parent, const char* name, const char* target)
{
    if (fail_publish && std::string(name).find(".Fndr-copy-") == 0)
        return -36;
    if (!ROMlib_finder_image_lookup(v, parent, target, nullptr))
        return -48;
    uint32_t id;
    auto e = ROMlib_finder_image_lookup(v, parent, name, &id);
    if (!e)
        records.at(id).name = target;
    return e;
}

extern "C" int16_t ROMlib_finder_image_flush(int16_t)
{
    return 0;
}

extern "C" int16_t ROMlib_finder_image_check_mutable(int16_t v, uint32_t parent, const char* name, uint32_t id)
{
    if (readonly)
        return -44;
    uint32_t found;
    auto error = ROMlib_finder_image_lookup(v, parent, name, &found);
    return error ? error : found == id ? 0 : -43;
}

extern "C" int16_t ROMlib_finder_image_move(int16_t v, uint32_t parent, const char* name, uint32_t id,
                                            uint32_t target_parent)
{
    auto error = ROMlib_finder_image_check_mutable(v, parent, name, id);
    if (!error && !ROMlib_finder_image_lookup(v, target_parent, name, nullptr))
        return -48;
    if (!error)
        records.at(id).parent = target_parent;
    return error;
}

static void finish(Copies& copies, const std::shared_ptr<CopyJob>& job)
{
    for (unsigned n = 0; n < 10000 && job->state < CopyJob::complete; ++n)
        copies.tick();
    assert(job->state >= CopyJob::complete);
    assert(opened.empty());
}

int main(int argc, char** argv)
{
    assert(argc == 3);
    fs::path work = fs::u8path(argv[1]), image = fs::u8path(argv[2]);
    assert(fs::create_directories(work / "Folder"));
    std::vector<unsigned char> data(200001, 0x36), resource(70003, 0x79);
    {
        std::ofstream f(work / "Folder" / "Payload", std::ios::binary);
        f.write((char*)data.data(), data.size());
    }
    fs::create_directory(work / "Folder" / ".rsrc");
    {
        std::ofstream f(work / "Folder" / ".rsrc" / "Payload", std::ios::binary);
        f.write((char*)resource.data(), resource.size());
    }
    fs::create_directory(work / "Folder" / ".finf");
    std::array<unsigned char, 32> info{};
    std::copy_n("APPLTst1", 8, info.begin());
    info[8] = 0x04;
    info[31] = 27;
    {
        std::ofstream f(work / "Folder" / ".finf" / "Payload", std::ios::binary);
        f.write((char*)info.data(), info.size());
    }
    std::atomic<bool> stop_size{false};
    auto host_size = host_folder_size(work / "Folder", stop_size);
    assert(host_size && host_size->logical == data.size() + resource.size() && host_size->allocated);
    assert(*host_size->allocated >= host_size->logical);
    stop_size = true;
    assert(!host_folder_size(work / "Folder", stop_size));
    Copies copies;
    auto start = [&] { return copy_into_image(copies, {work / "Folder"}, {}, -9, 2, work / "synthetic-volume"); };
    readonly = true;
    bool rejected = false;
    try {
        start();
    }
    catch (const std::exception&) {
        rejected = true;
    }
    assert(rejected && records.empty());
    readonly = false;
    auto new_id = create_image_folder(-9, 2);
    auto second_id = create_image_folder(-9, 2);
    assert(records.at(second_id).name == "Untitled Folder 2");
    ImageCatalogItem first{2, new_id, "Untitled Folder"};
    first.name = rename_image_item(-9, first, L"Renamed");
    first.name = rename_image_item(-9, first, L"RENAMED");
    for (auto invalid : {L"Untitled Folder 2", L"Name:Part", L"01234567890123456789012345678901"}) {
        try {
            rename_image_item(-9, first, invalid);
            assert(false);
        }
        catch (const std::exception&) {
            assert(records.at(new_id).name == "RENAMED");
        }
    }
    readonly = true;
    try {
        rename_image_item(-9, first, L"Read only");
        assert(false);
    }
    catch (const std::exception&) {
        assert(records.at(new_id).name == "RENAMED");
    }
    readonly = false;
    label_image_item(-9, first, 3);
    assert(records.at(new_id).name == "RENAMED" && records.at(new_id).info[9] == 6);
    try {
        rename_image_item(-9, {2, second_id, "Renamed"}, L"Stale");
        assert(false);
    }
    catch (const std::exception&) {
        assert(records.at(new_id).name == "RENAMED");
    }
    create_image_folder(-9, new_id);
    delete_image_items(-9, {first, {2, second_id, "Untitled Folder 2"}});
    assert(records.empty());
    auto job = start();
    copies.tick();
    assert(writes == 1 && job->state == CopyJob::copying && !opened.empty());
    FindOptions private_query{{FindField::name, FindRelation::starts, L".Fndr-"}};
    Search private_search({{work / "synthetic-volume", -9}}, private_query, true);
    while (!private_search.finished())
        private_search.advance_images();
    assert(private_search.snapshot().empty());
    job->stop = true;
    finish(copies, job);
    assert(job->state == CopyJob::cancelled && records.empty());
    fail_write = true;
    job = start();
    finish(copies, job);
    assert(job->state == CopyJob::failed && records.empty());
    fail_write = false;
    job = start();
    finish(copies, job);
    assert(job->state == CopyJob::complete && records.size() == 2 && job->done == data.size() + resource.size());
    uint32_t folder, payload;
    assert(!ROMlib_finder_image_lookup(-9, 2, "Folder", &folder));
    assert(!ROMlib_finder_image_lookup(-9, folder, "Payload", &payload));
    assert(records.at(payload).forks[0] == data && records.at(payload).forks[1] == resource);
    auto file_title = rename_image_item(-9, {folder, payload, "Payload"}, L"Caf\u00e9");
    assert(file_title == "Caf\x8e" && records.at(payload).forks[0] == data && records.at(payload).forks[1] == resource);
    assert(rename_image_item(-9, {folder, payload, file_title}, L"Payload") == "Payload");
    readonly = true;
    ImageFolderSize published_size(-9, folder);
    while (!published_size.advance()) {
    }
    auto file_size = published_size.size();
    assert(file_size.logical == data.size() + resource.size() && file_size.allocated == 200192 + 70144 &&
           opened.empty());
    // Exercise multiple timer slices with nested and resource-only files.
    uint32_t nested = next_id++;
    records.emplace(nested, Record{folder, "Nested", true, {}, {}});
    for (unsigned i = 0; i < 80; ++i)
        records.emplace(next_id++, Record{nested, "Part " + std::to_string(i), false, {}, {{}, {1, 2, 3}}});
    ImageFolderSize deep_size(-9, folder);
    assert(!deep_size.advance());
    while (!deep_size.advance()) {
    }
    assert(deep_size.size().logical == file_size.logical + 240);
    assert(deep_size.size().allocated == *file_size.allocated + 80 * 512 && opened.empty());
    for (auto record = records.lower_bound(nested); record != records.end();)
        record = records.erase(record);
    bool missing_folder = false;
    try {
        ImageFolderSize missing(-9, nested);
        missing.advance();
    }
    catch (const std::exception&) {
        missing_folder = true;
    }
    assert(missing_folder);
    auto volume_key = work / "synthetic-volume";
    FindOptions expanded;
    expanded.expanded = true;
    expanded.all_at_once = true;
    for (auto query : {FindQuery{FindField::name, FindRelation::ends, L"LOAD"},
                       FindQuery{FindField::size, FindRelation::greater, L"270000"},
                       FindQuery{FindField::kind, FindRelation::equal, L"", unsigned(FindKind::application)},
                       FindQuery{FindField::version, FindRelation::equal, L"1.2"},
                       FindQuery{FindField::comments, FindRelation::contains, L"fixture"}}) {
        assert(!query.validate());
        expanded.query = query;
        Search mounted({{volume_key, -9}}, expanded);
        assert(!mounted.finished() && mounted.result_count() == 0);
        for (unsigned n = 0; n < 10 && !mounted.finished(); ++n)
            mounted.advance_images();
        assert(mounted.finished() && mounted.result_count() == 1 && mounted.problem().empty());
        const auto match = mounted.snapshot().front();
        assert(match.catalog.id == payload && match.catalog.parent == folder && match.image == -9);
        assert(match.path == volume_key / std::to_wstring(folder) / std::to_wstring(payload));
    }
    Search selected(
        {{volume_key / std::to_wstring(folder) / std::to_wstring(payload), -9, payload, folder, "Payload", true}},
        expanded);
    while (!selected.finished())
        selected.advance_images();
    assert(selected.result_count() == 1);
    expanded.query = {FindField::name, FindRelation::equal, L"Payload"};
    Search mixed({{work / "Folder"}, {volume_key, -9}}, expanded);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!mixed.finished() && std::chrono::steady_clock::now() < deadline) {
        mixed.advance_images();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(mixed.finished() && mixed.result_count() == 2 && mixed.problem().empty());
    readonly = false;
    auto duplicate_folder = [&] {
        return copy_into_image(copies, {}, {{image, 0, {}, -9, {2, folder, "Folder"}}}, -9, 2,
                               work / "synthetic-volume", TransferKind::duplicate);
    };
    auto first_copy = duplicate_folder();
    auto second_copy = duplicate_folder();
    for (unsigned n = 0; n < 10000 && copies.active(); ++n)
        copies.tick();
    assert(first_copy->state == CopyJob::complete && second_copy->state == CopyJob::complete && opened.empty());
    for (auto name : {"Folder copy", "Folder copy 2"}) {
        uint32_t copy_dir, copy_file;
        assert(!ROMlib_finder_image_lookup(-9, 2, name, &copy_dir));
        assert(!ROMlib_finder_image_lookup(-9, copy_dir, "Payload", &copy_file));
        assert(records.at(copy_file).forks[0] == data && records.at(copy_file).forks[1] == resource);
        delete_image_items(-9, {{2, copy_dir, name}});
    }
    auto move_folder = create_image_folder(-9, 2);
    auto moving = copy_into_image(copies, {}, {{image, 0, {}, -9, {2, folder, "Folder"}}}, -9, move_folder,
                                  work / "synthetic-volume" / "move-target", TransferKind::move);
    finish(copies, moving);
    assert(moving->state == CopyJob::complete && records.at(folder).parent == move_folder);
    assert(records.at(payload).forks[0] == data && records.at(payload).forks[1] == resource);
    assert(!ROMlib_finder_image_move(-9, move_folder, "Folder", folder, 2));
    delete_image_items(-9, {{2, move_folder, "Untitled Folder"}});
    assert(records.at(payload).info == info);
    ImageCatalogItem pad{folder, payload, "Payload"};
    stationery_image_item(-9, pad, true);
    auto materialized = copy_into_image(copies, {}, {{image, 0, {}, -9, pad}}, -9, folder,
                                        work / "synthetic-volume" / "Folder", TransferKind::stationery, "New Document");
    finish(copies, materialized);
    uint32_t document;
    assert(materialized->state == CopyJob::complete);
    assert(!ROMlib_finder_image_lookup(-9, folder, "New Document", &document));
    assert(records.at(document).info == info && records.at(document).forks[0] == data &&
           records.at(document).forks[1] == resource);
    assert(records.at(payload).info[8] & (stationery_flag >> 8));
    assert(!ROMlib_finder_image_delete(-9, folder, "New Document"));
    stationery_image_item(-9, pad, false);
    auto conflict = [&] {
        auto pending = start();
        copies.tick();
        assert(pending->state == CopyJob::waiting && pending->conflict_directory && pending->conflict_pending);
        return pending;
    };
    for (auto answer : {ConflictAnswer::skip, ConflictAnswer::stop}) {
        job = conflict();
        job->answer_conflict(answer);
        finish(copies, job);
        assert(job->state == (answer == ConflictAnswer::skip ? CopyJob::complete : CopyJob::cancelled));
        assert(records.size() == 2 && records.at(payload).forks[1] == resource);
    }
    job = conflict();
    records.at(payload).forks[0].push_back(1);
    job->answer_conflict(ConflictAnswer::replace);
    finish(copies, job);
    assert(job->state == CopyJob::failed && records.at(payload).forks[0].size() == data.size() + 1);
    job = conflict();
    fail_publish = true;
    job->answer_conflict(ConflictAnswer::replace);
    finish(copies, job);
    fail_publish = false;
    assert(job->state == CopyJob::failed && records.at(folder).name == "Folder" && records.size() == 2);
    job = conflict();
    job->answer_conflict(ConflictAnswer::replace);
    finish(copies, job);
    assert(job->state == CopyJob::complete && !records.count(payload) && records.size() == 2);
    assert(!ROMlib_finder_image_lookup(-9, 2, "Folder", &folder));
    assert(!ROMlib_finder_image_lookup(-9, folder, "Payload", &payload));
    assert(records.at(payload).forks[0] == data && records.at(payload).forks[1] == resource);
    records.clear();
    job = copy_into_image(copies, {}, {{image, 0, ":Tools"}}, -9, 2, work / "synthetic-volume");
    finish(copies, job);
    assert(job->state == CopyJob::complete && records.size() >= 2);
    assert(!ROMlib_finder_image_lookup(-9, 2, "Tools", &folder));
    assert(!ROMlib_finder_image_lookup(-9, folder, "Application", &payload));
    assert(records.at(payload).info[0] == 'A');
    records.clear();
    job = start();
    copies.tick();
    copies.cancel_all();
    assert(job->state == CopyJob::cancelled && opened.empty() && records.empty());
    assert(fs::file_size(work / "Folder" / "Payload") == data.size());
    std::cout << "PASS: image operations, host and mounted folder sizes, both forks, conflicts, and Find\n";
}
