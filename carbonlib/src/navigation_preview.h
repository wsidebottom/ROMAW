/*+===================================================================
  File:      navigation_preview.h

  Summary:   Implement Navigation Services preview resources and drawing.

  Classes:   None.

  Functions: Private frame adapters for the Carbon export registry.

  Origin:    NavigationLib.CL 1.4, the System 9 Navigation_Real fragment,
             and the QuickTime Component Creation Guide preview format.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_NAVIGATION_PREVIEW_H
#define ROMAW_CARBON_NAVIGATION_PREVIEW_H

#include "TextEdit.h"
#include "rsys/file.h"
#include "rsys/pictheader.h"
#include "rsys/resource.h"
#include "rsys/system_resources.h"

typedef struct __attribute__((packed)) {
    uint32_t modification_date;
    uint16_t version;
    ResType type;
    INTEGER id;
} navigation_preview_marker;

typedef struct {
    INTEGER previous, file;
    BOOLEAN loading, borrowed;
} navigation_preview_map;

static OSErr navigation_preview_spec(uint32_t address, FSSpec* output)
{
    const AEDesc* source = address ? ROMlib_carbon_try_guest_memory(address, sizeof(AEDesc), 0) : NULL;
    if (!source)
        return paramErr;
    AEDesc object = *source;
    AEDesc coerced = {0};
    const int convert = object.descriptorType != TICKX("fss ");
    OSErr status = convert ? C_AECoerceDesc(&object, TICK("fss "), &coerced) : noErr;
    const AEDesc* descriptor = convert ? &coerced : &object;
    if (!status) {
        if (ROMlib_carbon_get_desc_data_size(descriptor) != sizeof *output)
            status = errAEWrongDataType;
        else
            status = ROMlib_carbon_get_desc_data(descriptor, output, sizeof *output);
    }
    if (convert && HIDDEN_RAW(coerced.dataHandle))
        C_AEDisposeDesc(&coerced);
    if (!status && (!output->name[0] || output->name[0] >= sizeof output->name))
        status = paramErr;
    return status;
}

static OSErr navigation_preview_open(FSSpec* spec, bool writing, navigation_preview_map* map)
{
    *map = (navigation_preview_map){.previous = C_CurResFile(), .loading = ResLoad, .file = -1};
    Boolean in_chain;
    INTEGER previous_ref;
    if (ROMlib_resource_file_already_open(spec, &in_chain, &previous_ref) && in_chain) {
        map->file = CW(previous_ref);
        map->borrowed = TRUE;
        if (writing) {
            OSErr error;
            const fcbrec* file = PRNTOFPERR(map->file, &error);
            if (error)
                return error;
            if (!(file->fcflags & fcwriteperm))
                return wrPermErr;
        }
    }
    else {
        const SignedByte permission = writing ? fsRdWrPerm : fsRdPerm;
        map->file = C_FSpOpenResFile(spec, permission);
        if (writing && map->file == -1 && C_ResError() == eofErr) {
            FInfo info;
            const OSErr error = C_FSpGetFInfo(spec, &info);
            if (error)
                return error;
            C_FSpCreateResFile(spec, CL(info.fdCreator), CL(info.fdType), -1);
            if (C_ResError())
                return C_ResError();
            map->file = C_FSpOpenResFile(spec, permission);
        }
        if (map->file == -1)
            return C_ResError();
    }
    C_UseResFile(map->file);
    const OSErr error = C_ResError();
    C_SetResLoad(TRUE);
    return error;
}

static OSErr navigation_preview_close(const navigation_preview_map* map, OSErr status)
{
    if (map->file != -1 && !map->borrowed) {
        C_CloseResFile(map->file);
        if (!status)
            status = C_ResError();
    }
    C_UseResFile(map->previous);
    C_SetResLoad(map->loading);
    return status;
}

static OSErr navigation_preview_date(FSSpec* spec, uint32_t* date)
{
    CInfoPBRec info = {0};
    PP_ASSIGN(info.hFileInfo.ioNamePtr, (StringPtr)spec->name);
    info.hFileInfo.ioVRefNum = spec->vRefNum;
    info.hFileInfo.ioDirID = spec->parID;
    const OSErr status = PBGetCatInfo(&info, FALSE);
    if (!status && (info.hFileInfo.ioFlAttrib & ATTRIB_ISADIR))
        return ioErr;
    if (!status)
        *date = info.hFileInfo.ioFlMdDat;
    return status;
}

static bool navigation_preview_read_marker(Handle resource, navigation_preview_marker* marker)
{
    if (!resource || GetHandleSize(resource) < (Size)sizeof *marker || !STARH(resource))
        return false;
    memcpy(marker, STARH(resource), sizeof *marker);
    return CW(marker->version) <= 1 && marker->type != TICKX("pnot") && marker->type;
}

static OSErr navigation_preview_write(FSSpec* spec, ResType type, const Byte* data, Size length)
{
    navigation_preview_marker marker = {.type = CL(type)};
    uint32_t modification;
    OSErr status = navigation_preview_date(spec, &modification);
    if (status)
        return status;
    marker.modification_date = modification;
    /* Copy guest storage before the Resource Manager can move its heap blocks. */
    Byte* copy = malloc(length);
    if (!copy)
        return memFullErr;
    memcpy(copy, data, length);
    Handle preview = NewHandle(length);
    if (preview)
        memcpy(STARH(preview), copy, length);
    free(copy);
    if (!preview)
        return memFullErr;

    navigation_preview_map map;
    status = navigation_preview_open(spec, true, &map);
    if (status) {
        DisposHandle(preview);
        return navigation_preview_close(&map, status);
    }
    Handle resource = C_Get1Resource(TICK("pnot"), 0);
    navigation_preview_marker previous;
    const bool replacing = resource != NULL;
    if (replacing) {
        if (!navigation_preview_read_marker(resource, &previous))
            status = nav_unavailable;
    }
    else {
        status = C_ResError();
        if (status == resNotFound)
            status = noErr;
        if (!status && !(resource = NewHandleClear(sizeof marker + sizeof(uint16_t))))
            status = memFullErr;
    }
    if (status) {
        DisposHandle(preview);
        return navigation_preview_close(&map, status);
    }

    const INTEGER id = C_Unique1ID(type);
    C_AddResource(preview, type, id, (StringPtr)"");
    status = C_ResError();
    bool attached = !status;
    if (!status) {
        marker.id = CW(id);
        if (replacing)
            C_ChangedResource(resource);
        else
            C_AddResource(resource, TICK("pnot"), 0, (StringPtr)"");
        status = C_ResError();
        if (!status)
            memcpy(STARH(resource), &marker, sizeof marker);
        else {
            C_RmveResource(preview);
            attached = C_ResError() != noErr;
        }
    }
    if (status) {
        if (!attached)
            DisposHandle(preview);
        if (!replacing)
            DisposHandle(resource);
    }
    else {
        if (replacing && !previous.version) {
            Handle old_preview = C_Get1Resource(CL(previous.type), CW(previous.id));
            if (old_preview && old_preview != preview && !(C_GetResAttrs(old_preview) & resProtected)) {
                C_RmveResource(old_preview);
                if (!C_ResError())
                    DisposHandle(old_preview);
            }
        }
        C_UpdateResFile(map.file);
        status = C_ResError();
    }
    return navigation_preview_close(&map, status);
}

