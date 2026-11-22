/*+===================================================================
  File:      fileDouble.c

  Summary:   Implements file double support for Macintosh File Manager and host-file operations.

  Classes:   None.

  Functions: ROMlib_FORKOFFSET, ROMlib_basilisk_sidecar_p, ROMlib_close_empty_fork, ROMlib_empty_fork_busy,
             ROMlib_empty_fork_p, ROMlib_forget_fork_offset, ROMlib_geteofostype, ROMlib_hiddenbyname, ROMlib_newresfork, ROMlib_open_empty_fork,
             ROMlib_orphan_scan_close, ROMlib_orphan_scan_next, ROMlib_orphan_scan_open, ROMlib_resource_fork_present,
             ROMlib_seteof, ROMlib_sidecar_catalog_batch, ROMlib_sidecar_delete, ROMlib_sidecar_move,
             ROMlib_sidecar_name, ROMlib_sidecar_rename_name, ROMlib_sidecar_resource_name, ROMlib_ufs_stat,
             double_dir_op

  Origin:    Existing repository source in executor/src/files; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1992-2000 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "FileMgr.h"
#include "rsys/file.h"
#include "rsys/mac_storage.h"

#if defined(CYGWIN32)
#include "winfs.h"
#endif

int afpd_conventions_p;
int netatalk_conventions_p;

char apple_double_quote_char;
const char* apple_double_fork_prefix;
int apple_double_fork_prefix_length;

/*
 * Coded more or less up to the spec:  APDA M0908LL/A
 *				       AppleSingle/AppleDouble Formats
 *				       For Foreign Files Developer's Note
 *
 * I don't like this spec for a variety of reasons:
 *
 *	The idea that you should ever have to look inside a file to determine
 *	whether %father is really 0xfather or the header file for "father"
 *	is messed.  The way we get around this is we always quote the first
 *	if that's really what they want, so the header file for "father"
 *	will be %%66ather (actually we only do that if the two characters
 *	are hex for something that we'd expand normally (i.e. '%', '/', NUL
 *	or something with the high bit on, like 0xFA.
 *
 *	The spec says you can put the descriptors in any order you want, but
 *	it would be much nicer if they had to be sorted by offset.  This is
 *	what we do/assume.
 */

#include "rsys/filedouble.h"
#include "rsys/suffix_maps.h"
#include "rsys/osutil.h"

PRIVATE defaulthead_t ourdefault = {{
                                        CLC(DOUBLEMAGIC),
                                        CLC(SINGLEVERSION),
                                        {
                                            0L,
                                            0L,
                                            0L,
                                            0L,
                                        },
                                        CWC(4),
                                    },
                                    {
#define LEN0 (sizeof(struct defaulthead))
                                        {CLC(Macintosh_File_Info_ID), CLC(LEN0), CLC(sizeof(Single_attribs))},
#define LEN1 (LEN0 + sizeof(Single_attribs))
                                        {CLC(File_Dates_Info_ID), CLC(LEN1), CLC(sizeof(Single_dates))},
#define LEN2 (LEN1 + sizeof(Single_dates))
                                        {CLC(Finder_Info_ID), CLC(LEN2), CLC(sizeof(Single_finfo))},
                                        {CLC(Resource_Fork_ID), CLC(512), 0L},
                                    }};

#define initialize_ourdefault()

PRIVATE defaultentries_t ourentries;

/* Detect the conventions per directory; mounted host volumes can mix them.
 * Prefer OS X, then Netatalk, then Basilisk/SheepShaver.
 */
PRIVATE char* sidecar_path(const char* pathname, const char* filename, const char* prefix)
{
    size_t parent = filename - pathname;
    char* result = malloc(parent + strlen(prefix) + strlen(filename) + 1);
    if (result) {
        memcpy(result, pathname, parent);
        sprintf(result + parent, "%s%s", prefix, filename);
    }
    return result;
}

PRIVATE boolean_t sidecar_exists(const char* name, boolean_t directory)
{
    struct stat sb;
    return name && Ustat(name, &sb) == 0 && (directory ? S_ISDIR(sb.st_mode) : S_ISREG(sb.st_mode));
}

/* Only invariant metadata-directory probes are shared within a read-only
   catalog snapshot. Individual companions still win in the normal order. */
static struct {
    char* path;
    boolean_t directory, present;
} catalog_probes[64];

static unsigned catalog_probe_next;
static boolean_t catalog_batch;

PUBLIC void ROMlib_sidecar_catalog_batch(boolean_t enabled)
{
    if (!enabled || !catalog_batch) {
        for (unsigned i = 0; i < 64; ++i) {
            free(catalog_probes[i].path);
            catalog_probes[i].path = NULL;
        }
        catalog_probe_next = 0;
    }
    catalog_batch = enabled;
}

static boolean_t catalog_probe(const char* name, boolean_t directory)
{
    if (!name || !catalog_batch)
        return sidecar_exists(name, directory);
    for (unsigned i = 0; i < 64; ++i)
        if (catalog_probes[i].path && catalog_probes[i].directory == directory && !strcmp(catalog_probes[i].path, name))
            return catalog_probes[i].present;
    boolean_t present = sidecar_exists(name, directory);
    unsigned slot = catalog_probe_next++ % 64;
    free(catalog_probes[slot].path);
    catalog_probes[slot].path = strdup(name);
    catalog_probes[slot].directory = directory;
    catalog_probes[slot].present = present;
    return present;
}

static boolean_t companion_directory(const char* pathname, const char* filename, const char* prefix)
{
    if (!catalog_batch)
        return TRUE;
    char* path = sidecar_path(pathname, filename, prefix);
    if (!path)
        return TRUE; /* Allocation failure must not suppress a real companion. */
    path[filename - pathname + strlen(prefix)] = 0;
    boolean_t present = catalog_probe(path, TRUE);
    free(path);
    return present;
}

