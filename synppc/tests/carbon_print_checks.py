#!/usr/bin/env python3
# +===================================================================
#   File:      carbon_print_checks.py
#
#   Summary:   Printing Manager scenarios for the grouped Carbon regression app.
#
#   Classes:   None.
#
#   Functions: make_printing_checks, ticket_checks, session_checks, conversion_checks, document_checks,
#              staged_dialog_checks, print_file_checks, raw_ticket_checks.
#
#   Origin:    CarbonLib 1.4 interface and Universal Interfaces 3.4.2.
#
#   Copyright and Legal notices: See synppc/COPYING and carbonlib/COPYING.
# ===================================================================+

import re
import struct

from make_ppc_carbon_toolbox_vectors_probe import ROOT, ToolboxProbe


def ticket_checks(probe, call):
    p = probe.p
    _, base = probe.raw(bytes(1536))
    ptr = lambda at: probe.ptr(base + at)
    slot = lambda at: probe.slot(base + at)
    settings, page = slot(0), slot(4)
    call("PMCreatePrintSettings", [ptr(0)])
    call("PMCreatePageFormat", [ptr(4)])
    call("PMRetain", [settings])
    call("PMRelease", [settings])
    call("PMNewPrintSettings", [ptr(8)])
    call("PMNewPageFormat", [ptr(12)])

    def word(offset, value, description):
        p.emit(0x807E0000 | (base + offset))
        p.expect(value, description)

    for get, set_, value, extra, default in (
        ("PMGetCopies", "PMSetCopies", 3, [0], 3),
        ("PMGetFirstPage", "PMSetFirstPage", 2, [0], 2),
        ("PMGetLastPage", "PMSetLastPage", 7, [0], 7),
    ):
        call(set_, [settings, value, *extra])
        call(get, [settings, ptr(28)])
        word(28, default, get + " reads the stored value")
    call("PMSetPageRange", [settings, 2, 8])
    call("PMGetPageRange", [settings, ptr(28), ptr(32)])
    word(28, 2, "page range minimum")
    word(32, 8, "page range maximum")
    call("PMSetPageRange", [settings, 8, 2], -30877)
    call("PMSetColorMode", [settings, 2])
    call("PMGetColorMode", [settings, ptr(36)])
    word(36, 0x00020000, "print color mode round trip")
    call("PMGetDestination", [settings, ptr(40), ptr(44)])
    title = probe.raw(b"Carbon Print\0")
    probe.call("CFStringCreateWithCString", [0, title, 0])
    p.nonzero("allocate a job name")
    p.emit(0x907E0000 | (base + 48))
    call("PMSetJobNameCFString", [settings, slot(48)])
    call("PMGetJobNameCFString", [settings, ptr(52)])
    probe.call("CFStringGetLength", [slot(52)])
    p.expect(12, "job name round trip")
    probe.call("CFRelease", [slot(48)])
    call("PMSetJobName", [settings, 0], -30871)
    call("PMBegin", [])
    legacy_name = probe.raw(b"\x03A:B")
    call("PMSetJobName", [settings, legacy_name])
    call("PMGetJobName", [settings, ptr(256)])
    word(256, 0x03412D42, "legacy job names normalize a colon to a hyphen")
    call("PMEnd", [])

    name, data = 0x43507274, probe.raw(b"ABCD")
    for ticket, kind in ((settings, "PrintSettings"), (page, "PageFormat")):
        call("PMSet" + kind + "ExtendedData", [ticket, name, 4, data])
        call("PMGet" + kind + "ExtendedData", [ticket, name, ptr(56), 0])
        word(56, 4, kind + " extended data length")
        call("PMGet" + kind + "ExtendedData", [ticket, name, ptr(56), ptr(60)])
        word(60, 0x41424344, kind + " extended data bytes")

    call("PMSetOrientation", [page, 2, 0])
    call("PMGetOrientation", [page, ptr(64)])
    word(64, 0x00020000, "orientation round trip")
    resolution = probe.raw(struct.pack(">2d", 300, 600))
    call("PMSetResolution", [page, resolution])
    call("PMGetResolution", [page, ptr(72)])
    word(72, 0x4072C000, "horizontal resolution round trip")
    word(80, 0x4082C000, "vertical resolution round trip")

    call("PMCreateSession", [ptr(24)])
    call("PMSessionDefaultPageFormat", [slot(24), page])
    call("PMSetScale", [page, 0, 0], fp=probe.raw(struct.pack(">d", 125)))
    call("PMGetScale", [page, ptr(192)])
    word(192, 0x405F4000, "print scale uses percentage units in the public interface")
    word(196, 0, "print scale retains the exact double-precision value")
    call("PMGetScale", [page, 0], -50)
    call("PMSetScale", [0, 0, 0], -30876, fp=probe.raw(struct.pack(">d", 100)))
    for name, right in (("PMSetUnadjustedPaperRect", 411), ("PMSetPhysicalPaperSize", 471)):
        call(name, [page, probe.raw(struct.pack(">4d", 7, 11, 607, right))])
        call("PMGetUnadjustedPaperRect", [page, ptr(96)], compare=(base + 96, 8))
    rectangle = probe.raw(struct.pack(">4d", 0, 0, 620, 420))
    for area in ("Page", "Paper"):
        call("PMGetUnadjusted" + area + "Rect", [page, ptr(128)], same_status=True)
        call("PMGetPhysical" + area + "Size", [page, ptr(160)], same_status=True)
        for offset in range(0, 32, 4):
            p.emit(0x807E0000 | (base + 128 + offset), 0x809E0000 | (base + 160 + offset))
            p.equal("physical and unadjusted " + area + " rectangle agree")
        call("PMSetAdjusted" + area + "Rect", [page, rectangle])
        call("PMGetAdjusted" + area + "Rect", [page, ptr(96)])
    call("PMRelease", [slot(24)])

    for ticket, kind, destination in ((settings, "PrintSettings", slot(8)), (page, "PageFormat", slot(12))):
        call("PMCopy" + kind, [ticket, destination])
        call("PMFlatten" + kind, [destination, ptr(16)])
        call("PMUnflatten" + kind, [slot(16), ptr(20)])
        probe.call("DisposeHandle", [slot(16)])
        call("PMDispose" + kind, [slot(20)])
        call("PMDispose" + kind, [destination])
        call("PMRelease", [ticket])


