/*+===================================================================
  File:      resOpen.c

  Summary:   Implements res open support for classic Macintosh resources.

  Classes:   None.

  Functions: HCreateResFile_helper, ROMlib_initialize_resource_descriptor, ROMlib_mgetres, ROMlib_mgetres2,
             ROMlib_open_resource_descriptor, ROMlib_open_data_resource_descriptor, ROMlib_open_data_resource_file,
             ROMlib_create_data_resource_file, ROMlib_detach_resource_file, ROMlib_detached_resource_maps,
             ROMlib_insert_resource_file, ROMlib_open_orphan_resource_file, ROMlib_resource_chain_refnum,
             ROMlib_resource_file_already_open, ROMlib_resource_file_map,
             ROMlib_rntohandl

  Origin:    Existing repository source in executor/src/resources; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1989, 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ResourceMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "FontMgr.h"
#include "rsys/font.h"
#include "ResourceMgr.h"
#include "MemoryMgr.h"
#include "FileMgr.h"
#include "OSUtil.h"

#include "rsys/resource.h"
#include "rsys/glue.h"
#include "rsys/mman.h"
#include "rsys/file.h"
#include "rsys/pstuff.h"
#include "rsys/osevent.h"
#include "rsys/prefs.h"
#include "rsys/dcomp0.h"
#include "rsys/system_resources.h"

OSErr ROMlib_initialize_resource_descriptor(INTEGER refnum)
{
    LONGINT length;
    OSErr error = GetEOF(refnum, &length);
    if (error || length)
        return error ? error : dupFNErr;
    empty_resource_template_t header = {0};
    header.bhead.rdatoff = header.bhead.rmapoff = CL(sizeof(reshead) + sizeof(rsrvrec));
    header.bhead.maplen = CL(sizeof(resmap) + sizeof(INTEGER));
    header.bmap.namoff = CW(sizeof(resmap) + sizeof(INTEGER));
    header.bmap.typoff = CW(sizeof(resmap));
    header.negone = CW(-1);
    length = sizeof header;
    return FSWriteAll(refnum, &length, (Ptr)&header);
}

static resmaphand detached_maps;

static INTEGER already_open_res_file(INTEGER swapped_vref, LONGINT swapped_file_num, int data_fork);

static HParamBlockRec data_resource_request(const FSSpec* spec)
{
    HParamBlockRec block = {0};
    PP_RAW(block.ioParam.ioNamePtr) = CL(US_TO_SYN68K(spec->name));
    block.ioParam.ioVRefNum = spec->vRefNum;
    block.fileParam.ioDirID = spec->parID;
    return block;
}

OSErr ROMlib_create_data_resource_file(const FSSpec* spec)
{
    HParamBlockRec block = data_resource_request(spec);
    block.ioParam.ioPermssn = fsCurPerm;
    OSErr error = PBHOpen(&block, FALSE);
    const int missing = error == fnfErr;
    if (missing) {
        block = data_resource_request(spec);
        error = PBHCreate(&block, FALSE);
        if (error)
            return ROMlib_setreserr(error);
        block = data_resource_request(spec);
        block.ioParam.ioPermssn = fsCurPerm;
        error = PBHOpen(&block, FALSE);
    }
    if (!error) {
        const INTEGER refnum = CW(block.ioParam.ioRefNum);
        error = ROMlib_initialize_resource_descriptor(refnum);
        const OSErr close_error = FSClose(refnum);
        if (!error)
            error = close_error;
    }
    if (error && missing) {
        block = data_resource_request(spec);
        PBHDelete(&block, FALSE);
    }
    return ROMlib_setreserr(error);
}

OSErr ROMlib_open_data_resource_file(const FSSpec* spec, SignedByte permission, INTEGER* reference)
{
    HParamBlockRec block = data_resource_request(spec);
    block.ioParam.ioPermssn = permission;
    OSErr error = PBHOpen(&block, FALSE);
    INTEGER refnum = -1;
    if (!error) {
        const INTEGER opened = CW(block.ioParam.ioRefNum);
        if (permission != fsRdPerm) {
            OSErr fcb_error;
            const fcbrec* fcb = PRNTOFPERR(opened, &fcb_error);
            if (!fcb_error) {
                const VCB* volume = PPR(fcb->fcvptr);
                refnum = already_open_res_file(volume->vcbVRefNum, fcb->fdfnum, 1);
            }
        }
        if (refnum == -1) {
            refnum = ROMlib_open_data_resource_descriptor(opened);
            if (refnum == -1)
                error = CW(ResErr);
        }
        else {
            FSClose(opened);
            CurMap = CW(refnum);
        }
    }
    else if (error == opWrErr && ROMlib_rntohandl(CW(block.ioParam.ioRefNum), NULL)) {
        refnum = CW(block.ioParam.ioRefNum);
        CurMap = CW(refnum);
        error = noErr;
    }
    *reference = CW(refnum);
    return ROMlib_setreserr(error);
}

OSErr ROMlib_resource_chain_refnum(int top, INTEGER current, INTEGER* reference)
{
    resmaphand map = top ? (resmaphand)MR(TopMapHndl) : ROMlib_rntohandl(current, NULL);
    *reference = 0;
    if (!map || !STARH(map))
        return resFNotFound;
    if (!top) {
        do {
            map = (resmaphand)HxP(map, nextmap);
            /* CarbonLib skips private maps marked with attribute bit 1. */
        } while (map && STARH(map) && (Hx(map, resfatr) & 2));
    }
    if (map && STARH(map))
        *reference = HxX(map, resfn);
    return noErr;
}

resmaphand ROMlib_detached_resource_maps(void)
{
    return detached_maps;
}

static resmaphand find_detached_map(INTEGER reference, resmaphand* previous)
{
    *previous = NULL;
    for (resmaphand map = detached_maps; map && STARH(map); map = (resmaphand)HxP(map, nextmap)) {
        if (Hx(map, resfn) == reference)
            return map;
        *previous = map;
    }
    return NULL;
}

