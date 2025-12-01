#include <RCSwitch.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <WiFi.h>
#include "time.h"

// --- (Все настройки пинов, кодов, Wi-Fi и т.д. остаются прежними) ---
const int receiverPin = 15; 
const int motorPin1 = 14; const int motorPin2 = 27; const int standbyPin = 12;
const unsigned long FORWARD_CODE = 504520; const unsigned long BACKWARD_CODE = 504514; const unsigned long CONFIG_CODE = 504516;
const int motorRunDuration = 3000;
const char* ssid = "..."; const char* pass = "yooooooo";
const char* ntpServer = "pool.ntp.org"; const long gmtOffset_sec = 3 * 3600; const int daylightOffset_sec = 0;
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
// ---

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ---
RCSwitch mySwitch = RCSwitch();
bool isBleActive = false;
bool scheduleEnabled = false;
int openHour = -1, openMinute = -1, closeHour = -1, closeMinute = -1;
unsigned long lastRfTime = 0;
const unsigned long debounceDelay = 1000;
BLEServer *pServer = NULL;
// ---------------------------

// --- ИСПРАВЛЕНИЕ: ОБЪЯВЛЯЕМ КЛАСС И ЭКЗЕМПЛЯР ДО SETUP() ---
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String command = pCharacteristic->getValue();
      command.trim(); command.toUpperCase();
      Serial.print("BLE command received: "); Serial.println(command);
      int spaceIndex = command.indexOf(' '); int colonIndex = command.indexOf(':');
      if (spaceIndex > 0 && colonIndex > spaceIndex) {
        String type = command.substring(0, spaceIndex);
        int hour = command.substring(spaceIndex + 1, colonIndex).toInt();
        int minute = command.substring(colonIndex + 1).toInt();
        if (type == "OPEN") {
          openHour = hour; openMinute = minute; scheduleEnabled = true;
          Serial.printf("New OPEN schedule set: %02d:%02d\n", openHour, openMinute);
        } else if (type == "CLOSE") {
          closeHour = hour; closeMinute = minute; scheduleEnabled = true;
          Serial.printf("New CLOSE schedule set: %02d:%02d\n", closeHour, closeMinute);
        }
      }
    }
};
MyCallbacks bleCallbacks; // Создаем глобальный экземпляр
// -------------------------------------------------------------

// Предварительное объявление функций
void runMotor(bool forward);
void startBluetoothConfig();
void stopBluetoothConfig();

void setup() {
  Serial.begin(115200);
  pinMode(motorPin1, OUTPUT); pinMode(motorPin2, OUTPUT); pinMode(standbyPin, OUTPUT);
  digitalWrite(standbyPin, LOW);
  mySwitch.enableReceive(digitalPinToInterrupt(receiverPin));

  // --- ИНИЦИАЛИЗИРУЕМ BLUETOOTH ОДИН РАЗ ---
  BLEDevice::init("Curtain_Config");
  pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
  // Теперь компилятор знает, что такое bleCallbacks
  pRxCharacteristic->setCallbacks(&bleCallbacks);
  pService->start();
  // ---------------------------------------
  
  Serial.printf("Connecting to %s ", ssid);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println(" CONNECTED");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  Serial.println("--- Curtain Robot v3.4 (BLE Toggle) ---");
  Serial.println("Ready.");
}

void loop() {
  if (isBleActive) {
    if (mySwitch.available()) {
      if (millis() - lastRfTime > debounceDelay) {
        unsigned long receivedCode = mySwitch.getReceivedValue();
        lastRfTime = millis();
        if (receivedCode == CONFIG_CODE) {
          stopBluetoothConfig();
        }
      }
      mySwitch.resetAvailable();
    }
    return;
  }

  if (mySwitch.available()) {
    if (millis() - lastRfTime > debounceDelay) {
      unsigned long receivedCode = mySwitch.getReceivedValue();
      lastRfTime = millis();
      Serial.print("RF Code received: "); Serial.println(receivedCode);
      
      if (receivedCode == FORWARD_CODE) runMotor(true);
      else if (receivedCode == BACKWARD_CODE) runMotor(false);
      else if (receivedCode == CONFIG_CODE) startBluetoothConfig();
    }
    mySwitch.resetAvailable();
  }

  if (scheduleEnabled) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      if (timeinfo.tm_hour == openHour && timeinfo.tm_min == openMinute) {
        runMotor(true); openHour = -1;
      }
      if (timeinfo.tm_hour == closeHour && timeinfo.tm_min == closeMinute) {
        runMotor(false); closeHour = -1;
      }
    }
  }
}

// --- ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ---

void runMotor(bool forward) {
  Serial.printf("Motor %s for %d seconds...\n", forward ? "FORWARD" : "BACKWARD", motorRunDuration / 1000);
  digitalWrite(standbyPin, HIGH);
  if (forward) {
    digitalWrite(motorPin1, HIGH);
    digitalWrite(motorPin2, LOW);
  } else {
    digitalWrite(motorPin1, LOW);
    digitalWrite(motorPin2, HIGH);
  }
  delay(motorRunDuration);
  digitalWrite(standbyPin, LOW);
  Serial.println("Motor stopped.");
}

void startBluetoothConfig() {
  if (isBleActive) return;
  WiFi.mode(WIFI_OFF);
  delay(100);
  Serial.println("Starting BLE advertising...");
  isBleActive = true;
  pServer->getAdvertising()->start();
}

void stopBluetoothConfig() {
  Serial.println("Stopping BLE advertising.");
  pServer->getAdvertising()->stop();
  isBleActive = false;
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(ssid, pass);
  }
}