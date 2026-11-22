/*+===================================================================
  File:      list.h

  Summary:   Declares list support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__RSYS_LIST__)
#define __RSYS_LIST__

/*
 * Copyright 1989, 1990 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: list.h 63 2004-12-24 18:19:43Z ctm $
 */

#include "ListMgr.h"
#include "MemoryMgr.h"
#include "ResourceMgr.h"
#include "rsys/mman.h"
#include "rsys/pstuff.h"

extern void C_ldef0(INTEGER, BOOLEAN, Rect*, Cell, INTEGER, INTEGER, ListHandle);

#define DODRAW 0x8 /* whether or not we should really draw */

/* Byte offset of the trailing cell-data sentinel in cellArray. ResEdit reads
   this field while preserving offsets across a change in column layout. */
static inline void ROMlib_list_update_max_index(ListHandle list)
{
    HxX(list, maxIndex) = CW(2 * (int32)(Hx(list, dataBounds.right) - Hx(list, dataBounds.left)) *
                             (Hx(list, dataBounds.bottom) - Hx(list, dataBounds.top)));
}

typedef pascal void (*listprocp)(INTEGER mess, BOOLEAN sel, Rect* rectp, Cell cell, INTEGER off, INTEGER len,
                                 ListHandle lhand);

extern void ROMlib_listcall(INTEGER mess, BOOLEAN sel, Rect* rp, Cell cell, INTEGER off, INTEGER len, ListHandle lhand);

#define LISTCALL(msg, sel, rect, cell, doff, dlen, list) \
    ROMlib_listcall((msg), (sel), (rect), (cell), (doff), (dlen), (list))

#define LISTDECL() INTEGER liststate

#define LISTBEGIN(l) (liststate = HGetState(HxP(l, listDefProc)), HSetState(HxP(l, listDefProc), liststate | LOCKBIT))

#define LISTEND(l) HSetState(HxP(l, listDefProc), liststate)

#if !defined(__STDC__)
extern void ROMlib_vminmax();
extern void ROMlib_hminmax();
extern INTEGER* ROMlib_getoffp();
#else  /* __STDC__ */
extern void ROMlib_vminmax(INTEGER* minp, INTEGER* maxp, ListPtr lp);
extern void ROMlib_hminmax(INTEGER* minp, INTEGER* maxp, ListPtr lp);
extern INTEGER* ROMlib_getoffp(Cell cell, ListHandle list);
#endif /* __STDC__ */

#endif /* !defined(__RSYS_LIST__) */