def session_checks(probe, call):
    p = probe.p
    _, base = probe.raw(bytes(1536))
    ptr = lambda at: probe.ptr(base + at)
    slot = lambda at: probe.slot(base + at)
    session, settings, page, printer = (slot(at) for at in (0, 4, 8, 12))
    call("PMBegin", [])
    call("PMBegin", [], -30871)
    call("PMEnd", [])
    call("PMEnd", [], -30871)
    call("PMCreateSession", [ptr(0)])
    call("PMCreatePrintSettings", [ptr(4)])
    call("PMCreatePageFormat", [ptr(8)])
    for prefix, arguments in (("PMSession", [session]), ("PM", [])):
        call(prefix + "DefaultPrintSettings", [*arguments, settings])
        call(prefix + "DefaultPageFormat", [*arguments, page])
        call(prefix + "ValidatePrintSettings", [*arguments, settings, ptr(16)])
        call(prefix + "ValidatePageFormat", [*arguments, page, ptr(16)])
        call(prefix + "SetIdleProc", [*arguments, 0])
        call(prefix + "SetError", [*arguments, -123])
        call(prefix + "Error", arguments, -123)
        call(prefix + "SetError", [*arguments, 0])
        call(prefix + "EnableColorSync", arguments, -30871)
        call(prefix + "DisableColorSync", arguments, -30871)
        general = probe.raw(struct.pack(">HH", 4, 0x7FFF) + bytes(126))
        call(prefix + "General", [*arguments, general], -50)
        p.emit(0xA87E0000 | (general[1] + 2))
        p.expect(0x7FFF, "rejected resolution opcode preserves its block through " + prefix)
        draft = probe.raw(struct.pack(">HH", 6, 0x7FFF) + bytes(126))
        call(prefix + "General", [*arguments, draft], same_status=True)
        call(prefix + "PostScriptHandle", [*arguments, 0], -50)
        call(prefix + "PostScriptData", [*arguments, 0, 0], -50)
    call("PMSessionPostScriptFile", [session, 0], -50)
    call("PMPostScriptFile", [0], -50)
    call("PMSessionGetGraphicsContext", [session, 0, ptr(20)], -30871)
    call("PMSessionGetDocumentFormatGeneration", [session, ptr(24)])
    probe.call("CFArrayGetValueAtIndex", [slot(24), 0])
    p.nonzero("the driver supplies a document format")
    p.emit(0x907E0000 | (base + 44))
    call("PMSessionSetDocumentFormatGeneration", [session, 0, 0, 0], -50)
    call("PMSessionSetDocumentFormatGeneration", [session, slot(44), 0, 0])
    probe.call("CFRelease", [slot(24)])
    call("PMSessionGetDocumentFormatSupported", [session, ptr(28), 0])
    probe.call("CFStringCreateWithCString", [0, probe.raw(b"application/postscript\0"), 0])
    p.nonzero("allocate a document format name")
    p.emit(0x907E0000 | (base + 40))
    call("PMSessionIsDocumentFormatSupported", [session, slot(40), ptr(32)])
    probe.call("CFRelease", [slot(40)])
    call("PMSessionGetCurrentPrinter", [session, ptr(12)])
    p.emit(0x807E0000 | (base + 12))
    p.nonzero("the print session provides its default printer")

    # Query the driver's real format and resolution data without opening a host print dialog.
    for name, arguments in (
        ("PMPrinterGetDescriptionURL", [printer, 0, ptr(36)]),
        ("PMPrinterGetPrinterResolution", [printer, page, ptr(48)]),
        ("PMPrinterGetPrinterResolutionCount", [printer, ptr(64)]),
        ("PMPrinterGetIndexedPrinterResolution", [printer, 1, ptr(72)]),
        ("PMPrinterGetLanguageInfo", [printer, ptr(96)]),
        ("PMPrinterGetDriverReleaseInfo", [printer, ptr(512)]),
        ("PMPrinterGetDriverCreator", [printer, ptr(768)]),
    ):
        call(name, arguments, same_status=True)
    for name, arguments, at, words in (
        ("PMGetPrinterResolution", [0x64667472], 48, 4),
        ("PMGetPrinterResolutionCount", [], 64, 1),
        ("PMGetIndexedPrinterResolution", [1], 72, 4),
        ("PMGetLanguageInfo", [], 96, 1),
        ("PMGetDriverReleaseInfo", [], 512, 1),
        ("PMGetDriverCreator", [], 768, 1),
    ):
        call(name, [*arguments, ptr(at)], same_status=True, compare=(base + at, words))
    call("PMIsPostScriptDriver", [ptr(812)], same_status=True, compare=(base + 812, 1))

    probe.call("CFStringCreateWithCString", [0, probe.raw(b"romaw.print.session\0"), 0])
    p.nonzero("make a print session dictionary key")
    p.emit(0x907E0000 | (base + 816))
    key = slot(816)
    # The saved error paths require the Classic C++ exception runtime.
    call("PMSessionGetDataFromSession", [session, key, ptr(820)], -30874, native_only=True)
    call("PMSessionSetError", [session, -123])
    call("PMSessionSetDataInSession", [session, key, key])
    call("PMSessionGetDataFromSession", [session, key, ptr(820)])
    p.emit(0x807E0000 | (base + 820), 0x809E0000 | (base + 816))
    p.equal("session data lookup returns the stored object")
    call("PMSessionError", [session], -123)
    call("PMSessionSetDataInSession", [session, key, 0], -50, native_only=True)
    call("PMSessionGetDataFromSession", [session, key, ptr(820)])
    p.emit(0x807E0000 | (base + 820), 0x809E0000 | (base + 816))
    p.equal("a rejected session data write preserves the stored object")
    call("PMSessionSetError", [session, 0])
    probe.call("CFRelease", [key])

    call("PMMakeOldPrintRecord", [settings, page, ptr(824)])
    p.emit(0x807E0000 | (base + 824))
    p.nonzero("the legacy conversion creates a classic print handle")
    call("PMSessionConvertOldPrintRecord", [0, slot(824), ptr(828), ptr(832)])
    probe.call("DisposeHandle", [slot(824)])
    call("PMSessionMakeOldPrintRecord", [0, slot(828), slot(832), ptr(824)])
    call("PMRelease", [slot(828)])
    call("PMRelease", [slot(832)])
    call("PMConvertOldPrintRecord", [slot(824), ptr(828), ptr(832)])
    probe.call("DisposeHandle", [slot(824)])
    call("PMGetCopies", [slot(828), ptr(836)], compare=(base + 836, 1))
    call("PMRelease", [slot(828)])
    call("PMRelease", [slot(832)])
    # These fields belong to the packed print-dialog record. Creating a GUI
    # dialog is unnecessary to exercise their storage and callback round trip.
    dialog = probe.raw(bytes(184))
    call("PMGetDialogPtr", [dialog, ptr(800)])
    p.emit(0x807E0000 | (base + 800))
    p.pointer(dialog[1], "PMGetDialogPtr returns the same record")
    for field, value in (("ModalFilterProc", 0x13572468), ("ItemProc", 0x87654321)):
        call("PMSet" + field, [dialog, value])
        call("PMGet" + field, [dialog, ptr(804)])
        p.emit(0x807E0000 | (base + 804))
        p.expect(value, field + " round trip")
    for field in ("DialogAccepted", "DialogDone"):
        call("PMSet" + field, [dialog, 1])
        call("PMGet" + field, [dialog, ptr(808)])
        p.emit(0x887E0000 | (base + 808))
        p.expect(1, field + " round trip")
    call("PMRelease", [settings])
    call("PMRelease", [page])
    call("PMRelease", [session])


