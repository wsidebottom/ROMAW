/*+===================================================================
  File:      interface.r

  Summary:   Classic System interface resources.

  Classes:   None.

  Functions: No explicitly exported functions.

  Origin:    Existing repository source in executor/resources/system; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "Types.r"
#include "resource_types.r"

/* Classic System interface resources. */

resource 'ALRT' (-4061, sysheap, preload) {
    { 114, 122, 206, 394 },
    -4061, /* DITL */
    {
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
    },
    centerMainScreen
};

resource 'ALRT' (-4060) {
    { 118, 114, 214, 384 },
    -4060, /* DITL */
    {
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
    },
    centerMainScreen
};

resource 'ALRT' (-3997, purgeable) {
    { 0, 0, 80, 200 },
    -3997, /* DITL */
    {
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
    },
    centerMainScreen
};

resource 'ALRT' (-3996, purgeable) {
    { 0, 0, 80, 200 },
    -3996, /* DITL */
    {
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
    },
    centerMainScreen
};

resource 'ALRT' (-3995, purgeable) {
    { 0, 0, 80, 200 },
    -3995, /* DITL */
    {
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
    },
    centerMainScreen
};

resource 'ALRT' (-3994, purgeable) {
    { 0, 0, 80, 200 },
    -3994, /* DITL */
    {
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
    },
    centerMainScreen
};

resource 'ALRT' (-3000) {
    { 40, 40, 140, 380 },
    -3000, /* DITL */
    {
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
    },
    centerMainScreen
};

/* Extended items and nonzero alignment bytes are explicit typed fields. */
resource 'DITL' (-6043, purgeable) {
    {
        { 161, 252, 181, 332 }, Button { enabled, "Save" };
        { 130, 252, 150, 332 }, Button { enabled, "Cancel" };
        { 0, 0, 0, 0 }, HelpItem { disabled, 1, -6043 };
        { 8, 235, 24, 337 }, UserItem { enabled };
        { 32, 252, 52, 332 }, Button { enabled, "Eject" };
        { 60, 252, 80, 332 }, Button { enabled, "Desktop" };
        { 29, 12, 127, 230 }, UserItem { enabled };
        { 6, 12, 25, 230 }, UserItem { enabled };
        { 119, 250, 120, 334 }, Picture { disabled, 11 };
        { 157, 15, 173, 227 }, EditText { enabled, "" };
        { 136, 15, 152, 227 }, StaticText { disabled, "Save as:" };
        { 88, 252, 108, 332 }, UserItem { disabled };
    }
};

/* Extended items and nonzero alignment bytes are explicit typed fields. */
resource 'DITL' (-6042, purgeable) {
    {
        { 135, 252, 155, 332 }, Button { enabled, "Open" };
        { 104, 252, 124, 332 }, Button { enabled, "Cancel" };
        { 0, 0, 0, 0 }, HelpItem { disabled, 1, -6042 };
        { 8, 235, 24, 337 }, UserItem { enabled };
        { 32, 252, 52, 332 }, Button { enabled, "Eject" };
        { 60, 252, 80, 332 }, Button { enabled, "Desktop" };
        { 29, 12, 159, 230 }, UserItem { enabled };
        { 6, 12, 25, 230 }, UserItem { enabled };
        { 91, 251, 92, 333 }, Picture { disabled, 11 };
    }
};

resource 'DITL' (-6044, purgeable) {
    {
        { 73, 194, 94, 266 }, Button { enabled, "Create" };
        { 73, 108, 94, 180 }, Button { enabled, "Cancel" };
        { 39, 18, 56, 266 }, EditText { enabled, "untitled folder" };
        { 15, 18, 33, 266 }, StaticText { disabled, "Folder name:" };
    }
};

