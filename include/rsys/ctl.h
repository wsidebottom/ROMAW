/*+===================================================================
  File:      ctl.h

  Summary:   Declares ctl support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_CTL_H_)
#define _CTL_H_

/*
 * Copyright 1986, 1989, 1990, 1995 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: ctl.h 85 2005-05-24 22:06:13Z ctm $
 */

#include "ControlMgr.h"
#include "MenuMgr.h"

extern void ROMlib_frame_default_button(const Rect* bounds);
#include "MemoryMgr.h"
#include "ResourceMgr.h"

#include "rsys/mman.h"
#include "rsys/cquick.h"
#include "rsys/pstuff.h"

extern const ColorSpec default_ctl_colors[];
extern CTabHandle default_ctl_ctab;
extern AuxCtlHandle default_aux_ctl;

enum {
    ROMlib_bevel_button_proc = 32,
    ROMlib_slider_proc = 48,
    ROMlib_slider_thumb_length = 14,
    ROMlib_progress_bar_proc = 80,
    ROMlib_relevance_bar_proc = 81,
    ROMlib_chasing_arrows_proc = 112,
    ROMlib_tabs_proc = 128,
    ROMlib_separator_proc = 144,
    ROMlib_group_box_proc = 160,
    ROMlib_image_well_proc = 176,
    ROMlib_user_pane_proc = 256,
    ROMlib_root_control_proc = 257,
    ROMlib_edit_text_proc = 272,
    ROMlib_static_text_proc = 288,
    ROMlib_picture_proc = 304,
    ROMlib_live_scroll_proc = 386
};

typedef struct PACKED {
    uint32 draw, hit, idle, activate, focus;
} ROMlib_pane_callbacks;

typedef struct PACKED {
    uint32 parent;
    uint32 first_child, last_child, previous_sibling, next_sibling;
    ROMlib_pane_callbacks callbacks;
    uint32 dialog_edit_text;
    INTEGER dialog_edit_item;
    INTEGER bevel_text_placement;
    INTEGER bevel_text_alignment, bevel_text_offset;
    INTEGER bevel_graphic_alignment;
    INTEGER bevel_menu_value;
    Point bevel_graphic_offset;
    Byte image_content[6];
    INTEGER focus_part;
    uint32 identifier_signature, identifier_id, command_id;
    int32 range[4]; /* Carbon value, minimum, maximum and view size. */
    uint32 custom_definition;
    uint32 color_proc;
    INTEGER image_transform;
    uint32 supervisor;
    uint32 chasing_frame;
    BOOLEAN chasing_animating;
    uint32 progress_last_tick;
    Byte progress_phase;
    BOOLEAN progress_indeterminate, progress_animating;
    boolean_t saved_range;
} ROMlib_aux_control_data;

