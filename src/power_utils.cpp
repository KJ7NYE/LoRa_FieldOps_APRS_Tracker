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

#include <SPI.h>

#include "configuration.h"
#include "battery_utils.h"
#include "board_pinout.h"
#include "power_utils.h"
#include "lora_utils.h"
#if defined(HAS_NIMBLE) || defined(ARDUINO_ARCH_NRF52)
#include "ble_utils.h"
#endif
#include "gps_utils.h"
#include "display.h"
#include "logger.h"


#if !defined(TTGO_T_Beam_S3_SUPREME_V3) && !defined(HELTEC_WIRELESS_TRACKER)
    #define I2C_SDA 21
    #define I2C_SCL 22
    #define IRQ_PIN 35
#endif

#ifdef TTGO_T_Beam_S3_SUPREME_V3
    #define I2C0_SDA 17
    #define I2C0_SCL 18
    #define I2C1_SDA 42
    #define I2C1_SCL 41
    #define IRQ_PIN  40
#endif

#ifdef HAS_AXP192
    XPowersAXP192 PMU;
#endif
#ifdef HAS_AXP2101
    XPowersAXP2101 PMU;
#endif

extern  Configuration                   Config;
extern  logging::Logger                 logger;
extern  bool                            transmitFlag;
extern  bool                            gpsIsActive;

bool    pmuInterrupt;
bool    disableGPS;

String  batteryChargeDischargeCurrent    = "";


namespace POWER_Utils {

    #ifdef VEXT_CTRL
        void vext_ctrl_ON() {
            // Heltec V3 family (incl. our 433-APRS variant): VEXT HIGH = peripherals ON
            #if defined(HELTEC_V3_GPS) || defined(HELTEC_V3_TNC) || \
                defined(HELTEC_WIRELESS_TRACKER) || defined(HELTEC_WSL_V3_GPS_DISPLAY) || \
                defined(HELTEC_V3_433_APRS)
                digitalWrite(VEXT_CTRL, HIGH);
            #endif
            // Heltec V3.2 family: VEXT LOW = peripherals ON (inverted polarity)
            #if defined(HELTEC_V3_2_GPS) || defined(HELTEC_V3_2_TNC)
                digitalWrite(VEXT_CTRL, LOW);
            #endif
        }

        void vext_ctrl_OFF() {
            #if defined(HELTEC_V3_GPS) || defined(HELTEC_V3_TNC) || \
                defined(HELTEC_WIRELESS_TRACKER) || defined(HELTEC_WSL_V3_GPS_DISPLAY) || \
                defined(HELTEC_V3_433_APRS)
                digitalWrite(VEXT_CTRL, LOW);
            #endif
            #if defined(HELTEC_V3_2_GPS) || defined(HELTEC_V3_2_TNC)
                digitalWrite(VEXT_CTRL, HIGH);
            #endif
        }
    #endif


    #ifdef ADC_CTRL
        void adc_ctrl_ON() {
            // HIGH enables divider: WIRELESS_TRACKER, V3.2 family, V3_433_APRS
            // (NMOS switch on GND side of divider — gate HIGH = transistor ON = divider connected)
            #if defined(HELTEC_WIRELESS_TRACKER) || defined(HELTEC_V3_2_GPS) || \
                defined(HELTEC_V3_2_TNC)         || defined(HELTEC_V3_433_APRS)
                digitalWrite(ADC_CTRL, HIGH);
            #endif
            // LOW enables divider: original V3 / V2 family
            // (PMOS switch on VBAT side — gate LOW = transistor ON = divider connected)
            #if defined(HELTEC_V3_GPS) || defined(HELTEC_V3_TNC) || \
                defined(HELTEC_V2_GPS) || defined(HELTEC_V2_GPS_915) || defined(HELTEC_V2_TNC) || \
                defined(HELTEC_WSL_V3_GPS_DISPLAY)
                digitalWrite(ADC_CTRL, LOW);
            #endif
        }

        void adc_ctrl_OFF() {
            // LOW disables: boards that use HIGH to enable
            #if defined(HELTEC_WIRELESS_TRACKER) || defined(HELTEC_V3_2_GPS) || \
                defined(HELTEC_V3_2_TNC)         || defined(HELTEC_V3_433_APRS)
                digitalWrite(ADC_CTRL, LOW);
            #endif
            // HIGH disables: boards that use LOW to enable
            #if defined(HELTEC_V3_GPS) || defined(HELTEC_V3_TNC) || \
                defined(HELTEC_V2_GPS) || defined(HELTEC_V2_GPS_915) || defined(HELTEC_V2_TNC) || \
                defined(HELTEC_WSL_V3_GPS_DISPLAY)
                digitalWrite(ADC_CTRL, HIGH);
            #endif
        }
    #endif

