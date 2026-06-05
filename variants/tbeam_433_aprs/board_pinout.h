#ifndef BOARD_PINOUT_H_
#define BOARD_PINOUT_H_

/*
 * LilyGo T-Beam v1.2 — 433 MHz APRS multi-role build.
 *
 * Hardware: SX1278 LoRa, OLED, WiFi, BLE, onboard GPS (NEO-6M/NEO-M8N),
 * AXP2101 PMIC.  Supports all roles: Tracker, iGate, Digipeater.
 *
 * For iGate: set wifiSTA credentials + aprsIS settings via web UI or serial CLI.
 * For Tracker: GPS_INTERNAL (default); SmartBeaconing or fixed interval.
 * For Digipeater: no GPS needed; fixed position optional for self-beacon.
 *
 * Pinout from LilyGo schematic v1.2.
 */

    //  LoRa Radio: SX1278 (433 MHz)
    #define HAS_SX1278
    #define RADIO_SCLK_PIN      5
    #define RADIO_MISO_PIN      19
    #define RADIO_MOSI_PIN      27
    #define RADIO_CS_PIN        18
    #define RADIO_RST_PIN       23
    #define RADIO_BUSY_PIN      26   // DIO0 on SX1278 used as busy/interrupt

    //  OLED display (128×64)
    #undef  OLED_SDA
    #undef  OLED_SCL
    #undef  OLED_RST
    #define OLED_SDA            21
    #define OLED_SCL            22
    #define OLED_RST            16

    //  GPS (NEO series on UART1)
    #define HAS_GPS_CTRL
    #define GPS_RX              12
    #define GPS_TX              34

    //  PMIC
    #define HAS_AXP2101

    //  Button (middle button on T-Beam)
    #define BUTTON_PIN          38

    //  Capability: BT Classic available on ESP32 (non-S3)
    #define HAS_BT_CLASSIC

#endif
