"""
Phase 11: NOGATE/echo-rejection from a fake sender (tracker transmits, iGate
receives).

Per the corrected design found while building Group B: injecting through
the iGate's own TCP KISS port makes the iGate TRANSMIT (a half-duplex radio
can't receive its own TX), so testing the iGate's RX-side echo-rejection
heuristic needs injection through the TRACKER's serial port instead --
same underlying KISS-decode mechanism as the iGate's TCP KISS server, just
over USB instead of WiFi (serial_setup.cpp:1015-1027). The tracker
transmits, the iGate receives and evaluates it through its genuine RX
pipeline (aprs_is_utils.cpp's processLoRaPacket()).

KNOWN LIMITATION (found this session, confirmed via 'git diff
src/aprs_is_utils.cpp'): the echo-rejection heuristic being tested here
exists only in an *uncommitted local diff* as of this harness session -- it
is not yet flashed to the iGate under test. This phase is written to
validate the intended/new behavior and will legitimately FAIL against
whatever firmware predates that diff; it should pass once it's flashed.

Uses paths with no WIDE1-1/WIDE2-n alias so a digi (if present) doesn't
relay these fake packets as a side effect (digi_utils.cpp:70-77 early-returns
when there's no alias to substitute), keeping this test isolated to the
iGate's RX logic only.
"""

from __future__ import annotations

import time

from log_parser import is_aprsis_uploaded_from, is_lora_rx_from, predicate_or
from phases.context import TestContext, TestResult
from serial_link import LogEvent

PHASE_NAME = "phase11_echo_rejection"

VARIANT_TIMEOUT = 10.0

VARIANTS = [
    ("known_igate_tocall", "ECHOF1>APLRG1:!3712.34N/12212.34W>echo-test-tocall"),
    ("tcpip_marker", "ECHOF2>APRS,TCPIP:!3712.34N/12212.34W>echo-test-tcpip"),
    ("third_party_wrap", "ECHOF3>APRS:}FAKESTA>APRS:!3712.34N/12212.34W>echo-test-3rdparty"),
]


def _is_echo_skip_for(sender: str):
    def pred(ev: LogEvent) -> bool:
        return "Skip IS->RF echo (rebroadcast):" in ev.raw and sender in ev.raw

    return pred


def run(ctx: TestContext) -> TestResult:
    cfg = ctx.config
    bus = ctx.bus
    tracker = ctx.tracker
    igate = ctx.igate

    if tracker.mode == "log":
        tracker.exit_log()
    elif tracker.mode == "setup":
        tracker.exit_setup()

    results: dict[str, str] = {}
    evidence: list[str] = []

    for variant_name, tnc2_line in VARIANTS:
        fake_sender = tnc2_line.split(">", 1)[0]
        t_trigger = time.monotonic()
        tracker.send_kiss_frame(tnc2_line)

        rx_ev = bus.wait_for(
            igate.channel, is_lora_rx_from(fake_sender), timeout=VARIANT_TIMEOUT, since=t_trigger
        )
        if rx_ev is None:
            results[variant_name] = "NOT_RECEIVED"
            continue
        evidence.append(rx_ev.raw)

        outcome_ev = bus.wait_for(
            igate.channel,
            predicate_or(_is_echo_skip_for(fake_sender), is_aprsis_uploaded_from(fake_sender)),
            timeout=VARIANT_TIMEOUT,
            since=rx_ev.ts,
        )
        if outcome_ev is None:
            results[variant_name] = "NEITHER_SKIP_NOR_UPLOAD"
            continue
        evidence.append(outcome_ev.raw)
        if "Skip IS->RF echo (rebroadcast):" in outcome_ev.raw:
            results[variant_name] = "CORRECTLY_REJECTED"
        else:
            results[variant_name] = "INCORRECTLY_UPLOADED"

    tracker.enter_log()  # leave the device in an observable state for any phase that follows

    failed = {k: v for k, v in results.items() if v != "CORRECTLY_REJECTED"}
    passed = not failed

    if not passed:
        notes = "; ".join(f"{k}: {v}" for k, v in failed.items())
    else:
        notes = ""

    return TestResult(
        phase_name=PHASE_NAME,
        passed=passed,
        failure_mode=None if passed else "IGATE_ECHO_NOT_REJECTED",
        evidence=evidence,
        latency_ms=None,
        notes=notes,
        details={"results": results},
    )
