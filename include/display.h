#pragma once
#ifndef DISPLAY_H_
#define DISPLAY_H_

#include <Arduino.h>

// Minimal status display — shows boot progress, then role/GPS/network status.
// No menu system, no keyboard nav, no profile selection.

void displaySetup();
void bootStatus(const char* msg);
void startupScreen(const String& versionDate);

// Update the status display — call each second or when state changes.
void displayStatus(
    const String& callsign,    // line 1 (large): device callsign
    const String& tactical,    // line 1: tactical callsign (if set, shown large; callsign below)
    const String& line2,       // role/mode + battery  e.g. "Tracker  B:85%"
    const String& line3,       // GPS lat/lon  e.g. "47.6062 -122.3321"
    const String& line4,       // 6-char grid + altitude + speed  e.g. "CN87qr  125m  35km/h"
    const String& line5,       // uptime  e.g. "Up 12m"
    const String& line6        // last heard  e.g. "Last: KJ7ABC-9"
);

// Show AP configuration screen — call repeatedly from inside the AP loop.
// Replaces the normal status screen while the device is in AP mode.
void displayAPMode(const String& ssid, const String& password);

// Show TX overlay for 2 s: "<< TX >>" header + the packet being sent.
// Called automatically by LoRa_Utils::sendNewPacket().
void displayTx(const String& packet);

// Legacy brief flash (no-op — superseded by displayTx).
void displayTxFlash();

// Turn display on/off (eco mode timeout).
void displayToggle(bool on);

// Apply display invert setting at runtime (no reboot required).
// Also updates Config.display.invertDisplay so save/reload is consistent.
void displaySetInvert(bool invert);

// Reset the eco-mode idle timer and wake the display if it was sleeping.
// Call on any user-visible event: button press, packet RX, packet TX.
// displayTx() calls this automatically — no need to call it on TX.
void displayActivity();

// Check eco-mode timeout — call once per second from the main loop.
// Blanks the display when idle for longer than timeoutMs.
// No-op when ecoMode is false or timeoutMs is 0.
void displayEcoTick(bool ecoMode, unsigned long timeoutMs);

#endif
