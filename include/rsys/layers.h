/*+===================================================================
  File:      layers.h

  Summary:   ifndef RSYS_LAYERS_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef RSYS_LAYERS_H
#define RSYS_LAYERS_H
#include "WindowMgr.h"
extern void* ROMlib_layer_current(void);
extern void* ROMlib_layer_application(void);
extern void* ROMlib_layer_root(void);
extern void* ROMlib_layer_find(Point);
extern void* ROMlib_layer_swap(void*);
extern void ROMlib_layer_set(void*);
extern void* ROMlib_layer_parent(WindowPtr);
extern WindowPtr ROMlib_layer_children(void*);
extern WindowPtr ROMlib_layer_front(void*);
extern BOOLEAN ROMlib_layer_p(WindowPtr);
extern OSErr ROMlib_new_desktop_layer_owner(HIDDEN_Ptr*);
extern OSErr ROMlib_new_float_layer(HIDDEN_Ptr*);
extern OSErr ROMlib_dispose_float_layer(HIDDEN_Ptr*);
extern WindowPeek ROMlib_window_first_all(void);
extern WindowPeek ROMlib_window_next_all(WindowPeek);
extern OSErr ROMlib_get_window_state(WindowPtr, Handle, LONGINT*);
extern OSErr ROMlib_set_window_state(WindowPtr, Handle);
extern void ROMlib_layers_reset(void);
#endif
