# LoRa FieldOps APRS Tracker — Changelog

Running log of changes in this fork ([KJ7NYE/LoRa_FieldOps_APRS_Tracker](https://github.com/KJ7NYE/LoRa_FieldOps_APRS_Tracker))
on top of upstream [richonguzman/LoRa_APRS_Tracker](https://github.com/richonguzman/LoRa_APRS_Tracker).

Fork divergence point: upstream commit [`bfd531a`](https://github.com/richonguzman/LoRa_APRS_Tracker/commit/bfd531a) — *"winlink challenge fisher-yates update"* (2026-04-23).

Newest entries first. Format: `YYYY-MM-DD — short title (commit)` followed by a brief description.

---

## 2026-07-02 — Show object names on "Last:" and a "Msg:" received indicator

Two status-display improvements. First, when the most recently heard packet is an APRS Object Report (`;OBJECTNAME*...`), the "Last:" line now shows the reported object's name instead of the transmitting station's callsign-SSID — the `?APRSD`/`?APRSH`/`?APRSL` heard-station log still tracks the real callsign, only the display summary changed. Second, any addressed or broadcast message packet (a query like `?PING?` or a plain free-text message) now flashes a `Msg: <text>` indicator on the T114's bottom display row; it stays up until the next RX event refreshes the heard-station log, rather than on a fixed timer.

Files changed:

- [src/station_utils.cpp](src/station_utils.cpp), [include/station_utils.h](include/station_utils.h) — object-name-aware `updateLastHeard()`; new `setPendingMessage()`/`getPendingMessage()` transient message state
- [src/query_utils.cpp](src/query_utils.cpp) — captures addressed message text into the pending-message display state
- [src/main.cpp](src/main.cpp) — `updateLastHeard()` now runs before digi/forwarding/query dispatch each RX cycle; line6 construction prefers a pending message over the last-heard summary

---

## 2026-07-02 — Answer queries addressed to the tactical object

APRS station capability queries (see 2026-06-15 entry below) are now also recognized when addressed to the configured tactical object name (`beacons.0.tacticalCallsign`), not just the device's real callsign. `?APRSP` / `?APRS?` addressed to the tactical name reply with the Object Report (as already emitted by `sendBeacon()` when a tactical name is set); all other directed queries (`?APRSD ?APRSH ?APRSL ?APRSS ?APRST ?APRSV ?PING? ?VER`) reply the same way they do for the real callsign. The digipeater's own-address guard was extended to match, so messages addressed to the tactical object also aren't needlessly re-relayed.

Files changed:

- [src/query_utils.cpp](src/query_utils.cpp), [include/query_utils.h](include/query_utils.h) — addressee routing also matches the configured tactical object name
- [src/digi_utils.cpp](src/digi_utils.cpp) — same-station relay guard extended to the tactical object name

---

## 2026-06-15 — Add APRS station capability query support

All device roles (Tracker, iGate, Digipeater) now respond to APRS station capability queries (APRS 1.01 §13). Queries arrive as directed APRS message packets addressed to the device's own callsign, or as undirected broadcasts to the `APRS` / `IGATE` symbolic addresses. Query keywords are matched case-insensitively. Duplicate queries from the same sender are suppressed for 60 seconds. Incoming messages with a sequence number receive an ACK before the response.

Supported queries:

| Query | Addressee | Responded by | Notes |
|---|---|---|---|
| `?APRS?` | `APRS` | All roles | Triggers a position beacon |
| `?APRSD` | Own callsign | All roles | Direct-heard station list |
| `?APRSH <CALL>` | Own callsign | All roles | Has-heard lookup with elapsed time |
| `?APRSL` | Own callsign | All roles | All recently heard stations |
| `?APRSP` | Own callsign | All roles | Position beacon |
| `?APRSS` | Own callsign | All roles | Status text from config |
| `?APRST` / `?PING?` | Own callsign | All roles | Ping reply |
| `?APRSV` / `?VER` | Own callsign | All roles | Firmware version string |
| `?IGATE?` | `IGATE` | iGate only | iGate-online confirmation |

Also adds a digipeater guard: message packets addressed to the device's own callsign are never relayed, preventing IS→RF→IS loops.

Files changed:

- New: [include/query_utils.h](include/query_utils.h), [src/query_utils.cpp](src/query_utils.cpp) — query parsing and dispatch
- [include/station_utils.h](include/station_utils.h), [src/station_utils.cpp](src/station_utils.cpp) — `updateLastHeard()` now takes the full raw packet; 20-slot heard-station ring buffer with direct/digipeated flag; new `getDirectHeardList()`, `getAllHeardList()`, `minutesSinceHeard()` accessors
- [src/digi_utils.cpp](src/digi_utils.cpp) — drop message packets addressed to own callsign before digipeating
- [src/main.cpp](src/main.cpp) — wire `QUERY_Utils::processLoRaPacket()` into the RX dispatch block

---

## 2026-05-03 — Add nRF52 platform layer + Heltec T114 variant (PR-1)

First nRF52840 board lands in the same repo as the ESP32 fleet — Heltec T114
(off-the-shelf nRF52840 + SX1262 + L76K GPS + ST7789 1.14" 240×135 TFT).
Future nRF boards (`LoRanger_V1_nRF`, RAK4631, etc.) become 2-file variant
additions on top of this layer. See [NRF52_PORT_NOTES.md](NRF52_PORT_NOTES.md)
for the design rationale and notes for future maintainers.

Five logical steps, six commits:

- **`9e64f69` — Steps 1+2** (capability flags + nRF platform): introduce
  `HAS_WIFI` / `HAS_NIMBLE` / `HAS_WEB_UI` / `HAS_DISPLAY` in
  [common_settings.ini](common_settings.ini) to gate ESP-only subsystems
  behind `#ifdef`; add `[nrf52_common]` block,
  [variants/heltec_t114/](variants/heltec_t114/), and
  [include/nrf52_shims/](include/nrf52_shims/) headers that intercept
  `<SPIFFS.h>` / `<logger.h>` on nRF builds; wire `#ifdef ARDUINO_ARCH_NRF52`
  arms across 12 source files (SPIFFS↔InternalFS, `ESP.restart()`↔
  `NVIC_SystemReset()`, `Wire/SPI.begin` no-args, `ledc` PWM→`tone()`,
  `gpsSerial`→`Serial1`, `esp_sleep_*`→`SYSTEMOFF`, etc).
- **`ba83b1f` — Step 3**: ST7789 driver path in [src/display.cpp](src/display.cpp)
  behind `HAS_TFT_ST7789` — software SPI on dedicated TFT pins,
  Adafruit_GFX-based, text-only rendering.
- **`247161e` — Step 4**: first-boot embedded defaults. New
  [tools/embed_config.py](tools/embed_config.py) pre-build script reads
  [data/tracker_conf.json](data/tracker_conf.json) and emits a generated
  C-string header that [src/configuration.cpp](src/configuration.cpp) writes
  straight to LittleFS on first boot. ESP32 first-boot path unchanged.
- **`15c1a75` — Step 5**: FCC TX-gate at [src/lora_utils.cpp](src/lora_utils.cpp)
  `sendNewPacket` chokepoint using the existing `APRSPacketLib::checkNocall()`
  validator on the packet's source-callsign field. Active on **both** platforms —
  every TX path (beacon, status, message, telemetry, digipeat, BLE/BT-Classic
  KISS injection) funnels through this single function.
- **`552ae38` — T114 hardware glue**: drive `VEXT_ENABLE` (P0.21) HIGH for
  L76K GPS power; gate battery ADC reads with `ADC_CTRL_PIN` (P0.6) HIGH
  before sample, LOW after.

`heltec_t114` build: flash 40.9 % (333 KB / 815 usable), RAM 32.3 %
(80 KB / 248 usable). UF2-flashable `firmware.zip` produced in
`.pio/build/heltec_t114/`. ESP32 envs unchanged from baseline
(`heltec_wireless_tracker` measured at 46.9 % flash / 17.3 % RAM throughout).

## 2026-05-03 — Enlarge Serial RX buffer so `import` paste survives LoRa stalls

The default ESP32 Arduino `Serial` RX buffer is 256 bytes — about 22 ms of
115200-baud traffic. The main loop ([src/LoRa_APRS_Tracker.cpp](src/LoRa_APRS_Tracker.cpp))
can stall longer than that during LoRa RX/TX, which silently dropped bytes
during a multi-KB `import` paste in the serial setup CLI; with bytes lost,
the brace-balance heuristic never fires and the import appears to hang.

- [src/LoRa_APRS_Tracker.cpp](src/LoRa_APRS_Tracker.cpp) — `Serial.setRxBufferSize(16384)`
  before `Serial.begin()`. Matches `SERIAL_Setup::PASTE_MAX_BYTES`, so any
  legal import paste fits even if the loop hangs the entire transfer.
  Costs 16 KB of the 327 KB heap (≈5 %).

## 2026-05-02 — Add APRS Object reports via per-beacon tactical callsign

New per-beacon `tacticalCallsign` field. When set (≤9 chars), the tracker emits
an APRS Object report (DTI `;`) labeled with that name instead of a Position
report. Source address remains the operator's licensed callsign — the tactical
name is purely the displayed object label, satisfying the "tactical event
support" use case (SAR, ultra-marathons, jetboat racing) without violating FCC
station-ID requirements. Empty value preserves today's position-report behavior.
Mic-E is silently bypassed when tactical is set (it can't carry an object name).

- [lib/APRSPacketLib/](lib/APRSPacketLib/) — APRSPacketLib forked locally
  (`1.0.4-loranger`); registry pin in `common_settings.ini` removed.
- [lib/APRSPacketLib/src/APRSPacketLib.cpp](lib/APRSPacketLib/src/APRSPacketLib.cpp),
  [lib/APRSPacketLib/include/APRSPacketLib.h](lib/APRSPacketLib/include/APRSPacketLib.h) —
  new `generateObjectPacket()` helper, symmetrical with the existing
  `generateBase91GPSBeaconPacket()` / `generateMiceGPSBeaconPacket()`. Pads
  object name to the spec-required 9 chars in one place.
- [include/configuration.h](include/configuration.h),
  [src/configuration.cpp](src/configuration.cpp),
  [data/tracker_conf.json](data/tracker_conf.json) — new
  `tacticalCallsign` field on each beacon, defaults to empty.
- [src/station_utils.cpp](src/station_utils.cpp) — `sendBeacon()` branches on
  tactical-callsign; builds DDHHMMz timestamp from current GPS time.
- [src/serial_setup.cpp](src/serial_setup.cpp) — `beacon tactical <text>` CLI
  command; field shown in `beacon list` output.
- [src/web_utils.cpp](src/web_utils.cpp),
  [data_embed/script.js](data_embed/script.js) — web config form gains a
  tactical-callsign input.
- [SERIAL_SETUP.md](SERIAL_SETUP.md) — beacon command table updated.

## 2026-05-02 — Add user-configurable SmartBeacon profile (index 3 = Custom)

A 4th SmartBeacon profile is now editable at runtime via the serial CLI, so
event-specific cadence/turn settings can be dialed in without recompiling.

- [include/smartbeacon_utils.h](include/smartbeacon_utils.h),
  [src/smartbeacon_utils.cpp](src/smartbeacon_utils.cpp) — array grows from 3
  to 4 (Runner / Bike / Car / **Custom**); `checkSettings()` clamps
  out-of-range values to 0 with a warning instead of crashing; new
  `setCustomValues()` and `profileLabel()` helpers.
- [include/configuration.h](include/configuration.h),
  [src/configuration.cpp](src/configuration.cpp) — new `customSmartBeacon`
  object in `tracker_conf.json` (8 fields: `slowRate`, `slowSpeed`,
  `fastRate`, `fastSpeed`, `minTxDist`, `minDeltaBeacon`, `turnMinDeg`,
  `turnSlope`). Missing keys auto-rewrite on first boot.
- [src/serial_setup.cpp](src/serial_setup.cpp) — new `smartcustom` command
  group (`show`, `slowrate`, `slowspeed`, `fastrate`, `fastspeed`,
  `mintxdist`, `mindelta`, `turnmindeg`, `turnslope`); changes take effect
  live (no save+reboot required to retune). `beacon smartset` now validates
  `0..3` and rejects out-of-range values; `show beacons` annotates the
  profile name (Runner/Bike/Car/Custom). Full reference:
  [SERIAL_SETUP.md](SERIAL_SETUP.md).

## 2026-05-02 — Add `import` / `export` commands to serial setup CLI ([`d56d83b`](../../commit/d56d83b))

[src/serial_setup.cpp](src/serial_setup.cpp) gains two top-level commands for
JSON-level config replication without WiFi or external tooling:

- **`export`** — streams the on-disk `tracker_conf.json` to the terminal between
  `---- BEGIN ----` / `---- END ----` markers.
- **`import`** — accepts a pasted full JSON, auto-detects end of paste via
  string-aware brace balancing, validates (parse + non-empty
  `beacons[0].callsign`), writes to SPIFFS, and reboots. 16 KB buffer cap;
  Ctrl-C aborts cleanly. Refuses to start if there are unsaved CLI edits.

Also: the unused `boolStr` helper was removed to silence a `-Wunused-function`
warning. Full reference: [SERIAL_SETUP.md](SERIAL_SETUP.md).

## 2026-05-02 — `tracker_conf` standard settings for event support ([`032df10`](../../commit/032df10))

Updated the default [data/tracker_conf.json](data/tracker_conf.json) to event-friendly defaults:

- `other.sendCommentAfterXBeacons`: **10 → 4**
- `other.path`: **`WIDE1-1` → `WIDE2-2`**
- `lora[0]` (EU 433.775 MHz): **SF12/125 kHz → SF8/62.5 kHz**

Goal: more frequent comments and broader digi reach during event use, with a faster RF profile on the EU channel.

## 2026-05-02 — Tune Car SmartBeacon `minDeltaBeacon` 12s → 10s ([`9bdebcc`](../../commit/9bdebcc))

[src/smartbeacon_utils.cpp](src/smartbeacon_utils.cpp) — shortens the minimum spacing
between beacons in the Car profile from 12s to 10s.

## 2026-05-02 — Add serial setup CLI for USB config without WiFi AP ([`be37b9f`](../../commit/be37b9f))

New USB-serial command-line interface (115200 baud) for first-flash provisioning,
scripted bulk configuration, or quick edits while tethered — no need to bring up
the `LoRaTracker-AP` web UI.

- New: [include/serial_setup.h](include/serial_setup.h), [src/serial_setup.cpp](src/serial_setup.cpp)
- Wired into [src/LoRa_APRS_Tracker.cpp](src/LoRa_APRS_Tracker.cpp) and [src/configuration.cpp](src/configuration.cpp)
- Logger is paused (errors only) while in setup mode
- Full reference: [SERIAL_SETUP.md](SERIAL_SETUP.md)

## 2026-05-01 — 30-second startup AP window for web config ([`87be561`](../../commit/87be561))

[src/wifi_utils.cpp](src/wifi_utils.cpp) — every boot now opens `LoRaTracker-AP`
for 30 seconds so the web config is reachable without needing `wifiAP.active` or
`NOCALL-7`. If a client connects during the window, the existing 2-minute idle
timeout takes over so config sessions aren't cut off.

## 2026-04-30 — Tune Car SmartBeacon profile ([`4ec4b9f`](../../commit/4ec4b9f))

[src/smartbeacon_utils.cpp](src/smartbeacon_utils.cpp):

- `fastRate`: **60s → 10s**
- `fastSpeed`: **70 → 110 km/h**

Denser beaconing at highway speeds with a higher kick-in threshold.

## 2026-04-30 — Add LoRanger V1 board variant ([`1898fa2`](../../commit/1898fa2))

Initial fork commit. Adds the LoRanger V1 hardware variant — custom ESP32-S3
+ 1 W SX1268 + ATGM336H GPS tracker with hardwired LDO rails.

- New: [variants/LoRanger_V1/board_pinout.h](variants/LoRanger_V1/board_pinout.h),
  [variants/LoRanger_V1/platformio.ini](variants/LoRanger_V1/platformio.ini)
- Touched: [include/configuration.h](include/configuration.h),
  [src/battery_utils.cpp](src/battery_utils.cpp),
  [src/configuration.cpp](src/configuration.cpp),
  [src/lora_utils.cpp](src/lora_utils.cpp)
- Some upstream `LIGHTTRACKER_PLUS_1_0` `#ifdef` branches were broadened to also
  match `LORANGER_V1`; others were intentionally left alone.

---

## How to update this file

When you add a fork commit, prepend a new section above using:

```
## YYYY-MM-DD — Short title ([`d56d83b`](../../commit/d56d83b))

One- or two-paragraph description: what changed, which files, and why.
```

The `../../commit/<hash>` link resolves on GitHub from any branch view of this file.
