"""
Phase registry. Every phase is a module exposing run(ctx: TestContext) ->
TestResult; adding a future phase (second-tracker dedup, aprsdroid traffic)
means writing a new module and registering it here -- no changes to
serial_link.py, device_session.py, or existing phases.
"""

from __future__ import annotations

from typing import Callable

from phases import (
    phase1_rf_link,
    phase2_igate_upload,
    phase3_external_feed,
    phase4_digipeat_relay,
    phase5_status_beacon,
    phase6_phg_beacon,
    phase7_mice_beacon,
    phase8_nogate_filter,
    phase9_wide2_digipeat,
    phase10_query_response,
    phase11_echo_rejection,
    phase12_is_downlink,
    phase13_role_switch,
    phase14_digi_dedup,
)
from phases.context import TestContext, TestResult

__all__ = ["TestContext", "TestResult", "PHASE_REGISTRY"]

PHASE_REGISTRY: dict[str, Callable[[TestContext], TestResult]] = {
    "phase1_rf_link": phase1_rf_link.run,
    "phase2_igate_upload": phase2_igate_upload.run,
    "phase3_external_feed": phase3_external_feed.run,
    "phase4_digipeat_relay": phase4_digipeat_relay.run,
    "phase5_status_beacon": phase5_status_beacon.run,
    "phase6_phg_beacon": phase6_phg_beacon.run,
    "phase7_mice_beacon": phase7_mice_beacon.run,
    "phase8_nogate_filter": phase8_nogate_filter.run,
    "phase9_wide2_digipeat": phase9_wide2_digipeat.run,
    "phase10_query_response": phase10_query_response.run,
    "phase11_echo_rejection": phase11_echo_rejection.run,
    "phase12_is_downlink": phase12_is_downlink.run,
    "phase13_role_switch": phase13_role_switch.run,
    "phase14_digi_dedup": phase14_digi_dedup.run,
}
