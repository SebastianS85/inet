/* Play M3U HTTP Living stream

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_wifi.h"
#include "freertos/freertos.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio.h"
#include "server.h"

#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "mdns.h"
#include "esp_http_server.h"
#include "esp_phy_init.h"
#include "esp_system.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "ssd1306.h"
#include "i2c_master_ext.h"
#include "driver/i2c_master.h"
#include "i2c.h"
#include "audio_idf_version.h"
#include "display.h"
#include "cJSON.h"
#include "softap.h"
#include "common_wifi.h"




// NVS storage keys



static const char *BASE_PATH = "/store";

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif

static const char *TAG = "HTTP_LIVINGSTREAM_EXAMPLE";

void start_mdns_service()
{
    mdns_init();
    mdns_hostname_set("radio");
    mdns_instance_name_set("LEARN esp32 thing");
}
void check_memory_task(void *pvParameter)
{
    while (1)
    {
        size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        size_t min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
        ESP_LOGI(TAG, "Free heap: %d bytes", free_heap);
        ESP_LOGI(TAG, "Minimum free heap: %d bytes", min_free_heap);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void mount_fs()
{
    esp_vfs_fat_mount_config_t fat_mount_config = {
        .max_files = 5,
        .format_if_mount_failed = true,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
    };
    esp_vfs_fat_spiflash_mount_ro(BASE_PATH, "storage", &fat_mount_config);
}

// Wi-Fi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, 
                                int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        printf("Station "MACSTR" joined, AID=%d\n", 
               MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        printf("Station "MACSTR" left, AID=%d\n", 
               MAC2STR(event->mac), event->aid);
    }
}


// Initialize Wi-Fi in Access Point mode

void app_main(void) {
    i2c_init();
    init_display();
    display_clear();
    display_set_contrast(0x80);
    display_set_text("Connecting to wifi..", 0, false);

    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
    ESP_ERROR_CHECK(esp_netif_init());
#else
    tcpip_adapter_init();
#endif

    // Check if Wi-Fi credentials are stored
    size_t ssid_len = 0, password_len = 0;
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &nvs_handle));

    esp_err_t ssid_err = nvs_get_str(nvs_handle, SSID_KEY, NULL, &ssid_len);
    esp_err_t password_err = nvs_get_str(nvs_handle, PASSWORD_KEY, NULL, &password_len);
    nvs_close(nvs_handle);
    //delete_wifi_handler(NULL);

    if (ssid_err == ESP_OK && password_err == ESP_OK && ssid_len > 0 && password_len > 0) {
        ESP_LOGI(TAG, "Wi-Fi credentials found in NVS");
        
        // Get the stored credentials
        char *ssid = malloc(ssid_len);
        char *password = malloc(password_len);
        
        ESP_ERROR_CHECK(nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &nvs_handle));
        ESP_ERROR_CHECK(nvs_get_str(nvs_handle, SSID_KEY, ssid, &ssid_len));
        ESP_ERROR_CHECK(nvs_get_str(nvs_handle, PASSWORD_KEY, password, &password_len));
        nvs_close(nvs_handle);
        
        // Initialize Wi-Fi with stored credentials
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
        esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
        esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);        periph_wifi_cfg_t wifi_cfg = {0};  // Initialize structure to zero
        memcpy(wifi_cfg.wifi_config.sta.ssid, ssid, ssid_len);
        memcpy(wifi_cfg.wifi_config.sta.password, password, password_len);
        

        esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
        esp_periph_start(set, wifi_handle);
        periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);

        // Free allocated memory
        free(ssid);
        free(password);

        display_clear();
        display_set_text("wifi connected", 0, false);
        print_ip_address(STATION_MODE_ENABLED);
        display_set_text("  radio.local    ", 2, true);

        esp_err_t ret1 = esp_wifi_set_ps(WIFI_PS_NONE);
        if (ret1 == ESP_OK) {
            ESP_LOGI("WiFi", "Wi-Fi power saving disabled.");
        } else {
            ESP_LOGE("WiFi", "Failed to disable Wi-Fi power saving: %d", ret1);
        }

        mount_fs();
        load_stations();
        audio_init();
        audio_start(set);
        init_server();
        start_mdns_service();

        xTaskCreatePinnedToCore(stream_task, "stream_task", (10 * 1024), NULL, 7, NULL, 1);
    } else {
        // Start in SoftAP mode
        wifi_init_softap();
    }
}
