/* LoRa APRS Tracker — APRS-IS connection utilities (WiFi boards only)
 *
 * Handles TCP connection to an APRS-IS server, packet upload from LoRa,
 * and bi-directional downlink (IS → LoRa RF) when passcode is valid.
 */

#pragma once
#ifndef APRS_IS_UTILS_H_
#define APRS_IS_UTILS_H_

#ifdef HAS_WIFI

#include <Arduino.h>

namespace APRS_IS_Utils {

    // (Re-)connect to the configured APRS-IS server and authenticate.
    void    connect();

    // True while the TCP socket to the APRS-IS server is open.
    bool    isConnected();

    // Upload a pre-formatted TNC2 line to APRS-IS (no \r\n needed).
    void    upload(const String& line);

    // Called each main-loop iteration: receive lines from the server and
    // optionally re-transmit them via LoRa (downlink / bi-directional gate).
    void    listenAPRSIS();

    // Build the upload line from a raw LoRa packet and send it.
    void    processLoRaPacket(const String& packet);

    // Check connection health; reconnect if dropped. Call from loop.
    void    checkConnection();

}

#endif // HAS_WIFI
#endif // APRS_IS_UTILS_H_
