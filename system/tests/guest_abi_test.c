/*+===================================================================
  File:      guest_abi_test.c

  Summary:   Guest ABI layouts must not depend on the Windows host pointer width.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Guest ABI layouts must not depend on the Windows host pointer width. */
#include "rsys/common.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "WindowMgr.h"
#include "ControlMgr.h"
#include "DialogMgr.h"
#include "TextEdit.h"
#include "MemoryMgr.h"
#include "ResourceMgr.h"
#include "AppleEvents.h"
#include "CommTool.h"
#include "EditionMgr.h"
#include "ProcessMgr.h"
#include "SoundMgr.h"
#include "PrintMgr.h"
#include "rsys/file.h"
#include "rsys/cfm.h"
#include "rsys/mixed_mode.h"
#include "rsys/print.h"
#include <stddef.h>
#include <stdio.h>
_Static_assert(sizeof(AEDesc) == 8, "AEDesc layout");
_Static_assert(sizeof(AEArrayData) == 12, "AEArrayData layout");
_Static_assert(sizeof(AEKeyDesc) == 12, "AEKeyDesc layout");
_Static_assert(sizeof(CRMSerialRecord) == 30, "CRMSerialRecord layout");
_Static_assert(sizeof(struct SectionRecord) == 36, "struct SectionRecord layout");
_Static_assert(sizeof(LaunchParamBlockRec) == 44, "LaunchParamBlockRec layout");
_Static_assert(sizeof(ExtSoundHeader) == 62, "ExtSoundHeader layout");
_Static_assert(sizeof(SndDoubleBufferHeader) == 24, "SndDoubleBufferHeader layout");
_Static_assert(sizeof(MemFragment) == 9, "MemFragment layout");
_Static_assert(sizeof(DiskFragment) == 12, "DiskFragment layout");
_Static_assert(sizeof(SegmentedFragment) == 10, "SegmentedFragment layout");
_Static_assert(sizeof(FragmentLocator) == 16, "FragmentLocator layout");
_Static_assert(sizeof(InitBlock) == 44, "InitBlock layout");
_Static_assert(sizeof(RoutineRecord) == 20, "RoutineRecord layout");
_Static_assert(sizeof(RoutineDescriptor) == 32, "RoutineDescriptor layout");
_Static_assert(sizeof(TSetRslBlk) == 16, "TSetRslBlk layout");
_Static_assert(sizeof(fcbrec) == 94, "fcbrec layout");
_Static_assert(offsetof(LaunchParamBlockRec, launchProcessSN) == 20, "launch process offset");
_Static_assert(offsetof(RoutineRecord, reserved2) == 12, "routine descriptor offset");
#undef main

int main(void)
{
    puts("Macintosh guest ABI layout checks passed.");
    return 0;
}
