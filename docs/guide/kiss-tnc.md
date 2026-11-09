# Connecting apps (KISS TNC)

The tracker works as a KISS TNC, so APRS apps on a phone or computer can send and receive packets through its LoRa radio. Three links are available, and they run independently of each other.

| Link | Works with | Notes |
|---|---|---|
| **USB serial** | APRSDroid (USB TNC), Xastir, Direwolf, other KISS clients | Default mode at 115200 baud. |
| **Bluetooth LE** | APRSDroid (Android), APRS.fi (iOS) | One phone at a time; PIN pairing on by default. See [Bluetooth & pairing PIN](bluetooth.md). |
| **TCP (WiFi)** | Any network KISS client | Starts automatically once WiFi station mode connects. Port 8001 by default (`tcpkiss port <n>`). |

Bluetooth Classic SPP is also available on ESP32 boards built with `HAS_BT_CLASSIC`.

## USB serial modes

The USB serial port has three modes. You can switch between them at any time without reconnecting:

```text
KISS mode (default)  →  type "setup"  →  Setup mode
                     →  type "log"    →  Log mode
Setup / Log mode     →  type "exit"   →  KISS mode (also on disconnect or reboot)
```

- **KISS mode** (default): binary APRS TNC passthrough for KISS clients.
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
