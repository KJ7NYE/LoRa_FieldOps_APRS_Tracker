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

/*
 * Bluetooth Classic SPP KISS TNC.
 * Used only on boards where HAS_BT_CLASSIC is defined and HAS_NIMBLE is not
 * (i.e. plain ESP32 boards without NimBLE in their build flags).
 * Always uses KISS (AX.25) framing — compatible with APRSDroid, Pinpoint, etc.
 *
 * The entire translation unit is guarded by HAS_BT_CLASSIC. Arduino-ESP32
 * 3.x's BluetoothSerial.h is a soft-fail stub on chips without BT Classic
 * hardware (ESP32-S3 / -C3 / -C6 / -H2): the header still defines the
 * BluetoothSerial type for forward compatibility but its member functions
 * aren't linkable, so referencing a SerialBT object would fail at link time
 * (and in -Werror builds, at compile time). The guard keeps the whole file
 * out of the build on those boards, where the shared NimBLE path handles BT.
 */

#include "board_pinout.h"   // defines HAS_BT_CLASSIC per-variant

#ifdef HAS_BT_CLASSIC

#include "bluetooth_utils.h"   // pulls in <BluetoothSerial.h>; S3/C3/etc. ship a stub
#include <esp_bt.h>
#include "configuration.h"
#include "lora_utils.h"
#include "query_utils.h"
#include "kiss_utils.h"
#include "display.h"
#include "logger.h"


extern Configuration    Config;
extern BluetoothSerial  SerialBT;
extern logging::Logger  logger;
extern bool             bluetoothConnected;
extern bool             bluetoothActive;

namespace BLUETOOTH_Utils {

    static String   serialReceived;
    static bool     shouldSendToLoRa = false;

    void setup() {
        if (!bluetoothActive) {
            btStop();
            esp_bt_controller_disable();
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BT", "Controller disabled (bluetooth.active = false)");
            return;
        }

        serialReceived.reserve(255);

        SerialBT.register_callback(BLUETOOTH_Utils::bluetoothCallback);
        SerialBT.onData(BLUETOOTH_Utils::getData);

        if (!SerialBT.begin(Config.bluetooth.deviceName)) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "BT", "Starting Bluetooth Classic failed!");
            bootStatus("ERROR: BT failed!");
            while (true) { delay(1000); }
        }
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BT",
                   "Classic KISS TNC ready as \"%s\"", Config.bluetooth.deviceName.c_str());
    }

    void bluetoothCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
        if (event == ESP_SPP_SRV_OPEN_EVT) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BT", "Client connected");
            bluetoothConnected = true;
            #ifdef RPC_MicroTracker
                digitalWrite(13, LOW);    // Led BT/BLE
            #endif
        } else if (event == ESP_SPP_CLOSE_EVT) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BT", "Client disconnected");
            bluetoothConnected = false;
            #ifdef RPC_MicroTracker
                digitalWrite(13, HIGH);    // Led BT/BLE
            #endif
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "BT", "Event: %d", event);
        }
    }

    void getData(const uint8_t *buffer, size_t size) {
        if (size == 0) return;
        shouldSendToLoRa = false;
        serialReceived.clear();

        for (size_t i = 0; i < size; i++) {
            serialReceived += (char)buffer[i];
        }

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "BT RX", "Received %u bytes", (unsigned)size);

        // Validate as KISS frame; if so, decode and queue for TX
        if (KISS_Utils::validateKISSFrame(serialReceived)) {
            bool dataFrame = false;
            String decoded = KISS_Utils::decodeKISS(serialReceived, dataFrame);
            if (dataFrame && KISS_Utils::validateTNC2Frame(decoded)) {
                serialReceived = decoded;
                shouldSendToLoRa = true;
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "BT RX KISS",
                           "Queued for RF: %s", serialReceived.c_str());
            }
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "BT RX", "Not a valid KISS frame, discarding");
        }
    }

    void sendToLoRa() {
        if (!shouldSendToLoRa) return;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "BT TX", "%s", serialReceived.c_str());
        shouldSendToLoRa = false;
        if (QUERY_Utils::processKissHostPacket(serialReceived)) return;   // addressed to us: answered locally
        displayTx(serialReceived);   // show TX overlay with packet content
        LoRa_Utils::sendNewPacket(serialReceived);
    }

    void sendToPhone(const String& packet, const String& trailingKissFrame) {
        if (packet.isEmpty()) return;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "BT RX KISS", "%s", packet.c_str());
        const String dataFrame = KISS_Utils::encodeKISS(packet);
        if (dataFrame.length() == 0) return;   // not valid TNC2: nothing to send
        SerialBT.print(dataFrame + trailingKissFrame);   // one write keeps the pair together
    }

}

#endif  // HAS_BT_CLASSIC
