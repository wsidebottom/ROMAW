/*+===================================================================
  File:      label-menu.r

  Summary:   Typed Finder label-menu layout, colors, shared CIcon and tint slot.

  Classes:   None.

  Functions: No explicitly exported functions.

  Origin:    Executor native Finder label-menu configuration, previously JSON.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

type 'lMnu' {
    integer; /* frame_width */
    integer; /* frame_height */
    integer; /* canvas_width */
    integer; /* canvas_height */
    integer; /* header_height */
    integer; /* item_height */
    integer; /* separator_y */
    integer; /* separator_gray */
    integer; /* text_x */
    integer; /* none_text_x */
    integer; /* sprite_x */
    integer; /* sprite_y */
    integer; /* check_x */
    integer; /* check_y */
    integer; /* icon_resource: shared Finder cicn resource */
    integer; /* icon_color_index: palette entry changed by label tinting */
    array [8] { /* None followed by the seven Finder labels */
        pstring; /* name */
        unsigned byte; unsigned byte; unsigned byte; /* RGB, 0..255 */
    };
    array [8] { pstring; }; /* nine columns: '.' clear, '#' set */
};

resource 'lMnu' (0) {
    143, /* frame_width */
    144, /* frame_height */
    144, /* canvas_width */
    145, /* canvas_height */
    30, /* header_height */
    16, /* item_height */
    23, /* separator_y */
    136, /* separator_gray */
    42, /* text_x */
    14, /* none_text_x */
    10, /* sprite_x */
    -8, /* sprite_y */
    3, /* check_x */
    3, /* check_y */
    301, /* icon_resource: artwork.r creates this from label-menu.png */
    255, /* icon_color_index: black in the generated Macintosh 8-bit palette */
    {
        "None", 0, 0, 0;
        "Essential", 255, 102, 0;
        "Hot", 221, 0, 0;
        "In Progress", 255, 0, 153;
        "Cool", 0, 153, 255;
        "Personal", 0, 0, 204;
        "Project 1", 0, 102, 0;
        "Project 2", 102, 51, 0;
    },
    {
        "........#";
        ".......##";
        "......##.";
        ".....##..";
        "#...##...";
        "##.##....";
        ".###.....";
        "..#......";
    }
};
