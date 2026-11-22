/*+===================================================================
  File:      application_instance_test.c

  Summary:   Exercise duplicate launch exclusion, document handoff and abandoned-owner recovery on Windows.

  Classes:   None.

  Functions: main

  Origin:    Regression fixture for Executor's Windows application-instance broker.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "../../host/application_instance.c"
#include <assert.h>

static PROCESS_INFORMATION child(const char* mode, const char* key)
{
    char executable[MAX_PATH], command[2048];
    assert(GetModuleFileNameA(NULL, executable, sizeof executable));
    snprintf(command, sizeof command, "\"%s\" %s %s", executable, mode, key);
    STARTUPINFOA start = {0};
    start.cb = sizeof start;
    PROCESS_INFORMATION process = {0};
    assert(CreateProcessA(executable, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &start, &process));
    CloseHandle(process.hThread);
    return process;
}

static void finish(PROCESS_INFORMATION process)
{
    DWORD start = GetTickCount();
    while (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
        MSG message;
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE))
            DispatchMessageA(&message);
        assert(GetTickCount() - start < 10000);
        Sleep(1);
    }
    DWORD code;
    assert(GetExitCodeProcess(process.hProcess, &code) && !code);
    CloseHandle(process.hProcess);
}

int main(int argc, char** argv)
{
    int reused = 0;
    const char* documents[] = {"C:\\A document", "Recovery:Folder:\xca Mac file"};
    if (argc == 3) {
        if (!strcmp(argv[1], "client")) {
            assert(!ROMlib_instance_claim(argv[2], documents, 2, &reused) && reused);
        }
        else if (!strcmp(argv[1], "activate")) {
            assert(!ROMlib_instance_claim(argv[2], NULL, 0, &reused) && reused);
        }
        else if (!strcmp(argv[1], "other")) {
            assert(!ROMlib_instance_claim(argv[2], NULL, 0, &reused) && !reused);
        }
        else if (!strcmp(argv[1], "abandon")) {
            assert(!ROMlib_instance_claim(argv[2], NULL, 0, &reused) && !reused);
            char event_name[128];
            snprintf(event_name, sizeof event_name, "Local\\%s.ready", argv[2]);
            HANDLE ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, event_name);
            assert(ready && SetEvent(ready));
            CloseHandle(ready);
            Sleep(INFINITE);
        }
        else
            assert(0);
        return 0;
    }
    char key[96], other_key[128];
    snprintf(key, sizeof key, "ROMAW-instance-test-%lu", (unsigned long)GetCurrentProcessId());
    snprintf(other_key, sizeof other_key, "%s-other", key);
    assert(!ROMlib_instance_claim(key, NULL, 0, &reused) && !reused);
    finish(child("client", key));
    uint32_t count = 0;
    char* received = ROMlib_instance_take(&count);
    assert(received && count == 2 && !strcmp(received, documents[0]));
    assert(!strcmp(received + strlen(received) + 1, documents[1]));
    free(received);
    assert(!ROMlib_instance_take(&count));
    finish(child("activate", key));
    assert(!ROMlib_instance_take(&count));
    /* A distinct application is allowed even while this one is held. */
    finish(child("other", other_key));
    assert(ROMlib_instance_claim(NULL, NULL, 0, &reused) == ERROR_INVALID_PARAMETER);
    assert(ROMlib_instance_claim(key, NULL, 1025, &reused) == ERROR_INVALID_PARAMETER);
    uint32_t malformed[] = {2, 0};
    COPYDATASTRUCT copy = {INSTANCE_MESSAGE, sizeof malformed, malformed};
    assert(!SendMessageA(owner_window, WM_COPYDATA, 0, (LPARAM)&copy));
    assert(!ROMlib_instance_take(&count));
    ROMlib_instance_close();

    /* The OS releases ownership even when the process never runs cleanup. */
    char event_name[128];
    snprintf(event_name, sizeof event_name, "Local\\%s.ready", key);
    HANDLE ready = CreateEventA(NULL, TRUE, FALSE, event_name);
    assert(ready);
    PROCESS_INFORMATION process = child("abandon", key);
    assert(WaitForSingleObject(ready, 10000) == WAIT_OBJECT_0);
    assert(TerminateProcess(process.hProcess, 0));
    assert(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0);
    CloseHandle(process.hProcess);
    CloseHandle(ready);
    assert(!ROMlib_instance_claim(key, NULL, 0, &reused) && !reused);
    ROMlib_instance_close();

    char folder[MAX_PATH], first[MAX_PATH], second[MAX_PATH], alias[MAX_PATH + 8];
    assert(GetTempPathA(sizeof folder, folder));
    assert(GetTempFileNameA(folder, "exi", 0, first));
    assert(GetTempFileNameA(folder, "exi", 0, second));
    snprintf(alias, sizeof alias, "%s.link", first);
    assert(CreateHardLinkA(alias, first, NULL));
    char* a = ROMlib_instance_file_identity(first, NULL);
    char* b = ROMlib_instance_file_identity(second, NULL);
    char* c = ROMlib_instance_file_identity(alias, NULL);
    char* d = ROMlib_instance_file_identity(first, "/hfs/1/42");
    assert(a && b && c && d && strcmp(a, b) && !strcmp(a, c) && strcmp(a, d));
    free(a);
    free(b);
    free(c);
    free(d);
    assert(DeleteFileA(alias) && DeleteFileA(first) && DeleteFileA(second));
    puts("PASS: duplicate exclusion, document handoff, distinct identities and crashed-owner recovery");
    return 0;
}
