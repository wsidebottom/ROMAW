/*+===================================================================
  File:      files.h

  Summary:   Host Macintosh metadata and cancellable host or streamed copy operations.

  Classes:   Declared: Copies, CopyJob, MacFile, SuitcaseMember, ImportedFile, HostFileStamp, HostMoveLease;
             Used: Copies, CopyJob, MacFile, std::atomic, std::mutex,
             std::shared_ptr, std::thread, std::vector, std::wstring

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef FINDER_FILES_H
#define FINDER_FILES_H
#include "finder/launcher.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace finder {
namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
// Install before starting transfer workers. The callback returns ConfigDriveFormat.
void set_drive_format_lookup(int (*lookup)(const char*));

struct MacFile {
    // Decoded host-order Finder information, not a guest-memory structure.
    uint32_t type = 0, creator = 0;
    bool has_finder_info = false; // Distinguish absent DOS metadata from explicitly empty Macintosh codes.
    bool macbinary = false;
    Bytes finder_info; // Complete catalog record, including extended Finder fields.
    uint16_t flags = 0, folder_view = 0;
    // Classic saved positions are relative to the containing folder's icon view.
    int16_t icon_x = 0, icon_y = 0;
    bool positioned = false, windowed = false;
    RECT window_bounds{};
    int driver = 0, kind = 0;
    // Owned resource bytes; UI listings release them after decoding artwork.
    Bytes fork;
    std::wstring comment;
};

// An unopened file yields empty bytes; oversized or incomplete reads throw.
Bytes read_bytes(const fs::path& path, size_t limit = 64 * 1024 * 1024);
// Missing metadata is optional. Invalid header/table extents throw; resource
// payload extents are checked when a particular resource is requested.
MacFile mac_file(const fs::path& path);
// Decode catalog FinderInfo without opening or materializing any file fork.
MacFile mac_catalog_info(const unsigned char info[32], bool directory);
// The configured System Folder and its immediate special subfolders, or -1.
int special_folder_kind(const fs::path&, const fs::path& system_folder);
// ID 65536 selects the first resource of the requested type.
Bytes mac_resource(const MacFile& file, uint32_t type, int id = 65536);

struct SuitcaseMember {
    std::wstring name;
    const wchar_t* kind;
    uint32_t icon_type;
    uint32_t resource_type;
    int16_t resource_id;
};

using ResourceKey = std::pair<uint32_t, int16_t>;
struct SuitcaseResource {
    ResourceKey key;
    uint8_t attributes;
    Bytes name, payload;
};

struct SuitcaseFork {
    uint16_t attributes = 0;
    Bytes reserved_header;
    std::vector<SuitcaseResource> resources;
};

struct FontAssociation {
    unsigned size, style;
    int16_t id;
};

std::vector<FontAssociation> font_associations(const Bytes& family);
SuitcaseFork read_suitcase_fork(const Bytes& fork);
Bytes encode_suitcase_fork(const SuitcaseFork& fork);
void replace_host_suitcase_fork(const fs::path& path, const Bytes& fork, bool data_fork);
void create_host_resource_file(const fs::path& path, uint32_t type, uint32_t creator, const Bytes& fork);

bool is_mac_suitcase(const MacFile& file);
// Read the Finder-visible members by name and kind without decoding resource payloads.
std::vector<SuitcaseMember> suitcase_members(const Bytes& fork);
// Decode host name escapes only for files identified as classic Mac items.
std::wstring mac_display_name(const fs::path& path, const MacFile& info);
// Preserve other Finder flags/AppleDouble entries when editing metadata.
void set_label(const fs::path& path, unsigned label);
inline constexpr uint16_t stationery_flag = 0x0800;
void set_stationery(const fs::path& path, bool stationery);
void write_icon_metadata(const fs::path& path, const Bytes* fork, bool custom, bool carrier = false);
// Give Desktop DB/DF their classic DMGR types/invisibility, preserving forks.
void mark_desktop_database_files(const fs::path& root);
// Reject text that cannot be represented losslessly in 200 MacRoman bytes.
Bytes encode_comment(const std::wstring& comment, size_t limit);
void set_comment(const fs::path& path, const std::wstring& comment);
// Exclude forks, desktop databases and unfinished-copy storage from listings.
bool hidden_metadata(const fs::path& path);

struct FileSize {
    uint64_t logical = 0;
    std::optional<uint64_t> allocated = 0;
    void add(const FileSize& other);
};

// Count logical data and resource bytes and their disk allocation.
FileSize host_file_size(const fs::path& path);
std::optional<FileSize> host_folder_size(const fs::path& path, const std::atomic<bool>& stop);
// Count committed logical Trash items, ignoring forks and private staging.
bool trash_contains_items(const fs::path& folder);
// Locate an existing standard/Netatalk companion, or name a new standard one.
fs::path sidecar(const fs::path& path);
// Existing companions in the driver's precedence order, including split forks.
std::vector<fs::path> mac_companions(const fs::path& path);
fs::path mac_companion_target(const fs::path& companion, const fs::path& target);
// Move every metadata/resource stream, restoring earlier moves on failure.
void relocate_mac_companions(const fs::path& source, const fs::path& target);
// Binary-unit formatting supports the full unsigned 64-bit byte range.
std::wstring format_bytes(uint64_t bytes);
std::wstring format_file_size(const FileSize& size);
// Return owned text after releasing Windows' allocated system-message buffer.
std::wstring error_text(DWORD error);
// Encode one argument for Windows command-line parsing, including backslashes.
std::wstring quote_argument(const std::wstring& argument);
// Same-directory rename of data and fork, with rollback if the fork move fails.
void rename_item(const fs::path& source, const fs::path& target);

// Host volumes decide the default drag action; modifiers may override it.
bool same_storage_volume(const fs::path& source, const fs::path& destination);
enum class TransferKind { duplicate, copy, move, trash, trash_copy, stationery };
// Trash moves retain ordinary move cancellation, source protection and Undo.
bool moving_transfer(TransferKind kind);
enum class ConflictAnswer { waiting, replace, skip, stop };

struct CopyJob {
    // Ordering is used by active/terminal comparisons throughout the UI.
    enum State { queued, planning, waiting, copying, complete, cancelled, failed };

    unsigned id = 0;
    TransferKind kind = TransferKind::duplicate;
    bool stuffit_folders = false; // Explicitly approved on the UI thread.
    bool zip_folders = false;
    std::shared_ptr<void> source_owner; // Retain private extraction data until worker/image import completion.
    // Indexes into sources and destinations. Skipped conflicts are absent.
    struct PublishedItem {
        size_t index;
        uint32_t image_id = 0;
    };
    std::vector<PublishedItem> published_items;
    // Published before terminal state; only the UI consumes completed moves.
    std::vector<std::pair<fs::path, fs::path>> moved;
    // Format-changing archive moves cannot use pathname-only Undo. Close their old
    // windows after publication rather than following them into an archive file.
    std::vector<fs::path> archived_sources;
    // The worker publishes these counters/state; the UI polls them without locks.
    std::atomic<State> state{queued};
    // The UI sets this flag; the worker polls it between and during file copies.
    std::atomic<bool> stop{false};
    // Bytes include companion streams; items counts visible regular payloads.
    std::atomic<uint64_t> total{0}, done{0}, items{0};
    mutable std::mutex mutex;
    std::condition_variable decision;
    bool conflict_pending = false;
    bool conflict_directory = false;
    ConflictAnswer conflict_answer = ConflictAnswer::waiting;
    std::wstring conflict_source, conflict_target, conflict_name;
    void answer_conflict(ConflictAnswer answer);
    // Own old destination containers through UI-thread Undo/history lifetime.
    std::vector<std::shared_ptr<void>> replacements;
    // Every read/write of these strings requires mutex, including UI snapshots.
    std::wstring current, from, to, error;
    // Paths are reserved on the UI thread and immutable once the worker starts.
    std::vector<fs::path> sources, destinations;
    std::vector<bool> binary_files; // Keep MacBinary conversion paired with its reserved .bin name.
    std::vector<bool> import_files; // Windows-to-Mac conversion, including trees and same-volume moves.
    std::vector<fs::path> import_names;
    // Stream directly into destination staging; sources need not be host files.
    std::function<void(size_t, const fs::path&, CopyJob&)> stream_writer;
    std::vector<bool> stream_directories;
    // UI-thread File Manager operations advance without a worker or nested event loop.
    std::function<void(CopyJob&)> step;
    // Registry reclamation or destruction joins this worker.
    std::thread worker;
    ~CopyJob();
};

using FolderArchiveWriter = void (*)(const fs::path&, const fs::path&, const fs::path&, CopyJob&, bool);
void set_folder_archive_writer(FolderArchiveWriter writer);
using FileArchiveWriter = void (*)(const fs::path&, const fs::path&, const fs::path&, CopyJob&);
void set_file_archive_writer(FileArchiveWriter writer);

struct ImportedFile {
    fs::path name;
    Bytes data, metadata, finder_info, prefix;
    uint64_t data_offset = 0, data_size = 0, resource_offset = 0, resource_size = 0;
};

using MacBinaryReader = bool (*)(const fs::path&, ImportedFile&, bool);
using HostTypeLookup = bool (*)(const fs::path&, uint32_t&, uint32_t&, bool&);
void set_host_import_services(MacBinaryReader reader, HostTypeLookup lookup);
bool read_macbinary(const fs::path& path, ImportedFile& file, bool data = false, const fs::path& logical_name = {});
fs::path host_import_name(const fs::path& path, Bytes* finder_info = nullptr);

struct HostFileStamp {
    fs::path path;
    bool directory;
    std::array<DWORD, 7> identity;
};

HostFileStamp host_file_stamp(const fs::path& path);

class HostMoveLease {
  public:
    virtual ~HostMoveLease() = default;
    virtual void read(const fs::path& path, uint64_t offset, void* buffer, size_t count) = 0;
    virtual void verify() = 0;
    virtual void remove() = 0;
};

// Own a reserved temporary directory until its transfer has completed.
struct TransferStaging {
    fs::path path;

    TransferStaging();
    ~TransferStaging();
    TransferStaging(const TransferStaging&) = delete;
    TransferStaging& operator=(const TransferStaging&) = delete;
};

std::shared_ptr<HostMoveLease> lock_host_move(const std::vector<HostFileStamp>& stamps);
bool windows_destination(const fs::path& folder);

// UI-thread registry. Each job has its own worker and cancellation flag;
// starting another operation never replaces an active job's state.
class Copies {
    std::vector<std::shared_ptr<CopyJob>> jobs_;
    unsigned next_ = 1;

  public:
    // Shutdown cancels and joins workers before releasing registry ownership.
    ~Copies();
    // Reserve names against disk and active jobs, then start an independent copy.
    std::shared_ptr<CopyJob> start(const std::vector<fs::path>& sources, const fs::path& folder,
                                   TransferKind kind = TransferKind::duplicate, bool stuffit_folders = false,
                                   bool zip_folders = false);

    // Explicit logical destinations support Put Away and other restored names,
    // including one job whose items return to different containing folders.
    std::shared_ptr<CopyJob> start_targets(const std::vector<std::pair<fs::path, fs::path>>& changes,
                                           TransferKind kind = TransferKind::move,
                                           std::function<void(size_t, const fs::path&, CopyJob&)> writer = {},
                                           std::vector<bool> directories = {}, bool stuffit_folders = false,
                                           bool zip_folders = false);

    std::shared_ptr<CopyJob> start_cooperative(const std::vector<fs::path>& sources,
                                               const std::vector<fs::path>& destinations,
                                               std::function<void(CopyJob&)> step,
                                               TransferKind kind = TransferKind::copy);
    void tick();

    // Borrow the registry only on the UI thread; workers never mutate it.
    const std::vector<std::shared_ptr<CopyJob>>& jobs() const
    {
        return jobs_;
    }

    bool active() const;
    // Signal cancellation; joining is deferred to reclamation or destruction.
    void cancel_all();
};
}
#endif
