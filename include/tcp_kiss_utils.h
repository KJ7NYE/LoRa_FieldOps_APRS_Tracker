/* LoRa APRS Tracker — TCP KISS server utilities (WiFi boards only)
 *
 * Provides a RFC-5652-style KISS TNC server over TCP so any APRS application
 * (Xastir, APRX, Direwolf in gateway mode, etc.) can connect and exchange
 * packets with the LoRa radio.
 *
 * USB serial KISS is handled in serial_setup.cpp (always on in KISS mode).
 */

#pragma once
#ifndef TCP_KISS_UTILS_H_
#define TCP_KISS_UTILS_H_

#ifdef HAS_WIFI

#include <Arduino.h>

namespace TCP_KISS_Utils {

    // Start the TCP server on Config.tcpKISS.port.  Call once from setup().
    void setup();

    // Accept new clients, read inbound KISS frames, TX via LoRa.
    // Call every loop iteration when iGate role is active.
    void loop();

    // Send a packet (TNC2, no 3-byte prefix) to all connected TCP KISS
    // clients as a KISS DATA frame. `trailingKissFrame`, if given, is an
    // already-encoded KISS frame (the RX signal report) written in the same
    // call right after it, so nothing can land between the two. Nothing is
    // sent if `packet` isn't valid TNC2.
    void sendToClients(const String& packet, const String& trailingKissFrame = "");

}

#endif // HAS_WIFI
#endif // TCP_KISS_UTILS_H_
