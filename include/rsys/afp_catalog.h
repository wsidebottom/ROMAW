/*+===================================================================
  File:      afp_catalog.h

  Summary:   Guest File Manager AFP catalog adapter.

  Classes:   None.

  Functions: Catalog result translation and AFP error mapping declarations.

  Origin:    ROMAW AFP integration.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#ifndef RSYS_AFP_CATALOG_H
#define RSYS_AFP_CATALOG_H
#include "FileMgr.h"
#include "rsys/host_afp.h"

OSErr ROMlib_afp_catalog_index(romaw_afp_session* session, uint16 volume, uint32 directory, CInfoPBPtr pb);
OSErr ROMlib_afp_file_error(int32 result);
BOOLEAN ROMlib_afp_is_volume(const VCB* volume);
OSErr ROMlib_afp_register_volume(uint32 address, uint16 port, const char* name, INTEGER* reference);
OSErr ROMlib_afp_mounted_catalog(VCB* volume, uint32 directory, CInfoPBPtr pb);
BOOLEAN ROMlib_afp_is_fork(INTEGER reference);
OSErr ROMlib_afp_open(VCB* volume, uint32 directory, HParmBlkPtr pb, BOOLEAN resource);
OSErr ROMlib_afp_read(ioParam* pb);
OSErr ROMlib_afp_close_fork(INTEGER reference);
OSErr ROMlib_afp_set_position(ioParam* pb);
OSErr ROMlib_afp_unmount(VCB* volume);
OSErr ROMlib_afp_volume_mount(ParmBlkPtr pb, BOOLEAN async);
OSErr ROMlib_afp_open_wd(VCB* volume, uint32 directory, WDPBPtr pb);
void ROMlib_afp_install_chooser_entry(Handle resource);
#endif
