/*+===================================================================
  File:      application_vectors.h

  Summary:   Wraps the installed menu, window, dialog, resource, and folder traps.

  Classes:   None.

  Functions: Private frame wrappers for the export registry.

  Origin:    CarbonLib 1.4 toolbox wrappers, Universal Interfaces 3.4.2 trap
             declarations, and Executor's Pascal trap signatures.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_APPLICATION_VECTORS_H
#define ROMAW_CARBON_APPLICATION_VECTORS_H

/* Menu Manager. */
TOOLBOX_VECTOR(AppendMenu, 0x12cc, 0x3c0, uint32_t)
TOOLBOX_VECTOR(ClearMenuBar, 0x12d0, 0x0, uint32_t)
TOOLBOX_VECTOR(CountMenuItems, 0x1340, 0xe0, uint16_t)
TOOLBOX_VECTOR(DeleteMenu, 0x12d8, 0x80, uint32_t)
TOOLBOX_VECTOR(DeleteMenuItem, 0x1348, 0x2c0, uint32_t)
TOOLBOX_VECTOR(DisposeMenu, 0x12c8, 0xc0, uint32_t)
TOOLBOX_VECTOR(DrawMenuBar, 0x12dc, 0x0, uint32_t)
TOOLBOX_VECTOR(GetItemCmd, 0x0f38, 0xec0, uint32_t)
TOOLBOX_VECTOR(GetItemMark, 0x130c, 0xec0, uint32_t)
TOOLBOX_VECTOR(GetItemStyle, 0x1304, 0xec0, uint32_t)
TOOLBOX_VECTOR(GetMenu, 0x14fc, 0xb0, uint32_t)
TOOLBOX_VECTOR(GetMenuBar, 0x12ec, 0x30, uint32_t)
TOOLBOX_VECTOR(GetNewMBar, 0x1500, 0xb0, uint32_t)
TOOLBOX_VECTOR(InsertMenu, 0x12d4, 0x2c0, uint32_t)
TOOLBOX_VECTOR(InsertMenuItem, 0x0e98, 0xbc0, uint32_t)
TOOLBOX_VECTOR(InsertResMenu, 0x1344, 0xbc0, uint32_t)
TOOLBOX_VECTOR(SetItemCmd, 0x0f3c, 0xac0, uint32_t)
TOOLBOX_VECTOR(SetItemIcon, 0x1300, 0xac0, uint32_t)
TOOLBOX_VECTOR(SetItemStyle, 0x1308, 0xac0, uint32_t)
TOOLBOX_VECTOR(SetMenuBar, 0x12f0, 0xc0, uint32_t)
TOOLBOX_VECTOR(SetMenuFlashCount, 0x1328, 0x80, uint32_t)
TOOLBOX_VECTOR(SetMenuItemText, 0x131c, 0xec0, uint32_t)

/* Window and Dialog Managers. */
TOOLBOX_VECTOR(DialogSelect, 0x1400, 0xfd0, uint8_t)
TOOLBOX_VECTOR(DisposeWindow, 0x1250, 0xc0, uint32_t)
TOOLBOX_VECTOR(DragWindow, 0x1294, 0xfc0, uint32_t)
TOOLBOX_VECTOR(FindWindow, 0x12b0, 0x3e0, int16_t)
TOOLBOX_VECTOR(FrontWindow, 0x1290, 0x30, uint32_t)
TOOLBOX_VECTOR(GetNewCWindow, 0x1718, 0xfb0, uint32_t)
TOOLBOX_VECTOR(GetNewDialog, 0x13f0, 0xfb0, uint32_t)
TOOLBOX_VECTOR(GetNewWindow, 0x14f4, 0xfb0, uint32_t)
TOOLBOX_VECTOR(GrowWindow, 0x12ac, 0xff0, uint32_t)
TOOLBOX_VECTOR(NewCWindow, 0x1714, 0x379ff0, uint32_t)
TOOLBOX_VECTOR(NewDialog, 0x13f4, 0xf79ff0, uint32_t)
TOOLBOX_VECTOR(NewWindow, 0x1714, 0x379ff0, uint32_t)
TOOLBOX_VECTOR(SetWTitle, 0x1268, 0x3c0, uint32_t)
TOOLBOX_VECTOR(SizeWindow, 0x1274, 0x1ac0, uint32_t)
TOOLBOX_VECTOR(TrackBox, 0x0eec, 0xbd0, uint8_t)
TOOLBOX_VECTOR(ZoomWindow, 0x0ee8, 0x6c0, uint32_t)

/* Control Manager. */
TOOLBOX_VECTOR(GetAuxiliaryControlRecord, 0x1710, 0x3d0, uint8_t)
TOOLBOX_VECTOR(GetControlAction, 0x13a8, 0xf0, uint32_t)
TOOLBOX_VECTOR(GetControlMaximum, 0x1388, 0xe0, int16_t)
TOOLBOX_VECTOR(GetControlMinimum, 0x1384, 0xe0, int16_t)
TOOLBOX_VECTOR(GetControlReference, 0x1368, 0xf0, uint32_t)
TOOLBOX_VECTOR(GetControlTitle, 0x1378, 0x3c0, uint32_t)
TOOLBOX_VECTOR(GetControlValue, 0x1380, 0xe0, int16_t)
TOOLBOX_VECTOR(GetControlVariant, 0xe24, 0xe0, int16_t)
TOOLBOX_VECTOR(SetControlAction, 0x13ac, 0x3c0, uint32_t)
TOOLBOX_VECTOR(SetControlMaximum, 0x1394, 0x2c0, uint32_t)
TOOLBOX_VECTOR(SetControlMinimum, 0x1390, 0x2c0, uint32_t)
TOOLBOX_VECTOR(SetControlReference, 0x136c, 0x3c0, uint32_t)
TOOLBOX_VECTOR(SetControlTitle, 0x137c, 0x3c0, uint32_t)
TOOLBOX_VECTOR(SetControlValue, 0x138c, 0x2c0, uint32_t)
TOOLBOX_VECTOR(UpdateControls, 0x134c, 0x3c0, uint32_t)

/* Resource Manager. */
TOOLBOX_VECTOR(CloseResFile, 0x1468, 0x80, uint32_t)

/* Folder Manager, selector zero in D0. */
TOOLBOX_SELECTOR(FindFolder, 0x0e8c, 0x3dea8, 0x0, int16_t)

#endif
