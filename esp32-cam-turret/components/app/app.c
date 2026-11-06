#include "app.h"

#include "cam.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_camera.h"
#include "servo.h"
#include "web_ui.h"

static const char *TAG = "turret_app";

#if CONFIG_CAM_ENABLE && CONFIG_CAM_CAPTURE_TEST
static esp_err_t run_camera_capture_test(void)
{
    const esp_err_t init_err = turret_camera_init();
    if (init_err != ESP_OK) {
        ESP_LOGE(TAG, "Camera-only diagnostic could not initialize camera: %s",
                 esp_err_to_name(init_err));
        return ESP_OK;
    }

    unsigned successful_frames = 0;
    for (unsigned attempt = 1; attempt <= 3; attempt++) {
        camera_fb_t *frame = turret_camera_acquire_frame();
        if (frame == NULL) {
            ESP_LOGE(TAG, "Camera-only diagnostic capture %u/3 failed", attempt);
            continue;
        }

        ESP_LOGI(TAG, "Camera-only diagnostic capture %u/3 succeeded: %ux%u, %u bytes",
                 attempt, (unsigned)frame->width, (unsigned)frame->height,
                 (unsigned)frame->len);
        successful_frames++;
        turret_camera_return_frame(frame);
    }

    ESP_LOGI(TAG, "Camera-only diagnostic finished: %u/3 frames captured",
             successful_frames);
    return ESP_OK;
}
#endif

esp_err_t app_start(void)
{
    ESP_RETURN_ON_ERROR(nvs_flash_init(), TAG, "NVS initialization failed");
#if CONFIG_CAM_ENABLE && CONFIG_CAM_CAPTURE_TEST
    return run_camera_capture_test();
#endif
    ESP_RETURN_ON_ERROR(servo_init((gpio_num_t)CONFIG_APP_SERVO_GPIO), TAG, "Servo initialization failed");
    ESP_RETURN_ON_ERROR(servo_set_speed(0), TAG, "Servo neutral command failed");
#if CONFIG_CAM_ENABLE
    ESP_LOGI(TAG, "Camera initialization is deferred until its separate stream is requested");
#else
    ESP_LOGI(TAG, "Camera disabled; starting control page without video");
#endif
    ESP_RETURN_ON_ERROR(web_ui_start(), TAG, "Web control initialization failed");
    ESP_LOGI(TAG, "Base servo ready on GPIO %d", CONFIG_APP_SERVO_GPIO);
    return ESP_OK;
}