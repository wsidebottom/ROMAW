/*+===================================================================
  File:      navigation.h

  Summary:   Implement Navigation Services records and modal file selection.

  Classes:   None.

  Functions: ROMlib_carbon_navigation_reset; private frame adapters for the Carbon export registry.

  Origin:    NavigationLib.CL 1.4 export wrappers, Navigation_Real dialog defaults and reply cleanup,
             and Universal Interfaces 3.4.2 Navigation.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_NAVIGATION_H
#define ROMAW_CARBON_NAVIGATION_H

#include "cf_batch.h"
#include "StdFilePkg.h"
#include "MemoryMgr.h"
#include "file_manager.h"
#include "rsys/cfm.h"
#include <stdlib.h>

static int navigation_result(ROMlib_carbon_ppc_frame* call, int32_t result)
{
    call->words[0] = result;
    return 1;
}

enum {
    navigation_symbols_slot = 0x190,
    navigation_load_count_slot = 0x194,
    navigation_library_names_slot = 0x154,
};

static int navigation_release_library(const ROMlib_carbon_reference* saved, uint32_t toc, uint32_t count)
{
    if (!count)
        return 1;
    return cfp_write(toc + navigation_load_count_slot, count - 1) &&
           (count != 1 || cfp_private(saved, 0x24, (const uint32_t[]){0}, 1, NULL));
}

static int navigation_lifecycle(ROMlib_carbon_ppc_frame* call, const char* name, uint32_t offset, int loading)
{
    ROMlib_carbon_reference saved;
    if (!ROMlib_carbon_load_reference("NavigationLib.CL", name, &saved))
        return navigation_result(call, loading ? fragLibNotFound : noErr);
    if (saved.code_offset != offset || saved.code_size != 0x7768 || !saved.toc || saved.toc_offset ||
        saved.toc_size != 0x13bc)
        return ROMlib_carbon_invoke_frame(saved.function, call);

    const uint32_t toc = US_TO_SYN68K(saved.toc);
    uint32_t count;
    if (!cfp_get(toc + navigation_load_count_slot, &count))
        return 0;
    if (!loading)
        return navigation_release_library(&saved, toc, count) && navigation_result(call, noErr);

    uint32_t status = noErr;
    if (!count) {
        uint32_t symbols;
        if (!cfp_get(toc + navigation_symbols_slot, &symbols))
            return 0;
        if (!symbols) {
            /* Reuse NavigationLib's private connection and symbol table factory.
               Native and saved dialog entry points then see the same table. */
            static const uint16_t names[] = {
                0x000, 0x010, 0x024, 0x041, 0x04e, 0x05b, 0x06f, 0x089, 0x0a0, 0x0b0, 0x0c2, 0x0d4, 0x0e6, 0x0f5,
                0x108, 0x11a, 0x12d, 0x13f, 0x153, 0x166, 0x17e, 0x19a, 0x1b7, 0x1c9, 0x1da, 0x1f2, 0x206, 0x21a,
            };
            uint32_t base;
            if (!cfp_get(toc + navigation_library_names_slot, &base) ||
                !cfp_private(&saved, 0x5014, (const uint32_t[]){0x30}, 1, &symbols))
                return 0;
            if (!symbols)
                return navigation_result(call, -0x2153);
            uint32_t arguments[NELEM(names) + 3] = {symbols};
            for (unsigned i = 0; i < NELEM(names); ++i)
                arguments[i + 1] = base + names[i];
            if (!cfp_private(&saved, 0x4c7c, arguments, NELEM(arguments), NULL) ||
                !cfp_write(toc + navigation_symbols_slot, symbols) || !cfp_get(symbols, &status))
                return 0;
        }
    }
    return cfp_write(toc + navigation_load_count_slot, count + 1) &&
           (!status || navigation_release_library(&saved, toc, count + 1)) && navigation_result(call, (int16_t)status);
}

static int carbon_NavLoad(ROMlib_carbon_ppc_frame* call)
{
    return navigation_lifecycle(call, "NavLoad", 0x14c, 1);
}

static int carbon_NavUnload(ROMlib_carbon_ppc_frame* call)
{
    return navigation_lifecycle(call, "NavUnload", 0x124, 0);
}

