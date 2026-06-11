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
void displaySetInvert(bool) {}
void displayTx(const String&) {}
void displayTxFlash() {}
void displayActivity() {}
void displayEcoTick(bool, unsigned long) {}
void startupScreen(const String&) {}
void displayStatus(const String&, const String&,
                   const String&, const String&, const String&,
                   const String&, const String&) {}
void displayAPMode(const String&, const String&) {}

void bootStatus(const char* step) {
    if (!step) return;
    Serial.print(F("[boot ")); Serial.print(millis()); Serial.print(F("ms] ")); Serial.println(step);
}

#else  // HAS_DISPLAY defined

#include <Arduino.h>
#include "display.h"

// TX overlay deadline — shared by all display paths.
// displayStatus() returns early while millis() < txDisplayEnd.
// Uses unsigned long (= millis() type) to avoid pulling in stdint.h here.
static unsigned long txDisplayEnd = 0;

// ── Display eco mode state ────────────────────────────────────────────────────
// lastActivityMs: reset on button press, RX packet, or TX — anything that
// justifies keeping the display on.  The 1-second status-refresh tick does NOT
// count; it uses this to decide whether to sleep, not to stay awake.
// displayOff: true while the display has been blanked by eco timeout.
static unsigned long _lastActivityMs = 0;   // 0 → treat as "just booted, display is on"
static bool          _displayOff     = false;

// Wake the display (if sleeping) and reset the eco-mode idle timer.
// Call from: button press, LoRa RX, LoRa TX.
void displayActivity() {
    _lastActivityMs = millis();
    if (_displayOff) {
        displayToggle(true);
        _displayOff = false;
    }
}

// Called once per second from the main loop.
// If eco mode is enabled and the idle timer has expired, blank the display.
void displayEcoTick(bool ecoMode, unsigned long timeoutMs) {
    if (!ecoMode || timeoutMs == 0 || _displayOff) return;
    if (millis() - _lastActivityMs > timeoutMs) {
        displayToggle(false);
        _displayOff = true;
    }
}

// ── Heltec T114 — Adafruit ST7789 path ───────────────────────────────────────
#ifdef HAS_TFT_ST7789

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include "display.h"
#include "configuration.h"

// BSP secondary SPI bus (NRF_SPIM2) wired to ST7789_SDA/SCK on P1.9/P1.8.
extern SPIClass SPI1;
extern Configuration Config;
static Adafruit_ST7789 tft(&SPI1, TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

namespace {
    // Colours (RGB565)
    constexpr uint16_t COLOR_BG   = 0x0000;   // black background
    constexpr uint16_t COLOR_HDR  = 0xFFE0;   // yellow  – callsign / separator
    constexpr uint16_t COLOR_BODY = 0xFFFF;   // white   – body text
    constexpr uint16_t COLOR_TX   = 0x07E0;   // green   – TX overlay
    constexpr uint16_t COLOR_DIM  = 0x5D1F;   // sky blue – secondary callsign under tactical

    bool    _tftReady    = false;
    bool    _cacheValid  = false;
    String  _prevCall    = "\xFF";
    String  _prevTactical= "\xFF";
    String  _prevLine2   = "\xFF";
    String  _prevLine3  = "\xFF";
    String  _prevLine4  = "\xFF";
    String  _prevLine5  = "\xFF";
    String  _prevLine6  = "\xFF";

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
    tft.setRotation(Config.display.turn180 ? 3 : 1);   // landscape; 3 = flipped 180°
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
    constexpr int STATUS_Y = 90;   // 2× font: below startup content
    constexpr int STATUS_H = 18;   // text size 2 = 16px + 2px margin
    tft.fillRect(0, STATUS_Y, tft.width(), STATUS_H, COLOR_BG);
    tft.setCursor(0, STATUS_Y);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_BODY, COLOR_BG);
    tft.print("> ");
    tft.print(step);
    _cacheValid = false;
}

