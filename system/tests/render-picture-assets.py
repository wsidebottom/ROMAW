# +===================================================================
#   File:      render-picture-assets.py
#
#   Summary:   Render and verify PNG-backed picture resources inside initialized ROMAW under Windows GDB.
#
#   Classes:   None.
#
#   Functions: value, invoke, render_picture, main.
#
#   Origin:    Executor picture asset conversion and raster round-trip regression fixture.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

"""Source at romaw_finder_main in Windows GDB with Pillow available to its Python; uses a private GWorld."""

from pathlib import Path
import struct
import sys

import gdb

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import pictures


def value(expression):
    return int(gdb.parse_and_eval(expression))


def invoke(expression):
    gdb.execute("call (void)" + expression, to_string=True)


def render_picture(data):
    top, left, bottom, right = struct.unpack_from(">4h", data, 2)
    width, height = right - left, bottom - top
    assert 1 <= width <= 4095 and 1 <= height <= 4095
    memory = gdb.selected_inferior()
    offset = value("ROMlib_offset")
    scratch = value("(void *)_NewPtr_flags(128,0,1)")
    assert scratch
    saved_interrupts = value("_virtual_interrupts_blocked")
    saved_sr = value("cpu_state.sr")
    gdb.execute("set _virtual_interrupts_blocked = 1")
    gdb.execute("set cpu_state.sr = %d" % (saved_sr | 0x700))
    picture = world = 0
    try:
        invoke("C_GetGWorld((void *)%d,(void *)%d)" % (scratch, scratch + 4))
        old_port, old_device = struct.unpack(">II", bytes(memory.read_memory(scratch, 8)))
        memory.write_memory(scratch + 16, struct.pack(">4h", 0, 0, height, width))
        error = value("(int)C_NewGWorld((void *)%d,32,(void *)%d,0,0,0)" % (scratch + 8, scratch + 16))
        assert error == 0, error
        world = offset + int.from_bytes(bytes(memory.read_memory(scratch + 8, 4)), "big")
        invoke("C_SetGWorld((void *)%d,0)" % world)
        memory.write_memory(scratch + 32, b"\xff" * 6)
        invoke("C_RGBBackColor((void *)%d)" % (scratch + 32))
        memory.write_memory(scratch + 32, bytes(6))
        invoke("C_RGBForeColor((void *)%d)" % (scratch + 32))
        invoke("C_EraseRect((void *)%d)" % (scratch + 16))
        invoke("C_PenNormal()")
        invoke("C_TextFont(0)")
        invoke("C_TextSize(12)")
        picture = value("(void *)_NewHandle_flags(%d,0,0)" % len(data))
        assert picture
        address = offset + int.from_bytes(bytes(memory.read_memory(picture, 4)), "big")
        memory.write_memory(address, data)
        invoke("C_DrawPicture((void *)%d,(void *)%d)" % (picture, scratch + 16))
        pixmap = value("(void *)C_GetGWorldPixMap((void *)%d)" % world)
        assert value("(int)C_LockPixels((void *)%d)" % pixmap)
        try:
            base = value("(void *)C_GetPixBaseAddr((void *)%d)" % pixmap)
            address = offset + int.from_bytes(bytes(memory.read_memory(pixmap, 4)), "big")
            stride = int.from_bytes(bytes(memory.read_memory(address + 4, 2)), "big") & 0x3FFF
            raw = bytes(memory.read_memory(base, stride * height))
            pixels = bytes(
                raw[y * stride + x * 4 + channel] for y in range(height) for x in range(width) for channel in (1, 2, 3)
            )
        finally:
            invoke("C_UnlockPixels((void *)%d)" % pixmap)
        return width, height, pixels
    finally:
        if picture:
            invoke("DisposHandle((void *)%d)" % picture)
        if world:
            invoke(
                "C_SetGWorld((void *)%d,(void *)%d)"
                % (offset + old_port if old_port else 0, offset + old_device if old_device else 0)
            )
            invoke("C_DisposeGWorld((void *)%d)" % world)
        invoke("DisposPtr((void *)%d)" % scratch)
        gdb.execute("set _virtual_interrupts_blocked = %d" % saved_interrupts)
        gdb.execute("set cpu_state.sr = %d" % saved_sr)


def main():
    from resource_catalog import load_scope

    count = 0
    for scope in ("system", "finder"):
        catalog, _ = load_scope(ROOT / ("finder/resources" if scope == "finder" else "system/resources/system"), scope)
        for entry in catalog["resources"]:
            if entry["type"] != "PICT":
                continue
            source = (ROOT / "bitmaps" / entry["file"]).read_bytes()
            generated = pictures.resource(source, *entry["picture_origin"])
            assert render_picture(generated) == pictures.read_image(source), entry["file"]
            count += 1
    print("PASS: %d PNG-backed PICT resources render pixel-for-pixel in ROMAW" % count, flush=True)


if __name__ == "__main__":
    main()
