#ifndef COMMON_WIFI_H
#define COMMON_WIFI_H

#include "esp_http_server.h"
#include "cJSON.h"
#include "esp_wifi.h"

#define EXAMPLE_ESP_WIFI_AP_SSID "ESP32_ConfigAP"
#define EXAMPLE_ESP_WIFI_AP_PASS "configuration"
#define EXAMPLE_ESP_WIFI_AP_CHANNEL 1
#define EXAMPLE_MAX_STA_CONN 4
#define STATION_MODE_ENABLED     "WIFI_STA_DEF"
#define AP_MODE_ENABLED          "WIFI_AP_DEF"
#define STORAGE_NAMESPACE "wifi_config"
#define SSID_KEY "sta_ssid"
#define PASSWORD_KEY "sta_password"
#define MAX_AP_COUNT 20



void print_ip_address(const char *mode);


#endif
