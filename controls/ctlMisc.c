/*+===================================================================
  File:      ctlMisc.c

  Summary:   Implements ctl misc support for Macintosh Control Manager operations.

  Classes:   None.

  Functions: ROMlib_set_bevel_menu_value, ROMlib_get_bevel_menu_value, ROMlib_bevel_graphic_alignment, ROMlib_bevel_graphic_offset, ROMlib_set_bevel_graphic_alignment,
             ROMlib_bevel_text_alignment, ROMlib_bevel_text_offset, ROMlib_set_bevel_text_alignment,
             ROMlib_apply_control_font_style, ROMlib_auto_embed_control, ROMlib_bevel_text_placement,
             ROMlib_set_bevel_text_placement, ROMlib_ctlcall, ROMlib_detach_control, ROMlib_embed_control,
             ROMlib_forget_control_font_style, ROMlib_get_control_text, ROMlib_get_control_features,
             ROMlib_native_control_kind, ROMlib_send_control_message, ROMlib_set_control_image_content, ROMlib_get_control_image_content,
             ROMlib_set_control_font_style, ROMlib_set_control_text, ROMlib_set_user_pane_callback, ctl_color_init,
             ROMlib_control_activate, ROMlib_control_best_rect, ROMlib_control_cycle_focus, ROMlib_control_data,
             ROMlib_control_focus,
             ROMlib_control_idle, ROMlib_control_region, ROMlib_set_up_control_color,
             ROMlib_live_control_aux, ROMlib_set_control_supervisor, ROMlib_control_hit_target,
             ROMlib_handle_control_click, ROMlib_handle_control_key, lookup_aux_ctl

  Origin:    Existing repository source in executor/src/controls; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1988, 1989, 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ControlMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "WindowMgr.h"
#include "ControlMgr.h"
#include "DialogMgr.h"
#include "EventMgr.h"
#include "ToolboxEvent.h"
#include "MemoryMgr.h"
#include "TextEdit.h"
#include "Iconutil.h"
#include "ToolboxUtil.h"

#include "rsys/cquick.h"
#include "rsys/ctl.h"
#include "rsys/resource.h"
#include "rsys/hook.h"
#include "rsys/wind.h"
#include "rsys/layers.h"

typedef struct control_font_style {
    ControlHandle control;
    INTEGER fields[6];
    struct control_font_style* next;
} control_font_style;

PRIVATE control_font_style* control_font_styles;

PUBLIC LONGINT ROMlib_set_control_font_style(ControlHandle control, const INTEGER* style)
{
    WindowPeek window;
    ControlHandle candidate;
    control_font_style* entry;
    boolean_t found = FALSE;

    if (!control || !style)
        return paramErr;
    for (window = MR(WindowList); window; window = WINDOW_NEXT_WINDOW(window))
        for (candidate = WINDOW_CONTROL_LIST(window); candidate; candidate = CTL_NEXT_CONTROL(candidate))
            if (candidate == control)
                found = TRUE;
    if (!found)
        return paramErr;
    /* The classic CDEF supports font, face, size and text mode.  Do not claim
     per-control colors or justification until the CDEF implements them. */
    if (CW(style[0]) & ~0x27)
        return -4; /* unimpErr */
    for (entry = control_font_styles; entry; entry = entry->next)
        if (entry->control == control)
            break;
    if (!entry) {
        entry = malloc(sizeof *entry);
        if (!entry)
            return memFullErr;
        entry->control = control;
        entry->next = control_font_styles;
        control_font_styles = entry;
    }
    memcpy(entry->fields, style, sizeof entry->fields);
    return noErr;
}

PUBLIC void ROMlib_apply_control_font_style(ControlHandle control)
{
    control_font_style* entry;
    for (entry = control_font_styles; entry; entry = entry->next)
        if (entry->control == control) {
            unsigned flags = CW(entry->fields[0]);
            if (flags & 1)
                TextFont(CW(entry->fields[1]));
            if (flags & 2)
                TextFace(CW(entry->fields[3]));
            if (flags & 4)
                TextSize(CW(entry->fields[2]));
            if (flags & 0x20)
                TextMode(CW(entry->fields[4]));
            break;
        }
}

PUBLIC void ROMlib_forget_control_font_style(ControlHandle control)
{
    control_font_style** entry;
    for (entry = &control_font_styles; *entry; entry = &(*entry)->next)
        if ((*entry)->control == control) {
            control_font_style* old = *entry;
            *entry = old->next;
            free(old);
            break;
        }
}

/* cheat, and make this a plain ctab, so we can use our old accessor
   macros */
AuxCtlHandle default_aux_ctl;

#define BLACK_RGB \
    {             \
        CWC(0),   \
        CWC(0),   \
        CWC(0),   \
    }
#define WHITE_RGB    \
    {                \
        CWC(0xFFFF), \
        CWC(0xFFFF), \
        CWC(0xFFFF), \
    }

#define LT_BLUISH_RGB {CWC(0xCCCC), CWC(0xCCCC), CWC(0xFFFF)}
#define DK_BLUISH_RGB {CWC(0x3333), CWC(0x3333), CWC(0x6666)}

#define DK_GRAY {CWC(0x5555), CWC(0x5555), CWC(0x5555)}

const ColorSpec default_ctl_colors[] = {
    {CWC(cFrameColor), BLACK_RGB},
    {CWC(cBodyColor), WHITE_RGB},
    {CWC(cTextColor), BLACK_RGB},
    {CWC(cThumbColor), WHITE_RGB},
    {CWC(4), DK_GRAY},
    {CWC(cArrowsColorLight), WHITE_RGB},
    {CWC(cArrowsColorDark), BLACK_RGB},
    {CWC(cThumbLight), WHITE_RGB},
    {CWC(cThumbDark), BLACK_RGB},
    /* used same as the w... color component */
    {CWC(cHiliteLight), WHITE_RGB},
    {CWC(cHiliteDark), BLACK_RGB},
    {CWC(cTitleBarLight), WHITE_RGB},
    {CWC(cTitleBarDark), BLACK_RGB},
    {CWC(cTingeLight), LT_BLUISH_RGB},
    {CWC(cTingeDark), DK_BLUISH_RGB},
};

#define KEEP_DEFAULT_CTL_CTAB_AROUND_FOR_OLD_SYSTEM_FILE_USERS

#if defined(KEEP_DEFAULT_CTL_CTAB_AROUND_FOR_OLD_SYSTEM_FILE_USERS)
PUBLIC CTabHandle default_ctl_ctab;
#endif

void ctl_color_init(void)
{
    default_aux_ctl = (AuxCtlHandle)NewHandle(sizeof(AuxCtlRec));

#if defined(KEEP_DEFAULT_CTL_CTAB_AROUND_FOR_OLD_SYSTEM_FILE_USERS)
    default_ctl_ctab = (CTabHandle)ROMlib_getrestid(TICK("cctb"), 0);

    if (!default_ctl_ctab) {
        default_ctl_ctab = (CTabHandle)NewHandle(CTAB_STORAGE_FOR_SIZE(14));
        CTAB_SIZE_X(default_ctl_ctab) = CWC(14);
        CTAB_SEED_X(default_ctl_ctab) = CLC(0);
        CTAB_FLAGS_X(default_ctl_ctab) = CWC(0);
        memcpy(&CTAB_TABLE(default_ctl_ctab)[0], &default_ctl_colors[0], 15 * sizeof default_ctl_colors[0]);
    }
#endif

    PP_RAW(HxX(default_aux_ctl, acNext)) = CLC_NULL;
    FIELD_ASSIGN_RAW(PP_RAW(HxX(default_aux_ctl, acOwner)), CLC_NULL);
    FIELD_ASSIGN_RAW(PP_RAW(HxX(default_aux_ctl, acCTable)), (CCTabHandle)RM(GetResource(TICK("cctb"), 0)));
    HxX(default_aux_ctl, acFlags) = CWC(0);
    HxX(default_aux_ctl, acReserved) = CLC(0);
    HxX(default_aux_ctl, acRefCon) = CLC(0);
}

HIDDEN_AuxCtlHandle* lookup_aux_ctl(ControlHandle ctl)
{
    HIDDEN_AuxCtlHandle* t;

    for (t = &AuxCtlHead_H; HIDDEN_RAW(*t) && HxP(HIDDEN_NATIVE(*t), acOwner) != ctl;
         t = (HIDDEN_AuxCtlHandle*)&PP_RAW(HxX(HIDDEN_NATIVE(*t), acNext)))
        ;
    return t;
}

P2(PUBLIC pascal trap, void, SetCRefCon, ControlHandle, c, /* IMI-327 */
   LONGINT, data)
{
    HxX(c, contrlRfCon) = CL(data);
}

P1(PUBLIC pascal trap, LONGINT, GetCRefCon, ControlHandle, c) /* IMI-327 */
{
    return Hx(c, contrlRfCon);
}

P2(PUBLIC pascal trap, void, SetCtlAction, ControlHandle, c, /* IMI-328 */
   ProcPtr, a)
{
    if ((LONGINT)a != CLC(-1))
        FIELD_ASSIGN_RAW(PP_RAW(HxX(c, contrlAction)), RM(a));
    else
        FIELD_ASSIGN_RAW(PP_RAW(HxX(c, contrlAction)), (ProcPtr)CLC(-1));
}

P1(PUBLIC pascal trap, ProcPtr, GetCtlAction, ControlHandle, c) /* IMI-328 */
{
    return HxP(c, contrlAction);
}

P1(PUBLIC pascal trap, INTEGER, GetCVariant, ControlHandle, c) /* IMV-222 */
{
    AuxCtlHandle h;

    for (h = MR(AuxCtlHead); h != 0 && HxP(h, acOwner) != c; h = HxP(h, acNext))
        ;
    return h != 0 ? Hx(h, acFlags) : 0;
}

/* according to IM-MTE; this has been renamed
   `GetAuxiliaryControlRecord ()', possibly because of the
   inconsistency below, i can only assume they have the same trap word */
P2(PUBLIC pascal trap, BOOLEAN, GetAuxCtl, ControlHandle, ctl, /* IMV-222 */
   HIDDEN_AuxCtlHandle*, aux_ctl)
{
    /* according to testing on the Mac+
     `GetAuxCtl ()' returns FALSE (not TRUE) and leaves
     aux_ctl untouched; this is not the case of later
     mac version, such as the color classic v7.1 */

    if (!ctl) {
        HIDDEN_RAW_SET(*(aux_ctl), RM(default_aux_ctl));
        return TRUE;
    }
    else {
        AuxCtlHandle t;

        t = HIDDEN_RAW(*lookup_aux_ctl(ctl));
        if (t) {
            HIDDEN_RAW_SET(*(aux_ctl), t);
            return FALSE;
        }
        else {
            HIDDEN_RAW_SET(*(aux_ctl), RM(default_aux_ctl));
            return TRUE;
        }
    }
}

