/* query_utils.cpp — APRS station capability query and message handler.
 *
 * Supported queries (APRS 1.01 §13), case-insensitive, each accepted with
 * or without a trailing '?' (?TELEM == ?TELEM?, ?PING == ?PING?):
 *   Directed (addressed to our callsign, or to the configured tactical
 *   object name if one is set):
 *     ?  (list of supported queries)
 *     ?APRSD  ?APRSH <CALL>  ?APRSL  ?APRSP  ?APRSS  ?APRST  ?APRSV
 *     ?PING  ?VER  ?TELEM
 *   Undirected (addressed to "APRS"):
 *     ?APRS
 *   iGate query (addressed to "IGATE", iGate mode only):
 *     ?IGATE
 *
 * Message numbers follow APRS 1.01 ch. 14: outgoing messages end in
 * "{N" (no closing brace) and the receiver answers "ackN". The closing
 * brace form "{MM}AA" is the APRS 1.1 reply-ack extension, which this
 * firmware does not implement; incoming reply-ack numbers are parsed and
 * acked as plain "ackMM". Incoming ack/rej messages are consumed silently.
 *
 * Plain (non-query) APRS messages addressed to our callsign or tactical
 * name are ACKed here too, independent of any attached KISS client — the
 * tracker is often deployed with no client attached at all, so it must
 * not rely on one to satisfy the sender's ack expectation. No automated
 * body reply is generated for ordinary free text, only the ack — except
 * for remote-config commands (CSR/CSU/CSW, see remote_cfg_utils.h), which
 * also get a reply carrying the requested data / write result.
 *
 * Responses/acks are queued through addToOutputPacketBuffer() so the
 * 200 ms inter-packet gap is respected and TX does not block the main loop.
 * When this device is an iGate, sendReply() also pushes the same packet
 * directly to APRS-IS — a query addressed to the iGate's own callsign may
 * have arrived over APRS-IS rather than RF, and the iGate cannot hear its
 * own RF transmission to gate the reply back up the normal way.
 * Duplicate queries/messages from the same sender are suppressed for 60 s.
 */

#include <APRSPacketLib.h>
#include "configuration.h"
#include "query_utils.h"
#include "station_utils.h"
#include "dedup_utils.h"
#include "remote_cfg_utils.h"
#include "version.h"
#include "logger.h"
#include "battery_utils.h"
#include "digi_utils.h"
#ifdef HAS_WIFI
#include "aprs_is_utils.h"
#endif

extern Configuration   Config;
extern logging::Logger logger;


namespace QUERY_Utils {

    // ── Internal state ────────────────────────────────────────────────────────

    // Dedup to rate-limit responses: same query from same sender within 60 s is ignored.
    static PacketDedup queryDedup;

    // Outgoing message sequence number, 1–999 wrapping. Shared by every
    // directed message this device originates (see nextMessageId()).
    static int msgCounter = 1;

    String nextMessageId() {
        String n = String(msgCounter++);
        if (msgCounter > 999) msgCounter = 1;
        return n;
    }

    String withMessageId(const String& body) {
        // APRS 1.01: "{" + 1-5 alphanumerics, NO closing brace. A trailing
        // "}" would advertise APRS 1.1 reply-acks, which we don't implement,
        // and clients without reply-ack support show "{N}" as message text.
        return body + "{" + nextMessageId();
    }

    // Extract the message number from a payload ending in "{N" (APRS 1.01)
    // or "{MM}AA" (APRS 1.1 reply-ack; MM is the number to ack, AA is the
    // sender's ack of one of ours and is ignored). Returns an empty string
    // if there is no valid number (1-5 alphanumerics).
    static String extractMsgNo(const String& payload) {
        int idx = payload.lastIndexOf('{');
        if (idx < 0) return "";
        String num = payload.substring(idx + 1);
        int replyAck = num.indexOf('}');
        if (replyAck >= 0) num = num.substring(0, replyAck);
        num.trim();
        if (num.length() == 0 || num.length() > 5) return "";
        for (unsigned i = 0; i < num.length(); i++) {
            if (!isalnum((unsigned char)num.charAt(i))) return "";
        }
        return num;
    }

