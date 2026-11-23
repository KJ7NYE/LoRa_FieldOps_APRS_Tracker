/* In-device application firmware update for ESP32 targets.
 * See include/ota_utils.h for the contract shared by all transports.
 *
 * Slot handling: the image is written to the OTA slot that is NOT running
 * (esp_ota_get_next_update_partition) and then made the boot slot, so the
 * device alternates app0 <-> app1 on each update. A running slot can never be
 * rewritten in place: esp_ota_begin() refuses it with
 * ESP_ERR_OTA_PARTITION_CONFLICT.
 *
 * The USB web flasher's "Firmware Update" image (<board>_ota_update.bin)
 * includes boot_app0.bin at the otadata offset, which resets the boot
 * selection to app0 before writing app0, so USB and in-device updates stay
 * consistent whichever slot this module last selected.
 *
 * Boards on huge_app.csv (tbeam_433_aprs, lilygo_t3_433_aprs) have a single
 * app slot and cannot update themselves; begin() fails with a clear message.
 */

#ifndef ARDUINO_ARCH_NRF52

#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include <atomic>
#include "ota_utils.h"

namespace OTA_Utils {

    static constexpr uint8_t ESP_APP_IMAGE_MAGIC = 0xE9;    // first byte of every ESP app image

    // Claim state. Written from whichever task the transport runs in, hence atomic.
    static std::atomic<uint8_t>  claimHolder{static_cast<uint8_t>(Client::None)};
    static std::atomic<uint32_t> lastProgressMs{0};          // millis() of the holder's last begin/write

    static esp_ota_handle_t        otaHandle      = 0;
    static const esp_partition_t*  targetSlot     = nullptr;
    static bool                    sessionActive  = false;
    static size_t                  expectedSize   = 0;       // 0 = unknown (web upload)
    static size_t                  writtenBytes   = 0;
    static String                  errorMessage;
    static mbedtls_sha256_context  imageHash;
    static bool                    imageHashLive  = false;   // imageHash initialised, must be freed

    static void releaseImageHash() {
        if (imageHashLive) {
            mbedtls_sha256_free(&imageHash);
            imageHashLive = false;
        }
    }

    // Close the session without selecting the target slot for boot. The
    // partially written slot is left unbootable and is never selected.
    static void closeSession() {
        if (sessionActive) {
            esp_ota_abort(otaHandle);   // frees the handle
            sessionActive = false;
        }
        releaseImageHash();
    }

    // Record the first failure of the session (later ones are usually
    // consequences of it) and close the session.
    static bool fail(const String& message) {
        if (errorMessage.isEmpty()) errorMessage = message;
        closeSession();
        return false;
    }

    // ── Claim ───────────────────────────────────────────────────────────────

    bool claim(Client client, Client* holderOut) {
        uint8_t current = claimHolder.load();
        while (true) {
            if (current == static_cast<uint8_t>(client)) {
                lastProgressMs.store(millis());
                return true;
            }
            const bool free  = current == static_cast<uint8_t>(Client::None);
            const bool stale = !free && millis() - lastProgressMs.load() >= STALE_CLAIM_TIMEOUT_MS;
            if (!free && !stale) {
                if (holderOut != nullptr) *holderOut = static_cast<Client>(current);
                return false;
            }
            if (claimHolder.compare_exchange_weak(current, static_cast<uint8_t>(client))) break;
            // Lost a race (or spurious failure): `current` was reloaded, re-evaluate.
        }
        lastProgressMs.store(millis());
        // Taking over from an idle holder: its task has stopped calling us,
        // so its open session (if any) can be closed from here.
        closeSession();
        return true;
    }

    void release(Client client) {
        if (claimHolder.load() != static_cast<uint8_t>(client)) return;
        closeSession();
        claimHolder.store(static_cast<uint8_t>(Client::None));
    }

    Client holder() { return static_cast<Client>(claimHolder.load()); }

    const char* clientName(Client client) {
        switch (client) {
            case Client::WebUpload: return "web UI";
            case Client::Bluetooth: return "Bluetooth";
            default:                return "none";
        }
    }

    size_t updateSlotSize() {
        const esp_partition_t* running = esp_ota_get_running_partition();
        const esp_partition_t* next    = esp_ota_get_next_update_partition(nullptr);
        return (next == nullptr || next == running) ? 0 : next->size;
    }

