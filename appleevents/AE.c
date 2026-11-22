/*+===================================================================
  File:      AE.c

  Summary:   Implements ae support for Macintosh AppleEvent handling.

  Classes:   None.

  Functions: C_AEGetTheCurrentEvent, ROMlib_decode_launch_parameters, ROMlib_send_application_open_event,
             ROMlib_send_document_open_event

  Origin:    Existing repository source in executor/src/appleevents; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1994, 1995, 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* AppleEvents implementation */

#include "rsys/common.h"

#include "EventMgr.h"
#include "NotifyMgr.h"
#include "AppleEvents.h"
#include "MemoryMgr.h"
#include "ProcessMgr.h"
#include "OSEvent.h"
#include "OSUtil.h"
#include "ResourceMgr.h"
#include "WindowMgr.h"
#include "ProcessMgr.h"
#include "AliasMgr.h"
#include "SegmentLdr.h"
#include "rsys/application_event.h"

#include "rsys/mman.h"
#include "rsys/flags.h"
#include "rsys/romaw.h"
#include "rsys/file.h"
#include "rsys/browsercompat.h"
#include "rsys/wind.h"

boolean_t send_application_open_aevt_p;

/* The HLE payload is the public flattened event, rather than our private
   ae_header_t. TeachText 7 reads this representation without using Pack8. */
static OSErr send_open_event(int count, const FSSpec* files)
{
    if (count < 0)
        return paramErr;
    Handle wire = NewHandleClear(count ? 32 : 12);
    if (!wire)
        return memFullErr;
    uint32 header[] = {
        CLC(TICK("aevt")), CLC(0x00010001), CLC(0x3b3b3b3b), CLC(keyDirectObject), CLC(typeAEList), 0, CL(count), 0};
    memcpy(STARH(wire), header, count ? sizeof header : 12);
    OSErr error = noErr;
    for (int i = 1; !error && i <= count; ++i) {
        AppFile file;
        FSSpec spec;
        HIDDEN_AliasHandle encoded_alias = {0};
        if (files)
            spec = files[i - 1];
        else {
            GetAppFiles(i, &file);
            error = FSMakeFSSpec(CW(file.vRefNum), 0, file.fName, &spec);
        }
        if (!error)
            error = NewAlias(NULL, &spec, &encoded_alias);
        AliasHandle alias = HIDDEN_NATIVE(encoded_alias);
        if (!error) {
            Size size = GetHandleSize(alias), old_size = GetHandleSize(wire);
            if (size < 0 || size > INT32_MAX - 9 - old_size)
                error = memFullErr;
            else {
                HLock(alias);
                SetHandleSize(wire, old_size + 8 + ((size + 1) & ~1));
                error = MemError();
                if (!error) {
                    uint32 descriptor[] = {CLC(typeAlias), CL(size)};
                    memcpy(STARH(wire) + old_size, descriptor, 8);
                    memcpy(STARH(wire) + old_size + 8, STARH(alias), size);
                    if (size & 1)
                        STARH(wire)[old_size + 8 + size] = 0;
                }
            }
        }
        if (alias)
            DisposHandle(alias);
    }
    if (!error) {
        Size size = GetHandleSize(wire);
        if (count) {
            uint32 list_size = CL(size - 24);
            memcpy(STARH(wire) + 20, &list_size, 4);
        }
        EventRecord event = {0};
        event.what = CWC(kHighLevelEvent);
        event.message = CLC(kCoreEventClass);
        uint32 event_id = CL(count ? kAEOpenDocuments : kAEOpenApplication);
        memcpy(&event.where, &event_id, 4);
        HLock(wire);
        error = PostHighLevelEvent(&event, NULL, -1, STARH(wire), size, -1);
    }
    DisposHandle(wire);
    return error;
}

OSErr ROMlib_send_application_open_event(int count)
{
    return send_open_event(count, NULL);
}

OSErr ROMlib_send_document_open_event(int count, const FSSpec* files)
{
    if (count <= 0 || !files)
        return paramErr;
    return send_open_event(count, files);
}