resmaphand ROMlib_resource_file_map(INTEGER reference)
{
    resmaphand map = ROMlib_rntohandl(reference, NULL);
    resmaphand previous;
    return map ? map : find_detached_map(reference, &previous);
}

static void unlink_resource_map(resmaphand map, resmaphand previous, int detached)
{
    const resmaphand next = (resmaphand)HxP(map, nextmap);
    if (previous)
        PP_ASSIGN(HxX(previous, nextmap), (Handle)next);
    else if (detached)
        detached_maps = next;
    else
        HIDDEN_RAW_SET(TopMapHndl_H, RM((Handle)next));
}

static void select_after_detach(resmaphand map)
{
    if (CurMap == HxX(map, resfn)) {
        resmaphand replacement = (resmaphand)HxP(map, nextmap);
        if (!replacement)
            replacement = (resmaphand)MR(TopMapHndl);
        CurMap = replacement ? HxX(replacement, resfn) : 0;
    }
}

static void invalidate_resource_chain(void)
{
    invalidate_kchr_ptr();
    ROMlib_invalar();
}

OSErr ROMlib_detach_resource_file(INTEGER reference)
{
    Handle previous;
    resmaphand map = ROMlib_rntohandl(reference, &previous);
    if (!map)
        return resFNotFound;
    unlink_resource_map(map, (resmaphand)previous, 0);
    select_after_detach(map);
    PP_ASSIGN(HxX(map, nextmap), (Handle)detached_maps);
    detached_maps = map;
    invalidate_resource_chain();
    return noErr;
}

OSErr ROMlib_insert_resource_file(INTEGER reference, INTEGER location)
{
    enum { below_system = 0, below_application = 1, above_application = 2, above_all = 4 };

    INTEGER anchor = 0;
    if (location == below_system)
        anchor = CW(SysMap);
    else if (location == below_application || location == above_application)
        anchor = CW(CurApRefNum);
    else if (location != above_all)
        return paramErr;

    Handle attached_previous;
    resmaphand map = ROMlib_rntohandl(reference, &attached_previous);
    resmaphand previous = (resmaphand)attached_previous;
    const int detached = !map;
    if (detached)
        map = find_detached_map(reference, &previous);
    if (!map)
        return resFNotFound;

    resmaphand destination = NULL;
    if (location != above_all) {
        Handle anchor_previous;
        resmaphand anchor_map = ROMlib_rntohandl(anchor, &anchor_previous);
        if (!anchor_map || anchor_map == map)
            return paramErr;
        destination = location == above_application ? (resmaphand)anchor_previous : anchor_map;
        if (destination == map)
            destination = previous;
    }
    unlink_resource_map(map, previous, detached);
    if (!detached)
        select_after_detach(map);
    if (destination) {
        PP_RAW(HxX(map, nextmap)) = PP_RAW(HxX(destination, nextmap));
        PP_ASSIGN(HxX(destination, nextmap), (Handle)map);
    }
    else {
        PP_ASSIGN(HxX(map, nextmap), (Handle)MR(TopMapHndl));
        HIDDEN_RAW_SET(TopMapHndl_H, RM((Handle)map));
    }
    if (location == above_all)
        CurMap = CW(reference);
    invalidate_resource_chain();
    return noErr;
}

OSErr ROMlib_open_orphan_resource_file(const FSSpec* spec, SignedByte permission, INTEGER* reference)
{
    const Handle previous_top = TopMapHndl;
    const INTEGER previous_current = CurMap;
    Str255 name;
    str255assign(name, spec->name);
    const INTEGER opened = HOpenResFile(CW(spec->vRefNum), CL(spec->parID), name, permission);
    if (opened == -1)
        return CW(ResErr);
    if (previous_top == TopMapHndl) {
        CurMap = previous_current;
        return paramErr; /* HOpenResFile reused an attached map. */
    }
    const OSErr status = ROMlib_detach_resource_file(opened);
    if (!status) {
        CurMap = previous_current;
        *reference = CW(opened);
    }
    return status;
}

PUBLIC void HCreateResFile_helper(INTEGER vrefnum, LONGINT parid, Str255 name, OSType creator, OSType type,
                                  ScriptCode script)
{
    OSErr error = HCreate(vrefnum, parid, name, creator, type);
    INTEGER refnum;
    if (error == noErr || error == dupFNErr) {
        error = HOpenRF(vrefnum, parid, name, fsRdWrPerm, &refnum);
        if (!error) {
            error = ROMlib_initialize_resource_descriptor(refnum);
            const OSErr close_error = FSClose(refnum);
            if (!error)
                error = close_error;
        }
    }
    ROMlib_setreserr(error);
}

int ROMlib_resource_file_already_open(const FSSpec* spec, Boolean* in_chain, INTEGER* reference)
{
    if (in_chain)
        *in_chain = 0;
    if (reference)
        *reference = CW(-1);
    if (!spec || spec->name[0] > 63)
        return 0;
    FCBPBRec block = {0};
    Str255 name;
    PP_ASSIGN(block.ioNamePtr, (StringPtr)name);
    for (unsigned index = 1; index <= INT16_MAX; ++index) {
        block.ioFCBIndx = CW(index);
        block.ioVRefNum = spec->vRefNum;
        if (PBGetFCBInfo(&block, FALSE))
            break;
        OSErr error;
        const INTEGER refnum = CW(block.ioRefNum);
        const fcbrec* fcb = PRNTOFPERR(refnum, &error);
        if (error || !(fcb->fcflags & fcfisres) || block.ioFCBParID != spec->parID ||
            ROMlib_RelString(name + 1, (Byte*)spec->name + 1, FALSE, TRUE, ((LONGINT)name[0] << 16) | spec->name[0]))
            continue;
        if (in_chain) {
            resmaphand map;
            WALKMAPTOP(map)
            if (Hx(map, resfn) == refnum) {
                *in_chain = 1;
                break;
            }
            EWALKMAP()
        }
        if (reference)
            *reference = block.ioRefNum;
        return 1;
    }
    return 0;
}

