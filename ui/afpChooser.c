/*+===================================================================
  File:      afpChooser.c

  Summary:   Native AFP over TCP server discovery and volume selection.

  Classes:   None.

  Functions: ROMlib_finder_afp_chooser, ROMlib_finder_afp_message, ROMlib_afp_chooser_services,
             ROMlib_afp_chooser_endpoint, ROMlib_afp_chooser_remembered.

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

enum { servers_id = 100, address_id, connect_id, volumes_id, status_id, refresh_id, server_limit = 64 };

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

static struct chooser finder_chooser;
static HWND chooser_window;

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

unsigned ROMlib_afp_chooser_services(char names[][256], unsigned capacity)
{
    start_discovery();
    AcquireSRWLockShared(&server_lock);
    unsigned count = 0;
    for (unsigned i = 0; i < server_count && count < capacity; ++i)
        if (WideCharToMultiByte(CP_UTF8, 0, servers[i], -1, names[count], sizeof names[count], NULL, NULL))
            ++count;
    ReleaseSRWLockShared(&server_lock);
    return count;
}

int ROMlib_afp_chooser_endpoint(const char* name, int advertised, uint32_t* address, uint16_t* port)
{
    wchar_t wide[256];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide, NELEM(wide)))
        return 0;
    return advertised ? resolve_service && resolve_advertisement(wide, address, port)
                      : resolve_address(wide, address, port);
}

void ROMlib_afp_chooser_remembered(char* endpoint, unsigned capacity)
{
    char path[4096];
    if (!capacity)
        return;
    endpoint[0] = 0;
    if (romaw_profile_path("AFP Mount.ini", path, sizeof path, NULL, 0))
        config_prefs_get(path, "AFP", "Last Server", endpoint, capacity);
}

static void list_volumes(HWND dialog, struct chooser* state, int choose_row)
{
    HWND volumes = GetDlgItem(dialog, volumes_id);
    SendMessageW(volumes, LB_RESETCONTENT, 0, 0);
    EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
    SetDlgItemTextW(dialog, status_id, L"");
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
        SetDlgItemTextW(dialog, status_id, L"Enter a server name or IPv4 address.");
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
        SetDlgItemTextW(dialog, status_id,
                        error ? L"Could not list AFP volumes as a guest."
                              : L"This AFP server offers no guest volumes.");
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
    HWND window = CreateWindowExW(edge, type, title, WS_CHILD | WS_VISIBLE | style, x, y, width, height, dialog,
                                  (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(window, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), TRUE);
}

static void draw_list_row(const struct chooser* state, const DRAWITEMSTRUCT* row)
{
    int selected = (row->itemState & ODS_SELECTED) != 0;
    HBRUSH ink = GetStockObject(selected ? WHITE_BRUSH : BLACK_BRUSH);
    HBRUSH paper = GetStockObject(selected ? BLACK_BRUSH : WHITE_BRUSH);
    FillRect(row->hDC, &row->rcItem, paper);
    if (row->itemID == (UINT)-1)
        return;
    int volume = row->CtlID == volumes_id;
    if (volume && row->itemData < state->volumes.count &&
        ROMlib_finder_afp_mounted(state->address, state->port, state->volumes.items[row->itemData].name)) {
        int x = row->rcItem.left + 4;
        int y = row->rcItem.top + 4;
        RECT monitor = {x, y, x + 9, y + 6};
        RECT stand = {x + 1, y + 7, x + 8, y + 8};
        FillRect(row->hDC, &monitor, ink);
        InflateRect(&monitor, -1, -1);
        FillRect(row->hDC, &monitor, paper);
        FillRect(row->hDC, &stand, ink);
    }
    wchar_t text[256];
    if (SendMessageW(row->hwndItem, LB_GETTEXT, row->itemID, (LPARAM)text) == LB_ERR)
        return;
    HGDIOBJ old = SelectObject(row->hDC, (HFONT)SendMessageW(row->hwndItem, WM_GETFONT, 0, 0));
    SetBkMode(row->hDC, TRANSPARENT);
    SetTextColor(row->hDC, selected ? RGB(255, 255, 255) : RGB(0, 0, 0));
    RECT label = row->rcItem;
    label.left += volume ? 18 : 5;
    DrawTextW(row->hDC, text, -1, &label, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(row->hDC, old);
}

static INT_PTR CALLBACK chooser_dialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    struct chooser* state = (void*)GetWindowLongPtrW(dialog, DWLP_USER);
    if (message == WM_INITDIALOG) {
        state = (void*)lparam;
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        control(dialog, L"STATIC", L"AFP over TCP servers", 0, 0, -1, 12, 9, 161, 20);
        control(dialog, L"STATIC", L"Volumes", 0, 0, -1, 185, 9, 160, 20);
        control(dialog, L"LISTBOX", L"",
                WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_DISABLENOSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
                    LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
                0, servers_id, 12, 30, 161, 122);
        control(dialog, L"LISTBOX", L"",
                WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_DISABLENOSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
                    LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
                0, volumes_id, 185, 30, 160, 122);
        control(dialog, L"STATIC", L"Server", 0, 0, -1, 12, 169, 91, 19);
        control(dialog, L"EDIT", state->previous, WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 0, address_id, 103, 165, 154,
                22);
        SendDlgItemMessageW(dialog, address_id, EM_LIMITTEXT, 255, 0);
        control(dialog, L"BUTTON", L"Connect", WS_TABSTOP, 0, connect_id, 265, 165, 80, 22);
        control(dialog, L"BUTTON", L"Refresh", WS_TABSTOP, 0, refresh_id, 12, 216, 88, 22);
        control(dialog, L"BUTTON", L"Mount", WS_TABSTOP, 0, IDOK, 265, 216, 80, 22);
        control(dialog, L"BUTTON", L"Close", WS_TABSTOP, 0, IDCANCEL, 174, 216, 80, 22);
        control(dialog, L"STATIC", L"Select a server or enter its address.", 0, 0, status_id, 12, 195, 333, 19);
        EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
        SetPropW(dialog, L"FinderWhite", (HANDLE)1);
        ROMlib_finder_style_afp_chooser(dialog);
        update_servers(dialog, state);
        SetTimer(dialog, 1, 400, NULL);
        return TRUE;
    }
    if (message == WM_MEASUREITEM && (wparam == volumes_id || wparam == servers_id)) {
        ((MEASUREITEMSTRUCT*)lparam)->itemHeight = 15;
        return TRUE;
    }
    if (state && message == WM_DRAWITEM && (wparam == volumes_id || wparam == servers_id)) {
        draw_list_row(state, (const DRAWITEMSTRUCT*)lparam);
        return TRUE;
    }
    if (message == WM_NCDESTROY) {
        RemovePropW(dialog, L"FinderWhite");
        chooser_window = NULL;
        return FALSE;
    }
    if (message == WM_TIMER || (message == WM_COMMAND && LOWORD(wparam) == refresh_id)) {
        if (message == WM_COMMAND)
            state->revision = (unsigned)-1;
        update_servers(dialog, state);
        InvalidateRect(GetDlgItem(dialog, volumes_id), NULL, FALSE);
        return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)) {
        DestroyWindow(dialog);
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
    InvalidateRect(GetDlgItem(dialog, volumes_id), NULL, FALSE);
    SetDlgItemTextW(dialog, status_id,
                    error           ? L"The selected AFP volume could not be mounted."
                    : publish_error ? L"Mounted here. Could not share with other ROMAW apps."
                                    : L"Volume mounted.");
    return TRUE;
}

int ROMlib_finder_afp_message(void* message)
{
    MSG* msg = message;
    return chooser_window && (msg->hwnd == chooser_window || IsChild(chooser_window, msg->hwnd)) &&
           IsDialogMessageW(chooser_window, msg);
}

void* ROMlib_finder_afp_chooser(void* owner)
{
    if (chooser_window) {
        BringWindowToTop(chooser_window);
        SetFocus(GetDlgItem(chooser_window, address_id));
        return chooser_window;
    }
    struct chooser* state = &finder_chooser;
    memset(state, 0, sizeof *state);
    state->revision = (unsigned)-1;
    char endpoint[64];
    ROMlib_afp_chooser_remembered(endpoint, sizeof endpoint);
    MultiByteToWideChar(CP_UTF8, 0, endpoint, -1, state->previous, NELEM(state->previous));
    start_discovery();
    if (!owner)
        owner = GetActiveWindow();

    enum { style = WS_CHILD | WS_CLIPSIBLINGS | WS_CAPTION | WS_SYSMENU | DS_SETFONT };

    _Alignas(DWORD) static const WORD template_words[] = {style & 0xffff,
                                                          (DWORD)style >> 16,
                                                          0,
                                                          0,
                                                          0,
                                                          0,
                                                          0,
                                                          308,
                                                          167,
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
    chooser_window = CreateDialogIndirectParamW(GetModuleHandleW(NULL), (const DLGTEMPLATE*)template_words, owner,
                                                chooser_dialog, (LPARAM)state);
    if (chooser_window) {
        RECT workspace = {0};
        GetClientRect(owner, &workspace);
        const int width = 362, height = 271;
        int x = workspace.right > width ? (workspace.right - width) / 2 : 0;
        int y = workspace.bottom > height ? (workspace.bottom - height) / 2 : 0;
        SetWindowPos(chooser_window, HWND_TOP, x, y, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        SetFocus(GetDlgItem(chooser_window, address_id));
    }
    return chooser_window;
}