static OSErr decode_application_open_event(Handle wire, EventRecord* event, AppleEvent* out)
{
    Size size = GetHandleSize(wire);
    const unsigned char* bytes = (const unsigned char*)STARH(wire);
    uint32 header[8];
    if (size < 12)
        return errAECoercionFail;
    memcpy(header, bytes, size < 32 ? 12 : 32);
    if (header[0] != CLC(TICK("aevt")) || header[1] != CLC(0x00010001) || header[2] != CLC(0x3b3b3b3b))
        return errAECoercionFail;
    ProcessSerialNumber psn;
    AEAddressDesc target = {0};
    AEDescList list = {0};
    GetCurrentProcess(&psn);
    OSErr error = AECreateDesc(typeProcessSerialNumber, (Ptr)&psn, sizeof psn, &target);
    uint32 id;
    memcpy(&id, &event->where, 4);
    if (!error)
        error = AECreateAppleEvent(CL(event->message), CL(id), &target, -1, -1, out);
    if (!error && size != 12) {
        if (size < 32 || header[3] != CLC(keyDirectObject) || header[4] != CLC(typeAEList) ||
            CL(header[5]) != size - 24 || CL(header[6]) > (size - 32) / 8)
            error = errAECoercionFail;
        else
            error = AECreateList(NULL, 0, FALSE, &list);
        size_t offset = 32;
        for (uint32 i = 0; !error && i < CL(header[6]); ++i) {
            uint32 descriptor[2];
            bytes = (const unsigned char*)STARH(wire);
            if (offset > (size_t)size || (size_t)size - offset < 8) {
                error = errAECoercionFail;
                break;
            }
            memcpy(descriptor, bytes + offset, 8);
            uint32 length = CL(descriptor[1]);
            if (descriptor[0] != CLC(typeAlias) || length > (size_t)size - offset - 8) {
                error = errAECoercionFail;
                break;
            }
            HLock(wire);
            error = AEPutPtr(&list, i + 1, typeAlias, (Ptr)bytes + offset + 8, length);
            HUnlock(wire);
            offset += 8 + (size_t)length + (length & 1);
        }
        if (!error && offset != (size_t)size)
            error = errAECoercionFail;
        if (!error)
            error = AEPutKeyDesc(out, keyDirectObject, &list);
    }
    if (PP_RAW(list.dataHandle))
        AEDisposeDesc(&list);
    if (PP_RAW(target.dataHandle))
        AEDisposeDesc(&target);
    if (error && PP_RAW(out->dataHandle))
        AEDisposeDesc(out);
    return error;
}

#if defined(SYN68K)
static syn68k_addr_t gregs_browser_bookmark_window(syn68k_addr_t pc, void* unused)
{
    syn68k_addr_t ret = POPADDR();
    uint32 address = READUL(EM_A7); /* C argument remains for the caller. */
    WindowPtr window = address ? (WindowPtr)SYN68K_TO_US(address) : NULL;

    EM_A0 = 0;
    if (window && (WINDOW_KIND(window) >= userKind || WINDOW_KIND(window) == dialogKind)) {
        StringHandle title = WINDOW_TITLE(window);
        if (!title || !STARH(title) || !ROMlib_gregs_browser_bookmark_dialog_p((unsigned char*)STARH(title))) {
            SetPort((GrafPtr)window);
            EM_A0 = WINDOW_REF_CON(window);
        }
    }
    EM_D0 = EM_A0;
    return ret;
}
#endif

/* dispatching apple events */

static AppleEvent* current_apple_event;

P3(PUBLIC pascal, OSErr, EventHandlerTemplate, AppleEvent*, evt, AppleEvent*, reply, int32, refcon)
{
    /* template for CTOP_... and PTOC_... flags generation */
    abort();
#if !defined(LETGCCWAIL)
    return paramErr;
#endif
}

