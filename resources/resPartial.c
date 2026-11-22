/*+===================================================================
  File:      resPartial.c

  Summary:   Implements res partial support for classic Macintosh resources.

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/resources; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"

#include "ResourceMgr.h"
#include "FileMgr.h"
#include "rsys/resource.h"
#include "rsys/file.h"

/* Native partial operations share the existing resource writer so resource-map
   relocation, compaction and persistence follow the normal WriteResource path. */
#include "MemoryMgr.h"

static OSErr partial_resource(Handle resource, Boolean writing, resmaphand* map)
{
    typref* type;
    resref* ref;
    OSErr err;
    if (!resource)
        return nilHandleErr;
    err = ROMlib_findres(resource, map, &type, &ref);
    if (err != noErr)
        return err;
    if (writing && (Hx(*map, resfatr) & mapReadOnly))
        return mapReadErr;
    if (writing && (ref->ratr & resProtected))
        return resAttrErr;
    LoadResource(resource);
    return ResError();
}

P4(PUBLIC pascal trap, void, ReadPartialResource, Handle, res, int32, offset, Ptr, buffer, int32, count)
{
    resmaphand map;
    OSErr err;
    Size size;
    if (offset < 0 || count < 0 || (count && !buffer)) {
        ROMlib_setreserr(inputOutOfBounds);
        return;
    }
    err = partial_resource(res, FALSE, &map);
    if (err == noErr) {
        size = GetHandleSize(res);
        if (offset > size || count > size - offset)
            err = inputOutOfBounds;
        else if (count)
            memmove(buffer, STARH(res) + offset, count);
    }
    ROMlib_setreserr(err);
}

static OSErr modify_partial_resource(Handle resource, int32 offset, Ptr buffer, int32 count, int32 requested_size)
{
    resmaphand map;
    Handle copy;
    Size old_size, size;
    SignedByte state;
    OSErr err = partial_resource(resource, TRUE, &map);
    if (err != noErr)
        return err;
    old_size = GetHandleSize(resource);
    if (requested_size < 0 && !count)
        return offset <= old_size ? noErr : inputOutOfBounds;
    if (requested_size == old_size)
        return noErr;
    size = requested_size >= 0 ? requested_size : MAX(old_size, offset + count);
    if (size > 0xffffff)
        return inputOutOfBounds;
    /* Stage the edit before resizing: the source may be in the same handle,
     and allocation failure must not overwrite the old resource bytes. */
    state = HGetState(resource);
    HLock(resource);
    copy = NewHandleClear(size);
    if (!copy) {
        err = MemError();
        HSetState(resource, state);
        return err;
    }
    if (MIN(old_size, size))
        memmove(STARH(copy), STARH(resource), MIN(old_size, size));
    if (count)
        memmove(STARH(copy) + offset, buffer, count);
    HSetState(resource, state);
    HLock(copy);
    SetHandleSize(resource, size);
    err = MemError();
    if (err == noErr) {
        if (size)
            memmove(STARH(resource), STARH(copy), size);
        ChangedResource(resource);
        err = ResError();
        if (err == noErr) {
            WriteResource(resource);
            err = ResError();
        }
        if (err == noErr) {
            UpdateResFile(Hx(map, resfn));
            err = ResError();
        }
    }
    DisposHandle(copy);
    return err;
}

P4(PUBLIC pascal trap, void, WritePartialResource, Handle, resource, int32, offset, Ptr, buffer, int32, count)
{
    if (offset < 0 || count < 0 || count > INT32_MAX - offset || (count && !buffer))
        ROMlib_setreserr(inputOutOfBounds);
    else
        ROMlib_setreserr(modify_partial_resource(resource, offset, buffer, count, -1));
}

P2(PUBLIC pascal trap, void, SetResourceSize, Handle, resource, int32, size)
{
    if (size < 0 || size > 0xffffff)
        ROMlib_setreserr(inputOutOfBounds);
    else
        ROMlib_setreserr(modify_partial_resource(resource, 0, NULL, 0, size));
}

/* Word6 has this lovely code in it

0xe49a6:	beqs 0xe49ba
0xe49a8:	subqw #4,sp
0xe49aa:	movel fp@(12),sp@-
0xe49ae:	moveq #10,d0
0xe49b0:	movel sp,fp@
0xe49b2:	_ResourceDispatch
0xe49b4:	movel sp@+,fp@(16)
0xe49b8:	bras 0xe49be

**************************************/

P1(PUBLIC pascal trap, Handle, GetNextFOND, Handle, fondHandle)
{
    Handle retval;
    warning_unimplemented(NULL_STRING);

    retval = 0;
    return retval;
}
