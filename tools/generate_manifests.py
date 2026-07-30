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

# "manufacturer" drives the <optgroup> grouping in the flasher's board dropdown
# (flasher/index.html, serial_config.html) — purely a UI concern, kept here so
# both pages read it from the same source of truth instead of duplicating it.
#
# "ota_partitions": True marks boards built with partitions_8mb_ota.csv — a real
# dual-slot OTA table (app0/app1 + otadata), as opposed to the single-slot
# huge_app.csv used by the other targets. See the update_manifest comment below
# for why that distinction matters to the update (non-factory) manifest.
ESP_TARGETS = [
    {
        "id":    "heltec_v3_433_aprs",
        "label": "Heltec WiFi LoRa 32 V3.2",
        "chip":  "ESP32-S3",
        "desc":  "Heltec WiFi LoRa 32 V3.2 — ESP32-S3, SX1262, SSD1306 OLED, WiFi/BLE.",
        "manufacturer": "Heltec",
        "ota_partitions": True,
    },
    {
        "id":    "tbeam_433_aprs",
        "label": "TTGO T-Beam",
        "chip":  "ESP32",
        "desc":  "TTGO T-Beam V1.2 — ESP32, SX1278, u-blox GPS, SSD1306 OLED, WiFi/BLE.",
        "manufacturer": "LilyGo",
    },
    {
        "id":    "lilygo_t3_433_aprs",
        "label": "LilyGo T3",
        "chip":  "ESP32",
        "desc":  "LilyGo T3 — ESP32, SX1278, SSD1306 OLED, WiFi/BLE. No onboard GPS.",
        "manufacturer": "LilyGo",
    },
    {
        "id":    "tbeam_433_1w_aprs",
        "label": "LilyGo T-Beam 1W",
        "chip":  "ESP32-S3",
        "desc":  "LilyGo T-Beam 1W — ESP32-S3, SX1262 (1 W), onboard GNSS, SH1106 OLED, WiFi/BLE.",
        "manufacturer": "LilyGo",
        "ota_partitions": True,
    },
    {
        "id":    "LoRanger_V1",
        "label": "KJ7NYE LoRanger V1",
        "chip":  "ESP32-S3",
        "desc":  "KJ7NYE LoRanger V1 — ESP32-S3, E22-400M30S (SX1262), ATGM336H GPS. Headless (no display).",
        "manufacturer": "Custom",
        "ota_partitions": True,
    },
    {
        "id":    "heltec_wireless_tracker_433_aprs",
        "label": "Heltec Wireless Tracker",
        "chip":  "ESP32-S3",
        "desc":  "Heltec Wireless Tracker V1.1 — ESP32-S3, SX1262, onboard UC6580 GNSS, 0.96\" ST7735 TFT, WiFi/BLE.",
        "manufacturer": "Heltec",
        "ota_partitions": True,
    },
]

NRF_TARGET = {
    "id":          "heltec_t114",
    "label":       "Heltec T114",
    "chip":        "nRF52840",
    "description": "Heltec T114 — nRF52840, SX1262, Quectel L76K GPS, ST7789 TFT. Use UF2 drag-and-drop — Web Serial is not supported on nRF52.",
    "manufacturer": "Heltec",
    "uf2_url":     f"{base}/heltec_t114_firmware.uf2",
}

# Display order for <optgroup> labels in the board dropdown. Any manufacturer
# value not listed here (future boards) sorts after these, alphabetically.
MANUFACTURER_ORDER = ["Heltec", "LilyGo", "Custom"]

def _manufacturer_sort_key(t):
    mfr = t["manufacturer"]
    idx = MANUFACTURER_ORDER.index(mfr) if mfr in MANUFACTURER_ORDER else len(MANUFACTURER_ORDER)
    return (idx, mfr)

os.makedirs("flasher/manifests", exist_ok=True)

# ESP32 targets get manifest/update_manifest filenames; the nRF52 target keeps
# its own shape (uf2_url, no Web Serial manifests). Build both as plain dicts
# up front, then sort the combined list by manufacturer so the dropdown can
# group targets into <optgroup>s just by walking the array in order and
# watching "manufacturer" change, with no client-side sorting needed.
esp_entries = [
    {
        "id":              t["id"],
        "label":           t["label"],
        "chip":            t["chip"],
        "description":     t["desc"],
        "manufacturer":    t["manufacturer"],
        "manifest":        f"{t['id']}.json",
        "update_manifest": f"{t['id']}_update.json",
    }
    for t in ESP_TARGETS
]
all_targets = sorted(esp_entries + [NRF_TARGET], key=_manufacturer_sort_key)

index = {
    "version":   tag,
    "published": published,
    "repo":      "KJ7NYE/LoRa_FieldOps_APRS_Tracker",
    "targets":   all_targets,
}

with open("flasher/manifests/targets.json", "w") as f:
    json.dump(index, f, indent=2)
print("Wrote flasher/manifests/targets.json")

# per-target ESP Web Tools manifests
for t in ESP_TARGETS:
    # Factory manifest: full merged binary (bootloader + partitions + firmware + SPIFFS).
    # Overwrites all flash regions including config. Use for first-time install or factory reset.
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

    # Update manifest: bare firmware binary at partition offset 0x10000.
    # Leaves SPIFFS untouched so the user's configuration is preserved.
    #
    # Boards on partitions_8mb_ota.csv have two app slots (app0/app1) plus an
    # otadata partition at 0xe000 that tells the bootloader which one to boot.
    # This manifest always writes app0 (the fixed 0x10000 offset below) but
    # never otherwise touches otadata. If otadata was ever left pointing at
    # app1 — e.g. from a single earlier use of the in-device web UI's OTA
    # updater, which does properly flip slots via esp_ota_get_next_update_partition()
    # — every future "Firmware Update" flash here would silently write the
    # *inactive* slot: esp-web-tools reports success (the write to app0 really
    # did succeed and verify), but the bootloader keeps trying to boot the
    # untouched, blank app1 and prints "invalid header: 0xffffffff". Only a
    # Fresh Install recovered from this, because the merged factory image
    # includes boot_app0.bin at 0xe000 (see .github/workflows/flasher.yml's
    # merge_bin step), which resets otadata to select app0.
    # Including that same static boot_app0.bin here — identical on every
    # build/board, so it's committed once rather than produced per-release —
    # makes the update manifest self-consistent with the fixed offset it
    # always writes to, regardless of any prior otadata state.
    update_parts = [{"path": f"{base}/{t['id']}_firmware.bin", "offset": 65536}]
    if t.get("ota_partitions"):
        update_parts.insert(0, {"path": f"{base}/boot_app0.bin", "offset": 57344})  # 0xe000
    update_manifest = {
        "name":    f"LoRa FieldOps — {t['label']} (Firmware Update)",
        "version": tag,
        "builds": [{
            "chipFamily": t["chip"],
            "parts": update_parts,
        }],
    }
    update_path = f"flasher/manifests/{t['id']}_update.json"
    with open(update_path, "w") as f:
        json.dump(update_manifest, f, indent=2)
    print(f"Wrote {update_path}")