static void* pane_callback(AuxCtlHandle aux, ResType tag)
{
    int kind = aux ? Hx(aux, acReserved) : 0;
    if ((kind & ~15) != ROMlib_user_pane_proc || kind == ROMlib_root_control_proc)
        return NULL;
    Byte* callbacks = (Byte*)&ROMlib_aux_control_extra(aux)->callbacks;
    switch (tag) {
    case 0x64726177: /* 'draw' */
        return callbacks + offsetof(ROMlib_pane_callbacks, draw);
    case 0x68697474: /* 'hitt' */
        return callbacks + offsetof(ROMlib_pane_callbacks, hit);
    case 0x69646c65: /* 'idle' */
        return callbacks + offsetof(ROMlib_pane_callbacks, idle);
    case 0x61637469: /* 'acti' */
        return callbacks + offsetof(ROMlib_pane_callbacks, activate);
    case 0x666f6369: /* 'foci' */
        return callbacks + offsetof(ROMlib_pane_callbacks, focus);
    default:
        return NULL;
    }
}

PUBLIC OSErr ROMlib_set_user_pane_callback(ControlHandle control, ResType tag, const Byte* data)
{
    void* callback = pane_callback(HIDDEN_NATIVE(*lookup_aux_ctl(control)), tag);
    if (!callback)
        return -30581; /* errDataNotSupported */
    memcpy(callback, data, sizeof(uint32));
    return noErr;
}

static ROMlib_aux_control_data* control_hierarchy(uint32 address)
{
    AuxCtlHandle aux = address ? HIDDEN_NATIVE(*lookup_aux_ctl(MR(address))) : NULL;
    return aux ? ROMlib_aux_control_extra(aux) : NULL;
}

PUBLIC void ROMlib_detach_control(ControlHandle control)
{
    ROMlib_aux_control_data* child = control_hierarchy(RM(control));
    if (!child || !child->parent)
        return;
    ROMlib_aux_control_data* parent = control_hierarchy(child->parent);
    ROMlib_aux_control_data* previous = control_hierarchy(child->previous_sibling);
    ROMlib_aux_control_data* next = control_hierarchy(child->next_sibling);
    if (previous)
        previous->next_sibling = child->next_sibling;
    else if (parent)
        parent->first_child = child->next_sibling;
    if (next)
        next->previous_sibling = child->previous_sibling;
    else if (parent)
        parent->last_child = child->previous_sibling;
    child->parent = child->previous_sibling = child->next_sibling = 0;
}

PUBLIC OSErr ROMlib_embed_control(ControlHandle control, ControlHandle parent)
{
    WindowPeek window;
    ControlHandle candidate;
    AuxCtlHandle child_aux = NULL, parent_aux = NULL;
    for (window = MR(WindowList); window; window = WINDOW_NEXT_WINDOW(window)) {
        child_aux = parent_aux = NULL;
        for (candidate = WINDOW_CONTROL_LIST(window); candidate; candidate = CTL_NEXT_CONTROL(candidate)) {
            if (candidate == control)
                child_aux = HIDDEN_NATIVE(*lookup_aux_ctl(candidate));
            if (candidate == parent)
                parent_aux = HIDDEN_NATIVE(*lookup_aux_ctl(candidate));
        }
        if (child_aux && parent_aux)
            break;
    }
    if (!window)
        return paramErr;
    int parent_kind = Hx(parent_aux, acReserved);
    if (parent_kind != ROMlib_root_control_proc && (parent_kind & ~15) != ROMlib_group_box_proc &&
        ((parent_kind & ~15) != ROMlib_user_pane_proc || !(CTL_VALUE(parent) & 2))) {
        uint32 features;
        /* A loaded CDEF advertises embedding through its feature message. It
           has no native pane kind, even for a standard Appearance group box. */
        if (parent_kind || ROMlib_get_control_features(parent, &features) || !(CL(features) & 2))
            return -30590; /* errControlIsNotEmbedder */
        child_aux = ROMlib_live_control_aux(control);
        parent_aux = ROMlib_live_control_aux(parent);
        if (!child_aux || !parent_aux || CTL_OWNER(control) != CTL_OWNER(parent))
            return paramErr;
    }
    if (Hx(child_aux, acReserved) == ROMlib_root_control_proc)
        return paramErr;
    for (candidate = parent; candidate;) {
        if (candidate == control)
            return paramErr;
        AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(candidate));
        candidate = aux ? MR(ROMlib_aux_control_extra(aux)->parent) : NULL;
    }
    ROMlib_detach_control(control);
    ROMlib_aux_control_data* child = ROMlib_aux_control_extra(child_aux);
    ROMlib_aux_control_data* storage = ROMlib_aux_control_extra(parent_aux);
    child->parent = RM(parent);
    child->previous_sibling = storage->last_child;
    ROMlib_aux_control_data* previous = control_hierarchy(storage->last_child);
    if (previous)
        previous->next_sibling = RM(control);
    else
        storage->first_child = RM(control);
    storage->last_child = RM(control);
    return noErr;
}

PUBLIC OSErr ROMlib_auto_embed_control(ControlHandle control, WindowPtr window)
{
    HIDDEN_ControlHandle root;
    OSErr error = ROMlib_get_root_control(window, &root);
    if (error)
        return error;
    return ROMlib_embed_control(control, HIDDEN_NATIVE(root));
}

PUBLIC int ROMlib_native_control_kind(ControlHandle control)
{
    AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(control));
    return aux ? Hx(aux, acReserved) : 0;
}

PUBLIC OSErr ROMlib_get_control_features(ControlHandle control, uint32* features)
{
    if (!control || !STARH(control) || !features)
        return paramErr;
    *features = 0;
    int kind = ROMlib_native_control_kind(control);
    switch (kind & ~15) {
    case ROMlib_image_well_proc:
    case ROMlib_bevel_button_proc:
    case ROMlib_edit_text_proc:
    case ROMlib_static_text_proc:
        *features = CL(1U << 6); /* kControlSupportsDataAccess */
        return noErr;
    case ROMlib_user_pane_proc:
        *features = CL((kind == ROMlib_root_control_proc ? 2U : (unsigned)CTL_VALUE(control) & 0x1eU) | (1U << 6));
        return noErr;
    case ROMlib_chasing_arrows_proc:
        *features = CLC((1U << 3) | (1U << 6)); /* Idle and Boolean animation data. */
        return noErr;
    case ROMlib_slider_proc:
    case ROMlib_progress_bar_proc:
        if (kind == ROMlib_progress_bar_proc)
            *features = CLC((1U << 3) | (1U << 6));
        return noErr;
    case ROMlib_tabs_proc:
    case ROMlib_separator_proc:
        return noErr;
    case ROMlib_group_box_proc:
        *features = CLC(2); /* kControlSupportsEmbedding */
        return noErr;
    default:
        /* Custom CDEFs opt in before receiving Appearance-only messages. */
        if ((uint32)CTLCALL(control, 27, 0) != 0x206f6b20)
            return -30580; /* errMsgNotSupported */
        *features = CL((uint32)CTLCALL(control, 19, 0));
        return noErr;
    }
}

PUBLIC OSErr ROMlib_control_best_rect(ControlHandle control, Rect* bounds, INTEGER* baseline)
{
    *baseline = 0;
    *bounds = CTL_RECT(control);
    uint32 features;
    if (!ROMlib_get_control_features(control, &features) && (CL(features) & 0x200)) {
        _Alignas(16) INTEGER dimensions[3] = {0};
        CTL_CALL_EXCURSION(control, { CTLCALL(control, 14, US_TO_SYN68K(dimensions)); });
        bounds->bottom = CW(CW(bounds->top) + CW(dimensions[0]));
        bounds->right = CW(CW(bounds->left) + CW(dimensions[1]));
        *baseline = dimensions[2];
    }
    return noErr;
}

PUBLIC OSErr ROMlib_control_region(ControlHandle control, INTEGER part, RgnHandle region)
{
    if (!part || part < -2 || part > 255)
        return -30588; /* errInvalidPartCode */
    uint32 features;
    if (!ROMlib_get_control_features(control, &features) && (CL(features) & 0x20000)) {
        _Alignas(16) struct PACKED {
            uint32 region;
            INTEGER part;
        } request = {RM(region), CW(part)};

        LONGINT result;
        CTL_CALL_EXCURSION(control, { result = CTLCALL(control, 31, US_TO_SYN68K(&request)); });
        return (OSErr)result;
    }
    if (part != -1 && part != inThumb)
        return -30588;
    CTL_CALL_EXCURSION(control, { CTLCALL(control, part == -1 ? calcCntlRgn : calcThumbRgn, (intptr_t)region); });
    return noErr;
}

PRIVATE boolean_t is_text_control(ControlHandle control)
{
    int kind = ROMlib_native_control_kind(control) & ~15;
    return kind == ROMlib_edit_text_proc || kind == ROMlib_static_text_proc;
}

PUBLIC OSErr ROMlib_set_control_text(ControlHandle control, Size size, const Byte* data)
{
    if (!is_text_control(control))
        return -30581;
    if (size < 0 || size > 255 || (size && !data))
        return paramErr;
    if (size)
        memmove(CTL_TITLE(control) + 1, data, size);
    CTL_TITLE(control)[0] = size;
    if (CTL_VIS(control))
        Draw1Control(control);
    return noErr;
}

PUBLIC OSErr ROMlib_get_control_text(ControlHandle control, Size size, Byte* data, Size* actual)
{
    if (!is_text_control(control))
        return -30581;
    Size length = CTL_TITLE(control)[0];
    if (actual)
        *actual = CL(length);
    if (size < length || (length && !data))
        return -30591;
    if (length)
        memmove(data, CTL_TITLE(control) + 1, length);
    return noErr;
}

PUBLIC AuxCtlHandle ROMlib_live_control_aux(ControlHandle control)
{
    AuxCtlHandle aux = control ? HIDDEN_NATIVE(*lookup_aux_ctl(control)) : NULL;
    if (!aux)
        return NULL;
    /* An installed Control Manager can remove a control before unlinking its
       auxiliary record. Validate against the actual owner list as well. */
    for (WindowPeek window = ROMlib_window_first_all(); window; window = ROMlib_window_next_all(window))
        for (ControlHandle candidate = WINDOW_CONTROL_LIST(window); candidate; candidate = CTL_NEXT_CONTROL(candidate))
            if (candidate == control)
                return aux;
    return ROMlib_ppc_control_in_saved_window(control) ? aux : NULL;
}

PUBLIC OSErr ROMlib_set_control_supervisor(ControlHandle control, ControlHandle supervisor)
{
    AuxCtlHandle aux = ROMlib_live_control_aux(control);
    if (!aux || (supervisor && (!ROMlib_live_control_aux(supervisor) || CTL_OWNER(control) != CTL_OWNER(supervisor))))
        return paramErr;
    HIDDEN_ControlHandle root;
    const OSErr error = ROMlib_get_root_control(CTL_OWNER(control), &root);
    if (error)
        return error;
    ROMlib_aux_control_extra(aux)->supervisor = supervisor ? US_TO_SYN68K(supervisor) : 0;
    return noErr;
}

