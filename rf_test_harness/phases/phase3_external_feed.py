"""
Phase 3: independent, read-only APRS-IS feed confirmation.

The tap connects to a *different* server pool than the iGate's own
configured APRS-IS server, so a match here confirms the packet actually
reached the public network -- decoupled from what the iGate's own log
claims. Only the "SENDER>" prefix is asserted (not full-line equality): the
harness's tap and the iGate's own server may be in different pools, so the
path can pick up additional backbone hops between them that aren't the
firmware's doing.
"""

from __future__ import annotations

from phases.context import TestContext, TestResult

PHASE_NAME = "phase3_external_feed"

NOT_ON_FEED_NOTE = (
    "The iGate logged a successful upload, but the packet never appeared on the "
    "independent public APRS-IS feed within the timeout. This points at APRS-IS "
    "network/server behavior, not the firmware -- the iGate's own configured server "
    "may be in a different pool than this harness's tap, or there may be upstream "
    "propagation delay/loss."
)


def run(ctx: TestContext) -> TestResult:
    cfg = ctx.config
    bus = ctx.bus
    since = ctx.state.get("t_trigger")
    prefix = f"{cfg.tracker_callsign}>"

    ev = bus.wait_for(
        ctx.tap.channel,
        lambda e: e.raw.startswith(prefix),
        timeout=cfg.phase3_timeout,
        since=since,
    )
    if ev is None:
        return TestResult(
            phase_name=PHASE_NAME,
            passed=False,
            failure_mode="UPLOADED_BUT_NOT_ON_FEED",
            evidence=[],
            latency_ms=None,
            notes=NOT_ON_FEED_NOTE,
        )

    latency_ms = (ev.ts - since) * 1000 if since is not None else None
    return TestResult(
        phase_name=PHASE_NAME,
        passed=True,
        failure_mode=None,
        evidence=[ev.raw],
        latency_ms=latency_ms,
        notes="",
        details={"raw_line": ev.raw},
    )