P1(PUBLIC pascal trap, OSErr, AEProcessAppleEvent, EventRecord*, evtrec)
{
    AEEventID event_id;
    AEEventClass event_class;
    DescType dummy_type;
    Size dummy_size;
    TargetID dummy_target_id;

    AppleEvent* evt = alloca(sizeof *evt);

    HIDDEN_ProcPtr hdlr;
    Handle evt_data;
    Size evt_data_size;
    int32 refcon, dummy_refcon;

    OSErr err;
    OSErr retval;

    /* our current buffer is empty */
    evt_data_size = 0;
    err = AcceptHighLevelEvent(&dummy_target_id, &dummy_refcon, NULL, &evt_data_size);
    if (err != bufferIsSmall)
        AE_RETURN_ERROR(errAEEventNotHandled);

    evt_data = NewHandle(CL(evt_data_size));
    if (MemError() != noErr)
        AE_RETURN_ERROR(MemError());

    LOCK_HANDLE_EXCURSION_1(
        evt_data, { err = AcceptHighLevelEvent(&dummy_target_id, &dummy_refcon, STARH(evt_data), &evt_data_size); });

    if (err != noErr) {
        DisposHandle(evt_data);
        AE_RETURN_ERROR(errAEEventNotHandled);
    }

    DESC_TYPE_X(evt) = CLC(typeAppleEvent);
    FIELD_ASSIGN_RAW(DESC_DATA_X(evt), RM(evt_data));
    if (CL(evt_data_size) >= 4 && !memcmp(STARH(evt_data), "aevt", 4)) {
        AppleEvent decoded = {0};
        err = decode_application_open_event(evt_data, evtrec, &decoded);
        DisposHandle(evt_data);
        if (err)
            AE_RETURN_ERROR(err);
        *evt = decoded;
    }

    err = AEGetAttributePtr(evt, keyEventClassAttr, typeType, &dummy_type, (Ptr)&event_class, sizeof event_class,
                            &dummy_size);
    if (err != noErr) {
        AEDisposeDesc(evt);
        AE_RETURN_ERROR(err);
    }
    event_class = CL(event_class);

    err = AEGetAttributePtr(evt, keyEventIDAttr, typeType, &dummy_type, (Ptr)&event_id, sizeof event_id, &dummy_size);
    if (err != noErr) {
        AEDisposeDesc(evt);
        AE_RETURN_ERROR(err);
    }
    event_id = CL(event_id);

    /* CODE 2 has been relocated by Browser's own segment loader by the
     time it sends its first bookmark notification. */
#if defined(SYN68K)
    if (ROMlib_creator == TICK("ASIT") && event_class == TICK("ASIT") && event_id == TICK("UpdB")) {
        Handle code = GetResource(TICK("CODE"), 2);
        if (code && STARH(code)) {
            static syn68k_addr_t getter;
            SignedByte state = HGetState(code);
            if (!getter)
                getter = callback_install(gregs_browser_bookmark_window, NULL);
            HLock(code);
            if (ROMlib_patch_gregs_browser_bookmarks((unsigned char*)STARH(code), GetHandleSize(code), getter))
                ROMlib_destroy_blocks(US_TO_SYN68K((char*)STARH(code) + GREGS_BROWSER_BOOKMARK_SCAN_OFFSET),
                                      GREGS_BROWSER_BOOKMARK_SCAN_SIZE, TRUE);
            HSetState(code, state);
        }
    }
#endif

    err = AEGetEventHandler(event_class, event_id, &hdlr, &refcon, FALSE);
    if (err != noErr) {
        err = AEGetEventHandler(event_class, event_id, &hdlr, &refcon, TRUE);
        if (err != noErr) {
            AEDisposeDesc(evt);
            AE_RETURN_ERROR(err);
        }
    }
    refcon = CL(refcon);

    {
        AppleEvent* reply = alloca(sizeof *reply);
        AEDesc* target = alloca(sizeof *target);
        ProcessSerialNumber psn;

        GetCurrentProcess(&psn);

        err = AECreateDesc(typeProcessSerialNumber, (Ptr)&psn, sizeof psn, target);

        /* create a reply apple event.  currently unused */
        err = AECreateAppleEvent(kCoreEventClass, kAEAnswer, target,
                                 /* dummy */ -1, /* dummy */ -1, reply);

        AppleEvent* previous = current_apple_event;
        current_apple_event = evt;
        retval = CToPascalCall(HIDDEN_NATIVE(hdlr), CTOP_EventHandlerTemplate, evt, reply, refcon);
        current_apple_event = previous;

        AEDisposeDesc(reply);
    }

    AEDisposeDesc(evt);
    AE_RETURN_ERROR(retval);
}

