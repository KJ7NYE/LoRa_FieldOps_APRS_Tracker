#ifndef TCP_KISS_UTILS_H_
#define TCP_KISS_UTILS_H_

#ifdef HAS_WIFI

#include <Arduino.h>

namespace TCP_KISS_Utils {

    void setup();
    void loop();
    void sendToClients(const String& aprsFrame);

}

#endif // HAS_WIFI
#endif // TCP_KISS_UTILS_H_
