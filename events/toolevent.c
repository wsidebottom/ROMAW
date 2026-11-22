/*+===================================================================
  File:      toolevent.c

  Summary:   Implements toolevent support for Macintosh event handling and virtual interrupts.

  Classes:   None.

  Functions: GetCaretTime, GetDblTime, ROMlib_alarmoffmbar, ROMlib_circledefault, ROMlib_post_quit_event,
             ROMlib_power_last_activity, ROMlib_power_note_activity,
             ROMlib_send_quit, dofloppymount, saveprefvalues, sendcopy, sendpaste, sendsuspendevent

  Origin:    Existing repository source in executor/src/events; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1987 - 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ToolboxEvent.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "rsys/romaw.h"
#include "rsys/mixed_mode.h"
#include "rsys/speech.h"
#include "rsys/fileplay.h"
#include "rsys/process.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "EventMgr.h"
#include "WindowMgr.h"
#include "OSUtil.h"
#include "ToolboxEvent.h"
#include "ToolboxUtil.h"
#include "FileMgr.h"
#include "MemoryMgr.h"
#include "DeskMgr.h"
#include "SysErr.h"
#include "BinaryDecimal.h"
#include "SegmentLdr.h"
#include "AppleEvents.h"
#include "ProcessMgr.h"

#include "rsys/cquick.h"
#include "rsys/wind.h"
#include "rsys/ctl.h"
#include "rsys/hfs.h"
#include "rsys/resource.h"
#include "rsys/notmac.h"
#include "rsys/stdfile.h"
#include "rsys/prefs.h"
#include "rsys/configprefs.h"
#include "rsys/options.h"
#include "rsys/prefpanel.h"
#include "rsys/aboutpanel.h"
#include "rsys/sounddriver.h"
#include "rsys/segment.h"
#include "rsys/version.h"
#include "rsys/syncint.h"
#include "rsys/vbl.h"
#include "rsys/osutil.h"
#include "rsys/osevent.h"
#include "rsys/blockinterrupts.h"
#include "rsys/keyboard.h"
#include "rsys/parse.h"
#include "rsys/refresh.h"
#include "rsys/parseopt.h"
#include "rsys/vdriver.h"
#include "rsys/aboutbox.h"
#include "rsys/redrawscreen.h"
#include "rsys/toolevent.h"
#include "rsys/menu.h"

/* #define	EVENTTRACE */

#if defined(EVENTTRACE)
LONGINT eventstate = 0;
#define TRACE(n) (eventstate = (n))
#else /* !defined(EVENTTRACE) */
#define TRACE(n)
#endif /* !defined(EVENTTRACE) */

PRIVATE BOOLEAN ROMlib_alarmonmbar = FALSE;

PUBLIC int ROMlib_delay = 0; /* number of ticks to wait when we
				haven't gotten anything interesting */

#define ALARMSICN -16385

PUBLIC BOOLEAN ROMlib_beepedonce = FALSE;

A0(PRIVATE, void, ROMlib_togglealarm)
{
    Handle alarmh;
    static unsigned char hard_coded_alarm[] = {
#include "generated/alarm.inc"
    };

    /* rectangle of alarm on the screen */
    static Rect screen_alarm_rect = {CWC(2), CWC(16), CWC(18), CWC(32)};

    BitMap src_alarm_bitmap = {/* the baseAddr field of the src_alarm bitmap will either be
			   the hard coded alarm, or the resource 'sicn' */
                               NULL,
                               CWC(2),
                               {CWC(0), CWC(0), CWC(16), CWC(16)}};

    static INTEGER alarm_bits[16];
    BitMap save_alarm_bitmap = {(Ptr)RM(&alarm_bits[0]), CWC(2), {CWC(0), CWC(0), CWC(16), CWC(16)}};

    if (ROMlib_alarmonmbar) {
        CopyBits(&save_alarm_bitmap, (BitMap*)STARH(GD_PMAP(MR(TheGDevice))), &save_alarm_bitmap.bounds,
                 &screen_alarm_rect, srcCopy, NULL);
        ROMlib_alarmonmbar = FALSE;
    }
    else {
        if (ROMlib_shouldalarm()) {
            if (!ROMlib_beepedonce) {
                SysBeep(5);
                ROMlib_beepedonce = TRUE;
            }
            /* save the bits underneath */
            /* draw sicon -16385 up there */
            if ((alarmh = ROMlib_getrestid(TICK("SICN"), ALARMSICN)))
                FIELD_ASSIGN_RAW(src_alarm_bitmap.baseAddr, HIDDEN_RAW(*alarmh));
            else
                /* once again, we need to move the (Ptr) cast
			   inside the CL () because it confuses gcc */
                FIELD_ASSIGN_RAW(src_alarm_bitmap.baseAddr, RM((Ptr)hard_coded_alarm));

            /* save the screen to save_alarm_bitmap */
            CopyBits((BitMap*)STARH(GD_PMAP(MR(TheGDevice))), &save_alarm_bitmap, &screen_alarm_rect,
                     &save_alarm_bitmap.bounds, srcCopy, NULL);
            /* and copy the new alarm to the screen */
            CopyBits(&src_alarm_bitmap, (BitMap*)STARH(GD_PMAP(MR(TheGDevice))), &src_alarm_bitmap.bounds,
                     &screen_alarm_rect, srcCopy, NULL);
            ROMlib_alarmonmbar = TRUE;
        }
    }
}

