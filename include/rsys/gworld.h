/*+===================================================================
  File:      gworld.h

  Summary:   Declares gworld support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_GWORLD_H_)
#define _GWORLD_H_

typedef struct gw_info {
    GWorldPtr gw;
    PixMapHandle gw_pixmap;
    GDHandle gw_gd;
    PixMapHandle gw_gd_pixmap;

    GWorldFlags flags;

    int pixel_lock_count;
    boolean_t gd_allocated_p;

    struct gw_info* next;
} gw_info_t;

/* NewGWorldFromPtr marks borrowed pixel buffers in both PixMaps it owns. */
#define GWORLD_EXTERNAL_PIXMAP_VERSION 4

extern QDErr ROMlib_new_gworld_from_ptr(HIDDEN_GWorldPtr*, INTEGER, Rect*, CTabHandle, GDHandle, GWorldFlags, Ptr,
                                        INTEGER);

extern gw_info_t* lookup_gw_info_by_gw(GWorldPtr);
extern gw_info_t* lookup_gw_info_by_gw_pixmap(PixMapHandle);
extern gw_info_t* lookup_gw_info_by_gw_pixmap_baseaddr(void*);

extern gw_info_t* lookup_gw_info_by_gw_gd_pixmap(PixMapHandle);

#endif /* !_GWORLD_H_ */
