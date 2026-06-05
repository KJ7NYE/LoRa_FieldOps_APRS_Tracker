/* LoRa APRS Multi-Mode Firmware
 * Targets: Heltec T114 (nRF52840), Heltec V3 (ESP32-S3), LilyGo T-Beam (ESP32)
 * Roles:   Tracker | iGate | Digipeater  (configurable at runtime)
 * Config:  Web UI (WiFi boards), Serial CLI (all boards), USB serial KISS
 */

#include "board_pinout.h"
#ifdef HAS_BT_CLASSIC
#include <BluetoothSerial.h>
#endif
#include <Arduino.h>
#include "logger.h"
#include <TinyGPS++.h>
#include <APRSPacketLib.h>
#include "configuration.h"
#include "smartbeacon_utils.h"
#include "lora_utils.h"
#include "gps_utils.h"
#include "battery_utils.h"   // getBatteryInfoVoltage, getPercentVoltageBattery
#include "power_utils.h"
#include "display.h"
#include "serial_setup.h"
#include "station_utils.h"
#include "led_utils.h"
#include "device_role.h"
#include "kiss_utils.h"
#include "digi_utils.h"
#ifdef HAS_WIFI
#include <WiFi.h>
#include "wifi_utils.h"
#include "aprs_is_utils.h"
#include "tcp_kiss_utils.h"
#endif
#if defined(HAS_NIMBLE) || defined(ARDUINO_ARCH_NRF52)
#include "ble_utils.h"
#endif
#ifdef HAS_BT_CLASSIC
#include "bluetooth_utils.h"
#endif
#ifdef HAS_WEB_UI
#include "web_utils.h"
#endif


String versionDate   = "2026-06-02";
String versionNumber = "3.0.0";

Configuration Config;

#ifdef ARDUINO_ARCH_NRF52
    #define gpsSerial Serial1
#else
    HardwareSerial gpsSerial(1);
#endif
TinyGPSPlus gps;

#ifdef HAS_BT_CLASSIC
    BluetoothSerial SerialBT;
#endif

bool     bluetoothConnected = false;
uint32_t lastDisplayUpdate  = 0;
uint32_t lastBeaconCheck    = 0;  // SmartBeacon interval ticker

// GPS / beacon state — shared via extern across TUs.
// gpsIsActive is defined in gps_utils.cpp (starts false, set true in GPS_Utils::setup).
// disableGPS is defined in power_utils.cpp.
bool     sendUpdate         = true;

// USR button — short press: position beacon; long press (≥3 s): status beacon.
#ifdef BUTTON_PIN
static uint32_t btnPressTime = 0;
static bool     btnActive    = false;
#endif

extern bool disableGPS;
extern bool gpsIsActive;

// currentBeacon pointer — always points to beacons[0] in this single-profile build.
// Shared with smartbeacon_utils.cpp, gps_utils.cpp, station_utils.cpp.
Beacon*      currentBeacon    = nullptr;  // initialized in setup() after Config loads
// currentLoRaType pointer — always points to loraTypes[0].
// Shared with lora_utils.cpp.
LoraType*    currentLoRaType  = nullptr;  // initialized in setup() after Config loads
uint32_t     txInterval       = 60000L;   // SmartBeacon TX interval (ms), updated by smartbeacon_utils
bool         miceActive       = false;    // set true if beacons[0].micE is valid

// Runtime state flags — mirrored from Config at boot, updated live by serial CLI.
bool         digipeaterActive = false;   // mirrors Config.digiMode != DIGI_OFF
bool         bluetoothActive  = false;   // mirrors Config.bluetooth.active

logging::Logger logger;


