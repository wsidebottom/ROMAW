/*+===================================================================
  File:      fs_refs.c

  Summary:   Implements Carbon file references, catalog lookups and volume queries.

  Classes:   None.

  Functions: ROMlib_carbon_fs_alias, ROMlib_carbon_fs_ref, ROMlib_carbon_fs_ref_parameter_block,
             ROMlib_carbon_fs_resource.

  Origin:    CarbonLib 1.4 File Manager, System 9.2.1 InterfaceLib overlay and
             Universal Interfaces 3.4.2 Files.h. MacRoman data comes from the
             disk-image catalog implementation.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "fs_refs.h"
#include "rsys/ecf.h"
#include "rsys/file.h"
#include "rsys/hfs.h"
#include "rsys/host_names.h"
#include "winfs.h"
#include "AliasMgr.h"
#include "ResourceMgr.h"
#include "rsys/resource.h"
#include "rsys/system_resources.h"

enum {
    fsref_stamp = 0x45584652, /* EXFR. The public FSRef bytes are opaque. */
    fsref_bad = -1401,
    fsref_bad_fork_name = -1402,
    fsref_bad_buffer = -1403,
    fsref_bad_fork = -1404,
    fsref_bad_bitmap = -1405,
    fsref_missing_info = -1406,
    fsref_not_folder = -1407,
    fsref_fork_not_found = -1409,
    fsref_name_too_long = -1410,
    fsref_missing_name = -1411,
    fsref_bad_position_mode = -1412,
    fsref_bad_allocation_flags = -1413,
    fsref_no_more_items = -1417,
    fsref_bad_item_count = -1418,
    fsref_different = -1420,
    fsref_fork_exists = -1421,
    fsref_bad_iterator_flags = -1422,
    fsref_iterator_not_found = -1423,
    fsref_iterator_not_supported = -1424,
    fsref_buffers_too_small = -210,
    fsref_path_too_long = -2110,
    fsref_path_limit = INT16_MAX, /* CarbonLib passes the input byte count as a signed short. */
    fsref_directory = 0x10,
    fsref_host_identity = 0x8000,
};

typedef struct PACKED {
    uint32_t stamp;
    int16_t volume;
    uint16_t flags;
    uint32_t parent;
    uint32_t node;
    Byte name[52];
    ROMlib_host_file_id identity;
} carbon_file_ref;

typedef struct PACKED {
    uint16_t high;
    uint32_t low;
    uint16_t fraction;
} carbon_file_date;

typedef struct PACKED {
    uint32_t high;
    uint32_t low;
} carbon_file_size;

typedef struct PACKED {
    uint16_t length;
    uint16_t characters[255];
} carbon_file_name;

typedef struct PACKED {
    uint16_t flags;
    int16_t volume;
    uint32_t parent;
    uint32_t node;
    uint8_t share_flags;
    uint8_t privileges;
    uint8_t reserved[2];
    carbon_file_date created;
    carbon_file_date modified;
    carbon_file_date attribute_modified;
    carbon_file_date accessed;
    carbon_file_date backed_up;
    uint32_t permissions[4];
    uint8_t finder[16];
    uint8_t extended[16];
    carbon_file_size data_logical_size;
    carbon_file_size data_physical_size;
    carbon_file_size resource_logical_size;
    carbon_file_size resource_physical_size;
    uint32_t children;
    uint32_t encoding;
} carbon_catalog_info;

typedef struct PACKED {
    carbon_file_date created;
    carbon_file_date modified;
    carbon_file_date backed_up;
    carbon_file_date checked;
    uint32_t file_count;
    uint32_t folder_count;
    carbon_file_size total_bytes;
    carbon_file_size free_bytes;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t next_block;
    uint32_t resource_clump;
    uint32_t data_clump;
    uint32_t next_id;
    uint8_t finder[32];
    uint16_t flags;
    uint16_t filesystem;
    uint16_t signature;
    uint16_t drive;
    int16_t driver;
} carbon_volume_info;

typedef struct PACKED {
    COMMONFSQUEUEDEFS;
    uint32_t unused;
    uint32_t ref;
    uint32_t fields;
    uint32_t info;
    uint32_t name_length;
    uint32_t name;
    uint32_t directory;
    uint32_t spec;
    uint32_t parent;
    uint32_t result;
    uint32_t encoding;
    uint32_t output_name;
} carbon_catalog_pb;

typedef struct PACKED {
    COMMONFSQUEUEDEFS;
    uint32_t index;
    uint32_t fields;
    uint32_t info;
    uint32_t name;
    uint32_t ref;
} carbon_volume_pb;

typedef struct PACKED {
    HVolumeParam volume;
    carbon_file_size total;
    carbon_file_size free;
} carbon_extended_volume_pb;

typedef struct PACKED {
    COMMONFSQUEUEDEFS;
    int16_t fork;
    uint8_t reserved;
    int8_t permissions;
    uint32_t ref;
    uint32_t buffer;
    uint32_t requested;
    uint32_t transferred;
    uint16_t position_mode;
    carbon_file_size position;
    uint16_t allocation_flags;
    carbon_file_size allocation;
    uint32_t name_length;
    uint32_t name;
    uint8_t iterator[16];
    uint32_t output_name;
} carbon_fork_pb;

typedef struct PACKED {
    int8_t flags;
    int8_t permissions;
    int16_t volume;
    uint32_t reserved;
    uint32_t node;
    uint32_t fork;
    carbon_file_size position;
    carbon_file_size logical_end;
    carbon_file_size physical_end;
    ProcessSerialNumber process;
} carbon_fork_info;

typedef struct PACKED {
    PACKED_MEMBER(QElemPtr, qLink);
    int16_t qType;
    int16_t ioTrap;
    PACKED_MEMBER(Ptr, ioCmdAddr);
    PACKED_MEMBER(ProcPtr, ioCompletion);
    OSErr ioResult;
    int16_t desired;
    int16_t volume;
    int16_t iterator;
    int16_t actual;
    uint32_t ref;
    uint32_t info;
    uint32_t name;
} carbon_fork_info_pb;

typedef struct PACKED {
    PACKED_MEMBER(QElemPtr, qLink);
    int16_t qType;
    int16_t ioTrap;
    PACKED_MEMBER(Ptr, ioCmdAddr);
    PACKED_MEMBER(ProcPtr, ioCompletion);
    OSErr ioResult;
    uint8_t changed;
    uint8_t reserved;
    uint32_t flags;
    uint32_t iterator;
    uint32_t container;
    uint32_t maximum;
    uint32_t actual;
    uint32_t fields;
    uint32_t info;
    uint32_t refs;
    uint32_t specs;
    uint32_t names;
    uint32_t search;
} carbon_bulk_pb;

typedef struct {
    uint32_t stamp;
    uint32_t index;
    uint32_t modified;
    uint16_t count;
    carbon_file_ref directory;
} carbon_catalog_iterator;

_Static_assert(sizeof(carbon_file_ref) == 80 && sizeof(carbon_catalog_info) == 144 &&
                   offsetof(carbon_catalog_pb, ref) == 28 && sizeof(carbon_catalog_pb) == 72,
               "FSRef and catalog parameter block ABI");
_Static_assert(sizeof(carbon_file_name) == 512 && sizeof(carbon_volume_info) == 126 &&
                   offsetof(carbon_volume_pb, index) == 24 && sizeof(carbon_volume_pb) == 44,
               "Volume parameter block ABI");
_Static_assert(offsetof(carbon_fork_pb, position) == 46 && offsetof(carbon_fork_pb, name) == 68 &&
                   sizeof(carbon_fork_pb) == 92,
               "Fork I/O parameter block ABI");
_Static_assert(sizeof(carbon_fork_info) == 48 && sizeof(carbon_fork_info_pb) == 38 && sizeof(carbon_bulk_pb) == 64,
               "Fork and bulk catalog info parameter block ABI");

static const struct {
    uint16_t value;
    uint16_t base;
    uint16_t accent;
} roman[128] = {

#include "disk-images/src/macroman.inc"
};

static void* fs_memory(uint32_t address, size_t extent, int write)
{
    return address ? ROMlib_carbon_try_guest_memory(address, extent, write) : NULL;
}

static carbon_file_size file_size(uint64_t value)
{
    return (carbon_file_size){CL((uint32_t)(value >> 32)), CL((uint32_t)value)};
}

static uint64_t read_file_size(carbon_file_size value)
{
    return ((uint64_t)CL(value.high) << 32) | CL(value.low);
}

static const Byte resource_fork_name[] = {13, 'R', 'E', 'S', 'O', 'U', 'R', 'C', 'E', '_', 'F', 'O', 'R', 'K'};

static void copy_unicode_name(carbon_file_name* output, const Byte* name)
{
    output->length = CW((uint16_t)name[0]);
    for (unsigned i = 0; i < name[0]; ++i) {
        const unsigned byte = name[i + 1];
        output->characters[i] = CW((uint16_t)(byte < 128 ? byte : roman[byte - 128].value));
    }
}

static OSErr copy_mac_name(Str255 output, uint32_t address, uint32_t length)
{
    if (!address || !length)
        return fsref_missing_name;
    if (length > 255)
        return fsref_name_too_long;
    const uint16_t* input = fs_memory(address, length * sizeof *input, 0);
    if (!input)
        return paramErr;

    unsigned bytes = 0;
    for (unsigned i = 0; i < length; ++i) {
        uint16_t unit;
        memcpy(&unit, input + i, sizeof unit);
        unit = CW(unit);
        if (!unit || unit == ':')
            return bdNamErr;
        uint16_t combining = 0;
        if (i + 1 < length) {
            memcpy(&combining, input + i + 1, sizeof combining);
            combining = CW(combining);
        }
        unsigned result = unit < 128 ? unit : UINT16_MAX;
        if (combining >= 0x300 || unit >= 128) {
            for (unsigned j = 0; j < sizeof roman / sizeof roman[0]; ++j) {
                if (roman[j].base == unit && roman[j].accent && roman[j].accent == combining) {
                    result = j + 128;
                    ++i;
                    break;
                }
                if (roman[j].value == unit) {
                    result = j + 128;
                    break;
                }
            }
        }
        if (result > UINT8_MAX)
            return bdNamErr;
        output[++bytes] = (Byte)result;
    }
    output[0] = bytes;
    return noErr;
}

static OSErr get_catalog(int16_t volume, uint32_t directory, int16_t index, Str255 name, CInfoPBRec* catalog)
{
    *catalog = (CInfoPBRec){0};
    HFileInfo* file = &catalog->hFileInfo;
    file->ioVRefNum = volume;
    file->ioDirID = directory;
    file->ioFDirIndex = CW(index);
    PP_RAW(file->ioNamePtr) = CL(US_TO_SYN68K(name));
    return PBGetCatInfo(catalog, FALSE);
}