#define ROMlib_aux_control_extra(aux) ((ROMlib_aux_control_data*)(STARH(aux) + 1))
extern ControlHandle ROMlib_alloc_custom_control(WindowPtr window, const Rect* bounds, uint32 definition);
extern void ROMlib_abort_custom_control(ControlHandle control);
extern int32 ROMlib_send_control_message(ControlHandle control, int16 message, uint32 parameter);
extern int32 ROMlib_set_up_control_color(ControlHandle control, int16 depth, Byte color_device, boolean_t text);
extern OSErr ROMlib_create_root_control(WindowPtr window, HIDDEN_ControlHandle* root);
extern OSErr ROMlib_get_root_control(WindowPtr window, HIDDEN_ControlHandle* root);
extern OSErr ROMlib_embed_control(ControlHandle control, ControlHandle parent);
extern OSErr ROMlib_set_control_supervisor(ControlHandle control, ControlHandle supervisor);
extern ControlHandle ROMlib_control_hit_target(ControlHandle control);
extern INTEGER ROMlib_handle_control_click(ControlHandle control, Point where, INTEGER modifiers, ProcPtr action);
extern INTEGER ROMlib_handle_control_key(ControlHandle control, INTEGER key, INTEGER character, INTEGER modifiers);
extern void ROMlib_detach_control(ControlHandle control);
extern OSErr ROMlib_auto_embed_control(ControlHandle control, WindowPtr window);
extern OSErr ROMlib_set_user_pane_callback(ControlHandle control, ResType tag, const Byte* data);
extern OSErr ROMlib_set_control_text(ControlHandle control, Size size, const Byte* data);
extern int ROMlib_native_control_kind(ControlHandle control);
extern OSErr ROMlib_get_control_features(ControlHandle control, uint32* features);
extern OSErr ROMlib_control_best_rect(ControlHandle control, Rect* bounds, INTEGER* baseline);
extern OSErr ROMlib_control_region(ControlHandle control, INTEGER part, RgnHandle region);
extern OSErr ROMlib_set_control_image_content(ControlHandle control, const Byte* content, int expected_kind);
extern OSErr ROMlib_get_control_image_content(ControlHandle control, Byte* content, int expected_kind);
extern ControlHandle ROMlib_dialog_edit_control(WindowPtr window, INTEGER item, Handle text);
extern INTEGER ROMlib_dialog_edit_control_item(ControlHandle control);
extern boolean_t ROMlib_control_visible_at(ControlHandle control, Point where);
extern OSErr ROMlib_get_control_text(ControlHandle control, Size size, Byte* data, Size* actual);

extern OSErr ROMlib_get_dialog_control(WindowPtr window, INTEGER item, HIDDEN_ControlHandle* control);
extern OSErr ROMlib_control_focus(WindowPtr window, ControlHandle control, INTEGER part, HIDDEN_ControlHandle* output);
extern OSErr ROMlib_control_cycle_focus(WindowPtr window, boolean_t reverse);
extern OSErr ROMlib_control_activate(ControlHandle control, boolean_t active);
extern void ROMlib_control_idle(WindowPtr window);

enum ROMlib_control_data_op { ROMLIB_CONTROL_READ, ROMLIB_CONTROL_MEASURE, ROMLIB_CONTROL_WRITE };

extern LONGINT ROMlib_control_data(ControlHandle control, INTEGER part, ResType tag, Size size, Byte* data,
                                   Size* actual, enum ROMlib_control_data_op operation);

/* accessor macros */
#define CTL_RECT(ctl) (HxX(ctl, contrlRect))
#define CTL_TITLE(ctl) (HxX(ctl, contrlTitle))

#define CTL_NEXT_CONTROL_X(ctl) (HxX(ctl, nextControl))
#define CTL_OWNER_X(ctl) (HxX(ctl, contrlOwner))
#define CTL_VIS_X(ctl) (HxX(ctl, contrlVis))
#define CTL_HILITE_X(ctl) (HxX(ctl, contrlHilite))
#define CTL_VALUE_X(ctl) (HxX(ctl, contrlValue))
#define CTL_MIN_X(ctl) (HxX(ctl, contrlMin))
#define CTL_MAX_X(ctl) (HxX(ctl, contrlMax))

#define CTL_DEFPROC_X(ctl) (HxX(ctl, contrlDefProc))
#define CTL_DATA_X(ctl) (HxX(ctl, contrlData))
#define CTL_ACTION_X(ctl) (HxX(ctl, contrlAction))
#define CTL_REF_CON_X(ctl) (HxX(ctl, contrlRfCon))

