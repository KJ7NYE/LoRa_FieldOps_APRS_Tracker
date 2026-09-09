/* remote_cfg_utils.cpp — compact remote read/write configuration protocol.
 *
 * See include/remote_cfg_utils.h for the wire format. This module is
 * transport-agnostic: query_utils.cpp (RF) and aprs_is_utils.cpp (APRS-IS)
 * both call handleCommand() and queue/upload the returned reply body
 * themselves, using their own message-numbering and transport.
 *
 * Two of the ten writable fields (deviceRole, gpsSource) mirror the same
 * "edit now, apply after a reboot" behavior the serial CLI already documents
 * (serial_setup.cpp's `role set`/`role gps` commands) — RadioLib/WiFi/GPS
 * subsystem setup only runs once at boot, so changing these live would leave
 * the device in a half-applied state. The remaining eight fields
 * (tacticalCallsign, symbol, beaconPath, digiMode, nonSmartBeaconRate,
 * fixedPosition) are read fresh from Config every time they're used (beacon
 * packet generation, digipeat gating, beacon-interval timers), so writes to
 * those take effect immediately with no reboot.
 */

#include "remote_cfg_utils.h"
#include "configuration.h"
#include <vector>
#include <ctype.h>

extern Configuration Config;

namespace RemoteCfg_Utils {

    // ── Internal state ────────────────────────────────────────────────────────

    static uint32_t unlockExpiresAt   = 0;    // millis(); 0 = locked
    static String   unlockedBy        = "";   // callsign currently holding the write window

    static uint8_t  failedUnlockAttempts = 0;
    static uint32_t lockoutUntil         = 0; // millis(); 0 = not locked out

    static constexpr uint8_t  MAX_FAILED_ATTEMPTS = 5;
    static constexpr uint32_t LOCKOUT_MS          = 10UL * 60UL * 1000UL; // 10 min

    // Per-sender cooldown on read/command spam, independent of the content
    // dedup already applied by the caller (query_utils.cpp's queryDedup only
    // suppresses identical repeated payloads, not a burst of different ones).
    static constexpr uint32_t READ_COOLDOWN_MS = 3000;
    static String   lastSenderSeen = "";
    static uint32_t lastSeenAt     = 0;

    // Set by a successful CSW that touched a field requiring a reboot to
    // take effect (deviceRole, gpsSource). Polled from main.cpp so the
    // reboot happens only after the "CS OK ..." reply has had time to drain
    // through the output packet buffer, instead of firing immediately from
    // inside packet-processing and dropping the reply.
    static uint32_t rebootAt = 0; // millis(); 0 = no reboot pending
    static constexpr uint32_t REBOOT_DELAY_MS = 5000;


    // ── Small parsing helpers ────────────────────────────────────────────────

    static bool isAllDigits(const String& s, bool allowLeadingSign) {
        if (s.length() == 0) return false;
        int start = 0;
        if (allowLeadingSign && (s[0] == '-' || s[0] == '+')) start = 1;
        if (start >= (int)s.length()) return false;
        for (int i = start; i < (int)s.length(); i++) {
            if (!isdigit((unsigned char)s[i])) return false;
        }
        return true;
    }

    static bool isFloatLike(const String& s) {
        if (s.length() == 0) return false;
        int start = 0;
        if (s[0] == '-' || s[0] == '+') start = 1;
        if (start >= (int)s.length()) return false;
        bool sawDigit = false, sawDot = false;
        for (int i = start; i < (int)s.length(); i++) {
            char c = s[i];
            if (isdigit((unsigned char)c)) { sawDigit = true; continue; }
            if (c == '.' && !sawDot) { sawDot = true; continue; }
            return false;
        }
        return sawDigit;
    }

