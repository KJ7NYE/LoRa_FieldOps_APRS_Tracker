/* ble_ota.h — application firmware update over BLE (ESP32 boards).
 *
 * The FieldOps app streams <board>_firmware.bin into the inactive OTA slot
 * through OTA_Utils, the same writer the web UI's Update Firmware uses. The
 * running firmware is never touched: any failure, disconnect or power loss
 * leaves the tracker booting what it runs now.
 *
 * ── Availability ─────────────────────────────────────────────────────────────
 * The GATT service exists only when BLE PIN pairing is required and the
 * board has a second app slot (not on tbeam_433_aprs / lilygo_t3_433_aprs).
 * Both characteristics require an authenticated (PIN/MITM) encrypted link.
 * The app detects support by the presence of the service.
 *
 * ── GATT layout ──────────────────────────────────────────────────────────────
 *   Service  62970001-5b20-49c7-ae44-96d63f6d50e8
 *   Control  62970002-...  write (with response) + notify
 *            Phone writes commands; the tracker answers with notifications.
 *            Subscribe to notifications before sending START.
 *   Data     62970003-...  write without response
 *            [u32 offset][payload], payload up to ATT MTU - 3 - 4 bytes.
 *
 * All integers are little-endian.
 *
 * ── Commands (phone → Control) ───────────────────────────────────────────────
 *   0x01 START  [u8 protocol=1][u32 imageSize][32 B SHA-256 of the image]
 *               [u8 n][n B board id, e.g. "heltec_v3_433_aprs"]
 *               [u8 m][m B firmware version, informational]
 *   0x02 END    all bytes sent; verify and install
 *   0x03 ABORT  stop; keep the running firmware
 *
 * ── Notifications (tracker → Control) ────────────────────────────────────────
 *   0x81 READY    [u8 protocol][u32 window][u32 ackInterval]
 *   0x82 ACK      [u32 bytesWritten]     bytes committed to flash
 *   0x83 RESYNC   [u32 expectedOffset]   a data packet arrived out of order;
 *                                        resend from expectedOffset
 *   0x84 DONE     image verified and selected; the tracker reboots in ~1.5 s
 *   0x85 ERROR    [u8 code][UTF-8 text]  session closed (codes: ErrorCode)
 *   0x86 ABORTED  reply to ABORT
 *
 * ── Flow ─────────────────────────────────────────────────────────────────────
 *   1. START → READY (or ERROR). Do not send data before READY.
 *   2. Send data packets in order from offset 0. Keep at most `window` bytes
 *      sent but not yet ACKed. The tracker ACKs at least every `ackInterval`
 *      bytes and when the last byte is written.
 *   3. On RESYNC, continue sending from expectedOffset.
 *   4. After ACK(imageSize), send END → DONE (or ERROR).
 *   No data for INACTIVITY_TIMEOUT_MS, or a disconnect, aborts the session.
 *   While a session is open the tracker pauses beaconing, LoRa TX/RX
 *   processing, digipeating, iGate and KISS, and shows "Updating" on its
 *   display.
 */

#pragma once

#include <Arduino.h>
#include "ble_utils.h"

#ifdef HAS_BLE_OTA

namespace BLE_OTA {

    #define BLE_OTA_SERVICE_UUID    "62970001-5b20-49c7-ae44-96d63f6d50e8"
    #define BLE_OTA_CONTROL_UUID    "62970002-5b20-49c7-ae44-96d63f6d50e8"
    #define BLE_OTA_DATA_UUID       "62970003-5b20-49c7-ae44-96d63f6d50e8"

    static constexpr uint8_t  PROTOCOL_VERSION       = 1;
    static constexpr uint32_t INACTIVITY_TIMEOUT_MS  = 10000;

    enum Command : uint8_t {
        CMD_START = 0x01,
        CMD_END   = 0x02,
        CMD_ABORT = 0x03,
    };

    enum Notification : uint8_t {
        NOTIFY_READY   = 0x81,
        NOTIFY_ACK     = 0x82,
        NOTIFY_RESYNC  = 0x83,
        NOTIFY_DONE    = 0x84,
        NOTIFY_ERROR   = 0x85,
        NOTIFY_ABORTED = 0x86,
    };

    enum ErrorCode : uint8_t {
        ERR_BAD_REQUEST      = 1,   // malformed or unexpected command
        ERR_PROTOCOL_VERSION = 2,   // START.protocol not supported
        ERR_WRONG_BOARD      = 3,   // image built for another board
        ERR_IMAGE_SIZE       = 4,   // zero, or larger than the update slot
        ERR_BUSY             = 5,   // another transport (web upload) is updating
        ERR_NO_MEMORY        = 6,   // receive buffer could not be allocated
        ERR_FLASH            = 7,   // OTA_Utils rejected the image or a write failed
        ERR_VERIFY           = 8,   // SHA-256 mismatch or image validation failed
        ERR_TIMEOUT          = 9,   // no data for INACTIVITY_TIMEOUT_MS
        ERR_FLOW             = 10,  // window exceeded or data past the image end
        ERR_INCOMPLETE       = 11,  // END before every byte was received
    };

    // ── BLE stack task (called from the NimBLE characteristic callbacks,
    //    only for the active PIN-authenticated phone) ──────────────────────────
    void onControlWrite(const uint8_t* data, size_t length);
    void onDataWrite(const uint8_t* data, size_t length);

    // ── Main loop ───────────────────────────────────────────────────────────
    // True from an accepted START until the session ends (error, abort) or the
    // tracker reboots into the new firmware. While true, the main loop runs
    // only service() and the watchdog so nothing else touches the radio.
    bool inProgress();

    // Handles queued commands, moves received data to flash, sends ACKs,
    // enforces timeouts and reboots after DONE. Cheap when idle.
    void service();

}

#endif  // HAS_BLE_OTA
