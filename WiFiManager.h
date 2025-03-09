#ifndef WIFIMANAGER_H
#define WIFIMANAGER_H

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>

typedef void (*WiFiEventCallback)();  // Define a function pointer type for the event callback

class WiFiManager {
public:
    WiFiManager(const char* hostname, uint16_t port, WiFiEventCallback callback);
    void begin();
    static void WiFiEvent(WiFiEvent_t event);  // Static WiFi event handler
    WiFiClient& getClient();
    WiFiServer& getServer();
    void waitForClient();

private:
    const char* hostname;
    uint16_t port;
    WiFiServer server;
    WiFiClient client;
    static WiFiEventCallback eventCallback;  // Static function pointer to store the callback
};

#endif