    // Validates a beaconPath-style token list: comma-separated tokens, each
    // 1-6 alnum chars optionally followed by "-" and 1-2 digits (SSID).
    static bool isSaneBeaconPath(const String& v) {
        if (v.length() == 0 || v.length() > 32) return false;
        int start = 0;
        while (start <= (int)v.length()) {
            int comma = v.indexOf(',', start);
            String tok = (comma < 0) ? v.substring(start) : v.substring(start, comma);
            if (tok.length() < 1 || tok.length() > 9) return false;
            int dash = tok.indexOf('-');
            String call = (dash < 0) ? tok : tok.substring(0, dash);
            String ssid = (dash < 0) ? ""  : tok.substring(dash + 1);
            if (call.length() < 1 || call.length() > 6) return false;
            for (int i = 0; i < (int)call.length(); i++) {
                char c = call[i];
                if (!isalnum((unsigned char)c)) return false;
            }
            if (dash >= 0) {
                if (!isAllDigits(ssid, false) || ssid.length() > 2) return false;
            }
            if (comma < 0) break;
            start = comma + 1;
        }
        return true;
    }


    // ── Field read/write table ───────────────────────────────────────────────
    // Kept as a small ordered list of codes so full/selective reads and error
    // messages ("BADFIELD <code>") stay in one place.

    static const char* ALL_CODES[] = { "RO", "TC", "SY", "BP", "DM", "BR", "GS", "LA", "LO", "EL" };
    static constexpr int ALL_CODES_COUNT = sizeof(ALL_CODES) / sizeof(ALL_CODES[0]);

    static bool isKnownCode(const String& code) {
        for (int i = 0; i < ALL_CODES_COUNT; i++) if (code == ALL_CODES[i]) return true;
        return false;
    }

    static String readField(const String& code) {
        if (code == "RO") return "RO=" + String((int)Config.deviceRole);
        if (code == "TC") return "TC=" + Config.beacons[0].tacticalCallsign;
        if (code == "SY") return "SY=" + Config.beacons[0].overlay + Config.beacons[0].symbol;
        if (code == "BP") return "BP=" + Config.beaconPath;
        if (code == "DM") return "DM=" + String((int)Config.digiMode);
        if (code == "BR") return "BR=" + String(Config.nonSmartBeaconRate);
        if (code == "GS") return "GS=" + String((int)Config.gpsSource);
        if (code == "LA") return "LA=" + String(Config.fixedPosition.latitude, 4);
        if (code == "LO") return "LO=" + String(Config.fixedPosition.longitude, 4);
        if (code == "EL") return "EL=" + String(Config.fixedPosition.elevation, 1);
        return "";
    }


    // ── Stage 1: read ────────────────────────────────────────────────────────

    static String handleRead(const String& args) {
        String reply = "";
        if (args.length() == 0) {
            for (int i = 0; i < ALL_CODES_COUNT; i++) {
                if (reply.length() > 0) reply += " ";
                reply += readField(ALL_CODES[i]);
            }
            return reply;
        }

        // Selective read: comma-separated codes. Validate all before building
        // any output, for a predictable all-or-nothing error like writes.
        int start = 0;
        while (start <= (int)args.length()) {
            int comma = args.indexOf(',', start);
            String code = (comma < 0) ? args.substring(start) : args.substring(start, comma);
            code.trim();
            if (!isKnownCode(code)) return "ERR BADFIELD " + code;
            if (comma < 0) break;
            start = comma + 1;
        }

        start = 0;
        while (start <= (int)args.length()) {
            int comma = args.indexOf(',', start);
            String code = (comma < 0) ? args.substring(start) : args.substring(start, comma);
            code.trim();
            if (reply.length() > 0) reply += " ";
            reply += readField(code);
            if (comma < 0) break;
            start = comma + 1;
        }
        return reply;
    }


    // ── Stage 2: unlock ──────────────────────────────────────────────────────