A0(PUBLIC, void, ROMlib_alarmoffmbar)
{
    if (ROMlib_alarmonmbar)
        ROMlib_togglealarm();
}

P3(PUBLIC pascal trap, LONGINT, KeyTrans, Ptr, mapp, unsigned short, code, LONGINT*, state)
{
    LONGINT ascii;
    int table_num;
    unsigned char virt;
    kchr_ptr_t p;
    int table_num_index;

    p = (kchr_ptr_t)mapp;
    virt = code & VIRT_MASK;

    table_num_index = (code >> MODIFIER_SHIFT) & MODIFIER_MASK;
    table_num = CB((KCHR_MODIFIER_TABLE_X(p))[table_num_index]);
    ascii = (unsigned char)CB(KCHR_TABLE_X(p)[table_num][virt]);

    if (*state == 0) {
        if (ascii)
            *state = 0;
        else {
            int n_dead;
            dead_key_rec_t* deadp;

            n_dead = KCHR_N_DEAD_KEY_RECS(p);
            deadp = KCHR_DEAD_KEY_RECS_X(p);
            while (--n_dead >= 0 && (DEAD_KEY_TABLE_NUMBER(deadp) != table_num || DEAD_KEY_VIRT_KEY(deadp) != virt))
                deadp = (dead_key_rec_t*)(&DEAD_KEY_NO_MATCH_X(deadp) + 1);
            if (n_dead >= 0)
                *state = (char*)deadp - (char*)&KCHR_N_TABLES_X(p);
            else
                *state = 0;
        }
    }
    else {
        dead_key_rec_t* deadp;
        completer_t* completep;
        int n_recs, i;

        deadp = (dead_key_rec_t*)((char*)&KCHR_N_TABLES_X(p) + *state);
        *state = 0;
        completep = &DEAD_KEY_COMPLETER_X(deadp);
        n_recs = COMPLETER_N_RECS(completep);
        for (i = 0; (i < n_recs && (CB((COMPLETER_COMPLETER_RECS_X(completep))[i].to_look_for) != ascii)); ++i)
            ;
        if (i < n_recs)
            ascii = (unsigned char)CB((COMPLETER_COMPLETER_RECS_X(completep)[i]).replacement);
        else
            ascii = (ascii << 16) | (unsigned short)DEAD_KEY_NO_MATCH(deadp);
    }
    return ascii;
}

PUBLIC void ROMlib_circledefault(DialogPtr dp)
{
    INTEGER type;
    HIDDEN_Handle h;
    Rect r;
    GrafPtr saveport;

    saveport = thePort;
    SetPort(dp);
    GetDItem(dp, 1, &type, &h, &r);
    PenSize(3, 3);
    InsetRect(&r, -4, -4);
    if (!(ROMlib_options & ROMLIB_RECT_SCREEN_BIT))
        ROMlib_frame_default_button(&r);
    else
        FrameRect(&r);
    PenSize(1, 1);
    SetPort(saveport);
}

PUBLIC void dofloppymount(void)
{
#if !defined(LINUX) && !defined(CYGWIN32)
    SysBeep(5);
#else
    futzwithdosdisks();
#endif
}

