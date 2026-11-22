/*+===================================================================
  File:      device.h

  Summary:   Declares device support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#define RAMBASEDBIT (1 << 6)
#define DRIVEROPENBIT (1 << 5)

extern Handle ROMlib_host_disk_color_icon(Handle mono, int* depth);
extern OSErr ROMlib_driver_install(INTEGER refnum);
extern OSErr ROMlib_driver_remove(INTEGER refnum);
extern void ROMlib_device_init(void);
