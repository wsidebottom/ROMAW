/*+===================================================================
  File:      system_resources.c

  Summary:   Embedded System defaults, with editable overrides in System Folder:Preferences:ROMAW.

  Classes:   None.

  Functions: ROMlib_romaw_preferences_spec, ROMlib_load_system_preferences, ROMlib_open_embedded_system, ROMlib_save_system_preferences

  Origin:    Existing repository source in executor/src/resources; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Embedded System defaults, with editable overrides in System Folder:Preferences:ROMAW. */
#include "rsys/common.h"
#include "ResourceMgr.h"
#include "MemoryMgr.h"
#include "FileMgr.h"
#include "AliasMgr.h"
#include "rsys/ecf.h"
#include "rsys/resource.h"
#include "rsys/file.h"
#include "rsys/system_resources.h"
#include "systemprefs.h"
#include "generated/system_resources.inc"

#define MAX_SAVED_RESOURCES (16U * 1024 * 1024)
static int preferences_ready, saving;
static unsigned char* system_defaults;
static size_t system_defaults_length;

typedef struct {
    const unsigned char *type, *name, *data;
    uint32 length;
    int16 id;
    int name_length;
    unsigned char attributes;
} saved_resource;

static uint32 be32(const unsigned char* p)
{
    return (uint32)p[0] << 24 | (uint32)p[1] << 16 | p[2] << 8 | p[3];
}

static unsigned be16(const unsigned char* p)
{
    return p[0] << 8 | p[1];
}

static int extent(size_t size, size_t offset, size_t length)
{
    return offset <= size && length <= size - offset;
}

/* Iterate a disk resource map, including resources never loaded as Handles. */
static int resource_at(const unsigned char* fork, size_t size, unsigned index, saved_resource* out)
{
    size_t map_offset, map_size, data_offset, data_size, types, names;
    const unsigned char* map;
    unsigned count;
    if (size < 16)
        return -1;
    data_offset = be32(fork);
    map_offset = be32(fork + 4);
    data_size = be32(fork + 8);
    map_size = be32(fork + 12);
    if (!extent(size, map_offset, map_size) || map_size < 30 || !extent(size, data_offset, data_size))
        return -1;
    map = fork + map_offset;
    types = be16(map + 24);
    names = be16(map + 26);
    if (!extent(map_size, types, 2) || names > map_size)
        return -1;
    count = be16(map + types);
    count = count == 65535 ? 0 : count + 1;
    if (!extent(map_size, types + 2, count * 8))
        return -1;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned char* type = map + types + 2 + i * 8;
        unsigned resources = be16(type + 4) + 1;
        if (index < resources) {
            size_t reference = types + be16(type + 6) + index * 12;
            size_t offset, name;
            const unsigned char* ref;
            if (!extent(map_size, reference, 12))
                return -1;
            ref = map + reference;
            offset = be32(ref + 4) & 0xFFFFFF;
            if (!extent(data_size, offset, 4))
                return -1;
            out->length = be32(fork + data_offset + offset);
            if (!extent(data_size, offset + 4, out->length))
                return -1;
            out->data = fork + data_offset + offset + 4;
            out->type = type;
            out->id = (int16)be16(ref);
            out->attributes = ref[4] & ~resChanged;
            out->name = NULL;
            out->name_length = -1;
            name = be16(ref + 2);
            if (name != 65535) {
                name += names;
                if (!extent(map_size, name, 1) || !extent(map_size, name + 1, map[name]))
                    return -1;
                out->name_length = map[name];
                out->name = map + name + 1;
            }
            return 1;
        }
        index -= resources;
    }
    return 0;
}

static int find_resource(const unsigned char* fork, size_t length, const saved_resource* key, saved_resource* out)
{
    int status;
    for (unsigned i = 0; (status = resource_at(fork, length, i, out)) > 0; ++i)
        if (key->id == out->id && !memcmp(key->type, out->type, 4))
            return 1;
    return status;
}

