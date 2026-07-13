"""
Phase 6: PHG beacon (tracker -> iGate).

'phg on' fires almost immediately the *first* time PHG is ever enabled
since the tracker last rebooted: the firmware's phgLastTx static starts at
0, so the very next handleRoleSpecificTasks() tick sends one unconditionally
(src/device_role.cpp:191-198). But phgLastTx is set to millis() on every
send and never resets except on reboot, and the interval is floored at 60s
regardless of 'phg rate 1' (device_role.cpp:193) -- so a *repeat* run of
this phase within the same boot session can take up to a minute before the
next beacon is due.

Critical ordering consequence, found the hard way: restoring 'phg off'
right after triggering (before waiting for the RX event) makes the delayed
case structurally impossible to pass -- Config.phg.enabled goes back to
false before the firmware's own timer ever permits a send, so the beacon
never fires at all. PHG must stay enabled for the *entire* wait; teardown
only happens after that wait completes (pass, fail, or content-mismatch).
"""

from __future__ import annotations

import time

from constants import PHG_EXTENSION_RE, TRACKER_TOCALL, VALID_POSITION_DTIS
from device_session import SerialCommandTimeout
from log_parser import extract_rx_packet, is_lora_rx_from, parse_kv_block
from phases.context import TestContext, TestResult

PHASE_NAME = "phase6_phg_beacon"

# Firmware floors the PHG interval at 60s (device_role.cpp:193) -- this only
# matters on a repeat run within the same boot session (phgLastTx nonzero
# from a prior send); a fresh-boot first run fires almost immediately and
# returns well before this timeout is used.
PHG_WAIT_TIMEOUT = 65.0


def _fail(failure_mode: str, evidence: list[str], notes: str) -> TestResult:
    return TestResult(
        phase_name=PHASE_NAME, passed=False, failure_mode=failure_mode, evidence=evidence,
        latency_ms=None, notes=notes,
    )


def _restore(tracker, original_enabled: str, original_rate: str) -> None:
    tracker.ensure_setup_mode()
    try:
        tracker.send_setup_cmd("phg rate " + str(original_rate))
        if original_enabled == "true":
            tracker.send_setup_cmd("phg on")
        else:
            tracker.send_setup_cmd("phg off")
        tracker.send_setup_cmd("save")
    finally:
        tracker.exit_setup()


def run(ctx: TestContext) -> TestResult:
    cfg = ctx.config
    tracker = ctx.tracker
    tracker.ensure_setup_mode()

    original_enabled = "false"
    original_rate = "10"  # firmware default (data/tracker_conf.json), used only if 'phg show' itself fails
    try:
        original = parse_kv_block(tracker.send_setup_cmd("phg show"))
        original_enabled = original.get("phg.enabled", "false")
        rate_raw = original.get("rate", "10min")
        original_rate = rate_raw.removesuffix("min") if rate_raw.endswith("min") else rate_raw

        t_trigger = time.monotonic()
        tracker.send_setup_cmd("phg on")
        tracker.send_setup_cmd("phg rate 1")
    except SerialCommandTimeout as exc:
        _restore(tracker, original_enabled, original_rate)
        return _fail("TRACKER_PHG_ENABLE_REJECTED", exc.buffered_lines, str(exc))

    # Deliberately still in SETUP mode here (not exited) -- the main loop
    # (and PHG's own timer check) runs every iteration regardless of serial
    # mode, and exiting now would hit the same "unsaved changes" refusal
    # fixed elsewhere (config is dirty from 'phg on'/'phg rate'); simplest
    # is to just stay put and let _restore() below handle mode transitions
    # (its ensure_setup_mode() is a no-op if we're already here). PHG must
    # stay enabled on the device for the entire wait below -- see module
    # docstring. Teardown happens only after this, regardless of outcome.
    rx_event = ctx.bus.wait_for(
        ctx.igate.channel,
        is_lora_rx_from(cfg.tracker_callsign, direct_only=True),
        timeout=PHG_WAIT_TIMEOUT,
        since=t_trigger,
    )

    _restore(tracker, original_enabled, original_rate)

    if rx_event is None:
        return _fail(
            "IGATE_NO_RX",
            [],
            f"iGate never heard the PHG beacon within {PHG_WAIT_TIMEOUT}s "
            f"(the firmware's own 60s rate floor applies on any run after the first since boot)",
        )

    latency_ms = (rx_event.ts - t_trigger) * 1000
    pkt = extract_rx_packet(rx_event)
    if pkt is None:
        return TestResult(
            phase_name=PHASE_NAME,
            passed=False,
            failure_mode="IGATE_PHG_RX_CONTENT_MISMATCH",
            evidence=[rx_event.raw],
            latency_ms=latency_ms,
            notes="matched RX predicate but the packet failed to re-parse (unexpected)",
        )

    passed, notes = True, ""
    if pkt.tocall != TRACKER_TOCALL:
        passed, notes = False, f"unexpected tocall '{pkt.tocall}', expected '{TRACKER_TOCALL}'"
    elif pkt.payload[:1] not in VALID_POSITION_DTIS:
        passed, notes = False, f"unexpected DTI {pkt.payload[:1]!r}, expected a position packet"
    elif not PHG_EXTENSION_RE.search(pkt.payload):
        passed, notes = False, f"no 'PHGxxxx' extension found in payload {pkt.payload!r}"

    return TestResult(
        phase_name=PHASE_NAME,
        passed=passed,
        failure_mode=None if passed else "IGATE_PHG_RX_CONTENT_MISMATCH",
        evidence=[rx_event.raw],
        latency_ms=latency_ms,
        notes=notes,
        details={"tocall": pkt.tocall, "payload": pkt.payload},
    )
