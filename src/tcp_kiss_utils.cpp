#ifdef HAS_WIFI

#include <WiFi.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include "ESPmDNS.h"
#include "configuration.h"
#include "tcp_kiss_utils.h"
#include "kiss_utils.h"
#include "lora_utils.h"
#include "logger.h"

extern Configuration    Config;
extern logging::Logger  logger;

#define MAX_KISS_CLIENTS 4

static WiFiServer*  tncServer   = nullptr;
static WiFiClient*  clients[MAX_KISS_CLIENTS] = {};
static String       inputBuf[MAX_KISS_CLIENTS];

// Per-client serial KISS buffer (index 0 = always-open serial path unused here)
static String inputSerialBuf;


namespace TCP_KISS_Utils {

    static void handleKissFrame(const String& rawFrame, int clientIdx) {
        bool isData = false;
        String frame = KISS_Utils::decodeKISS(rawFrame, isData);
        if (!isData || frame.length() == 0) return;

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "TCP-KISS",
                   "RX frame from client %d: %s", clientIdx, frame.c_str());

        // Re-transmit via LoRa
        LoRa_Utils::sendNewPacket(frame);
    }

    static void feedClientByte(char c, int idx) {
        String& buf = (idx == -1) ? inputSerialBuf : inputBuf[idx];

        if (buf.length() == 0 && c != (char)FEND) return;  // wait for frame start
        buf += c;

        if (c == (char)FEND && buf.length() > 3) {
            handleKissFrame(buf, idx);
            buf = "";
        }
        if (buf.length() > 512) buf = "";  // overflow guard
    }

    void setup() {
        if (!Config.tcpKISS.enabled) return;

        if (tncServer) { delete tncServer; tncServer = nullptr; }
        tncServer = new WiFiServer(Config.tcpKISS.port);
        tncServer->begin();

        String host = "lora-kiss-" + Config.beacons[0].callsign;
        host.toLowerCase();
        MDNS.begin(host.c_str());
        MDNS.addService("tnc", "tcp", Config.tcpKISS.port);

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "TCP-KISS",
                   "Server started on port %u (mDNS: %s.local)", Config.tcpKISS.port, host.c_str());
    }

    void loop() {
        if (!tncServer) return;

        // Accept new clients
        WiFiClient newClient = tncServer->accept();
        if (newClient.connected()) {
            for (int i = 0; i < MAX_KISS_CLIENTS; i++) {
                if (clients[i] == nullptr) {
                    clients[i] = new WiFiClient(newClient);
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "TCP-KISS",
                               "Client %d connected from %s", i, newClient.remoteIP().toString().c_str());
                    break;
                }
            }
        }

        // Read from connected clients
        for (int i = 0; i < MAX_KISS_CLIENTS; i++) {
            if (clients[i] == nullptr) continue;
            if (!clients[i]->connected()) {
                delete clients[i];
                clients[i] = nullptr;
                inputBuf[i] = "";
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "TCP-KISS", "Client %d disconnected", i);
                continue;
            }
            while (clients[i]->available() > 0) {
                feedClientByte((char)clients[i]->read(), i);
            }
        }
    }

    void sendToClients(const String& aprsFrame) {
        if (!tncServer) return;
        String kissFrame = KISS_Utils::encodeKISS(aprsFrame);
        for (int i = 0; i < MAX_KISS_CLIENTS; i++) {
            if (clients[i] != nullptr && clients[i]->connected()) {
                clients[i]->print(kissFrame);
                clients[i]->flush();
            }
        }
    }

}

#endif // HAS_WIFI