#define C_STRING_FROM_SYSTEM_VERSION()                                                        \
    ({                                                                                        \
        char* retval;                                                                         \
        retval = alloca(9);                                                                   \
        sprintf(retval, "%d.%d.%d", (system_version >> 8) & 0xF, (system_version >> 4) & 0xF, \
                (system_version >> 0) & 0xF);                                                 \
        retval;                                                                               \
    })

int saveprefvalues(char* savefilename, LONGINT locationx, LONGINT locationy)
{
    int retval;
    FILE* fp;

    if ((fp = tmpfile())) {
        ROMlib_preferences_write_string(fp, "Comments", ROMlib_Comments);
        ROMlib_preferences_write_string(fp, "WindowName", ROMlib_WindowName);
        fprintf(fp, "BitsPerPixel=%d\n", PIXMAP_PIXEL_SIZE(GD_PMAP(MR(MainDevice))));

        fprintf(fp, "ScreenLocation=%ld,%ld\n",
                (long)(ROMlib_ScreenLocation.first != INITIALPAIRVALUE ? ROMlib_ScreenLocation.first : locationx),
                (long)(ROMlib_ScreenLocation.first != INITIALPAIRVALUE ? ROMlib_ScreenLocation.second : locationy));

        fprintf(fp, "SystemVersion=%s\n", C_STRING_FROM_SYSTEM_VERSION());
        fprintf(fp, "RefreshNumber=%d\n", ROMlib_refresh);
        fprintf(fp, "Delay=%d\n", ROMlib_delay);
        switch (ROMlib_when) {
        case WriteAlways:
        case WriteInBltrgn:
            fprintf(fp, "BlitOften=1\n");
            break;
        default:
        case WriteNever:
        case WriteInOSEvent:
            fprintf(fp, "BlitInOSEvent=1\n");
            break;
        case WriteAtEndOfTrap:
            fprintf(fp, "BlitAtTrapEnd=1\n");
            break;
        }
        if (ROMlib_passpostscript)
            fprintf(fp, "PassPostscript=1\n");
        if (ROMlib_newlinetocr)
            fprintf(fp, "NewLineToCR=1\n");
        if (ROMlib_directdiskaccess)
            fprintf(fp, "DirectDiskAccess=1\n");
        if (ROMlib_nowarn32 != 0)
            fprintf(fp, "NoWarn32=1\n");
        if (ROMlib_flushoften != 0)
            fprintf(fp, "FlushOften=1\n");

        if (ROMlib_pretend_help)
            fprintf(fp, "PretendHelp=1\n");
        if (ROMlib_pretend_edition)
            fprintf(fp, "PretendEdition=1\n");
        if (ROMlib_pretend_script)
            fprintf(fp, "PretendScript=1\n");
        if (ROMlib_pretend_alias)
            fprintf(fp, "PretendAlias=1\n");

        ROMlib_extra_preferences_save(fp);
        retval = !ferror(fp);
        if (retval)
            retval = config_prefs_store(savefilename, ROMlib_configsection, fp);
        if (fclose(fp))
            retval = FALSE;
    }
    else
        retval = FALSE;
    return retval;
}

void ROMlib_show_preferences(void)
{
#if defined(SDL)
    static BOOLEAN active;
    ROMlib_extra_preferences prefs;
    if (active || (ROMlib_options & ROMLIB_NOPREFS_BIT))
        return;
    active = TRUE;
    ROMlib_extra_preferences_init(&prefs);
#if defined(CYGWIN32)
    if (ROMlib_win_preferences(&prefs, ROMlib_configfilename) < 0)
#endif
        ROMlib_sdl_preferences(&prefs, ROMlib_configfilename);
    active = FALSE;
#endif
}

PRIVATE void doquitreallyquits(void)
{
    ROMlib_exit = !ROMlib_exit;
}

