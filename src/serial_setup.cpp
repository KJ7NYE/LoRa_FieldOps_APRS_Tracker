/* Copyright (C) 2025 Ricardo Guzman - CA2RXU
 *
 * This file is part of LoRa APRS Tracker.
 *
 * LoRa APRS Tracker is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <logger.h>
#include "serial_setup.h"
#include "board_pinout.h"
#include "configuration.h"
#include "ble_pin.h"
#include "display.h"
#include "smartbeacon_utils.h"
#include "kiss_utils.h"
#include "lora_utils.h"
#include "battery_utils.h"
#include "gps_utils.h"
#include "station_utils.h"
#include "query_utils.h"
#include "version.h"
#include "ble_utils.h"
#ifdef HAS_WIFI
#include <vector>
#include <WiFi.h>
#include "aprs_is_utils.h"
#include "wifi_utils.h"
#endif

extern Configuration        Config;
extern logging::Logger      logger;
extern bool                 digipeaterActive;


namespace SERIAL_Setup {

    // ---------------- state ----------------
    //
    // The CLI can be driven from two transports: USB serial (always) and the
    // BLE configuration link (HAS_BLE_CONFIG, PIN-protected trackers only).
    // Only one of them may hold the setup session at a time, since both edit
    // the single global Config. Every reply goes to `replyOut`, which points
    // at whichever transport supplied the input currently being processed
    // (see ScopedInputSource), so command handlers never need to know which
    // transport they are serving.
    enum class SerialMode { KISS, SETUP, LOG };          // USB port mode
    enum class InputSource { USB, BLE };

    static SerialMode           serialMode      = SerialMode::KISS;
    static InputSource          inputSource     = InputSource::USB;
    static Print*               replyOut        = &Serial;
    static bool                 exitArmed       = false;
    static String               usbLine;                // USB line / trigger-word accumulator
    static bool                 usbLastByteWasCR = false;
    static bool                 dirty           = false;    // RAM Config differs from flash
    static uint32_t             lastSetupActivityMs = 0;    // last input byte from the session owner

    // A session owner that has gone quiet this long (e.g. USB cable pulled
    // without 'exit', which most USB bridges cannot report) may be taken over
    // by the other transport instead of locking it out until reboot.
    static const uint32_t       SETUP_TAKEOVER_IDLE_MS = 120000;

    #ifdef HAS_BLE_CONFIG
    // The BLE link has no KISS or LOG mode: it is either idle (waiting for
    // 'setup', every other line silently ignored exactly like USB KISS mode,
    // so clients use the same entry handshake) or holding the setup session.
    static bool                 bleInSetup          = false;
    static String               bleLine;
    static bool                 bleLastByteWasCR    = false;
    static uint32_t             bleSeenGeneration   = 0;
    #endif
    static bool                 showSecrets     = false;
    static logging::LoggerLevel savedLogLevel   = logging::LoggerLevel::LOGGER_LEVEL_INFO;
    static logging::LoggerLevel currentLogLevel = logging::LoggerLevel::LOGGER_LEVEL_INFO;
    static String               kissSerialBuf   = "";

    // paste-import state
    static bool                 pasting         = false;
    static String               pasteBuf;
    static int                  pasteBraceDepth = 0;
    static bool                 pasteSawOpen    = false;
    static bool                 pasteInString   = false;
    static bool                 pasteEscapeNext = false;
    static const size_t         PASTE_MAX_BYTES = 16384;

    // Routes replies to `source` for the lifetime of the scope.
    class ScopedInputSource {
    public:
        ScopedInputSource(InputSource source, Print& output)
            : savedSource(inputSource), savedOutput(replyOut) {
            inputSource = source;
            replyOut    = &output;
        }
        ~ScopedInputSource() {
            inputSource = savedSource;
            replyOut    = savedOutput;
        }
    private:
        InputSource savedSource;
        Print*      savedOutput;
    };

    static bool usbOwnsSetup() { return serialMode == SerialMode::SETUP; }

    static bool bleOwnsSetup() {
        #ifdef HAS_BLE_CONFIG
            return bleInSetup;
        #else
            return false;
        #endif
    }

    // True when the transport whose input is being processed holds the session.
    static bool sourceOwnsSetup() {
        return inputSource == InputSource::USB ? usbOwnsSetup() : bleOwnsSetup();
    }

    // Collapses a CRLF pair into one line ending: returns true for a '\n'
    // that directly follows a '\r'. Without this every "cmd\r\n" produced two
    // prompts, and the second could be mistaken by a client for the end of
    // its *next* command's reply (likely over BLE, where replies arrive in
    // small notifications rather than one USB read).
    static bool isSecondHalfOfCRLF(char c, bool& lastByteWasCR) {
        const bool swallow = (c == '\n' && lastByteWasCR);
        lastByteWasCR = (c == '\r');
        return swallow;
    }

    // Flushes queued replies before the chip resets, so a BLE client sees the
    // final status line ("Rebooting...") instead of a silent disconnect.
    // USB keeps relying on the existing short delay() at each restart site.
    static void flushRepliesBeforeRestart() {
        #ifdef HAS_BLE_CONFIG
            if (inputSource == InputSource::BLE) BLE_Utils::flushConfigOutput(1500);
        #endif
    }

    // ---------------- helpers ----------------
    static void prompt()                   { replyOut->print(F("\n> ")); }
    static void ok(const String& msg)       { replyOut->println("OK: " + msg); dirty = true; exitArmed = false; }
    static void okClean(const String& msg)  { replyOut->println("OK: " + msg); exitArmed = false; }
    static void err(const String& msg)      { replyOut->println("ERR: " + msg); exitArmed = false; }

    static String maskSecret(const String& s) {
        if (showSecrets) return s;
        if (s.length() == 0) return "";
        return "***";
    }

    static int parseBoolTok(const String& tok) {
        String t = tok; t.toLowerCase();
        if (t == "on"  || t == "true"  || t == "1" || t == "yes") return 1;
        if (t == "off" || t == "false" || t == "0" || t == "no")  return 0;
        return -1;
    }

    static bool applyBool(const String& tok, bool& target, const char* name) {
        int v = parseBoolTok(tok);
        if (v < 0) { err(String(name) + " expects on/off"); return false; }
        target = (v == 1);
        ok(String(name) + " = " + (target ? "on" : "off"));
        return true;
    }

    static int splitTokens(const String& line, String* tokens, int maxN) {
        int n = 0;
        int i = 0;
        int len = line.length();
        while (i < len && n < maxN) {
            while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
            if (i >= len) break;
            int start = i;
            while (i < len && line[i] != ' ' && line[i] != '\t') i++;
            tokens[n++] = line.substring(start, i);
        }
        return n;
    }

    static String restOfLine(const String& line, int skipTokens) {
        int n = 0;
        int i = 0;
        int len = line.length();
        while (n < skipTokens && i < len) {
            while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
            if (i >= len) break;
            while (i < len && line[i] != ' ' && line[i] != '\t') i++;
            n++;
        }
        while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
        return line.substring(i);
    }

    // ---------------- printers ----------------
    static void printBanner() {
        replyOut->println();
        replyOut->println(F("================================================"));
        replyOut->println(F(" LoRa APRS Tracker - Serial Setup"));
        replyOut->println(F(" type 'help' for commands"));
        replyOut->println(F(" type 'exit' to return to KISS TNC mode"));
        replyOut->println(F("================================================"));
    }

    static void printHelp() {
        replyOut->println(F("\n-- core --"));
        replyOut->println(F("  help                       show this list"));
        replyOut->println(F("  show [section]             dump config (sections: beacons|lora|"));
        replyOut->println(F("                              smartcustom|display|bt|bat|ptt|wifi|other)"));
        replyOut->println(F("  show secrets               toggle masked password display"));
        replyOut->println(F("  save                       persist to tracker_conf.json"));
        replyOut->println(F("  export                     dump current saved tracker_conf.json"));
        replyOut->println(F("  import                     paste full tracker_conf.json (auto-end on"));
        replyOut->println(F("                              balanced braces, Ctrl-C aborts)"));
        replyOut->println(F("  discard                    leave without saving"));
        replyOut->println(F("  exit                       leave (errors if dirty)"));
        replyOut->println(F("  reboot                     reboot the device"));
        replyOut->println(F("  format YES-ERASE-ALL       wipe LittleFS/SPIFFS, reboot to defaults"));
        #ifdef ARDUINO_ARCH_NRF52
        replyOut->println(F("  otadfu                     enter BLE OTA DFU mode (nRF52 only)"));
        #endif
        replyOut->println(F("  log <off|error|warn|info|debug>"));
        replyOut->println(F("\n-- beacons --"));
        replyOut->println(F("  beacon callsign <CALL-SSID>"));
        replyOut->println(F("  beacon symbol <c>          overlay <c>          micE <0..7>"));
        replyOut->println(F("  beacon comment <text...>   status <text...>     label <text...>"));
        replyOut->println(F("  beacon tactical <text...>  (<=9 chars; empty = position report)"));
        replyOut->println(F("  beacon smart on|off"));
        replyOut->println(F("  beacon smartset <0..4>     (0=Runner 1=Bike 2=Car 3=Jetboat 4=Custom)"));
        replyOut->println(F("\n-- smartcustom (used when beacon smartset = 4) --"));
        replyOut->println(F("  smartcustom show"));
        replyOut->println(F("  smartcustom slowrate <sec>     slowspeed <km/h>"));
        replyOut->println(F("  smartcustom fastrate <sec>     fastspeed <km/h>"));
        replyOut->println(F("  smartcustom turnmindeg <deg>   turnslope <n>"));
        replyOut->println(F("  smartcustom turnmintime <sec>  (min spacing for corner beacons)"));
        replyOut->println(F("\n-- lora --"));
        replyOut->println(F("  lora freq <Hz>             sf <7..12>           bw <Hz>"));
        replyOut->println(F("  lora cr <5..8>             power <dBm>"));
        replyOut->println(F("  lora cad on|off            (Channel Activity Detection: listen before TX)"));
        replyOut->println(F("\n-- peripherals --"));
        replyOut->println(F("  display eco|turn180|led on|off"));
        replyOut->println(F("  display timeout <sec>"));
        replyOut->println(F("  bt on|off                  name <text>"));
        replyOut->println(F("  bt pin on|off              pin <4-6 digits>     (save + reboot)"));
        replyOut->println(F("  bat sendv|alwaysv on|off"));
        replyOut->println(F("  bat sleepv <volts>          bat read"));
        replyOut->println(F("  ptt on|off                 pin <n>              reverse on|off"));
        replyOut->println(F("  ptt predelay <ms>          postdelay <ms>"));
        replyOut->println(F("\n-- multi-role --"));
        replyOut->println(F("  role show                  show current role and GPS source"));
        replyOut->println(F("  role set <tracker|igate|digipeater>"));
        replyOut->println(F("  role gps <internal|fixed|none>"));
        replyOut->println(F("  fixed latitude <dd.dddddd>  longitude <dd.dddddd>  elevation <m>"));
        replyOut->println(F("  wifista on|off"));
        replyOut->println(F("  wifista add <ssid...>        (appends a network, max 5; set password next)"));
        replyOut->println(F("  wifista remove <index>       ssid <index> <ssid...>  password <index> <text>"));
        replyOut->println(F("  wifista list                 (show all configured networks, masked passwords)"));
        replyOut->println(F("  wifista scan                 (2-4s blocking scan; lists nearby SSIDs)"));
        replyOut->println(F("  wifista status               (prints wifiSTA.connected=true/false, +ip= ssid= when connected)"));
        replyOut->println(F("  aprsiss server <host>       port <n>               passcode [<n>] (blank = auto)"));
        replyOut->println(F("  aprsiss filter <filter>     aprsiss status  (+host= port= login= server= when connected)"));
        replyOut->println(F("  aprsiss downlink on|off     (gate IS messages to a locally-heard station back to RF)"));
        replyOut->println(F("  tcpkiss port <n>             (TCP KISS port, default 8001; server auto-starts with WiFi STA)"));
        replyOut->println(F("  remotecfg show|on|off        remotecfg token <secret>   remotecfg window <60-3600 sec>"));
        replyOut->println(F("                               (compact remote read/write over APRS messages -- see SERIAL_SETUP.md)"));
        replyOut->println(F("\n-- transmit now (no timer reset) --"));
        replyOut->println(F("  tx comment                 send position+comment immediately"));
        replyOut->println(F("  tx status                  send status packet immediately"));
        replyOut->println(F("\n-- other --"));
        replyOut->println(F("  digi <off|wide1|wide1+wide2>  (works with any role)"));
        replyOut->println(F("  wifi password <text>       (AP password; AP triggers on NOCALL or USR button at boot)"));
        replyOut->println(F("  beaconpath <DIRECT|WIDE1-1|WIDE1-1,WIDE2-1|WIDE1-1,WIDE2-2>"));
        replyOut->println(F("  gps read                   print current GPS position (all sources)"));
        replyOut->println(F("  sendspeed on|off             sendalt on|off"));
        replyOut->println(F("  nonsmartrate <sec>"));
        replyOut->println(F("  commentafter <n>"));
        replyOut->println();
    }

    static void kv(const char* key, const String& value) {
        replyOut->print("  ");
        replyOut->print(key);
        replyOut->print(" = ");
        replyOut->println(value);
    }
    static void kv(const char* key, const char* value)  { kv(key, String(value)); }
    static void kv(const char* key, int value)          { kv(key, String(value)); }
    static void kv(const char* key, long value)         { kv(key, String(value)); }
    __attribute__((unused)) static void kv(const char* key, unsigned value) { kv(key, String(value)); }
    static void kv(const char* key, float value)        { kv(key, String(value, 2)); }
    static void kv(const char* key, bool value)         { kv(key, value ? "on" : "off"); }

    static void hdr(const char* title) {
        replyOut->println();
        replyOut->print("[");
        replyOut->print(title);
        replyOut->println("]");
    }

    static void printBeacon(int i) {
        if (i < 0 || (size_t)i >= Config.beacons.size()) return;
        const Beacon& b = Config.beacons[i];
        replyOut->println("  beacon[" + String(i) + "]:");
        kv("    callsign", b.callsign);
        kv("    symbol  ", b.symbol);
        kv("    overlay ", b.overlay);
        kv("    mic-e   ", b.micE);
        kv("    comment ", b.comment);
        kv("    status  ", b.status);
        kv("    tactical", b.tacticalCallsign);
        kv("    label   ", b.profileLabel);
        kv("    smart   ", b.smartBeaconActive);
        kv("    smartset", String((unsigned)b.smartBeaconSetting) + " (" + SMARTBEACON_Utils::profileLabel(b.smartBeaconSetting) + ")");
    }

    static void printLora(int i) {
        if (i < 0 || (size_t)i >= Config.loraTypes.size()) return;
        const LoraType& l = Config.loraTypes[i];
        const char* region = (i == 0) ? "EU" : (i == 1) ? "PL" : (i == 2) ? "UK" : (i == 3) ? "US" : "??";
        replyOut->println("  lora[" + String(i) + "] " + region + ":");
        kv("    freq ", l.frequency);
        kv("    sf   ", l.spreadingFactor);
        kv("    bw   ", l.signalBandwidth);
        kv("    cr   ", l.codingRate4);
        kv("    power", l.power);
        kv("  cad    ", Config.cadActive);   // global, not per lora[] entry
    }

    static void printSmartCustom() {
        SmartBeaconValues& s = Config.customSmartBeacon;
        replyOut->println("  customSmartBeacon (used when beacon smartset = 4):");
        kv("    slowRate   ", s.slowRate);
        kv("    slowSpeed  ", s.slowSpeed);
        kv("    fastRate   ", s.fastRate);
        kv("    fastSpeed  ", s.fastSpeed);
        kv("    turnMinDeg ", s.turnMinDeg);
        kv("    turnSlope  ", s.turnSlope);
        kv("    turnMinTime", s.turnMinTime);
        bool usedByBeacon0 = (Config.beacons.size() > 0 && Config.beacons[0].smartBeaconSetting == SMARTBEACON_CUSTOM_INDEX);
        replyOut->println(String("    used by beacon[0]: ") + (usedByBeacon0 ? "yes" : "no"));
    }

    static void printSection(const String& section) {
        if (section == "" || section == "beacons") {
            hdr("beacons");
            printBeacon(0);
        }
        if (section == "" || section == "lora") {
            hdr("lora");
            printLora(0);
        }
        if (section == "" || section == "smartcustom") {
            hdr("smartcustom");
            printSmartCustom();
        }
        if (section == "" || section == "display") {
            hdr("display");
            kv("eco    ", Config.display.ecoMode);
            kv("timeout", Config.display.timeout);
            kv("turn180", Config.display.turn180);
            kv("invert ", Config.display.invertDisplay);
            kv("led    ", Config.display.ledEnabled);
        }
        if (section == "" || section == "bt") {
            hdr("bt");
            kv("active", Config.bluetooth.active);
            kv("name  ", Config.bluetooth.deviceName);
            kv("pin on", Config.bluetooth.pinEnabled);
            kv("pin   ", BlePin::isValid(Config.bluetooth.pin)
                            ? Config.bluetooth.pin + "  (phone passkey " + BlePin::passkeyDigits(Config.bluetooth.pin) + ")"
                            : Config.bluetooth.pin + "  (INVALID - BLE stays off until fixed)");
        }
        if (section == "" || section == "bat") {
            const Battery& b = Config.battery;
            hdr("bat");
            kv("sendv  ", b.sendVoltage);
            kv("alwaysv", b.sendVoltageAlways);
            kv("sleepv ", b.sleepVoltage);
            // Live reading
            String bv = BATTERY_Utils::getBatteryInfoVoltage();
            if (bv.length() > 0 && bv.toFloat() > 1.5) {
                String pct = BATTERY_Utils::getPercentVoltageBattery(bv.toFloat());
                pct.trim();
                kv("voltage", bv + "V  (" + pct + "%)");
            } else {
                kv("voltage", "not available");
            }
        }
        if (section == "" || section == "ptt") {
            const PTT& p = Config.ptt;
            hdr("ptt");
            kv("active   ", p.active);
            kv("pin      ", p.io_pin);
            kv("reverse  ", p.reverse);
            kv("preDelay ", p.preDelay);
            kv("postDelay", p.postDelay);
        }
        if (section == "" || section == "phg") {
            const PHGConfig& pg = Config.phg;
            hdr("phg");
            kv("enabled    ", pg.enabled);
            kv("power      ", pg.power);
            kv("height     ", pg.height);
            kv("gain       ", pg.gain);
            kv("directivity", pg.directivity);
            kv("beaconRate ", pg.beaconRate);
            if (pg.enabled) {
                char phgStr[8];
                snprintf(phgStr, sizeof(phgStr), "PHG%c%c%c%c",
                    '0' + (pg.power & 0x0F), '0' + (pg.height & 0x0F),
                    '0' + (pg.gain  & 0x0F), '0' + (pg.directivity & 0x0F));
                kv("string     ", phgStr);
            }
        }
        if (section == "" || section == "wifi") {
            hdr("wifi");
            kv("password  ", maskSecret(Config.wifiAP.password));
            kv("sta.enabled", Config.wifiSTA.enabled ? "true" : "false");
            for (size_t i = 0; i < Config.wifiSTA.networks.size(); i++) {
                replyOut->println("  sta.networks[" + String(i) + "] ssid=" + Config.wifiSTA.networks[i].ssid +
                                " password=" + maskSecret(Config.wifiSTA.networks[i].password));
            }
        }
        if (section == "" || section == "other") {
            hdr("other");
            kv("commentafter       ", Config.sendCommentAfterXBeacons);
            kv("nonSmartBeaconRate ", Config.nonSmartBeaconRate);
            kv("sendSpeedCourse     ", Config.sendSpeedCourse);
            kv("sendAltitude       ", Config.sendAltitude);
            kv("digiMode           ", Config.digiMode == DIGI_OFF ? "off" :
                                       Config.digiMode == DIGI_WIDE1 ? "wide1" : "wide1+wide2");
            kv("digiActive(runtime)", digipeaterActive ? "yes" : "no");
            kv("beaconPath         ", Config.beaconPath);
        }
    }

    // ---------------- entry/exit ----------------
    static void resetPasteState();   // defined with import/export below

    // Returns the USB port to KISS mode with the logger muted, so log lines
    // can't corrupt KISS frames for a TNC client.
    static void returnUsbToKiss() {
        logger.setDebugLevel(logging::LoggerLevel::LOGGER_LEVEL_ERROR);
        serialMode    = SerialMode::KISS;
        kissSerialBuf = "";
    }

    // Ends the other transport's idle setup session so the requester can take
    // it over. Unsaved edits stay in RAM (and `dirty` stays set), so the new
    // owner is told about them on entry and can save or discard them.
    static void takeOverIdleSession() {
        resetPasteState();
        if (usbOwnsSetup()) {
            // Requester is BLE: tell whoever might still be on the USB port.
            Serial.println(F("\n[setup] session idle -- taken over by BLE; returning to KISS TNC mode."));
            returnUsbToKiss();
            usbLine = "";
        }
        #ifdef HAS_BLE_CONFIG
        if (bleInSetup) {
            BLE_Utils::configOutput().println(F("\n[setup] session idle -- taken over by USB; config session closed."));
            bleInSetup = false;
            bleLine    = "";
        }
        #endif
    }

    // Opens the setup session for the transport supplying the current input.
    // Fails (with an ERR line) while the other transport holds an active session.
    static bool enterSetup() {
        const bool otherOwns = (inputSource == InputSource::USB) ? bleOwnsSetup() : usbOwnsSetup();
        if (otherOwns) {
            if (millis() - lastSetupActivityMs < SETUP_TAKEOVER_IDLE_MS) {
                err(String("config session busy on ") + (inputSource == InputSource::USB ? "BLE" : "USB")
                    + " -- exit it there first, or retry after " + String(SETUP_TAKEOVER_IDLE_MS / 1000) + " s idle");
                return false;
            }
            takeOverIdleSession();
        }

        if (inputSource == InputSource::USB) {
            serialMode    = SerialMode::SETUP;
            kissSerialBuf = "";
        }
        #ifdef HAS_BLE_CONFIG
        else {
            bleInSetup = true;
        }
        #endif
        exitArmed           = false;
        lastSetupActivityMs = millis();
        // Logger already suppressed from KISS mode; keep it that way during setup
        printBanner();
        replyOut->println();
        replyOut->println(F(">>> SETUP MODE ACTIVE <<<"));
        if (Config.beacons.size() > 0) {
            replyOut->println("    callsign : " + Config.beacons[0].callsign);
        }
        if (Config.loraTypes.size() > 0) {
            replyOut->println("    lora     : " + String(Config.loraTypes[0].frequency) + " Hz");
        }
        if (dirty) {
            replyOut->println(F("    note     : unsaved edits from an interrupted session are still pending"));
            replyOut->println(F("               -- 'save' to keep them or 'discard' to drop them"));
        }
        return true;
    }

    static void enterLog() {
        serialMode = SerialMode::LOG;
        kissSerialBuf = "";
        logger.setDebugLevel(currentLogLevel);
        replyOut->println(F("\n[LOG] Serial log output active."));
        replyOut->println(F("[LOG] 'log <off|error|warn|info|debug>'  'exit' to return to KISS TNC"));
    }

    // Leaves setup (or USB LOG mode) for the transport supplying the current input.
    static void doExit(bool force) {
        if (sourceOwnsSetup()) {
            if (dirty && !force) {
                err("unsaved changes -- type 'save' or 'discard' to leave");
                return;
            }
            dirty = false;
            if (inputSource == InputSource::USB) {
                replyOut->println(F("\nReturning to KISS TNC mode.\n"));
            } else {
                replyOut->println(F("\nConfig session closed.\n"));
            }
        } else if (inputSource == InputSource::USB && serialMode == SerialMode::LOG) {
            replyOut->println(F("\n[LOG] Returning to KISS TNC mode."));
        }

        if (inputSource == InputSource::USB) {
            returnUsbToKiss();
        }
        #ifdef HAS_BLE_CONFIG
        else {
            bleInSetup = false;
        }
        #endif
    }

    // ---------------- per-section dispatch ----------------
    static void cmdBeacon(String* tk, int n, const String& line) {
        if (n < 2) { err("beacon: missing subcommand"); return; }
        const String& sub = tk[1];

        if (Config.beacons.empty()) { err("no beacon in config"); return; }
        Beacon& b = Config.beacons[0];

        if (sub == "callsign") {
            if (n < 3) { err("beacon callsign <CALL-SSID>"); return; }
            String c = tk[2]; c.toUpperCase(); c.trim();
            b.callsign = c;
            ok("beacon[0].callsign = " + c);
        } else if (sub == "symbol") {
            if (n < 3 || tk[2].length() != 1) { err("symbol must be 1 char"); return; }
            b.symbol = tk[2]; ok("symbol = " + tk[2]);
        } else if (sub == "overlay") {
            if (n < 3 || tk[2].length() != 1) { err("overlay must be 1 char"); return; }
            b.overlay = tk[2]; ok("overlay = " + tk[2]);
        } else if (sub == "mice") {
            if (n < 3) { err("micE <0..7>"); return; }
            b.micE = tk[2]; ok("micE = " + tk[2]);
        } else if (sub == "comment") {
            String v = restOfLine(line, 2); b.comment = v; ok("comment set (" + String(v.length()) + " chars)");
        } else if (sub == "status") {
            String v = restOfLine(line, 2); b.status = v; ok("status set (" + String(v.length()) + " chars)");
        } else if (sub == "tactical") {
            String v = restOfLine(line, 2); v.trim();
            if (v.length() > 9) v = v.substring(0, 9);
            b.tacticalCallsign = v;
            ok("tactical = '" + v + "'" + (v.length() ? " (object mode)" : " (position mode)"));
        } else if (sub == "label") {
            String v = restOfLine(line, 2); b.profileLabel = v; ok("label = " + v);
        } else if (sub == "smart") {
            if (n < 3) { err("smart on|off"); return; }
            applyBool(tk[2], b.smartBeaconActive, "smart");
        } else if (sub == "smartset") {
            if (n < 3) { err("smartset <0..4>  (0=Runner 1=Bike 2=Car 3=Jetboat 4=Custom)"); return; }
            int v = tk[2].toInt();
            if (v < 0 || v >= SMARTBEACON_PROFILE_COUNT) {
                err("smartset must be 0..4  (0=Runner 1=Bike 2=Car 3=Jetboat 4=Custom)");
                return;
            }
            b.smartBeaconSetting = (byte)v;
            ok("smartset = " + String(b.smartBeaconSetting) + " (" + SMARTBEACON_Utils::profileLabel(b.smartBeaconSetting) + ")");
        } else {
            err("unknown beacon subcommand: " + sub);
        }
    }

    static void cmdLora(String* tk, int n, const String& /*line*/) {
        if (n < 2) { err("lora: missing subcommand"); return; }
        const String& sub = tk[1];

        if (Config.loraTypes.empty()) { err("no lora type in config"); return; }
        LoraType& l = Config.loraTypes[0];

        if (sub == "freq") {
            if (n < 3) { err("freq <Hz>"); return; }
            l.frequency = tk[2].toInt();
            ok("freq = " + String(l.frequency));
        } else if (sub == "sf") {
            if (n < 3) { err("sf <7..12>"); return; }
            l.spreadingFactor = tk[2].toInt();
            ok("sf = " + String(l.spreadingFactor));
        } else if (sub == "bw") {
            if (n < 3) { err("bw <Hz>"); return; }
            l.signalBandwidth = tk[2].toInt();
            ok("bw = " + String(l.signalBandwidth));
        } else if (sub == "cr") {
            if (n < 3) { err("cr <5..8>"); return; }
            l.codingRate4 = tk[2].toInt();
            ok("cr = " + String(l.codingRate4));
        } else if (sub == "power") {
            if (n < 3) { err("power <dBm>"); return; }
            // Board-specific safety ceiling: the T-Beam 1W's HAS_1W_LORA path
            // drives an external 30 dBm PA with no headroom check from RadioLib
            // (see LoRa_Utils::applyOutputPower()), so cap CLI input here too —
            // typing a value bypasses the config UI's curated dropdown.
            #ifdef HAS_1W_LORA
            l.power = constrain(tk[2].toInt(), 2, 20);
            #else
            l.power = constrain(tk[2].toInt(), 2, 22);
            #endif
            ok("power = " + String(l.power));
        } else if (sub == "cad") {
            if (n < 3) { err("cad on|off"); return; }
            applyBool(tk[2], Config.cadActive, "cadActive");
        } else {
            err("unknown lora subcommand: " + sub);
        }
    }

    // True if s is a plain decimal integer (optional leading '-'), so that
    // typos like "1o" are rejected instead of String::toInt() reading them as 1 or 0.
    static bool isPlainInteger(const String& s) {
        unsigned start = (s.length() > 0 && s[0] == '-') ? 1 : 0;
        if (s.length() <= start) return false;
        for (unsigned i = start; i < s.length(); i++) {
            if (!isdigit((unsigned char)s[i])) return false;
        }
        return true;
    }

    static void cmdSmartcustom(String* tk, int n, const String& /*line*/) {
        if (n < 2) { err("smartcustom <show|slowrate|slowspeed|fastrate|fastspeed|turnmindeg|turnslope|turnmintime> <n>"); return; }
        const String& sub = tk[1];

        if (sub == "show") { printSmartCustom(); return; }

        const SmartBeaconField* field = SMARTBEACON_Utils::findCustomFieldByCliName(sub);
        if (field == nullptr) { err("unknown smartcustom subcommand: " + sub); return; }
        if (n < 3 || !isPlainInteger(tk[2])) { err("smartcustom " + sub + " <n>"); return; }

        // Out-of-range values are rejected rather than clamped so the caller
        // (serial_config.html, the mobile app) never shows a value the
        // firmware did not actually store.
        int v = tk[2].toInt();
        if (v < field->minValue || v > field->maxValue) {
            err("smartcustom " + sub + " must be " + String(field->minValue) + "-" + String(field->maxValue));
            return;
        }

        SmartBeaconValues& s = Config.customSmartBeacon;
        s.*(field->member) = v;
        SMARTBEACON_Utils::setCustomValues(s);
        ok("customSmartBeacon." + String(field->jsonKey) + " = " + String(v));

        // Not an error — the edit order is up to the user — but this pair
        // collapses the speed-interpolated band to a fixed fastRate.
        if (s.fastSpeed <= s.slowSpeed) {
            replyOut->println("  warning: fastSpeed (" + String(s.fastSpeed) + ") <= slowSpeed (" + String(s.slowSpeed)
                           + "); interval will be fixed at fastRate while moving");
        }
    }

    static void cmdDisplay(String* tk, int n) {
        if (n < 3) { err("display <eco|turn180|invert|led|timeout> <value>"); return; }
        const String& sub = tk[1];
        if      (sub == "eco")     applyBool(tk[2], Config.display.ecoMode,       "display.eco");
        else if (sub == "turn180") applyBool(tk[2], Config.display.turn180,       "display.turn180");
        else if (sub == "invert")  { bool v = (tk[2] == "on" || tk[2] == "1" || tk[2] == "true"); displaySetInvert(v); ok("display.invert = " + String(v ? "on" : "off")); }
        else if (sub == "led")     applyBool(tk[2], Config.display.ledEnabled,    "display.led");
        else if (sub == "timeout") { Config.display.timeout = tk[2].toInt(); ok("display.timeout = " + String(Config.display.timeout)); }
        else err("unknown display subcommand: " + sub);
    }

    // bt pin on|off        -> require / stop requiring PIN pairing
    // bt pin <4-6 digits>  -> change the PIN (on/off state unchanged)
    // Both take effect after save + reboot, which also makes every previously
    // paired phone forget-and-re-pair (see BLE_Utils bond policy).
    static void cmdBtPin(String* tk, int n) {
        if (n != 3) { err("bt pin on|off  or  bt pin <4-6 digits>"); return; }
        const String& arg = tk[2];
        if (BlePin::isValid(arg)) {
            Config.bluetooth.pin = arg;
            ok("bt.pin = " + arg + " (phone passkey " + BlePin::passkeyDigits(arg) + ")"
               + (Config.bluetooth.pinEnabled ? "" : "; note PIN is currently off")
               + ". Save + reboot, then re-pair phones.");
            return;
        }
        if (parseBoolTok(arg) < 0) { err("bt pin: expected on/off or 4-6 digits"); return; }
        if (applyBool(arg, Config.bluetooth.pinEnabled, "bt.pinEnabled")) {
            replyOut->println(F("   Save + reboot to apply; phones paired earlier must be forgotten and re-paired."));
        }
    }

    static void cmdBt(String* tk, int n, const String& line) {
        if (n < 2) { err("bt: missing subcommand"); return; }
        const String& sub = tk[1];
        if      (sub == "on" || sub == "off" || sub == "true" || sub == "false") applyBool(tk[1], Config.bluetooth.active, "bt.active");
        else if (sub == "name") { Config.bluetooth.deviceName = restOfLine(line, 2); ok("bt.name = " + Config.bluetooth.deviceName); }
        else if (sub == "pin") cmdBtPin(tk, n);
        else err("unknown bt subcommand: " + sub);
    }

    static void cmdBat(String* tk, int n) {
        if (n < 2) { err("bat <sendv|alwaysv|sleepv|read> ..."); return; }
        const String& sub = tk[1];
        Battery& b = Config.battery;
        if (sub == "read") {
            // Force a fresh ADC sample then report voltage + percent.
            BATTERY_Utils::obtainBatteryInfo();
            String bv = BATTERY_Utils::getBatteryInfoVoltage();
            if (bv.length() > 0 && bv.toFloat() > 1.5) {
                String pct = BATTERY_Utils::getPercentVoltageBattery(bv.toFloat());
                pct.trim();
                replyOut->println("bat.voltage=" + bv + " bat.percent=" + pct);
            } else {
                replyOut->println("bat.voltage=0.00 bat.percent=0");
            }
            return;
        }
        if (n < 3) { err("bat " + sub + " <value>"); return; }
        if      (sub == "sendv")   applyBool(tk[2], b.sendVoltage,       "bat.sendv");
        else if (sub == "alwaysv") applyBool(tk[2], b.sendVoltageAlways, "bat.alwaysv");
        else if (sub == "sleepv")  { b.sleepVoltage = tk[2].toFloat(); ok("bat.sleepv = " + String(b.sleepVoltage, 2)); }
        else err("unknown bat subcommand: " + sub);
    }

    static void cmdPtt(String* tk, int n) {
        if (n < 2) { err("ptt <on|off|pin|reverse|predelay|postdelay> ..."); return; }
        const String& sub = tk[1];
        PTT& p = Config.ptt;
        if (sub == "on" || sub == "off" || sub == "true" || sub == "false") {
            applyBool(tk[1], p.active, "ptt.active"); return;
        }
        if (n < 3) { err("ptt " + sub + " <value>"); return; }
        if      (sub == "pin")       { p.io_pin    = tk[2].toInt(); ok("ptt.pin = " + String(p.io_pin)); }
        else if (sub == "reverse")   applyBool(tk[2], p.reverse, "ptt.reverse");
        else if (sub == "predelay")  { p.preDelay  = tk[2].toInt(); ok("ptt.predelay = " + String(p.preDelay)); }
        else if (sub == "postdelay") { p.postDelay = tk[2].toInt(); ok("ptt.postdelay = " + String(p.postDelay)); }
        else err("unknown ptt subcommand: " + sub);
    }

    static void cmdPhg(String* tk, int n) {
        if (n < 2) { err("phg <show|on|off|power|height|gain|dir|rate> ..."); return; }
        const String& sub = tk[1];
        PHGConfig& pg = Config.phg;
        if (sub == "show") {
            char phgStr[8];
            snprintf(phgStr, sizeof(phgStr), "PHG%c%c%c%c",
                '0' + (pg.power & 0x0F), '0' + (pg.height & 0x0F),
                '0' + (pg.gain  & 0x0F), '0' + (pg.directivity & 0x0F));
            replyOut->println("phg.enabled=" + String(pg.enabled ? "true" : "false") +
                " power=" + pg.power + " height=" + pg.height +
                " gain=" + pg.gain + " dir=" + pg.directivity +
                " rate=" + pg.beaconRate + "min  string=" + phgStr);
            return;
        }
        if (sub == "on" || sub == "off") { applyBool(tk[1], pg.enabled, "phg.enabled"); return; }
        if (n < 3) { err("phg " + sub + " <value>"); return; }
        int v = tk[2].toInt();
        if      (sub == "power")  { pg.power       = constrain(v, 0, 9); ok("phg.power = " + String(pg.power)); }
        else if (sub == "height") { pg.height      = constrain(v, 0, 9); ok("phg.height = " + String(pg.height)); }
        else if (sub == "gain")   { pg.gain        = constrain(v, 0, 9); ok("phg.gain = " + String(pg.gain)); }
        else if (sub == "dir")    { pg.directivity = constrain(v, 0, 9); ok("phg.dir = " + String(pg.directivity)); }
        else if (sub == "rate")   { pg.beaconRate  = max(1, v);           ok("phg.rate = " + String(pg.beaconRate) + " min"); }
        else err("unknown phg subcommand: " + sub);
    }

    static void cmdWifi(String* tk, int n, const String& line) {
        if (n < 2) { err("wifi <password> ..."); return; }
        const String& sub = tk[1];
        if (sub == "password") {
            Config.wifiAP.password = restOfLine(line, 2);
            ok("wifi.password updated");
            return;
        }
        err("unknown wifi subcommand: " + sub);
    }

    static void cmdLog(String* tk, int n) {
        if (n < 2) { err("log <off|error|warn|info|debug>"); return; }
        String lv = tk[1]; lv.toLowerCase();
        logging::LoggerLevel target = currentLogLevel;
        if      (lv == "off"   || lv == "error") target = logging::LoggerLevel::LOGGER_LEVEL_ERROR;
        else if (lv == "warn")                   target = logging::LoggerLevel::LOGGER_LEVEL_WARN;
        else if (lv == "info")                   target = logging::LoggerLevel::LOGGER_LEVEL_INFO;
        else if (lv == "debug")                  target = logging::LoggerLevel::LOGGER_LEVEL_DEBUG;
        else { err("unknown log level: " + lv); return; }
        currentLogLevel = target;
        // Apply immediately in log mode; stored for next log-mode entry otherwise
        if (serialMode == SerialMode::LOG) {
            logger.setDebugLevel(target);
        }
        okClean("log level = " + lv);
    }

    // ---------------- import / export ----------------
    static void resetPasteState() {
        pasting         = false;
        pasteBuf        = "";
        pasteBraceDepth = 0;
        pasteSawOpen    = false;
        pasteInString   = false;
        pasteEscapeNext = false;
    }

    static void exportConfig() {
        File f = SPIFFS.open("/tracker_conf.json", "r");
        replyOut->println(F("---- BEGIN tracker_conf.json ----"));
        if (!f) {
            replyOut->println(F("(no saved config -- use 'save' first)"));
        } else {
            while (f.available()) replyOut->write(f.read());
            f.close();
            replyOut->println();
        }
        replyOut->println(F("---- END tracker_conf.json ----"));
    }

    static void beginImport() {
        resetPasteState();
        pasting = true;
        pasteBuf.reserve(4096);
        replyOut->println(F("\nPaste full tracker_conf.json now."));
        replyOut->println(F("End auto-detected on balanced braces. Ctrl-C aborts."));
        replyOut->println(F("Note: existing config is overwritten and device reboots on success.\n"));
    }

    static void commitImport() {
        // pasting flag and buffer ownership: caller leaves us responsible
        // for clearing state regardless of outcome.
        JsonDocument doc;
        DeserializationError jerr = deserializeJson(doc, pasteBuf);
        if (jerr) {
            replyOut->print(F("[import] parse failed: "));
            replyOut->println(jerr.c_str());
            replyOut->println(F("[import] existing config unchanged"));
            resetPasteState();
            return;
        }

        // minimum viability: at least one beacon with a non-empty callsign
        JsonArrayConst beaconsArr = doc["beacons"];
        if (beaconsArr.size() == 0) {
            replyOut->println(F("[import] rejected: no beacons[] array"));
            resetPasteState();
            return;
        }
        const char* cs0 = beaconsArr[0]["callsign"] | "";
        if (cs0[0] == '\0') {
            replyOut->println(F("[import] rejected: beacons[0].callsign is empty"));
            resetPasteState();
            return;
        }

        #ifdef ARDUINO_ARCH_NRF52
            SPIFFS.remove("/tracker_conf.json");
        #endif
        File f = SPIFFS.open("/tracker_conf.json", "w");
        if (!f) {
            replyOut->println(F("[import] failed to open file for write"));
            resetPasteState();
            return;
        }
        size_t written = serializeJson(doc, f);
        f.close();
        if (written == 0) {
            replyOut->println(F("[import] write failed (0 bytes)"));
            resetPasteState();
            return;
        }
        resetPasteState();

        replyOut->print(F("[import] config written ("));
        replyOut->print((unsigned)written);
        replyOut->println(F(" bytes). Rebooting to apply..."));
        flushRepliesBeforeRestart(); delay(300);
        #ifdef ARDUINO_ARCH_NRF52
            NVIC_SystemReset();
        #else
            ESP.restart();
        #endif
    }

    // top-of-loop hook: route a single byte into paste-mode buffer when active.
    // returns true if the byte was consumed by paste-mode (caller should skip
    // the normal line-mode handling for this byte).
    static bool feedPasteByte(char c) {
        if (!pasting) return false;

        if (c == 0x03) { // Ctrl-C
            resetPasteState();
            replyOut->println(F("\r\n[import] aborted"));
            return true;
        }
        if (pasteBuf.length() >= PASTE_MAX_BYTES) {
            resetPasteState();
            replyOut->print(F("\r\n[import] buffer overflow (>"));
            replyOut->print((unsigned)PASTE_MAX_BYTES);
            replyOut->println(F(" bytes) -- aborted"));
            return true;
        }

        pasteBuf += c;
        // Local echo so a paste is visible in a USB terminal. Not over BLE:
        // it would double the traffic of an import on a ~20-byte-per-packet
        // link, and BLE clients (the app, nRF Connect) show what they sent.
        if (inputSource == InputSource::USB) replyOut->write(c);

        // brace tracker, string-aware
        if (pasteEscapeNext) {
            pasteEscapeNext = false;
        } else if (pasteInString) {
            if      (c == '\\') pasteEscapeNext = true;
            else if (c == '"')  pasteInString = false;
        } else {
            if      (c == '"') pasteInString = true;
            else if (c == '{') { pasteBraceDepth++; pasteSawOpen = true; }
            else if (c == '}') {
                if (pasteBraceDepth > 0) pasteBraceDepth--;
                if (pasteBraceDepth == 0 && pasteSawOpen) {
                    replyOut->println(F("\r\n[import] braces balanced, parsing..."));
                    commitImport();
                }
            }
        }
        return true;
    }

    // ---------------- multi-role CLI commands ----------------

    static void cmdRole(String* tk, int n, const String& /*line*/) {
        if (n < 2) { err("role <show|set|gps> ..."); return; }
        const String& sub = tk[1];

        if (sub == "show") {
            hdr("device role & gps source");
            const char* roleStr =
                (Config.deviceRole == ROLE_TRACKER)    ? "Tracker"    :
                (Config.deviceRole == ROLE_IGATE)      ? "iGate"      :
                (Config.deviceRole == ROLE_DIGIPEATER) ? "Digipeater" : "Unknown";
            const char* gpsStr =
                (Config.gpsSource == GPS_INTERNAL) ? "Internal" :
                (Config.gpsSource == GPS_FIXED)    ? "Fixed"    :
                (Config.gpsSource == GPS_NONE)     ? "None"     : "Unknown";
            kv("role", roleStr);
            kv("gps",  gpsStr);
        } else if (sub == "set") {
            if (n < 3) { err("role set <tracker|igate|digipeater>"); return; }
            String r = tk[2]; r.toLowerCase();
            DeviceRole newRole;
            if      (r == "tracker")     newRole = ROLE_TRACKER;
            else if (r == "igate")       newRole = ROLE_IGATE;
            else if (r == "digipeater")  newRole = ROLE_DIGIPEATER;
            else { err("role must be: tracker, igate, digipeater"); return; }
            #ifdef ARDUINO_ARCH_NRF52
                if (newRole == ROLE_IGATE) {
                    err("iGate not supported on nRF52 (no WiFi)");
                    return;
                }
            #endif
            Config.deviceRole = newRole;
            ok("deviceRole = " + r + " (save + reboot to apply)");
        } else if (sub == "gps") {
            if (n < 3) { err("role gps <internal|fixed|none>"); return; }
            String g = tk[2]; g.toLowerCase();
            GPSSource newGps;
            if      (g == "internal")     newGps = GPS_INTERNAL;
            else if (g == "fixed")        newGps = GPS_FIXED;
            else if (g == "none")         newGps = GPS_NONE;
            else { err("gps: internal, fixed, none"); return; }
            Config.gpsSource = newGps;
            ok("gpsSource = " + g + " (save + reboot to apply)");
        } else {
            err("unknown role subcommand: " + sub);
        }
    }

    static void cmdFixed(String* tk, int n) {
        if (n < 3) { err("fixed <latitude|longitude|elevation> <value>"); return; }
        const String& sub = tk[1];
        if      (sub == "latitude")  { Config.fixedPosition.latitude  = tk[2].toFloat(); ok("fixedPosition.latitude = "  + String(Config.fixedPosition.latitude,  6)); }
        else if (sub == "longitude") { Config.fixedPosition.longitude = tk[2].toFloat(); ok("fixedPosition.longitude = " + String(Config.fixedPosition.longitude, 6)); }
        else if (sub == "elevation") { Config.fixedPosition.elevation = tk[2].toFloat(); ok("fixedPosition.elevation = " + String(Config.fixedPosition.elevation, 1)); }
        else err("fixed: latitude, longitude, elevation");
    }

    static void cmdWifiSta(String* tk, int n, const String& line) {
        if (n < 2) { err("wifista <on|off|add|remove|ssid|password|list|scan|status>"); return; }
        const String& sub = tk[1];
        if (sub == "on" || sub == "off" || sub == "true" || sub == "false") {
            applyBool(tk[1], Config.wifiSTA.enabled, "wifiSTA.enabled");
        } else if (sub == "add") {
            if (n < 3) { err("wifista add <ssid...>"); return; }
            if (Config.wifiSTA.networks.size() >= MAX_WIFI_NETWORKS) {
                err("wifista: max " + String(MAX_WIFI_NETWORKS) + " networks");
                return;
            }
            WiFiNetwork net;
            net.ssid = restOfLine(line, 2);
            net.ssid.trim();
            if (net.ssid.length() == 0) { err("wifista add: ssid required"); return; }
            Config.wifiSTA.networks.push_back(net);
            ok("wifiSTA.networks[" + String(Config.wifiSTA.networks.size() - 1) + "].ssid = " + net.ssid +
               "  (set password next: wifista password " + String(Config.wifiSTA.networks.size() - 1) + " <text>)");
        } else if (sub == "remove") {
            if (n < 3) { err("wifista remove <index>"); return; }
            int idx = tk[2].toInt();
            if (idx < 0 || (size_t)idx >= Config.wifiSTA.networks.size()) { err("wifista remove: index out of range"); return; }
            Config.wifiSTA.networks.erase(Config.wifiSTA.networks.begin() + idx);
            ok("wifiSTA.networks[" + String(idx) + "] removed");
        } else if (sub == "ssid") {
            if (n < 4) { err("wifista ssid <index> <ssid...>"); return; }
            int idx = tk[2].toInt();
            if (idx < 0 || (size_t)idx >= Config.wifiSTA.networks.size()) { err("wifista ssid: index out of range"); return; }
            Config.wifiSTA.networks[idx].ssid = restOfLine(line, 3);
            ok("wifiSTA.networks[" + String(idx) + "].ssid = " + Config.wifiSTA.networks[idx].ssid);
        } else if (sub == "password") {
            if (n < 4) { err("wifista password <index> <text>"); return; }
            int idx = tk[2].toInt();
            if (idx < 0 || (size_t)idx >= Config.wifiSTA.networks.size()) { err("wifista password: index out of range"); return; }
            Config.wifiSTA.networks[idx].password = restOfLine(line, 3);
            ok("wifiSTA.networks[" + String(idx) + "].password updated");
        } else if (sub == "list") {
            replyOut->println("wifiSTA.enabled=" + String(Config.wifiSTA.enabled ? "true" : "false"));
            if (Config.wifiSTA.networks.empty()) {
                replyOut->println("wifiSTA.networks: (none)");
            } else {
                for (size_t i = 0; i < Config.wifiSTA.networks.size(); i++) {
                    replyOut->println("wifiSTA.networks[" + String(i) + "]: ssid=" + Config.wifiSTA.networks[i].ssid +
                                    " password=" + maskSecret(Config.wifiSTA.networks[i].password));
                }
            }
        } else if (sub == "scan") {
            #ifdef HAS_WIFI
                // BLOCKING: scanNetworks() stalls the entire main loop for
                // ~2-4s (beacon TX, GPS, LoRa RX/TX, TCP KISS all pause) since
                // SERIAL_Setup::loop() runs there. One-off, user-initiated,
                // same order of magnitude as the existing blocking
                // connectSTA() at boot (up to 10s).
                std::vector<WIFI_Utils::ScanResult> results;
                int found = WIFI_Utils::scanNetworks(results, 20);
                replyOut->println("wifiSTA.scan.count=" + String(found));
                for (auto& r : results) {
                    // key=value tokens, ssid= last (unbounded) so SSIDs
                    // containing spaces aren't truncated by the parser.
                    replyOut->println("wifiSTA.scan: rssi=" + String(r.rssi) +
                                    " secure=" + String(r.secure ? "1" : "0") +
                                    " ssid=" + r.ssid);
                }
            #else
                replyOut->println("wifiSTA.scan.count=0");
            #endif
        } else if (sub == "status") {
            #ifdef HAS_WIFI
                bool connected = WIFI_Utils::isSTAConnected();
                String line = connected ? "wifiSTA.connected=true" : "wifiSTA.connected=false";
                if (connected) {
                    line += " ip=" + WiFi.localIP().toString();
                    // ssid= last (unbounded) so SSIDs containing spaces survive parsing.
                    line += " ssid=" + WiFi.SSID();
                }
                replyOut->println(line);
            #else
                replyOut->println("wifiSTA.connected=false");
            #endif
        } else {
            err("unknown wifista subcommand: " + sub);
        }
    }

    static void cmdAprsIS(String* tk, int n, const String& line) {
        if (n < 2) { err("aprsiss <server|port|passcode|filter|downlink|status>"); return; }
        const String& sub = tk[1];
        if      (sub == "server")   { if (n < 3) { err("aprsiss server <hostname>"); return; } Config.aprsIS.server  = tk[2];                ok("aprsIS.server = "  + Config.aprsIS.server); }
        else if (sub == "port")     { if (n < 3) { err("aprsiss port <port>");    return; } Config.aprsIS.port    = tk[2].toInt();          ok("aprsIS.port = "    + String(Config.aprsIS.port)); }
        // Passcode and filter may be cleared by giving no value: an empty passcode
        // means "auto-compute from the callsign", an empty filter means "no filter".
        else if (sub == "passcode") {
            if (n < 3) { Config.aprsIS.passcode = ""; ok("aprsIS.passcode cleared (auto from callsign)"); }
            else       { Config.aprsIS.passcode = tk[2]; ok("aprsIS.passcode updated"); }
        }
        else if (sub == "filter") {
            Config.aprsIS.filter = restOfLine(line, 2);
            ok(Config.aprsIS.filter.length() ? "aprsIS.filter = " + Config.aprsIS.filter : String("aprsIS.filter cleared (no filter)"));
        }
        else if (sub == "downlink") { if (n < 3) { err("aprsiss downlink on|off"); return; } applyBool(tk[2], Config.aprsIS.downlinkEnabled, "aprsIS.downlinkEnabled"); }
        else if (sub == "status")   {
            #ifdef HAS_WIFI
                if (APRS_IS_Utils::isConnected()) {
                    // host:port as dialed (often a rotate pool); server= is the pool
                    // member the login landed on, present once its logresp arrived.
                    String line = "aprsIS.connected=true host=" + APRS_IS_Utils::getConnectedHost()
                                + " port=" + String(APRS_IS_Utils::getConnectedPort())
                                + " login=" + APRS_IS_Utils::getLoginState();
                    const String serverName = APRS_IS_Utils::getServerName();
                    if (serverName.length() > 0) line += " server=" + serverName;
                    replyOut->println(line);
                } else {
                    replyOut->println("aprsIS.connected=false");
                }
            #else
                replyOut->println("aprsIS.connected=false");
            #endif
        }
        else err("unknown aprsiss subcommand: " + sub);
    }

    static void cmdRemoteCfg(String* tk, int n, const String& line) {
        if (n < 2) { err("remotecfg <show|on|off|token|window>"); return; }
        const String& sub = tk[1];
        if (sub == "show") {
            hdr("remote config (CourseSentry)");
            kv("enabled        ", Config.remoteCfg.enabled);
            kv("token          ", maskSecret(Config.remoteCfg.token));
            kv("unlockWindowSec", Config.remoteCfg.unlockWindowSec);
        } else if (sub == "on" || sub == "off") {
            Config.remoteCfg.enabled = (sub == "on");
            ok("remoteCfg.enabled = " + String(Config.remoteCfg.enabled ? "on" : "off"));
        } else if (sub == "token") {
            if (n < 3) { err("remotecfg token <value>  (empty value clears it, disabling Stage 2 writes)"); return; }
            String v = restOfLine(line, 2);
            Config.remoteCfg.token = v;
            ok("remoteCfg.token updated");
        } else if (sub == "window") {
            if (n < 3) { err("remotecfg window <seconds>"); return; }
            int secs = tk[2].toInt();
            if (secs < 60 || secs > 3600) { err("remotecfg window must be 60-3600 seconds"); return; }
            Config.remoteCfg.unlockWindowSec = secs;
            ok("remoteCfg.unlockWindowSec = " + String(Config.remoteCfg.unlockWindowSec));
        } else {
            err("unknown remotecfg subcommand: " + sub);
        }
    }

    static void cmdTcpKiss(String* tk, int n) {
        if (n < 2) { err("tcpkiss port <n>"); return; }
        const String& sub = tk[1];
        if (sub == "port") {
            if (n < 3) { err("tcpkiss port <port>"); return; }
            Config.tcpKISS.port = tk[2].toInt();
            ok("tcpKISS.port = " + String(Config.tcpKISS.port));
        } else {
            err("unknown tcpkiss subcommand: " + sub);
        }
    }

    // ---------------- top-level dispatch ----------------
    static void handleLine(const String& line) {
        String tk[8];
        int n = splitTokens(line, tk, 8);
        if (n == 0) return;
        const String& cmd = tk[0];

        if      (cmd == "help" || cmd == "?")       printHelp();
        else if (cmd == "show") {
            if (n >= 2 && tk[1] == "secrets") {
                showSecrets = !showSecrets;
                okClean(String("show secrets = ") + (showSecrets ? "on" : "off"));
            } else {
                printSection(n >= 2 ? tk[1] : String(""));
            }
        }
        else if (cmd == "save") {
            if (Config.writeFile()) { dirty = false; okClean("config saved"); }
            else                    { err("save failed"); }
        }
        else if (cmd == "export") exportConfig();
        else if (cmd == "import") {
            if (dirty) { err("unsaved edits would be lost -- 'save' or 'discard' first"); return; }
            beginImport();
        }
        else if (cmd == "discard") {
            if (!dirty) { okClean("nothing to discard"); doExit(true); return; }
            replyOut->println(F("Discarding unsaved changes -- rebooting to reload config..."));
            flushRepliesBeforeRestart(); delay(200);
            #ifdef ARDUINO_ARCH_NRF52
                NVIC_SystemReset();
            #else
                ESP.restart();
            #endif
        }
        else if (cmd == "exit" || cmd == "quit")    doExit(false);
        else if (cmd == "reboot")                   {
            replyOut->println(F("Rebooting..."));
            flushRepliesBeforeRestart(); delay(200);
            #ifdef ARDUINO_ARCH_NRF52
                NVIC_SystemReset();
            #else
                ESP.restart();
            #endif
        }
        else if (cmd == "format") {
            // Wipe the on-device filesystem partition (LittleFS on nRF52,
            // SPIFFS on ESP32). After reset the next boot's first-boot writer
            // re-creates tracker_conf.json from embedded defaults.
            // Requires an explicit hard-to-mistype confirmation token.
            if (n < 2 || String(tk[1]) != "YES-ERASE-ALL") {
                replyOut->println(F("WARNING: 'format' erases the on-device filesystem (config,"));
                replyOut->println(F("saved messages, indices). On reboot the firmware re-creates"));
                replyOut->println(F("config from embedded defaults — your callsign etc. resets."));
                replyOut->println(F("To proceed:  format YES-ERASE-ALL"));
            } else {
                replyOut->println(F("Formatting partition..."));
                delay(200);
                #ifdef ARDUINO_ARCH_NRF52
                    InternalFS.format();
                #else
                    SPIFFS.format();
                #endif
                replyOut->println(F("Format done. Rebooting..."));
                flushRepliesBeforeRestart(); delay(500);
                #ifdef ARDUINO_ARCH_NRF52
                    NVIC_SystemReset();
                #else
                    ESP.restart();
                #endif
            }
        }
        else if (cmd == "log")                      cmdLog(tk, n);
        else if (cmd == "beacon")                   cmdBeacon(tk, n, line);
        else if (cmd == "lora")                     cmdLora(tk, n, line);
        else if (cmd == "smartcustom")              cmdSmartcustom(tk, n, line);
        else if (cmd == "display")                  cmdDisplay(tk, n);
        else if (cmd == "bt")                       cmdBt(tk, n, line);
        else if (cmd == "bat")                      cmdBat(tk, n);
        else if (cmd == "ptt")                      cmdPtt(tk, n);
        else if (cmd == "phg")                      cmdPhg(tk, n);
        else if (cmd == "wifi")                     cmdWifi(tk, n, line);
        else if (cmd == "digi") {
            if (n < 2) { err("digi <off|wide1|wide1+wide2>"); return; }
            String m = tk[1]; m.toLowerCase();
            if      (m == "off")          { Config.digiMode = DIGI_OFF;         digipeaterActive = false; ok("digiMode = off"); }
            else if (m == "wide1")        { Config.digiMode = DIGI_WIDE1;       digipeaterActive = true;  ok("digiMode = wide1 (WIDE1-1 fill-in)"); }
            else if (m == "wide1+wide2")  { Config.digiMode = DIGI_WIDE1_WIDE2; digipeaterActive = true;  ok("digiMode = wide1+wide2 (infrastructure)"); }
            else { err("digi: off, wide1, or wide1+wide2"); }
        }
        else if (cmd == "beaconpath")               { Config.beaconPath = restOfLine(line, 1); ok("beaconPath = " + Config.beaconPath); }
        else if (cmd == "gps") {
            if (n >= 2 && tk[1] == "read") {
                replyOut->println(GPS_Utils::getStatusString());
            } else {
                err("gps read");
            }
        }
        else if (cmd == "sendspeed")                { if (n >= 2) applyBool(tk[1], Config.sendSpeedCourse, "sendSpeedCourse"); else err("sendspeed on|off"); }
        else if (cmd == "sendalt")                  { if (n >= 2) applyBool(tk[1], Config.sendAltitude, "sendAltitude"); else err("sendalt on|off"); }
        // Alias of `lora cad` (v1.4.1 and the config tools send this form).
        else if (cmd == "cad")                      { if (n >= 2) applyBool(tk[1], Config.cadActive, "cadActive"); else err("cad on|off"); }
        else if (cmd == "nonsmartrate")             { if (n >= 2) { Config.nonSmartBeaconRate = tk[1].toInt(); ok("nonSmartBeaconRate = " + String(Config.nonSmartBeaconRate)); } else err("nonsmartrate <sec>"); }
        else if (cmd == "commentafter")             { if (n >= 2) { Config.sendCommentAfterXBeacons = tk[1].toInt(); ok("sendCommentAfterXBeacons = " + String(Config.sendCommentAfterXBeacons)); } else err("commentafter <n>"); }
        else if (cmd == "tx") {
            if (n < 2) { err("tx comment|status"); return; }
            String sub = tk[1]; sub.toLowerCase();
            if (sub == "comment") {
                STATION_Utils::sendCommentBeaconNow();
                replyOut->println(F("OK tx comment beacon sent (timer unchanged)"));
            } else if (sub == "status") {
                STATION_Utils::sendStatusBeaconNow();
                replyOut->println(F("OK tx status beacon sent (timer unchanged)"));
            } else {
                err("tx comment|status");
            }
        }
        // multi-role commands
        else if (cmd == "role")     cmdRole(tk, n, line);
        else if (cmd == "fixed")    cmdFixed(tk, n);
        else if (cmd == "wifista")  cmdWifiSta(tk, n, line);
        else if (cmd == "aprsiss")  cmdAprsIS(tk, n, line);
        else if (cmd == "tcpkiss")  cmdTcpKiss(tk, n);
        else if (cmd == "remotecfg") cmdRemoteCfg(tk, n, line);
        else if (cmd == "version") {
            replyOut->println("version.date=" FIRMWARE_VERSION_DATE);
            replyOut->println("version.board=" BOARD_ENV_ID);
        }
        #ifdef ARDUINO_ARCH_NRF52
        else if (cmd == "otadfu") {
            // Reset into Nordic OTA DFU bootloader mode; flash via nRF Connect app.
            replyOut->println(F("Entering OTA DFU mode — use Nordic nRF Connect app to upload firmware.zip"));
            flushRepliesBeforeRestart(); delay(200);
            enterOTADfu();
        }
        #endif
        else err("unknown command: " + cmd + "  (try 'help')");
    }

    // ---------------- public ----------------
    void setup() {
        // Serial KISS is the default mode.
        // Suppress logger so output doesn't corrupt KISS frames on the USB port.
        savedLogLevel = currentLogLevel;
        logger.setDebugLevel(logging::LoggerLevel::LOGGER_LEVEL_ERROR);
        replyOut->println(F("\n[KISS TNC] Type 'setup' to configure, 'log' to monitor serial output."));
    }

    bool isActive()   { return usbOwnsSetup() || bleOwnsSetup(); }
    bool isKISSMode() { return serialMode == SerialMode::KISS; }

    // ---------------- input handling ----------------

    // One byte for the transport that holds the setup session (USB or BLE).
    // `line` is that transport's own line buffer.
    static void feedSetupByte(char c, String& line) {
        lastSetupActivityMs = millis();

        // paste-import mode swallows everything until balanced or aborted
        if (feedPasteByte(c)) {
            if (!pasting) prompt();
            return;
        }
        if (c == 0x03) {                       // Ctrl-C clears the typed line
            line = "";
            replyOut->println(F("^C"));
            prompt();
            return;
        }
        if (c == '\r' || c == '\n') {
            replyOut->print(F("\r\n"));
            // Take the line out of the buffer before dispatching, so a command
            // that ends the session (exit/discard) can't race its own buffer.
            String command = line;
            line = "";
            command.trim();
            if (command.length() == 0) { prompt(); return; }
            handleLine(command);
            if (sourceOwnsSetup() && !pasting) prompt();
        } else if (c == 0x08 || c == 0x7F) {
            if (line.length()) {
                line.remove(line.length() - 1);
                replyOut->print(F("\b \b"));
            }
        } else if ((uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) {
            if (line.length() < 200) {
                line += c;
                replyOut->write(c);
            }
        }
    }

    // One byte from USB serial. Returns false to stop reading for this pass.
    static bool feedUsbByte(char c) {
        // ── KISS mode (default) ─────────────────────────────────────────────
        // Route binary KISS frames to LoRa. Frame bytes never take part in
        // the text handling below (they may contain 0x0D / 0x0A).
        if (serialMode == SerialMode::KISS && ((uint8_t)c == 0xC0 || kissSerialBuf.length() > 0)) {
            usbLastByteWasCR = false;
            kissSerialBuf += c;
            if ((uint8_t)c == 0xC0 && kissSerialBuf.length() > 1) {
                bool isData = false;
                String frame = KISS_Utils::decodeKISS(kissSerialBuf, isData);
                // Messages addressed to this device (e.g. a query an external
                // iGate gated down from APRS-IS) are answered locally, not TX'd.
                if (isData && frame.length() > 0 && !QUERY_Utils::processKissHostPacket(frame)) {
                    LoRa_Utils::sendNewPacket(frame);
                }
                kissSerialBuf = "";
            }
            if (kissSerialBuf.length() > 512) kissSerialBuf = "";
            return true;
        }

        if (isSecondHalfOfCRLF(c, usbLastByteWasCR)) return true;

        if (serialMode == SerialMode::KISS) {
            // Non-FEND: accumulate trigger word (no echo — keep port clean for TNC clients)
            if (c == '\r' || c == '\n') {
                String word = usbLine;
                usbLine = "";
                word.trim();
                if (word.equalsIgnoreCase("setup")) {
                    enterSetup();   // prints its own ERR line if BLE holds the session
                    prompt();
                } else if (word.equalsIgnoreCase("log")) {
                    enterLog();
                }
            } else if ((uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) {
                if (usbLine.length() < 20) usbLine += c;   // enough for "setup" / "log"
            }
            return true;
        }

        // ── SETUP mode ──────────────────────────────────────────────────────
        if (serialMode == SerialMode::SETUP) {
            feedSetupByte(c, usbLine);
            return true;
        }

        // ── LOG mode ────────────────────────────────────────────────────────
        // Log messages stream freely; user types commands interspersed.
        // Supports: 'log <level>'  and  'exit'
        if (c == '\r' || c == '\n') {
            if (usbLine.length() > 0) {
                replyOut->print(F("\r\n"));
                String command = usbLine;
                usbLine = "";
                command.trim();
                if (command.equalsIgnoreCase("exit") || command.equalsIgnoreCase("quit")) {
                    doExit(true);
                    // serialMode is now KISS; let remaining bytes be handled next call
                    return false;
                } else if (command.startsWith("log") || command.startsWith("LOG")) {
                    String tk[2];
                    int n = splitTokens(command, tk, 2);
                    cmdLog(tk, n);
                } else {
                    replyOut->println(F("[log] commands: 'log <off|error|warn|info|debug>'  'exit'"));
                }
            }
        } else if (c == 0x08 || c == 0x7F) {
            if (usbLine.length()) {
                usbLine.remove(usbLine.length() - 1);
                replyOut->print(F("\b \b"));
            }
        } else if ((uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) {
            if (usbLine.length() < 200) {
                usbLine += c;
                replyOut->write(c);
            }
        }
        return true;
    }

    #ifdef HAS_BLE_CONFIG
    // Ends the BLE setup session when the config link drops or is replaced by
    // a new connection. Unsaved edits stay in RAM with `dirty` set, so the
    // next session (USB or BLE) is told to save or discard them.
    static void syncBleLinkState() {
        const bool     linkOpen   = BLE_Utils::configLinkOpen();
        const uint32_t generation = BLE_Utils::configLinkGeneration();
        if (linkOpen && generation == bleSeenGeneration) return;   // same link, nothing to do

        if (bleInSetup) {
            resetPasteState();
            bleInSetup = false;
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BLE CFG",
                       dirty ? "config link closed, unsaved edits kept in RAM" : "config link closed");
        }
        bleLine          = "";
        bleLastByteWasCR = false;
        if (linkOpen) bleSeenGeneration = generation;
    }

    // One byte from the BLE config link.
    static void feedBleByte(char c) {
        if (isSecondHalfOfCRLF(c, bleLastByteWasCR)) return;

        if (bleInSetup) {
            feedSetupByte(c, bleLine);
            return;
        }

        // Idle: wait for 'setup' and ignore everything else silently, exactly
        // like USB KISS mode, so clients use the same discard/exit/setup entry
        // handshake on both transports.
        if (c == '\r' || c == '\n') {
            String word = bleLine;
            bleLine = "";
            word.trim();
            if (word.equalsIgnoreCase("setup")) {
                enterSetup();   // prints its own ERR line if USB holds the session
                prompt();
            } else if (word.equalsIgnoreCase("log")) {
                err("log mode is only available over USB");
            }
        } else if ((uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) {
            if (bleLine.length() < 20) bleLine += c;   // enough for "setup"
        }
    }

    static void serviceBleInput() {
        syncBleLinkState();
        if (!BLE_Utils::configLinkOpen()) return;
        ScopedInputSource scope(InputSource::BLE, BLE_Utils::configOutput());
        int ch;
        while ((ch = BLE_Utils::configRead()) >= 0) {
            feedBleByte((char)ch);
        }
    }
    #endif  // HAS_BLE_CONFIG

    void loop() {
        {
            ScopedInputSource scope(InputSource::USB, Serial);
            while (Serial.available()) {
                int ch = Serial.read();
                if (ch < 0) break;
                if (!feedUsbByte((char)ch)) break;
            }
        }
        #ifdef HAS_BLE_CONFIG
            serviceBleInput();
        #endif
    }

}
