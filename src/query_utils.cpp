/* query_utils.cpp — APRS station capability query handler.
 *
 * Supported queries (APRS 1.01 §13):
 *   Directed (addressed to our callsign, or to the configured tactical
 *   object name if one is set):
 *     ?APRSD  ?APRSH <CALL>  ?APRSL  ?APRSP  ?APRSS  ?APRST  ?APRSV
 *     ?PING?  ?VER
 *   Undirected (addressed to "APRS"):
 *     ?APRS?
 *   iGate query (addressed to "IGATE", iGate mode only):
 *     ?IGATE?
 *
 * Responses are queued through addToOutputPacketBuffer() so the
 * 200 ms inter-packet gap is respected and TX does not block the main loop.
 * Duplicate queries from the same sender are suppressed for 60 s.
 */

#include <APRSPacketLib.h>
#include "configuration.h"
#include "query_utils.h"
#include "station_utils.h"
#include "dedup_utils.h"
#include "version.h"
#include "logger.h"

extern Configuration   Config;
extern logging::Logger logger;


namespace QUERY_Utils {

    // ── Internal state ────────────────────────────────────────────────────────

    // Dedup to rate-limit responses: same query from same sender within 60 s is ignored.
    static PacketDedup queryDedup;

    // Outgoing message sequence number, 1–999 wrapping.
    static int msgCounter = 1;

    static String nextMsgNo() {
        String n = String(msgCounter++);
        if (msgCounter > 999) msgCounter = 1;
        return n;
    }

    // Extract the message number from a payload ending in `{NNN}`.
    // Returns an empty string if no message number is present.
    static String extractMsgNo(const String& payload) {
        int idx = payload.lastIndexOf('{');
        if (idx < 0) return "";
        String num = payload.substring(idx + 1);
        num.trim();
        return num;
    }

    // Build a directed APRS message reply addressed to `toCall`.
    static String buildReply(const String& toCall, const String& body) {
        const String& myCall = Config.beacons[0].callsign;
        const String& myPath = Config.beaconPath;
        return APRSPacketLib::generateMessagePacket(
            myCall, "APLRT1", myPath, toCall,
            body + "{" + nextMsgNo() + "}");
    }

    // Build an ACK for the given message number.
    static String buildAck(const String& toCall, const String& msgNo) {
        const String& myCall = Config.beacons[0].callsign;
        const String& myPath = Config.beaconPath;
        return APRSPacketLib::generateMessagePacket(
            myCall, "APLRT1", myPath, toCall,
            "ack" + msgNo);
    }


    // ── Public entry point ────────────────────────────────────────────────────

    void processLoRaPacket(const String& rawPacket) {
        // Message packets have the format:
        //   SENDER>DEST,PATH::ADDRESSEE :payload
        //                   ^^ two colons mark message type
        //
        // Find the AX.25 info field start: first ':' after the header.
        int firstColon = rawPacket.indexOf(':');
        if (firstColon < 3) return;

        // Must be a message: info field starts with ':' immediately after the first.
        if (rawPacket.charAt(firstColon + 1) != ':') return;

        // Addressee is 9 characters after '::'; the 11th char must be ':'.
        if (rawPacket.charAt(firstColon + 11) != ':') return;

        String addressee = rawPacket.substring(firstColon + 2, firstColon + 11);
        addressee.trim();
        addressee.toUpperCase();

        String msgPayload = rawPacket.substring(firstColon + 12);

        // Extract sender callsign (before '>').
        int arrowIdx = rawPacket.indexOf('>');
        if (arrowIdx <= 0) return;
        String sender = rawPacket.substring(0, arrowIdx);

        // Route: addressed to us directly (real callsign or tactical object
        // name, if configured), or to the broadcast aliases.
        const String& myCall = Config.beacons[0].callsign;
        String tactical = Config.beacons[0].tacticalCallsign;
        tactical.trim();
        tactical.toUpperCase();

        bool toUs    = (addressee == myCall) ||
                       (tactical.length() > 0 && addressee == tactical);
        bool toAPRS  = (addressee == "APRS");
        bool toIGATE = (addressee == "IGATE");

        if (!toUs && !toAPRS && !toIGATE) return;

        // Surface the message text on the status display (as a "Msg:"
        // indicator) for any addressed/broadcast packet that reaches this
        // point, whether it turns out to be a query or a plain message.
        // Persists until the next RX event (see STATION_Utils::updateLastHeard()).
        {
            String displayText = msgPayload;
            int msgBrace = displayText.indexOf('{');
            if (msgBrace >= 0) displayText = displayText.substring(0, msgBrace);
            displayText.trim();
            if (displayText.length() > 0) {
                STATION_Utils::setPendingMessage(displayText);
            }
        }

        // Must be a query.
        if (!msgPayload.startsWith("?")) return;

        // Strip message number before matching query keyword, then normalise to
        // uppercase so matching is case-insensitive (?aprsv == ?APRSV, etc.).
        String query = msgPayload;
        int braceIdx = query.indexOf('{');
        if (braceIdx >= 0) query = query.substring(0, braceIdx);
        query.trim();
        query.toUpperCase();

        // Rate-limit: ignore duplicate queries from the same sender.
        if (!queryDedup.isNew(sender, query)) return;

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO,
            "Query", "Query from %s: %s", sender.c_str(), query.c_str());

