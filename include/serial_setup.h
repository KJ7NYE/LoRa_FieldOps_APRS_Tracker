/* Copyright (C) 2025 Ricardo Guzman - CA2RXU
 *
 * This file is part of LoRa APRS Tracker.
 *
 * LoRa APRS Tracker is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef SERIAL_SETUP_H_
#define SERIAL_SETUP_H_

#include <Arduino.h>


namespace SERIAL_Setup {

    void setup();
    void loop();
    bool isActive();   // true while setup CLI is open

}

#endif