PUBLIC ControlHandle ROMlib_control_hit_target(ControlHandle control)
{
    AuxCtlHandle aux = ROMlib_live_control_aux(control);
    if (!aux)
        return control;
    ROMlib_aux_control_data* extra = ROMlib_aux_control_extra(aux);
    if (!extra->supervisor)
        return control;
    ControlHandle supervisor = SYN68K_TO_US(extra->supervisor);
    if (ROMlib_live_control_aux(supervisor) && CTL_OWNER(control) == CTL_OWNER(supervisor))
        return supervisor;
    extra->supervisor = 0;
    return control;
}

static boolean_t control_has_feature(ControlHandle control, uint32 feature)
{
    uint32 features;
    return ROMlib_get_control_features(control, &features) == noErr && (CL(features) & feature);
}

PUBLIC INTEGER ROMlib_handle_control_click(ControlHandle control, Point where, INTEGER modifiers, ProcPtr action)
{
    if (!ROMlib_live_control_aux(control))
        return 0;

    _Alignas(8) struct PACKED {
        Point where;
        INTEGER modifiers;
        uint32 action;
    } tracking = {
        where,
        CW(modifiers),
        (uintptr_t)action == UINTPTR_MAX ? (uint32)(uintptr_t)PP_RAW(CTL_ACTION_X(control))
        : action                         ? CL(US_TO_SYN68K(action))
                                         : 0,
    };

    INTEGER result = 0;
    CTL_CALL_EXCURSION(control, {
        if (control_has_feature(control, 0x20))
            result = ROMlib_send_control_message(control, 15, US_TO_SYN68K(&tracking));
        else
            result = TrackControl(control, where, action);
    });
    return result;
}

PUBLIC INTEGER ROMlib_handle_control_key(ControlHandle control, INTEGER key, INTEGER character, INTEGER modifiers)
{
    if (!ROMlib_live_control_aux(control))
        return 0;
    _Alignas(8) INTEGER pressed[] = {CW(modifiers), CW(key), CW(character)};
    INTEGER result = 0;
    CTL_CALL_EXCURSION(control, {
        if (control_has_feature(control, 4))
            result = ROMlib_send_control_message(control, 17, US_TO_SYN68K(pressed));
    });
    return result;
}

PUBLIC int32 ROMlib_set_up_control_color(ControlHandle control, int16 depth, Byte color_device, boolean_t text)
{
    enum { set_up_background = 23, apply_text_color = 30, special_background = 0x80, color_proc_info = 0x14ac };

    AuxCtlHandle aux = ROMlib_live_control_aux(control);
    if (!aux)
        return paramErr;
    int16 message = text ? apply_text_color : set_up_background;
    uint32 procedure = ROMlib_aux_control_extra(aux)->color_proc;
    if (procedure && !CToPascalCall(SYN68K_TO_US(procedure), color_proc_info, control, message, depth, color_device))
        return noErr;

    aux = ROMlib_live_control_aux(control);
    if (!aux)
        return paramErr;
    boolean_t active = CTL_HILITE(control) != INACTIVE;
    uint32 ancestor = ROMlib_aux_control_extra(aux)->parent;
    for (unsigned i = 0; ancestor && i < UINT16_MAX; ++i) {
        ControlHandle parent = (ControlHandle)MR(ancestor);
        AuxCtlHandle parent_aux = ROMlib_live_control_aux(parent);
        if (!parent_aux)
            break;
        ancestor = ROMlib_aux_control_extra(parent_aux)->parent;
        if (control_has_feature(parent, special_background) && ROMlib_live_control_aux(parent)) {
            _Alignas(16) struct PACKED {
                int16 depth;
                Byte color_device;
                Byte active;
            } arguments = {CW(depth), color_device, active};

            CTLCALL(parent, message, US_TO_SYN68K(&arguments));
            return noErr;
        }
    }

    if (text) {
        static const RGBColor inactive = {CWC(0x7777), CWC(0x7777), CWC(0x7777)};
        RGBForeColor(!active && color_device && depth > 1 ? &inactive : &ROMlib_black_rgb_color);
    }
    else if (ROMlib_live_control_aux(control)) {
        GrafPtr owner = (GrafPtr)CTL_OWNER(control);
        RGBColor background = ROMlib_white_rgb_color;
        if (color_device && depth > 1 && CGrafPort_p(owner))
            background = CPORT_RGB_BK_COLOR(owner);
        RGBBackColor(&background);
    }
    return noErr;
}

static INTEGER send_focus(ControlHandle control, INTEGER part)
{
    INTEGER result;
    CTL_CALL_EXCURSION(control, { result = CTLCALL(control, 16, part); });
    return result;
}

PUBLIC OSErr ROMlib_control_focus(WindowPtr window, ControlHandle control, INTEGER part, HIDDEN_ControlHandle* output)
{
    if (output)
        HIDDEN_RAW_SET(*output, 0);
    if (!ROMlib_valid_window(window))
        return paramErr;
    AuxCtlHandle target = ROMlib_live_control_aux(control);
    if (control && (!target || CTL_OWNER(control) != window))
        return -30584; /* errUnknownControl */

    ControlHandle previous = NULL;
    for (ControlHandle candidate = WINDOW_CONTROL_LIST(window); candidate; candidate = CTL_NEXT_CONTROL(candidate)) {
        AuxCtlHandle aux = ROMlib_live_control_aux(candidate);
        if (aux && ROMlib_aux_control_extra(aux)->focus_part) {
            previous = candidate;
            break;
        }
    }
    const boolean_t dialog = WINDOW_KIND(window) == dialogKind && DIALOG_TEXTH((DialogPtr)window);
    const INTEGER active_item = dialog ? DIALOG_EDIT_FIELD((DialogPtr)window) + 1 : 0;
    if (output) {
        if (previous)
            HIDDEN_RAW_SET(*output, RM(previous));
        else if (active_item > 0)
            return ROMlib_get_dialog_control(window, active_item, output);
        return noErr;
    }

    const INTEGER edit_item = control && part && dialog ? ROMlib_dialog_edit_control_item(control) : 0;
    if (edit_item) {
        INTEGER type;
        HIDDEN_Handle handle;
        Rect bounds;
        GetDItem((DialogPtr)window, edit_item, &type, &handle, &bounds);
        if ((CW(type) & itemDisable) || CW(bounds.left) >= 8192)
            return -30582; /* errControlDoesntSupportFocus */
    }
    INTEGER chosen = 0;
    if (control && part && !edit_item) {
        if (!CTL_VIS(control) || CTL_HILITE(control) == INACTIVE || !control_has_feature(control, 1U << 2))
            return -30582;
        chosen = send_focus(control, part);
        target = ROMlib_live_control_aux(control);
        if (!target || !chosen)
            return -30582;
    }
    if (previous && (!chosen || previous != control)) {
        AuxCtlHandle aux = ROMlib_live_control_aux(previous);
        if (aux) {
            ROMlib_aux_control_extra(aux)->focus_part = 0;
            send_focus(previous, 0);
        }
    }
    if (active_item && active_item != edit_item) {
        THEPORT_SAVE_EXCURSION((GrafPtr)window, { TEDeactivate(DIALOG_TEXTH((DialogPtr)window)); });
        DIALOG_EDIT_FIELD_X((DialogPtr)window) = CWC(-1);
        DIALOG_EDIT_OPEN_X((DialogPtr)window) = 0;
    }
    if (edit_item && edit_item != active_item) {
        THEPORT_SAVE_EXCURSION((GrafPtr)window, { SelIText((DialogPtr)window, edit_item, 0, INT16_MAX); });
    }
    if (chosen && (target = ROMlib_live_control_aux(control)))
        ROMlib_aux_control_extra(target)->focus_part = CW(chosen);
    return noErr;
}

PUBLIC OSErr ROMlib_control_activate(ControlHandle control, boolean_t active)
static OSErr collect_focus_controls(ControlHandle root, ControlHandle** result, unsigned* count)
{
    unsigned capacity = 0;
    const uint32 raw_root = RM(root);
    const ROMlib_aux_control_data* root_extra = ROMlib_aux_control_extra(ROMlib_live_control_aux(root));
    uint32 address = root_extra->first_child;
    for (unsigned visited = 0; address && visited < UINT16_MAX; ++visited) {
        ControlHandle control = MR(address);
        AuxCtlHandle aux = ROMlib_live_control_aux(control);
        if (!aux)
            return paramErr;
        const ROMlib_aux_control_data* extra = ROMlib_aux_control_extra(aux);
        if (CTL_VIS(control) && CTL_HILITE(control) != INACTIVE) {
            if (*count == capacity) {
                const unsigned next_capacity = capacity ? 2 * capacity : 8;
                ControlHandle* next = realloc(*result, next_capacity * sizeof *next);
                if (!next)
                    return memFullErr;
                *result = next;
                capacity = next_capacity;
            }
            (*result)[(*count)++] = control;
            if (extra->first_child) {
                address = extra->first_child;
                continue;
            }
        }
        while (!extra->next_sibling && extra->parent && extra->parent != raw_root) {
            aux = ROMlib_live_control_aux(MR(extra->parent));
            if (!aux || ++visited == UINT16_MAX)
                return paramErr;
            extra = ROMlib_aux_control_extra(aux);
        }
        address = extra->next_sibling;
    }
    return address ? paramErr : noErr;
}

PUBLIC OSErr ROMlib_control_cycle_focus(WindowPtr window, boolean_t reverse)
{
    HIDDEN_ControlHandle root, focus;
    OSErr error = ROMlib_get_root_control(window, &root);
    if (error)
        return error;
    error = ROMlib_control_focus(window, NULL, 0, &focus);
    if (error)
        return error;

    unsigned count = 0;
    ControlHandle* controls = NULL;
    error = collect_focus_controls(HIDDEN_NATIVE(root), &controls, &count);
    if (!error) {
        const INTEGER part = reverse ? -2 : -1;
        ControlHandle previous = HIDDEN_NATIVE(focus);
        unsigned start = reverse || !count ? 0 : count - 1;
        for (unsigned i = 0; i < count; ++i) {
            if (controls[i] == previous) {
                start = i;
                break;
            }
        }
        if (!previous || ROMlib_control_focus(window, previous, part, NULL)) {
            ROMlib_control_focus(window, NULL, 0, NULL);
            error = -30585; /* errCouldntSetFocus */
            for (unsigned i = 0; i < count; ++i) {
                start = reverse ? (start ? start - 1 : count - 1) : (start + 1 == count ? 0 : start + 1);
                if (!ROMlib_control_focus(window, controls[start], part, NULL)) {
                    error = noErr;
                    break;
                }
            }
        }
    }
    free(controls);
    return error;
}

