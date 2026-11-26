/*+===================================================================
  File:      diskimages.c

  Summary:   Mount source-backed images and expose catalog, Desktop metadata and resource icons to Finder.

  Classes:   None.

  Functions: ROMlib_image_fork_io, ROMlib_image_error, ROMlib_image_forget_fd, ROMlib_image_io, ROMlib_image_sync,
             ROMlib_image_volume_kind, ROMlib_mount_modern_image, ROMlib_finder_image_volume,
             ROMlib_finder_image_desktop, ROMlib_finder_image_application, ROMlib_finder_image_find_application,
             ROMlib_finder_image_mount, ROMlib_finder_image_entry, ROMlib_finder_image_unmount,
             ROMlib_finder_image_path, ROMlib_finder_image_icon, ROMlib_finder_image_volume_icon,
             ROMlib_finder_image_writable, ROMlib_finder_image_lookup, ROMlib_finder_image_create,
             ROMlib_finder_image_set_info, ROMlib_finder_image_open_fork, ROMlib_finder_image_write,
             ROMlib_finder_image_open_source_fork, ROMlib_finder_image_read, ROMlib_finder_image_close_fork,
             ROMlib_finder_image_delete, ROMlib_finder_image_rename, ROMlib_finder_image_check_mutable,
             ROMlib_finder_image_move,
             ROMlib_finder_image_flush, ROMlib_finder_image_position, ROMlib_finder_image_window,
             ROMlib_finder_image_view, ROMlib_finder_image_info, ROMlib_finder_image_lock,
             ROMlib_finder_image_version, ROMlib_finder_image_preview, ROMlib_finder_host_memory,
             ROMlib_finder_image_memory, ROMlib_finder_image_set_comment, ROMlib_finder_image_set_custom_icon,
             ROMlib_finder_image_custom_icon

  Origin:    Existing repository source in executor/src/host; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "rsys/common.h"
#include "FileMgr.h"
#include "OSEvent.h"
#include "rsys/hfs.h"
#include "rsys/file.h"
#include "rsys/fileicon.h"
#include "rsys/notmac.h"
#include "rsys/hostdialog.h"
#include "romaw_images.h"
#include "finder/image_volumes.h"
#include "rsys/desktop_database.h"
#include "rsys/ecf.h"
#include "rsys/memsize.h"
#include "ResourceMgr.h"
#include "MemoryMgr.h"
#include "../finder-metadata/include/finder_metadata/icons.h"
#if defined(SDL)
#include "rsys/prefpanel.h"
#endif
typedef struct ImageMount {
    struct ImageMount* next;
    char* source;
    romaw_image_set* set;
    OSErr result;
    int* fds;
    unsigned count;
} ImageMount;

static ImageMount* mounted;
static char last_error[1024];

int ROMlib_image_io(int fd, void* buffer, LONGINT count, LONGINT offset, int writing, OSErr* error)
{
    for (ImageMount* p = mounted; p; p = p->next)
        for (unsigned i = 0; i < p->count; ++i)
            if (p->fds[i] == fd) {
                *error = writing                                                                      ? wPrErr
                         : count < 0 || offset < 0                                                    ? ioErr
                         : romaw_images_device_read(p->set, i, (uint32)offset, buffer, (size_t)count) ? noErr
                                                                                                      : ioErr;
                return 1;
            }
    return 0;
}

int ROMlib_image_fork_io(int fd, uint32 id, int resource, uint32 offset, void* bytes, LONGINT count, int writing,
                         OSErr* error)
{
    for (ImageMount* p = mounted; p; p = p->next)
        for (unsigned i = 0; i < p->count; ++i)
            if (p->fds[i] == fd) {
                int result =
                    count < 0 ? -1 : romaw_images_device_fork_read(p->set, i, id, resource, offset, bytes, count);
                if (!result)
                    return 0;
                *error = writing ? wPrErr : result < 0 ? ioErr : noErr;
                return 1;
            }
    return 0;
}

void ROMlib_image_forget_fd(int fd)
{
    for (ImageMount** link = &mounted; *link; link = &(*link)->next) {
        ImageMount* p = *link;
        for (unsigned i = 0; i < p->count; ++i)
            if (p->fds[i] == fd) {
                p->fds[i] = -1;
                p->result = nsvErr;
                unsigned j;
                for (j = 0; j < p->count && p->fds[j] < 0; ++j)
                    ;
                if (j == p->count) {
                    *link = p->next;
                    romaw_images_close(p->set, 1);
                    free(p->fds);
                    free(p->source);
                    free(p);
                }
                return;
            }
    }
}

OSErr ROMlib_image_sync(int fd, uint32 offset)
{
    if (offset != 84)
        return noErr;
    off_t position = lseek(fd, 0, SEEK_CUR);
    int copy = dup(fd);
    if (copy < 0)
        return ioErr;
    FILE* file = fdopen(copy, "r+b");
    if (!file) {
        close(copy);
        return ioErr;
    }
    int result = romaw_images_dc42_sync(file);
    if (fclose(file))
        result = 0;
    if (position < 0 || lseek(fd, position, SEEK_SET) < 0)
        result = 0;
    return result ? noErr : ioErr;
}

int ROMlib_image_volume_kind(const HVCB* volume)
{
    if (volume->vcbCTRef) {
        int fd = ((const VCBExtra*)volume)->u.hfs.fd;
        for (ImageMount* p = mounted; p; p = p->next)
            for (unsigned i = 0; i < p->count; ++i)
                if (p->fds[i] == fd) {
                    const char* suffix = strrchr(p->source, '.');
                    return suffix && !strcasecmp(suffix, ".dmg") ? 7 : 6;
                }
        DrvQExtra* drive = ROMlib_dqbydrive(CW(volume->vcbDrvNum));
        return drive && CW(drive->dq.dQRefNum) == OURHFSDREF && ((uint32)CL(drive->flags) & 0x00080000U) != 0 ? 6 : 0;
    }
    const char* path = ((const VCBExtra*)volume)->unixname;
    if (path)
        for (ImageMount* p = mounted; p; p = p->next)
            for (unsigned i = 0; i < romaw_images_count(p->set); ++i) {
                const char* root = romaw_images_path(p->set, i);
                size_t n = strlen(root);
                size_t j = 0;
                while (j < n && path[j] && (path[j] == root[j] || (path[j] == '/' && root[j] == '\\')))
                    ++j;
                if (j == n && !path[j]) {
                    const char* suffix = strrchr(p->source, '.');
                    return suffix && !strcasecmp(suffix, ".dmg") ? 7 : 6;
                }
            }
    return 0;
}

const char* ROMlib_image_error(void)
{
    return last_error[0] ? last_error : "Choose a supported HFS/HFS+ image or an unencrypted DMG.";
}

int ROMlib_finder_image_volume(uint32_t index, FinderImageVolume* result)
{
    if (!result)
        return 0;
    memset(result, 0, sizeof *result);
    for (HVCB* volume = (HVCB*)PPR(VCBQHdr.qHead); volume; volume = (HVCB*)PPR(volume->qLink)) {
        if (!ROMlib_image_volume_kind(volume))
            continue;
        DrvQExtra* drive = ROMlib_dqbydrive(CW(volume->vcbDrvNum));
        if (!drive || !drive->devicename)
            continue;
        if (index--)
            continue;
        snprintf(result->source, sizeof result->source, "%s", (char*)drive->devicename);
        unsigned length = MIN(volume->vcbVN[0], sizeof result->name - 1);
        memcpy(result->name, volume->vcbVN + 1, length);
        result->reference = CW(volume->vcbVRefNum);
        result->partition = CW(drive->partition);
        result->read_only = (CW(volume->vcbAtrb) & 0x8080) != 0;
        result->total_bytes = (uint64_t)(uint16_t)CW(volume->vcbNmAlBlks) * (uint32_t)CL(volume->vcbAlBlkSiz);
        result->free_bytes = (uint64_t)(uint16_t)CW(volume->vcbFreeBks) * (uint32_t)CL(volume->vcbAlBlkSiz);
        return 1;
    }
    return 0;
}

const char* ROMlib_finder_image_mount(uint32_t index, int* mode)
{
    return ROMlib_ecf_mounted_operand(index, mode);
}

static void image_catalog_result(const CInfoPBRec* catalog, const unsigned char* name, FinderImageEntry* result)
{
    memset(result, 0, sizeof *result);
    memcpy(result->name, name + 1, MIN(name[0], sizeof result->name - 1));
    result->id = CL(catalog->hFileInfo.ioDirID);
    result->directory = (catalog->hFileInfo.ioFlAttrib & 0x10) != 0;
    if (result->directory) {
        memcpy(result->finder_info, &catalog->dirInfo.ioDrUsrWds, 16);
        memcpy(result->finder_info + 16, &catalog->dirInfo.ioDrFndrInfo, 16);
        result->parent = CL(catalog->dirInfo.ioDrParID);
        result->flags = CW(catalog->dirInfo.ioDrUsrWds.frFlags);
        result->created = CL(catalog->dirInfo.ioDrCrDat);
        result->modified = CL(catalog->dirInfo.ioDrMdDat);
        result->children = CW(catalog->dirInfo.ioDrNmFls);
    }
    else {
        memcpy(result->finder_info, &catalog->hFileInfo.ioFlFndrInfo, 16);
        memcpy(result->finder_info + 16, &catalog->hFileInfo.ioFlXFndrInfo, 16);
        result->parent = CL(catalog->hFileInfo.ioFlParID);
        result->flags = CW(catalog->hFileInfo.ioFlFndrInfo.fdFlags);
        result->type = CL(catalog->hFileInfo.ioFlFndrInfo.fdType);
        result->creator = CL(catalog->hFileInfo.ioFlFndrInfo.fdCreator);
        result->created = CL(catalog->hFileInfo.ioFlCrDat);
        result->modified = CL(catalog->hFileInfo.ioFlMdDat);
        result->locked = (catalog->hFileInfo.ioFlAttrib & 1) != 0;
        result->data_size = CL(catalog->hFileInfo.ioFlLgLen);
        result->resource_size = CL(catalog->hFileInfo.ioFlRLgLen);
        result->data_allocated = CL(catalog->hFileInfo.ioFlPyLen);
        result->resource_allocated = CL(catalog->hFileInfo.ioFlRPyLen);
    }
}

int16_t ROMlib_finder_image_entry(int16_t reference, uint32_t directory, int16_t index, FinderImageEntry* result)
{
    HVCB* volume = ROMlib_findvcb(reference, NULL, NULL, FALSE);
    if (!volume || !ROMlib_image_volume_kind(volume))
        return nsvErr;
    if (!result || (!index || index < -1))
        return paramErr;
    memset(result, 0, sizeof *result);
    CInfoPBRec catalog = {0};
    Str255 name = {0};
    catalog.hFileInfo.ioVRefNum = CW(reference);
    catalog.hFileInfo.ioDirID = CL(directory);
    catalog.hFileInfo.ioFDirIndex = CW(index);
    FIELD_ASSIGN_RAW(catalog.hFileInfo.ioNamePtr, RM((StringPtr)name));
    OSErr error = PBGetCatInfo(&catalog, FALSE);
    if (error)
        return error;
    image_catalog_result(&catalog, name, result);
    return noErr;
}

int16_t ROMlib_finder_image_unmount(int16_t reference)
{
    HVCB* volume = ROMlib_findvcb(reference, NULL, NULL, FALSE);
    if (!volume || !ROMlib_image_volume_kind(volume))
        return nsvErr;
    return UnmountVol(NULL, reference);
}

int16_t ROMlib_finder_image_path(int16_t reference, uint32_t parent, const char* name, char** result)
{
    if (!result)
        return paramErr;
    *result = NULL;
    if (!name || strlen(name) > 31)
        return bdNamErr;
    HVCB* volume = ROMlib_findvcb(reference, NULL, NULL, FALSE);
    if (!volume || !ROMlib_image_volume_kind(volume))
        return nsvErr;
    FSSpec file = {0};
    file.vRefNum = CW(reference);
    file.parID = CL(parent);
    file.name[0] = strlen(name);
    memcpy(file.name + 1, name, file.name[0]);
    return ROMlib_launch_path_from_fsspec(result, &file);
}

int16_t ROMlib_finder_image_writable(int16_t reference)
{
    HVCB* volume = ROMlib_findvcb(reference, NULL, NULL, FALSE);
    if (!volume || !ROMlib_image_volume_kind(volume))
        return nsvErr;
    return (CW(volume->vcbAtrb) & 0x8080) ? wPrErr : noErr;
}

static OSErr image_name(const char* name, Str255 out)
{
    if (!name || !*name || strlen(name) > 31 || strchr(name, ':'))
        return bdNamErr;
    out[0] = strlen(name);
    memcpy(out + 1, name, out[0]);
    return noErr;
}

static OSErr image_catalog(int16_t reference, uint32_t parent, const char* name, CInfoPBRec* pb, Str255 storage)
{
    OSErr error = image_name(name, storage);
    if (error)
        return error;
    memset(pb, 0, sizeof *pb);
    PP_ASSIGN(pb->hFileInfo.ioNamePtr, (StringPtr)storage);
    pb->hFileInfo.ioVRefNum = CW(reference);
    pb->hFileInfo.ioDirID = CL(parent);
    return PBGetCatInfo(pb, FALSE);
}

int16_t ROMlib_finder_image_info(int16_t reference, uint32_t parent, const char* name, FinderImageEntry* result)
{
    HVCB* volume = ROMlib_findvcb(reference, NULL, NULL, FALSE);
    if (!volume || !ROMlib_image_volume_kind(volume))
        return nsvErr;
    if (!result)
        return paramErr;
    CInfoPBRec pb;
    Str255 storage;
    OSErr error = image_catalog(reference, parent, name, &pb, storage);
    if (!error)
        image_catalog_result(&pb, storage, result);
    return error;
}

int16_t ROMlib_finder_image_lock(int16_t reference, uint32_t parent, const char* name, uint32_t id, int locked)
{
    OSErr error = ROMlib_finder_image_writable(reference);
    if (error)
        return error;
    CInfoPBRec catalog;
    Str255 storage;
    error = image_catalog(reference, parent, name, &catalog, storage);
    if (error)
        return error;
    if ((catalog.hFileInfo.ioFlAttrib & 0x10) || CL(catalog.hFileInfo.ioDirID) != id)
        return fnfErr;
    HParamBlockRec pb = {0};
    PP_ASSIGN(pb.ioParam.ioNamePtr, (StringPtr)storage);
    pb.ioParam.ioVRefNum = CW(reference);
    pb.fileParam.ioDirID = CL(parent);
    return locked ? PBHSetFLock(&pb, FALSE) : PBHRstFLock(&pb, FALSE);
}

int ROMlib_finder_image_version(int16_t reference, uint32_t parent, const char* name, void* bytes, size_t capacity,
                                size_t* size)
{
    if (!bytes || !size || capacity > 4096)
        return 0;
    *size = 0;
    Str255 filename;
    if (image_name(name, filename))
        return 0;
    INTEGER saved_map = CurMap, saved_error = ResErr;
    BOOLEAN saved_load = ResLoad;
    SetResLoad(FALSE);
    INTEGER file = HOpenResFile(reference, parent, filename, fsRdPerm);
    if (file != -1) {
        Handle resource = Get1Resource(TICK("vers"), 1);
        if (resource) {
            Size length = SizeResource(resource);
            if (length >= 0 && (size_t)length <= capacity) {
                LoadResource(resource);
                if (STARH(resource) && ResError() == noErr) {
                    memcpy(bytes, STARH(resource), length);
                    *size = length;
                }
            }
            ReleaseResource(resource);
        }
        CloseResFile(file);
    }
    CurMap = saved_map;
    ResLoad = saved_load;
    ResErr = saved_error;
    return *size != 0;
}

/* 'kind' resources use big-endian codes and word-aligned Pascal strings.
   ROMAW's Finder UI uses the US English region. */
