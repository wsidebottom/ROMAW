/*+===================================================================
  File:      afpCatalog.c

  Summary:   Decode native AFP catalog entries for File Manager callers.

  Classes:   None.

  Functions: ROMlib_afp_catalog_index, ROMlib_afp_file_error, ROMlib_afp_is_volume,
             ROMlib_afp_register_volume, ROMlib_afp_mounted_catalog, ROMlib_afp_is_fork, ROMlib_afp_open,
             ROMlib_afp_read, ROMlib_afp_close_fork, ROMlib_afp_set_position, ROMlib_afp_unmount,
             ROMlib_afp_volume_mount, ROMlib_afp_open_wd, ROMlib_afp_publish_mount,
             ROMlib_afp_sync_mount, ROMlib_afp_volume_source, ROMlib_finder_mount_afp.

  Origin:    ROMAW bridge between AFP catalog records and classic parameter blocks.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include "rsys/common.h"
#include "finder/image_volumes.h"
#include "FileMgr.h"
#include "MemoryMgr.h"
#include "OSUtil.h"
#include "rsys/hfs.h"
#include "rsys/afp_catalog.h"
#include "rsys/configprefs.h"
#include "rsys/user_profile.h"
#include "ToolboxEvent.h"
#include "rsys/file.h"
#include "rsys/host_names.h"
#include "rsys/mactcp.h"

OSErr ROMlib_afp_file_error(int32 result)
{
    switch (result) {
    case 0:
        return noErr;
    case -5000:
        return permErr;
    case -5008:
        return dskFulErr;
    case -5009:
        return eofErr;
    case -5010:
        return fBsyErr;
    case -5017:
        return dupFNErr;
    case -5018:
        return fnfErr;
    case -5019:
        return paramErr;
    case -5023:
        return permErr;
    case -5026:
        return tmfoErr;
    case -5029:
        return dirNFErr;
    case -5031:
        return wPrErr;
    case -5034:
        return fnfErr;
    default:
        return ioErr;
    }
}

static OSErr copy_catalog(const romaw_afp_entry* entry, CInfoPBPtr pb)
{
    char mac_name[256];
    int length = ROMlib_host_name_to_mac(entry->name, strlen(entry->name), mac_name);
    if (length <= 0 || length > 255)
        return bdNamErr;
    StringPtr name = PPR(pb->hFileInfo.ioNamePtr);
    if (name) {
        name[0] = length;
        memcpy(name + 1, mac_name, length);
    }

    memset(&pb->hFileInfo.ioFlFndrInfo, 0, sizeof(*pb) - offsetof(HFileInfo, ioFlFndrInfo));
    pb->hFileInfo.ioFlAttrib = entry->directory ? ATTRIB_ISADIR : 0;
    pb->hFileInfo.ioACUser = 0;
    if (entry->directory) {
        memcpy(&pb->dirInfo.ioDrUsrWds, entry->finder_info, 16);
        memcpy(&pb->dirInfo.ioDrFndrInfo, entry->finder_info + 16, 16);
        pb->dirInfo.ioDrDirID = CL(entry->node_id);
        pb->dirInfo.ioDrParID = CL(entry->parent_id);
        pb->dirInfo.ioDrNmFls = CW(entry->children);
    }
    else {
        memcpy(&pb->hFileInfo.ioFlFndrInfo, entry->finder_info, 16);
        memcpy(&pb->hFileInfo.ioFlXFndrInfo, entry->finder_info + 16, 16);
        pb->hFileInfo.ioDirID = CL(entry->node_id);
        pb->hFileInfo.ioFlParID = CL(entry->parent_id);
        pb->hFileInfo.ioFlLgLen = CL(MIN(entry->data_length, INT32_MAX));
        pb->hFileInfo.ioFlRLgLen = CL(MIN(entry->resource_length, INT32_MAX));
    }
    /* AFP uses January 2000 and INT32_MIN denotes an unset date. */
    pb->hFileInfo.ioFlCrDat = entry->created == 0x80000000 ? 0 : CL(entry->created + 3029529600U);
    pb->hFileInfo.ioFlMdDat = entry->modified == 0x80000000 ? 0 : CL(entry->modified + 3029529600U);
    pb->hFileInfo.ioFlBkDat = entry->backup == 0x80000000 ? 0 : CL(entry->backup + 3029529600U);
    return noErr;
}