PRIVATE boolean_t appledouble_file_p(const char* name)
{
    Single_header header;
    int fd = Uopen(name, O_BINARY | O_RDONLY, 0);
    boolean_t result =
        fd >= 0 && read(fd, &header, sizeof(header)) == sizeof(header) && header.magic == CLC(DOUBLEMAGIC);
    if (fd >= 0)
        Uclose(fd);
    return result;
}

PUBLIC boolean_t ROMlib_basilisk_sidecar_p(const char* name)
{
    const char* leaf = strrchr(name, '/');
    return leaf && leaf - name >= 6 && !memcmp(leaf - 6, "/.finf", 6);
}

PUBLIC char* ROMlib_sidecar_resource_name(const char* metadata)
{
    char* result = strdup(metadata);
    if (result && ROMlib_basilisk_sidecar_p(result))
        memcpy(strrchr(result, '/') - 5, ".rsrc", 5);
    return result;
}

PUBLIC char* ROMlib_sidecar_rename_name(const char* source, const char* pathname, const char* filename)
{
    const char* leaf = strrchr(source, '/');
    const char* prefix = apple_double_fork_prefix;
    if (ROMlib_basilisk_sidecar_p(source))
        prefix = ".finf/";
    else if (leaf && leaf - source >= 13 && !memcmp(leaf - 13, "/.AppleDouble", 13))
        prefix = ".AppleDouble/";
    else if (leaf && !strncmp(leaf + 1, "._", 2))
        prefix = "._";
    return sidecar_path(pathname, filename, prefix);
}

PUBLIC char* ROMlib_sidecar_name(const char* pathname, const char* filename)
{
    const char* prefixes[] = {MAC_STORAGE_NETATALK_DIRECTORY "/", MAC_STORAGE_INFO_DIRECTORY "/", "._"};
    char *candidate, *directory;
    int i, style = -1;
    DIR* dir;
    struct dirent* entry;

    /* Existing companions take precedence over directory defaults. */
    const unsigned priority[] = {2, 0, 1};
    for (unsigned choice = 0; choice < 3; ++choice) {
        i = priority[choice];
        candidate = sidecar_path(pathname, filename, prefixes[i]);
        boolean_t possible = i == 2 || companion_directory(pathname, filename, i ? ".finf" : ".AppleDouble");
        if (possible && sidecar_exists(candidate, FALSE) && (i != 2 || appledouble_file_p(candidate)))
            return candidate;
        if (i == 1 && companion_directory(pathname, filename, ".rsrc")) {
            char* resource = ROMlib_sidecar_resource_name(candidate);
            boolean_t exists = sidecar_exists(resource, FALSE);
            free(resource);
            if (exists)
                return candidate;
        }
        free(candidate);
    }
    candidate = sidecar_path(pathname, filename, "%");
    if (candidate && appledouble_file_p(candidate))
        return candidate;
    free(candidate);
    if (style < 0) {
#if defined(CYGWIN32)
        struct _finddata_t entry;
        intptr_t search;

        /* A Finder-info lookup is made for every displayed host file.
         Enumerating every sibling here makes browsing a large Windows
         directory quadratic. Ask Windows for AppleDouble names only. */
        directory = sidecar_path(pathname, filename, ".DS_Store");
        if (directory)
            directory[(filename - pathname) + 9] = 0;
        if (catalog_probe(directory, FALSE))
            style = 2;
        free(directory);
        if (style < 0) {
            directory = sidecar_path(pathname, filename, "._*");
            if (directory)
                directory[(filename - pathname) + 3] = 0;
            search = directory ? _findfirst(directory, &entry) : -1;
            if (search != -1) {
                do {
                    char* probe = sidecar_path(pathname, filename, entry.name);
                    boolean_t valid;
                    if (probe)
                        probe[(filename - pathname) + strlen(entry.name)] = 0;
                    valid = probe && appledouble_file_p(probe);
                    free(probe);
                    if (valid) {
                        style = 2;
                        break;
                    }
                } while (_findnext(search, &entry) == 0);
                _findclose(search);
            }
            free(directory);
        }
#else
        directory = sidecar_path(pathname, filename, "");
        if (directory)
            directory[filename - pathname] = 0;
        dir = directory ? Uopendir(directory) : NULL;
        if (dir) {
            while ((entry = readdir(dir)))
                if (!strcmp(entry->d_name, ".DS_Store")) {
                    style = 2;
                    break;
                }
                else if (!strncmp(entry->d_name, "._", 2)) {
                    char* probe = sidecar_path(pathname, filename, entry->d_name);
                    boolean_t valid;
                    if (probe)
                        probe[(filename - pathname) + strlen(entry->d_name)] = 0;
                    valid = probe && appledouble_file_p(probe);
                    free(probe);
                    if (valid) {
                        style = 2;
                        break;
                    }
                }
            closedir(dir);
        }
        free(directory);
#endif
    }
    for (i = 0; style < 0 && i < 2; ++i) {
        directory = sidecar_path(pathname, filename, i == 0 ? ".AppleDouble" : ".finf");
        if (directory)
            directory[(filename - pathname) + (i == 0 ? 12 : 5)] = 0;
        if (catalog_probe(directory, TRUE))
            style = i;
        free(directory);
    }
    if (style < 0) {
        directory = sidecar_path(pathname, filename, ".rsrc");
        if (directory)
            directory[(filename - pathname) + 5] = 0;
        if (catalog_probe(directory, TRUE))
            style = 1;
        free(directory);
    }
    return sidecar_path(pathname, filename, style >= 0 ? prefixes[style] : apple_double_fork_prefix);
}

