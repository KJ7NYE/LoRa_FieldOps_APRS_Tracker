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

/*___________________________________________________________________

██╗      ██████╗ ██████╗  █████╗      █████╗ ██████╗ ██████╗ ███████╗
██║     ██╔═══██╗██╔══██╗██╔══██╗    ██╔══██╗██╔══██╗██╔══██╗██╔════╝
██║     ██║   ██║██████╔╝███████║    ███████║██████╔╝██████╔╝███████╗
██║     ██║   ██║██╔══██╗██╔══██║    ██╔══██║██╔═══╝ ██╔══██╗╚════██║
███████╗╚██████╔╝██║  ██║██║  ██║    ██║  ██║██║     ██║  ██║███████║
╚══════╝ ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═╝    ╚═╝  ╚═╝╚═╝     ╚═╝  ╚═╝╚══════╝

      ████████╗██████╗  █████╗  ██████╗██╗  ██╗███████╗██████╗
      ╚══██╔══╝██╔══██╗██╔══██╗██╔════╝██║ ██╔╝██╔════╝██╔══██╗
         ██║   ██████╔╝███████║██║     █████╔╝ █████╗  ██████╔╝
         ██║   ██╔══██╗██╔══██║██║     ██╔═██╗ ██╔══╝  ██╔══██╗
         ██║   ██║  ██║██║  ██║╚██████╗██║  ██╗███████╗██║  ██║
         ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝

                       Ricardo Guzman - CA2RXU
          https://github.com/richonguzman/LoRa_APRS_Tracker
             (donations : http://paypal.me/richonguzman)
____________________________________________________________________*/

#include "board_pinout.h"   // pulled to top so HAS_BT_CLASSIC is in scope before conditional library includes
#ifdef HAS_BT_CLASSIC
#include <BluetoothSerial.h>
#endif
#include <APRSPacketLib.h>
#include <TinyGPS++.h>
#include <Arduino.h>
#include <logger.h>
#ifdef HAS_WIFI
#include <WiFi.h>
#endif
#include "smartbeacon_utils.h"
#ifdef HAS_BT_CLASSIC
#include "bluetooth_utils.h"
#endif
#include "keyboard_utils.h"
#include "joystick_utils.h"
#include "configuration.h"
#include "battery_utils.h"
#include "station_utils.h"
#include "button_utils.h"
#include "power_utils.h"
#include "sleep_utils.h"
#include "menu_utils.h"
#include "lora_utils.h"
#ifdef HAS_WIFI
#include "wifi_utils.h"
#endif
#include "msg_utils.h"
#include "gps_utils.h"
#ifdef HAS_WEB_UI
#include "web_utils.h"
#endif
#ifdef HAS_NIMBLE
#include "ble_utils.h"
#endif
#include "wx_utils.h"
#include "display.h"
#include "serial_setup.h"
#include "utils.h"
#include "device_role.h"
#ifdef HAS_TOUCHSCREEN
#include "touch_utils.h"
#endif


String      versionDate             = "2026-05-03";
String      versionNumber           = "2.4.3.2";
Configuration                       Config;
#ifdef ARDUINO_ARCH_NRF52
    // Adafruit nRF52 BSP exposes Serial1 as a global Uart; alias it as gpsSerial.
    // Pin assignment is fixed by the BSP variant (PIN_SERIAL1_RX/TX).
    #define gpsSerial Serial1
#else
    HardwareSerial                  gpsSerial(1);
#endif
TinyGPSPlus                         gps;
#ifdef HAS_BT_CLASSIC
    BluetoothSerial                 SerialBT;
#endif

uint8_t     myBeaconsIndex          = 0;
int         myBeaconsSize           = Config.beacons.size();
Beacon      *currentBeacon          = &Config.beacons[myBeaconsIndex];
uint8_t     loraIndex               = 0;
int         loraIndexSize           = Config.loraTypes.size();
LoraType    *currentLoRaType        = &Config.loraTypes[loraIndex];

int         menuDisplay             = 100;
uint32_t    menuTime                = millis();

bool        statusUpdate            = true;
bool        displayEcoMode          = Config.display.ecoMode;
bool        displayState            = true;
uint32_t    displayTime             = millis();
uint32_t    refreshDisplayTime      = millis();

bool        sendUpdate              = true;

bool        bluetoothActive         = Config.bluetooth.active;
bool        bluetoothConnected      = false;

uint32_t    lastTx                  = 0.0;
uint32_t    txInterval              = 60000L;
uint32_t    lastTxTime              = 0;
double      lastTxLat               = 0.0;
double      lastTxLng               = 0.0;
double      lastTxDistance          = 0.0;

bool        flashlight              = false;
bool        digipeaterActive        = Config.digipeating;
bool        sosActive               = false;

bool        miceActive              = false;

bool        smartBeaconActive       = true;

uint32_t    lastGPSTime             = 0;

APRSPacket                          lastReceivedPacket;

logging::Logger                     logger;
//#define DEBUG

extern bool gpsIsActive;