{
    if (!ROMlib_live_control_aux(control))
        return -30584;
    if ((CTL_HILITE(control) != INACTIVE) == active)
        return noErr;
    if (!active && ROMlib_aux_control_extra(ROMlib_live_control_aux(control))->focus_part)
        ROMlib_control_focus(CTL_OWNER(control), NULL, 0, NULL);
    if (!ROMlib_live_control_aux(control))
        return noErr;
    HiliteControl(control, active ? 0 : INACTIVE);
    if (ROMlib_live_control_aux(control) && control_has_feature(control, 1U << 4))
        CTL_CALL_EXCURSION(control, { CTLCALL(control, 22, active); });
    return noErr;
}

PUBLIC void ROMlib_control_idle(WindowPtr window)
{
    if (!ROMlib_valid_window(window))
        return;
    if (WINDOW_KIND(window) == dialogKind && DIALOG_EDIT_FIELD((DialogPtr)window) >= 0 &&
        DIALOG_TEXTH((DialogPtr)window))
        THEPORT_SAVE_EXCURSION((GrafPtr)window, { TEIdle(DIALOG_TEXTH((DialogPtr)window)); });
    for (ControlHandle control = WINDOW_CONTROL_LIST(window); ROMlib_live_control_aux(control);) {
        ControlHandle next = CTL_NEXT_CONTROL(control);
        if (CTL_VIS(control) && CTL_HILITE(control) != INACTIVE && control_has_feature(control, 1U << 3))
            CTL_CALL_EXCURSION(control, { CTLCALL(control, 18, 0); });
        control = next;
    }
}

static boolean_t system_user_pane_upp(Handle definition, ResType tag)
{
    switch (tag) {
    case T('d', 'r', 'a', 'w'):
    case T('h', 'i', 't', 't'):
    case T('t', 'r', 'a', 'k'):
    case T('i', 'd', 'l', 'e'):
    case T('k', 'e', 'y', 'd'):
    case T('a', 'c', 't', 'i'):
    case T('f', 'o', 'c', 'i'):
    case T('b', 'a', 'c', 'k'):
        break;
    default:
        return FALSE;
    }
    resmaphand owner;
    typref* type;
    resref* resource;
    if (!definition || ROMlib_findres(definition, &owner, &type, &resource) != noErr ||
        CL(type->rtyp) != TICK("CDEF") || CW(resource->rid) != (ROMlib_user_pane_proc >> 4))
        return FALSE;
    for (resmaphand map = (resmaphand)HIDDEN_NATIVE(SysMapHndl_H); map && STARH(map);
         map = (resmaphand)HxP(map, nextmap)) {
        if (map == owner)
            return TRUE;
    }
    return FALSE;
}

PUBLIC LONGINT ROMlib_control_data(ControlHandle control, INTEGER part, ResType tag, Size size, Byte* data,
                                   Size* actual, enum ROMlib_control_data_op operation)
{
    AuxCtlHandle aux = ROMlib_live_control_aux(control);
    if (!aux)
        return -30584;
    if (size < 0 || (size && !data))
        return paramErr;
    if (part == 0) {
        if (tag == TICK("text") && is_text_control(control)) {
            if (operation == ROMLIB_CONTROL_WRITE)
                return ROMlib_set_control_text(control, size, data);
            if (operation == ROMLIB_CONTROL_MEASURE) {
                *actual = CL(CTL_TITLE(control)[0]);
                return noErr;
            }
            return ROMlib_get_control_text(control, size, data, actual);
        }
        void* callback = pane_callback(aux, tag);
        if (callback) {
            if (operation == ROMLIB_CONTROL_WRITE) {
                if (size != 4)
                    return -30591;
                memcpy(callback, data, 4);
                return noErr;
            }
            if (actual)
                *actual = CLC(4);
            if (operation == ROMLIB_CONTROL_MEASURE)
                return noErr;
            if (size < 4)
                return -30591;
            memcpy(data, callback, 4);
            return noErr;
        }
        const int definition = ROMlib_native_control_kind(control);
        const int kind = definition & ~15;
        if (tag == TICK("anim") && kind == ROMlib_chasing_arrows_proc) {
            if (actual)
                *actual = CLC(1);
            if (operation == ROMLIB_CONTROL_MEASURE)
                return noErr;
            if (size < 1 || (operation == ROMLIB_CONTROL_WRITE && size != 1))
                return -30591;
            BOOLEAN* animating = &ROMlib_aux_control_extra(aux)->chasing_animating;
            if (operation == ROMLIB_CONTROL_WRITE)
                *animating = *data != 0;
            else
                *data = *animating;
            return noErr;
        }
        if (tag == TICK("tran") && (kind == ROMlib_image_well_proc || kind == ROMlib_bevel_button_proc)) {
            const Size required = sizeof(INTEGER);
            if (actual)
                *actual = CL(required);
            if (operation == ROMLIB_CONTROL_MEASURE)
                return noErr;
            if (size < required || (operation == ROMLIB_CONTROL_WRITE && size != required))
                return -30591;
            void* transform = &ROMlib_aux_control_extra(aux)->image_transform;
            if (operation == ROMLIB_CONTROL_WRITE) {
                memcpy(transform, data, required);
                CTL_CALL_EXCURSION(control, { InvalRect(&CTL_RECT(control)); });
            }
            else
                memcpy(data, transform, required);
            return noErr;
        }
        const boolean_t image =
        if (definition == ROMlib_progress_bar_proc && (tag == TICK("inde") || tag == TICK("anim"))) {
            if (actual)
                *actual = CLC(1);
            if (operation == ROMLIB_CONTROL_MEASURE)
                return noErr;
            if (size < 1 || (operation == ROMLIB_CONTROL_WRITE && size != 1))
                return -30591;
            ROMlib_aux_control_data* extra = ROMlib_aux_control_extra(aux);
            BOOLEAN* flag = tag == TICK("inde") ? &extra->progress_indeterminate : &extra->progress_animating;
            if (operation == ROMLIB_CONTROL_WRITE) {
                *flag = *data != 0;
                CTL_CALL_EXCURSION(control, { InvalRect(&CTL_RECT(control)); });
            }
            else
                *data = *flag;
            return noErr;
        }
            tag == TICK("cont") && (kind == ROMlib_image_well_proc || kind == ROMlib_bevel_button_proc);
        const boolean_t bevel =
            kind == ROMlib_bevel_button_proc && (tag == TICK("mval") || tag == TICK("gali") || tag == TICK("goff") ||
                                                 tag == TICK("tali") || tag == TICK("toff") || tag == TICK("tplc"));
        if (image || bevel) {
            const Size required = image ? 6 : tag == TICK("goff") ? 4 : 2;
            if (actual)
                *actual = CL(required);
            if (operation == ROMLIB_CONTROL_MEASURE)
                return noErr;
            if (size < required || (operation == ROMLIB_CONTROL_WRITE && size != required))
                return -30591;
            if (image)
                return operation == ROMLIB_CONTROL_WRITE ? ROMlib_set_control_image_content(control, data, 0)
                                                         : ROMlib_get_control_image_content(control, data, 0);
            INTEGER value;
            Point offset = ROMlib_bevel_graphic_offset(control);
            if (operation == ROMLIB_CONTROL_WRITE) {
                memcpy(&value, data, 2);
                value = CW(value);
                if (tag == TICK("mval"))
                    return ROMlib_set_bevel_menu_value(control, value);
                if (tag == TICK("tplc"))
                    return ROMlib_set_bevel_text_placement(control, value);
                if (tag == TICK("tali") || tag == TICK("toff"))
                    return ROMlib_set_bevel_text_alignment(
                        control, tag == TICK("tali") ? value : ROMlib_bevel_text_alignment(control),
                        tag == TICK("toff") ? value : ROMlib_bevel_text_offset(control));
                if (tag == TICK("goff")) {
                    memcpy(&offset, data, sizeof offset);
                    return ROMlib_set_bevel_graphic_alignment(control, ROMlib_bevel_graphic_alignment(control),
                                                              CW(offset.h), CW(offset.v));
                }
                return ROMlib_set_bevel_graphic_alignment(control, value, offset.h, offset.v);
            }
            if (tag == TICK("goff")) {
                offset.h = CW(offset.h);
                offset.v = CW(offset.v);
                memcpy(data, &offset, sizeof offset);
                return noErr;
            }
            if (tag == TICK("mval"))
                return ROMlib_get_bevel_menu_value(control, (INTEGER*)data);
            switch (tag) {
            case 0x67616c69: /* 'gali' */
                value = ROMlib_bevel_graphic_alignment(control);
                break;
            case 0x74616c69: /* 'tali' */
                value = ROMlib_bevel_text_alignment(control);
                break;
            case 0x746f6666: /* 'toff' */
                value = ROMlib_bevel_text_offset(control);
                break;
            default:
                value = ROMlib_bevel_text_placement(control);
                break;
            }
            value = CW(value);
            memcpy(data, &value, 2);
            return noErr;
        }
        if (tag == TICK("dflt") && WINDOW_KIND(CTL_OWNER(control)) == dialogKind) {
            DialogPtr dialog = (DialogPtr)CTL_OWNER(control);
            const int count = CountDITL(dialog);
            for (int item = 1; item <= count; ++item) {
                INTEGER type;
                HIDDEN_Handle handle;
                Rect bounds;
                GetDItem(dialog, item, &type, &handle, &bounds);
                if (!(CW(type) & ctrlItem) || HIDDEN_NATIVE(handle) != (Handle)control)
                    continue;
                if (actual)
                    *actual = CLC(1);
                if (operation == ROMLIB_CONTROL_MEASURE)
                    return noErr;
                if (!size || (operation == ROMLIB_CONTROL_WRITE && size != 1))
                    return -30591;
                if (operation == ROMLIB_CONTROL_WRITE)
                    return C_SetDialogDefaultItem(dialog, *data ? item : 0);
                *data = DIALOG_ADEF_ITEM(dialog) == item;
                return noErr;
            }
        }
    }

    if ((!CTL_DEFPROC(control) && !ROMlib_aux_control_extra(aux)->custom_definition) ||
        !control_has_feature(control, 1U << 6))
        return -30581;
    /* The System user-pane CDEF copies a complete UPP even for a short read. */
    if (part == 0 && operation == ROMLIB_CONTROL_READ && size < (Size)sizeof(uint32) &&
        system_user_pane_upp(CTL_DEFPROC(control), tag)) {
        if (actual)
            *actual = CL(sizeof(uint32));
        return -30591;
    }
    /* ControlDataAccessRec is four big-endian words in the CDEF ABI.
       A size query uses the get-data message with a zero size and pointer. */
    _Alignas(16) uint32 fields[4] = {CL(tag), CL((int32)part), CL(size), CL(data ? US_TO_SYN68K(data) : 0)};
    LONGINT result;
    CTL_CALL_EXCURSION(
        control, { result = CTLCALL(control, operation == ROMLIB_CONTROL_WRITE ? 20 : 21, US_TO_SYN68K(fields)); });
    if (actual)
        *actual = fields[2];
    return result;
}

