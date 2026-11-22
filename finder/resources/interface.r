/*+===================================================================
  File:      interface.r

  Summary:   Editable Finder resources.

  Classes:   None.

  Functions: No explicitly exported functions.

  Origin:    Existing repository source in executor/resources/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Editable Finder resources. */
#include "resource_types.r"

resource 'ALRT' (6010, purgeable) {
    { 0, 0, 98, 368 },
    6010, /* DITL */
    {
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
    },
    alertPositionMainScreen
};

resource 'ALRT' (6011, purgeable) {
    { 0, 0, 98, 368 },
    6011, /* DITL */
    {
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
    },
    alertPositionMainScreen
};

resource 'ALRT' (6012, purgeable) {
    { 0, 0, 98, 368 },
    6012, /* DITL */
    {
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
    },
    alertPositionMainScreen
};

resource 'ALRT' (6013, purgeable) {
    { 0, 0, 98, 368 },
    6013, /* DITL */
    {
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
    },
    alertPositionMainScreen
};

resource 'ALRT' (6014, purgeable) {
    { 0, 0, 98, 368 },
    6014, /* DITL */
    {
        Cancel, visible, silent;
        Cancel, visible, silent;
        Cancel, visible, silent;
        Cancel, visible, silent;
    },
    alertPositionMainScreen
};

resource 'ALRT' (6015, purgeable) {
    { 0, 0, 98, 368 },
    6015, /* DITL */
    {
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
    },
    alertPositionMainScreen
};

resource 'ALRT' (12801, purgeable) {
    { 0, 0, 98, 368 },
    12801, /* DITL */
    {
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
    },
    alertPositionParentWindowScreen
};

resource 'DITL' (-16603, sysheap) {
    {
        /* Item 1 */
        { 148, 133, 168, 192 },
        Button { enabled, "Cancel" };

        /* Item 2 */
        { 148, 205, 168, 331 },
        Button { enabled, "Quit Application" };

        /* Item 3 */
        { 10, 20, 42, 52 },
        Icon { disabled, 2 };

        /* Item 4 */
        { 7, 74, 135, 331 },
        StaticText { disabled, "There is not enough memory available to open \0xD2^0\0xD3.\0x0D\0x0DDo you want to quit the application \0xD2^1\0xD3, which has no open windows, and open \0xD2^0\0xD3 instead?" };

    }
};

resource 'DITL' (-16602, sysheap) {
    {
        /* Item 1 */
        { 148, 133, 168, 192 },
        Button { enabled, "Cancel" };

        /* Item 2 */
        { 148, 205, 168, 331 },
        Button { enabled, "Quit Applications" };

        /* Item 3 */
        { 10, 20, 42, 52 },
        Icon { disabled, 2 };

        /* Item 4 */
        { 7, 74, 135, 331 },
        StaticText { disabled, "There is not enough memory available to open \0xD2^0\0xD3.\0x0D\0x0DDo you want to quit the applications that have no open windows and open \0xD2^0\0xD3 instead?" };

    }
};

resource 'DITL' (1260, purgeable) {
    {
        /* Item 1 */
        { 8, 10, 60, 224 },
        StaticText { disabled, "To cancel printing, hold down the \0x11 key and type a period (.)." };

    }
};

resource 'DITL' (5066, purgeable) {
    {
        /* Item 1 */
        { 0, 0, 200, 360 },
        Picture { disabled, 5066 };

    }
};

resource 'DITL' (6010, purgeable) {
    {
        /* Item 1 */
        { 68, 299, 88, 358 },
        Button { enabled, "OK" };

        /* Item 2 */
        { 68, 227, 88, 286 },
        StaticText { disabled, "" };

        /* Item 3 */
        { 10, 20, 42, 52 },
        Icon { disabled, 0 };

        /* Item 4 */
        { 7, 74, 55, 358 },
        StaticText { disabled, "^0" };

    }
};

resource 'DITL' (6011, purgeable) {
    {
        /* Item 1 */
        { 68, 299, 88, 358 },
        Button { enabled, "OK" };

        /* Item 2 */
        { 68, 227, 88, 286 },
        StaticText { disabled, "" };

        /* Item 3 */
        { 10, 20, 42, 52 },
        Icon { disabled, 1 };

        /* Item 4 */
        { 7, 74, 55, 358 },
        StaticText { disabled, "^0" };

    }
};

