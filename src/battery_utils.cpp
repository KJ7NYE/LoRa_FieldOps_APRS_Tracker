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

float       lora32BatReadingCorr    = 6.5; // % of correction to higher value to reflect the real battery voltage (adjust this to your needs)


namespace BATTERY_Utils {

    String getPercentVoltageBattery(float voltage) {
        #ifdef TTGO_T_BEAM_1W
            // 2S Li-ion pack
            int percent = ((voltage - 6.8f) / (8.0f - 6.8f)) * 100;
        #else
            int percent = ((voltage - 3.4) / (4.0 - 3.4)) * 100;
        #endif
        if (percent < 0)   percent = 0;
        if (percent > 100) percent = 100;
        return (percent < 100) ? (((percent < 10) ? "  ": " ") + String(percent)) : "100";
    }

    String getBatteryInfoVoltage() {
        return batteryVoltage;
    }

    float readBatteryVoltage() {
        #if defined(HAS_AXP192) || defined(HAS_AXP2101)
            return (PMU.getBattVoltage() / 1000.0);
        #else
            #ifdef BATTERY_PIN
                #if defined(TTGO_T_BEAM_1W)
                    // analogReadMilliVolts uses ESP32-S3 eFuse calibration tables,
                    // avoiding the ~0.6 V raw-ADC offset that makes 0 V VBAT read as
                    // ~2.2 V and display "B:-66%" when running on USB with no battery.
                    // 300 kΩ + 150 kΩ divider: VBAT = ADC_mV × (300+150)/150 = ADC_mV × 3.
                    uint32_t mvSum = 0;
                    analogReadMilliVolts(BATTERY_PIN);  // dummy read to settle ADC
                    delay(1);
                    for (int i = 0; i < averageReadings; i++) {
                        mvSum += analogReadMilliVolts(BATTERY_PIN);
                        delay(3);
                    }
                    return (float)(mvSum / averageReadings) / 1000.0f * 3.0f;
                #else
                #ifdef ADC_CTRL_PIN
                    // T114-style boards: a discrete FET gates the battery
                    // divider so it doesn't bleed current continuously.
                    // Drive ADC_CTRL_PIN to the enable level only while
                    // sampling, then back off.
                    pinMode(ADC_CTRL_PIN, OUTPUT);
                    digitalWrite(ADC_CTRL_PIN, ADC_CTRL_ENABLED);
                    delay(2);
                #elif defined(ADC_CTRL)
                    // All other ADC_CTRL boards (Heltec V3 family etc.):
                    // enable the divider FET, wait for the node to settle,
                    // then read.  adc_ctrl_ON/OFF handle board-specific
                    // polarity (HIGH or LOW) so this site stays generic.
                    POWER_Utils::adc_ctrl_ON();
                    delay(50);
                #endif
                int sampleSum = 0;
                analogRead(BATTERY_PIN);    // Dummy Read
                delay(1);
                for (int i = 0; i < averageReadings; i++) {
                    sampleSum += analogRead(BATTERY_PIN);
                    delay(3);
                }
                int adc_value = sampleSum/averageReadings;
                #ifdef ADC_CTRL_PIN
                    digitalWrite(ADC_CTRL_PIN, !ADC_CTRL_ENABLED);
                #elif defined(ADC_CTRL)
                    POWER_Utils::adc_ctrl_OFF();
                #endif
                #ifdef ARDUINO_ARCH_NRF52
                    // Matches POWER_Utils::setup's analogReference(AR_INTERNAL_3_0)
                    // + analogReadResolution(12). Without those, this formula
                    // would be off by ~5x.
                    double voltage = (adc_value * 3.0) / 4095.0;
                #else
                    double voltage = (adc_value * 3.3) / 4095.0;
                #endif

                #ifdef LIGHTTRACKER_PLUS_1_0
                    double inputDivider = (1.0 / (560.0 + 100.0)) * 100.0;  // The voltage divider is a 560k + 100k resistor in series, 100k on the low side.
                    return ((voltage / inputDivider) * 1.11029) + 0.14431;
                #endif
                #if defined(TTGO_T_Beam_V0_7) || defined(TTGO_T_LORA32_V2_1_GPS) || defined(TTGO_T_LORA32_V2_1_GPS_915) || defined(TTGO_T_LORA32_V2_1_TNC) || defined(TTGO_T_LORA32_V2_1_TNC_915) || defined(ESP32_DIY_LoRa_GPS) || defined(ESP32_DIY_LoRa_GPS_915) || defined(ESP32_DIY_1W_LoRa_GPS) || defined(ESP32_DIY_1W_LoRa_GPS_915) || defined(ESP32_DIY_1W_LoRa_GPS_LLCC68) || defined(OE5HWN_MeshCom) || defined(TTGO_T_DECK_GPS) || defined(TTGO_T_DECK_PLUS) || defined(ESP32S3_DIY_LoRa_GPS) || defined(ESP32S3_DIY_LoRa_GPS_915) || defined(TROY_LoRa_APRS) || defined(RPC_Electronics_1W_LoRa_GPS) || defined(TTGO_LORA32_T3S3_V1_2_GPS) || defined(LORANGER_V1) || defined(LILYGO_T3_433_APRS)
                    return (2 * (voltage + 0.1)) * (1 + (lora32BatReadingCorr/100)); // (2 x 100k voltage divider) 2 x voltage divider/+0.1 because ESP32 nonlinearity ~100mV ADC offset/extra correction
                #endif
                #if defined(HELTEC_V3_GPS) || defined(HELTEC_V3_TNC) || defined(HELTEC_V3_2_GPS) || defined(HELTEC_V3_2_TNC) || defined(HELTEC_WIRELESS_TRACKER) || defined(HELTEC_WSL_V3_GPS_DISPLAY) || defined(ESP32_C3_DIY_LoRa_GPS) || defined(ESP32_C3_DIY_LoRa_GPS_915) || defined(WEMOS_ESP32_Bat_LoRa_GPS) || defined(HELTEC_V3_433_APRS)
                    double inputDivider = (1.0 / (390.0 + 100.0)) * 100.0;  // The voltage divider is a 390k + 100k resistor in series, 100k on the low side.
                    return (voltage / inputDivider) + 0.285; // Yes, this offset is excessive, but the ADC on the ESP32s3 is quite inaccurate and noisy. Adjust to own measurements.
                #endif
                #if defined(HELTEC_V2_GPS) || defined(HELTEC_V2_GPS_915) || defined(HELTEC_V2_TNC) || defined(F4GOH_1W_LoRa_Tracker) || defined(F4GOH_1W_LoRa_Tracker_LLCC68)
                    double inputDivider = (1.0 / (220.0 + 100.0)) * 100.0;  // The voltage divider is a 220k + 100k resistor in series, 100k on the low side.
                    return (voltage / inputDivider) + 0.285; // Yes, this offset is excessive, but the ADC on the ESP32 is quite inaccurate and noisy. Adjust to own measurements.
                #endif
                #ifdef HELTEC_T114
                    // T114 hardware divider multiplier per meshtastic variant.h is 4.916.
                    // ADC_CTRL_PIN gating (HIGH-before-read) not yet wired — readings will be
                    // approximate until that lands. Tune via lora32BatReadingCorr config.
                    return (voltage * 4.916F) * (1 + (lora32BatReadingCorr/100));
                #endif
                #endif  // !TTGO_T_BEAM_1W
            #else
                return 0.0;
            #endif
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
