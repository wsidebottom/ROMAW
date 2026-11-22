/*+===================================================================
  File:      views.h

  Summary:   Native folder visibility, bounded selection enumeration, sorting, icon cleanup and persistent presentation settings.

  Classes:   Declared: EntryVisibility, IconCleanupEntry, IconPosition, ViewEntry, ViewPreferences;
             Used: std::wstring, std::vector.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

// Native folder presentation; serialized settings never contain HWND values.
#ifndef FINDER_VIEWS_H
#define FINDER_VIEWS_H
#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

namespace finder {
enum class FolderView { icons, small_icons, list };
// Append native columns so existing serialized sort values remain stable.
enum class FolderSort { name, date, size, kind, comments, label, version, type, creator };
inline constexpr unsigned folder_column_count = unsigned(FolderSort::creator) + 1;

struct ViewPreferences {
    FolderView view = FolderView::icons;
    FolderSort sort = FolderSort::name;
    bool descending = false;
};

struct ViewEntry {
    std::wstring name, kind, comments, version, type, creator;
    uint64_t size = 0, modified = 0;
    uint32_t version_number = 0;
    unsigned label = 0;
};

// Presentation policy only. Copying and resource discovery retain hidden data.
struct EntryVisibility {
    uint32_t type = 0, creator = 0, host_attributes = 0;
    uint16_t finder_flags = 0;
    bool directory = false, metadata = false;
};

bool folder_item_visible(const EntryVisibility& entry, bool show_hidden);
// Bound enumeration to the current model and reject destroyed/non-list handles.
// A failed SendMessage returns zero, which must never restart an item loop.
int selected_item_after(HWND list, size_t model_count, int previous);

struct IconPosition {
    int x, y;
};

struct IconCleanupEntry {
    std::wstring name;
    IconPosition position;
    bool fixed = false;  // Desktop volumes and Trash retain their anchored positions.
    int label_width = 0; // Measured single-line label, including padding.
};

enum class IconCleanup { grid, name };
// Return positions in input order. Grid cleanup snaps near each current position;
// name cleanup fills rows alphabetically, leaving grid spaces for wide labels.
std::vector<IconPosition> clean_icon_layout(const std::vector<IconCleanupEntry>& entries, IconCleanup mode,
                                            int viewport_width, int cell_width = 64, int cell_height = 52);

// Classic DInfo.frView: high-byte selector, with bit 6 selecting small icons.
ViewPreferences decode_catalog_view(uint16_t value);
uint16_t encode_catalog_view(const ViewPreferences& preferences);
// Invalid or future settings use the default without accepting partial input.
ViewPreferences decode_folder_view(const std::wstring& text);
std::wstring encode_folder_view(const ViewPreferences& preferences);
// Unicode ordinal comparison keeps a deterministic name tie-break in every
// column. Numeric sizes/dates remain 64-bit; classic vers keys use host order.
bool folder_entry_less(const ViewEntry& left, const ViewEntry& right, FolderSort column, bool descending);
}
#endif
