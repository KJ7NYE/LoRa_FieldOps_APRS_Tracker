"""
Wire-protocol constants for the RF test harness.

Every literal string here is copied verbatim from src/serial_setup.cpp,
src/lora_utils.cpp, src/station_utils.cpp, and src/aprs_is_utils.cpp as of
the commit this harness was written against. If the firmware's CLI/log
wording changes, update this file first -- everything else consumes these
constants rather than embedding literals of its own.
"""

import re

DEFAULT_BAUD = 115200

# --- Serial mode-switch trigger words (serial_setup.cpp, KISS-mode watcher) ---
SETUP_TRIGGER = "setup"
LOG_TRIGGER = "log"
EXIT_TRIGGER = "exit"
DISCARD_TRIGGER = "discard"

# --- Sentinels used to detect mode transitions ---
# prompt() prints "\n> " with no terminating newline (serial_setup.cpp:59).
# The reader thread strips complete lines first, so by the time a prompt
# arrives the residual buffer is exactly b"> ".
PROMPT_TAIL = b"> "
# The SETUP-mode input loop treats '\r' AND '\n' as independent line
# terminators (serial_setup.cpp:1060-1066: unconditional "\r\n" echo on
# *either* byte), so sending a "cmd\r\n" line causes the device to print its
# prompt TWICE in a row: "...\n> \r\n\n> ". The first, legitimate prompt is
# therefore usually followed by more bytes (not left dangling as the final
# buffer tail), and shows up mid-stream as an ordinary '\n'-terminated "> "
# line instead. The reader treats a drained line matching this marker as a
# prompt too, not just the final dangling-tail case.
PROMPT_LINE_STRIPPED = ">"

SETUP_BANNER_MARKER = ">>> SETUP MODE ACTIVE <<<"          # serial_setup.cpp:346
LOG_MODE_BANNER = "[LOG] Serial log output active."         # serial_setup.cpp:360
# SETUP->KISS prints "Returning to KISS TNC mode." (serial_setup.cpp:370);
# LOG->KISS prints "[LOG] Returning to KISS TNC mode." (serial_setup.cpp:372).
# The former is a substring of the latter, so one check covers both.
RETURN_TO_KISS_MARKER = "Returning to KISS TNC mode."
DIRTY_EXIT_REFUSED_MARKER = "unsaved changes"                # serial_setup.cpp:367 ("ERR: unsaved changes -- ...")

OK_PREFIX = "OK: "
ERR_PREFIX = "ERR: "
# tx comment/status acks bypass ok()/err() and print a bare literal (serial_setup.cpp:966,969)
TX_COMMENT_ACK = "OK tx comment beacon sent"
TX_STATUS_ACK = "OK tx status beacon sent"

# --- Logger line format ---
# Two shapes exist in practice, confirmed against real hardware output --
# the nRF52 shim (include/nrf52_shims/logger.h:35-45) is NOT a faithful
# match for the real upstream esp-logger library used on ESP32 targets:
#   nRF52 shim:  "[LVL] Module: message"            e.g. "[INF] LoRa Rx: ---> ..."
#   ESP32 (real esp-logger): "[LEVEL][Module] message", ANSI-colored,
#                            e.g. "\x1b[0;32m[INFO][LoRa Rx] ---> ...\x1b[0m"
# Strip ANSI escapes before matching either pattern.
ANSI_ESCAPE_RE = re.compile(r"\x1b\[[0-9;]*m")
LOG_LINE_RE_SHIM = re.compile(r"^\[(?P<level>ERR|WRN|INF|DBG|TRC)\]\s+(?P<module>[^:]+):\s?(?P<message>.*)$")
LOG_LINE_RE_ESP = re.compile(
    r"^\[(?P<level>ERROR|WARN|INFO|DEBUG|TRACE)\]\[(?P<module>[^\]]+)\]\s?(?P<message>.*)$"
)
LOG_LEVEL_NORMALIZE = {
    "ERROR": "ERR", "WARN": "WRN", "INFO": "INF", "DEBUG": "DBG", "TRACE": "TRC",
    "ERR": "ERR", "WRN": "WRN", "INF": "INF", "DBG": "DBG", "TRC": "TRC",
}

