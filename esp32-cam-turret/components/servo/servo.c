#include "servo.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/ledc.h"

#define SERVO_PWM_FREQUENCY_HZ 50
#define SERVO_DUTY_RESOLUTION LEDC_TIMER_14_BIT
#define SERVO_DUTY_MAX_COUNTS 16383U
#define SERVO_MIN_ANGLE_DEGREES -90.0f
#define SERVO_MAX_ANGLE_DEGREES 90.0f
#define SERVO_MIN_PULSE_US 500U
#define SERVO_MAX_PULSE_US 2500U

static bool initialized;
static float current_angle_degrees;

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

    initialized = true;
    current_angle_degrees = 0.0f;
    return ESP_OK;
}

esp_err_t servo_set_angle(float angle_degrees)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (angle_degrees < SERVO_MIN_ANGLE_DEGREES || angle_degrees > SERVO_MAX_ANGLE_DEGREES) {
        return ESP_ERR_INVALID_ARG;
    }

    const float normalized_angle = (angle_degrees + 90.0f) / 180.0f;
    const uint32_t pulse_width_us = SERVO_MIN_PULSE_US +
                                    (uint32_t)((SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * normalized_angle);
    const uint32_t duty = (pulse_width_us * SERVO_PWM_FREQUENCY_HZ * SERVO_DUTY_MAX_COUNTS) / 1000000U;

    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    if (err != ESP_OK) {
        return err;
    }
    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    if (err == ESP_OK) {
        current_angle_degrees = angle_degrees;
    }
    return err;
}

esp_err_t servo_get_angle(float *angle_degrees)
{
    if (angle_degrees == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    *angle_degrees = current_angle_degrees;
    return ESP_OK;
}