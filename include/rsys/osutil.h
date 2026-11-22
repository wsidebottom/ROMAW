/*+===================================================================
  File:      osutil.h

  Summary:   Declares osutil support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__RSYS_OSUTIL__)
#define __RSYS_OSUTIL__

/*
 * Copyright 1995 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: osutil.h 63 2004-12-24 18:19:43Z ctm $
 */

extern long long ROMlib_long_long_secs(INTEGER year, INTEGER month, INTEGER day, INTEGER hour, INTEGER minute,
                                       INTEGER second);

extern void date_to_swapped_fields(long long mactime, INTEGER* yearp, INTEGER* monthp, INTEGER* dayp, INTEGER* hourp,
                                   INTEGER* minutep, INTEGER* secondp, INTEGER* dayofweekp, INTEGER* dayofyearp,
                                   INTEGER* weekofyearp);

extern void C_ROMlib_wakeup(void);

extern LONGINT ROMlib_GMTcorrect; /* Correction for GMT to localtime */

enum { ROMLIB_MACHINE_LOCATION_SIZE = 12, ROMLIB_MACHINE_LOCATION_XPRAM = 0x000c00e4 };

void ROMlib_read_machine_location(void* location);
void ROMlib_write_machine_location(const void* location);
int32 ROMlib_machine_gmt_delta(void);
uint64 ROMlib_get_local_clock(void);
OSErr ROMlib_set_local_clock(uint64 value);
void ROMlib_strip_diacritics(Byte* text, INTEGER length);

#define U70MINUSM04 2082844800 /* 1/1/1970 - 1/1/1904 */
#define UNIXTIMETOMACTIME(x) ((x) - ROMlib_GMTcorrect + U70MINUSM04)
#define MACTIMETOLUNIXTIME(x) ((x) - U70MINUSM04)
#define MACTIMETOGUNIXTIME(x) ((x) + ROMlib_GMTcorrect - U70MINUSM04)

extern char ROMlib_phoneyrom[];
extern int ROMlib_addressing_mode;
extern boolean_t ROMlib_set_mmu_mode(Byte mode32);

#endif /* ! efined(__RSYS_OSUTIL__) */