static int is_directory(const CInfoPBRec* catalog)
{
    return (catalog->hFileInfo.ioFlAttrib & ATTRIB_ISADIR) != 0;
}

static int catalog_identity(int16_t volume, uint32_t directory, const Byte* name, ROMlib_host_file_id* id)
{
    FSSpec spec = {.vRefNum = volume, .parID = directory};
    if (name[0] >= sizeof spec.name)
        return 0;
    memcpy(spec.name, name, (size_t)name[0] + 1);
    char* path = NULL;
    const int known = ROMlib_filename_from_fsspec(&path, &spec) == noErr && ROMlib_host_get_file_id(path, id);
    free(path);
    return known;
}

static ROMlib_host_file_id ref_identity(const carbon_file_ref* ref)
{
    return (ROMlib_host_file_id){CL(ref->identity.volume_serial), CL(ref->identity.index_high),
                                 CL(ref->identity.index_low)};
}

static int matches_identity(const carbon_file_ref* ref, const CInfoPBRec* catalog, const Byte* name)
{
    if (!(CW(ref->flags) & fsref_host_identity))
        return ref->node == catalog->hFileInfo.ioDirID;
    const uint32_t parent = is_directory(catalog) ? catalog->dirInfo.ioDrParID : catalog->hFileInfo.ioFlParID;
    ROMlib_host_file_id id;
    if (!catalog_identity(ref->volume, parent, name, &id))
        return 0;
    const ROMlib_host_file_id expected = ref_identity(ref);
    return id.volume_serial == expected.volume_serial && id.index_high == expected.index_high &&
           id.index_low == expected.index_low;
}

static void store_ref(carbon_file_ref* output, int16_t volume, const CInfoPBRec* catalog, const Byte* name)
{
    carbon_file_ref ref = {
        .stamp = CL((uint32_t)fsref_stamp),
        .volume = volume,
        .flags = CW((uint16_t)(is_directory(catalog) ? fsref_directory : 0)),
        .parent = is_directory(catalog) ? catalog->dirInfo.ioDrParID : catalog->hFileInfo.ioFlParID,
        .node = catalog->hFileInfo.ioDirID,
    };
    ROMlib_host_file_id id;
    if (catalog_identity(volume, ref.parent, name, &id)) {
        ref.flags = CW((uint16_t)(CW(ref.flags) | fsref_host_identity));
        ref.identity = (ROMlib_host_file_id){CL(id.volume_serial), CL(id.index_high), CL(id.index_low)};
    }
    memcpy(ref.name, name, (size_t)name[0] + 1);
    *output = ref;
}

static OSErr make_directory_ref(int16_t volume, uint32_t directory, carbon_file_ref* output)
{
    _Alignas(256) Str255 name = {0};
    _Alignas(128) CInfoPBRec catalog;
    const OSErr error = get_catalog(volume, directory, -1, name, &catalog);
    if (error)
        return error;
    if (!is_directory(&catalog))
        return dirNFErr;
    if (name[0] >= sizeof output->name)
        return bdNamErr;
    store_ref(output, volume, &catalog, name);
    return noErr;
}

static OSErr make_ref(int16_t volume, uint32_t directory, const Byte* pathname, carbon_file_ref* output)
{
    _Alignas(256) Str255 name = {0};
    _Alignas(128) FSSpec spec = {0};
    if (pathname)
        memcpy(name, pathname, (size_t)pathname[0] + 1);
    OSErr error = C_FSMakeFSSpec(CW(volume), CL(directory), name, &spec);
    if (error)
        return error;
    if (spec.parID == CL((uint32_t)1))
        return make_directory_ref(spec.vRefNum, CL((uint32_t)2), output);
    if (spec.name[0] >= sizeof output->name)
        return fsref_name_too_long;

    memcpy(name, spec.name, (size_t)spec.name[0] + 1);
    _Alignas(128) CInfoPBRec catalog;
    error = get_catalog(spec.vRefNum, spec.parID, 0, name, &catalog);
    if (!error)
        store_ref(output, spec.vRefNum, &catalog, spec.name);
    return error;
}

static OSErr resolve_ref(const carbon_file_ref* ref, CInfoPBRec* catalog, Str255 name)
{
    if (ref->name[0] >= sizeof ref->name || !ref->node || !ref->volume)
        return fsref_bad;
    const int folder = (CW(ref->flags) & fsref_directory) != 0;
    if (!folder)
        memcpy(name, ref->name, (size_t)ref->name[0] + 1);
    const OSErr lookup_error =
        get_catalog(ref->volume, folder ? ref->node : ref->parent, folder ? -1 : 0, name, catalog);
    if (!lookup_error && is_directory(catalog) == folder && matches_identity(ref, catalog, name))
        return noErr;

    if (CW(ref->flags) & fsref_host_identity) {
        const VCBExtra* volume = (const VCBExtra*)ROMlib_vcbbyvrn(CW(ref->volume));
        if (volume && !volume->vcb.vcbCTRef) {
            const ROMlib_host_file_id id = ref_identity(ref);
            char* path = ROMlib_host_locate_file_id(volume->unixname, &id);
            if (path) {
                _Alignas(128) FSSpec spec = {0};
                const OSErr error = ROMlib_host_fsspec(path, &spec);
                free(path);
                if (!error && spec.vRefNum == ref->volume) {
                    memcpy(name, spec.name, (size_t)spec.name[0] + 1);
                    return get_catalog(spec.vRefNum, spec.parID, 0, name, catalog);
                }
            }
        }
    }
    if (folder || (lookup_error && lookup_error != fnfErr))
        return lookup_error ? lookup_error : fnfErr;

    /* Names can change while the catalog ID remains the same. */
    for (int index = 1; index <= INT16_MAX; ++index) {
        const OSErr error = get_catalog(ref->volume, ref->parent, index, name, catalog);
        if (error)
            return error;
        if (!is_directory(catalog) && matches_identity(ref, catalog, name))
            return noErr;
    }
    return fnfErr;
}

static void copy_catalog_info(carbon_catalog_info* output, uint32_t fields, const carbon_file_ref* ref,
                              const CInfoPBRec* catalog)
{
    const HFileInfo* file = &catalog->hFileInfo;
    const DirInfo* directory = &catalog->dirInfo;
    const int folder = is_directory(catalog);
    if (fields & 0x0001)
        output->encoding = 0;
    if (fields & 0x0002)
        output->flags = CW((uint16_t)(file->ioFlAttrib & 0xDD));
    if (fields & 0x0004)
        output->volume = ref->volume;
    if (fields & 0x0008)
        output->parent = folder ? directory->ioDrParID : file->ioFlParID;
    if (fields & 0x0010)
        output->node = file->ioDirID;
    if (fields & 0x0020)
        output->created = (carbon_file_date){.low = file->ioFlCrDat};
    if (fields & 0x0040)
        output->modified = (carbon_file_date){.low = file->ioFlMdDat};
    if (fields & 0x0080)
        output->attribute_modified = (carbon_file_date){0};
    if (fields & 0x0100)
        output->accessed = (carbon_file_date){0};
    if (fields & 0x0200)
        output->backed_up = (carbon_file_date){.low = file->ioFlBkDat};
    if (fields & 0x0400)
        memset(output->permissions, 0, sizeof output->permissions);
    if (fields & 0x0800)
        memcpy(output->finder, &file->ioFlFndrInfo, sizeof output->finder);
    if (fields & 0x1000)
        memcpy(output->extended, &file->ioFlXFndrInfo, sizeof output->extended);
    if (fields & 0x2000)
        output->children = CL((uint32_t)(folder ? CW(directory->ioDrNmFls) : 0));
    if (fields & 0x4000) {
        output->data_logical_size = file_size(folder ? 0 : (uint32_t)CL(file->ioFlLgLen));
        output->data_physical_size = file_size(folder ? 0 : (uint32_t)CL(file->ioFlPyLen));
    }
    if (fields & 0x8000) {
        output->resource_logical_size = file_size(folder ? 0 : (uint32_t)CL(file->ioFlRLgLen));
        output->resource_physical_size = file_size(folder ? 0 : (uint32_t)CL(file->ioFlRPyLen));
    }
    if (fields & 0x10000)
        output->share_flags = folder ? file->ioFlAttrib & 0x28 : 0;
    if (fields & 0x20000)
        output->privileges = file->ioACUser;
}

static OSErr read_catalog(const carbon_file_ref* ref, uint32_t fields, carbon_catalog_info* info,
                          carbon_file_name* output_name, FSSpec* spec, carbon_file_ref* parent)
{
    _Alignas(256) Str255 name = {0};
    _Alignas(128) CInfoPBRec catalog;
    const OSErr error = resolve_ref(ref, &catalog, name);
    if (error)
        return error;

    const uint32_t parent_id = is_directory(&catalog) ? catalog.dirInfo.ioDrParID : catalog.hFileInfo.ioFlParID;
    if (parent) {
        if (parent_id == CL((uint32_t)1))
            memset(parent, 0, sizeof *parent);
        else {
            const OSErr parent_error = make_directory_ref(ref->volume, parent_id, parent);
            if (parent_error)
                return parent_error;
        }
    }
    if (info)
        copy_catalog_info(info, fields, ref, &catalog);
    if (output_name)
        copy_unicode_name(output_name, name);
    if (spec) {
        if (name[0] > 63)
            return bdNamErr;
        spec->vRefNum = ref->volume;
        spec->parID = parent_id;
        memset(spec->name, 0, sizeof spec->name);
        memcpy(spec->name, name, (size_t)name[0] + 1);
    }
    return noErr;
}

static OSErr set_catalog(const carbon_file_ref* ref, CInfoPBRec* catalog, Str255 name, uint32_t fields,
                         const carbon_catalog_info* info)
{
    if (fields & ~0x1FE3u)
        return fsref_bad_bitmap;
    if (!fields)
        return noErr;
    if (!info)
        return fsref_missing_info;

    HFileInfo* file = &catalog->hFileInfo;
    const uint32_t directory = is_directory(catalog) ? catalog->dirInfo.ioDrParID : file->ioFlParID;
    const int was_locked = (file->ioFlAttrib & ATTRIB_ISLOCKED) != 0;
    if (fields & 0x0020)
        file->ioFlCrDat = info->created.low;
    if (fields & 0x0040)
        file->ioFlMdDat = info->modified.low;
    if (fields & 0x0200)
        file->ioFlBkDat = info->backed_up.low;
    if (fields & 0x0800)
        memcpy(&file->ioFlFndrInfo, info->finder, sizeof info->finder);
    if (fields & 0x1000)
        memcpy(&file->ioFlXFndrInfo, info->extended, sizeof info->extended);
    file->ioDirID = directory;
    file->ioFDirIndex = 0;
    if (fields & 0x1a60u) {
        const OSErr error = PBSetCatInfo(catalog, FALSE);
        if (error)
            return error;
    }
    const int lock = (CW(info->flags) & 1) != 0;
    if ((fields & 0x0002) && lock != was_locked) {
        _Alignas(128) HParamBlockRec block = {0};
        PP_RAW(block.ioParam.ioNamePtr) = CL(US_TO_SYN68K(name));
        block.ioParam.ioVRefNum = ref->volume;
        block.fileParam.ioDirID = directory;
        return lock ? PBHSetFLock(&block, FALSE) : PBHRstFLock(&block, FALSE);
    }
    return noErr;
}

