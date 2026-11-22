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
 * Not thread-safe: drive a session from one task at a time.
 * ESP32 only. The nRF52 (Heltec T114) updates through the Adafruit
 * bootloader's DFU instead (see the `otadfu` CLI command).
 */

#ifndef OTA_UTILS_H_
#define OTA_UTILS_H_

#ifndef ARDUINO_ARCH_NRF52

#include <Arduino.h>

namespace OTA_Utils {

    static constexpr size_t SHA256_BYTES = 32;

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
