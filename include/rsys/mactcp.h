/*+===================================================================
  File:      mactcp.h

  Summary:   MacTCP .IPP address and ICMP driver entry points.

  Classes:   None.

  Functions: Interface declarations for the MacTCP driver, resolver, polling and reset entry points.

  Origin:    ROMAW implementation of the MacTCP parameter-block interface.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#ifndef RSYS_MACTCP_H
#define RSYS_MACTCP_H
#include "DeviceMgr.h"
#include "FileMgr.h"
#define ROMAW_MACTCP_REFNUM (-23)
OSErr ROMlib_mactcp_dispatch(ParmBlkPtr pb, BOOLEAN async, DriverRoutineType routine);
OSErr ROMlib_mactcp_driver_open(ParmBlkPtr pb, DCtlPtr dce);
OSErr ROMlib_mactcp_driver_prime(ParmBlkPtr pb, DCtlPtr dce);
OSErr ROMlib_mactcp_driver_control(ParmBlkPtr pb, DCtlPtr dce);
OSErr ROMlib_mactcp_driver_status(ParmBlkPtr pb, DCtlPtr dce);
OSErr ROMlib_mactcp_driver_close(ParmBlkPtr pb, DCtlPtr dce);
ProcPtr ROMlib_mactcp_resolver_definition(void);
void* ROMlib_mactcp_memory(uint32 address, uint32 length, int write);
void ROMlib_mactcp_complete(ParmBlkPtr pb, BOOLEAN async, OSErr error);
OSErr ROMlib_mactcp_stream_control(ParmBlkPtr pb, BOOLEAN async);
void ROMlib_mactcp_stream_poll(void);
void ROMlib_mactcp_stream_reset(void);
void ROMlib_mactcp_resolver_poll(void);
void ROMlib_mactcp_resolver_reset(void);
void ROMlib_mactcp_poll(void);
void ROMlib_mactcp_reset(void);
#endif
