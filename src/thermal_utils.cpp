/* thermal_utils.cpp — Fan/thermal management for TTGO_T_BEAM_1W.
 *
 * Thermistor: NCP18XH103F03RB (Murata 10 kΩ NTC, B25/50 = 3380 K)
 * Circuit:    3.3V → NTC → IO14 (ADC input) → 10 kΩ → GND
 *
 * As temperature rises, NTC resistance falls and V(IO14) rises.
 * B-parameter equation:  1/T = 1/T₀ + (1/B) × ln(R/R₀)
 *
 * Fan strategy (hybrid):
 *   • TX-triggered: fan on at TX start, held on for TX_COOLDOWN_MS after TX end.
 *   • Temperature-triggered: fan on at FAN_ON_TEMP_C, off below FAN_OFF_TEMP_C
 *     with hysteresis to prevent rapid cycling.
 *   • Over-temp guard: sets isOverTemp() flag at TEMP_WARN_C;
 *     triggers clean shutdown at TEMP_SHUTDOWN_C.
 */

#include "board_pinout.h"

#ifdef FAN_CTRL_PIN

#include <Arduino.h>
#include <math.h>
#include "thermal_utils.h"
#include "power_utils.h"
#include "logger.h"

extern logging::Logger logger;

namespace {

    // ── Thermistor constants ──────────────────────────────────────────────────
    constexpr float NTC_B    = 3380.0f;    // B25/50 constant (K)
    constexpr float NTC_R25  = 10000.0f;   // resistance at 25 °C (Ω)
    constexpr float NTC_T0   = 298.15f;    // 25 °C in Kelvin
    constexpr float R_FIXED  = 10000.0f;   // pull-down resistor to GND (Ω)
    constexpr float VCC_MV   = 3300.0f;    // supply voltage (mV)

    // ── Fan control thresholds ────────────────────────────────────────────────
    constexpr float    FAN_ON_TEMP_C    = 50.0f;    // turn fan on above this
    constexpr float    FAN_OFF_TEMP_C   = 42.0f;    // turn fan off below this (hysteresis)
    constexpr float    TEMP_WARN_C      = 75.0f;    // set isOverTemp() flag above this
    constexpr float    TEMP_SHUTDOWN_C  = 85.0f;    // trigger clean shutdown above this
    constexpr uint32_t TX_COOLDOWN_MS   = 30000UL;  // post-TX fan hold (ms)
    constexpr uint32_t SAMPLE_MS        = 30000UL;  // temperature sampling interval (ms)
    constexpr uint32_t STATUS_LOG_MS    = 60000UL;  // periodic debug status print interval (ms)

    // ── Module state ─────────────────────────────────────────────────────────
    static float    currentTempC     = 25.0f;
    static bool     fanOn            = false;
    static bool     overTempFlag     = false;
    static uint32_t lastSampleMs     = 0;
    static uint32_t lastStatusLogMs  = 0;
    static uint32_t txEndMs          = 0;
    static bool     txCooldownActive = false;

    // ── Steinhart-Hart B-parameter conversion ─────────────────────────────────
    // millivolts: analogReadMilliVolts() output for IO14 (0 … 3300 mV)
    // Returns temperature in °C, or NAN if the ADC reading is out of range.
    static float adcMvToTempC(float mv) {
        if (mv <= 0.0f || mv >= VCC_MV) return NAN;
        float r_ntc = R_FIXED * (VCC_MV - mv) / mv;
        if (r_ntc <= 0.0f) return NAN;
        float tempK = 1.0f / (1.0f / NTC_T0 + (1.0f / NTC_B) * logf(r_ntc / NTC_R25));
        return tempK - 273.15f;
    }

    // Average 5 ADC readings; pattern mirrors battery_utils.cpp for T-Beam 1W.
    static float readTempC() {
        analogReadMilliVolts(TEMP_SENSOR_PIN);  // dummy read to settle ADC
        delay(1);
        uint32_t sum = 0;
        for (int i = 0; i < 5; i++) {
            sum += analogReadMilliVolts(TEMP_SENSOR_PIN);
            delay(3);
        }
        return adcMvToTempC((float)(sum / 5));
    }

