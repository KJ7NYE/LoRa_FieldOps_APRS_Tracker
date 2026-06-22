/* station_utils.cpp — beacon TX, output packet buffer, last-heard tracking,
 * and hash-based dedup for the LoRa APRS Multi-Mode Firmware.
 *
 * No menu, no telemetry, no WX, no Winlink.  Clean single-beacon impl.
 */

#include <APRSPacketLib.h>
#include <TinyGPS++.h>
#include <queue>
#include "configuration.h"
#include "station_utils.h"
#include "dedup_utils.h"
#include "gps_utils.h"
#include "lora_utils.h"
#include "battery_utils.h"
#include "kiss_utils.h"
#include "display.h"
#include "logger.h"
#include "board_pinout.h"
#ifdef FAN_CTRL_PIN
#include "thermal_utils.h"
#endif
#ifdef HAS_WIFI
#include "aprs_is_utils.h"
#endif

extern Configuration    Config;
extern logging::Logger  logger;
extern TinyGPSPlus      gps;

// Declared in main.cpp
extern bool             sendUpdate;
extern bool             gpsIsActive;


// ── Globals defined in this TU ────────────────────────────────────────────────
// smartBeaconActive is owned by smartbeacon_utils.cpp — extern here.
// currentHeading / previousHeading are defined in gps_utils.cpp.

extern bool smartBeaconActive;
extern double currentHeading;
extern double previousHeading;
uint32_t    lastTx              = 0;
uint32_t    lastTxTime          = 0;

static uint8_t  updateCounter  = 100;


// ── Output packet buffer ──────────────────────────────────────────────────────

static std::queue<String>  outBuffer;
static uint32_t            lastOutTx = 0;
static constexpr uint32_t  OUT_DELAY_MS           = 200;   // normal inter-packet gap
static constexpr uint32_t  OUT_DELAY_AFTER_ACK_MS = 2000;  // give receiver time to re-arm RX after an ACK
static bool                lastOutWasAck = false;

namespace STATION_Utils {

    void addToOutputPacketBuffer(const String& packet) {
        outBuffer.push(packet);
    }

    void processOutputPacketBuffer() {
        if (outBuffer.empty()) return;
        uint32_t now = millis();
        uint32_t gap = lastOutWasAck ? OUT_DELAY_AFTER_ACK_MS : OUT_DELAY_MS;
        if (now - lastOutTx < gap) return;
        String pkt = outBuffer.front();
        outBuffer.pop();
        lastOutWasAck = (pkt.indexOf(":ack") >= 0);
        LoRa_Utils::sendNewPacket(pkt);
        lastOutTx = now;
    }


    // ── Heard-station log ────────────────────────────────────────────────────────
    // 20-slot ring buffer used by APRS query responses (?APRSD, ?APRSH, ?APRSL).
    // Also keeps a single most-recent callsign for the status display.

    struct HeardEntry {
        String   callsign;
        uint32_t timestamp;  // millis() when heard
        bool     isDirect;   // true if no '*' in path (heard without a digi hop)
    };

    static constexpr uint8_t HEARD_SLOTS = 20;
    static HeardEntry heardLog[HEARD_SLOTS];
    static uint8_t    heardHead  = 0;
    static uint8_t    heardCount = 0;
    static String     lastHeardCallsign = "";

