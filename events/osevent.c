/*+===================================================================
  File:      osevent.c

  Summary:   Implements osevent support for Macintosh event handling and virtual interrupts.

  Classes:   None.

  Functions: GetEvQHdr, ROMlib_PPostEvent, ROMlib_eventinit, ROMlib_get_index_and_bit, ROMlib_kchr_ptr,
             ROMlib_next_user_cancel_event, ROMlib_queue_mouse_wheel, ROMlib_right_to_left_key_map, ROMlib_set_keyboard,
             ROMlib_take_wheel_click, ROMlib_user_cancel_event, ROMlib_xlate, ROMlib_zapmap, display_keyboard_choices,
             geteventelem, invalidate_kchr_ptr, maybe_wait_for_keyup, post_keytrans_key_events

  Origin:    Existing repository source in executor/src/events; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986 - 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in OSEvent.h (DO NOT DELETE THIS LINE) */

/*
 * really should be divided into two sections, just like main.c (that
 * is to say just like main.c should be, it isn't yet)
 */

#include "rsys/common.h"
#include "rsys/application_event.h"
#include "rsys/application_instance.h"

#include "QuickDraw.h"
#include "MemoryMgr.h"
#include "CQuickDraw.h"
#include "ToolboxEvent.h"
#include "OSEvent.h"
#include "EventMgr.h"
#include "OSUtil.h"
#include "ResourceMgr.h"
#include "ProcessMgr.h"
#include "AppleEvents.h"

#include "rsys/cquick.h"
#include "rsys/mman.h"
#include "rsys/arrowkeys.h"
#include "rsys/notmac.h"
#include "rsys/blockinterrupts.h"
#include "rsys/time.h"
#include "rsys/prefs.h"
#include "rsys/vdriver.h"
#include "rsys/host.h"
#include "rsys/segment.h"
#include "rsys/toolevent.h"
#include "rsys/osevent.h"
#include "rsys/ctl.h"
#include "rsys/wind.h"
#include "rsys/mousewheel.h"
#include "rsys/dirtyrect.h"
#include "rsys/stdfile.h"
#include "rsys/system_error.h"

#include "rsys/string.h"
#include "rsys/keyboard.h"
#if defined(SDL)
#include "sdlevents.h"
#endif

#include "DialogMgr.h"
#include "SegmentLdr.h"

#define NEVENT 20

PRIVATE int nevent = 0;
PRIVATE EvQEl evs[NEVENT], *freeelem = evs + NEVENT - 1;

#define ROMlib_curs MouseLocation

PUBLIC INTEGER ROMlib_mods = btnState;
/* Host wheel requests contain no guest pointers. Dispatch only at an event trap
 * boundary, never by calling application code from the SDL event interrupt. */
#define WHEEL_MESSAGE 0x57484C21L

static struct {
    Point where;
    int direction;
    boolean_t horizontal;
} wheel[32];

static unsigned wheel_read, wheel_count;
static boolean_t wheel_click;
static Point wheel_click_point, wheel_restore, wheel_mouse_point;
static boolean_t wheel_restore_pending;

static void finish_wheel_click(void)
{
    /* Real input may have moved the pointer since this synthetic click began. */
    if (wheel_restore_pending && MouseLocation.h == CW(wheel_mouse_point.h) &&
        MouseLocation.v == CW(wheel_mouse_point.v)) {
        MouseLocation.h = MouseLocation2.h = CW(wheel_restore.h);
        MouseLocation.v = MouseLocation2.v = CW(wheel_restore.v);
    }
    wheel_restore_pending = wheel_click = FALSE;
}

void ROMlib_queue_mouse_wheel(Point where, int direction, boolean_t horizontal)
{
    unsigned index;
    if (wheel_count == NELEM(wheel))
        return;
    index = (wheel_read + wheel_count++) % NELEM(wheel);
    wheel[index].where = where;
    wheel[index].direction = direction;
    wheel[index].horizontal = horizontal;
}

boolean_t ROMlib_take_wheel_click(Point local)
{
    GrafPtr port = thePort;
    boolean_t match;
    match = wheel_click && local.h - CW(PORT_BOUNDS(port).left) == wheel_click_point.h &&
            local.v - CW(PORT_BOUNDS(port).top) == wheel_click_point.v;
    if (match)
        wheel_click = FALSE;
    return match;
}

