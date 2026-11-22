# Classic Remote

Original background control agent for classic PowerPC Mac OS, with a Windows
browser controller. The primary transport is a persistent TCP/IP connection to
the Mac on port 6804. Commands and screenshots do not pass through the file
server or SSH. The original AFP mailbox transport remains available for
bootstrap, diagnostics and isolated ROMAW tests.

The first target is the user's Macintosh 5260/100 running Mac OS 8.5. The first
hardware build froze that Mac and was withdrawn. A later black-screen startup
was recovered by resetting PRAM. The cause of the freeze is not yet established.
The builder now uses a writable executable-data section for its GCC PIC image;
the original read-only code/BSS layout was invalid under Apple's PEF rules.
The corrected startup-only and one-shot screen checks have run successfully
on the physical 5260, including a 640 by 480 desktop capture. The persistent
agent also answers signed status and live screen requests over AFP. Fixing
mailbox permissions was necessary for it to publish its session. Keyboard
selection and launching, visible cursor movement, and a click clearing the
Finder selection have also been verified on the physical Mac, including
800 by 600 at 8-bit colour. Window/menu dragging is still unverified. This is a
cooperative control agent, not a complete VNC implementation. Busy applications
that do not yield can delay commands, particularly during menu/window tracking.

The TCP build uses asynchronous MacTCP controls through the `.IPP` driver
provided by Open Transport. It yields while waiting for a connection, incoming
data and outgoing buffers. Network parameter blocks and buffers remain alive
until completion, including cancellation. Create, abort and release now yield
too. Release errors retain the stream and buffers, retrying cooperatively;
the agent cannot exit or create a new stream over driver-owned memory.
Each complete command has a 15-second
receive deadline; a disconnected or invalid client releases held input and
returns the agent to listening. No Apple SDK implementation is included.

## Build and deployment

In WSL install `gcc-powerpc-linux-gnu`, `binutils-powerpc-linux-gnu`,
`python3-pyelftools` and the existing native/MinGW x64 GCC toolchains. Run:

```sh
python3 tools/classic-remote/build.py build/classic-remote-tcp --tcp
```

The build compiles freestanding original C, bridges Linux PPC register arguments
to classic Toolbox transition vectors, and packages the linked image as a PEF
application. Only 32-bit absolute ELF relocations become PEF base relocations;
resolved PC-relative references remain unchanged. Unknown relocations fail the
build. No Apple runtime binaries or SDK files are included.

Copy these two generated files to the Mac, preserving the resource fork:

- `Classic Remote TCP`
- `._Classic Remote TCP` (macOS AppleDouble resource fork and Finder information)

The current shared copy is under `Macintosh:From ROMAW:Z Remote TCP:`.
Launch it once on the Mac. Its SIZE resource marks it as a background-only,
32-bit-clean application; it opens no window. The TCP build requires neither
mailbox files nor an SSH connection. A local-disk installation can run without
the AFP share mounted. Keep `controller.json` and the signing libraries on the
Windows controller, never on the share. Set `host` in the local configuration
to the Mac's address; the current target is `10.0.0.51`.

## Controller

From Windows, with Python available:

```powershell
python tools/classic-remote/controller.py serve
```

Open `http://127.0.0.1:8765` and click **Connect** when the Mac agent is ready.
The viewer starts disconnected and makes no remote requests until that click.
Screen updates are manual by default; **Auto refresh (5 seconds)** is optional.
Any remote failure pauses both capture and input at the server, so even an old
browser tab cannot repeatedly reconnect. Another explicit **Connect** is needed
to retry. Closing the local server also stops all polling.

Click the screen to focus keyboard input. Ctrl/Meta
maps to the classic Command modifier; optional Command/Shift checkboxes also
work. The initial key map is US ASCII. The viewer supports screen capture,
mouse motion while dragging, button presses/releases, key presses/releases,
and quitting the Mac agent. Captures support indexed 1/2/4/8-bit, RGB555 and
32-bit direct-color main displays up to 1024 by 768. Secondary displays are not
captured. Capture conversion uses a small standard-library PNG encoder.
Input starts disabled. Select **Enable input** before controlling the Mac;
clearing that checkbox or leaving the viewer releases remote-held input.
Only one TCP controller can connect at a time. Stop the viewer server before
using separate command-line controls. After its browser tab closes, an idle
connection expires within 15 seconds. Failed input commands are never retried
automatically, so loss of a reply cannot duplicate a click or keystroke.

Other commands:

```powershell
python tools/classic-remote/controller.py status
python tools/classic-remote/controller.py state
python tools/classic-remote/controller.py screenshot build/mac-screen.png
python tools/classic-remote/controller.py click 100 120
python tools/classic-remote/controller.py quit
```

The default configuration is `build/classic-remote-tcp/controller.json`.
`--config` selects another pairing configuration. A TCP command-line click
explicitly arms input for that click and disarms it afterwards. The browser
uses its checkbox instead. Closing the TCP connection always disarms input.

