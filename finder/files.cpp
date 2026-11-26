/*+===================================================================
  File:      files.cpp

  Summary:   Host file and Macintosh metadata services. AppleDouble/resource fields are big-endian bytes, while all
             public paths and decoded values are native Windows data. Each copy worker stages complete logical items
             before publication and owns only its temporary output.

  Classes:   Declared: Entry, Interrupted, Part, Progress, ResourceMap, RootCopy; Used: CopyJob, Entry, ROMAWMacStorageEntry,
             Interrupted, MacFile, Part, Progress, RootCopy, std::array, std::atomic, std::error_code, std::exception,
             std::ifstream, std::lock_guard, std::mutex, std::pair, std::runtime_error, std::shared_ptr, std::string,
             std::thread, std::vector, std::wstring

  Functions: finder::Copies::start_cooperative, finder::Copies::tick, finder::Copies::start_targets,
             finder::Copies::active, finder::Copies::cancel_all, finder::Copies::start, finder::Copies::~Copies,
             finder::CopyJob::~CopyJob, finder::error_text, finder::format_bytes, finder::hidden_metadata,
             finder::host_file_size, finder::host_folder_size, finder::FileSize::add, finder::format_file_size,
             finder::mac_catalog_info, finder::mac_companion_target, finder::mac_companions, finder::mac_display_name, finder::mac_file,
             finder::mac_resource, finder::mark_desktop_database_files, finder::quote_argument, finder::read_bytes,
             finder::relocate_mac_companions, finder::rename_item, finder::encode_comment, finder::set_comment,
             finder::set_label, finder::set_stationery, finder::write_icon_metadata,
             finder::set_drive_format_lookup, finder::set_folder_archive_writer, finder::set_file_archive_writer,
             finder::windows_destination, finder::set_host_import_services, finder::read_macbinary,
             finder::host_import_name, finder::host_file_stamp, finder::lock_host_move,
             finder::sidecar, finder::special_folder_kind, finder::is_mac_suitcase, finder::suitcase_members

  Origin:    Existing repository source in executor/src/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

// Host file and Macintosh metadata services. AppleDouble/resource fields are
// big-endian bytes, while all public paths and decoded values are native
// Windows data. Each copy worker stages complete logical items before
// publication and owns only its temporary output.

#include "finder/files.h"
#include "rsys/mac_storage.h"
#include "rsys/configprefs.h"
#include <algorithm>
#include <array>
#include <set>
#include <map>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <exception>
#include <shlobj.h>
#include <shellapi.h>

namespace finder {
static std::atomic<int (*)(const char*)> drive_format_lookup{nullptr};
static std::atomic<FolderArchiveWriter> folder_archive_writer{nullptr};
static std::atomic<FileArchiveWriter> file_archive_writer{nullptr};
static std::atomic<MacBinaryReader> binary_reader{nullptr};
static std::atomic<HostTypeLookup> host_type_lookup{nullptr};

void set_host_import_services(MacBinaryReader reader, HostTypeLookup lookup)
{
    binary_reader = reader;
    host_type_lookup = lookup;
}

void set_file_archive_writer(FileArchiveWriter writer)
{
    file_archive_writer = writer;
}

void set_folder_archive_writer(FolderArchiveWriter writer)
{
    folder_archive_writer = writer;
}

void set_drive_format_lookup(int (*lookup)(const char*))
{
    drive_format_lookup = lookup;
}
#ifdef FINDER_TESTING
void copy_test_progress(CopyJob&);
bool copy_test_staged_moves();
void copy_test_before_publish(CopyJob&);
#endif
namespace {
// Read a big-endian longword only after checking its complete byte extent.
uint32_t be32(const Bytes& b, size_t p)
{
    if (p > b.size() || b.size() - p < 4)
        throw std::runtime_error("Truncated Macintosh metadata");
    return uint32_t(b[p]) << 24 | uint32_t(b[p + 1]) << 16 | uint32_t(b[p + 2]) << 8 | b[p + 3];
}

// Read a big-endian word without trusting offsets from Macintosh metadata.
unsigned be16(const Bytes& b, size_t p)
{
    if (p > b.size() || b.size() - p < 2)
        throw std::runtime_error("Truncated Macintosh metadata");
    return unsigned(b[p]) << 8 | b[p + 1];
}

// Validate a byte range with subtraction so malformed lengths cannot wrap.
void extent(const Bytes& b, size_t p, size_t n)
{
    if (p > b.size() || n > b.size() - p)
        throw std::runtime_error("Invalid Macintosh metadata extent");
}

// Copy a validated metadata extent into independently owned storage.
Bytes slice(const Bytes& b, size_t p, size_t n)
{
    extent(b, p, n);
    return Bytes(b.begin() + p, b.begin() + p + n);
}

struct Entry {
    uint32_t type;
    size_t offset, length;
};

// Validate the shared Macintosh container directory before exposing payloads.
std::vector<Entry> entries(const Bytes& b)
{
    unsigned count;
    if (!romaw_mac_storage_table(b.data(), b.size(), &count))
        throw std::runtime_error("Invalid AppleDouble metadata");
    std::vector<Entry> out;
    for (unsigned i = 0; i < count; ++i) {
        ROMAWMacStorageEntry entry;
        if (!romaw_mac_storage_entry(b.data(), b.size(), b.size(), i, &entry))
            throw std::runtime_error("Invalid AppleDouble entry extent");
        out.push_back({entry.id, entry.offset, entry.length});
    }
    return out;
}

struct ResourceMap {
    size_t offset, length, types;
    unsigned count;
};

ResourceMap resource_map(const Bytes& b)
{
    if (b.size() < 16)
        throw std::runtime_error("Truncated resource fork");
    size_t map = be32(b, 4), length = be32(b, 12);
    extent(b, be32(b, 0), be32(b, 8));
    extent(b, map, length);
    if (length < 28)
        throw std::runtime_error("Invalid resource map");
    size_t types = be16(b, map + 24);
    if (types > length || length - types < 2)
        throw std::runtime_error("Invalid resource type list");
    unsigned count = be16(b, map + types);
    count = count == 0xffff ? 0 : count + 1;
    if (count > (length - types - 2) / 8)
        throw std::runtime_error("Invalid resource type count");
    return {map, length, types, count};
}

// References are relative to the type list, regardless of the map offset.
size_t resource_references(const Bytes& b, const ResourceMap& map, size_t type, unsigned count)
{
    size_t refs = map.types + be16(b, type + 6);
    if (refs > map.length || count > (map.length - refs) / 12)
        throw std::runtime_error("Invalid resource references");
    return map.offset + refs;
}

// Classify CODE/cfrg, DRVR and cdev files for the launcher.
void check_resources(MacFile& f)
{
    const auto& b = f.fork;
    if (b.empty())
        return;
    const auto map = resource_map(b);
    if (!map.count)
        return;
    bool code = false, driver = false, panel = false;
    for (unsigned i = 0; i < map.count; ++i) {
        size_t p = map.offset + map.types + 2 + i * 8;
        uint32_t type = be32(b, p);
        unsigned n = be16(b, p + 4) + 1u;
        size_t refs = resource_references(b, map, p, n);
        code |= type == 0x434f4445 || type == 0x63667267; // CODE / cfrg
        panel |= type == 0x63646576;                      // cdev
        if (type == 0x44525652 && n) {
            int id = int16_t(be16(b, refs));
            if (!driver || id < 0)
                f.driver = id;
            driver = true;
        }
    }
    if (f.type == 0x63646576 || (panel && !code))
        f.kind = FINDER_CONTROL_PANEL;
    else if (driver && (f.type == 0x4446494c || f.type == 0x6466696c || !code))
        f.kind = FINDER_DESK_ACCESSORY;
    else if (code || f.type == 0x4150504c || f.type == 0x464e4452)
        f.kind = FINDER_CLASSIC_APPLICATION;
}

// Resolve existing path components before containment and destination checks.
fs::path normalized(const fs::path& p)
{
    return fs::weakly_canonical(fs::absolute(p));
}

// Compare Windows path spelling without case; callers normalize paths when
// filesystem identity matters.
bool same(const fs::path& a, const fs::path& b)
{
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}

// Check containment by path components, avoiding false matches such as Games
// and Games2.
bool beneath(const fs::path& parent, const fs::path& child)
{
    auto p = parent.begin(), c = child.begin();
    for (; p != parent.end(); ++p, ++c)
        if (c == child.end() || !same(*p, *c))
            return false;
    return true;
}

// Preserve a captured Win32 error code in the exception sent back to the UI.
void win_error(DWORD e)
{
    throw std::system_error(e, std::system_category());
}

// Publish a staged payload without replacing an existing destination.
void commit(const fs::path& from, const fs::path& to)
{
    for (unsigned attempt = 0;; ++attempt) {
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH))
            return;
        auto error = GetLastError();
        // Directory enumerators and scanners can briefly deny rename/delete
        // sharing. Retry the same non-replacing publication, never change ACLs
        // or delete an occupied target to make a retry succeed.
        if (attempt == 20 ||
            (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION))
            throw std::system_error(error, std::system_category(), "Cannot publish " + to.u8string());
        Sleep(10);
    }
}

struct Interrupted {};

bool unique_transfer_names(TransferKind kind)
{
    return kind == TransferKind::duplicate || kind == TransferKind::trash || kind == TransferKind::trash_copy ||
           kind == TransferKind::stationery;
}

struct Part {
    fs::path source, target;
    uint64_t size;
    bool item;
    std::array<DWORD, 7> identity;
};

enum class StorageFormat { windows, osx, netatalk, basilisk, percent };

struct RootCopy {
    fs::path source, target, temporary, payload;
    bool directory;
    bool archive = false;
    bool binary = false;
    bool importing = false;
    bool rename = false;
    bool convert = false;
    StorageFormat format = StorageFormat::osx;
    std::vector<Part> parts;
    std::vector<fs::path> directories;
    std::vector<std::pair<fs::path, fs::path>> companions{};
};

std::atomic<unsigned long> temporary_ids{0};

// Allocate process-local staging names shared by copy workers and metadata
// writers.
fs::path temp_name(const fs::path& directory)
{
    return directory /
           (L".Finder-copy-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++temporary_ids));
}

// Reject reparse points before traversal so a logical copy cannot escape
// through a junction.
void reject_link(const fs::path& p)
{
    DWORD attributes = GetFileAttributesW(p.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        win_error(GetLastError());
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
        throw std::runtime_error("Copying junctions and symbolic links is not supported");
}

std::array<DWORD, 7> file_identity(HANDLE handle)
{
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info))
        win_error(GetLastError());
    return {info.dwVolumeSerialNumber,
            info.nFileIndexHigh,
            info.nFileIndexLow,
            info.nFileSizeHigh,
            info.nFileSizeLow,
            info.ftLastWriteTime.dwHighDateTime,
            info.ftLastWriteTime.dwLowDateTime};
}

// Record one physical stream, including its bytes in progress while counting
// only visible file payloads as items.
void add_part(RootCopy& root, const fs::path& source, const fs::path& target, CopyJob& job, bool item = true)
{
    reject_link(source);
    uint64_t size = fs::file_size(source);
    if (size > UINT64_MAX - job.total.load())
        throw std::runtime_error("Copy size overflow");
    std::array<DWORD, 7> identity{};
    if (moving_transfer(job.kind)) {
        HANDLE handle =
            CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                        OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            win_error(GetLastError());
        try {
            identity = file_identity(handle);
        }
        catch (...) {
            CloseHandle(handle);
            throw;
        }
        CloseHandle(handle);
    }
    root.parts.push_back({source, target, size, item, identity});
    job.total += size;
    if (item)
        ++job.items;
}

struct Progress {
    CopyJob* job;
    uint64_t base;
};

// CopyFileEx calls this on the worker thread; publish byte progress and poll
// this job's independent Stop flag.
DWORD CALLBACK progress(LARGE_INTEGER, LARGE_INTEGER transferred, LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD, HANDLE,
                        HANDLE, LPVOID data)
{
    auto& p = *static_cast<Progress*>(data);
    p.job->done = p.base + uint64_t(transferred.QuadPart);
#ifdef FINDER_TESTING
    if (transferred.QuadPart > 0)
        copy_test_progress(*p.job);
#endif
    return p.job->stop ? PROGRESS_CANCEL : PROGRESS_CONTINUE;
}

// Own locked source objects until a cross-volume destination is published.
// DELETE access lets removal use the same verified object rather than its name.
struct MoveSource {
    HANDLE handle = INVALID_HANDLE_VALUE;

    MoveSource(const fs::path& path, bool directory)
    {
        handle = CreateFileW(path.c_str(), GENERIC_READ | DELETE,
                             directory ? FILE_SHARE_READ | FILE_SHARE_WRITE : FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             directory ? FILE_FLAG_BACKUP_SEMANTICS : 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            win_error(GetLastError());
    }

    MoveSource(MoveSource&& other) noexcept : handle(other.handle)
    {
        other.handle = INVALID_HANDLE_VALUE;
    }

    MoveSource(const MoveSource&) = delete;

    ~MoveSource()
    {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }

    void remove()
    {
        FILE_DISPOSITION_INFO disposition{TRUE};
        if (!SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof disposition))
            win_error(GetLastError());
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
};

// Re-read under a no-write/no-delete lock. A changed source is retained, never
// deleted merely because an earlier copy happened to finish successfully.
void verify_move_source(MoveSource& source, const Part& part, CopyJob& job)
{
    if (file_identity(source.handle) != part.identity)
        throw std::runtime_error("Source changed during move; original retained");
    std::ifstream destination(part.target, std::ios::binary);
    if (!destination)
        throw std::runtime_error("Cannot verify the moved file");
    std::array<char, 65536> original{}, copied{};
    uint64_t count = 0;
    for (;;) {
        if (job.stop)
            throw Interrupted();
        DWORD received;
        if (!ReadFile(source.handle, original.data(), DWORD(original.size()), &received, nullptr))
            win_error(GetLastError());
        destination.read(copied.data(), received);
        if (destination.gcount() != received ||
            !std::equal(original.begin(), original.begin() + received, copied.begin()))
            throw std::runtime_error("Source changed during move; original retained");
        count += received;
        if (!received)
            break;
    }
    if (count != part.size || destination.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("Source changed during move; original retained");
}

// A decision belongs to one job. Only its worker waits; the UI and every other
// transfer continue running. Stop/shutdown are observed even without an answer.
ConflictAnswer ask_conflict(CopyJob& job, const fs::path& source, const fs::path& target, bool directory)
{
    std::unique_lock<std::mutex> lock(job.mutex);
    job.conflict_source = source.wstring();
    job.conflict_target = target.wstring();
    job.conflict_name = target.filename().wstring();
    job.conflict_directory = directory;
    job.conflict_answer = ConflictAnswer::waiting;
    job.conflict_pending = true;
    job.state = CopyJob::waiting;
    while (job.conflict_answer == ConflictAnswer::waiting && !job.stop)
        job.decision.wait_for(lock, std::chrono::milliseconds(100));
    job.conflict_pending = false;
    auto answer = job.conflict_answer;
    job.state = CopyJob::copying;
    if (job.stop || answer == ConflictAnswer::stop)
        throw Interrupted();
    return answer;
}

using Fingerprint = std::map<fs::path, std::array<DWORD, 7>>;

// Relative keys allow verification before and after renaming the same objects.
Fingerprint fingerprint(const std::vector<fs::path>& objects)
{
    Fingerprint result;
    for (size_t i = 0; i < objects.size(); ++i) {
        const auto& root = objects[i];
        auto add = [&](const fs::path& path) {
            reject_link(path);
            HANDLE handle =
                CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                win_error(GetLastError());
            std::array<DWORD, 7> identity;
            try {
                identity = file_identity(handle);
            }
            catch (...) {
                CloseHandle(handle);
                throw;
            }
            CloseHandle(handle);
            result[fs::path(std::to_wstring(i)) / path.lexically_relative(root)] = identity;
        };
        add(root);
        if (fs::is_directory(root))
            for (const auto& child : fs::recursive_directory_iterator(root))
                add(child.path());
    }
    return result;
}

// Replacements remain recoverable while their job/Undo record lives. Never
// discard a backup changed by somebody else; leave that container for recovery.
struct Replacement {
    fs::path container, logical;
    bool payload = false;
    std::vector<fs::path> originals, backups;
    Fingerprint approved;
    bool published = false;

    explicit Replacement(const fs::path& target) : logical(target), payload(fs::exists(target))
    {
        if (payload)
            originals.push_back(target);
        auto companions = mac_companions(target);
        originals.insert(originals.end(), companions.begin(), companions.end());
        approved = fingerprint(originals);
    }

    void reserve()
    {
        if (fingerprint(originals) != approved)
            throw std::runtime_error("The destination changed after confirmation; it has been preserved");
        container = temp_name(originals.front().parent_path());
        if (!CreateDirectoryW(container.c_str(), nullptr))
            win_error(GetLastError());
        for (size_t i = 0; i < originals.size(); ++i) {
            auto backup =
                payload && i == 0 ? container / L"item" : mac_companion_target(originals[i], container / L"item");
            fs::create_directories(backup.parent_path());
            commit(originals[i], backup);
            backups.push_back(backup);
        }
        if (fingerprint(backups) != approved)
            throw std::runtime_error(
                "The destination changed during replacement; original preserved in recovery storage");
    }

    ~Replacement()
    {
        try {
            if (!published) {
                for (size_t i = backups.size(); i > 0; --i)
                    if (!fs::exists(originals[i - 1]))
                        commit(backups[i - 1], originals[i - 1]);
                // If rollback could not restore every object, retain its backup.
                for (const auto& path : backups)
                    if (fs::exists(path))
                        return;
            }
            else {
                bool remaining = false;
                for (const auto& path : backups)
                    remaining = remaining || fs::exists(path);
                if (remaining && fingerprint(backups) != approved)
                    return;
            }
            if (!container.empty())
                fs::remove_all(container);
        }
        catch (...) { /* Leave recoverable data intact on cleanup failure. */
        }
    }
};

