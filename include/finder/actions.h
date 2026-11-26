/*+===================================================================
  File:      actions.h

  Summary:   Declare native move/Undo actions, shell links and host and Macintosh catalog searches.

  Classes:   Declared: FindOptions, FindQuery, History, Record, Relocation, Search, SearchMatch, SearchRoot;
             Used: std::array, std::atomic, std::mutex, std::set, std::thread, std::vector, std::wstring.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef FINDER_ACTIONS_H
#define FINDER_ACTIONS_H
#include "finder/files.h"
#include "finder/image_volumes.h"
#include <functional>
#include <array>
#include <optional>
#include <set>

namespace finder {
struct Relocation {
    // One logical move includes an optional AppleDouble companion.
    fs::path source, target;
};

// Rebase an open folder/item path after a completed move, without consulting
// the old filesystem name (which may no longer exist). Compare components.
bool relocated_path(const fs::path& path, const Relocation& change, fs::path& result);
// Moves a logical item together with its AppleDouble fork, without overwriting.
void relocate_item(const fs::path& source, const fs::path& target);
// Validate a single host filename and retain the suffix of a Windows shortcut.
fs::path host_rename_target(const fs::path& source, const std::wstring& name);
std::array<DWORD, 3> host_item_identity(const fs::path& path);
// Find an unused data/fork name without modifying the directory.
fs::path unique_item(const fs::path& directory, const std::wstring& name);
// Resolve plain aliases with cycle/depth checks. Links with arguments must run through the shell.
fs::path shortcut_target(const fs::path& path);
// The calling thread must initialize COM before using shell-link services.
void create_shortcut(const fs::path& source, const fs::path& target, const std::wstring& arguments = {},
                     const fs::path& icon = {});

// Session-local Undo. Moves are guarded by volume/file identity; removal of a
// created file also checks its size/write time so later edits are not deleted.
class History {
    struct Record {
        std::wstring name;
        std::vector<Relocation> changes;
        bool created;
        std::vector<std::array<DWORD, 7>> identities;
        std::shared_ptr<void> recovery;
    };

    std::vector<Record> records_;

  public:
    // Failed moves attempt rollback; the history retains at most 64 records.
    void move(const std::vector<Relocation>& changes, const std::wstring& name);
    // Register successful worker results on the UI thread, without moving again.
    void remember_moves(const std::vector<Relocation>& changes, const std::wstring& name,
                        std::shared_ptr<void> recovery = {});
    void created(const fs::path& path, const std::wstring& name);

    bool available() const
    {
        return !records_.empty();
    }

    std::wstring name() const
    {
        return available() ? records_.back().name : L"";
    }

    // Return the successful inverse moves; an empty target denotes deletion.
    std::vector<Relocation> undo();

    // Forget records only; this does not change any files.
    void clear()
    {
        records_.clear();
    }
};

enum class FindField {
    name = 3050,
    size = -12805,
    kind = 3051,
    label = -3061,
    created = -20803,
    modified = -20805,
    version = 12802,
    comments = 20807,
    lock = -20806
};
enum class FindRelation {
    contains = 3,
    starts = 4,
    ends = 5,
    equal = 2,
    unequal = -3,
    excludes = -2,
    less = -1,
    greater = 1
};
enum class FindKind { alias = 1, application, document, folder, stationery, other };
enum class FindScope { current = 0, all_disks = 1, selection = 3, disk };

struct FindQuery {
    FindField field = FindField::name;
    FindRelation relation = FindRelation::contains;
    std::wstring text;
    uint64_t value = 0;
    const wchar_t* validate();
};

struct FindOptions {
    FindQuery query;
    bool expanded = false;
    FindScope scope = FindScope::current;
    bool all_at_once = false;
    fs::path disk{};
};

struct SearchRoot {
    fs::path path;
    int16_t image = 0;
    uint32_t id = 2, parent = 0;
    std::string name{};
    bool include_item = false;
    std::wstring title{};
};

struct SearchMatch {
    fs::path path;
    int16_t image = 0;
    FinderImageEntry catalog{};
};

// The host traversal owns one worker. Live Macintosh File Manager calls run in
// bounded slices on Finder's event thread. Both publish under the same lock.
class Search {
    struct CatalogFrame {
        fs::path path;
        int16_t image;
        uint32_t id;
        int next = 1;
    };

    std::vector<SearchRoot> image_roots;
    std::vector<CatalogFrame> catalog_stack;
    std::atomic<bool> stop{false}, host_done{true};
    bool include_hidden;
    mutable std::mutex mutex;
    std::vector<SearchMatch> results;
    std::set<fs::path> published;
    std::wstring error;
    std::thread worker;
    void publish(SearchMatch match);
    void report_error(const wchar_t* message);

  public:
    const std::vector<SearchRoot> roots;
    const FindOptions options;
    std::atomic<bool> limited{false};
    Search(std::vector<SearchRoot> roots, FindOptions options, bool include_hidden = false);
    ~Search();
    void advance_images();
    bool finished() const;
    size_t result_count() const;
    std::vector<SearchMatch> snapshot() const;
    std::wstring problem() const;
};

// Invariant, case-insensitive substring matching rather than wildcard syntax.
bool name_matches(const std::wstring& name, const std::wstring& query);
bool find_text_matches(const FindQuery& query, const std::wstring& text);
bool find_number_matches(const FindQuery& query, std::optional<uint64_t> number);
FindKind find_kind(const MacFile& metadata, bool folder, bool shortcut = false);
std::wstring find_version(const unsigned char* bytes, size_t size);
std::optional<uint64_t> find_date(FILETIME timestamp);
std::wstring image_search_comment(int16_t reference, uint32_t id);
}
#endif
