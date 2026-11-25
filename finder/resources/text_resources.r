/*+===================================================================
  File:      text_resources.r

  Summary:   Finder column and sort names consumed by the native desktop.

  Classes:   None.

  Functions: No explicitly exported functions.

  Origin:    Existing repository source in executor/resources/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "resource_types.r"

/* Names retain the order of classic Finder STR# 3000 entries 14 through 20. */
resource 'STR#' (3000, purgeable) {
    {
        "Name";
        "Date";
        "Size";
        "Kind";
        "Comments";
        "Label";
        "Version";
    }
};

/* Original Finder display credit: "Executor Finder \0xA9 2026". */
