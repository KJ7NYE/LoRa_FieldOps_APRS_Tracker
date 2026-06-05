/* LilyGo TTGO T3 V1.6 — 433 MHz APRS (iGate / Digipeater / TNC)
 *
 * ESP32 WROOM + SX1278 (433 MHz) + 0.96" SSD1306 OLED.
 * No onboard GPS. No dedicated USR button on this revision.
 * WiFi + NimBLE available via common build flags.
 *
 * Key difference from T-LoRa32 V2.1: RADIO_RST_PIN is 14, not 23.
 */

#ifndef BOARD_PINOUT_H_
#define BOARD_PINOUT_H_

    //  LoRa Radio (SX1278 — 433 MHz)
    #define HAS_SX1278
    #define RADIO_SCLK_PIN      5
    #define RADIO_MISO_PIN      19
    #define RADIO_MOSI_PIN      27
    #define RADIO_CS_PIN        18
    #define RADIO_RST_PIN       14     // T3-specific — T-LoRa32 uses 23
    #define RADIO_BUSY_PIN      26     // DIO0

    //  Display — 0.96" SSD1306 OLED 128×64
    #undef  OLED_SDA
    #undef  OLED_SCL
    #undef  OLED_RST

    #define OLED_SDA            21
    #define OLED_SCL            22
    #define OLED_RST            16

    //  GPS — none onboard; use Fixed position or None in config
    #define HAS_NO_GPS
    #define GPS_RX              -1
    #define GPS_TX              -1

    //  Battery — 100k/100k voltage divider (ADC pin 35)
    #define BATTERY_PIN         35

    //  Bluetooth — hardware supports Classic; NimBLE used at runtime
    //  (HAS_NIMBLE is set by common.build_flags and takes precedence)
    #define HAS_BT_CLASSIC

#endif
