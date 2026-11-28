/*+===================================================================
  File:      afpClassicChooser.c

  Summary:   Present the AFP Chooser through the classic Dialog Manager.

  Classes:   None.

  Functions: ROMlib_afp_classic_chooser, ROMlib_afp_classic_close, ROMlib_afp_classic_event,
             ROMlib_afp_classic_idle, ROMlib_afp_classic_edit.

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
#include "rsys/desk.h"
#include "rsys/wind.h"
#include "ToolboxEvent.h"
#include "rsys/host_names.h"
#include "DeskMgr.h"

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
    p = item(p, 4, 265, 216, 80, 22, "Mount");
    p = item(p, 4, 174, 216, 80, 22, "Close");
    p = item(p, 4, 265, 167, 80, 22, "Connect");
    p = item(p, 4, 12, 216, 88, 22, "Refresh");
    p = item(p, 136, 12, 9, 161, 20, "AFP over TCP servers");
    p = item(p, 136, 185, 9, 160, 20, "Volumes");
    p = item(p, 136, 12, 169, 91, 19, "Server address");
    p = item(p, 16, 103, 167, 154, 21, "");
    /* TextEdit draws a frame three pixels outside the edit item. */
    p = item(p, 136, 12, 195, 333, 19, "");
    Handle items = NewHandle(p - data);
    if (!items)
        return NULL;
    memcpy(STARH(items), data, p - data);
    Rect bounds;
    SetRect(&bounds, 136, 80, 494, 330);
    DialogPtr dialog =
        NewDialog(NULL, &bounds, (StringPtr) "\007Chooser", TRUE, noGrowDocProc, (WindowPtr)-1, TRUE, 0, items);
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
    const romaw_afp_volume_list* entries;
    uint32 address;
    uint16 port;
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

