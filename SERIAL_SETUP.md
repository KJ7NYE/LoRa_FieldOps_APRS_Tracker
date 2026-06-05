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
| `save`    | Writes `tracker_conf.json` to SPIFFS, clears the dirty flag, stays in setup.  |
| `exit`    | Leaves setup mode. **Refuses** to leave if there are unsaved changes.         |
| `discard` | Throws away unsaved edits. Reboots the device to reload config from SPIFFS.   |
| `reboot`  | Plain `ESP.restart()`. Same as `discard` if you have unsaved changes.         |

While setup mode is active, the global logger is dropped to **ERROR-only** so
that periodic `[INFO] LoRa Tx --->` lines don't garble your prompt. The
previous level is restored on `exit`.

---

## Command Reference

### Core

| Command                                   | Description                                            |
|-------------------------------------------|--------------------------------------------------------|
| `help`                                    | List all commands.                                     |
| `show`                                    | Dump entire config.                                    |
| `show <section>`                          | Dump one section (`beacons`, `lora`, `smartcustom`, `display`, `bt`, `notif`, `bat`, `telem`, `ptt`, `winlink`, `wifi`, `other`). |
| `show secrets`                            | Toggle masked password display (`***` ↔ plaintext).    |
| `save`                                    | Persist to `tracker_conf.json`.                        |
| `export`                                  | Dump the current saved `tracker_conf.json` to the terminal. |
| `import`                                  | Paste a full `tracker_conf.json`. Auto-ends on balanced braces; Ctrl-C aborts. Validates JSON + non-empty `beacons[0].callsign`; reboots on success. |
| `discard`                                 | Drop unsaved changes (reboots).                        |
| `exit`                                    | Leave setup mode (errors if dirty).                    |
| `reboot`                                  | `ESP.restart()`.                                       |
| `log <off\|error\|warn\|info\|debug>`     | Set logger level applied after `exit`.                 |

### Beacons

The tracker holds multiple beacon profiles (typically 3). Use `select` to pick
which one subsequent commands edit.

