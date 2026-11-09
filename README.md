# LoRa Field Ops APRS Tracker

A multi-role LoRa APRS firmware for 433 MHz amateur radio operations. One firmware build runs as a **tracker**, **iGate** or **digipeater**, selectable at runtime without reflashing. It is designed for field deployment at events, search and rescue operations, and remote-area monitoring where there is no cellular coverage.

Forked from [richonguzman/LoRa_APRS_Tracker](https://github.com/richonguzman/LoRa_APRS_Tracker) (CA2RXU), with nRF52840 support, [KJ7NYE LoRanger](https://github.com/KJ7NYE/LoRanger) hardware support and US-tuned defaults.

## 📖 [Read the user manual](https://kj7nye.github.io/LoRa_FieldOps_APRS_Tracker/)

| | |
|---|---|
| ⬇️ **[Web flasher](https://kj7nye.github.io/LoRa_FieldOps_APRS_Tracker/flasher/)** | Install or update firmware from Chrome or Edge over USB. |
| ⚙️ **[Serial configuration tool](https://kj7nye.github.io/LoRa_FieldOps_APRS_Tracker/serial_config.html)** | Configure a device over USB in the browser. Nothing to install. |
| 📱 **[Android configuration app](https://github.com/jeepnjonny/lora-fieldops-config-android/releases)** | Configure from a phone over USB, or over Bluetooth with the pairing PIN enabled. |
| 📦 **[Releases](https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker/releases/latest)** | Firmware files for every board. |

## Supported boards

Heltec T114 (nRF52840) · Heltec WiFi LoRa 32 V3.2 · Heltec Wireless Tracker V1.1 · LilyGo T-Beam · LilyGo T-Beam 1W · LilyGo T3 V1.6 · LilyGo T3-S3 V1.2/V1.3 · LoRanger V1

See [Supported hardware](docs/hardware/index.md) for specifications.

## Documentation

| Topic | Pages |
|---|---|
| **Getting started** | [Install firmware](docs/getting-started/flashing.md) · [First-time setup](docs/getting-started/first-setup.md) |
| **Using the tracker** | [Roles & digipeating](docs/guide/roles.md) · [SmartBeacon](docs/guide/smartbeacon.md) · [Button, display & LED](docs/guide/button-display.md) · [KISS TNC](docs/guide/kiss-tnc.md) · [Bluetooth & PIN](docs/guide/bluetooth.md) · [iGate & APRS-IS](docs/guide/igate.md) · [APRS queries](docs/guide/aprs-queries.md) · [Remote configuration](docs/guide/remote-config.md) |
| **Reference** | [Settings](docs/reference/settings.md) · [Serial CLI](docs/reference/serial-cli.md) |
| **Hardware** | [Supported hardware](docs/hardware/index.md) · [Board notes](docs/hardware/board-notes.md) |
| **Help** | [Troubleshooting](docs/troubleshooting.md) |
| **Developers** | [Building from source](docs/developers/building.md) · [nRF52 port notes](docs/developers/nrf52-port-notes.md) · [Changelog](CHANGELOG.md) |
| **About** | [Fork differences, credits & legal](docs/about.md) |

## Building

```bash
pio run                      # build all targets
pio run -e heltec_t114       # build one target
```

See [Building from source](docs/developers/building.md).

## Legal & licence

Operating on amateur radio frequencies requires a valid amateur radio licence; in the US, comply with FCC Part 97. The firmware will not transmit until a real callsign is configured.

GPL-3.0, inherited from the upstream project. See [LICENSE](LICENSE). Full credit to Ricardo Guzman (CA2RXU) and the upstream contributors; see [About](docs/about.md) for acknowledgements and how to support the upstream project.

*73 de KJ7NYE*