static void dispatch_mouse_wheel(void)
{
    /* Leave room for an inseparable down/up pair and avoid displacing real input.
   * The queue's mouse-up also bounds tracking, even if an app ignores the down. */
    while (wheel_count && !PP_RAW(EventQueue.qHead) && !wheel_restore_pending) {
        unsigned index = wheel_read;
        WindowPtr window = C_FrontWindow();
        ControlHandle control;
        Point where = wheel[index].where;
        wheel_read = (wheel_read + 1) % NELEM(wheel);
        --wheel_count;
        if (!window || !(ROMlib_mods & btnState) || !C_PtInRgn(where, WINDOW_CONT_REGION(window)))
            continue;
        for (control = WINDOW_CONTROL_LIST(window); control; control = CTL_NEXT_CONTROL(control)) {
            INTEGER id = -1, saved_error = ResErr;
            ResType type = 0;
            Rect rect;
            int x, y;
            Handle definition = CTL_DEFPROC(control);
            if (!definition || !CTL_VIS(control) || CTL_HILITE(control) == 255 || CTL_MAX(control) <= CTL_MIN(control))
                continue;
            C_GetResInfo(definition, &id, &type, NULL);
            ResErr = saved_error;
            /* Standard scrollBarProc is CDEF 1; custom controls retain their own
             behavior and are not guessed from the shape of a button. */
            if (CW(id) != 1 || CL(type) != TICK("CDEF"))
                continue;
            rect = CTL_RECT(control);
            if (!ROMlib_wheel_arrow(CW(rect.left), CW(rect.top), CW(rect.right), CW(rect.bottom),
                                    wheel[index].horizontal, wheel[index].direction, &x, &y))
                continue;
            where.h = x - CW(PORT_BOUNDS(window).left);
            where.v = y - CW(PORT_BOUNDS(window).top);
            wheel_restore = wheel[index].where;
            wheel_mouse_point = where;
            wheel_restore_pending = TRUE;
            ROMlib_PPostEvent(mouseDown, WHEEL_MESSAGE, NULL, TickCount(), where, ROMlib_mods & ~btnState);
            ROMlib_PPostEvent(mouseUp, WHEEL_MESSAGE, NULL, TickCount(), where, ROMlib_mods | btnState);
            break;
        }
    }
}

PRIVATE LONGINT autoticks;
PRIVATE LONGINT lastdown = -1;

PUBLIC short ROMlib_pinned = FALSE;

PRIVATE Ptr kchr_ptr;

PUBLIC void invalidate_kchr_ptr(void)
{
    kchr_ptr = 0;
}

PRIVATE INTEGER kchr_id = 0;

PUBLIC Ptr ROMlib_kchr_ptr(void)
{
    if (!kchr_ptr) {
        ZONE_SAVE_EXCURSION(SysZone, {
            Handle kchr_hand;

            /* Host input must not re-enter guest Resource Manager patches while
           a virtual interrupt is being serviced. */
            kchr_hand = C_GetResource(TICK("KCHR"), kchr_id);
            gui_assert(kchr_hand);
            C_LoadResource(kchr_hand);
            HLock(kchr_hand);
            kchr_ptr = STARH(kchr_hand);
        });
    }
    return kchr_ptr;
}

PUBLIC boolean_t ROMlib_set_keyboard(const char* keyboardname)
{
    Handle new_h;

    ZONE_SAVE_EXCURSION(SysZone, {
        Str255 pkeyboardname;

        str255_from_c_string(pkeyboardname, keyboardname);
        new_h = GetNamedResource(TICK("KCHR"), pkeyboardname);
        if (new_h) {
            GetResInfo(new_h, &kchr_id, 0, 0);
            kchr_id = CW(kchr_id);
            LoadResource(new_h);
            if (kchr_ptr) {
                Handle kchr_hand;

                kchr_hand = RecoverHandle(kchr_ptr);
                HUnlock(kchr_hand);
            }
            HLock(new_h);
            kchr_ptr = STARH(new_h);
        }
    });
    return !!new_h;
}

PRIVATE boolean_t map_right_to_left = TRUE;

PUBLIC uint16 ROMlib_right_to_left_key_map(uint16 what)
{
    uint16 retval;

    retval = what;
    if (map_right_to_left)
        switch (what) {
        default:
            break;
        case MKV_RIGHTSHIFT:
            retval = MKV_LEFTSHIFT;
            break;
        case MKV_RIGHTOPTION:
            retval = MKV_LEFTOPTION;
            break;
        case MKV_RIGHTCNTL:
            retval = MKV_LEFTCNTL;
            break;
        }
    return retval;
}

