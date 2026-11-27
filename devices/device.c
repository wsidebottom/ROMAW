/*+===================================================================
  File:      device.c

  Summary:   Implements device support for Macintosh device and communications services.

  Classes:   None.

  Functions: CloseDriver, Control, GetDCtlEntry, KillIO, OpenDriver, PBControl, PBKillIO, PBStatus, ROMlib_device_init,
             ROMlib_dispatch, ROMlib_driver_install, ROMlib_driver_remove, ROMlib_driveropen,
             ROMlib_host_disk_color_icon, Status

  Origin:    Existing repository source in executor/src/devices; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1989 - 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in DeviceMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "DeviceMgr.h"
#include "FileMgr.h"
#include "MemoryMgr.h"
#include "OSUtil.h"
#include "ResourceMgr.h"
#include "MenuMgr.h"
#include "ToolboxEvent.h"
#include "rsys/glue.h"
#include "rsys/mman.h"
#include "rsys/device.h"
#include "rsys/file.h"
#include "rsys/serial.h"
#include "rsys/legacysound.h"
#include "rsys/mactcp.h"
#include "rsys/hfs.h"
#include "rsys/resource.h"
#include "rsys/user_profile.h"

/* _DrvrInstall creates an empty DCE. A caller can then install a direct 68K
   driver pointer and clear dRAMBased, as Disk Copy does. Keep these separate
   from ROMAW's host function tables. Semantics: SuperMario OS/DeviceMgr.a. */
static DCtlHandle installed_drivers[NDEVICES];

/* SuperMario FXM.a: MapFBlock takes the FCB reference in D1, a byte
   position in D5 and a requested byte count in D4; D3/D6 receive the
   physical block and contiguous byte count. Host files have no such map. */
static syn68k_addr_t map_file_block(syn68k_addr_t address, void* argument)
{
    filecontrolblock* fcb = ROMlib_refnumtofcbp((uint16)EM_D1);
    OSErr error = rfNumErr;
    EM_D3 = EM_D6 = 0;
    if (fcb) {
        HVCB* volume = PPR(fcb->fcbVPtr);
        error = extFSErr;
        if (volume && volume->vcbCTRef) {
            error = 17; /* FXRangeErr */
            if (EM_D5 < (uint32)Cx(fcb->fcbPLen)) {
                LONGINT blocks = 0;
                LONGINT physical = ROMlib_logtophys(fcb, EM_D5, &blocks);
                if (physical >= 0) {
                    uint32 bytes = (uint32)blocks * PHYSBSIZE - EM_D5 % PHYSBSIZE;
                    EM_D3 = physical / PHYSBSIZE;
                    EM_D6 = MIN(EM_D4, bytes);
                    error = noErr;
                }
            }
        }
    }
    EM_D0 = (LONGINT)error;
    return POPADDR();
}

void ROMlib_device_init(void)
{
    static syn68k_addr_t map_callback;
    if (!map_callback)
        map_callback = callback_install(map_file_block, NULL);
    /* jMapFBlock is the private HFS vector used by the Disk Copy driver. */
    ULONGINT vector = CL(map_callback);
    memcpy((char*)ROMlib_offset + 0x6fc, &vector, sizeof vector);
}

OSErr ROMlib_driver_install(INTEGER refnum)
{
    unsigned unit = (unsigned)(-1 - (int)refnum);
    if (unit >= NDEVICES || unit >= (unsigned)(uint16)CW(UnitNtryCnt) || !UTableBase)
        return badUnitErr;
    DCtlHandle entry = STARH(&MR(UTableBase)[unit]);
    if (!entry) {
        entry = (DCtlHandle)_NewHandle_flags(sizeof(DCtlEntry), TRUE, TRUE);
        if (!entry)
            return memFullErr;
    }
    memset(STARH(entry), 0, sizeof(DCtlEntry));
    HIDDEN_RAW_SET(MR(UTableBase)[unit], RM(entry));
    HxX(entry, dCtlFlags) = CW(RAMBASEDBIT);
    HxX(entry, dCtlRefNum) = CW(refnum);
    installed_drivers[unit] = entry;
    return noErr;
}