void setup() {
    #ifndef ARDUINO_ARCH_NRF52
        Serial.setRxBufferSize(16384);
    #endif
    Serial.begin(115200);

    #ifdef ARDUINO_ARCH_NRF52
        Config.init();
    #else
        // Config constructor handles SPIFFS init on ESP32
    #endif

    // Set currentBeacon and currentLoRaType pointers after Config is loaded.
    currentBeacon    = &Config.beacons[0];
    currentLoRaType  = &Config.loraTypes[0];
    miceActive       = APRSPacketLib::validateMicE(currentBeacon->micE);
    digipeaterActive = (Config.digiMode != DIGI_OFF);
    bluetoothActive  = Config.bluetooth.active;

    POWER_Utils::setup();
    displaySetup();
    bootStatus("power OK");

    POWER_Utils::externalPinSetup();
    LED_Utils::setup();

    // USR button — active-LOW on all boards; set up here so it's ready before
    // the HAS_WIFI block does its own AP-mode check.
    #ifdef BUTTON_PIN
        pinMode(BUTTON_PIN, INPUT_PULLUP);
    #endif
    bootStatus("GPS");
    GPS_Utils::setup();

    bootStatus("LoRa");
    LoRa_Utils::setup();

    #ifdef HAS_WIFI
        // Read USR button at boot (active-LOW, INPUT_PULLUP).
        // Hold the button while powering on to force AP config mode.
        bool apButtonHeld = false;
        #ifdef BUTTON_PIN
            pinMode(BUTTON_PIN, INPUT_PULLUP);
            delay(10);   // settle
            apButtonHeld = (digitalRead(BUTTON_PIN) == LOW);
            if (apButtonHeld)
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Main",
                           "USR button held at boot — AP config mode requested");
        #endif
        bootStatus("WiFi AP check");
        WIFI_Utils::checkIfWiFiAP(apButtonHeld);
        if (Config.deviceRole != ROLE_IGATE) {
            WiFi.mode(WIFI_OFF);
        }
    #endif

    bootStatus("role");
    DeviceRoleUtils::initializeRole(Config.deviceRole);

    if (Config.bluetooth.active) {
        // Stack is board-determined: nRF52 or ESP32 with NimBLE → BLE KISS,
        // plain BT-Classic-only ESP32 → SPP KISS.
        #if defined(ARDUINO_ARCH_NRF52) || defined(HAS_NIMBLE)
            bootStatus("BLE KISS TNC");
            BLE_Utils::setup();
        #elif defined(HAS_BT_CLASSIC)
            bootStatus("BT KISS TNC");
            BLUETOOTH_Utils::setup();
        #endif
    }

    #ifdef ARDUINO_ARCH_NRF52
        randomSeed(analogRead(BATTERY_PIN));
    #else
        randomSeed(esp_random());
    #endif

    POWER_Utils::lowerCpuFrequency();
    SERIAL_Setup::setup();
    startupScreen(versionDate);
    bootStatus("READY");
}


