/*+===================================================================
  File:      process_client_test.c

  Summary:   Check process identity, system-mode nesting and per-process input script restrictions.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* System-mode nesting must select the real process or the system process. */
#define NDEBUG 1
#include "rsys/common.h"
#include "../../runtime/process.c"
#undef main
#undef NDEBUG
#include <assert.h>
#include <stddef.h>
#include "OSEvent.h"

_Static_assert(sizeof(EntityName) == 102, "AppleTalk entity ABI");
_Static_assert(sizeof(PPCPortRec) == 72, "PPC port ABI");
_Static_assert(offsetof(PPCPortRec, portKindsSelector) == 36, "PPC port selector ABI");
_Static_assert(sizeof(LocationNameRec) == 104, "PPC location ABI");
_Static_assert(offsetof(TargetID, location) == 76, "HLE sender location ABI");
_Static_assert(sizeof(TargetID) == 252, "HLE sender ABI");

_Static_assert(offsetof(AE_info_t, size) == 2, "ExpandMem size ABI");
_Static_assert(offsetof(AE_info_t, process_manager_exists) == 0x128, "Process Manager flag ABI");

_Static_assert(offsetof(AE_info_t, min_system_heap_free) == 0x1ae, "System heap reserve ABI");
_Static_assert(sizeof(AE_info_t) == 596, "ExpandMem extent ABI");
_Static_assert(sizeof(ROMlib_task_info) == 10, "legacy TaskInfoRec ABI");
_Static_assert(sizeof(ROMlib_legacy_process_info) == 80, "legacy procinfo ABI");
_Static_assert(offsetof(ROMlib_legacy_process_info, parent) == 28, "legacy procinfo Boolean alignment");
_Static_assert(offsetof(ROMlib_legacy_process_info, name) == 46, "legacy procinfo name offset");