static int carbon_NavCreatePreview(ROMlib_carbon_ppc_frame* call)
{
    if (!call->words[2])
        return navigation_fallback("NavCreatePreview", call);
    const Size length = (int32_t)call->words[3];
    const ResType type = call->words[1];
    const Byte* data = length > 0 ? ROMlib_carbon_try_guest_memory(call->words[2], length, 0) : NULL;
    if (!data || !type || type == TICK("pnot"))
        return navigation_result(call, paramErr);
    _Alignas(128) FSSpec spec;
    OSErr status = navigation_preview_spec(call->words[0], &spec);
    if (!status)
        status = navigation_preview_write(&spec, type, data, length);
    if (status == nav_unavailable)
        return navigation_fallback("NavCreatePreview", call);
    return navigation_result(call, status);
}

static int carbon_NavHasPreview(ROMlib_carbon_ppc_frame* call)
{
    Byte* output = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], 1, 1) : NULL;
    if (!output)
        return navigation_result(call, paramErr);
    _Alignas(128) FSSpec spec;
    OSErr status = navigation_preview_spec(call->words[0], &spec);
    if (status)
        return navigation_result(call, status);
    navigation_preview_map map;
    *output = 0;
    status = navigation_preview_open(&spec, false, &map);
    if (!status) {
        navigation_preview_marker marker;
        Handle resource = C_Get1IndResource(TICK("pnot"), 1);
        if (resource && !navigation_preview_read_marker(resource, &marker))
            status = nav_unavailable;
        else if (resource && C_Get1Resource(CL(marker.type), CW(marker.id))) {
            uint32_t modification;
            if (!navigation_preview_date(&spec, &modification)) {
                const uint32_t stored = CL(marker.modification_date), current = CL(modification);
                *output = stored <= current && current - stored <= 10;
            }
        }
    }
    status = navigation_preview_close(&map, status == eofErr ? noErr : status);
    if (status == nav_unavailable)
        return navigation_fallback("NavHasPreview", call);
    return navigation_result(call, status);
}