static OSErr update_catalog(carbon_catalog_pb* request, const carbon_file_ref* ref, enum carbon_fs_operation operation)
{
    _Alignas(256) Str255 name = {0}, new_name = {0};
    _Alignas(128) CInfoPBRec catalog;
    OSErr error = resolve_ref(ref, &catalog, name);
    if (error)
        return error;
    const uint32_t directory = is_directory(&catalog) ? catalog.dirInfo.ioDrParID : catalog.hFileInfo.ioFlParID;
    const uint32_t node = catalog.hFileInfo.ioDirID;
    carbon_file_ref* result = fs_memory(CL(request->result), sizeof *result, 1);
    if (request->result && !result)
        return paramErr;
    if (operation == carbon_fs_set_catalog) {
        const carbon_catalog_info* info = fs_memory(CL(request->info), sizeof *info, 0);
        if (request->info && !info)
            return paramErr;
        return set_catalog(ref, &catalog, name, CL(request->fields), info);
    }

    _Alignas(128) HParamBlockRec block = {0};
    block.fileParam.ioVRefNum = ref->volume;
    block.fileParam.ioDirID = directory;
    PP_RAW(block.ioParam.ioNamePtr) = CL(US_TO_SYN68K(name));
    if (operation == carbon_fs_delete_object)
        return PBHDelete(&block, FALSE);
    if (operation == carbon_fs_move_object) {
        const carbon_file_ref* target = fs_memory(CL(request->parent), sizeof *target, 0);
        if (!target || target->stamp != CL((uint32_t)fsref_stamp))
            return fsref_bad;
        if (ref->volume != target->volume)
            return diffVolErr;
        if (!(CW(target->flags) & fsref_directory))
            return fsref_not_folder;
        error = resolve_ref(target, &catalog, new_name);
        if (error)
            return error;
        const uint32_t target_directory = catalog.dirInfo.ioDrDirID;
        _Alignas(128) CMovePBRec move = {0};
        move.ioVRefNum = ref->volume;
        move.ioDirID = directory;
        move.ioNewDirID = target_directory;
        PP_RAW(move.ioNamePtr) = CL(US_TO_SYN68K(name));
        error = PBCatMove(&move, FALSE);
        if (error || !result)
            return error;
        return make_ref(ref->volume, target_directory, name, result);
    }

    error = copy_mac_name(new_name, CL(request->name), CL(request->name_length));
    if (error)
        return error;
    if (new_name[0] > 31)
        return fsref_name_too_long;
    if (operation == carbon_fs_rename_object) {
        block.ioParam.ioMisc = CL(US_TO_SYN68K(new_name));
        error = PBHRename(&block, FALSE);
        if (error || !result)
            return error;
        return make_ref(ref->volume, directory, new_name, result);
    }

    if (!is_directory(&catalog))
        return fsref_not_folder;
    if (operation == carbon_fs_make_ref_unicode)
        return result ? make_ref(ref->volume, node, new_name, result) : paramErr;

    const uint32_t fields = CL(request->fields);
    const carbon_catalog_info* info = fs_memory(CL(request->info), sizeof *info, 0);
    FSSpec* spec = fs_memory(CL(request->spec), sizeof *spec, 1);
    if ((request->info && !info) || (request->spec && !spec))
        return paramErr;
    if (fields & ~0x1FE3u)
        return fsref_bad_bitmap;
    if (fields && !info)
        return fsref_missing_info;

    block.fileParam.ioDirID = node;
    PP_RAW(block.ioParam.ioNamePtr) = CL(US_TO_SYN68K(new_name));
    if (operation == carbon_fs_create_directory)
        error = PBDirCreate(&block, FALSE);
    else
        error = PBHCreate(&block, FALSE);
    if (error)
        return error;
    if (operation == carbon_fs_create_directory)
        request->directory = block.fileParam.ioDirID;
    if (fields || result) {
        error = get_catalog(ref->volume, node, 0, new_name, &catalog);
        if (error)
            return error;
        if (result)
            store_ref(result, ref->volume, &catalog, new_name);
        if (fields) {
            error = set_catalog(ref, &catalog, new_name, fields, info);
            if (error)
                return error;
        }
    }
    if (spec) {
        *spec = (FSSpec){.vRefNum = ref->volume, .parID = node};
        memcpy(spec->name, new_name, (size_t)new_name[0] + 1);
    }
    return noErr;
}

static OSErr identify_fork(uint32_t address, uint32_t length, int* resource)
{
    *resource = 0;
    if (!length)
        return noErr;
    if (length > 255 || !address)
        return fsref_bad_fork_name;
    const uint16_t* input = fs_memory(address, length * sizeof *input, 0);
    if (!input)
        return fsref_bad_fork_name;
    if (length != resource_fork_name[0])
        return fsref_fork_not_found;
    for (unsigned i = 0; i < length; ++i) {
        uint16_t unit;
        memcpy(&unit, input + i, sizeof unit);
        if (CW(unit) != resource_fork_name[i + 1])
            return fsref_fork_not_found;
    }
    *resource = 1;
    return noErr;
}

static OSErr open_fork_kind(carbon_fork_pb* request, const carbon_file_ref* ref, int resource)
{
    _Alignas(256) Str255 name = {0};
    _Alignas(128) CInfoPBRec catalog;
    OSErr error = resolve_ref(ref, &catalog, name);
    if (error)
        return error;
    if (is_directory(&catalog))
        return fnfErr;

    _Alignas(128) HParamBlockRec block = {0};
    block.ioParam.ioVRefNum = ref->volume;
    block.fileParam.ioDirID = catalog.hFileInfo.ioFlParID;
    block.ioParam.ioPermssn = request->permissions;
    PP_RAW(block.ioParam.ioNamePtr) = CL(US_TO_SYN68K(name));
    error = resource ? PBHOpenRF(&block, FALSE) : PBOpenDF(&block, FALSE);
    if (!error)
        request->fork = block.ioParam.ioRefNum;
    return error;
}

static OSErr open_fork(carbon_fork_pb* request, const carbon_file_ref* ref)
{
    int resource;
    const OSErr error = identify_fork(CL(request->name), CL(request->name_length), &resource);
    return error ? error : open_fork_kind(request, ref, resource);
}

static OSErr file_forks(carbon_fork_pb* request, const carbon_file_ref* ref, enum carbon_fs_operation operation)
{
    _Alignas(256) Str255 name = {0};
    _Alignas(128) CInfoPBRec catalog;
    OSErr error = resolve_ref(ref, &catalog, name);
    if (error)
        return error;
    if (is_directory(&catalog))
        return fsref_fork_not_found;
    if (operation == carbon_fs_iterate_forks) {
        uint32_t index;
        memcpy(&index, request->iterator, sizeof index);
        index = CL(index);
        if (index >= 2)
            return fsref_no_more_items;
        carbon_file_name* output = fs_memory(CL(request->output_name), sizeof *output, 1);
        if (request->output_name && !output)
            return paramErr;
        const HFileInfo* file = &catalog.hFileInfo;
        request->position = file_size((uint32_t)CL(index ? file->ioFlRLgLen : file->ioFlLgLen));
        request->allocation = file_size((uint32_t)CL(index ? file->ioFlRPyLen : file->ioFlPyLen));
        if (output) {
            const Byte empty_name[] = {0};
            copy_unicode_name(output, index ? resource_fork_name : empty_name);
        }
        const uint32_t next = CL(index + 1);
        memcpy(request->iterator, &next, sizeof next);
        return noErr;
    }

    int resource;
    error = identify_fork(CL(request->name), CL(request->name_length), &resource);
    if (error)
        return error == fsref_fork_not_found && operation == carbon_fs_create_fork ? wrgVolTypErr : error;
    if (operation == carbon_fs_create_fork)
        return fsref_fork_exists; /* The classic catalog has two implicit forks. */
    if (!resource)
        return permErr;

    _Alignas(128) HParamBlockRec open = {0};
    open.ioParam.ioVRefNum = ref->volume;
    open.fileParam.ioDirID = catalog.hFileInfo.ioFlParID;
    open.ioParam.ioPermssn = fsRdWrPerm;
    PP_RAW(open.ioParam.ioNamePtr) = CL(US_TO_SYN68K(name));
    error = PBHOpenRF(&open, FALSE);
    if (error)
        return error;
    _Alignas(64) ParamBlockRec fork = {0};
    fork.ioParam.ioRefNum = open.ioParam.ioRefNum;
    error = PBSetEOF(&fork, FALSE);
    const OSErr close_error = PBClose(&fork, FALSE);
    return error ? error : close_error;
}

static OSErr read_fork_info(carbon_fork_info_pb* request)
{
    carbon_file_ref* ref = fs_memory(CL(request->ref), sizeof *ref, 1);
    carbon_fork_info* info = fs_memory(CL(request->info), sizeof *info, 1);
    carbon_file_name* output_name = fs_memory(CL(request->name), sizeof *output_name, 1);
    if ((request->ref && !ref) || (request->info && !info) || (request->name && !output_name))
        return paramErr;

    _Alignas(256) Str255 name = {0};
    _Alignas(128) FCBPBRec block = {0};
    const int16_t iterator = CW(request->iterator);
    if (!request->desired && (iterator < 0 || iterator == INT16_MAX))
        return paramErr;
    block.ioVRefNum = request->volume;
    block.ioRefNum = request->desired;
    block.ioFCBIndx = request->desired ? 0 : CW((int16_t)(iterator + 1));
    PP_RAW(block.ioNamePtr) = CL(US_TO_SYN68K(name));
    const OSErr error = PBGetFCBInfo(&block, FALSE);
    if (error)
        return error == rfNumErr ? fsref_bad_fork : error == fnOpnErr ? fsref_no_more_items : error;
    const uint8_t flags = CW(block.ioFCBFlags) >> 8;
    const int resource = (flags & fcfisres) != 0;
    if (ref) {
        const OSErr ref_error = make_ref(block.ioFCBVRefNum, block.ioFCBParID, name, ref);
        if (ref_error)
            return ref_error;
    }
    if (info) {
        ProcessSerialNumber process = {0};
        GetCurrentProcess(&process);
        *info = (carbon_fork_info){
            .flags = flags,
            .permissions = flags & fcsharedwrite ? fsRdWrShPerm
                           : flags & fcwriteperm ? fsRdWrPerm
                                                 : fsRdPerm,
            .volume = block.ioFCBVRefNum,
            .node = block.ioFCBFlNm,
            .fork = CL((uint32_t)(resource ? 0xff : 0)),
            .position = file_size((uint32_t)CL(block.ioFCBCrPs)),
            .logical_end = file_size((uint32_t)CL(block.ioFCBEOF)),
            .physical_end = file_size((uint32_t)CL(block.ioFCBPLen)),
            .process = process,
        };
    }
    if (output_name) {
        const Byte empty_name[] = {0};
        copy_unicode_name(output_name, resource ? resource_fork_name : empty_name);
    }
    if (!request->desired) {
        request->desired = block.ioRefNum;
        request->iterator = block.ioFCBIndx;
    }
    request->actual = block.ioRefNum;
    return noErr;
}

