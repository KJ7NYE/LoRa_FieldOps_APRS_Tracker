# Serial Setup CLI

A USB-serial command-line interface for configuring the tracker without bringing
up the WiFi web-config. Ideal for first-flash provisioning, scripted bulk
configuration, or quick edits when you already have the device tethered.

---

## Quick Start

1. Connect the tracker to your computer via USB.
2. Open any serial terminal at **115200 baud** (PuTTY, Tera Term, PlatformIO
   Monitor, Arduino IDE Serial Monitor, `screen`, etc.).
3. Line ending may be CR, LF, or CRLF — all work.
4. Type `setup` and press **Enter**.
5. You should see:

   ```
   ================================================
    LoRa APRS Tracker - Serial Setup
    type 'help' for commands, 'exit' to leave
    logger paused (ERROR only) while in setup
   ================================================

   >>> SETUP MODE ACTIVE <<<
       current callsign : KJ7NYE-7
       current lora     : EU (433775000 Hz)

   >
   ```

6. Type `help` for the command list, or jump in.

> **Note:** Until you type `setup`, the tracker silently ignores incoming serial
> bytes (so terminal noise won't trigger anything). Local echo is provided by
> the firmware — turn off your terminal's local echo to avoid double characters.

---

## Save / Exit Semantics

Edits live in RAM until you `save`. Three exit paths:

| Command   | Behavior                                                                      |
|-----------|-------------------------------------------------------------------------------|
| `save`    | Writes `tracker_conf.json` to the filesystem, clears the dirty flag, stays in setup. |
| `exit`    | Leaves setup mode. **Refuses** to leave if there are unsaved changes.         |
| `discard` | Throws away unsaved edits. Reboots the device to reload config from disk.     |
| `reboot`  | Plain device restart. Same as `discard` if you have unsaved changes.          |

While setup mode is active, the global logger is dropped to **ERROR-only** so
that periodic `[INFO] LoRa Tx --->` lines don't garble your prompt. The
previous level is restored on `exit`.

---

## Command Reference

### Core

| Command                                   | Description                                            |
|--------------------------------------------|---------------------------------------------------------|
| `help`                                    | List all commands.                                     |
| `show`                                    | Dump entire config.                                    |
| `show <section>`                          | Dump one section (`beacons`, `lora`, `smartcustom`, `display`, `bt`, `bat`, `ptt`, `phg`, `wifi`, `other`). |
| `show secrets`                            | Toggle masked password display (`***` ↔ plaintext).    |
| `save`                                    | Persist to `tracker_conf.json`.                        |
| `export`                                  | Dump the current saved `tracker_conf.json` to the terminal. |
| `import`                                  | Paste a full `tracker_conf.json`. Auto-ends on balanced braces; Ctrl-C aborts. Validates JSON + non-empty `beacons[0].callsign`; reboots on success. |
| `discard`                                 | Drop unsaved changes (reboots).                        |
| `exit` / `quit`                           | Leave setup mode (errors if dirty).                    |
| `reboot`                                  | Reboot the device.                                     |
| `format YES-ERASE-ALL`                    | Wipe the on-device filesystem (config and everything else) and reboot to embedded defaults. Requires the exact confirmation token; anything else just prints a warning. |
| `otadfu`                                  | Enter BLE OTA DFU mode (nRF52 boards only, e.g. Heltec T114). |
| `version`                                 | Print the firmware version string.                     |
| `log <off\|error\|warn\|info\|debug>`     | Set logger level applied after `exit`.                 |

### Beacons

This firmware uses a **single beacon profile** (`Config.beacons[0]`) — there
is no `beacon select` or `beacon list`; every `beacon` command below edits
that one profile directly.

| Command                           | Description                                  |
|-------------------------------------|-------------------------------------------------|
| `beacon callsign <CALL-SSID>`     | Set callsign (e.g. `KJ7NYE-7`).              |
| `beacon symbol <c>`               | APRS symbol character.                       |
| `beacon overlay <c>`              | Symbol overlay character.                    |
| `beacon mice <0..7>`              | Mic-E status code.                           |
| `beacon comment <text...>`        | Free-text comment (rest of line).            |
| `beacon status <text...>`         | Status string (rest of line).                |
| `beacon tactical <text...>`       | Tactical callsign / object name (≤9 chars, longer input is truncated). When set, beacon TX switches from a position report to an APRS Object Report using this name as the object label; the AX.25 source callsign stays your licensed call, and directed APRS station queries are also answered when addressed to this name (see the main [README](README.md#aprs-station-queries)). Empty value reverts to a normal position report. Overrides Mic-E. |
| `beacon label <text...>`          | Profile label shown on screen.               |
| `beacon smart on\|off`            | SmartBeacon active.                          |
| `beacon smartset <0..4>`          | SmartBeacon profile (`0`=Runner, `1`=Bike, `2`=Car, `3`=Jetboat, `4`=Custom — see [SmartBeacon Custom Profile](#smartbeacon-custom-profile)). Out-of-range values are clamped to `0` on boot with a warning. |
| `tx comment\|status`              | Send a position+comment or status beacon immediately, without resetting the SmartBeacon/status timers. |

### SmartBeacon Custom Profile

A user-editable 5th SmartBeacon profile (index 4) used when the beacon
selects `smartset 4`. Edits take effect **live** — no `save` + reboot needed
to retune cadence in the field. Persist with `save` to keep them across
reboots.

| Command                              | Description                                                |
|------------------------------------------|------------------------------------------------------------|
| `smartcustom show`                   | Print the 6 custom values and whether the beacon currently uses them. |
| `smartcustom slowrate <sec>`         | Beacon interval at or below `slowSpeed`.                   |
| `smartcustom slowspeed <km/h>`       | Speed at/below which `slowRate` applies.                   |
| `smartcustom fastrate <sec>`         | Beacon interval at or above `fastSpeed`.                   |
| `smartcustom fastspeed <km/h>`       | Speed at/above which `fastRate` applies.                   |
| `smartcustom turnmindeg <deg>`       | Minimum heading change to trigger a corner peg.            |
| `smartcustom turnslope <n>`          | Turn-angle slope (lower = peg sooner at speed).            |

Defaults are bike-like (`120, 5, 60, 40, 12, 60`). The on-disk JSON stores
these under a top-level `customSmartBeacon` object. Older configs without the
key are auto-upgraded on first boot via the existing missing-key rewrite
path.

If the saved `smartBeaconSetting` is out of range (e.g. from a hand-edited
JSON or older firmware), `checkSettings()` clamps to `0` (Runner) and prints
a warning to serial — the tracker won't crash on a bad value.

### LoRa

A single LoRa radio profile (`Config.loraTypes[0]`) — there is no region
preset list or `lora select`; set the values directly for your region/band
plan.

| Command                  | Description                       |
|------------------------------|------------------------------------|
| `lora freq <Hz>`         | Frequency in hertz.               |
| `lora sf <7..12>`        | Spreading factor.                 |
| `lora bw <Hz>`           | Signal bandwidth in hertz.        |
| `lora cr <5..8>`         | Coding rate denominator.          |
| `lora power <dBm>`       | TX power.                         |

### Display

| Command                       | Description                                    |
|-----------------------------------|---------------------------------------------------|
| `display eco on\|off`         | Eco mode (blank backlight after the configured timeout). |
| `display turn180 on\|off`     | Rotate display 180°.                           |
| `display invert on\|off`      | Invert display colors.                         |
| `display led on\|off`         | Enable/disable the onboard status LED.         |
| `display timeout <sec>`       | Eco-mode auto-off timeout in seconds.          |

### Bluetooth

| Command                       | Description                                           |
|-----------------------------------|-------------------------------------------------------|
| `bt on\|off`                  | Activate Bluetooth at boot.                           |
| `bt name <text>`              | Bluetooth device name (e.g. `LoRaTracker`).           |

### Battery

| Command                         | Description                              |
|--------------------------------------|-------------------------------------|
| `bat sendv on\|off`             | Include voltage in beacon comment.       |
| `bat alwaysv on\|off`           | Send voltage on every beacon.            |
| `bat sleepv <volts>`            | Voltage threshold for deep sleep.        |
| `bat read`                      | Force a fresh ADC sample and print voltage + percent. |

### PTT

| Command                          | Description                                |
|---------------------------------------|-----------------------------------------|
| `ptt on\|off`                    | PTT trigger active.                        |
| `ptt pin <n>`                    | GPIO pin number.                           |
| `ptt reverse on\|off`            | Invert active level.                       |
| `ptt predelay <ms>`              | Delay before TX after asserting PTT.       |
| `ptt postdelay <ms>`             | Delay after TX before releasing PTT.       |

### PHG Beaconing

Power-Height-Gain beaconing sends an uncompressed position beacon advertising
fixed-station RF capability, on its own timer, per the APRS spec's PHG
extension.

| Command                          | Description                                |
|---------------------------------------|-----------------------------------------|
| `phg show`                       | Print current PHG settings and the encoded PHG string. |
| `phg on\|off`                    | Enable/disable PHG beaconing.              |
| `phg power <0..9>`               | Power digit.                               |
| `phg height <0..9>`              | Height digit.                              |
| `phg gain <0..9>`                | Gain digit.                                |
| `phg dir <0..9>`                 | Directivity digit.                         |
| `phg rate <min>`                 | Interval between PHG beacons.              |

### Multi-Role Settings

| Command                                       | Description                                |
|----------------------------------------------------|---------------------------------------------|
| `role show`                                   | Show current device role and GPS source.   |
| `role set tracker\|igate\|digipeater`         | Set device role (takes effect after `save` + reboot). iGate is unavailable on nRF52 boards (no WiFi). |
| `role gps internal\|fixed\|none`              | Set GPS source (takes effect after `save` + reboot). |
| `fixed latitude <dd.dddddd>`                  | Fixed-position latitude, used when GPS source = Fixed. |
| `fixed longitude <dd.dddddd>`                 | Fixed-position longitude.                  |
| `fixed elevation <m>`                         | Fixed-position elevation in meters.        |
| `wifista on\|off`                             | Enable/disable WiFi station mode (iGate uplink). |
| `wifista ssid <text>`                         | WiFi STA SSID.                             |
| `wifista password <text>`                     | WiFi STA password.                         |
| `aprsiss server <host>`                       | APRS-IS server hostname.                   |
| `aprsiss port <n>`                            | APRS-IS server port.                       |
| `aprsiss passcode <code>`                     | APRS-IS passcode override (leave unset to auto-compute from callsign). |
| `aprsiss filter <filter>`                     | APRS-IS server-side filter string.         |
| `aprsiss status`                              | Print live APRS-IS connection status.      |
| `tcpkiss port <n>`                            | TCP KISS server port (default 8001; server auto-starts once WiFi STA connects). |

### Other

| Command                          | Description                                                    |
|---------------------------------------|--------------------------------------------------------------|
| `digi off\|wide1\|wide1+wide2`    | Digipeater mode — works on any device role (see [Digipeater Behavior](#digipeater-behavior)). |
| `wifi password <text>`           | Config AP password (AP triggers on `NOCALL-7` callsign or a long USR-button hold at boot). |
| `beaconpath <path>`              | APRS TX path, e.g. `WIDE1-1` or `WIDE1-1,WIDE2-1`.              |
| `gps read`                       | Print the current GPS position from whichever source is active. |
| `sendspeed on\|off`              | Include speed/course in beacons.                                |
| `sendalt on\|off`                | Include altitude in beacons.                                    |
| `nonsmartrate <min>`             | Beacon interval when SmartBeacon is off.                        |
| `commentafter <n>`               | Send beacon comment every Nth beacon.                            |

---

## Behavior Notes

### Digipeater Behavior

Digipeating is controlled by a single persisted setting, `Config.digiMode`
(`off` / `wide1` / `wide1+wide2`), editable via `digi off|wide1|wide1+wide2`
and available to any device role, not just the dedicated Digipeater role.

`digipeaterActive` is a RAM-only convenience mirror set once at boot from
`Config.digiMode != off` — there is no separate on-device menu toggle (this
firmware has no on-device menu) and no distinct "boot default vs. runtime"
split to reason about: change the mode, `save`, and it takes effect
immediately and persists across reboots.

### WiFi AP Behavior

The config AP is triggered by `WIFI_Utils::checkIfWiFiAP()` at boot, using
two independent conditions — no CLI on/off toggle exists for this behavior:

| Condition                        | Effect                                                        |
|----------------------------------------|-----------------------------------------------------------------|
| Callsign == `NOCALL-7`           | AP starts automatically. Safety net for a fresh/unconfigured device. |
| USR button held at boot          | AP starts even if the callsign is already configured.         |

Once started, the AP session blocks in a loop serving the web UI at
`192.168.4.1` (while still polling the serial CLI, so USB config works
concurrently) and shuts down automatically after a period of no client
activity. The only AP-related CLI setting is the password:

```
setup
wifi password MyNewPassword
save
exit
```

### Password Masking

By default, `wifi.password` and other secret fields show as `***` in `show`
output. Toggle with `show secrets` if you need to verify the actual stored
value.

### Backward Compatibility

When the firmware boots with an older `tracker_conf.json` that lacks newer
fields, `readFile()` detects the missing keys, sets defaults, rewrites the
JSON, and reboots once — giving you a clean upgraded config on the next boot.

### Config Replication via `export` / `import`

`export` dumps the on-disk JSON; `import` accepts a complete pasted JSON and
replaces the on-disk config wholesale. Together they form a backup/restore
+ device-cloning workflow that doesn't require WiFi or external tooling.

**End-of-paste detection.** `import` watches for balanced `{` / `}`, with
string- and escape-awareness so a `}` inside a comment field doesn't
terminate early. Once braces balance after at least one open brace, the
buffer is parsed.

**Validation gates** — all must pass before any flash write:

1. JSON must parse (ArduinoJson `deserializeJson` returns success).
2. `beacons[]` array must exist and be non-empty.
3. `beacons[0].callsign` must be non-empty.

If any gate fails, the existing `tracker_conf.json` is untouched and the
CLI prints a diagnostic. The buffer is capped at 16 KB, and Ctrl-C aborts
mid-paste cleanly.

**Reboot-on-success.** A successful `import` writes the JSON and reboots so
the new config is the live config. This matches the existing `discard`
semantics.

**Round-trip canonicalization.** `import` reserializes via ArduinoJson, so
whitespace and unknown fields are stripped. An `export` from a newer
firmware can be `import`ed by an older firmware (older fields kept, newer
fields ignored) and the existing `readFile()` missing-key fill-in path
backstops with C++ defaults on the next boot.

**Refuses to start with unsaved edits.** If you've made CLI edits without
`save`, `import` errors out — `save` or `discard` first.

---

## Example Sessions

### First-flash provisioning

```
setup
beacon callsign KJ7NYE-7
beacon symbol [
beacon overlay /
beacon comment LoRanger V1 KJ7NYE
lora power 22
beaconpath WIDE1-1
save
exit
```

### Checking and changing digipeater mode

```
setup
show other            # digiMode=off, digiActive(runtime)=no
digi wide1
show other            # digiMode=wide1, digiActive(runtime)=yes
save
reboot
setup
show other            # digiMode still wide1 -- proves persistence
```

### Bumping logger verbosity for a debug session

```
setup
log debug          # will apply after exit
exit
```

### Cloning a device via `export` / `import`

On the source device:

```
setup
export             # copy the JSON between the BEGIN/END markers
exit
```

On the destination device:

```
setup
import             # paste the JSON from above; press Enter
                   # device reboots automatically on success
```

---

## Reference: Config Field Map

The CLI reads/writes the same fields the web-config touches. Mapping CLI
section → JSON path in `tracker_conf.json`:

| CLI section    | JSON key             |
|----------------|-----------------------|
| `beacons`      | `beacons[]`          |
| `lora`         | `loraTypes[]`         |
| `smartcustom`  | `customSmartBeacon`  |
| `display`      | `display`             |
| `bt`           | `bluetooth`           |
| `bat`          | `battery`             |
| `ptt`          | `ptt`                  |
| `phg`          | `phg`                  |
| `wifi`         | `wifiAP`               |
| `role`/`fixed` | `deviceRole`, `gpsSource`, `fixedPosition` |
| `wifista`      | `wifiSTA`              |
| `aprsiss`      | `aprsIS`               |
| `tcpkiss`      | `tcpKISS`              |
| `other`        | top-level scalars (`beaconPath`, `nonSmartBeaconRate`, `sendSpeedCourse`, `sendAltitude`, `digiMode`, `sendCommentAfterXBeacons`) |
