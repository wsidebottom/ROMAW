/*+===================================================================
  File:      carbon_events.h

  Summary:   Declares native Carbon Manager event operations.

  Classes:   None.

  Functions: ROMlib_carbon_event_access.

  Origin:    CarbonLib 1.4 disassembly and Universal Interfaces 3.4.2 CarbonEvents.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_EVENTS_H
#define ROMAW_CARBON_EVENTS_H

#include "internal.h"

enum carbon_event_operation {
    event_create,
    event_copy,
    event_retain,
    event_release,
    event_read_word,
    event_read_time,
    event_write_time,
    event_set_parameter,
    event_get_parameter,
    event_clock,
    event_key_modifiers,
    event_target,
    event_object_target,
    event_standard_handler,
    event_install_timer,
    event_remove_timer,
    event_reschedule_timer,
    event_install_handler,
    event_remove_handler,
    event_add_types,
    event_remove_types,
    event_dispatch,
    event_dispatch_next,
    event_current_loop,
    event_main_queue,
    event_current_queue,
    event_post_queue,
    event_count_queue,
    event_is_queued,
    event_remove_queue,
    event_flush_queue,
    event_flush_matching_queue,
    event_find_in_queue,
    event_flush_matching_callback,
    event_masked_global,
    event_global_time,
    event_wait_next,
    event_in_classic_mask,
    event_convert_classic_record,
    event_flush_classic_queue,
    event_classic_mouse_button,
    event_classic_available,
    event_classic_os_available,
    event_classic_os_get,
    event_is_cancel_key,
    event_is_context_menu,
    event_could_be_menu_key,
    event_control_drag,
    event_receive_next,
    event_run_current_loop,
    event_run_application_loop,
    event_quit_application_loop,
    event_track_mouse_location,
    event_track_mouse_location_options,
    event_track_mouse_region,
    event_quit_loop,
    event_begin_modal,
    event_end_modal,
    event_run_modal,
    event_quit_modal,
    event_get_modality,
    event_set_modality,
};

int ROMlib_carbon_event_access(const char* name, uint32_t offset, enum carbon_event_operation operation, int field,
                               ROMlib_carbon_ppc_frame* call);

#endif
