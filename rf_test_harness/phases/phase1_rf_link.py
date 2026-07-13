"""
Phase 1: Tracker -> iGate RF link.

The firmware suppresses the tracker's own TX log for the entire SETUP-mode
lifetime (logger stays at ERROR until enterLog() -- see src/serial_setup.cpp
lines 343, 375), so a 'tx comment'-triggered beacon never produces the
tracker's own "Beacon: TX:" / "LoRa Tx:" log lines. This phase therefore
treats the SETUP command's synchronous ack as the trigger timestamp and the
iGate's own "LoRa Rx:" line as the first content-bearing checkpoint. See
README 'Known Limitations' for the --wait-for-natural-beacon alternative
that does capture the tracker's own echo.
"""

from __future__ import annotations

import time

from constants import DEFAULT_SETUP_CMD_TIMEOUT, TRACKER_TOCALL, TX_COMMENT_ACK, VALID_POSITION_DTIS
from device_session import SerialCommandTimeout
from log_parser import extract_rx_packet, is_lora_rx_from
from phases.context import TestContext, TestResult

PHASE_NAME = "phase1_rf_link"

NO_RX_NOTE = (
    "Tracker-side TX confirmation is limited to the SETUP command ack; the firmware "
    "suppresses its own TX log during a SETUP-triggered send (see README 'Known "
    "Limitations'). If an RF-path fault is suspected, re-run with "
    "--wait-for-natural-beacon for a full 4-checkpoint chain including the tracker's "
    "own log line."
)


def _fail(failure_mode: str, evidence: list[str], notes: str) -> TestResult:
    return TestResult(
        phase_name=PHASE_NAME,
        passed=False,
        failure_mode=failure_mode,
        evidence=evidence,
        latency_ms=None,
        notes=notes,
    )


def trigger_beacon(ctx: TestContext) -> tuple[float, list[str]]:
    """Send 'tx comment' and return (trigger_timestamp, response_lines).
    Raises SerialCommandTimeout if the device never responds."""
    tracker = ctx.tracker
    tracker.enter_setup()
    t_trigger = time.monotonic()
    resp = tracker.send_setup_cmd("tx comment", timeout=DEFAULT_SETUP_CMD_TIMEOUT)
    tracker.exit_setup()
    return t_trigger, resp


def wait_and_validate_rx(ctx: TestContext, t_trigger: float) -> TestResult:
    """Wait for the iGate's own "LoRa Rx:" line matching the tracker's
    callsign and validate its content. Shared by the normal 'tx comment'
    trigger path and --wait-for-natural-beacon (run_test.py), which supplies
    a different t_trigger (the tracker's own Beacon:TX timestamp) but
    otherwise wants identical RX validation."""
    cfg = ctx.config
    bus = ctx.bus

    rx_event = bus.wait_for(
        ctx.igate.channel,
        is_lora_rx_from(cfg.tracker_callsign),
        timeout=cfg.phase1_timeout,
        since=t_trigger,
    )
    if rx_event is None:
        return _fail("IGATE_NO_RX", [], NO_RX_NOTE)

    latency_ms = (rx_event.ts - t_trigger) * 1000
    pkt = extract_rx_packet(rx_event)

    if pkt is None:
        return TestResult(
            phase_name=PHASE_NAME,
            passed=False,
            failure_mode="IGATE_RX_CONTENT_MISMATCH",
            evidence=[rx_event.raw],
            latency_ms=latency_ms,
            notes="matched RX predicate but the packet failed to re-parse (unexpected)",
        )

    notes = ""
    passed = True
    if pkt.tocall != TRACKER_TOCALL:
        passed, notes = False, f"unexpected tocall '{pkt.tocall}', expected '{TRACKER_TOCALL}'"
    elif ctx.preflight.tracker_beacon_path and ctx.preflight.tracker_beacon_path not in pkt.path:
        passed, notes = (
            False,
            f"path '{pkt.path}' does not contain configured beaconPath "
            f"'{ctx.preflight.tracker_beacon_path}'",
        )
    elif pkt.payload[:1] not in VALID_POSITION_DTIS:
        passed, notes = (
            False,
            f"unexpected DTI '{pkt.payload[:1]!r}' in payload, expected one of {VALID_POSITION_DTIS}",
        )

    ctx.state["phase1_rx_ts"] = rx_event.ts

    return TestResult(
        phase_name=PHASE_NAME,
        passed=passed,
        failure_mode=None if passed else "IGATE_RX_CONTENT_MISMATCH",
        evidence=[rx_event.raw],
        latency_ms=latency_ms,
        notes=notes,
        details={"sender": pkt.sender, "tocall": pkt.tocall, "path": pkt.path, "payload": pkt.payload},
    )


def run(ctx: TestContext) -> TestResult:
    try:
        t_trigger, resp = trigger_beacon(ctx)
    except SerialCommandTimeout as exc:
        # Leave the tracker's SETUP session for the caller/teardown to sort
        # out; don't attempt exit_setup() again on top of an already-failed
        # exchange.
        return _fail("TRACKER_TX_COMMAND_REJECTED", exc.buffered_lines, str(exc))

    if not any(line.startswith(TX_COMMENT_ACK) for line in resp):
        return _fail(
            "TRACKER_TX_COMMAND_REJECTED",
            resp,
            "'tx comment' did not return the expected ack -- check tracker config/state",
        )

    ctx.state["t_trigger"] = t_trigger

    # Best-effort: also let the tracker stream LOG-mode output for the rest
    # of the run, so a later *natural* SmartBeacon send is visible too. Not
    # relied on for this phase's pass/fail.
    try:
        ctx.tracker.enter_log()
    except Exception:
        pass

    return wait_and_validate_rx(ctx, t_trigger)
