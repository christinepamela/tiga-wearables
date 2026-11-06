// ============================================================
// tiga_ble.h — BLE telemetry for TIGA v8
// ============================================================
// Service UUID:   4fafc201-1fb5-459e-8fcc-c5c9c331914b
// Characteristic: beb5483e-36e1-4688-b7f5-ea07361b26a8  (notify, 1 Hz)
//
// 20-byte packet, little-endian. Bytes 0-15 are the same as v6a/v7
// so the old PWA still reads them; v8 uses the reserved bytes.
//   [0]     HR            uint8   bpm (0 = no reading)
//   [1]     SpO2          uint8   %   (0 = no reading)
//   [2]     SpO2 valid    uint8   0|1
//   [3-4]   Steps         uint16
//   [5-6]   Altitude x10  int16   metres, relative to start
//   [7]     Floors up     uint8
//   [8]     Battery       uint8   %
//   [9]     Worn          uint8   0|1
//   [10]    Falls         uint8
//   [11]    Stable        uint8   0|1
//   [12]    GPS fix       uint8   0|1
//   [13]    GPS sats      uint8
//   [14-15] Pressure x10  uint16  hPa
//   [16]    Protocol ver  uint8   8
//   [17-18] Piezo taps    uint16
//   [19]    Status bits   uint8   b0 MPU, b1 MAX30102, b2 BMP280,
//                                  b3 piezo, b4 flash log OK
// ============================================================
#pragma once
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define TIGA_SERVICE_UUID    "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define TIGA_DATA_CHAR_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define TIGA_PROTOCOL_VER    8

BLEServer*         pServer      = nullptr;
BLECharacteristic* pDataChar    = nullptr;
bool               bleConnected = false;

class TIGAServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pSvr) override {
    bleConnected = true;
    Serial.println("[BLE] Client connected");
  }
  void onDisconnect(BLEServer* pSvr) override {
    bleConnected = false;
    Serial.println("[BLE] Client disconnected - advertising again");
    BLEDevice::startAdvertising();
  }
};

void bleSetup() {
  BLEDevice::init("TIGA-1");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new TIGAServerCallbacks());

  BLEService* pService = pServer->createService(TIGA_SERVICE_UUID);
  pDataChar = pService->createCharacteristic(
    TIGA_DATA_CHAR_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  pDataChar->addDescriptor(new BLE2902());   // required for notify
  pService->start();

  BLEAdvertising* pAdv = BLEDevice::getAdvertising();
  pAdv->addServiceUUID(TIGA_SERVICE_UUID);
  pAdv->setScanResponse(true);
  pAdv->setMinPreferred(0x06);   // helps iPhone connections
  pAdv->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Advertising as TIGA-1");
}

void bleNotify() {
  if (!bleConnected) return;

  uint8_t pkt[20] = {0};

  pkt[0] = (uint8_t)constrain((int)data.heartRate, 0, 255);
  pkt[1] = (uint8_t)constrain((int)data.spO2, 0, 100);
  pkt[2] = data.spO2Valid ? 1 : 0;

  uint16_t steps = (uint16_t)constrain(data.steps, 0, 65535);
  pkt[3] = steps & 0xFF;
  pkt[4] = (steps >> 8) & 0xFF;

  int16_t altX10 = (int16_t)constrain((int)(data.altitudeM * 10), -32768, 32767);
  pkt[5] = altX10 & 0xFF;
  pkt[6] = ((uint16_t)altX10 >> 8) & 0xFF;

  pkt[7]  = (uint8_t)constrain(data.floorsUp, 0, 255);
  pkt[8]  = (uint8_t)constrain((int)data.battery, 0, 100);
  pkt[9]  = data.wearing ? 1 : 0;
  pkt[10] = (uint8_t)constrain(daily.fallCount, 0, 255);
  pkt[11] = data.isStable ? 1 : 0;
  pkt[12] = gpsData.hasFix ? 1 : 0;
  pkt[13] = (uint8_t)constrain(gpsData.satellites, 0, 255);

  uint16_t presX10 = (uint16_t)constrain((int)(data.pressureHPa * 10), 0, 65535);
  pkt[14] = presX10 & 0xFF;
  pkt[15] = (presX10 >> 8) & 0xFF;

  pkt[16] = TIGA_PROTOCOL_VER;
  uint16_t taps = (uint16_t)constrain(piezoTapCount, 0, 65535);
  pkt[17] = taps & 0xFF;
  pkt[18] = (taps >> 8) & 0xFF;
  pkt[19] = (mpuOK ? 1 : 0) | (maxOK ? 2 : 0) | (bmpOK ? 4 : 0) |
            (piezoOK ? 8 : 0) | (logOK ? 16 : 0);

  pDataChar->setValue(pkt, 20);
  pDataChar->notify();
}