    // Accepts the full raw AX.25 packet string: "SENDER>DEST,PATH:payload"
    void updateLastHeard(const String& rawPacket) {
        int arrowIdx = rawPacket.indexOf('>');
        if (arrowIdx <= 0) return;

        String sender = rawPacket.substring(0, arrowIdx);
        if (sender.length() == 0) return;
        lastHeardCallsign = sender;

        // Extract path segment (between first ',' and ':') to check for digi hops.
        // A '*' suffix on any path element means the packet was repeated by a digi.
        int firstComma = rawPacket.indexOf(',', arrowIdx);
        int firstColon = rawPacket.indexOf(':', arrowIdx);
        bool isDirect = true;
        if (firstComma > 0 && firstColon > firstComma) {
            String path = rawPacket.substring(firstComma + 1, firstColon);
            isDirect = (path.indexOf('*') < 0);
        }

        // Update existing entry for this callsign if already in the log.
        for (uint8_t i = 0; i < heardCount; i++) {
            uint8_t idx = (heardHead + HEARD_SLOTS - 1 - i) % HEARD_SLOTS;
            if (heardLog[idx].callsign == sender) {
                heardLog[idx].timestamp = millis();
                heardLog[idx].isDirect  = isDirect;
                return;
            }
        }

        // New callsign — write to ring head.
        heardLog[heardHead] = { sender, millis(), isDirect };
        heardHead = (heardHead + 1) % HEARD_SLOTS;
        if (heardCount < HEARD_SLOTS) heardCount++;
    }

    String getLastHeardSummary() {
        return lastHeardCallsign;
    }

    String getDirectHeardList(uint8_t maxEntries) {
        String out;
        uint8_t added = 0;
        for (uint8_t i = 0; i < heardCount && added < maxEntries; i++) {
            // Walk backwards from head (newest first)
            uint8_t idx = (heardHead + HEARD_SLOTS - 1 - i) % HEARD_SLOTS;
            if (heardLog[idx].isDirect) {
                if (out.length() > 0) out += ' ';
                out += heardLog[idx].callsign;
                added++;
            }
        }
        return out;
    }

    String getAllHeardList(uint8_t maxEntries) {
        String out;
        uint8_t added = 0;
        for (uint8_t i = 0; i < heardCount && added < maxEntries; i++) {
            uint8_t idx = (heardHead + HEARD_SLOTS - 1 - i) % HEARD_SLOTS;
            if (out.length() > 0) out += ' ';
            out += heardLog[idx].callsign;
            added++;
        }
        return out;
    }

    int minutesSinceHeard(const String& callsign) {
        for (uint8_t i = 0; i < heardCount; i++) {
            uint8_t idx = (heardHead + HEARD_SLOTS - 1 - i) % HEARD_SLOTS;
            if (heardLog[idx].callsign == callsign) {
                uint32_t elapsed = millis() - heardLog[idx].timestamp;
                return (int)(elapsed / 60000UL);
            }
        }
        return -1;
    }


    // ── Hash-based dedup buffer ───────────────────────────────────────────────
    // 50-slot ring, 60 s TTL — see include/dedup_utils.h for implementation.

    static PacketDedup digiDedup;

    bool isInHashBuffer(const String& callsign, const String& payload) {
        return !digiDedup.isNew(callsign, payload);
    }


    // ── Beacon TX ─────────────────────────────────────────────────────────────

    void sendBeacon(bool forceComment) {
        double beaconLat = 0, beaconLng = 0;
        float  beaconElev = 0;
        if (!GPS_Utils::getCurrentLocation(beaconLat, beaconLng, beaconElev)) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Beacon", "No position — skipping beacon");
            return;
        }

        bool   hasLiveGPS  = (Config.gpsSource == GPS_INTERNAL) && gps.location.isValid();
        double speedKnots  = hasLiveGPS ? gps.speed.knots()    : 0.0;
        double courseDeg   = hasLiveGPS ? gps.course.deg()     : 0.0;
        float  altFeet     = hasLiveGPS ? gps.altitude.feet()  : (beaconElev * 3.28084f);
        float  altMeters   = hasLiveGPS ? gps.altitude.meters(): beaconElev;

        Beacon& b   = Config.beacons[0];
        String  path = Config.beaconPath;
        // No path for high-alt / high-speed sources.
        if (hasLiveGPS && (gps.speed.kmph() > 200 || gps.altitude.meters() > 9000)) path = "";

