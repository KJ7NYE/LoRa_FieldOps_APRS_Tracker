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
#include "ble_pin.h"
#include "log_buffer.h"
#include <stdarg.h>

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
 *  PIN pairing policy  —  shared by both platforms
 *
 *  When Config.bluetooth.pinEnabled is set, the GATT characteristics require
 *  an authenticated (MITM) encrypted link, which phones can only obtain via
 *  Passkey Entry pairing using the configured PIN. bluetoothConnected — the
 *  flag that gates all KISS traffic in both directions — only goes true once
 *  that authenticated link exists.
 *
 *  Bonded phones skip Passkey Entry on reconnect by presenting a stored key.
 *  If the PIN were changed (or turned on), those keys would still let
 *  previously paired phones in, so we persist a fingerprint of the policy in
 *  effect when the bonds were created and wipe every bond whenever it differs
 *  at boot. Each platform supplies the three storage primitives declared here.
 * ══════════════════════════════════════════════════════════════════════════ */

// Connection / pairing events. The serial logger is muted while USB serial is
// in KISS mode (it only prints after `log`), so these are also pushed to the
// web UI live log (a no-op on boards without a web UI) to be visible from there.
static void bleEvent(logging::LoggerLevel level, const char* fmt, ...) {
    char message[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    logger.log(level, "BLE", "%s", message);
    LogBuffer::pushf(LogBuffer::TYPE_INFO, "BLE: %s", message);
}

// Policy snapshot taken at BLE startup; config edits take effect after reboot.
static bool pinRequired = false;
// 6-digit passkey + NUL. Static storage is required: the nRF52 SoftDevice
// keeps a pointer to this buffer rather than copying it.
static char blePasskeyDigits[BlePin::MAX_DIGITS + 1] = {0};

static bool readStoredPairingPolicy(String& policyOut);
static bool writeStoredPairingPolicy(const String& policy);
static bool deleteAllPhoneBonds();

// Validates the configured PIN and captures the policy for this boot.
// Returns false (BLE must stay off) when the PIN is enabled but malformed,
// rather than silently falling back to an open, unauthenticated link.
static bool loadPinPolicy() {
    pinRequired = Config.bluetooth.pinEnabled;
    blePasskeyDigits[0] = '\0';
    if (!pinRequired) return true;
    if (!BlePin::isValid(Config.bluetooth.pin)) {
        bleEvent(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "PIN is enabled but not 4-6 digits; BLE disabled until the PIN is fixed");
        return false;
    }
    snprintf(blePasskeyDigits, sizeof(blePasskeyDigits), "%s",
             BlePin::passkeyDigits(Config.bluetooth.pin).c_str());
    return true;
}

// Revokes all stored phone bonds when the pairing policy changed since the
// last boot. Must run after the BLE stack is initialised (bond storage lives
// in the stack). Returns false when BLE must stay off because stale bonds
// could not be revoked or the new policy could not be recorded.
static bool enforceBondPolicy() {
    const String policy = pinRequired ? String("pin:") + blePasskeyDigits : String("open");
    String storedPolicy;
    const bool hasStoredPolicy = readStoredPairingPolicy(storedPolicy);
    if (hasStoredPolicy && storedPolicy == policy) return true;

    bleEvent(logging::LoggerLevel::LOGGER_LEVEL_INFO, hasStoredPolicy ? "PIN setting changed since last boot; forgetting all paired phones"
                               : "No saved PIN setting; forgetting all paired phones");
    if (!deleteAllPhoneBonds()) {
        bleEvent(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "Could not forget previously paired phones; BLE disabled");
        return false;
    }
    if (!writeStoredPairingPolicy(policy)) {
        bleEvent(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "Could not save PIN setting to flash; BLE disabled");
        return false;
    }
    return true;
}

static void logAdvertisingStarted() {
    if (pinRequired) {
        bleEvent(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Advertising as \"%s\" (APRS.fi KISS), PIN pairing required - enter %s on the phone",
                   Config.bluetooth.deviceName.c_str(), blePasskeyDigits);
    } else {
        bleEvent(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Advertising as \"%s\" (APRS.fi KISS), PIN disabled - any nearby phone can connect",
                   Config.bluetooth.deviceName.c_str());
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  nRF52840  —  Adafruit Bluefruit (built into Adafruit nRF52 BSP)
 * ══════════════════════════════════════════════════════════════════════════ */
#ifdef ARDUINO_ARCH_NRF52

#include <bluefruit.h>   // already includes services/BLEDfu.h
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

static BLEDfu            bleDfu;
static BLEService        aprsService(BLE_SERVICE_UUID);
static BLECharacteristic txChar(BLE_CHAR_UUID_TX);
static BLECharacteristic rxChar(BLE_CHAR_UUID_RX);

static String   nrf52KissBuffer     = "";
static bool     shouldSendToLoRa    = false;
static String   kissPacketToLoRa    = "";

// ── Pairing-policy storage: a small file on InternalFS (LittleFS) ──────────
// Bluefruit keeps its own bond keys on the same filesystem; clearBonds()
// erases them.
static const char* BLE_POLICY_FILE = "/ble_pin_policy.txt";

static bool readStoredPairingPolicy(String& policyOut) {
    using namespace Adafruit_LittleFS_Namespace;
    if (!InternalFS.exists(BLE_POLICY_FILE)) return false;
    File policyFile(BLE_POLICY_FILE, FILE_O_READ, InternalFS);
    if (!policyFile) return false;
    policyOut = policyFile.readString();
    policyFile.close();
    return policyOut.length() > 0;
}

static bool writeStoredPairingPolicy(const String& policy) {
    using namespace Adafruit_LittleFS_Namespace;
    // FILE_O_WRITE appends rather than truncates on Adafruit LittleFS.
    if (InternalFS.exists(BLE_POLICY_FILE)) InternalFS.remove(BLE_POLICY_FILE);
    {
        File policyFile(BLE_POLICY_FILE, FILE_O_WRITE, InternalFS);
        if (!policyFile) return false;
        policyFile.print(policy);
        policyFile.close();
    }
    String readBack;
    return readStoredPairingPolicy(readBack) && readBack == policy;
}

static bool deleteAllPhoneBonds() {
    Bluefruit.Periph.clearBonds();   // void API; failures surface as SoftDevice asserts
    return true;
}

// ── Connection / security callbacks ─────────────────────────────────────────
// Bluefruit allows a single peripheral connection by default, so no
// second-client handling is needed here.

static void connectCallback(uint16_t conn_handle) {
    nrf52KissBuffer  = "";
    shouldSendToLoRa = false;
    if (pinRequired) {
        // Stay closed until securedCallback() confirms a PIN-authenticated
        // link; the characteristics' ENC_WITH_MITM permissions make the phone
        // start Passkey Entry pairing as soon as it touches them.
        bluetoothConnected = false;
        bleEvent(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Phone connected, waiting for PIN pairing");
    } else {
        bluetoothConnected = true;
        bleEvent(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Phone connected (PIN disabled), KISS link open");
    }
}

static void disconnectCallback(uint16_t conn_handle, uint8_t reason) {
    bluetoothConnected = false;
    nrf52KissBuffer    = "";
    shouldSendToLoRa   = false;
    bleEvent(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Phone disconnected (reason 0x%02X), advertising again", reason);
    Bluefruit.Advertising.start(0);
}

// Fires once the link is encrypted: after a fresh PIN pairing, or when a
// bonded phone reconnects with its stored key.
static void securedCallback(uint16_t conn_handle) {
    if (!pinRequired) return;   // open mode: link was already opened in connectCallback()
    BLEConnection* connection = Bluefruit.Connection(conn_handle);
    if (connection == nullptr) return;
    // Security level 3+ = authenticated (MITM-protected) encryption, which
    // only Passkey Entry with our PIN can produce. Level 2 would be a
    // "Just Works" key that bypassed the PIN.
    if (connection->getSecureMode().lv < 3) {
        bleEvent(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Phone link encrypted without PIN authentication, disconnecting");
        connection->disconnect();
        return;
    }
    bluetoothConnected = true;
    bleEvent(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Phone authenticated with PIN, KISS link open");
}

static void pairCompleteCallback(uint16_t conn_handle, uint8_t auth_status) {
    if (auth_status == BLE_GAP_SEC_STATUS_SUCCESS) {
        bleEvent(logging::LoggerLevel::LOGGER_LEVEL_INFO, "PIN pairing succeeded, phone bonded");
        return;
    }
    bleEvent(logging::LoggerLevel::LOGGER_LEVEL_WARN, "PIN pairing failed (status 0x%02X): wrong PIN, cancelled, or timed out; disconnecting", auth_status);
    bluetoothConnected = false;
    BLEConnection* connection = Bluefruit.Connection(conn_handle);
    if (connection != nullptr) connection->disconnect();
}

static void rxWriteCallback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    // Defence in depth: the SoftDevice already rejects writes that don't meet
    // the characteristic's security level.
    if (!bluetoothConnected) return;
    logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BLE RX", "write callback fired, %d bytes", (int)len);
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
            if (isDataFrame) {
                shouldSendToLoRa = true;
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BLE RX",
                           "KISS data frame queued, len=%d", (int)kissPacketToLoRa.length());
            }
        }
    }
}

namespace BLE_Utils {

    void stop() {
        Bluefruit.Advertising.stop();
        // Full deinit not available on nRF52 without resetting SoftDevice
    }

    void setup() {
        bluetoothConnected = false;
        if (!loadPinPolicy()) return;

        // BLEDfu must be registered before Bluefruit.begin() so the SoftDevice
        // allocates an ATT handle for the Nordic DFU service at startup.
        bleDfu.begin();

        Bluefruit.begin();
        // Bond storage only exists once Bluefruit (and its InternalFS) is up.
        // On failure, leave the stack idle: never advertise with stale bonds.
        if (!enforceBondPolicy()) return;

        // Characteristic security: authenticated encryption when a PIN is set.
        // The TX CCCD (notification subscribe) inherits the TX read permission,
        // so a phone cannot even subscribe until it has paired with the PIN.
        SecureMode_t linkSecurity = SECMODE_OPEN;
        if (pinRequired) {
            Bluefruit.Security.setPIN(blePasskeyDigits);   // static passkey, MITM, display-only IO caps
            Bluefruit.Security.setPairCompleteCallback(pairCompleteCallback);
            Bluefruit.Security.setSecuredCallback(securedCallback);
            linkSecurity = SECMODE_ENC_WITH_MITM;
        }

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
        txChar.setPermission(linkSecurity, SECMODE_NO_ACCESS);
        txChar.setMaxLen(512);
        txChar.begin();

        rxChar.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
        rxChar.setPermission(SECMODE_NO_ACCESS, linkSecurity);
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

        logAdvertisingStarted();
    }

    void sendToLoRa() {
        if (!shouldSendToLoRa || !bluetoothConnected) return;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BLE TX", "decoded: \"%s\"", kissPacketToLoRa.c_str());
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
#include <Preferences.h>
#include <esp_timer.h>

static NimBLEServer*         pServer             = nullptr;
static NimBLECharacteristic* pCharacteristicTx   = nullptr;
static NimBLECharacteristic* pCharacteristicRx   = nullptr;

static bool     shouldSendBLEtoLoRa = false;
static String   BLEToLoRaPacket     = "";
static String   kissSerialBuffer    = "";

// The single phone allowed to talk KISS; further connections are refused so a
// second (possibly unpaired) device can't interleave with the active session.
static uint16_t activeConnHandle    = BLE_HS_CONN_HANDLE_NONE;

// A phone that connects but never completes PIN pairing would otherwise hold
// the single connection slot forever. This one-shot timer frees it.
static constexpr uint64_t PIN_PAIRING_TIMEOUT_US = 60ULL * 1000ULL * 1000ULL;   // 60 s
static esp_timer_handle_t pinPairingTimer        = nullptr;
static bool               loggedUnpairedWrite    = false;   // one warning per connection

static void onPinPairingTimeout(void*) {
    const uint16_t connHandle = activeConnHandle;
    if (bluetoothConnected || connHandle == BLE_HS_CONN_HANDLE_NONE || pServer == nullptr) return;
    bleEvent(logging::LoggerLevel::LOGGER_LEVEL_WARN,
             "Phone did not pair with the PIN within 60 s, disconnecting");
    pServer->disconnect(connHandle);
}

static void startPinPairingTimer() {
    if (pinPairingTimer == nullptr) return;
    esp_timer_stop(pinPairingTimer);   // harmless if not running
    esp_timer_start_once(pinPairingTimer, PIN_PAIRING_TIMEOUT_US);
}

static void stopPinPairingTimer() {
    if (pinPairingTimer != nullptr) esp_timer_stop(pinPairingTimer);
}

// ── Pairing-policy storage: ESP32 NVS (NimBLE keeps its bonds in NVS too) ──
static const char* BLE_PREFS_NAMESPACE  = "fieldops-ble";
static const char* BLE_PREFS_POLICY_KEY = "pinPolicy";

static bool readStoredPairingPolicy(String& policyOut) {
    Preferences prefs;
    if (!prefs.begin(BLE_PREFS_NAMESPACE, /*readOnly=*/true)) return false;   // namespace not created yet
    policyOut = prefs.getString(BLE_PREFS_POLICY_KEY, "");
    prefs.end();
    return policyOut.length() > 0;
}

static bool writeStoredPairingPolicy(const String& policy) {
    Preferences prefs;
    if (!prefs.begin(BLE_PREFS_NAMESPACE, /*readOnly=*/false)) return false;
    prefs.putString(BLE_PREFS_POLICY_KEY, policy);
    const bool persisted = prefs.getString(BLE_PREFS_POLICY_KEY, "") == policy;
    prefs.end();
    return persisted;
}

static bool deleteAllPhoneBonds() {
    NimBLEDevice::deleteAllBonds();
    return NimBLEDevice::getNumBonds() == 0;
}

// True when the link was encrypted with a key that only PIN (MITM) pairing produces.
static bool isPinAuthenticated(const ble_gap_conn_desc* desc) {
    return desc->sec_state.encrypted && desc->sec_state.authenticated;
}

// Human-readable text for an SMP pairing-failure reason (Core Spec Vol 3 Part H 3.5.5).
static const char* smpReasonText(int reason) {
    switch (reason) {
        case 0x01: return "passkey entry failed";
        case 0x03: return "authentication requirements not met";
        case 0x04: return "confirm value failed (wrong PIN)";
        case 0x05: return "pairing not supported";
        case 0x06: return "encryption key size";
        case 0x08: return "unspecified reason";
        case 0x09: return "repeated attempts";
        case 0x0B: return "DHKey check failed (wrong PIN)";
        case 0x0C: return "numeric comparison failed";
        default:   return "other";
    }
}

// Passive listener (NimBLEDevice::setCustomGapHandler) that logs why
// encryption or pairing failed. The server callbacks only see the resulting
// security state, not the error, which is what's needed to debug a phone.
static int blePairingDiagnostics(ble_gap_event* event, void*) {
    switch (event->type) {
        case BLE_GAP_EVENT_ENC_CHANGE: {
            const int status = event->enc_change.status;
            if (status == 0) break;   // success is reported by onAuthenticationComplete()
            if (status >= BLE_HS_ERR_SM_PEER_BASE && status < BLE_HS_ERR_SM_PEER_BASE + 0x100) {
                bleEvent(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Pairing rejected by phone: %s (0x%03X)",
                         smpReasonText(status - BLE_HS_ERR_SM_PEER_BASE), status);
            } else if (status >= BLE_HS_ERR_SM_US_BASE && status < BLE_HS_ERR_SM_US_BASE + 0x100) {
                bleEvent(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Pairing rejected by tracker: %s (0x%03X)",
                         smpReasonText(status - BLE_HS_ERR_SM_US_BASE), status);
            } else if (status == BLE_HS_ERR_HCI_BASE + 0x06) {
                bleEvent(logging::LoggerLevel::LOGGER_LEVEL_WARN,
                         "Phone presented a pairing key this tracker doesn't have (0x%03X)", status);
            } else if (status == BLE_HS_ETIMEOUT) {
                bleEvent(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Pairing timed out on the phone side");
            } else {
                bleEvent(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Encryption failed, status 0x%03X", status);
            }
            break;
        }
        case BLE_GAP_EVENT_REPEAT_PAIRING:
            bleEvent(logging::LoggerLevel::LOGGER_LEVEL_INFO,
                     "Phone is re-pairing; replacing its old pairing (stored pairings: %d)", NimBLEDevice::getNumBonds());
            break;
        default:
            break;
    }
    return 0;   // listeners are observe-only; NimBLEServer still handles every event
}

class MyServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, ble_gap_conn_desc* desc) override {
        if (activeConnHandle != BLE_HS_CONN_HANDLE_NONE) {
            bleEvent(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Second phone tried to connect while one is active, refusing it");
            server->disconnect(desc->conn_handle);
            return;
        }
        activeConnHandle    = desc->conn_handle;
        kissSerialBuffer    = "";
        shouldSendBLEtoLoRa = false;
        loggedUnpairedWrite = false;

        if (!pinRequired) {
            bluetoothConnected = true;
            bleEvent(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Phone connected (PIN disabled), KISS link open");
            return;
        }
        // Stay closed until onAuthenticationComplete() confirms the PIN.
        // Security is deliberately NOT initiated from this side (matching the
        // NimBLE_Secure_Server example and Meshtastic's NimBLE PIN pairing):
        // the first access to a protected characteristic returns Insufficient
        // Encryption (phone has a bond: it re-encrypts silently) or
        // Insufficient Authentication (no bond: it shows the PIN prompt).
        // A peripheral-sent Security Request on top of that can start a second,
        // competing pairing procedure on Android, which re-prompts for the PIN.
        bluetoothConnected = false;
        bleEvent(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Phone connected, waiting for PIN pairing");
        startPinPairingTimer();
    }

    // Static passkey the phone user must type. Overridden explicitly because
    // NimBLE falls back to this callback (default 123456) in some code paths.
    uint32_t onPassKeyRequest() override {
        bleEvent(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Phone is asking for the PIN, waiting for entry");
        return BlePin::passkeyValue(String(blePasskeyDigits));
    }

    // Fires when pairing finishes (success or failure) and when a bonded
    // phone re-encrypts with its stored key.
    //
    // A failed or unauthenticated result does NOT disconnect. The usual cause
    // is a phone still holding a pairing this tracker has forgotten (after a
    // PIN change, a reflash, or an old firmware's pairing): its stored key is
    // rejected, and Android then re-pairs on the same link, which is when the
    // PIN prompt appears. Dropping the link here would kill that re-pairing.
    // KISS traffic stays blocked (bluetoothConnected=false, and the GATT
    // characteristics require authenticated encryption), and the pairing
    // timer frees the slot if the phone never succeeds.
    void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
        if (desc->conn_handle != activeConnHandle || !pinRequired) return;
        if (!isPinAuthenticated(desc)) {
            bluetoothConnected = false;
            bleEvent(logging::LoggerLevel::LOGGER_LEVEL_WARN,
                     desc->sec_state.encrypted
                         ? "Phone used a pairing made without the PIN; waiting for it to pair again with the PIN"
                         : "Pairing failed (wrong PIN, cancelled, or a stale pairing on the phone); waiting for the phone to retry");
            return;
        }
        stopPinPairingTimer();
        bluetoothConnected = true;
        bleEvent(logging::LoggerLevel::LOGGER_LEVEL_INFO,
                 desc->sec_state.bonded ? "Phone authenticated with PIN, KISS link open"
                                        : "Phone authenticated with PIN (not bonded), KISS link open");
    }

    void onDisconnect(NimBLEServer* server, ble_gap_conn_desc* desc) override {
        if (desc->conn_handle != activeConnHandle) return;   // a refused second phone
        stopPinPairingTimer();
        bluetoothConnected  = false;
        activeConnHandle    = BLE_HS_CONN_HANDLE_NONE;
        shouldSendBLEtoLoRa = false;
        kissSerialBuffer    = "";
        bleEvent(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Phone disconnected, advertising again");
        server->startAdvertising();
    }
};

class MyCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, ble_gap_conn_desc* desc) override {
        // Ignore writes from anything but the active (and, if required, PIN-authenticated) phone.
        if (desc->conn_handle != activeConnHandle) return;
        if (!bluetoothConnected || (pinRequired && !isPinAuthenticated(desc))) {
            if (!loggedUnpairedWrite) {
                loggedUnpairedWrite = true;
                bleEvent(logging::LoggerLevel::LOGGER_LEVEL_WARN,
                         "Ignoring data from phone until it pairs with the PIN");
            }
            return;
        }
        std::string receivedData = pCharacteristic->getValue();
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BLE RX", "write callback fired, %d bytes", (int)receivedData.size());

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
                if (isDataFrame) {
                    shouldSendBLEtoLoRa = true;
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BLE RX",
                               "KISS data frame queued, len=%d", (int)BLEToLoRaPacket.length());
                }
            }
        }
    }
};

namespace BLE_Utils {

    void stop() {
        stopPinPairingTimer();
        NimBLEDevice::deinit();
    }

    void setup() {
        bluetoothConnected = false;
        activeConnHandle   = BLE_HS_CONN_HANDLE_NONE;
        if (!loadPinPolicy()) return;

        NimBLEDevice::init(Config.bluetooth.deviceName.c_str());
        // Bond storage only exists once the stack is initialised. On failure,
        // shut the stack back down: never advertise with stale bonds.
        if (!enforceBondPolicy()) {
            NimBLEDevice::deinit();
            return;
        }

        // PIN mode: bonding + MITM + LE Secure Connections, and the device
        // "displays" the static passkey (the phone user types it in).
        NimBLEDevice::setSecurityAuth(/*bonding=*/pinRequired, /*mitm=*/pinRequired, /*sc=*/true);
        NimBLEDevice::setSecurityIOCap(pinRequired ? BLE_HS_IO_DISPLAY_ONLY : BLE_HS_IO_NO_INPUT_OUTPUT);
        if (pinRequired) {
            // Exchange both encryption and identity keys in each direction
            // (NimBLE's default only sends our ENC key). Same as Meshtastic.
            NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
            NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
            NimBLEDevice::setSecurityPasskey(BlePin::passkeyValue(String(blePasskeyDigits)));
            NimBLEDevice::setCustomGapHandler(blePairingDiagnostics);
            if (pinPairingTimer == nullptr) {
                esp_timer_create_args_t timerArgs = {};
                timerArgs.callback        = onPinPairingTimeout;
                timerArgs.dispatch_method = ESP_TIMER_TASK;
                timerArgs.name            = "ble_pin_to";
                esp_timer_create(&timerArgs, &pinPairingTimer);
            }
        }

        pServer = NimBLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());

        NimBLEService* pService = pServer->createService(BLE_SERVICE_UUID);
        pCharacteristicTx = pService->createCharacteristic(
            BLE_CHAR_UUID_TX,
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY |
            (pinRequired ? (NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN) : 0));
        pCharacteristicRx = pService->createCharacteristic(
            BLE_CHAR_UUID_RX,
            NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
            (pinRequired ? (NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN) : 0));
        pCharacteristicRx->setCallbacks(new MyCallbacks());

        pService->start();

        NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
        pServer->getAdvertising()->setScanResponse(true);
        pServer->getAdvertising()->setMinPreferred(0x06);
        pServer->getAdvertising()->setMaxPreferred(0x0C);
        pAdvertising->start();

        logAdvertisingStarted();
    }

    void sendToLoRa() {
        if (!shouldSendBLEtoLoRa || !bluetoothConnected) return;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BLE TX", "decoded: \"%s\"", BLEToLoRaPacket.c_str());
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