static void document_kind_resource(const unsigned char* bytes, size_t size, uint32_t type, uint32_t creator,
                                   char kind[256])
{
    unsigned char signature[] = {creator >> 24, creator >> 16, creator >> 8, creator};
    unsigned char file_type[] = {type >> 24, type >> 16, type >> 8, type};
    if (size < 10 || memcmp(bytes, signature, sizeof signature) || bytes[4] || bytes[5])
        return;
    unsigned count = (unsigned)bytes[8] * 256 + bytes[9];
    size_t offset = 10;
    for (unsigned i = 0; i < count && size - offset >= 5; ++i) {
        unsigned length = bytes[offset + 4];
        if (length > size - offset - 5)
            return;
        if (length && !memcmp(bytes + offset, file_type, sizeof file_type)) {
            memcpy(kind, bytes + offset + 5, length);
            kind[length] = 0;
            return;
        }
        offset += (length + 6) & ~1u;
        if (offset > size)
            return;
    }
}

/* Resolve the volume's APPL records without opening or creating Desktop DB. */
static int host_kind_application(char* directory, uint32_t creator, FSSpec* app)
{
    HVCB* volume = ROMlib_vcbbybiggestunixname(directory);
    if (!volume) {
        ROMlib_automount(directory);
        volume = ROMlib_vcbbybiggestunixname(directory);
    }
    if (!volume || volume->vcbCTRef)
        return 0;
    for (unsigned index = 1; index <= 65536; ++index) {
        unsigned char key[38] = {5, 2, creator >> 24, creator >> 16, creator >> 8, creator};
        unsigned char record[40];
        size_t size = sizeof record;
        uint32_t tag = 0;
        if (romaw_desktop_database(((VCBExtra*)volume)->unixname, desktop_index, key, index, record, &size, &tag))
            break;
        if (size != sizeof record || !record[8] || record[8] > 31)
            continue;
        app->vRefNum = volume->vcbVRefNum;
        memcpy(&app->parID, record + 4, 4);
        memcpy(app->name, record + 8, record[8] + 1);
        CInfoPBRec info = {0};
        info.hFileInfo.ioVRefNum = app->vRefNum;
        info.hFileInfo.ioDirID = app->parID;
        FIELD_ASSIGN_RAW(info.hFileInfo.ioNamePtr, RM((StringPtr)app->name));
        if (!PBGetCatInfo(&info, FALSE) && !(info.hFileInfo.ioFlAttrib & 0x10) &&
            CL(info.hFileInfo.ioFlFndrInfo.fdCreator) == creator &&
            (CL(info.hFileInfo.ioFlFndrInfo.fdType) == TICK("APPL") ||
             CL(info.hFileInfo.ioFlFndrInfo.fdType) == TICK("FNDR")))
            return 1;
    }
    return 0;
}

