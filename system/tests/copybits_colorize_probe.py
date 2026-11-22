# +===================================================================
#   File:      copybits_colorize_probe.py
#
#   Summary:   Verify indexed CopyBits RGB colorization in initialized Windows ROMAW.
#
#   Classes:   None.
#
#   Functions: value, invoke, main (GDB Python entry point).
#
#   Origin:    Regression fixture for SimFarm's offscreen About image transfer.
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

    def create(depth, slot):
        error = value("(int)C_NewGWorld((void *)%d,%d,(void *)%d,0,0,0)" % (scratch + slot, depth, scratch + 16))
        assert error == 0, error
        world = dereference(scratch + slot)
        pixmap = value("(void *)C_GetGWorldPixMap((void *)%d)" % world)
        assert value("(int)C_LockPixels((void *)%d)" % pixmap)
        header = dereference(pixmap)
        base = value("(void *)C_GetPixBaseAddr((void *)%d)" % pixmap)
        stride = int.from_bytes(read(header + 4, 2), "big") & 0x3FFF
        return world, pixmap, header, base, stride

    saved_interrupts = value("_virtual_interrupts_blocked")
    saved_sr = value("cpu_state.sr")
    gdb.execute("set _virtual_interrupts_blocked = 1")
    gdb.execute("set cpu_state.sr = %d" % (saved_sr | 0x700))
    invoke("C_GetGWorld((void *)%d,(void *)%d)" % (scratch, scratch + 4))
    saved_port, saved_device = struct.unpack(">II", read(scratch, 8))
    memory.write_memory(scratch + 16, struct.pack(">4h", 0, 0, 16, 16))
    cases = 0
    try:
        for source_depth in (4, 8):
            source = create(source_depth, 8)
            try:
                world, _, header, base, stride = source
                table = dereference(dereference(header + 42))
                table_size = 8 + (1 << source_depth) * 8
                original_table = read(table, table_size)
                palette = [struct.unpack_from(">3H", original_table, 10 + i * 8) for i in range(1 << source_depth)]
                source_data = bytes(range(256)) if source_depth == 8 else bytes(range(16)) * 8
                memory.write_memory(base, source_data)
                for destination_depth in (8, 32):
                    destination = create(destination_depth, 12)
                    try:
                        dest_world, _, _, dest_base, dest_stride = destination
                        invoke("C_SetGWorld((void *)%d,0)" % dest_world)
                        for foreground, background in (
                            ((0, 0, 0), (65535, 65535, 65535)),
                            ((0, 0, 0), (52428, 52428, 52428)),
                            ((65535, 0, 0), (0, 0, 65535)),
                            ((0, 0, 0), (65535, 65535, 65535)),
                        ):
                            memory.write_memory(scratch + 32, struct.pack(">6H", *foreground, *background))
                            invoke("C_RGBForeColor((void *)%d)" % (scratch + 32))
                            invoke("C_RGBBackColor((void *)%d)" % (scratch + 38))
                            invoke(
                                "C_CopyBits((void *)%d,(void *)%d,(void *)%d,(void *)%d,0,0)"
                                % (world + 2, dest_world + 2, scratch + 16, scratch + 16)
                            )
                            result = read(dest_base, dest_stride * 16)
                            for y in range(16):
                                for x in range(16):
                                    packed = source_data[y * stride + x * source_depth // 8]
                                    index = packed if source_depth == 8 else (packed >> (0 if x & 1 else 4)) & 15
                                    rgb = tuple(
                                        (s * b + (65535 - s) * f + 32767) // 65535
                                        for s, f, b in zip(palette[index], foreground, background)
                                    )
                                    if destination_depth == 32:
                                        actual = tuple(result[y * dest_stride + x * 4 + c] for c in (1, 2, 3))
                                        expected = tuple(c >> 8 for c in rgb)
                                    else:
                                        memory.write_memory(scratch + 48, struct.pack(">3H", *rgb))
                                        expected = value("(long)C_Color2Index((void *)%d)" % (scratch + 48))
                                        actual = result[y * dest_stride + x]
                                    assert actual == expected, (
                                        source_depth,
                                        destination_depth,
                                        foreground,
                                        background,
                                        x,
                                        y,
                                        rgb,
                                        actual,
                                        expected,
                                    )
                            assert read(table, table_size) == original_table, "source color table changed"
                            assert read(base, len(source_data)) == source_data, "source pixels changed"
                            invoke("C_GetForeColor((void *)%d)" % (scratch + 64))
                            invoke("C_GetBackColor((void *)%d)" % (scratch + 70))
                            assert read(scratch + 64, 12) == struct.pack(">6H", *foreground, *background)
                            cases += 1
                    finally:
                        invoke("C_UnlockPixels((void *)%d)" % destination[1])
                        invoke("C_SetGWorld((void *)%d,(void *)%d)" % (offset + saved_port, offset + saved_device))
                        invoke("C_DisposeGWorld((void *)%d)" % destination[0])
            finally:
                invoke("C_UnlockPixels((void *)%d)" % source[1])
                invoke("C_DisposeGWorld((void *)%d)" % source[0])
    finally:
        invoke("C_SetGWorld((void *)%d,(void *)%d)" % (offset + saved_port, offset + saved_device))
        invoke("DisposPtr((void *)%d)" % scratch)
        gdb.execute("set _virtual_interrupts_blocked = %d" % saved_interrupts)
        gdb.execute("set cpu_state.sr = %d" % saved_sr)
    print("COPYBITS_COLORIZE_OK: %d indexed RGB transfers and unchanged source/port state" % cases, flush=True)


main()
