# BLE firmware update from the FieldOps app: plan and status

Working note for the BLE updater. Branch: `claude/ble-firmware-updates-w5sljy`.
Last updated 2026-11-22.

## Status at a glance

| Phase | What | Status |
|---|---|---|
| 1 | Shared ESP32 OTA writer (`OTA_Utils`), web upload moved onto it | Compiles; hardware test pending |
| 2 | BLE OTA GATT service on ESP32 (NimBLE) | Not started |
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
4. Commit/PR once 1–3 pass, then start Phase 2.

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

## Phase 2 design (ESP32 BLE OTA service)

- New GATT service with a **control** characteristic (write + notify: START,
  END, ABORT; device replies READY / ACK(offset) / OK / ERR(text)) and a **data**
  characteristic (write-without-response, `[u32 offset][payload]`).
- Both characteristics `WRITE_ENC | WRITE_AUTHEN` (+ `READ_*` for notify);
  registered only when the PIN is required, after existing services. The app
  detects support by the service's presence.
- START carries size, SHA-256, board env id, version. Device checks the board id
  against `BOARD_ENV_ID`, size against the slot, and the image header; calls
  `OTA_Utils::begin(first, len, size)`.
- Flow control: device ACKs every 16 KB; app keeps ≤ 2 windows in flight;
  out-of-order offset → app resends from the last ACK.
- Flash writes happen on the main loop, not in the NimBLE host task: data goes
  through a ring buffer (pattern: `configRx` `ByteRing` in `ble_utils.cpp`).
- END → `OTA_Utils::finish(sha)` → reply OK → reboot after ~1.5 s. Disconnect or
  10 s without data → `OTA_Utils::abort()`; old firmware keeps running.
- Global `firmwareUpdateInProgress` flag pauses beacons/LoRa TX, digi, iGate,
  refuses KISS, shows "Updating x%" (respect `displayPresent` on LoRanger V2 and
  `HAS_DISPLAY` on V1).
- Check whether the bootloader has rollback enabled
  (`esp_ota_mark_app_valid_cancel_rollback()` is already called in `main.cpp`).

## Phases 4–6

- **4:** extend `tools/generate_manifests.py` to publish `firmware_manifest.json`
  (per board: version, file, size, SHA-256, `ble_update` supported).
- **5 (app repo `jeepnjonny/lora-fieldops-config-android`):** read
  `version.board` over the BLE CLI, fetch manifest, download + verify, then
  ESP32 → Phase 2 protocol; nRF52 → Nordic DFU library with the zip. Local file
  picker for offline use.
- **6:** `bluetooth.md`, `flashing.md`, `serial-cli.md`, CHANGELOG; decide on a
  Web Bluetooth updater in `serial_config.html` for parity (CLAUDE.md sync rule).
