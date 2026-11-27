/*+===================================================================
  File:      resource_types.r

  Summary:   Defines the checked layouts for structured System resource inputs.

  Classes:   None.

  Functions: Resource type and native definition declarations only.

  Origin:    ROMAW IntlUtil/keyboard records, native System dispatch stubs,
             and the local Super ResEdit 2.1.3 international bundle template.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

/* The native builder encodes these layouts directly. External PNG expressions,
   pixel-pattern offset synthesis and explicit DITL alignment bytes are checked
   authoring extensions, like the existing DSAT $$icon expression. */
#define P_cdef0 0
#define P_cdef16 4
#define P_wdef0 8
#define P_wdef16 12
#define P_mdef0 16
#define P_ldef0 20
#define P_ldef19 72
#define P_mbdf0 24
#define P_snth5 28
#define P_cdef1008 36
#define P_dcmp_template 40

/* MOVEA.L $58.W,A0; MOVEA.L offset(A0),A0; JMP (A0).
   Only the native definition-table byte offset is supplied by the instance. */
type 'CDEF' {
    unsigned integer = 8312; unsigned integer = 88;
    unsigned integer = 8296; unsigned integer; unsigned integer = 20176;
};
type 'WDEF' as 'CDEF';
type 'MDEF' as 'CDEF';
type 'LDEF' as 'CDEF';
type 'MBDF' as 'CDEF';
type 'snth' as 'CDEF';
type 'dcmp' as 'CDEF';

type 'INTL' {
    switch {
        case NumberFormat:
            string[1]; string[1]; string[1]; string[3];
            unsigned byte; unsigned byte; unsigned byte;
            unsigned byte; unsigned byte; unsigned byte;
            string[4]; string[4]; string[9]; unsigned byte; integer;
        case DateFormat:
            array[7] { pstring[15]; };
            array[12] { pstring[15]; };
            unsigned byte; unsigned byte; unsigned byte; unsigned byte;
            string[4]; string[4]; string[4]; string[4]; string[4];
            integer; unsigned integer;
    };
};

type 'KCHR' {
    unsigned integer;
    array[256] { unsigned byte; };
    unsigned integer = $$CountOf(Tables);
    array Tables { string[128]; };
    unsigned integer = $$CountOf(DeadKeys);
    array DeadKeys {
        unsigned byte; unsigned byte;
        unsigned integer = $$CountOf(Pairs);
        array Pairs { unsigned byte; unsigned byte; };
        unsigned byte; unsigned byte;
    };
};

type 'KMAP' {
    unsigned integer; unsigned integer;
    array[128] { unsigned byte; };
    unsigned integer = $$CountOf(Exceptions);
    array Exceptions { unsigned byte; unsigned byte; pstring; };
};

type 'itlb' {
    integer; integer; integer; unsigned integer; integer; unsigned integer;
    integer; unsigned byte; unsigned byte; integer; integer;
};

type 'fwst' {
    unsigned integer; rect;
    array[6] { integer; };
};

/* The inherited movp record has no recovered semantic template. These are its
   seventeen signed preference words, explicitly indexed by byte offset. */
type 'movp' { array[17] { integer; }; };

/* PAT / PAT# instances use $$pattern(PNG): eight monochrome row bytes.
   ppat instances carry the pattern/expanded state, fallback PNG, PixMap fields,
   indexed PNG, and color-table seed/flags/indexes. The checked encoder computes
   all three resource-relative offsets and emits exact palette indexes. */
