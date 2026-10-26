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
#include "servo.h"

#define REQUEST_BODY_MAX_LENGTH 64

static const char *TAG = "turret_web";
static httpd_handle_t server;

extern const uint8_t index_html_start[] asm("_binary_index_html_start");

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
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, (const char *)index_html_start, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_error(httpd_req_t *request, const char *status, const char *message)
{
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, message);
}

static esp_err_t get_tilt_handler(httpd_req_t *request)
{
    float angle_degrees;
    const esp_err_t err = servo_get_angle(&angle_degrees);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read tilt servo position: %s", esp_err_to_name(err));
        return send_error(request, "500 Internal Server Error", "{\"error\":\"Failed to read tilt servo position.\"}");
    }

    char response[32];
    const int response_length = snprintf(response, sizeof(response), "{\"angle\":%.0f}", (double)angle_degrees);
    if (response_length < 0 || (size_t)response_length >= sizeof(response)) {
        return send_error(request, "500 Internal Server Error", "{\"error\":\"Failed to format tilt position.\"}");
    }

    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, response, response_length);
}

static esp_err_t set_tilt_handler(httpd_req_t *request)
{
    if (request->content_len == 0 || request->content_len > REQUEST_BODY_MAX_LENGTH) {
        return send_error(request, "400 Bad Request", "{\"error\":\"Expected a small body containing an angle.\"}");
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
    const long requested_angle = strtol(body, &end, 10);
    if (end == body || errno == ERANGE || *end != '\0' || requested_angle < -90 || requested_angle > 90) {
        return send_error(request, "400 Bad Request", "{\"error\":\"Angle must be a whole number from -90 to 90.\"}");
    }

    const float angle_degrees = (float)requested_angle;

    const esp_err_t err = servo_set_angle(angle_degrees);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set tilt servo to %.0f degrees: %s",
                 (double)angle_degrees, esp_err_to_name(err));
        return send_error(request, "500 Internal Server Error", "{\"error\":\"Tilt servo could not be updated.\"}");
    }

    ESP_LOGI(TAG, "Tilt servo set to %.0f degrees", (double)angle_degrees);

    char response[32];
    const int response_length = snprintf(response, sizeof(response), "{\"angle\":%ld}", requested_angle);
    if (response_length < 0 || (size_t)response_length >= sizeof(response)) {
        return send_error(request, "500 Internal Server Error", "{\"error\":\"Failed to format tilt position.\"}");
    }

    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, response, response_length);
}

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

    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    ESP_RETURN_ON_ERROR(httpd_start(&server, &server_config), TAG, "HTTP server start failed");

    const httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
    };
    const httpd_uri_t get_tilt_uri = {
        .uri = "/api/tilt",
        .method = HTTP_GET,
        .handler = get_tilt_handler,
    };
    const httpd_uri_t set_tilt_uri = {
        .uri = "/api/tilt",
        .method = HTTP_POST,
        .handler = set_tilt_handler,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &index_uri), TAG, "Control page route registration failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &get_tilt_uri), TAG, "Tilt read route registration failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &set_tilt_uri), TAG, "Tilt control route registration failed");

    ESP_LOGI(TAG, "Control Wi-Fi network '%s' started; fallback address is http://192.168.4.1",
             CONFIG_WEB_UI_AP_SSID);
    return ESP_OK;
}
