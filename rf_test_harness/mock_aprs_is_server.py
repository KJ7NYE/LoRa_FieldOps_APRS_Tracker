"""
Mock APRS-IS server: a minimal local TCP server standing in for the real
public APRS-IS network, for phase12_is_downlink's IS->RF downlink test.

On connect: reads the login line, replies with any '#'-prefixed line
containing "verified" -- confirmed the firmware's *sole* passcode-validity
check (src/aprs_is_utils.cpp:121-133: line.startsWith("#") then
line.indexOf("verified") != -1). No real passcode needed.

After that, push_message() writes an arbitrary line directly into the
still-open connection -- the iGate's listenAPRSIS() reads it exactly like
downlink traffic from a real server.

Must bind 0.0.0.0, not 127.0.0.1: the iGate is a separate physical device on
the LAN dialing Config.aprsIS.server itself, not a connection from this
same process.
"""

from __future__ import annotations

import socket
import threading
from typing import Optional


class MockAPRSISServer:
    def __init__(self, port: int = 0, bind_host: str = "0.0.0.0"):
        self.port = port
        self.bind_host = bind_host
        self._server_sock: Optional[socket.socket] = None
        self._client_sock: Optional[socket.socket] = None
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self._client_connected = threading.Event()
        self.last_login_line: Optional[str] = None

    def start(self) -> int:
        """Bind and start accepting a connection in the background. Returns
        the actual port (useful if port=0 was requested for an
        OS-assigned free one)."""
        self._server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server_sock.bind((self.bind_host, self.port))
        self._server_sock.listen(1)
        self.port = self._server_sock.getsockname()[1]
        self._thread = threading.Thread(target=self._accept_loop, daemon=True, name="MockAPRSISServer")
        self._thread.start()
        return self.port

    def _accept_loop(self) -> None:
        self._server_sock.settimeout(1.0)
        while not self._stop.is_set():
            try:
                conn, _addr = self._server_sock.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            self._handle_client(conn)

    def _handle_client(self, conn: socket.socket) -> None:
        conn.settimeout(5.0)
        buf = bytearray()
        try:
            while b"\n" not in buf and not self._stop.is_set():
                chunk = conn.recv(4096)
                if not chunk:
                    return
                buf.extend(chunk)
            line, _, _rest = buf.partition(b"\n")
            self.last_login_line = line.decode("ascii", errors="replace").rstrip("\r")
            conn.sendall(b"# logresp TEST verified, server rf_test_harness mock\r\n")
        except OSError:
            return

        self._client_sock = conn
        self._client_connected.set()

        # Keep the connection open so push_message() can write to it later.
        # This thread just drains (and discards) anything further the iGate
        # sends so the socket doesn't back up; sending from push_message()
        # on a different thread while this one blocks in recv() is safe.
        conn.settimeout(None)
        while not self._stop.is_set():
            try:
                chunk = conn.recv(4096)
                if not chunk:
                    break
            except OSError:
                break

    def wait_for_client(self, timeout: float = 30.0) -> bool:
        return self._client_connected.wait(timeout)

    def push_message(self, line: str) -> None:
        assert self._client_sock is not None, "MockAPRSISServer: no client connected yet"
        self._client_sock.sendall((line + "\r\n").encode("ascii", errors="replace"))

    def stop(self) -> None:
        self._stop.set()
        if self._client_sock is not None:
            try:
                self._client_sock.close()
            except OSError:
                pass
            self._client_sock = None
        if self._server_sock is not None:
            try:
                self._server_sock.close()
            except OSError:
                pass
            self._server_sock = None
        if self._thread is not None:
            self._thread.join(timeout=2.0)
            self._thread = None

    def __enter__(self) -> "MockAPRSISServer":
        self.start()
        return self

    def __exit__(self, *exc) -> None:
        self.stop()
