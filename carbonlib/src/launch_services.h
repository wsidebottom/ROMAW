/*+===================================================================
  File:      launch_services.h

  Summary:   Launch Services application lookup using the classic Finder registry.

  Classes:   None.

  Functions: Private frame adapter for LSGetApplicationForInfo.

  Origin:    Universal Interfaces 3.4.2 LaunchServices.h and Executor's classic application registry.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_LAUNCH_SERVICES_H
#define ROMAW_CARBON_LAUNCH_SERVICES_H

#include "cf_batch.h"
#include "cfx_entries.h"
#include "fs_refs.h"
#include "ProcessMgr.h"
#include "rsys/application_registry.h"
#include "rsys/filetypemap.h"

enum {
    carbon_ls_roles_viewer_or_editor = 0x06,
    carbon_ls_application_not_found = -10814,
    carbon_ls_data_unavailable = -10813,
};

static int carbon_ls_find_application(uint32 creator, uint32 type, FSSpec* spec)
{
    if ((type || creator) && ROMlib_application_spec(creator, type, 1, spec) == noErr)
        return 1;
    if (!creator)
        return 0;
    ProcessSerialNumber current;
    ProcessInfoRec info = {.processInfoLength = CLC(sizeof info)};
    PP_ASSIGN(info.processAppSpec, spec);
    return C_GetCurrentProcess(&current) == noErr && C_GetProcessInformation(&current, &info) == noErr &&
           info.processType == TICKX("APPL") && info.processSignature == CLC(creator);
}

static int carbon_ls_find_extension(void* extension, FSSpec* spec)
{
    char text[66];
    FileTypeMapping mapping;
    return extension && ROMlib_carbon_cf_string_c(extension, text, sizeof text, 0x08000100) &&
           filetype_map_lookup_path(text, &mapping) && carbon_ls_find_application(mapping.creator, mapping.type, spec);
}

static int carbon_LSGetApplicationForInfo(ROMlib_carbon_ppc_frame* call)
{
    uint32 type = call->words[0], creator = call->words[1], extension = call->words[2], roles = call->words[3];
    uint32 ref_address = call->words[4], url_address = call->words[5];
    _Alignas(128) Byte ref[80];
    _Alignas(128) FSSpec spec = {0};
    void* name = extension ? ROMlib_carbon_guest_memory(extension, 8, 0) : NULL;
    void* ref_output = ref_address ? ROMlib_carbon_guest_memory(ref_address, sizeof ref, 1) : NULL;
    void* url_output = url_address ? ROMlib_carbon_guest_memory(url_address, sizeof(uint32), 1) : NULL;
    call->words[0] = paramErr;
    if ((!type && !creator && !extension) || (!ref_address && !url_address) || (ref_address && !ref_output) ||
        (url_address && !url_output) ||
        (extension && (!name || ROMlib_carbon_cf_type_id(name) != ROMlib_cfvalue_string_type())))
        return 1;

    /* Classic associations do not distinguish viewer and editor claims. */
    call->words[0] = carbon_ls_application_not_found;
    if (!(roles & carbon_ls_roles_viewer_or_editor) ||
        (!carbon_ls_find_application(creator, type, &spec) && !carbon_ls_find_extension(name, &spec)))
        return 1;

    ROMlib_carbon_ppc_frame make_ref = *call;
    make_ref.words[0] = US_TO_SYN68K(&spec);
    make_ref.words[1] = US_TO_SYN68K(ref);
    if (!ROMlib_carbon_fs_ref(&make_ref, carbon_fs_make_ref_from_spec))
        return 0;
    if ((int32)make_ref.words[0] != noErr)
        return 1;

    if (url_output) {
        ROMlib_carbon_ppc_frame make_url = *call;
        make_url.words[0] = 0;
        make_url.words[1] = US_TO_SYN68K(&spec);
        make_url.words[2] = 0;
        if (!ROMlib_cfx_CFCreateURLFromFSSpec(&make_url))
            return 0;
        if (!make_url.words[0]) {
            call->words[0] = carbon_ls_data_unavailable;
            return 1;
        }
        uint32 encoded = CLC(make_url.words[0]);
        memcpy(url_output, &encoded, sizeof encoded);
    }
    if (ref_output)
        memcpy(ref_output, ref, sizeof ref);
    call->words[0] = noErr;
    return 1;
}

#endif
