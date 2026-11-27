/*+===================================================================
  File:      process.c

  Summary:   Implements process support for Macintosh startup, traps and runtime services.

  Classes:   None.

  Functions: ROMlib_get_legacy_process_info, ROMlib_get_active_task_info, ROMlib_begin_system_mode,
             ROMlib_end_system_mode, ROMlib_get_system_client_process,
             ROMlib_process_identity, ROMlib_process_enable_scripts, ROMlib_process_take_wakeup, ROMlib_tsm_registration, ROMlib_tsm_use_input_window,
             ROMlib_tsm_new_document, ROMlib_tsm_delete_document, ROMlib_tsm_set_active, ROMlib_tsm_active_document,
             ROMlib_tsm_fix_document, ROMlib_tsm_set_input_window, ROMlib_tsm_document_input_window,
             ROMlib_tsm_dispose_documents,
             get_process_info, process_create

  Origin:    Existing repository source in executor/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "AppleEvents.h"

#include "ProcessMgr.h"
#include "ResourceMgr.h"
#include "MemoryMgr.h"
#include "ToolboxEvent.h"

#include "rsys/mman.h"
#include "rsys/process.h"
#include "rsys/launch.h"
#include "rsys/desk.h"
#include "SegmentLdr.h"

#define declare_handle_type(type_prefix)        \
    typedef type_prefix##_t* type_prefix##_ptr; \
    MAKE_HIDDEN(type_prefix##_ptr);             \
    typedef HIDDEN_##type_prefix##_ptr* type_prefix##_handle

declare_handle_type(size_resource);

#define SIZE_FLAGS_X(size) (HxX(size, flags))
#define SIZE_FLAGS(size) (CW(SIZE_FLAGS_X(size)))

static size_resource_handle get_size_resource()
{
    Handle size;

    size = Get1Resource(T('S', 'I', 'Z', 'E'), 0);
    if (size == NULL)
        size = Get1Resource(T('S', 'I', 'Z', 'E'), -1);
    return (size_resource_handle)size;
}

typedef struct PACKED process_identity {
    int16 state, pid;
    uint32 type, signature;
    uint16 name_script;
    uint8 scrap_from_launch, alignment;
    uint32 version, next, state_next, state_previous, queued_front;
    ProcessSerialNumber serial;
    uint32 zone;
} process_identity;

_Static_assert(offsetof(process_identity, serial) == 0x24, "process identity ABI");

typedef struct tsm_document {
    Handle id;
    BOOLEAN input_window, input_window_override;
    struct tsm_document* next;
} tsm_document;

typedef struct process_info {
    process_identity identity;
    struct process_info* next;

    uint32 mode;
    uint32 type;
    uint32 signature;
    uint32 size;
    uint32 launch_ticks;
    uint32 stack_size;

    ProcessSerialNumber serial_number;
    FSSpec app_spec;
    OSErr spec_error;
    PPCPortRec event_port;
    BOOLEAN tsm_registered;
    BOOLEAN tsm_input_window;
    tsm_document* tsm_documents;
    Handle tsm_active;
    uint32 system_mode_level;
    BOOLEAN wake_pending;
    uint32 keyboard_scripts;
    BOOLEAN keyboard_unicode;
} process_info_t;

static process_info_t* process_info_list;
static process_info_t* current_process_info;

static const int default_process_mode_flags;

static ProcessSerialNumber no_process = {0, CLC(kNoProcess)};
static ProcessSerialNumber current_process = {0, CLC(kCurrentProcess)};

LONGINT ROMlib_get_legacy_process_info(INTEGER descriptor, ROMlib_legacy_process_info* result)
{
    /* ProcessMgr/OSDispatchPrivate.h and OSDispatch.c: the old process record
       is 80 bytes with a two-byte ProcessID and alignment after three Booleans. */
    process_info_t* info;
    for (info = process_info_list; info; info = info->next)
        if (CW(info->identity.pid) == descriptor)
            break;
    if (!info)
        return procNotFound;
    if (!result)
        return paramErr;
    ROMlib_legacy_process_info record = {0};
    record.state = info->identity.state;
    record.descriptor = info->identity.pid;
    record.type = CL(info->type);
    record.signature = info->identity.signature;
    record.version = info->identity.version;
    record.zone = info->identity.zone;
    record.mode = CL(info->mode);
    record.suspend_resume = (info->mode & modeNeedSuspendResume) != 0;
    record.background = (info->mode & modeCanBackground) != 0;
    record.activate = (info->mode & modeDoesActivateOnFGSwitch) != 0;
    record.size = CL(info->size);
    record.stack_size = CL(info->stack_size);
    /* ROMAW does not time-slice applications within a host process. */
    record.slices = 0;
    ZONE_SAVE_EXCURSION((THz)(uintptr_t)info->identity.zone, { record.free_memory = CL(FreeMem()); });
    unsigned length = MIN(info->app_spec.name[0], 31);
    record.name[0] = length;
    memcpy(record.name + 1, info->app_spec.name + 1, length);
    record.volume = info->app_spec.vRefNum;
    *result = record;
    return noErr;
}