OSErr ROMlib_afp_catalog_index(romaw_afp_session* session, uint16 volume, uint32 directory, CInfoPBPtr pb)
{
    INTEGER index = CW(pb->hFileInfo.ioFDirIndex);
    if (index <= 0 || directory < 2)
        return paramErr;
    romaw_afp_entry entry;
    int32 result = romaw_afp_enumerate(session, volume, directory, index, &entry);
    return result ? ROMlib_afp_file_error(result) : copy_catalog(&entry, pb);
}

static int encode_path(StringPtr name, char path[1024], uint32* directory)
{
    if (!name || !name[0])
        return 0;
    const char* first = (const char*)name + 1;
    const char* end = first + name[0];
    const char* colon = memchr(first, ':', end - first);
    if (colon == first)
        ++first;
    else if (colon) {
        first = colon + 1;
        *directory = 2;
    }
    int used = 0;
    while (first < end) {
        colon = memchr(first, ':', end - first);
        const char* stop = colon ? colon : end;
        if (stop != first) {
            int size = ROMlib_mac_name_to_utf8(first, stop - first, path + used, 1024 - used);
            if (size < 0)
                return -1;
            used += size;
        }
        if (!colon || colon + 1 == end)
            break;
        if (used == 1024)
            return -1;
        path[used++] = 0;
        first = colon + 1;
    }
    return used;
}

enum { afp_mount_limit = 8 };

static struct {
    VCBExtra* volume;
    romaw_afp_session* session;
    uint16 remote_id;
    uint32 address;
    uint16 port;
    char name[256];
} afp_mounts[afp_mount_limit];

static int mount_index(const VCB* volume)
{
    if (volume)
        for (int i = 0; i < afp_mount_limit; ++i)
            if (afp_mounts[i].volume && &afp_mounts[i].volume->vcb == volume)
                return i;
    return -1;
}

BOOLEAN ROMlib_afp_is_volume(const VCB* volume)
{
    return mount_index(volume) >= 0;
}

OSErr ROMlib_afp_register_volume(uint32 address, uint16 port, const char* name, INTEGER* reference)
{
    char mac_name[256];
    size_t bytes = strlen(name);
    if (!bytes || bytes > 255)
        return bdNamErr;
    int length = ROMlib_host_name_to_mac(name, bytes, mac_name);
    if (length <= 0 || length >= sizeof(((VCB*)0)->vcbVN) || memchr(mac_name, ':', length))
        return bdNamErr;

    int slot = -1;
    for (int i = 0; i < afp_mount_limit; ++i) {
        VCBExtra* mounted = afp_mounts[i].volume;
        if (!mounted) {
            if (slot < 0)
                slot = i;
            continue;
        }
        if (address == afp_mounts[i].address && port == afp_mounts[i].port && mounted->vcb.vcbVN[0] == length &&
            !memcmp(mounted->vcb.vcbVN + 1, mac_name, length)) {
            *reference = CW(mounted->vcb.vcbVRefNum);
            return noErr;
        }
    }
    if (slot < 0)
        return memFullErr;

    /* Classic volume references distinguish volumes with the same label.
       A local disk named Macintosh must not prevent mounting a remote one. */
    int32 error;
    romaw_afp_session* session = romaw_afp_connect(address, port, &error);
    if (!session)
        return ROMlib_afp_file_error(error);
    uint16 remote_id;
    error = romaw_afp_open_volume(session, name, &remote_id);
    if (error) {
        romaw_afp_close(session);
        return ROMlib_afp_file_error(error);
    }
    VCBExtra* mounted = (VCBExtra*)NewPtrSysClear(sizeof *mounted);
    if (!mounted) {
        romaw_afp_close_volume(session, remote_id);
        romaw_afp_close(session);
        return memFullErr;
    }

    mounted->vcb.vcbVN[0] = length;
    memcpy(mounted->vcb.vcbVN + 1, mac_name, length);
    mounted->vcb.vcbVRefNum = CW(--ROMlib_nextvrn);
    mounted->vcb.vcbSigWord = CWC(0x4244);
    mounted->vcb.vcbAtrb = CWC(VHARDLOCKBIT);
    mounted->vcb.vcbAlBlkSiz = CLC(512);
    afp_mounts[slot].volume = mounted;
    afp_mounts[slot].session = session;
    afp_mounts[slot].remote_id = remote_id;
    afp_mounts[slot].address = address;
    afp_mounts[slot].port = port;
    memcpy(afp_mounts[slot].name, name, bytes + 1);
    Enqueue((QElemPtr)mounted, &VCBQHdr);
    *reference = CW(mounted->vcb.vcbVRefNum);
    return noErr;
}