PUBLIC void double_dir_op(char* name, double_dir_op_t op)
{
    char* last_slash;

    last_slash = strrchr(name, '/');
    if (last_slash) {
        char save;

        save = *last_slash;
        *last_slash = 0;
        switch (op) {
        case mkdir_op:
            Umkdir(name, 0777);
            break;
        case rmdir_op: {
            char *to_del, *to_unlink;
            int len;

#define PARENT_FILE ".Parent"

            *last_slash = save;
            len = strlen(name) + sizeof("/.AppleDouble/");
            to_del = alloca(len + 16);
            sprintf(to_del, "%s/.AppleDouble/", name);
            to_unlink = alloca(len + strlen(PARENT_FILE));
            sprintf(to_unlink, "%s%s", to_del, PARENT_FILE);
            unlink(to_unlink);
            to_del[len - 2] = 0; /* remove trailing slash */
            Urmdir(to_del);
            sprintf(to_del, "%s/.finf", name);
            Urmdir(to_del);
            sprintf(to_del, "%s/.rsrc", name);
            Urmdir(to_del);
            sprintf(to_del, "%s/.DS_Store", name);
            Uunlink(to_del);
        } break;
        default:
            warning_unexpected("op = %d", op);
            break;
        }
        *last_slash = save;
    }
}

PUBLIC int ROMlib_sidecar_move(char* source, char* target)
{
    struct stat sb;
    int result = 0;
    if (ROMlib_basilisk_sidecar_p(source)) {
        char* old_resource = ROMlib_sidecar_resource_name(source);
        char* new_resource = ROMlib_sidecar_resource_name(target);
        if (Ustat(old_resource, &sb) == 0) {
            double_dir_op(new_resource, mkdir_op);
            result = Urename(old_resource, new_resource);
        }
        free(old_resource);
        free(new_resource);
    }
    if (!result && Ustat(source, &sb) == 0) {
        double_dir_op(target, mkdir_op);
        result = Urename(source, target);
    }
    return result;
}

PUBLIC int ROMlib_sidecar_delete(char* metadata)
{
    int result;
    boolean_t removed_resource = FALSE;
    if (ROMlib_basilisk_sidecar_p(metadata)) {
        char* resource = ROMlib_sidecar_resource_name(metadata);
        result = Uunlink(resource);
        removed_resource = result == 0;
        free(resource);
        if (result && errno != ENOENT && errno != ENOTDIR)
            return result;
    }
    result = Uunlink(metadata);
    return result && removed_resource && errno == ENOENT ? 0 : result;
}

A3(PUBLIC, OSErr, ROMlib_newresfork, char*, name, LONGINT*, fdp, boolean_t, unix_p)
{
    LONGINT fd;
    OSErr retval;

    if (netatalk_conventions_p)
        double_dir_op(name, mkdir_op);
    if (unix_p) {
        ourentries.finfo.finfo.fdType = TICKX("TEXT");
        ourentries.finfo.finfo.fdCreator = TICKX("UNIX");
        uint32 creator, type;
        if (ROMlib_creator_and_type_from_filename(strlen(name), name, &creator, &type)) {
            ourentries.finfo.finfo.fdCreator = CL(creator);
            ourentries.finfo.finfo.fdType = CL(type);
        }
    }
    else {
        ourentries.finfo.finfo.fdType = 0;
        ourentries.finfo.finfo.fdCreator = 0;
    }
    initialize_ourdefault();
    /* Basilisk stores FInfo followed by FXInfo (16 bytes each). Its resource
       fork is an ordinary file starting at offset zero. */
    if (ROMlib_basilisk_sidecar_p(name) || strstr(name, "/.rsrc/")) {
        unsigned char finfo[32] = {0};
        double_dir_op(name, mkdir_op);
        fd = Uopen(name, O_BINARY | O_RDWR | O_CREAT, 0666);
        if (fd < 0)
            return ROMlib_maperrno();
        if (ROMlib_basilisk_sidecar_p(name)) {
            memcpy(finfo, &ourentries.finfo.finfo, sizeof(FInfo));
            if (write(fd, finfo, sizeof(finfo)) != sizeof(finfo)) {
                retval = ROMlib_maperrno();
                Uclose(fd);
                return retval;
            }
        }
        *fdp = fd;
        return noErr;
    }
    const char* leaf = strrchr(name, '/');
    const char* backslash = strrchr(name, '\\');
    if (!leaf || (backslash && backslash > leaf))
        leaf = backslash;
    leaf = leaf ? leaf + 1 : name;
    if (leaf[0] == '.' && leaf[1] == '_') {
        /* Native OS X writers require FinderInfo first and resource fork second.
         Legacy multi-entry headers can be read but damaged on resource saves. */
        struct PACKED {
            Single_header head;
            Single_descriptor desc[2];
            Single_finfo finfo;
        } osx = {0};

        _Static_assert(sizeof(osx) == 82, "OS X AppleDouble header layout");
        osx.head.magic = CLC(DOUBLEMAGIC);
        osx.head.version = CLC(SINGLEVERSION);
        memcpy(osx.head.filler, "Mac OS X        ", 16);
        osx.head.nentries = CWC(2);
        osx.desc[0] = (Single_descriptor){CLC(Finder_Info_ID), CLC(50), CLC(32)};
        osx.desc[1] = (Single_descriptor){CLC(Resource_Fork_ID), CLC(82), 0};
        osx.finfo = ourentries.finfo;
        fd = Uopen(name, O_BINARY | O_RDWR | O_CREAT, 0666);
        if (fd < 0)
            return ROMlib_maperrno();
        if (write(fd, &osx, sizeof(osx)) != sizeof(osx)) {
            retval = ROMlib_maperrno();
            Uclose(fd);
            return retval;
        }
        *fdp = fd;
        return noErr;
    }
    if ((fd = Uopen(name, (O_BINARY | O_RDWR | O_CREAT), 0666L)) < 0 ||
        write(fd, (char*)&ourdefault, sizeof(ourdefault)) != sizeof(ourdefault) ||
        write(fd, (char*)&ourentries, sizeof(ourentries)) != sizeof(ourentries)) {
        retval = ROMlib_maperrno();
        Uclose(fd);
    }
    else {
        retval = noErr;
        *fdp = fd;
    }
    fs_err_hook(retval);
    return retval;
}

/*
 * Modifications to hiddenbyname:
 *
 * haven't thought it through yet.
 */

#define OURBSIZE 512

