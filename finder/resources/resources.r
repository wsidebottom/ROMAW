/*+===================================================================
  File:      resources.r

  Summary:   Editable Finder resources.

  Classes:   None.

  Functions: No explicitly exported functions.

  Origin:    Existing repository source in executor/resources/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Editable Finder resources. */
#include "resource_types.r"

/* Native CDEF 128 is declared in guest_code.r and implemented in ../guest/CDEF-128.c. */

resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    disableOptionSwitch,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    getFrontClicks,
    acceptChildDiedEvents,
    is32BitCompatible,
    isHighLevelEventAware,
    localAndRemoteHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    notDisplayManagerAware,
    reserved,
    reserved,
    302080, /* preferred memory, bytes */
    302080 /* minimum memory, bytes */
};

/* Native WDEF 128 is declared in guest_code.r and implemented in ../guest/WDEF-128.c. */

resource 'acur' (6500, locked, preload) {
    8, /* total frames */
    0, /* current frame; starts at zero */
    {
        6500, 0; /* Frame 1: 'CURS' ID 6500 */
        6501, 0; /* Frame 2: 'CURS' ID 6501 */
        6502, 0; /* Frame 3: 'CURS' ID 6502 */
        6503, 0; /* Frame 4: 'CURS' ID 6503 */
        6504, 0; /* Frame 5: 'CURS' ID 6504 */
        6505, 0; /* Frame 6: 'CURS' ID 6505 */
        6506, 0; /* Frame 7: 'CURS' ID 6506 */
        6507, 0; /* Frame 8: 'CURS' ID 6507 */
    }
};

resource 'aete' (0) {
    0x0095, /* terminology version */
    0, 0, /* language, script */
    {
        "7.0 Finder Suite",
        "This is the original Finder suite. These events will be supported in the future, but the syntax may be changed in a future Finder release.",
        'FNDR', 1, 1,
        {
            "open about box",
            "Open the 'About This Mac' window",
            'aevt', 'abou', /* event */
            'null', "", 0x8000, /* reply */
            'null', "", 0x8000, /* direct parameter */
            0, /* parameter count */
            {
            };
            "copy to",
            "Copies one or more items into a folder",
            'FNDR', 'drag', /* event */
            'null', "", 0x8000, /* reply */
            'alis', "Alias for folder into which the items are copied", 0x0000, /* direct parameter */
            1, /* parameter count */
            {
                "from", 'fsel', 'list',
                "List of aliases for items to be copied", 0x4000;
            };
            "duplicate",
            "Duplicate a set of items in a folder",
            'FNDR', 'sdup', /* event */
            'null', "", 0x8000, /* reply */
            'alis', "Alias for folder containing the items", 0x0000, /* direct parameter */
            1, /* parameter count */
            {
                "items", 'fsel', 'list',
                "List of aliases for items in the folder", 0x4000;
            };
            "empty trash",
            "Empties the trash",
            'FNDR', 'empt', /* event */
            'null', "", 0x8000, /* reply */
            'null', "", 0x8000, /* direct parameter */
            0, /* parameter count */
            {
            };
            "make aliases for",
            "Make aliases to items from a single folder",
            'FNDR', 'sali', /* event */
            'null', "", 0x0000, /* reply */
            'alis', "Alias for folder containing the items", 0x0000, /* direct parameter */
            1, /* parameter count */
            {
                "items", 'fsel', 'list',
                "List of aliases for items in folder", 0x4000;
            };
            "move to",
            "Move one or more items into a folder",
            'FNDR', 'move', /* event */
            'null', "", 0x8000, /* reply */
            'alis', "Alias for destination folder", 0x0000, /* direct parameter */
            1, /* parameter count */
            {
                "from", 'fsel', 'list',
                "List of aliases for items to be moved ", 0x4000;
            };
            "sleep",
            "Put portable into sleep mode",
            'FNDR', 'slep', /* event */
            'null', "", 0x8000, /* reply */
            'null', "", 0x8000, /* direct parameter */
            0, /* parameter count */
            {
            };
            "shut down",
            "Shuts down the Macintosh if all applications can quit",
            'FNDR', 'shut', /* event */
            'null', "", 0x8000, /* reply */
            'null', "", 0x8000, /* direct parameter */
            0, /* parameter count */
            {
            };
            "open",
            "Open folders, files, or applications from a given folder",
            'FNDR', 'sope', /* event */
            'null', "", 0x8000, /* reply */
            'alis', "Alias for folder containing the items", 0x0000, /* direct parameter */
            1, /* parameter count */
            {
                "items", 'fsel', 'list',
                "List of aliases for items in the folder", 0x4000;
            };
            "print",
            "Print items from a given folder",
            'FNDR', 'spri', /* event */
            'null', "", 0x8000, /* reply */
            'alis', "Alias for folder containing the items", 0x0000, /* direct parameter */
            1, /* parameter count */
            {
                "items", 'fsel', 'list',
                "List of aliases for items in folder", 0x4000;
            };
            "put away",
            "Put away items from a given folder",
            'FNDR', 'sput', /* event */
            'alis', "", 0x0000, /* reply */
            'alis', "Alias for folder containing the items", 0x0000, /* direct parameter */
            1, /* parameter count */
            {
                "items", 'fsel', 'list',
                "List of aliases to items in folder", 0x4000;
            };
            "restart",
            "Restart the Macintosh",
            'FNDR', 'rest', /* event */
            'null', "", 0x8000, /* reply */
            'null', "", 0x8000, /* direct parameter */
            0, /* parameter count */
            {
            };
            "select",
            "Select items in a folder",
            'FNDR', 'srev', /* event */
            'null', "", 0x8000, /* reply */
            'alis', "Alias for folder containing the items", 0x0000, /* direct parameter */
            1, /* parameter count */
            {
                "items", 'fsel', 'list',
                "List of aliases for items in folder", 0x4000;
            };
        };
    }
};

resource 'fAni' (11000, purgeable) {
    {
        11000; /* frame 1: PICT ID */
        11001; /* frame 2: PICT ID */
        11002; /* frame 3: PICT ID */
        11003; /* frame 4: PICT ID */
    }
};

resource 'fdmn' (5000, purgeable, preload) {
    1255, /* menu ID */
    'xx31', /* locate command */
    'slep', /* inserted command */
    0x8100, 0, 0,
    "Sleep"
};

resource 'fdmn' (5001, purgeable, preload) {
    1255, /* menu ID */
    'shut', /* locate command */
    'xx31', /* inserted command */
    0x0000, 0, 0,
    "-"
};

resource 'fdmn' (5003, purgeable, preload) {
    -16490, /* menu ID */
    -2, /* locate command */
    'fshc', /* inserted command */
    0x8002, 0, 0,
    "Finder Shortcuts"
};

resource 'fdmn' (5004, purgeable, preload) {
    1252, /* menu ID */
    'finn', /* locate command */
    'sfor', /* inserted command */
    0x1002, 82, 0,
    "Reveal Original"
};

resource 'fmap' (128, purgeable) {
    { /* key, mapped value; final zero entry terminates the map */
        'pgsu', (5042 << 16) | 5046;
        'pwin', (5043 << 16) | 5047;
        'open', (5043 << 16) | 5048;
        'prnt', (5043 << 16) | 5049;
        'puta', (5043 << 16) | 5050;
        'eras', (5043 << 16) | 5051;
        'empt', (5043 << 16) | 5052;
        'opno', (5043 << 16) | 5048;
        'opod', (5043 << 16) | 5048;
        'info', (5044 << 16) | 5053;
        'priv', (5044 << 16) | 5054;
        'new ', (5045 << 16) | 5055;
        'move', (5045 << 16) | 5056;
        'drag', (5045 << 16) | 5057;
        'sdup', (5045 << 16) | 5058;
        'fmvf', (5045 << 16) | 5059;
        'frep', (5045 << 16) | 5060;
        'alis', (5045 << 16) | 5072;
        0, 0;
    }
};

resource 'fmap' (129, purgeable) {
    { /* key, mapped value; final zero entry terminates the map */
        -48, 5010;
        -34, 5011;
        -36, 5012;
        -49, 5013;
        -61, 5013;
        -54, 5013;
        -5000, 5014;
        -5033, 5015;
        -5043, 5015;
        -124, 5016;
        -120, 5017;
        -43, 5018;
        -33, 5019;
        -44, 5020;
        -122, 5021;
        -47, 5022;
        -108, 5023;
        -42, 5024;
        -10, 5025;
        -11, 5026;
        -12, 5027;
        -14, 5028;
        -45, 5032;
        -53, 5070;
        -1804, 5071;
        -1070, 5016;
        -15277, 5073;
        -15276, 5074;
        0, 0;
    }
};

resource 'fmap' (151, purgeable) {
    { /* key, mapped value; final zero entry terminates the map */
        9750, 120;
        11250, 120;
        11500, 120;
        11501, 120;
        11750, 120;
        12500, 120;
        12750, 120;
        12751, 120;
        12752, 120;
        12753, 120;
        12754, 120;
        12755, 120;
        12756, 120;
        12757, 120;
        12758, 120;
        12759, 120;
        12760, 120;
        12761, 120;
        13000, 120;
        13500, 120;
        14000, 120;
        14250, 120;
        14500, 120;
        14750, 120;
        15500, 120;
        15750, 120;
        17000, 120;
        17001, 120;
        17250, 120;
        20500, 120;
        23250, 120;
        23500, 120;
        0, 0;
    }
};

resource 'fmap' (152, purgeable) {
    { /* key, mapped value; final zero entry terminates the map */
        20518, 120;
        10271, 120;
        12011, 120;
        10010, 120;
        11774, 120;
        1261, 120;
        1264, 120;
        20515, 120;
        1262, 120;
        5042, 120;
        11340, 120;
        11341, 120;
        11342, 120;
        11343, 120;
        11344, 120;
        11345, 120;
        11346, 120;
        11347, 120;
        11020, 120;
        11021, 120;
        11022, 120;
        11023, 120;
        11024, 120;
        11025, 120;
        11026, 120;
        11027, 120;
        11028, 120;
        11293, 120;
        11294, 120;
        0, 0;
    }
};

resource 'fmap' (5010, purgeable) {
    { /* key, mapped value; final zero entry terminates the map */
        'name', 3050;
        'xy  ', 3074;
        'scpt', 3058;
        'colr', 3061;
        'main', 0;
        'size', 3054;
        'cnt ', 3055;
        'kind', 3051;
        'what', 3052;
        'wher', 3056;
        'path', 3070;
        'phys', 20802;
        'crea', 20803;
        'modi', 20805;
        'lock', 20806;
        'comm', 20807;
        'snam', 20808;
        'ownr', 11301;
        'grup', 11302;
        'priv', 11309;
        'logi', 11550;
        'free', 11552;
        'vers', 12801;
        'svrs', 12802;
        'spad', 17050;
        'msiz', 17302;
        0, 0;
    }
};

resource 'fmap' (5011, purgeable) {
    { /* key, mapped value; final zero entry terminates the map */
        'fold', 11250;
        'disk', 11500;
        'trsh', 11750;
        'desk', 10250;
        'appl', 17250;
        'docu', 17000;
        'file', 12750;
        'afil', 20500;
        'cdev', 12250;
        'movr', 13000;
        'edit', 9750;
        'abot', 10000;
        'clip', 10500;
        0, 0;
    }
};

resource 'fmap' (7010, purgeable) {
    { /* key, mapped value; final zero entry terminates the map */
        2, 3050;
        4, -20812;
        4, -20813;
        4, -20804;
        4, -20810;
        5, 3051;
        7, -3061;
        3, -20805;
        8, 12802;
        6, 20807;
        0, 0;
    }
};

resource 'fmap' (10517, purgeable) {
    { /* key, mapped value; final zero entry terminates the map */
        'unkn', 10511;
        'PICT', 10513;
        'TEXT', 10514;
        'styl', 10515;
        'snd ', 10516;
        0, 0;
    }
};

resource 'fmap' (11330, purgeable) {
    { /* key, mapped value; final zero entry terminates the map */
        'INIT', 7;
        'thng', 7;
        'cdev', 6;
        'APPC', 6;
        'APPD', 5;
        'dfil', 5;
        'DFIL', 5;
        'ffil', 1;
        'tfil', 1;
        'FFIL', 1;
        'sfil', 2;
        'RDEV', 7;
        'PRES', 7;
        'PRER', 7;
        'fext', 7;
        'detf', 7;
        'dsam', 7;
        'msam', 7;
        'csam', 7;
        'dash', 7;
        'ldev', 7;
        'mlts', 7;
        'libr', 7;
        'LTMC', 7;
        'cfil', 1;
        'cbnd', 7;
        'tbnd', 7;
        'fbnd', 7;
        'adev', 7;
        'ddev', 7;
        'mdev', 7;
        'appe', 7;
        'etpp', 7;
        'ttpp', 7;
        'atlk', 7;
        'help', 7;
        'LWFN', 1;
        'scri', 7;
        'kfil', 4;
        'ifil', 3;
        'dic0', 7;
        'dic1', 7;
        'dic2', 7;
        'dict', 7;
        'vbnd', 7;
        'pdvr', 7;
        'pext', 7;
        'drpt', 7;
        'uspt', 7;
        'ndrv', 7;
        'expt', 7;
        'shlb', 7;
        'CLMP', 7;
        'xlts', 7;
        'AINI', 7;
        'comd', 7;
        0, 0;
    }
};

