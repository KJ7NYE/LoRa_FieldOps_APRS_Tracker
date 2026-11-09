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
    // Only call when WiFi is connected — does not guard against WiFi-absent TCP attempts.
    void    checkConnection();

    // Reset the reconnect cooldown timer so the next checkConnection() call
    // attempts to connect immediately (e.g., right after WiFi (re)associates).
    void    resetConnectTimer();

    // Returns true once after each successful connect() — used to trigger an
    // immediate self-beacon.  Clears the flag on first call that returns true.
    bool    consumeBeaconTrigger();

    // Upload the iGate's own beacon directly and register it in the upload dedup.
    void    uploadSelfBeacon(const String& packet);

    // Count of third-party packets actually uploaded to APRS-IS since boot
    // (excludes self-beacons/self-replies — see uploadSelfBeacon()).
    uint32_t getUploadedCount();

    // Current APRS-IS link state as a short code for status/telemetry
    // reporting: "RW" (connected, verified, downlink enabled — fully
    // bidirectional), "R" (connected but unverified and/or downlink
    // disabled — matches this codebase's existing "Rx only" terminology),
    // or "DOWN" (no TCP session).
    String  getConnectionState();

    // Outcome of the APRS-IS login on the current connection:
    //   "verified"   — passcode accepted, uploads are gated
    //   "unverified" — passcode rejected (or no login response within the
    //                  timeout): the server keeps the socket open but
    //                  receive-only, so uploads are not accepted
    //   "pending"    — waiting for the server's login response
    //   "none"       — not connected
    // Note: a changed passcode only takes effect on the next connect.
    String   getLoginState();

    // Host and port of the current (or most recent) connection, as dialed —
    // may be a rotate pool name such as "rotate.aprs.net". Empty / 0 until
    // the first successful TCP connect.
    String   getConnectedHost();
    uint16_t getConnectedPort();

    // Name the server reported in its login response
    // ("# logresp CALL verified, server T2USASW" -> "T2USASW"), i.e. which
    // pool member this connection actually landed on. Empty until received.
    String   getServerName();

}

#endif // HAS_WIFI
#endif // APRS_IS_UTILS_H_
