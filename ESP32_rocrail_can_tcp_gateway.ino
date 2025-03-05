
/*
   rocrail_CAN_TCP_GW

    Programme permettant le pilotage de locomotives à partir de Rocrail©
    et la mise a jour de la liste des locomotives en MFX en utilisant une liaison TCP (WiFi ou Ethernet).
*/

#define PROJECT "rocrail_can_tcp_gateway"
#define VERSION "1.3.1_V6_working"
#define AUTHOR "Christophe BOBILLE - www.locoduino.org"

//----------------------------------------------------------------------------------------
//  Board Check
//----------------------------------------------------------------------------------------

#ifndef ARDUINO_ARCH_ESP32
#error "Select an ESP32 board"
#endif

//----------------------------------------------------------------------------------------
//   Include files
//----------------------------------------------------------------------------------------

#include <ACAN_ESP32.h> // https://github.com/pierremolinaro/acan-esp32.git
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

//#define VERBOSE
#define debug Serial
#define LED_BUILTIN 2

//----------------------------------------------------------------------------------------
//  CAN Desired Bit Rate
//----------------------------------------------------------------------------------------

static const uint32_t DESIRED_BIT_RATE = 250UL * 1000UL; // Marklin CAN baudrate = 250Kbit/s

//----------------------------------------------------------------------------------------
//  Buffers  : Rocrail always send 13 bytes
//----------------------------------------------------------------------------------------

static const uint8_t BUFFER_SIZE = 13;
byte cBuffer[BUFFER_SIZE]; // CAN buffer
byte sBuffer[BUFFER_SIZE]; // Serial buffer

//----------------------------------------------------------------------------------------
//  Marklin hash
//----------------------------------------------------------------------------------------

uint16_t rrHash; // for Rocrail hash

//----------------------------------------------------------------------------------------
//  WIFI
//----------------------------------------------------------------------------------------
#include <WiFi.h>
#include <ESPmDNS.h>
#define NO_OTA_PORT
#include <ArduinoOTA.h>
const char *ssid = "patnaik";
const char *password = "2010Equinox!";
const char *hostname = "Gleisbox";
const uint port = 15731;
WiFiServer server(port);
WiFiClient client;

//----------------------------------------------------------------------------------------
//  WiFi Event Handler (Replaces wifiMonitorTask)
//----------------------------------------------------------------------------------------

void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case WIFI_EVENT_STA_DISCONNECTED:
            debug.println("WiFi disconnected! Attempting to reconnect...");
            WiFi.reconnect();
            break;
        case IP_EVENT_STA_GOT_IP:
            debug.print("WiFi reconnected. IP Address: ");
            debug.println(WiFi.localIP());
            break;
        default:
            break;
    }
}

//----------------------------------------------------------------------------------------
//  Queues
//----------------------------------------------------------------------------------------

QueueHandle_t canToTcpQueue;
QueueHandle_t tcpToCanQueue;
#ifdef VERBOSE
QueueHandle_t debugQueue; // Queue for debug messages
#endif
//----------------------------------------------------------------------------------------
//  Debug declaration
//----------------------------------------------------------------------------------------

void debugFrame(const CANMessage *);

//----------------------------------------------------------------------------------------
//  Tasks
//----------------------------------------------------------------------------------------

void CANReceiveTask(void *pvParameters);
void TCPSendTask(void *pvParameters);
void TCPReceiveTask(void *pvParameters);
void CANSendTask(void *pvParameters);
#ifdef VERBOSE
void debugFrameTask(void *pvParameters); // Debug task
#endif
//----------------------------------------------------------------------------------------
//   SETUP
//----------------------------------------------------------------------------------------