/*
 * NOTE: we figure out the value for a down keystroke, then we just remember
 * what we figured out and return that value on the up.  This probably isn't
 * how the Mac does it, but it's probably close enough.  Largely this
 * routine is just a wrapper for KeyTrans now.  See IMV for an explanation
 * of what's going on here.
 */

PUBLIC LONGINT ROMlib_xlate(INTEGER virt, INTEGER modifiers, boolean_t down_p)
{
    static uint16 down_value[VIRT_MASK + 1];
    LONGINT retval;

    if (!down_p)
        retval = down_value[virt & VIRT_MASK];
    else {
        static LONGINT state;

        retval = KeyTrans(ROMlib_kchr_ptr(), modifiers | (virt & VIRT_MASK), &state);
        down_value[virt & VIRT_MASK] = (retval >> 16 ? retval >> 16 : retval & 0xFFFF);
    }
    return retval;
}

A1(PUBLIC, void, ROMlib_eventinit, boolean_t, graphics_valid_p) /* INTERNAL */
{
    static int beenhere = 0;
    EvQEl *p, *ep;

    wheel_read = wheel_count = 0;
    wheel_click = wheel_restore_pending = FALSE;

    if (!beenhere) {
        MouseLocation.h = 0;
        MouseLocation.v = 0;
        MouseLocation2.h = 0;
        MouseLocation2.v = 0;
        ScrDmpEnb = TRUE;
        FIELD_ASSIGN_RAW(evs[0].qLink, 0); /* end of the line */
        beenhere = 1;
        for (p = evs + 1, ep = evs + NEVENT; p != ep; p++)
            FIELD_ASSIGN_RAW(p->qLink, RM((QElemPtr)(p - 1)));
        SysEvtMask = CWC(~(1L << keyUp)); /* EVERYTHING except keyUp */
        if (graphics_valid_p) {
            Rect* main_gd_bounds;

            main_gd_bounds = &GD_BOUNDS(MR(MainDevice));
        }
    }
}

A1(PRIVATE, void, dropevent, EvQEl*, qp)
{
    Dequeue((QElemPtr)qp, &EventQueue);
    FIELD_ASSIGN_RAW(qp->qLink, RM((QElemPtr)freeelem));
    freeelem = qp;
    nevent--;
}

/* A private Dialog Manager poll must leave unrelated queued keys intact. */
BOOLEAN
ROMlib_user_cancel_event(const EventRecord* event)
{
    return event && CW(event->what) == keyDown &&
           ((CL(event->message) & 0xffff) == 0x351b ||
            ((CW(event->modifiers) & cmdKey) && (CL(event->message) & 255) == '.'));
}

BOOLEAN
ROMlib_next_user_cancel_event(EventRecord* event)
{
    virtual_int_state_t blocked = block_virtual_ints();
    for (EvQEl* entry = (EvQEl*)PPR(EventQueue.qHead); entry; entry = (EvQEl*)PPR(entry->qLink))
        if (ROMlib_user_cancel_event((EventRecord*)&entry->evtQWhat)) {
            if (event)
                *event = *(EventRecord*)&entry->evtQWhat;
            dropevent(entry);
            restore_virtual_ints(blocked);
            return TRUE;
        }
    restore_virtual_ints(blocked);
    return FALSE;
}

EvQEl* geteventelem(void)
{
    EvQEl* retval = freeelem;

    if (nevent == NEVENT) {
        dropevent((EvQEl*)PPR(EventQueue.qHead));
        retval = freeelem;
    }
    freeelem = (EvQEl*)PPR(freeelem->qLink);
    nevent++;
    return retval;
}

PUBLIC boolean_t ROMlib_get_index_and_bit(LONGINT loc, int* indexp, uint8* bitp)
{
    boolean_t retval;

    if (loc < 0 || loc / 8 >= sizeof_KeyMap)
        retval = FALSE;
    else {
        retval = TRUE;
        *indexp = loc / 8;
        *bitp = (1 << (loc % 8));
    }
    return retval;
}

