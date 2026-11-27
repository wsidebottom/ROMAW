# +===================================================================
#   File:      launch_desk_accessory_probe.py
#
#   Summary:   Run inside Windows GDB at the private probe's ExitToShell breakpoint.
#
#   Classes:   None declared.
#
#   Functions: value, allocate, pascal, dispatch
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Run inside Windows GDB at the private probe's ExitToShell breakpoint."""

import gdb
import struct

memory = gdb.selected_inferior()
base = int(gdb.parse_and_eval("ROMlib_offset"))


def value(expression):
    return int(gdb.parse_and_eval(expression))


def allocate(size):
    return value(f"(void *)_NewPtr_flags({size},1,1)")


def pascal(text):
    data = text.encode("ascii")
    pointer = allocate(len(data) + 1)
    memory.write_memory(pointer, bytes([len(data)]) + data)
    return pointer


assert value("(int)ROMlib_osdispatch_available(0x36)")
# Native calls made between dispatches can reenter guest code. Reserve room
# below the synthetic Pascal frame for that downward-growing guest stack.
stack = allocate(8192) + 4096
saved = [value(f"cpu_state.regs[{i}].ul.n") for i in range(16)]


def dispatch(file, name, expected):
    frame = (
        struct.pack(
            ">IHIIH",
            0x1234ABCD,
            0x36,
            name - base if name else 0,
            file - base if file else 0,
            0xA55A,
        )
        + b"guard"
    )
    memory.write_memory(stack, frame)
    gdb.execute(f"set cpu_state.regs[15].ul.n={stack - base}", to_string=True)
    returned = value("(unsigned)_OSDispatch(0,0)")
    assert returned == 0x1234ABCD
    assert value("cpu_state.regs[15].ul.n") == stack - base + 14
    result = bytes(memory.read_memory(stack + 14, 2))
    assert int.from_bytes(result, "big", signed=True) == expected, result.hex()
    assert bytes(memory.read_memory(stack + 16, 5)) == b"guard"


