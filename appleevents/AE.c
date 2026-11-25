/*+===================================================================
  File:      AE.c

  Summary:   Deliver AppleEvents, coerce descriptors and dispatch native object accessors and token callbacks.

  Classes:   None.

  Functions: C_AEGetTheCurrentEvent, ROMlib_decode_launch_parameters, ROMlib_send_application_open_event,
             ROMlib_send_document_open_event, ROMlib_ae_dispatch_reinit, ROMlib_ae_object_init, ROMlib_ae_object_reinit,
             C_AEDisposeToken, C_AEREesolve, C_AERemoveObjectAccessor, C_AEInstallObjectAccessor,
             C_AEGetObjectAccessor, C_AECallObjectAccessor, C_AESetObjectCallbacks

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

typedef struct ae_suspended_event {
    struct ae_suspended_event* next;
    AppleEvent event, reply;
    bool dispatching;
} ae_suspended_event;

typedef struct {
    AppleEvent* event;
    ae_suspended_event* suspended;
} ae_dispatch_scope;

static ae_dispatch_scope* active_dispatch;
static ae_suspended_event *suspended_events, *manual_event;

enum {
    ae_ignore_app_handler = 0x00000002,
    ae_ignore_system_handler = 0x00000008,
    ae_keep_descriptors = 0x80000000,
    ae_standard_dispatch = UINT32_MAX,
};

static AppleEvent* current_apple_event(void)
{
    return active_dispatch ? active_dispatch->event : manual_event ? &manual_event->event : NULL;
}

static bool same_apple_event(const AppleEvent* left, const AppleEvent* right)
{
    return left && right && DESC_TYPE(left) == typeAppleEvent && DESC_TYPE(right) == typeAppleEvent &&
           DESC_DATA(left) && DESC_DATA(left) == DESC_DATA(right);
}

static ae_suspended_event** suspended_apple_event(const AppleEvent* event)
{
    ae_suspended_event** link = &suspended_events;
    while (*link && ((*link)->dispatching || !same_apple_event(&(*link)->event, event)))
        link = &(*link)->next;
    return link;
}

void ROMlib_ae_dispatch_reinit(void)
{
    /* AE_reinit runs after the old application's heap has been reset. */
    while (suspended_events) {
        ae_suspended_event* event = suspended_events;
        suspended_events = event->next;
        free(event);
    }
    manual_event = NULL;
    active_dispatch = NULL;
}

static OSErr find_event_handler(AEEventClass event_class, AEEventID event_id, uint32_t mask, HIDDEN_ProcPtr* handler,
                                int32* refcon)
{
    OSErr status = errAEHandlerNotFound;
    if (!(mask & ae_ignore_app_handler))
        status = AEGetEventHandler(event_class, event_id, handler, refcon, FALSE);
    if (status && !(mask & ae_ignore_system_handler))
        status = AEGetEventHandler(event_class, event_id, handler, refcon, TRUE);
    return status;
}