resource 'fmap' (11331, purgeable) {
    { /* key, mapped value; final zero entry terminates the map */
        1, 6;
        2, 1;
        3, 1;
        4, 1;
        5, 2;
        6, 4;
        7, 5;
        8, 7;
        0, 0;
    }
};

resource 'fmap' (12761, purgeable) {
    { /* key, mapped value; final zero entry terminates the map */
        'RDEV', 1;
        'PRER', 1;
        'PRES', 1;
        'INIT', 2;
        'thng', 2;
        'fext', 3;
        'detf', 11;
        'dash', 10;
        'dsam', 10;
        'msam', 10;
        'csam', 10;
        'sLnk', 2;
        'ddev', 4;
        'cbnd', 5;
        'tbnd', 5;
        'fbnd', 5;
        'cdev', 6;
        'adev', 2;
        'mdev', 2;
        'appe', 2;
        'etpp', 2;
        'ttpp', 2;
        'atlk', 2;
        'LWFN', 7;
        'scri', 2;
        'pdvr', 1;
        'pext', 8;
        'drpt', 9;
        'uspt', 9;
        0, 0;
    }
};

resource 'fmap' (14510, purgeable) {
    { /* key, mapped value; final zero entry terminates the map */
        0, 1;
    }
};

resource 'fmap' (17010, purgeable) {
    { /* key, mapped value; final zero entry terminates the map */
        'TEXT', 'ttxt';
        'PICT', 'ttxt';
        'lttr', 'lap2';
        'sjob', 'ttxt';
        'rjob', 'ttxt';
        'qjob', 'ttxt';
        0, 0;
    }
};

resource 'fval' (128, purgeable) {
    { /* default Finder preference words, in record order */
        1877;
        32768;
        65535;
        65535;
        3;
        9;
        1;
        0;
        0;
        2;
        0;
        0;
        1;
        1;
        1;
        1;
        0;
        0;
        4;
        2;
        0;
        6;
        36;
        0;
        0;
    }
};

resource 'fvew' (-31522, purgeable) {
    { 0, 0, 139, 279 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 139, 279 }, /* content bounds */
    0x0001, /* layout options */
    { 0, 0, 139, 279 }, /* view bounds */
    {
        /* View 1 */
        { 7, 8, 71, 269 },
        0x00000000, 0x00000010, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
            Text { "You have opened a stationery pad, so a new document will be created.\0x0D\0x0DType a name for the new document:" };
        };
        /* View 2 */
        { 73, 10, 93, 269 },
        0x00000000, 0x00000001, 3057, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
        };
        /* View 3 */
        { 106, 10, 126, 88 },
        0x00000000, 0x00000001, 17051, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Text { "Save In\0xC9" };
            Command { 'dloc' };
        };
        /* View 4 */
        { 106, 100, 126, 178 },
        0x00000000, 0x00000001, 17052, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Text { "Cancel" };
            Command { 'ucan' };
        };
        /* View 5 */
        { 102, 187, 130, 273 },
        0x00000000, 0x00000001, 17053, 7250,
        {
            Words { { 0, 12, 1, 0, 591, 19200, 28523, 8224 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (-31521, purgeable) {
    { 0, 0, 76, 280 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 76, 280 }, /* content bounds */
    0x0005, /* layout options */
    { 0, 0, 76, 280 }, /* view bounds */
    {
        /* View 1 */
        { 7, 9, 23, 210 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
            Text { "Items remaining to be signed:" };
        };
        /* View 2 */
        { 7, 210, 23, 252 },
        0x00000000, 0x00000000, 17056, 3250,
        {
            Words { { 0, 12, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 29, 9, 45, 80 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
            Text { "Signing:" };
        };
        /* View 4 */
        { 29, 81, 45, 270 },
        0x00000000, 0x00000000, 17057, 3250,
        {
            Words { { 0, 12, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 5 */
        { 55, 10, 66, 198 },
        0x00000000, 0x00000000, 17058, 5250,
        {
            Words { { 1, 21 } }; /* constructor offset 0 */
        };
        /* View 6 */
        { 51, 211, 71, 270 },
        0x00000000, 0x00000000, 3063, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Text { "Stop" };
            Command { 'ucan' };
        };
    }
};

resource 'fvew' (2751, purgeable) {
    { 0, 0, 252, 432 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 252, 432 }, /* content bounds */
    0x0004, /* layout options */
    { 0, 0, 252, 432 }, /* view bounds */
    {
        /* View 1 */
        { 0, 0, 222, 432 },
        0x00000000, 0x00000000, -1, 8500,
        {
            Picture { 2778 };
        };
        /* View 2 */
        { 144, 19, 199, 96 },
        0x00000000, 0x00000000, -1, 8500,
        {
            Picture { 2760 };
        };
        /* View 3 */
        { 225, 0, 239, 96 },
        0x00000000, 0x00000000, -1, 8500,
        {
            Picture { 2769 };
        };
        /* View 4 */
        { 222, 349, 242, 422 },
        0x00000000, 0x00000020, -1, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, -1, 0 };
            Text { "Next" };
            Command { 'nxtv' };
        };
    }
};

resource 'fvew' (2752, purgeable) {
    { 0, 0, 252, 432 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 252, 432 }, /* content bounds */
    0x0004, /* layout options */
    { 0, 0, 252, 432 }, /* view bounds */
    {
        /* View 1 */
        { 0, 0, 222, 432 },
        0x00000000, 0x00000000, -1, 8500,
        {
            Picture { 2779 };
        };
        /* View 2 */
        { 54, 9, 131, 101 },
        0x00000000, 0x00000000, -1, 8500,
        {
            Picture { 2761 };
        };
        /* View 3 */
        { 225, 0, 239, 96 },
        0x00000000, 0x00000000, -1, 8500,
        {
            Picture { 2770 };
        };
        /* View 4 */
        { 222, 266, 242, 339 },
        0x00000000, 0x00000020, -1, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, -1, 0 };
            Text { "Previous" };
            Command { 'prvv' };
        };
        /* View 5 */
        { 222, 349, 242, 422 },
        0x00000000, 0x00000020, -1, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, -1, 0 };
            Text { "Next" };
            Command { 'nxtv' };
        };
    }
};

resource 'fvew' (2753, purgeable) {
    { 0, 0, 252, 432 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 252, 432 }, /* content bounds */
    0x0004, /* layout options */
    { 0, 0, 252, 432 }, /* view bounds */
    {
        /* View 1 */
        { 0, 0, 222, 432 },
        0x00000000, 0x00000000, -1, 8500,
        {
            Picture { 2780 };
        };
        /* View 2 */
        { 60, 24, 125, 96 },
        0x00000000, 0x00000000, -1, 8500,
        {
            Picture { 2762 };
        };
        /* View 3 */
        { 225, 0, 239, 96 },
        0x00000000, 0x00000000, -1, 8500,
        {
            Picture { 2771 };
        };
        /* View 4 */
        { 222, 266, 242, 339 },
        0x00000000, 0x00000020, -1, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, -1, 0 };
            Text { "Previous" };
            Command { 'prvv' };
        };
        /* View 5 */
        { 222, 349, 242, 422 },
        0x00000000, 0x00000020, -1, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, -1, 0 };
            Text { "Next" };
            Command { 'nxtv' };
        };
    }
};

resource 'fvew' (2754, purgeable) {
    { 0, 0, 252, 432 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 252, 432 }, /* content bounds */
    0x0004, /* layout options */
    { 0, 0, 252, 432 }, /* view bounds */
    {
        /* View 1 */
        { 0, 0, 222, 432 },
        0x00000000, 0x00000000, -1, 8500,
        {
            Picture { 2781 };
        };
        /* View 2 */
        { 60, 25, 130, 96 },
        0x00000000, 0x00000000, -1, 8500,
        {
            Picture { 2763 };
        };
        /* View 3 */
        { 225, 0, 239, 96 },
        0x00000000, 0x00000000, -1, 8500,
        {
            Picture { 2772 };
        };
        /* View 4 */
        { 222, 266, 242, 339 },
        0x00000000, 0x00000020, -1, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, -1, 0 };
            Text { "Previous" };
            Command { 'prvv' };
        };
        /* View 5 */
        { 222, 349, 242, 422 },
        0x00000000, 0x00000020, -1, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, -1, 0 };
            Text { "Next" };
            Command { 'nxtv' };
        };
    }
};

resource 'fvew' (2755, purgeable) {
    { 0, 0, 252, 432 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 252, 432 }, /* content bounds */
    0x0004, /* layout options */
    { 0, 0, 252, 432 }, /* view bounds */
    {
        /* View 1 */
        { 0, 0, 222, 432 },
        0x00000000, 0x00000000, -1, 8500,
        {
            Picture { 2782 };
        };
        /* View 2 */
        { 60, 25, 138, 96 },
        0x00000000, 0x00000000, -1, 8500,
        {
            Picture { 2764 };
        };
        /* View 3 */
        { 225, 0, 239, 96 },
        0x00000000, 0x00000000, -1, 8500,
        {
            Picture { 2773 };
        };
        /* View 4 */
        { 222, 266, 242, 339 },
        0x00000000, 0x00000020, -1, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, -1, 0 };
            Text { "Previous" };
            Command { 'prvv' };
        };
    }
};

