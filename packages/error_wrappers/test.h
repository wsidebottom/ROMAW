/*+===================================================================
  File:      test.h

  Summary:   Declares test support for error_wrappers package support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in packages/error_wrappers; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__ERROR_CHECK_H__)
#define __ERROR_CHECK_H__

extern int last_error;
extern char* last_error_file;
extern int last_error_line;
extern int die_on_error;
extern void _DisposHandle(Handle);

#define badSizeErr (-512)

#define ERROR_CHECK(n)                             \
    do {                                           \
        last_error = (n);                          \
        if (last_error != noErr && die_on_error) { \
            last_error_file = __FILE__;            \
            last_error_line = __LINE__;            \
            *(long*)-1 = -1;                       \
        }                                          \
    } while (0)

#define MEM_CHECK() ERROR_CHECK(MemError())

#define RES_CHECK() ERROR_CHECK(ResError())

#define HANDLE_CHECK(h)                                        \
    do {                                                       \
        Size __s;                                              \
                                                               \
        __s = GetHandleSize(h);                                \
        ERROR_CHECK(__s == sizeof **(h) ? noErr : badSizeErr); \
    } while (0)

#define DisposHandle(h)   \
    do {                  \
        _DisposHandle(h); \
        MEM_CHECK();      \
    } while (0)

#endif
