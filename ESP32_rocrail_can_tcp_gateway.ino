
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

#include <ACAN_ESP32.h>  // https://github.com/pierremolinaro/acan-esp32.git
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#define VERBOSE
#define debug Serial
#define LED_BUILTIN 2

//----------------------------------------------------------------------------------------
//  CAN Desired Bit Rate
//----------------------------------------------------------------------------------------

static const uint32_t DESIRED_BIT_RATE = 250UL * 1000UL;  // Marklin CAN baudrate = 250Kbit/s

//----------------------------------------------------------------------------------------
//  Buffers  : Rocrail always send 13 bytes
//----------------------------------------------------------------------------------------

static const uint8_t BUFFER_SIZE = 13;
byte cBuffer[BUFFER_SIZE];  // CAN buffer
byte sBuffer[BUFFER_SIZE];  // Serial buffer

//----------------------------------------------------------------------------------------
//  Marklin hash
//----------------------------------------------------------------------------------------

uint16_t rrHash;  // for Rocrail hash

//----------------------------------------------------------------------------------------
//   Restart ESP32
//----------------------------------------------------------------------------------------

void restart() {
  debug.println("Restarting...");
  ESP.restart();
}

//----------------------------------------------------------------------------------------
//  WIFI
//----------------------------------------------------------------------------------------

#include "WiFiManager.h"
const char *hostname = "Gleisbox";
#define PORT 15731
WiFiManager wifiManager(hostname, PORT, restart);
WiFiClient &client = wifiManager.getClient();

//----------------------------------------------------------------------------------------
//  Logger
//----------------------------------------------------------------------------------------

#include "logger.h"  // This needs to be done aftr WiFiManager.h

//----------------------------------------------------------------------------------------
//  Queues
//----------------------------------------------------------------------------------------

QueueHandle_t canToTcpQueue;
QueueHandle_t tcpToCanQueue;
#ifdef VERBOSE
QueueHandle_t debugQueue;  // Queue for debug messages
#endif
//----------------------------------------------------------------------------------------
//  Debug declaration
//----------------------------------------------------------------------------------------

void debugFrame(const CANMessage *);

//----------------------------------------------------------------------------------------
//  Tasks
//----------------------------------------------------------------------------------------

void CANHandlerTask(void *pvParameters);
void TCPSendTask(void *pvParameters);
void TCPReceiveTask(void *pvParameters);
#ifdef VERBOSE
void debugFrameTask(void *pvParameters);  // Debug task
#endif
//----------------------------------------------------------------------------------------
//   SETUP
//----------------------------------------------------------------------------------------