static OSErr open_catalog_iterator(carbon_bulk_pb* block)
{
    const uint32_t flags = CL(block->flags);
    if (flags & ~3u)
        return fsref_bad_iterator_flags;
    if (flags)
        return fsref_iterator_not_supported;
    const carbon_file_ref* ref = fs_memory(CL(block->container), sizeof *ref, 0);
    if (!ref || ref->stamp != CL((uint32_t)fsref_stamp))
        return fsref_bad;
    _Alignas(256) Str255 name = {0};
    _Alignas(128) CInfoPBRec catalog;
    const OSErr error = resolve_ref(ref, &catalog, name);
    if (error)
        return error;
    if (!is_directory(&catalog))
        return fsref_not_folder;
    carbon_catalog_iterator* iterator = (void*)NewPtr(sizeof *iterator);
    if (!iterator)
        return memFullErr;
    *iterator = (carbon_catalog_iterator){
        .stamp = CL((uint32_t)TICK("FSIT")),
        .index = 1,
        .modified = catalog.dirInfo.ioDrMdDat,
        .count = catalog.dirInfo.ioDrNmFls,
        .directory = *ref,
    };
    block->iterator = CL(US_TO_SYN68K(iterator));
    return noErr;
}

static void* catalog_array(uint32_t address, size_t item_size, uint32_t count)
{
    return count <= SIZE_MAX / item_size ? fs_memory(address, item_size * count, 1) : NULL;
}

static OSErr read_catalog_iterator(carbon_bulk_pb* block, carbon_catalog_iterator* iterator)
{
    block->actual = 0;
    block->changed = 0;
    const uint32_t max = CL(block->maximum);
    if (!max)
        return fsref_bad_item_count;
    carbon_catalog_info* info = catalog_array(CL(block->info), sizeof *info, max);
    carbon_file_ref* refs = catalog_array(CL(block->refs), sizeof *refs, max);
    FSSpec* specs = catalog_array(CL(block->specs), sizeof *specs, max);
    carbon_file_name* names = catalog_array(CL(block->names), sizeof *names, max);
    if ((block->info && !info) || (block->refs && !refs) || (block->specs && !specs) || (block->names && !names))
        return paramErr;

    const carbon_file_ref directory = iterator->directory;
    _Alignas(256) Str255 name = {0};
    _Alignas(128) CInfoPBRec catalog;
    OSErr error = resolve_ref(&directory, &catalog, name);
    if (error)
        return error;
    if (iterator->modified != catalog.dirInfo.ioDrMdDat || iterator->count != catalog.dirInfo.ioDrNmFls) {
        block->changed = 1;
        iterator->modified = catalog.dirInfo.ioDrMdDat;
        iterator->count = catalog.dirInfo.ioDrNmFls;
    }

    uint32_t count = 0;
    for (; count < max; ++count) {
        if (iterator->index > INT16_MAX) {
            error = fsref_no_more_items;
            break;
        }
        error = get_catalog(directory.volume, directory.node, iterator->index, name, &catalog);
        if (error) {
            if (error == fnfErr)
                error = fsref_no_more_items;
            break;
        }
        if ((refs && name[0] >= sizeof refs->name) || (specs && name[0] >= sizeof specs->name)) {
            error = bdNamErr;
            break;
        }
        if (info)
            copy_catalog_info(info + count, CL(block->fields), &directory, &catalog);
        if (names)
            copy_unicode_name(names + count, name);
        if (refs)
            store_ref(refs + count, directory.volume, &catalog, name);
        if (specs) {
            specs[count] = (FSSpec){.vRefNum = directory.volume, .parID = directory.node};
            memcpy(specs[count].name, name, (size_t)name[0] + 1);
        }
        ++iterator->index;
    }
    block->actual = CL(count);
    return error;
}

static OSErr set_fork_size(ParamBlockRec* block, uint16_t mode, int32_t offset)
{
    int64_t size = offset;
    OSErr error;
    if (mode == fsFromLEOF) {
        error = PBGetEOF(block, FALSE);
        if (error)
            return error;
        size += (uint32_t)CL(block->ioParam.ioMisc);
    }
    else if (mode == fsFromMark || mode == fsAtMark) {
        error = PBGetFPos(block, FALSE);
        if (error)
            return error;
        size = (uint32_t)CL(block->ioParam.ioPosOffset) + (int64_t)(mode == fsAtMark ? 0 : offset);
    }
    if (size < 0 || size > INT32_MAX)
        return posErr;
    block->ioParam.ioMisc = CL((uint32_t)size);
    return PBSetEOF(block, FALSE);
}

static OSErr transfer_fork(carbon_fork_pb* request, enum carbon_fs_operation operation)
{
    _Alignas(128) ParamBlockRec block = {0};
    block.ioParam.ioRefNum = request->fork;
    const uint16_t mode = CW(request->position_mode);
    const int64_t offset = (int64_t)read_file_size(request->position);
    const int positioning = operation == carbon_fs_set_position || operation == carbon_fs_set_size ||
                            operation == carbon_fs_read_fork || operation == carbon_fs_write_fork;
    if (positioning) {
        if (mode & ~3u)
            return fsref_bad_position_mode;
        if (mode != fsAtMark && (offset < INT32_MIN || offset > INT32_MAX))
            return posErr;
        block.ioParam.ioPosMode = request->position_mode;
        block.ioParam.ioPosOffset = mode == fsAtMark ? 0 : CL((uint32_t)offset);
    }
    OSErr error;
    switch (operation) {
    case carbon_fs_get_position:
        error = PBGetFPos(&block, FALSE);
        if (!error)
            request->position = file_size((uint32_t)CL(block.ioParam.ioPosOffset));
        break;
    case carbon_fs_set_position:
        error = PBSetFPos(&block, FALSE);
        break;
    case carbon_fs_get_size:
        error = PBGetEOF(&block, FALSE);
        if (!error)
            request->position = file_size((uint32_t)CL(block.ioParam.ioMisc));
        break;
    case carbon_fs_set_size:
        error = set_fork_size(&block, mode, (int32_t)offset);
        break;
    case carbon_fs_flush_fork:
        error = PBFlushFile(&block, FALSE);
        break;
    case carbon_fs_close_fork:
        error = PBClose(&block, FALSE);
        break;
    case carbon_fs_read_fork:
    case carbon_fs_write_fork: {
        const uint32_t bytes = CL(request->requested);
        const int reading = operation == carbon_fs_read_fork;
        request->transferred = 0;
        if (bytes > INT32_MAX || (bytes && !fs_memory(CL(request->buffer), bytes, reading)))
            return fsref_bad_buffer;
        PP_RAW(block.ioParam.ioBuffer) = request->buffer;
        block.ioParam.ioReqCount = request->requested;
        error = reading ? PBRead(&block, FALSE) : PBWrite(&block, FALSE);
        request->transferred = block.ioParam.ioActCount;
        break;
    }
    case carbon_fs_allocate_fork: {
        const uint16_t flags = CW(request->allocation_flags);
        const uint64_t bytes = read_file_size(request->allocation);
        request->allocation = file_size(0);
        if (flags & ~7u)
            return fsref_bad_allocation_flags;
        if (mode & ~3u)
            return fsref_bad_position_mode;
        if (bytes > INT32_MAX)
            return dskFulErr;
        block.ioParam.ioReqCount = CL((uint32_t)bytes);
        error = flags & 2 ? PBAllocContig(&block, FALSE) : PBAllocate(&block, FALSE);
        request->allocation = file_size((uint32_t)CL(block.ioParam.ioActCount));
        break;
    }
    default:
        return paramErr;
    }
    return error == rfNumErr ? fsref_bad_fork : error;
}

static void copy_volume_info(carbon_volume_info* output, uint32_t fields, const HVolumeParam* volume)
{
    if (fields & 0x0001)
        output->created = (carbon_file_date){.low = volume->ioVCrDate};
    if (fields & 0x0002)
        output->modified = (carbon_file_date){.low = volume->ioVLsMod};
    if (fields & 0x0004)
        output->backed_up = (carbon_file_date){.low = volume->ioVBkUp};
    if (fields & 0x0008)
        output->checked = (carbon_file_date){0};
    if (fields & 0x0010)
        output->file_count = volume->ioVFilCnt;
    if (fields & 0x0020)
        output->folder_count = volume->ioVDirCnt;
    const uint32_t block_size = CL(volume->ioVAlBlkSiz);
    const uint32_t total_blocks = CW(volume->ioVNmAlBlks);
    const uint32_t free_blocks = CW(volume->ioVFrBlk);
    if (fields & 0x0040) {
        output->total_bytes = file_size((uint64_t)block_size * total_blocks);
        output->free_bytes = file_size((uint64_t)block_size * free_blocks);
    }
    if (fields & 0x0080) {
        output->block_size = volume->ioVAlBlkSiz;
        output->total_blocks = CL(total_blocks);
        output->free_blocks = CL(free_blocks);
    }
    if (fields & 0x0100)
        output->next_block = CL((uint32_t)CW(volume->ioVAllocPtr));
    if (fields & 0x0200)
        output->resource_clump = volume->ioVClpSiz;
    if (fields & 0x0400)
        output->data_clump = volume->ioVClpSiz;
    if (fields & 0x0800)
        output->next_id = volume->ioVNxtCNID;
    if (fields & 0x1000)
        memcpy(output->finder, volume->ioVFndrInfo, sizeof output->finder);
    if (fields & 0x2000)
        output->flags = volume->ioVAtrb;
    if (fields & 0x4000) {
        output->filesystem = volume->ioVFSID;
        output->signature = volume->ioVSigWord;
    }
    if (fields & 0x8000) {
        output->drive = volume->ioVDrvInfo;
        output->driver = volume->ioVDRefNum;
    }
}