resource 'fvew' (2992, purgeable) {
    { 0, 0, 38, 0 }, /* outer bounds */
    0x20020000, 0x00004000, 0x00002002, /* root state */
    0, /* root ID */
    { 0, 0, 38, 0 }, /* content bounds */
    0x0008, /* layout options */
    { 62, 14, 250, 418 }, /* view bounds */
    {
        /* View 1 */
        { -3, -1, 0, 1 },
        0x21000000, 0x00000000, -1, 4750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { 2, 8, 14, 200 },
        0x000F0000, 0x00000000, 3055, 3250,
        {
            Words { { 65535, 65535, 65534, 0 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 2, -45, 14, 45 },
        0x080F8000, 0x00000000, 11551, 3250,
        {
            Words { { 65535, 65535, 1, 0 } }; /* constructor offset 0 */
        };
        /* View 4 */
        { 2, -104, 14, -4 },
        0x040F8000, 0x00000000, 11552, 3250,
        {
            Words { { 65535, 65535, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 5 */
        { -21, 4, -20, -4 },
        0x21080000, 0x00000000, -2, 4750,
        {
            Words { { 4 } }; /* constructor offset 0 */
        };
        /* View 6 */
        { -20, 4, -4, 20 },
        0x01AE0000, 0x00000000, 11332, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 7 */
        { -20, 4, -4, 20 },
        0x01AE8000, 0x00000000, 11330, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 8 */
        { -20, 4, -4, 20 },
        0x01AE8000, 0x00000000, 11331, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 9 */
        { -20, 4, -4, 20 },
        0x01AE8000, 0x00000000, 11333, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 10 */
        { -20, 4, -4, 20 },
        0x01AE8000, 0x00000000, 11334, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 11 */
        { -20, 4, -4, 20 },
        0x01AE8000, 0x00000000, 11335, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 12 */
        { -18, 36, -6, 186 },
        0x019F0000, 0x00000020, 3050, 22250,
        {
            Words { { 65535, 65535, 0, 4 } }; /* constructor offset 0 */
            Text { "Name" };
            Words { { 30313, 25975, 65534 } }; /* constructor offset 14 */
        };
        /* View 13 */
        { -18, 188, -6, 236 },
        0x41AF0000, 0x00000020, 20812, 22250,
        {
            Words { { 65535, 65535, 65535, 0 } }; /* constructor offset 0 */
            Text { "Size" };
            Words { { 30313, 25975, 65532 } }; /* constructor offset 14 */
        };
        /* View 14 */
        { -18, 244, -6, 350 },
        0x41AF0000, 0x00000020, 3051, 22250,
        {
            Words { { 65535, 65535, 0, 0 } }; /* constructor offset 0 */
            Text { "Kind" };
            Words { { 30313, 25975, 65531 } }; /* constructor offset 14 */
        };
        /* View 15 */
        { -18, 352, -6, 406 },
        0x41AF0000, 0x00000020, 3061, 22250,
        {
            Words { { 65535, 65535, 0, 0 } }; /* constructor offset 0 */
            Text { "Label" };
            Words { { 30313, 25975, 65529 } }; /* constructor offset 14 */
        };
        /* View 16 */
        { -18, 408, -6, 568 },
        0x41AF0000, 0x00000020, 20805, 22250,
        {
            Words { { 65535, 65535, 0, 0 } }; /* constructor offset 0 */
            Text { "Last Modified" };
            Words { { 30313, 25975, 65533 } }; /* constructor offset 22 */
        };
        /* View 17 */
        { -18, 570, -6, 630 },
        0x41AF0000, 0x00000020, 12802, 22250,
        {
            Words { { 65535, 65535, 0, 0 } }; /* constructor offset 0 */
            Text { "Version" };
            Words { { 30313, 25975, 65528 } }; /* constructor offset 16 */
        };
        /* View 18 */
        { -18, 632, -6, 2232 },
        0x41AF0000, 0x00000020, 20807, 22250,
        {
            Words { { 65535, 65535, 0, 0 } }; /* constructor offset 0 */
            Text { "Comments" };
            Words { { 30313, 25975, 65530 } }; /* constructor offset 18 */
        };
    }
};

resource 'fvew' (3000, purgeable, preload) {
    { 0, 0, 20, 0 }, /* outer bounds */
    0x20020000, 0x00004000, 0x00002002, /* root state */
    0, /* root ID */
    { 0, 0, 20, 0 }, /* content bounds */
    0x0008, /* layout options */
    { 62, 14, 250, 418 }, /* view bounds */
    {
        /* View 1 */
        { -3, -1, 0, 1 },
        0x21000000, 0x00000000, -1, 4750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { -10, 4, 6, 20 },
        0x02040000, 0x00000000, 11332, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { -10, 4, 6, 20 },
        0x02048000, 0x00000000, 11330, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 4 */
        { -10, 4, 6, 20 },
        0x02048000, 0x00000000, 11331, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 5 */
        { -10, 4, 6, 20 },
        0x02048000, 0x00000000, 11333, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 6 */
        { -10, 4, 6, 20 },
        0x02048000, 0x00000000, 11334, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 7 */
        { -10, 4, 6, 20 },
        0x02048000, 0x00000000, 11335, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 8 */
        { -8, 8, 4, 200 },
        0x020F8000, 0x00000000, 3055, 3250,
        {
            Words { { 65535, 65535, 65534, 0 } }; /* constructor offset 0 */
        };
        /* View 9 */
        { -8, -45, 4, 45 },
        0x0A0F8000, 0x00000000, 11551, 3250,
        {
            Words { { 65535, 65535, 1, 0 } }; /* constructor offset 0 */
        };
        /* View 10 */
        { -8, -104, 4, -4 },
        0x060F8000, 0x00000000, 11552, 3250,
        {
            Words { { 65535, 65535, 65535, 0 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (3001, purgeable, preload) {
    { 0, 0, 20, 0 }, /* outer bounds */
    0x20020000, 0x00004000, 0x00002002, /* root state */
    0, /* root ID */
    { 0, 0, 20, 0 }, /* content bounds */
    0x0008, /* layout options */
    { 62, 14, 250, 418 }, /* view bounds */
    {
        /* View 1 */
        { -3, -1, 0, 1 },
        0x21000000, 0x00000000, -1, 4750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { -10, 4, 6, 20 },
        0x02040000, 0x00000000, 11332, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { -10, 4, 6, 20 },
        0x02048000, 0x00000000, 11330, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 4 */
        { -10, 4, 6, 20 },
        0x02048000, 0x00000000, 11331, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 5 */
        { -10, 4, 6, 20 },
        0x02048000, 0x00000000, 11333, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 6 */
        { -10, 4, 6, 20 },
        0x02048000, 0x00000000, 11334, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 7 */
        { -10, 4, 6, 20 },
        0x02048000, 0x00000000, 11335, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 8 */
        { -8, 8, 4, 200 },
        0x020F8000, 0x00000000, 3055, 3250,
        {
            Words { { 65535, 65535, 65534, 0 } }; /* constructor offset 0 */
        };
        /* View 9 */
        { -8, -45, 4, 45 },
        0x0A0F8000, 0x00000000, 11551, 3250,
        {
            Words { { 65535, 65535, 1, 0 } }; /* constructor offset 0 */
        };
        /* View 10 */
        { -8, -104, 4, -4 },
        0x060F8000, 0x00000000, 11552, 3250,
        {
            Words { { 65535, 65535, 65535, 0 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (3002, purgeable, preload) {
    { 0, 0, 20, 0 }, /* outer bounds */
    0x20020000, 0x00004000, 0x00002002, /* root state */
    0, /* root ID */
    { 0, 0, 20, 0 }, /* content bounds */
    0x0008, /* layout options */
    { 62, 14, 250, 418 }, /* view bounds */
    {
        /* View 1 */
        { -3, -1, 0, 1 },
        0x21000000, 0x00000000, -1, 4750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { -10, 4, 6, 20 },
        0x02A40000, 0x00000000, 11332, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { -10, 4, 6, 20 },
        0x02A48000, 0x00000000, 11330, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 4 */
        { -10, 4, 6, 20 },
        0x02A48000, 0x00000000, 11331, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 5 */
        { -10, 4, 6, 20 },
        0x02A48000, 0x00000000, 11333, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 6 */
        { -10, 4, 6, 20 },
        0x02A48000, 0x00000000, 11334, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 7 */
        { -10, 4, 6, 20 },
        0x02A48000, 0x00000000, 11335, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 8 */
        { -8, 36, 4, 186 },
        0x029F8000, 0x00000020, 3050, 22250,
        {
            Words { { 65535, 65535, 0, 4 } }; /* constructor offset 0 */
            Text { "Name" };
            Words { { 30313, 25975, 65534 } }; /* constructor offset 14 */
        };
        /* View 9 */
        { -8, 188, 4, 236 },
        0x42AF0000, 0x00000020, 20812, 22250,
        {
            Words { { 65535, 65535, 65535, 0 } }; /* constructor offset 0 */
            Text { "Size" };
            Words { { 30313, 25975, 65532 } }; /* constructor offset 14 */
        };
        /* View 10 */
        { -8, 244, 4, 350 },
        0x42AF0000, 0x00000020, 3051, 22250,
        {
            Words { { 65535, 65535, 0, 0 } }; /* constructor offset 0 */
            Text { "Kind" };
            Words { { 30313, 25975, 65531 } }; /* constructor offset 14 */
        };
        /* View 11 */
        { -8, 352, 4, 406 },
        0x42AF0000, 0x00000020, 3061, 22250,
        {
            Words { { 65535, 65535, 0, 0 } }; /* constructor offset 0 */
            Text { "Label" };
            Words { { 30313, 25975, 65529 } }; /* constructor offset 14 */
        };
        /* View 12 */
        { -8, 408, 4, 568 },
        0x42AF0000, 0x00000020, 20805, 22250,
        {
            Words { { 65535, 65535, 0, 0 } }; /* constructor offset 0 */
            Text { "Last Modified" };
            Words { { 30313, 25975, 65533 } }; /* constructor offset 22 */
        };
    }
};

resource 'fvew' (3050, "name", purgeable) {
    { 0, 0, 0, 0 }, /* outer bounds */
    0x00000002, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 0, 0 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 29, 144, 51, 282 },
        0x00000000, 0x00000000, 11053, 5750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Words { { 0, 0, 11053, 11030 } }; /* constructor offset 8 */
        };
        /* View 2 */
        { 30, 288, 49, 434 },
        0x00000000, 0x00000001, 11050, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
        };
    }
};

resource 'fvew' (3051, "kind", purgeable) {
    { 0, 0, 0, 0 }, /* outer bounds */
    0x00000003, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 0, 0 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 29, 144, 51, 282 },
        0x00000000, 0x00000000, 11053, 5750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
            Words { { 0, 0, 11053, 11035 } }; /* constructor offset 8 */
        };
        /* View 2 */
        { 30, 288, 49, 415 },
        0x00000000, 0x00000001, 11050, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
        };
        /* View 3 */
        { 29, 416, 51, 434 },
        0x00000000, 0x00000000, 11067, 5750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
            Words { { 0, 0, 11067, 11036 } }; /* constructor offset 8 */
        };
    }
};

resource 'fvew' (3061, "label", purgeable) {
    { 0, 0, 0, 0 }, /* outer bounds */
    0x00000002, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 0, 0 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 29, 144, 51, 282 },
        0x00000000, 0x00000000, 11053, 5750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Words { { 0, 0, 11053, 11031 } }; /* constructor offset 8 */
        };
        /* View 2 */
        { 30, 288, 49, 434 },
        0x00000000, 0x00000000, 11054, 5750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
            Words { { 0, 0, 11054, 1276 } }; /* constructor offset 8 */
        };
    }
};

resource 'fvew' (3092, purgeable) {
    { 0, 0, 16, 587 }, /* outer bounds */
    0x80A30000, 0x0BEA5000, 0x00003008, /* root state */
    0, /* root ID */
    { 38, 0, -15, -15 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { -8, 0, 8, 16 },
        0x82A40000, 0x00000002, 3060, 6250,
        {
            Words { { 1 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { -8, 18, 8, 34 },
        0x82040000, 0x00000001, 0, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { -6, 36, 6, 186 },
        0x820F0000, 0x00000301, 3050, 8250,
        {
            Words { { 65534, 65535, 65534, 0 } }; /* constructor offset 0 */
        };
        /* View 4 */
        { -6, 188, 6, 236 },
        0xC2AF0000, 0x00000000, 20804, 3250,
        {
            Words { { 65535, 65535, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 5 */
        { -6, 244, 6, 350 },
        0xC2AF0000, 0x00000000, 3051, 3250,
        {
            Words { { 65535, 65535, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 6 */
        { -6, 352, 6, 406 },
        0xC2AF0000, 0x00000000, 3061, 3250,
        {
            Words { { 65535, 65535, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 7 */
        { -6, 408, 6, 568 },
        0xC2AF0000, 0x00000000, 20805, 3250,
        {
            Words { { 65535, 65535, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 8 */
        { -6, 570, 6, 630 },
        0xC2AF0000, 0x00000000, 12802, 3250,
        {
            Words { { 65535, 65535, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 9 */
        { -6, 632, 6, 2232 },
        0xC2AF0000, 0x00000000, 20807, 3250,
        {
            Words { { 65535, 65535, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 10 */
        { -8, 2234, 8, 2250 },
        0xC2A40000, 0x00000000, 20806, 6250,
        {
            Words { { 1 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (3100, purgeable, preload) {
    { -1, -8, 19, 88 }, /* outer bounds */
    0x80030000, 0x00001100, 0x00003008, /* root state */
    0, /* root ID */
    { 20, 0, -15, -15 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { -9, 0, 7, 16 },
        0x82000000, 0x00000001, 0, 6250,
        {
            Words { { 1 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { -7, 25, 9, 96 },
        0xA20A0000, 0x00000301, 3050, 8250,
        {
            Words { { 65534, 65535, 65534, 0 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (3101, purgeable, preload) {
    { -4, -16, 48, 48 }, /* outer bounds */
    0x80050000, 0x00009000, 0x00003008, /* root state */
    0, /* root ID */
    { 20, 0, -15, -15 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 0, -16, 32, 16 },
        0x88000000, 0x00000001, 0, 6250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { 32, 0, 48, 0 },
        0x88020000, 0x00000301, 3050, 8250,
        {
            Words { { 65534, 65535, 1, 0 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (3102, purgeable, preload) {
    { 0, 0, 16, 587 }, /* outer bounds */
    0x80A30000, 0x0BEA5000, 0x00003008, /* root state */
    0, /* root ID */
    { 20, 0, -15, -15 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 16, 587 }, /* view bounds */
    {
        /* View 1 */
        { -8, 0, 8, 16 },
        0x82A40000, 0x00000002, 3060, 6250,
        {
            Words { { 1 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { -8, 18, 8, 34 },
        0x82040000, 0x00000001, 0, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { -6, 36, 6, 186 },
        0x820F0000, 0x00000301, 3050, 8250,
        {
            Words { { 65534, 65535, 65534, 0 } }; /* constructor offset 0 */
        };
        /* View 4 */
        { -6, 188, 6, 236 },
        0xC2AF0000, 0x00000000, 20804, 3250,
        {
            Words { { 65535, 65535, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 5 */
        { -6, 244, 6, 350 },
        0xC2AF0000, 0x00000000, 3051, 3250,
        {
            Words { { 65535, 65535, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 6 */
        { -6, 352, 6, 406 },
        0xC2AF0000, 0x00000000, 3061, 3250,
        {
            Words { { 65535, 65535, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 7 */
        { -6, 408, 6, 568 },
        0xC2AF0000, 0x00000000, 20805, 3250,
        {
            Words { { 65535, 65535, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 8 */
        { -8, 570, 8, 586 },
        0xC2A40000, 0x00000000, 20806, 6250,
        {
            Words { { 1 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (8751, purgeable) {
    { 0, 0, 176, 280 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 176, 280 }, /* content bounds */
    0x0005, /* layout options */
    { 50, 50, 226, 330 }, /* view bounds */
    {
        { 7, 9, 23, 210 },
        0x00000000, 0x00000000, 8805, 3250,
        { Words { { 0, 12, 0, 0 } }; };
        { 7, 210, 23, 258 },
        0x00000000, 0x00000000, 8800, 3250,
        { Words { { 0, 12, 65535, 0 } }; };
        /* Retain the original status and current-file bindings. */
        { 90, 27, 104, 77 },
        0x00000000, 0x00000000, 8801, 3250,
        { Words { { 1, 9, 0, 0 } }; };
        { 90, 84, 104, 274 },
        0x00000000, 0x00000000, 8802, 3250,
        { Words { { 1, 9, 0, 0 } }; };
        { 43, 10, 54, 198 },
        0x00000000, 0x00000000, 8803, 5250,
        { Words { { 1, 21 } }; };
        { 39, 211, 59, 270 },
        0x00000000, 0x00000000, 3063, 21000,
        {
            TextStyle { 0, 12, 1, 0 };
            Text { "Stop" };
            Command { 'ucan' };
        };
        { 68, 20, 82, 110 },
        0x00000000, 0x00000000, 8830, 7750,
        { TextStyle { 1, 9, 0, 0 }; Text { "Time remaining:" }; };
        { 112, 41, 126, 77 },
        0x00000000, 0x00000000, 8832, 7750,
        { TextStyle { 1, 9, 0, 0 }; Text { "From:" }; };
        { 134, 55, 148, 77 },
        0x00000000, 0x00000000, 8833, 7750,
        { TextStyle { 1, 9, 0, 0 }; Text { "To:" }; };
        { 156, 9, 170, 81 },
        0x00000000, 0x00000000, 8834, 7750,
        { TextStyle { 1, 9, 0, 0 }; Text { "Bytes Copied:" }; };
    }
};

resource 'fvew' (9751, purgeable) {
    { 0, 0, 190, 240 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 190, 240 }, /* content bounds */
    0x0004, /* layout options */
    { 70, 50, 260, 290 }, /* view bounds */
    {
        /* View 1 */
        { 151, 0, 190, 110 },
        0x00000000, 0x00000000, 9802, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { 150, 0, 151, 239 },
        0x00000000, 0x00000000, 9803, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 20, 60, 140, 180 },
        0x00000000, 0x00000000, 9801, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 4 */
        { 156, 116, 184, 234 },
        0x00000000, 0x00000001, 9800, 7250,
        {
            Words { { 0, 12, 1, 0 } }; /* constructor offset 0 */
            Text { "Open Publisher" };
            Words { { 0, 9800 } }; /* constructor offset 24 */
        };
    }
};

resource 'fvew' (10001, purgeable) {
    { 0, 0, 84, 378 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 84, 378 }, /* content bounds */
    0x0008, /* layout options */
    { 0, 0, 156, 378 }, /* view bounds */
    {
        /* View 1 */
        { 8, 16, 40, 48 },
        0x00000000, 0x00000000, 10056, 6250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { 40, 52, 52, 185 },
        0x00000000, 0x00000000, 10054, 3250,
        {
            Words { { 1, 9, 0, 33 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 10, 191, 22, 287 },
        0x00000000, 0x00000000, 10053, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "System Software" };
        };
        /* View 4 */
        { 10, 287, 22, 362 },
        0x00000000, 0x00000000, 10053, 3250,
        {
            Words { { 1, 9, 0, 1 } }; /* constructor offset 0 */
        };
        /* View 5 */
        { 25, 191, 37, 362 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "\0xA9 Apple Computer, Inc. 1983-1995" };
        };
        /* View 6 */
        { 68, 16, 80, 127 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Total Memory:" };
        };
        /* View 7 */
        { 68, 127, 80, 175 },
        0x00000000, 0x00000000, 10050, 3250,
        {
            Words { { 1, 9, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 8 */
        { 68, 191, 80, 314 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Largest Unused Block:" };
        };
        /* View 9 */
        { 68, 314, 80, 362 },
        0x00000000, 0x00000000, 10052, 3250,
        {
            Words { { 1, 9, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 10 */
        { 83, 0, 84, 10000 },
        0x00000000, 0x00000000, -1, 4750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (10002, purgeable) {
    { 0, 0, 96, 378 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 96, 378 }, /* content bounds */
    0x0008, /* layout options */
    { 0, 0, 168, 378 }, /* view bounds */
    {
        /* View 1 */
        { 8, 16, 40, 48 },
        0x00000000, 0x00000000, 10056, 6250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { 40, 52, 52, 185 },
        0x00000000, 0x00000000, 10054, 3250,
        {
            Words { { 1, 9, 0, 33 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 10, 191, 22, 287 },
        0x00000000, 0x00000000, 10053, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "System Software" };
        };
        /* View 4 */
        { 10, 287, 22, 362 },
        0x00000000, 0x00000000, 10053, 3250,
        {
            Words { { 1, 9, 0, 1 } }; /* constructor offset 0 */
        };
        /* View 5 */
        { 25, 191, 37, 362 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "\0xA9 Apple Computer, Inc. 1983-1995" };
        };
        /* View 6 */
        { 68, 16, 80, 127 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Built-in Memory:" };
        };
        /* View 7 */
        { 68, 127, 80, 175 },
        0x00000000, 0x00000000, 10050, 3250,
        {
            Words { { 1, 9, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 8 */
        { 80, 16, 92, 127 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Total Memory:" };
        };
        /* View 9 */
        { 80, 127, 92, 175 },
        0x00000000, 0x00000000, 10051, 3250,
        {
            Words { { 1, 9, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 10 */
        { 68, 191, 80, 314 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Largest Unused Block:" };
        };
        /* View 11 */
        { 68, 314, 80, 362 },
        0x00000000, 0x00000000, 10052, 3250,
        {
            Words { { 1, 9, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 12 */
        { 80, 191, 92, 376 },
        0x00000000, 0x00000000, 10055, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 13 */
        { 95, 0, 96, 10000 },
        0x00000000, 0x00000000, -1, 4750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (10003, purgeable) {
    { 0, 0, 84, 378 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 84, 378 }, /* content bounds */
    0x0008, /* layout options */
    { 0, 0, 156, 378 }, /* view bounds */
    {
        /* View 1 */
        { 8, 16, 40, 48 },
        0x00000000, 0x00000000, 10056, 6250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { 40, 52, 52, 185 },
        0x00000000, 0x00000000, 10054, 3250,
        {
            Words { { 1, 9, 0, 33 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 10, 191, 22, 287 },
        0x00000000, 0x00000000, 10053, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "System Software" };
        };
        /* View 4 */
        { 10, 287, 22, 362 },
        0x00000000, 0x00000000, 10053, 3250,
        {
            Words { { 1, 9, 0, 1 } }; /* constructor offset 0 */
        };
        /* View 5 */
        { 25, 191, 37, 362 },
        0x00000000, 0x00000000, 10060, 3250,
        {
            Words { { 1, 9, 0, 1 } }; /* constructor offset 0 */
        };
        /* View 6 */
        { 40, 191, 52, 362 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "\0xA9 Apple Computer, Inc. 1983-1995" };
        };
        /* View 7 */
        { 68, 16, 80, 127 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Total Memory:" };
        };
        /* View 8 */
        { 68, 127, 80, 175 },
        0x00000000, 0x00000000, 10050, 3250,
        {
            Words { { 1, 9, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 9 */
        { 68, 191, 80, 314 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Largest Unused Block:" };
        };
        /* View 10 */
        { 68, 314, 80, 362 },
        0x00000000, 0x00000000, 10052, 3250,
        {
            Words { { 1, 9, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 11 */
        { 83, 0, 84, 10000 },
        0x00000000, 0x00000000, -1, 4750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (10004, purgeable) {
    { 0, 0, 96, 378 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 96, 378 }, /* content bounds */
    0x0008, /* layout options */
    { 0, 0, 168, 378 }, /* view bounds */
    {
        /* View 1 */
        { 8, 16, 40, 48 },
        0x00000000, 0x00000000, 10056, 6250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { 40, 52, 52, 185 },
        0x00000000, 0x00000000, 10054, 3250,
        {
            Words { { 1, 9, 0, 33 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 10, 191, 22, 287 },
        0x00000000, 0x00000000, 10053, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "System Software" };
        };
        /* View 4 */
        { 10, 287, 22, 362 },
        0x00000000, 0x00000000, 10053, 3250,
        {
            Words { { 1, 9, 0, 1 } }; /* constructor offset 0 */
        };
        /* View 5 */
        { 25, 191, 37, 362 },
        0x00000000, 0x00000000, 10060, 3250,
        {
            Words { { 1, 9, 0, 1 } }; /* constructor offset 0 */
        };
        /* View 6 */
        { 40, 191, 52, 362 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "\0xA9 Apple Computer, Inc. 1983-1995" };
        };
        /* View 7 */
        { 68, 16, 80, 127 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Built-in Memory:" };
        };
        /* View 8 */
        { 68, 127, 80, 175 },
        0x00000000, 0x00000000, 10050, 3250,
        {
            Words { { 1, 9, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 9 */
        { 80, 16, 92, 127 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Total Memory:" };
        };
        /* View 10 */
        { 80, 127, 92, 175 },
        0x00000000, 0x00000000, 10051, 3250,
        {
            Words { { 1, 9, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 11 */
        { 68, 191, 80, 314 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Largest Unused Block:" };
        };
        /* View 12 */
        { 68, 314, 80, 362 },
        0x00000000, 0x00000000, 10052, 3250,
        {
            Words { { 1, 9, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 13 */
        { 80, 191, 92, 376 },
        0x00000000, 0x00000000, 10055, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 14 */
        { 95, 0, 96, 10000 },
        0x00000000, 0x00000000, -1, 4750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (10101, purgeable) {
    { 0, 0, 18, 362 }, /* outer bounds */
    0x80000000, 0x0BEA4600, 0x00003000, /* root state */
    0, /* root ID */
    { 84, 0, 0, -15 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 1, 16, 17, 32 },
        0x80000000, 0x00000000, 0, 6250,
        {
            Words { { 1 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { 3, 40, 15, 127 },
        0x80000000, 0x00000000, 3050, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 3, 127, 15, 175 },
        0x80000000, 0x00000000, 10058, 3250,
        {
            Words { { 1, 9, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 4 */
        { 3, 191, 14, 358 },
        0x80000000, 0x00000000, 10058, 5250,
        {
            Words { { 1, 21 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (10102, purgeable) {
    { 0, 0, 18, 362 }, /* outer bounds */
    0x80000000, 0x0BEA4600, 0x00003000, /* root state */
    0, /* root ID */
    { 96, 0, 0, -15 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 1, 16, 17, 32 },
        0x80000000, 0x00000000, 0, 6250,
        {
            Words { { 1 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { 3, 40, 15, 127 },
        0x80000000, 0x00000000, 3050, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 3, 127, 15, 175 },
        0x80000000, 0x00000000, 10058, 3250,
        {
            Words { { 1, 9, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 4 */
        { 3, 191, 14, 358 },
        0x80000000, 0x00000000, 10058, 5250,
        {
            Words { { 1, 21 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (10251, purgeable, preload) {
    { 0, 0, 0, 0 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 0, 0 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
    }
};

resource 'fvew' (10501, purgeable) {
    { 0, 0, 20, 0 }, /* outer bounds */
    0x20020000, 0x00000000, 0x00002002, /* root state */
    0, /* root ID */
    { 0, 0, 20, 0 }, /* content bounds */
    0x0008, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 4, 4, 16, 98 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Clipboard contents: " };
        };
        /* View 2 */
        { 4, 98, 16, -1 },
        0x20020000, 0x00000000, 10550, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { -3, -1, 0, 1 },
        0x21000000, 0x00000000, -1, 4750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (10502, purgeable) {
    { 0, 0, 60, 107 }, /* outer bounds */
    0x20020000, 0x00000000, 0x00002002, /* root state */
    0, /* root ID */
    { 0, 0, 60, 107 }, /* content bounds */
    0x0008, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 4, 4, 16, 98 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Clipboard contents: " };
        };
        /* View 2 */
        { 4, 98, 16, -1 },
        0x20020000, 0x00000000, 10550, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 17, -1, 20, 1 },
        0x20000000, 0x00000000, -1, 4750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 4 */
        { 30, 10, 50, 97 },
        0x00000000, 0x00000001, 10552, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Text { "Play Sound" };
            Words { { 0, 10552 } }; /* constructor offset 20 */
        };
    }
};

resource 'fvew' (10601, purgeable) {
    { 0, 0, 1, 1 }, /* outer bounds */
    0x80000000, 0x0BEA0200, 0x00003008, /* root state */
    0, /* root ID */
    { 20, 0, -15, -15 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 0, 0, 1, 1 },
        0x80000000, 0x00000001, -1, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (10602, purgeable) {
    { 0, 0, 0, 0 }, /* outer bounds */
    0x80000000, 0x00000200, 0x00003008, /* root state */
    0, /* root ID */
    { 20, 0, -15, -15 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
    }
};

resource 'fvew' (11011, purgeable) {
    { 0, 0, 65, 314 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 65, 314 }, /* content bounds */
    0x0005, /* layout options */
    { 50, 50, 115, 364 }, /* view bounds */
    {
        /* View 1 */
        { 7, 9, 23, 304 },
        0x00000000, 0x00000000, 11071, 3250,
        {
            Words { { 0, 12, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { 39, 9, 50, 197 },
        0x00000000, 0x00000000, 11072, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 35, 236, 55, 304 },
        0x00000000, 0x00000000, 3063, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Text { "Stop" };
            Command { 'ucan' };
        };
    }
};

resource 'fvew' (11012, purgeable) {
    { 0, 0, 65, 314 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 65, 314 }, /* content bounds */
    0x0005, /* layout options */
    { 50, 50, 115, 364 }, /* view bounds */
    {
        /* View 1 */
        { 7, 9, 23, 239 },
        0x00000000, 0x00000000, 11071, 3250,
        {
            Words { { 0, 12, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { 7, 254, 23, 304 },
        0x00000000, 0x00000000, 11070, 3250,
        {
            Words { { 0, 12, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 39, 9, 50, 197 },
        0x00000000, 0x00000000, 11072, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 4 */
        { 35, 236, 55, 304 },
        0x00000000, 0x00000000, 3063, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Text { "Stop" };
            Command { 'ucan' };
        };
    }
};

resource 'fvew' (11013, purgeable) {
    { 0, 0, 65, 314 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 65, 314 }, /* content bounds */
    0x0005, /* layout options */
    { 50, 50, 115, 364 }, /* view bounds */
    {
        /* View 1 */
        { 7, 9, 23, 239 },
        0x00000000, 0x00000000, 11071, 3250,
        {
            Words { { 0, 12, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { 7, 254, 23, 304 },
        0x00000000, 0x00000000, 11070, 3250,
        {
            Words { { 0, 12, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 39, 9, 50, 197 },
        0x00000000, 0x00000000, 11061, 5250,
        {
            Words { { 1, 21 } }; /* constructor offset 0 */
        };
        /* View 4 */
        { 35, 236, 55, 304 },
        0x00000000, 0x00000000, 3063, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Text { "Stop" };
            Command { 'ucan' };
        };
    }
};

resource 'fvew' (11014, purgeable) {
    { 0, 0, 80, 320 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 80, 320 }, /* content bounds */
    0x0005, /* layout options */
    { 0, 0, 80, 320 }, /* view bounds */
    {
        /* View 1 */
        { 13, 9, 29, 49 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
            Text { "Find:" };
        };
        /* View 2 */
        { 10, 49, 30, 310 },
        0x00000000, 0x00000001, 11050, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
        };
        /* View 3 */
        { 50, 10, 70, 118 },
        0x00000000, 0x00000001, 11057, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Text { "More Choices" };
            Words { { 0, 11057 } }; /* constructor offset 22 */
        };
        /* View 4 */
        { 50, 161, 70, 229 },
        0x00000000, 0x00000001, 3063, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Text { "Cancel" };
            Command { 'ucan' };
        };
        /* View 5 */
        { 46, 238, 74, 314 },
        0x00000000, 0x00000001, 3064, 7250,
        {
            Words { { 0, 12, 1, 0 } }; /* constructor offset 0 */
            Text { "Find" };
            Command { 'ok  ' };
        };
    }
};

resource 'fvew' (11015, purgeable) {
    { 0, 0, 159, 444 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 159, 444 }, /* content bounds */
    0x0005, /* layout options */
    { 0, 0, 159, 444 }, /* view bounds */
    {
        /* View 1 */
        { 7, 9, 23, 434 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
            Text { "Find and select items whose" };
        };
        /* View 2 */
        { 29, 10, 51, 138 },
        0x00000000, 0x00000000, 11052, 5750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
            Words { { 0, 0, 11052, 11052 } }; /* constructor offset 8 */
        };
        /* View 3 */
        { 29, 144, 51, 282 },
        0x00000000, 0x00000000, 11053, 5750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Words { { 0, 0, 11053, 11030 } }; /* constructor offset 8 */
        };
        /* View 4 */
        { 30, 288, 49, 434 },
        0x00000000, 0x00000001, 11050, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
        };
        /* View 5 */
        { 67, 11, 68, 434 },
        0x00000000, 0x00000000, -1, 4750,
        {
            Words { { 4 } }; /* constructor offset 0 */
        };
        /* View 6 */
        { 83, 33, 99, 83 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
            Text { "Search" };
        };
        /* View 7 */
        { 81, 83, 103, 241 },
        0x00000000, 0x00000000, 11056, 5750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
            Words { { 0, 0, 11056, 11056 } }; /* constructor offset 8 */
        };
        /* View 8 */
        { 83, 314, 99, 402 },
        0x00000000, 0x00000000, 11055, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
            Text { "all at once" };
            Words { { 0, 11055 } }; /* constructor offset 20 */
        };
        /* View 9 */
        { 115, 11, 116, 434 },
        0x00000000, 0x00000000, -1, 4750,
        {
            Words { { 4 } }; /* constructor offset 0 */
        };
        /* View 10 */
        { 129, 10, 149, 118 },
        0x00000000, 0x00000001, 11058, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Text { "Fewer Choices" };
            Words { { 0, 11058 } }; /* constructor offset 22 */
        };
        /* View 11 */
        { 129, 285, 149, 353 },
        0x00000000, 0x00000001, 3063, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Text { "Cancel" };
            Command { 'ucan' };
        };
        /* View 12 */
        { 125, 362, 153, 438 },
        0x00000000, 0x00000001, 3064, 7250,
        {
            Words { { 0, 12, 1, 0 } }; /* constructor offset 0 */
            Text { "Find" };
            Command { 'ok  ' };
        };
    }
};

resource 'fvew' (11261, purgeable) {
    { 0, 0, 217, 242 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 217, 242 }, /* content bounds */
    0x0000, /* layout options */
    { 36, 0, 253, 242 }, /* view bounds */
    {
        /* View 1 */
        { 81, 8, 93, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Where:" };
        };
        /* View 2 */
        { 45, 8, 57, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Kind:" };
        };
        /* View 3 */
        { 57, 8, 69, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Size:" };
        };
        /* View 4 */
        { 117, 8, 129, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Created:" };
        };
        /* View 5 */
        { 129, 8, 141, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Modified:" };
        };
        /* View 6 */
        { 141, 8, 153, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Comments:" };
        };
        /* View 7 */
        { 16, 71, 28, 234 },
        0x00000000, 0x00000000, 20816, 22250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 20816, 0 } }; /* constructor offset 8 */
        };
        /* View 8 */
        { 81, 71, 117, 234 },
        0x00000000, 0x00000010, 3056, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 9 */
        { 45, 71, 57, 234 },
        0x00000000, 0x00000000, 3051, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 10 */
        { 57, 71, 81, 234 },
        0x00000000, 0x00000010, 3054, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 11 */
        { 117, 71, 129, 234 },
        0x00000000, 0x00000000, 20803, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 12 */
        { 129, 71, 141, 234 },
        0x00000000, 0x00000000, 20805, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 13 */
        { 155, 8, 209, 234 },
        0x00000000, 0x00000011, 20807, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 14 */
        { 4, 27, 42, 65 },
        0x00000000, 0x00000000, 0, 9250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (11263, purgeable) {
    { 0, 0, 400, 400 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 400, 400 }, /* content bounds */
    0x0000, /* layout options */
    { 42, 6, 264, 310 }, /* view bounds */
    {
        /* View 1 */
        { 5, 8, 37, 40 },
        0x00000000, 0x00000000, 0, 6250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { 7, 45, 19, 110 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Where:" };
        };
        /* View 3 */
        { 7, 126, 31, 300 },
        0x00000000, 0x00000010, 3056, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 4 */
        { 73, 8, 74, 294 },
        0x00000000, 0x00000000, -1, 4750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 5 */
        { 113, 9, 125, 80 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Owner:" };
        };
        /* View 6 */
        { 135, 9, 147, 80 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "User/Group:" };
        };
        /* View 7 */
        { 78, 176, 90, 216 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 1, 0 };
            Text { "See" };
        };
        /* View 8 */
        { 90, 176, 102, 216 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 1, 0 };
            Text { "Folders" };
        };
        /* View 9 */
        { 78, 215, 90, 255 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 1, 0 };
            Text { "See" };
        };
        /* View 10 */
        { 90, 215, 102, 255 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 1, 0 };
            Text { "Files" };
        };
        /* View 11 */
        { 78, 254, 90, 294 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 1, 0 };
            Text { "Make" };
        };
        /* View 12 */
        { 90, 254, 102, 294 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 1, 0 };
            Text { "Changes" };
        };
        /* View 13 */
        { 157, 95, 169, 180 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Everyone" };
        };
        /* View 14 */
        { 111, 188, 127, 204 },
        0x00000000, 0x00000000, 11310, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 11310 } }; /* constructor offset 8 */
        };
        /* View 15 */
        { 113, 190, 125, 202 },
        0x00000000, 0x00000000, 11310, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 16 */
        { 111, 227, 127, 243 },
        0x00000000, 0x00000000, 11311, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 11311 } }; /* constructor offset 8 */
        };
        /* View 17 */
        { 113, 229, 125, 241 },
        0x00000000, 0x00000000, 11311, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 18 */
        { 111, 266, 127, 282 },
        0x00000000, 0x00000000, 11312, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 11312 } }; /* constructor offset 8 */
        };
        /* View 19 */
        { 113, 268, 125, 280 },
        0x00000000, 0x00000000, 11312, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 20 */
        { 133, 188, 149, 204 },
        0x00000000, 0x00000000, 11313, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 11313 } }; /* constructor offset 8 */
        };
        /* View 21 */
        { 135, 190, 147, 202 },
        0x00000000, 0x00000000, 11313, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 22 */
        { 133, 227, 149, 243 },
        0x00000000, 0x00000000, 11314, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 11314 } }; /* constructor offset 8 */
        };
        /* View 23 */
        { 135, 229, 147, 241 },
        0x00000000, 0x00000000, 11314, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 24 */
        { 133, 266, 149, 282 },
        0x00000000, 0x00000000, 11315, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 11315 } }; /* constructor offset 8 */
        };
        /* View 25 */
        { 135, 268, 147, 280 },
        0x00000000, 0x00000000, 11315, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 26 */
        { 155, 188, 171, 204 },
        0x00000000, 0x00000000, 11316, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 11316 } }; /* constructor offset 8 */
        };
        /* View 27 */
        { 157, 190, 169, 202 },
        0x00000000, 0x00000000, 11316, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 28 */
        { 155, 227, 171, 243 },
        0x00000000, 0x00000000, 11317, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 11317 } }; /* constructor offset 8 */
        };
        /* View 29 */
        { 157, 229, 169, 241 },
        0x00000000, 0x00000000, 11317, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 30 */
        { 155, 266, 171, 282 },
        0x00000000, 0x00000000, 11318, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 11318 } }; /* constructor offset 8 */
        };
        /* View 31 */
        { 157, 268, 169, 280 },
        0x00000000, 0x00000000, 11318, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 32 */
        { 175, 8, 176, 294 },
        0x00000000, 0x00000000, -1, 4750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 33 */
        { 180, 10, 196, 200 },
        0x00000000, 0x00000000, 11328, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 11328 } }; /* constructor offset 8 */
        };
        /* View 34 */
        { 182, 12, 194, 24 },
        0x00000000, 0x00000000, 11328, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 35 */
        { 182, 29, 194, 260 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Make all currently enclosed folders like this one" };
        };
    }
};

resource 'fvew' (11280, purgeable) {
    { 42, 0, 400, 400 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 0, 0 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 33, 45, 45, 124 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Connected As:" };
        };
        /* View 2 */
        { 33, 126, 45, 300 },
        0x00000000, 0x00000010, 11550, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 47, 45, 59, 110 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Privileges:" };
        };
        /* View 4 */
        { 47, 126, 71, 300 },
        0x00000000, 0x00000010, 11309, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 5 */
        { 111, 84, 127, 180 },
        0x00000000, 0x00000001, 11301, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 6 */
        { 133, 84, 149, 180 },
        0x00000000, 0x00000001, 11302, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
    }
};

resource 'fvew' (11281, purgeable) {
    { 42, 0, 400, 400 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 0, 0 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 52, 10, 68, 230 },
        0x00000000, 0x00000000, 11337, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Share this item and its contents" };
            Words { { 0, 11337 } }; /* constructor offset 42 */
        };
    }
};

resource 'fvew' (11282, purgeable) {
    { 42, 0, 400, 400 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 0, 0 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 47, 45, 59, 110 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Inside:" };
        };
        /* View 2 */
        { 47, 126, 71, 300 },
        0x00000000, 0x00000010, 11338, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (11283, purgeable) {
    { 42, 0, 400, 400 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 0, 0 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 200, 10, 216, 210 },
        0x00000000, 0x00000000, 11340, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 11340 } }; /* constructor offset 8 */
        };
        /* View 2 */
        { 202, 12, 214, 24 },
        0x00000000, 0x00000000, 11340, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 202, 29, 214, 200 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Can\0xD5t be moved, renamed or deleted" };
        };
    }
};

resource 'fvew' (11284, purgeable) {
    { 42, 0, 400, 400 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 0, 0 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 110, 84, 128, 180 },
        0x00000000, 0x00000000, 11301, 5750,
        {
            /* font, size, style, justification */
            TextStyle { 3, 9, 0, 0 };
            Words { { 0, 0, 11301, 11301 } }; /* constructor offset 8 */
        };
        /* View 2 */
        { 132, 84, 150, 180 },
        0x00000000, 0x00000000, 11302, 5750,
        {
            /* font, size, style, justification */
            TextStyle { 3, 9, 0, 0 };
            Words { { 0, 0, 11302, 11302 } }; /* constructor offset 8 */
        };
    }
};

resource 'fvew' (11285, purgeable) {
    { 42, 0, 400, 400 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 0, 0 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 77, 10, 93, 170 },
        0x00000000, 0x00000000, 11339, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 11339 } }; /* constructor offset 8 */
        };
        /* View 2 */
        { 79, 12, 91, 24 },
        0x00000000, 0x00000000, 11339, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 79, 29, 91, 200 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Same as enclosing folder" };
        };
    }
};

resource 'fvew' (11511, purgeable) {
    { 0, 0, 217, 242 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 217, 242 }, /* content bounds */
    0x0000, /* layout options */
    { 36, 0, 253, 242 }, /* view bounds */
    {
        /* View 1 */
        { 81, 8, 93, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Where:" };
        };
        /* View 2 */
        { 45, 8, 57, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Kind:" };
        };
        /* View 3 */
        { 57, 8, 69, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Size:" };
        };
        /* View 4 */
        { 117, 8, 129, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Created:" };
        };
        /* View 5 */
        { 129, 8, 141, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Modified:" };
        };
        /* View 6 */
        { 141, 8, 153, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Comments:" };
        };
        /* View 7 */
        { 24, 71, 36, 234 },
        0x00000000, 0x00000000, 20816, 22250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 20816, 0 } }; /* constructor offset 8 */
        };
        /* View 8 */
        { 81, 71, 117, 234 },
        0x00000000, 0x00000010, 3056, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 9 */
        { 45, 71, 57, 234 },
        0x00000000, 0x00000000, 3051, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 10 */
        { 57, 71, 81, 234 },
        0x00000000, 0x00000010, 3054, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 11 */
        { 117, 71, 129, 234 },
        0x00000000, 0x00000000, 20803, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 12 */
        { 129, 71, 141, 234 },
        0x00000000, 0x00000000, 20805, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 13 */
        { 155, 8, 209, 234 },
        0x00000000, 0x00000011, 20807, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 14 */
        { 4, 27, 42, 65 },
        0x00000000, 0x00000000, 0, 9250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (11514, purgeable, preload) {
    { 0, 0, 60, 300 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 60, 300 }, /* content bounds */
    0x0005, /* layout options */
    { 0, 0, 60, 300 }, /* view bounds */
    {
        /* View 1 */
        { 7, 9, 23, 290 },
        0x00000000, 0x00000000, 11562, 3250,
        {
            Words { { 0, 12, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { 39, 10, 50, 218 },
        0x00000000, 0x00000000, 11560, 5250,
        {
            Words { { 1, 21 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 35, 231, 55, 290 },
        0x00000000, 0x00000000, 3063, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Text { "Stop" };
            Command { 'ucan' };
        };
    }
};

resource 'fvew' (11761, purgeable) {
    { 0, 0, 173, 242 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 173, 242 }, /* content bounds */
    0x0000, /* layout options */
    { 36, 0, 209, 242 }, /* view bounds */
    {
        /* View 1 */
        { 45, 8, 57, 68 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Where:" };
        };
        /* View 2 */
        { 69, 8, 81, 68 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Contents:" };
        };
        /* View 3 */
        { 129, 8, 141, 68 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Modified:" };
        };
        /* View 4 */
        { 16, 70, 28, 234 },
        0x00000000, 0x00000000, 3050, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 5 */
        { 45, 70, 69, 234 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "On the desktop" };
        };
        /* View 6 */
        { 69, 70, 129, 234 },
        0x00000000, 0x00000010, 11800, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 7 */
        { 129, 70, 141, 234 },
        0x00000000, 0x00000000, 20805, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 8 */
        { 7, 30, 39, 62 },
        0x00000000, 0x00000000, 0, 6250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 9 */
        { 153, 15, 165, 234 },
        0x00000000, 0x00000000, 11805, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Warn before emptying" };
            Words { { 0, 11805 } }; /* constructor offset 30 */
        };
    }
};

resource 'fvew' (11764, purgeable, preload) {
    { 0, 0, 64, 285 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 64, 285 }, /* content bounds */
    0x0005, /* layout options */
    { 50, 50, 114, 335 }, /* view bounds */
    {
        /* View 1 */
        { 7, 9, 23, 219 },
        0x00000000, 0x00000000, 11804, 3250,
        {
            Words { { 0, 12, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { 7, 219, 23, 267 },
        0x00000000, 0x00000000, 11803, 3250,
        {
            Words { { 0, 12, 65535, 0 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 43, 10, 54, 203 },
        0x00000000, 0x00000000, 11802, 5250,
        {
            Words { { 1, 21 } }; /* constructor offset 0 */
        };
        /* View 4 */
        { 39, 216, 59, 275 },
        0x00000000, 0x00000000, 3063, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Text { "Stop" };
            Command { 'ucan' };
        };
    }
};

resource 'fvew' (12761, purgeable) {
    { 0, 0, 261, 242 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 261, 242 }, /* content bounds */
    0x0000, /* layout options */
    { 36, 0, 297, 242 }, /* view bounds */
    {
        /* View 1 */
        { 81, 8, 93, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Where:" };
        };
        /* View 2 */
        { 45, 8, 57, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Kind:" };
        };
        /* View 3 */
        { 57, 8, 69, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Size:" };
        };
        /* View 4 */
        { 117, 8, 129, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Created:" };
        };
        /* View 5 */
        { 129, 8, 141, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Modified:" };
        };
        /* View 6 */
        { 141, 8, 153, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Version:" };
        };
        /* View 7 */
        { 165, 8, 177, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Comments:" };
        };
        /* View 8 */
        { 16, 71, 28, 234 },
        0x00000000, 0x00000000, 20816, 22250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 20816, 0 } }; /* constructor offset 8 */
        };
        /* View 9 */
        { 28, 71, 40, 234 },
        0x00000000, 0x00000000, 12803, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 10 */
        { 81, 71, 117, 234 },
        0x00000000, 0x00000010, 3056, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 11 */
        { 45, 71, 57, 234 },
        0x00000000, 0x00000000, 3051, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 12 */
        { 57, 71, 81, 234 },
        0x00000000, 0x00000010, 3054, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 13 */
        { 117, 71, 129, 234 },
        0x00000000, 0x00000000, 20803, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 14 */
        { 129, 71, 141, 234 },
        0x00000000, 0x00000000, 20805, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 15 */
        { 141, 71, 165, 234 },
        0x00000000, 0x00000010, 12801, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 16 */
        { 179, 8, 233, 234 },
        0x00000000, 0x00000011, 20807, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 17 */
        { 4, 27, 42, 65 },
        0x00000000, 0x00000000, 0, 9250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 18 */
        { 241, 6, 253, 234 },
        0x00000000, 0x00000000, 20806, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Locked" };
            Words { { 0, 20806 } }; /* constructor offset 16 */
        };
    }
};

resource 'fvew' (12802, "version", purgeable) {
    { 0, 0, 0, 0 }, /* outer bounds */
    0x00000002, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 0, 0 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 29, 144, 51, 282 },
        0x00000000, 0x00000000, 11053, 5750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Words { { 0, 0, 11053, 11039 } }; /* constructor offset 8 */
        };
        /* View 2 */
        { 30, 288, 49, 434 },
        0x00000000, 0x00000001, 11050, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
        };
    }
};

resource 'fvew' (12805, "size", purgeable) {
    { 0, 0, 0, 0 }, /* outer bounds */
    0x00000003, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 0, 0 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 29, 144, 51, 282 },
        0x00000000, 0x00000000, 11053, 5750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Words { { 0, 0, 11053, 11037 } }; /* constructor offset 8 */
        };
        /* View 2 */
        { 30, 288, 49, 415 },
        0x00000000, 0x00000009, 11050, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
        };
        /* View 3 */
        { 31, 416, 53, 434 },
        0x00000000, 0x00000000, 11050, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
            Words { { 331 } }; /* constructor offset 8 */
        };
    }
};

resource 'fvew' (13761, purgeable) {
    { 0, 0, 149, 242 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 149, 242 }, /* content bounds */
    0x0000, /* layout options */
    { 36, 0, 185, 242 }, /* view bounds */
    {
        /* View 1 */
        { 81, 8, 93, 68 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Where:" };
        };
        /* View 2 */
        { 45, 8, 57, 68 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Kind:" };
        };
        /* View 3 */
        { 57, 8, 69, 68 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Size:" };
        };
        /* View 4 */
        { 117, 8, 129, 68 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Created:" };
        };
        /* View 5 */
        { 129, 8, 141, 68 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Modified:" };
        };
        /* View 6 */
        { 16, 70, 28, 234 },
        0x00000000, 0x00000000, 3050, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 7 */
        { 81, 70, 117, 234 },
        0x00000000, 0x00000010, 3056, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 8 */
        { 45, 70, 57, 234 },
        0x00000000, 0x00000000, 3051, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 9 */
        { 57, 70, 81, 234 },
        0x00000000, 0x00000010, 3054, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 10 */
        { 117, 70, 129, 234 },
        0x00000000, 0x00000000, 20803, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 11 */
        { 129, 70, 141, 234 },
        0x00000000, 0x00000000, 20805, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 12 */
        { 4, 27, 42, 65 },
        0x00000000, 0x00000000, 0, 9250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (14501, purgeable) {
    { 0, 0, 203, 242 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 203, 242 }, /* content bounds */
    0x0000, /* layout options */
    { 38, 0, 314, 242 }, /* view bounds */
    {
        /* View 1 */
        { 8, 8, 171, 234 },
        0x00000000, 0x00000000, -1, 4750,
        {
            Words { { 4 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { 2, 16, 14, 51 },
        0x00000000, 0x00000010, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Sample" };
        };
        /* View 3 */
        { 16, 12, 170, 230 },
        0x00000000, 0x00000000, -1, 6750,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 4 */
        { 181, 8, 193, 234 },
        0x00000000, 0x00000010, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "To use this font, drag it onto your System Folder icon." };
        };
    }
};

resource 'fvew' (14506, purgeable) {
    { 0, 0, 276, 242 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 276, 242 }, /* content bounds */
    0x0000, /* layout options */
    { 38, 0, 314, 242 }, /* view bounds */
    {
        /* View 1 */
        { 8, 8, 48, 234 },
        0x00000000, 0x00000000, -1, 4750,
        {
            Words { { 4 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { 2, 16, 14, 50 },
        0x00000000, 0x00000010, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "9 point" };
        };
        /* View 3 */
        { 16, 12, 47, 230 },
        0x00000000, 0x00000000, -1, 6750,
        {
            Words { { 9 } }; /* constructor offset 0 */
        };
        /* View 4 */
        { 56, 8, 116, 234 },
        0x00000000, 0x00000000, -1, 4750,
        {
            Words { { 4 } }; /* constructor offset 0 */
        };
        /* View 5 */
        { 50, 16, 62, 55 },
        0x00000000, 0x00000010, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "12 point" };
        };
        /* View 6 */
        { 64, 12, 115, 230 },
        0x00000000, 0x00000000, -1, 6750,
        {
            Words { { 12 } }; /* constructor offset 0 */
        };
        /* View 7 */
        { 124, 8, 244, 234 },
        0x00000000, 0x00000000, -1, 4750,
        {
            Words { { 4 } }; /* constructor offset 0 */
        };
        /* View 8 */
        { 118, 16, 130, 55 },
        0x00000000, 0x00000010, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "18 point" };
        };
        /* View 9 */
        { 132, 12, 243, 230 },
        0x00000000, 0x00000000, -1, 6750,
        {
            Words { { 18 } }; /* constructor offset 0 */
        };
        /* View 10 */
        { 254, 8, 266, 234 },
        0x00000000, 0x00000010, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "To use this font, drag it onto your System Folder icon." };
        };
    }
};

resource 'fvew' (17011, purgeable) {
    { 0, 0, 261, 242 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 261, 242 }, /* content bounds */
    0x0000, /* layout options */
    { 36, 0, 297, 242 }, /* view bounds */
    {
        /* View 1 */
        { 81, 8, 93, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Where:" };
        };
        /* View 2 */
        { 45, 8, 57, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Kind:" };
        };
        /* View 3 */
        { 57, 8, 69, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Size:" };
        };
        /* View 4 */
        { 117, 8, 129, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Created:" };
        };
        /* View 5 */
        { 129, 8, 141, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Modified:" };
        };
        /* View 6 */
        { 141, 8, 153, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Version:" };
        };
        /* View 7 */
        { 165, 8, 177, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Comments:" };
        };
        /* View 8 */
        { 16, 71, 28, 234 },
        0x00000000, 0x00000000, 20816, 22250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 20816, 0 } }; /* constructor offset 8 */
        };
        /* View 9 */
        { 28, 71, 40, 234 },
        0x00000000, 0x00000000, 12803, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 10 */
        { 81, 71, 117, 234 },
        0x00000000, 0x00000010, 3056, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 11 */
        { 45, 71, 57, 234 },
        0x00000000, 0x00000000, 3051, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 12 */
        { 57, 71, 81, 234 },
        0x00000000, 0x00000010, 3054, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 13 */
        { 117, 71, 129, 234 },
        0x00000000, 0x00000000, 20803, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 14 */
        { 129, 71, 141, 234 },
        0x00000000, 0x00000000, 20805, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 15 */
        { 141, 71, 165, 234 },
        0x00000000, 0x00000010, 12801, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 16 */
        { 179, 8, 233, 234 },
        0x00000000, 0x00000011, 20807, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 17 */
        { 4, 27, 42, 65 },
        0x00000000, 0x00000000, 0, 9250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 18 */
        { 241, 6, 253, 135 },
        0x00000000, 0x00000000, 20806, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Locked" };
            Words { { 0, 20806 } }; /* constructor offset 16 */
        };
        /* View 19 */
        { 241, 137, 253, 234 },
        0x00000000, 0x00000000, 17050, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Stationery pad" };
            Words { { 0, 17050 } }; /* constructor offset 24 */
        };
    }
};

resource 'fvew' (17261, purgeable) {
    { 0, 0, 293, 242 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 293, 242 }, /* content bounds */
    0x0000, /* layout options */
    { 32, 0, 325, 242 }, /* view bounds */
    {
        /* View 1 */
        { 81, 8, 93, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Where:" };
        };
        /* View 2 */
        { 45, 8, 57, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Kind:" };
        };
        /* View 3 */
        { 57, 8, 69, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Size:" };
        };
        /* View 4 */
        { 117, 8, 129, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Created:" };
        };
        /* View 5 */
        { 129, 8, 141, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Modified:" };
        };
        /* View 6 */
        { 141, 8, 153, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Version:" };
        };
        /* View 7 */
        { 165, 8, 177, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Comments:" };
        };
        /* View 8 */
        { 16, 71, 28, 234 },
        0x00000000, 0x00000000, 20816, 22250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 20816, 0 } }; /* constructor offset 8 */
        };
        /* View 9 */
        { 28, 71, 40, 234 },
        0x00000000, 0x00000000, 12803, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 10 */
        { 81, 71, 117, 234 },
        0x00000000, 0x00000010, 3056, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 11 */
        { 45, 71, 57, 234 },
        0x00000000, 0x00000000, 3051, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 12 */
        { 57, 71, 81, 234 },
        0x00000000, 0x00000010, 3054, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 13 */
        { 117, 71, 129, 234 },
        0x00000000, 0x00000000, 20803, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 14 */
        { 129, 71, 141, 234 },
        0x00000000, 0x00000000, 20805, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 15 */
        { 141, 71, 165, 234 },
        0x00000000, 0x00000010, 12801, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 16 */
        { 178, 8, 218, 234 },
        0x00000000, 0x00000011, 20807, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 17 */
        { 268, 6, 280, 76 },
        0x00000000, 0x00000000, 20806, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Locked" };
            Words { { 0, 20806 } }; /* constructor offset 16 */
        };
        /* View 18 */
        { 227, 78, 287, 234 },
        0x00000000, 0x00000000, -1, 4750,
        {
            Words { { 4 } }; /* constructor offset 0 */
        };
        /* View 19 */
        { 220, 86, 232, 230 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Memory Requirements" };
        };
        /* View 20 */
        { 233, 86, 245, 169 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Suggested size:" };
        };
        /* View 21 */
        { 250, 91, 262, 169 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Minimum size:" };
        };
        /* View 22 */
        { 268, 86, 280, 169 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Preferred size:" };
        };
        /* View 23 */
        { 233, 171, 245, 214 },
        0x00000000, 0x00000000, 17300, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 24 */
        { 248, 169, 264, 214 },
        0x00000000, 0x00000009, 17301, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 25 */
        { 266, 169, 282, 214 },
        0x00000000, 0x00000009, 17302, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 26 */
        { 233, 218, 245, 226 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 0 };
            Words { { 331 } }; /* constructor offset 8 */
        };
        /* View 27 */
        { 250, 218, 262, 226 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 0 };
            Words { { 331 } }; /* constructor offset 8 */
        };
        /* View 28 */
        { 268, 218, 280, 226 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 0 };
            Words { { 331 } }; /* constructor offset 8 */
        };
        /* View 29 */
        { 4, 27, 42, 65 },
        0x00000000, 0x00000000, 0, 9250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (17263, purgeable) {
    { 0, 0, 140, 184 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 140, 184 }, /* content bounds */
    0x0000, /* layout options */
    { 40, 0, 180, 184 }, /* view bounds */
    {
        /* View 1 */
        { 4, 16, 36, 48 },
        0x00000000, 0x00000000, 0, 6250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 2 */
        { 38, 15, 50, 55 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Kind:" };
        };
        /* View 3 */
        { 50, 15, 62, 55 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Where:" };
        };
        /* View 4 */
        { 14, 57, 26, 169 },
        0x00000000, 0x00000000, 3050, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 5 */
        { 38, 57, 50, 169 },
        0x00000000, 0x00000000, 3051, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 6 */
        { 50, 57, 86, 169 },
        0x00000000, 0x00000010, 3056, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 7 */
        { 90, 15, 91, 169 },
        0x00000000, 0x00000000, -1, 4750,
        {
            Words { { 4 } }; /* constructor offset 0 */
        };
        /* View 8 */
        { 94, 15, 106, 169 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Program Linking" };
        };
        /* View 9 */
        { 118, 14, 130, 169 },
        0x00000000, 0x00000000, 17303, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Allow remote program linking" };
            Words { { 0, 17303 } }; /* constructor offset 38 */
        };
    }
};

resource 'fvew' (17281, purgeable) {
    { 0, 0, 335, 242 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 335, 242 }, /* content bounds */
    0x0000, /* layout options */
    { 32, 0, 367, 242 }, /* view bounds */
    {
        /* View 1 */
        { 81, 8, 93, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Where:" };
        };
        /* View 2 */
        { 45, 8, 57, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Kind:" };
        };
        /* View 3 */
        { 57, 8, 69, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Size:" };
        };
        /* View 4 */
        { 117, 8, 129, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Created:" };
        };
        /* View 5 */
        { 129, 8, 141, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Modified:" };
        };
        /* View 6 */
        { 141, 8, 153, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Version:" };
        };
        /* View 7 */
        { 165, 8, 177, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Comments:" };
        };
        /* View 8 */
        { 16, 71, 28, 234 },
        0x00000000, 0x00000000, 20816, 22250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 20816, 0 } }; /* constructor offset 8 */
        };
        /* View 9 */
        { 28, 71, 40, 234 },
        0x00000000, 0x00000000, 12803, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 10 */
        { 81, 71, 117, 234 },
        0x00000000, 0x00000010, 3056, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 11 */
        { 45, 71, 57, 234 },
        0x00000000, 0x00000000, 3051, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 12 */
        { 57, 71, 81, 234 },
        0x00000000, 0x00000010, 3054, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 13 */
        { 117, 71, 129, 234 },
        0x00000000, 0x00000000, 20803, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 14 */
        { 129, 71, 141, 234 },
        0x00000000, 0x00000000, 20805, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 15 */
        { 141, 71, 165, 234 },
        0x00000000, 0x00000010, 12801, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 16 */
        { 178, 8, 218, 234 },
        0x00000000, 0x00000011, 20807, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 17 */
        { 268, 6, 280, 76 },
        0x00000000, 0x00000000, 20806, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Locked" };
            Words { { 0, 20806 } }; /* constructor offset 16 */
        };
        /* View 18 */
        { 227, 78, 287, 234 },
        0x00000000, 0x00000000, -1, 4750,
        {
            Words { { 4 } }; /* constructor offset 0 */
        };
        /* View 19 */
        { 220, 86, 232, 230 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Memory Requirements" };
        };
        /* View 20 */
        { 233, 86, 245, 169 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Suggested size:" };
        };
        /* View 21 */
        { 250, 91, 262, 169 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Minimum size:" };
        };
        /* View 22 */
        { 268, 86, 280, 169 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Preferred size:" };
        };
        /* View 23 */
        { 233, 171, 245, 214 },
        0x00000000, 0x00000000, 17300, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 24 */
        { 248, 169, 264, 214 },
        0x00000000, 0x00000009, 17301, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 25 */
        { 266, 169, 282, 214 },
        0x00000000, 0x00000009, 17302, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 26 */
        { 233, 218, 245, 226 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 0 };
            Words { { 331 } }; /* constructor offset 8 */
        };
        /* View 27 */
        { 250, 218, 262, 226 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 0 };
            Words { { 331 } }; /* constructor offset 8 */
        };
        /* View 28 */
        { 268, 218, 280, 226 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 0 };
            Words { { 331 } }; /* constructor offset 8 */
        };
        /* View 29 */
        { 4, 27, 42, 65 },
        0x00000000, 0x00000000, 0, 9250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 30 */
        { 293, 8, 305, 39 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Note:" };
        };
        /* View 31 */
        { 293, 41, 329, 234 },
        0x00000000, 0x00000010, 17308, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (19501, purgeable) {
    { 0, 0, 237, 336 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 237, 336 }, /* content bounds */
    0x0004, /* layout options */
    { 0, 0, 237, 336 }, /* view bounds */
    {
        /* View 1 */
        { 17, 20, 29, 100 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Font for views:" };
        };
        /* View 2 */
        { 15, 100, 34, 255 },
        0x00000000, 0x00000000, 19550, 5750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
            Words { { 0, 0, 19550, 19550 } }; /* constructor offset 8 */
        };
        /* View 3 */
        { 15, 265, 34, 287 },
        0x00000000, 0x00000008, 19551, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
        };
        /* View 4 */
        { 15, 289, 34, 309 },
        0x00000000, 0x00000000, 19568, 5750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
            Words { { 0, 0, 19568, 19568 } }; /* constructor offset 8 */
        };
        /* View 5 */
        { 49, 10, 110, 326 },
        0x00000000, 0x00000000, -1, 4750,
        {
            Words { { 4 } }; /* constructor offset 0 */
        };
        /* View 6 */
        { 42, 20, 54, 100 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Icon Views" };
        };
        /* View 7 */
        { 50, 20, 82, 52 },
        0x00000000, 0x00000000, 19566, 6250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 8 */
        { 50, 48, 82, 80 },
        0x00000000, 0x00000000, 19566, 6250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 9 */
        { 63, 85, 75, 201 },
        0x00000000, 0x00000000, 19552, 21500,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Straight grid" };
            Words { { 0, 19552, 0 } }; /* constructor offset 22 */
        };
        /* View 10 */
        { 75, 20, 107, 52 },
        0x00000000, 0x00000000, 19567, 6250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 11 */
        { 75, 48, 107, 80 },
        0x00000000, 0x00000000, 19567, 6250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 12 */
        { 88, 85, 100, 201 },
        0x00000000, 0x00000000, 19552, 21500,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Staggered grid" };
            Words { { 0, 19552, 1 } }; /* constructor offset 24 */
        };
        /* View 13 */
        { 75, 201, 87, 316 },
        0x00000000, 0x00000000, 19553, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Always snap to grid" };
            Words { { 0, 19553 } }; /* constructor offset 28 */
        };
        /* View 14 */
        { 120, 10, 227, 326 },
        0x00000000, 0x00000000, -1, 4750,
        {
            Words { { 4 } }; /* constructor offset 0 */
        };
        /* View 15 */
        { 113, 20, 125, 100 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "List Views" };
        };
        /* View 16 */
        { 135, 49, 151, 65 },
        0x00000000, 0x00000000, 19563, 6250,
        {
            Words { { 2 } }; /* constructor offset 0 */
        };
        /* View 17 */
        { 162, 49, 174, 66 },
        0x00000000, 0x00000001, 19554, 21500,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 19554, 2 } }; /* constructor offset 8 */
        };
        /* View 18 */
        { 133, 85, 149, 101 },
        0x00000000, 0x00000000, 19564, 6250,
        {
            Words { { 1 } }; /* constructor offset 0 */
        };
        /* View 19 */
        { 162, 85, 174, 102 },
        0x00000000, 0x00000000, 19554, 21500,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 19554, 1 } }; /* constructor offset 8 */
        };
        /* View 20 */
        { 126, 113, 158, 145 },
        0x00000000, 0x00000000, 19565, 6250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 21 */
        { 162, 121, 174, 138 },
        0x00000000, 0x00000000, 19554, 21500,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 19554, 0 } }; /* constructor offset 8 */
        };
        /* View 22 */
        { 190, 20, 202, 201 },
        0x00000000, 0x00000000, 19555, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Calculate folder sizes" };
            Words { { 0, 19555 } }; /* constructor offset 32 */
        };
        /* View 23 */
        { 205, 20, 217, 201 },
        0x00000000, 0x00000000, 19556, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Show disk info in header" };
            Words { { 0, 19556 } }; /* constructor offset 34 */
        };
        /* View 24 */
        { 130, 201, 142, 316 },
        0x00000000, 0x00000000, 19557, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Show size" };
            Words { { 0, 19557 } }; /* constructor offset 18 */
        };
        /* View 25 */
        { 145, 201, 157, 316 },
        0x00000000, 0x00000000, 19558, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Show kind" };
            Words { { 0, 19558 } }; /* constructor offset 18 */
        };
        /* View 26 */
        { 160, 201, 172, 316 },
        0x00000000, 0x00000000, 19559, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Show label" };
            Words { { 0, 19559 } }; /* constructor offset 20 */
        };
        /* View 27 */
        { 175, 201, 187, 316 },
        0x00000000, 0x00000000, 19560, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Show date" };
            Words { { 0, 19560 } }; /* constructor offset 18 */
        };
        /* View 28 */
        { 190, 201, 202, 316 },
        0x00000000, 0x00000000, 19561, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Show version" };
            Words { { 0, 19561 } }; /* constructor offset 22 */
        };
        /* View 29 */
        { 205, 201, 217, 316 },
        0x00000000, 0x00000000, 19562, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Text { "Show comments" };
            Words { { 0, 19562 } }; /* constructor offset 22 */
        };
    }
};

resource 'fvew' (20511, purgeable) {
    { 0, 0, 289, 242 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 289, 242 }, /* content bounds */
    0x0000, /* layout options */
    { 36, 0, 325, 242 }, /* view bounds */
    {
        /* View 1 */
        { 81, 8, 93, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Where:" };
        };
        /* View 2 */
        { 45, 8, 57, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Kind:" };
        };
        /* View 3 */
        { 57, 8, 69, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Size:" };
        };
        /* View 4 */
        { 117, 8, 129, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Created:" };
        };
        /* View 5 */
        { 129, 8, 141, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Modified:" };
        };
        /* View 6 */
        { 141, 8, 177, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Original:" };
        };
        /* View 7 */
        { 177, 8, 189, 69 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, -1, 1 };
            Text { "Comments:" };
        };
        /* View 8 */
        { 16, 71, 28, 234 },
        0x00000000, 0x00000000, 20816, 22250,
        {
            Words { { 1, 9, 0, 65534, 0, 0, 20816, 0 } }; /* constructor offset 0 */
        };
        /* View 9 */
        { 28, 71, 40, 234 },
        0x00000000, 0x00000000, 12803, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 10 */
        { 81, 71, 117, 234 },
        0x00000000, 0x00000010, 3056, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 11 */
        { 45, 71, 57, 234 },
        0x00000000, 0x00000000, 3051, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 12 */
        { 57, 71, 81, 234 },
        0x00000000, 0x00000010, 3054, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 13 */
        { 117, 71, 129, 234 },
        0x00000000, 0x00000000, 20803, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 14 */
        { 129, 71, 141, 234 },
        0x00000000, 0x00000000, 20805, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 15 */
        { 141, 71, 177, 234 },
        0x00000000, 0x00000010, 20551, 3250,
        {
            Words { { 1, 9, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 16 */
        { 191, 8, 245, 234 },
        0x00000000, 0x00000011, 20807, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 17 */
        { 4, 27, 42, 65 },
        0x00000000, 0x00000000, 0, 9250,
        {
            Words { { 0 } }; /* constructor offset 0 */
        };
        /* View 18 */
        { 261, 6, 273, 121 },
        0x00000000, 0x00000000, 20806, 21250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 1 };
            Text { "Locked" };
            Words { { 0, 20806 } }; /* constructor offset 16 */
        };
        /* View 19 */
        { 257, 137, 277, 234 },
        0x00000000, 0x00000001, 20552, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Text { "Find Original" };
            Command { 'fndo' };
        };
    }
};

resource 'fvew' (20803, "date created", purgeable) {
    { 0, 0, 0, 0 }, /* outer bounds */
    0x00000002, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 0, 0 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 29, 144, 51, 282 },
        0x00000000, 0x00000000, 11053, 5750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Words { { 0, 0, 11053, 11032 } }; /* constructor offset 8 */
        };
        /* View 2 */
        { 30, 288, 49, 434 },
        0x00000000, 0x00000000, 11054, 22000,
        {
            Words { { 0, 12, 1, 0, 0, 0, 11054, 6 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (20805, "date modified", purgeable) {
    { 0, 0, 0, 0 }, /* outer bounds */
    0x00000002, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 0, 0 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 29, 144, 51, 282 },
        0x00000000, 0x00000000, 11053, 5750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Words { { 0, 0, 11053, 11032 } }; /* constructor offset 8 */
        };
        /* View 2 */
        { 30, 288, 49, 434 },
        0x00000000, 0x00000000, 11054, 22000,
        {
            Words { { 0, 12, 1, 0, 0, 0, 11054, 6 } }; /* constructor offset 0 */
        };
    }
};

resource 'fvew' (20806, "lock", purgeable) {
    { 0, 0, 0, 0 }, /* outer bounds */
    0x00000002, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 0, 0 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 29, 144, 51, 282 },
        0x00000000, 0x00000000, 11053, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Words { { 617, 29440 } }; /* constructor offset 8 */
        };
        /* View 2 */
        { 30, 288, 49, 434 },
        0x00000000, 0x00000000, 11054, 5750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
            Words { { 0, 0, 11054, 11038 } }; /* constructor offset 8 */
        };
    }
};

