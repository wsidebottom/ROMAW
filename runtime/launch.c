/*+===================================================================
  File:      launch.c

  Summary:   Implements launch support for Macintosh startup, traps and runtime services.

  Classes:   None.

  Functions: NewLaunch, ROMlib_filename_from_fsspec, ROMlib_find_cfrg, ROMlib_launch_path_from_fsspec, ROMlib_set_ppc,
             SFSaveDisk_Update, empty_timer_queues, remalloc, reset_string

  Origin:    Existing repository source in executor/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1994 - 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "rsys/cooperative.h"
#include "rsys/ecf.h"
#include "rsys/application_event.h"
#include "rsys/application_instance.h"
#include "finder/image_volumes.h"
#include "rsys/configprefs.h"
#include "rsys/system_error.h"

#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "QuickTime.h"
#include "ResourceMgr.h"
#include "SegmentLdr.h"
#include "MemoryMgr.h"
#include "StdFilePkg.h"
#include "EventMgr.h"
#include "VRetraceMgr.h"
#include "OSUtil.h"
#include "FontMgr.h"
#include "ScrapMgr.h"
#include "ToolboxUtil.h"
#include "FileMgr.h"
#include "synppc/romaw.h"
#include "ControlMgr.h"
#include "DeviceMgr.h"
#include "rsys/device.h"
#include "SoundDvr.h"
#include "TextEdit.h"
#include "SysErr.h"
#include "MenuMgr.h"
#include "ScriptMgr.h"
#include "DeskMgr.h"
#include "AppleTalk.h"
#include "PrintMgr.h"
#include "StartMgr.h"
#include "ToolboxEvent.h"
#include "TimeMgr.h"
#include "ProcessMgr.h"
#include "AppleEvents.h"
#include "Gestalt.h"
#include "Package.h"
#include "AliasMgr.h"

#include "rsys/trapglue.h"
#include "rsys/file.h"
#include "rsys/sounddriver.h"
#include "rsys/pstuff.h"
#include "rsys/jumpvectors.h"
#include "rsys/prefs.h"
#include "rsys/prefpanel.h"
#include "rsys/flags.h"
#include "rsys/aboutpanel.h"
#include "rsys/segment.h"
#include "rsys/misc.h"
#include "rsys/pstuff.h"
#include "rsys/tesave.h"
#include "rsys/blockinterrupts.h"
#include "rsys/resource.h"
#include "rsys/hfs.h"
#include "rsys/hostdialog.h"
#include "rsys/osutil.h"
#include "rsys/memory_layout.h"
#include "rsys/stdfile.h"
#include "rsys/notmac.h"
#include "rsys/ctl.h"
#include "rsys/refresh.h"

#include "rsys/options.h"
#include "rsys/cquick.h"
#include "rsys/picture.h"
#include "rsys/desk.h"
#include "rsys/parse.h"
#include "rsys/romaw.h"
#include "rsys/crc.h"
#include "rsys/float.h"
#include "rsys/mman.h"
#include "rsys/vdriver.h"
#include "rsys/font.h"
#include "rsys/emustubs.h"

#include "rsys/adb.h"
#include "rsys/print.h"
#include "rsys/gestalt.h"
#include "rsys/osevent.h"

#include "rsys/cfm.h"
#include "rsys/launch.h"
#include "rsys/icon.h"
#include "rsys/local_charset.h"
#include "rsys/version.h"
#include "rsys/appearance.h"

/* The explicit -ppc option overrides the application preference. */
PRIVATE boolean_t ppc_override;

PUBLIC void ROMlib_set_ppc(boolean_t val)
{
    ppc_override = val;
}

FILE* configfile;

int32 ROMlib_options;

static int16 name0stripappl(StringPtr name)
{
    char* p;
    int16 retval;

    retval = name[0];
    if (name[0] >= 5) {
        p = (char*)name + name[0] + 1 - 5;
        if (p[0] == '.' && (p[1] == 'a' || p[1] == 'A') && (p[2] == 'p' || p[2] == 'P') &&
            (p[3] == 'p' || p[3] == 'P') && (p[4] == 'l' || p[4] == 'L'))
            retval -= 5;
    }
    return retval;
}

/* Application settings come exclusively from the shared preferences file. */

PUBLIC int ROMlib_nowarn32;

PUBLIC char* ROMlib_configfilename = NULL;
char ROMlib_configsection[CONFIG_PREFS_SECTION_SIZE];

PUBLIC int ROMlib_pretend_help = FALSE;
PUBLIC int ROMlib_pretend_alias = FALSE;
PUBLIC int ROMlib_pretend_edition = FALSE;
PUBLIC int ROMlib_pretend_script = FALSE;

void remalloc(char** strp)
{
    char* new_string;
    long len;

    if (*strp) {
        len = strlen(*strp) + 1;
        new_string = malloc(len);
        if (new_string)
            memcpy(new_string, *strp, len);
        *strp = new_string;
    }
}

void reset_string(char** strp)
{
    if (*strp)
        free(*strp);
    *strp = 0;
}

int ROMlib_desired_bpp;
int ROMlib_display_scale_override;

PRIVATE void ParseConfigFile(StringPtr exefname, OSType creator, OSType file_type)
{
    int strwidth;
    char* newtitle;
    char* dot;
    INTEGER fname0;

    ROMlib_preferences_capture_defaults();
    fname0 = name0stripappl(exefname);
    free(ROMlib_configfilename);
    ROMlib_configfilename = malloc(strlen(ROMlib_userdir) + sizeof "/ROMAW.prefs");
    configfile = NULL;
    if (ROMlib_configfilename) {
        sprintf(ROMlib_configfilename, "%s/ROMAW.prefs", ROMlib_userdir);
        if (file_type)
            config_prefs_app_section(file_type, creator, ROMlib_configsection);
        else
            strcpy(ROMlib_configsection, "Defaults");
        configfile = config_prefs_open(ROMlib_configfilename, ROMlib_configsection);
    }
    if (configfile) {
        yyparse();
        ROMlib_noclock = !!(ROMlib_options & ROMLIB_NOCLOCK_BIT);

        if (ROMlib_get_appearance() == appearance_win3)
            ROMlib_options |= ROMLIB_RECT_SCREEN_BIT;

        if (ROMlib_options & ROMLIB_BLIT_OS_BIT)
            ROMlib_WriteWhen(WriteInOSEvent);
        if (ROMlib_options & ROMLIB_BLIT_TRAP_BIT)
            ROMlib_WriteWhen(WriteAtEndOfTrap);
        if (ROMlib_options & ROMLIB_BLIT_OFTEN_BIT)
            ROMlib_WriteWhen(WriteInBltrgn);
        if (ROMlib_options & ROMLIB_REFRESH_BIT)
            ROMlib_refresh = 10;
        if (ROMlib_options & ROMLIB_DIRTY_VARIANT_BIT)
            ROMlib_dirtyvariant = TRUE;
        else
            ROMlib_dirtyvariant = FALSE;
        /* #warning ROMlib_passpostscript wired to TRUE */
        ROMlib_passpostscript = TRUE;
        if (ROMlib_options & ROMLIB_NEWLINETOCR_BIT)
            ROMlib_newlinetocr = TRUE;
        else
            ROMlib_newlinetocr = FALSE;
        if (ROMlib_options & ROMLIB_DIRECTDISKACCESS_BIT)
            ROMlib_directdiskaccess = TRUE;
        else
            ROMlib_directdiskaccess = FALSE;
        if (ROMlib_options & ROMLIB_NOWARN32_BIT)
            ROMlib_nowarn32 = TRUE;
        else
            ROMlib_nowarn32 = FALSE;
        if (ROMlib_options & ROMLIB_FLUSHOFTEN_BIT)
            ROMlib_flushoften = TRUE;
        else
            ROMlib_flushoften = FALSE;

        if (ROMlib_options & ROMLIB_PRETEND_HELP_BIT)
            ROMlib_pretend_help = TRUE;
        else
            ROMlib_pretend_help = FALSE;

        if (ROMlib_options & ROMLIB_PRETEND_ALIAS_BIT)
            ROMlib_pretend_alias = TRUE;
        else
            ROMlib_pretend_alias = FALSE;

        if (ROMlib_options & ROMLIB_PRETEND_EDITION_BIT)
            ROMlib_pretend_edition = TRUE;
        else
            ROMlib_pretend_edition = FALSE;

        if (ROMlib_options & ROMLIB_PRETEND_SCRIPT_BIT)
            ROMlib_pretend_script = TRUE;
        else
            ROMlib_pretend_script = FALSE;

        if (ROMlib_desired_bpp)
            SetDepth(MR(MainDevice), ROMlib_desired_bpp, 0, 0);
        fclose(configfile);
    }

#if !defined(VDRIVER_DISPLAYED_IN_WINDOW)

#define SetTitle(x)            \
    do {                       \
        ROMlib_WindowName = x; \
    } while (0)

#else

#define SetTitle(x)                     \
    do {                                \
        char* _x;                       \
        char* _title;                   \
        int len;                        \
                                        \
        _x = (x);                       \
        len = strlen(_x) + 1;           \
        _title = alloca(len);           \
        memcpy(_title, _x, len);        \
        ROMlib_localize_string(_title); \
        ROMlib_SetTitle(_title);        \
    } while (0)

#endif

    if (ROMlib_WindowName)
        SetTitle(ROMlib_WindowName);
    else {
        strwidth = fname0;
        newtitle = alloca(strwidth + 1);
        memcpy(newtitle, exefname + 1, strwidth);
        newtitle[strwidth] = 0;
        dot = strrchr(newtitle, '.');
        if (dot && (strcmp(dot, ".appl") == 0 || strcmp(dot, ".APPL") == 0))
            *dot = 0;
        SetTitle(newtitle);
    }

    remalloc(&ROMlib_WindowName);
    remalloc(&ROMlib_Comments);
}

