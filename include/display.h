#pragma once
#ifndef DISPLAY_H_
#define DISPLAY_H_

#include <Arduino.h>

// Minimal status display — shows boot progress, then role/GPS/network status.
// No menu system, no keyboard nav, no profile selection.

void displaySetup();
void bootStatus(const char* msg);
void startupScreen(const String& versionDate);

// Update the status lines shown during normal operation.
// Call each second or when state changes.
void displayStatus(
    const String& line1,   // role + callsign, e.g. "iGate  KJ7NYE"
    const String& line2,   // WiFi/APRS-IS state or GPS state
    const String& line3,   // last heard callsign or "No fix"
    const String& line4    // uptime or packet count
);

// Flash "TX" briefly when a LoRa packet is transmitted.
void displayTxFlash();

// Turn display on/off (eco mode timeout).
void displayToggle(bool on);

#endif