void startupScreen(const String& versionDate) {
    tft.fillScreen(COLOR_BG);
    tft.setTextSize(4);    // 2× — title is 32px high
    tft.setTextColor(COLOR_TX, COLOR_BG);
    tft.setCursor(0, 0);
    tft.println("LoRa APRS");
    tft.setTextSize(2);    // 2× — body is 16px high
    tft.setTextColor(COLOR_BODY, COLOR_BG);
    tft.setCursor(0, 36);
    tft.println(versionDate);
    tft.setCursor(0, 72);
    tft.println("433 MHz");
    tft.setCursor(0, 90);
    tft.println("Starting...");
    // Settle window for peripheral inits (LoRa SX1262 timing).
    for (int i = 1; i <= 5; ++i) {
        delay(500);
        char step[16];
        snprintf(step, sizeof(step), "settle %d/5", i);
        bootStatus(step);
    }
}

void displayStatus(const String& callsign, const String& tactical,
                   const String& line2, const String& line3,
                   const String& line4, const String& line5,
                   const String& line6) {
    if (millis() < txDisplayEnd) return;
    if (!_tftReady) return;
    const int16_t W = tft.width();   // 240

    // ── Header: Tactical (primary) / Callsign (secondary), or Callsign only ────
    // Header is always 44px tall.
    // With tactical: tactical text×3 (y=2..26), callsign text×2 (y=27..43).
    // Without tactical: callsign text×4 (y=2..34).
    // Separator: y=44.  Body: y=46+i×17, i=0..4 → last line ends y=130 < 135 ✓
    if (!_cacheValid || callsign != _prevCall || tactical != _prevTactical) {
        tft.fillRect(0, 0, W, 45, COLOR_BG);
        if (tactical.length() > 0) {
            tft.setCursor(2, 2);
            tft.setTextSize(3);
            tft.setTextColor(COLOR_HDR, COLOR_BG);
            tft.print(tactical);
            tft.setCursor(2, 27);
            tft.setTextSize(2);
            tft.setTextColor(COLOR_DIM, COLOR_BG);
            tft.print(callsign);
        } else {
            tft.setCursor(2, 2);
            tft.setTextSize(4);
            tft.setTextColor(COLOR_HDR, COLOR_BG);
            tft.print(callsign);
        }
        tft.drawLine(0, 44, W, 44, COLOR_HDR);
        _prevCall     = callsign;
        _prevTactical = tactical;
    }

    // ── Lines 2-6 (text×2, 17px spacing, starting at y=46) ─────────────────
    // text×2 = 12px/char × 16px high; ~20 chars/line at 240px.
    // Line 6 (i=4): y = 46 + 4×17 = 114; ends at 130 < 135 ✓
    const String* lv[5]  = { &line2, &line3, &line4, &line5, &line6 };
    String*       pv[5]  = { &_prevLine2, &_prevLine3, &_prevLine4,
                              &_prevLine5, &_prevLine6 };
    for (int i = 0; i < 5; i++) {
        if (!_cacheValid || *lv[i] != *pv[i]) {
            int16_t y = 46 + i * 17;
            tft.fillRect(0, y, W, 17, COLOR_BG);
            tft.setCursor(0, y);
            tft.setTextSize(2);
            tft.setTextColor(COLOR_BODY, COLOR_BG);
            tft.print(*lv[i]);
            *pv[i] = *lv[i];
        }
    }
    _cacheValid = true;
}