static OSErr invoke_event_handler(AppleEvent* event, AppleEvent* reply, ProcPtr handler, int32 refcon, bool dispose)
{
    ae_dispatch_scope* previous = active_dispatch;
    ae_dispatch_scope scope = {.event = event};
    active_dispatch = &scope;
    const OSErr status = CToPascalCall(handler, CTOP_EventHandlerTemplate, event, reply, refcon);
    active_dispatch = previous;
    if (scope.suspended) {
        scope.suspended->event = *event;
        scope.suspended->reply = *reply;
        scope.suspended->dispatching = false;
    }
    else if (dispose) {
        AEDisposeDesc(reply);
        AEDisposeDesc(event);
    }
    return status;
}

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

    err = find_event_handler(event_class, event_id, 0, &hdlr, &refcon);
    if (err != noErr) {
        AEDisposeDesc(evt);
        AE_RETURN_ERROR(err);
    }
    refcon = CL(refcon);

    {
        AppleEvent* reply = alloca(sizeof *reply);
        AEDesc* target = alloca(sizeof *target);
        ProcessSerialNumber psn;

        err = GetCurrentProcess(&psn);
        if (!err)
            err = AECreateDesc(typeProcessSerialNumber, (Ptr)&psn, sizeof psn, target);
        if (!err) {
            err = AECreateAppleEvent(kCoreEventClass, kAEAnswer, target, -1, -1, reply);
            AEDisposeDesc(target);
        }
        if (err) {
            AEDisposeDesc(evt);
            AE_RETURN_ERROR(err);
        }
        retval = invoke_event_handler(evt, reply, HIDDEN_NATIVE(hdlr), refcon, true);
    }
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
    if (!evt)
        AE_RETURN_ERROR(paramErr);
    if (active_dispatch && same_apple_event(active_dispatch->event, evt) && !active_dispatch->suspended) {
        active_dispatch->suspended = malloc(sizeof *active_dispatch->suspended);
        if (!active_dispatch->suspended)
            AE_RETURN_ERROR(memFullErr);
        active_dispatch->suspended->dispatching = true;
        active_dispatch->suspended->next = suspended_events;
        suspended_events = active_dispatch->suspended;
    }
    AE_RETURN_ERROR(noErr);
}

P4(PUBLIC pascal trap, OSErr, AEResumeTheCurrentEvent, AppleEvent*, evt, AppleEvent*, reply, EventHandlerProcPtr,
   dispatcher, int32, refcon)
{
    if (!evt)
        AE_RETURN_ERROR(paramErr);
    ae_suspended_event** link = suspended_apple_event(evt);
    if (!*link)
        AE_RETURN_ERROR(errAEEventNotHandled);
    ae_suspended_event* state = *link;
    if (reply && (DESC_TYPE(reply) != DESC_TYPE(&state->reply) || DESC_DATA(reply) != DESC_DATA(&state->reply)))
        AE_RETURN_ERROR(errAEReplyNotValid);

    const bool standard = dispatcher == (EventHandlerProcPtr)SYN68K_TO_US(ae_standard_dispatch) ||
                          (uintptr_t)dispatcher == UINTPTR_MAX;
    const bool dispose = !(standard && ((uint32_t)refcon & ae_keep_descriptors));
    OSErr status = noErr;
    if (standard) {
        AEEventClass event_class = 0;
        AEEventID event_id = 0;
        DescType type;
        Size size;
        status = AEGetAttributePtr(&state->event, keyEventClassAttr, typeType, &type, (Ptr)&event_class,
                                   sizeof event_class, &size);
        if (!status && CL(size) != sizeof event_class)
            status = errAEWrongDataType;
        if (!status)
            status = AEGetAttributePtr(&state->event, keyEventIDAttr, typeType, &type, (Ptr)&event_id, sizeof event_id,
                                       &size);
        if (!status && CL(size) != sizeof event_id)
            status = errAEWrongDataType;
        if (!status) {
            HIDDEN_ProcPtr handler;
            int32 handler_refcon;
            status = find_event_handler(CL(event_class), CL(event_id), (uint32_t)refcon, &handler, &handler_refcon);
            if (!status) {
                dispatcher = HIDDEN_NATIVE(handler);
                refcon = CL(handler_refcon);
            }
        }
    }
    *link = state->next;
    if (manual_event == state)
        manual_event = NULL;
    _Alignas(16) AppleEvent event = state->event, response = state->reply;
    free(state);
    if (dispatcher && !status)
        status = invoke_event_handler(&event, &response, dispatcher, refcon, dispose);
    else if (dispose) {
        AEDisposeDesc(&response);
        AEDisposeDesc(&event);
    }
    AE_RETURN_ERROR(status);
}

P1(PUBLIC pascal trap, OSErr, AEGetTheCurrentEvent, AppleEvent*, return_evt)
{
    if (!return_evt)
        AE_RETURN_ERROR(paramErr);

    const AppleEvent* event = current_apple_event();
    if (event) {
        /* The manager owns the event for the duration of the handler. */
        *return_evt = *event;
    }
    else {
        DESC_TYPE_X(return_evt) = CLC(typeNull);
        PP_RAW(return_evt->dataHandle) = 0;
    }
    AE_RETURN_ERROR(noErr);
}