OSErr ROMlib_afp_mounted_catalog(VCB* volume, uint32 directory, CInfoPBPtr pb)
{
    int slot = mount_index(volume);
    if (slot < 0)
        return nsvErr;
    INTEGER index = CW(pb->hFileInfo.ioFDirIndex);
    if (index > 0)
        return ROMlib_afp_catalog_index(afp_mounts[slot].session, afp_mounts[slot].remote_id, directory, pb);
    char path[1024];
    int length = index < 0 ? 0 : encode_path(PPR(pb->hFileInfo.ioNamePtr), path, &directory);
    if (length < 0)
        return bdNamErr;
    if (directory == 1 && length == volume->vcbVN[0] && !memcmp(path, volume->vcbVN + 1, length)) {
        directory = 2;
        length = 0;
    }
    romaw_afp_entry entry;
    int32 result =
        romaw_afp_lookup(afp_mounts[slot].session, afp_mounts[slot].remote_id, directory, path, length, &entry);
    return result ? ROMlib_afp_file_error(result) : copy_catalog(&entry, pb);
}

static struct {
    filecontrolblock* fcb;
    unsigned mount;
    uint16 remote_ref;
} afp_forks[NFCB];

static int fork_slot(INTEGER reference)
{
    filecontrolblock* fcb = ROMlib_refnumtofcbp(reference);
    if (fcb)
        for (int i = 0; i < NFCB; ++i)
            if (afp_forks[i].fcb == fcb)
                return i;
    return -1;
}

BOOLEAN ROMlib_afp_is_fork(INTEGER reference)
{
    return fork_slot(reference) >= 0;
}

OSErr ROMlib_afp_open(VCB* volume, uint32 directory, HParmBlkPtr pb, BOOLEAN resource)
{
    int mount = mount_index(volume);
    if (mount < 0)
        return nsvErr;
    pb->ioParam.ioRefNum = 0;
    if (pb->ioParam.ioPermssn != fsCurPerm && pb->ioParam.ioPermssn != fsRdPerm)
        return wPrErr;
    StringPtr name = PPR(pb->ioParam.ioNamePtr);
    if (!name || !name[0])
        return bdNamErr;
    if (pb->fileParam.ioDirID)
        directory = CL(pb->fileParam.ioDirID);

    int slot = 0;
    while (slot < NFCB && afp_forks[slot].fcb)
        ++slot;
    filecontrolblock* fcb = slot < NFCB ? ROMlib_getfreefcbp() : NULL;
    if (!fcb)
        return tmfoErr;

    char path[1024];
    int path_length = encode_path(name, path, &directory);
    if (path_length <= 0)
        return bdNamErr;
    romaw_afp_session* session = afp_mounts[mount].session;
    uint16 remote_volume = afp_mounts[mount].remote_id;
    romaw_afp_entry entry;
    int32 lookup = romaw_afp_lookup(session, remote_volume, directory, path, path_length, &entry);
    if (lookup)
        return ROMlib_afp_file_error(lookup);
    directory = entry.parent_id;
    if (entry.directory)
        return fnfErr;
    uint16 fork;
    uint32 length;
    int32 result = romaw_afp_open_read_fork(session, remote_volume, directory, entry.name, resource, &fork, &length);
    if (result)
        return ROMlib_afp_file_error(result);
    if (length > INT32_MAX) {
        romaw_afp_close_fork(session, fork);
        return posErr;
    }
    memset(fcb, 0, CW(FSFCBLen));
    fcb->fcbFlNum = CL(entry.node_id);
    fcb->fcbMdRByt = resource ? RESOURCEBIT : 0;
    fcb->fcbEOF = fcb->fcbPLen = CL(length);
    FIELD_ASSIGN_RAW(fcb->fcbVPtr, RM(volume));
    fcb->fcbDirID = CL(directory);
    unsigned display_length = MIN(name[0], sizeof fcb->fcbCName - 1);
    fcb->fcbCName[0] = display_length;
    memcpy(fcb->fcbCName + 1, name + 1, display_length);
    afp_forks[slot].fcb = fcb;
    afp_forks[slot].mount = mount;
    afp_forks[slot].remote_ref = fork;
    pb->ioParam.ioRefNum = CW((char*)fcb - (char*)MR(FCBSPtr));
    return noErr;
}