resource 'DITL' (6012, purgeable) {
    {
        /* Item 1 */
        { 68, 299, 88, 358 },
        Button { enabled, "OK" };

        /* Item 2 */
        { 68, 227, 88, 286 },
        Button { enabled, "Cancel" };

        /* Item 3 */
        { 10, 20, 42, 52 },
        Icon { disabled, 2 };

        /* Item 4 */
        { 7, 74, 55, 358 },
        StaticText { disabled, "^0" };

    }
};

resource 'DITL' (6013, purgeable) {
    {
        /* Item 1 */
        { 68, 284, 88, 358 },
        Button { enabled, "Continue" };

        /* Item 2 */
        { 68, 197, 88, 271 },
        Button { enabled, "Stop" };

        /* Item 3 */
        { 10, 20, 42, 52 },
        Icon { disabled, 2 };

        /* Item 4 */
        { 7, 74, 55, 358 },
        StaticText { disabled, "^0" };

    }
};

resource 'DITL' (6014, purgeable) {
    {
        /* Item 1 */
        { 68, 299, 88, 358 },
        Button { enabled, "OK" };

        /* Item 2 */
        { 68, 227, 88, 286 },
        Button { enabled, "Cancel" };

        /* Item 3 */
        { 10, 20, 42, 52 },
        Icon { disabled, 2 };

        /* Item 4 */
        { 7, 74, 55, 358 },
        StaticText { disabled, "^0" };

    }
};

resource 'DITL' (6015, purgeable) {
    {
        /* Item 1 */
        { 68, 299, 88, 358 },
        Button { enabled, "Save" };

        /* Item 2 */
        { 68, 227, 88, 286 },
        Button { enabled, "Cancel" };

        /* Item 3 */
        { 10, 20, 42, 52 },
        Icon { disabled, 2 };

        /* Item 4 */
        { 7, 74, 55, 358 },
        StaticText { disabled, "^0" };

        /* Item 5 */
        { 68, 74, 88, 159 },
        Button { enabled, "Don\0xD5t Save" };

    }
};

resource 'DITL' (12801, purgeable) {
    {
        /* Item 1 */
        { 68, 280, 88, 358 },
        Button { enabled, "Verify" };

        /* Item 2 */
        { 68, 98, 88, 176 },
        Button { enabled, "Cancel" };

        /* Item 3 */
        { 68, 189, 88, 267 },
        Button { enabled, "Remove" };

        /* Item 4 */
        { 7, 74, 55, 358 },
        StaticText { disabled, "^0" };

        /* Item 5 */
        { 10, 20, 42, 52 },
        Icon { disabled, 2 };

    }
};

resource 'DLOG' (1260, purgeable) {
    { 100, 144, 148, 368 },
    dBoxProc,
    visible,
    noGoAway,
    0x00000000, /* refCon */
    1260, /* DITL */
    "",
    alertPositionMainScreen
};

resource 'DLOG' (5066, purgeable) {
    { 0, 0, 200, 360 },
    altDBoxProc,
    visible,
    noGoAway,
    0x00000000, /* refCon */
    5066, /* DITL */
    "",
    alertPositionMainScreen
};

resource 'DLOG' (12250, purgeable) {
    { 40, 100, 292, 333 },
    noGrowDocProc,
    invisible,
    goAway,
    0x00000000, /* refCon */
    -4064, /* DITL */
    "",
    noAutoCenter
};

resource 'RECT' (-16602, sysheap) {
    { 44, 28, 222, 369 }
};

resource 'RECT' (10517, purgeable) {
    { 0, 0, 96, 122 }
};

resource 'dctb' (12250) {
    0, /* seed */
    0x0000, /* flags */
    -1, /* last color index */
    {
    }
};

resource 'fmnu' (1251, preload) {
    1, /* format version */
    1251, /* menu ID */
    0x00000000, /* options */
    "\0x14",
    {
        'abou', 0x8005, 0, 0, "p";
        'xxx0', 0x0000, 0, 0, "-";
    }
};

resource 'fmnu' (1252, preload) {
    1, /* format version */
    1252, /* menu ID */
    0x00000000, /* options */
    "File",
    {
        'new ', 0xC006, 78, 0, "New Folder";
        'sope', 0x1006, 79, 0, "Open";
        'spri', 0x1002, 80, 0, "Print";
        'clos', 0xC006, 87, 0, "Close Window";
        'xxx0', 0x0000, 0, 0, "-";
        'sinf', 0x1002, 73, 0, "Get Info";
        'sprv', 0x1002, 0, 0, "Sharing\0xC9";
        'sdup', 0x1002, 68, 0, "Duplicate";
        'sali', 0x1002, 77, 0, "Make Alias";
        'sput', 0x1002, 89, 0, "Put Away";
        'xxx0', 0x0000, 0, 0, "-";
        'find', 0x8110, 70, 0, "Find\0xC9";
        'finn', 0x8100, 71, 0, "Find Again";
        'xxx0', 0x0000, 0, 0, "-";
        'pgsu', 0xC002, 0, 0, "Page Setup\0xC9";
        'pwin', 0xC006, 0, 0, "m";
    }
};