static int carbon_NavServicesCanRun(ROMlib_carbon_ppc_frame* call)
{
    /* Navigation_Real checks both selectors via InterfaceLib, which reports
       the versions available to the running guest. */
    _Alignas(16) uint32_t version = 0;
    ROMlib_carbon_ppc_frame gestalt = {0};
    gestalt.words[0] = TICK("sysv");
    gestalt.words[1] = US_TO_SYN68K(&version);
    if (!ROMlib_carbon_call_romaw_service("Gestalt", &gestalt))
        return 0;
    if ((int16_t)gestalt.words[0] || (int32_t)CL(version) < 0x870)
        return navigation_result(call, 0);
    gestalt.words[0] = TICK("apvr");
    if (!ROMlib_carbon_call_romaw_service("Gestalt", &gestalt))
        return 0;
    return navigation_result(call, !(int16_t)gestalt.words[0] && (int32_t)CL(version) >= 0x101);
}

static int carbon_NavLibraryVersion(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t current = (int16_t)CW(CurMap);
    ROMlib_carbon_ppc_frame args = {0};
    uint32_t ignored, resource = 0, version = 0;
    const uint32_t system = (int16_t)CW(SysMap);
    args.words[0] = system;
    if (!ROMlib_carbon_call_toolbox_vector(0x1460, 0x80, 0, &args, &ignored))
        return 0;
    args.words[0] = TICK("nvrs");
    args.words[1] = (uint32_t)-20481;
    int ok = (uint32_t)(int16_t)CW(CurMap) != system ||
             ROMlib_carbon_call_toolbox_vector(0x0e7c, 0x2f0, 0, &args, &resource);
    /* A handle may be purged, or the Resource Manager may be in no-load mode. */
    uint32_t data;
    if (ok && resource) {
        args.words[0] = resource;
        uint32_t size = 0;
        ok = ROMlib_carbon_call_toolbox_vector(0x1488, 0xc0, 0, &args, &ignored) &&
             ROMlib_carbon_call_register_vector(0x0494, 0x133832, 0xa025, 0, &args, &size);
        if (ok && (int32_t)size >= 4 && cfp_get(resource, &data) && data)
            ok = cfp_get(data, &version);
        if (!ROMlib_carbon_call_toolbox_vector(0x148c, 0xc0, 0, &args, &ignored))
            ok = 0;
    }
    args.words[0] = current;
    if (!ROMlib_carbon_call_toolbox_vector(0x1460, 0x80, 0, &args, &ignored) || !ok)
        return 0;
    return navigation_result(call, version);
}

static int carbon_NavGetDefaultDialogOptions(ROMlib_carbon_ppc_frame* call)
{
    unsigned char* options = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], 0x612, 1) : NULL;
    if (!options)
        return navigation_result(call, paramErr);
    static const unsigned char header[] = {0, 0, 0, 0, 0, 0xe4, 0xff, 0xff, 0xff, 0xff};
    memcpy(options, header, sizeof header);
    for (size_t offset = sizeof header; offset < 0x60a; offset += 256)
        options[offset] = 0; /* Empty each Pascal string without writing past its length byte. */
    memset(options + 0x60a, 0, 8);
    return navigation_result(call, noErr);
}

static int carbon_NavGetDefaultDialogCreationOptions(ROMlib_carbon_ppc_frame* call)
{
    void* options = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], 0x32, 1) : NULL;
    if (!options)
        return navigation_result(call, paramErr);
    static const unsigned char defaults[0x32] = {
        [5] = 0xe4, [6] = 0xff, [7] = 0xff, [8] = 0xff, [9] = 0xff, [0x2d] = 2,
    };
    memcpy(options, defaults, sizeof defaults);
    return navigation_result(call, noErr);
}

static int navigation_dispose_descriptor(uint32_t address)
{
    ROMlib_carbon_ppc_frame arguments = {0};
    uint32_t result;
    arguments.words[0] = address;
    return ROMlib_carbon_call_toolbox_vector(0x0e58, 0x3a8, 0x204, &arguments, &result) && !(int16_t)result &&
           cfp_write(address, TICK("null")) && cfp_write(address + 4, 0);
}