INTEGER ROMlib_get_active_task_info(ROMlib_task_info* tasks, INTEGER slots)
{
    /* ProcessMgr/Puppet.c: count every eligible process, even when the
       supplied table has no room. TaskInfoRec has a two-byte ProcessID. */
    INTEGER count = 0;
    INTEGER filled = 0;
    for (process_info_t* info = process_info_list; info; info = info->next) {
        if (info->mode & (modeDeskAccessory | modeOnlyBackground))
            continue;
        if (tasks && filled < slots) {
            tasks[filled].descriptor = info->identity.pid;
            tasks[filled].signature = info->identity.signature;
            tasks[filled].version = info->identity.version;
            ++filled;
        }
        if (count < INT16_MAX)
            ++count;
    }
    return count;
}

OSErr ROMlib_get_system_client_process(ProcessSerialNumber* serial)
{
    if (!serial)
        return paramErr;
    if (current_process_info && !current_process_info->system_mode_level)
        *serial = current_process_info->serial_number;
    else {
        serial->highLongOfPSN = 0;
        serial->lowLongOfPSN = CL(kSystemProcess);
    }
    return noErr;
}

OSErr ROMlib_begin_system_mode(void)
{
    if (current_process_info) {
        if (current_process_info->system_mode_level == UINT32_MAX)
            return paramErr;
        ++current_process_info->system_mode_level;
    }
    return noErr;
}

OSErr ROMlib_end_system_mode(void)
{
    if (current_process_info && current_process_info->system_mode_level)
        --current_process_info->system_mode_level;
    return noErr;
}

void process_create(boolean_t desk_accessory_p, uint32 type, uint32 signature)
{
    size_resource_handle size;
    process_info_t* info;
    static uint32 next_free_psn = 4;

    size = get_size_resource();

    ZONE_SAVE_EXCURSION(SysZone, { info = (process_info_t*)NewPtr(sizeof *info); });

    /* ### we are seriously fucked */
    if (info == NULL)
        gui_fatal("unable to allocate process info record");

    memset(info, 0, sizeof *info);
    info->keyboard_scripts = UINT32_MAX;
    info->keyboard_unicode = TRUE;
    memset(&info->app_spec, 0, sizeof info->app_spec);
    info->spec_error = FSMakeFSSpec(ROMlib_exevrefnum, 0, CurApName, &info->app_spec);
    info->mode = ((size ? SIZE_FLAGS(size) : default_process_mode_flags) | (desk_accessory_p ? modeDeskAccessory : 0));
    info->type = type;
    info->signature = signature;
    /* ProcessMgr/Eppc.c uses the application name and signature + "ep01" for
     the default local high-level-event port. Preserve this launch identity. */
    unsigned name_length = MIN((unsigned)CurApName[0], 32);
    info->event_port.name[0] = name_length;
    memcpy(info->event_port.name + 1, CurApName + 1, name_length);
    info->event_port.portKindsSelector = CWC(ppcByString);
    info->event_port.u.portTypeStr[0] = 8;
    uint32 port_signature = CL(signature);
    memcpy(info->event_port.u.portTypeStr + 1, &port_signature, 4);
    memcpy(info->event_port.u.portTypeStr + 5, "ep01", 4);

    /* ### fixme; major bogosity */
    info->size = (zone_size(MR(ApplZone))
                  /* + A5 world size */
                  /* + stack size */);
    info->launch_ticks = TickCount();
    info->stack_size = CL(DefltStack);

    info->serial_number.highLongOfPSN = CL(-1);
    info->serial_number.lowLongOfPSN = CL(next_free_psn++);

    info->identity.state = CWC(4); /* Current foreground process. */
    info->identity.pid = CW(next_free_psn - 1);
    info->identity.type = CL(type);
    info->identity.signature = CL(signature);
    info->identity.version = CL(ROMlib_version_long);
    info->identity.serial = info->serial_number;
    info->identity.zone = (uint32)(uintptr_t)ApplZone;
    info->identity.next = process_info_list ? CL(US_TO_SYN68K(process_info_list)) : 0;
    info->next = process_info_list;
    process_info_list = info;

    /* ### hack */
    current_process_info = info;
    /* The Process Manager is now usable. Keep ExpandMem's public boot flag
     consistent with GetCurrentProcess/GetProcessInformation. */
    if (MR(AE_info))
        MR(AE_info)->process_manager_exists = CWC(1);
}

