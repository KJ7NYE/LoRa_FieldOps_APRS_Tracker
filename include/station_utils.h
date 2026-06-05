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

    // Queue a packet for LoRa TX (used by digi and iGate downlink).
    // Packets are dequeued and sent by processOutputPacketBuffer().
    void addToOutputPacketBuffer(const String& packet);

    void processOutputPacketBuffer();

    // Track the most recently heard callsign for the status display.
    void updateLastHeard(const String& callsign);
    String getLastHeardSummary();  // returns the single most-recently-heard callsign

    // Packet dedup (digi/iGate): true if this exact callsign+payload was seen
    // within the last ~30 seconds. Records the packet on first sight.
    bool isInHashBuffer(const String& callsign, const String& payload);

}

#endif