void ROMlib_finder_document_kind(char* host_directory, int16_t reference, uint32_t parent, const char* application,
                                 uint32_t type, uint32_t creator, char owner[256], char kind[256])
{
    owner[0] = kind[0] = 0;
    FSSpec app = {0};
    Str255 name;
    if (host_directory) {
        if (!host_kind_application(host_directory, creator, &app))
            return;
        memcpy(name, app.name, app.name[0] + 1);
        reference = CW(app.vRefNum);
        parent = CL(app.parID);
    }
    else if (!application || image_name(application, name))
        return;
    memcpy(owner, name + 1, name[0]);
    owner[name[0]] = 0;

    INTEGER saved_map = CurMap, saved_error = ResErr;
    BOOLEAN saved_load = ResLoad;
    SetResLoad(FALSE);
    INTEGER file = HOpenResFile(reference, parent, name, fsRdPerm);
    if (file != -1) {
        int count = Count1Resources(TICK("kind"));
        for (int i = 1; i <= count && !kind[0]; ++i) {
            Handle resource = Get1IndResource(TICK("kind"), i);
            if (!resource)
                continue;
            Size size = SizeResource(resource);
            if (size >= 10 && size <= 65536) {
                LoadResource(resource);
                if (ResError() == noErr && STARH(resource) && GetHandleSize(resource) >= size)
                    document_kind_resource(STARH(resource), size, type, creator, kind);
            }
            ReleaseResource(resource);
        }
        CloseResFile(file);
    }
    CurMap = saved_map;
    ResLoad = saved_load;
    ResErr = saved_error;
}

/* Read only the fixed SIZE header. Older forks may include extra bytes. */
static OSErr memory_header(Handle resource, size_info_t* header)
{
    if (!resource)
        return resNotFound;
    if (SizeResource(resource) < 10)
        return mapReadErr;
    LoadResource(resource);
    OSErr error = ResError();
    if (error || !STARH(resource) || GetHandleSize(resource) < 10)
        return error ? error : mapReadErr;
    memcpy(header, STARH(resource), 10);
    return noErr;
}

static OSErr application_memory(const FSSpec* app, FinderApplicationMemory* memory,
                                const FinderApplicationMemory* update)
{
    if (!memory || (update && (!update->preferred || update->minimum > update->preferred ||
                               update->preferred > MAX_APPLZONE_SIZE)))
        return paramErr;
    INTEGER saved_map = CurMap, saved_error = ResErr;
    BOOLEAN saved_load = ResLoad;
    SetResLoad(FALSE);
    INTEGER file = HOpenResFile(CW(app->vRefNum), CL(app->parID), (StringPtr)app->name, update ? fsRdWrPerm : fsRdPerm);
    OSErr error = ResError();
    if (file != -1) {
        Handle factory = Get1Resource(TICK("SIZE"), -1);
        Handle override = Get1Resource(TICK("SIZE"), 0);
        size_info_t source = {0}, original = {0};
        FinderApplicationMemory current = {0};
        if (!memory_header(factory, &original)) {
            current.suggested = CL(original.preferred_size);
            current.original_minimum = CL(original.minimum_size);
        }
        Handle effective = override ? override : factory;
        error = memory_header(effective, &source);
        if (!error) {
            current.preferred = CL(source.preferred_size);
            current.minimum = CL(source.minimum_size);
            current.editable = !override || !(GetResAttrs(override) & (resProtected | resCompressed));
            if (update) {
                if (current.minimum != memory->minimum || current.preferred != memory->preferred)
                    error = fBsyErr;
                else if (!current.editable)
                    error = resAttrErr;
                else {
                    source.preferred_size = CL(update->preferred);
                    source.minimum_size = CL(update->minimum);
                    if (override) {
                        ChangedResource(override);
                        error = ResError();
                        if (!error)
                            memcpy(STARH(override) + 2, (const char*)&source + 2, 8);
                    }
                    else {
                        Handle added = NewHandle(10);
                        if (!added)
                            error = memFullErr;
                        else {
                            memcpy(STARH(added), &source, 10);
                            Str255 empty = {0};
                            AddResource(added, TICK("SIZE"), 0, empty);
                            error = ResError();
                            if (error)
                                DisposHandle(added);
                        }
                    }
                    if (!error) {
                        UpdateResFile(file);
                        error = ResError();
                    }
                    if (!error) {
                        current.minimum = update->minimum;
                        current.preferred = update->preferred;
                    }
                }
            }
        }
        CloseResFile(file);
        if (!error)
            error = ResError();
        if (!error)
            *memory = current;
    }
    CurMap = saved_map;
    ResLoad = saved_load;
    ResErr = saved_error;
    return error;
}

int16_t ROMlib_finder_host_memory(char* path, FinderApplicationMemory* memory, const FinderApplicationMemory* update)
{
    if (!path)
        return paramErr;
    FSSpec app;
    OSErr error = ROMlib_host_fsspec(path, &app);
    return error ? error : application_memory(&app, memory, update);
}

