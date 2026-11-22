#!/usr/bin/env python3
# +===================================================================
#   File:      test-combined-preferences.py
#
#   Summary:   Verify combined PRAM and resource storage in an isolated Windows profile.
#
#   Classes:   None.
#
#   Functions: resource_fork, apple_double, run_case
#
#   Origin:    Regression fixture for Executor's combined preferences file.
#
#   Copyright and Legal notices: See the repository license.
# ===================================================================+

"""Run the staged Windows executable without modifying the user's profile."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "synppc/tests"))
from mac_resources import AppleDouble, ResourceFork
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def resource_fork(payload):
    data = struct.pack(">I", len(payload)) + payload
    header = struct.pack(">IIII", 256, 256 + len(data), len(data), 50)
    resource_map = bytearray(50)
    resource_map[:16] = header
    struct.pack_into(">HHH", resource_map, 24, 28, 50, 0)
    struct.pack_into(">4sHH", resource_map, 30, b"TEST", 0, 10)
    struct.pack_into(">HHII", resource_map, 38, 321, 65535, 0, 0)
    return header + bytes(240) + data + resource_map


def apple_double(fork):
    header = bytearray(82)
    struct.pack_into(">II", header, 0, 0x00051607, 0x00020000)
    struct.pack_into(">H", header, 24, 2)
    struct.pack_into(">III", header, 26, 9, 50, 32)
    struct.pack_into(">III", header, 38, 2, 82, len(fork))
    header[50:58] = b"rsrcRSED"
    return header + fork


def run_case(executable, existing, custom_system=False):
    base = ROOT / "build/preferences-combined"
    base.mkdir(parents=True, exist_ok=True)
    home = Path(tempfile.mkdtemp(prefix="profile-", dir=base)).resolve()
    profile = home / ".romaw"
    system = profile / ("Apps/Custom System" if custom_system else "Apps/System Folder")
    system.mkdir(parents=True)
    (profile / "ROMAW.prefs").write_text("[HostDrives]\nSidecarDrives=none\n", encoding="utf-8")
    app_name = "Combined Preferences Probe"
    image = pef([0x38600000, 0x4E800020], bytes(8), main=0, reloc=[0x4600])
    write_fragment(profile / "Apps", app_name, image, 1)
    parameter = bytes.fromhex("a8000000 00000000 00000000 0002 6300 0388 004c")
    assert len(parameter) == 20
    combined = system / "Preferences/ROMAW"
    sidecar = combined.with_name("._ROMAW")
    if existing:
        combined.parent.mkdir()
        combined.write_bytes(parameter)
        sidecar.write_bytes(apple_double(resource_fork(b"abc")))
    environment = dict(os.environ, USERPROFILE=str(home))
    # Honor the profile's default System Folder, independent of the parent shell.
    for key in ("SYSTEMFOLDER", "DEFAULTFOLDER", "MACVOLUMES"):
        environment.pop(key, None)
    if custom_system:
        environment["SYSTEMFOLDER"] = str(system)

    def launch():
        result = subprocess.run(
            [
                str(executable),
                "-syszone",
                "8m",
                "-applzone",
                "64m",
                "-sdlaudio",
                "test",
                "-debug",
                "trace",
                str(profile / "Apps" / app_name),
            ],
            cwd=home,
            env=environment,
            timeout=60,
            capture_output=True,
        )
        assert result.returncode == 0, (result.returncode, home)
        log = (home / "stderr.txt").read_text(errors="replace")
        assert "PowerPC fragment completed" in log and "win_exception" not in log, home
        assert "Unable to load System resource overrides" not in log, home

    launch()
    assert combined.read_bytes() == (parameter if existing else b""), home
    assert not (system / "ParamRAM").exists(), home
    parsed = AppleDouble(sidecar.read_bytes())
    assert parsed.get(9)[:8] == b"rsrcRSED", home
    fork = ResourceFork(parsed.get(2))
    if existing:
        assert fork.records[b"TEST", 321][0] == b"abc", home
    else:
        assert not fork.records, home
    combined.write_bytes(parameter)
    sidecar.write_bytes(parsed.replace(2, resource_fork(b"xyz")))
    # Restart after independently editing both forks; neither may reset the other.
    launch()
    assert combined.read_bytes() == parameter, home
    fork = ResourceFork(AppleDouble(sidecar.read_bytes()).get(2))
    assert fork.records[b"TEST", 321][0] == b"xyz", home
    print(f"PASS combined preferences, existing={existing}, custom={custom_system}: {home}")


if __name__ == "__main__":
    if os.name != "nt":
        raise SystemExit("Run with Windows Python against a staged Windows executable")
    executable = Path(sys.argv[1]).resolve()
    run_case(executable, False)
    run_case(executable, True)
    run_case(executable, False, custom_system=True)
