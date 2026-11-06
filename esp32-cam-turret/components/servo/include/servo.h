#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

esp_err_t servo_init(gpio_num_t signal_gpio);
esp_err_t servo_set_speed(int speed_percent);
esp_err_t servo_get_speed(int *speed_percent);