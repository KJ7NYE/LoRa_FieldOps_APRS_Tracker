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

#include <Arduino.h>
#include "configuration.h"
#include "battery_utils.h"
#include "board_pinout.h"
#include "power_utils.h"
#include "display.h"



#ifdef HAS_AXP192
    extern XPowersAXP192 PMU;
#endif
#ifdef HAS_AXP2101
    extern XPowersAXP2101 PMU;
#endif

extern      Configuration           Config;
uint32_t    batteryMeasurmentTime   = 0;
int         averageReadings         = 20;

String      batteryVoltage          = "";
bool        batteryConnected      = false;

extern      String                  batteryChargeDischargeCurrent;



namespace BATTERY_Utils {

    // Number of Li-ion cells in series in the board's pack. The state-of-charge
    // curve below is per cell, so pack voltage is divided by this first.
    #ifdef TTGO_T_BEAM_1W
        static const float kCellsInSeries = 2.0f;   // 2S pack
    #else
        static const float kCellsInSeries = 1.0f;
    #endif

    // Resting-voltage → state-of-charge curve for a single Li-ion / LiPo cell.
    // Li-ion discharge is strongly non-linear: the top ~20% of capacity is
    // spent between 4.20 V and 4.00 V, then the curve flattens across
    // 3.9–3.7 V and falls off a cliff below ~3.6 V. A straight-line map over
    // a narrow window pins the reading at 100% for a large part of the run
    // (the previous 3.4–4.0 V linear map reported 100% for everything above
    // 4.00 V). The 0% point is 3.40 V rather than the cell's ~3.0 V absolute
    // cutoff because the radio rail browns out well before then, so this is
    // "usable" capacity. Entries must be in descending voltage order.
    struct CellSocPoint { float volts; float percent; };
    static const CellSocPoint kCellSocCurve[] = {
        { 4.20f, 100.0f },
        { 4.10f,  90.0f },
        { 4.00f,  79.0f },
        { 3.92f,  68.0f },
        { 3.86f,  58.0f },
        { 3.82f,  48.0f },
        { 3.79f,  39.0f },
        { 3.76f,  29.0f },
        { 3.73f,  19.0f },
        { 3.69f,  10.0f },
        { 3.60f,   4.0f },
        { 3.40f,   0.0f },
    };
    static const int kCellSocCurveLength = sizeof(kCellSocCurve) / sizeof(kCellSocCurve[0]);

    // Linear interpolation between the two curve points bracketing cellVolts.
    static int cellVoltageToPercent(float cellVolts) {
        if (cellVolts >= kCellSocCurve[0].volts) return 100;
        for (int i = 1; i < kCellSocCurveLength; i++) {
            const CellSocPoint& upper = kCellSocCurve[i - 1];
            const CellSocPoint& lower = kCellSocCurve[i];
            if (cellVolts >= lower.volts) {
                float fraction = (cellVolts - lower.volts) / (upper.volts - lower.volts);
                return (int)(lower.percent + fraction * (upper.percent - lower.percent) + 0.5f);
            }
        }
        return 0;
    }

    String getPercentVoltageBattery(float voltage) {
        int percent = cellVoltageToPercent(voltage / kCellsInSeries);
        return (percent < 100) ? (((percent < 10) ? "  ": " ") + String(percent)) : "100";
    }

    String getBatteryInfoVoltage() {
        return batteryVoltage;
    }

    int getBatteryPercent() {
        if (!batteryConnected) return -1;
        return getPercentVoltageBattery(batteryVoltage.toFloat()).toInt();
    }

    // ── Battery divider ratio (VBAT / ADC-pin voltage) ───────────────────
    // Taken from each board's schematic resistor values. With calibrated
    // millivolt reads (see readBatteryVoltage) no empirical offsets are
    // needed; residual per-unit error is trimmed with kBatteryCalibration.
    #if defined(TTGO_T_BEAM_1W)
        static const float kBatteryDividerRatio = 3.0f;     // 300k + 150k
    #elif defined(HELTEC_T114)
        static const float kBatteryDividerRatio = 4.916f;   // per Heltec/Meshtastic T114 variant
    #elif defined(HELTEC_V3_433_APRS) || defined(HELTEC_WIRELESS_TRACKER) || defined(HELTEC_V3_GPS) || defined(HELTEC_V3_TNC) || defined(HELTEC_V3_2_GPS) || defined(HELTEC_V3_2_TNC) || defined(HELTEC_WSL_V3_GPS_DISPLAY) || defined(ESP32_C3_DIY_LoRa_GPS) || defined(ESP32_C3_DIY_LoRa_GPS_915) || defined(WEMOS_ESP32_Bat_LoRa_GPS)
        static const float kBatteryDividerRatio = 4.9f;     // 390k + 100k
    #elif defined(HELTEC_V2_GPS) || defined(HELTEC_V2_GPS_915) || defined(HELTEC_V2_TNC) || defined(F4GOH_1W_LoRa_Tracker) || defined(F4GOH_1W_LoRa_Tracker_LLCC68)
        static const float kBatteryDividerRatio = 3.2f;     // 220k + 100k
    #elif defined(LIGHTTRACKER_PLUS_1_0)
        static const float kBatteryDividerRatio = 6.6f;     // 560k + 100k
    #else
        // LoRanger V1, LilyGo T3 / T3-S3 and the upstream DIY/T-Deck family.
        static const float kBatteryDividerRatio = 2.0f;     // 100k + 100k
    #endif

