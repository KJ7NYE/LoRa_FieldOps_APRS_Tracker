# Settings reference

Every setting can be changed from the **web UI**, the **serial configuration tool**, the **Android app**, or the **[serial CLI](serial-cli.md)**. The sections below follow the order of the web UI. Defaults are the first-boot values from `data/tracker_conf.json`.

Most changes apply immediately. Changes to role, GPS source and Bluetooth apply after **save** and a reboot.

## Beacon

| Setting | Default | CLI | Description |
|---|---|---|---|
| Callsign | `NOCALL-7` | `beacon callsign <CALL-SSID>` | Your licensed callsign with SSID, for example `KJ7NYE-9`. The device will not transmit while this is `NOCALL`. |
| Tactical callsign | empty | `beacon tactical <text>` | Short event or role name (up to 9 characters). Shown large on the display, and switches beacons to an APRS Object Report using this name. The AX.25 source stays your licensed callsign. Leave empty for normal position reports. |
| Symbol | `>` (car) | `beacon symbol <c>` | APRS symbol character, for example `[` for a runner. |
| Overlay | `/` | `beacon overlay <c>` | Symbol table: `/` primary, `\` alternate, or an overlay letter. |
| Comment | `LoRa APRS` | `beacon comment <text>` | Added to every Nth beacon (see *Comment every N beacons*). |
| Status | empty | `beacon status <text>` | Status text, sent on a long button press. |
| Mic-E status | empty | `beacon mice <0..7>` | Mic-E status code. Ignored when a tactical callsign is set. |
| Label | empty | `beacon label <text>` | Profile label shown on the display. |
| SmartBeacon | on | `beacon smart on\|off` | Adapt the beacon rate to speed and turns. See [SmartBeacon](../guide/smartbeacon.md). |
| SmartBeacon profile | Car | `beacon smartset <0..4>` | 0 Runner, 1 Bike, 2 Car, 3 Jetboat, 4 Custom. |
| Beacon path | `WIDE1-1` | `beaconpath <path>` | `WIDE1-1` (one hop) or `WIDE1-1,WIDE2-1` (two hops). |
| Fixed beacon rate | 900 s | `nonsmartrate <sec>` | Interval when SmartBeacon is off, and when parked with SmartBeacon on. |
| Comment every N beacons | 10 | `commentafter <n>` | How often the comment is included. |
| Send speed/course | on | `sendspeed on\|off` | Include speed and course in beacons. |
| Send altitude | on | `sendalt on\|off` | Include altitude in beacons. |

## SmartBeacon Custom

Used when the SmartBeacon profile is **4 — Custom**. Ranges and details are in the [CLI reference](serial-cli.md#smartbeacon-custom-profile).

| Setting | Default | CLI |
|---|---|---|
| Slow rate | 120 s | `smartcustom slowrate <sec>` |
| Slow speed | 5 km/h | `smartcustom slowspeed <km/h>` |
| Fast rate | 60 s | `smartcustom fastrate <sec>` |
| Fast speed | 40 km/h | `smartcustom fastspeed <km/h>` |
| Min turn angle | 12° | `smartcustom turnmindeg <deg>` |
| Turn slope | 60 | `smartcustom turnslope <n>` |
| Min turn time | 15 s | `smartcustom turnmintime <sec>` |

## LoRa Radio

> [!WARNING]
> Every device on a LoRa APRS network must use the same frequency, spreading factor, bandwidth and coding rate. These defaults suit US 70 cm use and differ from stock CA2RXU firmware.

| Setting | Default | CLI | Description |
|---|---|---|---|
| Frequency | 433.775 MHz | `lora freq <Hz>` | In hertz, for example `433775000`. |
| Spreading factor | 8 | `lora sf <7..12>` | |
| Bandwidth | 62.5 kHz | `lora bw <Hz>` | In hertz, for example `62500`. |
| Coding rate | 4/5 | `lora cr <5..8>` | Denominator only. |
| TX power | 20 dBm | `lora power <dBm>` | 2–20 on 1 W boards (T-Beam 1W, LoRanger V1), 2–22 elsewhere. See the [power table](serial-cli.md#lora). |
| Listen before TX (CAD) | on | `lora cad on\|off` | Wait for a clear channel plus a short random backoff before transmitting. Transmits anyway after 10 s of busy channel. |

## Display

| Setting | Default | CLI | Description |
|---|---|---|---|
| Eco mode | off | `display eco on\|off` | Blank the display after a period of inactivity. |
| Eco timeout | 4 s | `display timeout <sec>` | Seconds before the display blanks. |
| Rotate 180° | off | `display turn180 on\|off` | For upside-down mounting. |
| Invert | off | `display invert on\|off` | Invert display colours. |
| Status LED | on | `display led on\|off` | Heartbeat and TX/RX flashes. |

## Bluetooth

| Setting | Default | CLI | Description |
|---|---|---|---|
| Bluetooth | off | `bt on\|off` | Start Bluetooth at boot. |
| Device name | `LoRaAPRS` | `bt name <text>` | |
| Require PIN | on | `bt pin on\|off` | Also required for [setup over Bluetooth](../guide/bluetooth.md#setup-over-bluetooth). |
| PIN | `123456` | `bt pin <4-6 digits>` | Change it from the default. See [Bluetooth & pairing PIN](../guide/bluetooth.md). |

## Battery & Altitude

| Setting | Default | CLI | Description |
|---|---|---|---|
| Send voltage | off | `bat sendv on\|off` | Include battery voltage in the beacon comment. |
| Always send voltage | off | `bat alwaysv on\|off` | Include voltage in every beacon. |
| Sleep voltage | 2.9 V | `bat sleepv <volts>` | Below this voltage the device goes to deep sleep. |

`bat read` prints a fresh voltage and percentage reading.

## PTT Trigger

For driving an external amplifier or radio PTT line.

| Setting | Default | CLI |
|---|---|---|
| PTT active | off | `ptt on\|off` |
| GPIO pin | -1 (none) | `ptt pin <n>` |
| Reverse (invert active level) | off | `ptt reverse on\|off` |
| Pre-TX delay | 0 ms | `ptt predelay <ms>` |
| Post-TX delay | 0 ms | `ptt postdelay <ms>` |

## PHG (fixed-station beaconing)

Advertises a fixed station's power, antenna height, gain and directivity. Sent as a separate uncompressed position beacon on its own timer. Mainly for iGates and digipeaters.

| Setting | Default | CLI |
|---|---|---|
| Enabled | off | `phg on\|off` |
| Power digit | 7 | `phg power <0..9>` |
| Height digit | 2 | `phg height <0..9>` |
| Gain digit | 3 | `phg gain <0..9>` |
| Directivity digit | 0 | `phg dir <0..9>` |
| Beacon rate | 10 min | `phg rate <min>` |

`phg show` prints the settings and the encoded PHG string.

## Device Role

| Setting | Default | CLI | Description |
|---|---|---|---|
| Role | Tracker | `role set tracker\|igate\|digipeater` | See [Device roles](../guide/roles.md). iGate needs WiFi (not on the T114). |
| GPS source | Internal | `role gps internal\|fixed\|none` | |
| Fixed position | 0, 0, 0 m | `fixed latitude <dd>`, `fixed longitude <dd>`, `fixed elevation <m>` | Used when GPS source is Fixed. |
| Digipeater mode | Off | `digi off\|wide1\|wide1+wide2` | Works on any role. |

## Remote Configuration (CourseSentry)

| Setting | Default | CLI |
|---|---|---|
| Enabled | off | `remotecfg on\|off` |
| Token | empty | `remotecfg token <secret>` |
| Write window | 300 s | `remotecfg window <60-3600>` |

See [Remote configuration](../guide/remote-config.md).

## WiFi

| Setting | Default | CLI | Description |
|---|---|---|---|
| AP password | `1234567890` | `wifi password <text>` | Password for the `LoRaAPRS` configuration access point. |
| WiFi station | off | `wifista on\|off` | Connect to a WiFi network (iGate uplink, TCP KISS). |
| Networks | none | `wifista add`, `ssid`, `password`, `remove`, `list` | Up to 5, tried in order. |
| APRS-IS server | `rotate.aprs.net` | `aprsiss server <host>` | |
| APRS-IS port | 14580 | `aprsiss port <n>` | |
| APRS-IS filter | `m/20` | `aprsiss filter [<f>]` | Packets within 20 km. |
| APRS-IS passcode | blank (auto) | `aprsiss passcode [<code>]` | Only set to override the computed passcode. |
| APRS-IS downlink | on | `aprsiss downlink on\|off` | Pass messages from APRS-IS back to RF. |
| TCP KISS port | 8001 | `tcpkiss port <n>` | |

See [iGate & APRS-IS](../guide/igate.md).
