#!/usr/bin/env python3
# +===================================================================
#   File:      test_regression_suites.py
#
#   Summary:   Verify suite listings, argument forwarding and shell failure isolation.
#
#   Classes:   RegressionSuitesTest.
#
#   Functions: windows_path, declared_shell_checks; unittest entry point.
#
#   Origin:    Regression tests for the consolidated repository check dispatchers.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

"""Check dispatcher behavior without launching GUI or physical-Mac probes."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SUITES = sorted(
    path for project in ("system", "syn68k", "synppc") for path in (ROOT / project / "tests").glob("check-*")
)
POWERSHELL = shutil.which("powershell.exe")


def windows_path(path):
    if os.name == "nt":
        return str(path)
    return subprocess.check_output(["wslpath", "-w", str(path)], text=True).strip()


def declared_shell_checks(source):
    """Read literal check names, including aliases, from the maintained case arms."""
    pattern = r"^  ([a-z0-9][a-z0-9-]*(?:\s*\|\s*[a-z0-9][a-z0-9-]*)*)\)"
    return [
        name.strip()
        for group in re.findall(pattern, source.replace("\\\n", ""), re.MULTILINE)
        for name in group.split("|")
        if name.strip() != "list"
    ]


class RegressionSuitesTest(unittest.TestCase):
    def test_shell_selector_inventory_includes_aliases(self):
        source = """case "$check" in
  --list | list)
    printf '%s\\n' alpha beta gamma delta epsilon
    ;;
  alpha)
    case "$mode" in
      nested) false ;;
    esac
    ;;
  beta | gamma)
    true
    ;;
  delta |
    epsilon)
    true
    ;;
  *) exit 2 ;;
esac
"""
        self.assertEqual(declared_shell_checks(source), ["alpha", "beta", "gamma", "delta", "epsilon"])
        continued = "  first | " + "\\\n" + "    second)\n"
        self.assertEqual(declared_shell_checks(continued), ["first", "second"])

    @unittest.skipUnless(os.name == "posix" and shutil.which("bash"), "Bash checks run in WSL/Linux")
    def test_shell_listings_and_errors(self):
        for path in SUITES:
            if path.suffix != ".sh":
                continue
            with self.subTest(suite=path.name, project=path.parent.parent.name):
                syntax = subprocess.run(["bash", "-n", str(path)], capture_output=True, text=True)
                self.assertEqual(syntax.returncode, 0, syntax.stderr)
                listed = subprocess.run(["bash", str(path), "--list"], capture_output=True, text=True, check=True)
                names = listed.stdout.splitlines()
                self.assertTrue(names)
                self.assertEqual(len(names), len(set(names)))
                declared = declared_shell_checks(path.read_text(encoding="utf-8"))
                # Listing order is a presentation choice, independent of the
                # implementation's placement of case arms.
                self.assertCountEqual(names, declared)
                default = subprocess.check_output(["bash", str(path)], text=True)
                self.assertEqual(default, listed.stdout)
                failed = subprocess.run(["bash", str(path), "unknown-check"], capture_output=True)
                self.assertEqual(failed.returncode, 2)

    @unittest.skipUnless(os.name == "posix", "Compiler-failure injection uses a temporary POSIX PATH")
    def test_shell_stops_after_failed_compilation(self):
        with tempfile.TemporaryDirectory() as directory:
            compiler = Path(directory) / "gcc"
            compiler.write_text("#!/bin/sh\nexit 37\n")
            compiler.chmod(0o755)
            result = subprocess.run(
                ["bash", str(ROOT / "system/tests/check-media.sh"), "aiff"],
                env=dict(os.environ, PATH=directory + os.pathsep + os.environ["PATH"]),
                capture_output=True,
            )
            self.assertEqual(result.returncode, 37)

    @unittest.skipUnless(POWERSHELL, "Windows PowerShell is unavailable")
    def test_powershell_listings(self):
        for path in SUITES:
            if path.suffix != ".ps1":
                continue
            with self.subTest(suite=path.name, project=path.parent.parent.name):
                listed = subprocess.run(
                    [POWERSHELL, "-NoProfile", "-File", windows_path(path), "--list"],
                    capture_output=True,
                    text=True,
                    check=True,
                )
                names = listed.stdout.splitlines()
                self.assertTrue(names)
                self.assertEqual(len(names), len(set(names)))
                declared = re.findall(
                    r"^function Invoke-Check-([a-z0-9-]+) \{", path.read_text(encoding="utf-8"), re.MULTILINE
                )
                self.assertCountEqual(names, declared)

    @unittest.skipUnless(POWERSHELL, "Windows PowerShell is unavailable")
    def test_powershell_argument_binding_and_errors(self):
        helper = windows_path(ROOT / "tools/regression-support.ps1").replace("'", "''")
        script = r"""
$ErrorActionPreference = 'Stop'
. '__HELPER__'
function Invoke-Check-sample {
    param([Alias('P')][Parameter(Position=0)][string]$Path, [switch]$Enabled, [int]$Count)
    "$Path|$Enabled|$Count"
}
function Invoke-Check-failure { throw 'Expected failure' }
$names = @('sample','failure')
$result = Invoke-RegressionCheck -Names $names -CheckArguments @('sample','-P','C:\a b','-Enabled','-Count','7')
if ($result -ne 'C:\a b|True|7') { throw "Named arguments changed: $result" }
$result = Invoke-RegressionCheck -Names $names -CheckArguments @('sample','C:\a b','-Enabled:$false','-Count:9')
if ($result -ne 'C:\a b|False|9') { throw "Positional/switch arguments changed: $result" }
$result = Invoke-RegressionCheck -Names $names -CheckArguments @('sample','-P:')
if ($result -ne '|False|0') { throw "Empty inline argument changed: $result" }
foreach ($bad in @(@('missing'), @('sample','-Count'), @('sample','-Unknown'), @('failure'))) {
    $failed = $false
    try { Invoke-RegressionCheck -Names $names -CheckArguments $bad } catch { $failed = $true }
    if (-not $failed) { throw "Failure did not propagate: $bad" }
}
'argument-binding-ok'
""".replace(
            "__HELPER__", helper
        )
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            path = Path(directory) / "dispatch.ps1"
            path.write_text(script, encoding="utf-8")
            result = subprocess.run(
                [POWERSHELL, "-NoProfile", "-File", windows_path(path)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("argument-binding-ok", result.stdout)


if __name__ == "__main__":
    unittest.main()