PRIVATE void beginexecutingat(LONGINT startpc)
{
#if defined(SYN68K)
#define ALINETRAPNUMBER 0xA
    trap_install_handler(ALINETRAPNUMBER, alinehandler, (void*)0);
#endif

    EM_D0 = 0;
    EM_D1 = 0xFFFC000;
    EM_D2 = 0;
    EM_D3 = 0x800008;
    EM_D4 = 0x408;
    EM_D5 = 0x10204000;
    EM_D6 = 0x7F0000;
    EM_D7 = 0x80000800;

    EM_A0 = 0x3EF796;
    EM_A1 = 0x910;
    EM_A2 = EM_D3;
    EM_A3 = 0;
    EM_A4 = 0;
    EM_A5 = CL((LONGINT)CurrentA5); /* was smashed when we
					   initialized above */
    EM_A6 = 0x1EF;

    ROMlib_cooperative_stack_low = ROMlib_cooperative_stack_high = 0;
    ROMlib_cooperative_interpreter = NULL;
    ROMlib_cooperative_depth = 0;
    ROMlib_cooperative_critical = 0;
    CALL_EMULATOR(startpc);
    C_ExitToShell();
}

static OSErr claim_application_instance(FSSpec* application, int* reused)
{
    char *path = NULL, *identity = NULL;
    OSErr error = ROMlib_filename_from_fsspec(&path, application);
    if (!error)
        identity = ROMlib_instance_file_identity(path, NULL);
    free(path);
    if (!identity) {
        FinderImageVolume volume;
        for (uint32_t i = 0; ROMlib_finder_image_volume(i, &volume); ++i) {
            if (volume.reference != CW(application->vRefNum))
                continue;
            char name[32], suffix[80];
            unsigned length = application->name[0];
            if (length >= sizeof name)
                return bdNamErr;
            memcpy(name, application->name + 1, length);
            name[length] = 0;
            FinderImageEntry entry;
            error = ROMlib_finder_image_info(volume.reference, CL(application->parID), name, &entry);
            if (error)
                return error;
            snprintf(suffix, sizeof suffix, "/hfs/%d/%lu", volume.partition, (unsigned long)entry.id);
            identity = ROMlib_instance_file_identity(volume.source, suffix);
            break;
        }
    }
    if (!identity)
        return error ? error : memFullErr;
    INTEGER message = 0, encoded_count = 0;
    CountAppFiles(&message, &encoded_count);
    int count = CW(encoded_count);
    if (count < 0 || count > 1024) {
        free(identity);
        return paramErr;
    }
    char** documents = calloc(count ? count : 1, sizeof *documents);
    if (!documents) {
        free(identity);
        return memFullErr;
    }
    error = noErr;
    for (int i = 0; !error && i < count; ++i) {
        AppFile file;
        FSSpec spec;
        GetAppFiles(i + 1, &file);
        error = FSMakeFSSpec(CW(file.vRefNum), 0, file.fName, &spec);
        if (!error)
            error = ROMlib_launch_path_from_fsspec(&documents[i], &spec);
    }
    if (!error) {
        uint32_t status = ROMlib_instance_claim(identity, (const char* const*)documents, count, reused);
        if (status) {
            char message[256];
            snprintf(message, sizeof message,
                     "Cannot contact or register this application instance (Windows error %lu). "
                     "A duplicate application has not been started.",
                     (unsigned long)status);
            host_dialog_error(message);
            error = abortErr;
        }
    }
    for (int i = 0; i < count; ++i)
        free(documents[i]);
    free(documents);
    free(identity);
    return error;
}

PUBLIC LONGINT ROMlib_appbit;

size_info_t size_info;

#define VERSFMT "(0x%02x, 0x%02x, 0x%02x, 0x%02x, %d)"
#define VERSSIZE(vp) (sizeof(VERSFMT) + (vp)->shortname[0] + 6)

LONGINT ROMlib_creator;

PUBLIC void* ROMlib_foolgcc; /* to force the alloca to be done */

PUBLIC void SFSaveDisk_Update(INTEGER vrefnum, Str255 filename)
{
    ParamBlockRec pbr;
    Str255 save_name;

    str255assign(save_name, filename);
    FIELD_ASSIGN_RAW(pbr.volumeParam.ioNamePtr, (StringPtr)RM((uintptr_t)save_name));
    pbr.volumeParam.ioVolIndex = CWC(-1);
    pbr.volumeParam.ioVRefNum = CW(vrefnum);
    PBGetVInfo(&pbr, FALSE);
    SFSaveDisk = CW(-CW(pbr.volumeParam.ioVRefNum));
}

PUBLIC uint32 ROMlib_version_long;

PRIVATE boolean_t launch_handle_body_valid_p(Handle h)
{
    return h && HIDDEN_RAW(*(h));
}

PRIVATE uint32 finder_required_system(const FInfo* info)
{
    if (CL(info->fdType) != TICK("FNDR") || CL(info->fdCreator) != TICK("MACS"))
        return 0;
    Handle version = Get1Resource(TICK("vers"), 1);
    if (!version)
        version = Get1Resource(TICK("vers"), 2);
    if (!launch_handle_body_valid_p(version) || GetHandleSize(version) < 6)
        return 0;
    const unsigned char* bytes = (const unsigned char*)STARH(version);
    /* Finder is a system component whose release identifies its OS contract.
       An ordinary application's own version does not identify its minimum OS. */
    uint32 required = (uint32)bytes[0] << 8 | bytes[1];
    return required >= 0x860 && required <= 0x922 ? required : 0;
}

PRIVATE boolean_t cfrg_match(const cfir_t* cfirp, OSType arch_x, uint8 type_x, Str255 name)
{
    boolean_t retval;

    retval = (CFIR_ISA_X(cfirp) == arch_x && CFIR_TYPE_X(cfirp) == type_x &&
              (!name[0] || EqualString(name, (StringPtr)CFIR_NAME(cfirp), FALSE, TRUE)));
    return retval;
}

PUBLIC cfir_t* ROMlib_find_cfrg(Handle cfrg, OSType arch, uint8 type, StringPtr name)
{
    cfrg_resource_t* cfrgp;
    Size cfrg_size;
    int n_descripts;
    cfir_t* cfirp;
    char* cfrg_end;
    OSType desired_arch_x;
    uint8 type_x;
    cfir_t* retval;

    if (!cfrg || !STARH(cfrg)) {
        fprintf(stderr, "trace: ROMlib_find_cfrg empty handle cfrg=%p trap=0x%04x\n", (void*)cfrg,
                (unsigned)mostrecenttrap);
        fflush(stderr);
        return NULL;
    }
    cfrg_size = GetHandleSize(cfrg);
    if (cfrg_size < (Size)sizeof *cfrgp) {
        fprintf(stderr, "trace: ROMlib_find_cfrg short handle cfrg=%p size=%ld trap=0x%04x\n", (void*)cfrg,
                (long)cfrg_size, (unsigned)mostrecenttrap);
        fflush(stderr);
        return NULL;
    }
    cfrgp = (cfrg_resource_t*)STARH(cfrg);
    cfirp = (cfir_t*)((char*)cfrgp + sizeof *cfrgp);
    cfrg_end = (char*)cfrgp + cfrg_size;
    desired_arch_x = CL(arch);
    type_x = CB(type);
    for (n_descripts = CFRG_N_DESCRIPTS(cfrgp); n_descripts > 0; --n_descripts) {
        int cfir_length;

        if ((size_t)(cfrg_end - (char*)cfirp) < sizeof *cfirp) {
            fprintf(stderr, "trace: ROMlib_find_cfrg descriptor outside cfrg=%p cfir=%p end=%p trap=0x%04x\n",
                    (void*)cfrg, (void*)cfirp, (void*)cfrg_end, (unsigned)mostrecenttrap);
            fflush(stderr);
            return NULL;
        }
        cfir_length = CFIR_LENGTH(cfirp);
        if (cfir_length < (int)sizeof *cfirp || cfir_length > cfrg_end - (char*)cfirp ||
            CFIR_NAME(cfirp)[0] > cfir_length - (int)sizeof *cfirp) {
            fprintf(stderr,
                    "trace: ROMlib_find_cfrg bad descriptor length cfrg=%p cfir=%p length=%d end=%p trap=0x%04x\n",
                    (void*)cfrg, (void*)cfirp, cfir_length, (void*)cfrg_end, (unsigned)mostrecenttrap);
            fflush(stderr);
            return NULL;
        }
        if (cfrg_match(cfirp, desired_arch_x, type_x, name))
            break;
        cfirp = (cfir_t*)((char*)cfirp + cfir_length);
    }
    retval = n_descripts > 0 ? cfirp : 0;

    return retval;
}

