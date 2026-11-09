# Supported hardware

| Board | MCU | LoRa | GPS | Display | WiFi | BLE |
|---|---|---|---|---|---|---|
| **Heltec T114** | nRF52840 | SX1262 | Quectel L76K (onboard) | ST7789 1.14" TFT | — | ✅ BLE 5 |
| **Heltec WiFi LoRa 32 V3.2** | ESP32-S3 | SX1262 | None (fixed position) | SSD1306/SH1106 OLED | ✅ | ✅ NimBLE |
| **Heltec Wireless Tracker V1.1** | ESP32-S3 | SX1262 | UC6580 (onboard) | ST7735 0.96" TFT | ✅ | ✅ NimBLE |
| **LilyGo T-Beam** | ESP32 | SX1278 | u-blox NEO-6M/M8N | SSD1306/SH1106 OLED | ✅ | ✅ NimBLE |
| **LilyGo T-Beam 1W** | ESP32-S3 | SX1262 (1 W) | Onboard GNSS | SH1106 OLED | ✅ | ✅ NimBLE |
| **LilyGo T3 V1.6** | ESP32 | SX1278 | None (fixed position) | SSD1306 OLED | ✅ | ✅ NimBLE |
| **LilyGo T3-S3 V1.2 / V1.3** | ESP32-S3 | SX1262 (433 MHz version) | None (fixed position) | SSD1306 OLED | ✅ | ✅ NimBLE |
| **LoRanger V1** (KJ7NYE) | ESP32-S3 | EBYTE E22-400 (SX1262) | ATGM336H (onboard) | None (headless) | ✅ | ✅ NimBLE |

The [LoRanger](https://github.com/KJ7NYE/LoRanger) is an open-source ESP32-S3, 1 W, GPS tracker designed by KJ7NYE for canyon and beyond-cellular field deployments. A LoRanger V2 firmware target is in development.

## What each board can do

| Feature | Heltec T114 | ESP32 boards |
|---|---|---|
| Tracker and digipeater roles | ✅ | ✅ |
| iGate role (APRS-IS) | — | ✅ |
| Web UI and WiFi firmware update | — | ✅ (WiFi update not on the 4 MB T-Beam and T3 V1.6) |
| Bluetooth KISS TNC and setup over Bluetooth | ✅ | ✅ |
| Firmware update over Bluetooth | ✅ | — |

> [!WARNING]
> The `heltec_v3_433_aprs` firmware targets the **Heltec V3.2** only. The original V3 switches peripheral power the opposite way, so this firmware leaves its OLED unpowered (the display stays dark).

## Boards without GPS

The Heltec V3.2, T3 V1.6 and T3-S3 have no GPS. Use them with a fixed position (web UI or `role gps fixed`), as an iGate or digipeater, or with a phone app supplying position over [Bluetooth or USB KISS](../guide/kiss-tnc.md).

See [Board notes](board-notes.md) for board-specific details.