        String packet;
        String tactical = b.tacticalCallsign;
        tactical.trim();
        if (tactical.length() > 0) {
            char ts[16];
            if (hasLiveGPS) {
                snprintf(ts, sizeof(ts), "%02d%02d%02dz",
                    gps.date.day() % 100, gps.time.hour() % 100, gps.time.minute() % 100);
            } else {
                snprintf(ts, sizeof(ts), "000000z");
            }
            // When sendSpeedCourse is on, speed/course occupy the Base91 extension and altitude
        // goes in the /A= comment below.  When sendSpeedCourse is off, fall back to the
        // original behaviour: altitude in the extension (if sendAltitude is on).
        bool altInExtension = Config.sendAltitude && !Config.sendSpeedCourse;
        packet = APRSPacketLib::generateObjectPacket(
                b.callsign, "APLRT1", path, tactical, String(ts), b.overlay,
                APRSPacketLib::encodeGPSIntoBase91(
                    beaconLat, beaconLng, courseDeg, speedKnots,
                    b.symbol, altInExtension, altFeet, false));
        } else if (b.micE.length() > 0) {
            // Mic-E encodes speed, course, and altitude independently — no trade-off needed.
            packet = APRSPacketLib::generateMiceGPSBeaconPacket(
                b.micE, b.callsign, b.symbol, b.overlay, path,
                beaconLat, beaconLng, courseDeg, speedKnots, altMeters);
        } else {
            bool altInExtension = Config.sendAltitude && !Config.sendSpeedCourse;
            packet = APRSPacketLib::generateBase91GPSBeaconPacket(
                b.callsign, "APLRT1", path, b.overlay,
                APRSPacketLib::encodeGPSIntoBase91(
                    beaconLat, beaconLng, courseDeg, speedKnots,
                    b.symbol, altInExtension, altFeet, false));
        }

        // Append standard /A=XXXXXX altitude comment when speed/course is in the extension
        // and sendAltitude is also enabled.  (Mic-E already carries altitude natively.)
        bool appendAltComment = Config.sendAltitude && Config.sendSpeedCourse
                                && (b.micE.length() == 0)
                                && altFeet > -1000.0f;
        if (appendAltComment) {
            char altBuf[12];
            int altFeetClamped = (altFeet > 0) ? (int)altFeet : 0;
            snprintf(altBuf, sizeof(altBuf), "/A=%06d", altFeetClamped);
            packet += altBuf;
        }

        // Battery voltage comment.
        if (Config.battery.sendVoltage) {
            String bv = BATTERY_Utils::getBatteryInfoVoltage();
            if (bv.length() > 0) {
                updateCounter++;
                int threshold = Config.battery.sendVoltageAlways
                    ? 1
                    : Config.sendCommentAfterXBeacons;
                if (forceComment || updateCounter >= threshold) {
                    packet += b.comment;
                    packet += " Bat=";
                    packet += String(bv.toFloat(), 2);
                    packet += "V";
                    if (!forceComment) updateCounter = 0;
                }
            }
        } else if (b.comment.length() > 0) {
            updateCounter++;
            if (forceComment || updateCounter >= Config.sendCommentAfterXBeacons) {
                packet += b.comment;
                if (!forceComment) updateCounter = 0;
            }
        }

        // Thermal warning — appended so operators see an alert via RF/APRS-IS.
        #ifdef FAN_CTRL_PIN
        if (THERMAL_Utils::isOverTemp()) {
            packet += " !OVERTEMP";
        }
        #endif

        // Register own beacon in the digi dedup buffer so that echoes heard back on
        // RF (e.g. digipeated copies) are not re-repeated or re-uploaded.
        {
            int selfColon = packet.indexOf(":");
            String selfPayload = (selfColon >= 0) ? packet.substring(selfColon + 1) : packet;
            isInHashBuffer(b.callsign, selfPayload);   // side-effect: inserts, return ignored
        }
        LoRa_Utils::sendNewPacket(packet);  // displayTx() is called inside sendNewPacket
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Beacon", "TX: %s", packet.c_str());
        // iGate: upload own beacon directly to APRS-IS (also registers in upload dedup).
        #ifdef HAS_WIFI
        if (Config.deviceRole == ROLE_IGATE) APRS_IS_Utils::uploadSelfBeacon(packet);
        #endif

