# +===================================================================
#   File:      clipboard_live_probe.py
#
#   Summary:   Exercise real Windows clipboard transfers through a paused ROMAW's Scrap Manager and QuickDraw.
#
#   Classes:   None.
#
#   Functions: bind, value, invoke, publish, publish_bitmap, host_bytes, handle_bytes, fetch, put, verify_picture,
#              make_dib, check
#
#   Origin:    Executor clipboard integration regression probe. Run through Windows GDB after application startup.
#
#   Copyright and Legal notices: See COPYING.
# ===================================================================+

"""This opt-in probe replaces the real clipboard with generated test data.

Run on the emulation thread in Windows GDB, after reaching WaitNextEvent.
No files are copied to the clipboard and no existing clipboard data is logged.
"""

import ctypes as c
import struct
import time

import gdb


def bind(library, name, result, *arguments):
    function = getattr(library, name)
    function.restype = result
    function.argtypes = arguments
    return function


user = c.WinDLL("user32", use_last_error=True)
kernel = c.WinDLL("kernel32", use_last_error=True)
gdi = c.WinDLL("gdi32", use_last_error=True)
pointer, integer, size_t = c.c_void_p, c.c_uint32, c.c_size_t
open_clipboard = bind(user, "OpenClipboard", c.c_int, pointer)
close_clipboard = bind(user, "CloseClipboard", c.c_int)
empty_clipboard = bind(user, "EmptyClipboard", c.c_int)
set_clipboard = bind(user, "SetClipboardData", pointer, integer, pointer)
get_clipboard = bind(user, "GetClipboardData", pointer, integer)
sequence = bind(user, "GetClipboardSequenceNumber", integer)
register = bind(user, "RegisterClipboardFormatW", integer, c.c_wchar_p)
allocate_global = bind(kernel, "GlobalAlloc", pointer, integer, size_t)
lock_global = bind(kernel, "GlobalLock", pointer, pointer)
unlock_global = bind(kernel, "GlobalUnlock", c.c_int, pointer)
free_global = bind(kernel, "GlobalFree", pointer, pointer)
size_global = bind(kernel, "GlobalSize", size_t, pointer)
create_window = bind(
    user,
    "CreateWindowExW",
    pointer,
    integer,
    c.c_wchar_p,
    c.c_wchar_p,
    integer,
    c.c_int,
    c.c_int,
    c.c_int,
    c.c_int,
    pointer,
    pointer,
    pointer,
    pointer,
)
destroy_window = bind(user, "DestroyWindow", c.c_int, pointer)
create_bitmap = bind(gdi, "CreateDIBSection", pointer, pointer, pointer, integer, c.POINTER(pointer), pointer, integer)
delete_bitmap = bind(gdi, "DeleteObject", c.c_int, pointer)
owner = create_window(0, "STATIC", "ROMAW clipboard test", 0, 0, 0, 0, 0, pointer(-3), None, None, None)
assert owner, "Unable to create clipboard test owner"


def value(expression):
    return int(gdb.parse_and_eval(expression))


def invoke(expression):
    gdb.execute("call (void)" + expression, to_string=True)


def publish(formats):
    for attempt in range(50):
        if open_clipboard(owner):
            break
        time.sleep(0.02)
    else:
        raise RuntimeError("Windows clipboard is busy")
    try:
        assert empty_clipboard()
        for kind, data in formats:
            block = allocate_global(2, len(data))
            assert block
            address = lock_global(block)
            if not address:
                free_global(block)
                raise RuntimeError("Unable to lock clipboard data")
            c.memmove(address, data, len(data))
            unlock_global(block)
            if not set_clipboard(kind, block):
                free_global(block)
                raise RuntimeError("Unable to publish clipboard data")
    finally:
        close_clipboard()
    # Let clipboard listeners finish processing the publication before the
    # guest's single OpenClipboard attempt, as in an interactive paste.
    time.sleep(0.1)


def host_bytes(kind):
    for attempt in range(50):
        if open_clipboard(owner):
            break
        time.sleep(0.02)
    else:
        raise RuntimeError("Windows clipboard is busy")
    try:
        block = get_clipboard(kind)
        assert block, "Missing Windows clipboard format " + str(kind)
        address = lock_global(block)
        assert address
        try:
            return c.string_at(address, size_global(block))
        finally:
            unlock_global(block)
    finally:
        close_clipboard()


def publish_bitmap(dib):
    info = c.create_string_buffer(dib)
    bits = pointer()
    bitmap = create_bitmap(None, info, 0, c.byref(bits), None, 0)
    assert bitmap and bits.value
    c.memmove(bits.value, dib[40:], len(dib) - 40)
    opened = False
    try:
        for attempt in range(50):
            if open_clipboard(owner):
                opened = True
                break
            time.sleep(0.02)
        assert opened and empty_clipboard()
        assert set_clipboard(2, bitmap)
        bitmap = None  # Windows owns the HBITMAP after publication.
    finally:
        if opened:
            close_clipboard()
        if bitmap:
            delete_bitmap(bitmap)
    time.sleep(0.1)


