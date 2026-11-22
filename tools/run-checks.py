#!/usr/bin/env python3
# +===================================================================
#   File:      run-checks.py
#
#   Summary:   Run grouped repository regression suites with compact progress and per-step diagnostic logs.
#
#   Classes:   None declared.
#
#   Functions: windows_path, steps, main
#
#   Origin:    Coordinates the existing project regression scripts without changing their test cases.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

"""Run in WSL; the root Makefile builds prerequisites once before invoking these suites."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
SUITES = ("build", "runtime", "cpu", "ppc", "ui")


def windows_path(path):
    return subprocess.check_output(["wslpath", "-w", str(path.resolve())], text=True).strip()


def steps(suite, executable, stage, obj):
    if suite == "build":
        return [
            ("archives", [sys.executable, "tools/check-static-libraries.py", "--obj", str(obj), "--stage", str(stage)]),
            ("generated", ["bash", "system/tests/check-build.sh", "generated-file"]),
            ("debug-info", ["bash", "system/tests/check-build.sh", "debug-info"]),
        ]
    if suite == "runtime":
        return [("robustness", ["bash", "system/tests/check-runtime.sh", "robustness"])]
    if suite == "cpu":
        return [
            ("68k-jit", ["bash", "syn68k/tests/check-core.sh", "jit-x64"]),
            ("68040-mmu", ["bash", "syn68k/tests/check-core.sh", "mmu040"]),
            ("68k-fpu", ["bash", "syn68k/tests/check-core.sh", "fpu", "--oracle"]),
            ("68k-control", ["bash", "syn68k/tests/check-core.sh", "control-register"]),
            ("ppc-core", ["bash", "synppc/tests/check-core.sh", "core"]),
        ]
    if suite == "ppc":
        return [
            (
                "toolbox",
                [
                    "powershell.exe",
                    "-NoProfile",
                    "-NonInteractive",
                    "-File",
                    windows_path(ROOT / "synppc/tests/run-romaw-tests.ps1"),
                    "-ROMAW",
                    windows_path(executable),
                ],
            )
        ]
    return [
        ("classic-ui", ["bash", "system/tests/check-ui.sh", "classic-ui"]),
        (
            "finder",
            [
                sys.executable,
                "tools/romaw/native_finder/build.py",
                "--output-dir",
                str(obj / "native-finder/windows-x64"),
                "--test",
            ],
        ),
        ("quickdraw-host", ["bash", "system/tests/check-graphics.sh", "quickdraw-host"]),
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("suites", nargs="*", choices=(*SUITES, "all", "standard"))
    parser.add_argument("--list", action="store_true", help="Describe suites without building or running tests")
    parser.add_argument("--romaw", type=Path)
    parser.add_argument("--logs", type=Path, default=ROOT / "build/checks")
    args = parser.parse_args()
    if args.list:
        for suite, description in (
            ("build", "archive ownership/staging, generated dependencies, debug information"),
            ("runtime", "existing robustness suite, including file, memory, sound and dialog fixtures"),
            ("cpu", "68K x64 JIT, 68040 MMU/FPU/control registers, PPC core/PEF"),
            ("ppc", "staged guest Toolbox, CFM and native compatibility probes"),
            ("ui", "shared drawing, native Finder and real QuickDraw presentation"),
        ):
            print(f"{suite}: {description}")
        return 0
    selected = []
    for suite in args.suites or ["standard"]:
        selected.extend(SUITES if suite == "all" else ("build", "runtime") if suite == "standard" else [suite])
    selected = list(dict.fromkeys(selected))
    bin_dir = Path(os.environ.get("ROMAW_STAGE_DIR", ROOT / "windows-binaries/bin"))
    mode = os.environ.get("ROMAW_BUILD_MODE", "optimized")
    if mode not in ("debug", "optimized"):
        parser.error("ROMAW_BUILD_MODE must be debug or optimized")
    default_obj = ROOT / ("system/obj" if mode == "debug" else "build/romaw-optimized/obj")
    obj = Path(os.environ.get("ROMAW_OBJ_DIR", default_obj)).resolve()
    executable = (args.romaw or bin_dir / ("ROMAW-debug.exe" if mode == "debug" else "ROMAW.exe")).resolve()
    default_stage = bin_dir / "libraries/x64"
    if mode == "optimized":
        default_stage /= "optimized"
    stage = Path(os.environ.get("ROMAW_LIBRARY_STAGE_DIR", default_stage)).resolve()
    environment = os.environ.copy()
    environment["ROMAW_TEST_EXE"] = str(executable)
    environment["ROMAW_OBJ_DIR"] = str(obj)
    environment["ROMAW_TEST_OBJ"] = str(obj)
    started = time.monotonic()
    for suite in selected:
        for name, command in steps(suite, executable, stage, obj):
            log = args.logs / suite / f"{name}.log"
            log.parent.mkdir(parents=True, exist_ok=True)
            print(f"Checking {suite}/{name}...", flush=True)
            with log.open("w", encoding="utf-8") as output:
                try:
                    status = subprocess.run(
                        command, cwd=ROOT, env=environment, stdout=output, stderr=subprocess.STDOUT
                    ).returncode
                except OSError as error:
                    output.write(str(error) + "\n")
                    status = 1
            if status:
                print(f"FAIL {suite}/{name} (exit {status}); see {log}", file=sys.stderr, flush=True)
                return 1
        print(f"PASS {suite}", flush=True)
    print(f"Passed {len(selected)} suites in {time.monotonic() - started:.1f}s. Logs: {args.logs}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
