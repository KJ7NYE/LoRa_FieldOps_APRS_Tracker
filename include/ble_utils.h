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

#ifndef BLE_UTILS_H_
#define BLE_UTILS_H_

#include <Arduino.h>

// BLE configuration link: the serial setup CLI carried over a Nordic UART
// Service (NUS) GATT service so the FieldOps app can configure the tracker
// without a USB cable. Built on every BLE-capable board unless a variant opts
// out with -D DISABLE_BLE_CONFIG. At runtime the service is only registered
// when BLE PIN pairing is required, so a PIN-less tracker can never be
// reconfigured over the air (and its GATT table is unchanged).
#if (defined(ARDUINO_ARCH_NRF52) || defined(HAS_NIMBLE)) && !defined(DISABLE_BLE_CONFIG)
    #define HAS_BLE_CONFIG
#endif

// BLE firmware update for ESP32 boards (protocol in include/ble_ota.h). Same
// containment as the config link: the GATT service is registered only when
// BLE PIN pairing is required, and only on boards whose partition table has a
// second app slot. A variant can opt out with -D DISABLE_BLE_OTA. The nRF52
// (T114) instead exposes the Nordic DFU service; see ble_utils.cpp.
#if defined(HAS_NIMBLE) && !defined(ARDUINO_ARCH_NRF52) && !defined(DISABLE_BLE_OTA)
    #define HAS_BLE_OTA
#endif


namespace BLE_Utils {

    void stop();
    void setup();
    void sendToLoRa();
    // Sends `packet` (TNC2) as a KISS data frame. `trailingKissFrame`, if
    // given, is an already-encoded KISS frame (the RX signal report) written
    // in the same call right after it, so nothing can land between the two.
    // Nothing is sent if `packet` isn't valid TNC2.
    void sendToPhone(const String& packet, const String& trailingKissFrame = "");

    #ifdef HAS_BLE_CONFIG
    // True while a PIN-authenticated phone is subscribed to the config TX
    // characteristic. Only meaningful from the main loop.
    bool configLinkOpen();

    // Incremented every time a config link opens, so the CLI can tell a
    // reconnect apart from a link that never dropped between two loop passes.
    uint32_t configLinkGeneration();

    // Next byte the phone wrote to the config RX characteristic, or -1.
    int configRead();

    // Reply stream for the CLI. Bytes are queued and sent as MTU-sized
    // notifications by serviceConfigLink(); when the queue fills, write()
    // drains it in place (bounded wait) instead of dropping output.
    Print& configOutput();

    // Main-loop pump: sends queued reply bytes. Cheap when idle.
    void serviceConfigLink();

    // Blocks until queued reply bytes have been handed to the BLE stack, the
    // link drops, or `timeoutMs` elapses. Used before a reboot so the phone
    // sees the final "Rebooting..." line.
    void flushConfigOutput(uint32_t timeoutMs);
    #endif

    #ifdef HAS_BLE_OTA
    // Transport hooks for BLE_OTA (src/ble_ota.cpp).

    // Identifies the current PIN-authenticated phone connection: a non-zero
    // value that changes on every new connection, or 0 when no authenticated
    // phone is connected. An update session is bound to the value it started on.
    uint32_t otaLinkId();

    // Sends one status notification on the update control characteristic.
    // Payloads longer than the link allows are truncated. Returns false when
    // the stack is out of buffers (retry later) or the link is down.
    bool otaSendStatus(const uint8_t* data, size_t length);

    // Asks the phone for a link tuned for bulk transfer (short connection
    // interval, 251-byte link-layer packets, 2M PHY) while an update runs, or
    // for a relaxed interval again afterwards. Requests only: the phone
    // decides, and a refusal just means a slower transfer.
    void otaSetFastLink(bool fast);
    #endif

}

#endif