int16_t ROMlib_finder_image_memory(int16_t reference, uint32_t parent, const char* name, uint32_t id,
                                   FinderApplicationMemory* memory, const FinderApplicationMemory* update)
{
    FinderImageEntry entry;
    OSErr error = ROMlib_finder_image_info(reference, parent, name, &entry);
    if (error || entry.directory || entry.id != id)
        return error ? error : fnfErr;
    if (update) {
        error = ROMlib_finder_image_check_mutable(reference, parent, name, id);
        if (error)
            return error;
    }
    FSSpec app = {0};
    app.vRefNum = CW(reference);
    app.parID = CL(parent);
    app.name[0] = strlen(name);
    memcpy(app.name + 1, name, app.name[0]);
    return application_memory(&app, memory, update);
}

int16_t ROMlib_finder_image_position(int16_t reference, uint32_t parent, const char* name, int32_t x, int32_t y)
{
    OSErr error = ROMlib_finder_image_writable(reference);
    if (error)
        return error;
    if (x <= INT16_MIN || x > INT16_MAX || y <= INT16_MIN || y > INT16_MAX)
        return paramErr;
    CInfoPBRec pb;
    Str255 storage;
    error = image_catalog(reference, parent, name, &pb, storage);
    if (error)
        return error;
    Point* location;
    uint16_t* flags;
    if (pb.hFileInfo.ioFlAttrib & 0x10) {
        location = &pb.dirInfo.ioDrUsrWds.frLocation;
        flags = &pb.dirInfo.ioDrUsrWds.frFlags;
    }
    else {
        location = &pb.hFileInfo.ioFlFndrInfo.fdLocation;
        flags = &pb.hFileInfo.ioFlFndrInfo.fdFlags;
    }
    if (CW(location->h) == x && CW(location->v) == y && (CW(*flags) & 0x100))
        return noErr;
    location->h = CW(x);
    location->v = CW(y);
    *flags = CW(CW(*flags) | 0x100);
    pb.hFileInfo.ioDirID = CL(parent);
    return PBSetCatInfo(&pb, FALSE);
}

int16_t ROMlib_finder_image_view(int16_t reference, uint32_t directory, uint16_t view)
{
    OSErr error = ROMlib_finder_image_writable(reference);
    if (error)
        return error;
    if (view != 0x100 && view != 0x140 && view != 0x200 && view != 0x300 && view != 0x400 && view != 0x500 &&
        view != 0x600 && view != 0x700 && view != 0x800)
        return paramErr;
    CInfoPBRec pb = {0};
    pb.dirInfo.ioVRefNum = CW(reference);
    pb.dirInfo.ioDrDirID = CL(directory);
    pb.dirInfo.ioFDirIndex = CWC(-1);
    error = PBGetCatInfo(&pb, FALSE);
    if (error)
        return error;
    /* Retain unrelated low-byte Finder state, changing only the selector and icon size. */
    uint16_t previous = CW(pb.dirInfo.ioDrUsrWds.frView);
    uint16_t next = (previous & 0xbf) | view;
    if (previous == next)
        return noErr;
    pb.dirInfo.ioDrUsrWds.frView = CW(next);
    return PBSetCatInfo(&pb, FALSE);
}

int16_t ROMlib_finder_image_window(int16_t reference, uint32_t directory, int32_t left, int32_t top, int32_t right,
                                   int32_t bottom)
{
    OSErr error = ROMlib_finder_image_writable(reference);
    if (error)
        return error;
    if (left < INT16_MIN || left > INT16_MAX || top < INT16_MIN || top > INT16_MAX || right < INT16_MIN ||
        right > INT16_MAX || bottom < INT16_MIN || bottom > INT16_MAX || right - left < 100 || bottom - top < 100)
        return paramErr;
    CInfoPBRec pb = {0};
    pb.dirInfo.ioVRefNum = CW(reference);
    pb.dirInfo.ioDrDirID = CL(directory);
    pb.dirInfo.ioFDirIndex = CWC(-1);
    error = PBGetCatInfo(&pb, FALSE);
    if (error)
        return error;
    Rect rectangle = {CW(top), CW(left), CW(bottom), CW(right)};
    if (!memcmp(&pb.dirInfo.ioDrUsrWds.frRect, &rectangle, sizeof rectangle))
        return noErr;
    pb.dirInfo.ioDrUsrWds.frRect = rectangle;
    return PBSetCatInfo(&pb, FALSE);
}

int16_t ROMlib_finder_image_lookup(int16_t reference, uint32_t parent, const char* name, uint32_t* id)
{
    CInfoPBRec pb;
    Str255 storage;
    OSErr error = image_catalog(reference, parent, name, &pb, storage);
    if (!error && id)
        *id = CL(pb.hFileInfo.ioDirID);
    return error;
}

int16_t ROMlib_finder_image_set_info(int16_t reference, uint32_t parent, const char* name, const unsigned char info[32])
{
    OSErr error = ROMlib_finder_image_writable(reference);
    if (error || !info)
        return error ? error : paramErr;
    CInfoPBRec pb;
    Str255 storage;
    error = image_catalog(reference, parent, name, &pb, storage);
    if (error)
        return error;
    if (pb.hFileInfo.ioFlAttrib & 0x10) {
        memcpy(&pb.dirInfo.ioDrUsrWds, info, 16);
        memcpy(&pb.dirInfo.ioDrFndrInfo, info + 16, 16);
        pb.dirInfo.ioDrDirID = CL(parent);
    }
    else {
        memcpy(&pb.hFileInfo.ioFlFndrInfo, info, 16);
        memcpy(&pb.hFileInfo.ioFlXFndrInfo, info + 16, 16);
        pb.hFileInfo.ioDirID = CL(parent);
    }
    return PBSetCatInfo(&pb, FALSE);
}

int16_t ROMlib_finder_image_delete(int16_t reference, uint32_t parent, const char* name)
{
    OSErr error = ROMlib_finder_image_writable(reference);
    HParamBlockRec pb = {0};
    Str255 storage;
    if (!error)
        error = image_name(name, storage);
    if (error)
        return error;
    PP_ASSIGN(pb.ioParam.ioNamePtr, (StringPtr)storage);
    pb.ioParam.ioVRefNum = CW(reference);
    pb.fileParam.ioDirID = CL(parent);
    return PBHDelete(&pb, FALSE);
}

int16_t ROMlib_finder_image_create(int16_t reference, uint32_t parent, const char* name, int directory,
                                   const unsigned char info[32], uint32_t* id)
{
    OSErr error = ROMlib_finder_image_writable(reference);
    HParamBlockRec pb = {0};
    Str255 storage;
    if (!error)
        error = image_name(name, storage);
    if (error || !id || !info)
        return error ? error : paramErr;
    PP_ASSIGN(pb.ioParam.ioNamePtr, (StringPtr)storage);
    pb.ioParam.ioVRefNum = CW(reference);
    pb.fileParam.ioDirID = CL(parent);
    error = directory ? PBDirCreate(&pb, FALSE) : PBHCreate(&pb, FALSE);
    if (error)
        return error;
    error = ROMlib_finder_image_set_info(reference, parent, name, info);
    if (!error)
        error = ROMlib_finder_image_lookup(reference, parent, name, id);
    if (error)
        ROMlib_finder_image_delete(reference, parent, name);
    return error;
}

static OSErr image_open_fork(int16_t reference, uint32_t parent, const char* name, int resource, int permission,
                             int16_t* file)
{
    HParamBlockRec pb = {0};
    Str255 storage;
    OSErr error = image_name(name, storage);
    if (error || !file)
        return error ? error : paramErr;
    PP_ASSIGN(pb.ioParam.ioNamePtr, (StringPtr)storage);
    pb.ioParam.ioVRefNum = CW(reference);
    pb.ioParam.ioPermssn = permission;
    pb.fileParam.ioDirID = CL(parent);
    error = resource ? PBHOpenRF(&pb, FALSE) : PBOpenDF(&pb, FALSE);
    if (!error)
        *file = CW(pb.ioParam.ioRefNum);
    return error;
}

int16_t ROMlib_finder_image_open_fork(int16_t reference, uint32_t parent, const char* name, int resource, int16_t* file)
{
    OSErr error = ROMlib_finder_image_writable(reference);
    return error ? error : image_open_fork(reference, parent, name, resource, fsRdWrPerm, file);
}

