# LoRanger Multi-Platform Support Notes

Handoff document covering ESP32 work that landed and the design decisions for an nRF52840 port. Drop into a future chat to continue without re-deriving context.

> **Status: historical design/handoff doc.** PR-1 (nRF52 platform + Heltec T114) fully landed — see CHANGELOG.md's 2026-05-03 entry. Sections below describe the state and reasoning at design/landing time; known-stale spots (content that never matched what actually shipped, or work that has since shipped) have been corrected or removed inline rather than left to rot.

---

## What landed (ESP32 build, currently shipped)

### `import` / `export` commands in serial setup CLI

Two new top-level commands in [src/serial_setup.cpp](src/serial_setup.cpp), documented in [SERIAL_SETUP.md](SERIAL_SETUP.md):

- **`export`** — streams the on-disk `tracker_conf.json` to the terminal between `---- BEGIN ----` / `---- END ----` markers.
- **`import`** — accepts a pasted full JSON, auto-detects end via string-aware brace balancing, validates (parse + non-empty `beacons[0].callsign`), writes to SPIFFS, reboots on success. 16 KB cap; Ctrl-C aborts; refuses to start if there are unsaved CLI edits.

Together they form a backup/restore + device-cloning workflow that doesn't need WiFi or external tooling. Round-trips canonicalize via ArduinoJson (whitespace and unknown fields stripped).

Commits:
- `d56d83b` — feature (code + docs)
- `30a4bdb` — CHANGELOG entry referencing `d56d83b`

Build verified for `LoRanger_V1` (ESP32-S3): 45.7% flash, 17.1% RAM, no warnings. The `boolStr` dead helper was removed in the same commit to silence a pre-existing `-Wunused-function`.

### Why this matters for the multi-platform discussion

`import` collapses what would otherwise be a major nRF52-port concern (bulk config provisioning) into existing portable code. Combined with the existing per-field setters, a serial-only LoRanger can do everything the web UI does — and the same CLI ports nearly free to nRF52.

---

## What landed (PR-1: nRF52 platform + Heltec T114)

Five-step PR landed as four commits. Each step is a green-build checkpoint on its own.

| Commit | Step | What |
|---|---|---|
| `9e64f69` | 1 + 2 | ESP32 capability flags (`HAS_WIFI` / `HAS_NIMBLE` / `HAS_WEB_UI` / `HAS_DISPLAY` in `[common].build_flags`) gating ESP-only subsystems behind `#ifdef`; new `[nrf52_common]` block in [common_settings.ini](common_settings.ini); [variants/heltec_t114/](variants/heltec_t114/); header shims under [include/nrf52_shims/](include/nrf52_shims/) intercepting `<SPIFFS.h>` and `<logger.h>` on nRF builds; `#ifdef ARDUINO_ARCH_NRF52` arms across 12 source files for SPIFFS↔InternalFS, ESP.restart↔NVIC_SystemReset, Wire/SPI.begin no-args, ledc PWM→`tone()`, gpsSerial→Serial1 macro, esp_sleep→SYSTEMOFF, etc. |
| `ba83b1f` | 3 | ST7789 driver path in [src/display.cpp](src/display.cpp) behind `HAS_TFT_ST7789`. Hardware SPI on a second SPIM peripheral (`SPIClass SPI1` on `NRF_SPIM2`, since the primary `SPI` bus is owned by the SX1262 radio); Adafruit_GFX-based; text-only rendering. |
| `247161e` | 4 | First-boot embedded defaults: [tools/embed_config.py](tools/embed_config.py) pre-build script reads [data/tracker_conf.json](data/tracker_conf.json) and emits `include/generated/default_config_embed.h` (gitignored). [src/configuration.cpp](src/configuration.cpp) writes `DEFAULT_CONFIG_JSON` straight to LittleFS on first boot; ESP32 path unchanged. |
| `15c1a75` | 5 | FCC TX-gate at [src/lora_utils.cpp](src/lora_utils.cpp) `sendNewPacket` chokepoint, using existing `APRSPacketLib::checkNocall()` validator on the source-callsign field. Active on both platforms. |

