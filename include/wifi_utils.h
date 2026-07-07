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

#ifndef WIFI_UTILS_H_
#define WIFI_UTILS_H_

#include <Arduino.h>
#include <vector>


namespace WIFI_Utils {

    struct ScanResult {
        String  ssid;
        int32_t rssi;
        bool    secure;
    };

    // Runs a blocking WiFi.scanNetworks() (~2-4 s). Promotes WIFI_AP to
    // WIFI_AP_STA first if the config AP is currently hosting (pure AP mode
    // cannot scan), and deliberately does not demote back afterward -- see
    // scanNetworks() in wifi_utils.cpp for why. Only call from a
    // user-triggered request/command handler, never from loop()/tick paths.
    // Populates out (cleared first), deduplicated by SSID (strongest RSSI
    // kept), sorted by RSSI descending, capped at maxResults. Hidden/blank
    // SSIDs are skipped. Returns the number of entries written.
    int scanNetworks(std::vector<ScanResult>& out, int maxResults = 20);

    void startAutoAP();

    // Check boot triggers and enter AP config mode if needed.
    // buttonHeld: true if the USR button was held when setup() ran.
    // Blocks until the last client has been gone for 2 minutes, then reboots.
    // Returns immediately (no AP started) if neither trigger is active.
    void checkIfWiFiAP(bool buttonHeld);

    // Connect to STA (infrastructure) network using Config.wifiSTA credentials.
    // Blocks up to 10 s; logs result. Returns true on successful association.
    // Use only at boot where a one-time blocking wait is acceptable.
    bool connectSTA();

    // Non-blocking: kick off WiFi association and return immediately.
    // Caller must poll isSTAConnected() each loop to detect success or timeout.
    void beginSTAConnect();

    // True when the station interface is associated and has an IP.
    bool isSTAConnected();

}

#endif