void ROMlib_process_enable_scripts(uint32 mask, BOOLEAN unicode)
{
    if (current_process_info) {
        current_process_info->keyboard_scripts = mask | 1;
        current_process_info->keyboard_unicode = unicode;
    }
}

process_info_t* get_process_info(ProcessSerialNumber* serial_number)
{
    process_info_t* t;

    if (PSN_EQ_P(*serial_number, current_process))
        return current_process_info;

    for (t = process_info_list; t; t = t->next) {
        if (PSN_EQ_P(*serial_number, t->serial_number))
            return t;
    }
    return NULL;
}

P1(PUBLIC pascal trap, OSErr, GetCurrentProcess, ProcessSerialNumber*, serial_number)
{
    if (!serial_number)
        return paramErr;
    if (!current_process_info)
        return procNotFound;
    *serial_number = current_process_info->serial_number;
    return noErr;
}

P1(PUBLIC pascal trap, OSErr, GetNextProcess, ProcessSerialNumber*, serial_number)
{
    if (!serial_number)
        return paramErr;
    process_info_t* t;

    if (PSN_EQ_P(*serial_number, no_process)) {
        if (!process_info_list)
            return procNotFound;
        else {
            *serial_number = process_info_list->serial_number;
            return noErr;
        }
    }

    t = get_process_info(serial_number);
    if (t == NULL)
        return paramErr;
    else if (t->next == NULL) {
        memset(serial_number, 0, sizeof *serial_number);
        return procNotFound;
    }
    else {
        *serial_number = t->next->serial_number;
        return noErr;
    }
}

P2(PUBLIC pascal trap, OSErr, GetProcessInformation, ProcessSerialNumber*, serial_number, ProcessInfoPtr, process_info)
{
    process_info_t* info;
    int32 current_ticks;

    if (!serial_number || !process_info || PROCESS_INFO_LENGTH(process_info) < sizeof *process_info)
        return paramErr;
    info = get_process_info(serial_number);
    if (info == NULL)
        return paramErr;

    if (PP_RAW(process_info->processAppSpec)) {
        if (info->spec_error != noErr)
            return info->spec_error;
        *PPR(process_info->processAppSpec) = info->app_spec;
    }
    if (PP_RAW(process_info->processName)) {
        unsigned char* name = (unsigned char*)PPR(process_info->processName);
        memcpy(name, info->app_spec.name, info->app_spec.name[0] + 1);
    }
    PROCESS_INFO_SERIAL_NUMBER(process_info) = info->serial_number;
    PROCESS_INFO_TYPE_X(process_info) = CL(info->type);
    PROCESS_INFO_SIGNATURE_X(process_info) = CL(info->signature);
    PROCESS_INFO_MODE_X(process_info) = CL(info->mode);
    FIELD_ASSIGN_RAW(PROCESS_INFO_LOCATION_X(process_info), (Ptr)ApplZone);
    PROCESS_INFO_SIZE_X(process_info) = CL(info->size);

    /* ### set current zone to applzone? */
    PROCESS_INFO_FREE_MEM_X(process_info) = CL(FreeMem());

    PROCESS_INFO_LAUNCHER(process_info) = no_process;

    PROCESS_INFO_LAUNCH_DATE_X(process_info) = CL(info->launch_ticks);
    current_ticks = TickCount();
    PROCESS_INFO_ACTIVE_TIME_X(process_info) = CL(current_ticks - info->launch_ticks);

    return noErr;
}

P3(PUBLIC pascal trap, OSErr, SameProcess, ProcessSerialNumber*, serial_number0, ProcessSerialNumber*, serial_number1,
   Boolean*, same_out)
{
    process_info_t *info0, *info1;

    if (!serial_number0 || !serial_number1 || !same_out)
        return paramErr;

    info0 = get_process_info(serial_number0);
    info1 = get_process_info(serial_number1);

    if (info0 == NULL || info1 == NULL)
        return paramErr;

    *same_out = (info0 == info1);
    return noErr;
}