    // Multiplicative trim for divider-resistor tolerance (1% parts can be off
    // by up to ~2% combined). Leave at 1.0 unless a multimeter across the
    // cell disagrees with `bat read`; then set to (meter V / reported V).
    static const float kBatteryCalibration = 1.0f;

    float readBatteryVoltage() {
        #if defined(HAS_AXP192) || defined(HAS_AXP2101)
            return (PMU.getBattVoltage() / 1000.0);
        #elif defined(BATTERY_PIN)
            // Enable the divider (on boards that gate it to avoid a constant
            // drain) and let the node settle before sampling.
            #ifdef ADC_CTRL_PIN
                pinMode(ADC_CTRL_PIN, OUTPUT);
                digitalWrite(ADC_CTRL_PIN, ADC_CTRL_ENABLED);
                delay(2);
            #elif defined(ADC_CTRL)
                POWER_Utils::adc_ctrl_ON();   // handles board-specific polarity
                delay(50);
            #endif

            #ifdef ARDUINO_ARCH_NRF52
                // nRF52 SAADC is linear enough to use raw counts. Scale matches
                // POWER_Utils::setup's analogReference(AR_INTERNAL_3_0) +
                // analogReadResolution(12).
                uint32_t rawSum = 0;
                analogRead(BATTERY_PIN);    // dummy read to settle the S/H cap
                delay(1);
                for (int i = 0; i < averageReadings; i++) {
                    rawSum += analogRead(BATTERY_PIN);
                    delay(3);
                }
                float pinVolts = ((float)rawSum / averageReadings) * 3.0f / 4095.0f;
            #else
                // ESP32 raw ADC counts are offset and non-linear (and the
                // S3's full scale at 12 dB is ~3.1 V, not 3.3 V), which is
                // what the old hand-fitted "+0.1 V / +6.5 % / +0.285 V"
                // corrections were chasing. analogReadMilliVolts applies the
                // chip's factory eFuse calibration curve instead.
                uint32_t mvSum = 0;
                analogReadMilliVolts(BATTERY_PIN);  // dummy read to settle ADC
                delay(1);
                for (int i = 0; i < averageReadings; i++) {
                    mvSum += analogReadMilliVolts(BATTERY_PIN);
                    delay(3);
                }
                float pinVolts = ((float)mvSum / averageReadings) / 1000.0f;
            #endif

            #ifdef ADC_CTRL_PIN
                digitalWrite(ADC_CTRL_PIN, !ADC_CTRL_ENABLED);
            #elif defined(ADC_CTRL)
                POWER_Utils::adc_ctrl_OFF();
            #endif

            return pinVolts * kBatteryDividerRatio * kBatteryCalibration;
        #else
            return 0.0;
        #endif
    }

    void obtainBatteryInfo() {
        #if defined(HAS_AXP192) || defined(HAS_AXP2101)
            batteryConnected = PMU.isBatteryConnect();
            if (batteryConnected) {
                batteryVoltage                  = String(readBatteryVoltage(), 2);
                batteryChargeDischargeCurrent   = String(POWER_Utils::getBatteryChargeDischargeCurrent(), 0);
            }
        #else
            batteryVoltage = String(readBatteryVoltage(), 2);
            if (batteryVoltage.toFloat() > 1.5) batteryConnected = true;
        #endif
    }

    void monitor() {
        #if defined(HAS_AXP192) || defined(HAS_AXP2101)
            if (batteryMeasurmentTime == 0 || (millis() - batteryMeasurmentTime) > 60 * 1000){
                obtainBatteryInfo();
                POWER_Utils::handleChargingLed();
                batteryMeasurmentTime = millis();
            }
        #elif defined(BATTERY_PIN)
            if (batteryMeasurmentTime == 0 || (millis() - batteryMeasurmentTime) > 60 * 1000){ //At least 60 seconds have to pass between measurements
                obtainBatteryInfo();
                #ifdef ADC_CTRL
                    // Only shut down if a battery is actually present.
                    // Without a battery the ADC reads ~0 V through the
                    // divider; batteryConnected is false in that case.
                    if (batteryConnected &&
                        batteryVoltage.toFloat() < (Config.battery.sleepVoltage - 0.1)) {
                        displayStatus("!BATTERY!", "",
                                      "LOW VOLTAGE",
                                      batteryVoltage + "V",
                                      "Shutting down...", "", "");
                        POWER_Utils::shutdown();
                    }
                #endif
                batteryMeasurmentTime = millis();
            }
        #endif
    }

}