def conversion_checks(probe, call):
    p = probe.p
    _, base = probe.raw(bytes(160))
    ptr = lambda at: probe.ptr(base + at)
    slot = lambda at: probe.slot(base + at)
    for name, string in (("CString", b"Wide \xc3\xa9\0"), ("PString", b"\x06Wide \x8e")):
        call("PMCreateCFStringFrom" + name, [0, probe.raw(string), ptr(0)])
        probe.call("CFStringGetLength", [slot(0)])
        p.expect(6, name + " converted string length")
        probe.call("CFStringGetCharacterAtIndex", [slot(0), 5])
        p.expect(0xE9, name + " decodes the declared encoding")
        probe.call("CFRelease", [slot(0)])
    for name, num_type, value, words in (
        ("SInt32", 3, -72, [-72]),
        ("Double", 13, 0, [0x40398000, 0]),
    ):
        arguments = [0, value, ptr(0)] if name == "SInt32" else [0, 0, 0, ptr(0)]
        floating = None if name == "SInt32" else probe.raw(struct.pack(">d", 25.5))
        call("PMCreateCFNumberFrom" + name, arguments, fp=floating)
        probe.call("CFNumberGetValue", [slot(0), num_type, ptr(8)])
        p.expect(1, name + " converted number type")
        for index, word in enumerate(words):
            p.emit(0x807E0000 | (base + 8 + 4 * index))
            p.expect(word, name + " converted number payload")
        probe.call("CFRelease", [slot(0)])
    for kind, values in (("Rect", (-1.25, 0, 620, 420)), ("Resolution", (300, 600))):
        data = probe.raw(struct.pack(">" + "d" * len(values), *values))
        call("PMCreateCFArrayFromPM" + kind, [0, data, ptr(0)])
        call("PMCreatePM" + kind + "FromCFArray", [slot(0), ptr(16)])
        for offset in range(0, len(values) * 8, 4):
            p.emit(0x807E0000 | (base + 16 + offset), 0x809E0000 | (data[1] + offset))
            p.equal(kind + " converted numeric tuple")
        probe.call("CFRelease", [slot(0)])

    call("PMCreateCFStringFromCFIndex", [0, -24, ptr(0)])
    call("PMCFTypeIsStringOrArrayOfStrings", [slot(0)], 1)
    call("PMCreateCStringFromCFString", [slot(0), ptr(4), 0])
    p.emit(0x807E0000 | (base + 4), 0x80630000)
    p.expect(0x2D323400, "the signed print index becomes a NUL-terminated C string")
    probe.call("free", [slot(4)])
    p.emit(0x807E0000 | base, 0x907E0000 | (base + 88), 0x907E0000 | (base + 92))
    call("PMCreateCFArray", [0, ptr(88), 2, ptr(8)])
    call("PMCFTypeIsStringOrArrayOfStrings", [slot(8)], 1)
    probe.call("CFArrayGetCount", [slot(8)])
    p.expect(2, "the print array retains both supplied string objects")
    probe.call("CFArrayGetValueAtIndex", [slot(8), 1])
    p.emit(0x809E0000 | base)
    p.equal("the print array stores the actual string reference")
    call("PMCreateCFArray", [0, 0, 0, ptr(12)])
    call("PMCFTypeIsStringOrArrayOfStrings", [slot(12)], 0)
    call("PMCFTypeIsStringOrArrayOfStrings", [0], 0)
    probe.call("CFRelease", [slot(12)])
    call("PMCreateCFNumberFromSInt32", [0, 19, ptr(12)])
    probe.call("CFArrayAppendValue", [slot(8), slot(12)])
    call("PMCFTypeIsStringOrArrayOfStrings", [slot(8)], 0)
    probe.call("CFArrayRemoveValueAtIndex", [slot(8), 2])
    probe.call("CFRelease", [slot(12)])

    for at, name in ((96, b"Printing test array\0"), (100, b"org.romaw.CarbonPrintingFixture\0")):
        probe.call("CFStringCreateWithCString", [0, probe.raw(name), 0])
        p.nonzero("allocate printing preferences key or application ID")
        p.emit(0x907E0000 | (base + at))
    key, app = slot(96), slot(100)
    user = probe.constant("kCFPreferencesCurrentUser")
    host = probe.constant("kCFPreferencesAnyHost")
    probe.call("CFPreferencesSetValue", [key, slot(8), app, user, host])
    call("PMCFPrefsCopyMutableArray", [key, app, user, host, ptr(12)])
    probe.call("CFArrayAppendValue", [slot(12), slot(0)])
    probe.call("CFArrayGetCount", [slot(12)])
    p.expect(3, "the preference getter returns a mutable copy")
    probe.call("CFArrayGetCount", [slot(8)])
    p.expect(2, "editing the copied array leaves the original intact")
    probe.call("CFRelease", [slot(12)])
    probe.call("CFPreferencesSetValue", [key, 0, app, user, host])
    call("PMCFPrefsCopyMutableArray", [key, app, user, host, ptr(12)], -50)
    for at in (0, 8, 96, 100):
        probe.call("CFRelease", [slot(at)])


