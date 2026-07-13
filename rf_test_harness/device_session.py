"""
DeviceSession: drives one device's USB serial port through the firmware's
KISS / SETUP / LOG mode state machine (see src/serial_setup.cpp).

    KISS (default) --"setup"--> SETUP --"exit"--> KISS
    KISS (default) --"log"-->   LOG   --"exit"--> KISS

SETUP cannot go directly to LOG; callers must exit_setup() first.
"""

from __future__ import annotations

import time
from typing import Optional

import serial

from ax25_kiss import encode_kiss_frame
from constants import (
    DEFAULT_BAUD,
    DEFAULT_MODE_SWITCH_TIMEOUT,
    DEFAULT_SETUP_CMD_TIMEOUT,
    DIRTY_EXIT_REFUSED_MARKER,
    DISCARD_TRIGGER,
    EXIT_TRIGGER,
    LOG_MODE_BANNER,
    LOG_TRIGGER,
    RETURN_TO_KISS_MARKER,
    SETUP_BANNER_MARKER,
    SETUP_TRIGGER,
)
from serial_link import EventBus, SerialReader


class ModeSwitchTimeout(Exception):
    """A mode-transition sentinel never arrived within the timeout."""


class DirtyConfigError(Exception):
    """SETUP refused to exit because of unsaved config changes."""


class SerialCommandTimeout(Exception):
    def __init__(self, device: str, cmd: str, buffered_lines: list[str]):
        self.device = device
        self.cmd = cmd
        self.buffered_lines = buffered_lines
        super().__init__(f"{device}: no response to '{cmd}' -- buffered so far: {buffered_lines!r}")


