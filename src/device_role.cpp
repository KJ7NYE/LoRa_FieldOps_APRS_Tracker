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
#include "station_utils.h"
#include "logger.h"
#include "display.h"
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
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Role", "iGate mode: APRS-IS relay enabled");

        // Connect WiFi STA
        if (Config.wifiSTA.enabled && Config.wifiSTA.ssid.length() > 0) {
            WIFI_Utils::connectSTA();
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Role", "WiFi STA not configured — iGate will not reach APRS-IS");
        }

        // Connect APRS-IS
        if (WIFI_Utils::isSTAConnected()) {
            APRS_IS_Utils::connect();
        }

        // Start TCP KISS server
        if (Config.tcpKISS.enabled) {
            TCP_KISS_Utils::setup();
        }

        displayShow("Mode", "IGATE", Config.beacons[0].callsign, 2000);
    }
    #endif

    void handleRoleSpecificTasks() {
        if (!roleInitialized) return;

        // iGate and Digipeater beacon their own position on a fixed interval
        // so they appear on APRS maps. Tracker beaconing is driven by the main loop.
        if (Config.deviceRole == ROLE_IGATE || Config.deviceRole == ROLE_DIGIPEATER) {
            static uint32_t lastSelfBeacon = 0;
            uint32_t beaconInterval = (uint32_t)Config.nonSmartBeaconRate * 60000UL;
            if (beaconInterval < 60000UL) beaconInterval = 60000UL;  // floor at 1 min
            if (millis() - lastSelfBeacon >= beaconInterval) {
                STATION_Utils::sendBeacon();
                lastSelfBeacon = millis();
            }
        }

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
                // Digipeater packet processing handled in main loop via MSG_Utils
                break;
            default:
                break;
        }
    }

    #ifdef HAS_WIFI
    void handleIGateTasks() {
        // Maintain WiFi + APRS-IS connection
        if (!WIFI_Utils::isSTAConnected() && Config.wifiSTA.enabled) {
            WIFI_Utils::connectSTA();
        }
        APRS_IS_Utils::checkConnection();

        // Poll inbound APRS-IS packets (bi-directional downlink)
        APRS_IS_Utils::listenAPRSIS();

        // Service TCP KISS clients
        TCP_KISS_Utils::loop();
    }
    #endif

    DeviceRole getCurrentRole() {
        return Config.deviceRole;
    }

    bool isRoleInitialized() {
        return roleInitialized;
    }

}
