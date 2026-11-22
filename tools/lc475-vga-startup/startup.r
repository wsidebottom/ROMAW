/*+===================================================================
  File:      startup.r

  Summary:   Implements startup support for ROMAW build and development support.

  Classes:   None.

  Functions: No explicitly exported functions.

  Origin:    Existing repository source in tools/lc475-vga-startup; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "Processes.r"

resource 'SIZE' (-1) {
  reserved,
  ignoreSuspendResumeEvents,
  reserved,
  cannotBackground,
  needsActivateOnFGSwitch,
  backgroundAndForeground,
  dontGetFrontClicks,
  ignoreChildDiedEvents,
  is32BitCompatible,
  notHighLevelEventAware,
  onlyLocalHLEvents,
  notStationeryAware,
  dontUseTextEditServices,
  reserved,
  reserved,
  reserved,
  128 * 1024,
  64 * 1024
};
