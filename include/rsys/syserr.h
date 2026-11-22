/*+===================================================================
  File:      syserr.h

  Summary:   Declares syserr support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__rsys_syserr_h__)
#define __rsys_syserr_h__

struct PACKED adef {
    INTEGER id;
    INTEGER alen;
    INTEGER primetextid;
    INTEGER secondtextid;
    INTEGER iconid;
    INTEGER procid;
    INTEGER buttonid;
};

struct PACKED tdef {
    INTEGER id;
    INTEGER alen;
    Point loc;
    char text[1]; /* at least one NUL byte */
};

struct PACKED idef {
    INTEGER id;
    INTEGER alen;
    Rect loc;
    LONGINT ike[32];
};

struct PACKED pdef {
    INTEGER id;
    INTEGER alen;
    LONGINT proc; /* signed native identity in built-in tables; guest tables contain inline code */
};

struct PACKED bdef {
    INTEGER id;
    INTEGER alen;
    INTEGER nbut;

    struct PACKED but {
        INTEGER butstrid;
        Rect butloc;
        INTEGER butprocid;
    } buts[1];
};

struct PACKED sdef {
    INTEGER id;
    INTEGER alen;
    char text[1];
};

/* Native equivalents of the extracted procedures share one alert context. */
typedef struct {
    INTEGER table;
    INTEGER offsetx;
    INTEGER offsety;
    INTEGER resource_map;
    uint32 guest_a5;
    uint32 guest_a7;
    Rect bounds;
} ROMlib_syserr_context;

extern void ROMlib_syserr_procedure(INTEGER identity, ROMlib_syserr_context* context);

#endif /* !defined (__rsys_syserr_h__) */
