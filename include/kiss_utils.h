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

#ifndef KISS_UTILS_H
#define KISS_UTILS_H

#include <Arduino.h>


enum KissChar {
    FEND                = 0xc0,
    FESC                = 0xdb,
    TFEND               = 0xdc,
    TFESC               = 0xdd
};

enum KissCmd {
    Data                = 0x00,
    SetHardware         = 0x06     // vendor-defined payload; carries the RX signal report
};

enum AX25Char {
    ControlField        = 0x03,
    InformationField    = 0xF0
};

#define HAS_BEEN_DIGIPITED_MASK         0b10000000
#define IS_LAST_ADDRESS_POSITION_MASK   0b1


namespace KISS_Utils {

    bool validateTNC2Frame(const String& tnc2FormattedFrame);
    bool validateKISSFrame(const String& kissFormattedFrame);

    // TNC2 text -> KISS data frame. Returns "" if `frame` is not valid TNC2.
    String encodeKISS(const String& frame);
    String decodeKISS(const String& inputFrame, bool& dataFrame);

    // RX signal report as a KISS SetHardware frame (command 0x06, port 0)
    // holding ASCII "RSSI:<int>,SNR:<one decimal>", e.g. "RSSI:-97,SNR:6.5".
    // Sent straight after a received packet's data frame when kiss.rssi is on.
    String encodeSignalReport(int rssi, float snr);

}

#endif
