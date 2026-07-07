# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a multi-platform APRS tracker/iGate/digipeater firmware for LoRa radios, forked from CA2RXU's `richonguzman/LoRa_APRS_Tracker`. It adds nRF52840 support, KJ7NYE LoRanger hardware support, and US-compliance tuning. Built with PlatformIO on the Arduino framework.

## Git Remotes

This repo has two remotes: `origin` (`KJ7NYE/LoRa_FieldOps_APRS_Tracker`, this fork) and `upstream` (`richonguzman/LoRa_APRS_Tracker`, the original project). **Never push, open PRs, or comment against `upstream`.** This fork has diverged significantly (nRF52840 support, single-beacon profile, hardware/config changes) and is never intended to be merged back upstream. All branches, pushes, and PRs go to `origin` only. `upstream` exists solely so it can be fetched from for reference (e.g. comparing against upstream behavior when debugging a port issue) — never write to it.

When using `gh pr create` or similar, always pass `--repo KJ7NYE/LoRa_FieldOps_APRS_Tracker` explicitly rather than relying on auto-detection, since this repo is a GitHub fork and `gh` may default to the upstream parent as the PR base.

## Build Commands

```sh
pio run                              # Build all default environments
pio run -e <env>                     # Build specific environment
pio run -e <env> -t upload           # Flash firmware + filesystem
pio run -e <env> -t uploadfs         # Upload filesystem only
pio run -e <env> -t buildfs          # Build filesystem image without uploading
```

There are no automated tests — the CI workflow (`.github/workflows/commit.yml`) only compiles all environments.

## Hardware Targets / PlatformIO Environments

| Environment | MCU | LoRa | GPS | Display | WiFi/BLE |
|---|---|---|---|---|---|
| `heltec_t114` | nRF52840 | SX1262 | L76K (onboard) | ST7789 1.14" TFT | BLE 5 (native) |
| `heltec_v3_433_aprs` | ESP32-S3 | SX1262 | None | SSD1306 OLED | WiFi + NimBLE |
| `tbeam_433_aprs` | ESP32 | SX1278 | u-blox NEO-6M/M8N | SSD1306 OLED | WiFi + NimBLE + BT Classic |
| `tbeam_433_1w_aprs` | ESP32-S3 | SX1262 (1 W) | onboard GNSS | SH1106 OLED | WiFi + NimBLE |
| `lilygo_t3_433_aprs` | ESP32 | SX1278 | None | SSD1306 OLED | WiFi + NimBLE |
| `LoRanger_V1` | ESP32-S3 | SX1262 (E22-400M30S) | ATGM336H (onboard) | None (headless) | WiFi + NimBLE |

Capability flags (defined per environment in `common_settings.ini`): `HAS_WIFI`, `HAS_NIMBLE`, `HAS_WEB_UI`, `HAS_DISPLAY`, `HAS_BT_CLASSIC`, `HAS_TFT_ST7789`. Platform macros: `HELTEC_T114`, `HELTEC_V3_433_APRS`, `TTGO_T_Beam_V1_2_433_APRS`, `TTGO_T_BEAM_1W`, `LORANGER_V1`.

## Architecture

### Device Roles (runtime-selectable, no reflash)
- **ROLE_TRACKER** — Beacons position over LoRa; SmartBeacon adaptive rate; optional digipeating
- **ROLE_IGATE** — Receives LoRa RF and uploads to APRS-IS over WiFi (ESP32 only)
- **ROLE_DIGIPEATER** — Relays LoRa packets per APRS WIDEn-N rules

Digipeating (`digi_utils.cpp`) is also independently configurable on any role via `DigiMode`.

### Single-Beacon Profile
Only `beacons[0]` is used. The config array exists for upstream compatibility but multi-beacon support was intentionally removed.

### Platform Abstraction (ESP32 vs. nRF52)
The nRF52 port uses:
- Header shims in `include/nrf52_shims/` replacing SPIFFS, logger, etc.
- `#ifdef ARDUINO_ARCH_NRF52` guards throughout source files
- `[nrf52_common]` block in `common_settings.ini` for build flag differences (C++17, no RadioLib module exclusions, `-Wno-sign-compare`)
- BSP override in `variants_bsp/heltec_t114/`
- LittleFS (via Adafruit InternalFileSystem) instead of SPIFFS

See `NRF52_PORT_NOTES.md` for port decisions and known quirks.

### Configuration System
Runtime config is stored as `tracker_conf.json` on the filesystem. Three interfaces, all without reflashing:
1. **Web UI** — WiFi AP at 192.168.4.1 (auto-starts on first boot or long button hold); served by `web_utils.cpp`
2. **Serial CLI** — Type `setup` from serial KISS mode; see `serial_setup.cpp` and `SERIAL_SETUP.md`
3. **Serial KISS** — Default mode; binary APRS passthrough

