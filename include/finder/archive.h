/*+===================================================================
  File:      archive.h

  Summary:   Finder-owned archive extraction and classic destination selection.

  Classes:   ArchiveSource, ArchiveDestination.

  Functions: Interface declarations; see archive.cpp and archive_picker.cpp.

  Origin:    ROMAW native Finder archive integration.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#ifndef FINDER_ARCHIVE_H
#define FINDER_ARCHIVE_H
#include "finder/files.h"
#include <optional>

namespace finder {
struct ArchiveDestination {
    fs::path path; // Host folder, or the normal image catalog reservation key.
    int16_t volume = 0;
    uint32_t directory = 2;
};

struct ArchiveSource {
    fs::path path;
    int16_t volume = 0;
    uint32_t parent = 2, id = 0;
    std::string name; // Image catalog name in MacRoman.
};

bool archive_destination_allowed(const ArchiveDestination& destination);
std::optional<ArchiveDestination> pick_archive_destination(HWND owner, const ArchiveDestination& initial,
                                                           const std::vector<fs::path>& shortcuts);
// 0: cancel, 1: StuffIt 5, 2: ditto-compatible ZIP.
int pick_folder_archive_format(HWND owner);
std::shared_ptr<CopyJob> extract_archives(Copies& copies, const std::vector<ArchiveSource>& sources,
                                          const ArchiveDestination& destination);
}
#endif
