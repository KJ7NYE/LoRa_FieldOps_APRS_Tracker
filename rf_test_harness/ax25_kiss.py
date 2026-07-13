"""
AX.25/KISS frame encoding, ported byte-for-byte from src/kiss_utils.cpp's
encodeKISS()/encodeAddressAX25()/encapsulateKISS() -- this is the exact
encoder the firmware itself uses when it re-emits a TNC2 line to a KISS
client, so a frame built here decodes identically on the device side
(src/kiss_utils.cpp's decodeKISS(), the inverse) as whatever the firmware
would have produced for the same TNC2 text.

Encode-only: the harness never needs to decode KISS, since RX observation
happens on the existing LOG-mode serial channels, not this transport.

Two independent injection points use this:
  - tcp_kiss_client.py, into the iGate's TCP KISS server (port 8001) -- the
    iGate itself transmits whatever frame it decodes.
  - DeviceSession.send_kiss_frame(), direct to a tracker/digi's own USB
    serial port while in KISS mode -- that device transmits instead.
"""

from __future__ import annotations

import re

FEND = 0xC0
FESC = 0xDB
TFEND = 0xDC
TFESC = 0xDD
KISS_CMD_DATA = 0x00
AX25_CONTROL_FIELD = 0x03
AX25_INFO_FIELD = 0xF0
HAS_BEEN_DIGIPITED_MASK = 0b10000000
IS_LAST_ADDRESS_POSITION_MASK = 0b1

_LEADING_DIGITS_RE = re.compile(r"\d*")


class InvalidTNC2Frame(ValueError):
    pass


def _validate_tnc2_frame(frame: str) -> bool:
    colon_pos = frame.find(":")
    gt_pos = frame.find(">")
    return colon_pos != -1 and gt_pos != -1 and colon_pos > gt_pos


def _arduino_to_int(s: str) -> int:
    """Mirror Arduino String::toInt(): parse leading digits, stop at the
    first non-digit (e.g. a trailing '*' on "CALL-1*"'s SSID portion "1*"),
    return 0 if nothing parseable."""
    m = _LEADING_DIGITS_RE.match(s)
    return int(m.group()) if m and m.group() else 0


def encode_address_ax25(address: str) -> bytes:
    """One 7-byte AX.25 address field: 6 shifted-left-1 callsign bytes
    (space-padded) + 1 SSID byte (kiss_utils.cpp:95-112)."""
    has_been_digipited = "*" in address
    if "-" not in address:
        if has_been_digipited:
            address = address[:-1]  # strip trailing '*' before appending "-0"
        address += "-0"

    separator_index = address.index("-")
    ssid = _arduino_to_int(address[separator_index + 1 :])

    kiss_address = bytearray()
    for i in range(6):
        address_char = " "
        if len(address) > i and i < separator_index:
            address_char = address[i]
        kiss_address.append((ord(address_char) << 1) & 0xFF)
    kiss_address.append(((ssid << 1) | 0b01100000 | (HAS_BEEN_DIGIPITED_MASK if has_been_digipited else 0)) & 0xFF)
    return bytes(kiss_address)


def encapsulate_kiss(ax25_frame: bytes, command: int = KISS_CMD_DATA) -> bytes:
    """FEND + command-nibble + escaped payload + FEND (kiss_utils.cpp:74-93)."""
    out = bytearray()
    out.append(FEND)
    out.append(0x0F & command)
    for b in ax25_frame:
        if b == FEND:
            out.append(FESC)
            out.append(TFEND)
        elif b == FESC:
            out.append(FESC)
            out.append(TFESC)
        else:
            out.append(b)
    out.append(FEND)
    return bytes(out)


def encode_kiss_frame(tnc2_line: str) -> bytes:
    """'SENDER>DEST[,PATH...]:PAYLOAD' -> a complete KISS-framed AX.25 data
    frame, ready to write directly to a serial port or TCP socket the
    firmware is listening on (kiss_utils.cpp:141-171, encodeKISS()).

    Address field order in the output is destination-then-source-then-path,
    matching real AX.25 -- the algorithm below finds the destination
    wherever it appears in the TNC2 string (right after the first '>') and
    prepends it, since TNC2 text writes source first.
    """
    if not _validate_tnc2_frame(tnc2_line):
        raise InvalidTNC2Frame(f"not a valid TNC2 frame: {tnc2_line!r}")

    colon_index = tnc2_line.index(":")
    ax25_frame = bytearray()
    address = ""
    destination_address_written = False

    for i in range(colon_index + 1):
        current_char = tnc2_line[i]
        if current_char in (":", ">", ","):
            if not destination_address_written and current_char in (",", ":"):
                ax25_frame = bytearray(encode_address_ax25(address)) + ax25_frame
                destination_address_written = True
            else:
                ax25_frame += encode_address_ax25(address)
            address = ""
        else:
            address += current_char

    last_byte = ax25_frame[-1]
    ax25_frame[-1] = last_byte | IS_LAST_ADDRESS_POSITION_MASK
    ax25_frame.append(AX25_CONTROL_FIELD)
    ax25_frame.append(AX25_INFO_FIELD)
    ax25_frame += tnc2_line[colon_index + 1 :].encode("ascii", errors="replace")

    return encapsulate_kiss(bytes(ax25_frame), KISS_CMD_DATA)


if __name__ == "__main__":
    # Self-check: round-trip a couple of known-shape frames and print the
    # encoded bytes for manual inspection -- no live device needed to
    # validate the pure encoding logic itself.
    for sample in [
        "TESTSTA>APRS,WIDE1-1::KG7KMV-5 :?PING?{01",
        "TESTFK1>APLRG1:!3712.34N/12212.34W>test",
        "K7SWI*>APRS:}TESTSTA>APRS,TCPIP,K7SWI*::KG7KMV-5 :hello{01",
    ]:
        encoded = encode_kiss_frame(sample)
        print(f"{sample!r}\n  -> {encoded!r}\n")
