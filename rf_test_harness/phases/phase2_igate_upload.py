"""
Phase 2: iGate RX -> APRS-IS upload attempt.

Passively observes the iGate's LOG-mode channel (already at DEBUG level from
pre-flight) for the "APRS-IS: Uploaded:" line -- no new commands are sent.
"""

from __future__ import annotations

from log_parser import extract_uploaded_packet, is_aprsis_uploaded_from
from phases.context import TestContext, TestResult

PHASE_NAME = "phase2_igate_upload"

NO_UPLOAD_NOTE = (
    "iGate received the packet on RF but never logged an upload attempt. Likely causes: "
    "a dedup collision from a prior run within the 60s TTL (see README 'Repeatability' -- "
    "consecutive runs need --run-spacing or --vary-comment), a 'NOGATE' marker in the "
    "payload, or the iGate's APRS-IS connection dropped between pre-flight and now."
)


def run(ctx: TestContext) -> TestResult:
    cfg = ctx.config
    bus = ctx.bus
    since = ctx.state.get("phase1_rx_ts")

    ev = bus.wait_for(
        ctx.igate.channel,
        is_aprsis_uploaded_from(cfg.tracker_callsign),
        timeout=cfg.phase2_timeout,
        since=since,
    )
    if ev is None:
        return TestResult(
            phase_name=PHASE_NAME,
            passed=False,
            failure_mode="IGATE_RX_BUT_NO_UPLOAD",
            evidence=[],
            latency_ms=None,
            notes=NO_UPLOAD_NOTE,
        )

    latency_ms = (ev.ts - since) * 1000 if since is not None else None
    pkt = extract_uploaded_packet(ev)
    if pkt is None:
        return TestResult(
            phase_name=PHASE_NAME,
            passed=False,
            failure_mode="IGATE_RX_BUT_NO_UPLOAD",
            evidence=[ev.raw],
            latency_ms=latency_ms,
            notes="matched upload predicate but the line failed to re-parse (unexpected)",
        )

    qar_marker = f"qAR,{cfg.igate_callsign}"
    qao_marker = f"qAO,{cfg.igate_callsign}"
    if qar_marker in pkt.path:
        passcode_status = "VALID (qAR)"
    elif qao_marker in pkt.path:
        passcode_status = "UNVERIFIED (qAO)"
    else:
        passcode_status = "UNKNOWN"

    notes = ""
    passed = True
    if passcode_status == "UNKNOWN":
        passed, notes = (
            False,
            f"uploaded path '{pkt.path}' missing expected 'qAR,{cfg.igate_callsign}' or "
            f"'qAO,{cfg.igate_callsign}' marker",
        )

    ctx.state["phase2_upload_ts"] = ev.ts

    return TestResult(
        phase_name=PHASE_NAME,
        passed=passed,
        failure_mode=None if passed else "IGATE_RX_BUT_NO_UPLOAD",
        evidence=[ev.raw],
        latency_ms=latency_ms,
        notes=notes,
        details={
            "sender": pkt.sender,
            "tocall": pkt.tocall,
            "path": pkt.path,
            "payload": pkt.payload,
            "passcode_status": passcode_status,
        },
    )
