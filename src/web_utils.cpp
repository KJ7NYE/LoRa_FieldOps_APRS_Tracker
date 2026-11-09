/* Copyright (C) 2026 Ricardo Guzman - CA2RXU
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
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <vector>
#include "board_pinout.h"
#include "configuration.h"
#include "lora_utils.h"
#include "ble_pin.h"
#include "web_utils.h"
#include "wifi_utils.h"
#include "display.h"
#include "battery_utils.h"
#include "aprs_is_utils.h"
#include "log_buffer.h"
#include "version.h"
#include "station_utils.h"
#include "logger.h"

extern Configuration               Config;
extern logging::Logger             logger;

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

    // Same fields as the serial CLI's `aprsiss status` (host/port as dialed,
    // login = verified|unverified|pending, server = pool member named in the
    // login response, once received).
    void handleAprsIsStatus(AsyncWebServerRequest *request) {
        JsonDocument doc;
        const bool up = APRS_IS_Utils::isConnected();
        doc["connected"] = up;
        if (up) {
            doc["host"] = APRS_IS_Utils::getConnectedHost();
            doc["port"]  = APRS_IS_Utils::getConnectedPort();
            doc["login"] = APRS_IS_Utils::getLoginState();
            const String serverName = APRS_IS_Utils::getServerName();
            if (serverName.length() > 0) doc["server"] = serverName;
        }
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    }

    // Same fields as the serial CLI's `wifista status`. Serialized with
    // ArduinoJson so an SSID containing quotes or backslashes stays valid JSON.
    void handleWifiStaStatus(AsyncWebServerRequest *request) {
        JsonDocument doc;
        const bool up = WIFI_Utils::isSTAConnected();
        doc["connected"] = up;
        if (up) {
            doc["ip"]   = WiFi.localIP().toString();
            doc["ssid"] = WiFi.SSID();
        }
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    }

    // Blocking (~2-4s): runs a synchronous WiFi scan inside the AsyncTCP
    // request-handler task. This stalls other concurrent HTTP responses for
    // the duration but not the main loop() (beacon TX, GPS, LoRa RX/TX all
    // continue), an acceptable cost for a manual, infrequent button click.
    void handleWifiScan(AsyncWebServerRequest *request) {
        std::vector<WIFI_Utils::ScanResult> results;
        WIFI_Utils::scanNetworks(results, 20);

        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (auto& r : results) {
            JsonObject o = arr.add<JsonObject>();
            o["ssid"]   = r.ssid;
            o["rssi"]   = r.rssi;
            o["secure"] = r.secure;
        }
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
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
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "WebUI", "Got new config from www");

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

        // Validate the BLE PIN before mutating any Config field, so a rejected
        // save leaves the running configuration untouched.
        const String blePin = getParamStringSafe("bluetooth.pin", Config.bluetooth.pin);
        if (!BlePin::isValid(blePin)) {
            request->send(400, "text/plain", "Bluetooth PIN must be 4 to 6 digits");
            return;
        }

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
        Config.beacons[0].smartBeaconActive  = getParamStringSafe("beacons.0.smartBeaconActive") == "true";
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
        Config.bluetooth.pinEnabled             = request->hasParam("bluetooth.pinEnabled", true);
        Config.bluetooth.pin                    = blePin;

        // LoRa (single)
        Config.loraTypes[0].frequency       = getParamDoubleSafe("lora.0.frequency",       Config.loraTypes[0].frequency);
        Config.loraTypes[0].spreadingFactor = getParamIntSafe   ("lora.0.spreadingFactor", Config.loraTypes[0].spreadingFactor);
        Config.loraTypes[0].codingRate4     = getParamIntSafe   ("lora.0.codingRate4",     Config.loraTypes[0].codingRate4);
        Config.loraTypes[0].signalBandwidth = getParamIntSafe   ("lora.0.signalBandwidth", Config.loraTypes[0].signalBandwidth);
        // Board-specific safety ceiling: the T-Beam 1W's HAS_1W_LORA path
        // drives an external 30 dBm PA with no headroom check from RadioLib
        // (see LoRa_Utils::applyOutputPower()), so cap saved values here too —
        // a direct POST to this endpoint bypasses the config UI's dropdown.
        #ifdef HAS_1W_LORA
        Config.loraTypes[0].power           = constrain(getParamIntSafe("lora.0.power", Config.loraTypes[0].power), 2, 20);
        #else
        Config.loraTypes[0].power           = constrain(getParamIntSafe("lora.0.power", Config.loraTypes[0].power), 2, 22);
        #endif
        // Global listen-before-TX switch (stored under "other"); shown in the LoRa Radio section.
        Config.cadActive                    = request->hasParam("cadActive", true);

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

        //  WiFi STA (for iGate mode) -- up to MAX_WIFI_NETWORKS networks, tried in
        //  list order. AsyncWebServer has no "enumerate params by prefix" API, so
        //  the form sends an explicit count alongside indexed wifiSTA.networks.<i>.*
        //  fields; never trust the client-sent count, always clamp server-side.
        Config.wifiSTA.enabled = request->hasParam("wifiSTA.enabled", true);
        {
            int count = getParamIntSafe("wifiSTA.networksCount", 0);
            count = constrain(count, 0, (int)MAX_WIFI_NETWORKS);
            std::vector<WiFiNetwork> nets;
            for (int i = 0; i < count; i++) {
                WiFiNetwork net;
                net.ssid     = getParamStringSafe("wifiSTA.networks." + String(i) + ".ssid", "");
                net.password = getParamStringSafe("wifiSTA.networks." + String(i) + ".password", "");
                net.ssid.trim();
                if (net.ssid.length() == 0) continue;   // drop blank rows silently
                nets.push_back(net);
            }
            Config.wifiSTA.networks = nets;
        }

        //  APRS-IS
        Config.aprsIS.server   = getParamStringSafe("aprsIS.server",   Config.aprsIS.server);
        Config.aprsIS.port     = getParamIntSafe   ("aprsIS.port",     Config.aprsIS.port);
        Config.aprsIS.passcode = getParamStringSafe("aprsIS.passcode", Config.aprsIS.passcode);
        Config.aprsIS.filter   = getParamStringSafe("aprsIS.filter",   Config.aprsIS.filter);
        Config.aprsIS.downlinkEnabled = request->hasParam("aprsIS.downlinkEnabled", true);

        //  TCP KISS server
        Config.tcpKISS.port           = getParamIntSafe("tcpKISS.port",            Config.tcpKISS.port);

        //  Remote configuration (CourseSentry) -- compact read/write protocol
        //  over APRS directed messages. Off by default; see remote_cfg_utils.h.
        Config.remoteCfg.enabled          = request->hasParam("remoteCfg.enabled", true);
        Config.remoteCfg.token            = getParamStringSafe("remoteCfg.token", Config.remoteCfg.token);
        Config.remoteCfg.unlockWindowSec  = constrain(getParamIntSafe("remoteCfg.unlockWindowSec", Config.remoteCfg.unlockWindowSec), 60, 3600);

        //  SmartBeacon custom profile (profile index 4). Form field names are
        //  "customSmartBeacon.<jsonKey>" from the shared field table; values the
        //  browser's min/max did not catch are clamped into range.
        for (const SmartBeaconField& f : SMARTBEACON_CUSTOM_FIELDS) {
            int& value = Config.customSmartBeacon.*(f.member);
            value = getParamIntSafe(String("customSmartBeacon.") + f.jsonKey, value);
        }
        SMARTBEACON_Utils::clampCustomValues(Config.customSmartBeacon);

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
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "WebUI", "Configuration saved successfully");
            AsyncWebServerResponse *response = request->beginResponse(302, "text/html", "");
            response->addHeader("Location", "/?success=1");
            request->send(response);

            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "WebUI", "Restarting...");
            scheduleRestart(1000);
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "WebUI", "Error saving configuration!");
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

        if (type == "send-comment") {
            STATION_Utils::sendCommentBeaconNow();
            request->send(200, "text/plain", "Comment beacon sent");
        } else if (type == "send-status") {
            STATION_Utils::sendStatusBeaconNow();
            request->send(200, "text/plain", "Status beacon sent");
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
        // BOARD_ENV_ID (version.h) is the single board-name table, shared with
        // the serial `version` command, so the two can't drift apart.
        String json = "{\"version\":\"" FIRMWARE_VERSION_DATE "\",\"env\":\"";
        json += BOARD_ENV_ID;
        json += "\"}";
        request->send(200, "application/json", json);
    }

    // ── In-device firmware update (Device → Update Firmware) ─────────────────
    //
    // firmware.bin is written to the OTA slot that is NOT running
    // (esp_ota_get_next_update_partition), then made the boot slot, so the
    // device alternates app0 <-> app1 on each update. A running slot can never
    // be rewritten in place: esp_ota_begin() refuses it with
    // ESP_ERR_OTA_PARTITION_CONFLICT. (Pinning every update to app0, as this
    // handler used to, therefore failed whenever the device ran from app0,
    // which is the normal state after any USB flash.)
    //
    // The USB web flasher's "Firmware Update" image (<board>_ota_update.bin)
    // includes boot_app0.bin at the otadata offset, which resets the boot
    // selection to app0 before writing app0, so the two update paths stay
    // consistent whichever slot this updater last selected.
    //
    // Boards on huge_app.csv (tbeam_433_aprs, lilygo_t3_433_aprs) have a single
    // app slot and cannot update themselves; they get a clear error instead.
    //
    // spiffs.bin (filesystem image) still goes through Update.h's U_SPIFFS path.

    static esp_ota_handle_t       otaHandle       = 0;
    static const esp_partition_t* otaAppPartition = nullptr;
    static bool                   otaFlashActive  = false;
    static bool                   otaError        = false;
    static size_t                 otaBytesWritten = 0;
    static String                 otaErrorMessage;          // returned to the browser

    static constexpr uint8_t ESP_APP_IMAGE_MAGIC = 0xE9;    // first byte of every ESP app image

    // Record the first failure (later ones are usually consequences of it),
    // log it to serial and the web live log, and stop writing.
    static void otaFail(const String& message) {
        if (otaError) return;
        otaError        = true;
        otaErrorMessage = message;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "OTA", "%s", message.c_str());
        LogBuffer::pushf(LogBuffer::TYPE_INFO, "OTA: %s", message.c_str());
        if (otaFlashActive) {
            esp_ota_abort(otaHandle);   // frees the handle; the target slot is left unbootable, never selected
            otaFlashActive = false;
        }
    }

    // Start writing firmware.bin: validate the file, pick the inactive slot.
    static void beginFirmwareUpdate(const String& filename, const uint8_t* data, size_t len) {
        // The USB-flasher images start with the bootloader at offset 0 and
        // would overwrite the slot with a non-app layout.
        if (filename.indexOf("web_factory") >= 0 || filename.indexOf("ota_update") >= 0) {
            otaFail("\"" + filename + "\" is a USB flasher image. Upload <board>_firmware.bin here instead.");
            return;
        }
        if (len == 0 || data[0] != ESP_APP_IMAGE_MAGIC) {
            otaFail("\"" + filename + "\" is not an ESP32 firmware image.");
            return;
        }

        const esp_partition_t* running = esp_ota_get_running_partition();
        otaAppPartition = esp_ota_get_next_update_partition(nullptr);
        if (otaAppPartition == nullptr || otaAppPartition == running) {
            otaFail("This board has only one firmware slot, so it can't update itself. "
                    "Use the USB web flasher (Firmware Update) instead.");
            return;
        }

        const esp_err_t err = esp_ota_begin(otaAppPartition, OTA_SIZE_UNKNOWN, &otaHandle);
        if (err != ESP_OK) {
            otaFail(String("Could not start the update (") + esp_err_to_name(err) + ").");
            return;
        }
        otaFlashActive = true;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "OTA",
                   "Writing %s to %s (running from %s)", filename.c_str(),
                   otaAppPartition->label, running ? running->label : "?");
        LogBuffer::pushf(LogBuffer::TYPE_INFO, "OTA: writing %s to %s", filename.c_str(), otaAppPartition->label);
    }

    // Finish firmware.bin: verify the written image and make it the boot slot.
    static void finishFirmwareUpdate() {
        otaFlashActive = false;
        esp_err_t err = esp_ota_end(otaHandle);   // validates the image (header, segments, checksum, chip)
        if (err != ESP_OK) {
            otaFail(err == ESP_ERR_OTA_VALIDATE_FAILED
                        ? String("The uploaded file failed image validation (wrong board/chip or corrupted download).")
                        : String("Could not finish the update (") + esp_err_to_name(err) + ").");
            return;
        }
        err = esp_ota_set_boot_partition(otaAppPartition);
        if (err != ESP_OK) {
            otaFail(String("Could not select the new firmware for boot (") + esp_err_to_name(err) + ").");
            return;
        }
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "OTA",
                   "Update OK: %u bytes written to %s, rebooting", (unsigned)otaBytesWritten, otaAppPartition->label);
        LogBuffer::pushf(LogBuffer::TYPE_INFO, "OTA: update OK (%u bytes), rebooting", (unsigned)otaBytesWritten);
    }

    // Streams the uploaded file. "spiffs.bin" updates the filesystem image;
    // anything else is treated as application firmware (see the block comment above).
    // On success the device reboots via scheduleRestart(); on failure the error
    // text is returned in the response body and shown by the web UI.
    void handleOtaUploadBody(AsyncWebServerRequest *request, const String &filename,
                             size_t index, uint8_t *data, size_t len, bool final) {
        const bool isSpiffs = (filename == "spiffs.bin");

        if (index == 0) {
            otaError        = false;
            otaErrorMessage = "";
            otaBytesWritten = 0;
            if (otaFlashActive) {                 // a previous upload was cut off mid-stream
                esp_ota_abort(otaHandle);
                otaFlashActive = false;
            }
            if (isSpiffs) {
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
                    otaFail(String("Could not start the filesystem update (") + Update.errorString() + ").");
                }
            } else {
                beginFirmwareUpdate(filename, data, len);
            }
        }

        if (isSpiffs) {
            if (!otaError && Update.isRunning() && Update.write(data, len) != len) {
                otaFail(String("Filesystem write failed (") + Update.errorString() + ").");
            }
            if (final && !otaError && !Update.end(true)) {
                otaFail(String("Filesystem update failed (") + Update.errorString() + ").");
            }
            return;
        }

        if (otaFlashActive && len > 0) {
            const esp_err_t err = esp_ota_write(otaHandle, data, len);
            if (err != ESP_OK) {
                otaFail(err == ESP_ERR_INVALID_SIZE
                            ? String("The file is larger than the firmware slot.")
                            : String("Write failed (") + esp_err_to_name(err) + ").");
            } else {
                otaBytesWritten += len;
            }
        }
        if (final && otaFlashActive) finishFirmwareUpdate();
    }

    void handleOtaUploadResponse(AsyncWebServerRequest *request) {
        const bool ok = !otaError;
        AsyncWebServerResponse *response = request->beginResponse(
            200, "text/plain", ok ? String("OK") : otaErrorMessage);
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
        server.on("/wifi-sta-status.json", HTTP_GET, handleWifiStaStatus);
        server.on("/wifi-scan.json", HTTP_GET, handleWifiScan);
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