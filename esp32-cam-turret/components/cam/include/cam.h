#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_camera.h"

esp_err_t turret_camera_init(void);
bool turret_camera_is_ready(void);
camera_fb_t *turret_camera_acquire_frame(void);
void turret_camera_return_frame(camera_fb_t *frame);