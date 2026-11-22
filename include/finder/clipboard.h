/*+===================================================================
  File:      clipboard.h

  Summary:   Host clipboard snapshots and interoperable native Finder file transfers.

  Classes:   Declared: HostClipboard.

  Functions: Interface declarations; see below.

  Origin:    Executor native Finder Windows clipboard integration.

  Copyright and Legal notices: License reference: COPYING.
===================================================================+*/

#ifndef FINDER_CLIPBOARD_H
#define FINDER_CLIPBOARD_H
#include "finder/files.h"
#include <optional>

namespace finder {
// Snapshot strings and bitmap are owned. Clipboard handles are never retained.
struct HostClipboard {
    std::vector<fs::path> files;
    std::wstring text;
    bool move = false;
    DWORD sequence = 0;
    HBITMAP picture = nullptr;
    SIZE picture_size{};
    HostClipboard() = default;
    ~HostClipboard();
    HostClipboard(HostClipboard&& other) noexcept;
    HostClipboard& operator=(HostClipboard&& other) noexcept;
    HostClipboard(const HostClipboard&) = delete;
    HostClipboard& operator=(const HostClipboard&) = delete;
};

// Busy clipboard returns no snapshot; malformed formats never yield file paths.
std::optional<HostClipboard> read_host_clipboard(HWND owner);
// Publish standard CF_HDROP, Preferred DropEffect and Unicode path text.
// Physical companions are included for transfers to Windows applications.
void write_host_clipboard_files(HWND owner, const std::vector<fs::path>& paths, bool move);
bool host_clipboard_has_files();
// Classic indexed icon data is lossless between Finder windows, with a DIB
// representation for Windows applications.
Bytes read_host_clipboard_icon(HWND owner);
void write_host_clipboard_icon(HWND owner, const Bytes& family, const unsigned char* rgba);
bool host_clipboard_has_icon();
DWORD host_clipboard_sequence();
// Complete a successful cut only if it still refers to this exact clipboard.
// False means busy/failed; true means cleared or superseded, so retries may stop.
bool finish_host_clipboard_move(HWND owner, DWORD sequence);
}
#endif
