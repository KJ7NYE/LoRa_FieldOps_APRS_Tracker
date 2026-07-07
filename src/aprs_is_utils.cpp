/* LoRa APRS Tracker — APRS-IS connection utilities
 *
 * Adapted from richonguzman/LoRa_APRS_iGate (CA2RXU) for the tracker's
 * Config struct layout (Config.aprsIS vs Config.aprs_is, single beacons[]
 * array for callsign, etc.).
 */

#ifdef HAS_WIFI

#include <WiFi.h>
#include <WiFiClient.h>
#include <APRSPacketLib.h>
#include "aprs_is_utils.h"
#include "dedup_utils.h"
#include "configuration.h"
#include "lora_utils.h"
#include "station_utils.h"
#include "display.h"
#include "logger.h"
#include "log_buffer.h"

extern Configuration    Config;
extern logging::Logger  logger;
extern String           versionNumber;

static WiFiClient   aprsIsClient;
static bool         passcodeValid   = false;
static uint32_t     lastConnectTry  = 0;
static const uint32_t RECONNECT_MS  = 30000UL;   // retry interval
static bool         _needsBeacon    = false;      // set true after each successful connect

// ── iGate upload dedup ────────────────────────────────────────────────────
// Prevents uploading both the direct copy and digipeated copies of the same
// frame (same originating callsign + payload, different digi path).
// Intentionally a separate PacketDedup instance from the digi dedup in
// station_utils so the two subsystems don't suppress each other.
// 50-slot ring, 60 s TTL — see include/dedup_utils.h.
static PacketDedup igDedup;

static bool igIsNew(const String& sender, const String& payload) {
    return igDedup.isNew(sender, payload);
}

// Compute the standard APRS-IS passcode (Friedman algorithm) from a callsign.
// Strips the SSID (everything after '-') before hashing.
// Returns the 15-bit passcode as a decimal string.
static String computePasscode(const String& callsign) {
    int dash = callsign.indexOf('-');
    String base = (dash > 0) ? callsign.substring(0, dash) : callsign;
    base.toUpperCase();
    uint16_t hash = 0x73e2;
    for (int i = 0; i < (int)base.length(); i += 2) {
        hash ^= (uint16_t)(uint8_t)base[i] << 8;
        if (i + 1 < (int)base.length())
            hash ^= (uint16_t)(uint8_t)base[i + 1];
    }
    return String(hash & 0x7FFF);
}

namespace APRS_IS_Utils {

    bool isConnected() {
        return aprsIsClient.connected();
    }

    void upload(const String& line) {
        if (!aprsIsClient.connected()) return;
        aprsIsClient.print(line + "\r\n");
    }

    void connect() {
        if (aprsIsClient.connected()) return;

        const String& server   = Config.aprsIS.server;
        uint16_t      port     = Config.aprsIS.port;
        const String& callsign = Config.beacons[0].callsign;
        // Use override passcode if configured; otherwise auto-derive from callsign.
        bool overridePasscode = Config.aprsIS.passcode.length() > 0;
        String passcode = overridePasscode
            ? Config.aprsIS.passcode
            : computePasscode(callsign);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "APRS-IS",
                   "Passcode: %s (%s)", passcode.c_str(),
                   overridePasscode ? "override" : "auto from callsign");
        const String& filter   = Config.aprsIS.filter;

