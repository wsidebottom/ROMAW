/*+===================================================================
  File:      application_registry.c

  Summary:   Installed Macintosh applications: an in-memory Desktop Manager index.

  Classes:   None.

  Functions: ROMlib_application_registry_idle, ROMlib_application_registry_pending, ROMlib_application_registry_reset,
             ROMlib_application_spec, ROMlib_application_spec_cached

  Origin:    Existing repository source in executor/src/files; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Installed Macintosh applications: an in-memory Desktop Manager index. */
#include "rsys/common.h"
#include "FileMgr.h"
#include "AliasMgr.h"
#include "ResourceMgr.h"
#include "MemoryMgr.h"
#include "rsys/file.h"
#include "rsys/application_registry.h"
#include "rsys/filetypemap.h"
#include "rsys/hfs.h"
#include "rsys/notmac.h"
#include "rsys/ecf.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    FSSpec spec;
    uint32 creator;
} application;

static application* applications;
static unsigned count, capacity;
static int ready;

static struct {
    uint32 type;
    int application_index;
} type_handlers[256];

static unsigned handler_count, next_handler;

static struct {
    INTEGER volume;
    LONGINT directory;
    unsigned index;
} pending_directories[33];

static unsigned pending_depth, pending_budget;
static int indexing_requested, indexing_started;
static uint32 pending_types[32];
static unsigned pending_type_count, pending_type_index, pending_application;

void ROMlib_application_registry_reset(void)
{
    free(applications);
    applications = NULL;
    count = capacity = 0;
    ready = 0;
    handler_count = next_handler = 0;
    pending_depth = pending_budget = 0;
    indexing_requested = indexing_started = 0;
    pending_type_count = pending_type_index = pending_application = 0;
}

static void remember_application(INTEGER volume, LONGINT directory, const Str255 name, uint32 creator)
{
    if (name[0] >= sizeof applications[0].spec.name || count >= 4096)
        return;
    if (count == capacity) {
        unsigned next = capacity ? capacity * 2 : 64;
        application* grown = realloc(applications, next * sizeof *grown);
        if (!grown)
            return;
        applications = grown;
        capacity = next;
    }
    application* app = &applications[count++];
    app->spec.vRefNum = volume;
    app->spec.parID = directory;
    memcpy(app->spec.name, name, name[0] + 1);
    app->creator = creator;
}

static void scan(INTEGER volume, LONGINT directory, unsigned depth, unsigned* budget)
{
    if (depth > 32)
        return;
    for (unsigned index = 1; index < 32768 && *budget && count < 4096; ++index) {
        CInfoPBRec pb = {0};
        Str255 name;
        pb.hFileInfo.ioVRefNum = volume;
        pb.hFileInfo.ioDirID = directory;
        FIELD_ASSIGN_RAW(pb.hFileInfo.ioNamePtr, RM((StringPtr)name));
        pb.hFileInfo.ioFDirIndex = CW(index);
        int saved_counts = ROMlib_catalog_skip_counts;
        ROMlib_catalog_skip_counts = 1;
        OSErr error = PBGetCatInfo(&pb, FALSE);
        ROMlib_catalog_skip_counts = saved_counts;
        if (error)
            return;
        --*budget;
        if (!name[0] || name[1] == '.' || name[1] == '%')
            continue;
        if (pb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR)
            scan(volume, pb.dirInfo.ioDrDirID, depth + 1, budget);
        else if (pb.hFileInfo.ioFlFndrInfo.fdType == TICKX("APPL") && name[0] < sizeof applications[0].spec.name)
            remember_application(volume, directory, name, CL(pb.hFileInfo.ioFlFndrInfo.fdCreator));
    }
}

static void initialize(void)
{
    INTEGER volume;
    LONGINT directory;
    CInfoPBRec pb = {0};
    if (ready)
        return;
    /* An explicit launch can finish discovery synchronously. Do not duplicate
     the partial index accumulated during Finder idle events. */
    count = 0;
    handler_count = next_handler = 0;
    pending_depth = 0;
    ready = 1;
    if (FindFolder(0, TICK("exdr"), FALSE, &volume, &directory))
        return;
    pb.hFileInfo.ioVRefNum = volume;
    pb.hFileInfo.ioDirID = directory;
    FIELD_ASSIGN_RAW(pb.hFileInfo.ioNamePtr, RM((StringPtr) "\004Apps"));
    int saved_counts = ROMlib_catalog_skip_counts;
    ROMlib_catalog_skip_counts = 1;
    OSErr error = PBGetCatInfo(&pb, FALSE);
    ROMlib_catalog_skip_counts = saved_counts;
    if (error || !(pb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR))
        return;
    unsigned budget = 30000;
    scan(volume, pb.dirInfo.ioDrDirID, 0, &budget);
}