void loop() {
    // ── LED heartbeat / TX / RX indicator ──────────────────────────────
    LED_Utils::tick();

    // ── Serial CLI + serial KISS ────────────────────────────────────────
    SERIAL_Setup::loop();

    // ── USR button ──────────────────────────────────────────────────────
    // Short press (≥50 ms, <3 s): send position beacon immediately.
    // Long press  (≥3 s):         send status beacon (beacons[0].status text).
    #ifdef BUTTON_PIN
    {
        bool btnDown = (digitalRead(BUTTON_PIN) == LOW);
        if (btnDown && !btnActive) {
            btnActive    = true;
            btnPressTime = millis();
        } else if (!btnDown && btnActive) {
            uint32_t held = millis() - btnPressTime;
            btnActive = false;
            displayActivity();   // any button release wakes the display
            if (held >= 3000) {
                STATION_Utils::sendStatusBeacon();
            } else if (held >= 50) {
                STATION_Utils::sendBeacon();
            }
        }
    }
    #endif

    // ── Receive LoRa ────────────────────────────────────────────────────
    ReceivedLoRaPacket rx = LoRa_Utils::receivePacket();
    if (rx.text.length() > 3) {
        LED_Utils::txRxFlash();
        displayActivity();   // incoming packet wakes the display
        String packet = rx.text.substring(3);   // strip 3-byte RSSI prefix

        // Digipeating (any role, controlled by digiMode config)
        DIGI_Utils::processLoRaPacket(packet);

        // iGate upload + TCP KISS forward
        #ifdef HAS_WIFI
        if (Config.deviceRole == ROLE_IGATE) {
            APRS_IS_Utils::processLoRaPacket(packet);
            TCP_KISS_Utils::sendToClients(packet);
        }
        #endif

        // Serial KISS forward — always on in KISS mode
        if (SERIAL_Setup::isKISSMode()) {
            String kissFrame = KISS_Utils::encodeKISS(packet);
            Serial.write((const uint8_t*)kissFrame.c_str(), kissFrame.length());
        }

        // BLE/BT KISS forward
        if (Config.bluetooth.active && bluetoothConnected) {
            #if defined(ARDUINO_ARCH_NRF52) || defined(HAS_NIMBLE)
                BLE_Utils::sendToPhone(packet);
            #elif defined(HAS_BT_CLASSIC)
                BLUETOOTH_Utils::sendToPhone(packet);
            #endif
        }

        STATION_Utils::updateLastHeard(
            packet.substring(0, packet.indexOf(">"))
        );
    }

    // ── BLE / BT inbound (KISS TX) ──────────────────────────────────────
    if (Config.bluetooth.active && bluetoothConnected) {
        #if defined(ARDUINO_ARCH_NRF52) || defined(HAS_NIMBLE)
            BLE_Utils::sendToLoRa();
        #elif defined(HAS_BT_CLASSIC)
            BLUETOOTH_Utils::sendToLoRa();
        #endif
    }

    // ── Role periodic tasks (APRS-IS keepalive, TCP KISS clients) ───────
    DeviceRoleUtils::handleRoleSpecificTasks();

    // ── Output packet buffer (digi re-TX, iGate downlink) ───────────────
    STATION_Utils::processOutputPacketBuffer();

    // ── Beaconing ────────────────────────────────────────────────────────
    uint32_t now = millis();
    if (gpsIsActive) {
        GPS_Utils::getData();
        bool locUpdated  = gps.location.isUpdated();
        bool timeUpdated = gps.time.isUpdated();
        GPS_Utils::setDateFromData();

        int speed = (int)gps.speed.kmph();
        SMARTBEACON_Utils::checkSettings(Config.beacons[0].smartBeaconSetting);
        SMARTBEACON_Utils::checkState();

        if (locUpdated) GPS_Utils::calculateDistanceTraveled();
        GPS_Utils::calculateHeadingDelta(speed);
        SMARTBEACON_Utils::checkFixedBeaconTime();

        if (sendUpdate && locUpdated) {
            STATION_Utils::sendBeacon();
        }
        if (timeUpdated) SMARTBEACON_Utils::checkInterval(speed);
    } else {
        // Fixed position or GPS sleeping — fire beacon on fixed interval
        if (now - lastBeaconCheck >= (uint32_t)Config.nonSmartBeaconRate * 60000UL) {
            lastBeaconCheck = now;
            STATION_Utils::sendBeacon();
        }
    }

    // ── Battery monitor ──────────────────────────────────────────────────
    BATTERY_Utils::monitor();

    // ── Display eco mode timeout check ──────────────────────────────────
    displayEcoTick(Config.display.ecoMode,
                   (unsigned long)Config.display.timeout * 1000UL);

    // ── Display refresh (once per second) ───────────────────────────────
    if (now - lastDisplayUpdate >= 1000) {
        lastDisplayUpdate = now;
        const Beacon& b = Config.beacons[0];

        // ── Line 1: callsign + tactical ──────────────────────────────────
        String callsign = b.callsign;
        String tactical = b.tacticalCallsign;
        tactical.trim();

        // ── Line 2: role / mode  +  battery ─────────────────────────────
        String line2;
        switch (Config.deviceRole) {
            case ROLE_TRACKER:
                line2 = "Tracker";
                break;
            case ROLE_IGATE:
                line2 = "iGate";
                #ifdef HAS_WIFI
                line2 += WIFI_Utils::isSTAConnected()
                       ? ("  " + WiFi.localIP().toString())
                       : "  No WiFi";
                #endif
                break;
            case ROLE_DIGIPEATER:
                line2 = "Digi";
                if      (Config.digiMode == DIGI_WIDE1)       line2 += " WIDE1";
                else if (Config.digiMode == DIGI_WIDE1_WIDE2) line2 += " WIDE1+W2";
                break;
            default:
                line2 = "Unknown";
                break;
        }
        {
            String bv = BATTERY_Utils::getBatteryInfoVoltage();
            if (bv.length() > 0 && bv.toFloat() > 1.5) {
                String pct = BATTERY_Utils::getPercentVoltageBattery(bv.toFloat());
                pct.trim();
                line2 += "  B:" + pct + "%";
            }
        }

        // ── Maidenhead gridsquare (6-char: AAnnll) ───────────────────────
        // Format: 2 uppercase letters (field) + 2 digits (square) + 2 lowercase letters (subsquare)
        auto calcGrid = [](double lat, double lon) -> String {
            double aLon = lon + 180.0;
            double aLat = lat + 90.0;
            int fLon  = (int)(aLon / 20.0);
            int fLat  = (int)(aLat / 10.0);
            int sLon  = (int)((aLon - fLon * 20.0) / 2.0);
            int sLat  = (int)(aLat - fLat * 10.0);
            int ssLon = (int)(((aLon - fLon * 20.0) - sLon * 2.0) * 12.0);
            int ssLat = (int)(((aLat - fLat * 10.0) - sLat)       * 24.0);
            char gs[7] = {
                (char)('A' + fLon),  (char)('A' + fLat),   // uppercase field
                (char)('0' + sLon),  (char)('0' + sLat),   // digits
                (char)('a' + ssLon), (char)('a' + ssLat),  // lowercase subsquare
                '\0'
            };
            return String(gs);
        };

        // ── Lines 3 & 4: position ────────────────────────────────────────
        // Line 3: lat / lon (4 decimals)
        // Line 4: 6-char gridsquare (AAnnll) + altitude + speed
        String line3, line4;
        bool hasGPSFix = gpsIsActive && gps.location.isValid();
        if (hasGPSFix) {
            double lat = gps.location.lat();
            double lon = gps.location.lng();
            char buf[32];
            snprintf(buf, sizeof(buf), "%.4f %.4f", lat, lon);
            line3 = String(buf);
            snprintf(buf, sizeof(buf), "%s  %dm  %dkm/h",
                     calcGrid(lat, lon).c_str(),
                     (int)gps.altitude.meters(), (int)gps.speed.kmph());
            line4 = String(buf);
        } else if (Config.gpsSource == GPS_FIXED) {
            double lat = Config.fixedPosition.latitude;
            double lon = Config.fixedPosition.longitude;
            char buf[32];
            snprintf(buf, sizeof(buf), "%.4f %.4f", lat, lon);
            line3 = String(buf);
            line4 = calcGrid(lat, lon) + "  Fixed Position";
        } else {
            line3 = gpsIsActive ? "Waiting for GPS fix" : "No GPS";
            line4 = "";
        }

        // ── Line 5: uptime ───────────────────────────────────────────────
        uint32_t upSec = millis() / 1000;
        String line5;
        if (upSec < 3600) {
            line5 = "Up " + String(upSec / 60) + "m";
        } else {
            line5 = "Up " + String(upSec / 3600) + "h" + String((upSec % 3600) / 60) + "m";
        }

        // ── Line 6: last heard station ───────────────────────────────────
        String lastRx = STATION_Utils::getLastHeardSummary();
        String line6 = lastRx.length() > 0 ? "Last: " + lastRx : "";

        displayStatus(callsign, tactical, line2, line3, line4, line5, line6);
    }
}
