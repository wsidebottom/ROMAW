/*+===================================================================
  File:      gestalt.c

  Summary:   Implements gestalt support for Macintosh startup, traps and runtime services.

  Classes:   None.

  Functions: ROMlib_add_to_gestalt_list, ROMlib_clear_gestalt_list, ROMlib_gestalt_value, gestalt_set_memory_size,
             gestalt_set_physical_gestalt_callback, gestalt_set_system_version, replace_physgestalt_selector,
             ROMlib_powerpc_cache_read, ROMlib_remove_gestalt_handlers

  Origin:    Existing repository source in executor/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1992 - 1999 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in Gestalt.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "FileMgr.h"
#include "MemoryMgr.h"
#include "ResourceMgr.h"
#include "Gestalt.h"

#include "rsys/prefs.h"
#include "rsys/soundopts.h"
#include "rsys/mman.h"
#include "rsys/gestalt.h"
#include "rsys/version.h"
#include "rsys/vdriver.h"
#include "rsys/hosttrash.h"
#include "rsys/ecf.h"
#include "rsys/filedouble.h"
#include "rsys/file.h"
#include "rsys/hfs.h"
#include "rsys/user_profile.h"
#include "rsys/macinfo.h"
#include "rsys/notmac.h"
#include "rsys/suffix_maps.h"
#include "rsys/filetypemap.h"
#include "rsys/application_registry.h"
#include "rsys/multimedia.h"
#include "rsys/findermetadata.h"
#include "rsys/hostcatalog.h"
#include <unistd.h>

#if defined(CYGWIN32)
#include "win_dongle.h"
#include "win_print.h"
#endif

typedef struct {
    OSType selector;
    LONGINT value;
} gestaltentry_t;

#define UNKNOWN 0xFFFF

PRIVATE gestaltentry_t gtable[] = {
    {TICK("fctx"), 3}, /* Private cooperative guest-context ABI. */
    {
        gestaltAddressingModeAttr,
        (1 << gestalt32BitAddressing) | (1 << gestalt32BitSysZone) | (1 << gestalt32BitCapable),
    },

    /*
 * note: PageMaker 4.2 does a Gestalt call for the AliasMgr and then
 *	 proceeds to use the Alias Manager, even if we say it's not
 *	 present.  However, it will avoid use of the Alias Manager
 *	 if the Gestalt call itself returns an error.  This doesn't
 *	 appear to be within spec (IMVI), but it looks like we should
 *	 just not know the descriptors that we don't support.  Boo.
 *
 *       MacInTax '93 does the same thing with the Edition Manager that
 *	 PageMaker does with the Alias Manager.  It's unclear how widespread
 *	 this problem is.
 *
 *	 Claris Works does the same thing with Apple Events... bad!
 */

    /*  gestaltAliasMgrAttr,	0,	*/
    /*  gestaltApplEventsAttr,	0,	*/
    /*  gestaltAppleTalkVersion,	0,	*/
    /*  gestaltAUXVersion,		UNKNOWN,*/ /* Should return gestaltUnknownErr */
    /*  gestaltConnMgrAttr,		0,	*/
    /*  gestaltCRMAttr,		0,	*/
    /*  gestaltCTBVersion,		UNKNOWN,*/ /* should return gestaltUnknownErr */
    /*  gestaltDBAccessMgrAttr,	0,	*/
    {
        gestaltDITLExtAttr,
        gestaltDITLExtPresent,
    },
    /*  gestaltEasyAccessAttr,	gestaltEasyAccessOff,	*/

    /*  gestaltEditionMgrAttr,	0, */

    {gestaltFindFolderAttr, 1},
    {T('q', 't', 'i', 'm'), ROMLIB_QUICKTIME_VERSION},
#if defined(CYGWIN32) && defined(SYN68K)
    {T('t', 't', 's', 'c'), 1},
#endif

    /*  gestaltFontMgrAttr,		0,	*/
    {
        gestaltFSAttr,
        gestaltStandardFile58 | gestaltHasFSSpecCalls,
    },
    /*  gestaltFXfrMgrAttr,		0,	*/
    /*  gestaltHardwareAttr,	0,	*/
    /*  gestaltHelpMgrAttr,		0,	*/
    /*  gestaltMiscAttr,		0,	*/
    /*  gestaltNotificatinMgrAttr,	0,	*/
    /*  gestaltNuBusConnectors,	0,	*/
    /*  gestaltOSAttr,		0,	*/
    /*  gestaltPopupAttr,		0,	*/
    /*  gestaltPowerMgrAttr,	0,	*/
    /*  gestaltPPCToolboxAttr,	0,	*/
    /*  gestaltResourceMgrAttr,	0,	*/
    /*  gestaltScriptCount,		0,	*/
    /*  gestaltScriptMgrVersion,	UNKNOWN,*/ /* should return gestaltUnknownErr */
                                                   /*  gestaltSerialAttr,		0,	*/
    {
        gestaltSoundAttr,
        0x0C00,
    },
/*  gestaltStdNBPAttr,		0,	*/
/*  gestaltTermMgrAttr,		0,	*/
/*  gestaltParityAttr,		0,	*/

/*
 * TODO: instead of casting these to LONGINT, shouldn't we be using something
 *       like US_TO_SYN68K here, or if not, soemthing that takes ROMlib_offset
 *       into account?
 */

