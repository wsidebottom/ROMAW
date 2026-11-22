/*+===================================================================
  File:      serial.c

  Summary:   Implements Macintosh Serial Manager dispatch through the Windows serial backend.

  Classes:   None.

  Functions: RAMSDClose, RAMSDOpen, ROMlib_serialclose, ROMlib_serialctl, ROMlib_serialopen, ROMlib_serialprime,
             SerClrBrk, SerGetBuf, SerHShake, SerReset, SerSetBrk, SerSetBuf, SerStatus, callcomp

  Origin:    Existing repository source in executor/src/devices; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/*
 * Copyright 1989, 1990, 1995, 1997 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"

#include "Serial.h"
#include "DeviceMgr.h"
#include "FileMgr.h"
#include "MemoryMgr.h"
#include "OSUtil.h"

#include "rsys/file.h"
#include "rsys/hfs.h"
#include "rsys/serial.h"

#include "win_serial.h"

/*
 * TODO:
 *
 * Post events on break action or hardware handshake changes IMII-247
 * Support KillIO from the device manager IMII-248
 * Hardware handshaking on machines that support it IMII-252
 *
 * Finish up the get status routine?
 */

#define AINNAME "\004.AIn"
#define AOUTNAME "\005.AOut"
#define BINNAME "\004.BIn"
#define BOUTNAME "\005.BOut"
#define AINREFNUM (-6)
#define AOUTREFNUM (-7)
#define BINREFNUM (-8)
#define BOUTREFNUM (-9)

#if !defined(XONC)
#define XONC '\21'
#define XOFFC '\23'
#endif

#define SER_START (1)
#define SER_STOP (0)

A1(PUBLIC, OSErr, RAMSDOpen, SPortSel, port) /* IMII-249 */
{
    OSErr err;
    INTEGER rn;

    switch (port) {
    case sPortA:
        if ((err = OpenDriver((StringPtr)AINNAME, &rn)) == noErr)
            err = OpenDriver((StringPtr)AOUTNAME, &rn);
        break;
    case sPortB:
        if ((err = OpenDriver((StringPtr)BINNAME, &rn)) == noErr)
            err = OpenDriver((StringPtr)BOUTNAME, &rn);
        break;
    default:
        err = openErr;
        break;
    }
    return err;
}

A1(PUBLIC, void, RAMSDClose, SPortSel, port) /* IMII-250 */
{
    switch (port) {
    case sPortA:
        (void)CloseDriver(AINREFNUM);
        (void)CloseDriver(AOUTREFNUM);
        break;
    case sPortB:
        (void)CloseDriver(BINREFNUM);
        (void)CloseDriver(BOUTREFNUM);
        break;
    }
}

#define SERSET 8 /* IMII-250 */

A2(PUBLIC, OSErr, SerReset, INTEGER, rn, INTEGER, config) /* IMII-250 */
{
    config = CW(config);

    return Control(rn, SERSET, (Ptr)&config);
}

#define SERSETBUF 9 /* IMII-251 */

A3(PUBLIC, OSErr, SerSetBuf, INTEGER, rn, Ptr, p, INTEGER, len) /* IMII-251 */
{
    sersetbuf_t temp;

    PP_ASSIGN(temp.p, p);
    temp.i = CW(len);

    return Control(rn, SERSETBUF, (Ptr)&temp);
}

#define SERHSHAKE 10 /* IMII-251 */

A2(PUBLIC, OSErr, SerHShake, INTEGER, rn, SerShk, flags) /* IMII-251 */
{
    return Control(rn, SERHSHAKE, (Ptr)&flags);
}

#define SERSETBRK 12 /* IMII-252 */

A1(PUBLIC, OSErr, SerSetBrk, INTEGER, rn) /* IMII-252 */
{
    return Control(rn, SERSETBRK, (Ptr)0);
}

#define SERCLRBRK 11 /* IMII-253 */

A1(PUBLIC, OSErr, SerClrBrk, INTEGER, rn) /* IMII-253 */
{
    return Control(rn, SERCLRBRK, (Ptr)0);
}

#define SERGETBUF 2 /* IMII-253 */

A2(PUBLIC, OSErr, SerGetBuf, INTEGER, rn, LONGINT*, lp) /* IMII-253 */
{
    INTEGER status[11];
    OSErr err;

    if ((err = Status(rn, SERGETBUF, (Ptr)status)) == noErr)
        *lp = *(LONGINT*)status;
    return err;
}

