#ifndef BOARD_PINOUT_H_
#define BOARD_PINOUT_H_

/*
 * LilyGo T-Beam 433 1W — 433 MHz APRS multi-role build.
 *
 * MCU:     ESP32-S3
 * Radio:   SX1262 at 30 dBm (1 W); module may be labeled SX1268 but ships
 *          SX1262 silicon — use SX1262 RadioLib driver.
 * GPS:     onboard GNSS module; VCC hardwired; WAKE_UP pin for standby.
 * Display: SH1106 OLED 128×64 via I2C.
 * PMIC:    none; LoRa LDO enabled via GPIO (LDO_EN); battery read via ADC.
 * Fan:     IO41, always on while running, off at sleep/shutdown.
 *
 * Pinout from LilyGo T-Beam 1W schematic.
 */

    //  LoRa Radio (FSPI — IO11/12/13 are ESP32-S3 FSPI pins)
    //  Module may be labeled SX1268 but ships SX1262 silicon.
    #define HAS_SX1262
    #define HAS_1W_LORA
    #define RADIO_SCLK_PIN      13
    #define RADIO_MISO_PIN      12
    #define RADIO_MOSI_PIN      11
    #define RADIO_CS_PIN        15
    #define RADIO_RST_PIN       3
    #define RADIO_DIO1_PIN      1
    #define RADIO_BUSY_PIN      38
    #define RADIO_VCC_PIN       40  // LDO_EN — GPIO-controlled LoRa power rail
    #define RADIO_RXEN          21  // CTL — RF switch RX enable; no TXEN on this board

    //  Display: SH1106 OLED 128×64 (I2C)
    #undef  OLED_SDA
    #undef  OLED_SCL
    #undef  OLED_RST
    #define OLED_SDA            8
    #define OLED_SCL            9
    #define OLED_RST            -1

    //  GPS (onboard GNSS, VCC hardwired; WAKE_UP toggles standby/active)
    #define HAS_GPS_CTRL
    #define GPS_TX              5   // GNSS_TXD → ESP UART RX
    #define GPS_RX              6   // GNSS_RXD → ESP UART TX
    #define GPS_VCC             16  // WAKE_UP pin (standby/active toggle)

    //  Fan and temperature
    #define FAN_CTRL_PIN        41  // Cooling fan; managed by THERMAL_Utils
    #define TEMP_SENSOR_PIN     14  // NTC: NCP18XH103F03RB (10 kΩ), 10 kΩ pull-down to GND

    //  User LED — heartbeat / TX-RX indicator (active-HIGH)
    #define INTERNAL_LED_PIN    18  // IO18 — user LED; gated by Config.display.ledEnabled

    //  User input and battery
    #define BUTTON_PIN          17  // USR button — beacon / AP mode trigger
                                    // IO0 (BOOT) = strapping pin, no define
                                    // RST button wired directly to ESP EN pin
    #define BATTERY_PIN         4   // ADC: 300 kΩ + 150 kΩ voltage divider

#endif