**T114 build (`heltec_t114` env, fresh from clean `pio run`):** Flash 40.9% (333 KB / 815), RAM 32.3% (80 KB / 248). UF2-flashable `firmware.zip` produced. Display body active, embedded defaults baked in, TX-gate enforcing.

**ESP32 envs unchanged** — `heltec_v3_433_aprs` measured at 46.9% / 17.3% throughout PR-1, identical to pre-PR baseline.

### Notes for future maintainers

- **PIO LDF discovery quirk** — LDF doesn't crawl `#include`s reached via custom `-I` paths (like `include/nrf52_shims/`). Worked around by an explicit `#include <Adafruit_LittleFS.h>` in [src/configuration.cpp](src/configuration.cpp) gated on nRF, plus `lib_extra_dirs = ${platformio.packages_dir}/framework-arduinoadafruitnrf52/libraries` in the variant ini. If a future shim needs another BSP-bundled lib, repeat that pattern.
- **`-Wno-sign-compare` on nRF** — gcc-arm-none-eabi flags signed/unsigned comparisons that gcc-xtensa lets pass. Many `millis()/uint32_t vs signed-int * 1000` and `int i; i < container.size()` sites. Cleaning these up is its own follow-up that benefits both platforms.
- **Hardware SPI for the T114 ST7789** — uses a second hardware SPI peripheral (`SPIClass SPI1` on `NRF_SPIM2`) rather than sharing the primary `SPI` bus the LoRa SX1262 owns. Adequate for text-only updates; revisit driver/layout choices if rich UI lands.
- **Commit message style** — see `feedback_commit_style.md` in memory: omit `Co-Authored-By: Claude` trailer.

---

## Design decisions for nRF52840 support

### Hardware targets

**Immediate — Heltec T114 (daily-driver bring-up board):**
- nRF52840 MCU (Adafruit nRF52 BSP, UF2 bootloader)
- SX1262 1W LoRa radio (built-in; *not* the SX1268 of LoRanger V1, but RadioLib supports both)
- L76K GPS (NMEA-compatible, parses via TinyGPSPlus)
- ST7789 1.14" TFT 240×135 display (built-in; *not* SSD1306, and *not* the ST7735 80×160 the earlier draft of this doc claimed — pinmap and driver cross-checked against meshtastic's variant.h for this board)
- Battery monitor on built-in divider
- Button + USR LED
- USB-C

The T114 mirrors `heltec_v3_433_aprs`'s role on the ESP32 side: an off-the-shelf integrated tracker for firmware iteration.

**Long-term — `LoRanger_V1_nRF` (custom PCB):**

Same hardware concept as the ESP32 LoRanger V1 (ATGM336H GPS + EBYTE E22-400M30S SX1268 radio + SSD1306 OLED + button + 100k/100k battery divider) but with **nRF52840 MCU** instead of ESP32-S3-WROOM-1-N8. Once the nRF52 platform layer and T114 variant land, this is a 2-file variant addition (`platformio.ini` + `board_pinout.h`).

### Layered architecture

The work splits into two layers, with the bigger investment going into the platform layer so future nRF52 boards (LoRanger V1 nRF, RAK4631, Adafruit Feather, …) drop in as ~2-file variants:

**Platform layer (built once, used by all nRF52 boards):**
- `[nrf52_common]` block in [common_settings.ini](common_settings.ini) — `platform = nordicnrf52`, Adafruit nRF52 BSP, InternalFileSystem, RadioLib subset, Adafruit_GFX
- Capability macros: `HAS_WIFI`, `HAS_BT_CLASSIC`, `HAS_NIMBLE`, `HAS_WEB_UI`, `DISPLAY_DRIVER_*` — nRF52 platform turns ESP-only ones off; ESP32 sets per-board (matches existing partial gating)
- Per-env `build_src_filter` template excluding [src/wifi_utils.cpp](src/wifi_utils.cpp), [src/web_utils.cpp](src/web_utils.cpp), [src/bluetooth_utils.cpp](src/bluetooth_utils.cpp), [src/ble_utils.cpp](src/ble_utils.cpp) for any nRF52 env
- `#ifdef ARDUINO_ARCH_NRF52` arms in the ~7 files identified below — keeping the doc's "no HAL abstraction layer" rule intact (capability flags ≠ HAL — they're feature gates, not new abstraction headers)
- First-boot writer + `tools/embed_config.py` pre-build script (see "Defaults source" below)
- TX-gate on callsign validity — added to *both* platforms (FCC-correctness on ESP32 too)