OSErr ROMlib_driver_remove(INTEGER refnum)
{
    unsigned unit = (unsigned)(-1 - (int)refnum);
    if (unit >= NDEVICES || unit >= (unsigned)(uint16)CW(UnitNtryCnt) || !UTableBase)
        return badUnitErr;
    DCtlHandle entry = STARH(&MR(UTableBase)[unit]);
    if (!entry)
        return noErr;
    if (HxX(entry, dCtlFlags) & CW(DRIVEROPENBIT))
        return dRemovErr;
    if (HxX(entry, dCtlFlags) & CW(RAMBASEDBIT)) {
        Handle driver = (Handle)HxP(entry, dCtlDriver);
        if (driver)
            ReleaseResource(driver);
    }
    HIDDEN_RAW_SET(MR(UTableBase)[unit], NULL);
    installed_drivers[unit] = NULL;
    DisposHandle((Handle)entry);
    return noErr;
}

/*
 * NOTE:  The device manager now executes "native code" and code read
 *	  in from resources.  The latter is "RAM" based, the former is
 *	  our version of "ROM" based.  We use the same bit that the Mac
 *	  uses to distinguish between the two, but our implementation of
 *	  ROM based is incompatible with theirs.  NOTE: even in our
 *	  incompatible ROM based routines, we still byte swap the pointers
 *	  that get us to the routines.
 */

/* RAM drivers return through JIODone for synchronous controls. The outer
   dispatch owns the parameter block and stores D0 after the guest returns. */
static syn68k_addr_t ram_driver_done(syn68k_addr_t address, void* argument)
{
    return POPADDR();
}

