"""
Phase 10: station query/ping response (iGate transmits, tracker receives).

Injects a fake '?PING?' query addressed to the tracker through the iGate's
TCP KISS port (tcp_kiss_client.py) -- the iGate decodes and transmits it
over real LoRa RF exactly as if it originated the frame
(tcp_kiss_utils.cpp:50). The tracker receives it and query_utils.cpp fires:
first an ack (the query carries a msgno), then the actual reply
(query_utils.cpp:181-184, 240-243), each a separate 'LoRa Tx:' line ~200ms
apart (station_utils.cpp:92-106).

Unlike phase1's 'tx comment' trigger, this one is RF-external -- the
tracker is sitting in LOG mode the whole time, so its own TX log (normally
suppressed during a SETUP-triggered send) is fully observable here. That's
the whole point of routing the trigger through RF instead of the serial CLI.

There is no CLI command to originate an arbitrary query/message
(SERIAL_SETUP.md's only "transmit now" commands are 'tx comment'/'tx
status') -- RF injection via KISS is the only way to exercise
query_utils.cpp, which otherwise has zero test coverage.
"""

from __future__ import annotations

import time

from constants import TRACKER_TOCALL
from log_parser import extract_tx_packet, is_aprsis_uploaded_from, is_lora_tx_from
from phases.context import TestContext, TestResult
from tcp_kiss_client import TCPKissClient

PHASE_NAME = "phase10_query_response"

# <=6 chars: AX.25 addressing truncates anything longer (confirmed against
# real hardware -- "TESTSTA" arrives as "TESTST"), so using a fake sender
# that's already <=6 chars avoids any truncation-vs-expected-value mismatch.
INJECTED_SENDER = "TSTQRY"
PING_MSGNO = "01"
INJECT_WAIT_TIMEOUT = 10.0


def _fail(failure_mode: str, evidence: list[str], notes: str) -> TestResult:
    return TestResult(
        phase_name=PHASE_NAME, passed=False, failure_mode=failure_mode, evidence=evidence,
        latency_ms=None, notes=notes,
    )


def run(ctx: TestContext) -> TestResult:
    cfg = ctx.config
    bus = ctx.bus
    tracker = ctx.tracker
    igate = ctx.igate

    tracker.ensure_log_mode()

    addressee = cfg.tracker_callsign.ljust(9)[:9]
    tnc2_line = f"{INJECTED_SENDER}>APRS,WIDE1-1::{addressee}:?PING?{{{PING_MSGNO}"

    t_trigger = time.monotonic()
    try:
        with TCPKissClient(cfg.igate_lan_ip, cfg.igate_tcp_kiss_port) as client:
            client.send_tnc2(tnc2_line)
    except OSError as exc:
        return _fail(
            "IGATE_KISS_INJECT_NOT_TXD",
            [],
            f"could not connect to iGate TCP KISS at {cfg.igate_lan_ip}:{cfg.igate_tcp_kiss_port}: {exc}",
        )

    # Checkpoint 1: iGate actually decoded and transmitted the injected frame.
    igate_tx = bus.wait_for(
        igate.channel, is_lora_tx_from(INJECTED_SENDER), timeout=INJECT_WAIT_TIMEOUT, since=t_trigger
    )
    if igate_tx is None:
        return _fail(
            "IGATE_KISS_INJECT_NOT_TXD",
            [],
            "iGate never logged a LoRa Tx: for the injected frame -- check the encoder/connection, "
            "not the RF path",
        )

    # Checkpoint 2: tracker replies (skip over the ack, which carries
    # ':ackNN' rather than 'PING' in its payload).
    def _is_ping_reply(ev):
        pkt = extract_tx_packet(ev)
        return pkt is not None and pkt.sender == cfg.tracker_callsign and "PING" in pkt.payload

    reply_ev = bus.wait_for(tracker.channel, _is_ping_reply, timeout=INJECT_WAIT_TIMEOUT, since=igate_tx.ts)
    if reply_ev is None:
        return _fail(
            "TRACKER_NO_QUERY_REPLY",
            [igate_tx.raw],
            "tracker never logged a PING reply -- either it never received the injected query, "
            "or query_utils.cpp didn't fire",
        )

    latency_ms = (reply_ev.ts - t_trigger) * 1000
    reply_pkt = extract_tx_packet(reply_ev)
    if reply_pkt is None:
        return TestResult(
            phase_name=PHASE_NAME,
            passed=False,
            failure_mode="TRACKER_QUERY_REPLY_CONTENT_MISMATCH",
            evidence=[igate_tx.raw, reply_ev.raw],
            latency_ms=latency_ms,
            notes="matched TX predicate but the packet failed to re-parse (unexpected)",
        )

    passed, notes = True, ""
    if reply_pkt.tocall != TRACKER_TOCALL:
        passed, notes = False, f"unexpected tocall '{reply_pkt.tocall}', expected '{TRACKER_TOCALL}'"
    elif cfg.tracker_callsign not in reply_pkt.payload:
        passed, notes = False, f"reply payload does not contain own callsign: {reply_pkt.payload!r}"

    evidence = [igate_tx.raw, reply_ev.raw]

    # Optional checkpoint 3: message-type packets aren't discriminated by
    # the RF->IS upload gate, so the reply should also reach APRS-IS.
    # Informational only -- doesn't affect pass/fail.
    upload_ev = bus.wait_for(
        igate.channel, is_aprsis_uploaded_from(cfg.tracker_callsign), timeout=cfg.phase2_timeout, since=reply_ev.ts
    )
    if upload_ev is not None:
        evidence.append(upload_ev.raw)

    return TestResult(
        phase_name=PHASE_NAME,
        passed=passed,
        failure_mode=None if passed else "TRACKER_QUERY_REPLY_CONTENT_MISMATCH",
        evidence=evidence,
        latency_ms=latency_ms,
        notes=notes,
        details={
            "reply_payload": reply_pkt.payload,
            "also_uploaded_to_is": upload_ev is not None,
        },
    )