int16_t ROMlib_finder_image_open_source_fork(int16_t reference, uint32_t parent, const char* name, int resource,
                                             int16_t* file)
{
    HVCB* volume = ROMlib_findvcb(reference, NULL, NULL, FALSE);
    if (!volume || !ROMlib_image_volume_kind(volume))
        return nsvErr;
    return image_open_fork(reference, parent, name, resource, fsRdPerm, file);
}

int16_t ROMlib_finder_image_read(int16_t file, void* bytes, uint32_t count)
{
    if ((!bytes && count) || count > 65536)
        return paramErr;
    Ptr buffer = NewPtrSys(count ? count : 1);
    if (!buffer)
        return memFullErr;
    LONGINT length = count;
    OSErr error = FSRead(file, &length, buffer);
    if (!error && length == count)
        memcpy(bytes, buffer, count);
    DisposPtr(buffer);
    return error ? error : length == count ? noErr : eofErr;
}

int16_t ROMlib_finder_image_write(int16_t file, const void* bytes, uint32_t count)
{
    if ((!bytes && count) || count > 65536)
        return paramErr;
    Ptr buffer = NewPtrSys(count ? count : 1);
    if (!buffer)
        return memFullErr;
    if (count)
        memcpy(buffer, bytes, count);
    LONGINT length = count;
    OSErr error = FSWrite(file, &length, buffer);
    DisposPtr(buffer);
    return error ? error : length == count ? noErr : ioErr;
}

int16_t ROMlib_finder_image_close_fork(int16_t file)
{
    return FSClose(file);
}

int16_t ROMlib_finder_image_rename(int16_t reference, uint32_t parent, const char* name, const char* target)
{
    OSErr error = ROMlib_finder_image_writable(reference);
    HParamBlockRec pb = {0};
    Str255 storage, renamed;
    if (!error)
        error = image_name(name, storage);
    if (!error)
        error = image_name(target, renamed);
    if (error)
        return error;
    /* ioMisc is a guest address, unlike the mapped ioNamePtr field. */
    Ptr target_name = NewPtrSys(renamed[0] + 1);
    if (!target_name)
        return memFullErr;
    memcpy(target_name, renamed, renamed[0] + 1);
    PP_ASSIGN(pb.ioParam.ioNamePtr, (StringPtr)storage);
    pb.ioParam.ioMisc = RM((uintptr_t)target_name);
    pb.ioParam.ioVRefNum = CW(reference);
    pb.fileParam.ioDirID = CL(parent);
    error = PBHRename(&pb, FALSE);
    DisposPtr(target_name);
    return error;
}

int16_t ROMlib_finder_image_flush(int16_t reference)
{
    ParamBlockRec pb = {0};
    pb.volumeParam.ioVRefNum = CW(reference);
    return PBFlushVol(&pb, FALSE);
}

int16_t ROMlib_finder_image_check_mutable(int16_t reference, uint32_t parent, const char* name, uint32_t id)
{
    OSErr error = ROMlib_finder_image_writable(reference);
    if (error)
        return error;
    FinderImageEntry entry;
    error = ROMlib_finder_image_info(reference, parent, name, &entry);
    if (error || id <= 2 || id != entry.id || parent != entry.parent)
        return error ? error : fnfErr;
    if (entry.locked)
        return fLckdErr;
    HVCB* volume = ROMlib_findvcb(reference, NULL, NULL, FALSE);
    if (entry.directory)
        return ROMlib_dirbusy(id, volume);
    return ROMlib_alreadyopen(volume, id, NULL, 0, eitherbusy) ? fBsyErr : noErr;
}

int16_t ROMlib_finder_image_move(int16_t reference, uint32_t parent, const char* name, uint32_t id,
                                 uint32_t target_parent)
{
    OSErr error = ROMlib_finder_image_check_mutable(reference, parent, name, id);
    if (error || parent == target_parent)
        return error;
    Str255 storage;
    error = image_name(name, storage);
    if (error)
        return error;
    CMovePBRec pb = {0};
    PP_ASSIGN(pb.ioNamePtr, (StringPtr)storage);
    pb.ioVRefNum = CW(reference);
    pb.ioDirID = CL(parent);
    pb.ioNewDirID = CL(target_parent);
    return PBCatMove(&pb, FALSE);
}

typedef struct ImageDesktopFiles {
    INTEGER files[2];
    Ptr buffer;
    size_t capacity;
    int16_t reference;
} ImageDesktopFiles;

static int image_desktop_read(void* opaque, unsigned file, uint64_t offset, void* data, size_t size)
{
    ImageDesktopFiles* source = opaque;
    if (file > 1 || source->files[file] == -1 || offset > INT32_MAX || size > source->capacity)
        return 0;
    LONGINT count = size;
    if (SetFPos(source->files[file], fsFromStart, offset) != noErr ||
        FSRead(source->files[file], &count, source->buffer) != noErr || count != size)
        return 0;
    memcpy(data, source->buffer, size);
    return 1;
}

int ROMlib_finder_image_desktop(int16_t reference, int operation, unsigned char key[38], unsigned index, void* data,
                                size_t* size, uint32_t* tag)
{
    HVCB* volume = ROMlib_findvcb(reference, NULL, NULL, FALSE);
    if (!volume || !ROMlib_image_volume_kind(volume))
        return nsvErr;
    ImageDesktopFiles source = {{-1, -1}, NULL, 32768, reference};
    LONGINT lengths[2] = {0, 0};
    Str255 name = "\012Desktop DB";
    for (unsigned i = 0; i < 2; ++i) {
        HParamBlockRec pb = {0};
        name[10] = i ? 'F' : 'B';
        PP_ASSIGN(pb.ioParam.ioNamePtr, (StringPtr)name);
        pb.ioParam.ioVRefNum = CW(reference);
        pb.ioParam.ioPermssn = fsRdPerm;
        pb.fileParam.ioDirID = CLC(2);
        OSErr error = PBHOpen(&pb, FALSE);
        if (error && !i)
            return error;
        if (!error)
            source.files[i] = CW(pb.ioParam.ioRefNum);
    }
    for (unsigned i = 0; i < 2; ++i)
        if (source.files[i] != -1 && (GetEOF(source.files[i], &lengths[i]) != noErr || lengths[i] < 0))
            lengths[i] = 0;
    source.buffer = NewPtrSys(32768);
    int result = source.buffer ? romaw_desktop_database_read(image_desktop_read, &source, lengths[0], lengths[1],
                                                             operation, key, index, data, size, tag)
                               : memFullErr;
    if (source.buffer)
        DisposPtr(source.buffer);
    for (unsigned i = 0; i < 2; ++i)
        if (source.files[i] != -1)
            FSClose(source.files[i]);
    return result;
}

static OSErr image_desktop_write(ImageDesktopFiles* source, const void* data, size_t size)
{
    INTEGER file = source->files[0];
    OSErr error = SetFPos(file, fsFromStart, 0);
    for (size_t offset = 0; !error && offset < size;) {
        LONGINT count = MIN(size - offset, source->capacity);
        LONGINT written = count;
        memcpy(source->buffer, (const char*)data + offset, count);
        error = FSWrite(file, &written, source->buffer);
        if (!error && written != count)
            error = ioErr;
        offset += count;
    }
    if (!error)
        error = SetEOF(file, size);
    if (!error)
        error = ROMlib_finder_image_flush(source->reference);
    return error;
}

/* Hold the File Manager's writable fork while validating and publishing the
 * edit. A failed write restores the original bytes before releasing it. */