#define SERSTATUS 8 /* IMII-253 */

A2(PUBLIC, OSErr, SerStatus, INTEGER, rn, SerStaRec*, serstap) /* IMII-253 */
{
    INTEGER status[11];
    OSErr err;

    if ((err = Status(rn, SERSTATUS, (Ptr)status)) == noErr)
        BlockMove((Ptr)status, (Ptr)serstap, (Size)sizeof(*serstap));
    return err;
}

#define OPENBIT (1 << 0)

typedef struct {
    LONGINT fd; /* probably need to save more state information */
    /* right now we just use fd to keep track of which port we're talking to */
} hidden, *hiddenp;

MAKE_HIDDEN(hiddenp);
typedef HIDDEN_hiddenp* hiddenh;

A1(PRIVATE, DCtlPtr, otherdctl, ParmBlkPtr, pbp)
{
    DCtlHandle h;

    h = 0;
    switch (CW(pbp->cntrlParam.ioCRefNum)) {
    case AINREFNUM:
        h = GetDCtlEntry(AOUTREFNUM);
        break;
    case AOUTREFNUM:
        h = GetDCtlEntry(AINREFNUM);
        break;
    case BINREFNUM:
        h = GetDCtlEntry(BOUTREFNUM);
        break;
    case BOUTREFNUM:
        h = GetDCtlEntry(BINREFNUM);
        break;
    }
    return h ? STARH(h) : 0;
}

typedef void (*compfuncp)(void);

void callcomp(ParmBlkPtr pbp, compfuncp comp, OSErr err)
{
    EM_A0 = (LONGINT)(long)US_TO_SYN68K(pbp);
    EM_A1 = (LONGINT)(long)US_TO_SYN68K(comp);
    EM_D0 = (unsigned short)err; /* TODO: unsigned short ? */
    CALL_EMULATOR((syn68k_addr_t)(long)comp);
}

#define DOCOMPLETION(pbp, err)                                                                                \
    (pbp)->ioParam.ioResult = CW(err);                                                                        \
    if (((pbp)->ioParam.ioTrap & asyncTrpBit) && PP_RAW((pbp)->ioParam.ioCompletion))                         \
        callcomp(pbp, (compfuncp)(uintptr_t)CL((uint32)(uintptr_t)PP_RAW((pbp)->ioParam.ioCompletion)), err); \
    return err

A2(PUBLIC, OSErr, ROMlib_serialopen, ParmBlkPtr, pbp, /* INTERNAL */
   DCtlPtr, dcp)
{
    OSErr err;
    auto DCtlPtr otherp; /* auto due to old compiler bug */
    hiddenh h;

    err = noErr;
    if (!(dcp->dCtlFlags & CWC(OPENBIT))) {
        h = (hiddenh)NewHandle(sizeof(hidden));
        FIELD_ASSIGN_RAW(dcp->dCtlStorage, (Handle)RM(h));
        otherp = otherdctl(pbp);
        if (otherp && (otherp->dCtlFlags & CWC(OPENBIT))) {
            *STARH(h) = *STARH((hiddenh)PPR(otherp->dCtlStorage));
            dcp->dCtlFlags |= CWC(OPENBIT);
        }
        else {
            if (err == noErr) {
                HxX(h, fd) =
                    (CW(pbp->cntrlParam.ioCRefNum) == AINREFNUM || CW(pbp->cntrlParam.ioCRefNum) == AOUTREFNUM) ? 0 : 1;
                dcp->dCtlFlags |= CWC(OPENBIT);
                SerReset(CW(pbp->cntrlParam.ioCRefNum),
                         (CW(pbp->cntrlParam.ioCRefNum) == AINREFNUM || CW(pbp->cntrlParam.ioCRefNum) == AOUTREFNUM)
                             ? CW(SPPortA)
                             : CW(SPPortB));
            }
        }
    }
    warning_trace_info("serial open returning %d", (LONGINT)err);
    DOCOMPLETION(pbp, err);
}