A4(PUBLIC, OSErr, ROMlib_dispatch, ParmBlkPtr, p, /* INTERNAL */
   BOOLEAN, async, DriverRoutineType, routine, INTEGER, trapn)
{
    int devicen;
    ramdriverhand ramdh;
    DCtlHandle h;
    OSErr retval;
    typedef OSErr (*devfp_t)(ParmBlkPtr, DCtlPtr);
    devfp_t procp;

    if (CW(p->cntrlParam.ioCRefNum) == -4) {
        if (routine == Ctl)
            return ROMlib_legacy_sound_control(p, async);
        if (routine == Close)
            ROMlib_legacy_sound_reset(TRUE);
        retval = routine == Open || routine == Close ? noErr : statusErr;
        FAKEASYNC(p, async, retval);
    }
    devicen = -CW(p->cntrlParam.ioCRefNum) - 1;
    if (devicen < 0 || devicen >= NDEVICES)
        retval = badUnitErr;
    else if (UTableBase == (DCtlHandlePtr)(long)CLC(0xFFFFFFFF) || (h = STARH(&MR(UTableBase)[devicen])) == 0 ||
             HIDDEN_RAW((*h)) == 0)
        retval = unitEmptyErr;
    else {
        HLock((Handle)h);
        p->ioParam.ioTrap = CW(trapn);
        if (async)
            p->ioParam.ioTrap |= CWC(asyncTrpBit);
        else
            p->ioParam.ioTrap |= CWC(noQueueBit);
        if (!(HxX(h, dCtlFlags) & CWC(RAMBASEDBIT)) && installed_drivers[devicen] != h) {
            switch (routine) {
            case Open:
                retval = (*(devfp_t)MR(HxP(h, dCtlDriver)->udrvrOpen))(p, STARH(h));
                break;
            case Prime:
                retval = (*(devfp_t)MR(HxP(h, dCtlDriver)->udrvrPrime))(p, STARH(h));
                break;
            case Ctl:
                retval = (*(devfp_t)MR(HxP(h, dCtlDriver)->udrvrCtl))(p, STARH(h));
                break;
            case Stat:
                retval = (*(devfp_t)MR(HxP(h, dCtlDriver)->udrvrStatus))(p, STARH(h));
                break;
            case Close:
                retval = (*(devfp_t)MR(HxP(h, dCtlDriver)->udrvrClose))(p, STARH(h));
                break;
            default:
                retval = fsDSIntErr;
                break;
            }
        }
        else {
            BOOLEAN resource_driver = !!(HxX(h, dCtlFlags) & CWC(RAMBASEDBIT));
            ramdriverptr driver;
            ramdh = resource_driver ? (ramdriverhand)HxP(h, dCtlDriver) : NULL;
            if (ramdh) {
                LoadResource((Handle)ramdh);
                HLock((Handle)ramdh);
            }
            driver = resource_driver ? (ramdh ? STARH(ramdh) : NULL) : (ramdriverptr)HxP(h, dCtlDriver);
            if (!driver) {
                HUnlock((Handle)h);
                p->ioParam.ioResult = CW(unitEmptyErr);
                return unitEmptyErr;
            }
            switch (routine) {
            case Open:
                procp = (devfp_t)((char*)driver + CW(driver->drvrOpen));
                break;
            case Prime:
                procp = (devfp_t)((char*)driver + CW(driver->drvrPrime));
                break;
            case Ctl:
                procp = (devfp_t)((char*)driver + CW(driver->drvrCtl));
                break;
            case Stat:
                procp = (devfp_t)((char*)driver + CW(driver->drvrStatus));
                break;
            case Close:
                procp = (devfp_t)((char*)driver + CW(driver->drvrClose));
                break;
            default:
                procp = 0;
                break;
            }
            if (procp) {
                if (!HIDDEN_RAW(JIODone_H) || HIDDEN_RAW(JIODone_H) == (Ptr)CLC(0xffffffff))
                    HIDDEN_RAW_SET(JIODone_H, CL(callback_install(ram_driver_done, NULL)));
                LONGINT saved1, saved2, saved3, saved4, saved5, saved6, saved7, savea2, savea3, savea4, savea5, savea6;

                savea2 = EM_A2;
                EM_A0 = (LONGINT)(long)US_TO_SYN68K(p);
                EM_A1 = (LONGINT)(long)US_TO_SYN68K(STARH(h));
                EM_A2 = (LONGINT)(long)US_TO_SYN68K(procp); /* for compatibility with above */
                saved1 = EM_D1;
                saved2 = EM_D2;
                saved3 = EM_D3;
                saved4 = EM_D4;
                saved5 = EM_D5;
                saved6 = EM_D6;
                saved7 = EM_D7;
                savea3 = EM_A3;
                savea4 = EM_A4;
                savea5 = EM_A5;
                savea6 = EM_A6;
                CALL_EMULATOR((syn68k_addr_t)(long)US_TO_SYN68K(procp));
                EM_D1 = saved1;
                EM_D2 = saved2;
                EM_D3 = saved3;
                EM_D4 = saved4;
                EM_D5 = saved5;
                EM_D6 = saved6;
                EM_D7 = saved7;
                EM_A3 = savea3;
                EM_A4 = savea4;
                EM_A5 = savea5;
                EM_A6 = savea6;
                retval = EM_D0;
                EM_A2 = savea2;
            }
            else
                retval = fsDSIntErr;
            if (routine == Open && retval == noErr)
                HxX(h, dCtlFlags) |= CWC(DRIVEROPENBIT);
            else if ((routine == Close && retval == noErr) || (routine == Open && retval != noErr)) {
                HxX(h, dCtlFlags) &= ~CWC(DRIVEROPENBIT);
                HUnlock((Handle)h);
                if (ramdh)
                    HUnlock((Handle)ramdh);
                MBarEnable = 0;
                /* NOTE: It's not clear whether we should zero out this
		   field or just check for DRIVEROPEN bit up above and never
		   send messages except open to non-open drivers.  */
                if (installed_drivers[devicen] != h)
                    HIDDEN_RAW_SET(MR(UTableBase)[devicen], 0);
            }
        }
    }
    /* Invalid or empty driver slots must complete synchronous PBs too. */
    if (!async)
        p->ioParam.ioResult = CW(retval);
    fs_err_hook(retval);
    return retval;
}

/* PBOpen, PBClose, PBRead and PBWrite are part of the file manager */

/* Classic driver controls return only ICN#. Remember the families actually
   supplied by our host driver so Icon Utilities can recover their embedded
   color companions when Finder copies that image into a temporary suite. */
static struct {
    INTEGER id;
    unsigned char mono[256];
    Ptr driver_data;
    boolean_t valid;
} host_disk_icons[4];

