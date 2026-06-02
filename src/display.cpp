/* Minimal status display for LoRa APRS Multi-Mode Firmware.
 * Supports: Heltec T114 (ST7789 TFT via Adafruit), Heltec V3 / T-Beam
 * (SSD1306/SH1106 OLED via Adafruit), and headless builds (no display).
 *
 * No menu system, no keyboard nav, no profile selection.
 * Public API: displaySetup, bootStatus, startupScreen, displayStatus,
 *             displayTxFlash, displayToggle.
 */

#include "board_pinout.h"  // HAS_DISPLAY, HAS_TFT_ST7789, HAS_TFT must be in scope first

#ifndef HAS_DISPLAY

// ── Headless / no-display build ──────────────────────────────────────────────
#include <Arduino.h>
#include "display.h"

void displaySetup() {}
void displayToggle(bool) {}
void displayTxFlash() {}
void startupScreen(const String&) {}
void displayStatus(const String&, const String&, const String&, const String&) {}

void bootStatus(const char* step) {
    if (!step) return;
    Serial.print(F("[boot ")); Serial.print(millis()); Serial.print(F("ms] ")); Serial.println(step);
}

#else  // HAS_DISPLAY defined

// ── Heltec T114 — Adafruit ST7789 path ───────────────────────────────────────
#ifdef HAS_TFT_ST7789

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include "display.h"

// BSP secondary SPI bus (NRF_SPIM2) wired to ST7789_SDA/SCK on P1.9/P1.8.
extern SPIClass SPI1;
static Adafruit_ST7789 tft(&SPI1, TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

namespace {
    constexpr uint16_t COLOR_BG     = 0x0000;   // black
    constexpr uint16_t COLOR_HEADER = 0xFFE0;   // yellow
    constexpr uint16_t COLOR_BODY   = 0xFFFF;   // white
    constexpr uint16_t COLOR_TX     = 0x07E0;   // green for TX flash
    constexpr int      HEADER_Y     = 0;
    constexpr int      BODY_Y       = 24;
    constexpr int      LINE_HEIGHT  = 14;
    constexpr int      MAX_LINES    = 4;

    String  _prevHeader = "\xFF";
    String  _prevLines[MAX_LINES];
    bool    _cacheValid = false;
    bool    _tftReady   = false;

    void drawScreen(const String& header, const String* lines, int nLines) {
        const int16_t w = tft.width();
        if (!_cacheValid || header != _prevHeader) {
            tft.fillRect(0, HEADER_Y, w, 16, COLOR_BG);
            tft.setCursor(0, HEADER_Y);
            tft.setTextSize(2);
            tft.setTextColor(COLOR_HEADER);
            tft.print(header);
            _prevHeader = header;
        }
        tft.setTextSize(1);
        tft.setTextColor(COLOR_BODY);
        int y = BODY_Y;
        for (int i = 0; i < nLines && i < MAX_LINES; i++) {
            if (!_cacheValid || lines[i] != _prevLines[i]) {
                tft.fillRect(0, y, w, LINE_HEIGHT, COLOR_BG);
                tft.setCursor(0, y);
                tft.print(lines[i]);
                _prevLines[i] = lines[i];
            }
            y += LINE_HEIGHT;
        }
        _cacheValid = true;
    }
}

void displaySetup() {
    _cacheValid = false;
    #ifdef HELTEC_T114
        pinMode(3, OUTPUT);          // VTFT_CTRL (P0.3) — active-LOW to enable TFT power
        digitalWrite(3, LOW);
        delay(10);
    #endif
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, LOW);  // backlight on (active-LOW per T114 variant.h)
    SPI1.begin();
    tft.init(135, 240);
    tft.setRotation(1);             // landscape 240x135
    tft.fillScreen(COLOR_BG);
    tft.setTextWrap(false);
    _tftReady = true;
}

void displayToggle(bool on) {
    digitalWrite(TFT_BL_PIN, on ? LOW : HIGH);
}

void bootStatus(const char* step) {
    if (!step) return;
    Serial.print(F("[boot ")); Serial.print(millis()); Serial.print(F("ms] ")); Serial.println(step);
    if (!_tftReady) return;
    constexpr int STATUS_Y = 74;
    constexpr int STATUS_H = 12;
    tft.fillRect(0, STATUS_Y, tft.width(), STATUS_H, COLOR_BG);
    tft.setCursor(0, STATUS_Y);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_BODY, COLOR_BG);
    tft.print("> ");
    tft.print(step);
    _cacheValid = false;
}

