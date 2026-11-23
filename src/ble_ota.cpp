/* ble_ota.cpp — application firmware update over BLE (ESP32 boards).
 * Protocol and GATT layout: include/ble_ota.h. GATT registration and the
 * link-level gates (active phone, PIN-authenticated) live in ble_utils.cpp.
 *
 * Threading (same split as the BLE config link):
 *   - The NimBLE host task only validates packet order and copies payload
 *     bytes into a lock-free ring (dataRing), and drops commands into a
 *     one-slot mailbox. It never touches flash or OTA_Utils.
 *   - The main loop (service()) runs every command, writes flash through
 *     OTA_Utils, and sends every notification.
 *   Hand-over of the per-session receive state uses `receiveOpen`: the main
 *   loop sets that state up while the gate is closed and publishes it with a
 *   release store; the BLE task reads it only after an acquire load sees the
 *   gate open.
 *
 * Flash writes run on the main loop because a 4 KB sector erase can take tens
 * of milliseconds, which would stall the BLE host task (and the link) if done
 * in the write callback.
 */

#include "ble_ota.h"

#ifdef HAS_BLE_OTA

#include <algorithm>
#include <atomic>
#include "byte_ring.h"
#include "display.h"
#include "log_buffer.h"
#include "logger.h"
#include "ota_utils.h"
#include "version.h"

extern logging::Logger logger;

namespace BLE_OTA {

    // ── Sizing ──────────────────────────────────────────────────────────────
    // The receive ring absorbs data while the main loop is busy erasing a
    // sector. The window advertised to the phone stays below the ring's
    // capacity, so a phone that honours it can never overflow the ring.
    static constexpr size_t   RING_BYTES          = 16 * 1024;
    static constexpr uint32_t WINDOW_BYTES        = 12 * 1024;
    static constexpr uint32_t ACK_INTERVAL_BYTES  = 4 * 1024;
    // Largest piece handed to OTA_Utils::write() per call; one flash sector.
    static constexpr size_t   FLASH_CHUNK_BYTES   = 4096;
    // Main-loop time slice for moving data to flash on each pass.
    static constexpr uint32_t SERVICE_BUDGET_MS   = 50;
    // Time for the DONE notification to reach the phone before the reset.
    static constexpr uint32_t REBOOT_DELAY_MS     = 1500;
    static constexpr uint32_t DISPLAY_REFRESH_MS  = 500;

    static constexpr size_t   CONTROL_MAX_BYTES   = 128;    // START is at most 104 bytes
    static constexpr size_t   DATA_HEADER_BYTES   = 4;      // u32 offset

    enum class Phase : uint8_t {
        Idle,           // no session
        Receiving,      // READY sent, data flowing
        Rebooting,      // DONE sent, waiting REBOOT_DELAY_MS before the reset
    };

    // ── State shared with the BLE task ──────────────────────────────────────
    static ByteRing              dataRing;                      // producer: BLE task, consumer: main loop
    static std::atomic<bool>     receiveOpen{false};            // gate for onDataWrite()
    static std::atomic<uint32_t> receivedBytes{0};              // next expected offset; producer-owned while open
    static std::atomic<uint32_t> lastDataMs{0};
    static std::atomic<bool>     flowViolation{false};          // window exceeded / data past the end
    static std::atomic<bool>     resyncPending{false};          // main loop must send RESYNC(receivedBytes)
    static bool                  awaitingResync = false;        // producer-owned while open
    static uint32_t              sessionImageSize = 0;          // written before the gate opens

    // One-slot command mailbox. Commands are writes with response, so a
    // well-behaved phone never has two in flight.
    static uint8_t               controlBuffer[CONTROL_MAX_BYTES];
    static size_t                controlLength = 0;
    static std::atomic<bool>     controlPending{false};
    static std::atomic<bool>     controlDropped{false};