        if (server.length() == 0 || callsign.length() == 0) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "APRS-IS", "Server or callsign not configured");
            return;
        }

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "APRS-IS", "Connecting to %s:%d ...", server.c_str(), port);

        // 3 attempts, 3 s TCP SYN timeout each, 1 s between tries.
        // Worst-case blocking: ~13 s (vs. ~25 s with the old 5-attempt / platform-default path).
        uint8_t attempts = 0;
        while (!aprsIsClient.connect(server.c_str(), port, 3000) && attempts < 3) {
            delay(1000);
            attempts++;
        }

        if (!aprsIsClient.connected()) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "APRS-IS", "Connection failed after %d attempts", attempts);
            LogBuffer::pushf(LogBuffer::TYPE_NET, "APRS-IS connect failed after %d attempts", attempts);
            return;
        }

        // Login string: user CALL pass PASS vers FIRMWARE filter FILTER
        String login = "user ";
        login += callsign;
        login += " pass ";
        login += passcode;
        login += " vers LoRaAPRS ";
        login += versionNumber;
        if (filter.length() > 0) {
            login += " filter ";
            login += filter;
        }
        upload(login);

        // Verify passcode (server echoes "#" lines; look for "verified")
        uint32_t t0 = millis();
        while (millis() - t0 < 3000) {
            if (aprsIsClient.available()) {
                String line = aprsIsClient.readStringUntil('\n');
                line.trim();
                if (line.startsWith("#")) {
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "APRS-IS", "%s", line.c_str());
                    passcodeValid = (line.indexOf("verified") != -1);
                    if (passcodeValid) break;
                }
            }
        }

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "APRS-IS", "Connected. Passcode %s",
                   passcodeValid ? "VALID" : "INVALID (Rx only)");
        LogBuffer::pushf(LogBuffer::TYPE_NET, "APRS-IS connected (%s)",
                         passcodeValid ? "validated" : "rx-only");
        _needsBeacon = true;   // signal caller to send an immediate self-beacon
    }

    void checkConnection() {
        if (aprsIsClient.connected()) return;
        if (millis() - lastConnectTry < RECONNECT_MS) return;
        lastConnectTry = millis();
        connect();
    }

    void resetConnectTimer() {
        lastConnectTry = 0;
    }

    // Returns true (and clears the flag) the first time called after each successful
    // connect().  Used by device_role to trigger an immediate self-beacon on (re)connect.
    bool consumeBeaconTrigger() {
        if (!_needsBeacon) return false;
        _needsBeacon = false;
        return true;
    }

    // Upload the iGate's own beacon directly (no qAR path — this is a direct TCP
    // submission; the server handles path attribution).  Also registers the packet
    // in the iGate upload dedup so that any RF echo of the same frame is suppressed.
    void uploadSelfBeacon(const String& packet) {
        if (!aprsIsClient.connected()) return;
        String sender = packet.substring(0, packet.indexOf(">"));
        int colonIdx  = packet.indexOf(":");
        String payload = (colonIdx >= 0) ? packet.substring(colonIdx) : packet;
        igIsNew(sender, payload);   // register in dedup (side-effect only)
        upload(packet);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "APRS-IS",
                   "Self-beacon: %s", packet.c_str());
    }

    // Strip any "qA?" gate-path bytes that some servers prepend
    static String stripStartingBytes(const String& packet) {
        int idx = packet.indexOf("\x3c\xff\x01");
        return (idx != -1) ? packet.substring(0, idx) : packet;
    }

    void processLoRaPacket(const String& packet) {
        if (!aprsIsClient.connected()) return;
        if (packet.indexOf("NOGATE") >= 0) return;

        // Dedup: skip if same originating callsign + payload was uploaded within 30 s.
        // Filters duplicates that arrive via multiple digi paths (e.g. direct + via KG7XXX).
        int colonIdx = packet.indexOf(":");
        if (colonIdx < 3) return;
        String sender  = packet.substring(0, packet.indexOf(">"));
        String payload = packet.substring(colonIdx);
        if (!igIsNew(sender, payload)) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "APRS-IS",
                       "Dedup skip: %s", sender.c_str());
            return;
        }

        const String& callsign = Config.beacons[0].callsign;

        String uploadLine = packet.substring(0, colonIdx);
        if (passcodeValid) {
            uploadLine += ",qAR,";
        } else {
            uploadLine += ",qAO,";
        }
        uploadLine += callsign;
        uploadLine += stripStartingBytes(packet.substring(colonIdx));

        upload(uploadLine);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "APRS-IS", "Uploaded: %s", uploadLine.c_str());
        LogBuffer::pushf(LogBuffer::TYPE_IGT, "IS: %s", uploadLine.c_str());
    }

    // Stations heard directly (no digi hop) within this window are treated
    // as "local" for downlink purposes — matches the APRS-IS IGate spec's
    // heard-list requirement (typically <=30-60 min in real deployments).
    static constexpr uint32_t HEARD_WINDOW_MS  = 30UL * 60UL * 1000UL;
    static constexpr int      HEARD_WINDOW_MIN = 30;

    void listenAPRSIS() {
        if (!aprsIsClient.connected()) return;

        while (aprsIsClient.available()) {
            String line = aprsIsClient.readStringUntil('\n');
            line.trim();
            if (line.length() == 0 || line.startsWith("#")) continue;

            logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "APRS-IS", "Rx: %s", line.c_str());

            // Downlink requires an explicit opt-in plus a validated passcode —
            // an unverified login already degrades RF->IS uploads to qAO, so
            // it must not be allowed to originate new RF traffic either.
            if (!Config.aprsIS.downlinkEnabled || !passcodeValid) continue;

            // Filter out third-party or loopback packets.
            // qAR/qAO = originated from RF — don't re-gate back to RF (avoids IS→RF→IS loops).
            if (line.indexOf("TCPIP")  != -1) continue;
            if (line.indexOf("NOGATE") != -1) continue;
            if (line.indexOf(",qAR,")  != -1) continue;
            if (line.indexOf(",qAO,")  != -1) continue;

            int arrowIdx = line.indexOf(">");
            if (arrowIdx <= 0) continue;
            const String& myCall = Config.beacons[0].callsign;
            String sender = line.substring(0, arrowIdx);
            if (sender == myCall) continue;   // own packet

            // Per the APRS-IS IGate spec, only gate directed messages (not
            // general position/object/status traffic) from IS back to RF —
            // gating everything is what caused RF stations to hear (and
            // re-gate) our own beacon a second time.
            int colonIdx = line.indexOf(":", arrowIdx);
            if (colonIdx < 3) continue;
            if (line.charAt(colonIdx + 1) != ':') continue;    // not a message packet
            if (line.charAt(colonIdx + 11) != ':') continue;   // malformed addressee field

            // Don't gate if the sender has itself just been heard on RF —
            // it's already local, so relaying its own message back doesn't
            // help and risks a loop.
            int senderHeardMin = STATION_Utils::minutesSinceHeard(sender);
            if (senderHeardMin >= 0 && senderHeardMin < HEARD_WINDOW_MIN) continue;

            String addressee = line.substring(colonIdx + 2, colonIdx + 11);
            addressee.trim();
            addressee.toUpperCase();

            // Only gate if the addressee has actually been heard directly on
            // RF recently — otherwise there's no reason to believe the
            // message can reach them at all.
            if (!STATION_Utils::wasHeardDirect(addressee, HEARD_WINDOW_MS)) continue;

            // Extract the original destination (TOCALL) for the third-party wrap.
            int commaIdx = line.indexOf(",", arrowIdx);
            String origToCall = (commaIdx > 0 && commaIdx < colonIdx)
                ? line.substring(arrowIdx + 1, commaIdx)
                : line.substring(arrowIdx + 1, colonIdx);

            // Wrap in standard APRS third-party format so other igates that
            // hear this retransmission recognize it already came from
            // APRS-IS (the TCPIP marker) and don't gate it back to IS —
            // this is what actually prevents the duplicate-log symptom.
            String txPacket = myCall + ">APRS";
            if (Config.beaconPath.length() > 0) {
                txPacket += ",";
                txPacket += Config.beaconPath;
            }
            txPacket += ":}";
            txPacket += sender;
            txPacket += ">";
            txPacket += origToCall;
            txPacket += ",TCPIP,";
            txPacket += myCall;
            txPacket += "*";
            txPacket += line.substring(colonIdx);

            LoRa_Utils::sendNewPacket(txPacket);
        }
    }

} // namespace APRS_IS_Utils

#endif // HAS_WIFI
