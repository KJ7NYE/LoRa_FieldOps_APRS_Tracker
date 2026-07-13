"""
TestContext and TestResult -- split out from phases/__init__.py so phase
submodules can import them without a circular dependency on the package
__init__ (which itself imports the phase submodules to build PHASE_REGISTRY).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional

from aprs_is_tap import APRSISTap
from config import HarnessConfig
from device_session import DeviceSession
from preflight import PreflightResult
from serial_link import EventBus


@dataclass
class TestContext:
    tracker: DeviceSession
    igate: DeviceSession
    bus: EventBus
    tap: APRSISTap
    config: HarnessConfig
    preflight: PreflightResult
    extra_devices: dict[str, DeviceSession] = field(default_factory=dict)
    # Cross-phase scratch data (t_trigger, phase1_rx_ts, ...) -- deliberately
    # untyped so future phases can stash whatever they need without editing
    # this dataclass.
    state: dict[str, object] = field(default_factory=dict)


@dataclass
class TestResult:
    phase_name: str
    passed: bool
    failure_mode: Optional[str]
    evidence: list[str]
    latency_ms: Optional[float]
    notes: str
    details: dict[str, object] = field(default_factory=dict)