PRIVATE void init_native_tabs(ControlHandle control)
{
    Handle tabs = GetResource(TICK("tab#"), CTL_VALUE(control));
    int count = 0;
    if (tabs && STARH(tabs) && GetHandleSize(tabs) >= 4) {
        Size size = GetHandleSize(tabs);
        const Byte* data = STARH(tabs);
        int declared = data[2] * 256 + data[3];
        Size offset = 4;
        while (!data[0] && !data[1] && count < declared && count < 127 && size - offset >= 9 &&
               data[offset + 2] <= size - offset - 9) {
            offset += 9 + data[offset + 2];
            ++count;
        }
    }
    FIELD_ASSIGN_RAW(PP_RAW(CTL_DATA_X(control)), RM(count ? tabs : NULL));
    CTL_MIN_X(control) = CW(count != 0);
    CTL_MAX_X(control) = CW(count);
    CTL_VALUE_X(control) = CTL_MIN_X(control);
}

PRIVATE void draw_tab_icon(const Rect* bounds, INTEGER id)
{
    Handle bits = GetResource(TICK("icm8"), id);
    Handle mask = GetResource(TICK("icm#"), id);
    if (!bits || !mask || GetHandleSize(bits) < 192 || GetHandleSize(mask) < 48) {
        C_PlotIconID(bounds, atNone, ttNone, id);
        return;
    }
    CTabHandle colors = GetCTable(8);
    if (!colors)
        return;
    LOCK_HANDLE_EXCURSION_2(bits, mask, {
        PixMap icon = {0};
        BitMap bitmap;
        SetRect(&icon.bounds, 0, 0, 16, 12);
        FIELD_ASSIGN_RAW(icon.baseAddr, HIDDEN_RAW(*bits));
        icon.rowBytes = CW(16 | PIXMAP_DEFAULT_ROW_BYTES);
        icon.pixelSize = icon.cmpSize = CWC(8);
        icon.cmpCount = CWC(1);
        FIELD_ASSIGN_RAW(icon.pmTable, RM(colors));
        PP_ASSIGN(bitmap.baseAddr, (char*)STARH(mask) + 24);
        bitmap.rowBytes = CWC(2);
        bitmap.bounds = icon.bounds;
        CopyMask((BitMap*)&icon, &bitmap, PORT_BITS_FOR_COPY(thePort), &icon.bounds, &bitmap.bounds, (Rect*)bounds);
    });
    DisposCTable(colors);
}

PRIVATE int native_tab_edges(ControlHandle control, int height, int edges[128])
{
    Handle tabs = CTL_DATA(control);
    int count = MIN(127, CTL_MAX(control));
    Rect bounds = CTL_RECT(control);
    int width = RECT_WIDTH(&bounds);
    int total = 0;
    int parsed = 0;
    edges[0] = CW(bounds.left);
    if (!tabs || !STARH(tabs) || count <= 0)
        return 0;
    draw_state_t saved;
    draw_state_save(&saved);
    TextFont(0);
    TextSize(height == 17 ? 9 : 12);
    LOCK_HANDLE_EXCURSION_1(tabs, {
        const Byte* data = STARH(tabs);
        Size size = GetHandleSize(tabs);
        Size offset = 4;
        for (int tab = 1; tab <= count && size - offset >= 9 && data[offset + 2] <= size - offset - 9; ++tab) {
            int label_width = StringWidth((StringPtr)data + offset + 2);
            total = height == 17 ? tab * width / count : total + MAX(64, label_width + 24);
            edges[tab] = total;
            offset += 9 + data[offset + 2];
            ++parsed;
        }
    });
    for (int tab = 1; tab <= parsed; ++tab)
        edges[tab] = edges[0] + (total > width ? edges[tab] * width / total : edges[tab]);
    draw_state_restore(&saved);
    return parsed;
}

PRIVATE void draw_native_tabs(ControlHandle control, int height)
{
    Handle tabs = CTL_DATA(control);
    int count = CTL_MAX(control);
    if (!tabs || !STARH(tabs) || !count)
        return;
    Rect bounds = CTL_RECT(control);
    int top = CW(bounds.top);
    int edges[128];
    count = native_tab_edges(control, height, edges);
    draw_state_t saved;
    draw_state_save(&saved);
    PenNormal();
    TextFont(0);
    TextSize(height == 17 ? 9 : 12);
    static const RGBColor inactive = {CWC(0xbbbb), CWC(0xbbbb), CWC(0xbbbb)};
    if (height > 17) {
        Rect panel = bounds;
        panel.top = CW(top + height - 1);
        FrameRect(&panel);
    }
    LOCK_HANDLE_EXCURSION_1(tabs, {
        Size size = GetHandleSize(tabs);
        Size offset = 4;
        const Byte* data = STARH(tabs);
        for (int tab = 1; tab <= count && size - offset >= 9 && data[offset + 2] <= size - offset - 9; ++tab) {
            int start = edges[tab - 1];
            int end = edges[tab];
            int center = (start + end) / 2;
            int icon = (int16)(data[offset] * 256 + data[offset + 1]);
            PolyHandle outline = OpenPoly();
            if (!outline)
                break;
            MoveTo(start, top + height - 1);
            LineTo(start + 6, top + 2);
            LineTo(end - 7, top + 2);
            LineTo(end - 1, top + height - 1);
            LineTo(start, top + height - 1);
            ClosePoly();
            RGBColor background;
            GetBackColor(&background);
            RGBForeColor(tab == CTL_VALUE(control) ? &background : &inactive);
            PaintPoly(outline);
            RGBForeColor(&ROMlib_black_rgb_color);
            FramePoly(outline);
            KillPoly(outline);
            if (icon) {
                Rect icon_bounds;
                SetRect(&icon_bounds, center - 8, top + 3, center + 8, top + 15);
                draw_tab_icon(&icon_bounds, icon);
            }
            else if (data[offset + 2]) {
                StringPtr label = (StringPtr)data + offset + 2;
                MoveTo(center - StringWidth(label) / 2, top + height - 4);
                DrawString(label);
            }
            offset += 9 + data[offset + 2];
        }
    });
    draw_state_restore(&saved);
}

PRIVATE void draw_native_decoration(ControlHandle control, int kind)
{
    Rect bounds = CTL_RECT(control);
    draw_state_t saved;
    draw_state_save(&saved);
    PenNormal();
    static const RGBColor edge = {CWC(0x8888), CWC(0x8888), CWC(0x8888)};
    RGBForeColor(&edge);
    if (kind == ROMlib_separator_proc) {
        MoveTo(CW(bounds.left), CW(bounds.top));
        LineTo(CW(bounds.right) - 1, CW(bounds.top));
    }
    else {
        int left = CW(bounds.left), top = CW(bounds.top);
        bounds.top = CW(top + 6);
        FrameRect(&bounds);
        TextFont(0);
        TextSize(12);
        LOCK_HANDLE_EXCURSION_1((Handle)control, {
            if (CTL_TITLE(control)[0]) {
                Rect label;
                int right = left + 10 + StringWidth(CTL_TITLE(control));
                SetRect(&label, left + 7, top, right + 3, top + 12);
                EraseRect(&label);
                RGBForeColor(&ROMlib_black_rgb_color);
                MoveTo(left + 10, top + 11);
                DrawString(CTL_TITLE(control));
            }
        });
    }
    draw_state_restore(&saved);
}

INTEGER ROMlib_bevel_text_placement(ControlHandle control)
{
    AuxCtlHandle aux = control ? HIDDEN_NATIVE(*lookup_aux_ctl(control)) : NULL;
    return aux ? CW(ROMlib_aux_control_extra(aux)->bevel_text_placement) : 0;
}

static OSErr set_control_short_data(ControlHandle control, ResType tag, INTEGER value)
{
    INTEGER encoded = CW(value);
    return ROMlib_control_data(control, 0, tag, sizeof encoded, (Byte*)&encoded, NULL, ROMLIB_CONTROL_WRITE);
}

OSErr ROMlib_set_bevel_text_placement(ControlHandle control, INTEGER placement)
{
    if (!control || !STARH(control))
        return paramErr;
    int kind = ROMlib_native_control_kind(control);
    if (!kind)
        return set_control_short_data(control, TICK("tplc"), placement);
    if (placement < -1 || placement > 4)
        return paramErr;
    if ((kind & ~15) != ROMlib_bevel_button_proc)
        return -30581;
    AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(control));
    if (!aux)
        return paramErr;
    ROMlib_aux_control_extra(aux)->bevel_text_placement = CW(placement);
    CTL_CALL_EXCURSION(control, { InvalRect(&CTL_RECT(control)); });
    return noErr;
}

INTEGER ROMlib_bevel_text_alignment(ControlHandle control)
{
    AuxCtlHandle aux = control ? HIDDEN_NATIVE(*lookup_aux_ctl(control)) : NULL;
    return aux ? CW(ROMlib_aux_control_extra(aux)->bevel_text_alignment) : teCenter;
}

INTEGER ROMlib_bevel_text_offset(ControlHandle control)
{
    AuxCtlHandle aux = control ? HIDDEN_NATIVE(*lookup_aux_ctl(control)) : NULL;
    return aux ? CW(ROMlib_aux_control_extra(aux)->bevel_text_offset) : 0;
}

OSErr ROMlib_set_bevel_text_alignment(ControlHandle control, INTEGER alignment, INTEGER offset)
{
    if (!control || !STARH(control))
        return paramErr;
    int kind = ROMlib_native_control_kind(control);
    if (!kind) {
        OSErr status = set_control_short_data(control, TICK("tali"), alignment);
        return status ? status : set_control_short_data(control, TICK("toff"), offset);
    }
    if (alignment < teFlushLeft || alignment > teCenter)
        return paramErr;
    if ((kind & ~15) != ROMlib_bevel_button_proc)
        return -30581;
    AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(control));
    if (!aux)
        return paramErr;
    ROMlib_aux_control_extra(aux)->bevel_text_alignment = CW(alignment);
    ROMlib_aux_control_extra(aux)->bevel_text_offset = CW(offset);
    CTL_CALL_EXCURSION(control, { InvalRect(&CTL_RECT(control)); });
    return noErr;
}

INTEGER ROMlib_bevel_graphic_alignment(ControlHandle control)
{
    AuxCtlHandle aux = control ? HIDDEN_NATIVE(*lookup_aux_ctl(control)) : NULL;
    return aux ? CW(ROMlib_aux_control_extra(aux)->bevel_graphic_alignment) : 0;
}

Point ROMlib_bevel_graphic_offset(ControlHandle control)
{
    AuxCtlHandle aux = control ? HIDDEN_NATIVE(*lookup_aux_ctl(control)) : NULL;
    Point result = {0, 0};
    if (aux) {
        result.h = CW(ROMlib_aux_control_extra(aux)->bevel_graphic_offset.h);
        result.v = CW(ROMlib_aux_control_extra(aux)->bevel_graphic_offset.v);
    }
    return result;
}