#define CTL_NEXT_CONTROL(ctl) (PPR(CTL_NEXT_CONTROL_X(ctl)))
#define CTL_OWNER(ctl) (PPR(CTL_OWNER_X(ctl)))
#define CTL_VIS(ctl) (CTL_VIS_X(ctl))
#define CTL_HILITE(ctl) ((uint8)CTL_HILITE_X(ctl))
#define CTL_VALUE(ctl) (CW(CTL_VALUE_X(ctl)))
#define CTL_MIN(ctl) (CW(CTL_MIN_X(ctl)))
#define CTL_MAX(ctl) (CW(CTL_MAX_X(ctl)))
#define CTL_DEFPROC(ctl) (PPR(CTL_DEFPROC_X(ctl)))
#define CTL_DATA(ctl) ((RgnHandle)PPR(CTL_DATA_X(ctl)))
#define CTL_ACTION(ctl) (PPR(CTL_ACTION_X(ctl)))
#define CTL_ACTION_AS_LONG(ctl) (CL((LONGINT)(uintptr_t)PP_RAW(CTL_ACTION_X(ctl))))
#define CTL_REF_CON(ctl) (CL(CTL_REF_CON_X(ctl)))

extern CTabHandle default_ctl_ctab;
extern HIDDEN_AuxCtlHandle* lookup_aux_ctl(ControlHandle ctl);
extern AuxCtlHandle ROMlib_live_control_aux(ControlHandle control);
extern boolean_t ROMlib_control_uses_saved_range(AuxCtlHandle aux);
extern BOOLEAN ROMlib_ppc_control_in_saved_window(ControlHandle control);
extern void ROMlib_ppc_sync_control_auxiliary_list(void);

enum ROMlib_control_range_field { control_value, control_minimum, control_maximum, control_view_size };

/* ControlsLib keeps the sign and the lowest fifteen bits in the legacy record. */
static inline int16 ROMlib_control_legacy_value(int32 value)
{
    return (int16)(((uint32)value & 0x7fff) | (value < 0 ? 0x8000 : 0));
}

static inline void ROMlib_control_read_range(ControlHandle control, AuxCtlHandle aux, int32 range[4])
{
    const int16 legacy[] = {CTL_VALUE(control), CTL_MIN(control), CTL_MAX(control)};
    ROMlib_aux_control_data* extra = aux ? ROMlib_aux_control_extra(aux) : NULL;
    for (unsigned i = 0; i < NELEM(legacy); ++i) {
        if (extra && legacy[i] != ROMlib_control_legacy_value(extra->range[i]))
            extra->range[i] = legacy[i];
        range[i] = extra ? extra->range[i] : legacy[i];
    }
    range[control_view_size] = extra ? extra->range[control_view_size] : 0;
}

static inline void ROMlib_control_store_value(ControlHandle control, AuxCtlHandle aux, int32 value)
{
    if (aux)
        ROMlib_aux_control_extra(aux)->range[control_value] = value;
    CTL_VALUE_X(control) = CW(ROMlib_control_legacy_value(value));
}

static inline boolean_t ROMlib_control_has_range(ControlHandle control)
{
    int32 range[4];
    ROMlib_control_read_range(control, HIDDEN_NATIVE(*lookup_aux_ctl(control)), range);
    return range[control_minimum] < range[control_maximum];
}

extern int32 C_cdef0(int16 var, ControlHandle ctl, int16 mess, int32 param);
extern int32 C_cdef16(int16 var, ControlHandle ctl, int16 mess, int32 param);
extern int32 C_cdef1008(int16 var, ControlHandle ctl, int16 mess, int32 param);

#define VAR(w) (GetCVariant((w)))

extern BOOLEAN ROMlib_dirtyvariant;

extern void sb_ctl_init(void);

typedef pascal LONGINT (*ctlfuncp)(INTEGER var, ControlHandle ctl, INTEGER mess, LONGINT param);

extern LONGINT ROMlib_ctlcall(ControlHandle c, INTEGER i, intptr_t l);
#define CTLCALL(c, i, l) ROMlib_ctlcall((c), (i), (l))

#define CTL_CALL_EXCURSION(control, body) THEPORT_SAVE_EXCURSION(CTL_OWNER(control), {body})

#define ENTIRECONTROL 0
#define MAXPARTCODE 253
#define INACTIVE 255
#define ALLINDICATORS 129

#define POPUP_MENU_X(popup) (HxX(popup, menu))
#define POPUP_MENU_ID_X(popup) (HxX(popup, menu_id))

