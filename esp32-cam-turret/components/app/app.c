#include "app.h"

#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "servo.h"
#include "web_ui.h"

static const char *TAG = "turret_app";

esp_err_t app_start(void)
{
    ESP_RETURN_ON_ERROR(nvs_flash_init(), TAG, "NVS initialization failed");
    ESP_RETURN_ON_ERROR(servo_init((gpio_num_t)CONFIG_APP_SERVO_GPIO), TAG, "Servo initialization failed");
    ESP_RETURN_ON_ERROR(servo_set_angle(0.0f), TAG, "Servo initial position failed");
    ESP_RETURN_ON_ERROR(web_ui_start(), TAG, "Web control initialization failed");
    ESP_LOGI(TAG, "Tilt servo ready on GPIO %d", CONFIG_APP_SERVO_GPIO);
    return ESP_OK;
}