memory = gdb.selected_inferior()
offset = value("ROMlib_offset")
saved_sr = value("cpu_state.sr")
saved_interrupt_mask = value("_virtual_interrupts_blocked")
gdb.execute("set _virtual_interrupts_blocked = 1")
gdb.execute("set cpu_state.sr = " + str(saved_sr | 0x700))
scratch = value("(void *)_NewPtr_flags(4096,0,1)")
if not scratch:
    gdb.execute("set _virtual_interrupts_blocked = " + str(saved_interrupt_mask))
    gdb.execute("set cpu_state.sr = " + str(saved_sr))
    destroy_window(owner)
    raise RuntimeError("Unable to allocate clipboard probe workspace")
handles = []
failures = []


def check(label, passed):
    print(("PASS: " if passed else "FAIL: ") + label, flush=True)
    if not passed:
        failures.append(label)


def handle_bytes(handle):
    size = value("(int)GetHandleSize((void *)" + str(handle) + ")")
    if not size:
        return b""
    address = int.from_bytes(bytes(memory.read_memory(handle, 4)), "big")
    return bytes(memory.read_memory(offset + address, size))


def fetch(kind, query=False):
    tag = int.from_bytes(kind, "big")
    handle = 0 if query else value("(void *)_NewHandle_flags(0,0,1)")
    assert query or handle
    if handle:
        handles.append(handle)
    length = value("(int)C_GetScrap((void *)%d,%d,(void *)%d)" % (handle, tag, scratch))
    assert length >= 0, (kind, length)
    where = int.from_bytes(bytes(memory.read_memory(scratch, 4)), "big", signed=True)
    if not query:
        return handle_bytes(handle)[:length], handle
    handle = offset + int.from_bytes(bytes(memory.read_memory(offset + 0x964, 4)), "big")
    data = handle_bytes(handle)
    assert 8 <= where <= len(data) - length
    assert data[where - 8 : where - 4] == kind
    assert int.from_bytes(data[where - 4 : where], "big") == length
    return data[where : where + length], handle


def put(kind, data):
    assert len(data) < 3500
    memory.write_memory(scratch + 512, data)
    error = value("(int)C_PutScrap(%d,%d,(void *)%d)" % (len(data), int.from_bytes(kind, "big"), scratch + 512))
    assert error == 0, error


colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255), (0, 0, 0), (255, 255, 0)]