static OSErr post_self_apple_event(AppleEvent* event, AEEventClass event_class, AEEventID event_id, AESendMode mode)
{
    AppleEvent delivered;
    OSErr error = AEDuplicateDesc(event, &delivered);
    if (error != noErr)
        return error;

    if (!(mode & _kAEInteractMask))
        mode |= kAECanInteract;
    int32 interaction = CL(mode & (_kAEInteractMask | kAECanSwitchLayer | _kAEReplyMask));
    int16 source = CW(kAESameProcess);
    error = AEPutAttributePtr(&delivered, keyInteractLevelAttr, TICK("long"), (Ptr)&interaction, sizeof interaction);
    if (error == noErr)
        error = AEPutAttributePtr(&delivered, keyEventSourceAttr, TICK("shor"), (Ptr)&source, sizeof source);
    if (error == noErr) {
        EventRecord event_record = {0};
        event_record.what = CWC(kHighLevelEvent);
        event_record.message = event_class;
        memcpy(&event_record.where, &event_id, sizeof event_id);
        Handle data = DESC_DATA(&delivered);
        LOCK_HANDLE_EXCURSION_1(
            data, { error = PostHighLevelEvent(&event_record, NULL, -1, STARH(data), GetHandleSize(data), -1); });
    }
    AEDisposeDesc(&delivered);
    return error;
}

P7(PUBLIC pascal trap, OSErr, AESend, AppleEvent*, evt, AppleEvent*, reply, AESendMode, send_mode, AESendPriority,
   send_priority, int32, timeout, IdleProcPtr, idle_proc, EventFilterProcPtr, filter_proc)
{
    AEDesc* target = alloca(sizeof *target);
    DescType target_type, dummy_type;
    Size target_size, dummy_size;

    ProcessSerialNumber* target_psn = alloca(sizeof *target_psn);
    ProcessSerialNumber* current_psn = alloca(sizeof *current_psn);

    AEEventID event_id;
    AEEventClass event_class;

    OSErr err;

    GetCurrentProcess(current_psn);
    err = AEGetAttributePtr(evt, keyAddressAttr, typeWildCard, &target_type, (Ptr)target_psn, sizeof *target_psn,
                            &target_size);
    if (err != noErr)
        /* ### not sure what error we should return here */
        AE_RETURN_ERROR(errAEEventNotHandled);

    target_type = CL(target_type);
    target_size = CL(target_size);

    if (err != noErr)
        AE_RETURN_ERROR(err);

    err = AEGetAttributePtr(evt, keyEventClassAttr, typeType, &dummy_type, (Ptr)&event_class, sizeof event_class,
                            &dummy_size);
    if (err != noErr)
        AE_RETURN_ERROR(err);

    err = AEGetAttributePtr(evt, keyEventIDAttr, typeType, &dummy_type, (Ptr)&event_id, sizeof event_id, &dummy_size);
    if (err != noErr)
        AE_RETURN_ERROR(err);

    if (target_type == keyProcessSerialNumber && target_size == sizeof *target_psn) {
        Boolean same_process_p;

        err = SameProcess(target_psn, current_psn, &same_process_p);
        if (err != noErr)
            AE_RETURN_ERROR(errAEEventNotHandled);

        if (same_process_p) {
            /* we are sending the AppleEvent to ourselves */

            if ((send_mode & _kAEReplyMask) == kAEWaitReply) {
                /* ### ignoring timeout/receipt requests */
                warning_unimplemented("target kCurrentProcess && kAEWaitReply");
                AE_RETURN_ERROR(errAEEventNotHandled);
            }

            if ((send_mode & _kAEReplyMask) == kAEQueueReply) {
                warning_unimplemented("can't handle reply requests yet");
                AE_RETURN_ERROR(errAEEventNotHandled);
            }

            AE_RETURN_ERROR(post_self_apple_event(evt, event_class, event_id, send_mode));
        }
        else {
            /* since there isn't any other processs this could be
             intended for, panic */
            warning_unimplemented("AE target not ProcessSerialNumber");
            AE_RETURN_ERROR((send_mode & _kAEReplyMask) == kAEWaitReply ? errAEEventNotHandled : noErr);
        }
    }
    else {
        /* target is not a psn, we can't deal with anything else at this
	 time */
        if (target_type == typeApplSignature) {
            warning_unimplemented("returning invalidConnection");
            AE_RETURN_ERROR(invalidConnection);
        }

        warning_unimplemented("bad AE target, not ProcessSerialNumber");
        AE_RETURN_ERROR((send_mode & _kAEReplyMask) == kAEWaitReply ? errAEUnknownAddressType : noErr);
    }
}