int16_t ROMlib_finder_image_replace_fork(int16_t reference, uint32_t parent, const char* name, uint32_t id,
                                       int resource, const void* before, uint32_t before_size,
                                       const void* after, uint32_t after_size)
{
    OSErr error = ROMlib_finder_image_check_mutable(reference, parent, name, id);
    ImageDesktopFiles fork = {0};
    fork.reference = reference;
    fork.capacity = 8192;
    fork.files[0] = -1;
    if (!error)
        error = ROMlib_finder_image_open_fork(reference, parent, name, resource, &fork.files[0]);
    if (error)
        return error;
    LONGINT length = 0;
    error = GetEOF(fork.files[0], &length);
    if (!error && (uint32_t)length != before_size)
        error = fBsyErr;
    unsigned char check[8192];
    fork.buffer = NewPtrSys(fork.capacity);
    if (!error && !fork.buffer)
        error = memFullErr;
    for (uint32_t offset = 0; !error && offset < before_size;) {
        uint32_t count = MIN(before_size - offset, sizeof check);
        if (!image_desktop_read(&fork, 0, offset, check, count) ||
            memcmp(check, (const unsigned char*)before + offset, count))
            error = fBsyErr;
        offset += count;
    }
    if (!error) {
        error = image_desktop_write(&fork, after, after_size);
        if (error && image_desktop_write(&fork, before, before_size))
            error = ioErr;
    }
    if (fork.buffer)
        DisposPtr(fork.buffer);
    OSErr close_error = FSClose(fork.files[0]);
    return error ? error : close_error;
}

static OSErr create_image_desktop_file(int16_t reference, const char* name, const char* type)
{
    unsigned char info[32] = {0};
    memcpy(info, type, 4);
    memcpy(info + 4, "DMGR", 4);
    info[8] = fInvisible >> 8;
    uint32_t id;
    return ROMlib_finder_image_create(reference, 2, name, 0, info, &id);
}

static int image_desktop_publish(void* opaque, const void* tree, size_t size, const void* previous,
                                 size_t previous_size)
{
    ImageDesktopFiles* source = opaque;
    int16_t reference = source->reference;
    if (source->files[0] != -1) {
        OSErr error = image_desktop_write(source, tree, size);
        if (error && image_desktop_write(source, previous, previous_size))
            return ioErr;
        return error;
    }

    // Create the classic pair only when the first comment needs a database.
    OSErr error = create_image_desktop_file(reference, "Desktop DF", "DTFL");
    BOOLEAN created_df = !error;
    if (error && error != dupFNErr)
        return error;
    error = create_image_desktop_file(reference, "Desktop DB", "BTFL");
    BOOLEAN created_db = !error;
    if (!error)
        error = ROMlib_finder_image_open_fork(reference, 2, "Desktop DB", 0, &source->files[0]);
    if (!error)
        error = image_desktop_write(source, tree, size);
    if (error) {
        if (source->files[0] != -1) {
            FSClose(source->files[0]);
            source->files[0] = -1;
        }
        if (created_db)
            ROMlib_finder_image_delete(reference, 2, "Desktop DB");
        if (created_df)
            ROMlib_finder_image_delete(reference, 2, "Desktop DF");
    }
    return error;
}

int16_t ROMlib_finder_image_set_comment(int16_t reference, uint32_t parent, const char* name, uint32_t id,
                                        const void* macroman, size_t size)
{
    OSErr error = ROMlib_finder_image_writable(reference);
    if (error)
        return error;
    FinderImageEntry entry;
    error = ROMlib_finder_image_info(reference, parent, name, &entry);
    if (error || id <= 2 || id != entry.id || parent != entry.parent)
        return error ? error : fnfErr;
    if (entry.locked)
        return fLckdErr;

    ImageDesktopFiles source = {{-1, -1}, NULL, 32768, reference};
    error = ROMlib_finder_image_open_fork(reference, 2, "Desktop DB", 0, &source.files[0]);
    if (error && error != fnfErr)
        return error;
    LONGINT length = 0;
    error = source.files[0] == -1 ? noErr : GetEOF(source.files[0], &length);
    if (!error) {
        source.buffer = NewPtrSys(source.capacity);
        error = source.buffer ? romaw_desktop_database_comment(image_desktop_read, image_desktop_publish, &source,
                                                               length, id, macroman, size)
                              : memFullErr;
        if (source.buffer)
            DisposPtr(source.buffer);
    }
    if (source.files[0] != -1) {
        OSErr close_error = FSClose(source.files[0]);
        if (!error)
            error = close_error;
    }
    return error;
}

int16_t ROMlib_finder_image_set_custom_icon(int16_t reference, uint32_t parent, const char* name, uint32_t id,
                                            const FinderIcon* icon)
{
    OSErr error = ROMlib_finder_image_check_mutable(reference, parent, name, id);
    FinderImageEntry item;
    if (!error)
        error = ROMlib_finder_image_info(reference, parent, name, &item);
    if (error)
        return error;
    const char* carrier = item.directory ? "Icon\r" : name;
    uint32_t directory = item.directory ? id : parent;
    BOOLEAN created = FALSE;
    if (item.directory) {
        FinderImageEntry existing;
        error = ROMlib_finder_image_info(reference, directory, carrier, &existing);
        if (error == fnfErr && icon) {
            unsigned char metadata[32] = "iconMACS";
            uint32_t icon_id;
            metadata[8] = fInvisible >> 8;
            error = ROMlib_finder_image_create(reference, directory, carrier, 0, metadata, &icon_id);
            created = !error;
        }
        else if (!error)
            error = ROMlib_finder_image_check_mutable(reference, directory, carrier, existing.id);
        else if (error != fnfErr)
            return error;
    }
    unsigned char updated[32];
    memcpy(updated, item.finder_info, sizeof updated);
    updated[8] = (updated[8] & ~4u) | (icon ? 4 : 0);
    if (error == fnfErr && !icon)
        return ROMlib_finder_image_set_info(reference, parent, name, updated);
    if (error)
        return error;

    ImageDesktopFiles file = {{-1, -1}, NULL, 32768, reference};
    unsigned char* original = NULL;
    unsigned char* replacement = NULL;
    LONGINT length = 0;
    size_t replacement_size = 0;
    error = ROMlib_finder_image_open_fork(reference, directory, carrier, 1, &file.files[0]);
    if (!error)
        error = GetEOF(file.files[0], &length);
    if (!error && (length < 0 || length > 17 * 1024 * 1024))
        error = mapReadErr;
    if (!error) {
        file.buffer = NewPtrSys(file.capacity);
        original = length ? malloc(length) : NULL;
        if (!file.buffer || (length && !original))
            error = memFullErr;
    }
    for (size_t offset = 0; !error && offset < (size_t)length;) {
        size_t count = MIN(file.capacity, (size_t)length - offset);
        if (!image_desktop_read(&file, 0, offset, original + offset, count))
            error = ioErr;
        offset += count;
    }
    if (!error && !finder_edit_custom_icon(original, length, icon, &replacement, &replacement_size))
        error = mapReadErr;
    if (!error) {
        error = image_desktop_write(&file, replacement, replacement_size);
        if (!error)
            error = ROMlib_finder_image_set_info(reference, parent, name, updated);
        if (error && !created && image_desktop_write(&file, original, length))
            error = ioErr;
    }
    if (file.files[0] != -1) {
        OSErr close_error = FSClose(file.files[0]);
        if (!error)
            error = close_error;
    }
    if (error && created)
        ROMlib_finder_image_delete(reference, directory, carrier);
    if (!error)
        error = ROMlib_finder_image_flush(reference);
    if (file.buffer)
        DisposPtr(file.buffer);
    free(original);
    free(replacement);
    return error;
}

int ROMlib_finder_image_application(int16_t reference, uint32_t creator, FinderImageEntry* result)
{
    if (!result)
        return 0;
    unsigned char key[38] = {5, 2, creator >> 24, creator >> 16, creator >> 8, creator};
    unsigned char record[40];
    uint32_t tag = 0;
    for (unsigned index = 1; index <= 65536; ++index) {
        size_t size = sizeof record;
        if (ROMlib_finder_image_desktop(reference, desktop_index, key, index, record, &size, &tag))
            break;
        if (size == 40 && record[8] && record[8] <= 31) {
            char name[32] = {0};
            memcpy(name, record + 9, record[8]);
            uint32_t parent =
                (uint32_t)record[4] << 24 | (uint32_t)record[5] << 16 | (uint32_t)record[6] << 8 | record[7];
            for (int i = 1; i <= 32767; ++i) {
                FinderImageEntry entry;
                if (ROMlib_finder_image_entry(reference, parent, i, &entry))
                    break;
                if (!strcmp(entry.name, name) && entry.creator == creator &&
                    (entry.type == TICK("APPL") || entry.type == TICK("FNDR"))) {
                    *result = entry;
                    return 1;
                }
            }
        }
        key[0] = 5; // Continue indexing all application records for this creator.
    }
    return 0;
}

