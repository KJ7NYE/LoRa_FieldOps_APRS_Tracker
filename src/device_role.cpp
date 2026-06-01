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

extern Configuration Config;
extern logging::Logger logger;

namespace DeviceRoleUtils {

    static bool roleInitialized = false;

    const char* getRoleString(DeviceRole role) {
        switch (role) {
            case ROLE_TRACKER:
                return "Tracker";
            case ROLE_IGATE:
                return "iGate";
            case ROLE_DIGIPEATER:
                return "Digipeater";
            default:
                return "Unknown";
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
        Serial.println("INFO: Tracker mode: GPS + smart beaconing enabled, messaging active");
        displayShow("Mode", "TRACKER", "", 2000);
    }

    void initializeDigipeater() {
        Serial.println("INFO: Digipeater mode: RF relaying enabled, beaconing disabled");
        displayShow("Mode", "DIGIPEATER", "", 2000);
    }

    #ifdef HAS_WIFI
    void initializeIGate() {
        Serial.println("INFO: iGate mode: APRS-IS relay enabled");

        if (Config.aprsIS.server.length() == 0) {
            Serial.println("WARN: APRS-IS server not configured");
        }

        if (Config.wifiSTA.ssid.length() == 0) {
            Serial.println("WARN: WiFi STA not configured, iGate will not connect");
        }

        displayShow("Mode", "IGATE", "", 2000);
    }
    #endif

    void handleRoleSpecificTasks() {
        if (!roleInitialized) return;

        switch (Config.deviceRole) {
            case ROLE_TRACKER:
                // Tracker tasks handled in main loop
                break;
            case ROLE_IGATE:
                #ifdef HAS_WIFI
                    handleIGateTasks();
                #endif
                break;
            case ROLE_DIGIPEATER:
                // Digipeater tasks handled in main loop
                break;
            default:
                break;
        }
    }

    #ifdef HAS_WIFI
    void handleIGateTasks() {
        // Poll APRS-IS for incoming packets
        // Handle TCP KISS clients
        // Manage WiFi connection state
    }
    #endif

    DeviceRole getCurrentRole() {
        return Config.deviceRole;
    }

    bool isRoleInitialized() {
        return roleInitialized;
    }

}