P1(PUBLIC pascal trap, OSErr, AESuspendTheCurrentEvent, AppleEvent*, evt)
{
    warning_unimplemented(NULL_STRING);
    AE_RETURN_ERROR(noErr);
}

P4(PUBLIC pascal trap, OSErr, AEResumeTheCurrentEvent, AppleEvent*, evt, AppleEvent*, reply, EventHandlerProcPtr,
   dispatcher, int32, refcon)
{
    warning_unimplemented(NULL_STRING);
    AE_RETURN_ERROR(noErr);
}

P1(PUBLIC pascal trap, OSErr, AEGetTheCurrentEvent, AppleEvent*, return_evt)
{
    if (!return_evt)
        AE_RETURN_ERROR(paramErr);

    if (current_apple_event) {
        /* The manager owns the event for the duration of the handler. */
        *return_evt = *current_apple_event;
    }
    else {
        DESC_TYPE_X(return_evt) = CLC(typeNull);
        PP_RAW(return_evt->dataHandle) = 0;
    }
    AE_RETURN_ERROR(noErr);
}

P1(PUBLIC pascal trap, OSErr, AESetTheCurrentEvent, AppleEvent*, evt)
{
    warning_unimplemented(NULL_STRING);
    AE_RETURN_ERROR(noErr);
}

/* array spew */

P7(PUBLIC pascal trap, OSErr, AEGetArray, AEDescList*, list, AEArrayType, array_type, AEArrayDataPointer, array_ptr,
   Size, max_size, DescType*, return_item_type, Size*, return_item_size, int32*, return_item_count)
{
    warning_unimplemented(NULL_STRING);
    AE_RETURN_ERROR(noErr);
}

P6(PUBLIC pascal trap, OSErr, AEPutArray, AEDescList*, list, AEArrayType, type, AEArrayDataPointer, array_data,
   DescType, item_type, Size, item_size, int32, item_count)
{
    warning_unimplemented(NULL_STRING);
    AE_RETURN_ERROR(noErr);
}

/* user interaction settings */

AEInteractionAllowed interaction_level = kAEInteractWithSelf;

P1(PUBLIC pascal trap, OSErr, AESetInteractionAllowed, AEInteractionAllowed, level)
{
    interaction_level = level;
    AE_RETURN_ERROR(noErr);
}

P1(PUBLIC pascal trap, OSErr, AEGetInteractionAllowed, AEInteractionAllowed*, level_out)
{
    *level_out = interaction_level;
    AE_RETURN_ERROR(noErr);
}

P3(PUBLIC pascal trap, OSErr, AEInteractWithUser, int32, timeout, NMRecPtr, nm_req, IdleProcPtr, idle_proc)
{
    if (current_apple_event) {
        DescType type;
        Size size;
        int32 interaction;
        const DescType long_type = TICK("long"), short_type = TICK("shor");
        if (AEGetAttributePtr(current_apple_event, keyInteractLevelAttr, long_type, &type, (Ptr)&interaction,
                              sizeof interaction, &size) == noErr &&
            CL(size) == sizeof interaction && (CL(interaction) & _kAEInteractMask) == kAENeverInteract)
            AE_RETURN_ERROR(errAENoUserInteraction);

        int16 source;
        if (AEGetAttributePtr(current_apple_event, keyEventSourceAttr, short_type, &type, (Ptr)&source, sizeof source,
                              &size) == noErr &&
            CL(size) == sizeof source &&
            ((CW(source) == kAELocalProcess && interaction_level < kAEInteractWithLocal) ||
             (CW(source) == kAERemoteProcess && interaction_level < kAEInteractWithAll)))
            AE_RETURN_ERROR(errAENoUserInteraction);
    }

    /* ROMAW runs a single Macintosh process. There is no other guest
       application to wait on or bring to the front through Notification Manager. */
    ProcessSerialNumber current, front;
    Boolean same;
    if (GetCurrentProcess(&current) || GetFrontProcess(&front, NULL) || SameProcess(&current, &front, &same) || !same)
        AE_RETURN_ERROR(errAETimeout);
    AE_RETURN_ERROR(noErr);
}

