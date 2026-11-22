/*+===================================================================
  File:      application_instance.c

  Summary:   Serialize classic application launches and hand document requests to the existing Windows process.

  Classes:   None.

  Functions: ROMlib_instance_claim, ROMlib_instance_file_identity, ROMlib_instance_take, ROMlib_instance_close

  Origin:    Executor Windows application launch integration.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "rsys/application_instance.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INSTANCE_CLASS "ROMAW.ApplicationInstance.1"
#define INSTANCE_MESSAGE 0x45584931
#define INSTANCE_LIMIT 65536

typedef struct instance_request {
    struct instance_request* next;
    uint32_t count;
    char paths[];
} instance_request;

static HANDLE owner_mutex;
static HWND owner_window;
static char* owner_identity;
static instance_request *pending_first, *pending_last;
static unsigned pending_count;

char* ROMlib_instance_file_identity(const char* path, const char* suffix)
{
    if (!path || !*path)
        return NULL;
    if (!suffix)
        suffix = "";
    HANDLE file = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS, NULL);
    BY_HANDLE_FILE_INFORMATION info;
    int known = file != INVALID_HANDLE_VALUE && GetFileInformationByHandle(file, &info);
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    if (known) {
        size_t size = strlen(suffix) + 64;
        char* identity = malloc(size);
        if (identity)
            snprintf(identity, size, "%08lx:%08lx:%08lx%s", (unsigned long)info.dwVolumeSerialNumber,
                     (unsigned long)info.nFileIndexHigh, (unsigned long)info.nFileIndexLow, suffix);
        return identity;
    }
    DWORD length = GetFullPathNameA(path, 0, NULL, NULL);
    if (!length || strlen(suffix) > SIZE_MAX - length - 1)
        return NULL;
    char* identity = malloc(length + strlen(suffix) + 1);
    if (!identity)
        return NULL;
    DWORD copied = GetFullPathNameA(path, length, identity, NULL);
    if (!copied || copied >= length) {
        free(identity);
        return NULL;
    }
    CharUpperBuffA(identity, copied);
    strcat(identity, suffix);
    return identity;
}

void ROMlib_instance_close(void)
{
    if (owner_window)
        DestroyWindow(owner_window);
    owner_window = NULL;
    if (owner_mutex) {
        ReleaseMutex(owner_mutex);
        CloseHandle(owner_mutex);
    }
    owner_mutex = NULL;
    free(owner_identity);
    owner_identity = NULL;
    while (pending_first) {
        instance_request* next = pending_first->next;
        free(pending_first);
        pending_first = next;
    }
    pending_last = NULL;
    pending_count = 0;
}

char* ROMlib_instance_take(uint32_t* count)
{
    instance_request* request = pending_first;
    if (!request || !count)
        return NULL;
    pending_first = request->next;
    if (!pending_first)
        pending_last = NULL;
    --pending_count;
    /* Return the allocation base while retaining ownership transfer to caller. */
    *count = request->count;
    size_t length = 0;
    for (uint32_t i = 0; i < request->count; ++i)
        length += strlen(request->paths + length) + 1;
    memmove(request, request->paths, length);
    return (char*)request;
}

static LRESULT CALLBACK instance_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message != WM_COPYDATA)
        return DefWindowProcA(window, message, wparam, lparam);
    const COPYDATASTRUCT* copy = (const COPYDATASTRUCT*)lparam;
    uint32_t count;
    if (!copy || copy->dwData != INSTANCE_MESSAGE || !copy->lpData || copy->cbData < sizeof count ||
        copy->cbData > INSTANCE_LIMIT)
        return 0;
    memcpy(&count, copy->lpData, sizeof count);
    if (count > 1024)
        return 0;
    const char* paths = (const char*)copy->lpData + sizeof count;
    size_t size = copy->cbData - sizeof count, used = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const char* end = memchr(paths + used, 0, size - used);
        if (!end || end == paths + used)
            return 0;
        used = end - paths + 1;
    }
    if (used != size)
        return 0;
    if (!count)
        return 1;
    if (pending_count >= 16)
        return 0;
    instance_request* request = malloc(sizeof *request + size);
    if (!request)
        return 0;
    request->next = NULL;
    request->count = count;
    memcpy(request->paths, paths, size);
    if (pending_last)
        pending_last->next = request;
    else
        pending_first = request;
    pending_last = request;
    ++pending_count;
    return 1;
}