int main(void)
{
    process_info_t process = {0};
    ProcessSerialNumber serial;
    assert(ROMlib_get_active_task_info(NULL, 0) == 0);
    assert(ROMlib_current_process_id() == 0);
    assert(C_GetCurrentProcess(NULL) == paramErr);
    assert(C_GetCurrentProcess(&serial) == procNotFound);
    assert(ROMlib_tsm_use_input_window());
    assert(ROMlib_get_system_client_process(NULL) == paramErr);
    assert(C_GetFrontProcess(NULL, NULL) == paramErr);
    assert(C_GetFrontProcess(&serial, NULL) == procNotFound);
    assert(ROMlib_get_system_client_process(&serial) == noErr);
    assert(!serial.highLongOfPSN && serial.lowLongOfPSN == CLC(kSystemProcess));
    assert(ROMlib_begin_system_mode() == noErr);
    assert(ROMlib_end_system_mode() == noErr);
    process.serial_number.highLongOfPSN = CLC(12);
    process.serial_number.lowLongOfPSN = CLC(34);
    current_process_info = &process;
    process_info_list = &process;
    {
        process_info_t other = {0};
        unsigned char guarded[sizeof(ROMlib_task_info) + 4];
        memset(guarded, 0xa5, sizeof guarded);
        process.identity.pid = CWC(4);
        assert(ROMlib_current_process_id() == 4);
        process.identity.signature = CLC(0x54455354);
        process.identity.version = CLC(0x06188000);
        process.next = &other;
        assert(ROMlib_get_active_task_info(NULL, 0) == 2);
        assert(ROMlib_get_active_task_info((ROMlib_task_info*)(guarded + 2), 1) == 2);
        assert(!memcmp(guarded + 2, "\0\4TEST\6\30\200\0", 10));
        assert(guarded[0] == 0xa5 && guarded[1] == 0xa5);
        assert(guarded[12] == 0xa5 && guarded[13] == 0xa5);
        assert(ROMlib_get_active_task_info((ROMlib_task_info*)guarded, -1) == 2);
        assert(guarded[0] == 0xa5 && guarded[1] == 0xa5);
        other.mode = modeDeskAccessory;
        assert(ROMlib_get_active_task_info(NULL, 0) == 1);
        other.mode = modeOnlyBackground;
        assert(ROMlib_get_active_task_info(NULL, 0) == 1);
        process.next = NULL;
    }
    ROMlib_process_enable_scripts(1U << 7, TRUE);
    assert(process.keyboard_scripts == ((1U << 7) | 1));
    assert(process.keyboard_unicode);
    {
        process_info_t other = {0};
        current_process_info = &other;
        ROMlib_process_enable_scripts(0, FALSE);
        assert(other.keyboard_scripts == 1);
        assert(!other.keyboard_unicode);
        assert(process.keyboard_scripts == ((1U << 7) | 1));
        assert(process.keyboard_unicode);
        current_process_info = &process;
    }
    {
        PPCPortRec port;
        memset(&port, 0xa5, sizeof port);
        assert(C_GetPortNameFromProcessSerialNumber(&port, &current_process) == noPortErr);
        assert(port.name[0] == 0xa5);
        process.mode = modeHighLevelEventAware;
        memcpy(process.event_port.name, "\004Test", 5);
        process.event_port.portKindsSelector = CWC(ppcByString);
        memcpy(process.event_port.u.portTypeStr, "\010TESTep01", 9);
        assert(C_GetPortNameFromProcessSerialNumber(&port, &current_process) == noErr);
        assert(!memcmp(&port, &process.event_port, sizeof port));
        assert(C_GetProcessSerialNumberFromPortName(&port, &serial) == noErr);
        assert(!memcmp(&serial, &process.serial_number, sizeof serial));
        port.name_padding = 0xff;
        port.type_padding = 0xff;
        assert(C_GetProcessSerialNumberFromPortName(&port, &serial) == noErr);
        port.name[1] = 'X';
        assert(C_GetProcessSerialNumberFromPortName(&port, &serial) == procNotFound);
        assert(!memcmp(&serial, &no_process, sizeof serial));
        port.name[0] = 33;
        assert(C_GetProcessSerialNumberFromPortName(&port, &serial) == paramErr);
        assert(C_GetProcessSerialNumberFromPortName(NULL, &serial) == paramErr);
        assert(C_GetPortNameFromProcessSerialNumber(NULL, &serial) == paramErr);
        process.mode = 0;
    }
    assert(!ROMlib_process_take_wakeup());
    assert(C_WakeUpProcess(NULL) == paramErr);
    serial = no_process;
    assert(C_WakeUpProcess(&serial) == procNotFound);
    assert(!ROMlib_process_take_wakeup());
    assert(C_WakeUpProcess(&current_process) == noErr);
    assert(ROMlib_process_take_wakeup());
    assert(!ROMlib_process_take_wakeup());
    assert(C_WakeUpProcess(&process.serial_number) == noErr);
    assert(ROMlib_process_take_wakeup());
    assert(ROMlib_tsm_use_input_window());
    assert(ROMlib_tsm_registration(TRUE) == noErr);
    assert(!ROMlib_tsm_use_input_window());
    assert(ROMlib_tsm_registration(FALSE) == noErr);
    assert(ROMlib_tsm_use_input_window());
    assert(C_GetFrontProcess(&serial, NULL) == noErr);
    assert(!memcmp(&serial, &process.serial_number, sizeof serial));
    assert(ROMlib_get_system_client_process(&serial) == noErr);
    assert(!memcmp(&serial, &process.serial_number, sizeof serial));
    assert(ROMlib_begin_system_mode() == noErr);
    assert(ROMlib_begin_system_mode() == noErr);
    assert(process.system_mode_level == 2);
    assert(ROMlib_get_system_client_process(&serial) == noErr);
    assert(!serial.highLongOfPSN && serial.lowLongOfPSN == CLC(kSystemProcess));
    assert(ROMlib_end_system_mode() == noErr);
    assert(process.system_mode_level == 1);
    assert(ROMlib_get_system_client_process(&serial) == noErr);
    assert(serial.lowLongOfPSN == CLC(kSystemProcess));
    assert(ROMlib_end_system_mode() == noErr);
    assert(ROMlib_end_system_mode() == noErr);
    assert(!process.system_mode_level);
    assert(ROMlib_get_system_client_process(&serial) == noErr);
    assert(!memcmp(&serial, &process.serial_number, sizeof serial));
    process.system_mode_level = UINT32_MAX;
    assert(ROMlib_begin_system_mode() == paramErr);
    assert(process.system_mode_level == UINT32_MAX);
    puts("System client selection, nesting, underflow and overflow passed");
    return 0;
}
