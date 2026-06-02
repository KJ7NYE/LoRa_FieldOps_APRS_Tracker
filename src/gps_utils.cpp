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

#include <TinyGPS++.h>
#include "TimeLib.h"
#include <APRSPacketLib.h>
#include "smartbeacon_utils.h"
#include "configuration.h"
#include "station_utils.h"
#include "board_pinout.h"
#include "power_utils.h"
#include "sleep_utils.h"
#include "gps_utils.h"
#include "display.h"
#include "logger.h"


#ifdef GPS_BAUDRATE
    #define GPS_BAUD    GPS_BAUDRATE
#else
    #define GPS_BAUD    9600
#endif


extern Configuration        Config;
#ifdef ARDUINO_ARCH_NRF52
    // Adafruit BSP's Serial1 is our GPS port — see LoRa_APRS_Tracker.cpp.
    #define gpsSerial Serial1
#else
    extern HardwareSerial   gpsSerial;
#endif
extern TinyGPSPlus          gps;
extern Beacon               *currentBeacon;
extern logging::Logger      logger;
extern bool                 sendUpdate;
extern bool		            sendStandingUpdate;

extern uint32_t             lastTxTime;
extern uint32_t             txInterval;
extern double               lastTxLat;
extern double               lastTxLng;
extern double               lastTxDistance;
extern uint32_t             lastTx;
extern bool                 disableGPS;
extern bool                 gpsShouldSleep;
extern SmartBeaconValues    currentSmartBeaconValues;

double      currentHeading  = 0;
double      previousHeading = 0;
float       bearing         = 0;

bool        gpsIsActive     = false;  // set to true in setup() only when GPS hardware is started

TinyGPSPlus externalGPS;  // For external GPS sources (serial or BLE)
uint32_t    lastExternalGPSUpdate = 0;


namespace GPS_Utils {

    struct PositionData {
        double latitude;
        double longitude;
        float elevation;
        uint8_t satelliteCount;
        uint32_t timestamp;
        bool isValid;
    };

    PositionData lastValidPosition = {0, 0, 0, 0, 0, false};

    void feedExternalGPSData(uint8_t byte) {
        externalGPS.encode(byte);
        if (externalGPS.location.isUpdated()) {
            lastExternalGPSUpdate = millis();
        }
    }

    bool getPositionData(PositionData &posData) {
        posData.timestamp = millis();
        posData.isValid = false;

        switch (Config.gpsSource) {
            case GPS_INTERNAL:
                if (disableGPS) return false;
                if (gps.location.isValid()) {
                    posData.latitude = gps.location.lat();
                    posData.longitude = gps.location.lng();
                    posData.elevation = gps.altitude.meters();
                    posData.satelliteCount = gps.satellites.value();
                    posData.isValid = true;
                    lastValidPosition = posData;
                    return true;
                }
                break;

            case GPS_FIXED:
                posData.latitude = Config.fixedPosition.latitude;
                posData.longitude = Config.fixedPosition.longitude;
                posData.elevation = Config.fixedPosition.elevation;
                posData.satelliteCount = 0;
                posData.isValid = (posData.latitude != 0 || posData.longitude != 0);
                if (posData.isValid) {
                    lastValidPosition = posData;
                }
                return posData.isValid;

            case GPS_EXTERNAL_SERIAL:
            case GPS_EXTERNAL_BLE:
                if (externalGPS.location.isValid()) {
                    posData.latitude = externalGPS.location.lat();
                    posData.longitude = externalGPS.location.lng();
                    posData.elevation = externalGPS.altitude.meters();
                    posData.satelliteCount = externalGPS.satellites.value();
                    posData.isValid = true;
                    lastValidPosition = posData;
                    return true;
                }
                // Fallback to fixed position if external GPS has no fix
                if (Config.fixedPosition.latitude != 0 || Config.fixedPosition.longitude != 0) {
                    posData.latitude = Config.fixedPosition.latitude;
                    posData.longitude = Config.fixedPosition.longitude;
                    posData.elevation = Config.fixedPosition.elevation;
                    posData.satelliteCount = 0;
                    posData.isValid = true;
                    lastValidPosition = posData;
                    return true;
                }
                break;

            default:
                break;
        }

        // Fallback to last valid position if current source has no data
        if (lastValidPosition.isValid) {
            posData = lastValidPosition;
            return true;
        }

        return false;
    }