#if SIZEOF_CHAR_P == 4
    {
        gestaltExtToolboxTable,
        (LONGINT)(tooltraptable + 0x200),
    },
    {
        gestaltToolboxTable,
        (LONGINT)tooltraptable,
    },
    {
        gestaltOSTable,
        (LONGINT)ostraptable,
    },
#else
  { gestaltExtToolboxTable,	UNKNOWN, },
  { gestaltOSTable,		UNKNOWN, },
  { gestaltToolboxTable,	UNKNOWN, },
#warning gestalt{ExtToolboxTable,ToolboxTable,OSTable} not initialized
#endif

#if defined(SYN68K)
    {
        gestaltFPUType,
        0,
    }, /* gestaltNoFPU */
#else
  { gestaltFPUType,		gestalt68881+2,	/* 'tis a lie; we're an 040 */ },
#endif
    {
        gestaltKeyboardType,
        gestaltMacKbd,
    },
    {
        gestaltLogicalPageSize,
        8 * 1024,
    },
    {
        gestaltLogicalRAMSize,
        (LONGINT)3 * 1024 * 1024,
    },
    {
        gestaltLowMemorySize,
        0x1000,
    },
#if !defined(SYN68K)
    {gestaltMMUType, gestalt68040MMu, /* ack */},
#else
  { gestaltMMUType,		gestaltNoMMU, },
#endif
    {
        gestaltPhysicalRAMSize,
        (LONGINT)3 * 1024 * 1024,
    },
    {T('r', 'r', 'a', 'm'), (LONGINT)3 * 1024 * 1024}, /* Finder's built-in RAM query. */
#if defined(SYN68K)
    {
        gestaltProcessorType,
        gestalt68040,
    },
#else
  { gestaltProcessorType,	gestalt68040,	/* TODO:  need to fill in
						   different info on 030*/ },
#endif

    {
        gestaltNativeCPUtype,
        gestaltCPU68040,
    },
    {
        gestaltMachineType,
        gestaltMacQuadra610,
    },

#if defined(ORIGINAL_QD)
    {
        gestaltQuickdrawVersion,
        gestaltOriginalQD,
    },
#else
/*
 * Star Trek demo needs 32-bit QD 1.1 at least.
 */
  { gestaltQuickdrawVersion,    gestalt32BitQD13 },
#endif
    {gestaltQuickdrawFeatures,
     ((1 << gestaltHasColor) | (1 << gestaltHasDeepGWorlds) | (1 << gestaltHasDirectPixMaps))},

    {
        gestaltTimeMgrVersion,
        gestaltStandardTimeMgr,
    },
    {
        gestaltVersion,
        1,
    },

/* We don't want ClarisWorks 3 calling funky IMVI memory manager
   * routines so we don't recognize gestaltVMAddr at all.  NOTE:
   * ClarisWorks 3 will also not call those "funky routines" if we
   * recognize the gestaltVMAttr selector, but say we don't support
   * it.  Furthermore, saying we don't support it breaks MacBench
   * (which is kludgily worked around elsewhere).  The ONLY REASON we
   * say we don't even recognize this selector (and do the MacBench
   * hack elsewhere) is paranoia: "our original ClarisWorks workaround
   * was to not recognize this selector, and we've done much testing
   * since then.  Perhaps some other program out there would die if we
   * recognized the VM selector."
   *
   * Once ROMAW 2 is out and we are next free to potentially break
   * programs, we should take out this #if 0 and recognize the VMAttr
   * selector, but claim we don't have any VM.
   */
#if 0
  { gestaltVMAttr, 0 },
#endif
    {
        gestaltMachineIcon,
        UNKNOWN,
    },
    {
        gestaltROMSize,
        (LONGINT)128 * 1024,
    },
    {
        gestaltROMVersion,
        0x7c,
    },
    {
        gestaltSystemVersion,
        0x607,
    },
    {
        gestaltTextEditVersion,
        gestaltTE1,
    },

#if 0
#warning DO NOT CHECK THIS IN
  { T ('d', 'r', 'a', 'g'),	0, },
#endif

    /* Help, Script and Edition Manager selectors require their corresponding
     ROMlib_pretend_xxx flags. The implemented Alias Manager is advertised
     for System 7 and later, or explicitly enabled for older systems. */

    {gestaltHelpMgrAttr, 1},          /* must be 1 */
    {gestaltScriptMgrVersion, 0x001}, /* have gotten away with 0x001 */
    {gestaltEditionMgrAttr, 1},       /* must be 1 */
    {gestaltAliasMgrAttr, 1},         /* must be 1 */

    {gestaltPHYSICAL, 0}, /* will be filled in w/ callback address */

#define SYSTEM_7_EXTRAS 2

    {
        gestaltStandardFileAttr,
        gestaltStandardFile58,
    },
    {gestaltApplEventsAttr, 1}, /* got away with 0, but that may
				        give us trouble elsewhere */

#if defined(THESE_REALLY_ARENT_NEEDED)
    {T('i', 'c', 'm', 'p'), 17}, /* QuickTime ... don't want this! */
    {T('s', 'y', 's', 'a'), 0x4F30},
    {gestaltOSAttr, 0x3FF},
    {gestaltPopupAttr, 1}, /* perhaps we should admit to this */
    {gestaltFontMgrAttr, 7},
#endif

};

PRIVATE OSType gtable_selectors_to_patch[] = {
    gestaltExtToolboxTable,
    gestaltToolboxTable,
    gestaltOSTable,
};

/*
gestaltSystemVersion   		'sysv' ROMAW version numeric encoding
gestaltPhysicalRAMSize		'ram '
*/