resource 'fmnu' (1253, preload) {
    1, /* format version */
    1253, /* menu ID */
    0x00000000, /* options */
    "Edit",
    {
        'undo', 0xC002, 90, 0, "Undo";
        'xxx0', 0x0000, 0, 0, "-";
        'cut ', 0xC002, 88, 0, "Cut";
        'copy', 0xC002, 67, 0, "Copy";
        'past', 0xC002, 86, 0, "Paste";
        'cler', 0xC002, 0, 0, "Clear";
        'sela', 0xC002, 65, 0, "Select All";
        'xxx0', 0x0000, 0, 0, "-";
        'shcl', 0x8100, 0, 0, "Show Clipboard";
    }
};

resource 'fmnu' (1254, preload) {
    1, /* format version */
    1254, /* menu ID */
    0x00000000, /* options */
    "View",
    {
    }
};

resource 'fmnu' (1255, preload) {
    1, /* format version */
    1255, /* menu ID */
    0x00000000, /* options */
    "Special",
    {
        'clea', 0x4006, 0, 0, "Clean Up";
        'empt', 0x8006, 0, 0, "Empty Trash\0xC9";
        'xxx0', 0x0000, 0, 0, "-";
        'seje', 0x1002, 69, 0, "Eject Disk";
        'sera', 0x1002, 0, 0, "Erase Disk\0xC9";
    }
};

resource 'fmnu' (1256, preload) {
    1, /* format version */
    1256, /* menu ID */
    0x00000000, /* options */
    "Label",
    {
        'sscl', 0x404A, 0, 0, "None";
        'xxx0', 0x0000, 0, 0, "-";
        'sscl', 0x404E, 0, 0, "a";
        'sscl', 0x404E, 0, 0, "l";
        'sscl', 0x404E, 0, 0, "a";
        'sscl', 0x404E, 0, 0, "n";
        'sscl', 0x404E, 0, 0, "j";
        'sscl', 0x404E, 0, 0, "e";
        'sscl', 0x404E, 0, 0, "f";
    }
};

resource 'fmnu' (1276, purgeable) {
    0, /* format version */
    1276, /* menu ID */
    0x00810000, /* options */
    "",
    {
        0, 0xA001, 0, 0, "None";
        'xxx0', 0x0000, 0, 0, "-";
        7, 0xA005, 0, 0, "\0xA5";
        6, 0xA005, 0, 0, "j";
        5, 0xA005, 0, 0, "e";
        4, 0xA005, 0, 0, "f";
        3, 0xA005, 0, 0, "f";
        2, 0xA005, 0, 0, "\0xA5";
        1, 0xA005, 0, 0, "\0xA5";
    }
};

resource 'fmnu' (11030, purgeable) {
    0, /* format version */
    11030, /* menu ID */
    0x00810000, /* options */
    "",
    {
        3, 0xA001, 0, 0, "contains";
        4, 0xA001, 0, 0, "starts with";
        5, 0xA001, 0, 0, "ends with";
        2, 0xA001, 0, 0, "is";
        -3, 0xA001, 0, 0, "is not";
        -2, 0xA001, 0, 0, "doesn\0xD5t contain";
    }
};

resource 'fmnu' (11031, purgeable) {
    0, /* format version */
    11031, /* menu ID */
    0x00810000, /* options */
    "",
    {
        2, 0xA001, 0, 0, "is";
        -3, 0xA001, 0, 0, "is not";
    }
};

resource 'fmnu' (11032, purgeable) {
    0, /* format version */
    11032, /* menu ID */
    0x00810000, /* options */
    "",
    {
        2, 0xA001, 0, 0, "is";
        -1, 0xA001, 0, 0, "is before";
        1, 0xA001, 0, 0, "is after";
        -3, 0xA001, 0, 0, "is not";
    }
};

resource 'fmnu' (11034, purgeable) {
    0, /* format version */
    11034, /* menu ID */
    0x00810000, /* options */
    "",
    {
        3, 0xA001, 0, 0, "contain";
        -2, 0xA001, 0, 0, "do not contain";
    }
};