static OSErr volume_info(carbon_volume_pb* request, enum carbon_fs_operation operation, carbon_volume_info* info,
                         carbon_file_name* name, carbon_file_ref* ref)
{
    const uint32_t fields = CL(request->fields);
    const uint32_t index = CL(request->index);
    if (index > INT16_MAX || (operation == carbon_fs_set_volume && (!info || (fields & ~0x3004u))))
        return paramErr;

    _Alignas(256) Str255 mac_name = {0};
    _Alignas(128) HParamBlockRec block = {0};
    HVolumeParam* volume = &block.volumeParam;
    volume->ioVRefNum = request->ioVRefNum;
    volume->ioVolIndex = CW((uint16_t)index);
    PP_RAW(volume->ioNamePtr) = CL(US_TO_SYN68K(mac_name));
    const OSErr error = PBHGetVInfo(&block, FALSE);
    if (error)
        return error;
    if (operation == carbon_fs_set_volume) {
        if (!fields)
            return noErr;
        PP_RAW(volume->ioNamePtr) = 0;
        if (fields & 0x0004)
            volume->ioVBkUp = info->backed_up.low;
        if (fields & 0x1000)
            memcpy(volume->ioVFndrInfo, info->finder, sizeof info->finder);
        if (fields & 0x2000)
            volume->ioVAtrb = info->flags;
        return PBSetVInfo(&block, FALSE);
    }

    if (ref) {
        const OSErr ref_error = make_directory_ref(volume->ioVRefNum, CL((uint32_t)2), ref);
        if (ref_error)
            return ref_error;
    }
    request->ioVRefNum = volume->ioVRefNum;
    if (info)
        copy_volume_info(info, fields, volume);
    if (name)
        copy_unicode_name(name, mac_name);
    return noErr;
}

static int fs_result(ROMlib_carbon_ppc_frame* call, OSErr result)
{
    call->words[0] = (int32_t)result;
    return 1;
}

static OSErr path_make_ref(uint32_t address, carbon_file_ref* ref)
{
    _Alignas(256) Str255 name;
    size_t length = 0;
    if (!address)
        return paramErr;
    for (size_t offset = 0; offset <= fsref_path_limit; ++offset) {
        const Byte* byte = (uint64_t)address + offset <= UINT32_MAX ? fs_memory(address + offset, 1, 0) : NULL;
        if (!byte)
            return paramErr;
        const Byte character = *byte;
        if (!character || character == ':') {
            const bool first = !ref->volume;
            if (first && (!character || !length))
                return nsvErr;
            if (!first && !(CW(ref->flags) & fsref_directory))
                return dirNFErr;
            if (!length && !character)
                return noErr;

            OSErr error;
            if (first) {
                if (length == sizeof name - 1)
                    return bdNamErr;
                name[++length] = ':';
                name[0] = length;
                error = make_ref(0, 0, name, ref);
            }
            else if (!length) {
                if (ref->parent == CL((uint32_t)1))
                    return dirNFErr;
                error = make_directory_ref(ref->volume, ref->parent, ref);
            }
            else {
                name[0] = length;
                _Alignas(128) CInfoPBRec catalog;
                error = get_catalog(ref->volume, ref->node, 0, name, &catalog);
                if (!error) {
                    if (is_directory(&catalog))
                        error = make_directory_ref(ref->volume, catalog.dirInfo.ioDrDirID, ref);
                    else if (name[0] >= sizeof ref->name)
                        error = fsref_name_too_long;
                    else
                        store_ref(ref, ref->volume, &catalog, name);
                }
            }
            if (error || !character)
                return error;
            length = 0;
        }
        else {
            if (length == sizeof name - 1)
                return bdNamErr;
            name[++length] = character;
        }
    }
    return fsref_path_too_long;
}

static void ref_path_directory_name(int16_t volume, uint32_t directory, Str255 name)
{
    if (name[0] < 31 || CL(directory) <= 2)
        return;
    HVCB* disk = ROMlib_vcbbyvrn(CW(volume));
    if (!disk || disk->vcbCTRef)
        return;
    /* The classic catalog shortens host directory names to 31 bytes. The
       host directory map retains the full component needed for a round trip. */
    const datum path = ROMlib_dbm_fetch((VCBExtra*)disk, CL(directory));
    if (!path.dptr || path.dsize <= 0)
        return;
    const char* end = path.dptr + path.dsize;
    while (end > path.dptr && (end[-1] == 0 || end[-1] == '/'))
        --end;
    const char* first = end;
    while (first > path.dptr && first[-1] != '/')
        --first;
    const size_t length = end - first;
    if (length <= 31 || length > 255)
        return;
    char mac[255];
    const int converted = ROMlib_host_name_to_mac(first, length, mac);
    if (converted > 31 && !memchr(mac, ':', converted)) {
        memcpy(name + 1, mac, converted);
        name[0] = converted;
    }
}

static OSErr ref_make_path(const carbon_file_ref* ref, uint32_t address, uint32_t capacity)
{
    _Alignas(256) Str255 name = {0};
    _Alignas(128) CInfoPBRec catalog;
    OSErr error = resolve_ref(ref, &catalog, name);
    if (error)
        return error;
    if (!capacity)
        return fsref_buffers_too_small;
    const bool directory = is_directory(&catalog);
    uint32_t parent = directory ? catalog.dirInfo.ioDrParID : catalog.hFileInfo.ioFlParID;
    const size_t size = MIN(capacity, fsref_path_limit + 1u);
    Byte* path = malloc(size);
    if (!path)
        return memFullErr;

    /* Fill from the end so each parent name is copied once. */
    size_t start = size - 1;
    path[start] = 0;
    bool colon = directory;
    for (;;) {
        if (colon)
            ref_path_directory_name(ref->volume, catalog.dirInfo.ioDrDirID, name);
        const size_t length = name[0];
        if (length + colon > start) {
            error = capacity > size ? fsref_path_too_long : fsref_buffers_too_small;
            break;
        }
        if (colon)
            path[--start] = ':';
        start -= length;
        memcpy(path + start, name + 1, length);
        if (parent == CL((uint32_t)1)) {
            Byte* output = fs_memory(address, size - start, 1);
            if (output)
                memcpy(output, path + start, size - start);
            else
                error = paramErr;
            break;
        }
        error = get_catalog(ref->volume, parent, -1, name, &catalog);
        if (error)
            break;
        if (!is_directory(&catalog) || !parent || catalog.dirInfo.ioDrParID == parent) {
            error = dirNFErr;
            break;
        }
        parent = catalog.dirInfo.ioDrParID;
        colon = true;
    }
    free(path);
    return error;
}

static int call_ref_path(ROMlib_carbon_ppc_frame* call, bool from_path)
{
    const uint32_t* words = call->words;
    if (from_path) {
        carbon_file_ref* output = fs_memory(words[1], sizeof *output, 1);
        Byte* is_folder = fs_memory(words[2], 1, 1);
        if (!output || (words[2] && !is_folder))
            return fs_result(call, paramErr);
        carbon_file_ref ref = {0};
        const OSErr error = path_make_ref(words[0], &ref);
        if (!error) {
            *output = ref;
            if (is_folder)
                *is_folder = (CW(ref.flags) & fsref_directory) != 0;
        }
        return fs_result(call, error);
    }

    const carbon_file_ref* ref = fs_memory(words[0], sizeof *ref, 0);
    if (!ref || !words[1])
        return fs_result(call, paramErr);
    if (ref->stamp != CL((uint32_t)fsref_stamp)) {
        ROMlib_carbon_reference original;
        if (ROMlib_carbon_load_reference("CarbonLib", "FSRefMakePath", &original))
            return ROMlib_carbon_invoke_frame(original.function, call);
        return fs_result(call, fsref_bad);
    }
    return fs_result(call, ref_make_path(ref, words[1], words[2]));
}

static int dispatch_fork(ROMlib_carbon_ppc_frame* call, enum carbon_fs_operation operation, int asynchronous,
                         carbon_fork_pb* block)
{
    OSErr error;
    if (operation == carbon_fs_open_fork || operation >= carbon_fs_create_fork) {
        const carbon_file_ref* ref = fs_memory(CL(block->ref), sizeof *ref, 0);
        if (!ref)
            error = fsref_bad;
        else if (ref->stamp != CL((uint32_t)fsref_stamp))
            return ROMlib_carbon_file_trap(asynchronous ? 0xa660 : 0xa260, operation, US_TO_SYN68K(block), call->words);
        else if (operation == carbon_fs_open_fork)
            error = open_fork(block, ref);
        else
            error = file_forks(block, ref, operation);
    }
    else
        error = transfer_fork(block, operation);
    return fs_result(call, ROMlib_file_complete(block, asynchronous, error));
}

static int fork_operation(enum carbon_fs_operation operation)
{
    return operation == carbon_fs_open_fork ||
           (operation >= carbon_fs_read_fork && operation <= carbon_fs_close_fork) ||
           (operation >= carbon_fs_create_fork && operation <= carbon_fs_iterate_forks);
}

