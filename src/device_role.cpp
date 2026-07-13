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

// lastTxTime is the single source of truth for beacon spacing — updated inside
// every sendBeacon() call regardless of which path triggered it.
extern uint32_t lastTxTime;
#ifdef HAS_WIFI
#include <WiFi.h>
#include "aprs_is_utils.h"
#include "tcp_kiss_utils.h"
#include "wifi_utils.h"
#ifdef HAS_WEB_UI
#include "web_utils.h"
#endif
#endif

extern Configuration Config;
extern logging::Logger logger;

namespace DeviceRoleUtils {

    static bool roleInitialized = false;
    static uint32_t phgLastTx = 0;   // millis() of last PHG beacon TX (0 = never sent)
    #ifdef HAS_WIFI
    static bool networkServicesStarted = false;
    #endif

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
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "DeviceRole", "iGate role not supported on nRF52 (no WiFi)");
                return false;
            }
        #endif
        return true;
    }

    void initializeRole(DeviceRole role) {
        if (!validateRoleForPlatform(role)) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "DeviceRole", "Invalid role for this platform, defaulting to Tracker");
            Config.deviceRole = ROLE_TRACKER;
            Config.writeFile();
            return;
        }

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "DeviceRole", "Initializing device role: %s", getRoleString(role));

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
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "DeviceRole", "Unknown device role");
                break;
        }

        roleInitialized = true;
    }

    void initializeTracker() {
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "DeviceRole", "Tracker mode: GPS + smart beaconing enabled, messaging active");
        bootStatus("Tracker");
        #ifdef HAS_WIFI
        initializeWiFiSTA();
        #endif
    }

    void initializeDigipeater() {
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "DeviceRole", "Digipeater mode: RF relaying enabled, beaconing disabled");
        bootStatus("Digipeater");
        #ifdef HAS_WIFI
        initializeWiFiSTA();
        #endif
    }

    #ifdef HAS_WIFI
    // Starts TCP KISS and the web UI exactly once — idempotent via the
    // networkServicesStarted flag.  Called both at boot (if WiFi connects
    // immediately) and at runtime (when tickWiFiReconnect fires for the
    // first time after a delayed or resumed connection).
    static void startNetworkServices() {
        if (networkServicesStarted) return;
        networkServicesStarted = true;
        TCP_KISS_Utils::setup();
        #ifdef HAS_WEB_UI
        WEB_Utils::setup();
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Role",
                   "Web config: http://%s", WiFi.localIP().toString().c_str());
        #endif
    }

    void initializeWiFiSTA() {
        if (!Config.wifiSTA.enabled || Config.wifiSTA.ssid.length() == 0) return;

        WIFI_Utils::connectSTA();

        if (WIFI_Utils::isSTAConnected()) {
            startNetworkServices();
        }
    }

    void initializeIGate() {
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Role", "iGate mode: APRS-IS relay enabled");

        if (Config.wifiSTA.enabled && Config.wifiSTA.ssid.length() > 0) {
            WIFI_Utils::connectSTA();
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Role", "WiFi STA not configured — iGate will not reach APRS-IS");
        }

        if (WIFI_Utils::isSTAConnected()) {
            APRS_IS_Utils::connect();
            startNetworkServices();
        }

        bootStatus("iGate");
    }
    #endif

    void handleRoleSpecificTasks() {
        if (!roleInitialized) return;

        // iGate and Digipeater beacon their own position on a fixed interval
        // so they appear on APRS maps. Tracker beaconing is driven by the main loop.
        //
        // Use lastTxTime (updated inside every sendBeacon() call) as the single
        // timing reference. Using a private timer here caused a double-beacon: the
        // GPS path or a button press could fire sendBeacon() and advance lastTxTime
        // without resetting the private counter, so this block would fire again on
        // the very next loop iteration.
        if (Config.deviceRole == ROLE_IGATE || Config.deviceRole == ROLE_DIGIPEATER) {
            // Beacon immediately after each APRS-IS (re)connect.
            #ifdef HAS_WIFI
            if (APRS_IS_Utils::consumeBeaconTrigger()) {
                STATION_Utils::sendBeacon();
            }
            #endif

            uint32_t beaconInterval = (uint32_t)Config.nonSmartBeaconRate * 60000UL;
            if (beaconInterval < 60000UL) beaconInterval = 60000UL;  // floor at 1 min
            if (millis() - lastTxTime >= beaconInterval) {
                STATION_Utils::sendBeacon();
            }
        }

        // PHG beacon fires on its own timer, independent of regular beacon cadence.
        // Available for any role; most useful for fixed iGate / digipeater stations.
        if (Config.phg.enabled) {
            uint32_t phgInterval = (uint32_t)Config.phg.beaconRate * 60000UL;
            if (phgInterval < 60000UL) phgInterval = 60000UL;  // floor at 1 min
            if (phgLastTx == 0 || millis() - phgLastTx >= phgInterval) {
                STATION_Utils::sendPHGBeacon();
                phgLastTx = millis();
            }
        }

        switch (Config.deviceRole) {
            case ROLE_TRACKER:
                #ifdef HAS_WIFI
                if (Config.wifiSTA.enabled) handleWiFiTasks();
                #endif
                break;
            case ROLE_IGATE:
                #ifdef HAS_WIFI
                    handleIGateTasks();
                #endif
                break;
            case ROLE_DIGIPEATER:
                #ifdef HAS_WIFI
                if (Config.wifiSTA.enabled) handleWiFiTasks();
                #endif
                break;
            default:
                break;
        }
    }

    #ifdef HAS_WIFI
    // Non-blocking WiFi reconnect state machine shared by Tracker and Digipeater roles.
    // Calls beginSTAConnect() once per WIFI_RETRY_INTERVAL_MS; polls isSTAConnected()
    // each loop iteration; gives up after WIFI_CONNECT_TIMEOUT_MS and waits for the
    // next interval.  Never blocks the main loop.
    static bool     wifiConnecting          = false;
    static uint32_t wifiConnectTimestamp    = 0;   // start of current attempt OR end of last attempt

    static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS  = 15000UL;  // per-attempt window
    static constexpr uint32_t WIFI_RETRY_INTERVAL_MS   = 30000UL;  // cooldown between attempts

    // Returns true on the loop iteration that WiFi first becomes connected.
    static bool tickWiFiReconnect() {
        if (WIFI_Utils::isSTAConnected()) {
            if (wifiConnecting) {
                wifiConnecting = false;
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "WiFi", "Reconnected — IP: %s",
                           WiFi.localIP().toString().c_str());
                return true;   // caller can react to the transition
            }
            return false;
        }

        // Not connected.
        if (wifiConnecting) {
            if (millis() - wifiConnectTimestamp > WIFI_CONNECT_TIMEOUT_MS) {
                wifiConnecting = false;
                WiFi.disconnect(true);
                wifiConnectTimestamp = millis();   // start cooldown
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "WiFi",
                           "Connect attempt timed out, retrying in %us", WIFI_RETRY_INTERVAL_MS / 1000);
            }
        } else {
            if (millis() - wifiConnectTimestamp >= WIFI_RETRY_INTERVAL_MS) {
                WIFI_Utils::beginSTAConnect();
                wifiConnectTimestamp = millis();
                wifiConnecting = true;
            }
        }
        return false;
    }

    void handleWiFiTasks() {
        bool justConnected = tickWiFiReconnect();
        if (justConnected) startNetworkServices();
        TCP_KISS_Utils::loop();
    }

    void handleIGateTasks() {
        bool justConnected = tickWiFiReconnect();

        if (WIFI_Utils::isSTAConnected()) {
            if (justConnected) {
                // Reset APRS-IS cooldown immediately on WiFi (re)connect so we don't
                // wait up to 30 s more before attempting the TCP connection.
                APRS_IS_Utils::resetConnectTimer();
                startNetworkServices();
            }
            APRS_IS_Utils::checkConnection();
            APRS_IS_Utils::listenAPRSIS();
        }
        // TCP KISS server binds to 0.0.0.0 and is managed independently of WiFi state.
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