void setup() {
    #ifndef ARDUINO_ARCH_NRF52
        // matches SERIAL_Setup::PASTE_MAX_BYTES so any legal 'import' paste fits
        // even if the main loop stalls during LoRa I/O. nRF52's TinyUSB CDC has
        // its own larger buffer (SERIAL_BUFFER_SIZE) set via build_flags.
        Serial.setRxBufferSize(16384);
    #endif
    Serial.begin(115200);

    #ifdef LORANGER_V1
        // Bring-up gate: give a freshly-attached serial monitor up to 5s to
        // catch boot output. Exits early as soon as a byte arrives, so once
        // the monitor is open this adds no real delay. Remove once bring-up
        // is done.
        {
            uint32_t t0 = millis();
            while (millis() - t0 < 5000 && !Serial.available()) {
                delay(50);
            }
            while (Serial.available()) Serial.read();
        }
    #endif

    #ifdef ARDUINO_ARCH_NRF52
        // FreeRTOS is now running, InternalFS is safe — finish what
        // Configuration's constructor would have done on ESP32.
        Config.init();
    #endif

    #ifndef DEBUG
        logger.setDebugLevel(logging::LoggerLevel::LOGGER_LEVEL_INFO);
    #endif

    bootStatus("power");
    POWER_Utils::setup();
    bootStatus("display");
    displaySetup();
    bootStatus("ext-pins");
    POWER_Utils::externalPinSetup();

    bootStatus("stations");
    STATION_Utils::loadIndex(0);    // callsign Index
    STATION_Utils::loadIndex(1);    // lora freq settins Index
    STATION_Utils::nearStationInit();
    startupScreen(loraIndex, versionDate);

    #ifdef HAS_WIFI
        bootStatus("wifi-AP check");
        WIFI_Utils::checkIfWiFiAP();
    #endif

    bootStatus("messages");
    MSG_Utils::loadNumMessages();
    bootStatus("GPS");
    GPS_Utils::setup();
    DeviceRoleUtils::initializeRole(Config.deviceRole);
    currentLoRaType = &Config.loraTypes[loraIndex];
    bootStatus("LoRa");
    #ifdef LORANGER_V1
        // Raw-SPI bring-up probe. Bypasses RadioLib so we can tell whether
        // the SX1268 is talking at all. Sends the SX126x GetStatus opcode
        // (0xC0, 0x00) and prints what MISO returned.
        //   ~0x22/0x42 → chip alive, RadioLib's begin() is failing for some
        //                other reason (TCXO config, calibration timeout)
        //   0x00       → MISO stuck low (chip dead, MISO shorted to GND, or
        //                chip never wakes from reset)
        //   0xFF       → MISO floating (chip dead, or MOSI/MISO swapped at
        //                the chip footprint)
        // Remove once bring-up is done.
        {
            SPIClass probeSPI(FSPI);
            probeSPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN, RADIO_CS_PIN);
            pinMode(RADIO_CS_PIN, OUTPUT);
            digitalWrite(RADIO_CS_PIN, HIGH);
            pinMode(RADIO_RST_PIN, OUTPUT);
            pinMode(RADIO_BUSY_PIN, INPUT);

            digitalWrite(RADIO_RST_PIN, LOW);
            delay(10);
            digitalWrite(RADIO_RST_PIN, HIGH);
            uint32_t t0 = millis();
            while (digitalRead(RADIO_BUSY_PIN) == HIGH && millis() - t0 < 100) { delay(1); }
            uint32_t busyWaitMs = millis() - t0;
            bool busyOk = digitalRead(RADIO_BUSY_PIN) == LOW;

            probeSPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
            digitalWrite(RADIO_CS_PIN, LOW);
            probeSPI.transfer(0xC0);
            uint8_t status = probeSPI.transfer(0x00);
            digitalWrite(RADIO_CS_PIN, HIGH);

            // Read 16-byte VERSION_STRING register at 0x0320 — opcode 0x1D
            // (READ_REGISTER), 2-byte addr, 1 NOP for status, then 16 bytes
            // of payload. RadioLib's findChip() compares the first 6 chars.
            char ver[17] = {0};
            digitalWrite(RADIO_CS_PIN, LOW);
            probeSPI.transfer(0x1D);
            probeSPI.transfer(0x03);
            probeSPI.transfer(0x20);
            probeSPI.transfer(0x00);
            for (int i = 0; i < 16; i++) ver[i] = probeSPI.transfer(0x00);
            digitalWrite(RADIO_CS_PIN, HIGH);
            probeSPI.endTransaction();
            probeSPI.end();

            Serial.printf("[probe] BUSY-low after RST: %s (waited %lums), GetStatus=0x%02X\n",
                          busyOk ? "yes" : "NO", (unsigned long)busyWaitMs, status);
            Serial.print("[probe] VERSION_STRING: \"");
            for (int i = 0; i < 16; i++) {
                if (ver[i] >= 0x20 && ver[i] < 0x7F) Serial.print(ver[i]);
                else Serial.print('.');
            }
            Serial.print("\" hex:");
            for (int i = 0; i < 16; i++) Serial.printf(" %02X", (uint8_t)ver[i]);
            Serial.println();
        }
    #endif
    LoRa_Utils::setup();
    bootStatus("I2C scan");
    Utils::i2cScannerForPeripherals();
    bootStatus("WX");
    WX_Utils::setup();

    #ifdef HAS_WIFI
        WiFi.mode(WIFI_OFF);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "Main", "WiFi controller stopped");
    #endif

    if (bluetoothActive) {
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

    #ifdef BUTTON_PIN
        bootStatus("button");
        BUTTON_Utils::setup();
    #endif
    #ifdef HAS_JOYSTICK
        bootStatus("joystick");
        JOYSTICK_Utils::setup();
    #endif
    bootStatus("keyboard");
    KEYBOARD_Utils::setup();
    #ifdef HAS_TOUCHSCREEN
        bootStatus("touch");
        TOUCH_Utils::setup();
    #endif

    #ifdef ARDUINO_ARCH_NRF52
        randomSeed(analogRead(BATTERY_PIN));
    #else
        esp_random();
        randomSeed(esp_random());
    #endif

    POWER_Utils::lowerCpuFrequency();
    logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "Main", "Smart Beacon is: %s", Utils::getSmartBeaconState());
    logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Main", "Setup Done!");
    bootStatus("serial CLI");
    SERIAL_Setup::setup();
    menuDisplay = 0;
    bootStatus("READY");
}