P1(PUBLIC pascal trap, void, CreateResFile, StringPtr, fn)
{
    HCreateResFile_helper(0, 0, fn, TICK("????"), TICK("????"), 0);
}

P3(PUBLIC pascal trap, void, HCreateResFile, INTEGER, vrefnum, LONGINT, parid, Str255, name)
{
    HCreateResFile_helper(vrefnum, parid, name, TICK("????"), TICK("????"), 0);
}

Handle ROMlib_mgetres(resmaphand map, resref* rr)
{
    Handle retval;

    if (ostraptable[0xFC] == osstuff[0xFC].orig)
        retval = ROMlib_mgetres2(map, rr);
    else {
        LONGINT saved0, saved1, saved2, savea0, savea1, savea2, savea3, savea4;

        saved0 = EM_D0;
        saved1 = EM_D1;
        saved2 = EM_D2;
        savea0 = EM_A0;
        savea1 = EM_A1;
        savea2 = EM_A2;
        savea3 = EM_A3;
        savea4 = EM_A4;
        EM_A4 = (LONGINT)(long)US_TO_SYN68K(map);
        EM_A3 = (LONGINT)(long)US_TO_SYN68K(rr);
        EM_A2 = (LONGINT)(long)US_TO_SYN68K(rr);
#define TEMPORARYHACKUNTILWESWAPTABLES
#if !defined(TEMPORARYHACKUNTILWESWAPTABLES)
        EM_A0 = (LONGINT)CL(ostraptable[0xFC]);
#else /* defined(TEMPORARYHACKUNTILWESWAPTABLES) */
        EM_A0 = (LONGINT)(long)ostraptable[0xFC];
#endif
        CALL_EMULATOR(EM_A0);
        retval = (Handle)SYN68K_TO_US(EM_A0);
        EM_D0 = saved0;
        EM_D1 = saved1;
        EM_D2 = saved2;
        EM_A0 = savea0;
        EM_A1 = savea1;
        EM_A2 = savea2;
        EM_A3 = savea3;
        EM_A4 = savea4;
    }
    return retval;
}

P4(PUBLIC pascal trap, void, dcmp_template, Ptr, source, Ptr, dest, Ptr, working, Size, len)
{
    uintptr_t base = (uintptr_t)ROMlib_offset;
    uintptr_t input = (uintptr_t)source - base;
    uintptr_t output = (uintptr_t)dest - base;
    size_t decoded_size;
    uint8_t* buffer;

    /* This Pascal interface supplies the compressed length only. Measure the
     stream before writing, bound both guest extents, and use separate storage
     because callers may place compressed input inside their output buffer. */
    if (len < 0 || input >= ROMlib_guest_memory_size || (size_t)len > ROMlib_guest_memory_size - input ||
        output >= ROMlib_guest_memory_size ||
        !ROMlib_decode_dcmp0((const uint8_t*)source, len, NULL, ROMlib_guest_memory_size - output, &decoded_size)) {
        ROMlib_setreserr(CantDecompress);
        return;
    }
    buffer = malloc(decoded_size ? decoded_size : 1);
    if (!buffer) {
        ROMlib_setreserr(memFullErr);
        return;
    }
    if (ROMlib_decompress_dcmp0((const uint8_t*)source, len, buffer, decoded_size)) {
        memcpy(dest, buffer, decoded_size);
        ROMlib_destroy_blocks(US_TO_SYN68K(dest), decoded_size, TRUE);
        ROMlib_setreserr(noErr);
    }
    else
        ROMlib_setreserr(CantDecompress);
    free(buffer);
}

/* Type-9 decompressors contain init/decode/finish entry offsets rather than
 * a single procedure. Decode receives the complete 18-byte header separately. */
enum {
    /* CToPascalCall: void (Ptr, Ptr, Ptr) and void (Ptr), respectively. */
    CTOP_dcmp9_template = 0x00000B68,
    CTOP_dcmp9_state_template = 0x00000028
};

typedef enum { DECODE_GUEST8, DECODE_BUILTIN0, DECODE_GUEST9 } resource_decode_mode;

PRIVATE boolean_t decompress9_setup(INTEGER rn, int32* dlenp, int32* final_sizep, Handle* dcmp_handlep, Ptr* workspacep,
                                    resource_decode_mode* modep, const dcomp_info_t* info)
{
    unsigned char header[18];
    LONGINT len = 2, saved;
    memcpy(header, info, sizeof *info);
    if (*dlenp < sizeof header || CL(info->uncompressedSize) < 0 ||
        FSReadAll(rn, &len, (Ptr)header + sizeof *info) != noErr)
        goto failed;
    INTEGER id = (header[12] << 8) | header[13];
    GetFPos(rn, &saved);
    *dcmp_handlep = GetResource(TICK("dcmp"), id);
    SetFPos(rn, fsFromStart, saved);
    if (!*dcmp_handlep)
        goto failed;
    LoadResource(*dcmp_handlep);
    if (!STARH(*dcmp_handlep) || GetHandleSize(*dcmp_handlep) < 6)
        goto failed;
    /* Validate all three entry points before invoking guest code. */
    const unsigned char* code = (const unsigned char*)STARH(*dcmp_handlep);
    Size size = GetHandleSize(*dcmp_handlep);
    for (unsigned i = 0; i < 6; i += 2) {
        unsigned offset = (code[i] << 8) | code[i + 1];
        if (offset < 6 || (offset & 1) || offset > size - 2)
            goto failed;
    }
    *workspacep = NewPtr(*dlenp);
    if (!*workspacep) {
        ROMlib_setreserr(memFullErr);
        return FALSE;
    }
    memcpy(*workspacep, header, sizeof header);
    *dlenp -= sizeof header;
    *final_sizep = CL(info->uncompressedSize);
    *modep = DECODE_GUEST9;
    ROMlib_setreserr(noErr);
    return TRUE;
failed:
    ROMlib_setreserr(CantDecompress);
    return FALSE;
}

