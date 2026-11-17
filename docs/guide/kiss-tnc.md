# Connecting apps (KISS TNC)

The tracker works as a KISS TNC, so APRS apps on a phone or computer can send and receive packets through its LoRa radio. Three links are available, and they run independently of each other.

| Link | Works with | Notes |
|---|---|---|
| **USB serial** | APRSDroid (USB TNC), Xastir, Direwolf, other KISS clients | Default mode at 115200 baud. |
| **Bluetooth LE** | APRSDroid (Android), APRS.fi (iOS) | One phone at a time; PIN pairing on by default. See [Bluetooth & pairing PIN](bluetooth.md). |
| **TCP (WiFi)** | Any network KISS client | Starts automatically once WiFi station mode connects. Port 8001 by default (`tcpkiss port <n>`). |

Bluetooth Classic SPP is also available on ESP32 boards built with `HAS_BT_CLASSIC`.

## Using an external iGate (aprx, Direwolf)

Where the site has no WiFi for the built-in iGate, the tracker can act as the radio for an iGate program such as aprx on a host computer. The host gates what the tracker hears to APRS-IS, and hands it messages from APRS-IS to transmit.

- **Packets the tracker hears, and its own transmissions** (beacons, acks, query and remote-config replies) are copied to the KISS link, so the host can gate them to APRS-IS.
- **Messages addressed to the tracker itself** (its callsign or tactical name), including ones the host has wrapped in third-party format (`}`), are answered by the tracker instead of being transmitted. This covers queries such as `?PING` and [remote configuration](remote-config.md) commands sent over APRS-IS.
- **Messages to the `APRS` or `IGATE` broadcast aliases** are answered and also transmitted, so other stations in range can answer too.
- **Everything else** is transmitted on LoRa as usual.

## Signal reports (`kiss.rssi`)

For apps that log coverage, such as the FieldOps app feeding the APRS Coverage website, the tracker can report the signal strength of each packet it hears. Turn it on with `kiss rssi on` (or the **Send signal strength with received packets** switch in the web UI or serial config page). It is off by default.

When on, every packet the tracker receives over LoRa is followed straight away, on the same link, by a KISS SetHardware frame (command byte `0x06`, port 0) whose payload is ASCII text:

```text
RSSI:-97,SNR:6.5
```

RSSI is in whole dBm, SNR in dB with one decimal. The frame uses normal FEND/FESC framing and always comes right after the data frame it describes, with nothing in between. It applies to USB serial, Bluetooth and TCP links alike.

- Only received packets get a signal report, never the tracker's own transmissions.
- Tracker role only. iGates and digipeaters send nothing extra, even with the setting on.
- Leave it off for APRSDroid, APRS.fi, Xastir, Direwolf, aprx and similar programs. Most ignore SetHardware frames, but some may log them as unknown or bad frames, which is why it is off by default.

The tracker's own beacons are copied to the KISS link only once they have actually been transmitted. Listen-before-talk can delay a beacon but never drops it; a beacon blocked by the unconfigured-callsign (NOCALL) check, or one the radio fails to send, is not copied.

The USB **log mode** shows the same values on every received packet: `---> <packet>  [RSSI:-97 SNR:6]`.

## USB serial modes

The USB serial port has three modes. You can switch between them at any time without reconnecting:

```text
KISS mode (default)  →  type "setup"  →  Setup mode
                     →  type "log"    →  Log mode
Setup / Log mode     →  type "exit"   →  KISS mode (also on disconnect or reboot)
```

- **KISS mode** (default): binary APRS TNC passthrough for KISS clients. Log output is fully muted, so nothing but KISS frames reaches the client.
- **Setup mode**: the interactive [configuration CLI](../reference/serial-cli.md). Changes take effect immediately; `save` writes them to flash.
- **Log mode**: live firmware log output. Set the detail level with `log off|error|warn|info|debug`.

The Bluetooth KISS link is unaffected by the USB mode. For example, you can watch the log over USB while APRSDroid is connected over Bluetooth.

## BLE KISS service

The Bluetooth KISS TNC uses the [APRS.fi GATT service](https://aprs.fi/info/a/APRSDROID) profile:

| | UUID |
|---|---|
| Service | `00000001-ba2a-46c9-ae49-01b0961f68bb` |
| TX (device → phone, notify) | `00000003-ba2a-46c9-ae49-01b0961f68bb` |
| RX (phone → device, write) | `00000002-ba2a-46c9-ae49-01b0961f68bb` |
