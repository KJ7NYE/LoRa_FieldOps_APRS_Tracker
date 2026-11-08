/* Copyright (C) 2025 Ricardo Guzman - CA2RXU
 * Portions Copyright (C) 2026 Steve (KJ7NYE)
 *
 * This file is part of LoRa APRS Tracker.
 *
 * LoRa APRS Tracker is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LoRa APRS Tracker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LoRa APRS Tracker. If not, see <https://www.gnu.org/licenses/>.
 */

/* LoRanger V2 (KJ7NYE)
 * Redesign of V1 with a stackable power-board carrier (BQ25798 charger with
 * built-in battery telemetry over I2C), accelerometer, and microSD.
 * Not pin-compatible with V1 — gated by the LORANGER_V2 build flag.
 *
 * MCU:     ESP32-S3-WROOM-1-N8 (8 MB flash, no PSRAM)
 * Radio:   EBYTE E22-400M30S (SX1262 silicon, 1 W; firmware-driven RF switch)
 * GPS:     ATGM336H on a gated rail (IO36)
 * Display: none fitted by default (J2 can accept a 0.96" 128x64 I2C OLED)
 *
 * Hardware source of truth: TRACKER.md, POWER.md and the 2026-10-31 netlist
 * in https://github.com/KJ7NYE/LoRanger.
 */

#ifndef BOARD_PINOUT_H_
#define BOARD_PINOUT_H_

    //  LoRa Radio (SPI)
    //  E22-400M30S ships with SX1262 silicon despite the SX1268 module-naming
    //  convention (confirmed via RadioLib VERSION_STRING readback on V1; same
    //  module family here). DIO2 is not wired — firmware drives RXEN/TXEN
    //  directly for the T/R switch.
    #define HAS_SX1262
    #define HAS_1W_LORA
    #define RADIO_SCLK_PIN      14
    #define RADIO_MISO_PIN      12      // POCI
    #define RADIO_MOSI_PIN      13      // PICO
    #define RADIO_CS_PIN        21      // NSS
    #define RADIO_RST_PIN       11
    #define RADIO_DIO1_PIN      9
    #define RADIO_BUSY_PIN      10
    #define RADIO_RXEN          47
    #define RADIO_TXEN          48
    // RADIO_VCC omitted — LoRa rail is hardwired-on via TLV75733 LDO.

    //  I2C — main bus (ESP32-S3 side of the TCA9517 isolator)
    //  Hosts the STK8328-C accelerometer and (on ID1 carriers) the BQ25798
    //  power board — both on the MCU rail, always reachable. Qwiic/J2 add-ons
    //  sit behind the isolator and only appear on-bus when QWIIC_VCC is raised.
    //  The fork's canonical names for this bus are OLED_SDA/OLED_SCL (every
    //  variant defines them, even headless ones) — Stage 3-5 driver code for
    //  the accel/BQ25798 will reference those same macros rather than
    //  introduce I2C_SDA/I2C_SCL aliases, which collide with power_utils.cpp's
    //  AXP-PMU bus defines elsewhere in the codebase.
    #undef  OLED_SDA
    #undef  OLED_SCL
    #undef  OLED_RST

    #define OLED_SDA            40
    #define OLED_SCL            41
    #define OLED_RST            -1

    //  GPS (UART) — ATGM336H on a gated rail
    //  Rail enable is GPS_VCC (IO36, 100 kΩ pulldown → off at POR).
    //  Stage 0 raises GPS_VCC in setup() and leaves it on, matching V1.
    //  Stage 1 (HAS_RAIL_SWITCHING) adds the shutoff ordering (drive UART TX
    //  low/input before dropping the rail, to stop back-powering through RXD).
    #define GPS_RX              39      // ESP RX  (receives from GPS TX)
    #define GPS_TX              38      // ESP TX  (drives GPS RX)
    #define GPS_VCC             36      // rail enable

    //  microSD (SPI) — Stage 5 (HAS_SDCARD)
    //  Rail-off rule: tri-state SD_SCK/MOSI/MISO/CS before dropping SD_VCC,
    //  and hold the rail off ≥0.5 s before re-enabling (10 kΩ bleeder ~0.1 s τ).
    #define SD_SCK              15
    #define SD_MOSI             7       // CMD   (DAT connector numbering)
    #define SD_MISO             16      // DAT0
    #define SD_CS               6       // DAT3
    #define SD_CD               17      // card detect, pulled up to 3.3 V
    #define SD_VCC              18      // rail enable, 100 kΩ pulldown

    //  Accelerometer — STK8328-C — Stage 4 (HAS_ACCEL)
    //  Address selected by JP1 (default SA0 low). Both addresses from the
    //  datasheet; confirm with an I2C scan on first bring-up.
    //  INT1 is RTC-capable for ext0/ext1 deep-sleep wake.
    #define ACCEL_INT1          2       // wake-on-motion (RTC)
    #define ACCEL_INT2          42      // inactivity

    //  CARRIER_ID (ADC1) — Stage 2 (HAS_CARRIER_ID)
    //  10 kΩ 1% pull-up to 3.3 V, 10 nF at the pin; each power board fits one
    //  resistor to GND to encode its identity. Decoded at boot into IDn and
    //  surfaced via ?version/show.
    //  IO3 is also a strapping pin (JTAG source, eFuse-dependent). The 10 kΩ
    //  pull-up keeps it high at boot across all defined CARRIER_ID states
    //  (power-board resistors divide but never pull to the strap threshold),
    //  so JTAG routing stays deterministic — do not add a stronger pulldown.
    #define CARRIER_ID_PIN      3

    //  Power-button sense — Stage 3 (HAS_POWER_BOARD)
    //  SW2 asserts both /QON (on the power board) and this pin through a
    //  BAT54C. 10 kΩ pull-up + 1 µF at the pin; RTC-capable for sleep wake.
    //  NEVER drive this pin as an output — it is diode-OR'd with the power
    //  board's own power-on signal (J3 pin 12). Input-only.
    #define PWR_BTN_SENSE       4

    //  Qwiic / J2 expansion rail — Stage 1 (HAS_RAIL_SWITCHING)
    //  Behind a TCA9517 isolator — the main I2C bus (accelerometer, power
    //  board) keeps working with this rail off. Raise only when a configured
    //  add-on on J1/J2 needs it.
    #define QWIIC_VCC           37

    //  User button — SW3 (shares the BOOT strap on IO0). Same role as V1.
    #define BUTTON_PIN          0

    //  No BATTERY_PIN on V2 — there is no ADC divider on the tracker. Pack
    //  telemetry comes from the BQ25798 on the power board via I2C
    //  (Stage 3, HAS_POWER_BOARD). Until that lands, battery_utils.cpp's
    //  default path falls through to return 0.0.

#endif