std::shared_ptr<Replacement> replacement_for(RootCopy& root, CopyJob& job, bool& skip)
{
    if (!fs::exists(root.target) && mac_companions(root.target).empty())
        return {};
    if (unique_transfer_names(job.kind))
        throw std::runtime_error("Another item appeared at the reserved destination; it has been preserved");
    // A folder/file type mismatch is never silently converted into deletion.
    if (fs::exists(root.target) && fs::is_directory(root.target) != (root.directory && !root.archive))
        throw std::runtime_error("A file cannot replace a folder, or a folder replace a file");
    auto replacement = std::make_shared<Replacement>(root.target);
    skip = ask_conflict(job, root.source, root.target, root.directory && !root.archive) == ConflictAnswer::skip;
    return skip ? nullptr : replacement;
}

// Destination markers declare the storage convention for new copies. Walk
// toward the drive/share root so an empty child inherits its containing volume.
// OS X wins in mixed folders; an entirely unmarked destination defaults to OS X.
StorageFormat destination_format(fs::path directory)
{
    if (const auto lookup = drive_format_lookup.load()) {
        switch (lookup(directory.u8string().c_str())) {
        case CONFIG_DRIVE_WINDOWS:
            return StorageFormat::windows;
        case CONFIG_DRIVE_OSX:
            return StorageFormat::osx;
        case CONFIG_DRIVE_NETATALK:
            return StorageFormat::netatalk;
        case CONFIG_DRIVE_BASILISKII:
            return StorageFormat::basilisk;
        case CONFIG_DRIVE_ROMAW:
            return StorageFormat::percent;
        }
    }
    // File callers inherit the containing directory's storage markers.
    if (fs::is_regular_file(directory))
        directory = directory.parent_path();
    wchar_t volume[32768]{};
    if (!GetVolumePathNameW(directory.c_str(), volume, std::size(volume)))
        win_error(GetLastError());
    const auto boundary = fs::path(volume).lexically_normal();
    for (;;) {
        if (fs::exists(directory / L".DS_Store"))
            return StorageFormat::osx;
        bool osx = false, percent = false;
        for (const auto& entry : fs::directory_iterator(directory)) {
            auto name = entry.path().filename().wstring();
            bool standard = name.rfind(L"._", 0) == 0;
            if ((!standard && (name.empty() || name.front() != L'%')) || !entry.is_regular_file())
                continue;
            std::array<unsigned char, 26> header{};
            std::ifstream input(entry.path(), std::ios::binary);
            input.read(reinterpret_cast<char*>(header.data()), header.size());
            if (input.gcount() == header.size() && romaw_mac_storage_magic(header.data(), header.size())) {
                osx |= standard;
                percent |= !standard;
            }
        }
        if (osx)
            return StorageFormat::osx;
        if (fs::is_directory(directory / MAC_STORAGE_NETATALK_DIRECTORY))
            return StorageFormat::netatalk;
        if (fs::is_directory(directory / MAC_STORAGE_INFO_DIRECTORY) ||
            fs::is_directory(directory / MAC_STORAGE_RESOURCE_DIRECTORY))
            return StorageFormat::basilisk;
        if (percent)
            return StorageFormat::percent;
        auto parent = directory.parent_path();
        if (same(directory / L"", boundary / L"") || parent.empty() || parent == directory)
            return StorageFormat::osx;
        directory = parent;
    }
}

fs::path storage_target(const fs::path& item, StorageFormat format)
{
    switch (format) {
    case StorageFormat::windows:
        throw std::runtime_error("Macintosh file metadata requires MacBinary conversion for a Windows destination");
    case StorageFormat::basilisk:
        return item.parent_path() / MAC_STORAGE_INFO_DIRECTORY / item.filename();
    case StorageFormat::netatalk:
        return item.parent_path() / MAC_STORAGE_NETATALK_DIRECTORY / item.filename();
    case StorageFormat::percent:
        return item.parent_path() / (L"%" + item.filename().wstring());
    default:
        return item.parent_path() / (L"._" + item.filename().wstring());
    }
}

// Only staged files are writable here. Read-only source metadata must remain
// untouched while redundant staged companions are retired after conversion.
void remove_staged_metadata(const fs::path& path)
{
    DWORD flags = GetFileAttributesW(path.c_str());
    if (flags == INVALID_FILE_ATTRIBUTES)
        win_error(GetLastError());
    if ((flags & FILE_ATTRIBUTE_READONLY) &&
        !SetFileAttributesW(path.c_str(), (flags & ~FILE_ATTRIBUTE_READONLY) | FILE_ATTRIBUTE_ARCHIVE))
        win_error(GetLastError());
    fs::remove(path);
}

