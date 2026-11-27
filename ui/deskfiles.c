/*+===================================================================
  File:      deskfiles.c

  Summary:   Folder-based desk accessories and classic control-panel hosting.

  Classes:   None.

  Functions: ROMlib_desk_file_p, ROMlib_desk_folder_names, ROMlib_desk_menu_forget, ROMlib_desk_menu_icon,
             ROMlib_desk_menu_select, ROMlib_desk_menu_setup, ROMlib_launch_desk_accessory,
             ROMlib_launch_separate_desk_spec, ROMlib_open_desk_file, ROMlib_run_desk_file

  Origin:    Existing repository source in executor/src/ui; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Folder-based desk accessories and classic control-panel hosting. */
#include "rsys/common.h"
#include "finder/image_volumes.h"
#include "rsys/everything.h"
#include "rsys/desk.h"
#include "rsys/prefpanel.h"
#include "OSUtil.h"
#include "rsys/device.h"
#include "rsys/wind.h"
#include "rsys/system_error.h"
#include "rsys/ecf.h"
#include "rsys/refresh.h"
#include "rsys/vdriver.h"
#include "rsys/appicon.h"
#include "rsys/resource.h"

int ROMlib_desk_host_active, ROMlib_desk_host_close;
int ROMlib_desk_resource_selected, ROMlib_desk_resource_id;

OSErr ROMlib_launch_desk_accessory(const FSSpec* file, StringPtr name)
{
    BOOLEAN named = name && name[0];
    if (!file) {
        if (!named || !GetNamedResource(TICK("DRVR"), name))
            return named ? resNotFound : paramErr;
        INTEGER refnum;
        OSErr error = OpenDriver(name, &refnum);
        if (!error) {
            DCtlHandle entry = GetDCtlEntry(CW(refnum));
            WindowPtr window = entry ? HxP(entry, dCtlWindow) : NULL;
            if (window) {
                ShowWindow(window);
                SelectWindow(window);
            }
            SEvtEnb = TRUE;
        }
        return error;
    }
    if (file->name[0] >= sizeof file->name)
        return bdNamErr;
    INTEGER previous = CurResFile();
    Handle previous_top = (Handle)MR(TopMapHndl);
    BOOLEAN load = ResLoad;
    SetResLoad(TRUE);
    INTEGER resource_file = FSpOpenResFile((FSSpec*)file, fsRdPerm);
    OSErr error = ResError();
    INTEGER id = 0;
    if (resource_file != -1) {
        Handle driver = named ? Get1NamedResource(TICK("DRVR"), name) : Get1IndResource(TICK("DRVR"), 1);
        BOOLEAN panel = !named && !driver && Count1Resources(TICK("cdev"));
        if (driver) {
            ResType type;
            Str255 title;
            GetResInfo(driver, &id, &type, title);
            error = ResError();
        }
        else
            error = panel ? noErr : resNotFound;
        if ((Handle)MR(TopMapHndl) != previous_top)
            CloseResFile(resource_file);
        UseResFile(previous);
        SetResLoad(load);
        if (!error)
            return panel ? ROMlib_launch_desk_file(file) : ROMlib_launch_desk_resource(file, CW(id));
    }
    else {
        UseResFile(previous);
        SetResLoad(load);
    }
    return error ? error : resNotFound;
}
#if defined(HAVE_LIBSDL)
extern int ROMlib_sdl_desk_drag(void);
#endif

static const OSType folders[] = {kAppleMenuFolderType, kControlPanelFolderType};
static MenuHandle apple_menu, panels_menu;
static const OSType menu_icon_types[] = {0x49434e23, 0x69637323, 0x69636c34, 0x69636c38, 0x69637334, 0x69637338};
static const Size menu_icon_sizes[] = {256, 64, 512, 1024, 128, 256};

typedef struct {
    MenuHandle menu;
    Str255 name;
    unsigned present;
    uint8 bytes[2240];
} desk_menu_icon;

static desk_menu_icon menu_icons[128];
static unsigned next_menu_icon;
static BOOLEAN open_folder_item(StringPtr name, unsigned folder, INTEGER* result);