    static String handleUnlock(const String& sender, const String& token) {
        uint32_t now = millis();

        if (lockoutUntil != 0) {
            if (now < lockoutUntil) {
                return "ERR LOCKED_OUT " + String((lockoutUntil - now) / 1000);
            }
            lockoutUntil = 0;
            failedUnlockAttempts = 0;
        }

        if (Config.remoteCfg.token.length() == 0) {
            return "ERR DISABLED";
        }

        if (token.length() > 0 && token == Config.remoteCfg.token) {
            unlockExpiresAt = now + (uint32_t)Config.remoteCfg.unlockWindowSec * 1000UL;
            unlockedBy = sender;
            failedUnlockAttempts = 0;
            return "UNLOCKED " + String(Config.remoteCfg.unlockWindowSec);
        }

        failedUnlockAttempts++;
        if (failedUnlockAttempts >= MAX_FAILED_ATTEMPTS) {
            lockoutUntil = now + LOCKOUT_MS;
            failedUnlockAttempts = 0;
            return "ERR LOCKED_OUT " + String(LOCKOUT_MS / 1000);
        }
        return "ERR BADTOKEN";
    }


    // ── Stage 2: write ───────────────────────────────────────────────────────

    struct PendingWrite {
        String code;
        String value;
    };

    // Validates one field=value pair against its allowlist rule. On success,
    // fills `outError` empty. On failure, fills `outError` with the code that
    // failed ("BADFIELD <code>" or "BADVALUE <code>") and returns false.
    static bool validateField(const String& code, const String& value, String& outError) {
        if (!isKnownCode(code)) { outError = "BADFIELD " + code; return false; }

        if (code == "RO") {
            if (!isAllDigits(value, false)) { outError = "BADVALUE RO"; return false; }
            int v = value.toInt();
            if (v < 0 || v > 2) { outError = "BADVALUE RO"; return false; }
            #ifdef ARDUINO_ARCH_NRF52
            if (v == ROLE_IGATE) { outError = "BADVALUE RO"; return false; }
            #endif
        } else if (code == "TC") {
            if (value.length() > 9) { outError = "BADVALUE TC"; return false; }
        } else if (code == "SY") {
            if (value.length() != 2) { outError = "BADVALUE SY"; return false; }
        } else if (code == "BP") {
            if (!isSaneBeaconPath(value)) { outError = "BADVALUE BP"; return false; }
        } else if (code == "DM") {
            if (!isAllDigits(value, false)) { outError = "BADVALUE DM"; return false; }
            int v = value.toInt();
            if (v < 0 || v > 2) { outError = "BADVALUE DM"; return false; }
        } else if (code == "BR") {
            if (!isAllDigits(value, false)) { outError = "BADVALUE BR"; return false; }
            int v = value.toInt();
            if (v < 1 || v > 86400) { outError = "BADVALUE BR"; return false; }  // seconds, up to 24h
        } else if (code == "GS") {
            if (!isAllDigits(value, false)) { outError = "BADVALUE GS"; return false; }
            int v = value.toInt();
            if (v < 0 || v > 2) { outError = "BADVALUE GS"; return false; }
        } else if (code == "LA") {
            if (!isFloatLike(value)) { outError = "BADVALUE LA"; return false; }
            float v = value.toFloat();
            if (v < -90.0f || v > 90.0f) { outError = "BADVALUE LA"; return false; }
        } else if (code == "LO") {
            if (!isFloatLike(value)) { outError = "BADVALUE LO"; return false; }
            float v = value.toFloat();
            if (v < -180.0f || v > 180.0f) { outError = "BADVALUE LO"; return false; }
        } else if (code == "EL") {
            if (!isFloatLike(value)) { outError = "BADVALUE EL"; return false; }
            float v = value.toFloat();
            if (v < -500.0f || v > 9000.0f) { outError = "BADVALUE EL"; return false; }
        }

        outError = "";
        return true;
    }

    static void applyField(const String& code, const String& value) {
        if      (code == "RO") Config.deviceRole = (DeviceRole)value.toInt();
        else if (code == "TC") { String v = value; v.toUpperCase(); Config.beacons[0].tacticalCallsign = v; }
        else if (code == "SY") { Config.beacons[0].overlay = value.substring(0, 1); Config.beacons[0].symbol = value.substring(1, 2); }
        else if (code == "BP") Config.beaconPath = value;
        else if (code == "DM") Config.digiMode = (DigiMode)value.toInt();
        else if (code == "BR") Config.nonSmartBeaconRate = value.toInt();
        else if (code == "GS") Config.gpsSource = (GPSSource)value.toInt();
        else if (code == "LA") Config.fixedPosition.latitude  = value.toFloat();
        else if (code == "LO") Config.fixedPosition.longitude = value.toFloat();
        else if (code == "EL") Config.fixedPosition.elevation = value.toFloat();
    }

