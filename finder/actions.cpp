/*+===================================================================
  File:      actions.cpp

  Summary:   Native file actions, Undo and background search. A logical item includes its data and AppleDouble or split
             metadata/fork companions. Undo records identities rather than trusting a reused pathname; search results
             cross threads only through atomics and locked snapshots.

  Classes:   Used: FindOptions, FindQuery, MacFile, Record, Relocation, SearchMatch, SearchRoot,
             std::array, std::error_code, std::exception, std::lock_guard,
             std::mutex, std::runtime_error, std::thread, std::vector, std::wstring

  Functions: finder::History::created, finder::History::move, finder::History::undo, finder::Search::Search,
             finder::Search::snapshot, finder::Search::~Search, finder::create_shortcut, finder::name_matches,
             finder::Search::finished, finder::Search::result_count, finder::Search::problem,
             finder::FindQuery::validate, finder::find_date, finder::find_kind, finder::find_number_matches,
             finder::find_text_matches, finder::find_version, finder::relocate_item, finder::shortcut_target,
             finder::unique_item, finder::host_rename_target, finder::host_item_identity.

  Origin:    Existing repository source in executor/src/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

// Native file actions, Undo and background search. A logical item includes its
// data and AppleDouble or split metadata/fork companions. Undo records identities rather than
// trusting a reused pathname; search results cross threads only through
// atomics and locked snapshots.

#include "finder/actions.h"
#include "finder/views.h"
#include "finder/launcher.h"
#include <algorithm>
#include <cstring>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <stdexcept>
#include <system_error>

namespace finder {
namespace {
// Turn a failed Windows operation into an exception for the owning UI command.
void check(bool ok)
{
    if (!ok)
        throw std::system_error(GetLastError(), std::system_category());
}

// Turn a failed Windows operation into an exception for the owning UI command.
void check(HRESULT result)
{
    if (FAILED(result))
        throw std::runtime_error("The shortcut could not be accessed.");
}

// Compare Windows path spelling without case when checking destinations and
// alias cycles.
bool equal_path(const fs::path& a, const fs::path& b)
{
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}

// Capture volume/file identity plus size and write time for later Undo
// validation; close the probe handle before returning.
std::array<DWORD, 7> identity(const fs::path& path)
{
    HANDLE file = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("The item is no longer available.");
    BY_HANDLE_FILE_INFORMATION info{};
    BOOL success = GetFileInformationByHandle(file, &info);
    CloseHandle(file);
    if (!success)
        throw std::runtime_error("The item could not be identified.");
    return {info.dwVolumeSerialNumber,
            info.nFileIndexHigh,
            info.nFileIndexLow,
            info.nFileSizeHigh,
            info.nFileSizeLow,
            info.ftLastWriteTime.dwHighDateTime,
            info.ftLastWriteTime.dwLowDateTime};
}

// Reject volume roots and reparse points before a relocation can affect data
// outside its intended tree.
void ordinary(const fs::path& p)
{
    DWORD flags = GetFileAttributesW(p.c_str());
    check(flags != INVALID_FILE_ATTRIBUTES);
    if (flags & FILE_ATTRIBUTE_REPARSE_POINT)
        throw std::runtime_error("Moving junctions and symbolic links is not supported.");
    if (p == p.root_path())
        throw std::runtime_error("A volume cannot be moved.");
}

// Compare volume identities before selecting the kernel or shell directory-
// move implementation.
bool same_volume(const fs::path& source, const fs::path& target)
{
    wchar_t source_root[32768]{}, target_root[32768]{};
    check(GetVolumePathNameW(source.c_str(), source_root, 32768) != FALSE);
    check(GetVolumePathNameW(target.parent_path().c_str(), target_root, 32768) != FALSE);
    wchar_t source_volume[64]{}, target_volume[64]{};
    if (GetVolumeNameForVolumeMountPointW(source_root, source_volume, 64) &&
        GetVolumeNameForVolumeMountPointW(target_root, target_volume, 64))
        return _wcsicmp(source_volume, target_volume) == 0;
    return _wcsicmp(source_root, target_root) == 0;
}

// Use a kernel move when possible; cross-volume directory moves use the shell
// after rejecting links in the source tree.
void move_payload(const fs::path& source, const fs::path& target)
{
    bool cross_directory = fs::is_directory(source) && !same_volume(source, target);
    if (!cross_directory) {
        check(MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH) != FALSE);
        return;
    }
    // Cross-volume directory renames can return ACCESS_DENIED as well as
    // NOT_SAME_DEVICE. Select the shell path by volume, rather than by error code.
    for (const auto& entry : fs::recursive_directory_iterator(source))
        if (GetFileAttributesW(entry.path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)
            throw std::runtime_error("Cross-volume folder moves cannot contain junctions or symbolic links.");
    auto from = source.wstring() + L'\0';
    auto to = target.wstring() + L'\0';
    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_MOVE;
    operation.pFrom = from.c_str();
    operation.pTo = to.c_str();
    operation.fFlags = FOF_MULTIDESTFILES | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI | FOF_SILENT;
    int result = SHFileOperationW(&operation);
    if (result || operation.fAnyOperationsAborted)
        throw std::runtime_error(
            "The folder move did not complete. Its source and destination have been preserved for inspection.");
}

}

bool relocated_path(const fs::path& path, const Relocation& change, fs::path& result)
{
    if (path.empty() || change.source.empty())
        return false;
    auto normalized_path = path.lexically_normal();
    auto source = change.source.lexically_normal();
    auto entry = normalized_path.begin();
    for (auto component = source.begin(); component != source.end(); ++component) {
        if (component->empty())
            continue;
        if (entry == normalized_path.end() || !equal_path(*entry, *component))
            return false;
        ++entry;
    }
    result = change.target;
    for (; entry != normalized_path.end(); ++entry)
        result /= *entry;
    return true;
}

fs::path host_rename_target(const fs::path& source, const std::wstring& name)
{
    if (name.empty() || name.back() == L'.' || name.back() == L' ' ||
        name.find_first_of(L"<>:\"/\\|?*") != std::wstring::npos ||
        std::any_of(name.begin(), name.end(), [](wchar_t character) { return character < 32; }))
        throw std::runtime_error("Choose a valid filename.");
    fs::path target(name);
    if (_wcsicmp(source.extension().c_str(), L".lnk") == 0 && _wcsicmp(target.extension().c_str(), L".lnk"))
        target += L".lnk";
    return source.parent_path() / target;
}

std::array<DWORD, 3> host_item_identity(const fs::path& path)
{
    auto file = identity(path);
    return {file[0], file[1], file[2]};
}

// Choose an unused data/fork name without overwriting either member of another
// logical item.
fs::path unique_item(const fs::path& directory, const std::wstring& name)
{
    fs::path candidate = directory / name;
    for (unsigned i = 2; fs::exists(candidate) || !mac_companions(candidate).empty(); ++i)
        candidate = directory / (fs::path(name).stem().wstring() + L" " + std::to_wstring(i) +
                                 fs::path(name).extension().wstring());
    return candidate;
}

// Move a data/fork pair after containment checks, rolling the data move back
// if its companion cannot be moved.
void relocate_item(const fs::path& from, const fs::path& to)
{
    ordinary(fs::absolute(from));
    if (from.parent_path() == to.parent_path() && equal_path(from, to)) {
        rename_item(from, to);
        return;
    }
    const auto source = fs::weakly_canonical(fs::absolute(from));
    const auto target = fs::weakly_canonical(fs::absolute(to));
    ordinary(source);
    if (equal_path(source, target))
        return;
    if (!fs::is_directory(target.parent_path()) || fs::exists(target) || !mac_companions(target).empty())
        throw std::runtime_error("The destination is unavailable or already contains that name.");
    if (fs::is_directory(source)) {
        for (auto parent = target.parent_path(); !parent.empty();) {
            if (equal_path(parent, source))
                throw std::runtime_error("A folder cannot be moved into itself.");
            auto next = parent.parent_path();
            if (next == parent)
                break;
            parent = next;
        }
    }
    // The shell supplies cross-volume directory moves when the kernel cannot.
    move_payload(source, target);
    try {
        relocate_mac_companions(source, target);
    }
    catch (...) {
        move_payload(target, source);
        throw;
    }
}

// Resolve native .lnk chains with bounded depth and cycle checks, releasing
// COM interfaces on every iteration.
fs::path shortcut_target(const fs::path& path)
{
    if (_wcsicmp(path.extension().c_str(), L".lnk"))
        return {};
    fs::path current = fs::absolute(path);
    std::vector<fs::path> visited;
    for (unsigned depth = 0; depth < 32; ++depth) {
        if (_wcsicmp(current.extension().c_str(), L".lnk"))
            return current;
        for (const auto& previous : visited)
            if (equal_path(previous, current))
                throw std::runtime_error("The alias refers back to itself.");
        visited.push_back(current);
        IShellLinkW* link = nullptr;
        check(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)));
        IPersistFile* file = nullptr;
        HRESULT result = link->QueryInterface(IID_PPV_ARGS(&file));
        if (SUCCEEDED(result))
            result = file->Load(current.c_str(), STGM_READ);
        wchar_t arguments[32768]{};
        if (SUCCEEDED(result))
            result = link->GetArguments(arguments, std::size(arguments));
        wchar_t target[32768]{};
        if (SUCCEEDED(result))
            result = link->GetPath(target, 32768, nullptr, SLGP_RAWPATH);
        if (file)
            file->Release();
        link->Release();
        check(result);
        if (*arguments)
            return {};
        if (!*target)
            throw std::runtime_error("The alias has no original item.");
        current = target;
    }
    throw std::runtime_error("The alias chain is too long.");
}

// Save a native shell link to an existing absolute target; never replace an
// existing alias name.
void create_shortcut(const fs::path& source, const fs::path& target, const std::wstring& arguments,
                     const fs::path& icon)
{
    if (fs::exists(target))
        throw std::runtime_error("The alias name is already in use.");
    auto absolute = fs::absolute(source);
    if (!fs::exists(absolute))
        throw std::runtime_error("The original item is unavailable.");
    IShellLinkW* link = nullptr;
    check(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)));
    IPersistFile* file = nullptr;
    HRESULT result = link->SetPath(absolute.c_str());
    if (SUCCEEDED(result))
        result = link->SetWorkingDirectory(absolute.parent_path().c_str());
    if (SUCCEEDED(result) && !arguments.empty())
        result = link->SetArguments(arguments.c_str());
    if (SUCCEEDED(result) && !icon.empty())
        result = link->SetIconLocation(icon.c_str(), 0);
    if (SUCCEEDED(result))
        result = link->QueryInterface(IID_PPV_ARGS(&file));
    if (SUCCEEDED(result))
        result = file->Save(target.c_str(), TRUE);
    if (file)
        file->Release();
    link->Release();
    check(result);
}

// Relocate a batch and record destination identities. A failed move attempts
// reverse rollback of earlier members.
void History::move(const std::vector<Relocation>& changes, const std::wstring& name)
{
    if (changes.empty())
        return;
    size_t moved = 0;
    try {
        for (const auto& change : changes) {
            relocate_item(change.source, change.target);
            ++moved;
        }
    }
    catch (...) {
        while (moved) {
            const auto& change = changes[--moved];
            relocate_item(change.target, change.source);
        }
        throw;
    }
    remember_moves(changes, name);
}

void History::remember_moves(const std::vector<Relocation>& changes, const std::wstring& name,
                             std::shared_ptr<void> recovery)
{
    if (changes.empty())
        return;
    Record record{name, changes, false, {}, std::move(recovery)};
    for (const auto& change : changes)
        record.identities.push_back(identity(change.target));
    records_.push_back(std::move(record));
    if (records_.size() > 64)
        records_.erase(records_.begin());
}

// Remember a newly created file or empty directory so Undo can later verify
// its identity before removing it.
void History::created(const fs::path& path, const std::wstring& name)
{
    records_.push_back({name, {{{}, path}}, true, {identity(path)}, {}});
    if (records_.size() > 64)
        records_.erase(records_.begin());
}

// Validate identities before reversing the newest record. Created files also
// require unchanged size/time; removal is never recursive.
std::vector<Relocation> History::undo()
{
    if (!available())
        return {};
    std::vector<Relocation> undone;
    auto& record = records_.back();
    for (size_t i = 0; i < record.changes.size(); ++i) {
        auto current = identity(record.changes[i].target);
        size_t extent = record.created && !fs::is_directory(record.changes[i].target) ? 7 : 3;
        if (!std::equal(current.begin(), current.begin() + extent, record.identities[i].begin()))
            throw std::runtime_error("Undo is unavailable because the item was replaced or edited.");
    }
    // Allocate the notification plan before changing any filesystem objects.
    if (record.created)
        undone.push_back({record.changes[0].target, {}});
    else
        for (auto change = record.changes.rbegin(); change != record.changes.rend(); ++change)
            undone.push_back({change->target, change->source});
    if (record.created) {
        const auto& path = record.changes[0].target;
        ordinary(path);
        // Never recursively delete an item which has been edited after creation.
        if (!fs::remove(path))
            throw std::runtime_error("The new item can no longer be removed.");
    }
    else {
        size_t moved = 0;
        try {
            for (auto it = record.changes.rbegin(); it != record.changes.rend(); ++it) {
                relocate_item(it->target, it->source);
                ++moved;
            }
        }
        catch (...) {
            auto start = record.changes.size() - moved;
            for (size_t i = start; i < record.changes.size(); ++i)
                relocate_item(record.changes[i].source, record.changes[i].target);
            throw;
        }
    }
    records_.pop_back();
    return undone;
}

// Perform an invariant, case-insensitive Unicode substring search; an empty
// query matches nothing.
bool name_matches(const std::wstring& name, const std::wstring& query)
{
    return find_text_matches({FindField::name, FindRelation::contains, query}, name);
}

bool find_text_matches(const FindQuery& query, const std::wstring& text)
{
    if (query.text.empty())
        return false;
    if (query.relation == FindRelation::equal || query.relation == FindRelation::unequal) {
        bool equal = CompareStringOrdinal(text.c_str(), int(text.size()), query.text.c_str(), int(query.text.size()),
                                          TRUE) == CSTR_EQUAL;
        return equal == (query.relation == FindRelation::equal);
    }
    DWORD mode = query.relation == FindRelation::starts ? FIND_STARTSWITH
                 : query.relation == FindRelation::ends ? FIND_ENDSWITH
                                                        : FIND_FROMSTART;
    bool found =
        !text.empty() && FindNLSStringEx(LOCALE_NAME_INVARIANT, mode | NORM_IGNORECASE, text.c_str(), int(text.size()),
                                         query.text.c_str(), int(query.text.size()), nullptr, nullptr, nullptr, 0) >= 0;
    return found != (query.relation == FindRelation::excludes);
}

bool find_number_matches(const FindQuery& query, std::optional<uint64_t> number)
{
    if (!number)
        return false;
    switch (query.relation) {
    case FindRelation::equal:
        return *number == query.value;
    case FindRelation::unequal:
        return *number != query.value;
    case FindRelation::less:
        return *number < query.value;
    case FindRelation::greater:
        return *number > query.value;
    default:
        return false;
    }
}

const wchar_t* FindQuery::validate()
{
    if (field == FindField::size) {
        uint64_t bytes = 0;
        if (text.empty())
            return L"Enter a size in bytes.";
        for (wchar_t digit : text) {
            if (digit < L'0' || digit > L'9' || bytes > (UINT64_MAX - (digit - L'0')) / 10)
                return L"Enter a size in bytes using whole numbers.";
            bytes = bytes * 10 + (digit - L'0');
        }
        value = bytes;
    }
    else if (field == FindField::created || field == FindField::modified) {
        auto failure = L"Enter a valid date as YYYY-MM-DD.";
        if (text.size() != 10 || text[4] != L'-' || text[7] != L'-')
            return failure;
        for (size_t i = 0; i < text.size(); ++i)
            if (i != 4 && i != 7 && (text[i] < L'0' || text[i] > L'9'))
                return failure;
        SYSTEMTIME date{};
        date.wYear = WORD(std::stoi(text.substr(0, 4)));
        date.wMonth = WORD(std::stoi(text.substr(5, 2)));
        date.wDay = WORD(std::stoi(text.substr(8, 2)));
        FILETIME ticks{};
        if (!SystemTimeToFileTime(&date, &ticks))
            return failure;
        value = uint64_t(date.wYear) * 10000 + date.wMonth * 100 + date.wDay;
    }
    else if ((field == FindField::name || field == FindField::version || field == FindField::comments) && text.empty())
        return L"Enter some text to find.";
    return nullptr;
}

std::optional<uint64_t> find_date(FILETIME timestamp)
{
    FILETIME local{};
    SYSTEMTIME date{};
    if (!(timestamp.dwLowDateTime || timestamp.dwHighDateTime) || !FileTimeToLocalFileTime(&timestamp, &local) ||
        !FileTimeToSystemTime(&local, &date))
        return {};
    return uint64_t(date.wYear) * 10000 + date.wMonth * 100 + date.wDay;
}

FindKind find_kind(const MacFile& metadata, bool folder, bool shortcut)
{
    if (folder)
        return FindKind::folder;
    if (shortcut || (metadata.flags & 0x8000))
        return FindKind::alias;
    if (metadata.flags & 0x0800)
        return FindKind::stationery;
    if (metadata.kind == FINDER_CLASSIC_APPLICATION || metadata.type == 0x4150504c || metadata.type == 0x464e4452)
        return FindKind::application;
    return metadata.kind == FINDER_CONTROL_PANEL || metadata.kind == FINDER_DESK_ACCESSORY ||
                   metadata.type == 0x63646576 || metadata.type == 0x6466696c || metadata.type == 0x4446494c
               ? FindKind::other
               : FindKind::document;
}

std::wstring find_version(const unsigned char* bytes, size_t size)
{
    if (size < 7 || !bytes[6] || size_t(bytes[6]) > size - 7)
        return {};
    auto text = reinterpret_cast<const char*>(bytes + 7);
    int length = MultiByteToWideChar(10000, 0, text, bytes[6], nullptr, 0);
    std::wstring version(length, L'\0');
    if (length)
        MultiByteToWideChar(10000, 0, text, bytes[6], version.data(), length);
    return version;
}

namespace {
bool host_matches(const FindQuery& query, const fs::path& path, const MacFile& info,
                  const WIN32_FILE_ATTRIBUTE_DATA& attributes)
{
    DWORD flags = attributes.dwFileAttributes;
    bool folder = flags & FILE_ATTRIBUTE_DIRECTORY;
    bool shortcut = !_wcsicmp(path.extension().c_str(), L".lnk");
    switch (query.field) {
    case FindField::name:
        return find_text_matches(query, shortcut ? path.stem().wstring() : mac_display_name(path, info));
    case FindField::size:
        return !folder &&
               find_number_matches(query, ((uint64_t(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow) +
                                              info.fork.size());
    case FindField::kind:
        return find_number_matches(query, unsigned(find_kind(info, folder, shortcut)));
    case FindField::label:
        return find_number_matches(query, (info.flags >> 1) & 7);
    case FindField::created:
        return find_number_matches(query, find_date(attributes.ftCreationTime));
    case FindField::modified:
        return find_number_matches(query, find_date(attributes.ftLastWriteTime));
    case FindField::version: {
        auto version = mac_resource(info, 0x76657273);
        auto text = find_version(version.data(), version.size());
        return !text.empty() && find_text_matches(query, text);
    }
    case FindField::comments:
        return find_text_matches(query, info.comment);
    case FindField::lock:
        return !folder && find_number_matches(query, (flags & FILE_ATTRIBUTE_READONLY) != 0);
    }
    return false;
}
}

Search::Search(std::vector<SearchRoot> scopes, FindOptions selection, bool hidden)
    : include_hidden(hidden), roots(std::move(scopes)), options(std::move(selection))
{
    for (auto it = roots.rbegin(); it != roots.rend(); ++it)
        if (it->image)
            image_roots.push_back(*it);
    if (image_roots.size() == roots.size())
        return;
    host_done = false;
    worker = std::thread([this] {
        try {
            auto examine = [&](const fs::path& path, const WIN32_FILE_ATTRIBUTE_DATA& attributes) {
                DWORD flags = attributes.dwFileAttributes;
                if ((flags & FILE_ATTRIBUTE_REPARSE_POINT) ||
                    (!include_hidden && ((flags & FILE_ATTRIBUTE_HIDDEN) || hidden_metadata(path))))
                    return false;
                try {
                    MacFile info = mac_file(path);
                    EntryVisibility visibility{
                        info.type, info.creator, flags, info.flags, bool(flags & FILE_ATTRIBUTE_DIRECTORY), false};
                    if (!folder_item_visible(visibility, include_hidden))
                        return false;
                    if (host_matches(options.query, path, info, attributes))
                        publish({path});
                }
                catch (const std::runtime_error&) {
                    // A malformed companion or resource must not hide an
                    // ordinary item from a name/date search.
                    auto field = options.query.field;
                    if ((field == FindField::name || field == FindField::created || field == FindField::modified ||
                         field == FindField::lock) &&
                        host_matches(options.query, path, {}, attributes))
                        publish({path});
                }
                return true;
            };
            for (const auto& scope : roots) {
                if (stop || limited)
                    break;
                if (scope.image)
                    continue;
                WIN32_FILE_ATTRIBUTE_DATA attributes{};
                if (!GetFileAttributesExW(scope.path.c_str(), GetFileExInfoStandard, &attributes) ||
                    (attributes.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                    report_error(L"One or more search locations are unavailable.");
                    continue;
                }
                if (scope.include_item && !examine(scope.path, attributes))
                    continue;
                if (!(attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    continue;
                std::error_code issue;
                fs::recursive_directory_iterator it(scope.path, fs::directory_options::skip_permission_denied, issue),
                    end;
                while (it != end && !stop && !limited) {
                    if (!GetFileAttributesExW(it->path().c_str(), GetFileExInfoStandard, &attributes) ||
                        !examine(it->path(), attributes))
                        it.disable_recursion_pending();
                    it.increment(issue);
                    if (issue) {
                        report_error(L"Some folders could not be searched.");
                        break;
                    }
                }
                if (issue)
                    report_error(L"Some folders could not be searched.");
            }
        }
        catch (const std::exception&) {
            report_error(L"The host search stopped before all items could be examined.");
        }
        host_done = true;
    });
}

// Stop traversal cooperatively and join before the result/error storage can
// disappear.
Search::~Search()
{
    stop = true;
    if (worker.joinable())
        worker.join();
}

// Return an owned result snapshot so the UI can enumerate it after releasing
// the worker mutex.
std::vector<SearchMatch> Search::snapshot() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return results;
}

void Search::publish(SearchMatch match)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (!limited && published.insert(match.path).second) {
        results.push_back(std::move(match));
        limited = results.size() == 10000;
    }
}

void Search::report_error(const wchar_t* message)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (error.empty())
        error = message;
}

std::wstring Search::problem() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return error;
}

size_t Search::result_count() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return options.all_at_once && !finished() ? 0 : results.size();
}

bool Search::finished() const
{
    return host_done && (limited || (image_roots.empty() && catalog_stack.empty()));
}
}