OSErr ROMlib_set_bevel_graphic_alignment(ControlHandle control, INTEGER alignment, INTEGER h, INTEGER v)
{
    if (!control || !STARH(control))
        return paramErr;
    int kind = ROMlib_native_control_kind(control);
    if (!kind) {
        OSErr status = set_control_short_data(control, TICK("gali"), alignment);
        if (status)
            return status;
        Point offset = {CW(v), CW(h)};
        return ROMlib_control_data(control, 0, TICK("goff"), sizeof offset, (Byte*)&offset, NULL, ROMLIB_CONTROL_WRITE);
    }
    if (alignment < -1 || alignment > 8)
        return paramErr;
    if ((kind & ~15) != ROMlib_bevel_button_proc)
        return -30581;
    AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(control));
    if (!aux)
        return paramErr;
    ROMlib_aux_control_extra(aux)->bevel_graphic_alignment = CW(alignment);
    ROMlib_aux_control_extra(aux)->bevel_graphic_offset.h = CW(h);
    ROMlib_aux_control_extra(aux)->bevel_graphic_offset.v = CW(v);
    CTL_CALL_EXCURSION(control, { InvalRect(&CTL_RECT(control)); });
    return noErr;
}

OSErr ROMlib_set_bevel_menu_value(ControlHandle control, INTEGER value)
{
    if (!control || !STARH(control))
        return paramErr;
    int kind = ROMlib_native_control_kind(control);
    if (!kind)
        return set_control_short_data(control, TICK("mval"), value);
    if (value < 0)
        return paramErr;
    if ((kind & ~15) != ROMlib_bevel_button_proc)
        return -30581;
    AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(control));
    if (!aux)
        return paramErr;
    ROMlib_aux_control_extra(aux)->bevel_menu_value = CW(value);
    return noErr;
}

OSErr ROMlib_get_bevel_menu_value(ControlHandle control, INTEGER* value)
{
    if (!control || !STARH(control) || !value)
        return paramErr;
    int kind = ROMlib_native_control_kind(control);
    if (!kind)
        return ROMlib_control_data(control, 0, TICK("mval"), sizeof *value, (Byte*)value, NULL, ROMLIB_CONTROL_READ);
    if ((kind & ~15) != ROMlib_bevel_button_proc)
        return -30581;
    AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(control));
    if (!aux)
        return paramErr;
    *value = ROMlib_aux_control_extra(aux)->bevel_menu_value;
    return noErr;
}

/* Content descriptors contain a big-endian type and four-byte union. Handle
   content is borrowed; replacing or disposing the well never disposes it. */
PRIVATE AuxCtlHandle content_control_aux(ControlHandle control)
{
    for (WindowPeek window = MR(WindowList); window; window = WINDOW_NEXT_WINDOW(window))
        for (ControlHandle candidate = WINDOW_CONTROL_LIST(window); candidate; candidate = CTL_NEXT_CONTROL(candidate))
            if (candidate == control)
                return HIDDEN_NATIVE(*lookup_aux_ctl(control));
    return NULL;
}

OSErr ROMlib_set_control_image_content(ControlHandle control, const Byte* content, int expected_kind)
{
    AuxCtlHandle aux = content_control_aux(control);
    if (!aux || !content)
        return paramErr;
    int kind = Hx(aux, acReserved) & ~15;
    if (!kind)
        return ROMlib_control_data(control, 0, TICK("cont"), 6, (Byte*)content, NULL, ROMLIB_CONTROL_WRITE);
    if ((kind != ROMlib_image_well_proc && kind != ROMlib_bevel_button_proc) ||
        (expected_kind && kind != expected_kind))
        return -30581;
    INTEGER type;
    memcpy(&type, content, sizeof type);
    type = CW(type);
    if (type != 0 && type != 1 && type != 2 && type != 3 && type != 129 && type != 130 && type != 131)
        return -4;
    memcpy(ROMlib_aux_control_extra(aux)->image_content, content, 6);
    CTL_CALL_EXCURSION(control, { InvalRect(&CTL_RECT(control)); });
    return noErr;
}

OSErr ROMlib_get_control_image_content(ControlHandle control, Byte* content, int expected_kind)
{
    AuxCtlHandle aux = content_control_aux(control);
    if (!aux || !content)
        return paramErr;
    int kind = Hx(aux, acReserved) & ~15;
    if (!kind)
        return ROMlib_control_data(control, 0, TICK("cont"), 6, content, NULL, ROMLIB_CONTROL_READ);
    if ((kind != ROMlib_image_well_proc && kind != ROMlib_bevel_button_proc) ||
        (expected_kind && kind != expected_kind))
        return -30581;
    memcpy(content, ROMlib_aux_control_extra(aux)->image_content, 6);
    return noErr;
}

typedef struct control_image {
    INTEGER type, id;
    Handle handle;
    Rect bounds;
} control_image;

PRIVATE boolean_t load_control_image(ControlHandle control, int kind, control_image* image)
{
    Byte content[6];
    memset(image, 0, sizeof *image);
    if (ROMlib_get_control_image_content(control, content, kind))
        return FALSE;
    uint32 address;
    memcpy(&image->type, content, 2);
    memcpy(&image->id, content + 2, 2);
    memcpy(&address, content + 2, 4);
    image->type = CW(image->type);
    image->id = CW(image->id);
    address = CL(address);
    SetRect(&image->bounds, 0, 0, 32, 32);
    if (image->type >= 129 && address && address <= ROMlib_guest_memory_size - sizeof(uint32))
        image->handle = (Handle)((Byte*)ROMlib_offset + address);
    if (image->type == 2)
        image->handle = (Handle)GetCIcon(image->id);
    else if (image->type == 3)
        image->handle = (Handle)GetPicture(image->id);
    if (image->type == 1)
        return TRUE;
    if (!image->handle || !STARH(image->handle))
        return FALSE;
    if (image->type == 2 || image->type == 130)
        image->bounds = BITMAP_BOUNDS(&CICON_PMAP((CIconHandle)image->handle));
    else if (image->type == 3 || image->type == 131) {
        if (GetHandleSize(image->handle) < sizeof(Picture))
            return FALSE;
        image->bounds = HxX((PicHandle)image->handle, picFrame);
    }
    return TRUE;
}

PRIVATE void draw_control_image(const control_image* image, const Rect* bounds, ControlHandle control)
{
    IconTransformType transform = CTL_HILITE(control) == 255 ? ttDisabled : ttNone;
    AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(control));
    if (aux)
        transform |= CW(ROMlib_aux_control_extra(aux)->image_transform);
    switch (image->type) {
    case 1:
        C_PlotIconID(bounds, atNone, transform, image->id);
        break;
    case 129:
        C_PlotIconSuite(bounds, atNone, transform, image->handle);
        break;
    case 2:
    case 130:
        PlotCIcon((Rect*)bounds, (CIconHandle)image->handle);
        break;
    case 3:
    case 131:
        DrawPicture((PicHandle)image->handle, (Rect*)bounds);
        break;
    }
}

PRIVATE void release_control_image(const control_image* image)
{
    if (image->type == 2 && image->handle)
        DisposeCIcon((CIconHandle)image->handle);
}

PRIVATE void draw_native_image_well(ControlHandle control)
{
    draw_state_t saved;
    draw_state_save(&saved);
    PenNormal();
    Rect bounds = CTL_RECT(control);
    RGBForeColor(&ROMlib_white_rgb_color);
    PaintRect(&bounds);
    static const RGBColor shadow = {CWC(0x8888), CWC(0x8888), CWC(0x8888)};
    RGBForeColor(&shadow);
    FrameRect(&bounds);
    InsetRect(&bounds, 1, 1);
    RGBForeColor(&ROMlib_black_rgb_color);
    FrameRect(&bounds);
    InsetRect(&bounds, 2, 2);
    if (RECT_WIDTH(&bounds) > 0 && RECT_HEIGHT(&bounds) > 0) {
        control_image image;
        if (load_control_image(control, ROMlib_image_well_proc, &image))
            draw_control_image(&image, &bounds, control);
        release_control_image(&image);
    }
    if (CTL_VALUE(control)) {
        bounds = CTL_RECT(control);
        InsetRect(&bounds, 2, 2);
        PenSize(2, 2);
        FrameRect(&bounds);
    }
    draw_state_restore(&saved);
}

PRIVATE void draw_native_chasing_arrows(ControlHandle control, unsigned phase)
{
    Rect bounds = CTL_RECT(control);
    int radius = MIN(RECT_WIDTH(&bounds), RECT_HEIGHT(&bounds)) / 2 - 2;
    if (radius < 2)
        return;
    const int center_x = (CW(bounds.left) + CW(bounds.right)) / 2;
    const int center_y = (CW(bounds.top) + CW(bounds.bottom)) / 2;
    draw_state_t saved;
    draw_state_save(&saved);
    PenNormal();
    EraseRect(&bounds);
    RGBForeColor(CTL_HILITE(control) == INACTIVE ? &ROMlib_gray_rgb_color : &ROMlib_black_rgb_color);
    SetRect(&bounds, center_x - radius, center_y - radius, center_x + radius, center_y + radius);
    static const int sine[] = {0, 707, 1000, 707, 0, -707, -1000, -707};
    static const int cosine[] = {1000, 707, 0, -707, -1000, -707, 0, 707};
    for (unsigned arrow = 0; arrow < 2; ++arrow) {
        unsigned start = (phase + arrow * 4) & 7;
        FrameArc(&bounds, start * 45, 135);
        unsigned end = (start + 3) & 7;
        int x = center_x + sine[end] * radius / 1000;
        int y = center_y - cosine[end] * radius / 1000;
        MoveTo(x - (3 * cosine[end] + 2 * sine[end]) / 1000, y - (3 * sine[end] - 2 * cosine[end]) / 1000);
        LineTo(x, y);
        LineTo(x - (3 * cosine[end] - 2 * sine[end]) / 1000, y - (3 * sine[end] + 2 * cosine[end]) / 1000);
    }
    draw_state_restore(&saved);
}

