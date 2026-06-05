/* LoRa APRS Tracker — Digipeater utilities
 *
 * WIDE1-1 and WIDE2-n digipeating adapted from richonguzman/LoRa_APRS_iGate.
 * When ROLE_DIGIPEATER: processes WIDE1+WIDE2 (full WIDEn-N).
 * When ROLE_TRACKER with digipeating=true: WIDE1-1 only.
 */

#pragma once
#ifndef DIGI_UTILS_H_
#define DIGI_UTILS_H_

#include <Arduino.h>

namespace DIGI_Utils {

    // Process a received LoRa packet (raw TNC2, no 3-byte prefix).
    // Checks the path, modifies it if digipeat rules apply, re-TXs.
    void processLoRaPacket(const String& packet);

    // Build the digipeated packet string for a given path/packet.
    // Returns "" if this packet should not be repeated.
    String generateDigipeatedPacket(const String& packet);

}

#endif
