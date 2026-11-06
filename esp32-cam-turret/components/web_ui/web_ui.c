#include "web_ui.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "servo.h"

#define REQUEST_BODY_MAX_LENGTH 64
#define BASE_COMMAND_TIMEOUT_MS 700
#if CONFIG_CAM_ENABLE
#include "cam.h"
#define STREAM_HTTP_PORT 81
#define STREAM_BOUNDARY "turret-frame-boundary"
#endif

static const char *TAG = "turret_web";
static httpd_handle_t server;
static TimerHandle_t base_command_timer;
#if CONFIG_CAM_ENABLE
static httpd_handle_t stream_server;
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" STREAM_BOUNDARY;
static const char *STREAM_PART_BOUNDARY = "\r\n--" STREAM_BOUNDARY "\r\n";
static const char *STREAM_PART_HEADER = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
#endif

extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        const esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start connecting to local Wi-Fi: %s", esp_err_to_name(err));
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = (const wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Disconnected from local Wi-Fi (reason %d); retrying", event->reason);
        const esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to retry local Wi-Fi connection: %s", esp_err_to_name(err));
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected to local Wi-Fi; open http://" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static esp_err_t index_handler(httpd_req_t *request)
{
    ESP_LOGI(TAG, "Serving control page");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(request, "text/html; charset=utf-8"),
                        TAG, "Failed to set control page content type");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(request, "Content-Encoding", "gzip"),
                        TAG, "Failed to set control page compression header");
    const size_t page_length = (size_t)(index_html_gz_end - index_html_gz_start);
    const esp_err_t err = httpd_resp_send(request, (const char *)index_html_gz_start, page_length);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send control page: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t send_error(httpd_req_t *request, const char *status, const char *message)
{
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, message);
}

static void base_command_timeout_callback(TimerHandle_t timer)
{
    (void)timer;
    const esp_err_t err = servo_set_speed(0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop base servo after command timeout: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGW(TAG, "Base command timed out; servo stopped");
}

static esp_err_t get_base_handler(httpd_req_t *request)
{
    int speed_percent;
    const esp_err_t err = servo_get_speed(&speed_percent);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read base servo command: %s", esp_err_to_name(err));
        return send_error(request, "500 Internal Server Error", "{\"error\":\"Failed to read base servo command.\"}");
    }

    char response[32];
    const int response_length = snprintf(response, sizeof(response), "{\"speed\":%d}", speed_percent);
    if (response_length < 0 || (size_t)response_length >= sizeof(response)) {
        return send_error(request, "500 Internal Server Error", "{\"error\":\"Failed to format base command.\"}");
    }

    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, response, response_length);
}

static esp_err_t set_base_handler(httpd_req_t *request)
{
    if (request->content_len == 0 || request->content_len > REQUEST_BODY_MAX_LENGTH) {
        return send_error(request, "400 Bad Request", "{\"error\":\"Expected a small body containing a speed command.\"}");
    }

    char body[REQUEST_BODY_MAX_LENGTH + 1];
    size_t received = 0;
    while (received < request->content_len) {
        const int result = httpd_req_recv(request, body + received, request->content_len - received);
        if (result <= 0) {
            return send_error(request, "400 Bad Request", "{\"error\":\"Could not read request body.\"}");
        }
        received += (size_t)result;
    }
    body[received] = '\0';

    errno = 0;
    char *end = NULL;
    const long requested_speed = strtol(body, &end, 10);
    if (end == body || errno == ERANGE || *end != '\0' || requested_speed < -100 || requested_speed > 100) {
        return send_error(request, "400 Bad Request", "{\"error\":\"Speed must be a whole number from -100 to 100.\"}");
    }

    int current_speed;
    esp_err_t err = servo_get_speed(&current_speed);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read current base servo speed: %s", esp_err_to_name(err));
        return send_error(request, "500 Internal Server Error", "{\"error\":\"Base servo state could not be read.\"}");
    }

    err = servo_set_speed((int)requested_speed);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set base servo speed to %ld%%: %s",
                 requested_speed, esp_err_to_name(err));
        return send_error(request, "500 Internal Server Error", "{\"error\":\"Base servo could not be updated.\"}");
    }
    if (requested_speed != current_speed) {
        ESP_LOGI(TAG, "Base servo command: %ld%%", requested_speed);
    }

    if (requested_speed == 0) {
        if (xTimerStop(base_command_timer, pdMS_TO_TICKS(10)) != pdPASS) {
            ESP_LOGE(TAG, "Failed to stop base command timeout timer");
            return send_error(request, "500 Internal Server Error", "{\"error\":\"Could not stop base command timer.\"}");
        }
    } else if (xTimerChangePeriod(base_command_timer,
                                  pdMS_TO_TICKS(BASE_COMMAND_TIMEOUT_MS),
                                  pdMS_TO_TICKS(10)) != pdPASS) {
        const esp_err_t stop_err = servo_set_speed(0);
        if (stop_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop base servo after timer error: %s", esp_err_to_name(stop_err));
        }
        ESP_LOGE(TAG, "Failed to refresh base command timeout timer");
        return send_error(request, "500 Internal Server Error", "{\"error\":\"Could not refresh base command timer.\"}");
    }

    char response[32];
    const int response_length = snprintf(response, sizeof(response), "{\"speed\":%ld}", requested_speed);
    if (response_length < 0 || (size_t)response_length >= sizeof(response)) {
        return send_error(request, "500 Internal Server Error", "{\"error\":\"Failed to format base command.\"}");
    }

    ESP_LOGD(TAG, "Base servo speed command: %ld%%", requested_speed);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, response, response_length);
}