        // ACK the incoming message if it carried a sequence number.
        String msgNo = extractMsgNo(msgPayload);
        if (msgNo.length() > 0) {
            STATION_Utils::addToOutputPacketBuffer(buildAck(sender, msgNo));
        }

        // ── Dispatch ──────────────────────────────────────────────────────────

        if (query == "?APRS?" && (toAPRS || toUs)) {
            // Undirected general query — respond with our current position beacon.
            // sendBeacon() transmits immediately; the ACK above is queued and will
            // follow on the next output-buffer drain (200 ms gap).
            STATION_Utils::sendBeacon();

        } else if (query == "?APRSP") {
            // Directed position request — same response as ?APRS?.
            STATION_Utils::sendBeacon();

        } else if (query == "?APRSD") {
            // Stations heard directly (no digi hop).
            String list = STATION_Utils::getDirectHeardList();
            String reply = list.length() > 0
                ? "Directs: " + list
                : "No direct stations heard";
            STATION_Utils::addToOutputPacketBuffer(buildReply(sender, reply));

        } else if (query == "?APRSL") {
            // All recently heard stations, newest first.
            String list = STATION_Utils::getAllHeardList();
            String reply = list.length() > 0
                ? "Last: " + list
                : "No stations heard";
            STATION_Utils::addToOutputPacketBuffer(buildReply(sender, reply));

        } else if (query.startsWith("?APRSH")) {
            // Has-heard query for a specific callsign.
            String target = query.substring(6);
            target.trim();
            String reply;
            if (target.length() == 0) {
                reply = "Usage: ?APRSH CALLSIGN";
            } else {
                int minutes = STATION_Utils::minutesSinceHeard(target);
                if (minutes < 0) {
                    reply = target + " not heard";
                } else if (minutes == 0) {
                    reply = target + " heard <1min ago";
                } else {
                    reply = target + " heard " + String(minutes) + "min ago";
                }
            }
            STATION_Utils::addToOutputPacketBuffer(buildReply(sender, reply));

        } else if (query == "?APRSS") {
            // Current status text.
            String status = Config.beacons[0].status;
            if (status.length() == 0) status = "No status configured";
            STATION_Utils::addToOutputPacketBuffer(buildReply(sender, status));

        } else if (query == "?APRST" || query == "?PING?") {
            // Trace / ping — echo back our callsign.
            STATION_Utils::addToOutputPacketBuffer(
                buildReply(sender, "PING " + myCall));

        } else if (query == "?APRSV" || query == "?VER") {
            // Firmware version.
            STATION_Utils::addToOutputPacketBuffer(
                buildReply(sender, "LoRa APRS Tracker " + String(FIRMWARE_VERSION_DATE)));

        } else if ((query == "?IGATE?" || (toIGATE && query == "?IGATE?"))
                   && Config.deviceRole == ROLE_IGATE) {
            // iGate capability query — only iGate mode responds.
            STATION_Utils::addToOutputPacketBuffer(
                buildReply(sender, "IGATE Online"));
        }
    }

} // namespace QUERY_Utils
