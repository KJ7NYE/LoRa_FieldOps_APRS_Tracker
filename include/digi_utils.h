#ifndef DIGI_UTILS_H_
#define DIGI_UTILS_H_

#include <Arduino.h>

namespace DIGI_Utils {

    // Process a raw TNC2-format received LoRa packet (no 3-byte RSSI prefix).
    // Handles WIDE1-1 and WIDE2-n digipeating; transmits if the packet qualifies.
    void processLoRaPacket(const String& packet);

}

#endif // DIGI_UTILS_H_
