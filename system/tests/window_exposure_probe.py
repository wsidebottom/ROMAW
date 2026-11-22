# +===================================================================
#   File:      window_exposure_probe.py
#
#   Summary:   Verify window exposure and update visibility across activation.
#
#   Classes:   None.
#
#   Functions: value, invoke, main (GDB Python entry point).
#
#   Origin:    Regression fixture for SimFarm farm/Map window activation.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

"""Source from Windows GDB after stopping at romaw_finder_main."""

import struct

import gdb


def value(expression):
    return int(gdb.parse_and_eval(expression))


def invoke(expression):
    gdb.execute("call (void)" + expression, to_string=True)


def main():
    memory = gdb.selected_inferior()
    offset = value("ROMlib_offset")
    scratch = value("(void *)_NewPtr_flags(128,0,1)")
    assert scratch

    def read(address, count):
        return bytes(memory.read_memory(address, count))

    def dereference(address):
        return offset + int.from_bytes(read(address, 4), "big")

    def region_bytes(handle):
        address = dereference(handle)
        return read(address, int.from_bytes(read(address, 2), "big"))

    def field(name):
        return value("(long long)&((WindowRecord*)0)->" + name)

    def create(bounds, title):
        memory.write_memory(scratch + 16, struct.pack(">4h", *bounds))
        memory.write_memory(scratch + 32, bytes([len(title)]) + title)
        window = value(
            "(void *)C_NewCWindow(0,(void *)%d,(void *)%d,1,8,(void *)-1,1,0)" % (scratch + 16, scratch + 32)
        )
        assert window
        return window

    def paint(window, color):
        invoke("C_SetPort((void *)%d)" % window)
        memory.write_memory(scratch + 48, struct.pack(">3H", *color))
        invoke("C_RGBForeColor((void *)%d)" % (scratch + 48))
        invoke("C_PaintRect((void *)%d)" % (window + 16))

    def pixel(x, y):
        invoke("C_GetCPixel(%d,%d,(void *)%d)" % (x, y, scratch + 64))
        return struct.unpack(">3H", read(scratch + 64, 6))

    saved_interrupts = value("_virtual_interrupts_blocked")
    saved_sr = value("cpu_state.sr")
    gdb.execute("set _virtual_interrupts_blocked = 1")
    gdb.execute("set cpu_state.sr = %d" % (saved_sr | 0x700))
    invoke("C_GetPort((void *)%d)" % scratch)
    saved_port = dereference(scratch)
    main_window = map_window = 0
    cases = 0
    try:
        main_window = create((80, 20, 220, 220), b"Exposure main")
        map_window = create((130, 150, 270, 300), b"Exposure map")
        main_vis = dereference(main_window + field("port.visRgn"))
        for altered in ("calculated", "full", "restricted", "empty"):
            invoke("C_BringToFront((void *)%d)" % main_window)
            paint(main_window, (0, 65535, 0))
            invoke("C_BringToFront((void *)%d)" % map_window)
            paint(map_window, (65535, 0, 0))
            invoke("C_SetPort((void *)%d)" % main_window)
            assert pixel(170, 80) == (65535, 0, 0), "overlapping window was not painted"
            # SimFarm's drawing helpers save, restrict and restore this public
            # region independently of activation. Exposure must use geometry.
            if altered != "calculated":
                bounds = {"full": (0, 0, 140, 200), "restricted": (20, 40, 120, 180), "empty": (0, 0, 0, 0)}[altered]
                memory.write_memory(scratch + 16, struct.pack(">4h", *bounds))
                invoke("C_RectRgn((void *)%d,(void *)%d)" % (main_vis, scratch + 16))
            invoke("C_BringToFront((void *)%d)" % main_window)
            assert value("(void *)C_FrontWindow()") == main_window
            assert pixel(170, 80) == (65535, 65535, 65535), (altered, "stale overlap pixels")
            assert pixel(10, 40) == (0, 65535, 0), (altered, "uncovered content was erased")
            update = dereference(main_window + field("updateRgn"))
            memory.write_memory(scratch + 16, struct.pack(">4h", 160, 190, 161, 191))
            assert value("(int)C_RectInRgn((void *)%d,(void *)%d)" % (scratch + 16, update))
            before = read(main_window + field("nextWindow"), 4)
            invoke("C_BringToFront((void *)%d)" % main_window)
            invoke("C_BringToFront(0)")
            assert read(main_window + field("nextWindow"), 4) == before
            cases += 1
        for transition in ("covered", "exposed", "hidden"):
            invoke("C_BringToFront((void *)%d)" % main_window)
            paint(main_window, (0, 65535, 0))
            if transition == "exposed":
                invoke("C_BringToFront((void *)%d)" % map_window)
                paint(map_window, (65535, 0, 0))
            invoke("C_SetPort((void *)%d)" % main_window)
            invoke("C_InvalRect((void *)%d)" % (main_window + 16))
            invoke("C_BeginUpdate((void *)%d)" % main_window)
            if transition == "covered":
                invoke("C_BringToFront((void *)%d)" % map_window)
                paint(map_window, (65535, 0, 0))
            elif transition == "exposed":
                invoke("C_BringToFront((void *)%d)" % main_window)
            else:
                invoke("C_ShowHide((void *)%d,0)" % main_window)
                paint(map_window, (65535, 0, 0))
            invoke("C_SetPort((void *)%d)" % main_window)
            invoke("C_EndUpdate((void *)%d)" % main_window)
            paint(main_window, (0, 65535, 0))
            expected = (0, 65535, 0) if transition == "exposed" else (65535, 0, 0)
            assert pixel(170, 80) == expected, (transition, "EndUpdate restored stale visibility")
            cases += 1
        invoke("C_ShowHide((void *)%d,1)" % main_window)
        invoke("C_BringToFront((void *)%d)" % main_window)
        invoke("C_SetPort((void *)%d)" % main_window)
        memory.write_memory(scratch + 16, struct.pack(">4h", 20, 40, 120, 180))
        invoke("C_RectRgn((void *)%d,(void *)%d)" % (main_vis, scratch + 16))
        saved_vis = region_bytes(main_vis)
        invoke("C_InvalRect((void *)%d)" % (main_window + 16))
        invoke("C_BeginUpdate((void *)%d)" % main_window)
        invoke("C_EndUpdate((void *)%d)" % map_window)
        invoke("C_EndUpdate((void *)%d)" % main_window)
        assert region_bytes(main_vis) == saved_vis, "EndUpdate of another window discarded the active snapshot"
        cases += 1
        invoke("C_CalcVis((void *)%d)" % main_window)
        invoke("C_InvalRect((void *)%d)" % (main_window + 16))
        invoke("C_BeginUpdate((void *)%d)" % main_window)
        invoke("C_SetOrigin(7,9)")
        invoke("C_EndUpdate((void *)%d)" % main_window)
        assert region_bytes(main_vis) == struct.pack(">H", 10) + read(main_window + 16, 8), "update origin was lost"
        invoke("C_SetOrigin(0,0)")
        cases += 1
        invoke("C_InvalRect((void *)%d)" % (main_window + 16))
        invoke("C_BeginUpdate((void *)%d)" % main_window)
        invoke("C_EndUpdate((void *)%d)" % main_window)
        invoke("C_BringToFront((void *)%d)" % map_window)
        paint(map_window, (65535, 0, 0))
        invoke("C_SetPort((void *)%d)" % main_window)
        invoke("C_EndUpdate((void *)%d)" % main_window)
        paint(main_window, (0, 65535, 0))
        assert pixel(170, 80) == (65535, 0, 0), "repeated EndUpdate restored a discarded snapshot"
        cases += 1
    finally:
        invoke("C_SetPort((void *)%d)" % saved_port)
        for window in (map_window, main_window):
            if window:
                invoke("C_DisposeWindow((void *)%d)" % window)
        invoke("DisposPtr((void *)%d)" % scratch)
        gdb.execute("set _virtual_interrupts_blocked = %d" % saved_interrupts)
        gdb.execute("set cpu_state.sr = %d" % saved_sr)
    print(
        "WINDOW_EXPOSURE_OK: %d visibility/update scenarios, pixels, origins and activation state" % cases, flush=True
    )


main()
