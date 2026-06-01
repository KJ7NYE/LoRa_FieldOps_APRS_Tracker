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
#include "smartbeacon_utils.h"
#include "device_role.h"

extern Configuration        Config;
extern logging::Logger      logger;
extern bool                 digipeaterActive;
extern bool                 bluetoothActive;
extern bool                 displayEcoMode;


namespace SERIAL_Setup {

    // ---------------- state ----------------
    static bool                 active          = false;
    static bool                 exitArmed       = false;
    static String               buf;
    static bool                 dirty           = false;
    static int                  selBeacon       = 0;
    static int                  selLora         = 0;
    static bool                 showSecrets     = false;
    static logging::LoggerLevel savedLogLevel   = logging::LoggerLevel::LOGGER_LEVEL_INFO;
    static logging::LoggerLevel currentLogLevel = logging::LoggerLevel::LOGGER_LEVEL_INFO;

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
        Serial.println(F(" type 'help' for commands, 'exit' to leave"));
        Serial.println(F(" logger paused (ERROR only) while in setup"));
        Serial.println(F("================================================"));
    }

    static void printHelp() {
        Serial.println(F("\n-- core --"));
        Serial.println(F("  help                       show this list"));
        Serial.println(F("  show [section]             dump config (sections: beacons|lora|"));
        Serial.println(F("                              smartcustom|display|bt|notif|bat|"));
        Serial.println(F("                              telem|ptt|winlink|wifi|other)"));
        Serial.println(F("  show secrets               toggle masked password display"));
        Serial.println(F("  save                       persist to tracker_conf.json"));
        Serial.println(F("  export                     dump current saved tracker_conf.json"));
        Serial.println(F("  import                     paste full tracker_conf.json (auto-end on"));
        Serial.println(F("                              balanced braces, Ctrl-C aborts)"));
        Serial.println(F("  discard                    leave without saving"));
        Serial.println(F("  exit                       leave (errors if dirty)"));
        Serial.println(F("  reboot                     ESP.restart()"));
        Serial.println(F("  format YES-ERASE-ALL       wipe LittleFS/SPIFFS, reboot to defaults"));
        Serial.println(F("  log <off|error|warn|info|debug>"));
        Serial.println(F("\n-- beacons --"));
        Serial.println(F("  beacon list"));
        Serial.println(F("  beacon select <0..n-1>"));
        Serial.println(F("  beacon callsign <CALL-SSID>"));
        Serial.println(F("  beacon symbol <c>          overlay <c>          micE <0..7>"));
        Serial.println(F("  beacon comment <text...>   status <text...>     label <text...>"));
        Serial.println(F("  beacon tactical <text...>  (<=9 chars; empty = position report)"));
        Serial.println(F("  beacon smart on|off        gpseco on|off"));
        Serial.println(F("  beacon smartset <0..3>     (0=Runner 1=Bike 2=Car 3=Custom)"));
        Serial.println(F("\n-- smartcustom (used when beacon smartset = 3) --"));
        Serial.println(F("  smartcustom show"));
        Serial.println(F("  smartcustom slowrate <sec>     slowspeed <km/h>"));
        Serial.println(F("  smartcustom fastrate <sec>     fastspeed <km/h>"));
        Serial.println(F("  smartcustom mintxdist <m>      mindelta <sec>"));
        Serial.println(F("  smartcustom turnmindeg <deg>   turnslope <n>"));
        Serial.println(F("\n-- lora --"));
        Serial.println(F("  lora list"));
        Serial.println(F("  lora select <0..3>         (0=EU 1=PL 2=UK 3=US)"));
        Serial.println(F("  lora freq <Hz>             sf <7..12>           bw <Hz>"));
        Serial.println(F("  lora cr <5..8>             power <dBm>"));
        Serial.println(F("\n-- peripherals --"));
        Serial.println(F("  display eco|turn180|symbol on|off"));
        Serial.println(F("  display timeout <sec>"));
        Serial.println(F("  bt on|off                  name <text>"));
        Serial.println(F("  bt ble on|off              kiss on|off"));
        Serial.println(F("  notif tx|msg|flashled on|off"));
        Serial.println(F("  notif beep <boot|tx|rx|station|low|shutdown> on|off"));
        Serial.println(F("  notif buzzer on|off"));
        Serial.println(F("  bat sendv|astelem|alwaysv|monitor on|off"));
        Serial.println(F("  bat sleepv <volts>"));
        Serial.println(F("  telem on|off               send on|off          tempcorr <float>"));
        Serial.println(F("  ptt on|off                 pin <n>              reverse on|off"));
        Serial.println(F("  ptt predelay <ms>          postdelay <ms>"));
        Serial.println(F("\n-- other --"));
        Serial.println(F("  digipeater on|off          (boot default; menu still toggles runtime)"));
        Serial.println(F("  winlink password <text>"));
        Serial.println(F("  wifi on|off                password <text>"));
        Serial.println(F("  wifi window on|off         (30s AP at boot; off by default)"));
        Serial.println(F("  path <text>                email <addr>         simplified on|off"));
        Serial.println(F("  disablegps on|off          sendalt on|off"));
        Serial.println(F("  nonsmartrate <sec>         rememberstation <sec>"));
        Serial.println(F("  standingupdate <sec>       commentafter <n>"));
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
        kv("    gpsEco  ", b.gpsEcoMode);
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
        Serial.println("  customSmartBeacon (used when beacon smartset = 3):");
        kv("    slowRate      ", s.slowRate);
        kv("    slowSpeed     ", s.slowSpeed);
        kv("    fastRate      ", s.fastRate);
        kv("    fastSpeed     ", s.fastSpeed);
        kv("    minTxDist     ", s.minTxDist);
        kv("    minDeltaBeacon", s.minDeltaBeacon);
        kv("    turnMinDeg    ", s.turnMinDeg);
        kv("    turnSlope     ", s.turnSlope);
        String users = "";
        for (size_t i = 0; i < Config.beacons.size(); i++) {
            if (Config.beacons[i].smartBeaconSetting == SMARTBEACON_CUSTOM_INDEX) {
                if (users.length()) users += ",";
                users += String((unsigned)i);
            }
        }
        Serial.println("    used by beacon[s]: " + (users.length() ? users : String("(none)")));
    }

    static void printSection(const String& section) {
        if (section == "" || section == "beacons") {
            hdr("beacons");
            kv("selected", selBeacon);
            for (size_t i = 0; i < Config.beacons.size(); i++) printBeacon(i);
        }
        if (section == "" || section == "lora") {
            hdr("lora");
            kv("selected", selLora);
            for (size_t i = 0; i < Config.loraTypes.size(); i++) printLora(i);
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
            kv("symbol ", Config.display.showSymbol);
        }
        if (section == "" || section == "bt") {
            hdr("bt");
            kv("active", Config.bluetooth.active);
            kv("name  ", Config.bluetooth.deviceName);
            kv("ble   ", Config.bluetooth.useBLE);
            kv("kiss  ", Config.bluetooth.useKISS);
        }
        if (section == "" || section == "notif") {
            const Notification& n = Config.notification;
            hdr("notif");
            kv("ledTx        ", n.ledTx);
            kv("ledTxPin     ", n.ledTxPin);
            kv("ledMsg       ", n.ledMessage);
            kv("ledMsgPin    ", n.ledMessagePin);
            kv("ledFlashlight", n.ledFlashlight);
            kv("ledFlashPin  ", n.ledFlashlightPin);
            kv("buzzer       ", n.buzzerActive);
            kv("buzzerTonePin", n.buzzerPinTone);
            kv("buzzerVccPin ", n.buzzerPinVcc);
            kv("beepBoot     ", n.bootUpBeep);
            kv("beepTx       ", n.txBeep);
            kv("beepRx       ", n.messageRxBeep);
            kv("beepStation  ", n.stationBeep);
            kv("beepLow      ", n.lowBatteryBeep);
            kv("beepShutdown ", n.shutDownBeep);
        }
        if (section == "" || section == "bat") {
            const Battery& b = Config.battery;
            hdr("bat");
            kv("sendv  ", b.sendVoltage);
            kv("astelem", b.voltageAsTelemetry);
            kv("alwaysv", b.sendVoltageAlways);
            kv("monitor", b.monitorVoltage);
            kv("sleepv ", b.sleepVoltage);
        }
        if (section == "" || section == "telem") {
            hdr("telem");
            kv("active  ", Config.telemetry.active);
            kv("send    ", Config.telemetry.sendTelemetry);
            kv("tempCorr", Config.telemetry.temperatureCorrection);
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
        if (section == "" || section == "winlink") {
            hdr("winlink");
            kv("password", maskSecret(Config.winlink.password));
        }
        if (section == "" || section == "wifi") {
            hdr("wifi");
            kv("active    ", Config.wifiAP.active);
            kv("bootWindow", Config.wifiAP.bootWindow);
            kv("password  ", maskSecret(Config.wifiAP.password));
        }
        if (section == "" || section == "other") {
            hdr("other");
            kv("simplified         ", Config.simplifiedTrackerMode);
            kv("commentafter       ", Config.sendCommentAfterXBeacons);
            kv("nonSmartBeaconRate ", Config.nonSmartBeaconRate);
            kv("rememberStation    ", Config.rememberStationTime);
            kv("standingUpdateTime ", Config.standingUpdateTime);
            kv("sendAltitude       ", Config.sendAltitude);
            kv("disableGPS         ", Config.disableGPS);
            kv("digipeating(boot)  ", Config.digipeating);
            kv("digipeaterActive   ", digipeaterActive);
            kv("path               ", Config.path);
            kv("email              ", Config.email);
        }
    }

    // ---------------- entry/exit ----------------
    static void enterSetup() {
        active = true;
        exitArmed = false;
        dirty = false;
        savedLogLevel = currentLogLevel;
        logger.setDebugLevel(logging::LoggerLevel::LOGGER_LEVEL_ERROR);
        selBeacon = 0;
        selLora   = 0;
        printBanner();
        Serial.println();
        Serial.println(F(">>> SETUP MODE ACTIVE <<<"));
        if (Config.beacons.size() > 0) {
            Serial.println("    current callsign : " + Config.beacons[selBeacon].callsign);
        }
        if (Config.loraTypes.size() > 0) {
            const char* region = (selLora == 0) ? "EU" : (selLora == 1) ? "PL" : (selLora == 2) ? "UK" : "US";
            Serial.println("    current lora     : " + String(region) + " (" + String(Config.loraTypes[selLora].frequency) + " Hz)");
        }
    }

    static void doExit(bool force) {
        if (dirty && !force) {
            err("unsaved changes -- type 'save' or 'discard' to leave");
            return;
        }
        logger.setDebugLevel(savedLogLevel);
        currentLogLevel = savedLogLevel;
        active = false;
        dirty = false;
        Serial.println(F("\nSetup mode exited.\n"));
    }

    // ---------------- per-section dispatch ----------------
    static void cmdBeacon(String* tk, int n, const String& line) {
        if (n < 2) { err("beacon: missing subcommand"); return; }
        const String& sub = tk[1];

        if (sub == "list") {
            for (size_t i = 0; i < Config.beacons.size(); i++) printBeacon(i);
            return;
        }
        if (sub == "select") {
            if (n < 3) { err("beacon select <index>"); return; }
            int i = tk[2].toInt();
            if (i < 0 || (size_t)i >= Config.beacons.size()) { err("index out of range"); return; }
            selBeacon = i;
            okClean("beacon selected = " + String(i));
            return;
        }

        if (selBeacon < 0 || (size_t)selBeacon >= Config.beacons.size()) {
            err("no beacon selected");
            return;
        }
        Beacon& b = Config.beacons[selBeacon];

        if (sub == "callsign") {
            if (n < 3) { err("beacon callsign <CALL-SSID>"); return; }
            String c = tk[2]; c.toUpperCase(); c.trim();
            b.callsign = c;
            ok("beacon[" + String(selBeacon) + "].callsign = " + c);
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
            if (n < 3) { err("smartset <0..3>  (0=Runner 1=Bike 2=Car 3=Custom)"); return; }
            int v = tk[2].toInt();
            if (v < 0 || v >= SMARTBEACON_PROFILE_COUNT) {
                err("smartset must be 0..3  (0=Runner 1=Bike 2=Car 3=Custom)");
                return;
            }
            b.smartBeaconSetting = (byte)v;
            ok("smartset = " + String(b.smartBeaconSetting) + " (" + SMARTBEACON_Utils::profileLabel(b.smartBeaconSetting) + ")");
        } else if (sub == "gpseco") {
            if (n < 3) { err("gpseco on|off"); return; }
            applyBool(tk[2], b.gpsEcoMode, "gpseco");
        } else {
            err("unknown beacon subcommand: " + sub);
        }
    }

    static void cmdLora(String* tk, int n, const String& /*line*/) {
        if (n < 2) { err("lora: missing subcommand"); return; }
        const String& sub = tk[1];

        if (sub == "list") {
            for (size_t i = 0; i < Config.loraTypes.size(); i++) printLora(i);
            return;
        }
        if (sub == "select") {
            if (n < 3) { err("lora select <0..3>"); return; }
            int i = tk[2].toInt();
            if (i < 0 || (size_t)i >= Config.loraTypes.size()) { err("index out of range"); return; }
            selLora = i;
            okClean("lora selected = " + String(i));
            return;
        }

        if (selLora < 0 || (size_t)selLora >= Config.loraTypes.size()) {
            err("no lora region selected");
            return;
        }
        LoraType& l = Config.loraTypes[selLora];

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
        if (n < 2) { err("smartcustom <show|slowrate|slowspeed|fastrate|fastspeed|mintxdist|mindelta|turnmindeg|turnslope> <n>"); return; }
        const String& sub = tk[1];

        if (sub == "show") { printSmartCustom(); return; }

        if (n < 3) { err("smartcustom " + sub + " <n>"); return; }
        SmartBeaconValues& s = Config.customSmartBeacon;
        int v = tk[2].toInt();

        if      (sub == "slowrate")   { s.slowRate       = v; }
        else if (sub == "slowspeed")  { s.slowSpeed      = v; }
        else if (sub == "fastrate")   { s.fastRate       = v; }
        else if (sub == "fastspeed")  { s.fastSpeed      = v; }
        else if (sub == "mintxdist")  { s.minTxDist      = v; }
        else if (sub == "mindelta")   { s.minDeltaBeacon = v; }
        else if (sub == "turnmindeg") { s.turnMinDeg     = v; }
        else if (sub == "turnslope")  { s.turnSlope      = v; }
        else { err("unknown smartcustom subcommand: " + sub); return; }

        SMARTBEACON_Utils::setCustomValues(s);
        ok("customSmartBeacon." + sub + " = " + String(v));
    }

    static void cmdDisplay(String* tk, int n) {
        if (n < 3) { err("display <eco|turn180|symbol|timeout> <value>"); return; }
        const String& sub = tk[1];
        if      (sub == "eco")     applyBool(tk[2], Config.display.ecoMode,    "display.eco");
        else if (sub == "turn180") applyBool(tk[2], Config.display.turn180,    "display.turn180");
        else if (sub == "symbol")  applyBool(tk[2], Config.display.showSymbol, "display.symbol");
        else if (sub == "timeout") { Config.display.timeout = tk[2].toInt(); ok("display.timeout = " + String(Config.display.timeout)); }
        else err("unknown display subcommand: " + sub);
    }

    static void cmdBt(String* tk, int n, const String& line) {
        if (n < 2) { err("bt: missing subcommand"); return; }
        const String& sub = tk[1];
        if      (sub == "on" || sub == "off" || sub == "true" || sub == "false") applyBool(tk[1], Config.bluetooth.active, "bt.active");
        else if (sub == "name") { Config.bluetooth.deviceName = restOfLine(line, 2); ok("bt.name = " + Config.bluetooth.deviceName); }
        else if (sub == "ble")  { if (n < 3) { err("bt ble on|off"); return; } applyBool(tk[2], Config.bluetooth.useBLE,  "bt.ble"); }
        else if (sub == "kiss") { if (n < 3) { err("bt kiss on|off"); return; } applyBool(tk[2], Config.bluetooth.useKISS, "bt.kiss"); }
        else err("unknown bt subcommand: " + sub);
    }

    static void cmdNotif(String* tk, int n) {
        if (n < 3) { err("notif <field> <on|off> [or beep <kind> on|off]"); return; }
        const String& sub = tk[1];
        Notification& nf = Config.notification;
        if      (sub == "tx")        applyBool(tk[2], nf.ledTx,         "notif.ledTx");
        else if (sub == "msg")       applyBool(tk[2], nf.ledMessage,    "notif.ledMsg");
        else if (sub == "flashled")  applyBool(tk[2], nf.ledFlashlight, "notif.flashLed");
        else if (sub == "buzzer")    applyBool(tk[2], nf.buzzerActive,  "notif.buzzer");
        else if (sub == "beep") {
            if (n < 4) { err("notif beep <kind> on|off"); return; }
            const String& k = tk[2];
            if      (k == "boot")     applyBool(tk[3], nf.bootUpBeep,    "beep.boot");
            else if (k == "tx")       applyBool(tk[3], nf.txBeep,        "beep.tx");
            else if (k == "rx")       applyBool(tk[3], nf.messageRxBeep, "beep.rx");
            else if (k == "station")  applyBool(tk[3], nf.stationBeep,   "beep.station");
            else if (k == "low")      applyBool(tk[3], nf.lowBatteryBeep,"beep.low");
            else if (k == "shutdown") applyBool(tk[3], nf.shutDownBeep,  "beep.shutdown");
            else err("unknown beep kind: " + k);
        } else err("unknown notif subcommand: " + sub);
    }

    static void cmdBat(String* tk, int n) {
        if (n < 3) { err("bat <field> <value>"); return; }
        const String& sub = tk[1];
        Battery& b = Config.battery;
        if      (sub == "sendv")   applyBool(tk[2], b.sendVoltage,        "bat.sendv");
        else if (sub == "astelem") applyBool(tk[2], b.voltageAsTelemetry, "bat.astelem");
        else if (sub == "alwaysv") applyBool(tk[2], b.sendVoltageAlways,  "bat.alwaysv");
        else if (sub == "monitor") applyBool(tk[2], b.monitorVoltage,     "bat.monitor");
        else if (sub == "sleepv")  { b.sleepVoltage = tk[2].toFloat(); ok("bat.sleepv = " + String(b.sleepVoltage, 2)); }
        else err("unknown bat subcommand: " + sub);
    }

    static void cmdTelem(String* tk, int n) {
        if (n < 2) { err("telem <on|off|send|tempcorr> ..."); return; }
        const String& sub = tk[1];
        if (sub == "on" || sub == "off" || sub == "true" || sub == "false") {
            applyBool(tk[1], Config.telemetry.active, "telem.active"); return;
        }
        if (n < 3) { err("telem " + sub + " <value>"); return; }
        if      (sub == "send")     applyBool(tk[2], Config.telemetry.sendTelemetry, "telem.send");
        else if (sub == "tempcorr") { Config.telemetry.temperatureCorrection = tk[2].toFloat(); ok("telem.tempcorr = " + String(Config.telemetry.temperatureCorrection, 2)); }
        else err("unknown telem subcommand: " + sub);
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

    static void cmdWinlink(String* tk, int n, const String& line) {
        if (n < 3 || tk[1] != "password") { err("winlink password <text>"); return; }
        Config.winlink.password = restOfLine(line, 2);
        ok("winlink.password updated");
    }

    static void cmdWifi(String* tk, int n, const String& line) {
        if (n < 2) { err("wifi <on|off|window|password> ..."); return; }
        const String& sub = tk[1];
        if (sub == "on" || sub == "off" || sub == "true" || sub == "false") {
            applyBool(tk[1], Config.wifiAP.active, "wifi.active"); return;
        }
        if (sub == "window") {
            if (n < 3) { err("wifi window on|off"); return; }
            applyBool(tk[2], Config.wifiAP.bootWindow, "wifi.bootWindow");
            return;
        }
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
        savedLogLevel   = target;   // also update what gets restored on exit
        okClean("log level (post-exit) = " + lv);
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

    static void cmdRole(String* tk, int n, const String& /*line*/) {
        if (n < 2) { err("role <show|set|gps> ..."); return; }
        const String& sub = tk[1];

        if (sub == "show") {
            hdr("device role & gps source");
            const char* roleStr = (Config.deviceRole == ROLE_TRACKER) ? "Tracker" :
                                   (Config.deviceRole == ROLE_IGATE) ? "iGate" :
                                   (Config.deviceRole == ROLE_DIGIPEATER) ? "Digipeater" : "Unknown";
            const char* gpsStr = (Config.gpsSource == GPS_INTERNAL) ? "Internal" :
                                  (Config.gpsSource == GPS_FIXED) ? "Fixed" :
                                  (Config.gpsSource == GPS_EXTERNAL_SERIAL) ? "External Serial" :
                                  (Config.gpsSource == GPS_EXTERNAL_BLE) ? "External BLE" : "Unknown";
            kv("role", roleStr);
            kv("gps", gpsStr);
        } else if (sub == "set") {
            if (n < 3) { err("role set <tracker|igate|digipeater>"); return; }
            String r = tk[2]; r.toLowerCase();
            DeviceRole newRole;
            if (r == "tracker")         newRole = ROLE_TRACKER;
            else if (r == "igate")      newRole = ROLE_IGATE;
            else if (r == "digipeater") newRole = ROLE_DIGIPEATER;
            else { err("role must be: tracker, igate, or digipeater"); return; }
            #ifdef ARDUINO_ARCH_NRF52
                if (newRole == ROLE_IGATE) {
                    err("iGate mode not supported on nRF52 (no WiFi)");
                    return;
                }
            #endif
            Config.deviceRole = newRole;
            ok("deviceRole = " + String(DeviceRoleUtils::getRoleString(newRole)));
        } else if (sub == "gps") {
            if (n < 3) { err("role gps <internal|fixed|externserial|externble>"); return; }
            String g = tk[2]; g.toLowerCase();
            GPSSource newGps;
            if (g == "internal")     newGps = GPS_INTERNAL;
            else if (g == "fixed")   newGps = GPS_FIXED;
            else if (g == "externserial") newGps = GPS_EXTERNAL_SERIAL;
            else if (g == "externble") newGps = GPS_EXTERNAL_BLE;
            else { err("gps must be: internal, fixed, externserial, or externble"); return; }
            Config.gpsSource = newGps;
            ok("gpsSource = " + String(g));
        } else {
            err("unknown role subcommand: " + sub);
        }
    }

    static void cmdFixed(String* tk, int n) {
        if (n < 3) { err("fixed <latitude|longitude|elevation> <value>"); return; }
        const String& sub = tk[1];
        if (sub == "latitude") {
            Config.fixedPosition.latitude = tk[2].toFloat();
            ok("fixedPosition.latitude = " + String(Config.fixedPosition.latitude, 6));
        } else if (sub == "longitude") {
            Config.fixedPosition.longitude = tk[2].toFloat();
            ok("fixedPosition.longitude = " + String(Config.fixedPosition.longitude, 6));
        } else if (sub == "elevation") {
            Config.fixedPosition.elevation = tk[2].toFloat();
            ok("fixedPosition.elevation = " + String(Config.fixedPosition.elevation, 1));
        } else {
            err("unknown fixed subcommand: " + sub);
        }
    }

    static void cmdWifiSta(String* tk, int n, const String& line) {
        if (n < 2) { err("wifista <on|off|ssid|password> ..."); return; }
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
        if (n < 2) { err("aprsiss <server|port|passcode|filter> ..."); return; }
        const String& sub = tk[1];
        if (sub == "server") {
            if (n < 3) { err("aprsiss server <hostname>"); return; }
            Config.aprsIS.server = tk[2];
            ok("aprsIS.server = " + Config.aprsIS.server);
        } else if (sub == "port") {
            if (n < 3) { err("aprsiss port <port>"); return; }
            Config.aprsIS.port = tk[2].toInt();
            ok("aprsIS.port = " + String(Config.aprsIS.port));
        } else if (sub == "passcode") {
            if (n < 3) { err("aprsiss passcode <code>"); return; }
            Config.aprsIS.passcode = tk[2];
            ok("aprsIS.passcode updated");
        } else if (sub == "filter") {
            if (n < 3) { err("aprsiss filter <filter>"); return; }
            Config.aprsIS.filter = restOfLine(line, 2);
            ok("aprsIS.filter = " + Config.aprsIS.filter);
        } else {
            err("unknown aprsiss subcommand: " + sub);
        }
    }

    static void cmdTcpKiss(String* tk, int n) {
        if (n < 2) { err("tcpkiss <on|off|port> ..."); return; }
        const String& sub = tk[1];
        if (sub == "on" || sub == "off" || sub == "true" || sub == "false") {
            applyBool(tk[1], Config.tcpKISS.enabled, "tcpKISS.enabled");
        } else if (sub == "port") {
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
        else if (cmd == "notif")                    cmdNotif(tk, n);
        else if (cmd == "bat")                      cmdBat(tk, n);
        else if (cmd == "telem")                    cmdTelem(tk, n);
        else if (cmd == "ptt")                      cmdPtt(tk, n);
        else if (cmd == "winlink")                  cmdWinlink(tk, n, line);
        else if (cmd == "wifi")                     cmdWifi(tk, n, line);
        else if (cmd == "digipeater") {
            if (n < 2) { err("digipeater on|off"); return; }
            if (applyBool(tk[1], Config.digipeating, "digipeating (boot default)")) {
                digipeaterActive = Config.digipeating;
            }
        }
        else if (cmd == "role")     cmdRole(tk, n, line);
        else if (cmd == "fixed")    cmdFixed(tk, n);
        else if (cmd == "wifista")  cmdWifiSta(tk, n, line);
        else if (cmd == "aprsiss")  cmdAprsIS(tk, n, line);
        else if (cmd == "tcpkiss")  cmdTcpKiss(tk, n);
        else if (cmd == "path")                     { Config.path = restOfLine(line, 1); ok("path = " + Config.path); }
        else if (cmd == "email")                    { Config.email = restOfLine(line, 1); ok("email = " + Config.email); }
        else if (cmd == "simplified")               { if (n >= 2) applyBool(tk[1], Config.simplifiedTrackerMode, "simplified"); else err("simplified on|off"); }
        else if (cmd == "disablegps")               { if (n >= 2) applyBool(tk[1], Config.disableGPS, "disableGPS"); else err("disablegps on|off"); }
        else if (cmd == "sendalt")                  { if (n >= 2) applyBool(tk[1], Config.sendAltitude, "sendAltitude"); else err("sendalt on|off"); }
        else if (cmd == "nonsmartrate")             { if (n >= 2) { Config.nonSmartBeaconRate = tk[1].toInt(); ok("nonSmartBeaconRate = " + String(Config.nonSmartBeaconRate)); } else err("nonsmartrate <sec>"); }
        else if (cmd == "rememberstation")          { if (n >= 2) { Config.rememberStationTime = tk[1].toInt(); ok("rememberStationTime = " + String(Config.rememberStationTime)); } else err("rememberstation <sec>"); }
        else if (cmd == "standingupdate")           { if (n >= 2) { Config.standingUpdateTime = tk[1].toInt(); ok("standingUpdateTime = " + String(Config.standingUpdateTime)); } else err("standingupdate <sec>"); }
        else if (cmd == "commentafter")             { if (n >= 2) { Config.sendCommentAfterXBeacons = tk[1].toInt(); ok("sendCommentAfterXBeacons = " + String(Config.sendCommentAfterXBeacons)); } else err("commentafter <n>"); }
        else err("unknown command: " + cmd + "  (try 'help')");
    }

    // ---------------- public ----------------
    void setup() {
        // After Serial.begin(115200) in main setup
        Serial.println(F("\n[serial] Type 'setup' over USB serial to enter the configuration menu."));
    }

    void loop() {
        while (Serial.available()) {
            int ch = Serial.read();
            if (ch < 0) break;
            char c = (char)ch;

            // paste-import mode swallows everything until balanced or aborted
            if (feedPasteByte(c)) {
                if (!pasting && active) prompt();      // only after commit/abort
                continue;
            }

            if (c == 0x03 && active) {                 // Ctrl-C clears typed buf
                buf = "";
                Serial.println(F("^C"));
                prompt();
                continue;
            }

            if (c == '\r' || c == '\n') {
                Serial.print(F("\r\n"));               // line break echo
                if (buf.length() == 0) {
                    if (active) prompt();
                    continue;
                }
                buf.trim();
                if (!active) {
                    if (buf.equalsIgnoreCase("setup")) {
                        enterSetup();
                        prompt();
                    } else {
                        // hint, don't punish accidental input
                        Serial.println(F("(type 'setup' to enter configuration)"));
                    }
                } else {
                    handleLine(buf);
                    if (active && !pasting) prompt();
                }
                buf = "";
            } else if (c == 0x08 || c == 0x7F) {       // backspace / DEL
                if (buf.length()) {
                    buf.remove(buf.length() - 1);
                    Serial.print(F("\b \b"));          // erase visually
                }
            } else if ((uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) {
                if (buf.length() < 200) {
                    buf += c;
                    Serial.write(c);                   // echo printable
                }
            }
            // else ignore
        }
    }

}