A3(PRIVATE, BOOLEAN, doevent, INTEGER, em, EventRecord*, evt, BOOLEAN, remflag) /* no DA support */
{
    EventRecord discarded_event;
    BOOLEAN retval;
    LONGINT now;
    static int beenhere = 0;
    ALLOCABEGIN

    /* Some older applications drain the queue without requesting a record. */
    if (!evt)
        evt = &discarded_event;

    /* We tend to call this routine from various ROMlib modal loops, so this
	 * is a good place to check for timer interrupts. */
    check_virtual_interrupt();

#if defined(SDL)
    if (remflag)
        sdl_apply_pending_resize();
#endif

    hle_reset();

    evt->message = CLC(0);
    TRACE(2);
    if (SPVolCtl & 0x80) {
        TRACE(3);
        GetDateTime(&now);
        now = CL(now);
        TRACE(4);
        if ((ULONGINT)now >= (ULONGINT)Cx(SPAlarm)) {
            TRACE(5);
            if (now & 1) {
                TRACE(6);
                if (ROMlib_alarmonmbar)
                    ROMlib_togglealarm();
            }
            else {
                TRACE(7);
                if (!ROMlib_alarmonmbar)
                    ROMlib_togglealarm();
            }
            TRACE(8);
        }
        else if (ROMlib_alarmonmbar) {
            TRACE(9);
            ROMlib_togglealarm();
        }
    }
    else if (ROMlib_alarmonmbar) {
        TRACE(10);
        ROMlib_togglealarm();
    }

    if (em & activMask) {
        TRACE(11);
        if (CurDeactive) {
            TRACE(12);
            GetOSEvent(0, evt);
            TRACE(13);
            evt->what = CW(activateEvt);
            evt->message = (LONGINT)(long)CurDeactive;
            if (remflag)
                HIDDEN_RAW_SET(CurDeactive_H, (WindowPtr)0);
            retval = TRUE;
            /*-->*/ goto done;
        }
        if (CurActivate) {
            TRACE(14);
            GetOSEvent(0, evt);
            TRACE(15);
            evt->what = CW(activateEvt);
            evt->message = (LONGINT)(long)CurActivate;
            evt->modifiers |= CW(activeFlag);
            if (remflag)
                HIDDEN_RAW_SET(CurActivate_H, (WindowPtr)0);
            retval = TRUE;
            /*-->*/ goto done;
        }
    }

    /*
	 * NOTE: Currently (Sept. 15, 1993), we get different charCodes for
	 *	 command-shift-1 on the NeXT and under DOS.  I suspect that
	 *	 the DOS behaviour (the lower case stuff) is correct, my
	 *	 clue is 1.15 vs 1.16 of osevent.c, but I don't want to mess
	 *	 around with things just before releasing ROMAW/DOS 1.0.
	 */

    if (remflag) {
        TRACE(16);
        retval = GetOSEvent(em, evt);
        TRACE(17);
        if (retval && Cx(evt->what) == keyDown && ScrDmpEnb &&
            (Cx(evt->modifiers) & (shiftKey | cmdKey)) == (shiftKey | cmdKey)) {
            TRACE(18);
            switch ((Cx(evt->message) & charCodeMask)) {
            case '1':
            case '!': /* command shift 1: About Box / Help */
                retval = FALSE;
                do_about_box();
                break;
            case '2':
            case '@': /* command shift 2: Floppy Stuff */
                retval = FALSE;
                /* dofloppymount(); already done at a lower level */
                break;
            case '5':
            case '%': /* command shift 5: Preferences */
                retval = FALSE;
                ROMlib_show_preferences();
                break;
            case '6':
            case '^': /* command shift 6: Don't restart ROMAW */
                retval = FALSE;
                doquitreallyquits();
                break;
            case '7':
            case '&':
                retval = FALSE;
                /* Reset the video mode.  Seems to be needed under DOS
				 * sometimes when hotkeying around.
				 */
                vdriver_set_mode(0, 0, 0, vdriver_grayscale_p);
                redraw_screen();
                break;

            case '9':
            case '(': /* command shift 9: Dump RAM error log */
                retval = FALSE;
                error_dump_ram_err_buf("\n *** cmd-shift-9 pressed ***\n");
                break;
            }
            if (!retval)
                evt->what = CW(nullEvent);
            /*-->*/ goto done;
        }
    }
    else {
        TRACE(24);
        retval = OSEventAvail(em, evt);
        TRACE(25);
    }
    /*
	 * NOTE: I think the following block of code should probably be in SystemEvent,
	 *	 rather than here.  It will probably make a difference when an event
	 *	 call is made without diskMask set.  (I think SystemTask does the
	 *	 mount anyway and it potentially gets lost if no one is looking for
	 *	 it).
	 */
    if (!retval && remflag && (em & diskMask)) {
        TRACE(26);
        if (!beenhere) {
            TRACE(26);
            beenhere = 1;
            ROMlib_openharddisk("/tmp/testvol\0\0", &evt->message);
            if (evt->message) {
                TRACE(27);
                evt->what = CW(diskEvt);
                retval = TRUE;
            }
        }
    }
    if (!retval && (em & updateMask)) {
        TRACE(28);
        GetOSEvent(0, evt);
        TRACE(29);
        retval = CheckUpdate(evt);
    }

    /* check for high level events */
    if (!retval && (em & highLevelEventMask)) {
        retval = hle_get_event(evt, remflag);
    }

    if (!retval && ROMlib_delay) {
        TRACE(30);
        Delay((LONGINT)ROMlib_delay, (LONGINT*)0);
    }
done:
    ALLOCAEND
    TRACE(31);
    if (SystemEvent(evt)) {
        TRACE(32);
        evt->what = CWC(nullEvent);
        retval = FALSE;
    }
    TRACE(33);

    return retval;
}

