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
#include <TinyGPS++.h>
#include <APRSPacketLib.h>
#include "configuration.h"
#include "smartbeacon_utils.h"
#include "lora_utils.h"
#include "gps_utils.h"
#include "battery_utils.h"
#include "power_utils.h"
#include "sleep_utils.h"
#include "display.h"
#include "serial_setup.h"
#include "station_utils.h"
#include "device_role.h"
#include "kiss_utils.h"
#include "digi_utils.h"
#ifdef HAS_WIFI
#include <WiFi.h>
#include "wifi_utils.h"
#include "aprs_is_utils.h"
#include "tcp_kiss_utils.h"
#endif
#ifdef HAS_NIMBLE
#include "ble_utils.h"
#endif
#ifdef HAS_BT_CLASSIC
#include "bluetooth_utils.h"
#endif
#ifdef HAS_WEB_UI
#include "web_utils.h"
#endif


const String versionDate   = "2026-06-02";
const String versionNumber = "3.0.0";

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
// sendStandingUpdate is defined in station_utils.cpp.
bool     sendUpdate         = true;
bool     gpsShouldSleep     = false;
bool     disableGPS         = false;
extern bool gpsIsActive;

// currentBeacon pointer — always points to beacons[0] in this single-profile build.
// Shared with smartbeacon_utils.cpp, gps_utils.cpp, station_utils.cpp.
Beacon*      currentBeacon    = nullptr;  // initialized in setup() after Config loads
uint32_t     txInterval       = 60000L;   // SmartBeacon TX interval (ms), updated by smartbeacon_utils
bool         miceActive       = false;    // set true if beacons[0].micE is valid

// Legacy state flags referenced from serial_setup.cpp.
bool         digipeaterActive = false;
bool         bluetoothActive  = false;
bool         displayEcoMode   = false;

// GPS timing — referenced by sleep_utils.cpp via extern.
uint32_t     lastGPSTime      = 0;

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

    // Set currentBeacon pointer after Config is loaded.
    currentBeacon = &Config.beacons[0];
    miceActive    = APRSPacketLib::validateMicE(currentBeacon->micE);

    POWER_Utils::setup();
    displaySetup();
    bootStatus("power OK");

    POWER_Utils::externalPinSetup();
    bootStatus("GPS");
    GPS_Utils::setup();

    bootStatus("LoRa");
    LoRa_Utils::setup();

    #ifdef HAS_WIFI
        bootStatus("WiFi AP check");
        WIFI_Utils::checkIfWiFiAP();
        if (Config.deviceRole != ROLE_IGATE) {
            WiFi.mode(WIFI_OFF);
        }
    #endif

    bootStatus("role");
    DeviceRoleUtils::initializeRole(Config.deviceRole);

    if (Config.bluetooth.active) {
        if (Config.bluetooth.useBLE) {
            #ifdef HAS_NIMBLE
                bootStatus("BLE");
                BLE_Utils::setup();
            #endif
        } else {
            #ifdef HAS_BT_CLASSIC
                bootStatus("BT classic");
                BLUETOOTH_Utils::setup();
            #endif
        }
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
    // ── Serial CLI + serial KISS ────────────────────────────────────────
    SERIAL_Setup::loop();

    // ── Receive LoRa ────────────────────────────────────────────────────
    ReceivedLoRaPacket rx = LoRa_Utils::receivePacket();
    if (rx.text.length() > 3) {
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

        // Serial KISS forward
        if (Config.tcpKISS.serialEnabled && !SERIAL_Setup::isActive()) {
            String kissFrame = KISS_Utils::encodeKISS(packet);
            Serial.write((const uint8_t*)kissFrame.c_str(), kissFrame.length());
        }

        // BLE/BT KISS forward
        if (Config.bluetooth.active && bluetoothConnected) {
            if (Config.bluetooth.useBLE && Config.bluetooth.useKISS) {
                #ifdef HAS_NIMBLE
                    BLE_Utils::sendToPhone(packet);
                #endif
            } else if (!Config.bluetooth.useBLE) {
                #ifdef HAS_BT_CLASSIC
                    BLUETOOTH_Utils::sendToPhone(packet);
                #endif
            }
        }

        STATION_Utils::updateLastHeard(
            packet.substring(0, packet.indexOf(">"))
        );
    }

    // ── BLE / BT inbound (KISS TX) ──────────────────────────────────────
    if (Config.bluetooth.active && bluetoothConnected) {
        if (Config.bluetooth.useBLE) {
            #ifdef HAS_NIMBLE
                BLE_Utils::sendToLoRa();
            #endif
        } else {
            #ifdef HAS_BT_CLASSIC
                BLUETOOTH_Utils::sendToLoRa();
            #endif
        }
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
        SLEEP_Utils::checkIfGPSShouldSleep();
    } else {
        // Fixed position or GPS sleeping — fire beacon on fixed interval
        if (now - lastBeaconCheck >= (uint32_t)Config.nonSmartBeaconRate * 60000UL) {
            lastBeaconCheck = now;
            STATION_Utils::sendBeacon();
        }
    }

    // ── Battery monitor ──────────────────────────────────────────────────
    BATTERY_Utils::monitor();

    // ── Display refresh (once per second) ───────────────────────────────
    if (now - lastDisplayUpdate >= 1000) {
        lastDisplayUpdate = now;
        String line1 = String(DeviceRoleUtils::getRoleString(Config.deviceRole))
                     + "  " + Config.beacons[0].callsign;
        String line2 = "";
        #ifdef HAS_WIFI
        if (Config.deviceRole == ROLE_IGATE) {
            line2 = WIFI_Utils::isSTAConnected()
                  ? ("IP " + WiFi.localIP().toString())
                  : "WiFi: not connected";
        } else
        #endif
        if (gpsIsActive && gps.location.isValid()) {
            line2 = "GPS " + String(gps.satellites.value()) + " sats";
        } else {
            line2 = (Config.gpsSource == GPS_FIXED) ? "Fixed pos" : "No GPS fix";
        }
        String line3 = STATION_Utils::getLastHeardSummary();
        String line4 = "Up " + String(millis() / 60000) + "m";
        displayStatus(line1, line2, line3, line4);
    }
}