| Command                           | Description                                  |
|-----------------------------------|----------------------------------------------|
| `beacon list`                     | List all beacon slots.                       |
| `beacon select <i>`               | Choose beacon to edit (0-based index).       |
| `beacon callsign <CALL-SSID>`     | Set callsign (e.g. `KJ7NYE-7`).              |
| `beacon symbol <c>`               | APRS symbol character.                       |
| `beacon overlay <c>`              | Symbol overlay character.                    |
| `beacon micE <0..7>`              | Mic-E status code.                           |
| `beacon comment <text...>`        | Free-text comment (rest of line).            |
| `beacon status <text...>`         | Status string (rest of line).                |
| `beacon tactical <text...>`       | Tactical callsign (≤9 chars). When set, transmits APRS Object reports with this name as the label; source callsign stays your licensed call. Empty value reverts to position report. Overrides Mic-E. |
| `beacon label <text...>`          | Profile label shown on screen.               |
| `beacon smart on\|off`            | SmartBeacon active.                          |
| `beacon smartset <0..3>`          | SmartBeacon profile (`0`=Runner, `1`=Bike, `2`=Car, `3`=Custom — see [SmartBeacon Custom Profile](#smartbeacon-custom-profile)). |
| `beacon gpseco on\|off`           | Per-beacon GPS eco mode.                     |

### SmartBeacon Custom Profile

A user-editable 4th SmartBeacon profile shared by every beacon that selects
`smartset 3`. Edits take effect **live** — no `save` + reboot needed to
retune cadence in the field. Persist with `save` to keep them across reboots.

| Command                              | Description                                                |
|--------------------------------------|------------------------------------------------------------|
| `smartcustom show`                   | Print the 8 custom values plus which beacons use them.     |
| `smartcustom slowrate <sec>`         | Beacon interval at or below `slowSpeed`.                   |
| `smartcustom slowspeed <km/h>`       | Speed at/below which `slowRate` applies.                   |
| `smartcustom fastrate <sec>`         | Beacon interval at or above `fastSpeed`.                   |
| `smartcustom fastspeed <km/h>`       | Speed at/above which `fastRate` applies.                   |
| `smartcustom mintxdist <m>`          | Minimum distance between beacons.                          |
| `smartcustom mindelta <sec>`         | Minimum spacing between beacons.                           |
| `smartcustom turnmindeg <deg>`       | Minimum heading change to trigger a corner peg.            |
| `smartcustom turnslope <n>`          | Turn-angle slope (lower = peg sooner at speed).            |

Defaults are bike-like (`120, 5, 60, 40, 100, 12, 12, 60`). The on-disk JSON
gains a new top-level `customSmartBeacon` object. Older configs without the
key are auto-upgraded on first boot via the existing missing-key rewrite
path.

If the saved `smartBeaconSetting` is out of range (e.g. from a hand-edited
JSON or older firmware), `checkSettings()` clamps to `0` (Runner) and prints
a warning to serial — the tracker won't crash on a bad value.

### LoRa

Four region presets are stored: `0=EU`, `1=PL`, `2=UK`, `3=US`.

| Command                  | Description                       |
|--------------------------|-----------------------------------|
| `lora list`              | Print all four region presets.    |
| `lora select <0..3>`     | Choose region to edit.            |
| `lora freq <Hz>`         | Frequency in hertz.               |
| `lora sf <7..12>`        | Spreading factor.                 |
| `lora bw <Hz>`           | Signal bandwidth in hertz.        |
| `lora cr <5..8>`         | Coding rate denominator.          |
| `lora power <dBm>`       | TX power.                         |

### Display

| Command                       | Description                                    |
|-------------------------------|------------------------------------------------|
| `display eco on\|off`         | Eco mode (sleep screen between updates).       |
| `display turn180 on\|off`     | Rotate display 180°.                           |
| `display symbol on\|off`      | Show APRS symbol on main screen.               |
| `display timeout <sec>`       | Auto-off timeout in seconds.                   |

### Bluetooth

| Command                       | Description                                           |
|-------------------------------|-------------------------------------------------------|
| `bt on\|off`                  | Activate Bluetooth at boot.                           |
| `bt name <text>`              | Bluetooth device name (e.g. `LoRaTracker`).           |
| `bt ble on\|off`              | Use BLE (off → Bluetooth Classic, where supported).   |
| `bt kiss on\|off`             | KISS framing (off → TNC2 plain text).                 |

### Notifications

| Command                                  | Description                          |
|------------------------------------------|--------------------------------------|
| `notif tx on\|off`                       | LED on TX.                           |
| `notif msg on\|off`                      | LED on message RX.                   |
| `notif flashled on\|off`                 | Flashlight LED feature enabled.      |
| `notif buzzer on\|off`                   | Buzzer master enable.                |
| `notif beep boot on\|off`                | Beep on boot-up.                     |
| `notif beep tx on\|off`                  | Beep on TX.                          |
| `notif beep rx on\|off`                  | Beep on message RX.                  |
| `notif beep station on\|off`             | Beep when new station heard.         |
| `notif beep low on\|off`                 | Beep on low battery.                 |
| `notif beep shutdown on\|off`            | Beep on shutdown.                    |

### Battery

| Command                         | Description                              |
|---------------------------------|------------------------------------------|
| `bat sendv on\|off`             | Include voltage in beacon comment.       |
| `bat astelem on\|off`           | Send voltage as telemetry parameter.     |
| `bat alwaysv on\|off`           | Send voltage on every beacon.            |
| `bat monitor on\|off`           | Enable low-battery monitor.              |
| `bat sleepv <volts>`            | Voltage threshold for deep sleep.        |

### Telemetry

| Command                          | Description                            |
|----------------------------------|----------------------------------------|
| `telem on\|off`                  | Enable telemetry.                      |
| `telem send on\|off`             | Send telemetry packets periodically.   |
| `telem tempcorr <float>`         | Temperature correction offset (°C).    |

### PTT

| Command                          | Description                                |
|----------------------------------|--------------------------------------------|
| `ptt on\|off`                    | PTT trigger active.                        |
| `ptt pin <n>`                    | GPIO pin number.                           |
| `ptt reverse on\|off`            | Invert active level.                       |
| `ptt predelay <ms>`              | Delay before TX after asserting PTT.       |
| `ptt postdelay <ms>`             | Delay after TX before releasing PTT.       |

### Winlink / WiFi / Other

| Command                          | Description                                                    |
|----------------------------------|----------------------------------------------------------------|
| `winlink password <text>`        | Winlink RMS password.                                          |
| `wifi on\|off`                   | WiFi-AP web-config persistent mode (stays up indefinitely).    |
| `wifi window on\|off`            | 30-second AP at boot (default **off** — see [WiFi AP Behavior](#wifi-ap-behavior)). |
| `wifi password <text>`           | WiFi AP password.                                              |
| `digipeater on\|off`             | Persisted digipeater boot default (see [Digipeater Behavior](#digipeater-behavior)). |
| `path <text>`                    | APRS digipeater path (e.g. `WIDE1-1`).                         |
| `email <addr>`                   | Email for the GPS-mail extras feature.                         |
| `simplified on\|off`             | Simplified tracker mode (no menu, no buttons).                 |
| `disablegps on\|off`             | Run as a TNC-only (no GPS).                                    |
| `sendalt on\|off`                | Include altitude in beacons.                                   |
| `nonsmartrate <sec>`             | Beacon interval when SmartBeacon is off.                       |
| `rememberstation <sec>`          | How long to remember heard stations.                           |
| `commentafter <n>`               | Send beacon comment every Nth beacon.                          |

---

## Behavior Notes

### Digipeater Behavior

Two-mode design:

- **`Config.digipeating`** (persisted in `tracker_conf.json`)
  → "Should the digipeater be ON at boot?"
  Edited via `digipeater on|off` in the setup CLI.
  Survives reboots when saved.

- **`digipeaterActive`** (RAM only)
  → "Is the digipeater on right now?"
  Toggled by the on-device menu (Extras → Digipeater).
  Resets to `Config.digipeating` on every boot.

The setup CLI's `digipeater on|off` updates **both** values, so the change
takes effect immediately *and* persists once you `save`. The on-device menu
toggle remains transient (no flash write per press) so users can flip the
digi off temporarily without committing the change.

### WiFi AP Behavior

Three orthogonal flags govern the AP at boot:

| Flag                       | Default | Effect when true                                                      |
|----------------------------|---------|-----------------------------------------------------------------------|
| Callsign == `NOCALL-7`     | (varies)| Force AP up indefinitely. Safety net for fresh/unconfigured devices.  |
| `Config.wifiAP.active`     | true*   | AP stays up indefinitely. Set by the on-device "WiFi AP" menu.        |
| `Config.wifiAP.bootWindow` | **false** | AP up for 30 seconds at boot, then auto-shuts if no client connects. |

*`active` defaults to `true` so a freshly-flashed device is always
configurable, but the web-config flow clears it to `false` on exit.

If **none** of the three flags are true, the AP is skipped entirely — faster
boot, no WiFi radio power.

To enable the 30-second boot AP persistently:

```
setup
wifi window on
save
exit
```

### Password Masking

By default, `winlink.password` and `wifi.password` show as `***` in `show`
output. Toggle with `show secrets` if you need to verify the actual stored
value.

### Backward Compatibility

When the firmware boots with an older `tracker_conf.json` that lacks the new
`digipeating` or `bootWindow` fields, `readFile()` detects the missing keys,
sets defaults, rewrites the JSON, and reboots once — giving you a clean
upgraded config on the next boot.

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

**Reboot-on-success.** A successful `import` writes the JSON and calls
`ESP.restart()` so the new config is the live config. This matches the
existing `discard` semantics.

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
beacon select 0
beacon callsign KJ7NYE-7
beacon symbol [
beacon overlay /
beacon comment LoRanger V1 KJ7NYE
lora select 3
lora power 22
path WIDE1-1
save
exit
```

### Verifying digipeater persistence

```
setup
show other            # digipeating(boot)=off, digipeaterActive=off
digipeater on
show other            # both on
save
reboot
setup
show other            # both still on -- proves persistence
```

### Enabling the 30-second boot AP

```
setup
wifi window on
save
exit
```

The AP `LoRaTracker-AP` (default password `1234567890`) will now appear on
every boot for 30 seconds. Connect to it within that window to access the
web-config at `192.168.4.1`.

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
|----------------|----------------------|
| `beacons`      | `beacons[]`          |
| `lora`         | `lora[]`             |
| `smartcustom`  | `customSmartBeacon`  |
| `display`      | `display`            |
| `bt`           | `bluetooth`          |
| `notif`        | `notification`       |
| `bat`          | `battery`            |
| `telem`        | `telemetry`          |
| `ptt`          | `pttTrigger`         |
| `winlink`      | `winlink`            |
| `wifi`         | `wifiAP`             |
| `other`        | `other`              |