P2(PUBLIC pascal trap, BOOLEAN, GetNextEvent, INTEGER, em, EventRecord*, evt)
{
    BOOLEAN retval;
    ROMlib_refresh_grow_icons();
#if defined(CYGWIN32) && defined(SDL) && defined(SYN68K)
    ROMlib_ppc_event_loop_entered();
#endif

    ROMlib_speech_poll();
    ROMlib_fileplay_poll();
    if (ROMlib_pending_image_dialog) {
        ROMlib_pending_image_dialog = 0;
        ROMlib_mount_image_dialog();
    }
    TRACE(1);
    retval = doevent(em, evt, TRUE);
    TRACE(0);
    return retval;
}

/*
 * NOTE: the code for WaitNextEvent below is not really according to spec,
 *	 but it should do the job.  We could rig a fancy scheme that
 *	 communicates with the display postscript loop on the NeXT but that
 *	 wouldn't be general purpose and this *should* be good enough.  We
 *	 have to use Delay rather than timer calls ourself in case the timer
 *	 is already in use.  We also can't delay the full interval because
 *	 that would prevent window resizing from working on the NeXT (and
 *	 we'd never get the mouse moved messages we need)
 */

P4(PUBLIC pascal trap, BOOLEAN, WaitNextEvent, INTEGER, mask, EventRecord*, evp, LONGINT, sleep, RgnHandle, mousergn)
{
    BOOLEAN retval = FALSE;
    Point p;

    do {
        /* Classic event-loop extensions service timers through EventAvail.
         Preserve that hook while this implementation polls for an event. */
        if (tooltraptable[0x171] != toolstuff[0x171].orig) {
            EventAvail(mask, evp);
#if defined(CYGWIN32) && defined(SDL) && defined(SYN68K)
            if (ROMlib_ppc_callback_failed())
                break;
#endif
        }
        retval = GetNextEvent(mask, evp);
        /* A wake request may arrive from a patched event routine or timer.
         Consume it even when an event already makes this wait return. */
        if (ROMlib_process_take_wakeup())
            break;
#if defined(CYGWIN32) && defined(SDL) && defined(SYN68K)
        if (ROMlib_ppc_callback_failed())
            break;
#endif
        if (!retval) {
            static INTEGER saved_h, saved_v;

            /* TODO: see what PtInRgn does with 0 as a RgnHandle */
            p.h = CW(evp->where.h);
            p.v = CW(evp->where.v);
            if (mousergn && !EmptyRgn(mousergn) && !PtInRgn(p, mousergn) && (p.h != saved_h || p.v != saved_v)) {
                evp->what = CWC(osEvt);
                evp->message = CLC(mouseMovedMessage << 24);
                retval = TRUE;
            }
            else if (sleep > 0) {

                Delay(MIN(sleep, 4), (LONGINT*)0);
                sleep -= 4;
            }
            saved_h = p.h;
            saved_v = p.v;
        }
    } while (!retval && sleep > 0);
    return retval;
}

P2(PUBLIC pascal trap, BOOLEAN, EventAvail, INTEGER, em, EventRecord*, evt)
{
    return (doevent(em, evt, FALSE));
}

P1(PUBLIC pascal trap, void, GetMouse, Point*, p)
{
    EventRecord evt;

    GetOSEvent(0, &evt);
    *p = evt.where;
    GlobalToLocal(p);
}

#warning Button not coded per IM Macintosh Toolbox Essentials 2-108

P0(PUBLIC pascal trap, BOOLEAN, Button)
{
    EventRecord evt;
    BOOLEAN retval;

    GetOSEvent(0, &evt);
    retval = (evt.modifiers & CWC(btnState)) ? FALSE : TRUE;
    return retval;
}

