# +===================================================================
#   File:      build.py
#
#   Summary:   Build an original SDK-free classic PPC agent with a Linux cross compiler.
#
#   Classes:   None declared.
#
#   Functions: position, validate_layout, build
#
#   Origin:    Existing repository source in tools/classic-remote; original attribution and provenance comments are
#              preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Build an original SDK-free classic PPC agent with a Linux cross compiler.

The private pairing key and all generated output stay in build/. Dependencies:
powerpc-linux-gnu-gcc/binutils and pyelftools (Debian python3-pyelftools).
"""

import argparse
import ctypes
import json
from pathlib import Path
import secrets
import struct
import subprocess
import sys

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "synppc/tests"))
sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment

IMPORTS = [
    "GetCurrentProcess",
    "GetProcessInformation",
    "HSetVol",
    "FSOpen",
    "FSClose",
    "FSRead",
    "FSWrite",
    "SetEOF",
    "WaitNextEvent",
    "GetMainDevice",
    "PostEvent",
    "TickCount",
    "ExitToShell",
    "Microseconds",
]


def position(value):
    assert 0 <= value < 1 << 26
    return [0xA000 | (value >> 16), value & 65535]


def validate_layout(container):
    """Enforce the PEF layout subset emitted here before producing an app.

    Apple's PEF section rules forbid a zero-filled tail on read-only code.
    Executable data is writable and must have process-local sharing.
    """
    if len(container) < 40 or container[:12] != b"Joy!peffpwpc":
        raise ValueError("Invalid PPC PEF header")
    if struct.unpack_from(">I", container, 12)[0] != 1:
        raise ValueError("Invalid PEF format version")
    count, instantiated = struct.unpack_from(">HH", container, 32)
    header_size = 40 + count * 28
    if not 0 < instantiated < count or header_size > len(container):
        raise ValueError("Invalid PEF section table")
    for index in range(count):
        _, preferred, total, unpacked, packed, offset, kind, share, align, reserved = struct.unpack_from(
            ">6I4B", container, 40 + index * 28
        )
        if preferred or reserved or kind not in (0, 1, 4, 6):
            raise ValueError("Unsupported PEF section attributes")
        if unpacked != packed or offset < header_size or offset + packed > len(container):
            raise ValueError("PEF section payload outside container")
        if index < instantiated:
            if kind == 4 or total < unpacked or share != 1 or align != 4 or offset % 16:
                raise ValueError("Invalid instantiated PEF section")
            if kind == 0 and total != unpacked:
                raise ValueError("Read-only code cannot contain zero-initialized data")
        elif kind != 4:
            raise ValueError("Noninstantiated section is not the loader")


def build(destination, diagnostic=False, tcp=False):
    destination.mkdir(parents=True, exist_ok=True)
    application_name = (
        "Classic Remote TCP"
        if tcp
        else (
            "Classic Remote Screen Check"
            if diagnostic == 2
            else "Classic Remote Check" if diagnostic else "Classic Remote"
        )
    )
    source = Path(__file__).parent
    imports_list = IMPORTS + (["OpenDriver", "PBControlSync", "PBControlAsync"] if tcp else [])
    crypto = source / "third_party/monocypher"
    for compiler, name in [
        ("gcc", "monocypher.so"),
        ("x86_64-w64-mingw32-gcc", "monocypher.dll"),
    ]:
        subprocess.run(
            [
                compiler,
                "-shared",
                "-fPIC",
                "-O2",
                "-static-libgcc",
                str(crypto / "monocypher.c"),
                str(crypto / "monocypher-ed25519.c"),
                "-o",
                str(destination / name),
            ],
            check=True,
        )
    library = ctypes.CDLL(str(destination / "monocypher.so"))
    keyfile = destination / "controller.json"
    config = (
        json.loads(keyfile.read_text())
        if keyfile.exists()
        else {
            "server": "root@10.0.0.114",
            "folder": "/srv/storage/From ROMAW/" + application_name,
        }
    )
    if "private_key" not in config:
        private, public = ctypes.create_string_buffer(64), ctypes.create_string_buffer(32)
        seed = ctypes.create_string_buffer(secrets.token_bytes(32), 32)
        library.crypto_ed25519_key_pair(private, public, seed)
        config["private_key"], config["public_key"] = (
            private.raw.hex(),
            public.raw.hex(),
        )
        config.pop("key", None)
    if tcp:
        config["transport"] = "tcp"
        config.setdefault("host", "10.0.0.51")
        config.setdefault("port", 6804)
    keyfile.write_text(json.dumps(config, indent=2) + "\n")
    keyfile.chmod(0o600)
    (destination / "pairing.h").write_text(
        "#define REMOTE_PUBLIC_KEY {" + ",".join(str(n) for n in bytes.fromhex(config["public_key"])) + "}\n"
    )
    subprocess.run(
        [
            "powerpc-linux-gnu-gcc",
            "-m32",
            "-mbig-endian",
            "-mcpu=603e",
            "-mno-altivec",
            "-mno-vsx",
            "-msdata=none",
            "-fPIC",
            "-ffunction-sections",
            "-fdata-sections",
            "-ffreestanding",
            "-fno-builtin",
            "-fno-stack-protector",
            "-fno-asynchronous-unwind-tables",
            "-fno-unwind-tables",
            "-fno-delete-null-pointer-checks",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DREMOTE_DIAGNOSTIC=" + str(int(diagnostic)),
            "-DREMOTE_TCP=" + str(int(tcp)),
            "-nostdlib",
            "-static",
            "-Wl,--gc-sections,--emit-relocs,--build-id=none,-T," + str(source / "agent.ld"),
            "-I" + str(destination),
            "-I" + str(crypto),
            str(source / "agent.c"),
            str(source / "call.S"),
            str(crypto / "monocypher.c"),
            str(crypto / "monocypher-ed25519.c"),
            "-o",
            str(destination / "agent.elf"),
        ],
        check=True,
    )
    with (destination / "agent.elf").open("rb") as stream:
        elf = ELFFile(stream)
        image = elf.get_section_by_name(".image").data()
        memory_size = max(s["sh_addr"] + s["sh_size"] for s in elf.iter_sections() if s["sh_flags"] & 2)
        symbols = elf.get_section_by_name(".symtab")
        main = symbols.get_symbol_by_name("remote_main")[0]["st_value"]
        imports = symbols.get_symbol_by_name("remote_imports")[0]["st_value"]
        relocations = []
        for section in elf.iter_sections():
            if section["sh_type"] != "SHT_RELA":
                continue
            for r in section.iter_relocations():
                kind = r["r_info_type"]
                if kind == 1:  # R_PPC_ADDR32: linked offset becomes code address.
                    relocations += position(r["r_offset"]) + [0x4000]
                elif kind not in (10, 18, 23, 26, 249, 250, 251, 252):
                    # Linked REL/PLTREL branches and PC-relative GOT offsets
                    # remain invariant when the complete image moves.
                    raise ValueError(f"Unsupported PPC ELF relocation {kind}")
        relocations += position(imports) + [0x4A00 + len(imports_list) - 1]
    first_ops = struct.pack(f">{len(relocations)}H", *relocations)
    strings = bytearray(b"InterfaceLib\0")
    symbols = bytearray()
    for name in imports_list:
        symbols += struct.pack(">I", 0x02000000 | len(strings))
        strings += name.encode() + b"\0"
    instructions = 56 + 24 + len(symbols) + 24
    op_bytes = first_ops + struct.pack(">H", 0x4600)
    string_offset = instructions + len(op_bytes)
    hash_offset = (string_offset + len(strings) + 3) & ~3
    loader = struct.pack(
        ">14I",
        1,
        0,
        0xFFFFFFFF,
        0,
        0xFFFFFFFF,
        0,
        1,
        len(imports_list),
        2,
        instructions,
        string_offset,
        hash_offset,
        0,
        0,
    )
    loader += struct.pack(">6I", 0, 0, 0, len(imports_list), 0, 0) + symbols
    loader += struct.pack(">HHII", 0, 0, len(relocations), 0)
    loader += struct.pack(">HHII", 1, 0, 1, len(first_ops))
    loader += op_bytes + strings
    loader += bytes(hash_offset - len(loader)) + bytes(4)
    payloads = [
        # The GCC PIC image contains writable GOT/global data and BSS.
        # PEF read-only code sections cannot have zero-initialized extensions.
        # Executable data (kind 6) explicitly supports code plus writable data.
        (image, memory_size, 6),
        (struct.pack(">II", main, 0), 8, 1),
        (loader, len(loader), 4),
    ]
    header = b"Joy!peffpwpc" + struct.pack(">5IHHI", 1, 0, 0, 0, 0, 3, 2, 0)
    offset = 128
    descriptors, contents = bytearray(), bytearray()
    for payload, extent, kind in payloads:
        descriptors += struct.pack(
            ">6I4B",
            0xFFFFFFFF,
            0,
            extent,
            len(payload),
            len(payload),
            offset,
            kind,
            1 if kind != 4 else 0,
            4,
            0,
        )
        contents += payload
        padding = (-len(contents)) & 15
        contents += bytes(padding)
        offset += len(payload) + padding
    pef = header + descriptors + bytes(4) + contents
    validate_layout(pef)
    write_fragment(destination, application_name, pef, 1)
    sidecar = (destination / ".AppleDouble" / application_name).read_bytes()
    entries = {}
    for i in range(struct.unpack_from(">H", sidecar, 24)[0]):
        kind, offset, length = struct.unpack_from(">III", sidecar, 26 + i * 12)
        entries[kind] = sidecar[offset : offset + length]
    resource = bytearray(entries[2])
    data_offset, map_offset = struct.unpack_from(">II", resource)
    types = map_offset + struct.unpack_from(">H", resource, map_offset + 24)[0]
    count = struct.unpack_from(">H", resource, types)[0] + 1
    for index in range(count):
        entry = types + 2 + index * 8
        if resource[entry : entry + 4] == b"SIZE":
            ref = types + struct.unpack_from(">H", resource, entry + 6)[0]
            payload = data_offset + (struct.unpack_from(">I", resource, ref + 4)[0] & 0xFFFFFF) + 4
            struct.pack_into(">H", resource, payload, 0x1480)
    header = struct.pack(">II16sH", 0x51607, 0x20000, b"Mac OS X        ", 2)
    header += struct.pack(">III", 9, 50, 32)
    header += struct.pack(">III", 2, 82, len(resource))
    finder_info = bytearray(entries[9])
    # Do not share the generic probe application's signature. Distinct check
    # signatures also allow a diagnostic to run beside the persistent agent.
    finder_info[4:8] = b"CMRn" if tcp else {0: b"CMRt", 1: b"CMRc", 2: b"CMRs"}[int(diagnostic)]
    (destination / ("._" + application_name)).write_bytes(header + finder_info + resource)
    for filename, length in [("Remote Command", 512), ("Remote Response", 48)]:
        if not (destination / filename).exists():
            (destination / filename).write_bytes(bytes(length))
        # AFP's Unix user can differ from the SMB mount's fixed owner. These
        # deliberately shared mailboxes must be writable by the AFP client.
        (destination / filename).chmod(0o666)
    print(f"Built {application_name}: {len(image)} initialized bytes, {memory_size} RAM bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--diagnostic", action="store_true")
    modes.add_argument("--screen-check", action="store_true")
    modes.add_argument("--tcp", action="store_true")
    args = parser.parse_args()
    build(
        args.destination.resolve(),
        2 if args.screen_check else int(args.diagnostic),
        args.tcp,
    )