resource 'DITL' (-4065) {
    {
        /* Item 1 */
        { 241, 278, 261, 336 },
        Button { enabled, "OK" };

        /* Item 2 */
        { 241, 355, 261, 413 },
        Button { enabled, "Cancel" };

        /* Item 3 */
        { 56, 223, 72, 389 },
        EditText { enabled, "your name" };

        /* Item 4 */
        { 77, 223, 93, 389 },
        EditText { enabled, "your organization" };

        /* Item 5 */
        { 98, 223, 114, 389 },
        EditText { enabled, "" };

        /* Item 6 */
        { 119, 223, 135, 389 },
        EditText { enabled, "" };

        /* Item 7 */
        { 223, 40, 277, 221 },
        StaticText { disabled, "Number of CPUs: 1\0x0DLast free update: 1.*\0x0DExpires: 11/1995" };

        /* Item 8 */
        { 56, 172, 72, 211 },
        StaticText { disabled, "Name" };

        /* Item 9 */
        { 77, 125, 94, 211 },
        StaticText { disabled, "Organization" };

        /* Item 10 */
        { 98, 116, 114, 211 },
        StaticText { disabled, "Serial Number" };

        /* Item 11 */
        { 119, 83, 135, 211 },
        StaticText { disabled, "Authentication Key" };

        /* Item 12 */
        { 10, 27, 27, 446 },
        StaticText { disabled, "Copyright \0xA9 1986-1996 Abacus Research and Development, Inc." };

        /* Item 13 */
        { 148, 27, 212, 445 },
        StaticText { disabled, "ROMAW runs classic Macintosh applications on Windows." };

        /* Item 14 */
        { 30, 70, 47, 403 },
        StaticText { disabled, "@ROMAW_VERSION@" };

    }
};

resource 'DITL' (-4064) {
    {
        /* Item 1 */
        { 271, 12, 291, 70 },
        Button { enabled, "Next" };

        /* Item 2 */
        { 8, 10, 24, 485 },
        StaticText { disabled, "^0" };

        /* Item 3 */
        { 32, 10, 257, 484 },
        StaticText { disabled, "^1" };

        /* Item 4 */
        { 271, 425, 291, 483 },
        Button { enabled, "OK" };

        /* Item 5 */
        { 271, 81, 291, 146 },
        Button { enabled, "Previous" };

    }
};

/* Literal data preserves nonzero alignment bytes not representable by the standard DITL layout. */
resource 'DITL' (-4063, sysheap, preload) {
    {
        { 225, 190, 245, 250 }, Button { enabled, "OK" };
        { 225, 105, 245, 165 }, Button { enabled, "Cancel" };
        { 225, 20, 245, 80 }, Button { enabled, "Save" };
        { 76, 22, 97, 128 }, RadioButton { enabled, "Normal" };
        { 0, 0, 1, 1 }, UserItem { disabled };
        { 96, 22, 117, 142 }, RadioButton { enabled, "Animation", 3 /* preserved alignment byte */ };
        { 70, 171, 91, 231 }, RadioButton { enabled, "Off", 7 /* preserved alignment byte */ };
        { 95, 171, 116, 261 }, RadioButton { enabled, "Pretend", 2 /* preserved alignment byte */ };
        { 120, 171, 141, 270 }, RadioButton { disabled, "On (silent)" };
        { 10006, 274, 10027, 365 }, CheckBox { enabled, "No Clock" };
        { 42, 1500, 63, 27152 }, CheckBox { disabled, "Pass PostScript", 15 /* preserved alignment byte */ };
        { 52, 272, 73, 424 }, CheckBox { disabled, "Newline Mapping", 17 /* preserved alignment byte */ };
        { 72, 272, 93, 421 }, CheckBox { enabled, "Flush Cache Often", 18 /* preserved alignment byte */ };
        { 92, 272, 113, 437 }, CheckBox { enabled, "Direct Disk Access" };
        { 112, 272, 133, 433 }, CheckBox { enabled, "No 32-bit Warnings" };
        { 132, 272, 153, 427 }, CheckBox { disabled, "Font Substitution", 16 /* preserved alignment byte */ };
        { 185, 1275, 207, 1424 }, CheckBox { enabled, "Cache Heuristics" };
        { 147, 81, 163, 126 }, EditText { enabled, "" };
        { 146, 22, 168, 78 }, StaticText { enabled, "Refresh", 14 /* preserved alignment byte */ };
        { 51, 22, 72, 129 }, StaticText { enabled, "Screen Updates" };
        { 50, 171, 71, 232 }, StaticText { enabled, "Sound", 16 /* preserved alignment byte */ };
        { 154, 146, 170, 262 }, StaticText { disabled, "Number of Colors" };
        { 169, 155, 187, 196 }, RadioButton { enabled, "2", 1 /* preserved alignment byte */ };
        { 169, 204, 187, 241 }, RadioButton { enabled, "4", 2 /* preserved alignment byte */ };
        { 186, 155, 204, 199 }, RadioButton { enabled, "16" };
        { 186, 204, 204, 255 }, RadioButton { enabled, "256" };
        { 181, 77, 197, 138 }, EditText { enabled, "" };
        { 258, 23, 292, 422 }, EditText { enabled, "" };
        { 181, 22, 197, 73 }, StaticText { disabled, "System" };
        { 162, 10148, 180, 10254 }, RadioButton { enabled, "thousands", 8 /* preserved alignment byte */ };
        { 179, 10148, 197, 10254 }, RadioButton { enabled, "millions" };
        { 152, 272, 173, 378 }, CheckBox { enabled, "Pretend Help" };
        { 172, 272, 193, 397 }, CheckBox { enabled, "Pretend Edition", 14 /* preserved alignment byte */ };
        { 192, 272, 213, 394 }, CheckBox { enabled, "Pretend Script" };
        { 212, 272, 233, 386 }, CheckBox { enabled, "Pretend Alias", 9 /* preserved alignment byte */ };
        { 196, 10148, 214, 10254 }, CheckBox { enabled, "grayscale" };
        { 17, 21, 34, 424 }, StaticText { disabled, "Preferences for ^0" };
    }
};