A2(PUBLIC, void, ROMlib_zapmap, LONGINT, loc, LONGINT, val)
{
    int i;
    uint8 bit;

    if (ROMlib_get_index_and_bit(loc, &i, &bit)) {
        if (val)
            KeyMap[i] |= bit;
        else
            KeyMap[i] &= ~(bit);
    }
}

PRIVATE boolean_t key_down(uint8 loc)
{
    boolean_t retval;
    int i;
    uint8 bit;

    if (!ROMlib_get_index_and_bit(loc, &i, &bit))
        retval = FALSE;
    else
        retval = !!(KeyMap[i] & bit);
    return retval;
}

A3(PUBLIC trap, OSErrRET, PPostEvent, INTEGER, evcode, /* IMIV-85 */
   LONGINT, evmsg, HIDDEN_EvQElPtr*, qelp)
{
    EvQEl* qp;
    LONGINT tmpticks;

    /*
   * Here is where the portable autokey stuff should go
   * If it is a keyUp event, clear autoticks, if it is
   * a keyDown then set the appropriate bugger ...
   *
   * Now that the portable code is here, SCO stuff is out of date
   */

    /*
   * NOTE:  The code below isn't strictly correct, since IMI-260 says
   * you can have at most 2 non modifier keys down at one time.
   */

    tmpticks = TickCount();
    if (evcode == keyUp) {
        ROMlib_zapmap((evmsg >> 8) & 0xFF, 0);
        if (!(evmsg & 0xff)) {
            if (qelp)
                HIDDEN_RAW_SET(*(qelp), 0);
            return noErr;
        }
        lastdown = -1;
    }
    else if (evcode == keyDown) {
        ROMlib_zapmap((evmsg >> 8) & 0xFF, 1);
        if (!(evmsg & 0xff)) {
            if (qelp)
                HIDDEN_RAW_SET(*(qelp), 0);
            return noErr;
        }
        lastdown = evmsg;
        autoticks = tmpticks + Cx(KeyThresh);

        if ((evmsg & 0xff) == '2' && /* cmd-shift-2 */
            key_down(MKV_CLOVER) && (key_down(MKV_LEFTSHIFT) || key_down(MKV_RIGHTSHIFT)))

            dofloppymount();
    }

    if (!((1 << evcode) & Cx(SysEvtMask)))
        /*-->*/ return evtNotEnb;
    qp = geteventelem();
    qp->evtQWhat = CW(evcode);
    qp->evtQMessage = CL(evmsg);
    qp->evtQWhen = CL(tmpticks);
    qp->evtQWhere = ROMlib_curs;
    qp->evtQModifiers = CW(ROMlib_mods);
    Enqueue((QElemPtr)qp, &EventQueue);
    if (qelp)
        HIDDEN_RAW_SET((*qelp), qp);
    return noErr;
}

A3(PRIVATE, OSErrRET, _PPostEvent, INTEGER, evcode, LONGINT, evmsg, HIDDEN_EvQElPtr*, qelpp)
{
    OSErrRET ret;
    ProcPtr proc;
    HIDDEN_EvQElPtr retquelp;

    proc = (ProcPtr)ostraptable[0x2F];

    ret = PPostEvent(evcode, evmsg, &retquelp);

    if (qelpp)
        *qelpp = retquelp;
    return ret;
}

A6(PUBLIC, OSErrRET, ROMlib_PPostEvent, INTEGER, evcode, LONGINT, evmsg, HIDDEN_EvQElPtr*, qelp, LONGINT, when, Point,
   where, INTEGER, butmods)
{
    MouseLocation2.h = ROMlib_curs.h = CW(where.h);
    MouseLocation2.v = ROMlib_curs.v = CW(where.v);
    ROMlib_mods = butmods;
    if (evcode == mouseDown || evcode == mouseUp || evcode == keyDown || evcode == keyUp)
        ROMlib_power_note_activity(ROMlib_power_user_activity, when);

    return _PPostEvent(evcode, evmsg, qelp);
}

A2(PUBLIC trap, OSErrRET, PostEvent, INTEGER, evcode, LONGINT, evmsg)
{
    return _PPostEvent(evcode, evmsg, (HIDDEN_EvQElPtr*)0);
}