void startupScreen(const String& versionDate) {
    tft.fillScreen(COLOR_BG);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_TX, COLOR_BG);
    tft.setCursor(0, 0);
    tft.println("LoRa APRS");
    tft.setTextSize(1);
    tft.setTextColor(COLOR_BODY, COLOR_BG);
    tft.setCursor(0, 24);
    tft.println("Multi-Mode v3");
    tft.setCursor(0, 38);
    tft.println(versionDate);
    tft.setCursor(0, 56);
    tft.println("433 MHz");
    tft.setCursor(0, 74);
    tft.println("Starting...");
    // Settle window for peripheral inits (LoRa SX1262 timing).
    for (int i = 1; i <= 3; ++i) {
        delay(500);
        char step[16];
        snprintf(step, sizeof(step), "settle %d/3", i);
        bootStatus(step);
    }
}

void displayStatus(const String& line1, const String& line2,
                   const String& line3, const String& line4) {
    const String lines[] = { line1, line2, line3, line4 };
    drawScreen("LoRa APRS", lines, 4);
}

void displayTxFlash() {
    if (!_tftReady) return;
    tft.fillRect(0, 0, 40, 16, COLOR_TX);
    tft.setCursor(2, 0);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_BG);
    tft.print("TX");
    delay(80);
    // Trigger full redraw on next displayStatus call.
    _cacheValid = false;
}

#else  // !HAS_TFT_ST7789 — SSD1306 / SH1106 OLED or TFT_eSPI path

// ── OLED (SSD1306 / SH1106) and TFT_eSPI ─────────────────────────────────────
#include <logger.h>
#include <Wire.h>
#include "configuration.h"
#include "display.h"

#ifdef HAS_TFT
    #include <TFT_eSPI.h>
    TFT_eSPI    tft    = TFT_eSPI();
    TFT_eSprite sprite = TFT_eSprite(&tft);

    #ifdef HELTEC_WIRELESS_TRACKER
        #define bigSizeFont   2
        #define smallSizeFont 1
        #define lineSpacing   12
        #define maxLineLength 26
    #endif
#else
    #include <Adafruit_GFX.h>

    #define ssd1306  // comment to use SH1106 instead
    #if defined(TTGO_T_Beam_S3_SUPREME_V3) || defined(TTGO_T_BEAM_1W)
        #undef ssd1306
    #endif
    #if defined(HELTEC_V3_GPS) || defined(HELTEC_V3_TNC) || defined(HELTEC_V3_2_GPS) || defined(HELTEC_V3_2_TNC)
        #define OLED_DISPLAY_HAS_RST_PIN
    #endif

    #ifdef ssd1306
        #include <Adafruit_SSD1306.h>
        Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);
    #else
        #include <Adafruit_SH110X.h>
        Adafruit_SH1106G display(128, 64, &Wire, OLED_RST);
    #endif
#endif

extern Configuration    Config;
extern logging::Logger  logger;

static uint8_t screenBrightness = 1;

void displaySetup() {
    #ifdef HAS_TFT
        tft.init();
        tft.begin();
        if (Config.display.turn180) {
            tft.setRotation(3);
        } else {
            tft.setRotation(1);
        }
        pinMode(TFT_BL, OUTPUT);
        analogWrite(TFT_BL, screenBrightness);
        tft.setTextFont(0);
        tft.fillScreen(TFT_BLACK);
        #ifdef HELTEC_WIRELESS_TRACKER
            sprite.createSprite(160, 80);
        #else
            sprite.createSprite(160, 80);
        #endif
    #else
        #ifdef OLED_DISPLAY_HAS_RST_PIN
            pinMode(OLED_RST, OUTPUT);
            digitalWrite(OLED_RST, LOW);
            delay(20);
            digitalWrite(OLED_RST, HIGH);
        #endif
        Wire.begin(OLED_SDA, OLED_SCL);
        #ifdef ssd1306
            if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "SSD1306", "allocation failed!");
                while (true) {}
            }
        #else
            if (!display.begin(0x3c, false)) {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "SH1106", "allocation failed!");
                while (true) {}
            }
        #endif
        if (Config.display.turn180) display.setRotation(2);
        display.clearDisplay();
        #ifdef ssd1306
            display.setTextColor(WHITE);
            display.ssd1306_command(SSD1306_SETCONTRAST);
            display.ssd1306_command(screenBrightness);
        #else
            display.setTextColor(SH110X_WHITE);
            display.setContrast(screenBrightness);
        #endif
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.display();
    #endif
}

