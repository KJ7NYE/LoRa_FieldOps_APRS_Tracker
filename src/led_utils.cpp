#include "led_utils.h"

#ifdef INTERNAL_LED_PIN

#include "configuration.h"
extern Configuration Config;

// LED polarity — active-LOW on T114 (LED_STATE_ON = 0), active-HIGH elsewhere.
// LED_STATE_ON is defined in the BSP variant.h for boards that need it.
#ifndef LED_STATE_ON
    #define LED_STATE_ON HIGH
#endif
static constexpr uint8_t LED_ON  =  LED_STATE_ON;
static constexpr uint8_t LED_OFF = !LED_STATE_ON;

namespace {
    constexpr uint32_t HEARTBEAT_PERIOD_MS = 1500;  // ms between heartbeat flashes
    constexpr uint32_t HEARTBEAT_ON_MS     = 50;    // brief heartbeat flash duration
    constexpr uint32_t TXRX_ON_MS          = 500;   // TX / RX flash duration

    static uint32_t ledOffAt      = 0;  // millis() when LED should turn off (0 = off)
    static uint32_t nextHeartbeat = 0;  // millis() of next scheduled heartbeat
}

namespace LED_Utils {

    void setup() {
        pinMode(INTERNAL_LED_PIN, OUTPUT);
        digitalWrite(INTERNAL_LED_PIN, LED_OFF);   // start dark
        nextHeartbeat = HEARTBEAT_PERIOD_MS;
    }

    void tick() {
        if (!Config.display.ledEnabled) {
            // Ensure LED is off if it was on when the setting changed.
            if (ledOffAt) { digitalWrite(INTERNAL_LED_PIN, LED_OFF); ledOffAt = 0; }
            return;
        }
        const uint32_t now = millis();
        // Turn off if the flash window has expired.
        if (ledOffAt && now >= ledOffAt) {
            digitalWrite(INTERNAL_LED_PIN, LED_OFF);
            ledOffAt = 0;
        }
        // Heartbeat: only fires when LED is already off.
        if (!ledOffAt && now >= nextHeartbeat) {
            digitalWrite(INTERNAL_LED_PIN, LED_ON);
            ledOffAt      = now + HEARTBEAT_ON_MS;
            nextHeartbeat = now + HEARTBEAT_PERIOD_MS;
        }
    }

    void txRxFlash() {
        if (!Config.display.ledEnabled) return;
        const uint32_t now = millis();
        digitalWrite(INTERNAL_LED_PIN, LED_ON);
        ledOffAt      = now + TXRX_ON_MS;
        nextHeartbeat = ledOffAt + HEARTBEAT_PERIOD_MS;  // skip heartbeat right after flash
    }

}  // namespace LED_Utils

#endif  // INTERNAL_LED_PIN
