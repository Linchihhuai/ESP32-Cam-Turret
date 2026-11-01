#include "cam.h"

#include "esp_log.h"

static const char *TAG = "turret_cam";
static bool initialized;
static bool initialization_attempted;
static esp_err_t initialization_result = ESP_OK;

esp_err_t turret_camera_init(void)
{
    if (initialized) {
        return ESP_OK;
    }
    if (initialization_attempted) {
        return initialization_result;
    }
    initialization_attempted = true;

    const camera_config_t config = {
        .pin_pwdn = CONFIG_CAM_PIN_PWDN,
        .pin_reset = CONFIG_CAM_PIN_RESET,
        .pin_xclk = CONFIG_CAM_PIN_XCLK,
        .pin_sccb_sda = CONFIG_CAM_PIN_SIOD,
        .pin_sccb_scl = CONFIG_CAM_PIN_SIOC,
        .pin_d0 = CONFIG_CAM_PIN_D0,
        .pin_d1 = CONFIG_CAM_PIN_D1,
        .pin_d2 = CONFIG_CAM_PIN_D2,
        .pin_d3 = CONFIG_CAM_PIN_D3,
        .pin_d4 = CONFIG_CAM_PIN_D4,
        .pin_d5 = CONFIG_CAM_PIN_D5,
        .pin_d6 = CONFIG_CAM_PIN_D6,
        .pin_d7 = CONFIG_CAM_PIN_D7,
        .pin_vsync = CONFIG_CAM_PIN_VSYNC,
        .pin_href = CONFIG_CAM_PIN_HREF,
        .pin_pclk = CONFIG_CAM_PIN_PCLK,
        .xclk_freq_hz = 10000000,
        .ledc_timer = LEDC_TIMER_1,
        .ledc_channel = LEDC_CHANNEL_1,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_VGA,
        .jpeg_quality = 12,
        .fb_count = 1,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
        .fb_location = CAMERA_FB_IN_PSRAM,
    };

    initialization_result = esp_camera_init(&config);
    if (initialization_result != ESP_OK) {
        ESP_LOGE(TAG, "Camera initialization failed: %s", esp_err_to_name(initialization_result));
        return initialization_result;
    }

    initialized = true;
    ESP_LOGI(TAG, "OV5640 camera initialized at VGA JPEG, 10 MHz XCLK");
    return initialization_result;
}

bool turret_camera_is_ready(void)
{
    return initialized;
}

camera_fb_t *turret_camera_acquire_frame(void)
{
    if (!initialized) {
        return NULL;
    }
    return esp_camera_fb_get();
}

void turret_camera_return_frame(camera_fb_t *frame)
{
    if (frame != NULL) {
        esp_camera_fb_return(frame);
    }
}