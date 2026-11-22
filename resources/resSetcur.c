/*+===================================================================
  File:      resSetcur.c

  Summary:   Implements res setcur support for classic Macintosh resources.

  Classes:   None.

  Functions: ROMlib_findmapres, ROMlib_findres, ROMlib_get_override_attributes

  Origin:    Existing repository source in executor/src/resources; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ResourceMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "ResourceMgr.h"
#include "MemoryMgr.h"
#include "rsys/resource.h"

P0(PUBLIC pascal trap, INTEGER, CurResFile)
{
    ROMlib_setreserr(noErr);
    return (Cx(CurMap));
}

/* System 7 private ResourceDispatch selector 0. Return the live map, not a
   copy. ResourceMgrExtensions.a preserves ResErr on success and writes it
   directly on failure, without invoking ResErrProc. */
P1(PUBLIC pascal trap, Handle, GetMap, INTEGER, rn)
{
    resmaphand map;

    if (rn == 0)
        rn = Cx(SysMap);
    map = ROMlib_rntohandl(rn, NULL);
    if (!map)
        ResErr = CWC(resFNotFound);
    return (Handle)map;
}

/* ResourceMgrExtensions.a selector 8 reads mInMemoryAttr, the low byte of
   the map attributes word, and returns only the four override bits. Success
   preserves ResErr; a nil handle sets it directly and leaves the result alone.
 */
PUBLIC void ROMlib_get_override_attributes(Handle resource_map, uint8* attributes)
{
    if (!resource_map)
        ResErr = CWC(nilHandleErr);
    else
        *attributes = Hx((resmaphand)resource_map, resfatr) & 0x1e;
}

/* ROMlib_findmapres:
      does same as ROMlib_findres (see below) except map is specified */

A4(PUBLIC, OSErr, ROMlib_findmapres, resmaphand, map, Handle, r, /* INTERNAL */
   typref**, trp, resref**, rrp)
{
    INTEGER i, j;
    typref* tr;
    resref* rr;

    WALKTANDR(map, i, tr, j, rr)
    if ((Handle)PPR(rr->rhand) == r) {
        *trp = tr;
        *rrp = rr;
        return (noErr);
    }
    EWALKTANDR(tr, rr)
    return (resNotFound);
}

/* ROMlib_findres:  given a handle to a resource, r,
	    ROMlib_findres fills in *mapp,
             *trp, *rrp with the Handle to the map, and pointers to
             the typref and the resref */

A4(PUBLIC, OSErr, ROMlib_findres, Handle, r, resmaphand*, mapp, /* INTERNAL */
   typref**, trp, resref**, rrp)
{
    resmaphand map;

    if (!r)
        /*-->*/ return resNotFound;
    WALKMAPTOP(map)
    if (ROMlib_findmapres(map, r, trp, rrp) == noErr) {
        *mapp = map;
        return (noErr);
    }
    EWALKMAP()
    for (map = ROMlib_detached_resource_maps(); map && STARH(map); map = (resmaphand)HxP(map, nextmap))
        if (ROMlib_findmapres(map, r, trp, rrp) == noErr) {
            *mapp = map;
            return noErr;
        }
    return resNotFound;
}

P1(PUBLIC pascal trap, INTEGER, HomeResFile, Handle, res)
{
    resmaphand map;
    typref* tr;
    resref* rr;

    ROMlib_setreserr(ROMlib_findres(res, &map, &tr, &rr));
    if (ResErr != noErr)
        return (-1);
    else
        return (Hx(map, resfn));
}

P1(PUBLIC pascal trap, void, UseResFile, INTEGER, rn)
{
    Handle map;

    if (rn == 0)
        rn = Cx(SysMap);
    ROMlib_invalar();
    if (ROMlib_rntohandl(rn, &map)) {
        CurMap = CW(rn);
        ROMlib_setreserr(noErr);
    }
    else
        ROMlib_setreserr(resFNotFound);
}
