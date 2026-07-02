/* LoRa APRS Tracker — Digipeater utilities
 *
 * Adapted from richonguzman/LoRa_APRS_iGate (CA2RXU).
 * Key differences from the iGate reference:
 *  - Callsign comes from Config.beacons[0].callsign (not Config.callsign)
 *  - Digi "mode" is derived from Config.deviceRole:
 *      ROLE_DIGIPEATER → mode 3 (WIDE1 + WIDE2)
 *      ROLE_TRACKER + digipeating=true → mode 2 (WIDE1 only)
 */

#include <APRSPacketLib.h>
#include "configuration.h"
#include "digi_utils.h"
#include "lora_utils.h"
#include "station_utils.h"
#include "logger.h"
#include "log_buffer.h"

extern Configuration    Config;
extern logging::Logger  logger;

namespace DIGI_Utils {

    static DigiMode digiMode() { return Config.digiMode; }

    static String buildPacket(const String& path, const String& packet) {
        const String& myCall = Config.beacons[0].callsign;
        int commaIdx = packet.indexOf(",");
        int colonIdx = packet.indexOf(":");

        DigiMode mode = digiMode();
        String tempPath = path;

        if (tempPath.indexOf("WIDE1-1") != -1 && mode >= DIGI_WIDE1) {
            if (tempPath.indexOf("WIDE1-1*") != -1) return "";  // alias already used
            tempPath.replace("WIDE1-1", myCall + "*");
        } else if (tempPath.indexOf("WIDE2-") != -1 && mode == DIGI_WIDE1_WIDE2) {
            if (tempPath.indexOf("WIDE2-1*") != -1 || tempPath.indexOf("WIDE2-2*") != -1) return "";  // alias already consumed
            if (tempPath.indexOf("WIDE2-1") != -1) {
                tempPath.replace("WIDE2-1", myCall + "*");
            } else if (tempPath.indexOf("WIDE2-2") != -1) {
                tempPath.replace("WIDE2-2", myCall + "*,WIDE2-1");
            } else {
                return "";
            }
        } else {
            return "";
        }

        // Reconstruct: keep source+dest, replace path, keep payload
        String result = packet.substring(0, commaIdx + 1);
        result += tempPath;
        result += packet.substring(colonIdx);
        return result;
    }

    String generateDigipeatedPacket(const String& packet) {
        // Extract path: everything between first "," and ":"
        int gtIdx    = packet.indexOf(">");
        int colonIdx = packet.indexOf(":");
        if (gtIdx < 0 || colonIdx < 0 || colonIdx <= gtIdx) return "";

        String destAndPath = packet.substring(gtIdx + 1, colonIdx);
        int commaIdx = destAndPath.indexOf(",");
        if (commaIdx < 0) return "";   // no path segment

        String path = destAndPath.substring(commaIdx + 1);
        DigiMode mode = digiMode();

        bool hasWide1 = path.indexOf("WIDE1-1") != -1;
        bool hasWide2 = path.indexOf("WIDE2-") != -1;

        if (mode == DIGI_WIDE1       && !hasWide1)             return "";
        if (mode == DIGI_WIDE1_WIDE2 && !hasWide1 && !hasWide2) return "";

        return buildPacket(path, packet);
    }

    void processLoRaPacket(const String& packet) {
        if (Config.digiMode == DIGI_OFF) return;
        if (packet.indexOf("NOGATE") >= 0) return;
        if (packet.length() < 10) return;

        // Guard: don't repeat own transmissions
        const String& myCall = Config.beacons[0].callsign;
        String sender = packet.substring(0, packet.indexOf(">"));
        if (sender == myCall) return;

        // Guard: don't relay message packets addressed to us — prevents IS→RF→IS loops
        // and ensures our own query responses are not re-digipeated by ourselves.
        // "Us" includes the configured tactical object name, if set.
        int dcIdx = packet.indexOf("::");
        if (dcIdx > 0) {
            String addressee = packet.substring(dcIdx + 2, dcIdx + 11);
            addressee.trim();
            String tactical = Config.beacons[0].tacticalCallsign;
            tactical.trim();
            if (addressee == myCall || (tactical.length() > 0 && addressee == tactical)) return;
        }

        // Dedup: skip if we've repeated this same payload within the TTL window.
        // Uses the shared PacketDedup instance in station_utils (50 slots, 60 s TTL).
        int colonIdx = packet.indexOf(":");
        String payload = (colonIdx >= 0) ? packet.substring(colonIdx + 1) : packet;
        if (STATION_Utils::isInHashBuffer(sender, payload)) return;

        String repeated = generateDigipeatedPacket(packet);
        if (repeated.length() == 0) return;

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Digi", "Repeating: %s", repeated.c_str());
        LogBuffer::pushf(LogBuffer::TYPE_DIG, "Relay: %s", repeated.c_str());
        delay(200);  // brief gap to avoid TX collision
        LoRa_Utils::sendNewPacket(repeated);
    }

} // namespace DIGI_Utils
