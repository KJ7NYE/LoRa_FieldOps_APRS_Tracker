#include <APRSPacketLib.h>
#include "configuration.h"
#include "digi_utils.h"
#include "lora_utils.h"
#include "display.h"
#include "logger.h"

extern Configuration    Config;
extern logging::Logger  logger;

namespace DIGI_Utils {

    // Remove all '*' markers from a path segment list.
    static String removeAsterisks(const String& path) {
        String out = path;
        out.replace("*", "");
        return out;
    }

    // Try to produce a digipeated packet string.
    // Returns "" if the packet should not be repeated.
    static String buildDigipeatedPacket(const String& packet) {
        // Expect TNC2: CALL>TOCALL,PATH:payload
        int gtIdx = packet.indexOf('>');
        if (gtIdx < 0) return "";
        int firstColonIdx = packet.indexOf(':');
        if (firstColonIdx < 0 || firstColonIdx <= gtIdx) return "";

        String header  = packet.substring(0, firstColonIdx);  // everything before first ':'
        String payload = packet.substring(firstColonIdx);      // ':' + payload

        // Extract path portion (after the first ',')
        int firstComma = header.indexOf(',');
        if (firstComma < 0) return "";  // no path → nothing to digipeat
        String pathStr = header.substring(firstComma + 1);

        const String& myCall = Config.beacons[0].callsign;

        // --- WIDE1-1 handling ---
        if (pathStr.indexOf("WIDE1-1") >= 0 && pathStr.indexOf('*') < 0) {
            // Replace WIDE1-1 with myCall* (mark as digipeated)
            String newPath = pathStr;
            newPath.replace("WIDE1-1", myCall + "*");
            return header.substring(0, firstComma + 1) + newPath + payload;
        }

        // --- WIDE2-n handling ---
        int w2Idx = pathStr.indexOf("WIDE2-");
        if (w2Idx >= 0) {
            // Don't repeat if already marked by an asterisk before WIDE2
            String pre = pathStr.substring(0, w2Idx);
            if (pre.indexOf('*') >= 0) return "";

            char nChar = pathStr.charAt(w2Idx + 6);
            int  n     = nChar - '0';
            if (n < 1 || n > 7) return "";

            String newPath = removeAsterisks(pathStr);
            if (n == 1) {
                newPath.replace("WIDE2-1", myCall + "*");
            } else {
                // Decrement hop count
                String oldToken = "WIDE2-" + String(n);
                String newToken = myCall + "*,WIDE2-" + String(n - 1);
                newPath.replace(oldToken, newToken);
            }
            return header.substring(0, firstComma + 1) + newPath + payload;
        }

        return "";
    }

    void processLoRaPacket(const String& packet) {
        if (packet.length() == 0) return;
        if (packet.indexOf("NOGATE") >= 0) return;

        // Don't repeat our own packets
        String sender = packet.substring(0, packet.indexOf('>'));
        if (sender == Config.beacons[0].callsign) return;

        // Don't repeat packets already showing our callsign in the path
        int firstColon = packet.indexOf(':');
        if (firstColon < 0) return;
        String path = packet.substring(packet.indexOf(',') + 1, firstColon);
        if (path.indexOf(Config.beacons[0].callsign) >= 0) return;

        String repeated = buildDigipeatedPacket(packet);
        if (repeated.length() == 0) return;

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Digi", "Repeating: %s", repeated.c_str());
        delay(200);  // brief listen-before-talk pause
        LoRa_Utils::sendNewPacket(repeated);
    }

}
