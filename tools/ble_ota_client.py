#!/usr/bin/env python3
"""Reference client for the ESP32 BLE firmware update (include/ble_ota.h).

Bench tool for testing the firmware side before (and alongside) the FieldOps
app, and a readable reference for the app's implementation of the protocol.

Requirements:
    pip install bleak

The tracker must have *Require PIN* on. Pair it with the PIN in the
operating system's Bluetooth settings first (Windows: Settings > Bluetooth >
Add device); this script uses the existing pairing and does not enter the PIN.

Usage:
    python tools/ble_ota_client.py --name MyTracker .pio/build/heltec_v3_433_aprs/firmware.bin --board heltec_v3_433_aprs
    python tools/ble_ota_client.py --address AA:BB:CC:DD:EE:FF heltec_v3_433_aprs_firmware.bin

The board id defaults to the file name's "<board>_firmware.bin" prefix.

Test switches (for the hardware checklist in docs/developers/ble-ota-plan.md):
    --corrupt-sha    send a wrong SHA-256 (expect ERROR 8, old firmware kept)
    --stall-at N     stop sending after N bytes (expect ERROR 9 after 10 s)
    --abort-at N     send ABORT after N bytes (expect ABORTED)
"""

import argparse
import asyncio
import hashlib
import os
import struct
import sys
import time

from bleak import BleakClient, BleakScanner

SERVICE_UUID = "62970001-5b20-49c7-ae44-96d63f6d50e8"
CONTROL_UUID = "62970002-5b20-49c7-ae44-96d63f6d50e8"
DATA_UUID = "62970003-5b20-49c7-ae44-96d63f6d50e8"

PROTOCOL_VERSION = 1
CMD_START, CMD_END, CMD_ABORT = 0x01, 0x02, 0x03
NOTIFY_READY, NOTIFY_ACK, NOTIFY_RESYNC = 0x81, 0x82, 0x83
NOTIFY_DONE, NOTIFY_ERROR, NOTIFY_ABORTED = 0x84, 0x85, 0x86

DATA_HEADER_BYTES = 4  # u32 offset


class UpdateFailed(Exception):
    pass


class Session:
    """Tracks the tracker's notifications for one update."""

    def __init__(self):
        self.ready = asyncio.Event()
        self.finished = asyncio.Event()
        self.progress = asyncio.Event()  # set on every ACK / RESYNC
        self.window = 0
        self.ack_interval = 0
        self.acked = 0
        self.resync_to = None
        self.outcome = None  # "done" | "aborted" | UpdateFailed

    def on_notify(self, _characteristic, data: bytearray):
        code = data[0]
        if code == NOTIFY_READY:
            _, self.window, self.ack_interval = struct.unpack_from("<BII", data, 1)
            self.ready.set()
        elif code == NOTIFY_ACK:
            (self.acked,) = struct.unpack_from("<I", data, 1)
            self.progress.set()
        elif code == NOTIFY_RESYNC:
            (self.resync_to,) = struct.unpack_from("<I", data, 1)
            self.progress.set()
        elif code == NOTIFY_DONE:
            self.outcome = "done"
            self.finished.set()
        elif code == NOTIFY_ABORTED:
            self.outcome = "aborted"
            self.finished.set()
        elif code == NOTIFY_ERROR:
            text = bytes(data[2:]).decode("utf-8", "replace")
            self.outcome = UpdateFailed(f"ERROR {data[1]}: {text}")
            self.finished.set()
            self.ready.set()
            self.progress.set()


def build_start(size: int, sha256: bytes, board: str, version: str) -> bytes:
    board_bytes = board.encode()[:255]
    version_bytes = version.encode()[:32]
    return (struct.pack("<BBI", CMD_START, PROTOCOL_VERSION, size) + sha256
            + bytes([len(board_bytes)]) + board_bytes
            + bytes([len(version_bytes)]) + version_bytes)


async def find_device(args):
    if args.address:
        return args.address
    print(f"Scanning for '{args.name}'...")
    device = await BleakScanner.find_device_by_name(args.name, timeout=15)
    if device is None:
        sys.exit(f"Tracker '{args.name}' not found")
    return device


