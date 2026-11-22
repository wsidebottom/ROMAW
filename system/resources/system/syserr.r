/*+===================================================================
  File:      syserr.r

  Summary:   Defines the built-in system-error alert fallback table.

  Classes:   None.

  Functions: Resource declarations only.

  Origin:    User-edited DSAT.rsrc extraction; original fallback notices retained below.

  Copyright and Legal notices: Original notice retained below; see COPYING.
===================================================================+*/

/* Copyright 1988 - 1999 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Procedure values are signed native handler identities, not executable bytes.
   Text includes its terminating NUL and original padding explicitly. */
type 'DSAT' {
    integer = $$CountOf(Entries);
    array Entries {
        integer; unsigned integer;
        switch {
            case Alert:
                integer; integer; integer; integer; integer;
            case AlertExtended:
                integer; integer; integer; integer; integer; integer;
            case Text:
                point; string;
            case Icon:
                rect; hex string[128];
            case Buttons:
                integer = $$CountOf(Buttons);
                array Buttons { integer; rect; integer; };
            case ButtonText:
                string;
            case Procedure:
                longint;
            case Rectangle:
                rect;
        };
    };
};

resource 'DSAT' (0, sysheap, locked) {
    {
        32767, 10, Alert { -20107, 0, -16397, -21103, -22001 };
        -10, 10, Alert { 0, 0, -20228, 0, 0 };
        -11, 10, Alert { 0, 0, -20229, 0, 0 };
        -13, 10, Alert { 0, 0, -20227, 0, 0 };
        -20107, 36, Text { { 94, 114 }, "Sorry, a system error occurred.\0x00" };
        -16397, 136, Icon { { 81, 60, 113, 92 }, $$icon("syserr/bomb.icns") };
        -20228, 136, Icon { { 315, 236, 347, 268 }, $$icon("syserr/DSAT-0-icon--20228.icns") };
        -20229, 136, Icon { { 315, 236, 347, 268 }, $$icon("syserr/DSAT-0-icon--20229.icns") };
        -20227, 136, Icon { { 315, 236, 347, 268 }, $$icon("syserr/DSAT-0-icon--20227.icns") };
        -22001, 14, Buttons {
            {
                -23000, { 154, 404, 174, 463 }, -21000;
            }
        };
        -23000, 6, ButtonText { "Quit \0x00" };
        -21000, 4, Procedure { -21000 };
        -21103, 4, Procedure { -21103 };
    }
};

resource 'DSAT' (2, sysheap, locked) {
    {
        32767, 10, Alert { -20102, 0, -16397, -21103, -22103 };
        20003, 12, AlertExtended { 0, 0, -16401, -21109, 0, -24301 };
        20004, 12, AlertExtended { 0, 0, -16398, -21110, 0, -24301 };
        30, 12, AlertExtended { 0, 0, -16398, -21101, 0, -24301 };
        28, 10, Alert { -20102, 0, -16397, -21106, -22103 };
        25, 10, Alert { -20108, 0, -16397, -21108, -22105 };
        20002, 10, Alert { -20107, 0, -16400, -21107, -22107 };
        101, 10, Alert { -20001, 0, -16397, 0, -22001 };
        1010, 10, Alert { -20004, 0, -16397, -21103, -22103 };
        1011, 10, Alert { -20005, 0, -16397, -21103, -22103 };
        -20102, 36, Text { { 94, 114 }, "Sorry, a system error occurred.\0x00" };
        -20001, 50, Text { { 94, 114 }, "Sorry, a system error occurred.//  RAM parity\0x00" };
        -20107, 36, Text { { 108, 114 }, "Unsaved changes will be lost./\0x00\0x00" };
        -20108, 48, Text { { 94, 114 }, "Not enough memory is available while using/\0x00" };
        -20004, 42, Text { { 94, 114 }, "Code Fragment could not be prepared.\0x00\0x00" };
        -20005, 34, Text { { 94, 114 }, "Internal Mixed Mode failure.\0x00\0x00" };
        -16397, 136, Icon { { 81, 60, 113, 92 }, $$icon("syserr/bomb.icns") };
        -16398, 136, Icon { { 12, 20, 44, 52 }, $$icon("syserr/insert.icns") };
        -16401, 136, Icon { { 12, 20, 44, 52 }, $$icon("syserr/remove.icns") };
        -16400, 136, Icon { { 81, 60, 113, 92 }, $$icon("syserr/hand.icns") };
        -22001, 14, Buttons {
            {
                -23000, { 154, 404, 174, 463 }, -21000;
            }
        };
        -22000, 14, Buttons {
            {
                -23000, { 154, 404, 174, 463 }, -21000;
            }
        };
        -22103, 14, Buttons {
            {
                -23000, { 154, 404, 174, 463 }, -21000;
            }
        };
        -22102, 26, Buttons {
            {
                -23000, { 154, 404, 174, 463 }, -21000;
                -23101, { 154, 325, 174, 392 }, -21104;
            }
        };
        -22105, 26, Buttons {
            {
                -23102, { 154, 384, 174, 463 }, -21105;
                -23000, { 154, 313, 174, 372 }, -21000;
            }
        };
        -22104, 26, Buttons {
            {
                -23102, { 154, 384, 174, 463 }, -21105;
                -23000, { 154, 313, 174, 372 }, -21000;
            }
        };
        -22107, 26, Buttons {
            {
                -23104, { 154, 411, 174, 463 }, 0;
                -23103, { 154, 324, 174, 399 }, -21102;
            }
        };
        -22106, 26, Buttons {
            {
                -23104, { 154, 411, 174, 463 }, 0;
                -23103, { 154, 324, 174, 399 }, -21102;
            }
        };
        -23000, 10, ButtonText { "Restart \0x00\0x00" };
        -23101, 10, ButtonText { "Continue\0x00\0x00" };
        -23102, 12, ButtonText { "Shut Down \0x00\0x00" };
        -23104, 8, ButtonText { "Cancel\0x00\0x00" };
        -23103, 12, ButtonText { "Force Quit\0x00\0x00" };
        -21000, 4, Procedure { -21000 };
        -21104, 4, Procedure { -21104 };
        -21106, 4, Procedure { -21106 };
        -21103, 4, Procedure { -21103 };
        -21111, 4, Procedure { -21111 };
        -21101, 4, Procedure { -21101 };
        -21110, 4, Procedure { -21110 };
        -21109, 4, Procedure { -21109 };
        -21108, 4, Procedure { -21108 };
        -21102, 4, Procedure { -21102 };
        -21105, 4, Procedure { -21105 };
        -21107, 4, Procedure { -21107 };
        -24301, 8, Rectangle { { 75, 106, 165, 406 } };
    }
};