/* TODO: decompress_setup also has to pass back the decompressed size
   we need to adjust down the size of "dlen" down below where we read
   the compressed info. */

PRIVATE boolean_t decompress_setup(INTEGER rn, int32* dlenp, int32* final_sizep, int32* offsetp, Handle* dcmp_handlep,
                                   Ptr* workspacep, resource_decode_mode* modep)
{
    boolean_t retval;
    OSErr err;
    LONGINT len;
    dcomp_info_t info;
    LONGINT master_save_pos;

    *final_sizep = *dlenp;
    *offsetp = 0;
    *dcmp_handlep = NULL;
    *workspacep = NULL;
    *modep = DECODE_GUEST8;

    GetFPos(rn, &master_save_pos);
    len = sizeof info;
    err = FSReadAll(rn, &len, (Ptr)&info);

    /*
   * If we can't read the entire header in or if we don't get the correct tag
   * then we'll return false, but clear ResErr.  This is a sign that the
   * resource is to be treated as a non-compressed resource.
   */

    if (err != noErr || info.compressedResourceTag != CLC(COMPRESSED_TAG)) {
        SetFPos(rn, fsFromStart, master_save_pos);
        ROMlib_setreserr(noErr);
        /*->*/ return FALSE;
    }

    if (info.typeFlags == CLC(0x00120901))
        return decompress9_setup(rn, dlenp, final_sizep, dcmp_handlep, workspacep, modep, &info);

    if (*dlenp < sizeof info || CL(info.uncompressedSize) < 0 || info.typeFlags != CLC(COMPRESSED_FLAGS))
        retval = FALSE;
    else {
        LONGINT save_pos;

        GetFPos(rn, &save_pos);
        *dcmp_handlep = GetResource(TICK("dcmp"), CW(info.dcmpID));
        SetFPos(rn, fsFromStart, save_pos);

        if (*dcmp_handlep)
            LoadResource(*dcmp_handlep);
        static const unsigned char builtin0[] = {0x20, 0x78, 0x00, 0x58, 0x20, 0x68, 0x00, 0x28, 0x4e, 0xd0};
        boolean_t native0 = *dcmp_handlep && GetHandleSize(*dcmp_handlep) == sizeof builtin0 && STARH(*dcmp_handlep) &&
                            !memcmp(STARH(*dcmp_handlep), builtin0, sizeof builtin0);
        if (info.dcmpID == CWC(0) && (!*dcmp_handlep || native0)) {
            /* System 7's standard decompressor need not exist in the open
	     resource chain. Decode from separate storage, never in place. */
            *dlenp -= sizeof info;
            *final_sizep = CL(info.uncompressedSize);
            *workspacep = NewPtr(*dlenp ? *dlenp : 1);
            *modep = DECODE_BUILTIN0;
            retval = *workspacep != NULL;
        }
        else if (!*dcmp_handlep)
            retval = FALSE;
        else {
            int32 final_size;
            int32 working_size;

            LoadResource(*dcmp_handlep);
            final_size = CL(info.uncompressedSize);

            /*
	   * The MacTech article says that the workingBufferFractionalSize
	   * byte is a fixed point value, but it doesn't give enough
	   * information to be sure how to interpret it.  I tried  using it
	   * in a variety of ways and found I was not allocating enough bytes.
	   * This use seems to work, but I'm sufficiently nervous to merit
	   * possibly allocating more room than we needed.
	   */

            working_size = (*dlenp + (double)*dlenp * CB(info.workingBufferFractionalRatio) / (1 << 8));

#define DONT_TRUST_FRACTIONAL_RATIO
#if defined(DONT_TRUST_FRACTIONAL_RATIO)
            working_size = MAX(final_size, working_size);
#endif

            *workspacep = NewPtr(working_size);
            if (!*workspacep)
                retval = FALSE;
            else {
                *dlenp -= sizeof info;
                *final_sizep = final_size;
                *offsetp = CB(info.expansionBufferSize);
                retval = TRUE;
            }
        }
    }

    if (!retval)
        ROMlib_setreserr(CantDecompress);
    return retval;
}

/* ROMlib_mgetres: given a resource map handle and a
	    resource reference pointer, ROMlib_mgetres returns a handle to
	    the appropriate resource */

/*
 * TODO: see whether or not lock bits and whatnot get reset on a
 *       mgetres when the handle's already there.
 */