// Prefer existing primary entries, filling missing entries from split forks.
// AppleDouble-only layout changes retain the entire container byte for byte,
// including extended FinderInfo/ATTR records with absolute internal offsets.
void convert_item_metadata(const fs::path& item, StorageFormat format, CopyJob& job)
{
    auto companions = mac_companions(item);
    if (companions.empty())
        return;
    std::map<uint32_t, Bytes> parts;
    Bytes original;
    bool added = false;
    for (const auto& companion : companions) {
        if (job.stop)
            throw Interrupted();
        auto raw = read_bytes(companion);
        auto directory = companion.parent_path().filename();
        if (directory == MAC_STORAGE_RESOURCE_DIRECTORY) {
            if (parts.emplace(2, std::move(raw)).second)
                added = true;
            continue;
        }
        unsigned count;
        if (romaw_mac_storage_table(raw.data(), raw.size(), &count)) {
            bool primary = original.empty();
            std::set<uint32_t> ids;
            size_t payload_size = 0;
            for (const auto& entry : entries(raw)) {
                if (entry.length > raw.size() - payload_size)
                    throw std::runtime_error("Overlapping AppleDouble payloads exceed the container size");
                payload_size += entry.length;
                if (!ids.insert(entry.type).second)
                    throw std::runtime_error("Duplicate AppleDouble entry");
                if (parts.emplace(entry.type, slice(raw, entry.offset, entry.length)).second && !primary)
                    added = true;
            }
            if (primary) {
                // Raw entries seen before this container cannot be discarded.
                added |= parts.size() != count;
                original = std::move(raw);
            }
        }
        else if (directory == MAC_STORAGE_INFO_DIRECTORY) {
            ROMAWMacStorageEntry compact;
            if (!romaw_mac_storage_find(raw.data(), raw.size(), raw.size(), 9, 1, &compact))
                throw std::runtime_error("Invalid Basilisk Finder information");
            if (parts.emplace(9, slice(raw, compact.offset, compact.length)).second)
                added = true;
        }
        else
            throw std::runtime_error("Invalid AppleDouble metadata");
    }
    if (!parts.count(9)) {
        parts[9].resize(32);
        added = true;
    }
    auto target = storage_target(item, format);
    auto resource_target = item.parent_path() / MAC_STORAGE_RESOURCE_DIRECTORY / item.filename();
    auto check_target = [&](const fs::path& path) {
        if (fs::exists(path) && std::find(companions.begin(), companions.end(), path) == companions.end())
            throw std::runtime_error("Converted metadata conflicts with a copied file");
    };
    check_target(target);
    if (format == StorageFormat::basilisk)
        check_target(resource_target);
    std::vector<std::pair<fs::path, fs::path>> output;
    auto stage = [&](const fs::path& path, const Bytes& bytes) {
        auto temporary = temp_name(item.parent_path());
        std::ofstream stream(temporary, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        stream.close();
        if (!stream)
            throw std::runtime_error("Cannot write converted Macintosh metadata");
        output.push_back({temporary, path});
    };
    if (format == StorageFormat::basilisk) {
        // Raw Basilisk records cannot encode comments, attributes or other
        // AppleDouble entries. Refuse a lossy conversion before publication.
        for (const auto& part : parts)
            if (part.first != 9 && part.first != 2)
                throw std::runtime_error("Basilisk metadata cannot represent these AppleDouble entries");
        if (parts.at(9).size() > 32)
            throw std::runtime_error("Basilisk metadata cannot represent extended Finder information");
        auto info = parts.at(9);
        info.resize(32);
        stage(target, info);
        if (parts.count(2))
            stage(resource_target, parts.at(2));
    }
    else if (!original.empty() && !added)
        stage(target, original);
    else {
        if (parts.size() > UINT16_MAX)
            throw std::runtime_error("Too many AppleDouble entries");
        if (parts.at(9).size() > 32)
            throw std::runtime_error("Cannot combine split forks with extended Finder information");
        parts[9].resize(32);
        Bytes result(26 + parts.size() * 12);
        if (!original.empty())
            std::copy_n(original.begin(), 26, result.begin());
        const unsigned char header[] = {0, 5, 22, 7, 0, 2, 0, 0};
        std::copy(std::begin(header), std::end(header), result.begin());
        result[24] = parts.size() >> 8;
        result[25] = parts.size();
        unsigned index = 0;
        auto append = [&](uint32_t id, const Bytes& bytes) {
            if (bytes.size() > 64 * 1024 * 1024 - result.size())
                throw std::runtime_error("AppleDouble metadata is too large");
            ROMAWMacStorageEntry entry{id, uint32_t(result.size()), uint32_t(bytes.size()), 0};
            result.insert(result.end(), bytes.begin(), bytes.end());
            if (!romaw_mac_storage_store(result.data(), result.size(), result.size(), index++, &entry))
                throw std::runtime_error("Invalid AppleDouble entry");
        };
        append(9, parts.at(9));
        for (const auto& part : parts)
            if (part.first != 9 && part.first != 2)
                append(part.first, part.second);
        if (parts.count(2))
            append(2, parts.at(2));
        stage(target, result);
    }
    for (const auto& companion : companions)
        remove_staged_metadata(companion);
    for (const auto& file : output) {
        fs::create_directories(file.second.parent_path());
        commit(file.first, file.second);
    }
}

// The destination convention applies to the whole copied tree, including
// metadata-only icon carriers. Source markers never override that convention.
void convert_metadata_tree(const fs::path& root, StorageFormat format, CopyJob& job)
{
    std::vector<fs::path> directories{root};
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
        if (job.stop)
            throw Interrupted();
        if (it->is_directory()) {
            auto name = it->path().filename();
            if (name == MAC_STORAGE_INFO_DIRECTORY || name == MAC_STORAGE_RESOURCE_DIRECTORY ||
                name == MAC_STORAGE_NETATALK_DIRECTORY)
                it.disable_recursion_pending();
            else
                directories.push_back(it->path());
        }
    }
    for (auto dir = directories.rbegin(); dir != directories.rend(); ++dir) {
        const auto& directory = *dir;
        std::set<fs::path> names;
        for (const auto& entry : fs::directory_iterator(directory)) {
            auto name = entry.path().filename().wstring();
            if (entry.is_regular_file()) {
                if (name.rfind(L"._", 0) == 0)
                    names.insert(name.substr(2));
                else if (!name.empty() && name.front() == L'%' && !mac_companions(directory / name.substr(1)).empty()) {
                    auto candidates = mac_companions(directory / name.substr(1));
                    if (std::find(candidates.begin(), candidates.end(), entry.path()) != candidates.end())
                        names.insert(name.substr(1));
                }
            }
            else if (entry.is_directory() && (name == L".AppleDouble" || name == L".finf" || name == L".rsrc")) {
                for (const auto& companion : fs::directory_iterator(entry.path())) {
                    if (!companion.is_regular_file())
                        throw std::runtime_error("Invalid Macintosh metadata directory");
                    if (name != L".AppleDouble" || companion.path().filename() != L".Parent")
                        names.insert(companion.path().filename());
                }
            }
        }
        for (const auto& name : names)
            convert_item_metadata(directory / name, format, job);
        auto parent_info = directory / MAC_STORAGE_NETATALK_DIRECTORY / L".Parent";
        if (fs::is_regular_file(parent_info)) {
            // Folder metadata uses its sibling name in every output format.
            auto sibling = directory.parent_path() / (L"._" + directory.filename().wstring());
            if (fs::exists(sibling))
                throw std::runtime_error("Conflicting folder Finder information");
            commit(parent_info, sibling);
            convert_item_metadata(directory, format, job);
        }
        for (const char* kind :
             {MAC_STORAGE_INFO_DIRECTORY, MAC_STORAGE_RESOURCE_DIRECTORY, MAC_STORAGE_NETATALK_DIRECTORY}) {
            auto folder = directory / kind;
            if (fs::is_directory(folder) && fs::is_empty(folder))
                fs::remove(folder);
        }
    }
}

// Windows imports transform only verified private staging. Folder publication
// remains atomic even when two decoded or stripped names collide.
fs::path import_staged_file(const fs::path& path, const fs::path& logical, CopyJob& job)
{
    if (job.stop)
        throw Interrupted();
    if (!mac_companions(path).empty())
        return logical.filename();
    ImportedFile binary;
    fs::path name = logical.filename();
    Bytes metadata;
    if (read_macbinary(path, binary, true, logical)) {
        name = binary.name;
        metadata = std::move(binary.metadata);
        remove_staged_metadata(path);
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(binary.data.data()), binary.data.size());
        stream.close();
        if (!stream || read_bytes(path, binary.data.size()) != binary.data)
            throw std::runtime_error("Cannot verify the decoded MacBinary data fork");
    }
    else {
        uint32_t type = 0, creator = 0;
        bool strip = false;
        const auto lookup = host_type_lookup.load();
        if (!lookup || !lookup(logical, type, creator, strip))
            return name;
        if (strip && logical.has_extension())
            name = logical.stem();
        metadata.resize(70);
        const unsigned char header[] = {0, 5, 22, 7, 0, 2, 0, 0};
        std::copy(std::begin(header), std::end(header), metadata.begin());
        metadata[25] = 1;
        ROMAWMacStorageEntry entry{9, 38, 32, 0};
        if (!romaw_mac_storage_store(metadata.data(), metadata.size(), metadata.size(), 0, &entry))
            throw std::runtime_error("Cannot create mapped Finder information");
        for (unsigned i = 0; i < 4; ++i) {
            metadata[38 + i] = type >> (24 - i * 8);
            metadata[42 + i] = creator >> (24 - i * 8);
        }
    }
    auto companion = path.parent_path() / (L"._" + path.filename().wstring());
    std::ofstream stream(companion, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(metadata.data()), metadata.size());
    stream.close();
    if (!stream || read_bytes(companion, metadata.size()) != metadata)
        throw std::runtime_error("Cannot verify imported Macintosh metadata");
    return name;
}

void import_staged_tree(const fs::path& root, CopyJob& job)
{
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
        if (hidden_metadata(it->path())) {
            if (it->is_directory())
                it.disable_recursion_pending();
        }
        else if (it->is_regular_file())
            files.push_back(it->path());
    }

    struct Planned {
        fs::path source, target, temporary;
    };

    auto less = [](const fs::path& a, const fs::path& b) {
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    };
    std::set<fs::path, decltype(less)> sources(less), targets(less);
    std::vector<Planned> planned;
    sources.insert(files.begin(), files.end());
    for (const auto& file : files) {
        auto target = file.parent_path() / host_import_name(file);
        if (!targets.insert(target).second ||
            ((fs::exists(target) || !mac_companions(target).empty()) && !sources.count(target)))
            throw std::runtime_error("Decoded or mapped filenames collide; original retained");
        planned.push_back({file, target, temp_name(file.parent_path())});
    }
    // Move every completed file through a private name so a valid rename cycle
    // or a target vacated by another mapped file never depends on traversal order.
    for (const auto& item : planned) {
        if (import_staged_file(item.source, item.source.filename(), job) != item.target.filename())
            throw std::runtime_error("The decoded or mapped source name changed during the transfer");
        relocate_mac_companions(item.source, item.temporary);
        commit(item.source, item.temporary);
    }
    for (const auto& item : planned) {
        relocate_mac_companions(item.temporary, item.target);
        commit(item.temporary, item.target);
    }
}

