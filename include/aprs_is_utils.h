#ifndef APRS_IS_UTILS_H_
#define APRS_IS_UTILS_H_

#ifdef HAS_WIFI

#include <Arduino.h>

namespace APRS_IS_Utils {

    void    connect();
    void    disconnect();
    bool    isConnected();

    void    upload(const String& line);
    void    processLoRaPacket(const String& packet);
    void    listenAPRSIS();
    void    checkConnection();

}

#endif // HAS_WIFI
#endif // APRS_IS_UTILS_H_