A2(PUBLIC trap, void, FlushEvents, INTEGER, evmask, INTEGER, stopmask) /* II-69 */
{
    EvQEl *qp, *next;
    int x;
    virtual_int_state_t block;

    block = block_virtual_ints();
    for (qp = (EvQEl*)PPR(EventQueue.qHead); qp && !((x = 1 << Cx(qp->evtQWhat)) & stopmask); qp = next) {
        next = (EvQEl*)PPR(qp->qLink); /* save before dropping event */
        if (x & evmask) {
            if (qp->evtQWhat == CWC(mouseUp) && qp->evtQMessage == CLC(WHEEL_MESSAGE)) {
                finish_wheel_click();
            }
            dropevent(qp);
        }
    }
    restore_virtual_ints(block);
    /* NOTE:  According to IMII-69 we should be leaving stuff in d0 */
}

PUBLIC BOOLEAN ROMlib_bewaremovement;

PUBLIC int ROMlib_refresh = 0;

A3(PRIVATE, BOOLEAN, OSEventCommon, INTEGER, evmask, EventRecord*, eventp, BOOLEAN, dropit)
{
    EvQEl* qp;
    virtual_int_state_t block;
    BOOLEAN retval;
    static Point oldpoint = {-1, -1};
    LONGINT ticks;

#if defined(SDL)
    /* The previous event's resume handler has returned to the event loop. */
    sdl_finish_resume();
#endif
    /* We tend to call this routine from various ROMlib modal loops, so this
   * is a good place to check for timer interrupts, etc. */
    check_virtual_interrupt();

    /* Applications such as TeachText read the initial document event directly,
     without registering an AEInstallEventHandler callback. Do not withhold
     their documents while waiting for a handler they will never install. */
    INTEGER launch_message = 0, launch_count = 0;
    if (send_application_open_aevt_p)
        CountAppFiles(&launch_message, &launch_count);
    if (send_application_open_aevt_p && (application_accepts_open_app_aevt_p || CW(launch_count) > 0)) {
        if (ROMlib_send_application_open_event(CW(launch_count)) == noErr)
            send_application_open_aevt_p = FALSE;
    }
    ROMlib_poll_application_instance();

    eventp->message = CLC(0);
    ROMlib_memnomove_p = FALSE; /* this is an icky hack needed for Excel */
    ticks = TickCount();

#if defined(X)
    /* if we are running on a version of linux that doesn't support
     SIGIO this will handle events (although not asynchronously) */

    if (x_event_pending_p())
        post_pending_x_events(/* dummy */ -1, /* dummy */ NULL);

#endif /* X */

#if defined(SDL)
    /* if we are running SDL with the event thread disabled... */
    handle_sdl_events(/* dummy */ -1, /* dummy */ NULL);

#endif /* SDL */

    /* Applications that never request mouse-up must not retain a synthetic
     release forever. A subsequent mouse-down request is a new click cycle. */
    if (wheel_restore_pending && wheel_click && (evmask & mDownMask) && !(evmask & mUpMask) &&
        PP_RAW(EventQueue.qHead)) {
        EvQEl* head = (EvQEl*)PPR(EventQueue.qHead);
        if (head->evtQWhat == CWC(mouseUp) && head->evtQMessage == CLC(WHEEL_MESSAGE)) {
            dropevent(head);
            finish_wheel_click();
        }
    }
    dispatch_mouse_wheel();

#if defined(CYGWIN32) && !defined(SDL)
    /* Run the Win32 event loop, since currently we don't do this
     in a separate thread. */
    process_win32_events();

#endif /* CYGWIN32 */

    block = block_virtual_ints();
    for (qp = (EvQEl*)PPR(EventQueue.qHead); qp && !((1 << Cx(qp->evtQWhat)) & evmask); qp = (EvQEl*)PPR(qp->qLink))
        ;
    if (qp) {
        *eventp = *(EventRecord*)(&qp->evtQWhat);
        if (dropit) {
            if (eventp->what == CWC(mouseDown)) {
                wheel_click = eventp->message == CLC(WHEEL_MESSAGE);
                wheel_click_point.h = CW(eventp->where.h);
                wheel_click_point.v = CW(eventp->where.v);
            }
            else if (eventp->what == CWC(mouseUp) && eventp->message == CLC(WHEEL_MESSAGE)) {
                finish_wheel_click();
            }
#if defined(SDL)
            if (eventp->what == CWC(osEvt) && (CL(eventp->message) & 0xff000001) == (SUSPENDRESUMEBITS | RESUME))
                sdl_resume_event_delivered();
#endif
            dropevent(qp);
        }
        if ((eventp->what == CWC(mouseDown) || eventp->what == CWC(mouseUp)) && eventp->message == CLC(WHEEL_MESSAGE))
            eventp->message = 0;
        retval = TRUE;
    }
    else {
        eventp->when = CL(TickCount());

        {
#if defined(X)
            if (!ROMlib_pinned) {
                LONGINT x, y;
                LONGINT newmods;

                querypointerX(&x, &y, &newmods);
                eventp->where.h = MouseLocation2.h = ROMlib_curs.h = CW(x);
                eventp->where.v = MouseLocation2.v = ROMlib_curs.v = CW(y);
            }
            else
#endif
                MouseLocation2 = eventp->where = ROMlib_curs;
        }

        eventp->modifiers = CW(ROMlib_mods);
        if ((evmask & autoKeyMask) && lastdown != -1 && ticks > autoticks) {
            autoticks = ticks + Cx(KeyRepThresh);
            eventp->what = CWC(autoKey);
            eventp->message = CL(lastdown);
            retval = TRUE;
        }
        else {
            eventp->what = CWC(nullEvent);
            retval = FALSE;
        }
    }
    restore_virtual_ints(block);
    if (eventp->where.h != oldpoint.h || eventp->where.v != oldpoint.v) {
        oldpoint = eventp->where;
        if (ROMlib_bewaremovement) {
            ROMlib_showhidecursor();
            ROMlib_bewaremovement = FALSE;
        }
    }
    if (ROMlib_when == WriteInOSEvent) {
        dirty_rect_update_screen();
    }
    else if (ROMlib_when == WriteAtEndOfTrap) {
        dirty_rect_update_screen();
    }

    return retval;
}