/* Use File Manager catalog calls so HFS images and host sidecars agree. */
static BOOLEAN folder_item(unsigned folder, INTEGER index, FSSpec* spec, OSType* type)
{
    INTEGER volume;
    LONGINT directory;
    CInfoPBRec pb = {0};
    Str255 name;
    if (FindFolder(kOnSystemDisk, folders[folder], FALSE, &volume, &directory))
        return FALSE;
    FIELD_ASSIGN_RAW(pb.hFileInfo.ioNamePtr, RM((StringPtr)name));
    pb.hFileInfo.ioVRefNum = volume;
    pb.hFileInfo.ioDirID = directory;
    pb.hFileInfo.ioFDirIndex = CW(index);
    if (PBGetCatInfo(&pb, FALSE))
        return FALSE;
    spec->vRefNum = volume;
    spec->parID = directory;
    if (name[0] >= sizeof spec->name) {
        spec->name[0] = 0;
        *type = 0;
        return TRUE;
    }
    memcpy(spec->name, name, name[0] + 1);
    *type = (pb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR) ? 0 : CL(pb.hFileInfo.ioFlFndrInfo.fdType);
    return TRUE;
}

static BOOLEAN supported_type(OSType type)
{
    return type == TICK("dfil") || type == TICK("cdev") || type == TICK("APPL");
}

static void folder_names(Handle names, unsigned folder)
{
    if (!names)
        return;
    for (int index = 1; index <= 32767; ++index) {
        FSSpec spec;
        OSType type;
        if (!folder_item(folder, index, &spec, &type))
            break;
        if (!supported_type(type) || !spec.name[0] || spec.name[1] == '.')
            continue;
        Size offset = 0, size = GetHandleSize(names);
        while (offset < size) {
            StringPtr p = (StringPtr)STARH(names) + offset;
            INTEGER order = RelString(p, spec.name, FALSE, TRUE);
            if (order >= 0)
                break;
            offset += p[0] + 1;
        }
        if (offset == size || !EqualString((StringPtr)STARH(names) + offset, spec.name, FALSE, TRUE))
            Munger(names, offset, NULL, 0, (Ptr)spec.name, spec.name[0] + 1);
    }
}

void ROMlib_desk_folder_names(Handle names)
{
    folder_names(names, 0);
}

void ROMlib_desk_menu_forget(MenuHandle menu)
{
    for (unsigned i = 0; i < NELEM(menu_icons); ++i)
        if (!menu || menu_icons[i].menu == menu || (menu == apple_menu && menu_icons[i].menu == panels_menu))
            menu_icons[i].menu = NULL;
    if (!menu)
        apple_menu = panels_menu = NULL;
    else if (menu == apple_menu) {
        MenuHandle child = panels_menu;
        apple_menu = panels_menu = NULL;
        if (child) {
            DeleteMenu(CW(HxX(child, menuID)));
            DisposeMenu(child);
        }
    }
    else if (menu == panels_menu)
        panels_menu = NULL;
}

void ROMlib_desk_menu_setup(MenuHandle menu)
{
    Handle names;
    INTEGER id;
    if (apple_menu == menu)
        return;
    if (apple_menu)
        ROMlib_desk_menu_forget(apple_menu);
    apple_menu = menu;
    Str255 name;
    BOOLEAN has_chooser = FALSE;
    for (INTEGER item = 1; item <= CountMItems(menu); ++item) {
        GetItem(menu, item, name);
        if (EqualString(name, (StringPtr)"\007Chooser", FALSE, TRUE))
            has_chooser = TRUE;
    }
    if (!has_chooser)
        AppendMenu(menu, (StringPtr)"\007Chooser");
    names = NewHandle(0);
    if (!names)
        return;
    folder_names(names, 1);
    if (!GetHandleSize(names)) {
        DisposHandle(names);
        return;
    }
    for (id = 200; id < 256 && GetMHandle(id); ++id)
        ;
    if (id < 256)
        panels_menu = NewMenu(id, (StringPtr) "\016Control Panels");
    if (panels_menu) {
        Size offset = 0;
        HLock(names);
        while (offset < GetHandleSize(names)) {
            StringPtr name = (StringPtr)STARH(names) + offset;
            AppendMenu(panels_menu, (StringPtr) "\001 ");
            SetItem(panels_menu, CountMItems(panels_menu), name);
            offset += name[0] + 1;
        }
        HUnlock(names);
        InsertMenu(panels_menu, -1);
        AppendMenu(menu, (StringPtr) "\016Control Panels");
        INTEGER item = CountMItems(menu);
        SetItemCmd(menu, item, 0x1b);
        SetItemMark(menu, item, id);
    }
    DisposHandle(names);
}