resource 'fmnu' (11035, purgeable) {
    0, /* format version */
    11035, /* menu ID */
    0x00810000, /* options */
    "",
    {
        3, 0xA001, 0, 0, "contains";
        -2, 0xA001, 0, 0, "doesn\0xD5t contain";
    }
};

resource 'fmnu' (11036, purgeable) {
    0, /* format version */
    11036, /* menu ID */
    0x00810000, /* options */
    "",
    {
        1, 0xA001, 0, 0, "alias";
        2, 0xA001, 0, 0, "application";
        3, 0xA001, 0, 0, "document";
        4, 0xA001, 0, 0, "folder";
        5, 0xA001, 0, 0, "stationery";
    }
};

resource 'fmnu' (11037, purgeable) {
    0, /* format version */
    11037, /* menu ID */
    0x00810000, /* options */
    "",
    {
        -1, 0xA001, 0, 0, "is less than";
        1, 0xA001, 0, 0, "is greater than";
    }
};

resource 'fmnu' (11038, purgeable) {
    0, /* format version */
    11038, /* menu ID */
    0x00810000, /* options */
    "",
    {
        1, 0xA001, 0, 0, "locked";
        0, 0xA001, 0, 0, "unlocked";
    }
};

resource 'fmnu' (11039, purgeable) {
    0, /* format version */
    11039, /* menu ID */
    0x00810000, /* options */
    "",
    {
        2, 0xA001, 0, 0, "is";
        -1, 0xA001, 0, 0, "is before";
        1, 0xA001, 0, 0, "is after";
        -3, 0xA001, 0, 0, "is not";
    }
};

resource 'fmnu' (11052, purgeable) {
    0, /* format version */
    11052, /* menu ID */
    0x00810000, /* options */
    "",
    {
        3050, 0xA001, 0, 0, "name";
        -12805, 0xA001, 0, 0, "size";
        3051, 0xA001, 0, 0, "kind";
        -3061, 0xA001, 0, 0, "label";
        -20803, 0xA001, 0, 0, "date created";
        -20805, 0xA001, 0, 0, "date modified";
        12802, 0xA001, 0, 0, "version";
        20807, 0xA001, 0, 0, "comments";
        -20806, 0xA001, 0, 0, "lock";
    }
};

resource 'fmnu' (11056, purgeable) {
    0, /* format version */
    11056, /* menu ID */
    0x00810000, /* options */
    "",
    {
        1, 0xA002, 0, 0, "on all disks";
        4, 0xA000, 0, 0, "-";
        5, 0xA000, 0, 0, "-";
        3, 0xA002, 0, 0, "the selected items";
    }
};

resource 'fmnu' (11301, purgeable) {
    0, /* format version */
    11301, /* menu ID */
    0x00810000, /* options */
    "",
    {
        0, 0x0001, 0, 0, "<Any User>";
        -10, 0x0001, 0, 0, "-";
    }
};

resource 'fmnu' (11302, purgeable) {
    0, /* format version */
    11302, /* menu ID */
    0x00810000, /* options */
    "",
    {
        0, 0x0001, 0, 0, "<None>";
        -10, 0x0001, 0, 0, "-";
    }
};

resource 'fmnu' (19550, purgeable) {
    0, /* format version */
    19550, /* menu ID */
    0x00810000, /* options */
    "",
    {
    }
};

resource 'fmnu' (19568, purgeable) {
    0, /* format version */
    19568, /* menu ID */
    0x00810000, /* options */
    "",
    {
        9, 0xA021, 0, 0, "9";
        10, 0xA021, 0, 0, "10";
        11, 0xA021, 0, 0, "11";
        12, 0xA021, 0, 0, "12";
        14, 0xA021, 0, 0, "14";
        18, 0xA021, 0, 0, "18";
        24, 0xA021, 0, 0, "24";
    }
};

resource 'pltt' (128) {
    { /* red, green, blue, usage, tolerance, private flags, private data */
        0x5600, 0x2C9D, 0x0524, 0x1702, 6600, 0, 0;
        0x0000, 0x64A5, 0x11BD, 0x1702, 6600, 0, 0;
        0x0000, 0x0000, 0xD400, 0x1702, 6600, 0, 0;
        0x0241, 0xAB54, 0xEAFF, 0x1702, 6600, 0, 0;
        0xF2D7, 0x0856, 0x84EC, 0x1702, 6600, 0, 0;
        0xDD68, 0x08C2, 0x06A2, 0x1702, 6600, 0, 0;
        0xFFFF, 0x648A, 0x028C, 0x1702, 6600, 0, 0;
    }
};
