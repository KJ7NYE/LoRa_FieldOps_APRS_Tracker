/* Copyright (C) 2026 Ricardo Guzman - CA2RXU
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

#ifndef BLUETOOTH_UTILS_H
#define BLUETOOTH_UTILS_H

#include <BluetoothSerial.h>


namespace BLUETOOTH_Utils {

    void setup();
    void bluetoothCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);
    void getData(const uint8_t *buffer, size_t size);
    void sendToLoRa();
    // Sends `packet` (TNC2) as a KISS data frame. `trailingKissFrame`, if
    // given, is an already-encoded KISS frame (the RX signal report) written
    // in the same call right after it, so nothing can land between the two.
    // Nothing is sent if `packet` isn't valid TNC2.
    void sendToPhone(const String& packet, const String& trailingKissFrame = "");

}

#endif