P1(PUBLIC pascal trap, OSErr, AEResetTimer, AppleEvent*, evt)
{
    warning_unimplemented(NULL_STRING);
    AE_RETURN_ERROR(noErr);
}

/* suspending and resuming AE handling */

/* type coersion */

P6(PUBLIC pascal, OSErr, CoercePtrTemplate, DescType, data_type, Ptr, data, Size, data_size, DescType, to_type, int32,
   refcon, AEDesc*, desc_out)
{
    /* template for CTOP_... and PTOC_... flags generation */
    abort();
#if !defined(LETGCCWAIL)
    return paramErr;
#endif
}

P4(PUBLIC pascal, OSErr, CoerceDescTemplate, AEDesc*, desc, DescType, to_type, int32, refcon, AEDesc*, desc_out)
{
    /* template for CTOP_... and PTOC_... flags generation */
    abort();
#if !defined(LETGCCWAIL)
    return paramErr;
#endif
}

P5(PUBLIC pascal trap, OSErr, AECoercePtr, DescType, data_type, Ptr, data, Size, data_size, DescType, result_type,
   AEDesc*, desc_out)
{
    HIDDEN_ProcPtr coercion_hdlr;
    int32 refcon;
    Boolean is_desc_hdlr_p;
    OSErr err;

    if (data_type == typeAlias && result_type == typeFSS) {
        AEDesc source = {0};
        err = C_AECreateDesc(data_type, data, data_size, &source);
        if (err == noErr) {
            err = C_AECoerceDesc(&source, result_type, desc_out);
            C_AEDisposeDesc(&source);
        }
        AE_RETURN_ERROR(err);
    }

    if (result_type == typeWildCard || result_type == data_type) {
        AECreateDesc(data_type, data, data_size, desc_out);
        AE_RETURN_ERROR(noErr);
    }

    err = AEGetCoercionHandler(data_type, result_type, &coercion_hdlr, &refcon, &is_desc_hdlr_p, FALSE);
    if (err != noErr) {
        err = AEGetCoercionHandler(data_type, result_type, &coercion_hdlr, &refcon, &is_desc_hdlr_p, TRUE);
        if (err != noErr)
            AE_RETURN_ERROR(errAECoercionFail);
    }

    /* swap things to a normal state */
    refcon = CL(refcon);

    if (is_desc_hdlr_p) {
        descriptor_t* desc = alloca(sizeof *desc);

        err = AECreateDesc(data_type, data, data_size, desc);
        if (err != noErr)
            return memFullErr;

        err = CToPascalCall(HIDDEN_NATIVE(coercion_hdlr), PTOC_CoerceDescTemplate, desc, result_type, refcon, desc_out);

        AEDisposeDesc(desc);
    }
    else {
        err = CToPascalCall(HIDDEN_NATIVE(coercion_hdlr), PTOC_CoercePtrTemplate, data_type, data, data_size,
                            result_type, refcon, desc_out);
    }

    if (err != noErr)
        AE_RETURN_ERROR(errAECoercionFail);

    AE_RETURN_ERROR(noErr);
}

#define AEGetParamDesc AEGetKeyDesc