static int supports_type(const application* app, uint32 type)
{
    INTEGER saved = CurResFile();
    SignedByte load = ResLoad;
    /* Association queries need the map and FREF entries, not application code,
     sound or pictures. Preserve the caller's loading mode on every path. */
    SetResLoad(FALSE);
    INTEGER file = HOpenResFile(CW(app->spec.vRefNum), CL(app->spec.parID), (StringPtr)app->spec.name, fsRdPerm);
    int found = 0;
    if (file == -1) {
        SetResLoad(load);
        UseResFile(saved);
        return 0;
    }
    SetResLoad(TRUE);
    int n = Count1Resources(TICK("FREF"));
    for (int i = 1; i <= n && i <= 1024 && !found; ++i) {
        Handle ref = Get1IndResource(TICK("FREF"), i);
        if (ref && STARH(ref) && GetHandleSize(ref) >= 7) {
            unsigned char* p = (unsigned char*)STARH(ref);
            uint32 code = (uint32)p[0] << 24 | (uint32)p[1] << 16 | p[2] << 8 | p[3];
            found = code == type;
        }
    }
    CloseResFile(file);
    UseResFile(saved);
    SetResLoad(load);
    return found;
}

static int mapped_application(uint32 type, uint32 creator, FSSpec* spec)
{
    char path[4096];
    if (!filetype_map_application_path(type, creator, path, sizeof path, 1))
        return 0;
    for (char* p = path; *p; ++p)
        if (*p == '\\')
            *p = '/';
    ROMlib_automount(path);
    HVCB* volume = ROMlib_vcbbybiggestunixname(path);
    if (!volume)
        return 0;
    Str255 name;
    size_t used = volume->vcbVN[0];
    memcpy(name + 1, volume->vcbVN + 1, used);
    name[++used] = ':';
    const char* part = path + strlen(((VCBExtra*)volume)->unixname);
    while (*part == '/')
        ++part;
    while (*part) {
        const char* slash = strchr(part, '/');
        size_t length = slash ? (size_t)(slash - part) : strlen(part);
        char component[256];
        if (length > 255)
            return 0;
        memcpy(component, part, length);
        length -= ROMlib_UNIX7_to_Mac(component, length);
        if (used + length + 1 > 255)
            return 0;
        memcpy(name + used + 1, component, length);
        used += length;
        if (!slash)
            break;
        name[++used] = ':';
        part = slash + 1;
    }
    name[0] = used;
    return FSMakeFSSpec(0, 0, name, spec) == noErr;
}

static void persist_application(application* app, uint32 type)
{
    char* path = NULL;
    if (!ROMlib_filename_from_fsspec(&path, &app->spec))
        filetype_map_register_application(path, app->creator, &type, 1, 1);
    free(path);
}

int ROMlib_application_registry_pending(void)
{
    return indexing_requested && (!ready || pending_type_index < pending_type_count);
}

/* At most eight catalog records or one resource map per idle event. Missing
   handlers may improve after discovery, without holding up window drawing. */
