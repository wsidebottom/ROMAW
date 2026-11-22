# +===================================================================
#   File:      test_viewer.py
#
#   Summary:   Loopback viewer failure isolation; no Mac, keys or AFP share are accessed.
#
#   Classes:   RecordingController, ViewerTests
#
#   Functions: No public module functions.
#
#   Origin:    Existing repository source in tools/classic-remote; original attribution and provenance comments are
#              preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Loopback viewer failure isolation; no Mac, keys or AFP share are accessed."""

from concurrent.futures import ThreadPoolExecutor
from contextlib import closing, contextmanager
from http.client import HTTPConnection
import json
import threading
import unittest

from controller import viewer_server


class RecordingController:
    def __init__(self):
        self.calls = []
        self.fail = False
        self.closes = 0

    def close(self):
        self.closes += 1

    def screenshot(self):
        self.calls.append("screen")
        if self.fail:
            raise TimeoutError("Mac did not respond")
        return b"test image"

    def request(self, *args):
        self.calls.append(args)
        if self.fail:
            raise ConnectionError("Mac disconnected")


class ViewerTests(unittest.TestCase):
    @contextmanager
    def server(self):
        controller = RecordingController()
        with viewer_server(controller, 0) as server:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                yield controller, server.server_port
            finally:
                server.shutdown()
                thread.join(3)
                self.assertFalse(thread.is_alive())

    def request(self, port, path, data=None, origin=None):
        with closing(HTTPConnection("127.0.0.1", port, timeout=3)) as connection:
            headers = {}
            body = None
            if data is not None:
                body = json.dumps(data)
                headers["Content-Type"] = "application/json"
                headers["Origin"] = origin or f"http://127.0.0.1:{port}"
            connection.request("GET" if data is None else "POST", path, body, headers)
            response = connection.getresponse()
            return response.status, response.read()

    def test_startup_and_stale_clients_do_not_contact_mac(self):
        with self.server() as (controller, port):
            self.assertEqual(self.request(port, "/")[0], 200)
            for _ in range(10):
                self.assertEqual(self.request(port, "/screen")[0], 503)
                self.assertEqual(self.request(port, "/input", {"op": 8})[0], 400)
            self.assertEqual(controller.calls, [])
            self.assertEqual(controller.closes, 0)

    def test_failure_latches_even_for_concurrent_old_clients(self):
        with self.server() as (controller, port):
            self.assertEqual(self.request(port, "/connect", {})[0], 200)
            controller.fail = True
            with ThreadPoolExecutor(5) as pool:
                results = list(pool.map(lambda _: self.request(port, "/screen"), range(10)))
            self.assertTrue(all(status == 503 for status, _ in results))
            self.assertEqual(controller.calls, ["screen"])
            self.assertEqual(controller.closes, 2)
            controller.fail = False
            self.assertEqual(self.request(port, "/screen")[0], 503)
            self.assertEqual(self.request(port, "/input", {"op": 11})[0], 400)
            self.assertEqual(controller.calls, ["screen"])
            self.assertEqual(self.request(port, "/connect", {})[0], 200)
            self.assertEqual(self.request(port, "/screen"), (200, b"test image"))
            self.assertEqual(controller.calls, ["screen", "screen"])

    def test_input_failure_blocks_further_network_work(self):
        with self.server() as (controller, port):
            self.request(port, "/connect", {})
            controller.fail = True
            self.assertEqual(self.request(port, "/input", {"op": 11})[0], 400)
            self.assertEqual(self.request(port, "/input", {"op": 11})[0], 400)
            self.assertEqual(self.request(port, "/screen")[0], 503)
            self.assertEqual(len(controller.calls), 1)

    def test_foreign_origin_cannot_resume_connection(self):
        with self.server() as (controller, port):
            self.assertEqual(self.request(port, "/connect", {}, "http://example.invalid")[0], 400)
            self.assertEqual(self.request(port, "/connect", {"unexpected": 1})[0], 400)
            self.assertEqual(self.request(port, "/screen")[0], 503)
            self.assertEqual(controller.calls, [])

    def test_quit_blocks_other_browser_tabs(self):
        with self.server() as (controller, port):
            self.request(port, "/connect", {})
            self.assertEqual(self.request(port, "/input", {"op": 6})[0], 200)
            self.assertEqual(self.request(port, "/screen")[0], 503)
            self.assertEqual(controller.calls, [(6, 0, 0, False, 0)])


if __name__ == "__main__":
    unittest.main()
