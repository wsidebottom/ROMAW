/*+===================================================================
  File:      qStandard.c

  Summary:   Implements q standard support for Macintosh QuickDraw graphics.

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/quickdraw; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1988, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "rsys/pstuff.h"

#define PTRCAST

P1(PUBLIC pascal trap, void, SetStdProcs, QDProcs*, procs)
{
    FIELD_ASSIGN_RAW(procs->textProc, RM(PTRCAST P_StdText));
    FIELD_ASSIGN_RAW(procs->lineProc, RM(PTRCAST P_StdLine));
    FIELD_ASSIGN_RAW(procs->rectProc, RM(PTRCAST P_StdRect));
    FIELD_ASSIGN_RAW(procs->rRectProc, RM(PTRCAST P_StdRRect));
    FIELD_ASSIGN_RAW(procs->ovalProc, RM(PTRCAST P_StdOval));
    FIELD_ASSIGN_RAW(procs->arcProc, RM(PTRCAST P_StdArc));
    FIELD_ASSIGN_RAW(procs->polyProc, RM(PTRCAST P_StdPoly));
    FIELD_ASSIGN_RAW(procs->rgnProc, RM(PTRCAST P_StdRgn));
    FIELD_ASSIGN_RAW(procs->bitsProc, RM(PTRCAST P_StdBits));
    FIELD_ASSIGN_RAW(procs->commentProc, RM(PTRCAST P_StdComment));
    PP_RAW(procs->txMeasProc) = RM(PTRCAST P_StdTxMeas);
    PP_RAW(procs->getPicProc) = RM(PTRCAST P_StdGetPic);
    PP_RAW(procs->putPicProc) = RM(PTRCAST P_StdPutPic);
}
