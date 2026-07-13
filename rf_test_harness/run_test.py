#!/usr/bin/env python3
"""
CLI entrypoint: validates the Tracker -> iGate -> APRS-IS RF chain.

    python run_test.py --list-ports
    python run_test.py --tracker-port COM5 --igate-port COM7
    python run_test.py --tracker-port COM5 --igate-port COM7 --runs 3 --vary-comment

See README.md for wiring notes, prerequisites, and known limitations.
"""

from __future__ import annotations

import sys
import time

import serial.tools.list_ports

import report as report_mod
from aprs_is_tap import APRSISTap, APRSISTapError
from config import parse_args
from device_session import DeviceSession, DirtyConfigError, ModeSwitchTimeout, SerialCommandTimeout
from log_parser import is_beacon_tx_from
from phases import PHASE_REGISTRY, phase1_rf_link
from phases.context import TestContext, TestResult
from preflight import PreflightError, run_preflight
from serial_link import EventBus


def list_ports() -> None:
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return
    for p in ports:
        print(f"{p.device}\t{p.description}\t{p.hwid}")


def set_tracker_comment(tracker: DeviceSession, comment: str) -> None:
    """Writes flash (SETUP 'save'). Used by --vary-comment to dodge the
    upload dedup window, and to restore the original comment at teardown."""
    tracker.enter_setup()
    try:
        tracker.send_setup_cmd(f"beacon comment {comment}")
        tracker.send_setup_cmd("save")
    finally:
        tracker.exit_setup()


def trigger_via_natural_beacon(ctx: TestContext) -> TestResult:
    """--wait-for-natural-beacon alternative to phase1_rf_link's 'tx comment'
    trigger: waits for a naturally-timed SmartBeacon send with the tracker
    already sitting in LOG mode, which -- unlike the SETUP-triggered path --
    does yield the tracker's own Beacon:TX/LoRa Tx log lines. Slower (governed
    by the live SmartBeacon interval), used as an occasional gold-standard
    run rather than for iterative testing."""
    cfg = ctx.config
    tracker = ctx.tracker
    if tracker.mode != "log":
        tracker.enter_log()

    t0 = time.monotonic()
    ev = ctx.bus.wait_for(
        tracker.channel,
        is_beacon_tx_from(cfg.tracker_callsign),
        timeout=cfg.natural_beacon_timeout,
        since=t0,
    )
    if ev is None:
        return TestResult(
            phase_name=phase1_rf_link.PHASE_NAME,
            passed=False,
            failure_mode="IGATE_NO_RX",
            evidence=[],
            latency_ms=None,
            notes=f"no natural beacon observed on the tracker within {cfg.natural_beacon_timeout}s",
        )

    ctx.state["t_trigger"] = ev.ts
    return phase1_rf_link.wait_and_validate_rx(ctx, ev.ts)


def run_one_attempt(ctx: TestContext, use_natural_beacon: bool) -> list[TestResult]:
    ctx.state.clear()
    results: list[TestResult] = []

    for i, name in enumerate(ctx.config.phases):
        if i > 0 and ctx.extra_devices.get("digi") is not None:
            # Every beacon a phase triggers gets relayed by the digi ~1-2s
            # later (its own 200ms collision-avoidance wait + TX airtime,
            # digi_utils.cpp). Moving straight to the next phase's trigger
            # risks a real RF collision between that still-in-flight relay
            # and the new transmission -- confirmed reproducible (phase7
            # failed IGATE_NO_RX twice in a row immediately after phase6,
            # but passed reliably standalone) before this delay was added.
            time.sleep(ctx.config.phase_settle_delay)
        if name == "phase1_rf_link" and use_natural_beacon:
            result = trigger_via_natural_beacon(ctx)
        else:
            phase_fn = PHASE_REGISTRY.get(name)
            if phase_fn is None:
                raise SystemExit(f"unknown phase '{name}' -- available: {', '.join(PHASE_REGISTRY)}")
            result = phase_fn(ctx)
        results.append(result)
        if not result.passed:
            break  # no point running phase2 if phase1 never got a packet, etc.

    return results


