/* LoRa APRS Tracker — TCP KISS server utilities
 *
 * Adapted from richonguzman/LoRa_APRS_iGate tnc_utils.cpp (CA2RXU).
 */

#ifdef HAS_WIFI

#include <WiFi.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include "tcp_kiss_utils.h"
#include "configuration.h"
#include "kiss_utils.h"
#include "lora_utils.h"
#include "logger.h"

extern Configuration    Config;
extern logging::Logger  logger;

#define MAX_KISS_CLIENTS    4
#define KISS_BUF_SIZE       512

static WiFiServer*  tncServer = nullptr;
static WiFiClient   clients[MAX_KISS_CLIENTS];
static String       inputBuf[MAX_KISS_CLIENTS];

namespace TCP_KISS_Utils {

    void setup() {
        if (tncServer) { delete tncServer; }
        tncServer = new WiFiServer(Config.tcpKISS.port);
        tncServer->begin();
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "KISS", "TCP server on port %d", Config.tcpKISS.port);
    }

    static void handleInputChar(char c, int slot) {
        String& buf = inputBuf[slot];
        if (buf.length() == 0 && (uint8_t)c != 0xC0) return; // wait for FEND

        buf += c;

        if ((uint8_t)c == 0xC0 && buf.length() > 3) {
            bool isData = false;
            String frame = KISS_Utils::decodeKISS(buf, isData);
            if (isData && frame.length() > 0) {
                // Check it's not our own callsign to avoid loop
                String sender = frame.substring(0, frame.indexOf(">"));
                if (sender != Config.beacons[0].callsign) {
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "KISS", "<-- Rx frame: %s", frame.c_str());
                    LoRa_Utils::sendNewPacket(frame);
                }
            }
            buf = "";
        }

        if (buf.length() > KISS_BUF_SIZE) buf = ""; // overflow guard
    }

    void loop() {
        if (!tncServer) return;

        // Accept new connections
        WiFiClient newClient = tncServer->accept();
        if (newClient.connected()) {
            for (int i = 0; i < MAX_KISS_CLIENTS; i++) {
                if (!clients[i].connected()) {
                    clients[i] = newClient;
                    inputBuf[i] = "";
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "KISS", "Client %d connected", i);
                    break;
                }
            }
        }

        // Read from connected clients
        for (int i = 0; i < MAX_KISS_CLIENTS; i++) {
            if (clients[i].connected()) {
                while (clients[i].available()) {
                    handleInputChar((char)clients[i].read(), i);
                }
            }
        }
    }

    void sendToClients(const String& packet) {
        if (!tncServer) return;
        String kissFrame = KISS_Utils::encodeKISS(packet);
        for (int i = 0; i < MAX_KISS_CLIENTS; i++) {
            if (clients[i].connected()) {
                clients[i].print(kissFrame);
            }
        }
    }

} // namespace TCP_KISS_Utils

#endif // HAS_WIFI
