/*+===================================================================
  File:      launcher.h

  Summary:   Declare Finder runtime launching and direct disk-image mounting.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef FINDER_LAUNCHER_H
#define FINDER_LAUNCHER_H
#include <stdint.h>
#include <wchar.h>

/* Host launch requests never contain emulated Macintosh pointers. */
enum FinderLaunchKind {
    FINDER_CLASSIC_APPLICATION = 1,
    FINDER_DESK_ACCESSORY = 2,
    FINDER_CONTROL_PANEL = 3,
    FINDER_CLASSIC_DOCUMENT = 4
};

typedef struct FinderImageMount {
    const wchar_t* source;
    int32_t mode; /* -1 read-only, 0 default policy, 1 explicitly read/write. */
} FinderImageMount;

typedef struct FinderLaunchRequest {
    // Initialize size to sizeof (FinderLaunchRequest); kind selects the launch mode.
    uint32_t size, kind;
    // Borrowed Unicode strings, copied into the command line before returning.
    const wchar_t* runtime;
    const wchar_t* file;
    const wchar_t* const* documents;
    uint32_t document_count;
    // Signed classic DRVR ID, used only for a desk-accessory launch.
    int32_t resource_id;
    const FinderImageMount* mounts;
    uint32_t mount_count;
} FinderLaunchRequest;

typedef struct FinderLaunchResult {
    // Initialize size before calling; zero-initialize reserved for future use.
    uint32_t size, error, process_id, reserved;
    uintptr_t process_handle; /* Caller owns this Windows handle on success. */
} FinderLaunchResult;

// Return a Win32 status. On success the caller owns process_handle and must
// CloseHandle it. Launch returns without waiting for the guest to exit.
uint32_t FinderLaunch(const FinderLaunchRequest* request, FinderLaunchResult* result);
// Recognize the image extensions supported by the mount picker, ignoring case.
int FinderIsDiskImage(const wchar_t* path);
// Mount in the initialized Finder process using the default image access policy.
// Returns a Macintosh OSErr; -128 is user cancellation. Details are optional.
int32_t FinderMountImage(const wchar_t* path, wchar_t* details, uint32_t capacity);
#endif
