/*+===================================================================
  File:      hle.c

  Summary:   Implements hle support for Macintosh event handling and virtual interrupts.

  Classes:   None.

  Functions: ROMlib_hle_exchange_current, hle_get_event, hle_init, hle_reinit, hle_reset

  Origin:    Existing repository source in executor/src/events; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"

#include "OSEvent.h"
#include "MemoryMgr.h"
#include "ProcessMgr.h"

typedef struct hle_q_elt {
    struct hle_q_elt* next;
    HighLevelEventMsgPtr hle_msg;
    TargetID sender;
} hle_q_elt_t;

static hle_q_elt_t* hle_q;
/* Consumed messages may be temporarily held by Carbon's event queue. Keep
   their existing queue nodes until the current-message context releases them. */
static hle_q_elt_t* hle_owned;

/* event q element currently being processed */
static HighLevelEventMsgPtr current_hle_msg;
static boolean_t current_hle_owned;

void hle_init(void)
{
    hle_q = NULL;
    hle_owned = NULL;
    current_hle_msg = NULL;
    current_hle_owned = FALSE;
}

void hle_reinit(void)
{
    hle_q = NULL;
    hle_owned = NULL;
    current_hle_msg = NULL;
    current_hle_owned = FALSE;
}

void hle_reset(void)
{
    if (current_hle_msg == NULL)
        return;

    /* EventAvail only borrows a message still owned by the queue. A consumed
     event transfers ownership here until the next event operation. */
    if (current_hle_owned) {
        hle_q_elt_t **link = &hle_owned, *node;
        while (*link && (*link)->hle_msg != current_hle_msg)
            link = &(*link)->next;
        node = *link;
        if (node)
            *link = node->next;
        DisposPtr((Ptr)MR(current_hle_msg->theMsgEvent.when));
        DisposPtr((Ptr)current_hle_msg);
        if (node)
            DisposPtr((Ptr)node);
    }

    current_hle_msg = NULL;
    current_hle_owned = FALSE;
}

boolean_t hle_get_event(EventRecord* evt, boolean_t remflag)
{
    if (hle_q) {
        hle_q_elt_t* t;

        if (current_hle_msg != NULL) {
            warning_unexpected("current_hle_msg != NULL_STRING");
            hle_reset();
        }

        current_hle_msg = hle_q->hle_msg;
        current_hle_owned = remflag;
        *evt = current_hle_msg->theMsgEvent;

        t = hle_q;
        if (remflag) {
            hle_q = hle_q->next;
            t->next = hle_owned;
            hle_owned = t;
        }

        return TRUE;
    }

    return FALSE;
}

HighLevelEventMsgPtr ROMlib_hle_exchange_current(HighLevelEventMsgPtr replacement)
{
    HighLevelEventMsgPtr previous = current_hle_msg;
    boolean_t owned = FALSE;
    hle_q_elt_t* node;
    if (replacement) {
        for (node = hle_owned; node && node->hle_msg != replacement; node = node->next)
            ;
        if (node)
            owned = TRUE;
        else {
            for (node = hle_q; node && node->hle_msg != replacement; node = node->next)
                ;
            if (!node)
                return NULL; /* A foreign or already released message. */
        }
    }
    current_hle_msg = replacement;
    current_hle_owned = owned;
    return previous;
}

P4(PUBLIC pascal trap, OSErr, AcceptHighLevelEvent, TargetID*, sender_id_return, int32*, refcon_return, Ptr, msg_buf,
   int32*, msg_buf_length_return)
{
    OSErr retval = noErr;

    if (current_hle_msg == NULL)
        return noOutstandingHLE;

    if (sender_id_return) {
        hle_q_elt_t* node;
        for (node = hle_owned; node && node->hle_msg != current_hle_msg; node = node->next)
            ;
        if (!node)
            for (node = hle_q; node && node->hle_msg != current_hle_msg; node = node->next)
                ;
        if (node)
            *sender_id_return = node->sender;
        else
            memset(sender_id_return, 0, sizeof *sender_id_return);
    }
    *refcon_return = current_hle_msg->userRefCon;

    if (CL(*msg_buf_length_return) < CL(current_hle_msg->msgLength))
        retval = bufferIsSmall;

    if (retval == noErr)
        memcpy(msg_buf, (Ptr)MR(current_hle_msg->theMsgEvent.when), CL(current_hle_msg->msgLength));

    *msg_buf_length_return = current_hle_msg->msgLength;

    return retval;
}