int ROMlib_application_registry_idle(void)
{
    if (!indexing_requested)
        return 0;
    if (!ready && !indexing_started) {
        INTEGER volume;
        LONGINT directory;
        CInfoPBRec pb = {0};
        indexing_started = 1;
        if (!FindFolder(0, TICK("exdr"), FALSE, &volume, &directory)) {
            pb.dirInfo.ioVRefNum = volume;
            pb.dirInfo.ioDrDirID = directory;
            FIELD_ASSIGN_RAW(pb.dirInfo.ioNamePtr, RM((StringPtr) "\004Apps"));
            int previous = ROMlib_catalog_skip_counts;
            ROMlib_catalog_skip_counts = 1;
            OSErr error = PBGetCatInfo(&pb, FALSE);
            ROMlib_catalog_skip_counts = previous;
            if (!error && (pb.dirInfo.ioFlAttrib & ATTRIB_ISADIR)) {
                pending_directories[0].volume = volume;
                pending_directories[0].directory = pb.dirInfo.ioDrDirID;
                pending_directories[0].index = 1;
                pending_depth = 1;
                pending_budget = 30000;
            }
        }
    }
    int changed = 0;
    for (unsigned work = 0; !ready && pending_depth && pending_budget && count < 4096 && work < 8; ++work) {
        unsigned slot = pending_depth - 1;
        if (pending_directories[slot].index >= 32768) {
            --pending_depth;
            continue;
        }
        CInfoPBRec pb = {0};
        Str255 name;
        pb.dirInfo.ioVRefNum = pending_directories[slot].volume;
        pb.dirInfo.ioDrDirID = pending_directories[slot].directory;
        FIELD_ASSIGN_RAW(pb.dirInfo.ioNamePtr, RM((StringPtr)name));
        pb.dirInfo.ioFDirIndex = CW(pending_directories[slot].index++);
        int previous = ROMlib_catalog_skip_counts;
        ROMlib_catalog_skip_counts = 1;
        OSErr error = PBGetCatInfo(&pb, FALSE);
        ROMlib_catalog_skip_counts = previous;
        if (error) {
            --pending_depth;
            continue;
        }
        --pending_budget;
        if (!name[0] || name[1] == '.' || name[1] == '%')
            continue;
        if (pb.dirInfo.ioFlAttrib & ATTRIB_ISADIR) {
            if (pending_depth < 33) {
                pending_directories[pending_depth].volume = pending_directories[slot].volume;
                pending_directories[pending_depth].directory = pb.dirInfo.ioDrDirID;
                pending_directories[pending_depth++].index = 1;
            }
        }
        else if (pb.hFileInfo.ioFlFndrInfo.fdType == TICKX("APPL")) {
            unsigned before = count;
            remember_application(pending_directories[slot].volume, pending_directories[slot].directory, name,
                                 CL(pb.hFileInfo.ioFlFndrInfo.fdCreator));
            if (count != before) {
                persist_application(&applications[count - 1], TICK("APPL"));
                changed = 1;
            }
        }
    }
    if (!ready && (!pending_depth || !pending_budget || count >= 4096))
        ready = 1;
    if (!ready || pending_type_index >= pending_type_count)
        return changed;
    uint32 type = pending_types[pending_type_index];
    int match = -1;
    if (pending_application < count) {
        unsigned candidate = pending_application++;
        if (supports_type(&applications[candidate], type))
            match = candidate;
        else
            return changed;
    }
    unsigned slot = next_handler++ % 256;
    if (handler_count < 256)
        ++handler_count;
    type_handlers[slot].type = type;
    type_handlers[slot].application_index = match;
    if (match >= 0)
        persist_application(&applications[match], type);
    ++pending_type_index;
    pending_application = 0;
    return 1;
}

OSErr ROMlib_application_spec_cached(uint32 creator, uint32 type, FSSpec* spec)
{
    if (!spec)
        return paramErr;
    if (!creator && !type)
        return fnfErr;
    if (mapped_application(type, creator, spec) || (creator && mapped_application(TICK("APPL"), creator, spec)))
        return noErr;
    for (unsigned i = 0; creator && i < count; ++i)
        if (applications[i].creator == creator) {
            *spec = applications[i].spec;
            return noErr;
        }
    /* An unregistered creator may use the indexed default for its document
     type. This is especially common for synthetic UNIX/TEXT host metadata. */
    if (type && creator && mapped_application(type, 0, spec))
        return noErr;
    for (unsigned i = 0; type && i < handler_count; ++i)
        if (type_handlers[i].type == type) {
            int match = type_handlers[i].application_index;
            if (match >= 0) {
                *spec = applications[match].spec;
                return noErr;
            }
            break;
        }
    indexing_requested = 1;
    if (type && pending_type_count < 32) {
        unsigned i;
        for (i = 0; i < pending_type_count && pending_types[i] != type; ++i)
            ;
        if (i == pending_type_count)
            pending_types[pending_type_count++] = type;
    }
    return fnfErr;
}

OSErr ROMlib_application_spec(uint32 creator, uint32 type, unsigned index, FSSpec* spec)
{
    if (!spec)
        return paramErr;
    if (index <= 1 && mapped_application(type, creator, spec))
        return noErr;
    initialize();
    if (!index)
        index = 1;
    /* Creator ownership takes precedence over a generic file-type handler. */
    int owned = 0;
    for (unsigned i = 0; creator && i < count; ++i)
        if (applications[i].creator == creator) {
            owned = 1;
            if (!--index) {
                *spec = applications[i].spec;
                return noErr;
            }
        }
    if (owned || index != 1 || !type)
        return fnfErr;
    for (unsigned i = 0; i < handler_count; ++i)
        if (type_handlers[i].type == type) {
            int match = type_handlers[i].application_index;
            if (match < 0)
                return fnfErr;
            *spec = applications[match].spec;
            return noErr;
        }
    int match = -1;
    for (unsigned i = 0; i < count; ++i)
        if (supports_type(&applications[i], type)) {
            match = i;
            break;
        }
    unsigned slot = next_handler++ % 256;
    if (handler_count < 256)
        ++handler_count;
    type_handlers[slot].type = type;
    type_handlers[slot].application_index = match;
    if (match < 0)
        return fnfErr;
    *spec = applications[match].spec;
    return noErr;
}