def main(argv: list[str] | None = None) -> int:
    args, cfg = parse_args(argv)

    if args.list_ports:
        list_ports()
        return 0

    assert cfg is not None
    bus = EventBus()
    # tracker (nRF52840 TinyUSB CDC) needs dtr=True held or it never flushes
    # output; igate (ESP32-S3, classic auto-reset/bootstrap wiring, USR
    # button on GPIO0) must NOT have a control line held asserted for the
    # session -- that reads as a sustained button hold and triggers WiFi AP
    # mode past the firmware's 8s threshold. See DeviceSession's docstring.
    tracker = DeviceSession(cfg.tracker_port, "tracker", bus, dtr=True, rts=False)
    igate = DeviceSession(cfg.igate_port, "igate", bus, dtr=False, rts=False)
    digi: DeviceSession | None = None
    if cfg.digi_port:
        digi = DeviceSession(cfg.digi_port, "digi", bus, dtr=cfg.digi_dtr_assert, rts=False)
    tap = APRSISTap(bus, host=cfg.aprs_is_tap_host, port=cfg.aprs_is_tap_port)

    try:
        try:
            tracker.open()
        except Exception as exc:
            print(f"ERROR: could not open tracker port {cfg.tracker_port}: {exc}", file=sys.stderr)
            print("(Is the port number correct? Is another program -- Arduino Serial Monitor, "
                  "PuTTY, a prior harness run -- still holding it open?)", file=sys.stderr)
            return 2
        try:
            igate.open()
        except Exception as exc:
            print(f"ERROR: could not open igate port {cfg.igate_port}: {exc}", file=sys.stderr)
            print("(Is the port number correct? Is another program still holding it open?)", file=sys.stderr)
            return 2
        if digi is not None:
            try:
                digi.open()
            except Exception as exc:
                print(f"ERROR: could not open digi port {cfg.digi_port}: {exc}", file=sys.stderr)
                print("(Is the port number correct? Is another program still holding it open?)", file=sys.stderr)
                return 2

        # Start the independent APRS-IS tap before pre-flight so it's already
        # primed with runway by the time Phase 3 needs it.
        try:
            tap.connect(cfg.monitor_callsign, filter_str=f"p/{cfg.monitor_callsign}")
        except APRSISTapError as exc:
            print(f"ERROR: could not connect to APRS-IS tap: {exc}", file=sys.stderr)
            return 2

        try:
            preflight = run_preflight(tracker, igate, cfg, digi=digi)
        except PreflightError as exc:
            print(f"PRE-FLIGHT FAILED [{exc.label}]: {exc}", file=sys.stderr)
            return 1
        except (ModeSwitchTimeout, SerialCommandTimeout, DirtyConfigError) as exc:
            print(f"PRE-FLIGHT FAILED [HARNESS_ERROR]: {exc}", file=sys.stderr)
            return 1

        extra_devices = {"digi": digi} if digi is not None else {}
        ctx = TestContext(
            tracker=tracker,
            igate=igate,
            bus=bus,
            tap=tap,
            config=cfg,
            preflight=preflight,
            extra_devices=extra_devices,
        )

        attempts: list[list[TestResult]] = []
        try:
            for i in range(cfg.runs):
                if cfg.vary_comment:
                    set_tracker_comment(tracker, f"RUN-{i}-{int(time.time())}")
                elif i > 0:
                    time.sleep(cfg.run_spacing)

                results = run_one_attempt(ctx, use_natural_beacon=cfg.wait_for_natural_beacon)
                attempts.append(results)
        finally:
            if cfg.vary_comment:
                set_tracker_comment(tracker, preflight.tracker_original_comment)

        report = report_mod.build_report(cfg, preflight, attempts)
        report_mod.print_console_table(report)
        json_path, md_path = report_mod.write_reports(report, cfg.report_dir)
        print(f"\nReport written to {json_path} and {md_path}")

        return 0 if report["overall_passed"] else 1

    finally:
        # Best-effort: leave every device back in KISS mode regardless of
        # how the run went (success, failure, or an unhandled exception).
        for dev in (tracker, igate, digi):
            if dev is None:
                continue
            try:
                if dev.mode != "unknown":
                    dev.resync_to_kiss()
            except Exception:
                pass
            dev.close()
        tap.close()


if __name__ == "__main__":
    sys.exit(main())
