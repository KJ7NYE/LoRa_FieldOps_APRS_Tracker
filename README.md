# LoRa Field Ops APRS Tracker

## [⚙️ Open Serial Configuration Tool](https://kj7nye.github.io/LoRa_FieldOps_APRS_Tracker/serial_config.html)
> Configure your device over USB — no app install required. Works in Chrome and Edge.

---

A multi-role LoRa APRS firmware for 433 MHz amateur radio operations. Supports tracker, iGate, and digipeater roles on a single configurable firmware build. Designed for field deployment at events, search and rescue operations, and remote area monitoring where cellular coverage is absent.

Derived from [richonguzman/LoRa_APRS_Tracker](https://github.com/richonguzman/LoRa_APRS_Tracker) (CA2RXU), with significant architectural changes and new features targeted at 433 MHz APRS operation.

> **License:** GPL-3.0 — see [LICENSE](LICENSE)

---

## Install / Flash Firmware

### ESP32 boards (Heltec V3, T-Beam, T-Beam 1W, T3, LoRanger V1)

Two release assets are published per ESP32 board:

| File | When to use | Tool required |
|---|---|---|
| `<board>_firmware.bin` | **Normal OTA update** — device already running, WiFi reachable | None — upload via device web UI at `192.168.4.1 → Device → Update Firmware` |
| `<board>_web_factory.bin` | **First-time flash or recovery** — new device, bricked device, or partition table changed | USB cable + Chrome/Edge → [kj7nye.github.io/LoRa\_FieldOps\_APRS\_Tracker/flasher](https://kj7nye.github.io/LoRa_FieldOps_APRS_Tracker/flasher/) |

> **Why two files?** The factory binary is a merged image (bootloader + partition table + firmware + filesystem) that writes the entire flash from scratch. The OTA binary is the bare application only — it fits in the OTA partition and is what the device web UI expects. Uploading the factory binary via OTA will fail with "Not Enough Space."

> **When you must use the factory binary (USB required):** first flash on a new device; recovery after a bad flash; any time the partition table changes (e.g. upgrading from a pre-OTA firmware release to one with OTA support).

### Heltec T114 (nRF52840)

| Method | Steps | Download |
|---|---|---|
| **BLE OTA** | Type `otadfu` in serial CLI → open [nRF Connect](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-mobile) (iOS/Android) → scan → connect to DFU target → DFU tab → select zip | [`heltec_t114_dfu.zip`](https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker/releases/latest) |
| **USB drag-and-drop** | Double-tap RESET → drag `.uf2` onto the USB drive that appears | [`heltec_t114_firmware.uf2`](https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker/releases/latest) |

---

## Supported Hardware

| Board | MCU | LoRa | GPS | Display | WiFi | BLE |
|---|---|---|---|---|---|---|
| **Heltec T114** | nRF52840 | SX1262 | Quectel L76K (onboard) | ST7789 1.14" TFT | — | ✅ BLE 5 |
| **Heltec WiFi LoRa 32 V3** | ESP32-S3 | SX1262 | None (fixed position) | SSD1306/SH1106 OLED | ✅ | ✅ NimBLE |
| **LilyGo T-Beam** | ESP32 | SX1278 | u-blox NEO-6M/M8N | SSD1306/SH1106 OLED | ✅ | ✅ NimBLE |
| **LilyGo T-Beam 1W** | ESP32-S3 | SX1262 (1 W) | onboard GNSS (WAKE_UP) | SH1106 OLED | ✅ | ✅ NimBLE |
| **LilyGo T3 V1.6** | ESP32 | SX1278 | None (fixed position) | SSD1306 OLED | ✅ | ✅ NimBLE |
| **LoRanger V1** (KJ7NYE) | ESP32-S3 | EBYTE E22-400 (SX1262) | ATGM336H (onboard) | None (headless) | ✅ | ✅ NimBLE |

---

## Roles

Each device can be configured as one of three roles at runtime — no reflash needed:

| Role | Function |
|---|---|
| **Tracker** | Beacons position via LoRa RF using SmartBeacon adaptive rate |
| **iGate** | Receives LoRa RF packets and uploads to APRS-IS over WiFi |
| **Digipeater** | Receives and re-transmits LoRa RF packets following standard APRS path rules |

Any role can also digipeat simultaneously. The digipeater includes a 30-second hash deduplication buffer to suppress repeated copies of the same packet.

---

## Features

### APRS / RF
- **SmartBeacon** — adaptive beaconing rate based on speed and heading change; parked tracker falls back to slow-rate beacons automatically (no separate timer needed)
- **Mic-E encoding** — compact position format
- **Status beacons** — configurable status text, triggered by long button press (>3 s)
- **WIDE1-1 / WIDE1-1,WIDE2-1** path selection
- **Digipeater deduplication** — 25-slot djb2 hash ring, 30 s TTL prevents duplicate relays
- **6-character Maidenhead grid square** — displayed on TFT (format: `AA00aa`)
- **APRS station capability queries** — responds to directed and undirected queries from any APRS tool; see [APRS Station Queries](#aprs-station-queries)
- **PHG (Power-Height-Gain) beaconing** — optional uncompressed position beacon advertising fixed-station RF capability, sent on its own timer

### Connectivity
- **USB serial KISS TNC** — default mode at 115200 baud; compatible with APRSDroid, Xastir, Direwolf
- **BLE KISS TNC** — APRS.fi GATT service UUID; compatible with APRSDroid (Android), APRS.fi (iOS)
- **TCP KISS server** — starts automatically when WiFi STA is connected (configurable port, default 8001)
- **APRS-IS upload** — iGate role; passcode auto-computed from callsign (Friedman algorithm), optional override
- **Bluetooth Classic SPP** — ESP32 boards with HAS_BT_CLASSIC

### Serial interface
USB serial operates in three modes, switchable at runtime without reconnecting:
- **KISS mode** (default) — binary APRS passthrough for TNC clients
- **Setup mode** — interactive CLI for all configuration
- **Log mode** — live firmware log output at configurable verbosity

Type `setup` or `log` over serial to switch; any mode returns to KISS on disconnect or reboot.

### Configuration
- **Web UI** — served over WiFi AP (hold USR button ≥8 s while running, or callsign = NOCALL-7 on first boot); full configuration via browser, plus OTA firmware update, JSON config backup/restore, and a live firmware log viewer
- **Serial config tool** (`serial_config.html`) — standalone HTML page using the Web Serial API; works in Chrome/Edge 89+; no server required
- **Serial CLI** — `setup` → interactive command line with `help`, `show`, `save`, `reboot`; see [Serial CLI Reference](#serial-cli-reference) below and [SERIAL_SETUP.md](SERIAL_SETUP.md) for the full command set
- **JSON config file** — `tracker_conf.json` on LittleFS/SPIFFS; importable/exportable via serial config tool or the serial CLI's `export`/`import` commands

### Display
- **TFT (T114)** — 240×135 ST7789; tactical or callsign large header, 5 body lines, TX overlay, eco mode backlight timeout
- **OLED (V3/T-Beam/T3)** — 128×64; callsign header, role+battery, last-heard station
- **Tactical callsign** — when set, shown as the large primary identifier; RF callsign shown smaller below
- **"Last:" heard-station line** — shows the most recently heard station's callsign-SSID, or the reported object's name instead when the packet is an APRS Object Report
- **"Msg:" received-message indicator** — flashes on the T114's bottom row for any addressed or broadcast message (a query like `?PING?` or a plain free-text message); persists until the next received packet updates the heard-station line
- **Display eco mode** — configurable timeout; wakes on button press, packet RX, or TX
- **LED indicator** — heartbeat (50 ms/1.5 s) + TX/RX flash; can be disabled in config

### Button (USR)
- **Short press (50 ms – 3 s)** — send position beacon immediately
- **Long press (3 s – 8 s)** — send status beacon
- **Extra-long hold (≥8 s, while running)** — enter AP config mode (WiFi boards)

> **Note for Heltec V3 and LoRanger V1:** the USR button is wired to GPIO0 (the ESP32 BOOT pin). Holding it during power-up or reset forces the chip into ROM download mode instead of running firmware. AP mode must therefore be triggered at runtime, not at boot.

### Thermal Management (T-Beam 1W)

The T-Beam 1W has a cooling fan (GPIO41) and an NTC thermistor input (GPIO14). The firmware implements hybrid thermal control:

| Condition | Fan behaviour |
|---|---|
| LoRa TX active | Fan turns on immediately at TX start |
| Within 30 s of TX end | Fan held on (post-TX cooldown) |
| Temperature ≥ 50 °C | Fan turns on |
| Temperature < 42 °C and no TX cooldown | Fan turns off |
| Temperature ≥ 75 °C | Fan on + `!OVERTEMP` appended to next beacon |
| Temperature ≥ 85 °C | Clean shutdown (same path as low-battery shutdown) |

Temperature is sampled every 30 seconds from a Murata NCP18XH103F03RB (10 kΩ NTC, B = 3380 K) via a 10 kΩ pull-down voltage divider, using the ESP32-S3's eFuse-calibrated `analogReadMilliVolts()`.

---

## Building

### Requirements
- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- Python 3.x (for build scripts)

### Quick start
```bash
git clone https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker
cd LoRa_FieldOps_APRS_Tracker

# Build all supported targets
pio run

# Build a specific target
pio run -e heltec_t114

# Flash + upload filesystem
pio run -e heltec_t114 -t upload
pio run -e heltec_t114 -t uploadfs
```

### Environments

| Environment | Board |
|---|---|
| `heltec_t114` | Heltec T114 (nRF52840) |
| `heltec_v3_433_aprs` | Heltec WiFi LoRa 32 V3 |
| `tbeam_433_aprs` | LilyGo T-Beam |
| `tbeam_433_1w_aprs` | LilyGo T-Beam 1W |
| `lilygo_t3_433_aprs` | LilyGo T3 V1.6 |
| `LoRanger_V1` | LoRanger V1 (KJ7NYE) |

---

## First-Time Setup

1. Flash firmware and filesystem (`upload` + `uploadfs`)
2. On first boot, callsign defaults to `NOCALL-7` → device starts in AP mode
3. Connect to the `LoRaAPRS` WiFi network (password: `1234567890`)
4. Open `http://192.168.4.1` in a browser
5. Set your callsign, role, frequency, and path; click **Save**
6. Device reboots into normal operation

Alternatively, use the serial config tool:
1. Open `serial_config.html` in Chrome or Edge
2. Click **Select Port** and choose your device's USB serial port
3. The tool auto-enters Setup mode and reads the current config
4. Edit fields; each change is sent immediately
5. Click **Save** to persist, or **Reboot** to apply

---

## Configuration Reference

### Beacon
| Setting | Description |
|---|---|
| Callsign | Your amateur radio callsign with SSID (e.g., `KJ7NYE-9`) |
| Tactical callsign | Short event/role identifier (≤9 chars); shown large on display when set. Also switches beacon TX from a position report to an APRS Object Report using this name as the object label — the AX.25 source callsign stays your licensed call. Empty value reverts to a normal position report. |
| Symbol | APRS symbol code (e.g., `>` = car, `[` = runner) |
| Overlay | Symbol table (`/` = primary, `\` = alternate, or overlay letter) |
| Comment | Appended to beacon packet every N beacons |
| Status | Status text sent on long button press |
| SmartBeacon | Adaptive rate based on speed/heading; profiles: Runner, Bike, Car, Jetboat, Custom |
| Beacon TX path | `WIDE1-1` (1 hop) or `WIDE1-1,WIDE2-1` (2 hops) |
| Non-smart rate | Fallback interval (minutes) when SmartBeacon is disabled |

### Device Role
| Setting | Description |
|---|---|
| Device role | Tracker / iGate / Digipeater (dedicated) |
| GPS source | Internal GPS / Fixed position / None |
| Digi mode | Off / WIDE1 fill-in / WIDE1+WIDE2 infrastructure |
| Fixed position | Lat/lon/elevation when GPS source = Fixed |

### Display
| Setting | Description |
|---|---|
| Eco mode | Blank display after N seconds of inactivity |
| Eco timeout | Seconds before display blanks (eco mode) |
| Rotate 180° | Flip display for reversed mounting |
| LED indicator | Enable/disable the onboard status LED |

### PHG (fixed-station beaconing)
| Setting | Description |
|---|---|
| Enabled | Send an uncompressed position beacon with a PHG extension on its own timer |
| Power / Height / Gain / Directivity | PHG digits (0-9 each) encoded into the beacon per APRS spec |
| Beacon rate | Interval between PHG beacons |

### WiFi (iGate/AP)
| Setting | Description |
|---|---|
| AP password | Password for the config AP (default: `1234567890`) |
| WiFi STA | SSID and password for iGate uplink |
| APRS-IS server | Default: `rotate.aprs.net:14580` |
| APRS-IS filter | Default: `m/20` (packets within 20 km) |
| APRS-IS passcode | Leave blank to auto-compute from callsign; set only for override |
| TCP KISS port | Port for TCP KISS server (auto-starts with WiFi STA) |

---

## Serial CLI Reference

Connect USB serial at 115200 baud, then type `setup` to enter the CLI. This is an abbreviated quick reference — see [SERIAL_SETUP.md](SERIAL_SETUP.md) for the full command list with descriptions, behavior notes, and example sessions.

```
help                              show all commands
show [section]                    show current config (all or specific section)
show secrets                      toggle masked password display
save                              write config to flash
reboot                            reboot device
discard                           discard unsaved changes
export                            print full config as JSON (copy/paste to save)
import                            paste JSON config (ends on balanced braces)
format YES-ERASE-ALL              wipe filesystem, reboot to defaults
otadfu                            enter BLE OTA DFU mode (nRF52 only)
version                           print firmware version string

-- beacons --
beacon callsign <CALL-SSID>
beacon symbol <c>          overlay <c>          mice <0..7>
beacon comment <text>      status <text>        label <text>
beacon tactical <text>     (≤9 chars)
beacon smart on|off
beacon smartset <0..4>     (0=Runner 1=Bike 2=Car 3=Jetboat 4=Custom)
beaconpath <path>
tx comment|status          send position/status beacon now (timer unchanged)

-- role --
role set tracker|igate|digipeater
role gps internal|fixed|none
fixed latitude <dd>    longitude <dd>    elevation <m>
digi off|wide1|wide1+wide2

-- network --
wifista on|off    ssid <ssid>    password <pw>
aprsiss server <host>    port <n>    passcode <code>    filter <f>
tcpkiss port <n>
wifi password <text>

-- bluetooth --
bt on|off    name <device-name>

-- lora --
lora freq <hz>    sf <7..12>    bw <hz>    cr <5..8>    power <dbm>

-- display --
display eco|turn180|led on|off
display timeout <sec>

-- PHG beaconing --
phg show                          show current PHG settings
phg on|off                        enable/disable PHG beacon
phg power <0..9>    height <0..9>    gain <0..9>    dir <0..9>
phg rate <min>                    interval between PHG beacons

-- misc --
gps read                          print current GPS position
sendspeed on|off    sendalt on|off
nonsmartrate <min>
commentafter <n>

-- serial mode --
log [off|error|warn|info|debug]   switch to log monitor mode
exit                              return to KISS TNC mode
```

---

## Serial Modes

The USB serial port operates in three mutually exclusive modes:

```
KISS mode (default)  →  type "setup"  →  Setup mode
                     →  type "log"    →  Log mode
Setup / Log mode     →  type "exit"   →  KISS mode (also on disconnect/reboot)
```

**KISS mode** — binary APRS TNC passthrough. Compatible with APRSDroid (USB TNC), Xastir, Direwolf, and other KISS TNC clients.

**Setup mode** — interactive CLI. All changes take effect immediately; `save` persists to flash.

**Log mode** — real-time firmware log. Verbosity controlled with `log <level>`. Does not interfere with BLE KISS TNC operation.

---

## BLE KISS TNC

The BLE KISS TNC uses the [APRS.fi GATT service](https://aprs.fi/info/a/APRSDROID) profile:

| | UUID |
|---|---|
| Service | `00000001-ba2a-46c9-ae49-01b0961f68bb` |
| TX (device → phone, notify) | `00000003-ba2a-46c9-ae49-01b0961f68bb` |
| RX (phone → device, write) | `00000002-ba2a-46c9-ae49-01b0961f68bb` |

**Compatible apps:** APRSDroid (Android), APRS.fi (iOS)

BLE KISS and USB serial log monitoring are fully independent — you can run both simultaneously. BLE KISS is unaffected by which serial mode is active.

---

## APRS-IS Passcode

The APRS-IS passcode is automatically computed from your callsign using the standard Friedman algorithm. You do not need to enter it manually.

To use a different passcode (e.g., for club callsigns where your personal call generates the valid passcode), enter it in the **Passcode (override)** field. Leave blank to use auto-computation.

---

## SmartBeacon Behaviour

SmartBeacon adjusts the beacon interval dynamically:
- **Moving fast** → short interval (fastRate)
- **Moving slow** → long interval (slowRate)
- **Stopped** (speed < 1 km/h) → beacons at slowRate regardless of distance; prevents the tracker from going silent when parked

Heading changes above `turnMinDeg` threshold trigger an immediate beacon regardless of time elapsed.

---

## APRS Station Queries

All device roles (Tracker, iGate, Digipeater) respond to APRS station capability queries as defined in APRS 1.01 §13. Queries are case-insensitive and may be sent from any APRS tool — APRSDroid, Xastir, APRS.fi, a handheld radio, etc.

Duplicate queries from the same station are suppressed for 60 seconds. If the incoming message includes a sequence number (`{NNN}`), an ACK is sent before the response.

Directed queries are also answered when addressed to the configured **tactical callsign** (object name), not just the device's real callsign — `?APRSP`/`?APRS?` addressed to the tactical name reply with the Object Report.

### Directed queries

Send as an APRS message addressed to the device's callsign (or its tactical callsign, if set):

| Query | Response |
|---|---|
| `?APRSD` | List of stations heard directly (no digi hop), space-separated |
| `?APRSH <CALL>` | Whether `<CALL>` has been heard recently and how long ago |
| `?APRSL` | All recently heard stations, newest first |
| `?APRSP` | Current position beacon (same as pressing the beacon button) |
| `?APRSS` | Current status text from config |
| `?APRST` or `?PING?` | Ping reply with device callsign |
| `?APRSV` or `?VER` | Firmware version string |

### Undirected queries

Send as an APRS message addressed to `APRS` or `IGATE`:

| Query | Addressee | Response |
|---|---|---|
| `?APRS?` | `APRS` | Position beacon from all listening stations |
| `?IGATE?` | `IGATE` | iGate-online confirmation *(iGate mode only)* |

### Anti-loop guard

Message packets addressed to the device's own callsign are never digipeated. This prevents IS→RF→IS loops when a query or response re-enters the network via the APRS-IS downlink.

---

## LoRanger V1 Notes

The LoRanger V1 uses a headless (no display) build. The `HAS_DISPLAY` flag is unset to avoid a boot hang that would otherwise occur when the I2C display initialisation fails.

The EBYTE E22-400M30S module is branded as SX1268 but ships with SX1262 silicon — the RadioLib SX1262 driver is used. Confirmed via `VERSION_STRING` readback during bring-up.

---

## Project Structure

```
src/                  Firmware source (all platforms)
include/              Headers and configuration structs
variants/             Per-board pin definitions and PlatformIO environments
  heltec_t114/        nRF52840 + ST7789 TFT
  heltec_v3_433_aprs/ ESP32-S3 + OLED
  tbeam_433_aprs/     ESP32 + GPS + AXP PMU
  tbeam_433_1w_aprs/  ESP32-S3 + SX1262 1W + GPS + SH1106
  lilygo_t3_433_aprs/ ESP32 + OLED
  LoRanger_V1/        ESP32-S3 + SX1262 (headless)
variants_bsp/         BSP-level variant overrides (T114 nRF52 BSP)
data/                 Default config JSON (flashed to filesystem)
data_embed/           Web UI assets (gzip-embedded in firmware)
tools/                Build scripts (compress.py, build_uf2.py)
serial_config.html    Standalone serial config tool (Chrome/Edge)
```

---

## Acknowledgements

- **Ricardo Guzman (CA2RXU)** — [LoRa_APRS_Tracker](https://github.com/richonguzman/LoRa_APRS_Tracker) and [LoRa_APRS_iGate](https://github.com/richonguzman/LoRa_APRS_iGate) — original firmware
- **SparkFun Electronics** — SparkFun Thing Plus ESP32-S3 design, the hardware base for LoRanger V1 (CC BY-SA 4.0)
- **Adafruit Industries** — Bluefruit BLE library and nRF52 BSP
- **h2zero** — NimBLE-Arduino library
- **jgromes** — RadioLib

---

## Legal

Operation on amateur radio frequencies requires a valid amateur radio licence. In the United States, comply with FCC Part 97. High-power modules (>30 dBm) exceed ISM band power limits and are for licensed amateur radio use only.

*73 de KJ7NYE*

> **Fork notice.** This is a fork of [richonguzman/LoRa_APRS_Tracker](https://github.com/richonguzman/LoRa_APRS_Tracker),
> maintained at [KJ7NYE/LoRa_FieldOps_APRS_Tracker](https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker). The upstream
> project is the canonical CA2RXU LoRa APRS Tracker firmware — full credit to Ricardo Guzman (CA2RXU) and the
> upstream contributors. Relevant excerpts from the upstream README are preserved below.
>
> This fork is tuned for **tactical event support** — search and rescue, ultra-marathon timing, jetboat racing,
> and other coordinated multi-tracker deployments where dense, fast-cadence position reporting matters — and
> adds firmware support for the [KJ7NYE LoRanger hardware](https://github.com/KJ7NYE/LoRanger).
>
> **Upstreaming is the goal.** Where changes here are useful beyond event-support contexts, the intent is to
> submit them back to CA2RXU. If you adopt something from this fork that belongs in the base project, please
> help push it upstream.

## What's different in this fork

This fork exists for two reasons: to support the [KJ7NYE LoRanger hardware](https://github.com/KJ7NYE/LoRanger),
and to make the firmware better suited for licensed-amateur tactical event support in the US.

- **LoRanger hardware support.** Firmware variant for an open-source ESP32-S3 + 1 W SX1268 + GPS tracker built
  for canyon and beyond-cellular field deployments. CC-BY-SA-4.0.

- **Defaults tuned for US compliance.** LoRa modulation defaults are set to comply with US licensed-amateur
  regulations on 70 cm. Operators outside the US should verify settings against their local rules before
  transmitting.

- **Tuned for dense multi-tracker channels.** Beacon cadence, comment-ID interval, SmartBeacon profile, and
  APRS path defaults are retuned for coordinated deployments where 15–20 trackers share a channel during an
  event window — search and rescue, ultra-marathon timing, jetboat racing, and similar.

- **Configuration ergonomics for field deployment.** USB serial setup CLI for scripted/tethered
  configuration, persistent digipeater state across reboots, and a short boot-time AP window that makes the
  web UI reachable without relying on display. Initial provisioning of a fresh device still routes through
  the web UI.

> Network compatibility note: every device on a LoRa APRS network must run identical SF/BW/CR. If you flash
> this fork onto a tracker that needs to talk to a fleet running stock CA2RXU defaults, you must either
> change the receivers or change this tracker's settings to match.

For per-commit specifics — file paths, exact values, divergence point from upstream — see
[CHANGELOG.md](https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker/blob/main/CHANGELOG.md).

---

# Upstream Project — CA2RXU LoRa APRS Tracker/Station

This firmware is for using ESP32-based boards with LoRa modules and GPS to live in the APRS world.

> **NOTE:** To use Tx/Rx capabilities of this tracker you should also have a Tx/Rx
> [LoRa iGate](https://github.com/richonguzman/LoRa_APRS_iGate) nearby.

## Support the upstream project

If this fork is useful to you, please also consider supporting Ricardo (CA2RXU), whose work this is built on:

[<img src="https://github.com/richonguzman/LoRa_APRS_Tracker/blob/main/images/github-sponsors.png">](https://github.com/sponsors/richonguzman) [<img src="https://github.com/richonguzman/LoRa_APRS_Tracker/blob/main/images/paypalme.png">](http://paypal.me/richonguzman)

## Upstream feature highlights

- Tracker with on-device menu (read/write/delete messages with I2C keyboard or phone, weather report, recent stations, eco/brightness controls)
- Bluetooth TNC (Android + APRSDroid, iPhone + APRS.fi) — BLE or BT Classic, KISS or TNC2
- Three configurable LoRa APRS region presets (EU/PL/UK)
- LED + buzzer notifications for Tx, message Rx, and boot events
- BME280 / BMP280 / BME680 weather telemetry
- Winlink mail through APRSLink
- Encoded GPS beacons for shorter on-air time
- Battery monitor with low-voltage sleep protection

## Upstream documentation (Wiki — English / Español)

- [FAQ — GPS, Bluetooth, Winlink, BME280, etc.](https://github.com/richonguzman/LoRa_APRS_Tracker/wiki/00.-FAQ-(frequently-asked-questions))
- [Supported boards and buying links](https://github.com/richonguzman/LoRa_APRS_Tracker/wiki/1000.-Supported-Boards-and-Buying-Links)
- [Installation guide](https://github.com/richonguzman/LoRa_APRS_Tracker/wiki/01.-Installation-Guide-%23-Guia-de-Instalacion)
- [Tracker configuration reference](https://github.com/richonguzman/LoRa_APRS_Tracker/wiki/02.-Tracker-Configuration--%23--Configuracion-del-Tracker)
- [Upload firmware and filesystem](https://github.com/richonguzman/LoRa_APRS_Tracker/wiki/03.-Upload-Firmware-and-Filesystem-%23-Subir-Firmware-y-sistema-de-archivos)
- [On-device menu guide](https://github.com/richonguzman/LoRa_APRS_Tracker/wiki/04.-Menu-Guide-%23-Guía-del-menú)

For upstream version history, see the [upstream repository](https://github.com/richonguzman/LoRa_APRS_Tracker).

## License

GPL-3.0, inherited from the upstream project. All modifications in this fork are released under the same
license. See [LICENSE](https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker/blob/main/LICENSE) for the full terms.

## This code was based on the work of:

- [Ricardo CA2RXU — LoRa APRS Tracker (direct upstream of this fork)](https://github.com/richonguzman/LoRa_APRS_Tracker)
- [Serge ON4AA — base91 byte-saving / encoding](https://github.com/aprs434/lora.tracker)
- [Peter OE5BPA — LoRa APRS Tracker](https://github.com/lora-aprs/LoRa_APRS_Tracker)
- [Manfred DC2MH (Mane76) — multiple-callsigns and processor-speed mods](https://github.com/Mane76/LoRa_APRS_Tracker)
- [Thomas DL9SAU — KISS / TNC2 lib](https://github.com/dl9sau/TTGO-T-Beam-LoRa-APRS)

---

*Original work: 73! — CA2RXU, Valparaíso, Chile*
*Fork maintained by: KJ7NYE, Idaho, US*
