/*
 * Heltec Mesh Node T114 (nRF52840) board pinout.
 *
 * Pin numbers cross-checked against meshtastic's variant.h for this board.
 * nRF52840 port encoding: P0.x = 0 + x, P1.x = 32 + x.
 *
 * Capability flags: no WiFi/BT-Classic/NimBLE/Web UI on nRF52840.
 * BLE is available via the nRF52840 SoftDevice (Adafruit Bluefruit) — detected
 * at compile time via ARDUINO_ARCH_NRF52, not a board_pinout.h flag.
 * HAS_DISPLAY + HAS_TFT_ST7789 are set below to route display.cpp into the
 * ST7789 software-SPI driver path.
 */
#ifndef BOARD_PINOUT_H_
#define BOARD_PINOUT_H_

// ---- LoRa: SX1262 (built-in) ------------------------------------------------
// DIO2 acts as RF switch, DIO3 powers a 1.8 V TCXO (set via build_flags).
#define HAS_SX1262
#define RADIO_SCLK_PIN          (0 + 19)
#define RADIO_MISO_PIN          (0 + 23)
#define RADIO_MOSI_PIN          (0 + 22)
#define RADIO_CS_PIN            (0 + 24)
#define RADIO_RST_PIN           (0 + 25)
#define RADIO_BUSY_PIN          (0 + 17)
#define RADIO_DIO1_PIN          (0 + 20)

// ---- GPS: Quectel L76K, on Serial1 -----------------------------------------
// Power is gated by VEXT_ENABLE — must be driven HIGH before GPS comes up.
#define GPS_RX                  (32 + 7)    // T114 nRF TX → GPS RX
#define GPS_TX                  (32 + 5)    // GPS TX → T114 nRF RX
#define GPS_BAUDRATE            9600
#define GPS_VCC                 (0 + 21)    // VEXT_ENABLE (active HIGH)
#define GPS_PPS_PIN             (32 + 4)
#define GPS_STANDBY_PIN         (32 + 2)

// ---- Display: ST7789 1.14" 240x135 TFT (built-in) ---------------------------
// Software-SPI driver path lives in display.cpp behind HAS_TFT_ST7789.
// Hardware SPI would require a second SPIM peripheral instance because the
// LoRa SPI bus owns the default SPI on different pins.
#define HAS_DISPLAY
#define HAS_TFT_ST7789
#define TFT_CS_PIN              11
#define TFT_DC_PIN              12
#define TFT_RST_PIN             2
#define TFT_MOSI_PIN            41
#define TFT_SCLK_PIN            40
#define TFT_BL_PIN              15

// ---- Battery monitoring -----------------------------------------------------
// ADC_CTRL must be driven HIGH to enable the divider before reading BATTERY_PIN.
#define BATTERY_PIN             4
#define ADC_CTRL_PIN            6
#define ADC_CTRL_ENABLED        HIGH

// ---- Button + LED -----------------------------------------------------------
#define BUTTON_PIN              (32 + 10)
#define INTERNAL_LED_PIN        (32 + 3)

// ---- I2C --------------------------------------------------------------------
// Adafruit nRF52 BSP exposes Wire on PIN_WIRE_SDA/SCL via the variant — we use
// Wire.begin() (no args) on nRF rather than naming pins here. The marketed I2C
// header on the T114 is on P0.16 (SDA) / P0.13 (SCL); a second sensor/RTC
// footprint exists on P0.26 / P0.27 but isn't populated on the standard board.
// Note: macro names SDA/SCL would collide with nRF52840 SoC TWI register field
// names in nordic/nrfx headers, so we deliberately do NOT define them here.

#endif