try:
    queue = value("(void *)GetVCBQHdr()")
    volume = int.from_bytes(memory.read_memory(queue + 2, 4), "big")
    volumes = []
    while volume:
        record = bytes(memory.read_memory(base + volume, 80))
        volumes.append(record[45 : 45 + record[44]])
        assert len(volumes) < 32, "volume queue cycle"
        volume = int.from_bytes(record[:4], "big")
    assert len(volumes) == 2 and b"ROMAW" in volumes, volumes
    print("SKIP_DRIVES_PRESERVES_PROFILE_VOLUME_OK")
    assert value("(int)ROMlib_osdispatch_available(1)")
    task_info = allocate(24)
    for slots, output in ((0, 0), (-1, task_info + 2), (1, task_info + 2)):
        memory.write_memory(task_info, b"\xa5" * 24)
        frame = struct.pack(">IHHIH", 0x1234ABCD, 1, slots & 0xFFFF, output - base if output else 0, 0xA55A) + b"guard"
        memory.write_memory(stack, frame)
        gdb.execute(f"set cpu_state.regs[15].ul.n={stack - base}", to_string=True)
        assert value("(unsigned)_OSDispatch(0,0)") == 0x1234ABCD
        assert value("cpu_state.regs[15].ul.n") == stack - base + 12
        assert bytes(memory.read_memory(stack + 12, 2)) == b"\0\1"
        assert bytes(memory.read_memory(stack + 14, 5)) == b"guard"
        actual = bytes(memory.read_memory(task_info, 24))
        if slots == 1:
            assert actual[:2] == b"\xa5" * 2 and actual[12:] == b"\xa5" * 12
            assert actual[4:8] == b"FwPr", actual.hex()
        else:
            assert actual == b"\xa5" * 24
    print("GET_ACTIVE_TASK_INFO_STACK_AND_BOUNDS_OK")
    descriptor = int.from_bytes(actual[2:4], "big")
    assert value("(int)ROMlib_osdispatch_available(0x13)")
    memory.write_memory(stack, struct.pack(">IHH", 0x1234ABCD, 0x13, 0xA55A) + b"guard")
    gdb.execute(f"set cpu_state.regs[15].ul.n={stack - base}", to_string=True)
    assert value("(unsigned)_OSDispatch(0,0)") == 0x1234ABCD
    assert value("cpu_state.regs[15].ul.n") == stack - base + 6
    assert int.from_bytes(memory.read_memory(stack + 6, 2), "big") == descriptor
    assert bytes(memory.read_memory(stack + 8, 5)) == b"guard"
    process_info = allocate(84)
    saved_zone = bytes(memory.read_memory(base + 0x118, 4))
    for pid, output, expected in (
        (-1, process_info + 2, -600),
        (descriptor, 0, -50),
        (descriptor, process_info + 2, 0),
    ):
        memory.write_memory(process_info, b"\xa5" * 84)
        frame = (
            struct.pack(">IHIHI", 0x1234ABCD, 0x17, output - base if output else 0, pid & 0xFFFF, 0xA55AA55A) + b"guard"
        )
        memory.write_memory(stack, frame)
        gdb.execute(f"set cpu_state.regs[15].ul.n={stack - base}", to_string=True)
        assert value("(unsigned)_OSDispatch(0,0)") == 0x1234ABCD
        assert value("cpu_state.regs[15].ul.n") == stack - base + 12
        assert int.from_bytes(memory.read_memory(stack + 12, 4), "big", signed=True) == expected
        assert bytes(memory.read_memory(stack + 16, 5)) == b"guard"
        assert bytes(memory.read_memory(base + 0x118, 4)) == saved_zone
        record = bytes(memory.read_memory(process_info, 84))
        if expected == 0:
            assert record[:2] == b"\xa5" * 2 and record[82:] == b"\xa5" * 2
            assert record[4:6] == descriptor.to_bytes(2, "big")
            assert record[6:14] == b"APPLFwPr", record.hex()
            assert record[18:22] == bytes(memory.read_memory(base + 0x2AA, 4)), record.hex()
            assert record[48:54] == b"\x05Probe", record.hex()
        else:
            assert record == b"\xa5" * 84
    print("GET_LEGACY_PROCESS_INFO_STACK_AND_BOUNDS_OK")
    assert value("(int)ROMlib_osdispatch_available(2)")
    switch_info = allocate(16)
    for output in (switch_info + 4, 0):
        memory.write_memory(switch_info, b"\xa5" * 16)
        frame = struct.pack(">IHI", 0x1234ABCD, 2, output - base if output else 0) + b"guard"
        memory.write_memory(stack, frame)
        gdb.execute(f"set cpu_state.regs[15].ul.n={stack - base}", to_string=True)
        assert value("(unsigned)_OSDispatch(0,0)") == 0x1234ABCD
        assert value("cpu_state.regs[15].ul.n") == stack - base + 10
        assert bytes(memory.read_memory(stack + 10, 5)) == b"guard"
        expected = b"\xa5" * 4 + b"\0" * 8 + b"\xa5" * 4 if output else b"\xa5" * 16
        assert bytes(memory.read_memory(switch_info, 16)) == expected
    print("GET_SWITCH_INFO_STACK_AND_BOUNDS_OK")
    assert value("(int)ROMlib_osdispatch_available(8)")
    # The tiny probe initializes QuickDraw only; layer clipping also requires
    # Window Manager ports and its gray region in the application's context.
    gdb.execute("call C_InitWindows()", to_string=True)
    saved_layer = value("(void *)ROMlib_layer_current()")
    for expression in ("ROMlib_layer_root()", "ROMlib_layer_application()", "0"):
        layer = value(f"(void *){expression}")
        frame = struct.pack(">IHII", 0x1234ABCD, 8, 0, layer - base if layer else 0) + b"guard"
        memory.write_memory(stack, frame)
        gdb.execute(f"set cpu_state.regs[15].ul.n={stack - base}", to_string=True)
        assert value("(unsigned)_OSDispatch(0,0)") == 0x1234ABCD
        assert value("cpu_state.regs[15].ul.n") == stack - base + 14
        assert bytes(memory.read_memory(stack + 14, 5)) == b"guard"
        assert value("(void *)ROMlib_layer_current()") == saved_layer
    print("LCLIP_ABOVE_STACK_AND_LAYER_OK")
    assert value("(int)ROMlib_osdispatch_available(0x14)")
    region = value("(void *)C_NewRgn()")
    gdb.execute(f"call C_SetRectRgn((void *){region},1,2,30,40)", to_string=True)
    original_master = bytes(memory.read_memory(region, 4))
    region_data = base + int.from_bytes(original_master, "big")
    original_region = bytes(memory.read_memory(region_data, 10))
    layer = value("(void *)ROMlib_layer_application()")
    output = allocate(12)
    creator = value("ROMlib_creator")
    flags = value("size_info.size_flags")
    try:
        gdb.execute("set ROMlib_creator=0x4d414353", to_string=True)
        gdb.execute(f"set size_info.size_flags={flags & ~0x40}", to_string=True)
        memory.write_memory(stack, struct.pack(">IHI", 0x1234ABCD, 0x14, region - base) + b"guard")
        gdb.execute(f"set cpu_state.regs[15].ul.n={stack - base}", to_string=True)
        assert value("(unsigned)_OSDispatch(0,0)") == 0x1234ABCD
        assert value("cpu_state.regs[15].ul.n") == stack - base + 10
        assert bytes(memory.read_memory(stack + 10, 5)) == b"guard"
        assert bytes(memory.read_memory(region, 4)) == original_master
        assert bytes(memory.read_memory(region_data, 10)) == original_region
        update = base + int.from_bytes(memory.read_memory(layer + 122, 4), "big")
        assert value(f"(int)C_EqualRgn((void *){region},(void *){update})")
        # The later contract has a pointer output and a two-byte status slot.
        gdb.execute(f"set size_info.size_flags={flags | 0x40}", to_string=True)
        memory.write_memory(output, b"\xa5" * 12)
        memory.write_memory(stack, struct.pack(">IHIH", 0x1234ABCD, 0x14, output + 4 - base, 0xA55A) + b"guard")
        gdb.execute(f"set cpu_state.regs[15].ul.n={stack - base}", to_string=True)
        assert value("(unsigned)_OSDispatch(0,0)") == 0x1234ABCD
        assert value("cpu_state.regs[15].ul.n") == stack - base + 10
        assert bytes(memory.read_memory(stack + 10, 2)) == b"\0\0"
        assert bytes(memory.read_memory(stack + 12, 5)) == b"guard"
        assert bytes(memory.read_memory(output, 4)) == b"\xa5" * 4
        assert bytes(memory.read_memory(output + 4, 4)) == (layer - base).to_bytes(4, "big")
        assert bytes(memory.read_memory(output + 8, 4)) == b"\xa5" * 4
    finally:
        gdb.execute(f"set ROMlib_creator={creator}", to_string=True)
        gdb.execute(f"set size_info.size_flags={flags}", to_string=True)
    gdb.execute(f"call C_DisposeRgn((void *){region})", to_string=True)
    assert value(f"(int)GetPtrSize((void *){layer})") >= 156
    assert value("(void *)ROMlib_layer_application()") == layer
    print("DESKTOP_OWNER_LEGACY_AND_MODERN_ABI_OK")
    dispatch(0, 0, -50)
    dispatch(0, pascal(".Missing"), -192)
    current = value("(int)C_CurResFile()")
    dispatch(0, pascal(".SelectorA"), 0)
    assert value("(void *)GetDCtlEntry(-18)"), "driver was not installed"
    closed = value("(int)CloseDriver(-18)")
    assert closed == 0, ("close driver", closed)
    spec = allocate(80)
    filename = pascal("Accessory")
    volume = value("ROMlib_exevrefnum")
    located = value(f"(int)C_FSMakeFSSpec({volume},0,(void *){filename},(void *){spec})")
    assert located == 0, ("locate fixture", located)
    dispatch(spec, pascal(".Missing"), -192)
    restored = value("(int)C_CurResFile()")
    assert restored == current, ("resource file", current, restored)
    gdb.set_convenience_variable("accessory_file", gdb.Value(spec))
    gdb.set_convenience_variable("accessory_name", gdb.Value(pascal(".SelectorB")))
    print("LAUNCH_DESK_ACCESSORY_STACK_AND_RESOURCE_OK")
finally:
    for i, number in enumerate(saved):
        gdb.execute(f"set cpu_state.regs[{i}].ul.n={number}", to_string=True)
