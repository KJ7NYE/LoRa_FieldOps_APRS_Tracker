# Building from source

## Requirements

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- Python 3.x (for the build scripts)

## Quick start

```bash
git clone https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker
cd LoRa_FieldOps_APRS_Tracker

# Build all supported targets
pio run

# Build a specific target
pio run -e heltec_t114

# Flash firmware, then upload the filesystem (default config)
pio run -e heltec_t114 -t upload
pio run -e heltec_t114 -t uploadfs
```

There are no automated tests. CI ([`commit.yml`](https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker/blob/main/.github/workflows/commit.yml)) compiles every environment on each push. For on-air testing, see the [RF test harness](https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker/tree/main/rf_test_harness).

## Environments

| Environment | Board |
|---|---|
| `heltec_t114` | Heltec T114 (nRF52840) |
| `heltec_v3_433_aprs` | Heltec WiFi LoRa 32 V3.2 |
| `tbeam_433_aprs` | LilyGo T-Beam |
| `tbeam_433_1w_aprs` | LilyGo T-Beam 1W |
| `lilygo_t3_433_aprs` | LilyGo T3 V1.6 |
| `lilygo_t3s3_433_aprs` | LilyGo T3-S3 V1.2 / V1.3 (SX1262) |
| `LoRanger_V1` | LoRanger V1 (KJ7NYE) |
| `LoRanger_V2` | LoRanger V2 (KJ7NYE), in development |
| `heltec_wireless_tracker_433_aprs` | Heltec Wireless Tracker V1.1 |

## Project structure

```text
src/                  Firmware source (all platforms)
include/              Headers and configuration structs
variants/             Per-board pin definitions and PlatformIO environments
  heltec_t114/        nRF52840 + ST7789 TFT
  heltec_v3_433_aprs/ ESP32-S3 + OLED
  tbeam_433_aprs/     ESP32 + GPS + AXP PMU
  tbeam_433_1w_aprs/  ESP32-S3 + SX1262 1W + GPS + SH1106
  lilygo_t3_433_aprs/ ESP32 + OLED
  lilygo_t3s3_433_aprs/ ESP32-S3 + SX1262 + OLED
  LoRanger_V1/        ESP32-S3 + SX1262 (headless)
  LoRanger_V2/        ESP32-S3 + SX1262 + accel + microSD (optional OLED on J2)
  heltec_wireless_tracker_433_aprs/  ESP32-S3 + SX1262 + GNSS + ST7735 TFT
variants_bsp/         BSP-level variant overrides (T114 nRF52 BSP)
data/                 Default config JSON (flashed to filesystem)
data_embed/           Web UI assets (gzip-embedded in firmware)
tools/                Build scripts (compress.py, embed_config.py, build_uf2.py)
docs/                 This manual (MkDocs source)
serial_config.html    Standalone serial config tool (Chrome/Edge)
flasher/              Web flasher page, manifests and firmware images
```

## Working on this manual

The manual is built with [MkDocs Material](https://squidfunk.github.io/mkdocs-material/) from the Markdown files in `docs/`, and deployed to GitHub Pages by the [`docs.yml`](https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker/blob/main/.github/workflows/docs.yml) workflow on every push to `main`.

To preview locally:

```bash
pip install -r docs/requirements.txt
mkdocs serve        # live preview at http://127.0.0.1:8000
mkdocs build --strict   # the same check CI runs
```

- Add new pages to the `nav:` list in `mkdocs.yml`.
- Use GitHub-style alerts (`> [!NOTE]`, `> [!WARNING]`). They render on github.com and on the site.
- Link to other pages with relative `.md` paths. Link to `serial_config.html` and `flasher/` with absolute `https://kj7nye.github.io/...` URLs, because they live outside `docs/`.
- When you change a setting, update the [settings reference](../reference/settings.md) and the [CLI reference](../reference/serial-cli.md) alongside the web UI, `serial_config.html` and the Android app.
