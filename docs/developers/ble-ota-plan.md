# BLE firmware update from the FieldOps app: plan and status

Working note for the BLE updater. Branch: `claude/ble-firmware-updates-w5sljy`.
Last updated 2026-11-22.

## Status at a glance

| Phase | What | Status |
|---|---|---|
| 1 | Shared ESP32 OTA writer (`OTA_Utils`), web upload moved onto it | **Hardware-tested** (Heltec V3) |
| 2 | BLE OTA GATT service on ESP32 (NimBLE) | **Hardware-tested** (Heltec V3, Windows bleak client) |
| 3 | T114: Nordic buttonless DFU behind the PIN | **Hardware-tested** (T114 + Android nRF Connect); app must follow the DFU rules below |
| 4 | Release manifest with per-board SHA-256 | Done (`firmware_manifest.json`, schema in `docs/developers/firmware-manifest.md`); first real output on the next release |
| 5 | FieldOps Android app updater (separate repo) | In progress (app repo; handoff in `docs/firmware-update-handoff.md` there) |
| 6 | Docs / `serial_config.html` parity | Docs for 1–3 done; Web Bluetooth updater undecided |

Decisions made: BLE updates require *Require PIN* = on (same rule as the BLE
setup CLI). The T114 DFU service stays (put behind the PIN, not removed): the
T114 is the most common board and users asked for a BLE updater. The T114
bootloader is **not** updated (no published T114 build with the fix below, and
a failed bootloader flash needs SWD to recover); the app works with the
bootloader already in the field.

## Hardware test results (2026-11-22)

**Heltec V3 (`heltec_v3_433_aprs`), Phase 1 + 2, all passed:**

- GATT: update service registered last; KISS and config handles unchanged.
  PIN on → KISS + config + update; PIN off → KISS only (checked with an
  uncached discovery). Unpaired write → Insufficient Authentication.
- Full update → DONE → reboot into the other slot, 4 times (including
  installing this branch's own fixes over BLE).
- Bad SHA-256 → ERROR 8; wrong board → ERROR 3; oversized → ERROR 4;
  ABORT → ABORTED; stall → ERROR 9 after 10 s; link loss → clean abort. Old
  firmware kept every time; the main loop and the iGate resume afterwards.
- Web upload during a BLE update → refused, BLE update still completes. BLE
  START during a web upload → ERROR 5, web upload completes. Web: non-firmware
  file and USB-flasher image names rejected without a reboot; claim released
  after each failure.
- **Bug found and fixed:** a BLE START refused before any session had
  allocated the receive ring (ERROR 5 right after boot) panicked the tracker
  (`ByteRing::drop()` divide by zero). Fixed in `byte_ring.h`.
- **Throughput:** 6.3 KB/s with the central's default link → 18.7 KB/s after
  the tracker requests a 7.5–15 ms interval, 251-byte LL packets and 2M PHY
  during the session (`BLE_Utils::otaSetFastLink`). Full 1.6 MB image: 80–89 s
  from Windows (which sends ~1 packet per connection event; Android should be
  faster). One link drop (supervision timeout) at 90 % in 6 full runs, not
  reproduced.
- **Windows quirk for the client:** `mtu_size` reads 23 immediately after
  connect and 256 a moment later; the client must wait for the MTU exchange
  or it sends 16-byte packets.

**Heltec T114, Phase 3, passed with conditions:**

- DFU service present (last) with the PIN on: the pre-branch firmware never
  registered it, as suspected. Unpaired CCCD subscribe and START_DFU write →
  Insufficient Authentication.
- nRF Connect (Android) update succeeded at **~1.1 KB/s** (~5 min for the
  312 KB image) once two conditions were met; see "T114 DFU rules" below.
- T114 bootloader: `UF2 Bootloader 0.9.0-2-g836c8dc-dirty`, Jul 9 2024,
  S140 6.1.1. It predates Adafruit PR #375 (Jan 2026: RX buffer queue 8 → 16,
  MTU above 23), hence the low speed and the PRN limit.
- Failure modes seen while finding the rules (useful for app error handling):
  a failed transfer leaves the app erased and the T114 in the bootloader
  (USB drive / COM port, no Bluetooth after a few minutes). Recovery: UF2
  drag-and-drop or `pio run -e heltec_t114 -t upload`. RST alone does not
  leave the bootloader once the app is erased.

## T114 DFU rules (requirements for the FieldOps app, Phase 5)

The T114 uses the Adafruit bootloader's legacy Nordic DFU with a bonded
(PIN) link. Two things the Nordic DFU library does not handle by default:

1. **Packet Receipt Notification must be 8 or less** (we tested 6). The 2024
   bootloader has 8 RX buffers; PRN 10 (nRF Connect's default) overruns them
   during flash writes and the bootloader answers Receive Firmware Image with
   status 6 (OPERATION FAILED) ~0.6 s into the transfer. Adafruit README:
   "PRN must be 8 or less". In the Nordic Android library:
   `setPacketsReceiptNotificationsValue(6)`.
2. **Refresh the GATT cache after the jump to the bootloader.** The bootloader
   reconnects at the same address with the shared bond, so Android reuses the
   application's cached GATT table; the library (bonded + Service Changed
   indications enabled) waits for a Service Changed indication instead of
   rediscovering, none takes effect, and the first read fails with
   `GATT INVALID HANDLE` (0x01). Working sequence: start DFU (buttonless jump),
   let it fail or disconnect, reconnect to the bootloader, call the hidden
   `BluetoothGatt.refresh()` (nRF Connect: "Refresh device cache"), then start
   the DFU library again; it reads DFU version 0.8, sees the bootloader and
   uploads. In the app: do the jump itself (write `01 04` to the DFU control
   point, or `otadfu` over the setup CLI), reconnect, `refresh()`, discover,
   then hand the address to the DFU library. Turning Service Changed
   indications off beforehand did not help (Android re-enables them).
   Reference: Meshtastic firmware PR #12220 ("Reset into BLE OTA DFU instead of
   jumping from the DFU callback") touches the same entry path; not reviewed.
3. Keep bond information on (the bootloader needs the bond to accept the
   encrypted reconnect). Expect ~5 min per update; show progress and warn the
   user not to walk away.

## Next steps (pick up here)

1. ~~Phase 4~~: done on branch `feat/firmware-manifest`. Verify the first
   release after merge: `firmware_manifest.json` on main and as a release
   asset, `heltec_t114_dfu.zip` in `flasher/firmware/`.
2. Phase 5: FieldOps app updater. ESP32: `include/ble_ota.h` protocol
   (`tools/ble_ota_client.py` is the reference client). T114: Nordic DFU library
   with the rules above.
3. Not yet tested on hardware: display "Updating nn%" on OLED / Wireless Tracker
   TFT / LoRanger V2 without OLED; single-slot board (`tbeam_433_aprs`) has no
   update service; bonded APRSDroid still works after a T114 update.
4. Open a PR to `main` for this branch.

## What changed on this branch

- `include/ota_utils.h`, `src/ota_utils.cpp` (new, ESP32 only): `begin / write /
  finish / abort`. Inactive-slot selection, image-magic check, single-slot
  refusal, `esp_ota_end` validation, boot-slot switch, running SHA-256 with an
  optional expected digest. `imageSize == 0` keeps the old erase-whole-slot-up-front
  behaviour (web); `imageSize > 0` uses `OTA_WITH_SEQUENTIAL_WRITES` (4 KB erases
  as data arrives; for BLE).
- `src/web_utils.cpp`: `/update` firmware path now calls `OTA_Utils`; filename
  check for USB-flasher images and all logging stay in the web handler.
  `spiffs.bin` path untouched.
- `src/ble_utils.cpp` (nRF52): `bleDfu.begin()` was called **before**
  `Bluefruit.begin()`, where the SoftDevice is not enabled and service
  registration fails, so the buttonless DFU service never existed (confirmed on
  hardware 2026-11-22). It is now registered in `beginDfuService()`,
  after `Bluefruit.begin()` and after the KISS and config services, only when
  the PIN is required, with service permission `SECMODE_ENC_WITH_MITM`
  (Bluefruit raises every characteristic and the CCCD to the service level;
  BLEDfu sets none itself). Same pattern Meshtastic uses.
- `include/version.h`: added `LORANGER_V2` → `BOARD_ENV_ID "LoRanger_V2"`
  (previously reported `unknown`). Found while planning; the app updater needs it.
- Docs: `docs/getting-started/flashing.md` (T114 Bluetooth row),
  `docs/guide/bluetooth.md` (security note), `CHANGELOG.md`.

## Investigation findings

- **T114 (nRF52):** BLE update already possible via the Adafruit bootloader's
  legacy DFU and `<board>_dfu.zip` (built by `flasher.yml`). The app only needs
  the Nordic DFU library (`no.nordicsemi.android:dfu`); entry is either the
  buttonless DFU service (Phase 3) or `otadfu` over the BLE setup CLI. The
  bootloader accepts unsigned images, hence the PIN gate.
- **ESP32:** dual-slot OTA already works for the web UI; BLE only needs a
  transport into `OTA_Utils`.
- **Not updatable over the air at all:** `tbeam_433_aprs`, `lilygo_t3_433_aprs`
  (`huge_app.csv`, single slot; their ~2.2 MB image would not fit a 4 MB
  dual-slot layout).