void displayAPMode(const String& ssid, const String& password) {
    if (!_tftReady) return;
    tft.fillScreen(COLOR_BG);
    tft.setCursor(0, 2);
    tft.setTextSize(4);    // 2×  — "AP MODE" = 7 chars × 24px = 168px, fits in 240px
    tft.setTextColor(COLOR_HDR, COLOR_BG);
    tft.print("AP MODE");
    tft.drawLine(0, 36, tft.width(), 36, COLOR_HDR);
    tft.setTextSize(2);    // 2×
    tft.setTextColor(COLOR_BODY, COLOR_BG);
    tft.setCursor(0, 38);  tft.println("SSID: " + ssid);
    tft.setCursor(0, 56);  tft.println("PW: " + password);
    tft.setCursor(0, 74);  tft.println("192.168.4.1");
    tft.setCursor(0, 92);  tft.println("Waiting...");
    _cacheValid = false;
}

void displayTx(const String& packet) {
    if (!_tftReady) return;
    displayActivity();   // wake display if sleeping; reset idle timer
    txDisplayEnd = millis() + 2000;
    _cacheValid  = false;
    const int16_t W = tft.width();
    tft.fillScreen(COLOR_BG);
    tft.setCursor(0, 2);
    tft.setTextSize(4);    // 2×  — "<< TX >>" = 9 chars × 24px = 216px, fits in 240px
    tft.setTextColor(COLOR_TX, COLOR_BG);
    tft.print("<< TX >>");
    tft.drawLine(0, 44, W, 44, COLOR_TX);   // separator matches status display (y=44)
    // Body: text×2 (12px/char, 20 chars/line), 17px row height.
    // 5 rows fit: y=46,63,80,97,114 — last ends y=130 < 135 ✓
    tft.setTextSize(2);
    tft.setTextColor(COLOR_BODY, COLOR_BG);
    const int COLS = 20, ROWS = 5;
    for (int i = 0; i < ROWS; i++) {
        int start = i * COLS;
        if (start >= (int)packet.length()) break;
        tft.setCursor(0, 46 + i * 17);
        tft.print(packet.substring(start, start + COLS));
    }
}

void displayTxFlash() {}   // superseded by displayTx(); kept for build compat

void displaySetInvert(bool invert) {
    // ST7789 has no hardware invert command — just keep Config in sync.
    Config.display.invertDisplay = invert;
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
    #if defined(HELTEC_V3_GPS) || defined(HELTEC_V3_TNC) || \
        defined(HELTEC_V3_2_GPS) || defined(HELTEC_V3_2_TNC) || \
        defined(HELTEC_V3_433_APRS)
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

#ifdef HAS_TFT
static uint8_t screenBrightness = 1;   // TFT backlight PWM level (0–255)
#endif

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
        // VEXT_CTRL was asserted in POWER_Utils::setup() with no delay before
        // displaySetup() is called. SSD1315-variant panels (common on 2024-batch
        // Heltec V3 units) need ~500 ms for VCC and the internal charge pump to
        // stabilize before I2C init commands will be accepted. Without this delay
        // begin() ACKs (I2C controller is up early) but SETCONTRAST and charge-pump
        // enable commands are silently dropped, leaving the screen dark.
        // Upstream richonguzman/LoRa_APRS_Tracker uses delay(500) at the top of
        // displaySetup() for the same reason.
        delay(500);
        #ifdef OLED_DISPLAY_HAS_RST_PIN
            pinMode(OLED_RST, OUTPUT);
            digitalWrite(OLED_RST, LOW);
            delay(20);
            digitalWrite(OLED_RST, HIGH);
            delay(10);  // SSD1306 needs ~1 ms after RST goes HIGH; 10 ms is safe margin
        #endif
        Wire.begin(OLED_SDA, OLED_SCL);
        #ifdef ssd1306
            if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) {
                delay(200);  // VEXT may still be settling after soft reset; retry once
                if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) {
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "SSD1306", "init failed — display disabled");
                }
            }
        #else
            if (!display.begin(0x3c, false)) {
                delay(200);
                if (!display.begin(0x3c, false)) {
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "SH1106", "init failed — display disabled");
                }
            }
        #endif
        // Set maximum OLED contrast. 0xCF (81% max for SWITCHCAPVCC) is insufficient
        // for 2024-batch Heltec V3 panels (SSD1315-variant) — they produce no visible
        // light below this threshold. 0xFF crosses the illumination threshold.
        display.ssd1306_command(SSD1306_SETCONTRAST);
        display.ssd1306_command(0xFF);
        // Apply user invert preference (black-on-white vs white-on-black).
        display.invertDisplay(Config.display.invertDisplay);
        if (Config.display.turn180) display.setRotation(2);
        display.clearDisplay();
        #ifdef ssd1306
            display.setTextColor(WHITE);
        #else
            display.setTextColor(SH110X_WHITE);
            display.setContrast(255);
        #endif
        display.setTextWrap(false);   // clip overlong lines instead of wrapping
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