A2(PUBLIC, OSErr, ROMlib_serialprime, ParmBlkPtr, pbp, /* INTERNAL */
   DCtlPtr, dcp)
{
    OSErr err;
    hiddenh h;
    char* buf;
    size_t req_count;

    buf = (char*)PPR(pbp->ioParam.ioBuffer);
    req_count = CL(pbp->ioParam.ioReqCount);

    err = noErr;
    if (!(dcp->dCtlFlags & CWC(OPENBIT)))
        err = notOpenErr;
    else {
        warning_trace_info("serial prime code %d", (LONGINT)(CW(pbp->ioParam.ioTrap) & 0xFF));
        h = (hiddenh)PPR(dcp->dCtlStorage);
        switch (CW(pbp->ioParam.ioTrap) & 0xFF) {
        case aRdCmd:
            if (CW(pbp->cntrlParam.ioCRefNum) != AINREFNUM && CW(pbp->cntrlParam.ioCRefNum) != BINREFNUM)
                err = readErr;
            else {
                /* this may have to be changed since we aren't looking for
				   parity and framing errors */
                pbp->ioParam.ioActCount = CL(serial_bios_read(HxX(h, fd), buf, req_count));
                warning_trace_info("serial prime read %d bytes, first is 0x%0x", (LONGINT)CL(pbp->ioParam.ioActCount),
                                   (LONGINT)(unsigned char)buf[0]);
            }
            break;
        case aWrCmd:
            if (CW(pbp->cntrlParam.ioCRefNum) != AOUTREFNUM && CW(pbp->cntrlParam.ioCRefNum) != BOUTREFNUM)
                err = writErr;
            else {
                warning_trace_info("serial prime writing %d bytes, first is 0x%0x", (LONGINT)req_count,
                                   (LONGINT)(unsigned char)buf[0]);
                pbp->ioParam.ioActCount = CL(serial_bios_write(HxX(h, fd), buf, req_count));
            }
            break;
        default:
            err = badUnitErr;
            break;
        }
    }
    warning_trace_info("serial prime returning %d", (LONGINT)err);
    DOCOMPLETION(pbp, err);
}

A2(PRIVATE, OSErr, serset, LONGINT, fd, INTEGER, param)
{
    OSErr retval;

    retval = serial_bios_serset(fd, param);
    return retval;
}

A2(PRIVATE, OSErr, serxhshake, LONGINT, fd, SerShk*, sershkp)
{
    OSErr retval;

    retval = serial_bios_serxhshake(fd, sershkp);
    return retval;
}

A2(PRIVATE, OSErr, setbaud, LONGINT, fd, INTEGER, baud)
{
    OSErr retval;

    retval = serial_bios_setbaud(fd, baud);
    return retval;
}

A2(PRIVATE, OSErr, ctlbrk, LONGINT, fd, INTEGER, flag)
{
    OSErr retval;

    retval = serial_bios_ctlbrk(fd, flag);
    return retval;
}

A2(PRIVATE, OSErr, flow, LONGINT, fd, LONGINT, flag)
{
    OSErr retval;

    retval = serial_bios_setflow(fd, flag);
    return retval;
}

#define SERBAUDRATE 13

#define SERXHSHAKE 14 /* IMIV-226 */
#define SERMISC 16    /* IMIV-226 */
#define SERSETDTR 17  /* IMIV-226 */
#define SERCLRDTR 18  /* IMIV-226 */

#define SERCHAR 19

#define SERXCHAR 20 /* IMIV-226 */

#define SERUSETXOFF 21
#define SERUCLRXOFF 22
#define SERCXMITXON 23
#define SERUXMITXON 24
#define SERCXMITXOFF 25
#define SERUXMITXOFF 26
#define SERRESET 27

#define SERKILLIO 1

