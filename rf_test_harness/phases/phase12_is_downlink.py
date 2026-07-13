"""
Phase 12: iGate IS->RF downlink (mock APRS-IS server, no real network
involved). Opt-in only (--is-downlink / --phases phase12_is_downlink) --
never in the default phase list.

The most invasive test in the suite: two full iGate reboots. Confirmed
necessary, not just simplest -- cmdAprsIS's 'aprsiss server'/'port' setters
only write Config.aprsIS.server/.port in memory (serial_setup.cpp:833-834)
and checkConnection() only reconnects once the existing socket has already
dropped (aprs_is_utils.cpp:142-147), so a live connection to the real
server just keeps running otherwise -- there's no live-reconnect path.

Procedure:
  1. Capture the iGate's current aprsIS.server/.port/.downlinkEnabled via
     'export' (dumps the full persisted tracker_conf.json -- no 'show'
     command exposes these fields individually).
  2. Start the mock server locally, repoint the iGate at it
     (aprsiss server/port, aprsiss downlink on if not already), save,
     reboot_and_reconnect().
  3. Wait for the iGate to actually dial into the mock server.
  4. Trigger a tracker beacon so the iGate has heard it "direct" within the
     last 30 min -- a hard gate in listenAPRSIS() (both the message's
     addressee AND its claimed sender have heard-direct requirements,
     aprs_is_utils.cpp:293-306).
  5. Push a crafted '::ADDRESSEE:' line from the mock server.
  6. Watch for the iGate's wrapped third-party downlink TX.
  7. Teardown (always, via try/finally): restore original server/port/
     downlinkEnabled, save, reboot_and_reconnect() again.

Teardown risk, documented in README: if the harness is killed between
step 2 and the finally block, the iGate is left pointed at a now-dead
local mock server and needs a manual 'setup' -> 'aprsiss server <real>' ->
'aprsiss port <real>' -> 'save' -> 'reboot' to recover.
"""

from __future__ import annotations

import json
import time

from device_session import SerialCommandTimeout
from log_parser import is_lora_rx_from, is_lora_tx_from
from mock_aprs_is_server import MockAPRSISServer
from phases.context import TestContext, TestResult
from serial_link import LogEvent

PHASE_NAME = "phase12_is_downlink"

# Sender the mock server claims the message is FROM -- must be something
# the iGate has never heard directly on RF (a "sender already local" skip
# in listenAPRSIS(), aprs_is_utils.cpp:293-297), so a fake/unused callsign.
DOWNLINK_SENDER = "DLTEST"
DOWNLINK_MSGNO = "01"
DOWNLINK_TEXT = "downlink-test-message"

CLIENT_CONNECT_TIMEOUT = 30.0
TRIGGER_RX_TIMEOUT = 15.0
DOWNLINK_TX_TIMEOUT = 20.0


def _fail(failure_mode: str, evidence: list[str], notes: str) -> TestResult:
    return TestResult(
        phase_name=PHASE_NAME, passed=False, failure_mode=failure_mode, evidence=evidence,
        latency_ms=None, notes=notes,
    )


def _parse_export_json(response_lines: list[str]) -> dict:
    started = False
    json_parts: list[str] = []
    for line in response_lines:
        if "BEGIN tracker_conf.json" in line:
            started = True
            continue
        if "END tracker_conf.json" in line:
            break
        if started:
            json_parts.append(line)
    return json.loads("".join(json_parts))


def _repoint_igate(igate, server: str, port: int, downlink_enabled: bool) -> None:
    # Deliberately left in SETUP mode on return (not exit_setup()) -- the
    # caller reboots next via reboot_and_reconnect(), which itself requires
    # SETUP mode.
    igate.ensure_setup_mode()
    igate.send_setup_cmd(f"aprsiss server {server}")
    igate.send_setup_cmd(f"aprsiss port {port}")
    igate.send_setup_cmd(f"aprsiss downlink {'on' if downlink_enabled else 'off'}")
    igate.send_setup_cmd("save")