PRIVATE void draw_native_value_control(ControlHandle control, int kind)
{
    Rect bounds = CTL_RECT(control);
    draw_state_t saved;
    draw_state_save(&saved);
    PenNormal();
    EraseRect(&bounds);
    if ((kind & ~15) == ROMlib_slider_proc) {
        int left = CW(bounds.left), right = CW(bounds.right);
        int top = CW(bounds.top), bottom = CW(bounds.bottom);
        int32 limits[4];
        ROMlib_control_read_range(control, HIDDEN_NATIVE(*lookup_aux_ctl(control)), limits);
        const int64 range = (int64)limits[control_maximum] - limits[control_minimum];
        const int64 value = (int64)limits[control_value] - limits[control_minimum];
        boolean_t vertical = bottom - top > right - left;
        int start = vertical ? top : left;
        int end = vertical ? bottom : right;
        int length = MIN(end - start, ROMlib_slider_thumb_length);
        int travel = end - start - length;
        int position = range > 0 ? value * travel / range : 0;
        if (vertical != !!(kind & 4))
            position = travel - position;
        position += start;
        Rect thumb, track;
        if (vertical) {
            int center = (left + right) / 2;
            SetRect(&track, center - 2, top, center + 3, bottom);
            SetRect(&thumb, center - 6, position, center + 6, position + length);
        }
        else {
            int center = (top + bottom) / 2;
            SetRect(&track, left, center - 2, right, center + 3);
            SetRect(&thumb, position, center - 7, position + length, center + 8);
        }
        static const RGBColor trough = {CWC(0x9999), CWC(0x9999), CWC(0x9999)};
        static const RGBColor face = {CWC(0xcccc), CWC(0xcccc), CWC(0xcccc)};
        RGBForeColor(&trough);
        PaintRoundRect(&track, 4, 4);
        RGBForeColor(&ROMlib_black_rgb_color);
        FrameRoundRect(&track, 4, 4);
        RGBForeColor(&face);
        PaintRoundRect(&thumb, 4, 4);
        RGBForeColor(&ROMlib_black_rgb_color);
        FrameRoundRect(&thumb, 4, 4);
        RGBForeColor(&trough);
        for (int stripe = -2; stripe <= 2; stripe += 2)
            if (vertical) {
                int y = position + length / 2 + stripe;
                MoveTo(CW(thumb.left) + 3, y);
                LineTo(CW(thumb.right) - 4, y);
PRIVATE void draw_native_progress_bar(ControlHandle control, AuxCtlHandle aux, boolean_t relevance)
{
    Rect bounds = CTL_RECT(control);
    if (EmptyRect(&bounds))
        return;

    static const RGBColor track = {CWC(0xdddd), CWC(0xdddd), CWC(0xdddd)};
    static const RGBColor progress = {CWC(0x5555), CWC(0x7777), CWC(0xaaaa)};
    static const RGBColor stripe = {CWC(0xaaaa), CWC(0xbbbb), CWC(0xdddd)};
    const boolean_t active = CTL_HILITE(control) != INACTIVE;
    const ROMlib_aux_control_data* extra = ROMlib_aux_control_extra(aux);
    draw_state_t saved;
    draw_state_save(&saved);
    PenNormal();
    RGBForeColor(active ? &ROMlib_black_rgb_color : &ROMlib_gray_rgb_color);
    FrameRect(&bounds);
    InsetRect(&bounds, 1, 1);
    if (!EmptyRect(&bounds)) {
        RGBForeColor(&track);
        PaintRect(&bounds);
        RGBForeColor(active && !relevance ? &progress : &ROMlib_gray_rgb_color);
        if (!relevance && extra->progress_indeterminate) {
            PaintRect(&bounds);
            RGBForeColor(active ? &stripe : &track);
            const int left = CW(bounds.left), right = CW(bounds.right);
            Rect band = bounds;
            for (int x = left + extra->progress_phase * 2 - 8; x < right; x += 8) {
                if (x + 3 > left) {
                    band.left = CW(MAX(left, x));
                    band.right = CW(MIN(right, x + 3));
                    PaintRect(&band);
                }
            }
        }
        else {
            int32 range[4];
            ROMlib_control_read_range(control, aux, range);
            const int64 span = (int64)range[control_maximum] - range[control_minimum];
            const int64 offset = (int64)range[control_value] - range[control_minimum];
            if (span > 0 && offset > 0) {
                bounds.right = CW(CW(bounds.left) + MIN(offset, span) * RECT_WIDTH(&bounds) / span);
                if (!EmptyRect(&bounds))
                    PaintRect(&bounds);
            }
        }
    }
    draw_state_restore(&saved);
}

            }
            else {
                int x = position + length / 2 + stripe;
                MoveTo(x, CW(thumb.top) + 3);
                LineTo(x, CW(thumb.bottom) - 4);
            }
    }
    else if ((kind & ~15) == ROMlib_bevel_button_proc) {
        FrameRect(&bounds);
        Rect content = bounds, text_rect;
        InsetRect(&content, 3, 3);
        ROMlib_apply_control_font_style(control);
        FontInfo font;
        GetFontInfo(&font);
        int text_height = MAX(1, CW(font.ascent) + CW(font.descent));
        int placement = ROMlib_bevel_text_placement(control);
        if (placement == -1)
            placement = 1; /* Roman system direction. */
        boolean_t has_text = CTL_TITLE(control)[0] != 0;
        int text_width = has_text ? MAX(0, StringWidth(CTL_TITLE(control))) : 0;
        int available_width = MAX(0, RECT_WIDTH(&content));
        int available_height = MAX(0, RECT_HEIGHT(&content));
        text_rect = content;
        int text_top = CW(content.top) + MAX(0, (available_height - text_height) / 2);
        text_rect.top = CW(text_top);
        text_rect.bottom = CW(MIN(CW(content.bottom), text_top + text_height));
        control_image image;
        if (load_control_image(control, ROMlib_bevel_button_proc, &image)) {
            Rect source = image.bounds;
            int width = MIN(RECT_WIDTH(&source), available_width);
            int height = MIN(RECT_HEIGHT(&source), available_height);
            int left = CW(content.left) + (available_width - width) / 2;
            int top = CW(content.top) + (available_height - height) / 2;
            if (has_text && (placement == 1 || placement == 2)) {
                int label = MIN(text_width, available_width);
                width = MIN(width, MAX(0, available_width - label - 3));
                int start = CW(content.left) + MAX(0, (available_width - width - label - 3) / 2);
                if (placement == 1) {
                    left = start;
                    text_rect.left = CW(left + width + 3);
                    text_rect.right = CW(MIN(CW(content.right), CW(text_rect.left) + label));
                }
                else {
                    text_rect.left = CW(start);
                    text_rect.right = CW(start + label);
                    left = start + label + 3;
                }
            }
            else if (has_text && (placement == 3 || placement == 4)) {
                int label = MIN(text_height, available_height);
                height = MIN(height, MAX(0, available_height - label - 3));
                int start = CW(content.top) + MAX(0, (available_height - height - label - 3) / 2);
                if (placement == 3) {
                    top = start;
                    text_rect.top = CW(top + height + 3);
                }
                else {
                    text_rect.top = CW(start);
                    top = start + label + 3;
                }
                text_rect.bottom = CW(MIN(CW(content.bottom), CW(text_rect.top) + label));
            }
            Rect area = content;
            if (has_text && placement == 1)
                area.right = CW(CW(text_rect.left) - 3);
            else if (has_text && placement == 2)
                area.left = CW(CW(text_rect.right) + 3);
            else if (has_text && placement == 3)
                area.bottom = CW(CW(text_rect.top) - 3);
            else if (has_text && placement == 4)
                area.top = CW(CW(text_rect.bottom) + 3);
            int alignment = ROMlib_bevel_graphic_alignment(control);
            Point offset = ROMlib_bevel_graphic_offset(control);
            if (alignment == -1)
                alignment = CW(TESysJust) < 0 ? 2 : 1;
            if (alignment != 0) {
                boolean_t right = alignment == 2 || alignment == 7 || alignment == 8;
                boolean_t bottom = alignment == 4 || alignment == 6 || alignment == 8;
                if (alignment == 1 || alignment == 5 || alignment == 6)
                    left = CW(area.left);
                else if (right)
                    left = CW(area.right) - width;
                if (alignment == 3 || alignment == 5 || alignment == 7)
                    top = CW(area.top);
                else if (bottom)
                    top = CW(area.bottom) - height;
                left += right ? -offset.h : offset.h;
                top += bottom ? -offset.v : offset.v;
            }
            /* Clip offsets to the reserved graphic area, including the space
               excluded by adjacent text. Keep QuickDraw coordinates in range. */
            if (width > 0 && height > 0 && RECT_WIDTH(&area) > 0 && RECT_HEIGHT(&area) > 0 && left >= INT16_MIN &&
                left + width <= INT16_MAX && top >= INT16_MIN && top + height <= INT16_MAX) {
                Rect icon;
                SetRect(&icon, left, top, left + width, top + height);
                RgnHandle saved_clip = PORT_CLIP_REGION(thePort), graphic_clip = NewRgn();
                if (graphic_clip) {
                    RectRgn(graphic_clip, &area);
                    SectRgn(graphic_clip, saved_clip, graphic_clip);
                    FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(thePort), RM(graphic_clip));
                    draw_control_image(&image, &icon, control);
                    FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(thePort), RM(saved_clip));
                    DisposeRgn(graphic_clip);
                }
            }
        }
        release_control_image(&image);
        if (has_text && RECT_WIDTH(&text_rect) > 0 && RECT_HEIGHT(&text_rect) > 0) {
            INTEGER alignment = ROMlib_bevel_text_alignment(control);
            int offset = ROMlib_bevel_text_offset(control);
            if (alignment == teFlushDefault)
                alignment = CW(TESysJust) < 0 ? teFlushRight : teFlushLeft;
            RgnHandle saved_clip = PORT_CLIP_REGION(thePort), text_clip = NewRgn();
            if (text_clip) {
                RectRgn(text_clip, &text_rect);
                SectRgn(text_clip, saved_clip, text_clip);
                FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(thePort), RM(text_clip));
                if (alignment == teFlushLeft)
                    text_rect.left = CW(MAX(INT16_MIN, MIN(INT16_MAX, CW(text_rect.left) + offset)));
                else if (alignment == teFlushRight)
                    text_rect.right = CW(MAX(INT16_MIN, MIN(INT16_MAX, CW(text_rect.right) - offset)));
                if (RECT_WIDTH(&text_rect) > 0) {
                    LOCK_HANDLE_EXCURSION_1((Handle)control, {
                        C_TextBox((Ptr)CTL_TITLE(control) + 1, CTL_TITLE(control)[0], &text_rect, alignment);
                    });
                }
                FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(thePort), RM(saved_clip));
                DisposeRgn(text_clip);
            }
        }
        if (CTL_VALUE(control) || CTL_HILITE(control) == inButton) {
            InsetRect(&bounds, 1, 1);
            InvertRect(&bounds);
        }
    }
    else {
        if ((kind & ~15) == ROMlib_edit_text_proc) {
            Rect frame_bounds = bounds;
            InsetRect(&frame_bounds, -3, -3);
            /* Editable fields retain a white interior on a themed palette. */
            RGBBackColor(&ROMlib_white_rgb_color);
            EraseRect(&frame_bounds);
            FrameRect(&frame_bounds);
        }
        AuxWinHandle owner_aux = HIDDEN_NATIVE(*lookup_aux_win(CTL_OWNER(control)));
        if (owner_aux && (Hx(owner_aux, awFlags) & ROMlib_compact_window_frame)) {
            TextFont(1);
            TextSize(9);
            TextFace(0);
        }
        ROMlib_apply_control_font_style(control);
        LOCK_HANDLE_EXCURSION_1((Handle)control, {
            C_TextBox((Ptr)CTL_TITLE(control) + 1, CTL_TITLE(control)[0], &bounds, teFlushDefault);
        });
    }
    draw_state_restore(&saved);
}