A2(PUBLIC trap, BOOLEANRET, GetOSEvent, INTEGER, evmask, EventRecord*, eventp)
{
    return OSEventCommon(evmask, eventp, TRUE);
}

A2(PUBLIC trap, BOOLEANRET, OSEventAvail, INTEGER, evmask, EventRecord*, eventp)
{
    return OSEventCommon(evmask, eventp, FALSE);
}

A1(PUBLIC trap, void, SetEventMask, INTEGER, evmask)
{
    SysEvtMask = CW(evmask);
}

A0(PUBLIC, QHdrPtr, GetEvQHdr)
{
    return &EventQueue;
}

PUBLIC void post_keytrans_key_events(INTEGER evcode, LONGINT keywhat, int32 when, Point where, uint16 button_state,
                                     unsigned char virt)
{
    INTEGER first_key, second_key;

    first_key = keywhat >> 16;
    second_key = keywhat;

    if (first_key) {
        ROMlib_PPostEvent(evcode, (virt << 8) | first_key, 0, when, where, button_state);
        if (second_key)
            ROMlib_PPostEvent(keyUp, (virt << 8) | first_key, 0, when, where, button_state);
    }
    if (second_key || !first_key)
        ROMlib_PPostEvent(evcode, (virt << 8) | second_key, 0, when, where, button_state);
}

PRIVATE int compare(const void* p1, const void* p2)
{
    int retval;

    retval = ROMlib_strcmp(p1, p2);
    return retval;
}

PUBLIC void display_keyboard_choices(void)
{
    INTEGER nres, i, nfound;
    unsigned char(*names)[256];

    vdriver_shutdown();
    printf("Available keyboard maps:\n");
    SetResLoad(FALSE);
    nres = CountResources(TICK("KCHR"));
    names = alloca(nres * sizeof(*names));
    nfound = 0;
    for (i = 1; i <= nres; ++i) {
        Handle h;

        h = GetIndResource(TICK("KCHR"), i);
        if (h) {
            GetResInfo(h, 0, 0, (StringPtr)names[nfound]);
            ++nfound;
        }
    }
    qsort(names, nfound, sizeof(names[0]), compare);

    for (i = 0; i < nfound; ++i)
        printf("%.*s\n", names[i][0], (char*)&names[i][1]);

    exit(0);
}

PUBLIC void maybe_wait_for_keyup(void)
{
#if defined(SDL) && defined(CYGWIN32)
    /* Run SDL's event processor so that any pending events get
     sent to us instead of the Win32 print stuff.  Specifically
     we don't want to lose the key-up if someone hit <CR> to
     choose the default button in a dialog.  Losing the key-up
     can cause all sorts of trouble. */
    while (lastdown != -1)
        handle_sdl_events(0, NULL);
#endif
}