A2(PUBLIC, OSErr, ROMlib_serialctl, ParmBlkPtr, pbp, /* INTERNAL */
   DCtlPtr, dcp)
{
    OSErr err;
    hiddenh h;
    char c;

    if (!(dcp->dCtlFlags & CWC(OPENBIT)))
        err = notOpenErr;
    else {
        warning_trace_info("serial control code = %d, param0 = 0x%x", (LONGINT)CW(pbp->cntrlParam.csCode),
                           (LONGINT)(unsigned short)CW(pbp->cntrlParam.csParam[0]));
        h = (hiddenh)PPR(dcp->dCtlStorage);
        switch (CW(pbp->cntrlParam.csCode)) {
        case SERKILLIO:
            err = noErr; /* All I/O done synchronously */
            break;
        case SERSET:
            err = serset(HxX(h, fd), CW(pbp->cntrlParam.csParam[0]));
            break;
        case SERSETBUF:
            err = noErr; /* ignored */
            break;
        case SERHSHAKE:
        case SERXHSHAKE: /* NOTE:  DTR handshake isn't supported  */
            err = serxhshake(HxX(h, fd), (SerShk*)pbp->cntrlParam.csParam);
            break;
        case SERSETBRK:
            err = ctlbrk(HxX(h, fd), SER_START);
            break;
        case SERCLRBRK:
            err = ctlbrk(HxX(h, fd), SER_STOP);
            break;
        case SERBAUDRATE:
            err = setbaud(HxX(h, fd), CW(pbp->cntrlParam.csParam[0]));
            break;
        case SERMISC:
            err = serial_bios_setdtr(HxX(h, fd));
            break;
        case SERSETDTR:
            err = serial_bios_clrdtr(HxX(h, fd));
            break;
        case SERCLRDTR:
            err = controlErr; /* not supported */
            break;
        case SERCHAR:
            err = controlErr; /* not supported */
            break;
        case SERXCHAR:
            err = controlErr; /* not supported */
            break;
        case SERUSETXOFF:
            err = flow(HxX(h, fd), SER_STOP);
            break;
        case SERUCLRXOFF:
            err = flow(HxX(h, fd), SER_START);
            break;
        case SERCXMITXON:
            err = controlErr; /* not supported */
            break;
        case SERUXMITXON:
            c = XONC;
            err = write(HxX(h, fd), &c, 1) != 1 ? ROMlib_maperrno() : noErr;
            break;
        case SERCXMITXOFF:
            err = controlErr; /* not supported */
            break;
        case SERUXMITXOFF:
            c = XOFFC;
            err = write(HxX(h, fd), &c, 1) != 1 ? ROMlib_maperrno() : noErr;
            break;
        case SERRESET:
            err = controlErr; /* not supported */
            break;
        default:
            err = controlErr;
            break;
        }
    }
    warning_trace_info("serial control returning %d", (LONGINT)err);
    DOCOMPLETION(pbp, err);
}

/*
 * NOTE:  SERSTATUS lies about everything except rdPend.
 */

A2(PUBLIC, OSErr, ROMlib_serialstatus, ParmBlkPtr, pbp, /* INTERNAL */
   DCtlPtr, dcp)
{
    OSErr err;
    hiddenh h;
    LONGINT n;

    if (!(dcp->dCtlFlags & CWC(OPENBIT)))
        err = notOpenErr;
    else {
        warning_trace_info("serial status csCode = %d", (LONGINT)CW(pbp->cntrlParam.csCode));
        h = (hiddenh)PPR(dcp->dCtlStorage);
        switch (CW(pbp->cntrlParam.csCode)) {
        case SERGETBUF:
            if (serial_bios_fionread(HxX(h, fd), &n) < 0)
                err = ROMlib_maperrno();
            else {
                warning_trace_info("serial status getbuf = %d", (LONGINT)n);
                *(LONGINT*)pbp->cntrlParam.csParam = CL(n);
                err = noErr;
            }
            break;
        case SERSTATUS:
            if (serial_bios_fionread(HxX(h, fd), &n) < 0)
                err = ROMlib_maperrno();
            else {
                ((SerStaRec*)pbp->cntrlParam.csParam)->cumErrs = 0;
                ((SerStaRec*)pbp->cntrlParam.csParam)->xOffSent = 0;
                ((SerStaRec*)pbp->cntrlParam.csParam)->rdPend = n > 0 ? 1 : 0;
                ((SerStaRec*)pbp->cntrlParam.csParam)->wrPend = 0;
                ((SerStaRec*)pbp->cntrlParam.csParam)->ctsHold = 0;
                ((SerStaRec*)pbp->cntrlParam.csParam)->xOffHold = 0;
                err = noErr;
            }
            break;
        default:
            err = statusErr;
            break;
        }
    }
    warning_trace_info("serial status returning %d", (LONGINT)err);
    DOCOMPLETION(pbp, err);
}

PRIVATE void restorecloseanddispose(hiddenh h)
{
    DisposHandle((Handle)h);
}

A2(PUBLIC, OSErr, ROMlib_serialclose, ParmBlkPtr, pbp, /* INTERNAL */
   DCtlPtr, dcp)
{
    OSErr err;
    hiddenh h;

    if (dcp->dCtlFlags & CWC(OPENBIT)) {
        h = (hiddenh)PPR(dcp->dCtlStorage);
        restorecloseanddispose(h);
        dcp->dCtlFlags &= CWC(~OPENBIT);
        err = noErr;
    }
    else
        err = notOpenErr;
    warning_trace_info("serial close returning %d", (LONGINT)err);
    DOCOMPLETION(pbp, err);
}
