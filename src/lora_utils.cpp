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

#include <APRSPacketLib.h>
#include <RadioLib.h>
#include <logger.h>
#include <SPI.h>

#include "configuration.h"
#include "board_pinout.h"
#include "lora_utils.h"
#include "display.h"
#include "led_utils.h"
#include "log_buffer.h"
#ifdef FAN_CTRL_PIN
#include "thermal_utils.h"
#endif

extern logging::Logger  logger;
extern Configuration    Config;
extern LoraType         *currentLoRaType;

bool operationDone   = true;
bool transmitFlag    = true;

#if defined(HAS_SX1262)
    // On HELTEC_T114, variants_bsp/heltec_t114/variant.h sets PIN_SPI_MISO/MOSI/SCK
    // to the LoRa pins (P0.23/22/19), so the BSP's default `SPI` global is already
    // the LoRa bus — no custom SPIClass needed, and SPIM2 stays free for SPI1 (TFT).
    // LORANGER_V1 and TTGO_T_BEAM_1W have the SX1262 on FSPI (IO11/12/13); use a
    // dedicated SPIClass so the bus is configured to the correct pins.
    #if defined(LORANGER_V1) || defined(TTGO_T_BEAM_1W)
        SPIClass loraSPI(FSPI);
        SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN, loraSPI);
    #else
        SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
    #endif
#endif
#if defined(HAS_SX1268)
    #if defined(LIGHTTRACKER_PLUS_1_0)
        SPIClass loraSPI(FSPI);
        SX1268 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN, loraSPI);
    #else
        SX1268 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
    #endif
#endif
#if defined(HAS_SX1278)
    SX1278 radio = new Module(RADIO_CS_PIN, RADIO_BUSY_PIN, RADIO_RST_PIN);
#endif
#if defined(HAS_SX1276)
    SX1276 radio = new Module(RADIO_CS_PIN, RADIO_BUSY_PIN, RADIO_RST_PIN);
#endif
#if defined(HAS_LLCC68) //  LLCC68 supports spreading factor only in range of 5-11!
    LLCC68 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
#endif

namespace LoRa_Utils {

    void setFlag(void) {
        operationDone = true;
    }

