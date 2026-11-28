/*+===================================================================
  File:      afpClassicChooser.c

  Summary:   Present the AFP Chooser through the classic Dialog Manager.

  Classes:   None.

  Functions: ROMlib_afp_classic_chooser.

  Origin:    ROMAW AFP integration.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include "rsys/common.h"
#include "ControlMgr.h"
#include "DialogMgr.h"
#include "FontMgr.h"
#include "ListMgr.h"
#include "MemoryMgr.h"
#include "OSUtil.h"
#include "QuickDraw.h"
#include "WindowMgr.h"
#include "finder/image_volumes.h"
#include "rsys/host_afp.h"
#include "rsys/host_names.h"
#include "rsys/system_error.h"

enum {
    mount_button = 1,
    cancel_button,
    connect_button,
    rescan_button,
    server_heading,
    volume_heading,
    address_label,
    address_field,
    status_label,
    server_list_hit,
    volume_list_hit
};

static void word(unsigned char* p, unsigned value)
{
    p[0] = value >> 8;
    p[1] = value;
}

static unsigned char* item(unsigned char* p, unsigned kind, int x, int y, int width, int height, const char* label)
{
    size_t length = strlen(label);
    memset(p, 0, 14 + ((length + 1) & ~1));
    word(p + 4, y);
    word(p + 6, x);
    word(p + 8, y + height);
    word(p + 10, x + width);
    p[12] = kind;
    p[13] = length;
    memcpy(p + 14, label, length);
    return p + 14 + ((length + 1) & ~1);
}

static DialogPtr new_chooser(void)
{
    unsigned char data[768];
    word(data, status_label - 1);
    unsigned char* p = data + 2;
    p = item(p, 4, 265, 209, 80, 22, "Mount");
    p = item(p, 4, 174, 209, 80, 22, "Cancel");
    p = item(p, 4, 265, 167, 80, 22, "Connect");
    p = item(p, 4, 12, 209, 88, 22, "Refresh");
    p = item(p, 136, 12, 9, 161, 20, "AFP over TCP servers");
    p = item(p, 136, 185, 9, 160, 20, "Volumes");
    p = item(p, 136, 12, 169, 91, 19, "Server address");
    p = item(p, 16, 103, 167, 154, 21, "");
    p = item(p, 136, 12, 188, 333, 19, "");
    Handle items = NewHandle(p - data);
    if (!items)
        return NULL;
    memcpy(STARH(items), data, p - data);
    Rect bounds;
    SetRect(&bounds, 136, 80, 494, 323);
    DialogPtr dialog =
        NewDialog(NULL, &bounds, (StringPtr) "\007Chooser", TRUE, movableDBoxProc, (WindowPtr)-1, FALSE, 0, items);
    if (!dialog)
        DisposHandle(items);
    return dialog;
}

static Handle item_handle(DialogPtr dialog, INTEGER number)
{
    INTEGER kind;
    HIDDEN_Handle handle;
    Rect bounds;
    GetDItem(dialog, number, &kind, &handle, &bounds);
    return MR(HIDDEN_RAW(handle));
}

static void mac_label(const char* text, Str255 label, unsigned limit)
{
    size_t bytes = strlen(text);
    char converted[768];
    int length = ROMlib_host_name_to_mac(text, bytes, converted);
    const char* name = length < 0 ? text : converted;
    unsigned count = MIN(length < 0 ? bytes : (size_t)length, limit);
    label[0] = count;
    memcpy(label + 1, name, count);
}

static void set_text(DialogPtr dialog, INTEGER number, const char* text)
{
    Str255 label;
    mac_label(text, label, 100);
    SetIText(item_handle(dialog, number), label);
}

struct chooser_lists {
    ListHandle servers;
    ListHandle volumes;
    RgnHandle redraw;
};

static void enable(DialogPtr dialog, INTEGER number, int available)
{
    HiliteControl((ControlHandle)item_handle(dialog, number), available ? 0 : 255);
}

static ListHandle new_list(DialogPtr dialog, int left, int right)
{
    Rect view, data;
    SetRect(&view, left, 31, right, 151);
    SetRect(&data, 0, 0, 1, 0);
    Point cell_size = {15, right - left};
    ListHandle list = LNew(&view, &data, cell_size, 0, (WindowPtr)dialog, TRUE, FALSE, FALSE, TRUE);
    if (list)
        HxX(list, selFlags) = lOnlyOne | lNoNilHilite;
    return list;
}

static void redraw_list(ListHandle list, RgnHandle redraw)
{
    Rect border = HxX(list, rView);
    EraseRect(&border);
    InsetRect(&border, -1, -1);
    FrameRect(&border);
    SetRectRgn(redraw, CW(border.left), CW(border.top), CW(border.right) + 16, CW(border.bottom));
    LUpdate(redraw, list);
}

static void begin_rows(ListHandle list, unsigned count)
{
    LDoDraw(FALSE, list);
    LDelRow(0, 0, list);
    if (count)
        LAddRow(count, 0, list);
}

static void set_row(ListHandle list, unsigned index, const char* name)
{
    Str255 label;
    mac_label(name, label, 255);
    Cell cell = {.v = index, .h = 0};
    LSetCell((Ptr)label + 1, label[0], cell, list);
}

static void show_servers(struct chooser_lists* lists, char servers[][256], unsigned count)
{
    begin_rows(lists->servers, count);
    for (unsigned i = 0; i < count; ++i) {
        char label[256];
        strcpy(label, servers[i]);
        char* suffix = strstr(label, "._afpovertcp.");
        if (suffix)
            *suffix = 0;
        set_row(lists->servers, i, label);
    }
    LDoDraw(TRUE, lists->servers);
    redraw_list(lists->servers, lists->redraw);
}

static void show_volumes(struct chooser_lists* lists, const romaw_afp_volume_list* volumes)
{
    begin_rows(lists->volumes, volumes->count);
    for (unsigned i = 0; i < volumes->count; ++i)
        set_row(lists->volumes, i, volumes->items[i].name);
    LDoDraw(TRUE, lists->volumes);
    redraw_list(lists->volumes, lists->redraw);
}

static int selected_row(ListHandle list)
{
    Cell cell = {0, 0};
    return LGetSelect(TRUE, &cell, list) ? CW(cell.v) : -1;
}

static BOOLEAN list_event(DialogPtr dialog, EventRecord* event, INTEGER* action, void* context)
{
    struct chooser_lists* lists = context;
    if (event->what == CWC(updateEvt) && MR(event->message) == (uintptr_t)dialog) {
        HIDDEN_DialogPtr updated;
        DialogSelect(event, &updated, action);
        redraw_list(lists->servers, lists->redraw);
        redraw_list(lists->volumes, lists->redraw);
        *action = 0;
        return TRUE;
    }
    if (event->what != CWC(mouseDown))
        return FALSE;
    HIDDEN_WindowPtr window;
    Point where = {CW(event->where.v), CW(event->where.h)};
    if (FindWindow(where, &window) != inContent || MR(HIDDEN_RAW(window)) != (WindowPtr)dialog)
        return FALSE;
    where = event->where;
    GlobalToLocal(&where);
    where = (Point){CW(where.v), CW(where.h)};
    ListHandle columns[] = {lists->servers, lists->volumes};
    for (unsigned i = 0; i < NELEM(columns); ++i) {
        ListHandle list = columns[i];
        Rect view = HxX(list, rView);
        int inside = PtInRect(where, &view);
        view.right = CW(CW(view.right) + 16);
        if (!PtInRect(where, &view))
            continue;
        if (!HxX(list, dataBounds.bottom)) {
            *action = 0;
            return TRUE;
        }
        int before = selected_row(list);
        BOOLEAN double_click = LClick(where, CW(event->modifiers), list);
        if (inside && i == 1 && double_click && selected_row(list) >= 0)
            *action = CWC(mount_button);
        else if (inside && (i == 1 || selected_row(list) != before))
            *action = CW(server_list_hit + i);
        else
            *action = 0;
        return TRUE;
    }
    return FALSE;
}

void ROMlib_afp_classic_chooser(void)
{
    enum { server_limit = 64 };

    char(*servers)[256] = calloc(server_limit, sizeof *servers);
    romaw_afp_volume_list* volumes = calloc(1, sizeof *volumes);
    HIDDEN_GrafPtr previous;
    GetPort(&previous);
    DialogPtr dialog = servers && volumes ? new_chooser() : NULL;
    if (!dialog)
        goto done;
    SetPort((GrafPtr)dialog);
    TextFont(geneva);
    TextSize(12);
    FontInfo font;
    GetFontInfo(&font);
    TEHandle address_text = DIALOG_TEXTH(dialog);
    TE_TX_FONT_X(address_text) = CWC(geneva);
    TE_TX_SIZE_X(address_text) = CWC(12);
    TE_FONT_ASCENT_X(address_text) = font.ascent;
    TE_LINE_HEIGHT_X(address_text) = CW(CW(font.ascent) + CW(font.descent) + CW(font.leading));
    TECalText(address_text);
    struct chooser_lists lists = {new_list(dialog, 13, 156), new_list(dialog, 186, 329), NewRgn()};
    if (!lists.servers || !lists.volumes || !lists.redraw)
        goto close_dialog;
    ROMlib_afp_chooser_remembered(servers[0], sizeof servers[0]);
    unsigned remembered = servers[0][0] != 0;
    unsigned server_count = remembered + ROMlib_afp_chooser_services(servers + remembered, server_limit - remembered);
    if (server_count == remembered) {
        LONGINT ticks;
        Delay(24, &ticks);
        server_count = remembered + ROMlib_afp_chooser_services(servers + remembered, server_limit - remembered);
    }
    set_text(dialog, address_field, remembered ? servers[0] : "");
    set_text(dialog, status_label, "Select a server or enter its address.");
    SelIText(dialog, address_field, 0, 32767);
    show_servers(&lists, servers, server_count);
    show_volumes(&lists, volumes);
    uint32 address = 0;
    uint16 port = 0;
    for (;;) {
        enable(dialog, mount_button, selected_row(lists.volumes) >= 0);
        INTEGER action;
        ROMlib_run_filtered_modal_dialog(dialog, list_event, &lists, &action);
        action = CW(action);
        if (action == cancel_button)
            break;
        if (action == rescan_button) {
            server_count = remembered + ROMlib_afp_chooser_services(servers + remembered, server_limit - remembered);
            show_servers(&lists, servers, server_count);
        }
        else if (action == connect_button || action == server_list_hit) {
            char name[768] = {0};
            int advertised = FALSE;
            if (action == connect_button) {
                Str255 text;
                GetIText(item_handle(dialog, address_field), text);
                if (ROMlib_mac_name_to_utf8((const char*)text + 1, text[0], name, sizeof name) < 0)
                    name[0] = 0;
            }
            else {
                int index = selected_row(lists.servers);
                if (index < 0 || (unsigned)index >= server_count)
                    continue;
                strcpy(name, servers[index]);
                advertised = (unsigned)index >= remembered;
            }
            volumes->count = 0;
            int32 error = ROMAW_AFP_TRANSPORT_ERROR;
            if (ROMlib_afp_chooser_endpoint(name, advertised, &address, &port)) {
                char endpoint[64];
                snprintf(endpoint, sizeof endpoint, "%u.%u.%u.%u:%u", address >> 24, (address >> 16) & 255,
                         (address >> 8) & 255, address & 255, port);
                set_text(dialog, address_field, endpoint);
                romaw_afp_session* session = romaw_afp_connect(address, port, &error);
                if (session) {
                    error = romaw_afp_list_volumes(session, volumes);
                    romaw_afp_close(session);
                }
            }
            show_volumes(&lists, volumes);
            set_text(dialog, status_label,
                     error            ? "Could not list volumes on that AFP server."
                     : volumes->count ? "Select a volume to mount."
                                      : "No guest volumes.");
        }
        else if (action == volume_list_hit) {
            int selected = selected_row(lists.volumes);
            if (selected >= 0) {
                char status[280];
                snprintf(status, sizeof status, "Selected: %s", volumes->items[selected].name);
                set_text(dialog, status_label, status);
            }
        }
        else if (action == mount_button) {
            int selected = selected_row(lists.volumes);
            if (selected < 0)
                continue;
            int publish_error;
            if (ROMlib_finder_mount_afp(address, port, volumes->items[selected].name, &publish_error)) {
                set_text(dialog, status_label, "The selected AFP volume could not be mounted.");
                continue;
            }
            if (publish_error)
                system_error("AFP mounted here, but could not be shared with other ROMAW processes.", 0, "OK", NULL,
                             NULL, NULL, NULL, NULL);
            break;
        }
    }
close_dialog:
    if (lists.servers) {
        LDispose(lists.servers);
        DisposHandle((Handle)lists.servers);
    }
    if (lists.volumes) {
        LDispose(lists.volumes);
        DisposHandle((Handle)lists.volumes);
    }
    if (lists.redraw)
        DisposeRgn(lists.redraw);
    DisposDialog(dialog);
    SetPort(MR(HIDDEN_RAW(previous)));
done:
    free(volumes);
    free(servers);
}
