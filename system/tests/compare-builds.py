#!/usr/bin/env python3
# +===================================================================
#   File:      compare-builds.py
#
#   Summary:   Compare initialized ROMAW rendering workloads and reject changed outputs between build profiles.
#
#   Classes:   None.
#
#   Functions: native_path, digest, main
#
#   Origin:    Executor performance comparison runner using the existing isolated graphics probe.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

"""Run in WSL or Windows after building toolbox-benchmark.dll and both executables."""

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def native_path(path):
    path = str(path.resolve())
    if os.name == "nt":
        return path
    return subprocess.check_output(["wslpath", "-w", path], text=True).strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--debug", type=Path, default=ROOT / "windows-binaries/bin/ROMAW-debug.exe")
    parser.add_argument("--optimized", type=Path, default=ROOT / "windows-binaries/bin/ROMAW-optimized.exe")
    parser.add_argument("--module", type=Path, default=ROOT / "build/toolbox-benchmark.dll")
    parser.add_argument("--runs", type=int, choices=range(1, 10), default=3)
    args = parser.parse_args()
    executables = {"debug": args.debug, "optimized": args.optimized}
    hashes = {name: digest(path) for name, path in executables.items()}
    artifacts = Path(tempfile.mkdtemp(prefix="toolbox-comparison.", dir=ROOT / "build"))
    samples = {name: {} for name in executables}
    outputs = {}
    # Alternate profiles so one does not consistently receive a colder machine.
    for run in range(args.runs):
        order = list(executables) if run % 2 == 0 else list(reversed(executables))
        for name in order:
            output = artifacts / f"{name}-{run + 1}.csv"
            env = os.environ.copy()
            env["ROMAW_BENCHMARK_OUTPUT"] = native_path(output)
            if os.name != "nt":
                env["WSLENV"] = ":".join(filter(None, [env.get("WSLENV"), "ROMAW_BENCHMARK_OUTPUT"]))
            command = [
                "powershell.exe",
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                native_path(ROOT / "system/tests/check-graphics.ps1"),
                "quickdraw-host",
                "-Executable",
                native_path(executables[name]),
                "-Module",
                native_path(args.module),
                "-Marker",
                "TOOLBOX_BENCHMARK_OK",
                "-NoTrace",
            ]
            result = subprocess.run(command, env=env, capture_output=True, text=True, errors="replace", timeout=90)
            (artifacts / f"{name}-{run + 1}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
            result.check_returncode()
            with output.open(newline="", encoding="utf-8") as file:
                rows = list(csv.DictReader(file))
            if {row["workload"] for row in rows} != {
                "window_chrome",
                "wrapped_text",
                "desktop_pattern",
                "image_copy",
            } or len(rows) != 4:
                raise RuntimeError(f"Incomplete benchmark output: {output}")
            for row in rows:
                workload = row["workload"]
                identity = {key: value for key, value in row.items() if key != "ms_per_operation"}
                if outputs.setdefault(workload, identity) != identity:
                    raise RuntimeError(f"Rendering output differs for {workload}: {output}")
                elapsed = float(row["ms_per_operation"])
                if not math.isfinite(elapsed) or elapsed <= 0:
                    raise RuntimeError(f"Invalid timing: {output}")
                samples[name].setdefault(workload, []).append(elapsed)
            print(f"{name} run {run + 1}: four workloads passed", flush=True)
    if hashes != {name: digest(path) for name, path in executables.items()}:
        raise RuntimeError("An executable changed during the comparison; rerun with stable builds")
    summary = {"executables": {name: str(path.resolve()) for name, path in executables.items()}, "sha256": hashes}
    summary["workloads"] = {}
    for workload, identity in outputs.items():
        debug = statistics.median(samples["debug"][workload])
        optimized = statistics.median(samples["optimized"][workload])
        summary["workloads"][workload] = {
            **identity,
            "debug_ms": debug,
            "optimized_ms": optimized,
            "speedup": debug / optimized,
            "samples_ms": {name: values[workload] for name, values in samples.items()},
        }
        print(f"{workload}: {debug:.3f} -> {optimized:.3f} ms, {debug / optimized:.2f}x", flush=True)
    (artifacts / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"Matching pixel checksums and metrics across all runs. Results: {artifacts}")


if __name__ == "__main__":
    main()