/* Sidecar offsets are stable while a fork is open. Keep this host-only cache
 * outside the guest FCB layout; descriptor writes invalidate every entry. */
static struct {
    boolean_t valid;
    LONGINT fd, hiddenfd, file_id;
    uint32 volume;
    Byte resource;
    LONGINT offset;
} fork_offsets[NFCB];

static int fork_offset_slot(const fcbrec* fp)
{
    uintptr_t address = (uintptr_t)fp, first = (uintptr_t)ROMlib_fcblocks;
    if (address < first || address - first >= sizeof(fcbrec) * NFCB || (address - first) % sizeof(fcbrec))
        return -1;
    return (address - first) / sizeof(fcbrec);
}

void ROMlib_forget_fork_offset(fcbrec* fp)
{
    int slot = fork_offset_slot(fp);
    if (slot >= 0)
        fork_offsets[slot].valid = FALSE;
}

A5(PRIVATE, BOOLEAN, getsetentry, GetOrSetType, gors, LONGINT, fd, Single_ID, sid, Single_descriptor*, savesdp,
   ULONGINT*, lengthp)
{
    LONGINT saveloc;
    INTEGER nread;
    char buf[OURBSIZE];
    BOOLEAN retval = FALSE;
    struct stat sbuf;
    ROMAWMacStorageEntry entry;

    if (gors == Set)
        memset(fork_offsets, 0, sizeof fork_offsets);
    if (fd < 0)
        return FALSE;
    saveloc = lseek(fd, 0L, L_INCR);
    if (saveloc < 0 || lseek(fd, 0L, L_SET) < 0)
        return FALSE;
    nread = read(fd, buf, sizeof buf);
    if (nread < 0 || fstat(fd, &sbuf) < 0 ||
        !romaw_mac_storage_find((const unsigned char*)buf, nread, sbuf.st_size, sid, TRUE, &entry))
        goto finished;
    if (gors == Get) {
        savesdp->id = CL(entry.id);
        savesdp->offset = CL(entry.offset);
        savesdp->length = CL(entry.length);
        if (lengthp)
            *lengthp = entry.capacity;
        retval = TRUE;
    }
    else if (gors == Set) {
        unsigned count;
        // Compact .finf records have no descriptor table to update.
        if (!romaw_mac_storage_table((const unsigned char*)buf, nread, &count))
            goto finished;
        for (unsigned index = 0; index < count; ++index)
            if (romaw_mac_storage_u32((const unsigned char*)buf + 26 + index * 12) == sid) {
                entry.id = CL(savesdp->id);
                entry.offset = CL(savesdp->offset);
                entry.length = CL(savesdp->length);
                if (entry.id == sid &&
                    romaw_mac_storage_store((unsigned char*)buf, nread, sbuf.st_size, index, &entry) &&
                    lseek(fd, 0L, L_SET) >= 0 && write(fd, buf, nread) == nread)
                    retval = TRUE;
                break;
            }
    }
finished:
    lseek(fd, saveloc, L_SET);
    return retval;
}

PUBLIC boolean_t ROMlib_resource_fork_present(LONGINT fd)
{
    Single_descriptor entry;
    return getsetentry(Get, fd, Resource_Fork_ID, &entry, NULL);
}

/* A valid companion is also a catalog entry when the data fork is absent.
   Never turn arbitrary/truncated ._ files into Macintosh files. */
static boolean_t catalog_companion(const char* path, struct stat* info)
{
    if (Ustat(path, info) < 0 || !S_ISREG(info->st_mode))
        return FALSE;
    if (strstr(path, "/.rsrc/"))
        return TRUE;
    if (!ROMlib_basilisk_sidecar_p(path) && !appledouble_file_p(path))
        return FALSE;
    int fd = Uopen(path, O_BINARY | O_RDONLY, 0);
    Single_descriptor descriptor;
    boolean_t valid = fd >= 0 && (getsetentry(Get, fd, Finder_Info_ID, &descriptor, NULL) ||
                                  getsetentry(Get, fd, Resource_Fork_ID, &descriptor, NULL));
    if (fd >= 0)
        Uclose(fd);
    return valid;
}

int ROMlib_ufs_stat(const char* path, struct stat* info)
{
    if (Ustat(path, info) == 0)
        return 0;
    if (errno != ENOENT)
        return -1;
    const char* leaf = strrchr(path, '/');
    leaf = leaf ? leaf + 1 : path;
    char* metadata = ROMlib_sidecar_name(path, leaf);
    if (!metadata) {
        errno = ENOMEM;
        return -1;
    }
    boolean_t valid = catalog_companion(metadata, info);
    if (!valid && ROMlib_basilisk_sidecar_p(metadata)) {
        char* resource = ROMlib_sidecar_resource_name(metadata);
        valid = resource && catalog_companion(resource, info);
        free(resource);
    }
    free(metadata);
    if (!valid) {
        errno = ENOENT;
        return -1;
    }
    info->st_size = 0;
    return 0;
}

struct ROMlib_orphan_scan {
    char* directory;
    DIR* stream;
    unsigned stage;
    char name[1024];
};

ROMlib_orphan_scan* ROMlib_orphan_scan_open(const char* directory)
{
    ROMlib_orphan_scan* scan = calloc(1, sizeof *scan);
    if (scan) {
        scan->directory = strdup(directory);
        if (!scan->directory) {
            free(scan);
            return NULL;
        }
    }
    return scan;
}

void ROMlib_orphan_scan_close(ROMlib_orphan_scan* scan)
{
    if (scan) {
        if (scan->stream)
            closedir(scan->stream);
        free(scan->directory);
        free(scan);
    }
}