BOOLEAN
ROMlib_desk_menu_select(LONGINT selection)
{
    INTEGER item = selection & 0xffff;
    if (!item)
        return FALSE;
    Str255 name;
    if (apple_menu && (selection >> 16) == CW(HxX(apple_menu, menuID))) {
        GetItem(apple_menu, item, name);
        if (EqualString(name, (StringPtr)"\007Chooser", FALSE, TRUE)) {
            HiliteMenu(0);
            ROMlib_finder_afp_chooser(NULL);
            return TRUE;
        }
        if (!EqualString(name, (StringPtr)ROMAW_PREFS_MENU_NAME, TRUE, TRUE))
            return FALSE;
        HiliteMenu(0);
        ROMlib_show_preferences();
        return TRUE;
    }
    if (!panels_menu || (selection >> 16) != CW(HxX(panels_menu, menuID)))
        return FALSE;
    INTEGER result;
    GetItem(panels_menu, item, name);
    HiliteMenu(0);
    open_folder_item(name, 1, &result);
    return TRUE;
}

static Handle menu_icon_suite(const desk_menu_icon* icon)
{
    Handle suite = NULL;
    Size offset = 0;
    if (!(icon->present & 1) || NewIconSuite(&suite))
        return NULL;
    suite = MR(suite);
    for (unsigned i = 0; i < NELEM(menu_icon_types); ++i) {
        if (icon->present & (1u << i)) {
            Handle copy = NewHandle(menu_icon_sizes[i]);
            if (!copy)
                goto failed;
            memcpy(STARH(copy), icon->bytes + offset, menu_icon_sizes[i]);
            if (AddIconToSuite(copy, suite, menu_icon_types[i])) {
                DisposHandle(copy);
                goto failed;
            }
        }
        offset += menu_icon_sizes[i];
    }
    return suite;
failed:
    DisposeIconSuite(suite, TRUE);
    return NULL;
}

Handle ROMlib_desk_menu_icon(MenuHandle menu, StringPtr name)
{
    FSSpec spec;
    FInfo info;
    OSType type;
    desk_menu_icon* cached;
    INTEGER previous, file;
    Handle previous_top;
    BOOLEAN load;
    short id = 0;
    int found = FALSE;
    unsigned folder;
    if (!menu || (menu != apple_menu && menu != panels_menu))
        return NULL;
    for (unsigned i = 0; i < NELEM(menu_icons); ++i)
        if (menu_icons[i].menu == menu && EqualString(menu_icons[i].name, name, FALSE, TRUE))
            return menu_icon_suite(&menu_icons[i]);
    cached = &menu_icons[next_menu_icon++ % NELEM(menu_icons)];
    cached->menu = menu;
    memcpy(cached->name, name, name[0] + 1);
    cached->present = 0;
    folder = menu == panels_menu;
    for (int index = 1; index <= 32767; ++index) {
        if (!folder_item(folder, index, &spec, &type))
            return NULL;
        if (supported_type(type) && EqualString(name, spec.name, FALSE, TRUE))
            break;
        if (index == 32767)
            return NULL;
    }
    if (FSpGetFInfo(&spec, &info))
        return NULL;
    previous = CurResFile();
    previous_top = (Handle)MR(TopMapHndl);
    load = ResLoad;
    file = FSpOpenResFile(&spec, fsRdPerm);
    if (file == -1)
        return NULL;
    UseResFile(file);
    SetResLoad(TRUE);
    for (int i = 1; i <= Count1Resources(TICK("FREF")) && !found; ++i) {
        Handle ref = Get1IndResource(TICK("FREF"), i);
        if (!ref || !STARH(ref) || GetHandleSize(ref) < 6 || memcmp(STARH(ref), &info.fdType, 4))
            continue;
        unsigned local = ROMlib_icon_word((unsigned char*)STARH(ref) + 4);
        for (int j = 1; j <= Count1Resources(TICK("BNDL")); ++j) {
            Handle bundle = Get1IndResource(TICK("BNDL"), j);
            if (bundle && STARH(bundle) &&
                ROMlib_bundle_icon_id((unsigned char*)STARH(bundle), GetHandleSize(bundle), CL(info.fdCreator), local,
                                      &id)) {
                found = TRUE;
                break;
            }
        }
    }
    if (!found && !Count1Resources(TICK("BNDL")) && Count1Resources(TICK("ICN#")) == 1) {
        Handle mono = Get1IndResource(TICK("ICN#"), 1);
        ResType resource_type;
        Str255 title;
        INTEGER resource_id;
        GetResInfo(mono, &resource_id, &resource_type, title);
        id = CW(resource_id);
        found = TRUE;
    }
    if (found) {
        Size offset = 0;
        for (unsigned i = 0; i < NELEM(menu_icon_types); ++i) {
            Handle resource = Get1Resource(menu_icon_types[i], id);
            if (resource && STARH(resource) && GetHandleSize(resource) >= menu_icon_sizes[i]) {
                memcpy(cached->bytes + offset, STARH(resource), menu_icon_sizes[i]);
                cached->present |= 1u << i;
            }
            offset += menu_icon_sizes[i];
        }
    }
    /* OpenResFile reuses an already-open map. Never close the application's
     resource file merely because that application is also an Apple item. */
    if ((Handle)MR(TopMapHndl) != previous_top)
        CloseResFile(file);
    UseResFile(previous);
    SetResLoad(load);
    return menu_icon_suite(cached);
}

