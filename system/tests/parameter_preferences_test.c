/*+===================================================================
  File:      parameter_preferences_test.c

  Summary:   Verify opening and creation of the combined parameter preferences file.

  Classes:   None.

  Functions: ROMlib_romaw_preferences_spec, C_FSpOpenDF, C_FSpCreate, main

  Origin:    Regression fixture for Executor's parameter preferences file.

  Copyright and Legal notices: See the repository license.
===================================================================+*/

#define NDEBUG 1
#include "../../services/osutil.c"
#undef main
#undef NDEBUG
#include <assert.h>

static boolean_t exists;
static OSErr open_error, create_error, folder_error;
static unsigned creates, opens;

OSErr ROMlib_romaw_preferences_spec(FSSpec* spec)
{
    spec->vRefNum = CWC(-4);
    spec->parID = CLC(12345);
    memcpy(spec->name, ROMAW_PREFERENCES_NAME, sizeof ROMAW_PREFERENCES_NAME);
    return folder_error;
}

pascal OSErr C_FSpOpenDF(FSSpecPtr spec, SignedByte permission, int16* ref)
{
    assert(CW(spec->vRefNum) == -4 && CL(spec->parID) == 12345 && permission == fsRdWrPerm);
    assert(!memcmp(spec->name, ROMAW_PREFERENCES_NAME, sizeof ROMAW_PREFERENCES_NAME));
    ++opens;
    if (open_error)
        return open_error;
    if (!exists)
        return fnfErr;
    *ref = CWC(7);
    return noErr;
}

pascal OSErr C_FSpCreate(FSSpecPtr spec, OSType creator, OSType type, ScriptCode script)
{
    assert(CW(spec->vRefNum) == -4 && CL(spec->parID) == 12345 && spec->name[0] == 5 && script == 0);
    assert(creator == TICK("RSED") && type == TICK("rsrc"));
    ++creates;
    if (create_error)
        return create_error;
    exists = TRUE;
    return noErr;
}

int main(void)
{
    INTEGER ref = 123;
    assert(openparam(&ref) == noErr && ref == 7 && creates == 1 && opens == 2);
    assert(openparam(&ref) == noErr && ref == 7 && creates == 1 && opens == 3);
    folder_error = fnfErr;
    assert(openparam(&ref) == fnfErr && opens == 3);
    folder_error = noErr;
    open_error = permErr;
    assert(openparam(&ref) == permErr && creates == 1);
    open_error = noErr;
    exists = FALSE;
    create_error = dskFulErr;
    assert(openparam(&ref) == dskFulErr && creates == 2);
    puts("Combined preferences use the configured folder, preserve existing files and report open/create errors");
    return 0;
}