    // ── Main-loop-only state ────────────────────────────────────────────────
    static Phase     phase              = Phase::Idle;
    static uint32_t  sessionLinkId      = 0;
    static uint8_t   expectedSha256[OTA_Utils::SHA256_BYTES];
    static bool      flashStarted       = false;    // OTA_Utils::begin() called for this session
    static uint32_t  flashedBytes       = 0;
    static uint32_t  lastAckedBytes     = 0;
    static bool      ackPending         = false;    // an ACK could not be sent yet
    static bool      endRequested       = false;
    static uint32_t  rebootAtMs         = 0;
    static uint32_t  lastDisplayMs      = 0;
    static uint32_t  sessionStartMs     = 0;
    static bool      fastLinkRequested  = false;    // otaSetFastLink(true) sent for this session

    // ── Helpers ─────────────────────────────────────────────────────────────

    static uint32_t readU32(const uint8_t* bytes) {
        return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    }

    // String from a length-prefixed field (not NUL-terminated on the wire).
    static String stringFromBytes(const uint8_t* bytes, size_t length) {
        String text;
        text.reserve(length);
        for (size_t i = 0; i < length; i++) text.concat(static_cast<char>(bytes[i]));
        return text;
    }

    static void writeU32(uint8_t* bytes, uint32_t value) {
        bytes[0] = value & 0xFF;
        bytes[1] = (value >> 8) & 0xFF;
        bytes[2] = (value >> 16) & 0xFF;
        bytes[3] = (value >> 24) & 0xFF;
    }

    // Logged to serial and to the web UI live log (no-op without a web UI).
    static void otaEvent(logging::LoggerLevel level, const String& message) {
        logger.log(level, "BLE OTA", "%s", message.c_str());
        LogBuffer::pushf(LogBuffer::TYPE_INFO, "BLE OTA: %s", message.c_str());
    }

    // Short notifications (READY / ACK / RESYNC / DONE / ABORTED): retried a
    // few times because the host may briefly be out of buffers under load.
    static bool sendStatus(const uint8_t* data, size_t length) {
        for (int attempt = 0; attempt < 20; attempt++) {
            if (BLE_Utils::otaSendStatus(data, length)) return true;
            if (BLE_Utils::otaLinkId() == 0) return false;
            delay(5);
        }
        return false;
    }

    static void sendSimple(Notification code) {
        const uint8_t message[1] = {code};
        sendStatus(message, sizeof(message));
    }

    static void sendOffset(Notification code, uint32_t offset) {
        uint8_t message[5] = {code};
        writeU32(message + 1, offset);
        sendStatus(message, sizeof(message));
    }

    // Closes the receive gate and clears the per-session state. The ring is
    // drained here (consumer side); stray bytes the BLE task pushes after the
    // gate closes are cleared again before the next session opens it.
    static void resetSession() {
        receiveOpen.store(false, std::memory_order_release);
        dataRing.clear();
        if (fastLinkRequested) {
            // Back to a power-friendly interval for the KISS / config session.
            BLE_Utils::otaSetFastLink(false);
            fastLinkRequested = false;
        }
        phase         = Phase::Idle;
        sessionLinkId = 0;
        flashStarted  = false;
        endRequested  = false;
        ackPending    = false;
    }

    // Ends the session with an ERROR notification (if the link is still up)
    // and keeps the running firmware.
    static void failSession(ErrorCode code, const String& reason) {
        otaEvent(logging::LoggerLevel::LOGGER_LEVEL_ERROR,
                 String("Update failed after ") + flashedBytes + " bytes: " + reason);
        if (BLE_Utils::otaLinkId() == sessionLinkId) {
            uint8_t message[2 + 120];
            message[0] = NOTIFY_ERROR;
            message[1] = code;
            const size_t textLength = std::min<size_t>(reason.length(), sizeof(message) - 2);
            memcpy(message + 2, reason.c_str(), textLength);
            sendStatus(message, 2 + textLength);
        }
        OTA_Utils::release(OTA_Utils::Client::Bluetooth);   // aborts the flash session
        resetSession();
    }

    static String describeImage(uint32_t size) {
        return String(size / 1024) + " KB";
    }

    // ── Commands ────────────────────────────────────────────────────────────