PRIVATE OSErr parse_evt(const AppleEvent* evtp, AEDesc* desc_out)
{
    OSErr retval;
    AEDesc d;

    /*
   * The cast to AppleEvent * of evtp is so the compiler won't complain
   * about our discarding const.  The problem is AEGetParamDesc doesn't the
   * thing pointed to by its first argument, but the prototype doesn't show
   * this.
   */

    retval = AEGetParamDesc((AppleEvent*)evtp, keyDirectObject, typeAEList, &d);
    if (retval == noErr) {
        LONGINT n;

        retval = AECountItems(&d, &n);
        n = CL(n);
        if (!retval && (n < 0 || n > INT16_MAX))
            retval = paramErr;
        if (retval == noErr) {
            Handle h;
            AppParametersPtr p;

            h = NewHandle(sizeof *p + n * sizeof(FSSpec));
            if (!h || !(p = (typeof(p))STARH(h)))
                retval = MemError();
            else {
                LONGINT l;

                HLock(h);
                p->magic = CLC(APP_PARAMS_MAGIC);
                p->n_fsspec = CW(n);
                for (l = 1; retval == noErr && l <= n; ++l) {
                    AEDesc d2;
                    AEKeyword keyword;

                    retval = AEGetNthDesc(&d, l, typeAlias, &keyword, &d2);
                    if (retval == noErr) {
                        AliasHandle ah;
                        Boolean wasChanged;

                        ah = (AliasHandle)PPR(d2.dataHandle);
                        retval = ResolveAlias(NULL, ah, &p->fsspec[l - 1], &wasChanged);
                        AEDisposeDesc(&d2);
                    }
                }
                HUnlock(h);
                if (retval != noErr)
                    DisposHandle(h);
                else {
                    desc_out->descriptorType = CLC(TICK("appa"));
                    PP_ASSIGN(desc_out->dataHandle, h);
                }
            }
        }
        AEDisposeDesc(&d);
    }
    return retval;
}

/* Processes.h AppParameters is a 16-byte EventRecord, a four-byte refcon,
   a four-byte length, then AETF bytes.  Copy before allocating guest handles:
   the caller's parameter block may itself live in a movable handle. */
OSErr ROMlib_decode_launch_parameters(const void* parameters, Handle* converted)
{
    if (!converted)
        return paramErr;
    *converted = NULL;
    uintptr_t address = (uintptr_t)parameters, base = (uintptr_t)ROMlib_offset;
    if (address < base || address - base > ROMlib_guest_memory_size || ROMlib_guest_memory_size - (address - base) < 24)
        return paramErr;
    const unsigned char* bytes = parameters;
    EventRecord event;
    uint32 length, identifier;
    memcpy(&event, bytes, sizeof event);
    memcpy(&length, bytes + 20, 4);
    memcpy(&identifier, &event.where, 4);
    length = CL(length);
    if (event.message != CLC(kCoreEventClass) ||
        (identifier != CLC(kAEOpenApplication) && identifier != CLC(kAEOpenDocuments)) ||
        length > ROMlib_guest_memory_size - (address - base) - 24 || length > INT32_MAX)
        return paramErr;
    void* copy = malloc(length ? length : 1);
    if (!copy)
        return memFullErr;
    memcpy(copy, bytes + 24, length);
    Handle wire = NewHandle(length);
    if (!wire) {
        free(copy);
        return memFullErr;
    }
    memcpy(STARH(wire), copy, length);
    free(copy);
    AppleEvent decoded = {0};
    AEDesc output = {0};
    OSErr error = decode_application_open_event(wire, &event, &decoded);
    if (!error && identifier == CLC(kAEOpenApplication)) {
        *converted = NewHandleClear(sizeof(ROMlib_AppParameters_t));
        if (!*converted)
            error = memFullErr;
        else
            ((ROMlib_AppParameters_t*)STARH(*converted))->magic = CLC(APP_PARAMS_MAGIC);
    }
    else if (!error) {
        error = parse_evt(&decoded, &output);
        if (!error)
            *converted = DESC_DATA(&output);
    }
    if (PP_RAW(decoded.dataHandle))
        AEDisposeDesc(&decoded);
    DisposHandle(wire);
    return error;
}