static BOOL CALLBACK foreground_window(HWND window, LPARAM process)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != (DWORD)process || !IsWindowVisible(window) || GetWindow(window, GW_OWNER))
        return TRUE;
    if (IsIconic(window))
        ShowWindow(window, SW_RESTORE);
    SetForegroundWindow(window);
    return FALSE;
}

uint32_t ROMlib_instance_claim(const char* identity, const char* const* documents, uint32_t count, int* reused)
{
    if (!identity || !*identity || !reused || count > 1024 || (count && !documents))
        return ERROR_INVALID_PARAMETER;
    *reused = 0;
    size_t size = sizeof count;
    for (uint32_t i = 0; i < count; ++i) {
        if (!documents[i] || !*documents[i] || strlen(documents[i]) >= INSTANCE_LIMIT - size)
            return ERROR_INVALID_PARAMETER;
        size += strlen(documents[i]) + 1;
    }
    if (owner_identity && !strcmp(owner_identity, identity))
        return ERROR_SUCCESS;
    unsigned char* payload = malloc(size);
    if (!payload)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(payload, &count, sizeof count);
    size_t used = sizeof count;
    for (uint32_t i = 0; i < count; ++i) {
        size_t length = strlen(documents[i]) + 1;
        memcpy(payload + used, documents[i], length);
        used += length;
    }
    /* Two independent stable hashes keep kernel object names bounded. The full
       identity is also kept locally to recognize a same-process relaunch. */
    uint64_t a = UINT64_C(14695981039346656037), b = UINT64_C(7809847782465536322);
    for (const unsigned char* p = (const unsigned char*)identity; *p; ++p) {
        a = (a ^ *p) * UINT64_C(1099511628211);
        b = (b ^ *p) * UINT64_C(14029467366897019727);
    }
    char key[96];
    snprintf(key, sizeof key, "Local\\ROMAW.App.%016llx.%016llx", (unsigned long long)a, (unsigned long long)b);
    HANDLE mutex = CreateMutexA(NULL, FALSE, key);
    if (!mutex) {
        free(payload);
        return GetLastError();
    }
    DWORD error = ERROR_TIMEOUT;
    for (unsigned attempt = 0; attempt < 200; ++attempt) {
        DWORD acquired = WaitForSingleObject(mutex, 0);
        if (acquired == WAIT_OBJECT_0 || acquired == WAIT_ABANDONED) {
            WNDCLASSA wc = {0};
            wc.lpfnWndProc = instance_proc;
            wc.hInstance = GetModuleHandleA(NULL);
            wc.lpszClassName = INSTANCE_CLASS;
            if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                error = GetLastError();
                ReleaseMutex(mutex);
                break;
            }
            char* saved = _strdup(identity);
            HWND window =
                saved ? CreateWindowExA(0, INSTANCE_CLASS, key, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL)
                      : NULL;
            if (!window) {
                error = saved ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
                free(saved);
                ReleaseMutex(mutex);
                break;
            }
            ROMlib_instance_close();
            owner_mutex = mutex;
            owner_window = window;
            owner_identity = saved;
            static int registered;
            if (!registered) {
                atexit(ROMlib_instance_close);
                registered = 1;
            }
            free(payload);
            return ERROR_SUCCESS;
        }
        if (acquired == WAIT_FAILED) {
            error = GetLastError();
            break;
        }
        HWND existing = FindWindowExA(HWND_MESSAGE, NULL, INSTANCE_CLASS, key);
        if (existing) {
            DWORD process = 0;
            GetWindowThreadProcessId(existing, &process);
            AllowSetForegroundWindow(process);
            if (!count) {
                EnumWindows(foreground_window, process);
                *reused = 1;
                error = ERROR_SUCCESS;
                break;
            }
            COPYDATASTRUCT copy = {INSTANCE_MESSAGE, (DWORD)size, payload};
            DWORD_PTR accepted = 0;
            if (SendMessageTimeoutA(existing, WM_COPYDATA, 0, (LPARAM)&copy, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000,
                                    &accepted) &&
                accepted) {
                EnumWindows(foreground_window, process);
                *reused = 1;
                error = ERROR_SUCCESS;
                break;
            }
            error = GetLastError();
            if (!error)
                error = ERROR_NOT_READY;
            DWORD departed = WaitForSingleObject(mutex, 0);
            if (departed == WAIT_OBJECT_0 || departed == WAIT_ABANDONED) {
                ReleaseMutex(mutex);
                continue;
            }
            break;
        }
        Sleep(10);
    }
    CloseHandle(mutex);
    free(payload);
    return error;
}
