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

void app_main(void)

{

    i2c_init();

    ssd1306_config_t dev_cfg = I2C_SSD1306_128x32_CONFIG_DEFAULT;
    ssd1306_handle_t dev_hdl;

    ssd1306_init(i2c0_bus_hdl, &dev_cfg, &dev_hdl);
    if (dev_hdl == NULL)
    {
        ESP_LOGE(TAG, "ssd1306 handle init failed");
        assert(dev_hdl);
    }

    //
    int center = 1, top = 0, bottom = 1;
    char lineChar[16];
    uint8_t image[24];

    ESP_LOGI(TAG, "Panel is 128x64");

    // Display x3 text

    ESP_LOGI(TAG, "Horizontal Scroll");
    ssd1306_clear_display(dev_hdl, false);
    ssd1306_set_contrast(dev_hdl, 0xff);
    ssd1306_display_text_x2(dev_hdl, 0, "RMF FM", false);
   // ssd1306_set_hardware_scroll(dev_hdl, SSD1306_SCROLL_LEFT, SSD1306_SCROLL_64_FRAMES);

    ESP_LOGI(TAG, "Horizontal Scroll");

    ssd1306_display_text(dev_hdl, 2, "192.168.188.61", false);
  
  

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

    esp_err_t ret1 = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ret1 == ESP_OK)
    {
        ESP_LOGI("WiFi", "Wi-Fi power saving disabled.");
    }
    else
    {
        ESP_LOGE("WiFi", "Failed to disable Wi-Fi power saving: %d", ret1);
    }

   
    audio_init();
    audio_start(set);
    mount_fs();
    init_server();
    start_mdns_service();
    xTaskCreatePinnedToCore(stream_task, "stream_task", (10 * 1024), NULL, 7, NULL, 1);
    xTaskCreatePinnedToCore(check_memory_task, "check_memory_task", (4 * 1024), NULL, 6, NULL, 1);

    // esp_log_level_set("*", ESP_LOG_WARN);
}
