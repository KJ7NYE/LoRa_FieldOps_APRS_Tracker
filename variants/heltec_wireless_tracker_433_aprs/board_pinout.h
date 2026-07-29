/* Copyright (C) 2025 Ricardo Guzman - CA2RXU
 * Portions Copyright (C) 2026 KJ7NYE
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

/* Heltec Wireless Tracker V1.1 (433 MHz) — 433 MHz APRS multi-role build.
 *
 * MCU:     ESP32-S3FN8 (8 MB flash, no PSRAM)
 * Radio:   SX1262
 * GNSS:    onboard UC6580 (multi-GNSS: GPS/GLONASS/BDS/Galileo/NAVIC/QZSS)
 *          on UART2; VEXT_CTRL (GPIO3) gates power to both GNSS and TFT.
 * Display: 0.96" 160x80 ST7735 TFT (SPI) via TFT_eSPI; backlight on GPIO21.
 * Battery: ADC divider gated by ADC_CTRL (GPIO2, active HIGH) so it doesn't
 *          bleed current continuously — see POWER_Utils::adc_ctrl_ON/OFF.
 *
 * The HELTEC_WIRELESS_TRACKER platform macro (set via build_flags) is
 * already wired throughout display.cpp / power_utils.cpp / battery_utils.cpp
 * upstream — this file only supplies the pin map.
 *
 * GNSS reset (GPIO35) and 1PPS (GPIO36) are wired on the board but not
 * currently used by this firmware.
 *
 * Pinout verified against the upstream richonguzman/LoRa_APRS_Tracker
 * heltec_wireless_tracker variant and the Zephyr heltec_wireless_tracker
 * board support package (both independently confirm the same pin map).
 */

#ifndef BOARD_PINOUT_H_
#define BOARD_PINOUT_H_

    //  LoRa Radio: SX1262
    #define HAS_SX1262
    #define RADIO_SCLK_PIN      9
    #define RADIO_MISO_PIN      11
    #define RADIO_MOSI_PIN      10
    #define RADIO_CS_PIN        8
    #define RADIO_RST_PIN       12
    #define RADIO_DIO1_PIN      14
    #define RADIO_BUSY_PIN      13

    //  Display: 0.96" 160x80 ST7735 TFT via TFT_eSPI (HAS_TFT); driver
    //  selection and pin wiring for TFT_eSPI itself live in platformio.ini
    //  build_flags (USER_SETUP_LOADED path) since TFT_eSPI is configured
    //  entirely at compile time.
    #undef  OLED_SDA
    #undef  OLED_SCL
    #undef  OLED_RST
    #define HAS_TFT
    #define BOARD_BL_PIN        21

    //  GPS: onboard UC6580 GNSS on UART2
    #define HAS_GPS_CTRL
    #define GPS_RX              34
    #define GPS_TX              33
    #define GPS_BAUDRATE        115200

    //  I/O
    #define BUTTON_PIN          0
    #define BATTERY_PIN         1
    #define ADC_CTRL            2   // Drive HIGH to enable VBAT divider
    #define VEXT_CTRL           3   // Powers GNSS + TFT (active HIGH)

    //  External I2C (Qwiic/Grove header — BME280 etc.); display is SPI, not I2C.
    #define BOARD_I2C_SDA       7
    #define BOARD_I2C_SCL       6

    //  Capability flags
    //  HAS_WIFI, HAS_NIMBLE, HAS_WEB_UI, HAS_DISPLAY come from
    //  common_settings.ini [common]. HAS_BT_CLASSIC intentionally omitted
    //  (ESP32-S3 has no BT Classic radio).

#endif