OSErr ROMlib_romaw_preferences_spec(FSSpec* spec)
{
    if (!spec)
        return paramErr;
    INTEGER volume;
    LONGINT directory;
    OSErr error = FindFolder((INTEGER)0x8000, kPreferencesFolderType, TRUE, &volume, &directory);
    if (error == noErr) {
        spec->vRefNum = volume;
        spec->parID = directory;
        memcpy(spec->name, ROMAW_PREFERENCES_NAME, sizeof ROMAW_PREFERENCES_NAME);
    }
    return error;
}

static char* romaw_preferences_path(void)
{
    FSSpec spec;
    char* path = NULL;
    if (ROMlib_romaw_preferences_spec(&spec) != noErr)
        return NULL;
    OSErr error = ROMlib_filename_from_fsspec(&path, &spec);
    if (error != noErr && error != fnfErr) {
        free(path);
        path = NULL;
    }
    return path;
}

INTEGER
ROMlib_open_embedded_system(void)
{
    FILE* temporary = tmpfile();
    int fd;
    INTEGER result;
    if (!temporary)
        goto fail;
#if defined(_WIN32)
    setmode(fileno(temporary), O_BINARY);
#endif
    if (fwrite(embedded_system, 1, sizeof embedded_system, temporary) != sizeof embedded_system || fflush(temporary) ||
        fseek(temporary, 0, SEEK_SET)) {
        fclose(temporary);
        goto fail;
    }
    fd = dup(fileno(temporary));
    fclose(temporary);
    if (fd < 0)
        goto fail;
    result = ROMlib_open_internal_resource(fd, sizeof embedded_system);
    return result;
fail:
    ROMlib_setreserr(ioErr);
    return -1;
}

/* Saved records: type[4], id[2], attrs[1], name length[2], data length[4],
   optional name, payload. FFFF means no name; FFFFFFFF means deletion. */
static int next_saved(const unsigned char* bytes, size_t length, size_t* offset, saved_resource* r)
{
    const unsigned char* p;
    size_t size;
    if (*offset == length)
        return 0;
    if (!extent(length, *offset, 13))
        return -1;
    p = bytes + *offset;
    r->type = p;
    r->id = (int16)be16(p + 4);
    r->attributes = p[6] & ~resChanged;
    r->name_length = be16(p + 7);
    if (r->name_length == 65535)
        r->name_length = -1;
    if (r->name_length > 255)
        return -1;
    r->length = be32(p + 9);
    size = 13 + (r->name_length < 0 ? 0 : r->name_length);
    if (r->length != UINT32_MAX) {
        if (r->length > MAX_SAVED_RESOURCES)
            return -1;
        size += r->length;
    }
    if (!extent(length, *offset, size))
        return -1;
    r->name = p + 13;
    r->data = p + 13 + (r->name_length < 0 ? 0 : r->name_length);
    *offset += size;
    return 1;
}

static OSErr read_system_fork(INTEGER refnum, unsigned char** bytes, size_t* length)
{
    OSErr error;
    fcbrec* fp = PRNTOFPERR(refnum, &error);
    LONGINT position;
    int ok;
    *bytes = NULL;
    *length = 0;
    if (error != noErr)
        return error;
    *length = Cx(fp->fcleof);
    if (*length > MAX_SAVED_RESOURCES)
        return ioErr;
    *bytes = malloc(*length);
    if (!*bytes)
        return memFullErr;
    position = lseek(fp->fcfd, 0, SEEK_CUR);
    if (position < 0)
        return ioErr;
    ok = lseek(fp->fcfd, 0, SEEK_SET) == 0 && read(fp->fcfd, *bytes, *length) == *length;
    if (lseek(fp->fcfd, position, SEEK_SET) < 0)
        ok = 0;
    return ok ? noErr : ioErr;
}