BOOLEAN
ROMlib_desk_file_p(void)
{
    return Count1Resources(TICK("cdev")) > 0 || Count1Resources(TICK("DRVR")) > 0;
}

BOOLEAN
ROMlib_launch_separate_desk_spec(const FSSpec* file, OSErr* result)
{
    FInfo info;
    FSSpec spec;
    OSType type;
    if (!file || !result)
        return FALSE;
    spec = *file;
    if (FSpGetFInfo(&spec, &info))
        return FALSE;
    type = CL(info.fdType);
    if (type != TICK("dfil") && type != TICK("cdev"))
        return FALSE;
    *result = ROMlib_launch_desk_file(&spec);
    return TRUE;
}

/* cdev's Pascal ABI: four INTEGERs, EventRecord*, LONGINT, DialogPtr -> LONGINT.
   The Finder supplies zero pre-existing items and cdevUnset (3) initially.
   See Inside Macintosh: More Macintosh Toolbox, Control Panels. */
static LONGINT panel_call(Handle code, INTEGER message, INTEGER item, EventRecord* event, LONGINT value,
                          DialogPtr dialog)
{
    LONGINT result =
        CToPascalCall(STARH(code), 4 | (2 << 3) | (2 << 6) | (2 << 9) | (2 << 12) | (5 << 15) | (4 << 18) | (5 << 21),
                      message, item, 0, 0, event, value, dialog);
    if (message == 0 || message == 8 || message == 2)
        fprintf(stderr, "trace: cdev message=%d result=0x%08lx\n", message, (unsigned long)result);
    return result;
}