const char* ROMlib_orphan_scan_next(ROMlib_orphan_scan* scan, struct stat* info)
{
    static const char* folders[] = {"", ".AppleDouble/", ".finf/", ".rsrc/"};
    if (!scan)
        return NULL;
    while (scan->stage < 4) {
        if (!scan->stream) {
            char* folder = malloc(strlen(scan->directory) + 16);
            if (!folder)
                return NULL;
            sprintf(folder, "%s/%s", scan->directory, folders[scan->stage]);
            scan->stream = Uopendir(folder);
            free(folder);
            if (!scan->stream) {
                ++scan->stage;
                continue;
            }
        }
        struct dirent* entry = readdir(scan->stream);
        if (!entry) {
            closedir(scan->stream);
            scan->stream = NULL;
            ++scan->stage;
            continue;
        }
        const char* name = entry->d_name;
        if (!scan->stage) {
            if (!strncmp(name, "._", 2))
                name += 2;
            else if (*name == '%')
                ++name;
            else
                continue;
        }
        if (!*name || *name == '.' || !strcmp(name, "%2F") || strlen(name) >= sizeof scan->name)
            continue;
        char* path = malloc(strlen(scan->directory) + strlen(name) + 2);
        char* source = malloc(strlen(scan->directory) + strlen(folders[scan->stage]) + strlen(entry->d_name) + 2);
        if (!path || !source) {
            free(path);
            free(source);
            return NULL;
        }
        sprintf(path, "%s/%s", scan->directory, name);
        sprintf(source, "%s/%s%s", scan->directory, folders[scan->stage], entry->d_name);
        struct stat data;
        boolean_t orphan = Ustat(path, &data) < 0 && errno == ENOENT;
        char* selected = NULL;
        if (orphan && catalog_companion(source, info)) {
            selected = ROMlib_sidecar_name(path, strrchr(path, '/') + 1);
            if (selected && scan->stage == 3) {
                /* .finf, if present, was already returned in stage 2. */
                if (Ustat(selected, &data) == 0)
                    orphan = FALSE;
                char* resource = ROMlib_sidecar_resource_name(selected);
                free(selected);
                selected = resource;
            }
            orphan = orphan && selected && !strcmp(selected, source);
        }
        else
            orphan = FALSE;
        free(selected);
        free(path);
        free(source);
        if (orphan) {
            strcpy(scan->name, name);
            info->st_size = 0;
            return scan->name;
        }
    }
    return NULL;
}

/* An anonymous stream supplies ordinary seek/read semantics for an absent
   fork. Its named host representation is created only on nonzero growth.
   Keep host pointers out of the guest-visible FCB layout. */
typedef struct empty_fork {
    struct empty_fork* next;
    fcbrec* fp;
    FILE* stream;
    char* path;
} empty_fork;

static empty_fork* empty_forks;

static boolean_t zero_bytes(const void* bytes, size_t length)
{
    const unsigned char* p = bytes;
    while (length--)
        if (*p++)
            return FALSE;
    return TRUE;
}

/* Only discard representations whose entire payload is known to be empty.
   Unknown descriptors, ATTR data, dates and Finder flags must survive. */
static void discard_empty_metadata(const char* path)
{
    const char* leaf = strrchr(path, '/');
    char* metadata = ROMlib_sidecar_name(path, leaf ? leaf + 1 : path);
    if (!metadata)
        return;
    int fd = Uopen(metadata, O_BINARY | O_RDONLY, 0);
    unsigned char bytes[1024];
    struct stat info;
    boolean_t empty = FALSE;
    if (fd >= 0 && fstat(fd, &info) == 0 && info.st_size <= sizeof bytes) {
        ssize_t size = read(fd, bytes, sizeof bytes);
        if (ROMlib_basilisk_sidecar_p(metadata))
            empty = (size == 16 || size == 18 || size == 32) && zero_bytes(bytes, size);
        else if (size >= sizeof(Single_header)) {
            Single_header head;
            memcpy(&head, bytes, sizeof head);
            unsigned count = CW(head.nentries);
            size_t header = sizeof head + count * sizeof(Single_descriptor);
            empty = head.magic == CLC(DOUBLEMAGIC) && head.version == CLC(SINGLEVERSION) && count && header <= size;
            for (unsigned i = 0; empty && i < count; ++i) {
                Single_descriptor entry;
                memcpy(&entry, bytes + sizeof head + i * sizeof entry, sizeof entry);
                ULONGINT id = CL(entry.id), offset = CL(entry.offset);
                ULONGINT length = CL(entry.length);
                empty = (id == Resource_Fork_ID && !length) ||
                        ((id == Finder_Info_ID || id == File_Dates_Info_ID || id == Macintosh_File_Info_ID) &&
                         offset >= header && offset <= size && length <= size - offset &&
                         zero_bytes(bytes + offset, length));
            }
        }
        Uclose(fd);
        fd = -1;
    }
    if (fd >= 0)
        Uclose(fd);
    if (empty && !ROMlib_empty_fork_busy(metadata)) {
        char* resource = ROMlib_sidecar_resource_name(metadata);
        /* Never delete a separate raw fork containing data. */
        if (!resource || !strcmp(resource, metadata) || (Ustat(resource, &info) < 0 && errno == ENOENT))
            Uunlink(metadata);
        free(resource);
    }
    free(metadata);
}

boolean_t ROMlib_empty_fork_p(fcbrec* fp)
{
    for (empty_fork* fork = empty_forks; fork; fork = fork->next)
        if (fork->fp == fp)
            return TRUE;
    return FALSE;
}

boolean_t ROMlib_empty_fork_busy(const char* path)
{
    for (empty_fork* fork = empty_forks; fork; fork = fork->next)
        if (!strcmp(fork->path, path))
            return TRUE;
    return FALSE;
}

OSErr ROMlib_open_empty_fork(fcbrec* fp, const char* path)
{
    empty_fork* fork = calloc(1, sizeof *fork);
    if (!fork)
        return memFullErr;
    fork->path = strdup(path);
    fork->stream = fork->path ? tmpfile() : NULL;
    if (!fork->stream) {
        free(fork->path);
        free(fork);
        return ioErr;
    }
    fork->fp = fp;
    fork->next = empty_forks;
    empty_forks = fork;
    fp->fcfd = fileno(fork->stream);
    fp->hiddenfd = -1;
    return noErr;
}

