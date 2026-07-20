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
#include <algorithm>
#include "configuration.h"
#include "wifi_utils.h"
#include "web_utils.h"
#include "display.h"
#include "serial_setup.h"

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

    int scanNetworks(std::vector<ScanResult>& out, int maxResults) {
        out.clear();

        // Pure WIFI_AP mode cannot scan on ESP32 -- scanning requires STA to
        // be enabled. If the config AP is currently hosting, promote to
        // WIFI_AP_STA so the AP keeps running while we scan. Deliberately
        // never demote back to WIFI_AP afterward: repeatedly switching modes
        // on every scan is riskier (AP client drop, LWIP/AP state corruption)
        // than just staying promoted -- mode resets naturally next boot via
        // startAutoAP()'s WIFI_MODE_NULL reset.
        wifi_mode_t mode = WiFi.getMode();
        if (mode == WIFI_MODE_AP) {
            WiFi.mode(WIFI_MODE_APSTA);
        }

        int n = WiFi.scanNetworks(false, false, false, 300);
        if (n <= 0) {
            WiFi.scanDelete();
            return 0;
        }

        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue;   // skip hidden/blank SSIDs
            int32_t rssi = WiFi.RSSI(i);
            bool secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;

            bool merged = false;
            for (auto& r : out) {
                if (r.ssid == ssid) {
                    if (rssi > r.rssi) { r.rssi = rssi; r.secure = secure; }
                    merged = true;
                    break;
                }
            }
            if (!merged) out.push_back({ssid, rssi, secure});
        }
        WiFi.scanDelete();

        std::sort(out.begin(), out.end(), [](const ScanResult& a, const ScanResult& b) {
            return a.rssi > b.rssi;
        });
        if ((int)out.size() > maxResults) out.resize(maxResults);

        return (int)out.size();
    }

    int nextConfiguredNetwork(size_t fromIndex) {
        size_t count = Config.wifiSTA.networks.size();
        if (count == 0) return -1;
        for (size_t i = 0; i < count; i++) {
            size_t idx = (fromIndex + i) % count;
            if (Config.wifiSTA.networks[idx].ssid.length() > 0) return (int)idx;
        }
        return -1;
    }

    bool beginSTAConnect(size_t index) {
        if (index >= Config.wifiSTA.networks.size()) return false;
        const WiFiNetwork& net = Config.wifiSTA.networks[index];
        if (net.ssid.length() == 0) return false;

        // Build DHCP hostname: CALLSIGN-last4ofMAC  (e.g. "KJ7NYE-7-A1B2")
        // Must be set after WIFI_STA mode but before WiFi.begin().
        uint8_t mac[6];
        WiFi.mode(WIFI_STA);
        WiFi.macAddress(mac);
        char hostname[36];
        snprintf(hostname, sizeof(hostname), "%s-%02X%02X",
                 Config.beacons[0].callsign.c_str(), mac[4], mac[5]);
        WiFi.setHostname(hostname);

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "WiFi",
                   "Connecting to '%s' (slot %u) as '%s' (async)...",
                   net.ssid.c_str(), (unsigned)index, hostname);
        WiFi.begin(net.ssid.c_str(), net.password.c_str());
        return true;
    }

    bool connectSTA() {
        int idx = nextConfiguredNetwork(0);
        if (idx < 0) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "WiFi", "No STA networks configured");
            return false;
        }
        beginSTAConnect((size_t)idx);

        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000UL) {
            delay(500);
        }
        if (WiFi.status() == WL_CONNECTED) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "WiFi", "Connected to '%s', IP: %s",
                       Config.wifiSTA.networks[idx].ssid.c_str(), WiFi.localIP().toString().c_str());
            bootStatus("WiFi STA connected");
            return true;
        }
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "WiFi",
                   "STA connect failed (status %d)", (int)WiFi.status());
        WiFi.disconnect(true);   // clean stop; tickWiFiReconnect handles retry
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
            // Poll serial CLI so USB config works while AP mode is blocking.
            uint32_t tick = millis();
            while (millis() - tick < 500) {
                SERIAL_Setup::loop();
                delay(10);
            }
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