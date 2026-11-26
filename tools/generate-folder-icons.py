# +===================================================================
#   File:      generate-folder-icons.py
#
#   Summary:   Generate shared Finder icon tables and Windows disk-image file icons.
#
#   Classes:   None declared.
#
#   Functions: family_resource, generate, generate_windows
#
#   Origin:    Existing repository source in tools; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Generate the classic special-folder and volume icon tables for ROMAW."""

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import mac_icons

IMAGE_FILE_TYPES = (
    ("hfs", "file_hfs.icns", 13, (".hfs", ".hda", ".hfv")),
    ("dmg", "file_dmg.icns", 14, (".dmg",)),
    ("iso", "file_iso.icns", 17, (".iso", ".cdr")),
    ("img", "file_img.icns", 18, (".img", ".image")),
    ("toast", "file_toast.icns", 19, (".toast",)),
    ("dsk", "file_dsk.icns", 20, (".dsk",)),
)

# Stable Windows RT_GROUP_ICON IDs; file associations use negative resource IDs.
WINDOWS_IMAGE_ICON_IDS = {"hfs": 101, "dmg": 102, "iso": 103, "img": 104, "toast": 105, "dsk": 106}

# Used only after custom, bundle and Desktop database artwork has failed.
GENERIC_FILE_TYPES = (
    ("dfil", "type_dfil_2.icns"),
    ("DFIL", "type_dfil_1.icns"),
    ("FNDR", "mac.icns"),
    ("ZSYS", "mac.icns"),
    ("APPL", "type_APPL.icns"),
    ("TEXT", "-4000_genericDocumentIconResource.icns"),
    ("PICT", "type_PICT.icns"),
    ("kfil", "type_kfil.icns"),
    ("FFIL", "type_ffil_1.icns"),
    ("ffil", "type_ffil_2.icns"),
    ("sfil", "type_sfil.icns"),
    ("tfil", "type_tfil.icns"),
    ("INIT", "type_INIT.icns"),
    ("shlb", "type_INIT.icns"),
)


def family_resource(data, kind):
    payload = mac_icons.resource(data, kind)
    if kind in (b"ICN#", b"ics#"):
        size = 32 if kind == b"ICN#" else 16
        # A classic family has one shared mask. Edited ICOs can have a smaller
        # mono silhouette than their colour art; use the displayed colour frame.
        frame = mac_icons.best_frame(mac_icons.decode(data), size, 8)
        opacity = mac_icons.classic_frame(frame, size, 8)["opacity"]
        payload = payload[: size * size // 8] + opacity
    return payload


def generate(output):
    output.mkdir(parents=True, exist_ok=True)
    kinds = (b"ICN#", b"ics#", b"icl4", b"icl8", b"ics4", b"ics8")
    folders = ["/* Generated from shared bitmaps/; do not edit. */"]
    folders.append("static const FinderIcon finder_special_icons[] = {")
    filenames = (
        "-3983_systemFolderIconResource.icns",
        "-3982_appleMenuFolderIconResource.icns",
        "-3976_controlPanelFolderIconResource.icns",
        "-3974_preferencesFolderIconResource.icns",
        "-3968_fontsFolderIconResource.icns",
        "-3973_extensionsFolderIconResource.icns",
        "-3995_genericHardDiskIconResource.icns",
        "-3972_FileServerIconResource.icns",
        "-3998_floppyIconResource.icns",
        "-3987_CDROMIconResource.icns",
        "ROMAW_volume.icns",
        "hfs_volume.icns",
        "dmg_volume.icns",
        "file_hfs.icns",
        "file_dmg.icns",
        "-3997_openFolderIconResource.icns",
        "-3992_desktopIconResource.icns",
        "file_iso.icns",
        "file_img.icns",
        "file_toast.icns",
        "file_dsk.icns",
    )
    for filename in filenames + tuple(filename for _, filename in GENERIC_FILE_TYPES):
        data = (ROOT / "bitmaps" / filename).read_bytes()
        folders.append("  {")
        for kind in kinds:
            payload = family_resource(data, kind)
            folders.append("    {" + ",".join(str(x) for x in payload) + "},")
        folders.append("    63,")
        folders.append("  },")
    folders.append("};")
    folders.append("static const struct { const char *extension; unsigned icon; } finder_image_extensions[] = {")
    for _, _, index, extensions in IMAGE_FILE_TYPES:
        for extension in extensions:
            folders.append(f'  {{"{extension}", {index}}},')
    folders.append("};")
    folders.append("static const struct { uint32_t type; unsigned icon; } finder_generic_types[] = {")
    for index, (file_type, _) in enumerate(GENERIC_FILE_TYPES, len(filenames)):
        code = int.from_bytes(file_type.encode("ascii"), "big")
        folders.append(f"  {{0x{code:08x}u, {index}}},")
    folders.append("};")
    (output / "finder_folder_icons.inc").write_text("\n".join(folders) + "\n")


def generate_windows(output):
    """Publish Shell icons, executable resources and native association tables."""
    output.mkdir(parents=True, exist_ok=True)
    manifest = []
    resources = ["/* Generated disk-image icon resources; do not edit. */"]
    associations = [
        "/* Generated Windows disk-image associations; do not edit. */",
        "static const struct { const wchar_t *key; unsigned resource; const wchar_t *extensions[4]; } disk_image_types[]={",
    ]
    for key, filename, _, extensions in IMAGE_FILE_TYPES:
        frames = mac_icons.decode((ROOT / "bitmaps" / filename).read_bytes())
        (output / (key + ".ico")).write_bytes(mac_icons.encode_ico(frames))
        resource_id = WINDOWS_IMAGE_ICON_IDS[key]
        resources.append(f'{resource_id} ICON "{key}.ico"')
        suffixes = ",".join(f'L"{extension}"' for extension in extensions)
        associations.append(f'{{L"{key}",{resource_id},{{{suffixes},nullptr}}}},')
        manifest.append(
            dict(key=key, icon=key + ".ico", resource_id=resource_id, source=filename, extensions=extensions)
        )
    (output / "resources.rc").write_text("\n".join(resources) + "\n")
    (output / "associations.inc").write_text("\n".join(associations + ["};"]) + "\n")
    (output / "mapping.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--windows-output", type=Path)
    args = parser.parse_args()
    if not args.output and not args.windows_output:
        parser.error("provide --output or --windows-output")
    if args.output:
        generate(args.output)
    if args.windows_output:
        generate_windows(args.windows_output)