PRIVATE void cfm_launch(Handle cfrg0, OSType desired_arch, FSSpecPtr fsp)
{
    cfir_t* cfirp;

    cfirp = ROMlib_find_cfrg(cfrg0, desired_arch, kApplicationCFrag, (StringPtr) "");

#if defined(CYGWIN32) && defined(SDL) && defined(SYN68K)
    if (cfirp && desired_arch == kMotorola68KArch && CFIR_LOCATION(cfirp) == kOnDiskSegmented) {
        if (!ROMlib_launch_cfm68k_resource(fsp))
            ROMlib_exit = 2;
    }
    else if (cfirp && CFIR_LOCATION(cfirp) == kOnDiskFlat) {
        int ok = desired_arch == kMotorola68KArch
                     ? ROMlib_launch_cfm68k_fragment(fsp, CFIR_OFFSET_TO_FRAGMENT(cfirp), CFIR_FRAGMENT_LENGTH(cfirp))
                     : ROMlib_launch_ppc_fragment(fsp, CFIR_OFFSET_TO_FRAGMENT(cfirp), CFIR_FRAGMENT_LENGTH(cfirp));
        if (!ok)
            ROMlib_exit = 2;
    }
    else {
        system_error("This code fragment has an unsupported storage format.", 0, "OK", NULL, NULL, NULL, NULL, NULL);
        ROMlib_exit = 2;
    }
#endif

    C_ExitToShell();
}

PUBLIC int ROMlib_uaf;

PUBLIC launch_failure_t ROMlib_launch_failure = launch_no_failure;
PUBLIC INTEGER ROMlib_exevrefnum;

