#!/usr/bin/env python3
# +===================================================================
#   File:      reference-runner-test.py
#
#   Summary:   Check reference-runner startup retries without executing guest CPU code.
#
#   Classes:   ReferenceRunnerTests
#
#   Functions: No public module functions.
#
#   Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              syn68k/COPYING.
# ===================================================================+

"""Check reference-runner startup retries without executing guest CPU code."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

FAKE = r"""#!/usr/bin/env python3
import os
from pathlib import Path
import sys
if "--reference-native-flags" in sys.argv:
    print(os.environ["REFERENCE_RUNNER_FLAGS"])
    sys.exit(0)
if "--native-code-enabled" in sys.argv:
    print(os.environ["REFERENCE_RUNNER_NATIVE"])
    sys.exit(0)
state = Path(os.environ["REFERENCE_RUNNER_STATE"])
attempt = int(state.read_text()) + 1 if state.exists() else 1
state.write_text(str(attempt))
scenario = os.environ["REFERENCE_RUNNER_SCENARIO"]
allocation = b"Unable to reserve CPU test memory; error 487.\r\n"
if scenario == "forever" or scenario == "allocation" and attempt <= 2:
    sys.stderr.buffer.write(allocation)
    sys.exit(1)
if scenario == "partial":
    print("Testing guest instructions")
    sys.stderr.buffer.write(allocation)
    sys.exit(1)
if scenario == "mixed":
    sys.stderr.buffer.write(allocation + b"CPU instruction failure\n")
    sys.exit(1)
if scenario == "cpu":
    print("Testing guest instructions")
    print("CPU instruction failure", file=sys.stderr)
    sys.exit(7)
interpreted = "-notnative" in sys.argv or os.environ["REFERENCE_RUNNER_FLAGS"] == "0"
expected = "interpreted reference" if interpreted else "reference"
print("changed reference" if scenario == "crc" else expected)
"""


class ReferenceRunnerTests(unittest.TestCase):
    def run_fixture(self, scenario, native="1", modes=("native",), flags=None):
        temporary = tempfile.TemporaryDirectory(prefix="syn68k-reference-runner-")
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        tests = root / "tests"
        tests.mkdir()
        runner = tests / "testall.sh"
        shutil.copyfile(RUNNER, runner)
        binary = root / "obj/tests/syngentest.exe"
        binary.parent.mkdir(parents=True)
        binary.write_text(FAKE)
        binary.chmod(0o755)
        reference = tests / "output/10000"
        reference.parent.mkdir()
        reference.write_text("reference\n")
        (reference.parent / "10000-noncc").write_text("reference\n")
        for name in ("10000-notnative", "10000-noncc-notnative"):
            (reference.parent / name).write_text("interpreted reference\n")
        state = root / "attempts"
        environment = dict(os.environ)
        environment.update(
            REFERENCE_RUNNER_STATE=str(state),
            REFERENCE_RUNNER_SCENARIO=scenario,
            REFERENCE_RUNNER_NATIVE=native,
            REFERENCE_RUNNER_FLAGS=native if flags is None else flags,
        )
        result = subprocess.run(
            ["bash", str(runner), *modes],
            env=environment,
            capture_output=True,
            text=True,
            timeout=20,
        )
        self.assertFalse(list((root / "obj/tests/reference").glob("syngentest-*.exe")))
        return (
            result,
            int(state.read_text()) if state.exists() else 0,
            root / "obj/tests/reference",
        )

    def test_allocation_retry_preserves_failures_and_final_output(self):
        result, attempts, output = self.run_fixture("allocation")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(attempts, 3)
        self.assertEqual((output / "native.out").read_text(), "reference\n")
        self.assertEqual((output / "native.startup.log").read_text().count("487"), 2)
        self.assertIn("PASS: integer CPU reference (native)", result.stdout)

    def test_allocation_retry_has_finite_limit(self):
        result, attempts, output = self.run_fixture("forever")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(attempts, 5)
        self.assertEqual((output / "native.startup.log").read_text().count("487"), 5)
        self.assertIn("remained unavailable after 5 launches", result.stderr)

    def test_instruction_failure_is_not_retried(self):
        result, attempts, _ = self.run_fixture("cpu")
        self.assertEqual(result.returncode, 7)
        self.assertEqual(attempts, 1)

    def test_partial_instruction_output_is_not_retried(self):
        result, attempts, _ = self.run_fixture("partial")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(attempts, 1)

    def test_additional_error_diagnostic_is_not_retried(self):
        result, attempts, _ = self.run_fixture("mixed")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(attempts, 1)

    def test_crc_difference_is_not_retried(self):
        result, attempts, _ = self.run_fixture("crc")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(attempts, 1)
        self.assertIn("changed reference", result.stdout)

    def test_success_needs_no_retry(self):
        result, attempts, output = self.run_fixture("success")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(attempts, 1)
        self.assertEqual((output / "native.startup.log").read_bytes(), b"")

    def test_unavailable_native_uses_interpreter_reference(self):
        result, attempts, output = self.run_fixture("success", native="0")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(attempts, 1)
        self.assertEqual((output / "native.out").read_text(), "interpreted reference\n")

    def test_interpreter_default_avoids_duplicate_native_modes(self):
        result, attempts, _ = self.run_fixture("success", native="0", modes=())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(attempts, 2)
        self.assertIn("(interpreter)", result.stdout)
        self.assertIn("(noncc-interpreter)", result.stdout)

    def test_x64_jit_runs_all_modes_with_interpreter_flag_reference(self):
        result, attempts, _ = self.run_fixture("success", flags="0", modes=())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(attempts, 4)
        self.assertIn("(native)", result.stdout)
        self.assertIn("(noncc)", result.stdout)

    def test_invalid_backend_report_does_not_run_cpu_tests(self):
        result, attempts, _ = self.run_fixture("success", native="unknown")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(attempts, 0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, default=Path(__file__).with_name("testall.sh"))
    RUNNER = parser.parse_args().runner.resolve()
    unittest.main(argv=[__file__])