static int call_fork(ROMlib_carbon_ppc_frame* call, enum carbon_fs_operation operation)
{
    const uint32_t* words = call->words;
    if (operation == carbon_fs_data_name || operation == carbon_fs_resource_name) {
        const Byte empty_name[] = {0};
        carbon_file_name* name = fs_memory(words[0], sizeof *name, 1);
        if (!name)
            return fs_result(call, paramErr);
        copy_unicode_name(name, operation == carbon_fs_resource_name ? resource_fork_name : empty_name);
        return fs_result(call, noErr);
    }

    _Alignas(128) carbon_fork_pb block = {0};
    void* iterator = NULL;
    void* physical_size = NULL;
    block.fork = CW((int16_t)words[0]);
    uint32_t result_address = 0;
    size_t result_size = 0;
    const void* result = NULL;
    switch (operation) {
    case carbon_fs_create_fork:
    case carbon_fs_delete_fork:
        block.ref = CL(words[0]);
        block.name_length = CL(words[1]);
        block.name = CL(words[2]);
        break;
    case carbon_fs_iterate_forks:
        block.ref = CL(words[0]);
        iterator = fs_memory(words[1], sizeof block.iterator, 1);
        physical_size = fs_memory(words[4], sizeof block.allocation, 1);
        if (!iterator || (words[4] && !physical_size))
            return fs_result(call, paramErr);
        memcpy(block.iterator, iterator, sizeof block.iterator);
        block.output_name = CL(words[2]);
        result_address = words[3];
        result_size = sizeof block.position;
        result = (const Byte*)&block + offsetof(carbon_fork_pb, position);
        break;
    case carbon_fs_open_fork:
        block.ref = CL(words[0]);
        block.name_length = CL(words[1]);
        block.name = CL(words[2]);
        block.permissions = (int8_t)words[3];
        result_address = words[4];
        result_size = sizeof block.fork;
        result = (const Byte*)&block + offsetof(carbon_fork_pb, fork);
        if (!result_address)
            return fs_result(call, paramErr);
        break;
    case carbon_fs_read_fork:
    case carbon_fs_write_fork:
        block.position_mode = CW((uint16_t)words[1]);
        block.position = (carbon_file_size){CL(words[2]), CL(words[3])};
        block.requested = CL(words[4]);
        block.buffer = CL(words[5]);
        result_address = words[6];
        result_size = sizeof block.transferred;
        result = (const Byte*)&block + offsetof(carbon_fork_pb, transferred);
        break;
    case carbon_fs_set_position:
    case carbon_fs_set_size:
        block.position_mode = CW((uint16_t)words[1]);
        block.position = (carbon_file_size){CL(words[2]), CL(words[3])};
        break;
    case carbon_fs_get_position:
    case carbon_fs_get_size:
        result_address = words[1];
        result_size = sizeof block.position;
        result = (const Byte*)&block + offsetof(carbon_fork_pb, position);
        if (!result_address)
            return fs_result(call, paramErr);
        break;
    case carbon_fs_allocate_fork:
        block.allocation_flags = CW((uint16_t)words[1]);
        block.position_mode = CW((uint16_t)words[2]);
        block.position = (carbon_file_size){CL(words[3]), CL(words[4])};
        block.allocation = (carbon_file_size){CL(words[5]), CL(words[6])};
        result_address = words[7];
        result_size = sizeof block.allocation;
        result = (const Byte*)&block + offsetof(carbon_fork_pb, allocation);
        break;
    default:
        break;
    }

    void* output = fs_memory(result_address, result_size, 1);
    if (result_address && !output)
        return fs_result(call, paramErr);
    const int ok = dispatch_fork(call, operation, 0, &block);
    if (ok && output &&
        (!call->words[0] || operation == carbon_fs_read_fork || operation == carbon_fs_write_fork ||
         operation == carbon_fs_allocate_fork))
        memcpy(output, result, result_size);
    if (ok && !call->words[0] && iterator) {
        memcpy(iterator, block.iterator, sizeof block.iterator);
        if (physical_size)
            memcpy(physical_size, &block.allocation, sizeof block.allocation);
    }
    return ok;
}

static int call_fork_info(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* words = call->words;
    void* iterator = fs_memory(words[2], 2, 1);
    void* actual = fs_memory(words[3], 2, 1);
    if ((words[2] && !iterator) || (words[3] && !actual))
        return fs_result(call, paramErr);
    _Alignas(64) carbon_fork_info_pb block = {
        .desired = CW((int16_t)words[0]),
        .volume = CW((int16_t)words[1]),
        .info = CL(words[4]),
        .ref = CL(words[5]),
        .name = CL(words[6]),
    };
    if (iterator)
        memcpy(&block.iterator, iterator, 2);
    const OSErr error = read_fork_info(&block);
    if (!error) {
        if (iterator)
            memcpy(iterator, &block.iterator, 2);
        if (actual)
            memcpy(actual, &block.actual, 2);
    }
    return fs_result(call, error);
}

static int is_bulk_operation(enum carbon_fs_operation operation)
{
    return operation == carbon_fs_open_iterator || operation == carbon_fs_close_iterator ||
           operation == carbon_fs_bulk_catalog;
}

static int dispatch_bulk(ROMlib_carbon_ppc_frame* call, enum carbon_fs_operation operation, int asynchronous,
                         carbon_bulk_pb* block)
{
    OSErr error = fsref_iterator_not_found;
    if (operation == carbon_fs_open_iterator) {
        const carbon_file_ref* ref = fs_memory(CL(block->container), sizeof *ref, 0);
        if (ref && ref->stamp != CL((uint32_t)fsref_stamp))
            return ROMlib_carbon_file_trap(asynchronous ? 0xa660 : 0xa260, operation, US_TO_SYN68K(block), call->words);
        error = open_catalog_iterator(block);
    }
    else {
        carbon_catalog_iterator* cursor = fs_memory(CL(block->iterator), sizeof *cursor, 1);
        if (cursor && cursor->stamp == CL((uint32_t)TICK("FSIT"))) {
            if (operation == carbon_fs_close_iterator) {
                cursor->stamp = 0;
                DisposPtr((Ptr)cursor);
                error = noErr;
            }
            else
                error = read_catalog_iterator(block, cursor);
        }
    }
    return fs_result(call, ROMlib_file_complete(block, asynchronous, error));
}

static int call_bulk(ROMlib_carbon_ppc_frame* call, enum carbon_fs_operation operation)
{
    const uint32_t* words = call->words;
    _Alignas(64) carbon_bulk_pb block = {0};
    void* result = NULL;
    uint8_t* changed = NULL;
    if (operation == carbon_fs_open_iterator) {
        result = fs_memory(words[2], sizeof block.iterator, 1);
        if (!result)
            return fs_result(call, paramErr);
        block.container = CL(words[0]);
        block.flags = CL(words[1]);
    }
    else {
        block.iterator = CL(words[0]);
        if (operation == carbon_fs_bulk_catalog) {
            result = fs_memory(words[2], sizeof block.actual, 1);
            changed = fs_memory(words[3], 1, 1);
            if (!result || (words[3] && !changed))
                return fs_result(call, paramErr);
            block.maximum = CL(words[1]);
            block.fields = CL(words[4]);
            block.info = CL(words[5]);
            block.refs = CL(words[6]);
            block.specs = CL(words[7]);
            uint32_t names;
            if (!ROMlib_carbon_frame_word(call, 8, &names))
                return 0;
            block.names = CL(names);
        }
    }
    const int ok = dispatch_bulk(call, operation, 0, &block);
    if (ok && result) {
        if (operation == carbon_fs_bulk_catalog) {
            memcpy(result, &block.actual, sizeof block.actual);
            if (changed)
                *changed = block.changed;
        }
        else if (!call->words[0])
            memcpy(result, &block.iterator, sizeof block.iterator);
    }
    return ok;
}

static const Byte* read_pathname(uint32_t address)
{
    const Byte* size = fs_memory(address, 1, 0);
    return size ? fs_memory(address, (size_t)*size + 1, 0) : NULL;
}

static int dispatch_catalog(ROMlib_carbon_ppc_frame* call, enum carbon_fs_operation operation, int asynchronous,
                            carbon_catalog_pb* block, uint32_t address)
{
    OSErr error = paramErr;
    if (operation == carbon_fs_make_ref) {
        const uint32_t name_ptr = CL(PP_RAW(block->ioNamePtr));
        const Byte* name = name_ptr ? read_pathname(name_ptr) : NULL;
        carbon_file_ref* result = fs_memory(CL(block->result), sizeof *result, 1);
        if (result && (!name_ptr || name))
            error = make_ref(block->ioVRefNum, block->directory, name, result);
    }
    else {
        const carbon_file_ref* ref = fs_memory(CL(block->ref), sizeof *ref, 0);
        if (!ref)
            error = fsref_bad;
        else if (ref->stamp != CL((uint32_t)fsref_stamp))
            return ROMlib_carbon_file_trap(asynchronous ? 0xa660 : 0xa260, operation, address, call->words);
        else if (operation == carbon_fs_compare_refs) {
            const carbon_file_ref* other = fs_memory(CL(block->parent), sizeof *other, 0);
            if (other && other->stamp == CL((uint32_t)fsref_stamp)) {
                int same_node = ref->node == other->node;
                if (CW(ref->flags) & CW(other->flags) & fsref_host_identity)
                    same_node = memcmp(&ref->identity, &other->identity, sizeof ref->identity) == 0;
                error = (ref->volume == other->volume && same_node) ? noErr : fsref_different;
            }
            else
                error = fsref_bad;
        }
        else if (operation == carbon_fs_get_catalog) {
            const carbon_file_ref snapshot = *ref;
            carbon_catalog_info* info = fs_memory(CL(block->info), sizeof *info, 1);
            carbon_file_name* name = fs_memory(CL(block->output_name), sizeof *name, 1);
            FSSpec* spec = fs_memory(CL(block->spec), sizeof *spec, 1);
            carbon_file_ref* parent = fs_memory(CL(block->parent), sizeof *parent, 1);
            if ((!block->info || info) && (!block->output_name || name) && (!block->spec || spec) &&
                (!block->parent || parent))
                error = read_catalog(&snapshot, CL(block->fields), info, name, spec, parent);
        }
        else {
            const carbon_file_ref snapshot = *ref;
            error = update_catalog(block, &snapshot, operation);
        }
    }
    return fs_result(call, ROMlib_file_complete(block, asynchronous, error));
}

static OSErr extended_volume(carbon_extended_volume_pb* block)
{
    if (block->volume.pfiller2)
        return paramErr;
    const uint32_t name_address = CL((uint32_t)(uintptr_t)PP_RAW(block->volume.ioNamePtr));
    if (name_address) {
        const Byte* name = fs_memory(name_address, 1, 0);
        if (!name || !fs_memory(name_address, MAX(28, name[0] + 1), 1))
            return paramErr;
    }

    _Alignas(128) HParamBlockRec volume = {0};
    volume.volumeParam = block->volume;
    const OSErr status = PBHGetVInfo(&volume, FALSE);
    if (status)
        return status;

    const HVolumeParam* info = &volume.volumeParam;
    const VCBExtra* mounted = (const VCBExtra*)ROMlib_vcbbyvrn(CW(info->ioVRefNum));
    if (!mounted)
        return nsvErr;

    uint64_t total_bytes, free_bytes;
    if (mounted->vcb.vcbCTRef) {
        const uint32_t allocation_size = CL(info->ioVAlBlkSiz);
        total_bytes = (uint64_t)allocation_size * CW(info->ioVNmAlBlks);
        free_bytes = (uint64_t)allocation_size * CW(info->ioVFrBlk);
    }
    else if (!ROMlib_win_volume_bytes(mounted->unixname, &total_bytes, &free_bytes))
        return ioErr;

    block->volume = *info;
    block->total = file_size(total_bytes);
    block->free = file_size(free_bytes);
    return noErr;
}