static OSErr run_panel(StringPtr title)
{
    Handle code = Get1Resource(TICK("cdev"), -4064);
    Handle items = Get1Resource(TICK("DITL"), -4064);
    Handle rectangles = Get1Resource(TICK("nrct"), -4064);
    Handle machine = Get1Resource(TICK("mach"), -4064);
    HIDDEN_GrafPtr saved_port;
    DialogPtr dialog;
    EventRecord event = {0};
    Rect bounds;
    int width = 330, height = 270;
    LONGINT value = 3;
    BOOLEAN done = FALSE;
    if (!code || GetHandleSize(code) < 2 || !items || GetHandleSize(items) < 2)
        return resNotFound;
    if (rectangles && GetHandleSize(rectangles) >= 2) {
        uint8* p = (uint8*)STARH(rectangles);
        unsigned count = (p[0] << 8) | p[1];
        if (count > (unsigned)(GetHandleSize(rectangles) - 2) / 8)
            return paramErr;
        width = height = 0;
        for (unsigned i = 0; i < count; ++i) {
            Rect r;
            memcpy(&r, p + 2 + i * 8, sizeof r);
            width = MAX(width, CW(r.right) + 8);
            height = MAX(height, CW(r.bottom) + 8);
        }
        if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
            return paramErr;
    }
    SetRect(&bounds, 40, 65, 40 + width, 65 + height);
    SignedByte code_state = HGetState(code);
    HLock(code);
    Handle item_copy = items;
    if (HandToHand(&item_copy)) {
        HSetState(code, code_state);
        return memFullErr;
    }
    items = item_copy;
    GetPort(&saved_port);
    dialog = NewDialog(NULL, &bounds, title, FALSE, documentProc, (WindowPtr)-1, TRUE, 0, items);
    if (!dialog) {
        DisposHandle(items);
        HSetState(code, code_state);
        return memFullErr;
    }
    SetPort((GrafPtr)dialog);
    TextFont(1);
    TextSize(9);
    if (machine && GetHandleSize(machine) >= 4 && !memcmp(STARH(machine), "\000\000\377\377", 4) &&
        panel_call(code, 8, 0, &event, 3, dialog) != 1) {
        DisposDialog(dialog);
        SetPort(MR(HIDDEN_RAW(saved_port)));
        HSetState(code, code_state);
        return paramErr;
    }
    ShowWindow((WindowPtr)dialog);
    DrawDialog(dialog);
    value = panel_call(code, 0, 0, &event, value, dialog);
    OSErr error = noErr;
    if (value == -1 || value == 0 || value == 1)
        error = value == 0 ? memFullErr : resNotFound;
    if (!error) {
        /* Initialization may create and draw the dialog in several stages. Do
         not recreate SDL's surface until the cdev has finished that work. */
        ROMlib_desk_host_active = TRUE;
        vdriver_update_screen(0, 0, vdriver_height, vdriver_width, FALSE);
    }
    while (!done && !error) {
        HIDDEN_DialogPtr selected;
        HIDDEN_WindowPtr window;
        INTEGER item = 0;
        WaitNextEvent(everyEvent, &event, 1, NULL);
        if (ROMlib_desk_host_close)
            break;
        SetPort((GrafPtr)dialog);
        unsigned what = CW(event.what);
        if (what == mouseDown) {
            Point where = {CW(event.where.v), CW(event.where.h)};
            INTEGER part = FindWindow(where, &window);
            if (MR(HIDDEN_RAW(window)) == (WindowPtr)dialog && part == inGoAway) {
                done = TrackGoAway((WindowPtr)dialog, where);
                continue;
            }
            if (MR(HIDDEN_RAW(window)) == (WindowPtr)dialog && part == inDrag) {
#if defined(HAVE_LIBSDL)
                if (ROMlib_desk_host_active && ROMlib_sdl_desk_drag())
                    continue;
#endif
                Rect drag = {CWC(20), CWC(0), CWC(32767), CWC(32767)};
                DragWindow((WindowPtr)dialog, where, &drag);
                continue;
            }
            if (part != inContent || MR(HIDDEN_RAW(window)) != (WindowPtr)dialog)
                continue;
        }
        if ((what == keyDown || what == autoKey) && (CW(event.modifiers) & cmdKey) &&
            ((CL(event.message) & 255) == 'w' || (CL(event.message) & 255) == 'q')) {
            done = TRUE;
            continue;
        }
        if (what == keyDown || what == autoKey)
            value = panel_call(code, 7, 0, &event, value, dialog);
        if (what == updateEvt && CL(event.message) == US_TO_SYN68K(dialog)) {
            BeginUpdate((WindowPtr)dialog);
            DrawDialog(dialog);
            value = panel_call(code, 4, 0, &event, value, dialog);
            EndUpdate((WindowPtr)dialog);
        }
        else if (what == activateEvt && CL(event.message) == US_TO_SYN68K(dialog))
            value = panel_call(code, (CW(event.modifiers) & activeFlag) ? 5 : 6, 0, &event, value, dialog);
        else if (what == nullEvent)
            value = panel_call(code, 3, 0, &event, value, dialog);
        if (what != updateEvt && what != activateEvt && DialogSelect(&event, &selected, &item) &&
            MR(HIDDEN_RAW(selected)) == dialog)
            value = panel_call(code, 1, CW(item), &event, value, dialog);
        if (value == -1 || value == 0 || value == 1)
            error = value == 0 ? memFullErr : resNotFound;
    }
    if (!error)
        panel_call(code, 2, 0, &event, value, dialog);
    DisposDialog(dialog);
    SetPort(MR(HIDDEN_RAW(saved_port)));
    HSetState(code, code_state);
    return error;
}