    static String handleWrite(const String& sender, const String& args) {
        uint32_t now = millis();
        if (unlockExpiresAt == 0 || now >= unlockExpiresAt || sender != unlockedBy) {
            unlockExpiresAt = 0;
            unlockedBy = "";
            return "ERR LOCKED";
        }
        if (args.length() == 0) return "ERR BADFIELD";

        // Pass 1: parse + validate every pair, all-or-nothing.
        std::vector<PendingWrite> writes;
        bool sawGS = false, gsFixed = false, sawLatLonElev = false;

        int start = 0;
        while (start <= (int)args.length()) {
            int comma = args.indexOf(',', start);
            String pair = (comma < 0) ? args.substring(start) : args.substring(start, comma);
            int eq = pair.indexOf('=');
            if (eq < 0) return "ERR BADFIELD " + pair;
            String code  = pair.substring(0, eq); code.trim(); code.toUpperCase();
            String value = pair.substring(eq + 1); value.trim();

            String errCode;
            if (!validateField(code, value, errCode)) return "ERR " + errCode;

            if (code == "GS") { sawGS = true; gsFixed = (value.toInt() == GPS_FIXED); }
            if (code == "LA" || code == "LO" || code == "EL") sawLatLonElev = true;

            writes.push_back({ code, value });
            if (comma < 0) break;
            start = comma + 1;
        }

        // Cross-field rule: writing fixed coordinates only makes sense paired
        // with GS=1 in the same batch (or already GPS_FIXED) — otherwise the
        // coordinates would be silently stored but not active.
        if (sawLatLonElev && !(sawGS && gsFixed) && Config.gpsSource != GPS_FIXED) {
            return "ERR BADVALUE GS";
        }

        // Pass 2: apply. Reboot required only if RO or GS was touched — every
        // other field is read live from Config, no reboot needed.
        bool needsReboot = false;
        String reply = "OK";
        for (auto& w : writes) {
            applyField(w.code, w.value);
            if (w.code == "RO" || w.code == "GS") needsReboot = true;
            reply += " " + w.code + "=" + w.value;
        }
        Config.writeFile();

        if (needsReboot) {
            rebootAt = now + REBOOT_DELAY_MS;
            reply += " REBOOT=" + String(REBOOT_DELAY_MS / 1000);
        }
        return reply;
    }


    // ── Public entry points ──────────────────────────────────────────────────

    bool isCommand(const String& text) {
        if (text.length() < 3) return false;
        String prefix = text.substring(0, 3);
        prefix.toUpperCase();
        return prefix == "CSR" || prefix == "CSU" || prefix == "CSW";
    }

    String handleCommand(const String& sender, const String& text) {
        if (!Config.remoteCfg.enabled) return "";

        uint32_t now = millis();
        if (sender == lastSenderSeen && (now - lastSeenAt) < READ_COOLDOWN_MS) {
            return "ERR TOOFAST";
        }
        lastSenderSeen = sender;
        lastSeenAt = now;

        String op = text.substring(0, 3);
        op.toUpperCase();
        String args = (text.length() > 3) ? text.substring(3) : "";
        args.trim();

        if (op == "CSR") return "CS " + handleRead(args);
        if (op == "CSU") return "CS " + handleUnlock(sender, args);
        if (op == "CSW") return "CS " + handleWrite(sender, args);
        return "";
    }

    // Polled once per loop (see main.cpp) to fire a deferred reboot after a
    // RO/GS write has had time for its "CS OK ..." reply to drain through
    // the output packet buffer.
    void pollReboot() {
        if (rebootAt == 0) return;
        if (millis() < rebootAt) return;
        #ifdef ARDUINO_ARCH_NRF52
            NVIC_SystemReset();
        #else
            ESP.restart();
        #endif
    }

} // namespace RemoteCfg_Utils
