#pragma once
/* Simple always-on LED indicator.
 *
 * Requires INTERNAL_LED_PIN to be defined in the board pinout.
 * If not defined the functions compile to no-ops.
 *
 *  Heartbeat — brief 50 ms flash every 1.5 s (call tick() from main loop)
 *  TX/RX     — 500 ms flash, overlaps / replaces heartbeat timing
 */

#include "board_pinout.h"
#include <Arduino.h>

namespace LED_Utils {

#ifdef INTERNAL_LED_PIN

    void setup();        // pinMode + initial state
    void tick();         // non-blocking state machine — call every loop iteration
    void txRxFlash();    // trigger a 500 ms flash (TX or RX event)

#else

    inline void setup()      {}
    inline void tick()       {}
    inline void txRxFlash()  {}

#endif

}  // namespace LED_Utils
