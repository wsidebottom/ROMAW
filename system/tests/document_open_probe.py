# +===================================================================
#   File:      document_open_probe.py
#
#   Summary:   Run inside Windows GDB after the staged Browser reaches its event loop.
#
#   Classes:   None declared.
#
#   Functions: value, allocate, read, write, pointer, success
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Run inside Windows GDB after the staged Browser reaches its event loop."""

import gdb


def value(expression):
    return int(gdb.parse_and_eval(expression))


def allocate(size):
    return value(f"_NewPtr_flags({size},0,1)")


def read(address, size):
    return bytes(gdb.selected_inferior().read_memory(address, size))


def write(address, data):
    gdb.selected_inferior().write_memory(address, data)


base = value("ROMlib_offset")


def pointer(address):
    return base + int.from_bytes(read(address, 4), "big")


def success(expression):
    result = value(expression)
    assert result == 0, (expression, result)


# These tables must all survive allocation/relocation during initialization.
info = pointer(base + 0x2B6)
for offset in (340, 380):
    zone = pointer(pointer(info + offset))
    for field in (0, 4, 8):
        assert int.from_bytes(read(zone + field, 4), "big")
        assert int.from_bytes(read(pointer(zone + field), 4), "big")

out = allocate(16)
write(out, b"\xa5" * 16)
success(f"C__AE_hdlr_table_alloc(16,0x80008,0,0,(HIDDEN_AE_hdlr_table_h *){out})")
assert read(out + 4, 12) == b"\xa5" * 12
gdb.execute(f"call DisposHandle((Handle){pointer(out)})")

proc = allocate(2)
write(proc, b"\x4e\x75")
refcon = allocate(4)
flag = allocate(1)
for descriptor in (0, 1):
    source_type = 0x54455330 + descriptor
    success(f"C_AEInstallCoercionHandler({source_type},0x54455354,(ProcPtr){proc},123,{descriptor},0)")
    write(out, b"\xa5" * 16)
    success(
        f"C_AEGetCoercionHandler({source_type},0x54455354,(HIDDEN_ProcPtr *){out},(int32 *){refcon},(Boolean *){flag},0)"
    )
    assert pointer(out) == proc and read(out + 4, 12) == b"\xa5" * 12
    assert read(refcon, 4) == (123).to_bytes(4, "big")
    assert read(flag, 1) == bytes([descriptor])

success(f"C_AEInstallEventHandler(0x54455354,0x74657374,(ProcPtr){proc},42,0)")
write(out, b"\xa5" * 16)
success(f"C_AEGetEventHandler(0x54455354,0x74657374,(HIDDEN_ProcPtr *){out},(int32 *){refcon},0)")
assert pointer(out) == proc and read(out + 4, 12) == b"\xa5" * 12
success(f"C_AEInstallSpecialHandler(0x54455354,(ProcPtr){proc},0)")
write(out, b"\xa5" * 16)
success(f"C_AEGetSpecialHandler(0x54455354,(HIDDEN_ProcPtr *){out},0)")
assert pointer(out) == proc and read(out + 4, 12) == b"\xa5" * 12

name = allocate(256)
path = DOCUMENT.replace("\\", ":").replace("/", ":")
if len(path) > 2 and path[1:3] == "::":
    path = path[:1] + path[2:]
encoded = path.encode("mac_roman")
assert len(encoded) < 256
write(name, bytes([len(encoded)]) + encoded)
spec = allocate(value("sizeof(FSSpec)"))
success(f"C_FSMakeFSSpec(0,0,(StringPtr){name},(FSSpec *){spec})")
success(f"C_NewAlias(0,(FSSpec *){spec},(AliasHandle *){out})")
alias = pointer(out)
alias_data = pointer(alias)
alias_size = value(f"GetHandleSize((Handle){alias})")
source, result, listing, record, event, target = [allocate(8) for _ in range(6)]
success(f"C_AECreateDesc(0x616c6973,(Ptr){alias_data},{alias_size},(AEDesc *){source})")
success(f"C_AECreateList(0,0,0,(AEDescList *){listing})")
success(f"C_AEPutDesc((AEDescList *){listing},1,(AEDesc *){source})")
success(f"C_AECreateList(0,0,1,(AEDescList *){record})")
success(f"C_AEPutKeyDesc((AERecord *){record},0x646f6320,(AEDesc *){source})")
psn = allocate(8)
success(f"C_GetCurrentProcess((ProcessSerialNumber *){psn})")
success(f"C_AECreateDesc(0x70736e20,(Ptr){psn},8,(AEDesc *){target})")
success(f"C_AECreateAppleEvent(0x54455354,0x74657374,(AEDesc *){target},-1,-1,(AppleEvent *){event})")
success(f"C_AEPutAttributeDesc((AppleEvent *){event},0x646f6320,(AEDesc *){source})")

payload = allocate(78)
kind, size, keyword = [allocate(4) for _ in range(3)]
getters = [
    f"C_AEGetNthPtr((AEDescList *){listing},1,0x66737320,(AEKeyword *){keyword},(DescType *){kind},(Ptr){payload},70,(Size *){size})",
    f"C_AEGetKeyPtr((AERecord *){record},0x646f6320,0x66737320,(DescType *){kind},(Ptr){payload},70,(Size *){size})",
    f"C_AEGetAttributePtr((AppleEvent *){event},0x646f6320,0x66737320,(DescType *){kind},(Ptr){payload},70,(Size *){size})",
]
expected = read(spec, 70)
for getter in getters:
    write(payload, b"\xa5" * 78)
    success(getter)
    assert read(kind, 4) == b"fss " and read(size, 4) == (70).to_bytes(4, "big")
    assert read(payload, 70) == expected
    assert read(payload + 70, 8) == b"\xa5" * 8

success(f"C_AECoercePtr(0x616c6973,(Ptr){alias_data},{alias_size},0x66737320,(AEDesc *){result})")
assert read(pointer(pointer(result + 4)), 70) == expected
gdb.execute(f"call C_AEDisposeDesc((AEDesc *){result})")
assert value(f"C_AECoercePtr(0x616c6973,(Ptr){alias_data},4,0x66737320,(AEDesc *){result})") != 0
assert read(result, 8) == b"null\0\0\0\0"
for desc in (source, listing, record, event, target):
    gdb.execute(f"call C_AEDisposeDesc((AEDesc *){desc})")
gdb.execute(f"call DisposHandle((Handle){alias})")
print("DOCUMENT_COERCION_AND_X64_OUTPUTS_OK")