/* Literal data preserves nonzero alignment bytes not representable by the standard DITL layout. */
resource 'DITL' (-4062, sysheap, preload) {
    {
        { 138, 280, 158, 340 }, Button { enabled, "OK" };
        { 9, 12, 141, 200 }, StaticText { enabled, "Executor®/DOS m.nnx\r© 1986-1996 ARDI\rAll Rights Reserved\r\re-mail: info@ardi.com\r\rWorld Wide Web:  http://www.ardi.com" };
        { 9, 209, 119, 380 }, StaticText { enabled, "ARDI\rSuite 4-101\r1650 University Blvd., NE\rAlbuquerque, NM  87102\r\r+1 505 766 9115 Phone\r+1 505 766 5153 FAX" };
        { 11, 388, 142, 495 }, StaticText { enabled, "Mat Hostetter single-handedly designed, implemented and debugged the synthetic CPU portion of Executor.", 128 /* preserved alignment byte */ };
        { 231, 12, 285, 496 }, StaticText { enabled, "Apple and Macintosh are registered trademarks of Apple Computer, Inc.\rExecutor is a registered trademark of\r                                Abacus Research and Development, Inc." };
        { 177, 12, 215, 496 }, StaticText { enabled, "\0x11-Shift-1: About Box                    \0x11-Shift-2: Scan for Floppies\r\0x11-Shift-3: Screen Dump to File    \0x11-Shift-5: Preferences Panel" };
        { 152, 12, 171, 240 }, StaticText { enabled, "Left-Alt = \0x11     Right-Alt = Option" };
    }
};

resource 'DITL' (-4061, sysheap, preload) {
    {
        /* Item 1 */
        { 63, 101, 83, 161 },
        Button { enabled, "OK" };

        /* Item 2 */
        { 6, 10, 54, 266 },
        StaticText { disabled, "Your Floppy Disk has been Ejected.\0x0DPlease remove it from the drive.\0x0DUse \0x11-Shift-2 When you Insert Disks." };

    }
};

resource 'DITL' (-4060) {
    {
        /* Item 1 */
        { 66, 101, 86, 161 },
        Button { enabled, "OK" };

        /* Item 2 */
        { 5, 14, 55, 255 },
        StaticText { enabled, "This Application does not claim to be \0x2232-bit clean.\0x22  It may be unsuitable for use with ROMAW." };

    }
};