void ROMlib_close_empty_fork(fcbrec* fp)
{
    empty_fork** link = &empty_forks;
    while (*link) {
        empty_fork* fork = *link;
        if (fork->fp == fp) {
            *link = fork->next;
            fclose(fork->stream);
            fp->fcfd = -1;
            free(fork->path);
            free(fork);
            return;
        }
        link = &fork->next;
    }
}

static OSErr materialize_fork(fcbrec* fp)
{
    for (empty_fork* fork = empty_forks; fork; fork = fork->next)
        if (fork->fp == fp) {
            LONGINT fd;
            OSErr err = noErr;
            if (!(fp->fcflags & fcwriteperm))
                return wrPermErr;
            if (fp->fcflags & fcfisres) {
                fd = Uopen(fork->path, O_BINARY | O_RDWR, 0);
                struct stat info;
                if (fd >= 0 && fstat(fd, &info) == 0 && !info.st_size && !strstr(fork->path, "/.rsrc/")) {
                    Uclose(fd);
                    fd = -1;
                    errno = ENOENT;
                }
                if (fd < 0)
                    err = errno == ENOENT ? ROMlib_newresfork(fork->path, &fd, TRUE) : ROMlib_maperrno();
            }
            else {
                fd = Uopen(fork->path, O_BINARY | O_RDWR | O_CREAT, 0666);
                if (fd < 0)
                    err = ROMlib_maperrno();
            }
            if (err != noErr)
                return err;
            if (!(fp->fcflags & fcfisres)) {
                struct stat info;
                if (Ustat(fork->path, &info) == 0) {
                    for (unsigned i = 0; i < NFCB; ++i)
                        if (ROMlib_fcblocks[i].fdfnum == fp->fdfnum &&
                            PP_RAW(ROMlib_fcblocks[i].fcvptr) == PP_RAW(fp->fcvptr) && &ROMlib_fcblocks[i] != fp)
                            ROMlib_fcblocks[i].fdfnum = CL((LONGINT)ST_INO(info));
                    fp->fdfnum = CL((LONGINT)ST_INO(info));
                }
            }
            /* Rebind all opens of this absent fork, each with its own mark. */
            for (empty_fork *other = empty_forks, *next; other; other = next) {
                next = other->next;
                if (strcmp(other->path, fork->path))
                    continue;
                fcbrec* target = other->fp;
                if (target == fp)
                    continue;
                int shared = Uopen(other->path, O_BINARY | ((target->fcflags & fcwriteperm) ? O_RDWR : O_RDONLY), 0);
                if (shared < 0) {
                    Uclose(fd);
                    return ROMlib_maperrno();
                }
                LONGINT position = lseek(target->fcfd, 0, L_INCR);
                ROMlib_close_empty_fork(target);
                target->fcfd = shared;
                if ((target->fcflags & fcfisres) && ROMlib_resource_fork_present(shared))
                    target->hiddenfd = shared;
                lseek(shared, ROMlib_FORKOFFSET(target) + position, L_SET);
            }
            if (!(fp->fcflags & fcfisres)) {
                if (fp->hiddenfd >= 0)
                    Uclose(fp->hiddenfd);
                fp->hiddenfd = -1;
                boolean_t resource_open = FALSE;
                for (unsigned i = 0; i < NFCB; ++i)
                    if (ROMlib_fcblocks[i].fdfnum == fp->fdfnum &&
                        PP_RAW(ROMlib_fcblocks[i].fcvptr) == PP_RAW(fp->fcvptr) &&
                        (ROMlib_fcblocks[i].fcflags & fcfisres))
                        resource_open = TRUE;
                if (!resource_open)
                    discard_empty_metadata(fork->path);
            }
            LONGINT position = lseek(fp->fcfd, 0, L_INCR);
            ROMlib_close_empty_fork(fp);
            fp->fcfd = fd;
            if ((fp->fcflags & fcfisres) && ROMlib_resource_fork_present(fd))
                fp->hiddenfd = fd;
            lseek(fd, ROMlib_FORKOFFSET(fp) + position, L_SET);
            return noErr;
        }
    return noErr;
}

#define IDWANTED(fp) ((fp->fcflags & fcfisres) ? Resource_Fork_ID : Data_Fork_ID)

A1(PUBLIC, LONGINT, ROMlib_FORKOFFSET, fcbrec*, fp) /* INTERNAL */
{
    Single_descriptor d;
    Single_ID idwanted;

    if (fp->fcfd != fp->hiddenfd)
        /*-->*/ return 0L;
    idwanted = IDWANTED(fp);
    int slot = fork_offset_slot(fp);
    if (slot >= 0 && fork_offsets[slot].valid && fork_offsets[slot].fd == fp->fcfd &&
        fork_offsets[slot].hiddenfd == fp->hiddenfd && fork_offsets[slot].file_id == fp->fdfnum &&
        fork_offsets[slot].volume == fp->fcvptr.pp && fork_offsets[slot].resource == (fp->fcflags & fcfisres))
        return fork_offsets[slot].offset;
    if (!getsetentry(Get, fp->fcfd, idwanted, &d, NULL))
        return RESOURCEPREAMBLE;
    if (slot >= 0) {
        fork_offsets[slot].valid = TRUE;
        fork_offsets[slot].fd = fp->fcfd;
        fork_offsets[slot].hiddenfd = fp->hiddenfd;
        fork_offsets[slot].file_id = fp->fdfnum;
        fork_offsets[slot].volume = fp->fcvptr.pp;
        fork_offsets[slot].resource = fp->fcflags & fcfisres;
        fork_offsets[slot].offset = CL(d.offset);
    }
    return CL(d.offset);
}

A2(PRIVATE, void, writebyteat, LONGINT, fd, LONGINT, loc)
{
    LONGINT saveloc;

    saveloc = lseek(fd, 0L, L_INCR);
    lseek(fd, loc, L_SET);
    write(fd, "", 1);
    lseek(fd, saveloc, L_SET);
}