    #if defined(HAS_AXP192) || defined(HAS_AXP2101)
        void activateMeasurement() {
                PMU.disableTSPinMeasure();
                PMU.enableBattDetection();
                PMU.enableVbusVoltageMeasure();
                PMU.enableBattVoltageMeasure();
                PMU.enableSystemVoltageMeasure();
        }

        void enableChgLed() {
            PMU.setChargingLedMode(XPOWERS_CHG_LED_ON);
        }

        void disableChgLed() {
            PMU.setChargingLedMode(XPOWERS_CHG_LED_OFF);
        }

        void handleChargingLed() {
            if (isCharging()) {
                enableChgLed();
            } else {
                disableChgLed();
            }
        }

        String getBatteryInfoCurrent() {
            return batteryChargeDischargeCurrent;
        }

        float getBatteryChargeDischargeCurrent() {
            #ifdef HAS_AXP192
                if (PMU.isCharging()) {
                    return PMU.getBatteryChargeCurrent();
                }
                return -1.0 * PMU.getBattDischargeCurrent();
            #endif
            #ifdef HAS_AXP2101
                return PMU.getBatteryPercent();
            #endif
        }
    #endif

    bool isCharging() {
        #if defined(HAS_AXP192) || defined(HAS_AXP2101)
            return PMU.isCharging();
        #else
            return 0;
        #endif
    }

    void activateGPS() {
        #ifdef HAS_AXP192
            PMU.setLDO3Voltage(3300);
            PMU.enableLDO3();
        #endif

        #ifdef HAS_AXP2101
            #ifdef TTGO_T_Beam_S3_SUPREME_V3
                PMU.setALDO4Voltage(3300);
                PMU.enableALDO4();
            #else
                PMU.setALDO3Voltage(3300);
                PMU.enableALDO3();
            #endif
        #endif
        #ifdef HELTEC_WIRELESS_TRACKER
            vext_ctrl_ON();
        #endif
        gpsIsActive = true;
    }

    void deactivateGPS() {
        #ifdef HAS_AXP192
            PMU.disableLDO3();
        #endif

        #ifdef HAS_AXP2101
            #ifdef TTGO_T_Beam_S3_SUPREME_V3
                PMU.disableALDO4();
            #else
                PMU.disableALDO3();
            #endif
        #endif
        #ifdef HELTEC_WIRELESS_TRACKER
            vext_ctrl_OFF();
        #endif
        gpsIsActive = false;
    }

    void activateLoRa() {
        #ifdef HAS_AXP192
            PMU.setLDO2Voltage(3300);
            PMU.enableLDO2();
        #endif

        #ifdef HAS_AXP2101
            #ifdef TTGO_T_Beam_S3_SUPREME_V3
                PMU.setALDO3Voltage(3300);
                PMU.enableALDO3();
            #else
                PMU.setALDO2Voltage(3300);
                PMU.enableALDO2();
            #endif
        #endif
    }

    void deactivateLoRa() {
        #ifdef HAS_AXP192
            PMU.disableLDO2();
        #endif

        #ifdef HAS_AXP2101
            #ifdef TTGO_T_Beam_S3_SUPREME_V3
                PMU.disableALDO3();
            #else
                PMU.disableALDO2();
            #endif
        #endif
    }