resource 'DITL' (-4000, purgeable) {
    {
        /* Item 1 */
        { 138, 256, 156, 336 },
        Button { enabled, "Open" };

        /* Item 2 */
        { 0, 571, 80, 589 },
        Button { enabled, "Hidden" };

        /* Item 3 */
        { 163, 256, 181, 336 },
        Button { enabled, "Cancel" };

        /* Item 4 */
        { 39, 232, 59, 347 },
        UserItem { disabled };

        /* Item 5 */
        { 68, 256, 86, 336 },
        Button { enabled, "Eject" };

        /* Item 6 */
        { 93, 256, 111, 336 },
        Button { enabled, "Desktop" };

        /* Item 7 */
        { 39, 12, 185, 230 },
        UserItem { enabled };

        /* Item 8 */
        { 39, 229, 185, 246 },
        UserItem { enabled };

        /* Item 9 */
        { 124, 252, 125, 340 },
        UserItem { disabled };

        /* Item 10 */
        { 0, 532, 101, 628 },
        StaticText { disabled, "" };

    }
};

resource 'DITL' (-3999, purgeable) {
    {
        /* Item 1 */
        { 132, 218, 150, 288 },
        Button { enabled, "Save" };

        /* Item 2 */
        { 158, 218, 176, 288 },
        Button { enabled, "Cancel" };

        /* Item 3 */
        { 136, 14, 152, 197 },
        StaticText { disabled, "Save as:" };

        /* Item 4 */
        { 29, 198, 49, 302 },
        UserItem { disabled };

        /* Item 5 */
        { 56, 218, 74, 288 },
        Button { enabled, "Eject" };

        /* Item 6 */
        { 82, 218, 100, 288 },
        Button { enabled, "Desktop" };

        /* Item 7 */
        { 157, 17, 173, 194 },
        EditText { enabled, "" };

        /* Item 8 */
        { 29, 14, 127, 197 },
        UserItem { disabled };

    }
};

resource 'DITL' (-3997, purgeable) {
    {
        /* Item 1 */
        { 50, 57, 68, 131 },
        Button { enabled, "OK" };

        /* Item 2 */
        { 14, 16, 46, 186 },
        StaticText { disabled, "Write Protection Violation" };

    }
};

resource 'DITL' (-3996, purgeable) {
    {
        /* Item 1 */
        { 50, 112, 68, 186 },
        Button { enabled, "Cancel" };

        /* Item 2 */
        { 50, 16, 68, 90 },
        Button { enabled, "OK" };

        /* Item 3 */
        { 14, 16, 46, 186 },
        StaticText { disabled, "Overwrite \0xD2^0\0xD3 ?" };

    }
};

resource 'DITL' (-3995, purgeable) {
    {
        /* Item 1 */
        { 52, 62, 70, 136 },
        Button { enabled, "OK" };

        /* Item 2 */
        { 14, 12, 46, 188 },
        StaticText { disabled, "Unexpected ROMlib\0xAA Error" };

    }
};

resource 'DITL' (-3994, purgeable) {
    {
        /* Item 1 */
        { 50, 62, 68, 136 },
        Button { enabled, "OK" };

        /* Item 2 */
        { 14, 16, 46, 186 },
        StaticText { disabled, "Disk not found" };

    }
};

resource 'DITL' (-3000) {
    {
        /* Item 1 */
        { 70, 140, 90, 200 },
        Button { enabled, "OK" };

        /* Item 2 */
        { 10, 72, 60, 300 },
        StaticText { enabled, "^0" };

    }
};

resource 'DITL' (128) {
    {
    }
};

resource 'DLOG' (-6043, purgeable) {
    /* Original pre-positioning layout: no optional position word. */
    { 0, 0, 188, 344 },
    dBoxProc,
    invisible,
    noGoAway,
    0x00000000, /* refCon */
    -6043, /* DITL */
    ""
};

resource 'DLOG' (-6042, purgeable) {
    /* Original pre-positioning layout: no optional position word. */
    { 0, 0, 166, 344 },
    dBoxProc,
    invisible,
    noGoAway,
    0x00000000, /* refCon */
    -6042, /* DITL */
    ""
};

resource 'DLOG' (-6044, purgeable) {
    { 0, 0, 110, 284 },
    dBoxProc,
    invisible,
    noGoAway,
    0x00000000,
    -6044,
    "New Folder",
    centerMainScreen
};

resource 'DLOG' (-4065) {
    { 64, 42, 348, 514 },
    altDBoxProc,
    invisible,
    noGoAway,
    0x00000000, /* refCon */
    -4065, /* DITL */
    "",
    centerMainScreen
};