    void changeFreq() {
        // Single LoRa profile — nothing to cycle.
        // Apply current profile settings (in case they changed via CLI/web).
        float freq = (float)currentLoRaType->frequency/1000000;
        radio.setFrequency(freq);
        radio.setSpreadingFactor(currentLoRaType->spreadingFactor);
        float signalBandwidth = currentLoRaType->signalBandwidth / 1000.0f;
        radio.setBandwidth(signalBandwidth);
        radio.setCodingRate(currentLoRaType->codingRate4);
        #if (defined(HAS_SX1268) || defined(HAS_SX1262)) && !defined(HAS_1W_LORA)
            radio.setOutputPower(currentLoRaType->power + 2);
        #endif
        #if defined(HAS_SX1278) || defined(HAS_SX1276) || defined(HAS_1W_LORA)
            radio.setOutputPower(currentLoRaType->power);
        #endif

        String currentLoRainfo = "LoRa / Freq: ";
        currentLoRainfo += String(currentLoRaType->frequency);
        currentLoRainfo += " / SF:";
        currentLoRainfo += String(currentLoRaType->spreadingFactor);
        currentLoRainfo += " / CR: ";
        currentLoRainfo += String(currentLoRaType->codingRate4);

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "LoRa", currentLoRainfo.c_str());
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "LoRa", "Freq: %ld", currentLoRaType->frequency);
    }

    void setup() {
        #if defined(LIGHTTRACKER_PLUS_1_0) || defined(TTGO_T_BEAM_1W)
            pinMode(RADIO_VCC_PIN,OUTPUT);
            digitalWrite(RADIO_VCC_PIN,HIGH);
        #endif
        #if defined(TTGO_T_BEAM_1W)
            pinMode(RADIO_RXEN, OUTPUT);
            digitalWrite(RADIO_RXEN, LOW);  // start setup in Tx mode
        #endif

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "LoRa", "Set SPI pins!");
        #if defined(LIGHTTRACKER_PLUS_1_0) || defined(LORANGER_V1) || defined(TTGO_T_BEAM_1W)
            loraSPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN, RADIO_CS_PIN);
        #else
            #ifdef ARDUINO_ARCH_NRF52
                SPI.begin();   // Adafruit nRF52 BSP's SPI.begin() takes no args; pins are fixed by the BSP variant (PIN_SPI_*)
            #else
                SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN);
            #endif
        #endif
        float freq = (float)currentLoRaType->frequency/1000000;
        #if defined(RADIO_HAS_XTAL)
            radio.XTAL = true;
        #endif
        int state = radio.begin(freq);
        if (state == RADIOLIB_ERR_NONE) {
            #if defined(HAS_SX1262) || defined(HAS_SX1268)
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "LoRa", "Initializing SX126X ...");
            #else
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "LoRa", "Initializing SX127X ...");
            #endif
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "LoRa", "Starting LoRa failed! State: %d", state);
            while (true);
        }
        #if defined(HAS_SX1262) || defined(HAS_SX1268) || defined(HAS_LLCC68)
            radio.setDio1Action(setFlag);
        #endif
        #if defined(HAS_SX1278) || defined(HAS_SX1276)
            radio.setDio0Action(setFlag, RISING);
        #endif
        radio.setSpreadingFactor(currentLoRaType->spreadingFactor);
        float signalBandwidth = currentLoRaType->signalBandwidth / 1000.0f;
        radio.setBandwidth(signalBandwidth);
        radio.setCodingRate(currentLoRaType->codingRate4);
        radio.setCRC(true);

        #if defined(RADIO_RXEN) && defined(RADIO_TXEN)
            radio.setRfSwitchPins(RADIO_RXEN, RADIO_TXEN);
        #endif
        #if defined(TTGO_T_BEAM_1W)
            radio.setRfSwitchPins(RADIO_RXEN, RADIOLIB_NC);
        #endif

        #ifdef HAS_1W_LORA  // Ebyte E22 400M30S (SX1268) / 900M30S (SX1262) / Ebyte E220 400M30S (LLCC68)
            state = radio.setOutputPower(currentLoRaType->power); // max value 20 (when 20dB in setup 30dB in output as 400M30S has Low Noise Amp)
            radio.setCurrentLimit(140); // to be validated (100 , 120, 140)?
        #endif

        #if (defined(HAS_SX1268) || defined(HAS_SX1262)) && !defined(HAS_1W_LORA)
            // SX1262 valid range is -9..+22 dBm. The +2 offset is upstream's
            // user-friendly bump (config 20 -> chip 22). If the on-flash
            // config has a value outside the safe band, the call returns
            // -13 (INVALID_OUTPUT_POWER) and aborts the whole init. Clamp
            // here so a stale or corrupt config can't brick startup.
            int8_t requestedPower = currentLoRaType->power + 2;
            int8_t clampedPower   = requestedPower;
            if (clampedPower > 22)  clampedPower = 22;
            if (clampedPower < -9)  clampedPower = -9;
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "LoRa",
                       "cfg power=%d, requested=%d, clamped=%d",
                       (int)currentLoRaType->power,
                       (int)requestedPower, (int)clampedPower);
            state = radio.setOutputPower(clampedPower);
            radio.setCurrentLimit(140);
        #endif

        #if defined(HAS_SX1278) || defined(HAS_SX1276)
            state = radio.setOutputPower(currentLoRaType->power);
            radio.setCurrentLimit(100); // to be validated (80 , 100)?
        #endif

        #if defined(HAS_SX1262) || defined(HAS_SX1268) || defined(HAS_LLCC68)
            radio.setRxBoostedGainMode(true);
        #endif

        #if defined(HAS_TCXO) && !defined(HAS_1W_LORA)
            radio.setDio2AsRfSwitch();
        #endif
        #ifdef HAS_TCXO
            radio.setTCXO(1.8);
        #endif

        if (state == RADIOLIB_ERR_NONE) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "LoRa", "LoRa init done!");
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "LoRa", "Starting LoRa failed! State: %d", state);
            while (true);
        }
    }

    void sendNewPacket(const String& newPacket) {
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "LoRa Tx","---> %s", newPacket.c_str());
        /*logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "LoRa","Send data: %s", newPacket.c_str());
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "LoRa","Send data: %s", newPacket.c_str());
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "LoRa","Send data: %s", newPacket.c_str());*/

        // FCC TX-gate: refuse to key the carrier if the source callsign field
        // looks like a placeholder (NOCALL / N0CALL). Catches misconfigured
        // local beacons and any externally-injected packets via BLE/BT-Classic
        // KISS paths. Load-bearing now that nRF52 first-boot writes a default
        // config including NOCALL-7 — without this gate, a fresh device would
        // TX before the operator can configure a real callsign.
        int sepPos = newPacket.indexOf('>');
        String fromCall = (sepPos > 0) ? newPacket.substring(0, sepPos) : newPacket;
        if (APRSPacketLib::checkNocall(fromCall)) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "LoRa Tx",
                       "TX BLOCKED: source callsign \"%s\" is a placeholder; configure a valid callsign before transmitting",
                       fromCall.c_str());
            return;
        }

        LogBuffer::pushf(LogBuffer::TYPE_TX, "%s", newPacket.c_str());
        displayTx(newPacket);

        if (Config.ptt.active) {
            digitalWrite(Config.ptt.io_pin, Config.ptt.reverse ? LOW : HIGH);
            delay(Config.ptt.preDelay);
        }
        LED_Utils::txRxFlash();

        #if defined(TTGO_T_BEAM_1W)
            digitalWrite(RADIO_RXEN, LOW);
        #endif
        #ifdef FAN_CTRL_PIN
            THERMAL_Utils::onTxStart();
        #endif
        int state = radio.transmit("\x3c\xff\x01" + newPacket);
        #ifdef FAN_CTRL_PIN
            THERMAL_Utils::onTxEnd();
        #endif
        transmitFlag = true;
        if (state == RADIOLIB_ERR_NONE) {
            //Serial.println(F("success!"));
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "LoRa Tx", "Tx failed, code %d", state);
        }

        if (Config.ptt.active) {
            delay(Config.ptt.postDelay);
            digitalWrite(Config.ptt.io_pin, Config.ptt.reverse ? HIGH : LOW);
        }
    }

    void wakeRadio() {
        radio.startReceive();
    }

    ReceivedLoRaPacket receiveFromSleep() {
        ReceivedLoRaPacket receivedLoraPacket;
        String packet = "";
        #if defined(TTGO_T_BEAM_1W)
            digitalWrite(RADIO_RXEN, HIGH);
        #endif
        int state = radio.readData(packet);
        if (state == RADIOLIB_ERR_NONE) {
            receivedLoraPacket.text       = packet;
            receivedLoraPacket.rssi       = radio.getRSSI();
            receivedLoraPacket.snr        = radio.getSNR();
            receivedLoraPacket.freqError  = radio.getFrequencyError();
        } else {
            //
        }
        return receivedLoraPacket;
    }

    ReceivedLoRaPacket receivePacket() {
        ReceivedLoRaPacket receivedLoraPacket;
        String packet = "";
        if (operationDone) {
            operationDone = false;
            if (transmitFlag) {
                #if defined(TTGO_T_BEAM_1W)
                    digitalWrite(RADIO_RXEN, HIGH);
                #endif
                radio.startReceive();
                transmitFlag = false;
            } else {
                int state = radio.readData(packet);
                if (state == RADIOLIB_ERR_NONE) {
                    if(packet.length() != 0) {
                        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "LoRa Rx","---> %s", packet.substring(3).c_str());
                        LogBuffer::pushf(LogBuffer::TYPE_RX, "%s  [RSSI:%d SNR:%.0f]",
                            packet.substring(3).c_str(), (int)radio.getRSSI(), (double)radio.getSNR());
                        receivedLoraPacket.text       = packet;
                        receivedLoraPacket.rssi       = radio.getRSSI();
                        receivedLoraPacket.snr        = radio.getSNR();
                        receivedLoraPacket.freqError  = radio.getFrequencyError();
                    }
                } else {
                    // 7 = CRC mismatch
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "LoRa Rx", "Rx failed, code %d", state);
                }
            }
        }
        return receivedLoraPacket;
    }

    void sleepRadio() {
        radio.sleep();
    }

}