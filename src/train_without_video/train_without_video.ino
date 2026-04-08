#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <TrainScope_Object_Detection_inferencing.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_http_server.h>
#include <vl53l8cx.h>

#include "esp_camera.h"
#include "esp_heap_caps.h"

VL53L8CX sensor_vl53l8cx(&Wire, -1, -1);
uint16_t readings[12];

#define bleServerName "TrainScope Server"

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

camera_fb_t *fb = NULL;

// httpd_handle_t camera_httpd = NULL;

// #define PART_BOUNDARY "123456789000000000000987654321"
// static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
// static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
// static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// static esp_err_t index_handler(httpd_req_t *req) {
//     const char *html = "...";
//     httpd_resp_set_type(req, "text/html");
//     return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
// }

// static esp_err_t stream_handler(httpd_req_t *req) {
//     return ESP_OK;
// }

// void startCameraServer() {}

int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
    size_t pixels_left = length;
    size_t out_ptr_ix = 0;
    size_t current_offset = offset;
    uint16_t *buf = (uint16_t *)fb->buf;

    const int W = 96;
    const int H = 96;

    while (pixels_left != 0) {
        int target_x = current_offset % W;
        int target_y = current_offset / W;
        int source_x, source_y;

        source_x = (H - 1) - target_y;
        source_y = target_x;

        int source_ix = (source_y * W) + source_x;
        uint16_t pixel = buf[source_ix];
        pixel = (pixel >> 8) | (pixel << 8);

        uint8_t r = (pixel >> 8) & 0xF8;
        uint8_t g = (pixel >> 3) & 0xFC;
        uint8_t b = (pixel << 3) & 0xF8;

        uint32_t rgb888 = (r << 16) | (g << 8) | b;
        out_ptr[out_ptr_ix] = (float)rgb888;

        out_ptr_ix++;
        current_offset++;
        pixels_left--;
    }
    return 0;
}

void setup() {
    Serial.begin(115200);
    Serial.println("Start Setup");
    while (!Serial);

    if (!psramFound()) {
        Serial.println("[CRITICAL ERROR] PSRAM not found or not enabled!");
        while (true) {
            delay(1000);
        }
    }

    // Serial.println("Start WIFI...");
    // WiFi.softAP("TrainScope_Video", "12345678");
    // startCameraServer();
    // delay(500);

    Wire.begin();
    Wire.setClock(100000);

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
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    Serial.println("Init camera...");
    if (esp_camera_init(&config) != ESP_OK) {
        Serial.println("Camera Init failed!");
        return;
    }

    BLEDevice::init(bleServerName);
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(BLEUUID((uint16_t)0x181A));
    pCharacteristic = pService->createCharacteristic(BLEUUID((uint16_t)0x2A56), BLECharacteristic::PROPERTY_NOTIFY);

    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(pService->getUUID());
    pAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();

    Serial.println("Setup done.");
}

void loop() {
    VL53L8CX_ResultsData Result;
    uint8_t NewDataReady = 0;
    uint8_t status;

    static uint8_t current_speed_mode = 1;
    static bool is_stopped_by_sign = false;
    static unsigned long stop_timer = 0;

    static bool in_cooldown = false;
    static unsigned long cooldown_timer = 0;
    const unsigned long COOLDOWN_TIME = 4000;

    bool tof_emergency = false;

    if (in_cooldown && millis() - cooldown_timer > COOLDOWN_TIME) {
        in_cooldown = false;
    }

    Wire.setClock(1000000);
    status = sensor_vl53l8cx.check_data_ready(&NewDataReady);
    Wire.setClock(100000);

    fb = esp_camera_fb_get();
    if (!fb) return;

    if ((!status) && (NewDataReady != 0)) {
        Wire.setClock(1000000);
        sensor_vl53l8cx.get_ranging_data(&Result);
        Wire.setClock(100000);

        uint16_t min_distance = 65535;

        Serial.println("\n--- ToF Distance Matrix (mm) ---");

        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 6; x++) {
                int index = (y * 8) + x;
                uint16_t current_dist = Result.distance_mm[index];

                Serial.printf("%4d ", current_dist);

                if (current_dist <= min_distance) {
                    min_distance = current_dist;
                }
            }
            Serial.println();
        }

        if (min_distance <= 150) {
            tof_emergency = true;
            Serial.println("ToF Stop");
        }
    }

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = &raw_feature_get_data;

    ei_impulse_result_t result = {0};
    run_classifier(&signal, &result, false);

    for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
        auto bb = result.bounding_boxes[i];
        if (bb.value == 0) continue;

        if (strcmp(bb.label, "stop") == 0) {
            if (!is_stopped_by_sign && !in_cooldown) {
                is_stopped_by_sign = true;
                stop_timer = millis();
            }
        } else if (strcmp(bb.label, "fast") == 0) {
            current_speed_mode = 2;
        } else if (strcmp(bb.label, "slow") == 0) {
            current_speed_mode = 1;
        }
    }

    esp_camera_fb_return(fb);

    uint8_t final_ble_value = current_speed_mode;

    if (is_stopped_by_sign) {
        if (millis() - stop_timer < 5000) {
            final_ble_value = 0;
        } else {
            is_stopped_by_sign = false;
            in_cooldown = true;
            cooldown_timer = millis();
        }
    }

    if (tof_emergency) {
        final_ble_value = 0;
    }

    pCharacteristic->setValue(&final_ble_value, 1);
    if (deviceConnected) {
        pCharacteristic->notify();
    }

    delay(100);
}