// Plan and copy one job into temporary containers, then publish complete
// logical items. Cancellation removes unfinished staging; already committed
// items remain.
void run(const std::shared_ptr<CopyJob>& job)
{
    std::vector<RootCopy> roots;
    auto outcome = CopyJob::complete;
    try {
        job->state = CopyJob::planning;
        for (size_t i = 0; i < job->sources.size(); ++i) {
            if (job->stop)
                throw Interrupted();
            const auto& source = job->sources[i];
            if (!job->stream_writer)
                reject_link(source);
            RootCopy root{};
            root.source = source;
            root.target = job->destinations[i];
            root.directory = job->stream_writer ? job->stream_directories[i] : fs::is_directory(source);
            root.format = destination_format(root.target.parent_path());
            root.archive = root.directory && root.format == StorageFormat::windows &&
                           destination_format(source) != StorageFormat::windows;
            root.binary = job->binary_files[i];
            root.importing = job->import_files[i];
            if (root.binary && !file_archive_writer.load())
                throw std::runtime_error("The MacBinary encoder is unavailable");
            if (root.archive && (!job->stuffit_folders || !folder_archive_writer.load()))
                throw std::runtime_error("MacBinary cannot contain folders; choose ZIP or StuffIt first");
            root.rename = moving_transfer(job->kind) && same_storage_volume(source, root.target.parent_path());
            if (root.archive || root.binary || root.importing)
                root.rename = false;
#ifdef FINDER_TESTING
            if (copy_test_staged_moves())
                root.rename = false;
#endif
            root.convert = !root.rename;
            if (root.rename) {
                roots.push_back(std::move(root));
                ++job->items;
                continue;
            }
            root.temporary = temp_name(root.target.parent_path());
            if (!CreateDirectoryW(root.temporary.c_str(), nullptr))
                win_error(GetLastError());
            roots.push_back(std::move(root));
            auto& r = roots.back();
            if (job->stream_writer) {
                r.payload = r.temporary / L"data";
                ++job->items;
                continue;
            }
            if (r.directory) {
                r.payload = r.temporary / L"data";
                fs::create_directory(r.payload);
                for (const auto& e : fs::recursive_directory_iterator(source)) {
                    if (job->stop)
                        throw Interrupted();
                    reject_link(e.path());
                    auto dest = r.payload / e.path().lexically_relative(source);
                    if (e.is_directory()) {
                        fs::create_directory(dest);
                        r.directories.push_back(e.path());
                    }
                    else if (e.is_regular_file()) {
                        bool item = !hidden_metadata(e.path());
                        for (const auto& component : e.path().lexically_relative(source))
                            if (component == L".AppleDouble" || component == MAC_STORAGE_INFO_DIRECTORY ||
                                component == MAC_STORAGE_RESOURCE_DIRECTORY)
                                item = false;
                        add_part(r, e.path(), dest, *job, item);
                    }
                }
            }
            else {
                r.payload = r.temporary / L"data";
                add_part(r, source, r.payload, *job);
            }
            for (const auto& companion : mac_companions(source)) {
                // A stationery copy edits FinderInfo while still private.
                // Keep the companion layout recognizable to the metadata editor.
                auto stage = r.convert || job->kind == TransferKind::stationery
                                 ? mac_companion_target(companion, r.payload)
                                 : r.temporary / (L".root-fork-" + std::to_wstring(r.companions.size()));
                if (r.convert || job->kind == TransferKind::stationery)
                    fs::create_directories(stage.parent_path());
                r.companions.push_back({stage, mac_companion_target(companion, r.target)});
                add_part(r, companion, stage, *job, false);
            }
        }
        job->state = CopyJob::copying;
        for (auto& root : roots) {
            if (job->stop)
                throw Interrupted();
            {
                std::lock_guard<std::mutex> guard(job->mutex);
                job->current = root.source.filename().wstring();
                job->from = root.source.parent_path().wstring();
                job->to = root.target.parent_path().wstring();
            }
            bool skip = false;
            auto replacement = replacement_for(root, *job, skip);
            if (skip) {
                uint64_t bytes = 0, items = root.rename || job->stream_writer ? 1 : 0;
                for (const auto& part : root.parts) {
                    bytes += part.size;
                    if (part.item)
                        ++items;
                }
                job->total -= bytes;
                job->items -= items;
                if (!root.temporary.empty()) {
                    fs::remove_all(root.temporary);
                    root.temporary.clear();
                }
                continue;
            }
            if (root.rename) {
                if (replacement)
                    replacement->reserve();
                {
                    std::lock_guard<std::mutex> guard(job->mutex);
                    job->current = root.source.filename().wstring();
                    job->from = root.source.parent_path().wstring();
                    job->to = root.target.parent_path().wstring();
                }
                commit(root.source, root.target);
                try {
                    relocate_mac_companions(root.source, root.target);
                }
                catch (...) {
                    commit(root.target, root.source);
                    throw;
                }
                if (replacement) {
                    replacement->published = true;
                    job->replacements.push_back(replacement);
                    if (replacement->payload)
                        job->moved.push_back({replacement->logical, replacement->container / L"item"});
                    else
                        for (size_t i = 0; i < replacement->originals.size(); ++i)
                            job->moved.push_back({replacement->originals[i], replacement->backups[i]});
                }
                job->moved.push_back({root.source, root.target});
                --job->items;
                continue;
            }
            if (job->stream_writer) {
                job->stream_writer(size_t(&root - roots.data()), root.payload, *job);
                for (const auto& companion : mac_companions(root.payload))
                    root.companions.push_back({companion, mac_companion_target(companion, root.target)});
            }
            for (const auto& part : root.parts) {
                if (job->stop)
                    throw Interrupted();
                {
                    std::lock_guard<std::mutex> guard(job->mutex);
                    job->current = part.source.filename().wstring();
                    job->from = root.source.parent_path().filename().wstring();
                    job->to = root.target.parent_path().filename().wstring();
                }
                Progress state{job.get(), job->done.load()};
                if (!CopyFileExW(part.source.c_str(), part.target.c_str(), progress, &state, nullptr,
                                 COPY_FILE_FAIL_IF_EXISTS)) {
                    if (job->stop || GetLastError() == ERROR_REQUEST_ABORTED)
                        throw Interrupted();
                    win_error(GetLastError());
                }
                if (fs::file_size(part.target) != part.size)
                    throw std::runtime_error("Source size changed during copy; unfinished item removed");
                job->done = state.base + part.size;
                if (part.item && root.directory)
                    --job->items;
            }
            if (job->stop)
                throw Interrupted();
            if (job->kind == TransferKind::stationery) {
                auto unlock = [](const fs::path& path) {
                    DWORD flags = GetFileAttributesW(path.c_str());
                    if (flags == INVALID_FILE_ATTRIBUTES)
                        win_error(GetLastError());
                    if (flags & FILE_ATTRIBUTE_READONLY) {
                        DWORD writable = flags & ~FILE_ATTRIBUTE_READONLY;
                        if (!SetFileAttributesW(path.c_str(), writable ? writable : FILE_ATTRIBUTE_NORMAL))
                            win_error(GetLastError());
                    }
                };
                unlock(root.payload);
                for (const auto& companion : root.companions)
                    unlock(companion.first);
                set_stationery(root.payload, false);
                root.companions.clear();
                for (const auto& companion : mac_companions(root.payload))
                    root.companions.push_back({companion, mac_companion_target(companion, root.target)});
            }
#ifdef FINDER_TESTING
            copy_test_before_publish(*job);
#endif
            std::vector<MoveSource> source_files, source_directories;
            if (moving_transfer(job->kind)) {
                if (root.directory) {
                    source_directories.emplace_back(root.source, true);
                    for (const auto& path : root.directories)
                        source_directories.emplace_back(path, true);
                }
                for (const auto& part : root.parts) {
                    source_files.emplace_back(part.source, false);
                    verify_move_source(source_files.back(), part, *job);
                }
                if (root.directory) {
                    std::set<fs::path> expected(root.directories.begin(), root.directories.end()), actual;
                    for (const auto& part : root.parts)
                        if (beneath(root.source, part.source))
                            expected.insert(part.source);
                    for (const auto& entry : fs::recursive_directory_iterator(root.source)) {
                        reject_link(entry.path());
                        actual.insert(entry.path());
                    }
                    if (expected != actual)
                        throw std::runtime_error("Folder changed during move; original retained");
                }
                if (job->stop)
                    throw Interrupted();
            }
            // Cross-volume moves verify the original staged streams first and
            // retain their no-write/no-delete source locks during conversion.
            if (root.convert) {
                if (root.importing) {
                    if (root.directory)
                        import_staged_tree(root.payload, *job);
                    else if (import_staged_file(root.payload, root.source.filename(), *job) !=
                             job->import_names[size_t(&root - roots.data())])
                        throw std::runtime_error("The decoded or mapped source name changed during the transfer");
                }
                convert_metadata_tree(root.temporary, root.archive || root.binary ? StorageFormat::osx : root.format,
                                      *job);
                root.companions.clear();
                if (root.archive) {
                    auto archive = root.temporary / L"archive.sit";
                    folder_archive_writer.load()(root.payload, root.source.filename(), archive, *job, job->zip_folders);
                    root.payload = archive;
                }
                else if (root.binary) {
                    auto archive = root.temporary / L"archive.bin";
                    file_archive_writer.load()(root.payload, root.target.filename().stem(), archive, *job);
                    root.payload = archive;
                }
                const auto& logical = root.payload;
                if (!root.archive && !root.binary && !logical.empty())
                    for (const auto& companion : mac_companions(logical))
                        root.companions.push_back({companion, mac_companion_target(companion, root.target)});
            }
            if (job->stop)
                throw Interrupted();
            // Check again for a destination created while this item copied.
            if (!replacement)
                replacement = replacement_for(root, *job, skip);
            if (skip) {
                uint64_t skipped_bytes = 0;
                for (const auto& part : root.parts)
                    skipped_bytes += part.size;
                job->total -= skipped_bytes;
                job->done -= skipped_bytes;
                if (!root.directory || job->stream_writer)
                    --job->items;
                fs::remove_all(root.temporary);
                root.temporary.clear();
                continue;
            }
            if (replacement)
                replacement->reserve();
            // Commit each complete logical item without replacing another file.
            std::vector<fs::path> committed;
            try {
                for (const auto& companion : root.companions) {
                    fs::create_directories(companion.second.parent_path());
                    reject_link(companion.second.parent_path());
                    commit(companion.first, companion.second);
                    committed.push_back(companion.second);
                }
                commit(!root.payload.empty() ? root.payload
                       : root.directory      ? root.temporary
                                             : root.temporary / L"data",
                       root.target);
                if (!root.directory || job->stream_writer)
                    --job->items;
            }
            catch (...) {
                for (const auto& companion : committed) {
                    std::error_code ignored;
                    fs::remove(companion, ignored);
                }
                throw;
            }
            if (replacement) {
                replacement->published = true;
                job->replacements.push_back(replacement);
                if (moving_transfer(job->kind)) {
                    if (replacement->payload)
                        job->moved.push_back({replacement->logical, replacement->container / L"item"});
                    else
                        for (size_t i = 0; i < replacement->originals.size(); ++i)
                            job->moved.push_back({replacement->originals[i], replacement->backups[i]});
                }
            }
            // Only empty format directories remain in this private container.
            std::error_code ignored;
            fs::remove_all(root.temporary, ignored);
            root.temporary.clear();
            if (moving_transfer(job->kind)) {
                // Once published, finish this logical item even if Stop arrives.
                // New children make directory deletion fail and remain at source.
                for (auto& file : source_files)
                    file.remove();
                for (auto directory = source_directories.rbegin(); directory != source_directories.rend(); ++directory)
                    directory->remove();
                if (root.archive || root.binary || root.importing)
                    job->archived_sources.push_back(root.source);
                else
                    job->moved.push_back({root.source, root.target});
            }
        }
        outcome = CopyJob::complete;
    }
    catch (const Interrupted&) {
        outcome = CopyJob::cancelled;
    }
    catch (const std::exception& e) {
        std::lock_guard<std::mutex> guard(job->mutex);
        auto text = std::string(e.what());
        int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
        job->error.resize(n);
        MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), job->error.data(), n);
        outcome = job->stop ? CopyJob::cancelled : CopyJob::failed;
    }
    for (const auto& r : roots)
        if (!r.temporary.empty()) {
            std::error_code ignored;
            fs::remove_all(r.temporary, ignored);
        }
    job->stream_writer = {};
    job->stream_directories.clear();
    job->state = outcome;
}
}

// Use volume GUIDs when available; mapped roots fall back conservatively to
// their mount path. Different mounts default to copying rather than deleting.
bool same_storage_volume(const fs::path& source, const fs::path& destination)
{
    wchar_t from[32768]{}, to[32768]{}, from_id[64]{}, to_id[64]{};
    if (!GetVolumePathNameW(source.c_str(), from, 32768) || !GetVolumePathNameW(destination.c_str(), to, 32768))
        return false;
    if (GetVolumeNameForVolumeMountPointW(from, from_id, 64) && GetVolumeNameForVolumeMountPointW(to, to_id, 64))
        return _wcsicmp(from_id, to_id) == 0;
    return _wcsicmp(from, to) == 0;
}

bool windows_destination(const fs::path& folder)
{
    return destination_format(folder) == StorageFormat::windows;
}

HostFileStamp host_file_stamp(const fs::path& path)
{
    reject_link(path);
    bool directory = fs::is_directory(path);
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, directory ? FILE_FLAG_BACKUP_SEMANTICS : 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        win_error(GetLastError());
    try {
        auto identity = file_identity(handle);
        CloseHandle(handle);
        return {path, directory, identity};
    }
    catch (...) {
        CloseHandle(handle);
        throw;
    }
}