- Expected throughput on ESP32-S3: MTU 517, 2M PHY, write-without-response,
  roughly 30–90 s for a 1.6 MB image (iOS slower).

## Phase 2 as built (ESP32 BLE OTA service)

The wire protocol is specified in `include/ble_ota.h`; that header is the
reference for the app (Phase 5). Summary and the decisions behind it:

- **GATT:** service `62970001-5b20-49c7-ae44-96d63f6d50e8`, control
  characteristic `…0002` (write + notify) and data characteristic `…0003`
  (write without response, `[u32 offset][payload]`). Both require an
  authenticated (PIN) link. Registered last, only when the PIN is required and
  `OTA_Utils::updateSlotSize() > 0`, so single-slot boards don't advertise a
  feature they can't use.
- **Commands:** START (protocol, size, SHA-256, board id, version), END,
  ABORT. **Notifications:** READY (window, ack interval), ACK(bytes on flash),
  RESYNC(offset), DONE, ERROR(code, text), ABORTED.
- **Flow control is credit-based:** READY advertises a 12 KB window and a 4 KB
  ACK interval; the phone keeps at most `window` bytes un-ACKed. The receive
  ring is 16 KB, so a phone that honours the window can't overflow it; one
  that doesn't gets ERROR 10. (Changed from the plan's "ACK every 16 KB, two
  windows in flight", which needed a 32 KB+ ring.)
- **Threading:** the NimBLE task only checks the packet offset and copies the
  payload into a `ByteRing`; commands go through a one-slot mailbox. The main
  loop (`BLE_OTA::service()`) does all flash work through `OTA_Utils`, using
  the known-size sequential-erase mode, and sends every notification.
- **OTA_Utils is started on the first data**, not at START, because
  `begin()` checks the image magic byte. Board id, size and slot are
  checked at START.
- **Pausing the tracker:** while a session is open, `loop()` runs only the
  watchdog, the LED and `BLE_OTA::service()`, then returns. That pauses
  beacons, LoRa, digi, iGate, every KISS transport, the button and the
  low-battery shutdown, without adding a flag to each subsystem. The display
  shows "Updating nn%" (`displayFirmwareUpdate()`, guarded by
  `displayPresent`).
- **Web and BLE can't collide:** `OTA_Utils` now has an ownership claim
  (`claim()/release()`, CAS on an atomic). The web upload (AsyncTCP task) and
  the BLE updater (main loop) each take it before touching a session; the loser
  gets a "… update is in progress" error. A holder idle for 30 s loses the
  claim, so a browser upload cut off mid-stream can't block BLE updates until
  reboot.
- **End states:** DONE → reboot after 1.5 s (claim held until then).
  ERROR/ABORT/disconnect/10 s idle → session aborted, inactive slot never
  selected, normal operation resumes on the next loop pass.
- **Rollback:** `esp_ota_mark_app_valid_cancel_rollback()` already runs at the
  end of `setup()`; whether the bootloader has rollback enabled is still
  unchecked (it only matters if a new image crashes before `setup()` ends).

### Open items for Phase 2

- **MTU:** NimBLE-Arduino's default preferred MTU is 255 (payload 247 B per
  data packet). Raising it to 517 (`NimBLEDevice::setMTU`) would roughly
  double throughput but also changes how large KISS notifications are split
  for every BLE client, so it needs its own test pass. Decide after measuring.
- **Connection interval:** done on the tracker side (`otaSetFastLink`); the app
  should still request high connection priority during the transfer.
- **`serial_config.html` parity (Phase 6):** a Web Bluetooth client for this
  protocol is feasible (Chrome desktop/Android) but not started.

## Phases 4–6

- **4 (done):** `tools/generate_manifests.py` writes
  `flasher/manifests/firmware_manifest.json` from the published files (sizes and
  SHA-256 computed, keyed by `version.board`, `ble_update` method per board incl.
  the T114 DFU parameters). The release job also hosts `heltec_t114_dfu.zip` in
  `flasher/firmware/` and attaches the manifest to the release. Schema:
  `docs/developers/firmware-manifest.md`.
- **5 (app repo `jeepnjonny/lora-fieldops-config-android`):** read
  `version.board` over the BLE CLI, fetch manifest, download + verify, then
  ESP32 → Phase 2 protocol; nRF52 → Nordic DFU library with the zip. Local file
  picker for offline use.
- **6:** `bluetooth.md`, `flashing.md`, `serial-cli.md`, CHANGELOG; decide on a
  Web Bluetooth updater in `serial_config.html` for parity (CLAUDE.md sync rule).