#if CONFIG_CAM_ENABLE
static esp_err_t camera_stream_handler(httpd_req_t *request)
{
    if (!turret_camera_is_ready()) {
        const esp_err_t init_err = turret_camera_init();
        if (init_err != ESP_OK) {
            httpd_resp_set_status(request, "503 Service Unavailable");
            httpd_resp_set_type(request, "text/plain");
            ESP_LOGW(TAG, "Camera stream request failed during initialization: %s",
                     esp_err_to_name(init_err));
            return httpd_resp_sendstr(request, "Camera is unavailable; control page is unaffected.");
        }
    }

    ESP_RETURN_ON_ERROR(httpd_resp_set_type(request, STREAM_CONTENT_TYPE),
                        TAG, "Failed to set camera stream content type");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(request, "Cache-Control", "no-store"),
                        TAG, "Failed to set camera stream cache header");

    char part_header[64];
    bool stream_started = false;
    while (true) {
        camera_fb_t *frame = turret_camera_acquire_frame();
        if (frame == NULL) {
            ESP_LOGE(TAG, "Camera frame capture failed");
            if (!stream_started) {
                return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                           "Camera frame capture failed");
            }
            httpd_resp_send_chunk(request, NULL, 0);
            return ESP_FAIL;
        }
        if (frame->format != PIXFORMAT_JPEG || frame->len < 2 ||
            frame->buf[0] != 0xff || frame->buf[1] != 0xd8) {
            ESP_LOGE(TAG, "Camera returned an invalid JPEG frame (format=%d, length=%u)",
                     frame->format, (unsigned int)frame->len);
            turret_camera_return_frame(frame);
            if (!stream_started) {
                return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                           "Camera returned an invalid JPEG frame");
            }
            httpd_resp_send_chunk(request, NULL, 0);
            return ESP_FAIL;
        }

        esp_err_t err = httpd_resp_send_chunk(request, STREAM_PART_BOUNDARY, strlen(STREAM_PART_BOUNDARY));
        if (err == ESP_OK) {
            stream_started = true;
        }
        if (err == ESP_OK) {
            const int header_length = snprintf(part_header, sizeof(part_header), STREAM_PART_HEADER,
                                               (unsigned int)frame->len);
            if (header_length < 0 || (size_t)header_length >= sizeof(part_header)) {
                ESP_LOGE(TAG, "Camera stream frame header exceeded its buffer");
                err = ESP_FAIL;
            } else {
                err = httpd_resp_send_chunk(request, part_header, (size_t)header_length);
            }
        }
        if (err == ESP_OK) {
            err = httpd_resp_send_chunk(request, (const char *)frame->buf, frame->len);
        }
        turret_camera_return_frame(frame);
        if (err != ESP_OK) {
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
#endif

esp_err_t web_ui_start(void)
{
    const size_t ssid_length = strlen(CONFIG_WEB_UI_AP_SSID);
    const size_t password_length = strlen(CONFIG_WEB_UI_AP_PASSWORD);
    const size_t station_ssid_length = strlen(CONFIG_WEB_UI_STA_SSID);
    const size_t station_password_length = strlen(CONFIG_WEB_UI_STA_PASSWORD);
    const bool station_enabled = station_ssid_length > 0;

    if (ssid_length == 0 || ssid_length > 32 || password_length < 8 || password_length > 63) {
        ESP_LOGE(TAG, "SoftAP SSID must be 1-32 characters and password 8-63 characters");
        return ESP_ERR_INVALID_ARG;
    }
    if (station_ssid_length > 32 ||
        (station_enabled && station_password_length > 0 &&
         (station_password_length < 8 || station_password_length > 63))) {
        ESP_LOGE(TAG, "Local Wi-Fi SSID must be at most 32 characters and password empty or 8-63 characters");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Network interface initialization failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Default event loop creation failed");
    if (esp_netif_create_default_wifi_ap() == NULL) {
        return ESP_FAIL;
    }
    if (station_enabled && esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_FAIL;
    }

    const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "Wi-Fi driver initialization failed");
    const wifi_mode_t wifi_mode = station_enabled ? WIFI_MODE_APSTA : WIFI_MODE_AP;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(wifi_mode), TAG, "Wi-Fi mode configuration failed");

    wifi_config_t ap_config = {0};
    memcpy(ap_config.ap.ssid, CONFIG_WEB_UI_AP_SSID, ssid_length);
    memcpy(ap_config.ap.password, CONFIG_WEB_UI_AP_PASSWORD, password_length);
    ap_config.ap.ssid_len = (uint8_t)ssid_length;
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "SoftAP credentials configuration failed");

    if (station_enabled) {
        wifi_config_t station_config = {0};
        memcpy(station_config.sta.ssid, CONFIG_WEB_UI_STA_SSID, station_ssid_length);
        memcpy(station_config.sta.password, CONFIG_WEB_UI_STA_PASSWORD, station_password_length);
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &station_config),
                            TAG, "Local Wi-Fi credentials configuration failed");
        ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL),
                            TAG, "Wi-Fi event handler registration failed");
        ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL),
                            TAG, "IP event handler registration failed");
        ESP_LOGI(TAG, "Connecting to local Wi-Fi network '%s'", CONFIG_WEB_UI_STA_SSID);
    }

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "SoftAP start failed");

    base_command_timer = xTimerCreate("base_stop",
                                      pdMS_TO_TICKS(BASE_COMMAND_TIMEOUT_MS),
                                      pdFALSE,
                                      NULL,
                                      base_command_timeout_callback);
    if (base_command_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create base command timeout timer");
        return ESP_ERR_NO_MEM;
    }

    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    // Both HTTP servers share LWIP_MAX_SOCKETS; leave room for their internal sockets.
    server_config.max_open_sockets = 2;
    server_config.lru_purge_enable = true;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &server_config), TAG, "HTTP server start failed");

    const httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
    };
    const httpd_uri_t get_base_uri = {
        .uri = "/api/base",
        .method = HTTP_GET,
        .handler = get_base_handler,
    };
    const httpd_uri_t set_base_uri = {
        .uri = "/api/base",
        .method = HTTP_POST,
        .handler = set_base_handler,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &index_uri), TAG, "Control page route registration failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &get_base_uri), TAG, "Base read route registration failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &set_base_uri), TAG, "Base control route registration failed");
    ESP_LOGI(TAG, "HTTP control server started on port %d", server_config.server_port);

