/*+===================================================================
  File:      systemnotify.c

  Summary:   Synchronous private system notifications. Registrations retain their A5 context; delivery snapshots the
             list so callbacks can register recursively. The AA77 0300/0301 ABI is established by CarbonLib 1.4's INIT:
             three-long void Pascal callbacks, and long status results from send/register.

  Classes:   None.

  Functions: ROMlib_system_notify, ROMlib_system_notify_register, ROMlib_system_notify_reset

  Origin:    Existing repository source in executor/src/services; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Synchronous private system notifications. Registrations retain their A5
   context; delivery snapshots the list so callbacks can register recursively.
   The AA77 0300/0301 ABI is established by CarbonLib 1.4's INIT: three-long
   void Pascal callbacks, and long status results from send/register. */
#include "rsys/common.h"
#include "rsys/everything.h"
#include "rsys/systemnotify.h"
#include "rsys/ctopflags.h"

typedef struct notification {
    struct notification* next;
    ProcPtr callback;
    uint32 kind, context, saved_a5;
} notification;

static notification* notifications;

PUBLIC int32 ROMlib_system_notify_register(ProcPtr callback, uint32 kind, uint32 context)
{
    notification *p, **tail;
    if (!callback)
        return paramErr;
    for (tail = &notifications; (p = *tail); tail = &p->next)
        if (p->callback == callback && p->kind == kind && p->context == context)
            return noErr;
    p = calloc(1, sizeof *p);
    if (!p)
        return memFullErr;
    p->callback = callback;
    p->kind = kind;
    p->context = context;
    p->saved_a5 = EM_A5;
    *tail = p;
    return noErr;
}

PUBLIC int32 ROMlib_system_notify(uint32 kind, const void* data)
{
    notification* p;
    notification* snapshot;
    size_t count = 0, i = 0;
    uint32 saved_a5 = EM_A5;
    for (p = notifications; p; p = p->next)
        if (p->kind == kind)
            ++count;
    if (!count)
        return noErr;
    if (count > SIZE_MAX / sizeof *snapshot)
        return memFullErr;
    snapshot = malloc(count * sizeof *snapshot);
    if (!snapshot)
        return memFullErr;
    for (p = notifications; p; p = p->next)
        if (p->kind == kind)
            snapshot[i++] = *p;
    for (i = 0; i < count; ++i) {
        EM_A5 = snapshot[i].saved_a5;
        CToPascalCall(snapshot[i].callback, (4 << 3) | (5 << 6) | (4 << 9), kind, data, snapshot[i].context);
    }
    EM_A5 = saved_a5;
    free(snapshot);
    return noErr;
}

PUBLIC void ROMlib_system_notify_reset(void)
{
    while (notifications) {
        notification* p = notifications;
        notifications = p->next;
        free(p);
    }
}
