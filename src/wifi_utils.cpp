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

#include <logger.h>
#include <WiFi.h>
#include "configuration.h"
#include "web_utils.h"
#include "display.h"

extern      Configuration       Config;
extern      logging::Logger     logger;

static constexpr char     AP_SSID[]       = "LoRaTracker-AP";
static constexpr uint32_t AP_IDLE_TIMEOUT = 2UL * 60UL * 1000UL;   // 2 minutes


namespace WIFI_Utils {

    void startAutoAP() {
        WiFi.mode(WIFI_MODE_NULL);
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, Config.wifiAP.password);
    }

    bool isSTAConnected() {
        return WiFi.status() == WL_CONNECTED;
    }

    bool connectSTA() {
        if (Config.wifiSTA.ssid.length() == 0) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "WiFi", "STA SSID not configured");
            return false;
        }
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "WiFi", "Connecting to '%s' ...", Config.wifiSTA.ssid.c_str());
        WiFi.mode(WIFI_STA);
        WiFi.begin(Config.wifiSTA.ssid.c_str(), Config.wifiSTA.password.c_str());

        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000UL) {
            delay(500);
        }
        if (WiFi.status() == WL_CONNECTED) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "WiFi", "Connected, IP: %s", WiFi.localIP().toString().c_str());
            bootStatus("WiFi STA connected");
            return true;
        }
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "WiFi", "STA connect failed");
        return false;
    }

    void checkIfWiFiAP(bool buttonHeld) {
        const bool isNoCall = (Config.beacons[0].callsign == "NOCALL-7");

        if (!isNoCall && !buttonHeld) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Main", "AP mode not triggered, skipping");
            return;
        }

        const char* reason = isNoCall ? "NOCALL callsign" : "USR button held";
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Main", "AP mode: %s", reason);

        startAutoAP();
        WEB_Utils::setup();

        bootStatus("WiFi AP: 192.168.4.1");
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Main", "WebConfiguration started");
        displayAPMode(AP_SSID, Config.wifiAP.password);

        uint32_t noClientsTime = 0;

        while (true) {
            delay(500);
            displayAPMode(AP_SSID, Config.wifiAP.password);

            if (WiFi.softAPgetStationNum() > 0) {
                // Client is connected — reset idle timer.
                noClientsTime = 0;
            } else {
                // No clients.
                if (noClientsTime == 0) {
                    noClientsTime = millis();
                } else if ((millis() - noClientsTime) > AP_IDLE_TIMEOUT) {
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Main",
                               "AP mode: no clients for 2 min, rebooting");
                    WiFi.softAPdisconnect(true);
                    ESP.restart();
                }
            }
        }
    }
}