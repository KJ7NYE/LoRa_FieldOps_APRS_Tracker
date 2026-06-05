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

#include "smartbeacon_utils.h"
#include "configuration.h"

extern Configuration    Config;
extern Beacon           *currentBeacon;
extern uint32_t         txInterval;
extern uint32_t         lastTxTime;
extern bool             sendUpdate;

// SmartBeacon runtime state — owned here, read via extern in station_utils.cpp.
bool                smartBeaconActive       = false;
SmartBeaconValues   currentSmartBeaconValues;
byte                smartBeaconSettingsIndex    = 10;
bool                wxRequestStatus             = false;
uint32_t            wxRequestTime               = 0;


SmartBeaconValues   smartBeaconSettings[SMARTBEACON_PROFILE_COUNT] = {
    {120,  3, 60, 15,  50, 20, 12, 60},     // Runner settings  = SLOW
    {120,  5, 60, 40, 100, 12, 12, 60},     // Bike settings    = MEDIUM
    {120, 10, 10, 110, 100, 10, 10, 80},     // Car settings     = FAST
    {120,  5, 60, 40, 100, 12, 12, 60}      // Custom slot      = filled from Config.customSmartBeacon at load
};

static const char* SMARTBEACON_PROFILE_LABELS[SMARTBEACON_PROFILE_COUNT] = {
    "Runner", "Bike", "Car", "Custom"
};


namespace SMARTBEACON_Utils {

    void checkSettings(byte index) {
        if (index >= SMARTBEACON_PROFILE_COUNT) {
            Serial.printf("warn: smartBeaconSetting %u out of range, clamping to 0\n", index);
            index = 0;
        }
        if (smartBeaconSettingsIndex != index) {
            currentSmartBeaconValues = smartBeaconSettings[index];
            smartBeaconSettingsIndex = index;
        }
    }

    void setCustomValues(const SmartBeaconValues& v) {
        smartBeaconSettings[SMARTBEACON_CUSTOM_INDEX] = v;
        if (smartBeaconSettingsIndex == SMARTBEACON_CUSTOM_INDEX) {
            smartBeaconSettingsIndex = 255;             // force re-copy on next checkSettings
            checkSettings(SMARTBEACON_CUSTOM_INDEX);
        }
    }

    const char* profileLabel(byte index) {
        if (index >= SMARTBEACON_PROFILE_COUNT) return "?";
        return SMARTBEACON_PROFILE_LABELS[index];
    }

    void checkInterval(int speed) {
        if (smartBeaconActive) {
            if (speed < currentSmartBeaconValues.slowSpeed) {
                txInterval = currentSmartBeaconValues.slowRate * 1000;
            } else if (speed > currentSmartBeaconValues.fastSpeed) {
                txInterval = currentSmartBeaconValues.fastRate * 1000;
            } else {
                txInterval = min(currentSmartBeaconValues.slowRate, currentSmartBeaconValues.fastSpeed * currentSmartBeaconValues.fastRate / speed) * 1000;
            }
        }
    }

    void checkFixedBeaconTime() {
        if (!smartBeaconActive) {
            uint32_t lastTxSmartBeacon = millis() - lastTxTime;
            if (lastTxSmartBeacon >= Config.nonSmartBeaconRate * 60 * 1000) sendUpdate = true;
        }
    }

    void checkState() {
        if (wxRequestStatus && (millis() - wxRequestTime) > 20000) wxRequestStatus = false;
        smartBeaconActive = !wxRequestStatus ? currentBeacon->smartBeaconActive : false;
    }

}