std::shared_ptr<HostMoveLease> lock_host_move(const std::vector<HostFileStamp>& stamps)
{
    struct Lease final : HostMoveLease {
        std::vector<HostFileStamp> stamps;
        std::vector<MoveSource> locked;
        std::map<fs::path, size_t> indexes;

        explicit Lease(const std::vector<HostFileStamp>& source) : stamps(source)
        {
            for (const auto& stamp : stamps) {
                indexes.emplace(stamp.path, locked.size());
                locked.emplace_back(stamp.path, stamp.directory);
            }
            verify();
        }

        void verify() override
        {
            for (size_t i = 0; i < stamps.size(); ++i) {
                const auto& stamp = stamps[i];
                if (file_identity(locked[i].handle) != stamp.identity)
                    throw std::runtime_error("The move source changed; original retained");
                if (stamp.directory) {
                    std::set<fs::path> expected, actual;
                    for (const auto& child : stamps)
                        if (child.path.parent_path() == stamp.path)
                            expected.insert(child.path);
                    for (const auto& child : fs::directory_iterator(stamp.path)) {
                        reject_link(child.path());
                        actual.insert(child.path());
                    }
                    if (expected != actual)
                        throw std::runtime_error("The source folder changed; original retained");
                }
            }
        }

        void read(const fs::path& path, uint64_t offset, void* buffer, size_t count) override
        {
            auto handle = locked.at(indexes.at(path)).handle;
            LARGE_INTEGER position{};
            position.QuadPart = offset;
            DWORD received = 0;
            if (count > MAXDWORD || !SetFilePointerEx(handle, position, nullptr, FILE_BEGIN) ||
                !ReadFile(handle, buffer, DWORD(count), &received, nullptr) || received != count)
                throw std::runtime_error("Cannot verify the original move source");
        }

        void remove() override
        {
            // The destination has already been flushed and published. Remove
            // verified objects by their handles, never by a replaceable path.
            for (size_t i = 0; i < stamps.size(); ++i)
                if (!stamps[i].directory)
                    locked[i].remove();
            for (size_t i = stamps.size(); i > 0; --i)
                if (stamps[i - 1].directory)
                    locked[i - 1].remove();
        }
    };

    return std::make_shared<Lease>(stamps);
}

bool read_macbinary(const fs::path& path, ImportedFile& file, bool data, const fs::path& logical_name)
{
    const auto& name = logical_name.empty() ? path : logical_name;
    auto extension = name.extension().wstring();
    if (_wcsicmp(extension.c_str(), L".bin") && _wcsicmp(extension.c_str(), L".macbin") &&
        _wcsicmp(extension.c_str(), L".macbinary"))
        return false;
    const auto reader = binary_reader.load();
    return reader && reader(path, file, data);
}

fs::path host_import_name(const fs::path& path, Bytes* info)
{
    if (info)
        info->clear();
    if (fs::is_directory(path) || !mac_companions(path).empty())
        return path.filename();
    ImportedFile binary;
    if (read_macbinary(path, binary)) {
        if (info)
            *info = std::move(binary.finder_info);
        return binary.name;
    }
    uint32_t type = 0, creator = 0;
    bool strip = false;
    const auto lookup = host_type_lookup.load();
    if (lookup && lookup(path, type, creator, strip)) {
        if (info) {
            info->resize(32);
            for (unsigned i = 0; i < 4; ++i) {
                (*info)[i] = type >> (24 - i * 8);
                (*info)[4 + i] = creator >> (24 - i * 8);
            }
        }
        if (strip && path.has_extension())
            return path.stem();
    }
    return path.filename();
}

bool moving_transfer(TransferKind kind)
{
    return kind == TransferKind::move || kind == TransferKind::trash;
}

bool trash_contains_items(const fs::path& folder)
{
    DWORD attributes = GetFileAttributesW(folder.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return true; // Do not inspect redirected Trash locations.
    std::error_code error;
    fs::directory_iterator item(folder, error), end;
    if (error)
        return error != std::errc::no_such_file_or_directory;
    for (; item != end; item.increment(error)) {
        if (error)
            return true; // An unreadable Trash must not be reported as empty.
        if (!hidden_metadata(item->path()))
            return true;
    }
    return bool(error);
}

// Read bounded metadata into owned bytes. An unopened file yields an empty
// vector; oversized or incomplete reads throw.
Bytes read_bytes(const fs::path& p, size_t limit)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return {};
    auto size = fs::file_size(p);
    if (size > limit)
        throw std::runtime_error("Metadata exceeds its size limit");
    Bytes b(size);
    if (size && !f.read(reinterpret_cast<char*>(b.data()), size))
        throw std::runtime_error("Cannot read metadata");
    return b;
}

// Prefer an existing Netatalk fork only when the standard AppleDouble
// companion is absent; new companions use the standard name.
fs::path sidecar(const fs::path& p)
{
    auto standard = p.parent_path() / (L"._" + p.filename().wstring());
    auto netatalk = p.parent_path() / L".AppleDouble" / p.filename();
    if (!fs::exists(standard) && fs::is_regular_file(netatalk))
        return netatalk;
    return standard;
}

std::vector<fs::path> mac_companions(const fs::path& path)
{
    std::vector<fs::path> result;
    for (const auto& candidate : {path.parent_path() / (L"._" + path.filename().wstring()),
                                  path.parent_path() / MAC_STORAGE_NETATALK_DIRECTORY / path.filename(),
                                  path.parent_path() / MAC_STORAGE_INFO_DIRECTORY / path.filename(),
                                  path.parent_path() / MAC_STORAGE_RESOURCE_DIRECTORY / path.filename(),
                                  path.parent_path() / (L"%" + path.filename().wstring())}) {
        std::error_code error;
        if (fs::is_regular_file(candidate, error)) {
            if (candidate.filename().wstring().rfind(L"%", 0) == 0) {
                std::array<unsigned char, 512> header{};
                HANDLE input =
                    CreateFileW(candidate.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, 0, nullptr);
                if (input == INVALID_HANDLE_VALUE)
                    continue;
                DWORD received = 0;
                bool read = ReadFile(input, header.data(), DWORD(header.size()), &received, nullptr) != 0;
                CloseHandle(input);
                unsigned count;
                if (!read || !romaw_mac_storage_table(header.data(), received, &count))
                    continue;
            }
            result.push_back(candidate);
        }
    }
    return result;
}

// Preserve the source storage convention even when moving between volumes.
fs::path mac_companion_target(const fs::path& companion, const fs::path& target)
{
    auto directory = companion.parent_path().filename();
    if (directory == MAC_STORAGE_INFO_DIRECTORY || directory == MAC_STORAGE_RESOURCE_DIRECTORY ||
        directory == MAC_STORAGE_NETATALK_DIRECTORY)
        return target.parent_path() / directory / target.filename();
    auto name = companion.filename().wstring();
    return target.parent_path() / ((name.rfind(L"._", 0) == 0 ? L"._" : L"%") + target.filename().wstring());
}

void relocate_mac_companions(const fs::path& source, const fs::path& target)
{
    std::vector<std::pair<fs::path, fs::path>> moved;
    auto companions = mac_companions(source);
    for (const auto& companion : companions) {
        auto destination = mac_companion_target(companion, target);
        if (!same(companion, destination) && fs::exists(destination))
            throw std::runtime_error("The destination metadata is already in use");
    }
    try {
        for (const auto& companion : companions) {
            auto destination = mac_companion_target(companion, target);
            fs::create_directories(destination.parent_path());
            reject_link(companion);
            reject_link(destination.parent_path());
            if (!MoveFileExW(companion.c_str(), destination.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH))
                win_error(GetLastError());
            moved.push_back({companion, destination});
        }
    }
    catch (...) {
        for (auto i = moved.rbegin(); i != moved.rend(); ++i)
            MoveFileExW(i->second.c_str(), i->first.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);
        throw;
    }
}

// Choose special-folder artwork by the configured System Folder's identity and
// immediate children, rather than by a folder name alone.
int special_folder_kind(const fs::path& folder, const fs::path& system_folder)
{
    auto path = normalized(folder), system = normalized(system_folder);
    if (same(path, system))
        return 0;
    if (!same(path.parent_path(), system))
        return -1;
    const wchar_t* names[] = {L"Apple Menu Items", L"Control Panels", L"Preferences", L"Fonts", L"Extensions"};
    for (unsigned i = 0; i < std::size(names); ++i)
        if (_wcsicmp(path.filename().c_str(), names[i]) == 0)
            return int(i + 1);
    return -1;
}

// Share the catalog decoder between host sidecars and live mounted images.
MacFile mac_catalog_info(const unsigned char info[32], bool directory)
{
    MacFile out;
    out.has_finder_info = true;
    auto word = [&](unsigned offset) { return uint16_t(uint16_t(info[offset]) << 8 | info[offset + 1]); };
    out.type = uint32_t(word(0)) << 16 | word(2);
    out.creator = uint32_t(word(4)) << 16 | word(6);
    out.flags = word(8);
    out.icon_y = int16_t(word(10));
    out.icon_x = int16_t(word(12));
    out.positioned = (out.flags & 0x100) && out.icon_x != INT16_MIN && out.icon_y != INT16_MIN;
    if (directory) {
        out.folder_view = word(14);
        int top = int16_t(word(0)), left = int16_t(word(2));
        int bottom = int16_t(word(4)), right = int16_t(word(6));
        out.windowed = bottom - top >= 100 && right - left >= 100;
        out.window_bounds = RECT{left, top, right, bottom};
    }
    return out;
}

// Decode optional Finder information, comments and resource bytes, then
// classify the item for classic launching. Malformed metadata is reported to
// the caller.
MacFile mac_file(const fs::path& p)
{
    MacFile out;
    bool have_info = false;
    std::exception_ptr failure;
    auto apply_info = [&](const Bytes& b, size_t offset, size_t length) {
        if (length < 16 || have_info)
            return;
        unsigned char info[32]{};
        std::copy_n(b.data() + offset, std::min(length, sizeof info), info);
        auto catalog = mac_catalog_info(info, fs::is_directory(p));
        out.finder_info.assign(info, info + 32);
        out.has_finder_info = true;
        out.type = catalog.type;
        out.creator = catalog.creator;
        out.flags = catalog.flags;
        out.icon_x = catalog.icon_x;
        out.icon_y = catalog.icon_y;
        out.positioned = catalog.positioned;
        out.windowed = catalog.windowed && (catalog.flags & 0x100);
        out.window_bounds = catalog.window_bounds;
        out.folder_view = catalog.folder_view;
        have_info = true;
    };
    ImportedFile binary;
    auto extension = p.extension().wstring();
    bool binary_name = !_wcsicmp(extension.c_str(), L".bin") || !_wcsicmp(extension.c_str(), L".macbin") ||
                       !_wcsicmp(extension.c_str(), L".macbinary");
    if (binary_name && binary_reader.load() && windows_destination(p) && read_macbinary(p, binary)) {
        out.macbinary = true;
        for (const auto& entry : entries(binary.metadata)) {
            if (entry.type == 9)
                apply_info(binary.metadata, entry.offset, entry.length);
            else if (entry.type == 2)
                out.fork = slice(binary.metadata, entry.offset, entry.length);
            else if (entry.type == 4 && entry.length <= 4096) {
                int length =
                    MultiByteToWideChar(10000, 0, reinterpret_cast<const char*>(binary.metadata.data() + entry.offset),
                                        int(entry.length), nullptr, 0);
                out.comment.resize(length);
                MultiByteToWideChar(10000, 0, reinterpret_cast<const char*>(binary.metadata.data() + entry.offset),
                                    int(entry.length), out.comment.data(), length);
            }
        }
    }
    for (const auto& companion : out.macbinary ? std::vector<fs::path>{} : mac_companions(p)) {
        try {
            auto b = read_bytes(companion);
            if (companion.parent_path().filename() == MAC_STORAGE_RESOURCE_DIRECTORY) {
                if (out.fork.empty())
                    out.fork = std::move(b);
                continue;
            }
            ROMAWMacStorageEntry compact;
            unsigned count;
            if (!romaw_mac_storage_table(b.data(), b.size(), &count) &&
                companion.parent_path().filename() == MAC_STORAGE_INFO_DIRECTORY &&
                romaw_mac_storage_find(b.data(), b.size(), b.size(), 9, 1, &compact)) {
                apply_info(b, compact.offset, compact.length);
                continue;
            }
            for (const auto& e : entries(b))
                if (e.type == 9 && e.length >= 10) {
                    apply_info(b, e.offset, e.length);
                }
                else if (e.type == 2 && e.length && out.fork.empty())
                    out.fork = slice(b, e.offset, e.length);
                else if (e.type == 4 && e.length <= 4096) {
                    int count = MultiByteToWideChar(10000, 0, reinterpret_cast<const char*>(b.data() + e.offset),
                                                    int(e.length), nullptr, 0);
                    out.comment.resize(count);
                    MultiByteToWideChar(10000, 0, reinterpret_cast<const char*>(b.data() + e.offset), int(e.length),
                                        out.comment.data(), count);
                }
        }
        catch (...) {
            failure = std::current_exception();
        }
    }
    if (failure && !have_info && out.fork.empty())
        std::rethrow_exception(failure);
    if (out.type == 0x4150504c || out.type == 0x464e4452)
        out.kind = FINDER_CLASSIC_APPLICATION;
    if (out.type == 0x63646576)
        out.kind = FINDER_CONTROL_PANEL;
    if (out.type == 0x6466696c || out.type == 0x4446494c)
        out.kind = FINDER_DESK_ACCESSORY;
    check_resources(out);
    if (!out.kind && !fs::is_directory(p) && out.type && out.creator)
        out.kind = FINDER_CLASSIC_DOCUMENT;
    return out;
}