    // START [u8 protocol][u32 size][32 B sha256][u8 n][board][u8 m][version]
    static void handleStart(const uint8_t* command, size_t length) {
        if (phase == Phase::Receiving) {
            // The phone restarted its transfer (e.g. after an app-side error).
            otaEvent(logging::LoggerLevel::LOGGER_LEVEL_WARN, "New START during a transfer; restarting the update");
            OTA_Utils::release(OTA_Utils::Client::Bluetooth);
            resetSession();
        }
        sessionLinkId = BLE_Utils::otaLinkId();   // ERROR replies need it
        flashedBytes  = 0;

        // Fixed part: opcode, protocol, size, sha256, board length.
        static constexpr size_t FIXED_BYTES = 1 + 1 + 4 + OTA_Utils::SHA256_BYTES + 1;
        if (length < FIXED_BYTES) {
            failSession(ERR_BAD_REQUEST, "START is too short");
            return;
        }
        const uint8_t  protocol  = command[1];
        const uint32_t imageSize = readU32(command + 2);
        const uint8_t* sha256    = command + 6;
        const size_t   boardLen  = command[6 + OTA_Utils::SHA256_BYTES];
        const size_t   versionAt = FIXED_BYTES + boardLen;
        if (versionAt >= length || versionAt + 1 + command[versionAt] > length) {
            failSession(ERR_BAD_REQUEST, "START is malformed");
            return;
        }
        const String board   = stringFromBytes(command + FIXED_BYTES, boardLen);
        const String version = stringFromBytes(command + versionAt + 1, command[versionAt]);

        if (protocol != PROTOCOL_VERSION) {
            failSession(ERR_PROTOCOL_VERSION, String("Protocol ") + protocol + " not supported (tracker speaks " +
                                              PROTOCOL_VERSION + ")");
            return;
        }
        if (board != BOARD_ENV_ID) {
            failSession(ERR_WRONG_BOARD, "Image is for " + board + ", this tracker is " BOARD_ENV_ID);
            return;
        }
        const size_t slotSize = OTA_Utils::updateSlotSize();
        if (imageSize == 0 || imageSize > slotSize) {
            failSession(ERR_IMAGE_SIZE, String("Image size ") + imageSize + " does not fit the " + slotSize + "-byte slot");
            return;
        }
        OTA_Utils::Client busyWith = OTA_Utils::Client::None;
        if (!OTA_Utils::claim(OTA_Utils::Client::Bluetooth, &busyWith)) {
            // failSession()'s release() is a no-op here: the claim is not ours.
            failSession(ERR_BUSY, String("A ") + OTA_Utils::clientName(busyWith) + " firmware update is in progress");
            return;
        }
        if (!dataRing.allocate(RING_BYTES)) {
            failSession(ERR_NO_MEMORY, "Out of memory for the receive buffer");
            return;
        }

        // Set up the receive state, then open the gate (release) for the BLE task.
        memcpy(expectedSha256, sha256, OTA_Utils::SHA256_BYTES);
        dataRing.clear();
        sessionImageSize = imageSize;
        receivedBytes.store(0, std::memory_order_relaxed);
        lastDataMs.store(millis(), std::memory_order_relaxed);
        flowViolation.store(false, std::memory_order_relaxed);
        resyncPending.store(false, std::memory_order_relaxed);
        awaitingResync = false;
        flashStarted   = false;
        flashedBytes   = 0;
        lastAckedBytes = 0;
        ackPending     = false;
        endRequested   = false;
        sessionStartMs = millis();
        lastDisplayMs  = 0;
        phase          = Phase::Receiving;
        receiveOpen.store(true, std::memory_order_release);

        otaEvent(logging::LoggerLevel::LOGGER_LEVEL_INFO,
                 "Receiving firmware " + version + " (" + describeImage(imageSize) + ") for " + board);
        displayFirmwareUpdate("Starting...");
        BLE_Utils::otaSetFastLink(true);
        fastLinkRequested = true;

        uint8_t ready[10] = {NOTIFY_READY, PROTOCOL_VERSION};
        writeU32(ready + 2, WINDOW_BYTES);
        writeU32(ready + 6, ACK_INTERVAL_BYTES);
        sendStatus(ready, sizeof(ready));
    }

