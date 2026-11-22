/*+===================================================================
  File:      probe.c

  Summary:   Implements probe support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests/mouse-input; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Menus.h>
#include <ToolUtils.h>
#include <Events.h>
#include <OSUtils.h>
#include <LowMem.h>
static unsigned actions;

static int fail(int code)
{
    SysBeep(-code);
    return code;
}

static pascal void scroll_action(ControlHandle control, short part)
{
    ++actions;
    SetControlValue(control, GetControlValue(control) + (part == inDownButton ? 1 : -1));
}

static int next_event(short mask, short what, EventRecord* event)
{
    unsigned long until = TickCount() + 120;
    while (TickCount() < until)
        if (GetNextEvent(mask, event) && event->what == what)
            return 1;
    return 0;
}

static int next_mouse(short what, EventRecord* event)
{
    return next_event(mDownMask | mUpMask, what, event);
}

int main(void)
{
    Rect bounds = {60, 40, 300, 440}, vertical = {0, 385, 225, 400}, horizontal = {225, 0, 240, 385};
    EventRecord event;
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    WindowPtr window = NewCWindow(NULL, &bounds, "\pMouse Input Probe", 1, 0, (WindowPtr)-1, 1, 0);
    if (!window)
        return fail(1);
    SetPort(window);
    ControlHandle v = NewControl(window, &vertical, "\p", 1, 50, 0, 100, scrollBarProc, 0);
    ControlHandle h = NewControl(window, &horizontal, "\p", 1, 50, 0, 100, scrollBarProc, 0);
    if (!v || !h)
        return fail(2);
    SysBeep(1);
    if (!next_mouse(mouseDown, &event) || !(event.modifiers & ControlKey))
        return fail(3);
    if (!next_mouse(mouseUp, &event) || !(event.modifiers & ControlKey))
        return fail(4);
    SysBeep(1);
    if (!next_mouse(mouseDown, &event) || event.message)
        return fail(5);
    GlobalToLocal(&event.where);
    if (TrackControl(v, event.where, NewControlActionUPP(scroll_action)) != inDownButton || actions != 1 ||
        GetControlValue(v) != 51)
        return fail(6);
    SysBeep(1);
    if (!next_mouse(mouseDown, &event))
        return fail(7);
    GlobalToLocal(&event.where);
    if (TrackControl(v, event.where, NULL) != inUpButton || actions != 1)
        return fail(8);
    SysBeep(1);
    if (!next_mouse(mouseDown, &event))
        return fail(9);
    GlobalToLocal(&event.where);
    if (TrackControl(h, event.where, NewControlActionUPP(scroll_action)) != inDownButton || actions != 2 ||
        GetControlValue(h) != 51)
        return fail(10);
    Point point;
    GetMouse(&point);
    if (point.h != 60 || point.v != 40 || Button())
        return fail(11);
    SysBeep(1);
    if (!next_mouse(mouseDown, &event) || (event.modifiers & ControlKey) || event.where.h != 100 ||
        event.where.v != 100)
        return fail(12);
    if (!next_mouse(mouseUp, &event) || Button())
        return fail(13);
    SysBeep(1);
    if (!next_mouse(mouseDown, &event) || !(event.modifiers & optionKey) || (event.modifiers & ControlKey))
        return fail(14);
    if (!next_mouse(mouseUp, &event) || !(event.modifiers & optionKey) || Button())
        return fail(15);
    SysBeep(1);
    if (!next_event(mDownMask, mouseDown, &event) || !next_event(mDownMask, mouseDown, &event))
        return fail(16);
    FlushEvents(everyEvent, 0);
    if (Button())
        return fail(17);
    DisposeWindow(window);
    SysBeep(777);
    return 0;
}
