#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Wire.h>
#include <vl53l8cx.h>
#include "esp_camera.h"
#include <TrainScope_Object_Detection_inferencing.h>

VL53L8CX sensor_vl53l8cx(&Wire, -1, -1);

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
    while (!Serial);
    Serial.println("\n\n--- Starting ESP32-CAM Edge Impulse Object Detection ---");

    // DEBUG: Check PSRAM. If this fails, the model WILL crash!
    if (!psramFound()) {
        Serial.println("[CRITICAL ERROR] PSRAM not found or not enabled!");
        Serial.println("You MUST enable PSRAM in Tools -> PSRAM -> Enabled.");
        while (true) {
            delay(1000);
        }  // Halt execution
    } else {
        Serial.printf("[DEBUG] PSRAM found! Total PSRAM: %d bytes\n", ESP.getPsramSize());
    }

    Wire.begin();
    Wire.setClock(100000);  // ToF has max I2C freq of 1MHz

    sensor_vl53l8cx.begin();
    sensor_vl53l8cx.init();
    sensor_vl53l8cx.set_resolution(VL53L8CX_RESOLUTION_8X8);
    sensor_vl53l8cx.set_ranging_frequency_hz(30);
    sensor_vl53l8cx.start_ranging();
    Wire.setClock(100000);

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
    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = FRAMESIZE_96X96;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;  // Framebuffer in PSRAM für mehr Platz
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    Serial.println("Init kamera...");
    if (esp_camera_init(&config) != ESP_OK) {
        Serial.println("Kamera Init fehlgeschlagen!");
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

    Serial.println("Setup erfolgreich abgeschlossen.");
}

void loop() {
    if (deviceConnected) {
        VL53L8CX_ResultsData Result;
        uint8_t NewDataReady = 0;
        uint8_t status;

        // ... (I2C Clock und Kamera-Check bleibt gleich)
        Wire.setClock(1000000);
        status = sensor_vl53l8cx.check_data_ready(&NewDataReady);
        Wire.setClock(100000);

        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("Kamera Fehler");
            return;
        }

        // Distanz-Sensor Logik (ToF)
        if ((!status) && (NewDataReady != 0)) {
            Wire.setClock(1000000);
            sensor_vl53l8cx.get_ranging_data(&Result);
            Wire.setClock(100000);

            uint16_t min_distance = Result.distance_mm[0];
            for (int i = 0; i < 64; i++) {
                if (Result.distance_mm[i] < min_distance) {
                    min_distance = Result.distance_mm[i];
                }
            }

            // Notstopp durch ToF Sensor (unabhängig von KI)
            if (min_distance <= 50) {
                pCharacteristic->setValue(0);
                pCharacteristic->notify();
                Serial.println("ToF NOTSTOPP!");
            }
        }

        esp_camera_fb_return(fb);
        pCharacteristic->notify();
        delay(100);
    }
}
