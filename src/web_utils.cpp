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
#include <esp_timer.h>
#include <Update.h>
#include "configuration.h"
#include "web_utils.h"
#include "display.h"
#include "battery_utils.h"
#include "aprs_is_utils.h"
#include "log_buffer.h"
#include "version.h"

extern Configuration               Config;

// Schedule a soft reset from an IDF timer task — safe to call from any context
// including AsyncWebServer request handlers (async_tcp task).
static void scheduleRestart(uint32_t delayMs) {
    esp_timer_handle_t t;
    esp_timer_create_args_t args = {};
    args.callback         = [](void*){ esp_restart(); };
    args.dispatch_method  = ESP_TIMER_TASK;
    args.name             = "web_rst";
    esp_timer_create(&args, &t);
    esp_timer_start_once(t, (uint64_t)delayMs * 1000);
}

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

    AsyncWebServer  server(80);
    AsyncEventSource events("/events");

    // Tracks the highest LogBuffer sequence number already sent to SSE clients.
    static uint32_t sseLastSeq  = 0;
    static uint32_t lastSseTick = 0;

    void handleNotFound(AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(404, "text/plain", "Not found");
        response->addHeader("Cache-Control", "max-age=3600");
        request->send(response);
    }

    void handleStatus(AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "OK");
    }

    void handleAprsIsStatus(AsyncWebServerRequest *request) {
        bool up = APRS_IS_Utils::isConnected();
        request->send(200, "application/json", up ? "{\"connected\":true}" : "{\"connected\":false}");
    }

    void handleBattery(AsyncWebServerRequest *request) {
        BATTERY_Utils::obtainBatteryInfo();
        String voltStr = BATTERY_Utils::getBatteryInfoVoltage();
        String pctStr  = BATTERY_Utils::getPercentVoltageBattery(voltStr.toFloat());
        pctStr.trim();
        String json = "{\"voltage\":\"" + voltStr + "\",\"percent\":\"" + pctStr + "\"}";
        request->send(200, "application/json", json);
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
        Config.beacons[0].smartBeaconActive  = request->hasParam("beacons.0.smartBeaconActive", true);
        Config.beacons[0].smartBeaconSetting = getParamIntSafe("beacons.0.smartBeaconSetting",  Config.beacons[0].smartBeaconSetting);

        //  Station Config
        Config.beaconPath = getParamStringSafe("beaconPath", getParamStringSafe("path", Config.beaconPath));
        Config.sendCommentAfterXBeacons         = getParamIntSafe("sendCommentAfterXBeacons", Config.sendCommentAfterXBeacons);
        Config.nonSmartBeaconRate               = getParamIntSafe("nonSmartBeaconRate", Config.nonSmartBeaconRate);
        Config.sendSpeedCourse                  = request->hasParam("sendSpeedCourse", true);
        Config.sendAltitude                     = request->hasParam("sendAltitude", true);
        //  Display
        Config.display.ecoMode                  = request->hasParam("display.ecoMode", true);
        if (Config.display.ecoMode) {
            Config.display.timeout              = getParamIntSafe("display.timeout", Config.display.timeout);
        }
        Config.display.turn180                  = request->hasParam("display.turn180", true);
        Config.display.invertDisplay            = request->hasParam("display.invertDisplay", true);
        Config.display.ledEnabled               = request->hasParam("display.ledEnabled", true);

        //  Bluetooth
        Config.bluetooth.active                 = request->hasParam("bluetooth.active", true);
        if (Config.bluetooth.active) {
            Config.bluetooth.deviceName         = getParamStringSafe("bluetooth.deviceName", Config.bluetooth.deviceName);
        }

        // LoRa (single)
        Config.loraTypes[0].frequency       = getParamDoubleSafe("lora.0.frequency",       Config.loraTypes[0].frequency);
        Config.loraTypes[0].spreadingFactor = getParamIntSafe   ("lora.0.spreadingFactor", Config.loraTypes[0].spreadingFactor);
        Config.loraTypes[0].codingRate4     = getParamIntSafe   ("lora.0.codingRate4",     Config.loraTypes[0].codingRate4);
        Config.loraTypes[0].signalBandwidth = getParamIntSafe   ("lora.0.signalBandwidth", Config.loraTypes[0].signalBandwidth);
        Config.loraTypes[0].power           = getParamIntSafe   ("lora.0.power",           Config.loraTypes[0].power);

        //  Battery
        Config.battery.sendVoltage              = request->hasParam("battery.sendVoltage", true);
        Config.battery.sendVoltageAlways        = request->hasParam("battery.sendVoltageAlways", true);
        Config.battery.sleepVoltage             = getParamFloatSafe("battery.sleepVoltage", Config.battery.sleepVoltage);

        //  WiFi AP
        Config.wifiAP.password                  = getParamStringSafe("wifiAP.password", Config.wifiAP.password);

        //  Device Role & GPS Source
        Config.deviceRole   = (DeviceRole) getParamIntSafe("deviceRole",  (int)Config.deviceRole);
        Config.gpsSource    = (GPSSource)  getParamIntSafe("gpsSource",   (int)Config.gpsSource);
        Config.digiMode     = (DigiMode)   getParamIntSafe("digiMode",    (int)Config.digiMode);

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
        Config.tcpKISS.port           = getParamIntSafe("tcpKISS.port",            Config.tcpKISS.port);

        //  SmartBeacon custom profile (profile index 3)
        Config.customSmartBeacon.slowRate       = getParamIntSafe("customSmartBeacon.slowRate",      Config.customSmartBeacon.slowRate);
        Config.customSmartBeacon.slowSpeed      = getParamIntSafe("customSmartBeacon.slowSpeed",     Config.customSmartBeacon.slowSpeed);
        Config.customSmartBeacon.fastRate       = getParamIntSafe("customSmartBeacon.fastRate",      Config.customSmartBeacon.fastRate);
        Config.customSmartBeacon.fastSpeed      = getParamIntSafe("customSmartBeacon.fastSpeed",     Config.customSmartBeacon.fastSpeed);
        Config.customSmartBeacon.turnMinDeg     = getParamIntSafe("customSmartBeacon.turnMinDeg",    Config.customSmartBeacon.turnMinDeg);
        Config.customSmartBeacon.turnSlope      = getParamIntSafe("customSmartBeacon.turnSlope",     Config.customSmartBeacon.turnSlope);

        //  PTT Trigger
        Config.ptt.active                       = request->hasParam("ptt.active", true);
        if (Config.ptt.active) {
            Config.ptt.reverse                  = request->hasParam("ptt.reverse", true);
            Config.ptt.io_pin                   = getParamIntSafe("ptt.io_pin", Config.ptt.io_pin);
            Config.ptt.preDelay                 = getParamIntSafe("ptt.preDelay", Config.ptt.preDelay);
            Config.ptt.postDelay                = getParamIntSafe("ptt.postDelay", Config.ptt.postDelay);
        }

        //  PHG (Power-Height-Gain) — uncompressed beacon on separate timer
        Config.phg.enabled     = request->hasParam("phg.enabled", true);
        Config.phg.power       = (uint8_t)getParamIntSafe("phg.power",       Config.phg.power);
        Config.phg.height      = (uint8_t)getParamIntSafe("phg.height",      Config.phg.height);
        Config.phg.gain        = (uint8_t)getParamIntSafe("phg.gain",        Config.phg.gain);
        Config.phg.directivity = (uint8_t)getParamIntSafe("phg.directivity", Config.phg.directivity);
        Config.phg.beaconRate  = (uint8_t)getParamIntSafe("phg.beaconRate",  Config.phg.beaconRate);

        bool saveSuccess = Config.writeFile();

        if (saveSuccess) {
            Serial.println("Configuration saved successfully");
            AsyncWebServerResponse *response = request->beginResponse(302, "text/html", "");
            response->addHeader("Location", "/?success=1");
            request->send(response);

            Serial.println("Restarting...");
            scheduleRestart(1000);
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
            request->send(200, "text/plain", "Rebooting...");
            scheduleRestart(1000);
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

    // Reports the running firmware version and board environment name.
    void handleVersion(AsyncWebServerRequest *request) {
        // PIOENV is not a C preprocessor macro; derive the env name from the
        // per-variant defines that are already passed in build_flags.
        #if defined(HELTEC_V3_433_APRS)
            static const char kEnv[] = "heltec_v3_433_aprs";
        #elif defined(TTGO_T_Beam_V1_2_433_APRS)
            static const char kEnv[] = "tbeam_433_aprs";
        #elif defined(LILYGO_T3_433_APRS)
            static const char kEnv[] = "lilygo_t3_433_aprs";
        #elif defined(LORANGER_V1)
            static const char kEnv[] = "LoRanger_V1";
        #else
            static const char kEnv[] = "unknown";
        #endif
        String json = "{\"version\":\"" FIRMWARE_VERSION_DATE "\",\"env\":\"";
        json += kEnv;
        json += "\"}";
        request->send(200, "application/json", json);
    }

    // Streams the incoming firmware.bin (or spiffs.bin) into the active OTA slot.
    // The file name drives slot selection: "spiffs.bin" → U_SPIFFS, else → U_FLASH.
    // On success the device reboots via scheduleRestart(); on failure the error
    // string is returned in the response body so the UI can display it.
    void handleOtaUploadBody(AsyncWebServerRequest *request, const String &filename,
                             size_t index, uint8_t *data, size_t len, bool final) {
        if (!index) {
            int cmd = (filename == "spiffs.bin") ? U_SPIFFS : U_FLASH;
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
                Update.printError(Serial);
            }
        }
        if (Update.isRunning()) {
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }
        }
        if (final) {
            if (!Update.end(true)) {
                Update.printError(Serial);
            }
        }
    }

    void handleOtaUploadResponse(AsyncWebServerRequest *request) {
        bool ok = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(
            200, "text/plain", ok ? "OK" : Update.errorString());
        response->addHeader("Connection", "close");
        request->send(response);
        if (ok) {
            scheduleRestart(1500);
        }
    }

    void setup() {
        server.on("/", HTTP_GET, handleHome);
        server.on("/status", HTTP_GET, handleStatus);
        server.on("/battery.json", HTTP_GET, handleBattery);
        server.on("/aprs-is-status.json", HTTP_GET, handleAprsIsStatus);
        //server.on("/received-packets.json", HTTP_GET, handleReceivedPackets);
        server.on("/configuration.json", HTTP_GET, handleReadConfiguration);
        server.on("/configuration.json", HTTP_POST, handleWriteConfiguration);
        server.on("/action", HTTP_POST, handleAction);
        server.on("/version.json", HTTP_GET, handleVersion);
        server.on("/update", HTTP_POST, handleOtaUploadResponse, handleOtaUploadBody);
        server.on("/style.css", HTTP_GET, handleStyle);
        server.on("/script.js", HTTP_GET, handleScript);
        server.on("/bootstrap.css", HTTP_GET, handleBootstrapStyle);
        server.on("/bootstrap.js", HTTP_GET, handleBootstrapScript);
        server.on("/favicon.png", HTTP_GET, handleFavicon);

        // SSE endpoint: replay buffered events to new client, then push live events.
        events.onConnect([](AsyncEventSourceClient *client) {
            char jsonBuf[256];
            LogBuffer::forEach([&](const LogBuffer::Entry& e) {
                LogBuffer::toJson(e, jsonBuf, sizeof(jsonBuf));
                client->send(jsonBuf, "log", e.ms);
            });
        });
        server.addHandler(&events);

        server.onNotFound(handleNotFound);

        server.begin();
    }

    // Call from main loop — sends new ring-buffer entries to connected SSE clients.
    // Returns immediately (< 1 µs) if no clients are connected or nothing is new.
    void loop() {
        if (events.count() == 0) return;

        uint32_t now = millis();
        if (now - lastSseTick < 100) return;   // throttle to 10 Hz max
        lastSseTick = now;

        char jsonBuf[256];
        LogBuffer::forEach([&](const LogBuffer::Entry& e) {
            if (e.seq > sseLastSeq) {
                LogBuffer::toJson(e, jsonBuf, sizeof(jsonBuf));
                events.send(jsonBuf, "log", e.ms);
                sseLastSeq = e.seq;
            }
        });
    }

}