static void redraw_volumes(struct chooser_lists* lists)
{
    ListHandle list = lists->volumes;
    redraw_list(list, lists->redraw);
    Rect view = HxX(list, rView);
    int first = Hx(list, visible.top);
    int limit = MIN(Hx(list, visible.bottom), Hx(list, dataBounds.bottom));
    int x = CW(view.left) + 4;
    int height = Hx(list, cellSize.v);
    for (int row = first; row < limit; ++row) {
        if (!lists->entries || !ROMlib_finder_afp_mounted(lists->address, lists->port, lists->entries->items[row].name))
            continue;
        int y = CW(view.top) + (row - first) * height + 4;
        if (y + 8 > CW(view.bottom))
            break;
        Rect mark;
        SetRect(&mark, x, y, x + 9, y + 6);
        InvertRect(&mark);
        SetRect(&mark, x + 1, y + 7, x + 8, y + 8);
        InvertRect(&mark);
        SetRect(&mark, x + 1, y + 1, x + 8, y + 5);
        InvertRect(&mark);
    }
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

static void show_volumes(struct chooser_lists* lists, const romaw_afp_volume_list* volumes, uint32 address, uint16 port)
{
    lists->entries = volumes;
    lists->address = address;
    lists->port = port;
    begin_rows(lists->volumes, volumes->count);
    for (unsigned i = 0; i < volumes->count; ++i) {
        set_row(lists->volumes, i, volumes->items[i].name);
    }
    LDoDraw(TRUE, lists->volumes);
    redraw_volumes(lists);
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
        redraw_volumes(lists);
        *action = 0;
        return TRUE;
    }
    if (event->what != CWC(mouseDown))
        return FALSE;
    HIDDEN_WindowPtr window;
    Point where = {CW(event->where.v), CW(event->where.h)};
    if (FindWindow(where, &window) != inSysWindow || MR(HIDDEN_RAW(window)) != (WindowPtr)dialog)
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
        if (i == 1)
            redraw_volumes(lists);
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

enum { server_limit = 64 };

static struct {
    DialogPtr dialog;
    struct chooser_lists lists;
    char servers[server_limit][256];
    romaw_afp_volume_list volumes;
    unsigned remembered, server_count;
    uint32 address, discover_until, next_discovery;
    uint16 port;
} chooser;

static void refresh_servers(void)
{
    unsigned count = chooser.remembered + ROMlib_afp_chooser_services(chooser.servers + chooser.remembered,
                                                                      server_limit - chooser.remembered);
    chooser.server_count = count;
    show_servers(&chooser.lists, chooser.servers, count);
}

void ROMlib_afp_classic_close(void)
{
    DialogPtr dialog = chooser.dialog;
    if (!dialog)
        return;
    chooser.dialog = NULL;
    if (chooser.lists.servers) {
        LDispose(chooser.lists.servers);
        DisposHandle((Handle)chooser.lists.servers);
    }
    if (chooser.lists.volumes) {
        LDispose(chooser.lists.volumes);
        DisposHandle((Handle)chooser.lists.volumes);
    }
    if (chooser.lists.redraw)
        DisposeRgn(chooser.lists.redraw);
    if (thePort == (GrafPtr)dialog) {
        HIDDEN_GrafPtr manager;
        GetWMgrPort(&manager);
        SetPort(MR(HIDDEN_RAW(manager)));
    }
    DisposDialog(dialog);
    memset(&chooser, 0, sizeof chooser);
}

int16 ROMlib_afp_classic_chooser(void)
{
    if (chooser.dialog) {
        SelectWindow((WindowPtr)chooser.dialog);
        return ROMLIB_CHOOSER_REFNUM;
    }
    GrafPtr previous = thePort;
    DialogPtr dialog = new_chooser();
    if (!dialog)
        return 0;
    chooser.dialog = dialog;
    WINDOW_KIND_X(dialog) = CWC(ROMLIB_CHOOSER_REFNUM);
    SEvtEnb = TRUE;
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
    chooser.lists = (struct chooser_lists){new_list(dialog, 13, 156), new_list(dialog, 186, 329), NewRgn()};
    if (!chooser.lists.servers || !chooser.lists.volumes || !chooser.lists.redraw) {
        SetPort(previous);
        ROMlib_afp_classic_close();
        return 0;
    }
    ROMlib_afp_chooser_remembered(chooser.servers[0], sizeof chooser.servers[0]);
    chooser.remembered = chooser.servers[0][0] != 0;
    set_text(dialog, address_field, chooser.servers[0]);
    set_text(dialog, status_label, "Select a server or enter its address.");
    SelIText(dialog, address_field, 0, 32767);
    refresh_servers();
    chooser.next_discovery = TickCount() + 24;
    chooser.discover_until = TickCount() + 180;
    HxX(chooser.lists.volumes, indent.h) = CWC(18);
    show_volumes(&chooser.lists, &chooser.volumes, chooser.address, chooser.port);
    enable(dialog, mount_button, FALSE);
    SetPort(previous);
    return ROMLIB_CHOOSER_REFNUM;
}

void ROMlib_afp_classic_idle(void)
{
    DialogPtr dialog = chooser.dialog;
    if (!dialog || FrontWindow() != (WindowPtr)dialog)
        return;
    GrafPtr previous = thePort;
    SetPort((GrafPtr)dialog);
    TEIdle(DIALOG_TEXTH(dialog));
    uint32 now = TickCount();
    if (chooser.discover_until && (int32)(now - chooser.next_discovery) >= 0) {
        if (selected_row(chooser.lists.servers) < 0)
            refresh_servers();
        chooser.next_discovery = now + 24;
        if ((int32)(now - chooser.discover_until) >= 0)
            chooser.discover_until = 0;
    }
    SetPort(previous);
}

BOOLEAN ROMlib_afp_classic_edit(INTEGER command)
{
    DialogPtr dialog = chooser.dialog;
    if (!dialog)
        return FALSE;
    switch (command) {
    case cutCmd:
        DlgCut(dialog);
        break;
    case copyCmd:
        DlgCopy(dialog);
        break;
    case pasteCmd:
        DlgPaste(dialog);
        break;
    case clearCmd:
        DlgDelete(dialog);
        break;
    default:
        return FALSE;
    }
    return TRUE;
}

static void connect_server(int from_list)
{
    DialogPtr dialog = chooser.dialog;
    char name[768] = {0};
    int advertised = FALSE;
    if (from_list) {
        int index = selected_row(chooser.lists.servers);
        if (index < 0 || (unsigned)index >= chooser.server_count)
            return;
        strcpy(name, chooser.servers[index]);
        advertised = (unsigned)index >= chooser.remembered;
    }
    else {
        Str255 text;
        GetIText(item_handle(dialog, address_field), text);
        if (ROMlib_mac_name_to_utf8((const char*)text + 1, text[0], name, sizeof name) < 0)
            name[0] = 0;
    }

    chooser.volumes.count = 0;
    int32 error = ROMAW_AFP_TRANSPORT_ERROR;
    if (ROMlib_afp_chooser_endpoint(name, advertised, &chooser.address, &chooser.port)) {
        char endpoint[64];
        uint32 address = chooser.address;
        snprintf(endpoint, sizeof endpoint, "%u.%u.%u.%u:%u", address >> 24, (address >> 16) & 255,
                 (address >> 8) & 255, address & 255, chooser.port);
        set_text(dialog, address_field, endpoint);
        romaw_afp_session* session = romaw_afp_connect(address, chooser.port, &error);
        if (session) {
            error = romaw_afp_list_volumes(session, &chooser.volumes);
            romaw_afp_close(session);
        }
    }
    show_volumes(&chooser.lists, &chooser.volumes, chooser.address, chooser.port);
    set_text(dialog, status_label,
             error                   ? "Could not list volumes on that AFP server."
             : chooser.volumes.count ? "Select a volume to mount."
                                     : "No guest volumes.");
}

void ROMlib_afp_classic_event(EventRecord* event)
{
    DialogPtr dialog = chooser.dialog;
    if (!dialog)
        return;
    GrafPtr previous = thePort;
    SetPort((GrafPtr)dialog);
    INTEGER action = 0;
    if (event->what == CWC(activateEvt)) {
        BOOLEAN active = (CW(event->modifiers) & activeFlag) != 0;
        LActivate(active, chooser.lists.servers);
        LActivate(active, chooser.lists.volumes);
        redraw_volumes(&chooser.lists);
    }
    if (event->what == CWC(keyDown) || event->what == CWC(autoKey)) {
        unsigned key = CL(event->message) & 255;
        if (CW(event->modifiers) & cmdKey) {
            if (key == 'w')
                action = CWC(cancel_button);
            else if (key == 'x')
                ROMlib_afp_classic_edit(cutCmd);
            else if (key == 'c')
                ROMlib_afp_classic_edit(copyCmd);
            else if (key == 'v')
                ROMlib_afp_classic_edit(pasteCmd);
            if (!action) {
                SetPort(previous);
                return;
            }
        }
        else if (key == '\r' || key == 3)
            action = selected_row(chooser.lists.volumes) >= 0 ? CWC(mount_button) : CWC(connect_button);
    }
    if (!action && !list_event(dialog, event, &action, &chooser.lists)) {
        HIDDEN_DialogPtr target;
        if (!DialogSelect(event, &target, &action))
            action = 0;
    }
    action = CW(action);
    if (action == rescan_button)
        refresh_servers();
    else if (action == connect_button || action == server_list_hit)
        connect_server(action == server_list_hit);
    else if (action == volume_list_hit || action == mount_button) {
        int selected = selected_row(chooser.lists.volumes);
        if (selected >= 0 && (unsigned)selected < chooser.volumes.count) {
            const char* name = chooser.volumes.items[selected].name;
            if (action == mount_button) {
                int publish_error;
                if (ROMlib_finder_mount_afp(chooser.address, chooser.port, name, &publish_error))
                    set_text(dialog, status_label, "The selected AFP volume could not be mounted.");
                else if (publish_error)
                    set_text(dialog, status_label, "Mounted here. Could not share with other ROMAW apps.");
                else
                    set_text(dialog, status_label, "Volume mounted.");
                redraw_volumes(&chooser.lists);
            }
            else {
                char status[280];
                snprintf(status, sizeof status, "Selected: %s", name);
                set_text(dialog, status_label, status);
            }
        }
    }
    enable(dialog, mount_button, selected_row(chooser.lists.volumes) >= 0);
    SetPort(previous);
    if (action == cancel_button)
        ROMlib_afp_classic_close();
}