void setup()
{
  //--- Start serial
  Serial.begin(115200); // For debug
  while (!Serial)
  {
    delay(100);
  }
  Serial.println("Rocrail-Can Gateway started!");
  debug.printf("\nProject  : %s", PROJECT);
  debug.printf("\nVersion  : %s", VERSION);
  debug.printf("\nAuthor   : %s", AUTHOR);
  //debug.printf("\nFichier   :    %s", __FILE__);
  debug.printf("\nCompiled : %s", __DATE__);
  debug.printf(" - %s\n\n", __TIME__);
  debug.printf("-----------------------------------\n\n");

  //--- Configure ESP32 CAN
  ACAN_ESP32_Settings settings(DESIRED_BIT_RATE);
  settings.mDriverReceiveBufferSize = 50;
  settings.mDriverTransmitBufferSize = 50;
  settings.mRxPin = GPIO_NUM_25; // Optional, default Rx pin is GPIO_NUM_5
  settings.mTxPin = GPIO_NUM_32; // Optional, default Tx pin is GPIO_NUM_4
  const uint32_t errorCode = ACAN_ESP32::can.begin(settings);
  if (errorCode)
  {
    debug.print("Configuration error 0x");
    debug.println(errorCode, HEX);
  }
  else
    debug.print("Configuration CAN OK\n\n");

  WiFi.setHostname("Gleisbox");
  WiFi.mode(WIFI_STA);
  WiFi.onEvent(WiFiEvent); // Register the WiFi event handler
  WiFi.begin(ssid, password);
  debug.print("Waiting for WiFi connection");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    debug.print(".");
  }
  ArduinoOTA.begin();
  // Initialize mDNS
  while (!MDNS.begin(hostname)) {   // Set the hostname to "esp32.local"
    debug.println("Error setting up MDNS responder!");
    delay(500);
  }
  MDNS.addService("mbus","tcp",port);
  MDNS.addService("arduino","tcp",3232);
  debug.print("\nWiFi connected, IP address: ");
  debug.print(WiFi.localIP());
  debug.printf(", Port: %d\nHostname: ", port);
  debug.print(hostname);
  debug.println(".local");
  server.begin();

  uint8_t led = LOW;
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, led);

  while (!client) {   // listen for incoming clients
    client = server.available();
    if (client) break;
    ArduinoOTA.handle();
    debug.print("\n\nWaiting for connection from Rocrail... ");
    delay(200);
    led = not led;
    digitalWrite(LED_BUILTIN, led);
  }
// loop while the client's connected
  debug.print("connected, wating on packets..");
  int16_t rb = 0; //!\ Do not change type int16_t See https://www.arduino.cc/reference/en/language/functions/communication/stream/streamreadbytes/
  while (rb != BUFFER_SIZE)
  {
    if (client.available()) { // if there's bytes to read from the client,
      rb = client.readBytes(cBuffer, BUFFER_SIZE);
      MDNS.end();       // done with mDNS
      break;
    }
    delay(1000);
    led = not led;
    digitalWrite(LED_BUILTIN, led);
    debug.print(".");
  }
  digitalWrite(LED_BUILTIN, HIGH);
  rrHash = ((cBuffer[2] << 8) | cBuffer[3]);   // extract the Rocrail hash
  debug.printf("\nRocral hash: 0x%04X\n",rrHash);
  CANMessage frame;
  frame.id = (cBuffer[0] << 24) | (cBuffer[1] << 16) | rrHash;
  frame.ext = true;
  frame.len = cBuffer[4];
  for (byte i = 0; i < frame.len; i++)
    frame.data[i] = cBuffer[i + 5];

  bool isSent = false;
  uint8_t attempts = 0;
  constexpr uint8_t MAX_RETRIES = 5;
  while (!isSent && attempts < MAX_RETRIES) {
    isSent = ACAN_ESP32::can.tryToSend(frame);
    ++attempts;
    delay(100);
  }
  if (isSent) {
    debugFrame(&frame);
  }
  else {
    constexpr uint16_t RESTART_DELAY_MS = 10000;
    debug.println("CAN frame failed to send.");
    debug.println("ESP32 will restart in 10 seconds.");
    debug.println("/!\\ Relaunch Rocrail.");
    delay(RESTART_DELAY_MS);
    ESP.restart();
  }
  // Create queues
  canToTcpQueue = xQueueCreate(100, sizeof(CANMessage));
  tcpToCanQueue = xQueueCreate(100, BUFFER_SIZE * sizeof(byte));
#ifdef VERBOSE
  debugQueue = xQueueCreate(100, sizeof(CANMessage)); // Create debug queue
#endif
  // Create tasks
  xTaskCreatePinnedToCore(CANReceiveTask, "CANReceiveTask", 4 * 1024, NULL, 3, NULL, 0); // priority 3 on core 0
  xTaskCreatePinnedToCore(TCPSendTask, "TCPSendTask", 4 * 1024, NULL, 5, NULL, 1);       // priority 5 on core 1
  xTaskCreatePinnedToCore(TCPReceiveTask, "TCPReceiveTask", 4 * 1024, NULL, 3, NULL, 1); // priority 3 on core 1
  xTaskCreatePinnedToCore(CANSendTask, "CANSendTask", 4 * 1024, NULL, 5, NULL, 0);       // priority 5 on core 0
#ifdef VERBOSE
  xTaskCreatePinnedToCore(debugFrameTask, "debugFrameTask", 2 * 1024, NULL, 1, NULL, 1); // debug task with priority 1 on core 1
#endif
} // end setup

