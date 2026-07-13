"""
Parsing helpers for firmware log lines and TNC2 APRS packet text, plus a
library of EventBus predicates used by the pre-flight checks and every phase.

Keeping all of this in one place means a future phase (digipeat marker
verification, second-tracker dedup, etc.) can compose new predicates out of
the same parse_log_line()/parse_tnc2() primitives without re-deriving the
wire format.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Callable, Optional

from constants import (
    ANSI_ESCAPE_RE,
    APRS_IS_CONNECTED_PREFIX,
    APRS_IS_RX_PREFIX,
    APRS_IS_UPLOADED_PREFIX,
    DIGI_REPEATING_PREFIX,
    KV_RE,
    LOG_LEVEL_NORMALIZE,
    LOG_LINE_RE_ESP,
    LOG_LINE_RE_SHIM,
    LORA_RX_MSG_PREFIX,
    LORA_TX_MSG_PREFIX,
    MODULE_APRS_IS,
    MODULE_BEACON,
    MODULE_DIGI,
    MODULE_LORA_RX,
    MODULE_LORA_TX,
    TNC2_RE,
)
from serial_link import LogEvent

# Compact "key=value" tokens with no surrounding whitespace, e.g.
# "gps.lat=37.123456 gps.lon=-122.123456" (possibly several per line) or a
# lone "wifiSTA.connected=true". Distinct from the spaced "key = value"
# format used inside 'show' section blocks (see KV_RE in constants.py).
KV_INLINE_RE = re.compile(r"(?P<key>[A-Za-z][\w.]*)=(?P<value>\S+)")


@dataclass(frozen=True)
class LogLine:
    level: str
    module: str
    message: str


@dataclass(frozen=True)
class TNC2Packet:
    sender: str
    tocall: str
    path: str
    payload: str
    raw: str


def parse_log_line(raw: str) -> Optional[LogLine]:
    text = ANSI_ESCAPE_RE.sub("", raw)
    m = LOG_LINE_RE_SHIM.match(text) or LOG_LINE_RE_ESP.match(text)
    if not m:
        return None
    level = LOG_LEVEL_NORMALIZE.get(m.group("level"), m.group("level"))
    return LogLine(level=level, module=m.group("module"), message=m.group("message"))


def strip_prefix(text: str, prefix: str) -> str:
    text = text.strip()
    if text.startswith(prefix):
        text = text[len(prefix) :].strip()
    return text


def parse_tnc2(text: str) -> Optional[TNC2Packet]:
    """Parse 'SENDER>TOCALL[,PATH...]:PAYLOAD'."""
    m = TNC2_RE.match(text.strip())
    if not m:
        return None
    rest = m.group("rest")
    if "," in rest:
        tocall, path = rest.split(",", 1)
    else:
        tocall, path = rest, ""
    return TNC2Packet(
        sender=m.group("sender"),
        tocall=tocall,
        path=path,
        payload=m.group("payload"),
        raw=text.strip(),
    )


def parse_kv_block(lines: list[str]) -> dict[str, str]:
    """Merge every 'key=value' (status-line, possibly several per line) and
    'key = value' (show-block, one per line) token found across a list of
    response lines into one dict. Last match wins on duplicate keys, which
    is fine since preflight fields are unique within a single command's
    response.

    Inline tokens are checked first: KV_RE's spaced pattern allows zero
    whitespace around '=' and is anchored to the whole line, so on a
    multi-token line like "gps.lat=1 gps.lon=2" it would otherwise swallow
    everything after the first '=' into one value instead of finding both
    pairs.
    """
    result: dict[str, str] = {}
    for line in lines:
        inline_matches = list(KV_INLINE_RE.finditer(line))
        if inline_matches:
            for im in inline_matches:
                result[im.group("key")] = im.group("value")
            continue
        m = KV_RE.match(line)
        if m:
            result[m.group("key").strip()] = m.group("value").strip()
    return result


# ---------------- EventBus predicates ----------------


def predicate_and(*preds: Callable[[LogEvent], bool]) -> Callable[[LogEvent], bool]:
    return lambda ev: all(p(ev) for p in preds)


def predicate_or(*preds: Callable[[LogEvent], bool]) -> Callable[[LogEvent], bool]:
    return lambda ev: any(p(ev) for p in preds)


def _module_message_packet(ev: LogEvent, module: str, msg_prefix: str) -> Optional[TNC2Packet]:
    ll = parse_log_line(ev.raw)
    if ll is None or ll.module != module:
        return None
    text = strip_prefix(ll.message, msg_prefix)
    return parse_tnc2(text)


def is_lora_rx_from(
    call: str, path_contains: Optional[str] = None, direct_only: bool = False
) -> Callable[[LogEvent], bool]:
    """direct_only=True additionally requires the path to contain no '*'
    (station_utils.cpp's own definition of "direct", used by
    wasHeardDirect()). Needed whenever a digipeater is present and multiple
    beacons get triggered in sequence within one run: the iGate hears BOTH
    the direct copy and a digipeated copy of *every* beacon, and the
    digipeated copy arrives with an unpredictable delay (digi RX + a 200ms
    collision-avoidance wait + its own TX airtime, digi_utils.cpp) -- long
    enough that a *later* phase's since=... window can end up matching an
    *earlier* phase's still-in-flight digipeated copy instead of its own
    trigger. Found via phase5_status_beacon's second sub-case intermittently
    matching phase5's own first sub-case's delayed relay."""

    def pred(ev: LogEvent) -> bool:
        pkt = _module_message_packet(ev, MODULE_LORA_RX, LORA_RX_MSG_PREFIX)
        if pkt is None or pkt.sender != call:
            return False
        if path_contains is not None and path_contains not in pkt.path:
            return False
        if direct_only and "*" in pkt.path:
            return False
        return True

    return pred


def is_lora_tx_from(call: str) -> Callable[[LogEvent], bool]:
    def pred(ev: LogEvent) -> bool:
        pkt = _module_message_packet(ev, MODULE_LORA_TX, LORA_TX_MSG_PREFIX)
        return pkt is not None and pkt.sender == call

    return pred


def is_digi_repeating_from(original_sender: str) -> Callable[[LogEvent], bool]:
    """Matches a digipeater's 'Digi: Repeating:' line for a packet
    originally sent by original_sender -- the digipeated packet's sender
    field is unchanged (only the path is rewritten, WIDE1-1 -> mycall+'*'),
    so this checks the same TNC2 sender field as the original."""

    def pred(ev: LogEvent) -> bool:
        pkt = _module_message_packet(ev, MODULE_DIGI, DIGI_REPEATING_PREFIX)
        return pkt is not None and pkt.sender == original_sender

    return pred


def extract_digi_relay_packet(ev: LogEvent) -> Optional[TNC2Packet]:
    return _module_message_packet(ev, MODULE_DIGI, DIGI_REPEATING_PREFIX)


def is_beacon_tx_from(call: str) -> Callable[[LogEvent], bool]:
    def pred(ev: LogEvent) -> bool:
        ll = parse_log_line(ev.raw)
        if ll is None or ll.module != MODULE_BEACON:
            return False
        text = strip_prefix(ll.message, "TX:")
        pkt = parse_tnc2(text)
        return pkt is not None and pkt.sender == call

    return pred


def is_aprsis_uploaded_from(call: str) -> Callable[[LogEvent], bool]:
    def pred(ev: LogEvent) -> bool:
        pkt = _module_message_packet(ev, MODULE_APRS_IS, APRS_IS_UPLOADED_PREFIX)
        return pkt is not None and pkt.sender == call

    return pred


def is_aprsis_rx() -> Callable[[LogEvent], bool]:
    def pred(ev: LogEvent) -> bool:
        ll = parse_log_line(ev.raw)
        return ll is not None and ll.module == MODULE_APRS_IS and ll.message.startswith(APRS_IS_RX_PREFIX)

    return pred


def is_aprsis_connect_result() -> Callable[[LogEvent], bool]:
    def pred(ev: LogEvent) -> bool:
        ll = parse_log_line(ev.raw)
        return (
            ll is not None
            and ll.module == MODULE_APRS_IS
            and ll.message.startswith(APRS_IS_CONNECTED_PREFIX)
        )

    return pred


def extract_uploaded_packet(ev: LogEvent) -> Optional[TNC2Packet]:
    return _module_message_packet(ev, MODULE_APRS_IS, APRS_IS_UPLOADED_PREFIX)


def extract_rx_packet(ev: LogEvent) -> Optional[TNC2Packet]:
    return _module_message_packet(ev, MODULE_LORA_RX, LORA_RX_MSG_PREFIX)


def extract_tx_packet(ev: LogEvent) -> Optional[TNC2Packet]:
    return _module_message_packet(ev, MODULE_LORA_TX, LORA_TX_MSG_PREFIX)