#define TO_BE_FILLED_IN (0)

PRIVATE gestaltentry_t phystable[] = {
    {gestaltSystemVersion, ROMAW_VERSION_NUMERIC},
#if SIZEOF_CHAR_P == 4
    {gestaltROMAWVersionString, (long)ROMAW_VERSION},
#else
#warning gestaltROMAWVersionString not currently available
#endif
#if defined(LINUX) || defined(CYGWIN32)
    {gestaltPhysicalRAMSize, TO_BE_FILLED_IN},
#endif
    {gestaltScreenSize, TO_BE_FILLED_IN},
};

PRIVATE OSType phystable_selectors_to_patch[] = {
    gestaltROMAWVersionString,
};

typedef struct gestalt_link_str {
    OSType selector;
    ProcPtr selectorFunction;
    struct gestalt_link_str* next;
} gestalt_link_t;

PRIVATE gestalt_link_t* gestalt_head;

void ROMlib_remove_gestalt_handlers(int (*matches)(ProcPtr, void*), void* owner)
{
    for (gestalt_link_t** link = &gestalt_head; *link;) {
        gestalt_link_t* entry = *link;
        if (matches(entry->selectorFunction, owner)) {
            *link = entry->next;
            DisposPtr((Ptr)entry);
        }
        else
            link = &entry->next;
    }
}

PRIVATE gestalt_link_t* find_selector_on_list(OSType selector)
{
    gestalt_link_t* gp;

    for (gp = gestalt_head; gp && gp->selector != selector; gp = gp->next)
        ;
    return gp;
}

PRIVATE gestaltentry_t* find_selector_in_table(OSType selector, gestaltentry_t table[], int table_length)
{
    INTEGER i;
    gestaltentry_t* retval;

    if (system_version < 0x700 && table == gtable)
        table_length -= SYSTEM_7_EXTRAS;

    switch (selector) {
    case gestaltHelpMgrAttr:
        if (!ROMlib_pretend_help)
            selector = -1;
        break;
    case gestaltScriptMgrVersion:
        if (!ROMlib_pretend_script)
            selector = -1;
        break;
    case gestaltEditionMgrAttr:
        if (!ROMlib_pretend_edition)
            selector = -1;
        break;
    case gestaltAliasMgrAttr:
        if (system_version < 0x700 && !ROMlib_pretend_alias)
            selector = -1;
        break;
    }

    for (i = 0; i < table_length && table[i].selector != selector; ++i)
        ;
    if (i < table_length)
        retval = &table[i];
    else
        retval = 0;

    return retval;
}

PRIVATE void replace_selector_in_table(OSType selector, LONGINT new_value, gestaltentry_t table[], int table_length)
{
    gestaltentry_t* gp;

    gp = find_selector_in_table(selector, table, table_length);
    if (gp)
        gp->value = new_value;
}

#define REPLACE_SELECTOR_IN_TABLE(selector, new_value, table) \
    replace_selector_in_table(selector, new_value, table, NELEM(table))

PUBLIC void replace_physgestalt_selector(OSType selector, uint32 new_value)
{
    REPLACE_SELECTOR_IN_TABLE(selector, new_value, phystable);
}

PUBLIC void gestalt_set_memory_size(uint32 size)
{
    REPLACE_SELECTOR_IN_TABLE(gestaltLogicalRAMSize, size, gtable);
    REPLACE_SELECTOR_IN_TABLE(gestaltPhysicalRAMSize, size, gtable);
    REPLACE_SELECTOR_IN_TABLE(T('r', 'r', 'a', 'm'), size, gtable);
}

PUBLIC void gestalt_set_system_version(uint32 version)
{
    REPLACE_SELECTOR_IN_TABLE(gestaltSystemVersion, version, gtable);
}

PUBLIC void gestalt_set_physical_gestalt_callback(void)
{
    REPLACE_SELECTOR_IN_TABLE(gestaltPHYSICAL, (long)US_TO_SYN68K(P_PhysicalGestalt), gtable);
}

PRIVATE OSErrRET gestalt_helper(OSType selector, LONGINT* responsep, BOOLEAN searchlist, gestaltentry_t table[],
                                int length)
{
    OSErr retval;
    gestalt_link_t* gp;

    retval = gestaltUndefSelectorErr;

    if (selector == gestaltSoundAttr && ROMlib_PretendSound == soundoff)
        selector = 0;

    *responsep = CLC(0); /* better safe than sorry */

    if (searchlist && (gp = find_selector_on_list(selector)))
        retval = CToPascalCall(gp->selectorFunction, CTOP_GestaltTablesOnly, selector, responsep);
    else {
        gestaltentry_t* gep;

        gep = find_selector_in_table(selector, table, length);
        if (gep) {
            if (gep->value == UNKNOWN)
                retval = gestaltUnknownErr;
            else {
                retval = noErr;
                *responsep = CL(gep->value);
            }
        }
    }

    /* Log the gestalt call if appropriate. */
    warning_trace_info("sel `%c%c%c%c'; rsp `%d'; retval `%d'", (selector >> 24) & 0xFF, (selector >> 16) & 0xFF,
                       (selector >> 8) & 0xFF, (selector) & 0xFF, CL(*responsep), retval);

    return retval;
}

PRIVATE void offset_address(gestaltentry_t* gp, int ngp, OSType sel)
{
    for (; ngp > 0 && gp->selector != sel; --ngp, ++gp)
        ;
    if (ngp > 0)
        gp->value = US_TO_SYN68K(gp->value);
}

