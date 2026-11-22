# +===================================================================
#   File:      device_install_probe.py
#
#   Summary:   Run inside Windows GDB after Browser reaches WaitNextEvent.
#
#   Classes:   None declared.
#
#   Functions: value, read, write, allocate, pointer, result
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Run inside Windows GDB after Browser reaches WaitNextEvent."""

import gdb


def value(expression):
    return int(gdb.parse_and_eval(expression))


def read(address, size):
    return bytes(gdb.selected_inferior().read_memory(address, size))


def write(address, data):
    gdb.selected_inferior().write_memory(address, data)


def allocate(size):
    return value(f"_NewPtr_flags({size},1,1)")


base = value("ROMlib_offset")


def pointer(address):
    return base + int.from_bytes(read(address, 4), "big")


def result(expression, expected=0):
    actual = value(expression)
    assert actual == expected, (expression, actual, expected)


table = pointer(base + 0x11C)
assert int.from_bytes(read(base + 0x1D2, 2), "big") == 128
for invalid in (0, 1, -129, -32768):
    result(f"ROMlib_driver_install({invalid})", -21)
    result(f"ROMlib_driver_remove({invalid})", -21)

for unit in (48, 127):
    ref = -unit - 1
    slot = table + unit * 4
    assert read(slot, 4) == bytes(4)
    result(f"ROMlib_driver_install({ref})")
    handle = pointer(slot)
    entry = pointer(handle)
    assert read(entry + 4, 2) == b"\x00\x40"
    assert read(entry + 24, 2) == ref.to_bytes(2, "big", signed=True)
    write(entry + 8, b"\xa5" * 8)
    result(f"ROMlib_driver_install({ref})")
    assert pointer(slot) == handle and read(entry + 8, 8) == bytes(8)
    result(f"ROMlib_driver_remove({ref})")
    assert read(slot, 4) == bytes(4)
    result(f"ROMlib_driver_remove({ref})")

# Original 68K driver: Open/Close succeed; Control returns controlErr.
# Its resource ID is deliberately unrelated to its installed unit number.
ref = -49
result(f"ROMlib_driver_install({ref})")
handle = pointer(table + 48 * 4)
entry = pointer(handle)
driver = allocate(48)
name = b"\x0c.TestInstall"
write(driver, bytes(8) + b"\x00\x20\x00\x24\x00\x24\x00\x24\x00\x28" + name)
write(driver + 32, bytes.fromhex("70004e7570ef4e7570004e75"))
write(entry, (driver - base).to_bytes(4, "big"))
write(entry + 4, bytes(2))  # Direct code, rather than a DRVR Handle.
pb = allocate(128)
name_ptr = allocate(len(name))
write(name_ptr, name)
write(pb + 18, (name_ptr - base).to_bytes(4, "big"))
result(f"ROMlib_driveropen((ParmBlkPtr){pb},0)")
assert read(pb + 24, 2) == b"\xff\xcf"
result(f"ROMlib_driver_remove({ref})", -25)
result(f"ROMlib_dispatch((ParmBlkPtr){pb},0,Open,0)")
result(f"ROMlib_dispatch((ParmBlkPtr){pb},0,Ctl,0)", -17)
assert read(pb + 16, 2) == b"\xff\xef"
result(f"ROMlib_dispatch((ParmBlkPtr){pb},0,Close,0)")
assert pointer(table + 48 * 4) == handle
result(f"ROMlib_driver_remove({ref})")

# Failed Open must not mark the driver open or prevent removal.
result(f"ROMlib_driver_install({ref})")
entry = pointer(pointer(table + 48 * 4))
previous = driver
driver = allocate(48)
write(driver, read(previous, 48))
write(entry, (driver - base).to_bytes(4, "big"))
write(entry + 4, bytes(2))
write(driver + 32, bytes.fromhex("70e94e75"))  # openErr
result(f"ROMlib_driveropen((ParmBlkPtr){pb},0)", -23)
result(f"ROMlib_driver_remove({ref})")

# MapFBlock must reject host FCBs without returning fictitious disk blocks.
fcb_table = pointer(base + 0x34E)
fcb_length = int.from_bytes(read(base + 0x3F6, 2), "big")
fcb_limit = int.from_bytes(read(fcb_table, 2), "big")
host_ref = None
for ref in range(2, fcb_limit, fcb_length):
    if read(fcb_table + ref, 4) != bytes(4):
        volume = pointer(fcb_table + ref + 20)
        if value(f"((VCB*){volume})->vcbCTRef") == 0:
            host_ref = ref
            break
assert host_ref is not None
stack = allocate(4)
saved = [value(f"cpu_state.regs[{i}].ul.n") for i in range(16)]
try:
    for ref, expected in ((0, -51), (host_ref, -58)):
        for index, number in (
            (1, ref),
            (3, 0xDEAD),
            (4, 512),
            (5, 0),
            (6, 0xBEEF),
            (15, stack - base),
        ):
            gdb.execute(f"set cpu_state.regs[{index}].ul.n={number}")
        result("map_file_block(0,0)")
        assert value("(short)cpu_state.regs[0].ul.n") == expected
        assert value("cpu_state.regs[3].ul.n") == 0
        assert value("cpu_state.regs[6].ul.n") == 0
    # An original HFS FCB with one contiguous extent exercises byte/block units.
    fcb = value("ROMlib_getfreefcbp()")
    assert fcb
    original = read(fcb, fcb_length)
    volume = allocate(value("sizeof(VCB)"))

    def field(name):
        return value(f"(char*)&((VCB*){volume})->{name}-(char*){volume}")

    write(volume + field("vcbCTRef"), b"\x00\x01")
    write(volume + field("vcbAlBlkSiz"), (2048).to_bytes(4, "big"))
    write(volume + field("vcbAlBlSt"), (2).to_bytes(2, "big"))
    write(fcb, (123).to_bytes(4, "big"))
    write(fcb + 12, (4096).to_bytes(4, "big"))
    write(fcb + 20, (volume - base).to_bytes(4, "big"))
    write(fcb + 38, b"\x00\x03\x00\x02" + bytes(8))
    try:
        for offset, count, expected, block, contiguous in (
            (512, 1024, 0, 15, 1024),
            (512, 8192, 0, 15, 3584),
            (4096, 512, 17, 0, 0),
        ):
            for index, number in (
                (1, fcb - fcb_table),
                (4, count),
                (5, offset),
                (15, stack - base),
            ):
                gdb.execute(f"set cpu_state.regs[{index}].ul.n={number}")
            result("map_file_block(0,0)")
            assert value("(short)cpu_state.regs[0].ul.n") == expected
            assert value("cpu_state.regs[3].ul.n") == block
            assert value("cpu_state.regs[6].ul.n") == contiguous
    finally:
        write(fcb, original)

finally:
    for index, number in enumerate(saved):
        gdb.execute(f"set cpu_state.regs[{index}].ul.n={number}")

print("DRIVER_INSTALL_OPEN_CLOSE_REMOVE_AND_HOST_MAPPING_OK")