resource 'fvew' (20807, "comments", purgeable) {
    { 0, 0, 0, 0 }, /* outer bounds */
    0x00000002, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 0, 0 }, /* content bounds */
    0x0000, /* layout options */
    { 0, 0, 0, 0 }, /* view bounds */
    {
        /* View 1 */
        { 29, 144, 51, 282 },
        0x00000000, 0x00000000, 11053, 5750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Words { { 0, 0, 11053, 11034 } }; /* constructor offset 8 */
        };
        /* View 2 */
        { 30, 288, 49, 434 },
        0x00000000, 0x00000001, 11050, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
        };
    }
};

resource 'fvew' (25550, purgeable) {
    { 0, 0, 76, 280 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 76, 280 }, /* content bounds */
    0x0005, /* layout options */
    { 0, 0, 76, 280 }, /* view bounds */
    {
        /* View 1 */
        { 7, 9, 23, 219 },
        0x00000000, 0x00000000, -1, 7750,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 0, 0 };
            Text { "Checking digital signature for:" };
        };
        /* View 2 */
        { 29, 9, 45, 270 },
        0x00000000, 0x00000000, 12807, 3250,
        {
            Words { { 0, 12, 0, 0 } }; /* constructor offset 0 */
        };
        /* View 3 */
        { 55, 10, 66, 198 },
        0x00000000, 0x00000000, 12808, 5250,
        {
            Words { { 1, 21 } }; /* constructor offset 0 */
        };
        /* View 4 */
        { 51, 211, 71, 270 },
        0x00000000, 0x00000000, 3063, 21000,
        {
            /* font, size, style, justification */
            TextStyle { 0, 12, 1, 0 };
            Text { "Stop" };
            Command { 'ucan' };
        };
    }
};