int ROMlib_carbon_fs_ref_parameter_block(ROMlib_carbon_ppc_frame* call, enum carbon_fs_operation operation,
                                         int asynchronous)
{
    const uint32_t address = call->words[0];
    if (operation == carbon_fs_extended_volume) {
        carbon_extended_volume_pb* block = fs_memory(address, sizeof *block, 1);
        return block ? fs_result(call, ROMlib_file_complete(block, asynchronous, extended_volume(block)))
                     : fs_result(call, paramErr);
    }
    if (fork_operation(operation)) {
        carbon_fork_pb* block = fs_memory(address, sizeof *block, 1);
        return block ? dispatch_fork(call, operation, asynchronous, block) : fs_result(call, paramErr);
    }
    if (operation == carbon_fs_fork_info) {
        carbon_fork_info_pb* block = fs_memory(address, sizeof *block, 1);
        if (!block)
            return fs_result(call, paramErr);
        return fs_result(call, ROMlib_file_complete(block, asynchronous, read_fork_info(block)));
    }
    if (is_bulk_operation(operation)) {
        carbon_bulk_pb* block = fs_memory(address, sizeof *block, 1);
        return block ? dispatch_bulk(call, operation, asynchronous, block) : fs_result(call, paramErr);
    }
    if (operation == carbon_fs_get_volume || operation == carbon_fs_set_volume) {
        carbon_volume_pb* block = fs_memory(address, sizeof *block, 1);
        if (!block)
            return fs_result(call, paramErr);
        const int reading = operation == carbon_fs_get_volume;
        carbon_volume_info* info = fs_memory(CL(block->info), sizeof *info, reading);
        carbon_file_name* name = reading ? fs_memory(CL(block->name), sizeof *name, 1) : NULL;
        carbon_file_ref* ref = reading ? fs_memory(CL(block->ref), sizeof *ref, 1) : NULL;
        OSErr error = paramErr;
        if ((!block->info || info) && (!reading || ((!block->name || name) && (!block->ref || ref))))
            error = volume_info(block, operation, info, name, ref);
        return fs_result(call, ROMlib_file_complete(block, asynchronous, error));
    }
    carbon_catalog_pb* block = fs_memory(address, sizeof *block, 1);
    return block ? dispatch_catalog(call, operation, asynchronous, block, address) : fs_result(call, paramErr);
}

int ROMlib_carbon_fs_ref(ROMlib_carbon_ppc_frame* call, enum carbon_fs_operation operation)
{
    const uint32_t* words = call->words;
    if (operation == carbon_fs_path_make_ref || operation == carbon_fs_ref_make_path)
        return call_ref_path(call, operation == carbon_fs_path_make_ref);
    if (fork_operation(operation) || operation == carbon_fs_data_name || operation == carbon_fs_resource_name)
        return call_fork(call, operation);
    if (operation == carbon_fs_fork_info)
        return call_fork_info(call);
    if (is_bulk_operation(operation))
        return call_bulk(call, operation);
    if (operation == carbon_fs_get_volume || operation == carbon_fs_set_volume) {
        const int reading = operation == carbon_fs_get_volume;
        int16_t* actual_volume = reading ? fs_memory(words[2], 2, 1) : NULL;
        if (reading && words[2] && !actual_volume)
            return fs_result(call, paramErr);
        _Alignas(64) carbon_volume_pb block = {
            .ioVRefNum = CW((int16_t)words[0]),
            .index = reading ? CL(words[1]) : 0,
            .fields = CL(words[reading ? 3 : 1]),
            .info = CL(words[reading ? 4 : 2]),
            .name = reading ? CL(words[5]) : 0,
            .ref = reading ? CL(words[6]) : 0,
        };
        call->words[0] = US_TO_SYN68K(&block);
        const int ok = ROMlib_carbon_fs_ref_parameter_block(call, operation, 0);
        if (actual_volume)
            memcpy(actual_volume, &block.ioVRefNum, sizeof *actual_volume);
        return ok;
    }
    if (operation == carbon_fs_find_folder || operation == carbon_fs_find_folder_extended) {
        const int extended = operation == carbon_fs_find_folder_extended;
        if (extended && (words[3] || words[4])) {
            ROMlib_carbon_reference original;
            return ROMlib_carbon_reference_function("CarbonLib", "FSFindFolderExtended", &original) &&
                   ROMlib_carbon_invoke_frame(original.function, call);
        }
        carbon_file_ref* result = fs_memory(words[extended ? 5 : 3], sizeof *result, 1);
        if (!result)
            return fs_result(call, paramErr);
        _Alignas(4) int16_t volume;
        _Alignas(4) int32_t directory;
        const OSErr error = FindFolder((int16_t)words[0], words[1], (uint8_t)words[2], &volume, &directory);
        return fs_result(call, error ? error : make_directory_ref(volume, directory, result));
    }
    if (operation == carbon_fs_get_volume_ref) {
        int16_t* volume = fs_memory(words[1], 2, 1);
        return fs_result(call, volume ? GetVRefNum((int16_t)words[0], volume) : paramErr);
    }
    if (operation == carbon_fs_flush_volume) {
        const Byte* name = words[0] ? read_pathname(words[0]) : NULL;
        if (words[0] && !name)
            return fs_result(call, paramErr);
        _Alignas(64) ParamBlockRec block = {0};
        PP_RAW(block.ioParam.ioNamePtr) = CL(words[0]);
        block.ioParam.ioVRefNum = CW((int16_t)words[1]);
        return ROMlib_carbon_file_trap(0xa013, -1, US_TO_SYN68K(&block), call->words);
    }
    if (operation == carbon_fs_cat_move) {
        if ((words[2] && !read_pathname(words[2])) || (words[4] && !read_pathname(words[4])))
            return fs_result(call, paramErr);
        _Alignas(64) CMovePBRec block = {0};
        block.ioVRefNum = CW((int16_t)words[0]);
        block.ioDirID = CL(words[1]);
        PP_RAW(block.ioNamePtr) = CL(words[2]);
        block.ioNewDirID = CL(words[3]);
        PP_RAW(block.ioNewName) = CL(words[4]);
        return ROMlib_carbon_file_trap(0xa260, 5, US_TO_SYN68K(&block), call->words);
    }

    _Alignas(128) carbon_catalog_pb block = {0};
    if (operation == carbon_fs_make_ref_from_spec) {
        const FSSpec* spec = fs_memory(words[0], sizeof *spec, 0);
        if (!spec || spec->name[0] > 63)
            return fs_result(call, paramErr);
        block.ioVRefNum = spec->vRefNum;
        block.directory = spec->parID;
        PP_RAW(block.ioNamePtr) = CL(words[0] + offsetof(FSSpec, name));
        block.result = CL(words[1]);
        operation = carbon_fs_make_ref;
    }
    else {
        block.ref = CL(words[0]);
        switch (operation) {
        case carbon_fs_get_catalog:
            block.fields = CL(words[1]);
            block.info = CL(words[2]);
            block.output_name = CL(words[3]);
            block.spec = CL(words[4]);
            block.parent = CL(words[5]);
            break;
        case carbon_fs_set_catalog:
            block.fields = CL(words[1]);
            block.info = CL(words[2]);
            break;
        case carbon_fs_create_file:
        case carbon_fs_create_directory:
            block.name_length = CL(words[1]);
            block.name = CL(words[2]);
            block.fields = CL(words[3]);
            block.info = CL(words[4]);
            block.result = CL(words[5]);
            block.spec = CL(words[6]);
            break;
        case carbon_fs_make_ref_unicode:
        case carbon_fs_rename_object:
            block.name_length = CL(words[1]);
            block.name = CL(words[2]);
            block.encoding = CL(words[3]);
            block.result = CL(words[4]);
            break;
        case carbon_fs_move_object:
            block.result = CL(words[2]);
            /* fall through */
        case carbon_fs_compare_refs:
            block.parent = CL(words[1]);
            break;
        default:
            break;
        }
    }
    void* new_directory = operation == carbon_fs_create_directory ? fs_memory(words[7], 4, 1) : NULL;
    if (operation == carbon_fs_create_directory && words[7] && !new_directory)
        return fs_result(call, paramErr);
    const int ok = dispatch_catalog(call, operation, 0, &block, US_TO_SYN68K(&block));
    if (ok && !call->words[0] && new_directory)
        memcpy(new_directory, &block.directory, 4);
    return ok;
}

/* Classic and FSRef alias records share the AliasHandle format. Only the input
   and output file identities need conversion around the live Alias trap. */