void displaySetInvert(bool invert) {
    #ifndef HAS_TFT
        // TFT boards don't support hardware invert — no-op there.
        display.invertDisplay(invert);
    #endif
    Config.display.invertDisplay = invert;   // keep Config in sync
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
        // title: text×2 (9 chars × 12px = 108px fits; text×3 = 162px > 128px)
        display.setTextSize(2);
        display.setCursor(0, 0);
        display.print("LoRa APRS");
        display.drawLine(0, 17, 128, 17, 1);
        // body: text×2 (2× larger than before; 128px / 12px = 10 chars max)
        display.setTextSize(2);
        display.setCursor(0, 20);
        display.print(versionDate);
        display.display();
    #endif
    delay(2500);
}

void displayStatus(const String& callsign, const String& tactical,
                   const String& line2, const String& line3,
                   const String& line4, const String& line5,
                   const String& line6) {
    if (millis() < txDisplayEnd) return;
    #ifdef HAS_TFT
        #ifdef HELTEC_WIRELESS_TRACKER
            sprite.fillSprite(TFT_BLACK);
            sprite.fillRect(0, 0, 160, 19, TFT_YELLOW);
            sprite.setTextFont(0);
            sprite.setTextSize(bigSizeFont);
            sprite.setTextColor(TFT_BLACK, TFT_YELLOW);
            String hdr = callsign;
            if (tactical.length() > 0) hdr += " " + tactical;
            sprite.drawString(hdr, 3, 3);
            sprite.setTextSize(smallSizeFont);
            sprite.setTextColor(TFT_WHITE, TFT_BLACK);
            sprite.drawString(line2, 3, 22);
            sprite.drawString(line3, 3, 34);
            sprite.drawString(line4, 3, 46);
            sprite.drawString(line5, 3, 58);
            sprite.pushSprite(0, 0);
        #endif
    #else
        // ── OLED 128×64 role-aware layout ────────────────────────────────────────
        //   y=0  : callsign / tactical — bold header (text×2)
        //   y=16 : separator
        //   y=17 : device role (text×2)                          — always text×2
        //   y=37 : role-specific detail (text×1, centred in slot) — GPS / IP / status
        //          iGate: drawn twice (x=0 + x=1) for bold strokes; never clips
        //   y=49 : battery or last-heard 4-s flash (text×2)
        //
        // Role → line 3 / line 4:
        //   Tracker : line3 = lat/lon or "Waiting..."   line4 = battery
        //   iGate   : line3 = IP or "No WiFi"           line4 = battery → last-heard 4 s
        //   Digi    : line3 = IP / lat/lon / status      line4 = battery → last-heard 4 s
        //
        // line2 from main.cpp: "Role  B:XX%" (battery suffix) and optionally
        // "Role  IP" or "Role  No WiFi" (WiFi suffix before battery suffix).
        // Full path names kept in main.cpp for TFT; OLED abbreviations applied here.

        // Persistent state for last-heard 4-second flash (iGate / Digi).
        static String   _oledLastHeard    = "";
        static uint32_t _oledLastHeardEnd = 0;

        display.clearDisplay();
        #ifdef ssd1306
            display.setTextColor(WHITE);
        #else
            display.setTextColor(SH110X_WHITE);
        #endif

        // ── Line 1: bold callsign header ─────────────────────────────────────────
        // Draw twice at x=0 and x=1 to widen strokes from 2 px → 3 px.
        display.setTextSize(2);
        {
            const String& hdr = tactical.length() > 0 ? tactical : callsign;
            display.setCursor(0, 0);  display.print(hdr);
            display.setCursor(1, 0);  display.print(hdr);
        }
        display.drawLine(0, 16, 128, 16, 1);

        // ── Parse line2 → role + battery + WiFi/IP suffix ────────────────────────
        String role = line2;
        String batt = "";
        {
            int bIdx = line2.indexOf("  B:");
            if (bIdx >= 0) {
                role = line2.substring(0, bIdx);
                batt = "B:" + line2.substring(bIdx + 4);
            }
        }
        // Role suffix: IP address or "No WiFi" (iGate always; Digi when wifiSTA enabled).
        String roleSuffix = "";
        {
            int sep = role.indexOf("  ");
            if (sep > 0) {
                roleSuffix = role.substring(sep + 2);
                role       = role.substring(0, sep);
            }
        }
        role.replace("WIDE1+W2", "W1+W2");
        role.replace("WIDE1",    "W1");

        // ── Line 2: device role ───────────────────────────────────────────────────
        display.setTextSize(2);
        display.setCursor(0, 17);
        display.print(role);

        // ── Last-heard flash tracking ─────────────────────────────────────────────
        String lastHd = line6.startsWith("Last: ") ? line6.substring(6) : line6;
        if (lastHd.length() > 0 && lastHd != _oledLastHeard) {
            _oledLastHeard    = lastHd;
            _oledLastHeardEnd = millis() + 4000;
        }
        bool showCallsign = (millis() < _oledLastHeardEnd && _oledLastHeard.length() > 0);

        // ── Role-aware line 3 / line 4 ────────────────────────────────────────────
        bool hasPosition    = (line3.indexOf('.') >= 0);
        bool hasIP          = (roleSuffix.length() > 0 && roleSuffix != "No WiFi");
        bool wifiConfigured = (roleSuffix.length() > 0);   // wifiSTA.enabled in main.cpp

        String line3disp, line4disp;

        if (role.startsWith("Track")) {
            // Tracker: GPS coordinates / waiting status on line 3; battery on line 4.
            line3disp = hasPosition              ? line3
                      : line3.startsWith("W")   ? "Waiting..."
                      :                           line3;      // "No GPS"
            line4disp = batt;

        } else if (role.startsWith("iGate")) {
            // iGate: WiFi IP (or "No WiFi") on line 3; last-heard flash else battery.
            line3disp = wifiConfigured ? roleSuffix : "No WiFi";
            line4disp = showCallsign   ? _oledLastHeard : batt;

        } else {
            // Digi: IP → lat/lon → "No GPS/Net" → "No GPS" (in priority order).
            // "No GPS/Net" = WiFi STA configured but neither connection nor GPS fix.
            if      (hasIP)          line3disp = roleSuffix;
            else if (hasPosition)    line3disp = line3;
            else if (wifiConfigured) line3disp = "No GPS/Net";
            else                     line3disp = "No GPS";
            line4disp = showCallsign ? _oledLastHeard : batt;
        }

        // iGate: IP bold text×1 (drawn twice at x=0 and x=1 to widen strokes, same as
        //        callsign header trick), last-heard/battery text×2 on y=49.
        // All other roles: IP/coords text×1 centred at y=37, battery text×2 on y=49.
        if (role.startsWith("iGate")) {
            display.setTextSize(1);
            display.setCursor(0, 37);  display.print(line3disp);
            display.setCursor(1, 37);  display.print(line3disp);
            display.setTextSize(2);
            display.setCursor(0, 49);
            display.print(line4disp);
        } else {
            // text×1 (6 w × 8 h px) — 21 chars/line handles full coords and IPs.
            // Vertically centred in the 16-px slot (y=33..48): offset = 33+(16-8)/2 = 37.
            display.setTextSize(1);
            display.setCursor(0, 37);
            display.print(line3disp);
            display.setTextSize(2);
            display.setCursor(0, 49);
            display.print(line4disp);
        }

        display.display();
    #endif
}

