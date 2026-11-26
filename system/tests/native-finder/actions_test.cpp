/*+===================================================================
  File:      actions_test.cpp

  Summary:   Exercise file/fork moves, Undo identity guards, shell links and search in temporary fixture trees; an
             optional environment-selected volume extends cross-volume coverage.

  Classes:   Used: History, Search, std::exception, std::ofstream, std::wstring

  Functions: main, rejects, write

  Origin:    Existing repository source in executor/tests/native-finder; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

// Exercise file/fork moves, Undo identity guards, shell links and search in
// temporary fixture trees; an optional environment-selected volume extends
// cross-volume coverage.

#include "finder/actions.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <objbase.h>
using namespace finder;
#undef assert
#define assert(value)                                               \
    do {                                                            \
        if (!(value)) {                                             \
            std::cerr << __LINE__ << ": " << #value << " failed\n"; \
            std::exit(2);                                           \
        }                                                           \
    } while (false)

void write(const fs::path& p, const char* text)
{
    std::ofstream out(p, std::ios::binary);
    out << text;
    assert(bool(out));
}

std::string text(const fs::path& path)
{
    auto bytes = read_bytes(path);
    return {bytes.begin(), bytes.end()};
}

template <class Function> void rejects(Function action)
{
    bool failed = false;
    try {
        action();
    }
    catch (const std::exception&) {
        failed = true;
    }
    assert(failed);
}

int main()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t temp[32768]{};
    assert(GetTempPathW(32768, temp));
    GUID guid;
    assert(SUCCEEDED(CoCreateGuid(&guid)));
    wchar_t name[64];
    StringFromGUID2(guid, name, 64);
    fs::path root = fs::path(temp) / (L"Finder-actions-" + std::wstring(name));
    fs::create_directories(root / L"Target");
    try {
        auto a = root / L"Original";
        auto b = root / L"Target/Original";
        write(a, "original");
        write(sidecar(a), "opaque fork bytes");
        fs::path rebased;
        assert(relocated_path(L"C:/Old/Folder/Child", {L"c:/old/folder", L"D:/Moved"}, rebased));
        assert(rebased == fs::path(L"D:/Moved/Child"));
        assert(relocated_path(L"C:/Old/Folder", {L"C:/Old/Folder", L"D:/Moved"}, rebased));
        assert(rebased == fs::path(L"D:/Moved"));
        assert(!relocated_path(L"C:/Old/Folder2", {L"C:/Old/Folder", L"D:/Moved"}, rebased));
        assert(!relocated_path(L"C:/Old/Folder", {{}, L"D:/Moved"}, rebased));
        History history;
        history.move({{a, b}}, L"Move");
        assert(!fs::exists(a) && fs::exists(sidecar(b)));
        assert(history.name() == L"Move");
        auto inverse = history.undo();
        assert(inverse.size() == 1 && inverse[0].source == b && inverse[0].target == a);
        assert(fs::exists(a) && fs::exists(sidecar(a)) && !history.available());
        auto original_id = host_item_identity(a);
        auto renamed = host_rename_target(a, L"Caf\u00e9");
        history.move({{a, renamed}}, L"Rename");
        assert(host_item_identity(renamed) == original_id && text(sidecar(renamed)) == "opaque fork bytes");
        assert(!fs::exists(a) && !fs::exists(sidecar(a)));
        history.undo();
        assert(host_item_identity(a) == original_id);
        for (auto invalid : {L"", L"..", L"Folder\\File", L"File:stream", L"Trailing. ", L"Bad?"})
            rejects([&] { host_rename_target(a, invalid); });
        assert(host_rename_target(root / L"Alias.lnk", L"New Alias") == root / L"New Alias.lnk");
        auto folder = root / L"Target";
        auto renamed_folder = host_rename_target(folder, L"New Folder");
        history.move({{folder, renamed_folder}}, L"Rename");
        assert(fs::is_directory(renamed_folder));
        history.undo();
        assert(fs::is_directory(folder));
        // A replacement move must undo both the new move and the displaced
        // logical item, including each item's independent Macintosh fork.
        auto incoming = root / L"Incoming", existing = root / L"Existing";
        fs::create_directories(incoming);
        fs::create_directories(existing);
        auto incoming_item = incoming / L"Document", existing_item = existing / L"Document";
        write(incoming_item, "new data");
        write(sidecar(incoming_item), "new fork");
        write(existing_item, "old data");
        write(sidecar(existing_item), "old fork");
        Copies transfers;
        auto replacement = transfers.start({incoming_item}, existing, TransferKind::move);
        auto move_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (replacement->state != CopyJob::waiting && replacement->state < CopyJob::complete &&
               std::chrono::steady_clock::now() < move_deadline)
            Sleep(1);
        assert(replacement->state == CopyJob::waiting);
        replacement->answer_conflict(ConflictAnswer::replace);
        while (replacement->state < CopyJob::complete && std::chrono::steady_clock::now() < move_deadline)
            Sleep(1);
        assert(replacement->state == CopyJob::complete);
        std::vector<Relocation> completed;
        for (const auto& change : replacement->moved)
            completed.push_back({change.first, change.second});
        history.remember_moves(completed, L"Replace Move", replacement);
        history.undo();
        assert(text(incoming_item) == "new data" && text(sidecar(incoming_item)) == "new fork");
        assert(text(existing_item) == "old data" && text(sidecar(existing_item)) == "old fork");
        // Background workers publish completed moves; the UI records Undo only
        // after terminal state, without executing the relocation a second time.
        relocate_item(a, b);
        history.remember_moves({{a, b}}, L"Background Move");
        assert(history.name() == L"Background Move");
        history.undo();
        assert(fs::exists(a) && fs::exists(sidecar(a)) && !fs::exists(b));

        assert(read_bytes(sidecar(a)) ==
               Bytes({'o', 'p', 'a', 'q', 'u', 'e', ' ', 'f', 'o', 'r', 'k', ' ', 'b', 'y', 't', 'e', 's'}));
        write(root / L"Blocked", "occupied");
        write(root / L"Second", "second");
        rejects([&] { history.move({{a, b}, {root / L"Second", root / L"Blocked"}}, L"Move"); });
        assert(fs::exists(a) && !fs::exists(b) && fs::exists(sidecar(a)));
        rejects([&] { relocate_item(root / L"Target", root / L"Target/Inside"); });
        auto created = root / L"Untitled Folder";
        fs::create_directory(created);
        history.created(created, L"New Folder");
        write(created / L"Keep", "keep");
        rejects([&] { history.undo(); });
        assert(fs::exists(created / L"Keep"));
        fs::remove(created / L"Keep");
        history.undo();
        assert(!fs::exists(created));
        auto alias = root / L"Original alias.lnk";
        create_shortcut(a, alias);
        assert(fs::equivalent(shortcut_target(alias), a));
        auto chained = root / L"Chained alias.lnk";
        create_shortcut(alias, chained);
        assert(fs::equivalent(shortcut_target(chained), a));
        auto command_link = root / L"Application command.lnk";
        create_shortcut(a, command_link, L"\"Macintosh app with spaces\"", a);
        assert(fs::is_regular_file(command_link));
        assert(shortcut_target(command_link).empty());
        history.created(alias, L"Make Alias");
        history.undo();
        assert(!fs::exists(alias));
        create_shortcut(a, alias);
        history.created(alias, L"Make Alias");
        write(alias, "edited alias");
        rejects([&] { history.undo(); });
        assert(fs::exists(alias));
        history.clear();
        history.move({{a, b}}, L"Move");
        write(a, "new original");
        rejects([&] { history.undo(); });
        assert(fs::exists(a) && fs::exists(b));
        fs::remove(a);
        history.undo();
        history.move({{a, root / L"ORIGINAL"}}, L"Rename");
        history.undo();
        assert(fs::exists(a));
        fs::create_directories(root / L"Nested/Deep");
        write(root / L"Nested/Deep/Needle \u65e5\u672c", "found");
        write(root / L"._Needle", "hidden");
        write(root / L"Hidden Needle", "host hidden");
        assert(SetFileAttributesW((root / L"Hidden Needle").c_str(), FILE_ATTRIBUTE_HIDDEN));
        fs::create_directories(root / L".AppleDouble");
        write(root / L".AppleDouble/Needle metadata", "metadata");
        auto link = root / L"Directory link";
        if (CreateSymbolicLinkW(link.c_str(), (root / L"Nested").c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | 2)) {
            rejects([&] { relocate_item(link, root / L"Target/Linked"); });
            assert(fs::exists(root / L"Nested/Deep/Needle \u65e5\u672c"));
        }
        auto wait_for_search = [](Search& search) {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!search.finished() && std::chrono::steady_clock::now() < deadline)
                Sleep(1);
            assert(search.finished() && search.problem().empty());
            std::vector<fs::path> found;
            for (const auto& match : search.snapshot())
                found.push_back(match.path);
            return found;
        };
        FindOptions options{{FindField::name, FindRelation::contains, L"nEeDlE"}};
        Search search({{root}}, options);
        auto found = wait_for_search(search);
        auto needle = root / L"Nested/Deep/Needle \u65e5\u672c";
        assert(found == std::vector<fs::path>{needle});
        Search inclusive(search.roots, search.options, true);
        auto hidden_found = wait_for_search(inclusive);
        assert(hidden_found.size() == 4);
        assert(std::find(hidden_found.begin(), hidden_found.end(), root / L"._Needle") != hidden_found.end());
        assert(std::find(hidden_found.begin(), hidden_found.end(), root / L"Hidden Needle") != hidden_found.end());
        assert(std::find(hidden_found.begin(), hidden_found.end(), root / L".AppleDouble/Needle metadata") !=
               hidden_found.end());
        assert(name_matches(L"Document \u65e5\u672c", L"\u65e5\u672c") && !name_matches(L"Document", L""));
        set_comment(needle, L"Original revision");
        set_label(needle, 3);
        assert(SetFileAttributesW(needle.c_str(), FILE_ATTRIBUTE_READONLY));
        FindOptions expanded;
        expanded.expanded = true;
        expanded.scope = FindScope::selection;
        expanded.all_at_once = true;
        for (auto query : {FindQuery{FindField::name, FindRelation::starts, L"needle"},
                           FindQuery{FindField::name, FindRelation::ends, L"\u65e5\u672c"},
                           FindQuery{FindField::size, FindRelation::greater, L"4"},
                           FindQuery{FindField::kind, FindRelation::equal, L"", unsigned(FindKind::document)},
                           FindQuery{FindField::label, FindRelation::equal, L"", 3},
                           FindQuery{FindField::comments, FindRelation::contains, L"REVISION"},
                           FindQuery{FindField::lock, FindRelation::equal, L"", 1}}) {
            assert(!query.validate());
            expanded.query = query;
            Search selected({{needle, 0, 2, 0, {}, true}, {root / L"Nested"}}, expanded);
            auto matches = wait_for_search(selected);
            assert(std::count(matches.begin(), matches.end(), needle) == 1);
        }
        FindQuery date{FindField::created, FindRelation::less, L"2100-02-28"};
        assert(!date.validate() && date.value == 21000228 && find_number_matches(date, 20241119));
        date.text = L"2100-02-29";
        assert(date.validate());
        FindQuery size{FindField::size, FindRelation::less, L"18446744073709551616"};
        assert(size.validate());
        assert(SetFileAttributesW(needle.c_str(), FILE_ATTRIBUTE_NORMAL));
        wchar_t cross_volume[32768]{};
        if (GetEnvironmentVariableW(L"FINDER_CROSS_VOLUME_TEST_ROOT", cross_volume, 32768)) {
            auto other = fs::path(cross_volume) / (L"Finder-cross-" + std::wstring(name));
            fs::create_directories(other);
            auto from = root / L"Cross-volume folder";
            fs::create_directories(from / L"Nested");
            write(from / L"Nested/File", "cross-volume data");
            write(sidecar(from), "cross-volume fork");
            History cross;
            cross.move({{from, other / L"Moved"}}, L"Move");
            assert(!fs::exists(from) && fs::exists(other / L"Moved/Nested/File") &&
                   fs::exists(sidecar(other / L"Moved")));
            cross.undo();
            assert(fs::exists(from / L"Nested/File") && fs::exists(sidecar(from)) && !fs::exists(other / L"Moved"));
            fs::remove_all(other);
        }
        fs::remove_all(root);
        std::cout
            << "PASS: file actions, nested Unicode and metadata Find, validation, visibility and selected-scope deduplication\n";
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << "\nFixture: " << root << "\n";
        return 2;
    }
    CoUninitialize();
}