static int64 fork_position(const ioParam* pb, const filecontrolblock* fcb)
{
    switch (CW(pb->ioPosMode) & 3) {
    case fsAtMark:
        return CL(fcb->fcbCrPs);
    case fsFromStart:
        return (int32)CL(pb->ioPosOffset);
    case fsFromLEOF:
        return (int64)CL(fcb->fcbEOF) + (int32)CL(pb->ioPosOffset);
    case fsFromMark:
        return (int64)CL(fcb->fcbCrPs) + (int32)CL(pb->ioPosOffset);
    }
    return -1;
}

OSErr ROMlib_afp_read(ioParam* pb)
{
    int slot = fork_slot(CW(pb->ioRefNum));
    pb->ioActCount = 0;
    if (slot < 0)
        return rfNumErr;
    filecontrolblock* fcb = afp_forks[slot].fcb;
    int64 position = fork_position(pb, fcb);
    int32 count = CL(pb->ioReqCount);
    if (count < 0 || position < 0 || position > INT32_MAX)
        return posErr;
    if (!count)
        return noErr;
    uint32 size = CL(fcb->fcbEOF);
    if (position >= size)
        return eofErr;
    uint32 read_count = MIN((uint32)count, size - position);
    unsigned char* buffer = ROMlib_mactcp_memory(CL((uint32)PP_RAW(pb->ioBuffer)), read_count, TRUE);
    if (!buffer)
        return paramErr;
    uint32 done = 0;
    int32 result = 0;
    while (done < read_count && !result) {
        unsigned chunk = MIN(read_count - done, 65536), received = 0;
        result = romaw_afp_read_fork(afp_mounts[afp_forks[slot].mount].session, afp_forks[slot].remote_ref,
                                     position + done, buffer + done, chunk, &received);
        done += received;
        if (!result && received != chunk)
            result = -5009;
    }
    pb->ioActCount = CL(done);
    pb->ioPosOffset = fcb->fcbCrPs = CL(position + done);
    return result ? ROMlib_afp_file_error(result) : done < (uint32)count ? eofErr : noErr;
}

OSErr ROMlib_afp_close_fork(INTEGER reference)
{
    int slot = fork_slot(reference);
    if (slot < 0)
        return rfNumErr;
    int32 result = romaw_afp_close_fork(afp_mounts[afp_forks[slot].mount].session, afp_forks[slot].remote_ref);
    afp_forks[slot].fcb->fcbFlNum = 0;
    memset(&afp_forks[slot], 0, sizeof afp_forks[slot]);
    return ROMlib_afp_file_error(result);
}

OSErr ROMlib_afp_set_position(ioParam* pb)
{
    int slot = fork_slot(CW(pb->ioRefNum));
    if (slot < 0)
        return rfNumErr;
    filecontrolblock* fcb = afp_forks[slot].fcb;
    int64 position = fork_position(pb, fcb);
    if (position < 0)
        return posErr;
    if (position > CL(fcb->fcbEOF))
        return eofErr;
    pb->ioPosOffset = fcb->fcbCrPs = CL(position);
    return noErr;
}

OSErr ROMlib_afp_open_wd(VCB* volume, uint32 directory, WDPBPtr pb)
{
    int mount = mount_index(volume);
    if (mount < 0)
        return nsvErr;
    if (pb->ioWDDirID)
        directory = CL(pb->ioWDDirID);
    char path[1024];
    int length = encode_path(PPR(pb->ioNamePtr), path, &directory);
    if (length < 0)
        return bdNamErr;
    romaw_afp_entry entry;
    int32 result =
        romaw_afp_lookup(afp_mounts[mount].session, afp_mounts[mount].remote_id, directory, path, length, &entry);
    if (result)
        return ROMlib_afp_file_error(result);
    if (!entry.directory)
        return dirNFErr;
    return ROMlib_mkwd(pb, (HVCB*)volume, entry.node_id, CL(pb->ioWDProcID));
}

