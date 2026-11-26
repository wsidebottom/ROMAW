/*+===================================================================
  File:      layers.c

  Summary:   Layer child lists use the classic LayerRecord ABI. Drawing traverses the tree without changing guest
             nextWindow links or the current layer's list.

  Classes:   None.

  Functions: ROMlib_desktop_window_at, ROMlib_dispose_float_layer, ROMlib_get_window_state,
             ROMlib_layer_application, ROMlib_layer_children,
             ROMlib_layer_current, ROMlib_layer_find, ROMlib_layer_front, ROMlib_layer_p, ROMlib_layer_parent,
             ROMlib_layer_root, ROMlib_layer_set, ROMlib_layer_swap, ROMlib_layers_reset,
             ROMlib_new_desktop_layer_owner, ROMlib_new_float_layer, ROMlib_set_window_state, ROMlib_window_first_all,
             ROMlib_window_next_all

  Origin:    Existing repository source in executor/src/windows; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Layer child lists use the classic LayerRecord ABI. Drawing traverses the
   tree without changing guest nextWindow links or the current layer's list. */
#include "rsys/common.h"
#include "rsys/everything.h"
#include "rsys/wind.h"
#include "rsys/layers.h"
#include "rsys/cquick.h"

typedef struct PACKED layer_record {
    GrafPort port;
    INTEGER kind;
    BOOLEAN visible, reserved1, never_active, reserved2;
    HIDDEN_RgnHandle structure, content, update;
    uint32 reserved3, parent, auxiliary_windows;
    INTEGER reserved4;
    uint32 auxiliary_controls, next, children, refcon;
} layer_record;

_Static_assert(sizeof(layer_record) == sizeof(WindowRecord), "LayerRecord ABI");
static layer_record *root_layer, *application_layer, *floating_layer, *current_layer;
static BOOLEAN desktop_layer_owned;

static void sync_current(void)
{
    if (current_layer) {
        current_layer->children = (uint32)WindowList;
        current_layer->auxiliary_windows = (uint32)AuxWinHead;
        current_layer->auxiliary_controls = (uint32)AuxCtlHead;
    }
}

static layer_record* new_layer(int never_active)
{
    GrafPtr saved_port = thePort;
    layer_record* p = (layer_record*)NewPtrSysClear(sizeof *p);
    if (!p)
        return NULL;
    C_OpenPort(&p->port);
    p->visible = TRUE;
    p->never_active = never_active;
    HIDDEN_RAW_SET(p->structure, RM(NewRgn()));
    HIDDEN_RAW_SET(p->content, RM(NewRgn()));
    HIDDEN_RAW_SET(p->update, RM(NewRgn()));
    if (!HIDDEN_RAW(p->structure) || !HIDDEN_RAW(p->content) || !HIDDEN_RAW(p->update)) {
        if (HIDDEN_RAW(p->structure))
            DisposeRgn(MR(HIDDEN_RAW(p->structure)));
        if (HIDDEN_RAW(p->content))
            DisposeRgn(MR(HIDDEN_RAW(p->content)));
        if (HIDDEN_RAW(p->update))
            DisposeRgn(MR(HIDDEN_RAW(p->update)));
        C_ClosePort(&p->port);
        C_SetPort(saved_port);
        DisposPtr((Ptr)p);
        return NULL;
    }
    if (GrayRgn) {
        CopyRgn(MR(GrayRgn), MR(HIDDEN_RAW(p->structure)));
        CopyRgn(MR(GrayRgn), MR(HIDDEN_RAW(p->content)));
    }
    C_SetPort(saved_port);
    return p;
}

static void free_layer(layer_record* p)
{
    DisposeRgn(MR(HIDDEN_RAW(p->structure)));
    DisposeRgn(MR(HIDDEN_RAW(p->content)));
    DisposeRgn(MR(HIDDEN_RAW(p->update)));
    C_ClosePort(&p->port);
    DisposPtr((Ptr)p);
}

