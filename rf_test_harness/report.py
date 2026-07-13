"""
Report generation: console summary table, JSON, and Markdown -- all built
from the same plain-dict representation so the three views can't drift.
"""

from __future__ import annotations

import json
import time
from dataclasses import asdict
from pathlib import Path

from config import HarnessConfig
from phases.context import TestResult
from preflight import PreflightResult

# Maps each failure-mode label (raised by preflight.py or a phase module) to
# a one-line hint about which subsystem it points at, so a failing run tells
# the operator where to look without them having to know the codebase.
FAILURE_MODE_HINTS: dict[str, str] = {
    "PREFLIGHT_IDENTITY_MISMATCH": "wrong device on wrong COM port",
    "LORA_PARAM_MISMATCH": "config drift between devices -- RF params must match exactly",
    "GPS_NOT_FIXED": "tracker GPS/antenna/placement",
    "WIFI_NOT_CONNECTED": "iGate WiFi STA configuration",
    "APRSIS_NOT_CONNECTED": "iGate APRS-IS server/network configuration",
    "TRACKER_TX_COMMAND_REJECTED": "firmware/config state on the tracker",
    "IGATE_NO_RX": "RF path -- antenna, distance, frequency, radio fault",
    "IGATE_RX_CONTENT_MISMATCH": "RF path -- packet arrived corrupted or with unexpected content",
    "IGATE_RX_BUT_NO_UPLOAD": "iGate gating logic -- dedup collision, NOGATE, WiFi drop",
    "UPLOADED_BUT_NOT_ON_FEED": "APRS-IS network/server, not the firmware",
    "DIGI_MODE_NOT_ACTIVE": "digipeater config -- 'digi wide1'/'digi wide1+wide2' not set",
    "DIGI_NOT_CONFIGURED": "harness invocation -- no --digi-port given but phase4 was requested",
    "DIGI_PHASE_PRECONDITION_MISSING": "harness phase ordering -- phase1_rf_link must run first",
    "DIGI_NO_RX": "digipeater RF path -- antenna, distance, frequency, radio fault",
    "DIGI_NO_RELAY": "digipeater digipeat logic -- mode/path-already-consumed",
    "IGATE_NO_DIGI_RX": "iGate RF path to the digipeater specifically -- range/antenna",
    "IGATE_DOUBLE_UPLOAD": "iGate upload-dedup logic -- failed to suppress a path-only duplicate",
    "TRACKER_STATUS_TX_REJECTED": "firmware/config state on the tracker (status beacon)",
    "IGATE_STATUS_RX_CONTENT_MISMATCH": "status/position DTI or fallback logic on the tracker",
    "TRACKER_PHG_ENABLE_REJECTED": "firmware/config state on the tracker (PHG)",
    "IGATE_PHG_RX_CONTENT_MISMATCH": "PHG beacon generation on the tracker",
    "TRACKER_MICE_CONFIG_REJECTED": "firmware/config state on the tracker (Mic-E/tactical)",
    "IGATE_MICE_RX_STRUCTURAL_MISMATCH": "Mic-E encoding on the tracker",
    "IGATE_NOGATE_LEAK": "iGate NOGATE filtering -- uploaded a packet it should have skipped",
    "DIGI_NOGATE_LEAK": "digipeater NOGATE filtering -- relayed a packet it should have skipped",
    "DIGI_WIDE2_NO_DECREMENT": "digipeater WIDE2-n substitution logic",
    "IGATE_KISS_INJECT_NOT_TXD": "harness KISS injection -- iGate never transmitted the injected frame",
    "TRACKER_NO_QUERY_REPLY": "query_utils.cpp on the tracker -- never replied to the injected query",
    "TRACKER_QUERY_REPLY_CONTENT_MISMATCH": "query_utils.cpp reply content on the tracker",
    "IGATE_ECHO_NOT_REJECTED": (
        "iGate echo-rejection heuristic -- uploaded a packet it should have skipped "
        "(NOTE: as of this harness's development, this feature exists only in an uncommitted "
        "local diff to src/aprs_is_utils.cpp -- confirm it's actually flashed before treating "
        "this as a firmware bug)"
    ),
    "HARNESS_ERROR": "harness invocation/setup issue, not firmware behavior -- see notes",
    "IGATE_NO_DOWNLINK_TX": "iGate IS->RF downlink gating -- passcode/downlinkEnabled/heard-direct window",
    "ROLE_SWITCH_NOT_APPLIED": "role set/save/reboot cycle on the digi -- role never actually changed",
    "ROLE_SWITCH_DIGI_BROKEN": "digipeating stopped working under the new role",
    "DIGI_DEDUP_LEAK": "digi's own upload-independent dedup (isInHashBuffer) -- failed to suppress a "
    "byte-identical repeat within the 60s TTL",
}


def _run_id(now: float | None = None) -> str:
    return time.strftime("%Y%m%dT%H%M%SZ", time.gmtime(now if now is not None else time.time()))


