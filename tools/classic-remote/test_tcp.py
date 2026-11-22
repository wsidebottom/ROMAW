# +===================================================================
#   File:      test_tcp.py
#
#   Summary:   Real loopback TCP framing/authentication checks, without a physical Mac.
#
#   Classes:   TCPTests
#
#   Functions: frame, read_exact, fragments
#
#   Origin:    Existing repository source in tools/classic-remote; original attribution and provenance comments are
#              preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Real loopback TCP framing/authentication checks, without a physical Mac."""

from contextlib import contextmanager
import ctypes
import json
from pathlib import Path
import socket
import struct
import threading
import time
import unittest

from controller import Controller

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build/classic-remote-tcp"
SESSION = b"test TCP session"


def frame(sequence=0, payload=b"", width=0, height=0, session=SESSION, length=None):
    return (
        struct.pack(
            ">4sIi5I16s",
            b"CMR1",
            sequence,
            0,
            width,
            height,
            len(payload) if length is None else length,
            100,
            0,
            session,
        )
        + payload
    )


def read_exact(connection, length):
    data = b""
    while len(data) < length:
        part = connection.recv(length - len(data))
        if not part:
            raise EOFError("Incomplete test command")
        data += part
    return data


def fragments(connection, data):
    for offset in range(0, len(data), 7):
        connection.sendall(data[offset : offset + 7])
        time.sleep(0.001)


class TCPTests(unittest.TestCase):
    @contextmanager
    def server(self, handler):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(5)
        config = json.loads((BUILD / "controller.json").read_text())
        config.update(
            transport="tcp",
            host="127.0.0.1",
            port=listener.getsockname()[1],
            crypto_directory=str(BUILD),
        )
        client = Controller(config)
        errors = []

        def run():
            try:
                with listener.accept()[0] as connection:
                    connection.settimeout(5)
                    fragments(connection, frame())
                    handler(connection, client)
            except Exception as error:
                errors.append(error)

        thread = threading.Thread(target=run, daemon=True)
        thread.start()
        try:
            yield client
        finally:
            client.close()
            thread.join(6)
            listener.close()
            self.assertFalse(thread.is_alive(), "TCP fixture did not finish")
            if errors:
                raise errors[0]

    def command(self, connection, client):
        data = read_exact(connection, 512)
        self.assertEqual(data[:4], b"CMR1")
        self.assertEqual(data[8:24], SESSION)
        check = client.crypto.crypto_ed25519_check(
            ctypes.create_string_buffer(data[256:320], 64),
            ctypes.create_string_buffer(bytes.fromhex(client.config["public_key"]), 32),
            ctypes.create_string_buffer(data[:256], 256),
            ctypes.c_size_t(256),
        )
        self.assertEqual(check, 0, "TCP command signature is invalid")
        return (
            struct.unpack_from(">I", data, 4)[0],
            struct.unpack_from(">I", data, 40)[0],
        )

    def test_persistent_connection_and_partial_frames(self):
        def handler(connection, client):
            for expected in (7, 1, 6):
                sequence, operation = self.command(connection, client)
                self.assertEqual(operation, expected)
                response = frame(sequence, bytes(range(18)), 3, 2) if operation == 1 else frame(sequence)
                fragments(connection, response)
            self.assertEqual(connection.recv(1), b"")

        with self.server(handler) as client:
            client.request(7)
            png = client.screenshot()
            self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
            self.assertEqual(struct.unpack_from(">II", png, 16), (3, 2))
            client.request(6)
            self.assertIsNone(client.connection)

    def test_reject_oversized_response_before_payload(self):
        def handler(connection, client):
            sequence, _ = self.command(connection, client)
            connection.sendall(frame(sequence, length=1024 * 768 * 3 + 1))
            self.assertEqual(connection.recv(1), b"")

        with self.server(handler) as client:
            with self.assertRaisesRegex(ValueError, "Oversized"):
                client.request(7)
            self.assertIsNone(client.connection)

    def test_truncated_response(self):
        def handler(connection, client):
            sequence, _ = self.command(connection, client)
            connection.sendall(frame(sequence, b"short", length=32))

        with self.server(handler) as client:
            with self.assertRaises(ConnectionError):
                client.request(7)
            self.assertIsNone(client.connection)

    def test_session_and_sequence_mismatch(self):
        for wrong_session in (False, True):
            with self.subTest(wrong_session=wrong_session):

                def handler(connection, client):
                    sequence, _ = self.command(connection, client)
                    connection.sendall(
                        frame(sequence, session=b"changed session!") if wrong_session else frame(sequence + 1)
                    )
                    self.assertEqual(connection.recv(1), b"")

                with self.server(handler) as client:
                    with self.assertRaises((ValueError, RuntimeError)):
                        client.request(7)
                    self.assertIsNone(client.connection)

    def test_lost_reply_does_not_replay_input(self):
        calls = []

        def handler(connection, client):
            calls.append(self.command(connection, client))
            # Simulate execution followed by loss of the acknowledgement.

        with self.server(handler) as client:
            with self.assertRaises(ConnectionError):
                client.request(11, 100, 100)
            self.assertIsNone(client.connection)
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0][1], 11)


if __name__ == "__main__":
    unittest.main()