static int ensure_layers(void)
{
    WindowPeek saved = WindowList;
    if (root_layer)
        return 1;
    root_layer = new_layer(0);
    application_layer = new_layer(0);
    if (!root_layer || !application_layer) {
        if (root_layer)
            free_layer(root_layer);
        if (application_layer)
            free_layer(application_layer);
        root_layer = application_layer = NULL;
        return 0;
    }
    application_layer->parent = CL(US_TO_SYN68K(root_layer));
    application_layer->children = (uint32)saved;
    application_layer->auxiliary_windows = (uint32)AuxWinHead;
    application_layer->auxiliary_controls = (uint32)AuxCtlHead;
    root_layer->children = CL(US_TO_SYN68K(application_layer));
    current_layer = application_layer;
    return 1;
}

PUBLIC BOOLEAN ROMlib_layer_p(WindowPtr p)
{
    return p && ((void*)p == root_layer || (void*)p == application_layer || (void*)p == floating_layer);
}

PUBLIC void* ROMlib_layer_current(void)
{
    return ensure_layers() ? current_layer : NULL;
}

PUBLIC void* ROMlib_layer_application(void)
{
    return ensure_layers() ? application_layer : NULL;
}

PUBLIC void* ROMlib_layer_root(void)
{
    return ensure_layers() ? root_layer : NULL;
}

PUBLIC void* ROMlib_layer_swap(void* layer)
{
    layer_record* old;
    if (!ensure_layers() || !ROMlib_layer_p((WindowPtr)layer))
        return NULL;
    sync_current();
    old = current_layer;
    current_layer = layer;
    HIDDEN_RAW_SET(WindowList_H, (WindowPeek)current_layer->children);
    HIDDEN_RAW_SET(AuxWinHead_H, (AuxWinHandle)current_layer->auxiliary_windows);
    HIDDEN_RAW_SET(AuxCtlHead_H, (AuxCtlHandle)current_layer->auxiliary_controls);
    return old;
}

PUBLIC void ROMlib_layer_set(void* p)
{
    ROMlib_layer_swap(p);
}

PUBLIC WindowPtr ROMlib_layer_children(void* layer)
{
    sync_current();
    return ROMlib_layer_p((WindowPtr)layer) && ((layer_record*)layer)->children
               ? (WindowPtr)SYN68K_TO_US(CL(((layer_record*)layer)->children))
               : NULL;
}

PUBLIC void* ROMlib_layer_parent(WindowPtr window)
{
    layer_record* layer;
    WindowPeek w;
    sync_current();
    if (ROMlib_layer_p(window))
        return window == (WindowPtr)root_layer ? NULL : root_layer;
    if (!root_layer)
        return NULL;
    for (layer = (layer_record*)SYN68K_TO_US(CL(root_layer->children)); layer;
         layer = layer->next ? (layer_record*)SYN68K_TO_US(CL(layer->next)) : NULL)
        for (w = layer->children ? SYN68K_TO_US(CL(layer->children)) : NULL; w; w = WINDOW_NEXT_WINDOW(w))
            if ((WindowPtr)w == window)
                return layer;
    return NULL;
}

PUBLIC WindowPtr ROMlib_layer_front(void* layer)
{
    WindowPeek w = (WindowPeek)ROMlib_layer_children(layer);
    for (; w; w = WINDOW_NEXT_WINDOW(w)) {
        if (ROMlib_layer_p((WindowPtr)w)) {
            WindowPtr child = ROMlib_layer_front(w);
            if (child)
                return child;
        }
        else if (WINDOW_VISIBLE_X(w))
            return (WindowPtr)w;
    }
    return NULL;
}

PUBLIC void* ROMlib_layer_find(Point point)
{
    if (!ensure_layers())
        return NULL;
    for (WindowPeek window = ROMlib_window_first_all(); window; window = ROMlib_window_next_all(window))
        if (WINDOW_VISIBLE_X(window) && PtInRgn(point, WINDOW_STRUCT_REGION(window)))
            return ROMlib_layer_parent((WindowPtr)window);
    return GrayRgn && PtInRgn(point, MR(GrayRgn)) ? application_layer : NULL;
}

