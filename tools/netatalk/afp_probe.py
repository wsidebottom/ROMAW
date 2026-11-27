#!/usr/bin/env python3
# +===================================================================
#   File:      afp_probe.py
#
#   Summary:   Verify AFP over TCP status and session negotiation.
#
#   Classes:   DSI, ProtocolError.
#
#   Functions: server_strings, guest_volumes, main.
#
#   Origin:    ROMAW interoperability probe, using the public DSI wire layout.
#
#   Copyright and Legal notices: See COPYING.
# ===================================================================+
"""Check a Netatalk endpoint without credentials or volume changes."""

import argparse
import socket
import struct

HEADER = struct.Struct(">BBHiII")
CLOSE, COMMAND, STATUS, OPEN, TICKLE, ATTENTION = 1, 2, 3, 4, 5, 8
ATTENTION_QUANTUM = 2
MAX_REPLY = 1 << 20


class ProtocolError(Exception):
    """The peer rejected the request or returned an invalid DSI frame."""


class DSI:
    def __init__(self, connection):
        self.connection = connection
        self.request_id = 0

    def _read(self, size):
        output = bytearray()
        while len(output) < size:
            block = self.connection.recv(size - len(output))
            if not block:
                raise ProtocolError("peer closed the connection before the frame ended")
            output.extend(block)
        return output

    def _send(self, flags, command, request_id, payload=b""):
        self.connection.sendall(HEADER.pack(flags, command, request_id, 0, len(payload), 0) + payload)

    def request(self, command, payload=b""):
        self.request_id = self.request_id % 65535 + 1
        self._send(0, command, self.request_id, payload)
        # Bound unsolicited frames independently of the socket's idle timeout.
        for _ in range(64):
            flags, function, number, status, size, reserved = HEADER.unpack(self._read(HEADER.size))
            if flags > 1 or reserved or size > MAX_REPLY:
                raise ProtocolError("invalid DSI header")
            data = self._read(size)
            if flags == 0:
                if function == CLOSE:
                    raise ProtocolError("server closed the DSI session")
                if function == TICKLE and not size:
                    self._send(1, TICKLE, number)
                elif function != ATTENTION:
                    raise ProtocolError(f"unexpected server request {function}")
                continue
            if function != command or number != self.request_id:
                raise ProtocolError("DSI reply does not match the request")
            if status:
                raise ProtocolError(f"DSI command {command} returned {status}")
            return data
        raise ProtocolError("too many unsolicited DSI frames")

    def open_session(self):
        options = self.request(OPEN, struct.pack(">BBI", 1, 4, ATTENTION_QUANTUM))
        offset = 0
        quantum = None
        while offset < len(options):
            if offset + 2 > len(options):
                raise ProtocolError("truncated session option")
            kind, size = options[offset : offset + 2]
            offset += 2
            if offset + size > len(options):
                raise ProtocolError("truncated session option value")
            if kind == 0:
                if size != 4 or quantum is not None:
                    raise ProtocolError("invalid server quantum option")
                quantum = struct.unpack_from(">I", options, offset)[0]
            offset += size
        if not quantum:
            raise ProtocolError("server did not advertise a request quantum")
        return quantum

    def close_session(self):
        self.request_id = self.request_id % 65535 + 1
        self._send(0, CLOSE, self.request_id)


def server_strings(reply, offset):
    """Read a counted list of Pascal strings at a server-info offset."""
    if not 0 < offset < len(reply):
        raise ProtocolError("invalid server-info list offset")
    count = reply[offset]
    offset += 1
    values = []
    for _ in range(count):
        if offset >= len(reply):
            raise ProtocolError("truncated server-info list")
        length = reply[offset]
        offset += 1
        if length > len(reply) - offset:
            raise ProtocolError("truncated server-info string")
        values.append(bytes(reply[offset : offset + length]))
        offset += length
    return values


def guest_volumes(session, status, selected=None):
    version_offset, uam_offset = struct.unpack_from(">HH", status, 2)
    versions = server_strings(status, version_offset)
    uams = server_strings(status, uam_offset)
    method = b"No User Authent"
    if method not in uams:
        raise ProtocolError("server requires an authenticated UAM")
    version = next(
        (value for value in (b"AFP3.4", b"AFP3.3", b"AFP3.2", b"AFP3.1", b"AFPX03", b"AFP2.2") if value in versions),
        None,
    )
    if version is None:
        raise ProtocolError("server offers no supported AFP version")
    session.request(COMMAND, bytes([18, len(version)]) + version + bytes([len(method)]) + method)
    try:
        response = session.request(COMMAND, bytes([16, 0]))
        if len(response) < 5:
            raise ProtocolError("truncated FPGetSrvrParms reply")
        offset = 5
        volumes = []
        selected_name = None
        encoding = "mac_roman" if version == b"AFP2.2" else "utf-8"
        for _ in range(response[4]):
            if len(response) - offset < 2:
                raise ProtocolError("truncated AFP volume entry")
            flags, length = response[offset : offset + 2]
            offset += 2
            if length > len(response) - offset:
                raise ProtocolError("truncated AFP volume name")
            wire_name = bytes(response[offset : offset + length])
            name = wire_name.decode(encoding, errors="replace")
            volumes.append((name, flags))
            if name == selected:
                selected_name = wire_name
            offset += length
        if selected is not None:
            if selected_name is None:
                raise ProtocolError(f"volume {selected!r} is absent from the guest list")
            # Request just the server's volume ID (bitmap bit 5).
            request = bytes([24, 0, 0, 32, len(selected_name)]) + selected_name
            reply = session.request(COMMAND, request)
            if len(reply) != 4 or reply[:2] != bytes([0, 32]):
                raise ProtocolError("unexpected FPOpenVol reply")
            volume_id = bytes(reply[2:4])
            session.request(COMMAND, bytes([2, 0]) + volume_id)
            print(f"Opened and closed AFP volume {selected!r} (ID {int.from_bytes(volume_id, 'big')})")
        return volumes
    finally:
        session.request(COMMAND, bytes([20, 0]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server")
    parser.add_argument("--port", type=int, default=548)
    parser.add_argument("--guest", action="store_true", help="log in as guest and list visible volumes")
    parser.add_argument("--volume", help="with --guest, open and close the named AFP volume")
    args = parser.parse_args()
    if args.volume and not args.guest:
        parser.error("--volume requires --guest")
    try:
        # GetStatus is a separate connection. Netatalk closes it after replying.
        with socket.create_connection((args.server, args.port), timeout=5) as connection:
            status = DSI(connection).request(STATUS)
            if len(status) < 11 or 11 + status[10] > len(status):
                raise ProtocolError("truncated AFP server information")
            name = status[11 : 11 + status[10]].decode("mac_roman")
            print(f"AFP server {name!r}")
        with socket.create_connection((args.server, args.port), timeout=5) as connection:
            session = DSI(connection)
            print(f"DSI session opened with a {session.open_session()}-byte server quantum")
            try:
                if args.guest:
                    for name, flags in guest_volumes(session, status, args.volume):
                        print(f"AFP volume {name!r} (flags 0x{flags:02x})")
            finally:
                session.close_session()
    except (OSError, ProtocolError) as error:
        parser.exit(1, f"AFP probe failed: {error}\n")


if __name__ == "__main__":
    main()
