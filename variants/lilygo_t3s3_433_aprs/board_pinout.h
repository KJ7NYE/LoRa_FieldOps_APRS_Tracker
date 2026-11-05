#ifndef BOARD_PINOUT_H_
#define BOARD_PINOUT_H_

/*
 * LilyGo T3-S3 V1.2 / V1.3 (ESP32-S3FH4R2) with SX1262 — 433 MHz APRS multi-role build.
 *
 * Hardware: SX1262 LoRa (TCXO, DIO2 drives the RF switch), 0.96" SSD1306
 * OLED 128x64, WiFi, BLE, microSD (unused), native USB (no USB-UART bridge).
 * NO onboard GPS: use a fixed position, or wire an external module to the
 * GPS header pins below and remove HAS_NO_GPS.
 *
 * LilyGo sells the T3-S3 in several radio/band versions. This build is for
 * the SX1262 module with a 433 MHz RF front end; a 868/915 MHz board will
 * tune to 433 MHz but radiates very poorly through its band-fixed filter.
 *
 * Pinout matches upstream richonguzman/LoRa_APRS_Tracker variant
 * ttgo_lora32_t3s3_v1_2_GPS, LilyGo's T3-S3 schematics, and Meshtastic's
 * tlora_t3s3_v1 variant (LED, TCXO voltage, DIO2 RF switch).
 */

    //  LoRa Radio: SX1262
    #define HAS_SX1262
    #define HAS_TCXO                    // 1.8 V TCXO on DIO3; DIO2 as RF switch (lora_utils.cpp)
    #define RADIO_SCLK_PIN      5
    #define RADIO_MISO_PIN      3
    #define RADIO_MOSI_PIN      6
    #define RADIO_CS_PIN        7
    #define RADIO_RST_PIN       8
    #define RADIO_DIO1_PIN      33
    #define RADIO_BUSY_PIN      34
    #define RADIO_WAKEUP_PIN    RADIO_DIO1_PIN
    #define GPIO_WAKEUP_PIN     GPIO_SEL_33

    //  OLED display (128x64 SSD1306, no reset line)
    #undef  OLED_SDA
    #undef  OLED_SCL
    #undef  OLED_RST
    #define OLED_SDA            18
    #define OLED_SCL            17
    #define OLED_RST            -1

    //  No onboard GPS. External-module header pins (upstream mapping), unused
    //  while HAS_NO_GPS is defined: GPS_RX 42, GPS_TX 46.
    #define HAS_NO_GPS
    #define GPS_RX              -1
    #define GPS_TX              -1

    //  I/O
    #define BUTTON_PIN          0   // BOOT button
    #define BATTERY_PIN         1   // 100k/100k divider
    #define INTERNAL_LED_PIN    37  // green LED, active HIGH

    //  Capability flags
    //  HAS_WIFI, HAS_NIMBLE, HAS_WEB_UI, HAS_DISPLAY come from common_settings.ini [common]
    //  HAS_BT_CLASSIC intentionally omitted (ESP32-S3 has BLE only)

#endif