P1(PUBLIC pascal trap, OSErr, AESetTheCurrentEvent, AppleEvent*, evt)
{
    if (!evt)
        AE_RETURN_ERROR(paramErr);
    ae_suspended_event* event = *suspended_apple_event(evt);
    if (!event)
        AE_RETURN_ERROR(errAEEventNotHandled);
    manual_event = event;
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
    AppleEvent* event = current_apple_event();
    if (event) {
        DescType type;
        Size size;
        int32 interaction;
        const DescType long_type = TICK("long"), short_type = TICK("shor");
        if (AEGetAttributePtr(event, keyInteractLevelAttr, long_type, &type, (Ptr)&interaction,
                              sizeof interaction, &size) == noErr &&
            CL(size) == sizeof interaction && (CL(interaction) & _kAEInteractMask) == kAENeverInteract)
            AE_RETURN_ERROR(errAENoUserInteraction);

        int16 source;
        if (AEGetAttributePtr(event, keyEventSourceAttr, short_type, &type, (Ptr)&source, sizeof source,
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

P2(PUBLIC pascal trap, OSErr, AEManagerInfo, AEKeyword, keyword, LONGINT*, resultp)
{
    if (!resultp)
        AE_RETURN_ERROR(paramErr);
    switch (keyword) {
    case T('v', 'e', 'r', 's'):
        *resultp = CLC(0x01058000); /* System 9 PACK 8's NumVersion. */
        break;
    case T('r', 'e', 'c', 'r'):
        *resultp = 0; /* No Apple Event recorder is registered. */
        break;
    default:
        AE_RETURN_ERROR(paramErr);
    }
    AE_RETURN_ERROR(noErr);
}

/* Object accessor records are host-owned; their procedures remain guest UPPs.
   Application records are reset with the application's AppleEvent tables. */
typedef struct AE_object_accessor {
    struct AE_object_accessor* next;
    DescType desired_class;
    DescType container_type;
    ProcPtr procedure;
    LONGINT refcon;
} AE_object_accessor;

typedef struct {
    AE_object_accessor* application;
    AE_object_accessor* system;
    ProcPtr callbacks[7];
    BOOLEAN initialized;
} AE_object_state;

static AE_object_state* object_state;

OSErr ROMlib_ae_object_init(void)
{
    if (!object_state)
        object_state = calloc(1, sizeof *object_state);
    if (!object_state)
        return memFullErr;
    object_state->initialized = TRUE;
    return noErr;
}

void ROMlib_ae_object_reinit(void)
{
    if (object_state) {
        while (object_state->application) {
            AE_object_accessor* record = object_state->application;
            object_state->application = record->next;
            free(record);
        }
        memset(object_state->callbacks, 0, sizeof object_state->callbacks);
        object_state->initialized = FALSE;
    }
}

static AE_object_accessor** object_accessor_slot(DescType desired_class, DescType container_type, BOOLEAN system)
{
    AE_object_accessor** slot = system ? &object_state->system : &object_state->application;
    while (*slot && ((*slot)->desired_class != desired_class || (*slot)->container_type != container_type))
        slot = &(*slot)->next;
    return slot;
}

P1(PUBLIC pascal trap, OSErr, AEDisposeToken, AEDesc*, theToken)
{
    if (!theToken)
        return paramErr;
    if (!object_state || !object_state->initialized)
        return paramErr;
    if (object_state->callbacks[2] && CL(theToken->descriptorType) != typeNull) {
        OSErr err = CToPascalCall(object_state->callbacks[2], PTOC_AEDisposeToken, theToken);
        if (err != errAEEventNotHandled)
            return err;
    }
    return C_AEDisposeDesc(theToken);
}

P4(PUBLIC pascal trap, OSErr, AERemoveObjectAccessor, DescType, desiredClass, DescType, containerType, ProcPtr,
   theAccessor, BOOLEAN, isSysHandler)
{
    if (!object_state || !object_state->initialized)
        return paramErr;
    AE_object_accessor** slot = object_accessor_slot(desiredClass, containerType, isSysHandler);
    AE_object_accessor* record = *slot;
    if (!record || (theAccessor && record->procedure != theAccessor))
        return -1723; /* errAEAccessorNotFound. */
    *slot = record->next;
    free(record);
    return noErr;
}

P5(PUBLIC pascal trap, OSErr, AEInstallObjectAccessor, DescType, desiredClass, DescType, containerType, ProcPtr,
   theAccessor, LONGINT, refcon, BOOLEAN, isSysHandler)
{
    if (!theAccessor)
        return paramErr;
    if (!object_state || !object_state->initialized)
        return paramErr;
    AE_object_accessor** slot = object_accessor_slot(desiredClass, containerType, isSysHandler);
    if (!*slot) {
        *slot = calloc(1, sizeof **slot);
        if (!*slot)
            return memFullErr;
        (*slot)->desired_class = desiredClass;
        (*slot)->container_type = containerType;
    }
    (*slot)->procedure = theAccessor;
    (*slot)->refcon = refcon;
    return noErr;
}

P5(PUBLIC pascal trap, OSErr, AEGetObjectAccessor, DescType, desiredClass, DescType, containerType, HIDDEN_ProcPtr*,
   theAccessor, LONGINT*, accessorRefcon, BOOLEAN, isSysHandler)
{
    if (!theAccessor || !accessorRefcon)
        return paramErr;
    if (!object_state || !object_state->initialized)
        return paramErr;
    AE_object_accessor* record = *object_accessor_slot(desiredClass, containerType, isSysHandler);
    if (!record)
        return -1723; /* errAEAccessorNotFound. */
    HIDDEN_RAW_SET(*theAccessor, RM(record->procedure));
    *accessorRefcon = CL(record->refcon);
    return noErr;
}

static OSErr call_object_accessor(DescType desired_class, AEDesc* container, DescType container_class, DescType form,
                                  AEDesc* key, AEDesc* token, BOOLEAN wildcards)
{
    if (!container || !key || !token)
        return paramErr;
    token->descriptorType = CLC(typeNull);
    PP_RAW(token->dataHandle) = 0;
    if (!object_state || !object_state->initialized)
        return paramErr;
    DescType container_type = CL(container->descriptorType);
    for (int system = 0; system < 2; ++system) {
        const DescType classes[] = {desired_class, desired_class, typeWildCard, typeWildCard};
        const DescType containers[] = {container_type, typeWildCard, container_type, typeWildCard};
        for (unsigned i = 0; i < (wildcards ? 4 : 1); ++i) {
            AE_object_accessor* record = *object_accessor_slot(classes[i], containers[i], system);
            if (record) {
                /* A callback may replace or remove its own registration. */
                ProcPtr procedure = record->procedure;
                LONGINT refcon = record->refcon;
                OSErr err = CToPascalCall(procedure, 0x0096C962, desired_class, container, container_class, form, key,
                                          token, refcon);
                if (err != errAEEventNotHandled)
                    return err;
            }
        }
    }
    return -1723; /* errAEAccessorNotFound. */
}

P6(PUBLIC pascal trap, OSErr, AECallObjectAccessor, DescType, desiredClass, AEDesc*, containerToken, DescType,
   containerClass, DescType, keyForm, AEDesc*, keyData, AEDesc*, theToken)
{
    return call_object_accessor(desiredClass, containerToken, containerClass, keyForm, keyData, theToken, FALSE);
}

P7(PUBLIC pascal trap, OSErr, AESetObjectCallbacks, ProcPtr, myCompareProc, ProcPtr, myCountProc, ProcPtr,
   myDisposeTokenProc, ProcPtr, myGetMarkTokenProc, ProcPtr, myMarkProc, ProcPtr, myAdjustMarksProc, ProcPtr,
   myGetErrDescProc)
{
    if (!object_state || !object_state->initialized)
        return paramErr;
    ProcPtr callbacks[] = {myCompareProc, myCountProc,       myDisposeTokenProc, myGetMarkTokenProc,
                           myMarkProc,    myAdjustMarksProc, myGetErrDescProc};
    for (unsigned i = 0; i < 7; ++i)
        if (callbacks[i])
            object_state->callbacks[i] = callbacks[i];
    return noErr;
}

static OSErr object_type_key(AEDesc* record, AEKeyword keyword, DescType* value)
{
    AEDesc field = {CLC(typeNull), {0}};
    OSErr err = C_AEGetKeyDesc(record, keyword, typeWildCard, &field);
    if (!err) {
        Handle data = DESC_DATA(&field);
        if (CL(field.descriptorType) != typeType || !data || GetHandleSize(data) != sizeof *value)
            err = errAEWrongDataType;
        else {
            memcpy(value, STARH(data), sizeof *value);
            *value = CL(*value);
        }
    }
    C_AEDisposeDesc(&field);
    return err;
}

static OSErr resolve_object(AEDesc* specifier, AEDesc* token, unsigned depth)
{
    if (depth == 64)
        return errAEEventNotHandled;
    if (CL(specifier->descriptorType) != TICK("obj "))
        return C_AEDuplicateDesc(specifier, token);
    /* Callback arguments must be guest-addressable, including intermediate
       descriptors. Host C stack addresses cannot be passed through a UPP. */
    AEDesc* scratch = (AEDesc*)NewPtrClear(3 * sizeof *scratch);
    if (!scratch)
        return memFullErr;
    AEDesc* container = scratch;
    AEDesc* key = scratch + 1;
    AEDesc* container_token = scratch + 2;
    for (unsigned i = 0; i < 3; ++i)
        scratch[i].descriptorType = CLC(typeNull);
    /* An object specifier contains an AERecord payload with a distinct type.
       Normalize only a borrowed descriptor, leaving the caller's type intact. */
    AEDesc record = *specifier;
    record.descriptorType = CLC(typeAERecord);
    DescType desired_class, form;
    OSErr err = object_type_key(&record, TICK("want"), &desired_class);
    if (!err)
        err = object_type_key(&record, TICK("form"), &form);
    if (!err)
        err = C_AEGetKeyDesc(&record, TICK("from"), typeWildCard, container);
    if (!err)
        err = C_AEGetKeyDesc(&record, TICK("seld"), typeWildCard, key);
    if (!err)
        err = resolve_object(container, container_token, depth + 1);
    /* Whose tests and range expansion require the comparison/count/mark
       protocols. Report them as unsupported rather than return a bogus token. */
    if (!err && (form == TICK("test") || form == TICK("rang")))
        err = errAEEventNotHandled;
    DescType container_class = CL(container->descriptorType);
    if (!err && container_class == TICK("obj ")) {
        AEDesc container_record = *container;
        container_record.descriptorType = CLC(typeAERecord);
        err = object_type_key(&container_record, TICK("want"), &container_class);
    }
    if (!err)
        err = call_object_accessor(desired_class, container_token, container_class, form, key, token, TRUE);
    C_AEDisposeToken(container_token);
    C_AEDisposeDesc(container);
    C_AEDisposeDesc(key);
    DisposPtr((Ptr)scratch);
    return err;
}

P3(PUBLIC pascal trap, OSErr, AEREesolve, AEDesc*, objectSpecifier, INTEGER, callbackFlags, AEDesc*, theToken)
{
    if (!object_state || !object_state->initialized || !objectSpecifier || !theToken || objectSpecifier == theToken)
        return paramErr;
    theToken->descriptorType = CLC(typeNull);
    PP_RAW(theToken->dataHandle) = 0;
    if (CL(objectSpecifier->descriptorType) != TICK("obj "))
        return -1727; /* errAENotAnObjectSpec. */
    OSErr err = resolve_object(objectSpecifier, theToken, 0);
    if (err) {
        C_AEDisposeToken(theToken);
        theToken->descriptorType = CLC(typeNull);
        PP_RAW(theToken->dataHandle) = 0;
    }
    return err;
}
