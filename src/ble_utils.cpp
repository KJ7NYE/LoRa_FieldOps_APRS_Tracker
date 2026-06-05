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

/*
 * BLE KISS TNC — platform-dual implementation.
 *
 * Both platforms advertise the same APRS.fi GATT service (SERVICE_UUID below)
 * and use KISS (AX.25) framing only.  Compatible clients:
 *   iOS:     APRS.fi app
 *   Android: APRSDroid (BLE KISS TNC), APRS.fi
 *   PC:      any BLE KISS TNC client (e.g. Bluetooth LE serial tools)
 *
 * nRF52840 (Heltec T114):  Adafruit Bluefruit library (built into BSP).
 * ESP32 / ESP32-S3:        NimBLE-Arduino (h2zero/NimBLE-Arduino).
 */

#include <Arduino.h>
#include "configuration.h"
#include "lora_utils.h"
#include "kiss_utils.h"
#include "ble_utils.h"
#include "display.h"
#include "logger.h"

// APRS.fi GATT service — KISS TNC profile
#define BLE_SERVICE_UUID    "00000001-ba2a-46c9-ae49-01b0961f68bb"
#define BLE_CHAR_UUID_TX    "00000003-ba2a-46c9-ae49-01b0961f68bb"  // notify: device → phone
#define BLE_CHAR_UUID_RX    "00000002-ba2a-46c9-ae49-01b0961f68bb"  // write:  phone  → device

#define BLE_CHUNK_SIZE      512
#define MAX_KISS_BUFFER     1024

extern Configuration    Config;
extern logging::Logger  logger;
extern bool             bluetoothConnected;


// Binary-safe substring — String::substring() uses C-string assignment internally,
// which calls strlen() and truncates at the first 0x00 byte.  KISS Data frames
// carry 0x00 at byte 1 (the command field), so substring() returns a 1-byte string.
// This helper uses charAt() (direct buffer[i]) and concat(char) (memcpy of 1 byte),
// both of which respect the String's internal `len` field rather than strlen().
static String binarySubstring(const String& s, int start, int end) {
    String out;
    out.reserve(end - start + 1);
    for (int i = start; i < end; i++) out.concat(s.charAt(i));
    return out;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  nRF52840  —  Adafruit Bluefruit (built into Adafruit nRF52 BSP)
 * ══════════════════════════════════════════════════════════════════════════ */
#ifdef ARDUINO_ARCH_NRF52

#include <bluefruit.h>

static BLEService        aprsService(BLE_SERVICE_UUID);
static BLECharacteristic txChar(BLE_CHAR_UUID_TX);
static BLECharacteristic rxChar(BLE_CHAR_UUID_RX);

static String   nrf52KissBuffer     = "";
static bool     shouldSendToLoRa    = false;
static String   kissPacketToLoRa    = "";

static void connectCallback(uint16_t conn_handle) {
    bluetoothConnected = true;
    logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BLE", "Client connected");
}

static void disconnectCallback(uint16_t conn_handle, uint8_t reason) {
    bluetoothConnected = false;
    logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BLE", "Client disconnected, restarting advertising");
    Bluefruit.Advertising.start(0);
}

static void rxWriteCallback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        nrf52KissBuffer.concat((char)data[i]);
    }
    if (nrf52KissBuffer.length() > MAX_KISS_BUFFER) {
        nrf52KissBuffer = "";
        return;
    }

    int maxIterations = 10;
    while (maxIterations-- > 0) {
        if (nrf52KissBuffer.length() == 0) break;

        int fendIndex = -1;
        if (nrf52KissBuffer.charAt(0) == (char)KissChar::FEND) {
            for (int i = 1; i < (int)nrf52KissBuffer.length(); i++) {
                if (nrf52KissBuffer.charAt(i) == (char)KissChar::FEND) {
                    fendIndex = i;
                    break;
                }
            }
        } else {
            // Buffer doesn't start with FEND — scan for the next one.
            // indexOf() uses strchr() which also stops at 0x00, so scan manually.
            int firstFend = -1;
            for (int i = 0; i < (int)nrf52KissBuffer.length(); i++) {
                if (nrf52KissBuffer.charAt(i) == (char)KissChar::FEND) {
                    firstFend = i; break;
                }
            }
            if (firstFend != -1) {
                nrf52KissBuffer.remove(0, firstFend);
            } else {
                nrf52KissBuffer = "";
                break;
            }
            continue;
        }

        if (fendIndex == -1) break;   // incomplete frame — wait for more data

        // Extract the FEND…FEND frame without using substring() which truncates at 0x00.
        String frame = binarySubstring(nrf52KissBuffer, 0, fendIndex + 1);
        nrf52KissBuffer.remove(0, fendIndex + 1);

        if (frame.length() >= 4) {
            bool isDataFrame = false;
            kissPacketToLoRa = KISS_Utils::decodeKISS(frame, isDataFrame);
            if (isDataFrame) shouldSendToLoRa = true;
        }
    }
}

