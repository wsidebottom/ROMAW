# +===================================================================
#   File:      controller.py
#
#   Summary:   Control the classic Mac agent over TCP, with an optional AFP mailbox fallback.
#
#   Classes:   Controller, ViewerConnection
#
#   Functions: viewer_server, serve
#
#   Origin:    Existing repository source in tools/classic-remote; original attribution and provenance comments are
#              preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Control the classic Mac agent over TCP, with an optional AFP mailbox fallback.

The browser viewer listens only on 127.0.0.1. The pairing configuration remains
local; only fixed mouse/keyboard/capture operations reach the Mac.
"""

import argparse
from contextlib import contextmanager
import ctypes
import json
from pathlib import Path
import shlex
import socket
import struct
import subprocess
import sys
import threading
import time
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Controller:
    def __init__(self, config, local=None):
        self.config = config
        self.local = local
        self.sequence = 0
        suffix = "monocypher.dll" if __import__("os").name == "nt" else "monocypher.so"
        self.crypto = ctypes.CDLL(str(Path(config["crypto_directory"]) / suffix))
        self.lock = threading.Lock()
        self.tcp = not local and config.get("transport") == "tcp"
        self.connection = None
        self.pending = False
        self.previous = None

    def close(self):
        if self.connection:
            self.connection.close()
        self.connection = None
        self.pending = False
        self.previous = None

    @contextmanager
    def transaction(self):
        with self.lock:
            try:
                yield
            except Exception:
                self.close()
                raise

    def tcp_read(self, length):
        data = bytearray()
        while len(data) < length:
            part = self.connection.recv(min(length - len(data), 65536))
            if not part:
                raise ConnectionError("Classic Remote closed the TCP connection")
            data.extend(part)
        return bytes(data)

    def tcp_frame(self):
        header = self.tcp_read(48)
        if header[:4] != b"CMR1":
            raise ValueError("Invalid TCP response header")
        length = struct.unpack_from(">I", header, 20)[0]
        if length > 1024 * 768 * 3:
            raise ValueError("Oversized agent response")
        return header + self.tcp_read(length)

    def tcp_file(self, name, data):
        if not self.connection:
            self.connection = socket.create_connection(
                (self.config["host"], int(self.config.get("port", 6804))), timeout=15
            )
            self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.previous = self.tcp_frame()
            if len(self.previous) != 48 or struct.unpack_from(">i", self.previous, 8)[0]:
                raise ValueError("Invalid TCP session handshake")
        if data is not None:
            if name != "Remote Command" or len(data) != 512 or self.pending:
                raise ValueError("Invalid TCP command frame")
            self.connection.sendall(data)
            self.pending = True
            return b""
        if name != "Remote Response":
            raise ValueError("Invalid TCP response request")
        if self.pending:
            response = self.tcp_frame()
            self.previous = response[:48]
            self.pending = False
            return response
        return self.previous

    def file(self, name, data=None):
        if self.tcp:
            return self.tcp_file(name, data)
        limit = 48 + 1024 * 768 * 3
        if self.local:
            path = self.local / name
            if data is None:
                with path.open("rb") as stream:
                    result = stream.read(limit + 1)
                if len(result) > limit:
                    raise ValueError("Oversized mailbox file")
                return result
            path.write_bytes(data)
            return b""
        path = self.config["folder"] + "/" + name
        if data is None:
            code = f"import sys;sys.stdout.buffer.write(open({path!r},'rb').read({limit + 1}))"
        else:
            # Keep the existing inode/CNID while the Mac repeatedly opens it.
            code = f"import sys;f=open({path!r},'r+b');" "f.write(sys.stdin.buffer.read());f.flush();f.close()"
        result = subprocess.run(
            [
                "ssh",
                "-C",
                "-o",
                "BatchMode=yes",
                "-o",
                "ConnectTimeout=5",
                "-o",
                "StrictHostKeyChecking=yes",
                self.config["server"],
                "python3 -c " + shlex.quote(code),
            ],
            input=data,
            capture_output=True,
            timeout=15,
        )
        if result.returncode:
            raise RuntimeError(result.stderr.decode(errors="replace"))
        if len(result.stdout) > limit:
            raise ValueError("Oversized mailbox file")
        return result.stdout

    def request(self, operation, x=0, y=0, down=False, modifiers=0):
        if operation not in range(1, 12) or not 0 <= x < 1024 or not 0 <= y < 768:
            raise ValueError("Invalid remote operation or coordinate")
        with self.transaction():
            previous = self.file("Remote Response")
            if len(previous) < 48 or previous[:4] != b"CMR1":
                raise RuntimeError("Agent has not published its session; launch Classic Remote")
            self.sequence = max(self.sequence, struct.unpack_from(">I", previous, 4)[0]) + 1
            if self.sequence > 0xFFFFFFFF:
                raise RuntimeError("Restart the agent to start a new command session")
            command = struct.pack(
                ">4sI32sIIII",
                b"CMR1",
                self.sequence,
                previous[32:48].ljust(32, b"\0"),
                operation,
                0,
                x,
                y,
            )
            command += bytes([bool(down), 0]) + struct.pack(">H", modifiers & 0x1F00)
            command = command.ljust(256, b"\0")
            signature = ctypes.create_string_buffer(64)
            private = ctypes.create_string_buffer(bytes.fromhex(self.config["private_key"]), 64)
            message = ctypes.create_string_buffer(command, len(command))
            self.crypto.crypto_ed25519_sign(signature, private, message, ctypes.c_size_t(len(command)))
            self.file("Remote Command", (command + signature.raw).ljust(512, b"\0"))
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                response = self.file("Remote Response")
                if len(response) >= 48:
                    magic, sequence, error, width, height, length, tick, _ = struct.unpack_from(">4sIi5I", response)
                    if magic == b"CMR1" and sequence == self.sequence:
                        if response[32:48] != previous[32:48]:
                            raise RuntimeError("Agent restarted during the command")
                        if length > 1024 * 768 * 3:
                            raise ValueError("Oversized agent response")
                        if len(response) == 48 + length:
                            if error:
                                raise RuntimeError(f"Mac Toolbox error {error}")
                            if operation == 1 and (
                                not 0 < width <= 1024 or not 0 < height <= 768 or length != width * height * 3
                            ):
                                raise ValueError("Malformed screenshot")
                            if operation == 6:
                                self.close()
                            return width, height, response[48:]
                    elif self.tcp:
                        raise ValueError("Unexpected TCP response sequence")
                time.sleep(0.08)
            raise TimeoutError("No reply from Classic Remote; is the agent running?")

    def input_state(self):
        width, height, data = self.request(10)
        if width or height or len(data) != 32:
            raise ValueError("Malformed input state")
        points = [struct.unpack_from(">hh", data, offset) for offset in (0, 4, 8)]
        return {
            **{
                name: {"x": point[1], "y": point[0]}
                for name, point in zip(("mouse_temp", "raw_mouse", "mouse"), points)
            },
            "cursor_new": data[12],
            "cursor_coupled": data[13],
            "button_down": not bool(data[14] & 128),
            "input_enabled": bool(data[15]),
            "key_map": data[16:].hex(),
        }

    def screenshot(self, saved=False):
        if saved and self.tcp:
            raise ValueError("Saved mailbox captures are unavailable over TCP")
        if saved:
            response = self.file("Remote Response")
            if len(response) < 48 or response[:4] != b"CMR1":
                raise ValueError("No saved screen response")
            error, width, height, length = struct.unpack_from(">iIII", response, 8)
            if error:
                raise RuntimeError(f"Mac Toolbox error {error}")
            if (
                not 0 < width <= 1024
                or not 0 < height <= 768
                or length != width * height * 3
                or len(response) != 48 + length
            ):
                raise ValueError("Saved screen response is incomplete or malformed")
            rgb = response[48:]
        else:
            width, height, rgb = self.request(1)

        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

        scanlines = b"".join(b"\0" + rgb[y * width * 3 : (y + 1) * width * 3] for y in range(height))
        return (
            b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(scanlines, 1))
            + chunk(b"IEND", b"")
        )


PAGE = """<!doctype html><meta charset=utf-8><title>Classic Remote</title>
<style>body{margin:16px;background:#ddd;font:14px sans-serif}button,label{margin-right:12px}
img{display:block;margin-top:14px;max-width:100%;image-rendering:pixelated;outline:1px solid #000}
#state{padding:8px 0}canvas{display:none}</style>
<b>Classic Remote</b> &nbsp; <button onclick=connect()>Connect</button>
<button onclick=refresh()>Refresh screen</button>
<label><input id=automatic type=checkbox onchange='if(this.checked)refresh()'>Auto refresh (5 seconds)</label>
<label><input id=control type=checkbox onchange='toggleInput(this)'>Enable input</label>
<label><input id=cmd type=checkbox>Command</label>
<label><input id=shift type=checkbox>Shift</label>
<button onclick='send({op:6}).then(ok=>{if(ok){stopped=true;state.textContent="Agent stopped"}})'>Quit Mac agent</button>
<div id=state>Disconnected — click Connect</div><img id=screen tabindex=0 alt='Mac screen'>
<p>Click the screen to focus keyboard input. Ctrl or Meta sends the Mac Command modifier.
Screen updates can take a moment.</p>
<script>
const screen=document.querySelector('#screen'),state=document.querySelector('#state');
let busy=false, stopped=true, url=null, queue=Promise.resolve();
function pause(error){stopped=true;document.querySelector('#automatic').checked=false;
 document.querySelector('#control').checked=false;held=false;dragging=false;
 state.textContent=error+' — paused; click Connect to retry'}
async function connect(){if(busy)return;try{await queue;
 const r=await fetch('/connect',{method:'POST',body:'{}'});
 if(!r.ok)throw Error(await r.text());stopped=false;await refresh();
 }catch(e){pause(e.message)}}
function send(data){queue=queue.catch(()=>{}).then(async()=>{
 const r=await fetch('/input',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});
 const text=await r.text();if(!r.ok)throw Error(text);state.textContent='Connected';return true;
 }).catch(e=>{pause(e.message);return false});return queue}
async function toggleInput(box){box.disabled=true;
 if(!await send({op:box.checked?8:9}))box.checked=false;box.disabled=false}
async function refresh(){if(busy||stopped)return;busy=true;try{await queue;
 const r=await fetch('/screen');if(!r.ok)throw Error(await r.text());
 const next=URL.createObjectURL(await r.blob());screen.src=next;
 if(url)URL.revokeObjectURL(url);url=next;state.textContent='Connected';
 }catch(e){pause(e.message)}finally{busy=false}}
function point(e){const r=screen.getBoundingClientRect();return {
 x:Math.max(0,Math.min(screen.naturalWidth-1,Math.floor((e.clientX-r.left)*screen.naturalWidth/r.width))),
 y:Math.max(0,Math.min(screen.naturalHeight-1,Math.floor((e.clientY-r.top)*screen.naturalHeight/r.height)))}}
function modifiers(e){return (e.metaKey||e.ctrlKey||document.querySelector('#cmd').checked?256:0)
 |(e.shiftKey||document.querySelector('#shift').checked?512:0)|(e.altKey?2048:0)}
let held=false,moving=false,dragging=false,origin=null,pressModifiers=0;
screen.onpointerdown=e=>{screen.focus();if(e.button||!document.querySelector('#control').checked)return;e.preventDefault();held=true;
 dragging=false;origin=point(e);pressModifiers=modifiers(e);screen.setPointerCapture(e.pointerId)};
screen.onpointerup=e=>{if(!held)return;held=false;
 send({op:dragging?4:11,...point(e),modifiers:modifiers(e)}).then(refresh);dragging=false};
screen.onpointercancel=()=>{if(held&&dragging)send({op:4,...origin});held=false;dragging=false};
screen.onpointermove=e=>{if(!held||moving)return;
 const p=point(e);if(!dragging&&Math.abs(p.x-origin.x)+Math.abs(p.y-origin.y)<3)return;
 if(!dragging){dragging=true;send({op:3,...origin,modifiers:pressModifiers})}moving=true;
 send({op:2,...point(e)}).finally(()=>{moving=false})};
screen.oncontextmenu=e=>e.preventDefault();
const codes={KeyA:0,KeyS:1,KeyD:2,KeyF:3,KeyH:4,KeyG:5,KeyZ:6,KeyX:7,KeyC:8,KeyV:9,
 KeyB:11,KeyQ:12,KeyW:13,KeyE:14,KeyR:15,KeyY:16,KeyT:17,Digit1:18,Digit2:19,Digit3:20,
 Digit4:21,Digit6:22,Digit5:23,Equal:24,Digit9:25,Digit7:26,Minus:27,Digit8:28,Digit0:29,
 BracketRight:30,KeyO:31,KeyU:32,BracketLeft:33,KeyI:34,KeyP:35,Enter:36,KeyL:37,KeyJ:38,
 Quote:39,KeyK:40,Semicolon:41,Backslash:42,Comma:43,Slash:44,KeyN:45,KeyM:46,Period:47,
 Tab:48,Space:49,Backquote:50,Backspace:51,Escape:53,ArrowLeft:123,ArrowRight:124,ArrowDown:125,ArrowUp:126};
const chars={Enter:13,Tab:9,Backspace:8,Escape:27,ArrowLeft:28,ArrowRight:29,ArrowUp:30,ArrowDown:31};
function key(e,down){if(!(e.code in codes)||!document.querySelector('#control').checked)return;e.preventDefault();if(e.repeat)return;
 let mods=modifiers(e);
 let ch=chars[e.code]??(e.key.length===1?e.key.charCodeAt(0):0);if(ch>127)ch=0;
 send({op:5,x:codes[e.code],y:ch,down,modifiers:mods}).then(()=>{if(!down)refresh()})}
screen.onkeydown=e=>key(e,true);screen.onkeyup=e=>key(e,false);
window.onblur=()=>{const box=document.querySelector('#control');if(box.checked){box.checked=false;held=false;send({op:9})}};
setInterval(()=>{if(document.querySelector('#automatic').checked)refresh()},5000);
</script>""".encode(
    "utf-8"
)


class ViewerConnection:
    """Require explicit connection and stop remote traffic after any failure."""

    def __init__(self, controller):
        self.controller = controller
        self.lock = threading.Lock()
        self.paused = "Disconnected; click Connect to contact the Mac"

    def connect(self):
        with self.lock:
            self.controller.close()
            self.paused = None

    def call(self, method, *args):
        with self.lock:
            if self.paused:
                raise ConnectionError(self.paused)
            try:
                result = getattr(self.controller, method)(*args)
                if method == "request" and args[0] == 6:
                    self.paused = "Mac agent stopped; click Connect after relaunching it"
                return result
            except Exception as error:
                self.paused = f"Remote connection paused after {type(error).__name__}: {error}"
                print(self.paused, file=sys.stderr, flush=True)
                self.controller.close()
                raise


def viewer_server(controller, port):
    remote = ViewerConnection(controller)

    class Handler(BaseHTTPRequestHandler):
        def answer(self, status, body, content_type):
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            try:
                if self.path == "/":
                    self.answer(200, PAGE, "text/html; charset=utf-8")
                elif self.path == "/screen":
                    self.answer(200, remote.call("screenshot"), "image/png")
                else:
                    self.answer(404, b"Not found", "text/plain")
            except Exception as error:
                self.answer(503, str(error).encode(), "text/plain")

        def do_POST(self):
            try:
                expected = f"http://127.0.0.1:{self.server.server_port}"
                if self.headers.get("Origin", expected) != expected:
                    raise ValueError("Unexpected browser origin")
                length = int(self.headers.get("Content-Length", 0))
                if self.path not in ("/input", "/connect") or not 0 < length <= 1024:
                    raise ValueError("Invalid input request")
                data = json.loads(self.rfile.read(length))
                if self.path == "/connect":
                    if data != {}:
                        raise ValueError("Invalid connect request")
                    remote.connect()
                    self.answer(200, b"OK", "text/plain")
                    return
                remote.call(
                    "request",
                    int(data["op"]),
                    int(data.get("x", 0)),
                    int(data.get("y", 0)),
                    bool(data.get("down", False)),
                    int(data.get("modifiers", 0)),
                )
                self.answer(200, b"OK", "text/plain")
            except Exception as error:
                self.answer(400, str(error).encode(), "text/plain")

    return ThreadingHTTPServer(("127.0.0.1", port), Handler)


def serve(controller, port):
    with viewer_server(controller, port) as server:
        print(f"Classic Remote viewer: http://127.0.0.1:{server.server_port}", flush=True)
        try:
            server.serve_forever()
        finally:
            controller.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=Path("build/classic-remote-tcp/controller.json"))
    parser.add_argument("--local-directory", type=Path)
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("status")
    sub.add_parser("state")
    sub.add_parser("quit")
    sub.add_parser("enable-input")
    sub.add_parser("disable-input")
    screenshot = sub.add_parser("screenshot")
    screenshot.add_argument("destination", type=Path)
    screenshot.add_argument("--saved", action="store_true")
    click = sub.add_parser("click")
    click.add_argument("x", type=int)
    click.add_argument("y", type=int)
    viewer = sub.add_parser("serve")
    viewer.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    config = json.loads(args.config.read_text())
    config["crypto_directory"] = str(args.config.resolve().parent)
    controller = Controller(config, args.local_directory)
    if controller.tcp and args.action in ("enable-input", "disable-input"):
        parser.error("Use the viewer's Enable input checkbox for a TCP session")
    if args.action == "serve":
        serve(controller, args.port)
    elif args.action == "screenshot":
        args.destination.write_bytes(controller.screenshot(saved=args.saved))
        print(args.destination)
    elif args.action == "click":
        if controller.tcp:
            controller.request(8)
        try:
            controller.request(11, args.x, args.y)
        finally:
            if controller.tcp:
                controller.request(9)
    elif args.action == "state":
        print(json.dumps(controller.input_state(), indent=2))
    else:
        operation = {"quit": 6, "status": 7, "enable-input": 8, "disable-input": 9}
        controller.request(operation[args.action])
        print(
            {
                "quit": "Agent quit",
                "status": "Agent responded",
                "enable-input": "Input enabled",
                "disable-input": "Input disabled",
            }[args.action]
        )
