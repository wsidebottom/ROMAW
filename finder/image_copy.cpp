/*+===================================================================
  File:      image_copy.cpp

  Summary:   Search and manage mounted image catalogs and stream both forks between images and host folders.

  Classes:   ImageCopySource, ImportNode, ImageImport; uses CopyJob, Copies, HostMoveLease and owned direct image readers.

  Functions: finder::copy_image_items, finder::copy_into_image, finder::create_image_folder,
             finder::rename_image_item, finder::label_image_item, finder::stationery_image_item,
             finder::delete_image_items, finder::image_catalog_name, finder::image_copy_private,
             finder::image_search_comment, finder::Search::advance_images, finder::ImageFolderSize methods.

  Origin:    Executor native Finder direct image integration.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "finder/image_copy.h"
#include "finder/actions.h"
#include "romaw_images.h"
#include "finder/image_volumes.h"
#include "finder/views.h"
#include "rsys/desktop_database.h"
#include "rsys/mac_storage.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <climits>
#include <map>
#include <set>
#include <stdexcept>

namespace finder {
std::shared_ptr<CopyJob> copy_image_items(Copies& copies, const std::vector<ImageCopySource>& sources,
                                          const fs::path& destination, const std::wstring& stationery_name)
{
    using Reader = std::shared_ptr<romaw_image_set>;

    struct Selection {
        Reader reader;
        std::string path;
    };

    std::map<fs::path, Reader> readers;
    std::vector<Selection> selections;
    std::vector<std::pair<fs::path, fs::path>> changes;
    std::vector<bool> directories;
    for (const auto& source : sources) {
        char error[1024]{}, path[32768]{};
        auto& reader = readers[source.source];
        if (!reader) {
            reader = Reader(romaw_images_open_direct_utf8(source.source.u8string().c_str(), error, sizeof error),
                            [](romaw_image_set* set) { romaw_images_close(set, 0); });
            if (!reader)
                throw std::runtime_error(error);
        }
        if (!romaw_images_guest_path_utf8(reader.get(), source.partition, source.guest.c_str(), path, sizeof path,
                                          error, sizeof error))
            throw std::runtime_error(error);
        romaw_image_item item{};
        if (!romaw_images_stat_utf8(reader.get(), path, &item))
            throw std::runtime_error("The selected image item is unavailable");
        auto original = fs::u8path(path);
        if (!stationery_name.empty() && (sources.size() != 1 || item.directory))
            throw std::runtime_error("Choose one stationery document.");
        changes.emplace_back(original,
                             destination / (stationery_name.empty() ? original.filename() : fs::path(stationery_name)));
        directories.push_back(item.directory != 0);
        selections.push_back({reader, path});
    }
    return copies.start_targets(
        changes, stationery_name.empty() ? TransferKind::copy : TransferKind::stationery,
        [selections = std::move(selections)](size_t index, const fs::path& target, CopyJob& job) {
            struct Progress {
                CopyJob& job;
                uint64_t done, total;
            } progress{job, job.done.load(), job.total.load()};
            char error[1024]{};
            const auto& source = selections.at(index);
            if (!romaw_images_export_progress_utf8(
                    source.reader.get(), source.path.c_str(), target.u8string().c_str(),
                    [](void* opaque, uint64_t done, uint64_t total) -> int {
                        auto& state = *static_cast<Progress*>(opaque);
                        state.job.total = state.total + total;
                        state.job.done = state.done + done;
                        return !state.job.stop;
                    },
                    &progress, error, sizeof error))
                throw std::runtime_error(error);
        },
        std::move(directories));
}

namespace {
using Reader = std::shared_ptr<romaw_image_set>;
// Owned by the Finder event thread, including during a nested conflict dialog.
std::set<std::pair<int16_t, uint32_t>> staging_roots;

struct ImportNode {
    fs::path source, resource;
    Reader reader;
    std::string virtual_path, name;
    ImageCatalogItem live{};
    romaw_image_item info{};
    uint64_t data_offset = 0, resource_offset = 0;
    Bytes binary_prefix;
    std::vector<fs::path> companions;
    std::vector<std::pair<fs::path, uint64_t>> metadata_hashes;
    size_t parent = SIZE_MAX, end = 0;
    uint32_t target_id = 0, target_parent = 0;
    std::string target_name;
};

uint64_t metadata_hash(const Bytes& bytes, uint64_t hash = 14695981039346656037ULL)
{
    for (auto byte : bytes)
        hash = (hash ^ byte) * 1099511628211ULL;
    return hash;
}

std::vector<HostFileStamp> host_tree_stamps(const fs::path& root)
{
    std::map<fs::path, HostFileStamp> stamps;
    auto add = [&](const fs::path& path) { stamps.emplace(path, host_file_stamp(path)); };
    add(root);
    for (const auto& companion : mac_companions(root))
        add(companion);
    if (fs::is_directory(root))
        for (const auto& entry : fs::recursive_directory_iterator(root))
            add(entry.path());
    std::vector<HostFileStamp> result;
    for (auto& entry : stamps)
        result.push_back(std::move(entry.second));
    return result;
}

std::wstring catalog_text(const std::string& name)
{
    int length = MultiByteToWideChar(10000, 0, name.data(), int(name.size()), nullptr, 0);
    std::wstring text(length, 0);
    MultiByteToWideChar(10000, 0, name.data(), int(name.size()), text.data(), length);
    return text;
}

// A Macintosh slash is part of a name. Escape path syntax in the job key
// independently of the name passed to the catalog or shown in the alert.
fs::path image_job_target(const fs::path& folder, const std::string& name)
{
    std::wstring key;
    for (wchar_t ch : catalog_text(name)) {
        if (ch == L'/' || ch == L'\\' || ch == L':' || ch == L'%' || ch == L'.')
            key += L"%" + std::to_wstring(ch) + L"%";
        else
            key += ch;
    }
    return folder / key;
}

void mac_error(int16_t error)
{
    if (error)
        throw std::runtime_error(error == -44   ? "The destination image is read-only"
                                 : error == -45 ? "The selected item is locked"
                                 : error == -47 ? "The selected item is in use"
                                 : error == -48
                                     ? "An item with that name already exists in the image"
                                     : "Mounted-image copy failed (Macintosh error " + std::to_string(error) + ")");
}

std::string catalog_name(const std::wstring& text)
{
    if (text.empty() || text.size() > 31)
        throw std::runtime_error("An image filename must fit in 31 Macintosh characters");
    int count = WideCharToMultiByte(10000, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    if (count < 1 || count > 31)
        throw std::runtime_error("An image filename must fit in 31 Macintosh characters");
    std::string name(count, 0);
    WideCharToMultiByte(10000, 0, text.data(), int(text.size()), name.data(), count, nullptr, nullptr);
    int length = MultiByteToWideChar(10000, 0, name.data(), count, nullptr, 0);
    std::wstring back(length, 0);
    MultiByteToWideChar(10000, 0, name.data(), count, back.data(), length);
    if (back != text || name.find(':') != std::string::npos || name.find('\0') != std::string::npos)
        throw std::runtime_error("A filename cannot be represented on this Macintosh image");
    return name;
}

std::string host_mac_name(const fs::path& path, const romaw_image_item& info)
{
    MacFile metadata;
    auto word = [](const unsigned char* bytes) {
        return uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 8 | bytes[3];
    };
    metadata.type = word(info.finder_info);
    metadata.creator = word(info.finder_info + 4);
    return catalog_name(mac_display_name(path, metadata));
}

FinderImageEntry catalog_item(int16_t reference, const ImageCatalogItem& item)
{
    FinderImageEntry entry{};
    mac_error(ROMlib_finder_image_info(reference, item.parent, item.name.c_str(), &entry));
    if (entry.id <= 2 || entry.id != item.id || entry.parent != item.parent)
        throw std::runtime_error("The selected item has changed");
    return entry;
}

std::string unused_image_name(int16_t reference, uint32_t directory, const char* role)
{
    static unsigned number;
    for (unsigned attempt = 0; attempt < 1000; ++attempt) {
        auto name =
            std::string(".Fndr-") + role + "-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++number);
        if (name.size() > 31)
            break;
        int16_t error = ROMlib_finder_image_lookup(reference, directory, name.c_str(), nullptr);
        if (error == -43)
            return name;
        mac_error(error);
    }
    throw std::runtime_error("Cannot reserve a temporary image name");
}

// Preorder snapshots can be removed in reverse without invalidating the
// File Manager's one-based directory enumeration.
void catalog_tree(int16_t reference, const FinderImageEntry& entry, std::vector<FinderImageEntry>& result,
                  unsigned depth)
{
    if (depth > 128 || result.size() >= 100000 || (entry.directory && entry.children > INT16_MAX))
        throw std::runtime_error("The folder exceeds supported catalog limits");
    if (image_copy_private(reference, entry.id))
        throw std::runtime_error("Another operation is using this folder");
    result.push_back(entry);
    if (entry.directory)
        for (int index = 1; index <= entry.children; ++index) {
            FinderImageEntry child{};
            mac_error(ROMlib_finder_image_entry(reference, entry.id, index, &child));
            catalog_tree(reference, child, result, depth + 1);
        }
}

struct ImageImport {
    struct Created {
        uint32_t parent, id;
        std::string name;
    };

    int16_t reference;
    uint32_t directory;
    bool moving;
    bool host_moving = false;
    std::map<size_t, std::vector<HostFileStamp>> host_stamps;
    std::shared_ptr<HostMoveLease> host_lease;
    size_t verify_node = 0;
    unsigned verify_fork = 0;
    uint64_t verify_offset = 0;
    std::vector<ImportNode> nodes;
    std::vector<size_t> roots;
    size_t root = 0, current = 0;
    std::vector<Created> created;
    std::string temporary, guard;
    uint32_t guard_id = 0, staging_id = 0;
    int16_t file = -1, source_file = -1, lease = -1;
    unsigned fork = 0;
    uint64_t offset = 0;
    std::ifstream input;
    bool active = false;
    bool awaiting_answer = false;
    std::vector<FinderImageEntry> approved;
    std::set<uint32_t> source_ids;
    uint64_t total = 0;
    std::array<unsigned char, 65536> buffer;

    ImageImport(int16_t volume, uint32_t parent, bool moving) : reference(volume), directory(parent), moving(moving)
    {
    }

    ~ImageImport()
    {
        abort();
    }

    void add_host(const fs::path& path, size_t parent, unsigned depth)
    {
        if (nodes.size() >= 100000 || depth > 128)
            throw std::runtime_error("The copy exceeds supported catalog limits");
        if (GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)
            throw std::runtime_error("Linked items cannot be copied into an image");
        ImportNode node;
        node.source = path;
        node.parent = parent;
        if (host_moving) {
            node.companions = mac_companions(path);
            for (const auto& companion : node.companions)
                node.metadata_hashes.push_back({companion, metadata_hash(read_bytes(companion))});
        }
        char error[1024]{}, resource[32768]{};
        if (!romaw_images_host_info_utf8(path.u8string().c_str(), &node.info, resource, sizeof resource,
                                         &node.resource_offset, error, sizeof error))
            throw std::runtime_error(error);
        node.resource = fs::u8path(resource);
        node.name = host_mac_name(path, node.info);
        if (!node.info.directory && windows_destination(path) && mac_companions(path).empty()) {
            ImportedFile binary;
            Bytes finder;
            fs::path name;
            if (read_macbinary(path, binary)) {
                unsigned count = 0;
                if (!romaw_mac_storage_table(binary.metadata.data(), binary.metadata.size(), &count))
                    throw std::runtime_error("Invalid MacBinary Finder metadata");
                for (unsigned i = 0; i < count; ++i) {
                    ROMAWMacStorageEntry entry{};
                    if (!romaw_mac_storage_entry(binary.metadata.data(), binary.metadata.size(), binary.metadata.size(),
                                                 i, &entry) ||
                        (entry.id != 2 && entry.id != 9) || (entry.id == 9 && entry.length > 32))
                        throw std::runtime_error("This image cannot preserve the MacBinary extended metadata");
                }
                name = binary.name;
                finder = std::move(binary.finder_info);
                node.info.data_size = binary.data_size;
                node.info.resource_size = binary.resource_size;
                node.data_offset = binary.data_offset;
                node.resource_offset = binary.resource_offset;
                node.resource = path;
                node.binary_prefix = std::move(binary.prefix);
            }
            else
                name = host_import_name(path, &finder);
            node.name = catalog_name(name.wstring());
            if (!finder.empty()) {
                if (finder.size() != 32)
                    throw std::runtime_error("The source has incomplete Finder information");
                std::copy_n(finder.begin(), 32, node.info.finder_info);
            }
        }
        size_t index = nodes.size();
        nodes.push_back(std::move(node));
        if (nodes[index].info.directory)
            for (const auto& child : fs::directory_iterator(path))
                if (!hidden_metadata(child.path()))
                    add_host(child.path(), index, depth + 1);
        nodes[index].end = nodes.size();
    }

    void add_image(const Reader& reader, const std::string& path, size_t parent, unsigned depth)
    {
        if (nodes.size() >= 100000 || depth > 128)
            throw std::runtime_error("The copy exceeds supported catalog limits");
        ImportNode node;
        node.reader = reader;
        node.virtual_path = path;
        node.parent = parent;
        char name[32]{};
        if (!romaw_images_stat_utf8(reader.get(), path.c_str(), &node.info) ||
            !romaw_images_guest_name_utf8(reader.get(), path.c_str(), name))
            throw std::runtime_error("The source image item is unavailable");
        node.name = name;
        size_t index = nodes.size();
        nodes.push_back(std::move(node));
        if (nodes[index].info.directory) {
            struct Listing {
                std::vector<std::string> paths;
                bool failed = false;
            } list;

            if (!romaw_images_list_utf8(
                    reader.get(), path.c_str(),
                    [](const char* child, const romaw_image_item*, void* opaque) -> int {
                        auto& list = *static_cast<Listing*>(opaque);
                        try {
                            list.paths.emplace_back(child);
                            return 1;
                        }
                        catch (...) {
                            list.failed = true;
                            return 0;
                        }
                    },
                    &list) ||
                list.failed)
                throw std::runtime_error("Cannot enumerate the source image folder");
            for (const auto& child : list.paths)
                add_image(reader, (fs::u8path(path) / fs::u8path(child)).u8string(), index, depth + 1);
        }
        nodes[index].end = nodes.size();
    }

    void add_live(const FinderImageEntry& entry, size_t parent, unsigned depth)
    {
        if (depth > 128 || nodes.size() >= 100000 || (entry.directory && entry.children > INT16_MAX))
            throw std::runtime_error("The source folder exceeds supported catalog limits");
        if (image_copy_private(reference, entry.id))
            throw std::runtime_error("Another operation is using this source folder");
        ImportNode node;
        node.live = {entry.parent, entry.id, entry.name};
        node.name = entry.name;
        node.parent = parent;
        node.info.directory = entry.directory;
        node.info.data_size = entry.data_size;
        node.info.resource_size = entry.resource_size;
        memcpy(node.info.finder_info, entry.finder_info, sizeof entry.finder_info);
        size_t index = nodes.size();
        nodes.push_back(std::move(node));
        if (entry.directory && !moving)
            for (int i = 1; i <= entry.children; ++i) {
                FinderImageEntry child{};
                mac_error(ROMlib_finder_image_entry(reference, entry.id, i, &child));
                add_live(child, index, depth + 1);
            }
        nodes[index].end = nodes.size();
    }

    int16_t abort() noexcept
    {
        host_lease.reset();
        input.close();
        if (source_file != -1) {
            ROMlib_finder_image_close_fork(source_file);
            source_file = -1;
        }
        if (file != -1) {
            ROMlib_finder_image_close_fork(file);
            file = -1;
        }
        if (lease != -1) {
            ROMlib_finder_image_close_fork(lease);
            lease = -1;
        }
        int16_t failure = 0;
        for (auto item = created.rbegin(); item != created.rend(); ++item) {
            uint32_t id = 0;
            int16_t error = ROMlib_finder_image_lookup(reference, item->parent, item->name.c_str(), &id);
            if (!error)
                error = id == item->id ? ROMlib_finder_image_delete(reference, item->parent, item->name.c_str()) : -48;
            if (error && !failure)
                failure = error;
        }
        created.clear();
        staging_roots.erase({reference, staging_id});
        staging_id = 0;
        if (!guard.empty()) {
            int16_t error = ROMlib_finder_image_delete(reference, directory, guard.c_str());
            if (error && !failure)
                failure = error;
            guard.clear();
        }
        staging_roots.erase({reference, guard_id});
        guard_id = 0;
        if (active)
            ROMlib_finder_image_flush(reference);
        active = false;
        return failure;
    }

    // Catalog IDs survive renames. Snapshot both fork lengths, metadata and all
    // descendants so a decision cannot authorize replacing a different tree.
    void snapshot(const FinderImageEntry& entry, std::vector<FinderImageEntry>& result, unsigned depth) const
    {
        catalog_tree(reference, entry, result, depth);
        if (std::any_of(result.begin(), result.end(), [&](const auto& item) { return source_ids.count(item.id); }))
            throw std::runtime_error("A copy cannot replace its source or a folder containing its source");
    }

    void begin_root(CopyJob& job)
    {
        mac_error(ROMlib_finder_image_writable(reference));
        auto& node = nodes[roots[root]];
        FinderImageEntry existing{};
        int16_t error = ROMlib_finder_image_info(reference, directory, node.name.c_str(), &existing);
        approved.clear();
        if (!error) {
            if (job.kind == TransferKind::stationery)
                throw std::runtime_error("An item already has the requested document name.");
            if (bool(existing.directory) != bool(node.info.directory))
                throw std::runtime_error("A file cannot replace a folder, or a folder replace a file");
            snapshot(existing, approved, 0);
        }
        else if (error != -43)
            mac_error(error);
        temporary = unused_image_name(reference, directory, "copy");
        auto guard_name = unused_image_name(reference, directory, "hold");
        unsigned char info[32]{};
        info[8] = 0x40;
        mac_error(ROMlib_finder_image_create(reference, directory, guard_name.c_str(), 0, info, &guard_id));
        guard.swap(guard_name);
        staging_roots.insert({reference, guard_id});
        active = true;
        mac_error(ROMlib_finder_image_open_fork(reference, directory, guard.c_str(), 0, &lease));
        current = roots[root];
        if (!approved.empty()) {
            std::lock_guard<std::mutex> lock(job.mutex);
            job.conflict_target = job.destinations[root].wstring();
            job.conflict_name = catalog_text(existing.name);
            job.conflict_directory = existing.directory;
            job.conflict_answer = ConflictAnswer::waiting;
            job.conflict_pending = awaiting_answer = true;
            job.state = CopyJob::waiting;
        }
    }

    void publish(ImportNode& top)
    {
        if (moving)
            mac_error(
                ROMlib_finder_image_check_mutable(reference, top.live.parent, top.live.name.c_str(), top.live.id));
        else
            mac_error(ROMlib_finder_image_set_info(reference, directory, temporary.c_str(), top.info.finder_info));
        std::string backup;
        if (!approved.empty()) {
            FinderImageEntry existing{};
            mac_error(ROMlib_finder_image_info(reference, directory, top.name.c_str(), &existing));
            std::vector<FinderImageEntry> fresh;
            snapshot(existing, fresh, 0);
            auto unchanged = [](const FinderImageEntry& a, const FinderImageEntry& b) {
                return a.id == b.id && a.parent == b.parent && a.directory == b.directory && a.modified == b.modified &&
                       a.created == b.created && a.data_size == b.data_size && a.resource_size == b.resource_size &&
                       !b.locked && !strcmp(a.name, b.name) &&
                       !memcmp(a.finder_info, b.finder_info, sizeof a.finder_info);
            };
            if (!std::equal(approved.begin(), approved.end(), fresh.begin(), fresh.end(), unchanged))
                throw std::runtime_error("The destination changed or is locked. Its contents were preserved.");
            for (const auto& item : fresh)
                mac_error(ROMlib_finder_image_check_mutable(reference, item.parent, item.name, item.id));
            backup = unused_image_name(reference, directory, "old");
            mac_error(ROMlib_finder_image_rename(reference, directory, existing.name, backup.c_str()));
        }
        int16_t error =
            moving ? ROMlib_finder_image_move(reference, top.live.parent, top.live.name.c_str(), top.live.id, directory)
                   : ROMlib_finder_image_rename(reference, directory, temporary.c_str(), top.name.c_str());
        if (error && !backup.empty() &&
            ROMlib_finder_image_rename(reference, directory, backup.c_str(), approved.front().name))
            throw std::runtime_error("Cannot restore the old destination. Its contents remain in " + backup);
        mac_error(error);
        created.clear(); // The new item now owns its public catalog name.
        staging_roots.erase({reference, staging_id});
        staging_id = 0;
        if (!backup.empty()) {
            // Parents follow children during removal. The old root alone changed name.
            for (auto it = approved.rbegin(); it != approved.rend(); ++it) {
                auto name = it->id == approved.front().id ? backup.c_str() : it->name;
                if (ROMlib_finder_image_delete(reference, it->parent, name))
                    throw std::runtime_error("Copied the item. Remaining old contents are in " + backup);
            }
        }
    }

    bool verify_host_move(CopyJob& job, std::chrono::steady_clock::time_point until)
    {
        auto& top = nodes[roots[root]];
        if (!host_lease) {
            host_lease = lock_host_move(host_stamps.at(roots[root]));
            for (size_t i = roots[root]; i < top.end; ++i) {
                auto& node = nodes[i];
                if (mac_companions(node.source) != node.companions)
                    throw std::runtime_error("The source metadata changed; original retained");
                for (const auto& entry : node.metadata_hashes) {
                    auto stamp = std::find_if(host_stamps.at(roots[root]).begin(), host_stamps.at(roots[root]).end(),
                                              [&](const auto& value) { return value.path == entry.first; });
                    if (stamp == host_stamps.at(roots[root]).end())
                        throw std::runtime_error("The source metadata changed; original retained");
                    uint64_t size = uint64_t(stamp->identity[3]) << 32 | stamp->identity[4];
                    uint64_t hash = 14695981039346656037ULL;
                    for (uint64_t at = 0; at < size;) {
                        if (job.stop)
                            return false;
                        auto count = size_t(std::min<uint64_t>(buffer.size(), size - at));
                        host_lease->read(entry.first, at, buffer.data(), count);
                        hash = metadata_hash(Bytes(buffer.begin(), buffer.begin() + count), hash);
                        at += count;
                    }
                    if (hash != entry.second)
                        throw std::runtime_error("The source metadata changed; original retained");
                }
                if (!node.binary_prefix.empty()) {
                    Bytes bytes(node.binary_prefix.size());
                    host_lease->read(node.source, 0, bytes.data(), bytes.size());
                    if (bytes != node.binary_prefix)
                        throw std::runtime_error("The MacBinary header changed; original retained");
                }
            }
            verify_node = roots[root];
            verify_fork = 0;
            verify_offset = 0;
        }
        std::array<unsigned char, 65536> original{};
        while (verify_node < top.end) {
            if (job.stop)
                return false;
            auto& node = nodes[verify_node];
            if (node.info.directory) {
                ++verify_node;
                continue;
            }
            auto size = verify_fork ? uint64_t(node.info.resource_size) : uint64_t(node.info.data_size);
            if (verify_offset == size) {
                if (source_file != -1) {
                    mac_error(ROMlib_finder_image_close_fork(source_file));
                    source_file = -1;
                }
                verify_offset = 0;
                if (++verify_fork == 2) {
                    ++verify_node;
                    verify_fork = 0;
                }
                continue;
            }
            if (source_file == -1)
                mac_error(ROMlib_finder_image_open_source_fork(reference, node.target_parent, node.target_name.c_str(),
                                                               verify_fork, &source_file));
            auto count = size_t(std::min<uint64_t>(buffer.size(), size - verify_offset));
            host_lease->read(verify_fork ? node.resource : node.source,
                             (verify_fork ? node.resource_offset : node.data_offset) + verify_offset, original.data(),
                             count);
            mac_error(ROMlib_finder_image_read(source_file, buffer.data(), count));
            if (!std::equal(original.begin(), original.begin() + count, buffer.begin()))
                throw std::runtime_error("The staged image fork differs from its source; original retained");
            verify_offset += count;
            if (std::chrono::steady_clock::now() >= until)
                return false;
        }
        host_lease->verify();
        return true;
    }

    void cancel(CopyJob& job)
    {
        auto error = abort();
        if (error) {
            job.state = CopyJob::failed;
            throw std::runtime_error("Stopped, but an unfinished image item could not be removed (error " +
                                     std::to_string(error) + ")");
        }
        std::lock_guard<std::mutex> lock(job.mutex);
        job.conflict_pending = false;
        job.state = CopyJob::cancelled;
    }

    void advance(CopyJob& job)
    {
        if (job.stop) {
            cancel(job);
            return;
        }
        job.total = total;
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
        do {
            if (root == roots.size()) {
                job.state = CopyJob::complete;
                return;
            }
            if (!active)
                begin_root(job);
            auto& top = nodes[roots[root]];
            if (awaiting_answer) {
                ConflictAnswer answer;
                {
                    std::lock_guard<std::mutex> lock(job.mutex);
                    answer = job.conflict_answer;
                }
                if (answer == ConflictAnswer::waiting)
                    return;
                awaiting_answer = false;
                if (answer == ConflictAnswer::stop) {
                    job.stop = true;
                    cancel(job);
                    return;
                }
                if (answer == ConflictAnswer::skip) {
                    if (!moving)
                        for (size_t i = current; i < top.end; ++i)
                            total -= nodes[i].info.data_size + uint64_t(nodes[i].info.resource_size);
                    job.total = total;
                    job.items -= top.end - current;
                    mac_error(abort());
                    ++root;
                    continue;
                }
            }
            job.state = CopyJob::copying;
            if (moving || current == top.end) {
                if (host_moving && !verify_host_move(job, until))
                    return;
                publish(top);
                mac_error(ROMlib_finder_image_flush(reference));
                if (!top.source.empty())
                    job.published_sources.push_back(top.source);
                if (host_moving) {
                    host_lease->remove();
                    host_lease.reset();
                    job.archived_sources.push_back(top.source);
                }
                if (moving)
                    --job.items;
                mac_error(ROMlib_finder_image_close_fork(lease));
                lease = -1;
                mac_error(ROMlib_finder_image_delete(reference, directory, guard.c_str()));
                guard.clear();
                staging_roots.erase({reference, guard_id});
                guard_id = 0;
                mac_error(ROMlib_finder_image_flush(reference));
                active = false;
                ++root;
                continue;
            }
            auto& node = nodes[current];
            if (node.target_name.empty()) {
                node.target_parent = node.parent == SIZE_MAX ? directory : nodes[node.parent].target_id;
                node.target_name = node.parent == SIZE_MAX ? temporary : node.name;
                auto info = node.info;
                if (node.parent == SIZE_MAX)
                    info.finder_info[8] |= 0x40;
                Created pending{node.target_parent, 0, node.target_name};
                mac_error(ROMlib_finder_image_create(reference, node.target_parent, node.target_name.c_str(),
                                                     info.directory, info.finder_info, &node.target_id));
                pending.id = node.target_id;
                created.push_back(std::move(pending));
                if (node.parent == SIZE_MAX) {
                    staging_id = node.target_id;
                    staging_roots.insert({reference, staging_id});
                }
                {
                    std::lock_guard<std::mutex> lock(job.mutex);
                    int length = MultiByteToWideChar(10000, 0, node.name.data(), int(node.name.size()), nullptr, 0);
                    job.current.resize(length);
                    MultiByteToWideChar(10000, 0, node.name.data(), int(node.name.size()), job.current.data(), length);
                }
                if (info.directory) {
                    ++current;
                    --job.items;
                    continue;
                }
                fork = 0;
                offset = 0;
            }
            uint64_t size = fork ? node.info.resource_size : node.info.data_size;
            if (offset == size) {
                if (source_file != -1) {
                    auto error = ROMlib_finder_image_close_fork(source_file);
                    source_file = -1;
                    mac_error(error);
                }
                if (file != -1) {
                    mac_error(ROMlib_finder_image_close_fork(file));
                    file = -1;
                }
                input.close();
                input.clear();
                offset = 0;
                if (++fork < 2)
                    continue;
                ++current;
                --job.items;
                continue;
            }
            if (file == -1) {
                mac_error(ROMlib_finder_image_open_fork(reference, node.target_parent, node.target_name.c_str(), fork,
                                                        &file));
                if (node.live.id) {
                    auto source = catalog_item(reference, node.live);
                    if (source.data_size != node.info.data_size || source.resource_size != node.info.resource_size)
                        throw std::runtime_error("The source fork changed while copying");
                    mac_error(ROMlib_finder_image_open_source_fork(reference, source.parent, source.name, fork,
                                                                   &source_file));
                }
                else if (!node.reader) {
                    input.open(fork ? node.resource : node.source, std::ios::binary);
                    input.seekg(fork ? node.resource_offset : node.data_offset);
                    if (!input)
                        throw std::runtime_error("Cannot open the source fork");
                }
            }
            size_t count = size_t(std::min<uint64_t>(buffer.size(), size - offset));
            if (source_file != -1)
                mac_error(ROMlib_finder_image_read(source_file, buffer.data(), count));
            else if (node.reader) {
                char error[1024]{};
                if (!romaw_images_read_fork_utf8(node.reader.get(), node.virtual_path.c_str(), fork, offset,
                                                 buffer.data(), count, error, sizeof error))
                    throw std::runtime_error(error);
            }
            else if (!input.read(reinterpret_cast<char*>(buffer.data()), count))
                throw std::runtime_error("The source fork changed or became unavailable");
            mac_error(ROMlib_finder_image_write(file, buffer.data(), count));
            offset += count;
            job.done += count;
        } while (std::chrono::steady_clock::now() < until);
    }

    void step(CopyJob& job)
    {
        try {
            advance(job);
        }
        catch (const std::exception& e) {
            auto error = abort();
            if (error)
                throw std::runtime_error(std::string(e.what()) + "; unfinished destination retained (cleanup error " +
                                         std::to_string(error) + ")");
            throw;
        }
    }
};
}

bool image_copy_private(int16_t reference, uint32_t id)
{
    return staging_roots.count({reference, id}) != 0;
}

std::string image_catalog_name(const std::wstring& name)
{
    return catalog_name(name);
}

uint32_t create_image_folder(int16_t reference, uint32_t parent)
{
    unsigned char info[32]{};
    uint32_t id = 0;
    for (unsigned n = 1; n <= UINT16_MAX; ++n) {
        std::string name = "Untitled Folder";
        if (n > 1)
            name += " " + std::to_string(n);
        auto error = ROMlib_finder_image_create(reference, parent, name.c_str(), 1, info, &id);
        if (error == -48)
            continue;
        mac_error(error);
        mac_error(ROMlib_finder_image_flush(reference));
        return id;
    }
    throw std::runtime_error("Cannot find an unused folder name");
}

std::string rename_image_item(int16_t reference, const ImageCatalogItem& item, const std::wstring& name)
{
    mac_error(ROMlib_finder_image_writable(reference));
    auto entry = catalog_item(reference, item);
    auto target = catalog_name(name);
    if (target != entry.name) {
        if (entry.locked)
            mac_error(-45);
        uint32_t found = 0;
        auto error = ROMlib_finder_image_lookup(reference, item.parent, target.c_str(), &found);
        if (error == -43)
            mac_error(ROMlib_finder_image_rename(reference, item.parent, entry.name, target.c_str()));
        else {
            mac_error(error);
            if (found != item.id)
                mac_error(-48);
            // HFS looks up names case-insensitively, including rename targets.
            auto temporary = unused_image_name(reference, item.parent, "name");
            mac_error(ROMlib_finder_image_rename(reference, item.parent, entry.name, temporary.c_str()));
            error = ROMlib_finder_image_rename(reference, item.parent, temporary.c_str(), target.c_str());
            if (error && ROMlib_finder_image_rename(reference, item.parent, temporary.c_str(), entry.name))
                throw std::runtime_error("Cannot restore the old name. The item is named " + temporary);
            mac_error(error);
        }
        mac_error(ROMlib_finder_image_flush(reference));
    }
    return target;
}

void label_image_item(int16_t reference, const ImageCatalogItem& item, unsigned label)
{
    if (label > 7)
        throw std::runtime_error("Invalid Finder label");
    mac_error(ROMlib_finder_image_writable(reference));
    auto entry = catalog_item(reference, item);
    entry.finder_info[9] = (entry.finder_info[9] & ~0x0e) | (label << 1);
    mac_error(ROMlib_finder_image_set_info(reference, item.parent, entry.name, entry.finder_info));
    mac_error(ROMlib_finder_image_flush(reference));
}

void stationery_image_item(int16_t reference, const ImageCatalogItem& item, bool stationery)
{
    mac_error(ROMlib_finder_image_writable(reference));
    auto entry = catalog_item(reference, item);
    if (entry.directory || entry.locked)
        throw std::runtime_error("Unlock the document before changing its Stationery Pad setting.");
    entry.finder_info[8] = (entry.finder_info[8] & ~(stationery_flag >> 8)) | (stationery ? stationery_flag >> 8 : 0);
    mac_error(ROMlib_finder_image_set_info(reference, item.parent, entry.name, entry.finder_info));
    mac_error(ROMlib_finder_image_flush(reference));
}

void delete_image_items(int16_t reference, const std::vector<ImageCatalogItem>& items)
{
    mac_error(ROMlib_finder_image_writable(reference));
    std::vector<FinderImageEntry> tree;
    for (const auto& item : items)
        catalog_tree(reference, catalog_item(reference, item), tree, 0);
    for (const auto& entry : tree)
        mac_error(ROMlib_finder_image_check_mutable(reference, entry.parent, entry.name, entry.id));
    std::set<uint32_t> removed;
    for (auto it = tree.rbegin(); it != tree.rend(); ++it)
        if (removed.insert(it->id).second)
            mac_error(ROMlib_finder_image_delete(reference, it->parent, it->name));
    mac_error(ROMlib_finder_image_flush(reference));
}

std::shared_ptr<CopyJob> copy_into_image(Copies& copies, const std::vector<fs::path>& host_sources,
                                         const std::vector<ImageCopySource>& image_sources, int16_t reference,
                                         uint32_t directory, const fs::path& destination_key, TransferKind kind,
                                         const std::string& stationery_name)
{
    mac_error(ROMlib_finder_image_writable(reference));
    bool moving = kind == TransferKind::move;
    if (moving && !host_sources.empty() && !image_sources.empty())
        throw std::runtime_error("Move host and image items in separate operations");
    auto transfer = std::make_shared<ImageImport>(reference, directory, moving && host_sources.empty());
    transfer->host_moving = moving && !host_sources.empty();
    std::vector<fs::path> sources, destinations;
    for (const auto& path : host_sources) {
        if (transfer->host_moving) {
            if (!windows_destination(path))
                throw std::runtime_error("Host-to-image moves currently require a Windows source drive");
            transfer->host_stamps.emplace(transfer->nodes.size(), host_tree_stamps(path));
        }
        transfer->roots.push_back(transfer->nodes.size());
        transfer->add_host(path, SIZE_MAX, 0);
        if (transfer->host_moving) {
            auto root = transfer->roots.back();
            std::set<fs::path> represented;
            for (size_t i = root; i < transfer->nodes[root].end; ++i) {
                const auto& node = transfer->nodes[i];
                represented.insert(node.source);
                represented.insert(node.companions.begin(), node.companions.end());
                if (node.info.directory && node.info.resource_size)
                    throw std::runtime_error("The image cannot represent a source folder resource fork");
            }
            for (const auto& stamp : transfer->host_stamps.at(root))
                if (!stamp.directory && !represented.count(stamp.path))
                    throw std::runtime_error(
                        "Some source entries cannot be represented in the image; original retained");
        }
        sources.push_back(path);
    }
    std::map<fs::path, Reader> readers;
    for (const auto& source : image_sources) {
        if (source.reference == reference && source.catalog.id) {
            auto entry = catalog_item(reference, source.catalog);
            if (moving && entry.parent == directory)
                continue;
            if (entry.directory)
                for (uint32_t parent = directory, depth = 0; parent != 2; ++depth) {
                    if (parent == entry.id)
                        throw std::runtime_error("A folder cannot be copied into itself");
                    FinderImageEntry enclosing{};
                    if (depth >= 128 || ROMlib_finder_image_entry(reference, parent, -1, &enclosing))
                        throw std::runtime_error("The destination image folder is unavailable");
                    parent = enclosing.parent;
                }
            transfer->source_ids.insert(entry.id);
            transfer->roots.push_back(transfer->nodes.size());
            transfer->add_live(entry, SIZE_MAX, 0);
            sources.push_back(source.source);
            continue;
        }
        if (moving)
            throw std::runtime_error("Only moves within the same Macintosh image are supported");
        transfer->roots.push_back(transfer->nodes.size());
        char error[1024]{}, path[32768]{};
        auto& reader = readers[source.source];
        if (!reader) {
            reader = Reader(romaw_images_open_direct_utf8(source.source.u8string().c_str(), error, sizeof error),
                            [](romaw_image_set* set) { romaw_images_close(set, 0); });
            if (!reader)
                throw std::runtime_error(error);
        }
        if (!romaw_images_guest_path_utf8(reader.get(), source.partition, source.guest.c_str(), path, sizeof path,
                                          error, sizeof error))
            throw std::runtime_error(error);
        transfer->add_image(reader, path, SIZE_MAX, 0);
        sources.push_back(source.source);
    }
    if (sources.empty())
        return {};
    if (kind == TransferKind::stationery) {
        if (sources.size() != 1 || transfer->nodes.front().info.directory || stationery_name.empty())
            throw std::runtime_error("Choose one stationery document and a name.");
        transfer->nodes.front().name = stationery_name;
        transfer->nodes.front().info.finder_info[8] &= ~(stationery_flag >> 8);
    }
    transfer->created.reserve(transfer->nodes.size());
    if (kind == TransferKind::duplicate) {
        std::vector<fs::path> reserved;
        for (const auto& job : copies.jobs())
            if (job->state <= CopyJob::copying)
                for (const auto& target : job->destinations)
                    if (target.parent_path() == destination_key)
                        reserved.push_back(target);
        for (size_t root : transfer->roots) {
            auto& node = transfer->nodes[root];
            for (unsigned n = 1;; ++n) {
                auto suffix = n == 1 ? std::string(" copy") : " copy " + std::to_string(n);
                auto name = node.name.substr(0, 31 - suffix.size()) + suffix;
                auto error = ROMlib_finder_image_lookup(reference, directory, name.c_str(), nullptr);
                if (error && error != -43)
                    mac_error(error);
                auto target = image_job_target(destination_key, name);
                if (error == -43 && std::none_of(reserved.begin(), reserved.end(), [&](const auto& other) {
                        return !_wcsicmp(target.c_str(), other.c_str());
                    })) {
                    reserved.push_back(std::move(target));
                    node.name = std::move(name);
                    break;
                }
                if (n == UINT16_MAX)
                    throw std::runtime_error("Cannot find an unused name for the duplicate");
            }
        }
    }
    for (size_t root : transfer->roots) {
        destinations.push_back(image_job_target(destination_key, transfer->nodes[root].name));
    }
    if (!transfer->moving)
        for (const auto& node : transfer->nodes) {
            if (node.info.data_size > INT32_MAX || node.info.resource_size > INT32_MAX ||
                node.info.data_size > UINT64_MAX - transfer->total ||
                node.info.resource_size > UINT64_MAX - transfer->total - node.info.data_size)
                throw std::runtime_error("The source fork exceeds this image's supported size");
            transfer->total += node.info.data_size + node.info.resource_size;
        }
    auto job = copies.start_cooperative(sources, destinations, [transfer](CopyJob& job) { transfer->step(job); }, kind);
    job->total = transfer->total;
    job->items = transfer->nodes.size();
    job->to = L"Mounted Macintosh image";
    return job;
}

ImageFolderSize::ImageFolderSize(int16_t reference, uint32_t directory) : reference(reference), root(directory)
{
}

bool ImageFolderSize::advance()
{
    if (started && pending.empty())
        return true;
    FinderImageEntry entry{};
    if (ROMlib_finder_image_entry(reference, root, -1, &entry) || !entry.directory || entry.id != root)
        throw std::runtime_error("The image folder is no longer available.");
    if (!started) {
        pending.push_back({root, entry.children});
        started = true;
    }
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(6);
    for (unsigned steps = 0; steps < 64 && !pending.empty() && std::chrono::steady_clock::now() < until; ++steps) {
        auto& frame = pending.back();
        if (frame.next > frame.children) {
            pending.pop_back();
            continue;
        }
        if (frame.next > INT16_MAX ||
            ROMlib_finder_image_entry(reference, frame.directory, int16_t(frame.next++), &entry) ||
            entry.parent != frame.directory)
            throw std::runtime_error("The image catalog changed or could not be enumerated.");
        if (image_copy_private(reference, entry.id))
            continue;
        if (entry.directory)
            pending.push_back({entry.id, entry.children});
        else
            total.add(image_file_size(entry));
    }
    return pending.empty();
}

std::wstring image_search_comment(int16_t reference, uint32_t id)
{
    unsigned char key[38] = {5,
                             3,
                             static_cast<unsigned char>(id >> 24),
                             static_cast<unsigned char>(id >> 16),
                             static_cast<unsigned char>(id >> 8),
                             static_cast<unsigned char>(id)};
    unsigned char comment[200]{};
    size_t count = sizeof comment;
    uint32_t tag = 0;
    if (ROMlib_finder_image_desktop(reference, desktop_get, key, 0, comment, &count, &tag) || !count ||
        size_t(comment[0]) + 1 > std::min(count, sizeof comment))
        return {};
    return catalog_text(std::string(reinterpret_cast<const char*>(comment + 1), comment[0]));
}

namespace {
bool image_matches(const FindQuery& query, int16_t volume, const FinderImageEntry& entry, const std::wstring& name)
{
    switch (query.field) {
    case FindField::name:
        return find_text_matches(query, name);
    case FindField::size:
        return !entry.directory && find_number_matches(query, uint64_t(entry.data_size) + entry.resource_size);
    case FindField::kind:
        return find_number_matches(
            query, unsigned(find_kind(mac_catalog_info(entry.finder_info, entry.directory), entry.directory)));
    case FindField::label:
        return find_number_matches(query, (entry.flags >> 1) & 7);
    case FindField::created:
    case FindField::modified: {
        uint32_t seconds = query.field == FindField::created ? entry.created : entry.modified;
        uint64_t ticks = seconds ? (uint64_t(seconds) + 9561628800ULL) * 10000000 : 0;
        return find_number_matches(query, find_date({DWORD(ticks), DWORD(ticks >> 32)}));
    }
    case FindField::version: {
        unsigned char data[512]{};
        size_t count = 0;
        if (entry.directory ||
            !ROMlib_finder_image_version(volume, entry.parent, entry.name, data, sizeof data, &count))
            return false;
        auto version = find_version(data, count);
        return !version.empty() && find_text_matches(query, version);
    }
    case FindField::comments:
        return find_text_matches(query, image_search_comment(volume, entry.id));
    case FindField::lock:
        return !entry.directory && find_number_matches(query, entry.locked != 0);
    }
    return false;
}
}

void Search::advance_images()
{
    if (stop || limited || (image_roots.empty() && catalog_stack.empty()))
        return;
    auto consider = [&](fs::path path, int16_t volume, const FinderImageEntry& entry) {
        if (image_copy_private(volume, entry.id))
            return;
        auto name = catalog_text(entry.name);
        EntryVisibility visible{entry.type,  entry.creator,        0,
                                entry.flags, entry.directory != 0, hidden_metadata(fs::path(name))};
        if (!folder_item_visible(visible, include_hidden))
            return;
        if (image_matches(options.query, volume, entry, name))
            publish({path, volume, entry});
        if (entry.directory && catalog_stack.size() < 128)
            catalog_stack.push_back({std::move(path), volume, entry.id});
    };
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
    for (int count = 0; count < 64 && !limited && std::chrono::steady_clock::now() < until; ++count) {
        if (catalog_stack.empty()) {
            if (image_roots.empty())
                break;
            auto root = std::move(image_roots.back());
            image_roots.pop_back();
            FinderImageEntry entry{};
            if (!root.include_item)
                catalog_stack.push_back({std::move(root.path), root.image, root.id});
            else if (ROMlib_finder_image_info(root.image, root.parent, root.name.c_str(), &entry) ||
                     entry.id != root.id)
                report_error(L"A selected image item is no longer available.");
            else
                consider(std::move(root.path), root.image, entry);
            continue;
        }
        auto& frame = catalog_stack.back();
        if (frame.next > INT16_MAX) {
            report_error(L"An image folder has more items than the catalog can enumerate.");
            catalog_stack.pop_back();
            continue;
        }
        FinderImageEntry entry{};
        int16_t error = ROMlib_finder_image_entry(frame.image, frame.id, int16_t(frame.next++), &entry);
        if (error) {
            if (error != -43)
                report_error(L"An image became unavailable or could not be searched.");
            catalog_stack.pop_back();
            continue;
        }
        consider(frame.path / std::to_wstring(entry.id), frame.image, entry);
    }
}

}
