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

#include "device_role.h"
#include "configuration.h"
#include "board_pinout.h"
#include "logger.h"
#include "display.h"
#include "digi_utils.h"
#ifdef HAS_WIFI
#include "aprs_is_utils.h"
#include "tcp_kiss_utils.h"
#include "wifi_utils.h"
#endif

extern Configuration Config;
extern logging::Logger logger;

namespace DeviceRoleUtils {

    static bool roleInitialized = false;

    const char* getRoleString(DeviceRole role) {
        switch (role) {
            case ROLE_TRACKER:    return "Tracker";
            case ROLE_IGATE:      return "iGate";
            case ROLE_DIGIPEATER: return "Digipeater";
            default:              return "Unknown";
        }
    }

    bool validateRoleForPlatform(DeviceRole role) {
        #ifdef ARDUINO_ARCH_NRF52
            if (role == ROLE_IGATE) {
                Serial.println("ERROR: iGate role not supported on nRF52 (no WiFi)");
                return false;
            }
        #endif
        return true;
    }

    void initializeRole(DeviceRole role) {
        if (!validateRoleForPlatform(role)) {
            Serial.println("WARN: Invalid role for this platform, defaulting to Tracker");
            Config.deviceRole = ROLE_TRACKER;
            Config.writeFile();
            return;
        }

        Serial.print("INFO: Initializing device role: ");
        Serial.println(getRoleString(role));

        switch (role) {
            case ROLE_TRACKER:
                initializeTracker();
                break;
            case ROLE_IGATE:
                #ifdef HAS_WIFI
                    initializeIGate();
                #endif
                break;
            case ROLE_DIGIPEATER:
                initializeDigipeater();
                break;
            default:
                Serial.println("ERROR: Unknown device role");
                break;
        }

        roleInitialized = true;
    }

    void initializeTracker() {
        Serial.println("INFO: Tracker mode: GPS + smart beaconing, messaging active");
        displayShow("Mode", "TRACKER", "", 1500);
    }

    void initializeDigipeater() {
        Serial.println("INFO: Digipeater mode: WIDE1/WIDE2 RF relay active");
        displayShow("Mode", "DIGIPEATER", "", 1500);
    }

    #ifdef HAS_WIFI
    void initializeIGate() {
        Serial.println("INFO: iGate mode: connecting WiFi + APRS-IS");

        if (!WIFI_Utils::connectSTA()) {
            Serial.println("WARN: WiFi STA connect failed; iGate will retry in background");
        }

        APRS_IS_Utils::connect();

        if (Config.tcpKISS.enabled) {
            TCP_KISS_Utils::setup();
        }

        displayShow("Mode", "IGATE", "", 1500);
    }
    #endif

    void processReceivedPacket(const String& frame) {
        if (frame.length() == 0) return;

        switch (Config.deviceRole) {
            case ROLE_DIGIPEATER:
                DIGI_Utils::processLoRaPacket(frame);
                break;
            case ROLE_IGATE:
                #ifdef HAS_WIFI
                    APRS_IS_Utils::processLoRaPacket(frame);
                    TCP_KISS_Utils::sendToClients(frame);
                #endif
                break;
            case ROLE_TRACKER:
            default:
                // Tracker packet handling is done in the main loop (MSG_Utils etc.)
                break;
        }
    }

    void handleRoleSpecificTasks() {
        if (!roleInitialized) return;

        #ifdef HAS_WIFI
            if (Config.deviceRole == ROLE_IGATE) {
                WIFI_Utils::checkWiFi();
                APRS_IS_Utils::checkConnection();
                APRS_IS_Utils::listenAPRSIS();
                TCP_KISS_Utils::loop();
            }
        #endif
    }

    DeviceRole getCurrentRole() {
        return Config.deviceRole;
    }

    bool isRoleInitialized() {
        return roleInitialized;
    }

}
