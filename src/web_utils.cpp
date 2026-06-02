/* Copyright (C) 2025 Ricardo Guzman - CA2RXU
 *
 * This file is part of LoRa APRS Tracker.
 *
 * LoRa APRS Tracker is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LoRa APRS Tracker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LoRa APRS Tracker. If not, see <https://www.gnu.org/licenses/>.
 */

#include <ArduinoJson.h>
#include "configuration.h"
#include "web_utils.h"
#include "display.h"

extern Configuration               Config;

extern const char web_index_html[] asm("_binary_data_embed_index_html_gz_start");
extern const char web_index_html_end[] asm("_binary_data_embed_index_html_gz_end");
extern const size_t web_index_html_len = web_index_html_end - web_index_html;

extern const char web_style_css[] asm("_binary_data_embed_style_css_gz_start");
extern const char web_style_css_end[] asm("_binary_data_embed_style_css_gz_end");
extern const size_t web_style_css_len = web_style_css_end - web_style_css;

extern const char web_script_js[] asm("_binary_data_embed_script_js_gz_start");
extern const char web_script_js_end[] asm("_binary_data_embed_script_js_gz_end");
extern const size_t web_script_js_len = web_script_js_end - web_script_js;

extern const char web_bootstrap_css[] asm("_binary_data_embed_bootstrap_css_gz_start");
extern const char web_bootstrap_css_end[] asm("_binary_data_embed_bootstrap_css_gz_end");
extern const size_t web_bootstrap_css_len = web_bootstrap_css_end - web_bootstrap_css;

extern const char web_bootstrap_js[] asm("_binary_data_embed_bootstrap_js_gz_start");
extern const char web_bootstrap_js_end[] asm("_binary_data_embed_bootstrap_js_gz_end");
extern const size_t web_bootstrap_js_len = web_bootstrap_js_end - web_bootstrap_js;

// Declare external symbols for the embedded image data
extern const unsigned char favicon_data[] asm("_binary_data_embed_favicon_png_gz_start");
extern const unsigned char favicon_data_end[] asm("_binary_data_embed_favicon_png_gz_end");
extern const size_t favicon_data_len = favicon_data_end - favicon_data;

namespace WEB_Utils {

    AsyncWebServer server(80);

    void handleNotFound(AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(404, "text/plain", "Not found");
        response->addHeader("Cache-Control", "max-age=3600");
        request->send(response);
    }

