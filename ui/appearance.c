/*+===================================================================
  File:      appearance.c

  Summary:   Implements appearance support for ROMAW application appearance and help.

  Classes:   None.

  Functions: ROMlib_RegisterAppearanceClient, ROMlib_UnregisterAppearanceClient, ROMlib_install_font_resources,
             ROMlib_parse_appearance, ROMlib_replace_resources, ROMlib_set_appearance

  Origin:    Existing repository source in executor/src/ui; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 2000 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ResourceMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "rsys/appearance.h"

#include "ResourceMgr.h"
#include "FileMgr.h"
#include "MemoryMgr.h"
#include "OSUtil.h"

#include "rsys/error.h"
#include "rsys/options.h"

PRIVATE appearance_t appearance = appearance_sys7;

/*
 * NOTE:  Order of following entries must correspond to appearance_t enum
 */

PRIVATE StringPtr res_filenames[] = {
    (StringPtr) "\010mac.rsrc",
    (StringPtr) "\014windows.rsrc",
};

/* Exactly the same as CountTypes, except only the resource file with the
   refnum of rn is consulted.  */

PRIVATE INTEGER CountTypesRN(INTEGER rn)
{
    INTEGER savern, retval;

    savern = CurResFile();
    UseResFile(rn);
    retval = Count1Types();
    UseResFile(savern);
    return retval;
}

/* Exactly the same as CountResources, except only the resource file with the
   refnum rn is consulted.  */

PRIVATE INTEGER CountResourcesRN(INTEGER rn, ResType type)
{
    INTEGER savern, retval;

    savern = CurResFile();
    UseResFile(rn);
    retval = Count1Resources(type);
    UseResFile(savern);
    return retval;
}

/* Exactly like GetResource, except limited to the file specified by rn.  */

PRIVATE Handle GetResourceRN(INTEGER rn, ResType type, INTEGER id)
{
    INTEGER savern;
    Handle retval;

    savern = CurResFile();
    UseResFile(rn);
    retval = Get1Resource(type, id);
    UseResFile(savern);
    return retval;
}

/* Exactly the same as AddResource, except its action is limited to the file
   with the refnum rn.  */

PRIVATE void AddResourceRN(INTEGER rn, Handle h, ResType type, INTEGER id, Str255 name, BOOLEAN mark_changed)
{
    Handle current_handle;

    current_handle = GetResourceRN(rn, type, id);
    if (current_handle) {
        if (current_handle != h) {
            Size new_size;
            OSErr err;

            LoadResource(current_handle);
            new_size = GetHandleSize(h);
            SignedByte state = HGetState(current_handle);
            HUnlock(current_handle);
            SetHandleSize(current_handle, new_size);
            err = MemError();
            HSetState(current_handle, state);
            if (err != noErr)
                warning_unexpected("err = %d", err);
            else {
                memcpy(STARH(current_handle), STARH(h), new_size);
                if (mark_changed) {
                    INTEGER attrs = GetResAttrs(current_handle);
                    SetResAttrs(current_handle, attrs & ~resProtected);
                    ChangedResource(current_handle);
                    SetResAttrs(current_handle, attrs | resChanged);
                }

                /* NOTE: we don't call ChangedResource because we don't
		 care if this change gets lost.  In fact, we will try
		 to set up our System file so that it has the union of
		 all the resources that the various appearances use so that
		 the act of setting an appearance won't cause the system
		 file to be marked dirty.  This should lessen the chance
		 of corruption.

		 ChangedResource (current_handle); */
            }
            DisposHandle(h);
        }
    }
    else {
        INTEGER savern;
        Handle hh;

        hh = h;
        if (HandleZone(h) != MR(SysZone)) {
            Handle save_hand;

            save_hand = h;
            HandToHand(&hh);
            DisposHandle(save_hand);
        }
        savern = CurResFile();
        UseResFile(rn);
        AddResource(hh, type, id, name);
        UseResFile(savern);
    }
}