/* TODO: better error checking */

/* AppleDouble descriptors need not be sorted. When metadata follows the
 * resource fork, grow it at the end instead of overwriting that metadata.
 */
PRIVATE boolean_t grow_resource_entry(LONGINT fd, Single_descriptor* entry, ULONGINT length)
{
    struct stat sb;
    unsigned char buffer[8192];
    ULONGINT copied = 0, old_offset = CL(entry->offset);
    ULONGINT old_length = CL(entry->length), new_offset;
    LONGINT position = lseek(fd, 0, L_INCR);

    if (fstat(fd, &sb) < 0 || sb.st_size > 0x7FFFFFFF - length)
        return FALSE;
    new_offset = sb.st_size;
    while (copied < old_length) {
        ULONGINT amount = MIN(sizeof(buffer), old_length - copied);
        if (lseek(fd, old_offset + copied, L_SET) < 0 || read(fd, buffer, amount) != amount ||
            lseek(fd, new_offset + copied, L_SET) < 0 || write(fd, buffer, amount) != amount)
            goto fail;
        copied += amount;
    }
    if (ftruncate(fd, new_offset + length) < 0)
        goto fail;
    entry->offset = CL(new_offset);
    entry->length = CL(length);
    if (!getsetentry(Set, fd, Resource_Fork_ID, entry, NULL))
        goto fail;
    lseek(fd, new_offset + position - old_offset, L_SET);
    return TRUE;
fail:
    lseek(fd, position, L_SET);
    return FALSE;
}

A1(PUBLIC, OSErr, ROMlib_seteof, fcbrec*, fp) /* INTERNAL */
{
    ULONGINT leof, peof, curloc;
    LONGINT fd;
    Single_descriptor d;
    Single_ID idwanted;
    INTEGER i;
    OSErr err;

    leof = Cx(fp->fcleof);
    if (leof && (err = materialize_fork(fp)) != noErr)
        return err;
    fd = fp->fcfd;
    err = noErr;
    if (fd == fp->hiddenfd) { /* mixed file */
        idwanted = IDWANTED(fp);
        if (getsetentry(Get, fd, idwanted, &d, &peof)) {
            if (leof > peof) {
                if (idwanted != Resource_Fork_ID || !grow_resource_entry(fd, &d, leof))
                    err = ioErr;
                else
                    peof = 0x7FFFFFFF;
            }
            else {
                struct stat sb;
                ULONGINT end = CL(d.offset) + leof;

                /* PBAllocate/PBSetEOF precede the copy's first write. Extend
		   the host file before publishing the larger fork length;
		   otherwise the next descriptor read rejects its payload. */
                if (fstat(fd, &sb) < 0 || (end > (ULONGINT)sb.st_size && ftruncate(fd, end) < 0))
                    err = ROMlib_maperrno();
                else {
                    d.length = CL(leof);
                    if (!getsetentry(Set, fd, idwanted, &d, NULL))
                        err = ioErr;
                }
            }
        }
        else {
            err = fsDSIntErr;
            warning_unexpected("getsetentry (Get, %d, %d) failed", fd, idwanted);
        }
    }
    else { /* pure file */
        peof = Cx(fp->fcPLen);
        if (leof > peof)
            writebyteat(fd, leof - 1);
        else if (leof < peof) {
            curloc = lseek(fd, 0L, L_INCR);
            ftruncate(fd, leof);
            if (curloc > leof)
                lseek(fd, leof, L_SET);
        }
        peof = leof;
    }
    if (err == noErr) {
        for (i = 0; i < NFCB; i++) {
            if (ROMlib_fcblocks[i].fdfnum == fp->fdfnum && PP_RAW(ROMlib_fcblocks[i].fcvptr) == PP_RAW(fp->fcvptr) &&
                (ROMlib_fcblocks[i].fcflags & fcfisres) == (fp->fcflags & fcfisres)) {
                ROMlib_fcblocks[i].fcleof = CL(leof);
                ROMlib_fcblocks[i].fcPLen = CL(peof);
            }
        }
    }
    fs_err_hook(err);
    return err;
}

A5(PRIVATE, BOOLEAN, getsetpiece, GetOrSetType, gors, LONGINT, fd, Single_descriptor*, sdp, char*, bufp, LONGINT,
   length)
{
    LONGINT saveloc;
    BOOLEAN retval;

    saveloc = lseek(fd, 0L, L_INCR);
    /* Compact Finder records must not spill into adjacent entries. */
    if (gors == Get)
        memset(bufp, 0, length);
    length = MIN(length, CL(sdp->length));
    lseek(fd, Cx(sdp->offset), L_SET);
    switch (gors) {
    case Get:
        retval = read(fd, bufp, length) == length;
        break;
    case Set:
        retval = write(fd, bufp, length) == length;
        break;
    default:
        retval = FALSE;
        gui_assert(0);
        break;
    }
    lseek(fd, saveloc, L_SET);
    return retval;
}

A1(PUBLIC, OSErr, ROMlib_geteofostype, fcbrec*, fp) /* INTERNAL */
{
    LONGINT fd;
    Single_descriptor d;
    Single_ID idwanted;
    Single_finfo finfo;
    OSErr err;
    struct stat sbuf;

    ROMlib_forget_fork_offset(fp);
    fd = fp->hiddenfd;
    if (fstat(fp->fcfd, &sbuf) < 0)
        err = ROMlib_maperrno();
    else {
        err = noErr;
        if (fd == fp->fcfd) { /* mixed file */
            idwanted = IDWANTED(fp);
            if (!getsetentry(Get, fd, idwanted, &d, NULL))
                err = fp->fcPLen = 0;
            else
                fp->fcPLen = fp->fcleof = d.length;
        }
        else
            fp->fcleof = fp->fcPLen = CL(sbuf.st_size);
        if (err == noErr) {
            if (!getsetentry(Get, fd, Finder_Info_ID, &d, NULL) ||
                (!getsetpiece(Get, fd, &d, (char*)&finfo, sizeof(finfo)))) {
                uint32 type;

                if (ROMlib_creator_and_type_from_filename(fp->fcname[0], (char*)fp->fcname + 1, NULL, &type))
                    fp->fcbFType = CL(type);
                else
                    fp->fcbFType = TICKX("TEXT");
            }
            else
                fp->fcbFType = finfo.finfo.fdType;
        }
    }
    fs_err_hook(err);
    return err;
}

