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

/* LoRanger V1 (KJ7NYE)
 * Derived from LightTracker Plus 1.0 (lightaprs/QRP Labs)
 * MCU: ESP32-S3-WROOM-1-N8
 * Radio: EBYTE E22-400M30S (SX1268, 1W)
 * GPS: ATGM336H-5N31 (hardwired 3.3V, no power control)
 * See: https://github.com/KJ7NYE/LoRanger
 */

#ifndef BOARD_PINOUT_H_
#define BOARD_PINOUT_H_

    //  LoRa Radio (FSPI)
    //  PICO = GPIO11, POCI = GPIO13
    //  EBYTE E22-400M30S modules ship with SX1262 silicon despite the SX1268
    //  module-naming convention — confirmed via VERSION_STRING readback during
    //  bring-up ("SX1261 V2D 2D02"). Use the SX1262 RadioLib driver.
    #define HAS_SX1262
    #define HAS_1W_LORA
    #define RADIO_SCLK_PIN      12
    #define RADIO_MISO_PIN      13      // POCI
    #define RADIO_MOSI_PIN      11      // PICO
    #define RADIO_CS_PIN        10
    #define RADIO_RST_PIN       9
    #define RADIO_DIO1_PIN      5
    #define RADIO_BUSY_PIN      6
    #define RADIO_RXEN          42
    #define RADIO_TXEN          14
    // Note: RADIO_VCC_PIN omitted — LoRa rail hardwired via TLV75733 LDO

    //  Display (I2C)
    #undef  OLED_SDA
    #undef  OLED_SCL
    #undef  OLED_RST

    #define OLED_SDA            3
    #define OLED_SCL            4
    #define OLED_RST            -1

    //  GPS (UART)
    //  Hardwired to GPS LDO 3.3V rail — no power control pin
    #define GPS_RX              17
    #define GPS_TX              18
    // Note: GPS_VCC omitted — GPS rail hardwired via TLV75733 LDO

    //  Other
    #define BUTTON_PIN          0
    #define BATTERY_PIN         1

#endif
