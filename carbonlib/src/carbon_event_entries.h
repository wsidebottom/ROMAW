/*+===================================================================
  File:      carbon_event_entries.h

  Summary:   Maps Carbon Event Manager exports to native PowerPC frame handlers.

  Classes:   None.

  Functions: Private PowerPC frame wrappers for the export registry.

  Origin:    CarbonLib 1.4 disassembly and Universal Interfaces 3.4.2 CarbonEvents.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_EVENT_ENTRIES_H
#define ROMAW_CARBON_EVENT_ENTRIES_H

#include "carbon_events.h"

#define CARBON_EVENT(name, offset, operation, field)                              \
    static int carbon_event_##name(ROMlib_carbon_ppc_frame* call)                 \
    {                                                                             \
        return ROMlib_carbon_event_access(#name, offset, operation, field, call); \
    }

CARBON_EVENT(CreateEvent, 0x4473c, event_create, 0)
CARBON_EVENT(CopyEvent, 0x44878, event_copy, 0)
CARBON_EVENT(RetainEvent, 0x44a80, event_retain, 0)
CARBON_EVENT(ReleaseEvent, 0x44b7c, event_release, 0)
CARBON_EVENT(GetEventRetainCount, 0x44cb0, event_read_word, 0x24)
CARBON_EVENT(GetEventKind, 0x44d18, event_read_word, 0x10)
CARBON_EVENT(GetEventAttributes, 0x44d80, event_read_word, 0x14)
CARBON_EVENT(GetEventClass, 0x44de8, event_read_word, 0xc)
CARBON_EVENT(GetEventTime, 0x44e50, event_read_time, 0)
CARBON_EVENT(SetEventTime, 0x44ebc, event_write_time, 0)
CARBON_EVENT(GetEventPriority, 0x44f24, event_read_word, 0x20)
CARBON_EVENT(SetEventParameter, 0x45198, event_set_parameter, 0)
CARBON_EVENT(GetEventParameter, 0x45290, event_get_parameter, 0)
CARBON_EVENT(GetCurrentEventTime, 0x42668, event_clock, 0)
CARBON_EVENT(GetCurrentKeyModifiers, 0x426d0, event_key_modifiers, 0)
CARBON_EVENT(GetLastUserEventTime, 0x44188, event_global_time, -0xd5c)
CARBON_EVENT(WaitNextEvent, 0x3b564, event_wait_next, 0)
CARBON_EVENT(IsEventInMask, 0x3c578, event_in_classic_mask, 0)
CARBON_EVENT(ConvertEventRefToEventRecord, 0x3c7bc, event_convert_classic_record, 0)
CARBON_EVENT(__FlushEvents, 0x3c350, event_flush_classic_queue, 0)
CARBON_EVENT(__WaitMouseUp, 0x3c454, event_classic_mouse_button, -0xecc)
CARBON_EVENT(__StillDown, 0x3c4cc, event_classic_mouse_button, -0xec8)
CARBON_EVENT(__GetOSEvent, 0x3c1ac, event_classic_os_get, -0xec4)
CARBON_EVENT(__OSEventAvail, 0x3c2bc, event_classic_os_available, -0xebc)
CARBON_EVENT(__EventAvail, 0x3c240, event_classic_available, -0xec0)
CARBON_EVENT(IsUserCancelEventRef, 0x4762c, event_is_cancel_key, 0)
CARBON_EVENT(IsShowContextualMenuEvent, 0x70758, event_is_context_menu, 0)
CARBON_EVENT(CouldBeMenuKeyEvent, 0x1b284, event_could_be_menu_key, 0)
CARBON_EVENT(HandleControlDragTracking, 0x6c360, event_control_drag, 0)
CARBON_EVENT(HandleControlDragReceive, 0x6c4d4, event_control_drag, 1)
CARBON_EVENT(SetControlDragTrackingEnabled, 0x6c74c, event_control_drag, 2)
CARBON_EVENT(IsControlDragTrackingEnabled, 0x6c904, event_control_drag, 3)
CARBON_EVENT(SetAutomaticControlDragTrackingEnabledForWindow, 0x6ca88, event_control_drag, 4)
CARBON_EVENT(IsAutomaticControlDragTrackingEnabledForWindow, 0x6c5e0, event_control_drag, 5)
CARBON_EVENT(ReceiveNextEvent, 0x3b058, event_receive_next, 0)
CARBON_EVENT(RunCurrentEventLoop, 0x4171c, event_run_current_loop, 0)
CARBON_EVENT(RunApplicationEventLoop, 0x3dd44, event_run_application_loop, 0)
CARBON_EVENT(QuitApplicationEventLoop, 0x3de58, event_quit_application_loop, 0)
CARBON_EVENT(TrackMouseLocation, 0x47c18, event_track_mouse_location, 0)
CARBON_EVENT(TrackMouseLocationWithOptions, 0x477ac, event_track_mouse_location_options, 0)
CARBON_EVENT(TrackMouseRegion, 0x47c50, event_track_mouse_region, 0)
CARBON_EVENT(GetMainEventLoop, 0x41024, event_masked_global, -0xdb4)

CARBON_EVENT(GetCurrentEventLoop, 0x4104c, event_current_loop, 0)
CARBON_EVENT(GetMainEventQueue, 0x42eb4, event_main_queue, 0)
CARBON_EVENT(GetCurrentEventQueue, 0x42f64, event_current_queue, 0)
CARBON_EVENT(PostEventToQueue, 0x434ec, event_post_queue, 0)
CARBON_EVENT(GetNumEventsInQueue, 0x43594, event_count_queue, 0)
CARBON_EVENT(RemoveEventFromQueue, 0x43614, event_remove_queue, 0)
CARBON_EVENT(IsEventInQueue, 0x43768, event_is_queued, 0)
CARBON_EVENT(FlushEventQueue, 0x43a14, event_flush_queue, 0)
CARBON_EVENT(FlushEventsMatchingListFromQueue, 0x43870, event_flush_matching_queue, 0)
CARBON_EVENT(FindSpecificEventInQueue, 0x44194, event_find_in_queue, 0)
CARBON_EVENT(FlushSpecificEventsFromQueue, 0x44000, event_flush_matching_callback, 0)
CARBON_EVENT(GetApplicationEventTarget, 0x3f7d4, event_target, -0xe1c)
CARBON_EVENT(GetEventDispatcherTarget, 0x3dc0c, event_target, -0xe58)
CARBON_EVENT(GetUserFocusEventTarget, 0x3f998, event_target, -0xe0c)
CARBON_EVENT(InstallEventHandler, 0x46424, event_install_handler, 0)
CARBON_EVENT(RemoveEventHandler, 0x47390, event_remove_handler, 0)
CARBON_EVENT(AddEventTypesToHandler, 0x46f14, event_add_types, 0)
CARBON_EVENT(RemoveEventTypesFromHandler, 0x4726c, event_remove_types, 0)
CARBON_EVENT(SendEventToEventTarget, 0x46dc0, event_dispatch, 0)
CARBON_EVENT(CallNextEventHandler, 0x46df8, event_dispatch_next, 0)

CARBON_EVENT(InstallEventLoopTimer, 0x418d4, event_install_timer, 0)
CARBON_EVENT(RemoveEventLoopTimer, 0x41a24, event_remove_timer, 0)
CARBON_EVENT(SetEventLoopTimerNextFireTime, 0x41b18, event_reschedule_timer, 0)
CARBON_EVENT(QuitEventLoop, 0x417bc, event_quit_loop, 0)

CARBON_EVENT(BeginAppModalStateForWindow, 0x41df4, event_begin_modal, 0)
CARBON_EVENT(_BeginAppModalStateForWindow, 0x41ee0, event_begin_modal, 0)
CARBON_EVENT(EndAppModalStateForWindow, 0x42030, event_end_modal, 0)
CARBON_EVENT(_EndAppModalStateForWindow, 0x42070, event_end_modal, 0)
CARBON_EVENT(RunAppModalLoopForWindow, 0x4213c, event_run_modal, 0)
CARBON_EVENT(_RunAppModalLoopForWindow, 0x42204, event_run_modal, 0)
CARBON_EVENT(QuitAppModalLoopForWindow, 0x4224c, event_quit_modal, 0)
CARBON_EVENT(_QuitAppModalLoopForWindow, 0x4228c, event_quit_modal, 0)
CARBON_EVENT(GetWindowModality, 0x4f560, event_get_modality, 0)
CARBON_EVENT(SetWindowModality, 0x4f624, event_set_modality, 0)

CARBON_EVENT(GetWindowEventTarget, 0x4c070, event_object_target, 0)
CARBON_EVENT(GetControlEventTarget, 0x3ac34, event_object_target, 1)
CARBON_EVENT(GetMenuEventTarget, 0x3f880, event_object_target, 2)
CARBON_EVENT(InstallStandardEventHandler, 0x46bf0, event_standard_handler, 0)

#undef CARBON_EVENT

#endif