**Per-board variant layer (~2 files per board):**
- `variants/<name>/platformio.ini` — board, freq, lib overrides
- `variants/<name>/board_pinout.h` — pin map

After the platform layer lands, adding any future nRF52 board is just dropping a `variants/<name>/` dir.

### Decision matrix

| Question | Decision | Why |
|---|---|---|
| Config UX | Serial CLI (existing) + new `import` for bulk | Users are FCC-licensed amateur radio operators — terminal tools (CHIRP, hamlib) are below the bar, no need for non-technical-user UX. |
| Web UI | **Delete entirely** on nRF | nRF52840 has no WiFi. AsyncTCP / ESPAsyncWebServer are ESP32-only. |
| Subsystem capability flags | `HAS_WIFI`, `HAS_BT_CLASSIC`, `HAS_NIMBLE`, `HAS_WEB_UI` macros (plus `DISPLAY_DRIVER_*`) | Avoids per-variant `defined(HELTEC_T114) \|\| defined(LORANGER_V1_NRF) \|\| …` chains. New nRF52 boards drop in clean. |
| MSC drag-drop drive | **Rejected** | Requires a QSPI flash chip (BOM addition), USB composite stack complexity, FAT-vs-LittleFS incoherence. The `import` command covers all the same use cases without new hardware. |
| Defaults source | **Project-level [data/tracker_conf.json](data/tracker_conf.json)** (single source of truth, already exists) + new pre-build script `tools/embed_config.py` that emits a generated `DEFAULT_CONFIG_JSON` C string header for first-boot use | Matches the existing ESP32 `data/tracker_conf.json + uploadfs` convention — *no per-variant JSON files*. nRF52 has no clean PIO `uploadfs` equivalent for InternalFS, so first-boot writes the generated header to LittleFS, reboots, runs normally. `setDefaultValues()` C++ floor still backstops missing keys. ESP32 build can also use the embedded path as a fallback if `uploadfs` was skipped. |
| Display driver | T114 ships ST7789 1.14" 240×135 TFT; LoRanger V1 nRF ships SSD1306. [src/display.cpp](src/display.cpp) body is gated behind `HAS_DISPLAY` (defined for ESP32 in `[common]`, undefined on nRF) with no-op stubs for headless variants. ST7789 driver path lands as its own step; layout/render code shared via Adafruit GFX. | T114 boots headless to USB serial during platform bring-up; display added in a follow-up step (see PR sequencing). Display driver polymorphic over hardware, not over board. |
| Repo structure | **One repo, variant pattern** | Deletions of WiFi/web/BT-Classic code are cheap (already partially `#ifdef`-gated). Per-env `build_src_filter` excludes those .cpp files entirely from nRF builds. Two repos would mean every shared-logic fix (e.g., the `import` command) needs porting both ways forever. |
| Config replication across devices | `export` → copy → `import` | Already works; same path on both platforms. |
| Filesystem image at flash time | **Skip** | First-boot writes embedded defaults to LittleFS. PlatformIO doesn't have a clean `uploadfs` equivalent for nRF52 + Adafruit BSP. |
| FCC compliance | TX-gate at the [src/lora_utils.cpp](src/lora_utils.cpp) `sendNewPacket` chokepoint, using the existing `APRSPacketLib::checkNocall()` validator on the source-callsign field of the packet. Lands as part of the nRF platform PR (where boot-with-defaults makes it load-bearing); covers ESP32 for free since the chokepoint is shared. | Boot-with-defaults is fine *if and only if* transmit is blocked until callsign is valid. Boot/RX/display all run; carrier never keys until configured. |

