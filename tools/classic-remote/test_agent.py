# +===================================================================
#   File:      test_agent.py
#
#   Summary:   End-to-end Windows ROMAW checks for the original compiled control agent.
#
#   Classes:   AgentTests
#
#   Functions: No public module functions.
#
#   Origin:    Existing repository source in tools/classic-remote; original attribution and provenance comments are
#              preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""End-to-end Windows ROMAW checks for the original compiled control agent.

Build with build.py first. Each mode gets a private application/mailbox copy;
no network service, physical Mac or user's running ROMAW is controlled.
"""

import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
import unittest

from controller import Controller

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build/classic-remote"
ROMAW = ROOT / "windows-binaries/bin/ROMAW.exe"


class AgentTests(unittest.TestCase):
    def run_agent(self, mode):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as temporary:
            directory = Path(temporary)
            for name in ("Classic Remote", "._Classic Remote"):
                shutil.copyfile(BUILD / name, directory / name)
            (directory / "Remote Command").write_bytes(bytes(512))
            config = json.loads((BUILD / "controller.json").read_text())
            config["crypto_directory"] = str(BUILD)
            client = Controller(config, directory)
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = subprocess.SW_HIDE
            process = subprocess.Popen(
                [
                    str(ROMAW),
                    mode,
                    "-debug",
                    "trace",
                    str(directory / "Classic Remote"),
                ],
                cwd=directory,
                startupinfo=startup,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            try:
                # Failed publication must yield and retry, rather than leaving
                # a running agent whose authentication session is inaccessible.
                time.sleep(0.3)
                self.assertIsNone(process.poll())
                (directory / "Remote Response").write_bytes(bytes(48))
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    response = (directory / "Remote Response").read_bytes()
                    if response[:4] == b"CMR1":
                        break
                    if process.poll() is not None:
                        self.fail("Agent exited before publishing its session")
                    time.sleep(0.05)
                else:
                    self.fail("Agent startup timed out")
                client.request(7)
                replay = (directory / "Remote Command").read_bytes()
                client.request(7)
                last = (directory / "Remote Response").read_bytes()
                # A valid older signed command cannot replace the newer reply.
                client.file("Remote Command", replay)
                time.sleep(0.25)
                self.assertEqual(client.file("Remote Response"), last)
                # The current session and a new sequence do not authorize a
                # modified payload with its old signature.
                tampered = bytearray(replay)
                struct.pack_into(">I", tampered, 4, client.sequence + 100)
                struct.pack_into(">I", tampered, 40, 6)
                client.file("Remote Command", bytes(tampered))
                time.sleep(0.25)
                self.assertIsNone(process.poll())
                self.assertEqual(client.file("Remote Response"), last)
                # Replacing the nonce also invalidates a captured command.
                tampered[8] ^= 1
                client.file("Remote Command", bytes(tampered))
                time.sleep(0.25)
                self.assertEqual(client.file("Remote Response"), last)
                png = client.screenshot()
                self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
                width, height = struct.unpack_from(">II", png, 16)
                self.assertGreater(width, 0)
                self.assertGreater(height, 0)
                with self.assertRaisesRegex(RuntimeError, "-54"):
                    client.request(3, 10, 10)
                client.request(8)
                client.request(2, 12, 12)
                # The event yield between requests lets ROMAW's cursor
                # processing copy the requested location into Mouse.
                self.assertEqual(client.input_state()["mouse"], {"x": 12, "y": 12})
                # Exercise button and key state without any host OS input.
                client.request(3, 10, 10)
                self.assertTrue(client.input_state()["button_down"])
                client.request(2, 12, 12)
                self.assertEqual(client.input_state()["raw_mouse"], {"x": 12, "y": 12})
                client.request(4, 12, 12)
                self.assertFalse(client.input_state()["button_down"])
                client.request(5, 0, ord("a"), True)
                self.assertTrue(bytes.fromhex(client.input_state()["key_map"])[0] & 1)
                client.request(5, 0, ord("a"), False)
                self.assertFalse(bytes.fromhex(client.input_state()["key_map"])[0] & 1)
                client.request(11, 14, 14)
                self.assertFalse(client.input_state()["button_down"])
                client.request(9)
                with self.assertRaisesRegex(RuntimeError, "-54"):
                    client.request(3, 10, 10)
                with self.assertRaisesRegex(RuntimeError, "-54"):
                    client.request(11, 10, 10)
                client.request(6)
                self.assertEqual(process.wait(timeout=10), 0)
                log = (directory / "stderr.txt").read_text(errors="replace")
                self.assertIn("PowerPC fragment completed", log)
                self.assertNotIn("win_exception", log)
                self.assertNotIn("PowerPC launch failed", log)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=10)
                log = directory / "stderr.txt"
                if log.exists():
                    shutil.copyfile(log, BUILD / (mode + "-agent-test.log"))

    def test_jit(self):
        self.run_agent("-ppc-jit")

    def test_interpreter(self):
        self.run_agent("-no-ppc-jit")


if __name__ == "__main__":
    unittest.main()