P3(PUBLIC pascal trap, OSErr, AECoerceDesc, AEDesc*, desc, DescType, result_type, AEDesc*, desc_out)
{
    HIDDEN_ProcPtr coercion_hdlr;
    int32 refcon;
    Boolean is_desc_hdlr_p;
    DescType desc_type;
    OSErr err;

    desc_type = DESC_TYPE(desc);

    if (desc_type == TICK("aevt") && result_type == TICK("appa")) {
        err = parse_evt(desc, desc_out);
        AE_RETURN_ERROR(err);
    }

    if (result_type == typeWildCard || result_type == desc_type) {
        AEDuplicateDesc(desc, desc_out);
        AE_RETURN_ERROR(noErr);
    }

    err = AEGetCoercionHandler(desc_type, result_type, &coercion_hdlr, &refcon, &is_desc_hdlr_p, FALSE);
    if (err != noErr) {
        err = AEGetCoercionHandler(desc_type, result_type, &coercion_hdlr, &refcon, &is_desc_hdlr_p, TRUE);
        if (err != noErr) {
            if (desc_type == typeAlias && result_type == typeFSS) {
                FSSpec spec;
                Boolean changed;
                err = C_ResolveAlias(NULL, (AliasHandle)DESC_DATA(desc), &spec, &changed);
                if (err == noErr)
                    err = C_AECreateDesc(typeFSS, (Ptr)&spec, sizeof spec, desc_out);
                if (err == noErr)
                    AE_RETURN_ERROR(noErr);
            }
            goto fail;
        }
    }

    /* swap things to a normal state */
    refcon = CL(refcon);

    if (is_desc_hdlr_p) {
        err = CToPascalCall(HIDDEN_NATIVE(coercion_hdlr), PTOC_CoerceDescTemplate, desc, result_type, refcon, desc_out);
    }
    else {
        Handle desc_data;

        desc_data = DESC_DATA(desc);

        LOCK_HANDLE_EXCURSION_1(desc_data, {
            err = CToPascalCall(HIDDEN_NATIVE(coercion_hdlr), PTOC_CoercePtrTemplate, desc_type, STARH(desc_data),
                                GetHandleSize(desc_data), result_type, refcon, desc_out);
        });
    }

    if (err != noErr)
        goto fail;

    AE_RETURN_ERROR(noErr);

fail:
    desc_out->descriptorType = CLC(typeNull);
    PP_RAW(desc_out->dataHandle) = 0;
    AE_RETURN_ERROR(errAECoercionFail);
}

P1(PUBLIC pascal trap, OSErr, AEManagerInfo, LONGINT*, resultp)
{
    OSErr retval;

    *resultp = 0;
    retval = noErr;

    return retval;
}

/* stubs added by Cliff after reading documentation on Apple's web site */

P1(PUBLIC pascal trap, OSErr, AEDisposeToken, AEDesc*, theToken)
{
    warning_unimplemented(NULL_STRING);
    return noErr;
}

P3(PUBLIC pascal trap, OSErr, AEREesolve, AEDesc*, objectSpecifier, INTEGER, callbackFlags, AEDesc*, theToken)
{
    warning_unimplemented(NULL_STRING);
    return noErr;
}

P4(PUBLIC pascal trap, OSErr, AERemoveObjectAccessor, DescType, desiredClass, DescType, containerType, ProcPtr,
   theAccessor, BOOLEAN, isSysHandler)
{
    warning_unimplemented(NULL_STRING);
    return noErr;
}

P5(PUBLIC pascal trap, OSErr, AEInstallObjectAccessor, DescType, desiredClass, DescType, containerType, ProcPtr,
   theAccessor, LONGINT, refcon, BOOLEAN, isSysHandler)
{
    warning_unimplemented(NULL_STRING);
    return noErr;
}

P5(PUBLIC pascal trap, OSErr, AEGetObjectAccessor, DescType, desiredClass, DescType, containerType, ProcPtr*,
   theAccessor, LONGINT*, accessorRefcon, BOOLEAN, isSysHandler)
{
    warning_unimplemented(NULL_STRING);
    return noErr;
}

P6(PUBLIC pascal trap, OSErr, AECallObjectAccessor, DescType, desiredClass, AEDesc*, containerToken, DescType,
   containerClass, DescType, keyForm, AEDesc*, keyData, AEDesc*, theToken)
{
    warning_unimplemented(NULL_STRING);
    return noErr;
}

P7(PUBLIC pascal trap, OSErr, AESetObjectCallbacks, ProcPtr, myCompareProc, ProcPtr, myCountProc, ProcPtr,
   myDisposeTokenProc, ProcPtr, myGetMarkTokenProc, ProcPtr, myMarkProc, ProcPtr, myAdjustMarksProc, ProcPtr,
   myGetErrDescProc)
{
    warning_unimplemented(NULL_STRING);
    return noErr;
}
