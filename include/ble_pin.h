/* ble_pin.h — BLE pairing PIN helpers shared by the BLE stack (ble_utils.cpp)
 * and every configuration pathway (web UI, serial CLI, config file loader).
 *
 * The user-facing PIN is 4–6 decimal digits (default "123456"), stored as a
 * string so leading zeroes survive. BLE Passkey Entry pairing, however, always
 * uses a 6-digit passkey (0–999999), so a shorter PIN is zero-padded on the
 * left: PIN "1234" is passkey 001234. Phones whose pairing dialog insists on
 * six digits (iOS, some Android builds) must be given the padded form;
 * passkeyDigits() produces it so UIs and logs can show exactly what to type.
 */

#pragma once
#include <Arduino.h>

namespace BlePin {

    constexpr uint8_t     MIN_DIGITS  = 4;
    constexpr uint8_t     MAX_DIGITS  = 6;   // BLE passkey length
    constexpr const char* DEFAULT_PIN = "123456";   // six digits: no zero-padding needed on any phone

    // True when `pin` is 4–6 ASCII digits and nothing else (no sign, no spaces).
    inline bool isValid(const String& pin) {
        if (pin.length() < MIN_DIGITS || pin.length() > MAX_DIGITS) return false;
        for (unsigned int i = 0; i < pin.length(); ++i) {
            if (pin[i] < '0' || pin[i] > '9') return false;
        }
        return true;
    }

    // The 6-digit passkey the phone must enter, e.g. "1234" -> "001234".
    // Caller must have validated `pin` with isValid().
    inline String passkeyDigits(const String& pin) {
        String padded = pin;
        while (padded.length() < MAX_DIGITS) padded = "0" + padded;
        return padded;
    }

    // Numeric passkey for the BLE stack (NimBLE takes a uint32_t).
    inline uint32_t passkeyValue(const String& pin) {
        return (uint32_t)strtoul(pin.c_str(), nullptr, 10);
    }

}   // namespace BlePin
