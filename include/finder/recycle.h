/*+===================================================================
  File:      recycle.h

  Summary:   Native Windows Recycle Bin operations for Finder items and their Macintosh companions.

  Classes:   None.

  Functions: Interface declarations; see the declarations below.

  Origin:    Executor Finder integration with Windows Shell recycle services.

  Copyright and Legal notices: License reference: COPYING.
===================================================================+*/

#ifndef FINDER_RECYCLE_H
#define FINDER_RECYCLE_H
#include "finder/files.h"

namespace finder {
// Windows chooses each source volume's bin and warns before any permanent
// fallback. Cancellation is normal; other failures throw. Includes sidecars.
void trash_items(HWND owner, const std::vector<fs::path>& paths);
// Aggregate native bin status; an unavailable status is conservatively full.
bool recycle_bin_full();
// Uses the native confirmation, progress and deletion routines for all bins.
void empty_recycle_bin(HWND owner);
// Open Windows' virtual bin (and its Restore commands), or its Properties.
void open_recycle_bin(HWND owner, bool properties = false);
}
#endif
