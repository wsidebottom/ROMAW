/*+===================================================================
  File:      desktop_icons.cpp

  Summary:   Native Finder shares the classic Desktop Manager's B-tree/DF storage. The worker discovers bundles; the UI
             reads registered families by creator/type.

  Classes:   Declared: DesktopIcons::State, Job, Volume; Used: FinderIcon, Job, MacFile, Volume, std::array,
             std::atomic, std::condition_variable, std::error_code, std::lock_guard, std::map, std::mutex, std::set,
             std::thread, std::tuple, std::unique_lock, std::vector, std::wstring

  Functions: finder::DesktopIcons::DesktopIcons, finder::DesktopIcons::State::State, finder::DesktopIcons::State::queue,
             finder::DesktopIcons::State::register_file, finder::DesktopIcons::State::run,
             finder::DesktopIcons::State::stopped, finder::DesktopIcons::State::watch,
             finder::DesktopIcons::State::~State, finder::DesktopIcons::generation, finder::DesktopIcons::lookup,
             finder::DesktopIcons::poll, finder::DesktopIcons::scan, finder::DesktopIcons::wait_idle,
             finder::DesktopIcons::~DesktopIcons, finder::icon_source_stamp, finder::set_host_custom_icon.

  Origin:    Existing repository source in executor/src/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

// Native Finder shares the classic Desktop Manager's B-tree/DF storage. The
// worker discovers bundles; the UI reads registered families by creator/type.
#include "finder/desktop_icons.h"
#include "rsys/desktop_database.h"
#include "rsys/mac_storage.h"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <chrono>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <tuple>

namespace finder {
namespace {
using Key = std::tuple<fs::path, uint32_t, uint32_t>;
// Inside Macintosh Desktop Manager types differ from FinderIcon's field order.
constexpr unsigned desktop_types[] = {1, 4, 2, 3, 5, 6};

std::wstring stamp(const fs::path& path)
{
    std::error_code error;
    auto time = fs::last_write_time(path, error);
    if (error)
        return L"missing";
    auto size = fs::is_regular_file(path, error) ? fs::file_size(path, error) : 0;
    return std::to_wstring(time.time_since_epoch().count()) + L":" + std::to_wstring(size);
}

std::wstring database_stamp(const fs::path& root)
{
    return stamp(root / L"Desktop DB") + L"/" + stamp(root / L"Desktop DF");
}

bool inside(const fs::path& root, const fs::path& path)
{
    auto r = root.lexically_normal(), p = path.lexically_normal();
    auto ri = r.begin(), pi = p.begin();
    for (; ri != r.end(); ++ri, ++pi)
        if (pi == p.end() || _wcsicmp(ri->c_str(), pi->c_str()))
            return false;
    return true;
}

std::vector<fs::path> volume_roots(const std::vector<fs::path>& paths)
{
    std::vector<fs::path> roots;
    for (const auto& path : paths) {
        if (path.empty())
            continue;
        auto root = fs::absolute(path).lexically_normal();
        if (std::none_of(roots.begin(), roots.end(),
                         [&](const fs::path& other) { return inside(other, root) && inside(root, other); }))
            roots.push_back(std::move(root));
    }
    return roots;
}

void put32(unsigned char* p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = value >> (24 - i * 8);
}

unsigned char* component(FinderIcon& icon, unsigned index)
{
    unsigned char* data[] = {icon.mono, icon.small, icon.color4, icon.color8, icon.small4, icon.small8};
    return data[index];
}

int database(const fs::path& root, desktop_operation op, unsigned char* key, void* data, size_t& size, uint32_t& tag)
{
    return romaw_desktop_database_wide(root.c_str(), op, key, 0, data, &size, &tag);
}

std::array<unsigned char, 38> icon_key(uint32_t creator, uint32_t type, unsigned index)
{
    std::array<unsigned char, 38> key{};
    key[0] = 11;
    key[1] = 1;
    put32(key.data() + 2, creator);
    put32(key.data() + 6, type);
    key[10] = desktop_types[index];
    return key;
}

// A failed or partial bitmap never becomes an apparently valid icon family.
FinderIcon read_family(const fs::path& root, uint32_t creator, uint32_t type)
{
    FinderIcon icon{};
    std::error_code error;
    if (!fs::exists(root / L"Desktop DB", error) || !fs::exists(root / L"Desktop DF", error))
        return icon;
    for (unsigned i = 0; i < 6; ++i) {
        auto key = icon_key(creator, type, i);
        std::array<unsigned char, 4500> bytes{};
        size_t size = bytes.size();
        uint32_t tag = 0;
        if (!database(root, desktop_get, key.data(), bytes.data(), size, tag) && size == finder_icon_sizes[i]) {
            memcpy(component(icon, i), bytes.data(), size);
            icon.present |= 1u << i;
        }
    }
    return icon;
}

// Avoid appending identical bitmaps on each launch or folder refresh. The
// underlying writer preserves all other icons, APPL records and comments.
void write_family(const fs::path& root, uint32_t creator, uint32_t type, FinderIcon& icon)
{
    for (unsigned i = 0; i < 6; ++i)
        if (icon.present & (1u << i)) {
            auto key = icon_key(creator, type, i);
            std::array<unsigned char, 4500> old{};
            size_t size = old.size();
            uint32_t tag = 0;
            int error = database(root, desktop_get, key.data(), old.data(), size, tag);
            if (!error && size == finder_icon_sizes[i] && !memcmp(old.data(), component(icon, i), size))
                continue;
            if (!error && size != finder_icon_sizes[i])
                database(root, desktop_remove, key.data(), nullptr, size, tag);
            size = finder_icon_sizes[i];
            database(root, desktop_put, key.data(), component(icon, i), size, tag);
        }
}
}

namespace {
// Keep discovery and cache signatures on exactly the same set of host aliases.
std::array<fs::path, 3> folder_icon_paths(const fs::path& folder)
{
    return {folder / MAC_STORAGE_CUSTOM_ICON_NAME, folder / MAC_STORAGE_ESCAPED_ICON_NAME,
            folder / (std::wstring(L"Icon") + wchar_t(MAC_STORAGE_SFM_ICON_CHARACTER))};
}
}

bool custom_folder_icon(const fs::path& folder, FinderIcon& icon)
{
    for (const auto& path : folder_icon_paths(folder)) {
        try {
            auto file = mac_file(path);
            FinderIcon candidate{};
            if (!file.fork.empty() && finder_parse_custom_icon(file.fork.data(), file.fork.size(), &candidate)) {
                icon = candidate;
                return true;
            }
        }
        catch (const std::exception&) {
            // A malformed or inaccessible alias cannot hide a valid later one.
        }
    }
    return false;
}

static void edit_host_icon_fork(const fs::path& path, const FinderIcon* icon, bool carrier)
{
    auto file = mac_file(path);
    unsigned char* edited = nullptr;
    size_t length = 0;
    if (!finder_edit_custom_icon(file.fork.data(), file.fork.size(), icon, &edited, &length))
        throw std::runtime_error("The custom icon could not be saved in this resource fork.");
    std::unique_ptr<unsigned char, decltype(&free)> bytes(edited, free);
    Bytes fork(bytes.get(), bytes.get() + length);
    write_icon_metadata(path, &fork, icon != nullptr, carrier);
}

void set_host_custom_icon(const fs::path& path, bool folder, const FinderIcon* icon)
{
    if (!folder) {
        edit_host_icon_fork(path, icon, false);
        return;
    }
    auto candidates = folder_icon_paths(path);
    if (icon) {
        // Prefer the existing carrier, including sidecar-only host layouts.
        auto found = std::find_if(candidates.begin(), candidates.end(), [](const auto& candidate) {
            return fs::exists(candidate) || !mac_companions(candidate).empty();
        });
        auto carrier = found != candidates.end() ? *found : candidates[1];
        bool created = !fs::exists(carrier);
        if (created) {
            HANDLE file =
                CreateFileW(carrier.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_HIDDEN, nullptr);
            if (file == INVALID_HANDLE_VALUE)
                throw std::runtime_error("The folder's custom icon file could not be created.");
            CloseHandle(file);
        }
        try {
            edit_host_icon_fork(carrier, icon, true);
        }
        catch (...) {
            if (created)
                DeleteFileW(carrier.c_str());
            throw;
        }
    }
    else
        for (const auto& carrier : candidates) {
            auto file = mac_file(carrier);
            FinderIcon current{};
            if (finder_parse_custom_icon(file.fork.data(), file.fork.size(), &current))
                edit_host_icon_fork(carrier, nullptr, true);
        }
    auto folder_info = mac_file(path);
    FinderIcon current{};
    if (!icon && (folder_info.flags & 0x400) &&
        finder_parse_custom_icon(folder_info.fork.data(), folder_info.fork.size(), &current))
        edit_host_icon_fork(path, nullptr, false);
    else
        write_icon_metadata(path, nullptr, icon != nullptr);
}

std::wstring icon_source_stamp(const fs::path& path, bool folder)
{
    try {
        std::wstring value = stamp(path);
        for (const auto& companion : mac_companions(path))
            value += L"/" + stamp(companion);
        if (folder)
            for (const auto& icon : folder_icon_paths(path)) {
                value += L"/" + stamp(icon);
                for (const auto& companion : mac_companions(icon))
                    value += L"/" + stamp(companion);
            }
        return value;
    }
    catch (const fs::filesystem_error&) {
        return stamp(path) + L"/unavailable";
    }
}

struct DesktopIcons::State {
    struct Volume {
        std::wstring disk_stamp;
        bool known = false, running = false;
        std::map<fs::path, std::wstring> applications;
    };

    struct Job {
        fs::path root, directory;
        bool recursive;
    };

    std::vector<fs::path> roots;
    std::map<fs::path, Volume> volumes;
    std::map<Key, FinderIcon> cache;
    std::deque<Job> jobs;
    std::set<fs::path> pending;
    std::mutex mutex;
    std::condition_variable changed;
    std::atomic<uint64_t> generation{1};
    bool stopping = false, busy = false;
    bool discovered = false;
    std::chrono::steady_clock::time_point last_publish{};
    std::thread worker;

    explicit State(std::vector<fs::path> paths) : roots(std::move(paths)), worker([this] { run(); })
    {
    }

    ~State()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        changed.notify_all();
        worker.join();
    }

    void queue(const fs::path& root, const fs::path& directory, bool recursive)
    {
        if (pending.insert(directory).second) {
            jobs.push_back({root, directory, recursive});
            changed.notify_all();
        }
    }

    void watch(const fs::path& root)
    {
        auto& volume = volumes[root];
        if (volume.running)
            return;
        auto current = database_stamp(root);
        if (!volume.known || current != volume.disk_stamp) {
            // Existing populated databases can be used immediately. Only an
            // empty/missing index needs a recursive volume rebuild.
            unsigned char prefix[38] = {1, 1}, record[10];
            size_t size = sizeof record;
            uint32_t tag = 0;
            bool populated = !romaw_desktop_database_wide(root.c_str(), desktop_index, prefix, 1, record, &size, &tag);
            volume.known = true;
            volume.disk_stamp = current;
            if (!populated)
                volume.applications.clear();
            cache.clear();
            ++generation;
            queue(root, root, !populated);
        }
    }

    // Malformed or inaccessible individual forks cannot abort volume discovery.
    void register_file(const fs::path& root, const fs::path& path, bool writable)
    {
        std::error_code error;
        if (mac_companions(path).empty()) {
            std::lock_guard<std::mutex> lock(mutex);
            volumes[root].applications.erase(path);
            return;
        }
        auto signature = icon_source_stamp(path, false);
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto found = volumes[root].applications.find(path);
            if (found != volumes[root].applications.end() && found->second == signature)
                return;
        }
        auto file = mac_file(path);
        uint32_t creator = file.creator;
        auto bundle = mac_resource(file, 0x424e444c);
        if (!creator && bundle.size() >= 4)
            creator = uint32_t(bundle[0]) << 24 | uint32_t(bundle[1]) << 16 | uint32_t(bundle[2]) << 8 | bundle[3];
        if (bundle.empty())
            return;
        if (creator && !file.fork.empty()) {
            std::vector<uint32_t> types(65536);
            size_t count =
                finder_bundle_file_types(file.fork.data(), file.fork.size(), creator, types.data(), types.size());
            for (size_t t = 0; t < count; ++t) {
                if (stopped())
                    return;
                FinderIcon icon{};
                if (finder_parse_icon(file.fork.data(), file.fork.size(), creator, types[t], &icon)) {
                    if (writable)
                        write_family(root, creator, types[t], icon);
                    std::lock_guard<std::mutex> lock(mutex);
                    auto& previous = cache[{root, creator, types[t]}];
                    discovered |= memcmp(&previous, &icon, sizeof icon) != 0;
                    previous = icon;
                    auto now = std::chrono::steady_clock::now();
                    if (discovered && now - last_publish >= std::chrono::seconds(1)) {
                        ++generation;
                        discovered = false;
                        last_publish = now;
                    }
                }
            }
        }
        std::lock_guard<std::mutex> lock(mutex);
        volumes[root].applications[path] = signature;
    }

    bool stopped()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return stopping;
    }

    void run()
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (true) {
            changed.wait(lock, [&] { return stopping || !jobs.empty(); });
            if (stopping)
                return;
            auto job = jobs.front();
            jobs.pop_front();
            busy = true;
            discovered = false;
            volumes[job.root].running = true;
            lock.unlock();
            try {
                size_t existed = 0;
                uint32_t tag = 0;
                bool writable = !database(job.root, desktop_open, nullptr, nullptr, existed, tag);
                if (writable)
                    try {
                        mark_desktop_database_files(job.root);
                    }
                    catch (...) {
                    }
                if (writable && !existed) {
                    std::lock_guard<std::mutex> guard(mutex);
                    volumes[job.root].applications.clear();
                }
                std::error_code error;
                auto visit = [&](const fs::directory_entry& entry) {
                    if (!entry.is_regular_file(error) || hidden_metadata(entry.path()))
                        return;
                    try {
                        register_file(job.root, entry.path(), writable);
                    }
                    catch (...) {
                        std::lock_guard<std::mutex> guard(mutex);
                        volumes[job.root].applications[entry.path()] = icon_source_stamp(entry.path(), false);
                    }
                };
                if (job.recursive) {
                    fs::recursive_directory_iterator it(job.directory, fs::directory_options::skip_permission_denied,
                                                        error),
                        end;
                    while (it != end && !stopped()) {
                        bool mapped_volume = std::any_of(roots.begin(), roots.end(), [&](const fs::path& root) {
                            return root != job.root && inside(job.root, root) && inside(root, it->path());
                        });
                        if (mapped_volume || hidden_metadata(it->path()) ||
                            (GetFileAttributesW(it->path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT))
                            it.disable_recursion_pending();
                        else
                            visit(*it);
                        it.increment(error);
                        if (error)
                            error.clear();
                    }
                }
                else
                    for (fs::directory_iterator it(job.directory, fs::directory_options::skip_permission_denied, error),
                         end;
                         it != end && !stopped(); it.increment(error)) {
                        visit(*it);
                        if (error)
                            error.clear();
                    }
            }
            catch (...) {
            }
            lock.lock();
            volumes[job.root].disk_stamp = database_stamp(job.root);
            volumes[job.root].running = false;
            pending.erase(job.directory);
            busy = false;
            if (discovered)
                ++generation;
            changed.notify_all();
        }
    }
};

DesktopIcons::DesktopIcons(std::vector<fs::path> roots) : state(std::make_unique<State>(volume_roots(roots)))
{
}

DesktopIcons::~DesktopIcons() = default;

void DesktopIcons::scan(const fs::path& directory)
{
    std::lock_guard<std::mutex> lock(state->mutex);
    fs::path best;
    for (const auto& root : state->roots)
        if (inside(root, directory) && root.native().size() > best.native().size())
            best = root;
    if (best.empty())
        return;
    state->watch(best);
    state->queue(best, directory, false);
}

void DesktopIcons::poll()
{
    std::lock_guard<std::mutex> lock(state->mutex);
    for (const auto& root : state->roots)
        if (state->volumes.count(root)) {
            state->watch(root);
            if (!state->volumes[root].running)
                for (auto app = state->volumes[root].applications.begin();
                     app != state->volumes[root].applications.end();) {
                    std::error_code error;
                    if (!fs::exists(app->first, error)) {
                        app = state->volumes[root].applications.erase(app);
                        ++state->generation;
                        continue;
                    }
                    if (icon_source_stamp(app->first, false) != app->second) {
                        state->queue(root, app->first.parent_path(), false);
                        ++state->generation;
                    }
                    ++app;
                }
        }
}

bool DesktopIcons::lookup(const fs::path& path, const MacFile& file, FinderIcon& icon)
{
    if (!file.creator || !file.type)
        return false;
    if (file.macbinary && !file.fork.empty() &&
        finder_parse_icon(file.fork.data(), file.fork.size(), file.creator, file.type, &icon))
        return true;
    auto roots = state->roots;
    std::stable_sort(roots.begin(), roots.end(), [&](const fs::path& a, const fs::path& b) {
        return inside(a, path) && (!inside(b, path) || a.native().size() > b.native().size());
    });
    for (const auto& root : roots) {
        Key key{root, file.creator, file.type};
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            auto found = state->cache.find(key);
            if (found != state->cache.end()) {
                if (found->second.present & 1) {
                    icon = found->second;
                    return true;
                }
                else
                    continue;
            }
        }
        auto found = read_family(root, file.creator, file.type);
        std::lock_guard<std::mutex> lock(state->mutex);
        auto inserted = state->cache.emplace(key, found);
        if (inserted.first->second.present & 1) {
            icon = inserted.first->second;
            return true;
        }
    }
    return false;
}

uint64_t DesktopIcons::generation() const
{
    return state->generation;
}

void DesktopIcons::wait_idle()
{
    std::unique_lock<std::mutex> lock(state->mutex);
    state->changed.wait(lock, [&] { return state->jobs.empty() && !state->busy; });
}
}