static int append_saved(unsigned char** bytes, size_t* length, const saved_resource* r)
{
    size_t names = r->name_length < 0 ? 0 : r->name_length;
    size_t payload = r->length == UINT32_MAX ? 0 : r->length;
    size_t size = 13 + names + payload;
    unsigned char *p, *grown;
    if (size > MAX_SAVED_RESOURCES - *length)
        return 0;
    grown = realloc(*bytes, *length + size);
    if (!grown)
        return 0;
    *bytes = grown;
    p = grown + *length;
    memcpy(p, r->type, 4);
    p[4] = (uint16)r->id >> 8;
    p[5] = r->id;
    p[6] = r->attributes;
    p[7] = (uint16)r->name_length >> 8;
    p[8] = r->name_length;
    p[9] = r->length >> 24;
    p[10] = r->length >> 16;
    p[11] = r->length >> 8;
    p[12] = r->length;
    if (names)
        memcpy(p + 13, r->name, names);
    if (payload)
        memcpy(p + 13 + names, r->data, payload);
    *length += size;
    return 1;
}

/* ExDl/-32768 holds six-byte type/ID keys for deleted embedded defaults.
   Ordinary overrides remain ordinary editable Macintosh resources. */
static const unsigned char deleted_type[4] = {'E', 'x', 'D', 'l'};
#define DELETED_RESOURCE_ID (-32768)

static void put16(unsigned char* p, unsigned n)
{
    p[0] = n >> 8;
    p[1] = n;
}

static void put32(unsigned char* p, uint32 n)
{
    p[0] = n >> 24;
    p[1] = n >> 16;
    p[2] = n >> 8;
    p[3] = n;
}

static int compare_saved(const void* a, const void* b)
{
    const saved_resource *left = a, *right = b;
    int result = memcmp(left->type, right->type, 4);
    return result ? result : (int)left->id - right->id;
}

static int fork_from_saved(const unsigned char* bytes, size_t length, unsigned char** fork, size_t* size)
{
    saved_resource r, *records = NULL;
    unsigned char *deletions = NULL, *result = NULL;
    size_t offset = 8, count = 0, removed = 0, seen = 0;
    size_t types = 0, names = 0, data = 0;
    int status, ok = 0;
    *fork = NULL;
    *size = 0;
    if (length < 8 || length > MAX_SAVED_RESOURCES || memcmp(bytes, "ERS1", 4) || be32(bytes + 4) != length)
        return 0;
    /* A resource map's 16-bit offsets bound its reference count. */
    size_t capacity = MIN((length - 8) / 13 + 1, 5460);
    records = calloc(capacity, sizeof *records);
    deletions = malloc(capacity * 6);
    if (!records || !deletions)
        goto done;
    while ((status = next_saved(bytes, length, &offset, &r)) > 0) {
        if (++seen >= capacity || (!memcmp(r.type, deleted_type, 4) && r.id == DELETED_RESOURCE_ID))
            goto done;
        if (r.length == UINT32_MAX) {
            memcpy(deletions + removed * 6, r.type, 4);
            put16(deletions + removed * 6 + 4, (uint16)r.id);
            ++removed;
        }
        else
            records[count++] = r;
    }
    if (status < 0)
        goto done;
    if (removed) {
        saved_resource deleted = {.type = deleted_type,
                                  .name = (const unsigned char*)"Deleted embedded resources",
                                  .data = deletions,
                                  .length = removed * 6,
                                  .id = DELETED_RESOURCE_ID,
                                  .name_length = 26};
        records[count++] = deleted;
    }
    qsort(records, count, sizeof *records, compare_saved);
    for (size_t i = 0; i < count; ++i) {
        if (i && !compare_saved(&records[i - 1], &records[i]))
            goto done;
        if (!i || memcmp(records[i - 1].type, records[i].type, 4))
            ++types;
        names += records[i].name_length < 0 ? 0 : records[i].name_length + 1;
        data += 4 + records[i].length;
    }
    size_t names_offset = 30 + types * 8 + count * 12;
    size_t map_size = names_offset + names;
    if (map_size > 65535 || data > MAX_SAVED_RESOURCES - 256 || map_size > MAX_SAVED_RESOURCES - 256 - data)
        goto done;
    size_t total = 256 + data + map_size;
    result = calloc(1, total);
    if (!result)
        goto done;
    put32(result, 256);
    put32(result + 4, 256 + data);
    put32(result + 8, data);
    put32(result + 12, map_size);
    unsigned char* map = result + 256 + data;
    memcpy(map, result, 16);
    put16(map + 24, 28);
    put16(map + 26, names_offset);
    put16(map + 28, types - 1);
    size_t type_index = 0, data_offset = 0, name_offset = 0;
    for (size_t i = 0; i < count;) {
        size_t first = i;
        while (i < count && !memcmp(records[first].type, records[i].type, 4))
            ++i;
        unsigned char* type = map + 30 + type_index++ * 8;
        memcpy(type, records[first].type, 4);
        put16(type + 4, i - first - 1);
        put16(type + 6, 2 + types * 8 + first * 12);
        for (size_t j = first; j < i; ++j) {
            const saved_resource* item = &records[j];
            unsigned char* ref = map + 30 + types * 8 + j * 12;
            put16(ref, (uint16)item->id);
            put16(ref + 2, item->name_length < 0 ? 65535 : name_offset);
            put32(ref + 4, (uint32)item->attributes << 24 | data_offset);
            put32(result + 256 + data_offset, item->length);
            memcpy(result + 260 + data_offset, item->data, item->length);
            data_offset += 4 + item->length;
            if (item->name_length >= 0) {
                unsigned char* name = map + names_offset + name_offset;
                *name = item->name_length;
                if (item->name_length)
                    memcpy(name + 1, item->name, item->name_length);
                name_offset += item->name_length + 1;
            }
        }
    }
    *fork = result;
    *size = total;
    result = NULL;
    ok = 1;
done:
    free(records);
    free(deletions);
    free(result);
    return ok;
}

