"""
CLI argument parsing and the HarnessConfig produced from it.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from pathlib import Path

from constants import (
    DEFAULT_APRS_IS_TAP_HOST,
    DEFAULT_APRS_IS_TAP_PORT,
    DEFAULT_PHASE1_TIMEOUT,
    DEFAULT_PHASE2_TIMEOUT,
    DEFAULT_PHASE3_TIMEOUT,
    DEFAULT_RUN_SPACING,
)

DEFAULT_TRACKER_CALLSIGN = "KG7KMV-5"
DEFAULT_IGATE_CALLSIGN = "KG7KMV-3"
DEFAULT_MONITOR_CALLSIGN = "KG7KMV"
DEFAULT_DIGI_CALLSIGN = "K7SWI"
# phase4_digipeat_relay reuses ctx.state["t_trigger"]/["phase1_rx_ts"] set by
# phase1_rf_link rather than sending its own trigger -- and ctx.state is only
# cleared once per *attempt*, not between phases (see run_test.py's
# run_one_attempt). So phase4 MUST run immediately after phase1-3, before any
# other phase sends its own beacon and makes that state stale/wrong. Groups A
# phases (5-8) go after the core+digi-relay chain; phase9 (also digi/WIDE2,
# but self-contained -- it triggers its own beacon) goes last.
CORE_PHASE_ORDER = ["phase1_rf_link", "phase2_igate_upload", "phase3_external_feed"]
DIGI_RELAY_PHASE = "phase4_digipeat_relay"
GROUP_A_PHASE_ORDER = [
    "phase5_status_beacon",
    "phase6_phg_beacon",
    "phase7_mice_beacon",
    "phase8_nogate_filter",
]
DIGI_WIDE2_PHASE = "phase9_wide2_digipeat"
DIGI_DEDUP_PHASE = "phase14_digi_dedup"
# phase11 needs only the tracker+iGate devices already required for
# everything else (injects through the tracker's own serial port), so it's
# unconditionally on. phase10 needs --igate-lan-ip (see HarnessConfig) and
# is gated on that being supplied.
ECHO_REJECTION_PHASE = "phase11_echo_rejection"
QUERY_RESPONSE_PHASE = "phase10_query_response"
# Opt-in only -- see the "never auto-appended" note in parse_args().
IS_DOWNLINK_PHASE = "phase12_is_downlink"
# Flattened for anything that just wants "the default order" as one list
# (e.g. --help text) -- --digi-port's and --igate-lan-ip's effects on
# ordering happen in parse_args(), not here, since they insert phases
# mid-list rather than appending.
DEFAULT_PHASE_ORDER = CORE_PHASE_ORDER + GROUP_A_PHASE_ORDER + [ECHO_REJECTION_PHASE]
# Anchored to this file's own location, not the process cwd -- run_test.py is
# meant to be run either from the repo root or from inside rf_test_harness/
# (both are shown in README.md), and a cwd-relative default silently landed
# reports in a nested rf_test_harness/rf_test_harness/reports/ the first time
# this was run from inside the directory.
DEFAULT_REPORT_DIR = str(Path(__file__).resolve().parent / "reports")


@dataclass
class HarnessConfig:
    tracker_port: str
    igate_port: str
    tracker_callsign: str = DEFAULT_TRACKER_CALLSIGN
    igate_callsign: str = DEFAULT_IGATE_CALLSIGN
    monitor_callsign: str = DEFAULT_MONITOR_CALLSIGN
    digi_port: str | None = None
    digi_callsign: str = DEFAULT_DIGI_CALLSIGN
    # heltec_t114 (nRF52840) needs dtr=True held or its TinyUSB CDC never
    # flushes output; an ESP32 board must NOT have a control line held for
    # the session (see DeviceSession's docstring -- this is what caused the
    # iGate to enter WiFi AP mode mid-test earlier). Default is ESP32-safe;
    # pass --digi-dtr-assert if the digipeater is itself an nRF52840 board.
    digi_dtr_assert: bool = False
    # Every beacon a phase triggers gets relayed by the digi ~1-2s later; a
    # gap between phases avoids the next phase's own trigger colliding on-air
    # with that still-in-flight relay (see run_one_attempt in run_test.py).
    # Only applied when a digi is configured.
    phase_settle_delay: float = 2.0
    # Needed only by phase10_query_response, which injects a packet through
    # the iGate's TCP KISS server (WiFi, not USB serial) -- no CLI command
    # exposes the iGate's own IP, and auto-detection (matching this
    # machine's subnet) isn't reliable when the iGate is on a different
    # network; provide it explicitly.
    igate_lan_ip: str | None = None
    igate_tcp_kiss_port: int = 8001
    # phase12_is_downlink (opt-in via --is-downlink): the iGate dials BACK to
    # this harness-running machine's LAN IP to reach the mock APRS-IS server,
    # so it must be supplied explicitly -- same reasoning as igate_lan_ip,
    # auto-detection isn't reliable across multi-NIC/VPN setups.
    is_downlink: bool = False
    harness_lan_ip: str | None = None
    mock_aprs_is_port: int = 0  # 0 = OS-assigned free port
    reboot_settle_delay: float = 8.0
    aprs_is_tap_host: str = DEFAULT_APRS_IS_TAP_HOST
    aprs_is_tap_port: int = DEFAULT_APRS_IS_TAP_PORT
    phase1_timeout: float = DEFAULT_PHASE1_TIMEOUT
    phase2_timeout: float = DEFAULT_PHASE2_TIMEOUT
    phase3_timeout: float = DEFAULT_PHASE3_TIMEOUT
    runs: int = 1
    run_spacing: float = DEFAULT_RUN_SPACING
    vary_comment: bool = False
    wait_for_natural_beacon: bool = False
    natural_beacon_timeout: float = 900.0
    report_dir: str = DEFAULT_REPORT_DIR
    phases: list[str] = field(default_factory=lambda: list(DEFAULT_PHASE_ORDER))


def build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="run_test.py",
        description=(
            "RF protocol test harness: validates Tracker -> iGate -> APRS-IS "
            "over LoRa, driving both devices' serial CLIs and an independent "
            "read-only APRS-IS tap."
        ),
    )
    p.add_argument(
        "--list-ports",
        action="store_true",
        help="List available serial ports (device, description, VID:PID) and exit.",
    )
    p.add_argument("--tracker-port", help="COM port for the tracker (e.g. COM5).")
    p.add_argument("--igate-port", help="COM port for the iGate (e.g. COM7).")
    p.add_argument(
        "--tracker-callsign", default=DEFAULT_TRACKER_CALLSIGN, help="Expected tracker callsign-SSID."
    )
    p.add_argument(
        "--igate-callsign", default=DEFAULT_IGATE_CALLSIGN, help="Expected iGate callsign-SSID."
    )
    p.add_argument(
        "--monitor-callsign",
        default=DEFAULT_MONITOR_CALLSIGN,
        help="Callsign used to log into the read-only APRS-IS tap.",
    )
    p.add_argument(
        "--digi-port",
        help=(
            "COM port for an optional digipeater (e.g. COM9). When set, "
            f"'{DIGI_RELAY_PHASE}' and '{DIGI_WIDE2_PHASE}' are added to the default "
            "phase list, validating WIDE1-1 fill-in and WIDE2-2 multi-hop relay."
        ),
    )
    p.add_argument("--digi-callsign", default=DEFAULT_DIGI_CALLSIGN, help="Expected digipeater callsign-SSID.")
    p.add_argument(
        "--digi-dtr-assert",
        action="store_true",
        help="Hold dtr=True on the digipeater's serial connection (needed for an nRF52840 board; do NOT set for an ESP32 board, see README).",
    )
    p.add_argument(
        "--phase-settle-delay",
        type=float,
        default=2.0,
        help=(
            "Seconds to wait between phases when --digi-port is set, so the next phase's "
            "trigger doesn't collide on-air with the digi's still-in-flight relay of the "
            "previous phase's beacon."
        ),
    )
    p.add_argument(
        "--igate-lan-ip",
        help=(
            "iGate's WiFi IP on your local network (e.g. 192.168.1.42). No CLI command exposes "
            f"this and auto-detection isn't reliable, so it must be given explicitly. When set, "
            f"'{QUERY_RESPONSE_PHASE}' is added to the default phase list."
        ),
    )
    p.add_argument(
        "--igate-tcp-kiss-port",
        type=int,
        default=8001,
        help="iGate's TCP KISS port (src/tcp_kiss_utils.cpp default is 8001).",
    )
    p.add_argument(
        "--is-downlink",
        action="store_true",
        help=(
            "Opt-in: run phase12_is_downlink (never auto-added to the default phase list -- "
            "requires --harness-lan-ip, reboots the iGate twice, and temporarily repoints it "
            "at a local mock APRS-IS server instead of its real one). Use --phases "
            "phase12_is_downlink explicitly, or list it alongside other phases."
        ),
    )
    p.add_argument(
        "--harness-lan-ip",
        help=(
            "This machine's LAN IP, reachable from the iGate (a separate physical device). "
            "Required for phase12_is_downlink -- the iGate dials back to this address for the "
            "mock APRS-IS server. Auto-detection isn't attempted (unreliable on multi-NIC/VPN "
            "setups); a wrong guess would silently make the iGate unable to connect."
        ),
    )
    p.add_argument("--mock-aprs-is-port", type=int, default=0, help="Mock APRS-IS server port (0 = OS-assigned).")
    p.add_argument(
        "--reboot-settle-delay",
        type=float,
        default=8.0,
        help="Seconds to wait after triggering a reboot before reopening the serial port.",
    )
    p.add_argument("--aprs-is-tap-host", default=DEFAULT_APRS_IS_TAP_HOST)
    p.add_argument("--aprs-is-tap-port", type=int, default=DEFAULT_APRS_IS_TAP_PORT)
    p.add_argument("--phase1-timeout", type=float, default=DEFAULT_PHASE1_TIMEOUT)
    p.add_argument("--phase2-timeout", type=float, default=DEFAULT_PHASE2_TIMEOUT)
    p.add_argument("--phase3-timeout", type=float, default=DEFAULT_PHASE3_TIMEOUT)
    p.add_argument("--runs", type=int, default=1, help="Number of trigger-and-observe iterations.")
    p.add_argument(
        "--run-spacing",
        type=float,
        default=DEFAULT_RUN_SPACING,
        help=(
            "Seconds to wait between consecutive runs when --runs > 1. Default is "
            "just over the iGate's 60s upload-dedup TTL so repeated identical "
            "beacons don't spuriously fail Phase 2 (see README 'Repeatability')."
        ),
    )
    p.add_argument(
        "--vary-comment",
        action="store_true",
        help=(
            "Set a unique 'beacon comment' before each trigger to dodge the upload "
            "dedup window instead of waiting --run-spacing seconds. WARNING: this "
            "writes flash on every run (SETUP 'save') and temporarily overwrites "
            "the tracker's persisted beacon comment; the harness restores the "
            "original comment and saves once at teardown."
        ),
    )
    p.add_argument(
        "--wait-for-natural-beacon",
        action="store_true",
        help=(
            "Skip 'tx comment' and instead wait for a naturally-timed SmartBeacon "
            "send with the tracker sitting in LOG mode the whole time. Slower, but "
            "yields the tracker's own TX log line as a 4th checkpoint -- the "
            "SETUP-triggered path can't observe it (see README 'Known Limitations')."
        ),
    )
    p.add_argument(
        "--natural-beacon-timeout",
        type=float,
        default=900.0,
        help="Max seconds to wait for a natural beacon when --wait-for-natural-beacon is set.",
    )
    p.add_argument("--report-dir", default=DEFAULT_REPORT_DIR)
    p.add_argument(
        "--phases",
        default=None,
        help=(
            "Comma-separated phase names to run, in order. Default: "
            f"{','.join(CORE_PHASE_ORDER)}, {DIGI_RELAY_PHASE} (if --digi-port), "
            f"{','.join(GROUP_A_PHASE_ORDER)}, {DIGI_WIDE2_PHASE}, {DIGI_DEDUP_PHASE} (if --digi-port), "
            f"{QUERY_RESPONSE_PHASE} (if --igate-lan-ip), {ECHO_REJECTION_PHASE}."
        ),
    )
    return p


def parse_args(argv: list[str] | None = None) -> tuple[argparse.Namespace, HarnessConfig | None]:
    parser = build_arg_parser()
    args = parser.parse_args(argv)

    if args.list_ports:
        return args, None

    if not args.tracker_port or not args.igate_port:
        parser.error("--tracker-port and --igate-port are required (or use --list-ports)")

    if args.phases is not None:
        phases = [p.strip() for p in args.phases.split(",") if p.strip()]
    else:
        phases = list(CORE_PHASE_ORDER)
        if args.digi_port:
            phases.append(DIGI_RELAY_PHASE)
        phases.extend(GROUP_A_PHASE_ORDER)
        if args.digi_port:
            phases.append(DIGI_WIDE2_PHASE)
            phases.append(DIGI_DEDUP_PHASE)
        if args.igate_lan_ip:
            phases.append(QUERY_RESPONSE_PHASE)
        phases.append(ECHO_REJECTION_PHASE)
        # phase12_is_downlink is NEVER auto-appended here even with
        # --is-downlink set -- it's opt-in-only given the reboot cost,
        # unlike every other optional phase above. Run it via --phases
        # phase12_is_downlink explicitly.

    if IS_DOWNLINK_PHASE in phases and not args.harness_lan_ip:
        parser.error(f"{IS_DOWNLINK_PHASE} requires --harness-lan-ip")

    cfg = HarnessConfig(
        tracker_port=args.tracker_port,
        igate_port=args.igate_port,
        tracker_callsign=args.tracker_callsign,
        igate_callsign=args.igate_callsign,
        monitor_callsign=args.monitor_callsign,
        digi_port=args.digi_port,
        digi_callsign=args.digi_callsign,
        digi_dtr_assert=args.digi_dtr_assert,
        phase_settle_delay=args.phase_settle_delay,
        igate_lan_ip=args.igate_lan_ip,
        igate_tcp_kiss_port=args.igate_tcp_kiss_port,
        is_downlink=args.is_downlink,
        harness_lan_ip=args.harness_lan_ip,
        mock_aprs_is_port=args.mock_aprs_is_port,
        reboot_settle_delay=args.reboot_settle_delay,
        aprs_is_tap_host=args.aprs_is_tap_host,
        aprs_is_tap_port=args.aprs_is_tap_port,
        phase1_timeout=args.phase1_timeout,
        phase2_timeout=args.phase2_timeout,
        phase3_timeout=args.phase3_timeout,
        runs=args.runs,
        run_spacing=args.run_spacing,
        vary_comment=args.vary_comment,
        wait_for_natural_beacon=args.wait_for_natural_beacon,
        natural_beacon_timeout=args.natural_beacon_timeout,
        report_dir=args.report_dir,
        phases=phases,
    )
    return args, cfg
