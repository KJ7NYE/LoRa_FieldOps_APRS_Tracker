#!/usr/bin/env python3
"""
Generate ESP Web Tools manifests and targets.json for the GitHub Pages flasher.
Called by the flasher.yml CI workflow after all release assets are uploaded.

Usage:
    python tools/gen_manifests.py --tag v1.2.3 --repo owner/repo [--output flasher/manifests]
"""

import argparse
import json
import os
from datetime import date

# ── Target definitions ──────────────────────────────────────────────────────
# ESP32 targets: flashed via Web Serial using ESP Web Tools.
# chipFamily must match the ESP Web Tools identifier exactly.
ESP32_TARGETS = [
    {
        "id":         "heltec_v3_433_aprs",
        "label":      "Heltec WiFi LoRa 32 V3.2",
        "chip_family": "ESP32-S3",
        "description": "433 MHz iGate / Digipeater / Tracker — SSD1306 OLED, WiFi, BLE",
    },
    {
        "id":         "tbeam_433_aprs",
        "label":      "LilyGo T-Beam V1.2",
        "chip_family": "ESP32",
        "description": "433 MHz Tracker — onboard GPS, WiFi, BT Classic, BLE",
    },
    {
        "id":         "lilygo_t3_433_aprs",
        "label":      "LilyGo TTGO T3 LoRa32",
        "chip_family": "ESP32",
        "description": "433 MHz iGate / TNC — WiFi, BLE, SSD1306 OLED",
    },
    {
        "id":         "LoRanger_V1",
        "label":      "LoRanger V1 (KJ7NYE)",
        "chip_family": "ESP32-S3",
        "description": "433 MHz Tracker / iGate — onboard GPS, WiFi, BLE, headless",
    },
]

# nRF52 target: UF2 drag-and-drop only — Web Serial cannot flash nRF52.
NRF52_TARGET = {
    "id":    "heltec_t114",
    "label": "Heltec T114 (nRF52840)",
    "chip":  "nRF52840",
    "description": "433 MHz Tracker — onboard GPS, ST7789 TFT, BLE 5",
}


def release_asset_url(repo: str, tag: str, filename: str) -> str:
    return f"https://github.com/{repo}/releases/download/{tag}/{filename}"


def write_esp_manifest(out_dir: str, target: dict, repo: str, tag: str) -> None:
    """Write a single ESP Web Tools manifest JSON for one ESP32 target."""
    asset_name = f"{target['id']}_web_factory.bin"
    manifest = {
        "name":    f"LoRa FieldOps APRS — {target['label']}",
        "version": tag,
        "builds": [
            {
                "chipFamily": target["chip_family"],
                "parts": [
                    {
                        "path":   release_asset_url(repo, tag, asset_name),
                        "offset": 0,
                    }
                ],
            }
        ],
    }
    path = os.path.join(out_dir, f"manifest_{target['id']}.json")
    with open(path, "w") as fh:
        json.dump(manifest, fh, indent=2)
    print(f"  wrote {path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate flasher manifests for a GitHub Release")
    parser.add_argument("--tag",    required=True, help="Release tag, e.g. v1.2.3")
    parser.add_argument("--repo",   required=True, help="GitHub repo as owner/repo")
    parser.add_argument("--output", default="flasher/manifests",
                        help="Directory to write manifests into (default: flasher/manifests)")
    args = parser.parse_args()

    os.makedirs(args.output, exist_ok=True)

    # ── Per-target ESP Web Tools manifests ──────────────────────────────────
    for target in ESP32_TARGETS:
        write_esp_manifest(args.output, target, args.repo, args.tag)

    # ── targets.json index (loaded by flasher/index.html at runtime) ────────
    targets_list = []

    for t in ESP32_TARGETS:
        targets_list.append({
            "id":          t["id"],
            "label":       t["label"],
            "chip":        t["chip_family"],
            "description": t["description"],
            "manifest":    f"manifest_{t['id']}.json",
        })

    # nRF52 entry — download link only, no manifest
    targets_list.append({
        "id":          NRF52_TARGET["id"],
        "label":       NRF52_TARGET["label"],
        "chip":        NRF52_TARGET["chip"],
        "description": NRF52_TARGET["description"],
        "uf2_url":     release_asset_url(args.repo, args.tag,
                                         f"{NRF52_TARGET['id']}_firmware.uf2"),
    })

    index = {
        "version":   args.tag,
        "published": str(date.today()),
        "repo":      args.repo,
        "targets":   targets_list,
    }

    index_path = os.path.join(args.output, "targets.json")
    with open(index_path, "w") as fh:
        json.dump(index, fh, indent=2)
    print(f"  wrote {index_path}")
    print(f"Done — {len(ESP32_TARGETS)} ESP32 manifests + targets.json → {args.output}/")


if __name__ == "__main__":
    main()
