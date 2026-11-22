/*+===================================================================
  File:      commtool.c

  Summary:   Implements commtool support for Macintosh device and communications services.

  Classes:   None.

  Functions: CRMGetCRMVersion, CRMGetHeader, CRMInstall, CRMRemove, CRMSearch, InitCRM

  Origin:    Existing repository source in executor/src/devices; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1999 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"

#include "CommTool.h"
#include "MemoryMgr.h"
#include "OSUtil.h"

#include "rsys/blockinterrupts.h"
#include "rsys/string.h"
#include "rsys/mman.h"

PUBLIC INTEGER CRMGetCRMVersion(void)
{
    INTEGER retval;

    retval = curCRMVersion;
    return retval;
}

PRIVATE QHdr commtool_head;

PUBLIC QHdrPtr CRMGetHeader(void)
{
    QHdrPtr retval;

    retval = &commtool_head;
    return retval;
}

PUBLIC void CRMInstall(QElemPtr qp)
{
    virtual_int_state_t block;

    if (!qp)
        return;
    block = block_virtual_ints();
    if (!PP_RAW(commtool_head.qTail))
        ((CRMRecPtr)qp)->crmDeviceID = CLC(1);
    else {
        CRMRecPtr prevp;

        prevp = (CRMRecPtr)PPR(commtool_head.qTail);
        ((CRMRecPtr)qp)->crmDeviceID = CL(CL(prevp->crmDeviceID) + 1);
    }

    Enqueue(qp, &commtool_head);
    restore_virtual_ints(block);
}

PUBLIC OSErr CRMRemove(QElemPtr qp)
{
    OSErr retval;

    if (!qp)
        return paramErr;
    retval = Dequeue(qp, &commtool_head);
    return retval;
}

PUBLIC QElemPtr CRMSearch(QElemPtr qp)
{
    QElemPtr retval;
    virtual_int_state_t block;
    CRMRecPtr p;
    LONGINT min;

    if (!qp)
        return NULL;
    min = CL(((CRMRecPtr)qp)->crmDeviceID);
    block = block_virtual_ints();

    for (p = (CRMRecPtr)PPR(commtool_head.qHead);
         p && (CL(p->crmDeviceID) <= min || p->crmDeviceType != ((CRMRecPtr)qp)->crmDeviceType);
         p = (CRMRecPtr)PPR(p->qLink))
        ;
    restore_virtual_ints(block);
    retval = (QElemPtr)p;
    return retval;
}

PRIVATE CRMErr serial_insert(const char* input, const char* output, const char* name, CRMRecPtr* recordp)
{
    CRMErr retval;
    CRMSerialPtr p;
    CRMRecPtr qp = NULL;
    StringHandle input_name = NULL, output_name = NULL, device_name = NULL;

    *recordp = NULL;
    p = (CRMSerialPtr)NewPtrSys(sizeof *p);
    if (!p)
        goto allocation_failed;
    qp = (CRMRecPtr)NewPtrSys(sizeof *qp);
    if (!qp)
        goto allocation_failed;
    input_name = stringhandle_from_c_string(input);
    if (!input_name)
        goto allocation_failed;
    output_name = stringhandle_from_c_string(output);
    if (!output_name)
        goto allocation_failed;
    device_name = stringhandle_from_c_string(name);
    if (!device_name)
        goto allocation_failed;

    p->version = CWC(curCRMSerRecVer);
    FIELD_ASSIGN_RAW(p->inputDriverName, RM(input_name));
    FIELD_ASSIGN_RAW(p->outputDriverName, RM(output_name));
    FIELD_ASSIGN_RAW(p->name, RM(device_name));
    PP_RAW(p->deviceIcon) = 0;
    p->ratedSpeed = CLC(19200);
    p->maxSpeed = CLC(57600);
    p->reserved = CLC(0);
    PP_RAW(qp->qLink) = CLC(0);
    qp->qType = CWC(crmType);
    qp->crmVersion = CWC(crmRecVersion);
    qp->crmPrivate = CLC(0);
    qp->crmReserved = CWC(0);
    qp->crmDeviceType = CLC(crmSerialDevice);
    qp->crmDeviceID = CLC(0);
    qp->crmAttributes = (LONGINT)RM(p);
    qp->crmStatus = CLC(0);
    qp->crmRefCon = CLC(0);
    CRMInstall((QElemPtr)qp);
    *recordp = qp;
    return noErr;

allocation_failed:
    /* Disposal may change MemError, so preserve the allocation error first. */
    retval = MemError();
    if (device_name)
        DisposHandle((Handle)device_name);
    if (output_name)
        DisposHandle((Handle)output_name);
    if (input_name)
        DisposHandle((Handle)input_name);
    if (qp)
        DisposPtr((Ptr)qp);
    if (p)
        DisposPtr((Ptr)p);
    return retval;
}

PRIVATE void dispose_serial_record(CRMRecPtr record)
{
    CRMSerialPtr attributes = MR((CRMSerialPtr)record->crmAttributes);

    DisposHandle((Handle)PPR(attributes->inputDriverName));
    DisposHandle((Handle)PPR(attributes->outputDriverName));
    DisposHandle((Handle)PPR(attributes->name));
    DisposPtr((Ptr)attributes);
    DisposPtr((Ptr)record);
}

PUBLIC CRMErr InitCRM(void)
{
    static boolean_t beenhere;
    CRMErr retval;

    if (beenhere)
        retval = noErr;
    else {
        CRMRecPtr first, second;

        ZONE_SAVE_EXCURSION(SysZone, {
            retval = serial_insert(".AIn", ".AOut", "COM1", &first);
            if (retval == noErr) {
                retval = serial_insert(".BIn", ".BOut", "COM2", &second);
                if (retval != noErr) {
                    CRMRemove((QElemPtr)first);
                    dispose_serial_record(first);
                }
            }
            beenhere = retval == noErr;
        });
    }
    return retval;
}