static int carbon_NavDisposeReply(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[0];
    const unsigned char* reply = address ? ROMlib_carbon_try_guest_memory(address, 28, 1) : NULL;
    if (!reply)
        return navigation_result(call, paramErr);
    const unsigned version = (unsigned)reply[0] << 8 | reply[1];
    if (version > 2) {
        ROMlib_carbon_reference saved;
        if (ROMlib_carbon_load_reference("NavigationLib.CL", "NavDisposeReply", &saved))
            return ROMlib_carbon_invoke_frame(saved.function, call);
        return navigation_result(call, paramErr);
    }
    uint32_t translation, save_name;
    if (!cfp_get(address + 16, &translation) || !cfp_get(address + 24, &save_name))
        return 0;
    if (translation && !ROMlib_carbon_try_guest_memory(translation, 4, 0))
        return navigation_result(call, paramErr);
    if (version && save_name) {
        void* object = ROMlib_carbon_try_guest_memory(save_name, 8, 1);
        if (!object)
            return navigation_result(call, paramErr);
        ROMlib_carbon_cf_release(object);
        if (!cfp_write(address + 24, 0))
            return 0;
    }
    if (!navigation_dispose_descriptor(address + 6) || !navigation_dispose_descriptor(address + 20))
        return 0;
    if (translation) {
        ROMlib_carbon_ppc_frame arguments = {0};
        uint32_t ignored;
        arguments.words[0] = translation;
        if (!ROMlib_carbon_call_register_vector(0x04a8, 0x133802, 0xa02a, 0, &arguments, &ignored) ||
            !ROMlib_carbon_call_register_vector(0x048c, 0x133802, 0xa023, 0, &arguments, &ignored) ||
            !cfp_write(address + 16, 0))
            return 0;
    }
    return navigation_result(call, noErr);
}

enum {
    nav_reply_size = 28,
    nav_carbon_reply_size = 30,
    nav_options_size = 0x612,
    nav_option_no_type_popup = 0x01,
    nav_option_no_translate = 0x02,
    nav_option_no_translate_items = 0x04,
    nav_option_previews = 0x40,
    nav_option_multiple = 0x80,
    nav_wrong_state = -5694,
    nav_wrong_class = -5695,
    nav_unavailable = -5696,
    nav_control_failed = -5697,
    nav_invalid_control = -5698,
    nav_cancel = -128,
    nav_action_cancel = 1,
    nav_action_open = 2,
    nav_action_save = 3,
    nav_action_choose = 4,
};

