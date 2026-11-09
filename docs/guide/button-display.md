# Button, display & LED

## USR button

| Press | Action |
|---|---|
| **Short** (50 ms – 3 s) | Send a position beacon now. |
| **Long** (3 – 8 s) | Send the status beacon. |
| **Extra-long** (8 s or more, while running) | Start the WiFi configuration access point (WiFi boards). |

> [!WARNING]
> On the **Heltec V3.2**, **LoRanger V1** and **LilyGo T3-S3**, the USR button (BOOT on the T3-S3) is wired to GPIO0, the ESP32 boot pin. Holding it while powering up or resetting puts the chip into ROM download mode instead of running the firmware. Only hold it **after** the device has booted. If a board gets stuck, see [Troubleshooting](../troubleshooting.md#device-stuck-in-rom-download-mode).

## Display

| Board | Display | What it shows |
|---|---|---|
| Heltec T114 | 240×135 ST7789 TFT | Large tactical or callsign header, 5 body lines, TX overlay |
| Heltec V3.2, T-Beam, T-Beam 1W, T3, T3-S3 | 128×64 OLED | Callsign header, role and battery, last-heard station |
| Heltec Wireless Tracker | 160×80 ST7735 TFT | Compact status screen |
| LoRanger V1 | None | Headless |

**Tactical callsign.** When set, the tactical callsign is the large primary identifier and the licensed callsign is shown smaller below it.

**"Last:" line.** Shows the most recently heard station's callsign-SSID, or the object name when the packet is an APRS Object Report.

**"Msg:" indicator.** Flashes on the T114's bottom row for any message addressed to the device or broadcast (a query like `?PING`, or a free-text message). It stays until the next received packet updates the last-heard line.

**Grid square.** The 6-character Maidenhead locator (for example `DN13ab`) is shown on the TFT.

### Display settings

| Setting | CLI | Default |
|---|---|---|
| Eco mode: blank the display after a period of inactivity. It wakes on a button press, received packet or transmit. | `display eco on\|off` | off |
| Eco timeout (seconds) | `display timeout <sec>` | 4 |
| Rotate 180° for upside-down mounting | `display turn180 on\|off` | off |
| Invert colours | `display invert on\|off` | off |

## LED

The status LED gives a short heartbeat blink (50 ms every 1.5 s) and flashes on transmit and receive. Turn it off with `display led off`.

## Thermal management (T-Beam 1W)

The T-Beam 1W's cooling fan is controlled automatically. See [Board notes](../hardware/board-notes.md#lilygo-t-beam-1w).