def build_report(
    cfg: HarnessConfig, preflight: PreflightResult, attempts: list[list[TestResult]]
) -> dict:
    attempt_dicts = []
    for i, results in enumerate(attempts):
        attempt_dicts.append(
            {
                "index": i,
                "passed": all(r.passed for r in results),
                "phases": [asdict(r) for r in results],
            }
        )
    return {
        "generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "config": {
            "tracker_port": cfg.tracker_port,
            "igate_port": cfg.igate_port,
            "tracker_callsign": cfg.tracker_callsign,
            "igate_callsign": cfg.igate_callsign,
            "monitor_callsign": cfg.monitor_callsign,
            "digi_port": cfg.digi_port,
            "digi_callsign": cfg.digi_callsign if cfg.digi_port else None,
            "aprs_is_tap_host": cfg.aprs_is_tap_host,
            "aprs_is_tap_port": cfg.aprs_is_tap_port,
        },
        "preflight": asdict(preflight),
        "attempts": attempt_dicts,
        "overall_passed": bool(attempt_dicts) and all(a["passed"] for a in attempt_dicts),
    }


def print_console_table(report: dict) -> None:
    print()
    print("=" * 72)
    print(f"RF Test Harness Report  ({report['generated_at']})")
    print("=" * 72)
    cfg = report["config"]
    pf = report["preflight"]
    print(
        f"tracker: {cfg['tracker_callsign']} on {cfg['tracker_port']}"
        f"  (fw {pf['tracker_version']}, lora {pf['tracker_lora']})"
    )
    print(
        f"igate:   {cfg['igate_callsign']} on {cfg['igate_port']}"
        f"  (fw {pf['igate_version']}, lora {pf['igate_lora']})"
    )
    if cfg.get("digi_port"):
        print(
            f"digi:    {cfg['digi_callsign']} on {cfg['digi_port']}"
            f"  (fw {pf['digi_version']}, lora {pf['digi_lora']}, mode {pf['digi_mode']})"
        )
    print()
    for attempt in report["attempts"]:
        print(f"--- run {attempt['index'] + 1} ---")
        for phase in attempt["phases"]:
            status = "PASS" if phase["passed"] else "FAIL"
            line = f"  [{status}] {phase['phase_name']}"
            if phase["latency_ms"] is not None:
                line += f"  ({phase['latency_ms']:.0f} ms)"
            print(line)
            if not phase["passed"]:
                hint = FAILURE_MODE_HINTS.get(phase["failure_mode"], "")
                print(f"         failure_mode={phase['failure_mode']}  ({hint})")
                if phase["notes"]:
                    print(f"         {phase['notes']}")
    print()
    overall = "PASS" if report["overall_passed"] else "FAIL"
    print(f"Overall: {overall}")
    print("=" * 72)


def write_reports(report: dict, directory: str) -> tuple[Path, Path]:
    """Write both JSON and Markdown views under one shared run-id stamp so
    filenames always pair up, and return their paths."""
    out_dir = Path(directory)
    out_dir.mkdir(parents=True, exist_ok=True)
    run_id = _run_id()

    json_path = out_dir / f"{run_id}_run.json"
    json_path.write_text(json.dumps(report, indent=2), encoding="utf-8")

    md_path = out_dir / f"{run_id}_run.md"
    md_path.write_text(_render_markdown(report), encoding="utf-8")

    return json_path, md_path


def _render_markdown(report: dict) -> str:
    cfg = report["config"]
    lines: list[str] = []
    lines.append(f"# RF Test Harness Report -- {report['generated_at']}")
    lines.append("")
    lines.append(f"- Tracker: `{cfg['tracker_callsign']}` on `{cfg['tracker_port']}`")
    lines.append(f"- iGate: `{cfg['igate_callsign']}` on `{cfg['igate_port']}`")
    lines.append(f"- Overall: **{'PASS' if report['overall_passed'] else 'FAIL'}**")
    lines.append("")
    lines.append("## Pre-flight")
    lines.append("")
    lines.append("```json")
    lines.append(json.dumps(report["preflight"], indent=2))
    lines.append("```")
    lines.append("")

    for attempt in report["attempts"]:
        lines.append(f"## Run {attempt['index'] + 1} -- {'PASS' if attempt['passed'] else 'FAIL'}")
        lines.append("")
        lines.append("| Phase | Result | Latency | Failure mode | Notes |")
        lines.append("|---|---|---|---|---|")
        for phase in attempt["phases"]:
            status = "PASS" if phase["passed"] else "FAIL"
            latency = f"{phase['latency_ms']:.0f} ms" if phase["latency_ms"] is not None else "-"
            fm = phase["failure_mode"] or "-"
            notes = phase["notes"].replace("|", "\\|") if phase["notes"] else "-"
            lines.append(f"| {phase['phase_name']} | {status} | {latency} | {fm} | {notes} |")
        lines.append("")

        for phase in attempt["phases"]:
            if phase["evidence"]:
                lines.append(f"<details><summary>{phase['phase_name']} evidence</summary>")
                lines.append("")
                lines.append("```")
                lines.extend(phase["evidence"])
                lines.append("```")
                lines.append("</details>")
                lines.append("")

    return "\n".join(lines)