def document_checks(probe, call):
    p = probe.p
    _, base = probe.raw(bytes(64))
    ptr = lambda at: probe.ptr(base + at)
    slot = lambda at: probe.slot(base + at)
    session, settings, page = slot(28), slot(32), slot(36)
    call("PMCreateSession", [ptr(28)])
    call("PMCreatePrintSettings", [ptr(32)])
    call("PMCreatePageFormat", [ptr(36)])
    call("PMSessionDefaultPrintSettings", [session, settings])
    call("PMSessionDefaultPageFormat", [session, page])
    call("PMSessionBeginDocumentNoDialog", [0, settings, page], -30879)
    call("PMSessionBeginDocumentNoDialog", [session, 0, page], -30875)
    call("PMSessionBeginDocumentNoDialog", [session, settings, 0], -30876)
    call("PMSessionBeginPageNoDialog", [session, 0, 0], -30871)
    call("PMSessionEndPageNoDialog", [session], -30871)
    call("PMSessionEndDocumentNoDialog", [session], -30871)
    call("PMSessionPostScriptBegin", [session], -30871)
    call("PMSessionPostScriptEnd", [session], -30871)
    call("PMBeginDocument", [settings, page, 0], -50)
    call("PMEndDocument", [0], -30871)
    call("PMBeginPage", [0, 0], -30871)
    call("PMEndPage", [0], -30871)
    call("PMGetGrafPtr", [session, ptr(40)], -30871)

    # 68K PrGlue shim. Read the Pascal selector above the return address.
    # For the document and page selectors, increment counters in the shared
    # record, return a fixture port for PrOpenDoc and pop the caller's arguments
    # with RTD. The two dialog selectors report Cancel. All other selectors
    # tail-call the saved vector with their original stack.
    driver = probe.raw(
        bytes.fromhex(
            "207c000000000caf04000c00000467360caf100008080004673a0caf1800040c"
            "000467380caf08000484000467360caf2a040484000467340caf320404880004"
            "672a2f104e7552a800082f68000400144e74001052a8000c4e74000c52a80010"
            "4e74000852a800144e74000852a80018426f000c4e740008"
        )
    )
    port = probe.raw(bytes(256))
    p.emit(
        0x816011F4,
        0x917E0000 | base,
        0x397E0000 | port[1],
        0x917E0000 | (base + 4),
        0x397E0000 | base,
        0x917E0000 | (driver[1] + 2),
        0x397E0000 | driver[1],
        0x916011F4,
    )
    for name, arguments in (
        ("PMSessionPageSetupDialog", [session, page]),
        ("PMSessionPrintDialog", [session, settings, page]),
        ("PMPageSetupDialog", [page]),
        ("PMPrintDialog", [settings, page]),
    ):
        p.emit(0x38600001, 0x987E0000 | (base + 44))
        call(name, [*arguments, ptr(44)])
        p.emit(0x887E0000 | (base + 44))
        p.expect(0, name + " clears the accepted flag on cancellation")
    call("PMSessionBeginDocumentNoDialog", [session, settings, page])
    call("PMSessionBeginDocument", [session, settings, page], -30871)
    call("PMSessionBeginPage", [session, page, 0])
    call("PMSessionGetGraphicsContext", [session, 0, ptr(40)])
    p.emit(0x807E0000 | (base + 40))
    p.pointer(port[1], "graphics context comes from the classic printer driver")
    call("PMSessionPostScriptBegin", [session])
    call("PMSessionPostScriptBegin", [session], -30871)
    call("PMSessionPostScriptEnd", [session])
    call("PMSessionPostScriptEnd", [session], -30871)
    call("PMSessionEndPageNoDialog", [session])
    call("PMSessionBeginPageNoDialog", [session, 0, 0])
    call("PMSessionEndPage", [session])
    call("PMSessionEndDocument", [session])
    call("PMBeginDocument", [settings, page, ptr(48)])
    p.emit(0x807E0000 | (base + 48), 0x809E0000 | (base + 28))
    p.equal("PMBeginDocument returns the default session as its print context")
    call("PMBeginPage", [slot(48), 0])
    call("PMGetGrafPtr", [slot(48), ptr(40)])
    p.emit(0x807E0000 | (base + 40))
    p.pointer(port[1], "the legacy port query reads the active page graphics context")
    call("PMPostScriptBegin", [])
    call("PMPostScriptEnd", [])
    call("PMEndPage", [slot(48)])
    call("PMEndDocument", [slot(48)])
    for offset, count, title in (
        (8, 2, "two documents opened"),
        (12, 3, "three pages opened"),
        (16, 3, "three pages closed"),
        (20, 2, "two documents closed"),
        (24, 4, "four dialogs reached the driver"),
    ):
        p.emit(0x807E0000 | (base + offset))
        p.expect(count, title)
    p.emit(0x817E0000 | base, 0x916011F4)
    call("PMRelease", [page])
    call("PMRelease", [settings])
    call("PMRelease", [session])