static int image_find_application(int16_t reference, uint32_t directory, uint32_t creator, unsigned depth,
                                  unsigned* remaining, FinderImageEntry* result)
{
    if (!depth || !*remaining)
        return 0;
    for (int i = 1; i <= 32767 && *remaining; ++i) {
        FinderImageEntry entry;
        --*remaining;
        if (ROMlib_finder_image_entry(reference, directory, i, &entry))
            break;
        if (!entry.directory && entry.creator == creator &&
            (entry.type == TICK("APPL") || entry.type == TICK("FNDR"))) {
            *result = entry;
            return 1;
        }
        if (entry.directory && image_find_application(reference, entry.id, creator, depth - 1, remaining, result))
            return 1;
    }
    return 0;
}

int ROMlib_finder_image_find_application(int16_t reference, uint32_t creator, FinderImageEntry* result)
{
    if (!result || !creator)
        return 0;
    if (ROMlib_finder_image_application(reference, creator, result))
        return 1;
    // Old Desktop files can be absent or stale. Discover only catalog identity;
    // application code and resources remain on the source image until opened.
    unsigned remaining = 100000;
    return image_find_application(reference, 2, creator, 64, &remaining, result);
}

int ROMlib_finder_image_preview(int16_t reference, uint32_t parent, const char* name, unsigned char* rgba)
{
    size_t length = name ? strlen(name) : 0;
    if (!length || length > 31 || !rgba)
        return 0;
    FSSpec spec = {0};
    spec.vRefNum = CW(reference);
    spec.parID = CL(parent);
    spec.name[0] = length;
    memcpy(spec.name + 1, name, length);
    return ROMlib_read_icon_data(&spec, NULL, rgba);
}

static int image_icon_family(int id, FinderIcon* result)
{
    unsigned char* planes[] = {result->mono,   result->small,  result->color4,
                               result->color8, result->small4, result->small8};
    memset(result, 0, sizeof *result);
    for (unsigned i = 0; i < NELEM(planes); ++i) {
        Handle resource = Get1Resource(finder_icon_types[i], id);
        if (resource) {
            Size size = GetHandleSize(resource);
            if (STARH(resource) && size >= 0 && (size_t)size >= finder_icon_sizes[i]) {
                memcpy(planes[i], STARH(resource), finder_icon_sizes[i]);
                result->present |= 1u << i;
            }
            ReleaseResource(resource);
        }
    }
    Handle family = Get1Resource(TICK("icns"), id);
    if (family) {
        FinderIcon embedded;
        Size size = GetHandleSize(family);
        if (STARH(family) && size >= 8 &&
            finder_parse_icns_icon((const unsigned char*)STARH(family), size, &embedded)) {
            const unsigned char* source[] = {embedded.mono,   embedded.small,  embedded.color4,
                                             embedded.color8, embedded.small4, embedded.small8};
            for (unsigned i = 0; i < NELEM(planes); ++i)
                if (!(result->present & (1u << i)) && (embedded.present & (1u << i))) {
                    memcpy(planes[i], source[i], finder_icon_sizes[i]);
                    result->present |= 1u << i;
                }
        }
        ReleaseResource(family);
    }
    return (result->present & 1) != 0;
}

int ROMlib_finder_image_custom_icon(int16_t reference, uint32_t parent, const char* name, int directory,
                                    FinderIcon* result)
{
    Str255 source;
    HVCB* volume = ROMlib_findvcb(reference, NULL, NULL, FALSE);
    if (!result || !volume || !ROMlib_image_volume_kind(volume) || image_name(directory ? "Icon\r" : name, source))
        return 0;
    INTEGER saved_map = CurMap, saved_error = ResErr;
    BOOLEAN saved_load = ResLoad;
    SetResLoad(FALSE);
    INTEGER file = HOpenResFile(reference, parent, source, fsRdPerm);
    SetResLoad(TRUE);
    int found = 0;
    if (file != -1) {
        found = image_icon_family(-16455, result);
        CloseResFile(file);
    }
    CurMap = saved_map;
    ResLoad = saved_load;
    ResErr = saved_error;
    return found;
}