    static void handleCommand(const uint8_t* command, size_t length) {
        // Once DONE is sent the new image is installed; a repeated END or a
        // late ABORT must not undo the pending reboot.
        if (length == 0 || phase == Phase::Rebooting) return;
        switch (command[0]) {
            case CMD_START:
                handleStart(command, length);
                break;
            case CMD_END:
                if (phase != Phase::Receiving) {
                    failSession(ERR_BAD_REQUEST, "END without an update in progress");
                    break;
                }
                endRequested = true;   // completed in service() once every byte is on flash
                break;
            case CMD_ABORT:
                if (phase == Phase::Receiving) {
                    otaEvent(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Update cancelled by the phone");
                    OTA_Utils::release(OTA_Utils::Client::Bluetooth);
                    resetSession();
                }
                sendSimple(NOTIFY_ABORTED);
                break;
            default:
                failSession(ERR_BAD_REQUEST, String("Unknown command 0x") + String(command[0], HEX));
                break;
        }
    }

    // ── Data path (main loop) ───────────────────────────────────────────────

    // Moves received bytes from the ring to flash for up to SERVICE_BUDGET_MS.
    // Returns false when the session failed.
    static bool drainToFlash() {
        static uint8_t chunk[FLASH_CHUNK_BYTES];   // static: keep 4 KB off the loop task's stack
        const uint32_t startMs = millis();
        while (dataRing.available() > 0) {
            const size_t length = dataRing.peek(chunk, sizeof(chunk));
            if (!flashStarted) {
                // Deferred to the first data so OTA_Utils can check the image magic.
                if (!OTA_Utils::begin(chunk, length, sessionImageSize)) {
                    failSession(ERR_FLASH, OTA_Utils::lastError());
                    return false;
                }
                flashStarted = true;
                otaEvent(logging::LoggerLevel::LOGGER_LEVEL_INFO,
                         String("Writing to ") + OTA_Utils::targetSlotLabel());
            }
            if (!OTA_Utils::write(chunk, length)) {
                failSession(ERR_FLASH, OTA_Utils::lastError());
                return false;
            }
            dataRing.drop(length);
            flashedBytes += length;
            if (millis() - startMs >= SERVICE_BUDGET_MS) break;
        }
        return true;
    }

    // Sends ACK(flashedBytes) every ACK_INTERVAL_BYTES and at the image end.
    static void acknowledgeProgress() {
        const bool intervalReached = flashedBytes - lastAckedBytes >= ACK_INTERVAL_BYTES;
        const bool imageComplete   = flashedBytes == sessionImageSize && lastAckedBytes != flashedBytes;
        if (!(intervalReached || imageComplete || ackPending)) return;
        uint8_t message[5] = {NOTIFY_ACK};
        writeU32(message + 1, flashedBytes);
        if (BLE_Utils::otaSendStatus(message, sizeof(message))) {
            lastAckedBytes = flashedBytes;
            ackPending     = false;
        } else {
            ackPending     = true;   // out of host buffers: retry next pass
        }
    }

    static void finishSession() {
        displayFirmwareUpdate("Verifying...");
        if (!OTA_Utils::finish(expectedSha256)) {
            const String& reason = OTA_Utils::lastError();
            const bool checksum = reason.indexOf("SHA-256") >= 0 || reason.indexOf("validation") >= 0;
            failSession(checksum ? ERR_VERIFY : ERR_FLASH, reason);
            displayFirmwareUpdate("Failed");
            return;
        }
        otaEvent(logging::LoggerLevel::LOGGER_LEVEL_INFO,
                 String("Update OK: ") + flashedBytes + " bytes in " + ((millis() - sessionStartMs) / 1000) +
                 " s to " + OTA_Utils::targetSlotLabel() + ", rebooting");
        receiveOpen.store(false, std::memory_order_release);
        sendSimple(NOTIFY_DONE);
        displayFirmwareUpdate("Done, rebooting");
        // Keep the claim: no other transport may start before the reset.
        phase      = Phase::Rebooting;
        rebootAtMs = millis() + REBOOT_DELAY_MS;
    }

    static void serviceReceiving() {
        if (BLE_Utils::otaLinkId() != sessionLinkId) {
            failSession(ERR_TIMEOUT, "Phone disconnected");
            return;
        }
        if (flowViolation.load(std::memory_order_acquire)) {
            failSession(ERR_FLOW, "Phone sent more than the window or past the image end");
            return;
        }
        if (resyncPending.exchange(false)) {
            sendOffset(NOTIFY_RESYNC, receivedBytes.load(std::memory_order_acquire));
        }
        if (!drainToFlash()) return;
        acknowledgeProgress();

        if (endRequested && dataRing.available() == 0) {
            const uint32_t received = receivedBytes.load(std::memory_order_acquire);
            if (received != sessionImageSize || flashedBytes != sessionImageSize) {
                failSession(ERR_INCOMPLETE, String("END after ") + received + " of " + sessionImageSize + " bytes");
                return;
            }
            finishSession();
            return;
        }
        if (millis() - lastDataMs.load(std::memory_order_relaxed) > INACTIVITY_TIMEOUT_MS) {
            failSession(ERR_TIMEOUT, String("No data for ") + (INACTIVITY_TIMEOUT_MS / 1000) + " s");
            return;
        }
        if (millis() - lastDisplayMs >= DISPLAY_REFRESH_MS) {
            lastDisplayMs = millis();
            const uint32_t percent = (uint32_t)((uint64_t)flashedBytes * 100 / sessionImageSize);
            displayFirmwareUpdate(String(percent) + "%  " + describeImage(flashedBytes));
        }
    }

    // ── Public API ──────────────────────────────────────────────────────────

    void onControlWrite(const uint8_t* data, size_t length) {
        if (length == 0) return;
        if (controlPending.load(std::memory_order_acquire) || length > CONTROL_MAX_BYTES) {
            controlDropped.store(true, std::memory_order_release);
            return;
        }
        memcpy(controlBuffer, data, length);
        controlLength = length;
        controlPending.store(true, std::memory_order_release);
    }

    void onDataWrite(const uint8_t* data, size_t length) {
        if (!receiveOpen.load(std::memory_order_acquire)) return;
        if (length <= DATA_HEADER_BYTES) return;

        const uint32_t offset   = readU32(data);
        const uint32_t expected = receivedBytes.load(std::memory_order_relaxed);
        if (offset != expected) {
            // Ask once per gap; packets already in flight behind the gap are
            // dropped silently until the phone resends from `expected`.
            if (!awaitingResync) {
                awaitingResync = true;
                resyncPending.store(true, std::memory_order_release);
            }
            return;
        }
        awaitingResync = false;

        const size_t payloadLength = length - DATA_HEADER_BYTES;
        if (expected + payloadLength > sessionImageSize ||
            dataRing.push(data + DATA_HEADER_BYTES, payloadLength) < payloadLength) {
            // Data past the announced end, or the phone ignored the window.
            // The session cannot continue consistently; the main loop ends it.
            flowViolation.store(true, std::memory_order_release);
            receiveOpen.store(false, std::memory_order_release);
            return;
        }
        receivedBytes.store(expected + payloadLength, std::memory_order_release);
        lastDataMs.store(millis(), std::memory_order_relaxed);
    }

    bool inProgress() { return phase != Phase::Idle; }

    void service() {
        if (controlDropped.exchange(false)) {
            // Can only happen when the phone ignores write-with-response.
            if (phase == Phase::Receiving) {
                failSession(ERR_BAD_REQUEST, "Command received while the previous one was still pending");
            }
        }
        if (controlPending.load(std::memory_order_acquire)) {
            uint8_t command[CONTROL_MAX_BYTES];
            const size_t length = controlLength;
            memcpy(command, controlBuffer, length);
            controlPending.store(false, std::memory_order_release);
            handleCommand(command, length);
        }

        switch (phase) {
            case Phase::Idle:
                break;
            case Phase::Receiving:
                serviceReceiving();
                // Nothing else runs during an update; let lower-priority tasks
                // (and the idle task) breathe when there is no data to write.
                if (phase == Phase::Receiving && dataRing.available() == 0) delay(1);
                break;
            case Phase::Rebooting:
                if ((int32_t)(millis() - rebootAtMs) >= 0) ESP.restart();
                delay(10);
                break;
        }
    }

}

#endif  // HAS_BLE_OTA