typedef struct navigation_preview {
    struct navigation_preview* next;
    Ptr token;
    uint32_t port, saved;
    FSSpec file;
    Rect area;
    Str255 missing_message, working_message;
} navigation_preview;

static navigation_preview* navigation_previews;

static navigation_preview** navigation_preview_link(uint32_t token)
{
    navigation_preview** link = &navigation_previews;
    while (*link && US_TO_SYN68K((*link)->token) != token)
        link = &(*link)->next;
    return link;
}

static void navigation_preview_destroy(navigation_preview** link)
{
    navigation_preview* preview = *link;
    *link = preview->next;
    DisposPtr(preview->token);
    free(preview);
}

static void navigation_preview_reset(void)
{
    while (navigation_previews)
        navigation_preview_destroy(&navigation_previews);
}

static int carbon_NavCreatePreviewReference(ROMlib_carbon_ppc_frame* call)
{
    navigation_preview* preview = calloc(1, sizeof *preview);
    if (!preview)
        return navigation_result(call, 0);
    preview->token = NewPtr(4);
    if (!preview->token) {
        free(preview);
        return navigation_result(call, 0);
    }
    preview->next = navigation_previews;
    navigation_previews = preview;
    return navigation_result(call, US_TO_SYN68K(preview->token));
}

static void navigation_preview_dispose_saved(navigation_preview* preview, ROMlib_carbon_ppc_frame* call)
{
    if (!preview->saved)
        return;
    ROMlib_carbon_ppc_frame arguments = *call;
    arguments.words[0] = preview->saved;
    preview->saved = 0;
    navigation_saved("NavDisposePreviewReference", &arguments, 0);
}

static int carbon_NavDisposePreviewReference(ROMlib_carbon_ppc_frame* call)
{
    if (!call->words[0])
        return 1;
    navigation_preview** link = navigation_preview_link(call->words[0]);
    if (!*link)
        return navigation_saved("NavDisposePreviewReference", call, 0);
    navigation_preview_dispose_saved(*link, call);
    navigation_preview_destroy(link);
    return 1;
}