namespace BLE_Utils {

    void stop() {
        Bluefruit.Advertising.stop();
        // Full deinit not available on nRF52 without resetting SoftDevice
    }

    void setup() {
        Bluefruit.begin();
        // Disable the BSP's automatic LED blinking — it uses LED_BLUE (aliased to
        // PIN_LED1 = P1.03, the green LED) for advertising/connection state, which
        // conflicts with our own LED management in led_utils.cpp.
        Bluefruit.autoConnLed(false);
        Bluefruit.setName(Config.bluetooth.deviceName.c_str());
        Bluefruit.Periph.setConnectCallback(connectCallback);
        Bluefruit.Periph.setDisconnectCallback(disconnectCallback);

        aprsService.begin();

        // Max length must be set explicitly — the Adafruit library defaults _max_len
        // to 0, which causes notify() to silently return false for any non-empty
        // payload, and causes Write Requests to be rejected by the SoftDevice when
        // Android sends a full-sized KISS frame after MTU negotiation.
        // 512 bytes comfortably covers the largest APRS KISS frame; the actual
        // per-packet limit is min(512, ATT_MTU - 3) which is 244 bytes at MTU=247.
        txChar.setProperties(CHR_PROPS_NOTIFY);
        txChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
        txChar.setMaxLen(512);
        txChar.begin();

        rxChar.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
        rxChar.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
        rxChar.setMaxLen(512);
        rxChar.setWriteCallback(rxWriteCallback);
        rxChar.begin();

        Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
        Bluefruit.Advertising.addTxPower();
        Bluefruit.Advertising.addService(aprsService);
        Bluefruit.ScanResponse.addName();
        Bluefruit.Advertising.restartOnDisconnect(true);
        Bluefruit.Advertising.setInterval(32, 244);
        Bluefruit.Advertising.setFastTimeout(30);
        Bluefruit.Advertising.start(0);

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BLE",
                   "Advertising as \"%s\" (APRS.fi KISS)", Config.bluetooth.deviceName.c_str());
    }

    void sendToLoRa() {
        if (!shouldSendToLoRa) return;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "BLE TX", "%s", kissPacketToLoRa.c_str());
        LoRa_Utils::sendNewPacket(kissPacketToLoRa);   // displayTx() fires inside sendNewPacket
        kissPacketToLoRa    = "";
        shouldSendToLoRa    = false;
    }

    void sendToPhone(const String& packet) {
        if (packet.length() == 0 || !bluetoothConnected) return;
        const String kissFrame = KISS_Utils::encodeKISS(packet);
        txChar.notify((const uint8_t*)kissFrame.c_str(), kissFrame.length());
    }

}   // namespace BLE_Utils


/* ══════════════════════════════════════════════════════════════════════════
 *  ESP32 / ESP32-S3  —  NimBLE-Arduino
 * ══════════════════════════════════════════════════════════════════════════ */
#else

#include <NimBLEDevice.h>

static NimBLEServer*         pServer             = nullptr;
static NimBLECharacteristic* pCharacteristicTx   = nullptr;
static NimBLECharacteristic* pCharacteristicRx   = nullptr;

static bool     shouldSendBLEtoLoRa = false;
static String   BLEToLoRaPacket     = "";
static String   kissSerialBuffer    = "";

class MyServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) override {
        bluetoothConnected = true;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BLE", "Client connected");
        delay(100);
    }
    void onDisconnect(NimBLEServer* pServer) override {
        bluetoothConnected = false;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BLE", "Client disconnected, restarting advertising");
        delay(100);
        pServer->startAdvertising();
    }
};

class MyCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) override {
        std::string receivedData = pCharacteristic->getValue();

        for (uint8_t c : receivedData) {
            kissSerialBuffer += (char)c;
        }
        if (kissSerialBuffer.length() > MAX_KISS_BUFFER) {
            kissSerialBuffer = "";
            return;
        }

        int maxIterations = 10;
        while (maxIterations-- > 0) {
            if (kissSerialBuffer.length() == 0) break;

            int fendIndex = -1;
            if (kissSerialBuffer.charAt(0) == (char)KissChar::FEND) {
                for (int i = 1; i < (int)kissSerialBuffer.length(); i++) {
                    if (kissSerialBuffer.charAt(i) == (char)KissChar::FEND) {
                        fendIndex = i;
                        break;
                    }
                }
            } else {
                // indexOf() uses strchr() which stops at 0x00 — scan manually.
                int firstFend = -1;
                for (int i = 0; i < (int)kissSerialBuffer.length(); i++) {
                    if (kissSerialBuffer.charAt(i) == (char)KissChar::FEND) {
                        firstFend = i; break;
                    }
                }
                if (firstFend != -1) {
                    kissSerialBuffer.remove(0, firstFend);
                } else {
                    kissSerialBuffer = "";
                    break;
                }
                continue;
            }

            if (fendIndex == -1) break;

            // Use binarySubstring() — substring() truncates at 0x00 (KISS cmd byte).
            String frame = binarySubstring(kissSerialBuffer, 0, fendIndex + 1);
            kissSerialBuffer.remove(0, fendIndex + 1);

            if (frame.length() >= 4) {
                bool isDataFrame    = false;
                BLEToLoRaPacket     = KISS_Utils::decodeKISS(frame, isDataFrame);
                if (isDataFrame) shouldSendBLEtoLoRa = true;
            }
        }
    }
};

namespace BLE_Utils {

    void stop() {
        NimBLEDevice::deinit();
    }

    void setup() {
        NimBLEDevice::init(Config.bluetooth.deviceName.c_str());
        pServer = NimBLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());

        NimBLEService* pService = pServer->createService(BLE_SERVICE_UUID);
        pCharacteristicTx = pService->createCharacteristic(
            BLE_CHAR_UUID_TX,
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
        pCharacteristicRx = pService->createCharacteristic(
            BLE_CHAR_UUID_RX,
            NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
        pCharacteristicRx->setCallbacks(new MyCallbacks());

        pService->start();

        NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
        pServer->getAdvertising()->setScanResponse(true);
        pServer->getAdvertising()->setMinPreferred(0x06);
        pServer->getAdvertising()->setMaxPreferred(0x0C);
        pAdvertising->start();

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BLE",
                   "Advertising as \"%s\" (APRS.fi KISS)", Config.bluetooth.deviceName.c_str());
    }

    void sendToLoRa() {
        if (!shouldSendBLEtoLoRa) return;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "BLE TX", "%s", BLEToLoRaPacket.c_str());
        LoRa_Utils::sendNewPacket(BLEToLoRaPacket);   // displayTx() fires inside sendNewPacket
        BLEToLoRaPacket         = "";
        shouldSendBLEtoLoRa     = false;
    }

    void sendToPhone(const String& packet) {
        if (packet.length() == 0 || !bluetoothConnected) return;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "BLE RX", "%s", packet.c_str());
        const String kissFrame = KISS_Utils::encodeKISS(packet);
        const char* t   = kissFrame.c_str();
        int length      = kissFrame.length();
        for (int i = 0; i < length; i += BLE_CHUNK_SIZE) {
            int chunkSize = min(BLE_CHUNK_SIZE, length - i);
            uint8_t* chunk = new uint8_t[chunkSize];
            memcpy(chunk, t + i, chunkSize);
            pCharacteristicTx->setValue(chunk, chunkSize);
            pCharacteristicTx->notify();
            delete[] chunk;
            delay(200);
        }
    }

}   // namespace BLE_Utils

#endif  // ARDUINO_ARCH_NRF52 / ESP32
