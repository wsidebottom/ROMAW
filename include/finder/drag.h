/*+===================================================================
  File:      drag.h

  Summary:   UI-thread drag overlays, transient destination highlights and folder drop routing.

  Classes:   Declared: DragImage; Used: DragImage, std::vector

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef FINDER_DRAG_H
#define FINDER_DRAG_H
#include <windows.h>
#include <commctrl.h>
#include <vector>

namespace finder {
// UI-thread owner of a nonactivating, per-pixel-alpha drag overlay. It never
// saves/restores desktop pixels. Screen-space movement preserves the grab
// offset; an optional surface clips feedback and can keep test windows hidden.
class DragImage {
  public:
    DragImage() = default;
    ~DragImage();
    DragImage(const DragImage&) = delete;
    DragImage& operator=(const DragImage&) = delete;
    // Supplied artwork transfers ownership and uses a list-client origin.
    // The detached control fixture can still construct its own test artwork.
    bool begin(HWND list, int index, POINT grab, POINT position, HWND surface = nullptr, HBITMAP artwork = nullptr,
               POINT origin = {});
    void move(POINT position);
    HWND window_at(POINT position) const;
    void end();
#ifdef FINDER_TESTING
    // Borrowed handles for geometry/alpha/lifetime regression checks.
    HWND feedback_window() const
    {
        return overlay;
    }

    HBITMAP feedback_bitmap() const
    {
        return bitmap;
    }
#endif

  private:
    HWND overlay = nullptr;
    HWND surface = nullptr;
    HBITMAP bitmap = nullptr;
    POINT hotspot{};
    SIZE size{};
};

// Temporary drop feedback is independent of the user's actual selection.
// Refresh/destruction may clear just the affected list; cancellation clears all.
class DragHighlight {
  public:
    ~DragHighlight();
    DragHighlight() = default;
    DragHighlight(const DragHighlight&) = delete;
    DragHighlight& operator=(const DragHighlight&) = delete;
    void set(HWND list, int index);
    void clear(HWND list = nullptr);

  private:
    HWND target = nullptr;
    int item = -1;
};

// Option/Alt forces copying; Shift forces moving across volumes. Control is
// accepted as a host copy modifier by the caller. Option takes precedence.
bool drag_copies(bool same_volume, bool option, bool shift);
// Indices belong to their source folder, not to every list in the desktop.
bool drag_target_allowed(HWND source, const std::vector<int>& selected, HWND destination, int index, bool accepts);
// Resolve the nearest folder frame containing the hit window. Owned popups are
// separate drop surfaces, even when their owner is the desktop frame.
HWND drag_destination(HWND hit, const std::vector<HWND>& folder_windows);
}
#endif