def staged_dialog_checks(probe, call):
    p = probe.p
    _, base = probe.raw(bytes(64))
    ptr = lambda at: probe.ptr(base + at)
    slot = lambda at: probe.slot(base + at)
    session, settings, page = slot(24), slot(28), slot(32)
    call("PMCreateSession", [ptr(24)])
    call("PMCreatePrintSettings", [ptr(28)])
    call("PMCreatePageFormat", [ptr(32)])
    call("PMSessionDefaultPrintSettings", [session, settings])
    call("PMSessionDefaultPageFormat", [session, page])

    call("PMSessionPageSetupDialogInit", [0, page, ptr(36)], -30879)
    call("PMSessionPrintDialogInit", [session, 0, page, ptr(36)], -30875)
    call("PMSessionPrintDialogInit", [session, settings, 0, ptr(36)], -30876)
    call("PMSessionPageSetupDialogInit", [session, page, 0], -50)
    # Trace the classic driver selectors and return a fixture dialog record.
    code = bytearray.fromhex("207c00000000")
    branches = []
    for selector in (0x3C04040C, 0x44040410, 0x4A040894):
        code.extend(struct.pack(">HIH", 0x0CAF, selector, 4))
        branches.append(len(code))
        code.extend(bytes.fromhex("6700"))
    code.extend(bytes.fromhex("2f104e75"))
    init = len(code)
    code.extend(bytes.fromhex("52a800082f680004000c4e740008"))
    main = len(code)
    code.extend(bytes.fromhex("52a8000c216f00080010426f00104e74000c"))
    for branch, target in zip(branches, (init, init, main)):
        code[branch + 1] = target - branch - 2
    driver = probe.raw(code)
    dialog = probe.raw(bytes(184))
    p.emit(
        0x816011F4,
        0x917E0000 | base,
        0x397E0000 | dialog[1],
        0x917E0000 | (base + 4),
        0x397E0000 | base,
        0x917E0000 | (driver[1] + 2),
        0x397E0000 | driver[1],
        0x916011F4,
    )
    for name, arguments in (
        ("PMSessionPageSetupDialogInit", [session, page]),
        ("PMPageSetupDialogInit", [page]),
        ("PMSessionPrintDialogInit", [session, settings, page]),
        ("PMPrintDialogInitWithPageFormat", [settings, page]),
        ("PMPrintDialogInit", [settings]),
    ):
        call(name, [*arguments, ptr(36)])
        p.emit(0x807E0000 | (base + 36))
        p.pointer(dialog[1], name + " returns the dialog supplied by the classic driver")
    p.emit(0x807E0000 | (base + 8))
    p.expect(5, "the five initialization calls use the live printer vector")

    callback = probe.raw(bytes(8))
    p.routines.append((callback[1], [0x38600000, 0x4E800020]))
    for kind, arguments in (("PageSetup", [page]), ("Print", [settings, page])):
        probe.call("NewPM" + kind + "DialogInitUPP", [callback])
        p.nonzero("allocate the staged dialog initialization callback")
        p.emit(0x907E0000 | (base + 40))
        for prefix, context in (("PMSession", [session]), ("PM", [])):
            # DialogMain consumes the staged print record even on cancellation.
            init_name = prefix + kind + "DialogInit"
            if prefix == "PM" and kind == "Print":
                init_name += "WithPageFormat"
            call(init_name, [*context, *arguments, ptr(36)])
            p.emit(0x38600001, 0x987E0000 | (base + 44))
            call(prefix + kind + "DialogMain", [*context, *arguments, ptr(44), slot(40)])
            p.emit(0x887E0000 | (base + 44))
            p.expect(0, "the staged dialog reports the driver's cancellation")
            p.emit(0x807E0000 | (base + 16))
            p.nonzero("the classic driver receives a staged initialization descriptor")
        probe.call("DisposePM" + kind + "DialogInitUPP", [slot(40)])
    p.emit(0x807E0000 | (base + 8))
    p.expect(9, "each staged dialog receives a fresh driver record")
    p.emit(0x807E0000 | (base + 12))
    p.expect(4, "the four staged dialog calls enter the current classic driver")
    p.emit(0x817E0000 | base, 0x916011F4)
    call("PMRelease", [page])
    call("PMRelease", [settings])
    call("PMRelease", [session])


