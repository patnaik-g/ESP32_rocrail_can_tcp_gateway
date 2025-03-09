#ifndef WIFIMANAGER_H
#define WIFIMANAGER_H

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>

typedef void (*WiFiEventCallback)();

class WiFiManager {
public:
    WiFiManager(const char* hostname, uint16_t port, WiFiEventCallback callback = nullptr);
    void begin();
    WiFiClient& getClient();
    WiFiServer& getServer();
    void waitForClient();

private:
    static void WiFiEvent(WiFiEvent_t event);
    void startAPMode();
    bool connectToWiFi(const String &ssid, const String &password);

    const char* hostname;
    uint16_t port;
    WiFiServer server;
    WiFiClient client;
    Preferences prefs;
    WebServer webServer = WebServer(80);
    static WiFiEventCallback eventCallback;
};

#endif