async def run(args):
    image = open(args.firmware, "rb").read()
    sha256 = hashlib.sha256(image).digest()
    if args.corrupt_sha:
        sha256 = bytes([sha256[0] ^ 0xFF]) + sha256[1:]
    board = args.board or os.path.basename(args.firmware).split("_firmware")[0]
    print(f"Image: {len(image)} bytes, board '{board}', SHA-256 {hashlib.sha256(image).hexdigest()[:16]}...")

    async with BleakClient(await find_device(args), timeout=20) as client:
        if client.services.get_service(SERVICE_UUID) is None:
            sys.exit("No update service: is Require PIN on, and does this board have two firmware slots?")
        payload_size = client.mtu_size - 3 - DATA_HEADER_BYTES
        print(f"Connected, MTU {client.mtu_size}, {payload_size} bytes per packet")

        session = Session()
        await client.start_notify(CONTROL_UUID, session.on_notify)
        await client.write_gatt_char(CONTROL_UUID, build_start(len(image), sha256, board, args.version), response=True)
        await asyncio.wait_for(session.ready.wait(), 10)
        if isinstance(session.outcome, UpdateFailed):
            raise session.outcome
        print(f"READY: window {session.window} B, ACK every {session.ack_interval} B")

        started = time.monotonic()
        offset = 0
        while offset < len(image):
            if isinstance(session.outcome, UpdateFailed):
                raise session.outcome
            if session.resync_to is not None:
                print(f"\nRESYNC to {session.resync_to}")
                offset, session.resync_to = session.resync_to, None
            if args.abort_at is not None and offset >= args.abort_at:
                await client.write_gatt_char(CONTROL_UUID, bytes([CMD_ABORT]), response=True)
                await asyncio.wait_for(session.finished.wait(), 10)
                print(f"\nTracker replied: {session.outcome}")
                return
            if args.stall_at is not None and offset >= args.stall_at:
                print(f"\nStalling at {offset} bytes; the tracker should time out...")
                await asyncio.wait_for(session.finished.wait(), 30)
                raise session.outcome if isinstance(session.outcome, UpdateFailed) else UpdateFailed("no timeout")
            # Credit-based flow control: never more than `window` bytes un-ACKed.
            if offset - session.acked + payload_size > session.window:
                session.progress.clear()
                await asyncio.wait_for(session.progress.wait(), 15)
                continue
            chunk = image[offset:offset + payload_size]
            await client.write_gatt_char(DATA_UUID, struct.pack("<I", offset) + chunk, response=False)
            offset += len(chunk)
            elapsed = max(time.monotonic() - started, 0.001)
            print(f"\r{offset * 100 // len(image):3d}%  {offset // 1024} KB  {offset / elapsed / 1024:.1f} KB/s",
                  end="", flush=True)

        while session.acked < len(image):
            if isinstance(session.outcome, UpdateFailed):
                raise session.outcome
            session.progress.clear()
            await asyncio.wait_for(session.progress.wait(), 15)

        await client.write_gatt_char(CONTROL_UUID, bytes([CMD_END]), response=True)
        await asyncio.wait_for(session.finished.wait(), 30)
        if isinstance(session.outcome, UpdateFailed):
            raise session.outcome
        print(f"\n{session.outcome.upper()} in {time.monotonic() - started:.1f} s; the tracker is rebooting.")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("--name", help="BLE device name (Bluetooth > Device name)")
    target.add_argument("--address", help="BLE address")
    parser.add_argument("firmware", help="<board>_firmware.bin (not the web_factory / ota_update images)")
    parser.add_argument("--board", help="board id (default: from the file name)")
    parser.add_argument("--version", default="bench", help="version string sent in START (informational)")
    parser.add_argument("--corrupt-sha", action="store_true")
    parser.add_argument("--stall-at", type=int)
    parser.add_argument("--abort-at", type=int)
    args = parser.parse_args()
    try:
        asyncio.run(run(args))
    except UpdateFailed as error:
        sys.exit(f"\nUpdate failed: {error}")


if __name__ == "__main__":
    main()