PUBLIC OSErr ROMlib_new_desktop_layer_owner(HIDDEN_Ptr* output)
{
    if (!output)
        return paramErr;
    HIDDEN_RAW_SET(*(output), NULL);
    if (!ensure_layers())
        return memFullErr;
    /* ROMAW runs one application at a time. When that application is Finder,
     its ordinary layer owns both desktop and folder windows. */
    desktop_layer_owned = TRUE;
    HIDDEN_RAW_SET(*(output), RM((Ptr)application_layer));
    return noErr;
}

PUBLIC WindowPtr ROMlib_desktop_window_at(Point point)
{
    if (!desktop_layer_owned || !application_layer || !application_layer->visible ||
        !PtInRgn(point, MR(HIDDEN_RAW(application_layer->content))))
        return NULL;
    /* Finder's desktop is a real child window with no structure region.
       The owner layer carries no window reference data for its icon model. */
    for (WindowPeek window = (WindowPeek)ROMlib_layer_children(application_layer); window;
         window = WINDOW_NEXT_WINDOW(window))
        if (WINDOW_VISIBLE_X(window) && EmptyRgn(WINDOW_STRUCT_REGION(window)) &&
            PtInRgn(point, WINDOW_CONT_REGION(window)))
            return (WindowPtr)window;
    return NULL;
}

PUBLIC OSErr ROMlib_new_float_layer(HIDDEN_Ptr* output)
{
    if (!output)
        return paramErr;
    HIDDEN_RAW_SET(*(output), NULL);
    if (!ensure_layers())
        return memFullErr;
    if (!floating_layer) {
        floating_layer = new_layer(1);
        if (!floating_layer)
            return memFullErr;
        floating_layer->parent = CL(US_TO_SYN68K(root_layer));
        floating_layer->next = root_layer->children;
        root_layer->children = CL(US_TO_SYN68K(floating_layer));
    }
    HIDDEN_RAW_SET(*(output), RM((Ptr)floating_layer));
    return noErr;
}

PUBLIC OSErr ROMlib_dispose_float_layer(HIDDEN_Ptr* output)
{
    if (!output || !floating_layer || HIDDEN_RAW(*(output)) != RM((Ptr)floating_layer))
        return paramErr;
    sync_current();
    if (floating_layer->children)
        return paramErr;
    if (current_layer == floating_layer)
        ROMlib_layer_set(application_layer);
    root_layer->children = floating_layer->next;
    free_layer(floating_layer);
    floating_layer = NULL;
    HIDDEN_RAW_SET(*(output), NULL);
    return noErr;
}

PUBLIC WindowPeek ROMlib_window_first_all(void)
{
    layer_record* layer;
    if (!root_layer)
        return MR(WindowList);
    sync_current();
    for (layer = (layer_record*)SYN68K_TO_US(CL(root_layer->children)); layer;
         layer = layer->next ? (layer_record*)SYN68K_TO_US(CL(layer->next)) : NULL)
        if (layer->visible && layer->children)
            return SYN68K_TO_US(CL(layer->children));
    return NULL;
}

PUBLIC WindowPeek ROMlib_window_next_all(WindowPeek window)
{
    layer_record* layer;
    WindowPeek next;
    if (!window)
        return NULL;
    next = WINDOW_NEXT_WINDOW(window);
    if (next || !root_layer)
        return next;
    layer = ROMlib_layer_parent((WindowPtr)window);
    while (layer && layer->next) {
        layer = (layer_record*)SYN68K_TO_US(CL(layer->next));
        if (layer->visible && layer->children)
            return SYN68K_TO_US(CL(layer->children));
    }
    return NULL;
}

