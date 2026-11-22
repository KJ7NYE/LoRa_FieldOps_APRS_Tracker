# BLE firmware update from the FieldOps app: plan and status

Working note for the BLE updater. Branch: `claude/ble-firmware-updates-w5sljy`.
Last updated 2026-11-22.

## Status at a glance

| Phase | What | Status |
|---|---|---|
| 1 | Shared ESP32 OTA writer (`OTA_Utils`), web upload moved onto it | Compiles; hardware test pending |
| 2 | BLE OTA GATT service on ESP32 (NimBLE) | Done in code; compile check below; hardware test pending |
| 3 | T114: Nordic buttonless DFU behind the PIN | Compiles; hardware test pending |
| 4 | Release manifest with per-board SHA-256 | Not started |
| 5 | FieldOps Android app updater (separate repo) | Not started |
| 6 | Docs / `serial_config.html` parity | Docs for 1 + 3 done |

Decisions made: BLE updates require *Require PIN* = on (same rule as the BLE
setup CLI). The T114 DFU service stays (put behind the PIN, not removed): the
T114 is the most common board and users asked for a BLE updater.

## Next steps (pick up here)

1. ~~Compile-verify.~~ Done 2026-11-22: `heltec_t114`, `heltec_v3_433_aprs`,
   `tbeam_433_aprs` and `LoRanger_V2` build with no new warnings. Fixed on the
   way: the WIP commit also called the nRF52-only `beginDfuService()` from the
   ESP32 (NimBLE) setup path; removed.
2. **Hardware regression check of the existing update paths** (should all be
   unchanged):
   - Web UI → Update Firmware with `<board>_firmware.bin` on an 8 MB board
     (alternates app0/app1, reboots, runs new version).
   - Web UI upload of a `_web_factory.bin` / `_ota_update.bin` → rejected with
     the "USB flasher image" message.
   - Web UI upload of a non-firmware file → "The file is not an ESP32 firmware
     image." (message text changed slightly: no longer prefixed with the filename).
   - Web UI on `tbeam_433_aprs` / `lilygo_t3_433_aprs` → "only one firmware slot".
   - `spiffs.bin` upload (untouched code path).
   - USB web flasher factory and OTA images (firmware side not involved).
   - T114: web flasher (Web Serial DFU), UF2 drag-and-drop, `otadfu` over USB.
3. **T114 hardware test of Phase 3:**
   - PIN on: nRF Connect → connect → pair with PIN → DFU tab → `heltec_t114_dfu.zip`
     should switch to the bootloader and flash without `otadfu`.
   - PIN on, unpaired phone: writing the DFU control point must be refused
     (insufficient authentication).
   - PIN off: no DFU service in the GATT table.
   - Bonded APRSDroid / FieldOps app still work after the update (KISS and
     config handles must not move; DFU service is registered last).
   - Boot log should not show `Could not register the BLE DFU service`.
4. **ESP32 hardware test of Phase 2** with `tools/ble_ota_client.py` (bleak;
   pair the tracker in the OS Bluetooth settings first; `--corrupt-sha`,
   `--stall-at N` and `--abort-at N` cover the failure cases). On an 8 MB
   board with *Require PIN* on:
   - Full update: START → READY, stream, END → DONE, reboot into the new
     version; `/version.json` and `version` report it.
   - Wrong board id → ERROR 3; oversized image → ERROR 4; wrong SHA-256 →
     ERROR 8 and the old firmware keeps running.
   - Disconnect mid-transfer → old firmware keeps running, beacons resume.
   - 10 s pause mid-transfer → ERROR 9.
   - Web UI upload during a BLE update → "A Bluetooth firmware update is in
     progress"; BLE START during a web upload → ERROR 5.
   - PIN off → no update service in the GATT table. Single-slot board
     (`tbeam_433_aprs`) → no update service.
   - Display shows "Updating nn%" (OLED, Wireless Tracker TFT; LoRanger V2
     with and without the OLED fitted).
   - Measure the transfer time at MTU 247 (see "Open items").
5. Commit/PR once 1–4 pass.

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
  registration fails, so the buttonless DFU service most likely never existed
  (not confirmed on hardware). It is now registered in `beginDfuService()`,
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
- **Connection interval:** the app should request high connection priority
  (Android `requestConnectionPriority(HIGH)`) and 2M PHY during the transfer.
- **`serial_config.html` parity (Phase 6):** a Web Bluetooth client for this
  protocol is feasible (Chrome desktop/Android) but not started.

## Phases 4–6

- **4:** extend `tools/generate_manifests.py` to publish `firmware_manifest.json`
  (per board: version, file, size, SHA-256, `ble_update` supported).
- **5 (app repo `jeepnjonny/lora-fieldops-config-android`):** read
  `version.board` over the BLE CLI, fetch manifest, download + verify, then
  ESP32 → Phase 2 protocol; nRF52 → Nordic DFU library with the zip. Local file
  picker for offline use.
- **6:** `bluetooth.md`, `flashing.md`, `serial-cli.md`, CHANGELOG; decide on a
  Web Bluetooth updater in `serial_config.html` for parity (CLAUDE.md sync rule).
