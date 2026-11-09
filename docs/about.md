# About

This firmware is a fork of [richonguzman/LoRa_APRS_Tracker](https://github.com/richonguzman/LoRa_APRS_Tracker) by Ricardo Guzman (CA2RXU), maintained at [KJ7NYE/LoRa_FieldOps_APRS_Tracker](https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker). Full credit goes to CA2RXU and the upstream contributors.

The fork is tuned for **tactical event support**: search and rescue, ultra-marathon timing, jetboat racing and other coordinated multi-tracker deployments where dense, fast-cadence position reporting matters.

## What's different in this fork

- **LoRanger hardware support.** A firmware target for the [KJ7NYE LoRanger](https://github.com/KJ7NYE/LoRanger), an open-source ESP32-S3, 1 W, GPS tracker built for canyon and beyond-cellular deployments (CC BY-SA 4.0).
- **nRF52840 support.** The Heltec T114, with Bluetooth firmware updates and USB drag-and-drop flashing.
- **Defaults tuned for US compliance.** LoRa defaults comply with US licensed-amateur regulations on 70 cm. Operators outside the US should check the settings against local rules before transmitting.
- **Tuned for dense multi-tracker channels.** Beacon cadence, comment interval, SmartBeacon profiles and path defaults are retuned for deployments where 15–20 trackers share a channel during an event.
- **Configuration for field deployment.** A serial setup CLI (over USB or Bluetooth), a browser-based serial configuration tool, an Android app, persistent digipeater settings, and remote configuration over APRS messages.
- **Single beacon profile.** Upstream's multi-beacon and on-device menu features are removed in favour of one configurable beacon.

> [!NOTE]
> Every device on a LoRa APRS network must use the same spreading factor, bandwidth and coding rate. If this tracker needs to talk to a network running stock CA2RXU defaults, change either the network or this tracker's settings to match.

For per-release details, see the [changelog](https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker/blob/main/CHANGELOG.md) and the [releases page](https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker/releases).

## Upstream project

The upstream CA2RXU firmware is a full-featured LoRa APRS tracker with an on-device menu, messaging with an I2C keyboard, weather sensors, Winlink through APRSLink and more. Its [wiki](https://github.com/richonguzman/LoRa_APRS_Tracker/wiki) (English and Spanish) covers those features.

If this fork is useful to you, please also consider supporting Ricardo (CA2RXU), whose work it is built on: [GitHub Sponsors](https://github.com/sponsors/richonguzman) or [PayPal](http://paypal.me/richonguzman).

## Acknowledgements

- **Ricardo Guzman (CA2RXU)**: [LoRa_APRS_Tracker](https://github.com/richonguzman/LoRa_APRS_Tracker) and [LoRa_APRS_iGate](https://github.com/richonguzman/LoRa_APRS_iGate), the original firmware
- **Serge ON4AA**: [base91 byte-saving encoding](https://github.com/aprs434/lora.tracker)
- **Peter OE5BPA**: [LoRa APRS Tracker](https://github.com/lora-aprs/LoRa_APRS_Tracker)
- **Manfred DC2MH (Mane76)**: [multiple-callsign and processor-speed changes](https://github.com/Mane76/LoRa_APRS_Tracker)
- **Thomas DL9SAU**: [KISS / TNC2 library](https://github.com/dl9sau/TTGO-T-Beam-LoRa-APRS)
- **SparkFun Electronics**: SparkFun Thing Plus ESP32-S3 design, the hardware base for the LoRanger V1 (CC BY-SA 4.0)
- **Adafruit Industries**: Bluefruit BLE library and nRF52 BSP
- **h2zero**: NimBLE-Arduino library
- **jgromes**: RadioLib

## Legal

Operating on amateur radio frequencies requires a valid amateur radio licence. In the United States, comply with FCC Part 97. High-power modules (above 30 dBm) exceed ISM band power limits and are for licensed amateur radio use only.

The firmware will not transmit while the callsign is `NOCALL` or unset.

## License

GPL-3.0, inherited from the upstream project. All changes in this fork are released under the same licence. See [LICENSE](https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker/blob/main/LICENSE).

*73 de KJ7NYE*