PUBLIC void ROMlib_layers_reset(void)
{
    desktop_layer_owned = FALSE;
    if (!root_layer)
        return;
    sync_current();
    /* Floating windows are disposed before this call. Application storage remains
     owned by the ordinary application shutdown path. */
    HIDDEN_RAW_SET(WindowList_H, (WindowPeek)application_layer->children);
    HIDDEN_RAW_SET(AuxWinHead_H, (AuxWinHandle)application_layer->auxiliary_windows);
    HIDDEN_RAW_SET(AuxCtlHead_H, (AuxCtlHandle)application_layer->auxiliary_controls);
    if (floating_layer)
        free_layer(floating_layer);
    free_layer(application_layer);
    free_layer(root_layer);
    current_layer = root_layer = application_layer = floating_layer = NULL;
}

/* Classic state version 1: big-endian version/Rect, Boolean and even padding.
   Keep a local copy across Toolbox calls that can move unlocked handles. */
typedef struct PACKED {
    INTEGER version;
    Rect bounds;
    BOOLEAN zoomed, padding;
} window_state;

_Static_assert(sizeof(window_state) == 12, "WindowStateInfo ABI");

OSErr ROMlib_get_window_state(WindowPtr window, Handle state, LONGINT* size)
{
    window_state record = {0};
    if (!window || !state)
        return paramErr;
    record.version = CWC(1);
    record.zoomed = WINDOW_SPARE_FLAG(window) && WINDOW_DATA(window) &&
                    GetHandleSize(WINDOW_DATA(window)) >= sizeof(WStateData) &&
                    ROMlib_window_zoomed((WindowPeek)window);
    if (record.zoomed)
        record.bounds = ((WStateData*)STARH(WINDOW_DATA(window)))->userState;
    else {
        record.bounds = PORT_RECT(window);
        OffsetRect(&record.bounds, -CW(PORT_BOUNDS(window).left), -CW(PORT_BOUNDS(window).top));
    }
    SetHandleSize(state, sizeof record);
    OSErr error = MemError();
    if (error)
        return error;
    memcpy(STARH(state), &record, sizeof record);
    if (size)
        *size = CL(sizeof record);
    return noErr;
}

OSErr ROMlib_set_window_state(WindowPtr window, Handle state)
{
    window_state record;
    Rect target, test;
    if (!window || !state || GetHandleSize(state) < sizeof record)
        return paramErr;
    memcpy(&record, STARH(state), sizeof record);
    if (CW(record.version) != 1)
        return 703; /* badVersNo */
    if (record.zoomed &&
        (!WINDOW_SPARE_FLAG(window) || !WINDOW_DATA(window) || GetHandleSize(WINDOW_DATA(window)) < sizeof(WStateData)))
        return 702; /* badWindowType */
    target = record.bounds;
    int width = CW(target.right) - CW(target.left);
    int height = CW(target.bottom) - CW(target.top);
    if (width <= 0 || width > 32767 || height <= 0 || height > 32767)
        return 704; /* badWindPos */
    /* Keep the saved content, or a draggable document's title strip, inside
     the current desktop. Standard draggable variants have a 20-pixel strip. */
    test = target;
    INTEGER variant = GetWVariant(window);
    BOOLEAN draggable = variant != 1 && variant != 2 && variant != 3;
    if (draggable) {
        test.top = CW(CW(test.top) - 20);
        test.bottom = CW(CW(test.top) + 20);
    }
    RgnHandle check = NewRgn();
    if (!check)
        return memFullErr;
    RectRgn(check, &test);
    DiffRgn(check, MR(GrayRgn), check);
    BOOLEAN fits = EmptyRgn(check);
    DisposeRgn(check);
    if (!fits)
        return 704;
    if (record.zoomed) {
        WStateData* zoom = (WStateData*)STARH(WINDOW_DATA(window));
        zoom->userState = target;
        target = zoom->stdState;
    }
    MoveWindow(window, CW(target.left), CW(target.top), FALSE);
    SizeWindow(window, CW(target.right) - CW(target.left), CW(target.bottom) - CW(target.top), TRUE);
    return noErr;
}
