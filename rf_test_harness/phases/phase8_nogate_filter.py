"""
Phase 8: NOGATE filtering (tracker's own packet).

Both DIGI_Utils::processLoRaPacket() (digi_utils.cpp:81) and
APRS_IS_Utils::processLoRaPacket() (aprs_is_utils.cpp:183) do a literal
substring search for "NOGATE" anywhere in the raw packet and skip
relay/upload -- but neither filters RX itself, so both devices should still
log a normal LoRa Rx: line for it. This phase asserts two negatives (no
relay, no upload), which means each assertion must wait out its full
timeout to be confident nothing happened -- same shape as phase4's
dedup-suppression check.

Runs the digi-side half only if --digi-port is configured; otherwise
degrades gracefully to the iGate-side half alone (same convention as
phase4_digipeat_relay's DIGI_NOT_CONFIGURED handling).
"""

from __future__ import annotations

import time

from device_session import SerialCommandTimeout
from log_parser import is_aprsis_uploaded_from, is_digi_repeating_from, is_lora_rx_from, parse_kv_block
from phases.context import TestContext, TestResult

PHASE_NAME = "phase8_nogate_filter"


def _fail(failure_mode: str, evidence: list[str], notes: str) -> TestResult:
    return TestResult(
        phase_name=PHASE_NAME, passed=False, failure_mode=failure_mode, evidence=evidence,
        latency_ms=None, notes=notes,
    )


def run(ctx: TestContext) -> TestResult:
    cfg = ctx.config
    bus = ctx.bus
    tracker = ctx.tracker
    digi = ctx.extra_devices.get("digi")

    tracker.ensure_setup_mode()
    original_comment = ""
    try:
        original = parse_kv_block(tracker.send_setup_cmd("show beacons"))
        original_comment = original.get("comment", "")

        try:
            tracker.send_setup_cmd(f"beacon comment NOGATE-test-{int(time.time())}")
            t_trigger = time.monotonic()
            resp = tracker.send_setup_cmd("tx comment")
        except SerialCommandTimeout as exc:
            return _fail("TRACKER_TX_COMMAND_REJECTED", exc.buffered_lines, str(exc))
        if "OK tx comment beacon sent" not in "\n".join(resp):
            return _fail("TRACKER_TX_COMMAND_REJECTED", resp, "'tx comment' did not return the expected ack")
    finally:
        tracker.send_setup_cmd(f"beacon comment {original_comment}".rstrip())
        tracker.send_setup_cmd("save")
        tracker.exit_setup()

    evidence: list[str] = []

    # iGate side: RX must still happen, upload must not.
    igate_rx = bus.wait_for(
        ctx.igate.channel,
        is_lora_rx_from(cfg.tracker_callsign, direct_only=True),
        timeout=cfg.phase1_timeout,
        since=t_trigger,
    )
    if igate_rx is None:
        return _fail("IGATE_NO_RX", [], "iGate never heard the NOGATE-marked packet at all")
    evidence.append(igate_rx.raw)

    igate_upload = bus.wait_for(
        ctx.igate.channel,
        is_aprsis_uploaded_from(cfg.tracker_callsign),
        timeout=cfg.phase2_timeout,
        since=igate_rx.ts,
    )
    if igate_upload is not None:
        evidence.append(igate_upload.raw)
        return TestResult(
            phase_name=PHASE_NAME,
            passed=False,
            failure_mode="IGATE_NOGATE_LEAK",
            evidence=evidence,
            latency_ms=None,
            notes="iGate uploaded a packet containing 'NOGATE' -- should have been skipped",
        )

    if digi is None:
        return TestResult(
            phase_name=PHASE_NAME,
            passed=True,
            failure_mode=None,
            evidence=evidence,
            latency_ms=None,
            notes="digi not configured (--digi-port) -- only the iGate-side NOGATE check ran",
        )

    # Digi side: RX must still happen, relay must not.
    digi_rx = bus.wait_for(
        digi.channel, is_lora_rx_from(cfg.tracker_callsign), timeout=cfg.phase1_timeout, since=t_trigger
    )
    if digi_rx is None:
        return _fail("DIGI_NO_RX", evidence, "digi never heard the NOGATE-marked packet at all")
    evidence.append(digi_rx.raw)

    digi_relay = bus.wait_for(
        digi.channel,
        is_digi_repeating_from(cfg.tracker_callsign),
        timeout=cfg.phase1_timeout,
        since=digi_rx.ts,
    )
    if digi_relay is not None:
        evidence.append(digi_relay.raw)
        return TestResult(
            phase_name=PHASE_NAME,
            passed=False,
            failure_mode="DIGI_NOGATE_LEAK",
            evidence=evidence,
            latency_ms=None,
            notes="digi relayed a packet containing 'NOGATE' -- should have been skipped",
        )

    return TestResult(
        phase_name=PHASE_NAME, passed=True, failure_mode=None, evidence=evidence, latency_ms=None, notes=""
    )