static bool navigation_preview_message(uint32_t address, Str255 message)
{
    if (!address) {
        message[0] = 0;
        return true;
    }
    const Byte* length = ROMlib_carbon_try_guest_memory(address, 1, 0);
    const Byte* text = length ? ROMlib_carbon_try_guest_memory(address, *length + 1, 0) : NULL;
    if (!text)
        return false;
    memcpy(message, text, text[0] + 1);
    return true;
}

static int carbon_NavSetupPreview(ROMlib_carbon_ppc_frame* call)
{
    navigation_preview* preview = *navigation_preview_link(call->words[0]);
    if (!preview)
        return navigation_fallback("NavSetupPreview", call);
    const Rect* area = call->words[3] ? ROMlib_carbon_try_guest_memory(call->words[3], sizeof(Rect), 0) : NULL;
    Str255 missing, working;
    if (!area || !navigation_preview_message(call->words[4], missing) ||
        !navigation_preview_message(call->words[5], working))
        return navigation_result(call, paramErr);
    const Rect bounds = *area;
    _Alignas(128) FSSpec file;
    const OSErr status = navigation_preview_spec(call->words[2], &file);
    if (status)
        return navigation_result(call, status);
    navigation_preview_dispose_saved(preview, call);
    preview->port = call->words[1];
    preview->file = file;
    preview->area = bounds;
    memcpy(preview->missing_message, missing, missing[0] + 1);
    memcpy(preview->working_message, working, working[0] + 1);
    return navigation_result(call, noErr);
}

static int carbon_NavGetPreviewArea(ROMlib_carbon_ppc_frame* call)
{
    const navigation_preview* preview = *navigation_preview_link(call->words[0]);
    if (!preview)
        return navigation_fallback("NavGetPreviewArea", call);
    void* output = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], sizeof(Rect), 1) : NULL;
    if (!output)
        return navigation_result(call, paramErr);
    memcpy(output, &preview->area, sizeof(Rect));
    return navigation_result(call, noErr);
}

static int carbon_NavSetPreviewArea(ROMlib_carbon_ppc_frame* call)
{
    navigation_preview* preview = *navigation_preview_link(call->words[0]);
    if (!preview)
        return navigation_fallback("NavSetPreviewArea", call);
    const Rect* input = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], sizeof(Rect), 0) : NULL;
    if (!input)
        return navigation_result(call, paramErr);
    const Rect area = *input;
    if (preview->saved) {
        ROMlib_carbon_ppc_frame arguments = *call;
        arguments.words[0] = preview->saved;
        if (!navigation_fallback("NavSetPreviewArea", &arguments))
            return 0;
        if ((int16_t)arguments.words[0])
            return navigation_result(call, (int16_t)arguments.words[0]);
    }
    preview->area = area;
    return navigation_result(call, noErr);
}

typedef struct {
    AEDesc file;
    Rect area;
    Str255 missing_message, working_message;
} navigation_preview_saved_options;

static OSErr navigation_preview_create_saved(navigation_preview* preview, ROMlib_carbon_ppc_frame* call)
{
    if (preview->saved)
        return noErr;
    navigation_preview_saved_options* options = (void*)NewPtr(sizeof *options);
    if (!options)
        return memFullErr;
    options->area = preview->area;
    memcpy(options->missing_message, preview->missing_message, preview->missing_message[0] + 1);
    memcpy(options->working_message, preview->working_message, preview->working_message[0] + 1);
    OSErr status = C_AECreateDesc(TICK("fss "), (Ptr)&preview->file, sizeof(FSSpec), &options->file);
    if (!status) {
        ROMlib_carbon_ppc_frame arguments = *call;
        if (navigation_saved("NavCreatePreviewReference", &arguments, 0) && arguments.words[0]) {
            preview->saved = arguments.words[0];
            arguments.words[1] = preview->port;
            arguments.words[2] = US_TO_SYN68K(&options->file);
            arguments.words[3] = US_TO_SYN68K(&options->area);
            arguments.words[4] = US_TO_SYN68K(options->missing_message);
            arguments.words[5] = US_TO_SYN68K(options->working_message);
            status = navigation_fallback("NavSetupPreview", &arguments) ? (int16_t)arguments.words[0] : nav_unavailable;
            if (status)
                navigation_preview_dispose_saved(preview, call);
        }
        else
            status = nav_unavailable;
        C_AEDisposeDesc(&options->file);
    }
    DisposPtr((Ptr)options);
    return status;
}