# --- SETUP "show"/status output: "  key = value" (serial_setup.cpp kv() helper) ---
# Note: a handful of kv() keys contain a hyphen (e.g. "mic-e"); this pattern
# won't match those, which is fine since the harness never reads that field.
KV_RE = re.compile(r"^\s*(?P<key>[A-Za-z][\w.]*)\s*=\s*(?P<value>\S.*?)\s*$")

# --- Known module tags + message prefixes (verified against source) ---
MODULE_LORA_TX = "LoRa Tx"          # "[INF] LoRa Tx: ---> <packet>"      (lora_utils.cpp:207)
MODULE_LORA_RX = "LoRa Rx"          # "[INF] LoRa Rx: ---> <packet>"      (lora_utils.cpp:297)
MODULE_BEACON = "Beacon"            # "[INF] Beacon: TX: <packet>"        (station_utils.cpp:359)
MODULE_APRS_IS = "APRS-IS"          # "[DBG] APRS-IS: Uploaded: <line>"   (aprs_is_utils.cpp:246)
MODULE_DIGI = "Digi"                # "[INF] Digi: Repeating: <packet>"   (digi_utils.cpp:110)
MODULE_PHG = "PHG"                  # "[INF] PHG: TX: <packet>"           (station_utils.cpp:458)

LORA_RX_MSG_PREFIX = "--->"
LORA_TX_MSG_PREFIX = "--->"
APRS_IS_UPLOADED_PREFIX = "Uploaded:"
APRS_IS_RX_PREFIX = "Rx:"
APRS_IS_CONNECTED_PREFIX = "Connected. Passcode"
DIGI_REPEATING_PREFIX = "Repeating:"
STATUS_TX_PREFIX = "Status TX:"     # station_utils.cpp:395 (module is still MODULE_BEACON)
PHG_TX_PREFIX = "TX:"               # station_utils.cpp:458 (module is MODULE_PHG)
# APRS-IS echo-rejection skip line (aprs_is_utils.cpp:217-218) -- distinct
# from the upload dedup skip line, module is still MODULE_APRS_IS.
APRS_IS_ECHO_SKIP_PREFIX = "Skip IS->RF echo (rebroadcast):"

# --- TNC2 packet line: SENDER>TOCALL[,PATH...]:PAYLOAD ---
TNC2_RE = re.compile(r"^(?P<sender>[^>,:]+)>(?P<rest>[^:]+):(?P<payload>.*)$")

TRACKER_TOCALL = "APLRT1"

# DTIs a tracker beacon may legitimately arrive as: plain position ('!'/'='),
# position-with-timestamp ('@'), or an object report (';') -- the latter is
# what station_utils.cpp sends whenever a tacticalCallsign is configured
# (see generateObjectPacket(), lib/APRSPacketLib), which is common in this
# fork. Not an exhaustive APRS DTI list -- just what a tracker in this
# firmware can produce for a position-bearing beacon.
VALID_POSITION_DTIS = ("!", "=", "@", ";")

# Status packet DTI: generateStatusPacket() emits "<base>:>" + status text
# (lib/APRSPacketLib/src/APRSPacketLib.cpp:53-55) -- after parse_tnc2() splits
# on the first colon, payload starts with this.
STATUS_DTI = ">"

# PHG beacons use the uncompressed format (DTI '=', already in
# VALID_POSITION_DTIS) with a "PHGxxxx" data extension appended to the
# comment (station_utils.cpp:433-441, four digits: power/height/gain/dir).
PHG_EXTENSION_RE = re.compile(r"PHG\d{4}")

# --- APRS-IS read-only external tap ---
DEFAULT_APRS_IS_TAP_HOST = "rotate.aprs2.net"
DEFAULT_APRS_IS_TAP_PORT = 14580
APRS_IS_READONLY_PASSCODE = "-1"

# --- iGate upload dedup (include/dedup_utils.h:11-14) ---
DEDUP_SLOTS = 50
DEDUP_TTL_SECONDS = 60

# --- Default timeouts (seconds) ---
DEFAULT_MODE_SWITCH_TIMEOUT = 3.0
DEFAULT_SETUP_CMD_TIMEOUT = 3.0
DEFAULT_PHASE1_TIMEOUT = 10.0
DEFAULT_PHASE2_TIMEOUT = 5.0
DEFAULT_PHASE3_TIMEOUT = 25.0
DEFAULT_RUN_SPACING = 65.0  # > DEDUP_TTL_SECONDS, see README "Repeatability"
