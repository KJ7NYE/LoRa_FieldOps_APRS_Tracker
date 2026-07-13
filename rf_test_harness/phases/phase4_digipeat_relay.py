"""
Phase 4: WIDE1-1 fill-in digipeat relay (tracker -> digipeater -> iGate).

Reuses phase1_rf_link's trigger (ctx.state["t_trigger"]) rather than sending
a second 'tx comment' -- this phase only makes sense as an extension of the
same beacon phase1 already validated reached the iGate directly. It adds
three checks phase1/phase2 don't cover:

  1. the digipeater itself heard the original packet directly,
  2. it retransmitted with WIDE1-1 substituted for its own callsign + '*'
     (digi_utils.cpp's generateDigipeatedPacket()),
  3. the iGate hears BOTH the direct copy (already confirmed by phase1) and
     this digipeated copy, but the upload-dedup logic (djb2 hash of
     sender+payload, path-independent -- include/dedup_utils.h) still
     uploads only once. A second upload here means dedup failed to catch a
     path-only difference, which is exactly the case it exists to handle.
"""

from __future__ import annotations

from log_parser import (
    extract_digi_relay_packet,
    extract_rx_packet,
    is_aprsis_uploaded_from,
    is_digi_repeating_from,
    is_lora_rx_from,
)
from phases.context import TestContext, TestResult

PHASE_NAME = "phase4_digipeat_relay"


def _fail(failure_mode: str, evidence: list[str], notes: str, latency_ms: float | None = None) -> TestResult:
    return TestResult(
        phase_name=PHASE_NAME,
        passed=False,
        failure_mode=failure_mode,
        evidence=evidence,
        latency_ms=latency_ms,
        notes=notes,
    )


def run(ctx: TestContext) -> TestResult:
    cfg = ctx.config
    bus = ctx.bus

    t_trigger = ctx.state.get("t_trigger")
    if t_trigger is None:
        return _fail(
            "DIGI_PHASE_PRECONDITION_MISSING",
            [],
            "phase4_digipeat_relay requires phase1_rf_link to have run first in this attempt "
            "(needs ctx.state['t_trigger'])",
        )

    digi = ctx.extra_devices.get("digi")
    if digi is None:
        return _fail(
            "DIGI_NOT_CONFIGURED", [], "no 'digi' DeviceSession in TestContext.extra_devices"
        )

    # 1. digi heard the original packet directly.
    digi_rx = bus.wait_for(
        digi.channel, is_lora_rx_from(cfg.tracker_callsign), timeout=cfg.phase1_timeout, since=t_trigger
    )
    if digi_rx is None:
        return _fail(
            "DIGI_NO_RX", [], "digipeater never heard the tracker's beacon directly over RF"
        )

    # 2. digi retransmitted it (sender unchanged, path rewritten).
    relay_ev = bus.wait_for(
        digi.channel,
        is_digi_repeating_from(cfg.tracker_callsign),
        timeout=cfg.phase1_timeout,
        since=digi_rx.ts,
    )
    if relay_ev is None:
        return _fail(
            "DIGI_NO_RELAY",
            [digi_rx.raw],
            "digipeater heard the packet but never logged a 'Digi: Repeating:' retransmission "
            "-- check its 'digi wide1'/'digi wide1+wide2' mode and that WIDE1-1 wasn't already "
            "consumed (WIDE1-1* in the path)",
        )
    relay_pkt = extract_digi_relay_packet(relay_ev)

    # 3. iGate hears the digipeated copy specifically (path contains the
    #    digi's own callsign + '*', distinguishing it from phase1's direct copy).
    marker = f"{cfg.digi_callsign}*"
    igate_relay_rx = bus.wait_for(
        ctx.igate.channel,
        is_lora_rx_from(cfg.tracker_callsign, path_contains=marker),
        timeout=cfg.phase1_timeout,
        since=relay_ev.ts,
    )
    if igate_relay_rx is None:
        return _fail(
            "IGATE_NO_DIGI_RX",
            [relay_ev.raw],
            f"iGate never logged a LoRa Rx event with path containing '{marker}' -- the digipeater "
            f"transmitted, but the iGate didn't receive that copy (range/antenna?)",
        )

    latency_ms = (igate_relay_rx.ts - digi_rx.ts) * 1000

    # 4. Confirm the upload-dedup layer suppresses a second upload for this
    #    now-twice-received packet. Proving a negative requires waiting out
    #    a real window -- use phase2's own timeout budget for that wait.
    dup_upload = bus.wait_for(
        ctx.igate.channel,
        is_aprsis_uploaded_from(cfg.tracker_callsign),
        timeout=cfg.phase2_timeout,
        since=igate_relay_rx.ts,
    )
    if dup_upload is not None:
        return TestResult(
            phase_name=PHASE_NAME,
            passed=False,
            failure_mode="IGATE_DOUBLE_UPLOAD",
            evidence=[relay_ev.raw, igate_relay_rx.raw, dup_upload.raw],
            latency_ms=latency_ms,
            notes=(
                "iGate uploaded the digipeated copy a second time -- upload dedup "
                "(sender+payload hash, path-independent) should have suppressed this"
            ),
        )

    igate_relay_pkt = extract_rx_packet(igate_relay_rx)

    return TestResult(
        phase_name=PHASE_NAME,
        passed=True,
        failure_mode=None,
        evidence=[digi_rx.raw, relay_ev.raw, igate_relay_rx.raw],
        latency_ms=latency_ms,
        notes="",
        details={
            "digi_relay_path": relay_pkt.path if relay_pkt else None,
            "igate_relay_rx_path": igate_relay_pkt.path if igate_relay_pkt else None,
        },
    )
