/*+===================================================================
  File:      window_property_entries.h

  Summary:   Maps Window and Control Manager properties and window class operations to native frame handlers.

  Classes:   None.

  Functions: Private PowerPC frame wrappers for the export registry.

  Origin:    CarbonLib 1.4 and Universal Interfaces 3.4.2 MacWindows.h and Controls.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_WINDOW_PROPERTY_ENTRIES_H
#define ROMAW_CARBON_WINDOW_PROPERTY_ENTRIES_H

#define CARBON_WINDOW_PROPERTY(name, offset, operation, argument)                                  \
    static int carbon_window_##name(ROMlib_carbon_ppc_frame* call)                                 \
    {                                                                                              \
        return ROMlib_carbon_window_property(#name, offset, property_##operation, argument, call); \
    }

CARBON_WINDOW_PROPERTY(GetWindowProperty, 0x8d10, get, 0)
CARBON_WINDOW_PROPERTY(GetWindowPropertySize, 0x8f50, size, 0)
CARBON_WINDOW_PROPERTY(SetWindowProperty, 0x9138, set, 0)
CARBON_WINDOW_PROPERTY(RemoveWindowProperty, 0x9348, remove, 0)
CARBON_WINDOW_PROPERTY(GetWindowPropertyAttributes, 0x9548, attributes, 0)
CARBON_WINDOW_PROPERTY(ChangeWindowPropertyAttributes, 0x975c, change, 0)
CARBON_WINDOW_PROPERTY(GetWindowSystemProperty, 0x9824, get, 1)
CARBON_WINDOW_PROPERTY(GetWindowSystemPropertySize, 0x98e0, size, 1)
CARBON_WINDOW_PROPERTY(SetWindowSystemProperty, 0x9984, set, 1)
CARBON_WINDOW_PROPERTY(RemoveWindowSystemProperty, 0x9a34, remove, 1)
CARBON_WINDOW_PROPERTY(GetWindowSystemPropertyAttributes, 0x9acc, attributes, 1)
CARBON_WINDOW_PROPERTY(ChangeWindowSystemPropertyAttributes, 0x9b70, change, 1)
CARBON_WINDOW_PROPERTY(SetWindowDefaultButton, 0x3ff14, button_set, 0x64666c74)
CARBON_WINDOW_PROPERTY(SetWindowCancelButton, 0x3ffac, button_set, 0x636e636c)
CARBON_WINDOW_PROPERTY(GetWindowDefaultButton, 0x40044, button_get, 0x64666c74)
CARBON_WINDOW_PROPERTY(GetWindowCancelButton, 0x40108, button_get, 0x636e636c)
CARBON_WINDOW_PROPERTY(CopyWindowTitleAsCFString, 0x1363c, title_get, 0)
CARBON_WINDOW_PROPERTY(SetWindowTitleWithCFString, 0x0fca8, title_set, 0)
CARBON_WINDOW_PROPERTY(CopyWindowAlternateTitle, 0x1388c, title_get, 1)
CARBON_WINDOW_PROPERTY(SetWindowAlternateTitle, 0x1372c, title_set, 1)
CARBON_WINDOW_PROPERTY(GetWindowClass, 0xd238, metadata_get, 6)
CARBON_WINDOW_PROPERTY(GetWindowAttributes, 0xd7e8, metadata_get, 7)
CARBON_WINDOW_PROPERTY(ChangeWindowAttributes, 0xe334, window_attributes_change, 0)
CARBON_WINDOW_PROPERTY(GetNextWindowOfClass, 0x10d40, window_class_search, 0)
CARBON_WINDOW_PROPERTY(GetFrontWindowOfClass, 0x10f1c, window_class_search, 1)
CARBON_WINDOW_PROPERTY(GetSheetWindowParent, 0x14a2c, sheet_parent, 0)
CARBON_WINDOW_PROPERTY(_IsWindowLatentVisible, 0x5a580, latent_visibility, 0)
CARBON_WINDOW_PROPERTY(ScrollWindowRegion, 0xa030, scroll_region, 0)
CARBON_WINDOW_PROPERTY(ScrollWindowRect, 0xa3f0, scroll_rect, 0)
CARBON_WINDOW_PROPERTY(FindWindowOfClass, 0x11188, find_window_of_class, 0)
CARBON_WINDOW_PROPERTY(GetWindowOwnerCount, 0xd3ac, metadata_get, 13)
CARBON_WINDOW_PROPERTY(GetWindowActivationScope, 0x10bc8, metadata_get, 14)
CARBON_WINDOW_PROPERTY(SetWindowActivationScope, 0x34c88, scope_set, 0)
CARBON_WINDOW_PROPERTY(SetWindowClass, 0x145b0, class_set, 0)
CARBON_WINDOW_PROPERTY(_IsValidWindowClass, 0x340d8, class_valid, 0)
CARBON_WINDOW_PROPERTY(_GetAvailableWindowAttributes, 0x34120, available_attributes, 0)

#undef CARBON_WINDOW_PROPERTY

#define CARBON_WINDOW_GROUP(name, offset)                          \
    static int carbon_window_##name(ROMlib_carbon_ppc_frame* call) \
    {                                                              \
        return ROMlib_carbon_window_group(#name, offset, call);    \
    }

CARBON_WINDOW_GROUP(GetWindowGroupOfClass, 0x34210)
CARBON_WINDOW_GROUP(GetWindowGroup, 0x342dc)
CARBON_WINDOW_GROUP(SetWindowGroup, 0x34340)
CARBON_WINDOW_GROUP(IsWindowContainedInGroup, 0x3441c)
CARBON_WINDOW_GROUP(GetIndexedWindow, 0x3467c)
CARBON_WINDOW_GROUP(CountWindowGroupContents, 0x346d8)
CARBON_WINDOW_GROUP(GetWindowGroupContents, 0x347f0)
CARBON_WINDOW_GROUP(GetWindowIndex, 0x34980)

#undef CARBON_WINDOW_GROUP

#define CARBON_WINDOW_ACTIVATION(name, offset)                       \
    static int carbon_window_##name(ROMlib_carbon_ppc_frame* call)   \
    {                                                                \
        return ROMlib_carbon_window_activation(#name, offset, call); \
    }

CARBON_WINDOW_ACTIVATION(FrontNonFloatingWindow, 0xf058)
CARBON_WINDOW_ACTIVATION(AreFloatingWindowsVisible, 0x11838)
CARBON_WINDOW_ACTIVATION(ShowFloatingWindows, 0x12618)
CARBON_WINDOW_ACTIVATION(HideFloatingWindows, 0x1263c)
CARBON_WINDOW_ACTIVATION(ActiveNonFloatingWindow, 0x34ae8)
CARBON_WINDOW_ACTIVATION(IsWindowActive, 0x34b1c)
CARBON_WINDOW_ACTIVATION(ActivateWindow, 0x34c14)
CARBON_WINDOW_ACTIVATION(GetNextWindow, 0xd2a4)
CARBON_WINDOW_ACTIVATION(GetWindowList, 0xed38)
CARBON_WINDOW_ACTIVATION(GetUserFocusWindow, 0x33b64)
CARBON_WINDOW_ACTIVATION(SetUserFocusWindow, 0x33894)
CARBON_WINDOW_ACTIVATION(GetWindowIdealUserState, 0x250cc)
CARBON_WINDOW_ACTIVATION(SetWindowIdealUserState, 0x2531c)
CARBON_WINDOW_ACTIVATION(ZoomWindowIdeal, 0x254f8)
CARBON_WINDOW_ACTIVATION(ConstrainWindowToScreen, 0x24f30)
CARBON_WINDOW_ACTIVATION(IsWindowInStandardState, 0x257b4)
CARBON_WINDOW_ACTIVATION(GetWindowWidgetHilite, 0x1d998)
CARBON_WINDOW_ACTIVATION(GetWindowStructureWidths, 0x59b5c)
CARBON_WINDOW_ACTIVATION(SetWindowContentColor, 0x5a228)
CARBON_WINDOW_ACTIVATION(GetWindowContentColor, 0x5a404)
CARBON_WINDOW_ACTIVATION(MoveWindowStructure, 0x59c40)
CARBON_WINDOW_ACTIVATION(SetWindowBounds, 0x59810)
CARBON_WINDOW_ACTIVATION(GetAvailableWindowPositioningBounds, 0x249e0)
CARBON_WINDOW_ACTIVATION(GetWindowGreatestAreaDevice, 0x24b0c)

#undef CARBON_WINDOW_ACTIVATION

#define CARBON_CONTROL_PROPERTY(name, offset, operation, system)                                  \
    static int carbon_control_##name(ROMlib_carbon_ppc_frame* call)                               \
    {                                                                                             \
        return ROMlib_carbon_control_property(#name, offset, property_##operation, system, call); \
    }

CARBON_CONTROL_PROPERTY(GetControlProperty, 0x7c14, get, 0)
CARBON_CONTROL_PROPERTY(GetControlPropertySize, 0x7e3c, size, 0)
CARBON_CONTROL_PROPERTY(SetControlProperty, 0x802c, set, 0)
CARBON_CONTROL_PROPERTY(RemoveControlProperty, 0x8224, remove, 0)
CARBON_CONTROL_PROPERTY(GetControlPropertyAttributes, 0x843c, attributes, 0)
CARBON_CONTROL_PROPERTY(ChangeControlPropertyAttributes, 0x8658, change, 0)
CARBON_CONTROL_PROPERTY(GetControlSystemProperty, 0x86f8, get, 1)
CARBON_CONTROL_PROPERTY(GetControlSystemPropertySize, 0x878c, size, 1)
CARBON_CONTROL_PROPERTY(SetControlSystemProperty, 0x8818, set, 1)
CARBON_CONTROL_PROPERTY(RemoveControlSystemProperty, 0x88a0, remove, 1)
CARBON_CONTROL_PROPERTY(GetControlSystemPropertyAttributes, 0x8918, attributes, 1)
CARBON_CONTROL_PROPERTY(ChangeControlSystemPropertyAttributes, 0x89a4, change, 1)

#undef CARBON_CONTROL_PROPERTY

#define CARBON_WINDOWS_COMPAT(name, offset)                              \
    static int carbon_window_##name(ROMlib_carbon_ppc_frame* call)       \
    {                                                                    \
        return ROMlib_carbon_windows_compatibility(#name, offset, call); \
    }

CARBON_WINDOWS_COMPAT(GetWindowVisualClass, 0x548)
CARBON_WINDOWS_COMPAT(GetWindowProcess, 0x330c)
CARBON_WINDOWS_COMPAT(SetWindowModified, 0x33bc)
CARBON_WINDOWS_COMPAT(IsWindowModified, 0x3474)
CARBON_WINDOWS_COMPAT(SetWindowGadgetDropThreshold, 0x5c9c)
CARBON_WINDOWS_COMPAT(GetWindowGadgetDropThreshold, 0x5d20)
CARBON_WINDOWS_COMPAT(SetWindowDoubleClickAction, 0x7464)
CARBON_WINDOWS_COMPAT(GetWindowDoubleClickAction, 0x74dc)
CARBON_WINDOWS_COMPAT(DoubleClickWindowTitle, 0x75b0)
CARBON_WINDOWS_COMPAT(TransitionWindow, 0x1980)
CARBON_WINDOWS_COMPAT(SetWindowFeatures, 0x5a70)
CARBON_WINDOWS_COMPAT(WMGetWindowFeatures, 0x5ae4)
CARBON_WINDOWS_COMPAT(WMIsWindowCollapsable, 0xf5a8)
CARBON_WINDOWS_COMPAT(WMIsWindowCollapsed, 0xf5f4)
CARBON_WINDOWS_COMPAT(IsWindowPathSelectClick, 0x49f0)
CARBON_WINDOWS_COMPAT(GetWindowContentPattern, 0xd5a4)
CARBON_WINDOWS_COMPAT(SetWindowContentPattern, 0xd43c)
CARBON_WINDOWS_COMPAT(WindowManagerSetThemeWindowBackground, 0xd104)
CARBON_WINDOWS_COMPAT(GetNextWindowActivateEvent, 0x80cc)
CARBON_WINDOWS_COMPAT(PostDelayedWindowReshape, 0x8630)
CARBON_WINDOWS_COMPAT(PostWindowActivateEvent, 0x8214)
CARBON_WINDOWS_COMPAT(HiliteWindowFrameForDrag, 0x6354)
CARBON_WINDOWS_COMPAT(GetWindowProxyAlias, 0x3d34)
CARBON_WINDOWS_COMPAT(GetWindowProxyFSSpec, 0x3a70)
CARBON_WINDOWS_COMPAT(GetWindowProxyIcon, 0x38cc)
CARBON_WINDOWS_COMPAT(SetWindowProxyAlias, 0x3b14)
CARBON_WINDOWS_COMPAT(SetWindowProxyFSSpec, 0x39ec)
CARBON_WINDOWS_COMPAT(SetWindowProxyIcon, 0x36e8)
CARBON_WINDOWS_COMPAT(SetWindowProxyCreatorAndType, 0x3640)
CARBON_WINDOWS_COMPAT(RemoveWindowProxy, 0x34f4)
CARBON_WINDOWS_COMPAT(BeginWindowProxyDrag, 0x453c)
CARBON_WINDOWS_COMPAT(EndWindowProxyDrag, 0x4798)
CARBON_WINDOWS_COMPAT(TrackWindowProxyDrag, 0x431c)
CARBON_WINDOWS_COMPAT(TrackWindowProxyFromExistingDrag, 0x402c)
CARBON_WINDOWS_COMPAT(WMGetWindowRegion, 0x6458)
CARBON_WINDOWS_COMPAT(GlobalFindWindow, 0xa3c4)
CARBON_WINDOWS_COMPAT(InstallWindowContentPaintProc, 0xc8f8)
CARBON_WINDOWS_COMPAT(EraseWindowRegion, 0xcc24)
CARBON_WINDOWS_COMPAT(SetDesktopPattern, 0x6df8)
CARBON_WINDOWS_COMPAT(GetDesktopPattern, 0x6f50)
CARBON_WINDOWS_COMPAT(GetDesktopPixPat, 0x723c)
CARBON_WINDOWS_COMPAT(DrawDesktop, 0x7418)
CARBON_WINDOWS_COMPAT(RebuildDesktopRegion, 0x6950)
CARBON_WINDOWS_COMPAT(GetCurrentMonitorDesktopList, 0x18f44)
CARBON_WINDOWS_COMPAT(SetCurrentMonitorDesktopList, 0x18fc8)
CARBON_WINDOWS_COMPAT(DisposeMonitorDesktopList, 0x190d0)
CARBON_WINDOWS_COMPAT(RefreshMonitorDesktopList, 0x19188)
CARBON_WINDOWS_COMPAT(GetInternalMonitorDesktopList, 0x19224)
CARBON_WINDOWS_COMPAT(InternalMonitorDesktopListChanged, 0x19244)
CARBON_WINDOWS_COMPAT(CountMonitorDesktopRecordsInList, 0x19294)
CARBON_WINDOWS_COMPAT(GetMonitorDesktopRecordFromList, 0x192f0)
CARBON_WINDOWS_COMPAT(SetMonitorDesktopRecordFromList, 0x19344)
CARBON_WINDOWS_COMPAT(MonitorDesktopHasPicture, 0x19794)
CARBON_WINDOWS_COMPAT(GetMonitorDesktopAlignment, 0x19810)
CARBON_WINDOWS_COMPAT(SetMonitorDesktopAlignment, 0x19894)
CARBON_WINDOWS_COMPAT(SetMonitorDesktopToFile, 0x1950c)
CARBON_WINDOWS_COMPAT(CopyMonitorDesktopFile, 0x195f8)
CARBON_WINDOWS_COMPAT(SetDesktopPictureForDevice, 0x1999c)
CARBON_WINDOWS_COMPAT(GetDesktopPictureForDevice, 0x19a64)
CARBON_WINDOWS_COMPAT(FileContainsPossibleDesktopPicture, 0x193e0)
CARBON_WINDOWS_COMPAT(GetPictureFromFile, 0x169a4)
CARBON_WINDOWS_COMPAT(ConstrainedDragRgn, 0x1381c)
CARBON_WINDOWS_COMPAT(DrawMonitorDesktopFromList, 0x19bc4)
CARBON_WINDOWS_COMPAT(DrawMonitorDesktopFromListWillTakeTime, 0x1a270)
CARBON_WINDOWS_COMPAT(RepositionWindow, 0xe8b8)
CARBON_WINDOWS_COMPAT(ReshapeWindows, 0x11c30)
CARBON_WINDOWS_COMPAT(RecalculateAllWindowTitleWidths, 0x2488)
CARBON_WINDOWS_COMPAT(WMCollapseWindow, 0xf644)
CARBON_WINDOWS_COMPAT(WMCollapseAllWindows, 0xf934)

#undef CARBON_WINDOWS_COMPAT

#define CARBON_SCREEN_BITS(name, offset)                                    \
    static int carbon_window_##name(ROMlib_carbon_ppc_frame* call)          \
    {                                                                       \
        return ROMlib_carbon_window_saved_screen_bits(#name, offset, call); \
    }

CARBON_SCREEN_BITS(IsScreenBitSavingEnabled, 0xd798)
CARBON_SCREEN_BITS(SetScreenBitSavingEnabled, 0xd7a0)
CARBON_SCREEN_BITS(SaveScreenBits, 0xd7c0)
CARBON_SCREEN_BITS(RestoreScreenBits, 0xdbe0)
CARBON_SCREEN_BITS(MoveSavedScreenBits, 0xde28)
CARBON_SCREEN_BITS(DisposeSavedScreenBits, 0xde84)
CARBON_SCREEN_BITS(GetSavedScreenBitsRepresentativePixMap, 0xdef4)

#undef CARBON_SCREEN_BITS

static int carbon_IsWindowPathSelectEvent(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], event = call->words[1];
    _Alignas(16) EventRecord record;
    const uint32_t address = US_TO_SYN68K(&record);
    ROMlib_carbon_ppc_frame convert = {.words = {event, address}};
    if (event && !carbon_event_ConvertEventRefToEventRecord(&convert))
        return 0;
    if (!event || !convert.words[0]) {
        call->words[0] = 0;
        return 1;
    }
    call->words[0] = window;
    call->words[1] = address;
    return carbon_window_IsWindowPathSelectClick(call);
}

static int carbon_CreateWindowFromResource(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t output = call->words[1];
    if (!output || !ROMlib_carbon_try_guest_memory(output, 4, 1)) {
        call->words[0] = paramErr;
        return 1;
    }

    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("CarbonLib", "CreateWindowFromResource", &ref))
        return 0;
    if (ref.code_offset != 0x1068c || ref.code_size != 0xb8fb4 || ref.toc_offset != 0x2f78 || ref.toc_size != 0x10f0c)
        return ROMlib_carbon_invoke_frame(ref.function, call);

    const uint32_t get[] = {TICK("wind"), (uint32_t)(int16_t)call->words[0]};
    uint32_t resource, status;
    if (!cfp_private(&ref, 0xb1fb0, get, NELEM(get), &resource) || !cfp_private(&ref, 0xb1ff8, NULL, 0, &status))
        return 0;
    if (status || !resource) {
        call->words[0] = status ? status : (uint32_t)resNotFound;
        return 1;
    }

    uint32_t collection = 0;
    int ok = cfp_private(&ref, 0xb6240, NULL, 0, &collection);
    if (ok && collection) {
        const uint32_t load[] = {collection, resource}, create[] = {collection, output};
        ok = cfp_private(&ref, 0xb6258, load, NELEM(load), &status) &&
             (status || cfp_private(&ref, 0xb2ac0, create, NELEM(create), &status));
        if (!cfp_private(&ref, 0xb6270, &collection, 1, NULL))
            ok = 0;
    }
    else
        status = memFullErr;
    if (!cfp_private(&ref, 0xb1f98, &resource, 1, NULL))
        ok = 0;
    call->words[0] = status;
    return ok;
}

#endif