static int image_resource_icon(int directory, uint16_t flags, uint32_t type, uint32_t creator, FinderIcon* result)
{
    int found = 0;
    if (directory || (flags & 0x400))
        found = image_icon_family(-16455, result);
    int bundles = Count1Resources(TICK("BNDL"));
    int references = Count1Resources(TICK("FREF"));
    for (int i = 1; i <= references && !found && !directory; ++i) {
        Handle resource = Get1IndResource(TICK("FREF"), i);
        unsigned local = 0;
        int match = 0;
        if (resource) {
            if (STARH(resource) && GetHandleSize(resource) >= 6) {
                const unsigned char* bytes = (const unsigned char*)STARH(resource);
                uint32_t file_type =
                    (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
                match = file_type == type;
                local = bytes[4] * 256u + bytes[5];
            }
            ReleaseResource(resource);
        }
        if (!match)
            continue;
        for (int j = 1; j <= bundles && !found; ++j) {
            resource = Get1IndResource(TICK("BNDL"), j);
            int id = 0, mapped = 0;
            if (resource) {
                if (STARH(resource))
                    mapped = finder_bundle_icon_id((const unsigned char*)STARH(resource), GetHandleSize(resource),
                                                   creator, local, &id);
                ReleaseResource(resource);
            }
            if (mapped)
                found = image_icon_family(id, result);
        }
    }
    if (!found && !directory && !bundles && Count1Resources(TICK("ICN#")) == 1) {
        Handle resource = Get1IndResource(TICK("ICN#"), 1);
        if (resource) {
            INTEGER id = 0;
            ResType resource_type;
            Str255 resource_name;
            GetResInfo(resource, &id, &resource_type, resource_name);
            ReleaseResource(resource);
            found = image_icon_family(CW(id), result);
        }
    }
    return found;
}

static int image_desktop_icon(int16_t reference, uint32_t type, uint32_t creator, FinderIcon* result)
{
    const unsigned char types[] = {1, 4, 2, 3, 5, 6};
    unsigned char* planes[] = {result->mono,   result->small,  result->color4,
                               result->color8, result->small4, result->small8};
    for (unsigned i = 0; i < NELEM(types); ++i) {
        unsigned char key[38] = {11,         1,          creator >> 24, creator >> 16, creator >> 8, creator,
                                 type >> 24, type >> 16, type >> 8,     type,          types[i]};
        unsigned char bytes[4500];
        size_t size = sizeof bytes;
        uint32_t tag = 0;
        if (!ROMlib_finder_image_desktop(reference, desktop_get, key, 0, bytes, &size, &tag) &&
            size == finder_icon_sizes[i]) {
            memcpy(planes[i], bytes, size);
            result->present |= 1u << i;
        }
    }
    return result->present != 0;
}

int ROMlib_finder_image_icon(int16_t reference, uint32_t parent, const char* name, int directory, uint16_t flags,
                             uint32_t type, uint32_t creator, FinderIcon* result)
{
    if (!result)
        return 0;
    memset(result, 0, sizeof *result);
    HVCB* volume = ROMlib_findvcb(reference, NULL, NULL, FALSE);
    if (!volume || !ROMlib_image_volume_kind(volume) || !name || strlen(name) > 31)
        return 0;
    if (!directory && !(flags & 0x400) && image_desktop_icon(reference, type, creator, result))
        return 1;
    memset(result, 0, sizeof *result);
    Str255 filename = {0};
    const char* source = directory ? "Icon\r" : name;
    filename[0] = strlen(source);
    memcpy(filename + 1, source, filename[0]);
    INTEGER saved_map = CurMap, saved_error = ResErr;
    BOOLEAN saved_load = ResLoad;
    // Suppress preload data: opening reads map metadata, then only the FREF,
    // BNDL and chosen bitmap resources below. No complete fork is retained.
    SetResLoad(FALSE);
    INTEGER file = HOpenResFile(reference, parent, filename, fsRdPerm);
    SetResLoad(TRUE);
    int found = 0;
    if (file != -1) {
        found = image_resource_icon(directory, flags, type, creator, result);
        CloseResFile(file);
    }
    if (!found && !directory && strcmp(name, "Desktop")) {
        SetResLoad(FALSE);
        Str255 desktop = "\007Desktop";
        file = HOpenResFile(reference, 2, desktop, fsRdPerm);
        SetResLoad(TRUE);
        if (file != -1) {
            found = image_resource_icon(0, 0, type, creator, result);
            CloseResFile(file);
        }
    }
    CurMap = saved_map;
    ResLoad = saved_load;
    ResErr = saved_error;
    if (!found)
        memset(result, 0, sizeof *result);
    return found;
}

// Read only ICNS headers and supported bitmap ranges from the data fork.
static int image_volume_data_icon(int16_t reference, FinderIcon* result)
{
    Str255 name = "\020.VolumeIcon.icns";
    HParamBlockRec pb = {0};
    PP_ASSIGN(pb.ioParam.ioNamePtr, (StringPtr)name);
    pb.ioParam.ioVRefNum = CW(reference);
    pb.ioParam.ioPermssn = fsRdPerm;
    pb.fileParam.ioDirID = CLC(2);
    if (PBOpenDF(&pb, FALSE))
        return 0;
    ImageDesktopFiles source = {{CW(pb.ioParam.ioRefNum), -1}, NewPtrSys(1024), 1024};
    LONGINT length = 0;
    unsigned char header[8];
    FinderIcon icon = {0};
    unsigned char* planes[] = {icon.mono, icon.small, icon.color4, icon.color8, icon.small4, icon.small8};
    int valid = source.buffer && GetEOF(source.files[0], &length) == noErr && length >= 8 &&
                image_desktop_read(&source, 0, 0, header, sizeof header);
    uint32_t declared = 0;
    if (valid) {
        declared = (uint32_t)header[4] << 24 | (uint32_t)header[5] << 16 | (uint32_t)header[6] << 8 | header[7];
        valid = !memcmp(header, "icns", 4) && declared == length;
    }
    uint32_t offset = 8;
    for (unsigned chunks = 0; valid && offset < declared; ++chunks) {
        if (chunks >= 4096 || declared - offset < 8 || !image_desktop_read(&source, 0, offset, header, sizeof header)) {
            valid = 0;
            break;
        }
        uint32_t type = (uint32_t)header[0] << 24 | (uint32_t)header[1] << 16 | (uint32_t)header[2] << 8 | header[3];
        uint32_t size = (uint32_t)header[4] << 24 | (uint32_t)header[5] << 16 | (uint32_t)header[6] << 8 | header[7];
        if (size < 8 || size > declared - offset) {
            valid = 0;
            break;
        }
        for (unsigned i = 0; i < NELEM(planes); ++i)
            if (type == finder_icon_types[i]) {
                if (size - 8 != finder_icon_sizes[i] ||
                    !image_desktop_read(&source, 0, offset + 8, planes[i], finder_icon_sizes[i]))
                    valid = 0;
                else
                    icon.present |= 1u << i;
            }
        offset += size;
    }
    if (source.buffer)
        DisposPtr(source.buffer);
    FSClose(source.files[0]);
    if (valid && (icon.present & 1)) {
        *result = icon;
        return 1;
    }
    return 0;
}

int ROMlib_finder_image_volume_icon(int16_t reference, FinderIcon* result)
{
    if (!result)
        return 0;
    memset(result, 0, sizeof *result);
    HVCB* volume = ROMlib_findvcb(reference, NULL, NULL, FALSE);
    if (!volume || !ROMlib_image_volume_kind(volume))
        return 0;
    if (ROMlib_finder_image_icon(reference, 2, "", 1, 0, 0, 0, result))
        return 1;
    if (image_volume_data_icon(reference, result))
        return 1;
    // Earlier Finder versions can keep the volume's custom family in Desktop.
    INTEGER saved_map = CurMap, saved_error = ResErr;
    BOOLEAN saved_load = ResLoad;
    Str255 name = "\007Desktop";
    SetResLoad(FALSE);
    INTEGER file = HOpenResFile(reference, 2, name, fsRdPerm);
    SetResLoad(TRUE);
    int found = 0;
    if (file != -1) {
        found = image_icon_family(-16455, result);
        CloseResFile(file);
    }
    CurMap = saved_map;
    ResLoad = saved_load;
    ResErr = saved_error;
    if (!found)
        memset(result, 0, sizeof *result);
    return found;
}

static void release_images(void)
{
    while (mounted) {
        ImageMount* next = mounted->next;
        for (unsigned i = 0; i < mounted->count; ++i)
            if (mounted->fds[i] >= 0)
                close(mounted->fds[i]);
        free(mounted->fds);
        romaw_images_close(mounted->set, 1);
        free(mounted->source);
        free(mounted);
        mounted = next;
    }
}

OSErr ROMlib_mount_modern_image(const char* path)
{
    for (ImageMount* p = mounted; p; p = p->next)
        if (!strcmp(path, p->source) && p->result == noErr)
            return noErr;
    last_error[0] = 0;
#if defined(SDL)
    romaw_image_set* set =
        romaw_images_open_direct(path, ROMlib_sdl_image_progress_pump, last_error, sizeof last_error);
#else
    romaw_image_set* set = romaw_images_open_direct(path, NULL, last_error, sizeof last_error);
#endif
    if (!set)
        return paramErr;
    /* Direct projected volumes below are always read-only, including incomplete
       sources. Opening their available contents needs no repair/write decision. */
    ImageMount* entry = calloc(1, sizeof *entry);
    char* source = strdup(path);
    unsigned count = romaw_images_count(set);
    int* fds = malloc(count * sizeof *fds);
    if (!entry || !source || !fds) {
        free(entry);
        free(source);
        free(fds);
        romaw_images_close(set, 0);
        return memFullErr;
    }
    for (unsigned i = 0; i < count; ++i)
        fds[i] = -1;
    if (!mounted)
        atexit(release_images);
    entry->result = paramErr;
    entry->next = mounted;
    entry->set = set;
    entry->source = source;
    entry->fds = fds;
    entry->count = count;
    mounted = entry;
    for (unsigned i = 0; i < count; ++i) {
        if (!romaw_images_device(set, i, last_error, sizeof last_error))
            return paramErr;
        int fd = Uopen(path, O_BINARY | O_RDONLY, 0);
        if (fd < 0)
            return ROMlib_maperrno();
        entry->fds[i] = fd;
        hfs_access_t access = {.fd = fd, .offset = 0, .bsize = 512, .maxbytes = 1024 * 1024};
        drive_flags_t flags = DRIVE_FLAGS_FIXED | DRIVE_FLAGS_LOCKED;
        DrvQExtra* drive = ROMlib_addtodq(4096, path, i, OURHFSDREF, flags, &access);
        if (!drive) {
            entry->fds[i] = -1;
            close(fd);
            return memFullErr;
        }
        INTEGER number = CW(drive->dq.dQDrive);
        drive->hfs = access;
        ParamBlockRec pb = {0};
        pb.ioParam.ioVRefNum = CW(number);
        OSErr error = hfsPBMountVol(&pb, fd, 0, access.bsize, access.maxbytes, flags, drive);
        if (error) {
            entry->fds[i] = -1;
            close(fd);
            return error;
        }
        PPostEvent(diskEvt, number, NULL);
    }
    entry->result = noErr;
    return noErr;
}