P3(PUBLIC pascal trap, Boolean, GetSpecificHighLevelEvent, GetSpecificFilterProcPtr, fn, Ptr, data, OSErr*, err_return)
{
    hle_q_elt_t *t, **prev;

    for (prev = &hle_q, t = hle_q; t; prev = &t->next, t = t->next) {
        Boolean evt_handled_p;

        evt_handled_p = CToPascalCall(fn, CTOP_GetSpecificHighLevelEventProcTemplate, t->hle_msg,
                                      /* ##### target id */
                                      NULL);
        if (evt_handled_p) {
            *prev = t->next;
            return TRUE;
        }
    }

    return FALSE;
}

P3(PUBLIC pascal, Boolean, GetSpecificHighLevelEventProcTemplate, Ptr, data, HighLevelEventMsgPtr, hle_msg, TargetID*,
   target_id)
{
    /* template for CTOP_... and PTOC_... flags generation */
    abort();
#if !defined(LETGCCWAIL)
    return FALSE;
#endif
}

P6(PUBLIC pascal trap, OSErr, PostHighLevelEvent, EventRecord*, evt, Ptr, receiver_id, int32, refcon, Ptr, msg_buf,
   int32, msg_length, int32, post_options)
{
    HighLevelEventMsgPtr hle_msg;
    Ptr msg_buf_copy;
    hle_q_elt_t *t, *elt;
    OSErr retval;

    hle_msg = (HighLevelEventMsgPtr)NewPtr(sizeof *hle_msg);
    if (MemError() != noErr) {
        retval = MemError();
        goto done;
    }

    hle_msg->HighLevelEventMsgHeaderlength = CWC(0);
    hle_msg->version = CWC(0);
    hle_msg->reserved1 = -1; /* All-one fields are byte-order invariant. */
    hle_msg->theMsgEvent = *evt;

    /* #### copy the message buffer? */
    msg_buf_copy = NewPtr(msg_length);
    if (MemError() != noErr) {
        retval = MemError();
        DisposPtr((Ptr)hle_msg);
        goto done;
    }
    memcpy(msg_buf_copy, msg_buf, msg_length);
    hle_msg->theMsgEvent.when = (int32)RM(msg_buf_copy);
    hle_msg->theMsgEvent.modifiers = -1;

    hle_msg->userRefCon = CL(refcon);
    hle_msg->postingOptions = CL(post_options);
    hle_msg->msgLength = CL(msg_length);

    /* stick the new msg */
    elt = (hle_q_elt_t*)NewPtr(sizeof *t);
    if (MemError() != noErr) {
        retval = MemError();
        DisposPtr((Ptr)MR(hle_msg->theMsgEvent.when));
        DisposPtr((Ptr)hle_msg);
        goto done;
    }

    if (hle_q == NULL)
        hle_q = elt;
    else {
        for (t = hle_q; t->next != NULL; t = t->next)
            ;
        t->next = elt;
    }
    elt->next = NULL;
    elt->hle_msg = hle_msg;
    memset(&elt->sender, 0, sizeof elt->sender);
    ProcessSerialNumber serial;
    if (C_GetCurrentProcess(&serial) == noErr &&
        C_GetPortNameFromProcessSerialNumber(&elt->sender.name, &serial) == noErr)
        elt->sender.recvrName = elt->sender.name;
    retval = noErr;

done:
    return retval;
}
