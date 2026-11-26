# +===================================================================
#   File:      pixpat_lifetime_probe.py
#
#   Summary:   Verify installed pixel-pattern disposal and replacement in Windows QuickDraw.
#
#   Classes:   None.
#
#   Functions: value, invoke, main (GDB Python entry point).
#
#   Origin:    Regression fixture for Kid Pix's disposed pattern preview handles.
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
    scratch = value("(void *)_NewPtr_flags(256,0,1)")
    assert scratch

    def read(address, size):
        return bytes(memory.read_memory(address, size))

    def pointer(address):
        address = int.from_bytes(read(address, 4), "big")
        return offset + address if address else 0

    def patterns(world):
        return tuple(pointer(world + field) for field in (32, 58, 62))

    saved_interrupts = value("_virtual_interrupts_blocked")
    saved_sr = value("cpu_state.sr")
    gdb.execute("set _virtual_interrupts_blocked = 1")
    gdb.execute("set cpu_state.sr = %d" % (saved_sr | 0x700))
    invoke("C_GetGWorld((void *)%d,(void *)%d)" % (scratch, scratch + 4))
    saved_port, saved_device = struct.unpack(">II", read(scratch, 8))
    memory.write_memory(scratch + 16, struct.pack(">4h", 0, 0, 16, 16))
    memory.write_memory(scratch + 32, b"\xff" * 8)
    worlds = []
    cases = 0
    try:
        for depth in (8, 32, 8):
            assert value("(int)C_NewGWorld((void *)%d,%d,(void *)%d,0,0,0)" % (scratch + 8, depth, scratch + 16)) == 0
            worlds.append(pointer(scratch + 8))
        untouched = patterns(worlds[2])
        for _ in range(32):
            pattern = value("(void *)C_NewPixPat()")
            assert pattern
            children = [pointer(pointer(pattern) + field) for field in (2, 6, 10)]
            children.append(pointer(pointer(children[0]) + 42))
            zone = value("(void *)HandleZone((void *)%d)" % pattern)
            for world in worlds[:2]:
                invoke("C_SetGWorld((void *)%d,0)" % world)
                invoke("C_PenPixPat((void *)%d)" % pattern)
                invoke("C_BackPixPat((void *)%d)" % pattern)
                invoke("ROMlib_fill_pixpat((void *)%d)" % pattern)
                assert patterns(world) == (pattern,) * 3
            # Kid Pix frees each preview without first replacing the installed pattern.
            invoke("C_DisposPixPat((void *)%d)" % pattern)
            dangling = any(patterns(world) != (0, 0, 0) for world in worlds[:2])
            if dangling:
                # Keep baseline cleanup from dereferencing the already freed handle.
                for world in worlds[:2]:
                    for field in (32, 58, 62):
                        memory.write_memory(world + field, bytes(4))
            assert not dangling, "disposed PixPat remains installed in a graphics port"
            assert patterns(worlds[2]) == untouched, "unrelated port patterns changed"
            # Freed master pointers form a list; HandleZone is not an allocation
            # predicate once their contents have become links to other masters.
            free_masters = set()
            master = pointer(zone + 8)
            while master:
                assert master not in free_masters, "free master-pointer list contains a cycle"
                free_masters.add(master)
                master = pointer(master)
            assert {pattern, *children} <= free_masters, "PixPat or subsidiary handle leaked"
            for world in worlds[:2]:
                invoke("C_SetGWorld((void *)%d,0)" % world)
                invoke("C_GetPenState((void *)%d)" % (scratch + 64))
                assert read(scratch + 74, 8) == b"\xff" * 8
                invoke("C_PenNormal()")
                invoke("C_BackPat((void *)%d)" % (scratch + 32))
                invoke("ROMlib_fill_pat((void *)%d)" % (scratch + 32))
                assert all(patterns(world)), "bit patterns were not recreated after disposal"
                invoke("C_RGBForeColor((void *)%d)" % (scratch + 40))
                invoke("C_PaintRect((void *)%d)" % (scratch + 16))
                invoke("C_GetCPixel(4,4,(void *)%d)" % (scratch + 48))
                assert read(scratch + 48, 6) == bytes(6), "replacement pen failed to paint black"
                cases += 1
    finally:
        invoke(
            "C_SetGWorld((void *)%d,(void *)%d)" % (offset + saved_port, offset + saved_device if saved_device else 0)
        )
        for world in worlds:
            invoke("C_DisposeGWorld((void *)%d)" % world)
        invoke("DisposPtr((void *)%d)" % scratch)
        gdb.execute("set _virtual_interrupts_blocked = %d" % saved_interrupts)
        gdb.execute("set cpu_state.sr = %d" % saved_sr)
    print("PIXPAT_LIFETIME_OK: %d shared-port disposal, replacement and pixel checks" % cases, flush=True)


main()
