#pragma once
#ifndef QUERY_UTILS_H_
#define QUERY_UTILS_H_

#include <Arduino.h>

namespace QUERY_Utils {

    // Inspect a received raw AX.25 packet string and respond to APRS station
    // capability queries directed at our callsign (or the configured tactical
    // object name, if set) or broadcast to APRS/IGATE.
    //
    // Handles: ?APRS? ?APRSD ?APRSH ?APRSL ?APRSP ?APRSS ?APRST ?APRSV
    //          ?PING? ?VER ?IGATE?
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

}

#endif