resource 'fvew' (27501, purgeable) {
    { 0, 0, 144, 160 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 144, 160 }, /* content bounds */
    0x0004, /* layout options */
    { 0, 0, 144, 160 }, /* view bounds */
    {
        /* View 1 */
        { 10, 46, 26, 150 },
        0x00000000, 0x00000001, 27567, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 2 */
        { 28, 46, 44, 150 },
        0x00000000, 0x00000001, 27566, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 3 */
        { 46, 46, 62, 150 },
        0x00000000, 0x00000001, 27565, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 4 */
        { 64, 46, 80, 150 },
        0x00000000, 0x00000001, 27564, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 5 */
        { 82, 46, 98, 150 },
        0x00000000, 0x00000001, 27563, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 6 */
        { 100, 46, 116, 150 },
        0x00000000, 0x00000001, 27562, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 7 */
        { 118, 46, 134, 150 },
        0x00000000, 0x00000001, 27561, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 8 */
        { 10, 10, 26, 40 },
        0x00000000, 0x00000000, 27557, 21750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 27557, 6 } }; /* constructor offset 8 */
        };
        /* View 9 */
        { 28, 10, 44, 40 },
        0x00000000, 0x00000000, 27556, 21750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 27556, 5 } }; /* constructor offset 8 */
        };
        /* View 10 */
        { 46, 10, 62, 40 },
        0x00000000, 0x00000000, 27555, 21750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 27555, 4 } }; /* constructor offset 8 */
        };
        /* View 11 */
        { 64, 10, 80, 40 },
        0x00000000, 0x00000000, 27554, 21750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 27554, 3 } }; /* constructor offset 8 */
        };
        /* View 12 */
        { 82, 10, 98, 40 },
        0x00000000, 0x00000000, 27553, 21750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 27553, 2 } }; /* constructor offset 8 */
        };
        /* View 13 */
        { 100, 10, 116, 40 },
        0x00000000, 0x00000000, 27552, 21750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 27552, 1 } }; /* constructor offset 8 */
        };
        /* View 14 */
        { 118, 10, 134, 40 },
        0x00000000, 0x00000000, 27551, 21750,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
            Words { { 0, 0, 27551, 0 } }; /* constructor offset 8 */
        };
    }
};

