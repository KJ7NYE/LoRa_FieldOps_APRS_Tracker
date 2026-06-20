"""
Generate flasher/manifests/ for the GitHub Pages web flasher.
Usage: python tools/generate_manifests.py <tag> <published_date>
  tag            - release tag, e.g. v1.0.2
  published_date - ISO date string, e.g. 2026-06-17
"""

import json
import os
import sys

tag       = sys.argv[1]
published = sys.argv[2]
# Firmware is served from GitHub Pages (flasher/firmware/) for CORS compatibility.
# github.com release download URLs redirect without CORS headers and are blocked
# by browser CORS policy when fetched from kj7nye.github.io.
base = "https://kj7nye.github.io/LoRa_FieldOps_APRS_Tracker/flasher/firmware"

ESP_TARGETS = [
    {
        "id":    "heltec_v3_433_aprs",
        "label": "Heltec WiFi LoRa 32 V3",
        "chip":  "ESP32-S3",
        "desc":  "Heltec WiFi LoRa 32 V3 — ESP32-S3, SX1262, SSD1306 OLED, WiFi/BLE.",
    },
    {
        "id":    "tbeam_433_aprs",
        "label": "TTGO T-Beam",
        "chip":  "ESP32",
        "desc":  "TTGO T-Beam V1.2 — ESP32, SX1278, u-blox GPS, SSD1306 OLED, WiFi/BLE.",
    },
    {
        "id":    "lilygo_t3_433_aprs",
        "label": "LilyGo T3",
        "chip":  "ESP32",
        "desc":  "LilyGo T3 — ESP32, SX1278, SSD1306 OLED, WiFi/BLE. No onboard GPS.",
    },
    {
        "id":    "tbeam_433_1w_aprs",
        "label": "LilyGo T-Beam 1W",
        "chip":  "ESP32-S3",
        "desc":  "LilyGo T-Beam 1W — ESP32-S3, SX1262 (1 W), onboard GNSS, SH1106 OLED, WiFi/BLE.",
    },
    {
        "id":    "LoRanger_V1",
        "label": "KJ7NYE LoRanger V1",
        "chip":  "ESP32-S3",
        "desc":  "KJ7NYE LoRanger V1 — ESP32-S3, E22-400M30S (SX1262), ATGM336H GPS. Headless (no display).",
    },
]

NRF_TARGET = {
    "id":          "heltec_t114",
    "label":       "Heltec T114",
    "chip":        "nRF52840",
    "description": "Heltec T114 — nRF52840, SX1262, Quectel L76K GPS, ST7789 TFT. Use UF2 drag-and-drop — Web Serial is not supported on nRF52.",
    "uf2_url":     f"{base}/heltec_t114_firmware.uf2",
}

os.makedirs("flasher/manifests", exist_ok=True)

# targets.json index
index = {
    "version":   tag,
    "published": published,
    "repo":      "KJ7NYE/LoRa_FieldOps_APRS_Tracker",
    "targets": [
        {
            "id":          t["id"],
            "label":       t["label"],
            "chip":        t["chip"],
            "description": t["desc"],
            "manifest":    f"{t['id']}.json",
        }
        for t in ESP_TARGETS
    ] + [NRF_TARGET],
}

with open("flasher/manifests/targets.json", "w") as f:
    json.dump(index, f, indent=2)
print("Wrote flasher/manifests/targets.json")

# per-target ESP Web Tools manifests
for t in ESP_TARGETS:
    manifest = {
        "name":    f"LoRa FieldOps — {t['label']}",
        "version": tag,
        "builds": [{
            "chipFamily": t["chip"],
            "parts": [{"path": f"{base}/{t['id']}_web_factory.bin", "offset": 0}],
        }],
    }
    path = f"flasher/manifests/{t['id']}.json"
    with open(path, "w") as f:
        json.dump(manifest, f, indent=2)
    print(f"Wrote {path}")