static OSErr navigation_preview_load(navigation_preview* preview, Handle* picture)
{
    _Alignas(128) FSSpec spec = preview->file;
    navigation_preview_map map;
    OSErr status = navigation_preview_open(&spec, false, &map);
    if (!status) {
        navigation_preview_marker marker;
        Handle resource = C_Get1IndResource(TICK("pnot"), 1);
        if (!resource)
            status = C_ResError();
        else if (!navigation_preview_read_marker(resource, &marker) || marker.type != TICKX("PICT"))
            status = nav_unavailable;
        else {
            Handle image = C_Get1Resource(TICK("PICT"), CW(marker.id));
            if (!image)
                status = C_ResError();
            else if (!STARH(image) || GetHandleSize(image) < 12 ||
                     !ROMlib_pict_header_valid((const Byte*)STARH(image), GetHandleSize(image)))
                status = resNotFound;
            else {
                *picture = image;
                status = HandToHand(picture);
                if (status)
                    *picture = NULL;
            }
        }
    }
    return navigation_preview_close(&map, status);
}

static int carbon_NavDrawPreview(ROMlib_carbon_ppc_frame* call)
{
    navigation_preview* preview = *navigation_preview_link(call->words[0]);
    if (!preview)
        return navigation_fallback("NavDrawPreview", call);
    if (!preview->file.name[0])
        return navigation_result(call, paramErr);
    Handle picture = NULL;
    const bool automatic = (Byte)call->words[1] != 0;
    OSErr status = automatic ? nav_unavailable : navigation_preview_load(preview, &picture);
    if (automatic || status == nav_unavailable || status == resNotFound || status == eofErr) {
        const OSErr saved_status = navigation_preview_create_saved(preview, call);
        if (!saved_status) {
            call->words[0] = preview->saved;
            return navigation_fallback("NavDrawPreview", call);
        }
        if (automatic || status == nav_unavailable)
            return navigation_result(call, saved_status);
    }
    if (picture || status == resNotFound || status == eofErr) {
        if (picture)
            navigation_preview_dispose_saved(preview, call);
        const GrafPtr prior = thePort;
        const GrafPtr port = preview->port ? (GrafPtr)SYN68K_TO_US_CHECK0(preview->port) : prior;
        if (!port || !ROMlib_valid_qd_port(port)) {
            if (picture)
                DisposHandle(picture);
            return navigation_result(call, paramErr);
        }
        _Alignas(16) Rect area = preview->area;
        C_SetPort(port);
        if ((Byte)call->words[2])
            C_EraseRect(&area);
        if (picture) {
            C_DrawPicture((PicHandle)picture, &area);
            DisposHandle(picture);
        }
        else if (preview->missing_message[0])
            C_TextBox((Ptr)preview->missing_message + 1, preview->missing_message[0], &area, 0);
        C_SetPort(prior);
    }
    return navigation_result(call, status);
}

static int carbon_NavHandlePreviewEvent(ROMlib_carbon_ppc_frame* call)
{
    const navigation_preview* preview = *navigation_preview_link(call->words[0]);
    if (!preview)
        return navigation_saved("NavHandlePreviewEvent", call, 0);
    if (!preview->saved || !call->words[1])
        return navigation_result(call, 0); /* A static QuickDraw picture has no event handler. */
    call->words[0] = preview->saved;
    return navigation_saved("NavHandlePreviewEvent", call, 0);
}

#endif