### Concrete port scope

Files needing changes for the nRF52 platform layer + T114 variant. This table
is a snapshot of the original plan — some filenames have since changed
(e.g. the entry point was `src/LoRa_APRS_Tracker.cpp` at design time, now
`src/main.cpp`; `src/msg_utils.cpp` no longer exists under that name), so
treat exact paths/line numbers here as historical rather than current:

| File | Change | Effort |
|---|---|---|
| [src/configuration.cpp](src/configuration.cpp) | `<SPIFFS.h>` ↔ `<InternalFileSystem.h>`, `SPIFFS.` ↔ `InternalFS.` (3 sites). New first-boot writer that pulls defaults from generated `default_config_embed.h` if no config on disk. | ~20 `#ifdef` lines + ~30-line first-boot writer |
| [src/serial_setup.cpp](src/serial_setup.cpp) | Same SPIFFS swap; `ESP.restart()` ↔ `NVIC_SystemReset()` (2 sites) | ~6 `#ifdef` lines |
| [src/msg_utils.cpp](src/msg_utils.cpp), [src/station_utils.cpp](src/station_utils.cpp) | SPIFFS swap | ~4 lines each |
| [src/power_utils.cpp:462-464](src/power_utils.cpp#L462-L464) | `esp_sleep_*` block → `NRF_POWER->SYSTEMOFF` or `sd_power_system_off()` | 5 lines |
| `src/main.cpp` (entry point, formerly `LoRa_APRS_Tracker.cpp`) | Wrap `<WiFi.h>` and `<BluetoothSerial.h>` in `#ifdef HAS_WIFI` / `#ifdef HAS_BT_CLASSIC` (some already half-gated) | ~6 lines |
| [src/display.cpp](src/display.cpp) | Step 2: gated body behind `HAS_DISPLAY`, added no-op stubs + `screenBrightness` / `symbolAvailable` globals so other TUs link. Step 3 (next): ST7789 driver path, Adafruit ST7789 lib added to nRF52 lib_deps, layout reused via Adafruit GFX with scale tweaks (240×135 vs 128×64). | Step 2: ~25 lines. Step 3: ~30-50 lines additional. |
| [src/battery_utils.cpp](src/battery_utils.cpp) | nRF52840 ADC: 12-bit at 0.6 V Vref × gain. Different scale factor than ESP32. **Tune via existing `lora32BatReadingCorr` config — do not edit the formula** (per durable memory). | new `#ifdef ARDUINO_ARCH_NRF52` branch with different starting constant |
| New: `tools/embed_config.py` | Reads [data/tracker_conf.json](data/tracker_conf.json), emits `default_config_embed.h` with `static const char DEFAULT_CONFIG_JSON[] = R"json(...)json";` for first-boot use. Wired into [platformio.ini](platformio.ini) `extra_scripts` for nRF52 envs (and optionally ESP32 as a `uploadfs`-skipped fallback). | ~30 lines |
| [common_settings.ini](common_settings.ini) | Conditional `lib_deps` per platform | new `[nrf52_common]` block |
| [platformio.ini](platformio.ini) | New `[env:nrf52_base]` + per-env `build_src_filter` to exclude wifi/web/bt files | ~15 lines |
| `variants/heltec_t114/` (immediate), `variants/LoRanger_V1_nRF/` (later) | New variant dirs: `platformio.ini`, `board_pinout.h`. **No per-variant `default_config.h`** — defaults come from project-level `data/tracker_conf.json` via the pre-build script. | ~2 files per variant, ~50-80 lines each |

The `import` and `export` commands ported in [src/serial_setup.cpp](src/serial_setup.cpp) require zero code changes other than the SPIFFS shim. The string-aware brace counter, validation gates, and reboot path are all platform-neutral.

### What NOT to extend to nRF52 (per existing memory on LightTracker flag-split)

Per [memory/project_lighttracker_flag_split.md](memory/project_lighttracker_flag_split.md), `LIGHTTRACKER_PLUS_1_0` historically gated four unrelated behaviors. LoRanger V1 reuses only the FSPI radio constructor branch. For nRF52 LoRanger:

- **Reuse**: SPI radio init, APRS frame builder, SmartBeacon math, GPS parsing, button handling, battery formula structure, display layout/render code via Adafruit GFX (driver differs SSD1306 vs ST7789 — GFX calls don't).
- **Don't extend**: `RADIO_VCC_PIN` / `GPS_VCC` / SHTC3 / 560k-100k divider branches — same reasoning as for the ESP32 LoRanger V1 (LDOs hardwired, no SHTC3, 100k/100k divider).

### nRF52840 resource picture (post-strip)

- **Flash**: 1 MB internal. SoftDevice s140 ≈ 156 KB. After stripping WiFi/web/BT-Classic/NimBLE/embed_files (~hundreds of KB on the ESP32 build), the remaining firmware (RadioLib, TinyGPSPlus, Adafruit GFX + SSD1306 *or* ST7789, ArduinoJson, app code) fits comfortably. **Measured Step 2 (headless T114):** Flash 39.2% (319 KB / 815 usable), RAM 32.2% (80 KB / 248 usable).
- **RAM**: 256 KB. Adequate for a tracker without TCP/IP stack.
- **No external QSPI required** because we rejected MSC. LittleFS lives in the InternalFS region at top of internal flash.

---

## Future PRs

PR-1 (Steps 1-5) is shipped — see "What landed (PR-1)" above. Since then, two
items originally listed here have also shipped:

- ~~**T114 hardware glue**~~ **Done** — commit `552ae38` drives `VEXT_ENABLE` (P0.21) HIGH for L76K GPS power and gates battery ADC reads with `ADC_CTRL_PIN` (P0.6), per the 2026-05-03 CHANGELOG entry.
- ~~**Hardware SPI for the T114 TFT**~~ **Done** — landed as part of PR-1 Step 3 (`ba83b1f`); the ST7789 driver already uses a second hardware SPI peripheral (`SPIClass SPI1` on `NRF_SPIM2`), not software/bitbang SPI.

Remaining work:

- **Additional nRF52 boards** (`LoRanger_V1_nRF`, RAK4631, Adafruit Feather, …) — 2-file variant-only PRs (`platformio.ini` + `board_pinout.h`) on top of the platform layer. No shared-code touches expected. Not started — no such variant directories exist yet.
- **Rich UI on T114 ST7789** — battery / GPS-fix / signal-bar icons. The legacy SSD1306 path uses driver-specific helpers that don't carry over; T114 would need GFX-based versions. Defer until there's a real reason.
- **`-Wno-sign-compare` cleanup** — fix the underlying signed/unsigned mismatches the codebase has accumulated. Benefits both platforms; would let nRF builds drop the warning suppression. Still present in `common_settings.ini`'s `[nrf52_common]` block — not yet cleaned up.

---

## Things explicitly considered and rejected

1. **MSC drag-drop config drive** — slick UX but redundant with `import`, requires QSPI chip BOM addition, FAT/LittleFS incoherence concerns, no clear value-add for FCC-licensed users who handle terminals fine.
2. **Two repos** — divergence is shallow now (~25 `#ifdef` lines + a few deleted files via `build_src_filter`). Two-repo overhead would be cross-porting every shared-logic improvement forever.
3. **HAL abstraction layer (`platform/storage.h` etc.)** — clean in theory, but breaks the upstream-merge story with CA2RXU's repo. Refactoring shared files for HAL adds merge conflicts at every upstream pull. (Note: the capability flags `HAS_WIFI` etc. are *not* HAL — they're feature gates, no new abstraction headers.)
4. **PlatformIO `uploadfs` equivalent for nRF** — `mklittlefs` exists but flash-side tooling is rough; UF2 bootloader doesn't have a clean partition handoff for it. First-boot embedded-JSON write achieves the same goal with simpler tooling.
5. **BLE-based config** — would require Adafruit Bluefruit BLE stack and a phone app. Out of scope for serial-CLI design.
6. **Per-variant `default_config.h`** — would diverge from the existing single-source-of-truth `data/tracker_conf.json` ESP32 convention. Replaced with project-level JSON + pre-build script that generates the embedded header at build time.
7. ~~**T114 headless first** — considered, rejected.~~ **Reversed in practice.** Splitting PR-1 into stepwise green-build commits made headless-first practical: the platform layer (Step 2) lands without a working display, USB serial gives enough bring-up debug visibility, and the ST7789 driver becomes its own focused follow-up (Step 3) instead of bloating the platform commit.
8. **Standalone ESP32-only TX-gate PR** — considered, dropped. Existing soft-warning + auto-rightArrow UI in `src/main.cpp` (formerly `LoRa_APRS_Tracker.cpp`) is sufficient on a shipping ESP32 build that's past initial config. The hard gate is load-bearing only with boot-with-defaults; it travels with PR-1.

---

## Open questions / not yet decided

- ~~**Whether to actually do the port at all.**~~ **Resolved by doing it** — PR-1 landed (commits `9e64f69`, `ba83b1f`, `247161e`, `15c1a75`).
- ~~**FCC TX-gate** — design agreed, implementation deferred to PR-1.~~ **Done in `15c1a75`** at [src/lora_utils.cpp](src/lora_utils.cpp) `sendNewPacket`.
- **T114 board definition source** — currently using `board = adafruit_feather_nrf52840` for the SoC + bootloader / flash layout, with T114-specific pins overridden via [variants/heltec_t114/board_pinout.h](variants/heltec_t114/board_pinout.h). This compiles and links; whether the Adafruit Feather flash layout is *flash-time* compatible with whatever bootloader Heltec ships on the T114 needs hardware verification. Custom board JSON is the fallback if it isn't.
- **Repo structure when port begins** — settled: `variants/heltec_t114/` lives alongside the 40+ existing variants. No shared-code restructuring needed at current `#ifdef` density.

---

## File pointers for the next conversation

- [src/serial_setup.cpp](src/serial_setup.cpp) — the import/export implementation; ports nearly free to nRF52.
- [src/configuration.cpp](src/configuration.cpp) — SPIFFS read/write path; primary shim site; first-boot writer lives here.
- [src/display.cpp](src/display.cpp) — display driver path; body gated behind `HAS_DISPLAY` (Step 2). Step 3 adds ST7789 path for T114.
- [data/tracker_conf.json](data/tracker_conf.json) — single source of truth for default config (both platforms).
- *New:* `tools/embed_config.py` — pre-build script that emits `default_config_embed.h` from the JSON.
- [variants/heltec_v3_433_aprs/](variants/heltec_v3_433_aprs/) — closest existing template for an off-the-shelf integrated tracker variant.
- [variants/LoRanger_V1/](variants/LoRanger_V1/) — template for the eventual `LoRanger_V1_nRF` variant.
- [SERIAL_SETUP.md](SERIAL_SETUP.md) — user-facing CLI reference, includes the new commands and the cloning example.
- [CHANGELOG.md](CHANGELOG.md) — fork-overlay history.
- [common_settings.ini](common_settings.ini) — current ESP32-only lib_deps; will need conditional sections.
- `~/.claude/projects/c--GitHub-LoRanger-APRS-Tracker/memory/project_lighttracker_flag_split.md` — flag-split rules to reapply for nRF.

## Memory-relevant constraints

- **Battery formula must not be edited** — tune via `lora32BatReadingCorr` config field. (Reason: ADC noise on ESP32-S3 was originally fine-tuned this way; same approach applies to nRF52 with a different starting constant.)
- **Commit messages omit `Co-Authored-By: Claude` trailer** — durable preference.
- **Active dev/test board (ESP32 side) is `heltec_v3_433_aprs`** — daily flashing happens there, not LoRanger_V1 (which is still being built). On the nRF52 side, **`heltec_t114` plays the same role** — the off-the-shelf bring-up board, not the future custom `LoRanger_V1_nRF`.