P2(PUBLIC pascal trap, OSErr, GetFrontProcess, ProcessSerialNumber*, serial_number, void*, dummy)
{
    if (!serial_number)
        return paramErr;
    if (!current_process_info)
        return procNotFound;
    *serial_number = current_process_info->serial_number;
    return noErr;
}

P1(PUBLIC pascal trap, OSErr, SetFrontProcess, ProcessSerialNumber*, serial_number)
{
    warning_unimplemented(NULL_STRING);
    return paramErr;
}

P1(PUBLIC pascal trap, OSErr, WakeUpProcess, ProcessSerialNumber*, serial_number)
{
    process_info_t* process;
    if (!serial_number)
        return paramErr;
    process = get_process_info(serial_number);
    if (!process)
        return procNotFound;
    process->wake_pending = TRUE;
    return noErr;
}

BOOLEAN
ROMlib_process_take_wakeup(void)
{
    BOOLEAN pending = current_process_info && current_process_info->wake_pending;
    if (current_process_info)
        current_process_info->wake_pending = FALSE;
    return pending;
}

P2(PUBLIC pascal trap, OSErr, GetProcessSerialNumberFromPortName, PPCPortPtr, port_name, ProcessSerialNumber*,
   serial_number)
{
    if (!port_name || !serial_number)
        return paramErr;
    *serial_number = no_process;
    if (port_name->name[0] > 32 ||
        (port_name->portKindsSelector == CWC(ppcByString) && port_name->u.portTypeStr[0] > 32))
        return paramErr;
    for (process_info_t* info = process_info_list; info; info = info->next) {
        const PPCPortRec* port = &info->event_port;
        if (!(info->mode & modeHighLevelEventAware) || port_name->nameScript != port->nameScript ||
            port_name->portKindsSelector != port->portKindsSelector || port_name->name[0] != port->name[0] ||
            memcmp(port_name->name, port->name, port->name[0] + 1))
            continue;
        if (port->portKindsSelector == CWC(ppcByString)
                ? (port_name->u.portTypeStr[0] == port->u.portTypeStr[0] &&
                   !memcmp(port_name->u.portTypeStr, port->u.portTypeStr, port->u.portTypeStr[0] + 1))
                : (port_name->u.port.creator == port->u.port.creator && port_name->u.port.type == port->u.port.type)) {
            *serial_number = info->serial_number;
            return noErr;
        }
    }
    return procNotFound;
}

P2(PUBLIC pascal trap, OSErr, GetPortNameFromProcessSerialNumber, PPCPortPtr, port_name, ProcessSerialNumber*,
   serial_number)
{
    if (!port_name || !serial_number)
        return paramErr;
    process_info_t* info = get_process_info(serial_number);
    if (!info || !(info->mode & modeHighLevelEventAware))
        return noPortErr;
    *port_name = info->event_port;
    return noErr;
}

/* ### temp memory spew; these go elsewhere */

P2(PUBLIC pascal trap, OSErr, LaunchDeskAccessory, FSSpec*, file, StringPtr, name)
{
    return ROMlib_launch_desk_accessory(file, name);
}

/* The leading identity fields are shared with the classic Process Manager.
   They belong to the live process record used by the public APIs. The rest
   of Apple's private PEntry/PCB layout is not exposed or implemented here. */
PUBLIC void* ROMlib_process_identity(void)
{
    return current_process_info ? &current_process_info->identity : NULL;
}

PUBLIC OSErr ROMlib_tsm_registration(BOOLEAN registering)
{
    if (!current_process_info)
        return -2502; /* tsmNotAnAppErr */
    if (registering && current_process_info->tsm_registered)
        return -2503;
    if (!registering && !current_process_info->tsm_registered)
        return -2504;
    if (!registering && current_process_info->tsm_documents)
        return -2511; /* tsmDocumentOpenErr */
    current_process_info->tsm_registered = registering;
    if (!registering)
        current_process_info->tsm_input_window = FALSE;
    return noErr;
}

static tsm_document** find_tsm_document(Handle id)
{
    if (!current_process_info)
        return NULL;
    tsm_document** entry = &current_process_info->tsm_documents;
    while (*entry && (*entry)->id != id)
        entry = &(*entry)->next;
    return entry;
}

