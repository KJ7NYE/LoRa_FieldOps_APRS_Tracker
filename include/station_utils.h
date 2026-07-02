#pragma once
#ifndef STATION_UTILS_H_
#define STATION_UTILS_H_

#include <Arduino.h>

namespace STATION_Utils {

    // Send the device's own APRS position beacon via LoRa.
    // forceComment=true bypasses the every-N counter and always includes the comment.
    // Called by device_role.cpp on the beacon interval timer.
    void sendBeacon(bool forceComment = false);

    // Send an APRS status beacon: CALLSIGN>APLRT1,path:>status
    // Falls back to sendBeacon() if beacons[0].status is empty.
    void sendStatusBeacon();

    // Immediate on-demand TX from web UI / serial CLI — does NOT reset lastTxTime.
    // sendCommentBeaconNow: position beacon with comment always included.
    // sendStatusBeaconNow: status packet (falls back to position if status is empty).
    void sendCommentBeaconNow();
    void sendStatusBeaconNow();

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
    // Also keeps getLastHeardSummary() updated for the display: shows the
    // reported object's name (not the sender's callsign-SSID) when the
    // packet is an APRS Object Report. Clears any pending Msg: indicator
    // (see setPendingMessage()) at the start of each call.
    void updateLastHeard(const String& rawPacket);
    String getLastHeardSummary();  // returns the single most-recently-heard callsign or object name

    // Transient "message received" indicator for the status display.
    // Set by QUERY_Utils::processLoRaPacket() for any addressed/broadcast
    // message packet (query or free text). Persists until the next RX
    // event, since updateLastHeard() clears it at the start of each call.
    void setPendingMessage(const String& text);
    String getPendingMessage();

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