PRIVATE Handle mgetres_helper(resmaphand map, resref* rr, int32 dlen, Handle retval)
{
    int32 dcmp_offset;
    Handle dcmp_handle;
    Ptr dcmp_workspace;
    int32 uncompressed_size;
    Ptr xxx;
    OSErr err;
    boolean_t compressed_p;
    boolean_t done_p;
    resource_decode_mode mode = DECODE_GUEST8;

    done_p = FALSE;
    compressed_p = (rr->ratr & resCompressed) && system_version >= 0x700;

    if (compressed_p) {
        if (!decompress_setup(Hx(map, resfn), &dlen, &uncompressed_size, &dcmp_offset, &dcmp_handle, &dcmp_workspace,
                              &mode)) {
            if (ResErr == noErr)
                compressed_p = FALSE;
            else {
                retval = NULL;
                done_p = TRUE;
            }
        }
    }

    if (!done_p) {
        if (!compressed_p) {
            dcmp_offset = 0;
            dcmp_handle = NULL;
            dcmp_workspace = NULL;
            uncompressed_size = dlen;
        }

        if (!PP_RAW(rr->rhand)) {
            HIDDEN_RAW_SET(TheZone_H, ((rr->ratr & resSysHeap) ? SysZone : RM(HandleZone((Handle)map))));
            retval = NewHandle(uncompressed_size + dcmp_offset);
            FIELD_ASSIGN_RAW(rr->rhand, RM(retval));
        }
        else {
            retval = PPR(rr->rhand);
            if (retval)
                ReallocHandle(retval, uncompressed_size + dcmp_offset);
            else {
                fprintf(
                    stderr,
                    "trace: mgetres_helper replacing stale resource handle map=%p rr=%p rhand=%p type? trap=0x%04x\n",
                    (void*)map, (void*)rr, (void*)(uintptr_t)PP_RAW(rr->rhand), (unsigned)mostrecenttrap);
                fflush(stderr);
                FIELD_ASSIGN_RAW(rr->rhand, NULL);
                HIDDEN_RAW_SET(TheZone_H, ((rr->ratr & resSysHeap) ? SysZone : RM(HandleZone((Handle)map))));
                retval = NewHandle(uncompressed_size + dcmp_offset);
                FIELD_ASSIGN_RAW(rr->rhand, RM(retval));
            }
        }
        err = MemError();
        if (err != noErr || !retval || !STARH(retval)) {
            ROMlib_setreserr(err != noErr ? err : memFullErr);
            if (dcmp_workspace)
                DisposPtr(dcmp_workspace);
            if (PP_RAW(rr->rhand)) {
                DisposHandle(PPR(rr->rhand));
                PP_RAW(rr->rhand) = NULL;
            }
            return NULL;
        }
        xxx = mode != DECODE_GUEST8 ? dcmp_workspace + (mode == DECODE_GUEST9 ? 18 : 0)
                                    : STARH(retval) + uncompressed_size + dcmp_offset - dlen;
        if ((ROMlib_setreserr(err)) || (ROMlib_setreserr(err = FSReadAll(Hx(map, resfn), &dlen, xxx)))) {
            if (dcmp_workspace)
                DisposPtr(dcmp_workspace);
            DisposHandle(PPR(rr->rhand));
            PP_RAW(rr->rhand) = NULL;
            retval = NULL;
        }
        else {
            if (mode == DECODE_BUILTIN0) {
                boolean_t decoded;

                decoded =
                    ROMlib_decompress_dcmp0((const uint8_t*)xxx, dlen, (uint8_t*)STARH(retval), uncompressed_size);
                DisposPtr(dcmp_workspace);
                if (!decoded) {
                    DisposHandle(retval);
                    PP_RAW(rr->rhand) = NULL;
                    retval = NULL;
                    ROMlib_setreserr(CantDecompress);
                }
            }
            else if (dcmp_handle) {
                ProcPtr dcmp;
                SignedByte state;

                state = hlock_return_orig_state(dcmp_handle);
                dcmp = (typeof(dcmp))STARH(dcmp_handle);
                HLock(retval);
                if (mode == DECODE_GUEST9) {
                    const unsigned char* code = (const unsigned char*)dcmp;
                    ProcPtr init = dcmp + ((code[0] << 8) | code[1]);
                    ProcPtr decode = dcmp + ((code[2] << 8) | code[3]);
                    ProcPtr finish = dcmp + ((code[4] << 8) | code[5]);
                    CToPascalCall(init, CTOP_dcmp9_state_template, dcmp_workspace);
                    CToPascalCall(decode, CTOP_dcmp9_template, xxx, STARH(retval), dcmp_workspace);
                    CToPascalCall(finish, CTOP_dcmp9_state_template, dcmp_workspace);
                }
                else
                    CToPascalCall(dcmp, CTOP_dcmp_template, xxx, STARH(retval), dcmp_workspace, dlen);
                HUnlock(retval);
                SetHandleSize(retval, uncompressed_size);
                HSetState(dcmp_handle, state);
                if (dcmp_workspace)
                    DisposPtr(dcmp_workspace);
            }
        }
    }

    if (compressed_p && retval && STARH(retval)) {
        /* Decompression writes outside PBRead. Reused heap addresses can still
         have translations for their former contents, including A-line traps. */
        ROMlib_destroy_blocks((syn68k_addr_t)US_TO_SYN68K(STARH(retval)), GetHandleSize(retval), TRUE);
    }

    return retval;
}

/* ROMlib_mgetres: given a resource map handle and a
	    resource reference pointer, ROMlib_mgetres returns a handle to
	    the appropriate resource */

/*
 * TODO: see whether or not lock bits and whatnot get reset on a
 *       mgetres when the handle's already there.
 */

PRIVATE int32 next_resource_data_offset(resmaphand map, resref* current_rr)
{
    INTEGER i, j;
    typref* tr;
    resref* rr;
    int32 current_offset, next_offset, offset;

    current_offset = B3TOLONG(current_rr->doff);
    next_offset = Hx(map, rh.datlen);
    WALKTANDR(map, i, tr, j, rr)
    offset = B3TOLONG(rr->doff);
    if (offset > current_offset && offset < next_offset)
        next_offset = offset;
    EWALKTANDR(tr, rr)
    return next_offset;
}