static BOOLEAN tsm_span(const void* pointer, size_t length)
{
    if (!pointer)
        return FALSE;
    uintptr_t address = (uintptr_t)pointer, base = (uintptr_t)ROMlib_offset;
    return address < base || address - base > ROMlib_guest_memory_size ||
           length <= ROMlib_guest_memory_size - (address - base);
}

PUBLIC OSErr ROMlib_tsm_new_document(INTEGER count, const OSType* types, HIDDEN_Handle* result, LONGINT refcon)
{
    if (!tsm_span(result, sizeof *result))
        return paramErr;
    HIDDEN_RAW_SET(*result, 0);
    if (count != 1 || !tsm_span(types, sizeof *types))
        return paramErr;
    if (!current_process_info || !current_process_info->tsm_registered)
        return -2504; /* tsmNeverRegisteredErr */
    OSType interface_type;
    memcpy(&interface_type, types, sizeof interface_type);
    if (CL(interface_type) != TICK("tsvc"))
        return -2518; /* tsmUnsupportedTypeErr */
    tsm_document* document = calloc(1, sizeof *document);
    if (!document)
        return memFullErr;
    document->id = NewHandleClear(sizeof(LONGINT));
    if (!document->id) {
        free(document);
        return memFullErr;
    }
    *(LONGINT*)STARH(document->id) = CL(refcon);
    document->next = current_process_info->tsm_documents;
    current_process_info->tsm_documents = document;
    HIDDEN_RAW_SET(*result, RM(document->id));
    /* The Roman keyboard has no input method. This result still supplies a
       valid document ID, as specified by Inside Macintosh: Text. */
    return -2517; /* tsmScriptHasNoIMErr */
}

PUBLIC OSErr ROMlib_tsm_delete_document(Handle id)
{
    if (!current_process_info || !current_process_info->tsm_registered)
        return -2504;
    tsm_document** entry = find_tsm_document(id);
    if (!id || !entry || !*entry)
        return -2505; /* tsmInvalidDocIDErr */
    tsm_document* document = *entry;
    *entry = document->next;
    if (current_process_info->tsm_active == id)
        current_process_info->tsm_active = NULL;
    DisposHandle(document->id);
    free(document);
    return noErr;
}

PUBLIC OSErr ROMlib_tsm_set_active(Handle id, BOOLEAN active)
{
    tsm_document** entry = find_tsm_document(id);
    if (!id || !entry || !*entry)
        return -2505;
    if (active)
        current_process_info->tsm_active = id;
    else if (current_process_info->tsm_active == id)
        current_process_info->tsm_active = NULL;
    return noErr;
}

PUBLIC Handle ROMlib_tsm_active_document(void)
{
    return current_process_info ? current_process_info->tsm_active : NULL;
}

PUBLIC OSErr ROMlib_tsm_fix_document(Handle id)
{
    tsm_document** entry = find_tsm_document(id);
    if (!id || !entry || !*entry)
        return -2505;
    return current_process_info->tsm_active == id ? -2514 : -2507;
}

PUBLIC OSErr ROMlib_tsm_set_input_window(Handle id, BOOLEAN use)
{
    if (!current_process_info || !current_process_info->tsm_registered)
        return -2504;
    if (!id) {
        current_process_info->tsm_input_window = use != 0;
        return noErr;
    }
    tsm_document** entry = find_tsm_document(id);
    if (!entry || !*entry)
        return -2505;
    (*entry)->input_window_override = TRUE;
    (*entry)->input_window = use != 0;
    return noErr;
}

PUBLIC BOOLEAN ROMlib_tsm_document_input_window(Handle id)
{
    if (id) {
        tsm_document** entry = find_tsm_document(id);
        if (!entry || !*entry)
            return FALSE;
        if ((*entry)->input_window_override)
            return (*entry)->input_window;
    }
    return !current_process_info || !current_process_info->tsm_registered || current_process_info->tsm_input_window;
}

PUBLIC BOOLEAN ROMlib_tsm_use_input_window(void)
{
    return ROMlib_tsm_document_input_window(NULL);
}

PUBLIC void ROMlib_tsm_dispose_documents(void)
{
    if (!current_process_info)
        return;
    while (current_process_info->tsm_documents) {
        tsm_document* document = current_process_info->tsm_documents;
        current_process_info->tsm_documents = document->next;
        DisposHandle(document->id);
        free(document);
    }
    current_process_info->tsm_active = NULL;
    current_process_info->tsm_input_window = FALSE;
    current_process_info->tsm_registered = FALSE;
}
