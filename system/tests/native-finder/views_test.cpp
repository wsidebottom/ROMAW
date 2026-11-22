/*+===================================================================
  File:      views_test.cpp

  Summary:   Exercise native presentation settings and the actual sort keys independently of UI controls; all
             comparisons use the Windows Unicode ordinal API.

  Classes:   Used: EntryVisibility, ViewEntry, ViewPreferences, std::vector, std::wstring

  Functions: main

  Origin:    Existing repository source in executor/tests/native-finder; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

// Exercise native presentation settings and the actual sort keys independently
// of UI controls; all comparisons use the Windows Unicode ordinal API.
#include "finder/views.h"
#include <windows.h>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

using namespace finder;

int main()
{
    EntryVisibility desktop_file;
    desktop_file.type = 0x464e4452;    // FNDR
    desktop_file.creator = 0x4552494b; // ERIK
    assert(!folder_item_visible(desktop_file, false));
    assert(folder_item_visible(desktop_file, true));
    desktop_file.creator = 0x4d414353; // A real Finder application, FNDR/MACS.
    assert(folder_item_visible(desktop_file, false));
    desktop_file.creator = 0x4552494b;
    desktop_file.type = 0x4150504c;
    assert(folder_item_visible(desktop_file, false));
    desktop_file.type = 0x464e4452;
    desktop_file.directory = true;
    assert(folder_item_visible(desktop_file, false));
    EntryVisibility ordinary;
    assert(folder_item_visible(ordinary, false));
    ordinary.finder_flags = 0x4000;
    assert(!folder_item_visible(ordinary, false) && folder_item_visible(ordinary, true));
    ordinary.finder_flags = 0x0400; // A custom icon is not invisibility.
    assert(folder_item_visible(ordinary, false));
    ordinary.host_attributes = FILE_ATTRIBUTE_HIDDEN;
    assert(!folder_item_visible(ordinary, false) && folder_item_visible(ordinary, true));
    ordinary.host_attributes = INVALID_FILE_ATTRIBUTES;
    assert(folder_item_visible(ordinary, false));
    ordinary.metadata = true;
    assert(!folder_item_visible(ordinary, false) && folder_item_visible(ordinary, true));
    for (int mode = 0; mode < 3; ++mode)
        for (unsigned column = 0; column < folder_column_count; ++column)
            for (bool reverse : {false, true}) {
                ViewPreferences original{FolderView(mode), FolderSort(column), reverse};
                auto restored = decode_folder_view(encode_folder_view(original));
                assert(restored.view == original.view && restored.sort == original.sort &&
                       restored.descending == original.descending);
            }
    for (const auto& invalid : {L"", L"3,0,0", L"0,9,0", L"0,0,2", L"0,0,0trailing", L" 0,0,0", L"-1,0,0"}) {
        auto defaults = decode_folder_view(invalid);
        assert(defaults.view == FolderView::icons && defaults.sort == FolderSort::name && !defaults.descending);
    }
    assert(decode_folder_view(std::wstring(1000000, L'0')).view == FolderView::icons);
    assert(decode_catalog_view(0x100).view == FolderView::icons);
    assert(decode_catalog_view(0x140).view == FolderView::small_icons);
    const FolderSort catalog_sorts[] = {FolderSort::name,     FolderSort::date,  FolderSort::size,   FolderSort::kind,
                                        FolderSort::comments, FolderSort::label, FolderSort::version};
    for (unsigned selector = 2; selector <= 8; ++selector) {
        auto restored = decode_catalog_view(uint16_t(selector << 8 | 0x25));
        assert(restored.view == FolderView::list && restored.sort == catalog_sorts[selector - 2]);
        assert(encode_catalog_view(restored) == selector << 8);
    }
    assert(encode_catalog_view({FolderView::icons}) == 0x100);
    assert(encode_catalog_view({FolderView::small_icons}) == 0x140);
    // The new host-only sort keys have no classic DInfo selector. Keep the
    // valid By Name catalog view while preserving the exact native setting.
    assert(encode_catalog_view({FolderView::list, FolderSort::type}) == 0x200);
    assert(encode_catalog_view({FolderView::list, FolderSort::creator}) == 0x200);
    for (auto value : {0, 0x40, 0x900, 0xffff})
        assert(decode_catalog_view(uint16_t(value)).view == FolderView::icons);

    ViewEntry alpha, beta, unicode;
    alpha.name = L"Alpha";
    beta.name = L"beta";
    unicode.name = L"\u65e5\u672c";
    alpha.size = UINT64_C(5) << 30;
    beta.size = 100;
    alpha.modified = UINT64_C(0xffffffff) + 8;
    beta.modified = 17;
    alpha.kind = L"folder";
    beta.kind = L"document";
    alpha.comments = L"Zebra";
    beta.comments = L"apple";
    alpha.label = 7;
    beta.label = 1;
    alpha.version_number = 0x01008000; // classic 1.0 final
    beta.version_number = 0x01006001;  // classic 1.0 beta 1
    alpha.type = L"'TEXT'";
    beta.type = L"'APPL'";
    alpha.creator = L"'ttxt'";
    beta.creator = L"'TEST'";
    std::vector<ViewEntry> entries{alpha, beta, unicode};
    for (unsigned column = 0; column < folder_column_count; ++column)
        for (bool reverse : {false, true}) {
            auto less = [&](const ViewEntry& left, const ViewEntry& right) {
                return folder_entry_less(left, right, FolderSort(column), reverse);
            };
            for (const auto& entry : entries)
                assert(!less(entry, entry));
            std::sort(entries.begin(), entries.end(), less);
            assert(std::is_sorted(entries.begin(), entries.end(), less));
            for (size_t i = 0; i < entries.size(); ++i)
                for (size_t j = i + 1; j < entries.size(); ++j)
                    assert(!less(entries[j], entries[i]));
        }
    assert(folder_entry_less(alpha, beta, FolderSort::name, false));
    assert(folder_entry_less(beta, alpha, FolderSort::size, false));
    assert(folder_entry_less(alpha, beta, FolderSort::size, true));
    assert(folder_entry_less(beta, alpha, FolderSort::date, false));
    assert(folder_entry_less(beta, alpha, FolderSort::kind, false));
    assert(folder_entry_less(beta, alpha, FolderSort::comments, false));
    assert(folder_entry_less(beta, alpha, FolderSort::label, false));
    assert(folder_entry_less(beta, alpha, FolderSort::version, false));
    assert(folder_entry_less(beta, alpha, FolderSort::type, false));
    assert(folder_entry_less(alpha, beta, FolderSort::type, true));
    assert(folder_entry_less(beta, alpha, FolderSort::creator, false));
    assert(folder_entry_less(alpha, beta, FolderSort::creator, true));
    beta.size = alpha.size;
    assert(folder_entry_less(alpha, beta, FolderSort::size, true)); // stable ascending name tie-break
    std::vector<IconCleanupEntry> icons{{L"Zebra", {150, 110}}, {L"alpha", {20, 10}}, {L"Beta", {82, 65}}};
    auto positions = clean_icon_layout(icons, IconCleanup::name, 160);
    assert(positions[1].x == 16 && positions[1].y == 8);
    assert(positions[2].x == 80 && positions[2].y == 8);
    assert(positions[0].x == 16 && positions[0].y == 60);
    positions = clean_icon_layout(icons, IconCleanup::name, 128);
    assert(positions[2].x == 16 && positions[2].y == 60); // Reserve the whole cell at the right edge.
    positions = clean_icon_layout(icons, IconCleanup::grid, 128);
    assert(positions[0].x == 144 && positions[0].y == 112);
    assert(positions[1].x == 16 && positions[1].y == 8);
    assert(positions[2].x == 80 && positions[2].y == 60);
    icons = {{L"b", {48, 34}}, {L"a", {49, 35}}, {L"c", {-100, -100}}};
    positions = clean_icon_layout(icons, IconCleanup::grid, 128);
    assert(positions[0].x == 16 && positions[0].y == 8); // Half-cell ties round down.
    assert(positions[1].x == 80 && positions[1].y == 60);
    assert(positions[2].x == 80 && positions[2].y == 8); // Collisions advance to a free cell.
    icons = {{L"Volume", {16, 8}, true}, {L"b", {300, 300}}, {L"a", {300, 300}}};
    positions = clean_icon_layout(icons, IconCleanup::name, 20);
    assert(positions[0].x == 16 && positions[0].y == 8);
    assert(positions[2].x == 16 && positions[2].y == 60);
    assert(positions[1].x == 16 && positions[1].y == 112);
    positions = clean_icon_layout(icons, IconCleanup::grid, 128, 0, 52);
    assert(positions[1].x == 300 && positions[1].y == 300);

    // Measured from the mixed-name comparison with classic Finder: narrow
    // labels share adjacent cells and the long name needs a skipped cell.
    icons = {{L"G", {0, 0}, false, 8},
             {L"A", {0, 0}, false, 8},
             {L"Brief note", {0, 0}, false, 47},
             {L"C", {0, 0}, false, 8},
             {L"Detailed artwork reference", {0, 0}, false, 122},
             {L"E", {0, 0}, false, 8},
             {L"Final instructions", {0, 0}, false, 74}};
    positions = clean_icon_layout(icons, IconCleanup::name, 416);
    assert(positions[1].x == 16 && positions[1].y == 8);
    assert(positions[2].x == 80 && positions[2].y == 8);
    assert(positions[3].x == 144 && positions[3].y == 8);
    assert(positions[4].x == 272 && positions[4].y == 8);
    assert(positions[5].x == 16 && positions[5].y == 60);
    assert(positions[6].x == 80 && positions[6].y == 60);
    assert(positions[0].x == 144 && positions[0].y == 60);

    icons = {{L"Alpha documentation and notes", {0, 0}, false, 136},
             {L"Bravo installation instructions", {0, 0}, false, 134},
             {L"Delta Macintosh artwork files", {0, 0}, false, 132}};
    positions = clean_icon_layout(icons, IconCleanup::name, 416);
    assert(positions[0].x == 53 && positions[0].y == 8);
    assert(positions[1].x == 245 && positions[1].y == 8);
    assert(positions[2].x == 53 && positions[2].y == 60);

    icons = {{L"Fixed long label", {16, 8}, true, 124}, {L"Move", {80, 8}, false, 32}};
    positions = clean_icon_layout(icons, IconCleanup::grid, 416);
    assert(positions[0].x == 16 && positions[0].y == 8);
    assert(positions[1].x == 144 && positions[1].y == 8);
    std::cout << "PASS: visibility, views, sorts, Unicode, cleanup ordering, grid ties, collisions and anchors\n";
}