def print_file_checks(probe, call):
    p = probe.p
    _, base = probe.raw(bytes(176))
    ptr = lambda at: probe.ptr(base + at)
    slot = lambda at: probe.slot(base + at)
    probe.call("HGetVol", [0, ptr(144), ptr(148)])
    p.expect(0, "save the current Classic working directory")
    probe.call("GetCurrentProcess", [ptr(0)])
    p.expect(0, "obtain the fixture process serial number")
    p.emit(0x3860003C, 0x907E0000 | (base + 8), 0x387E0000 | (base + 68), 0x907E0000 | (base + 64))
    probe.call("GetProcessInformation", [ptr(0), ptr(8)])
    p.expect(0, "locate the staged fixture directory")
    p.emit(0xA87E0000 | (base + 68), 0x907E0000 | (base + 156))
    volume, parent = slot(156), slot(70)
    probe.call("HSetVol", [0, volume, parent])
    p.expect(0, "use the staged fixture directory for print file calls")

    name = b"Carbon Printed Data"
    path = probe.raw(b":" + name + b"\0")
    probe.call("CFDataCreate", [0, probe.raw(b"1234567"), 7])
    p.nonzero("allocate the print data payload")
    p.emit(0x907E0000 | (base + 160))
    call("PMWriteCFDataToFile", [probe.raw(name + b"\0"), slot(160)])
    probe.call("CFRelease", [slot(160)])
    call("PMReadCFDataFromFile", [path, ptr(164)])
    probe.call("CFDataGetLength", [slot(164)])
    p.expect(7, "bare and colon-relative paths open the same file")
    probe.call("CFRelease", [slot(164)])
    probe.call("CFDataCreate", [0, probe.raw(b"\x00\x1a\xff"), 3])
    p.nonzero("allocate a short binary replacement")
    p.emit(0x907E0000 | (base + 160))
    call("PMWriteCFDataToFile", [path, slot(160)])
    call("PMReadCFDataFromFile", [path, ptr(164)])
    probe.call("CFDataGetLength", [slot(164)])
    p.expect(3, "replacing a print data file truncates the old suffix")
    probe.call("CFDataGetBytePtr", [slot(164)])
    p.emit(0xA0A30000, 0x88630002)
    p.expect(0xFF, "binary print file data preserves its final byte")
    p.emit(0x7CA32B78)
    p.expect(0x1A, "binary print file data preserves NUL and Ctrl-Z")
    probe.call("CFRelease", [slot(164)])
    call("PMReadCFPropertyListFromFile", [path, ptr(164)], -30870)
    p.emit(0x807E0000 | (base + 164))
    p.expect(0, "malformed property-list input clears the output")

    probe.call("CFStringCreateWithCString", [0, probe.raw(b"Print plist\0"), 0])
    p.nonzero("allocate the print property list")
    p.emit(0x907E0000 | (base + 168))
    call("PMWriteCFPropertyListToFile", [path, slot(168)])
    call("PMReadCFPropertyListFromFile", [path, ptr(164)])
    probe.call("CFEqual", [slot(164), slot(168)])
    p.expect(1, "the print property list round-trips through the file")
    for offset in (160, 164, 168):
        probe.call("CFRelease", [slot(offset)])
    probe.call("CFDataCreate", [0, 0, 0])
    p.nonzero("allocate empty print data")
    p.emit(0x907E0000 | (base + 160))
    call("PMWriteCFDataToFile", [path, slot(160)], -30870)
    call("PMReadCFDataFromFile", [path, ptr(164)], -30870)
    probe.call("CFRelease", [slot(160)])
    probe.call("HDelete", [volume, parent, probe.raw(bytes([len(name)]) + name)])
    p.expect(0, "remove the fixture print file")
    call("PMReadCFDataFromFile", [path, ptr(164)], -30870)
    p.emit(0xA87E0000 | (base + 144), 0x907E0000 | (base + 156))
    probe.call("HSetVol", [0, slot(156), slot(148)])
    p.expect(0, "restore the original working directory")


