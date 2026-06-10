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


namespace WIFI_Utils {

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