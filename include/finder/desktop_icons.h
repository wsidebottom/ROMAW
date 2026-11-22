/*+===================================================================
  File:      desktop_icons.h

  Summary:   ifndef FINDER_DESKTOP_ICONS_H

  Classes:   Declared: DesktopIcons; Used: DesktopIcons, FinderIcon, MacFile, std::unique_ptr, std::vector, std::wstring

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef FINDER_DESKTOP_ICONS_H
#define FINDER_DESKTOP_ICONS_H
#include "finder/files.h"
#include "finder_metadata/icons.h"
#include <cstdint>
#include <memory>

namespace finder {
// Desktop DB/DF access and bundle discovery run without guest Toolbox state.
// Only configured Macintosh volumes are scanned or written; longest root wins.
class DesktopIcons {
  public:
    explicit DesktopIcons(std::vector<fs::path> roots);
    ~DesktopIcons();
    void scan(const fs::path& directory);
    void poll();
    bool lookup(const fs::path& path, const MacFile& file, FinderIcon& icon);
    uint64_t generation() const;
    // Deterministic fixture/shutdown synchronization; never used by the UI loop.
    void wait_idle();

  private:
    struct State;
    std::unique_ptr<State> state;
};

// Decode a folder's Icon<CR> family through native, escaped or SMB-safe names.
// AppleDouble-only and split resource forks do not require a data-fork file.
bool custom_folder_icon(const fs::path& folder, FinderIcon& icon);
void set_host_custom_icon(const fs::path& path, bool folder, const FinderIcon* icon);
// Includes data and companion modification times/sizes, including folder icons.
std::wstring icon_source_stamp(const fs::path& path, bool folder);
}
#endif
