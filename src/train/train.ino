#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Wire.h>
#include <vl53l8cx.h>
#include <Adafruit_AS7341.h>


VL53L8CX sensor_vl53l8cx(&Wire, -1, -1);

Adafruit_AS7341 as7341;

uint16_t readings[12];


// BLE Server name (this device)
#define bleServerName "TrainSense Server"

BLECharacteristic *pCharacteristic;
BLECharacteristic *pMessage;
bool deviceConnected = false;
String myMessage;

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

    if (!as7341.begin()){ //Default address and I2C port
        Serial.println("Could not find AS7341");
        while (1) { delay(10); }
    }
    
    as7341.setATIME(50);
    as7341.setASTEP(500); //This combination of ATIME and ASTEP gives an integration time of about 1sec, so with two integrations, that's 2 seconds for a complete set of readings
    as7341.setGain(AS7341_GAIN_256X);
    
    as7341.startReading();

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

            if (min_distance <= 50) {
                pCharacteristic->setValue(0);
                pCharacteristic->notify();
            } else {
                pCharacteristic->setValue(2);
            }

            while (!as7341.checkReadingProgress()) {
                delay(10);
            }
            if (!as7341.readAllChannels()){
                Serial.println("Error reading all channels!");
                return;
            }

            // Werte holen
            uint16_t red = as7341.getChannel(AS7341_CHANNEL_630nm_F7);   // Rot
            uint16_t green = as7341.getChannel(AS7341_CHANNEL_555nm_F5); // Grün
            uint16_t blue = as7341.getChannel(AS7341_CHANNEL_480nm_F3);  // Blau

            uint16_t maxVal = max(red, max(green, blue));

            if (maxVal == 0) maxVal = 1;

            uint8_t R = (uint32_t)red * 255 / maxVal;
            uint8_t G = (uint32_t)green * 255 / maxVal;
            uint8_t B = (uint32_t)blue * 255 / maxVal;

            const uint16_t threshold = 100; 

            if (maxVal < threshold) {
                Serial.println("No Color Detected (Too Dark)");
            } 
            else if (R > G && R > B) {
                // Red is the strongest channel
                Serial.println("Detected Red");
                pCharacteristic->setValue(0); 
            } 
            else if (G > R && G > B) {
                // Green is the strongest channel
                Serial.println("Detected Green");
                // pCharacteristic->setValue(1); // Example for other colors
            } 
            else if (B > R && B > G) {
                // Blue is the strongest channel
                Serial.println("Detected Blue");
                // pCharacteristic->setValue(2);
            }
            pCharacteristic->notify();
        }

        as7341.startReading();

        delay(100);
    }
}