#warning StillDown not coded per IM Macintosh Toolbox Essentials 2-109

P0(PUBLIC pascal trap, BOOLEAN, StillDown) /* IMI-259 */
{
    EventRecord evt;

    return Button() ? !OSEventAvail((mDownMask | mUpMask), &evt) : FALSE;
}

/*
 * The weirdness below is because Word 5.1a gets very unhappy if
 * TickCount makes large adjustments to Ticks.  Even when we
 * just increase by one there are problems... The "no clock" option
 * might be retiring soon.
 */

P0(PUBLIC pascal trap, BOOLEAN, WaitMouseUp)
{
    EventRecord evt;
    int retval;

    retval = StillDown();
    if (!retval)
        GetOSEvent(mUpMask, &evt); /* just remove one ? */
    return (retval);
}

P1(PUBLIC pascal trap, void, GetKeys, unsigned char*, keys)
{
    BlockMove((Ptr)KeyMap, (Ptr)keys, (Size)sizeof_KeyMap);
}

P0(PUBLIC pascal trap, LONGINT, TickCount)
{
    unsigned long ticks;

    ticks = msecs_elapsed() * 3.0 / 50; /* == 60 / 1000 */

    /* Update Ticks and Time.  We only update Ticks if the clock is on;
	 * this seems to be necessary to make Word happy.
	 */

    if (ROMlib_clock)
        Ticks_UL.u = CL(ticks);

    Time = CL((uint32)(ROMlib_get_local_clock() >> 16));
    return ticks;
}

static uint32 power_activity_ticks[ROMlib_power_activity_count];

void ROMlib_power_note_activity(unsigned kind, uint32 ticks)
{
    if (kind < ROMlib_power_activity_count) {
        power_activity_ticks[kind] = ticks;
        power_activity_ticks[ROMlib_power_overall_activity] = ticks;
    }
}

uint32 ROMlib_power_last_activity(unsigned kind)
{
    return kind < ROMlib_power_activity_count ? power_activity_ticks[kind] : 0;
}

A0(PUBLIC, LONGINT, GetDblTime)
{
    return (Cx(DoubleTime));
}

A0(PUBLIC, LONGINT, GetCaretTime)
{
    return (Cx(CaretTime));
}

/*
 * These routines used to live in the various OS-specific config directories,
 * which was a real crock, since only the NEXTSTEP implementation worked
 * well.  I'm moving them here as I attempt to implement cut and paste
 * properly under Win32.
 */

#define SANE_DEBUGGING
#if defined(SANE_DEBUGGING)
int sane_debugging_on = 0; /* Leave this off and let the person doing the
				  debugging turn it on if he/she wants.  If this
				  is set to non-zero, it breaks code.  Not a
				  very nice thing to do. */
#endif                     /* SANE_DEBUGGING */

PUBLIC void sendsuspendevent(void)
{
    Point p;

    if ((size_info.size_flags & SZacceptSuspendResumeEvents)
#if defined(SANE_DEBUGGING)
        && !sane_debugging_on
#endif /* SANE_DEBUGGING */
        /* NOTE: Since ROMAW can currently only run one app at a time,
	   suspending an app that can background causes trouble with apps
	   like StuffIt Expander, since it makes it impossible to do work
	   in another window while waiting for StuffIt Expander to expand
	   a file.  It's not clear that the next two lines are the best solution,
	   but it's *a* solution. */
        && (!(ROMlib_options & ROMLIB_NOSUSPEND_BIT) /* ||
		!(size_info.size_flags & SZcanBackground) */
            )) {
        p.h = CW(MouseLocation.h);
        p.v = CW(MouseLocation.v);
        ROMlib_PPostEvent(osEvt, SUSPENDRESUMEBITS | SUSPEND | CONVERTCLIPBOARD, (HIDDEN_EvQElPtr*)0, TickCount(), p,
                          ROMlib_mods);
    }
}

PUBLIC void sendresumeevent(boolean_t cvtclip)
{
    LONGINT what;
    Point p;

    if ((size_info.size_flags & SZacceptSuspendResumeEvents)
#if defined(SANE_DEBUGGING)
        && !sane_debugging_on
#endif /* SANE_DEBUGGING */
    ) {
        what = SUSPENDRESUMEBITS | RESUME;
        if (cvtclip)
            what |= CONVERTCLIPBOARD;
        p.h = CW(MouseLocation.h);
        p.v = CW(MouseLocation.v);
        ROMlib_PPostEvent(osEvt, what, (HIDDEN_EvQElPtr*)0, TickCount(), p, ROMlib_mods);
    }
}