Handle ROMlib_mgetres2(resmaphand map, resref* rr)
{
    Handle retval;

    retval = PPR(rr->rhand);
    if (retval && HIDDEN_RAW(*(retval)))
        ROMlib_setreserr(noErr);
    else {
        THz savezone;
        SignedByte state;
        int32 loc;

        savezone = TheZone;
        state = hlock_return_orig_state((Handle)map);
        loc = Hx(map, rh.rdatoff) + B3TOLONG(rr->doff);
        ROMlib_setreserr(SetFPos(Hx(map, resfn), fsFromStart, loc));
        if (ResErr != noErr)
            retval = NULL;
        else {
            int32 lc;
            OSErr err;
            int32 dlen; /* length on disk (remaining) */
            int32 data_offset;
            int32 data_remaining;
            int32 next_offset;
            int32 max_payload_len;

            lc = sizeof(Size);
            err = FSReadAll(Hx(map, resfn), &lc, (Ptr)&dlen);
            ROMlib_setreserr(err);
            if (ResErr != noErr)
                retval = NULL;
            else {
                dlen = CL(dlen);
                data_offset = B3TOLONG(rr->doff);
                data_remaining = Hx(map, rh.datlen) - data_offset;
                next_offset = next_resource_data_offset(map, rr);
                if (next_offset > data_offset)
                    max_payload_len = next_offset - data_offset - sizeof(Size);
                else
                    max_payload_len = data_remaining - sizeof(Size);
                if (dlen < 0 || dlen > max_payload_len) {
                    int32 alt_len;

                    alt_len = -1;
                    if (data_offset >= (int32)sizeof(Size)) {
                        int32 raw_alt_len;

                        ROMlib_setreserr(SetFPos(Hx(map, resfn), fsFromStart, loc - sizeof(Size)));
                        if (ResErr == noErr) {
                            lc = sizeof(Size);
                            err = FSReadAll(Hx(map, resfn), &lc, (Ptr)&raw_alt_len);
                            ROMlib_setreserr(err);
                            if (ResErr == noErr) {
                                raw_alt_len = CL(raw_alt_len);
                                if (raw_alt_len > 0 && raw_alt_len <= data_remaining)
                                    alt_len = raw_alt_len;
                            }
                        }
                    }

                    if (alt_len < 0) {
                        if (next_offset > data_offset)
                            alt_len = next_offset - data_offset;
                    }

                    if (alt_len >= 0 && alt_len <= data_remaining) {
                        fprintf(
                            stderr,
                            "trace: ROMlib_mgetres2 corrected resource length old=%ld new=%ld offset=0x%lx map=%p rr=%p\n",
                            (long)dlen, (long)alt_len, (unsigned long)data_offset, (void*)map, (void*)rr);
                        fflush(stderr);
                        dlen = alt_len;
                        ROMlib_setreserr(SetFPos(Hx(map, resfn), fsFromStart, loc));
                    }
                }
                if (ResErr != noErr)
                    retval = NULL;
                else {
                    if (ResLoad)
                        retval = mgetres_helper(map, rr, dlen, retval);
                    else if (!PP_RAW(rr->rhand)) {
                        HIDDEN_RAW_SET(TheZone_H, ((rr->ratr & resSysHeap) ? SysZone : RM(HandleZone((Handle)map))));
                        retval = NewEmptyHandle();
                        PP_RAW(rr->rhand) = RM(retval);
                    }

                    /* we can only set the state bits if the block pointer
		 is non-nil */

                    if (retval && HIDDEN_RAW(*(retval)))
                        HSetState(retval, (RSRCBIT | ((rr->ratr & resLocked) ? LOCKBIT : 0) |
                                           ((rr->ratr & resPurgeable) ? PURGEBIT : 0)));
                }
            }
        }
        HSetState((Handle)map, state);
        HIDDEN_RAW_SET(TheZone_H, savezone);
    }
    return retval;
}

/* ROMlib_rntohandl:  ROMlib_rntohandl returns the resmap handle
	       of the resource file
               with the reference number rn, *pph is filled in with the
               Handle to the previous (on the linked list of files)
               file.  Note pph is undefined if rn is at the top, nor
               is it filled in if pph is nil */

A2(PUBLIC, resmaphand, ROMlib_rntohandl, INTEGER, rn, /* INTERNAL */
   Handle*, pph)
{
    resmaphand map, ph;

    ph = 0;
    WALKMAPTOP(map)
    if (!STARH(map)) {
        fprintf(stderr, "trace: ROMlib_rntohandl empty map handle rn=%d map=%p ph=%p TopMapHndl=%p CurMap=%d\n", rn,
                (void*)map, (void*)ph, (void*)MR(TopMapHndl), Cx(CurMap));
        fflush(stderr);
        map = 0;
        break;
    }
    if (Hx(map, resfn) == rn)
        break;
    ph = map;
    EWALKMAP()

    if (pph)
        *pph = (Handle)ph;
    return (map);
}

P3(PUBLIC pascal trap, INTEGER, OpenRFPerm, StringPtr, fn, /* IMIV-17 */
   INTEGER, vref, Byte, perm)
{
    INTEGER retval;

    retval = HOpenResFile(vref, 0, fn, perm);
    fprintf(stderr, "trace: OpenRFPerm name='%.*s' vref=%d perm=%d ref=%d ResErr=%d\n", fn ? fn[0] : 0,
            fn ? (char*)fn + 1 : "", vref, perm, retval, Cx(ResErr));
    fflush(stderr);
    return retval;
}

P1(PUBLIC pascal trap, INTEGER, OpenResFile, StringPtr, fn)
{
    return OpenRFPerm(fn, 0, fsCurPerm);
}

