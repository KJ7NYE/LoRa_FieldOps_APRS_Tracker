"""
Independent, read-only APRS-IS TCP tap.

This deliberately connects to a *different* server pool than the iGate's own
configured APRS-IS server (data/tracker_conf.json default: rotate.aprs.net)
so that Phase 3 is a genuine independent witness -- confirming the packet
actually reached the public network, decoupled from what the iGate's own
log claims it did.

Login uses passcode -1 (receive-only), matching a plain read-only monitor.
"""

from __future__ import annotations

import socket
import threading
import time
from typing import Optional

from constants import APRS_IS_READONLY_PASSCODE, DEFAULT_APRS_IS_TAP_HOST, DEFAULT_APRS_IS_TAP_PORT
from serial_link import EventBus, LogEvent


class APRSISTapError(Exception):
    pass


class APRSISTap:
    def __init__(
        self,
        bus: EventBus,
        channel: str = "aprs_is_tap",
        host: str = DEFAULT_APRS_IS_TAP_HOST,
        port: int = DEFAULT_APRS_IS_TAP_PORT,
    ):
        self.bus = bus
        self.channel = channel
        self.host = host
        self.port = port
        self.login_result: Optional[str] = None
        self._sock: Optional[socket.socket] = None
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()

    def connect(
        self,
        callsign: str,
        filter_str: str,
        vers: str = "rf_test_harness 1.0",
        connect_timeout: float = 10.0,
        login_timeout: float = 10.0,
    ) -> None:
        self._sock = socket.create_connection((self.host, self.port), timeout=connect_timeout)
        self._sock.settimeout(None)
        self._thread = threading.Thread(target=self._run, daemon=True, name="APRSISTapReader")
        self._thread.start()

        login_line = f"user {callsign} pass {APRS_IS_READONLY_PASSCODE} vers {vers} filter {filter_str}\r\n"
        t0 = time.monotonic()
        self._sock.sendall(login_line.encode("ascii", errors="replace"))
        ev = self.bus.wait_for(
            self.channel, lambda e: e.raw.startswith("# logresp"), timeout=login_timeout, since=t0
        )
        if ev is None:
            raise APRSISTapError(
                f"no login response from {self.host}:{self.port} within {login_timeout}s"
            )
        self.login_result = ev.raw

    def close(self) -> None:
        self._stop.set()
        if self._sock is not None:
            try:
                self._sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self._sock.close()
            self._sock = None
        if self._thread is not None:
            self._thread.join(timeout=1.0)
            self._thread = None

    def __enter__(self) -> "APRSISTap":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def _run(self) -> None:
        buf = bytearray()
        while not self._stop.is_set():
            try:
                chunk = self._sock.recv(4096)
            except OSError:
                return
            if not chunk:
                return
            buf.extend(chunk)
            while True:
                idx = buf.find(b"\n")
                if idx == -1:
                    break
                line_bytes = bytes(buf[:idx])
                del buf[: idx + 1]
                line = line_bytes.decode("utf-8", errors="replace").rstrip("\r")
                if line == "":
                    continue
                now = time.monotonic()
                self.bus.publish(self.channel, LogEvent(now, time.time(), self.channel, line))