void loop() {
    currentBeacon = &Config.beacons[myBeaconsIndex];
    if (statusUpdate) {
        if (APRSPacketLib::checkNocall(currentBeacon->callsign)) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "Config", "Change your callsigns in WebConfig");
            displayShow("ERROR", "Callsigns = NOCALL!", "---> change it !!!", 2000);
            KEYBOARD_Utils::rightArrow();
            currentBeacon = &Config.beacons[myBeaconsIndex];
        }
        miceActive = APRSPacketLib::validateMicE(currentBeacon->micE);
    }

    SMARTBEACON_Utils::checkSettings(currentBeacon->smartBeaconSetting);
    SMARTBEACON_Utils::checkState();

    BATTERY_Utils::monitor();
    Utils::checkDisplayEcoMode();

    #ifdef BUTTON_PIN
        BUTTON_Utils::loop();
    #endif
    KEYBOARD_Utils::read();
    SERIAL_Setup::loop();
    #ifdef HAS_JOYSTICK
        JOYSTICK_Utils::loop();
    #endif
    #ifdef HAS_TOUCHSCREEN
        TOUCH_Utils::loop();
    #endif

    ReceivedLoRaPacket packet = LoRa_Utils::receivePacket();

    MSG_Utils::checkReceivedMessage(packet);
    MSG_Utils::processOutputBuffer();
    MSG_Utils::clean15SegBuffer();

    if (bluetoothActive && bluetoothConnected) {
        if (Config.bluetooth.useBLE) {
            #ifdef HAS_NIMBLE
                BLE_Utils::sendToPhone(packet.text.substring(3));
                BLE_Utils::sendToLoRa();
            #endif
        } else {
            #ifdef HAS_BT_CLASSIC
                BLUETOOTH_Utils::sendToPhone(packet.text.substring(3));
                BLUETOOTH_Utils::sendToLoRa();
            #endif
        }
    }

    MSG_Utils::ledNotification();
    Utils::checkFlashlight();
    STATION_Utils::checkListenedStationsByTimeAndDelete();

    lastTx = millis() - lastTxTime;
    if (gpsIsActive) {
        GPS_Utils::getData();
        bool gps_time_update = gps.time.isUpdated();
        bool gps_loc_update  = gps.location.isUpdated();
        GPS_Utils::setDateFromData();

        int currentSpeed = (int) gps.speed.kmph();

        if (gps_loc_update) Utils::checkStatus();

        if (!sendUpdate && gps_loc_update && smartBeaconActive) {
            GPS_Utils::calculateDistanceTraveled();
            if (!sendUpdate) GPS_Utils::calculateHeadingDelta(currentSpeed);
            STATION_Utils::checkStandingUpdateTime();
        }
        SMARTBEACON_Utils::checkFixedBeaconTime();
        if (sendUpdate && gps_loc_update) STATION_Utils::sendBeacon();
        if (gps_time_update) SMARTBEACON_Utils::checkInterval(currentSpeed);

        if (millis() - refreshDisplayTime >= 1000 || gps_time_update) {
            GPS_Utils::checkStartUpFrames();
            MENU_Utils::showOnScreen();
            refreshDisplayTime = millis();
        }
        SLEEP_Utils::checkIfGPSShouldSleep();
    } else {
        if (millis() - lastGPSTime > txInterval) {
            SLEEP_Utils::gpsWakeUp();
        }
        STATION_Utils::checkStandingUpdateTime();
        if (millis() - refreshDisplayTime >= 1000) {
            MENU_Utils::showOnScreen();
            refreshDisplayTime = millis();
        }
    }
}