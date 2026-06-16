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
#include "configuration.h"
#include "display.h"
#include "smartbeacon_utils.h"
#include "kiss_utils.h"
#include "lora_utils.h"
#include "battery_utils.h"
#include "gps_utils.h"
#include "version.h"
#ifdef HAS_WIFI
#include "aprs_is_utils.h"
#endif

extern Configuration        Config;
extern logging::Logger      logger;
extern bool                 digipeaterActive;


namespace SERIAL_Setup {

    // ---------------- state ----------------
    enum class SerialMode { KISS, SETUP, LOG };
    static SerialMode           serialMode      = SerialMode::KISS;
    static bool                 exitArmed       = false;
    static String               buf;
    static bool                 dirty           = false;
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

    // ---------------- helpers ----------------
    static void prompt()                    { Serial.print(F("\n> ")); }
    static void ok(const String& msg)       { Serial.println("OK: " + msg); dirty = true; exitArmed = false; }
    static void okClean(const String& msg)  { Serial.println("OK: " + msg); exitArmed = false; }
    static void err(const String& msg)      { Serial.println("ERR: " + msg); exitArmed = false; }

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
        Serial.println();
        Serial.println(F("================================================"));
        Serial.println(F(" LoRa APRS Tracker - Serial Setup"));
        Serial.println(F(" type 'help' for commands"));
        Serial.println(F(" type 'exit' to return to KISS TNC mode"));
        Serial.println(F("================================================"));
    }

    static void printHelp() {
        Serial.println(F("\n-- core --"));
        Serial.println(F("  help                       show this list"));
        Serial.println(F("  show [section]             dump config (sections: beacons|lora|"));
        Serial.println(F("                              smartcustom|display|bt|bat|ptt|wifi|other)"));
        Serial.println(F("  show secrets               toggle masked password display"));
        Serial.println(F("  save                       persist to tracker_conf.json"));
        Serial.println(F("  export                     dump current saved tracker_conf.json"));
        Serial.println(F("  import                     paste full tracker_conf.json (auto-end on"));
        Serial.println(F("                              balanced braces, Ctrl-C aborts)"));
        Serial.println(F("  discard                    leave without saving"));
        Serial.println(F("  exit                       leave (errors if dirty)"));
        Serial.println(F("  reboot                     reboot the device"));
        Serial.println(F("  format YES-ERASE-ALL       wipe LittleFS/SPIFFS, reboot to defaults"));
        #ifdef ARDUINO_ARCH_NRF52
        Serial.println(F("  otadfu                     enter BLE OTA DFU mode (nRF52 only)"));
        #endif
        Serial.println(F("  log <off|error|warn|info|debug>"));
        Serial.println(F("\n-- beacons --"));
        Serial.println(F("  beacon callsign <CALL-SSID>"));
        Serial.println(F("  beacon symbol <c>          overlay <c>          micE <0..7>"));
        Serial.println(F("  beacon comment <text...>   status <text...>     label <text...>"));
        Serial.println(F("  beacon tactical <text...>  (<=9 chars; empty = position report)"));
        Serial.println(F("  beacon smart on|off"));
        Serial.println(F("  beacon smartset <0..4>     (0=Runner 1=Bike 2=Car 3=Jetboat 4=Custom)"));
        Serial.println(F("\n-- smartcustom (used when beacon smartset = 4) --"));
        Serial.println(F("  smartcustom show"));
        Serial.println(F("  smartcustom slowrate <sec>     slowspeed <km/h>"));
        Serial.println(F("  smartcustom fastrate <sec>     fastspeed <km/h>"));
        Serial.println(F("  smartcustom turnmindeg <deg>   turnslope <n>"));
        Serial.println(F("\n-- lora --"));
        Serial.println(F("  lora freq <Hz>             sf <7..12>           bw <Hz>"));
        Serial.println(F("  lora cr <5..8>             power <dBm>"));
        Serial.println(F("\n-- peripherals --"));
        Serial.println(F("  display eco|turn180|led on|off"));
        Serial.println(F("  display timeout <sec>"));
        Serial.println(F("  bt on|off                  name <text>"));
        Serial.println(F("  bat sendv|alwaysv on|off"));
        Serial.println(F("  bat sleepv <volts>          bat read"));
        Serial.println(F("  ptt on|off                 pin <n>              reverse on|off"));
        Serial.println(F("  ptt predelay <ms>          postdelay <ms>"));
        Serial.println(F("\n-- multi-role --"));
        Serial.println(F("  role show                  show current role and GPS source"));
        Serial.println(F("  role set <tracker|igate|digipeater>"));
        Serial.println(F("  role gps <internal|fixed|none>"));
        Serial.println(F("  fixed latitude <dd.dddddd>  longitude <dd.dddddd>  elevation <m>"));
        Serial.println(F("  wifista on|off              ssid <text>            password <text>"));
        Serial.println(F("  aprsiss server <host>       port <n>               passcode <n>"));
        Serial.println(F("  aprsiss filter <filter>     aprsiss status"));
        Serial.println(F("  tcpkiss port <n>             (TCP KISS port, default 8001; server auto-starts with WiFi STA)"));
        Serial.println(F("\n-- other --"));
        Serial.println(F("  digi <off|wide1|wide1+wide2>  (works with any role)"));
        Serial.println(F("  wifi password <text>       (AP password; AP triggers on NOCALL or USR button at boot)"));
        Serial.println(F("  beaconpath <text>"));
        Serial.println(F("  gps read                   print current GPS position (all sources)"));
        Serial.println(F("  sendspeed on|off             sendalt on|off"));
        Serial.println(F("  nonsmartrate <min>"));
        Serial.println(F("  commentafter <n>"));
        Serial.println();
    }

    static void kv(const char* key, const String& value) {
        Serial.print("  ");
        Serial.print(key);
        Serial.print(" = ");
        Serial.println(value);
    }
    static void kv(const char* key, const char* value)  { kv(key, String(value)); }
    static void kv(const char* key, int value)          { kv(key, String(value)); }
    static void kv(const char* key, long value)         { kv(key, String(value)); }
    __attribute__((unused)) static void kv(const char* key, unsigned value) { kv(key, String(value)); }
    static void kv(const char* key, float value)        { kv(key, String(value, 2)); }
    static void kv(const char* key, bool value)         { kv(key, value ? "on" : "off"); }

    static void hdr(const char* title) {
        Serial.println();
        Serial.print("[");
        Serial.print(title);
        Serial.println("]");
    }

    static void printBeacon(int i) {
        if (i < 0 || (size_t)i >= Config.beacons.size()) return;
        const Beacon& b = Config.beacons[i];
        Serial.println("  beacon[" + String(i) + "]:");
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
        Serial.println("  lora[" + String(i) + "] " + region + ":");
        kv("    freq ", l.frequency);
        kv("    sf   ", l.spreadingFactor);
        kv("    bw   ", l.signalBandwidth);
        kv("    cr   ", l.codingRate4);
        kv("    power", l.power);
    }

    static void printSmartCustom() {
        SmartBeaconValues& s = Config.customSmartBeacon;
        Serial.println("  customSmartBeacon (used when beacon smartset = 4):");
        kv("    slowRate  ", s.slowRate);
        kv("    slowSpeed ", s.slowSpeed);
        kv("    fastRate  ", s.fastRate);
        kv("    fastSpeed ", s.fastSpeed);
        kv("    turnMinDeg", s.turnMinDeg);
        kv("    turnSlope ", s.turnSlope);
        bool usedByBeacon0 = (Config.beacons.size() > 0 && Config.beacons[0].smartBeaconSetting == SMARTBEACON_CUSTOM_INDEX);
        Serial.println(String("    used by beacon[0]: ") + (usedByBeacon0 ? "yes" : "no"));
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
    static void enterSetup() {
        serialMode = SerialMode::SETUP;
        exitArmed = false;
        dirty = false;
        kissSerialBuf = "";
        buf = "";
        // Logger already suppressed from KISS mode; keep it that way during setup
        printBanner();
        Serial.println();
        Serial.println(F(">>> SETUP MODE ACTIVE <<<"));
        if (Config.beacons.size() > 0) {
            Serial.println("    callsign : " + Config.beacons[0].callsign);
        }
        if (Config.loraTypes.size() > 0) {
            Serial.println("    lora     : " + String(Config.loraTypes[0].frequency) + " Hz");
        }
    }

    static void enterLog() {
        serialMode = SerialMode::LOG;
        kissSerialBuf = "";
        buf = "";
        logger.setDebugLevel(currentLogLevel);
        Serial.println(F("\n[LOG] Serial log output active."));
        Serial.println(F("[LOG] 'log <off|error|warn|info|debug>'  'exit' to return to KISS TNC"));
    }

    static void doExit(bool force) {
        if (serialMode == SerialMode::SETUP) {
            if (dirty && !force) {
                err("unsaved changes -- type 'save' or 'discard' to leave");
                return;
            }
            Serial.println(F("\nReturning to KISS TNC mode.\n"));
        } else if (serialMode == SerialMode::LOG) {
            Serial.println(F("\n[LOG] Returning to KISS TNC mode."));
        }
        // Suppress logger so output doesn't corrupt KISS frames
        logger.setDebugLevel(logging::LoggerLevel::LOGGER_LEVEL_ERROR);
        serialMode = SerialMode::KISS;
        dirty = false;
        kissSerialBuf = "";
        buf = "";
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
            l.power = tk[2].toInt();
            ok("power = " + String(l.power));
        } else {
            err("unknown lora subcommand: " + sub);
        }
    }

    static void cmdSmartcustom(String* tk, int n, const String& /*line*/) {
        if (n < 2) { err("smartcustom <show|slowrate|slowspeed|fastrate|fastspeed|turnmindeg|turnslope> <n>"); return; }
        const String& sub = tk[1];

        if (sub == "show") { printSmartCustom(); return; }

        if (n < 3) { err("smartcustom " + sub + " <n>"); return; }
        SmartBeaconValues& s = Config.customSmartBeacon;
        int v = tk[2].toInt();

        if      (sub == "slowrate")   { s.slowRate       = v; }
        else if (sub == "slowspeed")  { s.slowSpeed      = v; }
        else if (sub == "fastrate")   { s.fastRate       = v; }
        else if (sub == "fastspeed")  { s.fastSpeed      = v; }
        else if (sub == "turnmindeg") { s.turnMinDeg     = v; }
        else if (sub == "turnslope")  { s.turnSlope      = v; }
        else { err("unknown smartcustom subcommand: " + sub); return; }

        SMARTBEACON_Utils::setCustomValues(s);
        ok("customSmartBeacon." + sub + " = " + String(v));
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

    static void cmdBt(String* tk, int n, const String& line) {
        if (n < 2) { err("bt: missing subcommand"); return; }
        const String& sub = tk[1];
        if      (sub == "on" || sub == "off" || sub == "true" || sub == "false") applyBool(tk[1], Config.bluetooth.active, "bt.active");
        else if (sub == "name") { Config.bluetooth.deviceName = restOfLine(line, 2); ok("bt.name = " + Config.bluetooth.deviceName); }
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
                Serial.println("bat.voltage=" + bv + " bat.percent=" + pct);
            } else {
                Serial.println("bat.voltage=0.00 bat.percent=0");
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
            Serial.println("phg.enabled=" + String(pg.enabled ? "true" : "false") +
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
        Serial.println(F("---- BEGIN tracker_conf.json ----"));
        if (!f) {
            Serial.println(F("(no saved config -- use 'save' first)"));
        } else {
            while (f.available()) Serial.write(f.read());
            f.close();
            Serial.println();
        }
        Serial.println(F("---- END tracker_conf.json ----"));
    }

    static void beginImport() {
        resetPasteState();
        pasting = true;
        pasteBuf.reserve(4096);
        Serial.println(F("\nPaste full tracker_conf.json now."));
        Serial.println(F("End auto-detected on balanced braces. Ctrl-C aborts."));
        Serial.println(F("Note: existing config is overwritten and device reboots on success.\n"));
    }

    static void commitImport() {
        // pasting flag and buffer ownership: caller leaves us responsible
        // for clearing state regardless of outcome.
        JsonDocument doc;
        DeserializationError jerr = deserializeJson(doc, pasteBuf);
        if (jerr) {
            Serial.print(F("[import] parse failed: "));
            Serial.println(jerr.c_str());
            Serial.println(F("[import] existing config unchanged"));
            resetPasteState();
            return;
        }

        // minimum viability: at least one beacon with a non-empty callsign
        JsonArrayConst beaconsArr = doc["beacons"];
        if (beaconsArr.size() == 0) {
            Serial.println(F("[import] rejected: no beacons[] array"));
            resetPasteState();
            return;
        }
        const char* cs0 = beaconsArr[0]["callsign"] | "";
        if (cs0[0] == '\0') {
            Serial.println(F("[import] rejected: beacons[0].callsign is empty"));
            resetPasteState();
            return;
        }

        #ifdef ARDUINO_ARCH_NRF52
            SPIFFS.remove("/tracker_conf.json");
        #endif
        File f = SPIFFS.open("/tracker_conf.json", "w");
        if (!f) {
            Serial.println(F("[import] failed to open file for write"));
            resetPasteState();
            return;
        }
        size_t written = serializeJson(doc, f);
        f.close();
        if (written == 0) {
            Serial.println(F("[import] write failed (0 bytes)"));
            resetPasteState();
            return;
        }
        resetPasteState();

        Serial.print(F("[import] config written ("));
        Serial.print((unsigned)written);
        Serial.println(F(" bytes). Rebooting to apply..."));
        delay(300);
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
            Serial.println(F("\r\n[import] aborted"));
            return true;
        }
        if (pasteBuf.length() >= PASTE_MAX_BYTES) {
            resetPasteState();
            Serial.print(F("\r\n[import] buffer overflow (>"));
            Serial.print((unsigned)PASTE_MAX_BYTES);
            Serial.println(F(" bytes) -- aborted"));
            return true;
        }

        pasteBuf += c;
        Serial.write(c); // local echo so paste is visible

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
                    Serial.println(F("\r\n[import] braces balanced, parsing..."));
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
        if (n < 2) { err("wifista <on|off|ssid|password>"); return; }
        const String& sub = tk[1];
        if (sub == "on" || sub == "off" || sub == "true" || sub == "false") {
            applyBool(tk[1], Config.wifiSTA.enabled, "wifiSTA.enabled");
        } else if (sub == "ssid") {
            if (n < 3) { err("wifista ssid <ssid>"); return; }
            Config.wifiSTA.ssid = restOfLine(line, 2);
            ok("wifiSTA.ssid = " + Config.wifiSTA.ssid);
        } else if (sub == "password") {
            if (n < 3) { err("wifista password <text>"); return; }
            Config.wifiSTA.password = restOfLine(line, 2);
            ok("wifiSTA.password updated");
        } else {
            err("unknown wifista subcommand: " + sub);
        }
    }

    static void cmdAprsIS(String* tk, int n, const String& line) {
        if (n < 2) { err("aprsiss <server|port|passcode|filter|status>"); return; }
        const String& sub = tk[1];
        if      (sub == "server")   { if (n < 3) { err("aprsiss server <hostname>"); return; } Config.aprsIS.server  = tk[2];                ok("aprsIS.server = "  + Config.aprsIS.server); }
        else if (sub == "port")     { if (n < 3) { err("aprsiss port <port>");    return; } Config.aprsIS.port    = tk[2].toInt();          ok("aprsIS.port = "    + String(Config.aprsIS.port)); }
        else if (sub == "passcode") { if (n < 3) { err("aprsiss passcode <code>"); return; } Config.aprsIS.passcode = tk[2];                ok("aprsIS.passcode updated"); }
        else if (sub == "filter")   { if (n < 3) { err("aprsiss filter <filter>"); return; } Config.aprsIS.filter  = restOfLine(line, 2);   ok("aprsIS.filter = "  + Config.aprsIS.filter); }
        else if (sub == "status")   {
            #ifdef HAS_WIFI
                Serial.println(APRS_IS_Utils::isConnected() ? "aprsIS.connected=true" : "aprsIS.connected=false");
            #else
                Serial.println("aprsIS.connected=false");
            #endif
        }
        else err("unknown aprsiss subcommand: " + sub);
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
            Serial.println(F("Discarding unsaved changes -- rebooting to reload config..."));
            delay(200);
            #ifdef ARDUINO_ARCH_NRF52
                NVIC_SystemReset();
            #else
                ESP.restart();
            #endif
        }
        else if (cmd == "exit" || cmd == "quit")    doExit(false);
        else if (cmd == "reboot")                   {
            Serial.println(F("Rebooting...")); delay(200);
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
                Serial.println(F("WARNING: 'format' erases the on-device filesystem (config,"));
                Serial.println(F("saved messages, indices). On reboot the firmware re-creates"));
                Serial.println(F("config from embedded defaults — your callsign etc. resets."));
                Serial.println(F("To proceed:  format YES-ERASE-ALL"));
            } else {
                Serial.println(F("Formatting partition..."));
                delay(200);
                #ifdef ARDUINO_ARCH_NRF52
                    InternalFS.format();
                #else
                    SPIFFS.format();
                #endif
                Serial.println(F("Format done. Rebooting..."));
                delay(500);
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
                Serial.println(GPS_Utils::getStatusString());
            } else {
                err("gps read");
            }
        }
        else if (cmd == "sendspeed")                { if (n >= 2) applyBool(tk[1], Config.sendSpeedCourse, "sendSpeedCourse"); else err("sendspeed on|off"); }
        else if (cmd == "sendalt")                  { if (n >= 2) applyBool(tk[1], Config.sendAltitude, "sendAltitude"); else err("sendalt on|off"); }
        else if (cmd == "nonsmartrate")             { if (n >= 2) { Config.nonSmartBeaconRate = tk[1].toInt(); ok("nonSmartBeaconRate = " + String(Config.nonSmartBeaconRate)); } else err("nonsmartrate <min>"); }
        else if (cmd == "commentafter")             { if (n >= 2) { Config.sendCommentAfterXBeacons = tk[1].toInt(); ok("sendCommentAfterXBeacons = " + String(Config.sendCommentAfterXBeacons)); } else err("commentafter <n>"); }
        // multi-role commands
        else if (cmd == "role")     cmdRole(tk, n, line);
        else if (cmd == "fixed")    cmdFixed(tk, n);
        else if (cmd == "wifista")  cmdWifiSta(tk, n, line);
        else if (cmd == "aprsiss")  cmdAprsIS(tk, n, line);
        else if (cmd == "tcpkiss")  cmdTcpKiss(tk, n);
        else if (cmd == "version") {
            Serial.println("version.date=" FIRMWARE_VERSION_DATE);
        }
        #ifdef ARDUINO_ARCH_NRF52
        else if (cmd == "otadfu") {
            // Reset into Nordic OTA DFU bootloader mode; flash via nRF Connect app.
            Serial.println(F("Entering OTA DFU mode — use Nordic nRF Connect app to upload firmware.zip"));
            delay(200);
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
        Serial.println(F("\n[KISS TNC] Type 'setup' to configure, 'log' to monitor serial output."));
    }

    bool isActive()   { return serialMode == SerialMode::SETUP; }
    bool isKISSMode() { return serialMode == SerialMode::KISS; }

    void loop() {
        while (Serial.available()) {
            int ch = Serial.read();
            if (ch < 0) break;
            char c = (char)ch;

            // ── KISS mode (default) ─────────────────────────────────────────
            // Route binary KISS frames to LoRa.  Non-FEND printable bytes
            // accumulate in a small buffer to detect the 'setup' / 'log' triggers.
            if (serialMode == SerialMode::KISS) {
                if ((uint8_t)c == 0xC0 || kissSerialBuf.length() > 0) {
                    kissSerialBuf += c;
                    if ((uint8_t)c == 0xC0 && kissSerialBuf.length() > 1) {
                        bool isData = false;
                        String frame = KISS_Utils::decodeKISS(kissSerialBuf, isData);
                        if (isData && frame.length() > 0) {
                            LoRa_Utils::sendNewPacket(frame);
                        }
                        kissSerialBuf = "";
                    }
                    if (kissSerialBuf.length() > 512) kissSerialBuf = "";
                    continue;
                }
                // Non-FEND: accumulate trigger word (no echo — keep port clean for TNC clients)
                if (c == '\r' || c == '\n') {
                    buf.trim();
                    if (buf.equalsIgnoreCase("setup")) {
                        buf = "";
                        enterSetup();
                        prompt();
                    } else if (buf.equalsIgnoreCase("log")) {
                        buf = "";
                        enterLog();
                    }
                    buf = "";
                } else if ((uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) {
                    if (buf.length() < 20) buf += c;   // enough for "setup" / "log"
                }
                continue;
            }

            // ── SETUP mode ──────────────────────────────────────────────────
            if (serialMode == SerialMode::SETUP) {
                // paste-import mode swallows everything until balanced or aborted
                if (feedPasteByte(c)) {
                    if (!pasting) prompt();
                    continue;
                }
                if (c == 0x03) {                       // Ctrl-C clears typed buf
                    buf = "";
                    Serial.println(F("^C"));
                    prompt();
                    continue;
                }
                if (c == '\r' || c == '\n') {
                    Serial.print(F("\r\n"));
                    if (buf.length() == 0) { prompt(); continue; }
                    buf.trim();
                    handleLine(buf);
                    if (serialMode == SerialMode::SETUP && !pasting) prompt();
                    buf = "";
                } else if (c == 0x08 || c == 0x7F) {
                    if (buf.length()) {
                        buf.remove(buf.length() - 1);
                        Serial.print(F("\b \b"));
                    }
                } else if ((uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) {
                    if (buf.length() < 200) {
                        buf += c;
                        Serial.write(c);
                    }
                }
                continue;
            }

            // ── LOG mode ────────────────────────────────────────────────────
            // Log messages stream freely; user types commands interspersed.
            // Supports: 'log <level>'  and  'exit'
            if (c == '\r' || c == '\n') {
                if (buf.length() > 0) {
                    Serial.print(F("\r\n"));
                    buf.trim();
                    if (buf.equalsIgnoreCase("exit") || buf.equalsIgnoreCase("quit")) {
                        buf = "";
                        doExit(true);
                        // serialMode is now KISS; let remaining bytes be handled next call
                        break;
                    } else if (buf.startsWith("log") || buf.startsWith("LOG")) {
                        String tk[2];
                        int n = splitTokens(buf, tk, 2);
                        cmdLog(tk, n);
                    } else {
                        Serial.println(F("[log] commands: 'log <off|error|warn|info|debug>'  'exit'"));
                    }
                    buf = "";
                }
            } else if (c == 0x08 || c == 0x7F) {
                if (buf.length()) {
                    buf.remove(buf.length() - 1);
                    Serial.print(F("\b \b"));
                }
            } else if ((uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) {
                if (buf.length() < 200) {
                    buf += c;
                    Serial.write(c);
                }
            }
        }
    }

}