/* Exactly the same as GetIndType, except its action is limited to the file
   with the refnum rn.  */

PRIVATE void GetIndTypeRN(INTEGER rn, ResType* typep, INTEGER type_num)
{
    INTEGER savern;

    savern = CurResFile();
    UseResFile(rn);
    Get1IndType(typep, type_num);
    UseResFile(savern);
}

/* Exactly the same as GetIndResource except limited to resources from the
   file with refnum rn. */

PRIVATE Handle GetIndResourceRN(INTEGER rn, ResType type, INTEGER id)
{
    INTEGER savern;
    Handle retval;

    savern = CurResFile();
    UseResFile(rn);
    retval = Get1IndResource(type, id);
    UseResFile(savern);
    return retval;
}

PRIVATE void replace_resources(INTEGER master_file_rn, INTEGER from_file_rn, BOOLEAN fonts_only)
{
    THz save_zone;
    INTEGER type_num, type_num_max;
    INTEGER save_resload;

    save_zone = GetZone();
    SetZone(MR(SysZone));
    type_num_max = CountTypesRN(from_file_rn);
    save_resload = ResLoad;
    SetResLoad(FALSE);
    for (type_num = 1; type_num <= type_num_max; ++type_num) {
        ResType type;
        INTEGER res_num, res_num_max;

        GetIndTypeRN(from_file_rn, &type, type_num);
        type = CL(type);
        if (fonts_only && type != TICK("FONT") && type != TICK("NFNT") && type != TICK("FOND") && type != TICK("sfnt"))
            continue;
        res_num_max = CountResourcesRN(from_file_rn, type);
        for (res_num = 1; res_num <= res_num_max; ++res_num) {
            Handle h;
            INTEGER id;
            ResType t;
            Str255 name;

            h = GetIndResourceRN(from_file_rn, type, res_num);
            GetResInfo(h, &id, &t, name);
            id = CW(id);
            LoadResource(h);
            DetachResource(h);
            AddResourceRN(master_file_rn, h, type, id, name, fonts_only);
        }
    }
    SetResLoad(save_resload);
    SetZone(save_zone);
}

PUBLIC void ROMlib_replace_resources(INTEGER master, INTEGER source)
{
    replace_resources(master, source, FALSE);
}

PUBLIC void ROMlib_install_font_resources(INTEGER source)
{
    replace_resources(CW(SysMap), source, TRUE);
}

PUBLIC void ROMlib_set_appearance(void)
{
    INTEGER res_file;

    if (appearance < 0 || appearance >= NELEM(res_filenames))
        appearance = 0;

    res_file = OpenRFPerm(res_filenames[appearance], CW(BootDrive), fsRdPerm);
    if (res_file != CWC(-1)) {
        ROMlib_replace_resources(CW(SysMap), res_file);
        CloseResFile(res_file);
    }
    else if (appearance != 0) {
        appearance = 0;
        ROMlib_set_appearance();
    }
}

PUBLIC boolean_t ROMlib_parse_appearance(const char* appearance_str)
{
    boolean_t retval;

    retval = TRUE;

    if (strcasecmp(appearance_str, "mac") == 0)
        appearance = appearance_sys7;
    else if (strcasecmp(appearance_str, "windows") == 0) {
        ROMlib_options |= ROMLIB_RECT_SCREEN_BIT;
        appearance = appearance_win3;
    }
    else
        retval = FALSE;

    return retval;
}

PUBLIC appearance_t ROMlib_get_appearance(void)
{
    return appearance;
}

/* Classic drawing remains available without opting into themed controls.
   This is the same registration compatibility behavior as the native PPC port. */
PUBLIC int32 ROMlib_RegisterAppearanceClient(void)
{
    return noErr;
}

/* Registration in the classic drawing backend owns no theme resources. */
PUBLIC int32 ROMlib_UnregisterAppearanceClient(void)
{
    return noErr;
}