def raw_ticket_checks(probe, call):
    p = probe.p
    _, base = probe.raw(bytes(32))
    ptr = lambda at: probe.ptr(base + at)
    slot = lambda at: probe.slot(base + at)
    call("PMTicketSetRetainCount", [0, -1], -50)
    call("PMTicketSetRetainCount", [0, 1], -30891)
    call("PMTicketGetTypedItem", [0, 0, 0, 0, 0, ptr(4)], -30891)

    plist = (
        b'<?xml version="1.0"?><plist version="1.0"><dict>'
        b"<key>com.apple.print.ticket.type</key><string>com.apple.print.PageFormatTicket</string>"
        b"<key>com.apple.print.ticket.APIVersion</key><string>00.20</string>"
        b"<key>TestValue</key><dict><key>com.apple.print.ticket.itemArray</key>"
        b"<array><dict><key>com.apple.print.ticket.stateFlag</key><integer>0</integer>"
        b"<key>TestValue</key><string>ticket data</string></dict></array></dict>"
        b"</dict></plist>"
    )
    probe.call("CFDataCreate", [0, probe.far_raw(plist), len(plist)])
    p.nonzero("allocate print ticket XML data")
    p.emit(0x907E0000 | (base + 8))
    probe.call("CFPropertyListCreateFromXMLData", [0, slot(8), 1, 0])
    p.nonzero("create a mutable print ticket")
    p.emit(0x907E0000 | base)
    probe.call("CFRelease", [slot(8)])

    for count in (3, 1):
        call("PMTicketSetRetainCount", [slot(0), count])
        probe.call("CFGetRetainCount", [slot(0)])
        p.expect(count, "ticket retain count uses the live Core Foundation object")
    for name, locked in (("PMTicketLock", 1), ("PMTicketUnlock", 0)):
        call(name, [slot(0)])
        probe.call("CFStringCreateWithCString", [0, probe.far_raw(b"com.apple.print.ticket.privateLock\0"), 0])
        p.nonzero("allocate the ticket lock key")
        p.emit(0x907E0000 | (base + 12))
        probe.call("CFDictionaryGetValue", [slot(0), slot(12)])
        p.nonzero("the ticket lock key is present")
        p.emit(0x907E0000 | (base + 16))
        probe.call("CFBooleanGetValue", [slot(16)])
        p.expect(locked, "ticket lock updates the shared Boolean flag")
        probe.call("CFRelease", [slot(12)])

    probe.call("CFStringCreateWithCString", [0, probe.far_raw(b"TestValue\0"), 0])
    p.nonzero("allocate the print ticket item key")
    p.emit(0x907E0000 | (base + 12))
    probe.call("CFStringGetTypeID")
    p.emit(0x907E0000 | (base + 20))
    call("PMTicketGetTypedItem", [slot(0), 0, 0, slot(12), slot(20), ptr(4)])
    probe.call("CFStringGetLength", [slot(4)])
    p.expect(11, "typed lookup returns the borrowed ticket item")
    probe.call("CFBooleanGetTypeID")
    p.emit(0x907E0000 | (base + 20))
    call("PMTicketGetTypedItem", [slot(0), 0, 0, slot(12), slot(20), ptr(4)], -30893)
    probe.call("CFStringGetLength", [slot(4)])
    p.expect(11, "a type mismatch leaves the borrowed item in the output")
    probe.call("CFRelease", [slot(12)])
    call("PMTicketSetRetainCount", [slot(0), 0])