resource 'DLOG' (-4063, sysheap, preload) {
    { 68, 44, 372, 486 },
    dBoxProc,
    visible,
    noGoAway,
    0x00000000, /* refCon */
    -4063, /* DITL */
    "Preferences",
    centerMainScreen
};

resource 'DLOG' (-4000, purgeable) {
    { 0, 0, 200, 348 },
    dBoxProc,
    invisible,
    noGoAway,
    0x00000000, /* refCon */
    -4000, /* DITL */
    "",
    centerMainScreen
};

resource 'DLOG' (-3999, purgeable) {
    { 0, 0, 184, 304 },
    dBoxProc,
    invisible,
    noGoAway,
    0x00000000, /* refCon */
    -3999, /* DITL */
    "",
    centerMainScreen
};

resource 'PAT ' (16, sysheap, purgeable) {
    $$pattern("system-patterns/PAT-16.png")
};

resource 'PAT ' (17, sysheap, purgeable) {
    $$pattern("system-patterns/PAT-17.png")
};

resource 'PAT ' (19, sysheap) {
    $$pattern("system-patterns/PAT-19.png")
};

resource 'PAT#' (0, purgeable) {
    {
        $$pattern("system-patterns/PAT-list-0-01.png");
        $$pattern("system-patterns/PAT-list-0-02.png");
        $$pattern("system-patterns/PAT-list-0-03.png");
        $$pattern("system-patterns/PAT-16.png");
        $$pattern("system-patterns/PAT-list-0-05.png");
        $$pattern("system-patterns/PAT-list-0-06.png");
        $$pattern("system-patterns/PAT-list-0-07.png");
        $$pattern("system-patterns/PAT-list-0-08.png");
        $$pattern("system-patterns/PAT-list-0-09.png");
        $$pattern("system-patterns/PAT-list-0-10.png");
        $$pattern("system-patterns/PAT-list-0-11.png");
        $$pattern("system-patterns/PAT-list-0-12.png");
        $$pattern("system-patterns/PAT-list-0-13.png");
        $$pattern("system-patterns/PAT-list-0-14.png");
        $$pattern("system-patterns/PAT-list-0-15.png");
        $$pattern("system-patterns/PAT-list-0-16.png");
        $$pattern("system-patterns/PAT-list-0-17.png");
        $$pattern("system-patterns/PAT-list-0-18.png");
        $$pattern("system-patterns/PAT-list-0-19.png");
        $$pattern("system-patterns/PAT-list-0-20.png");
        $$pattern("system-patterns/PAT-list-0-21.png");
        $$pattern("system-patterns/PAT-list-0-22.png");
        $$pattern("system-patterns/PAT-17.png");
        $$pattern("system-patterns/PAT-list-0-24.png");
        $$pattern("system-patterns/PAT-list-0-25.png");
        $$pattern("system-patterns/PAT-list-0-26.png");
        $$pattern("system-patterns/PAT-list-0-27.png");
        $$pattern("system-patterns/PAT-list-0-28.png");
        $$pattern("system-patterns/PAT-list-0-29.png");
        $$pattern("system-patterns/PAT-list-0-30.png");
        $$pattern("system-patterns/PAT-list-0-31.png");
        $$pattern("system-patterns/PAT-list-0-32.png");
        $$pattern("system-patterns/PAT-list-0-33.png");
        $$pattern("system-patterns/PAT-list-0-34.png");
        $$pattern("system-patterns/PAT-list-0-35.png");
        $$pattern("system-patterns/PAT-list-0-36.png");
        $$pattern("system-patterns/PAT-list-0-37.png");
        $$pattern("system-patterns/PAT-list-0-38.png");
    }
};

data 'PICT' (11) {
    $$pict("PICT-11.png", 0, 0)
};

resource 'STR ' (-8192) {
    "Printer"
};

resource 'STR ' (0) {
    "ROMAW System 7.0.1"
};

/* Historical installation notice: Please complete this form to finish installing Executor 2.  Your serial number and authentication key are with your Executor 2 CD-ROM.  If you don't have an Executor 2 CD-ROM, then you don't have a license to use this software.
Thank you for purchasing the Executor 2 CD-ROM. */