PRIVATE void launchchain(StringPtr fName, INTEGER vRefNum, BOOLEAN resetmemory, LaunchParamBlockRec* lpbp)
{
    ROMlib_ppc_begin_launch();
    OSErr err;
    FInfo finfo;
    Handle code0;
    Handle cfrg0;
    OSType fragment_arch = 0;
    Handle h;
    vers_t* vp;
    char *versbuf, *namebuf;
    int namelen;
    LONGINT abovea5, belowa5, jumplen, jumpoff, *lp;
    INTEGER toskip;
    Byte* p;
    WDPBRec wdpb;
    StringPtr ename;
    INTEGER elen;
    /* The PPC client addresses the complete record through a guest alias. */
    _Alignas(256) char quickbytes[grafSize] = {0};
    LONGINT tmpa5;
    BOOLEAN desk_file = FALSE;

#if defined(CYGWIN32) && defined(SDL) && defined(SYN68K)
    ROMlib_cfm68k_shutdown();
#endif

    fprintf(stderr, "trace: launchchain entered app='%.*s' vref=%d reset=%d\n", fName[0], fName + 1, CW(vRefNum),
            resetmemory);
    fflush(stderr);

    ROMlib_set_mmu_mode(TRUE);
    for (p = fName + fName[0] + 1; p > fName && *--p != ':';)
        ;
    toskip = p - fName;
    CurApName[0] = MIN(fName[0] - toskip, 31);
    BlockMove((Ptr)fName + 1 + toskip, (Ptr)CurApName + 1, (Size)CurApName[0]);
    if (!lpbp || lpbp->launchBlockID != CWC(extendedBlock)) {
        CInfoPBRec hpb;

        FIELD_ASSIGN_RAW(hpb.hFileInfo.ioNamePtr, RM(&fName[0]));
        hpb.hFileInfo.ioVRefNum = CW(vRefNum);
        hpb.hFileInfo.ioFDirIndex = CWC(0);
        hpb.hFileInfo.ioDirID = 0;
        fprintf(stderr, "trace: launchchain PBGetCatInfo begin\n");
        fflush(stderr);
        PBGetCatInfo(&hpb, FALSE);
        fprintf(stderr, "trace: launchchain PBGetCatInfo complete err=%d parID=%ld\n", CW(hpb.hFileInfo.ioResult),
                (long)CL(hpb.hFileInfo.ioFlParID));
        fflush(stderr);
        wdpb.ioVRefNum = CW(vRefNum);
        wdpb.ioWDDirID = hpb.hFileInfo.ioFlParID;
    }
    else {
        FSSpecPtr fsp;

        fsp = PPR(lpbp->launchAppSpec);
        wdpb.ioVRefNum = fsp->vRefNum;
        wdpb.ioWDDirID = fsp->parID;
    }
    /* Do not do this -- Loser does it SFSaveDisk_Update (vRefNum, fName); */
    wdpb.ioWDProcID = T('X', 'c', 't', 'r');
    FIELD_ASSIGN_RAW(wdpb.ioNamePtr, 0);
    fprintf(stderr, "trace: launchchain PBOpenWD begin\n");
    fflush(stderr);
    PBOpenWD(&wdpb, FALSE);
    fprintf(stderr, "trace: launchchain PBOpenWD complete err=%d vref=%d\n", CW(wdpb.ioResult), CW(wdpb.ioVRefNum));
    fflush(stderr);
    ROMlib_exevrefnum = CW(wdpb.ioVRefNum);
    ROMlib_exefname = CurApName;
    SetVol((StringPtr)0, ROMlib_exevrefnum);
    /* Route SMI files to the image reader even if their resource fork is
     missing. The mount path validates the image and reports any error. */
    size_t app_length = ROMlib_exefname[0];
    if (app_length >= 4 && !strncasecmp((char*)ROMlib_exefname + app_length - 3, ".smi", 4)) {
        FSSpec spec;
        char* path = NULL;
        err = C_FSMakeFSSpec(ROMlib_exevrefnum, 0, ROMlib_exefname, &spec);
        if (!err)
            err = ROMlib_filename_from_fsspec(&path, &spec);
        if (!err)
            err = ROMlib_mount_image(path);
        if (err) {
            char message[1200];
            snprintf(message, sizeof message, "Unable to mount self-mounting image (error %d): %s", err,
                     path ? ROMlib_image_error() : "Cannot locate the host file.");
            host_dialog_error(message);
        }
        else
            ROMlib_exit = FALSE;
        free(path);
        C_ExitToShell();
        return;
    }
    {
        FSSpec spec;
        int reused = 0;
        err = C_FSMakeFSSpec(ROMlib_exevrefnum, 0, ROMlib_exefname, &spec);
        if (!err)
            err = claim_application_instance(&spec, &reused);
        if (err || reused) {
            if (err != noErr && err != abortErr)
                host_dialog_error("Unable to identify this application for launching.");
            ROMlib_exit = err ? 2 : 1;
            C_ExitToShell();
            return;
        }
    }
    fprintf(stderr, "trace: launchchain OpenResFile begin name='%.*s' vref=%d\n", ROMlib_exefname[0],
            ROMlib_exefname + 1, ROMlib_exevrefnum);
    fflush(stderr);
    CurApRefNum = CW(OpenResFile(ROMlib_exefname));
    fprintf(stderr, "trace: launchchain OpenResFile complete ref=%d\n", CurApRefNum);
    fflush(stderr);

    fprintf(stderr, "trace: launchchain GetFInfo begin\n");
    fflush(stderr);
    err = GetFInfo(ROMlib_exefname, ROMlib_exevrefnum, &finfo);
    fprintf(stderr, "trace: launchchain GetFInfo complete err=%d type=0x%08lx creator=0x%08lx\n", err,
            (unsigned long)CL(finfo.fdType), (unsigned long)CL(finfo.fdCreator));
    fflush(stderr);

    process_create(FALSE, CL(finfo.fdType), CL(finfo.fdCreator));

    if (ROMlib_exeuname)
        free(ROMlib_exeuname);
    ROMlib_exeuname = ROMlib_newunixfrommac((char*)ROMlib_exefname + 1, ROMlib_exefname[0]);
    elen = strlen(ROMlib_exeuname);
    ename = (StringPtr)alloca(elen + 1);
    BlockMove((Ptr)ROMlib_exeuname, (Ptr)ename + 1, elen);
    ename[0] = elen;

    ROMlib_creator = CL(finfo.fdCreator);

#define LEMMINGSHACK
#if defined(LEMMINGSHACK)
    {
        if (finfo.fdCreator == CL(TICK("Psyg")) || finfo.fdCreator == CL(TICK("Psod")))
            ROMlib_flushoften = TRUE;
    }
#endif /* defined(LEMMINGSHACK) */

#if defined(ULTIMA_III_HACK)
    ROMlib_ultima_iii_hack = (finfo.fdCreator == CL(TICK("Ult3")));
#endif

    h = GetResource(T('v', 'e', 'r', 's'), 2);
    if (!h)
        h = GetResource(T('v', 'e', 'r', 's'), 1);

    ROMlib_version_long = 0;
    if (h) {
        vp = (vers_t*)STARH(h);
        versbuf = alloca(VERSSIZE(vp));
        memcpy(versbuf, vp->shortname + 1, vp->shortname[0]);
        sprintf(versbuf + vp->shortname[0], VERSFMT, (LONGINT)vp->c[0], (LONGINT)vp->c[1], (LONGINT)vp->c[2],
                (LONGINT)vp->c[3], (LONGINT)vp->loc);
        ROMlib_version_long = ((vp->c[0] << 24) | (vp->c[1] << 16) | (vp->c[2] << 8) | (vp->c[3] << 0));
    }
    else
        versbuf = "";

    ROMlib_ScreenSize.first = INITIALPAIRVALUE;
    ROMlib_MacSize.first = INITIALPAIRVALUE;
    ROMlib_directdiskaccess = FALSE;
    ROMlib_clear_gestalt_list();
    ROMlib_config_syszone_size = ROMlib_config_applzone_size = -1;
    ROMlib_prefer_m68k = 0;
    /* Reset once so application settings can override the profile defaults. */
    reset_string(&ROMlib_WindowName);
    reset_string(&ROMlib_Comments);
    ROMlib_desired_bpp = 0;
    ROMlib_display_scale = 100;
    ROMlib_noclock = 0;
    fprintf(stderr, "trace: launchchain ParseConfig ROMAWDefault begin\n");
    fflush(stderr);
    ParseConfigFile((StringPtr) "\014ROMAWDefault", 0, 0);
    fprintf(stderr, "trace: launchchain ParseConfig ROMAWDefault complete\n");
    fflush(stderr);
    fprintf(stderr, "trace: launchchain ParseConfig app begin\n");
    fflush(stderr);
    ParseConfigFile(ename, err == noErr ? CL(finfo.fdCreator) : 0, err == noErr ? CL(finfo.fdType) : TICK("APPL"));
    if (ROMlib_display_scale_override)
        ROMlib_display_scale = ROMlib_display_scale_override;
    ROMlib_ecf_apply_startup_profile();
    fprintf(stderr, "trace: launchchain ParseConfig app complete\n");
    fflush(stderr);
#if defined(SDL)
    ROMlib_sdl_select_application(ename, err == noErr ? CL(finfo.fdType) : 0, err == noErr ? CL(finfo.fdCreator) : 0);
#endif
    ROMlib_clockonoff(!ROMlib_noclock);
    if ((ROMlib_ScreenSize.first != INITIALPAIRVALUE || ROMlib_MacSize.first != INITIALPAIRVALUE)) {
        if (ROMlib_ScreenSize.first == INITIALPAIRVALUE)
            ROMlib_ScreenSize = ROMlib_MacSize;
        if (ROMlib_MacSize.first == INITIALPAIRVALUE)
            ROMlib_MacSize = ROMlib_ScreenSize;
    }
    fprintf(stderr, "trace: launchchain Get1Resource CODE0 begin\n");
    fflush(stderr);
    code0 = Get1Resource(T('C', 'O', 'D', 'E'), 0);
    fprintf(stderr, "trace: launchchain Get1Resource CODE0 complete handle=%p\n", (void*)code0);
    fflush(stderr);
    if (code0)
        HLock(code0);
    cfrg0 = Get1Resource(T('c', 'f', 'r', 'g'), 0);

    boolean_t has_cfm68k = cfrg0 && ROMlib_find_cfrg(cfrg0, kMotorola68KArch, kApplicationCFrag, (StringPtr) "");
    if (cfrg0 && (ppc_override || !ROMlib_prefer_m68k || (!code0 && !has_cfm68k)) &&
        ROMlib_find_cfrg(cfrg0, T('p', 'w', 'p', 'c'), kApplicationCFrag, (StringPtr) "")) {
        code0 = NULL;
        fragment_arch = kPowerPCArch;
    }
    else if (has_cfm68k) {
        code0 = NULL;
        fragment_arch = kMotorola68KArch;
    }
    else if (!code0 && !cfrg0 && (desk_file = ROMlib_desk_file_p())) {
        /* Classic desk accessories and panels have no CODE 0 entry point. */
    }
    else if (!code0) {
        if (cfrg0) {
            ROMlib_launch_failure = launch_no_supported_fragment;
        }
        else
            ROMlib_launch_failure = launch_damaged;
        fprintf(stderr, "trace: launchchain no CODE/cfrg, ExitToShell failure=%d\n", ROMlib_launch_failure);
        fflush(stderr);
        C_ExitToShell();
    }

    {
        Handle size_resource_h;
        int16 size_flags;

        fprintf(stderr, "trace: launchchain SIZE resource begin\n");
        fflush(stderr);
        size_resource_h = Get1Resource(T('S', 'I', 'Z', 'E'), 0);
        if (size_resource_h == NULL)
            size_resource_h = Get1Resource(T('S', 'I', 'Z', 'E'), -1);
        fprintf(stderr, "trace: launchchain SIZE resource complete handle=%p\n", (void*)size_resource_h);
        fflush(stderr);
        if (size_resource_h && launch_handle_body_valid_p(size_resource_h) && GetHandleSize(size_resource_h) >= 10) {
            size_info_t* size_resource;

            size_resource = (size_info_t*)STARH(size_resource_h);
            size_info.size_flags = CW(size_resource->size_flags);
            size_info.preferred_size = CL(size_resource->preferred_size);
            size_info.minimum_size = CL(size_resource->minimum_size);
            size_info.size_resource_present_p = TRUE;
        }
        else {
            if (size_resource_h) {
                fprintf(stderr, "trace: launchchain ignoring invalid SIZE handle=%p trap=0x%04x\n",
                        (void*)size_resource_h, (unsigned)mostrecenttrap);
                fflush(stderr);
            }
            memset(&size_info, '\000', sizeof size_info);
            size_info.size_resource_present_p = FALSE;
        }
        size_info.application_p = !desk_file;

        size_flags = size_info.size_flags;

        {
            Byte mode32 = !code0 && !desk_file ? TRUE
                          : ROMlib_addressing_mode >= 0
                              ? ROMlib_addressing_mode
                              : (size_info.size_resource_present_p && (size_flags & SZis32BitCompatible)) != 0;
            /* Other front ends do not yet reserve a 24-bit display buffer. */
            if (ROMlib_addressing_mode < 0 && !ROMlib_guest_framebuffer)
                mode32 = TRUE;
#if defined(SDL)
            if (!mode32 && !desk_file)
                sdl_apply_pending_resize();
#endif
            /* SIZE determines whether the larger default partition is safe.
         Select it before changing MMU mode, in the same fresh-process path
         used by per-application ECF settings. Do not bounce back to the
         conservative startup heap before evaluating SIZE on the retry. */
            FSSpec application;
            if (C_FSMakeFSSpec(ROMlib_exevrefnum, 0, ROMlib_exefname, &application) == noErr) {
                uint64 framebuffer_end = ROMlib_guest_framebuffer ? (uint64)US_TO_SYN68K(ROMlib_guest_framebuffer) +
                                                                        (uint64)vdriver_row_bytes * vdriver_height
                                                                  : 0;
                int restart =
                    err == noErr ? ROMlib_ecf_require_system(&application, finder_required_system(&finfo)) : 0;
                if (!restart)
                    restart = ROMlib_ecf_restart_mode_memory(&application, mode32, framebuffer_end,
                                                             size_info.preferred_size, size_info.minimum_size);
                if (restart < 0) {
                    system_error(
                        "Unable to restart ROMAW with the application startup settings. Check SystemVersion, SysZone and ApplZone.",
                        0, "Exit", NULL, NULL, C_ExitToShell, NULL, NULL);
                    ROMlib_exit = 1;
                    C_ExitToShell();
                }
                if (restart > 0) {
                    ROMlib_exit = 1;
                    C_ExitToShell();
                }
            }
            if (!ROMlib_set_mmu_mode(mode32)) {
                fprintf(stderr, "Unable to launch in 24-bit mode; reduce memory/display size or use -32bit\n");
                exit(1);
            }
            fprintf(stderr, "trace: Macintosh addressing mode=%d-bit\n", mode32 ? 32 : 24);
        }

        /* we don't accept open app events until a handler is installed */
        application_accepts_open_app_aevt_p = FALSE;
        send_application_open_aevt_p =
            system_version >= 0x700 && ((size_flags & SZisHighLevelEventAware) == SZisHighLevelEventAware);
    }

#if defined(SDL) && defined(_WIN32)
    ROMlib_set_application_icon(ROMlib_creator);
#endif
    h = GetResource(CL(finfo.fdCreator), 0);
    if (h && launch_handle_body_valid_p(h)) {
        namelen = *MR(*(unsigned char**)h);
        namebuf = alloca(namelen + 1);
        memcpy(namebuf, (char*)STARH(h) + 1, namelen);
        namebuf[namelen] = 0;
    }
    else {
        if (h) {
            fprintf(stderr, "trace: launchchain ignoring invalid creator name handle=%p trap=0x%04x\n", (void*)h,
                    (unsigned)mostrecenttrap);
            fflush(stderr);
        }
        namebuf = "";
    }

    if (!code0) {
        lp = 0;                /* just to shut GCC up */
        jumplen = jumpoff = 0; /* just to shut GCC up */
        a5 = (LONGINT)(long)US_TO_SYN68K(&tmpa5);
        HIDDEN_RAW_SET(CurrentA5_H, (Ptr)CL(a5));
        InitGraf((Ptr)quickbytes + grafSize - 4);
    }
    else {
        HLock(code0);
        if (!launch_handle_body_valid_p(code0) || GetHandleSize(code0) < 16) {
            fprintf(stderr, "trace: launchchain invalid CODE0 handle=%p size=%ld trap=0x%04x\n", (void*)code0,
                    launch_handle_body_valid_p(code0) ? (long)GetHandleSize(code0) : -1L, (unsigned)mostrecenttrap);
            fflush(stderr);
            ROMlib_launch_failure = launch_damaged;
            C_ExitToShell();
        }

        lp = (LONGINT*)STARH(code0);
        abovea5 = CL(*lp++);
        belowa5 = CL(*lp++);
        jumplen = CL(*lp++);
        jumpoff = CL(*lp++);

        /*
	 * NOTE: The stack initialization code that was here has been moved
	 *	     to ROMlib_InitZones in mman.c
	 */
        /* #warning Stack is getting reinitialized even when Chain is called ... */

#if defined(SYN68K)
        EM_A7 -= abovea5 + belowa5;
        HIDDEN_RAW_SET(CurStackBase_H, (Ptr)CL(EM_A7));
#else  /* !defined(SYN68K) */
        ROMlib_foolgcc = alloca(abovea5 + belowa5);
        HIDDEN_RAW_SET(CurStackBase_H, CL(MAC_STACK_START + MAC_STACK_SIZE - (abovea5 + belowa5)));
#endif /* !defined(SYN68K) */

        HIDDEN_RAW_SET(CurrentA5_H, RM(MR(CurStackBase) + belowa5)); /* set CurrentA5 */
        HIDDEN_RAW_SET(BufPtr_H, RM(MR(CurrentA5) + abovea5));
        CurJTOffset = CW(jumpoff);
        a5 = CL((LONGINT)CurrentA5);
    }

    GetDateTime((LONGINT*)&Time);
    HIDDEN_RAW_SET(ROMBase_H, RM((Ptr)ROMlib_phoneyrom));
    HIDDEN_RAW_SET(dodusesit_H, ROMBase);
    QDExist = WWExist = EXIST_NO;
    HIDDEN_RAW_SET(TheZone_H, ApplZone);
    ROMlib_memnomove_p = TRUE;

    /*
 * NOTE: this memcpy has to be done after all local variables have been used
 *	 because it will quite possibly smash stuff that's on our stack.
 *	 In reality, we only see this if we compile without optimization,
 *	 but trust me, it was *very* confusing when this memcpy was up
 *	 before we were done with our local varibles.
 */
    if (code0) {
        fprintf(stderr, "trace: launchchain beginexecutingat begin\n");
        fflush(stderr);
        memcpy(MR(CurrentA5) + jumpoff, lp, jumplen); /* copy in the
							 jump table */
        ROMlib_destroy_blocks(0, ~0, FALSE);
    }
    SetCursor(STARH(GetCursor(watchCursor)));

    /* Call this routine in case the refresh value changed, either just
    * now or when the config file was parsed.  We want to do this
    * before the screen's depth might be changed.
    */
    {
        int save_ROMlib_refresh = ROMlib_refresh;
        dequeue_refresh_task();
        set_refresh_rate(0);
        set_refresh_rate(save_ROMlib_refresh);
    }

    ROMlib_uaf = 0;

#if defined(SDL)
    if (!desk_file) {
        /* Resolve the selected application's saved dimensions before the
         startup window is shown, rather than at its first event poll. */
        sdl_apply_pending_resize();
        ROMlib_sdl_startup_ready();
    }
#endif
    if (desk_file) {
        InitFonts();
        InitWindows();
        InitMenus();
        TEInit();
        InitDialogs(NULL);
        InitCursor();
        OSErr desk_error = ROMlib_run_desk_file(CW(CurApRefNum), CurApName);
        if (desk_error)
            system_error("This desk accessory or control panel could not be opened.", 0, "OK", NULL, NULL, NULL, NULL,
                         NULL);
        C_ExitToShell();
    }
    else if (code0)
        beginexecutingat(CL((LONGINT)CurrentA5) + CW(CurJTOffset) + 2);
    else {
        FSSpec fs;

        FSMakeFSSpec(ROMlib_exevrefnum, 0, ROMlib_exefname, &fs);
        cfm_launch(cfrg0, fragment_arch, &fs);
    }
}

