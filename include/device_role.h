#ifndef DEVICE_ROLE_H_
#define DEVICE_ROLE_H_

#include <Arduino.h>
#include "configuration.h"

namespace DeviceRoleUtils {

    const char* getRoleString(DeviceRole role);

    void initializeRole(DeviceRole role);

    void initializeTracker();
    void initializeDigipeater();

    #ifdef HAS_WIFI
    void initializeIGate();
    #endif

    // Route a received TNC2 frame (no RSSI prefix) to the active role handler.
    void processReceivedPacket(const String& frame);

    // Called every loop iteration for role-specific background tasks
    // (WiFi/APRS-IS maintenance, TCP KISS client I/O, etc.).
    void handleRoleSpecificTasks();

    DeviceRole getCurrentRole();
    bool isRoleInitialized();

    bool validateRoleForPlatform(DeviceRole role);

}

#endif