// Decode percent-escaped Macintosh names only when metadata identifies a
// classic item; ordinary Windows percent signs remain literal.
std::wstring mac_display_name(const fs::path& path, const MacFile& info)
{
    auto name = path.filename().wstring();
    if (!info.kind && !info.positioned && !info.type && !info.creator)
        return name;
    std::wstring result;
    auto hex = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9')
            return c - L'0';
        if (c >= L'a' && c <= L'f')
            return c - L'a' + 10;
        if (c >= L'A' && c <= L'F')
            return c - L'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == L'%' && i + 2 < name.size() && hex(name[i + 1]) >= 0 && hex(name[i + 2]) >= 0) {
            char byte = char(hex(name[i + 1]) * 16 + hex(name[i + 2]));
            wchar_t decoded;
            if (byte && MultiByteToWideChar(10000, 0, &byte, 1, &decoded, 1) == 1) {
                result += decoded;
                i += 2;
                continue;
            }
        }
        result += name[i];
    }
    return result;
}

// Find a bounded resource payload by type and signed ID; the sentinel requests
// the first resource of that type.
Bytes mac_resource(const MacFile& file, uint32_t type, int id)
{
    const auto& b = file.fork;
    if (b.empty())
        return {};
    const auto map = resource_map(b);
    size_t data = be32(b, 0), data_size = be32(b, 8);
    for (unsigned i = 0; i < map.count; ++i) {
        size_t entry = map.offset + map.types + 2 + i * 8;
        if (be32(b, entry) != type)
            continue;
        unsigned n = be16(b, entry + 4) + 1;
        size_t refs = resource_references(b, map, entry, n);
        for (unsigned j = 0; j < n; ++j) {
            size_t ref = refs + j * 12;
            if (id != 65536 && int16_t(be16(b, ref)) != id)
                continue;
            size_t offset = be32(b, ref + 4) & 0xffffff;
            if (offset > data_size || data_size - offset < 4)
                throw std::runtime_error("Invalid resource data offset");
            size_t length = be32(b, data + offset);
            if (length > data_size - offset - 4)
                throw std::runtime_error("Invalid resource length");
            return slice(b, data + offset + 4, length);
        }
    }
    return {};
}

bool is_mac_suitcase(const MacFile& file)
{
    const bool system = file.creator == 0x4d414353; // MACS
    const bool mover = file.creator == 0x444d4f56;  // DMOV
    switch (file.type) {
    case 0x5a535953: // ZSYS
    case 0x7a737973: // zsys
        return system;
    case 0x4646494c: // FFIL
    case 0x4446494c: // DFIL
        return true;
    case 0x6666696c: // ffil
    case 0x6466696c: // dfil
        // Older Font/DA Mover files also use the standalone file types.
        return mover || system;
    default:
        return false;
    }
}

// Finder exposes the user-facing resources rather than the map's internal
// tables. The Finder 9 help calls out fonts, sounds, accessories and layouts.
std::vector<SuitcaseMember> suitcase_members(const Bytes& fork)
{
    std::vector<SuitcaseMember> members;
    if (fork.empty())
        return members;
    const auto map = resource_map(fork);
    const size_t names = be16(fork, map.offset + 26);
    const size_t first_ref = map.types + 2 + map.count * 8;
    if (map.types < 28 || names < first_ref || names > map.length)
        throw std::runtime_error("Invalid suitcase resource names");
    for (unsigned i = 0; i < map.count; ++i) {
        const size_t entry = map.offset + map.types + 2 + i * 8;
        const wchar_t* kind;
        uint32_t icon_type;
        switch (be32(fork, entry)) {
        case 0x464f4e44: // FOND
            kind = L"Font family";
            icon_type = 0x4646494c; // FFIL
            break;
        case 0x464f4e54: // FONT
        case 0x4e464e54: // NFNT
            kind = L"Bitmap font";
            icon_type = 0x6666696c; // ffil
            break;
        case 0x73666e74: // sfnt
            kind = L"Outline font";
            icon_type = 0x7466696c; // tfil
            break;
        case 0x736e6420: // snd
            kind = L"Sound";
            icon_type = 0x7366696c; // sfil
            break;
        case 0x44525652: // DRVR
            kind = L"Desk accessory";
            icon_type = 0x6466696c; // dfil
            break;
        case 0x4b434852: // KCHR
            kind = L"Keyboard layout";
            icon_type = 0x6b66696c; // kfil
            break;
        default:
            continue;
        }
        unsigned count = be16(fork, entry + 4) + 1;
        size_t refs = resource_references(fork, map, entry, count) - map.offset;
        if (refs < first_ref || refs > names || count > (names - refs) / 12 || count > UINT16_MAX / 12 - members.size())
            throw std::runtime_error("Invalid suitcase resource references");
        for (unsigned j = 0; j < count; ++j) {
            size_t ref = map.offset + refs + j * 12;
            size_t name = be16(fork, ref + 2);
            std::wstring label;
            if (name != UINT16_MAX) {
                const size_t available = map.length - names;
                if (name >= available)
                    throw std::runtime_error("Invalid suitcase resource name");
                const auto* text = fork.data() + map.offset + names + name;
                int length = *text++;
                if (size_t(length) > available - name - 1)
                    throw std::runtime_error("Truncated suitcase resource name");
                if (length) {
                    label.resize(
                        MultiByteToWideChar(10000, 0, reinterpret_cast<const char*>(text), length, nullptr, 0));
                    if (!label.empty())
                        MultiByteToWideChar(10000, 0, reinterpret_cast<const char*>(text), length, label.data(),
                                            int(label.size()));
                }
            }
            if (label.empty())
                label = std::wstring(kind) + L" #" + std::to_wstring(int16_t(be16(fork, ref)));
            members.push_back({std::move(label), kind, icon_type});
        }
    }
    return members;
}

// Hide fork/database files and both current and legacy unfinished-copy
// containers from normal folder listings.
bool hidden_metadata(const fs::path& p)
{
    auto n = p.filename().wstring();
    // The native bin is a shell namespace, not an ordinary folder of $I/$R
    // records. Show it through the Trash icon, never as raw backing storage.
    if (!_wcsicmp(n.c_str(), L"$Recycle.Bin") || !_wcsicmp(n.c_str(), L"RECYCLER") ||
        !_wcsicmp(n.c_str(), L"RECYCLED")) {
        DWORD attributes = GetFileAttributesW(p.c_str());
        constexpr DWORD bin_attributes = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & bin_attributes) == bin_attributes)
            return true;
    }
    // Folder Icon<CR> files are metadata in every supported host spelling.
    // Classify names only: artwork loading and physical folder copies must
    // still be able to read and preserve these files and their resource forks.
    static const std::array<fs::path, 3> icon_helpers = {
        fs::path(MAC_STORAGE_CUSTOM_ICON_NAME), fs::path(MAC_STORAGE_ESCAPED_ICON_NAME),
        fs::path(std::wstring(L"Icon") + wchar_t(MAC_STORAGE_SFM_ICON_CHARACTER))};
    if (std::any_of(icon_helpers.begin(), icon_helpers.end(),
                    [&](const fs::path& name) { return !_wcsicmp(n.c_str(), name.c_str()); }))
        return true;
    return n.rfind(L"._", 0) == 0 || n.rfind(L".Finder-copy-", 0) == 0 || n.rfind(L".Finder64-copy-", 0) == 0 ||
           n == L".DS_Store" || n == L".AppleDouble" || n == fs::path(MAC_STORAGE_INFO_DIRECTORY).wstring() ||
           n == fs::path(MAC_STORAGE_RESOURCE_DIRECTORY).wstring() || n == L"Desktop DB" ||
           n == L"Desktop DB.romaw-new" || n == L"Desktop DF" || n == L".Desktop.lock";
}

namespace {
// Write replacement metadata to a new temporary file and publish it only after
// the complete write and close succeed.
void write_sidecar(const fs::path& p, const fs::path& path, const Bytes& b)
{
    if (mac_file(p).macbinary)
        throw std::runtime_error("Extract this MacBinary file before editing its Finder metadata.");
    auto temporary = temp_name(p.parent_path());
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        win_error(GetLastError());
    DWORD written = 0;
    bool ok = WriteFile(file, b.data(), DWORD(b.size()), &written, nullptr) && written == b.size();
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    if (!CloseHandle(file) && ok) {
        ok = false;
        error = GetLastError();
    }
    if (!ok) {
        fs::remove(temporary);
        win_error(error ? error : ERROR_WRITE_FAULT);
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        auto e = GetLastError();
        fs::remove(temporary);
        win_error(e);
    }
}
}

Bytes encode_comment(const std::wstring& comment, size_t limit)
{
    if (comment.size() > limit)
        throw std::runtime_error("Classic Finder comments are limited to " + std::to_string(limit) +
                                 " MacRoman bytes.");
    BOOL substituted = FALSE;
    int length = comment.empty() ? 0
                                 : WideCharToMultiByte(10000, WC_NO_BEST_FIT_CHARS, comment.data(), int(comment.size()),
                                                       nullptr, 0, nullptr, &substituted);
    if ((!comment.empty() && !length) || substituted || size_t(length) > limit)
        throw std::runtime_error("The comment cannot be represented as a classic Macintosh comment.");
    Bytes text(length);
    if (length)
        WideCharToMultiByte(10000, WC_NO_BEST_FIT_CHARS, comment.data(), int(comment.size()),
                            reinterpret_cast<char*>(text.data()), length, nullptr, &substituted);
    return text;
}

// Keep the logical total even when the filesystem cannot report allocation.
void FileSize::add(const FileSize& other)
{
    if (other.logical > UINT64_MAX - logical ||
        (allocated && other.allocated && *other.allocated > UINT64_MAX - *allocated))
        throw std::overflow_error("The size exceeds the supported range.");
    logical += other.logical;
    if (allocated && other.allocated)
        *allocated += *other.allocated;
    else
        allocated.reset();
}