def run(ctx: TestContext) -> TestResult:
    cfg = ctx.config
    bus = ctx.bus
    tracker = ctx.tracker
    igate = ctx.igate

    if not cfg.harness_lan_ip:
        return _fail("HARNESS_ERROR", [], "phase12_is_downlink requires --harness-lan-ip")

    # 1. Capture original config.
    igate.ensure_setup_mode()
    try:
        export_resp = igate.send_setup_cmd("export")
    except SerialCommandTimeout as exc:
        igate.exit_setup()
        return _fail("HARNESS_ERROR", exc.buffered_lines, f"'export' failed: {exc}")
    try:
        original_config = _parse_export_json(export_resp)
        original_aprsis = original_config["aprsIS"]
        original_server = original_aprsis["server"]
        original_port = original_aprsis["port"]
        original_downlink = original_aprsis["downlinkEnabled"]
    except (KeyError, ValueError) as exc:
        igate.exit_setup()
        return _fail("HARNESS_ERROR", export_resp, f"could not parse 'export' JSON: {exc}")
    igate.exit_setup()

    mock_server = MockAPRSISServer(port=cfg.mock_aprs_is_port)
    mock_server.start()

    try:
        # 2. Repoint at the mock server and reboot.
        _repoint_igate(igate, cfg.harness_lan_ip, mock_server.port, downlink_enabled=True)
        igate.reboot_and_reconnect(settle_delay=cfg.reboot_settle_delay)

        # Re-stage DEBUG log level and resume LOG-mode observation (lost on reboot).
        igate.enter_setup()
        igate.send_setup_cmd("log debug")
        igate.exit_setup()
        igate.enter_log()

        # 3. Confirm the iGate actually dialed into the mock server.
        if not mock_server.wait_for_client(timeout=CLIENT_CONNECT_TIMEOUT):
            return _fail(
                "APRSIS_NOT_CONNECTED",
                [],
                f"iGate never connected to the mock server at {cfg.harness_lan_ip}:{mock_server.port} "
                f"within {CLIENT_CONNECT_TIMEOUT}s after reboot",
            )

        # 4. Tracker beacons so the iGate has heard it "direct" recently.
        tracker.ensure_setup_mode()
        t_trigger = time.monotonic()
        tracker.send_setup_cmd("tx comment")
        tracker.exit_setup()
        tracker.ensure_log_mode()

        tracker_rx = bus.wait_for(
            igate.channel,
            is_lora_rx_from(cfg.tracker_callsign, direct_only=True),
            timeout=TRIGGER_RX_TIMEOUT,
            since=t_trigger,
        )
        if tracker_rx is None:
            return _fail(
                "IGATE_NO_RX",
                [],
                "iGate never heard the tracker's trigger beacon directly -- can't satisfy the "
                "addressee-heard-direct gate in listenAPRSIS()",
            )

        # 5. Push the downlink message from the mock server.
        addressee = cfg.tracker_callsign.ljust(9)[:9]
        downlink_line = f"{DOWNLINK_SENDER}>APRS::{addressee}:{DOWNLINK_TEXT}{{{DOWNLINK_MSGNO}"
        t_push = time.monotonic()
        mock_server.push_message(downlink_line)

        # 6. Watch for the iGate's wrapped third-party downlink TX.
        def _is_downlink_wrap(ev: LogEvent) -> bool:
            pkt_ok = is_lora_tx_from(cfg.igate_callsign)(ev)
            return pkt_ok and "}" in ev.raw and "TCPIP" in ev.raw and DOWNLINK_SENDER in ev.raw

        downlink_tx = bus.wait_for(igate.channel, _is_downlink_wrap, timeout=DOWNLINK_TX_TIMEOUT, since=t_push)
        evidence = [tracker_rx.raw]
        if downlink_tx is None:
            return _fail(
                "IGATE_NO_DOWNLINK_TX",
                evidence,
                "iGate never transmitted the wrapped downlink packet -- check "
                "aprsIS.downlinkEnabled, passcode validity, and the heard-direct window",
            )
        evidence.append(downlink_tx.raw)

        latency_ms = (downlink_tx.ts - t_push) * 1000
        passed = (
            f"}}{DOWNLINK_SENDER}>" in downlink_tx.raw
            and f",TCPIP,{cfg.igate_callsign}*" in downlink_tx.raw
            and f"::{addressee}:" in downlink_tx.raw
        )

        return TestResult(
            phase_name=PHASE_NAME,
            passed=passed,
            failure_mode=None if passed else "IGATE_NO_DOWNLINK_TX",
            evidence=evidence,
            latency_ms=latency_ms,
            notes="" if passed else f"downlink TX line didn't match expected wrapped form: {downlink_tx.raw!r}",
            details={"downlink_tx_raw": downlink_tx.raw},
        )
    finally:
        mock_server.stop()
        # Teardown: restore original server/port/downlink and reboot again.
        # This runs even if an assertion above failed -- leaving the iGate
        # pointed at a now-stopped mock server is the single worst outcome
        # this phase could cause.
        try:
            _repoint_igate(igate, original_server, original_port, downlink_enabled=original_downlink)
            igate.reboot_and_reconnect(settle_delay=cfg.reboot_settle_delay)
        except Exception:
            # Best-effort: if this also fails, run_test.py's own top-level
            # cleanup will still attempt resync_to_kiss(), and the README
            # documents the manual recovery steps.
            pass