    // True for an APRS ack/rej message ("ackN" / "rejN", optionally in the
    // reply-ack form "ackMM}AA"). These answer one of our messages and must
    // never be acked, displayed, or treated as a command.
    static bool isAckOrRej(const String& payload) {
        if (!payload.startsWith("ack") && !payload.startsWith("rej")) return false;
        const unsigned len = payload.length();
        if (len < 4 || len > 3 + 5 + 1 + 5) return false;
        for (unsigned i = 3; i < len; i++) {
            const char c = payload.charAt(i);
            if (!isalnum((unsigned char)c) && c != '}') return false;
        }
        return true;
    }

    // Build a directed APRS message reply addressed to `toCall`.
    static String buildReply(const String& toCall, const String& body) {
        const String& myCall = Config.beacons[0].callsign;
        const String& myPath = Config.beaconPath;
        return APRSPacketLib::generateMessagePacket(
            myCall, "APLRT1", myPath, toCall, withMessageId(body));
    }

    // Reply to "?": every supported query, sized to fit one APRS message
    // (67-character text limit, before the message ID). Aliases (?APRST =
    // ?PING, ?APRSV = ?VER) are left out to stay within that limit.
    static String buildQueryList(bool includeIgate) {
        String list = "Query: ?APRSD ?APRSH ?APRSL ?APRSP ?APRSS ?PING ?VER ?TELEM";
        if (includeIgate) list += " ?IGATE";
        return list;
    }

    // Normalise a query for matching: uppercase, trimmed, and without one
    // trailing '?' so "?TELEM" and "?TELEM?" (or "?PING?" and "?PING") are
    // the same query. A lone "?" (the query list) is left as-is.
    static String normaliseQuery(const String& text) {
        String query = text;
        query.trim();
        query.toUpperCase();
        if (query.length() > 1 && query.endsWith("?")) {
            query.remove(query.length() - 1);
            query.trim();
        }
        return query;
    }

    // Build an ACK for the given message number.
    static String buildAck(const String& toCall, const String& msgNo) {
        const String& myCall = Config.beacons[0].callsign;
        const String& myPath = Config.beaconPath;
        return APRSPacketLib::generateMessagePacket(
            myCall, "APLRT1", myPath, toCall,
            "ack" + msgNo);
    }

    // "<n>m" or "<n>h<n>m" — matches the uptime format shown on the status display.
    static String uptimeString() {
        uint32_t upSec = millis() / 1000;
        if (upSec < 3600) return String(upSec / 60) + "m";
        return String(upSec / 3600) + "h" + String((upSec % 3600) / 60) + "m";
    }

    // Build the ?TELEM? reply body: battery%, uptime, APRS-IS link state,
    // and digipeat/upload counters — a compact status line for infrastructure
    // monitoring (e.g. a course/event monitoring system polling field igates
    // and digipeaters).
    static String buildTelemetry() {
        int battPct = BATTERY_Utils::getBatteryPercent();
        String batt = (battPct < 0) ? "NA" : (String(battPct) + "%");

        String   isState    = "DOWN";
        uint32_t gateCount  = 0;
        #ifdef HAS_WIFI
        if (Config.deviceRole == ROLE_IGATE) {
            isState   = APRS_IS_Utils::getConnectionState();
            gateCount = APRS_IS_Utils::getUploadedCount();
        }
        #endif

        return "TELEM BATT=" + batt +
               " UP=" + uptimeString() +
               " IS=" + isState +
               " RPT=" + String(DIGI_Utils::getRepeatedCount()) +
               " GATE=" + String(gateCount);
    }