void displayAPMode(const String& ssid, const String& password) {
    #ifdef HAS_TFT
        #ifdef HELTEC_WIRELESS_TRACKER
            sprite.fillSprite(TFT_BLACK);
            sprite.fillRect(0, 0, 160, 19, TFT_YELLOW);
            sprite.setTextFont(0);
            sprite.setTextSize(bigSizeFont);
            sprite.setTextColor(TFT_BLACK, TFT_YELLOW);
            sprite.drawString("** AP Mode **", 3, 3);
            sprite.setTextSize(smallSizeFont);
            sprite.setTextColor(TFT_WHITE, TFT_BLACK);
            sprite.drawString("SSID: " + ssid,    3, 22);
            sprite.drawString("PW:   " + password, 3, 34);
            sprite.drawString("192.168.4.1",       3, 46);
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
        // text×3: "AP Mode" = 7 chars × 18px = 126px — just fits in 128px
        display.setTextSize(3);
        display.setCursor(0, 0);
        display.print("AP Mode");
        display.drawLine(0, 25, 128, 25, 1);
        // text×2 body: 12px/char, 16px high; 2 lines fit in remaining 64-26=38px
        display.setTextSize(2);
        display.setCursor(0, 27);
        display.print(ssid);
        display.setCursor(0, 44);
        display.print("PW: " + password);
        display.display();
    #endif
}