//----------------------------------------------------------------------------------------
//   LOOP
//----------------------------------------------------------------------------------------

void loop()
{
  vTaskDelete(NULL);
} // Nothing to do

//----------------------------------------------------------------------------------------
//   CANReceiveTask
//----------------------------------------------------------------------------------------

void CANReceiveTask(void *pvParameters)
{
  CANMessage frameIn;
  while (true)
  {
    if (ACAN_ESP32::can.receive(frameIn))
    {
      xQueueSend(canToTcpQueue, &frameIn, portMAX_DELAY);
#ifdef VERBOSE
      xQueueSend(debugQueue, &frameIn, 10); // send to debug queue
#endif
    } else {
        vTaskDelay(1); // Avoid busy-waiting
    }
  }
}

//----------------------------------------------------------------------------------------
//   TCPSendTask
//----------------------------------------------------------------------------------------

void TCPSendTask(void *pvParameters)
{
  CANMessage frameIn;
  while (true)
  {
    if (xQueueReceive(canToTcpQueue, &frameIn, portMAX_DELAY))
    {
      if (client.connected()) {
        sBuffer[0] = (frameIn.id & 0xFF000000) >> 24;
        sBuffer[1] = (frameIn.id & 0xFF0000) >> 16;
        sBuffer[2] = (frameIn.id & 0xFF00) >> 8; // hash
        sBuffer[3] = (frameIn.id & 0x00FF);      // hash
        sBuffer[4] = frameIn.len;
        memcpy(sBuffer+5, frameIn.data, 8);
        client.write(sBuffer, BUFFER_SIZE);
      }
      else {  // connection to Rocrail broken
        delay(5000);
        if (!client.connected()) {
          debug.print("\n\n   RESTART ROCRAIL!\n\n");
          ESP.restart();
        }
      }
    }
  }
}

//----------------------------------------------------------------------------------------
//   TCPReceiveTask
//----------------------------------------------------------------------------------------

void TCPReceiveTask(void *pvParameters)
{
  while (true)
  {
    // Check if the client is connected and has at least BUFFER_SIZE bytes available
    if (client.connected() && client.available() >= BUFFER_SIZE)
    {
      // Read exactly BUFFER_SIZE bytes into cBuffer
      if (client.readBytes(cBuffer, BUFFER_SIZE) == BUFFER_SIZE)
      {
        // Send the data to the CAN queue
        xQueueSend(tcpToCanQueue, cBuffer, portMAX_DELAY);
      }
    } else {
      // Reduce delay for faster processing but still yield CPU time
      vTaskDelay(1); // 1 ms delay for better responsiveness
    }
  }
}

//----------------------------------------------------------------------------------------
//   CANSendTask
//----------------------------------------------------------------------------------------

void CANSendTask(void *pvParameters)
{
  byte buffer[BUFFER_SIZE];
  while (true)
  {
    if (xQueueReceive(tcpToCanQueue, &buffer, pdMS_TO_TICKS(5)) == pdPASS) 
    {
      CANMessage frameOut;
      frameOut.id = (buffer[0] << 24) | (buffer[1] << 16) | rrHash;
      frameOut.ext = true;
      frameOut.len = buffer[4];
      // Directly copy the data to frameOut using memcpy for better performance
      memcpy(frameOut.data, &buffer[5], frameOut.len);
      const bool ok = ACAN_ESP32::can.tryToSend(frameOut);
#ifdef VERBOSE
      xQueueSend(debugQueue, &frameOut, 10); // send to debug queue
#endif
    }
  }
}

//----------------------------------------------------------------------------------------
//   debugFrameTask
//----------------------------------------------------------------------------------------
#ifdef VERBOSE
void debugFrameTask(void *pvParameters)
{
  CANMessage frame;
  while (true)
  {
    if (xQueueReceive(debugQueue, &frame, portMAX_DELAY))
    {
      debugFrame(&frame);
    }
  }
}
#endif
//----------------------------------------------------------------------------------------
//   debugFrame
//----------------------------------------------------------------------------------------

void debugFrame(const CANMessage *frame)
{
  uint16_t hash = frame->id & 0xFFFF;
  debug.printf("%-1s|H:%04X|R:%-1s|C:%02X|L:%d|", 
               (hash == rrHash) ? "T" : "C",
               hash,
               (frame->id & 0x10000) ? "T" : "F",
               (frame->id & 0x1FE0000) >> 17,
               frame->len);

  for (byte i = 0; i < frame->len; i++)
  {
    debug.printf("%02X ", frame->data[i]);  // Two-character width for hex values
  }
  debug.println(); // Ensures proper line termination
}