        if (smartBeaconActive) {
            previousHeading = currentHeading;
        }
        lastTxTime      = millis();
        sendUpdate      = false;
    }

    void sendStatusBeacon() {
        const Beacon& b = Config.beacons[0];
        if (b.status.length() == 0) {
            // No status text configured — fall back to a normal position beacon.
            sendBeacon();
            return;
        }
        // Use generateStatusPacket so that path handling (WIDE-only, DIRECT=no-path) is
        // consistent with all other beacon types; avoids a dangling comma when path is empty.
        String statusText = b.status;
        #ifdef FAN_CTRL_PIN
        if (THERMAL_Utils::isOverTemp()) {
            statusText += " !OVERTEMP";
        }
        #endif
        String packet = APRSPacketLib::generateStatusPacket(b.callsign, "APLRT1", Config.beaconPath, statusText);
        {
            int selfColon = packet.indexOf(":");
            String selfPayload = (selfColon >= 0) ? packet.substring(selfColon + 1) : packet;
            isInHashBuffer(b.callsign, selfPayload);
        }
        LoRa_Utils::sendNewPacket(packet);   // displayTx() fires inside sendNewPacket
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Beacon", "Status TX: %s", packet.c_str());
        #ifdef HAS_WIFI
        if (Config.deviceRole == ROLE_IGATE) APRS_IS_Utils::uploadSelfBeacon(packet);
        #endif
        lastTxTime = millis();
        sendUpdate = false;   // prevent a pending auto-beacon from firing immediately after
    }

    // Send position beacon with comment forced in, without resetting the beacon
    // schedule timer.  Called by web UI / serial CLI on-demand Tx buttons.
    void sendCommentBeaconNow() {
        uint32_t savedTime    = lastTxTime;
        uint8_t  savedCounter = updateCounter;
        sendBeacon(true);           // forceComment=true bypasses every-N gate
        lastTxTime    = savedTime;  // restore timer so schedule is unaffected
        updateCounter = savedCounter;
    }

    // Send status beacon without resetting the beacon schedule timer.
    // Falls back to a plain position beacon if status text is empty.
    void sendStatusBeaconNow() {
        uint32_t savedTime = lastTxTime;
        sendStatusBeacon();
        lastTxTime = savedTime;
    }

    void sendPHGBeacon() {
        double beaconLat = 0, beaconLng = 0;
        float  beaconElev = 0;
        if (!GPS_Utils::getCurrentLocation(beaconLat, beaconLng, beaconElev)) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "PHG", "No position — skipping PHG beacon");
            return;
        }

        const Beacon& b = Config.beacons[0];
        const PHGConfig& p = Config.phg;

        // Build PHGphgd extension string — must be at start of comment field.
        // Uncompressed format only: APRS spec disallows PHG in compressed packets.
        char phgBuf[8];
        snprintf(phgBuf, sizeof(phgBuf), "PHG%c%c%c%c",
            '0' + (p.power       & 0x0F),
            '0' + (p.height      & 0x0F),
            '0' + (p.gain        & 0x0F),
            '0' + (p.directivity & 0x0F));
        String comment = String(phgBuf);
        if (b.comment.length() > 0) {
            comment += b.comment;
        }

        String packet = APRSPacketLib::generateUncompressedGPSBeaconPacket(
            b.callsign, "APLRT1", Config.beaconPath,
            b.overlay, b.symbol,
            (float)beaconLat, (float)beaconLng,
            comment);

        {
            int selfColon = packet.indexOf(":");
            String selfPayload = (selfColon >= 0) ? packet.substring(selfColon + 1) : packet;
            isInHashBuffer(b.callsign, selfPayload);
        }
        LoRa_Utils::sendNewPacket(packet);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "PHG", "TX: %s", packet.c_str());
        #ifdef HAS_WIFI
        if (Config.deviceRole == ROLE_IGATE) APRS_IS_Utils::uploadSelfBeacon(packet);
        #endif
        // Update shared lastTxTime so the regular beacon timer is pushed out;
        // prevents a regular beacon from firing seconds after a PHG beacon.
        lastTxTime = millis();
    }

}  // namespace STATION_Utils
