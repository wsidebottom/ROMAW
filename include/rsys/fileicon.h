/*+===================================================================
  File:      fileicon.h

  Summary:   Read custom resource icons and bounded ICO/ICNS data forks through the File Manager.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef RSYS_FILEICON_H
#define RSYS_FILEICON_H
#include "FileMgr.h"
struct FinderIcon;
/* Copy icon resources without retaining handles from another resource map. */
int ROMlib_read_file_icon(FSSpec* spec, uint32 creator, uint32 type, boolean_t custom, struct FinderIcon* icon);
/* Optional outputs: a classic family, straight-alpha 32px RGBA, or both. */
int ROMlib_read_icon_data(FSSpec* spec, struct FinderIcon* icon, unsigned char* rgba);
#endif
