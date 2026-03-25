#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <TrainScope_Object_Detection_inferencing.h>
#include <Wire.h>
#include <vl53l8cx.h>

#include "esp_camera.h"
<<<<<<< HEAD
#include <TrainScope_Object_Detection_inferencing.h>

#include "esp_heap_caps.h"

// NEU: Bibliotheken für WLAN und Webserver
#include <WiFi.h>
#include <esp_http_server.h>
    =======
>>>>>>> 7cbfbfc8236a4bd182b2caaba408027ee442dd46

    VL53L8CX sensor_vl53l8cx(&Wire, -1, -1);
uint16_t readings[12];

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

camera_fb_t *fb = NULL;

// ==========================================
// WEBSERVER & STREAMING LOGIK
// ==========================================
httpd_handle_t camera_httpd = NULL;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// HTML Seite, die das Video anzeigt (wird künstlich auf 400x400 vergrößert)
static esp_err_t index_handler(httpd_req_t *req) {
    const char *html =
        "<html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"></head>"
        "<body style=\"text-align:center; font-family:Arial; background:#222; color:#fff;\">"
        "<h1>TrainSense Live</h1>"
        "<img src=\"/stream\" style=\"width:400px; height:400px; image-rendering: pixelated; border: 2px solid #fff; "
        "transform: rotate(-90deg);\">"
        "</body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

// Handler für den eigentlichen MJPEG Video Stream
static esp_err_t stream_handler(httpd_req_t *req) {
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char part_buf[64];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    while (true) {
        camera_fb_t *fb_stream = esp_camera_fb_get();
        if (!fb_stream) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        // Konvertiere das RGB565 Bild der KI zu einem JPEG für den Browser
        bool jpeg_converted = frame2jpg(fb_stream, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb_stream);

        if (jpeg_converted) {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
            if (res == ESP_OK) {
                size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
                res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
            }
            if (res == ESP_OK) {
                res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
            }
            free(_jpg_buf);
            _jpg_buf = NULL;
        }

        if (res != ESP_OK) break;

        // WICHTIG: Kurze Pause, damit die Haupt-Schleife (KI + ToF) Zeit bekommt, ein Bild zu machen
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    return res;
}

void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_uri_t index_uri = {.uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL};
    httpd_uri_t stream_uri = {.uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL};

    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &stream_uri);
    }
}
// ==========================================

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

        // Option 3: 270 Degrees (90 Degrees Counter-Clockwise)
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
    Serial.println("\n\n--- Starting ESP32-CAM Edge Impulse Object Detection ---");

    if (!psramFound()) {
        Serial.println("[CRITICAL ERROR] PSRAM not found or not enabled!");
        while (true) {
            delay(1000);
        }
    }

    // ==========================================
    // NEU: WLAN Access Point aufbauen
    // ==========================================
    Serial.println("Starte WLAN Access Point...");
    WiFi.softAP("TrainSense_Video", "12345678");  // WLAN Name und Passwort
    IPAddress IP = WiFi.softAPIP();
    Serial.print("WLAN gestartet! Verbinde dich mit 'TrainSense_Video' und öffne im Browser: http://");
    Serial.println(IP);

    startCameraServer();  // Webserver starten
    // ==========================================

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
    config.fb_count = 2;  // WICHTIG GEÄNDERT: 2 Framebuffer, damit KI und WLAN gleichzeitig arbeiten können
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    Serial.println("Init kamera...");
    if (esp_camera_init(&config) != ESP_OK) {
        Serial.println("Kamera Init fehlgeschlagen!");
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
    pAdvertising->setMinPreferred(0x0);
    pAdvertising->setMinPreferred(0x1F);
    BLEDevice::startAdvertising();

    Serial.println("Setup erfolgreich abgeschlossen.");
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

    if (in_cooldown) {
        if (millis() - cooldown_timer > COOLDOWN_TIME) {
            in_cooldown = false;
            Serial.println("Cooldown vorbei! Achte wieder auf Stoppschilder.");
        }
    }

    Wire.setClock(1000000);
    status = sensor_vl53l8cx.check_data_ready(&NewDataReady);
    Wire.setClock(100000);

    // BILD HOLEN - Läuft jetzt immer!
    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Kamera Fehler");
        delay(100);
        return;
    }

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

        if (min_distance <= 100) {
            tof_emergency = true;
            Serial.println("ToF NOTSTOPP!");
        }
    }

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = &raw_feature_get_data;

    ei_impulse_result_t result = {0};
    EI_IMPULSE_ERROR ei_err = run_classifier(&signal, &result, false);

    if (ei_err != EI_IMPULSE_OK) {
        Serial.printf("[ERROR] Failed to run classifier (%d)\n", ei_err);
        esp_camera_fb_return(fb);
        return;
    }

    for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
        ei_impulse_result_bounding_box_t bb = result.bounding_boxes[i];
        if (bb.value == 0) continue;

        Serial.printf("  Found %s (Confidence: %.2f)\n", bb.label, bb.value);

        if (strcmp(bb.label, "stop") == 0) {
            if (!is_stopped_by_sign && !in_cooldown) {
                is_stopped_by_sign = true;
                stop_timer = millis();
                Serial.println("Stoppschild erkannt! Halte für 5 Sekunden...");
            } else if (in_cooldown) {
                Serial.println("Sehe Stoppschild, bin im Cooldown -> Ignoriert!");
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
            Serial.println("Stopp vorbei. 4s Cooldown gestartet.");
        }
    }

    if (tof_emergency) {
        final_ble_value = 0;
    }

    // BLE Wert immer intern setzen, aber nur senden, wenn verbunden!
    pCharacteristic->setValue(&final_ble_value, 1);
    if (deviceConnected) {
        pCharacteristic->notify();
    }

    // Kurze Pause, damit der ESP nicht überlastet und der Webserver atmen kann
    delay(100);
}