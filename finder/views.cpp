/*+===================================================================
  File:      views.cpp

  Summary:   Implements views support for the native ROMAW Finder.

  Classes:   Used: ViewEntry, ViewPreferences, std::wstring

  Functions: finder::decode_catalog_view, finder::encode_catalog_view, finder::decode_folder_view,
             finder::encode_folder_view, finder::folder_entry_less, finder::folder_item_visible, finder::clean_icon_layout,
             finder::selected_item_after

  Origin:    Existing repository source in executor/src/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "finder/views.h"
#include <windows.h>
#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <commctrl.h>

namespace finder {
int selected_item_after(HWND list, size_t model_count, int previous)
{
    wchar_t type[32]{};
    if (!IsWindow(list) || !model_count || !GetClassNameW(list, type, 32) || _wcsicmp(type, WC_LISTVIEWW))
        return -1;
    int count = ListView_GetItemCount(list);
    int next = ListView_GetNextItem(list, previous, LVNI_SELECTED);
    return next > previous && next >= 0 && next < count && size_t(next) < model_count ? next : -1;
}

bool folder_item_visible(const EntryVisibility& entry, bool show_hidden)
{
    if (show_hidden)
        return true;
    if (entry.metadata || (entry.finder_flags & 0x4000) ||
        (entry.host_attributes != INVALID_FILE_ATTRIBUTES && (entry.host_attributes & FILE_ATTRIBUTE_HIDDEN)))
        return false;
    // Classic Desktop files use this exact type/creator pair. An actual
    // Finder application (FNDR/MACS, for example) must remain visible.
    return entry.directory || entry.type != 0x464e4452 || entry.creator != 0x4552494b;
}

// DInfo view selectors follow the classic kBy*View order in Apple AERegistry.h. Match the selector exactly;
// bit tests would confuse By Date (3) with By Name (2), as in older HFVExplorer code.
ViewPreferences decode_catalog_view(uint16_t value)
{
    unsigned selector = value >> 8;
    if (selector == 1)
        return {(value & 0x40) ? FolderView::small_icons : FolderView::icons, FolderSort::name, false};
    static const FolderSort sorts[] = {FolderSort::name,     FolderSort::date,  FolderSort::size,   FolderSort::kind,
                                       FolderSort::comments, FolderSort::label, FolderSort::version};
    if (selector >= 2 && selector <= 8)
        return {FolderView::list, sorts[selector - 2], false};
    return {};
}

uint16_t encode_catalog_view(const ViewPreferences& preferences)
{
    if (preferences.view != FolderView::list)
        return preferences.view == FolderView::small_icons ? 0x140 : 0x100;
    switch (preferences.sort) {
    case FolderSort::date:
        return 0x300;
    case FolderSort::size:
        return 0x400;
    case FolderSort::kind:
        return 0x500;
    case FolderSort::comments:
        return 0x600;
    case FolderSort::label:
        return 0x700;
    case FolderSort::version:
        return 0x800;
    default:
        return 0x200;
    }
}

ViewPreferences decode_folder_view(const std::wstring& text)
{
    if (text.size() != 5 || text[0] < L'0' || text[0] > L'2' || text[1] != L',' || text[2] < L'0' || text[2] > L'8' ||
        text[3] != L',' || (text[4] != L'0' && text[4] != L'1'))
        return {};
    return {FolderView(text[0] - L'0'), FolderSort(text[2] - L'0'), text[4] == L'1'};
}

std::wstring encode_folder_view(const ViewPreferences& preferences)
{
    return std::to_wstring(unsigned(preferences.view)) + L"," + std::to_wstring(unsigned(preferences.sort)) + L"," +
           (preferences.descending ? L"1" : L"0");
}

namespace {
int compare_text(const std::wstring& left, const std::wstring& right)
{
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) - CSTR_EQUAL;
}

template <typename T> int compare_number(T left, T right)
{
    return left < right ? -1 : left > right ? 1 : 0;
}
}

bool folder_entry_less(const ViewEntry& left, const ViewEntry& right, FolderSort column, bool descending)
{
    int order = 0;
    switch (column) {
    case FolderSort::name:
        order = compare_text(left.name, right.name);
        break;
    case FolderSort::date:
        order = compare_number(left.modified, right.modified);
        break;
    case FolderSort::size:
        order = compare_number(left.size, right.size);
        break;
    case FolderSort::kind:
        order = compare_text(left.kind, right.kind);
        break;
    case FolderSort::comments:
        order = compare_text(left.comments, right.comments);
        break;
    case FolderSort::label:
        order = compare_number(left.label, right.label);
        break;
    case FolderSort::version:
        order = compare_number(left.version_number, right.version_number);
        break;
    case FolderSort::type:
        order = compare_text(left.type, right.type);
        break;
    case FolderSort::creator:
        order = compare_text(left.creator, right.creator);
        break;
    }
    if (order)
        return descending ? order > 0 : order < 0;
    order = compare_text(left.name, right.name);
    if (!order)
        order = CompareStringOrdinal(left.name.c_str(), -1, right.name.c_str(), -1, FALSE) - CSTR_EQUAL;
    return order < 0;
}

std::vector<IconPosition> clean_icon_layout(const std::vector<IconCleanupEntry>& entries, IconCleanup mode,
                                            int viewport_width, int cell_width, int cell_height)
{
    std::vector<IconPosition> result;
    for (const auto& entry : entries)
        result.push_back(entry.position);
    if (cell_width <= 0 || cell_height <= 0)
        return result;
    constexpr int left = 16, top = 8, icon_width = 32;
    // Finder 7 CODE 49:014a snaps relative to its layout origin, with
    // half-cell ties staying on the lower grid line. Use wide arithmetic for
    // scrolled coordinates; the native frontend uses a rectangular grid.
    auto floor_divide = [](int64_t value, int64_t step) { return value / step - (value < 0 && value % step != 0); };
    auto snap = [&](int value, int origin, int step) {
        int64_t delta = int64_t(value) - origin;
        int64_t cell = floor_divide(delta, step);
        if (delta - cell * step > step / 2)
            ++cell;
        return std::max<int64_t>(0, cell);
    };
    // Match refresh's cell extent, including the image's inset inside its
    // list-view cell. Fitting only the 32-pixel artwork clips the final column.
    int64_t columns = std::max<int64_t>(1, (int64_t(viewport_width) - left - cell_width + 12) / cell_width + 1);
    if (mode == IconCleanup::grid)
        for (const auto& entry : entries)
            columns = std::max(columns, snap(entry.position.x, left, cell_width) + 1);
    std::set<std::pair<int64_t, int64_t>> occupied;
    // The cell map handles icons. Label spans also cover the text extending
    // past a cell, without reserving extra space beside short names.
    std::map<int64_t, std::vector<std::pair<int64_t, int64_t>>> label_spans;
    auto footprint = [&](size_t i, int64_t x) {
        int64_t width = std::max(icon_width, entries[i].label_width);
        int64_t start = x + icon_width / 2 - width / 2;
        return std::pair{start, start + width};
    };
    std::vector<size_t> order;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].fixed) {
            order.push_back(i);
            continue;
        }
        // An anchor between grid lines can overlap two cells on each axis.
        int64_t x = int64_t(entries[i].position.x) - left, y = int64_t(entries[i].position.y) - top;
        int64_t first_column = std::max<int64_t>(0, floor_divide(x, cell_width));
        int64_t last_column = std::max<int64_t>(0, floor_divide(x + cell_width - 1, cell_width));
        for (int64_t row = std::max<int64_t>(0, floor_divide(y, cell_height));
             row <= std::max<int64_t>(0, floor_divide(y + cell_height - 1, cell_height)); ++row) {
            label_spans[row].push_back(footprint(i, entries[i].position.x));
            for (int64_t column = first_column; column <= last_column; ++column)
                occupied.insert({column, row});
        }
    }
    if (mode == IconCleanup::name)
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            int order = compare_text(entries[a].name, entries[b].name);
            if (!order)
                order =
                    CompareStringOrdinal(entries[a].name.c_str(), -1, entries[b].name.c_str(), -1, FALSE) - CSTR_EQUAL;
            return order < 0;
        });
    if (order.empty())
        return result;
    // Finder 7 lets the first label set the left inset for the sorted layout.
    // Keep that origin across rows, with extra inset if a later leading label needs it.
    auto leading_x = [&](size_t i) {
        return std::max<int64_t>(left, std::min(entries[i].label_width, viewport_width) / 2 - icon_width / 2 + 1);
    };
    int64_t name_left = mode == IconCleanup::name ? leading_x(order.front()) : left;
    int64_t column = 0, row = 0, row_left = name_left;
    for (size_t i : order) {
        if (mode == IconCleanup::grid) {
            column = snap(entries[i].position.x, left, cell_width);
            row = snap(entries[i].position.y, top, cell_height);
        }
        int64_t x;
        for (;;) {
            if (!column && mode == IconCleanup::name)
                row_left = std::max(name_left, leading_x(i));
            x = row_left + column * cell_width;
            auto span = footprint(i, x);
            bool past_edge = mode == IconCleanup::name && column && span.second > int64_t(viewport_width) - 16;
            auto& row_spans = label_spans[row];
            bool overlap = std::any_of(row_spans.begin(), row_spans.end(), [&](const auto& placed) {
                return span.first < placed.second + 4 && placed.first < span.second + 4;
            });
            if (!past_edge && !overlap && !occupied.count({column, row})) {
                row_spans.push_back(span);
                break;
            }
            if (++column >= columns || past_edge) {
                column = 0;
                ++row;
            }
        }
        occupied.insert({column, row});
        result[i] = {int(std::min<int64_t>(std::numeric_limits<int>::max(), x)),
                     int(std::min<int64_t>(std::numeric_limits<int>::max(), top + row * cell_height))};
        if (mode == IconCleanup::name && ++column >= columns) {
            column = 0;
            ++row;
        }
    }
    return result;
}
}
