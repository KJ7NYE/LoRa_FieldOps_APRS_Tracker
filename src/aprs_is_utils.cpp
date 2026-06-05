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
#include "configuration.h"
#include "lora_utils.h"
#include "display.h"
#include "logger.h"

extern Configuration    Config;
extern logging::Logger  logger;
extern String           versionNumber;

static WiFiClient   aprsIsClient;
static bool         passcodeValid   = false;
static uint32_t     lastConnectTry  = 0;
static const uint32_t RECONNECT_MS  = 30000UL;   // retry interval

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

        uint8_t attempts = 0;
        while (!aprsIsClient.connect(server.c_str(), port) && attempts < 5) {
            delay(1000);
            attempts++;
        }

        if (!aprsIsClient.connected()) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "APRS-IS", "Connection failed after %d attempts", attempts);
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
    }

    void checkConnection() {
        if (aprsIsClient.connected()) return;
        if (millis() - lastConnectTry < RECONNECT_MS) return;
        lastConnectTry = millis();
        connect();
    }

    // Strip any "qA?" gate-path bytes that some servers prepend
    static String stripStartingBytes(const String& packet) {
        int idx = packet.indexOf("\x3c\xff\x01");
        return (idx != -1) ? packet.substring(0, idx) : packet;
    }

    void processLoRaPacket(const String& packet) {
        if (!aprsIsClient.connected()) return;
        if (packet.indexOf("NOGATE") >= 0) return;

        const String& callsign = Config.beacons[0].callsign;

        // Build the upload line: ORIG_PATH,qAO,MYCALL:payload
        int colonIdx = packet.indexOf(":");
        if (colonIdx < 3) return;

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
    }

    void listenAPRSIS() {
        if (!aprsIsClient.connected()) return;

        while (aprsIsClient.available()) {
            String line = aprsIsClient.readStringUntil('\n');
            line.trim();
            if (line.length() == 0 || line.startsWith("#")) continue;

            logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "APRS-IS", "Rx: %s", line.c_str());

            // Downlink: re-TX IS packets to LoRa RF when passcode is valid
            if (!passcodeValid) continue;

            // Filter out third-party or loopback packets
            if (line.indexOf("TCPIP") != -1) continue;
            if (line.indexOf("NOGATE") != -1) continue;

            const String& callsign = Config.beacons[0].callsign;
            String sender = line.substring(0, line.indexOf(">"));
            if (sender == callsign) continue;   // own packet

            // Reformat for LoRa TX: strip any IS-injected path segments
            int colonIdx = line.indexOf(":");
            if (colonIdx < 3) continue;
            String txPacket = sender + ">" + "APLRG1";
            if (Config.beaconPath.length() > 0) {
                txPacket += ",";
                txPacket += Config.beaconPath;
            }
            txPacket += line.substring(colonIdx);

            LoRa_Utils::sendNewPacket(txPacket);
        }
    }

} // namespace APRS_IS_Utils

#endif // HAS_WIFI
