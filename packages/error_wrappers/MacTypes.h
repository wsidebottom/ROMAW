/*+===================================================================
  File:      MacTypes.h

  Summary:   Declares mac types support for error_wrappers package support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in packages/error_wrappers; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

struct foo {
    int elem;
};

typedef struct foo** Handle;

extern Handle NewHandle(int);
#define noErr 0
extern int MemError(void);
typedef int Size;
extern Size GetHandleSize(Handle);
extern void DisposHandle(Handle);