#if CONFIG_CAM_ENABLE
    httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
    stream_config.server_port = STREAM_HTTP_PORT;
    stream_config.ctrl_port = server_config.ctrl_port + 1;
    stream_config.max_open_sockets = 1;
    stream_config.lru_purge_enable = true;
    esp_err_t stream_err = httpd_start(&stream_server, &stream_config);
    if (stream_err == ESP_OK) {
        const httpd_uri_t camera_stream_uri = {
            .uri = "/stream",
            .method = HTTP_GET,
            .handler = camera_stream_handler,
        };
        stream_err = httpd_register_uri_handler(stream_server, &camera_stream_uri);
        if (stream_err != ESP_OK) {
            ESP_LOGW(TAG, "Camera stream route registration failed: %s",
                     esp_err_to_name(stream_err));
            httpd_stop(stream_server);
            stream_server = NULL;
        }
    } else {
        ESP_LOGW(TAG, "Camera stream server start failed: %s", esp_err_to_name(stream_err));
    }
    if (stream_err == ESP_OK) {
        ESP_LOGI(TAG, "Separate camera stream available on HTTP port %d", STREAM_HTTP_PORT);
    }
#endif

    ESP_LOGI(TAG, "Control Wi-Fi network '%s' started; fallback address is http://192.168.4.1",
             CONFIG_WEB_UI_AP_SSID);
    return ESP_OK;
}
