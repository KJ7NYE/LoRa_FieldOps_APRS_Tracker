#ifndef BOARD_PINOUT_H_
#define BOARD_PINOUT_H_

/*
 * Heltec WiFi LoRa 32 V3 (ESP32-S3) — 433 MHz APRS multi-role build.
 *
 * Hardware: SX1262 LoRa, OLED 128×64, WiFi, BLE — NO onboard GPS.
 * Default role: iGate or Digipeater with fixed position configured via web UI
 * or serial CLI ("role set igate / role gps fixed / fixed latitude ...").
 *
 * Pinout from Heltec datasheet and verified on production boards.
 */

    //  LoRa Radio: SX1262
    #define HAS_SX1262
    #define RADIO_SCLK_PIN      9
    #define RADIO_MISO_PIN      11
    #define RADIO_MOSI_PIN      10
    #define RADIO_CS_PIN        8
    #define RADIO_RST_PIN       12
    #define RADIO_DIO1_PIN      14
    #define RADIO_BUSY_PIN      13

    //  OLED display (128×64, SSD1306-compatible)
    #undef  OLED_SDA
    #undef  OLED_SCL
    #undef  OLED_RST
    #define OLED_SDA            17
    #define OLED_SCL            18
    #define OLED_RST            21

    //  No GPS on this board — fixed position must be set in config.
    #define HAS_NO_GPS
    #define GPS_RX              -1
    #define GPS_TX              -1

    //  I/O
    #define BUTTON_PIN          0
    #define BATTERY_PIN         1
    #define VEXT_CTRL           36
    #define ADC_CTRL            37  // Drive LOW to enable VBAT divider

    #define BOARD_I2C_SDA       41
    #define BOARD_I2C_SCL       42

    //  Capability flags
    //  HAS_WIFI, HAS_NIMBLE, HAS_WEB_UI come from common_settings.ini [common]
    //  HAS_BT_CLASSIC intentionally omitted (V3 uses NimBLE only)

#endif