A2(PUBLIC trap, void, Chain, StringPtr, fName, INTEGER, vRefNum)
{
    launchchain(fName, vRefNum, FALSE, 0);
}

PRIVATE void reset_low_globals(void)
{
    /*
 * we're about to smash applzone ... we may want to verify a few low-mem
 * globals beforehand
 */

    ProcPtr saveDABeeper;
    THz saveSysZone;
    uint32 saveTicks;
    INTEGER saveBootDrive;
    LONGINT saveLo3Bytes;
    LONGINT save20, save28, save58, save5C;
    Ptr saveSoundBase;
    Ptr saveVIA;
    Ptr saveSCCRd;
    Ptr saveSCCWr;
    Handle saveAppParmHandle;
    QHdr saveVCBQHdr;
    QHdr saveFSQHdr;
    QHdr saveDrvQHdr;
    QHdr saveEventQueue;
    QHdr saveVBLQueue;
    Ptr saveFCBSPtr;
    Ptr saveWDCBsPtr;
    LONGINT saveCurDirStore;
    INTEGER saveSFSaveDisk;
    VCBPtr saveDefVCBPtr;
    char saveCurApName[sizeof(CurApName)];
    INTEGER saveCurApRefNum;
    INTEGER saveCurMap;
    Handle saveTopMapHndl;
    Handle saveSysMapHndl;
    INTEGER saveSysMap;
    LONGINT saveScrapSize;
    Handle saveScrapHandle;
    INTEGER saveScrapCount;
    INTEGER saveScrapState;
    StringPtr saveScrapName;
    Handle saveROMFont0;
    Handle saveWidthListHand;
    Byte saveSPValid;
    Byte saveSPATalkA;
    Byte saveSPATalkB;
    Byte saveSPConfig;
    INTEGER saveSPPortA;
    INTEGER saveSPPortB;
    LONGINT saveSPAlarm;
    INTEGER saveSPFont;
    Byte saveSPKbd;
    Byte saveSPPrint;
    Byte saveSPVolCtl;
    Byte saveSPClikCaret;
    Byte saveSPMisc2;
    INTEGER saveKeyThresh;
    INTEGER saveKeyRepThresh;
    INTEGER saveMenuFlash;
    LONGINT saveCaretTime;
    LONGINT saveDoubleTime;
    LONGINT saveDefDirID;
    HIDDEN_Handle saveDAStrings[4];
    Ptr saveMemTop;
    DCtlHandlePtr saveUTableBase;
    INTEGER saveUnitNtryCnt;
    Point saveMouseLocation;
    CGrafPtr saveWMgrCPort;
    Handle saveMBDFHndl;
    ProcPtr saveJCrsrTask;

    AE_info_t* saveAE_info;

    RGBColor saveHiliteRGB;
    GDHandle saveTheGDevice, saveMainDevice, saveDeviceList;
    char saveKeyMap[sizeof_KeyMap];

    Byte saveFinderName[sizeof(FinderName)];
    virtual_int_state_t bt;

    bt = block_virtual_ints();
    saveSysZone = SysZone;
    saveTicks = Ticks;
    saveBootDrive = BootDrive;
    saveLo3Bytes = Lo3Bytes;
    save20 = *(LONGINT*)SYN68K_TO_US(0x20);
    save28 = *(LONGINT*)SYN68K_TO_US(0x28);
    save58 = *(LONGINT*)SYN68K_TO_US(0x58);
    save5C = *(LONGINT*)SYN68K_TO_US(0x5C);
    saveVIA = VIA;
    saveSCCRd = HIDDEN_RAW(SCCRd_H);
    saveSCCWr = HIDDEN_RAW(SCCWr_H);
    saveSoundBase = SoundBase;
    saveAppParmHandle = AppParmHandle;
    saveVCBQHdr = VCBQHdr;
    saveFSQHdr = FSQHdr;
    saveDrvQHdr = DrvQHdr;
    saveFCBSPtr = FCBSPtr;
    saveWDCBsPtr = WDCBsPtr;
    saveSFSaveDisk = SFSaveDisk;
    saveCurDirStore = CurDirStore;
    saveEventQueue = EventQueue;
    saveVBLQueue = VBLQueue;
    saveDefVCBPtr = DefVCBPtr;
    memcpy(saveCurApName, CurApName, sizeof(CurApName));
    saveCurApRefNum = CurApRefNum;
    saveCurMap = CurMap;
    saveTopMapHndl = TopMapHndl;
    saveSysMapHndl = SysMapHndl;
    saveSysMap = SysMap;
    saveScrapSize = ScrapSize;
    saveScrapHandle = ScrapHandle;
    saveScrapCount = ScrapCount;
    saveScrapState = ScrapState;
    saveScrapName = ScrapName;
    saveROMFont0 = ROMFont0;
    saveWidthListHand = WidthListHand;
    saveSPValid = SPValid;
    saveSPATalkA = SPATalkA;
    saveSPATalkB = SPATalkB;
    saveSPConfig = SPConfig;
    saveSPPortA = SPPortA;
    saveSPPortB = SPPortB;
    saveSPAlarm = SPAlarm;
    saveSPFont = SPFont;
    saveSPKbd = SPKbd;
    saveSPPrint = SPPrint;
    saveSPVolCtl = SPVolCtl;
    saveSPClikCaret = SPClikCaret;
    saveSPMisc2 = SPMisc2;
    saveKeyThresh = KeyThresh;
    saveKeyRepThresh = KeyRepThresh;
    saveMenuFlash = MenuFlash;
    saveCaretTime = CaretTime;
    saveDoubleTime = DoubleTime;
    saveDefDirID = DefDirID;

    saveHiliteRGB = HiliteRGB;
    saveTheGDevice = TheGDevice;
    saveMainDevice = MainDevice;
    saveDeviceList = DeviceList;
    saveDAStrings[0] = DAStrings_H[0];
    saveDAStrings[1] = DAStrings_H[1];
    saveDAStrings[2] = DAStrings_H[2];
    saveDAStrings[3] = DAStrings_H[3];
    saveMemTop = MemTop;
    saveUTableBase = UTableBase;
    saveUnitNtryCnt = UnitNtryCnt;

    saveMouseLocation = MouseLocation;
    saveDABeeper = DABeeper;

    memcpy(saveFinderName, FinderName, sizeof(saveFinderName));
    saveWMgrCPort = WMgrCPort;
    saveMBDFHndl = MBDFHndl;

    saveJCrsrTask = JCrsrTask;

    saveAE_info = AE_info;
    memcpy(saveKeyMap, KeyMap, sizeof_KeyMap);

    /* Set low globals to 0xFF, but don't touch exception vectors. */
    memset((char*)&nilhandle_H + 64 * sizeof(ULONGINT), 0xFF,
           ((char*)&lastlowglobal - (char*)&nilhandle_H - 64 * sizeof(ULONGINT)));

    HIDDEN_RAW_SET(AE_info_H, saveAE_info);

    HIDDEN_RAW_SET(JCrsrTask_H, saveJCrsrTask);

    HIDDEN_RAW_SET(MBDFHndl_H, saveMBDFHndl);
    HIDDEN_RAW_SET(WMgrCPort_H, saveWMgrCPort);
    HIDDEN_RAW_SET(WindowList_H, NULL);
    memcpy(FinderName, saveFinderName, sizeof(FinderName));

    HIDDEN_RAW_SET(DABeeper_H, saveDABeeper);
    MouseLocation = saveMouseLocation;
    MouseLocation2 = saveMouseLocation;

    HIDDEN_RAW_SET(UTableBase_H, saveUTableBase);
    UnitNtryCnt = saveUnitNtryCnt;
    HIDDEN_RAW_SET(MemTop_H, saveMemTop);
    DAStrings_H[3] = saveDAStrings[3];
    DAStrings_H[2] = saveDAStrings[2];
    DAStrings_H[1] = saveDAStrings[1];
    DAStrings_H[0] = saveDAStrings[0];
    DefDirID = saveDefDirID;
    DoubleTime = saveDoubleTime;
    CaretTime = saveCaretTime;
    MenuFlash = saveMenuFlash;
    KeyRepThresh = saveKeyRepThresh;
    KeyThresh = saveKeyThresh;
    SPMisc2 = saveSPMisc2;
    SPClikCaret = saveSPClikCaret;
    SPVolCtl = saveSPVolCtl;
    SPPrint = saveSPPrint;
    SPKbd = saveSPKbd;
    SPFont = saveSPFont;
    SPAlarm = saveSPAlarm;
    SPPortB = saveSPPortB;
    SPPortA = saveSPPortA;
    SPConfig = saveSPConfig;
    SPATalkB = saveSPATalkB;
    SPATalkA = saveSPATalkA;
    SPValid = saveSPValid;
    HIDDEN_RAW_SET(WidthListHand_H, saveWidthListHand);
    HIDDEN_RAW_SET(ROMFont0_H, saveROMFont0);
    HIDDEN_RAW_SET(ScrapName_H, saveScrapName);
    ScrapState = saveScrapState;
    ScrapCount = saveScrapCount;
    HIDDEN_RAW_SET(ScrapHandle_H, saveScrapHandle);
    ScrapSize = saveScrapSize;
    SysMap = saveSysMap;
    HIDDEN_RAW_SET(SysMapHndl_H, saveSysMapHndl);
    HIDDEN_RAW_SET(TopMapHndl_H, saveTopMapHndl);
    CurMap = saveCurMap;
    CurApRefNum = saveCurApRefNum;
    memcpy(CurApName, saveCurApName, sizeof(CurApName));
    HIDDEN_RAW_SET(DefVCBPtr_H, saveDefVCBPtr);
    VBLQueue = saveVBLQueue;
    EventQueue = saveEventQueue;
    CurDirStore = saveCurDirStore;
    SFSaveDisk = saveSFSaveDisk;
    HIDDEN_RAW_SET(WDCBsPtr_H, saveWDCBsPtr);
    HIDDEN_RAW_SET(FCBSPtr_H, saveFCBSPtr);
    DrvQHdr = saveDrvQHdr;
    FSQHdr = saveFSQHdr;
    VCBQHdr = saveVCBQHdr;
    Lo3Bytes = saveLo3Bytes;
    HIDDEN_RAW_SET(VIA_H, saveVIA);
    HIDDEN_RAW_SET(SCCRd_H, saveSCCRd);
    HIDDEN_RAW_SET(SCCWr_H, saveSCCWr);
    HIDDEN_RAW_SET(SoundBase_H, saveSoundBase);
    Ticks_UL.u = saveTicks;
    HIDDEN_RAW_SET(SysZone_H, saveSysZone);
    BootDrive = saveBootDrive;
    HIDDEN_RAW_SET(AppParmHandle_H, saveAppParmHandle);

    HiliteRGB = saveHiliteRGB;
    HIDDEN_RAW_SET(TheGDevice_H, saveTheGDevice);
    HIDDEN_RAW_SET(MainDevice_H, saveMainDevice);
    HIDDEN_RAW_SET(DeviceList_H, saveDeviceList);

    restore_virtual_ints(bt);

    HIDDEN_RAW_SET(nilhandle_H, 0); /* so nil dereferences "work" */

    CrsrBusy = 0;
    TESysJust = 0;
    HIDDEN_RAW_SET(DSAlertTab_H, 0);
    HIDDEN_RAW_SET(ResumeProc_H, 0);
    HIDDEN_RAW_SET(GZRootHnd_H, 0);
    ANumber = 0;
    HIDDEN_RAW_SET(ResErrProc_H, 0);
    FractEnable = 0;
    SEvtEnb = 0;
    HIDDEN_RAW_SET(MenuList_H, 0);
    MBarEnable = 0;
    MenuFlash = 0;
    TheMenu = 0;
    HIDDEN_RAW_SET(MBarHook_H, 0);
    HIDDEN_RAW_SET(MenuHook_H, 0);
    HIDDEN_RAW_SET(HeapEnd_H, 0);
    HIDDEN_RAW_SET(ApplLimit_H, 0);
    SoundActive = soundactiveoff;
    PortBUse = 2; /* configured for Serial driver */

    memcpy(KeyMap, saveKeyMap, sizeof_KeyMap);
    OneOne = CLC(0x00010001);
    HIDDEN_RAW_SET(DragHook_H, 0);
    HIDDEN_RAW_SET(MBDFHndl_H, 0);
    HIDDEN_RAW_SET(MenuList_H, 0);
    HIDDEN_RAW_SET(MBSaveLoc_H, 0);
    SysFontFam = 0;

    SysVersion = CW(system_version);
    FSFCBLen = CWC(94);

    /*
 * TODO:  how does this relate to Launch?
 */
    /* Set up default floating point environment. */
    {
        INTEGER env = 0;
        ROMlib_Fsetenv(&env, 0);
    }

    HIDDEN_RAW_SET(TEDoText_H, RM((ProcPtr)P_ROMlib_dotext)); /* where should this go ? */

    WWExist = QDExist = EXIST_NO; /* TODO:  LOOK HERE! */
    SCSIFlags = CWC(0xEC00);      /* scsi+clock+xparam+mmu+adb
				 (no fpu,aux or pwrmgr) */

    MMUType = 5;
    KbdType = 2;

    MCLKPCmiss1 = 0; /* &MCLKPCmiss1 = 0x358 + 72 (MacLinkPC starts
			   adding the 72 byte offset to VCB pointers too
			   soon, starting with 0x358, which is not the
			   address of a VCB) */

    MCLKPCmiss2 = 0; /* &MCLKPCmiss1 = 0x358 + 78 (MacLinkPC misses) */
    HIDDEN_RAW_SET(AuxCtlHead_H, 0);
    HIDDEN_RAW_SET(CurDeactive_H, 0);
    HIDDEN_RAW_SET(CurActivate_H, 0);
    macfpstate[0] = 0;
    fondid = 0;
    PrintErr = 0;
    mouseoffset = 0;
    HIDDEN_RAW_SET(heapcheck_H, 0);
    DefltStack = CLC(0x2000); /* nobody really cares about these two */
    MinStack = CLC(0x400);    /* values ... */
    HIDDEN_RAW_SET(IAZNotify_H, 0);
    CurPitch = 0;
    HIDDEN_RAW_SET(JSwapFont_H, (ProcPtr)RM(P_FMSwapFont));
    HIDDEN_RAW_SET(JInitCrsr_H, (ProcPtr)RM(P_InitCursor));

    HIDDEN_RAW_SET(JHideCursor_H, (ProcPtr)RM(P_HideCursor));
    HIDDEN_RAW_SET(JShowCursor_H, (ProcPtr)RM(P_ShowCursor));
    HIDDEN_RAW_SET(JShieldCursor_H, (ProcPtr)RM(P_ShieldCursor));
    HIDDEN_RAW_SET(JSetCrsr_H, (ProcPtr)RM(P_SetCursor));
    HIDDEN_RAW_SET(JCrsrObscure_H, (ProcPtr)RM(P_ObscureCursor));

    *(long*)(0x574 + ROMlib_offset) = (long)RM(P_Unknown574);

    HIDDEN_RAW_SET(Key1Trans_H, (Ptr)RM(P_Key1Trans));
    HIDDEN_RAW_SET(Key2Trans_H, (Ptr)RM(P_Key2Trans));
    HIDDEN_RAW_SET(JFLUSH_H, (ProcPtr)RM(P_flushcache));
    HIDDEN_RAW_SET(JResUnknown1_H, JFLUSH); /* I don't know what these are supposed to */
    HIDDEN_RAW_SET(JResUnknown2_H, JFLUSH); /* do, but they're not called enough for
				   us to worry about the cache flushing
				   overhead */

    CPUFlag = 4;                /* mc68040 */
    UnitNtryCnt = CW(NDEVICES); /* capacity of the allocated unit table */
    ROMlib_device_init();

    HIDDEN_RAW_SET(TheZone_H, ApplZone);

#if !defined(SYN68K)
    bzero((char*)&nilhandle_H, 0xc0); /* exception vectors */
                                      /* pagemaker 2.0 looks at 108
					   and doesn't want to see -1 */
#endif                                /* !defined(SYN68K) */
    *(LONGINT*)SYN68K_TO_US(0x20) = save20;
    *(LONGINT*)SYN68K_TO_US(0x28) = save28;
    *(LONGINT*)SYN68K_TO_US(0x58) = save58;
    *(LONGINT*)SYN68K_TO_US(0x5C) = save5C;

    HiliteMode = CB(0xFF); /* I think this is correct */
    ROM85 = CWC(0x3FFF);   /* We be color now */
    MMU32Bit = 0x01;
    loadtrap = 0;
    *(LONGINT*)SYN68K_TO_US(0x1008) = CLC(0x4); /* Quark XPress 3.0 references 0x1008
					explicitly.  It takes the value
					found there, subtracts four from
					it and dereferences that value.
					Yahoo */
    *(short*)SYN68K_TO_US(4) = CWC(0x4e75);     /* RTS, so when we dynamically recompile
				    code starting at 0 we won't get far */

    /* Micro-cap dereferences location one of the AppPacks locations */

    {
        int i;

        for (i = 0; i < (int)NELEM(AppPacks_H); ++i)
            HIDDEN_RAW_SET(AppPacks_H[i], 0);
    }
    SysEvtMask = CWC(~(1L << keyUp)); /* EVERYTHING except keyUp */
    SdVolume = 7;                     /* for Beebop 2 */
    HIDDEN_RAW_SET(CurrentA5_H, (Ptr)CL(EM_A5));
}