def make_dib(bits, top_down=False):
    stride = ((3 * bits + 31) // 32) * 4
    pixels = bytearray()
    for y in range(2) if top_down else reversed(range(2)):
        row = bytearray()
        for red, green, blue in colors[y * 3 : y * 3 + 3]:
            row += bytes([blue, green, red]) + (b"\x7f" if bits == 32 else b"")
        pixels += row + bytes(stride - len(row))
    header = struct.pack("<IiiHHIIiiII", 40, 3, -2 if top_down else 2, 1, bits, 0, len(pixels), 0, 0, 0, 0)
    return header + pixels


def verify_picture(handle):
    invoke("C_GetGWorld((void *)%d,(void *)%d)" % (scratch + 16, scratch + 20))
    old_port, old_device = struct.unpack(">II", bytes(memory.read_memory(scratch + 16, 8)))
    memory.write_memory(scratch + 32, struct.pack(">hhhh", 0, 0, 2, 3))
    memory.write_memory(scratch + 24, bytes(4))
    error = value("(int)C_NewGWorld((void *)%d,32,(void *)%d,0,0,0)" % (scratch + 24, scratch + 32))
    assert not error, error
    world = offset + int.from_bytes(bytes(memory.read_memory(scratch + 24, 4)), "big")
    try:
        invoke("C_SetGWorld((void *)%d,0)" % world)
        invoke("C_DrawPicture((void *)%d,(void *)%d)" % (handle, scratch + 32))
        actual = []
        for y in range(2):
            for x in range(3):
                invoke("C_GetCPixel(%d,%d,(void *)%d)" % (x, y, scratch + 64))
                actual.append(
                    tuple(channel >> 8 for channel in struct.unpack(">HHH", bytes(memory.read_memory(scratch + 64, 6))))
                )
        return actual == colors
    finally:
        invoke(
            "C_SetGWorld((void *)%d,(void *)%d)"
            % (offset + old_port if old_port else 0, offset + old_device if old_device else 0)
        )
        invoke("C_DisposeGWorld((void *)%d)" % world)


try:
    for label, raw, expected in (
        ("ASCII", b"ROMAW clipboard test: ABC 123", b"ROMAW clipboard test: ABC 123"),
        ("CRLF", b"Line one\r\nLine two", b"Line one\rLine two"),
        ("empty", b"", b""),
    ):
        publish([(1, raw + b"\0")])
        before = sequence()
        check("Windows TEXT to guest " + label, fetch(b"TEXT")[0] == expected)
        check("InfoScrap offset query " + label, fetch(b"TEXT", True)[0] == expected)
        check("Clipboard reads preserve host ownership " + label, sequence() == before)

    style = struct.pack(">HihhhBBhHHH", 1, 0, 12, 9, 3, 0, 0, 12, 0, 0, 0)
    opaque = {
        b"styl": style,
        b"utxt": "Unicode test".encode("utf-16-be"),
        b"TEST": bytes(range(256)),
        b"snd ": b"sound-test\0\xff",
        b"moov": b"movie-test\0\x80",
        b"ODD!": b"abc",
        b"ZERO": b"",
    }
    formats = {kind: register("ROMAW_0x%08x" % int.from_bytes(kind, "big")) for kind in opaque}
    for kind, data in opaque.items():
        publish([(formats[kind], struct.pack("<i", len(data)) + data)])
        check("Windows custom to guest " + kind.decode("ascii"), fetch(kind)[0] == data)
        check("Custom InfoScrap offset " + kind.decode("ascii"), fetch(kind, True)[0] == data)

    assert not value("(int)C_ZeroScrap()")
    put(b"TEXT", b"ROMAW clipboard test\rSecond line")
    check("Guest TEXT to Windows", host_bytes(1).split(b"\0")[0] == b"ROMAW clipboard test\r\nSecond line")
    for kind, data in opaque.items():
        put(kind, data)
        raw = host_bytes(formats[kind])
        check(
            "Guest custom to Windows " + kind.decode("ascii"),
            struct.unpack("<i", raw[:4])[0] == len(data) and raw[4 : 4 + len(data)] == data,
        )
    check("Adding custom formats preserves TEXT", host_bytes(1).startswith(b"ROMAW clipboard test"))

    for native_format, bits, top_down in (
        (8, 24, False),
        (8, 32, False),
        (8, 24, True),
        (2, 24, False),
        (17, 32, False),
    ):
        label = "%d/%d/%s" % (native_format, bits, top_down)
        dib = make_dib(bits, top_down)
        if native_format == 2:
            publish_bitmap(dib)
        elif native_format == 17:
            header = bytearray(124)
            header[:40] = dib[:40]
            struct.pack_into("<I", header, 0, 124)
            struct.pack_into("<I", header, 16, 3)  # BI_BITFIELDS.
            struct.pack_into("<IIIII", header, 40, 0xFF0000, 0xFF00, 0xFF, 0xFF000000, 0x73524742)
            struct.pack_into("<I", header, 108, 4)  # LCS_GM_IMAGES; no embedded profile.
            publish([(17, bytes(header) + dib[40:])])
        else:
            publish([(8, dib)])
        picture, handle = fetch(b"PICT")
        check("Windows image to PICT pixels " + label, verify_picture(handle))
        check("PICT InfoScrap offset " + label, bool(fetch(b"PICT", True)[0]))
        assert not value("(int)C_ZeroScrap()")
        put(b"PICT", picture)
        invoke("render_all_clipboard_formats()")
        raw = host_bytes(register("ROMAW_0x50494354"))
        check(
            "Guest raw PICT to Windows " + label,
            struct.unpack("<i", raw[:4])[0] == len(picture) and raw[4 : 4 + len(picture)] == picture,
        )
        dib = host_bytes(8)
        header_size, width, height, planes, depth = struct.unpack_from("<IiiHH", dib)
        check(
            "Guest PICT exports DIB dimensions " + label,
            (width, abs(height), planes) == (3, 2, 1) and depth in (24, 32),
        )
        publish([(8, dib)])
        check("PICT to DIB round-trip colors " + label, verify_picture(fetch(b"PICT")[1]))

    publish([(1, b"ROMAW clipboard integration tests completed\0"), (8, make_dib(24))])
    print("CLIPBOARD_LIVE_RESULTS", {"failures": failures}, flush=True)
    assert not failures, failures
    print("CLIPBOARD_LIVE_OK", flush=True)
finally:
    try:
        for handle in handles:
            invoke("DisposHandle((void *)%d)" % handle)
        invoke("DisposPtr((void *)%d)" % scratch)
    finally:
        gdb.execute("set _virtual_interrupts_blocked = " + str(saved_interrupt_mask))
        gdb.execute("set cpu_state.sr = " + str(saved_sr))
        gdb.execute("set cpu_state.interrupt_status_changed = 0x7fffffff")
        destroy_window(owner)
