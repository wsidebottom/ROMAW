# +===================================================================
#   File:      monochrome_mask_probe.py
#
#   Summary:   Verify one-bit icon mask polarity on indexed and direct-color destinations.
#
#   Classes:   None.
#
#   Functions: v, call, read; GDB Python entry point.
#
#   Origin:    Focused regression for classic Finder mask/paint icon rendering and CopyMask.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+
import gdb, struct

m = gdb.selected_inferior()


def v(e):
    return int(gdb.parse_and_eval(e))


def call(e):
    gdb.execute("call (void)" + e, to_string=True)


def read(p, n):
    return bytes(m.read_memory(p, n))


o = v("ROMlib_offset")
p = v("(void*)_NewPtr_flags(1024,0,1)")
assert p
saved_interrupts = v("_virtual_interrupts_blocked")
gdb.execute("set _virtual_interrupts_blocked=1")
call(f"C_GetGWorld((void*){p},(void*){p+4})")
saved = struct.unpack(">II", read(p, 8))
r = p + 16
m.write_memory(r, struct.pack(">4h", 0, 0, 16, 16))
mask = b"".join((b"\x0f\xf0" if 4 <= y < 12 else b"\0\0") for y in range(16))
icon = b"".join((b"\x06\x60" if 4 <= y < 12 else b"\0\0") for y in range(16))
for at, data in [(p + 128, mask), (p + 160, icon)]:
    m.write_memory(at, data)
for at, data in [(p + 64, p + 128), (p + 80, p + 160)]:
    m.write_memory(at, struct.pack(">IH4h", data - o, 2, 0, 0, 16, 16))
cases = 0
try:
    for depth in [8, 32]:
        assert v(f"(int)C_NewGWorld((void*){p+8},{depth},(void*){r},0,0,0)") == 0
        w = o + int.from_bytes(read(p + 8, 4), "big")
        pm = v(f"(void*)C_GetGWorldPixMap((void*){w})")
        assert v(f"(int)C_LockPixels((void*){pm})")
        hdr = o + int.from_bytes(read(pm, 4), "big")
        bits = v(f"(void*)C_GetPixBaseAddr((void*){pm})")
        stride = int.from_bytes(read(hdr + 4, 2), "big") & 0x3FFF
        call(f"C_SetGWorld((void*){w},0)")
        try:
            for swapped in [False, True]:
                fg = (65535,) * 3 if swapped else (0,) * 3
                bk = (0,) * 3 if swapped else (65535,) * 3
                m.write_memory(p + 32, struct.pack(">6H", *fg, *bk))
                call(f"C_RGBForeColor((void*){p+32})")
                call(f"C_RGBBackColor((void*){p+38})")
                for masked in [False, True]:
                    baseline = b"\x69" * (stride * 16) if depth == 8 else b"\0\x12\x34\x56" * (stride * 16 // 4)
                    m.write_memory(bits, baseline)
                    if masked:
                        call(f"C_CopyMask((void*){p+80},(void*){p+64},(void*){w+2},(void*){r},(void*){r},(void*){r})")
                    else:
                        call(f"C_CopyBits((void*){p+64},(void*){w+2},(void*){r},(void*){r},{1 if swapped else 3},0)")
                        call(f"C_CopyBits((void*){p+80},(void*){w+2},(void*){r},(void*){r},{3 if swapped else 1},0)")
                    out = read(bits, stride * 16)
                    for y in range(16):
                        for x in range(16):
                            j = y * stride + x * (depth // 8)
                            opaque = 4 <= x < 12 and 4 <= y < 12
                            one = bool(icon[y * 2 + x // 8] & (0x80 >> (x % 8)))
                            if not opaque:
                                expected = baseline[j : j + (depth // 8)]
                            else:
                                color = (fg if one else bk) if masked else ((0,) * 3 if one else (65535,) * 3)
                                if depth == 8:
                                    m.write_memory(p + 48, struct.pack(">3H", *color))
                                    expected = bytes([v(f"(long)C_Color2Index((void*){p+48})")])
                                else:
                                    expected = bytes([0] + [c >> 8 for c in color])
                            actual = out[j : j + (depth // 8)]
                            if depth == 32:
                                actual = actual[1:]
                                expected = expected[1:]
                            assert actual == expected, (depth, swapped, masked, x, y, actual.hex(), expected.hex())
                    assert read(p + 128, 32) == mask and read(p + 160, 32) == icon
                    cases += 1
        finally:
            call(f"C_UnlockPixels((void*){pm})")
            call(f"C_SetGWorld((void*){o+saved[0]},(void*){o+saved[1]})")
            call(f"C_DisposeGWorld((void*){w})")
finally:
    call(f"C_SetGWorld((void*){o+saved[0]},(void*){o+saved[1]})")
    call(f"DisposPtr((void*){p})")
    gdb.execute(f"set _virtual_interrupts_blocked={saved_interrupts}")
print("MONOCHROME_MASK_TRANSPARENCY_OK", cases)
