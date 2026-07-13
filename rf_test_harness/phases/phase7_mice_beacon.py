"""
Phase 7: Mic-E beacon (tracker -> iGate).

Object-report (tactical callsign) mode takes priority over Mic-E, which
takes priority over Base91-compressed (station_utils.cpp:274-306), so
testing Mic-E requires clearing 'beacon tactical' first.

Structural validation only -- this does not reimplement Mic-E lat/lon
decoding in Python. It trusts the already-ported lib/APRSPacketLib C++
encoder and just confirms the firmware actually switched into Mic-E mode:
tocall differs from the normal APLRT1 (position is encoded there instead)
and the info field's first byte is the Mic-E data-type indicator (backtick
for a current GPS fix, apostrophe for an old one).

Known asymmetry, harmless: unlike 'beacon comment'/'status'/'tactical',
'beacon mice <0..7>' has no bare/empty form (serial_setup.cpp requires a
token) -- there is no CLI way to restore micE to a true empty string.
Teardown therefore only restores 'tactical' (captured original), which is
checked *before* micE in the firmware's priority chain, so a leftover
non-empty micE value has no effect on any later beacon once tactical is
back.
"""

from __future__ import annotations

import time

from constants import TRACKER_TOCALL
from device_session import SerialCommandTimeout
from log_parser import extract_rx_packet, is_lora_rx_from, parse_kv_block
from phases.context import TestContext, TestResult

PHASE_NAME = "phase7_mice_beacon"

MICE_DTIS = ("`", "'")
TEST_MICE_VALUE = "1"


def _fail(failure_mode: str, evidence: list[str], notes: str) -> TestResult:
    return TestResult(
        phase_name=PHASE_NAME, passed=False, failure_mode=failure_mode, evidence=evidence,
        latency_ms=None, notes=notes,
    )


def run(ctx: TestContext) -> TestResult:
    cfg = ctx.config
    tracker = ctx.tracker
    tracker.ensure_setup_mode()

    original_tactical = ""
    try:
        original = parse_kv_block(tracker.send_setup_cmd("show beacons"))
        original_tactical = original.get("tactical", "")

        try:
            tracker.send_setup_cmd("beacon tactical")  # clears it (position/Mic-E mode)
            tracker.send_setup_cmd(f"beacon mice {TEST_MICE_VALUE}")
            t_trigger = time.monotonic()
            resp = tracker.send_setup_cmd("tx comment")
        except SerialCommandTimeout as exc:
            return _fail("TRACKER_MICE_CONFIG_REJECTED", exc.buffered_lines, str(exc))
        if "OK tx comment beacon sent" not in "\n".join(resp):
            return _fail("TRACKER_MICE_CONFIG_REJECTED", resp, "'tx comment' did not return the expected ack")
    finally:
        tracker.send_setup_cmd(f"beacon tactical {original_tactical}".rstrip())
        tracker.send_setup_cmd("save")
        tracker.exit_setup()

    rx_event = ctx.bus.wait_for(
        ctx.igate.channel,
        is_lora_rx_from(cfg.tracker_callsign, direct_only=True),
        timeout=cfg.phase1_timeout,
        since=t_trigger,
    )
    if rx_event is None:
        return _fail("IGATE_NO_RX", [], "iGate never heard the Mic-E beacon")

    latency_ms = (rx_event.ts - t_trigger) * 1000
    pkt = extract_rx_packet(rx_event)
    if pkt is None:
        return TestResult(
            phase_name=PHASE_NAME,
            passed=False,
            failure_mode="IGATE_MICE_RX_STRUCTURAL_MISMATCH",
            evidence=[rx_event.raw],
            latency_ms=latency_ms,
            notes="matched RX predicate but the packet failed to re-parse (unexpected)",
        )

    passed, notes = True, ""
    if pkt.tocall == TRACKER_TOCALL:
        passed, notes = (
            False,
            f"tocall is still '{TRACKER_TOCALL}' -- Mic-E should encode position into a different "
            f"pseudo-callsign destination field",
        )
    elif pkt.payload[:1] not in MICE_DTIS:
        passed, notes = (
            False,
            f"unexpected first payload byte {pkt.payload[:1]!r}, expected one of {MICE_DTIS}",
        )

    return TestResult(
        phase_name=PHASE_NAME,
        passed=passed,
        failure_mode=None if passed else "IGATE_MICE_RX_STRUCTURAL_MISMATCH",
        evidence=[rx_event.raw],
        latency_ms=latency_ms,
        notes=notes,
        details={"tocall": pkt.tocall, "payload_prefix": pkt.payload[:8]},
    )
