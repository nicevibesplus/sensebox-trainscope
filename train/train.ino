#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Wire.h>
#include <vl53l8cx.h>

VL53L8CX sensor_vl53l8cx(&Wire, -1, -1);

// BLE Server name (this device)
#define bleServerName "TrainSense Server"

BLECharacteristic *pCharacteristic;
BLECharacteristic *pMessage;
bool deviceConnected = false;
String myMessage;

int light_val = 0;

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *pServer) {
        deviceConnected = true;
    };

    void onDisconnect(BLEServer *pServer) {
        deviceConnected = false;
    }
};

void setup() {
    Serial.begin(115200);
    Serial.println("Start Setup");
    Wire.begin();
    Wire.setClock(100000);  // ToF has max I2C freq of 1MHz

    sensor_vl53l8cx.begin();
    sensor_vl53l8cx.init();
    sensor_vl53l8cx.set_resolution(VL53L8CX_RESOLUTION_8X8);
    sensor_vl53l8cx.set_ranging_frequency_hz(30);
    sensor_vl53l8cx.start_ranging();
    Wire.setClock(100000);

    /*
    if (!hdc.begin()) {
      Serial.println("Couldn't find sensor!");
      while (1);
    }*/

    BLEDevice::init(bleServerName);
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(BLEUUID((uint16_t)0x181A));    // Environmental Sensing
    pCharacteristic = pService->createCharacteristic(BLEUUID((uint16_t)0x2A56),  // Sensor Value
                                                     BLECharacteristic::PROPERTY_NOTIFY);

    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(pService->getUUID());
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x0);
    pAdvertising->setMinPreferred(0x1F);
    BLEDevice::startAdvertising();
}

void loop() {
    if (deviceConnected) {
        VL53L8CX_ResultsData Result;
        uint8_t NewDataReady = 0;
        uint8_t status;

        Wire.setClock(1000000);  // Sensor has max I2C freq of 1MHz
        status = sensor_vl53l8cx.check_data_ready(&NewDataReady);
        Wire.setClock(100000);  // Display has max I2C freq of 100kHz

        if ((!status) && (NewDataReady != 0)) {
            Wire.setClock(1000000);  // Sensor has max I2C freq of 1MHz
            sensor_vl53l8cx.get_ranging_data(&Result);
            Wire.setClock(100000);  // Display has max I2C freq of 100kHz

            int min_index = 0;
            uint16_t min_distance = (long)(&Result)->distance_mm[0];
            for (int i = 0; i < 64; i++) {
                if ((long)(&Result)->distance_mm[i] <= min_distance) {
                    min_index = i;
                    min_distance = (long)(&Result)->distance_mm[i];
                }
            }

            Serial.println(min_distance);

            if (min_distance <= 15) {
                pCharacteristic->setValue(0);
            } else {
                pCharacteristic->setValue(1);
            }

            pCharacteristic->notify();
        }

        delay(100);
    }
}