"""
Phase 14: digipeat dedup (digi's own instance, not the iGate's upload dedup).

digi_utils.cpp:101-105: DIGI_Utils::processLoRaPacket() looks up
STATION_Utils::isInHashBuffer(sender, payload) -- a 50-slot/60s-TTL djb2
hash of sender+payload-after-first-colon (include/dedup_utils.h) -- and
silently `return`s (no log line) if the same sender+payload combination was
already seen within the TTL, before ever reaching generateDigipeatedPacket().
This is a *separate* PacketDedup instance from the iGate's own upload dedup
(aprs_is_utils.cpp) -- by design they don't suppress each other
(dedup_utils.h:6-8) -- so phase4/phase9's passing digipeat-relay checks
don't already cover this; this phase targets the digi's dedup specifically.

Uses 'tx status' (not 'tx comment') deliberately: a position/object beacon
embeds a per-minute timestamp in its payload (DDHHMMz), so two triggers
straddling a minute boundary would produce genuinely different payloads and
never dedup -- a real, if low-probability, source of flakiness. A status
beacon's payload is just the status text with no time-dependence, so two
back-to-back triggers are guaranteed byte-identical regardless of timing.

No log line is emitted on a dedup skip (confirmed against source), so the
second trigger's "not relayed" check is a negative/timeout-based assertion,
same shape as phase8_nogate_filter's.
"""

from __future__ import annotations

import time

from constants import TX_STATUS_ACK
from device_session import SerialCommandTimeout
from log_parser import is_digi_repeating_from, is_lora_rx_from, parse_kv_block
from phases.context import TestContext, TestResult

PHASE_NAME = "phase14_digi_dedup"

TEST_STATUS_TEXT = "DEDUP-TEST"
RELAY_WAIT_TIMEOUT = 10.0
DEDUP_SUPPRESSION_TIMEOUT = 10.0


def _fail(failure_mode: str, evidence: list[str], notes: str) -> TestResult:
    return TestResult(
        phase_name=PHASE_NAME, passed=False, failure_mode=failure_mode, evidence=evidence,
        latency_ms=None, notes=notes,
    )


def _trigger_status(tracker) -> tuple[float, list[str]]:
    t_trigger = time.monotonic()
    resp = tracker.send_setup_cmd("tx status")
    return t_trigger, resp


def run(ctx: TestContext) -> TestResult:
    cfg = ctx.config
    bus = ctx.bus
    tracker = ctx.tracker
    digi = ctx.extra_devices.get("digi")

    if digi is None:
        return _fail("DIGI_NOT_CONFIGURED", [], "phase14_digi_dedup requires --digi-port")

    tracker.ensure_setup_mode()
    original = parse_kv_block(tracker.send_setup_cmd("show beacons"))
    original_status = original.get("status", "")
    tracker.exit_setup()

    try:
        return _run_dedup_test(ctx)
    finally:
        # Guaranteed restore regardless of which path above returned --
        # the first trigger below persists TEST_STATUS_TEXT to flash, so
        # an early return on a failed assertion must not skip this.
        tracker.ensure_setup_mode()
        tracker.send_setup_cmd(f"beacon status {original_status}".rstrip())
        tracker.send_setup_cmd("save")
        tracker.exit_setup()


def _run_dedup_test(ctx: TestContext) -> TestResult:
    cfg = ctx.config
    bus = ctx.bus
    tracker = ctx.tracker
    digi = ctx.extra_devices["digi"]

    tracker.ensure_setup_mode()
    try:
        tracker.send_setup_cmd(f"beacon status {TEST_STATUS_TEXT}")
        t1, resp1 = _trigger_status(tracker)
    except SerialCommandTimeout as exc:
        tracker.exit_setup()
        return _fail("TRACKER_STATUS_TX_REJECTED", exc.buffered_lines, str(exc))
    tracker.send_setup_cmd("save")
    tracker.exit_setup()
    if not any(line.startswith(TX_STATUS_ACK) for line in resp1):
        return _fail("TRACKER_STATUS_TX_REJECTED", resp1, "first 'tx status' did not return the expected ack")

    # First trigger: digi should hear it AND relay it (baseline -- dedup
    # buffer was empty for this payload).
    digi_rx1 = bus.wait_for(
        digi.channel, is_lora_rx_from(cfg.tracker_callsign), timeout=RELAY_WAIT_TIMEOUT, since=t1
    )
    if digi_rx1 is None:
        return _fail("DIGI_NO_RX", [], "digi never heard the first status beacon")

    relay1 = bus.wait_for(
        digi.channel, is_digi_repeating_from(cfg.tracker_callsign), timeout=RELAY_WAIT_TIMEOUT, since=digi_rx1.ts
    )
    if relay1 is None:
        return _fail(
            "DIGI_NO_RELAY", [digi_rx1.raw], "digi never relayed the first (non-duplicate) status beacon"
        )
    evidence = [digi_rx1.raw, relay1.raw]

    # Second trigger: same status text, unchanged -> byte-identical payload,
    # well within the 60s TTL. digi should hear it (RX isn't deduped) but
    # NOT relay it (dedup should suppress it before generateDigipeatedPacket()).
    tracker.ensure_setup_mode()
    try:
        t2, resp2 = _trigger_status(tracker)
    except SerialCommandTimeout as exc:
        tracker.exit_setup()
        return _fail("TRACKER_STATUS_TX_REJECTED", exc.buffered_lines, str(exc))
    tracker.exit_setup()
    if not any(line.startswith(TX_STATUS_ACK) for line in resp2):
        return _fail("TRACKER_STATUS_TX_REJECTED", resp2, "second 'tx status' did not return the expected ack")

    digi_rx2 = bus.wait_for(
        digi.channel, is_lora_rx_from(cfg.tracker_callsign), timeout=RELAY_WAIT_TIMEOUT, since=t2
    )
    if digi_rx2 is None:
        return _fail("DIGI_NO_RX", evidence, "digi never heard the second (duplicate) status beacon at all")
    evidence.append(digi_rx2.raw)

    relay2 = bus.wait_for(
        digi.channel,
        is_digi_repeating_from(cfg.tracker_callsign),
        timeout=DEDUP_SUPPRESSION_TIMEOUT,
        since=digi_rx2.ts,
    )
    if relay2 is not None:
        evidence.append(relay2.raw)
        return TestResult(
            phase_name=PHASE_NAME,
            passed=False,
            failure_mode="DIGI_DEDUP_LEAK",
            evidence=evidence,
            latency_ms=None,
            notes="digi relayed a duplicate (identical sender+payload) packet within the 60s TTL -- "
            "dedup should have suppressed it",
        )

    return TestResult(
        phase_name=PHASE_NAME, passed=True, failure_mode=None, evidence=evidence, latency_ms=None, notes=""
    )