class DeviceSession:
    def __init__(
        self,
        port: str,
        name: str,
        bus: EventBus,
        baud: int = DEFAULT_BAUD,
        dtr: Optional[bool] = True,
        rts: Optional[bool] = False,
    ):
        """dtr/rts: steady-state control-line values to hold for the life of
        the connection, or None to leave them untouched (OS/driver default).

        heltec_t114 (nRF52840 TinyUSB CDC) needs dtr=True held continuously
        or it never flushes output (variants/heltec_t114/platformio.ini:17-18)
        -- that's this class's default.

        Do NOT use that default for an ESP32 board wired with the classic
        auto-reset/bootstrap circuit (RTS/DTR -> EN/GPIO0 via transistors):
        on heltec_v3_433_aprs specifically, BUTTON_PIN is GPIO0
        (variants/heltec_v3_433_aprs/board_pinout.h:43), and holding a
        control line asserted for the whole session reads to the firmware as
        a sustained USR-button hold -- past the 8s runtime threshold
        (src/main.cpp:208) that starts WiFi AP mode, mid-test. Pass
        dtr=False, rts=False for that board.
        """
        self.port = port
        self.name = name
        self.bus = bus
        self.baud = baud
        self.dtr = dtr
        self.rts = rts
        self.channel = name
        self.prompt_channel = f"{name}_prompt"
        self.mode = "unknown"  # "unknown" | "kiss" | "setup" | "log"
        self._ser: Optional[serial.Serial] = None
        self._reader: Optional[SerialReader] = None

    # ---------------- lifecycle ----------------

    def open(self) -> None:
        self._ser = serial.Serial()
        self._ser.port = self.port
        self._ser.baudrate = self.baud
        self._ser.timeout = 0.2
        if self.dtr is not None:
            self._ser.dtr = self.dtr
        if self.rts is not None:
            self._ser.rts = self.rts
        self._ser.open()
        # Some platforms reset DTR/RTS on open() -- reassert after.
        if self.dtr is not None:
            self._ser.dtr = self.dtr
        if self.rts is not None:
            self._ser.rts = self.rts
        self._reader = SerialReader(self._ser, self.bus, self.channel, self.prompt_channel)
        self._reader.start()

    def close(self) -> None:
        if self._reader is not None:
            self._reader.stop()
            self._reader.join(timeout=1.0)
            self._reader = None
        if self._ser is not None and self._ser.is_open:
            self._ser.close()
        self._ser = None

    def __enter__(self) -> "DeviceSession":
        self.open()
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def _reopen_port(self, settle_delay: float = 8.0) -> None:
        """Close and reopen the serial connection -- used after the device
        reboots (via 'discard' or 'reboot') and the old OS-level handle
        becomes invalid; writing to it fails with a WriteFile/permission
        error rather than a clean disconnect (found via 'discard' during
        resync_to_kiss -- the device really does drop off the bus, it's not
        just slow to respond on the same handle)."""
        if self._reader is not None:
            self._reader.stop()
            self._reader.join(timeout=1.0)
            self._reader = None
        if self._ser is not None and self._ser.is_open:
            try:
                self._ser.close()
            except Exception:
                pass
        self._ser = None
        self.mode = "unknown"
        time.sleep(settle_delay)
        self.open()

    # ---------------- low-level write ----------------

    def _write_line(self, text: str) -> float:
        # A single '\r' terminator only. serial_setup.cpp's input loop treats
        # '\r' and '\n' as two *independent* line terminators (each triggers
        # its own "\r\n" echo, and -- if the typed buffer is already empty --
        # its own prompt reprint), so sending "\r\n" makes the device print
        # every prompt twice and creates a real race between this command's
        # completion and the next command's write. A lone '\r' (which
        # SERIAL_SETUP.md confirms is accepted on its own) avoids the quirk
        # entirely instead of trying to parse around it.
        assert self._ser is not None, f"{self.name}: serial port not open"
        t0 = time.monotonic()
        self._ser.write((text + "\r").encode("ascii", errors="replace"))
        self._ser.flush()
        return t0

    # ---------------- mode transitions ----------------

    def resync_to_kiss(self, timeout: float = DEFAULT_MODE_SWITCH_TIMEOUT) -> None:
        """Recover to a known KISS-mode state regardless of what mode a prior
        session (manual or a crashed harness run) left the device in.

        Strategy: 'exit' is a recognized command from both SETUP and LOG and
        returns to KISS; from KISS it is silently ignored as noise. If SETUP
        refuses the exit (unsaved changes from a prior aborted run), discard
        those changes -- an automated recovery step should never silently
        'save' someone else's half-typed edits.
        """
        t0 = self._write_line(EXIT_TRIGGER)
        ev = self.bus.wait_for(
            self.channel, lambda e: RETURN_TO_KISS_MARKER in e.raw, timeout=timeout, since=t0
        )
        if ev is not None:
            self.mode = "kiss"
            return

        # No exit confirmation: either already in KISS (where 'exit' is a
        # no-op), or in SETUP with a dirty flag that refused the exit request
        # before we could observe it. Probe with a blank line + prompt wait.
        t1 = self._write_line("")
        prompt_ev = self.bus.wait_for(self.prompt_channel, lambda e: True, timeout=1.5, since=t1)
        if prompt_ev is None:
            # No prompt appeared -- we were already in KISS.
            self.mode = "kiss"
            return

        # The "unsaved changes" refusal, if any, was printed in response to
        # the *original* exit attempt above (timestamped near t0) -- not the
        # blank-line probe (t1), which only reprints the prompt and produces
        # no new error line of its own. Scan from t0, not t1, or a dirty
        # session is never detected and this falls through to a second
        # doomed "exit" attempt that just times out (real bug, found when a
        # crashed phase actually left a device dirty for the first time).
        lines = self.bus.collect(self.channel, since=t0, until=prompt_ev.ts)
        if any(DIRTY_EXIT_REFUSED_MARKER in l.raw for l in lines):
            self._write_line(DISCARD_TRIGGER)
            # discard triggers a device reboot -- the old serial handle goes
            # invalid (confirmed: writing to it afterward raises a
            # WriteFile/permission error, not a clean timeout), so this must
            # reconnect, not just wait.
            self._reopen_port()
            self.mode = "kiss"
            return

        # We're in a clean SETUP session that just hadn't printed its exit
        # confirmation yet for some reason -- try exiting again.
        t2 = self._write_line(EXIT_TRIGGER)
        ev = self.bus.wait_for(
            self.channel, lambda e: RETURN_TO_KISS_MARKER in e.raw, timeout=timeout, since=t2
        )
        if ev is None:
            raise ModeSwitchTimeout(f"{self.name}: could not resync to KISS mode within {timeout}s")
        self.mode = "kiss"

    def enter_setup(self, timeout: float = DEFAULT_MODE_SWITCH_TIMEOUT) -> None:
        if self.mode != "kiss":
            raise RuntimeError(f"{self.name}: enter_setup() requires kiss mode, currently {self.mode}")
        t0 = self._write_line(SETUP_TRIGGER)
        banner_ev = self.bus.wait_for(
            self.channel, lambda e: SETUP_BANNER_MARKER in e.raw, timeout=timeout, since=t0
        )
        if banner_ev is None:
            raise ModeSwitchTimeout(f"{self.name}: no SETUP banner within {timeout}s")
        prompt_ev = self.bus.wait_for(self.prompt_channel, lambda e: True, timeout=timeout, since=t0)
        if prompt_ev is None:
            raise ModeSwitchTimeout(f"{self.name}: no SETUP prompt within {timeout}s")
        self.mode = "setup"

    def exit_setup(self, timeout: float = DEFAULT_MODE_SWITCH_TIMEOUT) -> None:
        if self.mode != "setup":
            raise RuntimeError(f"{self.name}: exit_setup() requires setup mode, currently {self.mode}")
        t0 = self._write_line(EXIT_TRIGGER)
        ev = self.bus.wait_for(
            self.channel,
            lambda e: RETURN_TO_KISS_MARKER in e.raw or DIRTY_EXIT_REFUSED_MARKER in e.raw,
            timeout=timeout,
            since=t0,
        )
        if ev is None:
            raise ModeSwitchTimeout(f"{self.name}: no response to exit within {timeout}s")
        if DIRTY_EXIT_REFUSED_MARKER in ev.raw:
            raise DirtyConfigError(
                f"{self.name}: exit refused, unsaved config changes pending (save or discard)"
            )
        self.mode = "kiss"

    def enter_log(self, timeout: float = DEFAULT_MODE_SWITCH_TIMEOUT) -> None:
        if self.mode != "kiss":
            raise RuntimeError(f"{self.name}: enter_log() requires kiss mode, currently {self.mode}")
        t0 = self._write_line(LOG_TRIGGER)
        ev = self.bus.wait_for(self.channel, lambda e: LOG_MODE_BANNER in e.raw, timeout=timeout, since=t0)
        if ev is None:
            raise ModeSwitchTimeout(f"{self.name}: no LOG banner within {timeout}s")
        self.mode = "log"

    def exit_log(self, timeout: float = DEFAULT_MODE_SWITCH_TIMEOUT) -> None:
        if self.mode != "log":
            raise RuntimeError(f"{self.name}: exit_log() requires log mode, currently {self.mode}")
        t0 = self._write_line(EXIT_TRIGGER)
        ev = self.bus.wait_for(
            self.channel, lambda e: RETURN_TO_KISS_MARKER in e.raw, timeout=timeout, since=t0
        )
        if ev is None:
            raise ModeSwitchTimeout(f"{self.name}: no exit confirmation within {timeout}s")
        self.mode = "kiss"

    def reboot_and_reconnect(
        self, settle_delay: float = 8.0, timeout: float = DEFAULT_MODE_SWITCH_TIMEOUT
    ) -> None:
        """Send 'reboot' from SETUP mode and reconnect once the device comes
        back up. Fire-and-forget on the write side: 'reboot' never produces
        a prompt (the device resets before it could print one), so this
        can't go through send_setup_cmd(), which would just time out
        waiting for one. settle_delay is a fixed wait for the reboot to
        complete before attempting to reopen the port -- conservative by
        default; tune down once measured against real hardware."""
        if self.mode != "setup":
            raise RuntimeError(f"{self.name}: reboot_and_reconnect() requires setup mode, currently {self.mode}")
        self._write_line("reboot")
        self._reopen_port(settle_delay)
        self.resync_to_kiss(timeout)

    def ensure_setup_mode(self, timeout: float = DEFAULT_MODE_SWITCH_TIMEOUT) -> None:
        """Transition to SETUP mode from whatever mode we're currently in.

        Phases run sequentially within one attempt and a device's mode
        persists across them (e.g. phase1_rf_link leaves the tracker in LOG
        mode) -- a phase needing SETUP access can't assume it's starting
        from KISS."""
        if self.mode == "setup":
            return
        if self.mode == "log":
            self.exit_log(timeout)
        self.enter_setup(timeout)

    def ensure_log_mode(self, timeout: float = DEFAULT_MODE_SWITCH_TIMEOUT) -> None:
        """Transition to LOG mode from whatever mode we're currently in."""
        if self.mode == "log":
            return
        if self.mode == "setup":
            self.exit_setup(timeout)
        self.enter_log(timeout)

    # ---------------- SETUP command exchange ----------------

    def send_setup_cmd(self, cmd: str, timeout: float = DEFAULT_SETUP_CMD_TIMEOUT) -> list[str]:
        """Send one SETUP-mode command and return its response lines
        (everything printed between the command and the next bare prompt)."""
        if self.mode != "setup":
            raise RuntimeError(f"{self.name}: send_setup_cmd() requires setup mode, currently {self.mode}")
        t0 = self._write_line(cmd)
        prompt_ev = self.bus.wait_for(self.prompt_channel, lambda e: True, timeout=timeout, since=t0)
        if prompt_ev is None:
            buffered = [l.raw for l in self.bus.collect(self.channel, since=t0)]
            raise SerialCommandTimeout(self.name, cmd, buffered)
        return [l.raw for l in self.bus.collect(self.channel, since=t0, until=prompt_ev.ts)]

    # ---------------- raw KISS injection ----------------

    def send_kiss_frame(self, tnc2_line: str) -> None:
        """Write a raw KISS-framed AX.25 packet directly to this device's
        USB serial port while in KISS mode -- the firmware decodes it and
        transmits it over LoRa RF as if it originated the frame itself
        (serial_setup.cpp:1015-1027, the tracker/digi-side equivalent of
        the iGate's TCP KISS server in tcp_kiss_client.py). No response is
        expected (KISS mode gives no echo -- serial_setup.cpp:1029).

        Requires KISS mode. A caller coming from LOG mode must exit_log()
        first and, if it needs continued log observation afterward,
        enter_log() again once done -- this method doesn't manage that
        transition itself since callers differ on whether they want to
        stay in KISS after injecting."""
        if self.mode != "kiss":
            raise RuntimeError(f"{self.name}: send_kiss_frame() requires kiss mode, currently {self.mode}")
        assert self._ser is not None, f"{self.name}: serial port not open"
        self._ser.write(encode_kiss_frame(tnc2_line))
        self._ser.flush()