A8(PUBLIC, OSErr, ROMlib_hiddenbyname, GetOrSetType, gors, /* INTERNAL */
   char*, pathname, char*, rpathname, Single_dates*, datep, FInfo*, finfop, FXInfo*, fxinfop, LONGINT*, lenp, LONGINT*,
   rlenp)
{
    LONGINT rfd;
    struct stat sbuf;
    Single_finfo sfinfo;
    Single_descriptor d;
    OSErr retval;
    BOOLEAN done;

    retval = noErr;
    memset(&sfinfo, 0, sizeof(sfinfo));
    if (gors == Get) {
        memset(finfop, 0, sizeof(*finfop));
        memset(fxinfop, 0, sizeof(*fxinfop));
    }
    if (ROMlib_ufs_stat(pathname, &sbuf) < 0)
        retval = ROMlib_maperrno();
    else {
        done = FALSE;
        rfd = Uopen(rpathname, O_BINARY | (gors == Set ? O_RDWR : O_RDONLY), 0);
        /*
if (rfd == -1)
fprintf(stderr, "%s(%d): open '%s' fails\n", __FILE__, __LINE__, rpathname);
*/
        if (rfd < 0) {
            done = TRUE;
            if (errno == ENOENT) {
                /* no resource fork (or AppleSingle) */
                /* right now we ignore that it could be AppleSingle */
                switch (gors) {
                case Get:
                    memset(datep, 0, sizeof(*datep));
                    memset(finfop, 0, sizeof(*finfop));
                    memset(fxinfop, 0, sizeof(*fxinfop));
                    *lenp = CL(sbuf.st_size);
                    *rlenp = 0;
                    datep->crdat = CL(UNIXTIMETOMACTIME(MIN(sbuf.st_ctime, sbuf.st_mtime)));
                    datep->moddat = CL(UNIXTIMETOMACTIME(sbuf.st_mtime));
                    if (ROMlib_basilisk_sidecar_p(rpathname)) {
                        char* resource = ROMlib_sidecar_resource_name(rpathname);
                        struct stat resource_stat;
                        if (Ustat(resource, &resource_stat) == 0)
                            *rlenp = CL(resource_stat.st_size);
                        free(resource);
                    }
                    break;
                case Set:
                    if ((!finfop || zero_bytes(finfop, sizeof *finfop)) &&
                        (!fxinfop || zero_bytes(fxinfop, sizeof *fxinfop)) &&
                        (!datep || zero_bytes(datep, sizeof *datep)))
                        break;
                    retval = ROMlib_newresfork(rpathname, &rfd, FALSE);
                    done = FALSE;
                    break;
                default:
                    gui_assert(0);
                    break;
                }
            }
            else
                retval = ROMlib_maperrno();
        }
        if (!done && retval == noErr) {
            switch (gors) {
            case Get:
                *lenp = CL(sbuf.st_size);
                if (getsetentry(Get, rfd, File_Dates_Info_ID, &d, NULL))
                    getsetpiece(Get, rfd, &d, (char*)datep, sizeof(*datep));
                else {
                    datep->crdat = CL(UNIXTIMETOMACTIME(MIN(sbuf.st_ctime, sbuf.st_mtime)));
                    datep->moddat = CL(UNIXTIMETOMACTIME(sbuf.st_mtime));
                    datep->accessdat = CL(UNIXTIMETOMACTIME(sbuf.st_atime));
                    datep->backupdat = 0;
                }

                if (!getsetentry(Get, rfd, Finder_Info_ID, &d, NULL))
                    warning_unexpected("no finder info");
                else {
                    getsetpiece(Get, rfd, &d, (char*)&sfinfo, sizeof(sfinfo));

                    if (finfop)
                        *finfop = sfinfo.finfo;
                    if (fxinfop)
                        *fxinfop = sfinfo.fxinfo;

                    *lenp = CL(sbuf.st_size);
                }
                if (getsetentry(Get, rfd, Resource_Fork_ID, &d, NULL))
                    *rlenp = d.length;
                else
                    *rlenp = 0;
                if (ROMlib_basilisk_sidecar_p(rpathname)) {
                    char* resource = ROMlib_sidecar_resource_name(rpathname);
                    struct stat resource_stat;
                    *rlenp = resource && Ustat(resource, &resource_stat) == 0 ? CL(resource_stat.st_size) : 0;
                    free(resource);
                }
                break;
            case Set:
                if (getsetentry(Get, rfd, File_Dates_Info_ID, &d, NULL))
                    getsetpiece(Set, rfd, &d, (char*)datep, sizeof(*datep));

                if (!getsetentry(Get, rfd, Finder_Info_ID, &d, NULL))
                    warning_unexpected("no finfo");
                else {
                    if (!finfop || !fxinfop) {
                        sfinfo.finfo.fdCreator = TICKX("UNIX");
                        sfinfo.finfo.fdType = TICKX("TEXT");
                        getsetpiece(Get, rfd, &d, (char*)&sfinfo, sizeof(sfinfo));
                    }

                    if (finfop)
                        sfinfo.finfo = *finfop;
                    if (fxinfop)
                        sfinfo.fxinfo = *fxinfop;
                    getsetpiece(Set, rfd, &d, (char*)&sfinfo, sizeof(sfinfo));
                }
                break;
            default:
                gui_assert(0);
                break;
            }
        }
        if (rfd >= 0)
            Uclose(rfd);
    }
    fs_err_hook(retval);
    return retval;
}
