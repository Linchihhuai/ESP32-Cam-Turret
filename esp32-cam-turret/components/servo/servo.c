#include "servo.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define SERVO_PWM_FREQUENCY_HZ 50
#define SERVO_DUTY_RESOLUTION LEDC_TIMER_14_BIT
#define SERVO_DUTY_MAX_COUNTS 16383U
#define SERVO_MIN_SPEED_PERCENT -100
#define SERVO_MAX_SPEED_PERCENT 100
#define SERVO_NEUTRAL_PULSE_US 1500
#define SERVO_PULSE_US_PER_PERCENT 10

static bool initialized;
static int current_speed_percent;
static SemaphoreHandle_t servo_mutex;

esp_err_t servo_init(gpio_num_t signal_gpio)
{
    if (signal_gpio < 0 || signal_gpio >= GPIO_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = SERVO_DUTY_RESOLUTION,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = SERVO_PWM_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_config);
    if (err != ESP_OK) {
        return err;
    }

    const ledc_channel_config_t channel_config = {
        .gpio_num = signal_gpio,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel_config);
    if (err != ESP_OK) {
        return err;
    }

    servo_mutex = xSemaphoreCreateMutex();
    if (servo_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    initialized = true;
    current_speed_percent = 0;
    return ESP_OK;
}

esp_err_t servo_set_speed(int speed_percent)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (speed_percent < SERVO_MIN_SPEED_PERCENT || speed_percent > SERVO_MAX_SPEED_PERCENT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(servo_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    const uint32_t pulse_width_us = (uint32_t)(SERVO_NEUTRAL_PULSE_US +
                                               speed_percent * SERVO_PULSE_US_PER_PERCENT);
    const uint32_t duty = (pulse_width_us * SERVO_PWM_FREQUENCY_HZ * SERVO_DUTY_MAX_COUNTS) / 1000000U;

    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    if (err != ESP_OK) {
        xSemaphoreGive(servo_mutex);
        return err;
    }
    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    if (err == ESP_OK) {
        current_speed_percent = speed_percent;
    }
    xSemaphoreGive(servo_mutex);
    return err;
}

esp_err_t servo_get_speed(int *speed_percent)
{
    if (speed_percent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(servo_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    *speed_percent = current_speed_percent;
    xSemaphoreGive(servo_mutex);
    return ESP_OK;
}