static int profile_mount_file(char path[4096]);
static void mount_key(unsigned slot, char key[280]);

static OSErr unmount(VCB* volume, BOOLEAN publish)
{
    int mount = mount_index(volume);
    if (mount < 0)
        return nsvErr;
    for (unsigned i = 0; i < NFCB; ++i)
        if (afp_forks[i].fcb && afp_forks[i].mount == mount)
            return fBsyErr;

    if (publish) {
        char path[4096], expected[280], remembered[280];
        mount_key(mount, expected);
        if (profile_mount_file(path) && config_prefs_get(path, "AFP", "Mount", remembered, sizeof remembered) &&
            !strcmp(expected, remembered) && !config_prefs_set(path, "AFP", "Mount", NULL))
            return ioErr;
    }

    /* Remove local visibility even when the remote side has disconnected. */
    int32 result = romaw_afp_close_volume(afp_mounts[mount].session, afp_mounts[mount].remote_id);
    romaw_afp_close(afp_mounts[mount].session);
    for (wdentry *wd = (wdentry*)(MR(WDCBsPtr) + sizeof(INTEGER)),
                 *end = (wdentry*)(MR(WDCBsPtr) + CW(*(INTEGER*)MR(WDCBsPtr)));
         wd != end; ++wd)
        if ((VCB*)PPR(wd->vcbp) == volume)
            FIELD_ASSIGN_RAW(wd->vcbp, 0);
    Dequeue((QElemPtr)volume, &VCBQHdr);
    if (MR(DefVCBPtr) == volume) {
        VCB* first = (VCB*)PPR(VCBQHdr.qHead);
        HIDDEN_RAW_SET(DefVCBPtr_H, RM(first));
        DefVRefNum = first ? first->vcbVRefNum : 0;
        DefDirID = CLC(2);
    }
    memset(&afp_mounts[mount], 0, sizeof afp_mounts[mount]);
    DisposPtr((Ptr)volume);
    return ROMlib_afp_file_error(result);
}

OSErr ROMlib_afp_unmount(VCB* volume)
{
    return unmount(volume, TRUE);
}

static uint16 mount_word(const unsigned char* data)
{
    return (uint16)data[0] << 8 | data[1];
}

static const unsigned char* mount_string(const unsigned char* record, unsigned size, unsigned field)
{
    unsigned offset = mount_word(record + field);
    if (!offset || offset >= size || record[offset] > size - offset - 1)
        return NULL;
    return record + offset;
}

OSErr ROMlib_afp_volume_mount(ParmBlkPtr pb, BOOLEAN async)
{
    /* AFPXVolMountInfo uses even 68K alignment, including its OSType. */
    uint32 pointer = CL((uint32)PP_RAW(pb->ioParam.ioBuffer));
    const unsigned char* record = ROMlib_mactcp_memory(pointer, 30, FALSE);
    OSErr error = paramErr;
    if (!record || memcmp(record + 2, "afpm", 4) || !(mount_word(record + 6) & 0x80))
        goto done;
    unsigned size = mount_word(record);
    if (size < 30 || !(record = ROMlib_mactcp_memory(pointer, size, FALSE)))
        goto done;
    const unsigned char* name = mount_string(record, size, 16);
    const unsigned char* user = mount_string(record, size, 18);
    const unsigned char* password = mount_string(record, size, 20);
    const unsigned char* volume_password = mount_string(record, size, 22);
    const unsigned char* method = mount_string(record, size, 26);
    if (!name || !name[0])
        goto done;
    if ((user && user[0]) || (password && password[0]) || (volume_password && volume_password[0]) ||
        (method && (method[0] != 14 || memcmp(method + 1, "No User Authent", 14)))) {
        error = permErr;
        goto done;
    }
    if (!(mount_word(record + 24) & 1))
        goto done;
    unsigned offset = mount_word(record + 28);
    if (offset < 30 || offset > size - 2 || record[offset])
        goto done;
    unsigned count = record[offset + 1];
    offset += 2;
    char volume[1024];
    int bytes = ROMlib_mac_name_to_utf8((const char*)name + 1, name[0], volume, sizeof volume - 1);
    if (bytes <= 0)
        goto done;
    volume[bytes] = 0;
    for (unsigned index = 0; index < count; ++index) {
        if (size - offset < 2)
            break;
        const unsigned char* tag = record + offset;
        unsigned length = tag[0];
        if (length < 2 || length > size - offset)
            break;
        offset += length;
        if (!((tag[1] == 1 && length == 6) || (tag[1] == 2 && length == 8)))
            continue;
        uint32 address = (uint32)mount_word(tag + 2) << 16 | mount_word(tag + 4);
        uint16 port = length == 8 ? mount_word(tag + 6) : 548;
        if (!address || !port)
            continue;
        INTEGER reference;
        error = ROMlib_afp_register_volume(address, port, volume, &reference);
        if (!error) {
            pb->ioParam.ioVRefNum = CW(reference);
            break;
        }
    }
done:
    FAKEASYNC(pb, async, error);
}