static std::optional<uint64_t> host_allocation(const fs::path& path)
{
    HANDLE file =
        CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return {};
    FILE_BASIC_INFO basic{};
    std::optional<uint64_t> result;
    if (GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof basic)) {
        if (basic.FileAttributes & (FILE_ATTRIBUTE_SPARSE_FILE | FILE_ATTRIBUTE_COMPRESSED)) {
            FILE_COMPRESSION_INFO physical{};
            if (GetFileInformationByHandleEx(file, FileCompressionInfo, &physical, sizeof physical) &&
                physical.CompressedFileSize.QuadPart >= 0)
                result = physical.CompressedFileSize.QuadPart;
        }
        else {
            FILE_STANDARD_INFO standard{};
            if (GetFileInformationByHandleEx(file, FileStandardInfo, &standard, sizeof standard) &&
                standard.AllocationSize.QuadPart >= 0)
                result = standard.AllocationSize.QuadPart;
        }
    }
    CloseHandle(file);
    return result;
}

// Inspect only the AppleDouble descriptor table. Split resource forks have
// no container header, so their file length is the logical fork length.
static FileSize host_resource_size(const fs::path& file)
{
    for (const auto& companion : mac_companions(file)) {
        auto length = fs::file_size(companion);
        if (companion.parent_path().filename() == MAC_STORAGE_RESOURCE_DIRECTORY) {
            if (length)
                return {length, host_allocation(companion)};
            continue;
        }
        std::ifstream input(companion, std::ios::binary);
        Bytes header(26);
        if (!input.read(reinterpret_cast<char*>(header.data()), header.size()) ||
            !romaw_mac_storage_magic(header.data(), header.size()))
            continue;
        unsigned count = unsigned(header[24]) * 256 + header[25];
        header.resize(26 + count * 12);
        if (!input.read(reinterpret_cast<char*>(header.data() + 26), count * 12))
            continue;
        ROMAWMacStorageEntry fork;
        if (romaw_mac_storage_find(header.data(), header.size(), length, 2, 0, &fork) && fork.length)
            return {fork.length, host_allocation(companion)};
    }
    return {};
}

FileSize host_file_size(const fs::path& path)
{
    FileSize size{fs::file_size(path), host_allocation(path)};
    size.add(host_resource_size(path));
    return size;
}

std::optional<FileSize> host_folder_size(const fs::path& path, const std::atomic<bool>& stop)
{
    if (stop)
        return {};
    // Junctions and symbolic links must not lead the scan outside the folder.
    reject_link(path);
    FileSize total;
    for (auto it = fs::recursive_directory_iterator(path); it != fs::recursive_directory_iterator() && !stop; ++it) {
        const auto& name = it->path();
        DWORD attrs = GetFileAttributesW(name.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES)
            win_error(GetLastError());
        if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) || hidden_metadata(name)) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file())
            total.add(host_file_size(name));
    }
    return stop ? std::nullopt : std::optional<FileSize>(total);
}

// Rebuild the AppleDouble comment entry while preserving other entries.
void set_comment(const fs::path& p, const std::wstring& comment)
{
    auto text = encode_comment(comment, 200);
    auto path = sidecar(p);
    auto original = read_bytes(path);
    if (original.empty() && fs::exists(path))
        throw std::runtime_error("The sidecar is invalid; original preserved.");
    // Preserve an existing legacy AppleDouble container before publishing a
    // standard companion. Compact split forks are assembled below instead.
    if (original.empty())
        for (const auto& companion : mac_companions(p)) {
            auto candidate = read_bytes(companion);
            unsigned count;
            if (romaw_mac_storage_table(candidate.data(), candidate.size(), &count)) {
                original = std::move(candidate);
                break;
            }
        }
    std::vector<std::pair<uint32_t, Bytes>> parts;
    size_t total = 26 + text.size();
    if (!original.empty())
        for (const auto& entry : entries(original))
            if (entry.type != 4) {
                if (entry.length + 12 > 64 * 1024 * 1024 - total)
                    throw std::runtime_error("AppleDouble metadata is too large.");
                total += entry.length + 12;
                parts.push_back({entry.type, slice(original, entry.offset, entry.length)});
            }
    if (original.empty()) {
        auto catalog = mac_file(p);
        Bytes info(32);
        auto raw = read_bytes(p.parent_path() / MAC_STORAGE_INFO_DIRECTORY / p.filename());
        ROMAWMacStorageEntry entry;
        if (romaw_mac_storage_find(raw.data(), raw.size(), raw.size(), 9, 1, &entry) && !entry.offset)
            info = raw;
        if (info.size() + catalog.fork.size() + text.size() > 64 * 1024 * 1024 - 62)
            throw std::runtime_error("AppleDouble metadata is too large.");
        parts.push_back({9, std::move(info)});
        if (!catalog.fork.empty())
            parts.push_back({2, std::move(catalog.fork)});
    }
    parts.push_back({4, std::move(text)});
    if (parts.size() > 65535)
        throw std::runtime_error("Too many AppleDouble entries.");
    Bytes result(26 + parts.size() * 12);
    if (!original.empty())
        std::copy(original.begin(), original.begin() + 26, result.begin());
    const unsigned char header[] = {0, 5, 22, 7, 0, 2, 0, 0};
    std::copy(std::begin(header), std::end(header), result.begin());
    result[24] = parts.size() >> 8;
    result[25] = parts.size();
    for (size_t i = 0; i < parts.size(); ++i) {
        ROMAWMacStorageEntry entry{parts[i].first, uint32_t(result.size()), uint32_t(parts[i].second.size()), 0};
        result.insert(result.end(), parts[i].second.begin(), parts[i].second.end());
        if (!romaw_mac_storage_store(result.data(), result.size(), result.size(), unsigned(i), &entry))
            throw std::runtime_error("Invalid AppleDouble entry; original preserved.");
    }
    write_sidecar(p, path, result);
}

// Edit only the requested bits in FinderInfo and preserve companion entries.
static void set_finder_flags(const fs::path& p, uint16_t mask, uint16_t value)
{
    auto path = sidecar(p);
    auto b = read_bytes(path);
    auto raw_path = p.parent_path() / MAC_STORAGE_INFO_DIRECTORY / p.filename();
    auto raw = read_bytes(raw_path);
    ROMAWMacStorageEntry raw_info;
    unsigned raw_count;
    bool compact = !romaw_mac_storage_table(raw.data(), raw.size(), &raw_count) &&
                   romaw_mac_storage_find(raw.data(), raw.size(), raw.size(), 9, 1, &raw_info);
    auto update_compact = [&] {
        auto flags = (be16(raw, 8) & ~mask) | value;
        raw[8] = flags >> 8;
        raw[9] = flags;
        write_sidecar(p, raw_path, raw);
    };
    if (b.empty() && !fs::exists(path) && compact) {
        update_compact();
        return;
    }
    if (b.empty() && !fs::exists(path))
        for (const auto& companion : mac_companions(p)) {
            auto candidate = read_bytes(companion);
            unsigned count;
            if (romaw_mac_storage_table(candidate.data(), candidate.size(), &count)) {
                b = std::move(candidate);
                break;
            }
        }
    if (b.empty() && fs::exists(path))
        throw std::runtime_error("The empty sidecar is invalid; original preserved");
    if (b.empty()) {
        b.resize(70);
        const unsigned char header[] = {0, 5, 22, 7, 0, 2, 0, 0};
        std::copy(std::begin(header), std::end(header), b.begin());
        b[25] = 1;
        b[29] = 9;
        b[33] = 38;
        b[37] = 32;
    }
    bool found = false;
    for (const auto& e : entries(b))
        if (e.type == 9 && e.length >= 10) {
            unsigned flags = (be16(b, e.offset + 8) & ~mask) | value;
            b[e.offset + 8] = flags >> 8;
            b[e.offset + 9] = flags;
            found = true;
        }
    if (!found)
        throw std::runtime_error("The sidecar has no Finder information; original preserved");
    write_sidecar(p, path, b);
    if (compact)
        update_compact();
}

void set_label(const fs::path& p, unsigned label)
{
    if (label > 7)
        throw std::runtime_error("Invalid label");
    set_finder_flags(p, 14, label * 2);
}

// Publish FinderInfo and the resource fork in the same AppleDouble write.
// An existing split fork stays behind the new primary companion.
void write_icon_metadata(const fs::path& path, const Bytes* fork, bool custom, bool carrier)
{
    auto target = sidecar(path);
    auto original = read_bytes(target);
    if (original.empty() && fs::exists(target))
        throw std::runtime_error("The sidecar is invalid; original preserved.");
    if (original.empty())
        for (const auto& candidate : mac_companions(path)) {
            if (candidate.parent_path().filename() == MAC_STORAGE_RESOURCE_DIRECTORY)
                continue;
            auto data = read_bytes(candidate);
            unsigned count;
            if (romaw_mac_storage_table(data.data(), data.size(), &count)) {
                original = std::move(data);
                if (candidate.filename().wstring().rfind(L"%", 0) == 0)
                    target = candidate;
                break;
            }
        }
    std::vector<std::pair<uint32_t, Bytes>> parts;
    Bytes info;
    if (!original.empty())
        for (const auto& entry : entries(original)) {
            if (entry.type == 9)
                info = slice(original, entry.offset, entry.length);
            else if (entry.type != 2 || !fork)
                parts.emplace_back(entry.type, slice(original, entry.offset, entry.length));
        }
    if (info.empty()) {
        auto raw = read_bytes(path.parent_path() / MAC_STORAGE_INFO_DIRECTORY / path.filename());
        ROMAWMacStorageEntry compact;
        if (romaw_mac_storage_find(raw.data(), raw.size(), raw.size(), 9, 1, &compact))
            info = slice(raw, compact.offset, compact.length);
        else
            info.resize(32);
    }
    if (carrier) {
        info[8] |= 0x40;
        if (be32(info, 0) == 0 && be32(info, 4) == 0)
            std::copy_n("iconMACS", 8, info.begin());
    }
    else
        info[8] = (info[8] & ~4u) | (custom ? 4 : 0);
    parts.emplace_back(9, std::move(info));
    if (fork)
        parts.emplace_back(2, *fork);
    if (parts.size() > UINT16_MAX)
        throw std::runtime_error("Too many AppleDouble entries.");
    Bytes result(26 + parts.size() * 12);
    if (!original.empty())
        std::copy_n(original.begin(), 26, result.begin());
    const unsigned char header[] = {0, 5, 22, 7, 0, 2, 0, 0};
    std::copy(std::begin(header), std::end(header), result.begin());
    result[24] = parts.size() >> 8;
    result[25] = parts.size();
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].second.size() > 64 * 1024 * 1024 - result.size())
            throw std::runtime_error("AppleDouble metadata is too large.");
        ROMAWMacStorageEntry entry{parts[i].first, uint32_t(result.size()), uint32_t(parts[i].second.size()), 0};
        result.insert(result.end(), parts[i].second.begin(), parts[i].second.end());
        if (!romaw_mac_storage_store(result.data(), result.size(), result.size(), unsigned(i), &entry))
            throw std::runtime_error("Invalid AppleDouble entry; original preserved.");
    }
    write_sidecar(path, target, result);
}

void set_stationery(const fs::path& p, bool stationery)
{
    auto attributes = GetFileAttributesW(p.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY)))
        throw std::runtime_error("Unlock the document before changing its Stationery Pad setting.");
    set_finder_flags(p, stationery_flag, stationery ? stationery_flag : 0);
}