static void fit_accessory_window(WindowPtr window)
{
    RgnHandle structure = WINDOW_STRUCT_REGION(window);
    RgnHandle content = WINDOW_CONT_REGION(window);
    if (!structure || !content)
        return;
    Rect bounds = HxX(structure, rgnBBox);
    Rect inside = HxX(content, rgnBBox);
    int left = CW(bounds.left), top = CW(bounds.top);
    int width = CW(bounds.right) - left;
    int height = CW(bounds.bottom) - top;
    if (width <= 0 || height <= 0 || vdriver_width <= 0 || vdriver_height <= 0)
        return;
    int target_left = MAX(0, MIN(left, vdriver_width - width));
    int target_top = MAX(20, MIN(top, vdriver_height - height));
    if (target_left != left || target_top != top)
        MoveWindow(window, CW(inside.left) + target_left - left, CW(inside.top) + target_top - top, FALSE);
}

static OSErr run_accessory_driver(StringPtr name)
{
    INTEGER ref;
    /* An accessory may display a modal error while opening, before returning
     its main window. Keep that error reachable by the host presentation. */
    ROMlib_desk_host_active = TRUE;
    OSErr error = OpenDriver(name, &ref);
    if (error)
        return error;
    ref = CW(ref);
    DCtlHandle entry = GetDCtlEntry(ref);
    /* Like OpenDeskAcc, present the window after the driver has opened. Some
     accessories (including Note Pad and Scrapbook) create it invisibly. */
    WindowPtr window = entry ? HxP(entry, dCtlWindow) : NULL;
    if (window) {
        /* A DA's saved position can lie outside the current virtual desktop.
         The cropped host surface cannot display pixels beyond that desktop. */
        ShowWindow(window);
        fit_accessory_window(window);
        SelectWindow(window);
    }
    BOOLEAN saved_event_enable = SEvtEnb;
    SEvtEnb = TRUE;
    ROMlib_desk_host_active = TRUE;
    vdriver_update_screen(0, 0, vdriver_height, vdriver_width, FALSE);
    while (entry && (HxX(entry, dCtlFlags) & CWC(DRIVEROPENBIT))) {
        EventRecord event;
        HIDDEN_WindowPtr window;
        SystemTask();
        WaitNextEvent(everyEvent, &event, 1, NULL);
        if (ROMlib_desk_host_close) {
            CloseDeskAcc(ref);
            break;
        }
        if (CW(event.what) == mouseDown) {
            Point where = {CW(event.where.v), CW(event.where.h)};
            INTEGER part = FindWindow(where, &window);
#if defined(HAVE_LIBSDL)
            if (part == inDrag && ROMlib_sdl_desk_drag())
                continue;
#endif
            if (part != inMenuBar)
                SystemClick(&event, MR(HIDDEN_RAW(window)));
        }
        if ((CW(event.what) == keyDown || CW(event.what) == autoKey) && (CW(event.modifiers) & cmdKey) &&
            ((CL(event.message) & 255) == 'w' || (CL(event.message) & 255) == 'q'))
            CloseDeskAcc(ref);
        entry = GetDCtlEntry(ref);
    }
    SEvtEnb = saved_event_enable;
    return noErr;
}