    // Queue a reply/ACK for RF transmission and, when this device is itself
    // an iGate, also push it directly to APRS-IS. An iGate can't hear its
    // own RF transmission to self-gate it the normal way, so a query that
    // arrived over APRS-IS addressed to this device's own callsign would
    // otherwise never make it back to the querying station — only stations
    // that hear the RF reply directly (or via another nearby iGate) would
    // see it.
    static void sendReply(const String& packet) {
        STATION_Utils::addToOutputPacketBuffer(packet);
        #ifdef HAS_WIFI
        if (Config.deviceRole == ROLE_IGATE) APRS_IS_Utils::uploadSelfBeacon(packet);
        #endif
    }


    // ── Public entry point ────────────────────────────────────────────────────

    void processLoRaPacket(const String& rawPacket) {
        // Unwrap third-party format (an iGate gating an APRS-IS message down
        // to RF wraps it as GATE>DEST,PATH:}ORIGSENDER>ORIGDEST,PATH::ADDR :payload
        // — see the txPacket construction in aprs_is_utils.cpp's listenAPRSIS()).
        // The wrapping gate's callsign is not the message's real sender, so
        // everything below must parse the inner packet, not the outer one.
        String packet = rawPacket;
        int wrapColon = packet.indexOf(':');
        if (wrapColon >= 3 && packet.charAt(wrapColon + 1) == '}') {
            packet = packet.substring(wrapColon + 2);
        }

        // Message packets have the format:
        //   SENDER>DEST,PATH::ADDRESSEE :payload
        //                   ^^ two colons mark message type
        //
        // Find the AX.25 info field start: first ':' after the header.
        int firstColon = packet.indexOf(':');
        if (firstColon < 3) return;

        // Must be a message: info field starts with ':' immediately after the first.
        if (packet.charAt(firstColon + 1) != ':') return;

        // Addressee is 9 characters after '::'; the 11th char must be ':'.
        if (packet.charAt(firstColon + 11) != ':') return;

        String addressee = packet.substring(firstColon + 2, firstColon + 11);
        addressee.trim();
        addressee.toUpperCase();

        String msgPayload = packet.substring(firstColon + 12);

        // Extract sender callsign (before '>'). For an unwrapped third-party
        // packet this is the ORIGINAL sender (e.g. KG7KMV), not the relaying
        // iGate — correct, since replies must go to whoever actually sent it.
        int arrowIdx = packet.indexOf('>');
        if (arrowIdx <= 0) return;
        String sender = packet.substring(0, arrowIdx);

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

        if (!toUs && !toAPRS && !toIGATE) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "Query",
                       "Ignoring message from %s addressed to \"%s\" (our SSID is %s)",
                       sender.c_str(), addressee.c_str(), myCall.c_str());
            return;
        }

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "Query",
                   "Message from %s addressed to our SSID \"%s\"", sender.c_str(), addressee.c_str());

        // An ack/rej answers one of our replies. This firmware doesn't
        // retry its own messages, so there is nothing to clear; just make
        // sure it isn't displayed, acked back, or parsed as a command.
        if (isAckOrRej(msgPayload)) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "Query",
                       "%s from %s: %s", msgPayload.startsWith("ack") ? "Ack" : "Reject",
                       sender.c_str(), msgPayload.c_str());
            return;
        }

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

        // Plain (non-query) message: ACK it if it carries a sequence number,
        // then stop — free-text messages get no automated reply, only
        // capability queries do below. The tracker acks on its own rather
        // than relying on an attached KISS client, since the common
        // deployment (Tracker/iGate/Digipeater in the field) has no client
        // attached at all. Only meaningful when addressed directly to us;
        // the broadcast aliases (APRS/IGATE) are for queries, not messages.
        if (!msgPayload.startsWith("?")) {
            if (!toUs) return;

            String text = msgPayload;
            int brace = text.indexOf('{');
            if (brace >= 0) text = text.substring(0, brace);
            text.trim();

            // Ack every copy, including retries: a sender retries because
            // our previous ack was lost, so a retry must be acked again
            // even though the duplicate filter below drops its content.
            String msgNo = extractMsgNo(msgPayload);
            if (msgNo.length() > 0) sendReply(buildAck(sender, msgNo));

            // Rate-limit: ignore duplicate messages from the same sender.
            if (!queryDedup.isNew(sender, text)) {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "Query",
                           "Repeat message from %s (msg# %s), re-acked only",
                           sender.c_str(), msgNo.length() ? msgNo.c_str() : "none");
                return;
            }
            if (msgNo.length() > 0) {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO,
                    "Query", "Message from %s, ACKed (msg# %s)", sender.c_str(), msgNo.c_str());
            } else {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO,
                    "Query", "Message from %s (no msg#, not acked)", sender.c_str());
            }

            // Remote-config command (CSR/CSU/CSW) — see remote_cfg_utils.h.
            // Handled after the ack above so a command still gets acked like
            // any other message; the command reply itself is a second,
            // separate directed message queued right behind it.
            if (RemoteCfg_Utils::isCommand(text)) {
                String replyBody = RemoteCfg_Utils::handleCommand(sender, text);
                if (replyBody.length() > 0) {
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO,
                        "RemoteCfg", "Command from %s: %s -> %s",
                        sender.c_str(), text.c_str(), replyBody.c_str());
                    sendReply(buildReply(sender, replyBody));
                }
            }
            return;
        }

        // Strip the message number, then normalise (case, trailing '?') so
        // ?aprsv, ?APRSV and ?APRSV? all match the same query.
        String queryText = msgPayload;
        int braceIdx = queryText.indexOf('{');
        if (braceIdx >= 0) queryText = queryText.substring(0, braceIdx);
        const String query = normaliseQuery(queryText);

        // Ack every copy, including retries (see the plain-message case above).
        String msgNo = extractMsgNo(msgPayload);
        if (msgNo.length() > 0) sendReply(buildAck(sender, msgNo));

        // Rate-limit: ignore duplicate queries from the same sender.
        if (!queryDedup.isNew(sender, query)) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "Query",
                       "Repeat query from %s: %s, re-acked only", sender.c_str(), query.c_str());
            return;
        }

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO,
            "Query", "Query from %s: %s", sender.c_str(), query.c_str());

        // ── Dispatch ──────────────────────────────────────────────────────────

        if (query == "?") {
            // Query list. Directed only: a "?" to APRS/IGATE would make every
            // station in range answer at once.
            if (toUs) sendReply(buildReply(sender, buildQueryList(Config.deviceRole == ROLE_IGATE)));

        } else if (query == "?APRS" && (toAPRS || toUs)) {
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
            sendReply(buildReply(sender, reply));

        } else if (query == "?APRSL") {
            // All recently heard stations, newest first.
            String list = STATION_Utils::getAllHeardList();
            String reply = list.length() > 0
                ? "Last: " + list
                : "No stations heard";
            sendReply(buildReply(sender, reply));

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
            sendReply(buildReply(sender, reply));

        } else if (query == "?APRSS") {
            // Current status text.
            String status = Config.beacons[0].status;
            if (status.length() == 0) status = "No status configured";
            sendReply(buildReply(sender, status));

        } else if (query == "?APRST" || query == "?PING") {
            // Trace / ping — echo back our callsign.
            sendReply(buildReply(sender, "PING " + myCall));

        } else if (query == "?APRSV" || query == "?VER") {
            // Firmware version.
            sendReply(buildReply(sender, "LoRa APRS Tracker " + String(FIRMWARE_VERSION_DATE)));

        } else if (query == "?TELEM") {
            // Infrastructure health snapshot — battery, uptime, APRS-IS link
            // state, digipeat/upload counters.
            sendReply(buildReply(sender, buildTelemetry()));

        } else if (query == "?IGATE" && Config.deviceRole == ROLE_IGATE) {
            // iGate capability query — only iGate mode responds.
            sendReply(buildReply(sender, "IGATE Online"));

        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Query",
                       "Unsupported query from %s: %s (no reply; \"?\" lists queries)",
                       sender.c_str(), query.c_str());
        }
    }

} // namespace QUERY_Utils