Handle ROMlib_host_disk_color_icon(Handle mono, int* depth)
{
    if (*depth < 4 || !mono || !STARH(mono) || GetHandleSize(mono) != 256)
        return NULL;
    INTEGER id = 0;
    for (unsigned i = 0; i < NELEM(host_disk_icons); ++i)
        if (host_disk_icons[i].valid && !memcmp(STARH(mono), host_disk_icons[i].mono, 256)) {
            id = host_disk_icons[i].id;
            break;
        }
    if (!id)
        return NULL;

    INTEGER saved = C_CurResFile(), error = ResErr;
    BOOLEAN loading = ResLoad;
    THz zone = TheZone;
    HIDDEN_RAW_SET(TheZone_H, SysZone);
    C_SetResLoad(TRUE);
    C_UseResFile(CW(SysMap));
    Handle color = NULL;
    int requested = *depth;
    for (int bits = requested >= 8 ? 8 : 4; bits >= 4; bits /= 2) {
        Handle candidate = C_Get1Resource(bits == 8 ? TICK("icl8") : TICK("icl4"), id);
        if (candidate)
            C_LoadResource(candidate);
        if (candidate && STARH(candidate) && GetHandleSize(candidate) == 128 * bits) {
            color = candidate;
            *depth = bits;
            break;
        }
    }
    C_UseResFile(saved);
    C_SetResLoad(loading);
    HIDDEN_RAW_SET(TheZone_H, zone);
    ResErr = error;
    return color;
}

static boolean_t host_disk_icon_control(ParmBlkPtr pb, OSErr* error)
{
    INTEGER selector = CW(pb->cntrlParam.csCode);
    HVCB* volume;
    Handle icon;
    INTEGER saved, id;
    BOOLEAN loading;
    THz zone;

    /* Host volumes have no guest disk driver. Implement the classic physical
     and logical icon controls: ioVRefNum is a drive number, and csParam
     receives a pointer to a 32 by 32 image, its mask, and a Pascal location
     string. Finder reads the string immediately after the 256 icon bytes. */
    if (pb->cntrlParam.ioCRefNum || (selector != 21 && selector != 22))
        return FALSE;
    volume = ROMlib_vcbbydrive(CW(pb->cntrlParam.ioVRefNum));
    if (!volume || volume->vcbCTRef)
        return FALSE;
    int kind = romaw_host_volume_kind(((VCBExtra*)volume)->unixname);
    if (volume->vcbVN[0] == 5 && !memcmp(volume->vcbVN + 1, "ROMAW", 5))
        id = -16455; /* Classic custom-icon resource ID, embedded default here. */
    else
        id = kind == 2 ? -3972 : kind == 3 ? -3998 : -3995;
    saved = C_CurResFile();
    loading = ResLoad;
    zone = TheZone;
    HIDDEN_RAW_SET(TheZone_H, SysZone);
    C_SetResLoad(TRUE);
    C_UseResFile(CW(SysMap));
    icon = C_Get1Resource(TICK("ICN#"), id);
    if (icon)
        C_LoadResource(icon);
    *error = controlErr;
    if (icon && STARH(icon) && GetHandleSize(icon) >= 256) {
        SignedByte state = HGetState(icon);
        HLock(icon);
        unsigned slot = id == -16455 ? 0 : id == -3972 ? 1 : id == -3998 ? 2 : 3;
#if defined(CYGWIN32) || defined(__MINGW32__)
        static const char location[] = "Windows host";
#else
        static const char location[] = "Host filesystem";
#endif
        if (!host_disk_icons[slot].driver_data)
            host_disk_icons[slot].driver_data = NewPtrSys(256 + sizeof location);
        Ptr data = host_disk_icons[slot].driver_data;
        if (!data)
            *error = memFullErr;
        else {
            host_disk_icons[slot].id = id;
            memcpy(host_disk_icons[slot].mono, STARH(icon), 256);
            memcpy(data, STARH(icon), 256);
            data[256] = sizeof location - 1;
            memcpy(data + 257, location, sizeof location - 1);
            host_disk_icons[slot].valid = TRUE;
            ULONGINT address = CL(US_TO_SYN68K(data));
            memcpy(pb->cntrlParam.csParam, &address, sizeof address);
            *error = noErr;
        }
        HSetState(icon, state);
    }
    C_UseResFile(saved);
    C_SetResLoad(loading);
    HIDDEN_RAW_SET(TheZone_H, zone);
    return TRUE;
}

