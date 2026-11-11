#pragma once
#ifndef QUERY_UTILS_H_
#define QUERY_UTILS_H_

#include <Arduino.h>

namespace QUERY_Utils {

    // Inspect a received raw AX.25 packet string and respond to APRS station
    // capability queries directed at our callsign (or the configured tactical
    // object name, if set) or broadcast to APRS/IGATE.
    //
    // Handles: ? (query list) ?APRS ?APRSD ?APRSH ?APRSL ?APRSP ?APRSS
    //          ?APRST ?APRSV ?PING ?VER ?IGATE ?TELEM, each with or without
    //          a trailing '?' and in any case.
    //
    // Also ACKs plain (non-query) APRS messages addressed to our callsign or
    // tactical name — no automated body reply, just the ack, since the
    // tracker cannot assume a KISS client is attached to do this instead —
    // except for remote-config commands (CSR/CSU/CSW), which are dispatched
    // to RemoteCfg_Utils and get a reply carrying the requested data/result.
    //
    // Responses/acks are queued via STATION_Utils::addToOutputPacketBuffer().
    // Duplicate queries/messages from the same sender are suppressed for 60 s.
    void processLoRaPacket(const String& rawPacket);

    // Entry point for a TNC2 packet a KISS host (serial, TCP, BLE or BT
    // Classic) hands us to transmit. If it is an APRS message addressed to
    // this device (callsign or tactical name, including one third-party
    // wrapped by an external iGate such as aprx), or to the APRS/IGATE
    // broadcast aliases, it is answered here via processLoRaPacket().
    // Returns true when the packet was consumed and must NOT be transmitted
    // (addressed to us only); false means the caller should transmit it as
    // usual (anything else, including broadcast queries we also answered).
    bool processKissHostPacket(const String& packet);

    // Next outgoing APRS message number (1-999, wrapping), shared by every
    // directed message this device originates.
    String nextMessageId();

    // `body` with an APRS 1.01 message number appended: "body{N" (no
    // closing brace; see query_utils.cpp).
    String withMessageId(const String& body);

}

#endif
