"""
Pre-flight validation: confirm both devices are who/what we expect, that
their LoRa RF parameters match (a mismatch silently prevents RX with no
error on either side), and that the iGate has a live path to APRS-IS --
before spending any time on the RF phases themselves.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

from config import HarnessConfig
from device_session import DeviceSession
from log_parser import parse_kv_block


class PreflightError(Exception):
    def __init__(self, label: str, message: str):
        self.label = label
        super().__init__(f"{label}: {message}")


@dataclass(frozen=True)
class LoraParams:
    freq: str
    sf: str
    bw: str
    cr: str


@dataclass
class PreflightResult:
    tracker_version: str
    tracker_lora: LoraParams
    tracker_beacon_path: str
    tracker_original_comment: str
    igate_version: str
    igate_lora: LoraParams
    igate_passcode_hint: str | None
    digi_version: Optional[str] = None
    digi_lora: Optional[LoraParams] = None
    digi_mode: Optional[str] = None


def _lora_params(kv: dict[str, str]) -> LoraParams:
    return LoraParams(freq=kv.get("freq", ""), sf=kv.get("sf", ""), bw=kv.get("bw", ""), cr=kv.get("cr", ""))


def _check_tracker(tracker: DeviceSession, cfg: HarnessConfig) -> tuple[str, LoraParams, str, str]:
    tracker.enter_setup()
    try:
        version_kv = parse_kv_block(tracker.send_setup_cmd("version"))
        version = version_kv.get("version.date", "unknown")

        role_kv = parse_kv_block(tracker.send_setup_cmd("role show"))
        if role_kv.get("role") != "Tracker":
            raise PreflightError(
                "PREFLIGHT_IDENTITY_MISMATCH",
                f"device on {tracker.port} reports role='{role_kv.get('role')}', expected Tracker "
                f"(wrong device on this COM port?)",
            )

        beacons_kv = parse_kv_block(tracker.send_setup_cmd("show beacons"))
        actual_callsign = beacons_kv.get("callsign")
        if actual_callsign != cfg.tracker_callsign:
            raise PreflightError(
                "PREFLIGHT_IDENTITY_MISMATCH",
                f"device on {tracker.port} reports callsign='{actual_callsign}', "
                f"expected '{cfg.tracker_callsign}'",
            )
        original_comment = beacons_kv.get("comment", "")

        lora_kv = parse_kv_block(tracker.send_setup_cmd("show lora"))
        lora = _lora_params(lora_kv)

        other_kv = parse_kv_block(tracker.send_setup_cmd("show other"))
        beacon_path = other_kv.get("beaconPath", "")

        gps_kv = parse_kv_block(tracker.send_setup_cmd("gps read"))
        if gps_kv.get("gps.valid") != "1":
            raise PreflightError(
                "GPS_NOT_FIXED",
                f"tracker gps.valid={gps_kv.get('gps.valid')} -- 'tx comment' will silently "
                f"no-op with no GPS fix (see README 'Known Limitations')",
            )

        tracker.send_setup_cmd("log debug")
    finally:
        tracker.exit_setup()

    return version, lora, beacon_path, original_comment


def _check_igate(igate: DeviceSession, cfg: HarnessConfig) -> tuple[str, LoraParams, str | None]:
    igate.enter_setup()
    try:
        version_kv = parse_kv_block(igate.send_setup_cmd("version"))
        version = version_kv.get("version.date", "unknown")

        role_kv = parse_kv_block(igate.send_setup_cmd("role show"))
        if role_kv.get("role") != "iGate":
            raise PreflightError(
                "PREFLIGHT_IDENTITY_MISMATCH",
                f"device on {igate.port} reports role='{role_kv.get('role')}', expected iGate "
                f"(wrong device on this COM port?)",
            )

        beacons_kv = parse_kv_block(igate.send_setup_cmd("show beacons"))
        actual_callsign = beacons_kv.get("callsign")
        if actual_callsign != cfg.igate_callsign:
            raise PreflightError(
                "PREFLIGHT_IDENTITY_MISMATCH",
                f"device on {igate.port} reports callsign='{actual_callsign}', "
                f"expected '{cfg.igate_callsign}'",
            )

        lora_kv = parse_kv_block(igate.send_setup_cmd("show lora"))
        lora = _lora_params(lora_kv)

        wifi_kv = parse_kv_block(igate.send_setup_cmd("wifista status"))
        if wifi_kv.get("wifiSTA.connected") != "true":
            raise PreflightError(
                "WIFI_NOT_CONNECTED", f"iGate wifiSTA.connected={wifi_kv.get('wifiSTA.connected')}"
            )

        aprsis_kv = parse_kv_block(igate.send_setup_cmd("aprsiss status"))
        if aprsis_kv.get("aprsIS.connected") != "true":
            raise PreflightError(
                "APRSIS_NOT_CONNECTED", f"iGate aprsIS.connected={aprsis_kv.get('aprsIS.connected')}"
            )

        # Pre-stage DEBUG level now, from SETUP -- cmdLog() only applies a
        # level immediately when already in LOG mode; from SETUP it just
        # stores the value, and enterLog() applies it on the next LOG entry
        # (serial_setup.cpp:359). Without this, the DEBUG-only
        # "APRS-IS: Uploaded:"/"APRS-IS: Rx:" lines Phase 2/3 depend on would
        # never appear.
        igate.send_setup_cmd("log debug")

        # 'aprsiss status' doesn't expose passcode validity -- only the
        # connect-time log line does, and that may be stale/absent if the
        # iGate already had a live connection before the harness attached.
        # Not gated on; reported as a hint, authoritative check happens live
        # in Phase 2 via the qAR/qAO marker on the actual upload line.
        passcode_hint = None
    finally:
        igate.exit_setup()

    return version, lora, passcode_hint


def _check_digi(digi: DeviceSession, cfg: HarnessConfig) -> tuple[str, LoraParams, str]:
    # Digipeating is role-independent in this firmware (works on any
    # Config.deviceRole via DigiMode -- see CLAUDE.md), so unlike
    # tracker/igate there's no role to assert here, only identity/RF
    # params/digi-mode-active.
    digi.enter_setup()
    try:
        version_kv = parse_kv_block(digi.send_setup_cmd("version"))
        version = version_kv.get("version.date", "unknown")

        beacons_kv = parse_kv_block(digi.send_setup_cmd("show beacons"))
        actual_callsign = beacons_kv.get("callsign")
        if actual_callsign != cfg.digi_callsign:
            raise PreflightError(
                "PREFLIGHT_IDENTITY_MISMATCH",
                f"device on {digi.port} reports callsign='{actual_callsign}', "
                f"expected '{cfg.digi_callsign}'",
            )

        lora_kv = parse_kv_block(digi.send_setup_cmd("show lora"))
        lora = _lora_params(lora_kv)

        other_kv = parse_kv_block(digi.send_setup_cmd("show other"))
        digi_mode = other_kv.get("digiMode", "off")
        if digi_mode not in ("wide1", "wide1+wide2"):
            raise PreflightError(
                "DIGI_MODE_NOT_ACTIVE",
                f"device on {digi.port} has digiMode='{digi_mode}' -- set it with "
                f"'digi wide1' or 'digi wide1+wide2' via the SETUP CLI first",
            )

        digi.send_setup_cmd("log debug")
    finally:
        digi.exit_setup()

    return version, lora, digi_mode


def run_preflight(
    tracker: DeviceSession,
    igate: DeviceSession,
    cfg: HarnessConfig,
    digi: Optional[DeviceSession] = None,
) -> PreflightResult:
    tracker.resync_to_kiss()
    igate.resync_to_kiss()
    if digi is not None:
        digi.resync_to_kiss()

    tracker_version, tracker_lora, tracker_beacon_path, tracker_original_comment = _check_tracker(
        tracker, cfg
    )
    igate_version, igate_lora, igate_passcode_hint = _check_igate(igate, cfg)

    digi_version = digi_lora = digi_mode = None
    if digi is not None:
        digi_version, digi_lora, digi_mode = _check_digi(digi, cfg)

    lora_by_device = {"tracker": tracker_lora, "igate": igate_lora}
    if digi_lora is not None:
        lora_by_device["digi"] = digi_lora
    if len(set(lora_by_device.values())) > 1:
        raise PreflightError(
            "LORA_PARAM_MISMATCH",
            f"{lora_by_device} -- RF param mismatches produce no error on any device, "
            f"RX simply never happens",
        )

    # iGate (and digi, if present) stay in LOG mode (at the DEBUG level
    # pre-staged above) for the rest of the run: iGate has WiFi + a separate
    # TCP KISS port, and digi has no further SETUP-mode interaction needed,
    # so nothing else needs their USB serial once pre-flight is done.
    igate.enter_log()
    if digi is not None:
        digi.enter_log()

    return PreflightResult(
        tracker_version=tracker_version,
        tracker_lora=tracker_lora,
        tracker_beacon_path=tracker_beacon_path,
        tracker_original_comment=tracker_original_comment,
        igate_version=igate_version,
        igate_lora=igate_lora,
        igate_passcode_hint=igate_passcode_hint,
        digi_version=digi_version,
        digi_lora=digi_lora,
        digi_mode=digi_mode,
    )
