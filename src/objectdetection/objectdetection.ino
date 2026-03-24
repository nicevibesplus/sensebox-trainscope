/* * 1. INCLUDE YOUR EDGE IMPULSE LIBRARY HERE
 * Replace this with the exact name of your downloaded library!
 */
#include <TrainScope_Object_Detection_inferencing.h>

#include "esp_camera.h"

// Pointer to the camera frame buffer
camera_fb_t *fb = NULL;

/*
 * 2. Signal Provider Callback
 */
int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
    size_t pixel_ix = offset;
    size_t pixels_left = length;
    size_t out_ptr_ix = 0;

    uint16_t *buf = (uint16_t *)fb->buf;

    while (pixels_left != 0) {
        // 1. Get the pixel
        uint16_t pixel = buf[pixel_ix];

        // 2. SWAP THE BYTES (ESP32-CAM quirk)
        pixel = (pixel >> 8) | (pixel << 8);

        // 3. Extract RGB channels
        uint8_t r = (pixel >> 8) & 0xF8;
        uint8_t g = (pixel >> 3) & 0xFC;
        uint8_t b = (pixel << 3) & 0xF8;

        // 4. Pack into RGB888 format (0xRRGGBB)
        uint32_t rgb888 = (r << 16) | (g << 8) | b;
        out_ptr[out_ptr_ix] = (float)rgb888;

        out_ptr_ix++;
        pixel_ix++;
        pixels_left--;
    }
    return 0;
}

void setup() {
    Serial.begin(115200);
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
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;

    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = FRAMESIZE_96X96;
    config.jpeg_quality = 12;
    config.fb_count = 1;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[ERROR] Camera init failed with error 0x%x\n", err);
        return;
    }

    // Print Model Info
}

void loop() {
    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("[ERROR] Camera capture failed (fb is NULL)");
        return;
    }

    if (fb->width != EI_CLASSIFIER_INPUT_WIDTH || fb->height != EI_CLASSIFIER_INPUT_HEIGHT) {
        Serial.printf("[ERROR] Mismatch! Frame size (%dx%d) != model size (%dx%d).\n", fb->width, fb->height,
                      EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT);
        esp_camera_fb_return(fb);
        delay(3000);
        return;
    }

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = &raw_feature_get_data;

    ei_impulse_result_t result = {0};

    // Run the classifier
    EI_IMPULSE_ERROR ei_err = run_classifier(&signal, &result, false);

    if (ei_err != EI_IMPULSE_OK) {
        Serial.printf("[ERROR] Failed to run classifier (%d)\n", ei_err);
        esp_camera_fb_return(fb);
        return;
    }

    bool object_found = false;

    for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
        ei_impulse_result_bounding_box_t bb = result.bounding_boxes[i];

        if (bb.value == 0) continue;

        object_found = true;

        Serial.printf("  Found %s (Confidence: %.2f) [ x: %u, y: %u, width: %u, height: %u ]\n", bb.label, bb.value,
                      bb.x, bb.y, bb.width, bb.height);
    }

    esp_camera_fb_return(fb);
}