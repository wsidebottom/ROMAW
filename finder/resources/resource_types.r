/*+===================================================================
  File:      resource_types.r

  Summary:   Standard Retro68 definitions plus Finder's private resource layouts.

  Classes:   None.

  Functions: No explicitly exported functions.

  Origin:    Existing repository source in executor/resources/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Standard Retro68 definitions plus Finder's private resource layouts. */
#include "Types.r"

type 'RECT' { rect; };

type 'dctb' {
    unsigned longint; /* seed */
    unsigned integer; /* flags */
    integer;          /* last color index; -1 means empty */
    array Colors { integer; unsigned integer; unsigned integer; unsigned integer; };
};

type 'pltt' {
    integer = $$CountOf(Colors);
    fill byte[14];
    array Colors {
        unsigned integer; unsigned integer; unsigned integer; /* RGB */
        unsigned integer; /* usage */
        unsigned integer; /* tolerance */
        unsigned integer; /* private flags */
        unsigned longint; /* private data */
    };
};

type 'fmnu' {
    unsigned integer; /* version */
    integer = $$CountOf(Items) + 1;
    longint;          /* menu identifier */
    unsigned longint; /* options */
    pstring; align word; /* title */
    array Items {
        longint;          /* command */
        unsigned integer; /* flags */
        byte; byte;       /* keyboard equivalent and mark */
        pstring; align word; /* item text */
    };
};

type 'fmap' { array Entries { longint; longint; }; };
type 'fAni' { integer = $$CountOf(Frames); array Frames { integer; }; };
type 'fdmn' {
    integer; /* menu identifier */
    longint; /* command to locate */
    longint; /* command to insert */
    unsigned integer; byte; byte; /* flags, key and mark */
    pstring; /* title */
};
type 'fval' { array Values { unsigned integer; }; };
type 'scsz' {
    unsigned integer; /* flags */
    array Sizes { longint; }; /* fixed-point screen-size thresholds */
};
type 'sirz' { unsigned longint; }; /* system heap size in bytes */

type 'acur' {
    unsigned integer; /* frame count */
    unsigned integer; /* current frame */
    array Frames { integer; unsigned integer; };
};

/* Finder view archives. Constructor parameters have class-specific layouts;
   the Rez variants below add no discriminator bytes to the stored resource. */
type 'fvew' {
    rect; /* outer bounds */
    unsigned longint; unsigned longint; unsigned longint; /* root state */
    integer; /* root identifier */
    rect; /* content bounds */
    unsigned integer; /* layout options */
    rect; /* view bounds */
    integer = $$CountOf(Views);
    array Views {
        rect; /* bounds */
        unsigned longint; /* state */
        unsigned longint; /* flags */
        integer; /* identifier */
        longint; /* class ID */
        array Parameters {
            switch {
                case TextStyle:
                    integer; integer; integer; integer;
                case Text:
                    pstring; align word;
                case UnalignedText:
                    pstring;
                case Words:
                    array { unsigned integer; };
                case Bytes:
                    array { byte; };
                case Command:
                    longint;
                case Picture:
                    integer;
            };
        };
    };
};

/* Apple-event terminology: the shipped Finder suite has event definitions
   and no class, comparison or enumeration definitions. */
type 'aete' {
    unsigned integer; /* terminology version */
    unsigned integer; /* language */
    unsigned integer; /* script */
    integer = $$CountOf(Suites);
    array Suites {
        pstring; pstring; align word; /* name and description */
        longint; /* suite code */
        unsigned integer; unsigned integer; /* level and version */
        integer = $$CountOf(Events);
        array Events {
            pstring; pstring; align word; /* name and description */
            longint; longint; /* event class and event ID */
            longint; /* reply type */
            pstring; align word; unsigned integer; /* reply description, flags */
            longint; /* direct parameter type */
            pstring; align word; unsigned integer; /* description, flags */
            unsigned integer; /* parameter count; keep in sync with the list */
            array Parameters {
                pstring; align word; /* parameter name */
                longint; longint; /* keyword and type */
                pstring; align word; unsigned integer; /* description, flags */
            };
        };
        fill word[3]; /* no classes, comparisons or enumerations */
    };
};

/* Finder signature text has the same Pascal-string layout as STR. */
type 'MACS' { pstring; };