def make_printing_checks(destination, reference_only):
    definitions = ROOT / "carbonlib/src"
    names = {
        "PMBegin",
        "PMEnd",
        "PMCreateSession",
        "PMBeginDocument",
        "PMEndDocument",
        "PMBeginPage",
        "PMEndPage",
        "PMGetGrafPtr",
        "PMIsPostScriptDriver",
        "PMSessionMakeOldPrintRecord",
        "PMSessionConvertOldPrintRecord",
    }
    for table in definitions.glob("printing*_entries.h"):
        names.update(re.findall(r"^CARBON_PRINT\w*\((\w+),", table.read_text(), re.M))
    if len(names) != 164:
        raise ValueError("Incomplete Printing Manager batch")
    support = {
        "CFArrayGetValueAtIndex",
        "CFArrayAppendValue",
        "CFArrayGetCount",
        "CFArrayRemoveValueAtIndex",
        "CFBooleanGetTypeID",
        "CFBooleanGetValue",
        "CFDataCreate",
        "CFDataGetBytePtr",
        "CFDataGetLength",
        "CFDictionaryGetValue",
        "CFEqual",
        "CFGetRetainCount",
        "CFNumberGetValue",
        "CFPreferencesSetValue",
        "CFPropertyListCreateFromXMLData",
        "CFRelease",
        "CFStringCreateWithCString",
        "CFStringGetLength",
        "CFStringGetTypeID",
        "CFStringGetCharacterAtIndex",
        "DebugStr",
        "DisposeHandle",
        "NewPMPageSetupDialogInitUPP",
        "NewPMPrintDialogInitUPP",
        "DisposePMPageSetupDialogInitUPP",
        "DisposePMPrintDialogInitUPP",
        "GetCurrentProcess",
        "GetProcessInformation",
        "HDelete",
        "HGetVol",
        "HSetVol",
    }
    probe = ToolboxProbe([{"name": name} for name in sorted(names)], reference_only, native_names=support)
    probe.p.imports.append(("StdCLib", "free", 2))
    p = probe.p
    _, statuses = probe.raw(bytes(400))
    same_index = 0
    labels = {}

    def run_section(name, scenario, *, native_section=False):
        def check():
            nonlocal same_index
            first = same_index
            for side in (0,) if native_section else (1,) if reference_only else (1, 0):
                same_index = first

                def call(symbol, arguments, expected=0, *, same_status=False, fp=None, compare=None, native_only=False):
                    nonlocal same_index
                    if side and native_only:
                        return
                    key = (symbol, side)
                    if key not in labels:
                        label = ("saved " if side else "native ") + symbol
                        labels[key] = probe.far_raw(bytes([len(label)]) + label.encode("ascii"))
                    probe.call("DebugStr", [labels[key]])
                    if fp:
                        p.emit(0xC83E0000 | fp[1])
                    probe.call(symbol, arguments, side)
                    if not same_status:
                        p.expect(expected, symbol + " returns the expected result")
                    elif side:
                        p.emit(0x907E0000 | (statuses + 4 * same_index))
                    else:
                        p.emit(0x809E0000 | (statuses + 4 * same_index))
                        p.equal(symbol + " agrees with the saved driver status")
                    same_index += same_status
                    if compare:
                        address, words = compare
                        for index in range(words):
                            reference = statuses + 4 * same_index
                            p.emit(0x807E0000 | (address + 4 * index))
                            if side:
                                p.emit(0x907E0000 | reference)
                            else:
                                p.emit(0x809E0000 | reference)
                                p.equal(symbol + " matches the saved output")
                            same_index += 1

                scenario(probe, call)

        probe.section(name, check)

    run_section("Print settings and page-format tickets", ticket_checks)
    run_section("Print session state and printer queries", session_checks)
    run_section("Core Foundation print conversions", conversion_checks)
    run_section("Document and page lifecycle", document_checks)
    run_section("Staged print dialogs", staged_dialog_checks)
    run_section("Raw print ticket ownership and typed items", raw_ticket_checks)
    if not reference_only:
        # The saved print file wrappers depend on Classic StdCLib file streams.
        run_section("Print data and property-list files", print_file_checks, native_section=True)
    probe.call("CloseConnection", [probe.ptr(probe.oracle_connections["CarbonLib"])])
    p.expect(0, "release the saved CarbonLib connection")
    application = "PPC Carbon Printing Checks"
    p.finish(destination, application, carbon=True)
    probe.write_coverage(destination, application, names)
