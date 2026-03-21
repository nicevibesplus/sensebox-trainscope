#include <Adafruit_AS7341.h>
#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Wire.h>
#include <vl53l8cx.h>

#include "esp_camera.h"

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

    if (!as7341.begin()) {  // Default address and I2C port
        Serial.println("Could not find AS7341");
        while (1) {
            delay(10);
        }
    }

    as7341.setATIME(50);
    as7341.setASTEP(500);  // This combination of ATIME and ASTEP gives an integration time of about 1sec, so with two
                           // integrations, that's 2 seconds for a complete set of readings
    as7341.setGain(AS7341_GAIN_256X);

    as7341.startReading();

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.frame_size = FRAMESIZE_QVGA;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = 4;
    config.fb_count = 1;

    if (config.pixel_format == PIXFORMAT_JPEG) {
        if (psramFound()) {
            config.jpeg_quality = 4;
            config.fb_count = 1;
            config.frame_size = FRAMESIZE_VGA;
            config.grab_mode = CAMERA_GRAB_LATEST;
        } else {
            config.frame_size = FRAMESIZE_QVGA;
            config.fb_location = CAMERA_FB_IN_DRAM;
        }
    } else {
        config.frame_size = FRAMESIZE_240X240;
        config.fb_count = 2;
    }

    Serial.println("init camera");
    delay(3);
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x", err);
        return;
    }

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
            } else {
                pCharacteristic->setValue(2);
            }

            /*
            while (!as7341.checkReadingProgress()) {
                delay(10);
            }
            if (!as7341.readAllChannels()){
                Serial.println("Error reading all channels!");
                return;
            }

            // Werte holen
            uint16_t red = as7341.getChannel(AS7341_CHANNEL_630nm_F7);   // Rot
            uint16_t green = as7341.getChannel(AS7341_CHANNEL_515nm_F4); // Grün
            uint16_t blue = as7341.getChannel(AS7341_CHANNEL_480nm_F3);  // Blau



            if (red > green && red > blue) {
                Serial.println("Detected Red");
            }
            else if (green > red && green > blue) {
                // Red is the strongest channel
                Serial.println("Detected green");
                pCharacteristic->setValue(0);
            }
            else if (blue > red && blue > green) {
                // Green is the strongest channel
                Serial.println("Detected blue");
                // pCharacteristic->setValue(1); // Example for other colors
            }
            pCharacteristic->notify();

        as7341.startReading();
        */
        }

        pCharacteristic->notify();
        delay(100);
    }
}