static int saved_from_fork(const unsigned char* fork, size_t size, unsigned char** bytes, size_t* length)
{
    saved_resource r;
    unsigned char* result = malloc(8);
    size_t used = 8;
    int status;
    *bytes = NULL;
    *length = 0;
    if (!result || size > MAX_SAVED_RESOURCES)
        goto invalid;
    memcpy(result, "ERS1", 4);
    for (unsigned i = 0; (status = resource_at(fork, size, i, &r)) > 0; ++i) {
        if (!memcmp(r.type, deleted_type, 4) && r.id == DELETED_RESOURCE_ID) {
            if (r.length % 6)
                goto invalid;
            for (size_t n = 0; n < r.length; n += 6) {
                saved_resource deleted = {
                    .type = r.data + n, .id = (int16)be16(r.data + n + 4), .length = UINT32_MAX, .name_length = -1};
                if (!append_saved(&result, &used, &deleted))
                    goto invalid;
            }
        }
        else if (!append_saved(&result, &used, &r))
            goto invalid;
    }
    if (status < 0)
        goto invalid;
    put32(result + 4, used);
    *bytes = result;
    *length = used;
    return 1;
invalid:
    free(result);
    return 0;
}

static int write_resource_overrides(const char* path, const unsigned char* bytes, size_t length)
{
    unsigned char* fork;
    size_t size;
    if (!fork_from_saved(bytes, length, &fork, &size))
        return 0;
    int ok = system_resource_write(path, fork, size);
    free(fork);
    return ok;
}

