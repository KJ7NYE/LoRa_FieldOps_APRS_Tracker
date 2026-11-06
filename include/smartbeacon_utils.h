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

#ifndef SMARTBEACON_UTILS_H_
#define SMARTBEACON_UTILS_H_

#include <Arduino.h>


// Field order matters: every brace initializer of this struct (the built-in
// profile table in smartbeacon_utils.cpp) must list all seven values, since an
// omitted trailing field silently becomes 0.
struct SmartBeaconValues {
    int     slowRate;       // s     — interval at or below slowSpeed
    int     slowSpeed;      // km/h  — below this, slowRate applies and corner pegging is disarmed
    int     fastRate;       // s     — interval at or above fastSpeed
    int     fastSpeed;      // km/h
    int     turnMinDeg;     // deg   — base heading change for a corner beacon
    int     turnSlope;      // deg·km/h — added as turnSlope / speed (larger turn needed when slow)
    int     turnMinTime;    // s     — minimum spacing before a corner beacon may follow any beacon
};

static constexpr byte SMARTBEACON_PROFILE_COUNT = 5;
static constexpr byte SMARTBEACON_CUSTOM_INDEX  = 4;

// Describes one user-editable field of the Custom profile. This single table
// drives JSON load/save, the serial CLI, the web UI POST handler and factory
// defaults, so every configuration pathway shares the same keys, ranges and
// defaults. The ranges are mirrored as min/max in index.html,
// serial_config.html and the LoRa FieldOps Config app — keep them in sync.
struct SmartBeaconField {
    const char*                 jsonKey;        // key under "customSmartBeacon" in tracker_conf.json / web form
    const char*                 cliName;        // `smartcustom <cliName> <n>`
    int SmartBeaconValues::*    member;
    int                         minValue;
    int                         maxValue;
    int                         defaultValue;   // Custom profile default (older configs, factory reset)
};

static constexpr byte SMARTBEACON_CUSTOM_FIELD_COUNT = 7;
extern const SmartBeaconField SMARTBEACON_CUSTOM_FIELDS[SMARTBEACON_CUSTOM_FIELD_COUNT];

namespace SMARTBEACON_Utils {

    void checkSettings(byte index);
    void checkInterval(int speed);
    void checkFixedBeaconTime();
    void checkState();
    void setCustomValues(const SmartBeaconValues& v);
    const char* profileLabel(byte index);

    // Custom profile populated entirely from SMARTBEACON_CUSTOM_FIELDS defaults.
    SmartBeaconValues customDefaults();

    // Lookup by CLI subcommand name; nullptr if unknown.
    const SmartBeaconField* findCustomFieldByCliName(const String& cliName);

    // Clamp every field of v into its valid range, logging each correction.
    // Returns true if any value was changed (caller should persist the fix).
    bool clampCustomValues(SmartBeaconValues& v);

}

#endif