resource 'fvew' (27513, purgeable) {
    { 0, 0, 144, 160 }, /* outer bounds */
    0x00000000, 0x00000000, 0x00000000, /* root state */
    0, /* root ID */
    { 0, 0, 144, 160 }, /* content bounds */
    0x0004, /* layout options */
    { 0, 0, 144, 160 }, /* view bounds */
    {
        /* View 1 */
        { 10, 10, 26, 150 },
        0x00000000, 0x00000001, 27567, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 2 */
        { 28, 10, 44, 150 },
        0x00000000, 0x00000001, 27566, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 3 */
        { 46, 10, 62, 150 },
        0x00000000, 0x00000001, 27565, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 4 */
        { 64, 10, 80, 150 },
        0x00000000, 0x00000001, 27564, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 5 */
        { 82, 10, 98, 150 },
        0x00000000, 0x00000001, 27563, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 6 */
        { 100, 10, 116, 150 },
        0x00000000, 0x00000001, 27562, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
        /* View 7 */
        { 118, 10, 134, 150 },
        0x00000000, 0x00000001, 27561, 8250,
        {
            /* font, size, style, justification */
            TextStyle { 1, 9, 0, 0 };
        };
    }
};

/* Native gest 128 and hcsl 0 are declared in guest_code.r and implemented in ../guest/. */

resource 'scsz' (0) {
    0x8000, /* flags */
    {
        16000;
        16000;
        32000;
        16000;
        16000;
        32000;
    }
};

resource 'sirz' (0) {
    111000 /* system heap size, bytes */
};