    // Apply fan state and print to Serial immediately (visible without log mode).
    static void setFan(bool on, const char* reason) {
        if (fanOn == on) return;
        fanOn = on;
        digitalWrite(FAN_CTRL_PIN, on ? HIGH : LOW);
        Serial.printf("[Thermal] Fan %s — %s (%.1f C)\n",
                      on ? "ON " : "OFF", reason, (double)currentTempC);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Thermal",
                   "Fan %s — %s (%.1f C)", on ? "ON" : "OFF", reason, (double)currentTempC);
    }

    // Evaluate fan state against current temp and cooldown; call after any state change.
    static void updateFanState() {
        if (!fanOn) {
            if (txCooldownActive)              setFan(true,  "TX cooldown active");
            else if (currentTempC >= FAN_ON_TEMP_C) setFan(true, "temp threshold");
        } else {
            if (!txCooldownActive && currentTempC < FAN_OFF_TEMP_C) {
                setFan(false, "temp + cooldown clear");
            }
        }
    }

} // anonymous namespace

namespace THERMAL_Utils {

    void setup() {
        #ifdef TEMP_SENSOR_PIN
            pinMode(TEMP_SENSOR_PIN, INPUT);
            analogSetPinAttenuation(TEMP_SENSOR_PIN, ADC_11db);
        #endif
        pinMode(FAN_CTRL_PIN, OUTPUT);
        fanOn = false;
        digitalWrite(FAN_CTRL_PIN, LOW);
        lastSampleMs    = 0;  // trigger an immediate first sample on the first monitor() call
        lastStatusLogMs = 0;
        Serial.println("[Thermal] setup complete — fan OFF");
    }

    void monitor() {
        uint32_t now = millis();

        // ── Expire TX cooldown ────────────────────────────────────────────────
        // This runs every loop (not rate-limited) so the fan turns off promptly
        // when the cooldown ends rather than waiting for the next temperature sample.
        if (txCooldownActive && (now - txEndMs >= TX_COOLDOWN_MS)) {
            txCooldownActive = false;
            Serial.printf("[Thermal] TX cooldown expired — temp %.1f C\n", (double)currentTempC);
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Thermal",
                       "TX cooldown expired — temp %.1f C", (double)currentTempC);
            updateFanState();  // re-evaluate fan now, not at the next 30s sample
        }

        // ── Periodic status log ───────────────────────────────────────────────
        if (now - lastStatusLogMs >= STATUS_LOG_MS) {
            lastStatusLogMs = now;
            uint32_t coolRemainS = txCooldownActive
                ? (uint32_t)((TX_COOLDOWN_MS - (now - txEndMs)) / 1000UL) : 0;
            Serial.printf("[Thermal] status: %.1f C | fan %s | cooldown %s (%us remain) | sampleAge %us\n",
                          (double)currentTempC,
                          fanOn ? "ON " : "off",
                          txCooldownActive ? "active" : "off",
                          (unsigned)coolRemainS,
                          (unsigned)((now - lastSampleMs) / 1000UL));
        }

        // ── Rate-limited temperature sample ───────────────────────────────────
        if (lastSampleMs != 0 && (now - lastSampleMs < SAMPLE_MS)) return;
        lastSampleMs = now;

        #ifdef TEMP_SENSOR_PIN
            float t = readTempC();
            if (!isnan(t)) {
                currentTempC = t;
            }
            Serial.printf("[Thermal] sample: %.1f C | fan %s | cooldown %s\n",
                          (double)currentTempC,
                          fanOn ? "ON " : "off",
                          txCooldownActive ? "active" : "off");
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Thermal",
                       "%.1f C | fan %s | cooldown %s",
                       (double)currentTempC,
                       fanOn ? "ON" : "off",
                       txCooldownActive ? "active" : "off");
        #endif

        overTempFlag = (currentTempC >= TEMP_WARN_C);

        if (currentTempC >= TEMP_SHUTDOWN_C) {
            Serial.printf("[Thermal] OVER-TEMP SHUTDOWN at %.1f C\n", (double)currentTempC);
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "Thermal",
                       "Over-temp shutdown at %.1f C", (double)currentTempC);
            POWER_Utils::shutdown();
            return;
        }

        updateFanState();
    }

    void onTxStart() {
        Serial.printf("[Thermal] TX start — fan on\n");
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Thermal", "TX start");
        setFan(true, "TX start");
    }

    void onTxEnd() {
        txEndMs          = millis();
        txCooldownActive = true;
        Serial.printf("[Thermal] TX end — cooldown %us\n", (unsigned)(TX_COOLDOWN_MS / 1000UL));
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Thermal",
                   "TX end — cooldown %us", (unsigned)(TX_COOLDOWN_MS / 1000UL));
    }

    bool isOverTemp() {
        return overTempFlag;
    }

    float getTemperatureC() {
        return currentTempC;
    }

} // namespace THERMAL_Utils

#endif // FAN_CTRL_PIN