PRIVATE void reset_traps(void)
{
    static void* savetooltraptable[0x400];
    static void* saveostraptable[0x100];
    static BOOLEAN beenhere = FALSE;

    ROMlib_reset_bad_trap_addresses();
    if (!beenhere) {
        memcpy(saveostraptable, ostraptable, sizeof(saveostraptable));
        memcpy(savetooltraptable, tooltraptable, sizeof(savetooltraptable));
        beenhere = TRUE;
    }
    else {
        /*
 * NOTE: I'm not preserving patches that go into the SystemZone.  Right now
 *       that just seems like an unnecessary source of mystery bugs.
 */
        memcpy(ostraptable, saveostraptable, sizeof(saveostraptable));
        memcpy(tooltraptable, savetooltraptable, sizeof(savetooltraptable));
    }
}

PRIVATE boolean_t our_special_map(resmaphand map)
{
    boolean_t retval;
    Handle h;

    CurMap = STARH(map)->resfn;
    h = Get1Resource(TICK("nUSE"), 0);
    retval = h ? TRUE : FALSE;

    return retval;
}

void empty_timer_queues(void)
{
    TMTask *tp, *nexttp;
    VBLTaskPtr vp, nextvp;
    virtual_int_state_t bt;

    bt = block_virtual_ints();

    dequeue_refresh_task();
    clear_pending_sounds();
    C_ExitMovies();
    for (vp = (VBLTaskPtr)PPR(VBLQueue.qHead); vp; vp = nextvp) {
        nextvp = (VBLTaskPtr)PPR(vp->qLink);
        VRemove(vp);
    }
    for (tp = (TMTask*)PPR(ROMlib_timehead.qHead); tp; tp = nexttp) {
        nexttp = (TMTask*)PPR(tp->qLink);
        RmvTime((QElemPtr)tp);
    }

    restore_virtual_ints(bt);
}

