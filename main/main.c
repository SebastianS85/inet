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
// #include "mdns.h"
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

void print_ip_address()
{
    esp_netif_ip_info_t ip_info;

    // Get the default netif (interface)
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    if (netif == NULL)
    {
        ESP_LOGE(TAG, "No network interface found!");
        return;
    }

    // Get the IP info
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK)
    {
        char ip_str[32];
        snprintf(ip_str, sizeof(ip_str), " " IPSTR, IP2STR(&ip_info.ip));

        display_set_text(ip_str, 0, false);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to get IP information");
        display_set_text("No IP check wifi", 0, false);
    }

}


void app_main(void)

{

    i2c_init();
    init_display();
    display_clear();
    display_set_contrast(0x80);
    display_set_text("Connecting to wifi..", 0, false);

    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
    ESP_ERROR_CHECK(esp_netif_init());

#else
    tcpip_adapter_init();
#endif

    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    periph_wifi_cfg_t wifi_cfg = {
        .wifi_config.sta.ssid = CONFIG_WIFI_SSID,
        .wifi_config.sta.password = CONFIG_WIFI_PASSWORD,
    };

    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
    esp_periph_start(set, wifi_handle);
    periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);
    display_clear();
    display_set_text("wifi connected", 0, false);
    print_ip_address();
    display_set_text("  radio.local    ", 2, true);

    esp_err_t ret1 = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ret1 == ESP_OK)
    {
        ESP_LOGI("WiFi", "Wi-Fi power saving disabled.");
    }
    else
    {
        ESP_LOGE("WiFi", "Failed to disable Wi-Fi power saving: %d", ret1);
    }

    mount_fs();
    load_stations();
    audio_init();
    audio_start(set);
    init_server();
    start_mdns_service();
    
   
    xTaskCreatePinnedToCore(stream_task, "stream_task", (10 * 1024), NULL, 7, NULL, 1);
    xTaskCreatePinnedToCore(check_memory_task, "check_memory_task", (4 * 1024), NULL, 6, NULL, 1);

    // esp_log_level_set("*", ESP_LOG_WARN);
}
