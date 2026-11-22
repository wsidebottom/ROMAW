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

/* Literal data preserves extended item records not representable by the standard DITL layout. */
data 'DITL' (-6043, purgeable) {
    $"000B0000000000A100FC00B5014C0404"
    $"5361766500000000008200FC0096014C"
    $"040643616E63656C0000000000000000"
    $"0000000081040001E865000000000008"
    $"00EB00180151000000000000002000FC"
    $"0034014C0405456A6563740000000000"
    $"003C00FC0050014C04074465736B746F"
    $"700000000000001D000C007F00E60000"
    $"000000000006000C001900E600000000"
    $"0000007700FA0078014EC002000B0000"
    $"0000009D000F00AD00E3100000000000"
    $"0088000F009800E38808536176652061"
    $"733A00000000005800FC006C014C8000"
};

/* Literal data preserves extended item records not representable by the standard DITL layout. */
data 'DITL' (-6042, purgeable) {
    $"000800000000008700FC009B014C0404"
    $"4F70656E00000000006800FC007C014C"
    $"040643616E63656C0000000000000000"
    $"0000000081040001E866000000000008"
    $"00EB00180151000000000000002000FC"
    $"0034014C0405456A6563740000000000"
    $"003C00FC0050014C04074465736B746F"
    $"700000000000001D000C009F00E60000"
    $"000000000006000C001900E600000000"
    $"0000005B00FB005C014DC002000B"
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
data 'DITL' (-4063, sysheap, preload) {
    $"00240000000000E100BE00F500FA0402"
    $"4F4B0000000000E1006900F500A50406"
    $"43616E63656C0000000000E1001400F5"
    $"005004045361766500000000004C0016"
    $"0061008006064E6F726D616C00000000"
    $"00000000000100018000000000000060"
    $"00160075008E0609416E696D6174696F"
    $"6E0300000000004600AB005B00E70603"
    $"4F66660700000000005F00AB00740105"
    $"060750726574656E6402000000000078"
    $"00AB008D010E860B4F6E202873696C65"
    $"6E7429000000000027160112272B016D"
    $"05084E6F20436C6F636B00000000002A"
    $"05DC003F6A10850F5061737320506F73"
    $"745363726970740F0000000000340110"
    $"004901A8850F4E65776C696E65204D61"
    $"7070696E67110000000000480110005D"
    $"01A50511466C75736820436163686520"
    $"4F6674656E1200000000005C01100071"
    $"01B50512446972656374204469736B20"
    $"41636365737300000000007001100085"
    $"01B105124E6F2033322D626974205761"
    $"726E696E677300000000008401100099"
    $"01AB8511466F6E742053756273746974"
    $"7574696F6E100000000000B904FB00CF"
    $"05900510436163686520486575726973"
    $"74696373000000000093005100A3007E"
    $"1000000000000092001600A8004E0807"
    $"526566726573680E0000000000330016"
    $"00480081080E53637265656E20557064"
    $"6174657300000000003200AB004700E8"
    $"0805536F756E641000000000009A0092"
    $"00AA010688104E756D626572206F6620"
    $"436F6C6F72730000000000A9009B00BB"
    $"00C4060132010000000000A900CC00BB"
    $"00F1060134020000000000BA009B00CC"
    $"00C7060231360000000000BA00CC00CC"
    $"00FF0603323536000000000000B5004D"
    $"00C5008A100000000000010200170124"
    $"01A610000000000000B5001600C50049"
    $"880653797374656D0000000000A227A4"
    $"00B4280E060974686F7573616E647308"
    $"0000000000B327A400C5280E06086D69"
    $"6C6C696F6E73000000000098011000AD"
    $"017A050C50726574656E642048656C70"
    $"0000000000AC011000C1018D050F5072"
    $"6574656E642045646974696F6E0E0000"
    $"000000C0011000D5018A050E50726574"
    $"656E64205363726970740000000000D4"
    $"011000E90182050D50726574656E6420"
    $"416C696173090000000000C427A400D6"
    $"280E0509677261797363616C65000000"
    $"000000110015002201A8881250726566"
    $"6572656E63657320666F72205E30"
};

/* Literal data preserves nonzero alignment bytes not representable by the standard DITL layout. */
data 'DITL' (-4062, sysheap, preload) {
    $"000600000000008A0118009E01540402"
    $"4F4B000000000009000C008D00C80875"
    $"4578656375746F72A82F444F53206D2E"
    $"6E6E780DA920313938362D3139393620"
    $"415244490D416C6C2052696768747320"
    $"52657365727665640D0D652D6D61696C"
    $"3A20696E666F40617264692E636F6D0D"
    $"0D576F726C642057696465205765623A"
    $"2020687474703A2F2F7777772E617264"
    $"692E636F6D0000000000000900D10077"
    $"017C086C415244490D53756974652034"
    $"2D3130310D3136353020556E69766572"
    $"7369747920426C76642E2C204E450D41"
    $"6C6275717565727175652C204E4D2020"
    $"38373130320D0D2B3120353035203736"
    $"3620393131352050686F6E650D2B3120"
    $"35303520373636203531353320464158"
    $"00000000000B0184008E01EF08674D61"
    $"7420486F737465747465722073696E67"
    $"6C652D68616E6465646C792064657369"
    $"676E65642C20696D706C656D656E7465"
    $"6420616E642064656275676765642074"
    $"68652073796E74686574696320435055"
    $"20706F7274696F6E206F662045786563"
    $"75746F722E800000000000E7000C011D"
    $"01F008B14170706C6520616E64204D61"
    $"63696E746F7368206172652072656769"
    $"7374657265642074726164656D61726B"
    $"73206F66204170706C6520436F6D7075"
    $"7465722C20496E632E0D457865637574"
    $"6F722069732061207265676973746572"
    $"65642074726164656D61726B206F660D"
    $"20202020202020202020202020202020"
    $"20202020202020202020202020202020"
    $"41626163757320526573656172636820"
    $"616E6420446576656C6F706D656E742C"
    $"20496E632E000000000000B1000C00D7"
    $"01F00883112D53686966742D313A2041"
    $"626F757420426F782020202020202020"
    $"202020202020202020202020112D5368"
    $"6966742D323A205363616E20666F7220"
    $"466C6F70706965730D112D5368696674"
    $"2D333A2053637265656E2044756D7020"
    $"746F2046696C6520202020112D536869"
    $"66742D353A20507265666572656E6365"
    $"732050616E656C00000000000098000C"
    $"00AB00F008234C6566742D416C74203D"
    $"2011202020202052696768742D416C74"
    $"203D204F7074696F6E00"
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

data 'PAT ' (16, sysheap, purgeable) {
    $"AA55AA55AA55AA55"
};

data 'PAT ' (17, sysheap, purgeable) {
    $"8822882288228822"
};

data 'PAT ' (19, sysheap) {
    $"4000040040000400"
};

data 'PAT#' (0, purgeable) {
    $"0026FFFFFFFFFFFFFFFFDDFF77FFDDFF"
    $"77FFDD77DD77DD77DD77AA55AA55AA55"
    $"AA5555FF55FF55FF55FFAAAAAAAAAAAA"
    $"AAAAEEDDBB77EEDDBB77888888888888"
    $"8888B130031BD8C00C8D801002200108"
    $"4004FF888888FF888888FF808080FF08"
    $"08088000000000000000804020000204"
    $"08008244394482010101F87422478F17"
    $"227155A04040550A0404205088888888"
    $"0502BF00BFBFB0B0B0B0000000000000"
    $"00008000080080000800880022008800"
    $"22008822882288228822AA00AA00AA00"
    $"AA00FF00FF00FF00FF00112244881122"
    $"4488FF000000FF000000010204081020"
    $"4080AA00800088008000FF8080808080"
    $"8080081C22C180010204881422418800"
    $"AA0040A00000040A0000038448300C02"
    $"01018080413E080814E3102054AAFF02"
    $"040877898F8F7798F8F80008142A552A"
    $"1408"
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