PRIVATE void reinitialize_things(void)
{
    resmaphand map, nextmap;
    filecontrolblock *fcbp, *efcbp;
    INTEGER length;
    INTEGER special_fn;
    int i;

#if defined(CYGWIN32) && defined(SDL) && defined(SYN68K)
    ROMlib_cfm68k_shutdown();
#endif

    ROMlib_shutdown_font_manager();
    SetZone(MR(SysZone));
    /* NOTE: we really shouldn't be closing desk accessories at all, but
       since we don't properly handle them when they're left open, it is
       better to close them down than not.  */

    for (i = DESK_ACC_MIN; i <= DESK_ACC_MAX; ++i)
        CloseDriver(-i - 1);

    empty_timer_queues();
    ROMlib_clock = 0; /* CLOCKOFF */

    special_fn = 0;
    for (map = (resmaphand)MR(TopMapHndl); map; map = nextmap) {
        nextmap = (resmaphand)HxP(map, nextmap);
        if (HxX(map, resfn) == SysMap)
            UpdateResFile(Hx(map, resfn));
        else {
            if (!our_special_map(map))
                CloseResFile(Hx(map, resfn));
            else {
                special_fn = Hx(map, resfn);
                UpdateResFile(special_fn);
            }
        }
    }

    length = CW(*(short*)MR(FCBSPtr));
    fcbp = (filecontrolblock*)((short*)MR(FCBSPtr) + 1);
    efcbp = (filecontrolblock*)((char*)MR(FCBSPtr) + length);
    for (; fcbp < efcbp; fcbp = (filecontrolblock*)((char*)fcbp + CW(FSFCBLen))) {
        INTEGER rn;

        rn = (char*)fcbp - (char*)MR(FCBSPtr);
        if (fcbp->fcbCName[0]
            /* && rn != Param_ram_rn */
            && rn != CW(SysMap) && rn != special_fn)
            FSClose((char*)fcbp - (char*)MR(FCBSPtr));
    }

    CurMap = STARH((resmaphand)MR(TopMapHndl))->resfn;

    /* TODO replace the code in main.c with this stuff */
    /* Set up default floating point environment. */
    {
        INTEGER env = 0;
        ROMlib_Fsetenv(&env, 0);
    }

    ROMlib_destroy_blocks(0, ~0, FALSE);
}

PUBLIC OSErr ROMlib_filename_from_fsspec(char** strp, FSSpec* fsp)
{
    OSErr retval;
    ParamBlockRec pbr;
    char *filename, *endname;
    VCBExtra* vcbp;
    struct stat sbuf;

    if (!strp)
        return paramErr;
    *strp = NULL;
    if (!fsp)
        return paramErr;
    HVCB* volume = ROMlib_findvcb(CW(fsp->vRefNum), fsp->name, NULL, TRUE);
    if (!volume)
        return nsvErr;
    /* HFS volumes have an HVCB, not the host-only VCBExtra extension read by
     ROMlib_nami. Their files do not have individual host filesystem paths. */
    if (volume->vcbCTRef)
        return paramErr;

    memset(&pbr, 0, sizeof pbr);
    pbr.ioParam.ioVRefNum = fsp->vRefNum;
    PP_RAW(pbr.ioParam.ioNamePtr) = (StringPtr)RM((Ptr)fsp->name);
    retval = ROMlib_nami(&pbr, CL(fsp->parID), NoIndex, strp, &filename, &endname, FALSE, &vcbp, &sbuf);
    return retval;
}