// Native database creation must supply the same Finder metadata as DTOpenInform.
// Never replace unrelated AppleDouble entries or existing resource forks.
void mark_desktop_database_files(const fs::path& root)
{
    for (unsigned index = 0; index < 2; ++index) {
        auto file = root / (index ? L"Desktop DF" : L"Desktop DB");
        auto path = sidecar(file);
        if (!fs::exists(path))
            set_label(file, 0);
        auto bytes = read_bytes(path);
        for (const auto& entry : entries(bytes))
            if (entry.type == 9 && entry.length >= 10) {
                uint32_t type = index ? 0x4454464c : 0x4254464c;
                auto offset = entry.offset;
                if (be32(bytes, offset) == type && be32(bytes, offset + 4) == 0x444d4752 &&
                    (be16(bytes, offset + 8) & 0x4000))
                    break;
                for (unsigned i = 0; i < 4; ++i) {
                    bytes[offset + i] = type >> (24 - i * 8);
                    bytes[offset + 4 + i] = 0x444d4752u >> (24 - i * 8);
                }
                bytes[offset + 8] |= 0x40;
                write_sidecar(file, path, bytes);
                break;
            }
    }
}

// Format a full 64-bit byte count in binary units through EB without floating-
// point overflow.
std::wstring format_bytes(uint64_t bytes)
{
    static const wchar_t* units[] = {L"bytes", L"KB", L"MB", L"GB", L"TB", L"PB", L"EB"};
    uint64_t whole = bytes, fraction = 0;
    unsigned unit = 0;
    while (whole >= 1024 && unit < 6) {
        fraction = whole & 1023;
        whole >>= 10;
        ++unit;
    }
    auto result = std::to_wstring(whole);
    unsigned tenth = unsigned(fraction * 10 / 1024);
    if (unit && tenth)
        result += L"." + std::to_wstring(tenth);
    return result + L" " + units[unit];
}

std::wstring format_file_size(const FileSize& size)
{
    auto disk = size.allocated ? format_bytes(*size.allocated) + L" on disk" : L"Size on disk unavailable";
    return disk + L"\r\n" + std::to_wstring(size.logical) + (size.logical == 1 ? L" byte" : L" bytes");
}

// Copy a Windows system message into owned text before releasing
// FormatMessage's allocation.
std::wstring error_text(DWORD e)
{
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   e, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::wstring result = text ? text : L"Windows error " + std::to_wstring(e);
    if (text)
        LocalFree(text);
    return result;
}

// Quote one Windows argument, doubling backslashes before quotes and the
// closing delimiter.
std::wstring quote_argument(const std::wstring& s)
{
    std::wstring out = L"\"";
    unsigned slashes = 0;
    for (wchar_t ch : s) {
        if (ch == L'\\') {
            ++slashes;
            continue;
        }
        out.append(slashes * (ch == L'"' ? 2 : 1), L'\\');
        if (ch == L'"')
            out += L'\\';
        out += ch;
        slashes = 0;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}

// Rename a same-directory logical item and its fork; restore the data name if
// publishing the fork name fails.
void rename_item(const fs::path& source, const fs::path& target)
{
    if (source.parent_path() != target.parent_path() || target.filename().empty())
        throw std::runtime_error("Invalid name");
    if (source == target)
        return;
    if (!same(source, target) && (fs::exists(target) || !mac_companions(target).empty()))
        throw std::runtime_error("The name is already in use");
    commit(source, target);
    try {
        relocate_mac_companions(source, target);
    }
    catch (...) {
        commit(target, source);
        throw;
    }
}

void CopyJob::answer_conflict(ConflictAnswer answer)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (conflict_pending && answer != ConflictAnswer::waiting) {
        conflict_answer = answer;
        conflict_pending = false;
        decision.notify_all();
    }
}

// Join any remaining worker before its shared job storage is destroyed.
CopyJob::~CopyJob()
{
    if (worker.joinable())
        worker.join();
}

// Request cancellation for every job and join workers before releasing the
// registry.
Copies::~Copies()
{
    cancel_all();
    for (auto& j : jobs_)
        if (j->worker.joinable())
            j->worker.join();
}

// Terminal states are ordered after copying, allowing this UI-thread query to
// inspect only atomic state.
bool Copies::active() const
{
    for (const auto& j : jobs_)
        if (j->state <= CopyJob::copying)
            return true;
    return false;
}

// Signal cooperative cancellation without blocking the UI on file I/O.
void Copies::cancel_all()
{
    for (auto& j : jobs_)
        j->stop = true;
    tick();
}

// On the UI thread, reclaim finished jobs and reserve names against both disk
// contents and active jobs before starting a worker.
std::shared_ptr<CopyJob> Copies::start_cooperative(const std::vector<fs::path>& sources,
                                                   const std::vector<fs::path>& destinations,
                                                   std::function<void(CopyJob&)> step, TransferKind kind)
{
    if (sources.empty() || sources.size() != destinations.size() || !step)
        throw std::runtime_error("Invalid mounted-image copy request");
    for (auto it = jobs_.begin(); it != jobs_.end();)
        if ((*it)->state >= CopyJob::complete) {
            if ((*it)->worker.joinable())
                (*it)->worker.join();
            it = jobs_.erase(it);
        }
        else
            ++it;
    if (jobs_.size() >= 32)
        throw std::runtime_error("Wait for an active operation to finish before starting another");
    for (const auto& active : jobs_)
        if (active->state <= CopyJob::copying)
            for (const auto& reserved : active->destinations)
                for (const auto& target : destinations)
                    if (beneath(reserved, target) || beneath(target, reserved))
                        throw std::runtime_error("Another operation is using this image destination");
    auto job = std::make_shared<CopyJob>();
    job->id = next_++;
    job->kind = kind;
    job->sources = sources;
    job->destinations = destinations;
    job->step = std::move(step);
    jobs_.push_back(job);
    return job;
}

void Copies::tick()
{
    for (auto& job : jobs_)
        if (job->step && job->state <= CopyJob::copying) {
            try {
                job->step(*job);
            }
            catch (const std::exception& e) {
                std::string text = e.what();
                int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
                std::lock_guard<std::mutex> guard(job->mutex);
                job->error.resize(count);
                MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), job->error.data(), count);
                job->state = job->stop && job->state != CopyJob::failed ? CopyJob::cancelled : CopyJob::failed;
            }
            if (job->state >= CopyJob::complete)
                job->step = {};
        }
}

std::shared_ptr<CopyJob> Copies::start(const std::vector<fs::path>& sources, const fs::path& folder, TransferKind kind,
                                       bool stuffit_folders, bool zip_folders)
{
    if (sources.empty() || !fs::is_directory(folder))
        throw std::runtime_error("Choose files and a destination folder");
    std::vector<std::pair<fs::path, fs::path>> changes;
    for (const auto& source : sources)
        changes.push_back({source, folder / source.filename()});
    return start_targets(changes, kind, {}, {}, stuffit_folders, zip_folders);
}

std::shared_ptr<CopyJob> Copies::start_targets(const std::vector<std::pair<fs::path, fs::path>>& changes,
                                               TransferKind kind,
                                               std::function<void(size_t, const fs::path&, CopyJob&)> writer,
                                               std::vector<bool> directories, bool stuffit_folders, bool zip_folders)
{
    if (writer && (directories.size() != changes.size() || moving_transfer(kind)))
        throw std::runtime_error("Invalid streamed copy request");
    if (kind == TransferKind::stationery &&
        (changes.size() != 1 || (writer ? directories.front() : !fs::is_regular_file(changes.front().first))))
        throw std::runtime_error("Choose one stationery document.");
    for (auto it = jobs_.begin(); it != jobs_.end();)
        if ((*it)->state >= CopyJob::complete) {
            if ((*it)->worker.joinable())
                (*it)->worker.join();
            it = jobs_.erase(it);
        }
        else
            ++it;
    if (jobs_.size() >= 32)
        throw std::runtime_error("Wait for an active operation to finish before starting another");
    if (changes.empty())
        return {};
    auto job = std::make_shared<CopyJob>();
    job->id = next_++;
    job->kind = kind;
    job->stuffit_folders = stuffit_folders;
    job->zip_folders = zip_folders;
    job->stream_writer = std::move(writer);
    job->stream_directories = std::move(directories);
    for (const auto& change : changes) {
        const auto& p = change.first;
        if (change.second.filename().empty() || change.second.filename() == L"." || change.second.filename() == L".." ||
            !fs::is_directory(change.second.parent_path()))
            throw std::runtime_error("The destination folder is unavailable");
        auto directory = normalized(change.second.parent_path());
        // Reject a selected link before canonicalization can replace its name
        // with the target and accidentally move somebody else's real item.
        if (!job->stream_writer)
            reject_link(fs::absolute(p));
        auto source = job->stream_writer ? fs::absolute(p).lexically_normal() : normalized(p);
        if (source == source.root_path() || (fs::is_directory(source) && beneath(source, directory)))
            throw std::runtime_error("A folder cannot be copied into itself");
        fs::path target = directory / change.second.filename();
        bool importing = !job->stream_writer && windows_destination(source) && !windows_destination(directory);
        auto import_name = importing ? host_import_name(source) : source.filename();
        if (importing && change.second.filename() == source.filename())
            target = directory / import_name;
        bool binary = windows_destination(directory) &&
                      (job->stream_writer ? !job->stream_directories[job->sources.size()]
                                          : !fs::is_directory(source) &&
                                                (!windows_destination(source) || !mac_companions(source).empty()));
        if (binary)
            target += L".bin";
        if (!job->stream_writer && fs::is_directory(source) && windows_destination(directory) &&
            !windows_destination(source)) {
            if (!stuffit_folders)
                throw std::runtime_error("MacBinary cannot contain folders; choose ZIP or StuffIt first");
            target += zip_folders ? L".zip" : L".sit";
        }
        if (moving_transfer(kind) && same(source, target))
            continue;
        if (!same(source, target) && fs::exists(target) && fs::is_directory(target) && beneath(target, source))
            throw std::runtime_error("A folder cannot be replaced by an item it contains");
        for (const auto& active : jobs_) {
            if (active->state > CopyJob::copying)
                continue;
            if (moving_transfer(kind) || moving_transfer(active->kind))
                for (const auto& used : active->sources)
                    if (beneath(source, used) || beneath(used, source))
                        throw std::runtime_error("An active operation is already using this source item");
            for (const auto& reserved : active->destinations)
                if (beneath(reserved, directory) || (!same(target, reserved) && beneath(target, reserved)))
                    throw std::runtime_error("An active operation is already using this destination folder");
        }
        unsigned number = 0;
        auto occupied = [&] {
            if (fs::exists(target) || !mac_companions(target).empty())
                return true;
            for (const auto& j : jobs_)
                if (j->state <= CopyJob::copying)
                    for (const auto& reserved : j->destinations)
                        if (same(reserved, target))
                            return true;
            for (const auto& reserved : job->destinations)
                if (same(reserved, target))
                    return true;
            return false;
        };
        if (!unique_transfer_names(kind) && !same(source, target)) {
            for (const auto& active : jobs_)
                if (active->state <= CopyJob::copying)
                    for (const auto& reserved : active->destinations)
                        if (same(reserved, target))
                            throw std::runtime_error("Another operation is already using this destination");
            for (const auto& reserved : job->destinations)
                if (same(reserved, target))
                    throw std::runtime_error("Choose items with distinct destination names");
        }
        if (kind == TransferKind::stationery && occupied())
            throw std::runtime_error("An item already has the requested document name.");
        while ((unique_transfer_names(kind) || same(source, target)) && occupied()) {
            ++number;
            auto basis = importing ? import_name : source.filename();
            auto name = basis.stem().wstring() + L" copy";
            if (number > 1)
                name += L" " + std::to_wstring(number);
            target = directory / (name + basis.extension().wstring());
            if (binary)
                target += L".bin";
            if (fs::is_directory(source) && windows_destination(directory) && !windows_destination(source))
                target += zip_folders ? L".zip" : L".sit";
        }
        job->sources.push_back(source);
        job->destinations.push_back(target);
        job->binary_files.push_back(binary);
        job->import_files.push_back(importing);
        job->import_names.push_back(import_name);
    }
    if (job->sources.empty())
        return {};
    jobs_.push_back(job);
    try {
        job->worker = std::thread(run, job);
    }
    catch (...) {
        jobs_.pop_back();
        throw;
    }
    return job;
}
}