PRIVATE void offset_addresses(gestaltentry_t* gp, int ngp, OSType* selp, int nsels)
{
    while (nsels-- > 0)
        offset_address(gp, ngp, *selp++);
}

#define OFFSET_ADDRESSES(table) \
    offset_addresses(table, NELEM(table), table##_selectors_to_patch, NELEM(table##_selectors_to_patch))

/*
 *  This is for gestalt entries that are hard-wired via configuration
 *  files.  They can't be altered by ReplaceGestalt.
 */

typedef struct {
    OSType selector;
    OSErr retval;
    uint32 value;
    int mutable_value;
} gestalt_list_entry_t;

PRIVATE gestalt_list_entry_t* gestalt_listp = NULL;
PRIVATE size_t listp_size = 0;

/* Older Power Mac software reads these fixed cache-information pointers
   directly. MacTest's consumer reads the L1 record's sizes at +16/+20 and
   unified flag at +24; version >= 0x0101 enables the later cache record.
   Supply a read-only virtual view: there is no emulated physical cache or
   secondary-cache board. In particular, do not invent the host CPU's caches.
   The records stay on the data bus, outside executable RAM and host aliases. */
PUBLIC int ROMlib_powerpc_cache_read(uint32 address, unsigned size, uint32* value)
{
    enum { record = 0x5fffe000, pointers = 0x5fffefd8 };

    uint32 offset;
    if (!value || (size != 1 && size != 2 && size != 4))
        return 0;
    if (address >= record && (uint64)address + size <= (uint64)record + 64)
        offset = address - record;
    else if (address >= pointers && (uint64)address + size <= (uint64)pointers + 28)
        offset = address - pointers + 64;
    else
        return 0;

    uint32 result = 0;
    for (unsigned i = 0; i < size; ++i) {
        unsigned byte = 0, index = offset + i;
        if (index == 25) /* Unified, zero-sized virtual L1 cache. */
            byte = 1;
        if (index >= 64 && index < 68)
            byte = ((uint32)record >> ((67 - index) * 8)) & 255;
        if (index >= 88 && index < 92)
            byte = ((uint32)record >> ((91 - index) * 8)) & 255;
        result = (result << 8) | byte;
    }
    *value = result;
    return 1;
}

PUBLIC void ROMlib_clear_gestalt_list(void)
{
    free(gestalt_listp);
    gestalt_listp = NULL;
    listp_size = 0;
}

PUBLIC void ROMlib_add_to_gestalt_list(OSType selector, OSErr retval, uint32 new_value)
{
    typeof(gestalt_listp) new_listp;

    new_listp = realloc(gestalt_listp, listp_size + sizeof *gestalt_listp);
    if (!new_listp)
        warning_unexpected("out of memory");
    else {
        typeof(gestalt_listp) entryp;

        gestalt_listp = new_listp;
        entryp = (typeof(entryp))((char*)gestalt_listp + listp_size);
        entryp->selector = selector;
        entryp->retval = retval;
        entryp->value = CL(new_value);
        entryp->mutable_value = FALSE;
        listp_size += sizeof *gestalt_listp;
    }
}

/* Static value selectors are distinct from procedure selectors and protected
   command-line overrides. Keep changes atomic when allocation fails. */
PUBLIC OSErr ROMlib_gestalt_value(OSType selector, uint32 value, unsigned action)
{
    size_t i, count = listp_size / sizeof *gestalt_listp;
    gestalt_list_entry_t* grown;
    for (i = 0; i < count; ++i)
        if (gestalt_listp[i].selector == selector)
            break;
    if (action < 1 || action > 4)
        return paramErr;
    if (action == 4)
        action = i == count ? 1 : 2;
    if (action == 1 &&
        (i < count || find_selector_on_list(selector) || find_selector_in_table(selector, gtable, NELEM(gtable))))
        return gestaltDupSelectorErr;
    if (action != 1 && (i == count || !gestalt_listp[i].mutable_value))
        return gestaltUndefSelectorErr;
    if (action == 3) {
        memmove(gestalt_listp + i, gestalt_listp + i + 1, (count - i - 1) * sizeof *gestalt_listp);
        listp_size -= sizeof *gestalt_listp;
        return noErr;
    }
    if (i == count) {
        if (listp_size > SIZE_MAX - sizeof *gestalt_listp)
            return memFullErr;
        grown = realloc(gestalt_listp, listp_size + sizeof *gestalt_listp);
        if (!grown)
            return memFullErr;
        gestalt_listp = grown;
        listp_size += sizeof *gestalt_listp;
    }
    gestalt_listp[i].selector = selector;
    gestalt_listp[i].retval = noErr;
    gestalt_listp[i].value = CL(value);
    gestalt_listp[i].mutable_value = TRUE;
    return noErr;
}

#if defined(SYN68K)
static syn68k_addr_t mac_info_callback(syn68k_addr_t pc, void* unused)
{
    (void)pc;
    (void)unused;
    syn68k_addr_t ret = POPADDR();
    uint32 address = READUL(EM_A7), capacity = READUL(EM_A7 + 4);
    EM_D0 = !address && capacity <= 1    ? ROMlib_mac_info_control(capacity)
            : address && capacity == 256 ? ROMlib_mac_info_take((char*)SYN68K_TO_US(address), capacity)
                                         : paramErr;
    return ret;
}

static syn68k_addr_t mount_image_callback(syn68k_addr_t pc, void* unused)
{
    (void)pc;
    (void)unused;
    syn68k_addr_t ret = POPADDR();
    EM_D0 = ROMlib_mount_image_dialog();
    return ret;
}

static syn68k_addr_t mount_image_path_callback(syn68k_addr_t pc, void* unused)
{
    (void)pc;
    (void)unused;
    syn68k_addr_t ret = POPADDR();
    uint32 address = READUL(EM_A7);
    char* path = NULL;
    OSErr error = paramErr;
    if (address) {
        error = ROMlib_filename_from_fsspec(&path, SYN68K_TO_US(address));
        if (!error)
            error = ROMlib_mount_image(path);
    }
    free(path);
    EM_D0 = error;
    return ret;
}

static void finder_host_put(unsigned char* out, uint32 value)
{
    value = CL(value);
    memcpy(out, &value, 4);
}

/* Each page has its own cursor. No process-global directory snapshot can go
   stale when another window enumerates or a copy changes the directory. */
static int finder_host_catalog(FSSpec* spec, unsigned index, unsigned char* out)
{
    CInfoPBRec pb = {0};
    FIELD_ASSIGN_RAW(pb.dirInfo.ioNamePtr, RM((StringPtr)spec->name));
    pb.dirInfo.ioVRefNum = spec->vRefNum;
    pb.dirInfo.ioDrDirID = spec->parID;
    OSErr error = PBGetCatInfo(&pb, FALSE);
    if (error)
        return error;
    if (!(pb.dirInfo.ioFlAttrib & 16))
        return dirNFErr;
    LONGINT directory = pb.dirInfo.ioDrDirID;
    unsigned count = 0;
    for (; count < FINDER_HOST_CATALOG_LIMIT; ++index) {
        unsigned char* record = out + 8 + count * FINDER_HOST_CATALOG_RECORD;
        memset(&pb, 0, sizeof pb);
        FIELD_ASSIGN_RAW(pb.dirInfo.ioNamePtr, RM(record));
        pb.dirInfo.ioVRefNum = spec->vRefNum;
        pb.dirInfo.ioDrDirID = directory;
        pb.dirInfo.ioFDirIndex = CW(index);
        error = PBGetCatInfo(&pb, FALSE);
        if (error == fnfErr) {
            index = 0;
            break;
        }
        if (error)
            return error;
        if (index == 32767)
            return dirFulErr;
        INTEGER flags = pb.dirInfo.ioFlAttrib & 16 ? pb.dirInfo.ioDrUsrWds.frFlags : pb.hFileInfo.ioFlFndrInfo.fdFlags;
        if (CW(flags) & fInvisible)
            continue;
        unsigned length = record[0];
        memmove(record, record + 1, length);
        record[length] = 0;
        finder_host_put(record + 256, !!(pb.dirInfo.ioFlAttrib & 16));
        finder_host_put(record + 260, CW(spec->vRefNum));
        memcpy(record + 264, &directory, 4);
        memcpy(record + 268, &pb.dirInfo.ioDrDirID, 4);
        memcpy(record + 272, &pb.hFileInfo.ioFlMdDat, 4);
        memcpy(record + 276, &pb.hFileInfo.ioFlFndrInfo.fdType, 4);
        memcpy(record + 280, &pb.hFileInfo.ioFlFndrInfo.fdCreator, 4);
        memcpy(record + 284, &pb.hFileInfo.ioFlLgLen, 4);
        memcpy(record + 288, &pb.hFileInfo.ioFlRLgLen, 4);
        ++count;
    }
    finder_host_put(out, count);
    finder_host_put(out + 4, index);
    return noErr;
}

/* Run Resource Manager traversal in native code, retaining decompression and
   resource-map semantics. Never retain a movable master-pointer dereference
   across a Resource Manager call. */
static int finder_host_icon(FSSpec* spec, unsigned char* out)
{
    uint32 creator, type;
    memcpy(&creator, out, 4);
    memcpy(&type, out + 4, 4);
    creator = CL(creator);
    type = CL(type);
    short saved = C_CurResFile();
    BOOLEAN loading = ResLoad;
    C_SetResLoad(FALSE);
    short file = C_FSpOpenResFile(spec, fsRdPerm);
    C_SetResLoad(TRUE);
    int found = 0, id = 0;
    unsigned present = 0;
    if (file != -1) {
        int refs = C_Count1Resources(TICK("FREF"));
        int bundles = C_Count1Resources(TICK("BNDL"));
        uint32 types[1024];
        unsigned type_count = 0;
        for (int i = 1; i <= refs; ++i) {
            Handle ref = C_Get1IndResource(TICK("FREF"), i);
            if (!ref || GetHandleSize(ref) < 6)
                continue;
            unsigned char* bytes = (unsigned char*)STARH(ref);
            uint32 code;
            memcpy(&code, bytes, 4);
            code = CL(code);
            unsigned local = bytes[4] * 256u + bytes[5];
            for (int j = 1; j <= bundles; ++j) {
                Handle bundle = C_Get1IndResource(TICK("BNDL"), j);
                if (!bundle)
                    continue;
                int matched;
                if (!romaw_finder_bundle_icon_id((unsigned char*)STARH(bundle), GetHandleSize(bundle), creator, local,
                                                 &matched))
                    continue;
                if (code == type) {
                    id = matched;
                    found = 1;
                }
                unsigned k;
                for (k = 0; k < type_count && types[k] != code; ++k)
                    ;
                if (k == type_count && type_count < NELEM(types))
                    types[type_count++] = code;
                break;
            }
        }
        if (found) {
            static const uint32 kinds[] = {TICK("ICN#"), TICK("ics#"), TICK("icl4"),
                                           TICK("icl8"), TICK("ics4"), TICK("ics8")};
            static const unsigned sizes[] = {256, 64, 512, 1024, 128, 256};
            unsigned offset = 8;
            for (unsigned i = 0; i < NELEM(kinds); ++i) {
                Handle resource = C_Get1Resource(kinds[i], id);
                if (resource && GetHandleSize(resource) >= sizes[i]) {
                    memcpy(out + offset, STARH(resource), sizes[i]);
                    present |= 1u << i;
                }
                offset += sizes[i];
            }
        }
        C_CloseResFile(file);
        if (type_count && (type == TICK("APPL") || type == TICK("FNDR"))) {
            char* path = NULL;
            if (!ROMlib_filename_from_fsspec(&path, spec))
                filetype_map_register_application(path, creator, types, type_count, 1);
            free(path);
        }
    }
    C_UseResFile(saved);
    C_SetResLoad(loading);
    finder_host_put(out + 2248, present);
    return (present & 1) ? 1 : -2;
}

static syn68k_addr_t finder_metadata_callback(syn68k_addr_t pc, void* unused)
{
    (void)pc;
    (void)unused;
    syn68k_addr_t ret = POPADDR();
    uint32 action = READUL(EM_A7), address = READUL(EM_A7 + 4);
    uint32 data = READUL(EM_A7 + 8), count = READUL(EM_A7 + 12);
    EM_D0 = paramErr;
    if (action == FINDER_HOST_CATALOG_ACTION || action == FINDER_HOST_ICON_ACTION) {
        unsigned bytes = action == FINDER_HOST_CATALOG_ACTION ? FINDER_HOST_CATALOG_BYTES : FINDER_HOST_ICON_BYTES;
        if (!address || !data || (uint64)address + sizeof(FSSpec) > ROMlib_guest_memory_size ||
            (uint64)data + bytes > ROMlib_guest_memory_size ||
            ((uint64)address < (uint64)data + bytes && (uint64)data < (uint64)address + sizeof(FSSpec)) ||
            (action == FINDER_HOST_CATALOG_ACTION && (!count || count > 32767)) ||
            (action == FINDER_HOST_ICON_ACTION && count != bytes))
            return ret;
        FSSpec* spec = SYN68K_TO_US(address);
        if (spec->name[0] >= sizeof spec->name)
            return ret;
        EM_D0 = action == FINDER_HOST_CATALOG_ACTION ? finder_host_catalog(spec, count, SYN68K_TO_US(data))
                                                     : finder_host_icon(spec, SYN68K_TO_US(data));
        return ret;
    }
    if (action == 5 && count <= 1) {
        EM_D0 = ROMlib_catalog_skip_counts;
        ROMlib_catalog_skip_counts = count;
        ROMlib_sidecar_catalog_batch(count != 0);
        return ret;
    }
    if (action == 4) {
        EM_D0 = romaw_finder_metadata(action, NULL, NULL, 0);
        return ret;
    }
    uint64 bytes = action < 2 ? 20 : (uint64)count * 268;
    if (action > 3 || !address || !data || count > 65536 ||
        (uint64)address + sizeof(FSSpec) > ROMlib_guest_memory_size || (uint64)data + bytes > ROMlib_guest_memory_size)
        return ret;
    FSSpec* spec = SYN68K_TO_US(address);
    if (spec->name[0] >= sizeof spec->name)
        return ret;
    char* path = NULL;
    OSErr error = ROMlib_filename_from_fsspec(&path, spec);
    if (!error) {
        HVCB* volume = ROMlib_vcbbybiggestunixname(path);
        if (volume && !volume->vcbCTRef && (!(action & 1) || !ROMlib_image_volume_kind(volume)))
            EM_D0 = romaw_finder_metadata(action, path, SYN68K_TO_US(data), count);
        else
            EM_D0 = -2;
    }
    else
        EM_D0 = -2;
    free(path);
    return ret;
}

/* Gestalt exAI: long operation (unsigned action, OSType creator, OSType type,
                               FSSpec *result).
   Action 0 queries ownership; 1 advances discovery; 2 reports pending work. */
static syn68k_addr_t indexed_application_callback(syn68k_addr_t pc, void* unused)
{
    (void)pc;
    (void)unused;
    syn68k_addr_t ret = POPADDR();
    uint32 action = READUL(EM_A7), creator = READUL(EM_A7 + 4);
    uint32 type = READUL(EM_A7 + 8), address = READUL(EM_A7 + 12);
    EM_D0 = paramErr;
    if (action == 1)
        EM_D0 = ROMlib_application_registry_idle();
    else if (action == 2)
        EM_D0 = ROMlib_application_registry_pending();
    else if (action == 0 && address && (uint64)address + sizeof(FSSpec) <= ROMlib_guest_memory_size)
        EM_D0 = ROMlib_application_spec_cached(creator, type, SYN68K_TO_US(address));
    return ret;
}

static syn68k_addr_t bundle_application_callback(syn68k_addr_t pc, void* unused)
{
    (void)pc;
    (void)unused;
    syn68k_addr_t ret = POPADDR();
    uint32 address = READUL(EM_A7), creator = READUL(EM_A7 + 4);
    uint32 types_address = READUL(EM_A7 + 8), count = READUL(EM_A7 + 12);
    EM_D0 = paramErr;
    if (!address || !types_address || !count || count > 1024 ||
        (uint64)address + sizeof(FSSpec) > ROMlib_guest_memory_size ||
        (uint64)types_address + count * 4 > ROMlib_guest_memory_size)
        return ret;
    FSSpec* spec = SYN68K_TO_US(address);
    if (spec->name[0] >= sizeof spec->name)
        return ret;
    uint32* types = malloc(count * sizeof *types);
    char* path = NULL;
    if (!types) {
        EM_D0 = memFullErr;
        return ret;
    }
    for (unsigned i = 0; i < count; ++i)
        types[i] = READUL(types_address + i * 4);
    OSErr error = ROMlib_filename_from_fsspec(&path, spec);
    if (!error)
        error = filetype_map_register_application(path, creator, types, count, 1) ? noErr : ioErr;
    free(path);
    free(types);
    EM_D0 = error;
    return ret;
}

/* Gestalt 'exTr' returns a C-callable 68K function:
   long operation (long action, const FSSpec *item).
   It returns noErr, unimpErr for non-host volumes, or an operation error.
   Opening/emptying uses the native Trash window and confirmation UI. */
static syn68k_addr_t host_trash_callback(syn68k_addr_t pc, void* unused)
{
    (void)pc;
    (void)unused;
    syn68k_addr_t ret = POPADDR();
    unsigned action = READUL(EM_A7);
    uint32 address = READUL(EM_A7 + 4);
    OSErr err = paramErr;
    if (!action)
        err = noErr;
    else if (action == 1 || action == 3)
        err = ROMlib_host_trash(action, 0, NULL) ? ioErr : noErr;
    else if (action == 2 && address && (uint64)address + sizeof(FSSpec) <= ROMlib_guest_memory_size) {
        FSSpec* spec = (FSSpec*)SYN68K_TO_US(address);
        char *path = NULL, *metadata = NULL, *resource = NULL;
        if (spec->name[0] >= sizeof spec->name) {
            EM_D0 = paramErr;
            return ret;
        }
        if (spec->parID != CLC(1) && ROMlib_filename_from_fsspec(&path, spec) == noErr) {
            char* name = strrchr(path, '/');
            if (name)
                metadata = ROMlib_sidecar_name(path, name + 1);
            if (metadata)
                resource = ROMlib_sidecar_resource_name(metadata);
            if (!metadata || !resource) {
                free(resource);
                free(metadata);
                free(path);
                EM_D0 = memFullErr;
                return ret;
            }
            const char* paths[3];
            unsigned count = 0;
            if (metadata && !access(metadata, F_OK))
                paths[count++] = metadata;
            if (resource && (!metadata || strcmp(resource, metadata)) && !access(resource, F_OK))
                paths[count++] = resource;
            paths[count++] = path;
            err = ROMlib_host_trash(action, count, paths) ? ioErr : noErr;
        }
        else
            err = spec->parID == CLC(1) ? paramErr : (OSErr)-4;
        free(resource);
        free(metadata);
        free(path);
    }
    EM_D0 = (int32)err;
    return ret;
}
#endif

A2(PUBLIC trap, OSErrRET, Gestalt, OSType, selector, LONGINT*, responsep)
{
    static boolean_t been_here = FALSE;
    /* ROMAW Finder extension: EV + signed volume reference -> host drive kind.
     Do not change standard File Manager driver references to encode host media. */
    if (((uint32)selector >> 16) == 0x4556 || ((uint32)selector >> 16) == 0x4548) {
        HVCB* volume = ROMlib_vcbbyvrn((int16)selector);
        if (!volume)
            return gestaltUnknownErr;
        if (((uint32)selector >> 16) == 0x4548) {
            *responsep = CL(volume->vcbCTRef != 0);
            return noErr;
        }
        int kind = ROMlib_image_volume_kind(volume);
        if (!kind)
            kind = 1;
        if (!volume->vcbCTRef) {
            const char* path = ((VCBExtra*)volume)->unixname;
#if defined(CYGWIN32)
            char root[4] = {0};
            if (path && path[0] && path[1] == ':') {
                root[0] = path[0];
                root[1] = ':';
                root[2] = '/';
                path = root;
            }
#endif
            if (path && kind < 6)
                kind = romaw_host_volume_kind(path);
        }
        *responsep = CL(kind);
        return noErr;
    }

#if defined(SYN68K)
    if (selector == TICK("exMD")) {
        static syn68k_addr_t callback;
        if (!callback)
            callback = callback_install(finder_metadata_callback, NULL);
        if (!callback)
            return memFullErr;
        *responsep = CL(callback);
        return noErr;
    }
    if (selector == TICK("exMp")) {
        static syn68k_addr_t callback;
        if (!callback)
            callback = callback_install(mount_image_path_callback, NULL);
        if (!callback)
            return memFullErr;
        *responsep = CL(callback);
        return noErr;
    }
    if (selector == TICK("exIn")) {
        static syn68k_addr_t callback;
        if (!callback)
            callback = callback_install(mac_info_callback, NULL);
        if (!callback)
            return memFullErr;
        *responsep = CL(callback);
        return noErr;
    }
    if (selector == TICK("exMi")) {
        static syn68k_addr_t callback;
        if (!callback)
            callback = callback_install(mount_image_callback, NULL);
        if (!callback)
            return memFullErr;
        *responsep = CL(callback);
        return noErr;
    }
    if (selector == TICK("exAI")) {
        static syn68k_addr_t callback;
        if (!callback)
            callback = callback_install(indexed_application_callback, NULL);
        if (!callback)
            return memFullErr;
        *responsep = CL(callback);
        return noErr;
    }
    if (selector == TICK("exBa")) {
        static syn68k_addr_t callback;
        if (!callback)
            callback = callback_install(bundle_application_callback, NULL);
        if (!callback)
            return memFullErr;
        *responsep = CL(callback);
        return noErr;
    }
    if (selector == TICK("exTr") && !ROMlib_host_trash(0, 0, NULL)) {
        static syn68k_addr_t callback;
        if (!callback)
            callback = callback_install(host_trash_callback, NULL);
        if (!callback)
            return memFullErr;
        *responsep = CL(callback);
        return noErr;
    }
#endif
#if defined(CYGWIN32) && defined(SDL) && defined(SYN68K)
    {
        extern int ROMlib_ppc_gestalt(OSType, LONGINT*);
        if (ROMlib_ppc_gestalt(selector, responsep))
            return noErr;
    }
#endif

    if (selector == gestaltAddressingModeAttr) {
        *responsep =
            CL((1 << gestalt32BitSysZone) | (1 << gestalt32BitCapable) | (MMU32Bit ? 1 << gestalt32BitAddressing : 0));
        return noErr;
    }

    warning_trace_info("IN: sel `%c%c%c%c'", (selector >> 24) & 0xFF, (selector >> 16) & 0xFF, (selector >> 8) & 0xFF,
                       (selector) & 0xFF);
#if defined(CYGWIN32)
    if ((uint32)selector == 0xb7d20e84) {
        OSErrRET retval;

        warning_trace_info("about to dongle_query");
        retval = dongle_query(responsep);
        warning_trace_info("dongle_queried");
        return retval;
    }
#endif

    {
        gestalt_list_entry_t *p, *ep;

        for (p = gestalt_listp, ep = (typeof(ep))((char*)p + listp_size); p != ep && p->selector != selector; ++p)
            ;
        if (p != ep) {
            *responsep = p->value;
            return p->retval;
        }
    }

    if (!been_here) {
        gestalt_set_physical_gestalt_callback();
        OFFSET_ADDRESSES(gtable);
        OFFSET_ADDRESSES(phystable);
        been_here = TRUE;
    }
    return gestalt_helper(selector, responsep, TRUE, gtable, NELEM(gtable));
}

P2(PUBLIC pascal trap, OSErrRET, PhysicalGestalt, OSType, selector, LONGINT*, responsep)
{
    OSErrRET retval;

    switch (selector) {
    case gestaltScreenSize:
        replace_physgestalt_selector(gestaltScreenSize, ((vdriver_height << 16) | (uint16)vdriver_width));
        break;
    default:
        break;
    }

    retval = gestalt_helper(selector, responsep, FALSE, phystable, NELEM(phystable));
    if (retval == gestaltUndefSelectorErr)
        retval = physicalUndefSelectorErr;
    return retval;
}

P2(PUBLIC pascal trap, OSErrRET, GestaltTablesOnly, OSType, selector, LONGINT*, responsep)
{
    return gestalt_helper(selector, responsep, FALSE, gtable, NELEM(gtable));
}

PRIVATE BOOLEAN syszone_p(ProcPtr p)
{
    THz syszone;

    syszone = MR(SysZone);
    return ((uintptr_t)p >= (uintptr_t)&syszone->heapData && (uintptr_t)p < (uintptr_t)PPR(syszone->bkLim));
}

PRIVATE OSErr new_link(OSType selector, ProcPtr selFunc)
{
    OSErr retval;

    if (!syszone_p(selFunc))
        retval = gestaltLocationErr;
    else {
        ZONE_SAVE_EXCURSION(SysZone, {
            gestalt_link_t* gp;

            gp = (gestalt_link_t*)NewPtr(sizeof(*gp));
            if (!gp)
                retval = memFullErr;
            else {
                retval = noErr;
                gp->selector = selector;
                gp->selectorFunction = selFunc;
                gp->next = gestalt_head;
                gestalt_head = gp;
            }
        });
    }
    return retval;
}

A2(PUBLIC trap, OSErrRET, NewGestalt, OSType, selector, ProcPtr, selFunc)
{
    OSErr retval;

    size_t i, count = listp_size / sizeof *gestalt_listp;
    for (i = 0; i < count && gestalt_listp[i].selector != selector; ++i)
        ;
    if (i < count || find_selector_on_list(selector) || find_selector_in_table(selector, gtable, NELEM(gtable)))
        retval = gestaltDupSelectorErr;
    else
        retval = new_link(selector, selFunc);
    return retval;
}

A3(PUBLIC trap, OSErrRET, ReplaceGestalt, OSType, selector, ProcPtr, selFunc, ProcPtr*, oldSelFuncp)
{
    OSErr retval;
    gestalt_link_t* gp;

    gp = find_selector_on_list(selector);
    if (gp) {
        if (syszone_p(selFunc)) {
            *oldSelFuncp = gp->selectorFunction;
            gp->selectorFunction = selFunc;
            retval = noErr;
        }
        else
            retval = gestaltLocationErr;
    }
    else {
        gestaltentry_t* gep;

        gep = find_selector_in_table(selector, gtable, NELEM(gtable));
        if (!gep)
            retval = gestaltUndefSelectorErr;
        else {
            *oldSelFuncp = P_GestaltTablesOnly;
            retval = new_link(selector, selFunc);
        }
    }
    return retval;
}
