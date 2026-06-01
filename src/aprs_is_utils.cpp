#ifdef HAS_WIFI

#include <WiFiClient.h>
#include <WiFi.h>
#include <APRSPacketLib.h>
#include "configuration.h"
#include "aprs_is_utils.h"
#include "lora_utils.h"
#include "station_utils.h"
#include "display.h"
#include "logger.h"

extern Configuration    Config;
extern logging::Logger  logger;
extern String           versionNumber;

static WiFiClient   aprsIsClient;
static bool         passcodeValid   = false;
static uint32_t     lastConnCheck   = 0;
static uint32_t     lastRxTime      = 0;

// APRS-IS server sends a verified login reply containing "verified" when the
// passcode is accepted, or "unverified" if the passcode is wrong/omitted.
static bool loginParsed = false;


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

        const String& callsign = Config.beacons[0].callsign;
        if (callsign.length() == 0 || callsign == "NOCALL-7") {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "APRS-IS", "Callsign not set, skipping connect");
            return;
        }
        if (Config.aprsIS.server.length() == 0) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "APRS-IS", "Server not configured");
            return;
        }

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "APRS-IS",
                   "Connecting to %s:%u ...", Config.aprsIS.server.c_str(), Config.aprsIS.port);

        uint8_t tries = 0;
        while (!aprsIsClient.connect(Config.aprsIS.server.c_str(), Config.aprsIS.port) && tries < 10) {
            delay(500);
            tries++;
        }

        if (!aprsIsClient.connected()) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "APRS-IS", "Connection failed after %u tries", tries);
            return;
        }

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "APRS-IS",
                   "Connected to %s:%u", Config.aprsIS.server.c_str(), Config.aprsIS.port);

        // Send login line
        String login = "user ";
        login += callsign;
        login += " pass ";
        login += Config.aprsIS.passcode;
        login += " vers LoRaFieldOps ";
        login += versionNumber;
        if (Config.aprsIS.filter.length() > 0) {
            login += " filter ";
            login += Config.aprsIS.filter;
        }
        upload(login);

        loginParsed  = false;
        passcodeValid = false;
        lastRxTime    = millis();
    }

    void disconnect() {
        aprsIsClient.stop();
        passcodeValid = false;
        loginParsed   = false;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "APRS-IS", "Disconnected");
    }

    // Strip the leading RSSI/SNR prefix bytes that lora_utils prepends.
    // The tracker wraps raw received frames as 3 printable-header bytes + payload.
    static String stripLoRaPrefix(const String& raw) {
        if (raw.length() > 3) return raw.substring(3);
        return raw;
    }

    // Build the upload string: SOURCE>DEST,PATH,qAO,IGATECALL:payload
    static String buildUploadPacket(const String& frame) {
        const String& igatecall = Config.beacons[0].callsign;
        int colonIdx = frame.indexOf(':');
        if (colonIdx < 0) return "";

        String header  = frame.substring(0, colonIdx);
        String payload = frame.substring(colonIdx);        // includes the ':'

        // Find where the path list starts (after '>')
        int gtIdx = header.indexOf('>');
        if (gtIdx < 0) return "";

        String result = header;
        // Append qAO gate tag after existing path
        result += ",qAO,";
        result += igatecall;
        result += payload;
        return result;
    }

    void processLoRaPacket(const String& rawPacket) {
        if (!aprsIsClient.connected()) return;

        String frame = rawPacket;
        // Strip RSSI prefix if still present (called from main loop after substring(3))
        if (frame.length() == 0) return;

        // Don't gate NOGATE packets
        if (frame.indexOf("NOGATE") >= 0) return;

        // Don't gate our own packets
        String sender = frame.substring(0, frame.indexOf('>'));
        if (sender == Config.beacons[0].callsign) return;

        String toUpload = buildUploadPacket(frame);
        if (toUpload.length() == 0) return;

        upload(toUpload);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "APRS-IS",
                   "Gated: %s", toUpload.c_str());
    }

    // Read any downlink packets from APRS-IS and retransmit via LoRa (bi-directional gate)
    void listenAPRSIS() {
        if (!aprsIsClient.connected()) return;

        while (aprsIsClient.available() > 0) {
            String line = aprsIsClient.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;
            lastRxTime = millis();

            // Parse server login verification
            if (!loginParsed && line.startsWith("#")) {
                if (line.indexOf("verified") >= 0 && line.indexOf("unverified") < 0) {
                    passcodeValid = true;
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "APRS-IS", "Login verified");
                } else if (line.indexOf("unverified") >= 0) {
                    passcodeValid = false;
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "APRS-IS", "Login UNVERIFIED (RX only)");
                }
                loginParsed = true;
                continue;
            }
            if (line.startsWith("#")) continue;  // other server comments

            // Downlink packet → re-TX via LoRa if passcode is valid
            if (!passcodeValid) continue;
            if (line.indexOf("TCPIP") < 0) continue;  // only re-gate TCPIP-tagged packets

            // Build TNC2 frame for LoRa TX (strip qA* path component)
            int firstColon = line.indexOf(':');
            if (firstColon < 0) continue;
            String loraFrame = line.substring(0, firstColon);
            // Remove TCPIP* and qA* path fragments
            int qIdx = loraFrame.indexOf(",qA");
            if (qIdx > 0) loraFrame = loraFrame.substring(0, qIdx);
            int tcpIdx = loraFrame.indexOf(",TCPIP*");
            if (tcpIdx > 0) loraFrame = loraFrame.substring(0, tcpIdx);
            loraFrame += line.substring(firstColon);

            LoRa_Utils::sendNewPacket(loraFrame);
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "APRS-IS",
                       "Downlinked: %s", loraFrame.c_str());
        }
    }

    // Called each loop iteration; reconnects if needed (every 30 s check)
    void checkConnection() {
        uint32_t now = millis();
        if (now - lastConnCheck < 30000) return;
        lastConnCheck = now;

        if (!WiFi.isConnected()) return;
        if (!aprsIsClient.connected()) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "APRS-IS", "Reconnecting...");
            connect();
        }
    }

}

#endif // HAS_WIFI
