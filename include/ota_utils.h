/* In-device application firmware update for ESP32 targets.
 *
 * One writer shared by every transport that delivers a firmware.bin to the
 * running tracker: the web UI's /update upload today, the BLE updater next.
 * The transport owns framing, flow control and its own pre-checks (e.g. the
 * web UI rejects USB-flasher images by filename); this module owns the flash:
 *
 *   begin()  - validate the image header, open the OTA slot that is NOT running
 *   write()  - append the next chunk (strictly sequential)
 *   finish() - optionally check a caller-supplied SHA-256, let ESP-IDF validate
 *              the image, and select the slot for the next boot
 *   abort()  - stop and leave the running firmware selected
 *
 * Safety: the running slot is never touched and the boot selection only
 * changes after a fully validated image, so any failure, disconnect or power
 * loss mid-update leaves the device booting the firmware it is running now.
 *
 * Ownership: transports run in different tasks (the web upload in the
 * AsyncTCP task, the BLE updater in the main loop), and the session state
 * below is not thread-safe. A transport must therefore hold the claim
 * (claim()/release()) for as long as it calls begin/write/finish/abort, and
 * the claim admits one transport at a time. A transport that stops making
 * progress (e.g. a browser upload cut off mid-stream, which never reaches its
 * failure path) loses the claim to another transport after
 * STALE_CLAIM_TIMEOUT_MS, so a dead session cannot block updates until reboot.
 *
 * ESP32 only. The nRF52 (Heltec T114) updates through the Adafruit
 * bootloader's DFU instead (see the `otadfu` CLI command).
 */

#ifndef OTA_UTILS_H_
#define OTA_UTILS_H_

#ifndef ARDUINO_ARCH_NRF52

#include <Arduino.h>

namespace OTA_Utils {

    static constexpr size_t SHA256_BYTES = 32;

    // A claim held without any begin()/write() for this long may be taken
    // over by another transport (see "Ownership" above).
    static constexpr uint32_t STALE_CLAIM_TIMEOUT_MS = 30000;

    // Transports that can deliver a firmware image.
    enum class Client : uint8_t { None = 0, WebUpload, Bluetooth };

    // Takes the update claim for `client`. Succeeds when the claim is free,
    // already held by `client`, or held by another client that has been idle
    // for STALE_CLAIM_TIMEOUT_MS (its open session, if any, is aborted).
    // Returns false while another transport is actively updating; `holderOut`
    // (optional) then names it so the caller can say who is busy.
    bool claim(Client client, Client* holderOut = nullptr);

    // Gives the claim back if `client` holds it; aborts any session it left
    // open. Safe to call when `client` does not hold the claim.
    void release(Client client);

    // Current claim holder (Client::None when free).
    Client holder();

    // Human-readable transport name for error messages ("web UI", "Bluetooth"),
    // used as "A <name> firmware update is in progress".
    const char* clientName(Client client);

    // Size of the app slot an update would be written to, or 0 when the
    // partition table has a single app slot (huge_app.csv boards), i.e. the
    // board cannot update itself. Read-only; no claim needed.
    size_t updateSlotSize();

    // ── Session API: call only while holding the claim ──────────────────────

    // Starts an update session. `firstChunk` must hold at least the first byte
    // of the image (checked for the ESP app-image magic). `imageSize`:
    //   0  - size unknown; the whole target slot is erased up front (blocks for
    //        a few seconds). Used by the web upload.
    //   >0 - exact image size; checked against the slot, and flash is erased
    //        4 KB at a time as write() reaches it, so no call blocks for long.
    // Any session still open is aborted first. Returns false and sets
    // lastError() on failure.
    bool begin(const uint8_t* firstChunk, size_t firstChunkLength, size_t imageSize = 0);

    // Appends `length` bytes. Returns false (and closes the session) on error.
    bool write(const uint8_t* data, size_t length);

    // Closes the session and, if the image is valid, makes it the boot slot.
    // `expectedSha256` (32 bytes) is compared with the SHA-256 of every byte
    // passed to write(); pass nullptr to rely on ESP-IDF's image validation
    // alone. The caller is responsible for rebooting afterwards.
    bool finish(const uint8_t* expectedSha256 = nullptr);

    // Stops the session without changing the boot selection. Safe to call at
    // any time, including when no session is open.
    void abort();

    bool          isActive();       // a session is open and accepting write()
    size_t        bytesWritten();   // bytes accepted by the current/last session
    const String& lastError();      // first failure of the current/last session; empty if none
    const char*   targetSlotLabel();// partition label being written, or "?"

}

#endif // ARDUINO_ARCH_NRF52
#endif // OTA_UTILS_H_