P1(PUBLIC pascal trap, void, CloseResFile, INTEGER, rn)
{
    resmaphand map, nextmap;
    INTEGER i, j;
    typref* tr;
    resref* rr;

    invalidate_kchr_ptr();

    ROMlib_invalar();
    if (rn == REF0) {
        for (map = (resmaphand)MR(TopMapHndl); map; map = nextmap) {
            nextmap = (resmaphand)HxP(map, nextmap);
            CloseResFile(Hx(map, resfn));
        }
        while (detached_maps && STARH(detached_maps))
            CloseResFile(Hx(detached_maps, resfn));
        return;
    }
    Handle attached_previous;
    map = ROMlib_rntohandl(rn, &attached_previous);
    resmaphand previous = (resmaphand)attached_previous;
    const int detached = !map;
    if (detached)
        map = find_detached_map(rn, &previous);
    if (map) {
        OSErr save_ResErr;

        UpdateResFile(rn);
        save_ResErr = CW(ResErr);

        unlink_resource_map(map, previous, detached);

        if (!detached && Cx(CurMap) == rn) {
            if (TopMapHndl)
                CurMap = STARH((resmaphand)MR(TopMapHndl))->resfn;
            else
                CurMap = 0;
        }

        /* release individual resource memory */

        WALKTANDR(map, i, tr, j, rr)
        {
            Handle h;

            h = PP_RAW(rr->rhand);
            if (h) {
                h = MR(h);
                ROMlib_forget_font_resource(h);
                if (HIDDEN_RAW((*h)))
                    HClrRBit(h);
                DisposHandle(h);
            }
        }
        EWALKTANDR(tr, rr)

        DisposHandle((Handle)map);
        FSClose(rn);
        ROMlib_setreserr(save_ResErr);
    }
    else
        ROMlib_setreserr(resFNotFound);
}

static INTEGER already_open_res_file(INTEGER swapped_vref, LONGINT swapped_file_num, int data_fork)
{
    resmaphand map;
    fcbrec* fcbp;
    OSErr err;
    INTEGER retval;

    retval = -1;
    WALKMAPTOP(map)
    fcbp = PRNTOFPERR(Hx(map, resfn), &err);
    if (err == noErr && fcbp->fdfnum == swapped_file_num) {
        VCB* vptr;
        vptr = PPR(fcbp->fcvptr);
        if (vptr->vcbVRefNum == swapped_vref && ((fcbp->fcflags & fcfisres) != 0) == !data_fork &&
            (!data_fork || (fcbp->fcflags & fcwriteperm)))
            retval = Hx(map, resfn);
    }
    EWALKMAP()
    return retval;
}

P4(PUBLIC pascal trap, INTEGER, HOpenResFile, INTEGER, vref, LONGINT, dirid, Str255, fn, SignedByte, perm)
{
    INTEGER f;
    HParamBlockRec pbr;
    OSErr err;

    invalidate_kchr_ptr();

    fprintf(stderr, "trace: HOpenResFile begin name='%.*s' vref=%d dirid=%ld perm=%d\n", fn ? fn[0] : 0,
            fn ? (char*)fn + 1 : "", vref, (long)dirid, perm);
    fflush(stderr);
    ROMlib_setreserr(noErr);

    /* check for file already opened */

    {
        CInfoPBRec cpb;
        Str255 local_name;

        str255assign(local_name, fn);
        PP_RAW(pbr.volumeParam.ioNamePtr) = (StringPtr)RM((Ptr)local_name);
        pbr.volumeParam.ioVRefNum = CW(vref);
        pbr.volumeParam.ioVolIndex = CLC(-1);
        err = PBHGetVInfo(&pbr, FALSE);
        if (err) {
            ROMlib_setreserr(err);
            fprintf(stderr, "trace: HOpenResFile PBHGetVInfo failed name='%.*s' vref=%d err=%d\n", fn ? fn[0] : 0,
                    fn ? (char*)fn + 1 : "", vref, err);
            fflush(stderr);
            return -1;
        }

        PP_RAW(cpb.hFileInfo.ioNamePtr) = RM(fn);
        cpb.hFileInfo.ioVRefNum = CW(vref);
        cpb.hFileInfo.ioFDirIndex = CWC(0);
        cpb.hFileInfo.ioDirID = CL(dirid);
        if ((ROMlib_setreserr(PBGetCatInfo(&cpb, 0))) == noErr && perm > fsRdPerm) {
            INTEGER fref;

            fref = already_open_res_file(pbr.volumeParam.ioVRefNum, cpb.hFileInfo.ioDirID, 0);
            if (fref != -1) {
                CurMap = CW(fref);
                /*-->*/ return fref;
            }
        }
    }

    if (ResErr != noErr)
        /*-->*/ return -1;

    ROMlib_invalar();
    PP_RAW(pbr.ioParam.ioNamePtr) = RM(fn);
    pbr.ioParam.ioVRefNum = CW(vref);
    pbr.fileParam.ioFDirIndex = CWC(0);
    pbr.ioParam.ioPermssn = CB(perm);
    pbr.ioParam.ioMisc = CLC(0);
    pbr.fileParam.ioDirID = CL(dirid);
    ROMlib_setreserr(PBHOpenRF(&pbr, FALSE));
    if (ResErr != noErr) {
        fprintf(stderr, "trace: HOpenResFile PBHOpenRF failed name='%.*s' vref=%d dirid=%ld ResErr=%d\n",
                fn ? fn[0] : 0, fn ? (char*)fn + 1 : "", vref, (long)dirid, Cx(ResErr));
        fflush(stderr);
        return (-1);
    }
    f = CW(pbr.ioParam.ioRefNum);
    return ROMlib_open_resource_descriptor(f);
}

/* All resource maps, including the embedded System, use real File Manager
   reference numbers. Guest file/resource APIs can inspect and seek them. */
static int resource_header_valid(const reshead* hd, LONGINT fork_length)
{
    uint32_t map_length = CL(hd->maplen), map_offset = CL(hd->rmapoff);
    uint32_t data_length = CL(hd->datlen), data_offset = CL(hd->rdatoff);
    return fork_length >= 0 && map_length >= sizeof(resmap) + sizeof(INTEGER) && map_offset >= sizeof(reshead) &&
           map_offset <= (uint32_t)fork_length && map_length <= (uint32_t)fork_length - map_offset &&
           data_offset <= (uint32_t)fork_length && data_length <= (uint32_t)fork_length - data_offset;
}