void ROMlib_load_system_preferences(void)
{
    char* path = romaw_preferences_path();
    unsigned char *bytes = NULL, *fork = NULL;
    size_t length = 0, fork_length = 0, offset;
    int create_file = 0;
    saved_resource r;
    int status;
    INTEGER saved = CurResFile();
    preferences_ready = 0;
    if (!path)
        goto cleanup;
    /* Fonts installed from suitcases are defaults for this launch. Do not save
     them as user overrides, which would outlive a removed/updated suitcase. */
    UpdateResFile(CW(SysMap));
    free(system_defaults);
    system_defaults = NULL;
    if (ResErr != noErr || read_system_fork(CW(SysMap), &system_defaults, &system_defaults_length) != noErr)
        goto invalid;
    if (!system_resource_read(path, &fork, &fork_length))
        goto invalid;
    if (fork) {
        if (!saved_from_fork(fork, fork_length, &bytes, &length))
            goto invalid;
    }
    else {
        create_file = 1;
        bytes = malloc(8);
        if (!bytes)
            goto invalid;
        memcpy(bytes, "ERS1\0\0\0\10", 8);
        length = 8;
    }
    if (length < 8 || memcmp(bytes, "ERS1", 4) || be32(bytes + 4) != length)
        goto invalid;
    offset = 8;
    while ((status = next_saved(bytes, length, &offset, &r)) > 0)
        ;
    if (status < 0)
        goto invalid;
    UseResFile(CW(SysMap));
    offset = 8;
    while (next_saved(bytes, length, &offset, &r) > 0) {
        ResType type = be32(r.type);
        Handle h = Get1Resource(type, r.id);
        if (h) {
            SetResAttrs(h, GetResAttrs(h) & ~resProtected);
            RmveResource(h);
            if (ResErr != noErr)
                goto invalid;
            DisposHandle(h);
        }
        if (r.length != UINT32_MAX) {
            Str255 name;
            h = NewHandleSys(r.length);
            if (!h)
                goto invalid;
            if (r.length)
                memcpy(STARH(h), r.data, r.length);
            name[0] = r.name_length < 0 ? 0 : r.name_length;
            if (name[0])
                memcpy(name + 1, r.name, name[0]);
            AddResource(h, type, r.id, name);
            if (ResErr != noErr) {
                DisposHandle(h);
                goto invalid;
            }
            ChangedResource(h);
            SetResAttrs(h, r.attributes | resChanged);
        }
    }
    UpdateResFile(CW(SysMap));
    if (ResErr != noErr)
        goto invalid;
    if (create_file && !write_resource_overrides(path, bytes, length))
        goto invalid;
    preferences_ready = 1;
    goto cleanup;
invalid:
    fprintf(stderr, "Unable to load System resource overrides from %s\n", path);
    /* Preserve unreadable preferences instead of overwriting them. */
cleanup:
    UseResFile(saved);
    free(path);
    free(fork);
    free(bytes);
}

OSErr ROMlib_save_system_preferences(INTEGER refnum)
{
    OSErr error;
    unsigned char *fork = NULL, *bytes = NULL;
    char* path = NULL;
    size_t size, length = 8;
    saved_resource current, base;
    int status;
    if (!preferences_ready || saving || refnum != CW(SysMap))
        return noErr;
    saving = 1;
    error = read_system_fork(refnum, &fork, &size);
    if (error != noErr)
        goto done;
    error = ioErr;
    bytes = malloc(8);
    path = romaw_preferences_path();
    if (!bytes || !path) {
        error = memFullErr;
        goto done;
    }
    memcpy(bytes, "ERS1", 4);
    for (unsigned i = 0; (status = resource_at(fork, size, i, &current)) > 0; ++i) {
        int found = find_resource(system_defaults, system_defaults_length, &current, &base);
        if (found < 0)
            goto done;
        if (!found || current.length != base.length || current.name_length != base.name_length ||
            current.attributes != base.attributes ||
            (current.name_length > 0 && memcmp(current.name, base.name, current.name_length)) ||
            memcmp(current.data, base.data, current.length))
            if (!append_saved(&bytes, &length, &current))
                goto done;
    }
    if (status < 0)
        goto done;
    for (unsigned i = 0; (status = resource_at(system_defaults, system_defaults_length, i, &base)) > 0; ++i) {
        int found = find_resource(fork, size, &base, &current);
        if (found < 0)
            goto done;
        if (!found) {
            base.length = UINT32_MAX;
            if (!append_saved(&bytes, &length, &base))
                goto done;
        }
    }
    bytes[4] = length >> 24;
    bytes[5] = length >> 16;
    bytes[6] = length >> 8;
    bytes[7] = length;
    if (status >= 0 && write_resource_overrides(path, bytes, length))
        error = noErr;
done:
    free(path);
    free(bytes);
    free(fork);
    saving = 0;
    return error;
}