    // ── Session ─────────────────────────────────────────────────────────────

    bool begin(const uint8_t* firstChunk, size_t firstChunkLength, size_t imageSize) {
        lastProgressMs.store(millis());
        closeSession();                 // a previous session was cut off mid-stream
        errorMessage  = "";
        writtenBytes  = 0;
        expectedSize  = imageSize;
        targetSlot    = nullptr;

        if (firstChunk == nullptr || firstChunkLength == 0 || firstChunk[0] != ESP_APP_IMAGE_MAGIC) {
            return fail("The file is not an ESP32 firmware image.");
        }

        const esp_partition_t* running = esp_ota_get_running_partition();
        const esp_partition_t* next    = esp_ota_get_next_update_partition(nullptr);
        if (next == nullptr || next == running) {
            return fail("This board has only one firmware slot, so it can't update itself. "
                        "Use the USB web flasher (Firmware Update) instead.");
        }
        if (imageSize > next->size) {
            return fail("The file is larger than the firmware slot.");
        }

        // Unknown size: erase the whole slot now (historic web-upload behaviour).
        // Known size: erase sector by sector inside esp_ota_write(), so no single
        // call blocks long enough to starve a BLE link or the main loop.
        const esp_err_t err = esp_ota_begin(next, imageSize == 0 ? OTA_SIZE_UNKNOWN : OTA_WITH_SEQUENTIAL_WRITES,
                                            &otaHandle);
        if (err != ESP_OK) {
            return fail(String("Could not start the update (") + esp_err_to_name(err) + ").");
        }
        targetSlot    = next;
        sessionActive = true;

        mbedtls_sha256_init(&imageHash);
        imageHashLive = true;
        mbedtls_sha256_starts(&imageHash, /*is224=*/0);
        return true;
    }

    bool write(const uint8_t* data, size_t length) {
        if (!sessionActive) return fail("No update in progress.");
        if (length == 0) return true;
        if (expectedSize != 0 && writtenBytes + length > expectedSize) {
            return fail("Received more data than the announced image size.");
        }

        const esp_err_t err = esp_ota_write(otaHandle, data, length);
        if (err != ESP_OK) {
            return fail(err == ESP_ERR_INVALID_SIZE
                            ? String("The file is larger than the firmware slot.")
                            : String("Write failed (") + esp_err_to_name(err) + ").");
        }
        mbedtls_sha256_update(&imageHash, data, length);
        writtenBytes += length;
        lastProgressMs.store(millis());
        return true;
    }

    bool finish(const uint8_t* expectedSha256) {
        if (!sessionActive) return fail("No update in progress.");
        if (expectedSize != 0 && writtenBytes != expectedSize) {
            return fail(String("The image is incomplete (") + writtenBytes + " of " + expectedSize + " bytes).");
        }

        uint8_t digest[SHA256_BYTES];
        mbedtls_sha256_finish(&imageHash, digest);
        releaseImageHash();
        if (expectedSha256 != nullptr && memcmp(digest, expectedSha256, SHA256_BYTES) != 0) {
            return fail("The image does not match its SHA-256 checksum (corrupted transfer).");
        }

        // esp_ota_end() validates the image (header, segments, checksum, chip)
        // and frees the handle whether or not validation passes.
        sessionActive = false;
        esp_err_t err = esp_ota_end(otaHandle);
        if (err != ESP_OK) {
            return fail(err == ESP_ERR_OTA_VALIDATE_FAILED
                            ? String("The uploaded file failed image validation (wrong board/chip or corrupted download).")
                            : String("Could not finish the update (") + esp_err_to_name(err) + ").");
        }
        err = esp_ota_set_boot_partition(targetSlot);
        if (err != ESP_OK) {
            return fail(String("Could not select the new firmware for boot (") + esp_err_to_name(err) + ").");
        }
        return true;
    }

    void abort() {
        closeSession();
    }

    bool          isActive()        { return sessionActive; }
    size_t        bytesWritten()    { return writtenBytes; }
    const String& lastError()       { return errorMessage; }
    const char*   targetSlotLabel() { return targetSlot ? targetSlot->label : "?"; }

}

#endif // ARDUINO_ARCH_NRF52