void displayTxFlash() {}   // superseded by displayTx(); kept for build compat

void displayTx(const String& packet) {
    displayActivity();   // wake display if sleeping; reset idle timer
    txDisplayEnd = millis() + 2000;
    #ifdef HAS_TFT
        #ifdef HELTEC_WIRELESS_TRACKER
            sprite.fillSprite(TFT_BLACK);
            sprite.fillRect(0, 0, 160, 19, TFT_GREEN);
            sprite.setTextFont(0);
            sprite.setTextSize(bigSizeFont);
            sprite.setTextColor(TFT_BLACK, TFT_GREEN);
            sprite.drawString("<< TX >>", 3, 3);
            sprite.setTextSize(smallSizeFont);
            sprite.setTextColor(TFT_WHITE, TFT_BLACK);
            sprite.drawString(packet.substring(0, 26), 3, 22);
            if (packet.length() > 26)
                sprite.drawString(packet.substring(26, 52), 3, 34);
            sprite.pushSprite(0, 0);
        #endif
    #else
        // OLED at 2× fonts: text×2 header (9×12=108px fits), text×2 body (10 chars max)
        display.clearDisplay();
        #ifdef ssd1306
            display.setTextColor(WHITE);
        #else
            display.setTextColor(SH110X_WHITE);
        #endif
        display.setTextSize(2);    // "<< TX >>" = 9 chars × 12px = 108px, fits in 128px
        display.setCursor(0, 0);
        display.print("<< TX >>");
        display.drawLine(0, 17, 128, 17, 1);
        // Body: text×1 (6px/char, 8px high, 21 chars/line), 10px row spacing.
        // 4 rows fit: y=19,29,39,49 — last ends y=57 < 64 ✓
        display.setTextSize(1);
        const int COLS = 21, ROWS = 4;
        for (int i = 0; i < ROWS; i++) {
            int start = i * COLS;
            if (start >= (int)packet.length()) break;
            display.setCursor(0, 19 + i * 10);
            display.print(packet.substring(start, start + COLS));
        }
        display.display();
    #endif
}

#endif  // !HAS_TFT_ST7789

#endif  // HAS_DISPLAY
