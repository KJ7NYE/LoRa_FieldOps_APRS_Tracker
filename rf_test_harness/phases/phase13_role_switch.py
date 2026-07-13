"""
Phase 13: role switching (digi device only). OPT-IN ONLY -- never in the
default phase list; only runs via explicit `--phases phase13_role_switch`
inclusion (see config.py's IS_DOWNLINK_PHASE-style "never auto-append"
convention -- this phase follows the same rule).

Confirmed 'role set <role>' only fully takes effect after save+reboot --
Config.deviceRole is read live for some behaviors (query gating, beacon
cadence, RX-upload gating) but DeviceRoleUtils::initializeRole() -- which
does the one-time startup work like WiFi STA bring-up -- only ever runs at
boot (main.cpp:159), never again on a live change.

Targets the digi device specifically: least disruptive to take out of its
normal working state mid-run, unlike the tracker/iGate which anchor every
other phase in the same run.

Confirmed digiMode is untouched by initializeTracker()/initializeDigipeater()/
initializeIGate() -- it's a config field independent of role (verified
against device_role.cpp) -- so this doesn't need to re-apply
'digi wide1+wide2' after switching back.

Given the cost of two more reboots, this phase catches broad exceptions
(not just its own assertions) so a hiccup partway through still runs the
restore-and-reboot teardown rather than crashing the whole harness
invocation with the digi stuck in Digipeater role.
"""

from __future__ import annotations

import time

from log_parser import is_digi_repeating_from, is_lora_rx_from, parse_kv_block
from phases.context import TestContext, TestResult

PHASE_NAME = "phase13_role_switch"

RELAY_CHECK_TIMEOUT = 10.0


def _fail(failure_mode: str, evidence: list[str], notes: str) -> TestResult:
    return TestResult(
        phase_name=PHASE_NAME, passed=False, failure_mode=failure_mode, evidence=evidence,
        latency_ms=None, notes=notes,
    )


def _switch_role(digi, role: str, settle_delay: float) -> None:
    digi.ensure_setup_mode()
    digi.send_setup_cmd(f"role set {role}")
    digi.send_setup_cmd("save")
    digi.reboot_and_reconnect(settle_delay=settle_delay)


def _read_role(digi) -> str:
    digi.ensure_setup_mode()
    role_kv = parse_kv_block(digi.send_setup_cmd("role show"))
    digi.exit_setup()
    return role_kv.get("role", "")


def _verify_digipeating_still_works(ctx: TestContext) -> TestResult | None:
    """Trigger a tracker beacon, confirm digi still hears+relays it under
    its new role. Returns a failing TestResult if broken, None if OK."""
    cfg = ctx.config
    bus = ctx.bus
    tracker = ctx.tracker
    digi = ctx.extra_devices["digi"]

    digi.ensure_log_mode()
    tracker.ensure_setup_mode()
    t_trigger = time.monotonic()
    tracker.send_setup_cmd("tx comment")
    tracker.exit_setup()

    digi_rx = bus.wait_for(
        digi.channel, is_lora_rx_from(cfg.tracker_callsign), timeout=RELAY_CHECK_TIMEOUT, since=t_trigger
    )
    if digi_rx is None:
        return _fail("ROLE_SWITCH_DIGI_BROKEN", [], "digi never heard the beacon after switching role")

    relay_ev = bus.wait_for(
        digi.channel, is_digi_repeating_from(cfg.tracker_callsign), timeout=RELAY_CHECK_TIMEOUT, since=digi_rx.ts
    )
    if relay_ev is None:
        return _fail(
            "ROLE_SWITCH_DIGI_BROKEN", [digi_rx.raw], "digi heard the beacon but never relayed it after switching role"
        )
    return None


def run(ctx: TestContext) -> TestResult:
    cfg = ctx.config
    digi = ctx.extra_devices.get("digi")
    if digi is None:
        return _fail("DIGI_NOT_CONFIGURED", [], "phase13_role_switch requires --digi-port")

    original_role = _read_role(digi)
    if not original_role:
        return _fail("HARNESS_ERROR", [], "could not read digi's current role before switching")

    restore_failed_notes = ""
    try:
        try:
            _switch_role(digi, "digipeater", cfg.reboot_settle_delay)
            new_role = _read_role(digi)
            if new_role != "Digipeater":
                return _fail(
                    "ROLE_SWITCH_NOT_APPLIED", [], f"expected role 'Digipeater' after switch, got '{new_role}'"
                )

            broken = _verify_digipeating_still_works(ctx)
            if broken is not None:
                return broken

            return TestResult(
                phase_name=PHASE_NAME,
                passed=True,
                failure_mode=None,
                evidence=[],
                latency_ms=None,
                notes="",
                details={"original_role": original_role, "switched_to": "Digipeater"},
            )
        except Exception as exc:  # noqa: BLE001 -- deliberately broad, see module docstring
            return _fail("HARNESS_ERROR", [], f"unexpected error mid-switch: {exc!r}")
    finally:
        try:
            _switch_role(digi, original_role.lower(), cfg.reboot_settle_delay)
            restored_role = _read_role(digi)
            if restored_role != original_role:
                restore_failed_notes = (
                    f"WARNING: digi role restore may have failed -- expected '{original_role}', "
                    f"read back '{restored_role}'. Check manually via 'setup' -> 'role show'."
                )
                print(restore_failed_notes)
            digi.ensure_log_mode()
        except Exception as exc:
            print(
                f"WARNING: phase13_role_switch teardown failed ({exc!r}) -- digi may be stuck in "
                f"Digipeater role. Recover manually: connect to it, 'setup' -> "
                f"'role set {original_role.lower()}' -> 'save' -> 'reboot'."
            )
