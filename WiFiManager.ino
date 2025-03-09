#include "WiFiManager.h"

#ifndef debug
#define debug Serial
#endif

// Define the static callback function pointer
WiFiEventCallback WiFiManager::eventCallback = nullptr;

WiFiManager::WiFiManager(const char* hostname, uint16_t port, WiFiEventCallback callback)
  : hostname(hostname), server(port), port(port) {
  eventCallback = callback;  // Store the callback function
}

void WiFiManager::begin() {
  WiFi.setHostname(hostname);  // Set the hostname before WiFi starts
  WiFi.onEvent(WiFiEvent);  // Register static function as event handler
  WiFi.begin();  // No default credentials provided
  debug.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    debug.print(".");
  }
  debug.println("\nWiFi connected!");
  ArduinoOTA.begin();

  // Initialize mDNS
  while (!MDNS.begin(hostname)) {  // Set the hostname
    debug.println("Error setting up MDNS responder!");
    delay(500);
  }
  MDNS.addService("mbus", "tcp", port);
  debug.print("\nWiFi connected, IP address: ");
  debug.print(WiFi.localIP());
  debug.printf(", Port: %d\nHostname: ", port);
  debug.print(hostname);
  debug.println(".local");
  server.begin();
}

// Static WiFi event handler
void WiFiManager::WiFiEvent(WiFiEvent_t event) {
  if (event == WIFI_EVENT_STA_DISCONNECTED) {
    debug.println("WiFi disconnected! Calling callback...");
    if (eventCallback) {
      eventCallback();  // Call the provided callback function
    }
  }
}

WiFiClient& WiFiManager::getClient() {
  return client;
}

WiFiServer& WiFiManager::getServer() {
  return server;
}

void WiFiManager::waitForClient() {
  debug.println("Waiting for client connection...");
  uint8_t led = LOW;
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, led);
  while (!client) {
    client = server.available();
    ArduinoOTA.handle();  // Allow OTA updates before connection
    delay(200);
    led = !led;
    digitalWrite(LED_BUILTIN, led);
  }
  Serial.println("Client connected!");
  MDNS.end();
}