#define POPUP_MENU_ID(popup) (CW(POPUP_MENU_ID_X(popup)))
#define POPUP_MENU(popup) (PPR(POPUP_MENU_X(popup)))

#define POPUP_TITLE_WIDTH(popup) (HxX(popup, title_width))
#define POPUP_FLAGS(popup) (HxX(popup, flags))

struct PACKED popup_data {
    PACKED_MEMBER(MenuHandle, menu);
    int16 menu_id;

    /* private fields */
    int16 title_width;
    int flags;
    boolean_t borrowed_menu;
};

typedef struct popup_data popup_data_t;
typedef popup_data_t* popup_data_ptr;
MAKE_HIDDEN(popup_data_ptr);
typedef HIDDEN_popup_data_ptr* popup_data_handle;

typedef struct PACKED {
    Rect _tlimit;
    Rect _tslop;
    INTEGER _taxis;
} thumbstr;

typedef struct PACKED {
    Rect _crect;
    INTEGER _cvalue;
    INTEGER _cvisible;
    INTEGER _cmax;
    INTEGER _cmin;
    INTEGER _cprocid;
    LONGINT _crefcon;
    Byte _ctitle;
} contrlrestype;

struct PACKED lsastr {
    Rect limitRect;
    Rect slopRect;
    INTEGER axis;
};

extern BOOLEAN ROMlib_cdef0_is_rectangular;

extern LONGINT ROMlib_set_control_font_style(ControlHandle control, const INTEGER* style);
extern void ROMlib_apply_control_font_style(ControlHandle control);
extern void ROMlib_forget_control_font_style(ControlHandle control);
extern OSErr ROMlib_set_bevel_text_placement(ControlHandle control, INTEGER placement);
extern OSErr ROMlib_set_bevel_text_alignment(ControlHandle control, INTEGER alignment, INTEGER offset);
extern INTEGER ROMlib_bevel_text_alignment(ControlHandle control);
extern INTEGER ROMlib_bevel_text_offset(ControlHandle control);
extern INTEGER ROMlib_bevel_graphic_alignment(ControlHandle control);
extern Point ROMlib_bevel_graphic_offset(ControlHandle control);
extern OSErr ROMlib_set_bevel_graphic_alignment(ControlHandle control, INTEGER alignment, INTEGER h, INTEGER v);
extern OSErr ROMlib_set_bevel_menu_value(ControlHandle control, INTEGER value);
extern OSErr ROMlib_get_bevel_menu_value(ControlHandle control, INTEGER* value);
extern INTEGER ROMlib_bevel_text_placement(ControlHandle control);

extern void image_wind_scroll_arrow_active_up_init(void);
extern void image_wind_scroll_arrow_inactive_up_init(void);
extern void image_wind_scroll_arrow_active_down_init(void);
extern void image_wind_scroll_arrow_inactive_down_init(void);
extern void image_wind_scroll_arrow_active_left_init(void);
extern void image_wind_scroll_arrow_inactive_left_init(void);
extern void image_wind_scroll_arrow_active_init(void);
extern void image_wind_scroll_arrow_clicked_up_init(void);
extern void image_wind_scroll_arrow_clicked_down_init(void);
extern void image_wind_scroll_arrow_clicked_left_init(void);
extern void image_wind_scroll_arrow_clicked_init(void);
extern void image_wind_scroll_arrow_inactive_init(void);
extern void image_wind_scroll_thumb_init(void);
extern void image_wind_scroll_thumb_vertical_init(void);
extern void image_wind_close_clicked_init(void);
extern void image_wind_close_init(void);
extern void image_wind_grow_init(void);
extern void image_wind_zoom_init(void);
extern void image_apple_init(void);

extern void C_new_draw_scroll(INTEGER depth, INTEGER flags, GDHandle target, LONGINT l);

extern void C_new_pos_ctl(INTEGER depth, INTEGER flags, GDHandle target, LONGINT l);
#endif /* !_CTL_H_ */
