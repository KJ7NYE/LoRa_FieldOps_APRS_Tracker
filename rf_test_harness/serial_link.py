"""
Byte-oriented serial reader thread + a generic pub/sub EventBus.

The firmware's SETUP-mode prompt has no trailing newline (see constants.py),
so a naive readline()-based reader would hang forever waiting for one. This
module instead owns raw byte reads on a background thread, splits complete
newline-terminated lines into timestamped LogEvents, and separately detects
the bare "> " prompt tail as a distinct PromptEvent -- both published onto a
shared EventBus that every other module (device_session, phases, aprs_is_tap)
consumes via a single primitive: wait_for(channel, predicate, timeout).
"""

from __future__ import annotations

import threading
import time
from collections import defaultdict, deque
from dataclasses import dataclass
from typing import Callable, Optional

from constants import ANSI_ESCAPE_RE, PROMPT_LINE_STRIPPED, PROMPT_TAIL


@dataclass(frozen=True)
class LogEvent:
    ts: float       # time.monotonic() at receipt -- used for ordering/timeouts
    wall_ts: float  # time.time() at receipt -- used for report timestamps
    channel: str
    raw: str


class EventBus:
    """Generic 'packet observed on channel X matching predicate Y within
    timeout Z' primitive, shared by every phase and future extension."""

    def __init__(self, history: int = 4000):
        self._lock = threading.Lock()
        self._cond = threading.Condition(self._lock)
        self._channels: dict[str, deque[LogEvent]] = defaultdict(lambda: deque(maxlen=history))

    def publish(self, channel: str, event: LogEvent) -> None:
        with self._cond:
            self._channels[channel].append(event)
            self._cond.notify_all()

    def wait_for(
        self,
        channel: str,
        predicate: Callable[[LogEvent], bool],
        timeout: float,
        since: Optional[float] = None,
    ) -> Optional[LogEvent]:
        deadline = time.monotonic() + timeout
        with self._cond:
            while True:
                for ev in self._channels[channel]:
                    if since is not None and ev.ts < since:
                        continue
                    if predicate(ev):
                        return ev
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                self._cond.wait(timeout=remaining)

    def collect(
        self,
        channel: str,
        since: Optional[float] = None,
        until: Optional[float] = None,
    ) -> list[LogEvent]:
        with self._lock:
            return [
                ev
                for ev in self._channels[channel]
                if (since is None or ev.ts >= since) and (until is None or ev.ts <= until)
            ]


class SerialReader(threading.Thread):
    """Owns all reads for one serial connection for the life of the session.
    No other code should call ser.read*() directly once this thread is running."""

    def __init__(self, ser, bus: EventBus, channel: str, prompt_channel: str):
        super().__init__(daemon=True, name=f"SerialReader-{channel}")
        self._ser = ser
        self._bus = bus
        self._channel = channel
        self._prompt_channel = prompt_channel
        self._buf = bytearray()
        self._stop = threading.Event()

    def stop(self) -> None:
        self._stop.set()

    def run(self) -> None:
        while not self._stop.is_set():
            try:
                n = self._ser.in_waiting or 1
                chunk = self._ser.read(n)
            except Exception:
                if self._stop.is_set():
                    return
                time.sleep(0.1)
                continue
            if not chunk:
                continue
            self._buf.extend(chunk)
            self._drain_lines()
            self._check_prompt_tail()

    def _drain_lines(self) -> None:
        while True:
            idx = self._buf.find(b"\n")
            if idx == -1:
                break
            line_bytes = bytes(self._buf[:idx])
            del self._buf[: idx + 1]
            line = line_bytes.decode("utf-8", errors="replace").rstrip("\r")
            # ESP32 targets use the real upstream esp-logger library, which
            # ANSI-colors its output (nRF52's shim doesn't). Strip it here so
            # every consumer -- predicates, evidence capture, reports -- sees
            # plain text regardless of which platform sent it.
            line = ANSI_ESCAPE_RE.sub("", line)
            if line.strip() == PROMPT_LINE_STRIPPED:
                # Defensive: a bare ">" drained as an ordinary line means a
                # prompt arrived mid-stream rather than as the dangling tail
                # _check_prompt_tail() below expects (see constants.py --
                # device_session writes a single '\r' terminator specifically
                # to avoid the firmware's double-prompt quirk that used to
                # make this the *common* case; this remains a fallback).
                now = time.monotonic()
                self._bus.publish(
                    self._prompt_channel, LogEvent(now, time.time(), self._prompt_channel, ">")
                )
                continue
            if line == "":
                continue
            now = time.monotonic()
            self._bus.publish(self._channel, LogEvent(now, time.time(), self._channel, line))

    def _check_prompt_tail(self) -> None:
        # After _drain_lines, self._buf holds only bytes received since the
        # last '\n'. A bare SETUP prompt ("\n> ") arrives as an empty line
        # (consumed above) followed by the un-terminated tail "> ".
        if bytes(self._buf) == PROMPT_TAIL:
            now = time.monotonic()
            self._bus.publish(self._prompt_channel, LogEvent(now, time.time(), self._prompt_channel, ">"))
            self._buf.clear()
