/*+===================================================================
  File:      textension.h

  Summary:   Declares the native CarbonLib operations on Textension records.

  Classes:   None.

  Functions: Declares ROMlib_carbon_textension and its operation selector.

  Origin:    CarbonLib 1.4 and Textension_CL 1.3.2 disassembly.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_TEXTENSION_H
#define ROMAW_CARBON_TEXTENSION_H

#include "carbonlib.h"

typedef enum {
    txn_initialize,
    txn_terminate,
    txn_new_object,
    txn_version,
    txn_data_size,
    txn_selection,
    txn_selection_empty,
    txn_view_rect,
    txn_line_count,
    txn_window_attached,
    txn_specific_window_attached,
    txn_change_count,
    txn_action_count,
    txn_clear_action_count,
    txn_get_data_filter,
    txn_set_data_filter,
    txn_sleep_ticks,
    txn_set_selection,
    txn_can_undo,
    txn_can_redo,
    txn_is_typing,
    txn_font_defaults,
    txn_set_font_defaults,
    txn_font_menu,
    txn_new_font_menu,
    txn_dispose_font_menu,
    txn_prepare_font_menu,
    txn_do_font_menu_selection,
    txn_select_all,
    txn_empty_undo_stack,
    txn_force_update,
    txn_set_background,
    txn_draw,
    txn_focus,
    txn_echo_mode,
    txn_undo,
    txn_redo,
    txn_show_selection,
    txn_set_view_rect,
    txn_offset_to_point,
    txn_point_to_offset,
    txn_count_runs,
    txn_indexed_run,
    txn_get_data,
    txn_activate,
    txn_count_elements,
    txn_find_element,
    txn_find,
    txn_scrap_pastable,
    txn_scrap_to_public,
    txn_scrap_from_public,
    txn_update,
    txn_resize_frame,
    txn_zoom_window,
    txn_frame_bounds,
    txn_adjust_cursor,
    txn_clear,
    txn_cut,
    txn_copy,
    txn_paste,
    txn_grow_window,
    txn_attach_window,
    txn_get_line_metrics,
    txn_get_referenced_data,
    txn_count_lines_to_print,
    txn_draw_in_rectangle,
    txn_click,
    txn_delete_object,
    txn_drag_tracker,
    txn_drag_receiver,
    txn_idle,
    txn_key_event,
    txn_unicode_key_event,
    txn_revert,
    txn_page_setup,
    txn_print,
    txn_get_data_encoded,
    txn_set_data,
    txn_get_continuous_attributes,
    txn_set_type_attributes,
    txn_set_data_from_file,
    txn_save,
    txn_get_controls,
    txn_draw_unicode_text_box,
    txn_draw_cf_text_box,
    txn_set_controls,
    txn_begin_print,
    txn_end_print,
    txn_print_page,
} CarbonTextensionOperation;

int ROMlib_carbon_textension(const char* name, CarbonTextensionOperation operation, ROMlib_carbon_ppc_frame* call);

#endif