PUBLIC OSErr ROMlib_launch_path_from_fsspec(char** path, FSSpec* spec)
{
    if (!path)
        return paramErr;
    *path = NULL;
    if (!spec)
        return paramErr;
    if (spec->name[0] >= sizeof spec->name)
        return bdNamErr;
    OSErr error = ROMlib_filename_from_fsspec(path, spec);
    if (error == noErr || !path || !spec)
        return error;
    free(*path);
    *path = NULL;
    HVCB* volume = ROMlib_findvcb(CW(spec->vRefNum), spec->name, NULL, TRUE);
    if (!volume || !volume->vcbCTRef)
        return error;
    if (!spec->name[0] || spec->name[0] >= sizeof spec->name || !volume->vcbVN[0] ||
        volume->vcbVN[0] >= sizeof volume->vcbVN)
        return bdNamErr;
    char result[256];
    size_t length = spec->name[0];
    memcpy(result, spec->name + 1, length);
    result[length] = 0;
    LONGINT parent = CL(spec->parID);
    unsigned depth = 0;
    while (parent != 2) {
        CInfoPBRec directory = {0};
        Str255 name;
        if (parent < 2 || ++depth > 127)
            return bdNamErr;
        directory.dirInfo.ioVRefNum = volume->vcbVRefNum;
        directory.dirInfo.ioDrDirID = CL(parent);
        directory.dirInfo.ioFDirIndex = CWC(-1);
        PP_RAW(directory.dirInfo.ioNamePtr) = RM((StringPtr)name);
        error = PBGetCatInfo(&directory, FALSE);
        if (error)
            return error;
        if (!name[0] || length + name[0] + 1 > 255)
            return bdNamErr;
        memmove(result + name[0] + 1, result, length + 1);
        memcpy(result, name + 1, name[0]);
        result[name[0]] = ':';
        length += name[0] + 1;
        parent = CL(directory.dirInfo.ioDrParID);
    }
    unsigned volume_length = volume->vcbVN[0];
    if (length + volume_length + 1 > 255)
        return bdNamErr;
    memmove(result + volume_length + 1, result, length + 1);
    memcpy(result, volume->vcbVN + 1, volume_length);
    result[volume_length] = ':';
    *path = strdup(result);
    return *path ? noErr : memFullErr;
}

PUBLIC OSErr NewLaunch(StringPtr fName_arg, INTEGER vRefNum_arg, LaunchParamBlockRec* lpbp)
{
    OSErr retval;
    static char beenhere = FALSE;
    static jmp_buf buf;
    static void* native_checkpoint;
    static Str255 fName;
    static INTEGER vRefNum;
    static LaunchParamBlockRec lpb;
    BOOLEAN extended_p;

    retval = noErr;
    if (lpbp && lpbp->launchBlockID == CWC(extendedBlock)) {
        OSErr desk_result;
        /* Finder's Open uses LaunchApplication, while the Apple menu already
         uses the separate accessory launcher. Keep both entry points alike,
         before replacing any of the caller's guest state. */
        if (ROMlib_launch_separate_desk_spec(PPR(lpbp->launchAppSpec), &desk_result))
            return desk_result;
        lpb = *lpbp;
        str255assign(fName, (PPR(lpbp->launchAppSpec))->name);
        extended_p = TRUE;
    }
    else {
        lpb.launchBlockID = 0;
        if (fName_arg && memchr(fName_arg + 1, ':', fName_arg[0])) {
            // Resolve a qualified target before resetting the caller's heap.
            // The new working directory must belong to the target volume,
            // even when legacy Launch supplies vRefNum zero.
            FSSpec target;
            retval = C_FSMakeFSSpec(vRefNum_arg, 0, fName_arg, &target);
            if (retval)
                return retval;
            WDPBRec directory = {0};
            directory.ioVRefNum = target.vRefNum;
            directory.ioWDDirID = target.parID;
            directory.ioWDProcID = T('X', 'c', 't', 'r');
            retval = PBOpenWD(&directory, FALSE);
            if (retval)
                return retval;
            str255assign(fName, target.name);
            vRefNum = CW(directory.ioVRefNum);
        }
        else {
            str255assign(fName, fName_arg);
            vRefNum = vRefNum_arg;
        }
        extended_p = FALSE;
    }

    if (extended_p && (lpbp->launchControlFlags & CWC(launchContinue))) {
        Handle converted = NULL;
        AppParametersPtr parameters = PPR(lpbp->launchAppParameters);
        if (parameters && parameters->magic != CLC(APP_PARAMS_MAGIC)) {
            retval = ROMlib_decode_launch_parameters(parameters, &converted);
            if (retval != noErr)
                return retval;
            HLock(converted);
            parameters = (AppParametersPtr)STARH(converted);
        }
        retval = ROMlib_launch_continue(PPR(lpbp->launchAppSpec), parameters);
        if (converted)
            DisposHandle(converted);
    }
    else {
        /* This setjmp/longjmp code might be better put in launchchain */
        if (!beenhere) {
            beenhere = TRUE;
            native_checkpoint = syn68k_native_checkpoint();
            SYN68K_NATIVE_SETJMP(buf);
        }
        else {
            if (!syn68k_native_unwind(native_checkpoint))
                abort();
            longjmp(buf, 1);
        }

        retval = noErr;
        reset_adb_vector();
        reinitialize_things();
        reset_traps();
        reset_low_globals();
        ROMlib_InitZones(offset_no_change); /* too late to change memory offset */
        ROMlib_InitGWorlds();
        ROMlib_pict_info_reinit();
        hle_reinit();
        AE_reinit();
        print_reinit();

        gd_set_bpp(MR(MainDevice), !vdriver_grayscale_p, vdriver_fixed_clut_p, vdriver_bpp);
        ROMlib_init_stdfile();
#if ERROR_SUPPORTED_P(ERROR_UNEXPECTED)
        if (ERROR_ENABLED_P(ERROR_UNEXPECTED)) {
            uintptr_t lp;

            for (lp = (uintptr_t)&nilhandle_H; lp <= (uintptr_t)&lastlowglobal; lp += 2)
                if (lp != (uintptr_t)&TheZone_H && lp != (uintptr_t)&ApplZone_H && lp != (uintptr_t)&FSFCBLen &&
                    lp != (uintptr_t)&SysMap && lp != (uintptr_t)SYN68K_TO_US(0x2f6) &&
                    lp != (uintptr_t)SYN68K_TO_US(0x8e6) && lp != (uintptr_t)SYN68K_TO_US(0x900) &&
                    lp != (uintptr_t)&CurMap && lp != (uintptr_t)SYN68K_TO_US(0x8a6) &&
                    lp != (uintptr_t)SYN68K_TO_US(0x8aa) && lp != (uintptr_t)SYN68K_TO_US(0x268) &&
                    lp != (uintptr_t)SYN68K_TO_US(0x982) && lp != (uintptr_t)SYN68K_TO_US(0xaee) &&
                    lp != (uintptr_t)SYN68K_TO_US(0xcca) && lp != (uintptr_t)SYN68K_TO_US(0xd50) &&
                    lp != (uintptr_t)SYN68K_TO_US(0x18e) && lp != (uintptr_t)SYN68K_TO_US(0x190) &&
                    lp != (uintptr_t)SYN68K_TO_US(0x2f2) && lp != (uintptr_t)SYN68K_TO_US(0x11e) &&
                    lp != (uintptr_t)SYN68K_TO_US(0x15c) && lp != (uintptr_t)SYN68K_TO_US(0x27e) &&
                    lp != (uintptr_t)SYN68K_TO_US(0x31a) && lp != (uintptr_t)SYN68K_TO_US(0x82c) &&
                    lp != (uintptr_t)SYN68K_TO_US(0x82e) && lp != (uintptr_t)SYN68K_TO_US(0x830) &&
                    lp != (uintptr_t)SYN68K_TO_US(0x832) && lp != (uintptr_t)SYN68K_TO_US(0xa4a) &&
                    lp != (uintptr_t)SYN68K_TO_US(0xa52) && lp != (uintptr_t)SYN68K_TO_US(0xa56) &&
                    lp != (uintptr_t)SYN68K_TO_US(0xbf4) && lp != (uintptr_t)SYN68K_TO_US(0x828) &&
                    lp != (uintptr_t)SYN68K_TO_US(0x82a) && lp != (uintptr_t)SYN68K_TO_US(0x16c))
                    if (MR(*(int32*)lp) >= (uintptr_t)MR(ApplZone) &&
                        MR(*(int32*)lp) < (uintptr_t)PPR(MR(ApplZone)->bkLim))
                        warning_unexpected("Low global at 0x%lx may point into "
                                           "ApplZone and probably shouldn't.",
                                           (unsigned long)US_TO_SYN68K(lp));
        }
#endif
        launchchain(fName, vRefNum, TRUE, &lpb);
    }
    return retval;
}

A2(PUBLIC trap, void, Launch, StringPtr, fName_arg, INTEGER, vRefNum_arg)
{
    NewLaunch(fName_arg, vRefNum_arg, 0);
}