    bool getCurrentLocation(double &lat, double &lng, float &elev) {
        PositionData posData;
        if (getPositionData(posData)) {
            lat = posData.latitude;
            lng = posData.longitude;
            elev = posData.elevation;
            return true;
        }
        return false;
    }

    void setup() {
        // Fixed position: no hardware to start; position comes from config.
        if (Config.gpsSource == GPS_FIXED) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "GPS", "Fixed position mode — GPS hardware not started");
            gpsIsActive = false;
            return;
        }

        // Board has no GPS connector or pin assignment.
        #ifdef HAS_NO_GPS
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "GPS", "No GPS hardware on this board");
            gpsIsActive = false;
            return;
        #endif

        if (disableGPS) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "GPS", "GPS disabled by config");
            gpsIsActive = false;
            return;
        }

        #ifdef LIGHTTRACKER_PLUS_1_0
            pinMode(GPS_VCC, OUTPUT);
            digitalWrite(GPS_VCC, LOW);
            delay(200);
        #endif
        #if defined(F4GOH_1W_LoRa_Tracker) || defined(F4GOH_1W_LoRa_Tracker_LLCC68) || defined(HELTEC_T114)
            // T114: VEXT_ENABLE (P0.21) gates the L76K GPS power rail; must be
            // HIGH before any UART activity or the GPS reports nothing.
            pinMode(GPS_VCC, OUTPUT);
            digitalWrite(GPS_VCC, HIGH);
            delay(200);
        #endif

        #ifdef ARDUINO_ARCH_NRF52
            gpsSerial.begin(GPS_BAUD);
        #else
            gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_TX, GPS_RX);
        #endif

        gpsIsActive = true;
    }

    void calculateDistanceCourse(const String& callsign, double checkpointLatitude, double checkPointLongitude) {
        double distanceKm = TinyGPSPlus::distanceBetween(gps.location.lat(), gps.location.lng(), checkpointLatitude, checkPointLongitude) / 1000.0;
        double courseTo   = TinyGPSPlus::courseTo(gps.location.lat(), gps.location.lng(), checkpointLatitude, checkPointLongitude);
        STATION_Utils::updateLastHeard(callsign);  // track heard station for display
    }

    void getData() {
        if (disableGPS) return;
        while (gpsSerial.available() > 0) gps.encode(gpsSerial.read());
    }

    void setDateFromData() {
        if (gps.time.isValid()) setTime(gps.time.hour(), gps.time.minute(), gps.time.second(), gps.date.day(), gps.date.month(), gps.date.year());
    }

    void calculateDistanceTraveled() {
        currentHeading  = gps.course.deg();
        lastTxDistance  = TinyGPSPlus::distanceBetween(gps.location.lat(), gps.location.lng(), lastTxLat, lastTxLng);
        if (lastTx >= txInterval) {
            if (lastTxDistance > currentSmartBeaconValues.minTxDist) {
                sendUpdate = true;
                sendStandingUpdate = false;
            } else {
                if (currentBeacon->gpsEcoMode) {
                    //
                    Serial.print("minTxDistance not achieved : ");
                    Serial.println(lastTxDistance);
                    //
                    gpsShouldSleep = true;
                }
            }
        }
    }

    void calculateHeadingDelta(int speed) {
        uint8_t TurnMinAngle;
        double headingDelta = abs(previousHeading - currentHeading);
        if (lastTx > (uint32_t)(currentSmartBeaconValues.minDeltaBeacon * 1000)) {
            if (speed == 0) {
                TurnMinAngle = currentSmartBeaconValues.turnMinDeg + (currentSmartBeaconValues.turnSlope/(speed + 1));
            } else {
                TurnMinAngle = currentSmartBeaconValues.turnMinDeg + (currentSmartBeaconValues.turnSlope/speed);
            }
            if (headingDelta > TurnMinAngle && lastTxDistance > currentSmartBeaconValues.minTxDist) {
                sendUpdate = true;
                sendStandingUpdate = false;
            }
        }
    }

    void checkStartUpFrames() {
        if (disableGPS) return;
        if ((millis() > 10000 && gps.charsProcessed() < 10)) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "GPS",
                        "No GPS frames detected! Try to reset the GPS Chip with this "
                        "firmware: https://github.com/richonguzman/TTGO_T_BEAM_GPS_RESET");
            bootStatus("ERROR: No GPS frames!");
        }
    }

    String getHumanBearing(const String& left, const String& center, const String& right) {
        String bearing = ">.";
        bearing += left;
        for (int i = 0; i < 9; i++) {
            bearing += ".";
        }
        bearing += "(";
        bearing += center;
        bearing += ").....";
        if (right.length() == 1 && center.length() != 2) bearing += ".";
        bearing += right;
        bearing += ".<";
        return bearing;
    }

    String getCardinalDirection(float course) {
        if (gps.speed.kmph() > 0.5) bearing = course;

        if (bearing >= 354.375 || bearing < 5.625)    return ">.NW.....(N).....NE.<"; // N
        if (bearing >= 5.675 && bearing < 16.875)     return ">.......N.|.....NE..<";
        if (bearing >= 16.875 && bearing < 28.125)    return ">.....N...|...NE....<"; // NEN
        if (bearing >= 28.125 && bearing < 39.375)    return ">...N.....|.NE......<";
        if (bearing >= 39.375 && bearing < 50.625)    return ">.N......(NE).....E.<"; // NE
        if (bearing >= 50.625 && bearing < 61.875)    return ">.......NE|.....E...<";
        if (bearing >= 61.875 && bearing < 73.125)    return ">.....NE..|...E.....<"; // ENE
        if (bearing >= 73.125 && bearing < 84.375)    return ">...NE....|.E.......<";
        if (bearing >= 84.375 && bearing < 95.625)    return ">.NE.....(E).....SE.<"; // E
        if (bearing >= 95.625 && bearing < 106.875)   return ">.......E.|.....SE..<";
        if (bearing >= 106.875 && bearing < 118.125)  return ">.....E...|...SE....<"; // ESE
        if (bearing >= 118.125 && bearing < 129.375)  return ">...E.....|.SE......<";
        if (bearing >= 129.375 && bearing < 140.625)  return ">.E......(SE).....S.<"; // SE
        if (bearing >= 140.625 && bearing < 151.875)  return ">.......SE|.....S...<";
        if (bearing >= 151.875 && bearing < 163.125)  return ">.....SE..|...S.....<"; // SES
        if (bearing >= 163.125 && bearing < 174.375)  return ">...SE....|.S.......<";
        if (bearing >= 174.375 && bearing < 185.625)  return ">.SE.....(S).....SW.<"; // S
        if (bearing >= 185.625 && bearing < 196.875)  return ">.......S.|.....SW..<";
        if (bearing >= 196.875 && bearing < 208.125)  return ">.....S...|...SW....<"; // SWS
        if (bearing >= 208.125 && bearing < 219.375)  return ">...S.....|.SW......<";
        if (bearing >= 219.375 && bearing < 230.625)  return ">.S......(SW).....W.<"; // SW
        if (bearing >= 230.625 && bearing < 241.875)  return ">.......SW|.....W...<";
        if (bearing >= 241.875 && bearing < 253.125)  return ">.....SW..|...W.....<"; // WSW
        if (bearing >= 253.125 && bearing < 264.375)  return ">...SW....|.W.......<";
        if (bearing >= 264.375 && bearing < 275.625)  return ">.SW.....(W).....NW.<"; // W
        if (bearing >= 275.625 && bearing < 286.875)  return ">.......W.|.....NW..<";
        if (bearing >= 286.875 && bearing < 298.125)  return ">.....W...|...NW....<"; // WNW
        if (bearing >= 298.125 && bearing < 309.375)  return ">...W.....|.NW......<";
        if (bearing >= 309.375 && bearing < 320.625)  return ">.W......(NW).....N.<"; // NW
        if (bearing >= 320.625 && bearing < 331.875)  return ">.......NW|.....N...<";
        if (bearing >= 331.875 && bearing < 343.125)  return ">.....NW..|...N.....<"; // NWN
        if (bearing >= 343.125 && bearing < 354.375)  return ">...NW....|.N.......<";
        return "";
    }

}