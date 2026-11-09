# LoRa Field Ops APRS Tracker

A multi-role LoRa APRS firmware for 433 MHz amateur radio operations. One firmware build runs as a **tracker**, **iGate**, or **digipeater**, selectable at runtime without reflashing.

It is built for field deployment at events, search and rescue operations, and remote-area monitoring where there is no cellular coverage: dense, fast-cadence position reporting from many trackers sharing one channel.

## Quick links

<div class="grid cards" markdown>

-   :material-download: **Install firmware**

    ---

    Flash a new board from Chrome or Edge over USB, or update an existing one over WiFi or Bluetooth.

    [Install firmware](getting-started/flashing.md)

-   :material-cog: **Serial configuration tool**

    ---

    Configure a tracker over USB from the browser. Nothing to install; works in Chrome and Edge.

    [Open the tool](https://kj7nye.github.io/LoRa_FieldOps_APRS_Tracker/serial_config.html)

-   :material-android: **Android configuration app**

    ---

    Configure from a phone over USB, or over Bluetooth when the tracker's pairing PIN is enabled.

    [Download the app](https://github.com/jeepnjonny/lora-fieldops-config-android/releases)

-   :material-rocket-launch: **First-time setup**

    ---

    Set your callsign, role and radio settings and send your first beacon.

    [First-time setup](getting-started/first-setup.md)

</div>

## Supported boards

| Board | MCU | GPS | Display | WiFi |
|---|---|---|---|---|
| Heltec T114 | nRF52840 | Onboard | 1.14" TFT | — |
| Heltec WiFi LoRa 32 V3.2 | ESP32-S3 | — | OLED | ✅ |
| Heltec Wireless Tracker V1.1 | ESP32-S3 | Onboard | 0.96" TFT | ✅ |
| LilyGo T-Beam | ESP32 | Onboard | OLED | ✅ |
| LilyGo T-Beam 1W | ESP32-S3 | Onboard | OLED | ✅ |
| LilyGo T3 V1.6 | ESP32 | — | OLED | ✅ |
| LilyGo T3-S3 V1.2 / V1.3 | ESP32-S3 | — | OLED | ✅ |
| LoRanger V1 (KJ7NYE) | ESP32-S3 | Onboard | None | ✅ |

Every board supports Bluetooth LE. See [Hardware](hardware/index.md) for the full specifications and the [board notes](hardware/board-notes.md) for board-specific details.

## Where to go next

- **New to the firmware?** Follow [Install firmware](getting-started/flashing.md), then [First-time setup](getting-started/first-setup.md).
- **Setting up an event?** Read [Device roles](guide/roles.md) and [SmartBeacon](guide/smartbeacon.md).
- **Looking up a setting or command?** Use the [Settings reference](reference/settings.md) or the [Serial CLI reference](reference/serial-cli.md), or press `/` to search.
- **Something not working?** Check [Troubleshooting](troubleshooting.md).

> [!IMPORTANT]
> Operating on amateur radio frequencies requires a valid amateur radio licence. The firmware will not transmit until a real callsign is configured. See [About](about.md#legal).
