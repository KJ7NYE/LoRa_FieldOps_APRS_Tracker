/* thermal_utils.h — Fan/thermal management for boards with FAN_CTRL_PIN.
 *
 * Currently supports: TTGO_T_BEAM_1W (FAN_CTRL_PIN = 41, TEMP_SENSOR_PIN = 14).
 * All declarations are compiled away on boards without FAN_CTRL_PIN.
 *
 * Thermistor: NCP18XH103F03RB (Murata 10 kΩ NTC, B25/50 = 3380 K)
 * Circuit:    3.3V → NTC → IO14 (ADC) → 10 kΩ → GND
 */

#ifndef THERMAL_UTILS_H_
#define THERMAL_UTILS_H_

#include "board_pinout.h"

#ifdef FAN_CTRL_PIN

namespace THERMAL_Utils {

    // Call once in setup(), after POWER_Utils::externalPinSetup().
    // Configures FAN_CTRL_PIN as output (fan off) and TEMP_SENSOR_PIN ADC attenuation.
    void  setup();

    // Call every loop iteration alongside BATTERY_Utils::monitor().
    // Non-blocking: samples temperature only every 30 s.
    // Manages fan state (temperature thresholds + TX cooldown) and triggers
    // over-temp shutdown if temperature reaches TEMP_SHUTDOWN_C.
    void  monitor();

    // Signal TX start: turns fan on immediately (before radio.transmit()).
    void  onTxStart();

    // Signal TX end: starts 30 s post-TX cooldown (after radio.transmit() returns).
    void  onTxEnd();

    // Returns true if the last temperature reading was >= TEMP_WARN_C (75 °C).
    // Used by station_utils to append a warning to the next beacon.
    bool  isOverTemp();

    // Returns the most recent temperature reading in °C.
    float getTemperatureC();

}

#endif // FAN_CTRL_PIN
#endif // THERMAL_UTILS_H_
