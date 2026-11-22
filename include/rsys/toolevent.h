/*+===================================================================
  File:      toolevent.h

  Summary:   Declares toolevent support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_TOOLEVENT_H_)
#define _TOOLEVENT_H_

/*
 * Copyright 1998 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: toolevent.h 63 2004-12-24 18:19:43Z ctm $
 */

typedef struct PACKED {
    INTEGER version;
    INTEGER tableno[256];
    INTEGER ntables;
    uint8 table[1][128]; /* who knows how many */
} keymap;

extern void dofloppymount(void);
extern BOOLEAN ROMlib_beepedonce;
extern int ROMlib_right_button_modifier;
extern void ROMlib_send_quit(void);
extern BOOLEAN ROMlib_post_quit_event(void);

enum { ROMlib_power_overall_activity, ROMlib_power_user_activity, ROMlib_power_activity_count = 5 };

extern void ROMlib_power_note_activity(unsigned kind, uint32 ticks);
extern uint32 ROMlib_power_last_activity(unsigned kind);

#endif /* !_TOOLEVENT_H_ */