int ROMlib_carbon_fs_alias(ROMlib_carbon_ppc_frame* call, enum carbon_fs_alias_operation operation)
{
    static const struct {
        const char* name;
        uint32_t selector, info;
        int8_t target_in, target_out, alias_in, alias_out;
        uint8_t flags_out, from_required;
    } layouts[] = {
        [fs_alias_new] = {"FSNewAlias", 2, 0x3fe8, 1, -1, -1, 2, 0, 0},
        [fs_alias_minimal] = {"FSNewAliasMinimal", 8, 0xfe8, -1, -1, -1, 1, 0, 1},
        [fs_alias_info] = {"FSIsAliasFile", 0, 0, -1, -1, -1, -1, (1 << 1) | (1 << 2), 1},
        [fs_alias_resolve] = {"FSResolveAlias", 3, 0xffe8, -1, 2, 1, -1, 1 << 3, 0},
        [fs_alias_resolve_mount] = {"FSResolveAliasWithMountFlags", 3, 0xffe8, -1, 2, 1, -1, 1 << 3, 0},
        [fs_alias_resolve_file] = {"FSResolveAliasFile", 0xc, 0xf7e8, -1, 0, -1, -1, (1 << 2) | (1 << 3), 1},
        [fs_alias_resolve_file_mount] = {"FSResolveAliasFileWithMountFlags", 0x29, 0x3f7e8, -1, 0, -1, -1,
                                         (1 << 2) | (1 << 3), 1},
        [fs_alias_follow] = {"FSFollowFinderAlias", 0xf, 0x3dfe8, 3, 3, 1, -1, 1 << 4, 0},
        [fs_alias_update] = {"FSUpdateAlias", 6, 0xffe8, 1, -1, 2, -1, 1 << 3, 0},
    };

    const uint32* words = call->words;
    const unsigned index = operation;
    const int target_in = layouts[index].target_in, target_out = layouts[index].target_out;
    const int alias_in = layouts[index].alias_in, alias_out = layouts[index].alias_out;
    const carbon_file_ref* from = fs_memory(words[0], sizeof *from, 0);
    const carbon_file_ref* target = target_in >= 0 ? fs_memory(words[target_in], sizeof *target, 0) : NULL;
    carbon_file_ref* output = target_out >= 0 ? fs_memory(words[target_out], sizeof *output, 1) : NULL;
    if ((!from && (words[0] || layouts[index].from_required)) || (!target && target_in >= 0) ||
        (!output && target_out >= 0) || (alias_in >= 0 && !fs_memory(words[alias_in], 4, 0)) ||
        (alias_out >= 0 && !fs_memory(words[alias_out], 4, 1)))
        return fs_result(call, paramErr);
    for (unsigned mask = layouts[index].flags_out, word = 0; mask; mask >>= 1, ++word)
        if ((mask & 1) && !fs_memory(words[word], 1, 1))
            return fs_result(call, paramErr);

    if ((from && from->stamp != CL((uint32_t)fsref_stamp)) || (target && target->stamp != CL((uint32_t)fsref_stamp)) ||
        (operation == fs_alias_resolve_mount && words[4]) ||
        (operation == fs_alias_resolve_file_mount && (words[4] & ~(uint32)kResolveAliasFileNoUI))) {
        ROMlib_carbon_reference original;
        if (ROMlib_carbon_probe_reference("CarbonLib", layouts[index].name, &original))
            return ROMlib_carbon_invoke_frame(original.function, call);
        return fs_result(call, -4); /* unimpErr */
    }

    if (operation == fs_alias_info) {
        _Alignas(256) Str255 name;
        _Alignas(128) CInfoPBRec catalog;
        OSErr error = resolve_ref(from, &catalog, name);
        if (!error) {
            const int folder = is_directory(&catalog);
            Byte* alias_flag = fs_memory(words[1], 1, 1);
            Byte* folder_flag = fs_memory(words[2], 1, 1);
            *alias_flag = !folder && !!(CW(catalog.hFileInfo.ioFlFndrInfo.fdFlags) & 0x8000);
            *folder_flag = folder;
        }
        return fs_result(call, error);
    }

    _Alignas(128) FSSpec from_spec, target_spec = {0};
    ROMlib_carbon_ppc_frame arguments = *call;
    if (from) {
        OSErr error = read_catalog(from, 0, NULL, NULL, &from_spec, NULL);
        if (error)
            return fs_result(call, error);
        arguments.words[0] = US_TO_SYN68K(&from_spec);
    }
    if (target) {
        OSErr error = read_catalog(target, 0, NULL, NULL, &target_spec, NULL);
        if (error)
            return fs_result(call, error);
        arguments.words[target_in] = US_TO_SYN68K(&target_spec);
    }
    if (output && target_out)
        arguments.words[target_out] = US_TO_SYN68K(&target_spec);
    uint32 result;
    if (!ROMlib_carbon_call_toolbox_vector(0x0e8c, layouts[index].info, layouts[index].selector, &arguments, &result))
        return 0;
    if (!(int16_t)result && output) {
        const FSSpec* spec = target_out ? &target_spec : &from_spec;
        result = spec->name[0] > 63 ? bdNamErr : make_ref(spec->vRefNum, spec->parID, spec->name, output);
    }
    return fs_result(call, (int16_t)result);
}

static int resource_result(ROMlib_carbon_ppc_frame* call, enum carbon_fs_resource_operation op, OSErr error)
{
    if (op == fs_resource_open || op == fs_resource_create)
        ROMlib_setreserr(error);
    if (op == fs_resource_open)
        return fs_result(call, -1);
    return fs_result(call, error);
}

int ROMlib_carbon_fs_resource(ROMlib_carbon_ppc_frame* call, enum carbon_fs_resource_operation op)
{
    const uint32* words = call->words;
    if (op == fs_resource_detach)
        return fs_result(call, ROMlib_detach_resource_file((int16_t)words[0]));
    if (op == fs_resource_insert)
        return fs_result(call, ROMlib_insert_resource_file((int16_t)words[0], (int16_t)words[1]));
    if (op == fs_resource_top || op == fs_resource_next) {
        const int top = op == fs_resource_top;
        void* output = fs_memory(words[top ? 0 : 1], sizeof(INTEGER), 1);
        if (!output)
            return fs_result(call, paramErr);
        INTEGER reference;
        const OSErr error = ROMlib_resource_chain_refnum(top, (int16_t)words[0], &reference);
        memcpy(output, &reference, sizeof reference);
        return fs_result(call, error);
    }

    if (op == fs_resource_open_data_spec || op == fs_resource_create_data_spec || op == fs_resource_open_orphan) {
        const FSSpec* spec = fs_memory(words[0], sizeof *spec, 0);
        if (!spec || spec->name[0] > 63)
            return fs_result(call, paramErr);
        if (op == fs_resource_create_data_spec)
            return fs_result(call, ROMlib_create_data_resource_file(spec));
        void* output = fs_memory(words[2], sizeof(INTEGER), 1);
        if (!output)
            return fs_result(call, paramErr);
        INTEGER reference;
        const OSErr error = op == fs_resource_open_orphan
                                ? ROMlib_open_orphan_resource_file(spec, (SignedByte)words[1], &reference)
                                : ROMlib_open_data_resource_file(spec, (SignedByte)words[1], &reference);
        if (!error || op == fs_resource_open_data_spec)
            memcpy(output, &reference, sizeof reference);
        return fs_result(call, error);
    }

    const int creating = op == fs_resource_create || op == fs_resource_create_named;
    const int named = op == fs_resource_open_named || op == fs_resource_create_named;
    const int querying = op == fs_resource_is_open || op == fs_resource_spec_is_open;
    static const char* const names[] = {
        "FSCreateResFile",    "FSCreateResourceFile",      "FSOpenResFile",
        "FSOpenResourceFile", "FSResourceFileAlreadyOpen", "FSpResourceFileAlreadyOpen",
    };
    const carbon_file_ref* ref = op == fs_resource_spec_is_open ? NULL : fs_memory(words[0], sizeof *ref, 0);
    const FSSpec* input = op == fs_resource_spec_is_open ? fs_memory(words[0], sizeof *input, 0) : NULL;
    const uint32_t name_length = named ? words[creating ? 5 : 1] : 0;
    const uint32_t name = named ? words[creating ? 6 : 2] : 0;
    int is_resource = 1;
    OSErr error = named ? identify_fork(name, name_length, &is_resource) : noErr;
    if ((ref && ref->stamp != CL((uint32_t)fsref_stamp)) || error == fsref_fork_not_found) {
        ROMlib_carbon_reference saved;
        if (ROMlib_carbon_probe_reference("CarbonLib", names[op], &saved))
            return ROMlib_carbon_invoke_frame(saved.function, call);
        return querying ? fs_result(call, 0) : resource_result(call, op, -4);
    }

    if (querying) {
        Boolean* chain = fs_memory(words[1], 1, 1);
        INTEGER* found = fs_memory(words[2], 2, 1);
        if ((words[1] && !chain) || (words[2] && !found))
            return fs_result(call, 0);
        FSSpec spec;
        if (ref && read_catalog(ref, 0, NULL, NULL, &spec, NULL) == noErr)
            input = &spec;
        INTEGER reference;
        const int open = ROMlib_resource_file_already_open(input, chain, &reference);
        if (found)
            memcpy(found, &reference, sizeof reference);
        return fs_result(call, open);
    }
    if (!ref)
        return resource_result(call, op, paramErr);

    _Alignas(64) carbon_fork_pb fork = {0};
    if (error)
        return resource_result(call, op, error);

    if (!creating) {
        INTEGER* output = named ? fs_memory(words[4], 2, 1) : NULL;
        if (named && !output)
            return resource_result(call, op, paramErr);
        _Alignas(128) FSSpec spec;
        INTEGER handle = -1;
        if (is_resource) {
            error = read_catalog(ref, 0, NULL, NULL, &spec, NULL);
            if (!error) {
                ROMlib_carbon_ppc_frame arguments = {0};
                uint32 result;
                arguments.words[0] = US_TO_SYN68K(&spec);
                arguments.words[1] = words[named ? 3 : 1];
                if (!ROMlib_carbon_call_toolbox_vector(0x1748, 0x7e8, 0xd, &arguments, &result))
                    return 0;
                handle = (int16_t)result;
            }
        }
        else {
            fork.permissions = words[3];
            error = open_fork_kind(&fork, ref, is_resource);
            if (!error)
                handle = ROMlib_open_data_resource_descriptor(CW(fork.fork));
        }
        if (error)
            ROMlib_setreserr(error);
        if (!named)
            return fs_result(call, handle);
        uint16_t encoded = CW(handle);
        memcpy(output, &encoded, sizeof encoded);
        return fs_result(call, handle == -1 ? (error ? error : C_ResError()) : noErr);
    }

    uint32 spec_address = words[6];
    if (named && !ROMlib_carbon_frame_word(call, 8, &spec_address))
        return 0;
    const uint32 ref_address = words[named ? 7 : 5];
    carbon_file_ref* output = fs_memory(ref_address, sizeof *output, 1);
    FSSpec* spec_out = fs_memory(spec_address, sizeof *spec_out, 1);
    const carbon_catalog_info* info = fs_memory(words[4], sizeof *info, 0);
    if ((ref_address && !output) || (spec_address && !spec_out) || (words[4] && !info))
        return resource_result(call, op, paramErr);
    if (words[3] && !info)
        return resource_result(call, op, fsref_missing_info);
    if (words[3] & ~0x1fe3u)
        return resource_result(call, op, fsref_bad_bitmap);

    _Alignas(128) carbon_file_ref created;
    _Alignas(128) FSSpec spec;
    _Alignas(128) carbon_catalog_pb request = {
        .name = CL(words[2]),
        .name_length = CL(words[1]),
        .result = CL(US_TO_SYN68K(&created)),
        .spec = CL(US_TO_SYN68K(&spec)),
    };
    error = update_catalog(&request, ref, carbon_fs_create_file);
    const int made_file = !error;
    if (error == dupFNErr && !named) {
        error = update_catalog(&request, ref, carbon_fs_make_ref_unicode);
        if (!error)
            error = read_catalog(&created, 0, NULL, NULL, &spec, NULL);
    }
    if (error)
        return resource_result(call, op, error);
    fork.permissions = fsRdWrPerm;
    error = open_fork_kind(&fork, &created, is_resource);
    if (!error) {
        const INTEGER fd = CW(fork.fork);
        error = ROMlib_initialize_resource_descriptor(fd);
        const OSErr close_error = FSClose(fd);
        if (!error)
            error = close_error;
    }
    if (!error && words[3]) {
        request.fields = CL(words[3]);
        request.info = CL(words[4]);
        error = update_catalog(&request, &created, carbon_fs_set_catalog);
    }
    if (error && made_file)
        update_catalog(&request, &created, carbon_fs_delete_object);
    if (!error) {
        if (output)
            memcpy(output, &created, sizeof created);
        if (spec_out)
            memcpy(spec_out, &spec, sizeof spec);
    }
    if (!named)
        ROMlib_setreserr(error);
    return fs_result(call, error);
}
