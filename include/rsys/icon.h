/*+===================================================================
  File:      icon.h

  Summary:   Declares icon support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__rsys_icon_h__)
#define __rsys_icon_h__

#define N_SUITE_ICONS 6
#define ICON_SUITE_SIGNATURE 0x45784953UL /* 'ExIS', private suite identity */
#define ICON_CACHE_SIGNATURE 0x45784943UL /* 'ExIC', private cache identity */

/* OSErr action (ResType, Handle *, void *), Pascal argument order. */
#define CTOP_IconActionProcTemplate 0x00000B62

#if defined(SDL) && defined(_WIN32)
extern void ROMlib_set_application_icon(unsigned long creator);
extern void ROMlib_sdl_set_application_icon(const unsigned char* rgba, unsigned long creator);
extern void ROMlib_sdl_apply_application_icon(void);
#endif

/* NOTE:  Cotton didn't really use a struct and I'm not about to munch his
   code, especially since we don't know the real layout and this is subject
   to change.  I just wanted to paste in the flags field so we can drop a
   label in there. */

typedef struct PACKED {
    Handle icons[N_SUITE_ICONS];
    INTEGER label;
    uint32 signature;
} cotton_suite_layout_t;

/* Preserve ROMAW's private suite prefix. Public callback/data outputs still
   occupy four guest bytes, independent of the host pointer width. */
typedef struct PACKED {
    cotton_suite_layout_t suite;
    uint32 data, procedure;
} cotton_cache_layout_t;

#endif /* !defined (__rsys_icon_h__) */
