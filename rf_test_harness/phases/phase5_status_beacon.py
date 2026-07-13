"""
Phase 5: Status beacon (tracker -> iGate).

Two sub-checks in one phase: a status beacon with real text (DTI '>'), and
the documented fallback -- an empty status silently sends a normal position
beacon instead (station_utils.cpp:374-378). Both share one SETUP session and
one teardown (restore the original status text).
"""

from __future__ import annotations

import time

from constants import STATUS_DTI, TRACKER_TOCALL, TX_STATUS_ACK, VALID_POSITION_DTIS
from device_session import SerialCommandTimeout
from log_parser import extract_rx_packet, is_lora_rx_from, parse_kv_block
from phases.context import TestContext, TestResult

PHASE_NAME = "phase5_status_beacon"

TEST_STATUS_TEXT = "RF-HARNESS-STATUS-TEST"


def _fail(failure_mode: str, evidence: list[str], notes: str, latency_ms: float | None = None) -> TestResult:
    return TestResult(
        phase_name=PHASE_NAME,
        passed=False,
        failure_mode=failure_mode,
        evidence=evidence,
        latency_ms=latency_ms,
        notes=notes,
    )


def _trigger_status(tracker, status_text: str) -> tuple[float, list[str]]:
    """Set beacon status text (empty string clears it) and force-send with
    'tx status'. Returns (trigger_ts, ack_response_lines)."""
    tracker.send_setup_cmd(f"beacon status {status_text}".rstrip())
    t_trigger = time.monotonic()
    resp = tracker.send_setup_cmd("tx status")
    return t_trigger, resp


def _wait_and_check(ctx: TestContext, t_trigger: float, expect_status: bool) -> TestResult:
    cfg = ctx.config
    rx_event = ctx.bus.wait_for(
        ctx.igate.channel,
        is_lora_rx_from(cfg.tracker_callsign, direct_only=True),
        timeout=cfg.phase1_timeout,
        since=t_trigger,
    )
    if rx_event is None:
        return _fail("IGATE_NO_RX", [], "iGate never heard the status-beacon trigger")

    latency_ms = (rx_event.ts - t_trigger) * 1000
    pkt = extract_rx_packet(rx_event)
    if pkt is None:
        return TestResult(
            phase_name=PHASE_NAME,
            passed=False,
            failure_mode="IGATE_STATUS_RX_CONTENT_MISMATCH",
            evidence=[rx_event.raw],
            latency_ms=latency_ms,
            notes="matched RX predicate but the packet failed to re-parse (unexpected)",
        )

    passed, notes = True, ""
    if pkt.tocall != TRACKER_TOCALL:
        passed, notes = False, f"unexpected tocall '{pkt.tocall}', expected '{TRACKER_TOCALL}'"
    elif expect_status:
        if not pkt.payload.startswith(STATUS_DTI):
            passed, notes = False, f"expected status DTI '{STATUS_DTI}', got payload {pkt.payload[:20]!r}"
        elif TEST_STATUS_TEXT not in pkt.payload:
            passed, notes = False, f"status text {TEST_STATUS_TEXT!r} not found in payload"
    else:
        if pkt.payload[:1] not in VALID_POSITION_DTIS:
            passed, notes = (
                False,
                f"empty status should fall back to a position beacon, got DTI {pkt.payload[:1]!r}",
            )

    return TestResult(
        phase_name=PHASE_NAME,
        passed=passed,
        failure_mode=None if passed else "IGATE_STATUS_RX_CONTENT_MISMATCH",
        evidence=[rx_event.raw],
        latency_ms=latency_ms,
        notes=notes,
        details={"tocall": pkt.tocall, "payload": pkt.payload, "sub_case": "with_text" if expect_status else "empty_fallback"},
    )


def run(ctx: TestContext) -> TestResult:
    tracker = ctx.tracker
    tracker.ensure_setup_mode()

    original_status = ""
    try:
        original_kv = parse_kv_block(tracker.send_setup_cmd("show beacons"))
        original_status = original_kv.get("status", "")

        try:
            t_trigger, resp = _trigger_status(tracker, TEST_STATUS_TEXT)
        except SerialCommandTimeout as exc:
            return _fail("TRACKER_STATUS_TX_REJECTED", exc.buffered_lines, str(exc))
        if not any(line.startswith(TX_STATUS_ACK) for line in resp):
            return _fail(
                "TRACKER_STATUS_TX_REJECTED", resp, "'tx status' did not return the expected ack"
            )
    finally:
        tracker.send_setup_cmd("save")
        tracker.exit_setup()

    result_with_text = _wait_and_check(ctx, t_trigger, expect_status=True)
    if not result_with_text.passed:
        return result_with_text

    tracker.ensure_setup_mode()
    try:
        try:
            t_trigger2, resp2 = _trigger_status(tracker, "")
        except SerialCommandTimeout as exc:
            return _fail("TRACKER_STATUS_TX_REJECTED", exc.buffered_lines, str(exc))
        if not any(line.startswith(TX_STATUS_ACK) for line in resp2):
            return _fail(
                "TRACKER_STATUS_TX_REJECTED", resp2, "'tx status' (empty) did not return the expected ack"
            )
    finally:
        # Restore the original status text now, regardless of what the
        # second sub-check finds below.
        tracker.send_setup_cmd(f"beacon status {original_status}".rstrip())
        tracker.send_setup_cmd("save")
        tracker.exit_setup()

    result_empty = _wait_and_check(ctx, t_trigger2, expect_status=False)
    result_empty.evidence = result_with_text.evidence + result_empty.evidence
    return result_empty