PUBLIC int32 ROMlib_send_control_message(ControlHandle control, int16 message, uint32 parameter)
{
    int32 result;
    CTL_CALL_EXCURSION(control, { result = CTLCALL(control, message, parameter); });
    return result;
}

int32 ROMlib_ctlcall(ControlHandle c, int16 i, intptr_t native_param)
{
    Handle defproc;
    int32 retval;
    ctlfuncp cp;
    int32 l = (int32)native_param;

    switch (i) {
    case calcCRgns:
    case calcCntlRgn:
    case calcThumbRgn:
    case thumbCntl:
        l = US_TO_SYN68K_CHECK0(native_param);
        break;
    default:
        break;
    }

    if (!c || !STARH(c)) {
        fprintf(stderr, "trace: ROMlib_ctlcall missing control c=%p msg=%d trap=0x%04x\n", (void*)c, i,
                (unsigned)mostrecenttrap);
        fflush(stderr);
        return 0;
    }
    if (i == drawCntl && !CTL_VIS(c))
        return 0;
    if (i == drawCntl || i == testCntl || i == 18) {
        AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(c));
        while (aux && ROMlib_aux_control_extra(aux)->parent) {
            ControlHandle parent = MR(ROMlib_aux_control_extra(aux)->parent);
            aux = HIDDEN_NATIVE(*lookup_aux_ctl(parent));
            if (aux && Hx(aux, acReserved) != ROMlib_root_control_proc && !CTL_VIS(parent))
                return 0;
        }
    }
    defproc = CTL_DEFPROC(c);
    if (!defproc) {
        AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(c));
        if (aux && ROMlib_aux_control_extra(aux)->custom_definition) {
            uint32 definition = ROMlib_aux_control_extra(aux)->custom_definition;
            return CToPascalCall(SYN68K_TO_US(definition), CTOP_cdef0, VAR(c), c, i, l);
        }
        int kind = aux ? Hx(aux, acReserved) : 0;
        if ((kind & ~15) == ROMlib_chasing_arrows_proc && aux) {
            ROMlib_aux_control_data* extra = ROMlib_aux_control_extra(aux);
            unsigned animation_step = ((uint32)TickCount() / 6) & 7;
            if (i == initCntl) {
                extra->chasing_frame = CL(animation_step);
                extra->chasing_animating = TRUE;
            }
            if (i == calcCntlRgn || i == calcCRgns)
                RectRgn((RgnHandle)native_param, &CTL_RECT(c));
            if ((i == drawCntl ||
                 (i == 18 && extra->chasing_animating && CL(extra->chasing_frame) != animation_step)) &&
                CTL_VIS(c) && WINDOW_VISIBLE(CTL_OWNER(c))) {
                if (extra->chasing_animating)
                    extra->chasing_frame = CL(animation_step);
                draw_native_chasing_arrows(c, CL(extra->chasing_frame));
            }
            return 0;
        }
        if ((kind & ~15) == ROMlib_group_box_proc || (kind & ~15) == ROMlib_separator_proc) {
            if (i == drawCntl && CTL_VIS(c) && WINDOW_VISIBLE(CTL_OWNER(c)))
                draw_native_decoration(c, kind & ~15);
            if (i == calcCntlRgn || i == calcCRgns)
                RectRgn((RgnHandle)native_param, &CTL_RECT(c));
            return 0;
        }
        if ((kind & ~15) == ROMlib_tabs_proc) {
            int height = (kind & 1) ? 17 : 22;
            if (i == initCntl)
                init_native_tabs(c);
            if (i == drawCntl && CTL_VIS(c) && WINDOW_VISIBLE(CTL_OWNER(c)))
                draw_native_tabs(c, height);
            if (i == calcCntlRgn || i == calcCRgns)
                RectRgn((RgnHandle)native_param, &CTL_RECT(c));
            if (i == testCntl && CTL_HILITE(c) != 255 && CTL_MAX(c) > 0) {
                Point where = {(INTEGER)((uint32)l >> 16), (INTEGER)l};
                Rect bounds = CTL_RECT(c);
                int edges[128];
                int count = native_tab_edges(c, height, edges);
                if (PtInRect(where, &bounds) && where.v < CW(bounds.top) + height)
                    for (int tab = 1; tab <= count; ++tab)
                        if (where.h >= edges[tab - 1] && where.h < edges[tab])
                            return tab;
            }
            return 0;
        }
        if ((kind & ~15) == ROMlib_image_well_proc) {
            if (i == initCntl && aux) {
                INTEGER type = CW(CTL_MIN(c)), id = CTL_VALUE_X(c);
                memcpy(ROMlib_aux_control_extra(aux)->image_content, &type, 2);
                memcpy(ROMlib_aux_control_extra(aux)->image_content + 2, &id, 2);
        if ((kind == ROMlib_progress_bar_proc || kind == ROMlib_relevance_bar_proc) && aux) {
            ROMlib_aux_control_data* extra = ROMlib_aux_control_extra(aux);
            if (i == initCntl)
                extra->progress_animating = TRUE;
            if (i == calcCntlRgn || i == calcCRgns)
                RectRgn((RgnHandle)native_param, &CTL_RECT(c));
            if (CTL_VIS(c) && WINDOW_VISIBLE(CTL_OWNER(c))) {
                if (i == drawCntl)
                    draw_native_progress_bar(c, aux, kind == ROMlib_relevance_bar_proc);
                if (i == 18 && extra->progress_indeterminate && extra->progress_animating) {
                    const uint32 ticks = TickCount();
                    if (ticks - extra->progress_last_tick >= 2) {
                        extra->progress_last_tick = ticks;
                        extra->progress_phase = (extra->progress_phase + 1) & 3;
                        draw_native_progress_bar(c, aux, FALSE);
                    }
                }
            }
            return 0;
        }
                CTL_MIN_X(c) = CWC(0);
                CTL_MAX_X(c) = CWC(1);
                CTL_VALUE_X(c) = CWC(0);
            }
            if (i == drawCntl && CTL_VIS(c) && WINDOW_VISIBLE(CTL_OWNER(c)))
                draw_native_image_well(c);
            if (i == calcCntlRgn || i == calcCRgns)
                RectRgn((RgnHandle)native_param, &CTL_RECT(c));
            if (i == testCntl && CTL_HILITE(c) != 255) {
                Point where = {(INTEGER)((uint32)l >> 16), (INTEGER)l};
                if (PtInRect(where, &CTL_RECT(c)))
                    return inButton;
            }
            return 0;
        }
        if ((kind & ~15) == ROMlib_bevel_button_proc || (kind & ~15) == ROMlib_slider_proc || is_text_control(c)) {
            if ((kind & ~15) == ROMlib_bevel_button_proc) {
                if (i == initCntl && aux) {
                    INTEGER type = CW(CTL_MIN(c) & 255), id = CTL_MAX_X(c);
                    ROMlib_aux_control_extra(aux)->bevel_text_alignment = CWC(teCenter);
                    memcpy(ROMlib_aux_control_extra(aux)->image_content, &type, 2);
                    memcpy(ROMlib_aux_control_extra(aux)->image_content + 2, &id, 2);
                }
            }
            if (i == calcCntlRgn || i == calcCRgns) {
                Rect region = CTL_RECT(c);
                if ((kind & ~15) == ROMlib_edit_text_proc)
                    InsetRect(&region, -3, -3);
                RectRgn((RgnHandle)native_param, &region);
            }
            if (i == drawCntl && CTL_VIS(c) && WINDOW_VISIBLE(CTL_OWNER(c)))
                draw_native_value_control(c, kind);
            if (i == testCntl && CTL_HILITE(c) != 255) {
                Point where = {(INTEGER)((uint32)l >> 16), (INTEGER)l};
                if (PtInRect(where, &CTL_RECT(c)))
                    return (kind & ~15) == ROMlib_slider_proc ? inThumb : inButton;
            }
            return 0;
        }
        if (aux && (kind & ~15) == ROMlib_user_pane_proc) {
            if (i == calcCntlRgn || i == calcCRgns)
                RectRgn((RgnHandle)native_param, &CTL_RECT(c));
            if (kind != ROMlib_root_control_proc) {
                ROMlib_pane_callbacks* callbacks = &ROMlib_aux_control_extra(aux)->callbacks;
                if (i == drawCntl && callbacks->draw) {
                    ProcPtr draw_callback = MR(callbacks->draw);
                    GrafPtr drawing_port = thePort;
                    RgnHandle saved_clip = PORT_CLIP_REGION(drawing_port);
                    RgnHandle pane_clip = NewRgn();
                    if (!pane_clip)
                        return 0;
                    RectRgn(pane_clip, &CTL_RECT(c));
                    SectRgn(pane_clip, saved_clip, pane_clip);
                    FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(drawing_port), RM(pane_clip));
                    CToPascalCall(draw_callback, CTOP_ROMlib_mytrack, c, (INTEGER)native_param);
                    FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(drawing_port), RM(saved_clip));
                    DisposeRgn(pane_clip);
                }
                if (i == testCntl && callbacks->hit) {
                    Point where = {(INTEGER)((uint32)l >> 16), (INTEGER)l};
                    return CToPascalCall(MR(callbacks->hit), CTOP_TestControl, c, where);
                }
                if (i == 16 && callbacks->focus) /* kControlMsgFocus */
                    return (INTEGER)CToPascalCall(MR(callbacks->focus), CTOP_ROMlib_mytrack | 2, c, (INTEGER)l);
                if (i == 18 && callbacks->idle) /* kControlMsgIdle */
                    CToPascalCall(MR(callbacks->idle), CTOP_ShowControl, c);
                if (i == 22 && callbacks->activate) /* kControlMsgActivate */
                    CToPascalCall(MR(callbacks->activate), 0x68, c, (BOOLEAN)l);
            }
            return 0;
        }
        fprintf(stderr, "trace: ROMlib_ctlcall missing defproc c=%p msg=%d trap=0x%04x\n", (void*)c, i,
                (unsigned)mostrecenttrap);
        fflush(stderr);
        return 0;
    }

    if (HIDDEN_RAW(*(defproc)) == NULL)
        LoadResource(defproc);
    if (!STARH(defproc)) {
        fprintf(stderr, "trace: ROMlib_ctlcall empty defproc c=%p defproc=%p msg=%d trap=0x%04x\n", (void*)c,
                (void*)defproc, i, (unsigned)mostrecenttrap);
        fflush(stderr);
        return 0;
    }

    cp = (ctlfuncp)STARH(defproc);

    if (cp == P_cdef0)
        retval = C_cdef0(VAR(c), c, i, l);
    else if (cp == P_cdef16)
        retval = C_cdef16(VAR(c), c, i, l);
    else if (cp == P_cdef1008)
        retval = C_cdef1008(VAR(c), c, i, l);
    else {
        ROMlib_hook(ctl_cdefnumber);
        LOCK_HANDLE_EXCURSION_1(defproc,
                                { retval = CToPascalCall((ctlfuncp)STARH(defproc), CTOP_cdef0, VAR(c), c, i, l); });
    }

    return retval;
}
