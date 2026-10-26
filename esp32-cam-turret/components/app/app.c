#include "app.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "servo.h"

#define SWEEP_DWELL_MS 700

static const char *TAG = "turret_app";
static const float servo_positions[] = {-90.0f, 0.0f, -90.0f, 0.0f, 90.0f, 0.0f, 90.0f, 0.0f};

static void servo_test_task(void *arg)
{
    (void)arg;

    while (true) {
        for (size_t index = 0; index < sizeof(servo_positions) / sizeof(servo_positions[0]); index++) {
            const float angle = servo_positions[index];
            ESP_LOGI(TAG, "Servo angle: %.0f degrees", (double)angle);
            ESP_ERROR_CHECK(servo_set_angle(angle));
            vTaskDelay(pdMS_TO_TICKS(SWEEP_DWELL_MS));
        }
    }
}

esp_err_t app_start(void)
{
    ESP_RETURN_ON_ERROR(servo_init((gpio_num_t)CONFIG_APP_SERVO_GPIO), TAG, "Servo initialization failed");
    ESP_RETURN_ON_ERROR(servo_set_angle(0.0f), TAG, "Servo initial position failed");

    if (xTaskCreate(servo_test_task, "servo_test", 2048, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Servo test started on GPIO %d", CONFIG_APP_SERVO_GPIO);
    return ESP_OK;
}