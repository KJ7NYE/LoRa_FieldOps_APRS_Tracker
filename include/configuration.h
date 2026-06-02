/* Copyright (C) 2025 Ricardo Guzman - CA2RXU
 *
 * This file is part of LoRa APRS Tracker.
 *
 * LoRa APRS Tracker is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LoRa APRS Tracker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LoRa APRS Tracker. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef CONFIGURATION_H_
#define CONFIGURATION_H_

#include <Arduino.h>
#include <vector>
#ifndef ARDUINO_ARCH_NRF52
#include <FS.h>          // header is unused in this declaration; kept ESP32-side for upstream-merge friendliness
#endif
#include "smartbeacon_utils.h"

enum DeviceRole {
    ROLE_TRACKER    = 0,   // GPS tracking + SmartBeaconing; digiMode applies independently
    ROLE_IGATE      = 1,   // APRS-IS gateway (WiFi required); digiMode applies independently
    ROLE_DIGIPEATER = 2    // Dedicated digipeater (no tracking); sets digiMode≥1 if unset
};

// Digipeating mode — independent of device role.
// A tracker or iGate can also digipeat by setting digiMode > DIGI_OFF.
enum DigiMode {
    DIGI_OFF        = 0,   // No digipeating
    DIGI_WIDE1      = 1,   // Fill-in digi: respond to WIDE1-1 only
    DIGI_WIDE1_WIDE2 = 2   // Infrastructure digi: respond to WIDE1-1 and WIDE2-n
};

enum GPSSource {
    GPS_INTERNAL = 0,
    GPS_FIXED = 1,
    GPS_EXTERNAL_SERIAL = 2,
    GPS_EXTERNAL_BLE = 3
};

class WiFiSTA {
public:
    bool    enabled;
    String  ssid;
    String  password;
};

class WiFiAP {
public:
    bool    active;
    bool    bootWindow;
    String  password;
};

class Beacon {
public:
    String  callsign;
    String  symbol;
    String  overlay;
    String  micE;
    String  comment;
    bool    smartBeaconActive;
    byte    smartBeaconSetting;
    bool    gpsEcoMode;
    String  profileLabel;
    String  status;
    String  tacticalCallsign;
};

class Display {
public:
    bool    showSymbol;
    bool    ecoMode;
    int     timeout;
    bool    turn180;
};

class Battery {
public:
    bool    sendVoltage;
    bool    voltageAsTelemetry;
    bool    sendVoltageAlways;
    bool    monitorVoltage;
    float   sleepVoltage;
};

class Winlink {
public:
    String  password;
};

class Telemetry {
public:
    bool    active;
    bool    sendTelemetry;
    float   temperatureCorrection;
};

class Notification {
public:
    bool    ledTx;
    int     ledTxPin;
    bool    ledMessage;
    int     ledMessagePin;
    bool    ledFlashlight;
    int     ledFlashlightPin;
    bool    buzzerActive;
    int     buzzerPinTone;
    int     buzzerPinVcc;
    bool    bootUpBeep;
    bool    txBeep;
    bool    messageRxBeep;
    bool    stationBeep;
    bool    lowBatteryBeep;
    bool    shutDownBeep;
};

class LoraType {
public:
    long    frequency;
    int     spreadingFactor;
    long    signalBandwidth;
    int     codingRate4;
    int     power;
};

class PTT {
public:
    bool    active;
    int     io_pin;
    int     preDelay;
    int     postDelay;
    bool    reverse;
};

class BLUETOOTH {
public:
    bool    active;
    String  deviceName;
    bool    useBLE;
    bool    useKISS;
};

class APRSISS {
public:
    String  server;
    uint16_t port;
    String  passcode;
    String  filter;
};

class TCPKISS {
public:
    bool    enabled;
    uint16_t port;
};

class FixedPosition {
public:
    float   latitude;
    float   longitude;
    float   elevation;
};


class Configuration {
public:

    WiFiAP                  wifiAP;
    WiFiSTA                 wifiSTA;
    std::vector<Beacon>     beacons;
    Display                 display;
    Battery                 battery;
    Winlink                 winlink;
    Telemetry               telemetry;
    Notification            notification;
    std::vector<LoraType>   loraTypes;
    PTT                     ptt;
    BLUETOOTH               bluetooth;
    SmartBeaconValues       customSmartBeacon;
    APRSISS                 aprsIS;
    TCPKISS                 tcpKISS;
    FixedPosition           fixedPosition;

    DeviceRole              deviceRole;
    GPSSource               gpsSource;
    DigiMode                digiMode;   // replaces old bool digipeating
    bool    simplifiedTrackerMode;

    int     sendCommentAfterXBeacons;
    String  beaconPath;     // APRS path for OWN beacon TX (e.g. "WIDE1-1").
                            // NOT used by the digipeater when relaying other stations.
    String  email;
    int     nonSmartBeaconRate;
    int     rememberStationTime;
    int     standingUpdateTime;
    bool    sendAltitude;
    bool    disableGPS;

    void setDefaultValues();
    bool writeFile();
    Configuration();
    // nRF52-only: filesystem-backed setup deferred from the constructor because
    // static init runs before FreeRTOS / InternalFS is up. ESP32 builds run the
    // same logic from the constructor as before. Call once early in setup().
    #ifdef ARDUINO_ARCH_NRF52
    void init();
    #endif

private:
    bool readFile();
};

#endif