## Mailbox fallback

Build `build.py build/classic-remote` without `--tcp`. Copy `Classic Remote`,
`._Classic Remote`, `Remote Command` and `Remote Response` into one AFP folder.
Set the matching local configuration's `folder` to its server path and `server`
to the SSH host. Use `controller.py --config build/classic-remote/controller.json`
for these commands. `--local-directory` replaces SSH with a local mailbox for
ROMAW regression tests.

Both mailboxes must be writable by the AFP login. A backing SMB mount can
report a different owner: this deployment uses mount UID 1024 and AFP UID 1000.
Set the two mailboxes to mode `0666`, or grant equivalent ACL access. The agent
retries a failed startup publication for up to a minute, yielding between
attempts, then exits. Stop an existing agent before replacing its application
or clearing its mailboxes. In this transport, `enable-input` and `disable-input`
can be issued as separate command-line operations.

## Authentication and limits

Commands carry Ed25519 signatures over their complete 256-byte payload. Only
the public verification key is in the Mac application; the private signing key
stays in the ignored local build configuration. Commands bind
to a fresh application session (uptime, ticks and stack address) and an increasing
sequence. Invalid signatures, other-session commands and repeated/older sequences
are rejected. An invalid TCP command closes the connection; invalid mailbox
commands are ignored. Pointer coordinates and key codes are bounded. Held inputs
are released after two seconds without an input update, and on normal shutdown.

TCP command authentication does not encrypt screenshots: use a trusted LAN.
For the fallback, use a trusted AFP server and volume; screen captures are
plain files there. Someone able to replace the app can replace its behavior.
The local browser server listens only on loopback and rejects cross-origin
input requests. This does not turn an untrusted file server into a secure one.

Low-memory cursor/button/event-queue access follows the classic Macintosh ABI.
The cursor task updates the final `Mouse` position itself; writing it before
the task runs prevents the requested movement from being drawn. Simple clicks
queue both button events before yielding, so the release does not depend on
the foreground application yielding during button tracking. Motion renews the
remote button timeout. The fixed `state` response reports mouse positions,
cursor flags, button state and KeyMap for diagnosis.
The MiniVNC author's [technical discussion](https://github.com/marciot/mac-minivnc)
provided a behavioral reference; its implementation was not copied. Dragging
and input during a busy application's tracking loop require further hardware
testing.

See `third_party/monocypher/README.md` for the cryptography provenance/license.

## Validation

Run packaging checks in WSL and isolated runtime checks on Windows:

```sh
python3 tools/classic-remote/test_layout.py
```

```powershell
python tools/classic-remote/test_agent.py
python tools/classic-remote/test_tcp.py
python tools/classic-remote/test_viewer.py
```

The runtime tests launch private ROMAW processes in JIT and interpreter
modes. They check recovery when the response file is initially missing,
session publication, signed commands, rejection of replayed/tampered commands,
screen encoding, input arming, mouse/key commands and clean shutdown. They do
not send input to the physical Mac or the Windows desktop.

TCP loopback tests check signed commands, persistent connections, fragmented
headers/payloads, bounded response lengths, session/sequence changes, truncation
and loss of acknowledgements without replaying input. Viewer tests use a fake
controller and real loopback HTTP requests to check disconnected startup,
concurrent stale clients, failure latching, explicit reconnection and origin
checks without contacting a Mac. Run the real C transport against an
asynchronous driver fixture under Linux ASan/UBSan:

```sh
gcc -m32 -g -O1 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -fno-builtin -fsanitize=address,undefined -Ibuild/classic-remote-tcp \
  -Itools/classic-remote/third_party/monocypher \
  tools/classic-remote/test_tcp.c -Wl,--gc-sections \
  -o build/classic-remote-tcp/transport-test
build/classic-remote-tcp/transport-test
```

The driver fixture rejects synchronous controls and checks delayed cancellation,
failed abort/release, buffer ownership, and 1,000 repeated stream lifecycles.
This does not establish that the physical Open Transport driver cannot hang.

Physical TCP validation on the 5260/100 with Mac OS 8.5 includes status,
800x600x8 capture, visible mouse movement, click selection, KeyMap updates,
reconnection and held-key/input cleanup on disconnect. Ten status round trips
had a median of 0.097 seconds and a maximum of 0.161 seconds; one screen capture
took 3.09 seconds. These are local measurements, not general performance claims.
A later user-reported freeze required a power cycle. Its cause has not been
isolated; these short transport checks do not establish long-running stability.
The local viewer was stopped, and no updated agent was automatically launched.

For staged hardware diagnosis, `build.py --diagnostic` builds a startup-only
check; `build.py --screen-check` builds a one-shot event-yield/screenshot check.
Both quit without processing remote commands. To retrieve the latter's result,
use `controller.py --config PATH screenshot --saved OUTPUT.png`.