PUBLIC void sendcopy(void)
{
    Point p;

    p.h = CW(MouseLocation.h);
    p.v = CW(MouseLocation.v);
    ROMlib_PPostEvent(keyDown, 0x0863, /* 0x63 == 'c' */
                      (HIDDEN_EvQElPtr*)0, TickCount(), p, cmdKey | btnState);
    ROMlib_PPostEvent(keyUp, 0x0863, (HIDDEN_EvQElPtr*)0, TickCount(), p, cmdKey | btnState);
}

PUBLIC void sendpaste(void)
{
    Point p;

    p.h = CW(MouseLocation.h);
    p.v = CW(MouseLocation.v);
    ROMlib_PPostEvent(keyDown, 0x0976, /* 0x76 == 'v' */
                      (HIDDEN_EvQElPtr*)0, TickCount(), p, cmdKey | btnState);
    ROMlib_PPostEvent(keyUp, 0x0976, (HIDDEN_EvQElPtr*)0, TickCount(), p, cmdKey | btnState);
}

/*
 * NOTE: the code for ROMlib_send_quit below is cleaner than the code for
 *       sendcopy and sendpaste above, but it was added after 2.1pr0
 *       was released but before 2.1 was released, so we can't tamper
 *       with the above code because there's a chance it would break
 *       something.  Ick.
 */

PRIVATE void post_helper(INTEGER code, uint8 raw, uint8 mapped, INTEGER mods)
{
    Point p;

    p.h = CW(MouseLocation.h);
    p.v = CW(MouseLocation.v);

    ROMlib_PPostEvent(code, (raw << 8) | mapped, (HIDDEN_EvQElPtr*)0, TickCount(), p, btnState | mods);
}

PUBLIC BOOLEAN ROMlib_post_quit_event(void)
{
    HIDDEN_ProcPtr handler;
    int32 refcon;
    if (ROMlib_creator == TICK("MACS") && AE_info && PP_RAW(MR(AE_info)->appl_zone_tables)) {
        /* Finder reads the public HLE wire format itself. AESend's private
         descriptor representation is only understood by our AE manager. */
        const uint32 payload[] = {CLC(TICK("aevt")), CLC(0x00010001), CLC(0x3b3b3b3b)};
        EventRecord event = {0};
        uint32 id = CLC(kAEQuitApplication);
        event.what = CWC(kHighLevelEvent);
        event.message = CLC(kCoreEventClass);
        memcpy(&event.where, &id, sizeof id);
        return C_PostHighLevelEvent(&event, NULL, -1, (Ptr)payload, sizeof payload, -1) == noErr;
    }
    if (AE_info && PP_RAW(MR(AE_info)->appl_zone_tables) &&
        C_AEGetEventHandler(kCoreEventClass, kAEQuitApplication, &handler, &refcon, FALSE) == noErr) {
        ProcessSerialNumber psn;
        AEDesc target = {0};
        AppleEvent event = {0};
        OSErr err = C_GetCurrentProcess(&psn);
        if (err == noErr)
            err = C_AECreateDesc(typeProcessSerialNumber, (Ptr)&psn, sizeof psn, &target);
        if (err == noErr)
            err = C_AECreateAppleEvent(kCoreEventClass, kAEQuitApplication, &target, -1, 0, &event);
        if (err == noErr)
            err = C_AESend(&event, NULL, kAENoReply, 0, 0, NULL, NULL);
        C_AEDisposeDesc(&event);
        C_AEDisposeDesc(&target);
        if (err == noErr)
            return TRUE;
    }
    return FALSE;
}

PUBLIC void ROMlib_send_quit(void)
{
    if (ROMlib_post_quit_event())
        return;
    ROMlib_prepare_quit_menu_key();
    post_helper(keyDown, MKV_CLOVER, 0, 0);
    post_helper(keyDown, MKV_q, 'q', cmdKey);
    post_helper(keyUp, MKV_q, 'q', cmdKey);
    post_helper(keyUp, MKV_CLOVER, 0, cmdKey);
}