static int profile_mount_file(char path[4096])
{
    return romaw_profile_path("AFP Mount.ini", path, 4096, NULL, 0);
}

static void mount_key(unsigned slot, char key[280])
{
    snprintf(key, 280, "%08lx:%u:%s", (unsigned long)afp_mounts[slot].address, afp_mounts[slot].port,
             afp_mounts[slot].name);
}

OSErr ROMlib_afp_publish_mount(INTEGER reference)
{
    int slot = mount_index((VCB*)ROMlib_vcbbyvrn(reference));
    if (slot < 0)
        return nsvErr;
    char path[4096], key[280];
    mount_key(slot, key);
    if (!profile_mount_file(path) || !config_prefs_set(path, "AFP", "Mount", key))
        return ioErr;
    return noErr;
}

void ROMlib_afp_sync_mount(void)
{
    static uint32 poll_at;
    static int polled;
    static INTEGER shared_reference;
    static char shared_key[280];
    uint32 now = TickCount();
    if (polled && (int32)(now - poll_at) < 0)
        return;
    polled = 1;
    poll_at = now + 120;
    char path[4096], key[280] = {0};
    if (!profile_mount_file(path))
        return;
    config_prefs_get(path, "AFP", "Mount", key, sizeof key);
    if (shared_reference) {
        VCB* current = (VCB*)ROMlib_vcbbyvrn(shared_reference);
        if (ROMlib_afp_is_volume(current)) {
            if (!strcmp(key, shared_key))
                return;
            if (unmount(current, FALSE) == fBsyErr)
                return;
        }
        shared_reference = 0;
    }
    if (!key[0])
        return;
    unsigned long address;
    unsigned port;
    int offset = 0;
    if (sscanf(key, "%8lx:%5u:%n", &address, &port, &offset) != 2 || offset < 11 || !address || !port || port > 65535 ||
        !key[offset] || strlen(key + offset) > 255)
        return;
    INTEGER reference;
    if (ROMlib_afp_register_volume(address, port, key + offset, &reference)) {
        poll_at = now + 600;
        return;
    }
    shared_reference = reference;
    strcpy(shared_key, key);
}

BOOLEAN ROMlib_afp_volume_source(const VCB* volume, char* source, size_t capacity)
{
    int slot = mount_index(volume);
    if (slot < 0)
        return FALSE;
    uint32 ip = afp_mounts[slot].address;
    snprintf(source, capacity, "afp://%u.%u.%u.%u:%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255,
             afp_mounts[slot].port);
    return TRUE;
}

int ROMlib_finder_mount_afp(uint32_t address, uint16_t port, const char* name, int* publish_error)
{
    INTEGER volume;
    OSErr error = ROMlib_afp_register_volume(address, port, name, &volume);
    *publish_error = error ? noErr : ROMlib_afp_publish_mount(volume);
    if (!error) {
        char path[4096], endpoint[64];
        snprintf(endpoint, sizeof endpoint, "%u.%u.%u.%u:%u", address >> 24, (address >> 16) & 255,
                 (address >> 8) & 255, address & 255, port);
        if (profile_mount_file(path))
            config_prefs_set(path, "AFP", "Last Server", endpoint);
    }
    return error;
}
