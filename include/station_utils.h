#pragma once
#ifndef STATION_UTILS_H_
#define STATION_UTILS_H_

#include <Arduino.h>

namespace STATION_Utils {

    // Send the device's own APRS position beacon via LoRa.
    // Uses Config.gpsSource to get position (GPS, fixed, external).
    // Called by device_role.cpp on the beacon interval timer.
    void sendBeacon();

    // Queue a packet for LoRa TX (used by digi and iGate downlink).
    // Packets are dequeued and sent by processOutputPacketBuffer().
    void addToOutputPacketBuffer(const String& packet);

    void processOutputPacketBuffer();

    // Track recently heard callsigns (for display + dedup).
    void updateLastHeard(const String& callsign);
    String getLastHeardSummary();  // returns "CALL1  CALL2  CALL3" for display

    // Dedup guard: true if we've seen this exact payload from this sender
    // in the last ~30 seconds (prevents digi loops).
    bool isInHashBuffer(const String& callsign, const String& payload);

}

#endif
