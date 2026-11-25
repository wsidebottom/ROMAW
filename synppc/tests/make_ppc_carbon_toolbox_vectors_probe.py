# +===================================================================
#   File:      make_ppc_carbon_toolbox_vectors_probe.py
#
#   Summary:   Compare live Pascal Toolbox vectors across native CarbonLib and its saved PowerPC entry points.
#
#   Classes:   ToolboxProbe.
#
#   Functions: vector_cases, install_vector_targets, patched_vectors, region_operations, graphics_and_resources, selector_operations, selector_descriptors,
#              upp_cases, routine_descriptors, upp_invocations, textedit_upp_invocations, direct_upp_cases, direct_upps,
#              register_cases, register_vectors, installed_memory_traps, toolbox_sections, write_toolbox_references,
#              math_compare, math_scalars, math_classification, math_environment, math_decimals, math_extended, math_long,
#              math_wide, file_cases, fsref_cases, file_parameter_blocks, file_operations, file_spec_operations,
#              launch_services_operations,
#              fsref_operations, apple_event_operations, romaw_service_operations, data_browser_operations,
#              data_browser_control_cases, data_browser_control_operations, icon_rgb_operations,
#              icon_rgb_draw_operations, color_sync_operations, write_color_profile_reference, color_profile_operations,
#              printing_operations, manager_operations, classic_utility_operations,
#              classic_clock_and_location, classic_state_operations, font_family_operations, font_instance_operations, font_catalog_operations, atsui_catalogue_operations, theme_cursor_operations, theme_font_operations, port_operations, region_rectangle_operations, borrowed_gworld_operations, textension_unicode_key_operations, textension_scrap_edits, textension_scrap_operations, textension_object_operations, sound_input_operations,
#              sound_converter_operations, power_activity_operations, code_fragment_operations, navigation_operations,
#              fixmath_wide_operations, fixed_decimal_operations,
#              classic_utility_accessors, font_script_queries, script_utility_operations,
#              geometry_operations, application_operations, window_metadata_operations, window_queue_operations,
#              window_proxy_operations,
#              collection_manager_operations, component_operations, menu_manager_operations, help_manager_operations,
#              control_manager_operations,
#              atsui_operations, atsui_style_edits, atsui_layout_operations, atsui_render_operations, make_atsui_probe, make_probe.
#
#   Origin:    Original grouped regression for the CarbonLib 1.4 low-memory Toolbox wrappers.
#
#   Copyright and Legal notices: See COPYING and synppc/COPYING.
# ===================================================================+

import argparse
import hashlib
import json
import math
import re
import struct
from decimal import ROUND_CEILING, ROUND_DOWN, ROUND_FLOOR, ROUND_HALF_EVEN, Decimal
from pathlib import Path

from carbon_event_checks import event_names, event_sections, window_property_names, windows_metadata_names
from carbon_text_scrap_checks import international_names, international_operations, text_scrap_rows, text_scrap_sections
from make_ppc_carbon_cf_frameworks_probe import FrameworksProbe
from make_ppc_carbon_cf_values_probe import ROOT, ValuesProbe
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef

ATSUI_REFERENCE_CALLS = (
    "FMActivateFonts",
    "FMDeactivateFonts",
)
FSREF_REFERENCE_CALLS = ("FSpMakeFSRef", "FSGetCatalogInfo", "FSGetVolumeInfo", "FSGetForkSize")
LOCALE_REFERENCE_CALLS = (
    "LocaleRefFromLangOrRegionCode",
    "LocaleRefFromLocaleString",
    "LocaleRefGetPartString",
    "LocaleStringToLangAndRegionCodes",
    "LocaleOperationCountLocales",
    "LocaleOperationGetLocales",
    "LocaleGetName",
    "LocaleCountNames",
    "LocaleGetIndName",
    "LocaleGetRegionLanguageName",
    "LocaleOperationGetName",
    "LocaleOperationCountNames",
    "LocaleOperationGetIndName",
    "LocaleOperationLoadData",
    "LocaleOperationUnloadData",
)
MENU_CLOCK_REFERENCE_CALLS = (
    "IsMenuBarClockEnabled",
    "EnableMenuBarClock",
    "DisableMenuBarClock",
    "GetMenuBarClockConfig",
    "SetMenuBarClockConfig",
    "RefreshMenuBarClock",
)
COLOR_PROFILE_APIS = (
    "CMOpenProfile",
    "CMNewProfile",
    "CMCloneProfileRef",
    "CMGetProfileRefCount",
    "CMProfileModified",
    "CMGetProfileHeader",
    "CMCountProfileElements",
    "CMProfileElementExists",
    "CMGetIndProfileElementInfo",
    "CMGetProfileElement",
    "CMGetIndProfileElement",
    "CMGetPartialProfileElement",
    "CMCloseProfile",
    "CMGetProfileLocation",
    "NCMGetProfileLocation",
    "CMSetProfileHeader",
    "CMRemoveProfileElement",
    "CMSetProfileElementReference",
    "CMSetProfileElement",
    "CMSetPartialProfileElement",
    "CMSetProfileElementSize",
    "CMUpdateProfile",
    "CMCopyProfile",
    "CMFlattenProfile",
    "NCMUnflattenProfile",
    "CMGetProfileDescriptions",
    "CMGetScriptProfileDescription",
    "CMSetProfileDescriptions",
    "CMGetNamedColorInfo",
    "CMGetNamedColorValue",
    "CMGetIndNamedColorValue",
    "CMGetNamedColorIndex",
    "CMGetNamedColorName",
    "CMNewProfileSearch",
    "CMUpdateProfileSearch",
    "CMDisposeProfileSearch",
    "CMSearchGetIndProfileFileSpec",
    "CMSearchGetIndProfile",
    "CMValidateProfile",
    "CMGetPreferredCMMGlobal",
    "CMGetDefaultProfileBySpace",
    "CMGetDefaultProfileByUse",
    "CMGetProfileByAVID",
    "CMGetSystemProfile",
    "CMGetSystemProfilePriv",
    "CMSetDefaultProfileBySpace",
    "CMSetDefaultProfileByUse",
    "CMSetSystemProfile",
    "NCMSetSystemProfile",
    "CMInitWorkflows",
    "CMTermWorkflows",
    "CMSaveWorkflows",
    "CMRevertWorkflows",
    "CMCountWorkflows",
    "CMGetIndWorkflow",
    "CMGetWorkflowName",
    "CMGetWorkflowDesc",
    "CMSetWorkflowName",
    "CMSetWorkflowDesc",
    "CMGetWorkflowDates",
    "CMWorkflowIsActive",
    "CMWorkflowIsModified",
    "CMActivateWorkflow",
    "CMActivateCurrentWorkflow",
)


class ToolboxProbe(ValuesProbe):
    def __init__(self, rows, original_only, native_names=(), support_names=(), sections=(), companion_imports=()):
        self.original_only = original_only
        self.native_names = set(native_names)
        self.selected_sections = sections
        super().__init__(
            "CarbonLib",
            rows=rows,
            extra_names=(
                "FreeMem",
                "NewRoutineDescriptor",
                "DisposeRoutineDescriptor",
                "CallUniversalProc",
                *support_names,
                *sorted(self.native_names),
            ),
            original_library="CarbonLib",
            raw_start=0x2800,
            data_imports=companion_imports,
            far_oracle_tables=True,
        )

    def call(self, name, arguments=(), side=0):
        if self.original_only and name in self.names:
            side = 1
            self.used.add(name)
        super().call(name, arguments, side)
        if name in self.native_names:
            self.used.add(name)

    def section(self, name, action):
        if not self.selected_sections or name in self.selected_sections:
            super().section(name, action)

    def far_raw(self, data):
        # Reserve signed TOC offsets for fixtures that encode load/store instructions directly.
        offset = (len(self.p.data) + 3) & ~3
        self.p.data.extend(bytes(offset - len(self.p.data)) + data)
        return self.ptr(offset)


MENU_ITEM_VECTORS = {"GetMenuItemFontID", "SetMenuItemFontID", "GetMenuItemIconHandle", "SetMenuItemIconHandle"}
MENU_STRUCTURE_APIS = (
    "GetMenuItemHierarchicalID",
    "SetMenuItemHierarchicalID",
    "GetMenuItemHierarchicalMenu",
    "SetMenuItemHierarchicalMenu",
    "GetMenuTitleIcon",
    "SetMenuTitleIcon",
    "GetMenuDefinition",
    "SetMenuDefinition",
    "CreateCustomMenu",
    "AcquireRootMenu",
    "SetRootMenu",
    "AppendMenuItemText",
    "InsertMenuItemText",
    "DeleteMenuItems",
    "CopyMenuItems",
    "DuplicateMenuBar",
    "DisposeMenuBar",
    "RegisterMenuDefinition",
    "IsValidMenu",
    "_ScrollMenuImage",
    "AppendResMenu",
    "InsertFontResMenu",
    "InsertIntlResMenu",
    "CallMenuDef",
    "GetMenuType",
)
APPEARANCE_STATE_APIS = (
    "RegisterAppearanceClient",
    "UnregisterAppearanceClient",
    "IsAppearanceClient",
    "GetThemeCheckBoxStyle",
    "SetThemeCheckBoxStyle",
    "GetThemeScrollBarThumbStyle",
    "SetThemeScrollBarThumbStyle",
    "GetThemeScrollBarArrowStyle",
    "SetThemeScrollBarArrowStyle",
    "GetThemeSoundMask",
    "AreThemeSoundsEnabled",
    "GetThemeSoundPanningSpread",
    "SetThemeSoundPanningSpread",
    "SetThemeSoundMask",
    "SetThemeSoundsEnabled",
    "PlayThemeSound",
    "PlayThemeControlSound",
    "BeginThemeDragSound",
    "BeginThemeDragIndicatorSound",
    "EndThemeDragSound",
    "GetThemePlaylist",
    "SetThemePlaylist",
    "IsValidAppearanceFileType",
    "GetAppearance",
    "SetAppearance",
    "GetTheme",
    "SetTheme",
    "GetScenario",
    "SetScenario",
    "GetThemeTextColor",
    "SetThemeTextColorForWindow",
    "SetThemeBackgroundForWindow",
    "CanThemeDrawOnAllMonitors",
    "IsThemeCreditDialogAvailable",
    "SetHighlightColor",
    "CountThemeVariants",
    "GetIndThemeVariant",
    "GetThemeVariantByName",
    "GetThemeVariantList",
    "IterateThemeVariants",
    "SetBevelButtonMenuValue",
    "GetBevelButtonMenuHandle",
    "GetTabContentRect",
    "GetThemeMenuBarHeight",
    "IsThemeInColor",
    "SetDisclosureTriangleLastValue",
    "SetTabEnabled",
)

BUILTIN_CONTROL_CASES = (
    ("CreateChasingArrowsControl", ()),
    ("CreateSeparatorControl", ()),
    ("CreateGroupBoxControl", (0, 1)),
    ("CreatePopupArrowControl", (1, 1)),
    ("CreatePlacardControl", ()),
    ("CreateClockControl", (0, 0)),
    ("CreateUserPaneControl", (0,)),
    ("CreateWindowHeaderControl", (1,)),
    ("CreatePushButtonControl", (0,)),
    ("CreateRadioButtonControl", (0, 1, 1)),
    ("CreateCheckBoxControl", (0, 1, 1)),
    ("CreateRadioGroupControl", ()),
    ("CreateProgressBarControl", (65000, -100000, 100000, 0)),
    ("CreateLittleArrowsControl", (65000, -100000, 100000, 1)),
    ("CreateSliderControl", (65000, -100000, 100000, 1, 0, 0, 0)),
    ("CreateScrollBarControl", (65000, -100000, 100000, 9000, 1, -1)),
    ("CreateImageWellControl", (0,)),
    ("CreateDisclosureTriangleControl", (1, 0, 1, 0, 1)),
    ("CreateCheckGroupBoxControl", (0, 1, 1, 0)),
    ("CreateBevelButtonControl", (0, 0, 0, 0, 0, 0, 0)),
    ("CreatePopupGroupBoxControl", (0, 1, 0, 1, 0, 0, 0)),
    ("CreatePopupButtonControl", (0, 0, 1, 0, 0, 0)),
    ("CreatePictureControl", (0, 0)),
    ("CreateIconControl", (0, 0)),
    ("CreateEditTextControl", (0, 0, 0, 0)),
    ("CreateStaticTextControl", (0, 0)),
    ("CreateScrollingTextBoxControl", (0, 0, 0, 0, 0)),
)
TEXTENSION_SUPPORT = (
    "GetWindowPort",
    "GetWindowPortBounds",
    "SetWindowStandardState",
    "LocalToGlobal",
    "ShowWindow",
    "HideWindow",
    "SelectWindow",
    "UpgradeScriptInfoToTextEncoding",
)


def vector_cases(header="toolbox_vectors.h", count=399):
    source = (ROOT / "carbonlib/src" / header).read_text()
    rows = []
    for name, address, info, result in re.findall(
        r"^TOOLBOX_VECTOR\((\w+), (0x[0-9a-f]+), (0x[0-9a-f]+), (\w+)\)$", source, re.M
    ):
        rows.append(dict(name=name, vector=int(address, 16), info=int(info, 16), result=result))
    for name, address, info, selector, result in re.findall(
        r"^TOOLBOX_SELECTOR\((\w+), (0x[0-9a-f]+), (0x[0-9a-f]+), (0x[0-9a-f]+), (\w+)\)$", source, re.M
    ):
        rows.append(
            dict(name=name, vector=int(address, 16), info=int(info, 16), selector=int(selector, 16), result=result)
        )
    if len(rows) != count:
        raise ValueError(f"Incomplete vector table in {header}")
    return rows


def register_cases(header="toolbox_vectors.h", count=47):
    source = (ROOT / "carbonlib/src" / header).read_text()
    rows = []
    for name, address, info, trap, result in re.findall(
        r"^TOOLBOX_REGISTER\((\w+), (0x[0-9a-f]+), (0x[0-9a-f]+), (0x[0-9a-f]+), (\w+)\)$", source, re.M
    ):
        rows.append(dict(name=name, vector=int(address, 16), info=int(info, 16), trap=int(trap, 16), result=result))
    if len(rows) != count:
        raise ValueError("Incomplete Carbon register trap table")
    return rows


def register_vectors(probe, rows, sides=(1, 0)):
    p = probe.p
    # MOVEM.L d0-d3/a0-a3, absolute.l; MOVE.L/MOVEA.L #result, d0/a0; RTS.
    # Patch each 68K routine's capture address once before it can execute.
    for offset, opcode in ((0x1DB0, 0x203C), (0x1DD0, 0x207C)):
        struct.pack_into(">7H", p.data, offset, 0x48F9, 0x0F0F, 0, 0, opcode, 0xABCD, 0xEF87)
        struct.pack_into(">H", p.data, offset + 14, 0x4E75)
        p.emit(0x397E1E00, 0x917E0000 | (offset + 4))
    for row in rows:
        name, vector, info, trap = (row[key] for key in ("name", "vector", "info", "trap"))
        fields = []
        packed = info >> 11
        while packed:
            fields.append(((packed >> 2) & 7, packed & 3))
            packed >>= 5
        arguments = [0x89ABCDEF + index * 0x101 for index in range(len(fields) - bool(trap))]
        raw = ([trap] if trap else []) + arguments
        result_register = (info >> 6) & 31
        target = {0: 0x1DB0, 4: 0x1DD0}[result_register]
        expected = [0, 0xFFFFFF87, 0xFFFFEF87, 0xABCDEF87][(info >> 4) & 3]
        if row["result"] == "int8_t":
            expected = ((expected + 128) % 256) - 128
        elif row["result"] == "int16_t":
            expected = ((expected + 32768) % 65536) - 32768
        for side in sides:
            p.emit(0x81600000 | vector, 0x917E1820, 0x397E0000 | target, 0x91600000 | vector)
            probe.call(name, arguments, side)
            p.emit(0x817E1820, 0x91600000 | vector)
            p.expect(expected, name + " extracts the 68K register result")
            for (register, width), value in zip(fields, raw):
                p.emit(0x807E1E00 + 4 * register)
                mask = (0xFF, 0xFFFF, 0xFFFFFFFF)[width - 1]
                p.expect(value & mask, f"{name} loaded Mixed Mode register {register}")


def installed_memory_traps(probe):
    p = probe.p
    for side in (1, 0):
        probe.call("FreeMem", (), side)
        p.emit(0x907E1D00)
        probe.call("NewHandleClear", [32], side)
        p.nonzero("NewHandleClear allocated a real handle")
        p.emit(0x907E1D04, 0x80630000, 0x80630000)
        p.expect(0, "NewHandleClear clears the handle data")
        probe.call("HLock", [probe.slot(0x1D04)], 1 - side)
        probe.call("HGetState", [probe.slot(0x1D04)], side)
        p.emit(0x5463063E)
        p.expect(0x80, "opposite binding locks the real handle")
        probe.call("HUnlock", [probe.slot(0x1D04)], side)
        probe.call("DisposeHandle", [probe.slot(0x1D04)], 1 - side)
        probe.call("NewPtr", [19], side)
        p.nonzero("NewPtr allocated a real pointer")
        p.emit(0x907E1D04)
        probe.call("DisposePtr", [probe.slot(0x1D04)], 1 - side)
        probe.call("FreeMem", (), 1 - side)
        p.emit(0x809E1D00)
        p.equal("both allocation kinds return their memory to the guest heap")


def install_vector_targets(probe):
    p = probe.p
    # Both descriptors write a call marker and their incoming registers into
    # this application's TOC. Alternate targets to catch a cached vector.
    for offset, marker in ((0x1800, 1), (0x1808, 2)):
        routine = [0x38000000 | marker, 0x90021900]
        routine += [0x90000000 | (3 + index) << 21 | 2 << 16 | (0x1910 + 4 * index) for index in range(8)]
        routine += [0x81810038, 0x91821930, 0x8181003C, 0x91821934, 0x81810040, 0x91821938]
        routine += [0x3C60ABCD, 0x6063EF87, 0x4E800020]
        p.routines.append((offset, routine))


def patched_vectors(probe, rows, sides=(1, 0)):
    p = probe.p
    for row in rows:
        name, vector, info = row["name"], row["vector"], row["info"]
        widths = []
        parameters = info >> 6
        while parameters:
            widths.append(parameters & 3)
            parameters >>= 2
        count = len(widths) - int("selector" in row)
        arguments = [0x8765A1B0 + index * 0x101 for index in range(count)]
        callback_arguments = ([row["selector"]] if "selector" in row else []) + arguments
        # Save the installed vector in guest data and restore it immediately
        # after each call, before inspecting the captured registers.
        p.emit(0x81600000 | vector, 0x917E1820)
        for side in sides:
            for offset, marker in ((0x1800, 1), (0x1808, 2)):
                p.emit(0x38000000, 0x901E1900, 0x397E0000 | offset, 0x91600000 | vector)
                probe.call(name, arguments, side)
                p.emit(0x817E1820, 0x91600000 | vector)
                expected = [0, 0xFFFFFF87, 0xFFFFEF87, 0xABCDEF87][(info >> 4) & 3]
                if row["result"] == "uint8_t":
                    expected &= 255
                elif row["result"] == "uint16_t":
                    expected &= 65535
                p.expect(expected, name + " returned the Pascal result")
                p.emit(0x807E1900)
                p.expect(marker, name + " called the current vector")
                for index, (width, value) in enumerate(zip(widths, callback_arguments)):
                    bits = (8, 16, 32)[width - 1]
                    narrowed = value & ((1 << bits) - 1)
                    if narrowed & (1 << (bits - 1)):
                        narrowed -= 1 << bits
                    p.emit(0x807E0000 | (0x1910 + index * 4))
                    p.expect(narrowed, f"{name} argument {index + 1} reached the replacement descriptor")


def region_operations(probe):
    p = probe.p
    # Exercise a real 68K trap chain and shared handles across both entry paths.
    for side in (1, 0):
        probe.call("NewRgn", (), side)
        p.nonzero("NewRgn allocated a real region")
        p.emit(0x907E1830)
        probe.call("EmptyRgn", [probe.slot(0x1830)], 1 - side)
        p.expect(1, "new region is empty")
        probe.call("SetRectRgn", [probe.slot(0x1830), -4, -7, 16, 20], side)
        probe.call("EmptyRgn", [probe.slot(0x1830)], 1 - side)
        p.expect(0, "opposite binding sees a nonempty region")
        p.emit(0x807E1830, 0x80630000, 0x80630002)
        p.expect(0xFFF9FFFC, "region stores signed top and left coordinates")
        probe.call("DisposeRgn", [probe.slot(0x1830)], 1 - side)

    if "RgnToHandle" not in probe.native_names:
        return
    source, flat, restored = (probe.slot(offset) for offset in (0x1830, 0x1834, 0x1838))
    probe.call("NewRgn")
    p.nonzero("allocate a source region for flattening")
    p.emit(0x907E1830)
    probe.call("NewHandle", [2])
    p.nonzero("allocate a destination Handle without a complete region header")
    p.emit(0x907E1834)
    probe.call("NewRgn")
    p.nonzero("allocate a destination region for unflattening")
    p.emit(0x907E1838)
    probe.call("HandleToRgn", [flat, restored])
    probe.call("QDError")
    p.expect(-50, "unflattening a truncated header reports paramErr")
    probe.call("SetRectRgn", [source, -4, -7, 16, 20])
    probe.call("RgnToHandle", [source, flat])
    probe.call("QDError")
    p.expect(0, "flattening a region clears the QuickDraw error")
    probe.call("GetHandleSize", [flat])
    p.expect(10, "the flattened rectangle has the classic ten-byte header")
    probe.call("HandleToRgn", [flat, restored])
    probe.call("EqualRgn", [source, restored])
    p.expect(1, "flattening and restoring preserves the region")
    probe.call("RgnToHandle", [0, flat])
    p.emit(0xA8600D6E)
    p.expect(-50, "invalid flattening updates QuickDraw low memory")
    probe.call("HandleToRgn", [flat, restored])
    probe.call("DisposeHandle", [flat])
    probe.call("DisposeRgn", [restored])
    probe.call("DisposeRgn", [source])

    # Work in the color Window Manager port, using one pixel and restoring it
    # before changing back to the caller's port.
    probe.call("GetPort", [probe.ptr(0x1840)])
    p.emit(0x80600D2C, 0x907E1844)
    probe.call("SetPort", [probe.slot(0x1844)])
    probe.call("NewRgn")
    p.nonzero("allocate a screen capture region")
    p.emit(0x907E1830)
    for side in (0, 1):
        probe.call("QDSaveRegionBits", [0], side)
        p.expect(0, "a null capture region produces no snapshot")
        probe.call("QDRestoreRegionBits", [source, 0], side)
        p.expect(-50, "restoring requires a live snapshot")
        probe.call("QDDisposeRegionBits", [0], side)
        p.expect(-50, "disposing requires a live snapshot")
    probe.call("QDSaveRegionBits", [source])
    p.expect(0, "an empty clipped region produces no snapshot")
    probe.call("SetRectRgn", [source, 7, 80, 8, 81])
    rectangle = probe.raw(struct.pack(">4h", 80, 7, 81, 8))
    for side in (1, 0):
        probe.call("GetCPixel", [7, 80, probe.ptr(0x1850)])
        probe.call("QDSaveRegionBits", [source], side)
        p.nonzero("capture at least one visible screen word")
        p.emit(0x907E1834)
        probe.call("InvertRect", [rectangle])
        probe.call("GetCPixel", [7, 80, probe.ptr(0x1858)])
        p.emit(0x807E1850, 0x809E1858, 0x7C632278)
        p.nonzero("the saved pixel changes before restore")
        probe.call("QDRestoreRegionBits", [source, flat], 1 - side)
        p.expect(0, "both implementations can restore the other's snapshot")
        probe.call("GetCPixel", [7, 80, probe.ptr(0x1858)])
        for channel in (0, 2, 4):
            p.emit(0xA07E1850 + channel, 0xA09E1858 + channel)
            p.equal("region restoration restores the original pixel channel")
    probe.call("QDSaveRegionBits", [source])
    p.nonzero("capture a second snapshot for disposal")
    p.emit(0x907E1834)
    probe.call("QDDisposeRegionBits", [flat])
    p.expect(0, "dispose an unused snapshot")
    probe.call("DisposeRgn", [source])
    probe.call("SetPort", [probe.slot(0x1840)])


def graphics_and_resources(probe):
    p = probe.p
    for side in (1, 0):
        probe.call("GetDeviceList", (), side)
        p.nonzero("Color QuickDraw has a device list")
        p.emit(0x907E1840)
        probe.call("GetDeviceList", (), 1 - side)
        p.emit(0x809E1840)
        p.equal("both bindings return the installed device list")

        probe.call("GetPort", [probe.ptr(0x1850)], side)
        probe.call("GetPort", [probe.ptr(0x1854)], 1 - side)
        p.emit(0x807E1850, 0x809E1854)
        p.equal("GetPort writes the same guest port pointer")
        probe.call("GetForeColor", [probe.ptr(0x1860)], side)
        probe.call("GetForeColor", [probe.ptr(0x1870)], 1 - side)
        for offset in (0, 2, 4):
            p.emit(0xA07E1860 + offset, 0xA09E1870 + offset)
            p.equal("GetForeColor writes matching RGB components")

        probe.call("GetResource", [0x53495A45, 0], side)
        p.nonzero("GetResource finds the fixture SIZE resource")
        p.emit(0x907E1880)
        probe.call("GetResourceSizeOnDisk", [probe.slot(0x1880)], 1 - side)
        p.expect(10, "Resource Manager reads the ten-byte fixture resource")
        probe.call("GetResInfo", [probe.slot(0x1880), probe.ptr(0x1884), probe.ptr(0x1888), probe.ptr(0x1A00)], side)
        p.emit(0xA07E1884)
        p.expect(0, "GetResInfo writes the resource ID")
        p.emit(0x807E1888)
        p.expect(0x53495A45, "GetResInfo writes the resource type")


def selector_operations(probe):
    p = probe.p
    for side in (1, 0):
        probe.call("GetCurrentProcess", [probe.ptr(0x1B00)], side)
        p.expect(0, "stack dispatch writes current process serial number")
        probe.call("SameProcess", [probe.ptr(0x1B00), probe.ptr(0x1B00), probe.ptr(0x1B08)], 1 - side)
        p.expect(0, "SameProcess accepts serial numbers from the opposite binding")
        p.emit(0x887E1B08)
        p.expect(1, "SameProcess recognizes the current process")

        probe.call("TempNewHandle", [17, probe.ptr(0x1B10)], side)
        p.nonzero("TempNewHandle allocates a guest handle")
        p.emit(0x907E1B14, 0xA07E1B10)
        p.expect(0, "TempNewHandle reports noErr")
        probe.call("TempDisposeHandle", [probe.slot(0x1B14), probe.ptr(0x1B10)], 1 - side)
        p.emit(0xA07E1B10)
        p.expect(0, "TempDisposeHandle releases the other binding's allocation")
        for name in ("OffscreenVersion", "GetOutlinePreferred"):
            probe.call(name, (), side)
            p.emit(0x907E1B20)
            probe.call(name, (), 1 - side)
            p.emit(0x809E1B20)
            p.equal(name + " returns the same installed dispatcher result")

        probe.call("SetAntiAliasedTextEnabled", [1, 18], side)
        p.expect(0, "set the installed font antialias preference")
        probe.call("IsAntiAliasedTextEnabled", [probe.ptr(0x1B22)], 1 - side)
        p.expect(1, "the opposite binding reads the enabled font antialias preference")
        p.emit(0xA07E1B22)
        p.expect(18, "font antialias query writes the minimum size in guest order")
        probe.call("SetAntiAliasedTextEnabled", [0, 0], side)
        p.expect(0, "restore the original font antialias preference")
        probe.call("IsAntiAliasedTextEnabled", [0], 1 - side)
        p.expect(0, "font antialias query accepts a null minimum-size output")


def selector_descriptors(probe, rows):
    # Install multi-record descriptors for D0 and stack dispatch. Their exact
    # match follows a default record, and kDontPassSelector makes the first
    # application argument arrive in r3 of the selected callback.
    p = probe.p
    table = {row["name"]: row for row in rows}
    for index, name in enumerate(("LockPixels", "GetCurrentProcess")):
        row = table[name]
        base = 0x1C00 + index * 64
        info = row["info"]
        record_info = (info & 0x30) | ((info >> 8) << 6)
        struct.pack_into(">HBBIBBH", p.data, base, 0xAAFE, 7, 0, 0, 0, 0, 1)
        for record, selector, flags, target in ((0, 0x77777777, 0x1D, 0x1800), (1, row["selector"], 0x0D, 0x1808)):
            struct.pack_into(
                ">IBBHIII",
                p.data,
                base + 12 + record * 20,
                record_info,
                0,
                1,
                flags,
                (target - base) & 0xFFFFFFFF,
                0,
                selector,
            )
        vector = row["vector"]
        for side in (1, 0):
            p.emit(
                0x38000000,
                0x901E1900,
                0x901E1910,
                0x81600000 | vector,
                0x917E1820,
                0x397E0000 | base,
                0x91600000 | vector,
            )
            probe.call(name, [0x12345678], side)
            p.emit(0x817E1820, 0x91600000 | vector, 0x807E1900)
            p.expect(2, name + " exact routine record precedes the default selection")
            p.emit(0x807E1910)
            p.expect(0x12345678, name + " strips the dispatch selector from callback arguments")

    # MacDialogsLib dispatches by byte even when a caller supplies a word.
    # WindowsLib's LayerDispatch uses a word for the signed GetParent selector.
    for width, selector_info, record_selector, passed in (("byte", 0x48, -2, 0x02FE), ("word", 0x88, -7, 0xFFF9)):
        _, base = probe.raw(bytes(52))
        struct.pack_into(">HBBIBBH", p.data, base, 0xAAFE, 7, 0, 0, 0, selector_info, 1)
        for index, (selector, flags, target) in enumerate(((0x42, 0x1D, 0x1800), (record_selector, 0x0D, 0x1808))):
            struct.pack_into(
                ">IBBHIII",
                p.data,
                base + 12 + index * 20,
                0xF0,
                0,
                1,
                flags,
                (target - base) & 0xFFFFFFFF,
                0,
                selector & 0xFFFFFFFF,
            )
        for selector, expected in ((passed, 2), (0x1234, 1)):
            p.emit(0x38000000, 0x901E1900, 0x901E1910)
            probe.call("CallUniversalProc", [probe.ptr(base), 0x3B8, selector, 0x12345678])
            p.emit(0x807E1900)
            p.expect(expected, f"D0 {width} dispatcher selects a sign-extended record or its default")
            p.emit(0x807E1910)
            p.expect(0x12345678, f"D0 {width} dispatcher omits the supplied word selector")


def upp_cases():
    source = (ROOT / "carbonlib/src/upp_entries.h").read_text()
    constructors = {
        name: int(info, 16) for name, info in re.findall(r"^CARBON_NEW_UPP\((\w+), (0x[0-9a-f]+)\)$", source, re.M)
    }
    registry = (ROOT / "carbonlib/src/exports.c").read_text()
    disposers = set(re.findall(r'\{"(Dispose\w+UPP)",\s*ROMlib_carbon_dispose_descriptor,', registry))
    if (len(constructors), len(disposers)) != (203, 204):
        raise ValueError("Incomplete Carbon UPP constructor or disposer table")
    invocations = {
        name: (int(info, 16), int(count), result)
        for name, info, count, result in re.findall(
            r"^CARBON_INVOKE_UPP\((\w+), (0x[0-9a-f]+), (\d+), (\w+)\)$", source, re.M
        )
    }
    if len(invocations) != 181:
        raise ValueError("Incomplete Carbon Pascal UPP table")
    return constructors, disposers, invocations


def routine_descriptors(probe, constructors, disposers):
    p = probe.p
    for constructor, info in constructors.items():
        disposer = "Dispose" + constructor[3:]
        for side in (1, 0):
            probe.call("FreeMem")
            p.emit(0x907E1D00)
            probe.call(constructor, [probe.ptr(0x1800)], side)
            p.nonzero(constructor + " allocated a descriptor")
            p.emit(0x907E1D04)
            for offset, word in ((0, 0xAAFE0700), (4, 0), (8, 0), (12, info), (16, 0x00010000), (24, 0), (28, 0)):
                p.emit(0x807E1D04, 0x80630000 | offset)
                p.expect(word, f"{constructor} descriptor word {offset // 4}")
            p.emit(0x807E1D04, 0x80630014)
            p.pointer(0x1800, constructor + " preserved the guest callback vector")
            if constructor == "NewAEEventHandlerUPP":
                p.emit(0x38000000, 0x901E1900)
                probe.call("CallUniversalProc", [probe.slot(0x1D04), info, 1, 2, 3])
                p.expect(0xFFFFEF87, "Mixed Mode executes the descriptor's callback and returns its OSErr")
                p.emit(0x807E1900)
                p.expect(1, "Mixed Mode selected the callback")
            probe.call(
                disposer if disposer in disposers else "DisposeRoutineDescriptor", [probe.slot(0x1D04)], 1 - side
            )
            probe.call("FreeMem")
            p.emit(0x809E1D00)
            p.equal(constructor + " descriptor memory was released")
            probe.call(constructor, [0], side)
            p.expect(0, constructor + " preserves the null procedure result")

    unmatched = disposers - {"Dispose" + name[3:] for name in constructors}
    for disposer in sorted(unmatched):
        for side in (1, 0):
            probe.call("FreeMem")
            p.emit(0x907E1D00)
            probe.call("NewRoutineDescriptor", [probe.ptr(0x1800), 0, 1])
            p.nonzero("Mixed Mode independently allocated a descriptor")
            p.emit(0x907E1D04)
            probe.call(disposer, [probe.slot(0x1D04)], side)
            probe.call("FreeMem")
            p.emit(0x809E1D00)
            p.equal(disposer + " released the independently allocated descriptor")


def upp_invocations(probe, invocations):
    p = probe.p
    for name, (info, count, result) in invocations.items():
        arguments = [0xFEDCB987 + 0x101 * index for index in range(count)]
        for side in (1, 0):
            p.emit(0x38000000, 0x901E1900)
            probe.call(name, [*arguments, probe.ptr(0x1808)], side)
            expected = [0, 0xFFFFFF87, 0xFFFFEF87, 0xABCDEF87][(info >> 4) & 3]
            if result == "uint8_t":
                expected &= 0xFF
            elif result == "uint16_t":
                expected &= 0xFFFF
            p.expect(expected, name + " converts the callback result")
            p.emit(0x807E1900)
            p.expect(2, name + " invoked the supplied descriptor")
            for index, value in enumerate(arguments):
                width = (info >> (6 + 2 * index)) & 3
                bits = (8, 16, 32)[width - 1]
                signed = ((value + (1 << (bits - 1))) % (1 << bits)) - (1 << (bits - 1))
                p.emit(0x807E1910 + 4 * index)
                p.expect(signed, f"{name} preserves Pascal argument {index + 1}")

    # A 68K Pascal callback writes its OSErr above the three long arguments.
    struct.pack_into(">5H", p.data, 0x1D20, 0x3F7C, 0xEF87, 16, 0x4E74, 12)
    for side in (1, 0):
        probe.call("InvokeAEEventHandlerUPP", [1, 2, 3, probe.ptr(0x1D20)], side)
        p.expect(0xFFFFEF87, "InvokeAEEventHandlerUPP invokes a raw 68K Pascal callback")


def mixed_upp_cases():
    source = (ROOT / "carbonlib/src/upp_entries.h").read_text()
    entries = {}
    for name, info, count, result, mask in re.findall(
        r"^CARBON_MIXED_UPP\((\w+), (0x[0-9a-f]+), (\d+), (\w+), (0x[0-9a-f]+)\)$", source, re.M
    ):
        entries[name] = int(info, 16), int(count), result, int(mask, 16)
    if len(entries) != 11:
        raise ValueError("Incomplete Carbon Mixed Mode UPP table")
    return entries


def mixed_upp_invocations(probe, entries):
    p = probe.p
    for name, (info, count, result, mask) in entries.items():
        arguments = [0xFEDCB987 + 0x101 * i for i in range(count)]
        for side in (1, 0):
            p.emit(0x38000000, 0x901E1900)
            probe.call(name, [*arguments, probe.ptr(0x1808)], side)
            expected = [0, 0xFFFFFF87, 0xFFFFEF87, 0xABCDEF87][(info >> 4) & 3]
            p.expect(expected & (0xFF if result == "uint8_t" else 0xFFFFFFFF), name + " returns the callback result")
            p.emit(0x807E1900)
            p.expect(2, name + " invokes the supplied PowerPC routine")
            for index, argument in enumerate(arguments):
                if info & 15 == 2:
                    width = (info >> (11 + 5 * index)) & 3
                    bits = (8, 16, 32)[width - 1]
                    argument = ((argument + (1 << (bits - 1))) % (1 << bits)) - (1 << (bits - 1))
                elif index == 0:
                    argument &= mask
                p.emit(0x807E1910 + 4 * index)
                p.expect(argument, f"{name} forwards Mixed Mode argument {index + 1}")

    struct.pack_into(">2H", p.data, 0x1D30, 0x200B, 0x4E75)  # MOVE.L A3,D0; RTS
    struct.pack_into(">3H", p.data, 0x1D34, 0x202F, 8, 0x4E75)  # MOVE.L 8(SP),D0; RTS
    for side in (1, 0):
        probe.call("InvokeTEClickLoopUPP", [0xFEDCB987, probe.ptr(0x1D30)], side)
        p.expect(0x87, "register UPP loads guest A3 and narrows the 68K D0 result")
        probe.call("InvokeQDStdGlyphsUPP", [0x10203040, 0xFEDCB987, probe.ptr(0x1D34)], side)
        p.expect(0xFEDCB987, "C UPP pushes arguments in 68K C order and returns D0")


def textedit_upp_invocations(probe, entries):
    p = probe.p
    base = 0x2000
    area = probe.ptr(base)

    def callback(*words):
        return probe.far_raw(struct.pack(f">{len(words)}H", *words))

    def read(offset, width=4):
        p.emit({1: 0x887E0000, 2: 0xA07E0000, 4: 0x807E0000}[width] | (base + offset))

    highlight = callback(0x202F, 4, 0x2680, 0x4E74, 4)  # Move the stacked Rect pointer through D0 into (A3).
    eol = callback(0x274C, 4, 0x4A00, 0x4E75)  # Save A4 at 4(A3), then TST.B D0.
    width = callback(0x2740, 8, 0x2748, 12, 0x274C, 16, 0x4E75)  # Save D0, A0, A4 and leave D1 as result.
    nwidth = callback(0x2742, 20, 0x274A, 24, 0x4E75)  # Save D2 and A2, leave D1 as result.
    hit_test = callback(0x203C, 1, 0xBEEF, 0x323C, 0xCAFE, 0x343C, 0x00A5, 0x4E75)
    find_word = callback(0x3002, 0x7234, 0x4E75)  # Return caller (D2.W) and $34 (D1.W).
    recalc = callback(0x3407, 0x7634, 0x7856, 0x4E75)  # Return D7.W in D2.W, plus D3/D4.
    do_text = callback(0x204B, 0x3007, 0x4E75)  # Return A3 in A0, D7.W in D0.W.
    results = {0x1F: 0x87, 0x2F: 0xEF87, 0x3F: 0xEF87, 0x5F: 0x87}

    for name, (info, count) in entries.items():
        args = [0xFEDCA087 + index for index in range(count)]
        for side in (1, 0):
            probe.call(name, [*args, probe.ptr(0x1808)], side)
            if info in results:
                p.expect(results[info], name + " narrows the PowerPC callback result")
            for index, arg in enumerate(args):
                if info == 0x1F and index == 0:
                    arg &= 0xFF
                if (info in (0x2F, 0x3F, 0x4F, 0x5F, 0x6F) and index < 2) or (
                    info == 0x3F
                    and index in (2, 3)
                    or info == 0x5F
                    and index == 2
                    or info == 0x9F
                    and index == 1
                    or info == 0xAF
                    and 1 <= index <= 3
                ):
                    arg &= 0xFFFF
                    if (info == 0x3F and index == 0) or (info == 0x6F and index == 1):
                        arg -= 0x10000
                p.emit(0x807E1910 + index * 4)
                p.expect(arg, f"{name} forwards PowerPC callback argument {index + 1}")

    for side in (1, 0):
        for name in ("InvokeHighHookUPP", "InvokeCaretHookUPP"):
            probe.call(name, [probe.ptr(base + 32), area, highlight], side)
            read(0)
            p.pointer(base + 32, name + " supplies the stacked rectangle and A3")
        for char, expected in ((0x100, 1), (0x141, 0)):
            probe.call("InvokeEOLHookUPP", [char, area, 0x4321, eol], side)
            p.expect(expected, "EOL hook returns the 68K zero flag")
            read(4)
            p.expect(0x4321, "EOL hook receives the TEHandle in A4")
        for name in ("InvokeWidthHookUPP", "InvokeTextWidthHookUPP", "InvokeDrawHookUPP"):
            probe.call(name, [0xAAFF, 0xCAFE, 0x11223344, area, 0x8877, width], side)
            if name != "InvokeDrawHookUPP":
                p.expect(0xCAFE, name + " returns the unsigned word in D1")
            read(10, 2)
            p.expect(0xAAFF, name + " passes the first word in D0")
            read(12)
            p.expect(0x11223344, name + " passes the buffer in A0")
            read(16)
            p.expect(0x8877, name + " passes the TEHandle in A4")
        probe.call("InvokeNWidthHookUPP", [7, 0xFACE, -17, -2, 5, 6, area, 8, nwidth], side)
        p.expect(0xFACE, "NWidth returns D1 after reading stack arguments")
        read(20)
        p.expect(0xFFFEFFEF, "NWidth packs direction and slop into D2")
        read(24)
        p.expect(6, "NWidth passes lineStart in A2")
        probe.call(
            "InvokeHitTestHookUPP",
            [1, 2, 3, 4, area, 6, area, probe.ptr(base + 2), probe.ptr(base + 4), hit_test],
            side,
        )
        p.expect(1, "hit-test hook returns the high word of D0 as a Boolean")
        read(0)
        p.expect(0xBEEFCAFE, "hit-test hook writes pixel width and character offset")
        read(4)
        p.expect(0xA5004321, "hit-test hook writes just one byte for pixelInChar")
        probe.call("InvokeTEFindWordUPP", [1, -2, area, 4, area, probe.ptr(base + 2), find_word], side)
        read(0)
        p.expect(0xFFFE0034, "find-word hook writes both result registers")
        probe.call("InvokeTERecalcUPP", [area, 0xBEEF, area, probe.ptr(base + 2), probe.ptr(base + 4), recalc], side)
        read(0)
        p.expect(0xBEEF0034, "recalc hook passes D7 and writes the first two outputs")
        read(4, 2)
        p.expect(0x56, "recalc hook writes the final output")
        probe.call("InvokeTEDoTextUPP", [area, 2, 3, -4, area, probe.ptr(base + 4), do_text], side)
        read(0)
        p.pointer(base, "do-text hook returns its GrafPort in A0")
        read(4, 2)
        p.expect(0xFFFC, "do-text hook returns its character position in D0")
        probe.call("NewRoutineDescriptor", [width, 0x2F, 0])
        p.nonzero("allocate an explicit 68K Mixed Mode descriptor")
        p.emit(0x907E0000 | (base + 28))
        probe.call("InvokeWidthHookUPP", [7, 0x9876, 5, area, 3, probe.slot(base + 28)], side)
        p.expect(0x9876, "invoke a width hook through its 68K routine descriptor")
        probe.call("DisposeRoutineDescriptor", [probe.slot(base + 28)])


def direct_upp_cases():
    source = (ROOT / "carbonlib/src/upp_entries.h").read_text()
    entries = {}
    for name, index, argument, mask in re.findall(
        r"^CARBON_DIRECT_UPP\((Invoke\w+UPP), (\d+), (\d+), (0x[0-9a-f]+)\)$", source, re.M
    ):
        entries[name] = int(index), int(argument), int(mask, 16)
    if len(entries) != 24:
        raise ValueError("Incomplete borrowed UPP table")
    return entries


def direct_upps(probe, entries):
    p = probe.p
    # Capture r12 as well as f1 and every ordinary argument needed by these callbacks.
    code = [0x91821904, 0xD8221DA0]
    code += [0x90000000 | (3 + index) << 21 | 2 << 16 | (0x1910 + 4 * index) for index in range(5)]
    code += [0x3C60ABCD, 0x6063EF87, 0x4E800020]
    p.routines.append((0x1B28, code))
    struct.pack_into(">II", p.data, 0x1D98, 0xC0454000, 0)
    for name, (count, narrowed_argument, mask) in entries.items():
        suffix = name[6:]
        values = [0xFEDCB987 + 0x101 * index for index in range(count)]
        for side in (1, 0):
            probe.call("FreeMem", (), side)
            p.emit(0x907E1D00)
            probe.call("New" + suffix, [probe.ptr(0x1B28)], side)
            p.pointer(0x1B28, "New" + suffix + " borrows the caller's transition vector")
            p.emit(0x907E1D04)
            probe.call("Dispose" + suffix, [probe.slot(0x1D04)], 1 - side)
            probe.call("FreeMem", (), 1 - side)
            p.emit(0x809E1D00)
            p.equal("borrowed UPP lifetime leaves the heap unchanged")
            p.emit(0x38000000, 0x901E1904, 0x901E1DA0, 0xC83E1D98)
            probe.call(name, [*values, probe.slot(0x1D04)], side)
            p.expect(0xABCDEF87, name + " returns the borrowed callback result")
            p.emit(0x807E1904)
            p.pointer(0x1B28, name + " supplied the transition vector in r12")
            p.emit(0x807E1DA0)
            p.expect(0xC0454000, name + " preserved floating argument f1")
            for index, value in enumerate(values):
                p.emit(0x807E1910 + 4 * index)
                p.expect(
                    value & mask if index == narrowed_argument else value,
                    f"{name} preserved callback argument {index + 1}",
                )

    for side in (1, 0):
        probe.call("NewComponentFunctionUPP", [probe.ptr(0x1800), 0xFE0], side)
        p.nonzero("NewComponentFunctionUPP allocates a descriptor")
        p.emit(0x907E1D04, 0x8063000C)
        p.expect(0xFE0, "NewComponentFunctionUPP uses the caller's ProcInfo")
        probe.call("DisposeComponentFunctionUPP", [probe.slot(0x1D04)], 1 - side)


def toolbox_sections(probe, rows, registers, constructors, disposers, invocations, borrowed, mixed):
    install_vector_targets(probe)
    probe.section(
        "Patched Toolbox vectors",
        lambda: patched_vectors(probe, [row for row in rows if row["name"] not in MENU_ITEM_VECTORS]),
    )
    if not probe.original_only:
        # This fixture does not include the saved AppearanceLib menu services.
        probe.section(
            "Patched Menu Manager item vectors",
            lambda: patched_vectors(probe, [row for row in rows if row["name"] in MENU_ITEM_VECTORS], sides=(0,)),
        )
    probe.section("Installed 68K region traps", lambda: region_operations(probe))
    probe.section("Installed graphics and resource traps", lambda: graphics_and_resources(probe))
    probe.section("Selector-specific routine descriptors", lambda: selector_descriptors(probe, rows))
    probe.section("Installed selector dispatch", lambda: selector_operations(probe))
    probe.section("UPP allocation and disposal", lambda: routine_descriptors(probe, constructors, disposers))
    probe.section("Pascal UPP invocation", lambda: upp_invocations(probe, invocations))
    probe.section("C and register-based UPP invocation", lambda: mixed_upp_invocations(probe, mixed))
    probe.section("Register-based trap vectors", lambda: register_vectors(probe, registers))
    probe.section("Installed Memory Manager traps", lambda: installed_memory_traps(probe))
    probe.section("Borrowed PowerPC UPPs", lambda: direct_upps(probe, borrowed))


def write_toolbox_references(destination, include_atsui=False, include_object_packing=False):
    references = ROOT / "references/disasm/system9"
    manifest = json.loads((references / "System/manifest.json").read_text())
    data = (references / "input/System").read_bytes()
    if hashlib.sha256(data).hexdigest() != manifest["data_sha256"]:
        raise ValueError("System data fork differs from the local disassembly reference")
    fragments = {name: row for row in manifest["fragments"] if row["container"] == "data" for name in row["names"]}

    def write_data_fragment(name, exported_name=None, resources=()):
        fragment = fragments[name]
        start = fragment["offset"]
        layout = json.loads((references / fragment["directory"] / "pef.json").read_text())
        size = max(row["offset"] + row["packed_size"] for row in layout["sections"])
        write_fragment(destination, exported_name or name, data[start : start + size], 0, extra_resources=resources)

    table = next(row for row in manifest["resources"] if row["type"] == "nlib" and row["name"] == "MathLibGlobals")
    write_fragment(destination, "MathLibGlobals", (references / "System" / table["file"]).read_bytes(), 0)
    table = next(row for row in manifest["resources"] if row["type"] == "nlib" and row["name"] == "Math64Lib")
    write_fragment(destination, "Math64Oracle", (references / "System" / table["file"]).read_bytes(), 0)
    write_data_fragment("MathLib", "MathOracle")
    if include_object_packing:
        resources = [
            (b"osl ", row["id"], (references / "System" / row["file"]).read_bytes())
            for row in manifest["resources"]
            if row["type"] == "osl "
        ]
        write_data_fragment("OSLData")
        write_data_fragment("ObjectSupportLib", "ObjectPackingOracle", resources)
    if include_atsui:
        required = (
            "FMSharedCaches",
            "FontManager",
            "AttributeGroup",
            "FontObjects",
            "FontObjectsGlobals",
            "OFAServices",
            "TTScaler3",
            "ATSStreaming",
        )
        for name in required:
            write_data_fragment(name)


def math_compare(probe, name, values, expected):
    p = probe.p
    tolerance = 24 * (math.ulp(expected) if expected else math.ulp(1.0))
    _, operands = probe.raw(struct.pack(">4d", values[0], values[1] if len(values) == 2 else 0, expected, tolerance))
    for side in (1, 0):
        p.emit(0xC81E1850, 0xFDFE058E)  # Clear exceptions and select nearest-even.
        for index in range(len(values)):
            p.emit(0xC83E0000 + (index << 21) + operands + 8 * index)
        probe.call(name, (), side)
        p.emit(0xD83E1860 + 8 * side)
    for offset, label in ((operands + 16, "known result"), (0x1860, "native result")):
        p.checks.append({"offset": len(p.code) * 4, "check": name + " agrees with the " + label})
        p.emit(
            0xC83E1868,
            0xC85E0000 | offset,
            0xFC011028,
            0xFC000210,
            0xC85E0000 | (operands + 24),
            0xFC001000,
            0x41800008,
            0,
        )


def math_scalars(probe, entries):
    domains = {
        "acosh": 1.75,
        "gamma": 1.25,
        "lgamma": 4.5,
        "sqrt": 1.75,
        "log": 1.75,
        "log10": 1.75,
        "log2": 1.75,
    }
    unary = {"gamma": math.gamma, "rint": round, "nearbyint": round, "round": lambda x: math.trunc(x + 0.5)}
    binary = {"nextafterd": math.nextafter}
    for name, shape, _ in entries:
        if shape == "UNARY":
            function = unary[name] if name in unary else getattr(math, name, None)
            if function is None and name == "logb":
                function = lambda value: math.frexp(value)[1] - 1
            value = domains.get(name, 0.625)
            math_compare(probe, name, [value], function(value))
            math_compare(probe, name, [value * 1.125], function(value * 1.125))
        elif shape == "FINANCIAL":
            for rate, periods in ((0.125, 4), (0.0, 12)):
                expected = (
                    (1 + rate) ** periods
                    if name == "compound"
                    else (-math.expm1(-periods * math.log1p(rate)) / rate if rate else periods)
                )
                math_compare(probe, name, (rate, periods), expected)
        elif shape == "BINARY":
            function = binary[name] if name in binary else getattr(math, name, None)
            if function is None:
                function = {"fmax": max, "fmin": min, "fdim": lambda x, y: max(x - y, 0)}[name]
            for values in ((1.625, 0.75), (0.375, 1.25)):
                math_compare(probe, name, values, function(*values))

    p = probe.p
    for side in (1, 0):
        for name, operands, result in (
            ("__inf", (), (0x7FF0000000000000,)),
            ("fabs", (0x8000000000000000,), (0,)),
            ("copysign", (0x7FF8000000001234, 0x8000000000000000), (0xFFF8000000001234,)),
            ("fabsl", (0xBFF0000000000000, 0xBC90000000000000), (0x3FF0000000000000, 0x3C90000000000000)),
            (
                "copysignl",
                (0x3FF0000000000000, 0x3C90000000000000, 0x8000000000000000, 0),
                (0xBFF0000000000000, 0xBC90000000000000),
            ),
        ):
            # This input area is reused after each check by storing immediate values at runtime.
            for index, data in enumerate(operands):
                probe.arguments([data >> 32, data & 0xFFFFFFFF], side)
                p.emit(0x907E1880 + 8 * index, 0x909E1884 + 8 * index, 0xC83E1880 + (index << 21) + 8 * index)
            probe.call(name, (), side)
            for index, data in enumerate(result):
                p.emit(0xD83E18C0 + (index << 21), 0x807E18C0)
                p.expect(data >> 32, name + " preserves sign and exponent bits")
                p.emit(0x807E18C4)
                p.expect(data & 0xFFFFFFFF, name + " preserves the low word")

        for left, right, result in ((2, 1, 0), (-1, 1, 1), (0, 0, 2), (float("nan"), 1, 3)):
            for index, value in enumerate((left, right)):
                bits = struct.unpack(">Q", struct.pack(">d", value))[0]
                probe.arguments([bits >> 32, bits & 0xFFFFFFFF], side)
                p.emit(0x907E1880, 0x909E1884, 0xC83E1880 + (index << 21))
            probe.call("relation", (), side)
            p.expect(result, "relation returns the Mac ordered or unordered result")


def math_classification(probe, entries):
    p = probe.p
    samples = (
        (0, 3, 0),
        (0x8000000000000000, 3, 1),
        (0x3FF0000000000000, 4, 0),
        (0x7FF0000000000000, 2, 0),
        (0xFFF8000000000000, 1, 1),
        (0x7FF4000000000000, 0, 0),
    )
    for name, kind, operands in entries:
        if kind != "CLASS":
            continue
        op, single = operands
        subnormal = 0x36A0000000000000 if single == "1" else 1
        for bits, kind, sign in (*samples, (subnormal, 5, 0)):
            expected = {"class": kind, "finite": kind >= 3, "nan": kind < 2, "normal": kind == 4, "sign": sign}[op]
            for side in (1, 0):
                probe.arguments([bits >> 32, bits & 0xFFFFFFFF], side)
                p.emit(0x907E18C0, 0x909E18C4, 0xC83E18C0, 0xC85E1850)
                probe.call(name, (), side)
                p.expect(expected, name + " classifies the guest IEEE value")


def math_environment(probe, entries):
    p = probe.p
    for side in (1, 0):
        for mode in (0, 1, 2, 3, 4):
            p.emit(0xC81E1850, 0xFDFE058E)
            probe.call("fesetround", [mode], side)
            p.expect(int(mode < 4), "fesetround reports the Boolean success flag")
            probe.call("fegetround", (), side)
            p.expect(mode if mode < 4 else 0, "fegetround returns the mode accepted by fesetround")
        p.emit(0xC81E1850, 0xFDFE058E)
        probe.arguments([0x84000002], side)
        p.emit(0x907E1900)
        probe.call("fesetenv", [probe.ptr(0x1900)], side)
        probe.call("fetestexcept", [0x3E000000], side)
        p.expect(0x04000000, "fetestexcept reads the PPC exception flags")
        probe.call("fegetenv", [probe.ptr(0x1904)], side)
        p.emit(0x807E1904)
        p.expect(0x84000002, "fegetenv publishes the big-endian FPSCR")
        probe.call("feholdexcept", [probe.ptr(0x1908)], side)
        p.expect(1, "feholdexcept reports the Boolean success flag")
        p.emit(0x807E1908)
        p.expect(0x84000002, "feholdexcept saves the complete environment")
        probe.call("fegetenv", [probe.ptr(0x190C)], side)
        p.emit(0x807E190C)
        p.expect(2, "feholdexcept clears exceptions and retains rounding")
        p.emit(0xC81E1850, 0xFDFE058E)

    for name, kind, body in entries:
        if kind != "AUX" or name in ("modff", "nextafterf", "randomx"):
            continue
        number = 5.5 if name != "frexp" else 12.0
        for side in (1, 0):
            probe.arguments([int.from_bytes(struct.pack(">d", number)[:4], "big"), 0], side)
            p.emit(0x907E1880, 0x909E1884, 0xC83E1880)
            if name == "remquo":
                probe.arguments([0x40000000, 0], side)
                p.emit(0x907E1880, 0x909E1884, 0xC85E1880)
                args, value, output = [0, 0, 0, 0, probe.ptr(0x1920)], -0.5, (3,)
            elif name == "modf":
                args, value, output = [0, 0, probe.ptr(0x1920)], 0.5, (0x40140000, 0)
            elif name == "frexp":
                args, value, output = [0, 0, probe.ptr(0x1920)], 0.75, (4,)
            else:
                args, value, output = [0, 0, -2], 1.375, ()
            probe.call(name, args, side)
            p.emit(0xD83E18C0, 0x807E18C0)
            p.expect(int.from_bytes(struct.pack(">d", value)[:4], "big"), name + " returns the expected double")
            p.emit(0x807E18C4)
            p.expect(0, name + " returns the expected low result word")
            for index, word in enumerate(output):
                p.emit(0x807E1920 + 4 * index)
                p.expect(word, name + " writes its big-endian auxiliary result")


def math_decimals(probe):
    p = probe.p
    if not probe.original_only:
        p.imports.extend(("MathLib", name, 2) for name in ("num2dec", "dec2str", "str2dec", "dec2num"))

    def call(name, arguments, side):
        if side == 2:
            probe.arguments(arguments, 0)
            p.call(name, library="MathLib")
        else:
            probe.call(name, arguments, side)

    def record(digits, exponent=0, negative=False):
        return probe.far_raw((struct.pack(">BBhB", negative, 0, exponent, len(digits)) + digits).ljust(42, b"\0"))

    def check_record(base, negative, exponent, digits, label):
        for index, byte in ((0, negative), (4, len(digits)), *enumerate(digits, 5)):
            p.emit(0x887E0000 | (base + index))
            p.expect(byte, label)
        p.emit(0xA07E0000 | (base + 2))
        p.expect(exponent & 65535, label + " exponent")

    def capture_float(bits, label, register=1):
        p.emit(0xD81E18C0 | register << 21, 0x807E18C0)
        p.expect(bits >> 32, label + " high result word")
        p.emit(0x807E18C4)
        p.expect(bits, label + " low result word")

    # The input double occupies two parameter words ahead of the decimal pointer.
    form = probe.far_raw(struct.pack(">BBh", 1, 0, 2))
    text = probe.far_raw(b"x-12.5\0")
    decimal = probe.ptr(0x1A00)
    output = probe.ptr(0x1A40)
    for side in (1, 0) if probe.original_only else (1, 0, 2):
        p.emit(0xC81E1850, 0xFDFE058E)
        probe.arguments([0x40290000, 0], side)
        p.emit(0x907E1880, 0x909E1884, 0xC83E1880)
        call("num2dec", [form, 0, 0, decimal], side)
        check_record(0x1A00, 0, -2, b"1250", "num2dec fixed-point decimal")
        call("dec2str", [form, decimal, output], side)
        for i, byte in enumerate(b"12.50\0"):
            p.emit(0x887E0000 | (0x1A40 + i))
            p.expect(byte, "dec2str fixed-point rendering")
        probe.arguments([0x00010000], side)
        p.emit(0x907E1AA0)
        call("str2dec", [text, probe.ptr(0x1AA0), decimal, probe.ptr(0x1AA2)], side)
        p.emit(0x807E1AA0)
        p.expect(0x00060001, "str2dec updates the big-endian index and valid-prefix flag")
        check_record(0x1A00, 1, -1, b"125", "str2dec parses from the supplied index")
        call("dec2num", [decimal], side)
        capture_float(0xC029000000000000, "dec2num restores the signed binary64 value")

    if not probe.original_only:
        high_precision = record(b"9007199254740993")
        form_long = probe.far_raw(struct.pack(">BBh", 0, 0, 16))
        p.emit(0xC81E1850, 0xFDFE058E)
        probe.call("fesetround", [0])
        probe.call("dec2numl", [high_precision])
        capture_float(0x4340000000000000, "dec2numl returns the nearest high limb")
        capture_float(0x3FF0000000000000, "dec2numl keeps the integer unit in the low limb", register=2)
        probe.call("num2decl", [form_long, 0, 0, 0, 0, decimal])
        check_record(0x1A00, 0, 0, b"9007199254740993", "num2decl keeps the precision of both limbs")
        for direction, bits in ((0, 0x4170000000000000), (2, 0x4170000020000000)):
            probe.call("fesetround", [direction])
            probe.call("dec2f", [record(b"16777217")])
            capture_float(bits, "dec2f rounds the binary32 result using FPSCR")
        probe.call("fesetround", [0])

        probe.call("dec2numl", [high_precision])
        exponent_output = probe.ptr(0x1AA4)
        probe.call("frexpl", [0, 0, 0, 0, exponent_output])
        p.emit(0x807E1AA4)
        p.expect(54, "frexpl stores the power-of-two exponent in guest byte order")
        capture_float(0x3FE0000000000000, "frexpl normalizes the high limb")
        capture_float(0x3C90000000000000, "frexpl scales the low limb", register=2)
        probe.call("ldexpl", [0, 0, 0, 0, 54])
        capture_float(0x4340000000000000, "ldexpl restores the original high limb")
        capture_float(0x3FF0000000000000, "ldexpl restores the original low limb", register=2)

    cases = ((b"15", -1), (b"25", -1), (b"51", -2), (b"1", -80), (b"327675", -1), (b"21474836485", -1), (b"I", 0))
    for digits, exponent in cases:
        for negative in (False, True):
            operand = record(digits, exponent, negative)
            for rn, rounding in enumerate((ROUND_HALF_EVEN, ROUND_DOWN, ROUND_CEILING, ROUND_FLOOR)):
                for name, limit in (("dec2s", 1 << 15), ("dec2l", 1 << 31)):
                    if digits == b"I":
                        expected, status = -limit, 0x20000000
                    else:
                        value = Decimal(digits.decode()).scaleb(exponent)
                        if negative:
                            value = -value
                        expected = int(value.to_integral_value(rounding=rounding))
                        if not -limit <= expected < limit:
                            expected, status = -limit, 0x20000000
                        else:
                            status = 0 if value == expected else 0x02000000
                    for side in (1, 0):
                        p.emit(0xC81E1850, 0xFDFE058E)
                        probe.call("fesetround", [rn], side)
                        probe.call(name, [operand], side)
                        context = f"{name} {digits!r}e{exponent}, negative={negative}, rounding={rn}"
                        p.expect(expected, context + " returns the rounded integer")
                        probe.call("fetestexcept", [0x3E000000], side)
                        p.expect(status, context + " records the decimal conversion exception")

    for tag, payload in ((b"0", 0), (b"42", 42), (b"255", 255), (b"256", 255), (b"-7", 0), (b"1e2", 100)):
        operand = probe.far_raw(tag + b"\0")
        for name in ("nan", "nanf", "nanl"):
            for side in (1, 0):
                p.emit(0xC81E1850, 0xFDFE058E)
                probe.call(name, [operand], side)
                capture_float(0x7FF8000000000000 | payload << 37, name + " preserves the decimal tag payload")
                if name == "nanl":
                    capture_float(0, "nanl returns a zero low double", 2)
    p.emit(0xC81E1850, 0xFDFE058E)


def math_extended(probe):
    p = probe.p
    tail = math.ldexp(1.0, -60)
    tiny = math.ldexp(1.0, -200)
    large = math.ldexp(1.0, 200)

    def set_inputs(*values):
        _, at = probe.raw(struct.pack(">" + str(len(values)) + "d", *values))
        for index in range(len(values)):
            p.emit(0xC83E0000 + (index << 21) + at + 8 * index)

    def check_float(register, expected, label):
        words = struct.unpack(">II", struct.pack(">d", expected))
        p.emit(0xD81E18C0 + (register << 21), 0x807E18C0)
        p.expect(words[0], label + " high word")
        p.emit(0x807E18C4)
        p.expect(words[1], label + " low word")

    for side in (1, 0):
        p.emit(0xC81E1850, 0xFDFE058E)
        for name, operands, args, result in (
            ("floorl", (1.0, -tail), (), (0.0, 0.0)),
            ("floorl", (1.0, -tiny), (), (0.0, 0.0)),
            ("floorl", (large, 0.75), (), (large, 0.0)),
            ("ceill", (1.0, tail), (), (2.0, 0.0)),
            ("ceill", (large, 0.75), (), (large, 1.0)),
            ("truncl", (1.5, -tail), (), (1.0, 0.0)),
            ("roundl", (0.5, -tail), (), (0.0, 0.0)),
            ("rintl", (0.5, tail), (), (1.0, 0.0)),
            ("rintl", (0.5, tiny), (), (1.0, 0.0)),
            ("nearbyintl", (1.5, -tail), (), (1.0, 0.0)),
            ("sqrtl", (4.0, 0.0), (), (2.0, 0.0)),
            ("sqrtl", (1.0, tiny), (), (1.0, tiny / 2)),
            ("scalbl", (1.0, tail), (0, 0, 0, 0, 3), (8.0, 8 * tail)),
            ("scalbl", (1.0, tiny), (0, 0, 0, 0, 3), (8.0, 8 * tiny)),
            ("modfl", (1.5, tail), (0, 0, 0, 0, probe.ptr(0x1A00)), (0.5, tail)),
            ("modfl", (1.5, tiny), (0, 0, 0, 0, probe.ptr(0x1A00)), (0.5, tiny)),
            ("modfl", (large, 0.75), (0, 0, 0, 0, probe.ptr(0x1A00)), (0.75, 0.0)),
            ("nextafterf", (1.0, 2.0), (), (1.0 + 2**-23,)),
            ("modff", (-2.25,), (0, probe.ptr(0x1A20)), (-0.25,)),
        ):
            set_inputs(*operands)
            probe.call(name, args, side)
            for index, expected in enumerate(result):
                check_float(index + 1, expected, name + f" returns f{index + 1}")
            if name == "modfl":
                integral_words = struct.unpack(">4I", struct.pack(">2d", math.trunc(operands[0]), 0))
                for index, expected in enumerate(integral_words):
                    p.emit(0x807E1A00 + 4 * index)
                    p.expect(expected, "modfl stores the double-double integral part")
            if name == "modff":
                p.emit(0x807E1A20)
                p.expect(0xC0000000, "modff stores a big-endian binary32")
        for name, operands, expected in (
            ("relationl", (1.0, tail, 1.0, 0.0), 2),
            ("rinttol", (-2.5,), -2),
            ("roundtol", (-2.5,), -3),
            ("rinttoll", (2.5, tail), 3),
            ("roundtoll", (-2.5, tail), -2),
        ):
            set_inputs(*operands)
            probe.call(name, (), side)
            p.expect(expected, name + " returns the expected guest long")

        _, seed = probe.raw(struct.pack(">d", 1.0))
        probe.call("randomx", [probe.ptr(seed)], side)
        check_float(1, 16807.0, "randomx returns the first Park-Miller value")
        p.emit(0x807E0000 | seed)
        p.expect(0x40D069C0, "randomx updates the big-endian seed")
        p.emit(0x807E0000 | (seed + 4))
        p.expect(0, "randomx updates the seed's low word")

        _, packed = probe.raw(bytes.fromhex("3fff8000000000000001"))
        _, binary64 = probe.raw(struct.pack(">d", 1.5))
        _, double_double = probe.raw(struct.pack(">2d", 1.5, tail))
        probe.call("x80tod", [probe.ptr(packed)], side)
        check_float(1, 1.0, "x80tod rounds away the extra significand bit")
        probe.call("x80told", [probe.ptr(packed), probe.ptr(0x1A40)], side)
        expected = struct.unpack(">4I", struct.pack(">2d", 1.0, 2**-63))
        for index, value in enumerate(expected):
            p.emit(0x807E1A40 + 4 * index)
            p.expect(value, "x80told retains the extra significand bit")
        for name, source, last in (("dtox80", binary64, 0), ("ldtox80", double_double, 8)):
            probe.call(name, [probe.ptr(source), probe.ptr(0x1A60)], side)
            for offset, value in ((0, 0x3FFFC000), (4, 0)):
                p.emit(0x807E1A60 + offset)
                p.expect(value, name + " writes the packed exponent and significand")
            p.emit(0xA07E1A68)
            p.expect(last, name + " writes the low significand bits")

        p.emit(0xC81E1850, 0xFDFE058E)
        probe.call("feraiseexcept", [0x22000000], side)
        probe.call("fetestexcept", [0x3E000000], side)
        p.expect(0x22000000, "feraiseexcept raises invalid and inexact")
        probe.call("fegetexcept", [probe.ptr(0x1AA0), 0x20000000], side)
        p.emit(0x807E1AA0, 0x5463008C)
        p.expect(0x20000000, "fegetexcept saves only the selected exception")
        probe.call("feclearexcept", [0x20000000], side)
        probe.call("fetestexcept", [0x3E000000], side)
        p.expect(0x02000000, "feclearexcept leaves the unrelated exception set")
        probe.call("fesetexcept", [probe.ptr(0x1AA0), 0x20000000], side)
        probe.call("fetestexcept", [0x3E000000], side)
        p.expect(0x22000000, "fesetexcept restores the saved invalid cause")
        probe.arguments([1], side)
        p.emit(0x907E1AA4)
        probe.call("feupdateenv", [probe.ptr(0x1AA4)], side)
        probe.call("fegetround", (), side)
        p.expect(1, "feupdateenv installs the saved rounding mode")
        probe.call("fetestexcept", [0x3E000000], side)
        p.expect(0x22000000, "feupdateenv re-raises the active exceptions")
        p.emit(0xC81E1850, 0xFDFE058E)


def math_long(probe, entries):
    p = probe.p
    for name, kind, _ in entries:
        if kind not in ("QUAD_UNARY", "QUAD_BINARY", "NARROW_UNARY", "NARROW_BINARY", "NARROW_AUX"):
            continue
        if name == "remquol":
            _, operands = probe.raw(struct.pack(">4d", 5.5, 2**-60, 2.0, 0.0))
            for side in (1, 0):
                p.emit(0xC81E1850, 0xFDFE058E)
                for index in range(4):
                    p.emit(0xC83E0000 + (index << 21) + operands + 8 * index)
                probe.call(name, [0] * 8 + [probe.ptr(0x1B40)], side)
                p.emit(0xD83E1B00, 0x807E1B00)
                p.expect(0xBFE00000, "remquol returns the binary64 remainder in the high limb")
                p.emit(0xD85E1B08, 0x807E1B08, 0x809E1B0C, 0x7C632378)
                p.expect(0, "remquol returns a zero low limb")
                p.emit(0x807E1B40)
                p.expect(3, "remquol writes the quotient through the ninth argument")
            continue

        high = {"acoshl": 1.75, "gammal": 1.25, "lgammal": 4.5, "logbl": 1.75}.get(name, 0.625)
        inputs = (high, 2**-60, 0.375, -(2**-61))
        # Check the difference of the two limb sums without rounding either
        # sum to binary64 first. The reference intentionally narrows some APIs.
        tolerance = 2**-92 if kind.startswith("QUAD") else 2**-46
        _, operands = probe.raw(struct.pack(">5d", *inputs, tolerance))
        for side in (1, 0):
            p.emit(0xC81E1850, 0xFDFE058E)
            for index in range(4):
                p.emit(0xC83E0000 + (index << 21) + operands + 8 * index)
            probe.call(name, (), side)
            p.emit(0xD83E1B00 + 16 * side, 0xD85E1B08 + 16 * side)
            if kind.startswith("NARROW"):
                p.emit(0x807E1B08 + 16 * side, 0x809E1B0C + 16 * side, 0x7C632378)
                p.expect(0, name + " clears the low limb after a binary64 calculation")
        p.checks.append({"offset": len(p.code) * 4, "check": name + " agrees with the original long-double result"})
        p.emit(
            0xC83E1B00,
            0xC85E1B10,
            0xFC011028,
            0xC83E1B08,
            0xC85E1B18,
            0xFC211028,
            0xFC00082A,
            0xFC000210,
            0xC85E0000 | (operands + 32),
            0xFC001000,
            0x41800008,
            0,
        )


def math_wide(probe, entries):
    p = probe.p
    left, right = 0x8123456701234567, 0x0000ABCDEF987654
    signed_min = -(1 << 63)
    signed_max = -signed_min - 1
    unsigned_max = (1 << 64) - 1

    def words(value):
        return [value >> 32 & 0xFFFFFFFF, value & 0xFFFFFFFF]

    def pair(offset, value, label):
        for index, word in enumerate(words(value)):
            p.emit(0x807E0000 | (offset + 4 * index))
            p.expect(word, label)

    aggregate = {
        "unsigned_max": [([], unsigned_max)],
        "signed_max": [([], signed_max)],
        "signed_min": [([], signed_min)],
        "add": [([*words(left), *words(right)], left + right), ([*words(unsigned_max), *words(2)], 1)],
        "subtract": [([*words(right), *words(left)], right - left)],
        "multiply": [([*words(left), *words(right)], left * right)],
        "negate": [(words(left), -left), (words(signed_min), signed_min)],
        "absolute": [(words(left), -left), (words(17), 17), (words(signed_min), signed_min)],
        "sign_extend": [([0x89ABCDEF], -0x76543211)],
        "zero_extend": [([0x89ABCDEF], 0x89ABCDEF)],
        "bitwise_and": [([*words(left), *words(right)], left & right)],
        "bitwise_or": [([*words(left), *words(right)], left | right)],
        "bitwise_xor": [([*words(left), *words(right)], left ^ right)],
        "bitwise_not": [(words(left), ~left)],
        "reinterpret": [(words(left), left)],
    }
    shifts = (0, 1, 31, 32, 37, 63, 64, 69, 96, 127, 128, 129)
    signed_left = left - (1 << 64)
    aggregate["left"] = [([*words(left), count], left << (count & 127)) for count in shifts]
    aggregate["unsigned_right"] = [([*words(left), count], left >> (count & 127)) for count in shifts]
    aggregate["signed_right"] = [
        ([*words(value), count], value >> (count & 127)) for value in (signed_left, right) for count in shifts
    ]
    scalars = {
        "narrow": [(words(left), left & 0xFFFFFFFF)],
        "signed_compare": [([*words(left), *words(right)], -2), ([*words(right), *words(left)], 1)],
        "unsigned_compare": [([*words(left), *words(right)], 1), ([*words(right), *words(left)], -2)],
        "logical_and": [([*words(left), *words(right)], 1), ([*words(left), *words(0)], 0)],
        "logical_or": [([*words(0), *words(right)], 1), ([*words(0), *words(0)], 0)],
        "logical_xor": [([*words(0), *words(right)], 1), ([*words(left), *words(right)], 0)],
        "logical_not": [(words(left), 0), (words(0), 1)],
    }
    divisions = {
        "signed_divide": [
            (-17, 5, -3, -2),
            (17, -5, -3, 2),
            (signed_min, -1, signed_min, 0),
            (-17, 0, signed_min, -17),
            (17, 0, signed_max, 17),
        ],
        "unsigned_divide": [(left, right, *divmod(left, right)), (left, 0, unsigned_max, left)],
    }
    float_inputs = {
        "signed_to_float": [(signed_max, float(1 << 63), -1.0), (-17, -17.0, 0.0)],
        "unsigned_to_float": [(unsigned_max, float(1 << 64), -1.0), (0, 0.0, 0.0)],
    }
    integer_inputs = {
        "float_to_signed": [
            (float(1 << 63), -1.0, signed_max),
            (-17.75, 0.0, -17),
            (1.0, -(2**-80), 0),
            (float("inf"), 0.0, signed_max),
            (float("nan"), 0.0, signed_min),
        ],
        "float_to_unsigned": [
            (float(1 << 64), -1.0, unsigned_max),
            (-17.75, 0.0, 0),
            (17.75, 0.0, 17),
            (float("inf"), 0.0, unsigned_max),
            (float("nan"), 0.0, 0),
        ],
    }
    for name, operation in entries:
        for side in (1, 0):
            for arguments, result in scalars.get(operation, ()):
                probe.call(name, arguments, side)
                p.expect(result, name + " returns the scalar result in r3")
            for arguments, result in aggregate.get(operation, ()):
                probe.call(name, [probe.ptr(0x1B50), *arguments], side)
                pair(0x1B50, result, name + " writes the aggregate result in guest byte order")
            for dividend, divisor, quotient, residue in divisions.get(operation, ()):
                for remainder in (0, probe.ptr(0x1B58), probe.ptr(0x1B50)):
                    probe.call(name, [probe.ptr(0x1B50), *words(dividend), *words(divisor), remainder], side)
                    pair(
                        0x1B50,
                        residue if remainder == probe.ptr(0x1B50) else quotient,
                        name + " writes the quotient first",
                    )
                    if remainder == probe.ptr(0x1B58):
                        pair(0x1B58, residue, name + " writes the remainder to the optional output")
            for value, high, low in float_inputs.get(operation, ()):
                p.emit(0xC81E1850, 0xFDFE058E)
                probe.call(name, words(value), side)
                p.emit(0xD83E1B50, 0xD85E1B58)
                pair(0x1B50, struct.unpack(">Q", struct.pack(">d", high))[0], name + " returns the high limb")
                pair(0x1B58, struct.unpack(">Q", struct.pack(">d", low))[0], name + " returns the residual limb")
            for high, low, value in integer_inputs.get(operation, ()):
                _, operands = probe.raw(struct.pack(">2d", high, low))
                p.emit(0xC81E1850, 0xFDFE058E, 0xC83E0000 | operands, 0xC85E0000 | (operands + 8))
                probe.call(name, [probe.ptr(0x1B50)], side)
                pair(0x1B50, value, name + " truncates and saturates the two-limb input")


def file_cases():
    source = (ROOT / "carbonlib/src/file_manager_entries.h").read_text()
    parameter_blocks = [
        (name, int(trap, 16), int(selector, 0))
        for name, trap, selector in re.findall(r"^CARBON_FILE_PB\((\w+), (0x\w+), (-1|0x\w+), \w+\)$", source, re.M)
    ]
    native_names = set(re.findall(r"^CARBON_FILE_\w+\((\w+),", source, re.M)) | {"FSMakeFSSpec", "PBWaitIOComplete"}
    if len(parameter_blocks) != 113 or len(native_names) != 147:
        raise ValueError("Incomplete Carbon File Manager table")
    return parameter_blocks, native_names


def fsref_cases():
    source = (ROOT / "carbonlib/src/fs_ref_entries.h").read_text()
    names = set(re.findall(r"^CARBON_FS\((\w+),", source, re.M))
    for stem in re.findall(r"^CARBON_FS_PAIR\((\w+),", source, re.M):
        names.update((stem + "Sync", stem + "Async"))
    if len(names) != 101:
        raise ValueError("Incomplete FSRef and fork File Manager table")
    names.update(re.findall(r"^CARBON_FS_(?:ALIAS|RESOURCE)\((\w+),", source, re.M))
    return names


def file_parameter_blocks(probe, entries):
    p = probe.p
    for name, trap, selector in entries:
        vector = 0x400 + 4 * (trap & 255)
        # The existing register-trap fixture captures D0-D3 and A0-A3 in guest memory.
        p.emit(0x81600000 | vector, 0x917E1820, 0x397E1DB0, 0x91600000 | vector)
        probe.call(name, [probe.ptr(0x2000)])
        p.emit(0x817E1820, 0x91600000 | vector)
        p.expect(-4217, name + " returns the patched trap's signed OSErr")
        p.emit(0x807E1E04)
        p.expect(trap, name + " supplies the OS trap word in D1")
        p.emit(0x807E1E10)
        p.pointer(0x2000, name + " supplies the original parameter block in A0")
        if selector >= 0:
            p.emit(0x807E1E00)
            p.expect(selector, name + " supplies the HFS selector in D0")


def file_operations(probe):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    file_name = probe.raw(b"\x11Carbon File Trial")
    renamed = probe.raw(b"\x13Carbon File Renamed")
    folder_name = probe.raw(b"\x11Carbon File Trial")
    payload = probe.raw(b"ARDItest")

    def success(name, arguments):
        probe.call(name, arguments)
        p.expect(0, name + " succeeds in the application's fixture directory")

    # Resolve the app's directory without relying on an absolute host path.
    success("GetCurrentProcess", [ptr(0x1EF0)])
    p.emit(0x3860003C, 0x907E1F00, 0x387E1F40, 0x907E1F38)
    success("GetProcessInformation", [ptr(0x1EF0), ptr(0x1F00)])
    p.emit(0xA87E1F40, 0x907E1FC0)
    volume, parent = slot(0x1FC0), slot(0x1F42)
    success("HSetVol", [0, volume, parent])
    success("HGetVol", [0, ptr(0x1FC4), ptr(0x1FC8)])
    p.emit(0xA07E1FC4, 0x809E1FC0, 0x5484043E)
    p.equal("HGetVol writes the application volume reference as a guest short")
    p.emit(0x807E1FC8, 0x809E1F42)
    p.equal("HGetVol writes the working directory ID")

    creator, kind = 0x54455354, 0x54455854
    success("HCreate", [volume, parent, file_name, creator, kind])
    success("HGetFInfo", [volume, parent, file_name, ptr(0x1FD0)])
    p.emit(0x807E1FD0)
    p.expect(kind, "HCreate writes the Finder type through the File Manager")
    p.emit(0x807E1FD4)
    p.expect(creator, "HCreate writes the Finder creator through the File Manager")

    def opened(name, path, permission):
        success(name, [volume, parent, path, permission, ptr(0x1FE0)])
        p.emit(0xA87E1FE0, 0x907E1FE4)
        p.nonzero(name + " returns a usable reference number")
        return slot(0x1FE4)

    reference = opened("HOpenDF", file_name, 3)
    p.emit(0x38600008, 0x907E1FE8)
    success("FSWrite", [reference, ptr(0x1FE8), payload])
    p.emit(0x807E1FE8)
    p.expect(8, "FSWrite updates the caller's byte count")
    success("GetFPos", [reference, ptr(0x1FEC)])
    p.emit(0x807E1FEC)
    p.expect(8, "GetFPos writes a guest longword")
    success("SetEOF", [reference, 5])
    success("GetEOF", [reference, ptr(0x1FEC)])
    p.emit(0x807E1FEC)
    p.expect(5, "GetEOF reports the new logical file size")
    success("SetFPos", [reference, 1, 2])
    p.emit(0x38600008, 0x907E1FE8, 0x3C60A5A5, 0x6063A5A5, 0x907E1FF0)
    probe.call("FSRead", [reference, ptr(0x1FE8), ptr(0x1FF0)])
    p.expect(-39, "FSRead reports EOF on a short transfer")
    p.emit(0x807E1FE8)
    p.expect(3, "FSRead returns the short transfer count")
    p.emit(0x807E1FF0)
    p.expect(0x444974A5, "FSRead copies only the available bytes")

    success("SetFPos", [reference, 1, 0])
    # A real asynchronous request must populate ioResult and invoke the guest completion.
    p.emit(0x387E1DB0, 0x907E200C, 0xA87E1FE0, 0xB07E2018, 0x387E1FF0, 0x907E2020, 0x38600004, 0x907E2024)
    success("PBReadAsync", [ptr(0x2000)])
    p.emit(0xA87E2010)
    p.expect(0, "PBReadAsync writes ioResult")
    p.emit(0x807E2028)
    p.expect(4, "PBReadAsync writes ioActCount")
    p.emit(0x807E1E10)
    p.pointer(0x2000, "File Manager completion receives the block in A0")
    p.emit(0x807E1FF0)
    p.expect(0x41524449, "File Manager completion observes the filled input buffer")
    for allocator in ("Allocate", "AllocContig"):
        p.emit(0x38600008, 0x907E1FE8)
        success(allocator, [reference, ptr(0x1FE8)])
        p.emit(0x807E1FE8)
        p.expect(8, allocator + " returns the allocated byte count")
        p.emit(0x3860FFFF, 0x907E1FE8)
        probe.call(allocator, [reference, ptr(0x1FE8)])
        p.expect(-50, allocator + " rejects a negative request")
        p.emit(0x807E1FE8)
        p.expect(0, allocator + " returns zero bytes for a rejected request")
    # The 68K completion increments the block's unused ioMisc longword.
    completion = probe.raw(bytes.fromhex("52a8001c4e75"))
    probe.arguments([completion], 0)
    p.emit(0x907E200C, 0x38600000, 0x907E201C, 0x38600008, 0x907E2024)
    success("PBAllocContigAsync", [ptr(0x2000)])
    p.emit(0x807E201C)
    p.expect(1, "PBAllocContigAsync completes exactly once on the caller's block")
    p.emit(0x807E2028)
    p.expect(8, "PBAllocContigAsync publishes the actual count")
    success("PBWaitIOComplete", [ptr(0x2000), 1000])
    for status, description in ((-43, "reports the signed I/O error"), (1, "polls an operation still in progress")):
        p.emit(0x38600000 | (status & 0xFFFF), 0xB07E2010)
        probe.call("PBWaitIOComplete", [ptr(0x2000), 0])
        p.expect(status, "PBWaitIOComplete " + description)
    probe.call("PBWaitIOComplete", [0, 0])
    p.expect(-50, "PBWaitIOComplete rejects a null parameter block")
    aiff_file_operations(probe, reference, payload)
    success("FSClose", [reference])
    file_open_deny_operations(probe, volume, parent, file_name)

    for name, locked in (("HSetFLock", 1), ("HRstFLock", 0)):
        success(name, [volume, parent, file_name])
        probe.arguments([file_name], 0)
        p.emit(0x907E2012, 0xA87E1FC2, 0xB07E2016, 0x38600000, 0xB07E201C, 0x807E1F42, 0x907E2030)
        success("PBHGetFInfoSync", [ptr(0x2000)])
        p.emit(0x887E201E, 0x70630001)
        p.expect(locked, name + " updates the lock bit in the catalog")
    success("HRename", [volume, parent, file_name, renamed])
    success("FSClose", [opened("HOpen", renamed, 1)])
    success("FSClose", [opened("HOpenRF", renamed, 1)])

    def delete(path):
        probe.arguments([path], 0)
        p.emit(0x907E2012, 0xA87E1FC2, 0xB07E2016, 0x807E1F42, 0x907E2030)
        success("PBHDeleteSync", [ptr(0x2000)])

    success("HDelete", [volume, parent, renamed])
    probe.call("HGetFInfo", [volume, parent, renamed, ptr(0x1FD0)])
    p.expect(-43, "HDelete removes the named file from its parent")
    success("DirCreate", [volume, parent, folder_name, ptr(0x1FD0)])
    p.emit(0x807E1FD0)
    p.nonzero("DirCreate returns the new catalog directory ID")
    delete(folder_name)
    move_rename_operations(probe, volume, parent)


def file_open_deny_operations(probe, volume, parent, name):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    _, first = probe.far_raw(bytes(56))
    _, second = probe.far_raw(bytes(56))
    _, io = probe.far_raw(bytes(12))
    # The completion receives the caller's original PB in A0.
    callback = probe.far_raw(bytes.fromhex("52a800344e75"))

    def open_fork(api, block, flags, expected=0, completion=0):
        probe.arguments([ptr(block), name, volume, parent, flags, completion], 0)
        p.emit(0x90830012, 0xB0A30016, 0x90C30030, 0xB0E3001A, 0x9103000C)
        probe.call(api, [ptr(block)])
        p.expect(expected, api + " applies the requested sharing modes")

    def reference(block, destination):
        probe.arguments([ptr(block + 24), ptr(destination)], 0)
        p.emit(0xA8630000, 0x90640000)
        return slot(destination)

    def close(ref):
        probe.call("FSClose", [ref])
        p.expect(0, "close a fork and release its deny modes")

    open_fork("PBHOpenDenySync", first, 0x21)
    protected_data = reference(first, io)
    p.nonzero("read and deny-write opens the data fork")
    probe.call("HOpenDF", [volume, parent, name, 3, ptr(second + 24)])
    p.expect(-54, "legacy writable opens respect an existing deny-write")
    open_fork("PBHOpenDenyAsync", second, 0x02, -54, callback)
    probe.arguments([slot(second + 52)], 0)
    p.expect(1, "a denied async request completes once on the original block")
    open_fork("PBHOpenRFDenyAsync", second, 0x11)
    protected_resource = reference(second, io + 4)
    p.nonzero("the resource fork can use independent deny modes")
    open_fork("PBHOpenRFDenySync", first, 0x01, -54)
    close(protected_resource)
    close(protected_data)

    open_fork("PBHOpenRFDenySync", first, 0x02)
    write_only = reference(first, io)
    p.nonzero("closing the earlier fork releases its deny-read access")
    probe.arguments([ptr(io + 4), 1], 0)
    p.emit(0x90830000)
    probe.call("FSRead", [write_only, ptr(io + 4), ptr(io + 8)])
    p.expect(-54, "a write-only fork handle cannot read")
    probe.arguments([slot(io + 4)], 0)
    p.expect(0, "a rejected read leaves the guest byte count at zero")
    close(write_only)


def move_rename_operations(probe, volume, parent):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    _, pb = probe.far_raw(bytes(52))
    _, fid = probe.far_raw(bytes(60))
    _, resolved_name = probe.far_raw(bytes(32))
    _, saved_id = probe.far_raw(bytes(4))
    _, destination_id = probe.far_raw(bytes(4))
    _, fork = probe.far_raw(bytes(8) + b"fork" + bytes(8))
    source_name = probe.far_raw(b"\x12Carbon Move Source")
    new_name = probe.far_raw(b"\x12Carbon Move Target")
    folder_name = probe.far_raw(b"\x12Carbon Move Folder")
    invalid_name = probe.far_raw(b"\x04:bad")
    completion = probe.far_raw(bytes.fromhex("52a800184e75"))  # addq.l #1, 24(a0); rts

    def passed(name, arguments):
        probe.call(name, arguments)
        p.expect(0, name + " succeeds during catalog move-and-rename")

    def setup(name, destination, old_parent, new_leaf, callback=0):
        probe.arguments([ptr(pb), callback, name, volume, new_leaf, destination, old_parent], 0)
        p.emit(
            0x9083000C,  # ioCompletion
            0x90A30012,  # ioNamePtr
            0xB0C30016,  # ioVRefNum
            0x90E30020,  # ioCopyName
            0x91030024,  # ioNewDirID
            0x91230030,  # ioDirID
            0x38000000,
            0x90030018,  # clear the completion count
        )

    passed("DirCreate", [volume, parent, folder_name, ptr(destination_id)])
    destination = slot(destination_id)
    passed("HCreate", [volume, parent, source_name, 0x54455354, 0x54455854])
    probe.arguments([ptr(fid), source_name, volume, parent], 0)
    p.emit(0x90830012, 0xB0A30016, 0x90C30030)
    probe.call("PBCreateFileIDRefSync", [ptr(fid)])
    p.emit(0x2C03FF85, 0)
    host_volume = len(p.code) - 1
    p.expect(0, "create the original HFS file ID reference")
    probe.arguments([ptr(fid), ptr(saved_id)], 0)
    p.emit(0x80630036, 0x90640000)
    p.nonzero("HFS exposes the catalog file ID")
    probe.call("PBCreateFileIDRefAsync", [ptr(fid)])
    p.expect(-1301, "a second request reports an existing file ID")
    probe.arguments([slot(fid + 54), slot(saved_id)], 0)
    p.equal("repeated creation returns the original file ID")
    p.emit(0)
    id_ready = len(p.code) - 1
    p.code[host_volume] = 0x41820000 | (len(p.code) - host_volume) * 4
    for api in ("PBCreateFileIDRefAsync", "PBResolveFileIDRefSync", "PBDeleteFileIDRefSync"):
        probe.call(api, [ptr(fid)])
        p.expect(-123, "non-HFS volumes report that they do not support file ID references")
    p.code[id_ready] = 0x48000000 | (len(p.code) - id_ready) * 4
    passed("HOpenRF", [volume, parent, source_name, 3, ptr(fork)])
    probe.arguments([ptr(fork + 4), 4], 0)
    p.emit(0x90830000)
    probe.arguments([ptr(fork), ptr(fork + 16)], 0)
    p.emit(0xA8630000, 0x90640000)
    resource_file = slot(fork + 16)
    passed("FSWrite", [resource_file, ptr(fork + 4), ptr(fork + 8)])
    passed("FSClose", [resource_file])
    passed("HOpenDF", [volume, parent, source_name, 3, ptr(fork)])
    probe.arguments([ptr(fork), ptr(fork + 16)], 0)
    p.emit(0xA8630000, 0x90640000)
    passed("FSWrite", [resource_file, ptr(fork + 4), ptr(fork + 8)])
    passed("FSClose", [resource_file])
    passed("HCreate", [volume, destination, new_name, 0x54455354, 0x54455854])
    setup(source_name, destination, parent, new_name)
    probe.call("PBHCopyFileSync", [ptr(pb)])
    p.expect(-48, "copy rejects an existing destination")
    probe.call("PBHMoveRenameSync", [ptr(pb)])
    p.expect(-48, "move-and-rename rejects a duplicate destination")
    passed("HGetFInfo", [volume, parent, source_name, ptr(0x1FD0)])
    passed("HDelete", [volume, destination, new_name])
    setup(source_name, destination, parent, 0)
    passed("PBHCopyFileSync", [ptr(pb)])
    passed("HGetFInfo", [volume, destination, source_name, ptr(0x1FD0)])
    passed("HDelete", [volume, destination, source_name])
    setup(source_name, parent, parent, new_name, completion)
    probe.arguments([ptr(pb), folder_name, volume], 0)
    p.emit(0x9083001C, 0xB0A30018)  # ioNewName and ioDstVRefNum
    passed("PBHCopyFileAsync", [ptr(pb)])
    probe.arguments([slot(pb + 24)], 0)
    p.emit(0x7063FFFF)
    p.expect(1, "async copy calls the completion once")
    passed("HGetFInfo", [volume, destination, new_name, ptr(0x1FD0)])
    p.emit(0x807E1FD4)
    p.expect(0x54455354, "a copy preserves the Finder creator")
    for open_call in ("HOpenDF", "HOpenRF"):
        passed(open_call, [volume, destination, new_name, 1, ptr(fork)])
        probe.arguments([ptr(fork), ptr(fork + 16)], 0)
        p.emit(0xA8630000, 0x90640000)
        probe.arguments([ptr(fork + 4), 4, ptr(fork + 12), 0], 0)
        p.emit(0x90830000, 0x90C50000)
        passed("FSRead", [resource_file, ptr(fork + 4), ptr(fork + 12)])
        probe.arguments([slot(fork + 12)], 0)
        p.expect(0x666F726B, "the copy contains the original bytes from " + open_call)
        passed("FSClose", [resource_file])
    passed("HDelete", [volume, destination, new_name])
    probe.arguments([ptr(pb), 0], 0)
    p.emit(0x9083001C)
    setup(source_name, destination, parent, invalid_name)
    probe.call("PBHMoveRenameSync", [ptr(pb)])
    p.expect(-37, "the replacement name must be a leaf")
    setup(source_name, destination, parent, new_name, completion)
    passed("PBHMoveRenameAsync", [ptr(pb)])
    probe.arguments([slot(pb + 24)], 0)
    p.expect(1, "async move-and-rename completes once on the caller's parameter block")
    passed("HGetFInfo", [volume, destination, new_name, ptr(0x1FD0)])
    passed("HOpenRF", [volume, destination, new_name, 1, ptr(fork)])
    probe.arguments([ptr(fork), ptr(fork + 16)], 0)
    p.emit(0xA8630000, 0x90640000)
    passed("FSRead", [resource_file, ptr(fork + 4), ptr(fork + 12)])
    probe.arguments([slot(fork + 12)], 0)
    p.expect(0x666F726B, "the moved file retains its resource fork data")
    passed("FSClose", [resource_file])
    probe.call("HGetFInfo", [volume, parent, source_name, ptr(0x1FD0)])
    p.expect(-43, "move-and-rename removes the old catalog entry")
    probe.arguments([slot(saved_id)], 0)
    p.emit(0x2C030000, 0)
    skip_resolve = len(p.code) - 1
    probe.arguments([ptr(fid), ptr(resolved_name)], 0)
    p.emit(0x90830012)
    passed("PBResolveFileIDRefAsync", [ptr(fid)])
    probe.arguments([slot(resolved_name), 0x12436172], 0)
    p.equal("HFS resolve copies the current Pascal name")
    probe.arguments([slot(resolved_name + 12), 0x20546172], 0)
    p.equal("HFS resolve returns the renamed file")
    probe.arguments([slot(fid + 48), destination], 0)
    p.equal("HFS resolve returns the file's new parent directory")
    passed("HRename", [volume, destination, new_name, source_name])
    passed("PBResolveFileIDRefSync", [ptr(fid)])
    probe.arguments([slot(resolved_name + 12), 0x20536F75], 0)
    p.equal("HFS file IDs survive a rename within the same directory")
    passed("HRename", [volume, destination, source_name, new_name])
    passed("PBDeleteFileIDRefAsync", [ptr(fid)])
    probe.call("PBResolveFileIDRefSync", [ptr(fid)])
    p.expect(-1300, "deleting the reference invalidates the HFS file ID")
    p.code[skip_resolve] = 0x41820000 | (len(p.code) - skip_resolve) * 4
    setup(new_name, parent, destination, 0)
    passed("PBHMoveRenameSync", [ptr(pb)])
    passed("HDelete", [volume, parent, new_name])
    passed("HDelete", [volume, parent, folder_name])


def aiff_file_operations(probe, reference, payload):
    p = probe.p
    buffer = probe.ptr(0x1FF0)
    count = probe.ptr(0x1FE8)
    description = probe.far_raw(bytes([0xA5]) * 36)
    base = description[1]

    def read_word(offset, value):
        probe.call("SetFPos", [reference, 1, offset])
        p.expect(0, "seek to the generated AIFF field")
        p.emit(0x38600004, 0x907E1FE8)
        probe.call("FSRead", [reference, count, buffer])
        p.expect(0, "read the generated AIFF field")
        p.emit(0x807E1FF0)
        p.expect(value, "AIFF chunk geometry and sample bytes are in file order")

    for codec, kind, header_size, sound_offset, frame_offset, sample_bytes, frames in (
        (b"NONE", b"AIFF", 54, 38, 22, 7, 7),
        (b"MAC3", b"AIFC", 82, 66, 34, 8, 4),
    ):
        for data_bytes in (0, sample_bytes):
            probe.call("SetFPos", [reference, 1, 1])
            p.expect(0, "seek to the same nonzero AIFF start")
            probe.call("SetupAIFFHeader", [reference, 1, 44100 << 16, 8, int.from_bytes(codec, "big"), data_bytes, 0])
            p.expect(0, "build a placeholder or final sampled sound file header")
            probe.call("GetFPos", [reference, count])
            p.expect(0, "get the post-header file mark")
            p.emit(0x807E1FE8)
            p.expect(1 + header_size, "SetupAIFFHeader leaves the mark at the first sample byte")
            if not data_bytes:
                p.emit(0x38600008, 0x907E1FE8)
                probe.call("FSWrite", [reference, count, payload])
                p.expect(0, "append samples directly after the placeholder header")

        for offset, value in (
            (0, 0x464F524D),
            (4, header_size),
            (8, int.from_bytes(kind, "big")),
            (frame_offset, frames),
            (sound_offset, 0x53534E44),
            (sound_offset + 4, 8 + sample_bytes),
            (header_size, 0x41524449),
        ):
            read_word(1 + offset, value)

        # The file parser reads a FORM at the beginning of the file even when
        # the caller's current mark is elsewhere.
        probe.call("SetFPos", [reference, 1, 0])
        p.expect(0, "seek to the beginning of a sampled sound file")
        probe.call("SetupAIFFHeader", [reference, 1, 44100 << 16, 8, int.from_bytes(codec, "big"), sample_bytes, 0])
        p.expect(0, "write the readable FORM at file offset zero")
        probe.call("ParseAIFFHeader", [reference, description, probe.ptr(base + 28), probe.ptr(base + 32)])
        p.expect(0, "parse a sound file header from a nonzero current mark")
        format_code = int.from_bytes(b"twos" if codec == b"NONE" else codec, "big")
        for field, value in ((4, format_code), (8, 0x00010008), (12, 44100 << 16), (28, frames), (32, header_size)):
            probe.arguments([probe.ptr(base + field)], 0)
            p.emit(0x80630000)
            p.expect(value, "the AIFF description reports the encoding, file frame count and sample data offset")
        probe.call("GetFPos", [reference, count])
        p.expect(0, "read the current mark after parsing")
        p.emit(0x807E1FE8)
        p.expect(header_size, "ParseAIFFHeader restores the caller's file mark")

    probe.call("SetFPos", [reference, 1, 0])
    p.expect(0, "seek to the FORM signature")
    p.emit(0x38600008, 0x907E1FE8)
    probe.call("FSWrite", [reference, count, payload])
    p.expect(0, "replace the FORM signature with malformed data")
    probe.call("ParseAIFFHeader", [reference, description, probe.ptr(base + 28), probe.ptr(base + 32)])
    p.expect(-208, "reject a file without a valid FORM signature")
    probe.arguments([probe.ptr(base + 32)], 0)
    p.emit(0x80630000)
    p.expect(82, "a rejected AIFF leaves the previous data offset intact")


def launch_services_operations(probe):
    p, ptr, slot = probe.p, probe.ptr, probe.slot
    reference = probe.far_raw(bytes(232))
    round_trip = ptr(reference[1] + 80)
    spec = ptr(reference[1] + 160)
    _, url = probe.raw(bytes(8))
    extension = url + 4
    creator = 0x50505242  # Finder creator of this grouped application.

    def lookup(file_type, signature, suffix, roles, output_ref, output_url, status=0):
        probe.call("LSGetApplicationForInfo", [file_type, signature, suffix, roles, output_ref, output_url])
        p.expect(status, "Launch Services returns the expected application lookup status")

    lookup(0, creator, 0, 0xFFFFFFFF, reference, ptr(url))
    p.emit(0x807E0000 | url)
    p.nonzero("Launch Services returns an owned application URL")
    probe.call("_CFGetFSSpecFromURL", [0, slot(url), spec])
    p.expect(1, "the application URL resolves to a file specification")
    probe.call("FSpMakeFSRef", [spec, round_trip])
    p.expect(0, "the resulting specification resolves to an FSRef")
    probe.call("FSCompareFSRefs", [reference, round_trip])
    p.expect(0, "the application URL and FSRef identify the same application")
    probe.call("CFRelease", [slot(url)])
    lookup(0, creator, 0, 2, reference, 0)
    lookup(0, creator, 0, 4, 0, ptr(url))
    probe.call("CFRelease", [slot(url)])

    probe.arguments([0xA55AA55A, reference], 0)
    p.emit(0x90640000, 0x907E0000 | url)
    lookup(0, 0x7A714C53, 0, 0xFFFFFFFF, reference, ptr(url), -10814)
    lookup(0, creator, 0, 1, reference, ptr(url), -10814)
    lookup(0, 0, 0, 0xFFFFFFFF, reference, ptr(url), -50)
    lookup(0, creator, 0, 0xFFFFFFFF, 0, 0, -50)
    probe.call("CFStringCreateWithCString", [0, probe.far_raw(b"romaw-unknown-suffix-fixture\0"), 0x600])
    p.nonzero("construct an unregistered filename extension")
    p.emit(0x907E0000 | extension)
    lookup(0, 0, slot(extension), 0xFFFFFFFF, reference, ptr(url), -10814)
    probe.call("CFRelease", [slot(extension)])
    probe.arguments([reference], 0)
    p.emit(0x80630000)
    p.expect(0xA55AA55A, "failed lookup preserves the caller's FSRef")
    p.emit(0x807E0000 | url)
    p.expect(0xA55AA55A, "failed lookup preserves the caller's URL slot")


def file_spec_operations(probe):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot

    def call(name, arguments, expected=0):
        probe.call(name, arguments)
        p.expect(expected, name + " returns the expected FSSpec operation result")

    volume, parent = slot(0x1FC0), slot(0x1F42)
    _, base = probe.raw(bytes(0x300))
    name = b"Carbon Spec Trial"
    p.data[base + 6 : base + 7 + len(name)] = bytes([len(name)]) + name
    path = ptr(base + 6)
    original, renamed, folder, destination = (ptr(base + offset) for offset in (0, 0x50, 0xA0, 0xF0))
    new_name = probe.raw(b"\x12Carbon Spec Rename")
    folder_name = probe.raw(b"\x12Carbon Spec Folder")
    info_offset, fork_offset, id_offset, catalog, pb = (base + offset for offset in (0x180, 0x190, 0x198, 0x200, 0x280))
    finder_info, fork, folder_id = ptr(info_offset), ptr(fork_offset), ptr(id_offset)
    type_code, creator = 0x54455854, 0x53504543

    call("FSMakeFSSpec", [volume, parent, path, original], -43)
    p.emit(0x807E0000 | (base + 6))
    p.expect(0x11436172, "FSMakeFSSpec preserves an overlapping filename on a missing leaf")
    call("FSpCreate", [original, creator, type_code, 7])
    p.emit(
        0x387E0000 | (base + 6),
        0x907E0000 | (pb + 0x12),
        0xA07E0000 | base,
        0xB07E0000 | (pb + 0x16),
        0x387E0000 | (base + 0x50),
        0x907E0000 | (pb + 0x1C),
        0x807E0000 | (base + 2),
        0x907E0000 | (pb + 0x30),
    )
    call("PBMakeFSSpecSync", [ptr(pb)])
    for offset in (0, 4, 8, 12, 16, 20):
        p.emit(0x807E0000 | (base + offset), 0x809E0000 | (base + 0x50 + offset))
        p.equal("PBMakeFSSpecSync returns the same file specification")
    p.emit(0x387E1DB0, 0x907E0000 | (pb + 0x0C), 0x38607FFF, 0xB07E0000 | (pb + 0x10))
    call("PBMakeFSSpecAsync", [ptr(pb)])
    p.emit(0xA87E0000 | (pb + 0x10))
    p.expect(0, "PBMakeFSSpecAsync writes ioResult before completion")
    p.emit(0x807E1E10)
    p.pointer(pb, "PBMakeFSSpecAsync invokes the caller's completion")
    call("FSpGetFInfo", [original, finder_info])
    p.emit(0x807E0000 | info_offset)
    p.expect(type_code, "FSpCreate writes the requested Finder type")
    p.emit(0x807E0000 | (info_offset + 4))
    p.expect(creator, "FSpCreate writes the requested Finder creator")
    p.emit(
        0x387E0000 | (base + 6),
        0x907E0000 | (catalog + 0x12),
        0xA07E0000 | base,
        0xB07E0000 | (catalog + 0x16),
        0x807E0000 | (base + 2),
        0x907E0000 | (catalog + 0x30),
    )
    call("PBGetCatInfoSync", [ptr(catalog)])
    p.emit(0x887E0000 | (catalog + 0x5C))
    p.expect(0x87, "FSpCreate saves the explicit script in the extended Finder info")
    p.emit(0x3C604152, 0x60634449, 0x907E0000 | (info_offset + 4))
    call("FSpSetFInfo", [original, finder_info])
    call("FSpGetFInfo", [original, finder_info])
    p.emit(0x807E0000 | (info_offset + 4))
    p.expect(0x41524449, "FSpSetFInfo changes the Finder creator")
    call("FSpGetFInfo", [0, finder_info], -35)
    call("FSpGetFInfo", [original, 0], -35)

    for name in ("FSpOpenDF", "FSpOpenRF"):
        call(name, [original, 1, fork])
        p.emit(0xA87E0000 | fork_offset, 0x907E0000 | (fork_offset + 4))
        p.nonzero(name + " opens the requested fork")
        call("FSClose", [slot(fork_offset + 4)])
    call("FSpSetFLock", [original])
    call("FSpRstFLock", [original])
    call("FSpRename", [original, new_name])
    call("FSMakeFSSpec", [volume, parent, new_name, renamed])

    call("FSMakeFSSpec", [volume, parent, folder_name, folder], -43)
    call("FSpDirCreate", [folder, -1, folder_id])
    p.emit(0x807E0000 | id_offset)
    p.nonzero("FSpDirCreate writes the new directory ID")
    call("FSpCatMove", [renamed, folder])
    call("FSMakeFSSpec", [volume, slot(id_offset), new_name, destination])
    call("FSpDelete", [destination])
    call("FSpDelete", [folder])


def fsref_operations(probe):
    p = probe.p
    _, base = probe.raw(bytes(0x880))
    ptr = lambda at: probe.ptr(base + at)
    slot = lambda at: probe.slot(base + at)

    def call(name, arguments, expected=0, *, saved=False):
        probe.call(name, arguments, int(saved))
        p.expect(expected, name + " returns the expected File Manager status")

    def put(at, value, width=4):
        probe.arguments([value], 0)
        p.emit(({1: 0x987E0000, 2: 0xB07E0000, 4: 0x907E0000}[width]) | (base + at))

    def check(at, value, message, width=4):
        p.emit(({1: 0x887E0000, 2: 0xA07E0000, 4: 0x807E0000}[width]) | (base + at))
        p.expect(value, message)

    def pb(stem, fields, expected=0, *, modes=("Sync", "Async")):
        for mode in modes:
            for offset in range(0, 128, 4):
                put(0x740 + offset, 0)
            put(0x750, 1, 2)
            if mode == "Async":
                put(0x74C, probe.ptr(0x1DB0))
                p.emit(0x38600000, 0x907E1E10)
            for offset, value, width in fields:
                put(0x740 + offset, value, width)
            call(stem + mode, [ptr(0x740)], expected)
            check(0x750, expected & 0xFFFF, stem + " stores the result in its parameter block", 2)
            if mode == "Async":
                p.emit(0x807E1E10)
                p.pointer(base + 0x740, stem + " invokes the guest completion")

    volume, directory_id = probe.slot(0x1FC0), probe.slot(0x1F42)
    empty = probe.raw(b"\0")
    folder_name = probe.raw("Carbon Catalog".encode("utf-16-be"))
    leaf = probe.raw("Cafe\u0301".encode("utf-16-be"))
    renamed = probe.raw("Renamed".encode("utf-16-be"))
    payload = probe.raw(b"ARDI file forks")
    container, folder, ref, other, resolved, spec = (ptr(at) for at in (0, 0x50, 0xA0, 0xF0, 0x140, 0x190))
    info, name, fork_name = ptr(0x200), ptr(0x2A0), ptr(0x4A0)
    fork, fork_word, position, actual, cursor = (ptr(at) for at in (0x6A0, 0x6A4, 0x6A8, 0x6B0, 0x6B4))
    rename_args = [ref, 7, renamed, 0, other]

    call("FSMakeFSSpec", [volume, directory_id, empty, spec])
    call("FSpMakeFSRef", [spec, container])
    call("FSpMakeFSRef", [spec, other], saved=True)
    call("FSCompareFSRefs", [container, other])
    pb("PBMakeFSRef", ((18, empty, 4), (22, volume, 2), (48, directory_id, 4), (60, other, 4)))
    call("FSCompareFSRefs", [container, other])
    call("FSFindFolder", [-32768, 0x6D616373, 0, other])
    call("FSFindFolderExtended", [-32768, 0x6D616373, 0, 0, 0, resolved])
    call("FSCompareFSRefs", [other, resolved])
    call("FSGetVolumeInfo", [volume, 0, ptr(0x6B8), 0xC0, info, name, resolved])
    p.emit(0xA07E0000 | (base + 0x6B8), 0x809E1FC0, 0x5484043E)
    p.equal("FSGetVolumeInfo reports the selected volume")
    p.emit(0x807E0000 | (base + 0x238))
    p.nonzero("FSGetVolumeInfo returns the allocation block size")
    call("FSGetVolumeInfo", [volume, 0, 0, 0, 0, 0, other], saved=True)
    call("FSCompareFSRefs", [resolved, other])
    call("FSGetCatalogInfo", [resolved, 0x18, info, 0, 0, 0])
    check(0x208, 2, "Volume FSRef points to the root catalog node")
    path_buffer, is_directory = ptr(0x4A0), ptr(0x6B8)
    call("FSRefMakePath", [resolved, path_buffer, 512])
    call("FSPathMakeRef", [path_buffer, other, is_directory])
    check(0x6B8, 1, "The root pathname resolves to a directory", 1)
    call("FSCompareFSRefs", [resolved, other])
    call("FSSetVolumeInfo", [volume, 0, info])
    call("FlushVol", [0, volume])
    pb("PBGetVolumeInfo", ((22, volume, 2), (28, 0xC0, 4), (32, info, 4), (36, name, 4), (40, resolved, 4)))
    pb("PBSetVolumeInfo", ((22, volume, 2), (32, info, 4)))
    pb("PBXGetVolInfo", ((22, volume, 2),))

    call("FSCreateDirectoryUnicode", [container, 14, folder_name, 0, 0, folder, spec, ptr(0x6BC)])
    p.emit(0x807E0000 | (base + 0x6BC))
    p.nonzero("FSCreateDirectoryUnicode writes the directory ID")
    call("FSCreateFileUnicode", [folder, 5, leaf, 0, 0, ref, spec])
    call("FSpMakeFSRef", [spec, other])
    call("FSCompareFSRefs", [ref, other])
    call("FSCompareFSRefs", [ref, folder], -1420)
    put(0x4A0, 0x12345678)
    call("FSRefMakePath", [ref, path_buffer, 1], -210)
    check(0x4A0, 0x12345678, "A short pathname buffer is left untouched")
    call("FSRefMakePath", [ref, path_buffer, 512])
    call("FSPathMakeRef", [path_buffer, other, is_directory])
    check(0x6B8, 0, "A pathname to a file clears the directory flag", 1)
    call("FSCompareFSRefs", [ref, other])
    call("FSRefMakePath", [folder, path_buffer, 512])
    call("FSPathMakeRef", [path_buffer, other, is_directory])
    check(0x6B8, 1, "A pathname to a directory sets the directory flag", 1)
    call("FSCompareFSRefs", [folder, other])
    call("FSMakeFSRefUnicode", [folder, 5, leaf, 0, other])
    pb("PBCompareFSRefs", ((28, ref, 4), (56, other, 4)))
    pb("PBMakeFSRefUnicode", ((28, folder, 4), (40, 5, 4), (44, leaf, 4), (60, other, 4)))
    call("FSGetCatalogInfo", [ref, 0x5F, info, name, spec, resolved])
    call("FSCompareFSRefs", [folder, resolved])
    check(0x2A0, 4, "Catalog lookup writes the composed filename length", 2)
    check(0x2A8, 0xE9, "Catalog lookup returns the composed MacRoman accent", 2)
    call("FSGetCatalogInfo", [ref, 0, 0, 0, 0, resolved], saved=True)
    call("FSCompareFSRefs", [folder, resolved])
    put(0x248, 0x54455854)
    put(0x24C, 0x46535246)
    call("FSSetCatalogInfo", [ref, 0x800, info])
    pb("PBSetCatalogInfo", ((28, ref, 4), (32, 0x800, 4), (36, info, 4)))
    put(0x248, 0)
    pb("PBGetCatalogInfo", ((28, ref, 4), (32, 0x800, 4), (36, info, 4)))
    check(0x248, 0x54455854, "Catalog round trip preserves Finder type")
    check(0x24C, 0x46535246, "Catalog round trip preserves Finder creator")

    alias_handle, alias_flags = slot(0x704), ptr(0x70C)
    folder_flag = ptr(0x70D)
    call("FSIsAliasFile", [ref, alias_flags, folder_flag])
    check(0x70C, 0, "a regular file has neither the alias nor folder flag", 2)
    call("FSIsAliasFile", [folder, alias_flags, folder_flag])
    check(0x70C, 1, "a directory has only the folder flag", 2)
    call("FSNewAliasMinimal", [ref, ptr(0x704)])
    p.emit(0x807E0000 | (base + 0x704))
    p.nonzero("FSNewAliasMinimal allocates an AliasHandle")
    call("FSResolveAlias", [0, alias_handle, other, alias_flags])
    call("FSCompareFSRefs", [ref, other])
    call("FSUpdateAlias", [0, folder, alias_handle, alias_flags])
    check(0x70C, 1, "updating the target changes the alias record", 1)
    call("FSResolveAliasWithMountFlags", [0, alias_handle, other, alias_flags, 0])
    call("FSCompareFSRefs", [folder, other])
    probe.call("DisposeHandle", [alias_handle])

    call("FSNewAlias", [0, ref, ptr(0x704)])
    call("FSFollowFinderAlias", [0, alias_handle, 0, other, alias_flags])
    call("FSCompareFSRefs", [ref, other])

    matched_spec = probe.far_raw(bytes(70))
    match_count = probe.far_raw(bytes(2))
    for matcher in ("MatchAlias", "MatchAliasNoUI"):
        probe.arguments([match_count], 0)
        p.emit(0x38800001, 0xB0830000)
        call(matcher, [0, 0, alias_handle, match_count, matched_spec, alias_flags, 0, 0])
        probe.arguments([match_count], 0)
        p.emit(0xA0630000)
        p.expect(1, matcher + " finds one matching alias target")
        call("FSpMakeFSRef", [matched_spec, other])
        call("FSCompareFSRefs", [ref, other])

    alias_name = probe.raw(b"\x0bCarbon Link")
    alias_file = ptr(0x4C0)
    call("FSMakeFSSpec", [volume, slot(0x6BC), alias_name, alias_file], -43)
    probe.call("CurResFile")
    p.emit(0x907E0000 | (base + 0x724))
    probe.call("FSpCreateResFile", [alias_file, 0x46535246, 0x616C6973, 0])
    call("ResError", [])
    probe.call("FSpOpenResFile", [alias_file, 3])
    p.nonzero("open the alias file's resource fork")
    p.emit(0x907E0000 | (base + 0x710))
    probe.call("UseResFile", [slot(0x710)])
    probe.call("AddResource", [alias_handle, 0x616C6973, 128, empty])
    call("ResError", [])
    probe.call("CloseResFile", [slot(0x710)])
    call("ResError", [])
    probe.call("UseResFile", [slot(0x724)])
    call("FSpGetFInfo", [alias_file, ptr(0x714)])
    put(0x71C, 0x8000, 2)
    p.emit(0xA87E0000 | (base + 0x4C0), 0x907E0000 | (base + 0x728))
    call("HSetFInfo", [slot(0x728), slot(0x4C2), ptr(0x4C6), ptr(0x714)])

    call("FSpMakeFSRef", [alias_file, other])
    call("FSIsAliasFile", [other, alias_flags, folder_flag])
    check(0x70C, 0x0100, "the Finder flag identifies the resource file as an alias", 2)
    for resolver, flags in (
        ("FSResolveAliasFile", []),
        ("FSResolveAliasFileWithMountFlags", [0]),
        ("FSResolveAliasFileWithMountFlags", [1]),
    ):
        call("FSpMakeFSRef", [alias_file, other])
        call(resolver, [other, 1, folder_flag, alias_flags, *flags])
        check(0x70C, 0x0100, resolver + " confirms the file alias resolved to a regular file", 2)
        call("FSCompareFSRefs", [ref, other])
    call("IsAliasFile", [alias_file, alias_flags, folder_flag])
    check(0x70C, 0x0100, "the classic catalog query identifies the resource file as an alias", 2)
    probe.call("BlockMoveData", [alias_file, ptr(0x810), 70])
    call("ResolveAliasFileWithMountFlagsNoUI", [ptr(0x810), 1, folder_flag, alias_flags, 1])
    check(0x70C, 0x0100, "the no-UI resolver confirms the Finder alias target", 2)
    call("FSpMakeFSRef", [ptr(0x810), other])
    call("FSCompareFSRefs", [ref, other])
    call("ResolveAliasFileWithMountFlagsNoUI", [ptr(0x810), 1, folder_flag, alias_flags, 2], -50)
    call("FSResolveAlias", [0, 0, other, alias_flags], -50)
    call("FSpDelete", [alias_file])

    resource_leaf = probe.raw("Resource File".encode("utf-16-be"))
    chain, res_ref = ptr(0x70C), ptr(0x710)
    probe.call("FSCreateResFile", [folder, 13, resource_leaf, 0x800, info, other, alias_file])
    call("ResError", [])
    call("FSResourceFileAlreadyOpen", [other, chain, res_ref], 0)
    check(0x70C, 0, "closed resource files are outside the resource chain", 1)
    check(0x710, 0xFFFF, "closed resource files report no reference", 2)
    probe.call("FSOpenResFile", [other, 3])
    p.emit(0x907E0000 | (base + 0x728))
    call("ResError", [])
    call("Count1Types", [], 0)
    call("FSResourceFileAlreadyOpen", [other, chain, res_ref], 1)
    check(0x70C, 1, "the open resource fork is in the resource chain", 1)
    p.emit(0xA07E0000 | (base + 0x710), 0xA09E0000 | (base + 0x72A))
    p.equal("the open query returns the resource manager reference number")
    call("FSpResourceFileAlreadyOpen", [alias_file, chain, res_ref], 1)
    put(0x710, 0x4444, 2)
    call("FSpOpenOrphanResFile", [alias_file, 3, res_ref], -50)
    check(0x710, 0x4444, "an orphan open leaves the output intact when it reuses an attached map", 2)
    probe.call("NewHandle", [4])
    p.nonzero("allocate a resource before detaching its map")
    p.emit(0x907E0000 | (base + 0x704), 0x80630000, 0x3C005253, 0x60004346, 0x90030000)
    probe.call("AddResource", [slot(0x704), 0x72736C66, 128, empty])
    call("ResError", [])
    probe.call("SetResAttrs", [slot(0x704), 4 | 2])
    call("ResError", [])
    call("DetachResourceFile", [slot(0x728)])
    call("DetachResourceFile", [slot(0x728)], -193)
    call("GetNextResourceFile", [slot(0x728), res_ref], -193)
    call("FSResourceFileAlreadyOpen", [other, chain, res_ref], 1)
    check(0x70C, 0, "the detached map keeps its open fork outside the active chain", 1)
    probe.call("HomeResFile", [slot(0x704)])
    p.emit(0x809E0000 | (base + 0x728))
    p.equal("a detached map continues to own its resource handle")
    call("InsertResourceFile", [slot(0x728), 5], -50)
    call("InsertResourceFile", [-1, 4], -193)
    call("InsertResourceFile", [slot(0x728), 4])
    call("GetTopResourceFile", [res_ref])
    p.emit(0xA07E0000 | (base + 0x710), 0xA09E0000 | (base + 0x72A))
    p.equal("reinserting above all maps sets the chain head")
    call("Count1Types", [], 1)

    # CurApRefNum and SysMap supply the original application and system chain anchors.
    for location, anchor_address in ((1, 0x900), (2, 0x900), (0, 0xA58)):
        p.emit(0xA8600000 | anchor_address, 0x907E0000 | (base + 0x72C))
        call("InsertResourceFile", [slot(0x728), location])
        if location == 2:
            call("GetNextResourceFile", [slot(0x728), res_ref])
            expected = 0x72E
        else:
            call("GetNextResourceFile", [slot(0x72C), res_ref])
            expected = 0x72A
        p.emit(0xA07E0000 | (base + 0x710), 0xA09E0000 | (base + expected))
        p.equal("insertion places the file next to its resource map anchor")

    call("DetachResourceFile", [slot(0x728)])
    probe.call("CloseResFile", [slot(0x728)])
    call("ResError", [])
    call("FSpOpenOrphanResFile", [alias_file, 1, res_ref])
    p.emit(0xA87E0000 | (base + 0x710), 0x907E0000 | (base + 0x728))
    call("FSpResourceFileAlreadyOpen", [alias_file, chain, res_ref], 1)
    check(0x70C, 0, "opening an orphan leaves it detached", 1)
    call("InsertResourceFile", [slot(0x728), 4])
    probe.call("SetResLoad", [0])
    probe.call("Get1Resource", [0x72736C66, 128])
    p.nonzero("closing a dirty detached map persists its resource")
    p.emit(0x80630000)
    p.nonzero("the orphan opener preloads resources before detaching")
    p.emit(0x80630000)
    p.expect(0x52534346, "the reattached orphan exposes the saved resource data")
    probe.call("SetResLoad", [1])
    probe.call("CloseResFile", [slot(0x728)])
    probe.call("UseResFile", [slot(0x724)])
    call("FSGetResourceForkName", [fork_name])
    call("FSOpenFork", [other, 13, ptr(0x4A2), 1, res_ref])
    p.emit(0xA87E0000 | (base + 0x710), 0x907E0000 | (base + 0x728))
    call("FSpResourceFileAlreadyOpen", [alias_file, chain, res_ref], 1)
    check(0x70C, 0, "a manually opened resource fork is outside the resource chain", 1)
    call("FSCloseFork", [slot(0x728)])
    call("FSDeleteObject", [other])

    call("FSCreateResourceFile", [folder, 13, resource_leaf, 0, 0, 0, 0, other, alias_file])
    call("FSCreateResourceFile", [folder, 13, resource_leaf, 0, 0, 0, 0, other, alias_file], -48)
    call("FSOpenResourceFile", [other, 0, 0, 3, res_ref])
    p.emit(0xA87E0000 | (base + 0x710), 0x907E0000 | (base + 0x728))
    call("Count1Types", [], 0)
    probe.call("NewHandle", [4])
    p.nonzero("allocate a data-fork resource")
    p.emit(0x907E0000 | (base + 0x704), 0x80630000, 0x3C005445, 0x60005854, 0x90030000)
    probe.call("AddResource", [slot(0x704), 0x54455354, 128, empty])
    call("ResError", [])
    probe.call("CloseResFile", [slot(0x728)])
    probe.call("UseResFile", [slot(0x724)])
    call("GetTopResourceFile", [ptr(0x704)])
    call("FSpOpenDataForkAsResourceFile", [alias_file, 1, res_ref])
    p.emit(0xA87E0000 | (base + 0x710), 0x907E0000 | (base + 0x728))
    call("GetTopResourceFile", [ptr(0x706)])
    p.emit(0xA07E0000 | (base + 0x706), 0xA09E0000 | (base + 0x72A))
    p.equal("GetTopResourceFile returns the newly opened data map")
    call("GetNextResourceFile", [slot(0x728), ptr(0x706)])
    p.emit(0xA07E0000 | (base + 0x706), 0xA09E0000 | (base + 0x704))
    p.equal("GetNextResourceFile advances to the previous top map")
    call("GetTopResourceFile", [0], -50)
    call("GetNextResourceFile", [slot(0x728), 0], -50)
    call("GetNextResourceFile", [-1, ptr(0x706)], -193)
    check(0x706, 0, "a missing map clears the next reference", 2)
    probe.call("Get1Resource", [0x54455354, 128])
    p.nonzero("reopen a persisted resource in a data fork")
    p.emit(0x80630000, 0x80630000)
    p.expect(0x54455854, "the persisted resource has the expected data")
    probe.call("CloseResFile", [slot(0x728)])
    probe.call("UseResFile", [slot(0x724)])
    call("FSDeleteObject", [other])

    data_name = probe.raw(b"\x0dData fork map")
    call("FSMakeFSSpec", [volume, directory_id, data_name, alias_file], -43)
    call("FSpOpenDataForkAsResourceFile", [alias_file, 1, res_ref], -43)
    check(0x710, 0xFFFF, "a failed data-fork open returns the invalid reference", 2)
    call("FSpOpenDataForkAsResourceFile", [alias_file, 1, 0], -50)
    call("FSpCreate", [alias_file, 0x3F3F3F3F, 0x3F3F3F3F, 0])
    call("FSpOpenDataForkAsResourceFile", [alias_file, 1, res_ref], -199)
    check(0x710, 0xFFFF, "an empty non-resource file does not become a resource map", 2)
    call("FSpCreateDataForkAsResourceFile", [alias_file])
    call("FSpDelete", [alias_file])
    call("FSpCreateDataForkAsResourceFile", [alias_file])
    call("FSpCreateDataForkAsResourceFile", [alias_file], -48)
    call("FSpOpenDataForkAsResourceFile", [alias_file, 3, res_ref])
    p.emit(0xA87E0000 | (base + 0x710), 0x907E0000 | (base + 0x728))
    call("Count1Types", [], 0)
    call("FSpOpenDataForkAsResourceFile", [alias_file, 3, res_ref])
    p.emit(0xA07E0000 | (base + 0x710), 0xA09E0000 | (base + 0x72A))
    p.equal("reopening the writable data map selects the existing map")
    pattern = b"\x80\x40\x20\x10\x08\x04\x02\x01"
    pattern_list = struct.pack(">H", 1) + pattern
    probe.call("NewHandle", [len(pattern_list)])
    p.nonzero("allocate a pattern-list resource")
    p.emit(0x907E0000 | (base + 0x704), 0x80630000, 0x907E0000 | (base + 0x70C))
    probe.call("BlockMoveData", [probe.raw(pattern_list), slot(0x70C), len(pattern_list)])
    probe.call("AddResource", [slot(0x704), 0x50415423, 177, empty])
    call("ResError", [])
    probe.call("GetIndPattern", [ptr(0x714), 177, 1])
    check(0x714, int.from_bytes(pattern[:4], "big"), "GetIndPattern reads the first four rows")
    check(0x718, int.from_bytes(pattern[4:], "big"), "GetIndPattern reads the last four rows")
    probe.call("CloseResFile", [slot(0x728)])
    probe.call("UseResFile", [slot(0x724)])
    call("FSpDelete", [alias_file])

    call("FSGetDataForkName", [fork_name])
    check(0x4A0, 0, "The data fork has the empty Unicode name", 2)
    call("FSOpenFork", [ref, 0, 0, 3, fork])
    p.emit(0xA87E0000 | (base + 0x6A0), 0x907E0000 | (base + 0x6A4))
    p.nonzero("FSOpenFork returns a usable fork reference")
    fork_ref = slot(0x6A4)
    call("GetVRefNum", [fork_ref, ptr(0x6B8)])
    call("FSWriteFork", [fork_ref, 1, 0, 0, 15, payload, actual])
    check(0x6B0, 15, "FSWriteFork writes the byte count")
    call("FSGetForkPosition", [fork_ref, position])
    check(0x6A8, 0, "Fork position writes a zero high word")
    check(0x6AC, 15, "Fork position writes the position low word")
    call("FSSetForkSize", [fork_ref, 1, 0, 12])
    call("FSGetForkSize", [fork_ref, position])
    check(0x6AC, 12, "FSGetForkSize reports the truncated size")
    call("FSGetForkSize", [fork_ref, position], saved=True)
    check(0x6AC, 12, "Saved fork-size calls read the same open file")
    call("FSSetForkPosition", [fork_ref, 2, -1, -4])
    call("FSReadFork", [fork_ref, 0, 0, 0, 8, ptr(0x6C0), actual], -39)
    check(0x6B0, 4, "Short fork reads report their byte count")
    check(0x6C0, 0x6520666F, "Short fork reads preserve the available data")
    call("FSGetForkCBInfo", [fork_ref, 0, 0, 0, info, other, name])
    call("FSCompareFSRefs", [ref, other])
    call("FSAllocateFork", [fork_ref, 8, 2, 0, 0, 0, 1, position], -1413)
    call("FSFlushFork", [fork_ref])
    for stem, fields, expected in (
        ("PBReadFork", ((32, ptr(0x6C0), 4), (36, 4, 4), (44, 1, 2)), 0),
        ("PBWriteFork", ((32, payload, 4), (36, 4, 4), (44, 1, 2)), 0),
        ("PBGetForkPosition", (), 0),
        ("PBSetForkPosition", ((44, 1, 2),), 0),
        ("PBGetForkSize", (), 0),
        ("PBSetForkSize", ((44, 1, 2), (50, 12, 4)), 0),
        ("PBAllocateFork", ((44, 2, 2), (54, 8, 2)), -1413),
        ("PBFlushFork", (), 0),
    ):
        pb(stem, ((24, fork_ref, 2), *fields), expected)
    pb("PBGetForkCBInfo", ((18, fork_ref, 2), (26, other, 4), (30, info, 4), (34, name, 4)))
    pb("PBAllocContig", ((24, fork_ref, 2),))
    call("FSCloseFork", [fork_ref])

    call("FSGetResourceForkName", [fork_name])
    check(0x4A0, 13, "The resource fork uses the CarbonLib name", 2)
    call("FSCreateFork", [ref, 0, 0], -1421)
    pb("PBCreateFork", ((28, ref, 4),), -1421)
    call("FSOpenFork", [ref, 13, ptr(0x4A2), 3, fork])
    p.emit(0xA87E0000 | (base + 0x6A0), 0x907E0000 | (base + 0x6A4))
    call("FSWriteFork", [fork_ref, 1, 0, 0, 4, payload, actual])
    call("FSCloseFork", [fork_ref])
    call("FSIterateForks", [ref, ptr(0x6D0), name, position, 0])
    call("FSIterateForks", [ref, ptr(0x6D0), name, position, 0])
    check(0x6AC, 4, "Fork iteration reports the resource fork length")
    pb("PBIterateForks", ((28, ref, 4), (88, name, 4)))
    call("FSIterateForks", [ref, ptr(0x6D0), name, position, 0], -1417)
    call("FSDeleteFork", [ref, 13, ptr(0x4A2)])
    pb("PBDeleteFork", ((28, ref, 4), (64, 13, 4), (68, ptr(0x4A2), 4)))
    for mode in ("Sync", "Async"):
        pb("PBOpenFork", ((27, 1, 1), (28, ref, 4)), modes=(mode,))
        p.emit(0xA87E0000 | (base + 0x758), 0x907E0000 | (base + 0x6A4))
        pb("PBCloseFork", ((24, fork_ref, 2),), modes=(mode,))

    call("FSOpenIterator", [folder, 0, cursor])
    call("FSGetCatalogInfoBulk", [slot(0x6B4), 1, actual, 0, 0x10, info, other, spec, name])
    check(0x6B0, 1, "Flat catalog iteration returns the child")
    call("FSCompareFSRefs", [ref, other])
    call("FSGetCatalogInfoBulk", [slot(0x6B4), 1, actual, 0, 0, 0, 0, 0, 0], -1417)
    call("FSCloseIterator", [slot(0x6B4)])
    for mode in ("Sync", "Async"):
        pb("PBOpenIterator", ((28, folder, 4),), modes=(mode,))
        put(0x6B4, slot(0x758))
        pb("PBGetCatalogInfoBulk", ((24, slot(0x6B4), 4), (32, 1, 4), (56, name, 4)), modes=(mode,))
        pb("PBCloseIterator", ((24, slot(0x6B4), 4),), modes=(mode,))

    criteria = ptr(0x6C0)
    for offset in range(0x6C0, 0x6D8, 4):
        put(offset, 0)
    call("FSGetVolumeInfo", [volume, 0, 0, 0, 0, 0, resolved])
    call("FSOpenIterator", [resolved, 1, cursor])
    call("FSGetCatalogInfoBulk", [slot(0x6B4), 1, actual, 0, 0, 0, 0, 0, 0], -1424)
    call("FSCatalogSearch", [slot(0x6B4), 0, 1, actual, 0, 0, 0, 0, 0, 0], -1419)
    call("FSCatalogSearch", [slot(0x6B4), criteria, 1, actual, 0, 0x10, info, 0, spec, name])
    check(0x6B0, 1, "A catalog search returns its first volume entry")
    p.emit(0x807E0000 | (base + 0x208))
    p.nonzero("A catalog search reports the matching node ID")
    call("FSCloseIterator", [slot(0x6B4)])
    for at, value in ((4, 0x8000), (16, info), (20, info)):
        put(0x6C0 + at, value)
    for mode in ("Sync", "Async"):
        call("FSOpenIterator", [resolved, 1, cursor])
        pb(
            "PBCatalogSearch",
            ((24, slot(0x6B4), 4), (32, 1, 4), (40, 0x10, 4), (44, name, 4), (60, criteria, 4)),
            modes=(mode,),
        )
        check(0x764, 1, "PBCatalogSearch publishes the actual match count")
        p.emit(0x807E0000 | (base + 0x2A8), 0x809E0000 | (base + 0x208))
        p.equal("PBCatalogSearch applies the requested node ID range")
        call("FSCloseIterator", [slot(0x6B4)])

    call("FSRenameUnicode", rename_args)
    call("FSGetCatalogInfo", [ref, 0, 0, name, 0, 0])
    check(0x2A0, 7, "A file reference survives a same-folder rename", 2)
    call("FSMoveObject", [other, container, resolved])
    call("FSCompareFSRefs", [ref, resolved])
    call("FSRefMakePath", [ref, path_buffer, 512])
    call("FSPathMakeRef", [path_buffer, other, 0])
    call("FSCompareFSRefs", [resolved, other])
    call("FSGetCatalogInfo", [ref, 0, 0, name, 0, other])
    call("FSCompareFSRefs", [container, other])
    call("FSDeleteObject", [resolved])
    call("FSDeleteObject", [folder])

    # Dispatch both native parameter block variants for validation paths in the
    # same application. A failed request must still call its completion.
    for stem in (
        "PBCreateFileUnicode",
        "PBCreateDirectoryUnicode",
        "PBRenameUnicode",
        "PBMoveObject",
        "PBDeleteObject",
    ):
        pb(stem, (), -1401)
    call("CatMove", [volume, directory_id, probe.raw(b"\x14Missing Catalog Leaf"), directory_id, 0], -43)


AE_DESCRIPTOR_ORACLES = ("AECheckIsRecord", "AESizeOfFlattenedDesc", "AEFlattenDesc", "AEUnflattenDesc")
AE_STREAM_NAMES = (
    "AEStreamOpen",
    "AEStreamClose",
    "AEStreamWriteData",
    "AEStreamWriteKey",
    "AEStreamOpenDesc",
    "AEStreamOpenKeyDesc",
    "AEStreamCloseDesc",
    "AEStreamOpenRecord",
    "AEStreamSetRecordType",
    "AEStreamCloseRecord",
    "AEStreamOpenList",
    "AEStreamCloseList",
    "AEStreamWriteDesc",
    "AEStreamWriteKeyDesc",
    "AEStreamWriteAEDesc",
    "AEStreamOpenEvent",
    "AEStreamCreateEvent",
    "AEStreamOptionalParam",
)


AE_BUILD_NAMES = (
    "AEBuildDesc",
    "vAEBuildDesc",
    "AEBuildParameters",
    "vAEBuildParameters",
    "AEBuildAppleEvent",
    "vAEBuildAppleEvent",
)

AE_OBJECT_PACKING = (
    "CreateCompDescriptor",
    "CreateLogicalDescriptor",
    "CreateObjSpecifier",
    "CreateOffsetDescriptor",
    "CreateRangeDescriptor",
)


def apple_event_printer_operations(probe):
    p = probe.p
    _, base = probe.raw(bytes(72))
    ptr = lambda at: probe.ptr(base + at)
    slot = lambda at: probe.slot(base + at)
    original, result, rebuilt, payload, length = (ptr(at) for at in (0, 8, 12, 44, 56))
    text_type, wildcard = 0x54455854, 0x2A2A2A2A

    def call(name, arguments, status=0):
        probe.call(name, arguments)
        p.expect(status, name + " status")

    def inspect_word(at, expected, label):
        p.emit(0x807E0000 | (base + at))
        p.expect(expected, label)

    def print_descriptor():
        call("AEPrintDescToHandle", [original, result])
        probe.call("GetHandleSize", [slot(8)])
        p.nonzero("the printer returns a nonempty handle")
        p.emit(0x907E0000 | (base + 36))
        p.emit(0x819E0000 | (base + 8), 0x818C0000, 0x919E0000 | (base + 40))

    def round_trip():
        print_descriptor()
        buffer = probe.far_raw(bytes(256))
        probe.call("BlockMoveData", [slot(40), buffer, slot(36)])
        call("AEBuildDesc", [rebuilt, 0, buffer])
        probe.call("DisposeHandle", [slot(8)])

    source = probe.far_raw(b"test")
    call("AECreateDesc", [text_type, source, 4, original])
    call("AEPrintDescToHandle", [original, 0], -50)
    print_descriptor()
    inspect_word(36, 6, "the text handle excludes a trailing NUL")
    p.emit(0x819E0000 | (base + 40), 0x806C0000)
    p.expect(0x22746573, "plain text is enclosed in quotes")
    p.emit(0x819E0000 | (base + 40), 0xA06C0004)
    p.expect(0x7422, "plain text retains the closing quote")
    probe.call("DisposeHandle", [slot(8)])
    call("AEDisposeDesc", [original])

    binary = probe.far_raw(b'a"\0z')
    call("AECreateDesc", [text_type, binary, 4, original])
    round_trip()
    call("AEGetDescData", [rebuilt, payload, 4])
    inspect_word(44, 0x6122007A, "quoted and zero bytes survive hexadecimal fallback")
    call("AEDisposeDesc", [rebuilt])
    call("AEDisposeDesc", [original])

    nested = probe.far_raw(b'[long(44), demo{item:"value"}]\0')
    call("AEBuildDesc", [original, 0, nested])
    round_trip()
    call("AECountItems", [rebuilt, length])
    inspect_word(56, 2, "printed lists retain every nested item")
    call("AEGetNthPtr", [rebuilt, 1, wildcard, ptr(60), ptr(52), payload, 8, length])
    inspect_word(52, 0x6C6F6E67, "the printed numeric syntax preserves a long descriptor's width")
    inspect_word(44, 44, "the long descriptor survives the print and rebuild")
    call("AEGetNthDesc", [rebuilt, 2, wildcard, ptr(60), ptr(20)])
    inspect_word(20, 0x64656D6F, "the printed record syntax preserves a custom record type")
    call("AECheckIsRecord", [ptr(20)], 1)
    call("AEDisposeDesc", [ptr(20)])
    call("AEDisposeDesc", [rebuilt])
    call("AEDisposeDesc", [original])

    target = probe.far_raw(struct.pack(">2I", 0, 2))
    event_format = probe.far_raw(b'text:"event"\0')
    call("AEBuildAppleEvent", [0x54455354, 0x50524E54, 0x70736E20, target, 8, -1, 0, original, 0, event_format])
    print_descriptor()
    p.emit(0x819E0000 | (base + 40), 0x806C0000)
    p.expect(0x27544553, "an event print starts with the event class and ID")
    probe.call("DisposeHandle", [slot(8)])
    call("AEDisposeDesc", [original])


def apple_event_array_operations(probe):
    p = probe.p
    _, base = probe.raw(bytes(128))
    ptr = lambda at: probe.ptr(base + at)
    slot = lambda at: probe.slot(base + at)
    listing, record = ptr(0), ptr(8)
    type_out, size_out, count_out, output = (ptr(at) for at in (16, 20, 24, 32))
    text, integer, wildcard = 0x54455854, 0x6C6F6E67, 0x2A2A2A2A

    def call(name, arguments, status=0):
        probe.call(name, arguments)
        p.expect(status, name + " status")

    def check(at, value, label):
        p.emit(0x807E0000 | (base + at))
        p.expect(value, label)

    def get(list_desc, representation, limit):
        call("AEGetArray", [list_desc, representation, output, limit, type_out, size_out, count_out])

    call("AECreateList", [0, 0, 0, listing])
    packed = probe.far_raw(b"abcdef")
    aligned = probe.far_raw(b"abc!def?")
    for representation, source in ((1, packed), (0, aligned)):
        call("AEPutArray", [listing, representation, source, text, 3, 2])
        get(listing, 0, 8)
        check(16, text, "uniform arrays report their descriptor type")
        check(20, 3, "uniform arrays report their unpadded item size")
        check(24, 2, "array replacement retains exactly two items")
        check(32, 0x61626300, "word-aligned output pads the first item")
        check(36, 0x64656600, "word-aligned output pads the second item")
        get(listing, 1, 3)
        check(24, 1, "a three-byte output buffer receives one complete packed item")
    call("AEGetArray", [listing, 3, output, -1, 0, 0, count_out], -50)

    first, second = ptr(80), ptr(88)
    call("AECreateDesc", [text, packed, 3, first])
    call("AECreateDesc", [text, packed, 5, second])
    p.emit(0x817E0000 | (base + 84), 0x917E0000 | (base + 64))
    p.emit(0x817E0000 | (base + 92), 0x917E0000 | (base + 68))
    call("AEPutArray", [listing, 2, ptr(64), text, 0, 2])
    call("AEDisposeDesc", [first])
    call("AEDisposeDesc", [second])
    get(listing, 2, 8)
    check(24, 2, "the handle array returns both payloads")
    for offset, length in ((32, 3), (36, 5)):
        probe.call("GetHandleSize", [slot(offset)])
        p.expect(length, "handle arrays preserve variable payload lengths")
        probe.call("DisposeHandle", [slot(offset)])
    call("AEGetArray", [listing, 1, output, 16, type_out, size_out, count_out], -1703)

    call("AECreateDesc", [text, packed, 3, first])
    call("AECreateDesc", [integer, probe.far_raw(struct.pack(">I", 729)), 4, second])
    call("AEPutArray", [listing, 3, first, 0, 0, 2])
    call("AEDisposeDesc", [first])
    call("AEDisposeDesc", [second])
    get(listing, 3, 16)
    check(32, text, "descriptor arrays return the first item's type")
    check(40, integer, "descriptor arrays retain heterogeneous types")
    call("AEGetDescData", [ptr(40), ptr(72), 4])
    check(72, 729, "descriptor array data survives disposal of the source descriptors")
    call("AEDisposeDesc", [ptr(32)])
    call("AEDisposeDesc", [ptr(40)])

    call("AECreateList", [0, 0, 1, record])
    p.emit(0x3C606F6E, 0x60636520, 0x907E0000 | (base + 96))
    p.emit(0x3C607477, 0x60636F20, 0x907E0000 | (base + 108))
    call("AECreateDesc", [text, packed, 2, ptr(100)])
    call("AECreateDesc", [text, packed, 5, ptr(112)])
    call("AEPutArray", [record, 4, ptr(96), 0, 0, 2])
    call("AEDisposeDesc", [ptr(100)])
    call("AEDisposeDesc", [ptr(112)])
    get(record, 4, 24)
    check(24, 2, "keyword arrays preserve their record length")
    check(32, 0x6F6E6520, "the first keyword remains in guest byte order")
    check(44, 0x74776F20, "the second keyword remains in guest byte order")
    call("AEGetNthPtr", [record, 2, wildcard, ptr(64), type_out, ptr(72), 4, size_out])
    check(72, 0x61626364, "normal record access reads the replacement array")
    call("AEDisposeDesc", [ptr(36)])
    call("AEDisposeDesc", [ptr(48)])
    call("AEPutArray", [record, 4, 0, 0, 0, 0])
    call("AECountItems", [record, count_out])
    check(24, 0, "a zero-length replacement clears the record")
    call("AEDisposeDesc", [record])
    call("AEDisposeDesc", [listing])


def apple_event_object_packing(probe):
    p = probe.p
    _, base = probe.raw(bytes(80))
    ptr = lambda at: probe.ptr(base + at)
    first, last, terms, output, field = (ptr(at) for at in (0, 8, 16, 24, 32))
    keyword, dtype, value, size = (ptr(at) for at in (40, 44, 48, 52))
    fourcc = lambda code: int.from_bytes(code, "big")
    record_view = probe.far_raw(struct.pack(">4I", 0, 0, 24, fourcc(b"reco")) + bytes(256))
    list_view = probe.far_raw(struct.pack(">4I", 0, 0, 24, fourcc(b"list")) + bytes(256))

    def word(at, expected, label):
        p.emit(0x807E0000 | (base + at))
        p.expect(expected, label)

    def call(name, args, side=0):
        probe.call(name, args, side=side)
        p.expect(0, name + " succeeds")

    call("AEObjectInit", [])
    call("AESetObjectCallbacks", [0, 0, 0, 0, 0, 0, 0])

    def expand_compact(at, kind, view):
        # Add the classic manager header to the original stream's count,
        # prefix size and entries so the same accessors can inspect them.
        probe.call("GetHandleSize", [probe.slot(base + at + 4)])
        p.emit(0x38830010, 0x909E0000 | (base + 76), 0x5463C23E)
        p.expect(0, "the original packed data fits the comparison buffer")
        descriptor = ptr(at)
        call("AEGetDescData", [descriptor, probe.ptr(view[1] + 16), 256])
        call("AEDisposeDesc", [descriptor])
        call("AECreateDesc", [fourcc(kind), view, probe.slot(base + 76), descriptor])

    def record(code, count, side):
        word(24, fourcc(code), "object packing uses the expected outer descriptor type")
        if side:
            expand_compact(24, b"reco", record_view)
        else:
            probe.arguments([fourcc(b"reco")], 0)
            p.emit(0x907E0000 | (base + 24))
        call("AECountItems", [output, size])
        word(52, count, "the object record contains every field")

    def scalar(index, name, type, expected):
        call("AEGetNthPtr", [output, index, fourcc(b"****"), keyword, dtype, value, 4, size])
        word(40, fourcc(name), "the object field has the expected keyword")
        word(44, fourcc(type), "the object field has the expected descriptor type")
        word(48, expected, "the object field preserves its scalar payload")

    def save_backing(at, saved):
        p.emit(
            0x807E0000 | (base + at + 4),
            0x907E0000 | (base + saved),
            0x80630000,
            0x907E0000 | (base + saved + 4),
        )

    def backing_released(saved, label):
        # AEDisposeDesc in ROMAW releases the handle and leaves the descriptor words in place.
        p.emit(0x807E0000 | (base + saved), 0x80630000, 0x809E0000 | (base + saved + 4), 0x7C632278)
        p.nonzero(label)

    for side in (0, 1):
        call("CreateOffsetDescriptor", [-70001, first], side=side)
        call("CreateOffsetDescriptor", [7, last], side=side)
        word(0, fourcc(b"long"), "offset descriptors use signed 32-bit integers")
        call("CreateRangeDescriptor", [first, last, 0, output], side=side)
        record(b"rang", 2, side)
        scalar(1, b"star", b"long", -70001)
        scalar(2, b"stop", b"long", 7)
        word(0, fourcc(b"long"), "range creation can retain its inputs")
        call("AEDisposeDesc", [output])

        save_backing(0, 56)
        save_backing(8, 64)
        call("CreateCompDescriptor", [fourcc(b"=   "), first, last, 1, output], side=side)
        for saved in (56, 64):
            backing_released(saved, "comparison creation releases each input when requested")
        record(b"cmpd", 3, side)
        scalar(1, b"relo", b"enum", fourcc(b"=   "))
        scalar(2, b"obj1", b"long", -70001)
        scalar(3, b"obj2", b"long", 7)
        call("AEDisposeDesc", [output])
        call("CreateOffsetDescriptor", [7, last], side=side)
        call("AECreateList", [0, 0, 0, terms])
        call("AEPutDesc", [terms, 0, last])
        call("AEDisposeDesc", [last])
        save_backing(16, 56)
        call("CreateLogicalDescriptor", [terms, fourcc(b"AND "), 1, output], side=side)
        backing_released(56, "logical creation releases its original term list")
        record(b"logi", 2, side)
        scalar(2, b"logc", b"enum", fourcc(b"AND "))
        call("AEGetNthDesc", [output, 1, fourcc(b"****"), keyword, field])
        word(40, fourcc(b"term"), "the logical record contains its term list")
        if side:
            expand_compact(32, b"list", list_view)
        call("AECountItems", [field, size])
        word(52, 1, "the logical record retains the copied term")
        call("AEGetNthPtr", [field, 1, fourcc(b"****"), keyword, dtype, value, 4, size])
        word(48, 7, "the logical term retains its value")
        call("AEDisposeDesc", [field])
        call("AEDisposeDesc", [output])

        call("AECreateDesc", [fourcc(b"null"), 0, 0, first])
        call("CreateOffsetDescriptor", [9, last], side=side)
        save_backing(8, 64)
        call("CreateObjSpecifier", [fourcc(b"docu"), first, fourcc(b"indx"), last, 1, output], side=side)
        backing_released(64, "object creation releases its input key descriptor")
        record(b"obj ", 4, side)
        scalar(1, b"want", b"type", fourcc(b"docu"))
        call("AEGetNthDesc", [output, 2, fourcc(b"****"), keyword, field])
        word(40, fourcc(b"from"), "the object preserves the container keyword")
        word(32, fourcc(b"null"), "a null container is serialized")
        scalar(3, b"form", b"enum", fourcc(b"indx"))
        scalar(4, b"seld", b"long", 9)
        call("AEDisposeDesc", [output])


def apple_event_builder_operations(probe):
    p = probe.p
    _, base = probe.raw(bytes(80))
    ptr = lambda at: probe.ptr(base + at)
    slot = lambda at: probe.slot(base + at)
    result, error, output, dtype, length = (ptr(at) for at in (0, 8, 16, 24, 28))
    text_type, long_type, wildcard = 0x54455854, 0x6C6F6E67, 0x2A2A2A2A
    text, tail, optional = 0x74657874, 0x7461696C, 0x6F70746B

    def expect_word(at, value, label):
        p.emit(0x807E0000 | (base + at))
        p.expect(value, label)

    def format_string(value):
        return probe.far_raw(value + b"\0")

    def call(name, arguments, status=0, side=0):
        probe.call(name, arguments, side)
        p.expect(status, name + " returns the expected status")

    def check_scalar(type, value):
        expect_word(0, type, "the builder returns the requested descriptor type")
        call("AEGetDescData", [result, output, 4])
        expect_word(16, value, "the builder emits bytes in guest order")
        call("AEDisposeDesc", [result])

    quoted = format_string(b'"test"')
    for side in (1, 0):
        call("AEBuildDesc", [result, error, quoted], side=side)
        expect_word(8, 0, "a successful build clears the syntax error")
        check_scalar(text_type, 0x74657374)

    call("AEBuildDesc", [result, error, format_string(b"blob($ 01 fE 23 45 $)")])
    check_scalar(0x626C6F62, 0x01FE2345)
    call("AEBuildDesc", [result, error, format_string(b"long(@)"), -70001])
    check_scalar(long_type, -70001)
    p.emit(0x38600654, 0x907E0000 | (base + 32))
    va_words = ptr(32)
    call("vAEBuildDesc", [result, error, format_string(b"long(@)"), va_words])
    check_scalar(long_type, 0x654)

    payload = probe.far_raw(b"data")
    call("AEBuildDesc", [result, error, format_string(b"rawb(@)"), 4, payload])
    check_scalar(0x72617762, 0x64617461)
    source = ptr(36)
    call("AECreateDesc", [text_type, payload, 4, source])
    call("AEBuildDesc", [result, error, format_string(b"blob(@@)"), slot(40)])
    check_scalar(0x626C6F62, 0x64617461)
    call("AEBuildDesc", [result, error, format_string(b"@"), source])
    call("AEDisposeDesc", [source])
    check_scalar(text_type, 0x64617461)

    call("AEBuildDesc", [result, error, format_string(b'[1, -40000, tst{unit:"last"}]')])
    call("AECountItems", [result, length])
    expect_word(28, 3, "lists include every nested descriptor")
    call("AEGetNthPtr", [result, 2, wildcard, ptr(44), dtype, output, 4, length])
    expect_word(16, -40000, "negative literals select a signed wide descriptor")
    expect_word(24, long_type, "the long descriptor type survives list insertion")
    call("AEGetNthDesc", [result, 3, wildcard, ptr(44), source])
    expect_word(36, 0x74737420, "a short type code pads with a space")
    call("AECheckIsRecord", [source], 1)
    call("AEDisposeDesc", [source])
    call("AEDisposeDesc", [result])

    for invalid, code in ((b"[1 2]", 14), (b'"abc', 10), (b"blob($a$)", 7), (b"{key 1}", 17)):
        call("AEBuildDesc", [result, error, format_string(invalid)], -1740)
        expect_word(8, code, "syntax failures identify the malformed token")
        expect_word(0, 0x6E756C6C, "syntax failures produce a null descriptor")
        expect_word(4, 0, "syntax failures release any partially built result")

    target = probe.far_raw(struct.pack(">2I", 0, 2))
    event_arguments = [0x54455354, 0x4255494C, 0x70736E20, target, 8, -1, 0, result, error]
    call("AEBuildAppleEvent", [*event_arguments, format_string(b"~text:long(@)"), 171])
    call("AEGetParamPtr", [result, text, wildcard, dtype, output, 4, length])
    expect_word(16, 171, "AppleEvent variadics read their stack-based argument")
    call("AEGetAttributeDesc", [result, optional, 0x6C697374, source])
    call("AECountItems", [source, length])
    expect_word(28, 1, "the optional event keyword is published")
    call("AEDisposeDesc", [source])
    call("AEBuildParameters", [result, error, format_string(b"tail:TEXT(@)"), format_string(b"more")])
    call("AEGetParamPtr", [result, tail, wildcard, dtype, output, 4, length])
    expect_word(16, 0x6D6F7265, "a parameter build augments a live event")

    p.emit(0x38600009, 0x907E0000 | (base + 32))
    call("vAEBuildParameters", [result, error, format_string(b"tail:long(@)"), va_words])
    call("AEGetParamPtr", [result, tail, wildcard, dtype, output, 4, length])
    expect_word(16, 9, "the indirect parameter builder updates the target keyword")
    call("AEBuildParameters", [result, error, format_string(b"tail:long(1), bad")], -1740)
    call("AEGetParamPtr", [result, tail, wildcard, dtype, output, 4, length])
    expect_word(16, 9, "a failed parameter build preserves the original event")
    call("AEDisposeDesc", [result])

    call("vAEBuildAppleEvent", [*event_arguments, format_string(b"text:long(@)"), va_words])
    call("AEGetParamPtr", [result, text, wildcard, dtype, output, 4, length])
    expect_word(16, 9, "the indirect event builder reads the guest va_list")
    call("AEDisposeDesc", [result])


def apple_event_stream_operations(probe):
    p = probe.p
    _, base = probe.raw(bytes(72))
    ptr = lambda n: probe.ptr(base + n)
    slot = lambda n: probe.slot(base + n)
    stream, result, temporary, attribute = slot(0), ptr(8), ptr(16), ptr(24)
    output, length, dtype = ptr(32), ptr(40), ptr(44)
    text, tail = probe.far_raw(b"abc"), probe.far_raw(b"de")
    text_type, key, second_key, wildcard = 0x54455854, 0x74657874, 0x7461696C, 0x2A2A2A2A

    def call(name, args, status=0, side=0):
        probe.call(name, args, side=side)
        p.expect(status, name + " status")

    def expect_word(offset, value, description):
        p.emit(0x807E0000 | (base + offset))
        p.expect(value, description)

    def open_stream(name="AEStreamOpen", args=(), side=0):
        probe.call(name, args, side=side)
        p.nonzero(name + " returns a live stream")
        p.emit(0x907E0000 | base)

    open_stream()
    call("AEStreamWriteData", [stream, text, 3], -1737)
    call("AEStreamOpenDesc", [stream, 0x6C697374], -1703)
    call("AEStreamOpenDesc", [stream, text_type])
    call("AEStreamWriteData", [stream, text, 3])
    call("AEStreamWriteData", [stream, tail, 2])
    call("AEStreamCloseList", [stream], -1737)
    call("AEStreamCloseDesc", [stream])
    call("AEStreamClose", [stream, result])
    expect_word(8, text_type, "a scalar stream preserves its type")
    call("AEGetDescData", [result, output, 5])
    expect_word(32, 0x61626364, "separate data writes concatenate")
    p.emit(0x887E0000 | (base + 36))
    p.expect(ord("e"), "scalar writes preserve the last byte")
    call("AEDisposeDesc", [result])

    open_stream()
    call("AEStreamOpenList", [stream])
    call("AEStreamWriteDesc", [stream, text_type, text, 3])
    call("AECreateDesc", [text_type, tail, 2, temporary])
    call("AEStreamWriteAEDesc", [stream, temporary])
    call("AEDisposeDesc", [temporary])
    call("AEStreamOpenRecord", [stream, 0x7265636F])
    call("AEStreamSetRecordType", [stream, 0x54455354])
    call("AEStreamOpenKeyDesc", [stream, key, text_type])
    call("AEStreamWriteData", [stream, text, 3])
    call("AEStreamCloseDesc", [stream])
    call("AEStreamWriteKeyDesc", [stream, second_key, text_type, tail, 2])
    call("AEStreamCloseRecord", [stream])
    call("AEStreamCloseList", [stream])
    call("AEStreamClose", [stream, result])
    call("AECountItems", [result, length])
    expect_word(40, 3, "the list owns both scalars and the nested record")
    for index, first_byte, size in ((1, ord("a"), 3), (2, ord("d"), 2)):
        call("AEGetNthPtr", [result, index, wildcard, ptr(48), dtype, output, 8, length])
        expect_word(40, size, "a stream list preserves exact item lengths")
        p.emit(0x887E0000 | (base + 32))
        p.expect(first_byte, "the appended list item owns a copy of its bytes")
    call("AEGetNthDesc", [result, 3, wildcard, ptr(48), temporary])
    expect_word(16, 0x54455354, "nested records preserve the changed record type")
    call("AECheckIsRecord", [temporary], 1)
    call("AEDisposeDesc", [temporary])
    call("AEDisposeDesc", [result])

    target = probe.far_raw(struct.pack(">2I", 0, 2))
    open_stream("AEStreamCreateEvent", [0x54455354, 0x5354524D, 0x70736E20, target, 8, -1, 0])
    call("AEStreamWriteKey", [stream, key])
    call("AEStreamWriteDesc", [stream, text_type, text, 3])
    call("AEStreamOptionalParam", [stream, key])
    call("AEStreamClose", [stream, result])
    open_stream("AEStreamOpenEvent", [result])
    expect_word(8, 0x6E756C6C, "opening an existing event consumes its descriptor")
    expect_word(12, 0, "opening an existing event clears its handle")
    call("AEStreamWriteKeyDesc", [stream, second_key, text_type, tail, 2])
    call("AEStreamOptionalParam", [stream, second_key])
    call("AEStreamClose", [stream, result])
    for parameter, size in ((key, 3), (second_key, 2)):
        call("AEGetParamPtr", [result, parameter, wildcard, dtype, output, 8, length])
        expect_word(40, size, "reopening an event keeps the existing parameters")
    call("AEGetAttributeDesc", [result, 0x6F70746B, 0x6C697374, attribute])
    call("AECountItems", [attribute, length])
    expect_word(40, 2, "optional parameters survive reopening the event")
    call("AEGetNthPtr", [attribute, 2, 0x6B657977, ptr(48), dtype, output, 4, length])
    expect_word(32, second_key, "the optional parameter list stores keyword descriptors")
    call("AEDisposeDesc", [attribute])
    call("AEDisposeDesc", [result])

    open_stream()
    call("AEStreamOptionalParam", [stream, key], -1707)
    call("AEStreamOpenRecord", [stream, 0x7265636F])
    call("AEStreamWriteKey", [stream, key])
    call("AEStreamCloseRecord", [stream], -1737)
    call("AEStreamClose", [stream, result], -1737)
    expect_word(8, 0x6E756C6C, "an unfinished stream returns a null descriptor")
    open_stream()
    call("AEStreamOpenList", [stream])
    call("AEStreamClose", [stream, 0])

    open_stream(side=1)
    call("AEStreamWriteDesc", [stream, text_type, text, 3])
    call("AEStreamClose", [stream, result])
    call("AEGetDescData", [result, output, 3])
    p.emit(0x887E0000 | (base + 32))
    p.expect(ord("a"), "a stream allocated by saved CarbonLib retains its original dispatch")
    call("AEDisposeDesc", [result])


def dictionary_field_operations(probe):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    null_desc = b"null" + bytes(4)
    _, base = probe.far_raw(null_desc * 4 + bytes([0xA5]) * 32)
    example = probe.far_raw(b"yomi")
    methods = probe.far_raw(b"=   bgwt")
    field_tag = int.from_bytes(b"yomi", "big")
    field_type = int.from_bytes(b"TEXT", "big")
    wildcard = int.from_bytes(b"****", "big")

    def check(name, arguments, result=0):
        probe.call(name, arguments)
        p.expect(result, name + " reads or writes a Dictionary Manager field descriptor")

    def value(offset, expected, description):
        probe.arguments([slot(base + offset)], 0)
        p.expect(expected, description)

    check("AECreateDesc", [field_type, example, 4, ptr(base)])
    check("DCMCreateFieldInfoRecord", [field_tag, field_type, 40, 0x80000003, ptr(base), 2, methods, ptr(base + 8)])
    value(8, int.from_bytes(b"reco", "big"), "a field information record is an Apple Event record")
    check("DCMGetFieldTagAndType", [ptr(base + 8), ptr(base + 32), ptr(base + 36)])
    value(32, field_tag, "the field tag is stored in guest order")
    value(36, field_type, "the field type is stored independently from the tag")
    check("DCMGetFieldMaxRecordSize", [ptr(base + 8), ptr(base + 40)])
    value(40, 40, "return the maximum record size")
    check("DCMGetFieldAttributes", [ptr(base + 8), ptr(base + 40)])
    value(40, 0x80000003, "preserve the complete field attribute word")
    check("DCMGetFieldDefaultData", [ptr(base + 8), wildcard, ptr(base + 16)])
    check("AEDisposeDesc", [ptr(base)])
    check("AEGetDescData", [ptr(base + 16), ptr(base + 44), 4])
    value(44, field_tag, "the caller owns an independent copy of the default value")
    check("AEDisposeDesc", [ptr(base + 16)])

    check("DCMGetFieldFindMethods", [ptr(base + 8), 0, 0, ptr(base + 48)])
    value(48, 2, "query the number of search methods without a destination")
    check("DCMGetFieldFindMethods", [ptr(base + 8), 1, ptr(base + 52), ptr(base + 48)], -7127)
    value(52, int.from_bytes(b"=   ", "big"), "copy only the search method the guest array can hold")
    value(56, 0xA5A5A5A5, "a short guest array preserves its trailing sentinel")
    check("DCMGetFieldFindMethods", [ptr(base + 8), 2, ptr(base + 52), ptr(base + 48)])
    value(56, int.from_bytes(b"bgwt", "big"), "return the second search method in declaration order")
    check("AEGetParamDesc", [ptr(base + 8), int.from_bytes(b"ffnd", "big"), wildcard, ptr(base + 24)])
    check("AECountItems", [ptr(base + 24), ptr(base + 48)])
    value(48, 2, "AppleEvent Manager can enumerate the published search method list")
    check("AEDisposeDesc", [ptr(base + 24)])

    check("DCMCreateFieldInfoRecord", [field_tag, field_type, 64, 1, 0, 0, 0, ptr(base + 8)])
    check("DCMGetFieldMaxRecordSize", [ptr(base + 8), ptr(base + 40)])
    value(40, 64, "rebuilding the record replaces the prior field")
    check("DCMGetFieldDefaultData", [ptr(base + 8), wildcard, ptr(base + 16)], -7113)
    check("AEDisposeDesc", [ptr(base + 8)])


def apple_event_operations(probe):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    text = probe.raw(b"data")
    event_class, event_id = 0x54455354, 0x4652414D  # TEST/FRAM
    text_type, wildcard, key = 0x54455854, 0x2A2A2A2A, 0x74657874  # TEXT/****/text

    def call(name, arguments, expected=0):
        probe.call(name, arguments)
        p.expect(expected, name + " returns the expected installed Apple Event result")

    descriptor, listing, target, event = (ptr(offset) for offset in (0x1E80, 0x1E88, 0x1E90, 0x1E98))
    output, length, dtype, keyword = (ptr(offset) for offset in (0x1EA0, 0x1EA8, 0x1EAC, 0x1EB0))
    call("AECreateDesc", [text_type, text, 4, descriptor])
    _, flat = probe.raw(bytes(64))
    flattened, decoded, actual = ptr(flat), ptr(flat + 32), ptr(flat + 40)
    for side in (1, 0):
        probe.call("AESizeOfFlattenedDesc", [descriptor], side)
        p.expect(20, "flattened descriptor size includes the sixteen-byte header")
        probe.call("AEFlattenDesc", [descriptor, 0, 0, actual], side)
        p.expect(0, "flattening supports a size query")
        p.emit(0x807E0000 | (flat + 40))
        p.expect(20, "the size query publishes the required bytes")
        probe.call("AEFlattenDesc", [descriptor, flattened, 19, actual], side)
        p.expect(-1741, "the full descriptor must fit in the destination")
        probe.call("AEFlattenDesc", [descriptor, flattened, 20, actual], side)
        p.expect(0, "flatten a typed descriptor at exact capacity")
        for offset, expected in enumerate((0x646C6532, 0, text_type, 4, 0x64617461)):
            p.emit(0x807E0000 | (flat + 4 * offset))
            p.expect(expected, "flattened descriptor header and payload use guest byte order")
        probe.call("AEUnflattenDesc", [flattened, decoded], 1 - side)
        p.expect(0, "read a descriptor written by the other implementation")
        p.emit(0x807E0000 | (flat + 32))
        p.expect(text_type, "unflattening restores the descriptor type")
        call("AEGetDescData", [decoded, output, 4])
        p.emit(0x807E1EA0)
        p.expect(0x64617461, "unflattening restores the original payload")
        call("AEDisposeDesc", [decoded])
    p.emit(0x3C60646C, 0x60636531, 0x907E0000 | flat)
    call("AEUnflattenDesc", [flattened, decoded])
    call("AEDisposeDesc", [decoded])
    p.emit(0x38600000, 0x907E0000 | flat)
    call("AEUnflattenDesc", [flattened, decoded], -50)
    p.emit(0x807E0000 | (flat + 32))
    p.expect(0x6E756C6C, "a bad signature leaves a null descriptor")
    p.emit(0x807E0000 | (flat + 36))
    p.expect(0, "a bad signature leaves no handle")
    p.emit(0x3C60646C, 0x60636532, 0x907E0000 | flat, 0x3860FFFF, 0x907E0000 | (flat + 12))
    call("AEUnflattenDesc", [flattened, decoded], -50)

    raw_record = probe.far_raw(struct.pack(">6I", 1, 0, key, text_type, 4, 0x64617461))
    call("AECreateDesc", [0x54455354, raw_record, 24, decoded])
    for side in (1, 0):
        probe.call("AECheckIsRecord", [decoded], side)
        p.expect(1, "a structurally valid record can have a custom descriptor type")
    call("AEDisposeDesc", [decoded])
    nested_record = probe.far_raw(struct.pack(">6I", 1, 0, key, 0x7265636F, 4, 0))
    call("AECreateDesc", [0x54455354, nested_record, 24, decoded])
    call("AECheckIsRecord", [decoded], 1)
    call("AEDisposeDesc", [decoded])
    call("AECreateDesc", [0x54455354, raw_record, 20, decoded])
    call("AECheckIsRecord", [decoded], 0)
    call("AEDisposeDesc", [decoded])
    call("AECreateList", [0, 0, 1, decoded])
    call("AECheckIsRecord", [decoded], 1)
    call("AEDisposeDesc", [decoded])

    call("AECreateList", [0, 0, 0, listing])
    call("AECheckIsRecord", [listing], 0)
    call("AEPutDesc", [listing, 0, descriptor])
    call("AECountItems", [listing, length])
    p.emit(0x807E1EA8)
    p.expect(1, "AECountItems reports the appended descriptor")
    call("AEGetNthPtr", [listing, 1, wildcard, keyword, dtype, output, 4, length])
    call("AEGetNthPtr", [listing, 1, wildcard, 0, 0, output, 4, 0])
    p.emit(0x807E1EA0)
    p.expect(0x64617461, "AEGetNthPtr copies the stored descriptor bytes")
    p.emit(0x807E1EAC)
    p.expect(text_type, "AEGetNthPtr writes the type in guest byte order")
    p.emit(0x807E1EA8)
    p.expect(4, "AEGetNthPtr writes the eighth argument")
    tail = probe.raw(b"last")
    call("AEPutPtr", [listing, 0, text_type, tail, 4])
    call("AEDeleteItem", [listing, 1])
    call("AECountItems", [listing, length])
    p.emit(0x807E1EA8)
    p.expect(1, "AEDeleteItem removes only the specified descriptor")
    call("AEGetNthPtr", [listing, 1, wildcard, keyword, dtype, output, 4, length])
    p.emit(0x807E1EA0)
    p.expect(0x6C617374, "AEDeleteItem preserves the following descriptor")
    call("AEDeleteItem", [listing, 2], -1719)

    psn = probe.raw(struct.pack(">II", 0, 2))  # kCurrentProcess
    call("AECreateDesc", [0x70736E20, psn, 8, target])
    call("AECreateAppleEvent", [event_class, event_id, target, -1, 0, event])
    call("AEPutParamDesc", [event, key, descriptor])
    packed_event = probe.raw(bytes(320))
    call("AEFlattenDesc", [event, packed_event, 320, actual])
    call("AEUnflattenDesc", [packed_event, decoded])
    call("AEGetParamPtr", [decoded, key, wildcard, dtype, output, 4, length])
    p.emit(0x807E1EA0)
    p.expect(0x64617461, "an unflattened AppleEvent retains its parameters")
    call("AEDisposeDesc", [decoded])
    call("AEGetParamPtr", [event, key, wildcard, dtype, output, 4, length])
    p.emit(0x807E1EA0)
    p.expect(0x64617461, "AEGetParamPtr reads the event parameter through the shared selector")
    call("AEGetParamDesc", [event, key, wildcard, output])
    call("AEDisposeDesc", [output])
    call("AEDeleteParam", [event, key])
    call("AEGetParamDesc", [event, key, wildcard, output], -1701)
    call("AEPutParamDesc", [event, key, descriptor])

    _, current = probe.raw(bytes(12))
    saved = packed_event[1]
    saved_event, saved_reply = ptr(saved), ptr(saved + 8)
    suspend_mode, suspend_status = saved + 16, saved + 20
    call("AEManagerInfo", [0x76657273, output])
    p.emit(0x807E1EA0)
    p.expect(0x01058000, "AEManagerInfo returns the System 9 packed NumVersion")
    call("AEManagerInfo", [0x72656372, output])
    p.emit(0x807E1EA0)
    p.expect(0, "AEManagerInfo reports the number of active recorders")
    call("AEManagerInfo", [0x3F3F3F3F, output], -50)
    call("AEManagerInfo", [0x76657273, 0], -50)
    p.emit(0x38000000, 0x901E0000 | suspend_mode)
    call("AEGetTheCurrentEvent", [ptr(current)])
    p.emit(0x807E0000 | current)
    p.expect(0x6E756C6C, "there is no current Apple event before dispatch")
    main_code, p.code = p.code, []
    p.emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378, 0x90BE1EE0)
    p.emit(0x80C30004, 0x90DE0000 | (current + 8))
    p.emit(0x801E0000 | suspend_mode, 0x2C000000, 0)
    skip_suspend = len(p.code) - 1
    p.emit(0x80030000, 0x901E0000 | saved, 0x90DE0000 | (saved + 4))
    p.emit(0x80040000, 0x901E0000 | (saved + 8), 0x80040004, 0x901E0000 | (saved + 12))
    probe.call("AESuspendTheCurrentEvent", [saved_event])
    p.emit(0x907E0000 | suspend_status)
    p.code[skip_suspend] = 0x41820000 | (len(p.code) - skip_suspend) * 4
    probe.call("AEGetTheCurrentEvent", [ptr(current)])
    probe.call("AEInteractWithUser", [0, 0, 0])
    p.emit(0x907E1EE4, 0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    p.routines.append((0x1EB8, p.code))
    p.code = main_code
    probe.call("NewAEEventHandlerUPP", [ptr(0x1EB8)])
    p.nonzero("NewAEEventHandlerUPP prepares a callback descriptor")
    p.emit(0x907E1EC0)
    call("AEInstallEventHandler", [event_class, event_id, slot(0x1EC0), 0x1234ABCD, 0])
    call("AEGetEventHandler", [event_class, event_id, ptr(0x1EC4), ptr(0x1EC8), 0])
    p.emit(0x807E1EC8)
    p.expect(0x1234ABCD, "AEGetEventHandler returns the registered reference value")

    def post_and_dispatch(mode):
        call("AESend", [event, 0, mode | 1, 0, -1, 0, 0])  # kAENoReply to self
        # Earlier Appearance checks can leave preference-change broadcasts in
        # the same queue as the launch event and the synthetic self-send.
        p.emit(0x38000001, 0x901E1EE0, 0x901E1EE4)
        p.emit(0x38600020, 0x907E1EDC)
        queue_loop = len(p.code)
        probe.call("GetNextEvent", [1024, ptr(0x1EF0)])
        p.emit(0x2C030000, 0)
        queue_empty = len(p.code) - 1
        probe.call("AEProcessAppleEvent", [ptr(0x1EF0)])
        p.emit(0x807E1EE0, 0x2C030001, 0)
        dispatched = len(p.code) - 1
        p.emit(0x807E1EDC, 0x3863FFFF, 0x907E1EDC, 0x2C030000)
        p.emit(0x40820000 | (((queue_loop - len(p.code)) * 4) & 0xFFFF))
        p.code[queue_empty] = 0x41820000 | (len(p.code) - queue_empty) * 4
        p.code[dispatched] = 0x40820000 | (len(p.code) - dispatched) * 4
        p.emit(0x807E1EE0)
        p.expect(0x1234ABCD, "AEProcessAppleEvent dispatches the event posted by AESend")

    for mode, status in ((0x10, -1713), (0x20, 0), (0, 0)):
        post_and_dispatch(mode)
        p.emit(0x807E0000 | current)
        p.expect(0x61657674, "the current Apple event is available inside its handler")
        p.emit(0x807E0000 | (current + 4), 0x809E0000 | (current + 8), 0x7C632278)
        p.expect(0, "the current Apple event refers to the event passed to the handler")
        call("AEGetTheCurrentEvent", [ptr(current)])
        p.emit(0x807E0000 | current)
        p.expect(0x6E756C6C, "the current Apple event is cleared after dispatch")
        p.emit(0x807E0000 | (current + 4))
        p.expect(0, "the cleared descriptor has no handle")
        p.emit(0x807E1EE4)
        p.expect(status, f"interaction follows Apple event send mode {mode:#x}")
        call("AEInteractWithUser", [0, 0, 0])
    call("AESetTheCurrentEvent", [event], -1708)
    call("AEResumeTheCurrentEvent", [event, 0, 0, 0], -1708)
    for dispatcher, refcon, expected in ((0, 0, 0), (slot(0x1EC0), 0xFEDCBA98, 0xFEDCBA98), (-1, 0, 0x1234ABCD)):
        p.emit(0x38000001, 0x901E0000 | suspend_mode, 0x901E0000 | suspend_status)
        post_and_dispatch(0x10)
        p.emit(0x807E0000 | suspend_status)
        p.expect(0, "the event handler successfully suspended its current event")
        call("AEGetTheCurrentEvent", [ptr(current)])
        p.emit(0x807E0000 | current)
        p.expect(0x6E756C6C, "a suspended event disappears from the active dispatch")
        call("AEGetParamPtr", [saved_event, key, wildcard, dtype, output, 4, length])
        p.emit(0x807E1EA0)
        p.expect(0x64617461, "the suspended event still owns its original data")
        call("AESetTheCurrentEvent", [saved_event])
        call("AEGetTheCurrentEvent", [ptr(current)])
        p.emit(0x807E0000 | (current + 4), 0x809E0000 | (saved + 4))
        p.equal("manual handling exposes the original suspended descriptor")
        call("AEInteractWithUser", [0, 0, 0], -1713)
        call("AEResumeTheCurrentEvent", [saved_event, event, 0, 0], -1709)
        p.emit(0x38000000, 0x901E0000 | suspend_mode, 0x901E1EE0)
        call("AEResumeTheCurrentEvent", [saved_event, saved_reply, dispatcher, refcon])
        p.emit(0x807E1EE0)
        p.expect(expected, "the resume dispatch mode selects the expected callback")
        call("AEGetTheCurrentEvent", [ptr(current)])
        p.emit(0x807E0000 | current)
        p.expect(0x6E756C6C, "the manual current event clears when handling finishes")
        call("AEResumeTheCurrentEvent", [saved_event, 0, 0, 0], -1708)
    call("AERemoveEventHandler", [event_class, event_id, slot(0x1EC0), 0])
    probe.call("DisposeAEEventHandlerUPP", [slot(0x1EC0)])
    for item in (event, target, listing, descriptor):
        call("AEDisposeDesc", [item])


def printing_operations(probe):
    p = probe.p
    names = {
        "PrOpen": (0xC8000000, (), 0),
        "PrClose": (0xD0000000, (), 0),
        "PrError": (0xBA000000, (), -4217),
        "PrSetError": (0xC0000200, (-24144,), 0),
        "PrintDefault": (0x20040480, (0x87654321,), 0),
        "PrValidate": (0x52040498, (0x87654321,), 135),
        "PrGeneral": (0x70070480, (0x87654321,), 0),
        "PrOpenDoc": (0x04000C00, (0x87654321, 0, 0xDEADBEEF), 0xABCDEF87),
        "PrCloseDoc": (0x08000484, (0x87654321,), 0),
        "PrOpenPage": (0x10000808, (0x87654321, 0x10293847), 0),
        "PrClosePage": (0x1800040C, (0x87654321,), 0),
        "PrStlDialog": (0x2A040484, (0x87654321,), 135),
        "PrJobDialog": (0x32040488, (0x87654321,), 135),
        "PrJobMerge": (0x5804089C, (0x87654321, 0x10293847), 0),
    }
    _, base = probe.raw(bytes(16 + 4 * len(names)))
    connection, symbol_class, handle, destination = (base + offset for offset in (0, 4, 8, 12))
    procedures = {name: base + 16 + 4 * index for index, name in enumerate(names)}
    ptr, slot = probe.ptr, probe.slot
    probe.call("GetSharedLibrary", [probe.raw(b"\x0cInterfaceLib"), 0x70777063, 1, ptr(connection), 0, 0])
    p.expect(0, "open InterfaceLib's Printing Manager")

    def call(name, arguments=()):
        probe.arguments(arguments, 0)
        p.emit(0x819E0000 | procedures[name], 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x7FC2F378)

    for name, (selector, arguments, result) in names.items():
        pascal_name = probe.raw(bytes([len(name)]) + name.encode("ascii"))
        probe.call("FindSymbol", [slot(connection), pascal_name, ptr(procedures[name]), ptr(symbol_class)])
        p.expect(0, name + " resolves in InterfaceLib")
        p.emit(0x816011F4, 0x917E1820, 0x397E1800, 0x916011F4)
        call(name, arguments)
        p.emit(0x817E1820, 0x916011F4)
        p.expect(result, name + " preserves its Pascal result width")
        p.emit(0x807E1910)
        p.expect(selector, name + " forwards its full selector to the current PrGlue vector")
        for index, argument in enumerate(arguments):
            p.emit(0x807E1914 + 4 * index)
            p.expect(argument, f"{name} forwards argument {index + 1} to PrGlue")

    call("PrSetError", [-123])
    call("PrError")
    p.expect(-123, "PrError reads the shared low-memory printing status")
    call("PrClose")
    call("PrError")
    p.expect(0, "PrClose resets the printing status")
    call("PrOpenDoc", [0, 0, 0])
    p.expect(0, "PrOpenDoc rejects a missing print record")
    call("PrError")
    p.expect(-50, "PrOpenDoc reports the invalid record")
    for name, arguments in (("PrCloseDoc", [0]), ("PrOpenPage", [0, 0])):
        call(name, arguments)
        call("PrError")
        p.expect(-50, name + " rejects a missing print port")
    call("PrSetError", [0])

    probe.call("NewHandleClear", [120])
    p.nonzero("allocate a classic print record")
    p.emit(0x907E0000 | handle)
    call("PrintDefault", [slot(handle)])
    p.emit(0x807E0000 | handle, 0x80630000, 0x80630004)
    p.expect(0x00480048, "PrintDefault sets both 72 dpi fields")
    call("PrValidate", [slot(handle)])
    p.expect(0, "PrValidate accepts the default print record")
    probe.call("NewHandleClear", [120])
    p.nonzero("allocate a destination record for PrJobMerge")
    p.emit(0x907E0000 | destination)
    call("PrintDefault", [slot(destination)])
    p.emit(0x807E0000 | handle, 0x80630000)
    p.emit(0x38800002, 0xB083003E, 0x38800007, 0xB0830040, 0x38800003, 0xB0830042)
    p.emit(0x807E0000 | destination, 0x80630000, 0x388003B0, 0xB0830016)
    call("PrJobMerge", [slot(handle), slot(destination)])
    p.emit(0x807E0000 | destination, 0x80630000, 0x8063003E)
    p.expect(0x00020007, "PrJobMerge transfers the page range")
    p.emit(0x807E0000 | destination, 0x80630000, 0xA0630042)
    p.expect(3, "PrJobMerge transfers the copy count")
    p.emit(0x807E0000 | destination, 0x80630000, 0xA0630016)
    p.expect(944, "PrJobMerge preserves the destination paper rectangle")
    call("PrError")
    p.expect(0, "PrJobMerge clears the printing status on success")
    call("PrJobMerge", [0, slot(destination)])
    call("PrError")
    p.expect(-109, "PrJobMerge rejects a missing source record")
    call("PrSetError", [0])
    probe.call("DisposeHandle", [slot(destination)])
    probe.call("DisposeHandle", [slot(handle)])

    resolution = probe.raw(struct.pack(">HH", 4, 0x7FFF) + bytes(126))
    call("PrGeneral", [resolution])
    p.emit(0xA87E0000 | (resolution[1] + 2))
    p.expect(0, "PrGeneral succeeds for GetRslData")
    p.emit(0x807E0000 | (resolution[1] + 20))
    p.expect(0x00480048, "PrGeneral reports the default 72 dpi resolution")
    probe.call("CloseConnection", [ptr(connection)])
    p.expect(0, "close the InterfaceLib connection")


def manager_operations(probe):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot

    view = probe.raw(struct.pack(">4h", 0, 0, 30, 60))
    bounds = probe.raw(struct.pack(">4h", 0, 0, 1, 1))
    text = probe.raw(b"list")
    _, scratch = probe.raw(bytes(16) + bytes([0xFF]) * 256)
    probe.call("GetPort", [ptr(scratch + 12)])
    p.emit(0x38000004, 0xB01E0000 | (scratch + 4))
    probe.call("LNew", [view, bounds, 0x000A001E, 0, slot(scratch + 12), 0, 0, 0, 0])
    p.nonzero("LNew creates a list through the installed selector")
    p.emit(0x907E0000 | scratch)
    probe.call("LAddRow", [1, 1, slot(scratch)])
    p.expect(1, "LAddRow appends the second row")
    probe.call("LSetCell", [text, 4, 0x00010000, slot(scratch)])
    probe.call("LGetCell", [ptr(scratch + 8), ptr(scratch + 4), 0x00010000, slot(scratch)])
    p.emit(0x807E0000 | (scratch + 8))
    p.expect(0x6C697374, "LGetCell reads the updated row")
    probe.call("LSetSelect", [1, 0x00010000, slot(scratch)])
    p.emit(0x38000000, 0x901E0000 | (scratch + 8))
    probe.call("LGetSelect", [1, ptr(scratch + 8), slot(scratch)])
    p.expect(1, "LGetSelect finds the selected cell")
    p.emit(0x807E0000 | (scratch + 8))
    p.expect(0x00010000, "LGetSelect writes the selected coordinates")
    # ListRec.lastClick is a packed Cell at offset 56.
    p.emit(0x809E0000 | scratch, 0x80840000, 0x3CA0FFFE, 0x60A50007, 0x90A40038)
    probe.call("LLastClick", [ptr(scratch + 8), slot(scratch)])
    p.emit(0x807E0000 | (scratch + 8))
    p.expect(0xFFFE0007, "LLastClick writes a Cell through the CFM result pointer")
    probe.call("LDispose", [slot(scratch)])

    probe.call("NewIconSuite", [ptr(scratch)])
    p.expect(0, "NewIconSuite allocates a suite")
    probe.call("NewHandleClear", [32])
    p.nonzero("allocate icon suite test data")
    p.emit(0x907E0000 | (scratch + 8))
    probe.call("AddIconToSuite", [slot(scratch + 8), slot(scratch), 0x49434E23])
    p.expect(0, "AddIconToSuite stores an ICN# handle")
    probe.call("GetIconFromSuite", [ptr(scratch + 12), slot(scratch), 0x49434E23])
    p.expect(0, "GetIconFromSuite finds the stored icon")
    p.emit(0x807E0000 | (scratch + 8), 0x809E0000 | (scratch + 12))
    p.equal("the suite returns the same icon handle")
    probe.call("SetSuiteLabel", [slot(scratch), 5])
    p.expect(0, "SetSuiteLabel changes the suite label")
    probe.call("GetSuiteLabel", [slot(scratch)])
    p.expect(5, "GetSuiteLabel returns the stored label")
    probe.call("DisposeIconSuite", [slot(scratch), 1])
    p.expect(0, "DisposeIconSuite disposes the suite and its owned data")

    probe.call("Fix2SmallFract", [0x0002FEDC])
    p.expect(0xFEDC, "Fix2SmallFract returns an unsigned fraction")
    probe.call("SmallFract2Fix", [0xFEDC])
    p.expect(0x0000FEDC, "SmallFract2Fix preserves an unsigned fraction")
    rgb = probe.raw(struct.pack(">3H", 0, 0x1234, 0xFFFF))
    probe.call("RGB2CMY", [rgb, ptr(scratch + 8)])
    p.emit(0x807E0000 | (scratch + 8))
    p.expect(0xFFFFEDCB, "RGB2CMY complements the first two components")
    p.emit(0xA07E0000 | (scratch + 12))
    p.expect(0, "RGB2CMY complements the last component")
    probe.call("NewPalette", [1, 0, 0, 0])
    p.nonzero("NewPalette allocates a palette")
    p.emit(0x907E0000 | scratch)
    probe.call("SetPaletteUpdates", [slot(scratch), 0xE000])
    probe.call("GetPaletteUpdates", [slot(scratch)])
    p.expect(-8192, "GetPaletteUpdates reads the saved pmAllUpdates mode")
    probe.call("ResizePalette", [slot(scratch), 3])
    p.emit(0x807E0000 | scratch, 0x80630000, 0xA0630000)
    p.expect(3, "ResizePalette grows the live palette")
    probe.call("ResizePalette", [slot(scratch), 1])
    p.emit(0x807E0000 | scratch, 0x80630000, 0xA0630000)
    p.expect(1, "ResizePalette shrinks the live palette")
    probe.call("DisposePalette", [slot(scratch)])

    probe.call("GetScriptManagerVariable", [0])
    p.expect(0x700, "smVersion reads the installed Script Manager record")
    probe.call("FillParseTable", [ptr(scratch + 16), 0])
    p.expect(1, "FillParseTable supports Roman")
    p.emit(0x807E0000 | (scratch + 16))
    p.expect(0, "FillParseTable initializes the first Roman entries")
    p.emit(0x807E0000 | (scratch + 268))
    p.expect(0, "FillParseTable initializes the last Roman entries")

    probe.call("SndGetSysBeepState", [ptr(scratch + 4)])
    p.emit(0xA07E0000 | (scratch + 4), 0x907E0000 | scratch)
    probe.call("SndSetSysBeepState", [0])
    p.expect(0, "SndSetSysBeepState changes the installed beep state")
    p.emit(0x38000001, 0xB01E0000 | (scratch + 4))
    probe.call("SndGetSysBeepState", [ptr(scratch + 4)])
    p.emit(0xA07E0000 | (scratch + 4))
    p.expect(0, "SndGetSysBeepState returns the updated state")
    probe.call("SndSetSysBeepState", [slot(scratch)])
    p.expect(0, "SndSetSysBeepState restores the initial state")


def picture_utility_operations(probe):
    p = probe.p
    base = 0x2040
    picture, bad_picture, session, output = base, base + 4, base + 12, base + 16
    original = struct.pack(">H4h", 0, 3, 4, 19, 24) + bytes.fromhex(
        "001102ff001affff000000000031000300040013001800a0000700a0000700ff"
    )

    def handle(data, target):
        source = probe.far_raw(data)
        probe.call("NewHandle", [len(data)])
        p.nonzero("allocate a picture input")
        p.emit(0x907E0000 | target)
        probe.arguments([source, probe.slot(target), len(data)], 0)
        p.emit(0x80840000)
        p.call("BlockMoveData")

    def call(name, arguments, result=0):
        probe.call(name, arguments)
        p.expect(result, name + " returns the expected Picture Utilities status")

    def word(offset, expected, message):
        p.emit(0x807E0000 | (output + offset))
        p.expect(expected, message)

    handle(original, picture)
    handle(original[:-2], bad_picture)
    call("GetPictInfo", [probe.slot(picture), probe.ptr(output), 5, 1, 1, 0])
    word(2, 1, "GetPictInfo finds the RGB foreground color")
    word(28, 0x00100014, "GetPictInfo reports the normalized source size")
    word(40, 1, "GetPictInfo counts the painted rectangle")
    word(72, 2, "GetPictInfo counts repeated picture comments")
    word(76, 1, "GetPictInfo reports a distinct comment ID")
    p.emit(0x807E0000 | (output + 10), 0x80630000, 0xA063000A)
    p.expect(0xFFFF, "a one-color table contains the recorded red component")
    p.emit(0x807E0000 | (output + 80), 0x80630000, 0x80630000)
    p.expect(0x00020007, "the comment handle reports its count and ID")
    probe.call("DisposeHandle", [probe.slot(output + 10)])
    probe.call("DisposeHandle", [probe.slot(output + 80)])
    call("GetPictInfo", [probe.slot(picture), probe.ptr(output), 0, 0, 0, 1], -11000)
    call("NewPictInfo", [probe.ptr(session), 4, 0, 0, 0])
    call("RecordPictInfo", [probe.slot(session), probe.slot(picture)])
    call("RecordPictInfo", [probe.slot(session), probe.slot(bad_picture)], -11005)
    call("RecordPictInfo", [probe.slot(session), probe.slot(picture)])
    call("RetrievePictInfo", [probe.slot(session), probe.ptr(output), 0])
    word(40, 2, "a failed record preserves previous rectangle counts")
    word(72, 4, "retrieval combines comments from both pictures")
    call("DisposePictInfo", [probe.slot(session)])
    p.emit(0x807E0000 | (output + 80), 0x80630000, 0x80630000)
    p.expect(0x00040007, "retrieved comment handle survives disposing the accumulator")
    probe.call("DisposeHandle", [probe.slot(output + 80)])
    call("DisposePictInfo", [0], -11001)
    probe.call("DisposeHandle", [probe.slot(picture)])
    probe.call("DisposeHandle", [probe.slot(bad_picture)])

    world, pixmap, pixels = base, base + 4, base + 124
    p.emit(0x38001234, 0x901E0000 | pixels)
    bounds = probe.far_raw(struct.pack(">4h", 0, 0, 1, 1))
    call("NewGWorldFromPtr", [probe.ptr(world), 32, bounds, 0, 0, 0, probe.ptr(pixels), 4])
    probe.call("GetGWorldPixMap", [probe.slot(world)])
    p.nonzero("the borrowed GWorld has a pixmap")
    p.emit(0x907E0000 | pixmap)
    call("GetPixMapInfo", [probe.slot(pixmap), probe.ptr(output), 1, 1, 1, 0])
    word(2, 1, "GetPixMapInfo returns one color when requested")
    word(68, 1, "GetPixMapInfo counts its direct pixmap")
    probe.call("DisposeHandle", [probe.slot(output + 10)])
    call("NewPictInfo", [probe.ptr(session), 0, 0, 0, 0])
    call("RecordPixMapInfo", [probe.slot(session), probe.slot(pixmap)])
    call("RecordPixMapInfo", [probe.slot(session), probe.slot(pixmap)])
    call("RetrievePictInfo", [probe.slot(session), probe.ptr(output), 0])
    word(68, 2, "RecordPixMapInfo accumulates both recordings")
    call("DisposePictInfo", [probe.slot(session)])
    probe.call("DisposeGWorld", [probe.slot(world)])


def network_location_operations(probe):
    p = probe.p
    base = 0x2040
    ptr = lambda offset: probe.ptr(base + offset)
    slot = lambda offset: probe.slot(base + offset)
    service, extra = probe.far_raw(b"http,ftp\0"), probe.far_raw(b"ssh\0")
    missing = probe.far_raw(b"telnet\0")
    url_text = b"https://example.test/"
    url = probe.far_raw(url_text + b"\0")
    name = probe.far_raw(b"local\0")
    protocols = probe.far_raw(b"dns,slp\0")

    def call(name, arguments, expected=0):
        probe.call(name, arguments)
        p.expect(expected, name + " returns the expected result")

    def save(name, arguments, offset):
        probe.call(name, arguments)
        p.nonzero(name + " creates a guest object")
        p.emit(0x907E0000 | (base + offset))

    def word(offset, expected, message):
        p.emit(0x807E0000 | (base + offset))
        p.expect(expected, message)

    call("NSLLibraryVersion", [], 0x10100)
    save("NSLMakeNewServicesList", [service], 0)
    call("NSLServiceIsInServiceList", [slot(0), missing], 0)
    probe.call("NSLAddServiceToServicesList", [ptr(52), slot(0), extra])
    word(52, 0, "adding a service returns noErr in the NSLError structure")
    word(56, 0, "adding a service clears the NSLError context")
    call("NSLServiceIsInServiceList", [slot(0), extra], 1)
    call("NSLMakeServicesRequestPB", [slot(0), ptr(4)])
    probe.call("NSLDisposeServicesList", [slot(0)])
    call("NSLParseServicesRequestPB", [slot(4), ptr(8), ptr(12)])
    p.emit(0x807E0000 | (base + 8), 0x80630000)
    p.expect(3, "the request owns an independent copy of the three services")
    p.emit(0xA07E0000 | (base + 12))
    p.expect(21, "the request parser returns the complete service list length")
    call("NSLFreeTypedDataPtr", [slot(4)])
    save("NewPtr", [16], 4)
    call("NSLFreeURL", [slot(4)])

    save("NSLMakeNewNeighborhood", [name, protocols], 16)
    save("NSLCopyNeighborhood", [slot(16)], 20)
    call("NSLFreeNeighborhood", [slot(16)])
    call("NSLGetNeighborhoodLength", [slot(20)], 30)
    probe.call("NSLGetNameFromNeighborhood", [slot(20), ptr(8), ptr(12)])
    word(12, 5, "the neighborhood copy retains the name length")
    p.emit(0x807E0000 | (base + 8), 0x80630000)
    p.expect(0x6C6F6361, "the neighborhood copy retains the Pascal name bytes")
    call("NSLFreeNeighborhood", [slot(20)])

    call("NSLGetServiceFromURL", [url, ptr(8), ptr(12)])
    p.emit(0xA07E0000 | (base + 12))
    p.expect(5, "the URL service excludes the colon")
    record = b"\x05local" + bytes(8)
    registration = probe.far_raw(struct.pack(">4sI", b"NSL_", 8 + len(record) + len(url_text)) + record + url_text)
    call("NSLParseServiceRegistrationPB", [registration, ptr(8), ptr(12), ptr(16), ptr(20)])
    p.emit(0xA07E0000 | (base + 12))
    p.expect(14, "registration parsing reports the serialized neighborhood extent")
    p.emit(0xA07E0000 | (base + 20))
    p.expect(len(url_text), "registration parsing reports the nonterminated URL extent")
    p.emit(0x807E0000 | (base + 16), 0x80630000)
    p.expect(0x68747470, "registration parsing points to the URL in the packet")

    results = probe.far_raw(b"ftp://first,http://next")
    probe.arguments([results], 0)
    p.emit(0x907E0000 | (base + 72), 0x38000017, 0x901E0000 | (base + 76), 0x38000000, 0x901E0000 | (base + 8))
    call("NSLGetNextUrl", [ptr(64), ptr(8), ptr(12)], 1)
    word(12, 11, "the first URL ends at the comma")
    call("NSLGetNextUrl", [ptr(64), ptr(8), ptr(12)], 1)
    word(12, 11, "the second URL ends at the buffer boundary")
    call("NSLGetNextUrl", [ptr(64), ptr(8), ptr(12)], 0)
    word(8, 0, "URL iteration clears the cursor after exhaustion")

    neighborhoods = probe.far_raw(record + record)
    probe.arguments([neighborhoods], 0)
    p.emit(0x907E0000 | (base + 72), 0x3800001C, 0x901E0000 | (base + 76), 0x38000007, 0x901E0000 | (base + 118))
    call("NSLGetNextNeighborhood", [ptr(64), ptr(8), ptr(12)], 1)
    word(12, 14, "the first neighborhood is a packed record")
    call("NSLGetNextNeighborhood", [ptr(64), ptr(8), ptr(12)], 1)
    call("NSLGetNextNeighborhood", [ptr(64), ptr(8), ptr(12)], 0)
    word(12, 0, "neighborhood iteration clears the length after exhaustion")

    raw = probe.far_raw(b"A /,\xff")
    encoded = probe.far_raw(bytes(24))
    decoded = probe.far_raw(bytes(8))

    def capacity(value):
        p.emit(0x38000000 | value, 0xB01E0000 | (base + 16))

    def hex_result(changed, written):
        p.emit(0x887E0000 | (base + 18))
        p.expect(changed, "URL hex conversion reports whether it changed the text")
        p.emit(0xA07E0000 | (base + 16))
        p.expect(written, "URL hex conversion returns the counted output length")

    capacity(13)
    call("NSLHexEncodeText", [raw, 5, encoded, ptr(16), ptr(18)])
    hex_result(1, 13)
    probe.arguments([encoded], 0)
    p.emit(0x80630000)
    p.expect(0x41253230, "the URL encoder escapes a space")
    probe.arguments([encoded], 0)
    p.emit(0x80630004)
    p.expect(0x25326625, "the URL encoder uses lowercase hexadecimal")
    capacity(5)
    call("NSLHexDecodeText", [encoded, 13, decoded, ptr(16), ptr(18)])
    hex_result(1, 5)
    probe.arguments([decoded], 0)
    p.emit(0x80630000)
    p.expect(0x41202F2C, "decoding restores the input punctuation")
    probe.arguments([decoded], 0)
    p.emit(0x88630004)
    p.expect(0xFF, "decoding preserves the 8-bit character")
    capacity(12)
    call("NSLHexEncodeText", [raw, 5, encoded, ptr(16), ptr(18)], -4187)
    hex_result(0, 0)
    capacity(5)
    call("NSLHexDecodeText", [probe.far_raw(b"x%G1"), 4, decoded, ptr(16), ptr(18)], -4172)
    hex_result(0, 0)

    options = probe.far_raw(bytes([0xA5]) * 1030)
    call("NSLGetDefaultDialogOptions", [0], -4176)
    call("NSLGetDefaultDialogOptions", [options])
    probe.arguments([options], 0)
    p.emit(0x80630000)
    p.expect(0x01010000, "NSL initializes the dialog version and flags")
    for offset in (6, 262, 518, 774):
        probe.arguments([options], 0)
        p.emit(0xA0630000 | offset)
        p.expect(0x00A5, "NSL initializes each Pascal field without changing unused bytes")

    spec = probe.far_raw(bytes(70))
    description, solution = probe.far_raw(bytes(256)), probe.far_raw(bytes(256))
    table_name = probe.far_raw(b"\x0fNSL Error Table")
    call("FSMakeFSSpec", [0, 0, table_name, spec])
    probe.call("CurResFile")
    p.emit(0x907E0000 | (base + 16))
    call("NSLGetErrorStringsFromResource", [-4172, spec, 128, description, solution])
    probe.arguments([description, solution], 0)
    p.emit(0x80630000)
    p.expect(0x42616420, "the NSLE resource supplies a C string description")
    probe.arguments([description, solution], 0)
    p.emit(0x80640000)
    p.expect(0x54727920, "the NSLE resource supplies a C string solution")
    probe.call("CurResFile")
    p.emit(0x809E0000 | (base + 16))
    p.equal("NSLE lookup restores the caller's selected resource map")
    call("NSLGetErrorStringsFromResource", [-4171, spec, 128, description, solution])
    probe.arguments([description, solution], 0)
    p.emit(0x88630000, 0x88840000, 0x7C632378)
    p.expect(0, "an absent NSLE code clears both strings")


def classic_utility_operations(probe):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    _, base = probe.raw(bytes(128))

    def expected(name, values, result):
        probe.call(name, values)
        p.expect(result, name + " returns the expected installed utility result")

    for name, address in (
        ("GetCaretTime", 0x2F4),
        ("GetDblTime", 0x2F0),
        ("GetGrayRgn", 0x9EE),
        ("TickCount", 0x16A),
    ):
        probe.call(name)
        p.emit(0x80800000 | address)
        p.equal(name + " reads the shared guest global")

    probe.call("GetGrowZone")
    p.emit(0x81600118, 0x808B0010)
    p.equal("GetGrowZone reads the current guest zone callback")
    p.emit(0xA0800144, 0xB09E0000 | base)
    probe.call("SetEventMask", [0x6F00])
    p.emit(0xA0600144)
    p.expect(0x6F00, "SetEventMask writes the shared guest mask")
    p.emit(0xA07E0000 | base, 0x907E0000 | (base + 4))
    probe.call("SetEventMask", [slot(base + 4)])
    probe.call("GetKeys", [ptr(base + 8)])
    for offset in range(0, 16, 4):
        p.emit(0x807E0000 | (base + 8 + offset), 0x80800174 + offset)
        p.equal(f"GetKeys copies key map word {offset // 4}")
    probe.call("GetDateTime", [ptr(base + 8)])
    p.emit(0x807E0000 | (base + 8))
    p.nonzero("GetDateTime writes the current Mac epoch time")

    expected("HoldMemory", [ptr(base), 128], 0)
    expected("UnholdMemory", [ptr(base), 128], 0)
    expected("HoldMemory", [0xFFFFFFF0, 128], -50)
    expected("UnholdMemory", [0xFFFFFFF0, 128], -50)
    probe.call("NewHandleClear", [67])
    p.nonzero("allocate a real handle to query its logical size")
    p.emit(0x907E0000 | base)
    expected("GetHandleSize", [slot(base)], 67)
    expected("MemError", [], 0)
    probe.call("DisposeHandle", [slot(base)])
    expected("GetHandleSize", [0], -109)
    expected("MemError", [], -109)

    source = probe.raw(b"abcdefgh")
    for name in ("BlockMove", "BlockMoveData", "BlockMoveUncached", "BlockMoveDataUncached", "MPBlockCopy"):
        probe.call(name, [source, ptr(base), 8])
        probe.call(name, [ptr(base), ptr(base + 2), 6])
        p.emit(0x807E0000 | base)
        p.expect(0x61626162, name + " respects overlap")
        p.emit(0x807E0000 | (base + 4))
        p.expect(0x63646566, name + " copies the exact byte count")
    for name in ("BlockZero", "BlockZeroUncached", "MPBlockClear"):
        probe.call("BlockMoveData", [source, ptr(base), 8])
        probe.call(name, [ptr(base + 1), 6])
        p.emit(0x807E0000 | base)
        p.expect(0x61000000, name + " preserves the leading guard byte")
        p.emit(0x807E0000 | (base + 4))
        p.expect(0x00000068, name + " preserves the trailing guard byte")

    expected("MPProcessors", [], 1)
    expected("MPProcessorsScheduled", [], 1)
    # Publish the same guest RAM function twice after changing its immediate.
    entry = ptr(base + 16)
    probe.arguments([0x4E800020], 0)
    p.emit(0x907E0000 | (base + 20))  # blr
    for value in (17, 29):
        probe.arguments([0x38600000 | value], 0)
        p.emit(0x907E0000 | (base + 16))  # li r3,value
        probe.call("MPDataToCode", [entry, 8])
        p.emit(0x399E0000 | (base + 16), 0x7D8903A6, 0x4E800421)
        p.expect(value, "MPDataToCode publishes the changed instruction")
    probe.call("MPDataToCode", [0xFFFFFFFF, 0])

    for name, vectors in (
        ("Long2Fix", ((-32769, -2147483648), (-3, -196608), (32767, 2147418112), (32768, 2147483647))),
        ("Fix2Frac", ((-0x20001, -2147483648), (-0x20000, -2147483648), (0x10000, 0x40000000), (0x20000, 2147483647))),
        ("Frac2Fix", ((-8193, -1), (-8192, 0), (8192, 1), (0x7FFFFFFF, 0x20000))),
        ("Fix2Long", ((-32769, -1), (-32768, 0), (32768, 1), (0x7FFFFFFF, 32768))),
        ("FixRound", ((-32769, -1), (-32768, 0), (32768, 1), (0x7FFFFFFF, 32767))),
    ):
        for value, result in vectors:
            expected(name, [value], result)
    expected("GetScriptVariable", [0, 0], 0x700)

    bounds = probe.raw(struct.pack(">4h", 0, 0, 10, 20))
    expected("NewGWorld", [ptr(base), 32, bounds, 0, 0, 0], 0)
    p.emit(0x807E0000 | base)
    p.nonzero("NewGWorld returns the allocated port")
    p.emit(0x907E0000 | (base + 4))
    expected("UpdateGWorld", [ptr(base), 32, bounds, 0, 0, 0], 0)
    p.emit(0x807E0000 | base, 0x809E0000 | (base + 4))
    p.equal("UpdateGWorld preserves the original port when no update is needed")
    expanded = probe.raw(struct.pack(">4h", 0, 0, 12, 24))
    expected("UpdateGWorld", [ptr(base), 32, expanded, 0, 0, 0], (1 << 20) | (1 << 19))
    probe.call("GetGWorldPixMap", [slot(base)])
    p.nonzero("the resized world has a pixel map")
    p.emit(0x80630000, 0x8063000A)
    p.expect(0x000C0018, "the resized pixel map stores the requested height and width")
    probe.call("DisposeGWorld", [slot(base)])


PORT_APIS = (
    "LockPortBits",
    "UnlockPortBits",
    "IsPortColor",
    "IsPortOffscreen",
    "IsPortRegionBeingDefined",
    "IsPortPictureBeingDefined",
    "IsPortPolyBeingDefined",
    "SetPortBounds",
    "SetPortClipRegion",
    "SetPortVisibleRegion",
    "SetPortFracHPenLocation",
    "SetPortGrafProcs",
    "SetPortOpColor",
    "SetPortPenMode",
    "SetPortPenSize",
    "GetPortBackPixPatDirect",
    "GetPortPenPixPatDirect",
    "GetPortBackPixPat",
    "GetPortPenPixPat",
    "GetPortFillPixPat",
    "SetPortBackPixPat",
    "SetPortPenPixPat",
    "SetPortFillPixPat",
    "SetPortBackPixPatDirect",
    "SetPortPenPixPatDirect",
    "SetPortFillPixPatDirect",
    "SetQDGlobalsArrow",
)


FONT_FAMILY_APIS = (
    "FMCreateFontFamilyIterator",
    "FMResetFontFamilyIterator",
    "FMDisposeFontFamilyIterator",
    "FMGetNextFontFamily",
    "FMGetFontFamilyFromName",
    "FMGetFontFamilyName",
)


def font_family_operations(probe):
    p = probe.p
    _, base = probe.raw(bytes(160))
    ptr = lambda offset: probe.ptr(base + offset)
    slot = lambda offset: probe.slot(base + offset)
    name = probe.raw(b"\x08ROMAW FM")
    renamed = probe.raw(b"\x08ROMAW FN")

    def expected(call, args, value):
        probe.call(call, args)
        p.expect(value, call + " family result")

    def word(offset, value):
        p.emit(0x807E0000 | (base + offset))
        p.expect(value, "Font Manager result at field " + str(offset))

    probe.call("NewHandleClear", [54])
    p.nonzero("allocate a temporary FOND")
    p.emit(0x907E0000 | (base + 64))
    probe.call("AddResource", [slot(64), 0x464F4E44, 32001, name])
    expected("ResError", [], 0)
    expected("FMGetFontFamilyFromName", [name], 32001)
    expected("FMGetFontFamilyName", [32001, ptr(72)], 0)
    word(72, 0x08524F4D)
    word(76, 0x41572046)
    word(80, 0x4D000000)

    expected("FMCreateFontFamilyIterator", [0, 0, 0, ptr(0)], 0)
    expected("FMGetNextFontFamily", [ptr(0), ptr(68)], 0)
    p.emit(0xA07E0000 | (base + 68))
    p.nonzero("iterator reports a resource-backed family")
    p.emit(0xB07E0000 | (base + 70))
    expected("FMResetFontFamilyIterator", [0, 0, 0, ptr(0)], 0)
    expected("FMGetNextFontFamily", [ptr(0), ptr(68)], 0)
    p.emit(0xA07E0000 | (base + 68), 0xA09E0000 | (base + 70))
    p.equal("reset returns to the first family")

    probe.call("SetResInfo", [slot(64), 32002, renamed])
    expected("ResError", [], 0)
    expected("FMGetNextFontFamily", [ptr(0), ptr(68)], -983)
    expected("FMGetFontFamilyFromName", [renamed], 32002)
    expected("FMResetFontFamilyIterator", [0, 0, 0, ptr(0)], 0)
    expected("FMGetNextFontFamily", [ptr(0), ptr(68)], 0)
    expected("FMDisposeFontFamilyIterator", [ptr(0)], 0)
    word(0, 0)
    expected("FMCreateFontFamilyIterator", [0, 0, 0, 0], -50)
    expected("FMGetFontFamilyFromName", [0], -1)
    expected("FMGetFontFamilyName", [32002, 0], -50)
    probe.call("RemoveResource", [slot(64)])
    probe.call("DisposeHandle", [slot(64)])


FONT_INSTANCE_APIS = (
    "FMCreateFontFamilyInstanceIterator",
    "FMResetFontFamilyInstanceIterator",
    "FMDisposeFontFamilyInstanceIterator",
    "FMGetNextFontFamilyInstance",
    "FMGetFontFromFontFamilyInstance",
    "FMGetFontFamilyInstanceFromFont",
    "FMGetFontFormat",
    "FMGetFontGeneration",
    "FMGetFontTableDirectory",
    "FMGetFontTable",
    "FMGetFontContainer",
)


def font_instance_operations(probe):
    p = probe.p
    _, base = probe.raw(bytes(232))
    ptr = lambda offset: probe.ptr(base + offset)
    slot = lambda offset: probe.slot(base + offset)
    family = 32010
    head = int.from_bytes(b"head", "big")

    def expected(call, args, value=0):
        probe.call(call, args)
        p.expect(value, call + " font instance result")

    def word(offset, value, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(value, label)

    def same(first, second, label):
        p.emit(0x807E0000 | (base + first), 0x809E0000 | (base + second))
        p.equal(label)

    expected("FMCreateFontFamilyInstanceIterator", [family, ptr(0)])
    for output, style_size in ((64, 12), (68, 18), (72, 0x10000), (76, 0x20009)):
        expected("FMGetNextFontFamilyInstance", [ptr(0), ptr(output), ptr(80), ptr(82)])
        word(80, style_size, "iteration uses intrinsic style then strike size")
    same(64, 68, "bitmap strikes of the same face share one FMFont")
    expected("FMGetNextFontFamilyInstance", [ptr(0), ptr(68), 0, 0], -980)
    expected("FMGetFontFormat", [slot(64), ptr(84)])
    word(84, 0, "bitmap font format")
    expected("FMGetFontFormat", [slot(72), ptr(84)])
    word(84, int.from_bytes(b"true", "big"), "TrueType font format")

    expected("FMGetFontFromFontFamilyInstance", [family, 5, ptr(88), ptr(80)])
    same(88, 72, "synthetic style selects its intrinsic face")
    expected("FMGetFontFamilyInstanceFromFont", [slot(88), ptr(80), ptr(82)])
    word(80, family << 16 | 1, "font converts back to the original family and style")
    expected("FMGetFontGeneration", [slot(72), ptr(84)])
    p.emit(0x807E0000 | (base + 84))
    p.nonzero("native font has a catalog generation")
    expected("FMGetFontContainer", [slot(72), ptr(120)])
    p.emit(0x887E0000 | (base + 126))
    p.nonzero("font container has a file name")

    expected("FMGetFontTableDirectory", [slot(72), 8, ptr(92), ptr(100)])
    word(92, 0x00010000, "bounded directory copy includes the sfnt version")
    word(96, 0x00010010, "bounded directory copy includes the table count")
    word(100, 28, "directory reports its full length")
    expected("FMGetFontTable", [slot(72), head, 1, 2, ptr(104), ptr(100)])
    word(104, 0x22330000, "table reads only the requested byte range")
    word(100, 4, "table reports its full length")
    expected("FMGetFontTable", [slot(72), head, 3, 2, ptr(104), ptr(100)], -50)
    word(104, 0x22330000, "failed range preserves the buffer")
    expected("FMGetFontTable", [slot(64), head, 0, 0, 0, ptr(100)], -984)
    expected("FMGetFontTableDirectory", [slot(64), 0, 0, ptr(100)], -984)

    probe.call("CurResFile", [])
    p.emit(0x907E0000 | (base + 108))
    probe.call("UseResFile", [slot(108)])
    expected("FMGetNextFontFamilyInstance", [ptr(0), ptr(88), 0, 0], -983)
    expected("FMResetFontFamilyInstanceIterator", [family, ptr(0)])
    expected("FMGetNextFontFamilyInstance", [ptr(0), ptr(88), 0, 0])
    same(88, 64, "reset preserves the face identifier")
    expected("FMDisposeFontFamilyInstanceIterator", [ptr(0)])
    expected("FMGetNextFontFamilyInstance", [ptr(0), ptr(88), 0, 0], -50)
    expected("FMResetFontFamilyInstanceIterator", [family, ptr(0)], -50)


FONT_CATALOG_APIS = (
    "FMCreateFontIterator",
    "FMResetFontIterator",
    "FMDisposeFontIterator",
    "FMGetNextFont",
    "FMGetGeneration",
    "FMGetFontFamilyGeneration",
)


TYPE_SELECTION_APIS = (
    "TypeSelectClear",
    "TypeSelectNewKey",
    "TypeSelectCompare",
    "TypeSelectFindItem",
    "CharacterType",
)


def type_selection_operations(probe):
    p = probe.p
    ptr = probe.ptr
    record = probe.raw(bytes([0xA5]) * 72)
    event = probe.raw(struct.pack(">HIIhhH", 3, ord("b"), 1, 0, 0, 0))
    items = [probe.raw(bytes([len(name)]) + name) for name in (b"Alpha", b"Book", b"Zulu")]
    _, table = probe.raw(bytes(12))
    probe.arguments(items, 0)
    for i in range(3):
        p.emit(0x901E0000 | (i + 3) << 21 | (table + i * 4))
    callback = probe.raw(bytes(8))
    p.routines.append(
        (callback[1], [0x38000000, 0xB0040000, 0x5463103A, 0x7CC61A14, 0x80C6FFFC, 0x90C50000, 0x38600001, 0x4E800020])
    )
    probe.call("TypeSelectClear", [record])
    p.emit(0xA07E0000 | (record[1] + 70))
    p.expect(0xA5A5, "clearing a type-selection record preserves the next guest word")
    probe.call("TypeSelectNewKey", [event, record])
    p.expect(1, "remember a key in the Roman search prefix")
    probe.call("TypeSelectCompare", [record, 0, items[1]])
    p.expect(0, "compare a typed lowercase prefix with a capitalized item")
    probe.call("TypeSelectFindItem", [record, 3, 0, callback, ptr(table)])
    p.expect(2, "invoke the PPC item callback and return the one-based match")
    probe.call("CharacterType", [probe.raw(b"A7 "), 1, 0])
    p.expect(0x100, "classify the selected Roman digit")


def font_catalog_operations(probe):
    p = probe.p
    _, base = probe.raw(bytes(112))
    ptr = lambda offset: probe.ptr(base + offset)
    slot = lambda offset: probe.slot(base + offset)

    def checked(name, arguments, value=0):
        probe.call(name, arguments)
        p.expect(value, name + " catalog result")

    def same(a, b, label):
        p.emit(0x807E0000 | (base + a), 0x809E0000 | (base + b))
        p.equal(label)

    checked("FMGetFontFromFontFamilyInstance", [32010, 0, ptr(64), 0])
    checked("FMCreateFontIterator", [0, 0, 0, ptr(0)])
    checked("FMGetNextFont", [ptr(0), ptr(68)])
    checked("FMResetFontIterator", [0, 0, 0, ptr(0)])

    entry = len(p.code)
    probe.call("FMGetNextFont", [ptr(0), ptr(72)])
    done = len(p.code) + 1
    p.emit(0x2C030000, 0)
    p.emit(0x807E0000 | (base + 72), 0x809E0000 | (base + 64), 0x7C032000, 0x40820010)
    p.emit(0x807E0000 | (base + 76), 0x38630001, 0x907E0000 | (base + 76))
    p.emit(0x807E0000 | (base + 80), 0x38630001, 0x907E0000 | (base + 80), 0x2C031000)
    p.emit(0x41800000 | (((entry - len(p.code)) * 4) & 0xFFFC))
    p.code[done] = 0x40820000 | ((len(p.code) - done) * 4)
    p.expect(-980, "catalog enumeration reaches completion")
    p.emit(0x807E0000 | (base + 76))
    p.expect(1, "two bitmap sizes appear only once in the face catalog")
    checked("FMGetNextFont", [ptr(0), ptr(72)], -980)
    checked("FMResetFontIterator", [0, 0, 0, ptr(0)])
    checked("FMGetNextFont", [ptr(0), ptr(72)])
    same(68, 72, "reset reproduces the first catalog face")

    probe.call("FMGetGeneration", [])
    p.nonzero("resource font catalog has a generation")
    p.emit(0x907E0000 | (base + 84))
    checked("FMGetFontFamilyGeneration", [32010, ptr(88)])
    same(84, 88, "catalog and fixture family share a generation")
    checked("FMGetFontGeneration", [slot(64), ptr(88)])
    same(84, 88, "catalog and fixture font share a generation")
    probe.call("CurResFile", [])
    p.emit(0x907E0000 | (base + 92))
    probe.call("UseResFile", [slot(92)])
    checked("FMGetNextFont", [ptr(0), ptr(72)], -983)
    probe.call("FMGetGeneration", [])
    p.emit(0x809E0000 | (base + 84), 0x7C632278)
    p.nonzero("changing the resource scope advances the catalog generation")
    checked("FMResetFontIterator", [0, 0, 0, ptr(0)])
    checked("FMGetNextFont", [ptr(0), ptr(72)])
    same(68, 72, "new generation still identifies the same first font")
    checked("FMDisposeFontIterator", [ptr(0)])
    checked("FMDisposeFontIterator", [ptr(0)], -50)
    checked("FMGetNextFont", [ptr(0), ptr(72)], -50)
    checked("FMGetFontFamilyGeneration", [32010, 0], -50)


def theme_font_operations(probe):
    p = probe.p
    base = 0x2000
    ptr = lambda offset: probe.ptr(base + offset)
    saved = probe.far_raw(bytes(52))
    name = probe.far_raw(bytes(256))
    probe.call("BlockMoveData", [ptr(0), saved, 52])
    for selector, font in ((78, 0x40009), (80, 0x3000C), (82, 0x30009), (86, 0x3000C), (88, 0x30009)):
        probe.call("GetScriptVariable", [0, selector])
        p.expect(font, "Roman Script Manager packed font/size selector " + str(selector))
    probe.call("GetScriptVariable", [0, 12])
    p.emit(0x5463801E, 0x6063000C, 0x907E0000 | (base + 28))
    probe.call("GetScriptVariable", [0, 84])
    p.emit(0x809E0000 | (base + 28))
    p.equal("the Roman system-font pair uses the live script family")

    system_name = probe.far_raw(bytes(256))
    probe.call("GetSystemFont", [0, system_name])
    p.expect(0, "get the Roman system font name")
    probe.call("GetScriptVariable", [0, 12])
    p.emit(0x907E0000 | base)
    probe.call("GetFontName", [probe.slot(base), name])
    for offset in range(0, 12, 4):
        probe.arguments([system_name, name], 0)
        p.emit(0x80630000 | offset, 0x80840000 | offset)
        p.equal("GetSystemFont uses the live Script Manager font family")
    probe.call("GetSystemFont", [0, 0])
    p.expect(-50, "GetSystemFont requires a name output")

    for role in (0, 1, 2, 3, 103, 106):
        legacy_role = {103: 0, 106: 1}.get(role, role)
        probe.call("GetThemeFont", [legacy_role, -1, name, ptr(32), ptr(34)], side=1)
        p.expect(0, "resolve the saved Appearance font role " + str(legacy_role))
        probe.call("GetThemeFont", [role, -1, name, ptr(8), ptr(10)])
        p.expect(0, "resolve Appearance theme font role " + str(role))
        p.emit(0xA07E0000 | (base + 8), 0xA09E0000 | (base + 32))
        p.equal("resolved theme font size matches the saved role")
        p.emit(0x887E0000 | (base + 10), 0x889E0000 | (base + 34))
        p.equal("resolved theme font face matches the saved role")
    probe.call("GetThemeFont", [5, 0, name, ptr(8), ptr(10)])
    p.expect(0, "resolve the Roman application font")
    p.emit(0xA07E0000 | (base + 8))
    p.expect(12, "the Roman application font uses the Script Manager size")
    p.emit(0x887E0000 | (base + 10))
    p.expect(0, "the Roman application font uses plain face")
    for getter, role in (("GetSmallSystemFont", 1), ("GetViewsFont", 3)):
        expected = probe.far_raw(bytes(256))
        actual = probe.far_raw(bytes(256))
        probe.call("GetThemeFont", [role, 0, expected, ptr(8), 0])
        p.expect(0, "resolve the Roman font role for " + getter)
        arguments = [0, actual]
        if role == 3:
            arguments.append(ptr(14))
        probe.call(getter, arguments)
        p.expect(0, "read the Roman font through " + getter)
        probe.arguments([expected, actual], 0)
        p.emit(0x80630000, 0x80840000)
        p.equal(getter + " uses the matching theme font")
        if role == 3:
            p.emit(0xA07E0000 | (base + 8), 0xA09E0000 | (base + 14))
            p.equal("GetViewsFont returns the matching size")
        arguments[1] = 0
        probe.call(getter, arguments)
        p.expect(-50, getter + " requires a name output")
    probe.call("GetThemeFont", [103, 0, name, 0, 0])
    p.expect(0, "resolve the command-key font with optional scalar outputs omitted")
    probe.arguments([name], 0)
    p.emit(0x80630000)
    p.expect(0x092E4B65, "command-key font is .Keyboard")
    probe.call("GetThemeFont", [99, 0, 0, 0, 0])
    p.expect(-50, "reject undefined theme font roles")
    probe.call("GetThemeFontSmoothing", [0, 0])
    p.expect(-50, "the smoothing query requires an output")
    probe.call("GetThemeFontSmoothing", [ptr(32), ptr(34)])
    p.expect(0, "read the live font smoothing preference")
    p.emit(0xA07E0000 | (base + 34), 0xB07E0000 | (base + 36))
    probe.call("SetThemeFontSmoothing", [1, 11, 0])
    p.expect(-50, "reject font smoothing below twelve points")
    probe.call("SetThemeFontSmoothing", [1, 25, 0])
    p.expect(-50, "reject font smoothing above twenty-four points")
    probe.call("GetThemeFontSmoothing", [ptr(32), ptr(34)])
    p.expect(0, "read font smoothing after rejecting the changes")
    p.emit(0xA07E0000 | (base + 34), 0xA09E0000 | (base + 36))
    p.equal("invalid smoothing setters leave the threshold intact")
    probe.call("GetPort", [ptr(12)])
    port = probe.slot(base + 12)
    for field, offset in (("Font", 16), ("Size", 20), ("Face", 24)):
        probe.call("GetPortText" + field, [port])
        p.emit(0x907E0000 | (base + offset))
    probe.call("GetThemeFont", [2, 0, name, ptr(32), ptr(34)])
    p.expect(0, "resolve the emphasized font before applying it")
    probe.call("UseThemeFont", [2, 0])
    p.expect(0, "apply a theme font to the current QuickDraw port")
    probe.call("GetPortTextSize", [port])
    p.emit(0xA09E0000 | (base + 32))
    p.equal("UseThemeFont applies the resolved size")
    probe.call("GetPortTextFace", [port])
    p.emit(0x889E0000 | (base + 34))
    p.equal("UseThemeFont applies the emphasized face")
    probe.call("GetThemeFont", [200, 0, 0, ptr(8), ptr(10)])
    p.expect(0, "resolve the current port's font")
    p.emit(0xA07E0000 | (base + 8), 0xA09E0000 | (base + 32))
    p.equal("the current-port role reads the live size")
    text = probe.far_raw(b"A")
    ink = probe.far_raw(bytes(8))
    probe.call("QDTextBounds", [1, text, ink])
    probe.arguments([ink], 0)
    p.emit(0xA8630000)
    p._check(0x2C030000, 0x41800000, "bitmap text ink extends above the baseline")
    probe.arguments([ink], 0)
    p.emit(0xA8830006, 0xA8630002)
    p._check(0x7C032000, 0x41800000, "bitmap text ink has positive width")
    probe.call("QDTextBounds", [0, 0, ink])
    probe.arguments([ink], 0)
    p.emit(0x80830000, 0x80630004, 0x7C632378)
    p.expect(0, "an empty text run clears the ink bounds")
    # Fill the Classic font cache and force it to evict entries while measuring.
    for size in range(8, 26):
        probe.call("TextSize", [size])
        probe.call("FontMetrics", [ptr(32)])
        probe.call("GetHandleSize", [probe.slot(base + 48)])
        p._check(0x2C030400, 0x40800000, "font metrics retain a live width table at size " + str(size))
    for field, offset in (("Font", 16), ("Size", 20), ("Face", 24)):
        probe.call("Text" + field, [probe.slot(base + offset)])
    probe.call("BlockMoveData", [saved, ptr(0), 52])
    theme_text_operations(probe)


def theme_text_operations(probe):
    p = probe.p
    base = 0x2040
    ptr = lambda offset: probe.ptr(base + offset)
    slot = lambda offset: probe.slot(base + offset)

    def check(name, args, status=0):
        probe.call(name, args)
        p.expect(status, name + " returns the expected Appearance text status")

    def load(offset, register=3, opcode=0x807E0000):
        p.emit((opcode + ((register - 3) << 21)) | (base + offset))

    def retain_string(offset, text):
        probe.call("CFStringCreateWithCString", [0, probe.far_raw(text + b"\0"), 0])
        p.nonzero("create a theme text fixture")
        p.emit(0x907E0000 | (base + offset))

    retain_string(0, b"Abc")
    retain_string(4, b"one\rtwo")
    retain_string(8, b"Appearance text truncation")
    retain_string(12, b"")
    probe.call("GetGWorld", [ptr(16), ptr(20)])
    bounds = probe.far_raw(struct.pack(">4h", 0, 0, 40, 96))
    check("NewGWorld", [ptr(24), 32, bounds, 0, 0, 0])
    probe.call("GetGWorldPixMap", [slot(24)])
    p.nonzero("resolve the theme text drawing pixmap")
    p.emit(0x907E0000 | (base + 28))
    check("LockPixels", [slot(28)], 1)
    probe.call("SetGWorld", [slot(24), 0])
    probe.call("TextFont", [0])
    probe.call("TextSize", [12])
    probe.call("TextFace", [0])
    probe.call("GetFontInfo", [ptr(32)])

    short_text = probe.far_raw(b"Abc")
    check("GetThemeTextDimensions", [slot(0), 200, 1, 0, ptr(40), ptr(44)])
    probe.call("TextWidth", [short_text, 0, 3])
    p.emit(0x38830001)
    load(42, opcode=0xA07E0000)
    p.equal("single-line Appearance measurement includes a pixel of width padding")
    load(32, register=4, opcode=0xA07E0000)
    load(34, register=5, opcode=0xA07E0000)
    p.emit(0x7C842A14)
    load(40, opcode=0xA07E0000)
    p.equal("single-line height adds the font ascent and descent")
    load(34, register=4, opcode=0xA07E0000)
    p.emit(0x7C8400D0)
    load(44, opcode=0xA87E0000)
    p.equal("single-line baseline is measured up from the bottom")

    p.emit(0x38000060, 0xB01E0000 | (base + 42))
    check("GetThemeTextDimensions", [slot(4), 200, 1, 1, ptr(40), ptr(44)])
    load(42, opcode=0xA07E0000)
    p.expect(96, "wrapped measurement preserves the width constraint")
    load(32, register=4, opcode=0xA07E0000)
    load(34, register=5, opcode=0xA07E0000)
    p.emit(0x7C842A14, 0x5484083C)
    load(38, register=5, opcode=0xA07E0000)
    p.emit(0x7C842A14)
    load(40, opcode=0xA07E0000)
    p.equal("two lines include one leading interval")
    check("GetThemeTextDimensions", [slot(12), 200, 1, 0, ptr(40), ptr(44)])
    load(40)
    p.expect(0, "empty text measures zero")
    check("GetThemeTextDimensions", [slot(0), 200, 1, 0, ptr(40), 0], -50)
    p.emit(0x38000001, 0x901E0000 | (base + 48), 0x901E0000 | (base + 52))
    check("GetThemeTextShadowOutset", [200, 1, ptr(48)])
    load(48)
    load(52, register=4)
    p.emit(0x7C632378)
    p.expect(0, "Classic Appearance text has no extra shadow outset")
    check("GetThemeTextShadowOutset", [99, 1, ptr(48)], -50)

    probe.call("CFStringCreateMutableCopy", [0, 0, slot(8)])
    p.nonzero("allocate a mutable string to truncate")
    p.emit(0x907E0000 | (base + 56))
    check("TruncateThemeText", [slot(56), 200, 1, 1000, 0, ptr(60)])
    load(60, opcode=0x887E0000)
    p.expect(0, "the truncation flag stays clear when all glyphs fit")
    check("TruncateThemeText", [slot(56), 200, 1, 45, 0, ptr(60)])
    load(60, opcode=0x887E0000)
    p.expect(1, "the truncation flag reports the actual edit")
    probe.call("CFStringGetLength", [slot(56)])
    p.emit(0x3863FFFF, 0x907E0000 | (base + 64))
    probe.call("CFStringGetCharacterAtIndex", [slot(56), slot(64)])
    p.expect(0x2026, "end truncation replaces the tail with an ellipsis")
    check("TruncateThemeText", [slot(8), 200, 1, 45, 0, ptr(60)], -50)

    target = probe.far_raw(struct.pack(">4h", 1, 2, 40, 96))
    check("DrawThemeTextBox", [slot(0), 200, 1, 0, target, -2, 0])
    probe.call("GetPen", [ptr(68)])
    load(32, register=4, opcode=0xA07E0000)
    p.emit(0x38840001)
    load(68, opcode=0xA07E0000)
    p.equal("single-line theme drawing positions the pen on the font baseline")
    probe.call("TextWidth", [short_text, 0, 3])
    p.emit(0x38830003)
    load(70, opcode=0xA07E0000)
    p.equal("theme drawing advances the pen by the actual text width")
    check("DrawThemeTextBox", [slot(4), 0, 1, 1, target, 1, 0])
    probe.call("GetPortTextFont", [slot(24)])
    p.expect(0, "theme drawing restores the original font")
    probe.call("GetPortTextSize", [slot(24)])
    p.expect(12, "theme drawing restores the original size")
    probe.call("SetGWorld", [slot(16), slot(20)])
    probe.call("UnlockPixels", [slot(28)])
    probe.call("DisposeGWorld", [slot(24)])
    for offset in (0, 4, 8, 12, 56):
        probe.call("CFRelease", [slot(offset)])


def theme_cursor_operations(probe):
    p = probe.p
    _, state = probe.raw(bytes(16))
    callback = probe.raw(bytes(8))
    p.routines.append((callback[1], [0x80630000, 0x90620000 | (state + 4), 0x4E800020]))
    # Preserve the real cursor and capture the data reaching the guest trap.
    p.emit(0x81600F44, 0x917E0000 | state)
    for name, args, status, pattern in (
        ("SetThemeCursor", [18], -30565, None),
        ("SetAnimatedThemeCursor", [4, 0], -30565, None),
        ("SetThemeCursor", [4], 0, 0x476A0123),
        ("SetThemeCursorWithOptions", [5, 0, 17], 0, 0x476A0124),
        ("SetThemeCursor", [7], 0, 0x476A0125),
        ("SetAnimatedThemeCursor", [7, 9], 0, 0x476A0126),
        ("SetThemeCursorWithOptions", [7, 1, 9], 0, 0x476A0126),
        ("SetAnimatedThemeCursor", [14, 16], 0, 0x476A0127),
    ):
        p.emit(0x38000000, 0x901E0000 | (state + 4), 0x397E0000 | callback[1], 0x91600F44)
        probe.call(name, args)
        p.emit(0x817E0000 | state, 0x91600F44)
        p.expect(status, name + " cursor status")
        p.emit(0x807E0000 | (state + 4))
        p.expect(pattern or 0, name + " installs only the resolved cursor")


def port_operations(probe):
    p = probe.p
    _, base = probe.raw(bytes(192))
    ptr = lambda offset: probe.ptr(base + offset)
    slot = lambda offset: probe.slot(base + offset)
    bounds = probe.raw(struct.pack(">4h", 0, 0, 12, 24))
    port = slot(0)

    def expected(name, arguments, value):
        probe.call(name, arguments)
        p.expect(value, name + " result")

    def save(name, arguments, offset):
        probe.call(name, arguments)
        p.nonzero(name + " returns a live object")
        p.emit(0x907E0000 | (base + offset))

    def word(offset, value, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(value, label)

    probe.call("GetPort", [ptr(176)])
    for side in (0, 1):
        probe.call("IsValidPort", [0], side)
        p.expect(0, "a null port is invalid")
        probe.call("IsValidPort", [ptr(0)], side)
        p.expect(0, "an unregistered port is invalid")
        probe.call("CreateNewPort", [], side)
        p.nonzero("CreateNewPort allocates a color port")
        p.emit(0x907E0000 | (base + 180))
        created = slot(180)
        # The saved validator installs a PPC exception handler when checking live handles.
        expected("IsValidPort", [created], 1)
        p.emit(0x809E0000 | (base + 180), 0x80640018, 0x907E0000 | (base + 184), 0x38000000, 0x90040018)
        expected("IsValidPort", [created], 0)
        p.emit(0x809E0000 | (base + 180), 0x807E0000 | (base + 184), 0x90640018)
        expected("IsValidPort", [created], 1)
        expected("IsPortColor", [created], 1)
        probe.call("GetPortPrintingReference", [created], side)
        p.expect(0, "new ports have no printing reference")
        probe.call("SetPortPrintingReference", [created, 0x12345678], 1 - side)
        probe.call("GetPortPrintingReference", [created], side)
        p.expect(0x12345678, "saved and native access share the GrafVars printing reference")
        probe.call("SetPort", [created])
        expected("GetPortPrintingReference", [0], 0x12345678)
        probe.call("SetPortPrintingReference", [0, 0])
        probe.call("GetPortPrintingReference", [created], 1 - side)
        p.expect(0, "the current-port setter clears the same printing reference")
        probe.call("SetPort", [slot(176)])
        probe.call("DisposePort", [created], 1 - side)
        for validator in (0, 1):
            probe.call("IsValidPort", [created], validator)
            p.expect(0, "both validators reject the disposed port")
        probe.call("DisposePort", [0], side)

    expected("NewGWorld", [ptr(0), 32, bounds, 0, 0, 0], 0)
    expected("IsValidPort", [port], 1)
    save("GetGWorldPixMap", [port], 4)
    expected("IsPortColor", [port], 1)
    expected("IsPortOffscreen", [port], 1)
    expected("IsPortColor", [probe.raw(bytes(108))], 0)
    expected("GetPixelsState", [slot(4)], 0)
    expected("LockPortBits", [port], 0)
    expected("LockPortBits", [port], 0)
    expected("GetPixelsState", [slot(4)], 128)
    expected("UnlockPortBits", [port], 0)
    expected("GetPixelsState", [slot(4)], 128)
    expected("UnlockPortBits", [port], 0)
    expected("GetPixelsState", [slot(4)], 0)

    shifted = probe.raw(struct.pack(">4h", 2, 4, 11, 17))
    probe.call("SetPortBounds", [port, shifted])
    probe.call("GetPortBounds", [port, ptr(32)])
    word(32, 0x00020004, "SetPortBounds updates top and left")
    word(36, 0x000B0011, "SetPortBounds updates bottom and right")
    probe.call("SetPortPenSize", [port, 0x00030004])
    probe.call("GetPortPenSize", [port, ptr(32)])
    word(32, 0x00030004, "SetPortPenSize writes the packed Point")
    probe.call("SetPortPenMode", [port, 10])
    expected("GetPortPenMode", [port], 10)
    probe.call("SetPortFracHPenLocation", [port, 0x1234])
    expected("GetPortFracHPenLocation", [port], 0x1234)
    proc_table = probe.raw(bytes(64))
    probe.call("SetPortGrafProcs", [port, proc_table])
    probe.call("GetPortGrafProcs", [port])
    p.pointer(proc_table[1], "SetPortGrafProcs stores the caller's procedure table")
    probe.call("SetPortGrafProcs", [port, 0])
    expected("GetPortGrafProcs", [port], 0)

    rgb = probe.raw(struct.pack(">3H", 0x1234, 0x5678, 0x9ABC))
    probe.call("SetPortOpColor", [port, rgb])
    probe.call("GetPortOpColor", [port, ptr(32)])
    word(32, 0x12345678, "SetPortOpColor writes red and green")
    p.emit(0xA07E0000 | (base + 36))
    p.expect(0x9ABC, "SetPortOpColor writes blue")

    save("NewRgn", [], 8)
    save("NewRgn", [], 12)
    for setter, getter in (
        ("SetPortClipRegion", "GetPortClipRegion"),
        ("SetPortVisibleRegion", "GetPortVisibleRegion"),
    ):
        probe.call("RectRgn", [slot(8), shifted])
        probe.call(setter, [port, slot(8)])
        probe.call("SetEmptyRgn", [slot(8)])
        probe.call(getter, [port, slot(12)])
        probe.call("GetRegionBounds", [slot(12), ptr(32)])
        word(32, 0x00020004, setter + " copies the region")
        word(36, 0x000B0011, setter + " preserves the entire bounds")
    probe.call("SetQDError", [0])
    probe.call("SetPortClipRegion", [port, 0])
    expected("QDError", [], -50)

    save("GetQDGlobalsThePort", [], 16)
    probe.call("SetPort", [port])
    expected("IsPortRegionBeingDefined", [port], 0)
    probe.call("OpenRgn", [])
    expected("IsPortRegionBeingDefined", [0], 1)
    probe.call("CloseRgn", [slot(8)])
    expected("IsPortRegionBeingDefined", [port], 0)
    expected("IsPortPictureBeingDefined", [port], 0)
    save("OpenPicture", [bounds], 20)
    expected("IsPortPictureBeingDefined", [0], 1)
    probe.call("ClosePicture", [])
    expected("IsPortPictureBeingDefined", [port], 0)
    probe.call("KillPicture", [slot(20)])
    expected("IsPortPolyBeingDefined", [port], 0)
    save("OpenPoly", [], 20)
    expected("IsPortPolyBeingDefined", [0], 1)
    probe.call("ClosePoly", [])
    expected("IsPortPolyBeingDefined", [port], 0)
    probe.call("KillPoly", [slot(20)])
    probe.call("SetPort", [slot(16)])
    probe.call("DisposeRgn", [slot(8)])
    probe.call("DisposeRgn", [slot(12)])

    save("NewPixPat", [], 24)
    save("NewPixPat", [], 28)
    probe.call("MakeRGBPat", [slot(24), rgb])
    for kind, field in (("Back", 0x20), ("Pen", 0x3A), ("Fill", 0x3E)):
        if kind == "Fill":
            p.emit(0x807E0000 | base, 0x80630000 | field, 0x907E0000 | (base + 20))
        else:
            save("GetPort" + kind + "PixPatDirect", [port], 20)
        probe.call("SetPort" + kind + "PixPat", [port, slot(24)])
        probe.call("GetPort" + kind + "PixPat", [port, slot(28)])
        p.emit(0x809E0000 | (base + 28))
        p.equal("GetPort" + kind + "PixPat returns the caller's handle")
        p.emit(0x807E0000 | (base + 28), 0x80630000, 0xA0630000)
        p.expect(2, "SetPort" + kind + "PixPat copies an RGB pattern")
        direct_set = "SetPort" + kind + "PixPatDirect"
        probe.call(direct_set, [port, slot(24)])
        if kind == "Fill":
            p.emit(0x807E0000 | base, 0x80630000 | field)
        else:
            probe.call("GetPort" + kind + "PixPatDirect", [port])
        p.emit(0x809E0000 | (base + 24))
        p.equal(direct_set + " preserves handle identity")
        probe.call(direct_set, [port, slot(20)])
    probe.call("DisposePixPat", [slot(24)])
    probe.call("DisposePixPat", [slot(28)])

    probe.call("GetQDGlobalsArrow", [ptr(40)])
    cursor = probe.raw(bytes((0x49, 0x5A, 0x23, 0xF0)) + bytes(64))
    probe.call("SetQDGlobalsArrow", [cursor])
    probe.call("GetQDGlobalsArrow", [ptr(108)])
    word(108, 0x495A23F0, "SetQDGlobalsArrow updates the guest cursor")
    probe.call("SetQDGlobalsArrow", [ptr(40)])
    probe.call("DisposeGWorld", [port])
    region_rectangle_operations(probe, base)
    borrowed_gworld_operations(probe, base)
    textension_object_operations(probe, base)


def region_rectangle_operations(probe, base):
    p = probe.p
    ptr = lambda offset: probe.ptr(base + offset)
    slot = lambda offset: probe.slot(base + offset)

    def save(name, arguments, offset):
        probe.call(name, arguments)
        p.nonzero(name + " returns a live object")
        p.emit(0x907E0000 | (base + offset))

    def check_word(offset, value, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(value, label)

    # Append message, handle, and rectangle to the buffer in refCon. Return
    # the selected error on parse only, so init and termination stay observable.
    routine = probe.far_raw(bytes(8))
    p.routines.append(
        (
            routine[1],
            [
                0x80E60000,  # lwz r7, 0(r6)
                0x90670000,  # stw r3, 0(r7)
                0x90870004,  # stw r4, 4(r7)
                0x39070010,  # addi r8, r7, 16
                0x91060000,  # stw r8, 0(r6)
                0x2C050000,  # cmpwi r5, 0
                0x4182001C,  # beq done
                0x81050000,  # lwz r8, 0(r5)
                0x81250004,  # lwz r9, 4(r5)
                0x91070008,  # stw r8, 8(r7)
                0x9127000C,  # stw r9, 12(r7)
                0x80660004,  # lwz r3, 4(r6)
                0x4E800020,  # blr
                0x38600000,  # done: li r3, 0
                0x4E800020,
            ],
        ),
    )
    save("NewRegionToRectsUPP", [routine], 176)
    callback = slot(176)
    save("NewRgn", [], 0)
    save("NewRgn", [], 4)
    region, scratch = slot(0), slot(4)
    rectangles = [(2, 3, 6, 9), (2, 11, 6, 15), (9, 4, 12, 14)]
    for top, left, bottom, right in rectangles:
        probe.call("SetRectRgn", [scratch, left, top, right, bottom])
        probe.call("UnionRgn", [region, scratch, region])

    def check_parse(direction, side, wanted, error=0, complete=True):
        p.emit(0x387E0000 | (base + 32), 0x907E0000 | (base + 16))
        p.emit(0x38600000 | (error & 0xFFFF), 0x907E0000 | (base + 20))
        probe.call("QDRegionToRects", [region, direction, callback, ptr(16)], side)
        p.expect(0 if complete else error, "QDRegionToRects returns the relevant callback status")
        check_word(32, 1, "region parser receives an initialize message")
        check_word(36, 0, "initialize has a null region argument")
        for index, (top, left, bottom, right) in enumerate(wanted):
            offset = 48 + index * 16
            check_word(offset, 2, "region parser receives one parse message per rectangle")
            p.emit(0x807E0000 | (base + offset + 4), 0x809E0000 | base)
            p.equal("parse message receives the original region handle")
            check_word(offset + 8, (top << 16) | left, "parser rectangle top and left")
            check_word(offset + 12, (bottom << 16) | right, "parser rectangle bottom and right")
        end = 48 + len(wanted) * 16
        if complete:
            check_word(end, 3, "region parser receives a termination message")
            check_word(end + 4, 0, "termination has a null region argument")
            end += 16
        p.emit(0x807E0000 | (base + 16))
        p.pointer(base + end, "region parser callback count is exact")

    for side in (0, 1):
        for direction, order in ((5, (0, 1, 2)), (9, (1, 0, 2)), (6, (2, 0, 1)), (10, (2, 1, 0))):
            check_parse(direction, side, [rectangles[index] for index in order])
        check_parse(5, side, rectangles[:1], -50, complete=False)
        probe.call("QDRegionToRects", [region, 3, callback, ptr(16)], side)
        p.expect(-50, "both directions along one axis is invalid")
        probe.call("QDRegionToRects", [0, 5, callback, ptr(16)], side)
        p.expect(-50, "a null region is invalid")

    # A claimed payload larger than the handle is rejected before scanning.
    p.emit(0x809E0000 | base, 0x80840000, 0xA0640000, 0x907E0000 | (base + 24))
    p.emit(0x38607FFE, 0xB0640000)
    check_parse(5, 0, [], -50, complete=False)
    p.emit(0x809E0000 | base, 0x80840000, 0x807E0000 | (base + 24), 0xB0640000)

    probe.call("SetEmptyRgn", [region])
    for side in (0, 1):
        check_parse(5, side, [])
    probe.call("SetRectRgn", [region, 3, 2, 9, 6])
    for side in (0, 1):
        check_parse(10, side, rectangles[:1])
        check_parse(10, side, rectangles[:1], -50)
    probe.call("DisposeRgn", [scratch])
    probe.call("DisposeRgn", [region])
    probe.call("DisposeRegionToRectsUPP", [callback])


def borrowed_gworld_operations(probe, base):
    p = probe.p
    ptr = lambda offset: probe.ptr(base + offset)
    slot = lambda offset: probe.slot(base + offset)
    world, pixels = slot(0), slot(4)
    probe.call("GetGWorld", [ptr(12), ptr(24)])

    def expected(name, arguments, value, side=0):
        probe.call(name, arguments, side)
        p.expect(value, name + " result")

    def save(name, arguments, offset):
        probe.call(name, arguments)
        p.nonzero(name + " returns a live object")
        p.emit(0x907E0000 | (base + offset))

    p.emit(0x3C600001, 0x60630002, 0x907E0000 | (base + 16))
    p.emit(0x3C600003, 0x60630006, 0x907E0000 | (base + 20))

    def arguments(depth, row_bytes):
        return [ptr(0), depth, ptr(16), 0, 0, 0, ptr(64), row_bytes]

    expected("NewGWorldFromPtr", arguments(32, 12), -50)
    for side in (0, 1):
        expected("NewGWorldFromPtr", arguments(24, 20), -157, side)
        expected("NewGWorldFromPtr", arguments(32, 20), 0, side)
        save("GetGWorldPixMap", [world], 4)
        expected("GetPixRowBytes", [pixels], 20)
        probe.call("GetPixBaseAddr", [pixels])
        p.pointer(base + 64, "GetPixBaseAddr returns the borrowed pixel buffer before locking")
        save("GetGWorldDevice", [world], 8)
        p.emit(0x807E0000 | (base + 8), 0x80630000, 0x80630016, 0x80630000, 0x80630000)
        p.pointer(base + 64, "the owned GDevice shares the borrowed pixel buffer")

        expected("NewGWorld", [ptr(28), 32, ptr(16), 0, slot(8), 2], 0)
        save("GetGWorldPixMap", [slot(28)], 32)
        expected("LockPixels", [slot(32)], 1)
        probe.call("GetPixBaseAddr", [slot(32)])
        p.emit(0x389E0000 | (base + 64), 0x7C632278)
        p.nonzero("a regular world using the same device still owns separate pixels")
        probe.call("DisposeGWorld", [slot(28)])

        expected("NewGWorldFromPtr", [ptr(28), 32, ptr(16), 0, slot(8), 2, ptr(128), 20], 0, side)
        save("GetGWorldPixMap", [slot(28)], 32)
        probe.call("GetPixBaseAddr", [slot(32)])
        p.pointer(base + 128, "a borrowed world can use an existing GDevice")
        probe.call("DisposeGWorld", [slot(28)])
        p.emit(0x807E0000 | (base + 8), 0x80630000, 0x80630016, 0x80630000, 0x80630000)
        p.pointer(base + 64, "disposing a world leaves its borrowed device untouched")

        expected("LockPixels", [pixels], 1)
        expected("LockPixels", [pixels], 1)
        probe.call("UnlockPixels", [pixels])
        expected("GetPixelsState", [pixels], 128)
        probe.call("UnlockPixels", [pixels])
        probe.call("AllowPurgePixels", [pixels])
        expected("GetPixelsState", [pixels], 0)
        probe.call("GetPixBaseAddr", [pixels])
        p.pointer(base + 64, "unlock and purge requests preserve the caller's base pointer")

        p.emit(0x3C605566, 0x60637788, 0x907E0000 | (base + 64), 0x907E0000 | (base + 80))
        probe.call("SetGWorld", [world, slot(8)])
        probe.call("PaintRect", [ptr(16)])
        probe.call("SetGWorld", [slot(12), slot(24)])
        probe.call("DisposeGWorld", [world])
        p.emit(0x807E0000 | (base + 64), 0x6C635566, 0x68637788)
        p.nonzero("QuickDraw paints directly into the caller's pixels")
        p.emit(0x807E0000 | (base + 80))
        p.expect(0x55667788, "drawing and disposal preserve the caller's row padding")


def textension_unicode_key_operations(probe, base, version_vector):
    p = probe.p
    document = probe.far_raw(bytes(6))
    editor = probe.far_raw(bytes(0x5E))
    methods = probe.far_raw(bytes(0x24))
    translate = probe.far_raw(bytes(8))
    event = probe.far_raw(struct.pack(">HIIIH", 3, 0x1251, 0, 0, 0))
    unicode_text = probe.far_raw(b"\x00Q" + bytes(50))
    p.routines.append(
        (
            translate[1],
            [
                0x80E40002,
                0x90E20000 | (base + 112),
                0xA0E40000,
                0x90E20000 | (base + 116),
                0x38E00001,
                0xB0E60044,
                0x38600000,
                0x4E800020,
            ],
        )
    )
    probe.arguments((document, editor, methods, translate), 0)
    p.emit(0x90830002, 0x90A40000, 0x90C50020, 0x38E0FFFF, 0x90E40056)
    for side in (0, 1):
        probe.call("TXNPsuedoUnicodeKeyDown", [0, event, unicode_text, 52], side)
        p.expect(-50, "Unicode key input requires a document")
        probe.call("TXNPsuedoUnicodeKeyDown", [document, event, unicode_text, 52], side)
        p.expect(-50, "Unicode key input requires Unicode Textension mode")

    probe.arguments((version_vector,), 0)
    p.emit(0x80630004, 0x8063FC84, 0x907E0000 | (base + 88))
    p.emit(0x88830000, 0x989E0000 | (base + 92), 0x38800001, 0x98830000)
    for side in (0, 1):
        for key_event, message in ((event, 0x1251), (0, 0x51)):
            probe.call("TXNPsuedoUnicodeKeyDown", [document, key_event, unicode_text, 52], side)
            p.expect(0, "the inline translation path ignores text beyond its capacity")
            p.emit(0x807E0000 | (base + 112))
            p.expect(message, "the translator receives the supplied or synthesized key message")
            p.emit(0x807E0000 | (base + 116))
            p.expect(3, "the translator receives a keyDown event")
    p.emit(0x807E0000 | (base + 88), 0x889E0000 | (base + 92), 0x98830000)


def textension_scrap_edits(probe, base, scrap, methods):
    p = probe.p
    document = probe.far_raw(bytes(0x138))
    editor = probe.far_raw(bytes(0x3E))
    layout = probe.far_raw(bytes(0x34))
    protected_layout = probe.far_raw(bytes(0xE))
    selection = probe.far_raw(bytes(0x54))
    clipboard_data = probe.far_raw(bytes(4))
    data_methods = probe.far_raw(bytes(8))
    reset = probe.far_raw(bytes(8))
    copy = probe.far_raw(bytes(8))
    p.routines.extend(
        (
            (reset[1], [0x38600000, 0x4E800020]),
            (
                copy[1],
                [
                    0x90820000 | (base + 96),
                    0x90A20000 | (base + 100),
                    0x90C20000 | (base + 104),
                    0x90E20000 | (base + 108),
                    0x91020000 | (base + 112),
                    0x91220000 | (base + 116),
                    0x3860FFDB,
                    0x4E800020,
                ],
            ),
        )
    )
    probe.arguments((scrap, clipboard_data, data_methods, reset, methods, copy), 0)
    p.emit(0x90830006, 0x90A40000, 0x90C50004, 0x91070004)
    probe.arguments((document, editor, layout, selection), 0)
    p.emit(0x90830002, 0x90A40020, 0x90C40024)
    p.emit(0x38E00011, 0x90E40010, 0x38E00022, 0x90E40030, 0x38E00033, 0x90E4000C)
    probe.arguments((layout, protected_layout), 0)
    p.emit(0x90830030)
    for side in (0, 1):
        for name in ("TXNCopy", "TXNCut", "TXNPaste"):
            probe.call(name, [0], side)
            p.expect(-50, name + " rejects a missing document")
        probe.arguments((selection,), 0)
        p.emit(0x38800002, 0x90830048, 0x9083004E)
        probe.call("TXNCopy", [document], side)
        p.expect(-108, "empty copy returns the shared scrap status")
        probe.arguments((selection,), 0)
        p.emit(0x38800005, 0x9083004E)
        probe.arguments((protected_layout,), 0)
        p.emit(0x38800001, 0x9083000A)
        probe.call("TXNCopy", [document], side)
        p.expect(-22014, "copy rejects protected layouts")
        probe.arguments((protected_layout, scrap), 0)
        p.emit(0x38A00000, 0x90A3000A, 0x98A40015)
        probe.call("TXNCopy", [document], side)
        p.expect(-37, "copy returns the guest scrap writer's error")
        probe.arguments((scrap,), 0)
        p.emit(0x88630015)
        p.expect(1, "copy marks the private scrap as populated")
        for index, expected in enumerate((2, 3, 0x11, 0x22, 0x33, 7)):
            p.emit(0x807E0000 | (base + 96 + index * 4))
            p.expect(expected, "copy passes the range and editor buffers to the guest scrap writer")
        probe.arguments((document,), 0)
        p.emit(0x38800001, 0x988300A0)
        for name in ("TXNCut", "TXNPaste"):
            probe.call(name, [document], side)
            p.expect(-108, name + " leaves a read-only document at the shared scrap status")
        probe.arguments((document,), 0)
        p.emit(0x38800000, 0x988300A0)


def textension_scrap_operations(probe, base):
    p = probe.p
    ptr = lambda offset: probe.ptr(base + offset)
    slot = lambda offset: probe.slot(base + offset)

    def check(name, args, expected, side=0):
        probe.call(name, args, side)
        p.expect(expected, name + " result")

    library = probe.far_raw(b"\x0dTextension_CL")
    symbol = probe.far_raw(b"\x15TXNVersionInformation")
    record = probe.far_raw(bytes(0x2C))
    methods = probe.far_raw(bytes(0x3C))
    flavor_method = probe.far_raw(bytes(8))
    export_method = probe.far_raw(bytes(8))
    import_method = probe.far_raw(bytes(8))
    p.routines.extend(
        (
            (flavor_method[1], [0x9083000A, 0x38600000, 0x4E800020]),
            (export_method[1], [0x38800000, 0x9083000A, 0x3860FFCE, 0x4E800020]),
            (import_method[1], [0x38800000, 0x9083000A, 0x38600000, 0x4E800020]),
        )
    )
    probe.arguments((record, methods, flavor_method, export_method, import_method), 0)
    p.emit(0x90830000, 0x90A40038, 0x90C4001C, 0x90E4002C)
    p.emit(0x38A08000, 0xB0A30024, 0xB0A30026, 0xB0A3002A)
    check("GetSharedLibrary", [library, 0x70777063, 1, ptr(64), 0, 0], 0)
    check("FindSymbol", [slot(64), symbol, ptr(68), 0], 0)
    p.emit(0x807E0000 | (base + 68), 0x80630004)
    p.emit(0x38833D2C, 0x909E0000 | (base + 80), 0x80A40000, 0x90BE0000 | (base + 84))
    p.emit(0x38A0FF94, 0x90A40000)
    p.emit(0x38633DFC, 0x907E0000 | (base + 72))
    p.emit(0x80830000, 0x909E0000 | (base + 76))
    probe.arguments((record, slot(72)), 0)
    p.emit(0x90640000)
    for side in (0, 1):
        probe.arguments((record,), 0)
        p.emit(0x38808000, 0xB083002A)
        check("TXNIsScrapPastable", [], 1, side)
        probe.arguments((record,), 0)
        p.emit(0x8063000A)
        p.expect(0x54455854, "a changed generation queries the public TEXT format")
        for name, expected in (("TXNConvertToPublicScrap", -50), ("TXNConvertFromPublicScrap", -108)):
            check(name, [], expected, side)
            check("TXNIsScrapPastable", [], 1, 1 - side)
            probe.arguments((record,), 0)
            p.emit(0x8063000A)
            p.expect(0, "an unchanged generation returns cached availability without querying again")
        probe.arguments((record,), 0)
        p.emit(0xA0800968, 0xB0830024, 0xB0830026)
        check("TXNIsScrapPastable", [], 0, side)
        probe.arguments((record,), 0)
        p.emit(0x38800001, 0x9083000A)
        check("TXNIsScrapPastable", [], 1, side)
        probe.arguments((record,), 0)
        p.emit(0x38808000, 0xB0830024, 0xB0830026)
    textension_scrap_edits(probe, base, record, methods)
    p.emit(0x807E0000 | (base + 76), 0x809E0000 | (base + 72), 0x90640000)
    p.emit(0x807E0000 | (base + 84), 0x809E0000 | (base + 80), 0x90640000)
    textension_unicode_key_operations(probe, base, slot(68))
    check("CloseConnection", [ptr(64)], 0)


def textension_object_operations(probe, base):
    p = probe.p
    ptr = lambda offset: probe.ptr(base + offset)
    slot = lambda offset: probe.slot(base + offset)
    window, document = slot(0), slot(4)

    def check(name, arguments, expected, side=0):
        probe.call(name, arguments, side)
        p.expect(expected, name + " result")

    def word(offset, value, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(value, label)

    def compare(name, arguments):
        probe.call(name, arguments, 1)
        p.emit(0x907E0000 | (base + 64))
        probe.call(name, arguments)
        p.emit(0x809E0000 | (base + 64))
        p.equal(name + " agrees with saved Textension")

    def history(name, available=None):
        probe.call(name, [document, ptr(48)], 1)
        p.emit(0x907E0000 | (base + 100))
        probe.call(name, [document, ptr(52)])
        p.emit(0x809E0000 | (base + 100))
        p.equal(name + " shares availability with saved Textension")
        if available is not None:
            p.expect(available, name + " reflects the recent edit")
        p.emit(0x807E0000 | (base + 48), 0x809E0000 | (base + 52))
        p.equal(name + " shares the saved action key")

    routine = probe.far_raw(bytes(8))
    p.routines.append((routine[1], [0x38600000, 0x4E800020]))
    # The Textension initializer queries this same component registry.
    p.emit(0x38000000, 0x901E0000 | (base + 104))
    p.emit(0x3C607473, 0x60637663, 0x907E0000 | (base + 68), 0x907E0000 | (base + 80))
    for offset in (84, 88, 92, 96):
        p.emit(0x901E0000 | (base + offset))
    check("GetServiceList", [1, ptr(68), ptr(104), ptr(108)], 0)
    p.emit(0x807E0000 | (base + 104))
    p.nonzero("the empty text service list has a real handle")
    p.emit(0x80630000, 0xA0630000)
    p.expect(0, "there are initially no registered text services")
    p.emit(0x3C600254, 0x60637800, 0x907E0000 | (base + 112))
    check("PtrToHand", [ptr(112), ptr(116), 3], 0)
    probe.call("RegisterComponent", [ptr(80), routine, 0, slot(116), 0, 0])
    p.nonzero("register a test text service component")
    p.emit(0x907E0000 | (base + 100))
    probe.call("DisposeHandle", [slot(116)])
    for side in (0, 1):
        check("GetServiceList", [1, ptr(68), ptr(104), ptr(108)], 0, side)
        p.emit(0x809E0000 | (base + 104), 0x80840000, 0xA0640000)
        p.expect(1, "the service list enumerates registered text-service components")
        p.emit(0x807E0000 | (base + 104), 0x80630000, 0x80630002, 0x809E0000 | (base + 100))
        p.equal("the service list preserves the component token")
        p.emit(0x807E0000 | (base + 104), 0x80630000, 0x80630006)
        p.expect(0x02547800, "the service record carries the registered Pascal name")
        probe.call("GetComponentListModSeed")
        p.emit(0x809E0000 | (base + 108))
        p.equal("the text service seed matches the component registry")
    check("UnregisterComponent", [slot(100)], 0)
    check("GetServiceList", [1, ptr(68), ptr(104), ptr(108)], 0)
    p.emit(0x807E0000 | (base + 104), 0x80630000, 0xA0630000)
    p.expect(0, "the service list no longer includes an unregistered component")
    check("GetServiceList", [1, ptr(84), ptr(104), ptr(108)], -2518)
    probe.call("DisposeHandle", [slot(104)])

    check("TXNInitTextension", [0, 0, 8], 0)
    for side in (0, 1):
        check("TXNInitTextension", [0, 0, 8], -22012, side)
        check("TXNVersionInformation", [ptr(60)], 0x01318000, side)
        word(60, 1, "version features advertise the default layout engine")
    textension_scrap_operations(probe, base)
    p.emit(0x3C60002D, 0x60630019, 0x907E0000 | (base + 16))  # window top, left
    p.emit(0x3C600103, 0x6063010B, 0x907E0000 | (base + 20))  # window bottom, right
    p.emit(0x38000000, 0x901E0000 | (base + 24))  # empty Pascal window title
    probe.call("NewCWindow", [0, ptr(16), ptr(24), 0, 8, -1, 0, 0])
    p.nonzero("allocate a window for the real Textension object")
    p.emit(0x907E0000 | base)
    p.emit(0x3C600008, 0x6063000A, 0x907E0000 | (base + 16))  # frame top, left
    p.emit(0x3C60004C, 0x60630080, 0x907E0000 | (base + 20))  # frame bottom, right
    for side in (0, 1):
        p.emit(0x38005A5A, 0x901E0000 | (base + 8))
        check("TXNNewObject", [0, window, ptr(16), 16, 1, 0x54455854, 0, 0, ptr(8), 0], -50, side)
        word(8, 0x5A5A, "a missing document output leaves the optional frame output untouched")
    check("TXNNewObject", [0, window, ptr(16), 16, 1, 0x54455854, 0, ptr(4), ptr(8), 0x12345678], 0)
    p.emit(0x807E0000 | (base + 4), 0x8063014C)
    p.expect(0x12345678, "document creation preserves the last stacked argument")
    p.emit(0x807E0000 | (base + 4), 0x38800000, 0x9083014C)
    for side in (0, 1):
        probe.call("TXNTerminateTextension", [], side)
        check("TXNInitTextension", [0, 0, 8], -22012, 1 - side)
        check("TXNIsObjectAttachedToWindow", [document], 1, side)
        check("TXNIsObjectAttachedToSpecificWindow", [document, window, ptr(12)], 0, side)
        p.emit(0x887E0000 | (base + 12))
        p.expect(1, "the Textension object is attached to its owning window")
        check("TXNIsObjectAttachedToSpecificWindow", [document, 0, ptr(12)], 0, side)
        p.emit(0x887E0000 | (base + 12))
        p.expect(0, "a null window is not the owner")
        probe.call("GetPort", [ptr(48)])
        check("TXNAttachObjectToWindow", [document, 0, 1], 0, side)
        check("TXNIsObjectAttachedToWindow", [document], 0, 1 - side)
        check("TXNAttachObjectToWindow", [document, window, 1], 0, side)
        check("TXNIsObjectAttachedToSpecificWindow", [document, window, ptr(12)], 0, 1 - side)
        p.emit(0x887E0000 | (base + 12))
        p.expect(1, "reattachment updates the owning window and layout flag")
        probe.call("GetPort", [ptr(52)])
        p.emit(0x807E0000 | (base + 48), 0x809E0000 | (base + 52))
        p.equal("attaching the document preserves the active QuickDraw port")
        probe.call("TXNGetViewRect", [document, ptr(28)], side)
        word(28, 0x0008000A, "Textension returns the frame's top and left")
        word(32, 0x004C0080, "Textension returns the frame's bottom and right")
        check("TXNDataSize", [document], 0, side)
        check("TXNIsSelectionEmpty", [document], 1, side)
        check("TXNGetLineCount", [document, ptr(36)], 0, side)
        word(36, 1, "an empty document contains one line")
        check("TXNGetLineMetrics", [document, 0xFFFFFFFF, ptr(40), ptr(44)], -50, side)
        p.emit(0x807E0000 | (base + 4), 0x8063013C, 0x68630001, 0x907E0000 | (base + 64))
        p.emit(0x380005A5, 0x901E0000 | (base + 52))
        check("TXNGetReferencedData", [document, slot(64), 0, ptr(52)], -50, side)
        word(52, 0x5A5, "a mismatched reference leaves the caller's handle slot unchanged")
        p.emit(0x387E0000 | (base + 80), 0x907E0000 | (base + 76))
        p.emit(0x38601234, 0xB07E0000 | (base + 86), 0x38605678, 0xB07E0000 | (base + 88))
        check("TXNCountLinesToPrint", [ptr(76)], 0x12345678, side)
        check("TXNCountElements", [document, 0, 0x7FFFFFFF, 0x63686120, ptr(36)], 0, side)
        word(36, 0, "an empty document contains no character elements")
        check("TXNFindElementByIndex", [document, 0, 0, 0x63686120, 0, ptr(40), ptr(44)], 0, side)
        word(40, 0, "an empty document returns zero for the element start")
        word(44, 0, "an empty document returns zero for the element end")
    compare("TXNGetSleepTicks", [document])
    history("TXNCanUndo")
    history("TXNCanRedo")
    p.emit(0x3C60410D, 0x60634243, 0x907E0000 | (base + 68))
    check("TXNSetData", [document, 0x54455854, ptr(68), 4, 0, 0x7FFFFFFF], 0)
    for side in (0, 1):
        check("TXNSetData", [0, 0x54455854, ptr(68), 4, 0, 4], -50, side)
        check("TXNSetData", [document, 0x54455854, ptr(68), 4, 3, 1], -50, side)
        check("TXNSetData", [document, 0x54455854, 0, 0, 2, 2], 0, side)
        check("TXNDataSize", [document], 4)
        check("TXNSetSelection", [document, 1, 3], 0)
        check("TXNSetData", [document, 0x54455854, 0, 0, 1, 3], 0, side)
        check("TXNGetData", [document, 0, 0x7FFFFFFF, ptr(52)], 0, 1 - side)
        check("GetHandleSize", [slot(52)], 2)
        p.emit(0x807E0000 | (base + 52), 0x80630000, 0xA0630000)
        p.expect(0x4143, "a null data pointer removes the selected bytes")
        probe.call("DisposeHandle", [slot(52)])
        check("TXNSetData", [document, 0x54455854, ptr(68), 4, 0, 0x7FFFFFFF], 0, side)
    find_bytes = probe.far_raw(b"BC")
    absent_bytes = probe.far_raw(b"ZZ")
    probe.arguments((find_bytes,), 0)
    p.emit(
        0x907E0000 | (base + 136),
        0x38800002,
        0x909E0000 | (base + 140),
        0x38800000,
        0x909E0000 | (base + 144),
    )
    family_name = probe.far_raw(bytes(256))
    probe.call("GetFontName", [3, family_name])
    probe.call("UpgradeScriptInfoToTextEncoding", [0, -128, -128, family_name, ptr(144)])
    p.expect(0, "use the text encoding of the document's Roman font")

    def find(first, last, expected, side):
        check("TXNFind", [document, ptr(136), 0x54455854, 0, first, last, 0, 0, ptr(40), ptr(44)], expected, side)

    for side in (0, 1):
        find(0, 4, 0, side)
        word(40, 2, "search returns the match start")
        word(44, 4, "search returns the exclusive match end")
        find(3, 1, -50, side)
        word(40, 2, "a reversed search range preserves the start output")
        word(44, 4, "a reversed search range preserves the end output")
        probe.arguments((absent_bytes,), 0)
        p.emit(0x907E0000 | (base + 136))
        find(0, 4, -22008, side)
        word(40, 0xFFFFFFFF, "a miss clears the start output")
        word(44, 0xFFFFFFFF, "a miss clears the end output")
        probe.arguments((find_bytes,), 0)
        p.emit(0x907E0000 | (base + 136))
    find_callback = probe.far_raw(bytes(8))
    p.routines.append(
        (
            find_callback[1],
            [
                0x81610038,  # End offset output pointer.
                0x8181003C,  # Match flag pointer.
                0x80010040,  # Callback refCon is the chosen match length.
                0x910A0000,
                0x7C080214,  # Add the length to the run start.
                0x900B0000,
                0x38000001,
                0x980C0000,
                0x38600000,
                0x4E800020,
            ],
        )
    )
    probe.call("NewTXNFindUPP", [find_callback])
    p.nonzero("allocate a Textension search callback")
    p.emit(0x907E0000 | (base + 148))
    for side in (0, 1):
        check("TXNFind", [document, ptr(136), 0x54455854, 0, 1, 4, slot(148), 2, ptr(40), ptr(44)], 0, side)
        word(40, 1, "the callback receives the active run's start")
        word(44, 3, "the callback receives its stacked output and context arguments")
    probe.call("DisposeTXNFindUPP", [slot(148)])
    check("TXNSetSelection", [document, 3, 1], -50)
    check("TXNSetSelection", [document, 0x7FFFFFFF, 0x7FFFFFFF], 0)
    probe.call("TXNGetSelection", [document, ptr(40), ptr(44)], 1)
    word(40, 4, "the end sentinel resolves to the document length")
    word(44, 4, "the end sentinel collapses the selection")
    check("TXNSetSelection", [document, 0, 999], 0)
    probe.call("TXNGetSelection", [document, ptr(40), ptr(44)], 1)
    word(44, 4, "an oversized selection is clamped before repainting")
    check("TXNSetSelection", [document, 1, 3], 0)
    compare("TXNDataSize", [document])
    compare("TXNGetChangeCount", [document])
    for side in (0, 1):
        check("TXNIsSelectionEmpty", [document], 0, side)
        probe.call("TXNGetSelection", [document, ptr(40), ptr(44)], side)
        word(40, 1, "selection starts after the first character")
        word(44, 3, "selection ends at the third character")
        check("TXNGetLineCount", [document, ptr(36)], 0, side)
        word(36, 2, "a carriage return produces two lines")
    for side in (0, 1):
        check("TXNCountRunsInRange", [document, 0, 0x7FFFFFFF, ptr(36)], 0, side)
        word(36, 1, "the four characters share one style run")
        check(
            "TXNGetIndexedRunInfoFromRange",
            [document, 0, 1, 3, ptr(40), ptr(44), ptr(48), 0, 0],
            0,
            side,
        )
        word(40, 0, "a partial query returns the run's actual start")
        word(44, 4, "a partial query returns the run's actual end")
        word(48, 0x54455854, "the run contains MacRoman text")
        check(
            "TXNGetIndexedRunInfoFromRange",
            [document, 20, 0, 4, ptr(40), ptr(44), ptr(48), 0, 0],
            -22011,
            side,
        )
        word(48, 0x54455854, "a missing run leaves the output record intact")
        check("TXNGetData", [document, 0, 0x7FFFFFFF, ptr(52)], 0, side)
        check("GetHandleSize", [slot(52)], 4)
        p.emit(0x807E0000 | (base + 52), 0x80630000, 0x80630000)
        p.expect(0x410D4243, "the returned handle is an independent copy of the whole document")
        probe.call("DisposeHandle", [slot(52)])
        check("TXNGetData", [document, -1, -1, ptr(52)], 0, side)
        check("GetHandleSize", [slot(52)], 2)
        p.emit(0x807E0000 | (base + 52), 0x80630000, 0xA0630000)
        p.expect(0x0D42, "the selection sentinel copies only the selected characters")
        probe.call("DisposeHandle", [slot(52)])
    check("TXNGetData", [document, 3, 1, ptr(52)], -50)
    check("TXNCountRunsInRange", [document, 3, 1, ptr(36)], -50)
    for kind in (0x63686120, 0x636C696E, 0x63706172, 0x63776F72, 0x74737479):
        for side in (0, 1):
            field = 80 + side * 12
            check("TXNCountElements", [document, 0, 0x7FFFFFFF, kind, ptr(field)], 0, side)
            check("TXNFindElementByIndex", [document, 0, 4, kind, 0, ptr(field + 4), ptr(field + 8)], 0, side)
        for offset in (0, 4, 8):
            p.emit(0x807E0000 | (base + 80 + offset), 0x809E0000 | (base + 92 + offset))
            p.equal("element count and bounds agree with saved Textension")
        p.nonzero("the first element has a nonempty range")
        if kind == 0x63776F72:
            word(84, 0, "the first word starts at the first character")
            word(88, 1, "the first word ends before the carriage return")
    check("TXNCountElements", [document, 0, 4, 0x63686120, ptr(36)], 0)
    word(36, 4, "character counting includes the carriage return")
    check("TXNFindElementByIndex", [document, 0, 4, 0x63686120, 2, ptr(40), ptr(44)], 0)
    word(40, 2, "character lookup uses a zero-based index")
    word(44, 3, "character lookup returns a half-open range")
    check("TXNGetActionChangeCount", [document, 3, ptr(48)], 0, 1)
    check("TXNGetActionChangeCount", [document, 3, ptr(52)], 0)
    p.emit(0x807E0000 | (base + 48), 0x809E0000 | (base + 52))
    p.equal("native and saved action counters agree")
    check("TXNClearActionChangeCount", [document, 3], 0)
    check("TXNGetActionChangeCount", [document, 3, ptr(48)], 0, 1)
    word(48, 0, "the real fragment sees the cleared action counters")
    compare("TXNIsTyping", [document])

    # The key event must update the shared undo record used by both implementations.
    check("TXNSetSelection", [document, 0x7FFFFFFF, 0x7FFFFFFF], 0)
    p.emit(0x3C600003, 0x907E0000 | (base + 120), 0x3C60005A, 0x907E0000 | (base + 124))
    p.emit(0x38000000, 0x901E0000 | (base + 128), 0x901E0000 | (base + 132))
    probe.call("TXNKeyDown", [document, ptr(120)])
    check("TXNDataSize", [document], 5)
    history("TXNCanUndo", 1)
    compare("TXNIsTyping", [document])
    probe.call("TXNUndo", [document])
    check("TXNDataSize", [document], 4)
    history("TXNCanRedo", 1)
    probe.call("TXNRedo", [document])
    check("TXNDataSize", [document], 5, 1)

    for side in (0, 1):
        check("TXNSetSelection", [document, 1, 3], 0)
        probe.call("TXNSelectAll", [document], side)
        probe.call("TXNGetSelection", [document, ptr(40), ptr(44)], 1 - side)
        word(40, 0, "Select All includes the first character")
        word(44, 5, "Select All includes the last character after redo")
        check("TXNSetSelection", [document, 0x7FFFFFFF, 0x7FFFFFFF], 0)
        probe.call("TXNKeyDown", [document, ptr(120)], side)
        check("TXNDataSize", [document], 6)
        history("TXNCanUndo", 1)
        probe.call("TXNUndo", [document], side)
        history("TXNCanRedo", 1)
        probe.call("TXNRedo", [document], 1 - side)
        check("TXNDataSize", [document], 6)
        probe.call("TXNUndo", [document], 1 - side)
        probe.call("TXNEmptyUndoStack", [document], side)
        history("TXNCanUndo")
        history("TXNCanRedo", 0)
        probe.call("TXNIdle", [document], side)
        check("TXNDataSize", [document], 5, 1 - side)

    background = probe.raw(bytes.fromhex("00000001123456789ABC"))
    invalid_background = probe.raw(bytes(12))
    world_bounds = probe.raw(struct.pack(">4h", 0, 0, 96, 144))
    check("NewGWorld", [ptr(80), 32, world_bounds, 0, 0, 0], 0)
    probe.call("GetGWorld", [ptr(84), ptr(96)])
    probe.call("GetWindowPort", [window])
    p.emit(0x907E0000 | (base + 88))
    probe.call("SetPort", [slot(88)])
    # Reuse the key event now that the history checks are complete.
    changed_view, original_view = ptr(112), ptr(120)
    first_point, second_point = ptr(132), ptr(136)
    first_offset, second_offset = ptr(140), ptr(144)
    for where, top, left, bottom, right in ((112, 16, 22, 80, 138), (120, 8, 10, 76, 128)):
        p.emit(0x3C600000 | top, 0x60630000 | left, 0x907E0000 | (base + where))
        p.emit(0x3C600000 | bottom, 0x60630000 | right, 0x907E0000 | (base + where + 4))
    check("TXNOffsetToPoint", [document, 6, first_point], -50)
    for side in (0, 1):
        point = (first_point, second_point)[side]
        offset = (first_offset, second_offset)[side]
        probe.call("TXNSetViewRect", [document, changed_view], side)
        p.emit(0x807E0000 | (base + 4), 0x80630002, 0x80630020, 0x80630036)
        p.expect(0x00100016, "setting the view updates the layout's top and left")
        probe.call("TXNShowSelection", [document, 1], side)
        check("TXNOffsetToPoint", [document, 0, point], 0, 1 - side)
        probe.call("TXNPointToOffset", [document, probe.slot(point[1]), offset], 1 - side)
        p.emit(0x907E0000 | (base + 100 + side * 4))
        probe.call("TXNSetViewRect", [document, original_view], side)
        p.emit(0x807E0000 | (base + 4), 0x80630002, 0x80630020, 0x80630036)
        p.expect(0x0008000A, "restoring the view resets the layout's top and left")
    p.emit(0x807E0000 | first_point[1], 0x809E0000 | second_point[1])
    p.equal("native and saved views place the first character at the same point")
    p.emit(0x807E0000 | first_offset[1], 0x809E0000 | second_offset[1])
    p.equal("native and saved hit testing agree about the offset")
    p.emit(0x807E0000 | (base + 100), 0x809E0000 | (base + 104))
    p.equal("native and saved hit testing agree about the position status")
    outside_click = probe.far_raw(struct.pack(">HIIhhH", 1, 0, 0, -1000, -1000, 0))
    print_state = probe.far_raw(bytes(32))
    for side in (0, 1):
        check("TXNSetBackground", [document, invalid_background], -50, side)
        check("TXNSetBackground", [document, background], 0, side)
        check("TXNEchoMode", [document, ord("*"), 0, 1], 0, side)
        probe.call("TXNForceUpdate", [document], side)
        probe.call("TXNDraw", [document, slot(80)], side)
        probe.call("GetPort", [ptr(92)])
        p.emit(0x807E0000 | (base + 92), 0x809E0000 | (base + 88))
        p.equal("drawing offscreen restores the caller's port")
        # TXNDraw restores the port, and SetGWorld restores its device before the next draw.
        probe.call("SetGWorld", [slot(88), slot(96)])
        probe.call("TXNDraw", [document, 0], side)
        probe.call("GetPort", [ptr(92)])
        p.emit(0x807E0000 | (base + 92), 0x809E0000 | (base + 88))
        p.equal("Textension drawing and invalidation restore the caller's port")
        p.emit(
            0x807E0000 | (base + 4),
            0x80630002,
            0x80630020,
            0x8063000A,
            0x80830028,
            0x909E0000 | (base + 112),
            0x80830024,
            0x909E0000 | (base + 116),
        )
        probe.call("TXNDrawInRectangle", [document, original_view], side)
        p.emit(0x807E0000 | (base + 4), 0x80630002, 0x80630020, 0x8063000A, 0x80830028)
        p.emit(0x807E0000 | (base + 112))
        p.equal("drawing within a supplied rectangle restores the line layout's horizontal origin")
        p.emit(0x807E0000 | (base + 4), 0x80630002, 0x80630020, 0x8063000A, 0x80830024)
        p.emit(0x807E0000 | (base + 116))
        p.equal("drawing within a supplied rectangle restores the line layout's vertical origin")
        probe.call("TXNGetSelection", [document, ptr(40), ptr(44)])
        probe.call("GetPort", [ptr(92)])
        probe.call("TXNClick", [document, outside_click], side)
        probe.call("TXNGetSelection", [document, ptr(112), ptr(116)])
        for previous, current in ((40, 112), (44, 116)):
            p.emit(0x807E0000 | (base + previous), 0x809E0000 | (base + current))
            p.equal("clicking outside the frame preserves the current selection")
        probe.call("GetPort", [ptr(112)])
        p.emit(0x807E0000 | (base + 92), 0x809E0000 | (base + 112))
        p.equal("clicking outside the frame restores the caller's port")
        probe.call("SetGWorld", [slot(80), 0])
        check("TXNBeginPrinting", [document, print_state, 120, 80, 1, 2], 0, side)
        probe.call("TXNCountLinesToPrint", [print_state], 1 - side)
        p.nonzero("pagination produces a page for the two-line document")
        probe.call("TXNPrintPage", [document, print_state, 0, 120], side)
        probe.call("TXNEndPrinting", [document, print_state], 1 - side)
        check("TXNDataSize", [document], 5)
        probe.call("SetGWorld", [slot(88), slot(96)])
        check("TXNEchoMode", [document, 0, 0, 0], 0, side)
        probe.call("TXNFocus", [document, 1], side)
        probe.call("TXNFocus", [document, 0], 1 - side)

    drag_methods = probe.far_raw(bytes(12))
    drag_callback = probe.far_raw(bytes(8))
    p.routines.append(
        (
            drag_callback[1],
            [
                0x90620000 | (base + 112),
                0x90820000 | (base + 116),
                0x90A20000 | (base + 120),
                0x90C20000 | (base + 124),
                0x3860FFDB,
                0x4E800020,
            ],
        )
    )
    probe.arguments((drag_callback, drag_methods), 0)
    p.emit(0x90640004, 0x90640008)
    for side in (0, 1):
        for name, arguments, expected in (
            ("TXNDragTracker", [document, slot(8), 0x1234FEDC, window, 0x10203040, 0x101], (0xFEDC, 0x10203040, 1)),
            ("TXNDragReceiver", [document, slot(8), window, 0x10203040, 0x102], (0x10203040, 2)),
        ):
            probe.arguments((drag_methods,), 0)
            p.emit(0x809E0000 | (base + 4), 0x80A400BC, 0x90BE0000 | (base + 104), 0x906400BC)
            probe.call(name, arguments, side)
            p.emit(0x809E0000 | (base + 4), 0x80BE0000 | (base + 104), 0x90A400BC)
            p.expect(-37, name + " returns the text drag handler's status")
            p.emit(0x809E0000 | (base + 4), 0x388400BC, 0x807E0000 | (base + 112))
            p.equal("drag dispatch receives the document's embedded drag context")
            for index, value in enumerate(expected):
                word(116 + index * 4, value, name + " dispatch preserves arguments and narrows the ABI fields")

    probe.call("SetGWorld", [slot(84), slot(96)])
    probe.call("DisposeGWorld", [slot(80)])

    # Set a known default before asking both implementations to copy the array.
    p.emit(0x38000000, 0x901E0000 | (base + 136), 0x901E0000 | (base + 144), 0x901E0000 | (base + 148))
    p.emit(0x3C60000C, 0x907E0000 | (base + 140))
    size_16 = probe.far_raw(struct.pack(">IIIBB", 0, 16 << 16, 0, 0, 0))
    check("TXNSetSelection", [document, 1, 3], 0)
    for side in (0, 1):
        check("TXNSetFontDefaults", [0, 1, size_16], -50, side)
        check("TXNSetFontDefaults", [document, 1, 0], -50, side)
        check("TXNSetFontDefaults", [document, 1, size_16], 0, side)
        check("TXNGetFontDefaults", [document, ptr(76), ptr(152)], 0, 1 - side)
        word(156, 0x00100000, "both implementations see the new default size")
        probe.call("TXNGetSelection", [document, ptr(40), ptr(44)], 1 - side)
        word(40, 1, "font defaults preserve the start of the existing selection")
        word(44, 3, "font defaults preserve the end of the existing selection")
        check("TXNSetFontDefaults", [document, 1, ptr(136)], 0, side)
    for side in (0, 1):
        check("TXNGetFontDefaults", [document, ptr(76), ptr(152 + side * 16)], 0, side)
        word(76, 1, "the real document has one font default")
        word(156 + side * 16, 0x000C0000, "the copied default preserves its Fixed point size")
    for offset in (0, 4, 8):
        p.emit(0x807E0000 | (base + 152 + offset), 0x809E0000 | (base + 168 + offset))
        p.equal("the two font default records agree")

    chicago = probe.raw(b"\x07Chicago")
    for side in (0, 1):
        probe.call("NewMenu", [32000, ptr(24)])
        p.nonzero("create a menu for a Textension font-menu object")
        p.emit(0x907E0000 | (base + 80))
        check("TXNNewFontMenuObject", [slot(80), 32000, 32001, ptr(84)], 0, side)
        check("TXNGetFontMenuHandle", [slot(84), ptr(88)], 0, 1 - side)
        p.emit(0x807E0000 | (base + 80), 0x809E0000 | (base + 88))
        p.equal("both implementations share the same font menu handle")
        menu, font_menu = slot(80), slot(84)
        probe.call("InsertMenu", [menu, 0])
        check("TXNPrepareFontMenu", [document, 0], -50, side)
        check("TXNPrepareFontMenu", [0, font_menu], 0, side)
        check("IsMenuItemEnabled", [menu, 0], 0)
        check("TXNPrepareFontMenu", [document, font_menu], 0, 1 - side)
        check("IsMenuItemEnabled", [menu, 0], 1)
        check("TXNDoFontMenuSelection", [0, font_menu, 32000, 1], -50, side)
        check("TXNDoFontMenuSelection", [document, 0, 32000, 1], -50, side)
        check("TXNDoFontMenuSelection", [document, font_menu, 31999, 1], -50, side)
        check("TXNDoFontMenuSelection", [document, font_menu, 32000, 0xABCDFFFF], -50, side)
        probe.call("AppendMenu", [menu, chicago])
        probe.call("CountMenuItems", [menu])
        p.emit(0x907E0000 | (base + 88))
        check("TXNDoFontMenuSelection", [document, font_menu, 0xABCD7D00, slot(88)], 0, side)
        probe.call("GetItemMark", [menu, slot(88), ptr(92)])
        p.emit(0xA07E0000 | (base + 92))
        p.expect(0x12, "font selection marks the selected item in the live menu")
        check("TXNDisposeFontMenuObject", [slot(84)], 0, 1 - side)
        probe.call("DeleteMenu", [32000])
        probe.call("DisposeMenu", [slot(80)])

    probe.call("NewTXNIncomingDataFilterUPP", [routine])
    p.nonzero("create a data filter UPP")
    p.emit(0x907E0000 | (base + 56))
    filter_upp = slot(56)
    for side in (0, 1):
        check("TXNSetDataFilter", [document, filter_upp, 2], 0, side)
        check("TXNGetDataFilter", [document, ptr(48), ptr(52)], 0, 1 - side)
        p.emit(0x807E0000 | (base + 48), 0x809E0000 | (base + 56))
        p.equal("the filter identity is shared between native and saved Textension")
        word(52, 2, "the recorded filter mask is shared")
        check("TXNSetDataFilter", [document, 0, 2], 0, side)
        check("TXNGetDataFilter", [document, ptr(48), ptr(52)], 0, 1 - side)
        word(48, 0, "the filter can be removed")
        word(52, 0, "removing the filter clears its mask")
    probe.call("DisposeTXNIncomingDataFilterUPP", [filter_upp])
    p.emit(0xA0600968, 0x907E0000 | (base + 100))
    for side in (0, 1):
        check("TXNSetSelection", [document, 1, 3], 0)
        check("TXNClear", [document], 0, side)
        check("TXNDataSize", [document], 3)
        check("TXNGetData", [document, 0, 0x7FFFFFFF, ptr(52)], 0, 1 - side)
        p.emit(0x807E0000 | (base + 52), 0x80630000, 0xA0630000)
        p.expect(0x4143, "clearing the selected text joins the retained characters")
        probe.call("DisposeHandle", [slot(52)])
        probe.call("TXNUndo", [document], 1 - side)
        check("TXNDataSize", [document], 5)
    p.emit(0xA0600968, 0x809E0000 | (base + 100))
    p.equal("clearing a selection preserves the public scrap generation")
    for offset in (136, 152):
        p.emit(0x3C607369, 0x60637A65, 0x907E0000 | (base + offset))
        p.emit(0x38600004, 0x907E0000 | (base + offset + 4))
    p.emit(0x3C600011, 0x6063C000, 0x907E0000 | (base + 144))
    unknown_attribute = probe.far_raw(struct.pack(">4sII", b"????", 4, 0))
    for side in (0, 1):
        check("TXNSetTypeAttributes", [document, 0, ptr(136), 1, 3], -50, side)
        check("TXNSetTypeAttributes", [document, 1, 0, 1, 3], -50, side)
        check("TXNSetTypeAttributes", [document, 1, unknown_attribute, 1, 3], -50, side)
        check("TXNSetTypeAttributes", [document, 1, ptr(136), 1, 3], 0, side)
        check("TXNSetSelection", [document, 1, 3], 0)
        p.emit(0x38000000, 0x901E0000 | (base + 160))
        check("TXNGetContinuousTypeAttributes", [document, ptr(36), 1, ptr(152)], 0, side)
        word(36, 2, "a uniform font size is reported in the continuity mask")
        word(160, 0x00120000, "the continuous size rounds the input Fixed value to a whole point")
        check("TXNGetDataEncoded", [document, -1, -1, ptr(52), 0x54455854], 0, side)
        check("GetHandleSize", [slot(52)], 2)
        p.emit(0x807E0000 | (base + 52), 0x80630000, 0xA0630000)
        p.expect(0x0D42, "encoded TEXT copies the selected bytes in their original encoding")
        probe.call("DisposeHandle", [slot(52)])
        check("TXNCountRunsInRange", [document, 0, 0x7FFFFFFF, ptr(36)], 0, side)
        word(36, 3, "changing two characters splits the original style run")
        check("TXNCountElements", [document, 0, 5, 0x74737479, ptr(36)], 0, side)
        word(36, 3, "element enumeration counts all three style runs")
        check("TXNFindElementByIndex", [document, 0, 5, 0x74737479, 1, ptr(40), ptr(44)], 0, side)
        word(40, 1, "element lookup advances from the first to the middle style")
        word(44, 3, "element lookup returns the middle style's end")
        p.emit(0x38000000, 0x901E0000 | (base + 160))
        check(
            "TXNGetIndexedRunInfoFromRange",
            [document, 1, 0, 0x7FFFFFFF, ptr(40), ptr(44), ptr(48), 1, ptr(152)],
            0,
            side,
        )
        word(40, 1, "the styled run starts at the first changed character")
        word(44, 3, "the styled run ends after the last changed character")
        word(160, 0x00120000, "the stacked attribute argument receives the Fixed font size")
        check("TXNGetData", [document, 0, 0x7FFFFFFF, ptr(52)], 0, side)
        check("GetHandleSize", [slot(52)], 5)
        p.emit(0x807E0000 | (base + 52), 0x80830000, 0x80640000)
        p.expect(0x410D4243, "copying across style boundaries preserves the original bytes")
        p.emit(0x807E0000 | (base + 52), 0x80830000, 0x88640004)
        p.expect(ord("Z"), "the copied handle includes the last run")
        probe.call("DisposeHandle", [slot(52)])
    check("TXNActivate", [0, 0, 1], -50)
    for side in (0, 1):
        for active in (1, 0):
            check("TXNActivate", [document, slot(8), active], 0, side)
            p.emit(0x807E0000 | (base + 4), 0x886300A5)
            p.expect(active, "activation updates the shared document flag")
    probe.call("GetPort", [ptr(84)])
    probe.call("GetWindowPort", [window])
    p.emit(0x907E0000 | (base + 88))
    for side in (0, 1):
        probe.call("TXNSetFrameBounds", [document, 12, 14, 80, 126, slot(8)], side)
        probe.call("TXNGetViewRect", [document, ptr(28)], 1 - side)
        word(28, 0x000C000E, "the frame moves to the requested top and left")
        word(32, 0x0050007E, "the frame exposes the requested bottom and right")
        probe.call("TXNSetFrameBounds", [document, 8, 10, 76, 128, slot(8)], side)
        probe.call("TXNResizeFrame", [document, 99, 31, slot(8)], side)
        probe.call("TXNGetViewRect", [document, ptr(28)], 1 - side)
        word(28, 0x0008000A, "resize keeps the frame origin")
        word(32, 0x0027006D, "resize adds height and width to the frame origin")
        probe.call("SetPort", [slot(84)])
        probe.call("TXNUpdate", [document], side)
        probe.call("GetPort", [ptr(92)])
        p.emit(0x807E0000 | (base + 92), 0x809E0000 | (base + 88))
        p.equal("Textension update draws in the owning window's port")
        probe.call("TXNResizeFrame", [document, 118, 68, slot(8)], side)
    # Replace the interactive tracking trap with a deterministic packed Point result.
    grow_event = probe.far_raw(bytes(16))
    grow_vector = 0x12AC
    p.emit(0x81600000 | grow_vector, 0x917E0000 | (base + 108))
    for packed_size, expected in ((0, 0x00D600F2), (0x006400C8, 0x006400C8)):
        track = probe.far_raw(bytes(8))
        p.routines.append(
            (track[1], [0x3C600000 | (packed_size >> 16), 0x60630000 | (packed_size & 0xFFFF), 0x4E800020])
        )
        for side in (0, 1):
            probe.arguments((track,), side)
            p.emit(0x90600000 | grow_vector)
            probe.call("TXNGrowWindow", [document, grow_event], side)
            p.emit(0x817E0000 | (base + 108), 0x91600000 | grow_vector)
            probe.call("GetWindowPortBounds", [window, ptr(28)])
            word(32, expected, "the grow result controls the owning window dimensions")
            probe.call("SizeWindow", [window, 242, 214, 1])
    # Fix the zoom target so these checks don't depend on the host display size.
    p.emit(0x3C60003C, 0x60630032, 0x907E0000 | (base + 112))
    p.emit(0x3C600104, 0x6063016A, 0x907E0000 | (base + 116))
    probe.call("SetWindowStandardState", [window, ptr(112)])
    for side in (0, 1):
        probe.call("TXNZoomWindow", [document, 8], side)
        probe.call("GetWindowPortBounds", [window, ptr(28)])
        word(32, 0x00C80138, "zoom out applies the window's standard bounds")
        probe.call("TXNZoomWindow", [document, 7], side)
        probe.call("GetWindowPortBounds", [window, ptr(28)])
        word(32, 0x00D600F2, "zoom in restores the window's user bounds")
    probe.call("ShowWindow", [window])
    probe.call("SelectWindow", [window])
    probe.call("SetPort", [slot(88)])
    probe.call("NewRgn")
    p.nonzero("allocate a cursor region")
    p.emit(0x907E0000 | (base + 104))
    p.emit(0x3C600010, 0x60630012, 0x907E0000 | (base + 96))
    probe.call("LocalToGlobal", [ptr(96)])
    mouse_vector = 0x13C8
    p.emit(0x81600000 | mouse_vector, 0x917E0000 | (base + 108))
    for v, h, inside in ((16, 18, 1), (300, 300, 0)):
        mouse = probe.far_raw(bytes(8))
        p.routines.append((mouse[1], [0x3C800000 | v, 0x60840000 | h, 0x90830000, 0x4E800020]))
        for side in (0, 1):
            probe.arguments((mouse,), side)
            p.emit(0x90600000 | mouse_vector)
            probe.call("TXNAdjustCursor", [document, slot(104)], side)
            p.emit(0x817E0000 | (base + 108), 0x91600000 | mouse_vector)
            p.emit(0xA0600B9E)
            p.expect(0, "looking up the I-beam consumes the temporary resource-load flag")
            check("PtInRgn", [slot(96), slot(104)], inside)
            check("PtInRgn", [0x90009000, slot(104)], 1 - inside)
    probe.call("DisposeRgn", [slot(104)])
    probe.call("HideWindow", [window])
    probe.call("SetPort", [slot(84)])
    probe.call("TXNDeleteObject", [0])
    probe.call("TXNDeleteObject", [document])
    # The original fixture has no scrollbars. Exercise both controls on a second frame.
    check("TXNNewObject", [0, window, ptr(16), 28, 1, 0x54455854, 0, ptr(4), ptr(8), 0], 0)
    size_12 = probe.far_raw(struct.pack(">IIIBB", 0, 12 << 16, 0, 0, 0))
    for side, record, size in ((0, size_16, 16), (1, size_12, 12)):
        check("TXNSetFontDefaults", [document, 1, record], 0, side)
        check("TXNGetFontDefaults", [document, ptr(76), ptr(152)], 0, 1 - side)
        word(156, size << 16, "changing the typing default on an empty document preserves the new size")
        check("TXNDataSize", [document], 0)
    for side in (0, 1):
        probe.call("TXNResizeFrame", [document, 119, 69, slot(8)], side)
        p.emit(0x807E0000 | (base + 4), 0x80630002, 0x80630020, 0x8063003A)
        p.expect(0x003E0072, "the viewport excludes both 15-pixel scrollbars")
        probe.call("TXNUpdate", [document], side)
        probe.call("TXNResizeFrame", [document, 118, 68, slot(8)], side)
    probe.call("SetPort", [slot(84)])
    one_character = probe.far_raw(b"Q")
    control_tags = probe.far_raw(b"iopvdcrtdslnslstauin")
    edit_tags = probe.far_raw(b"rfcndcrtdslnslst")
    changed_controls = probe.far_raw(struct.pack(">4I", 0x13579BDF, 0, 0, 0))
    for side in (0, 1):
        check("TXNPageSetup", [0], -50, side)
        check("TXNPrint", [0], -50, side)
        check("TXNRevert", [0], -50, side)
        check("TXNGetTXNObjectControls", [document, 1, 0, ptr(136)], -50, side)
        check("TXNGetTXNObjectControls", [0, 5, control_tags, ptr(136)], 0, side)
        for index, expected in enumerate((0, 1, 1, 1, 0)):
            word(136 + index * 4, expected, "document-less queries return the Textension control defaults")
        check("TXNSetTXNObjectControls", [document, 0, 4, edit_tags, changed_controls], 0, side)
        check("TXNGetTXNObjectControls", [document, 4, edit_tags, ptr(136)], 0, 1 - side)
        for index, expected in enumerate((0x13579BDF, 0, 0, 0)):
            word(136 + index * 4, expected, "the opposite binding reads back the changed controls")
        check("TXNSetTXNObjectControls", [document, 1, 0, edit_tags, changed_controls], 0, 1 - side)
        check("TXNGetTXNObjectControls", [document, 4, edit_tags, ptr(136)], 0, side)
        for index, expected in enumerate((0x13579BDF, 1, 1, 1)):
            word(136 + index * 4, expected, "reset restores the drawing defaults and keeps the reference constant")
        check("TXNSetData", [document, 0x54455854, one_character, 1, 0, 0x7FFFFFFF], 0)
        check("TXNDataSize", [document], 1)
        p.emit(
            0x807E0000 | (base + 4),
            0x888300A0,
            0x989E0000 | (base + 120),
            0x38800001,
            0x988300A0,
        )
        check("TXNGetTXNObjectControls", [document, 5, control_tags, ptr(136)], 0, side)
        word(136, 1, "the control getter returns the live document's read-only flag")
        check("TXNSetTXNObjectControls", [document, 1, 0, edit_tags, changed_controls], -0x1482, side)
        probe.call("TXNRevert", [document], side)
        p.emit(0x809E0000 | (base + 4), 0x88BE0000 | (base + 120), 0x98A400A0)
        p.expect(-0x1482, "revert rejects a read-only document")
        check("TXNDataSize", [document], 1)
        check("TXNRevert", [document], 0, side)
        check("TXNDataSize", [document], 0, 1 - side)
        check("TXNGetChangeCount", [document], 0, 1 - side)
    file_name = probe.far_raw(b"\x12URL buffer fixture")
    check("GetCurrentProcess", [probe.ptr(0x1EF0)], 0)
    p.emit(0x3860003C, 0x907E1F00, 0x387E1F40, 0x907E1F38)
    check("GetProcessInformation", [probe.ptr(0x1EF0), probe.ptr(0x1F00)], 0)
    p.emit(0xA87E1F40, 0x907E1FC0)
    for side in (0, 1):
        check("TXNSetDataFromFile", [document, 0, 0x54455854, 4, 3, 2], -50, side)
        check("HOpenDF", [probe.slot(0x1FC0), probe.slot(0x1F42), file_name, 1, ptr(120)], 0)
        p.emit(0xA87E0000 | (base + 120), 0x907E0000 | (base + 124))
        check("TXNSetDataFromFile", [document, slot(124), 0x54455854, 4, 0, 0x7FFFFFFF], 0, side)
        check("TXNGetData", [document, 0, 0x7FFFFFFF, ptr(52)], 0, 1 - side)
        p.emit(0x807E0000 | (base + 52), 0x80630000, 0x80630000)
        p.expect(0x48454144, "file insertion reads the requested bytes from the established fork")
        probe.call("DisposeHandle", [slot(52)])
        check("FSClose", [slot(124)], 0)
        check("TXNRevert", [document], 0, 1 - side)
    for side in (0, 1):
        check("TXNSave", [0, 0x54455854, 0x7374796C, 1, 0, -1, -1], -50, side)
        check("TXNSave", [document, 0x54455854, 0x7374796C, 1, probe.ptr(0x1F40), 0x1234FFFF, -1], -50, side)
        p.emit(0x807E0000 | (base + 4), 0x806300AA)
        p.expect(1, "save records the requested data format before validating the fork")
        p.emit(0x807E0000 | (base + 4), 0x806300AE)
        p.expect(0x7374796C, "save preserves the requested styl resource format")
        p.emit(0x807E0000 | (base + 4), 0x80630056, 0x809E1F40)
        p.equal("save copies the destination file specification before reporting the fork error")
    box_bounds = probe.far_raw(bytes(8))
    unicode_data = probe.far_raw(b"\x00Q")
    for side in (0, 1):
        check("TXNDrawUnicodeTextBox", [unicode_data, 1, box_bounds, 0, 0], -50, side)
        check("TXNDrawCFStringTextBox", [0, box_bounds, 0, 0], -50, side)
    for length in (0, 1, 257):
        probe.call("CFStringCreateWithCString", [0, probe.far_raw(b"Q" * length + b"\0"), 0x600])
        p.nonzero("create a real Core Foundation string for the Textension box")
        p.emit(0x907E0000 | (base + 124))
        # The saved long-string branch dereferences CFAllocatorAllocate's raw pointer.
        for side in (0, 1) if length <= 256 else (0,):
            probe.call("TXNDrawCFStringTextBox", [slot(124), box_bounds, 0, 0], side)
            if length:
                p.expect(-50, "zero-area boxes reject text after reading the CF string")
            else:
                p.emit(0x809E0000 | (base + 124))
                p.equal("the original empty-string path returns the input string")
        probe.call("CFRelease", [slot(124)])
    probe.call("TXNDeleteObject", [document], 1)
    probe.call("DisposeWindow", [window])
    probe.call("TXNTerminateTextension")


CLASSIC_STATE_APIS = (
    "AEInitializeDesc",
    "GetGlobalMouse",
    "TEGetDoTextHook",
    "TESetDoTextHook",
    "TEGetFindWordHook",
    "TESetFindWordHook",
    "TEGetRecalcHook",
    "TESetRecalcHook",
    "TEGetScrapHandle",
    "TESetScrapHandle",
    "TEScrapHandle",
    "TEGetScrapLength",
    "TESetScrapLength",
    "PtrToHand",
    "HandToHand",
    "PurgeSpace",
    "Microseconds",
    "UpTime",
    "AbsoluteToNanoseconds",
    "NanosecondsToAbsolute",
    "AbsoluteToDuration",
    "NanosecondsToDuration",
    "DurationToAbsolute",
    "DurationToNanoseconds",
    "AddAbsoluteToAbsolute",
    "SubAbsoluteFromAbsolute",
    "AbsoluteDeltaToDuration",
    "AbsoluteDeltaToNanoseconds",
    "AddDurationToAbsolute",
    "SubDurationFromAbsolute",
    "AddNanosecondsToAbsolute",
    "SubNanosecondsFromAbsolute",
    "DateToSeconds",
    "SecondsToDate",
    "GetTime",
)


def sound_input_operations(probe):
    p = probe.p
    _, base = probe.raw(bytes(128))
    callback = probe.raw(bytes(8))
    p.routines.append((callback[1], [0x90620000 | (base + 116), 0x4E800020]))
    port = probe.slot(base + 32)
    value = probe.ptr(base + 36)
    block = probe.ptr(base + 44)
    samples = probe.ptr(base + 84)

    def call(name, args, status=0):
        probe.call(name, args)
        p.expect(status, name + " status")

    def expect(offset, value):
        p.emit(0x807E0000 | (base + offset))
        p.expect(value, "sound input result at field " + str(offset))

    call("SPBGetIndexedDevice", [2, probe.ptr(base), 0], -221)
    call("SPBGetIndexedDevice", [1, probe.ptr(base), probe.ptr(base + 40)])
    expect(40, 0)
    call("SPBOpenDevice", [probe.ptr(base), 1, probe.ptr(base + 32)])
    call("SPBOpenDevice", [probe.ptr(base), 1, probe.ptr(base + 40)], -227)
    call("SPBGetDeviceInfo", [port, int.from_bytes(b"ssiz", "big"), value])
    p.emit(0xA07E0000 | (base + 36))
    p.expect(16, "capture reports its PCM sample width")
    p.emit(0x3C601F40, 0x907E0000 | (base + 36))
    call("SPBSetDeviceInfo", [port, int.from_bytes(b"srat", "big"), value])
    call("SPBGetDeviceInfo", [port, int.from_bytes(b"srat", "big"), value])
    expect(36, 8000 << 16)
    call("SPBGetDeviceInfo", [port, int.from_bytes(b"qual", "big"), value])
    expect(36, int.from_bytes(b"good", "big"))
    p.emit(0x38600001, 0x907E0000 | (base + 36))
    call("SPBMillisecondsToBytes", [port, value])
    expect(36, 16)
    call("SPBBytesToMilliseconds", [port, value])
    expect(36, 1)

    struct.pack_into(">3I", p.data, base + 48, 8, 0, 16)
    struct.pack_into(">I", p.data, base + 92, 0xA55AA55A)
    p.emit(0x807E0000 | (base + 32), 0x907E0000 | (base + 44))
    probe.arguments([samples, callback], 0)
    p.emit(0x907E0000 | (base + 60), 0x909E0000 | (base + 64))
    call("SPBRecord", [block, 0])
    expect(48, 8)
    expect(84, 0x34123412)
    expect(88, 0x34123412)
    expect(92, 0xA55AA55A)
    expect(116, 0)
    call("SPBGetRecordingStatus", [port, 0, 0, probe.ptr(base + 100), probe.ptr(base + 104), 0, 0])
    expect(100, 4)
    expect(104, 4)

    call("SPBRecord", [block, 1])
    call("SPBPauseRecording", [port])
    call("SPBGetRecordingStatus", [port, probe.ptr(base + 108), 0, 0, 0, 0, 0])
    p.emit(0xA07E0000 | (base + 108))
    p.expect(2, "the device reports a paused recording")
    call("SPBResumeRecording", [port])
    call("SPBStopRecording", [port])
    p.emit(0x807E0000 | (base + 116), 0x389E0000 | (base + 44))
    p.equal("asynchronous completion receives the parameter block")
    p.emit(0xA87E0000 | (base + 76))
    p.expect(-27, "stopping a capture publishes abortErr before completion")
    call("SPBCloseDevice", [port])
    call("SPBStopRecording", [port], -229)


def sound_manager_operations(probe):
    p = probe.p
    _, base = probe.far_raw(bytes([0xA5]) * 76)

    def read(offset, halfword=False):
        probe.arguments([probe.ptr(base + offset)], 0)
        p.emit(0xA0630000 if halfword else 0x80630000)

    for index, (getter, setter) in enumerate(
        (("GetSysBeepVolume", "SetSysBeepVolume"), ("GetDefaultOutputVolume", "SetDefaultOutputVolume"))
    ):
        saved = base + 4 * index
        probe.call(getter, [probe.ptr(saved)])
        p.expect(0, "read the original sound volume")
        for volume in (0, 0x004000C0):
            probe.call(setter, [volume])
            p.expect(0, "set the device's packed sound gain")
            probe.call(getter, [probe.ptr(base + 8)])
            p.expect(0, "read the live sound gain")
            read(8)
            p.expect(volume, "sound gain preserves both 8.8 channels in guest order")
        probe.call(setter, [probe.slot(saved)])
        p.expect(0, "restore the original sound volume")

    handle = probe.slot(base + 12)
    probe.call("NewHandle", [96])
    p.nonzero("allocate caller-owned sound resource storage")
    slot = base + 12
    p.emit(0x3C9E0000 | ((slot + 0x8000) >> 16), 0x90640000 | (slot & 0xFFFF))
    rate = 44100 << 16
    for channels, bits, codec, data_bytes, header_size, encoding in (
        (1, 8, b"NONE", 12, 42, 0x003C),
        (2, 16, b"NONE", 12, 84, 0xFF3C),
        (1, 8, b"MAC3", 12, 84, 0xFE3C),
    ):
        probe.call(
            "SetupSndHeader",
            [handle, channels, rate, bits, int.from_bytes(codec, "big"), 60, data_bytes, probe.ptr(base + 16)],
        )
        p.expect(0, "construct a playable sampled-sound resource header")
        read(16, halfword=True)
        p.expect(header_size, "the header length locates the caller's sample data")
        probe.arguments([handle], 0)
        p.emit(0x80630000, 0xA0630028)
        p.expect(encoding, "the sampled sound header sets the correct encoding and base note")
        probe.arguments([handle], 0)
        p.emit(0x80630000, 0x8063001C)
        p.expect(rate, "unsigned 16.16 sample rates above 32 kHz survive")
        probe.call("GetHandleSize", [handle])
        p.expect(96, "header construction leaves the caller's handle size intact")
        probe.call("ParseSndHeader", [handle, probe.ptr(base + 40), probe.ptr(base + 68), probe.ptr(base + 72)])
        p.expect(0, "parse the caller's sampled sound resource")
        code = b"MAC3" if codec == b"MAC3" else b"raw " if bits == 8 else b"twos"
        frames = data_bytes // (channels * (2 if codec == b"MAC3" else bits // 8))
        for field, value in (
            (44, int.from_bytes(code, "big")),
            (48, (channels << 16) | bits),
            (52, rate),
            (68, frames),
            (72, header_size),
        ):
            read(field)
            p.expect(value, "the resource description reports the encoding, stored frame count and data offset")
    probe.call("SetHandleSize", [handle, 24])
    probe.call("ParseSndHeader", [handle, probe.ptr(base + 40), probe.ptr(base + 68), probe.ptr(base + 72)])
    p.expect(-206, "reject a sampled sound header truncated inside the handle")
    read(72)
    p.expect(84, "a rejected sound resource leaves the previous data offset intact")
    probe.call("DisposeHandle", [handle])
    struct.pack_into(">I", p.data, base + 20, 20)
    probe.call("GetCompressionInfo", [3, 0, 2, 8, probe.ptr(base + 20)])
    p.expect(0, "query actual MACE packet geometry")
    for offset, value in ((20, 20), (24, 0x4D414333), (28, 0x00030006), (32, 0x00020004), (36, 0x00010000)):
        read(offset)
        p.expect(value, "compression info records real encoded and decoded packet geometry")
    probe.call("SpeechManagerVersion", [])
    p.expect(0x01508000, "preserve the complete four-byte speech manager version")
    sound_output_operations(probe)
    sound_preference_operations(probe)


def sound_output_operations(probe):
    p = probe.p
    _, base = probe.far_raw(bytes(12) + bytes([0xA5]) * 8 + struct.pack(">IHH", 0x01200040, 1, 0))
    hardware_volume = int.from_bytes(b"hvol", "big")
    hardware_mute = int.from_bytes(b"hmut", "big")
    channel_volume = int.from_bytes(b"volu", "big")

    def checked(name, arguments, message, result=0):
        probe.call(name, arguments)
        p.expect(result, message)

    def verify(selector, value):
        checked("GetSoundOutputInfo", [0, selector, probe.ptr(base + 12)], "query the default sound output")
        probe.arguments([probe.slot(base + 12)], 0)
        p.expect(value, "preserve the field width and guest byte order")

    checked("GetSoundOutputInfo", [0, hardware_volume, probe.ptr(base + 4)], "save the default device gain")
    checked("GetSoundOutputInfo", [0, hardware_mute, probe.ptr(base + 8)], "save the default device mute state")
    checked("SetSoundOutputInfo", [0, hardware_volume, probe.ptr(base + 20)], "apply over-unity hardware gain")
    verify(int.from_bytes(b"ssiz", "big"), 0x0008A5A5)
    verify(int.from_bytes(b"chan", "big"), 0x0001A5A5)
    checked("SetSoundOutputInfo", [0, hardware_mute, probe.ptr(base + 24)], "mute the default output device")
    verify(hardware_mute, 0x0001A5A5)
    verify(hardware_volume, 0x01200040)
    checked("SetSoundOutputInfo", [0, hardware_mute, probe.ptr(base + 8)], "restore the default device mute state")
    checked("SetSoundOutputInfo", [0, hardware_volume, probe.ptr(base + 4)], "restore the default device gain")

    checked("SndNewChannel", [probe.ptr(base), 5, 0, 0], "create a sampled sound channel")
    channel = probe.slot(base)
    checked("SndGetInfo", [channel, channel_volume, probe.ptr(base + 16)], "read a new channel's gain")
    probe.arguments([probe.slot(base + 16)], 0)
    p.expect(0x01000100, "new sound channels begin at unity gain")
    checked("SndSetInfo", [channel, channel_volume, probe.ptr(base + 20)], "apply independent channel gain")
    checked("SndGetInfo", [channel, channel_volume, probe.ptr(base + 16)], "read back live channel gain")
    probe.arguments([probe.slot(base + 16)], 0)
    p.expect(0x01200040, "a channel retains both gains")
    checked("SndGetInfo", [channel, 0, probe.ptr(base + 16)], "reject an unknown native channel selector", -231)
    checked("SndDisposeChannel", [channel, 1], "dispose the test sound channel")


def sound_preference_operations(probe):
    p = probe.p
    _, base = probe.far_raw(bytes(24))
    pref_type = int.from_bytes(b"tSPr", "big")
    name = probe.far_raw(b"\x18ROMAW sound fixture data")
    file_name = probe.far_raw(b"\x11Sound Preferences")
    source, destination = probe.slot(base), probe.slot(base + 4)

    def save(offset):
        address = base + offset
        p.emit(0x3C9E0000 | ((address + 0x8000) >> 16), 0x90640000 | (address & 0xFFFF))

    def check(name, arguments, expected=0):
        probe.call(name, arguments)
        p.expect(expected, name + " returns the expected result")

    for offset, size in ((0, 8), (4, 0)):
        probe.call("NewHandle", [size])
        p.nonzero("allocate a caller-owned sound preference handle")
        save(offset)

    probe.call("CurResFile")
    save(8)
    for size, value in ((4, 0x12345678), (8, 0xDFEAFE77)):
        probe.call("SetHandleSize", [source, size])
        probe.arguments([source, value], 0)
        p.emit(0x80630000, 0x90830000)
        if size == 8:
            p.emit(0x90830004)
        check("SetSoundPreference", [pref_type, name, source])
        probe.arguments([source], 0)
        p.emit(0x80630000, 0x38800000, 0x90830000)
        check("GetSoundPreference", [pref_type, name, destination])
        check("GetHandleSize", [destination], size)
        probe.arguments([destination], 0)
        p.emit(0x80630000, 0x80630000)
        p.expect(value, "the saved preference owns a copy of the original caller data")
    probe.call("CurResFile")
    p.emit(0x7C651B78)
    probe.arguments([probe.slot(base + 8)], 0)
    p.emit(0x38850000)
    p.equal("sound preferences restore the caller's current resource file")

    check("FindFolder", [0x8000, int.from_bytes(b"pref", "big"), 0, probe.ptr(base + 12), probe.ptr(base + 16)])
    probe.arguments([probe.ptr(base + 12)], 0)
    p.emit(0xA8630000)
    save(20)
    probe.call("HOpenResFile", [probe.slot(base + 20), probe.slot(base + 16), file_name, 3])
    p.nonzero("reopen the classic Sound Preferences resource file")
    save(20)
    probe.call("Get1NamedResource", [pref_type, name])
    p.nonzero("the preferences are stored as a classic named resource")
    save(16)
    probe.call("RemoveResource", [probe.slot(base + 16)])
    check("ResError", [])
    probe.call("DisposeHandle", [probe.slot(base + 16)])
    probe.call("CloseResFile", [probe.slot(base + 20)])
    check("ResError", [])
    probe.call("UseResFile", [probe.slot(base + 8)])
    probe.call("DisposeHandle", [source])
    probe.call("DisposeHandle", [destination])


def sound_converter_operations(probe):
    p = probe.p
    _, base = probe.raw(bytes(96))
    converter = probe.slot(base)
    result_frames, result_bytes = probe.ptr(base + 8), probe.ptr(base + 12)

    def format_record(code, rate, channels, bits):
        return probe.raw(struct.pack(">I4sHH4I", 0, code, channels, bits, rate << 16, 0, 0, 0))

    def call(name, arguments, status=0):
        probe.call("SoundConverter" + name, arguments)
        p.expect(status, "SoundConverter" + name + " status")

    def word(offset, expected, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(expected, label)

    raw = format_record(b"raw ", 8000, 1, 8)
    stereo = format_record(b"twos", 16000, 2, 16)
    call("Open", [0, stereo, probe.ptr(base)], -50)
    call("Open", [raw, stereo, probe.ptr(base)])
    p.emit(0x807E0000 | base)
    p.nonzero("open publishes a converter")
    call("GetBufferSizes", [converter, 3, result_frames, probe.ptr(base + 16), result_bytes])
    word(8, 3, "input buffers contain three PCM frames")
    word(16, 3, "input size accounts for the mono 8-bit stride")
    word(12, 24, "output size accounts for rate, sample width and both channels")
    call("ConvertBuffer", [converter, probe.ptr(base + 24), 3, probe.ptr(base + 24), result_frames, result_bytes], -50)
    call("BeginConversion", [converter])
    struct.pack_into(">I", p.data, base + 24, 0x0080FFA5)
    struct.pack_into(">I", p.data, base + 48, 0x5AA55AA5)
    call("ConvertBuffer", [converter, probe.ptr(base + 24), 3, probe.ptr(base + 24), result_frames, result_bytes])
    word(8, 6, "resampling reports the expanded PCM frame count")
    word(12, 24, "resampling reports the exact output extent")
    for index, sample in enumerate((0x8000, 0x8000, 0xC000, 0, 0x3F80, 0x7F00)):
        word(24 + 4 * index, sample << 16 | sample, "in-place expansion preserves the interpolated stereo samples")
    word(48, 0x5AA55AA5, "conversion leaves the following bytes untouched")
    call("EndConversion", [converter, 0, result_frames, result_bytes])
    word(8, 0, "end publishes no buffered frames")
    word(12, 0, "end publishes no buffered bytes")
    call("Close", [converter])

    source = probe.raw(b"\xA5\x5A\x11\xE3")
    signed = probe.far_raw(struct.pack(">I4sHH4I", 0, b"twos", 1, 8, 8000 << 16, 0, 0, 0))
    for input_format, output_format in ((signed, raw), (raw, signed)):
        call("Open", [input_format, output_format, probe.ptr(base)])
        call("BeginConversion", [converter])
        call("ConvertBuffer", [converter, source, 4, probe.ptr(base + 24), result_frames, result_bytes])
        word(8, 4, "signed and unsigned byte encodings have the same frame geometry")
        word(12, 4, "signed and unsigned byte conversions preserve the byte count")
        word(24, 0x25DA9163, "signed AIFF bytes and offset-binary sound-resource bytes differ in their top bit")
        call("EndConversion", [converter, 0, result_frames, result_bytes])
        call("Close", [converter])

    for code, packet_bytes in ((b"MAC3", 2), (b"MAC6", 1)):
        encoded = format_record(code, 8000, 1, 8)
        call("Open", [encoded, raw, probe.ptr(base)])
        call("BeginConversion", [converter])
        call("ConvertBuffer", [converter, source, 2, probe.ptr(base + 24), result_frames, result_bytes])
        word(8, 12, "two MACE packets decode to twelve PCM frames")
        word(12, 12, "MACE decoding reports the exact PCM extent")
        call("EndConversion", [converter, 0, result_frames, result_bytes])
        call("BeginConversion", [converter])
        for index in range(2):
            call(
                "ConvertBuffer",
                [
                    converter,
                    probe.ptr(source[1] + index * packet_bytes),
                    1,
                    probe.ptr(base + 48 + 6 * index),
                    result_frames,
                    result_bytes,
                ],
            )
            word(12, 6, "each MACE packet decodes to six bytes")
        for offset in range(0, 12, 4):
            p.emit(0x807E0000 | base + 24 + offset, 0x809E0000 | base + 48 + offset)
            p.equal("MACE predictor state survives split buffers and resets at begin")
        call("EndConversion", [converter, 0, result_frames, result_bytes])
        call("Close", [converter])

    data_proc = probe.ptr(base + 76)
    p.routines.append(
        (
            data_proc[1],
            [
                0x80A40008,  # Count every callback, including end-of-input.
                0x38A50001,
                0x90A40008,
                0x80A40004,
                0x2C050000,
                0x4182001C,
                0x80A40000,
                0x90A30000,  # Publish the SoundComponentData pointer.
                0x38000000,
                0x90040004,
                0x38600001,
                0x4E800020,
                0x38600000,
                0x4E800020,
            ],
        )
    )
    upp, flags = probe.slot(base + 84), probe.ptr(base + 88)
    probe.call("NewSoundConverterFillBufferDataUPP", [data_proc])
    p.nonzero("allocate a sound fill callback")
    p.emit(0x907E0000 | (base + 84))
    packet_data = probe.raw(b"\x12\x34\x56")
    packet = format_record(b"raw ", 8000, 1, 8)
    struct.pack_into(">I", p.data, packet[1] + 16, 3)
    probe.arguments([packet_data], 0)
    p.emit(0x907E0000 | (packet[1] + 20))
    probe.arguments([packet, 1], 0)
    p.emit(0x907E0000 | (base + 64), 0x909E0000 | (base + 68))
    probe.arguments([0xA5A5A5A5], 0)
    p.emit(0x907E0000 | (base + 24), 0x907E0000 | (base + 28))
    call("Open", [raw, raw, probe.ptr(base)])
    call("BeginConversion", [converter])
    fill = [converter, upp, probe.ptr(base + 64)]
    counters = [result_bytes, result_frames, flags]
    call("FillBuffer", [*fill, probe.ptr(base + 24), 2, *counters])
    word(8, 2, "filled two whole PCM frames")
    word(12, 2, "filled the requested two bytes")
    word(24, 0x1234A5A5, "the callback does not overwrite the output guard")
    word(88, 2, "the converter reports leftover audio")
    call("FillBuffer", [*fill, probe.ptr(base + 28), 1, *counters])
    word(28, 0x56A5A5A5, "the next fill returns the buffered third sample")
    word(72, 1, "a fill satisfied from buffered audio skips the callback")
    word(88, 0, "a complete fill with no buffered audio clears the flags")
    call("FillBuffer", [*fill, probe.ptr(base + 28), 2, *counters])
    word(8, 0, "the callback signaled end of input")
    word(12, 0, "end of input leaves the output untouched")
    word(72, 2, "fill requests new packets after draining")
    word(88, 1, "end of input reports a short fill")

    p.emit(0x38000001, 0x901E0000 | (base + 68))
    call("FillBuffer", [*fill, probe.ptr(base + 24), 1, *counters])
    call("EndConversion", [converter, probe.ptr(base + 28), result_frames, result_bytes])
    word(8, 2, "end drains two leftover PCM frames")
    word(12, 2, "end reports the drained byte count")
    word(28, 0x3456A5A5, "end preserves the order and extent of the leftover data")
    call("Close", [converter])
    probe.call("DisposeSoundConverterFillBufferDataUPP", [upp])


def power_activity_operations(probe):
    p = probe.p
    overall = probe.raw(struct.pack(">HIH", 0, 0xA5A5A5A5, 0xBEEF))
    disk = probe.raw(struct.pack(">HIH", 3, 0xA5A5A5A5, 0xBEEF))
    invalid = probe.raw(struct.pack(">HIH", 0xFFFF, 0x12345678, 0xBEEF))
    _, saved = probe.raw(bytes(8))

    def check_status(name, args, status=0):
        probe.call(name, args)
        p.expect(status, name + " returns the expected power activity result")

    probe.call("IdleUpdate")
    p.emit(0x907E0000 | saved)
    check_status("GetLastActivity", [overall])
    p.emit(0x807E0000 | (overall[1] + 2), 0x809E0000 | saved)
    p.equal("IdleUpdate returns the tick installed in the activity timer")
    p.emit(0xA07E0000 | (overall[1] + 6))
    p.expect(0xBEEF, "GetLastActivity respects the packed six-byte record extent")

    check_status("UpdateSystemActivity", [0xDEAD0003])
    check_status("GetLastActivity", [disk])
    check_status("GetLastActivity", [overall])
    p.emit(0x807E0000 | (disk[1] + 2), 0x809E0000 | (overall[1] + 2))
    p.equal("an explicit disk activity also resets the overall activity timer")
    p.emit(0x907E0000 | (saved + 4))
    poll = len(p.code)
    probe.call("TickCount")
    p.emit(0x809E0000 | (saved + 4), 0x7C032000)
    p.emit(0x41820000 | ((poll - len(p.code)) * 4 & 0xFFFF))
    probe.call("IdleUpdate")
    p.emit(0x809E0000 | (saved + 4), 0x7C632050)
    p.nonzero("IdleUpdate advances the overall timer")
    check_status("GetLastActivity", [disk])
    p.emit(0x807E0000 | (disk[1] + 2), 0x809E0000 | (saved + 4))
    p.equal("overall activity preserves the last disk timestamp")
    check_status("UpdateSystemActivity", [255], -50)
    check_status("GetLastActivity", [invalid], -50)
    check_status("GetLastActivity", [0], -50)
    p.emit(0x807E0000 | (invalid[1] + 2))
    p.expect(0x12345678, "an unknown activity leaves the caller's record intact")

    for name, arguments, expected in (
        ("PMFeatures", [], 0),
        ("MaximumProcessorSpeed", [], 250),
        ("CurrentProcessorSpeed", [], 250),
        ("FullProcessorSpeed", [], 1),
        ("SetProcessorSpeed", [0], 0),
        ("CurrentProcessorSpeed", [], 250),
        ("IsProcessorCyclingEnabled", [], 0),
        ("GetIntModemInfo", [], 0),
        ("BatteryCount", [], 0),
        ("GetBatteryVoltage", [0xFFFF], 0),
    ):
        probe.call(name, arguments)
        p.expect(expected, name + " reports the fixed-clock desktop hardware")

    timeouts = (
        ("SetSleepTimeout", "GetSleepTimeout", 0xC1),
        ("SetHardDiskTimeout", "GetHardDiskTimeout", 0x7F),
        ("SetDimmingTimeout", "GetDimmingTimeout", 0xA6),
    )
    for setter, _, value in timeouts:
        probe.call(setter, [0xBEEF0000 | value])
    for _, getter, value in timeouts:
        probe.call(getter)
        p.expect(value, getter + " returns its own stored unsigned byte")

    for setter, getter, inverted in (
        ("AutoSleepControl", "IsAutoSlpControlDisabled", True),
        ("DimmingControl", "IsDimmingControlDisabled", True),
        ("SetSpindownDisable", "IsSpindownDisabled", False),
        ("SetDimSuspendState", "GetDimSuspendState", False),
    ):
        for enabled in (0, 1):
            probe.call(setter, [0xA5A50000 | enabled])
            probe.call(getter)
            p.expect(enabled ^ inverted, getter + " reads the live policy flag")

    _, buffer = probe.far_raw(bytes([0xA5]) * 24)
    probe.call("GetBatteryTimes", [1, probe.ptr(buffer + 4)])
    for offset, expected in ((0, 0xA5A5A5A5), (4, 0), (8, 0), (12, 0), (16, 0), (20, 0xA5A5A5A5)):
        probe.arguments([probe.ptr(buffer + offset)], 0)
        p.emit(0x80630000)
        p.expect(expected, "battery time clears exactly four guest longwords for an absent battery")
    probe.call("GetScaledBatteryInfo", [0, probe.ptr(buffer)])
    probe.arguments([probe.ptr(buffer)], 0)
    p.emit(0x80630000)
    p.expect(0, "scaled battery information clears all four bytes on the desktop board")


def code_fragment_operations(probe, destination):
    p = probe.p
    symbol = "FixtureWord"
    marker = 0x0C0FFEE7
    image = pef([0x4E800020], struct.pack(">I", marker), exports=[(symbol, 1, 0, 1)])
    file_name = b"Carbon CFM Fixture"
    write_fragment(destination, file_name.decode(), image, 0)
    memory = probe.raw(image)
    _, base = probe.raw(bytes(0x270))
    connection, borrowed, address, count = (probe.ptr(base + i) for i in (0, 4, 8, 12))
    kind, spec, message, title = (probe.ptr(base + i) for i in (16, 20, 0x68, 0x168))
    connected = probe.slot(base)

    def call(name, args, status=0):
        probe.call(name, args)
        p.expect(status, name + " returns the expected CFM result")

    missing = probe.raw(b"\x07NoCfm01")
    call("GetSharedLibrary", [missing, 0x70777063, 2, connection, 0, message], -2804)
    p.emit(0x807E0000 | message[1], 0x809E0000 | missing[1])
    p.equal("GetSharedLibrary identifies the missing fragment")
    call("GetSharedLibrary", [0, 0x70777063, 1, connection, 0, 0], -50)
    call("GetMemFragment", [memory, 2, 0, 1, connection, 0, 0], -2806)
    call("GetMemFragment", [memory, len(image), 0, 1, connection, 0, 0])
    call("GetMemFragment", [memory, len(image), 0, 2, borrowed, 0, 0])
    call("CountSymbols", [connected, count])
    p.emit(0x807E0000 | count[1])
    p.expect(1, "CountSymbols returns the memory fragment's export count")
    call("GetIndSymbol", [connected, 0, title, address, kind])
    p.emit(0x887E0000 | kind[1])
    p.expect(1, "GetIndSymbol returns the data class")
    p.emit(0x807E0000 | address[1], 0x80630000)
    p.expect(marker, "GetIndSymbol resolves the data export")
    call("GetIndSymbol", [connected, 1, title, address, kind], -2802)
    call("FindSymbol", [probe.slot(base + 4), probe.raw(b"\x0bFixtureWord"), address, 0])
    p.emit(0x807E0000 | address[1], 0x80630000)
    p.expect(marker, "an unnamed memory fragment can be found without a new reference")
    call("FindSymbol", [connected, 0, address, 0], -50)
    call("CloseConnection", [connection])
    call("CloseConnection", [borrowed], -2801)
    call("CloseConnection", [0], -50)

    path = probe.raw(bytes([len(file_name)]) + file_name)
    call("FSMakeFSSpec", [probe.slot(0x1FC0), probe.slot(0x1F42), path, spec])
    call("GetDiskFragment", [spec, 0, len(image), 0, 1, connection, 0, message])
    call("FindSymbol", [connected, title, address, kind])
    p.emit(0x807E0000 | address[1], 0x80630000)
    p.expect(marker, "an unnamed disk fragment resolves the same data export")
    call("CloseConnection", [connection])
    call("GetDiskFragment", [0, 0, 0, 0, 1, connection, 0, message], -50)


def navigation_operations(probe):
    p = probe.p
    _, options = probe.raw(bytes([0xA5]) * 0x616)
    _, creation = probe.raw(bytes([0xA5]) * 0x36)
    _, legacy = probe.raw(bytes(32))
    _, carbon = probe.raw(bytes(32))
    p.data[carbon + 1] = 2

    def call(name, args, status=0):
        probe.call(name, args)
        p.expect(status, name + " returns the expected Navigation Services result")

    def word(offset, expected, label):
        p.emit(0x807E0000 | offset)
        p.expect(expected, label)

    _, load_status = probe.raw(bytes(4))
    for side in (1, 0):
        call("NavUnload", [])
        probe.call("NavLoad", side=side)
        p.emit(0x907E0000 | load_status)
        probe.call("NavLoad", side=1 - side)
        p.emit(0x809E0000 | load_status)
        p.equal("NavLoad agrees with the saved loader on repeated calls")
        probe.call("NavUnload", side=side)
        p.expect(0, "NavUnload releases one load")
        probe.call("NavUnload", side=1 - side)
        p.expect(0, "NavUnload releases the remaining load")

    # Supply a minimal backend in memory to exercise successful loads in the
    # same app even when the profile has no Navigation Real fragment on disk.
    backend = pef([0x4E800020], bytes(4))
    title = probe.raw(b"\x0fNavigation Real")
    connection = probe.raw(bytes(4))
    found = probe.raw(bytes(4))
    call("GetMemFragment", [probe.far_raw(backend), len(backend), title, 1, connection, 0, 0])
    call("NavLoad", [])
    probe.call("NavLoad", side=1)
    p.expect(0, "saved NavLoad shares the native successful load")
    call("CloseConnection", [connection])
    call("GetSharedLibrary", [title, 0x70777063, 2, found, 0, 0])
    probe.call("NavUnload", side=1)
    call("GetSharedLibrary", [title, 0x70777063, 2, found, 0, 0])
    call("NavUnload", [])
    call("GetSharedLibrary", [title, 0x70777063, 2, found, 0, 0], -2804)

    call("NavServicesCanRun", [], 0)  # The staged Appearance Manager reports version 1.0.
    probe.call("LMGetSysMap")
    p.emit(0x907E0000 | (carbon + 28))
    probe.call("CurResFile")
    p.emit(0x907E0000 | (legacy + 28))
    # Temporarily designate the fixture's resource map as System for the version lookup.
    probe.call("LMSetSysMap", [probe.slot(legacy + 28)])
    probe.call("UseResFile", [probe.slot(carbon + 28)])
    call("NavLibraryVersion", [], 0x02008000)
    probe.call("LMSetSysMap", [probe.slot(carbon + 28)])
    probe.call("CurResFile")
    p.emit(0x809E0000 | (carbon + 28))
    p.equal("NavLibraryVersion restores the current resource map")
    probe.call("UseResFile", [probe.slot(legacy + 28)])
    call("NavGetDefaultDialogOptions", [0], -50)
    call("NavGetDefaultDialogOptions", [probe.ptr(options)])
    word(options + 2, 0xE4, "legacy options select classic dialog defaults")
    word(options + 6, 0xFFFFFFFF, "legacy options request persistent dialog position")
    for offset in range(0x0A, 0x60A, 256):
        word(options + offset, 0x00A5A5A5, "default Pascal strings clear only their length byte")
    word(options + 0x60A, 0, "legacy options clear the preference key")
    word(options + 0x60E, 0, "legacy options clear the popup extension")
    word(options + 0x612, 0xA5A5A5A5, "legacy options leave reserved storage unchanged")
    call("NavGetDefaultDialogCreationOptions", [0], -50)
    call("NavGetDefaultDialogCreationOptions", [probe.ptr(creation)])
    word(creation + 2, 0xE4, "creation options select Carbon dialog defaults")
    word(creation + 6, 0xFFFFFFFF, "creation options request persistent dialog position")
    word(creation + 0x2A, 2, "creation options default to application modality")
    word(creation + 0x32, 0xA5A5A5A5, "creation options leave reserved storage unchanged")

    call("AECreateList", [0, 0, 0, probe.ptr(legacy + 6)])
    call("AECreateDesc", [0x54455854, probe.raw(b"data"), 4, probe.ptr(legacy + 20)])
    probe.call("NewHandle", [20])
    p.nonzero("allocate the legacy file translation handle")
    p.emit(0x907E0000 | (legacy + 16))
    probe.call("HLock", [probe.slot(legacy + 16)])
    call("NavDisposeReply", [probe.ptr(legacy)])
    word(legacy + 6, 0x6E756C6C, "dispose clears the legacy selection descriptor")
    word(legacy + 20, 0x6E756C6C, "dispose clears the legacy private descriptor")
    word(legacy + 16, 0, "dispose clears the file translation handle")
    call("NavDisposeReply", [probe.ptr(legacy)])

    probe.call("CFStringCreateWithPascalString", [0, probe.raw(b"\x04save"), 0])
    p.nonzero("allocate a Carbon reply's save filename")
    p.emit(0x907E0000 | (carbon + 24), 0x907E0000 | (carbon + 28))
    probe.call("CFRetain", [probe.slot(carbon + 28)])
    call("NavDisposeReply", [probe.ptr(carbon)])
    word(carbon + 24, 0, "dispose clears the saved CFString")
    probe.call("CFGetRetainCount", [probe.slot(carbon + 28)])
    p.expect(1, "disposing the reply releases its saved filename exactly once")
    probe.call("CFRelease", [probe.slot(carbon + 28)])
    call("NavDisposeReply", [0], -50)

    confirmation = probe.ptr(legacy + 28)
    output = probe.ptr(carbon + 28)
    options_address = probe.ptr(options)
    creation_address = probe.ptr(creation)
    dialog = probe.slot(carbon + 28)
    call("NavAskSaveChanges", [options_address, 3, confirmation, 0, 0], -50)
    call("NavAskDiscardChanges", [options_address, 0, 0, 0], -50)
    call("NavCustomAskSaveChanges", [options_address, confirmation, 0, 0], -50)
    p.emit(0x3C00034E, 0x60006176, 0x901E0000 | (options + 0x50A))
    for name, arguments, key, result in (
        ("NavAskSaveChanges", [options_address, 1, confirmation, 0, 0], 13, 1),
        ("NavCustomAskSaveChanges", [options_address, confirmation, 0, 0], 27, 2),
        ("NavAskDiscardChanges", [options_address, confirmation, 0, 0], 13, 1),
    ):
        call("PostEvent", [3, key])
        call(name, arguments)
        word(legacy + 28, result, "the legacy alert writes the actual selected button")
    p.emit(0x38000000, 0x981E0000 | (options + 0x50A))

    for name, arguments, key, action in (
        ("NavCreateAskSaveChangesDialog", [creation_address, 2, 0, 0, output], 13, 6),
        ("NavCreateAskDiscardChangesDialog", [creation_address, 0, 0, output], 27, 1),
        ("NavCreateAskDiscardChangesDialog", [creation_address, 0, 0, output], 13, 8),
    ):
        call(name, arguments)
        p.emit(0x807E0000 | (carbon + 28))
        p.nonzero("create an unshown Navigation confirmation dialog")
        call("NavDialogGetUserAction", [dialog], 0)
        call("PostEvent", [3, key])
        call("NavDialogRun", [dialog])
        call("NavDialogGetUserAction", [dialog], action)
        call("NavDialogGetReply", [dialog, probe.ptr(carbon)], -5695)
        call("NavDialogRun", [dialog], -5694)
        probe.call("NavDialogDispose", [dialog])

    p.emit(0x38000007, 0x901E0000 | (options + 2), 0x901E0000 | (creation + 2))
    reply = probe.ptr(legacy)
    for chooser in ("NavChooseFolder", "NavChooseObject", "NavChooseVolume", "NavNewFolder"):
        call(chooser, [0, 0, options_address], -50)
        call("PostEvent", [3, 27])
        call(chooser, [0, reply, options_address, 0, 0, 0], -128)
        word(legacy, 0, "cancelling a legacy chooser leaves its reply invalid")
        call("NavDisposeReply", [reply])

    # Use space in the unused file-name field before installing the Pack3 fixture.
    volume_spec, volume_size, volume_count = options + 0x440, options + 0x48C, options + 0x490
    selection = probe.ptr(legacy + 6)
    for chooser, arguments in (
        ("NavCreateChooseFolderDialog", [0, 0, 0, 0, output]),
        ("NavCreateChooseObjectDialog", [creation_address, 0, 0, 0, 0, output]),
        ("NavCreateChooseVolumeDialog", [0, 0, 0, 0, output]),
        ("NavCreateNewFolderDialog", [0, 0, 0, output]),
    ):
        volume_dialog = chooser == "NavCreateChooseVolumeDialog"
        call(chooser, arguments)
        call("NavDialogGetUserAction", [dialog], 0)
        call("NavDialogGetReply", [dialog, reply], -5694)
        call("PostEvent", [3, 13 if volume_dialog else 27])
        call("NavDialogRun", [dialog])
        call("NavDialogGetUserAction", [dialog], 4 if volume_dialog else 1)
        call("NavDialogGetReply", [dialog, reply])
        word(legacy, 0x00020100 if volume_dialog else 0x00020000, "chooser reports reply validity")
        if volume_dialog:
            call("AECountItems", [selection, probe.ptr(volume_count)])
            word(volume_count, 1, "the volume chooser returns a single item")
            call("AEGetNthPtr", [selection, 1, 0x66737320, 0, 0, probe.ptr(volume_spec), 70, probe.ptr(volume_size)])
            word(volume_size, 70, "the chosen volume uses the packed FSSpec layout")
            word(volume_spec + 2, 1, "the chosen volume has the root parent ID")
            p.emit(0x887E0000 | (volume_spec + 6))
            p.nonzero("the chosen volume retains its actual Macintosh name")
        call("NavDisposeReply", [reply])
        probe.call("NavDialogDispose", [dialog])

    # Create the folder beside the fixture in its writable, isolated directory.
    old_disk, old_directory = options + 0x494, options + 0x498
    p.emit(0xA0800214, 0x909E0000 | old_disk, 0x80800398, 0x909E0000 | old_directory)
    p.emit(0x809E1FC0, 0x7C8400D0, 0xB0800214, 0x809E1F42, 0x90800398)
    call("NavCreateNewFolderDialog", [0, 0, 0, output])
    call("PostEvent", [3, 13])
    call("PostEvent", [3, 13])
    call("NavDialogRun", [dialog])
    call("NavDialogGetUserAction", [dialog], 5)
    call("NavDialogGetReply", [dialog, reply])
    call("AEGetNthPtr", [selection, 1, 0x66737320, 0, 0, probe.ptr(volume_spec), 70, probe.ptr(volume_size)])
    word(volume_spec + 6, 0x0F756E74, "the result identifies the newly created folder")
    call("FSpDelete", [probe.ptr(volume_spec)])
    call("NavDisposeReply", [reply])
    probe.call("NavDialogDispose", [dialog])
    p.emit(0x809E0000 | old_disk, 0xB0800214, 0x809E0000 | old_directory, 0x90800398)

    # The fake Pack3 callback returns a guest FSSpec without opening a dialog.
    # Use the unused tails of the fixture's Pascal strings for its descriptor and state.
    state = options + 0x440
    callback = options + 0x80
    p.routines.append(
        (
            callback,
            [
                0x90620000 | (state + 4),  # Save Standard File selector.
                0x90820000 | (state + 8),
                0x90A20000 | (state + 12),
                0x2C030005,
                0x41820010,
                0x80060000,  # First type in the get list.
                0x90020000 | (state + 16),
                0x7CE63B78,  # Get's reply is r7. Put's reply is r6.
                0x80020000 | (state + 20),
                0x98060000,
                0x38000001,
                0x98060001,
                0x3800FFFE,
                0xB0060006,
                0x3C001234,
                0x60005678,
                0x90060008,
                0x3C000474,
                0x60006573,
                0x9006000C,
                0x38000074,
                0x98060010,
                0x38000005,
                0xB006004C,
                0x4E800020,
            ],
        )
    )
    p.emit(0x38000007, 0x901E0000 | (options + 2))  # Disable type popups and translation.
    p.emit(0x3C000474, 0x60006573, 0x901E0000 | (options + 0x40A))
    p.emit(0x38000074, 0x981E0000 | (options + 0x40E))
    probe.call("NewHandle", [12])
    p.nonzero("allocate a single-entry Navigation type list")
    p.emit(0x907E0000 | state, 0x80630000, 0x38000001, 0x90030004, 0x3C005445, 0x60005854, 0x90030008)

    def run_modal(name, arguments, accept, status):
        p.emit(0x38000000 | accept, 0x901E0000 | (state + 20))
        p.emit(0x816015A8, 0x917E0000 | (state + 24), 0x397E0000 | callback, 0x916015A8)
        probe.call(name, arguments)
        p.emit(0x817E0000 | (state + 24), 0x916015A8)
        p.expect(status, name + " maps the Standard File result")

    def file_selection(expected_header, volume_parent=0xFFFE1234, filename=0x04746573):
        word(legacy, expected_header, "Navigation reply publishes its validity and replacement flags")
        selection = probe.ptr(legacy + 6)
        call("AECountItems", [selection, probe.ptr(state + 28)])
        word(state + 28, 1, "the reply contains one file selection")
        call(
            "AEGetNthPtr",
            [selection, 1, 0x66737320, 0, probe.ptr(state + 32), probe.ptr(state + 36), 70, probe.ptr(state + 108)],
        )
        word(state + 32, 0x66737320, "the selected item is an FSSpec descriptor")
        word(state + 108, 70, "the source FSSpec uses the packed Macintosh ABI")
        word(state + 36, volume_parent, "the selected volume and parent ID keep guest byte order")
        word(state + 42, filename, "the selected Pascal filename is copied")
        call("NavDisposeReply", [probe.ptr(legacy)])

    get_args = [0, probe.ptr(legacy), probe.ptr(options), 0, 0, 0, probe.slot(state), 0]
    call("NavGetFile", [0, 0, probe.ptr(options)], -50)
    run_modal("NavGetFile", get_args, 1, 0)
    word(state + 4, 6, "NavGetFile selects StandardGetFile")
    word(state + 8, 0, "the modal adapter leaves the classic filter callback empty")
    word(state + 12, 1, "the type list contains the expected number of entries")
    word(state + 16, 0x54455854, "the type list reaches the classic open dialog")
    file_selection(0x00000100)
    run_modal("NavChooseFile", get_args, 0, -128)
    word(legacy, 0, "cancelling leaves the legacy reply invalid")
    word(legacy + 6, 0x6E756C6C, "cancelling leaves a disposable null descriptor")
    call("NavDisposeReply", [probe.ptr(legacy)])
    p.emit(0x807E0000 | state, 0x80630000, 0x38000002, 0x90030004)
    call("NavGetFile", get_args, -50)  # The handle contains space for only one type.
    p.emit(0x807E0000 | state, 0x80630000, 0x38000001, 0x90030004)

    save_args = [0, probe.ptr(legacy), probe.ptr(options), 0, 0x54455854, 0x74657374, 0]
    run_modal("NavPutFile", save_args, 1, 0)
    word(state + 4, 5, "NavPutFile selects StandardPutFile")
    p.emit(0x807E0000 | (state + 8))
    p.pointer(options + 0x50A, "NavPutFile forwards the Pascal prompt")
    p.emit(0x807E0000 | (state + 12))
    p.pointer(options + 0x40A, "NavPutFile forwards the default filename")
    p.emit(0xA07E0000 | (legacy + 14))
    p.expect(5, "the save reply records the selected script code")
    file_selection(0x00000101)

    p.emit(0x38000007, 0x901E0000 | (creation + 2))
    dialog = probe.slot(state + 112)
    output = probe.ptr(state + 112)
    reply = probe.ptr(legacy)
    create_args = [probe.ptr(creation), probe.slot(state), 0, 0, 0, 0, output]
    call("NavCreateGetFileDialog", create_args)
    call("NavDialogGetUserAction", [dialog])
    call("NavDialogGetWindow", [dialog])
    call("NavDialogGetReply", [dialog, reply], -5694)
    call("NavDialogSetSaveFileName", [dialog, 0], -5695)
    run_modal("NavDialogRun", [dialog], 1, 0)
    call("NavDialogGetUserAction", [dialog], 2)
    call("NavDialogGetReply", [dialog, reply])
    file_selection(0x00020100)
    call("NavDialogRun", [dialog], -5694)
    probe.call("NavDialogDispose", [dialog])

    call("NavCreateChooseFileDialog", create_args)
    probe.call("DisposeHandle", [probe.slot(state)])
    run_modal("NavDialogRun", [dialog], 0, 0)
    call("NavDialogGetUserAction", [dialog], 1)
    call("NavDialogGetReply", [dialog, reply])
    word(legacy, 0x00020000, "the cancelled Carbon dialog leaves an invalid reply")
    call("NavDisposeReply", [reply])
    probe.call("NavDialogDispose", [dialog])

    saved_name = probe.slot(state + 116)
    probe.call("CFStringCreateWithPascalString", [0, probe.ptr(options + 0x40A), 0])
    p.nonzero("allocate a default Carbon save filename")
    p.emit(0x907E0000 | (state + 116), 0x907E0000 | (creation + 0x1A))
    call("NavCreatePutFileDialog", [probe.ptr(creation), 0x54455854, 0x74657374, 0, 0, output])
    call("NavDialogSetSaveFileName", [dialog, saved_name])
    probe.call("CFRelease", [saved_name])
    call("NavCustomControl", [dialog, 24, probe.ptr(options + 0x40A)])
    call("NavCustomControl", [dialog, 23, probe.ptr(state + 120)])
    word(state + 120, 0x04746573, "the legacy control query exposes the current Carbon filename")
    probe.call("NavDialogGetSaveFileName", [dialog])
    p.nonzero("the save dialog owns a filename it can share with the caller")
    p.emit(0x907E0000 | (state + 116))
    call("CFStringGetLength", [saved_name], 4)
    run_modal("NavDialogRun", [dialog], 1, 0)
    call("NavDialogGetUserAction", [dialog], 3)

    directory_proc = options + 0x90
    p.routines.append(
        (
            directory_proc,
            [
                0x90820000 | (state + 128),  # Capture volume and directory ID.
                0x90A20000 | (state + 132),
                0x3800FFFE,
                0xB0070000,
                0x38000002,
                0x90070002,
                0x3C000472,
                0x60006F6F,
                0x90070006,
                0x38000074,
                0x9807000A,
                0x38600000,
                0x4E800020,
            ],
        )
    )
    p.emit(0x81601748, 0x917E0000 | (state + 24), 0x397E0000 | directory_proc, 0x91601748)
    p.emit(0x3800FFFF, 0xB01E0000 | (legacy + 28))
    probe.call("NavDialogGetReply", [dialog, reply])
    p.emit(0x817E0000 | (state + 24), 0x91601748)
    p.expect(0, "get a Carbon save reply from the finished dialog")
    word(state + 128, -2, "save replies resolve the selected directory on its actual volume")
    word(state + 132, 0x12345678, "save replies resolve the selected parent ID")
    probe.call("NavDialogDispose", [dialog])
    p.emit(0xA07E0000 | (legacy + 28))
    p.expect(0, "the save reply clears its extension visibility and reserved byte")
    call("CFStringGetLength", [probe.slot(legacy + 24)], 4)
    file_selection(0x00020101, 0xFFFE0000, 0x04726F6F)

    # Reuse the completed dialog's scratch storage for the on-disk preview.
    spec, descriptor, reference = state, state + 72, state + 80
    rect, area, observed = state + 84, state + 92, state + 100
    saved_map, preview_map, has_preview = state + 108, state + 112, state + 116
    world, old_world, old_device, pixmap = state + 120, state + 124, state + 128, state + 132
    pascal_name = probe.far_raw(b"\x12NavPreview Fixture")
    bounds = struct.pack(">4h", 0, 0, 24, 24)
    picture = struct.pack(">H", 22) + bounds + b"\x11\x01\x31" + bounds + b"\xff"
    source = probe.far_raw(picture)
    target = probe.far_raw(struct.pack(">4h", 4, 4, 20, 20))
    white = probe.far_raw(b"\xff" * 6)

    file = probe.ptr(spec)
    obj = probe.ptr(descriptor)
    call("FSMakeFSSpec", [probe.slot(0x1FC0), probe.slot(0x1F42), pascal_name, file], -43)
    probe.call("FSpCreateResFile", [file, 0x524F4D57, 0x54455854, -1])
    call("ResError", [])
    probe.call("CurResFile", [])
    p.emit(0x907E0000 | saved_map)
    probe.call("FSpOpenResFile", [file, 3])
    p.emit(0x907E0000 | preview_map)
    call("ResError", [])
    call("AECreateDesc", [0x66737320, file, 70, obj])
    call("NavHasPreview", [obj, 0], -50)
    call("NavHasPreview", [obj, probe.ptr(has_preview)])
    p.emit(0x887E0000 | has_preview)
    p.expect(0, "a resource fork without a preview reports false")
    call("NavCreatePreview", [obj, 0, source, len(picture)], -50)
    for _ in range(2):
        call("NavCreatePreview", [obj, 0x50494354, source, len(picture)])
        call("NavHasPreview", [obj, probe.ptr(has_preview)])
        p.emit(0x887E0000 | has_preview)
        p.expect(1, "a current preview is discoverable through its pnot resource")
    probe.call("Count1Resources", [0x50494354])
    p.expect(1, "replacing a preview removes the older owned picture")
    probe.call("CurResFile")
    p.emit(0x809E0000 | preview_map)
    p.equal("preview updates leave the caller's resource map open and selected")
    probe.call("CloseResFile", [probe.slot(preview_map)])
    probe.call("UseResFile", [probe.slot(saved_map)])

    probe.call("NavCreatePreviewReference")
    p.nonzero("allocate a Navigation preview reference")
    p.emit(0x907E0000 | reference, 0x38000000, 0x901E0000 | rect, 0x3C000018, 0x60000018)
    p.emit(0x901E0000 | (rect + 4))
    ref = probe.slot(reference)
    call("NavGetPreviewArea", [ref, probe.ptr(area)])
    word(area, 0, "a new preview has an empty area")
    call("NavGetPreviewArea", [ref, 0], -50)
    probe.call("GetGWorld", [probe.ptr(old_world), probe.ptr(old_device)])
    call("NewGWorld", [probe.ptr(world), 32, probe.ptr(rect), 0, 0, 0])
    probe.call("GetGWorldPixMap", [probe.slot(world)])
    p.nonzero("get the preview destination pixmap")
    p.emit(0x907E0000 | pixmap)
    call("LockPixels", [probe.slot(pixmap)], 1)
    probe.call("SetGWorld", [probe.slot(world), 0])
    probe.call("RGBBackColor", [white])
    probe.call("EraseRect", [probe.ptr(rect)])
    call("NavSetupPreview", [ref, probe.slot(world), obj, probe.ptr(rect), 0, 0])
    call("NavSetPreviewArea", [ref, target])
    call("NavGetPreviewArea", [ref, probe.ptr(area)])
    word(area, 0x00040004, "NavGetPreviewArea returns the changed origin")
    word(area + 4, 0x00140014, "NavGetPreviewArea returns the changed bounds")
    call("NavDrawPreview", [ref, 0, 1])
    probe.call("GetCPixel", [8, 8, probe.ptr(observed)])
    word(observed, 0, "the cached QuickDraw picture paints in the preview area")
    probe.call("GetCPixel", [2, 2, probe.ptr(observed)])
    word(observed, 0xFFFFFFFF, "preview drawing leaves pixels outside its bounds intact")
    call("NavHandlePreviewEvent", [ref, 0])
    probe.call("NavDisposePreviewReference", [ref])
    call("AEDisposeDesc", [obj])
    probe.call("SetGWorld", [probe.slot(old_world), probe.slot(old_device)])
    probe.call("UnlockPixels", [probe.slot(pixmap)])
    probe.call("DisposeGWorld", [probe.slot(world)])
    call("FSpDelete", [file])


def url_access_operations(probe, destination):
    p = probe.p
    source = destination / "URL buffer fixture"
    base = 0x2040
    text = probe.slot(base + 32)
    url_ref, buffer, cf_path, cf_url = [probe.slot(base + offset) for offset in (0, 4, 16, 20)]

    path = probe.far_raw(FrameworksProbe.hfs_path(source).encode("ascii") + b"\0")
    url_name = probe.far_raw(b"URLString\0")
    resource_size = probe.far_raw(b"URLResourceSize\0")
    file_type = probe.far_raw(b"URLFileType\0")
    type_bytes = probe.far_raw(b"TEXT")
    callback = probe.ptr(base + 48)
    p.routines.append(
        (
            callback[1],
            [
                0x90830000,  # Save the event in the caller's context.
                0x8085000C,  # Save URLCallbackInfo.currentSize.
                0x90830004,
                0x38600000,
                0x4E800020,
            ],
        )
    )
    context = probe.ptr(base + 40)
    data_events = (1 << 20) | (1 << 5)

    def call(name, args=(), status=0):
        probe.call(name, args)
        p.expect(status, name + " returns the expected URL Access result")

    def word(offset, expected, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(expected, label)

    def state(expected):
        call("URLGetCurrentState", [url_ref, probe.ptr(base + 12)])
        word(12, expected, "URL transfer reports the expected state")

    probe.call("CFStringCreateWithCString", [0, path, 0])
    p.nonzero("create the fixture's HFS path")
    p.emit(0x907E0000 | (base + 16))
    probe.call("CFURLCreateWithFileSystemPath", [0, cf_path, 1, 0])
    p.nonzero("create the fixture's file URL")
    p.emit(0x907E0000 | (base + 20))
    probe.call("CFURLGetString", [cf_url])
    p.nonzero("serialize the fixture's file URL")
    p.emit(0x907E0000 | (base + 24))
    probe.call("NewPtr", [1024])
    p.nonzero("allocate a buffer for the serialized URL")
    p.emit(0x907E0000 | (base + 32))
    probe.call("CFStringGetCString", [probe.slot(base + 24), text, 1024, 0x08000100])
    p.expect(1, "copy the serialized URL to guest memory")
    probe.call("CFRelease", [cf_path])
    probe.call("CFRelease", [cf_url])

    call("URLNewReference", [text, probe.ptr(base)])
    probe.call("DisposePtr", [text])
    state(0)
    call("URLGetPropertySize", [url_ref, url_name, probe.ptr(base + 8)])
    p.emit(0x807E0000 | (base + 8))
    p.nonzero("URLString size includes the serialized URL")
    call("URLGetProperty", [url_ref, url_name, probe.ptr(base + 28), 2], -30779)
    call("URLSetProperty", [url_ref, file_type, type_bytes, 4])
    call("URLGetProperty", [url_ref, file_type, probe.ptr(base + 12), 4])
    word(12, 0x54455854, "URL file type round-trips as an OSType")
    call("URLOpen", [url_ref, 0, 0, callback, data_events, context])
    state(1)
    call("URLIdle")
    state(4)
    call("URLGetProperty", [url_ref, resource_size, probe.ptr(base + 12), 4])
    word(12, 16388, "resource size is the full data fork length")
    call("URLIdle")
    state(5)
    for count, marker in ((16384, 0x48454144), (4, 0x5441494C)):
        call("URLIdle")
        state(21)
        word(40, 21, "data availability is delivered to the registered callback")
        word(44, 16384 if count == 16384 else 16388, "callback reports cumulative bytes transferred")
        call("URLGetDataAvailable", [url_ref, probe.ptr(base + 8)])
        word(8, count, "available bytes match the next buffer")
        call("URLGetBuffer", [url_ref, probe.ptr(base + 4), probe.ptr(base + 8)])
        word(8, count, "returned buffer has the correct length")
        p.emit(0x807E0000 | (base + 4))
        p.nonzero("buffered data is accessible to the guest")
        p.emit(0x80630000)
        p.expect(marker, "buffer contains the fixture's expected bytes")
        call("URLIdle")
        call("URLGetDataAvailable", [url_ref, probe.ptr(base + 8)])
        word(8, 0, "idle leaves a borrowed buffer with its owner")
        call("URLReleaseBuffer", [url_ref, buffer])
        call("URLReleaseBuffer", [url_ref, buffer], -30781)
    call("URLIdle")
    state(6)
    word(40, 6, "transaction completion is delivered to the registered callback")
    call("URLIdle")
    state(9)
    call("URLGetError", [url_ref, probe.ptr(base + 12)])
    word(12, 0, "completed transfer has no error")

    call("URLOpen", [url_ref, 0, 0, 0, 0, 0])
    call("URLAbort", [url_ref])
    state(8)
    call("URLIdle")
    state(9)
    call("URLGetError", [url_ref, probe.ptr(base + 12)])
    word(12, -128, "abort delivers userCanceledErr")
    call("URLDisposeReference", [url_ref])


def classic_clock_and_location(probe):
    p = probe.p
    _, base = probe.raw(bytes(48))
    original, written, result = (probe.ptr(base + offset) for offset in (0, 12, 24))
    location = (0x23456789, 0xE1020304, 0xFFFE0E00)
    struct.pack_into(">4I", p.data, base + 12, *location, 0x12121212)
    struct.pack_into(">I", p.data, base + 36, 0xA55AA55A)
    probe.call("ReadLocation", [original])
    probe.call("WriteLocation", [written])
    probe.call("InitUtil")
    p.expect(0, "reopen the guest system parameter file")
    probe.call("ReadLocation", [result])
    for offset, value in enumerate(location):
        p.emit(0x807E0000 | (base + 24 + 4 * offset))
        p.expect(value, "location survives a parameter RAM reload")
    p.emit(0x807E0000 | (base + 36))
    p.expect(0xA55AA55A, "location read writes only the twelve-byte record")

    _, dates = probe.raw(struct.pack(">HIH6I", 1, 0x12345678, 0x4000, 0, 0, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF))
    date = probe.ptr(dates)
    converted = probe.ptr(dates + 8)
    saved_clock = probe.ptr(dates + 16)
    maximum = probe.ptr(dates + 24)

    def utc_call(name, args, expected=0):
        probe.call(name, args)
        p.expect(expected, name + " returns the expected clock status")

    def clock_word(offset, value):
        p.emit(0x807E0000 | (dates + offset))
        p.expect(value, "clock record stores the expected big-endian seconds and fraction")

    utc_call("ConvertLocalTimeToUTC", [0x12345678, converted])
    clock_word(8, 0x12364878)
    utc_call("ConvertUTCToLocalTime", [0x12364878, converted])
    clock_word(8, 0x12345678)
    utc_call("ConvertUTCToLocalTime", [0, converted], -8850)
    utc_call("ConvertLocalTimeToUTC", [0xFFFFFFFF, converted], -8851)
    clock_word(8, 0x12345678)
    utc_call("ConvertUTCToLocalTime", [0, 0], -50)

    utc_call("ConvertUTCToLocalDateTime", [date, converted])
    clock_word(8, 0x00011232)
    clock_word(12, 0x64784000)
    utc_call("ConvertLocalToUTCDateTime", [converted, converted])
    clock_word(8, 0x00011234)
    clock_word(12, 0x56784000)
    utc_call("ConvertLocalToUTCDateTime", [maximum, converted], -8851)
    utc_call("ConvertUTCToLocalDateTime", [saved_clock, converted], -8850)
    clock_word(12, 0x56784000)
    utc_call("ConvertLocalToUTCDateTime", [0, converted], -50)

    utc_call("GetLocalDateTime", [saved_clock, 0])
    utc_call("SetUTCDateTime", [date, 0])
    probe.call("TickCount")
    utc_call("GetLocalDateTime", [converted, 0])
    clock_word(8, 0x00011232)
    p.emit(0x8060020C)
    p.expect(0x12326478, "extended UTC setter updates the classic Time low-memory global")
    utc_call("GetUTCDateTime", [converted, 0])
    clock_word(8, 0x00011234)
    utc_call("SetLocalDateTime", [date, 0])
    utc_call("GetUTCDateTime", [converted, 0])
    clock_word(8, 0x00011236)
    utc_call("GetUTCDateTime", [0, 0], -50)
    utc_call("SetLocalDateTime", [saved_clock, 1], -50)
    utc_call("SetLocalDateTime", [saved_clock, 0])

    probe.call("WriteLocation", [original])

    previous, observed = (probe.ptr(base + offset) for offset in (40, 44))
    probe.call("ReadDateTime", [previous])
    p.expect(0, "read the virtual Macintosh clock")
    probe.call("SetDateTime", [0xCAFE0000])
    p.expect(0, "set the virtual Macintosh clock")
    probe.call("GetDateTime", [observed])
    p.emit(0xA07E0000 | (base + 44))
    p.expect(0xCAFE, "GetDateTime sees the adjusted clock")
    p.emit(0xA060020C)  # Time low-memory global, high halfword.
    p.expect(0xCAFE, "the native clock write updates low-memory Time")
    probe.call("SetDateTime", [probe.slot(base + 40)])
    p.expect(0, "restore the virtual Macintosh clock")

    # Reuse the location slots after restoring them.
    for offset, value in enumerate((0x07D10001, 0x00010000, 0, 0x00020000)):
        probe.arguments([value], 0)
        p.emit(0x907E0000 | (base + 4 * offset))
    probe.call("SetTime", [original])
    probe.call("GetDateTime", [observed])
    p.emit(0xA07E0000 | (base + 44))
    p.expect(3061152000 >> 16, "SetTime converts Gregorian dates to Macintosh seconds")
    probe.call("SetDateTime", [probe.slot(base + 40)])
    p.expect(0, "restore the original clock after SetTime")
    probe.call("Delay", [0, observed])
    p.emit(0x807E0000 | (base + 44))
    p.nonzero("Delay writes the final tick count")
    probe.call("GetSysPPtr")
    p.expect(0x1F8, "GetSysPPtr points to the live System Parameters record")
    probe.call("ScreenRes", [probe.ptr(base + 44), probe.ptr(base + 46)])
    for offset, address in ((44, 0x104), (46, 0x102)):
        p.emit(0xA07E0000 | (base + offset), 0xA0800000 | address)
        p.equal("ScreenRes copies the corresponding low-memory DPI")


def classic_state_operations(probe):
    p, ptr, slot = probe.p, probe.ptr, probe.slot
    _, base = probe.raw(bytes(88))

    def word(offset, expected, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(expected, label)

    p.emit(0xA0600A9A, 0x907E0000 | base, 0x3860FFFD, 0xB0600A9A)
    probe.call("GetAlertStage")
    p.expect(-3, "GetAlertStage reads the signed legacy alert stage")
    probe.call("ResetAlertStage")
    p.emit(0xA0600A9A)
    p.expect(0, "ResetAlertStage clears the shared Dialog Manager stage")
    p.emit(0x807E0000 | base, 0xB0600A9A, 0xA0600AFA, 0x907E0000 | base)
    probe.call("SetDialogFont", [-101])
    p.emit(0xA8600AFA)
    p.expect(-101, "SetDialogFont updates the shared signed font family")
    probe.call("SetDialogFont", [slot(base)])

    probe.call("AEInitializeDesc", [ptr(base)])
    word(0, 0x6E756C6C, "initialize a null Apple event descriptor")
    word(4, 0, "initializing a descriptor clears its data handle")
    probe.call("GetGlobalMouse", [ptr(base + 8)])
    p.emit(0x807E0000 | (base + 8), 0x80800830)
    p.equal("GetGlobalMouse copies the shared guest coordinates")

    for getter, setter in (
        ("TEGetDoTextHook", "TESetDoTextHook"),
        ("TEGetFindWordHook", "TESetFindWordHook"),
        ("TEGetRecalcHook", "TESetRecalcHook"),
        ("TEGetScrapHandle", "TESetScrapHandle"),
    ):
        probe.call(getter)
        p.emit(0x907E0000 | (base + 12))
        probe.call(setter, [ptr(base + 16)])
        probe.call(getter)
        p.pointer(base + 16, getter + " observes the corresponding setter")
        if getter == "TEGetScrapHandle":
            probe.call("TEScrapHandle")
            p.pointer(base + 16, "InterfaceLib and Carbon TextEdit scrap accessors share storage")
        probe.call(setter, [slot(base + 12)])
    probe.call("TEGetScrapLength")
    p.emit(0x907E0000 | (base + 12))
    probe.call("TESetScrapLength", [13])
    probe.call("TEGetScrapLength")
    p.expect(13, "TextEdit reads the shared scrap length")
    probe.call("TESetScrapLength", [slot(base + 12)])

    text_rect = probe.raw(struct.pack(">4h", 4, 4, 64, 144))
    probe.call("TENew", [text_rect, text_rect])
    p.nonzero("allocate a classic TextEdit record")
    p.emit(0x907E0000 | (base + 80))
    text_edit = slot(base + 80)
    for action, state in ((1, 1), (0, 0)):
        probe.call("TEFeatureFlag", [0, action, text_edit])
        probe.call("TEFeatureFlag", [0, -1, text_edit])
        p.expect(state, "TEFeatureFlag reports the TextEdit auto-scroll state")
    probe.call("TEActivate", [text_edit])
    p.emit(0x807E0000 | (base + 80), 0x80630000, 0xA0630024)
    p.nonzero("TEActivate enables the classic TextEdit record")
    probe.call("TESetText", [probe.raw(b"ab"), 2, text_edit])
    probe.call("TESetSelect", [2, 2, text_edit])
    probe.call("TEClick", [0x00050004, 0, text_edit])
    probe.call("TEKey", [ord("X"), text_edit])
    probe.call("TEGetText", [text_edit])
    p.emit(0x80630000, 0x80630000, 0x5463C23E)
    p.expect(0x00586162, "TEClick moves the insertion point and TEKey inserts at it")
    probe.call("TEIdle", [text_edit])
    # The classic idle path must respect the current trap patch.
    p.emit(0x81601568, 0x917E1820, 0x397E1800, 0x91601568, 0x38000000, 0x901E1900)
    probe.call("TEIdle", [text_edit])
    p.emit(0x807E1900)
    p.expect(1, "classic TEIdle follows the live Toolbox vector")
    # Text services set bit 7 of the flags in the TERec extension handle.
    p.emit(0x809E0000 | (base + 80), 0x80840000, 0x80840042, 0x80840000)
    p.emit(0x909E0000 | (base + 84), 0x80A40010, 0x90BE0000 | (base + 76))
    p.emit(0x60A50080, 0x90A40010, 0x38000000, 0x901E1900)
    probe.call("TEIdle", [text_edit])
    p.emit(0x809E0000 | (base + 84), 0x80BE0000 | (base + 76), 0x90A40010, 0x817E1820, 0x91601568)
    p.emit(0x807E1900)
    p.expect(0, "inline TextEdit leaves caret idle handling to saved CarbonLib")
    probe.call("TEDeactivate", [text_edit])
    p.emit(0x807E0000 | (base + 80), 0x80630000, 0xA0630024)
    p.expect(0, "TEDeactivate clears the active state")
    probe.call("TEDispose", [text_edit])

    content = probe.raw(b"ardi-PPC")
    probe.call("PtrToHand", [content, ptr(base + 20), 8])
    p.expect(0, "PtrToHand copies guest bytes")
    p.emit(0x807E0000 | (base + 20), 0x907E0000 | (base + 24))
    probe.call("HandToHand", [ptr(base + 24)])
    p.expect(0, "HandToHand duplicates the guest handle")
    p.emit(0x807E0000 | (base + 24), 0x80630000, 0x80630004)
    p.expect(0x2D505043, "duplicate handle preserves copied data")
    probe.call("DisposeHandle", [slot(base + 24)])
    probe.call("GetHandleSize", [slot(base + 20)])
    p.expect(8, "disposing the duplicate leaves the source live")
    lower, upper = probe.raw(b"\x01a"), probe.raw(b"\x01A")
    for name, expected in (("EqualString", [1, 0]), ("RelString", [0, 1])):
        for index, flags in enumerate(((0, 0), (1, 1))):
            for side in (0, 1):
                probe.call(name, [lower, upper, *flags], side=side)
                p.expect(expected[index], name + " respects case sensitivity")
    c_string = probe.far_raw(b"a\x8a\x00Z")
    accented = probe.far_raw(b"A\x80\x00Q")
    probe.call("relstring", [c_string, accented, 0, 1])
    p.expect(0, "relstring handles MacRoman case and stops at the first NUL")
    probe.call("relstring", [c_string, accented, 1, 1])
    p.expect(1, "relstring supports case-sensitive ordering")
    probe.call("upperstring", [c_string, 1])
    probe.arguments([c_string], 0)
    p.emit(0x80630000)
    p.expect(0x4180005A, "upperstring uppercases the text and preserves bytes after NUL")
    probe.call("GetApplicationScript")
    p.expect(0, "GetApplicationScript resolves the default Roman application font")
    probe.call("UnsignedFixedMulDiv", [0x18000, 0x30000, 0x20000])
    p.expect(0x24000, "UnsignedFixedMulDiv preserves the full intermediate product")
    probe.call("UnsignedFixedMulDiv", [0xFFFFFFFF, 0x20000, 0x10000])
    p.expect(0xFFFFFFFF, "UnsignedFixedMulDiv saturates an out-of-range quotient")
    probe.call("GetMainDevice")
    p.emit(0x907E0000 | (base + 24))
    probe.call("GetIconDevice", [ptr(base + 28)])
    p.emit(0x807E0000 | (base + 24), 0x809E0000 | (base + 28))
    p.equal("GetIconDevice returns the selected screen device")

    for vector, name, args, capture, expected in (
        (0x1524, "SysError", [-17], 0x1910, -17),
        (
            0x1008,
            "stdtext",
            [2, c_string, probe.far_raw(b"\x00\x02\x00\x03"), probe.far_raw(b"\x00\x04\x00\x05")],
            0x1918,
            0x20003,
        ),
    ):
        p.emit(0x81600000 | vector, 0x917E1820, 0x397E1800, 0x91600000 | vector)
        probe.call(name, args)
        p.emit(0x817E1820, 0x91600000 | vector, 0x807E0000 | capture)
        p.expect(expected, name + " forwards the native arguments through the installed Toolbox vector")
    probe.call("DebugStr", [probe.far_raw(b"\x12Toolbox debug text")])
    for name, expected in (
        ("LowercaseText", 0x8A615A5A),
        ("UppercaseText", 0x80415A5A),
        ("StripDiacritics", 0x41615A5A),
        ("UppercaseStripDiacritics", 0x41415A5A),
    ):
        probe.arguments([0x80615A5A], 0)
        p.emit(0x907E0000 | (base + 24))
        probe.call(name, [ptr(base + 24), 2, 0])
        word(24, expected, name + " preserves the rest of the byte buffer")
    probe.call("PtrToHand", [probe.raw(b"zap"), ptr(base + 24), 3])
    p.expect(0, "allocate a substitution handle")
    probe.call("ReplaceText", [slot(base + 20), slot(base + 24), probe.raw(b"\x03PPC")])
    p.expect(1, "ReplaceText reports the occurrence it replaced")
    p.emit(0x807E0000 | (base + 20), 0x80630000, 0x80630004)
    p.expect(0x2D7A6170, "ReplaceText updates the actual text handle")
    probe.call("DisposeHandle", [slot(base + 24)])

    probe.call("TEGetScrapHandle")
    p.emit(0x907E0000 | (base + 12))
    probe.call("TEGetScrapLength")
    p.emit(0x907E0000 | (base + 24))
    probe.call("TESetScrapHandle", [slot(base + 20)])
    probe.call("TESetScrapLength", [8])
    p.emit(0x806015F0, 0x907E0000 | (base + 76))  # _ZeroScrap is a classic-only vector.
    probe.call("CallUniversalProc", [slot(base + 76), 0x30])
    p.expect(0, "reset the system scrap before the TextEdit round trip")
    probe.call("TEToScrap")
    p.expect(0, "TEToScrap publishes the TextEdit text")
    probe.call("SetHandleSize", [slot(base + 20), 0])
    probe.call("TEFromScrap")
    p.expect(0, "TEFromScrap retrieves the system text")
    probe.call("TEGetScrapLength")
    p.expect(8, "TextEdit recovers the original scrap length")
    p.emit(0x807E0000 | (base + 20), 0x80630000, 0x80630004)
    p.expect(0x2D7A6170, "the recovered text matches the published bytes")
    probe.call("TESetScrapHandle", [slot(base + 12)])
    probe.call("TESetScrapLength", [slot(base + 24)])
    probe.call("CallUniversalProc", [slot(base + 76), 0x30])
    probe.call("DisposeHandle", [slot(base + 20)])
    probe.call("NewPtr", [19])
    p.nonzero("allocate a live pointer for GetPtrSize")
    p.emit(0x907E0000 | (base + 20))
    probe.call("GetPtrSize", [slot(base + 20)])
    p.expect(19, "GetPtrSize reports the logical allocation size")
    probe.call("DisposePtr", [slot(base + 20)])
    probe.call("PurgeSpace", [ptr(base + 20), ptr(base + 24)])
    p.emit(0x807E0000 | (base + 20))
    p.nonzero("query the application's purgeable and free space")

    for clock in ("Microseconds", "UpTime"):
        probe.call(clock, [ptr(base + 28)])
        p.emit(0x807E0000 | (base + 28), 0x809E0000 | (base + 32), 0x7C632378)
        p.nonzero(clock + " writes a big-endian 64-bit clock value")
    probe.call("AbsoluteToNanoseconds", [ptr(base + 36), 1, 25])
    word(36, 40, "AbsoluteTime conversion preserves the high word")
    word(40, 1000, "25 time-base ticks represent one microsecond")
    probe.call("NanosecondsToAbsolute", [ptr(base + 44), 40, 1039])
    word(44, 1, "inverse tick conversion preserves the high word")
    word(48, 25, "inverse conversion discards a fractional tick")

    def time_words(value):
        return [value >> 32, value & 0xFFFFFFFF]

    forever = (1 << 31) - 1
    wide_forever = (1 << 63) - 1
    for name, inputs, expected in (
        ("AbsoluteToNanoseconds", [0xFFFFFFFF, 0xFFFFFFFF], wide_forever),
        ("DurationToAbsolute", [-250], 6250),
        ("DurationToAbsolute", [2], 50000),
        ("DurationToAbsolute", [forever], wide_forever),
        ("DurationToNanoseconds", [-0x80000000], (1 << 31) * 1000),
        ("DurationToNanoseconds", [2], 2000000),
        ("DurationToNanoseconds", [forever], wide_forever),
        ("AddAbsoluteToAbsolute", [1, 0xFFFFFFFF, 0, 3], 0x200000002),
        ("AddAbsoluteToAbsolute", [0xFFFFFFFF, 0xFFFFFFFF, 0, 1], 0),
        ("SubAbsoluteFromAbsolute", [2, 2, 0, 3], 0x1FFFFFFFF),
        ("SubAbsoluteFromAbsolute", [0, 2, 1, 0], 0),
        ("AbsoluteDeltaToNanoseconds", [0, 50, 0, 25], 1000),
        ("AbsoluteDeltaToNanoseconds", [0, 25, 0, 50], 0),
        ("AddDurationToAbsolute", [-1, 1, 10], 0x100000023),
        ("AddDurationToAbsolute", [forever, 1, 10], wide_forever),
        ("SubDurationFromAbsolute", [-2, 0, 10], 40),
        ("SubDurationFromAbsolute", [-2, 0, 51], 0),
        ("SubDurationFromAbsolute", [forever, 0, 10], wide_forever),
        ("AddNanosecondsToAbsolute", [0, 1039, 1, 5], 0x10000001E),
        ("SubNanosecondsFromAbsolute", [0, 1039, 0, 5], 20),
        ("SubNanosecondsFromAbsolute", [0, 1039, 0, 26], 0),
    ):
        probe.call(name, [ptr(base + 36), *inputs])
        high, low = time_words(expected)
        word(36, high, name + " high word")
        word(40, low, name + " low word")

    for name, inputs, expected in (
        ("AbsoluteToDuration", [0, 24], 0),
        ("AbsoluteToDuration", [0, 25], -1),
        ("AbsoluteToDuration", time_words(forever * 25), -forever),
        ("AbsoluteToDuration", time_words(forever * 25 + 1), forever // 1000),
        ("AbsoluteToDuration", time_words(forever * 25000 + 1), forever),
        ("NanosecondsToDuration", [0, 999], 0),
        ("NanosecondsToDuration", [0, 1000], -1),
        ("NanosecondsToDuration", time_words(forever * 1000), -forever),
        ("NanosecondsToDuration", time_words(forever * 1000 + 1), forever // 1000),
        ("NanosecondsToDuration", time_words(forever * 1000000 + 1), forever),
        ("AbsoluteDeltaToDuration", [0, 50, 0, 25], -1),
        ("AbsoluteDeltaToDuration", [0, 25, 0, 50], 0),
    ):
        probe.call(name, inputs)
        p.expect(expected, name + " signed duration")

    probe.call("SecondsToDate", [0, ptr(base + 52)])
    word(52, 0x07700001, "the Macintosh epoch begins in January 1904")
    word(56, 0x00010000, "the epoch begins at midnight on January 1")
    probe.call("DateToSeconds", [ptr(base + 52), ptr(base + 68)])
    word(68, 0, "the epoch date converts back to zero seconds")
    probe.call("GetTime", [ptr(base + 72)])
    p.emit(0xA07E0000 | (base + 72))
    p.nonzero("GetTime writes the current Macintosh calendar date")


FIXED_DECIMAL_APIS = (
    "FixMul",
    "FracMul",
    "FixDiv",
    "FracDiv",
    "FixRatio",
    "NumToString",
    "StringToNum",
    "Fix2X",
    "Frac2X",
    "X2Fix",
    "X2Frac",
    "FracCos",
    "FracSin",
    "FracSqrt",
    "FixATan2",
)


def fixed_decimal_operations(probe):
    p = probe.p
    # Include exact half units of either sign, overflow, signed 16-bit
    # parameters, zero divisors and values on either side of the radix point.
    values = (-0x80000000, -0x40000000, -65537, -32768, -3, -1, 0, 1, 3, 32768, 65537, 0x40000000, 0x7FFFFFFF)
    for name in FIXED_DECIMAL_APIS[:5]:
        for left in values:
            for right in values:
                probe.scalar(name, [left, right])
    for name, left, right, answer in (
        ("FixDiv", 1, 131072, 1),
        ("FixDiv", -1, 131072, 0),
        ("FracDiv", 1, -0x80000000, 0),
        ("FixRatio", 0x8000, 0xFFFF, 0x7FFFFFFF),
        ("FixMul", -0x80000000, -0x80000000, 0x7FFFFFFF),
        ("FracMul", 0x40000000, 0x40000000, 0x40000000),
    ):
        probe.scalar(name, [left, right], answer)

    for name, power in (("Fix2X", 16), ("Frac2X", 30)):
        for value in (-0x80000000, -65537, -1, 0, 1, 65537, 0x7FFFFFFF):
            for side in (0, 1):
                probe.call(name, [value], side)
                p.emit(0xD83E2000)
                for offset, word in enumerate(struct.unpack(">2I", struct.pack(">d", value * 2.0**-power))):
                    p.emit(0x807E2000 + 4 * offset)
                    p.expect(word, name + " returns the exact FPR value")
    for name, power in (("X2Fix", 16), ("X2Frac", 30)):
        for value in (
            -math.inf,
            -32768.0,
            -1.75,
            -0.0,
            0.0,
            1.25,
            32768.0,
            math.inf,
            math.nan,
            *(units * 2.0**-power for units in (-1.75, -1.5, -0.5, -0.25, 0.25, 0.5, 1.5, 1.75)),
        ):
            _, source = probe.raw(struct.pack(">d", value))
            for side in (0, 1):
                p.emit(0xC83E0000 | source)
                probe.call(name, side=side)
                if side:
                    p.emit(0x809E2000)
                    p.equal(name + " agrees with the saved binary64 conversion")
                else:
                    p.emit(0x907E2000)

    octant = 0xC910
    angles = {0, 1, -1, -0x80000000, 0x7FFFFFFF, 87 * octant + 37, -437 * octant + 13}
    angles.update(n * octant + offset for n in range(-8, 9) for offset in (-1, 0, 1))
    for name in ("FracCos", "FracSin"):
        for angle in sorted(angles):
            probe.scalar(name, [angle])
    for value in (0, 1, 4, 33, 0x10000000, 0x40000000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF):
        probe.scalar("FracSqrt", [value])
    for x, y in ((0, 0), (1, 0), (0, 1), (1, 1), (1, 3), (3, 1), (0x80000000, 0x7FFFFFFF)):
        for x_sign, y_sign in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
            probe.scalar("FixATan2", [x_sign * x, y_sign * y])

    base = 0x2000

    def store(offset, data):
        for index in range(0, len(data), 4):
            word = int.from_bytes(data[index : index + 4].ljust(4, b"\0"), "big")
            probe.arguments([word], 0)
            p.emit(0x907E0000 | (base + offset + index))

    for value in (-0x80000000, -1000000, -1, 0, 7, 123456789, 0x7FFFFFFF):
        text = str(value).encode("ascii")
        expected = (bytes([len(text)]) + text).ljust(16, b"\xA5")
        for side in (0, 1):
            store(0, b"\xA5" * 16)
            probe.call("NumToString", [value, probe.ptr(base)], side)
            for index in range(0, len(expected), 4):
                p.emit(0x807E0000 | (base + index))
                p.expect(int.from_bytes(expected[index : index + 4], "big"), "Pascal decimal text and guard bytes")
    for text, expected in (
        (None, 0),
        (b"", 0),
        (b"+", 0),
        (b"-0", 0),
        (b"0072", 72),
        (b"+123", 123),
        (b"-2147483648", 0x80000000),
        (b"4294967296", 0),
        (b"4294967297", 1),
        (b"a?", 25),
        (b"1-4", 234),
    ):
        if text is not None:
            store(0, bytes([len(text)]) + text)
        for side in (0, 1):
            probe.call("StringToNum", [probe.ptr(base) if text is not None else 0, probe.ptr(base + 16)], side)
            p.emit(0x807E0000 | (base + 16))
            p.expect(expected, "Pascal decimal parser handles signs, wrapping and low nibbles")


def fixmath_wide_operations(probe):
    p = probe.p
    ptr = probe.ptr
    _, base = probe.raw(bytes(24))
    target, source, remainder = ptr(base), ptr(base + 8), ptr(base + 16)

    def word(offset, expected, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(expected, label)

    def wide(expected, label):
        word(0, (expected >> 32) & 0xFFFFFFFF, label + " high word")
        word(4, expected & 0xFFFFFFFF, label + " low word")

    for lhs, rhs in ((-7, 11), (-2147483648, -2147483648), (2147483647, 2147483647)):
        probe.call("WideMultiply", [lhs, rhs, target])
        p.pointer(base, "WideMultiply returns its target")
        wide(lhs * rhs, "WideMultiply preserves full product precision")

    probe.call("WideMultiply", [-7, 11, target])
    probe.call("WideMultiply", [19, 14, source])
    probe.call("WideCompare", [target, source])
    p.expect(-1, "WideCompare orders signed inputs")
    probe.call("WideCompare", [source, source])
    p.expect(0, "WideCompare accepts aliased inputs")
    probe.call("WideAdd", [target, source])
    p.pointer(base, "WideAdd returns its target")
    wide(189, "WideAdd propagates carry")
    probe.call("WideSubtract", [target, source])
    p.pointer(base, "WideSubtract returns its target")
    wide(-77, "WideSubtract propagates borrow")
    probe.call("WideNegate", [target])
    p.pointer(base, "WideNegate returns its target")
    wide(77, "WideNegate changes sign")
    probe.call("WideShift", [target, 1])
    p.pointer(base, "WideShift returns its target")
    wide(39, "WideShift rounds after shifting right")
    probe.call("WideBitShift", [target, 1])
    p.pointer(base, "WideBitShift returns its target")
    wide(19, "WideBitShift discards shifted-out bits")
    probe.call("WideBitShift", [target, -34])
    wide(19 << 34, "WideBitShift shifts across word boundaries")

    probe.call("WideMultiply", [-9, 9, target])
    probe.call("WideDivide", [target, 8, remainder])
    p.expect(-10, "WideDivide truncates the quotient when storing a remainder")
    word(16, -1, "WideDivide preserves the dividend sign in the remainder")
    wide(-81, "WideDivide preserves the dividend")
    probe.call("WideMultiply", [7, 1, target])
    probe.call("WideDivide", [target, 4, 0])
    p.expect(2, "WideDivide rounds when no remainder is requested")
    probe.call("WideMultiply", [0x40000000, 16, target])
    probe.call("WideDivide", [target, 1, remainder])
    p.expect(0x7FFFFFFF, "WideDivide saturates large quotients")
    word(16, 0x80000000, "WideDivide marks overflow in the remainder")
    probe.call("WideDivide", [target, 1, -1])
    p.expect(0x80000000, "WideDivide honors the overflow sentinel")
    probe.call("WideWideDivide", [target, 3, remainder])
    p.pointer(base, "WideWideDivide returns its target")
    wide((0x40000000 * 16) // 3, "WideWideDivide keeps a 64-bit quotient")
    word(16, 1, "WideWideDivide stores the remainder")
    probe.call("WideMultiply", [65536, 65536, target])
    probe.call("WideSquareRoot", [target])
    p.expect(65536, "WideSquareRoot uses the high word")


def classic_utility_accessors(probe):
    p = probe.p
    _, state = probe.raw(bytes(8))
    probe.call("TopMem")
    p.emit(0x80800108)
    p.equal("TopMem returns the guest Memory Manager's upper address")
    probe.call("GZSaveHnd")
    p.emit(0x80800328)
    p.equal("GZSaveHnd exposes the currently protected grow-zone handle")

    probe.call("LMGetSysFontSize")
    p.emit(0x907E0000 | state)
    for configured, expected in ((0, 12), (17, 17)):
        probe.call("LMSetSysFontSize", [configured])
        probe.call("GetDefFontSize")
        p.expect(expected, "the default font size follows the system setting and treats zero as 12")
    probe.call("LMSetSysFontSize", [probe.slot(state)])

    probe.call("SetCurrentA5")
    p.emit(0x907E0000 | (state + 4))
    probe.call("SetA5", [0x12345678])
    p.emit(0x80800904)
    p.equal("SetCurrentA5 installs the current application's 68K A5")
    probe.call("SetCurrentA5")
    p.expect(0x12345678, "the previous A5 is returned when installing the current application A5")
    probe.call("SetA5", [probe.slot(state + 4)])
    p.emit(0x80800904)
    p.equal("SetA5 restores the earlier register value and returns the previous A5")


def font_script_queries(probe):
    p = probe.p
    _, state = probe.raw(bytes(16))
    for offset, selector in enumerate((8, 12, 14)):
        probe.call("GetScriptManagerVariable", [selector])
        p.emit(0x907E0000 | (state + offset * 4))

    probe.call("FontToScript", [0x4000])
    p.expect(0, "an unavailable font script falls back to the Roman system script")
    probe.call("GetScriptManagerVariable", [14])
    p.expect(1, "the Script Manager records the fallback")
    probe.call("FontToScript", [32])
    p.expect(0, "a Roman font ID resolves directly")
    probe.call("GetScriptManagerVariable", [14])
    p.expect(0, "mapping a live script resets the default result flag")
    probe.call("SetScriptManagerVariable", [8, 1])
    p.expect(0, "enable the font force flag")
    probe.call("FontToScript", [32])
    p.expect(0, "font force uses the live system script")
    probe.call("GetScriptManagerVariable", [12])
    p.expect(1, "font force sets the forced result flag")
    for entry in ("FontScript", "IntlScript"):
        probe.call(entry)
        p.expect(0, "the current Roman port uses the installed system script")
    for offset, selector in enumerate((8, 12, 14)):
        probe.call("SetScriptManagerVariable", [selector, probe.slot(state + offset * 4)])
        p.expect(0, "restore Script Manager fixture state")


def script_utility_operations(probe):
    p = probe.p
    _, state = probe.raw(bytes(64))
    text = probe.raw(b"a b")
    directions = probe.raw(bytes((0, 0, 0, 0, 1, 1, 0, 0)))
    _, callback = probe.raw(bytes(8))
    p.routines.append((callback, [0x7C6418AE, 0x4E800020]))  # lbzx r3,r4,r3; blr.
    output = probe.ptr(state)
    order_upp = probe.slot(state + 48)
    probe.call("NewRoutineDescriptor", [probe.ptr(callback), 0x390, 1])
    p.nonzero("create a PPC style-run direction callback")
    p.emit(0x907E0000 | (state + 48))
    for right, expected in ((0, (2, 3, 5, 4, 6, 7)), (1, (6, 7, 5, 4, 2, 3))):
        probe.call("GetFormatOrder", [output, 2, 7, right, order_upp, directions])
        for index, item in enumerate(expected):
            p.emit(0xA07E0000 | (state + index * 2))
            p.expect(item, "GetFormatOrder reverses runs opposite the line direction")
    probe.call("DisposeRoutineDescriptor", [order_upp])

    probe.call("HiliteText", [text, 3, -4, 17, output])
    for index, value in enumerate((3, 0, 0)):
        p.emit(0x807E0000 | (state + index * 4))
        p.expect(value, "Roman HiliteText clamps the selection and clears unused pairs")

    event = probe.raw(struct.pack(">HIIhhh", 3, 0x2E00, 0, 0, 0, 0x300))
    for letter, matched in ((b"M", 1), (b"m", 1), (b"x", 0)):
        probe.call("IsCmdChar", [event, letter[0]])
        p.expect(matched, "IsCmdChar tests the physical key with command and shift variants")

    scale = 0x10001
    probe.call("MeasureJustified", [text, 3, 0, output, 0, scale, scale])
    probe.call("MeasureJustified", [text, 3, 4 << 16, probe.ptr(state + 16), 0, scale, scale])
    for index, extra in enumerate((0, 0, 4, 4)):
        p.emit(0xA07E0000 | (state + 16 + index * 2), 0xA09E0000 | (state + index * 2), 0x7C641850)
        p.expect(extra, "MeasureJustified adds the slop to caret positions after the space")

    for selector, new in ((8, 0x7E), (12, 321), (84, 0x00150010)):
        probe.call("GetScriptVariable", [0, selector])
        p.emit(0x907E0000 | (state + 52))
        probe.call("SetScriptVariable", [-1, selector, new])
        p.expect(0, "set a live Roman script-record field")
        probe.call("GetScriptVariable", [0, selector])
        p.expect(new, "the new script value is visible through the shared selectors")
        probe.call("SetScriptVariable", [0, selector, probe.slot(state + 52)])
        p.expect(0, "restore the original script value")
    probe.call("SetScriptVariable", [65, 12, 17])
    p.expect(-2, "unavailable script record returns smScriptNotFound")
    probe.call("SetScriptVariable", [0, 3, 17])
    p.expect(-1, "an unknown script selector cannot modify its neighbour")


def geometry_operations(probe):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    _, base = probe.raw(bytes(64))
    first, second, output = ptr(base), ptr(base + 8), ptr(base + 16)

    def expect_word(offset, expected, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(expected, label)

    def call_result(name, arguments, expected):
        probe.call(name, arguments)
        p.expect(expected, name + " returns the expected geometry result")

    call_result("DMGetGDeviceByDisplayID", [0, first, 0], -6229)
    expect_word(0, 0, "unknown display ID clears the output handle")
    call_result("DMGetGDeviceByDisplayID", [0, first, 1], 0)
    p.emit(0x807E0000 | base)
    p.nonzero("display lookup can substitute the main device")
    call_result("DMGetDisplayIDByGDevice", [slot(base), second, 0], 0)
    expect_word(8, 0x100, "the main QuickDraw device has the first display ID")
    call_result("DMGetGDeviceByDisplayID", [slot(base + 8), output, 0], 0)
    p.emit(0x807E0000 | (base + 16), 0x809E0000 | base)
    p.equal("display ID round trip preserves the same live device handle")

    _, notification = probe.raw(bytes(12))
    p.routines.append((notification, [0x90830000, 0x90A30004, 0x4E800020]))
    _, state = probe.raw(bytes(20))
    result, upp = ptr(state), slot(state + 8)
    current_process = probe.raw(struct.pack(">II", 0, 2))
    probe.call("NewDMExtendedNotificationUPP", [ptr(notification)])
    p.nonzero("create a Display Manager notification UPP")
    p.emit(0x907E0000 | (state + 8))
    call_result("DMRegisterExtendedNotifyProc", [upp, result, 0, current_process], 0)
    p.emit(0x807E0000 | state)
    p.expect(1, "extended display callback receives its installation message")
    p.emit(0x807E0000 | (state + 4))
    p.expect(0, "the installation message carries no event payload")
    call_result("GetCurrentProcess", [ptr(state + 12)], 0)
    call_result("DMRemoveExtendedNotifyProc", [upp, result, ptr(state + 12), 0], 0)
    p.emit(0x807E0000 | state)
    p.expect(3, "extended display callback receives its removal message with a normalized process ID")
    call_result("DMRemoveExtendedNotifyProc", [upp, result, current_process, 0], -50)
    probe.call("DisposeDMExtendedNotificationUPP", [upp])

    probe.call("SetPt", [first, 0xFFFE, 7])
    expect_word(0, 0x0007FFFE, "SetPt writes v followed by h")
    probe.call("AddPt", [0x0004FFFC, first])
    expect_word(0, 0x000BFFFA, "AddPt consumes a by-value packed Point")
    probe.call("SubPt", [0x0004FFFC, first])
    expect_word(0, 0x0007FFFE, "SubPt reverses the translation")
    call_result("EqualPt", [slot(base), 0x0007FFFE], 1)
    call_result("EqualPt", [slot(base), 0xFFFE0007], 0)
    call_result("DeltaPoint", [0x0007FFFE, 0x00080002], 0xFFFFFFFC)
    probe.call("SetPt", [second, 2, 8])
    call_result("deltapoint", [first, second], 0xFFFFFFFC)

    probe.call("SetRect", [first, -8, -5, 10, 12])
    expect_word(0, 0xFFFBFFF8, "SetRect uses horizontal first arguments")
    expect_word(4, 0x000C000A, "SetRect writes far edges")
    probe.call("InsetRect", [first, 2, 3])
    probe.call("OffsetRect", [first, -2, -3])
    expect_word(0, 0xFFFBFFF8, "offset and inset can leave near edges unchanged")
    expect_word(4, 0x00060006, "offset and inset update far edges")
    call_result("PtInRect", [0xFFFBFFF8, first], 1)
    call_result("PtInRect", [0x00000006, first], 0)
    call_result("PtInRect", [0x00060000, first], 0)
    call_result("EmptyRect", [first], 0)
    call_result("EmptyRect", [0], 1)
    call_result("QDError", [], -50)
    probe.call("Pt2Rect", [0x00140014, 0x00010000, second])
    expect_word(8, 0x00010000, "Pt2Rect normalizes the near edges")
    expect_word(12, 0x00140014, "Pt2Rect normalizes the far edges")
    call_result("QDError", [], 0)
    call_result("SectRect", [first, second, 0], 1)
    call_result("SectRect", [first, second, output], 1)
    expect_word(16, 0x00010000, "SectRect returns the overlapping near edges")
    expect_word(20, 0x00060006, "SectRect returns the overlapping far edges")
    call_result("EqualRect", [first, output], 0)
    call_result("SectRect", [output, second, output], 1)
    expect_word(16, 0x00010000, "SectRect allows its output to alias an input")
    probe.call("SetRect", [second, 20, 30, 20, 40])
    call_result("EmptyRect", [second], 1)
    probe.call("UnionRect", [first, second, output])
    expect_word(16, 0xFFFBFFF8, "UnionRect takes the minimum near edges")
    expect_word(20, 0x00280014, "NQD UnionRect includes the coordinates of empty inputs")
    call_result("SectRect", [first, second, output], 0)
    expect_word(16, 0, "a disjoint intersection is zeroed")
    expect_word(20, 0, "a disjoint intersection zeroes both corners")
    call_result("EqualRect", [output, output], 1)

    probe.call("SetRect", [first, 10, 20, 14, 26])
    probe.call("SetRect", [second, 100, 200, 102, 203])
    probe.call("SetPt", [output, 9, 23])
    probe.call("MapPt", [output, first, second])
    expect_word(16, 0x00CA0063, "MapPt rounds each distance before translating")
    probe.call("SetRect", [output, 8, 20, 13, 25])
    probe.call("MapRect", [output, first, second])
    expect_word(16, 0x00C80063, "MapRect maps the near corner")
    expect_word(20, 0x00CB0066, "MapRect maps the far corner")
    probe.call("SetPt", [output, 1, 5])
    probe.call("ScalePt", [output, first, second])
    expect_word(16, 0x00030001, "ScalePt scales dimensions without adding the destination origin")
    clamp_source = probe.raw(struct.pack(">4h", 0, 0, 1, 1))
    clamp_dest = probe.raw(struct.pack(">4h", -100, -100, 1000, 1000))
    probe.call("SetPt", [output, -999, 999])
    probe.call("MapPt", [output, clamp_source, clamp_dest])
    expect_word(16, 0x7FFF8000, "MapPt clips the translated coordinates to signed words")

    probe.call("GetQDGlobalsRandomSeed")
    p.emit(0x907E0000 | (base + 24))
    probe.call("SetQDGlobalsRandomSeed", [1])
    call_result("Random", [], 16807)
    call_result("Random", [], 0x3AF1)
    call_result("GetQDGlobalsRandomSeed", [], 282475249)
    probe.call("SetQDGlobalsRandomSeed", [slot(base + 24)])

    probe.call("QDGetPatternOrigin", [ptr(base + 24)])
    probe.call("QDSetPatternOrigin", [0xFFF7000C])
    probe.call("QDGetPatternOrigin", [output])
    expect_word(16, 0xFFF7000C, "pattern origin accessors share active QuickDraw storage")
    probe.call("QDSetPatternOrigin", [slot(base + 24)])

    # A monochrome port exposes its BitMap bounds directly. A color port
    # stores a master pointer to a PixMap with a separate origin.
    mono = probe.raw(struct.pack(">HIh4h", 0, 0, 4, -7, 11, 33, 41))
    color = probe.raw(struct.pack(">HIH", 0, 0, 0xC000) + bytes(8))
    pixels = probe.raw(struct.pack(">IH4h", 0, 0x8004, 12, -4, 44, 40) + bytes(36))
    p.emit(
        0x387E0000 | pixels[1],
        0x907E0000 | (base + 28),
        0x387E0000 | (base + 28),
        0x907E0000 | (color[1] + 2),
    )
    for port, expected in ((mono, 0xFFFB000E), (color, 0x000EFFFF)):
        probe.call("SetPt", [output, 3, 2])
        probe.call("QDGlobalToLocalPoint", [port, output])
        p.pointer(base + 16, "point transform returns the caller buffer")
        expect_word(16, expected, "point transform uses the port's backing origin")
        probe.call("QDLocalToGlobalPoint", [port, output])
        expect_word(16, 0x00020003, "point transform round trip")
        probe.call("SetRect", [output, 3, 2, 9, 8])
        probe.call("QDGlobalToLocalRect", [port, output])
        p.pointer(base + 16, "rectangle transform returns the caller buffer")
        expect_word(16, expected, "rectangle and point transforms use the same origin")
        probe.call("QDLocalToGlobalRect", [port, output])
        expect_word(20, 0x00080009, "rectangle transform round trip")

    probe.call("NewRgn")
    p.nonzero("create a region for coordinate conversion")
    p.emit(0x907E0000 | (base + 32))
    region = slot(base + 32)
    probe.call("SetRectRgn", [region, 3, 2, 9, 8])
    call_result("IsRegionRectangular", [region], 1)
    probe.call("QDGlobalToLocalRegion", [mono, region])
    p.emit(0x809E0000 | (base + 32))
    p.equal("region transform returns the caller handle")
    probe.call("GetRegionBounds", [region, output])
    expect_word(16, 0xFFFB000E, "region transform translates the bounding rectangle")
    probe.call("QDLocalToGlobalRegion", [mono, region])
    probe.call("GetRegionBounds", [region, output])
    expect_word(16, 0x00020003, "region transform round trip")
    probe.call("DisposeRgn", [region])


def application_operations(probe):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    _, base = probe.raw(bytes(384))
    word, output = ptr(base + 20), ptr(base + 128)

    def expect_word(offset, expected, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(expected, label)

    def call(name, arguments, expected):
        probe.call(name, arguments)
        p.expect(expected, name + " returns the expected manager result")

    _, notification = probe.raw(bytes(16))
    p.routines.append((notification, [0x90650000, 0x90850004, 0x38600000, 0x4E800020]))
    _, notification_state = probe.raw(bytes(12))
    state, upp_slot = ptr(notification_state), slot(notification_state + 8)
    probe.call("NewFolderManagerNotificationUPP", [ptr(notification)])
    p.nonzero("create a Folder Manager notification UPP")
    p.emit(0x907E0000 | (notification_state + 8))
    call("FolderManagerRegisterNotificationProc", [upp_slot, state, 0], 0)
    call("FolderManagerRegisterCallNotificationProcs", [0x54455354, 0x1234, 0], 0)
    p.emit(0x807E0000 | notification_state)
    p.expect(0x54455354, "Folder Manager passes the notification message to the callback")
    p.emit(0x807E0000 | (notification_state + 4))
    p.expect(0x1234, "Folder Manager passes the payload and the registered refCon")
    call("FolderManagerUnregisterNotificationProc", [upp_slot, state], 0)
    call("FolderManagerRegisterCallNotificationProcs", [0x54455354, 0x5678, 0], 0)
    p.emit(0x807E0000 | (notification_state + 4))
    p.expect(0x1234, "unregistered callbacks receive no later notifications")
    probe.call("DisposeFolderManagerNotificationUPP", [upp_slot])

    call("GetSharedLibrary", [probe.raw(b"\x09CarbonLib"), 0x70777063, 3, word, 0, 0], 0)
    call("CloseConnection", [word], 0)
    call("GetSharedLibrary", [probe.raw(b"\x0cInterfaceLib"), 0x70777063, 1, word, 0, 0], 0)
    selector = 0x54534756  # 'TSGV'
    call("FindSymbol", [slot(base + 20), probe.raw(b"\x0fNewGestaltValue"), ptr(base + 24), ptr(base + 28)], 0)
    probe.arguments([selector, 0x87654321], 0)
    p.emit(0x819E0000 | (base + 24), 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)
    p.expect(0, "InterfaceLib NewGestaltValue registers a shared value")
    call("CloseConnection", [word], 0)
    call("Gestalt", [selector, output], 0)
    expect_word(128, 0x87654321, "Carbon Gestalt reads the InterfaceLib value")
    call("NewGestaltValue", [selector, 0], -5552)
    call("SetGestaltValue", [selector, 0x80000000], 0)
    call("Gestalt", [selector, output], 0)
    expect_word(128, 0x80000000, "SetGestaltValue replaces all 32 bits")
    call("DeleteGestaltValue", [selector], 0)
    call("ReplaceGestaltValue", [selector, 0], -5551)
    call("SetGestaltValue", [selector, 17], 0)
    call("ReplaceGestaltValue", [selector, 31], 0)
    call("Gestalt", [selector, output], 0)
    expect_word(128, 31, "SetGestaltValue inserts a missing selector")
    call("DeleteGestaltValue", [selector], 0)
    call("Gestalt", [selector, output], -5551)

    _, handlers = probe.raw(bytes(32))
    p.routines.append((handlers, [0x38001020, 0x90040000, 0x38600000, 0x4E800020]))
    p.routines.append((handlers + 16, [0x38003040, 0x90040000, 0x38600000, 0x4E800020]))
    _, gestures = probe.raw(bytes(20))
    # The original CarbonLib refuses a Carbon app with a direct InterfaceLib import.
    call("GetSharedLibrary", [probe.raw(b"\x0cInterfaceLib"), 0x70777063, 1, word, 0, 0], 0)

    def call_zone_api(name, arguments=()):
        encoded = name.encode("ascii")
        call("FindSymbol", [slot(base + 20), probe.raw(bytes([len(encoded)]) + encoded), ptr(base + 24), 0], 0)
        probe.arguments(arguments, 0)
        p.emit(0x819E0000 | (base + 24), 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    call_zone_api("GetZone")
    p.emit(0x907E0000 | gestures)
    call_zone_api("SystemZone")
    p.emit(0x907E0000 | (gestures + 4))
    call_zone_api("SetZone", [slot(gestures + 4)])
    for offset, routine in ((8, handlers), (12, handlers + 16)):
        probe.call("NewRoutineDescriptor", [ptr(routine), 0x3E0, 1])
        p.nonzero("allocate a System-zone Gestalt callback descriptor")
        p.emit(0x907E0000 | (gestures + offset))
    call_zone_api("SetZone", [slot(gestures)])
    call("CloseConnection", [word], 0)
    selector = 0x54534750  # 'TSGP'
    call("NewGestalt", [selector, slot(gestures + 8)], 0)
    call("Gestalt", [selector, output], 0)
    expect_word(128, 0x1020, "Gestalt dispatches to the registered PPC procedure")
    call("ReplaceGestalt", [selector, slot(gestures + 12), ptr(gestures + 16)], 0)
    p.emit(0x807E0000 | (gestures + 8), 0x809E0000 | (gestures + 16))
    p.equal("ReplaceGestalt returns the previous guest procedure pointer")
    probe.call("DisposeRoutineDescriptor", [slot(gestures + 8)])
    call("Gestalt", [selector, output], 0)
    expect_word(128, 0x3040, "Gestalt dispatches to the replacement after the old procedure is released")
    probe.call("DisposeRoutineDescriptor", [slot(gestures + 12)])
    call("Gestalt", [selector, output], -5551)

    selectors = (
        (b"sysa", 0, 2),
        (b"cput", 0, 0x108),
        (b"fold", 0, 0x11),
    )
    for name, status, value in selectors:
        for side in (0, 1):
            p.emit(0x3C60A55A, 0x60631234, 0x907E0000 | (base + 128))
            probe.call("Gestalt", [int.from_bytes(name, "big"), output], side=side)
            p.expect(status, "Gestalt " + name.decode() + " status")
            expect_word(128, value, "Gestalt " + name.decode() + " response")

    menu_selector = int.from_bytes(b"menu", "big")
    probe.call("NewGestaltValue", [menu_selector, 0x40000000])
    p.emit(0x907E0000 | (base + 140), 0x2C030000)
    created = len(p.code)
    p.emit(0)
    p.expect(-5552, "the real Menu Manager may already own its Gestalt selector")
    p.code[created] = 0x41820000 | (len(p.code) - created) * 4
    probe.call("Gestalt", [menu_selector, output], side=1)
    p.expect(0, "query the saved menu attributes")
    p.emit(0x807E0000 | (base + 128), 0x907E0000 | (base + 136))
    probe.call("Gestalt", [menu_selector, output])
    p.expect(0, "query the live menu attributes")
    p.emit(0x807E0000 | (base + 128), 0x809E0000 | (base + 136))
    p.equal("menu attributes match the saved Carbon wrapper")
    p.emit(0x809E0000 | (base + 140), 0x2C040000)
    keep_existing = len(p.code)
    p.emit(0)
    expect_word(128, 0x4000000C, "Carbon augments the registered menu capabilities")
    call("DeleteGestaltValue", [menu_selector], 0)
    p.code[keep_existing] = 0x40820000 | (len(p.code) - keep_existing) * 4
    for name in (b"uisf", b"uisv"):
        for side in (1, 0):
            p.emit(0x3C60A55A, 0x60631234, 0x907E0000 | (base + 128))
            probe.call("Gestalt", [int.from_bytes(name, "big"), output], side=side)
            if side:
                p.emit(0x907E0000 | (base + 132), 0x807E0000 | (base + 128), 0x907E0000 | (base + 136))
            else:
                p.emit(0x809E0000 | (base + 132))
                p.equal("ATSUI selector matches the saved status")
                p.emit(0x807E0000 | (base + 128), 0x809E0000 | (base + 136))
                p.equal("ATSUI selector matches the saved output")
    # Supply a controlled File Manager response through the same delegate used
    # by the saved Carbon wrapper and restore it before the next manager test.
    fixture = probe.raw(bytes(20))
    _, delegate = fixture
    p.routines.append(
        (
            delegate,
            [
                0x7060FFFF,  # Compare the selector suffix against 'ld'.
                0x28006C64,
                0x40820014,
                0x38E00001,
                0x90E40000,
                0x38600000,
                0x4E800020,
                0x80620000 | (delegate + 16),
                0x2C030000,
                0x4082000C,
                0x38E01000,
                0x90E40000,
                0x4E800020,
            ],
        )
    )
    probe.load_oracle_vector(probe.names.index("Gestalt"), 12)
    p.emit(0x818C0004, 0x818CFBA8, 0x806C0000)
    p.emit(0x919E0000 | (delegate + 8), 0x907E0000 | (delegate + 12))
    probe.arguments([fixture], 0)
    p.emit(0x819E0000 | (delegate + 8), 0x906C0000)
    for mode, value in ((0, 0x31), (-5551, 0x11)):
        p.emit(0x38600000 | (mode & 0xFFFF), 0x907E0000 | (delegate + 16))
        for side in (0, 1):
            probe.call("Gestalt", [int.from_bytes(b"fold", "big"), output], side=side)
            p.expect(mode, "Carbon folder selector returns the File Manager query status")
            expect_word(128, value, "Carbon folder flags reflect the File Manager response")
    p.emit(0x819E0000 | (delegate + 8), 0x807E0000 | (delegate + 12), 0x906C0000)
    call("GetSharedLibrary", [probe.raw(b"\x13PrivateInterfaceLib"), 0x70777063, 1, word, 0, 0], 0)
    call("FindSymbol", [slot(base + 20), probe.raw(b"\x0cGetQDGlobals"), ptr(base + 24), ptr(base + 28)], 0)
    p.emit(0x819E0000 | (base + 24), 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)
    p.nonzero("GetQDGlobals returns the active QuickDraw record")
    p.emit(0x907E0000 | (base + 32))
    probe.call("GetQDGlobalsThePort")
    p.emit(0x809E0000 | (base + 32), 0x808400CA)
    p.equal("the private QuickDraw record exposes the live port")
    probe.call("GetQDGlobalsRandomSeed")
    p.emit(0x809E0000 | (base + 32), 0x8084004C)
    p.equal("public and private QuickDraw access see the same random seed")
    call("FindSymbol", [slot(base + 20), probe.raw(b"\x11FragFindOwnerOfPC"), ptr(base + 24), ptr(base + 28)], 0)
    owner = base + 216

    def find_owner(pc, destination, expected):
        probe.arguments([pc, destination], 0)
        p.emit(0x819E0000 | (base + 24), 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)
        p.expect(expected, "FragFindOwnerOfPC returns the expected status")

    find_owner(ptr(base + 128), ptr(owner), 0)
    for offset in (0, 4, 8, 16, 20):
        p.emit(0x807E0000 | (owner + offset))
        p.nonzero("the owner record identifies a mapped fragment and section")
    probe.arguments([ptr(base + 128)], 0)
    p.emit(0x809E0000 | (owner + 16), 0x7C641850, 0x809E0000 | (owner + 20), 0x7C032040, 0x41800008, 0)
    find_owner(0, ptr(owner), -43)
    p.emit(0x807E0000 | owner)
    p.nonzero("an unmapped address preserves the previous owner record")
    find_owner(ptr(base + 128), 0, -50)
    call("CloseConnection", [word], 0)
    clips = [
        probe.raw(struct.pack(">4h", *rect))
        for rect in (
            (-4, -5, 40, 50),
            (0, 0, 30, 30),
            (1, 2, 24, 25),
            (2, 3, 22, 22),
            (2, 3, 20, 21),
            (3, 3, 18, 16),
            (4, 5, 10, 13),
        )
    ]
    probe.arguments([clips[0], len(clips), *clips], 0)
    p.call("RSect", library="NQD")
    p.expect(1, "NQD intersects seven rectangles, with the final pointer on the caller stack")
    p.emit(0x807E0000 | clips[0][1])
    p.expect(0x00040005, "the intersection can replace one of its inputs")
    p.emit(0x807E0000 | (clips[0][1] + 4))
    p.expect(0x000A000D, "the stack argument constrains the intersection")
    probe.arguments([clips[0], 0], 0)
    p.call("RSect", library="NQD")
    p.expect(0, "an empty input list has an empty intersection")
    p.emit(0x807E0000 | clips[0][1], 0x809E0000 | (clips[0][1] + 4), 0x7C632378)
    p.expect(0, "an empty intersection clears the whole rectangle")

    probe.call("GetMenuBar")
    p.nonzero("save the initial menu bar")
    p.emit(0x907E0000 | base)
    probe.call("ClearMenuBar")
    probe.call("NewMenu", [8300, probe.raw(b"\x04Test")])
    p.nonzero("NewMenu creates a real menu record")
    p.emit(0x907E0000 | (base + 4))
    menu = slot(base + 4)
    call("GetMenuID", [menu], 8300)
    probe.call("AppendMenu", [menu, probe.raw(b"\x07One;Two")])
    probe.call("InsertMenuItem", [menu, probe.raw(b"\x06Middle"), 1])
    call("CountMenuItems", [menu], 3)
    probe.call("GetMenuItemText", [menu, 2, output])
    expect_word(128, 0x064D6964, "InsertMenuItem inserts after the requested item")
    probe.call("SetMenuItemText", [menu, 1, probe.raw(bytes([130]) + b"a" * 130)])
    probe.call("GetMenuItemText", [menu, 1, output])
    expect_word(128, 0x82616161, "growing the first item preserves its unsigned length")
    probe.call("GetMenuItemText", [menu, 3, output])
    expect_word(128, 0x0354776F, "resizing the first item preserves following items")
    probe.call("SetItemCmd", [menu, 2, ord("X")])
    probe.call("GetItemCmd", [menu, 2, word])
    p.emit(0xA07E0000 | (base + 20))
    p.expect(ord("X"), "GetItemCmd writes the live key equivalent")
    probe.call("DeleteMenuItem", [menu, 1])
    call("CountMenuItems", [menu], 2)
    probe.call("InsertMenu", [menu, 0])
    probe.call("GetMenuHandle", [8300])
    p.emit(0x809E0000 | (base + 4))
    p.equal("InsertMenu registers the shared menu handle")
    call("SetMenuTitle", [menu, probe.raw(b"\x08Renamed!")], 0)
    probe.call("GetMenuTitle", [menu, output])
    expect_word(128, 0x0852656E, "SetMenuTitle resizes the live menu title")
    probe.call("DrawMenuBar")
    probe.call("DeleteMenu", [8300])
    call("GetMenuHandle", [8300], 0)
    probe.call("DisposeMenu", [menu])
    probe.call("SetMenuBar", [slot(base)])
    probe.call("DisposeHandle", [slot(base)])

    bounds = probe.raw(struct.pack(">4h", 65, 40, 115, 140))
    title = probe.raw(b"\x04Test")
    probe.call("NewWindow", [0, bounds, title, 0, 0, -1, 1, 0x12345678])
    p.nonzero("NewWindow creates a live Carbon window")
    p.emit(0x907E0000 | (base + 4))
    window = slot(base + 4)
    call("IsValidWindowPtr", [window], 1)
    call("IsWindowVisible", [window], 0)
    probe.call("SetWindowKind", [window, 0x123])
    call("GetWindowKind", [window], 0x123)
    call("GetWindowTitleLength", [window, word], 0)
    expect_word(20, 4, "GetWindowTitleLength writes a guest longword")
    probe.call("SetWTitle", [window, probe.raw(b"\x09New title")])
    call("GetWindowTitleLength", [window, word], 0)
    expect_word(20, 9, "SetWTitle changes the live window title")
    probe.call("SizeWindow", [window, 125, 90, 0])
    probe.call("GetWindowPortBounds", [window, output])
    expect_word(132, 0x005A007D, "SizeWindow updates the port bounds")
    probe.call("ShowWindow", [window])
    call("IsWindowVisible", [window], 1)
    probe.call("FrontWindow")
    p.emit(0x809E0000 | (base + 4))
    p.equal("FrontWindow visits the visible application window")
    probe.arguments([0, 0xBE, 0x63], 0)
    p.emit(0x8060103C)  # lwz r3, 0x103c(0), the live OSDispatch vector.
    p.call("CallUniversalProc")
    p.emit(0x809E0000 | (base + 4))
    p.equal("OSDispatch 0x0063 returns the visible front window")
    probe.call("DisposeWindow", [window])
    call("IsValidWindowPtr", [window], 0)

    probe.call("NewHandleClear", [17])
    p.nonzero("allocate a relocatable handle")
    p.emit(0x907E0000 | (base + 4))
    probe.call("HLockHi", [slot(base + 4)])
    probe.call("HGetState", [slot(base + 4)])
    p.emit(0x5463063E)
    p.expect(0x80, "HLockHi leaves the handle locked")
    probe.call("HUnlock", [slot(base + 4)])
    probe.call("DisposeHandle", [slot(base + 4)])

    probe.call("CurResFile")
    p.emit(0x907E0000 | (base + 8))
    spec = ptr(base + 32)
    call("FSMakeFSSpec", [slot(0x1FC0), slot(0x1F42), probe.raw(b"\x13Carbon Resource App"), spec], -43)
    probe.call("FSpCreateResFile", [spec, 0x54455354, 0x54455354, -1])
    call("ResError", [], 0)
    probe.call("FSpOpenResFile", [spec, 3])
    p.nonzero("open the private resource file")
    p.emit(0x907E0000 | (base + 12))
    for resource, data in ((0x1551, b"\x00\x02\x03One\x03Two"), (0x1552, b"\x00\x01\x07Bad")):
        probe.call("NewHandle", [len(data)])
        p.nonzero("allocate a string list resource")
        p.emit(0x907E0000 | (base + 16), 0x80630000, 0x907E0000 | (base + 20))
        probe.call("BlockMoveData", [probe.raw(data), slot(base + 20), len(data)])
        probe.call("AddResource", [slot(base + 16), 0x53545223, resource, probe.raw(b"\x00")])
        call("ResError", [], 0)
    probe.call("GetIndString", [output, 0x1551, 2])
    expect_word(128, 0x0354776F, "GetIndString returns the indexed Pascal string")
    probe.call("GetIndString", [output, 0x1552, 1])
    p.emit(0x887E0000 | (base + 128))
    p.expect(0, "GetIndString clears the output for a truncated resource")
    probe.call("CloseResFile", [slot(base + 12)])
    call("ResError", [], 0)
    probe.call("UseResFile", [slot(base + 8)])
    call("FSpDelete", [spec], 0)


def atsui_operations(probe):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    _, base = probe.raw(bytes(140 + 256))
    saved_style, native_style, layout = (slot(base + offset) for offset in (0, 4, 8))
    value, count = ptr(base + 12), ptr(base + 16)

    def call(name, args=(), expected=0, side=0):
        probe.call(name, args, side or int(name in ATSUI_REFERENCE_CALLS))
        p.expect(expected, name + " returns the expected ATSUI status")

    def word(offset, expected, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(expected, label)

    def byte(offset, expected, label):
        p.emit(0x887E0000 | (base + offset))
        p.expect(expected, label)

    _, spec = probe.raw(bytes(70))
    font_path = b":ATSUI Fixtures:ATSUITestFont"
    probe.call("FSMakeFSSpec", [0, 0, probe.raw(bytes([len(font_path)]) + font_path), ptr(spec)])
    p.expect(0, "find the test scalable face")
    call("FMActivateFonts", [ptr(spec), 0, 0, 1])
    # The first call starts ATSUnicodeLib.CL through the saved implementation.
    call("ATSUCreateStyle", [ptr(base)])
    call("ATSUCreateStyle", [ptr(base + 4)])
    atsui_catalogue_operations(probe)
    call("ATSUStyleIsEmpty", [native_style, value])
    byte(12, 1, "a new ATS style has no attributes")
    call("ATSUSetStyleRefCon", [saved_style, 0x12345678], side=1)
    call("ATSUGetStyleRefCon", [saved_style, value])
    word(12, 0x12345678, "native access reads the saved subclass record")
    call("ATSUSetStyleRefCon", [native_style, 0xDEADBEEF])
    call("ATSUGetStyleRefCon", [native_style, value], side=1)
    word(12, 0xDEADBEEF, "saved accessor reads the native subclass record")

    tags = probe.raw(struct.pack(">II", 256, 262))
    sizes = probe.raw(struct.pack(">II", 1, 4))
    wrong_sizes = probe.raw(struct.pack(">II", 1, 2))
    for offset, data in ((96, bytes([1, 0])), (100, struct.pack(">I", 18 << 16))):
        _, local = probe.raw(data)
        p.emit(0x387E0000 | local, 0x907E0000 | (base + offset))
    call("ATSUSetAttributes", [native_style, 2, tags, wrong_sizes, ptr(base + 96)], -8798)
    call("ATSUStyleIsEmpty", [native_style, value])
    byte(12, 1, "invalid attribute size prevents the earlier item from being applied")
    call("ATSUSetAttributes", [native_style, 2, tags, sizes, ptr(base + 96)])
    call("ATSUStyleIsEmpty", [native_style, value])
    byte(12, 0, "the native setters recorded two attributes")
    call("ATSUGetAttribute", [native_style, 262, 4, value, count])
    word(12, 18 << 16, "the saved size accessor sees the native update")
    word(16, 4, "style sizes are guest longwords")

    types = probe.raw(struct.pack(">HH", 1, 3))
    selectors = probe.raw(struct.pack(">HH", 2, 1))
    call("ATSUSetFontFeatures", [native_style, 2, types, selectors])
    call("ATSUGetAllFontFeatures", [native_style, 1, ptr(base + 24), ptr(base + 26), count])
    word(16, 2, "a bounded feature copy still reports the total")
    word(24, 0x00010002, "feature type and selector have the expected endian order")
    call("ATSUGetFontFeature", [native_style, 1, ptr(base + 28), ptr(base + 30)])
    word(28, 0x00030001, "the indexed feature reads the second saved pair")
    call("ATSUGetFontFeature", [native_style, 2, 0, 0], -50)
    axes = probe.raw(struct.pack(">I", 0x77676874))
    variation_values = probe.raw(struct.pack(">I", 2 << 16))
    call("ATSUSetVariations", [native_style, 1, axes, variation_values])
    call("ATSUGetAllFontVariations", [native_style, 1, ptr(base + 24), ptr(base + 28), count])
    word(16, 1, "report the variation axis count")
    word(24, 0x77676874, "the saved weight axis round trips")
    word(28, 2 << 16, "the native reader preserves the fixed-point value")
    call("ATSUGetFontVariationValue", [native_style, 0x77676874, value])
    word(12, 2 << 16, "lookup by variation axis")
    call("ATSUGetFontVariationValue", [native_style, 0x77647468, value], -8801)
    word(12, 0, "a missing axis clears the output")

    atsui_style_archives(probe, native_style)
    atsui_style_edits(probe, native_style, tags, sizes, ptr(base + 96), types, selectors, axes, variation_values)

    call("ATSUSetFontFallbacks", [0, 0, 0])
    call("ATSUGetFontFallbacks", [0, 0, ptr(base + 34), count])
    word(32, 0, "set the documented no-fallback mode")
    word(16, 0, "no explicit fallback fonts")
    call("ATSUSetFontFallbacks", [0, 0, 3])
    call("ATSUGetFontFallbacks", [0, 0, ptr(base + 34), count])
    word(32, 1, "sequential fallback without a list selects the default mode")
    call("ATSUSetFontFallbacks", [0, 0, 4], -50)

    text = probe.raw("ATSUI text".encode("utf-16be"))
    run = probe.raw(struct.pack(">I", 10))
    call("ATSUCreateTextLayoutWithTextPtr", [text, 0, 10, 10, 1, run, ptr(base + 4), ptr(base + 8)])
    atsui_run_analysis(probe, layout)
    call("ATSUSetTextLayoutRefCon", [layout, 0x11223344])
    call("ATSUGetTextLayoutRefCon", [layout, value], side=1)
    word(12, 0x11223344, "the real layout accessor sees the native write")
    call("ATSUGetTextLayoutRefCon", [layout, value])
    word(12, 0x11223344, "the native layout accessor reads the same field")
    call("ATSUGetTextLocation", [layout, value, ptr(base + 36), ptr(base + 40), ptr(base + 44), ptr(base + 48)])
    p.emit(0x807E0000 | (base + 12))
    p.pointer(text[1], "the text address is returned unchanged")
    byte(36, 0, "the original layout stores a pointer")
    word(40, 0, "report the original start offset")
    word(44, 10, "report the selected UTF-16 length")
    word(48, 10, "report the original buffer length")
    call("ATSUSetTransientFontMatching", [layout, 1])
    call("ATSUGetTransientFontMatching", [layout, value])
    byte(12, 1, "transient font matching is enabled")
    call("ATSUSetSoftLineBreak", [layout, 3])
    call("ATSUSetSoftLineBreak", [layout, 7])
    call("ATSUGetSoftLineBreaks", [layout, -1, -1, 1, ptr(base + 52), count])
    word(16, 2, "report the complete soft break count")
    word(52, 3, "copy the first requested soft break")
    call("ATSUGetSoftLineBreaks", [layout, 8, 9, 1, ptr(base + 52), count], -8792)
    atsui_glyph_queries(probe, native_style, layout)
    atsui_layout_operations(probe, native_style, layout, text, run)
    atsui_render_operations(probe, layout)
    call("ATSUDisposeTextLayout", [layout])
    call("ATSUDisposeStyle", [native_style])
    call("ATSUDisposeStyle", [saved_style])
    call("FMDeactivateFonts", [ptr(spec), 0, 0, 1])


def atsui_catalogue_operations(probe):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    _, base = probe.raw(bytes(192))
    font = slot(base)
    probe.call("ATSUGetFontIDs", [ptr(base), 1, ptr(base + 4)], side=1)
    p.expect(0, "enumerate the first installed font through the saved catalogue")
    p.emit(0x807E0000 | base)
    p.nonzero("the catalogue supplies a font ID for differential table queries")
    missing_name = probe.raw(b"MissingATSUIFont")

    cases = (
        ("ATSUFontCount", lambda out: [out(0)]),
        ("ATSUCountFontNames", lambda out: [font, out(0)]),
        ("ATSUGetFontIDs", lambda out: [out(4), 2, out(0)]),
        ("ATSUFontIDtoFOND", lambda out: [font, out(0), out(4)]),
        ("ATSUGetIndFontName", lambda out: [font, 0, 4, out(0), out(4), out(8), out(12), out(16), 0]),
        ("ATSUFindFontName", lambda out: [font, 1, 1, 0, 0, 4, out(0), out(4), out(8)]),
        ("ATSUFindFontFromName", lambda out: [missing_name, 16, 1, 1, 0, 0, out(0)]),
        ("ATSUCountFontFeatureTypes", lambda out: [font, out(0)]),
        ("ATSUGetFontFeatureTypes", lambda out: [font, 2, out(4), out(0)]),
        ("ATSUCountFontFeatureSelectors", lambda out: [font, 1, out(0)]),
        ("ATSUGetFontFeatureSelectors", lambda out: [font, 1, 2, out(4), out(8), out(0), out(12)]),
        ("ATSUGetFontFeatureNameCode", lambda out: [font, 1, -1, out(0)]),
        ("ATSUCountFontTracking", lambda out: [font, 0, out(0)]),
        ("ATSUGetIndFontTracking", lambda out: [font, 0, 0, out(0), out(4)]),
        ("ATSUCountFontVariations", lambda out: [font, out(0)]),
        ("ATSUGetIndFontVariation", lambda out: [font, 0, out(0), out(4), out(8), out(12)]),
        ("ATSUGetFontVariationNameCode", lambda out: [font, 0x77676874, out(0)]),
        ("ATSUCountFontInstances", lambda out: [font, out(0)]),
        ("ATSUGetFontInstance", lambda out: [font, 0, 2, out(4), out(12), out(0)]),
        ("ATSUGetFontInstanceNameCode", lambda out: [font, 0, out(0)]),
    )
    for name, arguments in cases:
        for side in (0, 1):
            output = base + 16 + side * 32
            p.emit(0x38005A5A)
            for offset in range(0, 20, 4):
                p.emit(0x901E0000 | (output + offset))
            probe.call(name, arguments(lambda offset: ptr(output + offset)), side)
            p.emit(0x907E0000 | (output + 20))
        for offset in range(0, 24, 4):
            p.emit(0x807E0000 | (base + 16 + offset), 0x809E0000 | (base + 48 + offset))
            p.equal(name + " agrees with the saved directory and table query")
    for side in (0, 1):
        probe.call("ATSUCountFontNames", [-1, ptr(base + 16)], side)
        p.expect(-8796, "reject an unknown font ID")
        probe.call("ATSUCountFontTracking", [font, 2, ptr(base + 16)], side)
        p.expect(-50, "reject a tracking direction outside the public enumeration")
        probe.call("ATSUGetFontIDs", [0, 0, 0], side)
        p.expect(-50, "reject font enumeration without a buffer or count output")
        probe.call("ATSUGetIndFontName", [font, 0, 0, ptr(base + 16), 0, 0, 0, 0, 0], side)
        p.expect(-50, "reject an indexed name buffer with zero capacity")


def atsui_glyph_queries(probe, style, layout):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    _, base = probe.raw(bytes(64))
    _, vectors = probe.raw(bytes([0xA5]) * 168)
    _, baselines = probe.raw(bytes([0xA5]) * 256)
    _, metrics = probe.raw(bytes([0xA5]) * 224)
    _, conversions = probe.raw(bytes(224))
    _, paths = probe.raw((struct.pack(">4I", 0, 256, 0, 0) + bytes([0xA5]) * 256) * 2)
    characters = probe.raw("AB".encode("utf-16be"))
    glyph = probe.raw(struct.pack(">H", 1))
    p.emit(0x387E0000 | (base + 0x24), 0x907E0000 | (base + 0x1C))
    p.emit(0x38000002, 0xB01E0000 | (base + 0x20))
    p.emit(0x38600001, 0x907E0000 | (base + 0x24))
    probe.arguments([style], 0)
    p.emit(0x907E0000 | (base + 0x2C))
    for side in (0, 1):
        group = base + 4 * side
        probe.call("ATSUGetStyleGroup", [style, ptr(group)], side)
        p.expect(0, "duplicate the style attribute group")
        p.emit(0x807E0000 | group)
        p.nonzero("the caller owns a returned style group")
        conversion = conversions + side * 112
        records, table = conversion + 0x34, conversion + 0x24
        probe.arguments([ptr(records), ptr(table), slot(group)], 0)
        p.emit(
            0x907E0000 | (conversion + 0xC),
            0x909E0000 | (conversion + 0x1C),
            0x90BE0000 | table,
            0x38000001,
            0xB01E0000 | (conversion + 0x20),
            0x38000002,
            0x901E0000 | (conversion + 4),
            0x3800001C,
            0x901E0000 | (conversion + 8),
        )
        probe.call("ATSUConvertCharToGlyphs", [slot(group), characters, 5, 2, 2, ptr(conversion)], side)
        p.expect(-8804, "reject a glyph insertion at capacity")
        probe.call("ATSUConvertCharToGlyphs", [slot(group), characters, 5, 2, 0, ptr(conversion)], side)
        p.expect(0, "convert two characters into the prepared glyph vector")
        p.emit(0x807E0000 | conversion)
        p.expect(2, "record both converted glyphs")
        probe.call("ATSUDisposeStyleGroup", [slot(group)], 1 - side)
        p.expect(0, "release a style group allocated by the other implementation")
        probe.call("ATSUGetStyleIndex", [ptr(base), style, ptr(group)], side)
        p.expect(0, "find an existing glyph style index")
        p.emit(0x807E0000 | group)
        p.expect(1, "return the existing zero-based index")
        probe.call("ATSUCalculateBaselineDeltas", [style, 0, ptr(baselines + side * 128)], side)
        p.emit(0x907E0000 | (base + 0x34 + side * 4))
        probe.call("ATSUGetNativeCurveType", [style, ptr(base + 8 + 4 * side)], side)
        p.emit(0x907E0000 | (base + 0x10 + side * 4))
        probe.call("ATSULoadGlyphVector", [layout, 0xFFFF, 0, 0, 0, ptr(vectors + side * 84)], side)
        p.expect(-8792, "reject a line outside the layout")
        probe.call("ATSULoadGlyphVector", [layout, 0, 0, 0, 0, ptr(vectors + side * 84)], side)
        p.expect(0, "load an empty glyph vector")
        probe.call("ATSUGetGlyphInfo", [layout, 0, 0, ptr(group), ptr(base + 0x18)], side)
        p.expect(0, "look up information for an empty glyph range")
        probe.arguments([layout, slot(base + 0x18)], 0)
        p.equal("return the source layout in the glyph information record")
        out = metrics + 112 * side
        probe.call("ATSUGlyphGetIdealMetrics", [style, 1, glyph, 0, ptr(out)], side)
        p.emit(0x907E0000 | (out + 24))
        probe.call("ATSUGlyphGetScreenMetrics", [style, 1, glyph, 0, 1, 0, ptr(out + 28)], side)
        p.emit(0x907E0000 | (out + 68))
        probe.call("ATSUGetGlyphBounds", [layout, 0, 0, 0, 1, 0, 1, ptr(out + 72), ptr(out + 104)], side)
        p.emit(0x907E0000 | (out + 108))
        probe.call("ATSUGetGlyphBounds", [layout, 0, 0, 11, 1, 3, 0, 0, 0], side)
        p.expect(-50, "reject a glyph bounds mode outside the public enumeration")
        path = paths + side * 272
        probe.call("ATSUGlyphGetCurvePaths", [style, 36, ptr(path), 0], side)
        p.emit(0x907E0000 | (path + 8))
        probe.call("ATSUGlyphGetCurvePaths", [style, 36, ptr(path + 4), ptr(path + 16)], side)
        p.emit(0x907E0000 | (path + 12))
        probe.call("ATSUGlyphGetCurvePaths", [style, 0xFFFF, ptr(group), 0], side)
        p.expect(0, "ignore the missing-glyph sentinel")
    for a, b, label in (
        (base + 0x34, base + 0x38, "baseline query status"),
        (base + 0x10, base + 0x14, "curve query status"),
        (base + 8, base + 12, "curve query output"),
    ):
        p.emit(0x807E0000 | a, 0x809E0000 | b)
        p.equal(label + " agrees with the saved API")
    for i in range(0, 128, 4):
        p.emit(0x807E0000 | (baselines + i), 0x809E0000 | (baselines + 128 + i))
        p.equal("each baseline matches the saved fixed-point result")
    for i in range(0, 168, 4):
        p.emit(0x807E0000 | (vectors + i))
        p.expect(0, "each empty glyph vector is fully initialized")
    for i in range(0, 112, 4):
        p.emit(0x807E0000 | (metrics + i), 0x809E0000 | (metrics + 112 + i))
        p.equal("the saved glyph query produces the same status and output words")
    for i in range(0x34, 112, 4):
        p.emit(0x807E0000 | (conversions + i), 0x809E0000 | (conversions + 112 + i))
        p.equal("the glyph ID, text offset, and flags match the saved conversion")
    for i in range(0, 272, 4):
        p.emit(0x807E0000 | (paths + i), 0x809E0000 | (paths + 272 + i))
        p.equal("curve size, status, and contour data match the saved API")
    atsui_path_callbacks(probe, style)


def atsui_path_callbacks(probe, style):
    p = probe.p
    ptr = probe.ptr
    _, outputs = probe.raw(bytes(128))

    def callback(index, context_register, with_point):
        vector = probe.raw(bytes(8))
        base = context_register << 16
        counter = index * 4
        code = [0x81000000 | base | counter, 0x39080001, 0x91000000 | base | counter]
        if with_point:
            code += [0x81230000, 0x81000000 | base | 16, 0x7D084A78, 0x91000000 | base | 16]
        code += [0x38600000, 0x4E800020]
        p.routines.append((vector[1], code))
        return vector

    for mode, name in enumerate(("ATSUGlyphGetQuadraticPaths", "ATSUGlyphGetCubicPaths")):
        registers = ((3, 5, 6, 3), (4, 4, 6, 3))[mode]
        routines = [callback(index, reg, index in (1, 2) or index == 0 and mode) for index, reg in enumerate(registers)]
        first = outputs + mode * 64
        for side in (0, 1):
            out = first + side * 32
            probe.call(name, [style, 36, *routines, ptr(out), ptr(out + 20)], side)
            p.emit(0x907E0000 | (out + 24))
            probe.call(name, [style, 36, *routines, ptr(out), 0], side)
            p.expect(-50, "reject a path request without a callback status output")
        for offset in (0, 4, 8, 12, 16, 24):
            p.emit(0x807E0000 | (first + offset), 0x809E0000 | (first + 32 + offset))
            p.equal("outline callback counts, coordinates, and status match the saved API")


def atsui_run_analysis(probe, layout):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    _, base = probe.raw(bytes(48))
    style = slot(base)
    for side in (0, 1):
        out = base + 8 + side * 16
        probe.call("ATSUCreateStyle", [ptr(base)])
        p.expect(0, "create the destination for common layout attributes")
        probe.call("ATSUGetContinuousAttributes", [layout, 0, 3, style], side)
        p.expect(0, "retrieve the attributes shared by a single run")
        probe.call("ATSUGetAttribute", [style, 262, 4, ptr(base + 40), ptr(base + 44)])
        p.expect(0, "retrieve the common font size")
        p.emit(0x807E0000 | (base + 40))
        p.expect(18 << 16, "common attributes include the single run's font size")
        probe.call("ATSUGetContinuousAttributes", [layout, 11, 1, style], side)
        p.expect(-8792, "reject a continuous attribute lookup outside the layout")
        probe.call("ATSUDisposeStyle", [style])
        p.expect(0, "dispose the common attribute style")
        probe.call("ATSUMatchFontsToText", [layout, 0, 0, 0, 0, 0], side)
        p.expect(0, "font matching has no work for an empty range")
        probe.call("ATSUMatchFontsToText", [layout, 0, 3, ptr(out), ptr(out + 4), ptr(out + 8)], side)
        p.emit(0x907E0000 | (out + 12))
    for offset in range(0, 16, 4):
        p.emit(0x807E0000 | (base + 8 + offset), 0x809E0000 | (base + 24 + offset))
        p.equal("font matching status and range outputs agree with the saved API")


def atsui_style_archives(probe, source):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    _, base = probe.raw(bytes(36))
    handle, style = slot(base), slot(base + 4)
    for writer, reader in ((1, 1), (0, 1), (1, 0)):
        probe.call("NewHandle", [4])
        p.nonzero("allocate the style archive handle")
        p.emit(0x907E0000 | base)
        probe.call("HGetState", [handle])
        p.emit(0x907E0000 | (base + 8))
        probe.call("ATSUCopyToHandle", [source, handle], writer)
        p.expect(0, "write a style archive to the handle")
        probe.call("HGetState", [handle])
        p.emit(0x809E0000 | (base + 8))
        p.equal("copy restores the handle state")
        probe.call("ATSUCreateStyle", [ptr(base + 4)])
        p.expect(0, "make a destination for the opposite implementation")
        probe.call("ATSUPasteFromHandle", [style, handle], reader)
        p.expect(0, "paste the style archive")
        probe.call("HGetState", [handle])
        p.emit(0x809E0000 | (base + 8))
        p.equal("paste restores the handle state")
        probe.call("ATSUGetAttribute", [style, 262, 4, ptr(base + 12), ptr(base + 16)])
        p.expect(0, "read the restored font size")
        p.emit(0x807E0000 | (base + 12))
        p.expect(18 << 16, "the serialized size survived the round trip")
        probe.call("ATSUGetFontFeature", [style, 0, ptr(base + 20), ptr(base + 22)])
        p.expect(0, "read the first restored feature")
        p.emit(0x807E0000 | (base + 20))
        p.expect(0x00010002, "the feature pair survived the round trip")
        probe.call("ATSUGetFontVariationValue", [style, 0x77676874, ptr(base + 24)])
        if not reader:
            p.expect(0, "read the weight axis after the native decoder consumes both feature fields")
            p.emit(0x807E0000 | (base + 24))
            p.expect(2 << 16, "the native decoder restores the serialized axis value")
        elif writer == reader:
            p.emit(0x907E0000 | (base + 28), 0x807E0000 | (base + 24), 0x907E0000 | (base + 32))
        else:
            p.emit(0x809E0000 | (base + 28))
            p.equal("the mixed round trip reports the same axis status as the saved APIs")
            p.emit(0x807E0000 | (base + 24), 0x809E0000 | (base + 32))
            p.equal("the mixed round trip returns the same axis data as the saved APIs")
        probe.call("DisposeHandle", [handle])
        probe.call("ATSUDisposeStyle", [style])
        p.expect(0, "dispose the restored style")


def atsui_style_edits(probe, source, tags, sizes, values, features, selectors, axes, variation_values):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    _, base = probe.raw(bytes(64))
    clone, destination = slot(base), slot(base + 4)
    value, count = ptr(base + 8), ptr(base + 12)

    def call(name, arguments, expected=0, side=0):
        probe.call(name, arguments, side)
        p.expect(expected, name + " style edit status")

    def check(offset, expected, label, width=4):
        p.emit({1: 0x887E0000, 2: 0xA07E0000, 4: 0x807E0000}[width] | (base + offset))
        p.expect(expected, label)

    def size(style, expected):
        call("ATSUGetAttribute", [style, 262, 4, value, count])
        check(8, expected, "style has the expected size")
        check(12, 4, "reported size attribute width")

    call("ATSUCreateAndCopyStyle", [source, 0], -50)
    call("ATSUCreateAndCopyStyle", [source, ptr(base)])
    call("ATSUCreateStyle", [ptr(base + 4)])
    size(clone, 18 << 16)
    call("ATSUCompareStyles", [clone, source, value])
    call("ATSUCompareStyles", [clone, source, ptr(base + 10)], side=1)
    p.emit(0xA07E0000 | (base + 8), 0xA09E0000 | (base + 10))
    p.equal("native and saved style comparisons agree for a cloned style")

    call("ATSUGetAllAttributes", [clone, 0, 1, count], -50)
    call("ATSUGetAllAttributes", [clone, ptr(base + 16), 1, count])
    call("ATSUGetAllAttributes", [clone, ptr(base + 32), 2, ptr(base + 48)], side=1)
    p.emit(0x807E0000 | (base + 12), 0x809E0000 | (base + 48))
    p.equal("attribute enumeration reports the full count")
    for offset in range(0, 8, 4):
        p.emit(0x807E0000 | (base + 16 + offset), 0x809E0000 | (base + 32 + offset))
        p.equal("the first attribute descriptor matches the saved API")
    check(24, 0, "one requested descriptor does not overwrite the following word")

    call("ATSUCopyAttributes", [source, destination])
    size(destination, 18 << 16)
    one_tag = probe.raw(struct.pack(">I", 262))
    one_size = probe.raw(struct.pack(">I", 4))
    small = probe.raw(struct.pack(">I", 9 << 16))
    probe.arguments([small], 0)
    p.emit(0x907E0000 | (base + 52))
    call("ATSUSetAttributes", [destination, 1, one_tag, one_size, ptr(base + 52)])
    call("ATSUUnderwriteAttributes", [source, destination])
    size(destination, 9 << 16)
    call("ATSUOverwriteAttributes", [source, destination])
    size(destination, 18 << 16)

    call("ATSUClearAttributes", [clone, 1, one_tag])
    call("ATSUGetAllAttributes", [clone, 0, 0, count])
    check(12, 1, "clearing one regular attribute removes only that tag")
    call("ATSUClearAttributes", [clone, -1, 0])
    call("ATSUGetAllAttributes", [clone, 0, 0, count])
    check(12, 0, "clear-all removes the rest of the regular attributes")

    call("ATSUSetFontFeatures", [clone, 2, features, selectors])
    call("ATSUClearFontFeatures", [clone, 1, features, selectors])
    call("ATSUGetAllFontFeatures", [clone, 0, 0, 0, count])
    check(12, 1, "clearing the first feature leaves the other pair")
    call("ATSUClearFontFeatures", [clone, -1, 0, 0])
    call("ATSUGetAllFontFeatures", [clone, 0, 0, 0, count])
    check(12, 0, "the feature clear-all sentinel needs no pair arrays")

    call("ATSUSetVariations", [clone, 1, axes, variation_values])
    call("ATSUClearFontVariations", [clone, 1, axes])
    call("ATSUGetAllFontVariations", [clone, 0, 0, 0, count])
    check(12, 0, "clearing a variation removes the requested axis")
    call("ATSUSetVariations", [clone, 1, axes, variation_values])
    call("ATSUClearFontVariations", [clone, -1, 0])
    call("ATSUGetAllFontVariations", [clone, 0, 0, 0, count])
    check(12, 0, "the variation clear-all sentinel needs no array")
    call("ATSUClearStyle", [destination])
    call("ATSUStyleIsEmpty", [destination, value])
    check(8, 1, "clearing the style restores its default attributes", 1)
    size(source, 18 << 16)
    call("ATSUDisposeStyle", [destination])
    call("ATSUDisposeStyle", [clone])


def atsui_layout_operations(probe, style, layout, text, run):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    _, base = probe.raw(bytes(192))
    clone, handle, handle_layout, bare = (slot(base + offset) for offset in (0, 4, 8, 12))
    value, count = ptr(base + 20), ptr(base + 24)

    def call(name, args=(), expected=0, side=0):
        probe.call(name, args, side)
        p.expect(expected, name + " returns the expected layout status")

    def word(offset, expected, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(expected, label)

    call("ATSUCreateTextLayout", [0], -50)
    call("ATSUCreateTextLayout", [ptr(base + 12)])
    call("ATSUGetTextLocation", [bare, value, 0, 0, 0, 0], -8790)
    call("ATSUSetTextPointerLocation", [bare, text, 1, 10, 10], -50)
    call("ATSUSetTextPointerLocation", [bare, text, 1, 9, 10])
    call("ATSUGetTextLocation", [bare, value, 0, count, 0, 0])
    word(24, 1, "a layout created empty can attach a nonzero-offset text run")
    call("ATSUSetRunStyle", [bare, style, 2, 3])
    call("ATSUGetRunStyle", [bare, 3, ptr(base + 28), ptr(base + 32), ptr(base + 36)])
    word(32, 2, "a borrowed run starts at the assigned offset")
    word(36, 3, "a borrowed run reports its assigned length")
    call("ATSUStyleIsEmpty", [slot(base + 28), value])
    p.emit(0x887E0000 | (base + 20))
    p.expect(0, "the borrowed style shares the original attributes")
    call("ATSUDisposeTextLayout", [bare])

    probe.call("NewHandle", [20])
    p.nonzero("allocate a movable UTF-16 buffer")
    p.emit(0x907E0000 | (base + 4), 0x80630000, 0x907E0000 | (base + 16))
    probe.call("BlockMoveData", [text, slot(base + 16), 20])
    probe.call("HGetState", [handle])
    p.emit(0x907E0000 | (base + 116))
    probe.arguments([style], 0)
    p.emit(0x907E0000 | (base + 28))
    call("ATSUCreateTextLayoutWithTextHandle", [handle, -1, -1, 10, 1, run, ptr(base + 28), ptr(base + 8)])
    call("ATSUTextMoved", [handle_layout, text], -50, side=1)
    call("ATSUTextMoved", [handle_layout, text], -50)
    call("ATSUGetTextLocation", [handle_layout, value, ptr(base + 40), 0, count, 0])
    word(24, 10, "a handle layout keeps the normalized length")
    p.emit(0x887E0000 | (base + 40))
    p.expect(1, "the text location remains a handle")
    call("ATSUCreateTextLayout", [ptr(base + 12)])
    call("ATSUSetTextHandleLocation", [bare, handle, 0, 10, 10])
    call("ATSUGetTextLocation", [bare, value, ptr(base + 40), 0, count, 0])
    word(24, 10, "the handle setter installs the expected text span")
    call("ATSUDisposeTextLayout", [bare])
    call("ATSUDisposeTextLayout", [handle_layout])
    probe.call("HGetState", [handle])
    p.emit(0x809E0000 | (base + 116))
    p.equal("the layout restores the text handle's previous lock state")
    probe.call("DisposeHandle", [handle])

    tags = probe.raw(struct.pack(">I", 1))
    sizes = probe.raw(struct.pack(">I", 4))
    width = probe.raw(struct.pack(">I", 78 << 16))
    p.emit(0x387E0000 | width[1], 0x907E0000 | (base + 44))
    call("ATSUSetLayoutControls", [layout, 1, tags, sizes, ptr(base + 44)])
    call("ATSUGetLayoutControl", [layout, 1, 4, value, count])
    word(20, 78 << 16, "the layout default width round trips")
    call("ATSUGetAllLayoutControls", [layout, ptr(base + 48), 1, count])
    word(24, 1, "enumerate one layout control")
    word(48, 1, "layout control metadata identifies the line width tag")
    word(52, 4, "line width is a Fixed control")
    call("ATSUCreateAndCopyTextLayout", [layout, ptr(base)])
    call("ATSUGetLayoutControl", [clone, 1, 4, value, count])
    word(20, 78 << 16, "the cloned layout owns its own control record")
    call("ATSUClearLayoutControls", [clone, 1, tags])
    call("ATSUGetAllLayoutControls", [clone, 0, 0, count])
    word(24, 0, "clearing clone controls leaves an empty list")
    call("ATSUGetLayoutControl", [layout, 1, 4, value, count])
    word(20, 78 << 16, "changing the clone leaves the source controls unchanged")
    call("ATSUCopyLayoutControls", [layout, 0], -50, side=1)
    call("ATSUCopyLayoutControls", [layout, 0], -50)
    call("ATSUCopyLayoutControls", [layout, clone])
    call("ATSUGetLayoutControl", [clone, 1, 4, value, count])
    word(20, 78 << 16, "copy controls into an existing empty control record")
    for side in (0, 1):
        call("ATSUCopyLineControls", [layout, 11, clone, 0], -8792, side)
        call("ATSUCopyLineControls", [layout, 0, 0, 0], -50, side)
        call("ATSUSetLineControls", [layout, 0, 1, tags, sizes, ptr(base + 44)])
        call("ATSUCopyLineControls", [layout, 0, clone, 0], side=side)
        call("ATSUGetLineControl", [clone, 0, 1, 4, value, count])
        word(20, 78 << 16, "copied line width is readable in the destination layout")
        call("ATSUClearLineControls", [clone, 0, 1, tags])
        call("ATSUClearLayoutCache", [clone, 11], -8792, side)
        call("ATSUClearLayoutCache", [clone, -1], side=side)
        call("ATSUIdle", [clone], side=side)
    replacement = probe.raw("ATSUI text".encode("utf-16be"))
    call("ATSUTextMoved", [clone, 0], -50)
    call("ATSUTextMoved", [clone, replacement], side=1)
    call("ATSUTextMoved", [clone, text])
    call("ATSUTextMoved", [clone, replacement])
    call("ATSUGetTextLocation", [clone, value, 0, 0, count, 0])
    p.emit(0x807E0000 | (base + 20))
    p.pointer(replacement[1], "moving external text updates the borrowed pointer")
    word(24, 10, "moving external text preserves the selected length")
    for side in (0, 1):
        call("ATSUTextDeleted", [clone, 11, 1], -8792, side)
        call("ATSUTextInserted", [clone, 11, 1], -8792, side)
        call("ATSUTextDeleted", [clone, 5, 1], side=side)
        call("ATSUGetTextLocation", [clone, value, 0, 0, count, 0])
        word(24, 9, "deletion updates the live layout length")
        call("ATSUTextInserted", [clone, 5, 1], side=side)
        call("ATSUGetTextLocation", [clone, value, 0, 0, count, 0])
        word(24, 10, "insertion restores the live layout length")
    call("ATSUDisposeTextLayout", [clone])
    call("ATSUSetLineControls", [layout, 0, 1, tags, sizes, ptr(base + 44)])
    call("ATSUGetLineControl", [layout, -1, 1, 4, value, count])
    word(20, 78 << 16, "the start sentinel resolves a line's explicit width")
    call("ATSUGetAllLineControls", [layout, 0, ptr(base + 48), 1, count])
    word(48, 1, "line control metadata identifies the line width tag")
    call("ATSUClearLineControls", [layout, 0, 1, tags])
    call("ATSUGetLineControl", [layout, 0, 1, 4, value, count], -8801)
    call("ATSUGetLayoutControl", [layout, 1, 4, value, count])
    word(20, 78 << 16, "clearing a line override preserves the layout width")

    call("ATSUClearSoftLineBreaks", [layout, 0, 10])
    call("ATSUGetSoftLineBreaks", [layout, 0, 10, 0, 0, count])
    word(24, 0, "clearing the selected range removes its soft line breaks")
    call("ATSUFONDtoFontID", [21, 0, 0], -50)
    for family, offset in ((21, 120), (0, 128)):
        probe.scalar("ATSUFONDtoFontID", [family, 0, ("pair", base + offset)])
        p.emit(0x807E0000 | (base + offset), 0x809E0000 | (base + offset + 4))
        p.equal("native and saved font lookup agree on the output ID")


def atsui_render_operations(probe, layout):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    _, base = probe.raw(bytes(264))
    _, glyph_info = probe.raw(bytes(524))
    bounds = probe.raw(struct.pack(">4h", 0, 0, 64, 176))
    world, pixmap, region = (slot(base + offset) for offset in (8, 12, 24))
    probe.call("GetGWorld", [ptr(base), ptr(base + 4)])
    probe.call("NewGWorld", [ptr(base + 8), 32, bounds, 0, 0, 0])
    p.expect(0, "create an offscreen test port for ATSUI")
    probe.call("GetGWorldPixMap", [world])
    p.nonzero("get the offscreen test pixmap")
    p.emit(0x907E0000 | (base + 12))
    probe.call("LockPixels", [pixmap])
    p.expect(1, "lock the test pixmap while comparing glyphs")
    probe.call("GetPixBaseAddr", [pixmap])
    p.nonzero("map the locked test pixels")
    p.emit(0x907E0000 | (base + 16))
    probe.call("GetPixRowBytes", [pixmap])
    p.emit(0x907E0000 | (base + 20))
    probe.call("SetGWorld", [world, 0])
    probe.call("NewRgn")
    p.nonzero("create an ATSUI highlight region")
    p.emit(0x907E0000 | (base + 24))

    def call(name, arguments, side, expected=0):
        probe.call(name, arguments, side)
        p.expect(expected, name + " returns the expected drawing status")

    def count_pixels():
        # Count pixels whose value differs from the untouched top-left corner.
        p.emit(
            0x80BE0000 | (base + 16),  # lwz r5, base address
            0x809E0000 | (base + 20),  # lwz r4, row bytes
            0x81450000,  # lwz r10, 0(r5)
            0x38600000,  # li r3, 0
            0x38C00040,  # li r6, 64
        )
        row_start = len(p.code)
        p.emit(0x7CA82B78, 0x38E000B0, 0x7CE903A6)  # mr r8,r5; li r7,176; mtctr r7.
        pixel_start = len(p.code)
        p.emit(0x81280000, 0x7C095000, 0x41820008, 0x38630001, 0x39080004)
        p.emit(0x42000000 | (((pixel_start - len(p.code)) * 4) & 0xFFFF))
        p.emit(0x7CA52214, 0x38C6FFFF, 0x2C060000)  # add r5,r5,r4; addi r6,r6,-1; cmpwi r6,0.
        p.emit(0x40820000 | (((row_start - len(p.code)) * 4) & 0xFFFF))

    for side in (1, 0):
        probe.call("EraseRect", [bounds])
        probe.call("MoveTo", [8, 32])
        x, y = (8 << 16, 32 << 16) if side else (-1, -1)
        output = base + 32 + 32 * side
        call("ATSUMeasureText", [layout, 0, 10, ptr(output), ptr(output + 4), 0, 0], side)
        call("ATSUMeasureTextImage", [layout, 0, 10, x, y, ptr(output + 8)], side)
        probe.call("MoveTo", [8, 32])
        call("ATSUDrawText", [layout, 0, 10, x, y], side)
        count_pixels()
        p.nonzero("ATSUDrawText changed the offscreen pixels")
        p.emit(0x907E0000 | (output + 16))
        probe.call("MoveTo", [8, 32])
        call("ATSUGetTextHighlight", [layout, x, y, 0, 2, region], side)
        probe.call("EmptyRgn", [region])
        p.expect(0, "highlight the first two characters")
        probe.call("GetRegionBounds", [region, ptr(output + 20)])
        call("ATSUGetTextHighlight", [layout, x, y, 0, 2, 0], side, -50)
        call("ATSUMeasureText", [layout, 11, 1, 0, 0, 0, 0], side, -8792)
        call("ATSUHighlightText", [layout, x, y, 11, 1], side, -8792)
        call("ATSUUnhighlightText", [layout, x, y, 11, 1], side, -8792)
        call("ATSUHighlightText", [layout, x, y, 0, 2], side)
        call("ATSUUnhighlightText", [layout, x, y, 0, 2], side)
        call("ATSUPositionToOffset", [layout, x, y, 0, 0, 0], side, -50)
        navigation = base + 104 + 80 * side
        p.emit(0x3800FFFF, 0x901E0000 | navigation)
        probe.call(
            "ATSUPositionToOffset", [layout, 0, 0, ptr(navigation), ptr(navigation + 4), ptr(navigation + 8)], side
        )
        p.emit(0x907E0000 | (navigation + 76))
        call("ATSUOffsetToPosition", [layout, 11, 1, 0, 0, 0], side, -8792)
        call(
            "ATSUOffsetToPosition",
            [layout, 2, 1, ptr(navigation + 36), ptr(navigation + 52), ptr(navigation + 68)],
            side,
        )
        for index, name in enumerate(
            (
                "ATSUNextCursorPosition",
                "ATSUPreviousCursorPosition",
                "ATSULeftwardCursorPosition",
                "ATSURightwardCursorPosition",
            )
        ):
            call(name, [layout, 11, 0, ptr(navigation + 12 + 4 * index)], side, -8792)
            call(name, [layout, 3, 0, ptr(navigation + 12 + 4 * index)], side)
        call("ATSUBreakLine", [layout, 11, 30 << 16, 0, ptr(navigation + 28)], side, -8792)
        probe.call("ATSUBreakLine", [layout, 0, 30 << 16, 0, ptr(navigation + 28)], side)
        p.emit(0x907E0000 | (navigation + 32))
        p.emit(0x38000200, 0x901E0000 | glyph_info)
        call("ATSUGetGlyphInfo", [layout, 0, 1, ptr(glyph_info), ptr(glyph_info + 4)], side)
        probe.call("EraseRect", [bounds])
        call("ATSUDrawGlyphInfo", [ptr(glyph_info + 4), 0x41000000, 0x42000000], side)
        count_pixels()
        p.nonzero("drawing the glyph information changed the offscreen pixels")
        p.emit(0x907E0000 | (glyph_info + 516 + 4 * side))
    p.emit(0x807E0000 | (glyph_info + 516), 0x809E0000 | (glyph_info + 520))
    p.equal("both glyph information renderers change the same number of pixels")
    for offset in (0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 68, 76):
        p.emit(0x807E0000 | (base + 104 + offset), 0x809E0000 | (base + 184 + offset))
        p.equal("native and saved cursor navigation agree")
    for offset, label in enumerate(
        (
            "text start",
            "text end",
            "image top and left",
            "image bottom and right",
            "rendered pixel count",
            "highlight top and left",
            "highlight bottom and right",
        )
    ):
        p.emit(0x807E0000 | (base + 32 + offset * 4), 0x809E0000 | (base + 64 + offset * 4))
        p.equal("native and saved " + label + " agree")
    probe.call("DisposeRgn", [region])
    probe.call("UnlockPixels", [pixmap])
    probe.call("SetGWorld", [slot(base), slot(base + 4)])
    probe.call("DisposeGWorld", [world])


def window_queue_operations(probe, window, base):
    p = probe.p
    slot, ptr = probe.slot, probe.ptr
    state, original, queue, output, event, active = (base + offset for offset in (0, 4, 8, 12, 16, 20))

    def queue_record(offset, saved):
        p.emit(0x809E0000 | state, 0x80640000 | offset, 0x907E0000 | saved)

    def install_queue(offset, source):
        p.emit(0x809E0000 | state, 0x807E0000 | source, 0x90640000 | offset)

    # Each test temporarily owns a separate queue in the current saved process.
    p.emit(0x806002B6, 0x806302A4, 0x80630008, 0x907E0000 | state)
    p.nonzero("the saved Window Manager has per-process queue state")
    queue_record(0x10, original)
    for side in (0, 1):
        probe.call("NewStackListSys", [8, 4, 0x77696E64, 0x61637476, ptr(queue)])
        p.expect(0, "allocate an isolated activation event queue")
        install_queue(0x10, queue)
        probe.arguments([window], 0)
        p.emit(0x907E0000 | event, 0x38600001, 0x987E0000 | active)
        probe.call("EnqueueStackListElement", [slot(queue), ptr(event)])
        p.expect(0, "enqueue an activation record in the original stack layout")
        p.emit(0x38600000, 0x987E0000 | active)
        probe.call("GetNextWindowActivateEvent", [ptr(output), ptr(active), 0], side=side)
        p.expect(1, "a queued activation is returned even when the low-memory remove flag is clear")
        p.emit(0x807E0000 | output, 0x809E0000 | event)
        p.equal("the activation record preserves its window pointer")
        p.emit(0x887E0000 | active)
        p.expect(1, "the activation record preserves its Boolean")
        probe.call("CountStackListElements", [slot(queue)])
        p.expect(0, "the saved activation queue was consumed")
        install_queue(0x10, original)
        probe.call("DisposeStackList", [slot(queue)])
        p.expect(0, "release the isolated activation queue")

    queue_record(0x0C, original)
    p.emit(0x809E0000 | state, 0x38600000, 0x9064000C)
    for side in (0, 1):
        probe.call("PostDelayedWindowReshape", [window], side=side)
        queue_record(0x0C, queue)
        p.nonzero("posting a delayed reshape creates or reuses the process queue")
        probe.call("CountStackListElements", [slot(queue)])
        p.expect(side + 1, "both reshape APIs append to the same saved queue")
    for _ in range(2):
        probe.call("PopStackListElement", [slot(queue), ptr(output)])
        p.expect(0, "read a queued reshape using the original stack list")
        probe.arguments([window], 0)
        p.emit(0x809E0000 | output)
        p.equal("the queued reshape contains the requested window")
    install_queue(0x0C, original)
    probe.call("DisposeStackList", [slot(queue)])
    p.expect(0, "release the isolated reshape queue")

    for side in (0, 1):
        probe.call("PostWindowActivateEvent", [window, 1], side=side)
        probe.arguments([window], 0)
        p.emit(0x8863006F)
        p.expect(1, "posting activation highlights the window")
        probe.call("PostWindowActivateEvent", [window, 0], side=1 - side)
        probe.arguments([window], 0)
        p.emit(0x8863006F)
        p.expect(0, "posting deactivation clears the highlight")


def window_proxy_operations(probe, window, base):
    p = probe.p
    out = probe.ptr(base)
    mouse = probe.far_raw(bytes(4))
    unset_spec = probe.far_raw(b"\x5a" * 70)
    for name, output, missing in (
        ("GetWindowProxyAlias", out, -5602),
        ("GetWindowProxyFSSpec", unset_spec, -5602),
        ("GetWindowProxyIcon", out, -5604),
    ):
        probe.scalar(name, [0, output], -5600)
        probe.scalar(name, [window, output], missing)
    probe.arguments([unset_spec], 0)
    p.emit(0x80630000)
    p.expect(0x5A5A5A5A, "an unregistered proxy leaves the file specification untouched")

    probe.call("NewRgn")
    p.nonzero("allocate a caller-owned proxy drag outline")
    p.emit(0x907E0000 | (base + 8))
    outline = probe.slot(base + 8)
    probe.scalar("BeginWindowProxyDrag", [0, out, outline], -5600)
    probe.scalar("BeginWindowProxyDrag", [window, 0, outline], -50)
    probe.scalar("BeginWindowProxyDrag", [window, out, 0], -50)
    probe.scalar("BeginWindowProxyDrag", [window, out, outline], -5602)
    p.emit(0x807E0000 | base)
    p.expect(0, "a missing proxy leaves no allocated drag")
    probe.scalar("EndWindowProxyDrag", [0, 0], -5600)
    probe.scalar("EndWindowProxyDrag", [window, 0], -50)
    probe.scalar("TrackWindowProxyDrag", [0, 0], -5600)
    probe.scalar("TrackWindowProxyDrag", [window, 0], -5602)
    probe.scalar("TrackWindowProxyFromExistingDrag", [0, 0, 0, outline], -5600)

    file_name = b"URL buffer fixture"
    name = probe.far_raw(bytes([len(file_name)]) + file_name)
    file_spec = probe.far_raw(bytes(70))
    probe.call("FSMakeFSSpec", [probe.slot(0x1FC0), probe.slot(0x1F42), name, file_spec])
    p.expect(0, "find the file used by the window proxy")
    probe.scalar("SetWindowProxyFSSpec", [window, 0], -50)
    probe.scalar("SetWindowProxyAlias", [window, 0], -50)
    probe.scalar("SetWindowProxyIcon", [window, 0], -50)
    probe.scalar("SetWindowProxyCreatorAndType", [0, 0, 0, 0], -5600)
    probe.scalar("RemoveWindowProxy", [0], -5600)
    probe.scalar("SetWindowProxyFSSpec", [window, file_spec], 0)
    result = probe.far_raw(bytes(70))
    for side in (0, 1):
        probe.call("GetWindowProxyAlias", [window, out], side=side)
        p.expect(0, "copy the proxy AliasHandle")
        p.emit(0x807E0000 | base)
        p.nonzero("the copied alias has separate caller ownership")
        probe.call("SetWindowProxyAlias", [window, probe.slot(base)], side=1 - side)
        p.expect(0, "assign the alias through the other implementation")
        probe.call("DisposeHandle", [probe.slot(base)])
        probe.call("GetWindowProxyFSSpec", [window, result], side=side)
        p.expect(0, "resolve the proxy after disposing its alias copy")
        probe.arguments([result, file_spec], 0)
        p.emit(0x80630006, 0x80840006)
        p.equal("native and saved resolution return the proxy file name")
    for side in (0, 1):
        probe.call("GetWindowProxyIcon", [window, out], side=side)
        p.expect(0, "return the cached icon for the file-backed proxy")
        p.emit(0x807E0000 | base)
        if side == 0:
            p.nonzero("the window owns an icon")
            p.emit(0x907E0000 | (base + 4))
        else:
            p.emit(0x809E0000 | (base + 4))
            p.equal("both implementations borrow the same cached IconRef")
            probe.call("SetWindowProxyIcon", [window, probe.slot(base)], side=side)
            p.expect(0, "setting the borrowed icon retains ownership for the window")

    for side in (0, 1):
        probe.call("SetWindowProxyFSSpec", [window, file_spec], side=side)
        p.expect(0, "install the file proxy in both implementations")
        probe.call("SetWindowProxyCreatorAndType", [window, 0x6D616373, 0x646F6375, -32768], side=side)
        p.expect(0, "select the generic document proxy icon by creator and type")
        probe.call("GetWindowProxyIcon", [window, out], side=1 - side)
        p.expect(0, "the other implementation sees the newly installed typed icon")
        probe.call("SetWindowProxyIcon", [window, probe.slot(base)], side=side)
        p.expect(0, "setting the current icon keeps the same shared reference")
        probe.call("BeginWindowProxyDrag", [window, out, outline], side=side)
        p.expect(0, "create a drag and its outline using the window proxy")
        p.emit(0x807E0000 | base)
        p.nonzero("successful proxy begin returns the drag reference")
        probe.call("GetGlobalMouse", [mouse])
        probe.arguments([mouse], 0)
        p.emit(0x80630000, 0x907E0000 | (base + 12))
        probe.call(
            "TrackWindowProxyFromExistingDrag",
            [window, probe.slot(base + 12), probe.slot(base), outline],
            side=1 - side,
        )
        p.expect(-128, "no mouse motion cancels tracking of the existing drag")
        probe.call("EndWindowProxyDrag", [window, probe.slot(base)], side=1 - side)
        p.expect(0, "the other implementation disposes the shared drag image and reference")
        probe.call("GetGlobalMouse", [mouse])
        probe.arguments([mouse], 0)
        p.emit(0x80630000, 0x907E0000 | (base + 12))
        probe.call("TrackWindowProxyDrag", [window, probe.slot(base + 12)], side=side)
        p.expect(-128, "the one-call tracker cancels and cleans up its temporary drag")
        probe.call("RemoveWindowProxy", [window], side=side)
        p.expect(0, "remove both the proxy alias and the owned icon")
        probe.call("GetWindowProxyFSSpec", [window, result], side=1 - side)
        p.expect(-5602, "the other implementation sees the removed alias")
        probe.call("GetWindowProxyIcon", [window, out], side=1 - side)
        p.expect(-5604, "the other implementation sees the removed icon")
    probe.call("DisposeRgn", [outline])


def desktop_pattern_fixture():
    # Flattened PixPat, PixMap, four pixel bytes and two indexed colors.
    pattern = bytearray(106)
    struct.pack_into(">HII", pattern, 0, 1, 28, 78)
    struct.pack_into(">H4h", pattern, 32, 0x8002, 0, 0, 2, 8)
    struct.pack_into(">4h", pattern, 58, 0, 1, 1, 1)
    struct.pack_into(">I", pattern, 70, 82)
    pattern[78:82] = bytes.fromhex("11223344")
    struct.pack_into(">IHH8H", pattern, 82, 0, 0, 1, 0, 0, 0, 0, 1, 0xFFFF, 0xFFFF, 0xFFFF)
    return pattern


def desktop_window_operations(probe, base):
    p = probe.p
    original, custom, output, data, saved_port, final_port = (base + offset for offset in range(0, 24, 4))
    original_name = probe.far_raw(bytes(256))
    read_name = probe.far_raw(bytes(256))
    probe.call("GetDesktopPattern", [probe.ptr(original), original_name], side=1)
    p.expect(0, "preserve the saved desktop pattern and its name")
    p.emit(0x807E0000 | original)
    p.nonzero("the saved desktop returns a caller-owned flattened pattern")
    probe.scalar("SetDesktopPattern", [0, original_name], -50)
    probe.scalar("SetDesktopPattern", [probe.slot(original), 0], -50)

    pixel_bytes = desktop_pattern_fixture()
    encoded = probe.far_raw(pixel_bytes)
    probe.call("NewHandle", [len(pixel_bytes)])
    p.nonzero("allocate a flattened replacement desktop pattern")
    p.emit(0x907E0000 | custom, 0x80630000, 0x907E0000 | data)
    probe.call("BlockMoveData", [encoded, probe.slot(data), len(pixel_bytes)])
    probe.call("GetPort", [probe.ptr(saved_port)])
    for side in (0, 1):
        name = probe.far_raw(b"\x04" + (b"One!" if side == 0 else b"Two!"))
        probe.call("SetDesktopPattern", [probe.slot(custom), name], side=side)
        p.expect(0, "install and save a desktop pattern")
        probe.call("GetDesktopPattern", [probe.ptr(output), read_name], side=1 - side)
        p.expect(0, "the other window manager reads the persisted flattened pattern")
        probe.arguments([read_name, name], 0)
        p.emit(0x80630000, 0x80840000)
        p.equal("the preference stores the original Pascal name")
        p.emit(0x807E0000 | output)
        p.nonzero("reading desktop preferences returns a flattened handle")
        p.emit(0x80630000, 0x8063004E)
        p.expect(0x11223344, "the caller owns a copy of the original pattern bytes")
        probe.call("DisposeHandle", [probe.slot(output)])
        probe.call("GetDesktopPixPat", [probe.ptr(output), 0], side=1 - side)
        p.expect(0, "unflatten a decoded desktop PixPat")
        p.emit(0x807E0000 | output)
        p.nonzero("decoding desktop preferences returns a PixPat handle")
        p.emit(0x813E0000 | output, 0x81290000, 0x81290006, 0x81290000, 0x80690000)
        p.expect(0x11223344, "the decoded PixPat retains its source pixels")
        probe.call("DisposePixPat", [probe.slot(output)])
        probe.call("RebuildDesktopRegion", [], side=side)
        p.expect(0, "recalculate the desktop across the active screen")
        probe.call("DrawDesktop", [], side=1 - side)
        probe.call("GetPort", [probe.ptr(final_port)])
        p.emit(0x807E0000 | saved_port, 0x809E0000 | final_port)
        p.equal("desktop pattern and drawing operations restore the caller's port")
    probe.call("SetDesktopPattern", [probe.slot(original), original_name], side=1)
    p.expect(0, "restore the original desktop preference")
    probe.call("DisposeHandle", [probe.slot(original)])
    probe.call("DisposeHandle", [probe.slot(custom)])


def monitor_desktop_operations(probe, base):
    p = probe.p
    internal, original, working, refreshed, count, value = (base + offset for offset in range(0, 24, 4))
    first = probe.far_raw(bytes(0x174))
    last = probe.far_raw(bytes(0x174))
    original_count = probe.far_raw(bytes(4))
    file_name = probe.far_raw(b"\x12URL buffer fixture")
    file_spec = probe.far_raw(bytes(70))
    resolved_file = probe.far_raw(bytes(70))
    picture_name = probe.far_raw(b"\x14Desktop PICT fixture")
    picture_file = probe.far_raw(bytes(70))
    picture_info = probe.far_raw(struct.pack(">II4h", 0x50494354, 0x524F4D41, 0, 0, 0, 0))
    target_port = probe.raw(bytes(4))
    bounds = probe.far_raw(struct.pack(">4h", 0, 0, 100, 100))
    sentinel = probe.slot(internal)
    current = probe.slot(original)
    copy = probe.slot(working)
    new_copy = probe.slot(refreshed)

    probe.scalar("GetCurrentMonitorDesktopList", [0], -50)
    probe.scalar("SetCurrentMonitorDesktopList", [0], -50)
    probe.scalar("GetInternalMonitorDesktopList", [0], -50)
    probe.scalar("DisposeMonitorDesktopList", [0], -50)
    probe.scalar("RefreshMonitorDesktopList", [0, probe.ptr(refreshed)], -50)
    probe.scalar("CountMonitorDesktopRecordsInList", [0, probe.ptr(count)], -50)
    probe.scalar("SetMonitorDesktopToFile", [0, 1, 0, 5], -50)
    probe.scalar("CopyMonitorDesktopFile", [0, 1, 0, 1], -50)
    probe.scalar("SetDesktopPictureForDevice", [0, 0, 5], -50)
    probe.scalar("GetDesktopPictureForDevice", [0, resolved_file, 0], -50)
    probe.scalar("GetInternalMonitorDesktopList", [probe.ptr(internal)], 0)
    p.emit(0x807E0000 | internal)
    p.expect(-1, "the internal list is addressed through the original sentinel")
    probe.scalar("DisposeMonitorDesktopList", [sentinel], -50)
    probe.scalar("SetCurrentMonitorDesktopList", [sentinel], -50)
    probe.scalar("RefreshMonitorDesktopList", [sentinel, probe.ptr(refreshed)], -50)
    probe.call("CountMonitorDesktopRecordsInList", [sentinel, probe.ptr(count)], side=1)
    p.expect(0, "count the saved internal list before publishing a new snapshot")
    probe.call("CountMonitorDesktopRecordsInList", [sentinel, probe.ptr(value)])
    p.expect(0, "count the internal list even when the desktop has not installed one")
    p.emit(0x807E0000 | value, 0x809E0000 | count)
    p.equal("native and saved counts agree before installing the snapshot")
    probe.call("GetCurrentMonitorDesktopList", [probe.ptr(original)], side=1)
    p.expect(0, "save an independent copy of the current monitor list")
    p.emit(0x807E0000 | original)
    p.nonzero("the current desktop list has a saved handle")
    probe.call("CountMonitorDesktopRecordsInList", [current, original_count], side=1)
    p.expect(0, "count the active display records in the rebuilt snapshot")
    probe.arguments([original_count], 0)
    p.emit(0x80630000)
    p.nonzero("the current desktop has at least one monitor record")
    probe.call("SetCurrentMonitorDesktopList", [current])
    p.expect(0, "publish the active display snapshot before using the internal list")
    probe.call("FSMakeFSSpec", [probe.slot(0x1FC0), probe.slot(0x1F42), file_name, file_spec])
    p.expect(0, "resolve a local file for the monitor picture alias")
    probe.call("FSMakeFSSpec", [probe.slot(0x1FC0), probe.slot(0x1F42), picture_name, picture_file])
    p.expect(0, "resolve the local data-fork PICT fixture")
    probe.call("FSpSetFInfo", [picture_file, picture_info])
    p.expect(0, "mark the fixture as a classic PICT file")
    probe.call("GetPort", [target_port])
    p.emit(0x807E0000 | target_port[1])
    p.nonzero("pass the live drawing port to the monitor timing query")

    for side in (0, 1):
        probe.call("GetCurrentMonitorDesktopList", [probe.ptr(working)], side=side)
        p.expect(0, "create an editable monitor desktop list")
        p.emit(0x807E0000 | working)
        p.nonzero("the editable list has its own handle")
        probe.call("CountMonitorDesktopRecordsInList", [copy, probe.ptr(count)], side=1 - side)
        p.expect(0, "both managers understand the same monitor list layout")
        probe.arguments([original_count], 0)
        p.emit(0x80630000, 0x809E0000 | count)
        p.equal("copying preserves the number of current displays")
        probe.call("GetMonitorDesktopRecordFromList", [copy, 0, first], side=side)
        p.expect(-50, "monitor record indices are one-based")
        probe.call("GetMonitorDesktopRecordFromList", [copy, 0x7FFFFFFF, last], side=side)
        p.expect(0, "the last-record index sentinel resolves")
        probe.call("GetMonitorDesktopRecordFromList", [copy, probe.slot(count), first], side=1 - side)
        p.expect(0, "the explicit last index resolves")
        probe.arguments([first, last], 0)
        p.emit(0x8063001C, 0x8084001C)
        p.equal("last-record indexing exposes the same cache fields")
        probe.call("SetMonitorDesktopRecordFromList", [copy, probe.slot(count), first], side=side)
        p.expect(0, "a record can be written back to its existing slot")
        probe.call("MonitorDesktopHasPicture", [copy, 1, probe.ptr(value)], side=side)
        p.expect(0, "query the picture flag of a valid monitor record")
        probe.call("SetMonitorDesktopAlignment", [copy, 1, 0], side=side)
        p.expect(-50, "reject an invalid alignment")
        for alignment in (5, 2):
            probe.call("SetMonitorDesktopAlignment", [copy, 1, alignment], side=side)
            p.expect(0, "change a private copy's alignment")
            probe.call("GetMonitorDesktopAlignment", [copy, 1, probe.ptr(value)], side=1 - side)
            p.expect(0, "read alignment through the other window manager")
            p.emit(0x807E0000 | value)
            p.expect(alignment, "tiling and ordinary alignment use the saved record flags")
        probe.call("RefreshMonitorDesktopList", [copy, probe.ptr(refreshed)], side=side)
        p.expect(0, "refresh the monitor list against the current displays")
        p.emit(0x807E0000 | refreshed)
        p.nonzero("refresh returns a new handle")

        probe.call("SetMonitorDesktopToFile", [copy, 1, file_spec, 5], side=side)
        p.expect(0, "set the monitor picture to a local file reference")
        probe.call("CopyMonitorDesktopFile", [copy, 1, new_copy, 1], side=1 - side)
        p.expect(0, "copy the file reference into a separate list")
        probe.call("SetMonitorDesktopToFile", [copy, 1, 0, 2], side=1 - side)
        p.expect(0, "clear the source image and release its alias")
        timing = [copy, 1, probe.slot(target_port[1]), bounds, probe.ptr(value)]
        probe.call("DrawMonitorDesktopFromListWillTakeTime", timing, side=side)
        p.expect(0, "query draw timing without a desktop picture")
        p.emit(0x887E0000 | value)
        p.expect(0, "a background without a picture needs no picture load")
        probe.call("FileContainsPossibleDesktopPicture", [picture_file, probe.ptr(value)], side=side)
        p.expect(0, "read the PICT file's Finder metadata")
        p.emit(0x887E0000 | value)
        p.expect(1, "the file type identifies a possible desktop picture")
        probe.call("GetPictureFromFile", [picture_file, probe.ptr(value)], side=side)
        p.expect(0, "load a PICT from the data fork")
        p.emit(0x807E0000 | value)
        p.nonzero("the caller owns the loaded PICT handle")
        p.emit(0x80630000, 0xA0630000)
        p.expect(34, "the picture record begins after the 512-byte file header")
        probe.call("DisposeHandle", [probe.slot(value)])
        probe.call("SetMonitorDesktopToFile", [copy, 1, picture_file, 2], side=1 - side)
        p.expect(0, "associate the PICT with the editable monitor list")
        probe.call("DrawMonitorDesktopFromListWillTakeTime", timing, side=side)
        p.expect(0, "query draw timing before a PICT has been cached")
        p.emit(0x887E0000 | value)
        p.expect(1, "the first picture draw requires loading its image")
        probe.call("SetMonitorDesktopToFile", [copy, 1, 0, 2], side=1 - side)
        p.expect(0, "clear the PICT before disposing the source list")
        probe.call("MonitorDesktopHasPicture", [new_copy, 1, probe.ptr(value)], side=side)
        p.expect(0, "the destination list still has its own alias")
        p.emit(0x887E0000 | value)
        p.expect(1, "releasing the source does not clear the copied picture")
        probe.call("SetMonitorDesktopToFile", [new_copy, 1, 0, 2], side=side)
        p.expect(0, "clear the private list before publishing")

        probe.call("GetMonitorDesktopRecordFromList", [sentinel, 1, first], side=1 - side)
        p.expect(0, "read the active display's device")
        probe.arguments([first], 0)
        p.emit(0x80630018, 0x907E0000 | count)
        device = probe.slot(count)
        probe.call("SetDesktopPictureForDevice", [device, file_spec, 5], side=side)
        p.expect(0, "assign the picture by its display device")
        probe.call("GetDesktopPictureForDevice", [device, resolved_file, probe.ptr(value)], side=1 - side)
        p.expect(0, "resolve the aliased picture from the live display")
        p.emit(0x807E0000 | value)
        p.expect(5, "device lookup preserves the tile alignment")
        probe.arguments([file_spec, resolved_file], 0)
        p.emit(0x80630006, 0x80840006)
        p.equal("device lookup returns the original file name")
        probe.call("SetDesktopPictureForDevice", [device, 0, 2], side=1 - side)
        p.expect(0, "clear the live desktop picture")
        probe.call("GetDesktopPictureForDevice", [device, resolved_file, 0], side=side)
        p.expect(-5605, "device lookup reports that the picture is absent")

        probe.call("SetCurrentMonitorDesktopList", [new_copy], side=1 - side)
        p.expect(0, "publish an independent copy as the current monitor list")
        probe.call("DisposeMonitorDesktopList", [new_copy], side=side)
        p.expect(0, "free the caller's refreshed copy after publishing")
        probe.call("GetMonitorDesktopAlignment", [sentinel, 1, probe.ptr(value)], side=side)
        p.expect(0, "the internal list is still available after freeing the copy")
        probe.call("DisposeMonitorDesktopList", [copy], side=1 - side)
        p.expect(0, "free the editable monitor list through the other implementation")
        probe.call("InternalMonitorDesktopListChanged", [], side=side)
    probe.call("SetCurrentMonitorDesktopList", [current], side=1)
    p.expect(0, "restore the saved monitor desktop list")
    probe.call("DisposeMonitorDesktopList", [current])
    p.expect(0, "dispose the original snapshot after restoring it")


def window_metadata_operations(probe, window, base):
    p = probe.p
    features, original_features, status, source, target, initial = (base + 4 * index for index in range(6))

    def load(offset, register):
        p.emit(0x801E0000 | register << 21 | offset)

    def store(offset):
        p.emit(0x907E0000 | offset)

    probe.scalar("WMGetWindowFeatures", [0, probe.ptr(features)], -5600)
    load(features, 3)
    p.expect(0, "an invalid window clears the features output")
    probe.call("WMGetWindowFeatures", [window, probe.ptr(initial)], side=1)
    store(status)
    probe.call("WMGetWindowFeatures", [window, probe.ptr(features)])
    load(status, 4)
    p.equal("native and saved WDEF feature query return the same status")
    load(features, 3)
    load(initial, 4)
    p.equal("native and saved WDEF feature bits agree")

    for setter in (0, 1):
        probe.call("SetWindowFeatures", [window, 4 if setter == 0 else 0], side=setter)
        probe.call("WMGetWindowFeatures", [window, probe.ptr(features)], side=setter)
        store(status)
        probe.call("WMGetWindowFeatures", [window, probe.ptr(original_features)], side=1 - setter)
        load(status, 4)
        p.equal("feature updates share the WDEF query status")
        load(features, 3)
        load(original_features, 4)
        p.equal("feature updates are visible to both window managers")
        probe.call("WMIsWindowCollapsable", [window], side=1 - setter)
        load(features, 4)
        p.emit(0x5484F7FE)  # move the collapse feature bit into the Boolean result position
        p.equal("the collapse capability follows the shared WDEF feature bit")
    probe.call("SetWindowFeatures", [window, probe.slot(initial)], side=1)
    probe.scalar("WMIsWindowCollapsed", [window], 0)
    probe.scalar("WMIsWindowCollapsed", [0], 0)
    probe.scalar("WMCollapseWindow", [0, 1], -5600)
    probe.call("SetWindowFeatures", [window, 0], side=1)
    probe.scalar("WMCollapseWindow", [window, 1], -30580)
    probe.call("SetWindowFeatures", [window, 4], side=1)
    for side in (0, 1):
        probe.call("WMCollapseAllWindows", [1], side=side)
        p.expect(0, "collapse all visible windows")
        probe.call("WMIsWindowCollapsed", [window], side=1 - side)
        p.expect(0, "collapsing all leaves a hidden window untouched")
        probe.call("WMCollapseAllWindows", [0], side=side)
        p.expect(0, "expand the visible windows again")
        probe.call("WMCollapseWindow", [window, 1], side=side)
        p.expect(0, "collapse a hidden eligible window")
        probe.call("WMIsWindowCollapsed", [window], side=1 - side)
        p.expect(1, "the other manager reads the collapsed state")
        probe.call("WMCollapseWindow", [window, 0], side=1 - side)
        p.expect(0, "expand the hidden window through the other implementation")
    probe.call("SetWindowFeatures", [window, probe.slot(initial)], side=1)

    probe.call("WMGetWindowFeatures", [window, probe.ptr(original_features)], side=1)
    store(status)
    probe.call("GetWindowFeatures", [window, probe.ptr(features)])
    load(status, 4)
    p.equal("Appearance and WindowsLib return the same WDEF query result")
    load(features, 3)
    load(original_features, 4)
    p.equal("Appearance and WindowsLib share the WDEF feature bits")
    probe.call("SetWindowFeatures", [window, 4], side=1)
    probe.scalar("IsWindowCollapsable", [window], 1)
    probe.scalar("CollapseAllWindows", [1], 0)
    probe.scalar("IsWindowCollapsed", [window], 0)
    probe.scalar("CollapseWindow", [window, 1], 0)
    probe.scalar("WMIsWindowCollapsed", [window], 1)
    probe.scalar("WMCollapseWindow", [window, 0], 0)
    probe.scalar("IsWindowCollapsed", [window], 0)
    probe.call("SetWindowFeatures", [window, probe.slot(initial)], side=1)

    probe.scalar("HiliteWindowFrameForDrag", [0, 1], -5600)
    probe.call("SetWindowFeatures", [window, 0x80], side=1)
    for side in (0, 1):
        probe.call("GetPort", [probe.ptr(source)])
        probe.call("HiliteWindowFrameForDrag", [window, 1], side=side)
        p.expect(0, "enable the drag highlight on an eligible frame")
        probe.call("HiliteWindowFrameForDrag", [window, 0], side=1 - side)
        p.expect(0, "clear the saved drag highlight through the other entry")
        probe.call("GetPort", [probe.ptr(target)])
        load(target, 3)
        load(source, 4)
        p.equal("drawing the drag highlight restores the caller's port")
    probe.call("SetWindowFeatures", [window, probe.slot(initial)], side=1)

    probe.call("NewRgn")
    p.nonzero("allocate the window-region result")
    store(source)
    probe.scalar("WMGetWindowRegion", [0, 1, probe.slot(source)], -50)
    probe.scalar("WMGetWindowRegion", [window, 1, 0], -50)
    probe.call("SetWindowFeatures", [window, 0], side=1)
    probe.scalar("WMGetWindowRegion", [window, 1, probe.slot(source)], -30580)
    probe.call("SetWindowFeatures", [window, 0x10], side=1)
    probe.scalar("WMGetWindowRegion", [window, 1, probe.slot(source)])
    probe.call("SetWindowFeatures", [window, probe.slot(initial)], side=1)
    probe.call("DisposeRgn", [probe.slot(source)])

    probe.call("NewRgn")
    p.nonzero("allocate a region to erase through the saved window painter")
    store(source)
    paint_region = probe.slot(source)
    rect = probe.far_raw(struct.pack(">4h", 1, 1, 3, 4))
    probe.call("RectRgn", [paint_region, rect])
    probe.scalar("InstallWindowContentPaintProc", [0, 0, 0, 0], -5600)
    probe.scalar("InstallWindowContentPaintProc", [window, 0, 1, 0], -50)
    probe.scalar("EraseWindowRegion", [0, paint_region], -50)
    probe.scalar("EraseWindowRegion", [window, 0], -50)
    paint_results = probe.far_raw(bytes(8))
    paint_callback = probe.far_raw(bytes(8))
    p.routines.append(
        (
            paint_callback[1],
            [0x90A80000, 0x81280004, 0x39290001, 0x91280004, 0x3860FD66, 0x4E800020],
        )
    )
    probe.call("NewWindowPaintUPP", [paint_callback])
    p.nonzero("build the window paint routine descriptor")
    store(features)
    probe.call("GetPort", [probe.ptr(target)])
    probe.call("SetPort", [window])
    for side in (0, 1):
        probe.call("InstallWindowContentPaintProc", [window, probe.slot(features), 0, paint_results], side=side)
        p.expect(0, "install the current window's client paint callback")
        probe.call("EraseWindowRegion", [window, paint_region], side=1 - side)
        p.expect(0, "either erase implementation uses the installed client")
        probe.arguments([paint_results, window], 0)
        p.emit(0x80630000)
        p.equal("paint callback receives the window and the caller's refCon")
        probe.call("InstallWindowContentPaintProc", [window, 0, 0, 0], side=1 - side)
        p.expect(0, "remove the shared window paint callback")
        probe.call("EraseWindowRegion", [window, paint_region], side=side)
        p.expect(0, "erasing after removal uses the normal system paint path")
        probe.arguments([paint_results], 0)
        p.emit(0x80630004)
        p.expect(side + 1, "removing the painter stops its callbacks")
    probe.call("SetPort", [probe.slot(target)])
    probe.call("DisposeWindowPaintUPP", [probe.slot(features)])
    probe.call("DisposeRgn", [paint_region])

    for point in (0x00060030, 0x00370046, 0x01F401F4):
        probe.call("GetPort", [probe.ptr(source)])
        probe.call("GlobalFindWindow", [point, probe.ptr(features)])
        store(status)
        probe.call("GlobalFindWindow", [point, probe.ptr(original_features)], side=1)
        load(status, 4)
        p.equal("both managers return the same part for a global point")
        load(features, 3)
        load(original_features, 4)
        p.equal("both managers identify the same window at that point")
        probe.call("GetPort", [probe.ptr(target)])
        load(source, 3)
        load(target, 4)
        p.equal("global hit testing restores the caller's port")

    original_bounds, native_position, saved_position = (probe.far_raw(bytes(8)) for _ in range(3))
    probe.call("GetWindowBounds", [window, 33, original_bounds])
    p.expect(0, "save the position before testing screen placement")
    probe.scalar("RepositionWindow", [window, 0, 0], -50)
    probe.scalar("RepositionWindow", [window, window, 1], -50)
    probe.scalar("RepositionWindow", [window, 0, 2], -50)
    for side, result in ((0, native_position), (1, saved_position)):
        probe.call("RepositionWindow", [window, 0, 1], side=side)
        p.expect(0, "center the window on the current main screen")
        probe.call("GetWindowBounds", [window, 33, result])
        p.expect(0, "read back the centered window bounds")
        probe.call("SetWindowBounds", [window, 33, original_bounds])
        p.expect(0, "restore the original window geometry")
    probe.arguments([native_position, saved_position], 0)
    p.emit(0x80630000, 0x80840000)
    p.equal("the native and original centered positions match")

    probe.call("GetPort", [probe.ptr(source)])
    for side in (0, 1):
        probe.call("RecalculateAllWindowTitleWidths", [], side=side)
        probe.arguments([window], 0)
        p.emit(0xA063008A)
        if side == 0:
            store(status)
        else:
            load(status, 4)
            p.equal("both title walks leave the same cached width")
        probe.call("SetPort", [probe.slot(source)])
        probe.call("ReshapeWindows", [], side=side)
        probe.call("GetGrayRgn")
        p.nonzero("reshaping keeps the live desktop region")
        store(target)
        probe.call("EmptyRgn", [probe.slot(target)])
        p.expect(0, "the reshaped desktop still contains the main display")

    for kind, v, h, modifiers, expected in (
        (0, 55, 70, 0x100, 0),
        (1, 55, 70, 0, 0),
        (1, 0, 0, 0x100, 0),
        (1, 55, 70, 0x100, None),
    ):
        event = probe.far_raw(struct.pack(">HIIhhH", kind, 0, 0, v, h, modifiers))
        probe.scalar("IsWindowPathSelectClick", [window, event], expected)

    probe.call("NewPixPat")
    p.nonzero("allocate a window pattern output PixPat")
    store(target)
    probe.scalar("GetWindowContentPattern", [window, probe.slot(target)], -5604)
    for setter, marker in ((0, 0xA5A5), (1, 0x5A5A)):
        probe.call("NewPixPat")
        p.nonzero("allocate a window content PixPat")
        store(source)
        p.emit(0x80830000, 0x38600000 | marker, 0xB0640014)
        probe.call("SetWindowContentPattern", [window, probe.slot(source)], side=setter)
        p.expect(0, "window keeps a copy of the caller's pattern")
        probe.call("DisposePixPat", [probe.slot(source)])
        probe.call("GetWindowContentPattern", [window, probe.slot(target)], side=1 - setter)
        p.expect(0, "the stored window pattern survives caller disposal")
        load(target, 3)
        p.emit(0x80630000, 0xA0630014)
        p.expect(marker, "the saved pattern retains its original bytes")
        probe.call("WindowManagerSetThemeWindowBackground", [window, 1, 0], side=1 - setter)
        p.expect(0, "switch the window from a PixPat to a theme background brush")
        probe.call("GetWindowContentPattern", [window, probe.slot(target)], side=setter)
        p.expect(-5604, "a theme background removes the window's stored PixPat")
    probe.call("DisposePixPat", [probe.slot(target)])
    window_queue_operations(probe, window, base)
    window_proxy_operations(probe, window, base)
    desktop_window_operations(probe, base)
    monitor_desktop_operations(probe, base)


def control_manager_operations(probe):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    _, base = probe.raw(bytes(396))
    window, pane, result, value = (slot(base + offset) for offset in (0, 4, 8, 12))
    out, count, data = (ptr(base + offset) for offset in (8, 16, 20))
    bounds = probe.raw(struct.pack(">4h", 65, 40, 135, 170))
    pane_bounds = probe.raw(struct.pack(">4h", 5, 8, 30, 90))
    title = probe.raw(b"\x04Pane")

    def call(name, args=(), expected=0):
        probe.call(name, args)
        p.expect(expected, name + " returns the expected Control Manager result")

    def save(offset):
        p.emit(0x907E0000 | (base + offset))

    def word(offset, expected, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(expected, label)

    def match(offset, label):
        p.emit(0x807E0000 | (base + offset), 0x809E0000 | (base + 4))
        p.equal(label)

    probe.call("IsScreenBitSavingEnabled")
    save(92)
    for enabled in (0, 1):
        call("SetScreenBitSavingEnabled", [enabled])
        call("IsScreenBitSavingEnabled", expected=enabled)
    call("SetScreenBitSavingEnabled", [slot(base + 92)])
    call("GetSavedScreenBitsRepresentativePixMap", [0])
    probe.call("MoveSavedScreenBits", [0, 17, 29])
    capture_bounds = probe.far_raw(struct.pack(">4h", 40, 40, 48, 48))
    call("SetScreenBitSavingEnabled", [0])
    probe.scalar("SaveScreenBits", [capture_bounds, 0, ptr(base + 96)], -108)
    probe.scalar("RestoreScreenBits", [0, 0], 0)
    for side in (0, 1):
        probe.call("DisposeSavedScreenBits", [0], side=side)
        call("SetScreenBitSavingEnabled", [1])
        probe.call("SaveScreenBits", [capture_bounds, 0, ptr(base + 96)], side=side)
        p.expect(0, "the saved manager captures an on-screen rectangle")
        p.emit(0x807E0000 | (base + 96))
        if side == 0:
            save(100)
        else:
            p.emit(0x809E0000 | (base + 100))
            p.equal("disposing screen bits releases the saved manager's reusable slot")
        probe.call("GetSavedScreenBitsRepresentativePixMap", [slot(base + 96)])
        p.nonzero("the captured screen pixels have an owned pixmap")
        probe.call("GetGWorld", [ptr(base + 104), ptr(base + 108)])
        probe.call("RestoreScreenBits", [slot(base + 96), 0], side=1 - side)
        p.expect(0, "restore the other implementation's screen capture")
        probe.call("GetGWorld", [ptr(base + 112), ptr(base + 116)])
        for original, restored in ((104, 112), (108, 116)):
            p.emit(0x807E0000 | (base + original), 0x809E0000 | (base + restored))
            p.equal("restoring screen bits preserves the caller's port and device")
        probe.call("DisposeSavedScreenBits", [slot(base + 96)], side=side)
    call("SetScreenBitSavingEnabled", [slot(base + 92)])

    probe.call("NewWindow", [0, bounds, title, 0, 0, -1, 1, 0])
    p.nonzero("create a window for pane and bounds checks")
    save(0)
    window_metadata_operations(probe, window, base + 96)
    call("GetWindowBounds", [window, 40, data])
    word(20, 0x00410028, "global port origin of a hidden window")
    word(24, 0x008700AA, "global port area of a hidden window")
    call("GetWindowBounds", [window, 33, data])
    word(20, 0x00410028, "hidden content region is calculated")
    word(24, 0x008700AA, "hidden content area")
    call("GetWindowBounds", [window, 99, data], -30593)
    call("GetWindowBounds", [0, 40, data], -5600)
    probe.call("NewRgn")
    p.nonzero("allocate a caller-owned window region")
    save(12)
    call("GetWindowRegion", [window, 40, value])
    probe.call("GetRegionBounds", [value, data])
    word(20, 0x00410028, "window region uses global coordinates")
    word(24, 0x008700AA, "window region matches the native bounds")
    probe.call("DisposeRgn", [value])

    probe.call("NewControl", [window, pane_bounds, title, 1, 28, 0, 28, 256, 1234])
    p.nonzero("create a live user pane")
    save(4)
    call("CopyControlTitleAsCFString", [pane, 0], -50)
    call("CopyControlTitleAsCFString", [0, out], -30599)
    call("SetControlTitleWithCFString", [0, 0], -30599)
    for encoding, character in ((0x10000, 0x00A4), (0x20000, 0x20AC)):
        probe.call("CFStringCreateWithPascalString", [0, probe.raw(b"\x01\xdb"), encoding])
        p.nonzero("create a string with an explicit Roman variant")
        save(104)
        probe.call("CFStringGetCharacterAtIndex", [slot(base + 104), 0])
        p.expect(character, "the Roman variant selects the currency or euro character")
        probe.call("CFRelease", [slot(base + 104)])
    for setter, text in ((0, b"\x04Able"), (1, b"\x04Caf\x8e")):
        probe.call("CFStringCreateWithPascalString", [0, probe.raw(text), 0])
        p.nonzero("allocate a Core Foundation control title")
        save(104)
        if setter or probe.original_only:
            probe.call("SetControlTitle", [pane, probe.raw(text)])
        else:
            probe.call("SetControlTitleWithCFString", [pane, slot(base + 104)])
            p.expect(0, "set the classic control title from a CFString")
        probe.call("GetControlTitle", [pane, ptr(base + 140)])
        word(140, int.from_bytes(text[:4], "big"), "the classic control record stores the updated title")
        probe.call("CopyControlTitleAsCFString", [pane, ptr(base + 108)], side=1 - setter)
        p.expect(0, "copy the classic control title as a CFString")
        probe.call("CFStringGetLength", [slot(base + 108)])
        p.expect(text[0], "the returned CFString has the Pascal title's length")
        probe.call("CFStringGetCharacterAtIndex", [slot(base + 108), 3])
        p.expect(ord(text[-1:].decode("mac_roman")), "the returned CFString decodes the final Macintosh byte")
        if not probe.original_only:
            p.emit(0x38605A5A, 0x907E0000 | (base + 116))
            probe.call(
                "GetTextAndEncodingFromCFString",
                [slot(base + 108), ptr(base + 112), 4, ptr(base + 120), ptr(base + 124)],
            )
            p.expect(0, "encode the Core Foundation title as one text run")
            word(112, int.from_bytes(text[1:], "big"), "single-run encoding writes the title without a prefix")
            word(116, 0x5A5A, "text conversion leaves the output guard intact")
            word(120, text[0], "text conversion reports the byte count")
            word(124, 0, "text conversion uses the local Roman system encoding")
            p.emit(0x3C605A5A, 0x60635A5A, 0x907E0000 | (base + 112))
            call(
                "GetTextAndEncodingFromCFString",
                [slot(base + 108), ptr(base + 112), 2, ptr(base + 120), ptr(base + 124)],
                -8785,
            )
            word(112, int.from_bytes(text[1:3] + b"\x5a\x5a", "big"), "a short destination holds only the prefix")
            word(120, 2, "a short destination reports the bytes actually written")
        call("GetTextAndEncodingFromCFString", [0, 0, 0, ptr(base + 120), ptr(base + 124)])
        word(120, 0, "a missing CFString reports no text bytes")
        probe.call("CFRelease", [slot(base + 108)])
        probe.call("CFRelease", [slot(base + 104)])
    call("IsValidControlHandle", [pane], 1)
    call("IsControlVisible", [pane], 1)
    call("IsControlActive", [pane], 1)
    call("GetControlClickActivation", [0, 0, 0, count], -50)
    call("GetControlClickActivation", [pane, 0, 0, 0], -50)
    call("GetControlClickActivation", [pane, 0x0008000C, 0x200, count])
    word(16, 2, "the pane uses the default click activation policy")
    call("HandleControlSetCursor", [0, 0, 0, count], -50)
    call("HandleControlSetCursor", [pane, 0, 0, 0], -50)
    call("HandleControlSetCursor", [pane, 0x0008000C, 0x200, count])
    p.emit(0x887E0000 | (base + 16))
    p.expect(0, "the pane leaves the caller responsible for the cursor")
    call("HandleControlContextualMenuClick", [0, 0, count], -50)
    call("HandleControlContextualMenuClick", [pane, 0, 0], -50)
    p.emit(0x3C60A55A, 0x6063A55A, 0x907E0000 | (base + 16))
    call("HandleControlContextualMenuClick", [pane, 0x0008000C, count])
    word(16, 0x005AA55A, "a pane without contextual menus clears only the result byte")
    call("GetControlFeatures", [pane, count])
    word(16, 0x5C, "pane advertises the installed Appearance capabilities")
    call("CountSubControls", [pane, count], -30586)
    word(16, 0x5C, "querying a window with no root preserves the count output")
    call("GetBestControlRect", [pane, data, 0], -50)
    call("GetBestControlRect", [pane, data, count])
    word(20, 0x00050008, "the best-rect fallback uses the control's top and left")
    word(24, 0x001E005A, "the best-rect fallback preserves the control's extent")
    p.emit(0xA07E0000 | (base + 16))
    p.expect(0, "the best-rect fallback gives a zero baseline offset")
    call("GetControl32BitValue", [0], 0)
    for field in ("Value", "Minimum", "Maximum"):
        probe.call("GetControl" + field, [pane])
        save(32)
        probe.call("GetControl32Bit" + field, [pane])
        p.emit(0x809E0000 | (base + 32))
        p.equal("the initial 32-bit control range agrees with the live classic field " + field)
    call("GetControlViewSize", [pane], 0)
    probe.call("SetControl32BitMinimum", [pane, -100000])
    probe.call("SetControl32BitMaximum", [pane, 120000])
    probe.call("SetControl32BitValue", [pane, 40000])
    call("GetControl32BitMinimum", [pane], -100000)
    call("GetControl32BitMaximum", [pane], 120000)
    call("GetControl32BitValue", [pane], 40000)
    call("GetControlValue", [pane], 40000 & 0x7FFF)
    probe.call("SetControlViewSize", [pane, 51000])
    call("GetControlViewSize", [pane], 51000)
    probe.call("SetControl32BitValue", [pane, -120000])
    call("GetControl32BitValue", [pane], -100000)
    probe.call("SetControl32BitValue", [pane, 200000])
    call("GetControl32BitValue", [pane], 120000)
    probe.call("SetControlValue", [pane, 11])
    call("GetControl32BitValue", [pane], 11)
    # Restore the pane's feature field before testing its callbacks.
    probe.call("SetControl32BitMinimum", [pane, 0])
    probe.call("SetControl32BitMaximum", [pane, 28])
    probe.call("SetControl32BitValue", [pane, 28])
    call("GetControlReference", [pane], 1234)
    probe.call("SetControlReference", [pane, -77])
    call("GetControlReference", [pane], -77)
    identifier = probe.raw(struct.pack(">Ii", 0x54455354, -12))
    call("SetControlID", [pane, identifier])
    call("GetControlID", [pane, data])
    word(20, 0x54455354, "control ID signature remains a guest FourCC")
    word(24, -12, "control identifier preserves the signed ID")
    call("GetControlByID", [window, identifier, out])
    match(8, "window lookup locates the same live control handle")
    call("SetControlCommandID", [pane, 0x50414E45])
    call("GetControlCommandID", [pane, count])
    word(16, 0x50414E45, "control command ID is independent of the control ID")

    for offset, tag, callback in (
        (64, 0x69646C65, [0x80620000 | (base + 32), 0x38630001, 0x90620000 | (base + 32), 0x4E800020]),
        (72, 0x61637469, [0x90820000 | (base + 36), 0x4E800020]),
        (80, 0x666F6369, [0x90820000 | (base + 40), 0x7C832378, 0x4E800020]),
    ):
        p.routines.append((base + offset, callback))
        p.emit(0x387E0000 | (base + offset))
        save(12)
        call("SetControlData", [pane, 0, tag, 4, ptr(base + 12)])
        call("GetControlDataSize", [pane, 0, tag, count])
        word(16, 4, "pane procedure data is one guest longword")
        call("GetControlData", [pane, 0, tag, 4, data, count])
        p.emit(0x807E0000 | (base + 20))
        p.pointer(base + offset, "pane data preserves the installed procedure descriptor")
        p.emit(0x3C60A531, 0x60637EC4)  # lis/ori r3, 0xA5317EC4
        save(20)
        p.emit(0x3860FFFF)  # li r3, -1
        save(16)
        call("GetControlData", [pane, 0, tag, 2, data, count], -30591)
        word(20, 0xA5317EC4, "short pane procedure reads leave the destination intact")
        word(16, 4, "short pane procedure reads report the complete size")
    p.emit(0x38600000)  # The range comparison used the callback counter as scratch.
    save(32)
    call("IdleControls", [window])
    word(32, 1, "IdleControls invokes the registered pane callback")
    call("SetKeyboardFocus", [window, pane, 9])
    word(40, 9, "the focus callback receives exactly the requested part")
    call("GetKeyboardFocus", [window, out])
    match(8, "the window owns the pane focus")
    call("DeactivateControl", [pane])
    call("IsControlActive", [pane])
    word(40, 0, "deactivation clears the focused pane part")
    word(36, 0, "the activation callback receives deactivation")
    call("ActivateControl", [pane])
    call("IsControlActive", [pane], 1)
    word(36, 1, "activation uses a one-byte Boolean argument")
    call("SetKeyboardFocus", [window, pane, 11])
    call("ClearKeyboardFocus", [window])
    call("GetKeyboardFocus", [window, out])
    word(8, 0, "clearing focus publishes a null control handle")
    call("AdvanceKeyboardFocus", [window], -30586)
    call("CreateRootControl", [window, out])
    call("EmbedControl", [pane, result])
    call("AdvanceKeyboardFocus", [window])
    word(40, -1, "advancing focus asks the pane for its next part")
    call("GetKeyboardFocus", [window, out])
    match(8, "the pane becomes the focused child of the root")
    call("ReverseKeyboardFocus", [window])
    word(40, -2, "reversing focus asks the current pane for its previous part")
    probe.call("DisposeControl", [pane])
    call("IsValidControlHandle", [pane])
    probe.call("ShowWindow", [window])
    scroll_bounds = probe.far_raw(struct.pack(">4h", 44, 8, 60, 208))
    probe.call("NewControl", [window, scroll_bounds, title, 1, 0, 0, 100, 16, 0])
    p.nonzero("create a visible scrollbar for view-size checks")
    save(4)
    probe.call("SetControl32BitMaximum", [pane, 200000])
    point = (52 << 16) | 60
    call("TestControl", [pane, point], 23)  # Page down outside the classic fixed-size thumb.
    probe.call("SetControlViewSize", [pane, 100000])
    call("TestControl", [pane, point], 129)  # The proportional thumb covers the same point.
    probe.call("NewRgn")
    p.nonzero("allocate a caller-owned control region")
    save(12)
    call("GetControlRegion", [pane, 0, value], -30588)
    call("GetControlRegion", [pane, 129, 0], -50)
    call("GetControlRegion", [pane, 129, value])
    probe.call("GetRegionBounds", [value, data])
    word(20, 0x002D0018, "the thumb region agrees with the left edge of the proportional thumb")
    word(24, 0x003B0050, "the thumb region returns the full proportional extent")
    probe.call("DisposeControl", [pane])
    probe.call("NewControl", [window, pane_bounds, title, 0, 0, 0, 0, 16 * 182, 0])
    p.nonzero("create a control with an extended 68K definition")
    save(4)
    call("GetBestControlRect", [pane, data, count])
    word(20, 0x00050008, "the extended size keeps the control's origin")
    word(24, 0x00300039, "the definition provides height and width")
    p.emit(0xA07E0000 | (base + 16))
    p.expect(7, "the definition supplies the baseline offset")
    call("GetControlRegion", [pane, 15, value])
    probe.call("GetRegionBounds", [value, data])
    word(20, 0x00010002, "the extended region message receives the caller's region handle")
    word(24, 0x00030004, "the definition writes the requested control part's region")
    p.emit(0x3C60A55A, 0x6063A55A, 0x907E0000 | (base + 16))
    call("HandleControlContextualMenuClick", [pane, 0x0008000C, count])
    word(16, 0x015AA55A, "a contextual-menu CDEF sets the displayed flag")
    probe.call("DisposeRgn", [value])
    probe.call("DisposeControl", [pane])
    probe.call("DisposeWindow", [window])

    edit = struct.pack(">I4hBB2s", 0, 8, 8, 28, 100, 16, 2, b"ok")
    edit2 = struct.pack(">I4hBB2s", 0, 31, 8, 51, 100, 16, 2, b"go")
    template = struct.pack(">H", 1) + edit + edit2
    probe.call("NewHandle", [len(template)])
    p.nonzero("allocate the edit dialog items")
    save(12)
    p.emit(0x80630000)
    save(28)
    probe.call("BlockMoveData", [probe.raw(template), slot(base + 28), len(template)])
    probe.call("NewDialog", [0, bounds, title, 1, 0, -1, 1, 0, value])
    p.nonzero("create a classic dialog with two edit fields")
    save(0)
    call("CountDITL", [window], 2)
    call("GetDialogItemAsControl", [window, 2, ptr(base + 4)])
    button = struct.pack(">I4hBB2s", 0, 45, 8, 62, 70, 4, 2, b"OK")
    appended = struct.pack(">H", 1) + edit + button
    probe.call("NewHandle", [len(appended)])
    p.nonzero("allocate the DITL to append")
    save(12)
    p.emit(0x80630000)
    save(28)
    probe.call("BlockMoveData", [probe.raw(appended), slot(base + 28), len(appended)])
    probe.call("AppendDITL", [window, value, 0])
    probe.call("DisposeHandle", [value])
    call("CountDITL", [window], 4)
    call("GetDialogItemAsControl", [window, 3, out])
    p.emit(0x807E0000 | (base + 8))
    p.nonzero("AppendDITL creates the additional edit field")
    call("SetKeyboardFocus", [window, pane, 5])
    call("GetKeyboardFocus", [window, out])
    match(8, "Carbon focus selects the live second DITL edit item")
    probe.call("GetDialogTextEditHandle", [window])
    p.nonzero("get the live TextEdit record for the focused dialog")
    save(28)
    p.emit(0x80830000, 0xA8A40000, 0xA8C4001A, 0x7CA53214, 0xB0BE0000 | (base + 20))
    p.emit(0xA0A40002, 0xB0BE0000 | (base + 22), 0x38A0FFF9, 0xB0A40002)
    probe.call("TEGetPoint", [ptr(base + 24), 0, slot(base + 28)])
    p.emit(0xA07E0000 | (base + 24), 0xA09E0000 | (base + 20))
    p.equal("TEGetPoint reports the baseline without borrowing from a negative horizontal position")
    p.emit(0xA07E0000 | (base + 26))
    p.expect(0xFFF9, "TEGetPoint writes a signed horizontal coordinate through the CFM result pointer")
    p.emit(0x809E0000 | (base + 28), 0x80840000, 0xA0BE0000 | (base + 22), 0xB0A40002)
    call("ClearKeyboardFocus", [window])
    call("GetKeyboardFocus", [window, out])
    word(8, 0, "classic dialog TextEdit focus clears through Carbon")
    call("MoveDialogItem", [window, 2, 17, 21])
    call("SizeDialogItem", [window, 2, 47, 12])
    probe.call("GetDialogItem", [window, 2, ptr(base + 32), ptr(base + 36), ptr(base + 40)])
    word(40, 0x00150011, "moving an item updates its DITL origin")
    word(44, 0x00210040, "resizing an item keeps its origin and updates its extent")
    call("SizeDialogItem", [window, 2, -1, 12], -50)
    call("MoveDialogItem", [window, 0, 1, 1], -50)
    probe.call("HideDialogItem", [window, 2])
    call("MoveDialogItem", [window, 2, 20, 30])
    probe.call("GetDialogItem", [window, 2, ptr(base + 32), ptr(base + 36), ptr(base + 40)])
    word(40, 0x001E4014, "moving a hidden item preserves its DITL visibility")
    probe.call("ShowDialogItem", [window, 2])
    probe.call("GetDialogItem", [window, 2, ptr(base + 32), ptr(base + 36), ptr(base + 40)])
    word(40, 0x001E0014, "showing the hidden item restores the moved origin")
    word(44, 0x002A0043, "moving an item preserves its resized extent")
    call("GetDialogItemAsControl", [window, 4, out])
    call("MoveDialogItem", [window, 4, 12, 42])
    call("SizeDialogItem", [window, 4, 51, 19])
    probe.call("GetControlBounds", [result, ptr(base + 40)])
    word(40, 0x002A000C, "moving a dialog button also moves its live control")
    word(44, 0x003D003F, "resizing a dialog button updates its live control extent")
    probe.call("SetDialogDefaultItem", [window, 4])
    return_event = probe.raw(struct.pack(">HIIhhh", 3, 13, 0, 0, 0, 0))
    null_event = probe.raw(bytes(16))
    call("StdFilterProc", [window, null_event, count])
    call("StdFilterProc", [window, return_event, count], 255)
    p.emit(0xA07E0000 | (base + 16))
    p.expect(4, "the default filter returns the active dialog button's item number")

    call("SetDialogCancelItem", [window, 4])
    call("GetDialogCancelItem", [window], 4)
    call("SetDialogCancelItem", [window, 5], -50)
    call("SetDialogTracksCursor", [window, 1])
    call("StdFilterProc", [window, null_event, count])
    call("SetDialogTracksCursor", [window, 0])
    for key, modifiers, handled in ((27, 0, 255), (46, 256, 255), (46, 0, 0)):
        event = probe.raw(struct.pack(">HIIhhh", 3, key, 0, 0, 0, modifiers))
        call("StdFilterProc", [window, event, count], handled)
        if handled:
            p.emit(0xA07E0000 | (base + 16))
            p.expect(4, "the filter returns the stored cancel button on a cancel shortcut")
    call("SetDialogCancelItem", [window, 0])
    call("GetDialogCancelItem", [window])
    probe.call("ShortenDITL", [window, 2])
    call("CountDITL", [window], 2)

    call("InsertDialogItem", [window, -1, 0, 0, pane_bounds], -50)
    call("InsertDialogItem", [window, 0, 0, 0, 0], -50)
    call("InsertDialogItem", [window, 1, 128, 0, pane_bounds])
    call("CountDITL", [window], 3)
    probe.call("GetDialogItem", [window, 2, ptr(base + 32), ptr(base + 36), ptr(base + 40)])
    p.emit(0xA07E0000 | (base + 32))
    p.expect(128, "insertion places the packed user item after the requested index")
    word(40, 0x00050008, "insertion copies the item origin")
    word(44, 0x001E005A, "insertion copies the item extent")
    probe.call("GetDialogItem", [window, 3, ptr(base + 32), ptr(base + 36), ptr(base + 40)])
    word(40, 0x001E0014, "insertion shifts the existing variable-length item")
    call("InsertDialogItem", [window, 100, 0, 0, pane_bounds])
    call("CountDITL", [window], 4)
    probe.call("GetDialogItem", [window, 4, ptr(base + 32), ptr(base + 36), ptr(base + 40)])
    word(40, 0x00050008, "an index beyond the end appends the item")
    call("RemoveDialogItems", [window, 0, 1, 0], -50)
    call("RemoveDialogItems", [window, 2, 0, 0], -50)
    call("RemoveDialogItems", [window, 5, 1, 1], -50)
    call("RemoveDialogItems", [window, 2, 1, 0])
    call("CountDITL", [window], 3)
    probe.call("GetDialogItem", [window, 2, ptr(base + 32), ptr(base + 36), ptr(base + 40)])
    word(40, 0x001E0014, "removing a middle item shifts the next item into its place")
    call("RemoveDialogItems", [window, 3, 20, 1])
    call("CountDITL", [window], 2)
    probe.call("DisposeDialog", [window])

    _, saved_params = probe.far_raw(bytes(4 * 256))
    saved = [ptr(saved_params + i * 256) for i in range(4)]
    probe.call("GetParamText", saved)
    probe.call("ParamText", [probe.raw(b"\x03abc"), probe.raw(b"\x00"), probe.raw(b"\x01z"), probe.raw(b"\x02xy")])
    probe.call("GetParamText", [ptr(base + 48), ptr(base + 52), 0, ptr(base + 56)])
    word(48, 0x03616263, "GetParamText returns the first Pascal substitution string")
    p.emit(0x887E0000 | (base + 52))
    p.expect(0, "GetParamText writes a zero length for an empty substitution")
    p.emit(0x807E0000 | (base + 56), 0x5463C23E)
    p.expect(0x027879, "GetParamText copies the fourth substitution across an omitted output")
    probe.call("ParamText", saved)


def color_sync_operations(probe):
    p = probe.p
    base = 0x2000
    source, output = probe.ptr(base), probe.ptr(base + 32)

    def store(offset, *words):
        for index, word in enumerate(words):
            probe.arguments([word], 0)
            p.emit(0x907E0000 | (base + offset + index * 4))

    def check(offset, *words):
        for index, word in enumerate(words):
            p.emit(0x807E0000 | (base + offset + index * 4))
            p.expect(word, "ColorSync output bytes including untouched padding")

    def convert(name, src, dst, count, status=0):
        probe.call(name, [src, dst, count])
        p.expect(status, name + " reports conversion status")

    store(32, 0xFFFFFFFF, 0xDEADBEEF)
    probe.call("CMGetColorSyncVersion", [0])
    p.expect(-50, "ColorSync version requires an output location")
    probe.call("CMGetColorSyncVersion", [output])
    p.expect(0, "query the ColorSync version")
    check(32, 0x0304, 0xDEADBEEF)

    volume, directory = output, probe.ptr(base + 36)
    probe.call("CMGetColorSyncFolderSpec", [-32768, 0, volume, 0])
    p.expect(-50, "ColorSync folder requires both output locations")
    probe.call("CMGetColorSyncFolderSpec", [-32768, 1, volume, directory])
    p.expect(0, "create or find the system ColorSync Profiles directory")
    probe.call("CMGetColorSyncFolderSpec", [-32768, 0, source, probe.ptr(base + 4)])
    p.expect(0, "reopen the same ColorSync Profiles directory")
    p.emit(0xA07E0000 | base, 0xA09E0000 | (base + 32))
    p.equal("both ColorSync folder calls return the same volume")
    p.emit(0x807E0000 | (base + 4), 0x809E0000 | (base + 36))
    p.equal("both ColorSync folder calls return the same directory ID")

    for name in (
        "CMConvertRGBToGray",
        "CMConvertXYZToFixedXYZ",
        "CMConvertFixedXYZToXYZ",
        "CMConvertXYZToYxy",
        "CMConvertYxyToXYZ",
    ):
        convert(name, 0, 0, 0)
        convert(name, 0, output, 1, -50)
        convert(name, source, output, 0xFFFFFFFF, -50)

    store(0, 0xFFFF0000, 0x00007777, 0x0000FFFF, 0x00006666, 0, 0xFFFF5555)
    convert("CMConvertRGBToGray", source, source, 3)
    check(0, 0x4D000000, 0x00007777, 0x96FFFFFF, 0x00006666, 0x1C000000, 0xFFFF5555)

    store(0, 0x00010002, 0xFFFF8000, 0x7FFF0000)
    convert("CMConvertXYZToFixedXYZ", source, source, 2)
    check(0, 3, 4, 0x1FFFF, 0x10000, 0xFFFF, 0)
    convert("CMConvertFixedXYZToXYZ", source, source, 2)
    check(0, 0x00020002, 0x00008000, 0x80000000)
    # Distinct fields in the second source prove all components advance.
    store(0, 0xFFFFFFFF, 0x00020000, 5, 6, 7, 8)
    store(32, 0, 0, 0, 0xDEADC0DE)
    convert("CMConvertFixedXYZToXYZ", source, output, 2, -4214)
    check(32, 0xFFFFFFFF, 0x00030003, 0x00040004, 0xDEADC0DE)

    store(0, 0x20001000, 0x1000C0DE, 0x00008000, 0x0000BEEF)
    convert("CMConvertXYZToYxy", source, source, 2, -4214)
    check(0, 0x20008000, 0x4000C0DE, 0xFFFF0000, 0xFFFEBEEF)
    convert("CMConvertYxyToXYZ", source, source, 1)
    check(0, 0x20001000, 0x0FFFC0DE)
    store(0, 0, 0x0000BEEF)
    convert("CMConvertXYZToYxy", source, source, 1)
    check(0, 0, 0x0000BEEF)

    # Expected bytes from the ColorSync Extension, with a neutral XYZ white point.
    white_point = probe.ptr(base + 24)
    store(24, 0x80008000, 0x80000000)
    for name, use_white, first, second in (
        ("RGBToHSV", False, (0x6106AF40, 0xBC09, 0x85FF7BE8, 0xBC09), (0xFFAA2D1C, 0x0797, 0x0674F866, 0xFFAA)),
        ("HSVToRGB", False, (0x6E8123EF, 0x446A, 0x3AD0446A, 0x407A), (0xFFFFFFFF, 0xFFFF, 0xFFFF0001, 0x0007)),
        ("RGBToHLS", False, (0xDEE3A7E2, 0xDFBD, 0xD4AFC3D0, 0x76C9), (0x9BAED8EF, 0x9937, 0x53AEB913, 0x72FE)),
        ("HLSToRGB", False, (0xA335BE37, 0xB387, 0x9015A037, 0xEC59), (0x8A7E9357, 0x376E, 0x7BD09F4C, 0xAADE)),
        ("XYZToLab", True, (0, 0, 0x00008000, 0x8000), (0x1AAA951D, 0xEC8C, 0xFF000000, 0x5D02)),
        ("LabToXYZ", True, (0xB1720FE2, 0xD536, 0x11563367, 0x03E0), (0xFFFFFFFF, 0xFFFF, 0xFFA200DC, 0x0624)),
        ("XYZToLuv", True, (0x899F6323, 0x60E6, 0xE6D8DA7B, 0x76DC), (0xFFFFFFFF, 0xFFFF, 0xFF000000, 0)),
        ("LuvToXYZ", True, (0, 0, 0xA5A50000, 0), (0x3BE9AA98, 0x56C2, 0x0BBC050D, 0x0F91)),
    ):
        symbol = "CMConvert" + name
        store(0, first[0], first[1] << 16, second[0], second[1] << 16)
        store(32, 0xA5A5A5A5, 0xA5A5BEEF, 0xA5A5A5A5, 0xA5A5C0DE)
        args = [source, white_point, output, 2] if use_white else [source, output, 2]
        probe.call(symbol, args)
        p.expect(-4214 if name in ("XYZToLab", "XYZToLuv") else 0, symbol + " array status")
        check(32, first[2], (first[3] << 16) | 0xBEEF, second[2], (second[3] << 16) | 0xC0DE)


def data_browser_control_cases():
    source = (ROOT / "carbonlib/src/data_browser_control_entries.h").read_text()
    return [
        (name, int(tag, 16), int(size), transfer, int(required), int(first), int(second), int(insert_key))
        for name, tag, size, transfer, required, first, second, insert_key in re.findall(
            r"BROWSER_CONTROL\(\s*(\w+),\s+0x[0-9a-f]+,\s+(0x[0-9a-f]+),\s+(\d+),\s+browser_(\w+),"
            r"\s+(\d+),\s+(-?\d+),\s+(-?\d+),\s+([01])\s*\)",
            source,
        )
    ]


def icon_rgb_operations(probe):
    p = probe.p
    base = 0x2000
    family, input_pixels, output_pixels = (base + i * 4 for i in range(3))
    saved = probe.far_raw(bytes(12))
    probe.call("BlockMoveData", [probe.ptr(base), saved, 12])

    if not probe.original_only:
        probe.call("GetDeepIconsEnabled", [probe.ptr(base)])
        p.expect(0, "read the current deep icon setting")
        p.emit(0x887E0000 | base, 0x907E0000 | (base + 4), 0x68630001, 0x907E0000 | (base + 8))
        probe.call("SetDeepIconsEnabled", [probe.slot(base + 8)])
        p.expect(0, "change the deep icon setting")
        probe.call("GetDeepIconsEnabled", [probe.ptr(base)])
        p.expect(0, "read the updated deep icon setting")
        p.emit(0x887E0000 | base, 0x809E0000 | (base + 8))
        p.equal("the deep icon switch takes effect")
        probe.call("SetDeepIconsEnabled", [probe.slot(base + 4)])
        p.expect(0, "restore the original deep icon setting")

        probe.call("FindFolder", [-32768, 0x6D616373, 0, probe.ptr(base + 8), probe.ptr(base + 4)])
        p.expect(0, "find the System Folder volume for the custom icon setting")
        p.emit(0xA87E0000 | (base + 8), 0x907E0000 | (base + 8))
        probe.call("GetCustomIconsEnabled", [probe.slot(base + 8), probe.ptr(base)])
        p.expect(0, "read the current volume's custom icon setting")
        p.emit(0x887E0000 | base, 0x907E0000 | (base + 4), 0x68630001)
        p.emit(0x907E0000 | base)
        probe.call("SetCustomIconsEnabled", [probe.slot(base + 8), probe.slot(base)])
        p.expect(0, "set the custom icon preference for the mounted volume")
        probe.call("GetCustomIconsEnabled", [probe.slot(base + 8), probe.ptr(base + 3)])
        p.expect(0, "read the changed custom icon preference")
        p.emit(0x887E0000 | (base + 3), 0x809E0000 | (base + 4), 0x68840001)
        p.equal("the per-volume custom icon setting takes effect")
        probe.call("SetCustomIconsEnabled", [probe.slot(base + 8), probe.slot(base + 4)])
        p.expect(0, "restore the original custom icon setting")

        probe.call("GetIconDevice", [probe.ptr(base)])
        probe.call("GetMainDevice")
        p.nonzero("the display has a main QuickDraw device")
        p.emit(0x907E0000 | (base + 4))
        probe.call("SetIconDevice", [probe.slot(base + 4)])
        probe.call("GetIconDevice", [probe.ptr(base + 8)])
        p.emit(0x807E0000 | (base + 4), 0x809E0000 | (base + 8))
        p.equal("the icon device is read back through Icon Utilities")
        probe.call("SetIconDevice", [probe.slot(base)])

    def load(data, slot):
        source = probe.far_raw(data)
        probe.call("NewHandle", [len(data)])
        p.nonzero("allocate a classic icon data handle")
        p.emit(0x907E0000 | slot)
        if data:
            probe.arguments([source, probe.slot(slot), len(data)], 0)
            p.emit(0x80840000)
            p.call("BlockMoveData")

    def compare(expected, slot, label):
        probe.call("GetHandleSize", [probe.slot(slot)])
        p.expect(len(expected), label + " has the expected size")
        probe.arguments([probe.far_raw(expected), probe.slot(slot), len(expected)], 0)
        p.emit(0x80840000, 0x7CA903A6)
        p.emit(
            0x88C30000,  # Load the next expected and actual bytes.
            0x88E40000,
            0x7C063800,
            0x40820018,
            0x38630001,
            0x38840001,
            0x4200FFE8,
            0x38600001,
            0x48000008,
            0x38600000,
        )
        p.expect(1, label + " matches byte for byte")

    def packed_family(kind, pixels):
        return struct.pack(">4sI4sI", b"icns", 16 + len(pixels), kind, 8 + len(pixels)) + pixels

    load(b"", output_pixels)
    for kind, width in ((b"is32", 16), (b"il32", 32), (b"ih32", 48), (b"it32", 128)):
        count = width * width
        rgb = [(1, 7, 27), (2, 9, 29), (3, 11, 31)] + [(0x25, 0x80, 0xE1)] * (count - 3)
        raw = b"".join(bytes((0, *pixel)) for pixel in rgb)
        with_padding = b"".join(bytes((0xFF, *pixel)) for pixel in rgb)
        packed = bytearray(bytes(4) if width == 128 else b"")
        for channel in range(3):
            packed.extend((2, rgb[0][channel], rgb[1][channel], rgb[2][channel]))
            remaining = count - 3
            while remaining:
                run = min(130, remaining)
                packed.extend((125 + run, rgb[3][channel]) if run >= 3 else (run - 1, *([rgb[3][channel]] * run)))
                remaining -= run
        element = int.from_bytes(kind, "big")

        # First decode an independently constructed stream, then encode the
        # same full-size xRGB image into an empty family.
        load(packed_family(kind, packed), family)
        probe.call("GetIconFamilyData", [probe.slot(family), element, probe.slot(output_pixels)])
        p.expect(0, "decode the independently packed " + kind.decode())
        compare(raw, output_pixels, "decoded " + kind.decode())
        probe.call("DisposeHandle", [probe.slot(family)])
        load(b"", family)
        load(with_padding, input_pixels)
        probe.call("SetIconFamilyData", [probe.slot(family), element, probe.slot(input_pixels)])
        p.expect(0, "encode the xRGB " + kind.decode())
        compare(packed_family(kind, packed), family, "encoded " + kind.decode())
        probe.call("DisposeHandle", [probe.slot(input_pixels)])
        probe.call("DisposeHandle", [probe.slot(family)])

    rgb = bytes(range(256))
    raw = b"".join(bytes((0, i, i ^ 0xAA, 255 - i)) for i in rgb)
    packed = b"".join(bytes((i, i ^ 0xAA, 255 - i)) for i in rgb)
    load(b"", family)
    load(raw, input_pixels)
    probe.call("SetIconFamilyData", [probe.slot(family), 0x69733332, probe.slot(input_pixels)])
    p.expect(0, "encode incompressible RGB data")
    compare(packed_family(b"is32", packed), family, "interleaved RGB element")
    probe.call("GetIconFamilyData", [probe.slot(family), 0x69733332, probe.slot(output_pixels)])
    p.expect(0, "decode an interleaved RGB element")
    compare(raw, output_pixels, "round-tripped interleaved RGB data")
    probe.call("DisposeHandle", [probe.slot(input_pixels)])
    probe.call("DisposeHandle", [probe.slot(family)])
    for kind, bad in ((b"is32", b"\xff\x00\xff\x00"), (b"is32", b"\x04\x00"), (b"it32", b"\x01\x00\x00\x00")):
        load(packed_family(kind, bad), family)
        probe.call("GetIconFamilyData", [probe.slot(family), int.from_bytes(kind, "big"), probe.slot(output_pixels)])
        p.expect(-50, "reject a malformed RGB element")
        compare(raw, output_pixels, "failed decode leaves the caller's output intact")
        probe.call("DisposeHandle", [probe.slot(family)])
    probe.call("DisposeHandle", [probe.slot(output_pixels)])
    probe.call("BlockMoveData", [saved, probe.ptr(base), 12])


def icon_rgb_draw_operations(probe):
    p = probe.p
    base = 0x2000
    saved = probe.far_raw(bytes(64))
    probe.call("BlockMoveData", [probe.ptr(base), saved, 64])
    family, icon, world, pixmap, region, old_world, old_device, point, result = (base + 4 * i for i in range(9))
    bounds, target = base + 40, base + 48

    def store(offset, value):
        probe.arguments([value], 0)
        p.emit(0x907E0000 | offset)

    packed = b"\xff\xff\xfb\xff\xff\x00\xfb\x00\xff\x00\xfb\x00"
    alpha = (b"\xff" * 4 + b"\x80" * 4 + b"\x00" * 8) * 16
    data = struct.pack(">4sI4sI", b"icns", 24 + len(packed) + len(alpha), b"is32", 8 + len(packed))
    data += packed + struct.pack(">4sI", b"s8mk", 8 + len(alpha)) + alpha
    source = probe.far_raw(data)
    probe.call("NewHandle", [len(data)])
    p.nonzero("allocate the true-color icon family")
    p.emit(0x907E0000 | family)
    probe.arguments([source, probe.slot(family), len(data)], 0)
    p.emit(0x80840000)
    p.call("BlockMoveData")
    key = (0x41524449, 0x52474220)
    probe.call("RegisterIconRefFromIconFamily", [*key, probe.slot(family), probe.ptr(icon)])
    p.expect(0, "register a true-color icon without indexed elements")
    probe.call("DisposeHandle", [probe.slot(family)])
    store(bounds, 0)
    store(bounds + 4, 0x00200020)
    store(target, 0x00020002)
    store(target + 4, 0x00120012)
    probe.call("GetGWorld", [probe.ptr(old_world), probe.ptr(old_device)])
    probe.call("NewGWorld", [probe.ptr(world), 32, probe.ptr(bounds), 0, 0, 0])
    p.expect(0, "allocate a 32-bit GWorld for RGB alpha composition")
    probe.call("GetGWorldPixMap", [probe.slot(world)])
    p.nonzero("get the GWorld pixel map")
    p.emit(0x907E0000 | pixmap)
    probe.call("LockPixels", [probe.slot(pixmap)])
    p.expect(1, "lock the true-color destination pixels")
    probe.call("SetGWorld", [probe.slot(world), 0])
    white = probe.far_raw(b"\xff" * 6)
    probe.call("RGBBackColor", [white])
    probe.call("EraseRect", [probe.ptr(bounds)])
    probe.call("PlotIconRef", [probe.ptr(target), 0, 0, 0, probe.slot(icon)])
    p.expect(0, "plot the true-color icon into a white GWorld")
    for x, expected in ((3, 0xFFFF0000), (7, 0xFFFF7F7F), (14, 0xFFFFFFFF)):
        probe.call("GetCPixel", [x, 3, probe.ptr(result)])
        p.emit(0x807E0000 | result)
        p.expect(expected, "RGB drawing distinguishes opaque, partial and zero alpha")
    clip = probe.far_raw(struct.pack(">4h", 0, 0, 32, 6))
    probe.call("EraseRect", [probe.ptr(bounds)])
    probe.call("ClipRect", [clip])
    probe.call("PlotIconRef", [probe.ptr(target), 0, 0, 0, probe.slot(icon)])
    p.expect(0, "plot the icon into a clipped port")
    probe.call("GetCPixel", [7, 3, probe.ptr(result)])
    p.emit(0x807E0000 | result)
    p.expect(0xFFFFFFFF, "pixels outside the port clip keep their background")
    probe.call("NewRgn", [])
    p.nonzero("allocate a region for the true-color icon")
    p.emit(0x907E0000 | region)
    probe.call("IconRefToRgn", [probe.slot(region), probe.ptr(target), 0, 0, probe.slot(icon)])
    p.expect(0, "convert the eight-bit alpha mask into a region")
    probe.call("GetRegionBounds", [probe.slot(region), probe.ptr(result)])
    p.emit(0x807E0000 | (result + 4))
    p.expect(0x0012000A, "alpha coverage includes the partial pixels and excludes zero alpha")
    for x, expected in ((7, 1), (14, 0)):
        store(point, 3 << 16 | x)
        probe.call("PtInIconRef", [probe.ptr(point), probe.ptr(target), 0, 0, probe.slot(icon)])
        p.expect(expected, "hit testing uses the true-color alpha mask")
    probe.call("DisposeRgn", [probe.slot(region)])
    probe.call("SetGWorld", [probe.slot(old_world), probe.slot(old_device)])
    probe.call("UnlockPixels", [probe.slot(pixmap)])
    probe.call("DisposeGWorld", [probe.slot(world)])
    probe.call("UnregisterIconRef", [*key])
    p.expect(0, "remove the registered true-color test icon")
    probe.call("ReleaseIconRef", [probe.slot(icon)])
    p.expect(0, "release the true-color test reference")
    probe.call("BlockMoveData", [saved, probe.ptr(base), 64])


def data_browser_control_operations(probe, cases):
    p = probe.p
    base = 0x2000
    capture, original, data = base, base + 0x40, base + 0x80
    window, control = probe.slot(base + 0xB0), probe.slot(base + 0xB4)
    bounds, title = probe.ptr(base + 0xB8), probe.ptr(base + 0xC0)
    connection, saved, symbol_class = (base + offset for offset in (0xC4, 0xC8, 0xCC))
    companion = probe.far_raw(b"\x0eDataBrowserLib")
    saved_scratch = probe.far_raw(bytes(0xD0))

    def store(offset, word):
        probe.arguments([word], 0)
        p.emit(0x907E0000 | offset)

    probe.call("BlockMoveData", [probe.ptr(base), saved_scratch, 0xD0])
    probe.call("GetSharedLibrary", [companion, 0x70777063, 1, probe.ptr(connection), 0, 0])
    p.expect(0, "open the original Data Browser library")
    store(base + 0xB8, 40 << 16 | 40)
    store(base + 0xBC, 120 << 16 | 180)
    store(base + 0xC0, 0)
    probe.call("NewCWindow", [0, bounds, title, 0, 0, -1, 0, 0])
    p.nonzero("create the window for the Data Browser CDEF")
    p.emit(0x907E0000 | (base + 0xB0))
    probe.call("NewControl", [window, bounds, title, 0, 0, 0, 0, 16 * 183, probe.ptr(capture)])
    p.nonzero("create the Data Browser CDEF fixture control")
    p.emit(0x907E0000 | (base + 0xB4))

    def call(name, args, side):
        if not side:
            probe.call(name, args)
            return
        probe.arguments(args, 1)
        p.emit(0x819E0000 | saved, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x7FC2F378)

    def find_saved(name):
        symbol = name.encode("ascii")
        probe.call(
            "FindSymbol",
            [
                probe.slot(connection),
                probe.far_raw(bytes([len(symbol)]) + symbol),
                probe.ptr(saved),
                probe.ptr(symbol_class),
            ],
        )
        p.expect(0, "resolve the saved " + name)
        p.emit(0x887E0000 | symbol_class)
        p.expect(2, name + " has a saved PPC descriptor")
        p.emit(0x807E0000 | saved, 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
        p.nonzero(name + " uses the original PPC implementation")

    for name, tag, size, transfer, required, first, second, insert_key in cases:
        find_saved(name)
        args = [probe.ptr(data), probe.ptr(data + 4), probe.ptr(data + 8), probe.ptr(data + 12)]
        direct_output = transfer in ("get_buffer", "get_scroll")
        if transfer in ("get_items", "get_item_count", "get_packet", "set_packet", "set_scalar") and not insert_key:
            args[0] = 0x12345678
            if transfer in ("get_items", "get_item_count", "set_packet"):
                args[1] = 0x1020309A
        if transfer == "set_packet" and insert_key:
            args[0] = 0x12345678
        if transfer in ("get_buffer", "set_buffer", "set_scalar") or insert_key:
            del args[1:]
        elif size < 12:
            del args[2:]
        elif size == 12:
            del args[3:]
        mode = 0x01000000 if direct_output else 0
        if transfer == "set_packet" and (first < 0 or second < 0):
            mode = 0x02000000 | (max(0, -first) << 16) | (max(0, -second) << 8)
        store(capture + 56, mode)
        for side in (1, 0):
            invalid = -30599
            call(name, [0, *args], side)
            p.expect(invalid, name + " rejects an invalid control")
            if required:
                call(name, [control, *([0] * len(args))], side)
                p.expect(-50, name + " rejects missing required values")
            store(capture, 0xFFFFFFFF)
            if direct_output:
                for offset in range(0, size + 4, 4):
                    store(data + offset, 0xA5A5A5A5)
            call(name, [control, *args], side)
            p.expect(0, name + " dispatches to the same live control")
            p.emit(0x807E0000 | capture)
            p.expect(tag, name + " selects its CDEF tag")
            p.emit(0x807E0000 | (capture + 4))
            p.expect(size, name + " writes the original CDEF record size")
            if direct_output:
                p.emit(0x887E0000 | data)
                p.expect(0x6A, name + " writes the output start")
                p.emit(0x887E0000 | (data + size - 1))
                p.expect(0x6A, name + " writes the output end")
                p.emit(0x887E0000 | (data + size))
                p.expect(0xA5, name + " leaves the next output byte intact")
            else:
                # Some original aggregate packets leave padding uninitialized.
                padding = {"get_items": (13, 14, 15), "get_item_count": (9, 10, 11)}.get(transfer, ())
                for offset in range(size):
                    if offset in padding:
                        continue
                    p.emit(0x887E0000 | (capture + 8 + offset))
                    if side:
                        p.emit(0x987E0000 | (original + offset))
                    else:
                        p.emit(0x889E0000 | (original + offset))
                        p.equal(name + " agrees with the saved CDEF argument byte " + str(offset))
    first, second, third, fourth, fifth = (0x81234567, 0x87654321, 0x19283746, 0x54321098, 0xACE15260)
    messages = (
        ("EnableDataBrowserEditCommand", 1000, [first], [first], 0),
        ("ExecuteDataBrowserEditCommand", 1001, [first], [first], 0),
        ("AddDataBrowserItems", 1002, [first, 2, third, fourth], [first, 2, third, fourth], 16),
        ("RevealDataBrowserItem", 1003, [first, second, third], [first, second, 0x46000000], 9),
        (
            "UpdateDataBrowserItems",
            1004,
            [first, second, third, fourth, fifth],
            [first, second, third, fourth, fifth],
            20,
        ),
        (
            "ForEachDataBrowserItem",
            1005,
            [first, second, third, fourth, fifth],
            [first, third, fourth, fifth, 0x21000000],
            17,
        ),
        ("RemoveDataBrowserItems", 1006, [first, 2, third, fourth], [first, 2, third, fourth], 16),
        ("OpenDataBrowserContainer", 1007, [first], [first], 0),
        ("SortDataBrowserContainer", 1008, [first, second], [first, 0x21000000], 5),
        ("CloseDataBrowserContainer", 1009, [first], [first], 0),
        ("MoveDataBrowserSelectionAnchor", 1010, [first, second], [first, 0x21000000], 5),
        ("AddDataBrowserListViewColumn", 1011, [first, second], [first, second], 8),
        ("RemoveDataBrowserTableViewColumn", 1012, [first], [first], 4),
        ("AutoSizeDataBrowserListViewColumns", 1013, [], [0], 0),
    )
    for name, message, args, packet, size in messages:
        find_saved(name)
        store(capture + 0x70, size)
        for side in (1, 0):
            call(name, [0, *args], side)
            p.expect(0 if message == 1000 else -30599, name + " validates its control")
            if message == 1002:
                call(name, [0, first, 0, 0, 0], side)
                p.expect(0, "adding zero items is a no-op before validating the control")
            if message == 1006:
                call(name, [control, first, 1, 0, 0], side)
                p.expect(-50, "removing named items requires an item array")
            if message == 1011:
                store(capture + 56, 0x04000000)
                store(capture + 60, 0x636C766D)  # clvm
                call(name, [control, *args], side)
                p.expect(-50, "list-view columns require list view")
                store(capture + 60, 0x6C737476)  # lstv
                call(name, [control, 0, second], side)
                p.expect(-50, "a new list-view column requires a description")
            store(capture, 0xFFFFFFFF)
            call(name, [control, *args], side)
            p.expect(0x88667701, name + " preserves the 32-bit CDEF result")
            p.emit(0x807E0000 | capture)
            p.expect(message, name + " selects the original CDEF message")
            for i, value in enumerate(struct.pack(f">{len(packet)}I", *packet)[: size or 4]):
                p.emit(0x887E0000 | (capture + 8 + i))
                p.expect(value, name + " packs the expected argument byte " + str(i))

    find_saved("IsDataBrowserItemSelected")
    store(capture + 56, 0x03000000)
    for side in (1, 0):
        call("IsDataBrowserItemSelected", [0, first], side)
        p.expect(0, "an invalid browser cannot report a selection")
        for flags in (0, 1, 2, 3):
            store(capture + 60, flags)
            call("IsDataBrowserItemSelected", [control, first], side)
            p.expect(flags & 1, "the CDEF selection bit is independent of other item flags")

    store(capture + 0x70, 0)
    probe.call("SendControlMessage", [control, 1001, fifth])
    p.expect(0x88667701, "SendControlMessage returns the full CDEF result")
    p.emit(0x807E0000 | (capture + 8))
    p.expect(fifth, "SendControlMessage preserves the full parameter word")
    probe.call("DisposeControl", [control])
    spec, collection, initial, result = base + 0x10, base + 0x18, base + 0x1C, base + 0x20
    callback = probe.far_raw(bytes(8))
    p.routines.append(
        (
            callback[1],
            [
                0x90620000 | capture,
                0x90820000 | (capture + 4),
                0x90A20000 | (capture + 8),
                0x90C20000 | (capture + 12),
                0x2C050003,
                0x4082000C,
                0x80620000 | result,
                0x4E800020,
                0x3C608866,
                0x60637701,
                0x4E800020,
            ],
        )
    )
    probe.call("NewControlDefUPP", [callback])
    p.nonzero("create a PowerPC custom control definition")
    p.emit(0x907E0000 | (spec + 4))
    store(spec, 0)
    probe.call("NewCollection", [])
    p.nonzero("allocate custom control initialization collection")
    p.emit(0x907E0000 | collection)
    for tag, value, width in (
        (b"min ", -50000, 4),
        (b"max ", 80000, 4),
        (b"valu", 77000, 4),
        (b"refc", 0x12345678, 4),
        (b"varc", 7 << 16, 2),
        (b"titl", 0x54657374, 4),
        (b"visi", 1 << 24, 1),
    ):
        store(initial, value)
        probe.call(
            "AddCollectionItem", [probe.slot(collection), int.from_bytes(tag, "big"), 0, width, probe.ptr(initial)]
        )
        p.expect(0, "store custom control initialization item " + tag.decode())
    store(result, 0)
    args = [window, bounds, probe.ptr(spec), probe.slot(collection), probe.ptr(base + 0xB4)]
    probe.call("CreateCustomControl", args)
    p.expect(0, "create a custom control with initialization data")
    for position, expected in ((0, 7), (8, 3)):
        p.emit(0x807E0000 | (capture + position))
        p.expect(expected, "custom control initialization receives the variant and init message")
    for position, expected in ((4, base + 0xB4), (12, collection)):
        p.emit(0x807E0000 | (capture + position), 0x809E0000 | expected, 0x7C632278)
        p.expect(0, "custom control initialization receives the control and collection")
    for name, expected in (
        ("GetControl32BitMinimum", -50000),
        ("GetControl32BitMaximum", 80000),
        ("GetControl32BitValue", 77000),
        ("GetControlReference", 0x12345678),
        ("IsControlVisible", 1),
    ):
        probe.call(name, [control])
        p.expect(expected, name + " reads the collection-supplied initial value")
    title_output = probe.far_raw(bytes(256))
    probe.call("GetControlTitle", [control, title_output])
    probe.arguments([title_output], 0)
    p.emit(0x80630000)
    p.expect(0x04546573, "custom control starts with its Pascal title")
    probe.call("SendControlMessage", [control, 1001, fifth])
    p.expect(0x88667701, "custom control receives messages through the installed definition")
    color_callback = probe.far_raw(bytes(8))
    p.routines.append(
        (
            color_callback[1],
            [
                0x90620000 | capture,
                0x90820000 | (capture + 4),
                0x90A20000 | (capture + 8),
                0x90C20000 | (capture + 12),
                0x80620000 | result,
                0x4E800020,
            ],
        )
    )
    probe.call("NewControlColorUPP", [color_callback])
    p.nonzero("create a PowerPC control color procedure")
    p.emit(0x907E0000 | initial)
    probe.call("SetControlColorProc", [control, probe.slot(initial)])
    p.expect(0, "install a control color procedure")
    probe.call("SetControlColorProc", [0, probe.slot(initial)])
    p.expect(-50, "reject a color procedure for an invalid control")
    for api, message, depth, in_color in (("SetUpControlBackground", 23, 32, 1), ("SetUpControlTextColor", 30, 1, 0)):
        probe.call(api, [control, depth, in_color])
        p.expect(0, "apply color through the control procedure")
        for position, expected in ((4, message), (8, depth), (12, in_color)):
            p.emit(0x807E0000 | (capture + position))
            p.expect(expected, "the color procedure receives the setup arguments")
        p.emit(0x807E0000 | capture, 0x809E0000 | (base + 0xB4), 0x7C632278)
        p.expect(0, "the color procedure receives its own control")
    store(result, -50)
    probe.call("SetUpControlTextColor", [control, 32, 1])
    p.expect(0, "a declining color procedure delegates to the default color")
    probe.call("SetControlColorProc", [control, 0])
    p.expect(0, "clear the installed color procedure")
    store(capture, 0)
    probe.call("SetUpControlBackground", [control, 32, 1])
    p.expect(0, "default background setup works after clearing the UPP")
    p.emit(0x807E0000 | capture)
    p.expect(0, "cleared color procedure is not called")
    probe.call("DisposeControlColorUPP", [probe.slot(initial)])
    probe.call("DisposeControl", [control])
    p.emit(0x807E0000 | (capture + 8))
    p.expect(4, "custom control disposal notifies the installed definition")
    store(result, -30580)
    probe.call("CreateCustomControl", args)
    p.expect(-30580, "failed custom control initialization returns the CDEF status")
    p.emit(0x807E0000 | (base + 0xB4))
    p.expect(0, "failed custom control initialization leaves the output empty")
    p.emit(0x807E0000 | (capture + 8))
    p.expect(3, "a failed custom control does not receive a disposal message")
    probe.call("DisposeCollection", [probe.slot(collection)])
    probe.call("DisposeControlDefUPP", [probe.slot(spec + 4)])
    family, payload, icon, second_icon, copy, owners = (base + 4 * i for i in range(6))
    source = probe.far_raw(struct.pack(">4sI4sI", b"icns", 80, b"ics#", 72) + b"\xff" * 32 + b"\xff\x00" * 16)

    def new_handle(offset, size):
        probe.call("NewHandle", [size])
        p.nonzero("allocate a handle for icon family data")
        p.emit(0x907E0000 | offset)

    sound = (0x6D616373, 0x61736E64)
    new_handle(family, 80)
    probe.arguments([source], 0)
    p.emit(0x809E0000 | family, 0x80840000, 0x38A00050)
    p.call("BlockMoveData")
    new_handle(payload, 4)
    probe.call("GetIconFamilyData", [probe.slot(family), 0x69637323, probe.slot(payload)])
    p.expect(0, "extract a small monochrome icon")
    probe.call("GetHandleSize", [probe.slot(payload)])
    p.expect(64, "icon family data resizes the result handle to the element length")
    probe.call("GetIconFamilyData", [probe.slot(family), 0x69636C38, probe.slot(payload)])
    p.expect(0, "request missing icon-family element")
    probe.call("GetHandleSize", [probe.slot(payload)])
    p.expect(0, "a missing icon element empties the output handle")
    probe.call("RegisterIconRefFromIconFamily", [*sound, probe.slot(family), probe.ptr(icon)])
    p.expect(0, "register a copy of an icon family")
    probe.call("DisposeHandle", [probe.slot(family)])
    probe.call("GetIconRef", [-32768, *sound, probe.ptr(second_icon)])
    p.expect(0, "lookup the registered sound icon by creator and type")
    p.emit(0x807E0000 | icon, 0x809E0000 | second_icon, 0x7C632278)
    p.expect(0, "registry lookup returns the same icon reference")
    probe.call("IsValidIconRef", [probe.slot(icon)])
    p.expect(1, "a live registered reference is valid")
    probe.call("AcquireIconRef", [probe.slot(icon)])
    p.expect(0, "acquire another owner for the same icon")
    probe.call("GetIconRefOwners", [probe.slot(icon), probe.ptr(owners)])
    p.expect(0, "get the registered icon owner count")
    p.emit(0xA07E0000 | owners)
    p.expect(3, "registration, lookup and acquire each grant one owner")
    probe.call("FlushIconRefs", [*sound])
    p.expect(0, "flush leaves a matching family without an external reload source intact")
    probe.call("IconRefToIconFamily", [probe.slot(icon), 0x100, probe.ptr(copy)])
    p.expect(0, "copy the small elements of a registered icon family")
    probe.call("GetHandleSize", [probe.slot(copy)])
    p.expect(80, "the selected element and family header are preserved")
    suite, exported, hit_bounds, location, clipped, region = (base + n for n in (0x14, 0x18, 0x20, 0x28, 0x30, 0x38))
    probe.call("IconFamilyToIconSuite", [probe.slot(copy), 0x100, probe.ptr(suite)])
    p.expect(0, "convert a small monochrome family to a classic icon suite")
    probe.call("IconSuiteToIconFamily", [probe.slot(suite), 0x100, probe.ptr(exported)])
    p.expect(0, "convert a classic icon suite back to a family")
    probe.call("GetHandleSize", [probe.slot(exported)])
    p.expect(80, "the suite round trip copies the 64-byte image and mask")
    probe.call("DisposeIconSuite", [probe.slot(suite), 1])
    p.expect(0, "dispose the suite and its copied elements")
    probe.call("DisposeHandle", [probe.slot(exported)])
    probe.call("DisposeHandle", [probe.slot(copy)])
    store(hit_bounds, 0x00020002)
    store(hit_bounds + 4, 0x00120012)
    probe.call("IsIconRefMaskEmpty", [probe.slot(icon)])
    p.expect(0, "the icon mask contains opaque pixels")
    probe.call("GetIconSizesFromIconRef", [0xFFFFFFFF, probe.ptr(clipped), 0, probe.slot(icon)])
    p.expect(0, "query the icon's available elements")
    p.emit(0x807E0000 | clipped)
    p.expect(0x100, "only the small monochrome icon is available")
    for h, expected in ((3, 1), (13, 0)):
        store(location, 3 << 16 | h)
        probe.call("PtInIconRef", [probe.ptr(location), probe.ptr(hit_bounds), 0, 0, probe.slot(icon)])
        p.expect(expected, "point hit testing follows the icon mask")
    store(clipped, 0x0003000B)
    store(clipped + 4, 0x0005000C)
    probe.call("RectInIconRef", [probe.ptr(clipped), probe.ptr(hit_bounds), 0, 0, probe.slot(icon)])
    p.expect(0, "a test rectangle within the transparent half misses")
    store(clipped, 0x00030003)
    probe.call("RectInIconRef", [probe.ptr(clipped), probe.ptr(hit_bounds), 0, 0, probe.slot(icon)])
    p.expect(1, "a test rectangle touching the opaque half hits")
    probe.call("NewRgn", [])
    p.nonzero("allocate a region for the icon mask")
    p.emit(0x907E0000 | region)
    probe.call("IconRefToRgn", [probe.slot(region), probe.ptr(hit_bounds), 0, 0, probe.slot(icon)])
    p.expect(0, "convert the icon mask into a region")
    probe.call("GetRegionBounds", [probe.slot(region), probe.ptr(clipped)])
    p.emit(0x807E0000 | (clipped + 4))
    p.expect(0x0012000A, "the region ends at the last opaque column")
    probe.call("DisposeRgn", [probe.slot(region)])
    probe.call("GetPort", [probe.ptr(location)])
    probe.call("PlotIconRefInPort", [window, probe.ptr(hit_bounds), 0, 0, 0, 0, probe.slot(icon)])
    p.expect(0, "draw the native icon into the requested port")
    probe.call("GetPort", [probe.ptr(clipped)])
    p.emit(0x807E0000 | location, 0x809E0000 | clipped, 0x7C632278)
    p.expect(0, "icon drawing restores the caller's port")
    probe.call("PlotIconRef", [probe.ptr(hit_bounds), 0, 0, 0, probe.slot(icon)])
    p.expect(0, "draw the native icon into the current port")
    store(location, 0x69636E72)
    probe.call("SetDataBrowserItemDataIcon", [probe.ptr(location + 4), probe.slot(icon)])
    p.expect(0, "store a native icon in the Data Browser item")
    p.emit(0x807E0000 | (location + 4), 0x809E0000 | icon, 0x7C632278)
    p.expect(0, "Data Browser stores the IconRef as a guest pointer")
    probe.call("GetIconRefOwners", [probe.slot(icon), probe.ptr(clipped)])
    p.expect(0, "query the icon owner count after the item setter")
    p.emit(0xA07E0000 | clipped)
    p.expect(4, "the item setter retains the icon")
    probe.call("ReleaseIconRef", [probe.slot(icon)])
    p.expect(0, "release the test item's ownership")
    probe.call("SetIconFamilyData", [probe.slot(payload), 0x69637323, probe.slot(payload)])
    p.expect(0, "initialize an empty icon family without losing aliased input")
    probe.call("GetHandleSize", [probe.slot(payload)])
    p.expect(16, "the empty icon element still has a family and element header")
    probe.call("DisposeHandle", [probe.slot(payload)])
    find_saved("CreateDataBrowserControl")
    probe.call("CreateDataBrowserControl", [0, bounds, 0x6C737476, probe.ptr(base + 0xB4)])
    p.expect(-50, "Data Browser creation rejects an invalid window")
    p.emit(0x807E0000 | (base + 0xB4))
    p.expect(0, "failed Data Browser creation clears the output")
    probe.call("CreateDataBrowserControl", [window, bounds, 0x6C737476, probe.ptr(base + 0xB4)])
    p.expect(0, "create a real Data Browser through the native wrapper")
    probe.call("GetDataBrowserViewStyle", [control, probe.ptr(owners)])
    p.expect(0, "query the real Data Browser control")
    p.emit(0x807E0000 | owners)
    p.expect(0x6C737476, "the native Data Browser constructor selects list view")
    probe.call("DisposeControl", [control])
    probe.call("UnregisterIconRef", [*sound])
    p.expect(0, "unregister the sound icon key")
    for _ in range(3):
        probe.call("ReleaseIconRef", [probe.slot(icon)])
        p.expect(0, "release a reference retained across unregister")
    probe.call("IsValidIconRef", [probe.slot(icon)])
    p.expect(0, "the final release invalidates an unregistered icon")
    probe.call("ReleaseIconRef", [probe.slot(icon)])
    p.expect(-2580, "reject reuse of the released icon reference")
    probe.call("GetIconRef", [-32768, sound[0], 0x646F6375, probe.ptr(icon)])
    p.expect(0, "look up the generic document icon in the embedded System resources")
    probe.call("IconRefToIconFamily", [probe.slot(icon), 1, probe.ptr(copy)])
    p.expect(0, "pack the system icon mask into an icon family")
    probe.call("GetHandleSize", [probe.slot(copy)])
    p.expect(272, "the system icon family has a 32-pixel monochrome image and mask")
    probe.call("DisposeHandle", [probe.slot(copy)])
    probe.call("ReleaseIconRef", [probe.slot(icon)])
    p.expect(0, "release the system icon reference")
    probe.call("DisposeWindow", [window])
    probe.call("CloseConnection", [probe.ptr(connection)])
    p.expect(0, "close the original Data Browser library")
    probe.call("BlockMoveData", [saved_scratch, probe.ptr(base), 0xD0])


def data_browser_operations(probe):
    p = probe.p
    base = 0x2000
    item = probe.ptr(base + 4)
    source = probe.ptr(base + 32)
    result = probe.ptr(base + 44)
    string = probe.slot(base + 56)

    def store(offset, value):
        probe.arguments([value], 0)
        p.emit(0x907E0000 | (base + offset))

    def expect(offset, value, detail, width=4):
        p.emit({1: 0x887E0000, 2: 0xA07E0000, 4: 0x807E0000}[width] | (base + offset))
        p.expect(value, detail)

    def call(name, args, status=0):
        probe.call(name, args)
        p.expect(status, name + " result")

    def tag(name):
        store(0, int.from_bytes(name, "big"))

    def field(action, name, value, status=0):
        call(action + "DataBrowserItemData" + name, [item, value], status)

    def round_trip(name, value, position, width=4, result_width=None):
        store(44, 0xAAAAAAAA)
        field("Set", name, value)
        expect(4 + position, value & ((1 << (width * 8)) - 1), name + " packed field", width)
        field("Get", name, result)
        expect(44, value & ((1 << (width * 8)) - 1), name + " output", result_width or width)
        if (result_width or width) < 4:
            expect(44 + (result_width or width), 0xAA, name + " output tail", 1)

    for name, extent in (("InitDataBrowserCallbacks", 44), ("InitDataBrowserCustomCallbacks", 32)):
        store(0, 1)
        store(4, 0x7F123456)
        call(name, [probe.ptr(base)], -50)
        expect(4, 0x7F123456, name + " rejects an unknown version without writing")
        call(name, [0], -50)
        store(0, 0)
        for offset in range(4, extent + 4, 4):
            store(offset, 0xF00DCAFE)
        call(name, [probe.ptr(base)])
        for offset in range(0, extent, 4):
            expect(offset, 0, name + " clears the version-zero callback table")
        expect(extent, 0xF00DCAFE, name + " leaves the following word intact")

    tag(b"trck")
    probe.call("GetDataBrowserItemDataType", [item])
    p.expect(int.from_bytes(b"trck", "big"), "Data Browser reads the hidden type tag")
    for name, value, offset in (("Minimum", -9, 10), ("Maximum", 81, 14), ("Value", -4, 18)):
        round_trip(name, value, offset)
    field("Get", "Maximum", 0, -50)
    store(44, 77)
    field("Get", "BooleanValue", result, -4974)
    expect(44, 77, "a type mismatch leaves the caller output intact")
    field("Set", "MenuRef", 0, -4974)

    tag(b"menu")
    round_trip("Value", 0xFEDC, 4, 2, 4)
    for value in (-1, 65536):
        field("Set", "Value", value, -30581)
        expect(8, 0xFEDC, "menu value overflow leaves its 16-bit field intact", 2)
    probe.call("NewMenu", [8322, probe.far_raw(b"\x04Item")])
    p.nonzero("allocate a menu to store on the Data Browser item")
    p.emit(0x907E0000 | (base + 48))
    menu = probe.slot(base + 48)
    probe.call("GetMenuRetainCount", [menu])
    p.expect(1, "the test menu has one owner before assignment")
    field("Set", "MenuRef", menu)
    field("Get", "MenuRef", result)
    p.emit(0x807E0000 | (base + 44), 0x809E0000 | (base + 48), 0x7C632278)
    p.expect(0, "the menu getter returns the stored reference")
    field("Set", "MenuRef", 0)
    expect(4, 0, "the menu setter can clear a menu reference")
    probe.call("GetMenuRetainCount", [menu])
    p.expect(2, "the item setter shares the saved Menu Manager's retain count")
    call("ReleaseMenu", [menu])
    call("ReleaseMenu", [menu])

    tag(b"#ID#")
    field("Set", "ItemID", 0xEDCBA987)
    expect(4, 0xEDCBA987, "item ID is a full-width field")

    for kind in (b"bool", b"edit"):
        tag(kind)
        round_trip("BooleanValue", 0x87, 0, 1)
    store(8, 0x5F616263)
    field("Get", "Property", result)
    expect(44, 0x5F616263, "the edit record exposes its property ID")

    tag(b"drwi")
    store(4, 1)
    round_trip("DrawState", 0, 0)
    field("Set", "DrawState", 1, -30581)
    field("Set", "DrawState", 2, -30581)
    field("Get", "DrawState", result)
    expect(44, 0, "an invalid transition leaves the draw state intact")
    round_trip("ButtonValue", 0xABCD, 4, 2)

    tag(b"date")
    field("Set", "DateTime", 0xF011DCBA)
    expect(4, 0, "the 32-bit date is zero extended")
    expect(8, 0xF011DCBA, "the low word of the date retains all its bits")
    field("Get", "DateTime", result)
    expect(44, 0xF011DCBA, "the legacy getter returns all 32 bits")
    store(32, 0xFFFFFFFF)
    store(36, 0xEDCBA987)
    field("Set", "LongDateTime", source)
    field("Get", "LongDateTime", result)
    expect(44, 0xFFFFFFFF, "the long date preserves its signed high word")
    expect(48, 0xEDCBA987, "the long date preserves its low word")
    field("Get", "DateTime", result, 0x8800)
    expect(44, 0xEDCBA987, "overflow still returns the low date word")

    for kind, offset in ((b"icnr", 4), (b"ticn", 8)):
        tag(kind)
        round_trip("IconTransform", 0xBA98, offset + 6, 2)
        store(32, 0x11223344)
        store(36, 0x55667788)
        store(48, 0xAAAAAAAA)
        field("Set", "RGBColor", source)
        expect(4 + offset, 0x11223344, "red and green are adjacent 16-bit channels")
        expect(8 + offset, 0x5566, "blue channel is 16 bits", 2)
        field("Get", "RGBColor", result)
        expect(44, 0x11223344, "RGBColor read from icon storage")
        expect(48, 0x5566AAAA, "RGBColor getter writes exactly six bytes")
        expect(10 + offset, 0xBA98, "setting the color leaves the transform intact", 2)

    store(32, 0x03616263)
    probe.call("CFStringCreateWithPascalString", [0, source, 0])
    p.nonzero("create text for the Data Browser ownership checks")
    p.emit(0x907E0000 | (base + 56))
    for kind, offset in ((b"text", 0), (b"ticn", 4)):
        tag(kind)
        field("Set", "Text", string)
        field("Get", "Text", result)
        p.emit(0x807E0000 | (base + 44), 0x809E0000 | (base + 56))
        p.equal("the text getter returns the stored reference")
        p.emit(0x807E0000 | (base + 4 + offset), 0x809E0000 | (base + 56))
        p.equal("the text reference is stored in the proper variant field")
        probe.call("CFGetRetainCount", [string])
        p.expect(3, "both the Data Browser text setter and getter took ownership")
        probe.call("CFRelease", [string])
        field("Set", "Text", 0)
        probe.call("CFRelease", [string])
        field("Get", "Text", result)
        expect(44, 0, "the getter writes a null stored reference")
    probe.call("CFRelease", [string])


def romaw_service_operations(probe):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    base = 0x2000  # Reuse the completed parameter-block checks scratch area.
    rect = probe.raw(struct.pack(">4h", 32, 32, 88, 160))
    control_rect = probe.raw(struct.pack(">4h", 6, 6, 22, 40))
    title = probe.raw(b"\x07Service")
    port, window, bevel, image, root, menu = (slot(base + at) for at in range(0, 24, 4))
    output = ptr(base + 24)

    def call(name, args=(), expected=0):
        probe.call(name, args)
        p.expect(expected, name + " returns the expected native result")

    def save(offset):
        p.emit(0x907E0000 | (base + offset))

    def load(offset, width=4):
        p.emit({1: 0x887E0000, 2: 0xA07E0000, 4: 0x807E0000}[width] | (base + offset))

    probe.call("GetFrontProcess", [output])
    p.expect(0, "the front process serial number is available")
    call("WakeUpProcess", [output])
    probe.call("MaxMem", [ptr(base + 44)])
    p.nonzero("MaxMem returns available contiguous memory")
    code = probe.far_raw(bytes.fromhex("386000074e800020"))
    probe.call("MakeDataExecutable", [code, 8])
    probe.call("GetMBarHeight")
    p.nonzero("the installed menu bar has a measurable height")
    call("GetThemeMenuBarHeight", [output])
    load(24, 2)
    p.nonzero("Appearance reports the installed menu bar height")
    call("GetThemeMenuBarHeight", [0], -50)
    for depth, color_device in ((32, 1), (1, 1), (32, 0)):
        probe.call("IsThemeInColor", [depth, color_device], side=1)
        p.emit(0x7C630034, 0x5463D97E, 0x68630001)  # Convert the saved trap's packed Boolean to 0 or 1.
        save(44)
        probe.call("IsThemeInColor", [depth, color_device])
        p.emit(0x809E0000 | (base + 44))
        p.equal("Appearance color mode agrees with the saved implementation")
    for name, address in (("GetSysFont", 0xBA6), ("GetAppFont", 0x984)):
        probe.call(name)
        p.emit(0xA0800000 | address)
        p.equal(name + " returns the configured low-memory font ID")
    probe.call("GetSysDirection")
    save(44)
    probe.call("SetSysDirection", [-1])
    call("GetSysDirection", [], -1)
    probe.call("SetSysDirection", [slot(base + 44)])
    probe.call("GetScriptManagerVariable", [28])
    save(44)
    call("SetScriptManagerVariable", [28, -73])
    call("GetScriptManagerVariable", [28], -73)
    call("SetScriptManagerVariable", [28, slot(base + 44)])
    probe.call("SndSoundManagerVersion", [output])
    load(24)
    p.expect(0x03218000, "Sound Manager returns NumVersion through the hidden PPC result pointer")
    call("TSMGetActiveDocument")
    pen_case = probe.far_raw(b"\x03aBc\xA5")
    probe.call("UpperString", [pen_case, 1])
    probe.arguments([pen_case], 0)
    p.emit(0x80630000)
    p.expect(0x03414243, "UpperString preserves the Pascal length and uppercases all characters")
    probe.arguments([pen_case], 0)
    p.emit(0x88630004)
    p.expect(0xA5, "UpperString respects the Pascal string boundary")
    for menu_id, expected in ((-20481, 0), (-20480, 1), (-16385, 1), (-16384, 0)):
        call("IsSystemMenu", [menu_id, output])
        load(24, 1)
        p.expect(expected, "IsSystemMenu uses the inclusive reserved menu-ID range")
    probe.call("GetScriptManagerVariable", [38])
    p.nonzero("the Script Manager exposes its keyboard map")
    save(44)
    p.emit(0x38600000, 0x907E0000 | (base + 52))
    call("KeyTranslate", [slot(base + 44), 0, ptr(base + 52)], ord("a"))

    p.emit(0xA0600144)
    save(56)
    probe.call("SetEventMask", [0xFFFF])
    probe.call("FlushEvents", [0x1000, 0])
    call("PostEvent", [12, 0x73657276])
    p.emit(0x80600150)  # EventQueue.qTail
    p.nonzero("PostEvent appends an event queue element")
    save(44)
    p.emit(0xA0630006)
    p.expect(12, "PostEvent queues the application event type")
    load(44)
    p.emit(0x80630008)
    p.expect(0x73657276, "PostEvent preserves the application event message")
    probe.call("FlushEvents", [0x1000, 0])
    p.emit(0x80600150, 0x809E0000 | (base + 44), 0x7C632278)
    p.nonzero("FlushEvents removes the application's queued event")
    probe.call("SetEventMask", [slot(base + 56)])

    probe.call("NewMenu", [207, title])
    p.nonzero("allocate a menu for Appearance menu getters")
    save(20)
    probe.call("AppendMenu", [menu, title])
    call("GetMenuItemModifiers", [menu, 1, output])
    load(24, 1)
    p.expect(0, "classic menu items begin with default modifiers")
    call("GetMenuItemKeyGlyph", [menu, 1, output])
    load(24, 2)
    p.expect(0, "classic menu items begin without a replacement glyph")
    call("GetMenuItemKeyGlyph", [menu, 2, output], -50)
    call("SetMenuItemModifiers", [menu, 1, 2])
    call("GetMenuItemModifiers", [menu, 1, output])
    load(24, 1)
    p.expect(2, "the Menu Manager keeps the replacement key modifiers")
    call("SetMenuItemModifiers", [menu, 1, 0])
    call("SetMenuItemKeyGlyph", [menu, 1, 0x25])
    call("GetMenuItemKeyGlyph", [menu, 1, output])
    load(24, 2)
    p.expect(0x25, "the Menu Manager keeps the replacement key glyph")
    call("SetMenuItemRefCon", [menu, 1, 0xFEED0123])
    call("GetMenuItemRefCon", [menu, 1, output])
    load(24)
    p.expect(0xFEED0123, "the Menu Manager preserves a full-width item reference constant")
    call("GetMenuItemRefCon", [menu, 2, output], -50)
    load(24)
    p.expect(0xFEED0123, "invalid item queries preserve caller output")
    probe.call("SetItemCmd", [menu, 1, ord("Z")])
    probe.call("InsertMenu", [menu, 0])
    event = probe.raw(struct.pack(">HIIhhH", 3, ord("z"), 0, 0, 0, 0x100))
    call("MenuEvent", [event], 207 << 16 | 1)
    probe.call("DeleteMenu", [207])
    call("GetMenuRetainCount", [menu], 1)
    call("RetainMenu", [menu])
    call("GetMenuRetainCount", [menu], 2)
    probe.call("DisposeMenu", [menu])
    call("GetMenuRetainCount", [menu], 1)
    call("GetMenuItemRefCon", [menu, 1, output])
    load(24)
    p.expect(0xFEED0123, "disposing one owner keeps the live menu's metadata")
    call("GetMenuAttributes", [menu, output])
    load(24)
    p.expect(0, "new menus begin with default attributes")
    call("ChangeMenuAttributes", [menu, 0x10, 0])
    call("GetMenuAttributes", [menu, output])
    load(24)
    p.expect(0x10, "the native menu attribute getter sees the hidden flag")
    call("ChangeMenuAttributes", [menu, 0x10, 0x10], -50)
    call("ChangeMenuAttributes", [menu, 0, 0x10])
    call("GetMenuAttributes", [menu, output])
    load(24)
    p.expect(0, "the hidden flag can be cleared")
    call("ReleaseMenu", [menu])
    call("StandardAlert", [-1, title, 0, 0, output], -50)
    load(24, 2)
    p.expect(0, "an invalid alert leaves no selected button")
    call("GetThemeMenuSeparatorHeight", [output])
    load(24, 2)
    p.nonzero("the separator follows the current menu font")
    call("GetThemeMenuItemExtra", [0, output, ptr(base + 26)])
    load(24)
    p.expect(0, "the classic menu style has no extra inset")

    probe.call("GetPort", [ptr(base)])
    probe.call("NewWindow", [0, rect, title, 0, 0, -1, 1, 0])
    p.nonzero("create a window for Appearance control hierarchy checks")
    save(4)
    probe.call("SetPort", [window])
    for name in ("DrawThemePrimaryGroup", "DrawThemeSecondaryGroup", "DrawThemeSeparator"):
        call(name, [0, 1], -50)
        call(name, [control_rect, 7])
    for name, extra in (("DrawThemeEditTextFrame", ()), ("DrawThemeGenericWell", (1,))):
        call(name, [0, 1, *extra], -50)
        call(name, [control_rect, 7, *extra])
    menu_item_args = [0, 200, 7, 0, 0, 0]
    call("DrawThemeMenuItem", [0, control_rect, *menu_item_args], -50)
    call("DrawThemeMenuItem", [control_rect, control_rect, *menu_item_args])
    foreground = probe.raw(bytes(6))
    background = probe.raw(bytes(6))
    probe.call("GetForeColor", [foreground])
    probe.call("GetBackColor", [background])
    call("SetThemePen", [49, 8, 1])
    call("SetThemeBackground", [51, 8, 1])
    probe.call("RGBForeColor", [foreground])
    probe.call("RGBBackColor", [background])
    call("CreateRootControl", [window, ptr(base + 16)])
    _, constructor_output = probe.raw(bytes(16))
    for constructor, args in BUILTIN_CONTROL_CASES:
        text_control = constructor == "CreateStaticTextControl"
        if text_control:
            probe.call("CFStringCreateWithCString", [0, probe.raw(b"Text\0"), 0])
            p.nonzero("create a Core Foundation string for the static text control")
            p.emit(0x907E0000 | (constructor_output + 4))
            args = (slot(constructor_output + 4), 0)
        call(constructor, [window, 0, *args, ptr(constructor_output)], -50)
        call(constructor, [window, control_rect, *args, ptr(constructor_output)])
        control = slot(constructor_output)
        call("IsValidControlHandle", [control], 1)
        call("IsControlVisible", [control], 1)
        if args[:1] == (65000,):
            call("GetControl32BitMinimum", [control], -100000)
            call("GetControl32BitMaximum", [control], 100000)
            call("GetControl32BitValue", [control], 65000)
        if text_control:
            probe.call("CFRelease", [slot(constructor_output + 4)])
            call(
                "GetControlData", [control, 0, 0x74657874, 4, ptr(constructor_output + 8), ptr(constructor_output + 12)]
            )
            p.emit(0x807E0000 | (constructor_output + 8))
            p.expect(0x54657874, "the static control copied its initial text from the CFString")
        if constructor == "CreateDisclosureTriangleControl":
            call("SetDisclosureTriangleLastValue", [control, 1])
        probe.call("DisposeControl", [control])
    call("CreatePushButtonWithIconControl", [window, control_rect, 0, 0, 6, ptr(constructor_output)], -50)
    for constructor, args in (
        ("CreatePushButtonWithIconControl", (0, probe.raw(struct.pack(">HhH", 2, 0, 0)), 6)),
        ("CreateTabsControl", (0, 0, 1, probe.raw(bytes(10)))),
        ("CreateListBoxControl", (0, 2, 1, 0, 1, 14, 0, 0, probe.raw(struct.pack(">II", 1, 0)))),
    ):
        if constructor == "CreateListBoxControl":
            probe.call(constructor, [window, control_rect, *args, ptr(constructor_output)], side=1)
            p.expect(0, "the reference list box accepts the same standard definition")
            probe.call("DisposeControl", [slot(constructor_output)])
        call(constructor, [window, control_rect, *args, ptr(constructor_output)])
        control = slot(constructor_output)
        call("IsValidControlHandle", [control], 1)
        call("IsControlVisible", [control], 1)
        if constructor == "CreateTabsControl":
            call("SetTabEnabled", [control, 1, 0])
            call("SetTabEnabled", [control, 1, 1])
            call("GetTabContentRect", [control, ptr(constructor_output + 8)])
        probe.call("DisposeControl", [control])
    call("HandleControlClick", [0, 0, 0, 0], 0)
    call("HandleControlKey", [0, 0, ord("a"), 0], 0)
    call("GetRootControl", [window, output])
    load(24)
    p.emit(0x809E0000 | (base + 16))
    p.equal("GetRootControl returns the attached root")
    p.nonzero("CreateRootControl provisions a live control")
    call("CountSubControls", [root, output])
    load(24, 2)
    p.expect(0, "the root has no children before embedding")
    for proc, at in ((32, 8), (176, 12)):
        probe.call("NewControl", [window, control_rect, title, 1, 0, 0, 0, proc, 0])
        p.nonzero("create a native Appearance control")
        save(at)
    call("EmbedControl", [bevel, root])
    call("AutoEmbedControl", [image, window])
    call("EmbedControl", [root, bevel], -30590)
    call("GetSuperControl", [bevel, output])
    load(24)
    p.emit(0x809E0000 | (base + 16))
    p.equal("the embedded bevel button belongs to the root")
    call("GetSuperControl", [root, output], -50)
    load(24)
    p.expect(0, "the root has no parent")
    call("CountSubControls", [root, output])
    load(24, 2)
    p.expect(2, "the root counts only immediate children")
    for index, slot_offset in ((1, 8), (2, 12)):
        call("GetIndexedSubControl", [root, index, output])
        load(24)
        p.emit(0x809E0000 | (base + slot_offset))
        p.equal("the one-based index follows embedding order")
    call("GetIndexedSubControl", [root, 3, output], -50)
    load(24)
    p.emit(0x809E0000 | (base + 12))
    p.equal("a missing index leaves the output alone")
    call("CountSubControls", [bevel, output], -30590)
    load(24, 2)
    p.expect(0, "a leaf control writes a zero child count")
    probe.call("NewControl", [window, control_rect, title, 0, 0, 0, 0, 160, 0])
    p.nonzero("create a nested control container")
    save(32)
    group = slot(base + 32)
    call("SetControlSupervisor", [0, group], -50)
    call("SetControlSupervisor", [bevel, group])
    call("EmbedControl", [group, root])
    call("EmbedControl", [image, group])
    call("CountSubControls", [root, output])
    load(24, 2)
    p.expect(2, "reparenting removes the child from the previous container")
    call("GetIndexedSubControl", [group, 1, output])
    load(24)
    p.emit(0x809E0000 | (base + 12))
    p.equal("reparenting adds the child to the new container")
    probe.call("DisposeControl", [group])
    call("SetControlSupervisor", [bevel, group], -50)
    call("SetControlSupervisor", [bevel, 0])
    call("CountSubControls", [root, output])
    load(24, 2)
    p.expect(1, "disposing the container removes it from the root")
    call("IsValidControlHandle", [image], 0)
    call("GetSuperControl", [image, output], -50)
    probe.call("NewControl", [window, control_rect, title, 1, 0, 0, 0, 176, 0])
    p.nonzero("create a replacement image well after disposing the group")
    save(12)
    call("AutoEmbedControl", [image, window])
    call("SetControlVisibility", [bevel, 0, 0])
    call("IsControlVisible", [bevel], 0)
    call("SetControlVisibility", [bevel, 1, 0])
    call("IsControlVisible", [bevel], 1)
    call("SetControlFontStyle", [bevel, probe.raw(struct.pack(">6h", 3, 0, 12, 0, 0, 0))])
    call("SetUpControlBackground", [bevel, 32, 1])
    call("FindControlUnderMouse", [0, window, output], 0)
    load(24, 2)
    p.expect(0, "hidden windows clear the hit-tested control part")
    call("SetBevelButtonGraphicAlignment", [bevel, 2, -3, 4])
    call("SetBevelButtonTextAlignment", [bevel, 0, 2])
    call("SetBevelButtonTextPlacement", [bevel, 0])
    for stem, control in (("BevelButton", bevel), ("ImageWell", image)):
        content = probe.raw(struct.pack(">HhH", 1, 321, 0))
        call("Set" + stem + "ContentInfo", [control, content])
        call("Get" + stem + "ContentInfo", [control, output])
        load(24)
        p.expect(0x00010141, "the native control retains the resource-based content record")
        transform = 0x4201
        call("Set" + stem + "Transform", [control, transform])
        call("GetControlData", [control, 0, int.from_bytes(b"tran", "big"), 2, output, ptr(base + 28)])
        load(24, 2)
        p.expect(transform, stem + " retains the complete icon transform word")
        load(28)
        p.expect(2, stem + " reports a two-byte icon transform")
        call("Set" + stem + "Transform", [control, 0])
    call("GetBevelButtonMenuValue", [bevel, output])
    load(24, 2)
    p.expect(0, "an unattached bevel menu begins with no choice")
    probe.call("SetBevelButtonMenuValue", [bevel, 0], side=1)
    save(28)
    probe.call("SetBevelButtonMenuValue", [bevel, 0])
    p.emit(0x809E0000 | (base + 28))
    p.equal("an unattached bevel returns its definition's menu update status")
    probe.call("GetBevelButtonMenuHandle", [bevel, output], side=1)
    save(28)
    probe.call("GetBevelButtonMenuHandle", [bevel, output])
    p.emit(0x809E0000 | (base + 28))
    p.equal("an unattached bevel returns its definition's menu query status")
    call("SetThemeTextColor", [2, 32, 1])
    call("DrawThemeMenuSeparator", [control_rect])
    call("DrawThemeFocusRect", [control_rect, 1])
    probe.call("DrawControlInCurrentPort", [bevel])
    probe.call("SetPort", [port])
    probe.call("DisposeWindow", [window])


def menu_clock_operations(probe):
    p = probe.p
    _, clock = probe.far_raw(bytes(64))
    invalid_config = probe.far_raw(b"\x00\x01" + bytes(20))
    for side in (0, 1):
        output = probe.ptr(clock + 32 * side)
        probe.call("GetMenuBarClockConfig", [output], side=side)
        p.expect(0, "read a version-zero menu clock configuration")
        probe.call("GetMenuBarClockConfig", [invalid_config], side=side)
        p.expect(-50, "reject a menu clock configuration with an unknown version")
        probe.call("SetMenuBarClockConfig", [invalid_config], side=side)
        p.expect(-50, "reject changing the clock with an unknown version")
        probe.call("IsMenuBarClockEnabled", side=side)
        probe.arguments([probe.ptr(clock + 24 + 32 * side)], 1)
        p.emit(0x90640000)
        probe.arguments([output], 0)
        p.emit(0x88630002, 0x5463D1BE)
        probe.arguments([probe.slot(clock + 24 + 32 * side)], 1)
        p.equal("the enabled query matches the configuration's enabled bit")
        probe.call("RefreshMenuBarClock", side=side)
        p.expect(0, "request a clock redraw")

    for offset in range(0, 22, 2):
        probe.arguments([probe.ptr(clock + offset), probe.ptr(clock + 32 + offset)], 0)
        p.emit(0xA0630000, 0xA0840000)
        p.equal("native and saved menu clock configurations agree")

    for side in (0, 1):
        probe.call("EnableMenuBarClock", side=side)
        probe.call("IsMenuBarClockEnabled", side=side)
        p.expect(1, "enabling the clock publishes its active state")
        probe.call("EnableMenuBarClock", side=side)
        p.expect(-5621, "reject enabling an already active clock")
        probe.call("DisableMenuBarClock", side=side)
        probe.call("IsMenuBarClockEnabled", side=side)
        p.expect(0, "disabling the clock releases its active state")
        probe.call("DisableMenuBarClock", side=side)
        p.expect(-5620, "reject disabling an already inactive clock")

        probe.arguments([probe.slot(clock + 24 + 32 * side)], 0)
        p.emit(0x2C030000)
        skip_enable = len(p.code)
        p.emit(0)
        probe.call("EnableMenuBarClock", side=side)
        p.code[skip_enable] = 0x41820000 | (len(p.code) - skip_enable) * 4
        probe.call("SetMenuBarClockConfig", [probe.ptr(clock + 32 * side)], side=side)
        probe.call("IsMenuBarClockEnabled", side=side)
        probe.arguments([probe.slot(clock + 24 + 32 * side)], 1)
        p.equal("restoring the configuration preserves the original clock state")


def menu_manager_operations(probe):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    _, base = probe.raw(bytes(32))
    menu = slot(base)
    size, output = ptr(base + 4), ptr(base + 8)
    creator, tag = 0x54455354, 0x44415441
    data = probe.raw(b"ABCD")

    def call(name, args=(), expected=0):
        probe.call(name, args)
        p.expect(expected, name + " returns the expected menu result")

    def word(offset, expected, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(expected, label)

    # Establish the native menu geometry after the earlier original-library redraw tests.
    probe.call("HideMenuBar")
    probe.call("ShowMenuBar")
    probe.call("GetGrayRgn")
    p.nonzero("the Window Manager has a desktop region")
    p.emit(0x907E0000 | (base + 12))
    desktop, strip_point = slot(base + 12), 0x0005000A
    call("IsMenuBarVisible", expected=1)
    call("PtInRgn", [strip_point, desktop])
    p.emit(0xA0600B20, 0xB07E0000 | (base + 24), 0x38600000, 0xB0600B20)
    call("IsMenuBarInvalid", [0])
    for flag in (0x40, 0x4000):
        p.emit(0x38600000 | flag, 0xB0600B20)
        call("IsMenuBarInvalid", [0], 1)
    p.emit(0xA07E0000 | (base + 24), 0xB0600B20)
    probe.call("HideMenuBar")
    probe.call("HideMenuBar")
    call("IsMenuBarVisible")
    call("PtInRgn", [strip_point, desktop], 1)
    probe.call("ShowMenuBar")
    probe.call("ShowMenuBar")
    call("IsMenuBarVisible", expected=1)
    call("PtInRgn", [strip_point, desktop])

    p.emit(0xA0600BAA, 0x907E0000 | (base + 16))
    probe.call("SetMenuBarHeight", [10])
    p.emit(0xA0600BAA)
    p.expect(10, "the menu bar publishes its new height")
    call("PtInRgn", [0x0010000A, desktop], 1)
    probe.call("SetMenuBarHeight", [24])
    call("PtInRgn", [0x0010000A, desktop])
    probe.call("HideMenuBar")
    probe.call("SetMenuBarHeight", [12])
    call("IsMenuBarVisible")
    probe.call("ShowMenuBar")
    p.emit(0xA0600BAA)
    p.expect(12, "showing a hidden menu bar restores the requested height")
    call("PtInRgn", [0x0010000A, desktop], 1)
    probe.call("SetMenuBarHeight", [slot(base + 16)])
    p.emit(0xA0600B20, 0xB07E0000 | (base + 24), 0x38604040, 0xB0600B20)
    probe.call("ValidateMenuBar")
    call("IsMenuBarInvalid", [0])
    p.emit(0xA07E0000 | (base + 24), 0xB0600B20)

    probe.call("NewMenu", [8319, probe.raw(b"\x04Menu")])
    p.nonzero("allocate the menu for state and property checks")
    p.emit(0x907E0000 | base)
    items = b";".join([b"Item"] * 34)
    probe.call("AppendMenu", [menu, probe.raw(bytes([len(items)]) + items)])
    call("CountMenuItems", [menu], 34)
    probe.call("InsertMenu", [menu, 0])
    probe.call("NewMenu", [8321, probe.far_raw(b"\x07Submenu")])
    p.nonzero("allocate a hierarchical menu")
    p.emit(0x907E0000 | (base + 16))
    submenu = slot(base + 16)
    probe.call("InsertMenu", [submenu, -1])
    for handle in (menu, submenu):
        probe.call("SetMenuWidth", [handle, 80])
        probe.call("SetMenuHeight", [handle, 24])
    probe.call("InvalidateAllMenuSizes")
    for handle in (menu, submenu):
        call("IsMenuSizeInvalid", [handle], 1)
    probe.call("DeleteMenu", [8321])
    probe.call("DisposeMenu", [submenu])
    probe.call("DeleteMenu", [8319])
    for side in (0, 1):
        probe.call("CheckMenuItem", [menu, 1, 1], side=side)
        probe.call("GetItemMark", [menu, 1, output])
        p.emit(0xA07E0000 | (base + 8))
        p.expect(0x12, "checked item uses the classic menu check mark")
        probe.call("CheckMenuItem", [menu, 1, 0], side=1 - side)
        probe.call("GetItemMark", [menu, 1, output])
        p.emit(0xA07E0000 | (base + 8))
        p.expect(0, "clearing a check removes the menu mark")
    call("IsMenuItemEnabled", [menu, 34], 1)
    probe.call("DisableMenuItem", [menu, 31])
    probe.call("DisableMenuItem", [menu, 32])
    call("IsMenuItemEnabled", [menu, 31])
    call("IsMenuItemEnabled", [menu, 32])
    call("IsMenuItemEnabled", [menu, 1], 1)
    call("IsMenuItemEnabled", [menu, 0], 1)
    probe.call("DeleteMenuItem", [menu, 1])
    call("IsMenuItemEnabled", [menu, 30])
    call("IsMenuItemEnabled", [menu, 31])
    call("IsMenuItemEnabled", [menu, 32], 1)
    probe.call("InsertMenuItem", [menu, probe.raw(b"\x06Insert"), 0])
    call("IsMenuItemEnabled", [menu, 1], 1)
    call("IsMenuItemEnabled", [menu, 31])
    call("IsMenuItemEnabled", [menu, 32])
    call("IsMenuItemEnabled", [menu, 33], 1)
    probe.call("DisableAllMenuItems", [menu])
    call("MenuHasEnabledItems", [menu])
    call("IsMenuItemEnabled", [menu, 0], 1)
    probe.call("EnableMenuItem", [menu, 33])
    call("MenuHasEnabledItems", [menu], 1)
    probe.call("EnableAllMenuItems", [menu])
    call("IsMenuItemEnabled", [menu, 1], 1)
    call("IsMenuItemEnabled", [menu, 32], 1)
    call("ChangeMenuItemAttributes", [menu, 32, 1, 0])
    call("IsMenuItemEnabled", [menu, 32])
    call("GetMenuItemAttributes", [menu, 32, ptr(base + 20)])
    p.emit(0x807E0000 | (base + 20), 0x70630001)
    p.expect(1, "item attributes share the extended enabled state above item 31")
    call("ChangeMenuItemAttributes", [menu, 32, 0, 1])
    call("IsMenuItemEnabled", [menu, 32], 1)

    call("GetMenuItemPropertySize", [menu, 32, creator, tag, size], -5604)
    call("SetMenuItemProperty", [menu, 32, creator, tag, 4, data])
    call("SetMenuItemProperty", [menu, 0, creator, tag, 0, 0])
    call("GetMenuItemPropertySize", [menu, 0, creator, tag, size])
    word(4, 0, "a zero-length menu-level property exists")
    call("GetMenuItemPropertySize", [menu, 32, creator, tag, size])
    word(4, 4, "the item property is independent of the menu-level property")
    call("GetMenuItemProperty", [menu, 32, creator, tag, 2, size, output])
    word(4, 4, "a short property read reports the full size")
    word(8, 0x41420000, "a short property read preserves bytes beyond the buffer")
    call("ChangeMenuItemPropertyAttributes", [menu, 32, creator, tag, 1, 0])
    call("SetMenuItemProperty", [menu, 32, creator, tag, 4, probe.raw(b"new!")])
    call("GetMenuItemPropertyAttributes", [menu, 32, creator, tag, size])
    word(4, 1, "property replacement keeps its attributes")
    call("SetMenuItemSystemProperty", [menu, 32, tag, 4, data])
    call("GetMenuItemSystemPropertySize", [menu, 32, tag, size])
    word(4, 4, "the system property has its own namespace")
    call("GetMenuItemPropertySize", [menu, 32, 0x6D616373, tag, size], -5603)
    call("ChangeMenuItemSystemPropertyAttributes", [menu, 32, tag, 1, 0])
    call("GetMenuItemSystemPropertyAttributes", [menu, 32, tag, size])
    word(4, 1, "system property attributes can be queried")
    probe.call("DeleteMenuItem", [menu, 1])
    call("GetMenuItemProperty", [menu, 31, creator, tag, 4, size, output])
    word(8, 0x6E657721, "user property moves with its item after deletion")
    call("GetMenuItemPropertySize", [menu, 32, creator, tag, size], -5604)
    call("GetMenuItemSystemProperty", [menu, 31, tag, 4, size, output])
    word(8, 0x41424344, "system property moves independently with the same item")
    call("RemoveMenuItemSystemProperty", [menu, 31, tag])
    call("GetMenuItemSystemPropertySize", [menu, 31, tag, size], -5604)
    call("RemoveMenuItemProperty", [menu, 31, creator, tag])
    call("GetMenuItemPropertySize", [menu, 31, creator, tag, size], -5604)
    call("RemoveMenuItemProperty", [menu, 0, creator, tag])
    probe.call("DisposeMenu", [menu])

    call("CreateNewMenu", [8324, 0x10, ptr(base)])
    call("GetMenuAttributes", [menu, output])
    word(8, 0x10, "CreateNewMenu applies the requested attributes")
    probe.call("AppendMenu", [menu, probe.far_raw(b"\x06Indent")])
    call("GetMenuItemAttributes", [menu, 1, output])
    call("IsMenuItemIconEnabled", [menu, 1], 1)
    call("DisableMenuItemIcon", [menu, 1])
    call("IsMenuItemIconEnabled", [menu, 1])
    call("SetSubmenuParentChoosable", [menu, 1, 1])
    call("GetSubmenuParentChoosable", [menu, 1, output])
    p.emit(0x887E0000 | (base + 8))
    p.expect(1, "the submenu-parent getter reads the enabled attribute")
    call("ChangeMenuItemAttributes", [menu, 1, 1, 0])
    call("IsMenuItemEnabled", [menu, 1])
    call("GetMenuItemAttributes", [menu, 1, output])
    p.emit(0x807E0000 | (base + 8), 0x70630007)
    p.expect(7, "item attributes expose the disabled item, disabled icon, and choosable parent bits")
    call("EnableMenuItemIcon", [menu, 1])
    call("IsMenuItemIconEnabled", [menu, 1], 1)
    call("SetSubmenuParentChoosable", [menu, 1, 0])
    call("GetSubmenuParentChoosable", [menu, 1, output])
    p.emit(0x887E0000 | (base + 8))
    p.expect(0, "the submenu-parent getter reads the cleared attribute")
    call("ChangeMenuItemAttributes", [menu, 1, 0, 1])
    call("IsMenuItemEnabled", [menu, 1], 1)
    call("GetMenuItemAttributes", [menu, 1, output])
    p.emit(0x807E0000 | (base + 8), 0x70630007)
    p.expect(0, "clearing item attributes restores the classic state")
    call("GetMenuItemAttributes", [menu, 2, output], -50)
    call("IsMenuItemInvalid", [menu, 2])
    call("InvalidateMenuItems", [0, 1, 1], -5623)
    call("InvalidateMenuItems", [menu, 0, 1], -50)
    call("InvalidateMenuItems", [menu, 1, 2], -50)
    call("InvalidateMenuItems", [menu, 1, 1])
    call("IsMenuItemInvalid", [menu, 1])
    call("InvalidateMenuEnabling", [menu])
    call("InvalidateMenuKeyCache", [menu, 1, 1, 1])
    # Save the MenuInfo definition handle and select the built-in classic MDEF.
    probe.call("GetResource", [0x4D444546, 0])
    p.nonzero("load the classic MDEF for the native menu layout checks")
    p.emit(0x809E0000 | base, 0x80840000, 0xA0A40006, 0xB0BE0000 | (base + 20))
    p.emit(0xA0A40008, 0xB0BE0000 | (base + 22), 0x5465843E, 0xB0A40006, 0xB0640008)
    probe.call("CalcMenuSize", [menu])
    call("IsMenuSizeInvalid", [menu])
    probe.call("GetMenuWidth", [menu])
    p.emit(0x907E0000 | (base + 16))
    call("SetMenuItemIndent", [menu, 1, 10])
    probe.call("CalcMenuSize", [menu])
    probe.call("GetMenuWidth", [menu])
    p.emit(0x809E0000 | (base + 16), 0x7C641850)
    p.expect(10, "the standard MDEF includes the item indent in menu measurement")
    call("GetMenuExcludesMarkColumn", [menu])
    probe.call("GetMenuWidth", [menu])
    p.emit(0x907E0000 | (base + 16))
    call("SetMenuExcludesMarkColumn", [menu, 1])
    call("GetMenuExcludesMarkColumn", [menu], 1)
    call("IsMenuSizeInvalid", [menu], 1)
    probe.call("CalcMenuSize", [menu])
    probe.call("GetMenuWidth", [menu])
    p.emit(0x809E0000 | (base + 16), 0x7C632050)
    p.nonzero("excluding the mark column changes the measured width")
    p.emit(0x54630FFE)
    p.expect(0, "excluding the mark column reduces the measured width")
    call("ChangeMenuAttributes", [menu, 0, 1])
    call("GetMenuExcludesMarkColumn", [menu])
    call("GetMenuFont", [menu, output, ptr(base + 10)])
    call("SetMenuFont", [menu, 0, 9])
    probe.call("CalcMenuSize", [menu])
    probe.call("GetMenuHeight", [menu])
    p.emit(0x907E0000 | (base + 16))
    call("SetMenuFont", [menu, 0, 24])
    call("GetMenuFont", [menu, output, ptr(base + 10)])
    word(8, 0x00000018, "menu font family and size round-trip in separate words")
    call("IsMenuSizeInvalid", [menu], 1)
    probe.call("CalcMenuSize", [menu])
    probe.call("GetMenuHeight", [menu])
    p.emit(0x809E0000 | (base + 16), 0x7C641850)
    p.nonzero("changing menu font size changes the measured row height")
    p.emit(0x54630FFE)
    p.expect(0, "a larger menu font increases the measured row height")
    call("GetMenuItemFontID", [menu, 1, output])
    p.emit(0xA07E0000 | (base + 8))
    p.expect(0, "a new menu item uses the menu font")
    call("SetMenuItemFontID", [menu, 1, 4])
    call("GetMenuItemFontID", [menu, 1, output])
    p.emit(0xA07E0000 | (base + 8))
    p.expect(4, "the item font override round-trips")
    call("IsMenuSizeInvalid", [menu], 1)
    call("SetMenuItemFontID", [menu, 1, 0])
    call("SetMenuFont", [menu, 0, 9])
    icon = slot(base + 28)
    probe.call("NewHandleClear", [128])
    p.nonzero("allocate a classic ICON")
    p.emit(0x907E0000 | (base + 28))
    call("SetMenuItemIconHandle", [menu, 1, 1, icon])
    call("GetMenuItemIconHandle", [menu, 1, output, ptr(base + 12)])
    p.emit(0x887E0000 | (base + 8))
    p.expect(1, "the classic ICON type round-trips")
    p.emit(0x807E0000 | (base + 12), 0x809E0000 | (base + 28))
    p.equal("the caller's ICON handle round-trips")
    call("IsMenuSizeInvalid", [menu], 1)
    probe.call("CalcMenuSize", [menu])
    probe.call("GetMenuHeight", [menu])
    p.emit(0x907E0000 | (base + 16))
    call("SetMenuItemIconHandle", [menu, 1, 0, 0])
    probe.call("CalcMenuSize", [menu])
    probe.call("GetMenuHeight", [menu])
    p.emit(0x809E0000 | (base + 16), 0x7C632050)
    p.nonzero("the attached ICON increases the measured menu height")
    p.emit(0x54630FFE)
    p.expect(0, "removing the ICON reduces the measured menu height")
    probe.call("GetHandleSize", [icon])
    p.expect(128, "the caller retains ownership of the detached ICON")
    probe.call("DisposeHandle", [icon])
    call("SetMenuItemIndent", [menu, 1, 0])
    call("GetMenuItemIndent", [menu, 1, output])
    word(8, 0, "new menu items start with a zero indent")
    call("SetMenuItemIndent", [menu, 1, 0x01020304])
    call("GetMenuItemIndent", [menu, 1, output])
    word(8, 0x01020304, "menu indentation retains all four bytes")
    call("GetMenuItemSystemProperty", [menu, 1, 0x69646E74, 4, size, output])
    word(8, 0x01020304, "the packed indent is visible through the system property API")
    call("GetMenuItemIndent", [menu, 2, output], -50)
    word(8, 0x01020304, "an invalid item does not modify the indent output")
    for virtual, key in ((0, ord("Q")), (1, 0x7A)):
        call("SetMenuItemCommandKey", [menu, 1, virtual, key])
        call("GetMenuItemCommandKey", [menu, 1, virtual, output])
        p.emit(0xA07E0000 | (base + 8))
        p.expect(key, "the selected character or virtual command key round-trips")
    probe.call("SetMenuWidth", [menu, 70])
    probe.call("SetMenuHeight", [menu, 42])
    call("InvalidateMenuSize", [menu])
    call("IsMenuSizeInvalid", [menu], 1)
    call("GetMenuWidth", [menu], -1)
    call("GetMenuHeight", [menu], -1)
    call("UpdateInvalidMenuItems", [0], -5623)
    call("GetMenuTrackingData", [menu, output], -5620)
    call("UpdateInvalidMenuItems", [menu])
    call("IsMenuSizeInvalid", [menu])
    # Restore the real definition before letting the saved Menu Manager dispose the menu.
    p.emit(0x809E0000 | base, 0x80840000, 0xA0BE0000 | (base + 20), 0xB0A40006)
    p.emit(0xA0BE0000 | (base + 22), 0xB0A40008)

    # Supply two classic MBSaveLoc entries, preserving the actual menu bar save handle.
    tracking = bytearray(84)
    struct.pack_into(">h", tracking, 0, 56)
    struct.pack_into(">hhI", tracking, 48, -23, 119, 0x00030002)
    struct.pack_into(">IhhI", tracking, 72, 0x12345678, -1, -1, 0x00050004)
    source = probe.far_raw(tracking)
    probe.call("NewHandle", [len(tracking)])
    p.nonzero("allocate a classic menu tracking save handle")
    p.emit(0x907E0000 | (base + 8), 0x80600B5C, 0x907E0000 | (base + 4))
    probe.arguments([source, slot(base + 8), len(tracking)], 0)
    p.emit(0x80840000)
    p.call("BlockMoveData")
    p.emit(0x807E0000 | (base + 8), 0x80830000, 0x80BE0000 | base, 0x90A4002C, 0x90600B5C)
    call("GetMenuTrackingData", [menu, output])
    p.emit(0x807E0000 | (base + 8), 0x809E0000 | base)
    p.equal("tracking data finds the requested non-topmost menu")
    word(12, 0x00020003, "tracking data exposes the selected and hovered item numbers")
    word(16, 0, "tracking data clears the legacy item rectangle")
    word(20, 0, "tracking data clears all four item rectangle coordinates")
    word(24, -23, "a non-topmost menu uses its saved signed top coordinate")
    word(28, 119, "a non-topmost menu uses its saved bottom coordinate")
    call("GetMenuTrackingData", [0, output])
    word(8, 0x12345678, "a null menu selects the topmost tracking record")
    word(12, 0x00040005, "the topmost record also decodes selected and hovered items")
    p.emit(0x807E0000 | (base + 24), 0xA8800A0A)
    p.equal("the topmost menu uses the live signed top coordinate")
    p.emit(0x807E0000 | (base + 28), 0xA8800A0C)
    p.equal("the topmost menu uses the live signed bottom coordinate")
    probe.call("GetPort", [ptr(base + 16)])
    call("InvalidateMenuSize", [menu])
    call("UpdateInvalidMenuItems", [menu])
    call("IsMenuSizeInvalid", [menu])
    probe.call("GetPort", [ptr(base + 20)])
    p.emit(0x807E0000 | (base + 16), 0x809E0000 | (base + 20))
    p.equal("updating a tracked menu restores the caller's graphics port")
    call("GetMenuTrackingData", [menu, output])
    word(24, -23, "updating a tracked menu preserves its saved scroll position")
    p.emit(0x80600B5C, 0x80830000, 0x38A07FFF, 0xB0A40000)
    call("GetMenuTrackingData", [0, output], -5620)
    word(24, -23, "an out-of-bounds save record leaves the tracking output intact")
    p.emit(0x80600B5C, 0x809E0000 | (base + 4), 0x90800B5C)
    p.call("DisposeHandle")
    probe.call("DisposeMenu", [menu])


def menu_command_operations(probe):
    p = probe.p
    _, base = probe.raw(bytes(48))
    menu = probe.slot(base)
    size, output, found, index = (probe.ptr(base + offset) for offset in (4, 8, 16, 20))
    command, other, creator, tag = 0x74657374, 0x74657332, 0x54455354, 0x44415441
    data = probe.raw(b"ABCD")

    def call(name, args=(), expected=0, side=0):
        probe.call(name, args, side=side)
        p.expect(expected, name + " returns the expected result")

    def word(offset, expected, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(expected, label)

    probe.call("NewMenu", [8320, probe.raw(b"\x04Menu")])
    p.nonzero("create the menu for command queries")
    p.emit(0x907E0000 | base)
    probe.call("AppendMenu", [menu, probe.raw(b"\x0dOne;Two;Three")])
    call("GetMenuItemCommandID", [menu, 1, output])
    word(8, 0, "classic menu items have no command until assigned")
    for item, identifier in ((1, command), (2, other), (3, command)):
        call("SetMenuItemCommandID", [menu, item, identifier])
        call("GetMenuItemCommandID", [menu, item, output])
        word(8, identifier, "item command reads back in guest byte order")
    call("CountMenuItemsWithCommandID", [menu, 0])
    call("GetIndMenuItemWithCommandID", [menu, command, 0, found, index], -50)
    call("GetIndMenuItemWithCommandID", [menu, command, 3, found, index], -5622)
    for side in (0, 1):
        call("CountMenuItemsWithCommandID", [menu, command], 2, side=side)
        call("GetIndMenuItemWithCommandID", [menu, command, 2, found, index], side=side)
        p.emit(0x807E0000 | (base + 16), 0x809E0000 | base)
        p.equal("command lookup returns the owning menu")
        p.emit(0xA07E0000 | (base + 20))
        p.expect(3, "command lookup enumerates duplicate IDs by item order")

    probe.call("InsertMenu", [menu, 0])
    call("CountMenuItemsWithCommandID", [0, command], 2)
    call("GetIndMenuItemWithCommandID", [0, command, 1, found, index])
    p.emit(0x807E0000 | (base + 16), 0x809E0000 | base)
    p.equal("a null menu searches the menu bar")
    probe.call("NewMenu", [99, probe.raw(b"\x03Sub")])
    p.nonzero("create a hierarchical menu")
    p.emit(0x907E0000 | (base + 24))
    child = probe.slot(base + 24)
    probe.call("AppendMenu", [child, probe.raw(b"\x06Nested")])
    probe.call("InsertMenu", [child, -1])
    probe.call("SetItemCmd", [menu, 2, 0x1B])
    probe.call("SetItemMark", [menu, 2, 99])
    call("SetMenuItemCommandID", [child, 1, command])
    call("CountMenuItemsWithCommandID", [0, command], 3)
    call("GetIndMenuItemWithCommandID", [menu, command, 2, found, index])
    p.emit(0x807E0000 | (base + 16), 0x809E0000 | (base + 24))
    p.equal("command lookup descends into the child in item order")
    p.emit(0xA07E0000 | (base + 20))
    p.expect(1, "nested command lookup returns the child item index")
    probe.call("SetItemCmd", [menu, 2, 0])
    probe.call("SetItemMark", [menu, 2, 0])
    probe.call("DeleteMenu", [99])
    probe.call("DisposeMenu", [child])
    probe.call("DeleteMenu", [8320])

    call("IsMenuCommandEnabled", [menu, command], 1)
    probe.call("DisableMenuCommand", [menu, command])
    call("IsMenuCommandEnabled", [menu, command])
    call("IsMenuItemEnabled", [menu, 3], 1)
    probe.call("EnableMenuCommand", [menu, command])
    call("IsMenuCommandEnabled", [menu, command], 1)
    probe.call("SetMenuCommandMark", [menu, command, 0x12])
    probe.call("GetMenuCommandMark", [menu, command, output])
    p.emit(0xA07E0000 | (base + 8))
    p.expect(0x12, "command mark updates its first item")

    key = [menu, command, creator, tag]
    call("GetMenuCommandPropertySize", [*key, size], -5604)
    call("SetMenuCommandProperty", [*key, 4, data])
    call("GetMenuCommandPropertySize", [*key, size])
    word(4, 4, "the command property reports the original size")
    call("GetMenuCommandProperty", [*key, 4, size, output])
    word(8, 0x41424344, "the command property returns its payload")
    call("GetMenuItemProperty", [menu, 1, creator, tag, 4, size, output])
    word(8, 0x41424344, "command and item property calls share storage")
    call("RemoveMenuCommandProperty", key)
    call("GetMenuItemPropertySize", [menu, 1, creator, tag, size], -5604)
    call("SetMenuItemCommandID", [menu, 1, 0])
    call("CountMenuItemsWithCommandID", [menu, command], 1, side=1)
    probe.call("DeleteMenuItem", [menu, 1])
    call("GetMenuItemCommandID", [menu, 2, output])
    word(8, command, "a command follows its item after deletion")
    call("CountMenuItemsWithCommandID", [menu, command], 1)
    probe.call("DisposeMenu", [menu])


def menu_structure_operations(probe):
    p = probe.p
    _, base = probe.raw(bytes(64))
    ptr = lambda offset: probe.ptr(base + offset)
    slot = lambda offset: probe.slot(base + offset)
    label = probe.raw(b"\x04Menu")
    literal = probe.raw(b"\x03A/B")
    item_text = probe.far_raw(bytes(256))
    procedure = probe.raw(bytes(8))
    p.routines.append((procedure[1], [0x4E800020]))

    def expect(name, args=(), status=0, side=0):
        probe.call(name, args, side=side)
        p.expect(status, name + " result")

    def store(offset):
        p.emit(0x907E0000 | (base + offset))

    def equal(first, second, description):
        p.emit(0x807E0000 | (base + first), 0x809E0000 | (base + second))
        p.equal(description)

    def new_menu(menu_id, offset):
        expect("CreateNewMenu", [menu_id, 0x10, ptr(offset)])
        p.emit(0x807E0000 | (base + offset))
        p.nonzero("allocate a menu for the hierarchy fixture")
        probe.call("SetMenuTitle", [slot(offset), label])

    for side in (0, 1):
        new_menu(8570, 0)
        new_menu(91, 4)
        menu, child = slot(0), slot(4)
        expect("IsValidMenu", [0], 0, side)
        expect("IsValidMenu", [menu], 1, side)
        p.emit(0x38605A5A, 0xB07E0000 | (base + 48), 0xB07E0000 | (base + 52))
        for getter_side, offset in ((0, 48), (1, 52)):
            probe.call("GetMenuType", [menu, ptr(offset)], side=getter_side)
            store(offset + 8)
        equal(56, 60, "menu type query reports the same tracking state")
        p.emit(0xA07E0000 | (base + 48), 0xA09E0000 | (base + 52))
        p.equal("menu type is shared without overwriting the output when unavailable")
        probe.call("AppendMenu", [menu, label])
        expect("GetMenuItemHierarchicalMenu", [menu, 0, ptr(8)], -50, side)
        expect("SetMenuItemHierarchicalMenu", [menu, 2, child], -50, side)
        expect("SetMenuItemHierarchicalMenu", [menu, 1, child], side=side)
        expect("GetMenuItemHierarchicalMenu", [menu, 1, ptr(8)], side=1 - side)
        equal(8, 4, "submenu links are shared across the native and saved implementations")
        expect("GetMenuItemHierarchicalID", [menu, 1, ptr(12)], side=side)
        p.emit(0xA07E0000 | (base + 12))
        p.expect(91, "the hierarchical ID comes from the attached child")
        expect("SetMenuItemHierarchicalID", [menu, 1, 0], side=side)
        expect("GetMenuItemHierarchicalMenu", [menu, 1, ptr(8)], side=1 - side)
        p.emit(0x807E0000 | (base + 8))
        p.expect(0, "clearing a hierarchical ID removes the attached submenu")

        expect("AppendMenuItemText", [menu, literal], side=side)
        expect("InsertMenuItemText", [menu, label, 0])
        expect("CountMenuItems", [menu], 3)
        probe.call("GetMenuItemText", [menu, 3, item_text])
        probe.arguments([item_text], 0)
        p.emit(0x80630000)
        p.expect(0x03412F42, "append preserves a slash in the literal title")
        expect("DeleteMenuItems", [menu, 2, 3], -50, side)
        expect("DeleteMenuItems", [menu, 1, 2], side=side)
        expect("CountMenuItems", [menu], 1)
        probe.call("GetMenuItemText", [menu, 1, item_text])
        probe.arguments([item_text], 0)
        p.emit(0x80630000)
        p.expect(0x03412F42, "deleting a range preserves the remaining item")
        expect("CopyMenuItems", [0, 1, 1, child, 0], -5623, side)
        expect("CopyMenuItems", [menu, 1, 1, menu, 0], -50, side)
        expect("CopyMenuItems", [menu, 1, 2, child, 0], -50, side)
        expect("SetMenuItemCommandID", [menu, 1, 0x74657374])
        expect("CopyMenuItems", [menu, 1, 1, child, 0], side=side)
        expect("CountMenuItems", [child], 1)
        expect("GetMenuItemCommandID", [child, 1, ptr(20)])
        p.emit(0x807E0000 | (base + 20))
        p.expect(0x74657374, "range copy preserves the item's command ID")
        probe.call("GetMenuItemText", [child, 1, item_text])
        probe.arguments([item_text], 0)
        p.emit(0x80630000)
        p.expect(0x03412F42, "range copy preserves the literal item title")

        expect("SetMenuTitleIcon", [menu, 2, 0], -50, side)
        expect("GetMenuTitleIcon", [menu, ptr(12), ptr(16)], side=side)
        p.emit(0x807E0000 | (base + 12))
        p.expect(0, "a text title has no icon type")
        expect("NewIconSuite", [ptr(20)])
        expect("SetMenuTitleIcon", [menu, 5, slot(20)], side=side)
        expect("GetMenuTitleIcon", [menu, ptr(12), ptr(16)], side=1 - side)
        p.emit(0x807E0000 | (base + 12))
        p.expect(5, "the saved type identifies the installed IconSuite")
        equal(16, 20, "the getter returns the same IconSuite handle")
        probe.call("DisposeMenu", [menu])
        probe.call("DisposeMenu", [child])

    probe.call("NewMenuDefUPP", [procedure])
    p.nonzero("allocate a guest custom menu definition")
    store(28)
    for side in (0, 1):
        p.emit(0x38600000, 0x907E0000 | (base + 24))
        expect("RegisterMenuDefinition", [0, ptr(24)], -50, side)
        expect("RegisterMenuDefinition", [63, ptr(24)], -50, side)
        expect("RegisterMenuDefinition", [126, ptr(24)], side=side)
        expect("RegisterMenuDefinition", [126, 0], side=1 - side)
        expect("CreateCustomMenu", [ptr(24), 8571, 0, ptr(0)], side=side)
        p.emit(0x807E0000 | base)
        p.nonzero("the custom constructor publishes a live menu")
        expect("GetMenuDefinition", [slot(0), ptr(32)], side=1 - side)
        equal(36, 28, "custom menu definition uses the original UPP")
        expect("SetMenuDefinition", [slot(0), ptr(24)], side=side)
        expect("GetMenuDefinition", [slot(0), ptr(32)], side=1 - side)
        equal(36, 28, "updating the definition leaves the live callback installed")
        p.emit(0x38605A5A, 0xB07E0000 | (base + 48))
        probe.call("CallMenuDef", [slot(0), 2, ptr(52), 0, ptr(48)], side)
        p.emit(0xA07E0000 | (base + 48))
        p.expect(0x5A5A, "the no-op custom definition leaves the selected-item output alone")
        expect("_ScrollMenuImage", [0, ptr(52), 0, 0], -5623, side)
        expect("_ScrollMenuImage", [slot(0), ptr(52), 0, 0], side=side)
        probe.call("AppendResMenu", [slot(0), 0x4E4F5045], side)
        probe.call("InsertIntlResMenu", [slot(0), 0x4E4F5045, 0, 0], side)
        expect("CountMenuItems", [slot(0)], 0)
        probe.call("DisposeMenu", [slot(0)])
        p.emit(0x38600001, 0x907E0000 | (base + 24))
        expect("CreateCustomMenu", [ptr(24), 8571, 0, ptr(0)], -50, side)
        p.emit(0x807E0000 | base)
        p.expect(0, "unsupported menu definition rolls back the new menu")
    probe.call("DisposeMenuDefUPP", [slot(28)])

    for side in (0, 1):
        new_menu(8573, 0)
        probe.call("InsertFontResMenu", [slot(0), 0, 0], side)
        probe.call("CountMenuItems", [slot(0)])
        store(48 + side * 4)
        probe.call("DisposeMenu", [slot(0)])
    equal(48, 52, "script-filtered font insertion sees the same loaded resources")

    probe.call("GetMenuBar")
    p.nonzero("copy the current menu bar before testing duplication")
    store(44)
    for side in (0, 1):
        expect("DuplicateMenuBar", [0, ptr(48)], -50, side)
        expect("DisposeMenuBar", [0], -50, side)
        expect("DuplicateMenuBar", [slot(44), ptr(48)], side=side)
        p.emit(0x807E0000 | (base + 48))
        p.nonzero("menu bar duplication publishes a new handle")
        p.emit(0x807E0000 | (base + 48), 0x809E0000 | (base + 44), 0x7C632278)
        p.nonzero("the copied menu bar has its own handle")
        expect("DisposeMenuBar", [slot(48)], side=1 - side)
    probe.call("DisposeHandle", [slot(44)])

    probe.call("AcquireRootMenu")
    p.nonzero("capture the active root to restore it after testing")
    store(40)
    for side in (0, 1):
        new_menu(8572, 0)
        expect("SetRootMenu", [slot(0)], side=side)
        probe.call("AcquireRootMenu", side=1 - side)
        p.emit(0x809E0000 | base)
        p.equal("root changes are visible to the other implementation")
        expect("ReleaseMenu", [slot(0)])
        expect("SetRootMenu", [slot(40)], side=1 - side)
        expect("ReleaseMenu", [slot(0)])
    expect("ReleaseMenu", [slot(40)])


def help_manager_operations(probe):
    p = probe.p
    _, base = probe.raw(bytes(40))
    ptr = lambda offset: probe.ptr(base + offset)
    slot = lambda offset: probe.slot(base + offset)

    def checked(name, arguments=(), status=0, side=0):
        probe.call(name, arguments, side)
        p.expect(status, name + " returns the expected status")

    def store(offset):
        p.emit(0x907E0000 | (base + offset))

    def word(offset, value, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(value, label)

    bounds = probe.raw(struct.pack(">4h", 50, 50, 130, 210))
    title = probe.raw(b"\x04Help")
    probe.call("NewWindow", [0, bounds, title, 0, 0, -1, 0, 0])
    p.nonzero("allocate a window for help properties")
    store(0)
    probe.call("NewControl", [slot(0), bounds, title, 0, 0, 0, 1, 0, 0])
    p.nonzero("allocate a control for help properties")
    store(4)
    checked("CreateNewMenu", [8585, 0x10, ptr(8)])
    p.emit(0x807E0000 | (base + 8))
    p.nonzero("allocate a menu for help properties")
    probe.call("SetMenuTitle", [slot(8), title])
    probe.call("AppendMenu", [slot(8), title])

    routine = probe.raw(bytes(8))
    p.routines.append((routine[1], [0x38600000, 0x4E800020]))
    probe.call("NewHMControlContentUPP", [routine])
    p.nonzero("allocate a Help Manager callback descriptor")
    store(12)
    for family, target in (("Control", slot(4)), ("Window", slot(0)), ("MenuItem", slot(8)), ("MenuTitle", slot(8))):
        get = f"HMGet{family}ContentCallback"
        install = f"HMInstall{family}ContentCallback"
        checked(get, [target, 0], -50)
        checked(get, [target, ptr(16)])
        word(16, 0, "unset help callback returns a null pointer")
        for side in (0, 1):
            checked(install, [target, slot(12)], side=side)
            checked(get, [target, ptr(16)], side=1 - side)
            p.emit(0x807E0000 | (base + 16), 0x809E0000 | (base + 12))
            p.equal("native and saved Help Manager APIs see the same installed callback")
            checked(install, [target, 0], side=1 - side)
            checked(get, [target, ptr(16)], side=side)
            word(16, 0, "removing a help callback is visible on both paths")
    probe.call("DisposeHMControlContentUPP", [slot(12)])

    record = bytearray(0x216)
    struct.pack_into(">4h", record, 4, 71, 81, 92, 111)
    struct.pack_into(">I5s", record, 0xE, 0x70737472, b"\x04Hint")
    source = probe.far_raw(record)
    output = probe.far_raw(bytes(0x218))
    _, menu_item = probe.raw(bytes(24))
    p.emit(0x807E0000 | (base + 8), 0x907E0000 | menu_item, 0x38600001, 0xB07E0000 | (menu_item + 6))
    for family, owner, query in (
        ("Window", [slot(0)], [slot(0), 0, 0]),
        ("Control", [slot(4)], [slot(4), 0, 0]),
        ("MenuItem", [slot(8), 1], [probe.ptr(menu_item), 0]),
    ):
        get, set_content = f"HMGet{family}HelpContent", f"HMSet{family}HelpContent"
        checked(get, [*owner, 0], -50)
        for side in (0, 1):
            checked(set_content, [*owner, source], side=side)
            checked(get, [*owner, output], side=1 - side)
            probe.arguments([output], 0)
            p.emit(0x80630004)
            p.expect(0x00470051, "the Help Manager preserves explicit content bounds")
            checked(f"HMGet{family}Content", [*query, ptr(20), output], side=side)
            p.emit(0xA07E0000 | (base + 20))
            p.expect(0, "the high-level help query accepts the stored content")
            checked(set_content, [*owner, 0], side=1 - side)
        checked(get, [*owner, output], -5604)
    checked("HMGetMenuTitleContent", [slot(8), 0, 0, output], -50)
    checked("HMGetMenuTitleContent", [slot(8), 0, ptr(20), output])
    p.emit(0x38600000, 0xB07E0000 | (menu_item + 6))
    checked("HMSetMenuItem", [probe.ptr(menu_item)])

    probe.call("HMAreHelpTagsDisplayed")
    store(24)
    checked("HMGetTagDelay", [ptr(28)])
    for side, ticks, shown in ((0, 45, 1), (1, -120, 0)):
        checked("HMSetHelpTagsDisplayed", [shown], side=side)
        checked("HMAreHelpTagsDisplayed", status=shown, side=1 - side)
        checked("HMSetTagDelay", [ticks], side=side)
        checked("HMGetTagDelay", [ptr(32)], side=0)
        checked("HMGetTagDelay", [ptr(36)], side=1)
        p.emit(0x807E0000 | (base + 32), 0x809E0000 | (base + 36))
        p.equal("native and saved Help Manager paths use the same delay")
    checked("HMSetTagDelay", [slot(28)])
    checked("HMSetHelpTagsDisplayed", [slot(24)])
    checked("HMGetHelpMenu", [0, ptr(20)])
    probe.call("DisposeMenu", [slot(8)])
    probe.call("DisposeControl", [slot(4)])
    probe.call("DisposeWindow", [slot(0)])


def menu_text_operations(probe):
    p = probe.p
    _, base = probe.raw(bytes(40))
    menu, title, text, result = (probe.slot(base + offset) for offset in (0, 4, 8, 12))
    output, index = probe.ptr(base + 12), probe.ptr(base + 16)
    original_title = probe.raw(b"\x04Menu")
    original_text = probe.raw(b"\x06Caf\x8e;X")
    command = 0x74657874

    def call(name, args, expected=0, side=0):
        probe.call(name, args, side=side)
        p.expect(expected, name + " returns the expected result")

    def make_string(raw, offset):
        probe.call("CFStringCreateWithPascalString", [0, raw, 0])
        p.nonzero("create a CFString from the MacRoman fixture")
        p.emit(0x907E0000 | (base + offset))

    def check_copy(name, args, expected, side):
        call(name, [*args, output], side=side)
        call("CFEqual", [result, expected], 1)
        probe.call("CFRelease", [result])

    probe.call("NewMenu", [8321, original_title])
    p.nonzero("allocate a classic menu for CFString checks")
    p.emit(0x907E0000 | base)
    make_string(original_title, 4)
    make_string(original_text, 8)
    check_copy("CopyMenuTitleAsCFString", [menu], title, 0)
    call("SetMenuTitleWithCFString", [menu, text])
    check_copy("CopyMenuTitleAsCFString", [menu], text, 1)
    call("SetMenuTitleWithCFString", [menu, title])
    check_copy("CopyMenuTitleAsCFString", [menu], title, 0)
    call("AppendMenuItemTextWithCFString", [menu, text, 0, command, index])
    p.emit(0xA07E0000 | (base + 16))
    p.expect(1, "append returns the one-based item")
    call("CountMenuItems", [menu], 1)
    call("CountMenuItemsWithCommandID", [menu, command], 1)
    check_copy("CopyMenuItemTextAsCFString", [menu, 1], text, 0)
    call("InsertMenuItemTextWithCFString", [menu, title, 0, 0, 0])
    call("CountMenuItems", [menu], 2)
    check_copy("CopyMenuItemTextAsCFString", [menu, 1], title, 0)
    check_copy("CopyMenuItemTextAsCFString", [menu, 2], text, 0)
    call("SetMenuItemTextWithCFString", [menu, 1, text])
    check_copy("CopyMenuItemTextAsCFString", [menu, 1], text, 0)
    call("SetMenuItemTextWithCFString", [menu, 2, title])
    check_copy("CopyMenuItemTextAsCFString", [menu, 2], title, 0)
    call("SetMenuItemTextEncoding", [menu, 2, 0x600])
    call("GetMenuItemTextEncoding", [menu, 2, output])
    p.emit(0x807E0000 | (base + 12))
    p.expect(0x600, "the menu retains the item's explicit text encoding")
    call("CopyMenuTitleAsCFString", [0, output], -5623)
    call("CopyMenuItemTextAsCFString", [menu, 0, output], -50)
    call("SetMenuItemTextWithCFString", [menu, 1, 0], -50)
    probe.call("CFRelease", [text])
    probe.call("CFRelease", [title])
    probe.call("DisposeMenu", [menu])


def collection_manager_operations(probe):
    p = probe.p
    ptr, slot = probe.ptr, probe.slot
    _, base = probe.raw(bytes(288))
    collection, copy, payload, serialized, original = (slot(base + offset) for offset in (0, 4, 8, 12, 16))
    size, output, tag_out, id_out, index_out, attrs = (ptr(base + offset) for offset in (24, 32, 44, 48, 52, 56))
    tag_a, tag_b = 0x54414741, 0x54414742
    data = probe.raw(b"data")

    def call(name, arguments, expected=0, side=0):
        probe.call(name, arguments, side)
        p.expect(expected, name + " returns the expected Collection Manager result")

    def save(offset):
        p.emit(0x907E0000 | (base + offset))

    def load(offset, value, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(value, label)

    def new_at(name, offset, args=()):
        probe.call(name, args)
        p.nonzero(name + " allocates a live guest handle")
        save(offset)

    def copy_size(value):
        p.emit(0x38600000 | value)
        save(24)

    new_at("NewCollection", 0)
    call("CountCollectionOwners", [collection], 1)
    probe.call("CloneCollection", [collection])
    p.emit(0x809E0000 | base)
    p.equal("CloneCollection retains the same collection handle")
    call("CountCollectionOwners", [collection], 2)
    probe.call("DisposeCollection", [collection])
    call("RetainCollection", [collection])
    call("GetCollectionRetainCount", [collection], 2)
    call("ReleaseCollection", [collection])
    call("GetCollectionDefaultAttributes", [collection], 0x40000000)
    probe.call("SetCollectionDefaultAttributes", [collection, 3, 1])
    call("GetCollectionDefaultAttributes", [collection], 0x40000001)

    new_at("NewHandleClear", 8, [4])
    p.emit(0x80630000, 0x3C80414C, 0x60844153, 0x90830000)
    call("AddCollectionItem", [collection, tag_b, 2, 4, data])
    call("AddCollectionItem", [collection, tag_a, 7, 5, probe.raw(b"12345")])
    call("AddCollectionItemHdl", [collection, tag_a, -2, payload])
    call("CountCollectionItems", [collection], 3)
    call("CountCollectionTags", [collection], 2)
    call("CountTaggedCollectionItems", [collection, tag_a], 2)
    call("CollectionTagExists", [collection, tag_b], 1)
    call("CollectionTagExists", [collection, tag_b + 1], 0)
    call("GetIndexedCollectionTag", [collection, 2, tag_out])
    load(44, tag_b, "tag indexing is in sorted order")
    call("GetCollectionItemInfo", [collection, tag_a, 7, index_out, size, attrs])
    load(52, 2, "item indexes use signed IDs within each tag")
    load(24, 5, "item info reports the full size")
    load(56, 0x40000001, "new items acquire the default attributes")
    call("GetIndexedCollectionItemInfo", [collection, 1, tag_out, id_out, 0, 0])
    load(44, tag_a, "indexed info returns the tag")
    load(48, -2, "indexed info returns the signed ID")
    call("GetTaggedCollectionItemInfo", [collection, tag_a, 2, id_out, index_out, size, attrs])
    load(48, 7, "tagged item info returns the matching ID")
    load(52, 2, "tagged item info returns the overall item index")
    copy_size(4)
    call("GetIndexedCollectionItem", [collection, 1, size, output])
    load(32, 0x414C4153, "GetIndexedCollectionItem reads bytes added from a handle")
    copy_size(4)
    call("GetTaggedCollectionItem", [collection, tag_a, 2, size, output])
    load(24, 5, "bounded tagged reads report the full size")
    load(32, 0x31323334, "bounded tagged reads copy only the requested bytes")
    copy_size(4)
    call("GetCollectionItem", [collection, tag_b, 2, size, output])
    load(32, 0x64617461, "keyed reads retrieve independent contents")

    call("SetCollectionItemInfo", [collection, tag_b, 2, 0x80000000, 0x80000000])
    call("ReplaceIndexedCollectionItem", [collection, 3, 0, 0], -5750)
    call("SetIndexedCollectionItemInfo", [collection, 3, 0x80000000, 0])
    call("ReplaceIndexedCollectionItem", [collection, 3, 4, data])
    call("ReplaceIndexedCollectionItemHdl", [collection, 3, payload])
    call("GetCollectionItemHdl", [collection, tag_a, 7, payload])
    call("GetHandleSize", [payload], 5)
    call("GetIndexedCollectionItemHdl", [collection, 3, payload])
    call("GetHandleSize", [payload], 4)
    p.emit(0x807E0000 | (base + 8), 0x80630000, 0x80630000)
    p.expect(0x414C4153, "handle output receives the replacement bytes")

    p.routines.append((base + 80, [0x90820000 | (base + 88), 0x3860FE0B, 0x4E800020]))
    callback = ptr(base + 80)
    probe.call("SetCollectionExceptionProc", [collection, callback])
    probe.call("GetCollectionExceptionProc", [collection])
    p.pointer(base + 80, "exception callback identity is retained")
    call("GetIndexedCollectionItem", [collection, 99, 0, 0], -501)
    load(88, -5752, "exception callback receives the original index error")
    probe.call("SetCollectionExceptionProc", [collection, 0])
    call("GetCollectionExceptionProc", [collection], 0)
    call("GetCollectionItem", [collection, tag_b, 99, 0, 0], -5751)

    new_at("CopyCollection", 4, [collection, 0])
    call("CountCollectionItems", [copy], 3)
    call("RemoveIndexedCollectionItem", [copy, 3])
    call("CountCollectionItems", [copy], 2)
    call("CountCollectionItems", [collection], 3)
    new_at("NewHandleClear", 12, [0])
    new_at("NewHandleClear", 16, [0])
    call("FlattenCollectionToHdl", [collection, serialized])
    call("FlattenCollectionToHdl", [collection, original], side=1)
    for flattened in (serialized, original):
        call("GetHandleSize", [flattened], 90)
    for word, expected in ((0, 0x00010000), (4, 0x40000001), (8, 3), (60, 26), (64, 4), (68, 0x414C4153)):
        for handle_offset in (12, 16):
            p.emit(0x807E0000 | (base + handle_offset), 0x80630000, 0x80630000 | word)
            p.expect(expected, "native and saved handle streams encode the same collection fields")
    probe.call("EmptyCollection", [copy])
    call("UnflattenCollectionFromHdl", [copy, serialized], side=1)
    call("CountCollectionItems", [copy], 3)
    copy_size(4)
    call("GetCollectionItem", [copy, tag_b, 2, size, output])
    load(32, 0x414C4153, "saved Collections reads the native stream")
    call("UnflattenCollectionFromHdl", [copy, original])
    call("CountCollectionItems", [copy], 3)
    copy_size(4)
    call("GetCollectionItem", [copy, tag_a, 7, size, output])
    load(32, 0x31323334, "native Collections reads the saved stream")

    # The callback cursor and call count live next to the stream in guest memory.
    for address, reading in ((96, False), (104, True)):
        load_register, store_register = (6, 4) if reading else (4, 6)
        p.routines.append(
            (
                base + address,
                [
                    0x80C50000,  # lwz r6,0(r5)
                    0x80E50004,  # lwz r7,4(r5)
                    0x38E70001,  # addi r7,r7,1
                    0x90E50004,  # stw r7,4(r5)
                    0x2C030000,  # cmpwi r3,0
                    0x41820020,  # beq done
                    0x7C6903A6,  # mtctr r3
                    0x88E00000 | (load_register << 16),
                    0x98E00000 | (store_register << 16),
                    0x38840001,  # addi r4,r4,1
                    0x38C60001,  # addi r6,r6,1
                    0x4200FFF0,  # bdnz loop
                    0x90C50000,  # stw r6,0(r5)
                    0x38600000,  # li r3,0
                    0x4E800020,
                ],
            )
        )
    ref = ptr(base + 120)

    def rewind():
        p.emit(0x387E0000 | (base + 144))
        save(120)
        p.emit(0x38600000)
        save(124)

    rewind()
    call("FlattenCollection", [collection, ptr(base + 96), ref])
    load(124, 4, "full callback stream uses four bulk chunks")
    load(152, 3, "full callback stream contains all persistent items")
    rewind()
    call("UnflattenCollection", [copy, ptr(base + 104), ref])
    load(124, 4, "reader requests four bulk chunks")
    call("CountCollectionItems", [copy], 3)

    call("SetCollectionItemInfo", [collection, tag_b, 2, 0x40000000, 0])
    rewind()
    call("FlattenCollection", [collection, ptr(base + 96), ref])
    load(124, 6, "selective stream sends individual persistent directory entries and payloads")
    load(152, 2, "transient items are omitted")
    call("SetCollectionItemInfo", [collection, tag_a, 7, 2, 2])
    rewind()
    call("FlattenPartialCollection", [collection, ptr(base + 96), ref, 2, 2])
    load(152, 1, "partial stream filters persistent items by attributes")
    load(168, 4, "partial stream recomputes the selected payload offset")
    rewind()
    call("UnflattenCollection", [copy, ptr(base + 104), ref])
    call("CountCollectionItems", [copy], 1)
    call("GetCollectionItemInfo", [copy, tag_a, 7, 0, size, attrs])
    load(24, 5, "partial stream restores the selected odd-sized payload")
    load(56, 0x40000003, "partial stream restores the selected attributes")
    call("CountCollectionItems", [collection], 3)
    call("UnflattenCollection", [copy, callback, 0], -501)
    call("CountCollectionItems", [copy], 0)
    probe.call("SetHandleSize", [serialized, 11])
    call("UnflattenCollectionFromHdl", [copy, serialized], -5753)
    call("UnflattenCollectionFromHdl", [copy, original])
    probe.call("PurgeCollectionTag", [copy, tag_b])
    call("CountCollectionTags", [copy], 1)
    probe.call("PurgeCollection", [copy, 1, 1])
    call("CountCollectionItems", [copy], 0)
    call("RemoveCollectionItem", [collection, tag_b, 2])
    call("CountCollectionItems", [collection], 2)
    probe.call("EmptyCollection", [collection])
    call("CountCollectionItems", [collection], 0)
    for obj in (copy, collection):
        probe.call("DisposeCollection", [obj])
    for obj in (payload, serialized, original):
        probe.call("DisposeHandle", [obj])

    new_at("GetNewCollection", 4, [182])
    call("CountCollectionItems", [copy], 2)
    call("GetCollectionItemInfo", [copy, tag_a, -3, index_out, size, attrs])
    load(52, 1, "cltn items are placed in collection order")
    load(24, 3, "cltn records decode an odd data size")
    load(56, 0x40000005, "cltn records restore item attributes")
    copy_size(4)
    call("GetCollectionItem", [copy, tag_b, 2, size, output])
    load(32, 0x4C415354, "cltn data following odd padding is intact")
    probe.call("DisposeCollection", [copy])
    call("GetNewCollection", [183], 0)
    call("GetNewCollection", [184], 0)


def internet_config_operations(probe, messages, typed_messages):
    p = probe.p
    base = 0x2080
    ptr, slot = probe.ptr, probe.slot
    callback = ptr(base + 112)
    # A guest PREF/ICAp component records the actual Pascal parameter block.
    struct.pack_into(">5I", p.data, base, 0x50524546, 0x49434170, 0x74657374, 0, 0)
    p.routines.append(
        (
            callback[1],
            [
                0x80030000,
                0x90020000 | (base + 32),
                0x88A30001,
                0x2C050000,
                0x41820024,
                0x38C00000,
                0x38E30004,
                0x39020000 | (base + 36),
                0x7CA903A6,
                0x7C0730AE,
                0x7C0831AE,
                0x38C60001,
                0x4200FFF4,
                0xA8830002,  # lha r4, 2(r3)
                0x80A20000 | (base + 68),
                0x7C042800,
                0x4082000C,
                0x80620000 | (base + 72),
                0x4E800020,
                0x38600000,
                0x4E800020,
            ],
        )
    )
    descriptor, component, instance = (slot(base + offset) for offset in (20, 24, 28))

    def call(name, arguments, expected=0):
        probe.call(name, arguments)
        p.expect(expected, name + " returns the expected Component Manager status")

    def word(offset, expected, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(expected, label)

    def set_return(selector, result):
        probe.arguments([selector, result], 0)
        p.emit(0x907E0000 | (base + 68), 0x909E0000 | (base + 72))

    probe.call("NewRoutineDescriptor", [callback, 0x3F0, 1])
    p.nonzero("create the Internet Config component callback")
    p.emit(0x907E0000 | (base + 20))
    probe.call("RegisterComponent", [ptr(base), descriptor, 0, 0, 0, 0])
    p.nonzero("register a PREF/ICAp component")
    p.emit(0x907E0000 | (base + 24))

    set_return(0, -674)
    call("ICStart", [ptr(base + 28), 0x74737432], -674)
    word(28, 0, "failed startup clears the reference")
    call("CountComponentInstances", [component], 0)

    set_return(0x32, -2147450878)  # 0x80008002 requests the legacy configuration path.
    call("ICStart", [ptr(base + 28), 0x74737432])
    word(32, 0x00060002, "older components receive the legacy configuration selector")
    call("ICStop", [instance])
    call("CountComponentInstances", [component], 0)

    set_return(0, 0)
    call("ICStart", [ptr(base + 28), 0x74737432])
    word(32, 0x00080032, "startup checks the component version after sending the signature")
    call("CountComponentInstances", [component], 1)

    def check_message(name, selector, layout, status):
        widths = [int(width) for width in layout.lstrip("0")]
        parameters = [0x123456A0 + i for i in range(len(widths))]
        payload = bytearray()
        for width, value in zip(reversed(widths), reversed(parameters)):
            if width == 1:
                payload += bytes([value & 0xFF, 0])
            else:
                payload += value.to_bytes(4, "big")[-width:]
        set_return(int(selector, 0), status)
        call(name, [instance, *parameters], status)
        word(32, (len(payload) << 16) | int(selector, 0), f"{name} sends the expected selector and extent")
        for index, expected in enumerate(payload):
            p.emit(0x887E0000 | (base + 36 + index))
            p.expect(expected, f"{name} sends the expected Pascal argument byte")

    for row in messages:
        check_message(*row, -675)
    for row in typed_messages:
        check_message(*row, 0x89ABCDEF)

    set_return(1, -671)
    call("ICStop", [instance], -671)
    call("CountComponentInstances", [component], 0)
    call("UnregisterComponent", [component])
    probe.call("DisposeRoutineDescriptor", [descriptor])


def component_function_operations(probe):
    p = probe.p
    _, base = probe.far_raw(bytes(24))
    callback = probe.far_raw(bytes(8))
    p.routines.append(
        (
            callback[1],
            [
                0x3CE20000 | ((base + 0x8000) >> 16),
                0x38E70000 | (base & 0xFFFF),
                0x90670000,
                0x90870004,
                0x90A70008,
                0x90C7000C,
                0x3C601357,
                0x60632468,
                0x4E800020,
            ],
        )
    )
    params = probe.far_raw(struct.pack(">BBhIhBb", 0, 8, 42, 0x76543210, -234, 0x80, 0))
    short_params = probe.far_raw(bytes((0, 2, 0, 42, 0, 0)))
    plain_info = (3 << 10) | (2 << 8) | (1 << 6) | (3 << 4)
    storage_info = (plain_info & ~0x3F) << 2 | (3 << 6) | (3 << 4)
    descriptor = probe.slot(base + 16)

    def verify_words(expected):
        for index, value in enumerate(expected):
            probe.arguments([probe.slot(base + index * 4)], 0)
            p.expect(value, "callback receives the original component argument in declaration order")

    probe.call("NewComponentFunctionUPP", [callback, plain_info])
    p.nonzero("create a component function descriptor")
    p.emit(0x3C9E0000 | ((base + 16 + 0x8000) >> 16), 0x90640000 | ((base + 16) & 0xFFFF))
    probe.call("CallComponentFunction", [params, descriptor])
    p.expect(0x13572468, "return the component function's complete result")
    verify_words((-128, -234, 0x76543210))
    probe.call("CallComponentFunction", [short_params, descriptor])
    p.expect(-50, "reject a parameter record shorter than its proc info signature")
    probe.call("DisposeComponentFunctionUPP", [descriptor])

    probe.call("NewComponentFunctionUPP", [callback, storage_info])
    p.nonzero("create a descriptor that accepts instance storage")
    p.emit(0x3C9E0000 | ((base + 16 + 0x8000) >> 16), 0x90640000 | ((base + 16) & 0xFFFF))
    probe.call("CallComponentFunctionWithStorage", [probe.ptr(base + 20), params, descriptor])
    p.expect(0x13572468, "invoke a component function with its instance storage")
    probe.arguments([probe.slot(base), probe.ptr(base + 20)], 0)
    p.equal("storage is prepended to the component function's arguments")
    probe.call("DisposeComponentFunctionUPP", [descriptor])
    probe.call("CallComponentFunctionWithStorageProcInfo", [0, params, callback, storage_info])
    p.expect(0x13572468, "invoke a PPC transition vector with explicit proc info")
    verify_words((0, -128, -234, 0x76543210))


def component_operations(probe):
    component_function_operations(probe)
    p, ptr, slot = probe.p, probe.ptr, probe.slot
    _, base = probe.raw(bytes(72))
    query = probe.far_raw(struct.pack(">5I", 0x43507072, 0x74657374, 0, 0, 0))
    descriptions = [
        probe.far_raw(struct.pack(">5I", 0x43507072, 0x74657374, manufacturer, 0, 0))
        for manufacturer in (0x6F6E6520, 0x74776F20)
    ]
    callback = probe.raw(bytes(8))
    p.routines.append(
        (
            callback[1],
            [
                0x80030000,
                0x90020000 | (base + 48),
                0x88A30001,
                0x2C050000,
                0x41820024,
                0x38C00000,
                0x38E30004,
                0x39020000 | (base + 52),
                0x7CA903A6,
                0x7C0730AE,
                0x7C0831AE,
                0x38C60001,
                0x4200FFF4,
                0x38600000,
                0x4E800020,
            ],
        )
    )
    first, second, descriptor, instance = (slot(base + offset) for offset in (0, 4, 8, 12))
    description, count, refcon = ptr(base + 16), ptr(base + 36), 0x1234ABCD

    def expect(name, arguments, expected):
        probe.call(name, arguments)
        p.expect(expected, name + " returns the expected Component Manager result")

    def store(offset):
        p.emit(0x907E0000 | (base + offset))

    probe.call("NewRoutineDescriptor", [callback, 0x3F0, 1])
    p.nonzero("create a guest Component Manager callback")
    store(8)
    probe.call("GetComponentListModSeed")
    store(36)
    probe.call("GetComponentTypeModSeed", [0x43507072])
    store(40)
    expect("GetComponentTypeModSeed", [0x7A7A7A7A], 0)
    for offset, entry in zip((0, 4), descriptions):
        probe.call("RegisterComponent", [entry, descriptor, 0, 0, 0, 0])
        p.nonzero("register a guest component")
        store(offset)
    probe.call("GetComponentListModSeed")
    p.emit(0x809E0000 | (base + 36), 0x38840002)
    p.equal("component registration advances the list modification seed")
    probe.call("GetComponentTypeModSeed", [0x43507072])
    p.emit(0x809E0000 | (base + 40), 0x7C641850)
    p.nonzero("component registration changes the matching type's modification seed")
    expect("GetComponentTypeModSeed", [0x7A7A7A7A], 0)
    expect("CountComponents", [query], 2)
    expect("SetDefaultComponent", [0, 0], -3000)
    expect("SetDefaultComponent", [first, 8], -50)
    expect("SetDefaultComponent", [first, 0], 0)
    probe.call("FindNextComponent", [0, query])
    p.emit(0x809E0000 | (base + 4))
    p.equal("identical matching preserves the order of different manufacturers")
    expect("SetDefaultComponent", [first, 2], 0)
    probe.call("FindNextComponent", [0, query])
    p.emit(0x809E0000 | base)
    p.equal("wildcard manufacturer moves the selected component ahead of a match")
    probe.call("FindNextComponent", [first, query])
    p.emit(0x809E0000 | (base + 4))
    p.equal("enumeration follows the updated search chain")
    expect("RegisterComponent", [descriptions[0], descriptor, 2, 0, 0, 0], 0)
    probe.call("RegisterComponent", [descriptions[0], descriptor, 4, 0, 0, 0])
    p.nonzero("register another component after existing matches")
    store(44)
    probe.call("FindNextComponent", [second, query])
    p.emit(0x809E0000 | (base + 44))
    p.equal("late registration follows all existing components of its type")
    expect("UnregisterComponent", [slot(base + 44)], 0)

    probe.call("SetComponentRefcon", [first, refcon])
    expect("GetComponentRefcon", [first], refcon)
    expect("OpenADefaultComponent", [0x43507072, 0, ptr(base + 12)], 0)
    expect("CountComponentInstances", [first], 1)
    expect("GetComponentInfo", [instance, description, 0, 0, 0], 0)
    p.emit(0x807E0000 | (base + 24))
    p.expect(0x6F6E6520, "instance information identifies the selected manufacturer")
    probe.call("SetComponentInstanceError", [instance, -42])
    expect("GetComponentInstanceError", [instance], -42)
    expect("GetComponentInstanceError", [instance], 0)
    probe.call("SetComponentInstanceStorage", [instance, count])
    probe.call("GetComponentInstanceStorage", [instance])
    p.pointer(base + 36, "instance storage preserves the guest pointer")
    for name, selector, arguments, size in (
        ("CallComponentOpen", -1, [0x10203040], 4),
        ("CallComponentClose", -2, [0x50607080], 4),
        ("CallComponentCanDo", -3, [0x3344], 2),
        ("CallComponentVersion", -4, [], 0),
        ("CallComponentRegister", -5, [], 0),
        ("CallComponentTarget", -6, [0x19293949], 4),
        ("CallComponentUnregister", -7, [], 0),
        ("CallComponentGetMPWorkFunction", -8, [ptr(base + 68), ptr(base + 64)], 8),
        ("CallComponentGetPublicResource", -10, [0x74657374, 0x1122, ptr(base + 64)], 10),
        ("ComponentFunctionImplemented", -3, [-123], 2),
        ("GetComponentVersion", -4, [], 0),
        ("ComponentSetTarget", -6, [0x39291909], 4),
    ):
        expect(name, [instance, *arguments], 0)
        p.emit(0x807E0000 | (base + 48))
        p.expect((size << 16) | (selector & 0xFFFF), name + " sends the expected component selector and size")
        if size == 2:
            p.emit(0xA07E0000 | (base + 52))
            p.expect(arguments[0] & 0xFFFF, name + " sends a big-endian short")
        elif size == 4:
            p.emit(0x807E0000 | (base + 52))
            p.expect(arguments[0], name + " sends a big-endian long")
        elif size in (8, 10):
            p.emit(0x807E0000 | (base + 52))
            p.pointer(base + 64, name + " sends the last Pascal argument first")
            if size == 8:
                p.emit(0x807E0000 | (base + 56))
                p.pointer(base + 68, name + " sends both output pointers")
            else:
                p.emit(0xA07E0000 | (base + 56))
                p.expect(0x1122, name + " sends the resource ID")
                p.emit(0x807E0000 | (base + 58))
                p.expect(0x74657374, name + " sends the resource type")
    forwarded = probe.far_raw(struct.pack(">BBhI", 0, 4, 42, 0x76543210))
    expect("DelegateComponentCall", [forwarded, instance], 0)
    p.emit(0x807E0000 | (base + 48))
    p.expect(0x0004002A, "delegation forwards the component header")
    p.emit(0x807E0000 | (base + 52))
    p.expect(0x76543210, "delegation forwards the original parameters")
    captured = slot(base + 44)
    expect("UncaptureComponent", [first], -3002)
    probe.call("CaptureComponent", [instance, second])
    p.nonzero("capture a component with an existing open instance")
    store(44)
    p.emit(0x809E0000 | base, 0x7C641850)
    p.nonzero("capture returns a distinct private component identifier")
    probe.call("GetComponentInstanceStorage", [instance])
    p.pointer(base + 36, "capturing a component preserves existing instances")
    expect("CountComponents", [query], 1)
    expect("OpenComponent", [first], 0)
    expect("CaptureComponent", [captured, second], 0)
    expect("CaptureComponent", [second, captured], 0)
    probe.call("FindNextComponent", [0, query])
    p.emit(0x809E0000 | (base + 4))
    p.equal("the captured component is skipped during discovery")
    expect("CloseComponent", [instance], 0)
    expect("CloseComponent", [instance], -3000)
    expect("OpenAComponent", [captured, ptr(base + 12)], 0)
    expect("CountComponentInstances", [captured], 1)
    expect("CloseComponent", [instance], 0)
    expect("UncaptureComponent", [captured], 0)
    expect("CountComponents", [query], 2)
    probe.call("FindNextComponent", [0, query])
    p.emit(0x809E0000 | base)
    p.equal("uncapture restores the component's identifier and search position")

    expect("OpenAComponent", [second, ptr(base + 12)], 0)
    expect("CountComponentInstances", [second], 1)
    expect("CloseComponent", [instance], 0)
    probe.call("OpenDefaultComponent", [0x43507072, 0x74657374])
    p.nonzero("open the selected default component")
    store(12)
    expect("CountComponentInstances", [first], 1)
    expect("CloseComponent", [instance], 0)
    probe.call("OpenComponent", [second])
    p.nonzero("open the explicitly selected component")
    store(12)
    expect("CloseComponent", [instance], 0)
    expect("GetComponentIconSuite", [first, ptr(base + 60)], 0)
    p.emit(0x807E0000 | (base + 60))
    p.expect(0, "a component without an icon has no icon suite")
    probe.call("CaptureComponent", [second, first])
    p.nonzero("capture a second component before unregistering its owner")
    expect("UnregisterComponent", [first], 0)
    expect("CountComponents", [query], 1)
    expect("UnregisterComponent", [second], 0)
    expect("CountComponents", [query], 0)

    probe.call("NewHandleClear", [128])
    p.nonzero("allocate a legacy 32-by-32 component icon")
    store(56)
    probe.call("RegisterComponent", [descriptions[0], descriptor, 0, 0, 0, slot(base + 56)])
    p.nonzero("register a component with a legacy monochrome icon")
    store(44)
    probe.call("DisposeHandle", [slot(base + 56)])
    expect("GetComponentIconSuite", [slot(base + 44), ptr(base + 60)], 0)
    mono_icon = int.from_bytes(b"ICN#", "big")
    expect("GetIconFromSuite", [ptr(base + 64), slot(base + 60), mono_icon], 0)
    expect("GetHandleSize", [slot(base + 64)], 256)
    probe.arguments([slot(base + 64)], 0)
    p.emit(0x80630000, 0x80630080)
    p.expect(0xFFFFFFFF, "converting a legacy icon adds a full mask")
    expect("DisposeIconSuite", [slot(base + 60), 1], 0)
    expect("UnregisterComponent", [slot(base + 44)], 0)

    codec = int.from_bytes(b"rCdc", "big")
    decoder = probe.far_raw(struct.pack(">5I", int.from_bytes(b"sdec", "big"), codec, 0, 0, 0))
    codec_name = probe.far_raw(bytes(256))
    probe.call("NewHandle", [4])
    p.nonzero("allocate a registered sound codec name")
    store(56)
    p.emit(0x80830000, 0x3C600352, 0x60633358, 0x90640000)
    probe.call("RegisterComponent", [decoder, descriptor, 0, slot(base + 56), 0, 0])
    p.nonzero("register a named sound decompressor")
    store(44)
    probe.call("DisposeHandle", [slot(base + 56)])
    expect("GetCompressionName", [codec, codec_name], 0)
    probe.arguments([codec_name], 0)
    p.emit(0x80630000)
    p.expect(0x03523358, "the Sound Manager reads the registered component's Pascal name")
    expect("UnregisterComponent", [slot(base + 44)], 0)
    expect("GetCompressionName", [codec, codec_name], -192)
    probe.arguments([codec_name], 0)
    p.emit(0x80630000)
    p.expect(0x03523358, "a missing codec preserves the name buffer")

    probe.call("DisposeRoutineDescriptor", [descriptor])

    query = probe.far_raw(struct.pack(">5I", 0x43507273, 0x74657374, 0, 0, 0))
    probe.call("CurResFile")
    store(40)
    probe.call("Get1Resource", [0x74686E67, 182])
    p.nonzero("load the application's original component resource")
    store(44)
    probe.call("RegisterComponentResource", [slot(base + 44), 0])
    p.nonzero("register a resource-backed component")
    store(0)
    expect("CountComponents", [query], 1)
    file_ref, resource = ptr(base + 56), ptr(base + 60)
    string = probe.far_raw(bytes(256))
    expect("OpenAComponentResFile", [first, file_ref], 0)
    p.emit(0xA87E0000 | (base + 56))
    p.nonzero("the component's resource fork is open")
    store(56)
    probe.call("OpenComponentResFile", [first])
    p.nonzero("the legacy opener returns another valid reference")
    store(64)
    expect("CloseComponentResFile", [slot(base + 64)], 0)
    expect("CloseComponentResFile", [slot(base + 56)], 0)
    probe.call("UseResFile", [slot(base + 40)])
    probe.call("ResError")
    p.expect(0, "closing component leases keeps the application resource map usable")
    expect("GetComponentResource", [first, 0x72436D70, 182, resource], 0)
    expect("GetHandleSize", [slot(base + 60)], 4)
    probe.call("DisposeHandle", [slot(base + 60)])
    expect("GetComponentResource", [first, 0x72436D70, 183, resource], -192)
    p.emit(0x807E0000 | (base + 60))
    p.expect(0, "a missing component resource leaves a null handle")
    expect("GetComponentIndString", [first, string, 182, 2], 0)
    probe.arguments([string], 0)
    p.emit(0x80630000)
    p.expect(0x0354776F, "the indexed string is copied as a Pascal string")
    public_type = int.from_bytes(b"PUBR", "big")
    expect("GetComponentPublicResource", [first, public_type, 314, resource], 0)
    expect("GetHandleSize", [slot(base + 60)], 4)
    probe.call("DisposeHandle", [slot(base + 60)])
    expect("GetComponentResource", [first, public_type, 314, resource], -192)
    expect("GetComponentPublicResource", [first, public_type, 315, resource], -192)
    p.emit(0x807E0000 | (base + 60))
    p.expect(0, "an unpublished ID cannot expose a private resource")
    expect("GetComponentPublicIndString", [first, string, 327, 1], 0)
    probe.arguments([string], 0)
    p.emit(0x80630000)
    p.expect(0x034F6E65, "public string lookup maps the list before indexing it")
    expect("GetComponentPublicIndString", [first, string, 182, 2], -192)
    expect("GetComponentIconSuite", [first, resource], 0)
    expect("GetIconFromSuite", [ptr(base + 64), slot(base + 60), mono_icon], 0)
    probe.arguments([slot(base + 64)], 0)
    p.emit(0x80630000, 0x80630000)
    p.expect(0xAA55AA55, "the icon suite contains the declared family's bitmap")
    expect("DisposeIconSuite", [slot(base + 60), 1], 0)
    probe.call("Get1Resource", [mono_icon, 185])
    p.nonzero("disposing the suite leaves the component resource intact")
    probe.call("CurResFile")
    p.emit(0x809E0000 | (base + 40))
    p.equal("reading a component resource preserves the selected resource map")

    expect("UnregisterComponent", [first], 0)
    expect("RegisterComponentResourceFile", [slot(base + 40), 0], 1)
    probe.call("FindNextComponent", [0, query])
    p.nonzero("enumerate the component registered from its resource file")
    store(0)
    probe.call("OpenComponent", [first])
    p.nonzero("open the resource-backed component")
    store(12)
    expect("CloseComponent", [instance], 0)
    expect("UnregisterComponent", [first], 0)


def make_atsui_probe(destination):
    write_toolbox_references(destination, include_atsui=True)
    name = "ATSUITestFont"
    font_directory = destination / "ATSUI Fixtures"
    (font_directory / ".AppleDouble").mkdir(parents=True, exist_ok=True)
    (font_directory / name).write_bytes((ROOT / "fonts/Helvetica.ttf").read_bytes())
    finder_info = b"sfntDMOV" + bytes(24)
    apple_double = struct.pack(">II16sHIII", 0x51607, 0x20000, bytes(16), 1, 9, 38, len(finder_info))
    (font_directory / ".AppleDouble" / name).write_bytes(apple_double + finder_info)
    source = (ROOT / "carbonlib/src/atsu_entries.h").read_text()
    native_names = set(re.findall(r"^CARBON_ATSU\((\w+),", source, re.M))
    saved_names = (
        "ATSUCreateStyle",
        "ATSUSetStyleRefCon",
        "ATSUGetStyleRefCon",
        "ATSUCompareStyles",
        "ATSUGetAllAttributes",
        "ATSUCopyLayoutControls",
        "ATSUTextMoved",
        "ATSUClearLayoutCache",
        "ATSUIdle",
        "ATSUTextDeleted",
        "ATSUTextInserted",
        "ATSUCopyLineControls",
        "ATSUHighlightText",
        "ATSUUnhighlightText",
        "ATSUPositionToOffset",
        "ATSUOffsetToPosition",
        "ATSUNextCursorPosition",
        "ATSUPreviousCursorPosition",
        "ATSURightwardCursorPosition",
        "ATSULeftwardCursorPosition",
        "ATSUBreakLine",
        "ATSULoadGlyphVector",
        "ATSUConvertCharToGlyphs",
        "ATSUGlyphGetCurvePaths",
        "ATSUGlyphGetQuadraticPaths",
        "ATSUGlyphGetCubicPaths",
        "ATSUCopyToHandle",
        "ATSUPasteFromHandle",
        "ATSUGetContinuousAttributes",
        "ATSUMatchFontsToText",
        "ATSUDrawGlyphInfo",
        "ATSUGetGlyphInfo",
        "ATSUGetGlyphBounds",
        "ATSUGlyphGetIdealMetrics",
        "ATSUGlyphGetScreenMetrics",
        "ATSUCalculateBaselineDeltas",
        "ATSUGetStyleGroup",
        "ATSUDisposeStyleGroup",
        "ATSUGetStyleIndex",
        "ATSUGetNativeCurveType",
        "ATSUGetFontIDs",
        "ATSUFontIDtoFOND",
        "ATSUGetIndFontName",
        "ATSUFindFontName",
        "ATSUFindFontFromName",
        "ATSUFontCount",
        "ATSUCountFontNames",
        "ATSUCountFontFeatureTypes",
        "ATSUGetFontFeatureTypes",
        "ATSUCountFontFeatureSelectors",
        "ATSUGetFontFeatureSelectors",
        "ATSUGetFontFeatureNameCode",
        "ATSUCountFontTracking",
        "ATSUGetIndFontTracking",
        "ATSUCountFontVariations",
        "ATSUGetIndFontVariation",
        "ATSUGetFontVariationNameCode",
        "ATSUCountFontInstances",
        "ATSUGetFontInstance",
        "ATSUGetFontInstanceNameCode",
        "ATSUGetTextLayoutRefCon",
        "ATSUFONDtoFontID",
        "ATSUDrawText",
        "ATSUGetTextHighlight",
        "ATSUMeasureText",
        "ATSUMeasureTextImage",
        *ATSUI_REFERENCE_CALLS,
    )
    toolbox = {
        "BlockMoveData",
        "DisposeGWorld",
        "DisposeRgn",
        "EmptyRgn",
        "EraseRect",
        "GetGWorld",
        "GetGWorldPixMap",
        "GetPixBaseAddr",
        "GetPixRowBytes",
        "GetRegionBounds",
        "LockPixels",
        "MoveTo",
        "NewGWorld",
        "NewRgn",
        "SetGWorld",
        "UnlockPixels",
        "DisposeHandle",
        "FSMakeFSSpec",
        "HGetState",
        "NewHandle",
    }
    probe = ToolboxProbe([{"name": name} for name in saved_names], False, native_names=native_names | toolbox)
    probe.p.reserve_parameter_words(9)
    probe.section("ATSUI styles and layouts", lambda: atsui_operations(probe))
    probe.call("CloseConnection", [probe.ptr(probe.oracle_connections["CarbonLib"])])
    probe.p.expect(0, "close the real CarbonLib oracle")
    name = "PPC Carbon Toolbox Vector Probe"
    probe.p.finish(destination, name, carbon=True)
    probe.write_coverage(destination, name, native_names)


def write_color_profile_reference(destination):
    folder = ROOT / "build/carbon-color-reference"
    name = "ColorSync Extension"
    data = folder / name
    if not data.is_file():
        return False
    manifest = json.loads((folder / "disassembly" / name / "manifest.json").read_text())
    image = bytearray(data.read_bytes())
    if hashlib.sha256(image).hexdigest() != manifest["data_sha256"]:
        raise ValueError(f"ColorSync reference changed: {data}")
    pef_layout = json.loads((folder / "disassembly" / name / "pef/data-ColorSync/pef.json").read_text())
    loader = next(section["offset"] for section in pef_layout["sections"] if section["kind"] == 4)
    library_count = struct.unpack_from(">I", image, loader + 24)[0]
    # The profile queries do not dispatch components through this legacy data
    # import. Allow the saved fragment to load in the test profile without it.
    unused = next(index for index, row in enumerate(pef_layout["imports"]) if row["name"] == "CallComponentUPP")
    image[loader + 56 + 24 * library_count + 4 * unused] |= 0x80
    end = max(section["offset"] + section["packed_size"] for section in pef_layout["sections"])
    write_fragment(destination, "ColorSync", image[:end], 0)
    return True


def color_profile_operations(probe):
    p = probe.p
    base = 0x1A00
    connection, profile, size, output = (probe.ptr(base + i) for i in (0, 4, 8, 64))
    profile_ref = probe.slot(base + 4)

    def store(offset, value):
        probe.arguments([value], 0)
        p.emit(0x907E0000 | (base + offset))

    def check(offset, value, label):
        p.emit(0x807E0000 | (base + offset))
        p.expect(value, label)

    def checked(name, arguments, status=0, original=False):
        if original:
            probe.arguments(arguments, 0)
            p.emit(0x819E0000 | vectors[name], 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x7FC2F378)
        else:
            probe.call(name, arguments)
        if status is not None:
            p.expect(status, name + " returns the expected status")

    library = probe.far_raw(b"\x09ColorSync")
    probe.call("GetSharedLibrary", [library, 0x70777063, 1, connection, 0, 0])
    p.expect(0, "load the saved ColorSync extension")
    vectors = {}
    for index, name in enumerate(COLOR_PROFILE_APIS):
        symbol = probe.far_raw(bytes([len(name)]) + name.encode("ascii"))
        at = base + 208 + index * 4
        vectors[name] = at
        probe.call("FindSymbol", [probe.slot(base), symbol, probe.ptr(at), 0])
        p.expect(0, "resolve the saved ColorSync " + name)
        p.emit(0x807E0000 | at, 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
        p.nonzero("ColorSync original export is a guest descriptor")

    profile_bytes = bytearray(192)
    struct.pack_into(
        ">6I6H", profile_bytes, 0, 192, 0, 0x02100000, 0x6D6E7472, 0x52474220, 0x58595A20, 2000, 1, 1, 0, 0, 0
    )
    struct.pack_into(">I", profile_bytes, 36, 0x61637370)
    struct.pack_into(">3I", profile_bytes, 68, 0xF6D6, 0x10000, 0xD32D)
    struct.pack_into(">I4sII4sII4sII", profile_bytes, 128, 3, b"cprt", 168, 16, b"desc", 168, 16, b"info", 184, 8)
    profile_bytes[168:184] = b"text\0\0\0\0sample!\0"
    profile_bytes[184:192] = b"data\0\0\0\0"
    data = probe.far_raw(profile_bytes)
    location = probe.far_raw(bytes(258))
    probe.arguments([location, data, 6, len(profile_bytes)], 0)
    p.emit(0xB0A30000, 0x90830002, 0x90C30006)
    checked("CMOpenProfile", [profile, location])
    p.emit(0x807E0000 | (base + 4))
    p.nonzero("the native opener creates a live ColorSync profile reference")

    def compare(first, second, message):
        p.emit(0x807E0000 | (base + first), 0x809E0000 | (base + second))
        p.equal(message)

    store(20, 0x5A5A5A5A)
    checked("CMValidateProfile", [profile_ref, probe.ptr(base + 20), probe.ptr(base + 21)], None)
    p.emit(0x907E0000 | base + 16, 0x2C03FF51)
    compare_original = len(p.code)
    p.emit(0)
    p.emit(0x887E0000 | base + 20)
    p.expect(0, "a missing CMM leaves the profile unvalidated")
    validation_done = len(p.code)
    p.emit(0)
    # The saved CMM selector underflows its deduplication loop with no candidates.
    p.code[compare_original] = 0x40820000 | (len(p.code) - compare_original) * 4
    store(36, 0x5A5A5A5A)
    checked("CMValidateProfile", [profile_ref, probe.ptr(base + 36), probe.ptr(base + 37)], None, True)
    p.emit(0x907E0000 | base + 32)
    compare(16, 32, "validation status matches when a CMM is available")
    compare(20, 36, "validation flags match when a CMM is available")
    p.code[validation_done] = 0x48000000 | (len(p.code) - validation_done) * 4

    for side, original in enumerate((False, True)):
        result = 16 + side * 16
        store(result + 4, 0x5A5A5A5A)
        checked("CMValidateProfile", [0, probe.ptr(base + 64), probe.ptr(base + 65)], -50, original)
        p.emit(0xA07E0000 | base + 64)
        p.expect(0, "a missing profile clears both validation flags")
        checked("CMGetPreferredCMMGlobal", [probe.ptr(base + result + 8), probe.ptr(base + result + 6)], None, original)
        p.emit(0x907E0000 | (base + result + 12))
        checked("CMGetPreferredCMMGlobal", [0, probe.ptr(base + 64)], -50, original)
        p.emit(0x887E0000 | base + 64)
        p.expect(0, "an omitted CMM signature clears availability")
    for offset in (4, 8, 12):
        compare(16 + offset, 32 + offset, "CMM preference matches the saved library")

    search = probe.slot(base + 16)
    spec = probe.far_raw(bytes(44))
    for original in (False, True):
        checked("CMNewProfileSearch", [0, 0, size, probe.ptr(base + 16)], -50, original)
        checked("CMUpdateProfileSearch", [0, 0, size], -4206, original)
        checked("CMSearchGetIndProfile", [0, 1, 0], -50, original)
        checked("CMSearchGetIndProfileFileSpec", [0, 1, output], -4206, original)
        checked("CMDisposeProfileSearch", [0], None, original)

    for side, original in enumerate((False, True)):
        offset = 16 + side * 16
        count = probe.ptr(base + offset + 4)
        handle = probe.ptr(base + offset + 8)
        store(offset + 4, 0x5A5A5A5A)
        store(offset + 8, 0)
        checked("CMNewProfileSearch", [spec, 0, count, handle], None, original)
        p.emit(0x907E0000 | base + offset)
        checked("CMUpdateProfileSearch", [probe.slot(base + offset + 8), 0, count], None, original)
        p.emit(0x907E0000 | (base + offset + 12))
        checked("CMDisposeProfileSearch", [probe.slot(base + offset + 8)], None, original)
    for offset in (0, 4, 12):
        compare(16 + offset, 32 + offset, "creating and refreshing a profile search matches ColorSync")

    record = bytearray(0x84)
    file_spec = struct.pack(">hI64s", -42, 0x12345678, b"\x08Test ICC")
    struct.pack_into(">4s", record, 0, b"pamg")
    struct.pack_into(">I", record, 0x38, 1)
    record[0x3C : 0x3C + len(file_spec)] = file_spec
    scratch = probe.far_raw(record)
    for original in (False, True):
        probe.call("NewHandleClear", [len(record)])
        p.nonzero("allocate a live search record")
        p.emit(0x907E0000 | base + 16, 0x80630000, 0x907E0000 | base + 20)
        probe.call("BlockMoveData", [scratch, probe.slot(base + 20), len(record)])
        checked("CMSearchGetIndProfileFileSpec", [search, 1, output], original=original)
        for index in range(0, len(file_spec) - 2, 4):
            check(64 + index, int.from_bytes(file_spec[index : index + 4], "big"), "one-based search copies FSSpec")
        checked("CMSearchGetIndProfile", [search, 0, size], -4201, original)
        checked("CMSearchGetIndProfileFileSpec", [search, 2, output], -4201, original)
        checked("CMDisposeProfileSearch", [search], None, original)

    rgb_space, display_use = int.from_bytes(b"RGB ", "big"), int.from_bytes(b"dply", "big")
    for name, arguments in (
        ("CMGetDefaultProfileBySpace", [rgb_space, size]),
        ("CMGetDefaultProfileBySpace", [int.from_bytes(b"nons", "big"), size]),
        ("CMGetDefaultProfileByUse", [display_use, size]),
        ("CMGetDefaultProfileByUse", [int.from_bytes(b"inpt", "big"), size]),
        ("CMGetProfileByAVID", [0, size]),
        ("CMGetSystemProfile", [size]),
        ("CMGetSystemProfilePriv", [size, 1]),
    ):
        for side, original in enumerate((False, True)):
            out = 16 + 16 * side
            store(8, 0)
            checked(name, arguments, None, original)
            p.emit(0x907E0000 | base + out)
            p.emit(0x807E0000 | base + 8, 0x7C630034, 0x5463D97E, 0x907E0000 | base + out + 4)
            checked("CMCloseProfile", [probe.slot(base + 8)], None)
        compare(16, 32, name + " returns the saved status")
        compare(20, 36, name + " yields the saved handle-presence result")

    missing_file = probe.far_raw(struct.pack(">hI64s", -32768, 1, b"\x14ROMAW missing ICC QA"))
    for original in (False, True):
        checked("CMGetDefaultProfileBySpace", [0, size], -50, original)
        checked("CMGetDefaultProfileByUse", [0, size], -50, original)
        checked("CMSetDefaultProfileBySpace", [0, profile_ref], -50, original)
        checked("CMSetDefaultProfileByUse", [0, profile_ref], -50, original)
        checked("NCMSetSystemProfile", [0], -50, original)
        checked("NCMSetSystemProfile", [location], -4205, original)

    for name, arguments in (
        ("CMSetDefaultProfileBySpace", [rgb_space, profile_ref]),
        ("CMSetDefaultProfileByUse", [display_use, profile_ref]),
        ("CMSetDefaultProfileByUse", [int.from_bytes(b"inpt", "big"), profile_ref]),
        ("CMSetSystemProfile", [missing_file]),
    ):
        for side, original in enumerate((False, True)):
            checked(name, arguments, None, original)
            p.emit(0x907E0000 | base + 16 + side * 16)
        compare(16, 32, name + " handles an unwritable location like the saved library")

    cprt = int.from_bytes(b"cprt", "big")
    for original in (True, False):
        checked("CMGetProfileRefCount", [profile_ref, size], original=original)
        check(8, 1, "read the shared profile retain count")
        checked("CMCloneProfileRef", [profile_ref], original=original)
        checked("CMGetProfileRefCount", [profile_ref, size], original=not original)
        check(8, 2, "each implementation observes the other implementation's retain")
        checked("CMCloseProfile", [profile_ref], original=original)
        checked("CMGetProfileRefCount", [profile_ref, size], original=not original)
        check(8, 1, "either close path releases a shared reference")
        store(12, 0xFFFFFFFF)
        checked("CMProfileModified", [profile_ref, probe.ptr(base + 12)], original=original)
        check(12, 0x00FFFFFF, "profile begins clean and the Boolean occupies one byte")
        checked("CMGetProfileHeader", [profile_ref, output], original=original)
        check(64, 192, "profile header stores the ICC size")
        check(100, 0x61637370, "profile header contains the ICC signature")
        checked("CMCountProfileElements", [profile_ref, size], original=original)
        check(8, 3, "profile exposes all three tags")
        for signature, expected in ((cprt, 0x01FFFFFF), (int.from_bytes(b"miss", "big"), 0x00FFFFFF)):
            store(12, 0xFFFFFFFF)
            checked("CMProfileElementExists", [profile_ref, signature, probe.ptr(base + 12)], original=original)
            check(12, expected, "tag presence uses a single-byte Boolean")
        checked(
            "CMGetIndProfileElementInfo",
            [profile_ref, 1, probe.ptr(base + 16), size, probe.ptr(base + 12)],
            original=original,
        )
        check(8, 16, "indexed tag reports its payload length")
        check(12, 0x01FFFFFF, "a second tag shares the same backing offset")
        check(16, cprt, "tag indices are one-based")
        checked("CMGetIndProfileElementInfo", [profile_ref, 0, 0, 0, 0], -4201, original)
        checked("CMGetProfileElement", [profile_ref, cprt, size, 0], original=original)
        check(8, 16, "a null payload requests the full tag extent")
        checked("CMGetProfileElement", [profile_ref, cprt, size, output], original=original)
        check(64, 0x74657874, "signature lookup reads the ICC payload")
        checked("CMGetIndProfileElement", [profile_ref, 2, size, output], original=original)
        check(64, 0x74657874, "indexed lookup reads the shared payload")
        store(8, 8)
        store(68, 0xFFFFFFFF)
        checked("CMGetPartialProfileElement", [profile_ref, cprt, 12, size, output], original=original)
        check(8, 4, "partial read clips to the end of the payload")
        check(64, 0x6C652100, "partial read returns the final payload bytes")
        check(68, 0xFFFFFFFF, "partial read preserves the following output word")
        checked("CMGetPartialProfileElement", [profile_ref, cprt, 16, size, output], -4201, original)
        check(8, 0, "an out-of-range read reports zero bytes")
        checked("NCMGetProfileLocation", [profile_ref, 0, size], original=original)
        check(8, 10, "a buffer location requires ten packed bytes")
        store(8, 6)
        store(64, 0xFFFFFFFF)
        store(68, 0xFFFFFFFF)
        checked("NCMGetProfileLocation", [profile_ref, output, size], -4205, original)
        check(64, 0, "a short location is cleared")
        check(68, 0x0000FFFF, "a short location preserves bytes outside capacity")
        checked("CMGetProfileLocation", [profile_ref, output], original=original)
        p.emit(0xA07E0000 | (base + 64))
        p.expect(6, "legacy location identifies the same buffer location")
        probe.arguments([0, data], 0)
        p.emit(0x807E0000 | (base + 66))
        p.equal("location exposes the caller's buffer pointer")
        p.emit(0x807E0000 | (base + 70))
        p.expect(192, "location exposes the buffer extent")

    checked("CMCloseProfile", [profile_ref])
    clone = int.from_bytes(b"copy", "big")
    info = int.from_bytes(b"info", "big")
    for original in (True, False):
        checked("CMOpenProfile", [profile, location], original=True)
        checked("CMGetProfileHeader", [profile_ref, output], original=not original)
        store(64, 0xDEADBEEF)
        store(144, 0x45584543)
        checked("CMSetProfileHeader", [profile_ref, output], original=original)
        checked("CMGetProfileHeader", [profile_ref, output], original=not original)
        check(64, 192, "header replacement preserves the serialized profile size")
        check(144, 0x45584543, "both implementations see the updated header")
        checked("CMProfileModified", [profile_ref, probe.ptr(base + 12)], original=not original)
        p.emit(0x887E0000 | (base + 12))
        p.expect(1, "setting the header marks the profile dirty")
        checked("CMSetProfileElementReference", [profile_ref, cprt, clone], original=original)
        checked("CMCountProfileElements", [profile_ref, size], original=not original)
        check(8, 4, "adding a reference extends the live directory")
        checked("CMSetProfileElementReference", [profile_ref, cprt, info], original=original)
        checked("CMCountProfileElements", [profile_ref, size], original=not original)
        check(8, 4, "replacing a reference preserves directory count")
        checked("CMRemoveProfileElement", [profile_ref, cprt], original=original)
        checked("CMCountProfileElements", [profile_ref, size], original=not original)
        check(8, 3, "removal compacts the live directory")
        checked("CMRemoveProfileElement", [profile_ref, cprt], -4200, original)
        store(8, 16)
        checked("CMGetProfileElement", [profile_ref, clone, size, output], original=not original)
        check(64, 0x74657874, "aliases retain the backing data after the source tag is removed")
        checked("CMGetIndProfileElementInfo", [profile_ref, 2, probe.ptr(base + 16), size, 0], original=not original)
        check(16, info, "remaining tag indices follow the compacted directory")
        checked("CMCloseProfile", [profile_ref], original=not original)
    replacement = probe.far_raw(b"ABCDwxyz")
    tiny = int.from_bytes(b"tiny", "big")
    alias = int.from_bytes(b"twin", "big")
    desc = int.from_bytes(b"desc", "big")
    for original in (True, False):
        checked("CMOpenProfile", [profile, location], original=True)
        checked("CMSetPartialProfileElement", [profile_ref, cprt, 8, 4, replacement], original=original)
        store(8, 16)
        checked("CMGetProfileElement", [profile_ref, cprt, size, output], original=not original)
        check(8, 16, "an in-range partial write preserves the extent")
        check(64, 0x74657874, "partial write preserves the existing tag header")
        check(72, 0x41424344, "partial write installs the requested bytes")
        check(76, 0x6C652100, "partial write preserves the suffix")
        checked("CMGetProfileElement", [profile_ref, desc, size, output], original=not original)
        check(72, 0x73616D70, "partial writes leave the original aliased tag intact")
        checked("CMSetProfileElement", [profile_ref, tiny, 8, replacement], original=original)
        checked("CMSetProfileElementReference", [profile_ref, tiny, alias], original=not original)
        checked("CMSetPartialProfileElement", [profile_ref, tiny, 2, 4, replacement], original=original)
        store(8, 8)
        checked("CMGetProfileElement", [profile_ref, tiny, size, output], original=not original)
        check(64, 0x41424142, "writing shared edit storage copies before replacing the tag")
        check(68, 0x4344797A, "shared edit copy preserves the untouched tail")
        checked("CMGetProfileElement", [profile_ref, alias, size, output], original=not original)
        check(64, 0x41424344, "the alias retains the original edit bytes")
        check(68, 0x7778797A, "the alias retains the complete payload")
        checked("CMSetProfileElementSize", [profile_ref, tiny, 4], original=original)
        checked("CMGetProfileElement", [profile_ref, tiny, size, 0], original=not original)
        check(8, 4, "resizing an existing tag can shrink it")
        resize = [profile_ref, tiny, 12]
        if original:
            # The saved writer may be unable to grow a locked directory in place.
            checked("CMSetProfileElementSize", resize, None, original=True)
            p.emit(0x2C030000)
            resized = len(p.code)
            p.emit(0)
            p.expect(-108, "the saved resize can fail when its locked directory cannot grow")
            checked("CMGetProfileElement", [profile_ref, tiny, size, 0], None)
            p.emit(0x2C030000)
            retained = len(p.code)
            p.emit(0)
            p.expect(-4200, "the saved writer removed the tag before its failed reinsertion")
            store(64, 0x41424142)
            checked("CMSetProfileElement", [profile_ref, tiny, 4, output])
            restored = len(p.code)
            p.emit(0)
            p.code[retained] = 0x41820000 | (len(p.code) - retained) * 4
            check(8, 4, "the saved writer preserved the tag when allocation failed before removal")
            p.code[restored] = 0x48000000 | (len(p.code) - restored) * 4
            checked("CMSetProfileElementSize", resize)
            p.code[resized] = 0x41820000 | (len(p.code) - resized) * 4
        else:
            checked("CMSetProfileElementSize", resize)
        checked("CMGetProfileElement", [profile_ref, tiny, size, 0], original=not original)
        check(8, 12, "resizing an existing tag can grow it")
        store(8, 4)
        checked("CMGetProfileElement", [profile_ref, tiny, size, output], original=not original)
        check(64, 0x41424142, "resizing preserves the prefix")
        checked("CMSetProfileElement", [profile_ref, tiny, 0, replacement], -50, original)
        checked("CMSetPartialProfileElement", [profile_ref, tiny, 0, 1, 0], -50, original)
        checked("CMSetProfileElementSize", [profile_ref, tiny, 0], -50, original)
        if not original:
            checked("CMSetPartialProfileElement", [profile_ref, tiny, 0xFFFFFFFF, 2, replacement], -50)
        checked("CMCountProfileElements", [profile_ref, size], original=not original)
        check(8, 5, "invalid writes leave the live tag directory intact")
        checked("CMCloseProfile", [profile_ref], original=True)
    updated_tag = probe.far_raw(b"abcdefghijklm")
    for kind in (2, 6):
        for saved_edits, saved_update in ((True, False), (False, True), (False, False)):
            if kind == 2:
                probe.call("NewHandle", [192])
                p.nonzero("allocate the handle-backed profile")
                p.emit(0x907E0000 | (base + 28))
                backing = probe.slot(base + 28)
                probe.arguments([data, backing, 192], 0)
                p.emit(0x80840000)
                p.call("BlockMoveData")
            else:
                backing = probe.far_raw(profile_bytes + bytes(64))
            probe.arguments([location, backing, kind, 256], 0)
            p.emit(0xB0A30000, 0x90830002, 0x90C30006)
            checked("CMOpenProfile", [profile, location], original=saved_edits)
            checked("CMUpdateProfile", [profile_ref], original=True)
            checked("CMGetProfileHeader", [profile_ref, output], original=saved_edits)
            store(144, 0x45584543)
            checked("CMSetProfileHeader", [profile_ref, output], original=saved_edits)
            checked("CMSetProfileElement", [profile_ref, info, 13, updated_tag], original=saved_edits)
            checked("CMGetProfileHeader", [profile_ref, output], original=saved_edits)
            check(64, 200, "a dirty header reports the pending serialized size")
            probe.arguments([backing], 0)
            if kind == 2:
                p.emit(0x80630000)
            p.emit(0x80630000)
            p.expect(192, "the primary storage keeps its old size until writeback")
            checked("CMUpdateProfile", [profile_ref], original=saved_update)
            checked("CMProfileModified", [profile_ref, probe.ptr(base + 12)], original=True)
            p.emit(0x887E0000 | (base + 12))
            p.expect(0, "serialization clears both dirty flags")
            checked("CMUpdateProfile", [profile_ref])
            checked("CMGetProfileHeader", [profile_ref, output], original=True)
            check(64, 200, "writeback updates the logical size")
            if kind == 2:
                probe.call("GetHandleSize", [backing])
                p.expect(200, "writeback grows the caller's movable handle")

            for at, value in ((0, 200), (80, 0x45584543), (136, 168), (148, 168), (160, 184), (164, 13)):
                probe.arguments([backing], 0)
                if kind == 2:
                    p.emit(0x80630000)
                p.emit(0x80630000 | at)
                p.expect(value, "committed ICC header and directory")
            checked("CMCloseProfile", [profile_ref], original=saved_update)
            checked("CMOpenProfile", [profile, location], original=True)
            store(8, 13)
            checked("CMGetProfileElement", [profile_ref, info, size, output], original=True)
            check(64, 0x61626364, "a newly opened profile can read the committed payload")
            check(72, 0x696A6B6C, "writeback preserves the updated payload tail")
            checked("CMCloseProfile", [profile_ref], original=True)
            if kind == 2:
                probe.call("DisposeHandle", [backing])
    ascii_name = probe.far_raw(b"Printer\0")
    script_name = probe.far_raw(b"\x03Ink")
    unicode_name = probe.far_raw(struct.pack(">3H", 0xFEFF, 0x0041, 0x03A9))
    script_out, script_code, unicode_out, unicode_size = (probe.ptr(base + i) for i in (88, 20, 120, 16))
    fields = [profile_ref, output, size, script_out, script_code, unicode_out, unicode_size]
    probe.arguments([location, data, 192], 0)
    p.emit(0x90830002, 0x90A30006)
    checked("CMOpenProfile", [profile, location], original=True)
    for original in (True, False):
        checked(
            "CMSetProfileDescriptions", [profile_ref, ascii_name, 8, script_name, 7, unicode_name, 3], original=original
        )
        store(8, 8)
        store(16, 4)
        store(20, 0xFFFFFFFF)
        checked("CMGetProfileDescriptions", fields, original=not original)
        check(8, 8, "ASCII description reports its extent including the NUL")
        check(64, 0x5072696E, "ASCII description round trips across implementations")
        check(68, 0x74657200, "ASCII description preserves the terminator")
        check(88, 0x03496E6B, "script description round trips as a Pascal string")
        check(20, 0x0007FFFF, "script code occupies one signed word")
        check(16, 3, "Unicode count excludes the BOM and includes the appended NUL")
        check(120, 0x004103A9, "Unicode description omits the BOM")
        store(8, 2)
        store(16, 2)
        store(64, 0xEEEEEEEE)
        store(120, 0xEEEEEEEE)
        checked("CMGetProfileDescriptions", fields, original=not original)
        check(8, 8, "truncated ASCII reports its full extent")
        check(64, 0x5072EEEE, "ASCII output respects caller capacity")
        check(16, 3, "truncated Unicode reports its full count")
        check(120, 0x00410000, "Unicode output terminates the last supplied unit")
        checked("CMGetScriptProfileDescription", [profile_ref, script_out, script_code], original=not original)
        check(88, 0x03496E6B, "script-only reader prefers the stored script")
        checked("CMSetProfileDescriptions", [profile_ref, ascii_name, 8, 0, -1, 0, 0], original=original)
        checked(
            "CMGetProfileDescriptions",
            [profile_ref, 0, size, 0, script_code, 0, unicode_size],
            original=not original,
        )
        check(20, 0x0000FFFF, "the description setter resolves the system script")
        checked("CMGetScriptProfileDescription", [profile_ref, script_out, script_code], original=not original)
        check(88, 0x07507269, "script-only reader falls back to ASCII")
        check(20, 0xFFFFFFFF, "ASCII fallback reports the system script code")
        checked("CMSetProfileDescriptions", [profile_ref, 0, 8, 0, 0, 0, 0], -50, original)
        checked("CMGetScriptProfileDescription", [profile_ref, 0, script_code], -50, original)
    checked("CMRemoveProfileElement", [profile_ref, desc], original=True)
    for original in (True, False):
        store(8, 4)
        store(16, 4)
        store(88, 0xEEEEEEEE)
        checked("CMGetProfileDescriptions", fields, -4200, original)
        check(8, 0, "missing description clears the ASCII count")
        check(16, 0, "missing description clears the Unicode count")
        check(88, 0x0000EEEE, "missing description clears the Pascal length and first data byte")
    checked("CMSetProfileElement", [profile_ref, desc, 8, ascii_name])
    checked("CMGetProfileDescriptions", fields, -4204)
    checked("CMCloseProfile", [profile_ref], original=True)
    # Take copies from a live profile whose edits have yet to be committed.
    probe.arguments([location, data, 6, 192], 0)
    p.emit(0xB0A30000, 0x90830002, 0x90C30006)
    checked("CMOpenProfile", [profile, location], original=True)
    checked("CMSetProfileElement", [profile_ref, info, 13, updated_tag])
    copy_location = probe.far_raw(bytes(258))
    copy_ref = probe.ptr(base + 36)
    for kind in (2, 6):
        for original in (True, False):
            if kind == 2:
                probe.call("NewHandle", [192])
                p.nonzero("allocate the copy destination handle")
                p.emit(0x907E0000 | (base + 40))
                copy_backing = probe.slot(base + 40)
            else:
                copy_backing = probe.far_raw(bytes(256))
            probe.arguments([copy_location, copy_backing, kind, 256], 0)
            p.emit(0xB0A30000, 0x90830002, 0x90C30006)
            checked("CMCopyProfile", [copy_ref, copy_location, profile_ref], original=original)
            p.emit(0x807E0000 | (base + 36))
            p.nonzero("copy returns a live profile")
            store(8, 13)
            checked("CMGetProfileElement", [probe.slot(base + 36), info, size, output], original=not original)
            check(64, 0x61626364, "the copy includes uncommitted source changes")
            check(72, 0x696A6B6C, "the copy preserves the staged tag suffix")
            checked("CMCloseProfile", [probe.slot(base + 36)], original=True)
            if kind == 2:
                probe.call("GetHandleSize", [copy_backing])
                p.expect(200, "copy resizes the caller's handle to the serialized extent")
                probe.call("DisposeHandle", [copy_backing])
    checked("CMCopyProfile", [copy_ref, location, profile_ref], -4205)
    check(36, 0, "copy rejects the source location and clears the output")
    small_buffer = probe.far_raw(bytes(128))
    probe.arguments([copy_location, small_buffer, 6, 128], 0)
    p.emit(0xB0A30000, 0x90830002, 0x90C30006)
    checked("CMCopyProfile", [copy_ref, copy_location, profile_ref], -108)
    check(36, 0, "an undersized copy buffer leaves the output clear")
    checked("CMProfileModified", [profile_ref, probe.ptr(base + 12)])
    p.emit(0x887E0000 | (base + 12))
    p.expect(1, "copy does not commit the source edit store")

    # The second spool write starts in the middle of the edited tag.
    large_tag = probe.far_raw(bytes([0xA7]) * 4200)
    checked("CMSetProfileElement", [profile_ref, info, 4200, large_tag])
    spool_proc = probe.far_raw(bytes(8))
    spool_state = probe.far_raw(bytes(32 + 4384))
    p.routines.append(
        (
            spool_proc[1],
            [
                0x5467103A,  # Count callback commands in the supplied refCon.
                0x7D06382E,
                0x39080001,
                0x7D06392E,
                0x2C030004,
                0x40820040,
                0x8066001C,  # Optional write error.
                0x2C030000,
                0x4C820020,
                0x81040000,
                0x81260018,
                0x39460020,
                0x7D4A4A14,
                0x7D294214,
                0x91260018,
                0x7D0903A6,
                0x88E50000,
                0x98EA0000,
                0x38A50001,
                0x394A0001,
                0x4200FFF0,
                0x38600000,
                0x4E800020,
            ],
        )
    )
    probe.call("NewCMFlattenUPP", [spool_proc])
    p.nonzero("allocate the mixed-mode profile spool callback")
    p.emit(0x907E0000 | (base + 44))
    for original in (True, False):
        for status in (0, -36):
            probe.arguments([spool_state, status], 0)
            p.emit(0x38000000, *(0x90030000 | offset for offset in (8, 16, 20, 24)), 0x9083001C)
            store(12, 0xFFFFFFFF)
            checked(
                "CMFlattenProfile",
                [profile_ref, 0, probe.slot(base + 44), spool_state, probe.ptr(base + 12)],
                status,
                original,
            )
            check(12, 0x00FFFFFF, "a standard ICC spool clears the preferred CMM flag")
            for offset, expected in (
                (8, 1),
                (16, 2 if not status else 1),
                (20, 0 if status else 1),
                (24, 0 if status else 4384),
            ):
                probe.arguments([spool_state], 0)
                p.emit(0x80630000 | offset)
                p.expect(expected, "the spool callback receives the expected commands and byte count")
            if not status:
                for offset, expected in ((32, 4384), (68, 0x61637370), (168, 168), (32 + 4096, 0xA7A7A7A7)):
                    probe.arguments([spool_state], 0)
                    p.emit(0x80630000 | offset)
                    p.expect(expected, "the callback receives the serialized ICC header and edited payload")
    probe.call("DisposeCMFlattenUPP", [probe.slot(base + 44)])
    read_proc = probe.far_raw(bytes(8))
    p.routines.append(
        (
            read_proc[1],
            [
                0x5467103A,  # Count open-read, read, and close separately.
                0x7D06382E,
                0x39080001,
                0x7D06392E,
                0x2C030003,
                0x40820064,
                0x81260018,  # Read cursor and total input bytes.
                0x81040000,
                0x8166001C,
                0x7D695850,
                0x39800000,
                0x7C085840,
                0x4081000C,
                0x7D685B78,
                0x3980FFD9,  # eofErr if the spool cannot supply the full request.
                0x91040000,
                0x2C080000,
                0x4182002C,
                0x39460020,
                0x7D4A4A14,
                0x7D294214,
                0x91260018,
                0x7D0903A6,
                0x88EA0000,
                0x98E50000,
                0x394A0001,
                0x38A50001,
                0x4200FFF0,
                0x7D836378,
                0x4E800020,
                0x38600000,
                0x4E800020,
            ],
        )
    )
    probe.call("NewCMFlattenUPP", [read_proc])
    p.nonzero("allocate the profile spool reader")
    p.emit(0x907E0000 | (base + 44))
    restore_buffer = probe.far_raw(bytes(4400))
    for kind in (2, 6):
        for original in (True, False):
            if kind == 2:
                probe.call("NewHandle", [64])
                p.nonzero("allocate the unflatten destination handle")
                p.emit(0x907E0000 | (base + 40))
                restore_backing = probe.slot(base + 40)
            else:
                restore_backing = restore_buffer
            probe.arguments([copy_location, restore_backing, kind, 4400], 0)
            p.emit(0xB0A30000, 0x90830002, 0x90C30006)
            probe.arguments([spool_state, 4384], 0)
            p.emit(0x38000000, *(0x90030000 | offset for offset in (4, 12, 20, 24)), 0x9083001C)
            checked(
                "NCMUnflattenProfile",
                [copy_location, probe.slot(base + 44), spool_state, probe.ptr(base + 12)],
                original=original,
            )
            for offset, expected in ((4, 1), (12, 2), (20, 1), (24, 4384)):
                probe.arguments([spool_state], 0)
                p.emit(0x80630000 | offset)
                p.expect(expected, "unflatten reads and closes the complete ICC stream")
            checked("CMOpenProfile", [copy_ref, copy_location], original=not original)
            checked("CMGetProfileElement", [probe.slot(base + 36), info, size, 0], original=True)
            check(8, 4200, "the reopened profile sees the full restored tag")
            store(8, 4)
            checked("CMGetPartialProfileElement", [probe.slot(base + 36), info, 4196, size, output], original=True)
            check(64, 0xA7A7A7A7, "unflatten preserves the final bytes of a multi-chunk tag")
            checked("CMCloseProfile", [probe.slot(base + 36)])
            if kind == 2:
                probe.call("GetHandleSize", [restore_backing])
                p.expect(4384, "unflatten resizes the caller's handle")
                probe.call("DisposeHandle", [restore_backing])
    probe.arguments([restore_buffer, 0x12345678], 0)
    p.emit(0x90830000)
    probe.arguments([spool_state, 100], 0)
    p.emit(0x38000000, 0x90030018, 0x9083001C)
    checked("NCMUnflattenProfile", [copy_location, probe.slot(base + 44), spool_state, probe.ptr(base + 12)], -39)
    probe.arguments([restore_buffer], 0)
    p.emit(0x80630000)
    p.expect(0x12345678, "premature EOF preserves the caller's destination buffer")
    probe.arguments([copy_location, 100], 0)
    p.emit(0x90830006)
    probe.arguments([spool_state, 4384], 0)
    p.emit(0x38000000, 0x90030018, 0x9083001C)
    checked("NCMUnflattenProfile", [copy_location, probe.slot(base + 44), spool_state, probe.ptr(base + 12)], -108)
    probe.arguments([restore_buffer], 0)
    p.emit(0x80630000)
    p.expect(0x12345678, "an undersized destination remains intact")
    probe.call("DisposeCMFlattenUPP", [probe.slot(base + 44)])
    checked("CMCloseProfile", [profile_ref], original=True)
    for original in (True, False):
        checked("CMNewProfile", [profile, 0], original=original)
        checked("CMGetProfileHeader", [profile_ref, output], original=not original)
        check(64, 0, "a new profile begins without serialized bytes")
        check(72, 0x02000000, "a new profile defaults to the ICC version 2 header")
        checked("CMCountProfileElements", [profile_ref, size], original=not original)
        check(8, 0, "a new profile begins with an empty tag directory")
        checked("CMCloseProfile", [profile_ref], original=not original)

    blank = probe.far_raw(bytes(256))
    probe.arguments([location, blank, 256], 0)
    p.emit(0x90830002, 0x90A30006)
    checked("CMNewProfile", [profile, location])
    checked("CMSetProfileHeader", [profile_ref, data], original=True)
    checked("CMSetProfileElement", [profile_ref, info, 13, updated_tag])
    checked("CMUpdateProfile", [profile_ref])
    checked("CMCloseProfile", [profile_ref])
    checked("CMOpenProfile", [profile, location], original=True)
    checked("CMCountProfileElements", [profile_ref, size], original=True)
    check(8, 1, "the saved reader sees the directory of a newly written native profile")
    checked("CMGetProfileHeader", [profile_ref, output], original=True)
    check(64, 160, "the saved reader sees the serialized size of a newly written profile")
    checked("CMCloseProfile", [profile_ref], original=True)
    bad_header = probe.far_raw(bytes(160))
    probe.arguments([location, bad_header, 160], 0)
    p.emit(0x90830002, 0x90A30006)
    store(4, 0xFFFFFFFF)
    checked("CMOpenProfile", [profile, location], -170)
    check(4, 0, "an invalid ICC header leaves no live profile reference")
    probe.arguments([location, 0], 0)
    p.emit(0x90830006)
    for original in (True, False):
        checked("CMOpenProfile", [profile, location], -4205, original)
        checked("CMNewProfile", [profile, location], -4205, original)
    ink = probe.far_raw(b"\x03Ink\0")
    absent = probe.far_raw(b"\x03ink\0")
    long_name = b"0123456789abcdefghijklmnopqrstu"
    long_pascal = probe.far_raw(bytes([len(long_name)]) + long_name + b"\0")
    for channels in (0, 3, 4):
        space = b"CMYK" if channels == 4 else b"RGB "
        palette = struct.pack(">4s4I32s32s", b"ncl2", 0, 0, 2, channels, b"Lab ", b" Matte")
        for name, first in ((b"Ink", 0x1234), (long_name, 0xABCD)):
            coords = (first, 0x4567, 0x89AB, 0x0123, 0xFEDC, 0x2468, 0xACE0)
            palette += struct.pack(">32s" + "H" * (3 + channels), name, *coords[: 3 + channels])
        icc = bytearray(profile_bytes[:128])
        struct.pack_into(">I", icc, 0, 144 + len(palette))
        struct.pack_into(">4s4s4s", icc, 12, b"nmcl", space, b"Lab ")
        icc += struct.pack(">I4sII", 1, b"ncl2", 144, len(palette)) + palette
        profile_data = probe.far_raw(icc)
        probe.arguments([location, profile_data, len(icc)], 0)
        p.emit(0x90830002, 0x90A30006)
        checked("CMOpenProfile", [profile, location], original=True)
        for original in (True, False):
            prefix, suffix = probe.ptr(base + 104), probe.ptr(base + 144)
            checked(
                "CMGetNamedColorInfo",
                [profile_ref, size, probe.ptr(base + 12), probe.ptr(base + 16), probe.ptr(base + 20), prefix, suffix],
                original=original,
            )
            check(8, channels, "named color metadata reports device channel count")
            check(12, int.from_bytes(space, "big"), "named color metadata reports device space")
            check(16, 0x4C616220, "named color metadata reports the PCS")
            check(20, 2, "named color metadata reports entry count")
            check(104, 0x044C6162, "named color prefix is a Pascal string")
            check(144, 0x06204D61, "named color suffix is a Pascal string")
            checked("CMGetNamedColorIndex", [profile_ref, ink, size], original=original)
            check(8, 1, "named color indices start at one")
            checked("CMGetNamedColorIndex", [profile_ref, long_pascal, size], original=original)
            check(8, 2, "the maximum length name resolves")
            checked("CMGetNamedColorIndex", [profile_ref, absent, size], -4216, original)
            check(8, 0, "failed name lookup clears the index")
            checked("CMGetNamedColorName", [profile_ref, 1, suffix], original=original)
            check(144, 0x03496E6B, "index returns a packed Pascal name")
            checked("CMGetNamedColorName", [profile_ref, 3, suffix], -4216, original)
            check(144, 0x03496E6B, "out-of-range lookup preserves the name buffer")
            for accessor, key, expected in (
                ("CMGetNamedColorValue", long_pascal, 0xABCD4567),
                ("CMGetIndNamedColorValue", 1, 0x12344567),
            ):
                checked(accessor, [profile_ref, key, output, probe.ptr(base + 80)], original=original)
                check(80, expected, "named color returns the selected PCS coordinates")
                p.emit(0xA07E0000 | (base + 84))
                p.expect(0x89AB, "named color returns the third PCS coordinate")
                if channels:
                    check(64, 0x0123FEDC, "named color returns both leading device coordinates")
                    p.emit(0xA07E0000 | (base + 68))
                    p.expect(0x2468, "named color returns the third device coordinate")
                if channels == 4:
                    p.emit(0xA07E0000 | (base + 70))
                    p.expect(0xACE0, "CMYK palettes retain the fourth device coordinate")
        checked("CMCloseProfile", [profile_ref], original=True)

    struct.pack_into(">I", icc, 144 + 12, 0xFFFFFFFF)
    truncated = probe.far_raw(icc)
    probe.arguments([location, truncated], 0)
    p.emit(0x90830002)
    checked("CMOpenProfile", [profile, location], original=True)
    checked("CMGetNamedColorIndex", [profile_ref, ink, size], -4204)
    check(8, 0, "an invalid named color record extent clears the index")
    checked("CMCloseProfile", [profile_ref], original=True)
    for original in (False, True):
        for name, arguments in (
            ("CMInitWorkflows", [0]),
            ("CMTermWorkflows", [0]),
            ("CMSaveWorkflows", [0]),
            ("CMRevertWorkflows", [0]),
            ("CMGetIndWorkflow", [0, 0, size, 0]),
            ("CMGetWorkflowName", [0, 0, output, size]),
            ("CMGetWorkflowDesc", [0, 0, output, size]),
            ("CMSetWorkflowName", [0, 0, output]),
            ("CMSetWorkflowDesc", [0, 0, output]),
            ("CMGetWorkflowDates", [0, 0, output, 0]),
            ("CMActivateWorkflow", [0, 0]),
            ("CMActivateCurrentWorkflow", [0, size, 0, 0, 0]),
        ):
            checked(name, arguments, -50, original)
        for name, arguments in (
            ("CMCountWorkflows", [0]),
            ("CMWorkflowIsActive", [0, 0]),
            ("CMWorkflowIsModified", [0, 0]),
        ):
            checked(name, arguments, 0, original)

    for side, original in enumerate((False, True)):
        scratch = 16 + side * 16
        store(scratch + 4, 0)
        checked("CMInitWorkflows", [probe.ptr(base + scratch + 4)], None, original)
        p.emit(0x907E0000 | base + scratch)
        context = probe.slot(base + scratch + 4)
        checked("CMCountWorkflows", [context], None, original)
        p.emit(0x907E0000 | base + scratch + 8)
    compare(16, 32, "workflow initialization returns the saved status")
    compare(24, 40, "native and saved initialization enumerate the same number of workflows")

    context = probe.slot(base + 36)
    for side, original in enumerate((False, True)):
        scratch = 56 + side * 12
        store(scratch + 4, 0)
        store(scratch + 8, 0x5A5A5A5A)
        checked(
            "CMGetIndWorkflow",
            [context, 0, probe.ptr(base + scratch + 4), probe.ptr(base + scratch + 8)],
            None,
            original,
        )
        p.emit(0x907E0000 | base + scratch)
    for offset in (0, 4, 8):
        compare(56 + offset, 68 + offset, "indexed workflow and active flag agree with the saved library")

    p.emit(0x807E0000 | base + 72, 0x907E0000 | base + 52)
    workflow = probe.slot(base + 52)
    for name, arguments in (
        ("CMWorkflowIsActive", [context, workflow]),
        ("CMWorkflowIsModified", [context, workflow]),
        ("CMGetWorkflowDates", [context, workflow, probe.ptr(base + 112), probe.ptr(base + 124)]),
        ("CMGetWorkflowName", [context, workflow, 0, size]),
        ("CMGetWorkflowDesc", [context, workflow, 0, size]),
        ("CMSetWorkflowName", [context, workflow, 0]),
        ("CMSetWorkflowDesc", [context, workflow, 0]),
        ("CMActivateCurrentWorkflow", [context, size, 1, 1, 0]),
    ):
        for side, original in enumerate((False, True)):
            store(8, 0x5A5A5A5A)
            checked(name, arguments, None, original)
            p.emit(0x907E0000 | base + 16 + side * 16)
            if name in ("CMGetWorkflowName", "CMGetWorkflowDesc"):
                p.emit(0x807E0000 | base + 8, 0x907E0000 | base + 24 + side * 16)
        compare(16, 32, name + " matches the saved workflow status")
        if name in ("CMGetWorkflowName", "CMGetWorkflowDesc"):
            compare(24, 40, name + " publishes the same text size")

    checked("CMTermWorkflows", [probe.slot(base + 20)], None, False)
    checked("CMTermWorkflows", [context], None, True)
    probe.call("CloseConnection", [connection])
    p.expect(0, "close the saved ColorSync connection")


DEBUG_REGISTRY_APIS = (
    "TaskLevel",
    "DebugAssert",
    "InstallDebugAssertOutputHandler",
    "NewDebugComponent",
    "DisposeDebugComponent",
    "GetDebugComponentInfo",
    "NewDebugOption",
    "GetDebugOptionInfo",
    "SetDebugOptionValue",
)


HEAP_DIAGNOSTIC_APIS = ("CheckAllHeaps", "IsHeapValid", "IsPointerValid", "IsHandleValid", "MoreMasterPointers")


def heap_diagnostic_operations(probe):
    p = probe.p
    base = 0x2000
    pointer, handle = probe.slot(base), probe.slot(base + 4)
    probe.call("CheckAllHeaps")
    p.expect(1, "CheckAllHeaps validates the system and application heaps")
    probe.call("IsHeapValid")
    p.expect(1, "IsHeapValid validates the application heap")
    probe.call("MoreMasterPointers", [5])
    probe.call("MemError")
    p.expect(0, "MoreMasterPointers reserves entries in the current heap")
    probe.call("NewPtr", [16])
    p.nonzero("Allocate a nonrelocatable test pointer")
    p.emit(0x907E0000 | base)
    probe.call("IsPointerValid", [pointer])
    p.expect(1, "A live nonrelocatable block has a valid data pointer")
    probe.call("IsPointerValid", [0])
    p.expect(0, "A null pointer fails heap validation")
    probe.call("NewHandle", [16])
    p.nonzero("Allocate a relocatable test handle")
    p.emit(0x907E0000 | (base + 4))
    probe.call("IsHandleValid", [handle])
    p.expect(1, "A live handle and its relocatable block agree")
    probe.call("IsHandleValid", [pointer])
    p.expect(0, "An allocated pointer is not a relocatable handle")
    probe.call("DisposeHandle", [handle])
    probe.call("IsHandleValid", [handle])
    p.expect(0, "A released master pointer fails heap validation")
    probe.call("DisposePtr", [pointer])
    probe.call("IsPointerValid", [pointer])
    p.expect(0, "A disposed nonrelocatable block fails heap validation")
    p.emit(0x38600000, 0x907E0000 | base, 0x907E0000 | (base + 4))


def debug_registry_operations(probe):
    p = probe.p
    base = 0x2000  # Shared fixture scratch, cleared before leaving this section.
    name = probe.far_raw(b"\x04Demo")
    option_name = probe.far_raw(b"\x05Trace")
    name_out = probe.far_raw(bytes(256))
    signature, setting, state = probe.ptr(base), probe.ptr(base + 4), base + 8
    descriptor_value = probe.slot(base + 12)
    callback = probe.far_raw(bytes(8))
    p.routines.append(
        (
            callback[1],
            [
                0x2C040002,  # cmpwi r4, kSetDebugOption
                0x41820010,
                0x88C20000 | state,
                0x98C50000,
                0x4E800020,
                0x88C50000,
                0x98C20000 | state,
                0x4E800020,
            ],
        )
    )

    def expect(name, arguments, expected=0):
        probe.call(name, arguments)
        p.expect(expected, name + " returns the expected DebugLib status")

    component = int.from_bytes(b"rDbg", "big")
    probe.call("NewDebugComponentCallbackUPP", [callback])
    p.nonzero("allocate a component option callback")
    p.emit(0x907E0000 | (base + 12))
    expect("NewDebugComponent", [component, 0, 0], -13885)
    expect("NewDebugComponent", [component, name, descriptor_value])
    expect("NewDebugComponent", [component, name, 0], -13881)
    expect("GetDebugComponentInfo", [1, signature, name_out])
    p.emit(0x807E0000 | base)
    p.expect(component, "component enumeration writes the signature")
    probe.arguments([name_out], 0)
    p.emit(0x80630000)
    p.expect(0x0444656D, "component enumeration writes the registered Pascal name")

    expect("GetDebugOptionInfo", [0, component, signature, name_out, setting])
    p.emit(0x887E0000 | (base + 4))
    p.expect(1, "component-level debug breaks start enabled")
    expect("SetDebugOptionValue", [component, 0, 0])
    expect("GetDebugOptionInfo", [0, component, signature, name_out, setting])
    p.emit(0x887E0000 | (base + 4))
    p.expect(0, "the built-in option can disable component breaks")

    expect("NewDebugOption", [component, 42, option_name])
    expect("NewDebugOption", [component, 42, option_name], -13882)
    expect("SetDebugOptionValue", [component, 19, 1], -13884)
    expect("SetDebugOptionValue", [component, 42, 1])
    expect("GetDebugOptionInfo", [1, component, signature, name_out, setting])
    p.emit(0x807E0000 | base)
    p.expect(42, "the option's index resolves to its registered selector")
    p.emit(0x887E0000 | (base + 4))
    p.expect(1, "option lookup invokes the callback and returns its value")
    expect("DisposeDebugComponent", [component])
    expect("GetDebugOptionInfo", [1, component, signature, name_out, setting], -13883)
    expect("GetDebugComponentInfo", [1, signature, name_out], -13887)
    probe.call("DisposeDebugComponentCallbackUPP", [descriptor_value])
    # The busy bits are in the guest queue headers. Restore both before
    # checking the status so later sections see the original queues.
    p.emit(0xA0600160, 0x907E0000 | base, 0xA0600D92, 0x907E0000 | (base + 4), 0x38604000, 0xB0600160, 0xB0600D92)
    probe.call("TaskLevel")
    p.emit(0x809E0000 | base, 0xB0800160, 0x809E0000 | (base + 4), 0xB0800D92)
    p.expect(0x30, "TaskLevel reports active VBL and deferred-task queues")

    handler = probe.far_raw(bytes(8))
    p.routines.append(
        (
            handler[1],
            [
                0x90620000 | base,
                0x90820000 | (base + 4),
                0x81610038,  # Read the ninth PPC argument, the Pascal message.
                0x818B0000,
                0x91820000 | (base + 8),
                0x818B0020,
                0x91820000 | (base + 16),
                0x4E800020,
            ],
        )
    )
    probe.call("NewDebugAssertOutputHandlerUPP", [handler])
    p.nonzero("allocate a DebugAssert output handler")
    p.emit(0x907E0000 | (base + 12))
    probe.call("InstallDebugAssertOutputHandler", [descriptor_value])
    probe.call(
        "DebugAssert",
        [component, 17, probe.far_raw(b"X\x00"), 0, 0, probe.far_raw(b"Disk:Sources:Unit.c\x00"), 37, -42],
    )
    message = b"Assert failed: X\r     file: Unit.c\r     line: 37\r    error: -42"
    encoded = bytes([len(message)]) + message
    for slot, expected, label in (
        (0, component, "assert handler receives the unmodified signature"),
        (4, 17, "assert handler receives the unmodified options"),
        (8, int.from_bytes(encoded[:4], "big"), "assert handler receives a Pascal message"),
        (16, int.from_bytes(encoded[32:36], "big"), "assert message prints the file basename and next line"),
    ):
        p.emit(0x807E0000 | (base + slot))
        p.expect(expected, label)
    probe.call("InstallDebugAssertOutputHandler", [0])
    probe.call("DisposeDebugAssertOutputHandlerUPP", [descriptor_value])
    p.emit(
        0x38600000,
        *(0x907E0000 | (base + offset) for offset in range(0, 20, 4)),
    )


def locale_operations(probe):
    p = probe.p
    _, base = probe.far_raw(bytes(160))
    text = probe.far_raw(b"EN-us.AB-z_ca-X\0")
    expected = b"en-us.Ab-Z_CA-X\0"
    for side in (0, 1):
        probe.call("LocaleRefFromLocaleString", [text, probe.ptr(base + side * 4)], side=side)
        p.expect(0, "parse a mixed-case locale part string")
        probe.arguments([probe.slot(base + side * 4)], 0)
        p.nonzero("parsing returns a live locale reference")
    probe.arguments([probe.slot(base), probe.slot(base + 4)], 0)
    p.equal("the native and saved locale entries intern into the same locale table")
    for side in (0, 1):
        probe.call("LocaleRefGetPartString", [probe.slot(base), 0x3F, 20, probe.ptr(base + 8)], side=side)
        p.expect(0, "format all locale parts")
        for offset in range(0, len(expected), 4):
            probe.arguments([probe.ptr(base + 8 + offset)], 0)
            p.emit(0x80630000)
            p.expect(int.from_bytes(expected[offset : offset + 4], "big"), "locale parts use the classic casing")
        probe.call("LocaleRefGetPartString", [probe.slot(base), 0x3F, 4, probe.ptr(base + 8)], side=side)
        p.expect(-30001, "a short locale buffer preserves whole parts")
        probe.arguments([probe.ptr(base + 8)], 0)
        p.emit(0x80630000, 0x5463002E)
        p.expect(0x656E0000, "the language part is still terminated")

    klass = 0x75636F6C
    for side in (0, 1):
        probe.call("LocaleOperationCountLocales", [klass, probe.ptr(base + 32 + side * 4)], side=side)
        p.expect(0, "count locales in the original collation table")
        probe.call(
            "LocaleOperationGetLocales",
            [klass, 4, probe.ptr(base + 40 + side * 4), probe.ptr(base + 64 + side * 32)],
            side=side,
        )
        probe.arguments([probe.ptr(base + 48 + side * 4)], 1)
        p.emit(0x90640000)
    for offset in (32, 40, 48):
        probe.arguments([probe.slot(base + offset), probe.slot(base + offset + 4)], 0)
        p.equal("native locale enumeration agrees with the saved fragment")
    for offset in range(0, 32, 4):
        probe.arguments([probe.slot(base + 64 + offset), probe.slot(base + 96 + offset)], 0)
        p.equal("the same locale references and variants are returned in order")

    us_locale = probe.far_raw(b"en_US\0")
    unknown_locale = probe.far_raw(b"zz_ZZ\0")
    _, region_names = probe.far_raw(bytes(128))
    probe.call("LocaleRefFromLocaleString", [us_locale, probe.ptr(base)])
    p.expect(0, "intern the US English locale")
    for side in (0, 1):
        probe.call("LocaleRefFromLangOrRegionCode", [0, 0, probe.ptr(base + 4)], side=side)
        p.expect(0, "map the classic US English codes to a locale")
        probe.arguments([probe.slot(base), probe.slot(base + 4)], 0)
        p.equal("locale code and string lookups return the same reference")
        probe.call("LocaleRefFromLangOrRegionCode", [11, 0, probe.ptr(base + 4)], side=side)
        p.expect(-50, "reject Japanese language with the US English region")
        probe.arguments([probe.slot(base + 4)], 0)
        p.expect(0, "an incompatible code pair clears the locale output")

        for name, status, codes in ((us_locale, 0, 0), (unknown_locale, -50, 0xFF80FF80)):
            probe.call(
                "LocaleStringToLangAndRegionCodes", [name, probe.ptr(base + 16), probe.ptr(base + 18)], side=side
            )
            p.expect(status, "convert a locale string to classic codes")
            probe.arguments([probe.slot(base + 16)], 0)
            p.expect(codes, "return US English codes or the unspecified code")

        probe.call("LocaleGetRegionLanguageName", [0, probe.ptr(region_names + side * 64)], side=side)
        probe.arguments([probe.ptr(base + 24 + side * 4)], 1)
        p.emit(0x90640000)
    probe.arguments([probe.slot(base + 24), probe.slot(base + 28)], 0)
    p.equal("the native and saved region name lookups return the same status")
    for offset in (0, 4):
        probe.arguments([probe.slot(region_names + offset), probe.slot(region_names + 64 + offset)], 0)
        p.equal("the native and saved region names have the same Pascal string prefix")

    _, names = probe.far_raw(bytes(120))
    length, display, buffer = probe.ptr(names), probe.ptr(names + 4), probe.ptr(names + 8)
    us = probe.slot(base)
    for side in (0, 1):
        for prefix, args, expected in (
            ("Locale", [us, 0, 1], "English"),
            ("LocaleOperation", [klass], "Text sort order"),
        ):
            probe.call(prefix + "CountNames", [*args, length], side=side)
            p.expect(0, "count display translations")
            probe.arguments([probe.slot(names)], 0)
            p.nonzero("the resource has a display translation")
            for suffix, tail in (
                ("GetName", [us, 40, length, buffer]),
                ("GetIndName", [0, 40, length, buffer, display]),
            ):
                probe.call(prefix + suffix, [*args, *tail], side=side)
                p.expect(0, "read a localized display name")
                probe.arguments([probe.slot(names)], 0)
                p.expect(len(expected), "report the Unicode character count")
                expected_utf16 = expected.encode("utf-16-be")
                for offset in range(0, len(expected_utf16), 2):
                    probe.arguments([buffer], 0)
                    p.emit(0xA0630000 | offset)
                    p.expect(int.from_bytes(expected_utf16[offset : offset + 2], "big"), "match a display code unit")
            probe.arguments([probe.slot(names + 4)], 0)
            p.nonzero("an indexed name returns its display locale")
            probe.call(prefix + "GetIndName", [*args, 0, 1, length, buffer, display], side=side)
            p.expect(-30001, "reject a short display name buffer")
            probe.arguments([probe.slot(names)], 0)
            p.expect(0, "a short buffer yields no partial name")

        token, first, second = (probe.ptr(names + offset) for offset in (88, 92, 96))
        for handle in (first, second):
            probe.call("LocaleOperationLoadData", [0x6C6E616D, us, 0, token, handle], side=side)
            p.expect(0, "load the English language name resource")
        probe.arguments([probe.slot(names + 92), probe.slot(names + 96)], 0)
        p.equal("consecutive operation loads share the cached resource handle")
        for offset, handle in ((92, first), (96, second)):
            probe.call("LocaleOperationUnloadData", [probe.slot(names + 88), handle], side=side)
            p.expect(0, "release one reference to the operation data")
            probe.arguments([probe.slot(names + offset)], 0)
            p.expect(0, "unloading clears the released handle")

    _, collation = probe.far_raw(bytes(8))
    for left, right, equal, order in (
        ((10, 1, 4), (11, 1, 4), 0, -2),
        ((11, 1, 4), (10, 1, 4), 0, 2),
        ((10, 0, 4), (10, 0, 5), 0, -1),
        ((10, 1, 4), (10, 1, 5), 1, -1),
        ((10, 1, 4), (10, 1, 4), 1, 0),
        ((10, 1, 4), (10, 1), 1, -1),
    ):
        left_key = probe.far_raw(struct.pack(f">{len(left)}I", *left))
        right_key = probe.far_raw(struct.pack(f">{len(right)}I", *right))
        probe.call(
            "UCCompareCollationKeys",
            [left_key, len(left), right_key, len(right), probe.ptr(collation), probe.ptr(collation + 4)],
        )
        p.expect(0, "compare classic Unicode collation keys")
        probe.arguments([probe.ptr(collation)], 0)
        p.emit(0x88630000)
        p.expect(equal, "report equivalence at the significant weight levels")
        probe.arguments([probe.slot(collation + 4)], 0)
        p.expect(order, "return the signed collation order and weight")


def appearance_geometry_operations(probe):
    p = probe.p
    output = probe.far_raw(bytes(8))
    top, left, bottom, right = 10, 20, 30, 90
    _, theme_output = probe.far_raw(bytes(4))
    theme = probe.slot(theme_output)
    theme_text = probe.far_raw(bytes(40))

    probe.call("CopyThemeIdentifier", [0])
    p.expect(-50, "CopyThemeIdentifier rejects a null output")
    probe.call("CopyThemeIdentifier", [probe.ptr(theme_output)])
    p.expect(0, "CopyThemeIdentifier returns an owned CFString")
    probe.call("CFStringGetCString", [theme, theme_text, 40, 0x0600])
    p.expect(1, "read the current classic theme identifier")
    expected_theme = b"com.apple.theme.appearance.platinum\0"
    for offset in range(0, len(expected_theme), 4):
        probe.arguments([theme_text], 0)
        p.emit(0x80630000 | offset)
        p.expect(int.from_bytes(expected_theme[offset : offset + 4], "big"), "the classic theme identifies Platinum")
    probe.call("CFRelease", [theme])

    def check_rect(expected, label):
        for offset in (0, 4):
            probe.arguments([output], 0)
            p.emit(0x80630000 | offset)
            p.expect(int.from_bytes(expected[offset : offset + 4], "big"), f"{label} rect word {offset // 4}")

    button = probe.far_raw(struct.pack(">4h", top, left, bottom, right))
    vertical_tab = probe.far_raw(struct.pack(">4h", 10, 20, 80, 44))
    focused = probe.far_raw(struct.pack(">IHH", 1, 0, 4))
    indicator = probe.far_raw(struct.pack(">IHH", 1, 1, 0x30))
    plain = probe.far_raw(struct.pack(">IHH", 1, 0, 0))
    for kind, draw_info, background, content in (
        (0, focused, (8, 18, 32, 92), (12, 22, 28, 88)),
        (1, indicator, (14, 74, 26, 90), (10, 20, 30, 72)),
        (5, plain, (10, 20, 30, 90), (12, 26, 28, 62)),
        (7, plain, (10, 20, 33, 33), (10, 35, 30, 90)),
        (9, plain, (10, 20, 30, 90), (14, 24, 26, 86)),
    ):
        for name, expected in (
            ("GetThemeButtonBackgroundBounds", background),
            ("GetThemeButtonContentBounds", content),
        ):
            for side in (0, 1):
                probe.call(name, [button, kind, draw_info, output], side=side)
                p.expect(0, f"{name} measures Platinum button kind {kind}")
                check_rect(struct.pack(">4h", *expected), f"button kind {kind}")

    for direction, small, expected in (
        (0, 0, (10, 20, 26, 36)),
        (1, 1, (10, 9, 21, 20)),
    ):
        for side in (0, 1):
            probe.call("GetThemeStandaloneGrowBoxBounds", [(top << 16) | left, direction, small, output], side=side)
            p.expect(0, "measure a standalone grow box")
            check_rect(struct.pack(">4h", *expected), "standalone grow box")

    for kind, flags, thumb, basis, status, expected in (
        (0, 1, 0, (top, left, bottom, right), 0, (top, left, bottom, right)),
        (1, 0, 0, (bottom, left, top, right), -50, (bottom, left, top, right)),
        (2, 1, 2, (top, left, bottom, right), 0, (top, left + 2, top + 16, right - 2)),
        (2, 2, 1, (top, left, right, bottom), 0, (top + 2, bottom - 16, right - 2, bottom)),
        (3, 1, 0, (top, left, bottom, right), 0, (top, left, top + 14, right)),
        (4, 1, 0, (top, left, bottom, right), 0, (top, left, top + 14, right)),
    ):
        track = probe.far_raw(struct.pack(">H4h4iHBBB", kind, *basis, 0, 100, 50, 0, flags, 0, 0, thumb))
        for side in (0, 1):
            probe.call("GetThemeTrackBounds", [track, output], side=side)
            p.expect(status, f"measure track kind {kind}")
            check_rect(struct.pack(">4h", *expected), f"track kind {kind}")

    slider = probe.far_raw(struct.pack(">H4h4iHBBB", 2, top, left, bottom, right, 0, 100, 50, 0, 1, 0, 0, 2))
    _, region_address = probe.far_raw(struct.pack(">H4h", 10, 10, 53, 26, 68))
    region_handle = probe.far_raw(bytes(4))
    for side in (0, 1):
        probe.call("GetThemeTrackDragRect", [slider, output], side=side)
        p.expect(0, "measure the slider thumb's drag rectangle")
        check_rect(struct.pack(">4h", 10, 25, 26, 85), "slider drag rectangle")

        probe.arguments([probe.ptr(region_address), region_handle], 0)
        p.emit(0x90640000)
        for name, argument, expected in (
            ("GetThemeTrackThumbPositionFromOffset", 3, 26),
            ("GetThemeTrackThumbPositionFromRegion", region_handle, 28),
            ("GetThemeTrackLiveValue", 23, 51),
        ):
            probe.call(name, [slider, argument, output], side=side)
            p.expect(0, name + " measures slider travel")
            probe.arguments([output], 0)
            p.emit(0x80630000)
            p.expect(expected, name + " returns the expected slider position")

    _, thumb_slot = probe.far_raw(bytes(4))
    thumb = probe.slot(thumb_slot)
    probe.call("NewRgn")
    p.nonzero("allocate a caller-owned slider thumb region")
    p.emit(0x3C9E0000 | ((thumb_slot + 0x8000) >> 16), 0x90640000 | (thumb_slot & 0xFFFF))
    for side in (0, 1):
        probe.call("GetThemeButtonRegion", [button, 1, indicator, thumb], side=side)
        p.expect(0, "construct the right-to-left checkbox indicator region")
        probe.call("GetRegionBounds", [thumb, output])
        check_rect(struct.pack(">4h", 14, 74, 26, 90), "checkbox indicator")
        probe.call("GetThemeButtonRegion", [button, 0, focused, thumb], side=side)
        p.expect(0, "construct the focused push button region")
        probe.call("GetRegionBounds", [thumb, output])
        check_rect(struct.pack(">4h", 7, 17, 33, 93), "push button focus region")
        for tab, style, direction, region_bounds, points in (
            (button, 0, 0, (10, 20, 29, 90), ((10, 20, 0), (10, 30, 1), (27, 20, 1), (27, 45, 0))),
            (button, 3, 1, (11, 20, 30, 90), ((11, 20, 1), (29, 20, 0), (29, 45, 1))),
            (vertical_tab, 0, 2, (10, 23, 80, 47), ((10, 23, 1), (10, 43, 0), (19, 45, 1), (35, 45, 0))),
            (vertical_tab, 4, 3, (10, 20, 80, 44), ((10, 43, 1), (10, 20, 0), (35, 20, 1))),
        ):
            probe.call("GetThemeTabRegion", [tab, style, direction, thumb], side=side)
            p.expect(0, f"construct the tab region for direction {direction}")
            probe.call("GetRegionBounds", [thumb, output])
            check_rect(struct.pack(">4h", *region_bounds), "Platinum tab")
            for y, x, inside in points:
                probe.call("PtInRgn", [(y << 16) | x, thumb])
                p.expect(inside, f"Platinum tab direction {direction} covers the expected silhouette")
        probe.call("GetThemeTrackThumbRgn", [slider, thumb], side=side)
        p.expect(0, "construct the pointed slider thumb region")
        probe.call("GetRegionBounds", [thumb, output])
        check_rect(struct.pack(">4h", 10, 48, 26, 63), "slider thumb")
        for y, x, inside in ((10, 48, 0), (11, 48, 1), (25, 55, 1), (25, 49, 0)):
            probe.call("PtInRgn", [(y << 16) | x, thumb])
            p.expect(inside, "the thumb silhouette includes its point and clips its corners")
        for y, x, part in ((16, 35, 22), (16, 55, 129), (16, 80, 23), (41, 55, 0)):
            probe.call("HitTestThemeTrack", [slider, (y << 16) | x, output], side=side)
            p.expect(int(part != 0), "the slider reports whether the point hits a part")
            probe.arguments([output], 0)
            p.emit(0xA0630000)
            p.expect(part, "hit testing identifies the page and indicator parts")
    window = probe.far_raw(struct.pack(">4h", 40, 40, 100, 120))
    metrics = probe.far_raw(struct.pack(">H4hH", 12, 20, 45, 0, 0, 0))
    appearance_hit = probe.far_raw(bytes(2))
    for flavor, attributes, code, expected in (
        (0, 0x80, 0, (14, 34, 40, 126)),
        (0, 0x80, 32, (14, 34, 107, 127)),
        (0, 0x80, 33, (40, 40, 100, 120)),
        (1, 0, 32, (37, 37, 104, 124)),
        (5, 0, 32, (39, 39, 101, 121)),
        (6, 0, 32, (39, 39, 103, 123)),
    ):
        probe.call("GetThemeWindowRegion", [flavor, window, 1, metrics, attributes, code, thumb])
        p.expect(0, f"measure Platinum window flavor {flavor} region {code}")
        probe.call("GetRegionBounds", [thumb, output])
        check_rect(struct.pack(">4h", *expected), "Platinum window")
    probe.call("GetThemeWindowRegion", [0, window, 1, metrics, 0x100, 33, thumb])
    p.expect(0, "collapsed document windows return an empty content region")
    probe.call("EmptyRgn", [thumb])
    p.expect(1, "collapsed document content has no points")
    for state, y, x, expected in ((1, 25, 70, 1), (7, 25, 70, 5), (1, 50, 60, 33), (1, 10, 20, 0)):
        probe.call("GetThemeWindowRegionHit", [0, window, state, metrics, 0x80, (y << 16) | x, appearance_hit])
        p.expect(int(bool(expected)), "the themed window reports a hit only in its structure region")
        probe.arguments([appearance_hit], 0)
        p.emit(0xA0630000)
        p.expect(expected, "the themed window returns its classic hit region code")
    probe.call("DisposeRgn", [thumb])

    original_style = probe.far_raw(bytes(4))
    probe.call("GetThemeScrollBarArrowStyle", [original_style])
    p.expect(0, "snapshot the existing scrollbar arrow preference")
    probe.call("SetThemeScrollBarArrowStyle", [1, 0])
    p.expect(0, "set the paired scrollbar arrow style")
    probe.call("GetThemeScrollBarArrowStyle", [output])
    p.expect(0, "read back the paired scrollbar arrow style")
    probe.arguments([output], 0)
    p.emit(0xA0630000)
    p.expect(1, "the getter sees the live scrollbar arrow style")
    probe.call("SetThemeScrollBarArrowStyle", [0, 0])
    p.expect(0, "measure classic scrollbars with single arrows")
    bar = probe.far_raw(struct.pack(">4h", 10, 20, 26, 120))
    part = probe.far_raw(bytes(4))
    for side in (0, 1):
        probe.call("GetThemeScrollBarTrackRect", [bar, 0, 0, 1, output], side=side)
        p.expect(0, "measure the track between the scrollbar arrows")
        check_rect(struct.pack(">4h", 10, 35, 26, 105), "scrollbar track")
        for horizontal_point, expected in ((22, 20), (75, 0), (119, 21)):
            probe.call(
                "HitTestThemeScrollBarArrows", [bar, 0, 0, 1, (16 << 16) | horizontal_point, output, part], side=side
            )
            p.expect(int(expected != 0), "hit-test the scrollbar arrows")
            probe.arguments([part], 0)
            p.emit(0xA0630000)
            p.expect(expected, "return the matching scrollbar part code")
    _, saved_offset = probe.raw(bytes(4))
    probe.arguments([original_style, probe.ptr(saved_offset)], 0)
    p.emit(0xA0630000, 0x90640000)
    probe.call("SetThemeScrollBarArrowStyle", [probe.slot(saved_offset), 0])
    p.expect(0, "restore the original scrollbar arrow preference")


def appearance_state_operations(probe):
    p = probe.p
    _, base = probe.raw(bytes(24))
    psn, saved, output = (probe.ptr(base + offset) for offset in (0, 8, 12))
    for side in (0, 1):
        for metric, pixels in ((0, 16), (10, 21), (14, 0), (17, None), (19, 20), (20, None), (0xFFFFFFFF, None)):
            p.emit(0x38607777, 0x907E0000 | (base + 12))
            probe.call("GetThemeMetric", [metric, output], side=side)
            p.expect(-50 if pixels is None else 0, f"theme metric {metric} returns its CarbonLib 1.4 status")
            p.emit(0x807E0000 | (base + 12))
            p.expect(pixels or 0, f"theme metric {metric} writes its pixel size or clears the output")
    probe.call("GetCurrentProcess", [psn])
    p.expect(0, "read the active Appearance client's process serial number")
    probe.call("IsAppearanceClient", [psn], side=1)
    p.emit(0x907E0000 | (base + 20))
    probe.call("IsAppearanceClient", [psn])
    p.emit(0x809E0000 | (base + 20))
    p.equal("native and saved Appearance agree on the initial registration")
    probe.call("UnregisterAppearanceClient", side=1)
    p.emit(0x809E0000 | (base + 20), 0x3884FFFF, 0x1C840000 | 30562)
    p.equal("removing the initial registration follows the saved state")
    probe.call("RegisterAppearanceClient")
    p.expect(0, "register the current process as an Appearance client")
    for side in (0, 1):
        probe.call("IsAppearanceClient", [psn], side=side)
        p.expect(1, "native and saved Appearance see the registration")
    probe.call("RegisterAppearanceClient")
    p.expect(-30561, "registering an existing Appearance client returns its duplicate error")
    probe.call("UnregisterAppearanceClient", side=1)
    p.expect(0, "the saved Appearance manager removes the native registration")
    probe.call("UnregisterAppearanceClient")
    p.expect(-30562, "the native Appearance manager observes saved removal")

    p.emit(0x807E0000 | (base + 20), 0x2C030000)
    skip_restore = len(p.code)
    p.emit(0)
    probe.call("RegisterAppearanceClient", side=1)
    p.expect(0, "restore the fixture's initial Appearance registration")
    p.code[skip_restore] = 0x41820000 | (len(p.code) - skip_restore) * 4

    for style, value in (("CheckBoxStyle", 1), ("ScrollBarThumbStyle", 1), ("ScrollBarArrowStyle", 2)):
        getter, setter = "GetTheme" + style, "SetTheme" + style
        probe.call(getter, [saved], side=1)
        p.expect(0, "save the original Appearance " + style)
        probe.call(setter, [value, 0])
        p.expect(0, "set a supported Appearance " + style)
        probe.call(getter, [output], side=1)
        p.expect(0, "the saved manager reads native " + style)
        p.emit(0xA07E0000 | (base + 12))
        p.expect(value, "the shared Appearance preference contains the new " + style)
        probe.call(setter, [value + 1, 0])
        p.expect(-50, "reject an unsupported Appearance " + style)
        p.emit(0xA07E0000 | (base + 8), 0x907E0000 | (base + 16))
        probe.call(setter, [probe.slot(base + 16), 0], side=1)
        p.expect(0, "restore the previous Appearance " + style)
        probe.call(getter, [output])
        p.expect(0, "native Appearance reads the restored " + style)
        p.emit(0xA07E0000 | (base + 12), 0xA09E0000 | (base + 8))
        p.equal("the native manager observes the restored " + style)

    for getter in ("GetThemeSoundMask", "GetThemeSoundPanningSpread"):
        probe.call(getter, [saved], side=1)
        p.expect(0, "read the saved Appearance sound preference")
        probe.call(getter, [output])
        p.expect(0, "read the native Appearance sound preference")
        p.emit(0x807E0000 | (base + 12), 0x809E0000 | (base + 8))
        p.equal(getter + " has the same value on both paths")
    probe.call("SetThemeSoundPanningSpread", [0x00008000])
    p.expect(0, "set the native sound panning spread")
    probe.call("GetThemeSoundPanningSpread", [output], side=1)
    p.expect(0, "read the changed panning spread from saved Appearance")
    p.emit(0x807E0000 | (base + 12))
    p.expect(0x00008000, "preserve the complete fixed-point panning spread")
    probe.call("SetThemeSoundPanningSpread", [probe.slot(base + 8)], side=1)
    p.expect(0, "restore the saved sound panning spread")
    probe.call("AreThemeSoundsEnabled", side=1)
    p.emit(0x907E0000 | (base + 8))
    probe.call("AreThemeSoundsEnabled")
    p.emit(0x809E0000 | (base + 8))
    p.equal("native and saved Appearance agree on sound availability")

    probe.call("SetThemeSoundsEnabled", [0])
    p.expect(0, "disable Appearance sounds")
    probe.call("AreThemeSoundsEnabled", side=1)
    p.expect(0, "saved Appearance observes native sound disable")
    probe.call("GetThemeSoundMask", [output], side=1)
    p.expect(0, "save the mask while playback is disabled")
    probe.call("SetThemeSoundMask", [1])
    p.expect(0, "set the native Appearance sound mask")
    probe.call("GetThemeSoundMask", [probe.ptr(base + 16)], side=1)
    p.expect(0, "read the native mask from saved Appearance")
    p.emit(0x807E0000 | (base + 16))
    p.expect(1, "saved Appearance sees the new event mask")
    for mask in (1, 0):
        probe.call("SetThemeSoundMask", [mask])
        p.expect(0, "select the Appearance sound mask")
        for name, args in (
            ("PlayThemeSound", [0x776F706E]),
            ("PlayThemeControlSound", [0x776F706E, 0]),
            ("BeginThemeDragSound", [0x776F706E]),
            ("BeginThemeDragIndicatorSound", [0x776F706E, 0]),
            ("EndThemeDragSound", []),
        ):
            probe.call(name, args, side=1)
            p.emit(0x907E0000 | (base + 16))
            probe.call(name, args)
            p.emit(0x809E0000 | (base + 16))
            p.equal(name + " honors disabled playback and the saved mask")
    probe.call("SetThemeSoundMask", [probe.slot(base + 12)])
    p.expect(0, "restore the previous Appearance sound mask")
    probe.call("SetThemeSoundsEnabled", [probe.slot(base + 8)])
    p.expect(0, "restore the previous Appearance sound availability")

    playlist = probe.raw(bytes(256))
    original = probe.raw(bytes(256))
    probe.call("GetThemePlaylist", [original], side=1)
    p.emit(0x907E0000 | (base + 20))
    probe.call("GetThemePlaylist", [playlist])
    p.emit(0x809E0000 | (base + 20))
    p.equal("native and saved Appearance report the same playlist status")
    p.emit(0x807E0000 | playlist[1], 0x809E0000 | original[1])
    p.equal("native and saved Appearance return the same playlist name prefix")
    probe.call("GetThemePlaylist", [0])
    p.expect(-50, "reject a null playlist destination")
    missing = b"ROMAW nonexistent sound set"
    missing_name = probe.raw(bytes([len(missing)]) + missing)
    probe.call("SetThemePlaylist", [missing_name], side=1)
    p.nonzero("saved Appearance rejects a missing playlist file")
    p.emit(0x907E0000 | (base + 20))
    probe.call("SetThemePlaylist", [missing_name])
    p.emit(0x809E0000 | (base + 20))
    p.equal("native Appearance reports the same missing playlist error")
    for side in (0, 1):
        probe.call("GetAppearance", [0, 0], side=side)
        p.expect(-50, "appearance name queries need at least one output")
    for theme in (True, False):
        probe.call("GetAppearance", [original, 0] if theme else [0, original], side=1)
        p.expect(0, "read the real Appearance engine name")
        probe.call("GetAppearance", [playlist, 0] if theme else [0, playlist])
        p.expect(0, "the native query reads the current engine name")
        p.emit(0x887E0000 | playlist[1], 0x889E0000 | original[1])
        p.equal("the returned Pascal names have the same lengths")
        probe.call("EqualString", [playlist, original, 1, 1])
        p.expect(1, "native and saved Appearance report the same name")
    probe.call("SetAppearance", [missing_name, 0, 0], side=1)
    p.nonzero("the saved manager rejects a missing theme")
    p.emit(0x907E0000 | (base + 20))
    probe.call("SetAppearance", [missing_name, 0, 0])
    p.emit(0x809E0000 | (base + 20))
    p.equal("native Appearance reports the same missing theme error")

    snapshot, modified = probe.slot(base + 8), probe.slot(base + 16)
    for offset in (8, 16):
        probe.call("NewCollection")
        p.nonzero("allocate a scenario collection")
        p.emit(0x907E0000 | (base + offset))
    for getter in ("GetTheme", "GetScenario"):
        probe.call(getter, [snapshot], side=1)
        p.expect(0, "capture the saved Appearance scenario")
        probe.call(getter, [modified])
        p.expect(0, "capture the same scenario in native code")
        for tag, width in ((b"sbar", 2), (b"sbth", 2), (b"snds", 1), (b"hcol", 6)):
            for collection, buffer in ((snapshot, original), (modified, playlist)):
                probe.call("GetCollectionItem", [collection, int.from_bytes(tag, "big"), 0, 0, buffer])
                p.expect(0, "scenario contains the requested setting")
            for index in range(width):
                p.emit(0x887E0000 | (playlist[1] + index), 0x889E0000 | (original[1] + index))
                p.equal("the scenario setting matches the saved implementation")
    arrow_style = probe.far_raw(b"\0\x02")
    for setter in ("SetTheme", "SetScenario"):
        probe.call("SetThemeScrollBarArrowStyle", [0, 0], side=1)
        p.expect(0, "start with a different saved scrollbar arrow style")
        probe.call("BlockMoveData", [arrow_style, output, 2])
        probe.call("AddCollectionItem", [modified, 0x73626172, 0, 2, output])
        p.expect(0, "replace the scenario's scrollbar arrow style")
        probe.call(setter, [modified])
        p.expect(0, "apply the scenario through the native entry")
        probe.call("GetThemeScrollBarArrowStyle", [output], side=1)
        p.expect(0, "read the resulting setting through the saved manager")
        p.emit(0xA07E0000 | (base + 12))
        p.expect(2, "the applied scenario updates the shared setting")
        probe.call(setter, [snapshot])
        p.expect(0, "restore the original Appearance scenario")
    for collection in (modified, snapshot):
        probe.call("DisposeCollection", [collection])

    for query in ("CanThemeDrawOnAllMonitors", "IsThemeCreditDialogAvailable"):
        probe.call(query, side=1)
        p.emit(0x907E0000 | (base + 20))
        probe.call(query)
        p.emit(0x809E0000 | (base + 20))
        p.equal("native and saved Appearance query the same engine capability")
    for color in (-2, -1, 0, 1, 47, 48):
        probe.call("GetThemeTextColor", [color, 32, 1, original], side=1)
        p.emit(0x907E0000 | (base + 20))
        probe.call("GetThemeTextColor", [color, 32, 1, playlist])
        p.emit(0x809E0000 | (base + 20))
        p.equal("text color lookup matches the real theme status")
        if color not in (0, 48):
            for offset in (0, 2, 4):
                p.emit(0xA07E0000 | (playlist[1] + offset), 0xA09E0000 | (original[1] + offset))
                p.equal("text color lookup returns the same RGB components")
    for name, args in (
        ("SetThemeBackgroundForWindow", [0, 32, 1]),
        ("SetThemeTextColorForWindow", [0, 1, 32, 1]),
    ):
        probe.call(name, args, side=1)
        p.emit(0x907E0000 | (base + 20))
        probe.call(name, args)
        p.emit(0x809E0000 | (base + 20))
        p.equal("a missing window returns the same theme color status")
    probe.call("LMGetHiliteRGB", [original])
    color = probe.far_raw(struct.pack(">3H", 0x2222, 0x7777, 0xAAAA))
    _, ports = probe.raw(bytes(8))
    world = probe.slot(ports + 4)
    bounds = probe.raw(struct.pack(">4h", 0, 0, 16, 16))
    probe.call("GetPort", [probe.ptr(ports)])
    probe.call("NewGWorld", [probe.ptr(ports + 4), 32, bounds, 0, 0, 0])
    p.expect(0, "create a color port for highlight propagation")
    probe.call("SetPort", [world])
    probe.call("HiliteColor", [original])
    probe.call("SetPort", [probe.slot(ports)])
    probe.call("SetHighlightColor", [color, 0])
    probe.call("LMGetHiliteRGB", [playlist])
    for offset, component in ((0, 0x2222), (2, 0x7777), (4, 0xAAAA)):
        p.emit(0xA07E0000 | (playlist[1] + offset))
        p.expect(component, "highlight color updates the live QuickDraw global")
    probe.call("GetPortHiliteColor", [world, playlist])
    for offset, component in ((0, 0x2222), (2, 0x7777), (4, 0xAAAA)):
        p.emit(0xA07E0000 | (playlist[1] + offset))
        p.expect(component, "highlight color propagates to other ports using the previous color")
    probe.call("SetHighlightColor", [original, 0])
    probe.call("DisposeGWorld", [world])

    for file_type in (0x74686D65, 0x61707072, 0x7A7A7A7A):
        probe.call("IsValidAppearanceFileType", [file_type], side=1)
        p.emit(0x907E0000 | (base + 20))
        probe.call("IsValidAppearanceFileType", [file_type])
        p.emit(0x809E0000 | (base + 20))
        p.equal("native Appearance uses the engine's real file-type registrations")

    packed = bytes.fromhex("000000000002") + b"\x03One" + bytes.fromhex("0001112233440002")
    packed += b"\x03Two" + bytes.fromhex("0003556677880004")
    table = probe.raw(packed)
    variant = probe.raw(bytes(264))
    handle = probe.slot(base + 16)
    probe.call("PtrToHand", [table, probe.ptr(base + 16), len(packed)])
    p.expect(0, "allocate a movable Appearance variant table")
    probe.call("CountThemeVariants", [handle, output])
    p.expect(0, "count packed Appearance variants")
    p.emit(0xA07E0000 | (base + 12))
    p.expect(2, "publish the variant count as a big-endian short")
    probe.call("GetIndThemeVariant", [handle, 2, variant])
    p.expect(0, "read the second packed Appearance variant")
    p.emit(0x807E0000 | variant[1])
    p.expect(0x0354776F, "variant indexes start at one")
    p.emit(0x807E0000 | (variant[1] + 256))
    p.expect(0x00035566, "copy the packed variant fields to the public record")
    p.emit(0x807E0000 | (variant[1] + 260))
    p.expect(0x77880004, "preserve all eight bytes of variant metadata")
    probe.call("GetThemeVariantByName", [handle, probe.raw(b"\x03one"), variant])
    p.expect(0, "find a variant name using the saved case-insensitive comparison")
    p.emit(0x807E0000 | variant[1])
    p.expect(0x034F6E65, "name lookup returns the display spelling")
    probe.call("GetIndThemeVariant", [handle, 0, variant])
    p.expect(-30572, "reject a zero Appearance variant index")
    probe.call("DisposeHandle", [handle])

    _, fixture = probe.raw(bytes(128))
    ptr, slot = probe.ptr, probe.slot
    theme_name = probe.raw(b"\x13ROMAW Variant Theme")
    spec = ptr(fixture + 32)
    probe.call("FindFolder", [-32768, 0x74686D65, 1, ptr(fixture), ptr(fixture + 4)])
    p.expect(0, "find the Appearance Themes directory")
    p.emit(0xA07E0000 | fixture, 0x907E0000 | (fixture + 8))
    probe.call("FSMakeFSSpec", [slot(fixture + 8), slot(fixture + 4), theme_name, spec])
    p.expect(-43, "build the specification for a new test theme")
    probe.call("CurResFile")
    p.emit(0x907E0000 | (fixture + 12))
    probe.call("FSpCreateResFile", [spec, 0x61707072, 0x74686D65, 0])
    probe.call("ResError")
    p.expect(0, "create the theme resource fork")
    probe.call("FSpOpenResFile", [spec, 3])
    p.nonzero("open the test theme resource fork")
    p.emit(0x907E0000 | (fixture + 16))
    probe.call("PtrToHand", [table, ptr(fixture + 20), len(packed)])
    p.expect(0, "create a theme variant resource handle")
    probe.call("AddResource", [slot(fixture + 20), 0x74766172, 128, probe.raw(b"\0")])
    probe.call("ResError")
    p.expect(0, "store the packed theme variants in the resource fork")
    probe.call("CloseResFile", [slot(fixture + 16)])
    probe.call("UseResFile", [slot(fixture + 12)])
    probe.call("GetThemeVariantList", [theme_name, ptr(fixture + 20)])
    p.expect(0, "detach the theme variant table before closing its resource fork")
    probe.call("CountThemeVariants", [slot(fixture + 20), output])
    p.expect(0, "read the detached theme variant table")
    p.emit(0xA07E0000 | (base + 12))
    p.expect(2, "the theme variant resource preserved both records")
    probe.call("DisposeHandle", [slot(fixture + 20)])

    _, routine = probe.raw(bytes(24))
    p.routines.append((routine, [0x90650000, 0x80040000, 0x90050004, 0x38600000, 0x4E800020]))
    probe.call("NewRoutineDescriptor", [ptr(routine), 0xF90, 1])
    p.nonzero("allocate a PPC theme iterator callback descriptor")
    p.emit(0x907E0000 | (fixture + 24))
    probe.call("IterateThemeVariants", [theme_name, slot(fixture + 24), ptr(fixture + 104)])
    p.expect(0, "iterate the resource fork variants with an early stop")
    p.emit(0x807E0000 | (fixture + 104))
    p.expect(1, "the callback stops iteration on the first index")
    p.emit(0x807E0000 | (fixture + 108))
    p.expect(0x034F6E65, "the callback receives the packed variant name and its refcon")
    probe.call("DisposeRoutineDescriptor", [slot(fixture + 24)])
    probe.call("FSpDelete", [spec])
    p.expect(0, "delete the isolated test theme")
    probe.call("UseResFile", [slot(fixture + 12)])


def nqd_native_operations(probe):
    p = probe.p
    _, base = probe.raw(bytes(16))
    region = probe.slot(base)
    copy = probe.slot(base + 4)

    def call(name, *arguments):
        probe.arguments(arguments, 0)
        p.call(name, library="NQD")

    for address in (base, base + 4):
        call("NQDNewRgn")
        p.nonzero("the original QuickDraw trap allocates a native NQD region")
        p.emit(0x907E0000 | address)
    call("NQDSetRectRgn", region, -10, 5, 11, 20)
    call("NQDOffsetRgn", region, 2, -3)
    call("NQDPtInRgn", (2 << 16) | 0xFFF8, region)
    p.expect(1, "NQD preserves signed coordinates and the packed Point ABI")
    call("NQDPtInRgn", (2 << 16) | 13, region)
    p.expect(0, "NQD membership excludes the right boundary")
    call("NQDCopyRgn", region, copy)
    call("NQDEqualRgn", region, copy)
    p.expect(1, "NQD region copies retain their geometry")
    call("NQDXorRgn", region, copy, copy)
    call("NQDEmptyRgn", copy)
    p.expect(1, "NQD region XOR permits an aliased destination")
    call("NQDDisposeRgn", copy)
    call("NQDDisposeRgn", region)

    pattern = desktop_pattern_fixture()
    encoded = probe.raw(pattern)
    guest_pattern = probe.ptr(base + 8)
    call("QDUnflattenPixPat", encoded, 40, guest_pattern)
    p.expect(-50, "reject a truncated flattened PixMap")
    p.emit(0x807E0000 | (base + 8))
    p.expect(0, "unflattening failures leave the output handle unchanged")
    call("QDUnflattenPixPat", encoded, len(pattern), guest_pattern)
    p.expect(0, "unflatten an indexed pixel pattern")
    p.emit(0x813E0000 | (base + 8), 0x81290000, 0x81290006, 0x81290000, 0x80690000)
    p.expect(0x11223344, "the unflattened pattern preserves its pixel bytes")
    probe.call("DisposePixPat", [probe.slot(base + 8)])

    rectangle = probe.raw(bytes(8))
    _, outputs = probe.raw(bytes(24))
    call("NQDSetRect", rectangle, 11, 13, 39, 29)
    call("NQDPtInRect", (13 << 16) | 11, rectangle)
    p.expect(1, "NQD rectangle geometry includes the top-left point")
    call("NQDOffsetRect", rectangle, -11, -13)
    call("NQDPtInRect", (16 << 16) | 28, rectangle)
    p.expect(0, "NQD rectangle geometry excludes the bottom-right point")
    call("NQDSetRect", 0, 0, 0, 0, 0)
    call("NQDQDError")
    p.expect(-50, "NQD geometry publishes paramErr without touching low memory")
    call("NQDOffscreenVersion")
    p.expect(0x150, "the NQD offscreen version comes from the saved fragment")
    call("NQDQDError")
    p.expect(0, "querying the NQD offscreen version clears QDErr")

    device, pixels = probe.slot(outputs), probe.slot(outputs + 4)
    for name in ("NQDNewScreenBuffer", "NQDNewTempScreenBuffer"):
        call(name, rectangle, 1, probe.ptr(outputs), probe.ptr(outputs + 4))
        p.expect(0, name + " allocates the pixmap and its pixel storage")
        p.emit(0x807E0000 | outputs)
        p.nonzero(name + " returns the selected graphics device")
        call("NQDGetPixRowBytes", pixels)
        p.nonzero(name + " publishes its row stride")
        call("NQDGetPixelsState", pixels)
        p.expect(64, name + " honors purgeable pixel storage")
        call("NQDLockPixels", pixels)
        p.expect(1, name + " locks its pixel storage")
        call("NQDGetPixelsState", pixels)
        p.expect(192, name + " publishes both the purgeable and locked flags")
        call("NQDGetPixBaseAddr", pixels)
        p.nonzero(name + " exposes its locked pixel data")
        p.emit(0x3880004B, 0x98830000, 0x88630000)
        p.expect(0x4B, name + " exposes writable guest pixel data")
        call("NQDUnlockPixels", pixels)
        call("NQDGetPixelsState", pixels)
        p.expect(64, name + " leaves the purgeable flag set after unlock")
        call("NQDDisposeScreenBuffer", pixels)

    call("NQDGetGWorld", probe.ptr(outputs), probe.ptr(outputs + 4))
    call("NQDNewGWorld", probe.ptr(outputs + 8), 32, rectangle, 0, 0, 0)
    p.expect(0, "NQD creates a color GWorld for picture recording")
    call("NQDSetGWorld", probe.slot(outputs + 8), 0)
    call("CheckPictureRecording")
    p.expect(0, "the NQD port starts outside a picture recording")
    call("NQDOpenPicture", rectangle)
    p.nonzero("NQD starts recording a PICT in the current port")
    p.emit(0x907E0000 | outputs + 12)
    call("CheckPictureRecording")
    p.expect(1, "NQD updates drawing state and reports the active recording")
    p.emit(0x813E0000 | outputs + 12, 0x81290000, 0xA0890000, 0x909E0000 | outputs + 16)
    call("PutPicByte", 0xAB)
    call("PutPicOp", 0xA0)
    call("PutPicWord", 0x1234)
    call("PutPicLong", 0x89ABCDEF)
    call("PutPicData", probe.raw(bytes.fromhex("cafe")), 2)
    for offset, expected in enumerate(bytes.fromhex("ab 00 00 a0 12 34 89 ab cd ef ca fe")):
        p.emit(0x813E0000 | outputs + 12, 0x81290000, 0x809E0000 | outputs + 16, 0x7D292214, 0x88690000 | offset)
        p.expect(expected, f"NQD picture writers preserve byte order and opcode alignment at byte {offset}")
    call("NQDClosePicture")
    call("CheckPictureRecording")
    p.expect(0, "NQD reports no active picture after closing it")
    call("NQDKillPicture", probe.slot(outputs + 12))
    call("NQDSetGWorld", device, pixels)
    call("NQDDisposeGWorld", probe.slot(outputs + 8))


def make_probe(destination, original_only, math_only=False, atsui=False, text_only=False):
    if atsui:
        if original_only or math_only:
            raise ValueError("The ATSUI group requires native entries and the original oracle")
        make_atsui_probe(destination)
        return
    write_toolbox_references(destination, include_object_packing=not (original_only or math_only or text_only))
    nqd_imports = ()
    if not (original_only or math_only or text_only):
        nqd_source = (ROOT / "carbonlib/src/nqd_entries.h").read_text()
        nqd_imports = tuple(
            ("NQD", name, 2) for name in re.findall(r"^NQD_(?:TOOLBOX|OFFSCREEN|NATIVE)\((\w+),", nqd_source, re.M)
        )
        if len(nqd_imports) != 220:
            raise ValueError("Incomplete NQD Toolbox export table")
    source = (ROOT / "carbonlib/src/math_entries.h").read_text()
    entries = [
        (name, kind, operands.split(", "))
        for kind, name, operands in re.findall(r"^CARBON_MATH_(\w+)\((\w+), ([^\n)]+)\)$", source, re.M)
    ]
    if len(entries) != 132:
        raise ValueError("Incomplete Carbon MathLib table")
    math_rows = [{"name": name, "original_library": "MathOracle"} for name, _, _ in entries]
    math_rows.extend(
        {"name": name, "original_library": "MathOracle"}
        for name in (
            "num2dec",
            "dec2str",
            "str2dec",
            "dec2num",
            "dec2s",
            "dec2l",
            "nan",
            "nanf",
            "nanl",
            "num2decl",
            "dec2numl",
            "dec2f",
            "frexpl",
            "ldexpl",
        )
    )
    wide_source = (ROOT / "carbonlib/src/math_wide_entries.h").read_text()
    wide_entries = re.findall(r"^CARBON_MATH_WIDE\((\w+), (\w+)\)$", wide_source, re.M)
    if len(wide_entries) != 47:
        raise ValueError("Incomplete Carbon Math64Lib table")
    math_rows += [{"name": name, "original_library": "Math64Oracle"} for name, _ in wide_entries]
    file_entries, file_names = file_cases() if not (original_only or math_only) else ([], set())
    color_profiles = file_entries and write_color_profile_reference(destination)
    interface_rows = vector_cases("interface_vectors.h", 179) if file_entries else []
    if file_entries:
        interface_rows.extend(
            dict(name=name, vector=0x17A0, info=info, selector=selector, result="int16_t")
            for name, info, selector in (
                ("SetDialogDefaultItem", 0xBA8, 0x304),
                ("SetDialogCancelItem", 0xBA8, 0x305),
                ("SetDialogTracksCursor", 0x7A8, 0x306),
            )
        )
    manager_rows = vector_cases("manager_vectors.h", 176) if file_entries else []
    browser_control_rows = data_browser_control_cases() if file_entries else []
    native_names = file_names | {row["name"] for row in interface_rows + manager_rows}
    native_names.update(
        {"GetWindowFeatures", "IsWindowCollapsable", "IsWindowCollapsed", "CollapseWindow", "CollapseAllWindows"}
    )
    if file_entries and not original_only:
        native_names.update(windows_metadata_names(ROOT))
        native_names.update(("CopyThemeIdentifier", "GetThemeWindowRegion", "GetThemeWindowRegionHit"))
        native_names.update(("MoveDialogItem", "SizeDialogItem", "GetSystemFont", "GetControlBounds"))
        native_names.update(
            (
                "BlockMoveUncached",
                "GetAlertStage",
                "HandleToRgn",
                "ResetAlertStage",
                "ResolveAliasFileWithMountFlagsNoUI",
                "RgnToHandle",
                "SetDialogFont",
            )
        )
        native_names.add("AEGetDescData")
        native_names.update(
            (
                "DCMCreateFieldInfoRecord",
                "DCMGetFieldTagAndType",
                "DCMGetFieldMaxRecordSize",
                "DCMGetFieldAttributes",
                "DCMGetFieldDefaultData",
                "DCMGetFieldFindMethods",
            )
        )

        native_names.add("AEPrintDescToHandle")
        native_names.update(("AEGetArray", "AEPutArray"))
    native_names.update(name for name, *_ in browser_control_rows)
    if color_profiles:
        native_names.update(COLOR_PROFILE_APIS)
    if file_entries:
        native_names.update(fsref_cases())
        color_sync_source = (ROOT / "carbonlib/src/color_sync.h").read_text()
        native_names.update(
            re.findall(r"^static int carbon_(CM(?:Convert|GetColorSync)\w+)\(", color_sync_source, re.M)
        )
        data_browser = (ROOT / "carbonlib/src/data_browser.h").read_text()
        native_names.update(re.findall(r"^CARBON_BROWSER_FIELD\((\w+),", data_browser, re.M))
        native_names.update(re.findall(r"^BROWSER_MESSAGE\((\w+),", data_browser, re.M))
        native_names.update(re.findall(r"^static int carbon_([A-Z]\w+)\(", data_browser, re.M))
        native_names.update(
            (
                "CreateCustomControl",
                "GetControlClickActivation",
                "HandleControlSetCursor",
                "HandleControlContextualMenuClick",
            )
        )
        native_names.update(name for name, _ in BUILTIN_CONTROL_CASES)
        native_names.update(("CreateListBoxControl", "CreatePushButtonWithIconControl", "CreateTabsControl"))
        native_names.update(
            (
                "IsScreenBitSavingEnabled",
                "SetScreenBitSavingEnabled",
                "GetSavedScreenBitsRepresentativePixMap",
                "MoveSavedScreenBits",
                "DisposeSavedScreenBits",
            )
        )
        native_names.update(("SetControlColorProc", "SetUpControlBackground", "SetUpControlTextColor"))
        icon_services = (ROOT / "carbonlib/src/icon_services.h").read_text()
        # The Finder Services fixture exercises file-backed IconRefs and icon-file I/O.
        finder_file_icons = {
            "GetIconRefFromFile",
            "GetIconRefFromFolder",
            "UpdateIconRef",
            "ReadIconFile",
            "WriteIconFile",
            "RegisterIconRefFromIconFile",
            "RegisterIconRefFromResource",
            "OverrideIconRef",
            "OverrideIconRefFromResource",
            "RemoveIconRefOverride",
            "FlushIconRefsByVolume",
        }
        native_names.update(
            set(re.findall(r"^static int carbon_([A-Z]\w+)\(", icon_services, re.M)) - finder_file_icons
        )
        native_names.update(
            ("ClipRect", "EraseRect", "GetCPixel", "GetGWorld", "LockPixels", "RGBBackColor", "UnlockPixels")
        )
        native_names.update(("GetGWorldDevice", "GetPixBaseAddr", "GetPixRowBytes"))
        romaw_services = (ROOT / "carbonlib/src/romaw_services.h").read_text()
        native_names.update(re.findall(r"^CARBON_ROMAW_SERVICE\((\w+)\)$", romaw_services, re.M))
        native_names.update(("SetBevelButtonTransform", "SetImageWellTransform"))
        native_names.update(("Count1Types", "FindFolder", "Get1Resource", "HomeResFile"))
        native_names.update(("LLastClick", "TEGetPoint", "GetDialogTextEditHandle"))
        native_names.update(
            ("CurResFile", "FSpCreateResFile", "FSpOpenResFile", "FSpGetFInfo", "FSpSetFInfo", "ResError")
        )
        native_names.update(international_names(ROOT))
        utility_rows = vector_cases("classic_utility_vectors.h", 24)
        utility_registers = register_cases("classic_utility_vectors.h", 8)
        native_names.update(row["name"] for row in utility_rows + utility_registers)
        native_names.update(
            (
                "Delay",
                "GetSysPPtr",
                "ScreenRes",
                "SetTime",
                "LowercaseText",
                "UppercaseText",
                "StripDiacritics",
                "UppercaseStripDiacritics",
                "TEFromScrap",
                "TEToScrap",
            )
        )
        native_names.update(
            (
                "BlockMoveDataUncached",
                "BlockZero",
                "BlockZeroUncached",
                "Fix2Frac",
                "Fix2Long",
                "FixRound",
                "Frac2Fix",
                "Gestalt",
                "GetCaretTime",
                "GetDblTime",
                "GetGrayRgn",
                "GetGrowZone",
                "GetGWorldPixMap",
                "GetKeys",
                "ReadLocation",
                "WriteLocation",
                "ConvertLocalTimeToUTC",
                "ConvertUTCToLocalTime",
                "ConvertLocalToUTCDateTime",
                "ConvertUTCToLocalDateTime",
                "GetLocalDateTime",
                "GetUTCDateTime",
                "SetLocalDateTime",
                "SetUTCDateTime",
                "InitUtil",
                "HoldMemory",
                "Long2Fix",
                "MemError",
                "MPBlockClear",
                "MPBlockCopy",
                "MPDataToCode",
                "MPProcessors",
                "MPProcessorsScheduled",
                "SetEventMask",
                "TickCount",
                "UnholdMemory",
            )
        )
        native_names.update(re.findall(r"^CARBON_FIXMATH_WIDE\((\w+),", wide_source, re.M))
        native_names.update(CLASSIC_STATE_APIS)
        network_location = (ROOT / "carbonlib/src/network_location.h").read_text()
        native_names.update(re.findall(r"^static int carbon_(NSL\w+)\(", network_location, re.M))
        native_names.update(("LSGetApplicationForInfo", "_CFGetFSSpecFromURL"))
        text_edit_exports = (ROOT / "carbonlib/src/exports.c").read_text()
        native_names.update(re.findall(r"^CARBON_TEXT_EDIT\((TE\w+),", text_edit_exports, re.M))
        native_names.update(PORT_APIS)
        native_names.update(FONT_FAMILY_APIS)
        native_names.update(FONT_INSTANCE_APIS)
        native_names.update(FONT_CATALOG_APIS)
        native_names.update(("SetThemeCursor", "SetAnimatedThemeCursor", "SetThemeCursorWithOptions"))
        native_names.update(
            (
                "UseThemeFont",
                "GetThemeFont",
                "DrawThemeTextBox",
                "TruncateThemeText",
                "GetThemeTextDimensions",
                "GetThemeTextShadowOutset",
                "GetPen",
                "CFStringCreateMutableCopy",
                "GetSmallSystemFont",
                "GetViewsFont",
                "GetThemeFontSmoothing",
                "SetThemeFontSmoothing",
                "GetPortTextFont",
                "GetPortTextSize",
                "GetPortTextFace",
                "FontMetrics",
                "QDTextBounds",
            )
        )
        native_names.update(
            (
                "GetPixelsState",
                "GetPortBounds",
                "GetPortPenSize",
                "GetPortPenMode",
                "GetPortFracHPenLocation",
                "GetPortGrafProcs",
                "GetPortOpColor",
                "GetPortClipRegion",
                "GetPortVisibleRegion",
                "GetRegionBounds",
                "GetQDGlobalsThePort",
                "GetQDGlobalsArrow",
                "SetQDError",
            )
        )
        url_access = (ROOT / "carbonlib/src/url_access.h").read_text()
        native_names.update(re.findall(r"^static int carbon_(URL\w+)\(", url_access, re.M))
        native_names.update(
            (
                "CFStringCreateWithCString",
                "CFStringGetCString",
                "CFURLCreateWithFileSystemPath",
                "CFURLGetString",
                "DisposePtr",
            )
        )
        sound_input = (ROOT / "carbonlib/src/exports.c").read_text()
        native_names.update(re.findall(r"^CARBON_SOUND_INPUT\((\w+),", sound_input, re.M))
        sound_converter = (ROOT / "carbonlib/src/sound_converter.h").read_text()
        native_names.update(re.findall(r"^static int carbon_(SoundConverter\w+)\(", sound_converter, re.M))
        native_names.update(("AEObjectInit", "AESetObjectCallbacks", "GetSubmenuParentChoosable", "MatchAliasNoUI"))
        native_names.update(DEBUG_REGISTRY_APIS)
        native_names.update(HEAP_DIAGNOSTIC_APIS)
        native_names.update(
            (
                "relstring",
                "upperstring",
                "GetApplicationScript",
                "UnsignedFixedMulDiv",
                "GetIconDevice",
                "SysError",
                "stdtext",
                "DebugStr",
            )
        )
        power_entries = (ROOT / "carbonlib/src/power_manager.h").read_text()
        native_names.update(re.findall(r"^CARBON_POWER\((\w+),", power_entries, re.M))
        native_names.update(re.findall(r"^static int carbon_(\w+)\(", power_entries, re.M))
        native_names.update(
            (
                "NavGetDefaultDialogOptions",
                "NavLoad",
                "NavUnload",
                "NavLibraryVersion",
                "NavServicesCanRun",
                "LMGetSysMap",
                "LMSetSysMap",
                "NavGetDefaultDialogCreationOptions",
                "NavGetFile",
                "NavChooseFile",
                "NavChooseFolder",
                "NavChooseObject",
                "NavChooseVolume",
                "NavNewFolder",
                "NavCreatePreview",
                "NavHasPreview",
                "NavCreatePreviewReference",
                "NavDisposePreviewReference",
                "NavSetupPreview",
                "NavDrawPreview",
                "NavHandlePreviewEvent",
                "NavGetPreviewArea",
                "NavSetPreviewArea",
                "NavPutFile",
                "NavAskSaveChanges",
                "NavAskDiscardChanges",
                "NavCustomAskSaveChanges",
                "NavCreateAskSaveChangesDialog",
                "NavCreateAskDiscardChangesDialog",
                "NavCreateGetFileDialog",
                "NavCreateChooseFileDialog",
                "NavCreateChooseFolderDialog",
                "NavCreateChooseObjectDialog",
                "NavCreateChooseVolumeDialog",
                "NavCreateNewFolderDialog",
                "NavCreatePutFileDialog",
                "NavDialogDispose",
                "NavDialogRun",
                "NavDialogGetWindow",
                "NavDialogGetUserAction",
                "NavDialogGetReply",
                "NavDialogGetSaveFileName",
                "NavDialogSetSaveFileName",
                "NavCustomControl",
                "CFStringGetLength",
                "CFStringGetCharacterAtIndex",
                "AECountItems",
                "AEGetNthPtr",
                "NavDisposeReply",
                "CFRetain",
                "CFGetRetainCount",
            )
        )
        native_names.update(
            (
                "GetSharedLibrary",
                "GetDiskFragment",
                "GetMemFragment",
                "FindSymbol",
                "CloseConnection",
                "CountSymbols",
                "GetIndSymbol",
            )
        )
        native_names.update(
            (
                "CaptureComponent",
                "UncaptureComponent",
                "CallComponentFunction",
                "CallComponentFunctionWithStorage",
                "CallComponentFunctionWithStorageProcInfo",
                "GetComponentPublicResource",
                "GetComponentPublicIndString",
                "GetComponentIconSuite",
            )
        )
        component_entries = (ROOT / "carbonlib/src/component_entries.h").read_text()
        native_names.update(re.findall(r"^CARBON_COMPONENT(?:_MESSAGE)?\((\w+),", component_entries, re.M))
        internet_config = re.findall(r"^CARBON_IC_MESSAGE\((IC\w+), (0x[0-9a-f]+), (\d+)\)$", component_entries, re.M)
        native_names.update(name for name, _, _ in internet_config)
        component_messages = (ROOT / "carbonlib/src/component_messages.h").read_text()
        typed_messages = re.findall(
            r'^CARBON_TYPED_COMPONENT\((\w+), (0x[0-9a-f]+), (\d+), "\w+"\)$', component_messages, re.M
        )
        native_names.update(name for name, _, _ in typed_messages)
        native_names.update(("ICStart", "ICStop"))
        native_names.add("DelegateComponentCall")
        geometry = (ROOT / "carbonlib/src/geometry.h").read_text()
        native_names.update(re.findall(r"^static int carbon_([A-Z]\w*)\(", geometry, re.M))
        native_names.update(
            (
                "deltapoint",
                "IsCmdChar",
                "GetFormatOrder",
                "HiliteText",
                "SetDialogCancelItem",
                "SetDialogTracksCursor",
                "GetDialogCancelItem",
                "TopMem",
                "GZSaveHnd",
                "GetDefFontSize",
                "SetA5",
                "SetCurrentA5",
                "LMGetSysFontSize",
                "LMSetSysFontSize",
            )
        )
        native_names.update(re.findall(r"^CARBON_COORDINATE_WRAPPER\((\w+),", geometry, re.M))
        native_names.update(TYPE_SELECTION_APIS)
        native_names.update(
            (
                "QDGetPatternOrigin",
                "GetRegionBounds",
                "GetQDGlobalsThePort",
                "GetQDGlobalsRandomSeed",
                "SetQDGlobalsRandomSeed",
            )
        )
        application_rows = vector_cases("application_vectors.h", 55)
        native_names.update(row["name"] for row in application_rows)
        native_names.update(("NewGestalt", "ReplaceGestalt"))
        collection_entries = (ROOT / "carbonlib/src/collection_entries.h").read_text()
        collection_names = set(re.findall(r"^CARBON_COLLECTION_ENTRY\((\w+),", collection_entries, re.M))
        if len(collection_names) != 42:
            raise ValueError("Incomplete Classic Collection Manager table")
        native_names.update(collection_names)
        menu_entries = (ROOT / "carbonlib/src/menu_access.h").read_text()
        native_names.update(re.findall(r"^CARBON_MENU_(?:ENABLE|PROPERTY)\((\w+),", menu_entries, re.M))
        menu_handlers = re.findall(r"^static int carbon_([A-Z]\w*)\(", menu_entries, re.M)
        native_names.update("_ScrollMenuImage" if name == "ScrollMenuImage" else name for name in menu_handlers)
        native_names.update(("CalcMenuSize", "GetMenuHeight", "GetMenuWidth", "SetMenuHeight", "SetMenuWidth"))
        native_names.update(("GetMenuItemCommandID", "HideMenuBar", "ShowMenuBar", "IsMenuBarVisible"))
        drag_entries = (ROOT / "carbonlib/src/drag_handlers.h").read_text()
        native_names.update(re.findall(r"^CARBON_DRAG_HANDLER\((\w+),", drag_entries, re.M))
        drag_data_entries = (ROOT / "carbonlib/src/drag_data.h").read_text()
        native_names.update(re.findall(r"^CARBON_DRAG_DATA\((\w+),", drag_data_entries, re.M))
        native_names.update(
            ("WaitMouseMoved", "ZoomRects", "ZoomRegion", "GetDragHiliteColor", "GetPortHiliteColor", "TEGetHiliteRgn")
        )
        native_names.update(
            (
                "CFEqual",
                "CFRelease",
                "CFStringCreateWithPascalString",
                "GetMenuItemTextEncoding",
                "SetMenuItemTextEncoding",
            )
        )
        control_entries = (ROOT / "carbonlib/src/control_access.h").read_text()
        native_names.update(re.findall(r"^CARBON_(?:CONTROL|RANGE)\((\w+),", control_entries, re.M))
        native_names.update(("GetBestControlRect", "GetControlRegion", "AdvanceKeyboardFocus", "ReverseKeyboardFocus"))
        native_names.update(("GetWindowBounds", "GetWindowRegion"))
        native_names.update(
            (
                "GetIndString",
                "GetMenuID",
                "GetMenuTitle",
                "GetWindowKind",
                "GetWindowPortBounds",
                "GetWindowTitleLength",
                "HLockHi",
                "IsValidWindowPtr",
                "IsWindowVisible",
                "SetMenuTitle",
                "SetWindowKind",
            )
        )
    if math_only:
        probe = ToolboxProbe(math_rows, original_only)
    else:
        rows = vector_cases()
        if not original_only:
            native_names.update(MENU_ITEM_VECTORS)
        registers = register_cases()
        constructors, disposers, invocations = upp_cases()
        borrowed = direct_upp_cases()
        mixed = mixed_upp_cases()
        upp_header = (ROOT / "carbonlib/src/upp_entries.h").read_text()
        textedit = {
            name: (int(info, 16), int(count))
            for name, info, count in re.findall(
                r"^CARBON_TEXTEDIT_UPP\((\w+), (0x[0-9a-f]+), (\d+)\)$", upp_header, re.M
            )
        }
        raw_names = {prefix + name[6:] for name in borrowed for prefix in ("New", "Dispose", "Invoke")}
        raw_names.add("NewComponentFunctionUPP")
        upp_rows = [
            {"name": name}
            for name in sorted(
                constructors.keys() | disposers | invocations.keys() | raw_names | mixed.keys() | textedit.keys()
            )
        ]
        collection_oracles = (
            [
                {"name": name, "original_library": "Collections"}
                for name in ("FlattenCollectionToHdl", "UnflattenCollectionFromHdl")
            ]
            if file_entries
            else []
        )
        fsref_oracles = [{"name": name} for name in FSREF_REFERENCE_CALLS] if file_entries else []
        locale_oracles = [{"name": name} for name in LOCALE_REFERENCE_CALLS] if file_entries else []
        clock_oracles = (
            [{"name": name, "original_library": "MenusLib"} for name in MENU_CLOCK_REFERENCE_CALLS]
            if file_entries
            else []
        )
        fixed_oracles = [{"name": name} for name in FIXED_DECIMAL_APIS] if file_entries else []
        string_oracles = [{"name": name} for name in ("EqualString", "RelString")] if file_entries else []
        navigation_oracles = (
            [{"name": name, "original_library": "NavigationLib.CL"} for name in ("NavLoad", "NavUnload")]
            if file_entries
            else []
        )
        menu_commands = re.findall(r"^CARBON_MENU_(?:COMMAND|TEXT)\((\w+),", menu_entries, re.M) if file_entries else []
        probe = ToolboxProbe(
            [row for row in rows if row["name"] not in MENU_ITEM_VECTORS]
            + registers
            + upp_rows
            + math_rows
            + collection_oracles
            + fsref_oracles
            + locale_oracles
            + clock_oracles
            + fixed_oracles
            + string_oracles
            + navigation_oracles
            + (
                [
                    {"name": name}
                    for name in event_names(ROOT)
                    + window_property_names(ROOT)
                    + [
                        "CheckMenuItem",
                        "EqualRgn",
                        "GetWindowBounds",
                        "LMGetHiliteRGB",
                        "ValidWindowRgn",
                        "Gestalt",
                        "GetParamText",
                        "StdFilterProc",
                        "InsertDialogItem",
                        "RemoveDialogItems",
                        "SetControlTitleWithCFString",
                        "CopyControlTitleAsCFString",
                        "GetTextAndEncodingFromCFString",
                        "UCCompareCollationKeys",
                        "QDSaveRegionBits",
                        "QDRestoreRegionBits",
                        "QDDisposeRegionBits",
                        "CreateNewPort",
                        "DisposePort",
                        "IsValidPort",
                        "QDRegionToRects",
                        "GetPortPrintingReference",
                        "SetPortPrintingReference",
                        "NewGWorldFromPtr",
                        "TXNInitTextension",
                        "TXNNewObject",
                        "TXNTerminateTextension",
                        "TXNVersionInformation",
                        "TXNDataSize",
                        "TXNGetSelection",
                        "TXNIsSelectionEmpty",
                        "TXNGetViewRect",
                        "TXNGetLineCount",
                        "TXNGetLineMetrics",
                        "TXNGetReferencedData",
                        "TXNCountLinesToPrint",
                        "TXNBeginPrinting",
                        "TXNPrintPage",
                        "TXNEndPrinting",
                        "TXNIsObjectAttachedToWindow",
                        "TXNIsObjectAttachedToSpecificWindow",
                        "TXNAttachObjectToWindow",
                        "TXNGetChangeCount",
                        "TXNGetActionChangeCount",
                        "TXNClearActionChangeCount",
                        "TXNGetDataFilter",
                        "TXNSetDataFilter",
                        "TXNSetData",
                        "TXNGetSleepTicks",
                        "TXNSetSelection",
                        "TXNCanUndo",
                        "TXNCanRedo",
                        "TXNIsTyping",
                        "TXNGetFontDefaults",
                        "TXNSetFontDefaults",
                        "TXNGetFontMenuHandle",
                        "TXNNewFontMenuObject",
                        "TXNDisposeFontMenuObject",
                        "TXNPrepareFontMenu",
                        "TXNDoFontMenuSelection",
                        "TXNSelectAll",
                        "TXNEmptyUndoStack",
                        "TXNForceUpdate",
                        "TXNSetBackground",
                        "TXNDraw",
                        "TXNDrawInRectangle",
                        "TXNClick",
                        "TXNDeleteObject",
                        "TXNDragTracker",
                        "TXNDragReceiver",
                        "TXNKeyDown",
                        "TXNPsuedoUnicodeKeyDown",
                        "TXNIdle",
                        "TXNRevert",
                        "TXNSave",
                        "TXNGetTXNObjectControls",
                        "TXNSetTXNObjectControls",
                        "TXNDrawUnicodeTextBox",
                        "TXNDrawCFStringTextBox",
                        "TXNPageSetup",
                        "TXNPrint",
                        "TXNFocus",
                        "TXNEchoMode",
                        "TXNUndo",
                        "TXNRedo",
                        "TXNShowSelection",
                        "TXNSetViewRect",
                        "TXNOffsetToPoint",
                        "TXNPointToOffset",
                        "TXNCountRunsInRange",
                        "TXNGetIndexedRunInfoFromRange",
                        "TXNGetData",
                        "TXNGetDataEncoded",
                        "TXNGetContinuousTypeAttributes",
                        "TXNSetTypeAttributes",
                        "TXNSetDataFromFile",
                        "TXNActivate",
                        "TXNCountElements",
                        "TXNFind",
                        "TXNIsScrapPastable",
                        "TXNConvertToPublicScrap",
                        "TXNConvertFromPublicScrap",
                        "TXNFindElementByIndex",
                        "TXNUpdate",
                        "TXNResizeFrame",
                        "TXNZoomWindow",
                        "TXNGrowWindow",
                        "TXNSetFrameBounds",
                        "TXNAdjustCursor",
                        "TXNClear",
                        "TXNCut",
                        "TXNCopy",
                        "TXNPaste",
                        *AE_DESCRIPTOR_ORACLES,
                        *AE_STREAM_NAMES,
                        *AE_BUILD_NAMES,
                        "CreateListBoxControl",
                        "GetThemeMetric",
                        "DrawThemePrimaryGroup",
                        "DrawThemeSecondaryGroup",
                        "DrawThemeSeparator",
                        "DrawThemeEditTextFrame",
                        "DrawThemeGenericWell",
                        "DrawThemeMenuItem",
                        "SetThemePen",
                        "SetThemeBackground",
                    ]
                ]
                + [
                    {"name": name}
                    for name in (
                        *menu_commands,
                        *MENU_STRUCTURE_APIS,
                        *re.findall(r"^HELP_ENTRY\((\w+),", (ROOT / "carbonlib/src/help_access.h").read_text(), re.M),
                    )
                ]
                + [
                    {"name": name, "original_library": "AppearanceLib"}
                    for name in (
                        *APPEARANCE_STATE_APIS,
                        "GetThemeFont",
                        "GetThemeButtonBackgroundBounds",
                        "GetThemeButtonContentBounds",
                        "GetThemeButtonRegion",
                        "GetThemeTabRegion",
                        "GetThemeStandaloneGrowBoxBounds",
                        "GetThemeTrackBounds",
                        "GetThemeTrackDragRect",
                        "GetThemeTrackThumbPositionFromOffset",
                        "GetThemeTrackThumbPositionFromRegion",
                        "GetThemeTrackLiveValue",
                        "GetThemeTrackThumbRgn",
                        "HitTestThemeTrack",
                        "GetThemeScrollBarTrackRect",
                        "HitTestThemeScrollBarArrows",
                    )
                ]
                + (
                    [{"name": name, "original_library": "ObjectPackingOracle"} for name in AE_OBJECT_PACKING]
                    if not text_only
                    else []
                )
                + (
                    [
                        {"name": name, "original_library": "WindowsLib"}
                        for name in (
                            "BeginWindowProxyDrag",
                            "RepositionWindow",
                            "ReshapeWindows",
                            "RecalculateAllWindowTitleWidths",
                            "FileContainsPossibleDesktopPicture",
                            "GetPictureFromFile",
                            "DrawMonitorDesktopFromListWillTakeTime",
                            "SetMonitorDesktopToFile",
                            "CopyMonitorDesktopFile",
                            "SetDesktopPictureForDevice",
                            "GetDesktopPictureForDevice",
                            "GetCurrentMonitorDesktopList",
                            "SetCurrentMonitorDesktopList",
                            "DisposeMonitorDesktopList",
                            "RefreshMonitorDesktopList",
                            "GetInternalMonitorDesktopList",
                            "InternalMonitorDesktopListChanged",
                            "CountMonitorDesktopRecordsInList",
                            "GetMonitorDesktopRecordFromList",
                            "SetMonitorDesktopRecordFromList",
                            "MonitorDesktopHasPicture",
                            "GetMonitorDesktopAlignment",
                            "SetMonitorDesktopAlignment",
                            "DisposeSavedScreenBits",
                            "SaveScreenBits",
                            "RestoreScreenBits",
                            "DrawDesktop",
                            "DoubleClickWindowTitle",
                            "EndWindowProxyDrag",
                            "EraseWindowRegion",
                            "GetNextWindowActivateEvent",
                            "GetDesktopPattern",
                            "GetDesktopPixPat",
                            "GetWindowContentPattern",
                            "GetWindowProcess",
                            "GetWindowProxyAlias",
                            "GetWindowProxyFSSpec",
                            "GetWindowProxyIcon",
                            "GlobalFindWindow",
                            "HiliteWindowFrameForDrag",
                            "InstallWindowContentPaintProc",
                            "IsWindowPathSelectClick",
                            "PostDelayedWindowReshape",
                            "PostWindowActivateEvent",
                            "RemoveWindowProxy",
                            "RebuildDesktopRegion",
                            "SetDesktopPattern",
                            "SetWindowContentPattern",
                            "SetWindowFeatures",
                            "SetWindowProxyAlias",
                            "SetWindowProxyCreatorAndType",
                            "SetWindowProxyFSSpec",
                            "SetWindowProxyIcon",
                            "TrackWindowProxyDrag",
                            "TrackWindowProxyFromExistingDrag",
                            "TransitionWindow",
                            "WMGetWindowFeatures",
                            "WMGetWindowRegion",
                            "WMCollapseWindow",
                            "WMCollapseAllWindows",
                            "WMIsWindowCollapsable",
                            "WMIsWindowCollapsed",
                            "WindowManagerSetThemeWindowBackground",
                            "IsWindowCollapsable",
                            "IsWindowCollapsed",
                            "CollapseWindow",
                            "CollapseAllWindows",
                            "GetWindowFeatures",
                        )
                    ]
                    if not text_only
                    else []
                )
                + text_scrap_rows(ROOT)
                if file_entries
                else []
            ),
            original_only,
            native_names=native_names,
            support_names=TEXTENSION_SUPPORT if file_entries else (),
            sections=("Script Manager type selection", "QuickDraw ports and Textension objects") if text_only else (),
            companion_imports=nqd_imports,
        )
    if not math_only:
        probe.p.imports.append(("NQD", "RSect", 2))
    if file_entries and not text_only:
        probe.p.imports.extend(
            ("ToolboxUtils", name, 2)
            for name in (
                "CountStackListElements",
                "DisposeStackList",
                "EnqueueStackListElement",
                "NewStackListSys",
                "PopStackListElement",
            )
        )
    probe.p.reserve_parameter_words(9 if math_only else 15 if file_entries else 12)
    if file_entries:
        # Textension also reads the fixture when the URL Access checks are filtered out.
        (destination / "URL buffer fixture").write_bytes(b"HEAD" + bytes(16380) + b"TAIL")
        picture = struct.pack(">H4h", 34, 0, 0, 16, 16) + bytes.fromhex(
            "001102ff001affff000000000031000000000010001000ff"
        )
        (destination / "Desktop PICT fixture").write_bytes(bytes(512) + picture)
        nsl_errors = struct.pack(">Hi", 2, -4176) + b"\x01A\x01B"
        nsl_errors += struct.pack(">i", -4172) + b"\x07Bad URL\x09Try again"
        write_fragment(
            destination, "NSL Error Table", b"", 0, extra_resources=[(b"NSLE", 128, nsl_errors)], include_cfrg=False
        )
        probe.section("URL Access buffered local files", lambda: url_access_operations(probe, destination))
    probe.section("MathLib scalar values", lambda: math_scalars(probe, entries))
    probe.section("MathLib IEEE classification", lambda: math_classification(probe, entries))
    probe.section("MathLib environment and output parameters", lambda: math_environment(probe, entries))
    probe.section("MathLib decimal conversion and NaN payloads", lambda: math_decimals(probe))
    probe.section("MathLib double-double and extended80", lambda: math_extended(probe))
    probe.section("MathLib long-double functions", lambda: math_long(probe, entries))
    probe.section("Math64Lib integer arithmetic and double-double conversions", lambda: math_wide(probe, wide_entries))
    if file_entries:
        probe.section("Classic FixMath wide arithmetic", lambda: fixmath_wide_operations(probe))
        probe.section("Carbon fixed-point arithmetic and decimal strings", lambda: fixed_decimal_operations(probe))
    if not math_only:
        toolbox_sections(probe, rows, registers, constructors, disposers, invocations, borrowed, mixed)
        probe.section("TextEdit register callbacks", lambda: textedit_upp_invocations(probe, textedit))
    if nqd_imports:
        probe.section("Appearance clients and preferences", lambda: appearance_state_operations(probe))
        if not original_only:
            probe.section("DebugLib component and option registry", lambda: debug_registry_operations(probe))
            probe.section("Memory Manager heap diagnostics", lambda: heap_diagnostic_operations(probe))
        probe.section("Appearance geometry", lambda: appearance_geometry_operations(probe))
        probe.section(
            "Locale references, localized names and Unicode collation",
            lambda: locale_operations(probe),
        )
        probe.section("NQD original QuickDraw traps", lambda: nqd_native_operations(probe))
    if file_entries:
        probe.section("File Manager parameter-block trap dispatch", lambda: file_parameter_blocks(probe, file_entries))
        probe.section("File Manager operations and async completion", lambda: file_operations(probe))
        probe.section("File Manager FSSpec operations", lambda: file_spec_operations(probe))
        probe.section("File Manager FSRef catalog and forks", lambda: fsref_operations(probe))
        if not original_only:
            probe.section("Launch Services Finder association lookup", lambda: launch_services_operations(probe))
    if interface_rows:
        probe.section("InterfaceLib Toolbox re-exports", lambda: patched_vectors(probe, interface_rows, sides=(0,)))
        probe.section("Installed Apple Event descriptor and callback operations", lambda: apple_event_operations(probe))
        probe.section("Apple Event stream construction and ownership", lambda: apple_event_stream_operations(probe))
        probe.section("Apple Event object specifier packing", lambda: apple_event_object_packing(probe))
        probe.section("Apple Event format builders and CFM varargs", lambda: apple_event_builder_operations(probe))
        probe.section("Apple Event descriptor printing and rebuild", lambda: apple_event_printer_operations(probe))
        probe.section("Apple Event array representations and ownership", lambda: apple_event_array_operations(probe))
        probe.section("Dictionary Manager field records", lambda: dictionary_field_operations(probe))
    if manager_rows:
        probe.section("Toolbox manager dispatchers", lambda: patched_vectors(probe, manager_rows, sides=(0,)))
        probe.section("Installed Toolbox manager operations", lambda: manager_operations(probe))
        if not original_only:
            probe.section("Picture and pixmap analysis", lambda: picture_utility_operations(probe))
            probe.section("Network Services Location records", lambda: network_location_operations(probe))
        probe.section("Classic Printing Manager", lambda: printing_operations(probe))
    if file_entries:
        probe.section("Classic utility register traps", lambda: register_vectors(probe, utility_registers, sides=(0,)))
        probe.section("Classic utility selector traps", lambda: patched_vectors(probe, utility_rows, sides=(0,)))
        probe.section("Installed classic utilities", lambda: classic_utility_operations(probe))
        probe.section("Shared ROMAW Toolbox and Appearance services", lambda: romaw_service_operations(probe))
        probe.section("Data Browser callback and item records", lambda: data_browser_operations(probe))
        probe.section(
            "Data Browser CDEF property dispatch", lambda: data_browser_control_operations(probe, browser_control_rows)
        )
        probe.section("Classic ICNS RGB data", lambda: icon_rgb_operations(probe))
        probe.section("Classic ICNS RGB drawing", lambda: icon_rgb_draw_operations(probe))
        probe.section("ColorSync packed color conversion", lambda: color_sync_operations(probe))
        if color_profiles:
            probe.section("ColorSync real ICC profile queries and edits", lambda: color_profile_operations(probe))
        probe.section("Classic date, time and machine location", lambda: classic_clock_and_location(probe))
        probe.section("Carbon Sound Input", lambda: sound_input_operations(probe))
        probe.section("Carbon Sound Converter PCM and MACE streams", lambda: sound_converter_operations(probe))
        probe.section("Sound Manager volume and resource headers", lambda: sound_manager_operations(probe))
        probe.section("Power Manager activity timers", lambda: power_activity_operations(probe))
        probe.section("Code Fragment Manager connections", lambda: code_fragment_operations(probe, destination))
        probe.section("Navigation Services options and replies", lambda: navigation_operations(probe))
        probe.section("Shared memory, clock and TextEdit state", lambda: classic_state_operations(probe))
        probe.section("Script Manager type selection", lambda: type_selection_operations(probe))
        probe.section("QuickDraw ports and Textension objects", lambda: port_operations(probe))
        probe.section("Appearance cursors", lambda: theme_cursor_operations(probe))
        probe.section("Appearance theme fonts", lambda: theme_font_operations(probe))
        probe.section("Font Manager families", lambda: font_family_operations(probe))
        probe.section("Font Manager instances and tables", lambda: font_instance_operations(probe))
        probe.section("Font Manager face catalog", lambda: font_catalog_operations(probe))
        probe.section("QuickDraw geometry and coordinate transforms", lambda: geometry_operations(probe))
        probe.section("Classic Memory Manager and A5 accessors", lambda: classic_utility_accessors(probe))
        probe.section("Font and international script selection", lambda: font_script_queries(probe))
        probe.section("Script variables, key translation and text runs", lambda: script_utility_operations(probe))
        probe.section("Application manager traps", lambda: patched_vectors(probe, application_rows, sides=(0,)))
        probe.section("Installed application manager operations", lambda: application_operations(probe))
        probe.section("Carbon control data, focus and window bounds", lambda: control_manager_operations(probe))
        probe.section("Menu bar clock configuration", lambda: menu_clock_operations(probe))
        probe.section("Carbon menu state and properties", lambda: menu_manager_operations(probe))
        probe.section("Carbon Help Manager content, callbacks and settings", lambda: help_manager_operations(probe))
        probe.section("Carbon menu command IDs, state and properties", lambda: menu_command_operations(probe))
        probe.section(
            "Carbon menu hierarchies, title icons, definitions and ownership", lambda: menu_structure_operations(probe)
        )
        probe.section("Carbon menu titles and item text", lambda: menu_text_operations(probe))
        event_sections(probe)
        probe.section(
            "Carbon international comparison, date and resource APIs", lambda: international_operations(probe)
        )
        text_scrap_sections(probe)
        probe.section("Classic Collection Manager", lambda: collection_manager_operations(probe))
        probe.section("Classic Component Manager", lambda: component_operations(probe))
        probe.section(
            "Component manager message selectors and payloads",
            lambda: internet_config_operations(probe, internet_config, typed_messages),
        )
    for library, connection in reversed(probe.oracle_connections.items()):
        probe.call("CloseConnection", [probe.ptr(connection)])
        probe.p.expect(0, "close original " + library + " oracle")
    name = "PPC Carbon Toolbox Vector Probe"
    resources = []
    if file_entries:
        collection = (
            struct.pack(">I4siIh", 2, b"TAGB", 2, 0x40000000, 4)
            + b"LAST"
            + struct.pack(">4siIh", b"TAGA", -3, 0x40000005, 3)
            + b"odd\x00"
        )
        component = (
            struct.pack(">5I", 0x43507273, 0x74657374, 0x61726469, 0, 0)
            + struct.pack(">4sh", b"rCmp", 182)
            + bytes(18 + 8)
            + struct.pack(">hI", 185, 0)
            + struct.pack(">4sh", b"thnr", 182)
        )
        public_resources = struct.pack(">4shH4shH", b"PUBR", 314, 0, b"rCmp", 182, 0) + struct.pack(
            ">4shH4shH", b"STR#", 327, 0, b"STR#", 182, 0
        )
        # A Pascal 68K control definition with a long result and 12 bytes of arguments.
        epilogue = (0x4E74, 12)
        return_zero = (0x42AF, 16, *epilogue)  # clr.l 16(sp); rtd #12
        cdef = []
        for message, routine in (
            (27, (0x2F7C, 0x206F, 0x6B20, 16, *epilogue)),  # ' ok '
            (19, (0x2F7C, 0x22, 0x0200, 16, *epilogue)),  # best rect, part regions, contextual menus
            (14, (0x206F, 4, 0x20BC, 43, 49, 0x317C, 7, 4, *return_zero)),
            (31, (0x206F, 4, 0x2250, 0x2251, 0x237C, 1, 2, 2, 0x237C, 3, 4, 6, *return_zero)),
            (0x2F, (0x206F, 4, 0x117C, 1, 4, *return_zero)),  # menuDisplayed = true
        ):
            cdef.extend((0x0C6F, message, 8, 0x6600 | len(routine) * 2, *routine))
        cdef.extend(return_zero)
        drag_cdef = []
        for message, result in ((27, 0x206F6B20), (19, 0x1000), (0x26, 1), (0x27, 1), (0x28, 1)):
            routine = (0x2F7C, result >> 16, result & 0xFFFF, 16, *epilogue)
            drag_cdef.extend((0x0C6F, message, 8, 0x6600 | len(routine) * 2, *routine))
        drag_cdef.extend(return_zero)
        # 68K CDEF fixture. Capture messages 1000..1013 and their input bytes.
        # Get/set data can capture, fill, follow scalar addresses, or return
        # selection flags and view style using the fixture's refCon buffer.
        browser_cdef = bytes.fromhex(
            "0c6f001b0008660c2f7c206f6b2000104e74000c0c6f00130008660c2f7c"
            "0000004000104e74000c0c6f00140008670a0c6f00150008660000a2206f"
            "0004226f000a22512269002422d02028000822c02068000c53800c290002"
            "0030670000440c290003003067300c2900040030672c0c6f001500086612"
            "0c2900010030660a10fc006a51c8fffa600612d851c8fffc42af00104e74"
            "000c2068000420a9003460ee2f0a12290031610a1229002e6104245f60dc"
            "4a01671e245870000c01000166041012600c0c0100026604301260022012"
            "22c04e7522d84e750c6f03e8000865ae0c6f03f5000862a6226f000a2251"
            "226900247000302f000822c02029006c22c04a806708206f000453806006"
            "22af0004600612d851c8fffc2f7c8866770100104e74000c"
        )
        resources = [
            (b"CDEF", 182, struct.pack(f">{len(cdef)}H", *cdef)),
            (b"CDEF", 183, browser_cdef),
            (b"CDEF", 184, struct.pack(f">{len(drag_cdef)}H", *drag_cdef)),
            (b"nvrs", -20481, bytes.fromhex("02008000")),
            (b"wcls", 123, b"\x00"),
            (b"cltn", 182, collection),
            (b"cltn", 183, collection[:-2]),
            (b"thng", 182, component),
            (b"thnr", 182, public_resources),
            (b"ICN#", 185, bytes.fromhex("aa55aa55") + bytes(124) + b"\xff" * 128),
            (b"rCmp", 182, bytes.fromhex("70004e75")),
            (b"STR#", 182, b"\x00\x02\x03One\x03Two"),
        ]
        resources.extend(
            (b"CURS", cursor, struct.pack(">I", word) + bytes(64))
            for cursor, word in (
                (1, 0x476A0123),
                (2, 0x476A0124),
                (4, 0x476A0125),
                (-20721, 0x476A0126),
                (-20708, 0x476A0127),
            )
        )
        associations = ((18, 0, 3003), (0, 1, 3004), (12, 0, 3002), (9, 2, 3005))
        fond = struct.pack(">hh", 0, 32010) + bytes(48) + struct.pack(">H", len(associations) - 1)
        fond += b"".join(struct.pack(">3h", *entry) for entry in associations)
        sfnt = struct.pack(">I4H4sIII", 0x00010000, 1, 16, 0, 0, b"head", 0, 28, 4) + bytes.fromhex("11223344")
        resources.extend(((b"FOND", 32010, fond), (b"sfnt", 3004, sfnt)))
        resources.extend((b"NFNT", identifier, bytes(26)) for identifier in (3002, 3003, 3005))
    probe.p.finish(destination, name, carbon=True, extra_resources=resources)
    expected = set(probe.names) | native_names
    if text_only:
        exports = (ROOT / "carbonlib/src/exports.c").read_text()
        expected = set(re.findall(r"^CARBON_TEXTENSION\((\w+),", exports, re.M))
        expected.update(
            ("TypeSelectClear", "TypeSelectNewKey", "TypeSelectCompare", "TypeSelectFindItem", "CharacterType")
        )
    probe.write_coverage(
        destination,
        name,
        expected,
        cleanup_checks=len(probe.oracle_connections),
    )
    if math_only:
        coverage = destination / "coverage.json"
        document = json.loads(coverage.read_text())
        document["math_reference"] = True
        coverage.write_text(json.dumps(document, indent=2) + "\n", newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--original-only", action="store_true")
    parser.add_argument("--math", action="store_true")
    parser.add_argument("--atsui", action="store_true")
    args = parser.parse_args()
    make_probe(args.destination, args.original_only, math_only=args.math, atsui=args.atsui)