static int navigation_saved(const char* name, ROMlib_carbon_ppc_frame* call, int32_t unavailable)
{
    ROMlib_carbon_reference saved;
    if (ROMlib_carbon_load_reference("NavigationLib.CL", name, &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    return navigation_result(call, unavailable);
}

static int navigation_fallback(const char* name, ROMlib_carbon_ppc_frame* call)
{
    return navigation_saved(name, call, nav_unavailable);
}

typedef struct {
    int16_t count;
    OSType values[4];
} nav_types;

static int navigation_types(uint32_t handle, nav_types* types)
{
    types->count = -1;
    if (!handle)
        return noErr;
    void* master = ROMlib_carbon_try_guest_memory(handle, 4, 0);
    uint32_t data;
    if (!master || !cfp_get(handle, &data) || !data)
        return paramErr;
    const Size length = GetHandleSize((Handle)master);
    const unsigned char* list = length >= 8 ? ROMlib_carbon_try_guest_memory(data, 8, 0) : NULL;
    if (!list)
        return paramErr;
    const unsigned count = (unsigned)list[6] << 8 | list[7];
    if (count > (uint32_t)(length - 8) / 4)
        return paramErr;
    if (count > sizeof types->values / sizeof types->values[0])
        return nav_unavailable;
    if (count) {
        list = ROMlib_carbon_try_guest_memory(data, 8 + count * 4, 0);
        if (!list)
            return paramErr;
        memcpy(types->values, list + 8, count * 4);
        types->count = count;
    }
    return noErr;
}

static int navigation_pick(StandardFileReply* file, int saving, const nav_types* types, uint32_t prompt,
                           uint32_t filename)
{
    ROMlib_carbon_ppc_frame arguments = {0};
    uint32_t ignored;
    if (saving) {
        arguments.words[0] = prompt;
        arguments.words[1] = filename;
        arguments.words[2] = US_TO_SYN68K(file);
        return ROMlib_carbon_call_toolbox_vector(0x15a8, 0x3f8e, 5, &arguments, &ignored);
    }
    _Alignas(16) OSType list[4];
    memcpy(list, types->values, sizeof list);
    arguments.words[1] = (uint16_t)types->count;
    arguments.words[2] = types->count > 0 ? US_TO_SYN68K(list) : 0;
    arguments.words[3] = US_TO_SYN68K(file);
    return ROMlib_carbon_call_toolbox_vector(0x15a8, 0xfb8e, 6, &arguments, &ignored);
}

static int navigation_select_reply(ROMlib_carbon_ppc_frame* call, StandardFileReply* file, int saving, void* save_name,
                                   int carbon)
{
    const uint32_t reply = call->words[1], selection = reply + 6;
    const size_t size = carbon ? nav_carbon_reply_size : nav_reply_size;
    unsigned char* record = ROMlib_carbon_try_guest_memory(reply, size, 1);
    if (!record)
        return navigation_result(call, paramErr);
    memset(record, 0, size);
    if (!cfp_write(selection, TICK("null")) || !cfp_write(reply + 20, TICK("null")))
        return 0;
    if (carbon)
        record[1] = 2;
    if (!file->sfGood)
        return navigation_result(call, carbon ? noErr : nav_cancel);
    ROMlib_carbon_ppc_frame arguments = {0};
    uint32_t result;
    arguments.words[3] = selection;
    if (!ROMlib_carbon_call_toolbox_vector(0x0e58, 0xdfa8, 0x706, &arguments, &result))
        return 0;
    if (!(int16_t)result) {
        arguments.words[0] = selection;
        arguments.words[1] = 1;
        arguments.words[2] = typeFSS;
        arguments.words[3] = US_TO_SYN68K(&file->sfFile);
        arguments.words[4] = sizeof file->sfFile;
        if (!ROMlib_carbon_call_toolbox_vector(0x0e58, 0x3ffa8, 0xa08, &arguments, &result))
            return 0;
    }
    if ((int16_t)result) {
        if (!navigation_dispose_descriptor(selection))
            return 0;
        return navigation_result(call, (int16_t)result);
    }
    record[2] = 1;
    if (saving) {
        record[3] = !!file->sfReplacing;
        memcpy(record + 14, &file->sfScript, sizeof file->sfScript);
        if (save_name && !cfp_write(reply + 24, US_TO_SYN68K(ROMlib_carbon_cf_retain(save_name))))
            return 0;
    }
    return navigation_result(call, noErr);
}

static int navigation_modal(ROMlib_carbon_ppc_frame* call, const char* name, int saving)
{
    const uint32_t reply = call->words[1], options = call->words[2];
    if (!reply || !ROMlib_carbon_try_guest_memory(reply, nav_reply_size, 1))
        return navigation_result(call, paramErr);
    if (call->words[0] || call->words[3] || (!saving && (call->words[4] || call->words[5])))
        return navigation_fallback(name, call);

    if (!options)
        return navigation_fallback(name, call);
    const unsigned char* record = ROMlib_carbon_try_guest_memory(options, nav_options_size, 0);
    if (!record)
        return navigation_result(call, paramErr);
    uint32_t flags, position, key, popup;
    if (!cfp_get(options + 2, &flags) || !cfp_get(options + 6, &position) || !cfp_get(options + 0x60a, &key) ||
        !cfp_get(options + 0x60e, &popup))
        return 0;
    const uint32_t required =
        nav_option_no_type_popup | (saving ? nav_option_no_translate_items : nav_option_no_translate);
    uint32_t supported = nav_option_no_type_popup | nav_option_no_translate | nav_option_no_translate_items;
    if (saving)
        supported |= nav_option_previews | nav_option_multiple; /* Open-only flags. */
    if (record[0] || record[1] || position != UINT32_MAX || key || popup || (flags & required) != required ||
        (flags & ~supported) || record[0x00a] || record[0x10a] || record[0x20a] || record[0x30a] ||
        (!saving && record[0x50a]))
        return navigation_fallback(name, call);

    _Alignas(128) StandardFileReply file = {0};
    _Static_assert(sizeof file <= 128, "Standard File reply must fit its guest alias slot");
    nav_types types = {0};
    const int status = navigation_types(saving ? 0 : call->words[6], &types);
    if (status == nav_unavailable)
        return navigation_fallback(name, call);
    if (status)
        return navigation_result(call, status);
    if (!navigation_pick(&file, saving, &types, options + 0x50a, options + 0x40a))
        return 0;
    return navigation_select_reply(call, &file, saving, NULL, 0);
}

static int carbon_NavGetFile(ROMlib_carbon_ppc_frame* call)
{
    return navigation_modal(call, "NavGetFile", 0);
}

static int carbon_NavChooseFile(ROMlib_carbon_ppc_frame* call)
{
    return navigation_modal(call, "NavChooseFile", 0);
}

static int carbon_NavPutFile(ROMlib_carbon_ppc_frame* call)
{
    return navigation_modal(call, "NavPutFile", 1);
}

typedef struct navigation_dialog {
    struct navigation_dialog* next;
    Ptr token;
    nav_types types;
    void* save_name;
    Str255 prompt, filename;
    StandardFileReply result;
    uint32_t action, accepted_action;
    int running, disposed;
} navigation_dialog;

static navigation_dialog* navigation_dialogs;

static navigation_dialog** navigation_link(uint32_t token)
{
    navigation_dialog** link = &navigation_dialogs;
    while (*link && US_TO_SYN68K((*link)->token) != token)
        link = &(*link)->next;
    return link;
}

static void navigation_destroy(navigation_dialog** link)
{
    navigation_dialog* dialog = *link;
    *link = dialog->next;
    ROMlib_carbon_cf_release(dialog->save_name);
    DisposPtr(dialog->token);
    free(dialog);
}

void ROMlib_carbon_navigation_reset(void)
{
    while (navigation_dialogs)
        navigation_destroy(&navigation_dialogs);
}

static int navigation_string(uint32_t address, Str255 output)
{
    output[0] = 0;
    if (!address)
        return 1;
    const void* string = ROMlib_carbon_try_guest_memory(address, 8, 0);
    uint32_t type;
    return string && cfp_call("CFStringGetTypeID", NULL, 0, &type) && ROMlib_carbon_cf_type_id(string) == type &&
           (uint8_t)ROMlib_carbon_cf_string_pascal((void*)string, output, sizeof(Str255), 0);
}

static int navigation_create(ROMlib_carbon_ppc_frame* call, const char* name, uint32_t accepted_action)
{
    const int saving = accepted_action == nav_action_save;
    const uint32_t output = call->words[saving ? 5 : 6], options = call->words[0];
    if (!output || !ROMlib_carbon_try_guest_memory(output, 4, 1))
        return navigation_result(call, paramErr);
    if (!options || call->words[3] || (!saving && (call->words[2] || call->words[4])))
        return navigation_fallback(name, call);
    const unsigned char* bytes = ROMlib_carbon_try_guest_memory(options, 0x32, 0);
    if (!bytes)
        return navigation_result(call, paramErr);

    const uint32_t required =
        nav_option_no_type_popup | (saving ? nav_option_no_translate_items : nav_option_no_translate);
    const uint32_t allowed = nav_option_no_type_popup | nav_option_no_translate | nav_option_no_translate_items |
                             (saving ? nav_option_previews | nav_option_multiple : 0);
    const uint32_t flags = cfp_read(options + 2);
    static const unsigned char app_modal[] = {0, 0, 0, 2, 0, 0, 0, 0};
    if (bytes[0] || bytes[1] || (flags & required) != required || (flags & ~allowed) ||
        cfp_read(options + 6) != UINT32_MAX || memcmp(bytes + 0x2a, app_modal, sizeof app_modal) ||
        cfp_read(options + 0x0a) || cfp_read(options + 0x0e) || cfp_read(options + 0x12) || cfp_read(options + 0x16) ||
        cfp_read(options + 0x22) || cfp_read(options + 0x26) || (!saving && cfp_read(options + 0x1e)))
        return navigation_fallback(name, call);

    nav_types types = {0};
    int status = navigation_types(saving ? 0 : call->words[1], &types);
    if (status == nav_unavailable)
        return navigation_fallback(name, call);
    if (status)
        return navigation_result(call, status);
    _Alignas(256) Str255 filename, prompt;
    uint32_t save_name = saving ? cfp_read(options + 0x1a) : 0;
    if (!navigation_string(save_name, filename) || !navigation_string(cfp_read(options + 0x1e), prompt))
        return navigation_fallback(name, call);
    if (!cfp_write(output, 0))
        return 0;

    navigation_dialog* dialog = calloc(1, sizeof *dialog);
    if (!dialog)
        return navigation_result(call, memFullErr);
    dialog->token = NewPtr(4);
    if (!dialog->token) {
        free(dialog);
        return navigation_result(call, memFullErr);
    }
    dialog->accepted_action = accepted_action;
    dialog->types = types;
    memcpy(dialog->prompt, prompt, prompt[0] + 1);
    memcpy(dialog->filename, filename, filename[0] + 1);
    if (save_name && !(dialog->save_name = ROMlib_carbon_cf_string_copy(0, SYN68K_TO_US_CHECK0(save_name)))) {
        DisposPtr(dialog->token);
        free(dialog);
        return navigation_result(call, memFullErr);
    }
    dialog->next = navigation_dialogs;
    navigation_dialogs = dialog;
    return cfp_write(output, US_TO_SYN68K(dialog->token)) && navigation_result(call, noErr);
}

static int carbon_NavCreateGetFileDialog(ROMlib_carbon_ppc_frame* call)
{
    return navigation_create(call, "NavCreateGetFileDialog", nav_action_open);
}

static int carbon_NavCreateChooseFileDialog(ROMlib_carbon_ppc_frame* call)
{
    return navigation_create(call, "NavCreateChooseFileDialog", nav_action_choose);
}

static int carbon_NavCreatePutFileDialog(ROMlib_carbon_ppc_frame* call)
{
    return navigation_create(call, "NavCreatePutFileDialog", nav_action_save);
}

static int carbon_NavDialogDispose(ROMlib_carbon_ppc_frame* call)
{
    navigation_dialog** link = navigation_link(call->words[0]);
    if (!*link)
        return navigation_saved("NavDialogDispose", call, 0);
    if ((*link)->running)
        (*link)->disposed = 1;
    else
        navigation_destroy(link);
    return 1;
}

static int carbon_NavDialogRun(ROMlib_carbon_ppc_frame* call)
{
    navigation_dialog* dialog = *navigation_link(call->words[0]);
    if (!dialog)
        return navigation_fallback("NavDialogRun", call);
    if (dialog->running || dialog->action)
        return navigation_result(call, nav_wrong_state);

    _Alignas(128) StandardFileReply file = {0};
    _Alignas(256) Str255 filename, prompt;
    memcpy(filename, dialog->filename, dialog->filename[0] + 1);
    memcpy(prompt, dialog->prompt, dialog->prompt[0] + 1);
    const int saving = dialog->accepted_action == nav_action_save;
    dialog->running = 1;
    int ok = navigation_pick(&file, saving, &dialog->types, US_TO_SYN68K(prompt), US_TO_SYN68K(filename));
    dialog->running = 0;
    if (dialog->disposed) {
        navigation_destroy(navigation_link(US_TO_SYN68K(dialog->token)));
        return ok && navigation_result(call, noErr);
    }
    if (!ok)
        return 0;
    if (file.sfGood && saving) {
        if (file.sfFile.name[0] >= sizeof file.sfFile.name)
            return navigation_result(call, paramErr);
        uint32_t name = ROMlib_cfstring_pascal(0, US_TO_SYN68K(&file.sfFile.name), 0);
        if (!name)
            return navigation_result(call, memFullErr);
        ROMlib_carbon_cf_release(dialog->save_name);
        dialog->save_name = SYN68K_TO_US_CHECK0(name);
        memcpy(dialog->filename, file.sfFile.name, file.sfFile.name[0] + 1);
    }
    dialog->result = file;
    dialog->action = file.sfGood ? dialog->accepted_action : nav_action_cancel;
    return navigation_result(call, noErr);
}

static int carbon_NavDialogGetUserAction(ROMlib_carbon_ppc_frame* call)
{
    const navigation_dialog* dialog = *navigation_link(call->words[0]);
    return dialog ? navigation_result(call, dialog->action) : navigation_saved("NavDialogGetUserAction", call, 0);
}

static int carbon_NavDialogGetWindow(ROMlib_carbon_ppc_frame* call)
{
    /* Native dialogs have no persistent WindowRef outside their blocking Standard File call. */
    return *navigation_link(call->words[0]) ? navigation_result(call, 0)
                                            : navigation_saved("NavDialogGetWindow", call, 0);
}

static int carbon_NavDialogGetReply(ROMlib_carbon_ppc_frame* call)
{
    const navigation_dialog* dialog = *navigation_link(call->words[0]);
    if (!dialog)
        return navigation_fallback("NavDialogGetReply", call);
    if (!dialog->action)
        return navigation_result(call, nav_wrong_state);
    if (!call->words[1] || !ROMlib_carbon_try_guest_memory(call->words[1], nav_carbon_reply_size, 1))
        return navigation_result(call, paramErr);
    _Alignas(128) StandardFileReply reply = dialog->result;
    const int saving = dialog->accepted_action == nav_action_save;
    if (saving && reply.sfGood) {
        _Alignas(16) Byte empty[] = {0};
        ROMlib_carbon_ppc_frame create = {0};
        create.words[0] = (int16_t)CW(reply.sfFile.vRefNum);
        create.words[1] = CL(reply.sfFile.parID);
        create.words[2] = US_TO_SYN68K(empty);
        create.words[3] = US_TO_SYN68K(&reply.sfFile);
        if (!ROMlib_carbon_file_make_spec(&create))
            return 0;
        if ((int16_t)create.words[0])
            return navigation_result(call, (int16_t)create.words[0]);
    }
    return navigation_select_reply(call, &reply, saving, dialog->save_name, 1);
}

static int carbon_NavDialogGetSaveFileName(ROMlib_carbon_ppc_frame* call)
{
    const navigation_dialog* dialog = *navigation_link(call->words[0]);
    if (!dialog)
        return navigation_saved("NavDialogGetSaveFileName", call, 0);
    return navigation_result(call, dialog->accepted_action == nav_action_save ? US_TO_SYN68K(dialog->save_name) : 0);
}

static int navigation_set_filename(navigation_dialog* dialog, uint32_t name, const Byte* filename)
{
    void* replacement = name ? ROMlib_carbon_cf_string_copy(0, SYN68K_TO_US_CHECK0(name)) : NULL;
    if (name && !replacement)
        return memFullErr;
    ROMlib_carbon_cf_release(dialog->save_name);
    dialog->save_name = replacement;
    memcpy(dialog->filename, filename, filename[0] + 1);
    return noErr;
}

static int carbon_NavDialogSetSaveFileName(ROMlib_carbon_ppc_frame* call)
{
    navigation_dialog* dialog = *navigation_link(call->words[0]);
    if (!dialog)
        return navigation_fallback("NavDialogSetSaveFileName", call);
    if (dialog->accepted_action != nav_action_save)
        return navigation_result(call, nav_wrong_class);
    if (dialog->running || dialog->action)
        return navigation_result(call, nav_wrong_state);
    _Alignas(256) Str255 filename;
    if (!call->words[1] || !navigation_string(call->words[1], filename))
        return navigation_result(call, paramErr);
    return navigation_result(call, navigation_set_filename(dialog, call->words[1], filename));
}

static int carbon_NavCustomControl(ROMlib_carbon_ppc_frame* call)
{
    navigation_dialog* dialog = *navigation_link(call->words[0]);
    if (!dialog)
        return navigation_fallback("NavCustomControl", call);
    const uint32_t selector = call->words[1], address = call->words[2];
    if (selector == 17) { /* kNavCtlIsPreviewShowing */
        Byte* showing = address ? ROMlib_carbon_try_guest_memory(address, 1, 1) : NULL;
        if (!showing)
            return navigation_result(call, paramErr);
        *showing = 0;
        return navigation_result(call, noErr);
    }
    if (selector != 23 && selector != 24)
        return navigation_result(call, selector <= 30 ? nav_control_failed : nav_invalid_control);
    if (dialog->accepted_action != nav_action_save)
        return navigation_result(call, nav_wrong_class);
    if (selector == 23) { /* kNavCtlGetEditFileName */
        size_t extent = dialog->filename[0] + 1;
        void* output = address ? ROMlib_carbon_try_guest_memory(address, extent, 1) : NULL;
        if (!output)
            return navigation_result(call, paramErr);
        memcpy(output, dialog->filename, extent);
        return navigation_result(call, noErr);
    }
    if (dialog->running || dialog->action)
        return navigation_result(call, nav_wrong_state);
    const Byte* input = address ? ROMlib_carbon_try_guest_memory(address, 1, 0) : NULL;
    if (!input || !(input = ROMlib_carbon_try_guest_memory(address, input[0] + 1, 0)))
        return navigation_result(call, paramErr);
    uint32_t name = ROMlib_cfstring_pascal(0, address, 0);
    if (!name)
        return navigation_result(call, memFullErr);
    int result = navigation_set_filename(dialog, name, input);
    ROMlib_carbon_cf_release(SYN68K_TO_US_CHECK0(name));
    return navigation_result(call, result);
}

#endif