A2(PUBLIC, OSErr, PBControl, ParmBlkPtr, pbp, BOOLEAN, a) /* IMII-186 */
{
    OSErr err;

    if (host_disk_icon_control(pbp, &err)) {
        FAKEASYNC(pbp, a, err);
    }
    err = ROMlib_dispatch(pbp, a, Ctl, 0);
    fs_err_hook(err);
    return err;
}

A2(PUBLIC, OSErr, PBStatus, ParmBlkPtr, pbp, BOOLEAN, a) /* IMII-186 */
{
    OSErr err;

    err = ROMlib_dispatch(pbp, a, Stat, 0);
    fs_err_hook(err);
    return err;
}

A2(PUBLIC, OSErr, PBKillIO, ParmBlkPtr, pbp, BOOLEAN, a) /* IMII-187 */
{
    OSErr err;

    pbp->cntrlParam.csCode = CWC(killCode);
    err = ROMlib_dispatch(pbp, a, Ctl, 0);
    fs_err_hook(err);
    return err;
}

/*
 * OpenDriver is defined below ROMlib_driveropen.
 * CloseDriver is defined below ROMlib_driverclose.
 */

/* FSRead, FSWrite are part of the file manager */

A3(PUBLIC, OSErr, Control, INTEGER, rn, INTEGER, code, Ptr, param) /* IMII-179 */
{
    ParamBlockRec pb = {0};
    OSErr err;

    pb.cntrlParam.ioVRefNum = 0;
    pb.cntrlParam.ioCRefNum = CW(rn);
    pb.cntrlParam.csCode = CW(code);
    if (param)
        BlockMove(param, (Ptr)pb.cntrlParam.csParam, (Size)sizeof(pb.cntrlParam.csParam));
    err = PBControl(&pb, FALSE);
    fs_err_hook(err);
    return err;
}

A3(PUBLIC, OSErr, Status, INTEGER, rn, INTEGER, code, Ptr, param) /* IMII-179 */
{
    ParamBlockRec pb = {0};
    OSErr retval;

    pb.cntrlParam.ioVRefNum = 0;
    pb.cntrlParam.ioCRefNum = CW(rn);
    pb.cntrlParam.csCode = CW(code);
    retval = PBStatus(&pb, FALSE);
    if (param)
        BlockMove((Ptr)pb.cntrlParam.csParam, param, (Size)sizeof(pb.cntrlParam.csParam));
    fs_err_hook(retval);
    return retval;
}

A1(PUBLIC, OSErr, KillIO, INTEGER, rn) /* IMII-179 */
{
    ParamBlockRec pb = {0};
    OSErr err;

    pb.cntrlParam.ioCRefNum = CW(rn);
    err = PBKillIO(&pb, FALSE);
    fs_err_hook(err);
    return err;
}

A1(PUBLIC, DCtlHandle, GetDCtlEntry, INTEGER, rn)
{
    int devicen;

    devicen = -rn - 1;
    return (devicen < 0 || devicen >= NDEVICES) ? 0 : STARH(&MR(UTableBase)[devicen]);
}

/*
 * ROMlib_driveropen will be called by PBOpen if it encounters a name
 * beginning with * a period.
 */

PUBLIC driverinfo* ROMlib_otherdrivers = 0; /* for extensibility */

PRIVATE driverinfo knowndrivers[] = {
    {
        ROMlib_mactcp_driver_open,
        ROMlib_mactcp_driver_prime,
        ROMlib_mactcp_driver_control,
        ROMlib_mactcp_driver_status,
        ROMlib_mactcp_driver_close,
        (StringPtr) "\004.IPP",
        ROMAW_MACTCP_REFNUM,
    },
#if defined(LINUX) || defined(CYGWIN32)
    {
        ROMlib_serialopen,
        ROMlib_serialprime,
        ROMlib_serialctl,
        ROMlib_serialstatus,
        ROMlib_serialclose,
        (StringPtr) "\004.AIn",
        -6,
    },

    {
        ROMlib_serialopen,
        ROMlib_serialprime,
        ROMlib_serialctl,
        ROMlib_serialstatus,
        ROMlib_serialclose,
        (StringPtr) "\005.AOut",
        -7,
    },

    {
        ROMlib_serialopen,
        ROMlib_serialprime,
        ROMlib_serialctl,
        ROMlib_serialstatus,
        ROMlib_serialclose,
        (StringPtr) "\004.BIn",
        -8,
    },

    {
        ROMlib_serialopen,
        ROMlib_serialprime,
        ROMlib_serialctl,
        ROMlib_serialstatus,
        ROMlib_serialclose,
        (StringPtr) "\005.BOut",
        -9,
    },
#endif
};