    void handleStatus(AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "OK");
    }

    void handleHome(AsyncWebServerRequest *request) {

        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (const uint8_t*)web_index_html, web_index_html_len);
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    }

    void handleFavicon(AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "image/x-icon", (const uint8_t*)favicon_data, favicon_data_len);
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    }

    void handleReadConfiguration(AsyncWebServerRequest *request) {

        File file = SPIFFS.open("/tracker_conf.json");

        String fileContent;
        while(file.available()){
            fileContent += String((char)file.read());
        }

        request->send(200, "application/json", fileContent);
    }

    void handleReceivedPackets(AsyncWebServerRequest *request) {
        JsonDocument data;

        String buffer;

        serializeJson(data, buffer);

        request->send(200, "application/json", buffer);
    }

    void handleWriteConfiguration(AsyncWebServerRequest *request) {
        Serial.println("Got new config from www");

        auto getParamStringSafe = [&](const String& name, const String& defaultValue = "") -> String {
            if (request->hasParam(name, true)) {
                return request->getParam(name, true)->value();
            }
            return defaultValue;
        };

        auto getParamIntSafe = [&](const String& name, int defaultValue = 0) -> int {
            if (request->hasParam(name, true)) {
                return request->getParam(name, true)->value().toInt();
            }
            return defaultValue;
        };

        auto getParamFloatSafe = [&](const String& name, float defaultValue = 0.0) -> float {
            if (request->hasParam(name, true)) {
                return request->getParam(name, true)->value().toFloat();
            }
            return defaultValue;
        };

        auto getParamDoubleSafe = [&](const String& name, double defaultValue = 0.0) -> double {
            if (request->hasParam(name, true)) {
                return request->getParam(name, true)->value().toDouble();
            }
            return defaultValue;
        };

        //  Beacon (single)
        Config.beacons[0].callsign      = getParamStringSafe("beacons.0.callsign",     Config.beacons[0].callsign);
        Config.beacons[0].symbol        = getParamStringSafe("beacons.0.symbol",        Config.beacons[0].symbol);
        Config.beacons[0].overlay       = getParamStringSafe("beacons.0.overlay",       Config.beacons[0].overlay);
        Config.beacons[0].micE          = getParamStringSafe("beacons.0.micE",          Config.beacons[0].micE);
        Config.beacons[0].comment       = getParamStringSafe("beacons.0.comment",       Config.beacons[0].comment);
        Config.beacons[0].status        = getParamStringSafe("beacons.0.status",        Config.beacons[0].status);
        Config.beacons[0].profileLabel  = getParamStringSafe("beacons.0.profileLabel",  Config.beacons[0].profileLabel);
        if (request->hasParam("beacons.0.tacticalCallsign", true)) {
            String tac = request->getParam("beacons.0.tacticalCallsign", true)->value();
            tac.trim();
            if (tac.length() > 9) tac = tac.substring(0, 9);
            Config.beacons[0].tacticalCallsign = tac;
        }
        Config.beacons[0].gpsEcoMode        = request->hasParam("beacons.0.gpsEcoMode",       true);
        Config.beacons[0].smartBeaconActive  = request->hasParam("beacons.0.smartBeaconActive", true);
        Config.beacons[0].smartBeaconSetting = getParamIntSafe("beacons.0.smartBeaconSetting",  Config.beacons[0].smartBeaconSetting);

        //  Station Config
        Config.path                             = getParamStringSafe("path", Config.path);
        Config.sendCommentAfterXBeacons         = getParamIntSafe("sendCommentAfterXBeacons", Config.sendCommentAfterXBeacons);
        Config.nonSmartBeaconRate               = getParamIntSafe("nonSmartBeaconRate", Config.nonSmartBeaconRate);
        Config.standingUpdateTime               = getParamIntSafe("standingUpdateTime", Config.standingUpdateTime);
        Config.email                            = getParamStringSafe("email", Config.email);
        Config.rememberStationTime              = getParamIntSafe("rememberStationTime", Config.rememberStationTime);
        Config.sendAltitude                     = request->hasParam("sendAltitude", true);
        Config.disableGPS                       = request->hasParam("disableGPS", true);
        Config.simplifiedTrackerMode            = request->hasParam("simplifiedTrackerMode", true);

        //  Display
        Config.display.ecoMode                  = request->hasParam("display.ecoMode", true);
        if (Config.display.ecoMode) {
            Config.display.timeout              = getParamIntSafe("display.timeout", Config.display.timeout);
        }
        Config.display.turn180                  = request->hasParam("display.turn180", true);
        Config.display.showSymbol               = request->hasParam("display.showSymbol", true);

        //  Bluetooth
        Config.bluetooth.active                 = request->hasParam("bluetooth.active", true);
        if (Config.bluetooth.active) {
            Config.bluetooth.deviceName         = getParamStringSafe("bluetooth.deviceName", Config.bluetooth.deviceName);
            Config.bluetooth.useBLE             = request->hasParam("bluetooth.useBLE", true);
            Config.bluetooth.useKISS            = request->hasParam("bluetooth.useKISS", true);
        }

        // LoRa (single)
        Config.loraTypes[0].frequency       = getParamDoubleSafe("lora.0.frequency",       Config.loraTypes[0].frequency);
        Config.loraTypes[0].spreadingFactor = getParamIntSafe   ("lora.0.spreadingFactor", Config.loraTypes[0].spreadingFactor);
        Config.loraTypes[0].codingRate4     = getParamIntSafe   ("lora.0.codingRate4",     Config.loraTypes[0].codingRate4);
        Config.loraTypes[0].signalBandwidth = getParamIntSafe   ("lora.0.signalBandwidth", Config.loraTypes[0].signalBandwidth);
        Config.loraTypes[0].power           = getParamIntSafe   ("lora.0.power",           Config.loraTypes[0].power);

        //  Battery
        Config.battery.sendVoltage              = request->hasParam("battery.sendVoltage", true);
        if (Config.battery.sendVoltage) {
            Config.battery.voltageAsTelemetry   = request->hasParam("battery.voltageAsTelemetry", true);
            Config.battery.sendVoltageAlways    = request->hasParam("battery.sendVoltageAlways", true);
        }
        Config.battery.monitorVoltage           = request->hasParam("battery.monitorVoltage", true);
        if (Config.battery.monitorVoltage) Config.battery.sleepVoltage = getParamFloatSafe("battery.sleepVoltage", Config.battery.sleepVoltage);

        //  Telemetry
        Config.telemetry.active                 = request->hasParam("telemetry.active", true);
        if (Config.telemetry.active) {
            Config.telemetry.sendTelemetry          = request->hasParam("telemetry.sendTelemetry", true);
            Config.telemetry.temperatureCorrection  = getParamFloatSafe("telemetry.temperatureCorrection", Config.telemetry.temperatureCorrection);
        }

        //  Winlink
        Config.winlink.password                 = getParamStringSafe("winlink.password", Config.winlink.password);

        //  WiFi AP
        Config.wifiAP.password                  = getParamStringSafe("wifiAP.password", Config.wifiAP.password);
        Config.wifiAP.active                    = false; // when Configuration is finished Tracker returns to normal mode.

        //  Device Role & GPS Source
        Config.deviceRole   = (DeviceRole) getParamIntSafe("deviceRole",  (int)Config.deviceRole);
        Config.gpsSource    = (GPSSource)  getParamIntSafe("gpsSource",   (int)Config.gpsSource);

        //  Fixed Position (used when GPS source = Fixed or as iGate beacon position)
        Config.fixedPosition.latitude   = getParamDoubleSafe("fixedPosition.latitude",  Config.fixedPosition.latitude);
        Config.fixedPosition.longitude  = getParamDoubleSafe("fixedPosition.longitude", Config.fixedPosition.longitude);
        Config.fixedPosition.elevation  = getParamFloatSafe ("fixedPosition.elevation", Config.fixedPosition.elevation);

        //  WiFi STA (for iGate mode)
        Config.wifiSTA.enabled  = request->hasParam("wifiSTA.enabled", true);
        Config.wifiSTA.ssid     = getParamStringSafe("wifiSTA.ssid",     Config.wifiSTA.ssid);
        Config.wifiSTA.password = getParamStringSafe("wifiSTA.password", Config.wifiSTA.password);

        //  APRS-IS
        Config.aprsIS.server   = getParamStringSafe("aprsIS.server",   Config.aprsIS.server);
        Config.aprsIS.port     = getParamIntSafe   ("aprsIS.port",     Config.aprsIS.port);
        Config.aprsIS.passcode = getParamStringSafe("aprsIS.passcode", Config.aprsIS.passcode);
        Config.aprsIS.filter   = getParamStringSafe("aprsIS.filter",   Config.aprsIS.filter);

        //  TCP KISS server
        Config.tcpKISS.enabled        = request->hasParam("tcpKISS.enabled",       true);
        Config.tcpKISS.port           = getParamIntSafe("tcpKISS.port",            Config.tcpKISS.port);
        Config.tcpKISS.serialEnabled  = request->hasParam("tcpKISS.serialEnabled", true);

        //  SmartBeacon custom profile (profile index 3)
        Config.customSmartBeacon.slowRate       = getParamIntSafe("customSmartBeacon.slowRate",      Config.customSmartBeacon.slowRate);
        Config.customSmartBeacon.slowSpeed      = getParamIntSafe("customSmartBeacon.slowSpeed",     Config.customSmartBeacon.slowSpeed);
        Config.customSmartBeacon.fastRate       = getParamIntSafe("customSmartBeacon.fastRate",      Config.customSmartBeacon.fastRate);
        Config.customSmartBeacon.fastSpeed      = getParamIntSafe("customSmartBeacon.fastSpeed",     Config.customSmartBeacon.fastSpeed);
        Config.customSmartBeacon.minTxDist      = getParamIntSafe("customSmartBeacon.minTxDist",     Config.customSmartBeacon.minTxDist);
        Config.customSmartBeacon.minDeltaBeacon = getParamIntSafe("customSmartBeacon.minDeltaBeacon",Config.customSmartBeacon.minDeltaBeacon);
        Config.customSmartBeacon.turnMinDeg     = getParamIntSafe("customSmartBeacon.turnMinDeg",    Config.customSmartBeacon.turnMinDeg);
        Config.customSmartBeacon.turnSlope      = getParamIntSafe("customSmartBeacon.turnSlope",     Config.customSmartBeacon.turnSlope);

        //  Notification
        Config.notification.ledTx               = request->hasParam("notification.ledTx", true);
        if (Config.notification.ledTx) Config.notification.ledTxPin = getParamIntSafe("notification.ledTxPin", Config.notification.ledTxPin);
        Config.notification.ledMessage          = request->hasParam("notification.ledMessage", true);
        if (Config.notification.ledMessage) Config.notification.ledMessagePin = getParamIntSafe("notification.ledMessagePin", Config.notification.ledMessagePin);
        Config.notification.buzzerActive        = request->hasParam("notification.buzzerActive", true);
        if (Config.notification.buzzerActive) {
            Config.notification.buzzerPinTone   = getParamIntSafe("notification.buzzerPinTone", Config.notification.buzzerPinTone);
            Config.notification.buzzerPinVcc    = getParamIntSafe("notification.buzzerPinVcc", Config.notification.buzzerPinVcc);
            Config.notification.bootUpBeep      = request->hasParam("notification.bootUpBeep", true);
            Config.notification.txBeep          = request->hasParam("notification.txBeep", true);
            Config.notification.messageRxBeep   = request->hasParam("notification.messageRxBeep", true);
            Config.notification.stationBeep     = request->hasParam("notification.stationBeep", true);
            Config.notification.lowBatteryBeep  = request->hasParam("notification.lowBatteryBeep", true);
            Config.notification.shutDownBeep    = request->hasParam("notification.shutDownBeep", true);
        }
        Config.notification.ledFlashlight           = request->hasParam("notification.ledFlashlight", true);
        if (Config.notification.ledFlashlight) Config.notification.ledFlashlightPin = getParamIntSafe("notification.ledFlashlightPin", Config.notification.ledFlashlightPin);

        //  PTT Trigger
        Config.ptt.active                       = request->hasParam("ptt.active", true);
        if (Config.ptt.active) {
            Config.ptt.reverse                  = request->hasParam("ptt.reverse", true);
            Config.ptt.io_pin                   = getParamIntSafe("ptt.io_pin", Config.ptt.io_pin);
            Config.ptt.preDelay                 = getParamIntSafe("ptt.preDelay", Config.ptt.preDelay);
            Config.ptt.postDelay                = getParamIntSafe("ptt.postDelay", Config.ptt.postDelay);
        }

        bool saveSuccess = Config.writeFile();

        if (saveSuccess) {
            Serial.println("Configuration saved successfully");
            AsyncWebServerResponse *response = request->beginResponse(302, "text/html", "");
            response->addHeader("Location", "/?success=1");
            request->send(response);

            displayToggle(false);
            delay(500);
            ESP.restart();
        } else {
            Serial.println("Error saving configuration!");
            String errorPage = "<!DOCTYPE html><html><head><title>Error</title></head><body>";
            errorPage += "<h1>Configuration Error:</h1>";
            errorPage += "<p>Couldn't save new configuration. Please try again.</p>";
            errorPage += "<a href='/'>Back</a></body></html>";

            AsyncWebServerResponse *response = request->beginResponse(500, "text/html", errorPage);
            request->send(response);
        }
    }

    void handleAction(AsyncWebServerRequest *request) {
        String type = request->getParam("type", false)->value();

        if (type == "send-beacon") {
            //lastBeaconTx = 0;

            request->send(200, "text/plain", "Beacon will be sent in a while");
        } else if (type == "reboot") {
            displayToggle(false);
            ESP.restart();
        } else {
            request->send(404, "text/plain", "Not Found");
        }
    }

    void handleStyle(AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/css", (const uint8_t*)web_style_css, web_style_css_len);
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    }

    void handleScript(AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/javascript", (const uint8_t*)web_script_js, web_script_js_len);
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    }

    void handleBootstrapStyle(AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/css", (const uint8_t*)web_bootstrap_css, web_bootstrap_css_len);
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "max-age=3600");
        request->send(response);
    }

    void handleBootstrapScript(AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/javascript", (const uint8_t*)web_bootstrap_js, web_bootstrap_js_len);
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "max-age=3600");
        request->send(response);
    }

    void setup() {
        server.on("/", HTTP_GET, handleHome);
        server.on("/status", HTTP_GET, handleStatus);
        //server.on("/received-packets.json", HTTP_GET, handleReceivedPackets);
        server.on("/configuration.json", HTTP_GET, handleReadConfiguration);
        server.on("/configuration.json", HTTP_POST, handleWriteConfiguration);
        server.on("/action", HTTP_POST, handleAction);
        server.on("/style.css", HTTP_GET, handleStyle);
        server.on("/script.js", HTTP_GET, handleScript);
        server.on("/bootstrap.css", HTTP_GET, handleBootstrapStyle);
        server.on("/bootstrap.js", HTTP_GET, handleBootstrapScript);
        server.on("/favicon.png", HTTP_GET, handleFavicon);

        server.onNotFound(handleNotFound);

        server.begin();
    }

}