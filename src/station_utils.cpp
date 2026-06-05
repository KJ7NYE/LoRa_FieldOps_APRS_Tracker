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
#include "gps_utils.h"
#include "lora_utils.h"
#include "battery_utils.h"
#include "kiss_utils.h"
#include "display.h"
#include "logger.h"

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
double      lastTxLat           = 0.0;
double      lastTxLng           = 0.0;
double      lastTxDistance      = 0.0;
uint32_t    lastTx              = 0;
uint32_t    lastTxTime          = 0;

static uint8_t  updateCounter  = 100;


// ── Output packet buffer ──────────────────────────────────────────────────────

static std::queue<String>  outBuffer;
static uint32_t            lastOutTx = 0;
static constexpr uint32_t  OUT_DELAY_MS = 200;  // inter-packet gap

namespace STATION_Utils {

    void addToOutputPacketBuffer(const String& packet) {
        outBuffer.push(packet);
    }

    void processOutputPacketBuffer() {
        if (outBuffer.empty()) return;
        uint32_t now = millis();
        if (now - lastOutTx < OUT_DELAY_MS) return;
        String pkt = outBuffer.front();
        outBuffer.pop();
        LoRa_Utils::sendNewPacket(pkt);
        lastOutTx = now;
    }


    // ── Last-heard (display) ──────────────────────────────────────────────────
    // Single most-recently-heard callsign for line3 of the status display.
    // No buffer, no expiry — always shows whoever was heard last.

    static String lastHeardCallsign = "";

    void updateLastHeard(const String& callsign) {
        if (callsign.length() == 0) return;
        lastHeardCallsign = callsign;
    }

    String getLastHeardSummary() {
        return lastHeardCallsign;
    }


    // ── Hash-based dedup buffer ───────────────────────────────────────────────

    static constexpr int   HASH_SIZE    = 25;
    static constexpr uint32_t HASH_TTL  = 30000;  // 30 seconds

    struct HashEntry {
        uint32_t hash;
        uint32_t seenAt;
    };

    static HashEntry hashBuf[HASH_SIZE];
    static int       hashHead = 0;

    // Simple djb2-style hash over a String.
    static uint32_t djb2(const String& s) {
        uint32_t h = 5381;
        for (unsigned i = 0; i < s.length(); i++) {
            h = ((h << 5) + h) + (uint8_t)s[i];
        }
        return h;
    }

    bool isInHashBuffer(const String& callsign, const String& payload) {
        uint32_t h = djb2(callsign + payload);
        uint32_t now = millis();
        for (int i = 0; i < HASH_SIZE; i++) {
            if (hashBuf[i].hash == h && (now - hashBuf[i].seenAt) < HASH_TTL) {
                return true;
            }
        }
        // Not found — record it.
        hashBuf[hashHead] = { h, now };
        hashHead = (hashHead + 1) % HASH_SIZE;
        return false;
    }


    // ── Beacon TX ─────────────────────────────────────────────────────────────

    void sendBeacon() {
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
            packet = APRSPacketLib::generateObjectPacket(
                b.callsign, "APLRT1", path, tactical, String(ts), b.overlay,
                APRSPacketLib::encodeGPSIntoBase91(
                    beaconLat, beaconLng, courseDeg, speedKnots,
                    b.symbol, Config.sendAltitude, altFeet, false));
        } else if (b.micE.length() > 0) {
            packet = APRSPacketLib::generateMiceGPSBeaconPacket(
                b.micE, b.callsign, b.symbol, b.overlay, path,
                beaconLat, beaconLng, courseDeg, speedKnots, altMeters);
        } else {
            packet = APRSPacketLib::generateBase91GPSBeaconPacket(
                b.callsign, "APLRT1", path, b.overlay,
                APRSPacketLib::encodeGPSIntoBase91(
                    beaconLat, beaconLng, courseDeg, speedKnots,
                    b.symbol, Config.sendAltitude, altFeet, false));
        }

        // Battery voltage comment.
        if (Config.battery.sendVoltage) {
            String bv = BATTERY_Utils::getBatteryInfoVoltage();
            if (bv.length() > 0) {
                updateCounter++;
                int threshold = Config.battery.sendVoltageAlways
                    ? 1
                    : Config.sendCommentAfterXBeacons;
                if (updateCounter >= threshold) {
                    packet += b.comment;
                    packet += " Bat=";
                    packet += String(bv.toFloat(), 2);
                    packet += "V";
                    updateCounter = 0;
                }
            }
        } else if (b.comment.length() > 0) {
            updateCounter++;
            if (updateCounter >= Config.sendCommentAfterXBeacons) {
                packet += b.comment;
                updateCounter = 0;
            }
        }

        LoRa_Utils::sendNewPacket(packet);  // displayTx() is called inside sendNewPacket
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Beacon", "TX: %s", packet.c_str());

        if (smartBeaconActive) {
            lastTxLat       = beaconLat;
            lastTxLng       = beaconLng;
            previousHeading = currentHeading;
            lastTxDistance  = 0.0;
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
        String packet = b.callsign + ">APLRT1," + Config.beaconPath + ":>" + b.status;
        LoRa_Utils::sendNewPacket(packet);   // displayTx() fires inside sendNewPacket
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Beacon", "Status TX: %s", packet.c_str());
        lastTxTime = millis();
    }

}  // namespace STATION_Utils