A2(PUBLIC, OSErr, ROMlib_driveropen, ParmBlkPtr, pbp, /* INTERNAL */
   BOOLEAN, a)
{
    driverinfo *dip, *edip;
    OSErr err;
    INTEGER devicen;
    umacdriverptr up;
    DCtlHandle h;
    ramdriverhand ramdh;
    ResType typ;
    BOOLEAN alreadyopen;

    /* Installed driver names take precedence over the DRVR resource used to
     construct them. Otherwise an SMI installs unit 48 but opens resource ID
     zero as a second driver, and its loader rejects the mismatched refnum. */
    for (unsigned unit = 0;
         UTableBase && PP_RAW(pbp->ioParam.ioNamePtr) && unit < NDEVICES && unit < (unsigned)(uint16)CW(UnitNtryCnt);
         ++unit) {
        h = STARH(&MR(UTableBase)[unit]);
        if (!h || installed_drivers[unit] != h)
            continue;
        ramdriverptr driver;
        if (HxX(h, dCtlFlags) & CWC(RAMBASEDBIT)) {
            ramdh = (ramdriverhand)HxP(h, dCtlDriver);
            driver = ramdh ? STARH(ramdh) : NULL;
        }
        else
            driver = (ramdriverptr)HxP(h, dCtlDriver);
        if (driver && EqualString((StringPtr)&driver->drvrName, PPR(pbp->ioParam.ioNamePtr), FALSE, TRUE)) {
            pbp->cntrlParam.ioCRefNum = HxX(h, dCtlRefNum);
            err = HxX(h, dCtlFlags) & CWC(DRIVEROPENBIT) ? noErr : ROMlib_dispatch(pbp, a, Open, 0);
            return err;
        }
    }

    if (PP_RAW(pbp->ioParam.ioNamePtr) &&
        EqualString(PPR(pbp->ioParam.ioNamePtr), (StringPtr) "\006.Sound", FALSE, TRUE)) {
        pbp->ioParam.ioRefNum = CWC(-4);
        FAKEASYNC(pbp, a, noErr);
    }
    ZONE_SAVE_EXCURSION(SysZone, {
        err = noErr;

        if ((ramdh = (ramdriverhand)GetNamedResource(TICK("DRVR"), PPR(pbp->ioParam.ioNamePtr)))) {
            LoadResource((Handle)ramdh);
            GetResInfo((Handle)ramdh, &devicen, &typ, (StringPtr)0);
            devicen = CW(devicen);
            if (devicen < 0 || devicen >= NDEVICES) {
                err = badUnitErr;
            }
            else {
                h = STARH(&MR(UTableBase)[devicen]);
                alreadyopen = h && (HxX(h, dCtlFlags) & CWC(DRIVEROPENBIT));
                if (!h && !(h = ({
                                DCtlHandle new_h = (DCtlHandle)NewHandle(sizeof(DCtlEntry));
                                HIDDEN_RAW_SET(MR(UTableBase)[devicen], RM(new_h));
                                new_h;
                            })))
                    err = MemError();
                else if (!alreadyopen) {
                    memset((char*)STARH(h), 0, sizeof(DCtlEntry));
                    PP_RAW(HxX(h, dCtlDriver)) = (umacdriverptr)RM(ramdh);
                    HxX(h, dCtlFlags) = HxX(ramdh, drvrFlags) | CWC(RAMBASEDBIT);
                    HxX(h, dCtlRefNum) = CW(-(devicen + 1));
                    HxX(h, dCtlDelay) = HxX(ramdh, drvrDelay);
                    HxX(h, dCtlEMask) = HxX(ramdh, drvrEMask);
                    HxX(h, dCtlMenu) = HxX(ramdh, drvrMenu);
                    if (HxX(h, dCtlFlags) & CWC(NEEDTIMEBIT))
                        HxX(h, dCtlCurTicks) = CL(TickCount() + Hx(h, dCtlDelay));
                    else
                        HxX(h, dCtlCurTicks) = CLC(0x7FFFFFFF);
                    /*
	    * NOTE: this code doesn't check to see if something is already open.
	    *	 TODO:  fix this
	    */
                    pbp->cntrlParam.ioCRefNum = HxX(h, dCtlRefNum);
                    err = ROMlib_dispatch(pbp, a, Open, 0);
                }
                else {
                    pbp->cntrlParam.ioCRefNum = HxX(h, dCtlRefNum);
                    err = noErr;
                }
            }
        }
        else {

            dip = 0;
            if (ROMlib_otherdrivers) {
                for (dip = ROMlib_otherdrivers;
                     dip->open && !EqualString(dip->name, PPR(pbp->ioParam.ioNamePtr), FALSE, TRUE); dip++)
                    ;
                if (!dip->open)
                    dip = 0;
            }
            if (!dip) {
                for (dip = knowndrivers, edip = dip + NELEM(knowndrivers);
                     dip != edip && !EqualString(dip->name, PPR(pbp->ioParam.ioNamePtr), FALSE, TRUE); dip++)
                    ;
                if (dip == edip)
                    dip = 0;
            }
            if (dip) {
                devicen = -dip->refnum - 1;
                if (devicen < 0 || devicen >= NDEVICES)
                    err = badUnitErr;
                else if (HIDDEN_RAW(MR(UTableBase)[devicen]))
                    err = noErr; /* note:  if we choose to support desk */
                /*	  accessories, we will have to */
                /*	  check to see if this is one and */
                /*	  call the open routine if it is */
                else {
                    if (!(h = ({
                              DCtlHandle new_h = (DCtlHandle)NewHandle(sizeof(DCtlEntry));
                              HIDDEN_RAW_SET(MR(UTableBase)[devicen], RM(new_h));
                              new_h;
                          })))
                        err = MemError();
                    else {
                        memset((char*)STARH(h), 0, sizeof(DCtlEntry));
                        up = (umacdriverptr)NewPtr(sizeof(umacdriver));
                        if (!(PP_RAW(HxX(h, dCtlDriver)) = RM(up)))
                            err = MemError();
                        else {
                            up->udrvrOpen = (ProcPtr)RM(dip->open);
                            up->udrvrPrime = (ProcPtr)RM(dip->prime);
                            up->udrvrCtl = (ProcPtr)RM(dip->ctl);
                            up->udrvrStatus = (ProcPtr)RM(dip->status);
                            up->udrvrClose = (ProcPtr)RM(dip->close);
                            str255assign(up->udrvrName, dip->name);
                            err = noErr;
                        }
                    }
                }
                if (err == noErr) {
                    pbp->cntrlParam.ioCRefNum = CW(dip->refnum);
                    err = ROMlib_dispatch(pbp, a, Open, 0);
                }
            }
            else
                err = dInstErr;
        }
    });
    fs_err_hook(err);
    return err;
}

A2(PUBLIC, OSErr, OpenDriver, StringPtr, name, INTEGER*, rnp) /* IMII-178 */
{
    ParamBlockRec pb = {0};
    OSErr retval;

    pb.ioParam.ioRefNum = CWC(0); /* so we can't get garbage for ioRefNum. */
    pb.ioParam.ioPermssn = fsCurPerm;
    PP_RAW(pb.ioParam.ioNamePtr) = RM(name);
    retval = ROMlib_driveropen(&pb, FALSE);
    *rnp = pb.ioParam.ioRefNum;
    fs_err_hook(retval);
    return retval;
}

A1(PUBLIC, OSErr, CloseDriver, INTEGER, rn) /* IMII-178 */
{
    ParamBlockRec pb = {0};
    OSErr err;

    pb.cntrlParam.ioCRefNum = CW(rn);
    err = PBClose(&pb, FALSE);
    fs_err_hook(err);
    return err;
}
