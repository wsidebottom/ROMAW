/*+===================================================================
  File:      dial.h

  Summary:   Declares dial support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__rsys_dial_h__)
#define __rsys_dial_h__

typedef struct PACKED {
    int16 count;
    PACKED_MEMBER(Handle, h);
    Rect r;
    uint8 type;
    uint8 len;
    int16 res_id;
} icon_item_template_t;

#endif /* !defined (__rsys_dial_h__) */
