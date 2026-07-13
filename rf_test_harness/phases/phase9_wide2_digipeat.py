"""
Phase 9: WIDE2-2 multi-hop digipeat (decrement, not full consumption).

Requires the digi in 'wide1+wide2' mode -- the WIDE2 branch in
buildPacket() is gated on mode == DIGI_WIDE1_WIDE2 specifically
(digi_utils.cpp:37), unlike WIDE1-1 fill-in which both wide1 and
wide1+wide2 handle identically. This phase upgrades the digi to
wide1+wide2 once (idempotent -- checks current mode first) and leaves it
there permanently: that mode is a strict superset for WIDE1-1 handling
(digi_utils.cpp:34, mode >= DIGI_WIDE1), so it doesn't break
phase4_digipeat_relay's existing WIDE1-1 coverage.

WIDE2-2 -> "<digicall>*,WIDE2-1" (decremented, one hop left) is the more
interesting case than WIDE2-1 -> "<digicall>*" (fully consumed) -- it's the
only one of the two that actually exercises the decrement logic
(digi_utils.cpp:41-42).
"""

from __future__ import annotations

import time

from device_session import SerialCommandTimeout
from log_parser import (
    extract_digi_relay_packet,
    extract_rx_packet,
    is_digi_repeating_from,
    is_lora_rx_from,
    parse_kv_block,
)
from phases.context import TestContext, TestResult

PHASE_NAME = "phase9_wide2_digipeat"

TEST_PATH = "WIDE2-2"


def _fail(failure_mode: str, evidence: list[str], notes: str, latency_ms: float | None = None) -> TestResult:
    return TestResult(
        phase_name=PHASE_NAME, passed=False, failure_mode=failure_mode, evidence=evidence,
        latency_ms=latency_ms, notes=notes,
    )


def _ensure_digi_wide2(digi) -> None:
    digi.ensure_setup_mode()
    try:
        other = parse_kv_block(digi.send_setup_cmd("show other"))
        if other.get("digiMode") != "wide1+wide2":
            digi.send_setup_cmd("digi wide1+wide2")
            digi.send_setup_cmd("save")
    finally:
        digi.exit_setup()
        # Preflight leaves digi in LOG mode (debug level) for the rest of the
        # run so its "LoRa Rx:"/"Digi: Repeating:" lines are observable --
        # the logger is suppressed at ERROR level for the entire KISS/SETUP
        # lifetime (serial_setup.cpp:343,375), so leaving digi in KISS mode
        # here (exit_setup()'s natural end state) makes it look like digi
        # never heard/relayed anything at all, even if it actually did.
        # Confirmed: this was a real bug (DIGI_NO_RX, reproducibly) before
        # this fix, not RF flakiness.
        digi.enter_log()


def run(ctx: TestContext) -> TestResult:
    cfg = ctx.config
    bus = ctx.bus
    tracker = ctx.tracker
    digi = ctx.extra_devices.get("digi")

    if digi is None:
        return _fail("DIGI_NOT_CONFIGURED", [], "phase9_wide2_digipeat requires --digi-port")

    _ensure_digi_wide2(digi)

    tracker.ensure_setup_mode()
    try:
        try:
            tracker.send_setup_cmd(f"beaconpath {TEST_PATH}")
            t_trigger = time.monotonic()
            resp = tracker.send_setup_cmd("tx comment")
        except SerialCommandTimeout as exc:
            return _fail("TRACKER_TX_COMMAND_REJECTED", exc.buffered_lines, str(exc))
        if "OK tx comment beacon sent" not in "\n".join(resp):
            return _fail("TRACKER_TX_COMMAND_REJECTED", resp, "'tx comment' did not return the expected ack")
    finally:
        tracker.send_setup_cmd(f"beaconpath {ctx.preflight.tracker_beacon_path}")
        tracker.send_setup_cmd("save")
        tracker.exit_setup()

    # 1. iGate hears the direct copy with the unconsumed WIDE2-2 path.
    igate_direct_rx = bus.wait_for(
        ctx.igate.channel,
        is_lora_rx_from(cfg.tracker_callsign, path_contains=TEST_PATH),
        timeout=cfg.phase1_timeout,
        since=t_trigger,
    )
    if igate_direct_rx is None:
        return _fail("IGATE_NO_RX", [], f"iGate never heard the direct beacon with path '{TEST_PATH}'")

    # 2. digi hears it directly too.
    digi_rx = bus.wait_for(
        digi.channel,
        is_lora_rx_from(cfg.tracker_callsign, path_contains=TEST_PATH),
        timeout=cfg.phase1_timeout,
        since=t_trigger,
    )
    if digi_rx is None:
        return _fail("DIGI_NO_RX", [igate_direct_rx.raw], "digi never heard the beacon directly")

    # 3. digi relays it, decrementing WIDE2-2 -> "<digicall>*,WIDE2-1".
    relay_ev = bus.wait_for(
        digi.channel, is_digi_repeating_from(cfg.tracker_callsign), timeout=cfg.phase1_timeout, since=digi_rx.ts
    )
    if relay_ev is None:
        return _fail(
            "DIGI_WIDE2_NO_DECREMENT",
            [igate_direct_rx.raw, digi_rx.raw],
            "digi never logged a 'Digi: Repeating:' line for the WIDE2-2 packet",
        )
    relay_pkt = extract_digi_relay_packet(relay_ev)
    expected_path = f"{cfg.digi_callsign}*,WIDE2-1"
    if relay_pkt is None or relay_pkt.path != expected_path:
        actual = relay_pkt.path if relay_pkt else "(unparseable)"
        return _fail(
            "DIGI_WIDE2_NO_DECREMENT",
            [igate_direct_rx.raw, digi_rx.raw, relay_ev.raw],
            f"expected decremented path '{expected_path}', got '{actual}'",
        )

    # 4. iGate hears the relayed copy.
    igate_relay_rx = bus.wait_for(
        ctx.igate.channel,
        is_lora_rx_from(cfg.tracker_callsign, path_contains=f"{cfg.digi_callsign}*"),
        timeout=cfg.phase1_timeout,
        since=relay_ev.ts,
    )
    evidence = [igate_direct_rx.raw, digi_rx.raw, relay_ev.raw]
    if igate_relay_rx is None:
        return _fail(
            "IGATE_NO_DIGI_RX", evidence, f"iGate never heard the relayed copy (path '{expected_path}')"
        )
    evidence.append(igate_relay_rx.raw)

    latency_ms = (igate_relay_rx.ts - t_trigger) * 1000
    igate_relay_pkt = extract_rx_packet(igate_relay_rx)

    return TestResult(
        phase_name=PHASE_NAME,
        passed=True,
        failure_mode=None,
        evidence=evidence,
        latency_ms=latency_ms,
        notes="",
        details={
            "relay_path": relay_pkt.path,
            "igate_relay_rx_path": igate_relay_pkt.path if igate_relay_pkt else None,
        },
    )
