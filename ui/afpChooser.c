/*+===================================================================
  File:      afpChooser.c

  Summary:   Connect Chooser's AppleShare IP action to native AFP.

  Classes:   None.

  Functions: ROMlib_afp_install_chooser_entry.

  Origin:    ROMAW native replacement for the AppleShare IP connect selector.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include "rsys/common.h"
#include "ControlMgr.h"
#include "DialogMgr.h"
#include "MemoryMgr.h"
#include "OSUtil.h"
#include "EditionMgr.h"
#include "QuickDraw.h"
#include "WindowMgr.h"
#include "rsys/afp_catalog.h"
#include "rsys/file.h"
#include "rsys/host_mactcp.h"
#include "rsys/host_names.h"
#include "rsys/mactcp.h"
#include "rsys/system_error.h"

static void word(unsigned char* bytes, unsigned value)
{
    bytes[0] = value >> 8;
    bytes[1] = value;
}

static unsigned char* dialog_item(unsigned char* p, unsigned type, unsigned left, unsigned top, unsigned right,
                                  unsigned bottom, const char* label)
{
    memset(p, 0, 4);
    word(p + 4, top);
    word(p + 6, left);
    word(p + 8, bottom);
    word(p + 10, right);
    size_t length = strlen(label);
    p[12] = type;
    p[13] = length;
    memcpy(p + 14, label, length);
    if (length & 1)
        p[14 + length] = 0;
    return p + 14 + ((length + 1) & ~1);
}

static int read_text(DialogPtr dialog, INTEGER number, char output[256])
{
    INTEGER type;
    HIDDEN_Handle item;
    Rect bounds;
    Str255 text;
    GetDItem(dialog, number, &type, &item, &bounds);
    if (!STARH(&item))
        return 0;
    GetIText(MR(HIDDEN_RAW(item)), text);
    int length = ROMlib_mac_name_to_utf8((const char*)text + 1, text[0], output, 256);
    if (length <= 0)
        return 0;
    while (length && (unsigned char)output[length - 1] <= ' ')
        output[--length] = 0;
    return length > 0;
}

static Handle item_handle(DialogPtr dialog, INTEGER number)
{
    INTEGER type;
    HIDDEN_Handle item;
    Rect bounds;
    GetDItem(dialog, number, &type, &item, &bounds);
    return MR(HIDDEN_RAW(item));
}

static void text_item(DialogPtr dialog, INTEGER number, const char* text)
{
    Str255 label;
    size_t length = MIN(strlen(text), 255);
    label[0] = length;
    memcpy(label + 1, text, length);
    SetIText(item_handle(dialog, number), label);
}

enum { browser_rows = 8 };

static OSErr browser_page(DialogPtr dialog, INTEGER volume, uint32 directory, unsigned first,
                          uint32 folders[browser_rows], uint32* parent, BOOLEAN* next)
{
    CInfoPBRec info = {0};
    Str255 name;
    info.dirInfo.ioVRefNum = CW(volume);
    info.dirInfo.ioDrDirID = CL(directory);
    info.dirInfo.ioFDirIndex = CWC(-1);
    FIELD_ASSIGN_RAW(info.dirInfo.ioNamePtr, RM((StringPtr)name));
    OSErr error = PBGetCatInfo(&info, FALSE);
    if (error)
        return error;
    *parent = CL(info.dirInfo.ioDrParID);
    char heading[260] = "Folder: ";
    memcpy(heading + 8, name + 1, MIN(name[0], sizeof heading - 9));
    heading[8 + MIN(name[0], sizeof heading - 9)] = 0;
    text_item(dialog, 5, heading);

    for (unsigned i = 0; i <= browser_rows; ++i) {
        if (first + i > 32767)
            error = fnfErr;
        else {
            memset(&info, 0, sizeof info);
            info.hFileInfo.ioVRefNum = CW(volume);
            info.hFileInfo.ioDirID = CL(directory);
            info.hFileInfo.ioFDirIndex = CW(first + i);
            FIELD_ASSIGN_RAW(info.hFileInfo.ioNamePtr, RM((StringPtr)name));
            error = PBGetCatInfo(&info, FALSE);
        }
        if (error && error != fnfErr)
            return error;
        if (i == browser_rows) {
            *next = !error;
            break;
        }
        ControlHandle control = (ControlHandle)item_handle(dialog, 6 + i);
        folders[i] = 0;
        if (error) {
            HideControl(control);
            continue;
        }
        int is_folder = !!(info.hFileInfo.ioFlAttrib & ATTRIB_ISADIR);
        if (is_folder)
            folders[i] = CL(info.dirInfo.ioDrDirID);
        Str255 title;
        const char* prefix = is_folder ? "Folder: " : "File: ";
        unsigned prefix_size = strlen(prefix), size = MIN(name[0], 53);
        title[0] = prefix_size + size;
        memcpy(title + 1, prefix, prefix_size);
        memcpy(title + 1 + prefix_size, name + 1, size);
        SetCTitle(control, title);
        HiliteControl(control, is_folder ? 0 : 255);
        ShowControl(control);
    }
    HiliteControl((ControlHandle)item_handle(dialog, 2), *parent >= 2 ? 0 : 255);
    HiliteControl((ControlHandle)item_handle(dialog, 3), first > 1 ? 0 : 255);
    HiliteControl((ControlHandle)item_handle(dialog, 4), *next ? 0 : 255);
    return noErr;
}

static void browse_volume(INTEGER volume)
{
    unsigned char data[512];
    word(data, 4 + browser_rows);
    unsigned char* p = data + 2;
    p = dialog_item(p, 4, 265, 224, 333, 246, "Done");
    p = dialog_item(p, 4, 16, 224, 81, 246, "Up");
    p = dialog_item(p, 4, 89, 224, 161, 246, "Previous");
    p = dialog_item(p, 4, 169, 224, 239, 246, "Next");
    p = dialog_item(p, 136, 16, 12, 333, 33, "Mounted AFP volume");
    for (unsigned i = 0; i < browser_rows; ++i)
        p = dialog_item(p, 4, 16, 38 + 22 * i, 333, 58 + 22 * i, "");
    Handle items = NewHandle(p - data);
    if (!items)
        return;
    memcpy(STARH(items), data, p - data);
    Rect bounds;
    SetRect(&bounds, 130, 80, 480, 336);
    HIDDEN_GrafPtr saved;
    GetPort(&saved);
    DialogPtr dialog =
        NewDialog(NULL, &bounds, (StringPtr) "\021Browse AFP Volume", TRUE, dBoxProc, (WindowPtr)-1, FALSE, 0, items);
    if (!dialog) {
        DisposHandle(items);
        return;
    }
    SetPort((GrafPtr)dialog);
    uint32 directory = 2;
    unsigned first = 1;
    for (;;) {
        uint32 folders[browser_rows], parent;
        BOOLEAN next;
        OSErr error = browser_page(dialog, volume, directory, first, folders, &parent, &next);
        if (error) {
            char message[96];
            snprintf(message, sizeof message, "AFP directory could not be listed (error %d).", error);
            system_error(message, 0, "OK", NULL, NULL, NULL, NULL, NULL);
            break;
        }
        INTEGER item = 0;
        ModalDialog(NULL, &item);
        item = CW(item);
        if (item == 1)
            break;
        if (item == 2 && parent >= 2) {
            directory = parent;
            first = 1;
        }
        else if (item == 3 && first > 1)
            first -= browser_rows;
        else if (item == 4 && next)
            first += browser_rows;
        else if (item >= 6 && item < 6 + browser_rows && folders[item - 6]) {
            directory = folders[item - 6];
            first = 1;
        }
    }
    DisposDialog(dialog);
    SetPort(MR(HIDDEN_RAW(saved)));
}

static OSErr connect_dialog(void)
{
    unsigned char data[256];
    word(data, 5);
    unsigned char* p = data + 2;
    p = dialog_item(p, 4, 245, 112, 330, 134, "Mount");
    p = dialog_item(p, 4, 148, 112, 233, 134, "Cancel");
    p = dialog_item(p, 136, 16, 17, 106, 35, "Server");
    p = dialog_item(p, 16, 110, 15, 329, 36, "");
    p = dialog_item(p, 136, 16, 55, 106, 75, "Volume");
    p = dialog_item(p, 16, 110, 54, 329, 76, "");
    Handle items = NewHandle(p - data);
    if (!items)
        return memFullErr;
    memcpy(STARH(items), data, p - data);
    Rect bounds;
    SetRect(&bounds, 130, 120, 476, 268);
    HIDDEN_GrafPtr previous;
    GetPort(&previous);
    DialogPtr dialog =
        NewDialog(NULL, &bounds, (StringPtr) "\016AFP Connection", TRUE, dBoxProc, (WindowPtr)-1, FALSE, 0, items);
    if (!dialog) {
        DisposHandle(items);
        return memFullErr;
    }
    SetPort((GrafPtr)dialog);
    SelIText(dialog, 4, 0, 32767);
    INTEGER reference = 0;
    OSErr result = userCanceledErr;
    for (;;) {
        INTEGER item = 0;
        ModalDialog(NULL, &item);
        if (CW(item) == 2)
            break;
        if (CW(item) != 1)
            continue;
        char server[256], volume[256];
        if (!read_text(dialog, 4, server) || !read_text(dialog, 6, volume)) {
            SysBeep(1);
            continue;
        }
        uint32 addresses[4];
        char canonical[255];
        result = host_mactcp_resolver_open();
        if (!result) {
            result = host_mactcp_resolve(server, addresses, canonical);
            host_mactcp_resolver_close();
        }
        if (!result) {
            result = ROMlib_afp_register_volume(addresses[0], 548, volume, &reference);
            if (result == fnfErr) {
                system_error("The AFP server could not find that volume. Check its name and try again.", 0, "OK", NULL,
                             NULL, NULL, NULL, NULL);
                SetPort((GrafPtr)dialog);
                continue;
            }
        }
        if (!result)
            break;
        char error[110];
        snprintf(error, sizeof error, "AFP could not mount the requested volume (error %d).", result);
        system_error(error, 0, "OK", NULL, NULL, NULL, NULL, NULL);
        SetPort((GrafPtr)dialog);
    }
    DisposDialog(dialog);
    SetPort(MR(HIDDEN_RAW(previous)));
    if (!result)
        browse_volume(reference);
    return result;
}

static syn68k_addr_t chooser_connect(syn68k_addr_t address, void* context)
{
    (void)address;
    (void)context;
    /* The patch is a JSR. Its synthetic return identifies this movable CODE
       handle without storing a host pointer to the resource contents. */
    uint32 resume = POPADDR();
    if (READUW(EM_A7 + 22) != 19)
        return resume - 20 + 0x5ae6;
    OSErr result = connect_dialog();
    WRITEUW(EM_A7 + 24, result == noErr ? 0 : -1);
    EM_D0 = result == noErr ? 0 : -1;
    uint32 target = POPADDR();
    EM_A7 += 20;
    return target;
}

void ROMlib_afp_install_chooser_entry(Handle resource)
{
    /* AppleShare IP 3.7.4: PACK -4096 branches past its 14-byte
       descriptor into the dispatcher. Preserve the descriptor and all
       non-connect selectors. Other releases keep their original code. */
    static const unsigned char signature[] = {
        0x60, 0x0e, 0x00, 0x4d, 'P',  'A',  'C',  'K',  0xf0, 0x00,
        0x00, 0x0e, 0x1f, 0x0a, 0x78, 0x00, 0x4e, 0xfa, 0x5a, 0xd4,
    };
    unsigned char* bytes = (unsigned char*)STARH(resource);
    if (GetHandleSize(resource) != 45590 || !bytes || memcmp(bytes, signature, sizeof signature))
        return;
    static syn68k_addr_t callback;
    if (!callback)
        callback = callback_install(chooser_connect, NULL);
    if (!callback || callback >= 0x8000)
        return;
    word(bytes + 16, 0x4eb8);
    word(bytes + 18, callback);
    ROMlib_destroy_blocks(US_TO_SYN68K(bytes + 16), 4, TRUE);
}