**First-boot defaults:**
- ESP32: `data/tracker_conf.json` is flashed to SPIFFS/LittleFS as a filesystem image
- nRF52: `tools/embed_config.py` pre-build script converts the same JSON to a C header; `configuration.cpp` writes it to LittleFS on first boot

### Build Scripts (`tools/`)
- `compress.py` — Pre-build: gzip web UI assets for embedding in firmware
- `embed_config.py` — Pre-build: converts `data/tracker_conf.json` to C header (nRF52 first-boot defaults)
- `build_uf2.py` — Post-build: generates UF2 for nRF52 (USB mass-storage flashing)

### Key Source File Map

| File | Responsibility |
|---|---|
| `src/main.cpp` | Entry point, main loop, button handling, role dispatch |
| `src/configuration.cpp` | JSON config load/save, struct definitions, first-boot defaults |
| `src/lora_utils.cpp` | RadioLib wrapper; TX/RX; FCC TX gate (blocks if callsign is NOCALL) |
| `src/station_utils.cpp` | Beacon/status packet generation; Mic-E encoding; tactical objects |
| `src/smartbeacon_utils.cpp` | SmartBeacon interval calculation; 5 profiles (Runner/Bike/Car/Jetboat/Custom) |
| `src/digi_utils.cpp` | WIDE1-1/WIDE2 digipeating; 25-slot djb2 hash dedup (30s TTL) |
| `src/query_utils.cpp` | APRS station capability query parsing/dispatch (?APRSD, ?PING?, etc.); also surfaces the Msg: display indicator |
| `src/display.cpp` | SSD1306 OLED + ST7789 TFT rendering; eco-mode timeout |
| `src/gps_utils.cpp` | TinyGPSPlus parser; GPS_INTERNAL/GPS_FIXED/GPS_NONE sources |
| `src/wifi_utils.cpp` | WiFi AP + STA management |
| `src/web_utils.cpp` | AsyncWebServer for web config UI |
| `src/aprs_is_utils.cpp` | iGate APRS-IS TCP socket; auto-passcode computation |
| `src/kiss_utils.cpp` | USB serial KISS TNC mode |
| `src/tcp_kiss_utils.cpp` | TCP KISS TNC server (WiFi STA, port 8001) |
| `src/ble_utils.cpp` | BLE KISS TNC (NimBLE on ESP32, native on nRF52; APRS.fi GATT UUID) |
| `src/bluetooth_utils.cpp` | BT Classic SPP (ESP32 non-S3 only) |
| `src/power_utils.cpp` | Sleep modes; GPS disable for power saving |
| `src/battery_utils.cpp` | ADC voltage reads; percentage; sleep threshold |
| `include/<env>/board_pinout.h` | Per-variant pin definitions |
| `lib/APRSPacketLib/` | Local fork of APRSPacketLib (tactical object report support added) |

### Global State
Subsystems share state via `extern` globals (no dependency injection). This is consistent with the upstream codebase style — maintain this pattern when adding features.

## Important Constraints

- **FCC TX gate**: `lora_utils.cpp` calls `APRSPacketLib::checkNocall()` before transmitting. If the source callsign is NOCALL (or unconfigured), the TX is blocked. Do not bypass this check.
- **ESP32-only features**: WiFi AP/STA, web UI, APRS-IS iGate, TCP KISS, BT Classic. Guard any new ESP32-only code with `#ifndef ARDUINO_ARCH_NRF52` or the appropriate capability flag.
- **nRF52 C++ standard**: nRF52 build requires C++17 (`-std=gnu++17`). Avoid C++17 features not available in the ESP32 C++11 build unless guarded.
- **Display disabled on LoRanger_V1**: `HAS_DISPLAY` is not defined; all display code must be guarded. Adding display calls without the guard will cause a boot hang on that target.
- **Single beacon profile**: Never reference `beacons[1]` or higher in new features.
- **`heltec_v3_433_aprs` targets V3.2 hardware only**: VEXT polarity is inverted vs. the original V3 (LOW = peripherals ON; see `power_utils.cpp` `vext_ctrl_ON()`/`OFF()` and `variants/heltec_v3_433_aprs/board_pinout.h`). Flashing this firmware to a true V3 board leaves the OLED unpowered (permanently dark display).
- **Syncronized configuration pathways**: keep index.html, serial_config.html, and mobile apps syncronized for UX, options, and functionality with any configuration changes to the firmware or updates to one of the configuration methods.
- maintain senior developer level of code, with clear comments, function and variable names.