void setup() {
  //--- Turn off bluetooth, not used
  btStop();
  //--- Start serial
  Serial.begin(115200);  // For debug
  while (!Serial) {
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
  settings.mRxPin = GPIO_NUM_36;  // Optional, default Rx pin is GPIO_NUM_5
  settings.mTxPin = GPIO_NUM_26;  // Optional, default Tx pin is GPIO_NUM_4
  const uint32_t errorCode = ACAN_ESP32::can.begin(settings);
  if (errorCode) {
    debug.print("Configuration error 0x");
    debug.println(errorCode, HEX);
  } else
    debug.print("Configuration CAN OK\n\n");

  // Initialize WiFi and wait for client
  wifiManager.begin();

  // loop while the client's connected
  logger.print("Waiting on packets from Rocrail...");
  bool led = HIGH;
  while (!client.available()) {  // if there's bytes to read from the client,
    delay(200);
    led = not led;
    digitalWrite(LED_BUILTIN, led);
    logger.print(".");
  }
  digitalWrite(LED_BUILTIN, HIGH);

  // Create queues
  canToTcpQueue = xQueueCreate(100, sizeof(CANMessage));
  tcpToCanQueue = xQueueCreate(100, BUFFER_SIZE * sizeof(byte));
#ifdef VERBOSE
  debugQueue = xQueueCreate(100, sizeof(CANMessage));  // Create debug queue
#endif
  // Create tasks
  xTaskCreatePinnedToCore(TCPHandlerTask, "TCPHandlerTask", 4 * 1024, NULL, 5, NULL, 0);  // priority 5 on core 1
  xTaskCreatePinnedToCore(CANHandlerTask, "CANHandlerTask", 4 * 1024, NULL, 5, NULL, 1);  // priority 3 on core 0
 #ifdef VERBOSE
  xTaskCreatePinnedToCore(debugFrameTask, "debugFrameTask", 4 * 1024, NULL, 1, NULL, tskNO_AFFINITY);  // debug task with priority 1 on any core
#endif
}  // end setup

//----------------------------------------------------------------------------------------
//   LOOP
//----------------------------------------------------------------------------------------

void loop() {
  vTaskDelete(NULL);
}  // Nothing to do

//----------------------------------------------------------------------------------------
//   CANHandlerTask
//----------------------------------------------------------------------------------------

void CANHandlerTask(void *pvParameters) {
  CANMessage frame;
  static CANMessage frameOut;
  byte buffer[BUFFER_SIZE];

  while (true) {

    // PRIORITIZE RECEIVING CAN MESSAGES
    while (ACAN_ESP32::can.receive(frame)) {
      xQueueSend(canToTcpQueue, &frame, portMAX_DELAY);
    }

    // ONLY ATTEMPT TO SEND IF NO PENDING RECEIVES
    if (xQueueReceive(tcpToCanQueue, &buffer, 0) == pdPASS) {
      if (!rrHash) {
        rrHash = ((buffer[2] << 8) | buffer[3]);  // extract the Rocrail hash
        logger.printf("\nRocral hash: 0x%04X\n", rrHash);
        frameOut.ext = true;
      }
      frameOut.id = (buffer[0] << 24) | (buffer[1] << 16) | rrHash;
      frameOut.len = buffer[4];
      memcpy(frameOut.data, &buffer[5], frameOut.len);

      ACAN_ESP32::can.tryToSend(frameOut);
#ifdef VERBOSE
      xQueueSend(debugQueue, &frameOut, 10);  // send to debug queue
#endif
    }
    vTaskDelay(1); // YIELD CPU ONLY WHEN NO WORK TO DO
  }
}

//----------------------------------------------------------------------------------------
//   TCPHndlerTask
//----------------------------------------------------------------------------------------

void TCPHandlerTask(void *pvParameters) {
  CANMessage frameIn;

  while (true) {
 
    // Prioritize receiving data
    if (client.available() >= BUFFER_SIZE) {
      if (client.readBytes(cBuffer, BUFFER_SIZE) == BUFFER_SIZE) {
        xQueueSend(tcpToCanQueue, cBuffer, portMAX_DELAY);
      }
      continue; // Immediately check for more data before sending
    }

    if (!client.connected()) {  // connection to Rocrail broken
      logger.println("Rocrail diconnected!");
       delay(5000);
      if (!client.connected()) {
        logger.print("\n\n   RESTART ROCRAIL!\n\n");
        wifiManager.waitForClient();
      }
    }

    // If no data to receive, attempt to send out CAN frames
    if (xQueueReceive(canToTcpQueue, &frameIn, 0)) {  // Non-blocking check
        sBuffer[0] = (frameIn.id & 0xFF000000) >> 24;
        sBuffer[1] = (frameIn.id & 0xFF0000) >> 16;
        sBuffer[2] = (frameIn.id & 0xFF00) >> 8;  // hash
        sBuffer[3] = (frameIn.id & 0x00FF);       // hash
        sBuffer[4] = frameIn.len;
        memcpy(sBuffer + 5, frameIn.data, 8);
        client.write(sBuffer, BUFFER_SIZE);
#ifdef VERBOSE
        xQueueSend(debugQueue, &frameIn, 10);  // send to debug queue
#endif
    } else {
      // Neither receiving nor sending is needed, so yield CPU time
      vTaskDelay(1);
    }
  }
}

//----------------------------------------------------------------------------------------
//   debugFrameTask
//----------------------------------------------------------------------------------------

#ifdef VERBOSE
void debugFrameTask(void *pvParameters) {
  CANMessage frame;
  while (true) {
    if (xQueueReceive(debugQueue, &frame, portMAX_DELAY)) {
      debugFrame(&frame);
    }
  }
}

//----------------------------------------------------------------------------------------
//   debugFrame
//----------------------------------------------------------------------------------------

void debugFrame(const CANMessage *frame) {
  uint16_t hash = frame->id & 0xFFFF;
   // Prepare the base debug string
  char debugStr[30];
  snprintf(debugStr, sizeof(debugStr), 
           "%-1s|H:%04X|R:%-1s|C:%02X|L:%d|",
           (hash == rrHash) ? "T" : "C",
           hash,
           (frame->id & 0x10000) ? "T" : "F",
           (frame->id & 0x1FE0000) >> 17,
           frame->len);
  
  // Print to both Serial and Telnet
  logger.print(debugStr);

  for (byte i = 0; i < frame->len; i++) {
    logger.printf("%02X ", frame->data[i]);  // Two-character width for hex values
  }
  logger.println();  // Ensures proper line termination
}
#endif