static int data_resource_map_valid(const resmap* header)
{
    const size_t type_offset = CW((uint16_t)header->typoff), name_offset = CW((uint16_t)header->namoff);
    const size_t length = CL(header->rh.maplen), data_length = CL(header->rh.datlen);
    const unsigned char* map = (const unsigned char*)header;
    if (type_offset < sizeof *header || type_offset > length - sizeof(INTEGER) || (type_offset & 1) ||
        (name_offset == UINT16_MAX ? length > INT16_MAX
                                   : (name_offset < type_offset || name_offset > length || name_offset > INT16_MAX)))
        return 0;
    const size_t type_length = (name_offset == UINT16_MAX ? length : name_offset) - type_offset;
    if (type_length < sizeof(INTEGER))
        return 0;

    INTEGER encoded_count;
    memcpy(&encoded_count, map + type_offset, sizeof encoded_count);
    const int type_count = (int16_t)CW(encoded_count) + 1;
    if (type_count < 0 || (size_t)type_count > (type_length - sizeof(INTEGER)) / sizeof(typref))
        return 0;
    const size_t type_table_size = sizeof(INTEGER) + (size_t)type_count * sizeof(typref);
    size_t resource_count = 0;
    const typref* types = (const typref*)(map + type_offset + sizeof(INTEGER));
    for (int i = 0; i < type_count; ++i) {
        const int count = (int16_t)CW(types[i].nres) + 1;
        const int offset = (int16_t)CW(types[i].rloff);
        if (count <= 0 || offset < 0 || (size_t)offset < type_table_size || (size_t)offset > type_length ||
            (size_t)count > (type_length - offset) / sizeof(resref))
            return 0;
        resource_count += count;
        if (resource_count > (type_length - type_table_size) / sizeof(resref))
            return 0;
        const resref* entries = (const resref*)(map + type_offset + offset);
        for (int j = 0; j < count; ++j) {
            const resref* entry = &entries[j];
            const size_t data_offset = ((size_t)entry->doff[0] << 16) | ((size_t)entry->doff[1] << 8) | entry->doff[2];
            if (data_offset > data_length || data_length - data_offset < sizeof(Size))
                return 0;
            const size_t name = CW((uint16_t)entry->noff);
            if (name != UINT16_MAX && name > INT16_MAX)
                return 0;
            if (name != UINT16_MAX && (name_offset == UINT16_MAX || name >= length - name_offset ||
                                       map[name_offset + name] >= length - name_offset - name))
                return 0;
        }
    }
    return 1;
}

static INTEGER open_resource_descriptor(INTEGER f, int data_fork)
{
    reshead hd;
    LONGINT lc;
    resmaphand map;
    INTEGER i, j;
    typref* tr;
    resref* rr;
    OSErr err;
    lc = sizeof(hd);
    ROMlib_setreserr(FSReadAll(f, &lc, (Ptr)&hd));
    if (ResErr != noErr) {
        if (data_fork && CW(ResErr) == eofErr)
            ROMlib_setreserr(mapReadErr);
        FSClose(f);
        return (-1);
    }
    /* Validate the map's fixed header before allocating or writing through it.
     In particular, a zero-length map yields a zero-sized handle, which cannot
     hold the copied resource header below. */
    LONGINT fork_length;
    err = GetEOF(f, &fork_length);
    if (err || !resource_header_valid(&hd, fork_length)) {
        ROMlib_setreserr(err ? err : mapReadErr);
        FSClose(f);
        return -1;
    }
    map = (resmaphand)NewHandleSys(CL(hd.maplen));
    err = MemError();
    if (ROMlib_setreserr(err)) {
        FSClose(f);
        return (-1);
    }

    ROMlib_setreserr(SetFPos(f, fsFromStart, Cx(hd.rmapoff)));
    if (ResErr != noErr) {
        DisposHandle((Handle)map);
        FSClose(f);
        return (-1);
    }
    lc = CL(hd.maplen);
    ROMlib_setreserr(FSReadAll(f, &lc, (Ptr)STARH(map)));
    if (ResErr != noErr) {
        DisposHandle((Handle)map);
        FSClose(f);
        return (-1);
    }

    HxX(map, rh) = hd;

    /* The classic opener requires the 240 reserved bytes. CarbonLib's
       data-fork opener also accepts maps with a shorter reserved area. */
    const size_t minimum_offset = data_fork ? 40 : sizeof(reshead) + sizeof(rsrvrec);
    if (CL(hd.rdatoff) < minimum_offset || Hx(map, typoff) < (int)sizeof(resmap) ||
        (data_fork && (CL(hd.rmapoff) < minimum_offset || !data_resource_map_valid(STARH(map))))) {
        ROMlib_setreserr(mapReadErr);
        DisposHandle((Handle)map);
        FSClose(f);
        return (-1);
    }

    if (data_fork) {
        if (CW((uint16_t)HxX(map, namoff)) == UINT16_MAX)
            HxX(map, namoff) = CW(CL(hd.maplen));
        invalidate_kchr_ptr();
        ROMlib_invalar();
    }
    PP_RAW(HxX(map, nextmap)) = TopMapHndl;
    HxX(map, resfn) = CW(f);
    HLock((Handle)map);
    HNoPurge((Handle)map);
    HIDDEN_RAW_SET(TopMapHndl_H, RM((Handle)map));
    CurMap = CW(f);

    /* Clear every on-disk handle before loading anything. A preloaded compressed
     resource can request a decompressor later in this same map. */

    WALKTANDR(map, i, tr, j, rr)
    PP_RAW(rr->rhand) = 0;
    rr->ratr &= ~resChanged;
    EWALKTANDR(tr, rr)

    WALKTANDR(map, i, tr, j, rr)
    if (rr->ratr & resPreload)
        ROMlib_mgetres(map, rr);
    EWALKTANDR(tr, rr)
    return f;
}

INTEGER ROMlib_open_resource_descriptor(INTEGER refnum)
{
    return open_resource_descriptor(refnum, 0);
}

INTEGER ROMlib_open_data_resource_descriptor(INTEGER refnum)
{
    return open_resource_descriptor(refnum, 1);
}