    void externalPinSetup() {
        if (Config.ptt.active && Config.ptt.io_pin >= 0) {
            pinMode(Config.ptt.io_pin, OUTPUT);
            digitalWrite(Config.ptt.io_pin, Config.ptt.reverse ? HIGH : LOW);
        } else if (Config.ptt.active && Config.ptt.io_pin < 0) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "PINOUT", "PTT Pin not defined");
            while (1);
        }
    }

    bool begin(TwoWire &port) {
        #if !defined(HAS_AXP192) && !defined(HAS_AXP2101)
            return true; // no powerManagment chip for this boards (only a few measure battery voltage).
        #endif

        #ifdef HAS_AXP192
            bool result = PMU.begin(Wire, AXP192_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
            if (result) {
                PMU.disableDC2();
                PMU.disableLDO2();
                PMU.disableLDO3();
                PMU.setDC1Voltage(3300);
                PMU.enableDC1();
                PMU.setProtectedChannel(XPOWERS_DCDC3);
                PMU.disableIRQ(XPOWERS_AXP192_ALL_IRQ);
            }
            return result;
        #endif

        #ifdef HAS_AXP2101
            #ifdef TTGO_T_Beam_S3_SUPREME_V3
                bool result = PMU.begin(Wire1, AXP2101_SLAVE_ADDRESS, I2C1_SDA, I2C1_SCL);
            #else
                bool result = PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
            #endif
            if (result) {
                PMU.disableDC2();
                PMU.disableDC3();
                PMU.disableDC4();
                PMU.disableDC5();
                #ifndef TTGO_T_Beam_S3_SUPREME_V3
                    PMU.disableALDO1();
                    PMU.disableALDO4();
                #endif
                PMU.disableBLDO1();
                PMU.disableBLDO2();
                PMU.disableDLDO1();
                PMU.disableDLDO2();
                PMU.setDC1Voltage(3300);
                PMU.enableDC1();
                #ifdef TTGO_T_Beam_S3_SUPREME_V3
                    PMU.setALDO1Voltage(3300);
                #endif
                PMU.setButtonBatteryChargeVoltage(3300);
                PMU.enableButtonBatteryCharge();
                PMU.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
            }
            return result;
        #endif
    }

    void setup() {
        #ifdef ARDUINO_ARCH_NRF52
            // Configure nRF52 ADC to match the (adc_value * 3.0) / 4095.0
            // formula used in BATTERY_Utils::readBatteryVoltage's nRF branch.
            // BSP default is 0.6V internal reference at 10-bit, which would
            // make analogRead values ~5x smaller than the formula expects
            // and trip the low-battery shutdown on any boot.
            analogReference(AR_INTERNAL_3_0);
            analogReadResolution(12);
        #endif

        #ifdef HAS_NO_GPS
            disableGPS = true;
        #else
            // GPS_NONE means "no position source" — treat same as no GPS hardware.
            disableGPS = (Config.gpsSource == GPS_NONE);
        #endif

        #ifdef HAS_AXP192
            Wire.begin(SDA, SCL);
            if (begin(Wire)) {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "AXP192", "init done!");
            } else {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "AXP192", "init failed!");
            }
            activateLoRa();
            if (disableGPS) {
                deactivateGPS();
            } else {
                activateGPS();
            }
            activateMeasurement();
            PMU.setChargerTerminationCurr(XPOWERS_AXP192_CHG_ITERM_LESS_10_PERCENT);
            PMU.setChargeTargetVoltage(XPOWERS_AXP192_CHG_VOL_4V2);
            PMU.setChargerConstantCurr(XPOWERS_AXP192_CHG_CUR_780MA);
            PMU.setSysPowerDownVoltage(2600);
        #endif

        #ifdef HAS_AXP2101
            bool beginStatus = false;
            #ifdef TTGO_T_Beam_S3_SUPREME_V3
                Wire1.begin(I2C1_SDA, I2C1_SCL);
                Wire.begin(I2C0_SDA, I2C0_SCL);
                if (begin(Wire1)) beginStatus = true;
            #else
                #ifdef ARDUINO_ARCH_NRF52
                    Wire.begin();
                #else
                    Wire.begin(SDA, SCL);
                #endif
                if (begin(Wire)) beginStatus = true;
            #endif
            if (beginStatus) {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "AXP2101", "init done!");
            } else {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "AXP2101", "init failed!");
            }
            activateLoRa();
            if (disableGPS) {
                deactivateGPS();
            } else {
                activateGPS();
            }
            activateMeasurement();
            PMU.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_200MA);
            PMU.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);
            PMU.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
            PMU.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_800MA);
            PMU.setSysPowerDownVoltage(2600);
        #endif

        #ifdef BATTERY_PIN
            pinMode(BATTERY_PIN, INPUT);
            #ifndef ARDUINO_ARCH_NRF52
                analogSetPinAttenuation(BATTERY_PIN, ADC_11db);
            #endif
        #endif

        #ifdef VEXT_CTRL
            pinMode(VEXT_CTRL,OUTPUT);
            vext_ctrl_ON();
        #endif

        #ifdef ADC_CTRL
            pinMode(ADC_CTRL, OUTPUT);
            adc_ctrl_OFF();  // Divider disabled by default; monitor() enables per sample
        #endif

        #ifdef HELTEC_T114
            // VEXT_ENABLE (GPS_VCC, P0.21) gates the L76K GPS *and* the SX1262
            // power rail on this board. It used to be driven HIGH only inside
            // GPS_Utils::setup(), which early-returns when disableGPS=true —
            // leaving the radio unpowered and radio.begin() failing with -13
            // (SPI cmd timeout). Drive it HIGH at board-level so the rail is
            // live before any peripheral init, regardless of GPS config.
            pinMode(GPS_VCC, OUTPUT);
            digitalWrite(GPS_VCC, HIGH);
            delay(200);     // let the rail settle before SPI traffic
        #endif

        #ifdef HELTEC_WIRELESS_TRACKER
            Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
        #endif

        #if defined(HELTEC_V3_GPS) || defined(HELTEC_V3_TNC) || defined(HELTEC_V3_2_GPS) || defined(HELTEC_V3_2_TNC) || defined(HELTEC_WSL_V3_GPS_DISPLAY)
            Wire1.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
        #endif

        #if defined(TTGO_T_DECK_GPS) || defined(TTGO_T_DECK_PLUS)
            pinMode(BOARD_POWERON, OUTPUT);
            digitalWrite(BOARD_POWERON, HIGH);

            pinMode(BOARD_SDCARD_CS, OUTPUT);
            pinMode(RADIO_CS_PIN, OUTPUT);
            pinMode(TFT_CS, OUTPUT);

            digitalWrite(BOARD_SDCARD_CS, HIGH);
            digitalWrite(RADIO_CS_PIN, HIGH);
            digitalWrite(TFT_CS, HIGH);

            delay(500);
            Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
        #endif

        #if defined(TTGO_T_BEAM_1W)
            // Enable LoRa LDO (LDO_EN, pin 40) early so the SX1262 power rail is live
            // for the entire boot sequence before radio.begin() is called in LoRa_Utils::setup().
            // Zero settle time in lora_utils.cpp caused radio.begin() to return -13 (SPI cmd
            // timeout) → while(true) hang → WDT reset loop and blank display.
            // Same fix pattern as HELTEC_T114's GPS_VCC above.
            pinMode(RADIO_VCC_PIN, OUTPUT);
            digitalWrite(RADIO_VCC_PIN, HIGH);
            // FAN_CTRL_PIN is owned by THERMAL_Utils::setup() — do not touch it here.
        #endif
    }

    void lowerCpuFrequency() {
        #ifdef ARDUINO_ARCH_NRF52
            // nRF52840 runs at a fixed 64 MHz; no DVFS API. Skip.
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "Main", "CPU frequency scaling not supported on nRF52");
        #else
            if (setCpuFrequencyMhz(80)) {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "Main", "CPU frequency set to 80MHz");
            } else {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Main", "CPU frequency unchanged");
            }
        #endif
    }

    void shutdown() {
        delay(3000);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Main", "SHUTDOWN !!!");
        #if defined(HAS_AXP192) || defined(HAS_AXP2101)
            displayToggle(false);
            PMU.shutdown();
        #else
            #if defined(ARDUINO_ARCH_NRF52) || defined(HAS_NIMBLE)
                if (Config.bluetooth.active) {
                    BLE_Utils::stop();
                }
            #endif

            #ifdef VEXT_CTRL
                vext_ctrl_OFF();
            #endif

            #ifdef ADC_CTRL
                adc_ctrl_OFF();
            #endif

            #if defined(TTGO_T_DECK_GPS) || defined(TTGO_T_DECK_PLUS)
                digitalWrite(BOARD_POWERON, LOW);
            #endif

            #if defined(TTGO_T_BEAM_1W)
                digitalWrite(FAN_CTRL_PIN, LOW);
            #endif

            LoRa_Utils::sleepRadio();

            long DEEP_SLEEP_TIME_SEC = 1296000; // 15 days
            delay(500);
            #ifdef ARDUINO_ARCH_NRF52
                (void)DEEP_SLEEP_TIME_SEC;
                NRF_POWER->SYSTEMOFF = 1;   // wakes only on reset/button (no RTC wakeup wired up yet)
                while (1) { __WFE(); }
            #else
                esp_sleep_enable_timer_wakeup(1000000ULL * DEEP_SLEEP_TIME_SEC);
                esp_deep_sleep_start();
            #endif
        #endif
    }

}