void displayToggle(bool toggle) {
    #ifdef HAS_TFT
        analogWrite(TFT_BL, toggle ? screenBrightness : 0);
    #else
        #ifdef ssd1306
            display.ssd1306_command(toggle ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
        #else
            display.oled_command(toggle ? SH110X_DISPLAYON : SH110X_DISPLAYOFF);
        #endif
    #endif
}

void bootStatus(const char* step) {
    if (!step) return;
    Serial.print(F("[boot ")); Serial.print(millis()); Serial.print(F("ms] ")); Serial.println(step);
}

void startupScreen(const String& versionDate) {
    #ifdef HAS_TFT
        #ifdef HELTEC_WIRELESS_TRACKER
            sprite.fillSprite(TFT_BLACK);
            sprite.fillRect(0, 0, 160, 19, TFT_YELLOW);
            sprite.setTextFont(0);
            sprite.setTextSize(bigSizeFont);
            sprite.setTextColor(TFT_BLACK, TFT_YELLOW);
            sprite.drawString("LoRa APRS", 3, 3);
            sprite.setTextSize(smallSizeFont);
            sprite.setTextColor(TFT_WHITE, TFT_BLACK);
            sprite.drawString("Multi-Mode v3", 3, 22);
            sprite.drawString(versionDate, 3, 34);
            sprite.drawString("433 MHz", 3, 46);
            sprite.pushSprite(0, 0);
        #endif
    #else
        display.clearDisplay();
        #ifdef ssd1306
            display.setTextColor(WHITE);
            display.drawLine(0, 16, 128, 16, WHITE);
            display.drawLine(0, 17, 128, 17, WHITE);
        #else
            display.setTextColor(SH110X_WHITE);
            display.drawLine(0, 16, 128, 16, SH110X_WHITE);
            display.drawLine(0, 17, 128, 17, SH110X_WHITE);
        #endif
        display.setTextSize(2);
        display.setCursor(0, 0);
        display.println("LoRa APRS");
        display.setTextSize(1);
        display.setCursor(0, 20);
        display.println("Multi-Mode v3");
        display.setCursor(0, 30);
        display.println(versionDate);
        display.setCursor(0, 40);
        display.println("433 MHz");
        #ifdef ssd1306
            display.ssd1306_command(SSD1306_SETCONTRAST);
            display.ssd1306_command(screenBrightness);
        #else
            display.setContrast(screenBrightness);
        #endif
        display.display();
    #endif
    delay(1500);
}

void displayStatus(const String& line1, const String& line2,
                   const String& line3, const String& line4) {
    #ifdef HAS_TFT
        #ifdef HELTEC_WIRELESS_TRACKER
            sprite.fillSprite(TFT_BLACK);
            sprite.fillRect(0, 0, 160, 19, TFT_RED);
            sprite.setTextFont(0);
            sprite.setTextSize(bigSizeFont);
            sprite.setTextColor(TFT_WHITE, TFT_RED);
            sprite.drawString(line1, 3, 3);
            sprite.setTextSize(smallSizeFont);
            sprite.setTextColor(TFT_WHITE, TFT_BLACK);
            sprite.drawString(line2, 3, 22);
            sprite.drawString(line3, 3, 34);
            sprite.drawString(line4, 3, 46);
            sprite.pushSprite(0, 0);
        #endif
    #else
        display.clearDisplay();
        #ifdef ssd1306
            display.setTextColor(WHITE);
            display.drawLine(0, 16, 128, 16, WHITE);
            display.drawLine(0, 17, 128, 17, WHITE);
        #else
            display.setTextColor(SH110X_WHITE);
            display.drawLine(0, 16, 128, 16, SH110X_WHITE);
            display.drawLine(0, 17, 128, 17, SH110X_WHITE);
        #endif
        display.setTextSize(2);
        display.setCursor(0, 0);
        display.println(line1);
        display.setTextSize(1);
        display.setCursor(0, 20);
        display.println(line2);
        display.setCursor(0, 30);
        display.println(line3);
        display.setCursor(0, 40);
        display.println(line4);
        #ifdef ssd1306
            display.ssd1306_command(SSD1306_SETCONTRAST);
            display.ssd1306_command(screenBrightness);
        #else
            display.setContrast(screenBrightness);
        #endif
        display.display();
    #endif
}

void displayTxFlash() {
    // Brief TX indication. For OLED: temporarily show "TX" in header area.
    #ifdef HAS_TFT
        // nothing — TFT boards use displayStatus refresh instead
    #else
        display.clearDisplay();
        #ifdef ssd1306
            display.setTextColor(WHITE);
        #else
            display.setTextColor(SH110X_WHITE);
        #endif
        display.setTextSize(2);
        display.setCursor(0, 0);
        display.println(">>> TX <<<");
        #ifdef ssd1306
            display.ssd1306_command(SSD1306_SETCONTRAST);
            display.ssd1306_command(screenBrightness);
        #else
            display.setContrast(screenBrightness);
        #endif
        display.display();
        delay(100);
    #endif
}

#endif  // !HAS_TFT_ST7789

#endif  // HAS_DISPLAY