static OSErr run_accessory(void)
{
    Handle driver = ROMlib_desk_resource_selected ? Get1Resource(TICK("DRVR"), ROMlib_desk_resource_id)
                                                  : Get1IndResource(TICK("DRVR"), 1);
    Str255 name;
    INTEGER id;
    ResType type;
    if (!driver || GetHandleSize(driver) < 18)
        return resNotFound;
    GetResInfo(driver, &id, &type, name);

    INTEGER saved_system = SysMap, saved_boot = BootDrive;
    HIDDEN_Handle saved_system_map = SysMapHndl_H;
    resmaphand map = ROMlib_rntohandl(CurResFile(), NULL);
    if (!map)
        return resFNotFound;
    /* System 6 accessories switch to SysMap/SysMapHndl to find their private
     resources. A standalone dfil keeps those resources in its own fork,
     followed by the real System map in the existing resource chain. */
    SysMap = CW(CurResFile());
    HIDDEN_RAW_SET(SysMapHndl_H, RM(map));

    WDPBRec panels = {0};
    BOOLEAN opened_panels = FALSE;
    if (EqualString(name, (StringPtr) "\016\000Control Panel", FALSE, TRUE)) {
        INTEGER volume;
        LONGINT directory;
        /* The original Control Panel DA enumerates cdev files using BootDrive
         (the System Folder working directory). Supply the System 7 folder
         for that legacy search without moving or copying any panel files. */
        if (FindFolder(kOnSystemDisk, kControlPanelFolderType, FALSE, &volume, &directory) == noErr) {
            panels.ioVRefNum = volume;
            panels.ioWDDirID = directory;
            panels.ioWDProcID = TICKX("exda");
            if (PBOpenWD(&panels, FALSE) == noErr) {
                opened_panels = TRUE;
                BootDrive = panels.ioVRefNum;
            }
        }
    }
    OSErr error = run_accessory_driver(name);
    BootDrive = saved_boot;
    SysMap = saved_system;
    SysMapHndl_H = saved_system_map;
    if (opened_panels)
        PBCloseWD(&panels, FALSE);
    return error;
}

OSErr ROMlib_run_desk_file(INTEGER refnum, StringPtr title)
{
    INTEGER previous = CurResFile();
    BOOLEAN load = ResLoad;
    int saved_host_active = ROMlib_desk_host_active;
    int saved_host_close = ROMlib_desk_host_close;
    HIDDEN_GrafPtr port;
    GetPort(&port);
    UseResFile(refnum);
    SetResLoad(TRUE);
    /* Both cdev panels and DRVR accessories use the same cropped, shaped host
     presentation. Restore the caller's state on every return path. */
    ROMlib_desk_host_close = FALSE;
    OSErr error = !ROMlib_desk_resource_selected && Count1Resources(TICK("cdev")) ? run_panel(title) : run_accessory();
    ROMlib_desk_host_active = saved_host_active;
    ROMlib_desk_host_close = saved_host_close;
    SetResLoad(load);
    UseResFile(previous);
    SetPort(MR(HIDDEN_RAW(port)));
    return error;
}

static BOOLEAN open_folder_item(StringPtr name, unsigned folder, INTEGER* result)
{
    /* A leading NUL is the traditional DA-name marker, not a filename byte. */
    Str255 filename;
    memcpy(filename, name, name[0] + 1);
    if (filename[0] && !filename[1]) {
        --filename[0];
        memmove(filename + 1, filename + 2, filename[0]);
    }
    for (int index = 1; index <= 32767; ++index) {
        FSSpec spec;
        OSType type;
        if (!folder_item(folder, index, &spec, &type))
            break;
        if (!supported_type(type) || !EqualString(filename, spec.name, FALSE, TRUE))
            continue;
        if (type == TICK("APPL")) {
            LaunchParamBlockRec launch = {0};
            launch.launchBlockID = CWC(extendedBlock);
            launch.launchEPBLength = CLC(extendedBlockLen);
            PP_ASSIGN(launch.launchAppSpec, &spec);
            *result = NewLaunch(spec.name, CW(spec.vRefNum), &launch);
            return TRUE;
        }
        *result = ROMlib_launch_desk_file(&spec);
        if (*result)
            system_error(
                "This Apple menu item could not be opened. Its resources or required Macintosh services are unavailable.",
                0, "OK", NULL, NULL, NULL, NULL, NULL);
        return TRUE;
    }
    return FALSE;
}

BOOLEAN
ROMlib_open_desk_file(StringPtr name, INTEGER* result)
{
    for (unsigned folder = 0; folder < NELEM(folders); ++folder)
        if (open_folder_item(name, folder, result))
            return TRUE;
    return FALSE;
}
