/*+===================================================================
  File:      afpChooser.c

  Summary:   Native AFP over TCP server discovery and volume selection.

  Classes:   None.

  Functions: ROMlib_finder_afp_chooser.

  Origin:    ROMAW AFP integration.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#define UNICODE
#define _UNICODE
#include <winsock2.h>
#include <windows.h>
#include <windns.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "rsys/host_afp.h"
#include "rsys/configprefs.h"
#include "rsys/host_mactcp.h"
#include "rsys/user_profile.h"
#include "finder/image_volumes.h"

#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

enum { servers_id = 100, address_id, connect_id, volumes_id, server_limit = 64 };

static const wchar_t service[] = L"_afpovertcp._tcp.local";
static SRWLOCK server_lock = SRWLOCK_INIT;
static wchar_t servers[server_limit][256];
static unsigned server_count;
static unsigned server_revision;
static HMODULE dns_module;
static DNS_SERVICE_CANCEL browse_cancel;
static DNS_STATUS(WINAPI* resolve_service)(PDNS_SERVICE_RESOLVE_REQUEST, PDNS_SERVICE_CANCEL);
static VOID(WINAPI* free_instance)(PDNS_SERVICE_INSTANCE);
static VOID(WINAPI* free_records)(PDNS_RECORD, DNS_FREE_TYPE);

static VOID WINAPI browse_result(DWORD status, PVOID context, PDNS_RECORD records)
{
    (void)context;
    if (status == ERROR_SUCCESS) {
        AcquireSRWLockExclusive(&server_lock);
        for (PDNS_RECORD record = records; record; record = record->pNext) {
            if (record->wType != DNS_TYPE_PTR || !record->pName || _wcsicmp(record->pName, service) ||
                !record->Data.PTR.pNameHost)
                continue;
            const wchar_t* name = record->Data.PTR.pNameHost;
            if (wcslen(name) >= 256)
                continue;
            unsigned i = 0;
            while (i < server_count && _wcsicmp(servers[i], name))
                ++i;
            if (!record->dwTtl && i < server_count) {
                memmove(servers + i, servers + i + 1, (--server_count - i) * sizeof servers[0]);
                ++server_revision;
            }
            else if (record->dwTtl && i == server_count && server_count < server_limit) {
                wcscpy(servers[server_count++], name);
                ++server_revision;
            }
        }
        ReleaseSRWLockExclusive(&server_lock);
    }
    if (records)
        free_records(records, DnsFreeRecordList);
}

static void start_discovery(void)
{
    if (dns_module)
        return;
    dns_module = LoadLibraryW(L"dnsapi.dll");
    if (!dns_module)
        return;
    DNS_STATUS(WINAPI * browse_service)(PDNS_SERVICE_BROWSE_REQUEST, PDNS_SERVICE_CANCEL);
    browse_service = (void*)GetProcAddress(dns_module, "DnsServiceBrowse");
    resolve_service = (void*)GetProcAddress(dns_module, "DnsServiceResolve");
    free_instance = (void*)GetProcAddress(dns_module, "DnsServiceFreeInstance");
    free_records = (void*)GetProcAddress(dns_module, "DnsFree");
    if (browse_service && resolve_service && free_instance && free_records) {
        static DNS_SERVICE_BROWSE_REQUEST request;
        request.Version = DNS_QUERY_REQUEST_VERSION1;
        request.QueryName = service;
        request.pBrowseCallback = browse_result;
        browse_service(&request, &browse_cancel);
    }
}

struct resolution {
    LONG users;
    HANDLE ready;
    DNS_SERVICE_CANCEL cancel;
    DNS_SERVICE_RESOLVE_REQUEST request;
    wchar_t name[256];
    uint32_t address;
    uint16_t port;
};

static void release_resolution(struct resolution* result)
{
    if (!InterlockedDecrement(&result->users)) {
        CloseHandle(result->ready);
        free(result);
    }
}

static VOID WINAPI resolved(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance)
{
    struct resolution* result = context;
    if (status == ERROR_SUCCESS && instance && instance->ip4Address) {
        result->address = ntohl(*instance->ip4Address);
        result->port = instance->wPort;
    }
    if (instance)
        free_instance(instance);
    SetEvent(result->ready);
    release_resolution(result);
}

static int resolve_advertisement(const wchar_t* name, uint32_t* address, uint16_t* port)
{
    struct resolution* result = calloc(1, sizeof *result);
    if (!result)
        return 0;
    result->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!result->ready) {
        free(result);
        return 0;
    }
    result->users = 2;
    wcscpy(result->name, name);
    result->request.Version = DNS_QUERY_REQUEST_VERSION1;
    result->request.QueryName = result->name;
    result->request.pResolveCompletionCallback = resolved;
    result->request.pQueryContext = result;
    int success = 0;
    if (resolve_service(&result->request, &result->cancel) != DNS_REQUEST_PENDING)
        release_resolution(result);
    else if (WaitForSingleObject(result->ready, 2000) == WAIT_OBJECT_0) {
        *address = result->address;
        *port = result->port;
        success = *address && *port;
    }
    release_resolution(result);
    return success;
}

struct chooser {
    wchar_t previous[256];
    wchar_t server_names[server_limit][256];
    unsigned revision;
    uint32_t address;
    uint16_t port;
    romaw_afp_volume_list volumes;
};

static void update_servers(HWND dialog, struct chooser* state)
{
    HWND list = GetDlgItem(dialog, servers_id);
    AcquireSRWLockShared(&server_lock);
    unsigned revision = server_revision;
    unsigned count = server_count;
    if (state->revision != revision)
        memcpy(state->server_names, servers, count * sizeof servers[0]);
    ReleaseSRWLockShared(&server_lock);
    if (state->revision == revision)
        return;
    state->revision = revision;
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    if (state->previous[0])
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)state->previous);
    for (unsigned i = 0; i < count; ++i) {
        wchar_t label[256];
        wcscpy(label, state->server_names[i]);
        wchar_t* suffix = wcsstr(label, L"._afpovertcp.");
        if (suffix)
            *suffix = 0;
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)label);
    }
}

static int resolve_address(wchar_t* input, uint32_t* address, uint16_t* port)
{
    *port = 548;
    wchar_t* separator = wcschr(input, L':');
    if (separator) {
        wchar_t* end;
        unsigned long number = wcstoul(separator + 1, &end, 10);
        if (*end || !number || number > 65535)
            return 0;
        *separator = 0;
        *port = number;
    }
    char host[256], canonical[255];
    uint32_t addresses[4];
    if (!input[0] || !WideCharToMultiByte(CP_UTF8, 0, input, -1, host, sizeof host, NULL, NULL) ||
        host_mactcp_resolver_open())
        return 0;
    int error = host_mactcp_resolve(host, addresses, canonical);
    host_mactcp_resolver_close();
    if (error)
        return 0;
    *address = addresses[0];
    return 1;
}

static void list_volumes(HWND dialog, struct chooser* state, int choose_row)
{
    HWND volumes = GetDlgItem(dialog, volumes_id);
    SendMessageW(volumes, LB_RESETCONTENT, 0, 0);
    EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
    wchar_t address[256];
    GetDlgItemTextW(dialog, address_id, address, NELEM(address));
    LRESULT row = SendDlgItemMessageW(dialog, servers_id, LB_GETCURSEL, 0, 0);
    int remembered = state->previous[0] != 0;
    int connected;
    if (choose_row && row >= remembered)
        connected = resolve_advertisement(state->server_names[row - remembered], &state->address, &state->port);
    else {
        if (choose_row && row == 0)
            wcscpy(address, state->previous);
        connected = resolve_address(address, &state->address, &state->port);
    }
    if (!connected) {
        MessageBoxW(dialog, L"Enter a server name or IPv4 address, optionally followed by a port.", L"Chooser",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    wchar_t endpoint[64];
    uint32_t ip = state->address;
    swprintf(endpoint, NELEM(endpoint), L"%u.%u.%u.%u:%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255,
             state->port);
    SetDlgItemTextW(dialog, address_id, endpoint);
    int32_t error;
    romaw_afp_session* session = romaw_afp_connect(state->address, state->port, &error);
    if (session) {
        error = romaw_afp_list_volumes(session, &state->volumes);
        romaw_afp_close(session);
    }
    if (error || !state->volumes.count) {
        MessageBoxW(dialog,
                    error ? L"Could not list volumes on this AFP server as a guest."
                          : L"This server offers no volumes to guests.",
                    L"Chooser", MB_OK | MB_ICONWARNING);
        return;
    }
    for (unsigned i = 0; i < state->volumes.count; ++i) {
        wchar_t name[256];
        if (MultiByteToWideChar(CP_UTF8, 0, state->volumes.items[i].name, -1, name, NELEM(name))) {
            LRESULT index = SendMessageW(volumes, LB_ADDSTRING, 0, (LPARAM)name);
            if (index >= 0)
                SendMessageW(volumes, LB_SETITEMDATA, index, i);
        }
    }
    if (SendMessageW(volumes, LB_GETCOUNT, 0, 0) > 0) {
        SendMessageW(volumes, LB_SETCURSEL, 0, 0);
        EnableWindow(GetDlgItem(dialog, IDOK), TRUE);
    }
}

static void control(HWND dialog, const wchar_t* type, const wchar_t* title, DWORD style, DWORD edge, int id, int x,
                    int y, int width, int height)
{
    RECT r = {x, y, x + width, y + height};
    MapDialogRect(dialog, &r);
    HWND window = CreateWindowExW(edge, type, title, WS_CHILD | WS_VISIBLE | style, r.left, r.top, r.right - r.left,
                                  r.bottom - r.top, dialog, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(window, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), TRUE);
}

static INT_PTR CALLBACK chooser_dialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    struct chooser* state = (void*)GetWindowLongPtrW(dialog, DWLP_USER);
    if (message == WM_INITDIALOG) {
        state = (void*)lparam;
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        control(dialog, L"STATIC", L"AFP over TCP servers", 0, 0, -1, 8, 8, 130, 10);
        control(dialog, L"STATIC", L"Volumes", 0, 0, -1, 158, 8, 130, 10);
        control(dialog, L"LISTBOX", L"", WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, WS_EX_CLIENTEDGE,
                servers_id, 8, 20, 142, 86);
        control(dialog, L"LISTBOX", L"", WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, WS_EX_CLIENTEDGE,
                volumes_id, 158, 20, 142, 86);
        control(dialog, L"STATIC", L"Server address", 0, 0, -1, 8, 113, 62, 12);
        control(dialog, L"EDIT", state->previous, WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, address_id, 72, 110,
                169, 15);
        SendDlgItemMessageW(dialog, address_id, EM_LIMITTEXT, 255, 0);
        control(dialog, L"BUTTON", L"Connect", WS_TABSTOP, 0, connect_id, 248, 110, 52, 15);
        control(dialog, L"BUTTON", L"Mount", WS_TABSTOP | BS_DEFPUSHBUTTON, 0, IDOK, 188, 135, 52, 16);
        control(dialog, L"BUTTON", L"Cancel", WS_TABSTOP, 0, IDCANCEL, 248, 135, 52, 16);
        EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
        update_servers(dialog, state);
        SetTimer(dialog, 1, 400, NULL);
        return TRUE;
    }
    if (message == WM_TIMER) {
        update_servers(dialog, state);
        return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)) {
        EndDialog(dialog, 0);
        return TRUE;
    }
    if (message != WM_COMMAND)
        return FALSE;
    if (LOWORD(wparam) == connect_id || (LOWORD(wparam) == servers_id && HIWORD(wparam) == LBN_SELCHANGE)) {
        list_volumes(dialog, state, LOWORD(wparam) == servers_id);
        return TRUE;
    }
    if (LOWORD(wparam) != IDOK && !(LOWORD(wparam) == volumes_id && HIWORD(wparam) == LBN_DBLCLK))
        return FALSE;
    LRESULT row = SendDlgItemMessageW(dialog, volumes_id, LB_GETCURSEL, 0, 0);
    if (row < 0)
        return TRUE;
    LRESULT index = SendDlgItemMessageW(dialog, volumes_id, LB_GETITEMDATA, row, 0);
    if (index < 0 || index >= state->volumes.count)
        return TRUE;
    int publish_error;
    int error = ROMlib_finder_mount_afp(state->address, state->port, state->volumes.items[index].name, &publish_error);
    if (error) {
        MessageBoxW(dialog, L"The selected AFP volume could not be mounted.", L"Chooser", MB_OK | MB_ICONWARNING);
        return TRUE;
    }
    if (publish_error)
        MessageBoxW(dialog, L"The volume mounted here, but could not be shared with other ROMAW processes.", L"Chooser",
                    MB_OK | MB_ICONWARNING);
    char path[4096], endpoint[64];
    uint32_t ip = state->address;
    snprintf(endpoint, sizeof endpoint, "%u.%u.%u.%u:%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255,
             state->port);
    if (romaw_profile_path("AFP Mount.ini", path, sizeof path, NULL, 0))
        config_prefs_set(path, "AFP", "Last Server", endpoint);
    EndDialog(dialog, 1);
    return TRUE;
}

int ROMlib_finder_afp_chooser(void* owner)
{
    struct chooser* state = calloc(1, sizeof *state);
    if (!state)
        return 0;
    state->revision = (unsigned)-1;
    char path[4096], endpoint[64] = {0};
    if (romaw_profile_path("AFP Mount.ini", path, sizeof path, NULL, 0)) {
        config_prefs_get(path, "AFP", "Last Server", endpoint, sizeof endpoint);
        MultiByteToWideChar(CP_UTF8, 0, endpoint, -1, state->previous, NELEM(state->previous));
    }
    start_discovery();
    if (!owner)
        owner = GetActiveWindow();

    enum { style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER | DS_SETFONT };

    _Alignas(DWORD) static const WORD template_words[] = {style & 0xffff,
                                                          (DWORD)style >> 16,
                                                          0,
                                                          0,
                                                          0,
                                                          0,
                                                          0,
                                                          308,
                                                          159,
                                                          0,
                                                          0,
                                                          'C',
                                                          'h',
                                                          'o',
                                                          'o',
                                                          's',
                                                          'e',
                                                          'r',
                                                          0,
                                                          9,
                                                          'S',
                                                          'e',
                                                          'g',
                                                          'o',
                                                          'e',
                                                          ' ',
                                                          'U',
                                                          'I',
                                                          0};
    INT_PTR result = DialogBoxIndirectParamW(GetModuleHandleW(NULL), (const DLGTEMPLATE*)template_words, owner,
                                             chooser_dialog, (LPARAM)state);
    free(state);
    return result == 1;
}
