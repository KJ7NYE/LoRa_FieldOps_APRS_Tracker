#pragma once
#ifndef STATION_UTILS_H_
#define STATION_UTILS_H_

#include <Arduino.h>

namespace STATION_Utils {

    // Send the device's own APRS position beacon via LoRa.
    // Uses Config.gpsSource to get position (GPS, fixed, external).
    // Called by device_role.cpp on the beacon interval timer.
    void sendBeacon();

    // Send an APRS status beacon: CALLSIGN>APLRT1,path:>status
    // Falls back to sendBeacon() if beacons[0].status is empty.
    void sendStatusBeacon();

    // Send an uncompressed position beacon with PHG extension.
    // PHG (Power-Height-Gain) advertises fixed-station RF capabilities.
    // Must use uncompressed format per APRS spec — sent on its own timer.
    void sendPHGBeacon();

    // Queue a packet for LoRa TX (used by digi and iGate downlink).
    // Packets are dequeued and sent by processOutputPacketBuffer().
    void addToOutputPacketBuffer(const String& packet);

    void processOutputPacketBuffer();

    // Update the heard-station log from a full raw AX.25 packet string.
    // Derives callsign and isDirect (no '*' in path) automatically.
    // Also keeps getLastHeardSummary() updated for the display.
    void updateLastHeard(const String& rawPacket);
    String getLastHeardSummary();  // returns the single most-recently-heard callsign

    // Query-response accessors (used by query_utils.cpp)
    // Returns space-separated callsigns heard directly (no digi hop), newest first.
    String getDirectHeardList(uint8_t maxEntries = 10);
    // Returns all recently heard callsigns, newest first.
    String getAllHeardList(uint8_t maxEntries = 10);
    // Returns elapsed minutes if callsign is in the heard log, -1 if not found.
    int minutesSinceHeard(const String& callsign);

    // Packet dedup (digi/iGate): true if this exact callsign+payload was seen
    // within the last ~30 seconds. Records the packet on first sight.
    bool isInHashBuffer(const String& callsign, const String& payload);

}

#endif
